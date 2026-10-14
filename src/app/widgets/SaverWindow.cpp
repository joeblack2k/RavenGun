// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/SaverWindow.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPicture>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScreen>
#include <QTime>
#include <QtMath>
#include <cmath>

namespace rg::ui {

namespace {

constexpr int kMaxValue = 160;   // langer lees je niet in een oogopslag
constexpr int kMaxQuestion = 300;   // een vraag mag langer zijn: hij staat groot en is het enige dat telt
constexpr int kMaxItems = 8;

QString StateDir() {
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    return base + QStringLiteral("/ravengun");
}

SaverStatus Bad(const QString& why) {
    SaverStatus s;
    s.error = why;
    return s;
}

QString Ago(qint64 secs) {
    if (secs < 90)
        return QStringLiteral("zojuist");
    const qint64 mins = secs / 60;
    if (mins < 60)
        return QStringLiteral("%1 min geleden").arg(mins);
    return QStringLiteral("%1 uur %2 min geleden").arg(mins / 60).arg(mins % 60);
}

QString StateText(const QString& state) {
    if (state == QStringLiteral("goed"))
        return QStringLiteral("Het gaat goed");
    if (state == QStringLiteral("slecht"))
        return QStringLiteral("Het gaat niet goed");
    return QStringLiteral("Bezig, nog geen uitslag");
}

QString GameText(SaverGame g) {
    switch (g) {
        case SaverGame::None: return QStringLiteral("Er draait nu geen spel");
        case SaverGame::Visible: return QStringLiteral("Er draait nu een spel (dat is nog geen uitslag)");
        case SaverGame::Hidden: return QStringLiteral("Er draait nu een spel, zonder beeld (dat is nog geen uitslag)");
        case SaverGame::Unknown: break;
    }
    return QStringLiteral("Onbekend of er nu een spel draait");
}

// ---------------------------------------------------------------------------------------------------------------
// Het bord zelf. Alles wordt in "logische" pixels getekend (1080p-maat) en door de schaal op het scherm gezet, zodat
// het op 1080p en 4K hetzelfde oogt. Donkere achtergrond, geen grote felle vlakken: kaarten zijn bijna zwart met
// een dunne rand. Alleen de vraagbalk is fel, en die pulseert.
// ---------------------------------------------------------------------------------------------------------------

struct BoardData {
    SaverStatus status;
    SaverGame game = SaverGame::Unknown;
    QList<SaverFeedItem> feed;
    qint64 now = 0;
};

QFont Font(int px, bool bold = false) {
    QFont f(QStringLiteral("sans-serif"));
    f.setPixelSize(px);
    f.setWeight(bold ? QFont::Bold : QFont::Normal);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QColor WithAlpha(QColor c, int a) {
    c.setAlpha(a);
    return c;
}

// Tekst met woordafbreking; geeft de hoogte terug. maxLines > 0 knipt af.
int Text(QPainter& p, int x, int y, int w, const QFont& f, const QColor& c, const QString& s, int align = Qt::AlignLeft, int maxLines = 0) {
    const QFontMetrics fm(f);
    int h = fm.boundingRect(QRect(0, 0, w, 100000), Qt::TextWordWrap | align, s).height();
    if (maxLines > 0)
        h = qMin(h, maxLines * fm.lineSpacing());
    p.save();
    p.setClipRect(QRect(x, y, w, h));
    p.setFont(f);
    p.setPen(c);
    p.drawText(QRect(x, y, w, 100000), Qt::TextWordWrap | align, s);
    p.restore();
    return h;
}

int TextWidth(const QFont& f, const QString& s) { return QFontMetrics(f).horizontalAdvance(s); }

QColor StateColor(const QString& state) {
    if (state == QStringLiteral("goed")) return QColor(0x3d, 0xdc, 0x84);
    if (state == QStringLiteral("slecht")) return QColor(0xff, 0x5a, 0x4f);
    return QColor(0x4d, 0xa3, 0xff);
}

QString PillText(const QString& state) {
    if (state == QStringLiteral("goed")) return QStringLiteral("HET GAAT GOED");
    if (state == QStringLiteral("slecht")) return QStringLiteral("HET GAAT NIET GOED");
    return QStringLiteral("BEZIG, NOG GEEN UITSLAG");
}

void Card(QPainter& p, const QRect& r, const QColor& border = QColor(0x24, 0x2b, 0x3b)) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0x0b, 0x0d, 0x12));
    p.setPen(QPen(border, 1.5));
    p.drawRoundedRect(QRectF(r).adjusted(0.75, 0.75, -0.75, -0.75), 18, 18);
}

