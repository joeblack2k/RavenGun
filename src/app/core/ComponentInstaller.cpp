// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ComponentInstaller.h"

#include <unistd.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QUrl>
#include <QSet>
#include <cstdio>
#include <cstring>

#include "core/ArchiveExtract.h"

namespace rg {
namespace {

QString safeName(const QString& id) {
    QString s = id;
    s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return s;
}

bool lexists(const QString& p) {
    const QFileInfo fi(p);
    return fi.exists() || fi.isSymLink();
}

bool isRealDir(const QString& p) {
    const QFileInfo fi(p);
    return fi.isDir() && !fi.isSymLink();
}

bool removePath(const QString& p) {
    if (isRealDir(p)) return QDir(p).removeRecursively();
    return QFile::remove(p);
}

QString stagingDir(const ComponentEnv& env, const QString& id) { return env.dataDir + QStringLiteral("/components/.staging/") + safeName(id); }

bool freshDir(const QString& d, QString* err) {
    if (lexists(d)) QDir(d).removeRecursively();
    if (!QDir().mkpath(d)) {
        if (err) *err = QStringLiteral("cannot create %1").arg(d);
        return false;
    }
    return true;
}

bool cancelled(const std::atomic_bool* c) { return c && c->load(); }

bool mergeInto(const QString& src, const QString& dst, bool overwrite, QString* err) {
    if (!QDir().mkpath(dst)) {
        if (err) *err = QStringLiteral("cannot create %1").arg(dst);
        return false;
    }
    const QDir sd(src);
    for (const QFileInfo& e : sd.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        const QString t = dst + QLatin1Char('/') + e.fileName();
        if (e.isDir() && !e.isSymLink() && isRealDir(t)) {
            if (!mergeInto(e.filePath(), t, overwrite, err)) return false;
            continue;
        }
        if (lexists(t)) {
            if (!overwrite) continue;
            if (!removePath(t)) {
                if (err) *err = QStringLiteral("cannot replace %1").arg(t);
                return false;
            }
        }
        if (std::rename(QFile::encodeName(e.filePath()).constData(), QFile::encodeName(t).constData()) != 0) {
            if (err) *err = QStringLiteral("cannot move %1 to %2").arg(e.filePath(), t);
            return false;
        }
    }
    return true;
}

QString readLink(const QString& path) {
    char buf[4096];
    const ssize_t n = ::readlink(QFile::encodeName(path).constData(), buf, sizeof buf - 1);
    return n < 0 ? QString() : QString::fromLocal8Bit(buf, static_cast<int>(n));
}

bool matchesAny(const QString& name, const QStringList& globs) {
    for (const QString& g : globs)
        if (QRegularExpression(QRegularExpression::wildcardToRegularExpression(g)).match(name).hasMatch()) return true;
    return false;
}

// Voert een programma uit (Wine) en wacht; kijkt tijdens het wachten naar annuleren.
bool runProcess(const QString& program, const QStringList& args, const QProcessEnvironment& env, int timeoutMs, const std::atomic_bool* cancel,
                QString* out, QString* err) {
    QProcess p;
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(10000)) {
        if (err) *err = QStringLiteral("cannot start %1").arg(program);
        return false;
    }
    int waited = 0;
    while (!p.waitForFinished(200)) {
        waited += 200;
        if (cancelled(cancel)) { p.kill(); p.waitForFinished(2000); if (err) *err = QStringLiteral("cancelled"); return false; }
        if (waited >= timeoutMs) { p.kill(); p.waitForFinished(2000); if (err) *err = QStringLiteral("%1 timed out").arg(program); return false; }
    }
    if (out) *out = QString::fromLocal8Bit(p.readAll());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (err) *err = QStringLiteral("%1 failed (exit %2)").arg(QFileInfo(program).fileName()).arg(p.exitCode());
        return false;
    }
    return true;
}

// --- ELF 32 bits: DT_NEEDED en DT_SONAME (voor de GStreamer-bibliotheken uit de sniper-runtime) ----------------------------------

struct ElfInfo {
    QStringList needed;
    QString soname;
    bool ok = false;
};

