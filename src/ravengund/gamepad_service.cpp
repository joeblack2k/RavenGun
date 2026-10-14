#include "gamepad_service.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "output_service.hpp"
#include "ravengun/gamesettings/gamepad_settings.hpp"
#include "ravengun/json.hpp"
#include "ravengun/sysinfo/mice.hpp"
#include "shared.hpp"

namespace ravengund {

using namespace ravengun;

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// De eventN-naam komt uit onze eigen opsomming; toch geen enkel pad-teken doorlaten.
bool event_name_ok(const std::string& e) {
    if (e.empty() || e.size() > 32 || e[0] == '.') return false;
    for (char c : e)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

std::string open_error(const std::string& path, int e) {
    if (e == EACCES || e == EPERM)
        return "geen toegang tot " + path + " (leesrechten nodig: de groep 'input' of een udev-regel met uaccess)";
    if (e == ENOENT || e == ENODEV || e == ENXIO) return "apparaat " + path + " bestaat niet meer (gamepad losgekoppeld?)";
    return "openen van " + path + " mislukt: " + std::strerror(e);
}

std::string hex4(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof b, "%04x", v & 0xffff);
    return b;
}

}  // namespace

GamepadService::GamepadService(OutputService& out, GamepadServiceConfig cfg) : out_(out), cfg_(std::move(cfg)) {
    wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (cfg_.scan_ms < 50) cfg_.scan_ms = 50;
    if (cfg_.load) {
        assigned_ = cfg_.load();
        for (auto& a : assigned_)
            if (!a.empty() && !sysinfo::mouse_id_valid(a)) a.clear();
    }
}

GamepadService::~GamepadService() {
    stop();
    if (wake_fd_ >= 0) ::close(wake_fd_);
}

std::vector<sysinfo::Gamepad> GamepadService::enumerate(std::vector<sysinfo::GamepadVerdict>* v) const {
    return cfg_.lister ? cfg_.lister(v) : sysinfo::list_system_gamepads(v);
}

void GamepadService::wake() {
    if (wake_fd_ < 0) return;
    uint64_t one = 1;
    if (::write(wake_fd_, &one, sizeof one) < 0) { /* de poll heeft een time-out */ }
}

void GamepadService::start() {
    std::lock_guard<std::mutex> g(m_);
    if (running_ || stop_) return;
    running_ = true;
    th_ = std::thread([this] { run(); });
}

void GamepadService::stop() {
    std::thread t;
    {
        std::lock_guard<std::mutex> g(m_);
        if (stop_) return;
        stop_ = true;
        t = std::move(th_);
    }
    wake();
    cv_.notify_all();
    if (t.joinable()) t.join();
    std::lock_guard<std::mutex> g(m_);
    running_ = false;
}

GamepadServiceConfig::Assignments GamepadService::effective_locked(std::array<bool, kPlayers>* is_auto) const {
    GamepadServiceConfig::Assignments e = assigned_;
    if (is_auto) is_auto->fill(false);
    // Automatisch: speler 1 zonder gamepad en precies één aangesloten gamepad die niet aan een andere speler hangt.
    if (e[0].empty() && pads_.size() == 1) {
        const std::string& only = pads_.front().id;
        if (std::find(e.begin(), e.end(), only) == e.end()) {
            e[0] = only;
            if (is_auto) (*is_auto)[0] = true;
        }
    }
    return e;
}

bool GamepadService::assign(int player, const std::string& id, std::string& code, std::string& msg) {
    if (player < 1 || player > kPlayers) { code = "invalid_argument"; msg = "ongeldige speler"; return false; }
    if (!id.empty()) {
        // Alleen een id uit onze eigen opsomming: nooit een vrij pad openen.
        bool known = sysinfo::mouse_id_valid(id);
        if (known) {
            known = false;
            for (const auto& p : enumerate(nullptr)) if (p.id == id) known = true;
        }
        if (!known) { code = "invalid_argument"; msg = "onbekende gamepad '" + id.substr(0, 64) + "' (zie 'pads' voor de aangesloten gamepads)"; return false; }
    }
    GamepadServiceConfig::Assignments snapshot;
    uint64_t before;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (stop_) { code = "unavailable"; msg = "gamepadservice is afgesloten"; return false; }
        if (!id.empty())
            for (int i = 0; i < kPlayers; ++i)
                if (i != player - 1 && assigned_[static_cast<size_t>(i)] == id) {
                    code = "invalid_argument";
                    msg = "deze gamepad is al aan speler " + std::to_string(i + 1) + " toegewezen";
                    return false;
                }
        assigned_[static_cast<size_t>(player - 1)] = id;
        snapshot = assigned_;
        ++gen_;
        rescan_now_ = true;
        before = rounds_;
    }
    if (!id.empty()) logf("info", "gamepad %s -> speler %d", id.c_str(), player);
    else logf("info", "gamepad van speler %d losgekoppeld", player);
    if (cfg_.save) {
        const std::string e = cfg_.save(snapshot);
        if (!e.empty()) logf("waarschuwing", "gamepad-toewijzing niet bewaard: %s", e.c_str());
    }
    wake();
    std::unique_lock<std::mutex> lk(m_);
    if (running_) cv_.wait_for(lk, std::chrono::milliseconds(400), [&] { return rounds_ > before + 1 || stop_; });
    return true;
}

