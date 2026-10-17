// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Components.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <functional>

#include "core/AppPaths.h"
#include "ravengun/gamesettings/gamesettings.hpp"

namespace rg {
namespace {

namespace gs = ravengun::gamesettings;

bool safeRel(const QString& p) {
    if (p.startsWith(QLatin1Char('/'))) return false;
    return !p.split(QLatin1Char('/')).contains(QStringLiteral(".."));
}

bool isHex64(const QString& s) {
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}$"));
    return re.match(s).hasMatch();
}

QStringList strList(const QJsonValue& v) {
    QStringList out;
    for (const QJsonValue& x : v.toArray()) out << x.toString();
    return out;
}

QList<QPair<QString, QString>> pairList(const QJsonValue& v, const char* a, const char* b) {
    QList<QPair<QString, QString>> out;
    for (const QJsonValue& x : v.toArray()) out.append({x.toObject().value(QLatin1String(a)).toString(), x.toObject().value(QLatin1String(b)).toString()});
    return out;
}

bool exists(const QString& path) {
    const QFileInfo fi(path);
    return fi.exists() || fi.isSymLink();
}

QString safeName(const QString& id) {
    QString s = id;
    s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return s;
}

}  // namespace

ComponentEnv ComponentEnv::standard() {
    ComponentEnv e;
    e.dataDir = AppPaths::dataDir();
    const QString root = AppPaths::dataRoot();
    if (!root.isEmpty()) e.bundledRoot = root + QStringLiteral("/components");
    return e;
}

QList<Component> Components::parse(const QByteArray& json, QString* err) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    auto fail = [&](const QString& m) { if (err) *err = m; return QList<Component>(); };
    if (pe.error != QJsonParseError::NoError) return fail(QStringLiteral("components.json: %1").arg(pe.errorString()));
    if (doc.object().value(QStringLiteral("schema")).toInt() != 1) return fail(QStringLiteral("components.json: unknown schema"));
    static const QSet<QString> kinds{QStringLiteral("download"), QStringLiteral("bundled"), QStringLiteral("userfolder"),
                                     QStringLiteral("steam"), QStringLiteral("wine")};
    static const QSet<QString> archives{QStringLiteral("tar.gz"), QStringLiteral("tar.xz"), QStringLiteral("zip"),
                                        QStringLiteral("pkg.tar.zst"), QStringLiteral("deb"), QStringLiteral("cab"), QStringLiteral("file")};
    QList<Component> out;
    QSet<QString> ids;
    for (const QJsonValue& v : doc.object().value(QStringLiteral("components")).toArray()) {
        const QJsonObject o = v.toObject();
        Component c;
        c.id = o.value(QStringLiteral("id")).toString();
        c.name = o.value(QStringLiteral("name")).toString();
        c.description = o.value(QStringLiteral("description")).toString();
        c.version = o.value(QStringLiteral("version")).toString();
        c.group = o.value(QStringLiteral("group")).toString();
        c.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("download"));
        c.recipe = o.value(QStringLiteral("recipe")).toString(QStringLiteral("extract"));
        c.dest = o.value(QStringLiteral("dest")).toString();
        c.detect = strList(o.value(QStringLiteral("detect")));
        c.strip = o.value(QStringLiteral("strip")).toInt();
        c.subdir = o.value(QStringLiteral("subdir")).toString();
        c.include = strList(o.value(QStringLiteral("include")));
        c.map = pairList(o.value(QStringLiteral("map")), "from", "to");
        c.merge = o.value(QStringLiteral("merge")).toBool();
        c.links = pairList(o.value(QStringLiteral("links")), "path", "target");
        c.writeFiles = pairList(o.value(QStringLiteral("writeFiles")), "path", "text");
        c.dependsOn = strList(o.value(QStringLiteral("dependsOn")));
        c.treeSha256 = o.value(QStringLiteral("treeSha256")).toString();
        c.sourceNames = strList(o.value(QStringLiteral("sourceNames")));
        c.sourceRequire = strList(o.value(QStringLiteral("sourceRequire")));
        c.exclude = strList(o.value(QStringLiteral("exclude")));
        c.note = o.value(QStringLiteral("note")).toString();
        c.optional = o.value(QStringLiteral("optional")).toBool();
        for (const QJsonValue& dv : o.value(QStringLiteral("downloads")).toArray()) {
            const QJsonObject d = dv.toObject();
            ComponentDownload dl;
            dl.name = d.value(QStringLiteral("name")).toString();
            dl.url = d.value(QStringLiteral("url")).toString();
            dl.sha256 = d.value(QStringLiteral("sha256")).toString().toLower();
            dl.size = static_cast<qint64>(d.value(QStringLiteral("size")).toDouble());
            dl.archive = d.value(QStringLiteral("archive")).toString();
            if (dl.name.isEmpty() || dl.name.contains(QLatin1Char('/'))) return fail(QStringLiteral("%1: bad download name").arg(c.id));
            if (!dl.url.startsWith(QLatin1String("https://")) && !dl.url.startsWith(QLatin1String("file://")))
                return fail(QStringLiteral("%1: download url must be https").arg(c.id));
            if (!isHex64(dl.sha256)) return fail(QStringLiteral("%1: download %2 needs a sha256").arg(c.id, dl.name));
            if (!archives.contains(dl.archive)) return fail(QStringLiteral("%1: unknown archive type '%2'").arg(c.id, dl.archive));
            c.downloads << dl;
        }
        if (c.id.isEmpty() || c.name.isEmpty()) return fail(QStringLiteral("components.json: component without id or name"));
        if (ids.contains(c.id)) return fail(QStringLiteral("components.json: duplicate id %1").arg(c.id));
        ids.insert(c.id);
        if (!kinds.contains(c.kind)) return fail(QStringLiteral("%1: unknown kind '%2'").arg(c.id, c.kind));
        if (c.kind == QLatin1String("download") && c.downloads.isEmpty()) return fail(QStringLiteral("%1: no downloads").arg(c.id));
        if (c.detect.isEmpty()) return fail(QStringLiteral("%1: no detect paths").arg(c.id));
        QStringList paths = c.detect + c.include;
        paths << c.dest << c.subdir;
        for (const auto& p : c.links) paths << p.first;
        for (const auto& p : c.writeFiles) paths << p.first;
        for (const auto& p : c.map) paths << p.second;
        for (const QString& p : paths)
            if (!safeRel(p)) return fail(QStringLiteral("%1: unsafe path '%2'").arg(c.id, p));
        out << c;
    }
    for (const Component& c : out)
        for (const QString& d : c.dependsOn)
            if (!ids.contains(d)) return fail(QStringLiteral("%1: unknown dependency %2").arg(c.id, d));
    return out;
}