template <typename T>
T rd(const QByteArray& b, qint64 off) {
    T v{};
    if (off < 0 || off + static_cast<qint64>(sizeof(T)) > b.size()) return v;
    memcpy(&v, b.constData() + off, sizeof(T));
    return v;
}

ElfInfo readElf32(const QString& path) {
    ElfInfo info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return info;
    const QByteArray b = f.readAll();
    if (b.size() < 52 || b.left(4) != QByteArrayLiteral("\x7f""ELF") || b[4] != 1 || b[5] != 1) return info;   // alleen 32 bits, little endian
    const quint32 phoff = rd<quint32>(b, 28);
    const quint16 phentsize = rd<quint16>(b, 42), phnum = rd<quint16>(b, 44);
    struct Load { quint32 vaddr, off, filesz; };
    QList<Load> loads;
    quint32 dynOff = 0, dynSize = 0;
    for (int i = 0; i < phnum; ++i) {
        const qint64 p = phoff + static_cast<qint64>(i) * phentsize;
        const quint32 type = rd<quint32>(b, p), off = rd<quint32>(b, p + 4), vaddr = rd<quint32>(b, p + 8), filesz = rd<quint32>(b, p + 16);
        if (type == 1) loads.append({vaddr, off, filesz});
        if (type == 2) { dynOff = off; dynSize = filesz; }
    }
    if (!dynOff) return info;
    quint32 strtab = 0;
    QList<quint32> needed;
    quint32 soname = 0;
    bool hasSoname = false;
    for (quint32 o = 0; o + 8 <= dynSize; o += 8) {
        const qint32 tag = rd<qint32>(b, dynOff + o);
        const quint32 val = rd<quint32>(b, dynOff + o + 4);
        if (tag == 0) break;
        if (tag == 1) needed.append(val);
        if (tag == 5) strtab = val;
        if (tag == 14) { soname = val; hasSoname = true; }
    }
    quint32 strOff = 0;
    for (const Load& l : loads)
        if (strtab >= l.vaddr && strtab < l.vaddr + l.filesz) strOff = strtab - l.vaddr + l.off;
    if (!strOff) return info;
    auto str = [&](quint32 idx) { return QString::fromLatin1(b.constData() + strOff + idx); };
    for (quint32 n : needed) info.needed << str(n);
    if (hasSoname) info.soname = str(soname);
    info.ok = true;
    return info;
}

// --- recepten ---------------------------------------------------------------------------------------------------------------------

bool installExtract(const Component& c, const QStringList& files, const ComponentEnv& env, const ComponentInstaller::Progress& progress,
                    const std::atomic_bool* cancel, QString* err) {
    const QString st = stagingDir(env, c.id);
    if (!freshDir(st, err)) return false;
    for (int i = 0; i < files.size(); ++i) {
        const ComponentDownload& d = c.downloads.at(i);
        if (d.archive == QLatin1String("file")) {
            QString target = st + QLatin1Char('/') + d.name;
            if (!QFile::copy(files.at(i), target)) {
                if (err) *err = QStringLiteral("cannot copy %1").arg(d.name);
                return false;
            }
            continue;
        }
        ExtractOptions o;
        o.strip = c.strip;
        o.subdir = c.subdir;
        o.include = c.include;
        o.map = c.map;
        o.cancel = cancel;
        o.progress = [&](qint64 done, qint64 total) { if (progress) progress(QStringLiteral("Extracting"), done, total); };
        if (!extractArchive(files.at(i), st, o, err)) return false;
    }
    if (!c.treeSha256.isEmpty()) {
        const QString have = Components::treeSha256(st);
        if (have != c.treeSha256) {
            if (err) *err = QStringLiteral("tree sha256 mismatch (%1, expected %2)").arg(have, c.treeSha256);
            return false;
        }
    }
    return ComponentInstaller::placeStaging(st, env.path(c.dest), c.merge, true, err);
}

