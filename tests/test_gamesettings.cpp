// Tests voor de lezer van de instellingen en patches per game (src/ravengun/gamesettings): INI-formaat (gelijk aan het Python-
// script scripts/gamesettings.py, via tests/data/gamesettings-cases.*), de drie lagen, lege waarden, patches aan/uit per laag,
// het schrijven van de gebruikerslaag en de echte patches/-boom van de repo. Raakt alleen een tijdelijke map.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/json_parse.hpp"

namespace fs = std::filesystem;
namespace gs = ravengun::gamesettings;
namespace json = ravengun::json;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void put(const std::string& path, const std::string& text) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

// Hetzelfde bestand en dezelfde uitkomst als de Python-lezer (tests/test_gamesettings.py).
static void test_conformance() {
    const std::string dir = std::string(RAVENGUN_SOURCE_DIR) + "/tests/data/";
    std::vector<std::string> warn;
    gs::Ini ini = gs::parse_ini(slurp(dir + "gamesettings-cases.ini"), &warn);
    auto expected = json::parse(slurp(dir + "gamesettings-cases.expected.json"));
    CHECK(expected.ok && expected.value.is_object());
    CHECK(!warn.empty());   // de sleutel buiten een sectie en de regel zonder '='
    size_t keys = 0;
    for (const auto& sec : expected.value.obj) {
        for (const auto& kv : sec.second.obj) {
            ++keys;
            const std::string* got = ini.find(sec.first, kv.first);
            CHECK(got && *got == kv.second.str);
            if (!got || *got != kv.second.str)
                std::fprintf(stderr, "  [%s] %s: verwacht '%s', kreeg '%s'\n", sec.first.c_str(), kv.first.c_str(), kv.second.str.c_str(), got ? got->c_str() : "(ontbreekt)");
        }
    }
    size_t have = 0;
    for (const auto& s : ini.sections) have += s.kv.size();
    CHECK(have == keys);                       // niets extra's
    CHECK(ini.sections.size() == expected.value.obj.size());
    CHECK(!ini.has("Game", "orphan") && !ini.has("", "orphan"));
}

static void test_ini_basics() {
    gs::Ini a = gs::parse_ini("[S]\r\nA = 1\r\nB=2 ; c\r\n;x\r\n[T]\r\nC =\r\n");
    CHECK(a.get("S", "A") == "1" && a.get("S", "B") == "2" && a.has("T", "C") && a.get("T", "C", "d") == "");
    gs::Ini base = a;
    gs::merge_into(base, gs::parse_ini("[S]\nA = 9\nB =\nN = nieuw\n[U]\nZ = 1\n"));
    CHECK(base.get("S", "A") == "9" && !base.has("S", "B") && base.get("S", "N") == "nieuw" && base.get("U", "Z") == "1");
    CHECK(gs::truthy("True") && gs::truthy(" 1 ") && gs::truthy("yes") && gs::truthy("ON") && !gs::truthy("0") && !gs::truthy("") && !gs::truthy("nee"));
}

