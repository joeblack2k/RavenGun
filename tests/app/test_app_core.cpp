// SPDX-License-Identifier: GPL-3.0-or-later
// Tests van de app-kern (src/app/core) zonder engine en zonder widgets: TP-profiel lezen/schrijven, de parser van
// game-options.json, schrijven naar doelen (nooit naar de "NAS"), bibliotheek-scan op een nepmap, memory card,
// ProfileSync en de runtime-controle. De "NAS" is een tijdelijke map via $RAVENGUN_NAS_PREFIXES.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "core/FileSafety.h"
#include "core/GameData.h"
#include "core/GameLibrary.h"
#include "core/GameOptions.h"
#include "core/Launcher.h"
#include "core/MemoryCard.h"
#include "core/TextConfig.h"
#include "core/TpProfile.h"

using namespace rg;

namespace {

QString dataFile(const QString& name) { return QStringLiteral(RAVENGUN_TEST_DATA "/") + name; }

QString readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(data);
}

// Een TP-profiel uit de testdata met het juiste GamePath.
void writeProfile(const QString& path, const QString& gamePath) {
    QString t = readAll(dataFile(QStringLiteral("TC5.xml")));
    t.replace(QStringLiteral("@GAMEPATH@"), gamePath);
    writeFile(path, t.toUtf8());
}

}  // namespace

class AppCoreTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void tpProfileFields();
    void tpProfileSetField();
    void textConfig();
    void gameOptionsParser();
    void gameOptionsTolerant();
    void expandBraces();
    void optionStoreWritesProfile();
    void optionStoreEnvAndIni();
    void neverWritesToNas();
    void overlayForViewSymlink();
    void libraryScan();
    void libraryRoundTrip();
    void memoryCardList();
    void profileSyncFromLibrary();
    void runtimeCheck();

private:
    QTemporaryDir tmp_;
    QString nas_, tp_, games_;
};

void AppCoreTest::initTestCase() {
    QVERIFY(tmp_.isValid());
    nas_ = tmp_.filePath(QStringLiteral("nas"));
    tp_ = tmp_.filePath(QStringLiteral("tp"));
    games_ = nas_ + QStringLiteral("/Teknoparrot");
    QDir().mkpath(games_);
    qputenv("RAVENGUN_NAS_PREFIXES", nas_.toUtf8());
    qputenv("XDG_DATA_HOME", tmp_.filePath(QStringLiteral("data")).toUtf8());
    qputenv("XDG_CONFIG_HOME", tmp_.filePath(QStringLiteral("config")).toUtf8());
}

void AppCoreTest::tpProfileFields() {
    TpProfile p;
    QVERIFY(p.load(dataFile(QStringLiteral("TC5.xml"))));
    QCOMPARE(p.tag(QStringLiteral("EmulatorType")), QStringLiteral("TeknoParrot"));
    QCOMPARE(p.executableNames(), QStringList{QStringLiteral("timecrisisgame-win64-shipping.exe")});
    const QList<TpField> f = p.fields();
    QCOMPARE(f.size(), 7);
    QCOMPARE(f[0].name, QStringLiteral("Input API"));
    QCOMPARE(f[0].options, (QStringList{QStringLiteral("DirectInput"), QStringLiteral("XInput"), QStringLiteral("RawInput")}));
    QCOMPARE(f[4].type, QStringLiteral("Slider"));
    QVERIFY(f[4].hasMin && f[4].hasMax);
    QCOMPARE(f[4].max, 50);
    QVERIFY(f[4].options.isEmpty());
    bool found = false;
    QCOMPARE(p.fieldValue(QStringLiteral("Enable"), QStringLiteral("Crosshair"), &found), QStringLiteral("0"));
    QVERIFY(found);
    QCOMPARE(p.fieldValue(QStringLiteral("Player 1 IP"), {}, &found), QString());
    QVERIFY(found);
    p.fieldValue(QStringLiteral("Nope"), {}, &found);
    QVERIFY(!found);
}

void AppCoreTest::tpProfileSetField() {
    TpProfile p;
    QVERIFY(p.load(dataFile(QStringLiteral("TC5.xml"))));
    const QString before = p.text();
    QVERIFY(p.setFieldValue(QStringLiteral("FreePlay"), QStringLiteral("1")));
    QVERIFY(p.setFieldValue(QStringLiteral("Player 1 IP"), QStringLiteral("10.0.0.2 & <x>")));  // <FieldValue /> wordt gevuld
    QVERIFY(p.setFieldValue(QStringLiteral("Enable"), QStringLiteral("1"), QStringLiteral("Crosshair")));
    QVERIFY(!p.setFieldValue(QStringLiteral("Enable"), QStringLiteral("1"), QStringLiteral("NoSuchCategory")));
    QCOMPARE(p.fieldValue(QStringLiteral("FreePlay")), QStringLiteral("1"));
    QCOMPARE(p.fieldValue(QStringLiteral("Player 1 IP")), QStringLiteral("10.0.0.2 & <x>"));
    QVERIFY(p.text().contains(QStringLiteral("<FieldValue>10.0.0.2 &amp; &lt;x&gt;</FieldValue>")));
    // De rest van het bestand blijft gelijk (bindingen e.d.).
    QVERIFY(p.text().startsWith(before.left(before.indexOf(QStringLiteral("<ConfigValues>")))));
    QVERIFY(p.text().contains(QStringLiteral("<BindNameRi>RavenGun Virtual Lightgun</BindNameRi>")));
}

