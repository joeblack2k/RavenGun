// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Maakt op verzoek van de gebruiker een TeknoParrot-gebruikersprofiel voor een herkende game, met het bestaande
// scripts/tp-add-game.py (PROFIEL PAD [--view] [--fullscreen] [--gamescope] [--force]). Het script schrijft alleen lokaal
// (UserProfiles/ in de TeknoParrot-map en de view-map); de game zelf blijft waar hij staat.
#include <QDialog>

#include "core/AppCore.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace rg::ui {

class CreateProfileDialog final : public QDialog {
    Q_OBJECT
public:
    CreateProfileDialog(rg::AppCore* core, const rg::GameEntry& game, QWidget* parent = nullptr);
    // Argumenten voor tp-add-game.py (zonder python3 -I en het scriptpad); voor tests.
    static QStringList BuildArguments(const QString& profile, const QString& exe, bool view, bool fullscreen, bool gamescope, bool force);

private:
    void Run();
    void UpdateState();

    rg::AppCore* core_;
    rg::GameEntry game_;
    QComboBox* m_profile = nullptr;
    QLineEdit* m_exe = nullptr;
    QCheckBox* m_view = nullptr;
    QCheckBox* m_fullscreen = nullptr;
    QCheckBox* m_gamescope = nullptr;
    QCheckBox* m_force = nullptr;
    QLabel* m_exists = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QPushButton* m_create = nullptr;
};

}  // namespace rg::ui
