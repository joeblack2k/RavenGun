#pragma once
// Welke invoerroute geldt per game ([Input] in patches/<Game>/game.ini). Routes:
//   evdev       alleen de virtuele gun en het toetsenbord (geen brug in de daemon); de game leest ze zelf
//   tp-shm      de daemon schrijft het gedeelde geheugen van de TeknoParrot-kern zelf (dialect tp-shm)
//   unity-tcp   de daemon is TCP-klant van de BepInEx-plugin in een Unity-game (dialect unity-tcp)
//   hid-board   de daemon maakt het invoerbord van de game na als virtuele joystick/HID (dialect hid-board, hid-boards.json)
//   jvs         de daemon is het JVS-bord aan het eind van een seriële lijn (pty als COM-poort in de Wine-prefix; dialect jvs, jvs-boards.json)
#include <string>
#include <vector>

#include "ravengun/gamesettings/gamesettings.hpp"

namespace ravengun::iobridge {

inline constexpr const char* kRouteEvdev = "evdev";
inline constexpr const char* kRouteTpShm = "tp-shm";
inline constexpr const char* kRouteUnityTcp = "unity-tcp";
inline constexpr const char* kRouteHidBoard = "hid-board";
inline constexpr const char* kRouteJvs = "jvs";

bool valid_route(const std::string& r);

struct IoRoute {
    std::string game;     // TeknoParrot-profielnaam, bv. Halo
    std::string route;    // evdev | tp-shm | unity-tcp | hid-board | jvs
    std::string board;    // hid-board: naam van het bord in hid-boards.json; jvs: naam van het (enige) bord in jvs-boards.json
    std::vector<std::string> boards;   // jvs: de borden van de keten in volgorde; hid-board: meerdere borden naast
                                       // elkaar, elk met zijn eigen transport (bv. de gun via uinput naast het
                                       // IO-bord via uhid). Leeg = alleen `board`.
    std::string com_port;              // jvs: de COM-poort die de game opent (bv. COM4); voor de route, niet voor het bord van de daemon
    std::string layout;   // tp-shm: naam van het profiel in tp-shm-profiles.json (de EmulationProfile)
    // tp-shm: ook het rapport voor de besturingspipe TeknoParrotPipe (naam in control-pipe-profiles.json, bv. AllsUsbIo); leeg = geen.
    // Mag ook bij evdev staan (de directe start maakt van evdev met een Layout tp-shm); bij andere routes is het een fout.
    std::string control_pipe;
    std::string note;
    // Toetsen die de daemon ook indrukt zolang de munt van een speler ingedrukt is (games die hun munt uit het toetsenbord lezen,
    // bv. Time Crisis 5: Shift+T). Namen uit ravengun/output/key_table.hpp.
    std::vector<std::string> coin_keys;
    // Where the PS Move rumble comes from while this game runs. Empty = the trigger and pump (the owner's "rumble when
    // shooting"); "game" = the game's own outputs, read from the game-out file a hook writes (game_out.hpp; route tp-shm only).
    std::string rumble;
};

struct IoRoutes {
    std::string default_route = kRouteEvdev;
    std::vector<IoRoute> games;
    const IoRoute* find(const std::string& game) const;
    // De route van een game; onbekende games krijgen de standaardroute.
    std::string route_for(const std::string& game) const;
};

// Leest de invoerroute van alle games uit hun instellingen: [Input] in patches/<Game>/game.ini (met _defaults/<Type>.ini eronder en de
// gebruikerslaag eroverheen, ravengun/gamesettings). Sleutels: Route, Layout, ControlPipe, Board, Boards (komma's), ComPort, CoinKeys
// (komma's), Rumble, Note. Klopt de samenvoeging niet (bv. route tp-shm zonder Layout), dan telt de gebruikerslaag niet mee; klopt ook de meegeleverde laag
// niet, dan krijgt de game geen eigen route (evdev). De eerste melding staat in err (Nederlands), leeg als alles klopt.
// De brug leest dit bij elke start opnieuw, dus een keuze in de app geldt zonder herstart van de daemon.
IoRoutes load_io_routes(const ravengun::gamesettings::Dirs& dirs, std::string& err);
// Eén game; leeg resultaat (game.empty()) als er geen [Input] is. err als hierboven.
IoRoute io_route_from_settings(const ravengun::gamesettings::Game& g, std::string& err);

}  // namespace ravengun::iobridge