bool installCopyTree(const Component& c, const ComponentEnv& env, const std::atomic_bool* cancel, QString* err) {
    QString src;
    bool overwrite = true;
    if (c.kind == QLatin1String("bundled")) {
        src = env.bundledRoot + QLatin1Char('/') + c.id;
    } else {
        src = Components::findUserFolderSource(c, env);
        overwrite = false;   // wat er al staat (profielen, instellingen van de gebruiker) blijft
    }
    if (src.isEmpty() || !QFileInfo(src).isDir()) {
        if (err) *err = QStringLiteral("no source folder found for %1").arg(c.name);
        return false;
    }
    const QString st = stagingDir(env, c.id);
    if (!freshDir(st, err)) return false;
    if (!ComponentInstaller::copyTree(src, st, c.exclude, cancel, err)) return false;
    return ComponentInstaller::placeStaging(st, env.path(c.dest), true, overwrite, err);
}

// DirectX-herdistribueerbare: alle DLL's uit *_x86.cab en *_x64.cab, in kleine letters, naar redist/{x86,x64}.
bool installRedistDirectx(const Component& c, const ComponentEnv& env, const ComponentInstaller::Progress& progress, const std::atomic_bool* cancel,
                          QString* err) {
    const QString src = Components::findRedistDir(env);
    if (src.isEmpty()) { if (err) *err = QStringLiteral("DirectX redistributable not found in Steam"); return false; }
    const QString st = stagingDir(env, c.id);
    if (!freshDir(st, err)) return false;
    for (const QString arch : {QStringLiteral("x86"), QStringLiteral("x64")}) {
        const QStringList cabs = QDir(src).entryList({QStringLiteral("*_%1.cab").arg(arch)}, QDir::Files, QDir::Name);
        int n = 0;
        for (const QString& cab : cabs) {
            ExtractOptions o;
            o.flattenLower = true;
            o.nameFilterSuffix = QStringLiteral(".dll");
            o.cancel = cancel;
            if (!extractArchive(src + QLatin1Char('/') + cab, st + QLatin1Char('/') + arch, o, err)) return false;
            if (progress) progress(QStringLiteral("Extracting"), ++n, cabs.size());
        }
    }
    QFile bron(st + QStringLiteral("/BRON.txt"));
    if (bron.open(QIODevice::WriteOnly)) bron.write(("bron: " + src + "\n").toUtf8());
    return ComponentInstaller::placeStaging(st, env.path(c.dest), true, true, err);
}

// Visual C++ 2010: de DLL's staan in cab-bestanden binnen vcredist_<arch>.exe; zoek de MSCF-koppen en lees die cabs.
bool installRedistVc2010(const Component& c, const ComponentEnv& env, const std::atomic_bool* cancel, QString* err) {
    const QString src = Components::findVcredistDir(env);
    if (src.isEmpty()) { if (err) *err = QStringLiteral("Visual C++ 2010 redistributable not found in Steam"); return false; }
    const QString st = stagingDir(env, c.id);
    if (!freshDir(st, err)) return false;
    const QStringList names{QStringLiteral("mfc100u"), QStringLiteral("mfcm100u"), QStringLiteral("mfc100"), QStringLiteral("msvcr100"),
                            QStringLiteral("msvcp100"), QStringLiteral("atl100")};
    int total = 0;
    for (const QString arch : {QStringLiteral("x86"), QStringLiteral("x64")}) {
        QFile exe(src + QStringLiteral("/vcredist_%1.exe").arg(arch));
        if (!exe.open(QIODevice::ReadOnly)) continue;
        const QByteArray b = exe.readAll();
        ExtractOptions o;
        o.cancel = cancel;
        for (const QString& n : names) o.map.append({QStringLiteral("F_CENTRAL_%1_%2").arg(n, arch), QStringLiteral("%1/%2.dll").arg(arch, n)});
        int pos = 0, idx = 0;
        while ((pos = b.indexOf(QByteArrayLiteral("MSCF\0\0\0\0"), pos)) >= 0) {
            const quint32 cb = rd<quint32>(b, pos + 8);
            if (cb < 36 || pos + static_cast<qint64>(cb) > b.size()) { pos += 4; continue; }
            const QString tmp = st + QStringLiteral("/.cab-%1-%2").arg(arch).arg(idx++);
            QFile t(tmp);
            if (!t.open(QIODevice::WriteOnly) || t.write(b.constData() + pos, cb) != cb) { if (err) *err = QStringLiteral("cannot write %1").arg(tmp); return false; }
            t.close();
            QString e2;
            if (!extractArchive(tmp, st, o, &e2) && cancelled(cancel)) { if (err) *err = e2; return false; }
            QFile::remove(tmp);
            pos += static_cast<int>(cb);
        }
        for (const QString& n : names)
            if (QFileInfo::exists(st + QStringLiteral("/%1/%2.dll").arg(arch, n))) ++total;
    }
    if (total == 0) { if (err) *err = QStringLiteral("no Visual C++ 2010 DLLs found in the Steam redistributable"); return false; }
    return ComponentInstaller::placeStaging(st, env.path(c.dest), true, true, err);
}

