// SPDX-License-Identifier: GPL-3.0-or-later
// Opbouw naar Dolphin MainWindow.cpp (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08).
#include "widgets/MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "core/AppPaths.h"
#include "core/SystemControl.h"
#include "widgets/BigPictureWindow.h"
#include "widgets/ControllersWindow.h"
#include "widgets/CreateProfileDialog.h"
#include "widgets/GameList.h"
#include "widgets/MenuBar.h"
#include "widgets/PairWizardDialog.h"
#include "widgets/PropertiesDialog.h"
#include "widgets/Resources.h"
#include "widgets/SearchBar.h"
#include "widgets/SettingsWindow.h"
#include "widgets/SetupWizard.h"
#include "widgets/SteamIntegration.h"
#include "widgets/ToolBar.h"

namespace rg::ui {

namespace {
QSettings& settings() {
    static QSettings s(rg::AppPaths::appSettingsFile(), QSettings::IniFormat);
    return s;
}

QString batteryShort(const QJsonObject& b) {
    if (!b.value(QStringLiteral("known")).toBool()) return QStringLiteral("battery unknown");
    QString s;
    if (!b.value(QStringLiteral("percent")).isNull()) s = QStringLiteral("%1%").arg(b.value(QStringLiteral("percent")).toInt());
    else s = b.value(QStringLiteral("code")).toString();
    if (b.value(QStringLiteral("charging")).toBool()) s += QStringLiteral(", charging");
    return s;
}
}  // namespace

MainWindow::MainWindow(rg::AppCore* core, QWidget* parent) : QMainWindow(parent), core_(core) {
    setWindowTitle(QStringLiteral("RavenGun"));
    setWindowIcon(Resources::GetAppIcon());
    setUnifiedTitleAndToolBarOnMac(true);
    setAcceptDrops(false);

    CreateComponents();
    ConnectGameList();
    ConnectToolBar();
    ConnectMenuBar();
    ConnectEngine();

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_search_bar);
    layout->addWidget(m_game_list);
    setCentralWidget(central);

    resize(1280, 760);
    restoreGeometry(settings().value(QStringLiteral("mainwindow/geometry")).toByteArray());
    restoreState(settings().value(QStringLiteral("mainwindow/state")).toByteArray());
    m_tool_bar->setVisible(settings().value(QStringLiteral("mainwindow/toolbar"), true).toBool());
    m_menu_bar->setToolBarVisible(m_tool_bar->isVisible());
    m_menu_bar->setPreferList(m_game_list->IsListView());
    for (int c = 0; c < static_cast<int>(rg::GameListModel::Column::Count); ++c) m_menu_bar->setColumnVisible(c, m_game_list->IsColumnVisible(c));
    UpdateActions();
    UpdateStatusBar();
}

MainWindow::~MainWindow() = default;

void MainWindow::CreateComponents() {
    m_menu_bar = new MenuBar(this);
    setMenuBar(m_menu_bar);
    m_tool_bar = new ToolBar(this);
    addToolBar(m_tool_bar);
    m_search_bar = new SearchBar(this);
    m_game_list = new GameList(core_->gameListModel(), this);

    m_engine_label = new QLabel;
    m_moves_label = new QLabel;
    m_game_label = new QLabel;
    for (QLabel* l : {m_engine_label, m_moves_label, m_game_label}) l->setContentsMargins(8, 0, 8, 0);
    statusBar()->addPermanentWidget(m_game_label, 1);
    statusBar()->addPermanentWidget(m_moves_label);
    statusBar()->addPermanentWidget(m_engine_label);
}

void MainWindow::ConnectToolBar() {
    connect(m_tool_bar, &ToolBar::OpenPressed, this, &MainWindow::AddFolder);
    connect(m_tool_bar, &ToolBar::RefreshPressed, core_->library(), &rg::GameLibrary::rescan);
    connect(m_tool_bar, &ToolBar::PlayPressed, this, &MainWindow::Play);
    connect(m_tool_bar, &ToolBar::StopPressed, this, &MainWindow::Stop);
    connect(m_tool_bar, &ToolBar::FullScreenPressed, this, &MainWindow::ToggleFullScreen);
    connect(m_tool_bar, &ToolBar::SettingsPressed, this, [this] { ShowSettingsWindow(); });
    connect(m_tool_bar, &ToolBar::ControllersPressed, this, [this] { ShowControllersWindow(); });
}

