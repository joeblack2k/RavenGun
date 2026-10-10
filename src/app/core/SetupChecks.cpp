// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SetupChecks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "core/AppPaths.h"
#include "core/SystemControl.h"

namespace rg {

bool SetupChecks::QFileInfoExists(const QString& path) { return QFileInfo::exists(path); }

QString SetupChecks::engineBinary() {
    // Installatie: ravengund staat naast de app (<prefix>/bin). Ontwikkeling: <repo>/build/ravengund. Anders in PATH.
    const QString bin = AppPaths::binDir();
    if (!bin.isEmpty()) {
        const QString p = bin + QStringLiteral("/ravengund");
        if (QFileInfo(p).isExecutable()) return p;
    }
    const QString root = AppPaths::dataRoot();
    if (!root.isEmpty()) {
        const QString p = root + QStringLiteral("/build/ravengund");
        if (QFileInfo(p).isExecutable()) return p;
    }
    return QStandardPaths::findExecutable(QStringLiteral("ravengund"));
}

QString SetupChecks::serviceFile() {
    const QString x = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString base = (!x.isEmpty() && QDir::isAbsolutePath(x)) ? x : QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/systemd/user/ravengund.service");
}

SetupChecks::Camera SetupChecks::camera(const QString& sysDir) {
    Camera best;
    const QDir d(sysDir);
    for (const QString& v : d.entryList({QStringLiteral("video*")}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        auto read = [](const QString& p) {
            QFile f(p);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
        };
        Camera c;
        c.found = true;
        c.device = QStringLiteral("/dev/") + v;
        c.name = read(d.filePath(v) + QStringLiteral("/name"));
        // device -> USB-interface; idVendor/idProduct staan een map hoger.
        const QString usbIf = QFileInfo(d.filePath(v) + QStringLiteral("/device")).canonicalFilePath();
        const QString usbDev = QFileInfo(usbIf).path();
        c.ps3eye = read(usbDev + QStringLiteral("/idVendor")) == QStringLiteral("1415") &&
                   read(usbDev + QStringLiteral("/idProduct")) == QStringLiteral("2000");
        if (c.ps3eye) {
            c.name = QStringLiteral("PlayStation Eye");
            return c;
        }
        if (!best.found) best = c;
    }
    return best;
}

bool SetupChecks::isGameMode() {
    if (!qEnvironmentVariable("GAMESCOPE_WAYLAND_DISPLAY").isEmpty()) return true;
    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP").compare(QStringLiteral("gamescope"), Qt::CaseInsensitive) == 0) return true;
    return qEnvironmentVariableIntValue("SteamGamepadUI") == 1;
}

namespace {
QString script(const QString& name) {
    const QString root = AppPaths::dataRoot();
    if (root.isEmpty()) return {};
    const QString p = QDir(root).absoluteFilePath(QStringLiteral("scripts/") + name);
    return QFileInfo(p).isFile() ? QDir::cleanPath(p) : QString();
}
}  // namespace

CommandLine SetupChecks::permissionsInstall() {
    const QString s = script(QStringLiteral("install-permissions.sh"));
    if (s.isEmpty()) return {};
    QString pk = qEnvironmentVariable("RAVENGUN_PKEXEC");
    if (pk.isEmpty()) pk = QStringLiteral("pkexec");
    // pkexec vraagt een absoluut pad en geeft het programma een schone omgeving; het script bepaalt de gebruiker uit PKEXEC_UID.
    return {pk, {s, QStringLiteral("--apply")}};
}

CommandLine SetupChecks::permissionsCheck() {
    const QString s = script(QStringLiteral("install-permissions.sh"));
    if (s.isEmpty()) return {};
    return {QStringLiteral("/bin/bash"), {s, QStringLiteral("--check")}};
}

CommandLine SetupChecks::serviceInstall() {
    const QString s = script(QStringLiteral("install-service.sh"));
    if (s.isEmpty()) return {};
    return {QStringLiteral("/bin/bash"), {s, QStringLiteral("--apply")}};
}

void SetupChecks::checkPermissions(QObject* context, std::function<void(bool, const QString&)> cb) {
    const CommandLine c = permissionsCheck();
    if (!c.valid()) {
        cb(false, QStringLiteral("scripts/install-permissions.sh not found"));
        return;
    }
    SystemControl::run(c.program, c.args, context, [cb](int code, const QString& out) { cb(code == 0, out); }, 20000);
}

}  // namespace rg