// 32-bits GStreamer: links naar de plug-ins van een Proton in Steam, plus wat die plug-ins nog vragen uit de sniper-runtime.
bool installGst32(const Component& c, const ComponentEnv& env, const std::atomic_bool* cancel, QString* err) {
    const QString pick = Components::findProtonGst32(env);
    if (pick.isEmpty()) { if (err) *err = QStringLiteral("no Proton with 32-bit GStreamer (libgstlibav) found in Steam"); return false; }
    const QString sniperLib = Components::findSniperPlatformLib32(env);
    const QString st = stagingDir(env, c.id);
    if (!freshDir(st, err)) return false;
    const QString plugins = st + QStringLiteral("/plugins"), libdir = st + QStringLiteral("/lib");
    QDir().mkpath(plugins);
    QDir().mkpath(libdir);
    if (!QFile::link(pick, st + QStringLiteral("/proton"))) { if (err) *err = QStringLiteral("cannot create link in %1").arg(st); return false; }
    QStringList todo;
    for (const QString& n : QDir(pick + QStringLiteral("/gstreamer-1.0")).entryList({QStringLiteral("*.so")}, QDir::Files, QDir::Name)) {
        if (n.contains(QLatin1String("protonmediaconverter"))) continue;
        const QString full = pick + QStringLiteral("/gstreamer-1.0/") + n;
        QFile::link(full, plugins + QLatin1Char('/') + n);
        todo << full;
    }
    static const QSet<QString> glibc{QStringLiteral("libc.so.6"), QStringLiteral("libm.so.6"), QStringLiteral("libpthread.so.0"),
                                     QStringLiteral("libdl.so.2"), QStringLiteral("librt.so.1"), QStringLiteral("ld-linux.so.2"),
                                     QStringLiteral("libresolv.so.2")};
    QMap<QString, QString> bySoname;
    if (!sniperLib.isEmpty()) {
        for (const QFileInfo& fi : QDir(sniperLib).entryInfoList(QDir::Files | QDir::NoSymLinks)) {
            if (!fi.fileName().contains(QLatin1String(".so"))) continue;
            const ElfInfo ei = readElf32(fi.filePath());
            if (ei.ok && !ei.soname.isEmpty() && !bySoname.contains(ei.soname)) bySoname.insert(ei.soname, fi.filePath());
        }
    }
    auto find = [&](const QString& lib) -> QString {
        for (const QString& d : {pick, QStringLiteral("/usr/lib32"), libdir})
            if (lexists(d + QLatin1Char('/') + lib)) return d + QLatin1Char('/') + lib;
        return {};
    };
    QSet<QString> seen;
    QStringList linked, missing;
    while (!todo.isEmpty()) {
        if (cancelled(cancel)) { if (err) *err = QStringLiteral("cancelled"); return false; }
        const ElfInfo ei = readElf32(todo.takeLast());
        for (const QString& lib : ei.needed) {
            if (seen.contains(lib) || glibc.contains(lib)) continue;
            seen.insert(lib);
            const QString f = find(lib);
            if (!f.isEmpty()) { todo << f; continue; }
            const QString s = bySoname.value(lib);
            if (s.isEmpty()) { missing << lib; continue; }
            QFile::link(s, libdir + QLatin1Char('/') + lib);
            linked << lib;
            todo << s;
        }
    }
    QFile bron(st + QStringLiteral("/BRON.txt"));
    if (bron.open(QIODevice::WriteOnly))
        bron.write(("proton: " + pick + "\nsniper: " + (sniperLib.isEmpty() ? QStringLiteral("-") : sniperLib) + "\nmissing: " + missing.join(QLatin1Char(' ')) + "\n").toUtf8());
    return ComponentInstaller::placeStaging(st, env.path(c.dest), false, true, err);
}

