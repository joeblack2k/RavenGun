// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// De logica van de bedieningsrondgang (ControlsSetup) zonder widgets: welke stap, welke Move-knop er bij een signaal hoort, wat er met
// de oude knoppen gebeurt en wat er in game.ini komt ([Controls], docs/per-game-patches.md). Getest in tests/app/test_app_widgets.cpp.
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ravengun/gamesettings/controls_settings.hpp"

namespace rg {

class ControlsWalk {
public:
    // base = de huidige knoppenindeling van de speler (Move-knop -> actie, zoals de engine hem meldt); steps = de signalen van de game.
    ControlsWalk(const ravengun::gamesettings::ControlsInfo& info, std::map<std::string, std::string> base, int player = 1);

    enum class Press { Ignored, Advanced, Picked, Duplicate, Kept };

    size_t count() const { return steps_.size(); }
    size_t index() const { return index_; }
    bool done() const { return index_ >= steps_.size(); }
    const ravengun::gamesettings::ControlSignal& step() const { return steps_[index_]; }
    int player() const { return player_; }

    // Een druk op een Move-knop (naam uit de engine: triangle, circle, cross, square, select, start, move, ps, t, ...).
    // - uitleg-stap (trigger, offscreen): elke knop gaat verder
    // - toewijsbare stap: een van de zeven indeelbare knoppen wordt de keuze (Duplicate als hij al voor een ander signaal is gekozen);
    //   PS laat de standaard staan (Kept); T (de trekker) en andere knoppen tellen niet (Ignored).
    Press press(const std::string& button);
    void keepDefault();   // de standaard laten staan en verder
    void back();          // een stap terug (wist de keuze van die stap)
    void restart();

    // Knoppen die nu (volgens de basis) dit signaal geven, in de vaste volgorde van control_buttons().
    std::vector<std::string> defaultButtons(const std::string& signal) const;
    std::string picked(const std::string& signal) const;   // gekozen knop of leeg

    struct Line {
        std::string signal, label;
        bool assignable = false;
        bool picked = false;                // de gebruiker koos een knop
        std::vector<std::string> buttons;   // wat het signaal na het opslaan gebruikt (leeg + assignable = geen knop)
    };
    std::vector<Line> summary() const;
    // Het (knop, waarde)-lijstje voor save_user_controls: elke keuze (knop, signaal), plus (knop, "none") voor de oude knoppen van een
    // signaal waar de gebruiker een nieuwe knop voor koos, voor zover die knop niet voor iets anders gekozen is.
    std::vector<std::pair<std::string, std::string>> choices() const;
    // Signalen die na het opslaan geen enkele knop hebben (bv. de standaardknop ging naar een ander signaal).
    std::vector<std::string> unbound() const;

    // Weergavenaam van een Move-knop ("cross" -> "Cross", "move" -> "Move (big button)", "t" -> "Trigger").
    static std::string buttonName(const std::string& button);

private:
    std::vector<ravengun::gamesettings::ControlSignal> steps_;
    std::map<std::string, std::string> base_;
    int player_;
    size_t index_ = 0;
    std::map<std::string, std::string> picks_;   // signaal -> knop
    bool buttonTaken(const std::string& button) const;
};

}  // namespace rg