void Check(QPainter& p, int x, int y, int d, const QColor& c) {   // vinkje in een cirkel, zonder lettertype-afhankelijkheid
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 2.5));
    p.setBrush(WithAlpha(c, 40));
    p.drawEllipse(QRectF(x, y, d, d));
    QPainterPath path;
    path.moveTo(x + d * 0.27, y + d * 0.53);
    path.lineTo(x + d * 0.43, y + d * 0.69);
    path.lineTo(x + d * 0.74, y + d * 0.33);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
}

void OpenDot(QPainter& p, int x, int y, int d, const QColor& c) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 2.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(x, y, d, d));
}

QString Clock(qint64 t) { return QDateTime::fromSecsSinceEpoch(t).time().toString(QStringLiteral("HH:mm")); }

struct Drawn {
    int height = 0;
    QRect banner;          // de vraagbalk (voor het pulseren), leeg zonder vraag
    QList<QRect> cards;    // de kaartvlakken; de tweede pas tekent ze eerst, daarna de inhoud
};

// Tekent het hele bord op breedte W (logisch). `pulse` is 0..1. Geeft de hoogte terug (ook als er niets echt zichtbaar is:
// de maatvoering gebeurt met dezelfde code).
Drawn DrawBoard(QPainter& p, int W, const BoardData& d, double pulse, const QList<QRect>* cards = nullptr) {
    const SaverStatus& s = d.status;
    const QColor dim(0x6b, 0x74, 0x88), soft(0xa7, 0xb0, 0xc3), bright(0xee, 0xf1, 0xf7), amber(0xff, 0xb4, 0x2e);
    Drawn out;
    int y = 0;
    if (cards)
        for (const QRect& c : *cards) Card(p, c);

    // Vraag voor de eigenaar: onmogelijk te missen. Pulserende rand en gloed; de tekst zelf blijft constant leesbaar.
    if (s.ok && !s.question.isEmpty()) {
        const QFont qf = Font(60, true);
        const int inner = W - 2 * 56;
        const int qh = QFontMetrics(qf).boundingRect(QRect(0, 0, inner, 100000), Qt::TextWordWrap, s.question).height();
        const int bh = 36 + 44 + 16 + qh + 40;
        const QRect r(0, y, W, bh);
        out.banner = r.adjusted(-24, -24, 24, 24);
        const int glow = 40 + int(pulse * 70);
        for (int i = 3; i >= 1; --i) {   // zachte gloed om de balk
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(WithAlpha(amber, glow / (i + 1)), 3 + i * 4));
            p.drawRoundedRect(QRectF(r).adjusted(-i * 3, -i * 3, i * 3, i * 3), 24, 24);
        }
        p.setBrush(QColor(0x2c + int(pulse * 0x14), 0x1a + int(pulse * 0x0c), 0x00));
        p.setPen(QPen(QColor(255, 0xa0 + int(pulse * 0x4a), 0x2a + int(pulse * 0x30)), 5));
        p.drawRoundedRect(QRectF(r).adjusted(2.5, 2.5, -2.5, -2.5), 22, 22);
        // Label-badge
        const QFont bf = Font(34, true);
        const QString label = QStringLiteral("VRAAG VOOR JOU");
        const int bw = TextWidth(bf, label) + 100;
        const QRect badge(56, y + 32, bw, 52);
        p.setPen(Qt::NoPen);
        p.setBrush(amber);
        p.drawRoundedRect(badge, 26, 26);
        p.setBrush(QColor(0x1a, 0x10, 0x00));
        p.drawEllipse(QRectF(badge.x() + 22, badge.y() + 17, 18, 18));
        Text(p, badge.x() + 52, badge.y() + 3, bw - 52, bf, QColor(0x1a, 0x10, 0x00), label);
        Text(p, 56, y + 36 + 44 + 16 + 8, inner, qf, QColor(255, 255, 255), s.question);
        y += bh + 36;
    }

    // Kop: naam links, klok rechts.
    const bool running = d.game == SaverGame::Visible || d.game == SaverGame::Hidden;
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(running ? amber : QColor(0x4a, 0x52, 0x63));
    p.drawEllipse(QRectF(2, y + 14, 14, 14));
    const int clockW = 330;
    const int hl = Text(p, 28, y + 4, W - clockW - 28, Font(24, true), running ? amber : soft, GameText(d.game));
    Text(p, 28, y + 4 + hl + 2, W - clockW - 28, Font(20), dim, QStringLiteral("RAVENGUN statusbord"));
    const int ch = Text(p, W - clockW, y - 8, clockW, Font(72, true), soft, Clock(d.now), Qt::AlignRight);
    QString upd;
    QColor updColor = dim;
    if (s.ok && s.updated > 0) {
        const qint64 age = d.now - s.updated;
        upd = QStringLiteral("bijgewerkt %1 (%2)").arg(Ago(age), Clock(s.updated));
        if (age > SaverWindow::kStaleStatusSecs) {
            upd = QStringLiteral("LET OP: oud, bijgewerkt %1 (%2)").arg(Ago(age), Clock(s.updated));
            updColor = amber;
        }
    }
    if (!upd.isEmpty())
        Text(p, W - 700, y - 8 + ch, 700, Font(24, !upd.startsWith(QStringLiteral("bijgew"))), updColor, upd, Qt::AlignRight);
    y += 150;

    if (!s.ok) {
        const QRect r(0, y, W, 250);
        if (cards) Card(p, r, WithAlpha(amber, 120));
        Text(p, 48, y + 36, W - 96, Font(72, true), amber, QStringLiteral("Geen status beschikbaar"));
        Text(p, 48, y + 140, W - 96, Font(34), soft, QStringLiteral("Reden: %1.").arg(s.error), Qt::AlignLeft, 3);
        out.height = y + 250;
        return out;
    }

    // Hoofdkaart: spel, stand, wat er nu gebeurt, voortgang, doel.
    const int top = y;
    const QColor sc = StateColor(s.state);
    const int pad = 44;
    const QFont pf = Font(34, true);
    const QString pill = PillText(s.state);
    const int pillW = TextWidth(pf, pill) + 44 + 56;
    int cy = y + 34;
    const int gh = Text(p, pad, cy - 8, W - 2 * pad - pillW - 30, Font(104, true), bright, s.game, Qt::AlignLeft, 1);
    const QRect pr(W - pad - pillW, cy + 12, pillW, 64);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(WithAlpha(sc, 36));
    p.setPen(QPen(WithAlpha(sc, 220), 2.5));
    p.drawRoundedRect(pr, 32, 32);
    p.setPen(Qt::NoPen);
    p.setBrush(sc);
    p.drawEllipse(QRectF(pr.x() + 26, pr.y() + 23, 18, 18));
    Text(p, pr.x() + 56, pr.y() + 8, pillW - 56, pf, sc, pill);
    cy += gh + 6;
    cy += Text(p, pad, cy, W - 2 * pad, Font(54, true), QColor(0xdf, 0xe6, 0xf3), s.doing, Qt::AlignLeft, 2) + 24;

    if (s.steps > 0) {
        QString lab = QStringLiteral("STAP %1 VAN %2").arg(s.step).arg(s.steps);
        if (s.step == s.steps) lab += QStringLiteral("  ·  de laatste stap");
        else if (s.steps - s.step == 1) lab += QStringLiteral("  ·  bijna klaar");
        cy += Text(p, pad, cy, W - 2 * pad, Font(24, true), soft, lab) + 10;
        const int gap = 10, bw = W - 2 * pad, h = 18;
        const double seg = (bw - gap * (s.steps - 1)) / double(s.steps);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < s.steps; ++i) {
            const QRectF r(pad + i * (seg + gap), cy, seg, h);
            p.setBrush(i < s.step - 1 ? sc : i == s.step - 1 ? WithAlpha(sc, 130) : QColor(0x1d, 0x23, 0x30));
            p.drawRoundedRect(r, h / 2.0, h / 2.0);
        }
        cy += h + 26;
    }
    if (!s.purpose.isEmpty()) {
        const int lw = TextWidth(Font(24, true), QStringLiteral("WAARVOOR")) + 24;
        Text(p, pad, cy + 3, lw, Font(24, true), dim, QStringLiteral("WAARVOOR"));
        cy += Text(p, pad + lw, cy, W - 2 * pad - lw, Font(32), soft, s.purpose, Qt::AlignLeft, 2) + 20;
    }
    cy += 14;
    out.cards << QRect(0, top, W, cy - top);
    y = cy + 30;

    // Twee kolommen: klaar/nog links, activiteit rechts.
    const bool feed = !d.feed.isEmpty();
    const int colGap = 36;
    const int leftW = feed ? (W - colGap) * 45 / 100 : W;
    const int rightW = W - colGap - leftW;
    int ly = y, ry = y;
    const auto heading = [&](int x, int& yy, int w, const QString& s2, const QColor& c) {
        yy += Text(p, x, yy, w, Font(24, true), c, s2) + 14;
    };
    if (!s.done.isEmpty() || !s.todo.isEmpty()) {
        const int cardTop = ly;
        ly += 28;
        if (!s.done.isEmpty()) {
            heading(32, ly, leftW - 64, QStringLiteral("KLAAR"), dim);
            for (const QString& it : s.done) {
                Check(p, 32, ly + 3, 34, QColor(0x3d, 0xdc, 0x84));
                ly += qMax(40, Text(p, 84, ly, leftW - 116, Font(32), QColor(0xc8, 0xd0, 0xdf), it, Qt::AlignLeft, 2)) + 12;
            }
            ly += 8;
        }
        if (!s.todo.isEmpty()) {
            heading(32, ly, leftW - 64, QStringLiteral("NOG TE DOEN"), dim);
            for (const QString& it : s.todo) {
                OpenDot(p, 32, ly + 3, 34, QColor(0x8a, 0x93, 0xa6));
                ly += qMax(40, Text(p, 84, ly, leftW - 116, Font(32), bright, it, Qt::AlignLeft, 2)) + 12;
            }
            ly += 8;
        }
        ly += 16;
        out.cards << QRect(0, cardTop, leftW, ly - cardTop);
    }
    if (feed) {
        ry += 28;
        heading(32 + leftW + colGap, ry, rightW - 64, QStringLiteral("ACTIVITEIT"), dim);
        for (int i = 0; i < d.feed.size(); ++i) {
            const int a = i == 0 ? 255 : qMax(110, 235 - i * 22);
            const int x = leftW + colGap + 32;
            Text(p, x, ry + 3, 100, Font(26, i == 0), WithAlpha(i == 0 ? QColor(0x7f, 0xc4, 0xff) : dim, a), Clock(d.feed.at(i).time));
            ry += qMax(36, Text(p, x + 104, ry, rightW - 64 - 104, Font(i == 0 ? 32 : 28, i == 0), WithAlpha(i == 0 ? bright : soft, a),
                                d.feed.at(i).text, Qt::AlignLeft, 2)) + 12;
        }
        ry += 28;
        out.cards << QRect(leftW + colGap, y, rightW, ry - y);
    }
    y = qMax(ly, ry);

    // Onderaan klein: grotere doelen en wat gehaald is.
    if (!s.goals.isEmpty()) {
        const QString lab = QStringLiteral("DOELEN");
        y += 8;
        const int lw = TextWidth(Font(20, true), lab) + 20;
        Text(p, 32, y + 2, lw, Font(20, true), dim, lab);
        y += Text(p, 32 + lw, y, W - 64 - lw, Font(24), dim, s.goals.join(QStringLiteral("   ·   ")), Qt::AlignLeft, 2) + 10;
    }
    if (!s.passed.isEmpty()) {
        const QString lab = QStringLiteral("GEHAALD (5 MIN ATTRACT OF SPEL)");
        y += 6;
        const int lw = TextWidth(Font(20, true), lab) + 20;
        Text(p, 32, y + 2, lw, Font(20, true), dim, lab);
        y += Text(p, 32 + lw, y, W - 64 - lw, Font(24), QColor(0x3d, 0xdc, 0x84).darker(150), s.passed.join(QStringLiteral("   ·   ")), Qt::AlignLeft, 2) + 10;
    }
    out.height = y;
    return out;
}

