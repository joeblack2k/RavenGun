// SPDX-License-Identifier: GPL-3.0-or-later
// OLED-bescherming (`ravengun --saver`): schermvullend zwart venster zonder rand. Toont een helder, bewegend
// statusregeltje (wat RavenGun nu doet) plus een gedimde regel met de tijd en hoelang het stil is. Alles springt
// elke kMoveIntervalMs naar een nieuwe plek zodat er niets inbrandt. Geen engine, geen hoofdvenster, geen tray,
// geen browserbalk. De status komt uit ~/.local/state/ravengun/saver-status.txt (eerste regel = de zin); de
// mtime van dat bestand bepaalt "stil sinds". Zo kan de eigenaar de laptop dichtdoen en op de tv de voortgang zien.
#pragma once
#include <QLabel>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace rg::ui {

class SaverWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMoveIntervalMs = 12000;
    explicit SaverWindow(QWidget* parent = nullptr);

    // Schermvullend op het primaire scherm tonen (muisaanwijzer verborgen).
    void Open();
    // Status opnieuw inlezen, de tekst bijwerken en het paneel naar een willekeurige plek zetten.
    void Reposition();
    QString ClockText() const { return clock_->text(); }
    QRect ClockGeometry() const { return panel_->geometry(); }

private:
    // Pad naar het statusbestand (~/.local/state/ravengun/saver-status.txt), honoreert XDG_STATE_HOME.
    static QString StatusPath();
    // Leest de eerste regel van het statusbestand; vult 'since' met de mtime (seconden sinds epoch, 0 = geen bestand).
    static QString ReadStatus(qint64* sinceEpoch);

    QLabel* panel_;   // helder statusregeltje + gedimde tijd/stil-regel (rich text)
    QLabel* clock_;   // compat: losse klok-tekst voor ClockText() in tests
    QTimer timer_;
};

}  // namespace rg::ui