void GamepadService::set_game(const std::string& game) {
    const gamesettings::GamepadMapping gm = gamesettings::gamepad_mapping_for(cfg_.dirs, game);
    for (const auto& w : gm.warnings) logf("waarschuwing", "gamepad (%s): %s", game.c_str(), w.c_str());
    std::string text;
    for (const auto& r : gm.rows) text += (text.empty() ? "" : " ") + r.control + "=" + r.target;
    logf("info", "gamepad-indeling voor %s (%s): %s", game.empty() ? "(geen game)" : game.c_str(),
         gm.has_section ? "[Gamepad] uit game.ini" : "standaard", text.c_str());
    {
        std::lock_guard<std::mutex> lk(m_);
        map_ = gm.map;
        game_ = game;
        map_warnings_ = gm.warnings;
        ++gen_;
    }
    wake();
}

// ---------------------------------------------------------------------------------------------------------------------
// Werkerthread

void GamepadService::rescan() {
    std::vector<sysinfo::GamepadVerdict> v;
    auto list = enumerate(&v);
    // Per invoerapparaat waarom het wel of geen gamepad is, maar alleen als er iets veranderde, en hoogstens een keer per minuut de hele
    // lijst: een pad die steeds verschijnt en verdwijnt (gemeten 2026-10-14: hid-steam meldt de Steam Controller ~15 keer per seconde
    // af en aan, ook zonder daemon) zou het logboek anders vullen. Tussendoor telt een regel de wijzigingen.
    std::string sig;
    for (const auto& x : v) sig += x.event + "|" + x.name + "|" + x.why + "\n";
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (sig != verdict_sig_ && verdict_log_ms_ && now - verdict_log_ms_ < 60000) {
        verdict_sig_ = sig;
        ++verdict_changes_;
    } else if (sig != verdict_sig_ || (verdict_changes_ && now - verdict_log_ms_ >= 60000)) {
        verdict_sig_ = sig;
        if (verdict_changes_)
            logf("waarschuwing", "gamepads: de lijst veranderde %d keer in de laatste minuut (een apparaat dat steeds verschijnt en verdwijnt?)",
                 verdict_changes_);
        verdict_changes_ = 0;
        verdict_log_ms_ = now;
        logf("info", "gamepads: %zu gevonden; invoerapparaten:", list.size());
        for (const auto& x : v)
            logf("info", "  %s \"%s\" (%s:%s): %s", x.event.empty() ? "-" : x.event.c_str(), x.name.c_str(), hex4(x.vendor).c_str(),
                 hex4(x.product).c_str(), x.why.c_str());
    }
    std::lock_guard<std::mutex> lk(m_);
    pads_ = std::move(list);
    verdicts_ = std::move(v);
}

bool GamepadService::try_open(const sysinfo::Gamepad& g, Dev& d, std::string& err) {
    if (!event_name_ok(g.event)) { err = "ongeldig apparaat voor gamepad '" + g.id + "'"; return false; }
    const std::string path = cfg_.dev_dir + "/" + g.event;
    // Geen EVIOCGRAB: de pad blijft ook voor de desktop en Steam bruikbaar.
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { err = open_error(path, errno); return false; }
    d = Dev{};
    d.fd = fd;
    d.event = g.event;
    // Bereik per as; een as die het apparaat niet heeft (of een FIFO in de tests) houdt het gewone bereik.
    for (int code : {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_GAS, ABS_BRAKE, ABS_HAT0X, ABS_HAT0Y}) {
        input_absinfo ai{};
        if (::ioctl(fd, EVIOCGABS(code), &ai) == 0 && ai.maximum > ai.minimum) d.reader.set_range(code, {ai.minimum, ai.maximum});
    }
    return true;
}

