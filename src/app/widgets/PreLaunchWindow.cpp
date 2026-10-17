// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/PreLaunchWindow.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <utility>

#ifdef RG_HAVE_CONTROLS_SETUP
#include "widgets/ControlsSetup.h"
#endif

namespace rg::ui {

namespace {

// Zelfde gedempte kleuren als de fullscreen-modus (OLED-tv): zwarte achtergrond, niets lichter dan nodig.
const QColor kText(0xc8, 0xc8, 0xc8);
const QColor kDim(0x6a, 0x6a, 0x6a);
const QColor kFaint(0x3c, 0x3c, 0x3c);
const QColor kRed(0xc0, 0x26, 0x26);

constexpr qint64 kInputGuardMs = 350;    // zo lang na het begin van een stap telt een trekker niet (de vorige stap)
constexpr qint64 kReadyFlashMs = 900;    // "Player 1 is ready" na het centreren
constexpr qint64 kFailedShowMs = 12000;  // een fout blijft zo lang staan (of tot een knop)
constexpr qint64 kStartAckMs = 5000;     // zonder antwoord op game_start geldt de toestand van de engine na deze tijd toch

PreLaunchWindow* g_active = nullptr;

QString playerName(int p) {
    switch (p) {
        case 2: return QStringLiteral("red");
        case 3: return QStringLiteral("green");
        case 4: return QStringLiteral("yellow");
        default: return QStringLiteral("blue");
    }
}

}  // namespace

PreLaunchWindow* PreLaunchWindow::Active() { return g_active; }

PreLaunchWindow::PreLaunchWindow(rg::AppCore* core, const rg::GameEntry& game, Mode mode, QWidget* parent)
    : QWidget(parent), core_(core), game_(game), mode_(mode), aim_(core->engine()->socketPath()) {
    setWindowTitle(QStringLiteral("RavenGun - %1").arg(game_.displayName()));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    clock_.start();

    connect(&aim_, &rg::AimStream::aimChanged, this, &PreLaunchWindow::OnAim);
    connect(&pads_, &rg::GamepadInput::action, this, [this](rg::GamepadInput::Action a) { OnGamepad(static_cast<int>(a)); });
    connect(core_->launcher(), &rg::GameLauncher::stateChanged, this, &PreLaunchWindow::OnLauncherState);
    connect(core_->launcher(), &rg::GameLauncher::message, this, [this](const QString& text, bool error) {
        if (!started_ || finished_) return;
        // Het antwoord op game_start (of een weigering vooraf, zoals "A game is already running"). Na dit antwoord gaat elke
        // toestand van de engine over deze start (antwoorden komen in volgorde over dezelfde verbinding).
        if (error) {
            Fail(text);
            return;
        }
        startAcked_ = true;
        OnLauncherState();
    });
    connect(core_->engine(), &rg::EngineClient::connectedChanged, this, [this](bool c) {
        if (c && step_ == Step::Connecting && !finished_) RequestPlan();
    });
    poll_.setInterval(500);
    connect(&poll_, &QTimer::timeout, this, &PreLaunchWindow::RequestPlan);
    frame_.setInterval(33);
    connect(&frame_, &QTimer::timeout, this, [this] {
        const qint64 in = clock_.elapsed() - stepAt_;
        if (step_ == Step::Join && in >= rg::prelaunch::kJoinMs) Advance();
        if (step_ == Step::Failed && in >= kFailedShowMs) Done(kExitFailed);
        update();
    });
    engineWait_.setSingleShot(true);
    connect(&engineWait_, &QTimer::timeout, this, [this] {
        if (step_ == Step::Connecting && !finished_)
            Fail(tr("The RavenGun engine is not running. Start it in the RavenGun app (Engine > Start Engine) and try again."));
    });
}

PreLaunchWindow::~PreLaunchWindow() {
    if (g_active == this) g_active = nullptr;
}

void PreLaunchWindow::Begin() {
    g_active = this;
    calibrate_ = rg::prelaunch::calibrateEnabled();
    SetStep(Step::Connecting);
    // In Steam Game Mode is er verder niets te zien: meteen een zwart scherm met de naam van de game.
    if (mode_ == Mode::Play) ShowScreen();
    if (!core_->engine()->isConnected()) {
        engineWait_.start(rg::prelaunch::kEngineWaitMs);
        return;
    }
    RequestPlan();
}

void PreLaunchWindow::ShowScreen() {
    if (!isVisible()) {
        showFullScreen();
        raise();
        activateWindow();
    }
    setFocus();
    aim_.start();
    pads_.start();
    frame_.start();
}

void PreLaunchWindow::SetStep(Step s) {
    step_ = s;
    stepAt_ = clock_.elapsed();
    note_.clear();
    update();
}

void PreLaunchWindow::RequestPlan() {
    if (planInFlight_ || finished_) return;
    planInFlight_ = true;
    core_->engine()->request(QStringLiteral("game_ready"), {{QStringLiteral("profile"), game_.profile}}, [this](const rg::EngineReply& r) {
        planInFlight_ = false;
        if (finished_) return;
        // Een oudere engine kent game_ready niet (of het profiel is onbekend): geen scherm; game_start wacht of meldt het zelf.
        OnPlan(r.ok ? rg::prelaunch::parsePlan(r.object()) : rg::prelaunch::Plan{});
    }, 3000);
}

void PreLaunchWindow::InjectPlan(const QJsonObject& gameReady) { OnPlan(rg::prelaunch::parsePlan(gameReady)); }

void PreLaunchWindow::OnPlan(const rg::prelaunch::Plan& p) {
    plan_ = p;
    switch (step_) {
        case Step::Connecting:
            engineWait_.stop();
            if (!rg::prelaunch::needsScreen(p, calibrate_, ControlsSetupNeeded(game_.profile))) {
                StartGame();
                return;
            }
            ShowScreen();
            if (!p.ready) {
                SetStep(Step::WaitMove);
                poll_.start();
                return;
            }
            queue_ = calibrate_ ? p.calibrate : QList<int>{};
            ColorLeds();
            Advance();
            return;
        case Step::WaitMove:
            if (started_ || !p.ready) { update(); return; }
            poll_.stop();
            queue_ = calibrate_ ? p.calibrate : QList<int>{};
            ColorLeds();
            Advance();
            return;
        case Step::Join:
            // Een speler die nu zijn Move aanzet, kalibreert nog (in de volgorde van de spelers).
            for (int pl : p.calibrate)
                if (joinPlayers_.contains(pl) && !done_.contains(pl) && !queue_.contains(pl)) queue_ << pl;
            if (!queue_.isEmpty()) {
                ColorLeds();
                Advance();
            }
            update();
            return;
        default:
            return;
    }
}

void PreLaunchWindow::Advance() {
    if (finished_ || started_) return;
    while (!queue_.isEmpty() && done_.contains(queue_.first())) queue_.removeFirst();
    if (calibrate_ && !queue_.isEmpty()) {
        StartCalibration(queue_.takeFirst());
        return;
    }
    // Speler 2..N met een Move die uit staat: even de tijd om mee te doen (alleen als het scherm er toch al is).
    if (calibrate_ && !joinOffered_ && isVisible()) {
        QList<int> off;
        for (int pl : plan_.optionalOff)
            if (!done_.contains(pl)) off << pl;
        if (!off.isEmpty()) {
            joinPlayers_ = off;
            StartJoin();
            return;
        }
    }
    poll_.stop();
    RestoreLeds();
    if (!controlsDone_) {
        controlsDone_ = true;
        if (RunControlsSetupStep()) return;
    }
    StartGame();
}

void PreLaunchWindow::StartCalibration(int player) {
    player_ = player;
    poll_.stop();
    aim_.setPlayer(player);
    aimState_ = {};
    recentering_ = false;
    flashUntil_ = 0;
    SetStep(Step::Calibrate);
}

void PreLaunchWindow::FinishCalibration(bool skipped) {
    (void)skipped;
    if (step_ != Step::Calibrate) return;
    done_ << player_;
    Advance();
}

void PreLaunchWindow::StartJoin() {
    joinOffered_ = true;
    player_ = 1;
    aim_.setPlayer(1);
    SetStep(Step::Join);
    poll_.start();
}

void PreLaunchWindow::StartGame() {
    if (started_ || finished_) return;
    started_ = true;
    poll_.stop();
    RestoreLeds();
    SetStep(Step::Starting);
    startAt_ = clock_.elapsed();
    // Zonder antwoord op game_start (time-out) telt de toestand van de engine na kStartAckMs toch: dan nog een keer kijken.
    QTimer::singleShot(kStartAckMs + 100, this, &PreLaunchWindow::OnLauncherState);
    core_->launcher()->play(game_);
}

void PreLaunchWindow::OnLauncherState() {
    if (!started_ || finished_) return;
    using S = rg::GameLauncher::State;
    const S st = core_->launcher()->state();
    // Een antwoord op een eerdere `games` kan nog "idle" of "failed" van vóór deze start zijn: dat telt pas na het antwoord op
    // game_start (of na kStartAckMs).
    const bool trusted = seenRunning_ || startAcked_ || clock_.elapsed() - startAt_ > kStartAckMs;
    switch (st) {
        case S::Running:
            seenRunning_ = true;
            if (mode_ == Mode::InApp) {
                Done(kExitOk);
            } else {
                // Steam volgt dit proces: het blijft bestaan, onzichtbaar, tot de game stopt.
                SetStep(Step::Running);
                aim_.stop();
                pads_.stop();
                frame_.stop();
                hide();
            }
            break;
        case S::WaitingMove:
            // De engine wacht zelf (de Move ging tussendoor uit): dezelfde melding, nu uit de status van de engine.
            ShowScreen();
            if (step_ != Step::WaitMove) SetStep(Step::WaitMove);
            break;
        case S::Starting:
            if (step_ == Step::WaitMove) { step_ = Step::Starting; update(); }
            break;
        case S::Failed:
            if (trusted) Fail(core_->launcher()->text());
            break;
        case S::Idle:
            if (trusted) Done(kExitOk);
            break;
        default:
            break;
    }
}

void PreLaunchWindow::OnAim(const rg::bigpicture::AimState& s) {
    aimState_ = s;
    if (finished_ || step_ == Step::Controls) return;
    const bool guard = clock_.elapsed() - stepAt_ < kInputGuardMs;
    if (!guard) {
        if (s.movePresses > 0) Skip();
        else if (s.triggerPresses > 0 || (step_ == Step::Calibrate && s.psPresses > 0)) Confirm();
    }
    update();
}

void PreLaunchWindow::InjectAim(const rg::bigpicture::AimState& s) { OnAim(s); }

void PreLaunchWindow::OnGamepad(int action) {
    if (finished_ || step_ == Step::Controls) return;
    switch (static_cast<rg::GamepadInput::Action>(action)) {
        case rg::GamepadInput::Action::Accept: Confirm(); break;
        case rg::GamepadInput::Action::Back: Cancel(); break;
        case rg::GamepadInput::Action::Menu: Skip(); break;
        default: break;
    }
}

void PreLaunchWindow::keyPressEvent(QKeyEvent* e) {
    if (step_ == Step::Controls) return QWidget::keyPressEvent(e);
    switch (e->key()) {
        case Qt::Key_Escape:
        case Qt::Key_Back:
        case Qt::Key_Backspace: Cancel(); break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space: Confirm(); break;
        case Qt::Key_Tab:
        case Qt::Key_S: Skip(); break;
        default: QWidget::keyPressEvent(e);
    }
}

void PreLaunchWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) Confirm();
    else if (e->button() == Qt::RightButton) Cancel();
}

