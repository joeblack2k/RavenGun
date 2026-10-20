// Tests voor de bediening per game ([Controls] in game.ini, src/ravengun/gamesettings/controls_settings.*): het formaat, de keuzes per
// speler, opslaan in de gebruikerslaag, needsSetup (controls_need_setup) en het toepassen op de knoppenindeling (zonder keuzes
// ongewijzigd). Raakt alleen een tijdelijke map.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "ravengun/config/settings.hpp"
#include "ravengun/gamesettings/controls_settings.hpp"
#include "ravengun/output/live_feed.hpp"

namespace fs = std::filesystem;
namespace gs = ravengun::gamesettings;
namespace cfg = ravengun::config;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static void put(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

static const char* kShipped =
    "[Game]\nProfile = Demo\n[Controls]\n"
    "trigger = Shoot | Fires the gun.\n"
    "reload = Reload | Reloads your weapon.\n"
    "grenade = Grenade\n"
    "offscreen = Reload off-screen | Aim away from the screen to reload.\n"
    "coin = Coin | Adds a credit.\n"
    "bogus = Nope | Not a signal.\n";

static void test_format() {
    const gs::Ini ini = gs::parse_ini(kShipped);
    const gs::ControlsInfo ci = gs::controls_info(ini);
    CHECK(ci.entries.size() == 5);   // bogus telt niet
    CHECK(ci.warnings.size() == 1);
    CHECK(ci.entries[0].signal == "trigger" && !ci.entries[0].assignable && ci.entries[0].label == "Shoot");
    CHECK(ci.entries[0].explanation == "Fires the gun.");
    CHECK(ci.entries[1].assignable && ci.entries[1].label == "Reload");
    CHECK(ci.entries[2].label == "Grenade" && ci.entries[2].explanation.empty());   // zonder '|': alleen een label
    CHECK(ci.entries[3].signal == "offscreen" && !ci.entries[3].assignable);
    CHECK(!ci.confirmed);
    // Lege waarde haalt een signaal weg (laag boven laag).
    gs::Ini over = gs::parse_ini("[Controls]\ngrenade =\n");
    gs::Ini merged = ini;
    gs::merge_into(merged, over);
    CHECK(gs::controls_info(merged).entries.size() == 4);
    // Een game zonder [Controls]
    CHECK(gs::controls_info(gs::parse_ini("[Game]\nProfile = X\n")).entries.empty());
    CHECK(gs::control_action("start", 2) == "key:2" && gs::control_action("coin", 2) == "key:5" && gs::control_action("reload", 1) == "reload");
    CHECK(gs::control_action("trigger", 1).empty());
}

static void test_overrides_and_apply() {
    gs::Ini ini = gs::parse_ini(std::string(kShipped) +
                                "P1.Cross = grenade\nP1.Square = start\nP1.Circle = none\nP1.PS = reload\nP2.Move = coin\nP1.Triangle = key:7\nP1.Start = whatever\n");
    std::vector<std::string> warn;
    auto o1 = gs::controls_overrides(ini, 1, &warn);
    CHECK(o1.size() == 4);   // cross, square, circle, triangle
    CHECK(warn.size() == 2);   // PS (vast) en 'whatever'
    bool cross = false, square = false;
    for (const auto& kv : o1) {
        if (kv.first == "cross") cross = kv.second == "grenade";
        if (kv.first == "square") square = kv.second == "key:1";
    }
    CHECK(cross && square);
    auto o2 = gs::controls_overrides(ini, 2);
    CHECK(o2.size() == 1 && o2[0].first == "move" && o2[0].second == "key:5");
    CHECK(gs::controls_overrides(ini, 3).empty());

    // The Sharp Shooter inputs (pump, ss_rl) can be given a signal; they reach gun_buttons.map like the Move buttons.
    gs::Ini ss = gs::parse_ini(std::string(kShipped) + "P1.Pump = grenade\nP1.SS_RL = none\n");
    std::vector<std::string> ssw;
    auto os = gs::controls_overrides(ss, 1, &ssw);
    CHECK(os.size() == 2 && ssw.empty());
    const auto ssmap = cfg::with_gun_overrides(cfg::GunButtonSettings{}, os);
    CHECK(ssmap.map.at("pump") == "grenade" && ssmap.map.at("ss_rl") == "none");

    // Toepassen: zonder keuzes ongewijzigd, met keuzes alleen die knoppen.
    const cfg::GunButtonSettings def;
    const auto same = cfg::with_gun_overrides(def, {});
    CHECK(same.map == def.map);
    CHECK(ravengun::output::describe_layout(ravengun::output::build_live_map(same)) ==
          ravengun::output::describe_layout(ravengun::output::build_live_map(def)));
    const auto g = cfg::with_gun_overrides(def, o1);
    CHECK(g.map.at("cross") == "grenade" && g.map.at("square") == "key:1" && g.map.at("circle") == "none" && g.map.at("triangle") == "key:7");
    CHECK(g.map.at("move") == def.map.at("move") && g.map.at("select") == def.map.at("select"));   // de rest blijft
    // Ongeldige acties en onbekende knoppen: overgeslagen
    const auto bad = cfg::with_gun_overrides(def, {{"cross", "launch-missiles"}, {"nonexistent", "reload"}});
    CHECK(bad.map == def.map);
    const auto live = ravengun::output::build_live_map(g);
    const uint64_t cross_bit = ravengun::output::move_button_bit("cross");
    CHECK((live.gun[static_cast<int>(ravengun::output::GunButton::Grenade)] & cross_bit) != 0);
}

static void test_need_setup_and_save(const std::string& root) {
    const gs::Dirs dirs{root + "/shipped", root + "/user"};
    put(dirs.shipped + "/Demo/game.ini", kShipped);
    put(dirs.shipped + "/Mk/game.ini", "[Game]\nProfile = Mk\n[Gamepad]\nRightX = wheel\n");
    put(dirs.shipped + "/TrigOnly/game.ini", "[Game]\nProfile = TrigOnly\n[Controls]\ntrigger = Shoot | Fires.\n");
    CHECK(gs::controls_need_setup(gs::load_game(dirs, "Demo").settings));
    CHECK(!gs::controls_need_setup(gs::load_game(dirs, "Mk").settings));        // geen lightgun-game
    CHECK(!gs::controls_need_setup(gs::load_game(dirs, "TrigOnly").settings));  // niets om te kiezen
    CHECK(!gs::controls_need_setup(gs::load_game(dirs, "Bestaatniet").settings));

    std::string err;
    CHECK(gs::save_user_controls(dirs, "Demo", {{{"cross", "grenade"}, {"square", "none"}}, {{"cross", "grenade"}}}, &err));
    gs::Game g = gs::load_game(dirs, "Demo");
    CHECK(!gs::controls_need_setup(g.settings));
    CHECK(g.get("Controls", "P1.Cross") == "grenade" && g.get("Controls", "P2.Cross") == "grenade" && g.get("Controls", "Confirmed") == "true");
    CHECK(gs::controls_overrides(g.settings, 1).size() == 2);
    CHECK(gs::controls_info(g.settings).entries.size() == 5);   // de meegeleverde labels blijven
    // Opnieuw opslaan vervangt de oude keuzes
    CHECK(gs::save_user_controls(dirs, "Demo", {{{"triangle", "reload"}}}, &err));
    g = gs::load_game(dirs, "Demo");
    auto o = gs::controls_overrides(g.settings, 1);
    CHECK(o.size() == 1 && o[0].first == "triangle" && gs::controls_overrides(g.settings, 2).empty());
    // Terug naar de standaard
    CHECK(gs::reset_user_controls(dirs, "Demo", &err));
    g = gs::load_game(dirs, "Demo");
    CHECK(gs::controls_need_setup(g.settings) && gs::controls_overrides(g.settings, 1).empty());
}

static void test_repo_tree() {
    // Alle meegeleverde [Controls]-secties moeten zonder waarschuwing lezen.
    const std::string patches = std::string(RAVENGUN_SOURCE_DIR) + "/patches";
    if (!fs::exists(patches)) { std::printf("controls: patches/ ontbreekt, boomtest overgeslagen\n"); return; }
    const gs::Dirs dirs{patches, {}};
    int with = 0;
    for (const auto& name : gs::list_games(dirs)) {
        const gs::Game g = gs::load_game(dirs, name);
        const gs::ControlsInfo ci = gs::controls_info(g.settings);
        for (const auto& w : ci.warnings) std::fprintf(stderr, "%s: %s\n", name.c_str(), w.c_str());
        CHECK(ci.warnings.empty());
        std::vector<std::string> warn;
        for (int p = 1; p <= 4; ++p) gs::controls_overrides(g.settings, p, &warn);
        CHECK(warn.empty());
        if (!ci.entries.empty()) ++with;
    }
    std::printf("controls: %d games met [Controls] in patches/\n", with);
}

int main() {
    char tmpl[] = "/tmp/rg-controls-XXXXXX";
    const char* t = mkdtemp(tmpl);
    if (!t) { std::perror("mkdtemp"); return 2; }
    const std::string root = t;
    test_format();
    test_overrides_and_apply();
    test_need_setup_and_save(root);
    test_repo_tree();
    fs::remove_all(root);
    std::printf("controls: %d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
