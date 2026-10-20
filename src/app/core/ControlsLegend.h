// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The legend of the PS Move buttons on the start screen (PreLaunchWindow), in plain words and for one game: built from the real
// configuration, never from a fixed table. Sources: the global `gun_buttons.map` (the engine's status, else ~/.config/ravengun/config.json,
// else the engine's built-in default), the game's own [Controls] choices (gamesettings::controls_overrides) and the signals the game
// declares in its [Controls] (their labels, e.g. "Shoot" or "Reload off-screen"). Without windows, so it is testable (ravengun_app_tests).
#include <QJsonObject>
#include <QList>
#include <QString>
#include <map>
#include <string>

#include "ravengun/gamesettings/controls_settings.hpp"
#include "ravengun/gamesettings/gamesettings.hpp"

namespace rg::controls {

// One line of the legend: the button(s) the player presses and what that does ("Square / Start" -> "Start").
struct LegendRow {
    QString buttons;
    QString text;
};

// The global button layout (Move button or Sharp Shooter input -> action): `engineStatus` is the engine's status object
// (gun_buttons.map); without it the engine's config.json is read; the built-in default fills what neither names.
std::map<std::string, std::string> baseLayout(const QJsonObject& engineStatus);
// The built-in default of the engine (config::GunButtonSettings::default_gun_map), only for what the engine and config.json do not say.
std::map<std::string, std::string> defaultLayout();
// The layout the game gets: `base` with the game's [Controls] choices for `player` over it.
std::map<std::string, std::string> effectiveLayout(const std::map<std::string, std::string>& base, const ravengun::gamesettings::Ini& merged,
                                                   int player = 1);

// The legend for a game (its merged settings) with the layout above. Only what applies: the trigger and the signals the game declares
// (reload, grenade, ...), always Start, Coin, TEST and Service when a button gives them, and the buttons that recenter the aim.
QList<LegendRow> legend(const ravengun::gamesettings::Ini& merged, const std::map<std::string, std::string>& effective);

// Display name of a Move button or Sharp Shooter input as the legend writes it ("move" -> "MOVE", "pump" -> "Pump").
QString legendButtonName(const std::string& button);

}  // namespace rg::controls
