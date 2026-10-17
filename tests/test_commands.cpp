// Tests van de opdrachtlaag van ravengund zonder hardware: een nep-HID-context draait de jobs, de
// camera-service wordt niet gestart. Dekt set_led (incl. tracker-tint), set_camera (validatie en de
// foutmelding zonder camera), set_filter/set_aim/set_web (bewaren), info, settings en diagnostics.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <mutex>
#include <thread>
#include <unistd.h>

#include "camera.hpp"
#include "controllers.hpp"
#include "ravengun/core/magnetometer.hpp"
#include <map>
#include <vector>
#include "commands.hpp"
#include "hid_thread.hpp"
#include "output_service.hpp"
#include "ravengun/config/led_color.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/games/aagames.hpp"
#include "ravengun/games/games.hpp"
#include "aagames_service.hpp"
#include "mouse_service.hpp"
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

using namespace ravengun;
using namespace ravengund;
namespace fs = std::filesystem;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static bool contains(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }

static std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct FakeHid final : HidContext {
    int led_calls = 0;
    bool led_enabled = false;
    uint8_t r = 0, g = 0, b = 0;
    core::OneEuroConfig f;
    core::AimConfig a;
    core::PredictConfig pc;
    int predict_calls = 0, aim_calls = 0;
    std::string status_json(bool full) override {
        return std::string("{\"daemon\":{\"uptime_s\":1},\"controller\":{\"id\":\"") + (full ? "00:11:22:33:44:55" : "00:11:22:xx:xx:xx") + "\"}}";
    }
    bool recenter() override { return false; }
    core::OneEuroConfig filter() const override { return f; }
    void set_filter(const core::OneEuroConfig& c) override { f = c; }
    core::AimConfig aim() const override { return a; }
    void set_aim(const core::AimConfig& c) override { a = c; ++aim_calls; }
    core::PredictConfig predict() const override { return pc; }
    void set_predict(const core::PredictConfig& c) override { pc = c; ++predict_calls; }
    void set_led(bool en, uint8_t rr, uint8_t gg, uint8_t bb) override { ++led_calls; led_enabled = en; r = rr; g = gg; b = bb; }
    bool led(bool& en, uint8_t rgb[3]) const override { en = led_enabled; rgb[0] = r; rgb[1] = g; rgb[2] = b; return true; }
    std::string record(const ipc::Request&) override { return "{}"; }
    std::string calib_mag(const ipc::Request&) override { return "{}"; }
    output::RumbleConfig rc;
    int rumble_sets = 0, rumble_tests = 0, last_test_ms = 0;
    bool connected = true;
    output::RumbleConfig rumble() const override { return rc; }
    void set_rumble(const output::RumbleConfig& c) override { rc = c; ++rumble_sets; }
    std::string rumble_test(int ms) override {
        if (!rc.enabled) return "!unavailable|rumble staat uit";
        if (!connected) return "!unavailable|geen Move verbonden";
        ++rumble_tests; last_test_ms = ms;
        return "1";
    }
    core::FusionMode fm = core::FusionMode::Mahony;
    core::FusionMode fusion() const override { return fm; }
    void set_fusion(core::FusionMode m) override { fm = m; }
    bool learning = true;
    bool recenter_learning() const override { return learning; }
    void set_recenter_learning(bool on) override { learning = on; }
    output::LiveButtonMap gun_map;
    int gun_map_sets = 0;
    void set_gun_buttons(const output::LiveButtonMap& m) override { gun_map = m; ++gun_map_sets; }
};