void AppCoreTest::textConfig() {
    const QString text = QStringLiteral("; comment\nTop=1\n[Sound]\nVolume = 50\n\n[Video]\nWidth=640\n");
    bool found = false;
    QCOMPARE(ini::get(text, QStringLiteral("sound"), QStringLiteral("volume"), &found), QStringLiteral("50"));
    QVERIFY(found);
    QCOMPARE(ini::get(text, {}, QStringLiteral("Top")), QStringLiteral("1"));
    QString t2 = ini::set(text, QStringLiteral("Sound"), QStringLiteral("Volume"), QStringLiteral("70"));
    QCOMPARE(ini::get(t2, QStringLiteral("Sound"), QStringLiteral("Volume")), QStringLiteral("70"));
    QVERIFY(t2.contains(QStringLiteral("; comment")));
    t2 = ini::set(t2, QStringLiteral("Sound"), QStringLiteral("Mute"), QStringLiteral("0"));
    QVERIFY(t2.indexOf(QStringLiteral("Mute=0")) < t2.indexOf(QStringLiteral("[Video]")));
    t2 = ini::set(t2, QStringLiteral("New"), QStringLiteral("K"), QStringLiteral("v"));
    QVERIFY(t2.contains(QStringLiteral("[New]\nK=v")));
    QString env = keyvalue::set(QStringLiteral("# x\nA=1\n"), QStringLiteral("B"), QStringLiteral("2"));
    QCOMPARE(keyvalue::get(env, QStringLiteral("B")), QStringLiteral("2"));
    env = keyvalue::remove(env, QStringLiteral("A"));
    keyvalue::get(env, QStringLiteral("A"), &found);
    QVERIFY(!found);
}

void AppCoreTest::gameOptionsParser() {
    GameOptionsDb db;
    QString err;
    QVERIFY2(db.load(dataFile(QStringLiteral("game-options.json")), &err), qPrintable(err));
    QCOMPARE(db.entries().size(), 3);
    const GameOptionsEntry* e = db.find(QStringLiteral("TC5"));
    QVERIFY(e);
    QCOMPARE(e->id, QStringLiteral("time_crisis_5"));
    QCOMPARE(e->title, QStringLiteral("Time Crisis 5"));
    QCOMPARE(e->note, QStringLiteral("Playable via tp-shm."));
    QCOMPARE(e->options.size(), 8);
    const GameOption& fp = e->options[0];
    QCOMPARE(fp.type, QStringLiteral("bool"));
    QCOMPARE(fp.defaultValue, QStringLiteral("0"));
    QCOMPARE(fp.target.kind, OptionTarget::Kind::TpField);
    QCOMPARE(fp.target.category, QStringLiteral("General"));
    QCOMPARE(fp.target.field, QStringLiteral("FreePlay"));
    QCOMPARE(e->options[1].type, QStringLiteral("enum"));
    QCOMPARE(e->options[1].values.size(), 2);
    QVERIFY(e->options[2].hasRange);
    QCOMPARE(e->options[2].max, 50.0);
    QCOMPARE(e->options[3].target.kind, OptionTarget::Kind::TpTag);
    QCOMPARE(e->options[3].target.field, QStringLiteral("WineRunnerPath"));
    QCOMPARE(e->options[4].target.kind, OptionTarget::Kind::Env);
    QCOMPARE(e->options[5].target.companion, QStringLiteral("TP_GAMESCOPE_PATH=/usr/bin/gamescope"));
    QCOMPARE(e->options[6].target.kind, OptionTarget::Kind::Ini);
    QCOMPARE(e->options[6].target.section, QStringLiteral("Sound"));
    QCOMPARE(e->options[7].target.kind, OptionTarget::Kind::Unsupported);
    QVERIFY(e->options[7].target.readOnlyReason.contains(QStringLiteral("test menu")));
    QCOMPARE(e->memoryCard.size(), 4);
    QCOMPARE(e->memoryCard[2].safeToErase, QStringLiteral("no"));
    QCOMPARE(e->memoryNotes, QStringLiteral("Example notes."));
    QVERIFY(db.findByFolder(QStringLiteral("Mystery Gun")));
    const GameOptionsEntry* none = db.findByFolder(QStringLiteral("Shooter X"));
    QVERIFY(none);
    QVERIFY(none->profile.isEmpty());
    QVERIFY(!db.find(QString()));
    QVERIFY(db.warnings().isEmpty());
}

