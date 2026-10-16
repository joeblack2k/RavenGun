// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/SetupWizard.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "core/AppPaths.h"
#include "core/Launcher.h"
#include "core/SetupChecks.h"
#include "core/SteamShortcuts.h"
#include "core/SystemControl.h"
#include "widgets/PairWizardDialog.h"
#include "widgets/QtUtils.h"
#include "widgets/SteamIntegration.h"

namespace rg::ui {

namespace {

QSettings& settings() {
    static QSettings s(rg::AppPaths::appSettingsFile(), QSettings::IniFormat);
    return s;
}

QLabel* para(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QString mark(int state) {  // 1 = in orde, 0 = ontbreekt, -1 = optioneel/onbekend
    if (state > 0) return QStringLiteral("<span style='color:#2e9e44'>&#10004;</span> ");
    if (state == 0) return QStringLiteral("<span style='color:#c0392b'>&#10008;</span> ");
    return QStringLiteral("<span style='color:#b06a00'>&#9679;</span> ");
}

QPlainTextEdit* outputBox() {
    auto* e = new QPlainTextEdit;
    e->setReadOnly(true);
    e->setMaximumBlockCount(400);
    e->setMinimumHeight(110);
    e->setPlaceholderText(QObject::tr("Output appears here."));
    QFont f = e->font();
    f.setFamily(QStringLiteral("monospace"));
    e->setFont(f);
    return e;
}

QWizardPage* makePage(const QString& title, const QString& subtitle) {
    auto* p = new QWizardPage;
    p->setTitle(title);
    p->setSubTitle(subtitle);
    new QVBoxLayout(p);
    return p;
}

}  // namespace

bool SetupWizard::ShouldShowAtStart() {
    if (rg::SetupChecks::isGameMode()) return false;
    return !settings().value(QStringLiteral("wizard/done"), false).toBool();
}

void SetupWizard::MarkDone(bool done) { settings().setValue(QStringLiteral("wizard/done"), done); }

SetupWizard::SetupWizard(rg::AppCore* core, QWidget* parent) : QWizard(parent), core_(core) {
    setWindowTitle(tr("RavenGun Setup"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::HaveCustomButton1, false);
    setPage(Welcome, MakeWelcome());
    setPage(Permissions, MakePermissions());
    setPage(Service, MakeService());
    setPage(Controllers, MakeControllers());
    setPage(Calibration, MakeCalibration());
    setPage(Games, MakeGames());
    setPage(Steam, MakeSteam());
    setPage(Done, MakeDone());
    setStartId(Welcome);
    resize(820, 600);
    connect(core_->engine(), &rg::EngineClient::controllersUpdated, this, &SetupWizard::UpdateControllers);
    connect(core_->engine(), &rg::EngineClient::connectedChanged, this, [this] {
        RefreshChecks();
        UpdateControllers();
    });
    connect(core_->library(), &rg::GameLibrary::gamesChanged, this, &SetupWizard::UpdateGames);
    connect(core_->library(), &rg::GameLibrary::scanStarted, this, &SetupWizard::UpdateGames);
    connect(core_->library(), &rg::GameLibrary::scanFinished, this, &SetupWizard::UpdateGames);
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        if (id == Welcome) RefreshChecks();
        if (id == Service) UpdateService();
        if (id == Controllers) UpdateControllers();
        if (id == Games) UpdateGames();
        if (id == Steam) UpdateSteam();
    });
    RefreshChecks();
    UpdateControllers();
    UpdateGames();
    UpdateSteam();
}

void SetupWizard::done(int result) {
    // Afgerond of afgebroken: niet opnieuw vanzelf tonen (Tools > Setup Wizard blijft kunnen), tenzij de gebruiker het vinkje uitzet.
    MarkDone(dont_show_ ? dont_show_->isChecked() || result != QDialog::Accepted : true);
    QWizard::done(result);
}

// --- 1. Welkom en controle --------------------------------------------------------------------------------------------

QWizardPage* SetupWizard::MakeWelcome() {
    QWizardPage* p = makePage(tr("Welcome to RavenGun"), tr("RavenGun turns a PlayStation Move into a lightgun for arcade games. "
                                                        "This wizard sets up everything once, here in Desktop Mode."));
    auto* v = static_cast<QVBoxLayout*>(p->layout());
    v->addWidget(para(tr("First a quick check of this PC:")));
    for (QLabel** l : {&check_engine_, &check_running_, &check_perms_, &check_service_, &check_camera_, &check_runtime_, &check_steam_}) {
        *l = para(QString());
        v->addWidget(*l);
    }
    auto* again = new NonDefaultQPushButton(tr("Check Again"));
    connect(again, &QPushButton::clicked, this, &SetupWizard::RefreshChecks);
    auto* h = new QHBoxLayout;
    h->addWidget(again);
    h->addStretch(1);
    v->addLayout(h);
    v->addStretch(1);
    v->addWidget(para(tr("Press <b>Next</b> to fix what is missing. Steps that are already done can be skipped.")));
    return p;
}

void SetupWizard::RefreshChecks() {
    const QString engine = rg::SetupChecks::engineBinary();
    check_engine_->setText(mark(engine.isEmpty() ? 0 : 1) +
                           (engine.isEmpty() ? tr("<b>Engine program</b>: not found. Reinstall RavenGun or build it (see docs/build.md).")
                                             : tr("<b>Engine program</b>: %1").arg(engine.toHtmlEscaped())));
    const bool running = core_->engine()->isConnected();
    check_running_->setText(mark(running ? 1 : 0) + (running ? tr("<b>Engine</b>: running") : tr("<b>Engine</b>: not running (step 3 starts it)")));
    check_service_->setText(mark(rg::SetupChecks::serviceInstalled() ? 1 : 0) +
                            (rg::SetupChecks::serviceInstalled() ? tr("<b>Start at login</b>: installed")
                                                                 : tr("<b>Start at login</b>: not installed (step 3)")));
    const auto cam = rg::SetupChecks::camera();
    check_camera_->setText(mark(cam.ps3eye ? 1 : -1) +
                           (cam.ps3eye ? tr("<b>Camera</b>: %1 (%2)").arg(cam.name, cam.device)
                            : cam.found ? tr("<b>Camera</b>: %1 (%2); a PlayStation Eye is optional").arg(cam.name.toHtmlEscaped(), cam.device)
                                        : tr("<b>Camera</b>: none (optional; aiming works without it)")));
    const rg::RuntimeCheck rc = rg::RuntimeCheck::check(core_->library()->tpDir());
    check_runtime_->setText(mark(rc.ok ? 1 : 0) +
                            (rc.ok ? tr("<b>Game runtime</b> (TeknoParrot): %1").arg(rc.dir.toHtmlEscaped())
                                               : tr("<b>Game runtime</b> (TeknoParrot): not found or incomplete. Choose it later in "
                                                    "Settings > Paths.")));
    const QString steamRoot = rg::SteamShortcuts::steamRoot();
    QString where;
    const bool inSteam = !steamRoot.isEmpty() && IsInSteam(&where);
    check_steam_->setText(mark(inSteam ? 1 : (steamRoot.isEmpty() ? -1 : 0)) +
                          (inSteam ? tr("<b>Steam</b>: RavenGun is in your library")
                           : steamRoot.isEmpty() ? tr("<b>Steam</b>: not found")
                                                 : tr("<b>Steam</b>: RavenGun is not in your library yet (step 7)")));
    check_perms_->setText(mark(-1) + tr("<b>Permissions</b>: checking..."));
    rg::SetupChecks::checkPermissions(this, [this](bool ok, const QString& out) {
        perms_ok_ = ok;
        check_perms_->setText(mark(ok ? 1 : 0) + (ok ? tr("<b>Permissions</b>: installed (virtual lightgun, Bluetooth pairing)")
                                                     : tr("<b>Permissions</b>: missing or outdated (step 2)")));
        check_perms_->setToolTip(out.trimmed());
        if (perms_status_) {
            perms_status_->setText(ok ? mark(1) + tr("Everything is installed and up to date. You can skip this step.")
                                      : mark(0) + tr("Not installed or outdated. Press <b>Install</b>."));
            perms_status_->setToolTip(out.trimmed());
        }
    });
}

// --- 2. Rechten ---------------------------------------------------------------------------------------------------------

QWizardPage* SetupWizard::MakePermissions() {
    QWizardPage* p = makePage(tr("Permissions"), tr("RavenGun needs a few system permissions. They are installed in one go; you are "
                                                "asked for your password once."));
    auto* v = static_cast<QVBoxLayout*>(p->layout());
    v->addWidget(para(tr("What is installed:<ul>"
                         "<li><b>Device rules</b> (/etc/udev/rules.d/70-ravengun.rules, 71-ravengun-uhid.rules and 72-ravengun-dongle.rules): games can "
                         "read the virtual lightguns and keyboard of RavenGun, and the desktop ignores them. The last rule only gives "
                         "you access to USB dongles of Aladdin/SafeNet/Thales and Wibu-Systems, so the maker's own runtime can use "
                         "a dongle you own.</li>"
                         "<li><b>Bluetooth helper</b> (/opt/ravengun/bin/ravengun-bt-helper) with a rule in "
                         "/etc/sudoers.d/ravengun-bt-helper: RavenGun may pair and unpair PS Move controllers without asking "
                         "for a password. The rule allows only this one helper, nothing else.</li></ul>"
                         "These survive SteamOS updates. To remove them later, run <tt>uninstall-user.sh</tt> (in the scripts folder "
                         "of RavenGun), or <tt>install-permissions.sh --rollback</tt> with administrator rights.")));
    perms_status_ = para(mark(-1) + tr("Checking..."));
    v->addWidget(perms_status_);
    perms_install_ = new NonDefaultQPushButton(tr("Install (asks for your password)"));
    auto* h = new QHBoxLayout;
    h->addWidget(perms_install_);
    h->addStretch(1);
    v->addLayout(h);
    perms_out_ = outputBox();
    v->addWidget(perms_out_, 1);
    connect(perms_install_, &QPushButton::clicked, this, [this] {
        const rg::CommandLine c = rg::SetupChecks::permissionsInstall();
        if (!c.valid()) {
            perms_out_->setPlainText(tr("scripts/install-permissions.sh was not found next to the app."));
            return;
        }
        perms_out_->setPlainText(QStringLiteral("$ %1 %2\n").arg(c.program, c.args.join(QLatin1Char(' '))));
        RunCommand(c.program, c.args, perms_out_, perms_install_, [this](bool ok) {
            perms_out_->appendPlainText(ok ? tr("\nDone.") : tr("\nNot installed (cancelled or failed)."));
            RefreshChecks();
        }, 300000);
    });
    return p;
}

void SetupWizard::RunCommand(const QString& program, const QStringList& args, QPlainTextEdit* out, QPushButton* button,
                             const std::function<void(bool)>& after, int timeoutMs) {
    button->setEnabled(false);
    rg::SystemControl::run(program, args, this, [out, button, after](int code, const QString& text) {
        out->appendPlainText(text.trimmed());
        if (code == -2) out->appendPlainText(QObject::tr("Could not start the program."));
        button->setEnabled(true);
        after(code == 0);
    }, timeoutMs);
}

// --- 3. Engine als service ----------------------------------------------------------------------------------------------

QWizardPage* SetupWizard::MakeService() {
    QWizardPage* p = makePage(tr("Engine"), tr("The engine is the background service that reads the PS Move and drives the lightgun."));
    auto* v = static_cast<QVBoxLayout*>(p->layout());
    v->addWidget(para(tr("RavenGun installs the engine as a user service (no administrator rights): it starts when you log in, "
                         "in Desktop Mode and in Game Mode. File: ~/.config/systemd/user/ravengund.service.")));
    service_status_ = para(QString());
    v->addWidget(service_status_);
    service_install_ = new NonDefaultQPushButton(tr("Install and Start"));
    auto* h = new QHBoxLayout;
    h->addWidget(service_install_);
    h->addStretch(1);
    v->addLayout(h);
    service_out_ = outputBox();
    v->addWidget(service_out_, 1);
    connect(service_install_, &QPushButton::clicked, this, [this] {
        const rg::CommandLine c = rg::SetupChecks::serviceInstall();
        if (!c.valid()) {
            service_out_->setPlainText(tr("scripts/install-service.sh was not found next to the app."));
            return;
        }
        service_out_->setPlainText(QStringLiteral("$ %1\n").arg(c.args.join(QLatin1Char(' '))));
        RunCommand(c.program, c.args, service_out_, service_install_, [this](bool) {
            core_->engine()->refreshNow();
            UpdateService();
        }, 60000);
    });
    return p;
}

void SetupWizard::UpdateService() {
    const bool installed = rg::SetupChecks::serviceInstalled();
    const bool running = core_->engine()->isConnected();
    if (installed && running) service_status_->setText(mark(1) + tr("Installed and running. You can skip this step."));
    else if (running) service_status_->setText(mark(-1) + tr("The engine is running, but it does not start at login yet."));
    else if (installed) service_status_->setText(mark(0) + tr("Installed, but the engine is not running. Press <b>Install and Start</b>."));
    else service_status_->setText(mark(0) + tr("Not installed yet."));
}

// --- 4. Controllers -----------------------------------------------------------------------------------------------------

QWizardPage* SetupWizard::MakeControllers() {
    QWizardPage* p = makePage(tr("Controllers"), tr("Pair your PS Move (and a Navigation Controller, if you have one)."));
    auto* v = static_cast<QVBoxLayout*>(p->layout());
    v->addWidget(para(tr("Pairing takes a USB cable once: plug the Move in, press <b>Pair a Controller</b>, follow the steps, "
                         "then unplug it and press the PS button. After that it connects by Bluetooth.")));
    controllers_status_ = para(QString());
    v->addWidget(controllers_status_);
    auto* pair = new NonDefaultQPushButton(tr("Pair a Controller..."));
    connect(pair, &QPushButton::clicked, this, [this] {
        PairWizardDialog d(core_, this);
        d.exec();
        core_->engine()->refreshNow();
    });
    auto* h = new QHBoxLayout;
    h->addWidget(pair);
    h->addStretch(1);
    v->addLayout(h);
    v->addStretch(1);
    return p;
}

void SetupWizard::UpdateControllers() {
    if (!controllers_status_) return;
    if (!core_->engine()->isConnected()) {
        controllers_status_->setText(mark(0) + tr("The engine is not running, so controllers cannot be paired yet (step 3)."));
        return;
    }
    QStringList moves;
    for (const auto& v : core_->engine()->controllers().value(QStringLiteral("controllers")).toArray()) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("removed")).toBool()) continue;
        const QString type = c.value(QStringLiteral("type")).toString() == QStringLiteral("navigation") ? tr("Navigation Controller") : tr("PS Move");
        moves << QStringLiteral("%1 \"%2\" (player %3, %4)")
                     .arg(type, c.value(QStringLiteral("name")).toString().toHtmlEscaped())
                     .arg(c.value(QStringLiteral("player")).toInt())
                     .arg(c.value(QStringLiteral("connected")).toBool() ? tr("connected") : tr("off"));
    }
    controllers_status_->setText(moves.isEmpty() ? mark(0) + tr("No controller paired yet.")
                                                 : mark(1) + tr("Paired:") + QStringLiteral("<br>") + moves.join(QStringLiteral("<br>")));
}

