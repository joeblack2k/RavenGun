// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ArchiveExtract.h"

#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace rg {
namespace {

// Pad uit het archief naar schone onderdelen; ok=false als het pad onveilig is.
QStringList cleanParts(const QString& path, bool* ok) {
    *ok = true;
    QStringList parts;
    for (const QString& p : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (p == QLatin1String(".")) continue;
        if (p == QLatin1String("..")) { *ok = false; return {}; }
        parts << p;
    }
    return parts;
}

bool selected(const QString& rel, const ExtractOptions& o, QString* target) {
    *target = rel;
    for (const auto& m : o.map)
        if (m.first == rel) { *target = m.second; return true; }
    if (o.include.isEmpty()) return o.map.isEmpty();
    for (const QString& inc : o.include)
        if (inc.endsWith(QLatin1Char('/')) ? rel.startsWith(inc) : rel == inc) return true;
    return false;
}

// Wijst de symlink `rel` (pad binnen de uitpakmap) met doel `target` binnen de uitpakmap?
bool linkStaysInside(const QString& rel, const QString& target) {
    if (target.startsWith(QLatin1Char('/'))) return false;
    QStringList stack = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!stack.isEmpty()) stack.removeLast();
    for (const QString& p : target.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (p == QLatin1String(".")) continue;
        if (p == QLatin1String("..")) {
            if (stack.isEmpty()) return false;
            stack.removeLast();
        } else {
            stack << p;
        }
    }
    return true;
}

archive* openReader(const QString& file, QString* err) {
    archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, QFile::encodeName(file).constData(), 1 << 20) != ARCHIVE_OK) {
        if (err) *err = QStringLiteral("cannot open %1: %2").arg(file, QString::fromUtf8(archive_error_string(a)));
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

struct ReadGuard {
    archive* a;
    ~ReadGuard() { archive_read_free(a); }
};
struct WriteGuard {
    archive* w;
    ~WriteGuard() { archive_write_free(w); }
};

}  // namespace

QStringList listArchive(const QString& file, QString* err) {
    archive* a = openReader(file, err);
    if (!a) return {};
    ReadGuard rg{a};
    QStringList out;
    archive_entry* e = nullptr;
    while (archive_read_next_header(a, &e) == ARCHIVE_OK) out << QString::fromUtf8(archive_entry_pathname(e));
    return out;
}

bool extractArchive(const QString& file, const QString& destDir, const ExtractOptions& opt, QString* err) {
    archive* a = openReader(file, err);
    if (!a) return false;
    ReadGuard rguard{a};
    archive* w = archive_write_disk_new();
    WriteGuard wguard{w};
    archive_write_disk_set_options(w, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    // ARCHIVE_EXTRACT_SECURE_SYMLINKS past hier niet (het doelpad is absoluut en mag zelf een symlink bevatten); daarom weigert
    // deze functie zelf elke symlink die buiten de uitpakmap zou wijzen, zodat een latere regel er ook niet doorheen kan schrijven.
    const qint64 total = QFileInfo(file).size();
    if (!QDir().mkpath(destDir)) {
        if (err) *err = QStringLiteral("cannot create %1").arg(destDir);
        return false;
    }
    const QString base = QDir(destDir).absolutePath();

    // Doelpad van een regel uit het archief: strip, subdir, filter; leeg = overslaan, bad = onveilig pad.
    auto place = [&](const QString& raw, bool* bad) -> QString {
        bool ok = true;
        QStringList parts = cleanParts(raw, &ok);
        if (!ok) { *bad = true; return {}; }
        if (parts.size() <= opt.strip) return {};
        parts = parts.mid(opt.strip);
        if (!opt.subdir.isEmpty()) {
            const QStringList sd = opt.subdir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            if (parts.size() <= sd.size() || parts.mid(0, sd.size()) != sd) return {};
            parts = parts.mid(sd.size());
        }
        QString target;
        if (!selected(parts.join(QLatin1Char('/')), opt, &target)) return {};
        if (opt.flattenLower) {
            target = parts.last().toLower();
            if (!opt.nameFilterSuffix.isEmpty() && !target.endsWith(opt.nameFilterSuffix)) return {};
        }
        bool ok2 = true;
        if (cleanParts(target, &ok2).isEmpty() || !ok2) return {};
        return base + QLatin1Char('/') + target;
    };

    archive_entry* e = nullptr;
    int r;
    while ((r = archive_read_next_header(a, &e)) != ARCHIVE_EOF) {
        if (opt.cancel && opt.cancel->load()) { if (err) *err = QStringLiteral("cancelled"); return false; }
        if (r < ARCHIVE_WARN) {
            if (err) *err = QStringLiteral("read error in %1: %2").arg(file, QString::fromUtf8(archive_error_string(a)));
            return false;
        }
        bool bad = false;
        const QString raw = QString::fromUtf8(archive_entry_pathname(e));
        const QString dst = place(raw, &bad);
        if (bad) { if (err) *err = QStringLiteral("unsafe path in archive: %1").arg(raw); return false; }
        if (dst.isEmpty()) continue;
        const auto type = archive_entry_filetype(e);
        if (type != AE_IFREG && type != AE_IFDIR && type != AE_IFLNK) continue;   // geen apparaten of fifo's
        if (opt.flattenLower && type != AE_IFREG) continue;
        if (type == AE_IFLNK) {
            const QString target = QString::fromUtf8(archive_entry_symlink(e));
            if (!linkStaysInside(dst.mid(base.size() + 1), target)) {
                if (err) *err = QStringLiteral("symlink points outside the target folder: %1 -> %2").arg(raw, target);
                return false;
            }
        }
        archive_entry_set_pathname(e, QFile::encodeName(dst).constData());
        if (const char* hl = archive_entry_hardlink(e)) {
            bool bad2 = false;
            const QString hdst = place(QString::fromUtf8(hl), &bad2);
            if (hdst.isEmpty()) continue;
            archive_entry_set_hardlink(e, QFile::encodeName(hdst).constData());
        }
        if (archive_write_header(w, e) != ARCHIVE_OK) {
            if (err) *err = QStringLiteral("cannot write %1: %2").arg(dst, QString::fromUtf8(archive_error_string(w)));
            return false;
        }
        if (type == AE_IFREG && archive_entry_size(e) > 0) {
            const void* buf;
            size_t len;
            la_int64_t off;
            int rr;
            while ((rr = archive_read_data_block(a, &buf, &len, &off)) != ARCHIVE_EOF) {
                if (rr < ARCHIVE_WARN) {
                    if (err) *err = QStringLiteral("read error in %1: %2").arg(file, QString::fromUtf8(archive_error_string(a)));
                    return false;
                }
                if (archive_write_data_block(w, buf, len, off) < ARCHIVE_WARN) {
                    if (err) *err = QStringLiteral("write error for %1: %2").arg(dst, QString::fromUtf8(archive_error_string(w)));
                    return false;
                }
                if (opt.cancel && opt.cancel->load()) { if (err) *err = QStringLiteral("cancelled"); return false; }
                if (opt.progress) opt.progress(archive_filter_bytes(a, -1), total);
            }
        }
        archive_write_finish_entry(w);
    }
    if (opt.progress) opt.progress(total, total);
    return true;
}

}  // namespace rg
