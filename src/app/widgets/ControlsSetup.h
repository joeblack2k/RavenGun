// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// De bedieningsrondgang van een lightgun-game: per signaal dat de game gebruikt ([Controls] in game.ini) legt hij uit wat het doet en
// vraagt hij "Press the button for: <label>". De Move-knop die de speler indrukt komt uit de aim-stroom van de engine (watch, lite);
// muis en toetsenbord werken ook (knoppen op het scherm). Groot en eenvoudig, bedoeld voor de tv. Aan het eind bevestigt de speler
// (de trekker) en worden de keuzes in de gebruikerslaag van game.ini bewaard ([Controls] P<n>.<knop> = <signaal>, Confirmed = true).
// De engine past ze toe bij de start van de game (docs/per-game-patches.md, "Bediening per game").
//
// Bediening met alleen de Move: een knop = die knop kiezen, PS = de standaard laten staan, aan het eind T (trekker) = opslaan en PS =
// opnieuw beginnen. De widget is op zichzelf bruikbaar (de pre-launch-flow roept hem aan tussen kalibratie en start); finished(ok)
// komt als hij klaar is: true = bewaard (of niets te kiezen), false = afgebroken.
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QWidget>
#include <memory>

#include "core/AppCore.h"
#include "core/BigPicture.h"
#include "core/ControlsWalk.h"
#include "ravengun/gamesettings/gamesettings.hpp"

class QCheckBox;
class QGridLayout;
class QVBoxLayout;

namespace rg::ui {

class ControlsSetup : public QWidget {
    Q_OBJECT
public:
    ControlsSetup(rg::AppCore* core, const QString& profile, QWidget* parent = nullptr);
    ~ControlsSetup() override;

    // Moet de rondgang nog gedaan worden: een lightgun-game met toewijsbare signalen waarvan de gebruiker de bediening nog niet
    // bevestigde ([Controls] Confirmed). Leest de gewone lagen (de gebruikerslaag volgt $XDG_DATA_HOME).
    static bool needsSetup(const QString& profile);
    static bool needsSetup(const QString& profile, const ravengun::gamesettings::Dirs& dirs);
    // Heeft de game een [Controls] met iets om te kiezen (voor Properties).
    static bool hasControls(const QString& profile, const ravengun::gamesettings::Dirs& dirs);

    void setGameTitle(const QString& title);
    // Voor tests en voor Properties: de huidige stap en de logica.
    const rg::ControlsWalk& walk() const { return walk_; }
    bool onConfirmPage() const { return walk_.done(); }
    // Een Move-knop (naam uit de engine) alsof de speler hem indrukte; voor tests en de knoppen op het scherm.
    void pressButton(const QString& moveButton);
    // Opslaan (de bevestiging); false met een melding in lastError() als schrijven mislukt.
    bool save();
    QString lastError() const { return error_; }
    // Gelijke knoppen voor alle spelers (standaard aan).
    void setAllPlayers(bool on);

signals:
    void finished(bool ok);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void rebuild();
    void onAim(const rg::bigpicture::AimState& s);
    QString defaultText(const rg::ControlsWalk::Line& l) const;
    static std::map<std::string, std::string> baseLayout(rg::AppCore* core);

    rg::AppCore* core_;
    QString profile_, title_;
    ravengun::gamesettings::Dirs dirs_;
    rg::ControlsWalk walk_;
    std::unique_ptr<rg::AimStream> stream_;
    QElapsedTimer sinceAccepted_;
    QString error_;
    bool allPlayers_ = true;

    QLabel* header_ = nullptr;
    QLabel* label_ = nullptr;
    QLabel* explanation_ = nullptr;
    QLabel* prompt_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* status_ = nullptr;
    QWidget* clickRow_ = nullptr;
    QPushButton *back_ = nullptr, *skip_ = nullptr, *cancel_ = nullptr, *save_ = nullptr, *restart_ = nullptr;
    QLabel* summary_ = nullptr;
    QCheckBox* all_ = nullptr;
    QStackedWidget* pages_ = nullptr;
};

}  // namespace rg::ui
