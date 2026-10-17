// SPDX-License-Identifier: GPL-3.0-or-later
// RavenGun: de app (Qt 6 Widgets). Opties:
//   --fullscreen           fullscreen-modus ("big picture") voor Steam Game Mode en de tv, zonder het hoofdvenster
//   --play PROFIEL         een game starten met het startscherm (PS Move aan, korte kalibratie; docs/prelaunch.md), schermvullend
//                          en zonder hoofdvenster; het proces blijft tot de game stopt (Steam en snelkoppelingen). Exitcode 0 =
//                          gespeeld, 1 = mislukt, 2 = afgebroken. SIGTERM (Steam: "Exit game") stopt de game.
//   --saver                OLED-bescherming: zwart schermvullend venster met een bewegend statusbord (geen engine; stopt bij SIGTERM/SIGINT)
//   --saver-loop           de lus van de OLED-bescherming (unit ravengun-oled-saver; geen scherm nodig, zie core/SaverLoop.h)
//   --saver-stop-view      het zwarte scherm (unit ravengun-oled-view) dichtdoen; ExecStopPost van diezelfde unit
//   --saver-snapshot PNG  het statusbord als PNG bewaren (zonder iets te tonen; --saver-size BxH, standaard 1920x1080) en stoppen
//   --saver-check          toon wat het statusbord zou tonen en stop (exitcode 1 als het statusbestand ontbreekt of niet klopt)
//   --add-to-steam         RavenGun als niet-Steam-game toevoegen (zonder vragen; weigert als Steam draait) en stoppen
//   --steam-account ID     bij --add-to-steam: deze Steam-gebruiker (map onder userdata/) in plaats van de laatst ingelogde
//   --list-steam-shortcuts de niet-Steam-games van de laatst ingelogde gebruiker tonen (alleen lezen) en stoppen
//   --setup-wizard         de eerste-gebruik-wizard openen
//   --install              RavenGun uit deze (uitgepakte) map naar ~/.local/opt/ravengun installeren, zonder venster (zoals de knop in de wizard) en stoppen
//   --components           de onderdelen (id, naam, versie, grootte, stand) tonen, zonder venster, en stoppen
//   --install-component ID een onderdeel installeren (met wat het nodig heeft; herhaalbaar; "all" = alles wat ontbreekt), zonder venster;
//                          --install-components id1,id2 kan ook. Exitcode 0 = gelukt, 1 = mislukt (reden op stderr)
//   --no-start             bij --install: de engine niet starten of herstarten
//   --uninstall            RavenGun verwijderen (zoals de knop in de wizard; vraagt om het wachtwoord voor de systeemonderdelen) en stoppen
//   --remove-config        bij --uninstall: ook de instellingen en de bibliotheek verwijderen
//   --screenshot-dir MAP   maak schermafdrukken van alle vensters en stop (ontwikkelaars; zie widgets/Screenshots.h)
//   --screenshot-game ID   game voor Properties in de schermafdrukken (standaard tp:Halo)
//   --socket PAD           andere engine-socket (standaard $XDG_RUNTIME_DIR/ravengun/ravengund.sock)
//   --library PAD          ander bibliotheekbestand (standaard ~/.config/ravengun/library.json)
//   --no-scan              niet scannen bij het starten
// $RAVENGUN_STEAM_ROOT: een andere Steam-map (een kopie, voor tests); dan geldt de controle "Steam draait" niet.
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QDateTime>
#include <QSocketNotifier>
#include <QTimer>
#include <csignal>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/AppCore.h"
#include "core/AppPaths.h"
#include "core/ComponentsCli.h"
#include "core/Install.h"
#include "core/SaverLoop.h"
#include "core/SetupChecks.h"
#include "core/SteamShortcuts.h"
#include "widgets/BigPictureWindow.h"
#include "widgets/ControllersWindow.h"
#include "widgets/MainWindow.h"
#include "widgets/PreLaunchWindow.h"
#include "widgets/Resources.h"
#include "widgets/SaverWindow.h"
#include "widgets/Screenshots.h"
#include "widgets/SetupWizard.h"
#include "widgets/SteamIntegration.h"

