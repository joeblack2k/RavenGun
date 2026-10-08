// Tests van de instellingenlaag: schema, validatie, migratiehaak, atomisch schrijven, herstel na een
// corrupt bestand, de LED-presets met hun tracker-tint en de nieuwe IPC-commando's (parser).
// Eigen CHECK-macro (actief bij NDEBUG), zoals de andere tests.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "ravengun/config/led_color.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

using namespace ravengun;
using namespace ravengun::config;
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
static void spit(const std::string& p, const std::string& t) {
    fs::create_directories(fs::path(p).parent_path());
    std::ofstream f(p, std::ios::binary);
    f << t;
}
static int mode_of(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 ? static_cast<int>(st.st_mode & 0777) : -1;
}
static std::string tmpdir() {
    std::string t = (fs::temp_directory_path() / ("rg-config-test-" + std::to_string(::getpid()))).string();
    fs::remove_all(t);
    fs::create_directories(t);
    return t;
}
static int tmp_files(const std::string& dir) {
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().filename().string().find(".tmp.") != std::string::npos) ++n;
    return n;
}

static Settings modified() {
    Settings s;
    s.led = {true, "magenta", 255, 0, 255};
    s.tracker = {86, 30, 28};
    s.camera.width = 320; s.camera.height = 240; s.camera.fps = 125;
    s.camera.auto_exposure = true; s.camera.exposure = 77; s.camera.gain = 12;
    s.filter = {false, 2.5, 0.25, 3.0};
    s.aim = {25, 14};
    s.web.port = 18999;
    s.sharp_shooter.buttons = {{"trekker", "t"}, {"pomp", "move"}, {"cirkel", "circle"}};
    return s;
}

static bool same(const Settings& a, const Settings& b) { return to_json(a) == to_json(b); }

static void test_roundtrip_and_validation() {
    Settings d;
    CHECK(validate(d).empty());
    Settings out;
    std::string err;
    CHECK(parse_settings(to_json(d), out, err) && same(d, out));
    Settings m = modified();
    CHECK(validate(m).empty());
    CHECK(parse_settings(to_json(m), out, err) && same(m, out));
    CHECK(parse_settings(to_json(m, false), out, err) && same(m, out));  // compacte vorm
    CHECK(to_json(m, false).find('\n') == std::string::npos);            // IPC is regelgebaseerd
    CHECK(contains(to_json(m), "\"schema\": 1") && contains(to_json(m), "\"mode\":\"320x240@125\""));

    // Alleen een schema: alle secties vallen terug op de standaardwaarden.
    CHECK(parse_settings(R"({"schema":1})", out, err) && same(d, out));

    // Validatie: bereiken, typen en onbekende waarden.
    auto bad = [&](const std::string& json, const std::string& frag) {
        Settings o = modified();
        std::string e;
        bool ok = parse_settings(json, o, e);
        CHECK(!ok);
        CHECK(contains(e, frag));
        CHECK(same(o, modified()));  // 'out' blijft ongemoeid
    };
    bad(R"({"schema":1,"camera":{"exposure":300}})", "camera.exposure");
    bad(R"({"schema":1,"camera":{"exposure":1.5}})", "camera.exposure");
    bad(R"({"schema":1,"camera":{"gain":64}})", "camera.gain");
    bad(R"({"schema":1,"camera":{"mode":"800x600@30"}})", "camera.mode");
    bad(R"({"schema":1,"camera":{"auto_awb":"ja"}})", "camera.auto_awb");
    bad(R"({"schema":1,"led":{"preset":"paars"}})", "led.preset");
    bad(R"({"schema":1,"led":{"r":256}})", "led.r");
    bad(R"({"schema":1,"tracker":{"hue_deg":181}})", "tracker.hue_deg");
    bad(R"({"schema":1,"tracker":{"hue_tol_deg":0}})", "tracker.hue_tol_deg");
    bad(R"({"schema":1,"filter":{"beta":-1}})", "filter.beta");
    bad(R"({"schema":1,"aim":{"h_edge_deg":90}})", "aim.h_edge_deg");
    bad(R"({"schema":1,"web":{"port":0}})", "web.port");
    bad(R"({"schema":1,"web":{"port":70000}})", "web.port");
    bad(R"({"schema":1,"web":5})", "web");
    bad(R"([1,2])", "JSON-object");
    bad(R"({"schema":1,)", "JSON");
    bad(R"({"led":{}})", "schema");
    bad(R"({"schema":2})", "nieuwer");
    bad(R"({"schema":"1"})", "schema");

    // Onbekende sleutels worden genegeerd, met een waarschuwing.
    std::vector<std::string> w;
    CHECK(parse_settings(R"({"schema":1,"toekomst":1,"web":{"port":9000,"nieuw":true}})", out, err, &w));
    CHECK(out.web.port == 9000 && w.size() == 2);

    // Validatie op de structuur zelf (bijvoorbeeld door de UI of een bug).
    Settings s;
    s.camera.exposure = -1;
    CHECK(!validate(s).empty());
    s = Settings{};
    s.camera.fps = 61;
    CHECK(contains(validate(s), "camera.mode"));
}

