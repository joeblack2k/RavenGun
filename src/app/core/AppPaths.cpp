// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace rg {

namespace {
QString envDir(const char* var, const QString& fallbackRel) {
    const QString v = qEnvironmentVariable(var);
    if (!v.isEmpty() && QDir::isAbsolutePath(v)) return v;
    return QDir::homePath() + QLatin1Char('/') + fallbackRel;
}
}  // namespace

QString AppPaths::homeDir() { return QDir::homePath(); }

QString AppPaths::configDir() { return envDir("XDG_CONFIG_HOME", QStringLiteral(".config")) + QStringLiteral("/ravengun"); }
QString AppPaths::libraryFile() { return configDir() + QStringLiteral("/library.json"); }
QString AppPaths::engineConfigFile() { return configDir() + QStringLiteral("/config.json"); }
QString AppPaths::appSettingsFile() { return configDir() + QStringLiteral("/ravengun-app.conf"); }
QString AppPaths::stateDir() { return envDir("XDG_STATE_HOME", QStringLiteral(".local/state")) + QStringLiteral("/ravengun"); }
QString AppPaths::dataDir() { return envDir("XDG_DATA_HOME", QStringLiteral(".local/share")) + QStringLiteral("/ravengun"); }
QString AppPaths::defaultTpDir() { return dataDir() + QStringLiteral("/runtime/TeknoParrotUi-2.0.0.130-linux-x64"); }
QString AppPaths::viewsDir() { return dataDir() + QStringLiteral("/runtime/views"); }

bool AppPaths::looksLikeDataRoot(const QString& d) {
    return !d.isEmpty() && QDir::isAbsolutePath(d) && QFileInfo::exists(d + QStringLiteral("/scripts/play-halo.sh")) &&
           QFileInfo::exists(d + QStringLiteral("/packaging"));
}

QStringList AppPaths::dataRootCandidates(const QString& dataDirEnv, const QString& rootEnv, const QString& appDir, const QString& home) {
    // Dezelfde volgorde als de engine (src/ravengun/paths.cpp): RAVENGUN_DATA_DIR, <prefix>/share/ravengun, de repo.
    QStringList c;
    if (!dataDirEnv.isEmpty()) c << dataDirEnv;
    // Pure padbewerking (de mappen hoeven niet te bestaan); "/" heeft geen bovenliggende map.
    auto parentOf = [](const QString& d) {
        const QString p = QDir::cleanPath(d + QStringLiteral("/.."));
        return (p == d || p == QStringLiteral("/") || p == QStringLiteral(".")) ? QString() : p;
    };
    const QString prefix = appDir.isEmpty() ? QString() : parentOf(QDir::cleanPath(appDir));
    if (!prefix.isEmpty()) c << prefix + QStringLiteral("/share/ravengun");
    if (!rootEnv.isEmpty()) c << rootEnv;
    if (!prefix.isEmpty()) {
        c << prefix;  // build/ of build-app/ -> de repo
        const QString up = parentOf(prefix);
        if (!up.isEmpty()) c << up;
    }
    if (!home.isEmpty()) c << home + QStringLiteral("/projects/ravengun");
    return c;
}

QString AppPaths::findDataRoot(const QString& dataDirEnv, const QString& rootEnv, const QString& appDir, const QString& home) {
    for (const QString& c : dataRootCandidates(dataDirEnv, rootEnv, appDir, home))
        if (looksLikeDataRoot(c)) return QDir(c).absolutePath();
    return {};
}

QString AppPaths::dataRoot() {
    const QString appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
    return findDataRoot(qEnvironmentVariable("RAVENGUN_DATA_DIR"), qEnvironmentVariable("RAVENGUN_ROOT"), appDir, QDir::homePath());
}

QString AppPaths::binDir() {
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
}

QString AppPaths::socketPath() {
    const QString env = qEnvironmentVariable("RAVENGUN_SOCKET");
    if (!env.isEmpty()) return env;
    const QString rt = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (rt.isEmpty() || !QDir::isAbsolutePath(rt)) return {};
    return rt + QStringLiteral("/ravengun/ravengund.sock");
}

QString AppPaths::packagingFile(const QString& name) {
    const QString root = dataRoot();
    if (!root.isEmpty()) {
        const QString p = root + QStringLiteral("/packaging/") + name;
        if (QFileInfo::exists(p)) return p;
    }
    const QString res = QStringLiteral(":/packaging/") + name;
    if (QFileInfo::exists(res)) return res;
    return {};
}

QString AppPaths::expandHome(const QString& path) {
    if (path == QStringLiteral("~")) return QDir::homePath();
    if (path.startsWith(QStringLiteral("~/"))) return QDir::homePath() + path.mid(1);
    return path;
}

}  // namespace rg
