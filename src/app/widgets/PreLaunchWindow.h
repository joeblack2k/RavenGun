// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Het startscherm vóór een lightgun-game (docs/prelaunch.md): schermvullend, zwart, te bedienen met alleen de Move (en een gamepad,
// toetsenbord of B/Escape om af te breken). Werkt op het bureaublad (dubbelklik, Play, Big Picture) en in Steam Game Mode
// (`ravengun --play PROFIEL`, gamescope), waar het hoofdvenster niet te zien is.
//
// Stappen, elk alleen als het nodig is (de beslissing komt van de engine, IPC game_ready, src/ravengun/games/readiness.hpp):
//   1. Wachten:     "Please turn on your PS Move controller" tot de Move van speler 1 aan is (geen time-out; afbreken kan).
//   2. Kalibreren:  per speler met een verbonden Move (speler 1 blauw, speler 2 rood op de Move en op het scherm): op het midden
//                   richten en de trekker overhalen (IPC recenter van die speler). MOVE slaat de speler over. Uit te zetten in
//                   Settings > Display.
//   3. Meedoen:     heeft de game een tweede gun en staat de Move van speler 2 uit, dan krijgt hij kJoinMs om hem aan te zetten.
//   4. HAAK:        de uitleg van de knoppen (ControlsSetup), zie RunControlsSetupStep().
//   5. Starten:     GameLauncher::play (IPC game_start; de engine wacht daar met dezelfde poort, dus nooit blind).
// Mode::InApp sluit het venster zodra de game start (of meteen, als er niets te tonen is). Mode::Play (--play, Steam en
// snelkoppelingen) blijft als proces bestaan zolang de game draait (Steam volgt dat proces) en stopt als de game stopt.
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include "core/AppCore.h"
#include "core/BigPicture.h"
#include "core/PreLaunch.h"

namespace rg::ui {

class PreLaunchWindow final : public QWidget {
    Q_OBJECT
public:
    enum class Mode { InApp, Play };
    enum class Step { Connecting, WaitMove, Calibrate, Join, Controls, Starting, Running, Failed, Done };
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
    void resizeEvent(QResizeEvent*) override;
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
    void Skip();                    // MOVE, Y, Tab
    void Cancel();                  // B, Escape
    void Fail(const QString& message);
    void Done(int code);
    void OnLauncherState();
    void ColorLeds();
    void RestoreLeds();
    // HAAK: de uitleg van de knoppen vóór de start (src/app/widgets/ControlsSetup.{h,cpp}, een aparte taak). true = de stap loopt
    // (de widget meldt zich met finished(bool ok)); false = geen ControlsSetup in deze build of niet nodig voor deze game.
    bool RunControlsSetupStep();
    static bool ControlsSetupNeeded(const QString& profile);

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
    bool controlsDone_ = false;
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
    QPointer<QWidget> controls_;
};

// Eén ingang voor elke start vanuit de app (dubbelklik, Play, Big Picture): het startscherm als het nodig is, anders meteen
// GameLauncher::play. Geeft het venster terug (nullptr als er meteen gestart is of er al een startscherm loopt).
PreLaunchWindow* PlayWithPreLaunch(rg::AppCore* core, const rg::GameEntry& game);

}  // namespace rg::ui
