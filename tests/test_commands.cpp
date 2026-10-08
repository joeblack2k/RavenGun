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
#include <thread>
#include <unistd.h>

#include "camera.hpp"
#include "commands.hpp"
#include "hid_thread.hpp"
#include "output_service.hpp"
#include "ravengun/config/led_color.hpp"
#include "ravengun/config/settings.hpp"
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
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
