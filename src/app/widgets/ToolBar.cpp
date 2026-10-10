// SPDX-License-Identifier: GPL-2.0-or-later
// Naar Dolphin, Source/Core/DolphinQt/ToolBar.cpp (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08).
// Copyright 2015 Dolphin Emulator Project.
#include "widgets/ToolBar.h"

#include <QAction>
#include <algorithm>
#include <vector>

#include "widgets/Resources.h"

namespace rg::ui {

static QSize ICON_SIZE(32, 32);

ToolBar::ToolBar(QWidget* parent) : QToolBar(parent) {
    setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    setMovable(false);
    setFloatable(false);
    setIconSize(ICON_SIZE);
    setWindowTitle(tr("Toolbar"));
    setObjectName(QStringLiteral("toolbar"));
    MakeActions();
    UpdateIcons();
    setGameState(false, false);
}

void ToolBar::MakeActions() {
    m_open_action = addAction(tr("Add Folder"), this, &ToolBar::OpenPressed);
    m_open_action->setToolTip(tr("Add a folder with games to the library"));
    m_refresh_action = addAction(tr("Refresh"), [this] { emit RefreshPressed(); });
    m_refresh_action->setToolTip(tr("Scan the game folders again"));

    addSeparator();

    m_play_action = addAction(tr("Play"), this, &ToolBar::PlayPressed);
    m_stop_action = addAction(tr("Stop"), this, &ToolBar::StopPressed);
    m_fullscreen_action = addAction(tr("FullScr"), this, &ToolBar::FullScreenPressed);
    m_fullscreen_action->setToolTip(tr("Toggle full screen"));

    addSeparator();

    m_config_action = addAction(tr("Config"), this, &ToolBar::SettingsPressed);
    m_controllers_action = addAction(tr("Controllers"), this, &ToolBar::ControllersPressed);

    // Ensure every button has about the same width
    std::vector<QWidget*> items;
    for (const auto& action : {m_open_action, m_refresh_action, m_play_action, m_stop_action, m_fullscreen_action, m_config_action,
                               m_controllers_action})
        items.emplace_back(widgetForAction(action));
    std::vector<int> widths;
    std::ranges::transform(items, std::back_inserter(widths), [](QWidget* item) { return item->sizeHint().width(); });
    const int min_width = static_cast<int>(*std::ranges::max_element(widths) * 0.85);
    for (QWidget* widget : items) widget->setMinimumWidth(min_width);
}

void ToolBar::setGameState(bool canPlay, bool canStop) {
    m_play_action->setEnabled(canPlay);
    m_stop_action->setEnabled(canStop);
}

void ToolBar::setRefreshing(bool refreshing) { m_refresh_action->setEnabled(!refreshing); }

void ToolBar::UpdateIcons() {
    m_open_action->setIcon(Resources::GetThemeIcon("open"));
    m_refresh_action->setIcon(Resources::GetThemeIcon("refresh"));
    m_play_action->setIcon(Resources::GetThemeIcon("play"));
    m_stop_action->setIcon(Resources::GetThemeIcon("stop"));
    m_fullscreen_action->setIcon(Resources::GetThemeIcon("fullscreen"));
    m_config_action->setIcon(Resources::GetThemeIcon("config"));
    m_controllers_action->setIcon(Resources::GetThemeIcon("classic"));
}

}  // namespace rg::ui
