// SPDX-License-Identifier: GPL-3.0-or-later
// OLED-bescherming (`ravengun --saver`): schermvullend zwart venster zonder rand met een klein, gedimd klokje dat elke
// 20 s naar een willekeurige plek springt. Geen engine, geen hoofdvenster, geen tray; zo kan er nooit een browserbalk
// of infobalk in beeld komen die juist inbrandt.
#pragma once
#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace rg::ui {

class SaverWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMoveIntervalMs = 20000;
    explicit SaverWindow(QWidget* parent = nullptr);

    // Schermvullend op het primaire scherm tonen (muisaanwijzer verborgen).
    void Open();
    // Tijd bijwerken en het klokje naar een willekeurige plek zetten.
    void Reposition();
    QString ClockText() const { return clock_->text(); }
    QRect ClockGeometry() const { return clock_->geometry(); }

private:
    QLabel* clock_;
    QTimer timer_;
};

}  // namespace rg::ui
