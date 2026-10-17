// HID-inname via PSMoveAPI. Dit is de enige plek in RavenGun die PSMoveAPI aanroept.
//
// Meerdere Moves: per speler (1..4) een MoveSlot met een eigen pijplijn (move_slot.hpp). De thread zoekt elke seconde in
// sysfs naar via Bluetooth verbonden Moves (goedkoop, geen PSMoveAPI) en opent alleen een nieuwe Move met PSMoveAPI. Een Move
// gaat naar de speler die hij in config.json heeft (controllers); een onbekende Move wordt opgenomen met het laagste vrije
// spelernummer. Daarnaast leest de thread de Navigation Controllers (hidraw, alleen lezen) en geeft hun knoppen aan de speler
// die ze aanvullen.
#include "hid_thread.hpp"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <map>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include "camera.hpp"
#include "move_slot.hpp"
#include "output_service.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/core/navcon.hpp"
#include "ravengun/json.hpp"
#include "ravengun/parse.hpp"
#include "ravengun/sysinfo/hidraw_scan.hpp"

#ifndef RAVENGUN_NO_PSMOVEAPI
#include <psmoveapi/psmove.h>
#endif

namespace ravengund {

using namespace ravengun;
using namespace ravengun::core;

namespace {

#ifndef RAVENGUN_NO_PSMOVEAPI
std::string serial_of(PSMove* m) {
    char* s = psmove_get_serial(m);
    std::string r = s ? s : "";
    if (s) psmove_free_mem(s);
    for (auto& ch : r) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return r;
}

// Opent de via Bluetooth verbonden Move met dit adres (USB levert geen inputrapporten).
PSMove* connect_move(const std::string& address, ControllerInfo& ci) {
    int n = psmove_count_connected();
    for (int i = 0; i < n; ++i) {
        PSMove* m = psmove_connect_by_id(i);
        if (!m) continue;
        if (psmove_connection_type(m) != Conn_Bluetooth || serial_of(m) != address) { psmove_disconnect(m); continue; }
        ci = ControllerInfo{};
        ci.open = true;
        ci.id = address;
        ci.model = psmove_get_model(m) == Model_ZCM1 ? "ZCM1" : psmove_get_model(m) == Model_ZCM2 ? "ZCM2" : "onbekend";
        ci.connection = "bluetooth";
        ci.has_calibration = psmove_has_calibration(m);
        return m;
    }
    return nullptr;
}

RawReport read_report(PSMove* m, int poll_ret, bool zcm2, int64_t t, HidrawTap& tap) {
    RawReport r;
    r.t_rx_ns = t;
    r.hid_seq = poll_ret - 1;  // psmove_poll geeft 1 + volgnummer
    r.subsamples = zcm2 ? 1 : 2;
    float x, y, z;
    for (int i = 0; i < r.subsamples; ++i) {
        // ZCM1: Frame_FirstHalf is het oudere subsample, Frame_SecondHalf het nieuwere (psmove.h).
        // ZCM2: één subsample; PSMoveAPI negeert daar de frame-parameter.
        PSMove_Frame f = i == 0 ? Frame_FirstHalf : Frame_SecondHalf;
        psmove_get_accelerometer_frame(m, f, &x, &y, &z);
        r.accel[i] = {x, y, z};
        psmove_get_gyroscope_frame(m, f, &x, &y, &z);
        r.gyro[i] = {x, y, z};
    }
    psmove_get_magnetometer(m, &r.mag_raw[0], &r.mag_raw[1], &r.mag_raw[2]);
    r.buttons = psmove_get_buttons(m);
    r.trigger = psmove_get_trigger(m);
    r.battery_raw = static_cast<int>(psmove_get_battery(m));
    r.temperature_raw = psmove_get_temperature(m);
    if (!zcm2) {
        // EXT-connector (Sharp Shooter): vlag uit buttons4 bit 0x10 en 5 bytes extdata (psmove.c).
        PSMove_Ext_Data xd;
        if (psmove_get_ext_data(m, &xd)) {
            r.has_ext = true;
            r.ext_connected = psmove_is_ext_connected(m);
            std::memcpy(r.ext, xd, sizeof r.ext);
        }
    }
    if (tap.is_open()) {
        // Hetzelfde rapport via de eigen hidraw-descriptor: interne stempel en ruwe waarden.
        int avg[3];
        psmove_get_accelerometer(m, &avg[0], &avg[1], &avg[2]);
        if (auto in = tap.take(r.hid_seq, avg)) {
            r.has_stamp = true;
            r.stamp = in->stamp;
            r.temperature_raw = in->temperature_raw;
            r.has_raw = true;
            std::memcpy(r.accel_raw, in->accel_raw, sizeof r.accel_raw);
            std::memcpy(r.gyro_raw, in->gyro_raw, sizeof r.gyro_raw);
        }
    }
    return r;
}
#endif

void sleep_us(int us) {
    timespec ts{0, static_cast<long>(us) * 1000};
    nanosleep(&ts, nullptr);
}

// Een Navigation Controller (USB of Bluetooth), alleen-lezen via hidraw.
struct NavReader {
    std::string node, address;
    int fd = -1;
    int player = 0;
    NavState st;
    uint64_t reports = 0;
};

// Het eigen adres van een Navigation Controller via USB: feature-rapport 0xF2, bytes 4..9 (alleen lezen; zie BlueZ
// plugins/sixaxis.c en hid-sony). Leeg bij een fout.
std::string nav_usb_address(int fd) {
    uint8_t buf[18] = {0xF2};
    if (::ioctl(fd, HIDIOCGFEATURE(sizeof buf), buf) < 10) return {};
    char a[24];
    std::snprintf(a, sizeof a, "%02x:%02x:%02x:%02x:%02x:%02x", buf[4], buf[5], buf[6], buf[7], buf[8], buf[9]);
    return config::normalize_address(a);
}

}  // namespace

void hid_thread_main(SharedState& shared, HidOptions opt) {
    const int64_t start = mono_ns();
    std::array<std::unique_ptr<MoveSlot>, kPlayers> slots;
    for (int p = 1; p <= kPlayers; ++p) slots[static_cast<size_t>(p - 1)] = std::make_unique<MoveSlot>(p, shared, opt);
    auto slot = [&](int p) -> MoveSlot& { return *slots[static_cast<size_t>(SharedState::clamp_player(p) - 1)]; };
#ifndef RAVENGUN_NO_PSMOVEAPI
    std::array<PSMove*, kPlayers> handles{};
#endif
    std::vector<NavReader> navs;
    std::map<std::string, int64_t> failed_until;  // adres -> niet opnieuw proberen voor dit moment

    // Instellingen per speler uit config.json (de Move die die speler heeft, anders de standaard), plus de spelers met een
    // Move voor de uitvoer en de camera.
    auto close_handle = [&](int p, int64_t now) {
#ifndef RAVENGUN_NO_PSMOVEAPI
        PSMove*& h = handles[static_cast<size_t>(p - 1)];
        if (!h) return;
        slot(p).disconnect(now);
        psmove_disconnect(h);
        h = nullptr;
#else
        (void)p; (void)now;
#endif
    };
    auto reconfigure = [&] {
        config::Settings s = shared.settings ? shared.settings->get() : config::Settings{};
        unsigned active = 1;
        for (int p = 1; p <= kPlayers; ++p) {
            const std::string addr = config::move_for_player(s, p);
            MoveSlot& sl = slot(p);
            if (sl.connected() && sl.info().id != addr) {
                logf("info", "speler %d: Move %s hoort niet meer bij deze speler; opnieuw koppelen", p, redact_mac(sl.info().id).c_str());
                close_handle(p, mono_ns());
            }
            const config::Settings eff = config::effective(s, addr);
            // LED en trackertint van de opdrachtregel gelden alleen voor speler 1.
            std::set<std::string> keep = opt.cli_keys;
            if (p != 1) for (auto it = keep.begin(); it != keep.end();) it = (it->rfind("led.", 0) == 0 || it->rfind("tracker.", 0) == 0) ? keep.erase(it) : std::next(it);
            sl.configure(addr, eff, keep);
            if (!addr.empty() && p > 1) active |= 1u << (p - 1);
            if (shared.camera) {
                if (p > 1 || !opt.cli_keys.count("tracker.hue_deg")) {
                    auto tc = shared.camera->tracker(p);
                    // Speler 2..4 zonder eigen keuze: de tint van zijn spelerkleur (config::player_led).
                    const bool own = s.controllers.count(addr) && s.controllers.at(addr).keys.count("tracker.hue_deg");
                    if (p == 1 || own) { tc.hue_deg = eff.tracker.hue_deg; tc.hue_tol_deg = eff.tracker.hue_tol_deg; tc.min_chroma = eff.tracker.min_chroma; }
                    else if (p == 2) { tc.hue_deg = 86; tc.hue_tol_deg = 30; }
                    else if (p == 3) { tc.hue_deg = -145; tc.hue_tol_deg = 30; }
                    else { tc.hue_deg = 171; tc.hue_tol_deg = 20; }
                    if (p > 1 || tc.hue_deg != shared.camera->tracker(1).hue_deg || tc.hue_tol_deg != shared.camera->tracker(1).hue_tol_deg)
                        shared.camera->set_tracker(p, tc);
                }
                shared.camera->set_tracker_enabled(p, p == 1 || !addr.empty());
            }
        }
        if (shared.output) shared.output->set_active_players(active);
        for (auto& n : navs) {
            auto it = s.controllers.find(n.address);
            n.player = it != s.controllers.end() && !it->second.removed ? it->second.player : 0;
        }
    };
    // Speler voor een controller (en zo nodig opnemen in config.json).
    auto player_for = [&](const std::string& addr, const std::string& type, const std::string& model) -> int {
        if (!shared.settings) return type == "move" ? 1 : 1;
        config::Settings s = shared.settings->get();
        auto it = s.controllers.find(addr);
        if (it != s.controllers.end() && !it->second.removed && it->second.type == type) return it->second.player;
        int player = 0;
        bool added = false;
        shared.settings->update([&](config::Settings& st) { player = config::register_controller(st, addr, type, model, static_cast<int64_t>(std::time(nullptr)), &added); });
        if (added) {
            logf("info", "%s %s opgenomen als %s", type == "move" ? "Move" : "Navigation Controller", redact_mac(addr).c_str(),
                 player ? ("speler " + std::to_string(player)).c_str() : "geen speler (alle plekken bezet)");
            reconfigure();
        }
        return player;
    };
    auto touch_last_seen = [&](const std::string& addr) {
        if (shared.settings)
            shared.settings->update([&](config::Settings& st) {
                auto it = st.controllers.find(addr);
                if (it != st.controllers.end()) it->second.last_seen_unix = static_cast<int64_t>(std::time(nullptr));
            });
    };

    // Opdrachten uit de IPC-thread draaien hier, zodat de pijplijnen één eigenaar hebben.
    struct Ctx final : HidContext {
        std::function<MoveSlot&(int)> slot_of;
        std::function<void()> reconf;
        int cur = 1;
        int64_t start;
        std::atomic<int>* recording_player;
        std::array<std::unique_ptr<MoveSlot>, kPlayers>* all;
        MoveSlot& s() { return slot_of(cur); }
        const MoveSlot& s() const { return const_cast<Ctx*>(this)->slot_of(cur); }
        bool select_player(int p) override {
            if (p < 1 || p > kPlayers) return false;
            cur = p;
            return true;
        }
        void reconfigure() override { reconf(); }
        std::string blink(int ms) override {
            if (!s().connected()) return "!unavailable|de Move van speler " + std::to_string(cur) + " is niet verbonden";
            s().blink(mono_ns(), ms);
            return "1";
        }
        std::string status_json(bool full) override { return s().status_json(full, start); }
        std::string record(const ravengun::ipc::Request& r) override {
            if (r.action == "start") {
                // Een opname is per controller: alleen deze speler voert de recorder.
                *recording_player = cur;
                for (auto& sl : *all) sl->set_recording_owner(sl->player() == cur);
            }
            return slot_of(r.action == "start" ? cur : recording_player->load()).record(r);
        }
        std::string calib_mag(const ravengun::ipc::Request& r) override { return s().calib_mag(r); }
        bool recenter() override { return s().recenter(); }
        bool clear_trim() override { return s().clear_trim(); }
        OneEuroConfig filter() const override { return s().filter(); }
        void set_filter(const OneEuroConfig& c) override { s().set_filter(c); }
        AimConfig aim() const override { return s().aim(); }
        void set_aim(const AimConfig& c) override { s().set_aim(c); }
        PredictConfig predict() const override { return s().predict(); }
        void set_predict(const PredictConfig& c) override { s().set_predict(c); }
        void set_led(bool en, uint8_t r, uint8_t g, uint8_t b) override { s().set_led(en, r, g, b); }
        bool led(bool& en, uint8_t rgb[3]) const override {
            const HidOptions& o = s().options();
            en = o.led_enabled; rgb[0] = o.led_r; rgb[1] = o.led_g; rgb[2] = o.led_b;
            return true;
        }
        ravengun::output::RumbleConfig rumble() const override { return s().rumble(); }
        void set_rumble(const ravengun::output::RumbleConfig& c) override { s().set_rumble(c); }
        std::string rumble_test(int ms) override { return s().rumble_test(ms); }
        FusionMode fusion() const override { return s().fusion(); }
        void set_fusion(FusionMode m) override { s().set_fusion(m); }
        bool recenter_learning() const override { return s().recenter_learning(); }
        void set_recenter_learning(bool on) override { s().set_recenter_learning(on); }
        void set_gun_buttons(const ravengun::output::LiveButtonMap& m) override { s().set_gun_buttons(m); }
    } ctx;
    ctx.slot_of = slot;
    ctx.reconf = reconfigure;
    ctx.start = start;
    ctx.recording_player = &shared.recording_player;
    ctx.all = &slots;
    auto handle_jobs = [&] { ctx.cur = 1; shared.run_jobs(ctx); ctx.cur = 1; };

    reconfigure();

#ifndef RAVENGUN_NO_PSMOVEAPI
    int64_t next_scan = 0;
    if (opt.enabled) {
        psmove_set_remote_config(PSMove_OnlyLocal);  // geen netwerk (moved) gebruiken
        if (!psmove_init(PSMOVE_CURRENT_VERSION)) {
            logf("fout", "psmove_init() faalde (verkeerde PSMoveAPI-versie?)");
            opt.enabled = false;
        }
    }
#else
    opt.enabled = false;
#endif

    while (shared.running.load()) {
        handle_jobs();
#ifndef RAVENGUN_NO_PSMOVEAPI
        if (opt.enabled) {
            int64_t now = mono_ns();
            if (now >= next_scan) {
                next_scan = now + 1'000'000'000;
                // Nieuwe Moves (Bluetooth) en Navigation Controllers (Bluetooth of USB) zoeken in sysfs.
                const auto devs = sysinfo::scan_sony_hidraw(opt.hidraw_root);
                for (const auto& d : devs) {
                    if (d.type() != "move" || !d.bluetooth()) continue;
                    const std::string addr = config::normalize_address(d.uniq);
                    if (addr.empty()) continue;
                    if (!opt.preferred_controller.empty() && addr != opt.preferred_controller) continue;
                    bool open = false;
                    for (auto& sl : slots) open = open || (sl->connected() && sl->info().id == addr);
                    if (open) continue;
                    if (auto f = failed_until.find(addr); f != failed_until.end() && now < f->second) continue;
                    const int p = player_for(addr, "move", d.model());
                    if (p < 1) continue;
                    if (slot(p).connected()) continue;  // speler bezet (zou niet mogen: spelernummers zijn uniek)
                    ControllerInfo ci;
                    PSMove* m = connect_move(addr, ci);
                    if (!m) { failed_until[addr] = now + 3'000'000'000; continue; }
                    handles[static_cast<size_t>(p - 1)] = m;
                    slot(p).connect(ci, mono_ns());
                    touch_last_seen(addr);
                }
                // Navigation Controllers: nieuw openen, verdwenen sluiten.
                for (auto it = navs.begin(); it != navs.end();) {
                    bool present = false;
                    for (const auto& d : devs) present = present || d.node == it->node;
                    if (!present) {
                        logf("info", "Navigation Controller %s weg", redact_mac(it->address).c_str());
                        if (it->player) slot(it->player).set_nav(0, now);
                        ::close(it->fd);
                        it = navs.erase(it);
                    } else {
                        ++it;
                    }
                }
                for (const auto& d : devs) {
                    if (d.type() != "navigation") continue;
                    bool have = false;
                    for (const auto& n : navs) have = have || n.node == d.node;
                    if (have) continue;
                    if (auto f = failed_until.find(d.node); f != failed_until.end() && now < f->second) continue;
                    int fd = ::open(d.node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                    if (fd < 0) { failed_until[d.node] = now + 10'000'000'000; continue; }
                    NavReader n;
                    n.node = d.node;
                    n.fd = fd;
                    n.address = d.bluetooth() ? config::normalize_address(d.uniq) : nav_usb_address(fd);
                    if (n.address.empty()) { ::close(fd); failed_until[d.node] = now + 10'000'000'000; continue; }
                    n.player = player_for(n.address, "navigation", "");
                    logf("info", "Navigation Controller %s via %s, speler %d", redact_mac(n.address).c_str(), d.usb() ? "USB" : "Bluetooth", n.player);
                    navs.push_back(n);
                    touch_last_seen(n.address);
                }
            }
            bool any_open = false, any_tap = false, got_any = false;
            std::vector<pollfd> pfds;
            for (int p = 1; p <= kPlayers; ++p) {
                PSMove* move = handles[static_cast<size_t>(p - 1)];
                if (!move) continue;
                MoveSlot& sl = slot(p);
                any_open = true;
                now = mono_ns();
                if (auto rep = sl.due_output(now)) {
                    // Eén uitvoerrapport voor LED en rumble samen; zonder wijziging weigert PSMoveAPI zelf een rapport tot 4 s na
                    // het vorige (PSMOVE_MAX_LED_INHIBIT_MS): dat is de keepalive van de Move.
                    psmove_set_leds(move, rep->r, rep->g, rep->b);
                    psmove_set_rumble(move, rep->rumble);
                    auto res = psmove_update_leds(move);
                    sl.output_sent(now, *rep, res == Update_Success, res == Update_Ignored);
                }
                const bool zcm2 = sl.info().model == "ZCM2";
                HidrawTap& tap = sl.aux().tap;
                tap.drain();
                for (int k = 0; k < 64; ++k) {  // wachtrij leegmaken; elk rapport apart verwerken
                    int ret = psmove_poll(move);
                    if (ret <= 0) break;
                    int64_t t = mono_ns();
                    RawReport r = read_report(move, ret, zcm2, t, tap);
                    sl.on_report(r, static_cast<double>(psmove_get_temperature_in_celsius(move)));
                    got_any = true;
                }
                if (sl.tick(mono_ns())) {
                    close_handle(p, mono_ns());
                    touch_last_seen(sl.info().id);
                    continue;
                }
                if (tap.is_open()) { any_tap = true; pfds.push_back({tap.fd(), POLLIN, 0}); }
            }
            // Navigation Controllers: alle wachtende rapporten lezen.
            for (auto& n : navs) {
                uint8_t buf[64];
                for (int k = 0; k < 32; ++k) {
                    ssize_t len = ::read(n.fd, buf, sizeof buf);
                    if (len <= 0) break;
                    NavState st;
                    if (!parse_nav_report(buf, static_cast<size_t>(len), n.st, st)) continue;
                    ++n.reports;
                    if (st.buttons != n.st.buttons && n.player) slot(n.player).set_nav(st.buttons, mono_ns());
                    n.st = st;
                    got_any = true;
                }
                pfds.push_back({n.fd, POLLIN, 0});
            }
            now = mono_ns();
            // Voortgang (opname/kalibratie): de speler met een lopende kalibratie, anders speler 1.
            int prog = 1;
            for (auto& sl : slots) if (sl->calibrating()) prog = sl->player();
            for (auto& sl : slots) { sl->set_progress_owner(sl->player() == prog); sl->periodic(now); }
            if (!any_open) {
                if (!pfds.empty()) ::poll(pfds.data(), pfds.size(), 20);
                else sleep_us(20000);
                continue;
            }
            if (!got_any) {
                // Wachten op het volgende rapport: met de hidraw-taps via poll() (direct wakker bij een nieuw rapport), anders
                // de oude slaap van idle_sleep_us.
                if (any_tap) {
                    if (::poll(pfds.data(), pfds.size(), 2) > 0) {
                        for (int p = 1; p <= kPlayers; ++p) {
                            HidrawTap& tap = slot(p).aux().tap;
                            if (!tap.is_open()) continue;
                            for (const auto& pf : pfds)
                                if (pf.fd == tap.fd() && (pf.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                                    tap.close();
                                    logf("waarschuwing", "speler %d: hidraw-tap gesloten (apparaat weg?); terug naar polling", p);
                                }
                        }
                    }
                } else {
                    sleep_us(opt.idle_sleep_us);
                }
            }
            continue;
        }
#endif
        for (auto& sl : slots) sl->periodic(mono_ns());
        sleep_us(5000);
    }

#ifndef RAVENGUN_NO_PSMOVEAPI
    for (int p = 1; p <= kPlayers; ++p) {
        PSMove*& move = handles[static_cast<size_t>(p - 1)];
        if (!move) continue;
        MoveSlot& sl = slot(p);
        auto fin = sl.final_output();
        sl.disconnect(mono_ns());  // alle knoppen vrij (ook op de virtuele lightgun)
        if (fin) {  // rumble uit (en de LED, als wij die aanstuurden)
            psmove_set_rumble(move, 0);
            psmove_set_leds(move, 0, 0, 0);
            psmove_update_leds(move);
        }
        psmove_disconnect(move);
        move = nullptr;
    }
#endif
    for (auto& n : navs) ::close(n.fd);
    for (auto& sl : slots) sl->aux().on_disconnect(mono_ns());
    if (opt.recorder) opt.recorder->stop("shutdown");
    handle_jobs();
    logf("info", "HID-thread gestopt");
}

}  // namespace ravengund