static void test_migration_hook() {
    // Schema 0 is een (denkbeeldige) oudere vorm met "kleur" in plaats van led.preset.
    std::vector<MigrationStep> steps;
    steps.push_back({0, [](json::Value& root, std::string& err) {
                         for (auto& kv : root.obj) {
                             if (kv.first == "schema") kv.second.num = 1;
                             if (kv.first == "kleur") {
                                 kv.first = "led";
                                 json::Value preset;
                                 preset.type = json::Value::Type::String;
                                 preset.str = kv.second.str;
                                 json::Value led;
                                 led.type = json::Value::Type::Object;
                                 led.obj.push_back({"preset", preset});
                                 kv.second = led;
                             }
                         }
                         (void)err;
                         return true;
                     }});
    Settings out;
    std::string err;
    CHECK(parse_settings(R"({"schema":0,"kleur":"rood"})", out, err, nullptr, &steps));
    CHECK(out.led.preset == "rood");
    // Zonder migratie voor die versie: nette fout. Een falende migratie wordt gemeld.
    CHECK(!parse_settings(R"({"schema":0})", out, err) && contains(err, "geen migratie"));
    std::vector<MigrationStep> failing;
    failing.push_back({0, [](json::Value&, std::string& e) { e = "kapot"; return false; }});
    CHECK(!parse_settings(R"({"schema":0})", out, err, nullptr, &failing) && contains(err, "kapot"));
    CHECK(!parse_settings(R"({"schema":-1})", out, err) && contains(err, "ongeldig"));
}