void AppCoreTest::gameOptionsTolerant() {
    // Ruimere vorm: games als lijst, doelen als korte tekst, memory_card als lijst.
    const QByteArray json = R"({"games":[{"profile":"Halo","note":"Works.","options":[
        {"key":"fp","label":"Free play","type":"checkbox","target":"tp:General/FreePlay"},
        {"key":"vol","type":"number","range":[0,10],"target":"ini:cfg/game.ini#Audio/Volume"},
        {"key":"gs","type":"bool","target":"env:TP_GAMESCOPE"},
        {"key":"lang","values":[{"value":"en","label":"English"},{"value":"nl","label":"Dutch"}],"target":"cfg:lang.txt#Lang"},
        {"key":"bad","type":"weird","target":{"kind":"nonsense"}},
        {"name":"no key"}],
      "memory_card":["nvram/*.bin", {"path":"save","label":"Saves","local":true}]}]})";
    GameOptionsDb db;
    QVERIFY(db.loadData(json));
    const GameOptionsEntry* e = db.find(QStringLiteral("Halo"));
    QVERIFY(e);
    QCOMPARE(e->note, QStringLiteral("Works."));
    QCOMPARE(e->options.size(), 5);
    QCOMPARE(e->options[0].type, QStringLiteral("bool"));
    QCOMPARE(e->options[0].name, QStringLiteral("Free play"));
    QCOMPARE(e->options[1].type, QStringLiteral("int"));
    QCOMPARE(e->options[1].target.file, QStringLiteral("cfg/game.ini"));
    QCOMPARE(e->options[1].target.section, QStringLiteral("Audio"));
    QCOMPARE(e->options[2].target.kind, OptionTarget::Kind::Env);
    QCOMPARE(e->options[3].type, QStringLiteral("enum"));
    QCOMPARE(e->options[3].labels, (QStringList{QStringLiteral("English"), QStringLiteral("Dutch")}));
    QCOMPARE(e->options[3].target.kind, OptionTarget::Kind::KeyValue);
    QCOMPARE(e->options[4].type, QStringLiteral("string"));
    QCOMPARE(e->memoryCard.size(), 2);
    QVERIFY(e->memoryCard[1].local);
    QVERIFY(db.warnings().size() >= 3);  // onbekend type, onbekend doel, optie zonder sleutel
    GameOptionsDb bad;
    QString err;
    QVERIFY(!bad.loadData("{nope", &err));
    QVERIFY(!err.isEmpty());
}

void AppCoreTest::expandBraces() {
    QCOMPARE(GameOptionsDb::expandBraces(QStringLiteral("a/{0,1}.txt")), (QStringList{QStringLiteral("a/0.txt"), QStringLiteral("a/1.txt")}));
    QCOMPARE(GameOptionsDb::expandBraces(QStringLiteral("{f,g}/{x,y}")).size(), 4);
    QCOMPARE(GameOptionsDb::expandBraces(QStringLiteral("{profile}/x")), QStringList{QStringLiteral("{profile}/x")});
}

void AppCoreTest::optionStoreWritesProfile() {
    const QString tp = tmp_.filePath(QStringLiteral("tp-store"));
    writeProfile(tp + QStringLiteral("/UserProfiles/TC5.xml"), QStringLiteral("/x/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"));
    GameOptionsDb db;
    QVERIFY(db.load(dataFile(QStringLiteral("game-options.json"))));
    const GameOptionsEntry* e = db.find(QStringLiteral("TC5"));
    GameContext c;
    c.profile = QStringLiteral("TC5");
    c.tpDir = tp;
    const GameOption& fp = e->options[0];
    bool present = false;
    QCOMPARE(OptionStore::read(fp, c, &present), QStringLiteral("0"));
    QVERIFY(present);
    QVERIFY(OptionStore::writable(fp, c));
    QString err;
    QVERIFY2(OptionStore::write(fp, c, QStringLiteral("1"), &err), qPrintable(err));
    QCOMPARE(OptionStore::read(fp, c), QStringLiteral("1"));
    QVERIFY(QFileInfo::exists(tp + QStringLiteral("/UserProfiles/TC5.xml.ravengun-bak")));  // eenmalige back-up
    QVERIFY(readAll(tp + QStringLiteral("/UserProfiles/TC5.xml.ravengun-bak")).contains(QStringLiteral("<FieldName>FreePlay</FieldName>\n      <FieldValue>0</FieldValue>")));
    // Element op het hoogste niveau (WineRunnerPath): toevoegen en weer weghalen.
    const GameOption& wr = e->options[3];
    QVERIFY(OptionStore::write(wr, c, QStringLiteral("/opt/wine/bin/wine"), &err));
    QCOMPARE(OptionStore::read(wr, c), QStringLiteral("/opt/wine/bin/wine"));
    QVERIFY(OptionStore::write(wr, c, QString(), &err));
    QVERIFY(!readAll(c.userProfilePath()).contains(QStringLiteral("WineRunnerPath")));
    // Zonder gebruikersprofiel: niet schrijfbaar.
    GameContext none = c;
    none.profile = QStringLiteral("Nope");
    QString reason;
    QVERIFY(!OptionStore::writable(fp, none, &reason));
    QCOMPARE(reason, QStringLiteral("set up the game first"));
}

