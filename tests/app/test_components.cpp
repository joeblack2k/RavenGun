// SPDX-License-Identifier: GPL-3.0-or-later
// Tests van de onderdelen (core/Components, core/ArchiveExtract, core/ComponentInstaller): catalogus, standen op een nep-datamap,
// sha256, uitpakken van kleine testarchieven, staging en hernoemen, het downloaden via file://, Steam- en Wine-recepten met nepbestanden.
// Niets raakt de echte gebruikersmap: alles staat in een QTemporaryDir.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "core/ArchiveExtract.h"
#include "core/ComponentInstaller.h"
#include "core/Components.h"

using namespace rg;

namespace {

QString arch(const QString& name) { return QStringLiteral(RAVENGUN_TEST_DATA "/components/") + name; }

void writeFile(const QString& path, const QByteArray& data, bool exec = false) {
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
    if (exec) f.setPermissions(f.permissions() | QFile::ExeOwner);
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

Component simple(const QString& id, const QString& archiveName, const QString& type, const QString& dest) {
    Component c;
    c.id = id;
    c.name = id;
    c.version = QStringLiteral("1");
    c.kind = QStringLiteral("download");
    c.recipe = QStringLiteral("extract");
    c.dest = dest;
    ComponentDownload d;
    d.name = archiveName;
    d.url = QUrl::fromLocalFile(arch(archiveName)).toString();
    d.archive = type;
    d.sha256 = Components::sha256File(arch(archiveName));
    c.downloads << d;
    c.strip = 1;
    c.detect = {dest + QStringLiteral("/lib/data.txt")};
    return c;
}

}  // namespace

class ComponentsTest : public QObject {
    Q_OBJECT
    QTemporaryDir tmp_;
    ComponentEnv env() const {
        ComponentEnv e;
        e.dataDir = tmp_.path() + QStringLiteral("/data");
        e.steamRoot = tmp_.path() + QStringLiteral("/steam");
        return e;
    }
    void resetData() { QDir(tmp_.path()).removeRecursively(); QDir().mkpath(tmp_.path()); }

private slots:
    void init() { resetData(); }

    void catalogueParses() {
        QFile f(QStringLiteral(RAVENGUN_SOURCE_DIR "/packaging/components.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QString err;
        const QList<Component> all = Components::parse(f.readAll(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(all.size() >= 20);
        QSet<QString> ids;
        for (const Component& c : all) {
            QVERIFY(!ids.contains(c.id));
            ids.insert(c.id);
            for (const auto& d : c.downloads) {
                QCOMPARE(d.sha256.size(), 64);
                QVERIFY(d.url.startsWith(QStringLiteral("https://")));
                QVERIFY(d.size > 0);
            }
        }
        for (const char* id : {"wine", "dxvk", "bepinex-x64", "bepinex-x86", "physx", "redist-directx", "gst32", "gst-wmv", "teknoparrot-runtime", "openparrot"})
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
        Component* wine = Components::find(const_cast<QList<Component>&>(all), QStringLiteral("wine"));
        QVERIFY(wine);
        QCOMPARE(wine->downloads.first().sha256, QStringLiteral("51e1b54068c242c0a59c329674afe4b0ae03d313516a535be4acd1c5f3277e9b"));
        QCOMPARE(wine->detect.first(), QStringLiteral("wine/proton-11.0-2/bin/wine"));
        // dxvk en bepinex: de pins van scripts/fetch-*.sh
        QCOMPARE(Components::find(const_cast<QList<Component>&>(all), QStringLiteral("dxvk"))->downloads.first().sha256,
                 QStringLiteral("d85ce7c79f57ecd765aaa1b9e7007cb875e6fde9f6d331df799bce73d513ce87"));
    }

    void parseRejectsBadInput() {
        QString err;
        const QByteArray ok = R"({"schema":1,"components":[{"id":"a","name":"A","kind":"download","detect":["x"],
            "downloads":[{"name":"f","url":"https://e/f","sha256":"%1","archive":"zip"}]}]})";
        const QString h64(64, QLatin1Char('a'));
        QCOMPARE(Components::parse(QByteArray(ok).replace("%1", h64.toUtf8()), &err).size(), 1);
        QVERIFY(Components::parse(QByteArray(ok).replace("%1", "abc"), &err).isEmpty());
        QVERIFY(err.contains(QStringLiteral("sha256")));
        QVERIFY(Components::parse(QByteArray(ok).replace("%1", h64.toUtf8()).replace("https://e/f", "http://e/f"), &err).isEmpty());
        QVERIFY(Components::parse(QByteArray(ok).replace("%1", h64.toUtf8()).replace("\"detect\":[\"x\"]", "\"detect\":[\"../x\"]"), &err).isEmpty());
        QVERIFY(err.contains(QStringLiteral("unsafe")));
        QVERIFY(Components::parse(QByteArray(ok).replace("%1", h64.toUtf8()).replace("\"zip\"", "\"rar\""), &err).isEmpty());
        QVERIFY(Components::parse(QByteArray(ok).replace("%1", h64.toUtf8()).replace("\"kind\":\"download\"", "\"kind\":\"download\",\"dependsOn\":[\"zz\"]"), &err).isEmpty());
        QVERIFY(Components::parse("{", &err).isEmpty());
    }

