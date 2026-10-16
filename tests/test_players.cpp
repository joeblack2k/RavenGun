// Meerdere spelers zonder hardware: twee MoveSlots (elk een eigen pijplijn) met synthetische rapporten, een OutputService met
// nep-guns en een nep-toetsenbord. Controleert dat elke Move zijn eigen gun stuurt, dat instellingen per controller gelden,
// dat de Navigation Controller de knoppen van zijn speler aanvult, en het LED/rumble-rapport per speler.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <cstring>
#include <vector>

#include "aagames_service.hpp"
#include "move_slot.hpp"
#include "output_service.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/core/buttons.hpp"
#include "ravengun/core/navcon.hpp"
#include "ravengun/iobridge/tp_shm.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/output/key_table.hpp"
#include "shared.hpp"

using namespace ravengun;
using namespace ravengund;
namespace fs = std::filesystem;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

struct FakeGun : output::GunDevice {
    int player;
    bool exists = false;
    int x = -1, y = -1, positions = 0;
    std::set<int> down;
    explicit FakeGun(int p) : player(p) {}
    bool create(std::string&) override { exists = true; return true; }
    void destroy() override { exists = false; down.clear(); }
    bool created() const override { return exists; }
    bool write_position(int nx, int ny) override { x = nx; y = ny; ++positions; return exists; }
    bool write_button(output::GunButton b, bool d) override {
        if (d) down.insert(static_cast<int>(b)); else down.erase(static_cast<int>(b));
        return exists;
    }
};

struct FakeKbd : output::KeyDevice {
    bool exists = false;
    std::set<int> pressed;
    bool create(std::string&) override { exists = true; return true; }
    void destroy() override { exists = false; pressed.clear(); }
    bool created() const override { return exists; }
    bool write_key(int code, bool d) override { if (d) pressed.insert(code); else pressed.erase(code); return exists; }
};

// Rapport met draaisnelheid w_z (rad/s rond de verticaal), plat liggend (richtas horizontaal).
core::RawReport rep(int64_t t_ns, double w_z, uint32_t buttons = 0) {
    core::RawReport r;
    r.t_rx_ns = t_ns;
    r.battery_raw = 4;
    r.buttons = buttons;
    for (int k = 0; k < 2; ++k) { r.accel[k] = {0, 0, 1}; r.gyro[k] = {0, 0, w_z}; }
    return r;
}

const std::string A = "00:00:00:aa:00:01", B = "00:00:00:aa:00:02";

// ---- Synthetische invoer naast live Move-rapporten -------------------------------------------------------------------
// Een verbonden Move zet bij elk rapport (~80 Hz) zijn eigen knoppen en toetsen terug. Een synthetische druk
// (ravengunctl output-button/key) moet daarom zijn hele duur zichtbaar blijven, ook voor wat de Move zelf koppelt.

// Telt hoe vaak een niveau omhoog/omlaag gaat en hoeveel samples het aanwezig was (elke ~2 ms).
struct Track {
    int rises = 0, falls = 0, present = 0, samples = 0;
    bool prev = false;
    void add(bool v) {
        ++samples;
        if (v) ++present;
        if (v && !prev) ++rises;
        if (!v && prev) ++falls;
        prev = v;
    }
};

// Het byte-niveau zoals de tp-shm-route het schrijft: een 16-bit bitveld op byte 8 met een eigen profiel.
iobridge::TpShmProfile bench_profile() {
    iobridge::TpShmProfile p;
    p.name = "Bench";
    p.size = 64;
    p.bits = {{"coin1", 8, 0}, {"p1.start", 8, 1}, {"p1.trigger", 8, 2}, {"p1.reload", 8, 3}, {"test", 8, 8}};
    p.mirror_first = -1; p.mirror_last = -1;
    return p;
}

uint16_t shm_word(OutputService& out, const iobridge::TpShmProfile& prof) {
    iobridge::IoState st;
    out.io_snapshot(st);
    uint8_t b[64] = {};
    iobridge::tp_shm_render(prof, st, b, sizeof b);
    return static_cast<uint16_t>(b[8] | (b[9] << 8));
}

