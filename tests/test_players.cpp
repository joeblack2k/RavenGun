// Meerdere spelers zonder hardware: twee MoveSlots (elk een eigen pijplijn) met synthetische rapporten, een OutputService met
// nep-guns en een nep-toetsenbord. Controleert dat elke Move zijn eigen gun stuurt, dat instellingen per controller gelden,
// dat de Navigation Controller de knoppen van zijn speler aanvult, en het LED/rumble-rapport per speler.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
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

}  // namespace

int main() {
    // Geen echte kalibratie-, bias- of opnamebestanden lezen of schrijven.
    const std::string tmp = (fs::temp_directory_path() / ("rg-players-test-" + std::to_string(::getpid()))).string();
    fs::create_directories(tmp);
    ::setenv("XDG_CONFIG_HOME", tmp.c_str(), 1);
    ::setenv("XDG_STATE_HOME", tmp.c_str(), 1);

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
            games::AaConfig acfg;
            acfg.port = ntohs(a.sin_port);
            games::AaGame ag;
            ag.key = "T";
            ag.type = games::AaType::TombRaider;
            ag.send_hz = 200;
            acfg.games.push_back(ag);
            AagamesService svc(sh, acfg);
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