namespace {
int g_sigPipe[2] = {-1, -1};
// Signaalhandler: alleen een byte in de pipe schrijven (async-signal-safe); de Qt-lus sluit de app daarna netjes af.
void onSignal(int) {
    const char c = 1;
    if (g_sigPipe[1] >= 0) (void)!write(g_sigPipe[1], &c, 1);
}
}  // namespace

int main(int argc, char** argv) {
    // Onder gamescope (Steam Game Mode) is Xwayland de betrouwbare weg: Qt kiest anders misschien de eigen Wayland-socket
    // van gamescope, die niet alle protocollen heeft. Een expliciete QT_QPA_PLATFORM gaat altijd voor.
    if (!std::getenv("QT_QPA_PLATFORM") && rg::SetupChecks::isGameMode() && std::getenv("DISPLAY")) setenv("QT_QPA_PLATFORM", "xcb", 1);

    // Installeren en verwijderen zonder venster (ook vanaf een terminal of ssh zonder grafische sessie): dan het offscreen-platform,
    // anders breekt QApplication af op een ontbrekend scherm (gemeten: --install deed zonder DISPLAY niets).
    for (int i = 1; i < argc; ++i)
        if ((std::strcmp(argv[i], "--install") == 0 || std::strcmp(argv[i], "--uninstall") == 0 || std::strcmp(argv[i], "--components") == 0 ||
             std::strncmp(argv[i], "--install-component", 19) == 0) && !std::getenv("QT_QPA_PLATFORM") &&
            !std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY"))
            setenv("QT_QPA_PLATFORM", "offscreen", 1);

    // De unit van de OLED-bescherming: geen QApplication (en dus geen scherm) nodig. Alleen als eerste en enige argument.
    if (argc == 2 && std::strcmp(argv[1], "--saver-loop") == 0) return rg::saver::RunSaverLoop();
    if (argc == 2 && std::strcmp(argv[1], "--saver-stop-view") == 0) return rg::saver::RunStopView();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RavenGun"));
    QApplication::setApplicationDisplayName(QStringLiteral("RavenGun"));
    QApplication::setDesktopFileName(QStringLiteral("ravengun-qt"));
    QApplication::setApplicationVersion(QStringLiteral(RAVENGUN_APP_VERSION));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("RavenGun: PS Move lightgun games"));
    p.addHelpOption();
    p.addVersionOption();
    const QCommandLineOption fullscreen(QStringLiteral("fullscreen"), QStringLiteral("Big Picture mode: full screen, for Steam Game Mode and the TV."));
    const QCommandLineOption play(QStringLiteral("play"),
                                  QStringLiteral("Start a game full screen after the pre-launch checks (PS Move on, short aim calibration) and "
                                                 "stay until the game ends. For Steam and desktop shortcuts."),
                                  QStringLiteral("PROFILE"));
    const QCommandLineOption addToSteam(QStringLiteral("add-to-steam"), QStringLiteral("Add RavenGun to the Steam library (Steam must be closed) and quit."));
    const QCommandLineOption steamAccount(QStringLiteral("steam-account"), QStringLiteral("Steam user (folder under userdata/) for --add-to-steam."),
                                          QStringLiteral("ID"));
    const QCommandLineOption listSteam(QStringLiteral("list-steam-shortcuts"), QStringLiteral("Show the non-Steam games of the Steam user and quit."));
    const QCommandLineOption printPaths(QStringLiteral("print-paths"), QStringLiteral("Show where RavenGun finds its scripts, data and engine, and quit."));
    const QCommandLineOption wizard(QStringLiteral("setup-wizard"), QStringLiteral("Open the setup wizard."));
    const QCommandLineOption installCmd(QStringLiteral("install"), QStringLiteral("Install RavenGun from this folder into ~/.local/opt/ravengun (no window) and quit."));
    const QCommandLineOption componentsCmd(QStringLiteral("components"), QStringLiteral("List the components (id, name, version, size, state) without a window and quit."));
    const QCommandLineOption installComponent(QStringLiteral("install-component"),
                                              QStringLiteral("Install a component (and what it needs) without a window and quit; repeatable, \"all\" = everything missing. Exit 0 = ok, 1 = failed."),
                                              QStringLiteral("ID"));
    const QCommandLineOption installComponents(QStringLiteral("install-components"), QStringLiteral("Like --install-component, with a comma-separated list."),
                                               QStringLiteral("ID,ID"));
    const QCommandLineOption noStart(QStringLiteral("no-start"), QStringLiteral("With --install: do not start or restart the engine."));
    const QCommandLineOption uninstall(QStringLiteral("uninstall"), QStringLiteral("Remove RavenGun (service, shortcuts, program folder, device rules) and quit."));
    const QCommandLineOption removeConfig(QStringLiteral("remove-config"), QStringLiteral("With --uninstall: also remove your settings and library."));
    const QCommandLineOption shotDir(QStringLiteral("screenshot-dir"), QStringLiteral("Save screenshots of all windows to DIR and quit."),
                                     QStringLiteral("DIR"));
    const QCommandLineOption shotGame(QStringLiteral("screenshot-game"), QStringLiteral("Game id for the Properties screenshots."),
                                      QStringLiteral("ID"), QStringLiteral("tp:Halo"));
    const QCommandLineOption socket(QStringLiteral("socket"), QStringLiteral("Engine socket path."), QStringLiteral("PATH"));
    const QCommandLineOption library(QStringLiteral("library"), QStringLiteral("Library file."), QStringLiteral("PATH"));
    const QCommandLineOption noScan(QStringLiteral("no-scan"), QStringLiteral("Do not scan the game folders at start."));
    const QCommandLineOption saver(QStringLiteral("saver"), QStringLiteral("OLED screen saver: black full-screen window with a dim moving clock."));
    const QCommandLineOption saverCheck(QStringLiteral("saver-check"),
                                        QStringLiteral("Read the saver status file, print what the saver would show and quit (exit 1 if the file is missing or invalid)."));
    const QCommandLineOption saverFile(QStringLiteral("saver-file"), QStringLiteral("Status file for --saver-check (default: the saver's own file)."),
                                       QStringLiteral("PATH"));
    const QCommandLineOption saverSnap(QStringLiteral("saver-snapshot"), QStringLiteral("Render the saver board to a PNG (use with QT_QPA_PLATFORM=offscreen) and quit."),
                                       QStringLiteral("PNG"));
    const QCommandLineOption saverSize(QStringLiteral("saver-size"), QStringLiteral("Size for --saver-snapshot, WxH."), QStringLiteral("WxH"),
                                       QStringLiteral("1920x1080"));
    p.addOptions({saverCheck, saverFile, saverSnap, saverSize});
    p.addOptions({saver, fullscreen, play, addToSteam, steamAccount, listSteam, printPaths, wizard, installCmd, componentsCmd, installComponent, installComponents, noStart, uninstall, removeConfig, shotDir, shotGame, socket, library, noScan});
    p.process(app);

    // Iconen uit het bureaubladthema (KDE: breeze) ook buiten een Plasma-sessie (bv. offscreen).
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QStringLiteral("hicolor"))
        if (QFileInfo(QStringLiteral("/usr/share/icons/breeze")).isDir()) QIcon::setThemeName(QStringLiteral("breeze"));
    QApplication::setWindowIcon(rg::ui::Resources::GetAppIcon());

    if (p.isSet(printPaths)) {
        std::printf("data-dir: %s\n", qPrintable(rg::AppPaths::dataRoot()));
        std::printf("bin-dir: %s\n", qPrintable(rg::AppPaths::binDir()));
        std::printf("engine: %s\n", qPrintable(rg::SetupChecks::engineBinary()));
        std::printf("setup-helper: %s\n", qPrintable(rg::SetupChecks::setupHelperBinary()));
        return 0;
    }
    if (p.isSet(installCmd)) {
        const QString tree = rg::Install::runningTreePrefix(rg::AppPaths::binDir());
        if (tree.isEmpty()) {
            std::fprintf(stderr, "Error: RavenGun is not running from a complete release folder (bin/, lib/ and share/ravengun): %s\n",
                         qPrintable(rg::AppPaths::binDir()));
            return 1;
        }
        rg::Install::InstallOptions io;
        io.start = !p.isSet(noStart);
        const rg::OpResult r = rg::Install::installTree(tree, rg::InstallLayout::current(), io, [](const QString& s) { std::printf("%s\n", qPrintable(s)); });
        if (!r.ok) std::fprintf(stderr, "Error: %s\n", qPrintable(r.error));
        return r.ok ? 0 : 1;
    }
    if (p.isSet(componentsCmd) || p.isSet(installComponent) || p.isSet(installComponents)) {
        // Zonder venster, dezelfde code als de Components-pagina (ComponentInstaller).
        QString cerr;
        QList<rg::Component> all = rg::Components::catalogue(&cerr);
        all += rg::Components::modComponents();
        if (!cerr.isEmpty()) {
            std::fprintf(stderr, "Error: catalogue: %s\n", qPrintable(cerr));
            return 1;
        }
        const rg::ComponentEnv cenv = rg::ComponentEnv::standard();
        if (p.isSet(componentsCmd)) return rg::ComponentsCli::list(all, cenv, stdout);
        const QStringList ids = rg::ComponentsCli::splitIds(p.values(installComponent) + p.values(installComponents));
        if (ids.isEmpty()) {
            std::fprintf(stderr, "Error: no component id given\n");
            return 1;
        }
        return rg::ComponentsCli::install(all, cenv, ids, stdout, stderr);
    }
    if (p.isSet(uninstall)) {
        const rg::InstallLayout layout = rg::InstallLayout::current();
        rg::Install::UninstallOptions opt;
        opt.removeConfig = p.isSet(removeConfig);
        opt.systemPartsPresent = rg::SetupChecks::systemPartsInstalled();
        const rg::CommandLine rm = rg::SetupChecks::permissionsRemove();
        opt.setupHelper = rm.args.value(0);
        opt.pkexec = rm.program;
        const rg::OpResult r = rg::Install::uninstall(layout, opt, [](const QString& s) { std::printf("%s\n", qPrintable(s)); });
        if (!r.ok) std::fprintf(stderr, "Error: %s\n", qPrintable(r.error));
        return r.ok ? 0 : 1;
    }
    if (p.isSet(listSteam)) {
        const QList<rg::SteamUser> users = rg::SteamShortcuts::users(rg::SteamShortcuts::steamRoot());
        if (users.isEmpty()) {
            std::fprintf(stderr, "No Steam user found (Steam root: %s)\n", qPrintable(rg::SteamShortcuts::steamRoot()));
            return 1;
        }
        const QString vdf = users.first().configDir() + QStringLiteral("/shortcuts.vdf");
        QString err;
        const auto entries = rg::SteamShortcuts::list(vdf, &err);
        if (!err.isEmpty()) {
            std::fprintf(stderr, "%s: %s\n", qPrintable(vdf), qPrintable(err));
            return 1;
        }
        std::printf("%s (%d)\n", qPrintable(vdf), static_cast<int>(entries.size()));
        for (const auto& e : entries)
            std::printf("  %-28s appid %-10u exe %s  options %s\n", qPrintable(e.appName), e.appId, qPrintable(e.exe), qPrintable(e.launchOptions));
        return 0;
    }
    if (p.isSet(addToSteam)) {
        const rg::ui::SteamAddOutcome o = rg::ui::AddToSteamQuiet(p.value(steamAccount));
        std::fprintf(o.ok ? stdout : stderr, "%s\n", qPrintable(o.message));
        return o.ok ? 0 : 3;
    }

    if (p.isSet(saverCheck)) {
        // Wat het statusbord zou tonen, als gewone regels. Dezelfde ontleder als het venster: een status die hier
        // wordt afgekeurd, toont het bord ook als "geen status".
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const rg::ui::SaverStatus st = rg::ui::SaverWindow::LoadStatus(p.isSet(saverFile) ? p.value(saverFile) : rg::ui::SaverWindow::StatusPath());
        const QStringList lines = rg::ui::SaverWindow::Lines(st, rg::ui::SaverWindow::LoadGame(rg::ui::SaverWindow::GamePath(), now), now);
        for (const QString& l : lines) std::printf("%s\n", qPrintable(l));
        return st.ok ? 0 : 1;
    }

    if (p.isSet(saverSnap)) {
        const QStringList wh = p.value(saverSize).split(QLatin1Char('x'));
        const QSize size(wh.value(0).toInt(), wh.value(1).toInt());
        if (wh.size() != 2 || size.width() < 320 || size.height() < 240) {
            std::fprintf(stderr, "--saver-size: use WxH, for example 1920x1080\n");
            return 2;
        }
        rg::ui::SaverWindow saverWin;
        if (!saverWin.SaveSnapshot(p.value(saverSnap), size)) {
            std::fprintf(stderr, "could not write %s\n", qPrintable(p.value(saverSnap)));
            return 1;
        }
        return 0;
    }

    if (p.isSet(saver)) {
        // Zonder engine, hoofdvenster of tray. SIGTERM/SIGINT sluiten de lus.
        if (pipe(g_sigPipe) == 0) {
            auto* sn = new QSocketNotifier(g_sigPipe[0], QSocketNotifier::Read, &app);
            QObject::connect(sn, &QSocketNotifier::activated, &app, [] { QApplication::quit(); });
            struct sigaction sa {};
            sa.sa_handler = onSignal;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGTERM, &sa, nullptr);
            sigaction(SIGINT, &sa, nullptr);
        }
        rg::ui::SaverWindow saverWin;
        saverWin.Open();
        return app.exec();
    }

    rg::AppCore core(nullptr, p.value(socket), p.value(library));
    QObject::connect(core.engine(), &rg::EngineClient::connectedChanged, &core, [&core](bool c) {
        if (c) rg::ui::ControllersWindow::ReapplyMouseAssignments(&core);
    });

    if (p.isSet(play) && !p.isSet(shotDir)) {
        // Steam (Game Mode) en snelkoppelingen: alleen het startscherm en dan de game. Geen scan (de bibliotheek uit de cache).
        const QString profile = p.value(play);
        rg::GameEntry entry;
        core.start(false);
        for (const auto& g : core.library()->data().games)   // ook een verborgen game
            if (g.hasProfile() && g.profile == profile) entry = g;
        for (const auto& g : core.library()->visibleGames())   // met de eigen naam en instellingen van de gebruiker
            if (g.hasProfile() && g.profile == profile) entry = g;
        if (entry.profile.isEmpty()) {
            // Niet (meer) in de bibliotheek: toch proberen, de engine kent het profiel of weigert met een melding.
            entry.kind = rg::GameKind::TeknoParrot;
            entry.id = QStringLiteral("tp:") + profile;
            entry.profile = profile;
            entry.name = profile;
        }
        rg::ui::PreLaunchWindow w(&core, entry, rg::ui::PreLaunchWindow::Mode::Play);
        QObject::connect(&w, &rg::ui::PreLaunchWindow::Finished, &app, [](int code) { QApplication::exit(code); });
        // Steam sluit een niet-Steam-game met SIGTERM ("Exit game"): de game draait in een eigen unit en moet dan mee stoppen.
        if (pipe(g_sigPipe) == 0) {
            auto* sn = new QSocketNotifier(g_sigPipe[0], QSocketNotifier::Read, &app);
            QObject::connect(sn, &QSocketNotifier::activated, &w, [&w] { w.Abort(); });
            struct sigaction sa {};
            sa.sa_handler = onSignal;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGTERM, &sa, nullptr);
            sigaction(SIGINT, &sa, nullptr);
            sigaction(SIGHUP, &sa, nullptr);
        }
        w.Begin();
        return app.exec();
    }

    if (p.isSet(fullscreen) && !p.isSet(shotDir)) {
        // Game Mode / tv: alleen de fullscreen-weergave. Exit sluit de app.
        rg::ui::BigPictureWindow big(&core);
        QObject::connect(&big, &rg::ui::BigPictureWindow::ExitRequested, &app, &QApplication::quit);
        core.start(!p.isSet(noScan));
        big.Open();
        return app.exec();
    }

    rg::ui::MainWindow window(&core);
    core.start(!p.isSet(noScan));

    if (p.isSet(shotDir)) {
        rg::ui::RunScreenshots(&window, p.value(shotDir), p.value(shotGame));
    } else {
        window.show();
        // Eerste start in Desktop Mode (of op verzoek): de wizard, zodra het hoofdvenster er staat.
        if (p.isSet(wizard) || rg::ui::SetupWizard::ShouldShowAtStart())
            QTimer::singleShot(400, &window, [&window] { window.ShowSetupWizard(); });
    }
    return app.exec();
}
