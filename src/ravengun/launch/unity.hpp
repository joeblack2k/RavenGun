#pragma once
// De Unity-route zonder shellscripts (Tomb Raider Arcade, Rabbids Hollywood Arcade): de game draait direct onder GE-Proton (ons
// onderdeel, niet die van TeknoParrotUi; default_unity_proton) met
// onze BepInEx-plugin, en de daemon is de TCP-klant van die plugin (route unity-tcp, AagamesService). Dit verving
// scripts/play-unity.sh (weg sinds 2026-10-16); tools/dev/aagames-setup.py blijft als benchgereedschap.
//
// Het profiel van een game is patches/<Game>/unity.json (docs/per-game-patches.md): de gebruikerslaag eerst, en dat bestand vervangt het
// meegeleverde als geheel; kapot = een foutmelding bij de start, geen stille terugval. De map van de game op de NAS staat er NIET in als
// pad (dat is per machine anders) maar als mapnaam ("folder"), en wordt gezocht in de gamemappen van library.json. Een eigen pad ("nas_dir") hoort alleen
// in de gebruikerslaag en gaat dan voor. De gedeelde instellingen (poort, versies) staan in packaging/unity-route.json.
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
    std::string route_json;               // packaging/unity-route.json: poort en versies voor alle Unity-games; leeg of onleesbaar = standaardwaarden
    std::string patches;                  // patches/ (meegeleverd): <Game>/unity.json
    std::string user_patches;             // gebruikerslaag van patches/ (leeg = geen)
    std::string home;                     // voor ~ in nas_dir en de Steam-map
    std::string data;                     // ~/.local/share/ravengun (views-aagames, prefixes, bepinex, demulshooter)
    std::vector<std::string> plugin_dirs; // mappen met <Game>/RavenGun.<Game>.dll; de eerste die hem heeft wint
    std::string plugin_src;               // plugins/unity/src (de plugin is verouderd als een .cs nieuwer is); leeg = niet controleren
    std::string proton;                   // map van GE-Proton (met het programma `proton`); zie default_unity_proton
};

// De Proton van de Unity-route: GE-Proton 11-7, het onderdeel "ge-proton" van packaging/components.json (vastgepind op de release van
// GloriousEggroll met sha256), uitgepakt in <data>/proton/GE-Proton11-7-x86_64. Tot 2026-10-17 kwam hij uit
// ~/.local/share/TeknoParrotUI/proton (de download van TeknoParrotUi); dat is geen bron meer. Waarom GE-Proton en niet onze
// Kron4ek-Wine: de route draait `proton run` (prefix, DXVK voor Unity/D3D11) en is alleen zo op de tv gemeten (docs/decisions.md).
inline constexpr const char* kUnityProtonName = "GE-Proton11-7-x86_64";
// De eerste die bestaat (met een uitvoerbaar `proton`): <data>/proton/<kUnityProtonName>, dan Steam
// (<home>/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64 of GE-Proton11-7). Geen = het pad in <data> (de foutmelding
// noemt dan het onderdeel). Nooit de map van TeknoParrot(Ui).
std::string default_unity_proton(const std::string& data, const std::string& home);

// De instellingen die voor alle Unity-games gelden (packaging/unity-route.json).
struct UnityRoute {
    int port = 33610;                           // TCP-poort van de plugin in de game (127.0.0.1)
    std::string bepinex_version = "5.4.21.0";
    std::string demulshooter_version = "v17.9"; // alleen voor de terugval plugin = "demulshooter"
};

struct UnityGame {
    std::string key, name, folder, nas_dir, exe, plugin, plugin_dir, language;   // nas_dir: de gevonden (of door de gebruiker gegeven) map
    std::string source;                   // het unity.json waaruit dit komt
    bool fullscreen = true;
    int width = 1920, height = 1080, port = 33610;
    std::string bepinex_version = "5.4.21.0", demulshooter_version;
};

// Leest packaging/unity-route.json. Lege tekst = de standaardwaarden; een kapot bestand = false + err (Nederlands).
bool parse_unity_route(const std::string& json_text, UnityRoute& out, std::string& err);
// Dat bestand via UnityPaths::route_json (leeg pad of ontbrekend bestand = standaardwaarden).
bool load_unity_route(const UnityPaths& p, UnityRoute& out, std::string& err);

// Het unity.json van een game: patches/<Game>/unity.json, de gebruikerslaag eerst, dan het meegeleverde (leeg = geen, ook bij een
// ongeldige naam). Een bestand dat bestaat telt, ook als het kapot is (de start meldt dan wat er mis is).
std::string unity_file(const UnityPaths& p, const std::string& game);
// De games met een unity.json (beide lagen, gesorteerd, zonder dubbele).
std::vector<std::string> unity_games(const UnityPaths& p);
// Heeft `game` een unity.json? (dan gaat hij over de Unity-route)
bool unity_game_known(const UnityPaths& p, const std::string& game);
// De tekst van dat bestand (leeg bij geen bestand of niet te lezen).
std::string unity_text(const UnityPaths& p, const std::string& game);

// De map van een game op de NAS: `folder` is een mapnaam in de gamemappen van library.json (~ al ingevuld), met dezelfde regel als
// "@nas/<Map>" van de native route: <gamemap>/<folder>, anders een niveau dieper <gamemap>/*/<folder>; de naam moet exact kloppen.
// Leeg = niet gevonden. De daemon roept dit bij elke start aan met de lijst van dat moment (een net toegevoegde gamemap telt mee).
std::string resolve_unity_dir(const std::string& folder, const std::vector<std::string>& game_folders);

// Leest de game uit zijn unity.json (de gebruikerslaag eerst) en de gedeelde instellingen, met de map op de NAS ingevuld uit
// `game_folders` (of de nas_dir van de gebruiker, ~ ingevuld). `plugin_override` (leeg = geen) gaat voor het veld plugin; standaard "ravengun".
bool load_unity_game(const UnityPaths& p, const std::vector<std::string>& game_folders, const std::string& game, const std::string& plugin_override,
                     UnityGame& out, std::string& err);
// Hetzelfde met de tekst van het unity.json al in de hand (`source` = de naam van het bestand, voor de meldingen).
bool parse_unity_game(const std::string& json_text, const std::string& source, const std::string& game, const UnityRoute& route, const std::string& home,
                      const std::vector<std::string>& game_folders, const std::string& plugin_override, UnityGame& out, std::string& err);

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
