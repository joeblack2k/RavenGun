// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Kern van de fullscreen-modus ("big picture", A7) zonder widgets: welke games, waar de tegels staan, welke tegel onder het
// vizier ligt, de bevestiging van een schot (kiezen met de lightgun), navigatie met pijltjes/gamepad, de aim-stroom van de
// engine (IPC watch met lite) en een gamepadlezer (evdev, alleen-lezen).
//
// Bediening met de lightgun: de virtuele gun is voor de desktop verborgen (LIBINPUT_IGNORE_DEVICE), dus het venster leest de
// aim en de trekker van speler 1 rechtstreeks uit de engine (AimStream) en tekent een eigen vizier. Een schot op een tegel
// kiest hem; een tweede schot op dezelfde tegel binnen ShotConfirm::kConfirmMs, of de trekker kHoldMs vasthouden, start hem.
// De PS-knop centreert (dat doet de engine zelf, ook zonder gewapende uitvoer).
#include <QJsonObject>
#include <QList>
#include <QLocalSocket>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "core/GameData.h"
#include "core/GameLibrary.h"

class QSocketNotifier;

namespace rg {

namespace bigpicture {

// Games voor de fullscreen-modus: alleen startklare games (profiel aanwezig, bestanden aanwezig), en zonder showAll alleen
// die volgens [Status] in patches/<Game>/game.ini speelbaar zijn (Status = playable). Volgorde: speelbaar eerst, dan op naam.
QList<GameEntry> games(const QList<GameEntry>& all, const GameData& data, bool showAll);

struct Layout {
    QList<QRectF> tiles;  // per game
    int columns = 1;
    double tile = 0;      // zijde van het icoon
};
// Raster binnen `area`: zo groot mogelijke vierkante tegels (met ruimte voor de titel eronder), hoogstens maxTile breed,
// gecentreerd. Past niet alles, dan scrollt de weergave per rij (firstRow); de tegels buiten beeld krijgen een lege rect.
Layout layout(const QRectF& area, int count, double maxTile = 260, double minTile = 120, int firstRow = 0);
// Index van de tegel onder punt p (met wat marge rond de tegel, zodat een lightgun niet pixelprecies hoeft te zijn); -1 = geen.
int hitTest(const QList<QRectF>& tiles, const QPointF& p, double margin = 12);
// Pijltjes en gamepad: nieuwe index (blijft binnen 0..count-1; -1 als count 0 is).
enum class Move { Left, Right, Up, Down };
int navigate(int index, Move m, int columns, int count);
// Rij die zichtbaar moet zijn voor `index` (scrollen), gegeven het aantal zichtbare rijen.
int scrollRowFor(int index, int columns, int visibleRows, int firstRow);

// Bevestiging van een schot met de lightgun.
class ShotConfirm {
public:
    static constexpr qint64 kConfirmMs = 3000;  // tweede schot binnen deze tijd
    static constexpr qint64 kHoldMs = 1000;     // of de trekker zo lang vasthouden
    struct Result {
        int launch = -1;      // start deze tegel
        int armed = -1;       // deze tegel wacht op bevestiging (toon "Shoot again to start")
        double hold = 0;      // voortgang van het vasthouden 0..1 (voor een ring)
    };
    // Eén stap: `target` is de tegel onder het vizier (-1 = geen), triggerDown de trekker, nowMs een monotone tijd.
    Result update(int target, bool triggerDown, qint64 nowMs);
    void reset();
    int armed() const { return armed_; }

private:
    bool wasDown_ = false;
    int armed_ = -1;
    qint64 armedAt_ = 0;
    int holdTarget_ = -1;
    qint64 downAt_ = 0;
    bool consumed_ = false;  // deze trekkerdruk heeft al iets gestart
};

// Toestand van speler 1 uit de engine.
struct AimState {
    bool valid = false;        // er is een geldige aim (Move verbonden en gecentreerd)
    QPointF pos{0.5, 0.5};     // 0..1 (geklemd)
    bool offScreen = false;
    bool trigger = false;      // trekker ingedrukt (knop "t")
    int triggerPresses = 0;    // aantal trekkerdrukken sinds het vorige frame (flanken)
    bool ps = false;           // PS-knop ingedrukt
    int psPresses = 0;         // ... flanken van de PS-knop en de MOVE-knop (het startscherm: PS centreert, MOVE slaat over)
    int movePresses = 0;
    QString link;              // fresh, stale, lost, disconnected
};
// Leest een event-regel van de engine: lite ("event":"aim") of het volledige sample ("event":"sample"). false = geen aim-event.
bool parseAimEvent(const QJsonObject& line, AimState* out);

}  // namespace bigpicture

// Eigen verbinding met de engine voor de aim-stroom (watch met lite, 60 Hz, standaard speler 1; setPlayer kiest een andere speler,
// voor de kalibratie per speler op het startscherm). Een oudere engine kent `lite` niet (bad_request): dan valt hij terug op de
// volledige watch. Opnieuw verbinden elke 2 s.
class AimStream : public QObject {
    Q_OBJECT
public:
    explicit AimStream(QString socketPath = {}, QObject* parent = nullptr);
    void start();
    void stop();
    bool isActive() const { return sock_.state() == QLocalSocket::ConnectedState; }
    const bigpicture::AimState& state() const { return state_; }
    bool liteSupported() const { return lite_; }
    // Volg de aim van speler 1..4 (een nieuwe watch op een nieuwe verbinding; de toestand begint leeg).
    void setPlayer(int player);
    int player() const { return player_; }

signals:
    void aimChanged(const rg::bigpicture::AimState& state);
    void connectedChanged(bool connected);

private:
    void connectNow();
    void subscribe();
    void onReadyRead();
    QString path_;
    QLocalSocket sock_;
    QByteArray buf_;
    QTimer reconnect_;
    bool lite_ = true;
    bool running_ = false;
    int player_ = 1;
    bigpicture::AimState state_;
};

// Gamepads (evdev, alleen-lezen, nooit EVIOCGRAB): D-pad/stick = pijltjes, A/Kruis = Enter, B/Cirkel = terug, Start = menu.
// Slaat de eigen virtuele apparaten van RavenGun (1209:72xx) en de PS Move zelf over (die bedient het vizier). Werkt ook
// met de virtuele gamepad van Steam Input in Game Mode. $RAVENGUN_NO_GAMEPAD=1 zet het uit.
class GamepadInput : public QObject {
    Q_OBJECT
public:
    enum class Action { None, Left, Right, Up, Down, Accept, Back, Menu };
    explicit GamepadInput(QObject* parent = nullptr);
    ~GamepadInput() override;
    void start(const QString& inputDir = QStringLiteral("/dev/input"));
    void stop();
    int deviceCount() const { return static_cast<int>(devs_.size()); }

    // Zuivere vertaling van één evdev-event naar een actie (state onthoudt de stand van de assen voor de flank).
    struct AxisState {
        int x = 0, y = 0;  // -1, 0, 1 na de drempel
    };
    static Action map(int type, int code, int value, AxisState* st, int absMin = -32768, int absMax = 32767);
    // Is dit apparaat een gamepad die we willen lezen? (bus/vendor/product en of het BTN_SOUTH of BTN_GAMEPAD heeft)
    static bool wanted(quint16 vendor, quint16 product, bool hasGamepadButtons);

signals:
    void action(rg::GamepadInput::Action a);

private:
    void scan();
    void readDev(int fd);
    struct Dev {
        int fd = -1;
        QString path;
        QSocketNotifier* notifier = nullptr;
        AxisState axes;
        int absMin = -32768, absMax = 32767;
    };
    QList<Dev> devs_;
    QString dir_;
    QTimer rescan_;
};

}  // namespace rg

Q_DECLARE_METATYPE(rg::bigpicture::AimState)