QList<Component> Components::catalogue(QString* err) {
    const QString file = AppPaths::packagingFile(QStringLiteral("components.json"));
    if (file.isEmpty()) { if (err) *err = QStringLiteral("components.json not found"); return {}; }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = QStringLiteral("cannot read %1").arg(file); return {}; }
    return parse(f.readAll(), err);
}

QList<Component> Components::modComponents(const gs::Dirs& dirs) {
    QList<Component> out;
    for (const std::string& game : gs::list_games(dirs)) {
        const gs::Game g = gs::load_game(dirs, game);
        for (const gs::Section& s : g.settings.sections) {
            if (s.name.rfind("Mod ", 0) != 0) continue;
            const QString name = QString::fromStdString(s.name.substr(4)).trimmed();
            const QString url = QString::fromStdString(g.get(s.name, "Url")).trimmed();
            const QString root = QString::fromStdString(g.get(s.name, "Root")).trimmed();
            const QString tree = QString::fromStdString(g.get(s.name, "TreeSha256")).trimmed().toLower();
            if (name.isEmpty() || !url.startsWith(QLatin1String("https://")) || root.isEmpty() || !isHex64(tree)) continue;
            const QString gname = QString::fromStdString(game);
            if (!safeRel(root) || !safeRel(gname) || !safeRel(name) || name.contains(QLatin1Char('/'))) continue;
            Component c;
            c.id = QStringLiteral("mod:%1/%2").arg(gname, name);
            c.name = QStringLiteral("%1: %2").arg(gname, QString::fromStdString(g.get(s.name, "Title", name.toStdString())));
            c.description = QStringLiteral("Mod for %1 (from its game.ini).").arg(gname);
            c.group = QStringLiteral("Mods");
            c.version = tree.left(12);
            c.kind = QStringLiteral("download");
            c.dest = QStringLiteral("mods/%1/%2/files").arg(gname, name);
            c.detect = {c.dest, QStringLiteral("mods/%1/%2/tree-sha256").arg(gname, name)};
            c.strip = 1;
            c.subdir = root;
            c.treeSha256 = tree;
            ComponentDownload d;
            d.name = QStringLiteral("mod-%1-%2.tar.gz").arg(safeName(gname), safeName(name));
            d.url = url;
            d.archive = QStringLiteral("tar.gz");
            c.downloads << d;
            c.writeFiles = {{QStringLiteral("mods/%1/%2/tree-sha256").arg(gname, name), tree + QLatin1Char('\n')}};
            out << c;
        }
    }
    return out;
}