// i386-bibliotheken uit de Steam Linux Runtime (sniper) naar runtime/lib32.
bool installLib32Sniper(const Component& c, const ComponentEnv& env, QString* err) {
    const QString sn = Components::findSniperLib32(env);
    if (sn.isEmpty()) { if (err) *err = QStringLiteral("Steam Linux Runtime (sniper) with i386 libraries not found"); return false; }
    const QString st = stagingDir(env, c.id);
    if (!freshDir(st, err)) return false;
    static const QStringList libs{QStringLiteral("libglut.so.3"), QStringLiteral("libXi.so.6"), QStringLiteral("libGLU.so.1"), QStringLiteral("libtheora.so.0"),
                                  QStringLiteral("libXrandr.so.2"), QStringLiteral("libXrender.so.1"), QStringLiteral("libsamplerate.so.0"),
                                  QStringLiteral("libXcursor.so.1"), QStringLiteral("libxkbcommon.so.0"), QStringLiteral("libdecor-0.so.0"),
                                  QStringLiteral("libSDL2-2.0.so.0")};
    QStringList lacking;
    for (const QString& l : libs) {
        const QString from = sn + QLatin1Char('/') + l;
        if (!QFileInfo::exists(from)) { lacking << l; continue; }
        if (!QFile::copy(QFileInfo(from).canonicalFilePath(), st + QLatin1Char('/') + l)) { if (err) *err = QStringLiteral("cannot copy %1").arg(l); return false; }
    }
    if (lacking.size() == libs.size()) { if (err) *err = QStringLiteral("none of the expected libraries were found in %1").arg(sn); return false; }
    return ComponentInstaller::placeStaging(st, env.path(c.dest), true, true, err);
}

QProcessEnvironment wineEnv(const ComponentEnv& env) {
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert(QStringLiteral("WINEPREFIX"), env.path(QStringLiteral("prefixes/shared/wine")));
    pe.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));
    pe.insert(QStringLiteral("WINESERVER"), env.path(QStringLiteral("wine/proton-11.0-2/bin/wineserver")));
    pe.insert(QStringLiteral("WINEDLLOVERRIDES"), QStringLiteral("mscoree,mshtml=d"));   // geen venster dat Mono of Gecko wil downloaden
    return pe;
}

bool installWinePrefix(const ComponentEnv& env, const std::atomic_bool* cancel, QString* err) {
    const QString wine = env.path(QStringLiteral("wine/proton-11.0-2/bin/wine"));
    if (!QFileInfo(wine).isExecutable()) { if (err) *err = QStringLiteral("Wine is not installed yet (%1)").arg(wine); return false; }
    QDir().mkpath(env.path(QStringLiteral("prefixes/shared")));
    QString out;
    if (!runProcess(wine, {QStringLiteral("wineboot"), QStringLiteral("-u")}, wineEnv(env), 300000, cancel, &out, err)) return false;
    runProcess(env.path(QStringLiteral("wine/proton-11.0-2/bin/wineserver")), {QStringLiteral("-w")}, wineEnv(env), 120000, cancel, nullptr, nullptr);
    if (!QFileInfo::exists(env.path(QStringLiteral("prefixes/shared/wine/system.reg")))) {
        if (err) *err = QStringLiteral("wineboot did not create a prefix");
        return false;
    }
    return true;
}

bool installWinePhysx(const Component& c, const QStringList& files, const ComponentEnv& env, const std::atomic_bool* cancel, QString* err) {
    const QString wine = env.path(QStringLiteral("wine/proton-11.0-2/bin/wine"));
    if (!QFileInfo(wine).isExecutable()) { if (err) *err = QStringLiteral("Wine is not installed yet (%1)").arg(wine); return false; }
    if (!QFileInfo::exists(env.path(QStringLiteral("prefixes/shared/wine/drive_c")))) { if (err) *err = QStringLiteral("the shared Wine prefix does not exist yet"); return false; }
    QString msi = QStringLiteral("Z:") + files.value(0);
    msi.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (!runProcess(wine, {QStringLiteral("msiexec"), QStringLiteral("/i"), msi, QStringLiteral("/qn")}, wineEnv(env), 600000, cancel, nullptr, err)) return false;
    runProcess(env.path(QStringLiteral("wine/proton-11.0-2/bin/wineserver")), {QStringLiteral("-w")}, wineEnv(env), 120000, cancel, nullptr, nullptr);
    for (const QString& d : c.detect)
        if (!lexists(env.path(d))) { if (err) *err = QStringLiteral("PhysX installation did not produce %1").arg(d); return false; }
    return true;
}

}  // namespace