void PreLaunchWindow::Confirm() {
    if (finished_) return;
    if (clock_.elapsed() - stepAt_ < kInputGuardMs) return;
    switch (step_) {
        case Step::Calibrate: {
            if (recentering_ || flashUntil_ > clock_.elapsed()) return;
            recentering_ = true;
            const int p = player_;
            core_->engine()->request(QStringLiteral("recenter"), {{QStringLiteral("player"), p}}, [this, p](const rg::EngineReply& r) {
                recentering_ = false;
                if (finished_ || step_ != Step::Calibrate || player_ != p) return;
                if (!r.ok) {
                    note_ = tr("The PS Move is not ready yet. Hold it still for a moment, then aim at the centre and pull the trigger again.");
                    update();
                    return;
                }
                flashUntil_ = clock_.elapsed() + kReadyFlashMs;
                note_.clear();
                update();
                QTimer::singleShot(kReadyFlashMs, this, [this, p] {
                    if (step_ == Step::Calibrate && player_ == p) FinishCalibration(false);
                });
            });
            break;
        }
        case Step::Join: Advance(); break;
        case Step::Failed: Done(kExitFailed); break;
        default: break;
    }
}

void PreLaunchWindow::Skip() {
    if (finished_) return;
    if (step_ == Step::Calibrate && !recentering_) FinishCalibration(true);
    else if (step_ == Step::Join) Advance();
}

