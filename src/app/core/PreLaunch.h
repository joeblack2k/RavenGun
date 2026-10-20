// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Het startscherm vóór een lightgun-game, zonder widgets (docs/prelaunch.md): het antwoord van de engine op `game_ready` (de poort
// van src/ravengun/games/readiness.hpp), de Engelse meldingen, de kleuren per speler en welke stappen er komen. Het venster zelf is
// widgets/PreLaunchWindow.*; deze logica is los te testen (ravengun_app_tests).
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace rg::prelaunch {

// Wat de engine over deze start zegt (IPC game_ready). `known` = false: een oudere engine zonder game_ready (dan geen scherm, de
// engine wacht zelf zoals vroeger).
struct Plan {
    bool known = false;
    bool lightgun = true;
    int gunPlayers = 4;
    bool ready = true;
    QList<int> waiting;        // spelers op wiens Move de start wacht
    bool waitingAny = false;   // speler 1 heeft geen Move: een willekeurige lightgun is genoeg
    QList<int> optionalOff;    // speler 2..N met een Move die uit staat (kan nog meedoen)
    QList<int> calibrate;      // spelers met een verbonden Move, oplopend
};
Plan parsePlan(const QJsonObject& gameReady);

// "Please turn on your PS Move controller" (speler 1, of een willekeurige Move), anders met de speler erbij. Leeg als niets wacht.
QString waitingText(const QList<int>& players, bool any);
// De Engelse melding bij de engine-toestand waiting_move (games.status.waiting_players).
QString waitingTextFromStatus(const QJsonObject& gamesStatus);

// De kleur van een speler op het startscherm en tijdelijk op zijn Move: 1 blauw, 2 rood, 3 groen, 4 geel (de eigenaar: P1 blauw,
// P2 rood). `ledPreset` is de naam voor set_led (de engine kent Nederlandse presets); leeg = rgb gebruiken.
QColor playerColor(int player);
QString ledPreset(int player);

// Instelling (ravengun-app.conf, Settings > Display): de korte kalibratie vóór elke lightgun-game. Standaard aan.
bool calibrateEnabled();
void setCalibrateEnabled(bool on);

// Zo lang krijgt een speler 2..N met een Move die uit staat de tijd om mee te doen (na de kalibratie van speler 1).
inline constexpr int kJoinMs = 10000;
// Zo lang wacht --play op de engine voordat het een fout toont.
inline constexpr int kEngineWaitMs = 10000;

// Must the start screen appear? Only for a lightgun game when it has to wait for a Move or calibrate. Otherwise the game starts at
// once (a gamepad game, or calibration is off and the Move is already on). There is no button set-up step in the launch flow.
bool needsScreen(const Plan& p, bool calibrate);

}  // namespace rg::prelaunch
