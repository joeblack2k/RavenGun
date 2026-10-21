#include "output_service.hpp"

#include <chrono>
#include <unistd.h>

#include "ravengun/json.hpp"
#include "ravengun/output/key_table.hpp"
#include "shared.hpp"

namespace ravengund {

using namespace ravengun::output;
using std::chrono::duration;
using std::chrono::steady_clock;

OutputService::OutputService(GunFactory guns, std::unique_ptr<KeyDevice> keyboard)
    : kbd_(keyboard ? std::move(keyboard) : std::make_unique<UinputKeyboard>()), mux_(*kbd_) {
    for (int i = 0; i < kPlayers; ++i) {
        Player& p = players_[static_cast<size_t>(i)];
        p.number = i + 1;
        p.id = gun_identity(p.number);
        p.dev = guns ? guns(p.number) : nullptr;
        if (!p.dev) p.dev = std::make_unique<UinputGun>(p.id);
        p.out = std::make_unique<GunOutput>(*p.dev, &mux_.port(p.number));
    }
    mux_.set_wake([this] { clock_changed_ = true; clock_cv_.notify_all(); });   // runs under m_
    clock_thread_ = std::thread(&OutputService::clock_main, this);
}

void OutputService::set_key_min_hold(double ms) {
    std::lock_guard<std::mutex> g(m_);
    ravengun::output::KeyTiming t;
    if (ms > 0) {
        t.min_hold_ns = static_cast<int64_t>(ms * 1e6);
        t.min_gap_ns = static_cast<int64_t>(kKeyMinGapMs * 1e6);
    }
    mux_.set_timing(t);
}

OutputService::~OutputService() { shutdown(); }

ravengun::output::OutputClockConfig OutputService::clock_config() const {
    std::lock_guard<std::mutex> g(m_);
    return clock_cfg_;
}

void OutputService::set_clock(const ravengun::output::OutputClockConfig& c) {
    std::lock_guard<std::mutex> g(m_);
    clock_cfg_ = c;
    for (auto& p : players_) { p.clock.set_config(c); p.clock_idle = false; }
    clock_changed_ = true;
    clock_cv_.notify_all();
}

// Klokthread: tikt op een vast raster en zet per speler de geëxtrapoleerde aim. Houdt m_ alleen tijdens de berekening en de
// (niet-blokkerende) uinput-schrijfacties; de HID-thread levert de ankers via feed_live.
void OutputService::clock_main() {
    std::unique_lock<std::mutex> lk(m_);
    while (!shut_) {
        // Keys whose release (or next press) was delayed by the minimum hold time (key_mux.hpp).
        mux_.poll(mux_.now());
        const int64_t key_due = mux_.next_due();
        auto needs = [&](const Player& p) { return p.out->armed() && p.clock.anchor().valid && !p.clock_idle; };
        bool any = false;
        if (clock_cfg_.enabled() && armed_ && !pattern_running_)
            for (const auto& p : players_) any = any || needs(p);
        // Niets te doen (klok uit, ongewapend, patroon, geen anker of overal vastgehouden): slapen tot een rapport, een
        // instelling of het afsluiten haar wakker maakt (met een time-out als vangnet).
        if (!any) {
            auto idle = std::chrono::nanoseconds(std::chrono::milliseconds(50));
            if (key_due != KeyMux::kNever) idle = std::min(idle, std::chrono::nanoseconds(std::max<int64_t>(0, key_due - mux_.now())));
            clock_cv_.wait_for(lk, idle, [&] { return shut_ || clock_changed_; });
            clock_changed_ = false;
            continue;
        }
        // Alle spelers delen hetzelfde raster (dezelfde klokinstelling): de klok van speler 1 bepaalt het tikmoment.
        const int64_t next = p1().clock.next_tick_after(mono_ns());
        for (size_t i = 1; i < players_.size(); ++i) players_[i].clock.next_tick_after(mono_ns());
        const auto tp = steady_clock::time_point(std::chrono::nanoseconds(std::min(next, key_due)));
        if (clock_cv_.wait_until(lk, tp, [&] { return shut_ || clock_changed_; })) { clock_changed_ = false; continue; }
        if (shut_) break;
        if (key_due < next) continue;   // woken for a delayed key event: emit it at the top, tick on the grid
        if (!armed_ || pattern_running_) continue;
        const int64_t now = mono_ns();
        for (auto& p : players_) {
            if (!needs(p)) continue;
            auto pos = p.clock.tick(now);
            if (!pos) continue;
            ++p.clock_ticks;
            if (pos->extrapolating) ++p.clock_extrapolated;
            else if (pos->held) { ++p.clock_held; p.clock_idle = true; }  // staat vast: pas weer tikken na een nieuw rapport
            const uint64_t before = p.out->stats().position_events;
            p.out->set_position(pos->x, pos->y);
            if (p.out->stats().position_events != before) ++p.clock_events;
        }
    }
}

void OutputService::set_initial_transform(const GunTransform& t) {
    std::lock_guard<std::mutex> g(m_);
    for (auto& p : players_) p.out->set_transform(t);
}

GunTransform OutputService::transform() const {
    std::lock_guard<std::mutex> g(m_);
    return p1().out->transform();
}

void OutputService::stop_pattern_locked(std::unique_lock<std::mutex>& lk) {
    if (!pattern_thread_.joinable()) return;
    pattern_stop_ = true;
    cv_.notify_all();
    std::thread t = std::move(pattern_thread_);
    lk.unlock();
    t.join();
    lk.lock();
}

std::string OutputService::event_node(const Player& p) const {
    if (auto* u = dynamic_cast<const UinputGun*>(p.dev.get())) return u->event_node();
    return {};
}

void OutputService::arm_player_locked(Player& p) {
    std::string err;
    const bool was = p.out->armed();
    if (!was) p.ov.clear();  // een nieuwe arm begint schoon: geen synthetische stand van voor het disarmen
    if (!p.out->arm(err)) {
        logf("fout", "arm van speler %d mislukt: %s", p.number, err.c_str());
        return;
    }
    if (!was) logf("info", "virtuele lightgun speler %d: %s (%s)", p.number, p.id.name.c_str(), event_node(p).c_str());
}

bool OutputService::test_source_locked(Player& p) {
    if (!armed_) return false;
    test_ |= 1u << (p.number - 1);
    if (!p.out->armed()) arm_player_locked(p);
    return p.out->armed();
}

OutputResult OutputService::arm() {
    std::unique_lock<std::mutex> lk(m_);
    if (shut_) return {false, "unavailable", "uitvoer is afgesloten"};
    std::string err;
    const bool was_armed = armed_;
    if (!p1().out->armed()) p1().ov.clear();
    // Volgorde: eerst speler 1 (en daarmee het toetsenbord), dan de andere spelers met een Move.
    if (!p1().out->arm(err)) {
        logf("fout", "arm mislukt: %s", err.c_str());
        return {false, "unavailable", err};
    }
    armed_ = true;
    if (!was_armed) {
        auto* kb = dynamic_cast<UinputKeyboard*>(kbd_.get());
        logf("info", "uitvoer GEWAPEND: virtueel lightgun-apparaat %s (%s), toetsenbord %s (%s)", kDeviceName, event_node(p1()).c_str(),
             kKbdDeviceName, kb ? kb->event_node().c_str() : "");
        if (!p1().out->keyboard_error().empty()) logf("fout", "virtueel toetsenbord niet beschikbaar: %s", p1().out->keyboard_error().c_str());
    }
    for (size_t i = 1; i < players_.size(); ++i)
        if (should_exist(players_[i].number)) arm_player_locked(players_[i]);
    return {};
}

OutputResult OutputService::disarm() {
    std::unique_lock<std::mutex> lk(m_);
    stop_pattern_locked(lk);
    if (armed_) logf("info", "uitvoer ontwapend: knoppen vrijgegeven");
    for (auto& p : players_) { p.out->disarm(); p.clock.clear(); p.ov.clear(); }
    armed_ = false;
    test_ = 0;
    return {};
}

OutputResult OutputService::stop_pattern() {
    std::unique_lock<std::mutex> lk(m_);
    stop_pattern_locked(lk);
    p1().out->release_all();
    p1().ov.clear();  // ook een synthetische down van output-button/key
    return {};
}

void OutputService::release_all() {
    std::lock_guard<std::mutex> g(m_);
    for (auto& p : players_) { p.out->release_all(); p.ov.clear(); }
}

bool OutputService::armed() const {
    std::lock_guard<std::mutex> g(m_);
    return armed_ && p1().out->armed();
}

bool OutputService::aim_snapshot(int player, AimSnapshot& s) const {
    if (player < 1 || player > kPlayers) return false;
    std::lock_guard<std::mutex> g(m_);
    const Player& p = players_[static_cast<size_t>(player - 1)];
    if (shut_ || !armed_ || !should_exist(player) || !p.out->armed()) return false;
    s.have_position = p.out->have_position();
    if (s.have_position) {
        s.u = static_cast<double>(p.out->last_abs_x()) / kAbsMax;
        s.v = static_cast<double>(p.out->last_abs_y()) / kAbsMax;
    }
    s.trigger = p.out->button_down(GunButton::Trigger);
    s.reload = p.out->button_down(GunButton::Reload);
    return true;
}

bool OutputService::io_snapshot(ravengun::iobridge::IoState& s) const {
    using ravengun::iobridge::IoPlayer;
    s = {};
    std::lock_guard<std::mutex> g(m_);
    if (shut_ || !armed_) return false;
    s.armed = true;
    for (int n = 1; n <= kPlayers; ++n) {
        const Player& p = players_[static_cast<size_t>(n - 1)];
        IoPlayer& o = s.player[static_cast<size_t>(n - 1)];
        // De gamepad-signalen gelden ook voor een speler zonder eigen gun (alleen een gamepad).
        const ravengun::iobridge::IoPadInput& pad = pads_[static_cast<size_t>(n - 1)];
        o.pad = pad.pad;
        o.wheel_from_pad = pad.wheel_from_pad;
        o.wheel = pad.wheel;
        o.gas = pad.gas;
        o.brake = pad.brake;
        o.btn = pad.btn;
        if (!should_exist(n) || !p.out->armed()) continue;
        o.valid = p.out->have_position();
        if (o.valid) {
            o.x = static_cast<double>(p.out->last_abs_x()) / kAbsMax;
            o.y = static_cast<double>(p.out->last_abs_y()) / kAbsMax;
            o.offscreen = p.out->last_nx() < 0 || p.out->last_nx() > 1 || p.out->last_ny() < 0 || p.out->last_ny() > 1;
        }
        o.trigger = p.out->button_down(GunButton::Trigger);
        o.reload = p.out->button_down(GunButton::Reload);
        o.grenade = p.out->button_down(GunButton::Grenade);
        o.aux1 = p.out->button_down(GunButton::Aux1);
        o.aux2 = p.out->button_down(GunButton::Aux2);
        // Start van speler n = toets n (de standaardindeling), door elke speler vast te houden.
        o.start = mux_.down(KEY_1 + (n - 1));
        s.sys.coin[static_cast<size_t>(n - 1)] = mux_.held_by(n, KEY_5);
    }
    s.sys.test = mux_.down(KEY_F2) || mux_.down(KEY_0);
    s.sys.service = mux_.down(KEY_F3);
    // Menuknoppen van de kast (testmenu: "Enter" is bij veel indelingen de bit die wij volume omhoog noemen): F4 en F5.
    s.sys.volume_up = mux_.down(KEY_F4);
    s.sys.volume_down = mux_.down(KEY_F5);
    return true;
}

OutputResult OutputService::key(int player, int code, const std::string& action) {
    if (player < 1 || player > kPlayers) return {false, "invalid_argument", "ongeldige speler"};
    if (action != "down" && action != "up" && action != "tap") return {false, "invalid_argument", "actie moet down, up of tap zijn"};
    std::unique_lock<std::mutex> lk(m_);
    Player& p = players_[static_cast<size_t>(player - 1)];
    GunOutput& out = *p.out;
    if (!test_source_locked(p)) return {false, "unavailable", "uitvoer is niet gewapend: gebruik eerst 'arm'"};
    // Synthetisch (OR met de live stand): een live Move-rapport dat "los" zegt mag deze druk niet wegzetten. Na up of het
    // einde van de tap volgt de toets weer de live stand.
    if (action == "up") {
        p.ov.release_key(code);
        if (!out.set_key(code, p.ov.want_key(code))) return {false, "internal", "toets schrijven mislukt"};
        return {};
    }
    const uint64_t token = action == "tap" ? p.ov.tap_key(code) : 0;
    if (action == "down") p.ov.hold_key(code);
    if (!out.set_key(code, true)) { p.ov.release_key(code); return {false, "internal", "toets schrijven mislukt"}; }
    if (action == "tap") {
        cv_.wait_for(lk, std::chrono::milliseconds(80), [&] { return !out.armed(); });
        p.ov.end_tap_key(code, token);   // een nieuwere tap, een up of een disarm tussendoor laat dit ongemoeid
        if (out.armed()) out.set_key(code, p.ov.want_key(code));
    }
    return {};
}

void OutputService::set_active_players(unsigned mask) {
    std::lock_guard<std::mutex> g(m_);
    const unsigned now = (mask | 1u) & ((1u << kPlayers) - 1);
    const unsigned added = now & ~active_;
    active_ = now;
    if (!armed_ || shut_) return;
    for (auto& p : players_)
        if (added & (1u << (p.number - 1))) arm_player_locked(p);
}

void OutputService::set_mouse_players(unsigned mask) {
    std::lock_guard<std::mutex> g(m_);
    const unsigned now = mask & ~1u & ((1u << kPlayers) - 1);   // speler 1 bestaat altijd
    const unsigned added = now & ~(active_ | mouse_);
    mouse_ = now;
    if (!armed_ || shut_) return;
    for (auto& p : players_)
        if (added & (1u << (p.number - 1))) arm_player_locked(p);
}

unsigned OutputService::mouse_players() const {
    std::lock_guard<std::mutex> g(m_);
    return mouse_;
}

void OutputService::set_pad_input(int player, const ravengun::iobridge::IoPadInput& in) {
    if (player < 1 || player > kPlayers) return;
    std::lock_guard<std::mutex> g(m_);
    pads_[static_cast<size_t>(player - 1)] = in;
}

void OutputService::set_layout(int player, const std::vector<std::pair<std::string, std::string>>& layout) {
    if (player < 1 || player > kPlayers) return;
    std::lock_guard<std::mutex> g(m_);
    players_[static_cast<size_t>(player - 1)].layout = layout;
}

void OutputService::feed_live(int player, const LiveAction& a, int src) {
    if (player < 1 || player > kPlayers) return;
    std::lock_guard<std::mutex> g(m_);
    if (shut_) return;
    Player& p = players_[static_cast<size_t>(player - 1)];
    GunOutput& out = *p.out;
    ++p.live_samples;
    if (a.release_all) {
        p.live_aim_valid = false;
        bool any = false;
        for (int i = 0; i < kGunButtonCount; ++i) any = any || out.button_down(static_cast<GunButton>(i));
        any = any || !out.keys_down().empty();
        if (any) { out.release_all(); ++p.live_releases; }
        p.ov.clear();   // bronuitval: ook een synthetische druk gaat los (veilig boven bench-gemak)
        p.clock.clear();
        return;
    }
    if (!out.armed()) { p.clock.clear(); return; }
    if (pattern_running_ && player == pattern_player_ && !a.no_aim) { ++p.live_suspended; return; }  // testpatroon heeft voorrang (op de aim)
    if (a.no_aim) {
        // Gamepad: geen richtpunt, alleen knoppen en toetsen (hieronder).
    } else if ((p.live_aim_valid = a.move)) {
        if (p.clock.config().enabled()) {
            // Anker voor de uitvoerklok en meteen de positie op dit moment (anker + snelheid * leeftijd): geen wachttijd tot de volgende tik.
            const bool wake = p.clock_idle || !p.clock.anchor().valid;
            p.clock.on_report(a.t_ns ? a.t_ns : mono_ns(), a.x, a.y, a.vx, a.vy, a.age_ms);
            if (wake) { p.clock_idle = false; clock_changed_ = true; clock_cv_.notify_one(); }
            const auto pos = p.clock.at(mono_ns());
            out.set_position(pos ? pos->x : a.x, pos ? pos->y : a.y);
        } else {
            out.set_position(a.x, a.y);
        }
        ++p.live_positions;
    } else {
        p.clock.clear();
        ++p.live_invalid;  // ongeldige aim: niet bewegen (cursor blijft staan)
    }
    if (a.buttons) {
        // Live OR synthetisch: een rapport dat "los" meldt mag een lopende output-button/key-druk niet annuleren.
        // ... en per bron: een Move die "los" meldt laat een vastgehouden muisknop van dezelfde speler staan (en omgekeerd).
        out.set_button(GunButton::Trigger, p.ov.live_button(GunButton::Trigger, a.trigger, src));
        out.set_button(GunButton::Reload, p.ov.live_button(GunButton::Reload, a.reload, src));
        out.set_button(GunButton::Grenade, p.ov.live_button(GunButton::Grenade, a.grenade, src));
        out.set_button(GunButton::Aux1, p.ov.live_button(GunButton::Aux1, a.aux1, src));
        out.set_button(GunButton::Aux2, p.ov.live_button(GunButton::Aux2, a.aux2, src));
        for (const auto& k : a.keys) out.set_key(k.first, p.ov.live_key(k.first, k.second, src));
    }
}

void OutputService::shutdown() {
    std::unique_lock<std::mutex> lk(m_);
    if (shut_) return;
    shut_ = true;
    clock_cv_.notify_all();
    std::thread ct = std::move(clock_thread_);
    lk.unlock();
    if (ct.joinable()) ct.join();
    lk.lock();
    stop_pattern_locked(lk);
    for (auto& p : players_) p.out->shutdown();  // knoppen vrij, apparaat weg
    mux_.shutdown();                              // toetsen vrij, toetsenbord weg
    armed_ = false;
}

OutputResult OutputService::start_pattern(const PatternParams& p, int player) {
    if (player < 1 || player > kPlayers) return {false, "invalid_argument", "ongeldige speler"};
    std::string err;
    auto steps = generate_pattern(p, err);
    if (steps.empty()) return {false, "invalid_argument", err.empty() ? "ongeldig patroon" : err};
    std::unique_lock<std::mutex> lk(m_);
    Player& pl = players_[static_cast<size_t>(player - 1)];
    if (!test_source_locked(pl)) return {false, "unavailable", "uitvoer is niet gewapend: gebruik eerst 'arm'"};
    stop_pattern_locked(lk);
    if (!pl.out->armed()) return {false, "unavailable", "uitvoer is niet gewapend"};
    pl.clock.clear();  // het patroon heeft voorrang; geen verouderd anker dat daarna terugspringt
    pattern_player_ = player;
    pattern_stop_ = false;
    pattern_running_ = true;
    pattern_state_ = "running";
    pattern_params_ = p;
    pattern_steps_total_ = steps.size();
    pattern_step_ = 0;
    pattern_length_s_ = pattern_length_s(p, steps);
    uint64_t run = ++pattern_run_;
    logf("info", "testpatroon %s gestart (%zu stappen, %.1f s)", to_string(p.kind), steps.size(), pattern_length_s_);
    pattern_thread_ = std::thread(&OutputService::pattern_main, this, std::move(steps), p, run);
    return {};
}

void OutputService::pattern_main(std::vector<PatternStep> steps, PatternParams p, uint64_t run) {
    auto t0 = steady_clock::now();
    auto at = [&](double s) { return t0 + std::chrono::duration_cast<steady_clock::duration>(duration<double>(s)); };
    std::unique_lock<std::mutex> lk(m_);
    GunOutput& out = *players_[static_cast<size_t>(pattern_player_ - 1)].out;
    bool stopped = false;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (cv_.wait_until(lk, at(steps[i].t_s), [&] { return pattern_stop_ || !out.armed(); })) { stopped = true; break; }
        out.set_position(steps[i].x, steps[i].y);
        if (pattern_run_ == run) pattern_step_ = i + 1;
        if (!out.armed()) { stopped = true; break; }  // schrijffout: GunOutput heeft zichzelf gedisarmd
    }
    if (!stopped && (p.kind == PatternKind::Corners || p.kind == PatternKind::Grid)) {
        // Laatste punt nog dwell_s vasthouden.
        stopped = cv_.wait_until(lk, at(pattern_length_s_), [&] { return pattern_stop_ || !out.armed(); });
    }
    if (pattern_run_ == run) {
        pattern_running_ = false;
        pattern_state_ = stopped ? "stopped" : "done";
    }
}

