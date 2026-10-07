#pragma once
// Eén opdrachtafhandeling voor alle beheerkanalen (IPC-socket en webinterface).
#include <string>

#include "ravengun/ipc_protocol.hpp"
#include "shared.hpp"

namespace ravengund {

// Voert een getypeerd commando uit (alles behalve Watch, dat per verbinding bestaat) en geeft het
// IPC-antwoord (ok_response of error_response, zonder '\n'). Mag tot 500 ms blokkeren op de
// HID-thread, nooit langer.
std::string execute_command(SharedState& sh, const ravengun::ipc::Request& r);

// Parseert een IPC-verzoekregel en voert die uit; een foutief verzoek geeft een error_response.
std::string execute_line(SharedState& sh, const std::string& line);

}  // namespace ravengund
