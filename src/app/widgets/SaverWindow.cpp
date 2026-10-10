// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/SaverWindow.h"

#include <QGuiApplication>
#include <QRandomGenerator>
#include <QScreen>
#include <QTime>

namespace rg::ui {

SaverWindow::SaverWindow(QWidget* parent) : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), clock_(new QLabel(this)) {
    setWindowTitle(QStringLiteral("RavenGun screen saver"));
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setCursor(Qt::BlankCursor);
    clock_->setStyleSheet(QStringLiteral("color: rgb(45,45,45); background: transparent; font: 28px sans-serif;"));
    clock_->setAttribute(Qt::WA_TransparentForMouseEvents);
    // Alleen een 20 s-timer: nauwelijks CPU; een grove timer is genoeg.
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

void SaverWindow::Reposition() {
    clock_->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
    clock_->adjustSize();
    // Zelfde bereik als de oude HTML: links 0..85 %, boven 0..90 % van het venster, maar altijd binnen beeld.
    const int maxX = qMax(0, qMin(width() * 85 / 100, width() - clock_->width()));
    const int maxY = qMax(0, qMin(height() * 90 / 100, height() - clock_->height()));
    clock_->move(QRandomGenerator::global()->bounded(maxX + 1), QRandomGenerator::global()->bounded(maxY + 1));
}

}  // namespace rg::ui
