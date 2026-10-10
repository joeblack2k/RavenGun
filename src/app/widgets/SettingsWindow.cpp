// SPDX-License-Identifier: GPL-3.0-or-later
// Paths-paneel naar Dolphin, Source/Core/DolphinQt/Settings/PathPane.cpp (MakeGameFolderBox; GPL-2.0-or-later,
// commit e6f3ae17627e4344da95b13424af5baf4c892b08).
#include "widgets/SettingsWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include "core/AppPaths.h"
#include "core/Launcher.h"
#include "core/SystemControl.h"
#include "widgets/QtUtils.h"

namespace rg::ui {

namespace {
QLabel* hint(const QString& t) {
    auto* l = new QLabel(t);
    l->setWordWrap(true);
    QFont f = l->font();
    f.setItalic(true);
    l->setFont(f);
    return l;
}
}  // namespace

SettingsWindow::SettingsWindow(rg::AppCore* core, QWidget* parent) : StackedSettingsWindow(parent), core_(core) {
    setWindowTitle(tr("Settings"));
    AddWrappedPane(MakeEnginePane(), tr("Engine"));
    AddWrappedPane(MakeRemotePane(), tr("Remote"));
    AddWrappedPane(MakeDisplayPane(), tr("Display"));
    AddWrappedPane(MakePathsPane(), tr("Paths"));
    AddPane(MakeAdvancedPane(), tr("Advanced"));
    OnDoneCreatingPanes();
    resize(std::max(width(), 820), std::max(height(), 560));
    connect(core_->engine(), &rg::EngineClient::connectedChanged, this, &SettingsWindow::RefreshEngine);
}

// --- Engine ------------------------------------------------------------------------------------------------------------

QWidget* SettingsWindow::MakeEnginePane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* box = new QGroupBox(tr("Engine (ravengund)"));
    auto* form = new QFormLayout(box);
    m_engine_state = new QLabel;
    m_engine_info = new QLabel;
    m_engine_info->setWordWrap(true);
    m_engine_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Status:"), m_engine_state);
    form->addRow(tr("Details:"), m_engine_info);
    auto* buttons = new QHBoxLayout;
    m_start = new NonDefaultQPushButton(tr("Start"));
    m_stop = new NonDefaultQPushButton(tr("Stop"));
    m_restart = new NonDefaultQPushButton(tr("Restart"));
    buttons->addWidget(m_start);
    buttons->addWidget(m_stop);
    buttons->addWidget(m_restart);
    buttons->addStretch(1);
    form->addRow(buttons);
    m_autostart = new QCheckBox(tr("Start the engine automatically when I log in"));
    form->addRow(m_autostart);
    v->addWidget(box);
    v->addWidget(hint(tr("The engine runs in the background as the user service ravengund.service. It reads the PS Moves "
                         "and the camera and drives the virtual lightguns. Restarting it while a game is running makes the "
                         "game lose its lightgun.")));
    v->addStretch(1);

