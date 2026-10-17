// SPDX-License-Identifier: GPL-3.0-or-later
// Hoofdvenster, opgebouwd als dat van Dolphin (Source/Core/DolphinQt/MainWindow.cpp, GPL-2.0-or-later, commit
// e6f3ae17627e4344da95b13424af5baf4c892b08): CreateComponents, ConnectMenuBar, ConnectToolBar, ConnectGameList,
// een QStackedWidget met de gamelijst en de zoekbalk erboven, plus een statusbalk (engine, Moves, actieve game).
#pragma once
#include <QMainWindow>
#include <QPointer>

#include "core/AppCore.h"

class QLabel;

namespace rg::ui {

class GameList;
class MenuBar;
class SearchBar;
class ToolBar;
class ControllersWindow;
class SettingsWindow;
class BigPictureWindow;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(rg::AppCore* core, QWidget* parent = nullptr);
    ~MainWindow() override;

    rg::AppCore* core() { return core_; }
    GameList* gameList() { return m_game_list; }

    // Voor schermafdrukken en het rechtermuismenu.
    void ShowPropertiesFor(const QString& gameId, int pane = 0, bool modal = true);
    ControllersWindow* ShowControllersWindow();
    SettingsWindow* ShowSettingsWindow(int pane = 0);
    void SetListView(bool list);

    void Play();
    void Stop();
    void ShowSetupWizard();
    void ShowComponents();
    void ShowBigPicture();

private:
    void CreateComponents();
    void ConnectMenuBar();
    void ConnectToolBar();
    void ConnectGameList();
    void ConnectEngine();
    void UpdateActions();
    void UpdateStatusBar();
    void AddFolder();
    void OpenContainingFolder();
    void CreateProfile(const QString& gameId);
    void EngineCommand(const QString& verb);
    void ShowLog();
    void ShowAbout();
    void ShowUserGuide();
    void ToggleFullScreen();
    const rg::GameEntry* SelectedGame();
    void closeEvent(QCloseEvent* event) override;

    rg::AppCore* core_;
    MenuBar* m_menu_bar = nullptr;
    ToolBar* m_tool_bar = nullptr;
    SearchBar* m_search_bar = nullptr;
    GameList* m_game_list = nullptr;
    QLabel* m_engine_label = nullptr;
    QLabel* m_moves_label = nullptr;
    QLabel* m_game_label = nullptr;
    QPointer<ControllersWindow> m_controllers_window;
    QPointer<SettingsWindow> m_settings_window;
    QPointer<BigPictureWindow> m_big_picture;
    rg::GameEntry m_selected;
};

}  // namespace rg::ui
