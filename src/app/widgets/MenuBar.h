// SPDX-License-Identifier: GPL-2.0-or-later
// Naar Dolphin, Source/Core/DolphinQt/MenuBar.{h,cpp} (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08):
// menuopbouw (File, Emulation, Options, Tools, View, Help), AddGameListTypeSection (List View / Grid View) en
// AddListColumnsMenu. Copyright 2015 Dolphin Emulator Project. Emulation heet hier Engine.
#pragma once
#include <QMenuBar>

class QAction;

namespace rg::ui {

class MenuBar final : public QMenuBar {
    Q_OBJECT
public:
    explicit MenuBar(QWidget* parent = nullptr);
    void setGameState(bool canPlay, bool canStop);
    void setEngineRunning(bool running);
    void setPreferList(bool list);
    void setColumnVisible(int column, bool visible);
    void setToolBarVisible(bool visible);

signals:
    // File
    void AddFolder();
    void RefreshList();
    void Exit();
    // Engine
    void Play();
    void Stop();
    void StartEngine();
    void StopEngine();
    void RestartEngine();
    void Recenter();
    // Options
    void Configure();
    void ConfigureControllers();
    // Tools
    void PairController();
    void CalibrateOnTv();
    void CreateProfile();
    void OpenConfigFolder();
    void ShowLog();
    // View
    void ShowList();
    void ShowGrid();
    void ToggleToolBar(bool visible);
    void ToggleSearch();
    void ColumnVisibilityToggled(int column, bool visible);
    void ZoomIn();
    void ZoomOut();
    void ShowHiddenGames();
    // Help
    void ShowUserGuide();
    void ShowAboutDialog();

private:
    void AddFileMenu();
    void AddEngineMenu();
    void AddOptionsMenu();
    void AddToolsMenu();
    void AddViewMenu();
    void AddHelpMenu();
    void AddGameListTypeSection(QMenu* view_menu);
    void AddListColumnsMenu(QMenu* view_menu);

    QAction* m_play_action;
    QAction* m_stop_action;
    QAction* m_start_engine;
    QAction* m_stop_engine;
    QAction* m_restart_engine;
    QAction* m_recenter;
    QAction* m_list_view;
    QAction* m_grid_view;
    QAction* m_show_toolbar;
    QList<QAction*> m_column_actions;
};

}  // namespace rg::ui
