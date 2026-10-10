// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/MemoryCard.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include "core/FileSafety.h"

namespace rg {

namespace {

struct Meta {
    QString label, kind, safe = QStringLiteral("yes"), effect;
    bool markedLocal = false;
};

void addFile(QList<MemoryCardFile>& out, QSet<QString>& seen, const QFileInfo& fi, const Meta& m) {
    const QString path = fi.absoluteFilePath();
    if (seen.contains(path)) return;
    seen.insert(path);
    MemoryCardFile f;
    f.path = path;
    f.label = m.label;
    f.kind = m.kind;
    f.safeToErase = m.safe;
    f.eraseEffect = m.effect;
    f.size = fi.size();
    f.modified = fi.lastModified();
    f.onNas = isOnNetwork(path);
    if (f.onNas) {
        f.reason = m.markedLocal ? QStringLiteral("marked local, but the file is on the NAS: read-only") : QStringLiteral("on NAS: read-only");
    } else if (fi.isSymLink()) {
        f.reason = QStringLiteral("symbolic link: not deleted");
    } else if (m.safe == QStringLiteral("no")) {
        f.reason = QStringLiteral("protected: the game needs this file");
    } else {
        f.deletable = true;
    }
    out << f;
}

void addTree(QList<MemoryCardFile>& out, QSet<QString>& seen, const QString& dir, const Meta& m, bool onlyRealFiles, int maxFiles) {
    // Niet doorlopen via symlinks naar mappen (een view-map linkt naar de hele game op de NAS).
    QList<QPair<QString, int>> todo{{dir, 0}};
    while (!todo.isEmpty() && out.size() < maxFiles) {
        const auto [d, depth] = todo.takeFirst();
        const QFileInfoList entries =
            QDir(d).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
        for (const QFileInfo& fi : entries) {
            if (out.size() >= maxFiles) break;
            if (fi.isSymLink()) {
                if (!onlyRealFiles && fi.isFile()) addFile(out, seen, fi, m);
                continue;
            }
            if (fi.isDir()) {
                if (depth < 4) todo.append({fi.absoluteFilePath(), depth + 1});
                continue;
            }
            if (onlyRealFiles && (fi.fileName().endsWith(QStringLiteral(".ravengun-bak")) || fi.fileName().endsWith(QStringLiteral(".rg-overlay"))))
                continue;
            addFile(out, seen, fi, m);
        }
    }
}

}  // namespace

QList<MemoryCardFile> MemoryCard::list(const GameContext& c, const QList<MemoryCardSpec>& specs, int maxFiles) {
    QList<MemoryCardFile> out;
    QSet<QString> seen;
    for (const MemoryCardSpec& s : specs) {
        Meta m;
        m.label = s.label.isEmpty() ? QStringLiteral("Save data") : s.label;
        m.kind = s.kind;
        m.safe = s.safeToErase.isEmpty() ? QStringLiteral("yes") : s.safeToErase;
        m.effect = s.eraseEffect;
        m.markedLocal = s.local;
        for (const QString& pattern : GameOptionsDb::expandBraces(c.expand(s.path))) {
            const QString p = c.resolve(pattern, s.base);
            if (p.isEmpty()) continue;
            if (p.contains(QLatin1Char('*')) || p.contains(QLatin1Char('?'))) {
                const QFileInfo pat(p);
                const QFileInfoList hits = QDir(pat.path()).entryInfoList(QStringList{pat.fileName()}, QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
                for (const QFileInfo& fi : hits) {
                    if (out.size() >= maxFiles) break;
                    addFile(out, seen, fi, m);
                }
                continue;
            }
            const QFileInfo fi(p);
            if (fi.isDir()) addTree(out, seen, p, m, false, maxFiles);
            else if (fi.exists()) addFile(out, seen, fi, m);
        }
    }
    if (!c.viewDir.isEmpty() && QFileInfo(c.viewDir).isDir()) {
        Meta m;
        m.label = QStringLiteral("Created by the game (local view)");
        m.kind = QStringLiteral("local");
        m.safe = QStringLiteral("unknown");
        addTree(out, seen, c.viewDir, m, true, maxFiles);
    }
    return out;
}

bool MemoryCard::remove(const MemoryCardFile& f, QString* err) {
    const QFileInfo fi(f.path);
    if (!f.deletable || fi.isSymLink() || isOnNetwork(f.path) || f.safeToErase == QStringLiteral("no")) {
        if (err) *err = f.reason.isEmpty() ? QStringLiteral("not deletable") : f.reason;
        return false;
    }
    if (!fi.exists()) return true;
    if (!QFile::remove(f.path)) {
        if (err) *err = QStringLiteral("could not delete %1").arg(f.path);
        return false;
    }
    return true;
}

}  // namespace rg
