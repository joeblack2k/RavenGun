// SPDX-License-Identifier: GPL-3.0-or-later
// Controllers-venster naar Dolphin's GamecubeControllersWidget en ControllersPane (Source/Core/DolphinQt/Config/,
// GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08): per poort/speler een rij "Player N", een keuzelijst
// en een knop Configure (NonDefaultQPushButton), in een QGroupBox met QGridLayout. Bij ons: None / Mouse / PS Move.
#pragma once
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <array>

#include "core/AppCore.h"

class QComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace rg::ui {

class ControllersWindow final : public QDialog {
    Q_OBJECT
public:
    explicit ControllersWindow(rg::AppCore* core, QWidget* parent = nullptr);
    static constexpr int kPlayers = 4;
    // Muistoewijzingen staan in de engine alleen in het geheugen; de app bewaart ze en zet ze terug na het verbinden.
    static void ReapplyMouseAssignments(rg::AppCore* core);

private:
    void CreateLayout();
    void Refresh();
    void RefreshMice();
    void Populate();
    void OnTypeChanged(int player);
    void OnConfigure(int player);
    QJsonObject ControllerByKey(const QString& key) const;
    QString SelectedControllerKey() const;

    rg::AppCore* core_;
    QGroupBox* m_players_box = nullptr;
    QGridLayout* m_players_layout = nullptr;
    std::array<QComboBox*, kPlayers> m_combos{};
    std::array<QPushButton*, kPlayers> m_configure{};
    std::array<QLabel*, kPlayers> m_state{};
    QTreeWidget* m_list = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_player = nullptr;
    QPushButton* m_blink = nullptr;
    QPushButton* m_remove = nullptr;
    QLabel* m_mouse_note = nullptr;
    QJsonArray m_controllers;
    QJsonArray m_mice;
    QJsonArray m_assignments;
    bool m_mice_supported = true;
    bool m_updating = false;
};

}  // namespace rg::ui