void GamepadService::close_dev(const std::string& id, const std::string& why) {
    auto it = devs_.find(id);
    if (it == devs_.end()) return;
    if (it->second.fd >= 0) ::close(it->second.fd);
    devs_.erase(it);
    std::lock_guard<std::mutex> lk(m_);
    Live& l = live_[id];
    l.connected = false;
    l.state = output::PadState{};
    if (!why.empty()) {
        if (l.last_error != why) logf("waarschuwing", "gamepad %s: %s", id.c_str(), why.c_str());
        l.last_error = why;
    }
}

void GamepadService::sync_devices() {
    std::vector<sysinfo::Gamepad> pads;
    GamepadServiceConfig::Assignments eff;
    {
        std::lock_guard<std::mutex> lk(m_);
        pads = pads_;
        eff = effective_locked();
    }
    std::set<std::string> wanted;
    if (cfg_.open_devices)
        for (const auto& id : eff) if (!id.empty()) wanted.insert(id);
    auto find = [&](const std::string& id) -> const sysinfo::Gamepad* {
        for (const auto& p : pads) if (p.id == id) return &p;
        return nullptr;
    };
    // Dicht wat niet meer gevraagd wordt, verdwenen is of een ander eventN kreeg.
    std::vector<std::string> close;
    for (const auto& kv : devs_) {
        const sysinfo::Gamepad* g = find(kv.first);
        if (!wanted.count(kv.first) || !g || g->event != kv.second.event) close.push_back(kv.first);
    }
    for (const auto& id : close) close_dev(id, find(id) || !wanted.count(id) ? std::string() : "gamepad losgekoppeld");
    for (const auto& id : wanted) {
        if (devs_.count(id)) continue;
        const sysinfo::Gamepad* g = find(id);
        std::string err;
        Dev d;
        if (!g) err = "gamepad '" + id + "' is niet aangesloten";
        else if (try_open(*g, d, err)) {
            devs_[id] = std::move(d);
            logf("info", "gamepad %s geopend (%s, \"%s\")", id.c_str(), g->event.c_str(), g->name.c_str());
        }
        std::lock_guard<std::mutex> lk(m_);
        Live& l = live_[id];
        l.connected = err.empty();
        if (!err.empty() && l.last_error != err) logf("waarschuwing", "gamepad %s: %s", id.c_str(), err.c_str());
        l.last_error = err;
        if (!err.empty()) l.state = output::PadState{};
    }
    // Geen status bewaren van pads die geen speler meer hebben.
    std::lock_guard<std::mutex> lk(m_);
    for (auto it = live_.begin(); it != live_.end();) {
        if (!wanted.count(it->first) && !devs_.count(it->first)) it = live_.erase(it);
        else ++it;
    }
}

void GamepadService::push_player(int player) {
    std::string id;
    output::GamepadMap map;
    bool stopping;
    {
        std::lock_guard<std::mutex> lk(m_);
        id = effective_locked()[static_cast<size_t>(player - 1)];
        map = map_;
        stopping = stop_;
    }
    const Dev* d = nullptr;
    if (!stopping && !id.empty()) {
        auto it = devs_.find(id);
        if (it != devs_.end() && it->second.fd >= 0) d = &it->second;
    }
    Sent& sent = sent_[static_cast<size_t>(player - 1)];
    if (!d && !sent.any && sent.keys.empty()) return;   // niets te melden en niets los te laten
    output::PadOutput o;
    iobridge::IoPadInput in;
    if (d) {
        o = output::apply_gamepad_map(map, d->reader.state());
        in.pad = true;
        in.wheel_from_pad = o.wheel_mapped;
        in.wheel = o.wheel;
        in.gas = o.gas;
        in.brake = o.brake;
        in.btn = o.btn;
    }
    out_.set_pad_input(player, in);
    output::LiveAction a;
    a.no_aim = true;
    a.buttons = true;
    a.trigger = o.gun[static_cast<size_t>(output::GunButton::Trigger)];
    a.reload = o.gun[static_cast<size_t>(output::GunButton::Reload)];
    a.grenade = o.gun[static_cast<size_t>(output::GunButton::Grenade)];
    a.aux1 = o.gun[static_cast<size_t>(output::GunButton::Aux1)];
    a.aux2 = o.gun[static_cast<size_t>(output::GunButton::Aux2)];
    // Toetsen: start = toets N van speler N, munt = toets 5 (op de poort van deze speler), en de key:-bedieningen.
    std::map<int, bool> keys;
    if (d) {
        keys[KEY_1 + (player - 1)] = o.start;
        keys[KEY_5] = keys[KEY_5] || o.coin;
        for (const auto& k : o.keys) keys[k.first] = keys[k.first] || k.second;
    }
    for (int code : sent.keys) keys.emplace(code, false);   // wat niet meer gekoppeld is: los
    sent.keys.clear();
    for (const auto& k : keys) {
        a.keys.emplace_back(k.first, k.second);
        if (d) sent.keys.insert(k.first);
    }
    sent.any = d != nullptr;
    out_.feed_live(player, a, 2);   // bron 2: de gamepad
}