// Het bord tekenen in twee passen: eerst de kaartvlakken (op gemeten maat), dan de inhoud erbovenop. Dat gaat het
// eenvoudigst door de inhoud twee keer te laten tekenen: de eerste keer in een QPicture voor de maat en de blokhoogten.
class SaverPanel : public QWidget {
public:
    explicit SaverPanel(QWidget* parent) : QWidget(parent) { setAttribute(Qt::WA_TransparentForMouseEvents); }

    void Set(const BoardData& d, double scale) {
        data_ = d;
        scale_ = scale;
    }
    // De maat voor breedte `w` (echte pixels) en schaal; geeft de echte hoogte.
    int Measure(int w) {
        logicalW_ = qRound(w / scale_) - 2 * kMargin;
        QPicture pic;
        QPainter p(&pic);
        const Drawn dr = DrawBoard(p, logicalW_, data_, 0.5);
        p.end();
        logicalH_ = dr.height + 8;
        banner_ = dr.banner;
        cards_ = dr.cards;
        return qRound((logicalH_ + 2 * kMargin) * scale_);
    }
    int LogicalHeight() const { return logicalH_; }
    void SetPulse(double v) {
        pulse_ = v;
        if (!banner_.isEmpty())
            update(QRect(qFloor((banner_.x() + kMargin) * scale_), qFloor((banner_.y() + kMargin) * scale_), qCeil(banner_.width() * scale_) + 2, qCeil(banner_.height() * scale_) + 2));
    }
    bool HasBanner() const { return !banner_.isEmpty(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.scale(scale_, scale_);
        p.translate(kMargin, kMargin);
        DrawBoard(p, logicalW_, data_, pulse_, &cards_);
    }

private:
    static constexpr int kMargin = 24;   // ruimte voor de gloed om de vraagbalk
    BoardData data_;
    QList<QRect> cards_;
    double scale_ = 1.0, pulse_ = 0.5;
    int logicalW_ = 1600, logicalH_ = 600;
    QRect banner_;
};

}  // namespace