// Voert opdrachten uit; de HID-jobs draaien in een eigen thread (zoals in de daemon).
struct Rig {
    SharedState sh;
    FakeHid hid;
    std::atomic<bool> stop{false};
    std::thread t;
    Rig() : t([this] { while (!stop.load()) { sh.run_jobs(hid); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }) {}
    ~Rig() { stop.store(true); t.join(); }
    json::Value call(const std::string& line, bool* ok = nullptr, std::string* code = nullptr, std::string* msg = nullptr) {
        auto pr = json::parse(execute_line(sh, line));
        const json::Value* okv = pr.value.find("ok");
        bool good = pr.ok && okv && okv->b;
        if (ok) *ok = good;
        if (!good && pr.ok) {
            if (const json::Value* e = pr.value.find("error")) {
                if (code && e->find("code")) *code = e->find("code")->str;
                if (msg && e->find("message")) *msg = e->find("message")->str;
            }
        }
        if (good) return *pr.value.find("result");
        return pr.value;
    }
};

static double num(const json::Value& v, const char* k) { const json::Value* x = v.find(k); return x ? x->num : -9999; }

static void test_set_led(const std::string& dir) {
    Rig rig;
    config::SettingsStore store(dir + "/led/config.json");
    store.load();
    rig.sh.settings = &store;
    CameraOptions co;  // camera aan, maar niet gestart: set_tracker/tracker() werken wel
    CameraService cam(rig.sh, co);
    rig.sh.camera = &cam;

    bool ok = false;
    std::string code, msg;
    // Preset magenta: LED naar de HID-thread, tracker-tint 86 (gemeten) met tolerantie 30.
    json::Value r = rig.call(R"({"v":1,"cmd":"set_led","preset":"magenta"})", &ok);
    CHECK(ok);
    CHECK(rig.hid.led_calls == 1 && rig.hid.led_enabled && rig.hid.r == 255 && rig.hid.g == 0 && rig.hid.b == 255);
    CHECK(cam.tracker().hue_deg == 86 && cam.tracker().hue_tol_deg == 30);
    const json::Value* tr = r.find("tracker");
    CHECK(tr && num(*tr, "hue_deg") == 86 && num(*tr, "hue_tol_deg") == 30 && tr->find("applied_live")->b && tr->find("changed")->b);
    CHECK(tr && tr->find("source")->str == "gemeten");
    CHECK(r.find("led")->find("preset")->str == "magenta" && !r.find("led")->find("custom")->b);
    CHECK(r.find("saved")->b && r.find("warnings")->arr.empty());
    CHECK(slurp(dir + "/led/config.json").find("\"preset\":\"magenta\"") != std::string::npos);
    CHECK(store.get().tracker.hue_deg == 86 && store.get().led.enabled);

    // Alle andere presets: de tint volgt de gemeten waarde.
    struct E { const char* n; double hue; int r, g, b; };
    for (const E& e : {E{"cyaan", -75, 0, 255, 255}, E{"blauw", -31, 0, 0, 255}, E{"groen", -145, 0, 255, 0}, E{"rood", 119, 255, 0, 0}}) {
        r = rig.call(std::string(R"({"v":1,"cmd":"set_led","preset":")") + e.n + "\"}", &ok);
        CHECK(ok && cam.tracker().hue_deg == e.hue && cam.tracker().hue_tol_deg == 30);
        CHECK(rig.hid.r == e.r && rig.hid.g == e.g && rig.hid.b == e.b);
        CHECK(store.get().tracker.hue_deg == e.hue);
    }

    // RGB dat exact een preset is, gedraagt zich als die preset (gemeten tint).
    rig.call(R"({"v":1,"cmd":"set_led","r":255,"g":0,"b":0})", &ok);
    CHECK(ok && cam.tracker().hue_deg == 119 && store.get().led.preset == "rood");

    // Custom: nominale tint, ruimere tolerantie en een waarschuwing.
    r = rig.call(R"({"v":1,"cmd":"set_led","r":255,"g":128,"b":0})", &ok);
    CHECK(ok && r.find("led")->find("custom")->b && r.find("tracker")->find("source")->str == "nominaal");
    CHECK(cam.tracker().hue_tol_deg == config::kCustomHueTolDeg && cam.tracker().hue_deg > 90 && cam.tracker().hue_deg < 180);
    CHECK(r.find("warnings")->arr.size() == 1);
    CHECK(store.get().led.preset == "custom" && store.get().tracker.hue_tol_deg == config::kCustomHueTolDeg);

    // Wit: LED wel, tracker-tint blijft zoals hij was, met een waarschuwing.
    double hue_before = cam.tracker().hue_deg;
    r = rig.call(R"({"v":1,"cmd":"set_led","preset":"wit"})", &ok);
    CHECK(ok && rig.hid.r == 255 && rig.hid.g == 255 && cam.tracker().hue_deg == hue_before);
    CHECK(!r.find("tracker")->find("changed")->b && r.find("warnings")->arr.size() == 1);

    // Fouten: onbekende preset, ongeldige RGB, onbekend veld (parser).
    int calls = rig.hid.led_calls;
    rig.call(R"({"v":1,"cmd":"set_led","preset":"paars"})", &ok, &code, &msg);
    CHECK(!ok && code == "invalid_argument" && contains(msg, "paars"));
    rig.call(R"({"v":1,"cmd":"set_led","r":300,"g":0,"b":0})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    CHECK(rig.hid.led_calls == calls);  // niets naar de LED bij een fout

    // Tijdelijke kleur (het startscherm van de app): alleen live, niets bewaard, de trackertint blijft; restore zet de kleur van vóór de
    // eerste tijdelijke kleur terug (ook na twee tijdelijke kleuren achter elkaar).
    {
        rig.call(R"({"v":1,"cmd":"set_led","r":10,"g":20,"b":30})", &ok);
        CHECK(ok);
        const std::string saved = slurp(dir + "/led/config.json");
        const double hue = cam.tracker().hue_deg;
        r = rig.call(R"({"v":1,"cmd":"set_led","player":1,"preset":"blauw","temporary":true})", &ok);
        CHECK(ok && r.find("temporary")->b && rig.hid.r == 0 && rig.hid.g == 0 && rig.hid.b == 255);
        r = rig.call(R"({"v":1,"cmd":"set_led","player":1,"r":255,"g":0,"b":0,"temporary":true})", &ok);
        CHECK(ok && rig.hid.r == 255 && rig.hid.b == 0);
        CHECK(slurp(dir + "/led/config.json") == saved && cam.tracker().hue_deg == hue);
        r = rig.call(R"({"v":1,"cmd":"set_led","player":1,"restore":true})", &ok);
        CHECK(ok && r.find("restored")->b && rig.hid.r == 10 && rig.hid.g == 20 && rig.hid.b == 30);
        r = rig.call(R"({"v":1,"cmd":"set_led","player":1,"restore":true})", &ok);
        CHECK(ok && !r.find("restored")->b);   // niets meer te herstellen
        rig.call(R"({"v":1,"cmd":"set_led","player":1,"restore":true,"preset":"rood"})", &ok, &code);
        CHECK(!ok && code == "invalid_argument");
        rig.call(R"({"v":1,"cmd":"set_led","controller":"default","preset":"rood","temporary":true})", &ok, &code);
        CHECK(!ok && code == "invalid_argument");
        rig.call(R"({"v":1,"cmd":"set_led","player":1,"temporary":true})", &ok, &code);
        CHECK(!ok && code == "invalid_argument");
        rig.call(R"({"v":1,"cmd":"set_led","player":1,"preset":"rood","temporary":"ja"})", &ok, &code);
        CHECK(!ok && code == "invalid_argument");
    }

    // Zonder camera-service of opslag: de LED werkt, de tracker-tint wordt alleen gemeld.
    Rig bare;
    r = bare.call(R"({"v":1,"cmd":"set_led","preset":"groen"})", &ok);
    CHECK(ok && bare.hid.g == 255 && !r.find("tracker")->find("applied_live")->b && !r.find("saved")->b);
}

static void test_set_camera(const std::string& dir) {
    bool ok = false;
    std::string code, msg;
    {
        // Camera uit (--no-camera): nette melding, niets bewaard.
        Rig rig;
        config::SettingsStore store(dir + "/cam1/config.json");
        store.load();
        rig.sh.settings = &store;
        CameraOptions co;
        co.enabled = false;
        CameraService cam(rig.sh, co);
        rig.sh.camera = &cam;
        rig.call(R"({"v":1,"cmd":"set_camera","exposure":60})", &ok, &code, &msg);
        CHECK(!ok && code == "unavailable" && contains(msg, "--no-camera"));
        CHECK(!fs::exists(dir + "/cam1/config.json") && store.get().camera.exposure == 50);
        rig.call(R"({"v":1,"cmd":"set_camera","mode":"320x240@125"})", &ok, &code, &msg);
        CHECK(!ok && code == "unavailable");
        CameraChange ch;
        ch.exposure = 10;
        CameraApplyResult res = cam.apply(ch);
        CHECK(!res.ok && res.code == "unavailable" && !res.message.empty());
    }
    {
        // Camera aan maar niet gestart (geen apparaat): ook een nette melding.
        Rig rig;
        CameraOptions co;
        CameraService cam(rig.sh, co);
        rig.sh.camera = &cam;
        rig.call(R"({"v":1,"cmd":"set_camera","gain":10})", &ok, &code, &msg);
        CHECK(!ok && code == "unavailable" && contains(msg, "draait niet"));
    }
    {
        // Geen camera-service in het geheel.
        Rig rig;
        rig.call(R"({"v":1,"cmd":"set_camera","gain":10})", &ok, &code, &msg);
        CHECK(!ok && code == "unavailable");
    }
    {
        // Grenzen worden door de gedeelde parser gecontroleerd, ook als er geen camera is.
        Rig rig;
        for (const char* bad : {R"({"v":1,"cmd":"set_camera","exposure":256})", R"({"v":1,"cmd":"set_camera","exposure":-1})",
                                R"({"v":1,"cmd":"set_camera","gain":64})", R"({"v":1,"cmd":"set_camera","gain":1.5})",
                                R"({"v":1,"cmd":"set_camera","mode":"640x480@30"})", R"({"v":1,"cmd":"set_camera","auto_gain":1})",
                                R"({"v":1,"cmd":"set_camera"})", R"({"v":1,"cmd":"set_camera","pad":"/dev/video0"})"}) {
            rig.call(bad, &ok, &code);
            CHECK(!ok && (code == "invalid_argument" || code == "bad_request"));
        }
    }
}


// Voorspelling en uitvoerklok: live toegepast, bewaard, en een ongeldige waarde verandert niets.
static void test_predict_and_clock(const std::string& dir) {
    Rig rig;
    config::SettingsStore store(dir + "/pc/config.json");
    store.load();
    rig.sh.settings = &store;
    OutputService out;  // niet gewapend: er wordt niets naar een apparaat geschreven
    rig.sh.output = &out;
    bool ok = false;
    std::string code;
    CHECK(store.get().aim.predict_ms == 40 && store.get().output.clock_hz == 250 && store.get().output.extrapolate_ms == 40);  // standaardwaarden

    json::Value r = rig.call(R"({"v":1,"cmd":"set_aim","predict_ms":55})", &ok);
    CHECK(ok && num(r, "predict_ms") == 55 && rig.hid.pc.horizon_ms == 55 && rig.hid.predict_calls == 1);
    CHECK(rig.hid.aim_calls == 0);  // de voorspelling verandert live, zonder het filter te resetten
    CHECK(store.get().aim.predict_ms == 55 && store.get().aim.h_edge_deg == 20);
    CHECK(contains(slurp(dir + "/pc/config.json"), "\"predict_ms\":55"));
    rig.call(R"({"v":1,"cmd":"set_aim","h_edge_deg":25})", &ok);
    CHECK(ok && rig.hid.aim_calls == 1 && rig.hid.pc.horizon_ms == 55 && store.get().aim.predict_ms == 55);  // randen raken de horizon niet
    rig.call(R"({"v":1,"cmd":"set_aim","predict_ms":0})", &ok);
    CHECK(ok && rig.hid.pc.horizon_ms == 0 && store.get().aim.predict_ms == 0);
    rig.call(R"({"v":1,"cmd":"set_aim","predict_ms":900})", &ok, &code);
    CHECK(!ok && code == "invalid_argument" && store.get().aim.predict_ms == 0);

    // Uitvoerklok.
    CHECK(out.clock_config().hz == 250 && out.clock_config().max_extrap_ms == 40);  // standaard van de klasse
    r = rig.call(R"({"v":1,"cmd":"set_clock","clock_hz":125,"extrapolate_ms":30})", &ok);
    CHECK(ok && num(r, "clock_hz") == 125 && num(r, "extrapolate_ms") == 30);
    CHECK(out.clock_config().hz == 125 && out.clock_config().max_extrap_ms == 30);
    CHECK(store.get().output.clock_hz == 125 && store.get().output.extrapolate_ms == 30);
    CHECK(contains(slurp(dir + "/pc/config.json"), "\"clock_hz\":125"));
    rig.call(R"({"v":1,"cmd":"set_clock","clock_hz":0})", &ok);
    CHECK(ok && !out.clock_config().enabled() && store.get().output.clock_hz == 0 && store.get().output.extrapolate_ms == 30);
    rig.call(R"({"v":1,"cmd":"set_clock","clock_hz":15})", &ok, &code);
    CHECK(!ok && code == "invalid_argument" && store.get().output.clock_hz == 0);
    // Status van de uitvoer toont de klok.
    json::Value st = rig.call(R"({"v":1,"cmd":"output"})", &ok);
    CHECK(ok);
    const json::Value* live = st.find("live");
    const json::Value* ck = live ? live->find("clock") : nullptr;
    CHECK(ck && ck->find("enabled") && !ck->find("enabled")->b && num(*ck, "extrapolate_ms") == 30);
    rig.call(R"({"v":1,"cmd":"set_clock","clock_hz":250})", &ok);
    st = rig.call(R"({"v":1,"cmd":"output"})", &ok);
    ck = st.find("live")->find("clock");
    CHECK(ck && ck->find("enabled")->b && num(*ck, "hz") == 250);

    // Zonder uitvoerservice: nette fout.
    rig.sh.output = nullptr;
    rig.call(R"({"v":1,"cmd":"set_clock","clock_hz":250})", &ok, &code);
    CHECK(!ok && code == "unavailable");
}

static void test_rumble(const std::string& dir) {
    Rig rig;
    config::SettingsStore store(dir + "/rb/config.json");
    store.load();
    rig.sh.settings = &store;
    bool ok = false;
    std::string code, msg;
    CHECK(store.get().rumble.enabled && store.get().rumble.strength == 180);
    json::Value r = rig.call(R"({"v":1,"cmd":"set_rumble","strength":220,"pulse_ms":80,"repeat_hz":6,"pump_strength":30})", &ok);
    CHECK(ok && num(r, "strength") == 220 && num(r, "pulse_ms") == 80 && num(r, "repeat_hz") == 6 && num(r, "pump_strength") == 30);
    CHECK(r.find("enabled")->b && r.find("saved")->b);
    CHECK(rig.hid.rumble_sets == 1 && rig.hid.rc.strength == 220 && rig.hid.rc.pulse_ms == 80 && rig.hid.rc.repeat_hz == 6 && rig.hid.rc.pump_strength == 30);
    CHECK(store.get().rumble.strength == 220 && store.get().rumble.pulse_ms == 80 && store.get().rumble.repeat_hz == 6 && store.get().rumble.pump_strength == 30);
    CHECK(contains(slurp(dir + "/rb/config.json"), "\"pulse_ms\":80"));
    // Een deel wijzigen laat de rest staan; aan/uit.
    rig.call(R"({"v":1,"cmd":"set_rumble","enabled":false})", &ok);
    CHECK(ok && !rig.hid.rc.enabled && rig.hid.rc.strength == 220 && !store.get().rumble.enabled && store.get().rumble.strength == 220);
    rig.call(R"({"v":1,"cmd":"set_rumble","repeat_hz":0})", &ok);
    CHECK(ok && rig.hid.rc.repeat_hz == 0 && store.get().rumble.repeat_hz == 0 && !store.get().rumble.enabled);
    // Ongeldig: niets verandert (ook niet in config.json).
    int sets = rig.hid.rumble_sets;
    rig.call(R"({"v":1,"cmd":"set_rumble","strength":300})", &ok, &code);
    CHECK(!ok && code == "invalid_argument" && rig.hid.rumble_sets == sets && store.get().rumble.strength == 220);
    // Testpuls: uitgeschakeld geeft een fout, aan werkt (standaard 300 ms), ook zonder Move een nette fout.
    rig.call(R"({"v":1,"cmd":"rumble_test"})", &ok, &code, &msg);
    CHECK(!ok && code == "unavailable" && contains(msg, "uit") && rig.hid.rumble_tests == 0);
    rig.call(R"({"v":1,"cmd":"set_rumble","enabled":true})", &ok);
    r = rig.call(R"({"v":1,"cmd":"rumble_test"})", &ok);
    CHECK(ok && rig.hid.rumble_tests == 1 && rig.hid.last_test_ms == 300 && num(r, "pulse_ms") == 300);
    rig.call(R"({"v":1,"cmd":"rumble_test","ms":120})", &ok);
    CHECK(ok && rig.hid.last_test_ms == 120);
    rig.hid.connected = false;
    rig.call(R"({"v":1,"cmd":"rumble_test"})", &ok, &code, &msg);
    CHECK(!ok && code == "unavailable" && contains(msg, "Move"));
    // Zonder instellingenopslag: wel toegepast, niet bewaard.
    rig.sh.settings = nullptr;
    r = rig.call(R"({"v":1,"cmd":"set_rumble","strength":100})", &ok);
    CHECK(ok && !r.find("saved")->b && rig.hid.rc.strength == 100);
}

static void test_persist_and_info(const std::string& dir) {
    Rig rig;
    config::SettingsStore store(dir + "/p/config.json");
    store.load();
    rig.sh.settings = &store;
    rig.sh.web_port = 18766;
    bool ok = false;

    // Filter, aim en web-poort worden bewaard.
    rig.call(R"({"v":1,"cmd":"set_filter","enabled":false,"beta":0.9})", &ok);
    CHECK(ok && !rig.hid.f.enabled);
    CHECK(!store.get().filter.enabled && store.get().filter.beta == 0.9 && store.get().filter.min_cutoff_hz == 0.3);  // standaard uit de latentiemeting
    rig.call(R"({"v":1,"cmd":"set_aim","h_edge_deg":30})", &ok);
    CHECK(ok && store.get().aim.h_edge_deg == 30 && store.get().aim.v_edge_deg == 11.25);
    json::Value r = rig.call(R"({"v":1,"cmd":"set_web","port":18999})", &ok);
    CHECK(ok && store.get().web.port == 18999 && num(r, "active_port") == 18766);
    CHECK(contains(slurp(dir + "/p/config.json"), "\"port\":18999"));
    // Fase 4: oriëntatieschatter omschakelen en bewaren.
    CHECK(store.get().fusion.mode == "mahony");  // standaard (opt-in)
    r = rig.call(R"({"v":1,"cmd":"set_fusion","mode":"mahony"})", &ok);
    CHECK(ok && rig.hid.fm == core::FusionMode::Mahony && store.get().fusion.mode == "mahony");
    CHECK(r.find("mode") && r.find("mode")->str == "mahony");
    CHECK(contains(slurp(dir + "/p/config.json"), "\"mode\":\"mahony\""));
    rig.call(R"({"v":1,"cmd":"set_fusion","mode":"mahony_mag"})", &ok);
    CHECK(ok && rig.hid.fm == core::FusionMode::MahonyMag && store.get().fusion.mode == "mahony_mag");
    rig.call(R"({"v":1,"cmd":"set_fusion","mode":"vqf_mag"})", &ok);
    CHECK(ok && rig.hid.fm == core::FusionMode::VqfMag && store.get().fusion.mode == "vqf_mag");
    rig.call(R"({"v":1,"cmd":"set_fusion","mode":"vqf"})", &ok);
    CHECK(ok && rig.hid.fm == core::FusionMode::Vqf && store.get().fusion.mode == "vqf");
    rig.call(R"({"v":1,"cmd":"set_fusion","mode":"mahony_mag"})", &ok);
    CHECK(ok && rig.hid.fm == core::FusionMode::MahonyMag);
    rig.call(R"({"v":1,"cmd":"set_fusion","mode":"eskf"})", &ok);
    CHECK(!ok && store.get().fusion.mode == "mahony_mag");
    // Ongeldige filterwaarde: niets veranderd of bewaard.
    rig.call(R"({"v":1,"cmd":"set_filter","beta":5000})", &ok);
    CHECK(!ok && store.get().filter.beta == 0.9);

    // info: versies, uptime, instellingen.
    r = rig.call(R"({"v":1,"cmd":"info"})", &ok);
    CHECK(ok && r.find("version")->find("daemon") && !r.find("version")->find("daemon")->str.empty());
    CHECK(r.find("version")->find("psmoveapi_pin")->str.size() >= 7);
    CHECK(r.find("version")->find("git_commit") && r.find("daemon")->find("uptime_s")->num >= 0);
    CHECK(r.find("settings")->find("available")->b && r.find("settings")->find("state")->str == "loaded");

    // settings: waarden, presets (6) en camera-modi (3).
    r = rig.call(R"({"v":1,"cmd":"settings"})", &ok);
    CHECK(ok && r.find("presets")->arr.size() == 6 && r.find("camera_modes")->arr.size() == 3);
    CHECK(r.find("values")->find("web")->find("port")->num == 18999);
    bool wit_null = false;
    for (const auto& p : r.find("presets")->arr)
        if (p.find("name")->str == "wit") wit_null = p.find("hue_deg")->is_null();
    CHECK(wit_null);

    // diagnostics: geredigeerd (controller-ID, thuismap), geen volledig ID.
    ::setenv("HOME", dir.c_str(), 1);
    r = rig.call(R"({"v":1,"cmd":"diagnostics"})", &ok);
    CHECK(ok && r.find("kind")->str == "ravengun-diagnostics" && r.find("status") && r.find("info") && r.find("settings"));
    std::string text = execute_line(rig.sh, R"({"v":1,"cmd":"diagnostics"})");
    CHECK(!contains(text, "00:11:22:33:44:55") && contains(text, "00:11:22:xx:xx:xx"));
    CHECK(!contains(text, dir) && contains(text, "~/p/config.json"));
    CHECK(text.find('\n') == std::string::npos);  // IPC: één regel

    // Zonder opslag: settings en set_web melden dat netjes.
    Rig bare;
    r = bare.call(R"({"v":1,"cmd":"settings"})", &ok);
    CHECK(ok && !r.find("info")->find("available")->b && r.find("values")->is_null());
    std::string code;
    bare.call(R"({"v":1,"cmd":"set_web","port":1234})", &ok, &code);
    CHECK(!ok && code == "unavailable");
}

static void test_buttons_and_ui(const std::string& dir) {
    Rig rig;
    config::SettingsStore store(dir + "/btn/config.json");
    store.load();
    rig.sh.settings = &store;
    bool ok = false;
    std::string code, msg;
    json::Value r = rig.call(R"({"v":1,"cmd":"set_buttons","trekker":"t","pomp":"move","driehoek":"triangle"})", &ok);
    CHECK(ok && r.find("saved")->b && r.find("buttons")->find("pomp")->str == "move");
    CHECK(store.get().sharp_shooter.buttons.size() == 3 && store.get().sharp_shooter.buttons.at("trekker") == "t");
    CHECK(contains(slurp(dir + "/btn/config.json"), "\"pomp\":\"move\"") || contains(slurp(dir + "/btn/config.json"), "\"pomp\": \"move\""));
    // Een lege waarde wist de koppeling.
    rig.call(R"({"v":1,"cmd":"set_buttons","pomp":""})", &ok);
    CHECK(ok && store.get().sharp_shooter.buttons.count("pomp") == 0 && store.get().sharp_shooter.buttons.size() == 2);
    // Ongeldig: onbekende rol of Move-knop gaat niet eens door de parser.
    rig.call(R"({"v":1,"cmd":"set_buttons","laser":"t"})", &ok, &code);
    CHECK(!ok && code == "bad_request");
    rig.call(R"({"v":1,"cmd":"set_buttons","trekker":"l2"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    CHECK(store.get().sharp_shooter.buttons.size() == 2);
    // Zonder instellingenopslag: nette fout.
    Rig bare;
    bare.call(R"({"v":1,"cmd":"set_buttons","trekker":"t"})", &ok, &code);
    CHECK(!ok && code == "unavailable");

    // Kiosk: status werkt altijd; openen vereist een webpoort (hier 0), dus er start nooit een browser in de test.
    r = rig.call(R"({"v":1,"cmd":"calibrate_ui","action":"status"})", &ok);
    CHECK(ok && r.find("running") && r.find("message"));
    rig.call(R"({"v":1,"cmd":"calibrate_ui","action":"open"})", &ok, &code, &msg);
    CHECK(!ok && code == "unavailable" && contains(msg, "webinterface"));
}

static std::string layout_of(const output::LiveButtonMap& m, const std::string& button) {
    for (const auto& kv : m.layout) if (kv.first == button) return kv.second;
    return "?";
}

// Knoppenindeling live wijzigen: validatie, live toepassen, bewaren, en niets half toepassen bij een fout.
static void test_set_gun_buttons(const std::string& dir) {
    Rig rig;
    config::SettingsStore store(dir + "/gun/config.json");
    store.load();
    rig.sh.settings = &store;
    OutputService out;
    rig.sh.output = &out;
    bool ok = false;
    std::string code, msg;
    json::Value r = rig.call(R"({"v":1,"cmd":"set_gun_buttons","map":{"cross":"grenade","circle":"key:f2","move":"recenter"}})", &ok);
    CHECK(ok && r.find("saved")->b);
    CHECK(rig.hid.gun_map_sets == 1);
    CHECK(layout_of(rig.hid.gun_map, "cross") == "grenade" && layout_of(rig.hid.gun_map, "circle") == "key:f2");
    CHECK(layout_of(rig.hid.gun_map, "move") == "recenter" && layout_of(rig.hid.gun_map, "ps") == "recenter (vast)");
    CHECK(layout_of(rig.hid.gun_map, "square") == "key:1");  // niet genoemd: blijft zoals het was
    CHECK(r.find("map")->find("cross")->str == "grenade");
    CHECK(store.get().gun_buttons.map.at("circle") == "key:f2" && store.get().gun_buttons.map.at("move") == "recenter");
    CHECK(contains(slurp(dir + "/gun/config.json"), "key:f2"));
    // De uitvoerstatus toont dezelfde indeling.
    CHECK(contains(out.status_json(), "\"circle\":\"key:f2\""));
    // Een tweede wijziging legt zich over de eerste heen.
    rig.call(R"({"v":1,"cmd":"set_gun_buttons","map":{"cross":"none"}})", &ok);
    CHECK(ok && store.get().gun_buttons.map.at("cross") == "none" && store.get().gun_buttons.map.at("circle") == "key:f2");
    // Ongeldig: geen enkele wijziging bereikt de HID-thread of het bestand.
    const int sets = rig.hid.gun_map_sets;
    const std::string before = slurp(dir + "/gun/config.json");
    rig.call(R"({"v":1,"cmd":"set_gun_buttons","map":{"cross":"reload","ps":"reload"}})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"set_gun_buttons","map":{"cross":"reload","circle":"key:f99"}})", &ok, &code, &msg);
    CHECK(!ok && code == "invalid_argument" && contains(msg, "toets"));
    rig.call(R"({"v":1,"cmd":"set_gun_buttons","map":{"cross":"`id`"}})", &ok, &code);
    CHECK(!ok);
    CHECK(rig.hid.gun_map_sets == sets && slurp(dir + "/gun/config.json") == before);
    // Zonder instellingenopslag werkt het live, maar is niets bewaard.
    Rig bare;
    r = bare.call(R"({"v":1,"cmd":"set_gun_buttons","map":{"cross":"aux1"}})", &ok);
    CHECK(ok && !r.find("saved")->b && layout_of(bare.hid.gun_map, "cross") == "aux1");
}

static void test_fusion_and_learning(const std::string& dir) {
    Rig rig;
    config::SettingsStore store(dir + "/fus/config.json");
    store.load();
    rig.sh.settings = &store;
    bool ok = false;
    std::string code;
    json::Value r = rig.call(R"({"v":1,"cmd":"set_fusion","mode":"vqf_mag"})", &ok);
    CHECK(ok && r.find("mode")->str == "vqf_mag" && r.find("recenter_learning")->b && r.find("saved")->b);
    CHECK(store.get().fusion.mode == "vqf_mag" && rig.hid.fm == core::FusionMode::VqfMag);
    r = rig.call(R"({"v":1,"cmd":"set_fusion","recenter_learning":false})", &ok);
    CHECK(ok && !r.find("recenter_learning")->b && r.find("mode")->str == "vqf_mag");  // modus blijft zoals hij was
    CHECK(!rig.hid.learning && !store.get().fusion.recenter_learning && store.get().fusion.mode == "vqf_mag");
    rig.call(R"({"v":1,"cmd":"set_fusion"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
}

// Games: de lijst, het starten (alleen bestaande profielen, vaste opdrachtregel) en het stoppen, met een nep-spawner.
static void test_games_commands(const std::string& dir) {
    fs::create_directories(dir + "/tp/UserProfiles");
    fs::create_directories(dir + "/tp/Icons");
    { std::ofstream f(dir + "/tp/UserProfiles/Halo.xml"); f << "<GameProfile><GameNameInternal>HALO Test</GameNameInternal><IconName>Icons/Halo.png</IconName></GameProfile>"; }
    { std::ofstream f(dir + "/tp/UserProfiles/TombRaider.xml"); f << "<GameProfile></GameProfile>"; }
    { std::ofstream f(dir + "/tp/Icons/Halo.png", std::ios::binary); f << "png"; }
    fs::create_directories(dir + "/proc");

    games::Config gc;
    gc.tp_dir = dir + "/tp";
    gc.log_path = dir + "/logs/play-last.log";
    gc.proc_root = dir + "/proc";  // lege nep-procesmap: er draait niets
    fs::create_directories(dir + "/patches/TombRaider");
    { std::ofstream f(dir + "/patches/TombRaider/game.ini"); f << "[Status]\nNote = Nog niet getest.\n"; }
    gc.dirs.shipped = dir + "/patches";
    std::vector<std::vector<std::string>> spawned;
    std::mutex sm;
    games::GameService svc(gc, [&](const std::vector<std::string>& argv, std::string&) {
        std::lock_guard<std::mutex> g(sm);
        spawned.push_back(argv);
        return 0;
    });
    Rig rig;
    rig.sh.games = &svc;
    bool ok = false;
    std::string code, msg;
    json::Value r = rig.call(R"({"v":1,"cmd":"games"})", &ok);
    CHECK(ok && r.find("available")->b && r.find("games")->arr.size() == 2);
    CHECK(r.find("games")->arr[0].find("profile")->str == "Halo" && r.find("games")->arr[0].find("name")->str == "HALO Test");
    CHECK(r.find("games")->arr[0].find("icon")->str == "/api/game-icon/Halo.png");
    CHECK(r.find("games")->arr[1].find("note")->str == "Nog niet getest.");
    CHECK(r.find("status")->find("state")->str == "idle");

    // De poort vóór de start (game_ready): zonder ready_inputs een lightgun voor elke speler en klaar (er is geen lightgun_ready).
    r = rig.call(R"({"v":1,"cmd":"game_ready","profile":"Halo"})", &ok);
    CHECK(ok && r.find("lightgun")->b && r.find("ready")->b && r.find("players")->arr.size() == 4);
    rig.call(R"({"v":1,"cmd":"game_ready","profile":"Bestaatniet"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"game_ready"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"game_ready","profile":"Halo","launch":"direct"})", &ok, &code);
    CHECK(!ok && code == "bad_request");

    // Onbekende of ongeldige profielen worden geweigerd zonder dat er iets gespawnd wordt.
    rig.call(R"({"v":1,"cmd":"game_start","profile":"Bestaatniet"})", &ok, &code, &msg);
    CHECK(!ok && code == "invalid_argument" && contains(msg, "onbekend profiel"));
    rig.call(R"({"v":1,"cmd":"game_start","profile":"../UserProfiles/Halo"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"game_start","profile":"Halo;touch x"})", &ok, &code);
    CHECK(!ok);
    // Startwijze (A10): alleen direct of auto.
    rig.call(R"({"v":1,"cmd":"game_start","profile":"Halo","launch":"--direct"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"game_start","profile":"Halo","launch":3})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    { std::lock_guard<std::mutex> g(sm); CHECK(spawned.empty()); }

    // Een bestaand profiel zonder eigen route (geen native, Unity of direct in deze opstelling): de start wordt aangenomen en eindigt
    // in 'failed' met de reden; er wordt niets gestart (geen startscript, geen TeknoParrotUi).
    r = rig.call(R"({"v":1,"cmd":"game_start","profile":"Halo"})", &ok);
    CHECK(ok);
    for (int i = 0; i < 400; ++i) {
        r = rig.call(R"({"v":1,"cmd":"games"})", &ok);
        if (r.find("status")->find("state")->str == "failed") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(r.find("status")->find("state")->str == "failed");
    CHECK(r.find("status")->find("message")->is_string() && contains(r.find("status")->find("message")->str, "nog geen eigen route"));
    rig.call(R"({"v":1,"cmd":"game_start","profile":"Halo","launch":"teknoparrot-ui"})", &ok, &code, &msg);
    CHECK(!ok && code == "invalid_argument" && contains(msg, "TeknoParrotUi"));
    { std::lock_guard<std::mutex> g(sm); CHECK(spawned.empty()); }

    // Zonder instellingen/games-service: nette fout.
    Rig bare;
    bare.call(R"({"v":1,"cmd":"games"})", &ok, &code);
    CHECK(!ok && code == "unavailable");
    bare.call(R"({"v":1,"cmd":"game_start","profile":"Halo"})", &ok, &code);
    CHECK(!ok && code == "unavailable");
    bare.call(R"({"v":1,"cmd":"game_ready","profile":"Halo"})", &ok, &code);
    CHECK(!ok && code == "unavailable");
}

// Unity-route: IPC-opdrachten aagames en output_key, en de status.
static void test_aagames_commands(const std::string& dir) {
    // Zonder service of uitvoer: nette fouten.
    {
        Rig bare;
        bool ok = true;
        std::string code;
        bare.call(R"({"v":1,"cmd":"aagames","action":"status"})", &ok, &code);
        CHECK(!ok && code == "unavailable");
        bare.call(R"({"v":1,"cmd":"output_key","key":"5"})", &ok, &code);
        CHECK(!ok && code == "unavailable");
    }
    Rig rig;
    OutputService out;   // niet gewapend: er wordt geen apparaat gemaakt
    // De game komt uit patches/<Game>/unity.json, de poort uit unity-route.json.
    const std::string pd = dir + "/aagames-patches";
    fs::create_directories(pd + "/TombRaider");
    std::ofstream(pd + "/TombRaider/unity.json") << R"({"name":"Tomb Raider Arcade","type":"tomb_raider","folder":"Tomb Raider"})";
    std::ofstream(dir + "/aagames-route.json") << R"({"port":9})";   // niets luistert hier: de klant blijft proberen
    ravengun::launch::UnityPaths up;
    up.patches = pd;
    up.route_json = dir + "/aagames-route.json";
    AagamesService svc(rig.sh, up);
    rig.sh.output = &out;
    rig.sh.aagames = &svc;
    bool ok = false;
    std::string code, msg;
    json::Value r = rig.call(R"({"v":1,"cmd":"aagames"})", &ok);       // standaard: status
    CHECK(ok && !r.find("active")->b && r.find("games")->arr.size() == 1 && r.find("games")->arr[0].str == "TombRaider");
    rig.call(R"({"v":1,"cmd":"aagames","action":"start","game":"Bestaat niet"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"aagames","action":"start"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"aagames","action":"bogus"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"aagames","extra":1})", &ok, &code);
    CHECK(!ok && code == "bad_request");
    CHECK(rig.sh.game_rumble_mask.load() == 0);
    r = rig.call(R"({"v":1,"cmd":"aagames","action":"start","game":"TombRaider"})", &ok);
    CHECK(ok && r.find("active")->b && r.find("game")->str == "TombRaider" && !r.find("connected")->b);
    CHECK(rig.sh.game_rumble_mask.load() == 0xF);
    r = rig.call(R"({"v":1,"cmd":"status"})", &ok);                     // de status noemt de route
    CHECK(ok && r.find("aagames") && r.find("aagames")->find("active")->b);
    r = rig.call(R"({"v":1,"cmd":"aagames","action":"stop"})", &ok);
    CHECK(ok && !r.find("active")->b && rig.sh.game_rumble_mask.load() == 0);
    // output_key: onbekende toets, ongeldige actie, en niet gewapend.
    rig.call(R"({"v":1,"cmd":"output_key","key":"bestaatniet"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"output_key","key":"5","action":"bogus"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"output_key","key":"5"})", &ok, &code, &msg);
    CHECK(!ok && code == "unavailable" && contains(msg, "arm"));
    rig.call(R"({"v":1,"cmd":"output_key","key":"5","player":5})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.sh.aagames = nullptr;
    rig.sh.output = nullptr;
}

// Meerdere spelers: per-controller-instellingen via de spelerkeuze, de standaard, spelernummers ruilen en de controllerlijst,
// het koppelen en verwijderen met een nep-systeem (geen Bluetooth of hidraw).
struct SelectHid final : HidContext {
    int cur = 1;
    std::map<int, core::OneEuroConfig> f;
    std::vector<int> selected;
    int reconfigs = 0;
    bool select_player(int p) override { cur = p; selected.push_back(p); return p >= 1 && p <= 4; }
    void reconfigure() override { ++reconfigs; }
    std::string blink(int ms) override { return cur == 2 ? "1" : "!unavailable|niet verbonden (" + std::to_string(ms) + ")"; }
    std::string status_json(bool) override { return "{\"player\":" + std::to_string(cur) + "}"; }
    bool recenter() override { return true; }
    core::OneEuroConfig filter() const override { auto it = f.find(cur); return it == f.end() ? core::OneEuroConfig{} : it->second; }
    void set_filter(const core::OneEuroConfig& c) override { f[cur] = c; }
    core::AimConfig aim() const override { return {}; }
    void set_aim(const core::AimConfig&) override {}
    void set_led(bool, uint8_t, uint8_t, uint8_t) override {}
    std::string record(const ipc::Request&) override { return "{}"; }
    std::string calib_mag(const ipc::Request&) override { return "{}"; }
    core::FusionMode fusion() const override { return core::FusionMode::Mahony; }
    void set_fusion(core::FusionMode) override {}
};

// Muis als lightgun: IPC-opdrachten mice en set_mouse_gun (nepmuizen; de service draait niet, er wordt niets geopend).
static void test_mouse_commands() {
    bool ok = true;
    std::string code, msg;
    {
        Rig bare;   // zonder muisservice
        bare.call(R"({"v":1,"cmd":"mice"})", &ok, &code);
        CHECK(!ok && code == "unavailable");
        bare.call(R"({"v":1,"cmd":"set_mouse_gun","player":1,"mouse":""})", &ok, &code);
        CHECK(!ok && code == "unavailable");
    }
    Rig rig;
    OutputService out;   // niet gewapend: er wordt geen apparaat gemaakt
    MouseServiceConfig cfg;
    cfg.dev_dir = "/nonexistent-ravengun-test";
    cfg.lister = [] {
        sysinfo::Mouse a, b;
        a.id = "usb-Test_Mouse-event-mouse"; a.name = "Test Mouse"; a.event = "event5";
        b.id = "event6"; b.name = "Test Tablet"; b.event = "event6"; b.absolute = true;
        return std::vector<sysinfo::Mouse>{a, b};
    };
    MouseService svc(out, cfg);   // niet gestart
    rig.sh.output = &out;
    rig.sh.mouse = &svc;

    json::Value r = rig.call(R"({"v":1,"cmd":"mice"})", &ok);
    CHECK(ok && r.find("mice")->arr.size() == 2 && r.find("assignments")->arr.size() == 4);
    if (ok && r.find("mice")->arr.size() == 2) {
        const auto& m0 = r.find("mice")->arr[0];
        CHECK(m0.find("id")->str == "usb-Test_Mouse-event-mouse" && m0.find("name")->str == "Test Mouse" && !m0.find("absolute")->b && m0.find("event")->str == "event5");
        CHECK(r.find("mice")->arr[1].find("absolute")->b);
        const auto& a1 = r.find("assignments")->arr[0];
        CHECK(num(a1, "player") == 1 && a1.find("mouse")->is_null() && !a1.find("connected")->b && num(a1, "counts_per_screen") == 1920);
    }
    rig.call(R"({"v":1,"cmd":"mice","x":1})", &ok, &code);
    CHECK(!ok && code == "bad_request");

    // Onbekend id: invalid_argument; een pad wordt nooit aangenomen.
    rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":2,"mouse":"bestaat-niet"})", &ok, &code, &msg);
    CHECK(!ok && code == "invalid_argument" && contains(msg, "onbekende muis"));
    rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":2,"mouse":"/dev/input/event5"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":2,"mouse":"event5"})", &ok, &code);   // eventN dat de daemon niet kent
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":9,"mouse":""})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"set_mouse_gun","mouse":""})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":2,"mouse":"","pad":"x"})", &ok, &code);
    CHECK(!ok && code == "bad_request");
    CHECK(out.mouse_players() == 0);

    // Een bekend id: het antwoord is de nieuwe toewijzing; het apparaat bestaat niet in dev_dir, de toewijzing blijft staan.
    r = rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":3,"mouse":"usb-Test_Mouse-event-mouse","counts_per_screen":2400})", &ok);
    CHECK(ok && num(r, "player") == 3 && r.find("mouse")->str == "usb-Test_Mouse-event-mouse" && num(r, "counts_per_screen") == 2400 && !r.find("connected")->b);
    CHECK(out.mouse_players() == 0b0100);
    // Dezelfde muis bij een andere speler: geweigerd; een andere muis mag.
    rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":2,"mouse":"usb-Test_Mouse-event-mouse"})", &ok, &code, &msg);
    CHECK(!ok && code == "invalid_argument" && contains(msg, "speler 3"));
    r = rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":2,"mouse":"event6"})", &ok);
    CHECK(ok && out.mouse_players() == 0b0110);
    // status: blok mouse_guns.
    r = rig.call(R"({"v":1,"cmd":"status"})", &ok);
    CHECK(ok && r.find("mouse_guns") && r.find("mouse_guns")->arr.size() == 4 && r.find("mouse_guns")->arr[2].find("mouse")->str == "usb-Test_Mouse-event-mouse");
    // Loskoppelen.
    r = rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":3,"mouse":""})", &ok);
    CHECK(ok && r.find("mouse")->is_null() && num(r, "counts_per_screen") == 2400);
    rig.call(R"({"v":1,"cmd":"set_mouse_gun","player":2,"mouse":""})", &ok);
    CHECK(ok && out.mouse_players() == 0);
    rig.sh.mouse = nullptr;
}