// --- 5. Kalibratie ------------------------------------------------------------------------------------------------------

QWizardPage* SetupWizard::MakeCalibration() {
    QWizardPage* p = makePage(tr("Calibration"), tr("Teach RavenGun where your TV is."));
    auto* v = static_cast<QVBoxLayout*>(p->layout());
    v->addWidget(para(tr("The calibration screen opens full screen on the TV and is operated with the gun itself (trigger = "
                         "next). It calibrates the compass of the Move: hold the gun as when playing and turn it slowly in all "
                         "directions for about 40 seconds. This keeps the aim from drifting. You can do this later in "
                         "Controllers > Configure > Calibration.")));
    calib_status_ = para(QString());
    v->addWidget(calib_status_);
    auto* open = new NonDefaultQPushButton(tr("Open Calibration on the TV"));
    connect(open, &QPushButton::clicked, this, [this] {
        core_->engine()->request(QStringLiteral("calibrate_ui"), {{QStringLiteral("action"), QStringLiteral("open")}},
                                 [this](const rg::EngineReply& r) {
                                     calib_status_->setText(r.ok ? mark(1) + tr("The calibration screen is open on the TV.")
                                                                 : mark(0) + r.errorMessage.toHtmlEscaped());
                                 });
    });
    auto* h = new QHBoxLayout;
    h->addWidget(open);
    h->addStretch(1);
    v->addLayout(h);
    v->addStretch(1);
    return p;
}

