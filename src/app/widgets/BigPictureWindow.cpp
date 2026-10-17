// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/BigPictureWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileInfo>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <cmath>

#include "core/AppPaths.h"
#include "core/GameListModel.h"
#include "widgets/PreLaunchWindow.h"

namespace rg::ui {

namespace {

// Gedempte kleuren: niets is lichter dan nodig (OLED-tv), de achtergrond is altijd puur zwart.
const QColor kText(0xb8, 0xb8, 0xb8);
const QColor kDim(0x6a, 0x6a, 0x6a);
const QColor kFaint(0x3c, 0x3c, 0x3c);
const QColor kRed(0xc0, 0x26, 0x26);
const QColor kRedDim(0x7a, 0x1c, 0x1c);

QSettings& settings() {
    static QSettings s(rg::AppPaths::appSettingsFile(), QSettings::IniFormat);
    return s;
}

double batteryFraction(const QJsonObject& b) {
    if (!b.value(QStringLiteral("percent")).isNull() && b.contains(QStringLiteral("percent")))
        return b.value(QStringLiteral("percent")).toInt() / 100.0;
    const QString c = b.value(QStringLiteral("code")).toString();
    if (c == QStringLiteral("min")) return 0.1;
    if (c == QStringLiteral("ge20")) return 0.3;
    if (c == QStringLiteral("ge40")) return 0.5;
    if (c == QStringLiteral("ge60")) return 0.7;
    if (c == QStringLiteral("ge80")) return 0.9;
    if (c == QStringLiteral("max") || c == QStringLiteral("charged")) return 1.0;
    return -1;
}

}  // namespace

BigPictureWindow::BigPictureWindow(rg::AppCore* core, QWidget* parent) : QWidget(parent), core_(core), aim_(core->engine()->socketPath()) {
    setWindowTitle(QStringLiteral("RavenGun"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    show_all_ = settings().value(QStringLiteral("fullscreen/show_all"), false).toBool();
    hide_during_game_ = settings().value(QStringLiteral("fullscreen/hide_during_game"), true).toBool();
    clock_.start();

    connect(&aim_, &rg::AimStream::aimChanged, this, &BigPictureWindow::OnAim);
    connect(&pads_, &rg::GamepadInput::action, this, [this](rg::GamepadInput::Action a) { OnGamepad(static_cast<int>(a)); });
    connect(core_->library(), &rg::GameLibrary::gamesChanged, this, &BigPictureWindow::Rebuild);
    connect(core_->launcher(), &rg::GameLauncher::stateChanged, this, &BigPictureWindow::OnLauncherState);
    connect(core_->launcher(), &rg::GameLauncher::message, this, [this](const QString& text, bool error) {
        if (error) message_ = text;
        update();
    });
    connect(core_->engine(), &rg::EngineClient::controllersUpdated, this, [this] { update(); });
    connect(core_->engine(), &rg::EngineClient::connectedChanged, this, [this] { update(); });
    frame_.setInterval(33);
    connect(&frame_, &QTimer::timeout, this, [this] {
        // Bevestiging en vasthouden lopen ook zonder nieuwe aim-frames door (tijd).
        if (crosshair_visible_ && !confirm_exit_) {
            const int t = TargetAt(crosshair_);
            shot_ = confirm_.update(t, aim_state_.trigger && !ignore_trigger_until_release_, clock_.elapsed());
            if (shot_.launch >= 0) Activate(shot_.launch);
        }
        frame_.setInterval(Idle() > kIdleSaverMs ? 500 : 33);
        update();
    });
    Rebuild();
}

BigPictureWindow::~BigPictureWindow() = default;

void BigPictureWindow::Open() {
    showFullScreen();
    raise();
    activateWindow();
    setFocus();
}

void BigPictureWindow::showEvent(QShowEvent*) {
    aim_.start();
    pads_.start();
    frame_.start();
    Touch();
    confirm_.reset();
    shot_ = {};
    ignore_trigger_until_release_ = true;  // een schot uit de game telt niet in het menu
    Rebuild();
}

void BigPictureWindow::hideEvent(QHideEvent*) {
    aim_.stop();
    pads_.stop();
    frame_.stop();
}

void BigPictureWindow::closeEvent(QCloseEvent* e) {
    e->accept();
    emit ExitRequested();
}

void BigPictureWindow::Rebuild() {
    QString keep;
    if (selected_ >= 0 && selected_ < targets_.size() && targets_[selected_].kind == Kind::Game)
        keep = games_.value(targets_[selected_].game).id;
    games_ = rg::bigpicture::games(core_->library()->visibleGames(), *core_->gameData(), show_all_);
    icon_cache_.clear();
    Relayout();
    for (int i = 0; i < targets_.size(); ++i)
        if (targets_[i].kind == Kind::Game && games_.value(targets_[i].game).id == keep) selected_ = i;
    if (selected_ >= targets_.size()) selected_ = 0;
    update();
}

void BigPictureWindow::Relayout() {
    targets_.clear();
    const double W = width(), H = height();
    if (confirm_exit_) {
        const QSizeF b(W * 0.16, H * 0.075);
        targets_ << Target{Kind::ConfirmExit, -1, QRectF(QPointF(W / 2 - b.width() - W * 0.015, H * 0.55), b), tr("Exit")};
        targets_ << Target{Kind::Cancel, -1, QRectF(QPointF(W / 2 + W * 0.015, H * 0.55), b), tr("Cancel")};
        selected_ = std::clamp(selected_, 0, 1);
        return;
    }
    const QRectF grid(W * 0.05, H * 0.13, W * 0.90, H * 0.66);
    layout_ = rg::bigpicture::layout(grid, static_cast<int>(games_.size()), std::min(300.0, H * 0.24), std::max(90.0, H * 0.11), first_row_);
    const double cellH = layout_.tile * (1 + 0.22 + 0.30);
    visible_rows_ = cellH > 0 ? std::max(1, static_cast<int>(grid.height() / cellH)) : 1;
    for (int i = 0; i < games_.size(); ++i) targets_ << Target{Kind::Game, i, layout_.tiles.value(i), games_[i].displayName()};
    QList<Target> buttons;
    buttons << Target{Kind::ToggleAll, -1, {}, show_all_ ? tr("Show Playable Games") : tr("Show All Games")};
    if (core_->launcher()->canStop()) buttons << Target{Kind::StopGame, -1, {}, tr("Stop Game")};
    buttons << Target{Kind::Exit, -1, {}, tr("Exit")};
    const QSizeF b(W * 0.15, H * 0.062);
    const double gap = W * 0.02;
    double x = W / 2 - (buttons.size() * b.width() + (buttons.size() - 1) * gap) / 2;
    for (Target& t : buttons) {
        t.rect = QRectF(QPointF(x, H * 0.835), b);
        x += b.width() + gap;
        targets_ << t;
    }
    if (selected_ < 0 || selected_ >= targets_.size()) selected_ = 0;
}

void BigPictureWindow::resizeEvent(QResizeEvent*) {
    icon_cache_.clear();
    Relayout();
}

qint64 BigPictureWindow::Idle() const {
    if (idle_override_ >= 0) return idle_override_;
    return clock_.elapsed() - last_input_;
}

void BigPictureWindow::Touch() { last_input_ = clock_.elapsed(); }

bool BigPictureWindow::WakeIfIdle() {
    const bool wasIdle = Idle() > kIdleSaverMs;
    idle_override_ = -1;
    Touch();
    return wasIdle;
}

QPointF BigPictureWindow::DriftOffset() const {
    const qint64 idle = Idle();
    if (idle < kIdleDimMs) return {};
    // Langzame Lissajous-beweging (perioden van minuten), zodat geen pixel lang hetzelfde toont.
    const double t = clock_.elapsed() / 1000.0;
    const double a = std::min(1.0, (idle - kIdleDimMs) / 10000.0);  // zacht beginnen
    return QPointF(std::sin(t / 23.0) * 18 * a, std::cos(t / 37.0) * 12 * a);
}

int BigPictureWindow::TargetAt(const QPointF& p) const {
    QList<QRectF> tiles;
    for (const Target& t : targets_) tiles << (t.kind == Kind::Game ? t.rect : QRectF());
    const int g = rg::bigpicture::hitTest(tiles, p, std::max(8.0, height() * 0.012));
    if (g >= 0) return g;
    for (int i = 0; i < targets_.size(); ++i)
        if (targets_[i].kind != Kind::Game && targets_[i].rect.adjusted(-8, -8, 8, 8).contains(p)) return i;
    return -1;
}

void BigPictureWindow::OnAim(const rg::bigpicture::AimState& s) {
    // Het startscherm ligt erover (docs/prelaunch.md): de trekker is daar voor de kalibratie, niet voor het menu eronder.
    if (PreLaunchWindow::Active()) {
        aim_state_ = s;
        ignore_trigger_until_release_ = true;
        confirm_.reset();
        crosshair_visible_ = false;
        return;
    }
    const rg::bigpicture::AimState prev = aim_state_;
    aim_state_ = s;
    if (!s.trigger) ignore_trigger_until_release_ = false;
    crosshair_visible_ = s.valid && !s.offScreen;
    const QPointF pos(s.pos.x() * width(), s.pos.y() * height());
    const bool moved = std::hypot(s.pos.x() - prev.pos.x(), s.pos.y() - prev.pos.y()) > 0.01;
    const bool pressed = s.triggerPresses > 0 || (s.trigger && !prev.trigger);
    if ((moved && s.valid) || pressed || (s.ps && !prev.ps)) {
        if (WakeIfIdle()) {
            ignore_trigger_until_release_ = s.trigger;  // dit schot wekt alleen het scherm
            return;
        }
    }
    if (!crosshair_visible_) {
        confirm_.update(-1, false, clock_.elapsed());
        update();
        return;
    }
    crosshair_ = pos;
    const int t = TargetAt(pos);
    if (t >= 0 && !(confirm_exit_ && targets_[t].kind == Kind::Game)) selected_ = t;
    if (ignore_trigger_until_release_) {
        update();
        return;
    }
    // Een snelle tik tussen twee frames (in én uit) telt ook als schot.
    if (s.triggerPresses > 0 && !s.trigger && !prev.trigger) {
        shot_ = confirm_.update(t, true, clock_.elapsed());
        if (shot_.launch < 0) shot_ = confirm_.update(t, false, clock_.elapsed());
    } else {
        shot_ = confirm_.update(t, s.trigger, clock_.elapsed());
    }
    if (shot_.launch >= 0) Activate(shot_.launch);
    update();
}

void BigPictureWindow::InjectAim(const QPointF& norm, bool valid, bool trigger) {
    rg::bigpicture::AimState s;
    s.valid = valid;
    s.pos = norm;
    s.trigger = trigger;
    s.link = QStringLiteral("fresh");
    ignore_trigger_until_release_ = false;
    OnAim(s);
}

void BigPictureWindow::SelectIndex(int index) {
    selected_ = std::clamp(index, 0, std::max(0, static_cast<int>(targets_.size()) - 1));
    update();
}

QPointF BigPictureWindow::TileCenterNorm(int index) const {
    if (index < 0 || index >= targets_.size() || width() <= 0 || height() <= 0) return {0.5, 0.5};
    const QPointF c = targets_[index].rect.center();
    return {c.x() / width(), c.y() / height()};
}

void BigPictureWindow::SetIdleForScreenshot(qint64 idleMs) {
    idle_override_ = idleMs;
    update();
}

void BigPictureWindow::OnGamepad(int action) {
    using A = rg::GamepadInput::Action;
    if (PreLaunchWindow::Active()) return;
    if (WakeIfIdle()) return;
    switch (static_cast<A>(action)) {
        case A::Left: MoveSelection(rg::bigpicture::Move::Left); break;
        case A::Right: MoveSelection(rg::bigpicture::Move::Right); break;
        case A::Up: MoveSelection(rg::bigpicture::Move::Up); break;
        case A::Down: MoveSelection(rg::bigpicture::Move::Down); break;
        case A::Accept: Activate(selected_); break;
        case A::Back:
        case A::Menu:
            confirm_exit_ = !confirm_exit_;
            selected_ = confirm_exit_ ? 1 : 0;
            confirm_.reset();
            shot_ = {};
            Relayout();
            break;
        case A::None: break;
    }
    update();
}

void BigPictureWindow::keyPressEvent(QKeyEvent* e) {
    if (PreLaunchWindow::Active()) return;
    if (WakeIfIdle()) return;
    switch (e->key()) {
        case Qt::Key_Left: MoveSelection(rg::bigpicture::Move::Left); break;
        case Qt::Key_Right: MoveSelection(rg::bigpicture::Move::Right); break;
        case Qt::Key_Up: MoveSelection(rg::bigpicture::Move::Up); break;
        case Qt::Key_Down: MoveSelection(rg::bigpicture::Move::Down); break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space: Activate(selected_); break;
        case Qt::Key_Escape:
        case Qt::Key_Backspace:
            confirm_exit_ = !confirm_exit_;
            selected_ = confirm_exit_ ? 1 : 0;  // Cancel voorgeselecteerd
            confirm_.reset();
            shot_ = {};
            Relayout();
            break;
        default: QWidget::keyPressEvent(e); return;
    }
    update();
}

void BigPictureWindow::mouseMoveEvent(QMouseEvent* e) {
    const QPointF p = e->position();
    if (mouse_seen_ && (p - last_mouse_).manhattanLength() < 3) return;
    last_mouse_ = p;
    if (!mouse_seen_) {
        mouse_seen_ = true;
        return;  // het eerste event komt ook bij het openen van het venster
    }
    WakeIfIdle();
    setCursor(Qt::ArrowCursor);
    const int t = TargetAt(p);
    if (t >= 0) selected_ = t;
    update();
}

void BigPictureWindow::mousePressEvent(QMouseEvent* e) {
    if (WakeIfIdle()) return;
    if (e->button() != Qt::LeftButton) return;
    const int t = TargetAt(e->position());
    if (t >= 0) {
        selected_ = t;
        Activate(t);  // een muisklik is bewust: meteen
    }
}

void BigPictureWindow::MoveSelection(rg::bigpicture::Move m) {
    using rg::bigpicture::Move;
    if (targets_.isEmpty()) return;
    const int nGames = static_cast<int>(games_.size());
    if (confirm_exit_) {
        if (m == Move::Left) selected_ = 0;
        if (m == Move::Right) selected_ = 1;
        return;
    }
    const bool onGame = selected_ < nGames;
    if (onGame) {
        const int n = rg::bigpicture::navigate(selected_, m, layout_.columns, nGames);
        if (m == Move::Down && n == selected_) selected_ = nGames;  // naar de knoppen
        else selected_ = n;
    } else {
        if (m == Move::Left && selected_ > nGames) --selected_;
        else if (m == Move::Right && selected_ + 1 < targets_.size()) ++selected_;
        else if (m == Move::Up && nGames > 0) selected_ = nGames - 1;
    }
    if (selected_ < nGames) {
        const int fr = rg::bigpicture::scrollRowFor(selected_, layout_.columns, visible_rows_, first_row_);
        if (fr != first_row_) {
            first_row_ = fr;
            Relayout();
        }
    }
}

void BigPictureWindow::Activate(int index) {
    if (index < 0 || index >= targets_.size()) return;
    const Target t = targets_[index];
    confirm_.reset();
    shot_ = {};
    ignore_trigger_until_release_ = aim_state_.trigger;
    switch (t.kind) {
        case Kind::Game: {
            if (core_->launcher()->canStop()) {
                message_ = tr("A game is already running. Stop it first.");
                break;
            }
            const rg::GameEntry g = games_.value(t.game);
            message_ = tr("Starting %1...").arg(g.displayName());
            // Het startscherm (PS Move aan, kalibratie per speler) als het nodig is, anders meteen; docs/prelaunch.md.
            if (PreLaunchWindow* w = PlayWithPreLaunch(core_, g))
                connect(w, &PreLaunchWindow::Finished, this, [this](int code) {
                    ignore_trigger_until_release_ = true;
                    if (code == PreLaunchWindow::kExitCancelled) message_.clear();
                    update();
                });
            break;
        }
        case Kind::ToggleAll:
            show_all_ = !show_all_;
            settings().setValue(QStringLiteral("fullscreen/show_all"), show_all_);
            first_row_ = 0;
            Rebuild();
            for (int i = 0; i < targets_.size(); ++i)
                if (targets_[i].kind == Kind::ToggleAll) selected_ = i;
            break;
        case Kind::StopGame:
            core_->launcher()->stop();
            message_ = tr("Stopping the game...");
            break;
        case Kind::Exit:
            confirm_exit_ = true;
            selected_ = 1;
            Relayout();
            break;
        case Kind::ConfirmExit:
            emit ExitRequested();
            break;
        case Kind::Cancel:
            confirm_exit_ = false;
            selected_ = 0;
            Relayout();
            break;
    }
    update();
}

void BigPictureWindow::OnLauncherState() {
    using S = rg::GameLauncher::State;
    const S s = core_->launcher()->state();
    last_state_ = s;
    switch (s) {
        case S::WaitingMove:
        case S::Starting:
        case S::Stopping: message_ = core_->launcher()->text(); break;
        case S::Running:
            message_.clear();
            // Tijdens de game niets tonen: het venster verbergt zich (ook onder gamescope komt de game dan in beeld).
            if (hide_during_game_ && isVisible()) {
                hidden_for_game_ = true;
                hide();
            }
            break;
        case S::Failed:
        case S::Idle:
            message_ = s == S::Failed ? core_->launcher()->text() : QString();
            if (hidden_for_game_) {
                hidden_for_game_ = false;
                Open();  // de game is gestopt: terug naar het menu
            }
            break;
        case S::EngineOffline:
            // Een game die nog loopt houdt het scherm; anders melden.
            if (!hidden_for_game_) message_ = tr("The engine is not running.");
            break;
    }
    Relayout();
    update();
}

QPixmap BigPictureWindow::IconFor(const rg::GameEntry& g, int size) {
    const QString key = g.id + QLatin1Char('@') + QString::number(size);
    auto it = icon_cache_.find(key);
    if (it != icon_cache_.end()) return *it;
    QPixmap pm;
    if (!g.iconPath.isEmpty() && QFileInfo(g.iconPath).isFile()) {
        QPixmap src(g.iconPath);
        if (!src.isNull()) pm = src.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (pm.isNull()) {
        // Zonder icoon: geen gevuld vlak (OLED), alleen een gekleurde ring met de beginletter.
        pm = QPixmap(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor c = QColor::fromHsv(static_cast<int>(qHash(g.displayName()) % 360), 110, 140);
        QPen pen(c);
        pen.setWidthF(std::max(2.0, size * 0.025));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(size * 0.06, size * 0.06, size * 0.88, size * 0.88));
        QFont f;
        f.setPixelSize(size * 2 / 5);
        p.setFont(f);
        const QString letter = g.displayName().trimmed().isEmpty() ? QStringLiteral("?") : g.displayName().trimmed().left(1).toUpper();
        p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, letter);
    }
    icon_cache_.insert(key, pm);
    return pm;
}

void BigPictureWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const double W = width(), H = height();
    const qint64 idle = Idle();

    if (idle > kIdleSaverMs) {
        // Schermbeveiliging: alleen een klein, langzaam bewegend tekstje.
        const double t = clock_.elapsed() / 1000.0;
        QFont f = font();
        f.setPixelSize(static_cast<int>(H * 0.022));
        p.setFont(f);
        p.setPen(kFaint);
        const QString text = tr("RavenGun - shoot or press a key");
        const double tw = p.fontMetrics().horizontalAdvance(text);
        const QPointF at(W * 0.1 + (W * 0.8 - tw) * (0.5 + 0.5 * std::sin(t / 41.0)), H * 0.15 + H * 0.7 * (0.5 + 0.5 * std::sin(t / 29.0)));
        p.drawText(at, text);
        return;
    }

    const bool dim = idle > kIdleDimMs;
    p.save();
    p.translate(DriftOffset());

    // Kop: naam links, controllers rechts.
    QFont small = font();
    small.setPixelSize(static_cast<int>(H * 0.024));
    p.setFont(small);
    p.setPen(kDim);
    p.drawText(QRectF(W * 0.05, H * 0.03, W * 0.4, H * 0.05), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("RavenGun"));
    DrawStatus(p);

    if (!message_.isEmpty()) {
        QFont mf = font();
        mf.setPixelSize(static_cast<int>(H * 0.03));
        p.setFont(mf);
        p.setPen(kText);
        p.drawText(QRectF(W * 0.1, H * 0.075, W * 0.8, H * 0.05), Qt::AlignCenter, message_);
    }

    // Tegels.
    QFont titleFont = font();
    titleFont.setPixelSize(static_cast<int>(std::clamp(layout_.tile * 0.11, H * 0.016, H * 0.03)));
    const int nGames = static_cast<int>(games_.size());
    if (nGames == 0 && !confirm_exit_) {
        QFont ef = font();
        ef.setPixelSize(static_cast<int>(H * 0.03));
        p.setFont(ef);
        p.setPen(kDim);
        p.drawText(QRectF(0, H * 0.35, W, H * 0.1), Qt::AlignCenter,
                   show_all_ ? tr("No games yet. Add a game folder in the RavenGun app on the desktop.")
                             : tr("No playable games yet. Choose \"Show All Games\" below."));
    }
    for (int i = 0; i < targets_.size() && !confirm_exit_; ++i) {
        const Target& t = targets_[i];
        if (t.kind != Kind::Game || t.rect.isNull()) continue;
        const bool sel = i == selected_;
        QRectF r = t.rect;
        if (sel) r = r.adjusted(-r.width() * 0.04, -r.height() * 0.04, r.width() * 0.04, r.height() * 0.04);
        p.setOpacity(dim ? 0.35 : (sel ? 1.0 : 0.62));
        const int px = static_cast<int>(r.width() * devicePixelRatioF());
        QPixmap pm = IconFor(games_[t.game], px);
        pm.setDevicePixelRatio(devicePixelRatioF());
        const QSizeF ps = pm.deviceIndependentSize();
        p.drawPixmap(QPointF(r.center().x() - ps.width() / 2, r.center().y() - ps.height() / 2), pm);
        p.setOpacity(1.0);
        if (sel && !dim) {
            QPen pen(shot_.armed == i ? kRed : kRedDim);
            pen.setWidthF(std::max(2.0, H * 0.004));
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(-6, -6, 6, 6), 10, 10);
            if (shot_.hold > 0) {
                QPen hp(kRed);
                hp.setWidthF(std::max(3.0, H * 0.006));
                p.setPen(hp);
                const QRectF ring = r.adjusted(-14, -14, 14, 14);
                p.drawArc(ring, 90 * 16, -static_cast<int>(360 * 16 * shot_.hold));
            }
        }
        p.setFont(titleFont);
        p.setPen(sel && !dim ? kText : dim ? kFaint : kDim);
        const QRectF label(t.rect.left() - t.rect.width() * 0.1, t.rect.bottom() + t.rect.height() * 0.06, t.rect.width() * 1.2,
                           t.rect.height() * 0.24);
        QString text = p.fontMetrics().elidedText(t.label, Qt::ElideRight, static_cast<int>(label.width()));
        if (sel && shot_.armed == i) {
            p.setPen(kRed);
            text = tr("Shoot again to start");
        }
        p.drawText(label, Qt::AlignHCenter | Qt::AlignTop, text);
    }

    // Knoppen (alleen omlijnd: geen lichte vlakken).
    if (confirm_exit_) {
        QFont qf = font();
        qf.setPixelSize(static_cast<int>(H * 0.04));
        p.setFont(qf);
        p.setPen(kText);
        p.drawText(QRectF(0, H * 0.38, W, H * 0.1), Qt::AlignCenter, tr("Exit RavenGun?"));
        QFont hf = font();
        hf.setPixelSize(static_cast<int>(H * 0.022));
        p.setFont(hf);
        p.setPen(kDim);
        p.drawText(QRectF(0, H * 0.46, W, H * 0.05), Qt::AlignCenter,
                   core_->launcher()->canStop() ? tr("The running game keeps running.") : tr("You can start RavenGun again from your library."));
    }
    QFont bf = font();
    bf.setPixelSize(static_cast<int>(H * 0.024));
    for (int i = 0; i < targets_.size(); ++i) {
        const Target& t = targets_[i];
        if (t.kind == Kind::Game) continue;
        const bool sel = i == selected_ && !dim;
        QPen pen(sel ? (shot_.armed == i ? kRed : kRedDim) : kFaint);
        pen.setWidthF(sel ? std::max(2.0, H * 0.003) : 1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(t.rect, t.rect.height() * 0.25, t.rect.height() * 0.25);
        if (sel && shot_.hold > 0) {
            QPen hp(kRed);
            hp.setWidthF(std::max(3.0, H * 0.005));
            p.setPen(hp);
            p.drawLine(QPointF(t.rect.left() + 6, t.rect.bottom() + 6), QPointF(t.rect.left() + 6 + (t.rect.width() - 12) * shot_.hold, t.rect.bottom() + 6));
        }
        p.setFont(bf);
        p.setPen(sel ? kText : dim ? kFaint : kDim);
        p.drawText(t.rect, Qt::AlignCenter, sel && shot_.armed == i ? tr("Shoot again") : t.label);
    }

    // Hulptekst onderaan.
    QFont hint = font();
    hint.setPixelSize(static_cast<int>(H * 0.018));
    p.setFont(hint);
    p.setPen(kFaint);
    p.drawText(QRectF(0, H * 0.93, W, H * 0.04), Qt::AlignCenter,
               tr("Shoot a game, then shoot again or hold the trigger to start   |   PS button: center the aim   |   "
                  "Arrows and Enter, or a gamepad, work too"));
    p.restore();

    if (crosshair_visible_ && !dim) DrawCrosshair(p);
}

void BigPictureWindow::DrawStatus(QPainter& p) {
    const double W = width(), H = height();
    const auto* e = core_->engine();
    QStringList parts;
    double x = W * 0.95;
    const double y = H * 0.055;
    QFont f = font();
    f.setPixelSize(static_cast<int>(H * 0.02));
    p.setFont(f);
    if (!e->isConnected()) {
        p.setPen(kRedDim);
        p.drawText(QRectF(W * 0.5, H * 0.03, W * 0.45, H * 0.05), Qt::AlignRight | Qt::AlignVCenter, tr("Engine stopped"));
        return;
    }
    // Van rechts naar links: per Move (en Navigation Controller) speler, batterij en verbinding.
    QList<QJsonObject> ctl;
    for (const auto& v : e->controllers().value(QStringLiteral("controllers")).toArray()) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("removed")).toBool() || c.value(QStringLiteral("player")).toInt() <= 0) continue;
        ctl << c;
    }
    std::sort(ctl.begin(), ctl.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value(QStringLiteral("player")).toInt() > b.value(QStringLiteral("player")).toInt();
    });
    if (ctl.isEmpty()) {
        p.setPen(kDim);
        p.drawText(QRectF(W * 0.5, H * 0.03, W * 0.45, H * 0.05), Qt::AlignRight | Qt::AlignVCenter, tr("No PS Move paired"));
        return;
    }
    for (const QJsonObject& c : ctl) {
        const bool on = c.value(QStringLiteral("connected")).toBool();
        const bool nav = c.value(QStringLiteral("type")).toString() == QStringLiteral("navigation");
        const double frac = on ? batteryFraction(c.value(QStringLiteral("battery")).toObject()) : -1;
        const bool charging = c.value(QStringLiteral("battery")).toObject().value(QStringLiteral("charging")).toBool();
        // Batterijsymbool (omlijnd, gevuld naar niveau, gedempt).
        const QSizeF bs(H * 0.032, H * 0.016);
        x -= bs.width() + 3;
        const QRectF br(x, y - bs.height() / 2, bs.width(), bs.height());
        p.setPen(QPen(on ? kDim : kFaint, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(br, 2, 2);
        p.fillRect(QRectF(br.right() + 1, br.center().y() - bs.height() / 4, 2, bs.height() / 2), on ? kDim : kFaint);
        if (frac >= 0) {
            const QColor fill = frac < 0.25 ? kRedDim : QColor(0x55, 0x55, 0x55);
            p.fillRect(QRectF(br.left() + 2, br.top() + 2, (br.width() - 4) * frac, br.height() - 4), fill);
        }
        QString label = QStringLiteral("P%1%2").arg(c.value(QStringLiteral("player")).toInt()).arg(nav ? tr(" Nav") : QString());
        if (!on) label += tr(" off");
        else if (charging) label += tr(" charging");
        const double tw = p.fontMetrics().horizontalAdvance(label);
        x -= tw + 6;
        p.setPen(on ? kDim : kFaint);
        p.drawText(QPointF(x, y + p.fontMetrics().ascent() / 2.0 - 1), label);
        x -= W * 0.02;
    }
}

void BigPictureWindow::DrawCrosshair(QPainter& p) {
    const double r = std::max(10.0, height() * 0.018);
    const QPointF c = crosshair_;
    const double w = std::max(2.0, height() * 0.0028);
    // Eerst een zwarte rand, dan rood: zichtbaar op elk icoon.
    for (int pass = 0; pass < 2; ++pass) {
        QPen pen(pass == 0 ? QColor(Qt::black) : kRed);
        pen.setWidthF(pass == 0 ? w + 3 : w);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r, r);
        p.drawLine(QPointF(c.x(), c.y() - r * 1.7), QPointF(c.x(), c.y() - r * 0.45));
        p.drawLine(QPointF(c.x(), c.y() + r * 0.45), QPointF(c.x(), c.y() + r * 1.7));
        p.drawLine(QPointF(c.x() - r * 1.7, c.y()), QPointF(c.x() - r * 0.45, c.y()));
        p.drawLine(QPointF(c.x() + r * 0.45, c.y()), QPointF(c.x() + r * 1.7, c.y()));
    }
    if (aim_state_.trigger) {
        p.setBrush(kRed);
        p.drawEllipse(c, r * 0.18, r * 0.18);
    }
}

}  // namespace rg::ui