void test_synthetic_hold() {
    std::map<int, FakeGun*> guns;
    auto kbd = std::make_unique<FakeKbd>();
    FakeKbd* kb = kbd.get();
    OutputService out([&](int p) { auto g = std::make_unique<FakeGun>(p); guns[p] = g.get(); return g; }, std::move(kbd));
    SharedState sh;
    sh.output = &out;
    HidOptions base;
    base.bias_dir.clear();
    base.recorder = nullptr;
    config::Settings s;
    config::register_controller(s, A, "move", "ZCM1", 1);
    MoveSlot p1(1, sh, base);
    p1.configure(A, config::effective(s, A), {});   // standaard: Vierkant = toets 1, Start/Driehoek = toets 5, Kruis = herladen
    CHECK(out.arm().ok);
    ControllerInfo ca;
    ca.id = A; ca.model = "ZCM1"; ca.connection = "bluetooth";
    p1.connect(ca, mono_ns());

    // Live Move-rapporten op ~80 Hz in een eigen thread (zoals de HID-thread); de knoppen zijn live instelbaar.
    std::atomic<uint32_t> live_buttons{0};
    std::atomic<bool> stop{false};
    std::thread feeder([&] {
        while (!stop.load()) {
            p1.on_report(rep(mono_ns(), 0, live_buttons.load()), NAN);
            std::this_thread::sleep_for(std::chrono::microseconds(12500));
        }
    });
    auto sleep_ms = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    auto io = [&] { iobridge::IoState st; out.io_snapshot(st); return st; };
    // Neemt ~`ms` lang samples van `pick` terwijl `act` in een eigen thread loopt (de tap blokkeert 80 ms).
    auto measure = [&](int ms, const std::function<void()>& act, const std::function<bool(const iobridge::IoState&)>& pick) {
        Track t;
        std::thread th(act);
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            t.add(pick(io()));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        th.join();
        return t;
    };
    // Een tap van 80 ms moet in een venster van 140 ms precies een keer omhoog en omlaag gaan en ruim 60 ms zichtbaar zijn.
    auto whole_tap = [](const Track& t) { return t.rises == 1 && t.falls == 1 && t.present >= 30 && !t.prev; };
    sleep_ms(100);
    CHECK(io().armed && !io().player[0].reload);

    // Controle: de live Move werkt gewoon (Kruis = herladen) en laat los.
    live_buttons.store(core::kBtnCross);
    sleep_ms(60);
    CHECK(io().player[0].reload && guns[1]->created());
    live_buttons.store(0);
    sleep_ms(60);
    CHECK(!io().player[0].reload);

    // Een toets die de Move niet koppelt (test = toets 0) blijft de hele tap staan.
    CHECK(whole_tap(measure(140, [&] { out.key(1, output::key_code("0"), "tap"); }, [](const iobridge::IoState& st) { return st.sys.test; })));
    // Dezelfde proef voor wat de Move wel koppelt: trekker, herladen, start (toets 1) en munt (toets 5).
    CHECK(whole_tap(measure(140, [&] { out.button(output::GunButton::Trigger, "tap"); }, [](const iobridge::IoState& st) { return st.player[0].trigger; })));
    CHECK(whole_tap(measure(140, [&] { out.button(output::GunButton::Reload, "tap"); }, [](const iobridge::IoState& st) { return st.player[0].reload; })));
    CHECK(whole_tap(measure(140, [&] { out.key(1, output::key_code("1"), "tap"); }, [](const iobridge::IoState& st) { return st.player[0].start; })));
    CHECK(whole_tap(measure(140, [&] { out.key(1, output::key_code("5"), "tap"); }, [](const iobridge::IoState& st) { return st.sys.coin[0]; })));
    // En na afloop is alles weer los (geen blijvende synthetische stand).
    CHECK(!io().player[0].trigger && !io().player[0].reload && !io().player[0].start && !io().sys.coin[0] && !io().sys.test);
    CHECK(guns[1]->down.empty() && kb->pressed.empty());

    // Op byteniveau (tp-shm): de bit van de trekker (0x4) staat de hele tap in het bitveld.
    {
        const auto prof = bench_profile();
        Track t;
        std::thread th([&] { out.button(output::GunButton::Trigger, "tap"); });
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(140);
        while (std::chrono::steady_clock::now() < end) { t.add((shm_word(out, prof) & 0x4) != 0); sleep_ms(2); }
        th.join();
        CHECK(whole_tap(t));
        CHECK(shm_word(out, prof) == 0);
        // Een toets zonder Move-koppeling (test, 0x100) en een gekoppelde (start, 0x2).
        Track tt, ts;
        std::thread th2([&] { out.key(1, output::key_code("0"), "tap"); });
        const auto end2 = std::chrono::steady_clock::now() + std::chrono::milliseconds(140);
        while (std::chrono::steady_clock::now() < end2) { tt.add((shm_word(out, prof) & 0x100) != 0); sleep_ms(2); }
        th2.join();
        CHECK(whole_tap(tt));
        std::thread th3([&] { out.key(1, output::key_code("1"), "tap"); });
        const auto end3 = std::chrono::steady_clock::now() + std::chrono::milliseconds(140);
        while (std::chrono::steady_clock::now() < end3) { ts.add((shm_word(out, prof) & 0x2) != 0); sleep_ms(2); }
        th3.join();
        CHECK(whole_tap(ts));
    }

    // down blijft staan tot een expliciete up, ook als de Move steeds "los" meldt.
    CHECK(out.button(output::GunButton::Trigger, "down").ok && out.key(1, output::key_code("1"), "down").ok);
    sleep_ms(200);
    CHECK(io().player[0].trigger && io().player[0].start && guns[1]->down.count(static_cast<int>(output::GunButton::Trigger)));
    CHECK(out.button(output::GunButton::Trigger, "up").ok && out.key(1, output::key_code("1"), "up").ok);
    sleep_ms(40);
    CHECK(!io().player[0].trigger && !io().player[0].start);

    // Een live druk tijdens een synthetische druk: het live loslaten wist de synthetische druk niet, en omgekeerd.
    live_buttons.store(core::kBtnCross);
    sleep_ms(50);
    CHECK(out.button(output::GunButton::Reload, "down").ok);
    live_buttons.store(0);
    sleep_ms(80);
    CHECK(io().player[0].reload);                           // live los, synthetisch nog vast
    CHECK(out.button(output::GunButton::Reload, "up").ok);
    sleep_ms(30);
    CHECK(!io().player[0].reload);
    live_buttons.store(core::kBtnCross);
    sleep_ms(50);
    CHECK(out.button(output::GunButton::Reload, "tap").ok);
    CHECK(io().player[0].reload);                           // de live druk blijft na het einde van de tap staan
    live_buttons.store(0);
    sleep_ms(50);
    CHECK(!io().player[0].reload);

    // Disarm geeft alles vrij, ook een synthetische down; na opnieuw wapenen komt hij niet terug.
    CHECK(out.button(output::GunButton::Trigger, "down").ok && out.key(1, output::key_code("0"), "down").ok);
    sleep_ms(30);
    CHECK(io().player[0].trigger && io().sys.test);
    out.disarm();
    CHECK(guns[1]->down.empty() && kb->pressed.empty() && !io().armed);
    CHECK(out.arm().ok);
    sleep_ms(100);
    CHECK(io().armed && !io().player[0].trigger && !io().sys.test && guns[1]->down.empty() && kb->pressed.empty());

    // Bronuitval (Move weg): ook een synthetische down gaat los.
    CHECK(out.button(output::GunButton::Trigger, "down").ok);
    stop.store(true);
    feeder.join();
    p1.disconnect(mono_ns());
    CHECK(!io().player[0].trigger && guns[1]->down.empty());
}

}  // namespace