void PreLaunchWindow::Cancel() {
    if (finished_) return;
    if (step_ == Step::Failed) { Done(kExitFailed); return; }
    if (started_ && core_->launcher()->canStop()) { Abort(); return; }
    Done(kExitCancelled);
}

void PreLaunchWindow::Abort() {
    if (finished_ || aborting_) return;
    aborting_ = true;
    RestoreLeds();
    if (!started_) { Done(kExitCancelled); return; }
    // Een game die start of draait: stoppen en pas daarna klaar (een proces dat Steam afsluit, wacht hier hoogstens 3 s).
    note_ = tr("Stopping the game...");
    update();
    core_->engine()->request(QStringLiteral("game_stop"), {}, [this](const rg::EngineReply&) { Done(kExitCancelled); }, 3000);
}

void PreLaunchWindow::Fail(const QString& message) {
    if (finished_) return;
    RestoreLeds();
    poll_.stop();
    failure_ = message;
    if (mode_ == Mode::InApp && !isVisible()) {   // de app toont de melding zelf (statusbalk)
        Done(kExitFailed);
        return;
    }
    ShowScreen();
    SetStep(Step::Failed);
}

void PreLaunchWindow::Done(int code) {
    if (finished_) return;
    finished_ = true;
    RestoreLeds();
    poll_.stop();
    frame_.stop();
    engineWait_.stop();
    aim_.stop();
    pads_.stop();
    if (controls_) controls_->deleteLater();
    hide();
    if (g_active == this) g_active = nullptr;
    emit Finished(code);
}