void MainWindow::ConnectMenuBar() {
    connect(m_menu_bar, &MenuBar::AddFolder, this, &MainWindow::AddFolder);
    connect(m_menu_bar, &MenuBar::RefreshList, core_->library(), &rg::GameLibrary::rescan);
    connect(m_menu_bar, &MenuBar::Exit, this, &QWidget::close);
    connect(m_menu_bar, &MenuBar::Play, this, &MainWindow::Play);
    connect(m_menu_bar, &MenuBar::Stop, this, &MainWindow::Stop);
    connect(m_menu_bar, &MenuBar::StartEngine, this, [this] { EngineCommand(QStringLiteral("start")); });
    connect(m_menu_bar, &MenuBar::StopEngine, this, [this] { EngineCommand(QStringLiteral("stop")); });
    connect(m_menu_bar, &MenuBar::RestartEngine, this, [this] { EngineCommand(QStringLiteral("restart")); });
    connect(m_menu_bar, &MenuBar::Recenter, this, [this] {
        core_->engine()->request(QStringLiteral("recenter"), {{QStringLiteral("player"), 1}}, [this](const rg::EngineReply& r) {
            statusBar()->showMessage(r.ok ? tr("Aim recentered.") : tr("Recenter failed: %1").arg(r.errorMessage), 4000);
        });
    });
    connect(m_menu_bar, &MenuBar::Configure, this, [this] { ShowSettingsWindow(); });
    connect(m_menu_bar, &MenuBar::ConfigureControllers, this, [this] { ShowControllersWindow(); });
    connect(m_menu_bar, &MenuBar::PairController, this, [this] {
        PairWizardDialog d(core_, this);
        d.exec();
    });
    connect(m_menu_bar, &MenuBar::CalibrateOnTv, this, [this] {
        if (QMessageBox::question(this, tr("Calibrate on TV"),
                                  tr("This opens the calibration screen full screen on the TV.\nContinue?")) != QMessageBox::Yes)
            return;
        core_->engine()->request(QStringLiteral("calibrate_ui"), {{QStringLiteral("action"), QStringLiteral("open")}},
                                 [this](const rg::EngineReply& r) {
                                     if (!r.ok) QMessageBox::warning(this, tr("Calibrate on TV"), r.errorMessage);
                                 });
    });
    connect(m_menu_bar, &MenuBar::CreateProfile, this, [this] {
        const rg::GameEntry* g = SelectedGame();
        if (!g) {
            QMessageBox::information(this, tr("Set Up Game"), tr("Select a game folder in the list first."));
            return;
        }
        CreateProfile(g->id);
    });
    connect(m_menu_bar, &MenuBar::OpenConfigFolder, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(rg::AppPaths::configDir())); });
    connect(m_menu_bar, &MenuBar::ShowLog, this, &MainWindow::ShowLog);
    connect(m_menu_bar, &MenuBar::SetupWizard, this, &MainWindow::ShowSetupWizard);
    connect(m_menu_bar, &MenuBar::AddToSteam, this, [this] { AddToSteamInteractive(this); });
    connect(m_menu_bar, &MenuBar::BigPicture, this, &MainWindow::ShowBigPicture);
    connect(m_menu_bar, &MenuBar::ShowList, this, [this] { SetListView(true); });
    connect(m_menu_bar, &MenuBar::ShowGrid, this, [this] { SetListView(false); });
    connect(m_menu_bar, &MenuBar::ToggleToolBar, this, [this](bool v) {
        m_tool_bar->setVisible(v);
        settings().setValue(QStringLiteral("mainwindow/toolbar"), v);
    });
    connect(m_menu_bar, &MenuBar::ToggleSearch, this, [this] {
        if (m_search_bar->isVisible()) m_search_bar->Hide();
        else m_search_bar->Show();
    });
    connect(m_menu_bar, &MenuBar::ColumnVisibilityToggled, m_game_list, &GameList::SetViewColumn);
    connect(m_menu_bar, &MenuBar::ZoomIn, m_game_list, &GameList::ZoomIn);
    connect(m_menu_bar, &MenuBar::ZoomOut, m_game_list, &GameList::ZoomOut);
    connect(m_menu_bar, &MenuBar::ShowHiddenGames, core_->library(), &rg::GameLibrary::unhideAll);
    connect(m_menu_bar, &MenuBar::ShowUserGuide, this, &MainWindow::ShowUserGuide);
    connect(m_menu_bar, &MenuBar::ShowAboutDialog, this, &MainWindow::ShowAbout);
    connect(m_search_bar, &SearchBar::Search, m_game_list, &GameList::SetSearchTerm);
}

