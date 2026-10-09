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

        // LED/rumble-rapport per speler: elk zijn eigen kleur; knipperen geeft wit.
        auto o1 = p1.due_output(t), o2 = p2.due_output(t);
        CHECK(o1 && o2 && o1->led_due && o1->g == 255 && o1->r == 0 && o2->r == 255 && o2->g == 0);
        p1.output_sent(t, *o1, true);
        p2.output_sent(t, *o2, true);
        CHECK(!p1.due_output(t + 1'000'000));  // keepalive pas na led_refresh_ms
        p2.blink(t, 1000);
        auto ob = p2.due_output(t + 30'000'000);
        CHECK(ob && ob->r == 255 && ob->g == 255 && ob->b == 255);

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