// --- publieke hulpfuncties -----------------------------------------------------------------------------------------------------

bool ComponentInstaller::placeStaging(const QString& staging, const QString& dest, bool merge, bool overwrite, QString* err) {
    QDir().mkpath(QFileInfo(dest).dir().absolutePath());
    if (merge) {
        if (!mergeInto(staging, dest, overwrite, err)) return false;
        QDir(staging).removeRecursively();
        return true;
    }
    QString old;
    if (lexists(dest)) {
        old = dest + QStringLiteral(".old-%1").arg(QCoreApplication::applicationPid());
        if (lexists(old)) removePath(old);
        if (std::rename(QFile::encodeName(dest).constData(), QFile::encodeName(old).constData()) != 0) {
            if (err) *err = QStringLiteral("cannot move the existing %1 out of the way").arg(dest);
            return false;
        }
    }
    if (std::rename(QFile::encodeName(staging).constData(), QFile::encodeName(dest).constData()) != 0) {
        if (!old.isEmpty()) std::rename(QFile::encodeName(old).constData(), QFile::encodeName(dest).constData());
        if (err) *err = QStringLiteral("cannot move %1 to %2").arg(staging, dest);
        return false;
    }
    if (!old.isEmpty()) removePath(old);
    return true;
}

bool ComponentInstaller::copyTree(const QString& src, const QString& dst, const QStringList& exclude, const std::atomic_bool* cancel, QString* err) {
    if (!QDir().mkpath(dst)) {
        if (err) *err = QStringLiteral("cannot create %1").arg(dst);
        return false;
    }
    for (const QFileInfo& e : QDir(src).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        if (cancelled(cancel)) { if (err) *err = QStringLiteral("cancelled"); return false; }
        if (matchesAny(e.fileName(), exclude)) continue;
        const QString t = dst + QLatin1Char('/') + e.fileName();
        if (e.isSymLink()) {
            if (::symlink(QFile::encodeName(readLink(e.filePath())).constData(), QFile::encodeName(t).constData()) != 0) {
                if (err) *err = QStringLiteral("cannot create link %1").arg(t);
                return false;
            }
        } else if (e.isDir()) {
            if (!copyTree(e.filePath(), t, exclude, cancel, err)) return false;
        } else if (e.isFile()) {
            if (!QFile::copy(e.filePath(), t)) {
                if (err) *err = QStringLiteral("cannot copy %1").arg(e.filePath());
                return false;
            }
        }
    }
    return true;
}

bool ComponentInstaller::installFromFiles(const Component& c, const QStringList& files, const ComponentEnv& env, const Progress& progress,
                                          const std::atomic_bool* cancel, QString* err) {
    bool ok = false;
    if (c.recipe == QLatin1String("extract")) {
        if (c.kind == QLatin1String("bundled") || c.kind == QLatin1String("userfolder")) ok = installCopyTree(c, env, cancel, err);
        else ok = installExtract(c, files, env, progress, cancel, err);
    } else if (c.recipe == QLatin1String("redist-directx")) ok = installRedistDirectx(c, env, progress, cancel, err);
    else if (c.recipe == QLatin1String("redist-vc2010")) ok = installRedistVc2010(c, env, cancel, err);
    else if (c.recipe == QLatin1String("gst32")) ok = installGst32(c, env, cancel, err);
    else if (c.recipe == QLatin1String("lib32-sniper")) ok = installLib32Sniper(c, env, err);
    else if (c.recipe == QLatin1String("wine-prefix")) ok = installWinePrefix(env, cancel, err);
    else if (c.recipe == QLatin1String("wine-physx")) ok = installWinePhysx(c, files, env, cancel, err);
    else if (err) *err = QStringLiteral("unknown recipe '%1'").arg(c.recipe);
    if (!ok) return false;
    for (const auto& l : c.links) {
        const QString p = env.path(l.first);
        QDir().mkpath(QFileInfo(p).dir().absolutePath());
        if (lexists(p)) removePath(p);
        if (::symlink(QFile::encodeName(l.second).constData(), QFile::encodeName(p).constData()) != 0) {
            if (err) *err = QStringLiteral("cannot create link %1").arg(p);
            return false;
        }
    }
    for (const auto& w : c.writeFiles) {
        const QString p = env.path(w.first);
        QDir().mkpath(QFileInfo(p).dir().absolutePath());
        QSaveFile f(p);
        if (!f.open(QIODevice::WriteOnly) || f.write(w.second.toUtf8()) < 0 || !f.commit()) {
            if (err) *err = QStringLiteral("cannot write %1").arg(p);
            return false;
        }
    }
    QDir(stagingDir(env, c.id)).removeRecursively();
    for (const QString& d : c.detect) {
        if (!lexists(env.path(d))) {
            if (err) *err = QStringLiteral("installed, but %1 is still missing (the source does not contain it)").arg(d);
            return false;
        }
    }
    return Components::writeStamp(env, c, err);
}