static void test_store_files() {
    std::string dir = tmpdir();
    std::string path = dir + "/cfg/ravengun/config.json";

    // Geen bestand: standaardwaarden, en load() maakt niets aan.
    SettingsStore st(path);
    LoadInfo li = st.load();
    CHECK(li.state == LoadInfo::State::Missing && li.message.empty());
    CHECK(same(st.get(), Settings{}) && !st.file_present() && !fs::exists(dir + "/cfg"));

    // Eerste wijziging: bestand 0600 in een map 0700, geen tijdelijke restanten, geldig schema.
    SaveResult sr = st.update([](Settings& s) { s.camera.exposure = 80; s.led = {true, "rood", 255, 0, 0}; });
    CHECK(sr.ok);
    CHECK(mode_of(path) == 0600 && mode_of(dir + "/cfg/ravengun") == 0700);
    CHECK(tmp_files(dir + "/cfg/ravengun") == 0);
    CHECK(contains(slurp(path), "\"exposure\":80"));
    CHECK(st.info().state == LoadInfo::State::Loaded);

    // Rondgang: een nieuwe store leest dezelfde waarden.
    SettingsStore st2(path);
    CHECK(st2.load().state == LoadInfo::State::Loaded);
    CHECK(st2.get().camera.exposure == 80 && st2.get().led.preset == "rood");

    // Ongeldige update: niet overgenomen, bestand ongewijzigd.
    std::string before = slurp(path);
    sr = st.update([](Settings& s) { s.camera.exposure = 999; });
    CHECK(!sr.ok && contains(sr.message, "camera.exposure"));
    CHECK(st.get().camera.exposure == 80 && slurp(path) == before);

    // Tweede wijziging maakt een laatst-goede kopie van de vorige inhoud.
    sr = st.update([](Settings& s) { s.camera.exposure = 90; });
    CHECK(sr.ok && fs::exists(path + ".bak") && mode_of(path + ".bak") == 0600);
    Settings bak;
    std::string err;
    CHECK(parse_settings(slurp(path + ".bak"), bak, err) && bak.camera.exposure == 80);

    // Corrupt hoofdbestand: laatst-goede kopie wordt gebruikt, met melding.
    spit(path, "{ dit is geen json");
    SettingsStore st3(path);
    li = st3.load();
    CHECK(li.state == LoadInfo::State::Recovered && contains(li.message, "laatst-goede"));
    CHECK(st3.get().camera.exposure == 80);
    CHECK(slurp(path) == "{ dit is geen json");  // load() raakt het bestand niet aan

    // De eerstvolgende wijziging zet het corrupte bestand opzij (nooit stil weggooien); de kopie blijft goed.
    sr = st3.update([](Settings& s) { s.aim.h_edge_deg = 22; });
    CHECK(sr.ok && fs::exists(path + ".corrupt") && slurp(path + ".corrupt") == "{ dit is geen json");
    CHECK(parse_settings(slurp(path + ".bak"), bak, err) && bak.camera.exposure == 80);
    SettingsStore st4(path);
    CHECK(st4.load().state == LoadInfo::State::Loaded && st4.get().aim.h_edge_deg == 22);

    // Corrupt zonder kopie: standaardwaarden en een duidelijke melding.
    std::string dir2 = tmpdir() + "/b";
    spit(dir2 + "/config.json", R"({"schema":1,"camera":{"gain":99}})");
    SettingsStore st5(dir2 + "/config.json");
    li = st5.load();
    CHECK(li.state == LoadInfo::State::Invalid && contains(li.message, "camera.gain") && contains(li.message, "standaardwaarden"));
    CHECK(same(st5.get(), Settings{}));

    // Een te nieuw schema wordt niet half toegepast.
    spit(dir2 + "/config.json", R"({"schema":7,"web":{"port":1234}})");
    SettingsStore st6(dir2 + "/config.json");
    CHECK(st6.load().state == LoadInfo::State::Invalid && st6.get().web.port == 8765);

    // Te groot bestand en een symlink worden geweigerd.
    spit(dir2 + "/groot.json", std::string(70 * 1024, ' '));
    SettingsStore st7(dir2 + "/groot.json");
    CHECK(st7.load().state == LoadInfo::State::Invalid);
    ::symlink((dir2 + "/config.json").c_str(), (dir2 + "/link.json").c_str());
    SettingsStore st8(dir2 + "/link.json");
    CHECK(st8.load().state == LoadInfo::State::Invalid);
}

static void test_atomic_write() {
    std::string dir = tmpdir();
    std::string path = dir + "/f.json";
    std::string err;
    CHECK(write_file_atomic(path, "een", err) && slurp(path) == "een" && mode_of(path) == 0600);
    CHECK(write_file_atomic(path, "twee", err) && slurp(path) == "twee" && tmp_files(dir) == 0);

    // Een tijdelijk bestand blijft nooit achter bij een mislukte vervanging (doel is een map).
    fs::create_directories(dir + "/doel");
    CHECK(!write_file_atomic(dir + "/doel", "x", err) && contains(err, "hernoemen"));
    CHECK(tmp_files(dir) == 0 && fs::is_directory(dir + "/doel"));

    // Een bestaand doel blijft intact als het tijdelijke bestand niet te maken is (map is een bestand).
    spit(dir + "/blok", "x");
    CHECK(!write_file_atomic(dir + "/blok/config.json", "y", err));
    CHECK(slurp(dir + "/blok") == "x");

    // Opslaan mislukt: de wijziging blijft in het geheugen (de daemon past hem toe) en de fout wordt gemeld.
    SettingsStore st(dir + "/blok/config.json");
    SaveResult sr = st.update([](Settings& s) { s.camera.gain = 5; });
    CHECK(!sr.ok && contains(sr.message, "niet bewaard") && st.get().camera.gain == 5 && !st.last_save_error().empty());

    // Zonder pad (--no-config): wel in het geheugen, niet bewaard.
    SettingsStore none((std::string()));
    sr = none.update([](Settings& s) { s.camera.gain = 6; });
    CHECK(!sr.ok && none.get().camera.gain == 6);
}