int main() {
    // Geen echte kalibratie-, bias- of opnamebestanden lezen of schrijven.
    const std::string tmp = (fs::temp_directory_path() / ("rg-players-test-" + std::to_string(::getpid()))).string();
    fs::create_directories(tmp);
    ::setenv("XDG_CONFIG_HOME", tmp.c_str(), 1);
    ::setenv("XDG_STATE_HOME", tmp.c_str(), 1);
    test_synthetic_hold();

    std::map<int, FakeGun*> guns;
    auto kbd = std::make_unique<FakeKbd>();
    FakeKbd* kb = kbd.get();
    {
        OutputService out([&](int p) { auto g = std::make_unique<FakeGun>(p); guns[p] = g.get(); return g; }, std::move(kbd));
        SharedState sh;
        sh.output = &out;
        HidOptions base;
        base.bias_dir.clear();
        base.recorder = nullptr;
        base.led_enabled = true; base.led_r = 0; base.led_g = 255; base.led_b = 255;

        // Instellingen: A is speler 1 zonder afwijkingen; B is speler 2 met filter uit en Kruis = granaat.
        config::Settings s;
        int pa = config::register_controller(s, A, "move", "ZCM1", 1);
        int pb = config::register_controller(s, B, "move", "ZCM1", 2);
        CHECK(pa == 1 && pb == 2);
        config::modify_for(s, B, {"filter.enabled", "gun_buttons.map.cross"}, [](config::Settings& e) { e.filter.enabled = false; e.gun_buttons.map["cross"] = "grenade"; });
        CHECK(config::validate(s).empty());

        MoveSlot p1(1, sh, base), p2(2, sh, base);
        p1.configure(A, config::effective(s, A), {});
        p2.configure(B, config::effective(s, B), {});
        CHECK(p1.filter().enabled && !p2.filter().enabled);
        CHECK(p1.options().led_g == 255 && p1.options().led_r == 0);                       // speler 1: cyaan
        CHECK(p2.options().led_r == 255 && p2.options().led_g == 0 && p2.options().led_b == 255);  // speler 2: magenta
        out.set_active_players(0b11);
        CHECK(out.arm().ok);
        CHECK(guns.size() == 4 && guns[1]->exists && guns[2]->exists && !guns[3]->exists && !guns[4]->exists);
        CHECK(kb->exists);

        ControllerInfo ca, cb;
        ca.id = A; ca.model = "ZCM1"; ca.connection = "bluetooth";
        cb.id = B; cb.model = "ZCM1"; cb.connection = "bluetooth";
        int64_t t = mono_ns();
        p1.connect(ca, t);
        p2.connect(cb, t);
        CHECK(p1.connected() && p2.connected());
        auto infos = sh.slot_infos();
        CHECK(infos[0].connected && infos[0].address == A && infos[1].connected && infos[1].address == B && !infos[2].connected);

        // Twee pijplijnen tegelijk: speler 1 draait naar links, speler 2 naar rechts (om de verticaal).
        const double w = 20 * 3.14159265 / 180;
        for (int i = 0; i < 60; ++i) {
            t += 11'270'000;
            p1.on_report(rep(t, i < 5 ? 0 : w), NAN);
            p2.on_report(rep(t, i < 5 ? 0 : -w), NAN);
        }
        auto s1 = sh.get(1).sample, s2 = sh.get(2).sample;
        CHECK(s1.aim_valid && s2.aim_valid);
        CHECK(s1.controller_id == A && s2.controller_id == B);
        CHECK(s1.x_raw < 0.4 && s2.x_raw > 0.6);  // tegengesteld, elk zijn eigen richting
        CHECK(guns[1]->positions > 0 && guns[2]->positions > 0 && guns[3]->positions == 0);
        CHECK(guns[1]->x < 16384 && guns[2]->x > 16384);

        // Knoppen: Kruis is herladen bij speler 1 en granaat bij speler 2; Vierkant is Start (toets 1 en 2).
        t += 11'270'000;
        core::RawReport r1 = rep(t, 0, core::kBtnCross | core::kBtnSquare), r2 = rep(t, 0, core::kBtnCross | core::kBtnSquare);
        p1.on_report(r1, NAN);
        p2.on_report(r2, NAN);
        CHECK(guns[1]->down.count(static_cast<int>(output::GunButton::Reload)) && !guns[1]->down.count(static_cast<int>(output::GunButton::Grenade)));
        CHECK(guns[2]->down.count(static_cast<int>(output::GunButton::Grenade)) && !guns[2]->down.count(static_cast<int>(output::GunButton::Reload)));
        CHECK(kb->pressed.count(output::key_code("1")) && kb->pressed.count(output::key_code("2")));
        // Speler 1 laat los: alleen zijn knoppen en zijn Start-toets gaan los.
        t += 11'270'000;
        p1.on_report(rep(t, 0), NAN);
        CHECK(guns[1]->down.empty() && !kb->pressed.count(output::key_code("1")) && kb->pressed.count(output::key_code("2")));
        CHECK(guns[2]->down.count(static_cast<int>(output::GunButton::Grenade)));

        // Navigation Controller van speler 2: L1 = herladen (standaard), alleen op gun 2.
        t += 11'270'000;
        p2.on_report(rep(t, 0), NAN);
        CHECK(guns[2]->down.empty());
        p2.set_nav(core::kNavL1, t);
        CHECK(guns[2]->down.count(static_cast<int>(output::GunButton::Reload)) && guns[1]->down.empty());
        p2.set_nav(0, t);
        CHECK(guns[2]->down.empty());

        // Unity-route: aim-momentopname, toets via de service, en rumble uit de Recoil-vlag van de game (niet uit de trekker).
        {
            OutputService::AimSnapshot as;
            CHECK(out.aim_snapshot(2, as) && as.have_position && as.u > 0.5 && as.v >= 0 && as.v <= 1);
            CHECK(as.reload == false && as.trigger == false);
            CHECK(!out.aim_snapshot(4, as));          // speler 4 bestaat niet
            CHECK(out.key(2, output::key_code("5"), "down").ok && kb->pressed.count(output::key_code("5")));
            CHECK(out.key(2, output::key_code("5"), "up").ok && !kb->pressed.count(output::key_code("5")));
            CHECK(!out.key(2, output::key_code("5"), "bogus").ok && !out.key(9, output::key_code("5"), "tap").ok);
            sh.game_rumble_mask.store(0b10);           // speler 2: rumble komt uit de game
            t += 5'000'000;
            p2.tick(t);
            auto q0 = p2.due_output(t);
            CHECK(!q0 || q0->rumble == 0);             // zonder Recoil geen trilling
            sh.game_recoil[1].fetch_add(1);
            t += 5'000'000;
            p2.tick(t);
            auto q1 = p2.due_output(t + 30'000'000);
            CHECK(q1 && q1->rumble == 180);            // een schot: puls met de ingestelde sterkte
            t += 2'000'000'000;  // ruim voorbij de puls (de testklok loopt iets voor op de echte klok die emit() gebruikt)
            p2.tick(t);
            auto q2 = p2.due_output(t + 30'000'000);
            CHECK(q2 && q2->rumble == 0);              // de puls is afgelopen
            sh.game_rumble_mask.store(0);
        }

        // LED/rumble-rapport per speler: elk zijn eigen kleur; knipperen geeft wit.
        auto o1 = p1.due_output(t), o2 = p2.due_output(t);
        CHECK(o1 && o2 && o1->led_due && o1->g == 255 && o1->r == 0 && o2->r == 255 && o2->g == 0);
        p1.output_sent(t, *o1, true);
        p2.output_sent(t, *o2, true);
        CHECK(!p1.due_output(t + 1'000'000));  // keepalive pas na led_refresh_ms
        p2.blink(t, 1000);
        auto ob = p2.due_output(t + 30'000'000);
        CHECK(ob && ob->r == 255 && ob->g == 255 && ob->b == 255);

        // PS-knop lang vasthouden = game stoppen (ps_hold_elapsed, klok los van echte tijd). Drempel 3 s, één keer per keer
        // vasthouden, reset bij loslaten. Een korte druk (recenter) raakt dit niet.
        {
            const int64_t s = 1'000'000'000, t0 = 10'000'000'000;  // 1 s in ns, een willekeurig startmoment
            CHECK(!p1.ps_hold_elapsed(true, t0));                  // net ingedrukt
            CHECK(!p1.ps_hold_elapsed(true, t0 + 2 * s));          // 2 s: nog niet
            CHECK(!p1.ps_hold_elapsed(true, t0 + 3 * s - 1));      // net voor 3 s: nog niet
            CHECK(p1.ps_hold_elapsed(true, t0 + 3 * s));           // 3 s: stoppen
            CHECK(!p1.ps_hold_elapsed(true, t0 + 5 * s));          // blijft vastgehouden: niet nog eens
            CHECK(!p1.ps_hold_elapsed(false, t0 + 6 * s));         // losgelaten: reset
            CHECK(!p1.ps_hold_elapsed(true, t0 + 6 * s));          // opnieuw ingedrukt
            CHECK(!p1.ps_hold_elapsed(true, t0 + 8 * s));          // 2 s sinds opnieuw: nog niet
            CHECK(p1.ps_hold_elapsed(true, t0 + 9 * s));           // weer 3 s: stoppen
            // Een korte druk (minder dan 3 s) stopt niet.
            CHECK(!p1.ps_hold_elapsed(false, t0 + 10 * s));
            CHECK(!p1.ps_hold_elapsed(true, t0 + 10 * s) && !p1.ps_hold_elapsed(false, t0 + 10 * s + s / 2));
        }

        // Status per speler.
        auto st = json::parse(p2.status_json(false, t));
        CHECK(st.ok && st.value.find("player")->num == 2 && !st.value.find("config")->find("filter")->find("enabled")->b);
        CHECK(st.value.find("gun_buttons")->find("map")->find("cross")->str == "grenade");
        CHECK(st.value.find("controller")->find("id")->str == "xx:xx:xx:xx:xx:02");

        // Speler 1 valt weg: zijn gun laat los, speler 2 speelt door.
        t += 11'270'000;
        p2.on_report(rep(t, 0, core::kBtnCross), NAN);
        p1.disconnect(t);
        CHECK(!p1.connected() && p2.connected() && guns[2]->down.count(static_cast<int>(output::GunButton::Grenade)));
        CHECK(!sh.slot_infos()[0].connected && sh.slot_infos()[1].connected);

        // Uitvoerstatus: beide spelers met hun eigen identiteit.
        auto os = json::parse(out.status_json());
        CHECK(os.ok && os.value.find("players")->arr.size() == 4);
        const auto& pl = os.value.find("players")->arr;
        CHECK(pl[0].find("device")->find("product")->str == "7202" && pl[1].find("device")->find("product")->str == "7203");
        CHECK(pl[1].find("device")->find("name")->str == "RavenGun Virtual Lightgun P2" && pl[1].find("active")->b && !pl[2].find("active")->b);
        CHECK(os.value.find("device")->find("product")->str == "7202");  // speler 1 ook op het hoogste niveau (zoals voorheen)

        // Unity-route: de service stuurt de aim van speler 2 als pixels naar een nep-plugin en geeft Recoil door aan SharedState.
        {
            int lfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a);
            ::listen(lfd, 1);
            socklen_t al = sizeof a;
            ::getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &al);
            // patches/T/unity.json (de game) en unity-route.json (de poort van deze nep-plugin).
            fs::create_directories(tmp + "/unity-patches/T");
            std::ofstream(tmp + "/unity-patches/T/unity.json") << R"({"type":"tomb_raider","folder":"T","send_hz":200})";
            std::ofstream(tmp + "/unity-route.json") << "{\"port\":" << ntohs(a.sin_port) << "}";
            ravengun::launch::UnityPaths up;
            up.patches = tmp + "/unity-patches";
            up.route_json = tmp + "/unity-route.json";
            AagamesService svc(sh, up);
            svc.set_timeouts(400, 5000);
            CHECK(!svc.start("Bestaat niet").empty() && !svc.active());
            CHECK(svc.start("T").empty() && svc.active() && sh.game_rumble_mask.load() == 0xF);
            pollfd pf{lfd, POLLIN, 0};
            CHECK(::poll(&pf, 1, 2000) > 0);
            int cfd = ::accept(lfd, nullptr, nullptr);
            CHECK(cfd >= 0);
            std::vector<uint8_t> got;
            for (int i = 0; i < 100 && got.size() < 42 * 20; ++i) {
                pollfd rp{cfd, POLLIN, 0};
                if (::poll(&rp, 1, 50) > 0) { uint8_t b[512]; ssize_t r = ::recv(cfd, b, sizeof b, 0); if (r <= 0) break; got.insert(got.end(), b, b + r); }
            }
            CHECK(got.size() >= 42 * 20);
            const size_t base = (got.size() / 42 - 1) * 42;     // het laatste volledige pakket (de stroom heeft geen kaders)
            float x2, y2, x1;
            std::memcpy(&x1, &got[base], 4);
            std::memcpy(&x2, &got[base + 4], 4);                // Axis_X speler 2
            std::memcpy(&y2, &got[base + 16 + 4], 4);           // Axis_Y speler 2
            OutputService::AimSnapshot as2;
            CHECK(out.aim_snapshot(2, as2));
            CHECK(std::fabs(x2 - as2.u * 1919) < 1.0f);
            CHECK(std::fabs(y2 - (1.0 - as2.v) * 1079) < 1.0f);  // Y-oorsprong onderaan (Unity)
            OutputService::AimSnapshot as1;
            CHECK(out.aim_snapshot(1, as1) && std::fabs(x1 - as1.u * 1919) < 1.0f);   // speler 1: de laatste positie blijft staan
            CHECK(got[base + 32] == 1);                          // EnableInputsHack
            // Recoil van speler 2 (60-byte uitvoer): de teller van SharedState gaat omhoog.
            const uint64_t rc0 = sh.game_recoil[1].load();      // staat al op 1 door het rumble-blok hierboven
            std::vector<uint8_t> fr(65, 0);
            const int32_t len = 61;
            std::memcpy(fr.data(), &len, 4);
            fr[4] = 2;
            fr[5 + 56 + 1] = 1;                                  // Recoil[1]
            ::send(cfd, fr.data(), fr.size(), MSG_NOSIGNAL);
            for (int i = 0; i < 100 && sh.game_recoil[1].load() == rc0; ++i) ::usleep(10000);
            CHECK(sh.game_recoil[1].load() == rc0 + 1 && sh.game_recoil[0].load() == 0);
            auto js = json::parse(svc.status_json());
            CHECK(js.ok && js.value.find("active")->b && js.value.find("connected")->b);
            // De game verdwijnt: de route stopt vanzelf (en de rumble is weer aan de trekker).
            ::close(cfd);
            for (int i = 0; i < 300 && svc.active(); ++i) ::usleep(10000);
            CHECK(!svc.active() && sh.game_rumble_mask.load() == 0);
            ::close(lfd);
        }

        // Een nieuwe speler terwijl de uitvoer gewapend is: zijn gun komt er meteen.
        out.set_active_players(0b111);
        CHECK(guns[3]->exists);
        out.disarm();
        CHECK(guns[2]->down.empty() && kb->pressed.empty());
        p2.disconnect(t);
    }
    fs::remove_all(tmp);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
