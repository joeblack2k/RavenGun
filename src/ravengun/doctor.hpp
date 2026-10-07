#pragma once
#include <vector>

#include "ravengun/model.hpp"

namespace ravengun {

// Zuivere logica: beoordeelt een Snapshot. Geen I/O.
std::vector<Check> evaluate(const Snapshot& snap);

// Ernstigste uitkomst als exitcode: Fail -> 2, Warn -> 1, anders 0. Info telt niet mee.
int exit_code_for(const std::vector<Check>& checks);

// Bruikbare Move-apparaten in de snapshot (hidraw met Sony Move-ID).
std::vector<HidDevice> find_moves(const Snapshot& snap);

}  // namespace ravengun