void AppCoreTest::optionStoreEnvAndIni() {
    const QString tp = tmp_.filePath(QStringLiteral("tp-env"));
    writeProfile(tp + QStringLiteral("/UserProfiles/TC5.xml"), QStringLiteral("/x/game.exe"));
    GameOptionsDb db;
    QVERIFY(db.load(dataFile(QStringLiteral("game-options.json"))));
    const GameOptionsEntry* e = db.find(QStringLiteral("TC5"));
    GameContext c;
    c.profile = QStringLiteral("TC5");
    c.tpDir = tp;
    c.rootDir = tmp_.filePath(QStringLiteral("localgame"));
    c.gameDir = c.rootDir + QStringLiteral("/TC5/Binaries/Win64");
    QString err;
    // Bool met companion in .env: aan = beide regels, uit = beide weg.
    const GameOption& gs = e->options[5];
    QCOMPARE(OptionStore::read(gs, c), QStringLiteral("0"));
    QVERIFY2(OptionStore::write(gs, c, QStringLiteral("1"), &err), qPrintable(err));
    QString env = readAll(c.envPath());
    QVERIFY(env.contains(QStringLiteral("TP_GAMESCOPE=1")));
    QVERIFY(env.contains(QStringLiteral("TP_GAMESCOPE_PATH=/usr/bin/gamescope")));
    QVERIFY(OptionStore::write(gs, c, QStringLiteral("0"), &err));
    env = readAll(c.envPath());
    QVERIFY(!env.contains(QStringLiteral("TP_GAMESCOPE")));
    QVERIFY(OptionStore::write(e->options[4], c, QStringLiteral("2500"), &err));
    QCOMPARE(OptionStore::read(e->options[4], c), QStringLiteral("2500"));
    // INI relatief aan de gamemap (lokaal): nieuw bestand met sectie.
    const GameOption& vol = e->options[6];
    QCOMPARE(OptionStore::targetFile(vol.target, c), c.rootDir + QStringLiteral("/TC5/Config/Game.ini"));
    QVERIFY2(OptionStore::write(vol, c, QStringLiteral("55"), &err), qPrintable(err));
    QCOMPARE(OptionStore::read(vol, c), QStringLiteral("55"));
    QVERIFY(readAll(c.rootDir + QStringLiteral("/TC5/Config/Game.ini")).contains(QStringLiteral("[Sound]\nVolume=55")));
    // Een niet-bewerkbaar formaat blijft alleen-lezen.
    QString reason;
    QVERIFY(!OptionStore::writable(e->options[7], c, &reason));
    QVERIFY(reason.contains(QStringLiteral("test menu")));
}

void AppCoreTest::neverWritesToNas() {
    const QString game = games_ + QStringLiteral("/Time Crisis 5");
    writeFile(game + QStringLiteral("/TC5/Config/Game.ini"), "[Sound]\nVolume=10\n");
    QVERIFY(isOnNetwork(game + QStringLiteral("/TC5/Config/Game.ini")));
    QVERIFY(!isOnNetwork(tmp_.filePath(QStringLiteral("localgame"))));
    GameOptionsDb db;
    QVERIFY(db.load(dataFile(QStringLiteral("game-options.json"))));
    const GameOption& vol = db.find(QStringLiteral("TC5"))->options[6];
    GameContext c;
    c.profile = QStringLiteral("TC5");
    c.rootDir = game;
    QString reason, err;
    QVERIFY(!OptionStore::writable(vol, c, &reason));
    QCOMPARE(reason, QStringLiteral("read-only (on NAS)"));
    QVERIFY(!OptionStore::write(vol, c, QStringLiteral("99"), &err));
    QCOMPARE(readAll(game + QStringLiteral("/TC5/Config/Game.ini")), QStringLiteral("[Sound]\nVolume=10\n"));
    QVERIFY(!writeTextAtomic(game + QStringLiteral("/new.txt"), "x", &err));
    QVERIFY(!QFileInfo::exists(game + QStringLiteral("/new.txt")));
    // Een TP-map op de NAS is ook alleen-lezen.
    writeProfile(nas_ + QStringLiteral("/tp/UserProfiles/TC5.xml"), QStringLiteral("/x"));
    GameContext n;
    n.profile = QStringLiteral("TC5");
    n.tpDir = nas_ + QStringLiteral("/tp");
    QVERIFY(!OptionStore::writable(db.find(QStringLiteral("TC5"))->options[0], n, &reason));
}

