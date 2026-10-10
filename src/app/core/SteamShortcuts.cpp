// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SteamShortcuts.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QtEndian>

#include "core/AppPaths.h"
#include "core/FileSafety.h"

namespace rg {

// --- VdfNode -----------------------------------------------------------------------------------------------------------

VdfNode VdfNode::string(const QByteArray& key, const QString& v) {
    VdfNode n;
    n.type = String;
    n.key = key;
    n.value = v.toUtf8();
    return n;
}

VdfNode VdfNode::int32(const QByteArray& key, qint32 v) {
    VdfNode n;
    n.type = Int32;
    n.key = key;
    n.value.resize(4);
    qToLittleEndian<qint32>(v, n.value.data());
    return n;
}

qint32 VdfNode::toInt32() const { return value.size() >= 4 ? qFromLittleEndian<qint32>(value.constData()) : 0; }

VdfNode* VdfNode::child(const QByteArray& k) {
    for (VdfNode& c : children)
        if (c.key.compare(k, Qt::CaseInsensitive) == 0) return &c;
    return nullptr;
}

const VdfNode* VdfNode::child(const QByteArray& k) const {
    for (const VdfNode& c : children)
        if (c.key.compare(k, Qt::CaseInsensitive) == 0) return &c;
    return nullptr;
}

void VdfNode::set(const VdfNode& n) {
    if (VdfNode* c = child(n.key)) {
        const QByteArray keep = c->key;
        *c = n;
        c->key = keep;
        return;
    }
    children << n;
}

// --- binair lezen en schrijven ----------------------------------------------------------------------------------------

namespace vdf {

namespace {

int fixedSize(quint8 t) {
    switch (t) {
        case VdfNode::Int32:
        case VdfNode::Float:
        case VdfNode::Ptr:
        case VdfNode::Color: return 4;
        case VdfNode::UInt64:
        case VdfNode::Int64: return 8;
        default: return -1;
    }
}

struct Reader {
    const QByteArray& d;
    qsizetype p = 0;
    QString err;

    bool cstring(QByteArray* out) {
        const qsizetype e = d.indexOf('\0', p);
        if (e < 0) {
            err = QStringLiteral("unterminated text at byte %1").arg(p);
            return false;
        }
        *out = d.mid(p, e - p);
        p = e + 1;
        return true;
    }

    bool wstring(QByteArray* out) {
        qsizetype q = p;
        while (q + 1 < d.size() && !(d[q] == '\0' && d[q + 1] == '\0')) q += 2;
        if (q + 1 >= d.size()) {
            err = QStringLiteral("unterminated wide text at byte %1").arg(p);
            return false;
        }
        *out = d.mid(p, q - p);
        p = q + 2;
        return true;
    }

