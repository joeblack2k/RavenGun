// Tests voor de Unity-route zonder shellscripts (src/ravengun/launch/unity.*): de game lezen uit aagames-games.json, de view
// (symlinks naar de nep-NAS, lokale kopieen, BepInEx en onze plugin), de controle dat niets van ons op de NAS uitkomt, en het
// Proton-commando. Gebouwd met tijdelijke mappen; start niets.
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ravengun/launch/unity.hpp"

namespace fs = std::filesystem;
using namespace ravengun::launch;

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

static void write(const fs::path& p, const std::string& text, bool exec = false) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
    if (exec) ::chmod(p.c_str(), 0755);
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
static bool is_link(const fs::path& p) { return fs::is_symlink(fs::symlink_status(p)); }
static bool has_env(const UnityPlan& pl, const std::string& k, const std::string& v) {
    return std::find(pl.env.begin(), pl.env.end(), std::make_pair(k, v)) != pl.env.end();
}

int main() {
    char tmpl[] = "/tmp/rg-unity-XXXXXX";
    const char* t = mkdtemp(tmpl);
    if (!t) { std::perror("mkdtemp"); return 2; }
    const fs::path root = t;
    const fs::path home = root / "home";
    const fs::path nas = home / "nas" / "Rabbids Hollywood ";   // met spatie aan het eind, zoals op de echte NAS

    const std::string json = R"({
  "schema": 1, "port": 33611, "demulshooter_version": "v17.9", "bepinex_version": "5.4.21.0",
  "games": {
    "RabbidsHollywood": {"name": "Rabbids Hollywood Arcade", "nas_dir": "~/nas/Rabbids Hollywood ", "exe": "Game.exe",
                         "plugin": "ravengun", "plugin_dir": "RabbidsHollywood", "fullscreen": true, "language": "EN",
                         "width": 1280, "height": 720},
    "Kapot": {"nas_dir": 5}
  }
})";

    // Lezen.
    CHECK(unity_game_known(json, "RabbidsHollywood"));
    CHECK(!unity_game_known(json, "TombRaider"));
    CHECK(!unity_game_known("{kapot", "RabbidsHollywood"));
    UnityGame g;
    std::string err;
    CHECK(load_unity_game(json, "RabbidsHollywood", home.string(), "", g, err));
    CHECK(g.nas_dir == nas.string());
    CHECK(g.exe == "Game.exe" && g.plugin == "ravengun" && g.width == 1280 && g.height == 720 && g.port == 33611);
    CHECK(g.name == "Rabbids Hollywood Arcade" && g.language == "EN" && g.bepinex_version == "5.4.21.0");
    UnityGame g2;
    CHECK(load_unity_game(json, "RabbidsHollywood", home.string(), "demulshooter", g2, err) && g2.plugin == "demulshooter");
    CHECK(!load_unity_game(json, "RabbidsHollywood", home.string(), "iets", g2, err));
    CHECK(!load_unity_game(json, "Kapot", home.string(), "", g2, err) && err.find("nas_dir") != std::string::npos);
    CHECK(!load_unity_game(json, "Onbekend", home.string(), "", g2, err));

    // De nep-NAS: de game, een log, kleine en grote configuratie, een TeknoParrot-onderdeel, Game_Data met Plugins en assets.
    write(nas / "Game.exe", "MZ");
    write(nas / "UnityPlayer.dll", std::string(70 * 1024, 'x'));
    write(nas / "Gun_old.log", "oud");
    write(nas / "ControllerBaseConfig.json", "{}");
    write(nas / "Groot.json", std::string(70 * 1024, ' '));
    write(nas / "ForceReboot", "");
    write(nas / "teknoparrot.ini", "[x]");
    write(nas / "winhttp.dll", "tp-doorstop");
    write(nas / "Game_Data" / "boot.config", "a=1");
    write(nas / "Game_Data" / "level0", "groot");
    write(nas / "Game_Data" / "Plugins" / "matrix64.dll", "dll");
    write(nas / "Game_Data" / "Plugins" / "sub" / "x.dat", "x");
    write(nas / "Game_Data" / "StreamingAssets" / "a.json", "{}");
    write(nas / "Game_Data" / "Managed" / "Assembly-CSharp.dll", "managed");

    // BepInEx, onze plugin en Proton.
    const fs::path data = home / ".local/share/ravengun";
    const fs::path bep = data / "bepinex/5.4.21.0/x64";
    write(bep / "BepInEx/core/BepInEx.dll", "core");
    write(bep / "winhttp.dll", "bep-doorstop");
    write(bep / "doorstop_config.ini", "[General]\nenabled=true\n");
    const fs::path plugins = root / "build/unity-plugin";
    UnityPaths p;
    p.json = (root / "aagames.json").string();
    p.home = home.string();
    p.data = data.string();
    p.plugin_dirs = {(root / "leeg").string(), plugins.string()};
    p.proton = (root / "proton").string();

    // Zonder plugin: een melding met het bouwcommando.
    std::string view;
    std::vector<std::string> log;
    CHECK(!build_view(g, p, view, log, err));
    CHECK(err.find("build-unity-plugin.sh RabbidsHollywood") != std::string::npos);

    write(plugins / "RabbidsHollywood/RavenGun.RabbidsHollywood.dll", "plugin");
    log.clear();
    err.clear();
    CHECK(build_view(g, p, view, log, err));
    if (!err.empty()) std::fprintf(stderr, "build_view: %s\n", err.c_str());
    const fs::path v = view;
    CHECK(v == data / "runtime/views-aagames/RabbidsHollywood");
    // Groot en alleen-lezen: symlink naar de NAS. Klein of leeg: lokale kopie. Logs en TeknoParrot-onderdelen niet.
    CHECK(is_link(v / "Game.exe") && fs::read_symlink(v / "Game.exe") == nas / "Game.exe");
    CHECK(is_link(v / "UnityPlayer.dll"));
    CHECK(!is_link(v / "ControllerBaseConfig.json") && fs::is_regular_file(v / "ControllerBaseConfig.json"));
    CHECK(is_link(v / "Groot.json"));
    CHECK(!is_link(v / "ForceReboot") && fs::is_regular_file(v / "ForceReboot"));
    CHECK(!fs::exists(fs::symlink_status(v / "Gun_old.log")));
    CHECK(!fs::exists(fs::symlink_status(v / "teknoparrot.ini")));
    // Game_Data: echte map; Plugins en StreamingAssets lokaal, de rest symlink, kleine configuratie lokaal.
    CHECK(!is_link(v / "Game_Data") && fs::is_directory(v / "Game_Data"));
    CHECK(!is_link(v / "Game_Data/Plugins") && fs::is_regular_file(v / "Game_Data/Plugins/matrix64.dll") &&
          !is_link(v / "Game_Data/Plugins/matrix64.dll"));
    CHECK(fs::is_regular_file(v / "Game_Data/Plugins/sub/x.dat") && !is_link(v / "Game_Data/Plugins/sub/x.dat"));
    CHECK(!is_link(v / "Game_Data/StreamingAssets/a.json"));
    CHECK(is_link(v / "Game_Data/level0") && is_link(v / "Game_Data/Managed"));
    CHECK(!is_link(v / "Game_Data/boot.config"));
    // BepInEx en onze plugin: lokaal, uit de officiele release (niet de doorstop van de NAS).
    CHECK(slurp(v / "winhttp.dll") == "bep-doorstop" && !is_link(v / "winhttp.dll"));
    CHECK(slurp(v / "BepInEx/core/BepInEx.dll") == "core");
    CHECK(slurp(v / "BepInEx/plugins/RavenGun.RabbidsHollywood.dll") == "plugin");
    CHECK(slurp(v / "BepInEx/config/BepInEx.cfg").find("Enabled = true") != std::string::npos);
    const std::string cfg = slurp(v / "BepInEx/config/nl.studiodamon.ravengun.rabbidshollywood.cfg");
    CHECK(cfg.find("Poort = 33611") != std::string::npos && cfg.find("Breedte = 1280") != std::string::npos &&
          cfg.find("Hoogte = 720") != std::string::npos && cfg.find("Beeldvullend = true") != std::string::npos);
    CHECK(nas_write_errors(view, nas.string()).empty());

    // Opnieuw: wat de game lokaal schreef blijft staan, de eigen configuratie van de eigenaar ook, een oude DemulShooter-plugin gaat weg,
    // het logboek van BepInEx gaat van false naar true.
    write(v / "ControllerBaseConfig.json", "{\"eigen\":1}");
    write(v / "BepInEx/config/nl.studiodamon.ravengun.rabbidshollywood.cfg", "eigen");
    write(v / "BepInEx/plugins/DemulShooter_Plugin.dll", "ds");
    write(v / "BepInEx/config/BepInEx.cfg", "[Logging.Console]\n\nEnabled = false\n\n[Logging.Disk]\n\n## uitleg\nEnabled = false\n");
    // Een oude symlink naar de NAS in de lokale Plugins-map wordt een kopie.
    fs::remove(v / "Game_Data/Plugins/matrix64.dll");
    fs::create_symlink(nas / "Game_Data/Plugins/matrix64.dll", v / "Game_Data/Plugins/matrix64.dll");
    log.clear();
    CHECK(build_view(g, p, view, log, err));
    CHECK(slurp(v / "ControllerBaseConfig.json") == "{\"eigen\":1}");
    CHECK(slurp(v / "BepInEx/config/nl.studiodamon.ravengun.rabbidshollywood.cfg") == "eigen");
    CHECK(!fs::exists(v / "BepInEx/plugins/DemulShooter_Plugin.dll"));
    CHECK(slurp(v / "BepInEx/config/BepInEx.cfg") ==
          "[Logging.Console]\n\nEnabled = false\n\n[Logging.Disk]\n\n## uitleg\nEnabled = true\n");
    CHECK(!is_link(v / "Game_Data/Plugins/matrix64.dll") && slurp(v / "Game_Data/Plugins/matrix64.dll") == "dll");

    // De controle: BepInEx/ of Game_Data/Plugins via een symlink op de NAS is altijd fout.
    fs::rename(v / "BepInEx", root / "bep-weg");
    fs::create_directories(nas / "BepInEx");
    fs::create_symlink(nas / "BepInEx", v / "BepInEx");
    CHECK(!nas_write_errors(view, nas.string()).empty());
    fs::remove(v / "BepInEx");
    fs::rename(root / "bep-weg", v / "BepInEx");
    CHECK(nas_write_errors(view, nas.string()).empty());
    CHECK(path_under((nas / "x").string(), nas.string()) && path_under(nas.string(), nas.string()));
    CHECK(!path_under((nas.string() + "2/x"), nas.string()));

    // Verouderde plugin: een .cs-bestand nieuwer dan de DLL.
    UnityPaths ps = p;
    ps.plugin_src = (root / "src").string();
    write(root / "src/Plugin.cs", "class A {}");
    fs::last_write_time(plugins / "RabbidsHollywood/RavenGun.RabbidsHollywood.dll", fs::last_write_time(root / "src/Plugin.cs") - std::chrono::hours(1));
    CHECK(!build_view(g, ps, view, log, err) && err.find("verouderd") != std::string::npos);

    // Het commando.
    UnityPlan pl;
    CHECK(!plan_unity(g, p, view, {}, pl, err) && err.find("GE-Proton") != std::string::npos);
    write(root / "proton/proton", "#!/bin/sh\n", true);
    UnityOptions o;
    o.runtime_dir = "/run/user/1000";
    o.xauthority = "/run/user/1000/xauth_x";
    CHECK(plan_unity(g, p, view, o, pl, err));
    CHECK((pl.argv == std::vector<std::string>{(root / "proton/proton").string(), "run", "Game.exe", "-popupwindow"}));
    CHECK(pl.cwd == view && pl.comm == "Game.exe" && pl.prefix == (data / "prefixes/unity-aagames").string());
    CHECK(has_env(pl, "WINEDLLOVERRIDES", "winhttp=n,b"));
    CHECK(has_env(pl, "STEAM_COMPAT_DATA_PATH", pl.prefix));
    CHECK(has_env(pl, "STEAM_COMPAT_CLIENT_INSTALL_PATH", (home / ".local/share/Steam").string()));
    CHECK(has_env(pl, "DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/1000/bus"));
    CHECK(has_env(pl, "XAUTHORITY", "/run/user/1000/xauth_x") && has_env(pl, "WAYLAND_DISPLAY", "wayland-0") && has_env(pl, "DISPLAY", ":0"));
    CHECK(!pl.notes.empty());   // nog geen prefix
    o.popup = false;
    o.xauthority.clear();
    CHECK(plan_unity(g, p, view, o, pl, err) && pl.argv.size() == 3);
    CHECK(std::none_of(pl.env.begin(), pl.env.end(), [](const auto& kv) { return kv.first == "XAUTHORITY"; }));

    // XAUTHORITY uit de opdrachtregel van kwin_wayland (een nep-/proc).
    const fs::path proc = root / "proc";
    write(proc / "12/comm", "bash\n");
    write(proc / "34/comm", "kwin_wayland\n");
    {
        std::ofstream f(proc / "34/cmdline", std::ios::binary);
        const char cmd[] = "kwin_wayland\0--xwayland\0--xwayland-xauthority\0/run/user/1000/xauth_ABC\0";
        f.write(cmd, sizeof cmd - 1);
    }
    CHECK(find_xwayland_xauthority(proc.string()) == "/run/user/1000/xauth_ABC");
    CHECK(find_xwayland_xauthority((root / "geen").string()).empty());

    fs::remove_all(root);
    std::printf("unity launch: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