// --- 6. Games -----------------------------------------------------------------------------------------------------------

QWizardPage* SetupWizard::MakeGames() {
    QWizardPage* p = makePage(tr("Games"), tr("Tell RavenGun where your games are."));
    auto* v = static_cast<QVBoxLayout*>(p->layout());
    v->addWidget(para(tr("Choose the folder with your arcade games, for example the TeknoParrot folder on your NAS. RavenGun "
                         "searches it and recognises the games by their files. Nothing in the folder is changed.")));
    games_status_ = para(QString());
    v->addWidget(games_status_);
    auto* add = new NonDefaultQPushButton(tr("Add Folder..."));
    connect(add, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Add a Folder with Games"), QDir::homePath());
        if (dir.isEmpty()) return;
        core_->library()->addFolder(dir, true);
        UpdateGames();
    });
    auto* h = new QHBoxLayout;
    h->addWidget(add);
    h->addStretch(1);
    v->addLayout(h);
    v->addStretch(1);
    return p;
}

void SetupWizard::UpdateGames() {
    if (!games_status_) return;
    const auto& d = core_->library()->data();
    QStringList folders;
    for (const auto& f : d.folders) folders << f.path.toHtmlEscaped();
    const QList<rg::GameEntry> games = core_->library()->visibleGames();
    int ready = 0, playable = 0;
    for (const auto& g : games) {
        if (g.hasProfile()) ++ready;
        if (g.hasProfile()) {
            const QString st = core_->gameData()->playState(g.profile);
            if (st == QStringLiteral("playable") || st == QStringLiteral("working")) ++playable;
        }
    }
    QString t = folders.isEmpty() ? mark(0) + tr("No game folder yet.") : mark(1) + tr("Game folders:") + QStringLiteral("<br>") + folders.join(QStringLiteral("<br>"));
    t += QStringLiteral("<br><br>");
    t += core_->library()->scanning() ? tr("Searching for games...")
                                      : tr("%n game(s) found, %1 ready to play, %2 known to work well.", nullptr, static_cast<int>(games.size()))
                                            .arg(ready)
                                            .arg(playable);
    games_status_->setText(t);
}