SaverWindow::SaverWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      panel_(nullptr),
      clock_(new QLabel(this)) {
    setWindowTitle(QStringLiteral("RavenGun screen saver"));
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setCursor(Qt::BlankCursor);

    panel_ = new SaverPanel(this);

    clock_->hide();  // alleen voor ClockText()/tests; het zichtbare werk doet panel_

    // Grove timer: nauwelijks CPU. Elke tik leest de bestanden opnieuw en verplaatst het bord.
    clockStart_.start();
    pulseTimer_.setInterval(33);
    connect(&pulseTimer_, &QTimer::timeout, this, &SaverWindow::Pulse);
    timer_.setTimerType(Qt::VeryCoarseTimer);
    timer_.setInterval(kMoveIntervalMs);
    connect(&timer_, &QTimer::timeout, this, &SaverWindow::Reposition);
    Reposition();
}

void SaverWindow::Open() {
    if (QScreen* s = QGuiApplication::primaryScreen()) {
        setScreen(s);
        setGeometry(s->geometry());
    }
    showFullScreen();
    Reposition();
    timer_.start();
}

QString SaverWindow::StatusPath() { return StateDir() + QStringLiteral("/saver-status.txt"); }
QString SaverWindow::FeedPath() { return StateDir() + QStringLiteral("/saver-feed.txt"); }
QString SaverWindow::GamePath() { return StateDir() + QStringLiteral("/saver-game.txt"); }

