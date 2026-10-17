// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Controles en installatiestappen voor de eerste-gebruik-wizard (A6), zonder widgets en zonder root.
//
// Rechten installeren gebeurt met één beheerdersprompt: pkexec op het eigen hulpprogramma ravengun-setup-helper (src/syshelper)
// met een vast argument (--apply, --rollback), nooit een shell-tekst en nooit invoer van de gebruiker. $RAVENGUN_PKEXEC
// vervangt pkexec (tests en schermafdrukken: een nep-pkexec). De controle zonder root is de --check van hetzelfde programma.
// Geen enkel script: installeren (core/Install.h) en rechten zijn C++.
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

    // Het hulpprogramma dat bij de app hoort: <bin>/ravengun-setup-helper, bij ontwikkeling <repo>/build/ravengun-setup-helper
    // ($RAVENGUN_SETUP_HELPER gaat voor). Leeg als het ontbreekt.
    static QString setupHelperBinary();
    // De kopie die --apply in /opt/ravengun/bin zette (root:root, door de gebruiker niet te wijzigen); leeg als die er niet is of niet
    // van root is. optDir is alleen voor tests.
    static QString installedSetupHelper(const QString& optDir = QStringLiteral("/opt/ravengun"));
    // Zijn er systeemonderdelen van RavenGun (udev-regels of de Bluetooth-hulp)? Voor de uninstall.
    static bool systemPartsInstalled(const QString& etcRulesDir = QStringLiteral("/etc/udev/rules.d"), const QString& optDir = QStringLiteral("/opt/ravengun"));

    // pkexec <hulpprogramma> --apply (installeren of bijwerken; altijd het hulpprogramma van deze versie).
    static CommandLine permissionsInstall();
    // pkexec <hulpprogramma> --rollback (de root-kopie in /opt als die er is, anders die bij de app).
    static CommandLine permissionsRemove();
    // <hulpprogramma> --check (zonder root).
    static CommandLine permissionsCheck();

    // Voert de controle van de rechten uit; cb(ok, uitvoer). Uitvoer is de tekst van het hulpprogramma (Engels, voor details).
    static void checkPermissions(QObject* context, std::function<void(bool ok, const QString& output)> cb);

private:
    static bool QFileInfoExists(const QString& path);
};

}  // namespace rg