// --- 7. Steam -----------------------------------------------------------------------------------------------------------

QWizardPage* SetupWizard::MakeSteam() {
    QWizardPage* p = makePage(tr("Steam"), tr("Add RavenGun to your Steam library, so you can start it in Game Mode."));
    auto* v = static_cast<QVBoxLayout*>(p->layout());
    v->addWidget(para(tr("RavenGun is added as a non-Steam game that opens full screen (big icons, made for the lightgun). "
                         "Steam has to be closed for a moment; RavenGun asks before it closes Steam. A backup of your list of "
                         "non-Steam games is kept.")));
    steam_status_ = para(QString());
    v->addWidget(steam_status_);
    steam_add_ = new NonDefaultQPushButton(tr("Add to Steam"));
    connect(steam_add_, &QPushButton::clicked, this, [this] {
        AddToSteamInteractive(this);
        UpdateSteam();
    });
    auto* h = new QHBoxLayout;
    h->addWidget(steam_add_);
    h->addStretch(1);
    v->addLayout(h);
    v->addStretch(1);
    return p;
}

void SetupWizard::UpdateSteam() {
    if (!steam_status_) return;
    const QString root = rg::SteamShortcuts::steamRoot();
    if (root.isEmpty()) {
        steam_status_->setText(mark(-1) + tr("Steam was not found on this PC."));
        steam_add_->setEnabled(false);
        return;
    }
    const QList<rg::SteamUser> users = rg::SteamShortcuts::users(root);
    QString where;
    if (IsInSteam(&where)) steam_status_->setText(mark(1) + tr("RavenGun is in your Steam library. You can skip this step."));
    else if (users.isEmpty()) steam_status_->setText(mark(0) + tr("No Steam user found. Log in to Steam once first."));
    else
        steam_status_->setText(mark(0) + tr("Not in your library yet (user: %1).")
                                             .arg((users.first().name.isEmpty() ? users.first().accountId : users.first().name).toHtmlEscaped()));
    steam_add_->setEnabled(!users.isEmpty());
}

// --- 8. Klaar -----------------------------------------------------------------------------------------------------------

QWizardPage* SetupWizard::MakeDone() {
    QWizardPage* p = makePage(tr("All Set"), tr("RavenGun is ready."));
    auto* v = static_cast<QVBoxLayout*>(p->layout());
    v->addWidget(para(tr("<p><b>Reboot into Game Mode and start RavenGun from your library.</b></p>"
                         "<p>In Game Mode RavenGun opens full screen: aim at a game and shoot it, then shoot again (or hold "
                         "the trigger) to start it. The PS button centers the aim. When the game ends, RavenGun comes back.</p>"
                         "<p>Here on the desktop you can change game settings, controllers and paths at any time. This wizard "
                         "is in <b>Tools > Setup Wizard</b>.</p>")));
    v->addStretch(1);
    dont_show_ = new QCheckBox(tr("Do not show this wizard at start again"));
    dont_show_->setChecked(true);
    v->addWidget(dont_show_);
    p->setFinalPage(true);
    return p;
}

}  // namespace rg::ui