static void test_led_presets() {
    // Gemeten tinten (docs/decisions.md): cyaan -75, blauw -31, groen -145, rood 119, magenta 86.
    struct E { const char* n; double hue; int r, g, b; };
    const E exp[] = {{"cyaan", -75, 0, 255, 255}, {"blauw", -31, 0, 0, 255}, {"groen", -145, 0, 255, 0},
                     {"rood", 119, 255, 0, 0}, {"magenta", 86, 255, 0, 255}};
    for (const auto& e : exp) {
        LedChoice c;
        CHECK(resolve_led_preset(e.n, c));
        CHECK(c.preset == e.n && c.r == e.r && c.g == e.g && c.b == e.b);
        CHECK(c.hue_known && c.hue_deg == e.hue && c.hue_tol_deg == kPresetHueTolDeg && c.hue_source == "gemeten");
        CHECK(c.warnings.empty());
        LedChoice c2 = resolve_led_rgb(e.r, e.g, e.b);  // exact dezelfde RGB = dezelfde preset
        CHECK(c2.preset == e.n && c2.hue_deg == e.hue);
    }
    LedChoice c;
    CHECK(resolve_led_preset("CYAN", c) && c.preset == "cyaan");
    CHECK(!resolve_led_preset("paars", c));

    // Wit: geen tint, tracker blijft ongewijzigd, met een waarschuwing.
    CHECK(resolve_led_preset("wit", c) && !c.hue_known && c.hue_source == "geen" && c.warnings.size() == 1);

    // Eigen kleur: nominale tint, ruimere tolerantie en een waarschuwing.
    c = resolve_led_rgb(255, 128, 0);  // oranje
    CHECK(c.preset == "custom" && c.hue_known && c.hue_source == "nominaal");
    CHECK(c.hue_tol_deg == kCustomHueTolDeg && c.hue_tol_deg > kPresetHueTolDeg);
    CHECK(c.hue_deg > 90 && c.hue_deg < 180);  // tussen magenta/rood en geel (nominaal)
    CHECK(c.warnings.size() == 1 && contains(c.warnings[0], "niet gemeten"));

    // Bijna grijs en uit: geen tint.
    c = resolve_led_rgb(120, 110, 100);
    CHECK(c.preset == "custom" && !c.hue_known && contains(c.warnings[0], "verzadiging"));
    c = resolve_led_rgb(0, 0, 0);
    CHECK(!c.hue_known && contains(c.warnings[0], "uit"));
    // Waarden buiten het bereik worden afgekapt.
    c = resolve_led_rgb(999, -5, 0);
    CHECK(c.r == 255 && c.g == 0);
    CHECK(led_presets().size() == 6);
}

static ipc::Request parse_ok(const std::string& line, bool& ok, std::string* err = nullptr) {
    auto r = ipc::parse_request(line);
    if (auto* rq = std::get_if<ipc::Request>(&r)) { ok = true; return *rq; }
    ok = false;
    if (err) *err = std::get<ipc::Error>(r).message;
    return {};
}