    // Items tot 0x08/0x0B (of het einde van de gegevens als top true is).
    bool items(QList<VdfNode>* out, bool top, int depth) {
        if (depth > 64) {
            err = QStringLiteral("nested too deeply");
            return false;
        }
        while (p < d.size()) {
            const quint8 t = static_cast<quint8>(d[p++]);
            if (t == 0x08 || t == 0x0B) return true;
            VdfNode n;
            n.type = static_cast<VdfNode::Type>(t);
            if (!cstring(&n.key)) return false;
            if (t == VdfNode::Map) {
                if (!items(&n.children, false, depth + 1)) return false;
            } else if (t == VdfNode::String) {
                if (!cstring(&n.value)) return false;
            } else if (t == VdfNode::WString) {
                if (!wstring(&n.value)) return false;
            } else {
                const int sz = fixedSize(t);
                if (sz < 0) {
                    err = QStringLiteral("unknown value type 0x%1 at byte %2").arg(t, 2, 16, QLatin1Char('0')).arg(p - 1);
                    return false;
                }
                if (p + sz > d.size()) {
                    err = QStringLiteral("file ends inside a value");
                    return false;
                }
                n.value = d.mid(p, sz);
                p += sz;
            }
            out->append(n);
        }
        if (!top) {
            err = QStringLiteral("file ends inside a section");
            return false;
        }
        return true;
    }
};

void writeItems(QByteArray& out, const QList<VdfNode>& items) {
    for (const VdfNode& n : items) {
        out.append(static_cast<char>(n.type));
        out.append(n.key);
        out.append('\0');
        switch (n.type) {
            case VdfNode::Map:
                writeItems(out, n.children);
                out.append('\x08');
                break;
            case VdfNode::String:
                out.append(n.value);
                out.append('\0');
                break;
            case VdfNode::WString:
                out.append(n.value);
                out.append('\0');
                out.append('\0');
                break;
            default: out.append(n.value); break;
        }
    }
}

}  // namespace

bool parseBinary(const QByteArray& data, VdfNode* root, QString* err) {
    Reader r{data, 0, {}};
    VdfNode n;
    n.type = VdfNode::Map;
    if (!r.items(&n.children, true, 0)) {
        if (err) *err = r.err;
        return false;
    }
    if (r.p < data.size()) {
        if (err) *err = QStringLiteral("unexpected data after the end (byte %1)").arg(r.p);
        return false;
    }
    *root = n;
    return true;
}

QByteArray writeBinary(const VdfNode& root) {
    QByteArray out;
    writeItems(out, root.children);
    out.append('\x08');
    return out;
}

}  // namespace vdf

// --- Steam ------------------------------------------------------------------------------------------------------------

QString SteamShortcuts::steamRoot() {
    const QString env = qEnvironmentVariable("RAVENGUN_STEAM_ROOT");
    if (!env.isEmpty()) return env;
    for (const QString& rel : {QStringLiteral("/.local/share/Steam"), QStringLiteral("/.steam/steam")}) {
        const QString p = QDir::homePath() + rel;
        if (QFileInfo(p + QStringLiteral("/userdata")).isDir()) return QFileInfo(p).canonicalFilePath();
    }
    return {};
}

QString SteamUser::configDir() const { return SteamShortcuts::steamRoot() + QStringLiteral("/userdata/") + accountId + QStringLiteral("/config"); }

namespace {

// Tekst-VDF (loginusers.vdf): "sleutel" "waarde" en "sleutel" { ... }. Plat: pad "users/7656.../MostRecent" -> waarde.
QHash<QString, QString> parseTextVdf(const QString& text) {
    QHash<QString, QString> out;
    QStringList path;
    QString pendingKey;
    bool haveKey = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == QLatin1Char('"')) {
            QString tok;
            ++i;
            while (i < text.size() && text[i] != QLatin1Char('"')) {
                if (text[i] == QLatin1Char('\\') && i + 1 < text.size()) ++i;
                tok += text[i++];
            }
            if (!haveKey) {
                pendingKey = tok;
                haveKey = true;
            } else {
                out.insert((path + QStringList{pendingKey}).join(QLatin1Char('/')), tok);
                haveKey = false;
            }
        } else if (c == QLatin1Char('{')) {
            path << (haveKey ? pendingKey : QString());
            haveKey = false;
        } else if (c == QLatin1Char('}')) {
            if (!path.isEmpty()) path.removeLast();
            haveKey = false;
        } else if (c == QLatin1Char('/') && i + 1 < text.size() && text[i + 1] == QLatin1Char('/')) {
            while (i < text.size() && text[i] != QLatin1Char('\n')) ++i;
        }
    }
    return out;
}

}  // namespace

QList<SteamUser> SteamShortcuts::users(const QString& root) {
    QList<SteamUser> out;
    if (root.isEmpty()) return out;
    QHash<QString, QString> login;
    QFile lf(root + QStringLiteral("/config/loginusers.vdf"));
    if (lf.open(QIODevice::ReadOnly)) login = parseTextVdf(QString::fromUtf8(lf.readAll()));
    const QDir ud(root + QStringLiteral("/userdata"));
    for (const QString& id : ud.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const qulonglong acc = id.toULongLong(&ok);
        if (!ok || acc == 0) continue;  // 0 = anoniem
        SteamUser u;
        u.accountId = id;
        const QString sid = QString::number(acc + 76561197960265728ULL);
        u.name = login.value(QStringLiteral("users/%1/PersonaName").arg(sid));
        u.mostRecent = login.value(QStringLiteral("users/%1/MostRecent").arg(sid)) == QStringLiteral("1");
        out << u;
    }
    std::stable_sort(out.begin(), out.end(), [&root](const SteamUser& a, const SteamUser& b) {
        if (a.mostRecent != b.mostRecent) return a.mostRecent;
        // Anders: de map met de laatst gewijzigde config/ eerst.
        const QDateTime ta = QFileInfo(root + QStringLiteral("/userdata/") + a.accountId + QStringLiteral("/config")).lastModified();
        const QDateTime tb = QFileInfo(root + QStringLiteral("/userdata/") + b.accountId + QStringLiteral("/config")).lastModified();
        return ta > tb;
    });
    return out;
}