void MainWindow::ConnectGameList() {
    connect(m_game_list, &GameList::GameSelected, this, &MainWindow::Play);
    connect(m_game_list, &GameList::SelectionChanged, this, [this](const QString&) { UpdateActions(); });
    connect(m_game_list, &GameList::OpenProperties, this, [this] { ShowPropertiesFor(m_game_list->GetSelectedGameId()); });
    connect(m_game_list, &GameList::OpenContainingFolder, this, &MainWindow::OpenContainingFolder);
    connect(m_game_list, &GameList::SetDefaultGame, this, [this] { core_->library()->setDefaultGame(m_game_list->GetSelectedGameId()); });
    connect(m_game_list, &GameList::RemoveFromList, this, [this] {
        const rg::GameEntry* g = SelectedGame();
        if (!g) return;
        if (QMessageBox::question(this, tr("Remove from List"),
                                  tr("Remove \"%1\" from the game list?\n\nNo files are deleted. Use View > Show Removed Games "
                                     "Again to bring it back.")
                                      .arg(g->displayName())) == QMessageBox::Yes)
            core_->library()->hideGame(g->id);
    });
    connect(m_game_list, &GameList::CreateProfile, this, [this] { CreateProfile(m_game_list->GetSelectedGameId()); });
    connect(m_game_list, &GameList::AddFolderRequested, this, &MainWindow::AddFolder);
    connect(m_game_list, &GameList::GameCountUpdated, this, [this](int total, int visible) {
        if (total == visible) statusBar()->showMessage(tr("%n game(s)", nullptr, total), 4000);
        else statusBar()->showMessage(tr("Showing %1 of %2 games").arg(visible).arg(total), 4000);
    });
    connect(core_->library(), &rg::GameLibrary::scanStarted, this, [this] {
        m_tool_bar->setRefreshing(true);
        m_game_list->SetRefreshing(true);
    });
    connect(core_->library(), &rg::GameLibrary::scanProgress, this, [this](const QString& w) { statusBar()->showMessage(w, 3000); });
    connect(core_->library(), &rg::GameLibrary::scanFinished, this, [this](int n) {
        m_tool_bar->setRefreshing(false);
        m_game_list->SetRefreshing(false);
        statusBar()->showMessage(tr("Game list refreshed: %n game(s)", nullptr, n), 5000);
    });
    connect(core_->library(), &rg::GameLibrary::tpDirDetected, this, [this](const QString& dir) {
        if (QMessageBox::question(this, tr("Game runtime found"),
                                  tr("A game runtime was found in:\n%1\n\nUse it as the runtime folder (Settings > Paths)?").arg(dir)) ==
            QMessageBox::Yes)
            core_->library()->setTpDir(dir);
    });
}

void MainWindow::ConnectEngine() {
    auto* e = core_->engine();
    connect(e, &rg::EngineClient::connectedChanged, this, [this](bool) {
        UpdateActions();
        UpdateStatusBar();
    });
    connect(e, &rg::EngineClient::controllersUpdated, this, &MainWindow::UpdateStatusBar);
    connect(e, &rg::EngineClient::statusUpdated, this, &MainWindow::UpdateStatusBar);
    connect(core_->launcher(), &rg::GameLauncher::stateChanged, this, [this] {
        UpdateActions();
        UpdateStatusBar();
    });
    connect(core_->launcher(), &rg::GameLauncher::message, this, [this](const QString& text, bool error) {
        if (error) QMessageBox::warning(this, tr("RavenGun"), text);
        else statusBar()->showMessage(text, 8000);
    });
}

const rg::GameEntry* MainWindow::SelectedGame() {
    const QString id = m_game_list->GetSelectedGameId();
    for (const auto& g : core_->library()->visibleGames())
        if (g.id == id) {
            m_selected = g;
            return &m_selected;
        }
    return nullptr;
}