OutputResult OutputService::button(GunButton b, const std::string& action, int player) {
    if (player < 1 || player > kPlayers) return {false, "invalid_argument", "ongeldige speler"};
    std::unique_lock<std::mutex> lk(m_);
    Player& p = players_[static_cast<size_t>(player - 1)];
    GunOutput& out = *p.out;
    if (!test_source_locked(p)) return {false, "unavailable", "uitvoer is niet gewapend: gebruik eerst 'arm'"};
    // Synthetisch (OR met de live stand), zie key(): live rapporten annuleren deze druk niet.
    if (action == "up") {
        p.ov.release_button(b);
        if (!out.set_button(b, p.ov.want_button(b))) return {false, "internal", "knop schrijven mislukt"};
        return {};
    }
    const bool tap = action != "down";   // tap: 80 ms ingedrukt, zonder de lock lang vast te houden
    const uint64_t token = tap ? p.ov.tap_button(b) : 0;
    if (!tap) p.ov.hold_button(b);
    if (!out.set_button(b, true)) { p.ov.release_button(b); return {false, "internal", "knop schrijven mislukt"}; }
    if (tap) {
        cv_.wait_for(lk, std::chrono::milliseconds(80), [&] { return !out.armed(); });
        p.ov.end_tap_button(b, token);   // een nieuwere tap, een up of een disarm tussendoor laat dit ongemoeid
        if (out.armed()) out.set_button(b, p.ov.want_button(b));
    }
    return {};
}