    void sha256AndTree() {
        QCOMPARE(Components::sha256File(arch(QStringLiteral("test.zip"))),
                 QStringLiteral("96c3b9285be91988ce285cb8fdb0c753ea4f447f9f66a1d2b6e73bc42bba0871"));
        QVERIFY(Components::sha256File(tmp_.path() + QStringLiteral("/none")).isEmpty());
        // Zelfde uitkomst als `find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum` (scripts/fetch-mods.py).
        const QString dir = tmp_.path() + QStringLiteral("/tree");
        ExtractOptions o;
        o.strip = 0;
        QString err;
        QVERIFY2(extractArchive(arch(QStringLiteral("test.zip")), dir, o, &err), qPrintable(err));
        QCOMPARE(Components::treeSha256(dir + QStringLiteral("/pkg-1.0")), QStringLiteral("eb7de87e61e4561a9b735df8fb23ef9b8d6ff005e8b3b43bb36e2be677ce79a5"));
    }

    void extractFormats_data() {
        QTest::addColumn<QString>("file");
        QTest::newRow("tar.gz") << "test.tar.gz";
        QTest::newRow("tar.xz") << "test.tar.xz";
        QTest::newRow("zip") << "test.zip";
        QTest::newRow("pkg.tar.zst") << "test.pkg.tar.zst";
    }
    void extractFormats() {
        QFETCH(QString, file);
        const QString dir = tmp_.path() + QStringLiteral("/x");
        ExtractOptions o;
        o.strip = 1;
        qint64 lastDone = -1;
        o.progress = [&](qint64 done, qint64) { lastDone = done; };
        QString err;
        QVERIFY2(extractArchive(arch(file), dir, o, &err), qPrintable(err));
        QCOMPARE(readFile(dir + QStringLiteral("/lib/data.txt")), QByteArray("hello\n"));
        QVERIFY(QFileInfo(dir + QStringLiteral("/bin/tool")).isExecutable());
        QVERIFY(QFileInfo::exists(dir + QStringLiteral("/Unity/Game/plugin.dll")));
        QVERIFY(lastDone > 0);
        QVERIFY(listArchive(arch(file)).size() >= 5);
    }

    void extractFilters() {
        QString err;
        const QString d1 = tmp_.path() + QStringLiteral("/sub");
        ExtractOptions o;
        o.strip = 1;
        o.subdir = QStringLiteral("Unity");
        QVERIFY(extractArchive(arch(QStringLiteral("test.tar.gz")), d1, o, &err));
        QVERIFY(QFileInfo::exists(d1 + QStringLiteral("/Game/plugin.dll")));
        QVERIFY(!QFileInfo::exists(d1 + QStringLiteral("/lib")));

        const QString d2 = tmp_.path() + QStringLiteral("/inc");
        ExtractOptions i;
        i.strip = 1;
        i.include = {QStringLiteral("Unity/"), QStringLiteral("lib/data.txt")};
        QVERIFY(extractArchive(arch(QStringLiteral("test.zip")), d2, i, &err));
        QVERIFY(QFileInfo::exists(d2 + QStringLiteral("/Unity/Game/plugin.dll")));
        QVERIFY(QFileInfo::exists(d2 + QStringLiteral("/lib/data.txt")));
        QVERIFY(!QFileInfo::exists(d2 + QStringLiteral("/Other")));

        const QString d3 = tmp_.path() + QStringLiteral("/map");
        ExtractOptions m;
        m.strip = 1;
        m.map = {{QStringLiteral("lib/data.txt"), QStringLiteral("runtime/lib32/renamed.so.0")}};
        QVERIFY(extractArchive(arch(QStringLiteral("test.pkg.tar.zst")), d3, m, &err));
        QCOMPARE(readFile(d3 + QStringLiteral("/runtime/lib32/renamed.so.0")), QByteArray("hello\n"));
        QVERIFY(!QFileInfo::exists(d3 + QStringLiteral("/bin")));
    }