// --- de klasse: wachtrij, downloaden, thread ----------------------------------------------------------------------------------

ComponentInstaller::ComponentInstaller(const ComponentEnv& env, QObject* parent) : QObject(parent), env_(env), nam_(new QNetworkAccessManager(this)) {}

ComponentInstaller::~ComponentInstaller() {
    cancel_ = true;
    if (reply_) { reply_->disconnect(this); reply_->abort(); }
    if (worker_) { worker_->wait(); }
}

QString ComponentInstaller::downloadPath(const ComponentDownload& d) const { return env_.dataDir + QStringLiteral("/components/dl/") + d.name; }

void ComponentInstaller::install(const QList<Component>& all, const QStringList& ids) {
    if (busy_) return;
    all_ = all;
    queue_ = Components::withDependencies(all, ids);
    busy_ = true;
    allOk_ = true;
    cancel_ = false;
    QMetaObject::invokeMethod(this, &ComponentInstaller::next, Qt::QueuedConnection);
}

void ComponentInstaller::cancel() {
    cancel_ = true;
    if (reply_) reply_->abort();
}

void ComponentInstaller::finishCurrent(bool ok, const QString& msg) {
    if (!ok) allOk_ = false;
    emit finished(cur_.id, ok, msg);
    if (!ok && cancel_) queue_.clear();
    QMetaObject::invokeMethod(this, &ComponentInstaller::next, Qt::QueuedConnection);
}

void ComponentInstaller::next() {
    if (queue_.isEmpty()) {
        busy_ = false;
        emit allFinished(allOk_);
        return;
    }
    cur_ = queue_.takeFirst();
    emit started(cur_.id);
    const ComponentStatus st = Components::status(cur_, env_);
    if (st.state == ComponentState::Installed) { finishCurrent(true, QStringLiteral("already installed")); return; }
    if (st.state == ComponentState::Unavailable) { finishCurrent(false, st.detail); return; }
    files_.clear();
    dlIndex_ = 0;
    downloadNext();
}

void ComponentInstaller::downloadNext() {
    if (cancel_) { finishCurrent(false, QStringLiteral("cancelled")); return; }
    if (dlIndex_ >= cur_.downloads.size()) { runWorker(); return; }
    const ComponentDownload& d = cur_.downloads.at(dlIndex_);
    const QString target = downloadPath(d);
    QDir().mkpath(QFileInfo(target).dir().absolutePath());
    if (!d.sha256.isEmpty() && QFileInfo::exists(target) && Components::sha256File(target) == d.sha256) {
        files_ << target;
        ++dlIndex_;
        downloadNext();
        return;
    }
    QFile::remove(target);
    retriedNoRange_ = false;
    startRequest(true);
}

