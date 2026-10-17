#pragma once
// Eén opdrachtafhandeling voor alle beheerkanalen (IPC-socket en webinterface).
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "ravengun/ipc_protocol.hpp"
#include "shared.hpp"

namespace ravengund {

// Voert een getypeerd commando uit (alles behalve Watch, dat per verbinding bestaat) en geeft het
// IPC-antwoord (ok_response of error_response, zonder '\n'). Mag tot 500 ms blokkeren op de
// HID-thread, nooit langer.
std::string execute_command(SharedState& sh, const ravengun::ipc::Request& r);

// Per-game knopkeuzes ([Controls] in game.ini): legt per speler (Move-knop, actie)-paren over de globale knoppenindeling heen en
// past ze live toe. Een lege lijst zet de globale indeling terug; zonder keuzes nu en eerder gebeurt er niets.
void apply_game_controls(SharedState& sh, const std::array<std::vector<std::pair<std::string, std::string>>, kPlayers>& per_player);

// Parseert een IPC-verzoekregel en voert die uit; een foutief verzoek geeft een error_response.
std::string execute_line(SharedState& sh, const std::string& line);

}  // namespace ravengund