static void test_ipc_new_commands() {
    bool ok = false;
    std::string err;
    auto rq = parse_ok(R"({"v":1,"cmd":"set_led","preset":"magenta"})", ok);
    CHECK(ok && rq.cmd == ipc::Cmd::SetLed && rq.led_preset == "magenta");
    rq = parse_ok(R"({"v":1,"cmd":"set_led","r":1,"g":2,"b":255})", ok);
    CHECK(ok && rq.led_r && *rq.led_r == 1 && *rq.led_b == 255);
    parse_ok(R"({"v":1,"cmd":"set_led"})", ok, &err);
    CHECK(!ok && contains(err, "preset"));
    parse_ok(R"({"v":1,"cmd":"set_led","preset":"rood","r":1,"g":2,"b":3})", ok, &err);
    CHECK(!ok && contains(err, "allebei"));
    parse_ok(R"({"v":1,"cmd":"set_led","r":1,"g":2})", ok);
    CHECK(!ok);
    parse_ok(R"({"v":1,"cmd":"set_led","r":256,"g":0,"b":0})", ok, &err);
    CHECK(!ok && contains(err, "'r'"));
    parse_ok(R"({"v":1,"cmd":"set_led","r":1.5,"g":0,"b":0})", ok, &err);
    CHECK(!ok && contains(err, "geheel"));
    parse_ok(R"({"v":1,"cmd":"set_led","preset":"rood","kleur":1})", ok, &err);
    CHECK(!ok && contains(err, "onbekend veld"));

    rq = parse_ok(R"({"v":1,"cmd":"set_camera","exposure":0,"gain":63,"auto_exposure":false,"mode":"320x240@187"})", ok);
    CHECK(ok && rq.cmd == ipc::Cmd::SetCamera && *rq.exposure == 0 && *rq.gain == 63 && rq.mode == "320x240@187");
    parse_ok(R"({"v":1,"cmd":"set_camera","exposure":256})", ok, &err);
    CHECK(!ok && contains(err, "'exposure'"));
    parse_ok(R"({"v":1,"cmd":"set_camera","exposure":-1})", ok);
    CHECK(!ok);
    parse_ok(R"({"v":1,"cmd":"set_camera","gain":64})", ok, &err);
    CHECK(!ok && contains(err, "'gain'"));
    parse_ok(R"({"v":1,"cmd":"set_camera","gain":2.5})", ok, &err);
    CHECK(!ok && contains(err, "geheel"));
    parse_ok(R"({"v":1,"cmd":"set_camera","mode":"1920x1080@30"})", ok, &err);
    CHECK(!ok && contains(err, "mode"));
    parse_ok(R"({"v":1,"cmd":"set_camera","auto_gain":"aan"})", ok);
    CHECK(!ok);
    parse_ok(R"({"v":1,"cmd":"set_camera"})", ok, &err);
    CHECK(!ok && contains(err, "zonder te wijzigen"));
    for (const char* m : {"640x480@60", "320x240@125", "320x240@187"}) {
        rq = parse_ok(std::string(R"({"v":1,"cmd":"set_camera","mode":")") + m + "\"}", ok);
        CHECK(ok && rq.mode == m);
    }
    rq = parse_ok(R"({"v":1,"cmd":"set_web","port":18766})", ok);
    CHECK(ok && *rq.web_port == 18766);
    parse_ok(R"({"v":1,"cmd":"set_web","port":0})", ok);
    CHECK(!ok);
    for (const char* c : {"info", "settings", "diagnostics"}) {
        parse_ok(std::string(R"({"v":1,"cmd":")") + c + "\"}", ok);
        CHECK(ok);
        parse_ok(std::string(R"({"v":1,"cmd":")") + c + R"(","x":1})", ok);
        CHECK(!ok);
    }

    // Rondgang van make_request voor de nieuwe commando's.
    ipc::Request a;
    a.cmd = ipc::Cmd::SetCamera;
    a.exposure = 42; a.gain = 3; a.auto_awb = true; a.mode = "640x480@60";
    auto r2 = parse_ok(ipc::make_request(a), ok);
    CHECK(ok && *r2.exposure == 42 && *r2.gain == 3 && *r2.auto_awb && r2.mode == "640x480@60");
    ipc::Request l;
    l.cmd = ipc::Cmd::SetLed;
    l.led_preset = "groen";
    r2 = parse_ok(ipc::make_request(l), ok);
    CHECK(ok && r2.led_preset == "groen");
    l.led_preset.clear(); l.led_r = 9; l.led_g = 8; l.led_b = 7;
    r2 = parse_ok(ipc::make_request(l), ok);
    CHECK(ok && *r2.led_g == 8);
}

static void test_sharp_shooter_buttons() {
    Settings m = modified();
    Settings out;
    std::string err;
    CHECK(contains(to_json(m, false), "\"sharp_shooter\":{\"buttons\":{"));
    CHECK(parse_settings(to_json(m), out, err) && out.sharp_shooter.buttons.size() == 3 && out.sharp_shooter.buttons["pomp"] == "move");
    // Zonder sectie: leeg. Onbekende rol, onbekende Move-knop en verkeerd type worden geweigerd.
    CHECK(parse_settings(R"({"schema":1})", out, err) && out.sharp_shooter.buttons.empty());
    CHECK(!parse_settings(R"({"schema":1,"sharp_shooter":{"buttons":{"laser":"t"}}})", out, err) && contains(err, "onbekende knop"));
    CHECK(!parse_settings(R"({"schema":1,"sharp_shooter":{"buttons":{"trekker":"l2"}}})", out, err) && contains(err, "geen Move-knop"));
    CHECK(!parse_settings(R"({"schema":1,"sharp_shooter":{"buttons":{"trekker":5}}})", out, err));
    CHECK(!parse_settings(R"({"schema":1,"sharp_shooter":{"buttons":[1]}})", out, err));
    CHECK(parse_settings(R"({"schema":1,"sharp_shooter":{"buttons":{"trekker":""}}})", out, err));  // leeg = niet gemeten
    CHECK(config::sharp_shooter_roles().size() == 10 && config::move_button_name_valid("t") && !config::move_button_name_valid(""));

    bool ok = false;
    auto rq = parse_ok(R"({"v":1,"cmd":"set_buttons","trekker":"t","pomp":"move","ps":""})", ok);
    CHECK(ok && rq.cmd == ipc::Cmd::SetButtons && rq.button_map.size() == 3 && rq.button_map["pomp"] == "move" && rq.button_map["ps"].empty());
    auto rt = parse_ok(ipc::make_request(rq), ok);
    CHECK(ok && rt.button_map == rq.button_map);
    parse_ok(R"({"v":1,"cmd":"set_buttons"})", ok, &err);
    CHECK(!ok && contains(err, "zonder"));
    parse_ok(R"({"v":1,"cmd":"set_buttons","laser":"t"})", ok, &err);
    CHECK(!ok && contains(err, "onbekend veld"));
    parse_ok(R"({"v":1,"cmd":"set_buttons","trekker":"l2"})", ok, &err);
    CHECK(!ok && contains(err, "onbekende Move-knop"));
    parse_ok(R"({"v":1,"cmd":"set_buttons","trekker":3})", ok, &err);
    CHECK(!ok);

    rq = parse_ok(R"({"v":1,"cmd":"calibrate_ui","action":"open"})", ok);
    CHECK(ok && rq.cmd == ipc::Cmd::CalibrateUi && rq.action == "open");
    rt = parse_ok(ipc::make_request(rq), ok);
    CHECK(ok && rt.action == "open");
    for (const char* a : {"close", "status"}) {
        rq = parse_ok(std::string(R"({"v":1,"cmd":"calibrate_ui","action":")") + a + "\"}", ok);
        CHECK(ok && rq.action == a);
    }
    rq = parse_ok(R"({"v":1,"cmd":"calibrate_ui","action":"open","step":"practice"})", ok);
    CHECK(ok && rq.step == "practice");
    rt = parse_ok(ipc::make_request(rq), ok);
    CHECK(ok && rt.step == "practice");
    parse_ok(R"({"v":1,"cmd":"calibrate_ui","action":"open","step":"mag"})", ok, &err);
    CHECK(!ok);
    parse_ok(R"({"v":1,"cmd":"calibrate_ui","action":"close","step":"practice"})", ok, &err);
    CHECK(!ok);
    parse_ok(R"({"v":1,"cmd":"calibrate_ui","action":"verwijder"})", ok, &err);
    CHECK(!ok);
    parse_ok(R"({"v":1,"cmd":"calibrate_ui"})", ok, &err);
    CHECK(!ok);
    parse_ok(R"({"v":1,"cmd":"calibrate_ui","action":"open","url":"http://x"})", ok, &err);
    CHECK(!ok && contains(err, "onbekend veld"));
}

static void test_fusion_and_gun_buttons() {
    config::Settings s;
    CHECK(s.fusion.mode == "mahony");
    CHECK(s.gun_buttons.map.at("move") == "grenade" && s.gun_buttons.map.at("cross") == "reload");
    CHECK(s.gun_buttons.map.at("triangle") == "key:5" && s.gun_buttons.map.at("square") == "key:1");
    CHECK(s.gun_buttons.map.at("select") == "key:5" && s.gun_buttons.map.at("start") == "key:1" && s.gun_buttons.map.at("circle") == "none");
    CHECK(s.gun_buttons.trigger_on == 140 && s.gun_buttons.trigger_off == 100);
    CHECK(config::validate(s).empty());
    s.fusion.mode = "mahony_mag";
    s.gun_buttons.map["circle"] = "key:Space";
    s.gun_buttons.map["cross"] = "";
    std::string text = config::to_json(s);
    CHECK(text.find("\"map\"") != std::string::npos && text.find("\"reload\":") == std::string::npos && text.find("\"recenter\"") == std::string::npos);
    config::Settings b;
    std::string err;
    CHECK(config::parse_settings(text, b, err));
    CHECK(b.fusion.mode == "mahony_mag" && b.gun_buttons.map.at("circle") == "key:space" && b.gun_buttons.map.at("cross") == "none");
    CHECK(b.gun_buttons.map.size() == 13 && b.gun_buttons.map.at("move") == "grenade");
    CHECK(config::to_json(b) == text);  // stabiel
    // Bestand zonder de nieuwe secties: standaardwaarden.
    config::Settings c;
    CHECK(config::parse_settings(R"({"schema":1})", c, err) && c.fusion.mode == "mahony" && c.gun_buttons.map.at("move") == "grenade");
    // Nieuw formaat: gedeeltelijke map vult de defaults aan; meerdere knoppen op dezelfde actie mag.
    CHECK(config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"circle":"reload","cross":"reload","square":"aux1","start":"none"}}})", c, err));
    CHECK(c.gun_buttons.map.at("circle") == "reload" && c.gun_buttons.map.at("cross") == "reload" && c.gun_buttons.map.at("square") == "aux1");
    CHECK(c.gun_buttons.map.at("start") == "none" && c.gun_buttons.map.at("move") == "grenade" && c.gun_buttons.map.at("select") == "key:5");
    CHECK(config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"triangle":"recenter","circle":"key:KEY_F1"}}})", c, err));
    CHECK(c.gun_buttons.map.at("triangle") == "recenter" && config::to_json(c).find("\"circle\":\"key:f1\"") != std::string::npos);
    // Oud formaat (reload/grenade/recenter = knopnaam): vertaald, overschrijft de default voor die knop; opslaan = nieuw formaat.
    std::vector<std::string> w;
    CHECK(config::parse_settings(R"({"schema":1,"gun_buttons":{"trigger_on":150,"reload":"circle","grenade":"square","recenter":"select"}})", c, err, &w));
    CHECK(c.gun_buttons.map.at("circle") == "reload" && c.gun_buttons.map.at("square") == "grenade" && c.gun_buttons.map.at("select") == "recenter");
    CHECK(c.gun_buttons.map.at("cross") == "reload" && c.gun_buttons.map.at("start") == "key:1" && c.gun_buttons.trigger_on == 150);
    CHECK(w.empty());
    std::string saved = config::to_json(c);
    CHECK(saved.find("\"reload\":") == std::string::npos && saved.find("\"grenade\":") == std::string::npos && saved.find("\"recenter\":") == std::string::npos);
    CHECK(config::parse_settings(saved, b, err) && b.gun_buttons.map == c.gun_buttons.map);
    // Oud, leeg = geen wijziging; oud recenter "ps" wordt genegeerd (PS is altijd recenter).
    w.clear();
    CHECK(config::parse_settings(R"({"schema":1,"gun_buttons":{"reload":"","recenter":"ps"}})", c, err, &w));
    CHECK(c.gun_buttons.map == config::GunButtonSettings{}.map && !w.empty());
    // Ongeldig: ps in de map (duidelijke tekst), t, onbekende knop, onbekende toets, onbekende actie, hysterese.
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"ps":"key:1"}}})", c, err));
    CHECK(contains(err, "PS-knop") && contains(err, "recenter") && contains(err, "ps"));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"ps":"none"}}})", c, err));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"t":"reload"}}})", c, err) && contains(err, "trekker"));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"knop":"reload"}}})", c, err) && contains(err, "knop"));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"cross":"key:hyper"}}})", c, err) && contains(err, "hyper"));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"cross":"vuur"}}})", c, err) && contains(err, "vuur"));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"map":{"cross":5}}})", c, err));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"map":[]}})", c, err));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"reload":"ps"}})", c, err));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"reload":"t"}})", c, err));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"reload":"knop"}})", c, err));
    CHECK(!config::parse_settings(R"({"schema":1,"fusion":{"mode":"eskf"}})", c, err));
    CHECK(!config::parse_settings(R"({"schema":1,"gun_buttons":{"trigger_on":100,"trigger_off":120}})", c, err));
    // validate() op een rechtstreeks aangepaste struct.
    config::Settings v;
    v.gun_buttons.map["ps"] = "recenter";
    CHECK(contains(config::validate(v), "PS-knop"));
}

int main() {
    test_fusion_and_gun_buttons();
    test_sharp_shooter_buttons();
    test_roundtrip_and_validation();
    test_migration_hook();
    test_store_files();
    test_atomic_write();
    test_led_presets();
    test_ipc_new_commands();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
