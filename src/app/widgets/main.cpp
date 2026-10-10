// SPDX-License-Identifier: GPL-3.0-or-later
// RavenGun: de app (Qt 6 Widgets). Opties:
//   --fullscreen           fullscreen-modus ("big picture") voor Steam Game Mode en de tv, zonder het hoofdvenster
//   --saver                OLED-bescherming: zwart schermvullend venster met bewegend klokje (geen engine; stopt bij SIGTERM/SIGINT)
//   --add-to-steam         RavenGun als niet-Steam-game toevoegen (zonder vragen; weigert als Steam draait) en stoppen
//   --steam-account ID     bij --add-to-steam: deze Steam-gebruiker (map onder userdata/) in plaats van de laatst ingelogde
//   --list-steam-shortcuts de niet-Steam-games van de laatst ingelogde gebruiker tonen (alleen lezen) en stoppen
//   --setup-wizard         de eerste-gebruik-wizard openen
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
#include <QSocketNotifier>
#include <QTimer>
#include <csignal>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/AppCore.h"
#include "core/AppPaths.h"
#include "core/SetupChecks.h"
#include "core/SteamShortcuts.h"
#include "widgets/BigPictureWindow.h"
#include "widgets/ControllersWindow.h"
#include "widgets/MainWindow.h"
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
    const QCommandLineOption addToSteam(QStringLiteral("add-to-steam"), QStringLiteral("Add RavenGun to the Steam library (Steam must be closed) and quit."));
    const QCommandLineOption steamAccount(QStringLiteral("steam-account"), QStringLiteral("Steam user (folder under userdata/) for --add-to-steam."),
                                          QStringLiteral("ID"));
    const QCommandLineOption listSteam(QStringLiteral("list-steam-shortcuts"), QStringLiteral("Show the non-Steam games of the Steam user and quit."));
    const QCommandLineOption printPaths(QStringLiteral("print-paths"), QStringLiteral("Show where RavenGun finds its scripts, data and engine, and quit."));
    const QCommandLineOption wizard(QStringLiteral("setup-wizard"), QStringLiteral("Open the setup wizard."));
    const QCommandLineOption shotDir(QStringLiteral("screenshot-dir"), QStringLiteral("Save screenshots of all windows to DIR and quit."),
                                     QStringLiteral("DIR"));
    const QCommandLineOption shotGame(QStringLiteral("screenshot-game"), QStringLiteral("Game id for the Properties screenshots."),
                                      QStringLiteral("ID"), QStringLiteral("tp:Halo"));
    const QCommandLineOption socket(QStringLiteral("socket"), QStringLiteral("Engine socket path."), QStringLiteral("PATH"));
    const QCommandLineOption library(QStringLiteral("library"), QStringLiteral("Library file."), QStringLiteral("PATH"));
    const QCommandLineOption noScan(QStringLiteral("no-scan"), QStringLiteral("Do not scan the game folders at start."));
    const QCommandLineOption saver(QStringLiteral("saver"), QStringLiteral("OLED screen saver: black full-screen window with a dim moving clock."));
    p.addOptions({saver, fullscreen, addToSteam, steamAccount, listSteam, printPaths, wizard, shotDir, shotGame, socket, library, noScan});
    p.process(app);

    // Iconen uit het bureaubladthema (KDE: breeze) ook buiten een Plasma-sessie (bv. offscreen).
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QStringLiteral("hicolor"))
        if (QFileInfo(QStringLiteral("/usr/share/icons/breeze")).isDir()) QIcon::setThemeName(QStringLiteral("breeze"));
    QApplication::setWindowIcon(rg::ui::Resources::GetAppIcon());

    if (p.isSet(printPaths)) {
        std::printf("data-dir: %s\n", qPrintable(rg::AppPaths::dataRoot()));
        std::printf("bin-dir: %s\n", qPrintable(rg::AppPaths::binDir()));
        std::printf("engine: %s\n", qPrintable(rg::SetupChecks::engineBinary()));
        return 0;
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