static void test_layers(const std::string& root) {
    const std::string sh = root + "/shipped", us = root + "/user";
    fs::remove_all(root);
    put(sh + "/_defaults/OpenParrot.ini", "[Loader]\nRemoteThread = off\n[Wine]\nDllOverrides = d3d9=b\n[Input]\nJvsPipeBoard = true\n");
    put(sh + "/_defaults/OpenParrot.patches.ini",
        "[Patch: Lader]\nEnabled = false\nKind = file\nTarget = loader\nFind = 00\nReplace = 01\nReason = omdat\n[Patch: Tweede]\nEnabled = false\nKind = file\n");
    put(sh + "/G/game.ini", "[Game]\nName = G\nType = OpenParrot\n[Input]\nRoute = tp-shm\nLayout = X\n[Loader]\nRemoteThread =\n");
    put(sh + "/G/patches.ini", "[Patch: Lader]\nEnabled = true\n");
    gs::Dirs d{sh, us};

    gs::Game g = gs::load_game(d, "G");
    CHECK(g.exists && g.type == "OpenParrot" && g.profile == "G");
    CHECK(g.get("Wine", "DllOverrides") == "d3d9=b" && g.get("Input", "JvsPipeBoard") == "true");   // uit _defaults
    CHECK(!g.settings.has("Loader", "RemoteThread"));                      // een lege waarde in game.ini zet de standaard terug
    CHECK(g.get("Input", "Route") == "tp-shm" && g.get("Game", "Name") == "G");
    CHECK(g.patches.size() == 2 && g.patches[0].name == "Lader" && g.patches[0].enabled() && !g.patches[1].enabled());
    CHECK(g.patches[0].kind() == "file" && g.patches[0].reason() == "omdat" && g.patch("Tweede") && !g.patch("Nee"));

    // Gebruikerslaag: wint per sleutel, lege waarde zet terug, patch uit en een eigen patch.
    put(us + "/G/game.ini", "[Input]\nRoute = evdev\n[Wine]\nDllOverrides =\n[Env]\nFOO = bar\n");
    put(us + "/G/patches.ini", "[Patch: Lader]\nEnabled = false\n[Patch: Eigen]\nEnabled = true\nKind = file\nTarget = core\n");
    g = gs::load_game(d, "G");
    CHECK(g.get("Input", "Route") == "evdev" && g.get("Input", "Layout") == "X");    // de rest van [Input] blijft
    CHECK(!g.settings.has("Wine", "DllOverrides") && g.get("Env", "FOO") == "bar");
    CHECK(g.patches.size() == 3 && !g.patch("Lader")->enabled() && g.patch("Lader")->get("Find") == "00");   // definitie blijft, Enabled uit
    CHECK(g.patch("Eigen") && g.patch("Eigen")->enabled() && g.patches[2].name == "Eigen");
    // Het soort kan de gebruiker veranderen: dan gelden de standaarden van het andere soort.
    put(us + "/G/game.ini", "[Game]\nType = TeknoParrot\n");
    g = gs::load_game(d, "G");
    CHECK(g.type == "TeknoParrot" && !g.settings.has("Wine", "DllOverrides") && !g.settings.has("Loader", "RemoteThread"));
    CHECK(g.patches.size() == 2 && g.patches[0].name == "Lader" && !g.patches[0].enabled() && g.patches[1].name == "Eigen");   // geen gedeelde definities meer
    // Een game zonder bestanden maar met type_hint krijgt de standaarden van dat soort.
    gs::Game h = gs::load_game(d, "Onbekend", "OpenParrot");
    CHECK(!h.exists && h.type == "OpenParrot" && h.get("Loader", "RemoteThread") == "off" && h.patches.size() == 2);
    gs::Game n = gs::load_game(d, "Nergens");
    CHECK(!n.exists && n.type.empty() && n.settings.sections.empty() && n.patches.empty());
    // Games met een map in een van beide lagen.
    put(us + "/Alleen/game.ini", "[Game]\nName = A\n");
    fs::create_directories(us + "/_intern");
    auto list = gs::list_games(d);
    CHECK(list.size() == 2 && list[0] == "Alleen" && list[1] == "G");
}

static void test_edit_and_user_writes(const std::string& root) {
    // Tekst bewerken: commentaar en volgorde blijven staan.
    const std::string t0 = "; kop\n[A]\nx = 1 ; uitleg\ny = 2\n\n; over B\n[B]\nz = 3\n";
    std::string t = gs::edit_ini_text(t0, "A", "y", "9", false);
    CHECK(t == "; kop\n[A]\nx = 1 ; uitleg\ny = 9\n\n; over B\n[B]\nz = 3\n");
    t = gs::edit_ini_text(t0, "A", "n", "nieuw", false);
    CHECK(t == "; kop\n[A]\nx = 1 ; uitleg\ny = 2\nn = nieuw\n\n; over B\n[B]\nz = 3\n");
    t = gs::edit_ini_text(t0, "C", "k", "v", false);
    CHECK(t == t0 + "\n[C]\nk = v\n");
    t = gs::edit_ini_text(t0, "A", "x", "", true);
    CHECK(t == "; kop\n[A]\ny = 2\n\n; over B\n[B]\nz = 3\n");
    CHECK(gs::edit_ini_text(t0, "A", "bestaatniet", "", true) == t0);
    CHECK(gs::edit_ini_text("", "S", "k", "", false) == "[S]\nk =\n");
    CHECK(gs::parse_ini(gs::edit_ini_text(t0, "B", "z", "", false)).has("B", "z"));   // leeg schrijven = "z =" (reset), blijft een sleutel

    const std::string us = root + "/uw";
    fs::remove_all(us);
    gs::Dirs d{root + "/uw-shipped", us};
    put(d.shipped + "/G/game.ini", "[Input]\nRoute = tp-shm\nLayout = L\n");
    put(d.shipped + "/G/patches.ini", "[Patch: P]\nEnabled = false\nKind = file\n");
    std::string err;
    CHECK(gs::set_user_value(d, "G", "Input", "Route", "evdev", &err) && err.empty());
    CHECK(slurp(us + "/G/game.ini") == "[Input]\nRoute = evdev\n");
    CHECK(gs::load_game(d, "G").get("Input", "Route") == "evdev");
    CHECK(gs::set_user_patch_enabled(d, "G", "P", true, &err));
    CHECK(slurp(us + "/G/patches.ini") == "[Patch: P]\nEnabled = true\n");
    CHECK(gs::load_game(d, "G").patch("P")->enabled());
    CHECK(gs::set_user_patch_enabled(d, "G", "P", false, &err) && !gs::load_game(d, "G").patch("P")->enabled());
    CHECK(gs::erase_user_patch_enabled(d, "G", "P", &err));
    CHECK(!gs::parse_ini(slurp(us + "/G/patches.ini")).has("Patch: P", "Enabled"));
    CHECK(gs::erase_user_value(d, "G", "Input", "Route", &err) && gs::load_game(d, "G").get("Input", "Route") == "tp-shm");
    CHECK(gs::erase_user_value(d, "NietAanwezig", "Input", "Route", &err));            // niets te doen, geen fout
    CHECK(!fs::exists(us + "/NietAanwezig"));
    CHECK(!gs::set_user_value(d, "../boos", "A", "b", "c", &err) && !err.empty());       // geen ontsnapping uit de map
    CHECK(!gs::set_user_value(d, "_defaults", "A", "b", "c", &err));
    CHECK(!gs::set_user_value(gs::Dirs{d.shipped, {}}, "G", "A", "b", "c", &err));       // geen gebruikersmap
}