bool SteamShortcuts::steamRunning(const QString& procDir) {
    const QDir d(procDir);
    for (const QString& pid : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        pid.toInt(&ok);
        if (!ok) continue;
        QFile f(procDir + QLatin1Char('/') + pid + QStringLiteral("/comm"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        if (f.readAll().trimmed() == "steam") return true;
    }
    return false;
}

quint32 SteamShortcuts::crc32(const QByteArray& data) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (char ch : data) c = table[(c ^ static_cast<quint8>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

QString SteamShortcuts::quoted(const QString& path) { return QLatin1Char('"') + path + QLatin1Char('"'); }

quint32 SteamShortcuts::shortcutId(const QString& exe, const QString& appName) {
    return crc32((quoted(exe) + appName).toUtf8()) | 0x80000000u;
}

namespace {

QString unquote(QString s) {
    s = s.trimmed();
    if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))) s = s.mid(1, s.size() - 2);
    return s;
}

}  // namespace

QList<SteamShortcuts::Entry> SteamShortcuts::list(const QString& vdfPath, QString* err) {
    QList<Entry> out;
    QFile f(vdfPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return out;
    }
    VdfNode root;
    if (!vdf::parseBinary(f.readAll(), &root, err)) return out;
    const VdfNode* sc = root.child("shortcuts");
    if (!sc) return out;
    for (const VdfNode& e : sc->children) {
        if (e.type != VdfNode::Map) continue;
        Entry x;
        auto txt = [&e](const char* k) {
            const VdfNode* c = e.child(k);
            return c ? c->text() : QString();
        };
        x.appName = txt("appname");
        x.exe = txt("exe");
        x.startDir = txt("StartDir");
        x.icon = txt("icon");
        x.launchOptions = txt("LaunchOptions");
        if (const VdfNode* a = e.child("appid")) x.appId = static_cast<quint32>(a->toInt32());
        out << x;
    }
    return out;
}

ShortcutResult SteamShortcuts::addOrUpdate(const QString& vdfPath, const ShortcutSpec& spec) {
    ShortcutResult res;
    if (spec.exe.isEmpty() || !QDir::isAbsolutePath(spec.exe)) {
        res.error = QStringLiteral("The program path is not absolute.");
        return res;
    }
    VdfNode root;
    root.type = VdfNode::Map;
    const bool exists = QFileInfo::exists(vdfPath);
    if (exists) {
        QFile f(vdfPath);
        if (!f.open(QIODevice::ReadOnly)) {
            res.error = QStringLiteral("Cannot read %1: %2").arg(vdfPath, f.errorString());
            return res;
        }
        const QByteArray data = f.readAll();
        QString perr;
        if (!data.isEmpty() && !vdf::parseBinary(data, &root, &perr)) {
            res.error = QStringLiteral("%1 could not be read (%2). Nothing was changed.").arg(vdfPath, perr);
            return res;
        }
    }
    VdfNode* sc = root.child("shortcuts");
    if (!sc) {
        root.children << VdfNode::map("shortcuts");
        sc = &root.children.last();
    }
    // Schrijfwijze van bestaande items volgen (nieuw Steam: appname/exe, oud: AppName/Exe).
    QByteArray kName = "appname", kExe = "exe";
    VdfNode* entry = nullptr;
    int nextIndex = 0;
    const QString exeQ = quoted(spec.exe);
    for (VdfNode& e : sc->children) {
        bool ok = false;
        const int idx = e.key.toInt(&ok);
        if (ok) nextIndex = std::max(nextIndex, idx + 1);
        if (e.type != VdfNode::Map) continue;
        if (const VdfNode* n = e.child("appname")) kName = n->key;
        if (const VdfNode* x = e.child("exe")) kExe = x->key;
        const VdfNode* n = e.child("appname");
        const VdfNode* x = e.child("exe");
        if ((n && n->text() == spec.appName) || (x && unquote(x->text()) == spec.exe)) entry = &e;
    }
    res.updated = entry != nullptr;
    if (!entry) {
        sc->children << VdfNode::map(QByteArray::number(nextIndex));
        entry = &sc->children.last();
    }
    res.appId = shortcutId(spec.exe, spec.appName);
    entry->set(VdfNode::int32("appid", static_cast<qint32>(res.appId)));
    entry->set(VdfNode::string(kName, spec.appName));
    entry->set(VdfNode::string(kExe, exeQ));
    entry->set(VdfNode::string("StartDir", quoted(spec.startDir)));
    entry->set(VdfNode::string("icon", spec.icon));
    if (!entry->child("ShortcutPath")) entry->set(VdfNode::string("ShortcutPath", QString()));
    entry->set(VdfNode::string("LaunchOptions", spec.launchOptions));
    // Standaardwaarden alleen bij een nieuw item; bij bijwerken blijven de keuzes van de gebruiker in Steam staan.
    if (!res.updated) {
        entry->set(VdfNode::int32("IsHidden", 0));
        entry->set(VdfNode::int32("AllowDesktopConfig", 1));
        entry->set(VdfNode::int32("AllowOverlay", 1));
        entry->set(VdfNode::int32("OpenVR", 0));
        entry->set(VdfNode::int32("Devkit", 0));
        entry->set(VdfNode::string("DevkitGameID", QString()));
        entry->set(VdfNode::int32("DevkitOverrideAppID", 0));
        entry->set(VdfNode::int32("LastPlayTime", 0));
        entry->set(VdfNode::string("FlatpakAppID", QString()));
        entry->set(VdfNode::string("sortas", QString()));
        entry->set(VdfNode::map("tags"));
    }
    const QByteArray out = vdf::writeBinary(root);
    // Controle vóór het schrijven: wat we schrijven moet weer te lezen zijn.
    VdfNode check;
    QString cerr;
    if (!vdf::parseBinary(out, &check, &cerr)) {
        res.error = QStringLiteral("Internal error: the new file would not be readable (%1). Nothing was changed.").arg(cerr);
        return res;
    }
    if (exists) {
        const QString base = vdfPath + QStringLiteral(".ravengun-bak-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        res.backup = base;
        for (int n = 2; QFileInfo::exists(res.backup); ++n) res.backup = base + QLatin1Char('-') + QString::number(n);
        if (!QFile::copy(vdfPath, res.backup)) {
            res.error = QStringLiteral("Could not make a backup (%1). Nothing was changed.").arg(res.backup);
            res.backup.clear();
            return res;
        }
    }
    QString werr;
    if (!writeTextAtomic(vdfPath, out, &werr)) {
        res.error = QStringLiteral("Could not write %1: %2").arg(vdfPath, werr);
        return res;
    }
    res.ok = true;
    return res;
}

QStringList SteamShortcuts::writeGrid(const QString& gridDir, quint32 appId, const QImage& portrait, const QImage& landscape,
                                      const QImage& hero, const QImage& logo, const QImage& icon, QString* err) {
    QStringList written;
    if (!QDir().mkpath(gridDir)) {
        if (err) *err = QStringLiteral("Cannot create %1").arg(gridDir);
        return written;
    }
    const QString id = QString::number(appId);
    const QList<QPair<QString, QImage>> files{{id + QStringLiteral("p.png"), portrait},
                                              {id + QStringLiteral(".png"), landscape},
                                              {id + QStringLiteral("_hero.png"), hero},
                                              {id + QStringLiteral("_logo.png"), logo},
                                              {id + QStringLiteral("_icon.png"), icon}};
    for (const auto& [name, img] : files) {
        if (img.isNull()) continue;
        const QString path = gridDir + QLatin1Char('/') + name;
        if (QFileInfo::exists(path) && !QFileInfo::exists(path + QStringLiteral(".ravengun-bak"))) QFile::copy(path, path + QStringLiteral(".ravengun-bak"));
        if (img.save(path, "PNG")) written << path;
        else if (err) *err = QStringLiteral("Cannot write %1").arg(path);
    }
    return written;
}

// --- Afbeeldingen -----------------------------------------------------------------------------------------------------

namespace artwork {

namespace {

const QColor kRed(0xc0, 0x26, 0x26);

void crosshair(QPainter& p, QPointF c, double r) {
    QPen pen(kRed);
    pen.setWidthF(std::max(2.0, r * 0.12));
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r, r);
    p.drawEllipse(c, r * 0.12, r * 0.12);
    p.drawLine(QPointF(c.x(), c.y() - r * 1.45), QPointF(c.x(), c.y() - r * 0.45));
    p.drawLine(QPointF(c.x(), c.y() + r * 0.45), QPointF(c.x(), c.y() + r * 1.45));
    p.drawLine(QPointF(c.x() - r * 1.45, c.y()), QPointF(c.x() - r * 0.45, c.y()));
    p.drawLine(QPointF(c.x() + r * 0.45, c.y()), QPointF(c.x() + r * 1.45, c.y()));
}

void title(QPainter& p, const QRectF& r, double px, bool subtitle) {
    QFont f;
    f.setBold(true);
    f.setPixelSize(static_cast<int>(px));
    f.setLetterSpacing(QFont::PercentageSpacing, 104);
    p.setFont(f);
    p.setPen(QColor(0xee, 0xee, 0xee));
    QRectF tr = r;
    if (subtitle) tr.setBottom(r.center().y() + px * 0.55);
    p.drawText(tr, Qt::AlignHCenter | (subtitle ? Qt::AlignBottom : Qt::AlignVCenter), QStringLiteral("RavenGun"));
    if (subtitle) {
        QFont s;
        s.setPixelSize(static_cast<int>(px * 0.32));
        s.setLetterSpacing(QFont::PercentageSpacing, 120);
        p.setFont(s);
        p.setPen(QColor(0x9a, 0x9a, 0x9a));
        QRectF sr = r;
        sr.setTop(r.center().y() + px * 0.75);
        p.drawText(sr, Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("LIGHTGUN ARCADE"));
    }
}

QImage canvas(int w, int h, bool transparent = false) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(transparent ? Qt::transparent : Qt::black);
    return img;
}

}  // namespace

QImage portrait() {
    QImage img = canvas(600, 900);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QRadialGradient g(QPointF(300, 330), 360);
    g.setColorAt(0, QColor(0x3a, 0x08, 0x08));
    g.setColorAt(1, Qt::black);
    p.fillRect(img.rect(), g);
    crosshair(p, QPointF(300, 330), 120);
    title(p, QRectF(0, 520, 600, 260), 92, true);
    return img;
}

QImage landscape() {
    QImage img = canvas(920, 430);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QRadialGradient g(QPointF(220, 215), 260);
    g.setColorAt(0, QColor(0x3a, 0x08, 0x08));
    g.setColorAt(1, Qt::black);
    p.fillRect(img.rect(), g);
    crosshair(p, QPointF(220, 215), 92);
    title(p, QRectF(380, 60, 500, 300), 96, true);
    return img;
}

QImage hero() {
    QImage img = canvas(1920, 620);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QRadialGradient g(QPointF(1500, 310), 700);
    g.setColorAt(0, QColor(0x30, 0x06, 0x06));
    g.setColorAt(1, Qt::black);
    p.fillRect(img.rect(), g);
    p.setOpacity(0.55);
    crosshair(p, QPointF(1500, 310), 170);
    return img;
}

QImage logo() {
    QImage img = canvas(800, 260, true);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    crosshair(p, QPointF(120, 130), 70);
    title(p, QRectF(220, 0, 580, 260), 118, false);
    return img;
}

QImage icon(int size) {
    QImage img = canvas(size, size, true);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x14, 0x14, 0x14));
    p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.18, size * 0.18);
    crosshair(p, QPointF(size / 2.0, size / 2.0), size * 0.25);
    return img;
}

}  // namespace artwork

}  // namespace rg