SaverStatus SaverWindow::ParseStatus(const QString& text) {
    static const QStringList known = {QStringLiteral("spel"), QStringLiteral("bezig"), QStringLiteral("stand"), QStringLiteral("stap"),
                                      QStringLiteral("doel"), QStringLiteral("klaar"), QStringLiteral("nog"), QStringLiteral("gehaald"), QStringLiteral("vraag"), QStringLiteral("doelen")};
    QHash<QString, QString> kv;
    int nr = 0;
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        ++nr;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            return Bad(QStringLiteral("regel %1 is geen 'sleutel: waarde'").arg(nr));
        const QString key = line.left(colon).trimmed().toLower();
        const QString value = line.mid(colon + 1).trimmed();
        if (!known.contains(key))
            return Bad(QStringLiteral("regel %1: onbekende sleutel '%2'").arg(nr).arg(key));
        if (kv.contains(key))
            return Bad(QStringLiteral("regel %1: '%2' staat er twee keer").arg(nr).arg(key));
        if (value.size() > (key == QStringLiteral("vraag") ? kMaxQuestion : kMaxValue))
            return Bad(QStringLiteral("regel %1: '%2' is te lang om in een oogopslag te lezen").arg(nr).arg(key));
        kv.insert(key, value);
    }
    if (kv.isEmpty())
        return Bad(QStringLiteral("het statusbestand is leeg"));
    for (const QString& need : {QStringLiteral("spel"), QStringLiteral("bezig"), QStringLiteral("stand")})
        if (kv.value(need).isEmpty())
            return Bad(QStringLiteral("'%1' ontbreekt of is leeg").arg(need));

    SaverStatus s;
    s.game = kv.value(QStringLiteral("spel"));
    s.doing = kv.value(QStringLiteral("bezig"));
    s.state = kv.value(QStringLiteral("stand")).toLower();
    if (s.state != QStringLiteral("goed") && s.state != QStringLiteral("slecht") && s.state != QStringLiteral("bezig"))
        return Bad(QStringLiteral("'stand' is '%1'; het moet goed, slecht of bezig zijn").arg(s.state));
    if (kv.contains(QStringLiteral("stap"))) {
        static const QRegularExpression rx(QStringLiteral("^(\\d{1,2})\\s*(?:van|/)\\s*(\\d{1,2})$"));
        const QRegularExpressionMatch m = rx.match(kv.value(QStringLiteral("stap")).toLower());
        if (!m.hasMatch())
            return Bad(QStringLiteral("'stap' is '%1'; schrijf het als '3 van 5'").arg(kv.value(QStringLiteral("stap"))));
        s.step = m.captured(1).toInt();
        s.steps = m.captured(2).toInt();
        if (s.step < 1 || s.steps < 1 || s.step > s.steps)
            return Bad(QStringLiteral("'stap' is %1 van %2; dat kan niet").arg(s.step).arg(s.steps));
    }
    s.purpose = kv.value(QStringLiteral("doel"));
    s.question = kv.value(QStringLiteral("vraag"));
    for (const auto& [key, list] : {std::pair{QStringLiteral("klaar"), &s.done}, std::pair{QStringLiteral("nog"), &s.todo},
                                    std::pair{QStringLiteral("gehaald"), &s.passed}, std::pair{QStringLiteral("doelen"), &s.goals}}) {
        for (const QString& item : kv.value(key).split(QLatin1Char(';'), Qt::SkipEmptyParts))
            if (!item.trimmed().isEmpty())
                list->append(item.trimmed());
        if (list->size() > kMaxItems)
            return Bad(QStringLiteral("'%1' heeft %2 punten; meer dan %3 lees je niet in een oogopslag").arg(key).arg(list->size()).arg(kMaxItems));
    }
    s.ok = true;
    return s;
}

