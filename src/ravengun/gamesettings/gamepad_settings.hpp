// SPDX-License-Identifier: GPL-3.0-or-later
// De gamepad-indeling van een game: [Gamepad] in patches/<Game>/game.ini (docs/gamepad.md), per sleutel over de standaardindeling
// (output/gamepad.hpp) heen, met de gewone lagen (_defaults, meegeleverd, gebruiker; een latere laag wint). Zelfde code voor de
// daemon (welke indeling geldt nu) en de app (Properties: wat geldt er en waar komt het vandaan).
#pragma once
#include <string>
#include <vector>

#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/output/gamepad.hpp"

namespace ravengun::gamesettings {

inline constexpr const char* kGamepadSection = "Gamepad";

struct GamepadRow {
    std::string control;   // "LeftX"
    std::string target;    // "wheel", "none", ...
    std::string source;    // "default" (standaardindeling), "shipped" (meegeleverd game.ini of _defaults) of "user" (gebruikerslaag)
};

struct GamepadMapping {
    output::GamepadMap map;
    std::vector<GamepadRow> rows;        // elke bediening met een doel, plus de bedieningen die een laag op none zet
    std::vector<std::string> warnings;   // onbekende bedieningen of doelen (die tellen niet)
    bool has_section = false;            // een laag heeft [Gamepad]
};

// Uit de samengevoegde instellingen van een game en (voor de herkomst) de gebruikerslaag alleen.
GamepadMapping gamepad_mapping(const Ini& merged, const Ini& user_layer);
// Leest de lagen van `profile` (load_game + read_layer). Een leeg profiel: de standaardindeling.
GamepadMapping gamepad_mapping_for(const Dirs& dirs, const std::string& profile);

}  // namespace ravengun::gamesettings
