#pragma once
// Welke invoerroute geldt per game (packaging/io-routes.json). Routes:
//   evdev       zoals voorheen: de virtuele gun en het toetsenbord, TeknoParrotUi leest ze (geen brug in de daemon)
//   tp-shm      de daemon schrijft het gedeelde geheugen van de TeknoParrot-kern zelf (dialect tp-shm)
//   unity-tcp   de daemon is TCP-klant van de BepInEx-plugin in een Unity-game (dialect unity-tcp)
//   hid-board   de daemon maakt het invoerbord van de game na als virtuele joystick/HID (dialect hid-board, hid-boards.json)
//   jvs         de daemon is het JVS-bord aan het eind van een seriële lijn (pty als COM-poort in de Wine-prefix; dialect jvs, jvs-boards.json)
#include <string>
#include <vector>

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
    std::vector<std::string> boards;   // jvs: de borden van de keten in volgorde (leeg = alleen `board`)
    std::string com_port;              // jvs: de COM-poort die de game opent (bv. COM4); voor het startscript, niet voor de daemon
    std::string layout;   // tp-shm: naam van het profiel in tp-shm-profiles.json (de EmulationProfile)
    std::string note;
    // Toetsen die de daemon ook indrukt zolang de munt van een speler ingedrukt is (games die hun munt uit het toetsenbord lezen,
    // bv. Time Crisis 5: Shift+T). Namen uit ravengun/output/key_table.hpp.
    std::vector<std::string> coin_keys;
};

struct IoRoutes {
    std::string default_route = kRouteEvdev;
    std::vector<IoRoute> games;
    const IoRoute* find(const std::string& game) const;
    // De route van een game; onbekende games krijgen de standaardroute.
    std::string route_for(const std::string& game) const;
};

// Leest packaging/io-routes.json. Bij een fout is het resultaat leeg en staat er een Nederlandse melding in err.
IoRoutes parse_io_routes(const std::string& json_text, std::string& err);

}  // namespace ravengun::iobridge