SaverStatus SaverWindow::LoadStatus(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists())
        return Bad(QStringLiteral("het statusbestand ontbreekt"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Bad(QStringLiteral("het statusbestand is niet te lezen"));
    const QByteArray bytes = f.read(64 * 1024);
    SaverStatus s = ParseStatus(QString::fromUtf8(bytes));
    s.updated = fi.lastModified().toSecsSinceEpoch();
    return s;
}

QList<SaverFeedItem> SaverWindow::ParseFeed(const QString& text, int max) {
    // Een regel per gebeurtenis: `2026-10-14T21:03:11 tekst` (lokale tijd). Nieuwste onderaan in het bestand, bovenaan op het bord.
    QList<SaverFeedItem> all;
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        const int sp = line.indexOf(QLatin1Char(' '));
        if (sp < 10)
            continue;
        const QDateTime when = QDateTime::fromString(line.left(sp), Qt::ISODate);
        const QString what = line.mid(sp + 1).trimmed();
        if (!when.isValid() || what.isEmpty())
            continue;
        all.append({when.toSecsSinceEpoch(), what.left(kMaxValue)});
    }
    QList<SaverFeedItem> out;
    for (int i = all.size() - 1; i >= 0 && out.size() < max; --i)
        out.append(all.at(i));
    return out;
}