QList<Component> Components::modComponents() { return modComponents(gs::default_dirs(AppPaths::dataRoot().toStdString())); }

Component* Components::find(QList<Component>& all, const QString& id) {
    for (Component& c : all)
        if (c.id == id) return &c;
    return nullptr;
}

QString Components::stampPath(const ComponentEnv& env, const QString& id) {
    return env.dataDir + QStringLiteral("/components/") + safeName(id) + QStringLiteral(".json");
}

QString Components::stampVersion(const ComponentEnv& env, const QString& id) {
    QFile f(stampPath(env, id));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("version")).toString();
}

bool Components::writeStamp(const ComponentEnv& env, const Component& c, QString* err) {
    QDir().mkpath(env.dataDir + QStringLiteral("/components"));
    QSaveFile f(stampPath(env, c.id));
    QJsonArray hashes;
    for (const auto& d : c.downloads) hashes.append(d.sha256);
    const QJsonObject o{{QStringLiteral("id"), c.id}, {QStringLiteral("version"), c.version}, {QStringLiteral("sha256"), hashes}};
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson()) < 0 || !f.commit()) {
        if (err) *err = QStringLiteral("cannot write %1").arg(f.fileName());
        return false;
    }
    return true;
}

QString Components::sha256File(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return QString::fromLatin1(h.result().toHex());
}

QString Components::treeSha256(const QString& root) {
    QList<QByteArray> rels;
    const QDir base(root);
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) rels << (QByteArrayLiteral("./") + base.relativeFilePath(it.next()).toUtf8());
    std::sort(rels.begin(), rels.end());
    QCryptographicHash all(QCryptographicHash::Sha256);
    for (const QByteArray& rel : rels) {
        const QString h = sha256File(root + QLatin1Char('/') + QString::fromUtf8(rel.mid(2)));
        all.addData((h + QStringLiteral("  ") + QString::fromUtf8(rel) + QLatin1Char('\n')).toUtf8());
    }
    return QString::fromLatin1(all.result().toHex());
}

