// SPDX-License-Identifier: GPL-3.0-or-later
// RavenGun: de app (Qt 6 Widgets). Opties:
//   --screenshot-dir MAP   maak schermafdrukken van alle vensters en stop (ontwikkelaars; zie widgets/Screenshots.h)
//   --screenshot-game ID   game voor Properties in de schermafdrukken (standaard tp:Halo)
//   --socket PAD           andere engine-socket (standaard $XDG_RUNTIME_DIR/ravengun/ravengund.sock)
//   --library PAD          ander bibliotheekbestand (standaard ~/.config/ravengun/library.json)
//   --no-scan              niet scannen bij het starten
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>

#include "core/AppCore.h"
#include "widgets/ControllersWindow.h"
#include "widgets/MainWindow.h"
#include "widgets/Resources.h"
#include "widgets/Screenshots.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RavenGun"));
    QApplication::setApplicationDisplayName(QStringLiteral("RavenGun"));
    QApplication::setDesktopFileName(QStringLiteral("ravengun-qt"));
    QApplication::setApplicationVersion(QStringLiteral(RAVENGUN_APP_VERSION));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("RavenGun: PS Move lightgun games"));
    p.addHelpOption();
    p.addVersionOption();
    const QCommandLineOption shotDir(QStringLiteral("screenshot-dir"), QStringLiteral("Save screenshots of all windows to DIR and quit."),
                                     QStringLiteral("DIR"));
    const QCommandLineOption shotGame(QStringLiteral("screenshot-game"), QStringLiteral("Game id for the Properties screenshots."),
                                      QStringLiteral("ID"), QStringLiteral("tp:Halo"));
    const QCommandLineOption socket(QStringLiteral("socket"), QStringLiteral("Engine socket path."), QStringLiteral("PATH"));
    const QCommandLineOption library(QStringLiteral("library"), QStringLiteral("Library file."), QStringLiteral("PATH"));
    const QCommandLineOption noScan(QStringLiteral("no-scan"), QStringLiteral("Do not scan the game folders at start."));
    p.addOptions({shotDir, shotGame, socket, library, noScan});
    p.process(app);

    // Iconen uit het bureaubladthema (KDE: breeze) ook buiten een Plasma-sessie (bv. offscreen).
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QStringLiteral("hicolor"))
        if (QFileInfo(QStringLiteral("/usr/share/icons/breeze")).isDir()) QIcon::setThemeName(QStringLiteral("breeze"));
    QApplication::setWindowIcon(rg::ui::Resources::GetAppIcon());

    rg::AppCore core(nullptr, p.value(socket), p.value(library));
    rg::ui::MainWindow window(&core);
    QObject::connect(core.engine(), &rg::EngineClient::connectedChanged, &core, [&core](bool c) {
        if (c) rg::ui::ControllersWindow::ReapplyMouseAssignments(&core);
    });
    core.start(!p.isSet(noScan));

    if (p.isSet(shotDir)) {
        rg::ui::RunScreenshots(&window, p.value(shotDir), p.value(shotGame));
    } else {
        window.show();
    }
    return app.exec();
}
