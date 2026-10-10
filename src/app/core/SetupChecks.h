// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Controles en installatiestappen voor de eerste-gebruik-wizard (A6), zonder widgets en zonder root.
//
// Rechten installeren gebeurt met één beheerdersprompt: pkexec met een vast programma en vaste argumenten
// (<repo>/scripts/install-permissions.sh --apply), nooit een shell-tekst en nooit invoer van de gebruiker. $RAVENGUN_PKEXEC
// vervangt pkexec (tests en schermafdrukken: een nep-pkexec). De controle zonder root gebruikt de --check van de scripts.
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace rg {

struct CommandLine {
    QString program;
    QStringList args;
    bool valid() const { return !program.isEmpty(); }
};

class SetupChecks {
public:
    // De engine: <repo>/build/ravengund, anders ravengund in PATH; leeg als hij ontbreekt.
    static QString engineBinary();
    // ~/.config/systemd/user/ravengund.service (XDG_CONFIG_HOME wordt gevolgd).
    static QString serviceFile();
    static bool serviceInstalled() { return !serviceFile().isEmpty() && QFileInfoExists(serviceFile()); }
    // Camera: "PlayStation Eye (/dev/video0)" als een PS3 Eye (1415:2000) er is, anders een andere camera of leeg.
    struct Camera {
        bool found = false;
        bool ps3eye = false;
        QString device, name;
    };
    static Camera camera(const QString& sysDir = QStringLiteral("/sys/class/video4linux"));
    // Zit de app in Game Mode (gamescope)? Dan geen wizard.
    static bool isGameMode();

    // pkexec <repo>/scripts/install-permissions.sh --apply (leeg als de repo of het script ontbreekt).
    static CommandLine permissionsInstall();
    // <repo>/scripts/install-permissions.sh --check (zonder root).
    static CommandLine permissionsCheck();
    // <repo>/scripts/install-service.sh --apply (zonder root: gebruikersservice).
    static CommandLine serviceInstall();

    // Voert de controle van de rechten uit; cb(ok, uitvoer). Uitvoer is de tekst van de scripts (Nederlands, voor details).
    static void checkPermissions(QObject* context, std::function<void(bool ok, const QString& output)> cb);

private:
    static bool QFileInfoExists(const QString& path);
};

}  // namespace rg