void GamepadService::push_all() {
    for (int p = 1; p <= kPlayers; ++p) push_player(p);
}

void GamepadService::drain(const std::string& id) {
    auto it = devs_.find(id);
    if (it == devs_.end()) return;
    Dev& d = it->second;
    constexpr size_t kEv = sizeof(input_event);
    char buf[kEv * 64];
    bool changed = false;
    for (int round = 0; round < 8; ++round) {
        const ssize_t n = ::read(d.fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            close_dev(id, errno == ENODEV ? "gamepad losgekoppeld" : std::string("lezen mislukt: ") + std::strerror(errno));
            push_all();
            return;
        }
        if (n == 0) { close_dev(id, "apparaat gesloten"); push_all(); return; }
        d.carry.append(buf, static_cast<size_t>(n));
        size_t off = 0;
        while (d.carry.size() - off >= kEv) {
            input_event ev;
            std::memcpy(&ev, d.carry.data() + off, kEv);
            off += kEv;
            changed = d.reader.event(ev.type, ev.code, ev.value) || changed;
        }
        d.carry.erase(0, off);
        if (static_cast<size_t>(n) < sizeof buf) break;
    }
    if (!changed) return;
    GamepadServiceConfig::Assignments eff;
    {
        std::lock_guard<std::mutex> lk(m_);
        live_[id].state = d.reader.state();
        eff = effective_locked();
    }
    for (int p = 1; p <= kPlayers; ++p)
        if (eff[static_cast<size_t>(p - 1)] == id) push_player(p);
}

bool GamepadService::wait_round(int timeout_ms) {
    std::unique_lock<std::mutex> lk(m_);
    const uint64_t before = rounds_;
    wake();
    return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return rounds_ > before + 1 || stop_; });
}

void GamepadService::run() {
    int64_t next_scan = 0;
    for (;;) {
        bool scan;
        uint64_t gen;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (stop_) break;
            scan = rescan_now_ || now_ms() >= next_scan;
            rescan_now_ = false;
            gen = gen_;
        }
        if (scan) {
            rescan();
            next_scan = now_ms() + cfg_.scan_ms;
        }
        if (scan || gen != applied_gen_) {
            sync_devices();
            applied_gen_ = gen;
            push_all();   // ook na een opsomming: een nieuwe arm krijgt zo binnen 2 s de stand van de pads terug
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            ++rounds_;
        }
        cv_.notify_all();
        pollfd pfd[kPlayers + 1];
        std::string owner[kPlayers + 1];
        int n = 0;
        pfd[n++] = {wake_fd_, POLLIN, 0};
        for (const auto& kv : devs_) {
            if (n > kPlayers) break;
            pfd[n] = {kv.second.fd, POLLIN, 0};
            owner[n++] = kv.first;
        }
        const int64_t timeout = std::max<int64_t>(0, std::min<int64_t>(1000, next_scan - now_ms()));
        const int pr = ::poll(pfd, static_cast<nfds_t>(n), static_cast<int>(timeout));
        if (pr < 0 && errno != EINTR) {
            logf("fout", "gamepadservice: poll mislukt: %s", std::strerror(errno));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (pr <= 0) continue;
        if (pfd[0].revents & POLLIN) {
            uint64_t v;
            if (::read(wake_fd_, &v, sizeof v) < 0) { /* leeg */ }
        }
        for (int k = 1; k < n; ++k) {
            if (!devs_.count(owner[k])) continue;
            if (pfd[k].revents & POLLIN) drain(owner[k]);   // eerst leegmaken: een HUP kan na de laatste events komen
            if (devs_.count(owner[k]) && (pfd[k].revents & (POLLHUP | POLLERR | POLLNVAL))) {
                close_dev(owner[k], "gamepad losgekoppeld");
                push_all();
            }
        }
    }
    // Afsluiten: niets mag blijven hangen (push_player ziet stop_ en laat alles los).
    push_all();
    std::vector<std::string> ids;
    for (const auto& kv : devs_) ids.push_back(kv.first);
    for (const auto& id : ids) close_dev(id, {});
}

