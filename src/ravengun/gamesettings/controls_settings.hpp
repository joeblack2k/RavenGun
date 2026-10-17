// SPDX-License-Identifier: GPL-3.0-or-later
// De bediening van een lightgun-game: [Controls] in patches/<Game>/game.ini (docs/per-game-patches.md, "Bediening per game").
// Drie soorten sleutels in dezelfde sectie:
//   <signaal> = Label | Uitleg        wat het signaal in deze game doet (meegeleverd; de gebruikerslaag mag het overschrijven)
//   P<n>.<knop> = <signaal>           de keuze van de gebruiker: Move-knop van speler n geeft dit signaal (alleen gebruikerslaag)
//   Confirmed = true                  de gebruiker heeft de bediening doorlopen en bevestigd (zet de app)
// Zelfde code voor de daemon (welke knoppen gelden nu) en de app (ControlsSetup, Properties). Alleen de standaardbibliotheek.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "ravengun/gamesettings/gamesettings.hpp"

namespace ravengun::gamesettings {

inline constexpr const char* kControlsSection = "Controls";

// Een signaal van de lightgun-laag. `assignable` = de gebruiker kiest er een Move-knop voor; trigger (de analoge T-knop) en
// offscreen (het richtpunt buiten het beeld) niet, die worden alleen uitgelegd.
struct ControlSignalDef {
    const char* name;
    bool assignable;
};
extern const ControlSignalDef kControlSignals[];   // trigger, reload, grenade, aux1, aux2, start, coin, offscreen
extern const size_t kControlSignalCount;
bool control_signal_known(const std::string& name);
bool control_signal_assignable(const std::string& name);
// De actie voor gun_buttons.map (config::check_gun_action) die dit signaal geeft aan een Move-knop van `player`: reload, grenade, aux1,
// aux2, "key:<speler>" voor start, "key:5" voor coin. Leeg als het signaal niet toewijsbaar is.
std::string control_action(const std::string& signal, int player);

// De Move-knoppen die de gebruiker mag indelen (de PS-knop is altijd recenter, T is de trekker).
const std::vector<std::string>& control_buttons();   // triangle, circle, cross, square, select, start, move

struct ControlSignal {
    std::string signal, label, explanation;
    bool assignable = false;
};
struct ControlsInfo {
    std::vector<ControlSignal> entries;   // de signalen, in de volgorde van de game.ini
    bool confirmed = false;
    std::vector<std::string> warnings;    // onbekende signalen (die tellen niet)
};
// Uit de samengevoegde instellingen van een game.
ControlsInfo controls_info(const Ini& merged);

// "Label | Uitleg" splitsen (de eerste '|'); zonder '|' is alles het label.
std::pair<std::string, std::string> split_label(const std::string& value);

// De keuzes van `player` als (Move-knop, actie)-paren voor gun_buttons.map; leeg = geen keuzes (alles blijft zoals nu). De waarde is
// een toewijsbaar signaal, "none", "recenter" of "key:<naam>"; een onbekende waarde of knop wordt overgeslagen met een waarschuwing.
std::vector<std::pair<std::string, std::string>> controls_overrides(const Ini& merged, int player, std::vector<std::string>* warnings = nullptr);

// Moet de bediening nog doorlopen worden: de game beschrijft minstens een toewijsbaar signaal en de gebruiker heeft niet bevestigd.
bool controls_need_setup(const Ini& merged);

// Schrijft de keuzes van de gebruiker: verwijdert alle P<n>.<knop> uit de gebruikerslaag, zet de nieuwe en Confirmed = true.
// choices[n-1] = (knop, signaal of "none"); spelers zonder keuzes krijgen niets.
bool save_user_controls(const Dirs& dirs, const std::string& profile, const std::vector<std::vector<std::pair<std::string, std::string>>>& choices,
                        std::string* err = nullptr);
// Wist de keuzes en Confirmed uit de gebruikerslaag (terug naar de standaard; de walkthrough komt weer).
bool reset_user_controls(const Dirs& dirs, const std::string& profile, std::string* err = nullptr);

}  // namespace ravengun::gamesettings