void AppCoreTest::overlayForViewSymlink() {
    // Een lokale view-map met een symlink naar een bestand op de NAS: schrijven maakt eerst een lokale kopie.
    const QString nasFile = games_ + QStringLiteral("/ViewGame/settings.ini");
    writeFile(nasFile, "[Main]\nSpeed=1\n");
    const QString view = tmp_.filePath(QStringLiteral("views/ViewGame"));
    QDir().mkpath(view);
    QVERIFY(QFile::link(nasFile, view + QStringLiteral("/settings.ini")));
    const WriteCheck w = checkWritable(view + QStringLiteral("/settings.ini"));
    QVERIFY(w.ok);
    QVERIFY(w.needsOverlay);
    GameOption o;
    o.key = QStringLiteral("speed");
    o.type = QStringLiteral("int");
    o.target = GameOptionsDb::parseTarget(QStringLiteral("ini:settings.ini#Main/Speed"));
    GameContext c;
    c.rootDir = view;
    QString err;
    QVERIFY2(OptionStore::write(o, c, QStringLiteral("3"), &err), qPrintable(err));
    QVERIFY(!QFileInfo(view + QStringLiteral("/settings.ini")).isSymLink());
    QCOMPARE(OptionStore::read(o, c), QStringLiteral("3"));
    QCOMPARE(readAll(nasFile), QStringLiteral("[Main]\nSpeed=1\n"));  // NAS ongewijzigd
}

