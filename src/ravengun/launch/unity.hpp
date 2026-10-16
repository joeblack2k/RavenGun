#pragma once
// De Unity-route zonder shellscripts (Tomb Raider Arcade, Rabbids Hollywood Arcade): de game draait direct onder GE-Proton met
// onze BepInEx-plugin, en de daemon is de TCP-klant van die plugin (route unity-tcp, AagamesService). Dit verving
// scripts/play-unity.sh (weg sinds 2026-10-16); tools/dev/aagames-setup.py blijft als benchgereedschap.
//
// Gelijk aan aagames-setup.py: de lokale view (~/.local/share/ravengun/runtime/views-aagames/<Game>) met symlinks naar de
// game op de NAS, behalve BepInEx/, winhttp.dll en doorstop_config.ini (ons artefact, lokaal), Game_Data/ als echte map met
// links, daarin Plugins/ en StreamingAssets/ als lokale kopie, en kleine configuratie- en lege vlagbestanden als lokale kopie.
// Daarna de controle dat geen van onze artefacten (de view, BepInEx/, Game_Data/Plugins) via een symlink op de NAS uitkomt:
// op de NAS wordt nooit geschreven. Anders dan het script: de plugin wordt hier niet gebouwd (ontbreekt of is hij verouderd,
// dan een melding met het bouwcommando), en een nieuwe prefix maakt Proton zelf bij de eerste start.
//
// Gelijk aan play-unity.sh: `proton run Game.exe -popupwindow` in de view als werkmap, met WINEDLLOVERRIDES=winhttp=n,b (de
// plugin laadt via doorstop) en de schermomgeving van de ingelogde sessie (Wayland, Xwayland met zijn XAUTHORITY van kwin).
#include <string>
#include <utility>
#include <vector>

namespace ravengun::launch {

struct UnityPaths {
    std::string json;                     // packaging/aagames-games.json
    std::string home;                     // voor ~ in nas_dir en de Steam-map
    std::string data;                     // ~/.local/share/ravengun (views-aagames, prefixes, bepinex, demulshooter)
    std::vector<std::string> plugin_dirs; // mappen met <Game>/RavenGun.<Game>.dll; de eerste die hem heeft wint
    std::string plugin_src;               // plugins/unity/src (de plugin is verouderd als een .cs nieuwer is); leeg = niet controleren
    std::string proton;                   // map van GE-Proton (met het programma `proton`)
};

struct UnityGame {
    std::string key, name, nas_dir, exe, plugin, plugin_dir, language;
    bool fullscreen = true;
    int width = 1920, height = 1080, port = 33610;
    std::string bepinex_version = "5.4.21.0", demulshooter_version;
};

// Staat `game` in aagames-games.json? (false bij elke fout)
bool unity_game_known(const std::string& json_text, const std::string& game);
// Leest de game met ~ ingevuld. `plugin_override` (leeg = geen) gaat voor het veld plugin; standaard "ravengun".
bool load_unity_game(const std::string& json_text, const std::string& game, const std::string& home, const std::string& plugin_override,
                     UnityGame& out, std::string& err);

// Ligt `path` (na het volgen van symlinks) in of onder `root`?
bool path_under(const std::string& path, const std::string& root);
// Onze artefacten in de view die op de NAS uitkomen (leeg = goed); zie pruef_schrijfpaden in aagames-setup.py.
std::vector<std::string> nas_write_errors(const std::string& view, const std::string& nas);

// Zet de view klaar. `view` = het pad; `log` krijgt een regel per opvallende stap. false + err (Nederlands) bij een fout.
bool build_view(const UnityGame& g, const UnityPaths& p, std::string& view, std::vector<std::string>& log, std::string& err);

struct UnityOptions {
    std::string runtime_dir;               // XDG_RUNTIME_DIR
    std::string wayland_display = "wayland-0";
    std::string display = ":0";
    std::string xauthority;                // leeg = niet meegeven
    std::string dbus;                      // leeg = unix:path=<runtime_dir>/bus
    bool popup = true;                     // -popupwindow (randloos schermvullend venster)
};

struct UnityPlan {
    std::vector<std::string> argv;                          // proton run Game.exe [-popupwindow]
    std::vector<std::pair<std::string, std::string>> env;   // voor de unit (systemd-run --setenv)
    std::string cwd;                                        // de view
    std::string prefix;                                     // STEAM_COMPAT_DATA_PATH
    std::string comm;                                       // naam van het spelproces (Wine: de exe-naam)
    std::vector<std::string> notes;                         // extra regels voor het logboek
};

bool plan_unity(const UnityGame& g, const UnityPaths& p, const std::string& view, const UnityOptions& o, UnityPlan& out, std::string& err);

// XAUTHORITY van Xwayland: het argument --xwayland-xauthority van kwin_wayland (leeg = niet gevonden).
std::string find_xwayland_xauthority(const std::string& proc_root = "/proc");

}  // namespace ravengun::launch
