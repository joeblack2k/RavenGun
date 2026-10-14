// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BigPicture.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <algorithm>
#include <cmath>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "core/AppPaths.h"
#include "core/GameLauncher.h"

namespace rg {

namespace bigpicture {

QList<GameEntry> games(const QList<GameEntry>& all, const GameData& data, bool showAll) {
    QList<GameEntry> out;
    for (const GameEntry& g : all) {
        if (!GameLauncher::whyNotPlayable(g).isEmpty() || !g.exeExists) continue;
        const QString st = data.playState(g.profile);
        if (!showAll && st != QStringLiteral("playable") && st != QStringLiteral("working")) continue;
        out << g;
    }
    std::stable_sort(out.begin(), out.end(), [&data](const GameEntry& a, const GameEntry& b) {
        auto good = [&data](const GameEntry& e) { const QString s = data.playState(e.profile); return s == QStringLiteral("playable") || s == QStringLiteral("working"); };
        const bool pa = good(a);
        const bool pb = good(b);
        if (pa != pb) return pa;
        return a.displayName().compare(b.displayName(), Qt::CaseInsensitive) < 0;
    });
    return out;
}

Layout layout(const QRectF& area, int count, double maxTile, double minTile, int firstRow) {
    Layout l;
    if (count <= 0 || area.width() <= 0 || area.height() <= 0) return l;
    const double gap = 0.22;     // ruimte tussen tegels als deel van de tegel
    const double label = 0.30;   // ruimte voor de titel onder het icoon
    // Grootste tegel waarbij alles past; anders de minimale tegel en scrollen.
    double best = minTile;
    int bestCols = std::max(1, static_cast<int>(area.width() / (minTile * (1 + gap))));
    for (int cols = 1; cols <= count; ++cols) {
        const int rows = (count + cols - 1) / cols;
        const double byW = area.width() / (cols * (1 + gap));
        const double byH = area.height() / (rows * (1 + gap + label));
        const double t = std::min({byW, byH, maxTile});
        if (t > best + 0.5) {
            best = t;
            bestCols = cols;
        }
    }
    l.tile = best;
    l.columns = std::min(bestCols, count);
    const double cellW = best * (1 + gap), cellH = best * (1 + gap + label);
    const int rows = (count + l.columns - 1) / l.columns;
    const int visibleRows = std::max(1, static_cast<int>(area.height() / cellH));
    const double usedW = l.columns * cellW;
    const double usedH = std::min(rows, visibleRows) * cellH;
    const double x0 = area.left() + (area.width() - usedW) / 2 + best * gap / 2;
    const double y0 = area.top() + (area.height() - usedH) / 2 + best * gap / 2;
    for (int i = 0; i < count; ++i) {
        const int row = i / l.columns - firstRow;
        const int col = i % l.columns;
        if (row < 0 || row >= visibleRows) {
            l.tiles << QRectF();
            continue;
        }
        // Laatste, onvolledige rij centreren.
        const int inRow = (i / l.columns == rows - 1) ? count - (rows - 1) * l.columns : l.columns;
        const double rowShift = (l.columns - inRow) * cellW / 2;
        l.tiles << QRectF(x0 + rowShift + col * cellW, y0 + row * cellH, best, best);
    }
    return l;
}

int hitTest(const QList<QRectF>& tiles, const QPointF& p, double margin) {
    int best = -1;
    double bestD = 1e18;
    for (int i = 0; i < tiles.size(); ++i) {
        const QRectF& r = tiles[i];
        if (r.isNull()) continue;
        // De titel onder het icoon hoort er ook bij.
        const QRectF hit = r.adjusted(-margin, -margin, margin, margin + r.height() * 0.3);
        if (!hit.contains(p)) continue;
        const QPointF d = p - r.center();
        const double dd = d.x() * d.x() + d.y() * d.y();
        if (dd < bestD) {
            bestD = dd;
            best = i;
        }
    }
    return best;
}

int navigate(int index, Move m, int columns, int count) {
    if (count <= 0) return -1;
    columns = std::max(1, columns);
    if (index < 0 || index >= count) return 0;
    switch (m) {
        case Move::Left: return index > 0 ? index - 1 : index;
        case Move::Right: return index + 1 < count ? index + 1 : index;
        case Move::Up: return index - columns >= 0 ? index - columns : index;
        case Move::Down: {
            if (index + columns < count) return index + columns;
            // Onder in een onvolledige laatste rij: naar het laatste item als er nog een rij is.
            const int lastRow = (count - 1) / columns;
            return index / columns < lastRow ? count - 1 : index;
        }
    }
    return index;
}

int scrollRowFor(int index, int columns, int visibleRows, int firstRow) {
    if (index < 0) return firstRow;
    const int row = index / std::max(1, columns);
    if (row < firstRow) return row;
    if (row >= firstRow + visibleRows) return row - visibleRows + 1;
    return firstRow;
}

ShotConfirm::Result ShotConfirm::update(int target, bool triggerDown, qint64 nowMs) {
    Result r;
    if (armed_ >= 0 && nowMs - armedAt_ > kConfirmMs && !triggerDown) armed_ = -1;
    if (triggerDown && !wasDown_) {
        consumed_ = false;
        holdTarget_ = target;
        downAt_ = nowMs;
        if (target < 0) {
            armed_ = -1;
            consumed_ = true;
        } else if (armed_ == target && nowMs - armedAt_ <= kConfirmMs) {
            r.launch = target;
            armed_ = -1;
            consumed_ = true;
        } else {
            armed_ = target;
            armedAt_ = nowMs;
        }
    } else if (triggerDown && !consumed_) {
        if (target != holdTarget_) {
            holdTarget_ = -1;  // vizier verlaten: vasthouden telt niet meer
        } else if (holdTarget_ >= 0) {
            r.hold = std::min(1.0, static_cast<double>(nowMs - downAt_) / kHoldMs);
            if (r.hold >= 1.0) {
                r.launch = holdTarget_;
                armed_ = -1;
                consumed_ = true;
                r.hold = 0;
            }
        }
    }
    if (!triggerDown && wasDown_ && armed_ >= 0) armedAt_ = nowMs;  // de bevestigingstijd telt vanaf het loslaten
    wasDown_ = triggerDown;
    r.armed = armed_;
    return r;
}

void ShotConfirm::reset() {
    wasDown_ = false;
    armed_ = -1;
    holdTarget_ = -1;
    consumed_ = false;
}

bool parseAimEvent(const QJsonObject& line, AimState* out) {
    const QString ev = line.value(QStringLiteral("event")).toString();
    const QJsonObject data = line.value(QStringLiteral("data")).toObject();
    AimState s;
    QJsonObject aim, pressed;
    QJsonArray down;
    if (ev == QStringLiteral("aim")) {
        aim = data.value(QStringLiteral("aim")).toObject();
        down = data.value(QStringLiteral("down")).toArray();
        pressed = data.value(QStringLiteral("pressed")).toObject();
        s.link = data.value(QStringLiteral("link")).toString();
        s.pos = QPointF(aim.value(QStringLiteral("x")).toDouble(0.5), aim.value(QStringLiteral("y")).toDouble(0.5));
    } else if (ev == QStringLiteral("sample")) {
        const QJsonObject sample = data.value(QStringLiteral("sample")).toObject();
        aim = sample.value(QStringLiteral("aim")).toObject();
        down = sample.value(QStringLiteral("buttons")).toObject().value(QStringLiteral("down")).toArray();
        pressed = data.value(QStringLiteral("edges_since_last_frame")).toObject().value(QStringLiteral("pressed")).toObject();
        s.link = sample.value(QStringLiteral("link")).toString();
        s.pos = QPointF(aim.value(QStringLiteral("x_clamped")).toDouble(0.5), aim.value(QStringLiteral("y_clamped")).toDouble(0.5));
    } else {
        return false;
    }
    s.valid = aim.value(QStringLiteral("valid")).toBool();
    s.offScreen = aim.value(QStringLiteral("off_screen")).toBool();
    s.pos.setX(std::clamp(s.pos.x(), 0.0, 1.0));
    s.pos.setY(std::clamp(s.pos.y(), 0.0, 1.0));
    for (const auto& b : down) {
        if (b.toString() == QStringLiteral("t")) s.trigger = true;
        if (b.toString() == QStringLiteral("ps")) s.ps = true;
    }
    s.triggerPresses = pressed.value(QStringLiteral("t")).toInt();
    if (s.link == QStringLiteral("disconnected") || s.link == QStringLiteral("lost")) s.valid = false;
    *out = s;
    return true;
}

}  // namespace bigpicture

// --- AimStream --------------------------------------------------------------------------------------------------------

AimStream::AimStream(QString socketPath, QObject* parent)
    : QObject(parent), path_(socketPath.isEmpty() ? AppPaths::socketPath() : std::move(socketPath)) {
    qRegisterMetaType<rg::bigpicture::AimState>();
    reconnect_.setInterval(2000);
    connect(&reconnect_, &QTimer::timeout, this, &AimStream::connectNow);
    connect(&sock_, &QLocalSocket::connected, this, [this] {
        buf_.clear();
        subscribe();
        emit connectedChanged(true);
    });
    connect(&sock_, &QLocalSocket::disconnected, this, [this] {
        state_ = {};
        emit aimChanged(state_);
        emit connectedChanged(false);
    });
    connect(&sock_, &QLocalSocket::readyRead, this, &AimStream::onReadyRead);
}

void AimStream::start() {
    running_ = true;
    reconnect_.start();
    connectNow();
}

void AimStream::stop() {
    running_ = false;
    reconnect_.stop();
    sock_.abort();
}

void AimStream::connectNow() {
    if (!running_ || path_.isEmpty() || sock_.state() != QLocalSocket::UnconnectedState) return;
    sock_.connectToServer(path_, QIODevice::ReadWrite);
}

void AimStream::subscribe() {
    QJsonObject o{{QStringLiteral("v"), 1}, {QStringLiteral("cmd"), QStringLiteral("watch")}, {QStringLiteral("rate_hz"), 60},
                  {QStringLiteral("player"), 1}, {QStringLiteral("id"), lite_ ? 1 : 2}};
    if (lite_) o.insert(QStringLiteral("lite"), true);
    sock_.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
}

void AimStream::onReadyRead() {
    buf_ += sock_.readAll();
    if (buf_.size() > 4 * 1024 * 1024) {
        sock_.abort();
        return;
    }
    int nl;
    bool changed = false;
    while ((nl = static_cast<int>(buf_.indexOf('\n'))) >= 0) {
        const QJsonObject o = QJsonDocument::fromJson(buf_.left(nl)).object();
        buf_.remove(0, nl + 1);
        if (o.contains(QStringLiteral("event"))) {
            bigpicture::AimState s;
            if (bigpicture::parseAimEvent(o, &s)) {
                // Flanken optellen over frames die in één keer binnenkomen.
                s.triggerPresses += changed ? state_.triggerPresses : 0;
                state_ = s;
                changed = true;
            }
            continue;
        }
        // Antwoord op watch: een oudere engine kent `lite` niet -> volledige frames.
        if (!o.value(QStringLiteral("ok")).toBool() && lite_ &&
            o.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("bad_request")) {
            lite_ = false;
            subscribe();
        }
    }
    if (changed) emit aimChanged(state_);
}

// --- GamepadInput -----------------------------------------------------------------------------------------------------

GamepadInput::GamepadInput(QObject* parent) : QObject(parent) {
    rescan_.setInterval(3000);
    connect(&rescan_, &QTimer::timeout, this, &GamepadInput::scan);
}

GamepadInput::~GamepadInput() { stop(); }

bool GamepadInput::wanted(quint16 vendor, quint16 product, bool hasGamepadButtons) {
    if (!hasGamepadButtons) return false;
    if (vendor == 0x1209 && (product & 0xFF00) == 0x7200) return false;               // eigen virtuele apparaten
    if (vendor == 0x054c && (product == 0x03d5 || product == 0x0c5e)) return false;   // PS Move (bedient het vizier)
    return true;
}

GamepadInput::Action GamepadInput::map(int type, int code, int value, AxisState* st, int absMin, int absMax) {
    if (type == EV_KEY) {
        if (value != 1) return Action::None;  // alleen indrukken (geen herhaling, geen loslaten)
        switch (code) {
            case BTN_SOUTH: return Action::Accept;
            case BTN_EAST: return Action::Back;
            case BTN_START:
            case BTN_MODE: return Action::Menu;
            case BTN_DPAD_UP: return Action::Up;
            case BTN_DPAD_DOWN: return Action::Down;
            case BTN_DPAD_LEFT: return Action::Left;
            case BTN_DPAD_RIGHT: return Action::Right;
            default: return Action::None;
        }
    }
    if (type != EV_ABS || !st) return Action::None;
    int dir = 0;
    if (code == ABS_HAT0X || code == ABS_HAT0Y) {
        dir = value < 0 ? -1 : value > 0 ? 1 : 0;
    } else if (code == ABS_X || code == ABS_Y) {
        const double mid = (absMin + absMax) / 2.0, half = std::max(1.0, (absMax - absMin) / 2.0);
        const double v = (value - mid) / half;
        int* cur = code == ABS_X ? &st->x : &st->y;
        // Hysterese: aan boven 0,6, uit onder 0,35.
        if (*cur == 0) dir = v > 0.6 ? 1 : v < -0.6 ? -1 : 0;
        else dir = std::fabs(v) < 0.35 ? 0 : *cur;
    } else {
        return Action::None;
    }
    const bool horizontal = code == ABS_HAT0X || code == ABS_X;
    int* cur = horizontal ? &st->x : &st->y;
    if (dir == *cur) return Action::None;
    *cur = dir;
    if (dir == 0) return Action::None;
    if (horizontal) return dir < 0 ? Action::Left : Action::Right;
    return dir < 0 ? Action::Up : Action::Down;
}

void GamepadInput::start(const QString& inputDir) {
    if (qEnvironmentVariableIntValue("RAVENGUN_NO_GAMEPAD") == 1) return;
    dir_ = inputDir;
    scan();
    rescan_.start();
}

void GamepadInput::stop() {
    rescan_.stop();
    for (Dev& d : devs_) {
        delete d.notifier;
        if (d.fd >= 0) ::close(d.fd);
    }
    devs_.clear();
}

void GamepadInput::scan() {
    // Weggevallen apparaten opruimen.
    for (int i = static_cast<int>(devs_.size()) - 1; i >= 0; --i) {
        if (!QFileInfo::exists(devs_[i].path)) {
            delete devs_[i].notifier;
            ::close(devs_[i].fd);
            devs_.removeAt(i);
        }
    }
    const QDir d(dir_);
    for (const QString& name : d.entryList({QStringLiteral("event*")}, QDir::System)) {
        const QString path = d.filePath(name);
        if (std::any_of(devs_.begin(), devs_.end(), [&path](const Dev& x) { return x.path == path; })) continue;
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;  // geen leesrecht: overslaan
        input_id id{};
        unsigned long keys[(KEY_MAX + 1) / (8 * sizeof(long)) + 1] = {};
        bool ok = ::ioctl(fd, EVIOCGID, &id) == 0 && ::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0;
        auto has = [&keys](int code) { return (keys[code / (8 * sizeof(long))] >> (code % (8 * sizeof(long)))) & 1UL; };
        ok = ok && wanted(id.vendor, id.product, has(BTN_SOUTH) || has(BTN_GAMEPAD));
        if (!ok) {
            ::close(fd);
            continue;
        }
        Dev dev;
        dev.fd = fd;
        dev.path = path;
        input_absinfo ai{};
        if (::ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0 && ai.maximum > ai.minimum) {
            dev.absMin = ai.minimum;
            dev.absMax = ai.maximum;
        }
        dev.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(dev.notifier, &QSocketNotifier::activated, this, [this, fd] { readDev(fd); });
        devs_ << dev;
    }
}

void GamepadInput::readDev(int fd) {
    static QElapsedTimer clock;
    static Action lastAction = Action::None;
    static qint64 lastAt = 0;
    if (!clock.isValid()) clock.start();
    Dev* dev = nullptr;
    for (Dev& d : devs_)
        if (d.fd == fd) dev = &d;
    if (!dev) return;
    input_event ev[32];
    while (true) {
        const ssize_t n = ::read(fd, ev, sizeof ev);
        if (n <= 0) break;
        for (size_t i = 0; i < static_cast<size_t>(n) / sizeof(input_event); ++i) {
            const Action a = map(ev[i].type, ev[i].code, ev[i].value, &dev->axes, dev->absMin, dev->absMax);
            if (a == Action::None) continue;
            // Twee apparaten voor dezelfde knoppen (Steam Deck en de virtuele pad van Steam): dubbele acties overslaan.
            const qint64 now = clock.elapsed();
            if (a == lastAction && now - lastAt < 60) continue;
            lastAction = a;
            lastAt = now;
            emit action(a);
        }
    }
}

}  // namespace rg
