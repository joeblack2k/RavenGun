// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Install.h"

#include <gnu/libc-version.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <filesystem>
#include <memory>

namespace rg {

namespace fs = std::filesystem;

namespace {

QString absEnvOr(const QString& value, const QString& fallback) {
    return (!value.isEmpty() && QDir::isAbsolutePath(value)) ? QDir::cleanPath(value) : fallback;
}

QString readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool writeFileAtomic(const QString& path, const QByteArray& data, QFileDevice::Permissions perms, QString* err) {
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = path + QStringLiteral(": ") + f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (err) *err = path + QStringLiteral(": ") + f.errorString();
        return false;
    }
    QFile::setPermissions(path, perms);
    return true;
}

constexpr QFileDevice::Permissions kPerm644 = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther;
constexpr QFileDevice::Permissions kPerm755 = kPerm644 | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther;

OpResult fail(const QString& why) {
    OpResult r;
    r.error = why;
    return r;
}
OpResult success(const QString& prefix = QString()) {
    OpResult r;
    r.ok = true;
    r.prefix = prefix;
    return r;
}

void say(const LogFn& log, const QString& s) {
    if (log) log(s);
}

QString systemctlProgram() {
    const QString e = qEnvironmentVariable("RAVENGUN_SYSTEMCTL");
    return e.isEmpty() ? QStringLiteral("systemctl") : e;
}

}  // namespace

// --- InstallLayout -------------------------------------------------------------------------------------------------------

QString Install::desktopDirFromUserDirs(const QString& text, const QString& home) {
    static const QRegularExpression re(QStringLiteral("^\\s*XDG_DESKTOP_DIR\\s*=\\s*\"([^\"]*)\"\\s*$"), QRegularExpression::MultilineOption);
    const auto m = re.match(text);
    if (!m.hasMatch()) return {};
    QString d = m.captured(1);
    d.replace(QStringLiteral("$HOME"), home);
    return QDir::isAbsolutePath(d) ? QDir::cleanPath(d) : QString();
}

InstallLayout InstallLayout::forEnvironment(const QString& homeIn, const QString& xdgConfig, const QString& xdgData, const QString& xdgState,
                                            const QString& prefixOverride) {
    InstallLayout l;
    l.home = QDir::cleanPath(homeIn);
    const QString cfg = absEnvOr(xdgConfig, l.home + QStringLiteral("/.config"));
    const QString data = absEnvOr(xdgData, l.home + QStringLiteral("/.local/share"));
    const QString state = absEnvOr(xdgState, l.home + QStringLiteral("/.local/state"));
    l.prefix = prefixOverride.isEmpty() ? l.home + QStringLiteral("/.local/opt/ravengun") : QDir::cleanPath(prefixOverride);
    l.unitDir = cfg + QStringLiteral("/systemd/user");
    l.applicationsDir = data + QStringLiteral("/applications");
    l.localBinDir = l.home + QStringLiteral("/.local/bin");
    l.configDir = cfg + QStringLiteral("/ravengun");
    l.stateDir = state + QStringLiteral("/ravengun");
    l.userDataDir = data + QStringLiteral("/ravengun");
    QString desk = Install::desktopDirFromUserDirs(readText(cfg + QStringLiteral("/user-dirs.dirs")), l.home);
    if (desk.isEmpty()) desk = l.home + QStringLiteral("/Desktop");
    if (desk != l.home && QFileInfo(desk).isDir()) l.desktopDir = desk;
    return l;
}

InstallLayout InstallLayout::current() {
    return forEnvironment(QDir::homePath(), qEnvironmentVariable("XDG_CONFIG_HOME"), qEnvironmentVariable("XDG_DATA_HOME"),
                          qEnvironmentVariable("XDG_STATE_HOME"), qEnvironmentVariable("RAVENGUN_PREFIX"));
}

// --- Zuivere logica ------------------------------------------------------------------------------------------------------

