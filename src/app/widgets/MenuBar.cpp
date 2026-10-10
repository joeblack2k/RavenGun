// SPDX-License-Identifier: GPL-2.0-or-later
// Naar Dolphin, Source/Core/DolphinQt/MenuBar.cpp (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08).
// Copyright 2015 Dolphin Emulator Project.
#include "widgets/MenuBar.h"

#include <QActionGroup>
#include <QKeySequence>
#include <QMenu>

#include "core/GameListModel.h"
#include "widgets/Resources.h"

namespace rg::ui {

MenuBar::MenuBar(QWidget* parent) : QMenuBar(parent) {
    AddFileMenu();
    AddEngineMenu();
    AddOptionsMenu();
    AddToolsMenu();
    AddViewMenu();
    AddHelpMenu();
    setGameState(false, false);
    setEngineRunning(false);
}

void MenuBar::AddFileMenu() {
    QMenu* file_menu = addMenu(tr("&File"));
    file_menu->addAction(Resources::GetThemeIcon("open"), tr("&Add Folder..."), QKeySequence::Open, this, &MenuBar::AddFolder);
    file_menu->addAction(Resources::GetThemeIcon("refresh"), tr("&Refresh Game List"), QKeySequence::Refresh, this, &MenuBar::RefreshList);
    file_menu->addSeparator();
    file_menu->addAction(tr("E&xit"), QKeySequence::Quit, this, &MenuBar::Exit);
}

void MenuBar::AddEngineMenu() {
    QMenu* m = addMenu(tr("&Engine"));
    m_play_action = m->addAction(Resources::GetThemeIcon("play"), tr("&Play"), this, &MenuBar::Play);
    m_stop_action = m->addAction(Resources::GetThemeIcon("stop"), tr("&Stop"), this, &MenuBar::Stop);
    m->addSeparator();
    m_recenter = m->addAction(tr("&Recenter Aim (Player 1)"), this, &MenuBar::Recenter);
    m->addSeparator();
    m_start_engine = m->addAction(tr("S&tart Engine"), this, &MenuBar::StartEngine);
    m_stop_engine = m->addAction(tr("St&op Engine"), this, &MenuBar::StopEngine);
    m_restart_engine = m->addAction(tr("R&estart Engine"), this, &MenuBar::RestartEngine);
}

void MenuBar::AddOptionsMenu() {
    QMenu* m = addMenu(tr("&Options"));
    m->addAction(Resources::GetThemeIcon("config"), tr("Co&nfiguration..."), QKeySequence::Preferences, this, &MenuBar::Configure);
    m->addSeparator();
    m->addAction(Resources::GetThemeIcon("classic"), tr("&Controller Settings..."), this, &MenuBar::ConfigureControllers);
}

void MenuBar::AddToolsMenu() {
    QMenu* m = addMenu(tr("&Tools"));
    m->addAction(Resources::GetThemeIcon("pair"), tr("&Pair New Controller..."), this, &MenuBar::PairController);
    m->addAction(tr("&Calibrate on TV..."), this, &MenuBar::CalibrateOnTv);
    m->addSeparator();
    m->addAction(tr("&Set Up Game..."), this, &MenuBar::CreateProfile);
    m->addSeparator();
    m->addAction(Resources::GetThemeIcon("folder"), tr("Open C&onfiguration Folder"), this, &MenuBar::OpenConfigFolder);
    m->addSeparator();
    m->addAction(tr("Show &Log..."), this, &MenuBar::ShowLog);
}

void MenuBar::AddViewMenu() {
    QMenu* view_menu = addMenu(tr("&View"));
    m_show_toolbar = view_menu->addAction(tr("Show &Toolbar"));
    m_show_toolbar->setCheckable(true);
    m_show_toolbar->setChecked(true);
    connect(m_show_toolbar, &QAction::toggled, this, &MenuBar::ToggleToolBar);
    view_menu->addSeparator();
    AddGameListTypeSection(view_menu);
    view_menu->addSeparator();
    AddListColumnsMenu(view_menu);
    view_menu->addSeparator();
    view_menu->addAction(tr("&Search"), QKeySequence::Find, this, &MenuBar::ToggleSearch);
    view_menu->addAction(tr("Zoom &In"), QKeySequence::ZoomIn, this, &MenuBar::ZoomIn);
    view_menu->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, this, &MenuBar::ZoomOut);
    view_menu->addSeparator();
    view_menu->addAction(tr("Show &Removed Games Again"), this, &MenuBar::ShowHiddenGames);
}

void MenuBar::AddGameListTypeSection(QMenu* view_menu) {
    m_list_view = view_menu->addAction(tr("List View"));
    m_list_view->setCheckable(true);
    m_grid_view = view_menu->addAction(tr("Grid View"));
    m_grid_view->setCheckable(true);
    auto* list_group = new QActionGroup(this);
    list_group->addAction(m_list_view);
    list_group->addAction(m_grid_view);
    m_list_view->setChecked(true);
    connect(m_list_view, &QAction::triggered, this, &MenuBar::ShowList);
    connect(m_grid_view, &QAction::triggered, this, &MenuBar::ShowGrid);
}

void MenuBar::AddListColumnsMenu(QMenu* view_menu) {
    using Column = rg::GameListModel::Column;
    static const QList<QPair<QString, Column>> columns{
        {tr("Icon"), Column::Icon},       {tr("Platform"), Column::Platform}, {tr("Status"), Column::Status},
        {tr("Input Route"), Column::Route}, {tr("Notes"), Column::Notes},     {tr("Profile"), Column::Profile},
        {tr("Path"), Column::Path}};
    auto* column_group = new QActionGroup(this);
    column_group->setExclusive(false);
    QMenu* cols_menu = view_menu->addMenu(tr("List Columns"));
    for (const auto& [name, col] : columns) {
        QAction* action = column_group->addAction(cols_menu->addAction(name));
        action->setCheckable(true);
        action->setData(static_cast<int>(col));
        m_column_actions << action;
        connect(action, &QAction::toggled, this, [this, col](bool v) { emit ColumnVisibilityToggled(static_cast<int>(col), v); });
    }
}

void MenuBar::AddHelpMenu() {
    QMenu* help_menu = addMenu(tr("&Help"));
    help_menu->addAction(tr("&User Guide"), QKeySequence::HelpContents, this, &MenuBar::ShowUserGuide);
    help_menu->addSeparator();
    help_menu->addAction(tr("&About RavenGun"), this, &MenuBar::ShowAboutDialog);
}

void MenuBar::setGameState(bool canPlay, bool canStop) {
    m_play_action->setEnabled(canPlay);
    m_stop_action->setEnabled(canStop);
}

void MenuBar::setEngineRunning(bool running) {
    m_start_engine->setEnabled(!running);
    m_stop_engine->setEnabled(running);
    m_restart_engine->setEnabled(running);
    m_recenter->setEnabled(running);
}

void MenuBar::setPreferList(bool list) {
    m_list_view->setChecked(list);
    m_grid_view->setChecked(!list);
}

void MenuBar::setColumnVisible(int column, bool visible) {
    for (QAction* a : m_column_actions)
        if (a->data().toInt() == column) {
            const QSignalBlocker b(a);
            a->setChecked(visible);
        }
}

void MenuBar::setToolBarVisible(bool visible) {
    const QSignalBlocker b(m_show_toolbar);
    m_show_toolbar->setChecked(visible);
}

}  // namespace rg::ui