static void test_default_dirs() {
    ::setenv("RAVENGUN_PATCHES_DIR", "", 1);
    ::setenv("RAVENGUN_USER_PATCHES_DIR", "", 1);
    ::setenv("XDG_DATA_HOME", "/tmp/xdg-test", 1);
    ::setenv("HOME", "/tmp/home-test", 1);
    gs::Dirs d = gs::default_dirs("/opt/rg/share/ravengun");
    CHECK(d.shipped == "/opt/rg/share/ravengun/patches" && d.user == "/tmp/xdg-test/ravengun/patches");   // XDG_DATA_HOME wordt gevolgd
    ::unsetenv("XDG_DATA_HOME");
    CHECK(gs::default_dirs("/x").user == "/tmp/home-test/.local/share/ravengun/patches");
    ::setenv("RAVENGUN_PATCHES_DIR", "/p", 1);
    ::setenv("RAVENGUN_USER_PATCHES_DIR", "/u", 1);
    d = gs::default_dirs("/x");
    CHECK(d.shipped == "/p" && d.user == "/u");
    ::setenv("RAVENGUN_PATCHES_DIR", "", 1);
    ::setenv("RAVENGUN_USER_PATCHES_DIR", "", 1);
}

// De echte boom in de repo: elk game.ini en patches.ini is leesbaar zonder waarschuwing en bevat de verwachte gegevens.
static void test_repo_tree() {
    gs::Dirs d{std::string(RAVENGUN_SOURCE_DIR) + "/patches", {}};
    auto games = gs::list_games(d);
    CHECK(games.size() >= 30);
    for (const auto& name : games) {
        gs::Game g = gs::load_game(d, name);
        CHECK(g.exists);
        if (!g.warnings.empty()) std::fprintf(stderr, "  %s: %s\n", name.c_str(), g.warnings[0].c_str());
        CHECK(g.warnings.empty());
        const std::string st = g.get("Status", "Status");
        CHECK(st.empty() || st == "playable" || st == "parked" || st == "untested");
    }
    gs::Game lgi = gs::load_game(d, "LGI");
    CHECK(lgi.type == "TeknoParrot" && lgi.get("Input", "Route") == "jvs" && lgi.get("Wine", "Prefix") == "ravengun-test-lgi");
    CHECK(lgi.get("Status", "Status") == "playable" && !lgi.get("Status", "Note").empty() && !lgi.get("Status", "NoteEN").empty());
    gs::Game gaia = gs::load_game(d, "GaiaAttack4");
    const gs::Patch* p = gaia.patch("OpenParrotLoader zonder CONTEXT_FLOATING_POINT");
    CHECK(p && p->enabled() && p->kind() == "file" && p->get("Count") == "1" && !p->reason().empty());
    for (const char* other : {"TC5", "NERF", "Halo", "LGI"}) {
        gs::Game o = gs::load_game(d, other);
        CHECK(!o.patches.empty() ? !o.patches[0].enabled() : true);   // alleen de vier Type X2-games zetten de lader-patch aan
    }
    int enabled = 0;
    for (const auto& name : games)
        for (const auto& pt : gs::load_game(d, name).patches)
            if (pt.enabled()) {
                ++enabled;
                CHECK(name == "GaiaAttack4" || name == "HauntedMuseum" || name == "HauntedMuseumII" || name == "MusicGunGun2");
            }
    CHECK(enabled == 4);
}

int main() {
    char tmpl[] = "/tmp/rg-gamesettings-XXXXXX";
    const char* t = mkdtemp(tmpl);
    if (!t) { std::perror("mkdtemp"); return 2; }
    const std::string root = t;
    test_conformance();
    test_ini_basics();
    test_layers(root);
    test_edit_and_user_writes(root);
    test_default_dirs();
    test_repo_tree();
    fs::remove_all(root);
    std::printf("gamesettings: %d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
