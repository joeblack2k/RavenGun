// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/SaverWindow.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScreen>
#include <QTime>

namespace rg::ui {

namespace {

constexpr int kMaxValue = 160;   // langer lees je niet in een oogopslag
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

}  // namespace

SaverWindow::SaverWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      panel_(new QLabel(this)),
      clock_(new QLabel(this)) {
    setWindowTitle(QStringLiteral("RavenGun screen saver"));
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setCursor(Qt::BlankCursor);

    panel_->setTextFormat(Qt::RichText);
    panel_->setWordWrap(true);
    panel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    panel_->setStyleSheet(QStringLiteral("background: transparent;"));

    clock_->hide();  // alleen voor ClockText()/tests; het zichtbare werk doet panel_

    // Grove timer: nauwelijks CPU. Elke tik leest de bestanden opnieuw en verplaatst het bord.
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
QString SaverWindow::GamePath() { return StateDir() + QStringLiteral("/saver-game.txt"); }

SaverStatus SaverWindow::ParseStatus(const QString& text) {
    static const QStringList known = {QStringLiteral("spel"), QStringLiteral("bezig"), QStringLiteral("stand"), QStringLiteral("stap"),
                                      QStringLiteral("doel"), QStringLiteral("klaar"), QStringLiteral("nog"), QStringLiteral("gehaald")};
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
        if (value.size() > kMaxValue)
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
    for (const auto& [key, list] : {std::pair{QStringLiteral("klaar"), &s.done}, std::pair{QStringLiteral("nog"), &s.todo},
                                    std::pair{QStringLiteral("gehaald"), &s.passed}}) {
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

QString SaverWindow::Html(const SaverStatus& s, SaverGame g, qint64 now) {
    const QStringList lines = Lines(s, g, now);
    const bool stale = s.ok && s.updated > 0 && now - s.updated > kStaleStatusSecs;
    // Kleuren: gedempt, want het bord staat lang aan. Groen/rood alleen op de regel die zegt hoe het gaat.
    const QString gameColor = g == SaverGame::Hidden || g == SaverGame::Visible ? QStringLiteral("#d8b24a") : QStringLiteral("#8a8a8a");
    QString html = QStringLiteral("<div style='font: 600 26px sans-serif; color: %1;'>%2</div>").arg(gameColor, lines.value(0).toHtmlEscaped());
    if (!s.ok) {
        html += QStringLiteral("<div style='font: 600 44px sans-serif; color: #d8b24a; margin-top: 14px;'>%1</div>").arg(lines.value(1).toHtmlEscaped());
        html += QStringLiteral("<div style='font: 26px sans-serif; color: #b0b0b0; margin-top: 8px;'>%1</div>").arg(lines.value(2).toHtmlEscaped());
        html += QStringLiteral("<div style='font: 22px sans-serif; color: #666666; margin-top: 12px;'>%1</div>").arg(lines.value(3).toHtmlEscaped());
        return html;
    }
    const QString stateColor = s.state == QStringLiteral("goed") ? QStringLiteral("#5fbf6a")
                               : s.state == QStringLiteral("slecht") ? QStringLiteral("#d9655a") : QStringLiteral("#b0b0b0");
    html += QStringLiteral("<div style='font: 600 44px sans-serif; color: #e6e6e6; margin-top: 14px;'>%1</div>").arg(lines.value(1).toHtmlEscaped());
    html += QStringLiteral("<div style='font: 600 34px sans-serif; color: %1; margin-top: 8px;'>%2</div>").arg(stateColor, lines.value(2).toHtmlEscaped());
    for (int i = 3; i < lines.size() - 1; ++i)
        html += QStringLiteral("<div style='font: 26px sans-serif; color: #b0b0b0; margin-top: 8px;'>%1</div>").arg(lines.at(i).toHtmlEscaped());
    html += QStringLiteral("<div style='font: 22px sans-serif; color: %1; margin-top: 12px;'>%2</div>")
                .arg(stale ? QStringLiteral("#d8b24a") : QStringLiteral("#666666"), lines.last().toHtmlEscaped());
    return html;
}

void SaverWindow::Reposition() {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const SaverStatus status = LoadStatus(StatusPath());
    const SaverGame game = LoadGame(GamePath(), now);
    clock_->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
    shown_ = Lines(status, game, now);

    panel_->setText(Html(status, game, now));
    panel_->setMaximumWidth(qMax(320, width() * 72 / 100));
    panel_->adjustSize();

    const int maxX = qMax(0, qMin(width() * 88 / 100, width() - panel_->width()));
    const int maxY = qMax(0, qMin(height() * 90 / 100, height() - panel_->height()));
    panel_->move(QRandomGenerator::global()->bounded(maxX + 1),
                 QRandomGenerator::global()->bounded(maxY + 1));
}

}  // namespace rg::ui