void MainWindow::UpdateActions() {
    const rg::GameEntry* g = SelectedGame();
    const bool playable = g && rg::GameLauncher::whyNotPlayable(*g).isEmpty() && core_->engine()->isConnected() &&
                          !core_->launcher()->canStop();
    const bool canStop = core_->launcher()->canStop();
    m_tool_bar->setGameState(playable, canStop);
    m_menu_bar->setGameState(playable, canStop);
    m_menu_bar->setEngineRunning(core_->engine()->isConnected());
}

void MainWindow::UpdateStatusBar() {
    auto* e = core_->engine();
    if (e->isConnected()) {
        const QString pid = QString::number(e->status().value(QStringLiteral("daemon")).toObject().value(QStringLiteral("pid")).toInt());
        m_engine_label->setText(tr("<span style='color:#2e9e44'>&#9679;</span> Engine: running"));
        m_engine_label->setToolTip(tr("ravengund is running (pid %1)\nSocket: %2").arg(pid, e->socketPath()));
    } else {
        m_engine_label->setText(tr("<span style='color:#c0392b'>&#9679;</span> Engine: stopped"));
        m_engine_label->setToolTip(tr("The engine (ravengund) is not running. Engine > Start Engine starts it."));
    }
    QStringList moves;
    for (const auto& v : e->controllers().value(QStringLiteral("controllers")).toArray()) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("removed")).toBool()) continue;
        const bool move = c.value(QStringLiteral("type")).toString() == QStringLiteral("move");
        const QString who = c.value(QStringLiteral("name")).toString();
        const int player = c.value(QStringLiteral("player")).toInt();
        QString s = player > 0 ? QStringLiteral("%1 (P%2)").arg(who).arg(player) : who;
        if (c.value(QStringLiteral("connected")).toBool()) s += QStringLiteral(": ") + batteryShort(c.value(QStringLiteral("battery")).toObject());
        else s += tr(": off");
        if (move || c.value(QStringLiteral("connected")).toBool()) moves << s;
    }
    m_moves_label->setText(moves.isEmpty() ? (e->isConnected() ? tr("No PS Move") : QString()) : moves.join(QStringLiteral("  |  ")));
    m_game_label->setText(core_->launcher()->text());
}

void MainWindow::SetListView(bool list) {
    if (list) m_game_list->SetListView();
    else m_game_list->SetGridView();
    m_menu_bar->setPreferList(list);
}

void MainWindow::Play() {
    const rg::GameEntry* g = SelectedGame();
    if (!g) {
        // Dolphin: zonder selectie de standaardgame.
        const QString def = core_->library()->data().defaultGame;
        for (const auto& x : core_->library()->visibleGames())
            if (x.id == def) {
                m_selected = x;
                g = &m_selected;
            }
    }
    if (!g) {
        statusBar()->showMessage(tr("Select a game first."), 4000);
        return;
    }
    if (g->kind == rg::GameKind::Detected) {
        if (QMessageBox::question(this, tr("Play"),
                                  tr("\"%1\" is not set up yet. Set it up now?").arg(g->displayName())) ==
            QMessageBox::Yes)
            CreateProfile(g->id);
        return;
    }
    if (g->kind == rg::GameKind::Unconfigured) {
        ShowPropertiesFor(g->id);
        return;
    }
    core_->launcher()->play(*g);
}

void MainWindow::Stop() {
    if (QMessageBox::question(this, tr("Stop"), tr("Stop the running game?")) == QMessageBox::Yes) core_->launcher()->stop();
}

void MainWindow::AddFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add a Folder with Games"),
                                                          settings().value(QStringLiteral("lastfolder"), QDir::homePath()).toString());
    if (dir.isEmpty()) return;
    settings().setValue(QStringLiteral("lastfolder"), QFileInfo(dir).path());
    core_->library()->addFolder(dir, true);
    statusBar()->showMessage(tr("Scanning %1 ...").arg(dir), 5000);
}