    void extractRejectsEscapesAndCancel() {
        const QString dir = tmp_.path() + QStringLiteral("/e/inner");
        QString err;
        ExtractOptions o;
        QVERIFY(!extractArchive(arch(QStringLiteral("evil.tar.gz")), dir, o, &err));
        QVERIFY(err.contains(QStringLiteral("unsafe")));
        QVERIFY(!QFileInfo::exists(tmp_.path() + QStringLiteral("/evil.txt")));
        QVERIFY(!extractArchive(arch(QStringLiteral("evil-link.tar.gz")), tmp_.path() + QStringLiteral("/l"), ExtractOptions(), &err));
        QVERIFY(err.contains(QStringLiteral("symlink points outside")));
        std::atomic_bool stop{true};
        ExtractOptions c;
        c.cancel = &stop;
        QVERIFY(!extractArchive(arch(QStringLiteral("test.tar.gz")), tmp_.path() + QStringLiteral("/c"), c, &err));
        QCOMPARE(err, QStringLiteral("cancelled"));
        QVERIFY(!extractArchive(tmp_.path() + QStringLiteral("/missing.zip"), tmp_.path() + QStringLiteral("/c2"), ExtractOptions(), &err));
    }

    void stateDetection() {
        const ComponentEnv e = env();
        Component c = simple(QStringLiteral("t"), QStringLiteral("test.tar.gz"), QStringLiteral("tar.gz"), QStringLiteral("opt/t"));
        QCOMPARE(Components::status(c, e).state, ComponentState::Missing);
        writeFile(e.path(c.detect.first()), "x");
        auto st = Components::status(c, e);
        QCOMPARE(st.state, ComponentState::Installed);   // met de hand klaargezet: geen stempel nodig
        QVERIFY(Components::writeStamp(e, c));
        QCOMPARE(Components::status(c, e).state, ComponentState::Installed);
        c.version = QStringLiteral("2");
        st = Components::status(c, e);
        QCOMPARE(st.state, ComponentState::Outdated);
        QVERIFY(st.detail.contains(QStringLiteral("installed 1")));
        // een mod met een andere boom is verouderd
        Component mod = c;
        mod.version = QStringLiteral("1");
        mod.treeSha256 = QStringLiteral("aa");
        mod.detect = {QStringLiteral("opt/t/lib"), QStringLiteral("opt/t/tree")};
        writeFile(e.path(QStringLiteral("opt/t/tree")), "bb\n");
        QCOMPARE(Components::status(mod, e).state, ComponentState::Outdated);
        writeFile(e.path(QStringLiteral("opt/t/tree")), "aa\n");
        QCOMPARE(Components::status(mod, e).state, ComponentState::Installed);
        // bundled zonder bron en Steam zonder bestanden: niet beschikbaar
        Component b;
        b.id = QStringLiteral("b");
        b.kind = QStringLiteral("bundled");
        b.detect = {QStringLiteral("nothing")};
        QCOMPARE(Components::status(b, e).state, ComponentState::Unavailable);
        Component s = b;
        s.kind = QStringLiteral("steam");
        s.recipe = QStringLiteral("redist-directx");
        QCOMPARE(Components::status(s, e).state, ComponentState::Unavailable);
        s.recipe = QStringLiteral("gst32");
        QCOMPARE(Components::status(s, e).state, ComponentState::Unavailable);
    }