void PreLaunchWindow::closeEvent(QCloseEvent* e) {
    e->accept();
    if (!finished_) Cancel();
}

void PreLaunchWindow::resizeEvent(QResizeEvent*) {
    if (controls_) controls_->setGeometry(rect());
}

void PreLaunchWindow::ColorLeds() {
    if (!calibrate_) return;
    QList<int> players = queue_;
    if (step_ == Step::Calibrate && player_ > 0) players << player_;
    for (int p : players) {
        if (leds_.contains(p) || done_.contains(p)) continue;
        QJsonObject f{{QStringLiteral("player"), p}, {QStringLiteral("temporary"), true}};
        const QString preset = rg::prelaunch::ledPreset(p);
        if (!preset.isEmpty()) {
            f.insert(QStringLiteral("preset"), preset);
        } else {
            const QColor c = rg::prelaunch::playerColor(p);
            f.insert(QStringLiteral("r"), c.red());
            f.insert(QStringLiteral("g"), c.green());
            f.insert(QStringLiteral("b"), c.blue());
        }
        // Een oudere engine kent `temporary` niet (bad_request): dan blijft de eigen kleur staan. Nooit een bewaarde kleur zetten.
        core_->engine()->request(QStringLiteral("set_led"), f, {});
        leds_ << p;
    }
}

