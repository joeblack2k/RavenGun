// SPDX-License-Identifier: GPL-3.0-or-later
// Instellingen (Options > Configuration) als Dolphin's SettingsWindow (Source/Core/DolphinQt/Config/SettingsWindow.cpp,
// GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08): een StackedSettingsWindow met panelen Engine, Remote,
// Display, Paths (naar Dolphin's PathPane: "Game Folders" met Add.../Remove en "Search Subfolders") en Advanced.
#pragma once
#include "core/AppCore.h"
#include "widgets/StackedSettingsWindow.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace rg::ui {

class SettingsWindow final : public StackedSettingsWindow {
    Q_OBJECT
public:
    SettingsWindow(rg::AppCore* core, QWidget* parent = nullptr);

private:
    QWidget* MakeEnginePane();
    QWidget* MakeRemotePane();
    QWidget* MakeDisplayPane();
    QWidget* MakePathsPane();
    QWidget* MakeAdvancedPane();
    void RefreshEngine();
    void RefreshRemote();
    void RefreshFolders();
    void RefreshLog();

    rg::AppCore* core_;
    // Engine
    QLabel* m_engine_state = nullptr;
    QLabel* m_engine_info = nullptr;
    QCheckBox* m_autostart = nullptr;
    QPushButton* m_start = nullptr;
    QPushButton* m_stop = nullptr;
    QPushButton* m_restart = nullptr;
    // Remote
    QCheckBox* m_lan = nullptr;
    QCheckBox* m_token = nullptr;
    QLabel* m_address = nullptr;
    // Display
    QCheckBox* m_oled = nullptr;
    QSpinBox* m_oled_minutes = nullptr;
    QComboBox* m_game_camera = nullptr;
    // Paths
    QListWidget* m_folders = nullptr;
    QPushButton* m_remove_folder = nullptr;
    QCheckBox* m_recursive = nullptr;
    QLineEdit* m_tp_dir = nullptr;
    QLabel* m_runtime_status = nullptr;
    void RefreshRuntime();
    // Advanced
    QPlainTextEdit* m_log = nullptr;
    QString m_log_file;
};

}  // namespace rg::ui
