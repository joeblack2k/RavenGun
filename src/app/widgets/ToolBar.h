// SPDX-License-Identifier: GPL-2.0-or-later
// Naar Dolphin, Source/Core/DolphinQt/ToolBar.{h,cpp} (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08).
// Copyright 2015 Dolphin Emulator Project. Aanpassingen voor RavenGun: geen debugknoppen, Pause en ScrShot; wel Add Folder
// (Dolphin: Open), Refresh, Play, Stop, FullScr, Config en Controllers. Play/Stop volgen de toestand van de game.
#pragma once
#include <QToolBar>

class QAction;

namespace rg::ui {

class ToolBar final : public QToolBar {
    Q_OBJECT
public:
    explicit ToolBar(QWidget* parent = nullptr);
    void setGameState(bool canPlay, bool canStop);
    void setRefreshing(bool refreshing);

signals:
    void OpenPressed();
    void RefreshPressed();
    void PlayPressed();
    void StopPressed();
    void FullScreenPressed();
    void SettingsPressed();
    void ControllersPressed();

private:
    void MakeActions();
    void UpdateIcons();
    QAction* m_open_action;
    QAction* m_refresh_action;
    QAction* m_play_action;
    QAction* m_stop_action;
    QAction* m_fullscreen_action;
    QAction* m_config_action;
    QAction* m_controllers_action;
};

}  // namespace rg::ui