    auto run = [this](const QString& verb) {
        if (verb != QStringLiteral("start") && core_->launcher()->canStop() &&
            QMessageBox::warning(this, tr("Engine"), tr("A game is running and will lose its lightgun. Continue?"),
                                 QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        rg::SystemControl::systemctl({verb, QLatin1String(rg::kEngineUnit)}, this, [this, verb](int code, const QString& out) {
            if (code != 0) QMessageBox::warning(this, tr("Engine"), tr("systemctl --user %1 failed:\n%2").arg(verb, out.trimmed()));
            core_->engine()->refreshNow();
            RefreshEngine();
        });
    };
    connect(m_start, &QPushButton::clicked, this, [run] { run(QStringLiteral("start")); });
    connect(m_stop, &QPushButton::clicked, this, [run] { run(QStringLiteral("stop")); });
    connect(m_restart, &QPushButton::clicked, this, [run] { run(QStringLiteral("restart")); });
    connect(m_autostart, &QCheckBox::clicked, this, [this](bool on) {
        rg::SystemControl::systemctl({on ? QStringLiteral("enable") : QStringLiteral("disable"), QLatin1String(rg::kEngineUnit)}, this,
                                     [this, on](int code, const QString& out) {
                                         if (code != 0)
                                             QMessageBox::warning(this, tr("Engine"),
                                                                  tr("Could not %1 autostart:\n%2").arg(on ? tr("enable") : tr("disable"), out.trimmed()));
                                         RefreshEngine();
                                     });
    });
    RefreshEngine();
    return w;
}

void SettingsWindow::RefreshEngine() {
    rg::SystemControl::unitState(QLatin1String(rg::kEngineUnit), this, [this](const QString& active, const QString& enabled) {
        const bool connected = core_->engine()->isConnected();
        const bool running = active == QStringLiteral("active") || connected;
        m_engine_state->setText(running ? tr("<span style='color:#2e9e44'>&#9679;</span> Running") + (connected ? QString() : tr(" (not answering yet)"))
                                        : tr("<span style='color:#c0392b'>&#9679;</span> Stopped (%1)").arg(active));
        m_start->setEnabled(!running);
        m_stop->setEnabled(running);
        m_restart->setEnabled(running);
        {
            const QSignalBlocker b(m_autostart);
            m_autostart->setChecked(enabled == QStringLiteral("enabled"));
        }
        m_autostart->setEnabled(enabled == QStringLiteral("enabled") || enabled == QStringLiteral("disabled"));
        if (!m_autostart->isEnabled()) m_autostart->setToolTip(tr("The user service is not installed (%1). See scripts/install-service.sh.").arg(enabled));
        m_engine_info->setText(tr("Socket: %1").arg(core_->engine()->socketPath()));
        if (connected) {
            core_->engine()->request(QStringLiteral("info"), {}, [this](const rg::EngineReply& r) {
                if (!r.ok) return;
                const QJsonObject ver = r.object().value(QStringLiteral("version")).toObject();
                const QJsonObject d = r.object().value(QStringLiteral("daemon")).toObject();
                m_engine_info->setText(tr("Version %1 (%2), pid %3, running for %4 min\nSocket: %5")
                                           .arg(ver.value(QStringLiteral("daemon")).toString(), ver.value(QStringLiteral("git_commit")).toString())
                                           .arg(d.value(QStringLiteral("pid")).toInt())
                                           .arg(static_cast<int>(d.value(QStringLiteral("uptime_s")).toDouble() / 60))
                                           .arg(core_->engine()->socketPath()));
            });
        }
    });
}

// --- Remote -----------------------------------------------------------------------------------------------------------

QWidget* SettingsWindow::MakeRemotePane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* box = new QGroupBox(tr("Phone remote (web page)"));
    auto* form = new QFormLayout(box);
    m_lan = new QCheckBox(tr("Allow phones and tablets on the home network to open the RavenGun web page"));
    m_token = new QCheckBox(tr("Require the access token (recommended on shared networks)"));
    m_address = new QLabel;
    m_address->setWordWrap(true);
    m_address->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_address->setOpenExternalLinks(true);
    form->addRow(m_lan);
    form->addRow(m_token);
    form->addRow(tr("Address:"), m_address);
    auto* apply = new NonDefaultQPushButton(tr("Apply and Restart Engine"));
    auto* h = new QHBoxLayout;
    h->addStretch(1);
    h->addWidget(apply);
    form->addRow(h);
    v->addWidget(box);
    v->addWidget(hint(tr("This changes web.lan in ~/.config/ravengun/config.json. The engine reads it when it starts, so it is "
                         "restarted. The page is plain HTTP: use it only on a network you trust.")));
    v->addStretch(1);
    connect(apply, &QPushButton::clicked, this, [this] {
        if (core_->launcher()->canStop() &&
            QMessageBox::warning(this, tr("Remote"), tr("A game is running and will lose its lightgun while the engine restarts. Continue?"),
                                 QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        QString err;
        if (!rg::SystemControl::setEngineWeb(m_lan->isChecked(), m_token->isChecked(), &err)) {
            QMessageBox::warning(this, tr("Remote"), err);
            return;
        }
        rg::SystemControl::systemctl({QStringLiteral("restart"), QLatin1String(rg::kEngineUnit)}, this, [this](int code, const QString& out) {
            if (code != 0) QMessageBox::warning(this, tr("Remote"), tr("Saved, but restarting the engine failed:\n%1").arg(out.trimmed()));
            RefreshRemote();
        });
    });
    RefreshRemote();
    return w;
}

void SettingsWindow::RefreshRemote() {
    const QJsonObject web = rg::SystemControl::engineConfig().value(QStringLiteral("web")).toObject();
    const bool lan = web.value(QStringLiteral("lan")).toBool();
    m_lan->setChecked(lan);
    m_token->setChecked(web.value(QStringLiteral("require_token")).toBool());
    const int port = web.value(QStringLiteral("port")).toInt(8765);
    QStringList lines;
    lines << tr("On this PC: <a href='http://127.0.0.1:%1/'>http://127.0.0.1:%1/</a>").arg(port);
    if (lan) {
        const QString token = web.value(QStringLiteral("require_token")).toBool() ? rg::SystemControl::webToken() : QString();
        for (const QString& ip : rg::SystemControl::lanAddresses()) {
            const QString url = QStringLiteral("http://%1:%2/").arg(ip).arg(port) + (token.isEmpty() ? QString() : QStringLiteral("?token=") + token);
            lines << tr("On your phone: %1").arg(url.toHtmlEscaped());
        }
    } else {
        lines << tr("Not reachable from other devices.");
    }
    m_address->setText(lines.join(QStringLiteral("<br>")));
}

// --- Display ----------------------------------------------------------------------------------------------------------

QWidget* SettingsWindow::MakeDisplayPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* box = new QGroupBox(tr("OLED protection"));
    auto* form = new QFormLayout(box);
    m_oled = new QCheckBox(tr("Show a black screen with a small moving clock when no game is running"));
    m_oled_minutes = new QSpinBox;
    m_oled_minutes->setRange(1, 240);
    m_oled_minutes->setSuffix(tr(" minutes"));
    m_oled_minutes->setValue(std::max(1, rg::SystemControl::oledIdleSeconds() / 60));
    form->addRow(m_oled);
    form->addRow(tr("Wait time:"), m_oled_minutes);
    auto* apply = new NonDefaultQPushButton(tr("Apply"));
    auto* h = new QHBoxLayout;
    h->addStretch(1);
    h->addWidget(apply);
    form->addRow(h);
    v->addWidget(box);
    v->addWidget(hint(tr("Uses the user service ravengun-oled-saver.service. The TV itself stays on (needed for HDMI); black "
                         "pixels are off on an OLED screen.")));
    v->addStretch(1);
    rg::SystemControl::unitState(QLatin1String(rg::kOledUnit), this, [this](const QString& active, const QString& enabled) {
        m_oled->setChecked(active == QStringLiteral("active") || enabled == QStringLiteral("enabled"));
        if (enabled != QStringLiteral("enabled") && enabled != QStringLiteral("disabled"))
            m_oled->setToolTip(tr("The service is not installed (%1).").arg(enabled));
    });
    connect(apply, &QPushButton::clicked, this, [this] {
        QString err;
        const int seconds = m_oled_minutes->value() * 60;
        if (seconds != rg::SystemControl::oledIdleSeconds() && !rg::SystemControl::setOledIdleSeconds(seconds, &err)) {
            QMessageBox::warning(this, tr("OLED protection"), err);
            return;
        }
        const bool on = m_oled->isChecked();
        rg::SystemControl::systemctl({QStringLiteral("daemon-reload")}, this, [this, on](int, const QString&) {
            const QStringList args = on ? QStringList{QStringLiteral("enable"), QStringLiteral("--now"), QLatin1String(rg::kOledUnit)}
                                        : QStringList{QStringLiteral("disable"), QStringLiteral("--now"), QLatin1String(rg::kOledUnit)};
            rg::SystemControl::systemctl(args, this, [this, on](int code, const QString& out) {
                if (code != 0) {
                    QMessageBox::warning(this, tr("OLED protection"), out.trimmed());
                    return;
                }
                if (on) rg::SystemControl::systemctl({QStringLiteral("restart"), QLatin1String(rg::kOledUnit)}, this, {});
            });
        });
    });
    return w;
}

// --- Paths (naar Dolphin PathPane::MakeGameFolderBox) ----------------------------------------------------------------

QWidget* SettingsWindow::MakePathsPane() {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    auto* game_box = new QGroupBox(tr("Game Folders"));
    auto* vlayout = new QVBoxLayout(game_box);
    m_folders = new QListWidget;
    m_folders->setSpacing(1);
    vlayout->addWidget(m_folders);
    auto* hlayout = new QHBoxLayout;
    hlayout->addStretch();
    auto* add = new NonDefaultQPushButton(tr("Add..."));
    m_remove_folder = new NonDefaultQPushButton(tr("Remove"));
    m_remove_folder->setEnabled(false);
    auto* rescan = new NonDefaultQPushButton(tr("Rescan"));
    hlayout->addWidget(rescan);
    hlayout->addWidget(add);
    hlayout->addWidget(m_remove_folder);
    vlayout->addLayout(hlayout);
    m_recursive = new QCheckBox(tr("Search Subfolders"));
    m_recursive->setEnabled(false);
    vlayout->addWidget(m_recursive);
    layout->addWidget(game_box);
    connect(m_folders, &QListWidget::itemSelectionChanged, this, [this] {
        const auto sel = m_folders->selectedItems();
        m_remove_folder->setEnabled(!sel.isEmpty());
        m_recursive->setEnabled(!sel.isEmpty());
        if (!sel.isEmpty()) {
            const QSignalBlocker b(m_recursive);
            m_recursive->setChecked(sel.first()->data(Qt::UserRole).toBool());
        }
    });
    connect(add, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select a Directory"), QDir::homePath());
        if (!dir.isEmpty()) core_->library()->addFolder(dir, true);
    });
    connect(m_remove_folder, &QPushButton::clicked, this, [this] {
        const auto sel = m_folders->selectedItems();
        if (!sel.isEmpty()) core_->library()->removeFolder(sel.first()->text());
    });
    connect(m_recursive, &QCheckBox::toggled, this, [this](bool on) {
        const auto sel = m_folders->selectedItems();
        if (sel.isEmpty()) return;
        core_->library()->setFolderRecursive(sel.first()->text(), on);
        core_->library()->rescan();
    });
    connect(rescan, &QPushButton::clicked, core_->library(), &rg::GameLibrary::rescan);
    connect(core_->library(), &rg::GameLibrary::foldersChanged, this, &SettingsWindow::RefreshFolders);

    // De game-runtime (laders en cores van TeknoParrot) is een afhankelijkheid, zoals een emulatorcore voor een frontend.
    auto* tp = new QGroupBox(tr("TeknoParrot runtime"));
    auto* grid = new QGridLayout(tp);
    grid->setColumnStretch(1, 1);
    m_tp_dir = new QLineEdit(core_->library()->tpDir());
    auto* browse = new NonDefaultQPushButton(QStringLiteral("..."));
    auto* openTp = new NonDefaultQPushButton(tr("Open"));
    grid->addWidget(new QLabel(tr("Runtime folder:")), 0, 0);
    grid->addWidget(m_tp_dir, 0, 1);
    grid->addWidget(browse, 0, 2);
    grid->addWidget(openTp, 0, 3);
    m_runtime_status = new QLabel;
    m_runtime_status->setWordWrap(true);
    m_runtime_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(m_runtime_status, 1, 0, 1, 4);
    auto* launcher = new QComboBox;
    for (const QString& id : rg::backendIds()) {
        const auto b = rg::makeBackend(id, core_->engine(), core_->library(), core_->gameData());
        launcher->addItem(b->displayName(), id);
    }
    const QString cur = core_->launcher()->backend() ? core_->launcher()->backend()->id() : QStringLiteral("auto");
    launcher->setCurrentIndex(std::max(0, launcher->findData(cur)));
    grid->addWidget(new QLabel(tr("Launch games with:")), 2, 0);
    grid->addWidget(launcher, 2, 1, 1, 3);
    grid->addWidget(hint(tr("RavenGun manages your games itself; you never need to open TeknoParrotUi. The TeknoParrot runtime "
                            "(its loaders and cores) is still needed to run the games. RavenGun writes the launch profiles in "
                            "UserProfiles/ from its own settings. The engine uses its own setting for this folder (--tp-dir, "
                            "default %1).")
                             .arg(rg::AppPaths::defaultTpDir())),
                    3, 0, 1, 4);
    layout->addWidget(tp);
    connect(launcher, &QComboBox::currentIndexChanged, this, [this, launcher] {
        const QString id = launcher->currentData().toString();
        core_->library()->setLauncher(id);
        core_->launcher()->setBackend(id);
        RefreshRuntime();
    });
    connect(openTp, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(core_->library()->tpDir())); });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select the TeknoParrot Runtime Folder"), m_tp_dir->text());
        if (!dir.isEmpty()) {
            m_tp_dir->setText(dir);
            core_->library()->setTpDir(dir == rg::AppPaths::defaultTpDir() ? QString() : dir);
            RefreshRuntime();
        }
    });
    connect(m_tp_dir, &QLineEdit::editingFinished, this, [this] {
        const QString dir = m_tp_dir->text().trimmed();
        if (dir == core_->library()->tpDir()) return;
        core_->library()->setTpDir(dir == rg::AppPaths::defaultTpDir() ? QString() : dir);
        RefreshRuntime();
    });
    RefreshRuntime();

    auto* other = new QGroupBox(tr("Other paths"));
    auto* of = new QFormLayout(other);
    auto addPath = [of](const QString& label, const QString& path) {
        auto* e = new QLineEdit(path);
        e->setReadOnly(true);
        of->addRow(label, e);
    };
    addPath(tr("Library file:"), core_->library()->file());
    addPath(tr("Engine settings:"), rg::AppPaths::engineConfigFile());
    addPath(tr("RavenGun folder:"), rg::AppPaths::dataRoot());
    layout->addWidget(other);
    layout->addStretch(1);
    RefreshFolders();
    return w;
}

