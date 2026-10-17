// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Eerste-gebruik-wizard (A6), alleen in Desktop Mode: bij de eerste start of via Tools > Setup Wizard. Acht stappen:
// 1 welkom, controle en installeren (draait de app uit een uitgepakt archief, dan kopieert "Install RavenGun" de boom naar
// ~/.local/opt/ravengun en start de app daaruit opnieuw; rg::Install), 2 rechten (één beheerdersprompt via pkexec op
// ravengun-setup-helper, of overslaan als het al in orde is), 3 engine als gebruikersservice (unitbestand uit het sjabloon +
// systemctl --user), 4 controllers koppelen, 5 kalibratie op de tv, 6 gamemap toevoegen en scannen, 7 toevoegen aan Steam,
// 8 klaar. Geen scripts: alles is C++ met vaste paden en argumenten (rg::Install, rg::SetupChecks); niets zonder klik.
#include <QWizard>
#include <functional>

#include "core/AppCore.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;

namespace rg::ui {

class SetupWizard final : public QWizard {
    Q_OBJECT
public:
    enum Page { Welcome, Permissions, Service, Controllers, Calibration, Games, Steam, Done, Count };
    explicit SetupWizard(rg::AppCore* core, QWidget* parent = nullptr);

    // Moet de wizard bij het starten verschijnen? (Desktop Mode en nog niet afgerond of overgeslagen)
    static bool ShouldShowAtStart();
    static void MarkDone(bool done = true);

    void RefreshChecks();  // stap 1 (en de status op de andere pagina's) opnieuw controleren
    void done(int result) override;  // afgerond of afgebroken: niet opnieuw vanzelf tonen

private:
    QWizardPage* MakeWelcome();
    QWizardPage* MakePermissions();
    QWizardPage* MakeService();
    QWizardPage* MakeControllers();
    QWizardPage* MakeCalibration();
    QWizardPage* MakeGames();
    QWizardPage* MakeSteam();
    QWizardPage* MakeDone();
    void RunCommand(const QString& program, const QStringList& args, QPlainTextEdit* out, QPushButton* button,
                    const std::function<void(bool)>& after, int timeoutMs);
    void UpdateControllers();
    void UpdateGames();
    void UpdateSteam();
    void UpdateService();
    void UpdateInstallBox();   // stap 1: installeren/verwijderen van RavenGun zelf
    void RunInstall();
    void RunUninstall();
    void RunPermissionsRemove();

    rg::AppCore* core_;
    // Stap 1
    QLabel* install_status_ = nullptr;
    QPushButton* install_button_ = nullptr;
    QPushButton* uninstall_button_ = nullptr;
    QPlainTextEdit* install_out_ = nullptr;
    QLabel* check_install_ = nullptr;
    QLabel* check_engine_ = nullptr;
    QLabel* check_running_ = nullptr;
    QLabel* check_perms_ = nullptr;
    QLabel* check_service_ = nullptr;
    QLabel* check_camera_ = nullptr;
    QLabel* check_runtime_ = nullptr;
    QLabel* check_steam_ = nullptr;
    // Stap 2
    QLabel* perms_status_ = nullptr;
    QPlainTextEdit* perms_out_ = nullptr;
    QPushButton* perms_install_ = nullptr;
    QPushButton* perms_remove_ = nullptr;
    bool perms_ok_ = false;
    // Stap 3
    QLabel* service_status_ = nullptr;
    QPlainTextEdit* service_out_ = nullptr;
    QPushButton* service_install_ = nullptr;
    // Stap 4-7
    QLabel* controllers_status_ = nullptr;
    QLabel* calib_status_ = nullptr;
    QLabel* games_status_ = nullptr;
    QLabel* steam_status_ = nullptr;
    QPushButton* steam_add_ = nullptr;
    // Stap 8
    QCheckBox* dont_show_ = nullptr;
};

}  // namespace rg::ui
