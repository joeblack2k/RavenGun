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

QString AppPaths::repoRoot() {
    auto looksLikeRepo = [](const QString& d) {
        return !d.isEmpty() && QFileInfo::exists(d + QStringLiteral("/scripts/play-halo.sh")) &&
               QFileInfo::exists(d + QStringLiteral("/packaging"));
    };
    const QString env = qEnvironmentVariable("RAVENGUN_ROOT");
    if (looksLikeRepo(env)) return QDir(env).absolutePath();
    if (QCoreApplication::instance()) {
        QDir d(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 3; ++i) {
            if (looksLikeRepo(d.absolutePath())) return d.absolutePath();
            if (!d.cdUp()) break;
        }
    }
    const QString def = QDir::homePath() + QStringLiteral("/projects/ravengun");
    return looksLikeRepo(def) ? def : QString();
}

QString AppPaths::socketPath() {
    const QString env = qEnvironmentVariable("RAVENGUN_SOCKET");
    if (!env.isEmpty()) return env;
    const QString rt = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (rt.isEmpty() || !QDir::isAbsolutePath(rt)) return {};
    return rt + QStringLiteral("/ravengun/ravengund.sock");
}

QString AppPaths::packagingFile(const QString& name) {
    const QString root = repoRoot();
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