void AppCoreTest::libraryScan() {
    const QString tp = tmp_.filePath(QStringLiteral("tp-scan"));
    // Runtime: sjablonen, metadata, een installatie-map die geen game is.
    writeProfile(tp + QStringLiteral("/GameProfiles/TC5.xml"), QString());
    QString unity = readAll(dataFile(QStringLiteral("TC5.xml")));
    unity.replace(QStringLiteral("TimeCrisisGame-Win64-Shipping.exe"), QStringLiteral("game.exe"));
    writeFile(tp + QStringLiteral("/GameProfiles/GenericA.xml"), unity.toUtf8());
    writeFile(tp + QStringLiteral("/GameProfiles/GenericB.xml"), unity.toUtf8());
    writeFile(tp + QStringLiteral("/Metadata/TC5.json"), R"({"game_name":"Time Crisis 5","platform":"Namco ES3X","icon_name":"TC5.png"})");
    writeFile(tp + QStringLiteral("/Metadata/GenericA.json"), R"({"game_name":"Alpha Blaster"})");
    writeFile(tp + QStringLiteral("/Metadata/GenericB.json"), R"({"game_name":"Beta Shooter"})");
    writeFile(tp + QStringLiteral("/Icons/Halo.png"), "png");
    // Gamemap op de "NAS".
    const QString lib = tmp_.filePath(QStringLiteral("scan-games"));
    writeFile(lib + QStringLiteral("/Halo Fireteam/g7/halo/game"), "elf");
    writeFile(lib + QStringLiteral("/Time Crisis 5/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"), "exe");
    writeFile(lib + QStringLiteral("/Beta Shooter/game.exe"), "exe");
    writeFile(lib + QStringLiteral("/Ambiguous/game.exe"), "exe");
    writeFile(lib + QStringLiteral("/Mystery Gun/readme.txt"), "x");
    writeFile(lib + QStringLiteral("/Something Else/readme.txt"), "x");
    writeFile(lib + QStringLiteral("/TeknoParrotUi-2/TeknoParrotUi"), "bin");
    writeFile(lib + QStringLiteral("/TeknoParrotUi-2/GameProfiles/x.xml"), "<GameProfile/>");
    // Gebruikersprofiel Halo met GamePath in de gamemap.
    QString halo = readAll(dataFile(QStringLiteral("TC5.xml")));
    halo.replace(QStringLiteral("@GAMEPATH@"), lib + QStringLiteral("/Halo Fireteam/g7/halo/game"));
    halo.replace(QStringLiteral("<EmulatorType>TeknoParrot</EmulatorType>"), QStringLiteral("<EmulatorType>ElfLdr2</EmulatorType>"));
    halo.replace(QStringLiteral("<GunGame>true</GunGame>"), QStringLiteral("<GunGame>true</GunGame>\n  <GameNameInternal>HALO Fireteam Raven</GameNameInternal>\n  <IconName>Icons/Halo.png</IconName>"));
    writeFile(tp + QStringLiteral("/UserProfiles/Halo.xml"), halo.toUtf8());
    writeFile(tp + QStringLiteral("/UserProfiles/bad name.xml"), halo.toUtf8());  // ongeldige naam: genegeerd

    ScanInput in;
    in.tpDir = tp;
    in.folders << LibraryFolder{lib, true};
    in.known << KnownGame{QStringLiteral("Mystery Gun"), QStringLiteral("Mystery Gun Arcade"), QStringLiteral("MysteryGun"), QStringLiteral("Example PC"), QString()};
    const ScanResult r = LibraryScanner::scan(in);
    QHash<QString, GameEntry> byName;
    for (const GameEntry& g : r.games) byName.insert(QFileInfo(g.folder).fileName().isEmpty() ? g.id : QFileInfo(g.folder).fileName(), g);
    auto find = [&](const QString& id) -> GameEntry {
        for (const GameEntry& g : r.games)
            if (g.id == id) return g;
        return {};
    };
    const GameEntry h = find(QStringLiteral("tp:Halo"));
    QCOMPARE(h.kind, GameKind::TeknoParrot);
    QCOMPARE(h.name, QStringLiteral("HALO Fireteam Raven"));
    QCOMPARE(h.folder, lib + QStringLiteral("/Halo Fireteam"));
    QCOMPARE(h.platform, QStringLiteral("Linux PC"));
    QVERIFY(h.iconPath.endsWith(QStringLiteral("Icons/Halo.png")));
    const GameEntry t = find(QStringLiteral("dir:") + lib + QStringLiteral("/Time Crisis 5"));
    QCOMPARE(t.kind, GameKind::Detected);
    QCOMPARE(t.templateProfile, QStringLiteral("TC5"));
    QCOMPARE(t.name, QStringLiteral("Time Crisis 5"));
    QCOMPARE(t.platform, QStringLiteral("Namco ES3X"));
    QVERIFY(t.exePath.endsWith(QStringLiteral("TimeCrisisGame-Win64-Shipping.exe")));
    const GameEntry b = find(QStringLiteral("dir:") + lib + QStringLiteral("/Beta Shooter"));
    QCOMPARE(b.kind, GameKind::Detected);  // gedeelde exe-naam, beslist door de mapnaam
    QCOMPARE(b.templateProfile, QStringLiteral("GenericB"));
    const GameEntry a = find(QStringLiteral("dir:") + lib + QStringLiteral("/Ambiguous"));
    QCOMPARE(a.kind, GameKind::Unconfigured);  // twee kandidaten, geen naamgelijkenis
    QCOMPARE(a.candidates.size(), 2);
    const GameEntry m = find(QStringLiteral("dir:") + lib + QStringLiteral("/Mystery Gun"));
    QCOMPARE(m.kind, GameKind::Detected);  // via game-options.json (nas_folder)
    QCOMPARE(m.templateProfile, QStringLiteral("MysteryGun"));
    QCOMPARE(m.name, QStringLiteral("Mystery Gun Arcade"));
    QCOMPARE(find(QStringLiteral("dir:") + lib + QStringLiteral("/Something Else")).kind, GameKind::Unconfigured);
    QVERIFY(find(QStringLiteral("dir:") + lib + QStringLiteral("/TeknoParrotUi-2")).id.isEmpty());  // runtime, geen game
    QCOMPARE(r.detectedTpDir, lib + QStringLiteral("/TeknoParrotUi-2"));
    QVERIFY(find(QStringLiteral("tp:bad name")).id.isEmpty());
    QCOMPARE(r.games.size(), 6);
    // Zonder "Search subfolders" wordt de diep geneste exe van TC5 niet gevonden.
    in.folders[0].recursive = false;
    const ScanResult flat = LibraryScanner::scan(in);
    for (const GameEntry& g : flat.games)
        if (g.id.endsWith(QStringLiteral("/Time Crisis 5"))) QCOMPARE(g.kind, GameKind::Unconfigured);
    QVERIFY(LibraryScanner::nameSimilarity(QStringLiteral("Time Crisis 5"), QStringLiteral("Time Crisis 5")) > 0.99);
    QVERIFY(LibraryScanner::nameSimilarity(QStringLiteral("Rabbids Hollywood "), QStringLiteral("Rabbids Hollywood Arcade")) > 0.6);
    QCOMPARE(LibraryScanner::humanize(QStringLiteral("JurassicPark")), QStringLiteral("Jurassic Park"));
}