OutputResult OutputService::set_transform(const GunTransform& t) {
    if (!t.viewport.valid()) return {false, "invalid_argument", "ongeldige viewport"};
    std::lock_guard<std::mutex> g(m_);
    for (auto& p : players_) p.out->set_transform(t);
    return {};
}

std::string OutputService::status_json() const {
    namespace json = ravengun::json;
    std::lock_guard<std::mutex> g(m_);
    json::Writer w;
    const GunOutput& o1 = *p1().out;
    const GunTransform& t = o1.transform();
    auto hex4 = [](unsigned v) { char b[8]; std::snprintf(b, sizeof b, "%04x", v); return std::string(b); };
    auto device = [&](const Player& p) {
        w.begin_object();
        w.key("name").value(p.id.name).key("vendor").value(hex4(p.id.vendor)).key("product").value(hex4(p.id.product));
        w.key("created").value(p.out->device_created());
        w.key("stable_link").value(p.id.stable_link).key("by_id").value(p.id.by_id_link);
        w.key("event_node").value(event_node(p));
        w.key("abs_min").value(kAbsMin).key("abs_max").value(kAbsMax);
        w.end_object();
    };
    auto position = [&](const GunOutput& o) {
        w.begin_object();
        if (o.have_position()) {
            w.key("nx").value(o.last_nx()).key("ny").value(o.last_ny());
            w.key("abs_x").value(o.last_abs_x()).key("abs_y").value(o.last_abs_y());
        } else {
            w.key("nx").null().key("ny").null().key("abs_x").null().key("abs_y").null();
        }
        w.end_object();
    };
    auto buttons = [&](const GunOutput& o) {
        w.begin_array();
        for (int i = 0; i < kGunButtonCount; ++i)
            if (o.button_down(static_cast<GunButton>(i))) w.value(to_string(static_cast<GunButton>(i)));
        w.end_array();
    };
    auto keys = [&](const GunOutput& o) {
        w.begin_array();
        for (int code : o.keys_down()) w.value(ravengun::output::key_name(code));
        w.end_array();
    };
    auto map = [&](const Player& p) {
        w.begin_object();
        for (const auto& kv : p.layout) w.key(kv.first).value(kv.second);
        w.end_object();
    };
    auto live = [&](const Player& p) {
        w.begin_object();
        const auto& cc = p.clock.config();
        w.key("source").value(cc.enabled() ? "move (pijplijn-aim per rapport, uitvoerklok met extrapolatie)" : "move (pijplijn-aim per rapport, uitvoerklok uit)");
        w.key("aim_valid").value(p.live_aim_valid);
        w.key("samples").value(static_cast<unsigned long long>(p.live_samples));
        w.key("positions").value(static_cast<unsigned long long>(p.live_positions));
        w.key("invalid_aim").value(static_cast<unsigned long long>(p.live_invalid));
        w.key("releases").value(static_cast<unsigned long long>(p.live_releases));
        w.key("suspended_by_pattern").value(static_cast<unsigned long long>(p.live_suspended));
        w.key("clock").begin_object();
        w.key("enabled").value(cc.enabled()).key("hz").value(cc.hz).key("extrapolate_ms").value(cc.max_extrap_ms);
        w.key("age_comp").value(cc.age_comp);
        w.key("ticks").value(static_cast<unsigned long long>(p.clock_ticks));
        w.key("extrapolated").value(static_cast<unsigned long long>(p.clock_extrapolated));
        w.key("held").value(static_cast<unsigned long long>(p.clock_held));
        w.key("events").value(static_cast<unsigned long long>(p.clock_events));
        w.end_object();
        w.end_object();
    };

    w.begin_object();
    w.key("armed").value(armed_ && o1.armed());
    // Speler 1 op het hoogste niveau (zoals voorheen); alle spelers onder "players".
    w.key("device");
    device(p1());
    w.key("keyboard").begin_object();
    w.key("name").value(kKbdDeviceName).key("vendor").value("1209").key("product").value("7201");
    w.key("created").value(kbd_->created());
    w.key("stable_link").value(kKbdStableLink);
    auto* kb = dynamic_cast<const UinputKeyboard*>(kbd_.get());
    w.key("event_node").value(kb ? kb->event_node() : std::string());
    if (o1.keyboard_error().empty()) w.key("error").null();
    else w.key("error").value(o1.keyboard_error());
    w.end_object();
    w.key("position");
    position(o1);
    w.key("buttons_down");
    buttons(o1);
    w.key("keys_down");
    keys(o1);
    w.key("map");
    map(p1());
    w.key("transform").begin_object();
    w.key("invert_x").value(t.invert_x).key("invert_y").value(t.invert_y);
    w.key("scale_x").value(t.scale_x).key("scale_y").value(t.scale_y);
    w.key("offset_x").value(t.offset_x).key("offset_y").value(t.offset_y);
    w.key("viewport").begin_object().key("x0").value(t.viewport.x0).key("y0").value(t.viewport.y0).key("x1").value(t.viewport.x1)
        .key("y1").value(t.viewport.y1).end_object();
    w.end_object();
    w.key("pattern").begin_object();
    w.key("state").value(pattern_state_);
    if (pattern_state_ != "none") {
        w.key("kind").value(to_string(pattern_params_.kind));
        w.key("step").value(static_cast<unsigned long long>(pattern_step_));
        w.key("steps").value(static_cast<unsigned long long>(pattern_steps_total_));
        w.key("length_s").value(pattern_length_s_);
    }
    w.end_object();
    w.key("live");
    live(p1());
    const GunOutputStats& s = o1.stats();
    w.key("stats").begin_object();
    w.key("position_events").value(static_cast<unsigned long long>(s.position_events));
    w.key("button_events").value(static_cast<unsigned long long>(s.button_events));
    w.key("rejected").value(static_cast<unsigned long long>(s.rejected));
    w.key("write_errors").value(static_cast<unsigned long long>(s.write_errors));
    w.key("releases").value(static_cast<unsigned long long>(s.releases));
    w.key("arms").value(static_cast<unsigned long long>(s.arms));
    w.end_object();
    if (o1.last_error().empty()) w.key("last_error").null();
    else w.key("last_error").value(o1.last_error());
    w.key("players").begin_array();
    for (const auto& p : players_) {
        w.begin_object();
        w.key("player").value(p.number);
        w.key("active").value(should_exist(p.number));
        w.key("armed").value(p.out->armed());
        w.key("device");
        device(p);
        w.key("position");
        position(*p.out);
        w.key("buttons_down");
        buttons(*p.out);
        w.key("keys_down");
        keys(*p.out);
        w.key("map");
        map(p);
        w.key("live");
        live(p);
        if (p.out->last_error().empty()) w.key("last_error").null(); else w.key("last_error").value(p.out->last_error());
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

}  // namespace ravengund