    // Alleen lezen: toont de stand van de catalogus op de echte machine (RAVENGUN_COMPONENTS_REAL=1); schrijft niets.
    void realMachineStatus() {
        if (qgetenv("RAVENGUN_COMPONENTS_REAL") != "1") QSKIP("set RAVENGUN_COMPONENTS_REAL=1 to print the status on this machine");
        ComponentEnv e;
        e.dataDir = QDir::homePath() + QStringLiteral("/.local/share/ravengun");
        e.sourceRoots = {QStringLiteral("/home/deck/mnt/spidernas_emulation/Teknoparrot")};
        QString err;
        for (const Component& c : Components::catalogue(&err)) {
            const auto st = Components::status(c, e);
            qInfo().noquote() << c.id << int(st.state) << st.detail;
        }
    }

    // Echt downloaden van de pins uit de catalogus naar een tijdelijke datamap (RAVENGUN_COMPONENTS_NET=1; kost ~100 MB).
    void realDownloads() {
        if (qgetenv("RAVENGUN_COMPONENTS_NET") != "1") QSKIP("set RAVENGUN_COMPONENTS_NET=1 to download the pinned files into a temp dir");
        const ComponentEnv e = env();
        QString err;
        const QList<Component> all = Components::catalogue(&err);
        ComponentInstaller inst(e);
        QSignalSpy fin(&inst, &ComponentInstaller::finished);
        QSignalSpy done(&inst, &ComponentInstaller::allFinished);
        inst.install(all, {QStringLiteral("wine"), QStringLiteral("dxvk"), QStringLiteral("bepinex-x64"), QStringLiteral("bepinex-x86"),
                           QStringLiteral("sdl12-compat"), QStringLiteral("gst-wmv"), QStringLiteral("demulshooter")});
        QVERIFY(done.wait(600000));
        for (const auto& f : fin) qInfo().noquote() << f.at(0).toString() << f.at(1).toBool() << f.at(2).toString();
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(QFileInfo(e.path(QStringLiteral("wine/proton-11.0-2/bin/wine"))).isExecutable());
        QVERIFY(QFileInfo(e.path(QStringLiteral("dxvk/current/x64/d3d11.dll"))).exists());
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("runtime/lib32/libSDL-1.2.so.0"))));
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("tools/gst-extra/plugins/libgstasf.so"))));
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("demulshooter/v17.9/Unity/TombRaider"))));
        for (const Component& c : all)
            if (c.id == QStringLiteral("wine") || c.id == QStringLiteral("dxvk") || c.id.startsWith(QStringLiteral("bepinex")))
                QCOMPARE(Components::status(c, e).state, ComponentState::Installed);
    }

    void withDependenciesOrder() {
        QList<Component> all;
        for (const char* id : {"a", "b", "c"}) { Component c; c.id = QString::fromLatin1(id); all << c; }
        all[2].dependsOn = {QStringLiteral("b")};
        all[1].dependsOn = {QStringLiteral("a")};
        const auto q = Components::withDependencies(all, {QStringLiteral("c"), QStringLiteral("a")});
        QCOMPARE(q.size(), 3);
        QCOMPARE(q[0].id, QStringLiteral("a"));
        QCOMPARE(q[2].id, QStringLiteral("c"));
    }

    void installReplacesAtomically() {
        const ComponentEnv e = env();
        Component c = simple(QStringLiteral("t"), QStringLiteral("test.tar.xz"), QStringLiteral("tar.xz"), QStringLiteral("opt/t"));
        c.links = {{QStringLiteral("opt/current"), QStringLiteral("t")}};
        c.writeFiles = {{QStringLiteral("opt/t/version"), QStringLiteral("v1\n")}};
        writeFile(e.path(QStringLiteral("opt/t/old-file")), "old");   // een oude installatie wordt vervangen
        QString err;
        QVERIFY2(ComponentInstaller::installFromFiles(c, {arch(QStringLiteral("test.tar.xz"))}, e, nullptr, nullptr, &err), qPrintable(err));
        QVERIFY(!QFileInfo::exists(e.path(QStringLiteral("opt/t/old-file"))));
        QCOMPARE(readFile(e.path(QStringLiteral("opt/t/lib/data.txt"))), QByteArray("hello\n"));
        QCOMPARE(readFile(e.path(QStringLiteral("opt/t/version"))), QByteArray("v1\n"));
        QVERIFY(QFileInfo(e.path(QStringLiteral("opt/current"))).isSymLink());
        QCOMPARE(QFile::symLinkTarget(e.path(QStringLiteral("opt/current"))), QFileInfo(e.path(QStringLiteral("opt/t"))).absoluteFilePath());
        QVERIFY(!QFileInfo::exists(e.path(QStringLiteral("components/.staging/t"))));
        QCOMPARE(Components::stampVersion(e, c.id), QStringLiteral("1"));
        QCOMPARE(Components::status(c, e).state, ComponentState::Installed);
        QStringList left = QDir(e.path(QStringLiteral("opt"))).entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        left.sort();
        QCOMPARE(left, (QStringList{QStringLiteral("current"), QStringLiteral("t")}));   // geen .old-resten
    }

    void installMergeKeepsOthers() {
        const ComponentEnv e = env();
        Component c = simple(QStringLiteral("m"), QStringLiteral("test.pkg.tar.zst"), QStringLiteral("pkg.tar.zst"), QString());
        c.merge = true;
        c.map = {{QStringLiteral("lib/data.txt"), QStringLiteral("runtime/lib32/libx.so.0")}};
        c.detect = {QStringLiteral("runtime/lib32/libx.so.0")};
        writeFile(e.path(QStringLiteral("runtime/lib32/other.so")), "keep");
        QString err;
        QVERIFY2(ComponentInstaller::installFromFiles(c, {arch(QStringLiteral("test.pkg.tar.zst"))}, e, nullptr, nullptr, &err), qPrintable(err));
        QCOMPARE(readFile(e.path(QStringLiteral("runtime/lib32/other.so"))), QByteArray("keep"));
        QCOMPARE(readFile(e.path(QStringLiteral("runtime/lib32/libx.so.0"))), QByteArray("hello\n"));
    }

    void treeMismatchLeavesDestAlone() {
        const ComponentEnv e = env();
        Component c = simple(QStringLiteral("mod"), QStringLiteral("test.zip"), QStringLiteral("zip"), QStringLiteral("mods/G/n/files"));
        c.treeSha256 = QString(64, QLatin1Char('0'));
        writeFile(e.path(QStringLiteral("mods/G/n/files/keep")), "keep");
        QString err;
        QVERIFY(!ComponentInstaller::installFromFiles(c, {arch(QStringLiteral("test.zip"))}, e, nullptr, nullptr, &err));
        QVERIFY(err.contains(QStringLiteral("tree sha256 mismatch")));
        QCOMPARE(readFile(e.path(QStringLiteral("mods/G/n/files/keep"))), QByteArray("keep"));
        c.treeSha256 = QStringLiteral("eb7de87e61e4561a9b735df8fb23ef9b8d6ff005e8b3b43bb36e2be677ce79a5");
        c.strip = 1;
        QVERIFY2(ComponentInstaller::installFromFiles(c, {arch(QStringLiteral("test.zip"))}, e, nullptr, nullptr, &err), qPrintable(err));
        QVERIFY(!QFileInfo::exists(e.path(QStringLiteral("mods/G/n/files/keep"))));
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("mods/G/n/files/lib/data.txt"))));
    }

    void downloadViaFileUrl() {
        const ComponentEnv e = env();
        Component c = simple(QStringLiteral("dl"), QStringLiteral("test.tar.gz"), QStringLiteral("tar.gz"), QStringLiteral("opt/dl"));
        ComponentInstaller inst(e);
        QSignalSpy fin(&inst, &ComponentInstaller::finished);
        QSignalSpy all(&inst, &ComponentInstaller::allFinished);
        QSignalSpy prog(&inst, &ComponentInstaller::progress);
        inst.install({c}, {QStringLiteral("dl")});
        QVERIFY(all.wait(15000));
        QCOMPARE(fin.size(), 1);
        QVERIFY2(fin.first().at(1).toBool(), qPrintable(fin.first().at(2).toString()));
        QVERIFY(prog.size() > 0);
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("opt/dl/lib/data.txt"))));
        QVERIFY(!QFileInfo::exists(inst.downloadPath(c.downloads.first())));   // archief opgeruimd
        QVERIFY(!QFileInfo::exists(inst.downloadPath(c.downloads.first()) + QStringLiteral(".part")));
        // nog eens: al geïnstalleerd, wordt overgeslagen
        fin.clear();
        all.clear();
        inst.install({c}, {QStringLiteral("dl")});
        QVERIFY(all.wait(5000));
        QCOMPARE(fin.first().at(2).toString(), QStringLiteral("already installed"));
    }

    void downloadWithWrongShaIsDiscarded() {
        const ComponentEnv e = env();
        Component c = simple(QStringLiteral("bad"), QStringLiteral("test.tar.gz"), QStringLiteral("tar.gz"), QStringLiteral("opt/bad"));
        c.downloads[0].sha256 = QString(64, QLatin1Char('1'));
        ComponentInstaller inst(e);
        QSignalSpy fin(&inst, &ComponentInstaller::finished);
        QSignalSpy all(&inst, &ComponentInstaller::allFinished);
        inst.install({c}, {QStringLiteral("bad")});
        QVERIFY(all.wait(15000));
        QVERIFY(!fin.first().at(1).toBool());
        QVERIFY(fin.first().at(2).toString().contains(QStringLiteral("sha256")));
        QVERIFY(!all.first().at(0).toBool());
        QVERIFY(!QFileInfo::exists(e.path(QStringLiteral("opt/bad"))));
        QVERIFY(!QFileInfo::exists(inst.downloadPath(c.downloads.first()) + QStringLiteral(".part")));
        QVERIFY(!QFileInfo::exists(inst.downloadPath(c.downloads.first())));
    }

    void cachedDownloadIsReused() {
        const ComponentEnv e = env();
        Component c = simple(QStringLiteral("cache"), QStringLiteral("test.tar.gz"), QStringLiteral("tar.gz"), QStringLiteral("opt/cache"));
        c.downloads[0].url = QStringLiteral("file:///nonexistent/never.tar.gz");   // zou falen als hij gedownload werd
        ComponentInstaller inst(e);
        QDir().mkpath(e.path(QStringLiteral("components/dl")));
        QVERIFY(QFile::copy(arch(QStringLiteral("test.tar.gz")), inst.downloadPath(c.downloads.first())));
        QSignalSpy all(&inst, &ComponentInstaller::allFinished);
        inst.install({c}, {QStringLiteral("cache")});
        QVERIFY(all.wait(15000));
        QVERIFY(all.first().at(0).toBool());
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("opt/cache/lib/data.txt"))));
    }

    void cancelStopsQueue() {
        const ComponentEnv e = env();
        Component a = simple(QStringLiteral("a"), QStringLiteral("test.tar.gz"), QStringLiteral("tar.gz"), QStringLiteral("opt/a"));
        Component b = simple(QStringLiteral("b"), QStringLiteral("test.zip"), QStringLiteral("zip"), QStringLiteral("opt/b"));
        ComponentInstaller inst(e);
        QSignalSpy all(&inst, &ComponentInstaller::allFinished);
        inst.install({a, b}, {QStringLiteral("a"), QStringLiteral("b")});
        inst.cancel();
        QVERIFY(all.wait(15000));
        QVERIFY(!all.first().at(0).toBool());
        QVERIFY(!QFileInfo::exists(e.path(QStringLiteral("opt/b/lib/data.txt"))));
    }

    void bundledAndUserFolderCopy() {
        ComponentEnv e = env();
        e.bundledRoot = tmp_.path() + QStringLiteral("/share/components");
        writeFile(e.bundledRoot + QStringLiteral("/hasp/ravengun-hasp.so"), "so");
        writeFile(e.bundledRoot + QStringLiteral("/hasp/Halo Fireteam/dongle.json"), "secret");
        QVERIFY(QFile::link(QStringLiteral("rev1"), e.bundledRoot + QStringLiteral("/hasp/current")));
        Component b;
        b.id = QStringLiteral("hasp");
        b.name = b.id;
        b.version = QStringLiteral("1");
        b.kind = QStringLiteral("bundled");
        b.recipe = QStringLiteral("extract");
        b.dest = QStringLiteral("hasp");
        b.merge = true;
        b.exclude = {QStringLiteral("Halo Fireteam")};
        b.detect = {QStringLiteral("hasp/ravengun-hasp.so")};
        QCOMPARE(Components::status(b, e).state, ComponentState::Missing);
        QString err;
        QVERIFY2(ComponentInstaller::installFromFiles(b, {}, e, nullptr, nullptr, &err), qPrintable(err));
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("hasp/ravengun-hasp.so"))));
        QVERIFY(!QFileInfo::exists(e.path(QStringLiteral("hasp/Halo Fireteam"))));   // dongle-gegevens gaan nooit mee
        QVERIFY(QFileInfo(e.path(QStringLiteral("hasp/current"))).isSymLink());

        // userfolder: gevonden naast de gamemap, gekopieerd zonder iets te overschrijven
        const QString games = tmp_.path() + QStringLiteral("/nas/Teknoparrot/Some Game");
        const QString tp = tmp_.path() + QStringLiteral("/nas/Teknoparrot/TeknoParrotUi-9.9-linux-x64");
        QDir().mkpath(games);
        writeFile(tp + QStringLiteral("/GameProfiles/A.xml"), "profile");
        writeFile(tp + QStringLiteral("/TeknoParrot/TeknoParrot64.dll"), "dll");
        writeFile(tp + QStringLiteral("/debug.log"), "log");
        Component t;
        t.id = QStringLiteral("tp");
        t.name = t.id;
        t.version = QStringLiteral("9.9");
        t.kind = QStringLiteral("userfolder");
        t.recipe = QStringLiteral("extract");
        t.dest = QStringLiteral("runtime/tp");
        t.merge = true;
        t.sourceNames = {QStringLiteral("TeknoParrotUi-*")};
        t.sourceRequire = {QStringLiteral("GameProfiles"), QStringLiteral("TeknoParrot/TeknoParrot64.dll")};
        t.exclude = {QStringLiteral("*.log")};
        t.detect = {QStringLiteral("runtime/tp/GameProfiles"), QStringLiteral("runtime/tp/TeknoParrot/TeknoParrot64.dll")};
        QCOMPARE(Components::status(t, e).state, ComponentState::Unavailable);
        e.sourceRoots = {games};
        auto st = Components::status(t, e);
        QCOMPARE(st.state, ComponentState::Missing);
        QCOMPARE(st.source, tp);
        writeFile(e.path(QStringLiteral("runtime/tp/GameProfiles/A.xml")), "mine");   // de eigen versie blijft
        QVERIFY2(ComponentInstaller::installFromFiles(t, {}, e, nullptr, nullptr, &err), qPrintable(err));
        QCOMPARE(readFile(e.path(QStringLiteral("runtime/tp/GameProfiles/A.xml"))), QByteArray("mine"));
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("runtime/tp/TeknoParrot/TeknoParrot64.dll"))));
        QVERIFY(!QFileInfo::exists(e.path(QStringLiteral("runtime/tp/debug.log"))));
        QVERIFY(QFileInfo::exists(tp + QStringLiteral("/GameProfiles/A.xml")));   // de bron is onaangeroerd
        QCOMPARE(readFile(tp + QStringLiteral("/GameProfiles/A.xml")), QByteArray("profile"));
    }

    void steamGst32AndLib32() {
        ComponentEnv e = env();
        const QString common = e.steamRoot + QStringLiteral("/steamapps/common");
        const QString proton = e.steamRoot + QStringLiteral("/compatibilitytools.d/GE-Proton10-1/files/lib/i386-linux-gnu");
        writeFile(proton + QStringLiteral("/gstreamer-1.0/libgstlibav.so"), "x");
        writeFile(proton + QStringLiteral("/gstreamer-1.0/libgstprotonmediaconverter.so"), "x");
        writeFile(proton + QStringLiteral("/libgstreamer-1.0.so.0"), "x");
        const QString sniper = common + QStringLiteral("/SteamLinuxRuntime_sniper/var/tmp-ABC/usr/lib/i386-linux-gnu");
        for (const char* l : {"libglut.so.3", "libXi.so.6", "libSDL2-2.0.so.0"}) writeFile(sniper + QLatin1Char('/') + QString::fromLatin1(l), "lib");
        Component g;
        g.id = QStringLiteral("gst32");
        g.name = g.id;
        g.version = QStringLiteral("1");
        g.kind = QStringLiteral("steam");
        g.recipe = QStringLiteral("gst32");
        g.dest = QStringLiteral("gst32");
        g.detect = {QStringLiteral("gst32/plugins"), QStringLiteral("gst32/proton")};
        QCOMPARE(Components::status(g, e).state, ComponentState::Missing);   // bron is er
        QString err;
        QVERIFY2(ComponentInstaller::installFromFiles(g, {}, e, nullptr, nullptr, &err), qPrintable(err));
        QVERIFY(QFileInfo(e.path(QStringLiteral("gst32/plugins/libgstlibav.so"))).isSymLink());
        QVERIFY(!QFileInfo::exists(e.path(QStringLiteral("gst32/plugins/libgstprotonmediaconverter.so"))));
        QCOMPARE(QFile::symLinkTarget(e.path(QStringLiteral("gst32/proton"))), QFileInfo(proton).absoluteFilePath());

        Component l;
        l.id = QStringLiteral("lib32-sniper");
        l.name = l.id;
        l.version = QStringLiteral("1");
        l.kind = QStringLiteral("steam");
        l.recipe = QStringLiteral("lib32-sniper");
        l.dest = QStringLiteral("runtime/lib32");
        l.merge = true;
        l.detect = {QStringLiteral("runtime/lib32/libglut.so.3"), QStringLiteral("runtime/lib32/libSDL2-2.0.so.0")};
        QVERIFY2(ComponentInstaller::installFromFiles(l, {}, e, nullptr, nullptr, &err), qPrintable(err));
        QCOMPARE(readFile(e.path(QStringLiteral("runtime/lib32/libXi.so.6"))), QByteArray("lib"));
    }

    void wineRecipesWithFakeWine() {
        const ComponentEnv e = env();
        const QString bin = e.path(QStringLiteral("wine/proton-11.0-2/bin"));
        // Een nep-wine: wineboot maakt system.reg, msiexec zet het PhysX-bestand neer.
        writeFile(bin + QStringLiteral("/wine"),
                  "#!/bin/sh\n"
                  "case \"$1\" in\n"
                  " wineboot) mkdir -p \"$WINEPREFIX/drive_c\" && echo reg > \"$WINEPREFIX/system.reg\" ;;\n"
                  " msiexec) test -n \"$WINESERVER\" && d=\"$WINEPREFIX/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.1\" && mkdir -p \"$d\" && echo \"$3\" > \"$d/PhysXCore.dll\" ;;\n"
                  "esac\n", true);
        writeFile(bin + QStringLiteral("/wineserver"), "#!/bin/sh\nexit 0\n", true);
        Component p;
        p.id = QStringLiteral("wine-prefix");
        p.name = p.id;
        p.version = QStringLiteral("1");
        p.kind = QStringLiteral("wine");
        p.recipe = QStringLiteral("wine-prefix");
        p.detect = {QStringLiteral("prefixes/shared/wine/system.reg")};
        QString err;
        QVERIFY2(ComponentInstaller::installFromFiles(p, {}, e, nullptr, nullptr, &err), qPrintable(err));
        QVERIFY(QFileInfo::exists(e.path(QStringLiteral("prefixes/shared/wine/system.reg"))));

        Component x = p;
        x.id = QStringLiteral("physx");
        x.recipe = QStringLiteral("wine-physx");
        x.detect = {QStringLiteral("prefixes/shared/wine/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.1/PhysXCore.dll")};
        QVERIFY2(ComponentInstaller::installFromFiles(x, {QStringLiteral("/tmp/x/PhysX.msi")}, e, nullptr, nullptr, &err), qPrintable(err));
        QCOMPARE(readFile(e.path(x.detect.first())).trimmed(), QByteArray("Z:\\tmp\\x\\PhysX.msi"));

        // zonder Wine: duidelijke melding, niets gedaan
        QDir(e.path(QStringLiteral("wine"))).removeRecursively();
        QVERIFY(!ComponentInstaller::installFromFiles(p, {}, e, nullptr, nullptr, &err));
        QVERIFY(err.contains(QStringLiteral("Wine is not installed")));
    }
};

QTEST_GUILESS_MAIN(ComponentsTest)
#include "test_components.moc"
