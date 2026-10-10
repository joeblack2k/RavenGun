// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/FileSafety.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <sys/statfs.h>

#include "core/AppPaths.h"

namespace rg {

namespace {

QStringList nasPrefixes() {
    QStringList out{QDir::homePath() + QStringLiteral("/mnt")};
    for (const QString& p : qEnvironmentVariable("RAVENGUN_NAS_PREFIXES").split(QLatin1Char(':'), Qt::SkipEmptyParts))
        out << QDir::cleanPath(AppPaths::expandHome(p));
    return out;
}

bool underPrefix(const QString& path, const QStringList& prefixes) {
    for (const QString& p : prefixes)
        if (path == p || path.startsWith(p + QLatin1Char('/'))) return true;
    return false;
}

// Het dichtstbijzijnde bestaande pad (voor statfs op een bestand dat nog niet bestaat).
QString existingAncestor(QString p) {
    while (!p.isEmpty() && !QFileInfo::exists(p)) {
        const QString up = QFileInfo(p).path();
        if (up == p) break;
        p = up;
    }
    return p;
}

bool networkFs(const QString& path) {
    struct statfs st {};
    const QByteArray b = QFile::encodeName(existingAncestor(path));
    if (b.isEmpty() || ::statfs(b.constData(), &st) != 0) return false;
    switch (static_cast<unsigned long>(st.f_type)) {
        case 0xFF534D42UL:  // CIFS
        case 0xFE534D42UL:  // SMB2
        case 0x517BUL:      // SMB
        case 0x6969UL:      // NFS
            return true;
        default:
            return false;
    }
}

}  // namespace

bool isOnNetwork(const QString& path) {
    if (path.isEmpty()) return false;
    const QString abs = QDir::cleanPath(QFileInfo(AppPaths::expandHome(path)).absoluteFilePath());
    const QStringList prefixes = nasPrefixes();
    if (underPrefix(abs, prefixes)) return true;
    // Symlinks volgen (een view-map bevat links naar de NAS).
    const QString canon = QFileInfo(abs).canonicalFilePath();
    if (!canon.isEmpty() && underPrefix(canon, prefixes)) return true;
    return networkFs(canon.isEmpty() ? abs : canon);
}

WriteCheck checkWritable(const QString& path) {
    WriteCheck w;
    const QString abs = QDir::cleanPath(QFileInfo(AppPaths::expandHome(path)).absoluteFilePath());
    w.path = abs;
    const QFileInfo fi(abs);
    const QString dir = fi.path();
    if (isOnNetwork(dir) && !fi.isSymLink()) {
        w.reason = QStringLiteral("read-only (on NAS): %1").arg(abs);
        return w;
    }
    if (fi.isSymLink()) {
        // Een link in een lokale map naar een bestand op de NAS: alleen via een lokale kopie.
        if (isOnNetwork(dir)) {
            w.reason = QStringLiteral("read-only (on NAS): %1").arg(abs);
            return w;
        }
        if (isOnNetwork(fi.symLinkTarget())) {
            w.ok = true;
            w.needsOverlay = true;
            return w;
        }
    }
    if (fi.exists() && isOnNetwork(abs)) {
        w.reason = QStringLiteral("read-only (on NAS): %1").arg(abs);
        return w;
    }
    w.ok = true;
    return w;
}

bool makeLocalOverlay(const QString& linkPath, QString* err) {
    const QFileInfo fi(linkPath);
    if (!fi.isSymLink()) return true;
    QFile src(fi.symLinkTarget());
    if (!src.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot read %1: %2").arg(src.fileName(), src.errorString());
        return false;
    }
    const QByteArray data = src.readAll();
    src.close();
    // Eerst een tijdelijk bestand ernaast, dan de link vervangen (rename is atomisch, de NAS wordt niet aangeraakt).
    const QString tmp = linkPath + QStringLiteral(".rg-overlay");
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size()) {
        if (err) *err = QStringLiteral("cannot write %1").arg(tmp);
        return false;
    }
    out.close();
    if (::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(linkPath).constData()) != 0) {
        QFile::remove(tmp);
        if (err) *err = QStringLiteral("cannot replace link %1").arg(linkPath);
        return false;
    }
    return true;
}

QJsonDocument readJson(const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return {};
    }
    QJsonParseError pe{};
    QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (err) *err = QStringLiteral("%1 (offset %2)").arg(pe.errorString()).arg(pe.offset);
        return {};
    }
    return d;
}

bool writeTextAtomic(const QString& path, const QByteArray& data, QString* err, const QString& backupSuffix) {
    const WriteCheck w = checkWritable(path);
    if (!w.ok) {
        if (err) *err = w.reason;
        return false;
    }
    if (w.needsOverlay && !makeLocalOverlay(w.path, err)) return false;
    QDir().mkpath(QFileInfo(w.path).path());
    if (!backupSuffix.isEmpty() && QFileInfo::exists(w.path) && !QFileInfo::exists(w.path + backupSuffix))
        QFile::copy(w.path, w.path + backupSuffix);
    QSaveFile f(w.path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (err) *err = f.errorString();
        return false;
    }
    return true;
}

bool writeJsonAtomic(const QString& path, const QJsonObject& obj, QString* err, const QString& backupSuffix) {
    return writeTextAtomic(path, QJsonDocument(obj).toJson(QJsonDocument::Indented), err, backupSuffix);
}

}  // namespace rg