QList<SaverFeedItem> SaverWindow::LoadFeed(const QString& path, int max) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = f.size();
    if (size > 64 * 1024)
        f.seek(size - 64 * 1024);   // alleen het eind telt
    return ParseFeed(QString::fromUtf8(f.readAll()), max);
}

SaverGame SaverWindow::ParseGame(const QString& text) {
    const QString word = text.section(QLatin1Char('\n'), 0, 0).trimmed().toLower();
    if (word == QStringLiteral("geen"))
        return SaverGame::None;
    if (word == QStringLiteral("zichtbaar"))
        return SaverGame::Visible;
    if (word == QStringLiteral("onzichtbaar"))
        return SaverGame::Hidden;
    return SaverGame::Unknown;
}

SaverGame SaverWindow::LoadGame(const QString& path, qint64 now) {
    const QFileInfo fi(path);
    if (!fi.exists() || now - fi.lastModified().toSecsSinceEpoch() > kStaleGameSecs)
        return SaverGame::Unknown;   // niemand houdt het bij: dan weten we het niet
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return SaverGame::Unknown;
    return ParseGame(QString::fromUtf8(f.read(256)));
}

QStringList SaverWindow::Lines(const SaverStatus& s, SaverGame g, qint64 now) {
    QStringList out;
    out << GameText(g);
    const QString clock = QDateTime::fromSecsSinceEpoch(now).time().toString(QStringLiteral("HH:mm"));
    if (!s.ok) {
        out << QStringLiteral("Geen status beschikbaar");
        out << QStringLiteral("Reden: %1.").arg(s.error);
        out << clock;
        return out;
    }
    if (!s.question.isEmpty())
        out << QStringLiteral("Vraag voor jou: %1").arg(s.question);
    out << QStringLiteral("%1: %2").arg(s.game, s.doing);
    QString how = StateText(s.state);
    if (s.steps > 0) {
        how += QStringLiteral(" · stap %1 van %2").arg(s.step).arg(s.steps);
        if (s.step == s.steps)
            how += QStringLiteral(" · de laatste stap");
        else if (s.steps - s.step == 1)
            how += QStringLiteral(" · bijna klaar");
    }
    out << how;
    if (!s.purpose.isEmpty())
        out << QStringLiteral("Waarvoor: %1").arg(s.purpose);
    if (!s.done.isEmpty())
        out << QStringLiteral("Al gedaan: %1").arg(s.done.join(QStringLiteral(" · ")));
    if (!s.todo.isEmpty())
        out << QStringLiteral("Nog te doen: %1").arg(s.todo.join(QStringLiteral(" · ")));
    if (!s.goals.isEmpty())
        out << QStringLiteral("Doelen: %1").arg(s.goals.join(QStringLiteral(" · ")));
    // "Er draait een spel" is iets anders dan "het spel is gehaald": gehaald is het pas na 5 minuten attract of spel.
    if (!s.passed.isEmpty())
        out << QStringLiteral("Gehaald (5 minuten attract of spel): %1").arg(s.passed.join(QStringLiteral(" · ")));
    const qint64 age = s.updated > 0 ? now - s.updated : -1;
    const QString at = s.updated > 0 ? QDateTime::fromSecsSinceEpoch(s.updated).time().toString(QStringLiteral("HH:mm")) : QString();
    if (age < 0)
        out << clock;
    else if (age > kStaleStatusSecs)
        out << QStringLiteral("%1 · LET OP: deze status is oud, bijgewerkt %2 (%3)").arg(clock, Ago(age), at);
    else
        out << QStringLiteral("%1 · bijgewerkt %2 (%3)").arg(clock, Ago(age), at);
    return out;
}

