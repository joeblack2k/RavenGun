// Tests voor de lezer van de instellingen en patches per game (src/ravengun/gamesettings): INI-formaat (gelijk aan het Python-
// script scripts/gamesettings.py, via tests/data/gamesettings-cases.*), de drie lagen, lege waarden, patches aan/uit per laag,
// het schrijven van de gebruikerslaag en de echte patches/-boom van de repo. Raakt alleen een tijdelijke map.
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// Aantal met witruimte gescheiden woorden, gelijk aan Python's str.split() in tests/test_gamesettings.py.
static size_t words(const std::string& text) {
    std::istringstream ss(text);
    std::string w;
    size_t n = 0;
    while (ss >> w) ++n;
    return n;
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

// [Dongle]: dezelfde regels en uitkomsten als Game.dongle() in scripts/gamesettings.py (tests/data/dongle-cases.json).
static void test_dongle() {
    auto cases = json::parse(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/tests/data/dongle-cases.json"));
    CHECK(cases.ok && cases.value.is_array() && cases.value.arr.size() >= 8);
    for (const auto& c : cases.value.arr) {
        const gs::Dongle d = gs::dongle_of(gs::parse_ini(c.find("ini")->str));
        const bool same = d.enabled == c.find("enabled")->b && d.source == c.find("source")->str && d.error == c.find("error")->str;
        CHECK(same);
        if (!same) std::fprintf(stderr, "  dongle-geval '%s': kreeg %d/%s/%s\n", c.find("ini")->str.c_str(), d.enabled, d.source.c_str(), d.error.c_str());
    }
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
        "[Patch: Lader]\nEnabled = false\nKind = file\nCategory = loader\nTarget = loader\nFind = 00\nReplace = 01\nReason = omdat\n[Patch: Tweede]\nEnabled = false\nKind = file\nCategory = Input\n");
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
    put(us + "/G/patches.ini", "[Patch: Lader]\nEnabled = false\n[Patch: Eigen]\nEnabled = true\nKind = file\nCategory = crash\nTarget = core\n");
    g = gs::load_game(d, "G");
    CHECK(g.get("Input", "Route") == "evdev" && g.get("Input", "Layout") == "X");    // de rest van [Input] blijft
    CHECK(!g.settings.has("Wine", "DllOverrides") && g.get("Env", "FOO") == "bar");
    CHECK(g.patches.size() == 3 && !g.patch("Lader")->enabled() && g.patch("Lader")->get("Find") == "00");   // definitie blijft, Enabled uit
    CHECK(g.patch("Eigen") && g.patch("Eigen")->enabled() && g.patches[2].name == "Eigen");
    // Het soort kan de gebruiker veranderen: dan gelden de standaarden van het andere soort.
    put(us + "/G/game.ini", "[Game]\nType = TeknoParrot\n");
    g = gs::load_game(d, "G");
    CHECK(g.type == "TeknoParrot" && !g.settings.has("Wine", "DllOverrides") && !g.settings.has("Loader", "RemoteThread"));
    // Geen gedeelde definities meer: "Lader" is alleen nog een Enabled zonder definitie en wordt afgewezen.
    CHECK(g.patches.size() == 1 && g.patches[0].name == "Eigen" && g.patch_errors.size() == 1 && g.patch_errors[0].name == "Lader");
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

// Categorie, versie, AppliedBy: elke patch heeft een bekende Category, in elke laag.
static void test_patch_policy(const std::string& root) {
    const std::string sh = root + "/pol-shipped", us = root + "/pol-user";
    fs::remove_all(sh);
    fs::remove_all(us);
    put(sh + "/_defaults/OpenParrot.patches.ini",
        "[Patch: Goed]\nEnabled = false\nKind = file\nCategory = Loader\nVersion = 1.08\nTarget = loader\n"
        "[Patch: ZonderCategory]\nEnabled = false\nKind = file\n[Patch: Onbekend]\nEnabled = true\nKind = file\nCategory = security\n"
        "[Patch: Geheugen]\nEnabled = false\nKind = memory\nCategory = input\nAppliedBy = Core\nModule = game.exe\nOffset = 0x10\n");
    put(sh + "/G/game.ini", "[Game]\nType = OpenParrot\n");
    put(sh + "/G/patches.ini", "[Patch: Goed]\nEnabled = true\n");
    gs::Dirs d{sh, us};
    gs::Game g = gs::load_game(d, "G");
    CHECK(g.patches.size() == 2 && g.patches[0].name == "Goed" && g.patches[1].name == "Geheugen");
    CHECK(g.patches[0].category() == "loader" && g.patches[0].version() == "1.08" && g.patches[0].enabled());
    CHECK(g.patches[1].applied_by() == "core" && g.patches[1].kind() == "memory" && g.patches[1].get("Find").empty());
    CHECK(g.patch_errors.size() == 2 && g.patch_errors[0].name == "ZonderCategory" && !g.patch_errors[0].enabled);
    CHECK(g.patch_errors[0].message.find("Category ontbreekt") != std::string::npos &&
          g.patch_errors[0].message.find("graphics, input, timing, audio, paths, crash, network, loader, dongle, region, keychip, integrity, activation") != std::string::npos);
    CHECK(g.patch_errors[1].name == "Onbekend" && g.patch_errors[1].enabled && g.patch_errors[1].message.find("onbekende Category 'security'") != std::string::npos);
    CHECK(!g.warnings.empty());
    for (const char* const* c = gs::kPatchCategories; *c; ++c) CHECK(std::string(*c) != "security" && std::string(*c) != "protection");
    for (const char* c : {"dongle", "region", "keychip", "integrity", "activation"})
        CHECK(gs::patch_categories_text().find(c) != std::string::npos);
    // Ook de gebruikerslaag: een eigen patch zonder of met een onbekende Category wordt afgewezen; een Enabled-override is gewoon goed.
    put(us + "/G/patches.ini", "[Patch: Goed]\nEnabled = false\n[Patch: Eigen]\nEnabled = true\nKind = file\n[Patch: Eigen2]\nEnabled = true\nKind = file\nCategory = onbekend\n"
                               "[Patch: Eigen3]\nEnabled = true\nKind = file\nCategory = timing\n");
    g = gs::load_game(d, "G");
    CHECK(g.patch("Goed") && !g.patch("Goed")->enabled() && g.patch("Eigen3") && !g.patch("Eigen") && !g.patch("Eigen2"));
    int eigen = 0;
    for (const auto& e : g.patch_errors) if (e.name == "Eigen" || e.name == "Eigen2") ++eigen;
    CHECK(eigen == 2);
    // De gebruiker kan een categorie van een meegeleverde patch niet naar iets onbekends zetten.
    put(us + "/G/patches.ini", "[Patch: Goed]\nCategory = onbekend\n");
    g = gs::load_game(d, "G");
    CHECK(!g.patch("Goed"));
    // De repo-boom: elke patch heeft een geldige Category, en er zijn geen regiovelden van een dongle of keychip.
    gs::Dirs real{std::string(RAVENGUN_SOURCE_DIR) + "/patches", {}};
    for (const auto& name : gs::list_games(real)) {
        gs::Game rg = gs::load_game(real, name);
        CHECK(rg.patch_errors.empty());
        for (const auto& p : rg.patches) CHECK(!p.category().empty());
        CHECK(!rg.settings.has("Game", "KeychipRegion") && !rg.settings.has("Game", "DongleEmulation"));
    }
    gs::Game lgi = gs::load_game(real, "LGI");
    CHECK(lgi.get("Game", "EmulationProfile") == "SegaJvsLetsGoIsland" && !lgi.get("Game", "DongleRegion").empty() && !lgi.get("Game", "PcbRegion").empty());
    gs::Game rr = gs::load_game(real, "RaccoonRampage");
    CHECK(!rr.get("Display", "DisplayMode").empty());
    CHECK(!gs::load_game(real, "Halo").get("Display", "CustomResolution").empty());
    // De veertien Type X2-geheugenpatches uit OpenParrot: alleen documentatie (Enabled = false, AppliedBy = core).
    // Waar de Find op onze eigen dump is gemeten is hij precies zo lang als de patch schrijft; de vier zonder Find
    // zijn de 2101J-set (onze dump is de USA 1.00-build: CRC 0x5a7fa241 -> GameID::HauntedMuseum2100) en de
    // crash-NOP van 0x1391, die in onze dump al op 0x90 staat. Zelfde afspraak als tests/test_gamesettings.py.
    int memory = 0;
    std::vector<std::string> zonder_find;
    for (const char* name : {"HauntedMuseum", "HauntedMuseumII", "GaiaAttack4", "MusicGunGun2"})
        for (const auto& p : gs::load_game(real, name).patches)
            if (p.kind() == "memory") {
                ++memory;
                CHECK(!p.enabled() && p.applied_by() == "core" && p.get("Source").find("OpenParrot TypeX2Generic.cpp (GPL-3.0), regel ") == 0);
                CHECK(!p.get("Module").empty() && !p.get("Offset").empty());
                if (p.get("Find").empty()) zonder_find.push_back(p.name);
                else CHECK(words(p.get("Find")) == static_cast<size_t>(std::atoi(p.get("Size").c_str())));
            }
    CHECK(memory == 14);
    std::sort(zonder_find.begin(), zonder_find.end());
    CHECK(zonder_find == std::vector<std::string>({"Crash-NOP, versie 2100 (0x1391)",
                                                   "Firmware-foutmelding uit, versie 2101J (0x1105)",
                                                   "Gun-schrijfacties uit, versie 2101J (0x9B920)",
                                                   "Volumeschrijfactie uit, versie 2101J (0x9DA08)"}));
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
    put(d.shipped + "/G/patches.ini", "[Patch: P]\nEnabled = false\nKind = file\nCategory = paths\n");
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
        CHECK(st.empty() || st == "working" || st == "playable" || st == "parked" || st == "untested");
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

// Het eigen profiel in game.ini: ConfigValues (volgorde, types, keuzelijsten, lege waarden, laag per sleutel), [Identify] en het afleiden van het
// pad uit views en gamemappen (zelfde regels als scripts/ownprofile.py, tests/test_ownprofile.py).
static void test_own_profile(const std::string& root) {
    const std::string sh = root + "/own-shipped", us = root + "/own-user";
    fs::remove_all(sh);
    fs::remove_all(us);
    put(sh + "/A/game.ini",
        "[Game]\nName = Spel A\nExecutable = game.exe\nProfileRevision = 7\nType = OpenParrot\n"
        "[Identify]\nPath = bin/game.exe\nSize = 3\n"
        "[Config]\nGeneral/Windowed = 1\nGeneral/Keyboard/Button Axis = 5\n"
        "[ConfigType]\nGeneral/Input API = Dropdown\nGeneral/Windowed = Bool\nGeneral/Keyboard/Button Axis = Slider\nGeneral/Nickname = Text\n"
        "[ConfigOptions]\nGeneral/Input API = DirectInput|RawInput\n");
    put(us + "/A/game.ini", "[Config]\nGeneral/Windowed = 0\nGeneral/Nickname = Jan\nGeneral/Keyboard/Button Axis =\n");
    gs::Dirs d{sh, us};
    gs::Game g = gs::load_game(d, "A");
    CHECK(gs::has_own_profile(g.settings));
    auto fields = gs::config_fields(g.settings);
    CHECK(fields.size() == 4);
    if (fields.size() == 4) {
        CHECK(fields[0].key() == "General/Input API" && fields[0].type == "Dropdown" && fields[0].value.empty());
        CHECK(fields[0].options.size() == 2 && fields[0].options[1] == "RawInput");
        CHECK(fields[1].value == "0");                       // de gebruikerslaag wint per sleutel
        CHECK(fields[2].category == "General" && fields[2].name == "Keyboard/Button Axis");   // gesplitst bij de eerste '/'
        CHECK(fields[2].value.empty());                      // een lege waarde in de gebruikerslaag heft de meegeleverde op
        CHECK(fields[3].value == "Jan");
    }
    CHECK(gs::find_config_key(g.settings, "Windowed") == "General/Windowed");
    CHECK(gs::find_config_key(g.settings, "Windowed", "Nope").empty());
    CHECK(!gs::has_own_profile(gs::load_game(d, "Onbekend").settings));
    // De tabellen: profielvelden en omgevingsvariabelen.
    const gs::ProfileField* f = gs::profile_field_for_xml("GameProfileRevision");
    CHECK(f && std::string(f->section) == "Game" && std::string(f->key) == "ProfileRevision");
    CHECK(gs::profile_field_for_xml("JoystickButtons") == nullptr);
    const gs::EnvMapEntry* e = gs::env_for_var("TP_GAMESCOPE");
    CHECK(e && std::string(e->section) == "Display" && std::string(e->kind) == "bool1");
    CHECK(gs::env_for_var("FOO") == nullptr);
    // Het pad afleiden: gamemap, view, override.
    const std::string games = root + "/own-games", views = root + "/own-views";
    fs::remove_all(games);
    fs::remove_all(views);
    put(games + "/Spel A/bin/game.exe", "abc");
    put(games + "/Ander/bin/game.exe", "abcdef");          // andere grootte: niet deze game
    gs::Identify id = gs::identify_of(g.settings);
    CHECK(id.path == "bin/game.exe" && id.size == 3 && id.sha256.empty() && id.folder.empty());
    std::string how;
    CHECK(gs::locate_game("A", g.settings, {games}, views, "/home/x", &how) == games + "/Spel A/bin/game.exe" && how == "folders");
    CHECK(gs::locate_game("A", g.settings, {}, views, "/home/x", &how).empty() && how.empty());
    put(views + "/A/bin/game.exe", "abc");
    CHECK(gs::locate_game("A", g.settings, {games}, views, "/home/x", &how) == views + "/A/bin/game.exe" && how == "view");
    // sha256: met een hashfunctie gecontroleerd, zonder (de engine) genegeerd; Folder = de mapnaam.
    gs::Ini s = g.settings;
    s.set("Identify", "Sha256", "ABCDEF");
    auto wrong = [](const std::string&) { return std::string("000"); };
    auto right = [](const std::string&) { return std::string("abcdef"); };
    CHECK(gs::locate_game("A", s, {games}, "", "", &how, wrong).empty());
    CHECK(gs::locate_game("A", s, {games}, "", "", &how, right) == games + "/Spel A/bin/game.exe");
    CHECK(!gs::locate_game("A", s, {games}, "", "", &how).empty());
    s.set("Identify", "Folder", "spel a");
    CHECK(gs::locate_game("A", s, {games}, "", "", &how, right) == games + "/Spel A/bin/game.exe");
    s.set("Identify", "Folder", "Ander");
    CHECK(gs::locate_game("A", s, {games}, "", "", &how, right).empty());
    s.set("Game", "GamePath", "~/spel/game.exe");
    CHECK(gs::locate_game("A", s, {games}, views, "/home/x", &how) == "/home/x/spel/game.exe" && how == "override");
    // De gebruiker schrijft een waarde: alleen het verschil gaat naar de gebruikerslaag (set/erase door de app).
    CHECK(gs::set_user_value(d, "A", "Config", "General/Input API", "RawInput"));
    CHECK(gs::load_game(d, "A").get("Config", "General/Input API") == "RawInput");
}

// De vaste categorieenlijst en de nieuwe categorieen (dongle, region, keychip, integrity, activation): hetzelfde bestand en dezelfde
// uitkomst als tests/test_gamesettings.py (tests/data/patch-categories.json), zodat Python en C++ niet uit elkaar lopen.
static void test_patch_categories(const std::string& root) {
    const std::string dir = std::string(RAVENGUN_SOURCE_DIR) + "/tests/data/";
    auto data = json::parse(slurp(dir + "patch-categories.json"));
    CHECK(data.ok && data.value.is_object());
    const auto* cats = data.value.find("categories");
    CHECK(cats && cats->is_array());
    std::string want;
    size_t n = 0;
    if (cats)
        for (const auto& c : cats->arr) {
            want += (want.empty() ? "" : ", ") + c.str;
            ++n;
        }
    CHECK(gs::patch_categories_text() == want);   // dezelfde lijst en dezelfde volgorde als Python
    size_t have = 0;
    for (const char* const* c = gs::kPatchCategories; *c; ++c) ++have;
    CHECK(have == n);
    const auto* cases = data.value.find("cases");
    CHECK(cases && cases->is_array());
    const std::string sh = root + "/cat-shipped";
    fs::remove_all(sh);
    put(sh + "/G/game.ini", "[Game]\nType = OpenParrot\n");
    for (const auto& c : cases->arr) {
        put(sh + "/_defaults/OpenParrot.patches.ini", c.find("ini")->str);
        gs::Game g = gs::load_game(gs::Dirs{sh, {}}, "G");
        const bool accepted = g.patch("Proef") != nullptr;
        CHECK(accepted == c.find("accepted")->b);
        if (accepted) {
            CHECK(g.patch("Proef")->category() == c.find("category")->str);
        } else {
            bool named = false;
            for (const auto& e : g.patch_errors) named = named || e.name == "Proef";
            CHECK(named);
            if (!c.find("category")->str.empty())
                CHECK(!g.patch_errors.empty() && g.patch_errors[0].message.find("onbekende Category") != std::string::npos);
        }
    }
}

int main() {
    char tmpl[] = "/tmp/rg-gamesettings-XXXXXX";
    const char* t = mkdtemp(tmpl);
    if (!t) { std::perror("mkdtemp"); return 2; }
    const std::string root = t;
    test_conformance();
    test_dongle();
    test_ini_basics();
    test_layers(root);
    test_patch_policy(root);
    test_patch_categories(root);
    test_edit_and_user_writes(root);
    test_default_dirs();
    test_repo_tree();
    test_own_profile(root);
    fs::remove_all(root);
    std::printf("gamesettings: %d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
