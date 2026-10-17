#pragma once
// Klaar om te spelen? De poort vóór elke start van een lightgun-game (docs/prelaunch.md).
//
// Eén plek voor de beslissing, voor elke startweg: de daemon (GameService) wacht hiermee vóór elke start (dubbelklik in de app,
// `ravengunctl game start`, snelkoppelingen), en het startscherm van de app (`ravengun --play PROFIEL`) vraagt dezelfde beslissing
// op via IPC `game_ready` en toont "Please turn on your PS Move controller" en de kalibratie per speler. Zuiver: geen I/O, geen
// klok (de tijd komt als argument binnen), zodat tests elke beslissing nagaan.
//
// - Soort game: een lightgun-game heeft in de indeling van zijn invoerroute aim-assen pN.x en pN.y (tp-shm-profiles.json,
//   jvs-boards.json, hid-boards.json), of gaat via de virtuele gun (evdev) of de Unity-plugin. Een game zonder aim-paar (Mario Kart:
//   stuur, gas, rem) is een pad-game: geen wachten op een Move en geen kalibratie. [Input] GunPlayers = 0..4 in game.ini gaat voor.
// - Spelers: speler 1 is verplicht. Heeft hij een Move (config.json, het spelernummer van een niet-verwijderde Move), dan wacht de
//   start tot die Move aan is, of tot een muis als lightgun van speler 1 verbonden is (de muismodus van de bench). Zonder Move van
//   speler 1 geldt het oude gedrag: een willekeurige Move, muis of gamepad van een speler is genoeg.
//   Speler 2..N (N = guns in de game) wacht niet: een toegewezen Move die uit staat staat in `optional_off` (het startscherm geeft
//   hem even de kans om mee te doen); wie aan staat, komt in `calibrate`. Zo blijft een twee-spelergame alleen te spelen.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ravengun/iobridge/hid_board.hpp"
#include "ravengun/iobridge/jvs.hpp"
#include "ravengun/iobridge/routes.hpp"
#include "ravengun/iobridge/tp_shm.hpp"

namespace ravengun::games::readiness {

inline constexpr int kPlayers = 4;
inline constexpr int64_t kMoveWaitMs = 120000;   // zo lang wacht de daemon op een Move (daarna "mislukt" met een melding)

enum class Kind { Lightgun, Pad };

struct GameKind {
    Kind kind = Kind::Lightgun;
    int gun_players = kPlayers;   // spelers met een eigen gun (1..4); 0 bij een pad-game
    std::string source;           // waar dat uit volgt (Nederlands, voor logboek en status)
};

// Wat de invoerroute van een game over zijn assen zegt.
struct InputInfo {
    bool has_input = false;                 // [Input] in game.ini (een geldige route)
    std::string route;                      // evdev | tp-shm | unity-tcp | hid-board | jvs
    std::vector<std::string> axis_signals;  // de as-signalen van de indeling en de borden van die route
    bool layout_known = true;               // false: een indeling of bord ontbreekt in de packaging-bestanden
    std::string gun_players_override;       // [Input] GunPlayers (0..4); leeg = afleiden
};

// Het hoogste N waarvoor zowel pN.x als pN.y een as is (0 = geen aim-paar).
int gun_players_of(const std::vector<std::string>& axis_signals);
// De assen van de route: de tp-shm-indeling (Layout, ook bij jvs), de JVS-borden of de hid-borden van [Input].
InputInfo input_info(const ravengun::iobridge::IoRoute* route, const ravengun::iobridge::TpShmProfiles& profiles,
                     const ravengun::iobridge::HidBoards& hid_boards, const ravengun::iobridge::JvsBoards& jvs_boards,
                     const std::string& gun_players_override = {});
GameKind classify(const InputInfo& in);

struct PlayerInput {
    bool move_assigned = false;    // een niet-verwijderde Move heeft dit spelernummer
    bool move_connected = false;   // ... en die is nu verbonden
    bool mouse_assigned = false;   // een muis als lightgun van deze speler
    bool mouse_connected = false;  // ... en die is geopend
};

struct Inputs {
    std::array<PlayerInput, kPlayers> players{};
    bool any_move_connected = false;   // een verbonden Move, ook zonder (bekend) spelernummer
    bool any_pad_connected = false;    // een verbonden gamepad van een speler
};

struct Decision {
    GameKind game;
    bool ready = true;
    std::vector<int> waiting;         // spelers op wiens Move de start wacht (nu alleen speler 1)
    bool waiting_any = false;         // speler 1 heeft geen Move: wacht op een willekeurige Move, muis of gamepad
    std::vector<int> optional_off;    // speler 2..N met een Move die uit staat (wacht niet; kan nog meedoen)
    std::vector<int> calibrate;       // spelers 1..N met een verbonden Move, oplopend (de kalibratie van het startscherm)
};

Decision decide(const GameKind& g, const Inputs& in);

enum class WaitStep { Ready, Waiting, TimedOut };
// Eén stap van het wachten: klaar, nog wachten, of de tijd is om (limit_ms na started_ms; <= 0 = nooit).
WaitStep wait_step(const Decision& d, int64_t started_ms, int64_t now_ms, int64_t limit_ms = kMoveWaitMs);

// {"lightgun":..,"gun_players":..,"source":..,"ready":..,"waiting":[..],"waiting_any":..,"optional_off":[..],"calibrate":[..],
//  "players":[{"player","move_assigned","move_connected","mouse"}...]} (IPC game_ready).
std::string decision_json(const Decision& d, const Inputs& in);
// Nederlands, voor de status van de daemon en het logboek: "Zet de PS Move van speler 1 aan." Leeg als er niets te wachten is.
std::string waiting_text(const Decision& d);

}  // namespace ravengun::games::readiness