void MainWindow::OpenContainingFolder() {
    const rg::GameEntry* g = SelectedGame();
    if (!g) return;
    QString dir = g->folder;
    if (dir.isEmpty() && !g->exePath.isEmpty()) dir = QFileInfo(g->exePath).path();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        QMessageBox::information(this, tr("Open Containing Folder"), tr("The game folder is not available."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::CreateProfile(const QString& gameId) {
    for (const auto& g : core_->library()->visibleGames())
        if (g.id == gameId) {
            CreateProfileDialog d(core_, g, this);
            d.exec();
            return;
        }
}

void MainWindow::ShowPropertiesFor(const QString& gameId, int pane, bool modal) {
    for (const auto& g : core_->library()->visibleGames()) {
        if (g.id != gameId) continue;
        auto* d = new PropertiesDialog(core_, g, this);
        d->setAttribute(Qt::WA_DeleteOnClose, true);
        d->ActivatePane(pane);
        if (modal) d->exec();
        else d->show();
        return;
    }
}

ControllersWindow* MainWindow::ShowControllersWindow() {
    if (!m_controllers_window) m_controllers_window = new ControllersWindow(core_, this);
    m_controllers_window->show();
    m_controllers_window->raise();
    m_controllers_window->activateWindow();
    return m_controllers_window;
}

SettingsWindow* MainWindow::ShowSettingsWindow(int pane) {
    if (!m_settings_window) m_settings_window = new SettingsWindow(core_, this);
    m_settings_window->ActivatePane(pane);
    m_settings_window->show();
    m_settings_window->raise();
    m_settings_window->activateWindow();
    return m_settings_window;
}

void MainWindow::EngineCommand(const QString& verb) {
    const bool gameActive = core_->launcher()->canStop();
    if ((verb == QStringLiteral("stop") || verb == QStringLiteral("restart")) && gameActive &&
        QMessageBox::warning(this, tr("Engine"),
                             tr("A game is running. Stopping the engine releases the lightgun and the game will lose its input.\n"
                                "Continue?"),
                             QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    statusBar()->showMessage(tr("Engine: %1...").arg(verb), 4000);
    rg::SystemControl::systemctl({verb, QLatin1String(rg::kEngineUnit)}, this, [this, verb](int code, const QString& out) {
        if (code != 0) QMessageBox::warning(this, tr("Engine"), tr("systemctl --user %1 failed:\n%2").arg(verb, out.trimmed()));
        core_->engine()->refreshNow();
    });
}

void MainWindow::ShowLog() { ShowSettingsWindow(4); }

void MainWindow::ShowSetupWizard() {
    SetupWizard w(core_, this);
    w.exec();
}

void MainWindow::ShowBigPicture() {
    // Dezelfde fullscreen-weergave als `ravengun --fullscreen`; Exit brengt je hier terug.
    if (!m_big_picture) {
        m_big_picture = new BigPictureWindow(core_);
        m_big_picture->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_big_picture, &BigPictureWindow::ExitRequested, this, [this] {
            if (m_big_picture) m_big_picture->deleteLater();
            show();
            raise();
            activateWindow();
        });
    }
    hide();
    m_big_picture->Open();
}

void MainWindow::ShowUserGuide() {
    const QString path = rg::AppPaths::repoRoot() + QStringLiteral("/docs/user-guide.md");
    QFile f(path);
    QDialog d(this);
    d.setWindowTitle(tr("RavenGun User Guide"));
    auto* l = new QVBoxLayout(&d);
    auto* b = new QTextBrowser;
    b->setOpenExternalLinks(true);
    if (f.open(QIODevice::ReadOnly)) b->setMarkdown(QString::fromUtf8(f.readAll()));
    else b->setPlainText(tr("The user guide was not found (%1).").arg(path));
    l->addWidget(b);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    l->addWidget(bb);
    d.resize(820, 680);
    d.exec();
}

void MainWindow::ShowAbout() {
    QMessageBox::about(this, tr("About RavenGun"),
                       tr("<h3>RavenGun</h3>"
                          "<p>Use a PlayStation Move as a lightgun for arcade games on SteamOS.</p>"
                          "<p>Licensed under the GNU General Public License, version 3 or later.</p>"
                          "<p>The user interface is modelled on the Dolphin emulator and reuses parts of its source code "
                          "(GPL-2.0-or-later). See THIRD_PARTY.md.</p>"
                          "<p>Qt %1</p>")
                           .arg(QString::fromLatin1(qVersion())));
}

void MainWindow::ToggleFullScreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    settings().setValue(QStringLiteral("mainwindow/geometry"), saveGeometry());
    settings().setValue(QStringLiteral("mainwindow/state"), saveState());
    QMainWindow::closeEvent(event);
}

}  // namespace rg::ui
