// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/SaverWindow.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QScreen>
#include <QTextStream>
#include <QTime>

namespace rg::ui {

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

    // Grove timer: nauwelijks CPU. Elke tik leest de status opnieuw en verplaatst het paneel.
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

QString SaverWindow::StatusPath() {
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    return base + QStringLiteral("/ravengun/saver-status.txt");
}

QString SaverWindow::ReadStatus(qint64* sinceEpoch) {
    *sinceEpoch = 0;
    QFileInfo fi(StatusPath());
    if (!fi.exists())
        return QString();
    *sinceEpoch = fi.lastModified().toSecsSinceEpoch();
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream ts(&f);
    return ts.readLine().trimmed();
}

void SaverWindow::Reposition() {
    qint64 since = 0;
    QString status = ReadStatus(&since);
    if (status.isEmpty())
        status = QStringLiteral("RavenGun");
    clock_->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));

    // "stil"-regel: hoelang geleden is de status voor het laatst bijgewerkt.
    QString sub = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    if (since > 0) {
        const qint64 secs = QDateTime::currentSecsSinceEpoch() - since;
        const QString sinds =
            QDateTime::fromSecsSinceEpoch(since).time().toString(QStringLiteral("HH:mm"));
        if (secs >= 120) {
            const qint64 mins = secs / 60;
            const QString dur = mins >= 60
                                    ? QStringLiteral("%1 u %2 m").arg(mins / 60).arg(mins % 60)
                                    : QStringLiteral("%1 m").arg(mins);
            sub += QStringLiteral(" · stil %1 (sinds %2)").arg(dur, sinds);
        } else {
            sub += QStringLiteral(" · bezig (sinds %1)").arg(sinds);
        }
    }

    // Helder statusregeltje + gedimde tijd/stil-regel. Helder genoeg om van de bank te lezen; het paneel
    // beweegt elke tik, dus inbranden is geen risico.
    const QString html =
        QStringLiteral(
            "<div style='font: 600 42px sans-serif; color: #e6e6e6;'>%1</div>"
            "<div style='font: 22px sans-serif; color: #555555; margin-top: 10px;'>%2</div>")
            .arg(status.toHtmlEscaped(), sub.toHtmlEscaped());
    panel_->setText(html);
    panel_->setMaximumWidth(qMax(320, width() * 72 / 100));
    panel_->adjustSize();

    const int maxX = qMax(0, qMin(width() * 88 / 100, width() - panel_->width()));
    const int maxY = qMax(0, qMin(height() * 90 / 100, height() - panel_->height()));
    panel_->move(QRandomGenerator::global()->bounded(maxX + 1),
                 QRandomGenerator::global()->bounded(maxY + 1));
}

}  // namespace rg::ui