QStringList Components::steamLibraries(const ComponentEnv& env) {
    QStringList libs;
    const QString steam = env.steamRoot.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share/Steam") : env.steamRoot;
    libs << steam;
    QFile vdf(steam + QStringLiteral("/steamapps/libraryfolders.vdf"));
    if (vdf.open(QIODevice::ReadOnly)) {
        static const QRegularExpression re(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
        auto it = re.globalMatch(QString::fromUtf8(vdf.readAll()));
        while (it.hasNext()) libs << it.next().captured(1);
    }
    if (env.steamRoot.isEmpty()) {
        const QDir media(QStringLiteral("/run/media"));
        for (const QString& u : media.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            for (const QString& m : QDir(media.filePath(u)).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                libs << media.filePath(u) + QLatin1Char('/') + m + QStringLiteral("/SteamLibrary");
    }
    libs.removeDuplicates();
    QStringList out;
    for (const QString& l : libs)
        if (QFileInfo(l + QStringLiteral("/steamapps/common")).isDir()) out << l + QStringLiteral("/steamapps/common");
    return out;
}

// Mappen onder `base` die bij `pattern` passen. Elk onderdeel van het patroon mag een * hebben ("TeknoParrotUi-*/ElfLdr2/libs");
// de uitkomst loopt van de laatste (hoogste) naam naar de eerste.
static QStringList expandDirGlob(const QString& base, const QString& pattern) {
    QStringList cur{base};
    for (const QString& part : pattern.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        QStringList next;
        for (const QString& d : cur)
            for (const QString& h : QDir(d).entryList({part}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) next << d + QLatin1Char('/') + h;
        cur = next;
    }
    return cur;
}

QString Components::findUserFolderSource(const Component& c, const ComponentEnv& env) {
    QStringList places;
    for (const QString& root : env.sourceRoots) {
        places << root;
        const QDir d(root);
        for (const QString& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) places << d.filePath(sub);
        places << QFileInfo(root).dir().absolutePath();   // naast de gamemap (bv. .../Teknoparrot/<game> en .../Teknoparrot/TeknoParrotUi-*)
    }
    places.removeDuplicates();
    for (const QString& place : places) {
        for (const QString& n : c.sourceNames) {
            for (const QString& dir : expandDirGlob(place, n)) {
                bool ok = true;
                for (const QString& r : c.sourceRequire) ok = ok && exists(dir + QLatin1Char('/') + r);
                if (ok) return dir;
            }
        }
    }
    return {};
}

QString Components::formatSize(qint64 b) {
    if (b <= 0) return QStringLiteral("-");
    if (b >= (1 << 30)) return QStringLiteral("%1 GB").arg(b / double(1 << 30), 0, 'f', 1);
    if (b >= (1 << 20)) return QStringLiteral("%1 MB").arg(b / double(1 << 20), 0, 'f', 1);
    return QStringLiteral("%1 kB").arg(qMax<qint64>(1, b / 1024));
}

ComponentStatus Components::status(const Component& c, const ComponentEnv& env) {
    ComponentStatus st;
    bool present = true;
    for (const QString& d : c.detect) present = present && exists(env.path(d));
    if (present && !c.treeSha256.isEmpty()) {
        QFile f(env.path(c.detect.value(1)));
        const QString have = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
        if (have != c.treeSha256) {
            st.state = ComponentState::Outdated;
            st.detail = QStringLiteral("installed mod differs from the one this version expects");
            return st;
        }
    }
    if (present) {
        const QString ver = stampVersion(env, c.id);
        if (!ver.isEmpty() && ver != c.version) {
            st.state = ComponentState::Outdated;
            st.detail = QStringLiteral("installed %1, this version wants %2").arg(ver, c.version);
        } else {
            st.state = ComponentState::Installed;
            st.detail = ver.isEmpty() ? QStringLiteral("found in the data folder") : QStringLiteral("installed by RavenGun");
        }
        return st;
    }
    st.state = ComponentState::Missing;
    if (c.kind == QLatin1String("bundled")) {
        if (env.bundledRoot.isEmpty() || !QFileInfo(env.bundledRoot + QLatin1Char('/') + c.id).isDir()) {
            st.state = ComponentState::Unavailable;
            st.detail = QStringLiteral("not included in this build of RavenGun");
        }
    } else if (c.kind == QLatin1String("userfolder")) {
        st.source = findUserFolderSource(c, env);
        if (st.source.isEmpty()) {
            st.state = ComponentState::Unavailable;
            st.detail = c.note.isEmpty() ? QStringLiteral("not found in your game folders; add the folder that contains it in the Games step") : c.note;
        } else {
            st.detail = QStringLiteral("found at %1").arg(st.source);
        }
    } else if (c.kind == QLatin1String("steam")) {
        QString why;
        if (c.recipe == QLatin1String("redist-directx") && findRedistDir(env).isEmpty()) why = QStringLiteral("DirectX redistributable not found in Steam (Steamworks Shared)");
        if (c.recipe == QLatin1String("redist-vc2010") && findVcredistDir(env).isEmpty()) why = QStringLiteral("Visual C++ 2010 redistributable not found in Steam (Steamworks Shared)");
        if (c.recipe == QLatin1String("gst32") && findProtonGst32(env).isEmpty()) why = QStringLiteral("no Proton with 32-bit GStreamer found in Steam; install Proton or GE-Proton");
        if (c.recipe == QLatin1String("lib32-sniper") && findSniperLib32(env).isEmpty()) why = QStringLiteral("Steam Linux Runtime (sniper) not found in Steam");
        if (!why.isEmpty()) {
            st.state = ComponentState::Unavailable;
            st.detail = why;
        }
    }
    return st;
}

QString Components::findRedistDir(const ComponentEnv& env) {
    for (const QString& lib : steamLibraries(env)) {
        const QString d = lib + QStringLiteral("/Steamworks Shared/_CommonRedist/DirectX/Jun2010");
        if (!QDir(d).entryList({QStringLiteral("*_x86.cab")}, QDir::Files).isEmpty()) return d;
    }
    return {};
}

QString Components::findVcredistDir(const ComponentEnv& env) {
    for (const QString& lib : steamLibraries(env)) {
        const QString d = lib + QStringLiteral("/Steamworks Shared/_CommonRedist/vcredist/2010");
        if (QFileInfo::exists(d + QStringLiteral("/vcredist_x64.exe")) || QFileInfo::exists(d + QStringLiteral("/vcredist_x86.exe"))) return d;
    }
    return {};
}

static QString steamRootOf(const ComponentEnv& env) {
    return env.steamRoot.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share/Steam") : env.steamRoot;
}

QString Components::findProtonGst32(const ComponentEnv& env) {
    const QString steam = steamRootOf(env);
    QStringList cand;
    auto sortedDirs = [](const QString& parent, const QString& glob) {
        QStringList l = QDir(parent).entryList({glob}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        QCollator col;
        col.setNumericMode(true);
        std::sort(l.begin(), l.end(), [&col](const QString& a, const QString& b) { return col.compare(a, b) > 0; });
        return l;
    };
    const QString sub = QStringLiteral("/files/lib/i386-linux-gnu");
    for (const QString& n : sortedDirs(steam + QStringLiteral("/compatibilitytools.d"), QStringLiteral("GE-Proton*")))
        cand << steam + QStringLiteral("/compatibilitytools.d/") + n + sub;
    for (const QString& lib : steamLibraries(env)) {
        cand << lib + QStringLiteral("/Proton - Experimental") + sub;
        for (const QString& n : sortedDirs(lib, QStringLiteral("Proton *"))) cand << lib + QLatin1Char('/') + n + sub;
    }
    for (const QString& d : cand)
        if (QFileInfo::exists(d + QStringLiteral("/gstreamer-1.0/libgstlibav.so")) && QFileInfo::exists(d + QStringLiteral("/libgstreamer-1.0.so.0"))) return d;
    return {};
}

QString Components::findSniperLib32(const ComponentEnv& env) {
    for (const QString& lib : steamLibraries(env)) {
        const QDir sn(lib + QStringLiteral("/SteamLinuxRuntime_sniper/var"));
        for (const QString& t : sn.entryList({QStringLiteral("tmp-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString d = sn.filePath(t) + QStringLiteral("/usr/lib/i386-linux-gnu");
            if (QFileInfo::exists(d + QStringLiteral("/libglut.so.3"))) return d;
        }
    }
    return {};
}

QString Components::findSniperPlatformLib32(const ComponentEnv& env) {
    for (const QString& lib : steamLibraries(env)) {
        const QDir sn(lib + QStringLiteral("/SteamLinuxRuntime_sniper"));
        QStringList l = sn.entryList({QStringLiteral("sniper_platform_*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (!l.isEmpty()) {
            const QString d = sn.filePath(l.last()) + QStringLiteral("/files/lib/i386-linux-gnu");
            if (QFileInfo(d).isDir()) return d;
        }
    }
    return {};
}

QList<Component> Components::withDependencies(const QList<Component>& all, const QStringList& ids) {
    QList<Component> out;
    QSet<QString> seen;
    std::function<void(const QString&)> add = [&](const QString& id) {
        if (seen.contains(id)) return;
        seen.insert(id);
        for (const Component& c : all) {
            if (c.id != id) continue;
            for (const QString& d : c.dependsOn) add(d);
            out << c;
            return;
        }
    };
    for (const QString& id : ids) add(id);
    return out;
}

}  // namespace rg