void SettingsWindow::RefreshRuntime() {
    const rg::RuntimeCheck c = rg::RuntimeCheck::check(core_->library()->tpDir());
    QString why;
    const bool usable = core_->launcher()->backend() && core_->launcher()->backend()->available(&why);
    QString text = QStringLiteral("<span style='color:%1'>&#9679;</span> %2").arg(c.ok ? QStringLiteral("#2e9e44") : QStringLiteral("#c0392b"), c.summary.toHtmlEscaped());
    if (!c.found.isEmpty()) text += QStringLiteral("<br>") + tr("Found: %1").arg(c.found.join(QStringLiteral(", ")).toHtmlEscaped());
    if (!usable) text += QStringLiteral("<br><span style='color:#c0392b'>%1</span>").arg(why.toHtmlEscaped());
    m_runtime_status->setText(text);
}

void SettingsWindow::RefreshFolders() {
    m_folders->clear();
    for (const auto& f : core_->library()->data().folders) {
        auto* it = new QListWidgetItem(f.path, m_folders);
        it->setData(Qt::UserRole, f.recursive);
        it->setToolTip(f.recursive ? tr("Subfolders are searched") : tr("Only the top level is searched"));
    }
}

// --- Advanced ---------------------------------------------------------------------------------------------------------

QWidget* SettingsWindow::MakeAdvancedPane() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    auto* h = new QHBoxLayout;
    auto* which = new QComboBox;
    const QString logs = rg::AppPaths::stateDir() + QStringLiteral("/logs/");
    which->addItem(tr("Engine log"), logs + QStringLiteral("ravengund.log"));
    which->addItem(tr("Last game start"), logs + QStringLiteral("play-last.log"));
    which->addItem(tr("Game list scan"), QString());
    auto* refresh = new NonDefaultQPushButton(tr("Refresh"));
    auto* open = new NonDefaultQPushButton(tr("Open Logs Folder"));
    h->addWidget(new QLabel(tr("Show:")));
    h->addWidget(which, 1);
    h->addWidget(refresh);
    h->addWidget(open);
    v->addLayout(h);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    v->addWidget(m_log, 1);
    m_log_file = which->currentData().toString();
    connect(which, &QComboBox::currentIndexChanged, this, [this, which] {
        m_log_file = which->currentData().toString();
        RefreshLog();
    });
    connect(refresh, &QPushButton::clicked, this, &SettingsWindow::RefreshLog);
    connect(open, &QPushButton::clicked, this, [logs] { QDesktopServices::openUrl(QUrl::fromLocalFile(logs)); });
    RefreshLog();
    return w;
}

void SettingsWindow::RefreshLog() {
    QString text;
    if (m_log_file.isEmpty()) text = core_->library()->lastLog().join(QLatin1Char('\n'));
    else if (QFileInfo::exists(m_log_file)) text = rg::SystemControl::tail(m_log_file, 500);
    else text = tr("(no log yet: %1)").arg(m_log_file);
    m_log->setPlainText(text);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

}  // namespace rg::ui