void SaverWindow::Layout(bool center) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    BoardData d;
    d.status = LoadStatus(StatusPath());
    d.game = LoadGame(GamePath(), now);
    d.feed = d.status.ok ? LoadFeed(FeedPath(), kFeedShown) : QList<SaverFeedItem>();
    d.now = now;
    clock_->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
    shown_ = Lines(d.status, d.game, now);

    // Schaal naar de hoogte van het scherm (1080p = 1.0, 4K = 2.0). Past de inhoud niet, dan iets kleiner.
    auto* panel = static_cast<SaverPanel*>(panel_);
    double scale = qMax(0.4, height() / 1080.0);
    int w = 0, h = 0;
    for (int pass = 0; pass < 3; ++pass) {
        panel->Set(d, scale);
        w = qMin(width() * 88 / 100, qRound(scale * 1760));   // niet groter dan nodig: meer ruimte om te verspringen
        h = panel->Measure(w);
        const int avail = height() * 94 / 100;
        if (h <= avail) break;
        scale *= double(avail) / h * 0.99;
    }
    panel->resize(w, h);
    const bool hasQuestion = d.status.ok && !d.status.question.isEmpty();
    if (hasQuestion && !pulseTimer_.isActive() && isVisible()) pulseTimer_.start();
    if (!hasQuestion) pulseTimer_.stop();
    Pulse();
    panel->update();

    const int maxX = qMax(0, width() - w);
    const int maxY = qMax(0, height() - h);
    if (center)
        panel->move(maxX / 2, maxY / 2);
    else
        panel->move(QRandomGenerator::global()->bounded(maxX + 1), QRandomGenerator::global()->bounded(maxY + 1));
}

void SaverWindow::Pulse() {
    auto* panel = static_cast<SaverPanel*>(panel_);
    if (!panel->HasBanner()) return;
    // Een rustige ademhaling (ongeveer 2,4 s per slag), geen knipperlicht.
    panel->SetPulse(0.5 - 0.5 * std::cos(clockStart_.elapsed() / 2400.0 * 2.0 * M_PI));
}

void SaverWindow::Reposition() { Layout(false); }

bool SaverWindow::SaveSnapshot(const QString& png, const QSize& size) {
    pulseTimer_.stop();
    resize(size);
    Layout(true);
    static_cast<SaverPanel*>(panel_)->SetPulse(0.75);
    return grab().save(png);
}

}  // namespace rg::ui