void AppCoreTest::libraryRoundTrip() {
    LibraryData d;
    d.folders << LibraryFolder{QStringLiteral("/games"), false};
    d.tpDir = QStringLiteral("/tp");
    d.defaultGame = QStringLiteral("tp:Halo");
    d.launcher = QStringLiteral("teknoparrot-ui");
    GameOverride o;
    o.name = QStringLiteral("My Halo");
    o.hidden = true;
    o.options.insert(QStringLiteral("general.freeplay"), QStringLiteral("1"));
    o.tpFields.insert(QStringLiteral("General/Windowed"), QStringLiteral("0"));
    d.overrides.insert(QStringLiteral("tp:Halo"), o);
    GameEntry g;
    g.id = QStringLiteral("tp:Halo");
    g.kind = GameKind::TeknoParrot;
    g.candidates << QStringLiteral("A");
    d.games << g;
    const QString path = tmp_.filePath(QStringLiteral("config/ravengun/library.json"));
    QString err;
    QVERIFY2(d.save(path, &err), qPrintable(err));
    LibraryData e;
    QVERIFY(e.load(path));
    QCOMPARE(e.folders.size(), 1);
    QVERIFY(!e.folders[0].recursive);
    QCOMPARE(e.defaultGame, QStringLiteral("tp:Halo"));
    QCOMPARE(e.launcher, QStringLiteral("teknoparrot-ui"));
    const GameOverride x = e.overrides.value(QStringLiteral("tp:Halo"));
    QCOMPARE(x.name, QStringLiteral("My Halo"));
    QVERIFY(x.hidden);
    QCOMPARE(x.options.value(QStringLiteral("general.freeplay")).toString(), QStringLiteral("1"));
    QCOMPARE(x.tpFields.value(QStringLiteral("General/Windowed")).toString(), QStringLiteral("0"));
    QCOMPARE(e.games.size(), 1);
    QCOMPARE(e.games[0].kind, GameKind::TeknoParrot);
    QCOMPARE(e.games[0].candidates, QStringList{QStringLiteral("A")});
    // GameLibrary: verborgen games en overrides.
    GameLibrary lib(path);
    lib.load();
    QVERIFY(lib.visibleGames().isEmpty());
    lib.unhideAll();
    QCOMPARE(lib.visibleGames().size(), 1);
    QCOMPARE(lib.visibleGames()[0].displayName(), QStringLiteral("My Halo"));
}

void AppCoreTest::memoryCardList() {
    // Gamemap lokaal, met een "NAS"-bestand via een symlink, plus een view met een eigen bestand.
    const QString root = tmp_.filePath(QStringLiteral("mc/Time Crisis 5"));
    writeFile(root + QStringLiteral("/TeknoParrot/c/TC5RankingData.bin"), "rank-c");
    writeFile(root + QStringLiteral("/TeknoParrot/f/TC5RankingData.bin"), "rank-f");
    writeFile(root + QStringLiteral("/TeknoParrot/g/TC5TestModeData.bin"), "test");
    writeFile(root + QStringLiteral("/hasp/key.bin"), "dongle");
    const QString nasRank = games_ + QStringLiteral("/mc-nas/TC5RankingData.bin");
    writeFile(nasRank, "nas");
    QFile::remove(root + QStringLiteral("/TeknoParrot/f/TC5RankingData.bin"));
    QVERIFY(QFile::link(nasRank, root + QStringLiteral("/TeknoParrot/f/TC5RankingData.bin")));
    const QString view = tmp_.filePath(QStringLiteral("mc/view"));
    writeFile(view + QStringLiteral("/hiscore.dat"), "hs");
    QVERIFY(QFile::link(nasRank, view + QStringLiteral("/linked.bin")));
    GameOptionsDb db;
    QVERIFY(db.load(dataFile(QStringLiteral("game-options.json"))));
    GameContext c;
    c.rootDir = root;
    c.gameDir = root + QStringLiteral("/TC5/Binaries/Win64");
    c.viewDir = view;
    const QList<MemoryCardFile> files = MemoryCard::list(c, db.find(QStringLiteral("TC5"))->memoryCard);
    QHash<QString, MemoryCardFile> by;
    for (const auto& f : files) by.insert(QDir(tmp_.path()).relativeFilePath(f.path), f);
    QCOMPARE(files.size(), 5);  // c-ranking, f-ranking (link), testmode, dongle, view-bestand; geen wine_prefix, geen view-link
    const MemoryCardFile rc = by.value(QStringLiteral("mc/Time Crisis 5/TeknoParrot/c/TC5RankingData.bin"));
    QVERIFY(rc.deletable);
    QCOMPARE(rc.kind, QStringLiteral("highscores"));
    QCOMPARE(rc.eraseEffect, QStringLiteral("Clears rankings."));
    const MemoryCardFile rf = by.value(QStringLiteral("mc/Time Crisis 5/TeknoParrot/f/TC5RankingData.bin"));
    QVERIFY(!rf.deletable);
    QVERIFY(rf.onNas);
    const MemoryCardFile dongle = by.value(QStringLiteral("mc/Time Crisis 5/hasp/key.bin"));
    QVERIFY(!dongle.deletable);
    QVERIFY(dongle.reason.contains(QStringLiteral("protected")));
    const MemoryCardFile tm = by.value(QStringLiteral("mc/Time Crisis 5/TeknoParrot/g/TC5TestModeData.bin"));
    QVERIFY(tm.deletable);
    QCOMPARE(tm.safeToErase, QStringLiteral("unknown"));
    const MemoryCardFile hs = by.value(QStringLiteral("mc/view/hiscore.dat"));
    QVERIFY(hs.deletable);
    QString err;
    QVERIFY(MemoryCard::remove(rc, &err));
    QVERIFY(!QFileInfo::exists(rc.path));
    QVERIFY(!MemoryCard::remove(rf, &err));
    QVERIFY(QFileInfo::exists(nasRank));
    QVERIFY(!MemoryCard::remove(dongle, &err));
    QVERIFY(QFileInfo::exists(dongle.path));
}