bool Install::unitPathSafe(const QString& path) {
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) return false;
    for (const QChar c : path)
        if (c.isSpace() || c == QLatin1Char('%') || c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('\\') || c.unicode() < 32)
            return false;
    return true;
}

bool Install::prefixIsSafe(const QString& prefixIn, const QString& homeIn, QString* why) {
    auto no = [why](const QString& w) {
        if (why) *why = w;
        return false;
    };
    if (prefixIn.isEmpty() || !QDir::isAbsolutePath(prefixIn)) return no(QStringLiteral("The install folder must be an absolute path: %1").arg(prefixIn));
    if (prefixIn.contains(QStringLiteral("/../")) || prefixIn.endsWith(QStringLiteral("/..")))
        return no(QStringLiteral("The install folder must not contain \"..\": %1").arg(prefixIn));
    const QString prefix = QDir::cleanPath(prefixIn), home = QDir::cleanPath(homeIn);
    if (!unitPathSafe(prefix))
        return no(QStringLiteral("The install folder must not contain spaces or special characters: %1").arg(prefix));
    if (prefix == QStringLiteral("/") || prefix == QStringLiteral("/home") || prefix == QStringLiteral("/opt") || prefix == home ||
        home.startsWith(prefix + QLatin1Char('/')))
        return no(QStringLiteral("Unsafe install folder: %1").arg(prefix));
    for (const char* sys : {"/usr", "/etc", "/bin", "/sbin", "/lib", "/lib64", "/var", "/boot", "/proc", "/sys", "/dev", "/run"}) {
        const QString s = QString::fromLatin1(sys);
        if (prefix == s || prefix.startsWith(s + QLatin1Char('/'))) return no(QStringLiteral("Unsafe install folder (system location): %1").arg(prefix));
    }
    return true;
}

bool Install::isRavenGunTree(const QString& prefix) {
    return QFileInfo(prefix + QStringLiteral("/bin/ravengund")).isFile() && QFileInfo(prefix + QStringLiteral("/bin/ravengunctl")).isFile() &&
           QFileInfo(prefix + QStringLiteral("/share/ravengun/packaging/tp-shm-profiles.json")).isFile();
}

QString Install::runningTreePrefix(const QString& binDir) {
    if (binDir.isEmpty()) return {};
    const QString bin = QFileInfo(binDir).canonicalFilePath();
    if (bin.isEmpty() || !bin.endsWith(QStringLiteral("/bin"))) return {};
    const QString prefix = bin.left(bin.size() - 4);
    return isRavenGunTree(prefix) ? prefix : QString();
}

bool Install::runsFromPrefix(const QString& binDir, const QString& prefix) {
    const QString a = QFileInfo(binDir).canonicalFilePath(), b = QFileInfo(prefix + QStringLiteral("/bin")).canonicalFilePath();
    return !a.isEmpty() && a == b;
}