static void test_controllers(const std::string& dir) {
    const std::string A = "00:07:04:8c:70:ac", B = "00:07:04:11:22:33", N = "00:06:f7:0e:c1:93";
    SharedState sh;
    SelectHid hid;
    std::atomic<bool> stop{false};
    std::thread t([&] { while (!stop.load()) { sh.run_jobs(hid); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } });
    auto call = [&](const std::string& line, bool* ok = nullptr, std::string* code = nullptr, std::string* msg = nullptr) {
        auto pr = json::parse(execute_line(sh, line));
        const json::Value* okv = pr.value.find("ok");
        bool good = pr.ok && okv && okv->b;
        if (ok) *ok = good;
        if (!good && pr.ok)
            if (const json::Value* e = pr.value.find("error")) {
                if (code) *code = e->find("code")->str;
                if (msg) *msg = e->find("message")->str;
            }
        return good ? *pr.value.find("result") : pr.value;
    };
    config::SettingsStore store(dir + "/ctl/config.json");
    store.load();
    sh.settings = &store;
    store.update([&](config::Settings& s) {
        config::register_controller(s, A, "move", "ZCM1", 1);
        config::register_controller(s, B, "move", "ZCM1", 2);
        config::register_controller(s, N, "navigation", "", 3);
    });
    bool ok = false;
    std::string code, msg;

    // Speler 2: live bij speler 2, bewaard als afwijking van B.
    json::Value r = call(R"({"v":1,"cmd":"set_filter","beta":4,"player":2})", &ok);
    CHECK(ok && num(r, "beta") == 4 && hid.f[2].beta == 4 && hid.selected.back() == 2);
    CHECK(config::effective(store.get(), B).filter.beta == 4 && store.get().filter.beta == 10 && config::effective(store.get(), A).filter.beta == 10);
    // Zonder keuze: speler 1 (A).
    call(R"({"v":1,"cmd":"set_filter","beta":7})", &ok);
    CHECK(ok && hid.f[1].beta == 7 && config::effective(store.get(), A).filter.beta == 7 && store.get().filter.beta == 10);
    // Op adres of sleutel.
    call(std::string(R"({"v":1,"cmd":"set_aim","predict_ms":20,"controller":")") + core::controller_key(B) + "\"}", &ok);
    CHECK(ok && config::effective(store.get(), B).aim.predict_ms == 20 && config::effective(store.get(), A).aim.predict_ms == 40);
    call(R"({"v":1,"cmd":"set_rumble","strength":99,"controller":"00:07:04:11:22:33"})", &ok);
    CHECK(ok && config::effective(store.get(), B).rumble.strength == 99 && store.get().rumble.strength == 180);
    // De standaard: geldt live voor elke speler zonder eigen waarde (niet A en B, wel de lege spelers 3 en 4).
    hid.selected.clear();
    call(R"({"v":1,"cmd":"set_filter","beta":12,"controller":"default"})", &ok);
    CHECK(ok && store.get().filter.beta == 12 && config::effective(store.get(), B).filter.beta == 4);
    CHECK(hid.selected == std::vector<int>({3, 4}));
    // Knoppen voor speler 2.
    call(R"({"v":1,"cmd":"set_gun_buttons","player":2,"map":{"cross":"key:2"}})", &ok);
    CHECK(ok && config::effective(store.get(), B).gun_buttons.map.at("cross") == "key:2" && config::effective(store.get(), A).gun_buttons.map.at("cross") == "reload");

    // Ongeldige keuzes: geweigerd (parser of daemon), niets bewaard.
    const std::string before = slurp(dir + "/ctl/config.json");
    for (const char* bad : {R"({"v":1,"cmd":"set_filter","beta":1,"player":5})", R"({"v":1,"cmd":"set_filter","beta":1,"player":1.5})",
                            R"({"v":1,"cmd":"set_filter","beta":1,"controller":"bla"})", R"({"v":1,"cmd":"set_filter","beta":1,"controller":"00:07:04:8c:70:ac;id"})",
                            R"({"v":1,"cmd":"set_filter","beta":1,"player":1,"controller":"default"})", R"({"v":1,"cmd":"set_filter","beta":1,"controller":"00:00:00:00:00:00"})",
                            R"({"v":1,"cmd":"set_web","port":9000,"player":2})", R"({"v":1,"cmd":"unpair","controller":"default"})",
                            R"({"v":1,"cmd":"unpair","player":1})", R"({"v":1,"cmd":"set_controller","controller":"../etc","name":"x"})",
                            R"({"v":1,"cmd":"set_controller","controller":"00:07:04:8c:70:ac","name":"a\nb"})",
                            R"({"v":1,"cmd":"set_controller","controller":"00:07:04:8c:70:ac","new_player":5})"}) {
        call(bad, &ok, &code);
        CHECK(!ok && (code == "invalid_argument" || code == "bad_request"));
    }
    call(R"({"v":1,"cmd":"set_filter","beta":1,"controller":"00:07:04:99:99:99"})", &ok, &code, &msg);
    CHECK(!ok && code == "invalid_argument" && contains(msg, "onbekende"));
    call(R"({"v":1,"cmd":"set_filter","beta":1,"controller":"00:06:f7:0e:c1:93"})", &ok, &code, &msg);  // Navigation: geen pijplijn
    CHECK(!ok && contains(msg, "Navigation"));
    CHECK(slurp(dir + "/ctl/config.json") == before);

    // Naam en spelernummer: ruilen met de Move die dat nummer had; de spelerkleur gaat mee.
    hid.reconfigs = 0;
    r = call(R"({"v":1,"cmd":"set_controller","controller":"00:07:04:8c:70:ac","name":"Rode Move","new_player":2})", &ok);
    CHECK(ok && r.find("player")->num == 2 && r.find("swapped_with")->str == "Move 2" && r.find("name")->str == "Rode Move");
    CHECK(store.get().controllers.at(A).player == 2 && store.get().controllers.at(B).player == 1 && hid.reconfigs == 1);
    CHECK(config::effective(store.get(), A).led.preset == "magenta" && config::effective(store.get(), B).led.preset == "cyaan");
    CHECK(config::effective(store.get(), A).gun_buttons.map.at("start") == "key:2" && config::effective(store.get(), B).gun_buttons.map.at("start") == "key:1");
    CHECK(config::effective(store.get(), B).filter.beta == 4);  // de eigen instellingen blijven bij de controller
    // Afwijkingen wissen.
    call(R"({"v":1,"cmd":"set_controller","controller":"00:07:04:11:22:33","reset_settings":true})", &ok);
    CHECK(ok && config::effective(store.get(), B).filter.beta == 12 && store.get().controllers.at(B).keys.empty());

    // Blink en status per speler.
    call(R"({"v":1,"cmd":"blink","player":2})", &ok);
    CHECK(ok);
    call(R"({"v":1,"cmd":"blink","player":3})", &ok, &code);
    CHECK(!ok && code == "unavailable");
    r = call(R"({"v":1,"cmd":"status","player":2})", &ok);
    CHECK(ok && num(r, "player") == 2 && r.find("assignment")->find("controller")->find("name")->str == "Rode Move");
    CHECK(!contains(execute_line(sh, R"({"v":1,"cmd":"status","player":2})"), A));  // adres afgeschermd

    // Controllerlijst, koppelen en verwijderen met een nep-systeem.
    CHECK(call(R"({"v":1,"cmd":"controllers"})", &ok, &code).is_object() && !ok && code == "unavailable");
    std::string ferr;
    auto ops = make_fake_controller_ops(R"({"adapter":"1c:ce:51:98:5d:6b","helper":true,
        "bluez":[{"address":"00:07:04:8c:70:ac","type":"move","model":"ZCM1","paired":true,"connected":true}],
        "hidraw":[{"node":"/dev/hidraw10","bus":5,"type":"move","uniq":"00:07:04:8c:70:ac"},
                  {"node":"/dev/hidraw21","bus":3,"type":"move","own":"00:07:04:55:66:77","master":"aa:bb:cc:dd:ee:01"}]})", ferr);
    CHECK(ops != nullptr);
    ControllerService svc(sh, std::move(ops));
    sh.controllers = &svc;
    r = call(R"({"v":1,"cmd":"controllers"})", &ok);
    CHECK(ok && r.find("controllers")->arr.size() == 3 && r.find("adapter")->find("found")->b);
    const std::string list = execute_line(sh, R"({"v":1,"cmd":"controllers"})");
    CHECK(!contains(list, A) && !contains(list, B) && contains(list, core::controller_key(A)));
    CHECK(contains(execute_line(sh, R"({"v":1,"cmd":"controllers","full_ids":true})"), A));
    // Wizard: start, de USB-Move wordt gezien, master gezet, geregistreerd (nep), kabel eruit, verbonden.
    r = call(R"({"v":1,"cmd":"pair","action":"start"})", &ok);
    CHECK(ok && r.find("state")->str == "wait_usb");
    call(R"({"v":1,"cmd":"pair","action":"start"})", &ok, &code);
    CHECK(!ok && code == "unavailable");  // er loopt al een koppeling
    for (int i = 0; i < 6; ++i) svc.tick_for_test();
    r = call(R"({"v":1,"cmd":"pair","action":"status"})", &ok);
    CHECK(ok && r.find("state")->str == "wait_unplug" && r.find("player")->num == 3);
    CHECK(store.get().controllers.count("00:07:04:55:66:77") && store.get().controllers.at("00:07:04:55:66:77").player == 3);
    CHECK(config::effective(store.get(), "00:07:04:55:66:77").led.preset == "groen");
    r = call(R"({"v":1,"cmd":"pair","action":"cancel"})", &ok);
    CHECK(ok && r.find("state")->str == "cancelled");
    // Verwijderen: met bewaarde instellingen (speler vrij, instellingen blijven), daarna helemaal.
    r = call(R"({"v":1,"cmd":"unpair","controller":"00:07:04:8c:70:ac","keep_settings":true})", &ok);
    CHECK(ok && r.find("settings_kept")->b && store.get().controllers.at(A).removed && store.get().controllers.at(A).player == 0);
    CHECK(config::effective(store.get(), A).led.preset == "magenta");  // bewaard voor als hij terugkomt
    r = call(std::string(R"({"v":1,"cmd":"unpair","keep_settings":false,"controller":")") + core::controller_key(B) + "\"}", &ok);
    CHECK(ok && !store.get().controllers.count(B));
    call(R"({"v":1,"cmd":"unpair","controller":"00:07:04:99:99:99"})", &ok, &code);
    CHECK(!ok && code == "invalid_argument");
    // Een teruggekomen Move krijgt een vrije speler en zijn oude instellingen terug.
    store.update([&](config::Settings& s) { config::register_controller(s, A, "move", "ZCM1", 9); });
    CHECK(!store.get().controllers.at(A).removed && store.get().controllers.at(A).player >= 1);
    sh.controllers = nullptr;
    stop.store(true);
    t.join();
}

int main() {
    std::string dir = (fs::temp_directory_path() / ("rg-cmd-test-" + std::to_string(::getpid()))).string();
    fs::remove_all(dir);
    fs::create_directories(dir);
    test_set_led(dir);
    test_set_camera(dir);
    test_persist_and_info(dir);
    test_predict_and_clock(dir);
    test_rumble(dir);
    test_buttons_and_ui(dir);
    test_set_gun_buttons(dir);
    test_fusion_and_learning(dir);
    test_games_commands(dir);
    test_aagames_commands(dir);
    test_mouse_commands();
    test_controllers(dir);
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
