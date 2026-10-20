// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The start screen before a lightgun game (docs/prelaunch.md): full screen, black, operated with the Move alone (and a gamepad, the
// keyboard, or B/Escape to cancel). Works on the desktop (double-click, Play, Big Picture) and in Steam Game Mode
// (`ravengun --play PROFILE`, gamescope), where the main window is not visible.
//
// Steps, each only when needed (the engine decides, IPC game_ready, src/ravengun/games/readiness.hpp):
//   1. Waiting:     "Please turn on your PS Move controller" until player 1's Move is on (no time-out; cancelling works).
//   2. Calibrating: per player with a connected Move (player 1 blue, player 2 red on the Move and on the screen): aim at the centre
//                   and pull the trigger or press MOVE (IPC recenter of that player; MOVE recenters in the game too). Skipping a
//                   player is Tab or the gamepad Menu button, never a Move button. Can be switched off in Settings > Display.
//   3. Joining:     if the game has a second gun and player 2's Move is off, it gets kJoinMs to switch it on.
//   4. Starting:    GameLauncher::play (IPC game_start; the engine waits there with the same gate, so never blind).
// While waiting, calibrating and joining the screen shows a legend of the PS Move buttons for this game (core/ControlsLegend.*, built
// from the real configuration). There is no button set-up step: the buttons are the global layout, changed per game only in
// Properties > Controls (widgets/ControlsSetup.*).
// Mode::InApp closes the window as soon as the game starts (or at once if there is nothing to show). Mode::Play (--play, Steam and
// shortcuts) stays as a process while the game runs (Steam follows that process) and ends when the game stops.
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include "core/AppCore.h"
#include "core/BigPicture.h"
#include "core/ControlsLegend.h"
#include "core/PreLaunch.h"

namespace rg::ui {

class PreLaunchWindow final : public QWidget {
    Q_OBJECT
public:
    enum class Mode { InApp, Play };
    enum class Step { Connecting, WaitMove, Calibrate, Join, Starting, Running, Failed, Done };
    // Afloop (Finished): 0 = de game is gestart (InApp) of gespeeld en gestopt (Play); 1 = mislukt; 2 = afgebroken.
    enum Exit { kExitOk = 0, kExitFailed = 1, kExitCancelled = 2 };

    PreLaunchWindow(rg::AppCore* core, const rg::GameEntry& game, Mode mode, QWidget* parent = nullptr);
    ~PreLaunchWindow() override;

    // De flow beginnen: met de engine verbinden (Play) en game_ready vragen; het venster verschijnt pas als er iets te tonen is.
    void Begin();
    // Van buiten afbreken (SIGTERM van Steam bij "Exit game"): een lopende of startende game stoppen, LED's terug, dan Finished(2).
    void Abort();
    Step CurrentStep() const { return step_; }
    int CurrentPlayer() const { return player_; }
    // The legend lines as shown ("Square / Start = Start"), for tests.
    QStringList LegendLines() const;
    // Voor tests en schermafdrukken: een antwoord van game_ready of een aim-frame inspelen, zonder engine.
    void InjectPlan(const QJsonObject& gameReady);
    void InjectAim(const rg::bigpicture::AimState& s);

    // Het ene actieve startscherm van de app (nullptr = geen); een tweede Play wacht niet op het eerste.
    static PreLaunchWindow* Active();

signals:
    void Finished(int exitCode);

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    void ShowScreen();
    void SetStep(Step s);
    void RequestPlan();
    void OnPlan(const rg::prelaunch::Plan& p);
    void Advance();                 // naar de volgende stap na wachten, kalibreren of meedoen
    void StartCalibration(int player);
    void FinishCalibration(bool skipped);
    void StartJoin();
    void StartGame();
    void OnAim(const rg::bigpicture::AimState& s);
    void OnGamepad(int action);
    void Confirm();                 // trekker, A, Enter
    void Skip();                    // Tab, gamepad Menu (Join: MOVE)
    void Cancel();                  // B, Escape
    void Fail(const QString& message);
    void Done(int code);
    void OnLauncherState();
    void ColorLeds();
    void RestoreLeds();
    void RefreshLegend();           // the button legend from the real configuration (rg::controls::legend)
    void DrawLegend(QPainter& p, double W, double H, double u) const;

    rg::AppCore* core_;
    rg::GameEntry game_;
    Mode mode_;
    Step step_ = Step::Connecting;
    rg::prelaunch::Plan plan_;
    bool calibrate_ = true;
    QList<int> queue_;              // nog te kalibreren spelers
    QSet<int> done_;                // al gekalibreerd of overgeslagen
    QSet<int> leds_;                // spelers met een tijdelijke LED-kleur
    bool joinOffered_ = false;
    QList<int> joinPlayers_;        // de spelers die de stap "meedoen" noemt
    bool started_ = false;          // GameLauncher::play is aangeroepen
    bool startAcked_ = false;       // ... en de engine heeft game_start beantwoord
    qint64 startAt_ = 0;
    bool seenRunning_ = false;
    bool aborting_ = false;
    bool finished_ = false;
    bool recentering_ = false;
    int player_ = 0;                // speler van de kalibratie
    QString note_;                  // regel onder de opdracht (bv. "Hold the Move still...")
    QString failure_;
    qint64 stepAt_ = 0;             // begin van de stap (clock_)
    qint64 flashUntil_ = 0;         // "Player 1 is ready" tot dan
    rg::AimStream aim_;
    rg::GamepadInput pads_;
    rg::bigpicture::AimState aimState_;
    QElapsedTimer clock_;
    QTimer poll_, frame_, engineWait_;
    bool planInFlight_ = false;
    QList<rg::controls::LegendRow> legend_;   // the PS Move buttons for this game, shown while waiting and calibrating
};

// Eén ingang voor elke start vanuit de app (dubbelklik, Play, Big Picture): het startscherm als het nodig is, anders meteen
// GameLauncher::play. Geeft het venster terug (nullptr als er meteen gestart is of er al een startscherm loopt).
PreLaunchWindow* PlayWithPreLaunch(rg::AppCore* core, const rg::GameEntry& game);

}  // namespace rg::ui