void AppCoreTest::profileSyncFromLibrary() {
    // De bibliotheek is de bron van waarheid: ProfileSync schrijft de bewaarde waarden in het gebruikersprofiel.
    const QString tp = tmp_.filePath(QStringLiteral("tp-sync"));
    writeProfile(tp + QStringLiteral("/UserProfiles/TC5.xml"), QStringLiteral("/x/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"));
    const QString libFile = tmp_.filePath(QStringLiteral("sync-library.json"));
    LibraryData d;
    d.tpDir = tp;
    GameEntry g;
    g.id = QStringLiteral("tp:TC5");
    g.kind = GameKind::TeknoParrot;
    g.profile = QStringLiteral("TC5");
    g.exePath = QStringLiteral("/x/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe");
    d.games << g;
    GameOverride o;
    o.options.insert(QStringLiteral("general.freeplay"), QStringLiteral("1"));
    o.options.insert(QStringLiteral("display.gamescope"), QStringLiteral("1"));
    o.tpFields.insert(QStringLiteral("General/Windowed"), QStringLiteral("1"));
    o.tpFields.insert(QStringLiteral("Arguments/Text Language"), QStringLiteral("Spanish"));
    d.overrides.insert(g.id, o);
    QVERIFY(d.save(libFile));
    GameLibrary lib(libFile);
    lib.load();
    GameData data;
    data.loadFrom({}, {}, dataFile(QStringLiteral("game-options.json")));
    QStringList errors;
    QVERIFY2(ProfileSync::apply(g, lib, data, &errors), qPrintable(errors.join(QLatin1Char(';'))));
    TpProfile p;
    QVERIFY(p.load(tp + QStringLiteral("/UserProfiles/TC5.xml")));
    QCOMPARE(p.fieldValue(QStringLiteral("FreePlay")), QStringLiteral("1"));
    QCOMPARE(p.fieldValue(QStringLiteral("Windowed")), QStringLiteral("1"));
    QCOMPARE(p.fieldValue(QStringLiteral("Text Language"), QStringLiteral("Arguments")), QStringLiteral("Spanish"));
    QVERIFY(readAll(tp + QStringLiteral("/UserProfiles/TC5.env")).contains(QStringLiteral("TP_GAMESCOPE=1")));
    // Context: de gamemap volgt uit `exe` in game-options.json.
    const GameContext c = data.context(g, tp);
    QCOMPARE(c.rootDir, QStringLiteral("/x"));
    QCOMPARE(c.gameDir, QStringLiteral("/x/TC5/Binaries/Win64"));
}

void AppCoreTest::runtimeCheck() {
    const QString rt = tmp_.filePath(QStringLiteral("runtime"));
    RuntimeCheck c = RuntimeCheck::check(rt);
    QVERIFY(!c.ok);
    writeFile(rt + QStringLiteral("/ElfLdr2/BudgieLoader.exe"), "x");
    writeFile(rt + QStringLiteral("/GameProfiles/a.xml"), "x");
    c = RuntimeCheck::check(rt);
    QVERIFY(c.ok);
    QVERIFY(!c.hasUi);
    writeFile(rt + QStringLiteral("/TeknoParrotUi"), "x");
    c = RuntimeCheck::check(rt);
    QVERIFY(c.hasUi);
    DirectBackend direct;
    QString why;
    QVERIFY(!direct.available(&why));
    QVERIFY(!why.isEmpty());
}

QTEST_GUILESS_MAIN(AppCoreTest)
#include "test_app_core.moc"