int Install::compareVersions(const QString& a, const QString& b) {
    const QStringList pa = a.split(QLatin1Char('.')), pb = b.split(QLatin1Char('.'));
    for (qsizetype i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa[i].toInt() : 0, y = i < pb.size() ? pb[i].toInt() : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

QString Install::runtimeGlibc() { return QString::fromLatin1(::gnu_get_libc_version()); }

QString Install::glibcProblem(const QString& requiresText, const QString& have) {
    static const QRegularExpression re(QStringLiteral("^glibc[ =>]*([0-9][0-9.]*)"), QRegularExpression::MultilineOption);
    const auto m = re.match(requiresText);
    if (!m.hasMatch() || have.isEmpty()) return {};
    const QString need = m.captured(1);
    if (compareVersions(have, need) >= 0) return {};
    return QStringLiteral("This build of RavenGun needs glibc %1 or newer, but this system has %2. Update SteamOS, or build RavenGun yourself (docs/build.md).")
        .arg(need, have);
}

QStringList Install::missingLibraries(const QString& binary, const QString& ldSoIn) {
    const QString ldSo = ldSoIn.isEmpty() ? QStringLiteral("/lib64/ld-linux-x86-64.so.2") : ldSoIn;
    if (!QFileInfo(binary).isFile() || !QFileInfo(ldSo).isExecutable()) return {};
    QString out;
    runProgram(ldSo, {QStringLiteral("--list"), binary}, &out, 20000);
    QStringList missing;
    for (const QString& line : out.split(QLatin1Char('\n'))) {
        if (!line.contains(QStringLiteral("not found"))) continue;
        missing << line.section(QStringLiteral("=>"), 0, 0).trimmed();
    }
    return missing;
}

QString Install::renderTemplate(const QString& text, const QString& binDir, const QString& dataDir) {
    QString t = text;
    t.replace(QStringLiteral("@BINDIR@"), binDir);
    t.replace(QStringLiteral("@DATADIR@"), dataDir);
    return t;
}

namespace {
QString renderFromTemplate(const QString& dataDir, const QString& relTemplate, const QString& binDir, QString* err) {
    if (!Install::unitPathSafe(dataDir) || !Install::unitPathSafe(binDir)) {
        if (err) *err = QStringLiteral("The path contains spaces or special characters, which the service does not support: %1").arg(Install::unitPathSafe(binDir) ? dataDir : binDir);
        return {};
    }
    const QString tpl = dataDir + QLatin1Char('/') + relTemplate;
    if (!QFileInfo(tpl).isFile()) {
        if (err) *err = QStringLiteral("Template missing: %1").arg(tpl);
        return {};
    }
    return Install::renderTemplate(readText(tpl), binDir, dataDir);
}
}  // namespace

QString Install::engineUnitText(const QString& dataDir, const QString& binDir, QString* err) {
    return renderFromTemplate(dataDir, QStringLiteral("packaging/systemd/ravengund.service.in"), binDir, err);
}
QString Install::oledUnitText(const QString& dataDir, const QString& binDir, QString* err) {
    return renderFromTemplate(dataDir, QStringLiteral("packaging/systemd/ravengun-oled-saver.service.in"), binDir, err);
}
QString Install::desktopEntryText(const QString& dataDir, const QString& binDir, QString* err) {
    return renderFromTemplate(dataDir, QStringLiteral("packaging/desktop/ravengun-qt.desktop.in"), binDir, err);
}

QString Install::engineBinDirFor(const QString& dataDir) {
    const QString env = qEnvironmentVariable("RAVENGUN_BIN_DIR");
    if (!env.isEmpty()) return env;
    // <prefix>/share/ravengun -> <prefix>/bin; de repo -> <repo>/build (zoals scripts/rg-paths.sh)
    const QDir d(dataDir);
    if (d.dirName() == QStringLiteral("ravengun")) {
        QDir share(d);
        if (share.cdUp() && share.dirName() == QStringLiteral("share")) {
            QDir prefix(share);
            if (prefix.cdUp() && QFileInfo(prefix.filePath(QStringLiteral("bin"))).isDir()) return prefix.filePath(QStringLiteral("bin"));
        }
    }
    return QDir::cleanPath(dataDir + QStringLiteral("/build"));
}

// --- Uitvoering ----------------------------------------------------------------------------------------------------------

int Install::runProgram(const QString& program, const QStringList& args, QString* output, int timeoutMs) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program == QStringLiteral("systemctl") ? systemctlProgram() : program, args);
    if (!p.waitForStarted(5000)) {
        if (output) *output = QStringLiteral("could not start %1").arg(program);
        return -2;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        if (output) *output = QString::fromUtf8(p.readAll());
        return -1;
    }
    if (output) *output = QString::fromUtf8(p.readAll());
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

int Install::systemctlUser(const QStringList& args, QString* output) {
    return runProgram(QStringLiteral("systemctl"), QStringList{QStringLiteral("--user")} + args, output, 60000);
}

bool Install::copyTree(const QString& from, const QString& to, QString* err) {
    std::error_code ec;
    const fs::path src = from.toStdString(), dst = to.toStdString();
    const fs::file_status st = fs::symlink_status(src, ec);
    if (ec) {
        if (err) *err = QStringLiteral("%1: %2").arg(from, QString::fromStdString(ec.message()));
        return false;
    }
    if (fs::is_symlink(st)) {
        fs::remove(dst, ec);
        fs::copy_symlink(src, dst, ec);
    } else if (fs::is_directory(st)) {
        fs::create_directories(dst, ec);
        if (!ec) {
            for (const auto& e : fs::directory_iterator(src, ec)) {
                if (ec) break;
                if (!copyTree(QString::fromStdString(e.path().string()), QString::fromStdString((dst / e.path().filename()).string()), err)) return false;
            }
        }
        if (!ec) fs::permissions(dst, fs::status(src).permissions() | fs::perms::owner_write | fs::perms::owner_read | fs::perms::owner_exec, ec);
    } else if (fs::is_regular_file(st)) {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        // De wijzigtijd meenemen (zoals cp -a): de engine vergelijkt een gebouwde plugin met zijn bron in share/ (verouderd = nieuwere
        // bron). Zonder dit kreeg alles de installatietijd en leek de bron (later gekopieerd) nieuwer dan de DLL (gemeten 2026-10-17:
        // Tomb Raider en Rabbids weigerden na ravengun --install met "plugin verouderd").
        if (!ec) fs::last_write_time(dst, fs::last_write_time(src, ec), ec);
    } else {
        return true;  // sockets, apparaten e.d. horen niet in een installatie
    }
    if (ec) {
        if (err) *err = QStringLiteral("%1: %2").arg(to, QString::fromStdString(ec.message()));
        return false;
    }
    return true;
}

bool Install::gameRunning(const QString& procDir) {
    if (systemctlUser({QStringLiteral("is-active"), QStringLiteral("--quiet"), QStringLiteral("ravengun-tpui.service")}) == 0) return true;
    static const QStringList names = {QStringLiteral("BudgieLoader_x6"), QStringLiteral("BudgieLoader.ex"), QStringLiteral("Game.exe"),
                                      QStringLiteral("Shell.exe"), QStringLiteral("Transformers2.ex"), QStringLiteral("wineserver"),
                                      QStringLiteral("gamescope")};
    for (const QString& pid : QDir(procDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool num = false;
        pid.toInt(&num);
        if (!num) continue;
        if (names.contains(readText(procDir + QLatin1Char('/') + pid + QStringLiteral("/comm")).trimmed())) return true;
    }
    return false;
}

bool Install::restartEngineIfIdle(const QString& procDir, const LogFn& log) {
    // Een herstart tijdens een game laat de game zijn virtuele invoerapparaten verliezen.
    if (gameRunning(procDir)) {
        say(log, QStringLiteral("A game is running; the engine was NOT restarted. Restart it later: Settings > Engine > Restart."));
        return false;
    }
    QString out;
    if (systemctlUser({QStringLiteral("restart"), QStringLiteral("ravengund.service")}, &out) != 0) {
        say(log, QStringLiteral("The engine could not be restarted: %1").arg(out.trimmed()));
        return false;
    }
    say(log, QStringLiteral("Engine restarted."));
    return true;
}

OpResult Install::writeEngineUnit(const InstallLayout& layout, const QString& dataDir, const QString& binDir, const LogFn& log) {
    QString err;
    const QString text = engineUnitText(dataDir, binDir, &err);
    if (text.isEmpty()) return fail(err);
    if (!QFileInfo(binDir + QStringLiteral("/ravengund")).isExecutable())
        return fail(QStringLiteral("ravengund not found in %1 (build it first, or install RavenGun).").arg(binDir));
    QDir().mkpath(layout.stateDir + QStringLiteral("/logs"));
    if (!writeFileAtomic(layout.engineUnitFile(), text.toUtf8(), kPerm644, &err)) return fail(err);
    say(log, QStringLiteral("Wrote %1").arg(layout.engineUnitFile()));
    return success();
}

OpResult Install::writeOledUnit(const InstallLayout& layout, const QString& dataDir, const QString& binDir, const LogFn& log) {
    QString err;
    const QString text = oledUnitText(dataDir, binDir, &err);
    if (text.isEmpty()) return fail(err);
    if (!writeFileAtomic(layout.oledUnitFile(), text.toUtf8(), kPerm644, &err)) return fail(err);
    say(log, QStringLiteral("Wrote %1").arg(layout.oledUnitFile()));
    return success();
}

OpResult Install::writeShortcuts(const InstallLayout& layout, const InstallOptions& opt, const QString& dataDir, const QString& binDir,
                                 const LogFn& log) {
    QString err;
    const QString text = desktopEntryText(dataDir, binDir, &err);
    if (text.isEmpty()) return fail(err);
    if (!writeFileAtomic(layout.desktopFile(), text.toUtf8(), kPerm755, &err)) return fail(err);
    say(log, QStringLiteral("Wrote the menu entry %1").arg(layout.desktopFile()));
    QString udd = opt.updateDesktopDatabase;
    if (udd.isEmpty() && QFileInfo(QStringLiteral("/usr/bin/update-desktop-database")).isExecutable()) udd = QStringLiteral("/usr/bin/update-desktop-database");
    if (!udd.isEmpty()) runProgram(udd, {layout.applicationsDir}, nullptr, 20000);
    if (!layout.desktopDir.isEmpty()) {
        const QString d = layout.desktopDir + QStringLiteral("/ravengun-qt.desktop");
        if (writeFileAtomic(d, text.toUtf8(), kPerm755, &err)) say(log, QStringLiteral("Wrote the desktop icon %1").arg(d));
    }
    // Handige koppelingen voor de terminal.
    QDir().mkpath(layout.localBinDir);
    for (const QString& b : {QStringLiteral("ravengun"), QStringLiteral("ravengunctl")}) {
        if (!QFileInfo(binDir + QLatin1Char('/') + b).isExecutable()) continue;
        const QString link = layout.localBinDir + QLatin1Char('/') + b;
        QFile::remove(link);
        QFile::link(binDir + QLatin1Char('/') + b, link);
    }
    return success();
}

OpResult Install::enableEngine(bool start, const LogFn& log) {
    QString out;
    if (systemctlUser({QStringLiteral("daemon-reload")}, &out) != 0) return fail(QStringLiteral("systemctl --user daemon-reload failed: %1").arg(out.trimmed()));
    QStringList args = {QStringLiteral("enable")};
    if (start) args << QStringLiteral("--now");
    args << QStringLiteral("ravengund.service");
    if (systemctlUser(args, &out) != 0) return fail(QStringLiteral("systemctl --user %1 failed: %2").arg(args.join(QLatin1Char(' ')), out.trimmed()));
    say(log, start ? QStringLiteral("The engine is enabled (starts at login) and started.") : QStringLiteral("The engine is enabled (starts at login)."));
    return success();
}

OpResult Install::installTree(const QString& sourcePrefixIn, const InstallLayout& layout, const InstallOptions& opt, const LogFn& log) {
    const QString prefix = QDir::cleanPath(layout.prefix);
    QString why;
    if (!prefixIsSafe(prefix, layout.home, &why)) return fail(why);
    if (::geteuid() == 0) return fail(QStringLiteral("Run RavenGun as your normal user, not as root: everything is installed under your home folder."));
    const QString src = QFileInfo(sourcePrefixIn).canonicalFilePath();
    if (src.isEmpty() || !isRavenGunTree(src))
        return fail(QStringLiteral("This is not a complete RavenGun folder (bin/ravengund, bin/ravengunctl and share/ravengun are needed): %1").arg(sourcePrefixIn));
    // Een map die er al is en niet van ons is, blijft met rust.
    if (QFileInfo::exists(prefix)) {
        if (!QFileInfo(prefix).isDir()) return fail(QStringLiteral("%1 exists and is not a folder").arg(prefix));
        if (!QDir(prefix).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden) && !isRavenGunTree(prefix))
            return fail(QStringLiteral("%1 already exists and is not a RavenGun installation; pick another folder.").arg(prefix));
    }
    const QString canonPrefix = QFileInfo(prefix).canonicalFilePath();
    const bool inPlace = !canonPrefix.isEmpty() && canonPrefix == src;

    if (inPlace) {
        say(log, QStringLiteral("RavenGun is already in %1; only refreshing the services and shortcuts.").arg(prefix));
    } else {
        QDir().mkpath(QFileInfo(prefix).path());
        const QString stage = prefix + QStringLiteral(".new.") + QString::number(::getpid());
        QDir(stage).removeRecursively();
        auto cleanup = [&stage] { QDir(stage).removeRecursively(); };
        say(log, QStringLiteral("Copying %1 to %2").arg(src, prefix));
        QDir().mkpath(stage);
        for (const QString& d : {QStringLiteral("bin"), QStringLiteral("lib"), QStringLiteral("share"), QStringLiteral("patches")}) {
            if (!QFileInfo(src + QLatin1Char('/') + d).isDir()) continue;
            QString err;
            if (!copyTree(src + QLatin1Char('/') + d, stage + QLatin1Char('/') + d, &err)) {
                cleanup();
                return fail(QStringLiteral("Copying failed: %1").arg(err));
            }
        }
        // De profielen per game horen bij de installatie; een boom zonder profielen is geen
        // complete RavenGun. Ze staan in patches/ (een release- of installatiemap, met in
        // de datamap een relatieve koppeling erheen) of direct in share/ravengun/patches
        // (de portabele map, een cmake-installatie in een systeemprefix), zoals
        // scripts/install-user.sh ook accepteert. isDir() volgt een koppeling, een koppeling
        // zonder doel telt dus niet.
        if (!QFileInfo(stage + QStringLiteral("/patches")).isDir() && !QFileInfo(stage + QStringLiteral("/share/ravengun/patches")).isDir()) {
            cleanup();
            return fail(QStringLiteral("The per-game profiles (patches/ or share/ravengun/patches) are missing in %1; the tree to install is not complete.").arg(src));
        }
        if (!isRavenGunTree(stage)) {
            cleanup();
            return fail(QStringLiteral("ravengund, ravengunctl or the data folder (share/ravengun, with packaging/) is missing in the tree to install."));
        }
        // De eisen van deze build: de glibc van de container waarin gebouwd is, en de systeembibliotheken (Qt 6.11 voor de app).
        const QString problem = glibcProblem(readText(stage + QStringLiteral("/share/ravengun/REQUIRES")), runtimeGlibc());
        if (!problem.isEmpty()) {
            cleanup();
            return fail(problem);
        }
        QStringList missing;
        for (const QString& b : {QStringLiteral("ravengun"), QStringLiteral("ravengund"), QStringLiteral("ravengunctl")}) {
            const QStringList m = missingLibraries(stage + QStringLiteral("/bin/") + b);
            if (!m.isEmpty()) missing << QStringLiteral("%1: %2").arg(b, m.join(QStringLiteral(", ")));
        }
        if (!missing.isEmpty()) {
            cleanup();
            return fail(QStringLiteral("Missing or too old system libraries: %1. (The app needs Qt 6.11 or newer: SteamOS 3.x has it; on Arch install qt6-base.)")
                            .arg(missing.join(QStringLiteral("; "))));
        }
        // Verwisselen met de bestaande installatie (de gebruikersgegevens elders blijven ongemoeid).
        QString old;
        if (QFileInfo::exists(prefix)) {
            old = prefix + QStringLiteral(".old.") + QString::number(::getpid());
            QDir(old).removeRecursively();
            if (::rename(QFile::encodeName(prefix).constData(), QFile::encodeName(old).constData()) != 0) {
                cleanup();
                return fail(QStringLiteral("Could not move the old installation out of the way: %1").arg(prefix));
            }
        }
        if (::rename(QFile::encodeName(stage).constData(), QFile::encodeName(prefix).constData()) != 0) {
            if (!old.isEmpty()) ::rename(QFile::encodeName(old).constData(), QFile::encodeName(prefix).constData());
            cleanup();
            return fail(QStringLiteral("Could not put the new installation in place: %1").arg(prefix));
        }
        if (!old.isEmpty()) QDir(old).removeRecursively();
        say(log, QStringLiteral("Installed in %1").arg(prefix));
    }

    // Units en snelkoppelingen wijzen naar de geïnstalleerde kopie, nooit naar het archief of de repo.
    const QString data = layout.dataDir(), bin = layout.binDir();
    QDir().mkpath(layout.stateDir + QStringLiteral("/logs"));
    OpResult r = writeEngineUnit(layout, data, bin, log);
    if (!r.ok) return r;
    r = writeOledUnit(layout, data, bin, log);
    if (!r.ok) return r;
    if (opt.shortcuts) {
        r = writeShortcuts(layout, opt, data, bin, log);
        if (!r.ok) return r;
    }
    const bool wasActive = systemctlUser({QStringLiteral("is-active"), QStringLiteral("--quiet"), QStringLiteral("ravengund.service")}) == 0;
    // Een draaiende engine krijgt alleen een herstart als er geen game draait.
    r = enableEngine(opt.start && !wasActive, log);
    if (!r.ok) return r;
    if (wasActive && opt.start) restartEngineIfIdle(opt.procDir, log);
    // Een draaiende OLED-bescherming draait nog het oude programma (bij een update van het script naar ravengun --saver-loop
    // gemeten: de unit was herschreven, de lus bleef het script): herstarten. Aan/uit blijft een keuze in de app.
    if (opt.start && systemctlUser({QStringLiteral("is-active"), QStringLiteral("--quiet"), QStringLiteral("ravengun-oled-saver.service")}) == 0) {
        if (systemctlUser({QStringLiteral("restart"), QStringLiteral("ravengun-oled-saver.service")}) == 0) say(log, QStringLiteral("OLED protection restarted."));
        else say(log, QStringLiteral("The OLED protection could not be restarted; it uses the new program after the next login."));
    }
    return success(prefix);
}

OpResult Install::uninstall(const InstallLayout& layout, const UninstallOptions& opt, const LogFn& log) {
    if (::geteuid() == 0) return fail(QStringLiteral("Run RavenGun as your normal user, not as root."));
    const QString prefix = QDir::cleanPath(layout.prefix);
    QString why;
    if (!prefixIsSafe(prefix, layout.home, &why)) return fail(why);
    const bool installed = isRavenGunTree(prefix);
    if (!installed) say(log, QStringLiteral("No RavenGun installation found in %1 (the services and shortcuts are still cleaned up).").arg(prefix));

    say(log, QStringLiteral("Stopping and removing the user services"));
    for (const QString& u : {QStringLiteral("ravengund"), QStringLiteral("ravengun-oled-saver")}) {
        systemctlUser({QStringLiteral("disable"), QStringLiteral("--now"), u + QStringLiteral(".service")});
        QFile::remove(layout.unitDir + QLatin1Char('/') + u + QStringLiteral(".service"));
    }
    QDir(layout.unitDir + QStringLiteral("/ravengun-oled-saver.service.d")).removeRecursively();
    systemctlUser({QStringLiteral("daemon-reload")});
    systemctlUser({QStringLiteral("stop"), QStringLiteral("ravengun-oled-view.service")});

    say(log, QStringLiteral("Removing shortcuts"));
    for (const QString& f : {layout.desktopFile(), layout.desktopDir.isEmpty() ? QString() : layout.desktopDir + QStringLiteral("/ravengun-qt.desktop")}) {
        if (f.isEmpty() || !QFileInfo(f).isFile()) continue;
        // Alleen onze eigen bestanden: ze wijzen naar de installatiemap.
        if (readText(f).contains(QStringLiteral("\nExec=") + prefix + QStringLiteral("/bin/ravengun"))) QFile::remove(f);
    }
    QString udd = opt.updateDesktopDatabase;
    if (udd.isEmpty() && QFileInfo(QStringLiteral("/usr/bin/update-desktop-database")).isExecutable()) udd = QStringLiteral("/usr/bin/update-desktop-database");
    if (!udd.isEmpty() && QFileInfo(layout.applicationsDir).isDir()) runProgram(udd, {layout.applicationsDir}, nullptr, 20000);
    for (const QString& b : {QStringLiteral("ravengun"), QStringLiteral("ravengunctl")}) {
        const QString link = layout.localBinDir + QLatin1Char('/') + b;
        const QFileInfo fi(link);
        if (fi.isSymLink() && fi.symLinkTarget().startsWith(prefix + QStringLiteral("/bin/"))) QFile::remove(link);
    }

    // De systeemonderdelen (root) vóór het verwijderen van de installatiemap: het hulpprogramma staat daarin of in /opt.
    if (opt.systemPartsPresent && !opt.keepRoot) {
        say(log, QStringLiteral("Removing the system parts (device rules, Bluetooth helper); you will be asked for your password"));
        QString pk = opt.pkexec;
        if (pk.isEmpty()) pk = qEnvironmentVariable("RAVENGUN_PKEXEC");
        if (pk.isEmpty()) pk = QStringLiteral("pkexec");
        QString out;
        const int rc = opt.setupHelper.isEmpty() ? -3 : runProgram(pk, {opt.setupHelper, QStringLiteral("--rollback")}, &out, 300000);
        if (!out.trimmed().isEmpty()) say(log, out.trimmed());
        if (rc != 0) {
            say(log, QStringLiteral("The system parts could not be removed automatically. To remove them yourself, run in a terminal:\n"
                                    "  sudo rm -f /etc/udev/rules.d/70-ravengun.rules /etc/udev/rules.d/71-ravengun-uhid.rules "
                                    "/etc/udev/rules.d/72-ravengun-dongle.rules /etc/sudoers.d/ravengun-bt-helper\n"
                                    "  sudo rm -rf /opt/ravengun && sudo udevadm control --reload"));
        }
    } else if (opt.systemPartsPresent) {
        say(log, QStringLiteral("Keeping the system parts (device rules, Bluetooth helper) as requested."));
    }

    if (installed) {
        say(log, QStringLiteral("Removing %1").arg(prefix));
        if (!QDir(prefix).removeRecursively()) return fail(QStringLiteral("Could not remove %1 completely.").arg(prefix));
    }
    if (opt.removeConfig) {
        say(log, QStringLiteral("Removing your settings and logs"));
        QDir(layout.configDir).removeRecursively();
        QDir(layout.stateDir).removeRecursively();
    } else {
        say(log, QStringLiteral("Your settings and library are kept in %1 and %2.").arg(layout.configDir, layout.stateDir));
    }
    say(log, QStringLiteral("Not removed (remove by hand if you want): %1 (the TeknoParrot runtime, Wine and other data RavenGun downloaded or prepared; your game "
                            "folders are untouched) and the RavenGun entry in your Steam library.").arg(layout.userDataDir));
    return success();
}

// --- Op de achtergrond ---------------------------------------------------------------------------------------------------

void runAsync(QObject* context, const std::function<OpResult(const LogFn&)>& work, const LogFn& onLog, const std::function<void(const OpResult&)>& done) {
    QPointer<QObject> ctx(context);
    LogFn post = [ctx, onLog](const QString& s) {
        if (!ctx || !onLog) return;
        QMetaObject::invokeMethod(ctx.data(), [ctx, onLog, s] { if (ctx) onLog(s); }, Qt::QueuedConnection);
    };
    QThread* t = QThread::create([ctx, work, post, done] {
        const OpResult r = work(post);
        if (!ctx) return;
        QMetaObject::invokeMethod(ctx.data(), [ctx, done, r] { if (ctx && done) done(r); }, Qt::QueuedConnection);
    });
    QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

}  // namespace rg