void ComponentInstaller::startRequest(bool allowResume) {
    const ComponentDownload& d = cur_.downloads.at(dlIndex_);
    const QString partPath = downloadPath(d) + QStringLiteral(".part");
    const QUrl url(d.url);
    resumeFrom_ = 0;
    resumeRequested_ = false;
    if (allowResume && url.scheme() == QLatin1String("https")) resumeFrom_ = QFileInfo(partPath).size();
    else QFile::remove(partPath);
    part_ = new QFile(partPath, this);
    if (!part_->open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QString m = QStringLiteral("cannot write %1").arg(partPath);
        delete part_;
        part_ = nullptr;
        finishCurrent(false, m);
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("RavenGun-components"));
    if (resumeFrom_ > 0) {
        req.setRawHeader("Range", "bytes=" + QByteArray::number(resumeFrom_) + "-");
        resumeRequested_ = true;
    }
    reply_ = nam_->get(req);
    connect(reply_, &QNetworkReply::readyRead, this, &ComponentInstaller::onReadyRead);
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        emit progress(cur_.id, QStringLiteral("Downloading"), resumeFrom_ + got, total > 0 ? resumeFrom_ + total : cur_.downloads.at(dlIndex_).size);
    });
    connect(reply_, &QNetworkReply::finished, this, &ComponentInstaller::onReplyFinished);
}

void ComponentInstaller::onReadyRead() {
    if (!reply_ || !part_) return;
    if (resumeRequested_ && reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
        // De server negeerde Range: opnieuw vanaf het begin.
        part_->resize(0);
        resumeFrom_ = 0;
        resumeRequested_ = false;
    }
    part_->write(reply_->readAll());
}

void ComponentInstaller::onReplyFinished() {
    QNetworkReply* r = reply_;
    reply_ = nullptr;
    r->deleteLater();
    const ComponentDownload d = cur_.downloads.at(dlIndex_);
    const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (part_) {
        if (r->error() == QNetworkReply::NoError) part_->write(r->readAll());
        part_->close();
    }
    const QString partPath = downloadPath(d) + QStringLiteral(".part");
    delete part_;
    part_ = nullptr;
    if (cancel_) { finishCurrent(false, QStringLiteral("cancelled")); return; }   // de .part blijft staan om te hervatten
    if (code == 416 && resumeRequested_) {   // het deel was al compleet of hoort niet bij dit bestand
        if (d.sha256.isEmpty() || Components::sha256File(partPath) != d.sha256) { QFile::remove(partPath); if (!retriedNoRange_) { retriedNoRange_ = true; startRequest(false); return; } }
    } else if (r->error() != QNetworkReply::NoError) {
        finishCurrent(false, QStringLiteral("download of %1 failed: %2").arg(d.name, r->errorString()));
        return;
    }
    emit progress(cur_.id, QStringLiteral("Verifying"), 0, 0);
    if (!d.sha256.isEmpty() && Components::sha256File(partPath) != d.sha256) {
        QFile::remove(partPath);
        finishCurrent(false, QStringLiteral("sha256 of %1 does not match the catalogue; the download was discarded").arg(d.name));
        return;
    }
    const QString target = downloadPath(d);
    QFile::remove(target);
    if (!QFile::rename(partPath, target)) { finishCurrent(false, QStringLiteral("cannot rename %1").arg(partPath)); return; }
    files_ << target;
    ++dlIndex_;
    downloadNext();
}

void ComponentInstaller::runWorker() {
    const Component c = cur_;
    const QStringList files = files_;
    const ComponentEnv env = env_;
    worker_ = QThread::create([this, c, files, env] {
        QString err;
        const bool ok = installFromFiles(
            c, files, env,
            [this, id = c.id](const QString& phase, qint64 done, qint64 total) {
                QMetaObject::invokeMethod(this, [this, id, phase, done, total] { emit progress(id, phase, done, total); }, Qt::QueuedConnection);
            },
            &cancel_, &err);
        QMetaObject::invokeMethod(this, [this, ok, err] {
            worker_->wait();
            worker_->deleteLater();
            worker_ = nullptr;
            if (ok) for (const auto& d : cur_.downloads) QFile::remove(downloadPath(d));   // het archief is niet meer nodig
            finishCurrent(ok, ok ? QStringLiteral("installed") : err);
        }, Qt::QueuedConnection);
    });
    emit progress(c.id, QStringLiteral("Installing"), 0, 0);
    worker_->start();
}

}  // namespace rg
