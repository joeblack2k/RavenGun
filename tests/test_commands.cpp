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
    std::string status_json(bool full) override {
        return std::string("{\"daemon\":{\"uptime_s\":1},\"controller\":{\"id\":\"") + (full ? "00:11:22:33:44:55" : "00:11:22:xx:xx:xx") + "\"}}";
    }
    bool recenter() override { return false; }
    core::OneEuroConfig filter() const override { return f; }
    void set_filter(const core::OneEuroConfig& c) override { f = c; }
    core::AimConfig aim() const override { return a; }
    void set_aim(const core::AimConfig& c) override { a = c; }
    void set_led(bool en, uint8_t rr, uint8_t gg, uint8_t bb) override { ++led_calls; led_enabled = en; r = rr; g = gg; b = bb; }
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
    CHECK(!store.get().filter.enabled && store.get().filter.beta == 0.9 && store.get().filter.min_cutoff_hz == 1.0);
    rig.call(R"({"v":1,"cmd":"set_aim","h_edge_deg":30})", &ok);
    CHECK(ok && store.get().aim.h_edge_deg == 30 && store.get().aim.v_edge_deg == 11.25);
    json::Value r = rig.call(R"({"v":1,"cmd":"set_web","port":18999})", &ok);
    CHECK(ok && store.get().web.port == 18999 && num(r, "active_port") == 18766);
    CHECK(contains(slurp(dir + "/p/config.json"), "\"port\":18999"));
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

int main() {
    std::string dir = (fs::temp_directory_path() / ("rg-cmd-test-" + std::to_string(::getpid()))).string();
    fs::remove_all(dir);
    fs::create_directories(dir);
    test_set_led(dir);
    test_set_camera(dir);
    test_persist_and_info(dir);
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