void PreLaunchWindow::RestoreLeds() {
    for (int p : std::as_const(leds_))
        core_->engine()->request(QStringLiteral("set_led"), {{QStringLiteral("player"), p}, {QStringLiteral("restore"), true}}, {});
    leds_.clear();
}

// ---- HAAK: de uitleg van de knoppen ------------------------------------------------------------------------------------
// Tussen de kalibratie en de start van de game. De widget ControlsSetup (src/app/widgets/ControlsSetup.{h,cpp}) bouwt een andere
// taak: ctor (AppCore*, const QString& profile, QWidget* parent), signaal finished(bool ok), static bool needsSetup(const QString&).
// Staat ControlsSetup.cpp in src/app/widgets, dan neemt src/app/CMakeLists.txt hem vanzelf mee en zet RG_HAVE_CONTROLS_SETUP; anders
// slaat de flow deze stap over. finished(true) = verder naar de start; finished(false) = de speler breekt af (terug, geen start).
bool PreLaunchWindow::ControlsSetupNeeded(const QString& profile) {
#ifdef RG_HAVE_CONTROLS_SETUP
    return ControlsSetup::needsSetup(profile);
#else
    (void)profile;
    return false;
#endif
}

bool PreLaunchWindow::RunControlsSetupStep() {
#ifdef RG_HAVE_CONTROLS_SETUP
    if (!ControlsSetupNeeded(game_.profile)) return false;
    ShowScreen();
    SetStep(Step::Controls);
    auto* w = new ControlsSetup(core_, game_.profile, this);
    controls_ = w;
    w->setGeometry(rect());
    w->show();
    w->setFocus();
    connect(w, &ControlsSetup::finished, this, [this, w](bool ok) {
        w->deleteLater();
        if (finished_) return;
        setFocus();
        if (ok) StartGame();
        else Done(kExitCancelled);
    });
    return true;
#else
    return false;
#endif
}

// ---- Tekenen ---------------------------------------------------------------------------------------------------------

void PreLaunchWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::black);
    if (step_ == Step::Controls) return;
    const double W = width(), H = height();
    const double u = std::max(1.0, H / 1080.0);
    // Tekst gecentreerd vanaf y (in eenheden van 1080p), met afbreken; geeft de onderkant (zelfde eenheden) voor de volgende regel.
    auto text = [&](const QString& t, double y, double px, const QColor& c, bool bold = false) {
        QFont f = font();
        f.setPixelSize(static_cast<int>(px * u));
        f.setBold(bold);
        p.setFont(f);
        p.setPen(c);
        QRectF used;
        p.drawText(QRectF(W * 0.08, y * u, W * 0.84, H - y * u), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, t, &used);
        return used.bottom() / u;
    };
    // Bovenaan altijd de game.
    text(game_.displayName(), 60, 30, kDim);
    QString hint = tr("B / Esc: cancel");
    const qint64 now = clock_.elapsed();
    switch (step_) {
        case Step::Connecting:
            text(tr("Connecting to RavenGun..."), H / u * 0.42, 44, kText);
            break;
        case Step::WaitMove: {
            const QString main = started_ ? core_->launcher()->text() : rg::prelaunch::waitingText(plan_.waiting, plan_.waitingAny);
            const double below = text(main.isEmpty() ? rg::prelaunch::waitingText({}, true) : main, H / u * 0.34, 60, kText, true);
            text(tr("Press the PS button on the Move."), below + 24, 34, kDim);
            // Een rustig kloppende ring: er gebeurt iets, ook zonder invoer.
            const double pulse = 0.5 + 0.5 * std::sin(now / 450.0);
            p.setPen(QPen(QColor(kDim.red(), kDim.green(), kDim.blue(), static_cast<int>(80 + 120 * pulse)), 3 * u));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(W / 2, H * 0.72), 26 * u, 26 * u);
            break;
        }
        case Step::Calibrate: {
            const QColor pc = rg::prelaunch::playerColor(player_);
            const bool ready = flashUntil_ > now;
            const double below = text(tr("Player %1 (%2)").arg(player_).arg(playerName(player_)), 130, 46, pc, true);
            text(ready ? tr("Player %1 is ready").arg(player_) : tr("Aim at the centre of the screen and pull the trigger"), below + 10, 40,
                 ready ? pc : kText);
            if (!note_.isEmpty()) text(note_, H / u * 0.70, 30, kRed);
            // Het doel in het midden, in de kleur van de speler.
            const QPointF c(W / 2, H / 2);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(pc, 4 * u));
            p.drawEllipse(c, 70 * u, 70 * u);
            p.setPen(QPen(pc.darker(170), 2 * u));
            p.drawEllipse(c, 28 * u, 28 * u);
            p.drawLine(QPointF(c.x() - 100 * u, c.y()), QPointF(c.x() - 80 * u, c.y()));
            p.drawLine(QPointF(c.x() + 80 * u, c.y()), QPointF(c.x() + 100 * u, c.y()));
            p.drawLine(QPointF(c.x(), c.y() - 100 * u), QPointF(c.x(), c.y() - 80 * u));
            p.drawLine(QPointF(c.x(), c.y() + 80 * u), QPointF(c.x(), c.y() + 100 * u));
            // Het vizier van deze speler (als de engine al een aim heeft).
            if (aimState_.valid && !aimState_.offScreen) {
                const QPointF a(aimState_.pos.x() * W, aimState_.pos.y() * H);
                p.setPen(QPen(pc.lighter(130), 3 * u));
                p.drawLine(QPointF(a.x() - 22 * u, a.y()), QPointF(a.x() + 22 * u, a.y()));
                p.drawLine(QPointF(a.x(), a.y() - 22 * u), QPointF(a.x(), a.y() + 22 * u));
            }
            hint = tr("MOVE: skip   ·   B / Esc: cancel");
            break;
        }
        case Step::Join: {
            QStringList who;
            for (int pl : std::as_const(joinPlayers_)) who << QString::number(pl);
            const QColor pc = rg::prelaunch::playerColor(joinPlayers_.value(0, 2));
            const double below = text(tr("Player %1: turn on your PS Move to join").arg(who.join(tr(" and "))), H / u * 0.34, 52, pc, true);
            const int left = std::max(0, static_cast<int>((rg::prelaunch::kJoinMs - (now - stepAt_) + 999) / 1000));
            text(tr("Starting in %1 s. Player 1: pull the trigger to start now.").arg(left), below + 24, 34, kDim);
            hint = tr("Trigger: start now   ·   B / Esc: cancel");
            break;
        }
        case Step::Starting:
        case Step::Running:
            text(note_.isEmpty() ? tr("Starting %1...").arg(game_.displayName()) : note_, H / u * 0.42, 44, kText);
            break;
        case Step::Failed: {
            const double below = text(tr("Could not start %1").arg(game_.displayName()), H / u * 0.34, 48, kRed, true);
            text(failure_, below + 24, 30, kText);
            hint = tr("Press any button to go back");
            break;
        }
        default:
            break;
    }
    text(hint, H / u - 90, 26, kFaint);
}

// ---- Eén ingang voor de app ------------------------------------------------------------------------------------------

PreLaunchWindow* PlayWithPreLaunch(rg::AppCore* core, const rg::GameEntry& game) {
    // Wat de launcher zelf weigert (niet ingesteld, engine uit, er loopt al iets): geen scherm, de melding komt van de launcher.
    if (!rg::GameLauncher::whyNotPlayable(game).isEmpty() || !core->engine()->isConnected() || core->launcher()->canStop()) {
        core->launcher()->play(game);
        return nullptr;
    }
    if (PreLaunchWindow* a = PreLaunchWindow::Active()) {
        a->raise();
        a->activateWindow();
        return nullptr;
    }
    auto* w = new PreLaunchWindow(core, game, PreLaunchWindow::Mode::InApp);
    QObject::connect(w, &PreLaunchWindow::Finished, w, [w](int) { w->deleteLater(); });
    w->Begin();
    return w;
}

}  // namespace rg::ui