// ---------------------------------------------------------------------------------------------------------------------
// JSON

std::string GamepadService::assignment_locked(int player) const {
    std::array<bool, kPlayers> is_auto{};
    const auto eff = effective_locked(&is_auto);
    const std::string& id = eff[static_cast<size_t>(player - 1)];
    json::Writer w;
    w.begin_object().key("player").value(player);
    if (id.empty()) w.key("pad").null(); else w.key("pad").value(id);
    w.key("auto").value(is_auto[static_cast<size_t>(player - 1)]);
    auto it = live_.find(id);
    const bool connected = !id.empty() && it != live_.end() && it->second.connected;
    w.key("connected").value(connected);
    if (id.empty() || it == live_.end() || it->second.last_error.empty()) w.key("last_error").null();
    else w.key("last_error").value(it->second.last_error);
    w.end_object();
    return w.str();
}

bool GamepadService::any_player_connected() const {
    std::lock_guard<std::mutex> lk(m_);
    for (const std::string& id : effective_locked()) {
        auto it = id.empty() ? live_.end() : live_.find(id);
        if (it != live_.end() && it->second.connected) return true;
    }
    return false;
}

std::string GamepadService::assignments_locked() const {
    std::string s = "[";
    for (int p = 1; p <= kPlayers; ++p) {
        if (p > 1) s += ',';
        s += assignment_locked(p);
    }
    return s + "]";
}

std::string GamepadService::assignment_json(int player) const {
    if (player < 1 || player > kPlayers) return "null";
    std::lock_guard<std::mutex> g(m_);
    return assignment_locked(player);
}

std::string GamepadService::status_json() const {
    std::lock_guard<std::mutex> g(m_);
    return assignments_locked();
}

std::string GamepadService::pads_json() const {
    std::lock_guard<std::mutex> g(m_);
    std::array<bool, kPlayers> is_auto{};
    const auto eff = effective_locked(&is_auto);
    json::Writer w;
    w.begin_object().key("pads").begin_array();
    for (const auto& p : pads_) {
        w.begin_object().key("id").value(p.id).key("name").value(p.name).key("event").value(p.event);
        w.key("vendor").value(hex4(p.vendor)).key("product").value(hex4(p.product));
        int player = 0;
        for (int i = 0; i < kPlayers; ++i) if (eff[static_cast<size_t>(i)] == p.id) player = i + 1;
        if (player) w.key("player").value(player); else w.key("player").null();
        auto it = live_.find(p.id);
        w.key("connected").value(it != live_.end() && it->second.connected);
        if (it == live_.end() || it->second.last_error.empty()) w.key("last_error").null(); else w.key("last_error").value(it->second.last_error);
        // Live weergave (alleen een geopende pad): assen 0..1 en ingedrukte knoppen.
        if (it != live_.end() && it->second.connected) {
            const output::PadState& st = it->second.state;
            w.key("live").begin_object().key("axes").begin_object();
            for (int i = 0; i < output::kPadControls; ++i) {
                const auto c = static_cast<output::PadControl>(i);
                if (output::pad_control_kind(c) != output::PadKind::Button)
                    w.key(output::pad_control_name(c)).value(std::round(st.value(c) * 1000.0) / 1000.0);
            }
            w.end_object().key("buttons").begin_array();
            for (int i = 0; i < output::kPadControls; ++i) {
                const auto c = static_cast<output::PadControl>(i);
                if (output::pad_control_kind(c) == output::PadKind::Button && st.pressed(c)) w.value(output::pad_control_name(c));
            }
            w.end_array().end_object();
        } else {
            w.key("live").null();
        }
        w.end_object();
    }
    w.end_array();
    w.key("assignments").raw(assignments_locked());
    if (game_.empty()) w.key("game").null(); else w.key("game").value(game_);
    w.key("mapping").begin_array();
    for (const auto& kv : output::describe_gamepad_map(map_)) w.begin_object().key("control").value(kv.first).key("target").value(kv.second).end_object();
    w.end_array();
    w.key("warnings").begin_array();
    for (const auto& x : map_warnings_) w.value(x);
    w.end_array();
    w.key("devices").begin_array();
    for (const auto& v : verdicts_) {
        w.begin_object().key("name").value(v.name).key("event").value(v.event).key("vendor").value(hex4(v.vendor));
        w.key("product").value(hex4(v.product)).key("gamepad").value(v.gamepad).key("why").value(v.why).end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

}  // namespace ravengund
