// SPDX-License-Identifier: GPL-3.0-or-later
// Tests van de app-kern (src/app/core) zonder engine en zonder widgets: TP-profiel lezen/schrijven, de parser van
// options.json per game (met lagen), schrijven naar doelen (nooit naar de "NAS"), bibliotheek-scan op een nepmap, memory card,
// ProfileSync en de runtime-controle. De "NAS" is een tijdelijke map via $RAVENGUN_NAS_PREFIXES.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QProcess>
#include <QThread>
#include <QtTest>

#include "core/AppPaths.h"
#include "core/DongleStatus.h"
#include "core/FileSafety.h"
#include "core/GameData.h"
#include "core/GameLibrary.h"
#include "core/GameOptions.h"
#include "core/Install.h"
#include "core/Launcher.h"
#include "core/MemoryCard.h"
#include "core/TextConfig.h"
#include "core/TpProfile.h"
#include "core/BigPicture.h"
#include "core/SetupChecks.h"
#include "core/SteamShortcuts.h"
#include "core/SystemControl.h"

#include <linux/input.h>
#include <unistd.h>

using namespace rg;

namespace {

QString dataFile(const QString& name) { return QStringLiteral(RAVENGUN_TEST_DATA "/") + name; }

// De drie testgames uit tests/app/data/patches/<Game>/options.json.
const QStringList& fixtureGames() {
    static const QStringList g{QStringLiteral("TC5"), QStringLiteral("MysteryGun"), QStringLiteral("ShooterX")};
    return g;
}
bool loadFixtureOptions(GameOptionsDb& db, QString* err = nullptr) {
    for (const QString& g : fixtureGames())
        if (!db.loadGame(g, dataFile(QStringLiteral("patches/") + g + QStringLiteral("/options.json")), err)) return false;
    return true;
}

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
    void optionStoreAdjVal();
    void neverWritesToNas();
    void overlayForViewSymlink();
    void libraryScan();
    void libraryRoundTrip();
    void memoryCardList();
    void profileSyncFromLibrary();
    void libraryFindsOwnProfiles();
    void unityGamesPerGame();
    void ownProfileOptionsWriteUserLayer();
    void profileSyncOwnProfile();
    void dongleStatusText();
    void dongleSettingDifferencesOnly();
    void dongleDetectionRuns();
    void gameCameraChoice();
    void runtimeCheck();
    // A3: echte options.json-bestanden, invoerroute van de gebruiker, notities
    void realGameOptions();
    void optionsLayers();
    void extraIniFormats();
    void routeOverride();
    void routeOverrideInvalid();
    void patchesAndSettings();
    void notesEnglish();
    // A7: fullscreen-logica
    void bigPictureLayoutAndHit();
    void bigPictureNavigate();
    void shotConfirm();
    void aimEvents();
    void gamepadMapping();
    void bigPictureGameFilter();
    // A8: binaire VDF en Steam
    void vdfRoundTripSample();
    void vdfRoundTripRealCopy();
    void vdfRejectsBroken();
    void steamShortcutId();
    void steamAddOrUpdate();
    void steamNewFile();
    void steamUsersAndRunning();
    // A6: commando's van de wizard, installeren en de engine als service (zonder shell)
    void installPathLogic();
    void installUnitAndDesktopFiles();
    void installSelfInstall();
    void installEngineService();
    void installUninstall();
    void installAsyncRunner();
    void installRealBinary();
    void setupCommands();
    void dataRootOrder();

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
    QVERIFY2(loadFixtureOptions(db, &err), qPrintable(err));
    QCOMPARE(db.entries().size(), 3);
    const GameOptionsEntry* e = db.find(QStringLiteral("TC5"));
    QVERIFY(e);
    QCOMPARE(e->profile, QStringLiteral("TC5"));
    QCOMPARE(e->title, QStringLiteral("Time Crisis 5"));
    QVERIFY(e->file.endsWith(QStringLiteral("/patches/TC5/options.json")));
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
    QCOMPARE(none->profile, QStringLiteral("ShooterX"));
    QVERIFY(!db.find(QString()));
    QVERIFY(db.warnings().isEmpty());
}

void AppCoreTest::gameOptionsTolerant() {
    // Ruimere vorm: doelen als korte tekst, memory_card als lijst.
    const QByteArray json = R"({"options":[
        {"key":"fp","label":"Free play","type":"checkbox","target":"tp:General/FreePlay"},
        {"key":"vol","type":"number","range":[0,10],"target":"ini:cfg/game.ini#Audio/Volume"},
        {"key":"gs","type":"bool","target":"env:TP_GAMESCOPE"},
        {"key":"lang","values":[{"value":"en","label":"English"},{"value":"nl","label":"Dutch"}],"target":"cfg:lang.txt#Lang"},
        {"key":"bad","type":"weird","target":{"kind":"nonsense"}},
        {"name":"no key"}],
      "memory_card":["nvram/*.bin", {"path":"save","label":"Saves","local":true}]})";
    GameOptionsDb db;
    QVERIFY(db.loadGameData(QStringLiteral("Halo"), json));
    const GameOptionsEntry* e = db.find(QStringLiteral("Halo"));
    QVERIFY(e);
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
    QVERIFY(!bad.loadGameData(QStringLiteral("Halo"), "{nope", &err));
    QVERIFY(err.contains(QStringLiteral("Halo/options.json")));
    QVERIFY(!bad.find(QStringLiteral("Halo")));
    QVERIFY(!bad.loadGameData(QStringLiteral("Halo"), "[1,2]", &err));   // een JSON-lijst is geen game
    QVERIFY(!bad.loadGame(QStringLiteral("Halo"), tmp_.filePath(QStringLiteral("bestaat-niet.json")), &err));
    // Opnieuw laden vervangt de entry van deze game; een mislukte lading laat niets van de oude over.
    QVERIFY(bad.loadGameData(QStringLiteral("Halo"), R"({"title":"A"})"));
    QVERIFY(bad.loadGameData(QStringLiteral("Halo"), R"({"title":"B"})"));
    QCOMPARE(bad.entries().size(), 1);
    QCOMPARE(bad.find(QStringLiteral("Halo"))->title, QStringLiteral("B"));
    QVERIFY(!bad.loadGameData(QStringLiteral("Halo"), "x"));
    QVERIFY(bad.isEmpty());
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
    QVERIFY(loadFixtureOptions(db));
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

void AppCoreTest::optionStoreAdjVal() {
    // Een instelling van een Raw Thrills-game in zijn eigen opslagvorm (rtdt_adjval), zoals Jurassic Park ze heeft: drie kopieen op de NAS,
    // schrijven gaat naar de lokale schrijflaag van de native route (XDG_DATA_HOME/ravengun/native/<profiel>/0/upper), de NAS blijft gelijk.
    const QString game = games_ + QStringLiteral("/AdjGame");
    const QString rel = QStringLiteral("/pmuser/adj/save/playerCost/freeplay.%1.txt");
    QByteArray text("# RTDT Data Type AdjValStore\r\nname = freeplay\r\nversion = 0\r\ntime_set = 1704136932\r\nval = 1\r\n");
    text.append('\0');
    auto u32 = [](quint32 v) { QByteArray b(4, '\0'); for (int i = 0; i < 4; ++i) b[i] = char((v >> (8 * i)) & 0xff); return b; };
    const QByteArray orig = u32(20) + QCryptographicHash::hash(text, QCryptographicHash::Sha1) + u32(quint32(text.size())) + text;
    QCOMPARE(orig.size(), 121);  // de vorm van het echte bestand (freeplay.0.txt van Jurassic Park: 121 bytes)
    for (int i = 0; i < 3; ++i) {
        QDir().mkpath(QFileInfo(game + rel.arg(i)).path());
        QFile f(game + rel.arg(i));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(orig);
    }
    GameOptionsDb db;
    QVERIFY(db.loadGameData(QStringLiteral("AdjGame"), R"({"options":[{"key":"adj.freeplay","name_en":"Free play","type":"bool",
        "default":false,"target":{"kind":"game_config_file","path":"pmuser/adj/save/playerCost/freeplay.{0,1,2}.txt","base":"native_layer",
        "format":"rtdt_adjval","key":"freeplay","edit_outside_game":true}}]})"));
    const GameOption& o = db.find(QStringLiteral("AdjGame"))->options[0];
    QCOMPARE(o.target.kind, OptionTarget::Kind::AdjVal);
    GameContext c;
    c.profile = QStringLiteral("AdjGame");
    c.gameDir = game;
    bool present = false;
    QCOMPARE(OptionStore::read(o, c, &present), QStringLiteral("1"));  // nog niets in de schrijflaag: de NAS
    QVERIFY(present);
    QString why, err;
    QVERIFY2(OptionStore::writable(o, c, &why), qPrintable(why));
    QVERIFY2(OptionStore::write(o, c, QStringLiteral("0"), &err), qPrintable(err));
    QCOMPARE(OptionStore::read(o, c), QStringLiteral("0"));
    const QString layer = tmp_.filePath(QStringLiteral("data")) + QStringLiteral("/ravengun/native/AdjGame/0/upper");
    for (int i = 0; i < 3; ++i) {
        QFile n(game + rel.arg(i));
        QVERIFY(n.open(QIODevice::ReadOnly));
        QCOMPARE(n.readAll(), orig);  // de NAS is niet aangeraakt
        QFile f(layer + rel.arg(i));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()));
        const QByteArray d = f.readAll();
        const QByteArray t = d.mid(28);
        QCOMPARE(d.left(4), u32(20));
        QCOMPARE(d.mid(4, 20), QCryptographicHash::hash(t, QCryptographicHash::Sha1));  // de controlesom die de game controleert
        QCOMPARE(d.mid(24, 4), u32(quint32(t.size())));
        QVERIFY(t.contains("name = freeplay\r\n") && t.contains("val = 0\r\n") && t.endsWith('\0'));
    }
    // Een kapotte kopie in de schrijflaag wordt overgeslagen (zoals de game zelf doet): de volgende kopie telt.
    QFile kapot(layer + rel.arg(0));
    QVERIFY(kapot.open(QIODevice::ReadWrite));
    kapot.seek(40);
    kapot.write("X");
    kapot.close();
    QCOMPARE(OptionStore::read(o, c), QStringLiteral("0"));
    // Zonder native_layer is het bestand op de NAS: alleen lezen.
    GameOptionsDb nas;
    QVERIFY(nas.loadGameData(QStringLiteral("AdjGame"), R"({"options":[{"key":"adj.freeplay","type":"bool",
        "target":{"kind":"game_config_file","path":"pmuser/adj/save/playerCost/freeplay.{0,1,2}.txt","format":"rtdt_adjval","key":"freeplay",
        "edit_outside_game":true}}]})"));
    QVERIFY(!OptionStore::writable(nas.find(QStringLiteral("AdjGame"))->options[0], c, &why));
    QCOMPARE(why, QStringLiteral("read-only (on NAS)"));
}

void AppCoreTest::optionStoreEnvAndIni() {
    const QString tp = tmp_.filePath(QStringLiteral("tp-env"));
    writeProfile(tp + QStringLiteral("/UserProfiles/TC5.xml"), QStringLiteral("/x/game.exe"));
    GameOptionsDb db;
    QVERIFY(loadFixtureOptions(db));
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
    QVERIFY(loadFixtureOptions(db));
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
    QCOMPARE(m.kind, GameKind::Detected);  // via options.json (nas_folder)
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
    d.launcher = QStringLiteral("direct");
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
    QCOMPARE(e.launcher, QStringLiteral("direct"));
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
    QVERIFY(loadFixtureOptions(db));
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
    const QString userSettings = tmp_.filePath(QStringLiteral("sync-user-settings"));
    data.loadFrom(ravengun::gamesettings::Dirs{dataFile(QStringLiteral("patches")).toStdString(), userSettings.toStdString()});
    QStringList errors;
    QVERIFY2(ProfileSync::apply(g, lib, data, &errors), qPrintable(errors.join(QLatin1Char(';'))));
    TpProfile p;
    QVERIFY(p.load(tp + QStringLiteral("/UserProfiles/TC5.xml")));
    QCOMPARE(p.fieldValue(QStringLiteral("FreePlay")), QStringLiteral("1"));
    QCOMPARE(p.fieldValue(QStringLiteral("Windowed")), QStringLiteral("1"));
    QCOMPARE(p.fieldValue(QStringLiteral("Text Language"), QStringLiteral("Arguments")), QStringLiteral("Spanish"));
    QVERIFY(readAll(tp + QStringLiteral("/UserProfiles/TC5.env")).contains(QStringLiteral("TP_GAMESCOPE=1")));
    // Ook in de gebruikerslaag van game.ini, zodat de keuze voor het meegeleverde game.ini gaat; uit = sleutel terugzetten.
    const QString userIni = readAll(userSettings + QStringLiteral("/TC5/game.ini"));
    QVERIFY(userIni.contains(QStringLiteral("Gamescope = true")) && userIni.contains(QStringLiteral("GamescopePath = /usr/bin/gamescope")));
    LibraryData d2 = lib.data();
    d2.overrides[g.id].options.insert(QStringLiteral("display.gamescope"), QStringLiteral("0"));
    QVERIFY(d2.save(libFile));
    GameLibrary lib2(libFile);
    lib2.load();
    QVERIFY2(ProfileSync::apply(g, lib2, data, &errors), qPrintable(errors.join(QLatin1Char(';'))));
    QVERIFY(!readAll(tp + QStringLiteral("/UserProfiles/TC5.env")).contains(QStringLiteral("TP_GAMESCOPE")));
    const QString userIni2 = readAll(userSettings + QStringLiteral("/TC5/game.ini"));
    QVERIFY(userIni2.contains(QStringLiteral("Gamescope =\n")) && !userIni2.contains(QStringLiteral("Gamescope = true")));
    // Context: de gamemap volgt uit `exe` in options.json.
    const GameContext c = data.context(g, tp);
    QCOMPARE(c.rootDir, QStringLiteral("/x"));
    QCOMPARE(c.gameDir, QStringLiteral("/x/TC5/Binaries/Win64"));
}

// --- Eigen profielen in game.ini (docs/per-game-patches.md, "Het profiel van een game") -------------------------------------------

namespace {

// patches/TC5/game.ini met een eigen profiel; de games staan in een gamemap, het pad staat nergens.
QString writeOwnTc5(const QString& patches) {
    writeFile(patches + QStringLiteral("/TC5/game.ini"),
              "[Game]\nName = Time Crisis 5\nType = TeknoParrot\nExecutable = TimeCrisisGame-Win64-Shipping.exe\nProfileRevision = 19\nEmulationProfile = TimeCrisis5\n"
              "[Identify]\nPath = TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe\nSize = 3\n"
              "[Config]\nGeneral/FreePlay = 0\nGeneral/Windowed = 0\nArguments/Text Language = English\n"
              "[ConfigType]\nGeneral/FreePlay = Bool\nGeneral/Windowed = Bool\nArguments/Text Language = Dropdown\n"
              "[ConfigOptions]\nArguments/Text Language = English|Spanish\n");
    for (const QString& g : fixtureGames()) {   // en de opties van de testgames, zoals patches/<Game>/options.json
        QDir().mkpath(patches + QLatin1Char('/') + g);
        QFile::copy(dataFile(QStringLiteral("patches/") + g + QStringLiteral("/options.json")), patches + QLatin1Char('/') + g + QStringLiteral("/options.json"));
    }
    return patches;
}

}  // namespace

void AppCoreTest::libraryFindsOwnProfiles() {
    const QString patches = writeOwnTc5(tmp_.filePath(QStringLiteral("own-patches")));
    const QString user = tmp_.filePath(QStringLiteral("own-user"));
    const QString games = tmp_.filePath(QStringLiteral("own-games"));
    const QString tp = tmp_.filePath(QStringLiteral("own-tp"));      // geen UserProfiles: de game bestaat alleen in game.ini
    QDir().mkpath(tp + QStringLiteral("/GameProfiles"));
    writeFile(games + QStringLiteral("/Time Crisis 5/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"), "abc");
    writeFile(games + QStringLiteral("/Andere Map/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"), "abcdef");   // andere versie
    GameData data;
    data.loadFrom(ravengun::gamesettings::Dirs{patches.toStdString(), user.toStdString()});
    QVERIFY(data.hasOwnProfile(QStringLiteral("TC5")));
    QCOMPARE(data.ownGames().size(), 1);
    ScanInput in;
    in.tpDir = tp;
    in.folders << LibraryFolder{games, true};
    in.ownGames = data.ownGames();
    in.viewsDir = tmp_.filePath(QStringLiteral("own-views"));
    ScanResult r = LibraryScanner::scan(in);
    GameEntry tc5;
    for (const GameEntry& g : r.games)
        if (g.id == QStringLiteral("tp:TC5")) tc5 = g;
    QCOMPARE(tc5.kind, GameKind::TeknoParrot);
    QCOMPARE(tc5.profile, QStringLiteral("TC5"));
    QCOMPARE(tc5.name, QStringLiteral("Time Crisis 5"));
    QCOMPARE(tc5.exePath, games + QStringLiteral("/Time Crisis 5/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"));
    QCOMPARE(tc5.folder, games + QStringLiteral("/Time Crisis 5"));
    int inFolder = 0;
    for (const GameEntry& g : r.games) inFolder += g.folder == tc5.folder ? 1 : 0;
    QCOMPARE(inFolder, 1);   // de map komt niet nog eens als niet-ingestelde game
    // Een lokale view gaat voor de gamemap.
    writeFile(in.viewsDir + QStringLiteral("/TC5/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"), "abc");
    r = LibraryScanner::scan(in);
    for (const GameEntry& g : r.games)
        if (g.id == QStringLiteral("tp:TC5")) tc5 = g;
    QVERIFY(tc5.exePath.startsWith(in.viewsDir));
    // [Launch] StackReserve: de exe in de view is een lokale kopie, met de koppeling <exe>.ravengun-orig naar het origineel. Zonder die
    // koppeling hoort de gamemap bij niemand en komt hij als tweede game in de lijst (Mario Kart DX); met de koppeling hoort hij bij TC5.
    const QString viewExe = in.viewsDir + QStringLiteral("/TC5/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe");
    const QString gameDirId = QStringLiteral("dir:") + games + QStringLiteral("/Time Crisis 5");
    auto dirEntries = [&](const ScanResult& res) {
        int n = 0;
        for (const GameEntry& g : res.games) n += g.id == gameDirId ? 1 : 0;
        return n;
    };
    QCOMPARE(dirEntries(r), 1);
    QVERIFY(QFile::link(games + QStringLiteral("/Time Crisis 5/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"), viewExe + QStringLiteral(".ravengun-orig")));
    r = LibraryScanner::scan(in);
    QCOMPARE(dirEntries(r), 0);
    QFile::remove(viewExe + QStringLiteral(".ravengun-orig"));
    // Een override van de gebruiker wint; zonder bestand is de game niet gevonden.
    writeFile(user + QStringLiteral("/TC5/game.ini"), ("[Game]\nGamePath = " + games + "/Andere Map/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe\n").toUtf8());
    data.loadFrom(ravengun::gamesettings::Dirs{patches.toStdString(), user.toStdString()});
    in.ownGames = data.ownGames();
    r = LibraryScanner::scan(in);
    for (const GameEntry& g : r.games)
        if (g.id == QStringLiteral("tp:TC5")) tc5 = g;
    QVERIFY(tc5.exePath.contains(QStringLiteral("Andere Map")));
    QFile::remove(in.viewsDir + QStringLiteral("/TC5/TC5/Binaries/Win64/TimeCrisisGame-Win64-Shipping.exe"));
    QDir(games).removeRecursively();
    QDir().mkpath(games);
    writeFile(user + QStringLiteral("/TC5/game.ini"), "");
    data.loadFrom(ravengun::gamesettings::Dirs{patches.toStdString(), user.toStdString()});
    in.ownGames = data.ownGames();
    r = LibraryScanner::scan(in);
    for (const GameEntry& g : r.games) QVERIFY(g.id != QStringLiteral("tp:TC5"));
}

void AppCoreTest::unityGamesPerGame() {
    // Unity-games: patches/<Game>/unity.json, de gebruikerslaag eerst en als geheel. De map staat als mapnaam in het bestand en wordt
    // gezocht in de gamemappen (zoals de daemon doet); alleen de gebruikerslaag mag een eigen pad geven.
    const QString patches = tmp_.filePath(QStringLiteral("unity-patches")), user = tmp_.filePath(QStringLiteral("unity-user"));
    const QString games = tmp_.filePath(QStringLiteral("unity-games")), elders = tmp_.filePath(QStringLiteral("unity-elders"));
    writeFile(patches + QStringLiteral("/Uni/unity.json"), R"({"name":"Uni Game","folder":"Uni Map ","exe":"Game.exe"})");
    writeFile(patches + QStringLiteral("/Diep/unity.json"), R"({"folder":"Diep Map"})");
    writeFile(patches + QStringLiteral("/Eigen/unity.json"), R"({"name":"Eigen","folder":"Bestaat Niet"})");
    writeFile(patches + QStringLiteral("/Kapot/unity.json"), R"({"folder":"Kapot Map"})");
    writeFile(user + QStringLiteral("/Eigen/unity.json"), ("{\"name\":\"Eigen\",\"nas_dir\":\"" + elders + "/Eigen Plek\"}").toUtf8());
    writeFile(user + QStringLiteral("/Kapot/unity.json"), "{kapot");
    writeFile(games + QStringLiteral("/Uni Map /Game.exe"), "MZ");
    writeFile(games + QStringLiteral("/Teknoparrot/Diep Map/Game.exe"), "MZ");   // een niveau dieper dan de gamemap
    writeFile(games + QStringLiteral("/Kapot Map/Game.exe"), "MZ");
    writeFile(elders + QStringLiteral("/Eigen Plek/Game.exe"), "MZ");
    GameData d;
    d.loadFrom(ravengun::gamesettings::Dirs{patches.toStdString(), user.toStdString()});
    QCOMPARE(d.unityGames().size(), 4);
    QCOMPARE(d.availableRoutes(QStringLiteral("Uni")), QStringList{QStringLiteral("unity-tcp")});
    QCOMPARE(d.availableRoutes(QStringLiteral("Kapot")), QStringList{QStringLiteral("unity-tcp")});   // het blijft een Unity-game
    QCOMPARE(d.availableRoutes(QStringLiteral("Weg")), QStringList{QStringLiteral("evdev")});
    ScanInput in;
    in.folders << LibraryFolder{games, true};
    in.unityGames = d.unityGames();
    in.viewsDir = tmp_.filePath(QStringLiteral("unity-views"));
    ScanResult r = LibraryScanner::scan(in);
    QHash<QString, GameEntry> byId;
    for (const GameEntry& g : r.games) byId.insert(g.id, g);
    QVERIFY(byId.contains(QStringLiteral("unity:Uni")));
    QCOMPARE(byId.value(QStringLiteral("unity:Uni")).folder, games + QStringLiteral("/Uni Map "));
    QCOMPARE(byId.value(QStringLiteral("unity:Uni")).name, QStringLiteral("Uni Game"));
    QCOMPARE(byId.value(QStringLiteral("unity:Uni")).kind, GameKind::Unity);
    QCOMPARE(byId.value(QStringLiteral("unity:Uni")).exePath, games + QStringLiteral("/Uni Map /Game.exe"));
    QVERIFY(byId.contains(QStringLiteral("unity:Diep")));
    QCOMPARE(byId.value(QStringLiteral("unity:Diep")).folder, games + QStringLiteral("/Teknoparrot/Diep Map"));
    // Het eigen pad van de gebruiker gaat voor de mapnaam uit het meegeleverde bestand (dat vervangen is als geheel).
    QVERIFY(byId.contains(QStringLiteral("unity:Eigen")));
    QCOMPARE(byId.value(QStringLiteral("unity:Eigen")).folder, elders + QStringLiteral("/Eigen Plek"));
    // Kapot in de gebruikerslaag: geen terugval op de meegeleverde mapnaam (de map staat er wel), en een regel in het logboek.
    QVERIFY(!byId.contains(QStringLiteral("unity:Kapot")));
    QVERIFY(r.log.join(QLatin1Char('\n')).contains(QStringLiteral("Unity game Kapot: unity.json is broken")));
    // Zonder de gebruikerslaag: de meegeleverde bestanden.
    d.loadFrom(ravengun::gamesettings::Dirs{patches.toStdString(), {}});
    in.unityGames = d.unityGames();
    r = LibraryScanner::scan(in);
    byId.clear();
    for (const GameEntry& g : r.games) byId.insert(g.id, g);
    QVERIFY(byId.contains(QStringLiteral("unity:Kapot")));
    QVERIFY(!byId.contains(QStringLiteral("unity:Eigen")));   // "Bestaat Niet" staat in geen gamemap
}

void AppCoreTest::ownProfileOptionsWriteUserLayer() {
    const QString patches = writeOwnTc5(tmp_.filePath(QStringLiteral("opt-patches")));
    const QString user = tmp_.filePath(QStringLiteral("opt-user"));
    const QString tp = tmp_.filePath(QStringLiteral("opt-tp"));
    GameOptionsDb db;
    QVERIFY(loadFixtureOptions(db));
    const GameOptionsEntry* e = db.find(QStringLiteral("TC5"));
    GameContext c;
    c.profile = QStringLiteral("TC5");
    c.tpDir = tp;
    c.ownProfile = true;
    c.layers = ravengun::gamesettings::Dirs{patches.toStdString(), user.toStdString()};
    const QString userIni = user + QStringLiteral("/TC5/game.ini");
    // Profielveld (ConfigValues): de waarde van het profiel, schrijven naar de gebruikerslaag en alleen als het afwijkt.
    const GameOption& fp = e->options[0];
    bool present = false;
    QCOMPARE(OptionStore::read(fp, c, &present), QStringLiteral("0"));
    QVERIFY(present);
    QVERIFY(OptionStore::writable(fp, c));
    QCOMPARE(OptionStore::targetFile(fp.target, c), userIni);
    QString err;
    QVERIFY2(OptionStore::write(fp, c, QStringLiteral("1"), &err), qPrintable(err));
    QCOMPARE(OptionStore::read(fp, c), QStringLiteral("1"));
    QVERIFY(readAll(userIni).contains(QStringLiteral("General/FreePlay = 1")));
    QVERIFY(!QFileInfo::exists(tp + QStringLiteral("/UserProfiles/TC5.xml")));                 // de XML wordt niet aangemaakt of aangepast
    QVERIFY(readAll(patches + QStringLiteral("/TC5/game.ini")).contains(QStringLiteral("General/FreePlay = 0")));   // meegeleverd blijft staan
    QVERIFY2(OptionStore::write(fp, c, QStringLiteral("0"), &err), qPrintable(err));            // terug op de meegeleverde waarde: de sleutel verdwijnt
    QVERIFY(!readAll(userIni).contains(QStringLiteral("FreePlay")));
    // Een veld dat het profiel niet kent: niet schrijfbaar.
    GameOption unknown = fp;
    unknown.target.field = QStringLiteral("Bestaat Niet");
    QString reason;
    QVERIFY(!OptionStore::writable(unknown, c, &reason));
    QCOMPARE(reason, QStringLiteral("not in the profile of this game"));
    // Profielveld van het hoogste niveau (WineRunnerPath -> [Wine] Runner), en een omgevingsvariabele (TP_POSTSTART_REMOTETHREAD_MS -> [Loader] PostStartMs).
    const GameOption& wr = e->options[3];
    QVERIFY(OptionStore::write(wr, c, QStringLiteral("/opt/wine/bin/wine"), &err));
    QCOMPARE(OptionStore::read(wr, c), QStringLiteral("/opt/wine/bin/wine"));
    QVERIFY(readAll(userIni).contains(QStringLiteral("Runner = /opt/wine/bin/wine")));
    QVERIFY(OptionStore::write(wr, c, QString(), &err));
    QVERIFY(!readAll(userIni).contains(QStringLiteral("Runner")));
    const GameOption& ps = e->options[4];
    QVERIFY(OptionStore::write(ps, c, QStringLiteral("3000"), &err));
    QCOMPARE(OptionStore::read(ps, c), QStringLiteral("3000"));
    QVERIFY(readAll(userIni).contains(QStringLiteral("PostStartMs = 3000")));
    QVERIFY(!QFileInfo::exists(tp + QStringLiteral("/UserProfiles/TC5.env")));                  // geen .env
    // Een bool-omgevingsvariabele met een tweede variabele erbij (gamescope).
    const GameOption& gsc = e->options[5];
    QCOMPARE(OptionStore::read(gsc, c), QStringLiteral("0"));
    QVERIFY(OptionStore::write(gsc, c, QStringLiteral("1"), &err));
    QCOMPARE(OptionStore::read(gsc, c), QStringLiteral("1"));
    QVERIFY(readAll(userIni).contains(QStringLiteral("Gamescope = true")) && readAll(userIni).contains(QStringLiteral("GamescopePath = /usr/bin/gamescope")));
    QVERIFY(OptionStore::write(gsc, c, QStringLiteral("0"), &err));
    QVERIFY(!readAll(userIni).contains(QStringLiteral("Gamescope")));
    // Een meegeleverde waarde opheffen: een lege waarde in de gebruikerslaag.
    writeFile(patches + QStringLiteral("/TC5/game.ini"), readAll(patches + QStringLiteral("/TC5/game.ini")).toUtf8() + "[Display]\nGamescope = true\n");
    QCOMPARE(OptionStore::read(gsc, c), QStringLiteral("1"));
    QVERIFY(OptionStore::write(gsc, c, QStringLiteral("0"), &err));
    QCOMPARE(OptionStore::read(gsc, c), QStringLiteral("0"));
    QVERIFY(readAll(userIni).contains(QStringLiteral("Gamescope =\n")));
}

void AppCoreTest::dongleStatusText() {
    // De JSON van scripts/dongle.py --json (met ruis ervoor); nooit een serienummer in de uitvoer.
    const QByteArray ok =
        "warning: noise\n{\"dongles\": [{\"vendor_id\": \"0529\", \"product_id\": \"0001\", \"manufacturer\": \"Aladdin Knowledge Systems\", "
        "\"product\": \"HASP HL 3.25\", \"vendor_name\": \"Aladdin\", \"runtime\": \"sentinel-ldk\", \"runtime_status\": {\"name\": \"Sentinel LDK Runtime (Thales)\", "
        "\"status\": \"running\", \"how\": \"process\", \"install_url\": \"https://example.org/\"}}], \"error\": \"\", \"ok\": true, \"message\": \"Dongle detected\"}";
    DongleStatus st = parseDongleJson(ok);
    QVERIFY(st.valid && st.ok);
    QCOMPARE(st.dongles.size(), 1);
    QCOMPARE(st.text(), QStringLiteral("Dongle detected: Aladdin Knowledge Systems HASP HL 3.25 (0529:0001) \u2014 runtime: running"));
    const QByteArray none = "{\"dongles\": [], \"error\": \"\", \"ok\": false, \"message\": \"No dongle found (...)\"}";
    st = parseDongleJson(none);
    QVERIFY(st.valid && !st.ok);
    QCOMPARE(st.text(), QStringLiteral("No dongle found"));
    const QByteArray missing =
        "{\"dongles\": [{\"vendor_id\": \"064f\", \"product_id\": \"2af9\", \"manufacturer\": \"\", \"product\": \"\", \"vendor_name\": \"Wibu-Systems (CodeMeter)\", "
        "\"runtime\": \"codemeter\", \"runtime_status\": {\"name\": \"CodeMeter Runtime\", \"status\": \"not installed\", \"install_url\": \"https://www.wibu.com/\"}}], \"ok\": false, \"message\": \"x\"}";
    st = parseDongleJson(missing);
    QCOMPARE(st.text(), QStringLiteral("Dongle detected: Wibu-Systems (CodeMeter) (064f:2af9) \u2014 runtime: not installed"));
    QCOMPARE(st.dongles[0].installUrl, QStringLiteral("https://www.wibu.com/"));
    const QByteArray unknownRt = "{\"dongles\": [{\"vendor_id\": \"dead\", \"product_id\": \"beef\", \"manufacturer\": \"X\", \"product\": \"Y\", \"runtime_status\": null}], \"ok\": true}";
    QCOMPARE(parseDongleJson(unknownRt).text(), QStringLiteral("Dongle detected: X Y (dead:beef) \u2014 runtime: unknown"));
    QVERIFY(!parseDongleJson("geen json").valid);
    QVERIFY(!parseDongleJson("{kapot").valid);
    QCOMPARE(dongleSources().size(), 2);
    QCOMPARE(dongleSources()[0].value, QStringLiteral("usb-passthrough"));
    QCOMPARE(dongleSources()[1].value, QStringLiteral("drop-in"));
    QVERIFY(!dongleSources()[1].label.isEmpty());
    QCOMPARE(dongleSourceLabel(QStringLiteral("usb-passthrough")), QStringLiteral("USB pass-through (physical dongle)"));
}

void AppCoreTest::dongleSettingDifferencesOnly() {
    // [Dongle] volgt de regel van de gebruikerslaag: alleen verschillen met de meegeleverde laag (standaard uit, usb-passthrough).
    const QString patches = writeOwnTc5(tmp_.filePath(QStringLiteral("dg-patches")));
    const QString user = tmp_.filePath(QStringLiteral("dg-user"));
    GameContext c;
    c.profile = QStringLiteral("TC5");
    c.layers = ravengun::gamesettings::Dirs{patches.toStdString(), user.toStdString()};
    const QString userIni = user + QStringLiteral("/TC5/game.ini");
    QString err;
    auto dongle = [&] { return ravengun::gamesettings::dongle_of(ravengun::gamesettings::load_game(c.layers, "TC5").settings); };
    QVERIFY(!dongle().enabled);
    QVERIFY(OptionStore::writeSetting(c, QStringLiteral("Dongle"), QStringLiteral("Enabled"), QStringLiteral("true"), &err, QStringLiteral("false")));
    QVERIFY(readAll(userIni).contains(QStringLiteral("[Dongle]\nEnabled = true")));
    QVERIFY(dongle().enabled);
    QVERIFY(OptionStore::writeSetting(c, QStringLiteral("Dongle"), QStringLiteral("Source"), QStringLiteral("usb-passthrough"), &err, QStringLiteral("usb-passthrough")));
    QVERIFY(!readAll(userIni).contains(QStringLiteral("Source")));      // gelijk aan de standaard: niet in de gebruikerslaag
    QVERIFY(OptionStore::writeSetting(c, QStringLiteral("Dongle"), QStringLiteral("Enabled"), QStringLiteral("false"), &err, QStringLiteral("false")));
    QVERIFY(!readAll(userIni).contains(QStringLiteral("Enabled")));     // uit = de standaard: de sleutel verdwijnt
    QVERIFY(!dongle().enabled);
    // Een meegeleverde Enabled = true: uitzetten schrijft false (een verschil), aanzetten haalt het weer weg.
    writeFile(patches + QStringLiteral("/TC5/game.ini"), readAll(patches + QStringLiteral("/TC5/game.ini")).toUtf8() + "[Dongle]\nEnabled = true\n");
    QVERIFY(dongle().enabled);
    QVERIFY(OptionStore::writeSetting(c, QStringLiteral("Dongle"), QStringLiteral("Enabled"), QStringLiteral("false"), &err, QStringLiteral("false")));
    QVERIFY(readAll(userIni).contains(QStringLiteral("Enabled = false")));
    QVERIFY(!dongle().enabled);
    QVERIFY(OptionStore::writeSetting(c, QStringLiteral("Dongle"), QStringLiteral("Enabled"), QStringLiteral("true"), &err, QStringLiteral("false")));
    QVERIFY(!readAll(userIni).contains(QStringLiteral("Enabled")));
    QVERIFY(dongle().enabled);
}

void AppCoreTest::gameCameraChoice() {
    // De camerakeuze voor de spellen: de app schrijft hem in ravengun-app.conf ([Games] camera) en scripts/camera.py leest hem
    // bij een start. De echte aanroep van dat script laat zien dat beide kanten hetzelfde bestand en dezelfde sleutel bedoelen.
    QCOMPARE(rg::SystemControl::gameCamera(), QStringLiteral("auto"));   // niets gekozen
    rg::SystemControl::setGameCamera(QStringLiteral("/dev/video2"));
    QCOMPARE(rg::SystemControl::gameCamera(), QStringLiteral("/dev/video2"));
    QFile conf(rg::AppPaths::appSettingsFile());
    QVERIFY(conf.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(conf.readAll());
    QVERIFY2(text.contains(QStringLiteral("[Games]")) && text.contains(QStringLiteral("camera=/dev/video2")), qPrintable(text));
    // iets dat geen camera is komt er niet in
    rg::SystemControl::setGameCamera(QStringLiteral("/dev/sda"));
    QCOMPARE(rg::SystemControl::gameCamera(), QStringLiteral("auto"));
    rg::SystemControl::setGameCamera(QStringLiteral("none"));
    QCOMPARE(rg::SystemControl::gameCamera(), QStringLiteral("none"));

    // de lijst: per camera een regel (de tweede node van dezelfde camera niet), op nummer
    QTemporaryDir d;
    QVERIFY(d.isValid());
    const QString sys = d.filePath(QStringLiteral("sys"));
    const auto node = [&](const char* n, const char* name, const char* index) {
        writeFile(sys + QLatin1Char('/') + QLatin1String(n) + QStringLiteral("/name"), QByteArray(name) + "\n");
        writeFile(sys + QLatin1Char('/') + QLatin1String(n) + QStringLiteral("/index"), QByteArray(index) + "\n");
    };
    node("video0", "USB Camera", "0");
    node("video1", "USB Camera", "1");
    node("video10", "Tenth", "0");
    node("video2", "Second", "0");
    const auto cams = rg::SystemControl::cameras(sys);
    QCOMPARE(cams.size(), 3);
    QCOMPARE(cams.at(0), qMakePair(QStringLiteral("/dev/video0"), QStringLiteral("USB Camera")));
    QCOMPARE(cams.at(1).first, QStringLiteral("/dev/video2"));
    QCOMPARE(cams.at(2).first, QStringLiteral("/dev/video10"));
    QVERIFY(rg::SystemControl::cameras(d.filePath(QStringLiteral("missing"))).isEmpty());

    // het script leest de keuze van de app: een spel dat de camera in het spel gebruikt krijgt precies die camera
    rg::SystemControl::setGameCamera(QStringLiteral("/dev/video7"));
    const QString patches = d.filePath(QStringLiteral("patches"));
    writeFile(patches + QStringLiteral("/Spel/game.ini"), "[Camera]\nUse = game\n");
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("RAVENGUN_PATCHES_DIR"), patches);
    env.insert(QStringLiteral("RAVENGUN_USER_PATCHES_DIR"), d.filePath(QStringLiteral("user")));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("python3"), {QStringLiteral("-I"), QStringLiteral(RAVENGUN_SOURCE_DIR "/scripts/camera.py"), QStringLiteral("resolve"), QStringLiteral("Spel")});
    QVERIFY(p.waitForFinished(20000));
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    QVERIFY2(out.startsWith(QStringLiteral("/dev/video7\tgame\t")), qPrintable(out + QString::fromUtf8(p.readAllStandardError())));
    rg::SystemControl::setGameCamera(QStringLiteral("auto"));
}

void AppCoreTest::dongleDetectionRuns() {
    // De echte aanroep van scripts/dongle.py met een nep-sysfs: de app toont wat het script ziet (nooit het serienummer).
    QTemporaryDir d;
    QVERIFY(d.isValid());
    const QString sys = d.filePath(QStringLiteral("sys")), proc = d.filePath(QStringLiteral("proc")), root = d.filePath(QStringLiteral("root"));
    QDir().mkpath(proc);
    QDir().mkpath(root);
    writeFile(sys + QStringLiteral("/1-1/idVendor"), "0529\n");
    writeFile(sys + QStringLiteral("/1-1/idProduct"), "0001\n");
    writeFile(sys + QStringLiteral("/1-1/manufacturer"), "Aladdin Knowledge Systems\n");
    writeFile(sys + QStringLiteral("/1-1/product"), "HASP HL 3.25\n");
    writeFile(sys + QStringLiteral("/1-1/serial"), "SN-SECRET-0042\n");
    writeFile(proc + QStringLiteral("/77/comm"), "aksusbd\n");
    // Poorten dicht in een eigen gegevensbestand, zodat een echte License Manager op deze machine niets verandert.
    QFile vf(QStringLiteral(RAVENGUN_SOURCE_DIR "/packaging/dongle-vendors.json"));
    QVERIFY(vf.open(QIODevice::ReadOnly));
    QJsonObject vj = QJsonDocument::fromJson(vf.readAll()).object();
    QJsonObject rts = vj.value(QStringLiteral("runtimes")).toObject();
    for (const QString& k : rts.keys()) {
        QJsonObject r = rts.value(k).toObject();
        r.insert(QStringLiteral("tcp_port"), 1);   // poort 1: niemand luistert
        rts.insert(k, r);
    }
    vj.insert(QStringLiteral("runtimes"), rts);
    writeFile(d.filePath(QStringLiteral("vendors.json")), QJsonDocument(vj).toJson());
    qputenv("RAVENGUN_USB_SYSFS", sys.toUtf8());
    qputenv("RAVENGUN_PROC_ROOT", proc.toUtf8());
    qputenv("RAVENGUN_DONGLE_ROOT", root.toUtf8());
    qputenv("RAVENGUN_DONGLE_VENDORS", d.filePath(QStringLiteral("vendors.json")).toUtf8());
    qputenv("RAVENGUN_ROOT", QByteArray(RAVENGUN_SOURCE_DIR));
    DongleStatus st;
    auto run = [&] {
        bool done = false;
        detectDongle(this, [&](const DongleStatus& got) { st = got; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    };
    run();
    QVERIFY2(st.valid, qPrintable(st.error));
    QVERIFY(st.ok);
    QCOMPARE(st.text(), QStringLiteral("Dongle detected: Aladdin Knowledge Systems HASP HL 3.25 (0529:0001) \u2014 runtime: running"));
    QVERIFY(!st.text().contains(QStringLiteral("SECRET")) && !st.tooltip().contains(QStringLiteral("SECRET")));
    QFile::remove(proc + QStringLiteral("/77/comm"));
    run();
    QVERIFY(st.valid && !st.ok);
    QVERIFY(st.text().endsWith(QStringLiteral("runtime: not installed")));
    QVERIFY(st.tooltip().contains(QStringLiteral("does not install")));
    QDir(sys + QStringLiteral("/1-1")).removeRecursively();
    run();
    QVERIFY(st.valid && !st.ok);
    QCOMPARE(st.text(), QStringLiteral("No dongle found"));
    for (const char* v : {"RAVENGUN_USB_SYSFS", "RAVENGUN_PROC_ROOT", "RAVENGUN_DONGLE_ROOT", "RAVENGUN_DONGLE_VENDORS"}) qunsetenv(v);
}

void AppCoreTest::profileSyncOwnProfile() {
    const QString patches = writeOwnTc5(tmp_.filePath(QStringLiteral("sync2-patches")));
    const QString user = tmp_.filePath(QStringLiteral("sync2-user"));
    const QString tp = tmp_.filePath(QStringLiteral("sync2-tp"));
    QDir().mkpath(tp + QStringLiteral("/UserProfiles"));
    const QString libFile = tmp_.filePath(QStringLiteral("sync2-library.json"));
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
    o.tpFields.insert(QStringLiteral("General/Bestaat Niet"), QStringLiteral("x"));
    d.overrides.insert(g.id, o);
    QVERIFY(d.save(libFile));
    GameLibrary lib(libFile);
    lib.load();
    GameData data;
    data.loadFrom(ravengun::gamesettings::Dirs{patches.toStdString(), user.toStdString()});
    QStringList errors;
    QVERIFY2(ProfileSync::apply(g, lib, data, &errors), qPrintable(errors.join(QLatin1Char(';'))));
    const QString ini = readAll(user + QStringLiteral("/TC5/game.ini"));
    QVERIFY(ini.contains(QStringLiteral("General/FreePlay = 1")));
    QVERIFY(ini.contains(QStringLiteral("General/Windowed = 1")));
    QVERIFY(ini.contains(QStringLiteral("Arguments/Text Language = Spanish")));
    QVERIFY(ini.contains(QStringLiteral("Gamescope = true")) && ini.contains(QStringLiteral("GamescopePath = /usr/bin/gamescope")));
    QVERIFY(!ini.contains(QStringLiteral("Bestaat Niet")));
    // Geen UserProfiles-XML, geen .env.
    QVERIFY(QDir(tp + QStringLiteral("/UserProfiles")).entryList(QDir::Files).isEmpty());
    // De velden van het profiel voor Properties: de waarden die nu gelden, met het sjabloon voor hint en bereik.
    data.reloadGame(QStringLiteral("TC5"));
    const QList<TpField> fields = data.profileFields(QStringLiteral("TC5"), tp);
    QCOMPARE(fields.size(), 3);
    QCOMPARE(fields[2].value, QStringLiteral("Spanish"));
    QCOMPARE(fields[2].options, (QStringList{QStringLiteral("English"), QStringLiteral("Spanish")}));
    // Terug naar de meegeleverde waarden: de gebruikerslaag wordt weer leeg.
    LibraryData d2 = lib.data();
    d2.overrides[g.id].options.insert(QStringLiteral("general.freeplay"), QStringLiteral("0"));
    d2.overrides[g.id].options.insert(QStringLiteral("display.gamescope"), QStringLiteral("0"));
    d2.overrides[g.id].tpFields.insert(QStringLiteral("General/Windowed"), QStringLiteral("0"));
    d2.overrides[g.id].tpFields.insert(QStringLiteral("Arguments/Text Language"), QStringLiteral("English"));
    QVERIFY(d2.save(libFile));
    GameLibrary lib2(libFile);
    lib2.load();
    QVERIFY2(ProfileSync::apply(g, lib2, data, &errors), qPrintable(errors.join(QLatin1Char(';'))));
    const QString ini2 = readAll(user + QStringLiteral("/TC5/game.ini"));
    QVERIFY2(!ini2.contains(QStringLiteral("General/")) && !ini2.contains(QStringLiteral("Gamescope")), qPrintable(ini2));
}

void AppCoreTest::runtimeCheck() {
    const QString rt = tmp_.filePath(QStringLiteral("runtime"));
    RuntimeCheck c = RuntimeCheck::check(rt);
    QVERIFY(!c.ok);
    writeFile(rt + QStringLiteral("/ElfLdr2/BudgieLoader.exe"), "x");
    writeFile(rt + QStringLiteral("/GameProfiles/a.xml"), "x");
    c = RuntimeCheck::check(rt);
    QVERIFY(c.ok);   // zonder TeknoParrotUi: RavenGun start er niets mee, dus geen eis en niet als ontbrekend gemeld
    QVERIFY(!c.hasUi);
    QVERIFY(!c.summary.contains(QStringLiteral("TeknoParrotUi")) && !c.missing.join(QLatin1Char(' ')).contains(QStringLiteral("TeknoParrotUi")));
    writeFile(rt + QStringLiteral("/TeknoParrotUi"), "x");
    c = RuntimeCheck::check(rt);
    QVERIFY(c.ok && c.hasUi);
    // Backends (A10): auto en direct hebben alleen laders en profielen nodig; TeknoParrotUi start RavenGun niet meer.
    GameLibrary lib(tmp_.filePath(QStringLiteral("launcher-lib.json")));
    lib.setTpDir(tmp_.filePath(QStringLiteral("leeg")));
    QString why;
    for (const QString& id : backendIds()) {
        const auto b = makeBackend(id, nullptr, &lib, nullptr);
        QCOMPARE(b->id(), id);
        why.clear();
        QVERIFY(!b->available(&why));
        QVERIFY(!why.isEmpty());
    }
    QCOMPARE(backendIds(), (QStringList{QStringLiteral("auto"), QStringLiteral("direct")}));
    QCOMPARE(makeBackend(QStringLiteral("teknoparrot-ui"), nullptr, &lib, nullptr)->id(), QStringLiteral("auto"));   // oude keuze in library.json
    QCOMPARE(makeBackend(QString(), nullptr, &lib, nullptr)->id(), QStringLiteral("auto"));
    QCOMPARE(makeBackend(QStringLiteral("onbekend"), nullptr, &lib, nullptr)->id(), QStringLiteral("auto"));
    lib.setTpDir(rt);
    QVERIFY(makeBackend(QStringLiteral("direct"), nullptr, &lib, nullptr)->available(&why));
    QVERIFY(makeBackend(QStringLiteral("auto"), nullptr, &lib, nullptr)->available(&why));
    QFile::remove(rt + QStringLiteral("/TeknoParrotUi"));
    QVERIFY(makeBackend(QStringLiteral("direct"), nullptr, &lib, nullptr)->available(&why));
    // De startwijze die game_start meekrijgt.
    QCOMPARE(static_cast<EngineBackend*>(makeBackend(QStringLiteral("direct"), nullptr, &lib, nullptr).get())->launchMode(), QStringLiteral("direct"));
    QCOMPARE(static_cast<EngineBackend*>(makeBackend(QStringLiteral("auto"), nullptr, &lib, nullptr).get())->launchMode(), QString());
    QCOMPARE(static_cast<EngineBackend*>(makeBackend(QStringLiteral("teknoparrot-ui"), nullptr, &lib, nullptr).get())->launchMode(),
             QString());   // de oude keuze wordt auto: geen startwijze meer die de engine weigert
}

// --- A3: echte options.json-bestanden, invoerroute van de gebruiker, notities ------------------------------------------------

namespace {
QString repoFile(const QString& rel) { return QStringLiteral(RAVENGUN_SOURCE_DIR "/") + rel; }
// De meegeleverde patches: $RAVENGUN_PATCHES_DIR, anders patches/ in de repo (een symlink naar de ravengun-patches-repo).
QString realPatchesDir() {
    const QString env = qEnvironmentVariable("RAVENGUN_PATCHES_DIR");
    return env.isEmpty() ? repoFile(QStringLiteral("patches")) : env;
}
}  // namespace

void AppCoreTest::optionsLayers() {
    // patches/<Game>/options.json, zoals game.ini en native.json: de gebruikerslaag vervangt het meegeleverde bestand als geheel; een
    // kapot bestand geeft deze game geen opties en er is geen stille terugval op het meegeleverde bestand.
    const QString shipped = tmp_.filePath(QStringLiteral("opt-shipped"));
    const QString user = tmp_.filePath(QStringLiteral("opt-user-layer"));
    const QByteArray a = R"({"title":"Shipped A","options":[{"key":"o1","type":"bool"},{"key":"o2","type":"bool"}],"memory_card":[{"path":"save"}]})";
    const QByteArray b = R"({"title":"Shipped B","options":[{"key":"b1","type":"bool"}]})";
    writeFile(shipped + QStringLiteral("/GameA/options.json"), a);
    writeFile(shipped + QStringLiteral("/GameB/options.json"), b);
    writeFile(shipped + QStringLiteral("/GameC/game.ini"), "[Game]\nName = C\n");   // een game zonder options.json
    writeFile(shipped + QStringLiteral("/GameD/options.json"), "{kapot");
    GameData d;
    d.loadFrom(ravengun::gamesettings::Dirs{shipped.toStdString(), {}});
    QCOMPARE(d.options().entries().size(), 2);
    QCOMPARE(d.options().find(QStringLiteral("GameA"))->options.size(), 2);
    QCOMPARE(d.options().find(QStringLiteral("GameA"))->memoryCard.size(), 1);
    QCOMPARE(d.optionsFile(QStringLiteral("GameA")), shipped + QStringLiteral("/GameA/options.json"));
    QVERIFY(d.optionsFile(QStringLiteral("GameC")).isEmpty());
    QVERIFY(d.optionsError(QStringLiteral("GameC")).isEmpty());
    QVERIFY(!d.options().find(QStringLiteral("GameD")));   // kapot (ook zonder gebruikerslaag): geen opties, met een melding
    QVERIFY(d.optionsError(QStringLiteral("GameD")).contains(QStringLiteral("GameD/options.json")));
    // De gebruikerslaag vervangt het bestand als geheel (geen samenvoeging); een game die alleen daar staat, telt ook.
    writeFile(user + QStringLiteral("/GameA/options.json"), R"({"title":"User A","options":[{"key":"u1","type":"int"}]})");
    writeFile(user + QStringLiteral("/GameE/options.json"), R"({"options":[{"key":"e1","type":"bool"}]})");
    d.loadFrom(ravengun::gamesettings::Dirs{shipped.toStdString(), user.toStdString()});
    const GameOptionsEntry* ua = d.options().find(QStringLiteral("GameA"));
    QVERIFY(ua);
    QCOMPARE(ua->title, QStringLiteral("User A"));
    QCOMPARE(ua->options.size(), 1);
    QCOMPARE(ua->options[0].key, QStringLiteral("u1"));
    QVERIFY(ua->memoryCard.isEmpty());                                   // niet uit het meegeleverde bestand overgenomen
    QCOMPARE(d.optionsFile(QStringLiteral("GameA")), user + QStringLiteral("/GameA/options.json"));
    QCOMPARE(d.options().find(QStringLiteral("GameB"))->title, QStringLiteral("Shipped B"));
    QVERIFY(d.options().find(QStringLiteral("GameE")));
    // Kapot in de gebruikerslaag: geen opties voor deze game, en het meegeleverde bestand telt niet.
    writeFile(user + QStringLiteral("/GameB/options.json"), "{\"options\": [");
    d.loadFrom(ravengun::gamesettings::Dirs{shipped.toStdString(), user.toStdString()});
    QVERIFY(!d.options().find(QStringLiteral("GameB")));
    QVERIFY(d.optionsError(QStringLiteral("GameB")).contains(user + QStringLiteral("/GameB/options.json")));
    QCOMPARE(d.optionsFile(QStringLiteral("GameB")), user + QStringLiteral("/GameB/options.json"));
    QVERIFY(d.options().find(QStringLiteral("GameA")));                  // de andere games blijven werken
    // Een leeg bestand in de gebruikerslaag is ook kapot (geen JSON); `{}` is geldig en betekent geen opties.
    writeFile(user + QStringLiteral("/GameB/options.json"), "");
    d.loadFrom(ravengun::gamesettings::Dirs{shipped.toStdString(), user.toStdString()});
    QVERIFY(!d.optionsError(QStringLiteral("GameB")).isEmpty());
    writeFile(user + QStringLiteral("/GameB/options.json"), "{}");
    d.loadFrom(ravengun::gamesettings::Dirs{shipped.toStdString(), user.toStdString()});
    QVERIFY(d.optionsError(QStringLiteral("GameB")).isEmpty());
    QVERIFY(d.options().find(QStringLiteral("GameB"))->options.isEmpty());
    // De bibliotheek-scan kent de games met een nas_folder uit deze bestanden.
    writeFile(shipped + QStringLiteral("/GameC/options.json"), R"({"title":"C Title","nas_folder":"Game C Folder","platform":"P","exe":"bin/c.exe"})");
    d.loadFrom(ravengun::gamesettings::Dirs{shipped.toStdString(), {}});
    const QList<KnownGame> known = d.knownGames();
    QCOMPARE(known.size(), 1);
    QCOMPARE(known[0].folder, QStringLiteral("Game C Folder"));
    QCOMPARE(known[0].profile, QStringLiteral("GameC"));
    QCOMPARE(known[0].exe, QStringLiteral("bin/c.exe"));
}


void AppCoreTest::realGameOptions() {
    // De echte bestanden: patches/<Game>/options.json van de meegeleverde laag.
    const QString dir = realPatchesDir();
    GameData data;
    data.loadFrom(ravengun::gamesettings::Dirs{dir.toStdString(), {}});
    for (const std::string& game : ravengun::gamesettings::list_games(ravengun::gamesettings::Dirs{dir.toStdString(), {}}))
        QVERIFY2(data.optionsError(QString::fromStdString(game)).isEmpty(), qPrintable(data.optionsError(QString::fromStdString(game))));
    const GameOptionsDb& db = data.options();
    if (db.isEmpty()) QSKIP(qPrintable(QStringLiteral("no options.json under %1 (tools/dev/split-game-options.py makes them)").arg(dir)));
    QVERIFY(db.entries().size() >= 20);
    int options = 0, items = 0, editable = 0, readOnly = 0;
    for (const GameOptionsEntry& e : db.entries()) {
        options += static_cast<int>(e.options.size());
        items += static_cast<int>(e.memoryCard.size());
        for (const GameOption& o : e.options) {
            QVERIFY2(!o.key.isEmpty() && !o.name.isEmpty(), qPrintable(e.profile));
            QVERIFY2(o.type == QStringLiteral("bool") || o.type == QStringLiteral("int") || o.type == QStringLiteral("enum") ||
                         o.type == QStringLiteral("string"),
                     qPrintable(e.profile + QLatin1Char('/') + o.key));
            if (o.target.kind == OptionTarget::Kind::Unsupported) {
                QVERIFY2(!o.target.readOnlyReason.isEmpty(), qPrintable(e.profile + QLatin1Char('/') + o.key));  // altijd met reden
                ++readOnly;
            } else {
                ++editable;
            }
        }
    }
    QVERIFY2(db.warnings().isEmpty(), qPrintable(db.warnings().join(QLatin1Char('\n'))));
    QVERIFY(editable > 300);
    qInfo("options.json: %d games, %d options (%d editable, %d read-only), %d memory-card items", static_cast<int>(db.entries().size()),
          options, editable, readOnly, items);
    // Steekproef: profielveld, element, env met companion, INI met woorden (ON/OFF).
    const GameOptionsEntry* tc5 = db.find(QStringLiteral("TC5"));
    QVERIFY(tc5);
    QCOMPARE(tc5->options[0].target.kind, OptionTarget::Kind::TpField);
    // Een native game (Halo) toont geen TeknoParrot-profielvelden: de native route leest het profiel niet.
    const GameOptionsEntry* halo = db.find(QStringLiteral("Halo"));
    QVERIFY(halo);
    for (const GameOption& o : halo->options) QVERIFY2(o.target.kind != OptionTarget::Kind::TpField, qPrintable(o.key));
    const GameOptionsEntry* tsr = db.find(QStringLiteral("TransformersShadowsRising"));
    QVERIFY(tsr);
    bool sawOnOff = false;
    for (const GameOption& o : tsr->options)
        if (o.key == QStringLiteral("gs.revival")) {
            sawOnOff = true;
            QCOMPARE(o.target.kind, OptionTarget::Kind::Ini);
            QCOMPARE(o.trueValue, QStringLiteral("ON"));
            QCOMPARE(o.defaultValue, QStringLiteral("ON"));
        }
    QVERIFY(sawOnOff);
}

void AppCoreTest::extraIniFormats() {
    const QString dir = tmp_.filePath(QStringLiteral("formats"));
    writeFile(dir + QStringLiteral("/MINIGUN.INI"), "REGION\tE\r\nLANG\tEN\r\n");
    writeFile(dir + QStringLiteral("/op.conf"), "Language: EN\nVolume: 5\n");
    writeFile(dir + QStringLiteral("/GameSettings.ini"), "[GameSettings]\nREVIVAL=ON\n");
    const QByteArray json = R"({"options":[
      {"key":"r","type":"enum","values":["A","E"],"target":{"kind":"game_config_file","path":"MINIGUN.INI","format":"tab_ini","key":"REGION"}},
      {"key":"l","type":"enum","values":["EN","FR"],"target":{"kind":"game_config_file","path":"op.conf","format":"colon","key":"Language"}},
      {"key":"v","type":"bool","default":true,"target":{"kind":"game_config_file","path":"GameSettings.ini","format":"ini_onoff","section":"GameSettings","key":"REVIVAL"}},
      {"key":"a","type":"int","target":{"kind":"game_config_file","path":"x.txt","format":"csv","key":"a"}}]})";
    GameOptionsDb db;
    QVERIFY(db.loadGameData(QStringLiteral("X"), json));
    const GameOptionsEntry* e = db.find(QStringLiteral("X"));
    QVERIFY(e && e->options.size() == 4);
    GameContext c;
    c.profile = QStringLiteral("X");
    c.rootDir = dir;
    c.gameDir = dir;
    QString err;
    QCOMPARE(OptionStore::read(e->options[0], c), QStringLiteral("E"));
    QVERIFY2(OptionStore::write(e->options[0], c, QStringLiteral("A"), &err), qPrintable(err));
    QCOMPARE(readAll(dir + QStringLiteral("/MINIGUN.INI")), QStringLiteral("REGION\tA\r\nLANG\tEN\r\n"));  // tab en CRLF blijven
    QCOMPARE(OptionStore::read(e->options[1], c), QStringLiteral("EN"));
    QVERIFY(OptionStore::write(e->options[1], c, QStringLiteral("FR"), &err));
    QCOMPARE(readAll(dir + QStringLiteral("/op.conf")), QStringLiteral("Language: FR\nVolume: 5\n"));      // spatie na ':' blijft
    QCOMPARE(OptionStore::read(e->options[2], c), QStringLiteral("ON"));
    QVERIFY(OptionStore::write(e->options[2], c, e->options[2].falseValue, &err));
    QVERIFY(readAll(dir + QStringLiteral("/GameSettings.ini")).contains(QStringLiteral("REVIVAL=OFF")));
    QString reason;
    QVERIFY(!OptionStore::writable(e->options[3], c, &reason));
    QVERIFY(reason.contains(QStringLiteral("test menu")) || reason.contains(QStringLiteral("csv")));  // rtdt_adjval is wel schrijfbaar: optionStoreAdjVal
}

namespace {
ravengun::gamesettings::Dirs repoDirs(const QString& user = {}) {
    return ravengun::gamesettings::Dirs{QStringLiteral(RAVENGUN_SOURCE_DIR "/patches").toStdString(), user.toStdString()};
}
}  // namespace

void AppCoreTest::routeOverride() {
    namespace gs = ravengun::gamesettings;
    const QString user = tmp_.filePath(QStringLiteral("routes-user"));
    GameData d;
    d.setRouteSources(repoFile(QStringLiteral("packaging/hid-boards.json")), repoFile(QStringLiteral("packaging/jvs-boards.json")));
    // TombRaider is een Unity-game: patches/TombRaider/unity.json (hier een bestand in de gebruikerslaag, zodat de test de patches-repo niet nodig heeft).
    writeFile(user + QStringLiteral("/TombRaider/unity.json"), R"({"name":"Tomb Raider Arcade","type":"tomb_raider","folder":"Tomb Raider"})");
    d.loadFrom(repoDirs(user));
    QCOMPARE(d.route(QStringLiteral("TC5")).route, QStringLiteral("tp-shm"));
    QVERIFY(!d.route(QStringLiteral("TC5")).userChoice);
    // Keuzes per game: de aanbevolen route eerst, alleen routes waarvoor er een indeling of bord is.
    QCOMPARE(d.availableRoutes(QStringLiteral("TC5")), (QStringList{QStringLiteral("tp-shm"), QStringLiteral("evdev")}));
    QCOMPARE(d.availableRoutes(QStringLiteral("Halo")), (QStringList{QStringLiteral("tp-shm"), QStringLiteral("evdev")}));
    // Jurassic Park: tp-shm aanbevolen sinds de RIO-brug het lightgun-bord uit het bordbeeld van de daemon beantwoordt (2026-10-14).
    QCOMPARE(d.availableRoutes(QStringLiteral("JurassicPark")), (QStringList{QStringLiteral("tp-shm"), QStringLiteral("evdev")}));
    QVERIFY(d.availableRoutes(QStringLiteral("TransformersShadowsRising")).contains(QStringLiteral("hid-board")));
    QVERIFY(d.availableRoutes(QStringLiteral("LGS")).contains(QStringLiteral("jvs")));
    QCOMPARE(d.availableRoutes(QStringLiteral("TombRaider")), QStringList{QStringLiteral("unity-tcp")});
    QCOMPARE(d.availableRoutes(QStringLiteral("OnbekendeGame")), QStringList{QStringLiteral("evdev")});
    // Kiezen: het gebruikers-game.ini ontstaat; de meegeleverde laag (patches/ in de repo) verandert niet.
    QString err;
    QVERIFY2(d.setUserRoute(QStringLiteral("TC5"), QStringLiteral("evdev"), &err), qPrintable(err));
    QCOMPARE(d.route(QStringLiteral("TC5")).route, QStringLiteral("evdev"));
    QVERIFY(d.route(QStringLiteral("TC5")).userChoice);
    QCOMPARE(d.route(QStringLiteral("TC5")).layout, QStringLiteral("TimeCrisis5"));  // de rest uit de meegeleverde laag
    QCOMPARE(d.route(QStringLiteral("TC5")).baseRoute, QStringLiteral("tp-shm"));
    QCOMPARE(gs::read_layer(user.toStdString(), "TC5").get("Input", "Route"), std::string("evdev"));
    QCOMPARE(readAll(user + QStringLiteral("/TC5/game.ini")), QStringLiteral("[Input]\nRoute = evdev\n"));
    QVERIFY(d.setUserRoute(QStringLiteral("Halo"), QStringLiteral("evdev"), &err));
    // Een tweede GameData (een herstart van de app) leest dezelfde keuzes.
    GameData d2;
    d2.setRouteSources({}, {});
    d2.loadFrom(repoDirs(user));
    QCOMPARE(d2.route(QStringLiteral("TC5")).route, QStringLiteral("evdev"));
    QCOMPARE(d2.route(QStringLiteral("Halo")).route, QStringLiteral("evdev"));
    // Niet beschikbaar: geweigerd. Terug naar de aanbevolen route: de keuze verdwijnt uit het bestand.
    QVERIFY(!d.setUserRoute(QStringLiteral("TC5"), QStringLiteral("hid-board"), &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(d.setUserRoute(QStringLiteral("TC5"), QStringLiteral("tp-shm"), &err));
    QCOMPARE(d.route(QStringLiteral("TC5")).route, QStringLiteral("tp-shm"));
    QVERIFY(!d.route(QStringLiteral("TC5")).userChoice);
    QVERIFY(!gs::read_layer(user.toStdString(), "TC5").has("Input", "Route"));
    QVERIFY(gs::read_layer(user.toStdString(), "Halo").has("Input", "Route"));
    QVERIFY(d.setUserRoute(QStringLiteral("Halo"), QString(), &err));
    QVERIFY(!gs::read_layer(user.toStdString(), "Halo").has("Input", "Route"));
    // Handwerk in het bestand blijft staan als de app een route kiest.
    writeFile(user + QStringLiteral("/Halo/game.ini"), "; mijn notitie\n[Env]\nFOO = bar\n");
    QVERIFY(d.setUserRoute(QStringLiteral("Halo"), QStringLiteral("evdev"), &err));
    const QString kept = readAll(user + QStringLiteral("/Halo/game.ini"));
    QVERIFY(kept.contains(QStringLiteral("; mijn notitie")) && kept.contains(QStringLiteral("FOO = bar")) && kept.contains(QStringLiteral("Route = evdev")));
    // Zonder gebruikersmap kan er niets gekozen worden.
    GameData d3;
    d3.loadFrom(repoDirs());
    QVERIFY(!d3.setUserRoute(QStringLiteral("TC5"), QStringLiteral("evdev"), &err));
}

void AppCoreTest::routeOverrideInvalid() {
    // Zelfde regels als de engine: een gebruikerslaag die de route ongeldig maakt, telt voor die game niet mee.
    const QString user = tmp_.filePath(QStringLiteral("routes-bad"));
    writeFile(user + QStringLiteral("/TC5/game.ini"), "[Input]\nRoute = onzin\n");
    writeFile(user + QStringLiteral("/Nieuw/game.ini"), "[Input]\nRoute = tp-shm\n");   // zonder Layout
    GameData d;
    d.loadFrom(repoDirs(user));
    QCOMPARE(d.route(QStringLiteral("TC5")).route, QStringLiteral("tp-shm"));
    QVERIFY(!d.userRoutesError().isEmpty());
    QCOMPARE(d.route(QStringLiteral("Nieuw")).route, QStringLiteral("evdev"));
    writeFile(user + QStringLiteral("/TC5/game.ini"), "[Input]\nRoute = hid-board\n");   // zonder Board
    d.loadFrom(repoDirs(user));
    QCOMPARE(d.route(QStringLiteral("TC5")).route, QStringLiteral("tp-shm"));
    QVERIFY(!d.userRoutesError().isEmpty());
    QFile::remove(user + QStringLiteral("/TC5/game.ini"));
    QFile::remove(user + QStringLiteral("/Nieuw/game.ini"));
    writeFile(user + QStringLiteral("/NieuweGame/game.ini"), "[Input]\nRoute = evdev\n");
    d.loadFrom(repoDirs(user));
    QVERIFY(d.userRoutesError().isEmpty());
    QCOMPARE(d.route(QStringLiteral("NieuweGame")).route, QStringLiteral("evdev"));
}

void AppCoreTest::notesEnglish() {
    namespace gs = ravengun::gamesettings;
    GameData d;
    d.loadFrom(repoDirs());
    // De app toont Engels (NoteEN); de Nederlandse tekst (Note) blijft voor de webpagina en de engine. Vergeleken met game.ini zelf:
    // de teksten en de status veranderen met elke bench-run, het mechanisme niet.
    for (const char* name : {"TC5", "Halo"}) {
        const gs::Game g = gs::load_game(repoDirs(), name);
        const QString en = QString::fromStdString(g.get("Status", "NoteEN")), nl = QString::fromStdString(g.get("Status", "Note"));
        QVERIFY(!en.isEmpty() && en != nl);
        QVERIFY(d.note(QString::fromLatin1(name)).startsWith(en.left(40)));
        QCOMPARE(d.playState(QString::fromLatin1(name)), QString::fromStdString(g.get("Status", "Status")));
    }
    QCOMPARE(d.playState(QStringLiteral("GhostBusters")), QStringLiteral("parked"));
    QCOMPARE(d.playState(QStringLiteral("NoSuchGameProfile")), QStringLiteral("untested"));  // zonder status
    const gs::Dirs dirs = repoDirs();
    int withNote = 0;
    for (const std::string& name : gs::list_games(dirs)) {
        const gs::Game g = gs::load_game(dirs, name);
        const std::string nl = g.get("Status", "Note"), en = g.get("Status", "NoteEN"), st = g.get("Status", "Status");
        QCOMPARE(nl.empty(), en.empty());                                   // elke notitie in beide talen
        if (!st.empty()) QVERIFY(!nl.empty());                              // een status zonder notitie bestaat niet
        QVERIFY(st.empty() || st == "working" || st == "playable" || st == "parked" || st == "untested");
        if (!nl.empty()) ++withNote;
    }
    QVERIFY(withNote >= 28);
    // Zonder Engelse notitie geldt de Nederlandse tekst uit game.ini; het options.json van de game speelt geen rol meer.
    const QString shipped = tmp_.filePath(QStringLiteral("notes-nl"));
    writeFile(shipped + QStringLiteral("/TC5/game.ini"), "[Status]\nNote = Nederlands\n");
    QFile::copy(dataFile(QStringLiteral("patches/TC5/options.json")), shipped + QStringLiteral("/TC5/options.json"));
    GameData d3;
    d3.loadFrom(gs::Dirs{shipped.toStdString(), {}});
    QVERIFY(!d3.options().isEmpty());
    QCOMPARE(d3.note(QStringLiteral("TC5")), QStringLiteral("Nederlands"));
    // De gebruikerslaag gaat voor.
    writeFile(tmp_.filePath(QStringLiteral("notes-user/TC5/game.ini")), "[Status]\nNoteEN = My note\n");
    GameData d4;
    d4.loadFrom(gs::Dirs{shipped.toStdString(), tmp_.filePath(QStringLiteral("notes-user")).toStdString()});
    QCOMPARE(d4.note(QStringLiteral("TC5")), QStringLiteral("My note"));
}

void AppCoreTest::patchesAndSettings() {
    namespace gs = ravengun::gamesettings;
    const QString user = tmp_.filePath(QStringLiteral("patches-user"));
    GameData d;
    d.loadFrom(repoDirs(user));
    // De lader-patch voor Type X2: gedeeld gedefinieerd, door de game aangezet.
    const QString name = QStringLiteral("OpenParrotLoader zonder CONTEXT_FLOATING_POINT");
    QList<PatchInfo> p = d.patches(QStringLiteral("GaiaAttack4"));
    QCOMPARE(p.size(), 3);   // de lader-patch en twee geheugenpatches (informatie)
    QCOMPARE(p[0].name, name);
    QVERIFY(p[0].enabled && p[0].shippedEnabled && !p[0].userSet && !p[0].userDefined);
    QCOMPARE(p[0].kind, QStringLiteral("file"));
    QVERIFY(p[0].reason.contains(QStringLiteral("MXCSR")));
    QCOMPARE(p[0].category, QStringLiteral("loader"));
    // De geheugenpatches uit OpenParrot staan er alleen ter informatie bij: uit, door de kern toegepast, met bron en versie.
    const QList<PatchInfo> hm2 = d.patches(QStringLiteral("HauntedMuseumII"));
    int info = 0;
    for (const PatchInfo& i : hm2)
        if (i.kind == QStringLiteral("memory")) {
            ++info;
            QVERIFY(!i.enabled && i.appliedByCore && !i.version.isEmpty() && i.source.contains(QStringLiteral("TypeX2Generic.cpp")));
        }
    QCOMPARE(info, 6);   // drie per build: versie 2100 (onze dump) en 2101J (andere build, alleen documentatie)
    QVERIFY(d.patchErrors(QStringLiteral("GaiaAttack4")).isEmpty());
    QVERIFY(d.patches(QStringLiteral("TC5")).isEmpty());
    // Uitzetten: het gebruikers-patches.ini krijgt Enabled = false; de meegeleverde stand blijft zichtbaar.
    QString err;
    QVERIFY2(d.setPatchEnabled(QStringLiteral("GaiaAttack4"), name, false, &err), qPrintable(err));
    p = d.patches(QStringLiteral("GaiaAttack4"));
    QVERIFY(!p[0].enabled && p[0].shippedEnabled && p[0].userSet);
    QCOMPARE(readAll(user + QStringLiteral("/GaiaAttack4/patches.ini")), QStringLiteral("[Patch: %1]\nEnabled = false\n").arg(name));
    QVERIFY(!gs::load_game(repoDirs(user), "GaiaAttack4").patches[0].enabled());   // de lezers (engine, launchers) zien hetzelfde
    // Weer aanzetten = gelijk aan de meegeleverde stand: de keuze verdwijnt.
    QVERIFY(d.setPatchEnabled(QStringLiteral("GaiaAttack4"), name, true, &err));
    p = d.patches(QStringLiteral("GaiaAttack4"));
    QVERIFY(p[0].enabled && !p[0].userSet);
    QVERIFY(!gs::read_layer(user.toStdString(), "GaiaAttack4", "patches.ini").has("Patch: " + name.toStdString(), "Enabled"));
    // Een patch die alleen de gebruiker kent; en de gedeelde definitie van een game die hem niet aanzet staat er ook (uit).
    writeFile(user + QStringLiteral("/NERF/patches.ini"), "[Patch: Eigen]\nEnabled = true\nKind = file\nCategory = crash\nTarget = core\nFind = 00\nReplace = 01\n");
    d.loadFrom(repoDirs(user));
    p = d.patches(QStringLiteral("NERF"));
    QCOMPARE(p.size(), 1);
    QVERIFY(p[0].enabled && p[0].userDefined && p[0].userSet);
    // Een eigen patch zonder Category wordt afgewezen en als melding getoond.
    writeFile(user + QStringLiteral("/TC5/patches.ini"), "[Patch: Zonder]\nEnabled = true\nKind = file\n");
    d.loadFrom(repoDirs(user));
    QVERIFY(d.patches(QStringLiteral("TC5")).isEmpty());
    QVERIFY(d.patchErrors(QStringLiteral("TC5")).value(0).contains(QStringLiteral("Category ontbreekt")));
    QFile::remove(user + QStringLiteral("/TC5/patches.ini"));
    // Zonder gebruikersmap geen schrijven.
    GameData ro;
    ro.loadFrom(repoDirs());
    QVERIFY(!ro.setPatchEnabled(QStringLiteral("GaiaAttack4"), name, false, &err));
    // Effectieve instellingen met de laag waar ze vandaan komen.
    writeFile(user + QStringLiteral("/GaiaAttack4/game.ini"), "[Env]\nFOO = bar\n[Input]\nRoute = evdev\n");
    d.loadFrom(repoDirs(user));
    QHash<QString, QString> layers;
    for (const SettingEntry& e : d.settings(QStringLiteral("GaiaAttack4"))) layers.insert(e.section + QLatin1Char('.') + e.key, e.layer);
    QCOMPARE(layers.value(QStringLiteral("Wine.DllOverrides")), QStringLiteral("default"));   // van het soort OpenParrot
    QCOMPARE(layers.value(QStringLiteral("Input.Layout")), QStringLiteral("shipped"));
    QCOMPARE(layers.value(QStringLiteral("Input.Route")), QStringLiteral("user"));
    QCOMPARE(layers.value(QStringLiteral("Env.FOO")), QStringLiteral("user"));
    QCOMPARE(d.userGameDir(QStringLiteral("GaiaAttack4")), user + QStringLiteral("/GaiaAttack4"));
    QVERIFY(QFileInfo(d.shippedGameDir(QStringLiteral("GaiaAttack4"))).isDir());
}

// --- A7: fullscreen-logica --------------------------------------------------------------------------------------------

void AppCoreTest::bigPictureLayoutAndHit() {
    using namespace bigpicture;
    const QRectF area(96, 140, 1728, 712);
    const Layout l = layout(area, 6, 300, 110);
    QCOMPARE(l.tiles.size(), 6);
    QVERIFY(l.tile >= 200);  // zes games: grote iconen
    for (int i = 0; i < l.tiles.size(); ++i) {
        QVERIFY2(area.contains(l.tiles[i]), qPrintable(QString::number(i)));
        for (int j = i + 1; j < l.tiles.size(); ++j) QVERIFY(!l.tiles[i].intersects(l.tiles[j]));
        QCOMPARE(hitTest(l.tiles, l.tiles[i].center()), i);  // vizier in het midden: die tegel
    }
    // Iets naast de rand (marge) telt nog mee; ver ernaast niet; de titel onder het icoon hoort erbij.
    QCOMPARE(hitTest(l.tiles, l.tiles[0].topLeft() - QPointF(6, 6), 12), 0);
    QCOMPARE(hitTest(l.tiles, QPointF(1, 1)), -1);
    QCOMPARE(hitTest(l.tiles, QPointF(l.tiles[0].center().x(), l.tiles[0].bottom() + l.tiles[0].height() * 0.2)), 0);
    // Veel games: kleinere tegels, alles nog in beeld; 37 games past op een 1080p-tv.
    const Layout many = layout(area, 37, 300, 110);
    QCOMPARE(many.tiles.size(), 37);
    for (const QRectF& r : many.tiles) QVERIFY(!r.isNull() && area.contains(r));
    // Te veel voor het scherm: rijen buiten beeld krijgen een lege rect, en scrollen schuift.
    const Layout tooMany = layout(QRectF(0, 0, 800, 300), 40, 300, 110);
    int hidden = 0;
    for (const QRectF& r : tooMany.tiles) hidden += r.isNull() ? 1 : 0;
    QVERIFY(hidden > 0);
    QCOMPARE(hitTest(tooMany.tiles, QPointF(-50, -50)), -1);
    // Geen games of geen ruimte: niets.
    QVERIFY(layout(area, 0).tiles.isEmpty());
    QVERIFY(layout(QRectF(), 3).tiles.isEmpty());
}

void AppCoreTest::bigPictureNavigate() {
    using namespace bigpicture;
    // 7 games in 3 kolommen: 0 1 2 / 3 4 5 / 6
    QCOMPARE(navigate(0, Move::Right, 3, 7), 1);
    QCOMPARE(navigate(0, Move::Left, 3, 7), 0);
    QCOMPARE(navigate(1, Move::Down, 3, 7), 4);
    QCOMPARE(navigate(4, Move::Down, 3, 7), 6);   // naar het laatste item in de onvolledige rij
    QCOMPARE(navigate(6, Move::Down, 3, 7), 6);   // onderste rij: blijft (het venster gaat dan naar de knoppen)
    QCOMPARE(navigate(6, Move::Up, 3, 7), 3);
    QCOMPARE(navigate(6, Move::Right, 3, 7), 6);
    QCOMPARE(navigate(-1, Move::Right, 3, 7), 0);
    QCOMPARE(navigate(0, Move::Right, 3, 0), -1);
    QCOMPARE(scrollRowFor(9, 3, 2, 0), 2);         // rij 3 zichtbaar maken bij 2 zichtbare rijen
    QCOMPARE(scrollRowFor(0, 3, 2, 2), 0);
    QCOMPARE(scrollRowFor(4, 3, 2, 1), 1);
}

void AppCoreTest::shotConfirm() {
    using bigpicture::ShotConfirm;
    ShotConfirm c;
    // Eerste schot op tegel 2 kiest hem (armed), tweede schot binnen de tijd start hem.
    auto r = c.update(2, true, 1000);
    QCOMPARE(r.armed, 2);
    QCOMPARE(r.launch, -1);
    r = c.update(2, false, 1100);
    QCOMPARE(r.launch, -1);
    r = c.update(2, true, 1900);
    QCOMPARE(r.launch, 2);
    r = c.update(2, false, 2000);
    QCOMPARE(r.armed, -1);
    // Tweede schot op een andere tegel: die wordt gekozen, er start niets.
    c.reset();
    c.update(1, true, 0);
    c.update(1, false, 50);
    r = c.update(3, true, 400);
    QCOMPARE(r.launch, -1);
    QCOMPARE(r.armed, 3);
    c.update(3, false, 450);
    // Te laat (na kConfirmMs vanaf het loslaten): opnieuw kiezen.
    r = c.update(3, true, 450 + ShotConfirm::kConfirmMs + 10);
    QCOMPARE(r.launch, -1);
    QCOMPARE(r.armed, 3);
    c.update(3, false, 5000);
    // Vasthouden: na kHoldMs start hij, met voortgang ertussen; maar één keer per druk.
    c.reset();
    c.update(0, true, 10000);
    r = c.update(0, true, 10500);
    QVERIFY(r.hold > 0.4 && r.hold < 0.6);
    QCOMPARE(r.launch, -1);
    r = c.update(0, true, 10000 + ShotConfirm::kHoldMs);
    QCOMPARE(r.launch, 0);
    r = c.update(0, true, 12000);
    QCOMPARE(r.launch, -1);
    c.update(0, false, 12100);
    // Vizier verlaat de tegel tijdens het vasthouden: geen start.
    c.reset();
    c.update(4, true, 20000);
    c.update(-1, true, 20300);
    r = c.update(4, true, 21500);
    QCOMPARE(r.launch, -1);
    c.update(4, false, 21600);
    // Mis (naast elke tegel): de keuze vervalt.
    c.reset();
    c.update(5, true, 30000);
    c.update(5, false, 30050);
    r = c.update(-1, true, 30500);
    QCOMPARE(r.armed, -1);
    c.update(-1, false, 30600);
    r = c.update(5, true, 30700);
    QCOMPARE(r.launch, -1);  // opnieuw kiezen nodig
}

void AppCoreTest::aimEvents() {
    using bigpicture::AimState;
    AimState s;
    // Compact (watch met lite).
    QJsonObject lite = QJsonDocument::fromJson(R"({"v":1,"event":"aim","data":{"seq":5,"link":"fresh","aim":{"valid":true,"x":0.25,"y":1.3,
        "off_screen":true},"trigger":200,"down":["t","ps"],"pressed":{"t":2},"released":{}}})").object();
    QVERIFY(bigpicture::parseAimEvent(lite, &s));
    QVERIFY(s.valid && s.trigger && s.ps && s.offScreen);
    QCOMPARE(s.pos, QPointF(0.25, 1.0));  // geklemd
    QCOMPARE(s.triggerPresses, 2);
    // Volledig sample (oudere engine zonder lite).
    QJsonObject full = QJsonDocument::fromJson(R"({"v":1,"event":"sample","data":{"sample":{"link":"fresh","aim":{"valid":true,
        "x":0.5,"y":0.5,"x_clamped":0.6,"y_clamped":0.4,"off_screen":false},"buttons":{"down":["move"]}},
        "edges_since_last_frame":{"pressed":{"t":1},"released":{"t":1}}}})").object();
    QVERIFY(bigpicture::parseAimEvent(full, &s));
    QVERIFY(s.valid && !s.trigger && !s.ps);
    QCOMPARE(s.pos, QPointF(0.6, 0.4));
    QCOMPARE(s.triggerPresses, 1);  // een snelle tik tussen twee frames
    // Zonder verbonden Move: niet geldig. Geen aim-event: false.
    QJsonObject lost = QJsonDocument::fromJson(R"({"event":"aim","data":{"link":"lost","aim":{"valid":true,"x":0.5,"y":0.5}}})").object();
    QVERIFY(bigpicture::parseAimEvent(lost, &s));
    QVERIFY(!s.valid);
    QVERIFY(!bigpicture::parseAimEvent(QJsonObject{{QStringLiteral("ok"), true}}, &s));
}

void AppCoreTest::gamepadMapping() {
    using A = GamepadInput::Action;
    GamepadInput::AxisState st;
    QCOMPARE(GamepadInput::map(EV_KEY, BTN_SOUTH, 1, &st), A::Accept);
    QCOMPARE(GamepadInput::map(EV_KEY, BTN_SOUTH, 0, &st), A::None);   // loslaten
    QCOMPARE(GamepadInput::map(EV_KEY, BTN_SOUTH, 2, &st), A::None);   // herhaling
    QCOMPARE(GamepadInput::map(EV_KEY, BTN_EAST, 1, &st), A::Back);
    QCOMPARE(GamepadInput::map(EV_KEY, BTN_START, 1, &st), A::Menu);
    QCOMPARE(GamepadInput::map(EV_KEY, BTN_DPAD_LEFT, 1, &st), A::Left);
    // D-pad als hat: één actie per richting tot hij terug in het midden is.
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_HAT0X, -1, &st), A::Left);
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_HAT0X, -1, &st), A::None);
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_HAT0X, 0, &st), A::None);
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_HAT0Y, 1, &st), A::Down);
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_HAT0Y, 0, &st), A::None);
    // Stick met hysterese (-32768..32767).
    GamepadInput::AxisState s2;
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_X, 15000, &s2), A::None);   // minder dan 0,6
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_X, 25000, &s2), A::Right);
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_X, 15000, &s2), A::None);   // nog boven 0,35: geen nieuwe actie
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_X, 2000, &s2), A::None);    // terug in het midden
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_X, 30000, &s2), A::Right);
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_Y, -30000, &s2), A::Up);
    // Stick 0..255 (bv. een oudere pad).
    GamepadInput::AxisState s3;
    QCOMPARE(GamepadInput::map(EV_ABS, ABS_X, 10, &s3, 0, 255), A::Left);
    // Welke apparaten: eigen virtuele apparaten en de Move niet, een gewone pad wel, geen knoppen = niet.
    QVERIFY(!GamepadInput::wanted(0x1209, 0x7201, true));
    QVERIFY(!GamepadInput::wanted(0x1209, 0x7202, true));
    QVERIFY(!GamepadInput::wanted(0x054c, 0x03d5, true));
    QVERIFY(GamepadInput::wanted(0x054c, 0x042f, true));   // Navigation Controller: wel
    QVERIFY(GamepadInput::wanted(0x28de, 0x11ff, true));   // virtuele pad van Steam
    QVERIFY(!GamepadInput::wanted(0x046d, 0xc52b, false));
}

void AppCoreTest::bigPictureGameFilter() {
    GameData d;
    d.loadFrom(repoDirs());
    auto mk = [](const QString& prof, const QString& name, GameKind k = GameKind::TeknoParrot) {
        GameEntry g;
        g.id = QStringLiteral("tp:") + prof;
        g.profile = k == GameKind::Detected ? QString() : prof;
        g.name = name;
        g.kind = k;
        return g;
    };
    QList<GameEntry> all{mk(QStringLiteral("TC5"), QStringLiteral("Time Crisis 5")), mk(QStringLiteral("GhostBusters"), QStringLiteral("Ghostbusters")),
                         mk(QStringLiteral("Halo"), QStringLiteral("Halo")), mk(QStringLiteral("WalkingDead"), QStringLiteral("Walking Dead")),
                         mk(QStringLiteral("LGI"), QStringLiteral("Let's Go Island"), GameKind::Detected)};
    GameEntry missing = mk(QStringLiteral("Terminator"), QStringLiteral("Terminator"));
    missing.exeExists = false;
    all << missing;
    const QList<GameEntry> playable = bigpicture::games(all, d, false);
    QCOMPARE(playable.size(), 2);
    QCOMPARE(playable[0].profile, QStringLiteral("Halo"));  // op naam
    QCOMPARE(playable[1].profile, QStringLiteral("TC5"));
    const QList<GameEntry> every = bigpicture::games(all, d, true);
    QCOMPARE(every.size(), 4);  // niet opgezet en ontbrekende bestanden nooit
    QCOMPARE(every[0].profile, QStringLiteral("Halo"));  // speelbaar eerst
    QCOMPARE(every[2].profile, QStringLiteral("GhostBusters"));  // daarna op naam
    QCOMPARE(every[3].profile, QStringLiteral("WalkingDead"));
}

// --- A8: binaire VDF en Steam -----------------------------------------------------------------------------------------

void AppCoreTest::vdfRoundTripSample() {
    QFile f(dataFile(QStringLiteral("shortcuts-sample.vdf")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    VdfNode root;
    QString err;
    QVERIFY2(vdf::parseBinary(data, &root, &err), qPrintable(err));
    QCOMPARE(vdf::writeBinary(root), data);  // byte voor byte
    const VdfNode* sc = root.child("shortcuts");
    QVERIFY(sc && sc->children.size() == 2);
    QCOMPARE(sc->children[0].child("appname")->text(), QStringLiteral("Emulator Frontend"));
    QCOMPARE(sc->children[0].child("AppName")->text(), QStringLiteral("Emulator Frontend"));  // hoofdletterongevoelig
    QCOMPARE(sc->children[0].child("appid")->toInt32(), -1208917406);
    QCOMPARE(sc->children[0].child("tags")->children[0].text(), QStringLiteral("favorite"));
    QCOMPARE(sc->children[1].child("BigNumber")->type, VdfNode::UInt64);
    QCOMPARE(sc->children[1].child("Ratio")->type, VdfNode::Float);
    const auto list = SteamShortcuts::list(dataFile(QStringLiteral("shortcuts-sample.vdf")));
    QCOMPARE(list.size(), 2);
    QCOMPARE(list[1].exe, QStringLiteral("\"/home/user/Games/Some Game/run.sh\""));
    QCOMPARE(list[0].appId, static_cast<quint32>(-1208917406));
}

void AppCoreTest::vdfRoundTripRealCopy() {
    // Een KOPIE van een echte shortcuts.vdf (nooit het origineel): RAVENGUN_TEST_SHORTCUTS_VDF=<pad naar de kopie>.
    const QString path = qEnvironmentVariable("RAVENGUN_TEST_SHORTCUTS_VDF");
    if (path.isEmpty()) QSKIP("RAVENGUN_TEST_SHORTCUTS_VDF is not set (a copy of a real shortcuts.vdf)");
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    VdfNode root;
    QString err;
    QVERIFY2(vdf::parseBinary(data, &root, &err), qPrintable(err));
    QCOMPARE(vdf::writeBinary(root), data);
    const QString copy = tmp_.filePath(QStringLiteral("realcopy/shortcuts.vdf"));
    writeFile(copy, data);
    const int before = static_cast<int>(SteamShortcuts::list(copy).size());
    ShortcutSpec s;
    s.exe = QStringLiteral("/opt/ravengun/bin/ravengun");
    s.startDir = QStringLiteral("/opt/ravengun/bin");
    const ShortcutResult r = SteamShortcuts::addOrUpdate(copy, s);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(static_cast<int>(SteamShortcuts::list(copy).size()), before + 1);
    // Alle bestaande items onveranderd (byte voor byte).
    QFile g(copy);
    QVERIFY(g.open(QIODevice::ReadOnly));
    VdfNode after;
    QVERIFY(vdf::parseBinary(g.readAll(), &after, &err));
    for (int i = 0; i < before; ++i) {
        VdfNode a, b;
        a.children = {root.child("shortcuts")->children[i]};
        b.children = {after.child("shortcuts")->children[i]};
        QCOMPARE(vdf::writeBinary(a), vdf::writeBinary(b));
    }
    qInfo("real copy: %d shortcuts, RavenGun added as #%d", before, before);
}

void AppCoreTest::vdfRejectsBroken() {
    QFile f(dataFile(QStringLiteral("shortcuts-sample.vdf")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray data = f.readAll();
    VdfNode root;
    QString err;
    QVERIFY(!vdf::parseBinary(data.left(data.size() / 2), &root, &err));  // afgekapt
    QVERIFY(!err.isEmpty());
    QByteArray bad = data;
    bad[bad.indexOf("\x01" "appname")] = '\x09';  // onbekend type
    QVERIFY(!vdf::parseBinary(bad, &root, &err));
    QVERIFY(!vdf::parseBinary(data + QByteArray("xx"), &root, &err));  // rommel na het einde
    QVERIFY(vdf::parseBinary(QByteArray("\x08", 1), &root, &err));    // leeg maar geldig
    QVERIFY(root.children.isEmpty());
    // Een kapot bestand wordt nooit overschreven.
    const QString path = tmp_.filePath(QStringLiteral("broken/shortcuts.vdf"));
    writeFile(path, data.left(100));
    ShortcutSpec s;
    s.exe = QStringLiteral("/x/ravengun");
    const ShortcutResult r = SteamShortcuts::addOrUpdate(path, s);
    QVERIFY(!r.ok);
    QFile chk(path);
    QVERIFY(chk.open(QIODevice::ReadOnly));
    QCOMPARE(chk.readAll(), data.left(100));
    QVERIFY(QDir(tmp_.filePath(QStringLiteral("broken"))).entryList(QDir::Files).size() == 1);  // ook geen back-up
}

void AppCoreTest::steamShortcutId() {
    QCOMPARE(SteamShortcuts::crc32("123456789"), 0xCBF43926u);  // de standaard-controlewaarde van CRC-32
    // crc32("\"/opt/ravengun/bin/ravengun\"RavenGun") | 0x80000000 (zelfde formule als bekende hulpprogramma's)
    QCOMPARE(SteamShortcuts::shortcutId(QStringLiteral("/opt/ravengun/bin/ravengun"), QStringLiteral("RavenGun")), 0xca8338f6u);
    QVERIFY(SteamShortcuts::shortcutId(QStringLiteral("/a"), QStringLiteral("b")) & 0x80000000u);
}

void AppCoreTest::steamAddOrUpdate() {
    const QString cfg = tmp_.filePath(QStringLiteral("steam/userdata/12345/config"));
    const QString vdfPath = cfg + QStringLiteral("/shortcuts.vdf");
    QFile src(dataFile(QStringLiteral("shortcuts-sample.vdf")));
    QVERIFY(src.open(QIODevice::ReadOnly));
    const QByteArray original = src.readAll();
    writeFile(vdfPath, original);
    ShortcutSpec s;
    s.exe = QStringLiteral("/opt/ravengun/bin/ravengun");
    s.startDir = QStringLiteral("/opt/ravengun/bin");
    s.icon = QStringLiteral("/opt/ravengun/icon.png");
    ShortcutResult r = SteamShortcuts::addOrUpdate(vdfPath, s);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(!r.updated);
    QCOMPARE(r.appId, 0xca8338f6u);
    // Back-up = het oude bestand.
    QVERIFY(QFileInfo::exists(r.backup));
    QFile b(r.backup);
    QVERIFY(b.open(QIODevice::ReadOnly));
    QCOMPARE(b.readAll(), original);
    auto list = SteamShortcuts::list(vdfPath);
    QCOMPARE(list.size(), 3);
    QCOMPARE(list[2].appName, QStringLiteral("RavenGun"));
    QCOMPARE(list[2].exe, QStringLiteral("\"/opt/ravengun/bin/ravengun\""));
    QCOMPARE(list[2].startDir, QStringLiteral("\"/opt/ravengun/bin\""));
    QCOMPARE(list[2].launchOptions, QStringLiteral("--fullscreen"));
    QCOMPARE(list[2].icon, QStringLiteral("/opt/ravengun/icon.png"));
    QCOMPARE(list[2].appId, 0xca8338f6u);
    // De bestaande items zijn byte voor byte gelijk; het nieuwe item heet "2" en heeft alle gebruikelijke velden.
    VdfNode before, after;
    QVERIFY(vdf::parseBinary(original, &before));
    QFile a(vdfPath);
    QVERIFY(a.open(QIODevice::ReadOnly));
    QVERIFY(vdf::parseBinary(a.readAll(), &after));
    for (int i = 0; i < 2; ++i) {
        VdfNode x, y;
        x.children = {before.child("shortcuts")->children[i]};
        y.children = {after.child("shortcuts")->children[i]};
        QCOMPARE(vdf::writeBinary(x), vdf::writeBinary(y));
    }
    const VdfNode& e = after.child("shortcuts")->children[2];
    QCOMPARE(e.key, QByteArray("2"));
    for (const char* k : {"appid", "appname", "exe", "StartDir", "icon", "ShortcutPath", "LaunchOptions", "IsHidden", "AllowDesktopConfig",
                          "AllowOverlay", "OpenVR", "Devkit", "DevkitGameID", "DevkitOverrideAppID", "LastPlayTime", "FlatpakAppID", "tags"})
        QVERIFY2(e.child(k), k);
    QCOMPARE(e.child("AllowDesktopConfig")->toInt32(), 1);
    QCOMPARE(e.child("tags")->type, VdfNode::Map);
    // Nog een keer: bijwerken, geen dubbel item; een door de gebruiker in Steam gezette waarde blijft staan.
    {
        VdfNode tweak = after;
        tweak.child("shortcuts")->children[2].set(VdfNode::int32("AllowDesktopConfig", 0));
        writeFile(vdfPath, vdf::writeBinary(tweak));
    }
    s.launchOptions = QStringLiteral("--fullscreen --no-scan");
    r = SteamShortcuts::addOrUpdate(vdfPath, s);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.updated);
    list = SteamShortcuts::list(vdfPath);
    QCOMPARE(list.size(), 3);
    QCOMPARE(list[2].launchOptions, QStringLiteral("--fullscreen --no-scan"));
    QFile c(vdfPath);
    QVERIFY(c.open(QIODevice::ReadOnly));
    VdfNode again;
    QVERIFY(vdf::parseBinary(c.readAll(), &again));
    QCOMPARE(again.child("shortcuts")->children[2].child("AllowDesktopConfig")->toInt32(), 0);
    // Relatief pad: geweigerd.
    ShortcutSpec rel;
    rel.exe = QStringLiteral("ravengun");
    QVERIFY(!SteamShortcuts::addOrUpdate(vdfPath, rel).ok);
}

void AppCoreTest::steamNewFile() {
    // Nog geen shortcuts.vdf: een nieuw bestand met alleen RavenGun, geen back-up.
    const QString vdfPath = tmp_.filePath(QStringLiteral("steam-new/userdata/1/config/shortcuts.vdf"));
    QDir().mkpath(QFileInfo(vdfPath).path());
    ShortcutSpec s;
    s.exe = QStringLiteral("/home/u/ravengun/build-app/ravengun");
    s.startDir = QStringLiteral("/home/u/ravengun/build-app");
    const ShortcutResult r = SteamShortcuts::addOrUpdate(vdfPath, s);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.backup.isEmpty());
    const auto list = SteamShortcuts::list(vdfPath);
    QCOMPARE(list.size(), 1);
    QCOMPARE(list[0].appName, QStringLiteral("RavenGun"));
    QFile f(vdfPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    QVERIFY(d.startsWith(QByteArray("\x00shortcuts\x00\x00" "0\x00", 14)));
    QVERIFY(d.endsWith(QByteArray("\x08\x08\x08\x08", 4)));  // tags, item, shortcuts, wortel
}

void AppCoreTest::steamUsersAndRunning() {
    const QString root = tmp_.filePath(QStringLiteral("steam-users"));
    for (const char* id : {"0", "111", "222", "notanumber"}) QDir().mkpath(root + QStringLiteral("/userdata/") + QLatin1String(id) + QStringLiteral("/config"));
    // 222 = 76561197960265950; die is MostRecent.
    writeFile(root + QStringLiteral("/config/loginusers.vdf"), R"("users"
{
	"76561197960265839"
	{
		"AccountName"		"een"
		"PersonaName"		"Speler Een"
		"MostRecent"		"0"
	}
	"76561197960265950"
	{
		"AccountName"		"twee"
		"PersonaName"		"Speler \"Twee\""
		"MostRecent"		"1"
	}
}
)");
    const QList<SteamUser> users = SteamShortcuts::users(root);
    QCOMPARE(users.size(), 2);  // 0 en niet-numeriek tellen niet
    QCOMPARE(users[0].accountId, QStringLiteral("222"));
    QVERIFY(users[0].mostRecent);
    QCOMPARE(users[0].name, QStringLiteral("Speler \"Twee\""));
    QCOMPARE(users[1].name, QStringLiteral("Speler Een"));
    // Steam draait? Een nep-/proc.
    const QString proc = tmp_.filePath(QStringLiteral("proc"));
    writeFile(proc + QStringLiteral("/100/comm"), "bash\n");
    writeFile(proc + QStringLiteral("/101/comm"), "steamwebhelper\n");
    writeFile(proc + QStringLiteral("/self/comm"), "steam\n");  // geen pid
    QVERIFY(!SteamShortcuts::steamRunning(proc));
    writeFile(proc + QStringLiteral("/102/comm"), "steam\n");
    QVERIFY(SteamShortcuts::steamRunning(proc));
}

// --- Installeren, de engine als service en verwijderen (core/Install.h): alles in een tijdelijke HOME met een nep-systemctl ---------

namespace {

void makeExecutable(const QString& path) {
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                    QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
}

// Een nep-systemctl: schrijft zijn argumenten naar <dir>/systemctl.log. is-active ravengund.service is "active" als <dir>/active bestaat;
// alles met enable faalt als <dir>/fail-enable bestaat. Zet $RAVENGUN_SYSTEMCTL erop (een test mag nooit het echte systemctl bereiken).
QString makeFakeSystemctl(const QString& dir) {
    const QString p = dir + QStringLiteral("/fake-systemctl");
    writeFile(p, QStringLiteral("#!/bin/sh\necho \"$@\" >> '%1/systemctl.log'\n"
                                "case \"$*\" in\n"
                                "  *is-active*ravengund.service*) [ -e '%1/active' ] && exit 0; exit 1;;\n"
                                "  *is-active*) exit 1;;\n"
                                "  *enable*) [ -e '%1/fail-enable' ] && { echo 'boom'; exit 1; }; exit 0;;\n"
                                "esac\nexit 0\n").arg(dir).toUtf8());
    makeExecutable(p);
    qputenv("RAVENGUN_SYSTEMCTL", p.toUtf8());
    return p;
}

// Een uitgepakt releasearchief in klein: programma's (nepbestanden), een bibliotheek en de datamap met de echte sjablonen.
QString makeFakeRelease(const QString& root, const QString& marker = QStringLiteral("v1"), const QString& needs = QStringLiteral("glibc 2.17\n")) {
    QDir(root).removeRecursively();
    for (const char* b : {"ravengun", "ravengund", "ravengunctl", "ravengun-setup-helper"}) {
        const QString f = root + QStringLiteral("/bin/") + QLatin1String(b);
        writeFile(f, QStringLiteral("#!/bin/sh\necho %1 %2\n").arg(QLatin1String(b), marker).toUtf8());
        makeExecutable(f);
    }
    writeFile(root + QStringLiteral("/lib/libpsmoveapi.so"), "lib\n");
    QFile::link(QStringLiteral("libpsmoveapi.so"), root + QStringLiteral("/lib/libpsmoveapi.so.1"));
    const QString data = root + QStringLiteral("/share/ravengun");
    writeFile(data + QStringLiteral("/packaging/tp-shm-profiles.json"), "{}\n");
    for (const char* t : {"systemd/ravengund.service.in", "systemd/ravengun-oled-saver.service.in", "desktop/ravengun-qt.desktop.in"})
        writeFile(data + QStringLiteral("/packaging/") + QLatin1String(t), readAll(QStringLiteral(RAVENGUN_SOURCE_DIR "/packaging/") + QLatin1String(t)).toUtf8());
    writeFile(data + QStringLiteral("/scripts/oled-saver.sh"), "#!/bin/sh\n");
    writeFile(data + QStringLiteral("/VERSION"), (marker + QStringLiteral("\n")).toUtf8());
    if (!needs.isEmpty()) writeFile(data + QStringLiteral("/REQUIRES"), needs.toUtf8());
    return root;
}

InstallLayout fakeLayout(const QString& home, const QString& prefix) {
    return InstallLayout::forEnvironment(home, home + QStringLiteral("/.config"), home + QStringLiteral("/.local/share"),
                                         home + QStringLiteral("/.local/state"), prefix);
}

QString readLog(const QString& dir) { return readAll(dir + QStringLiteral("/systemctl.log")); }

}  // namespace

void AppCoreTest::installPathLogic() {
    const QString home = QStringLiteral("/home/deck");
    QString why;
    QVERIFY(Install::prefixIsSafe(home + QStringLiteral("/.local/opt/ravengun"), home, &why));
    QVERIFY(Install::prefixIsSafe(QStringLiteral("/home/deck/apps/rg"), home));
    for (const QString& bad : {QStringLiteral("relative/path"), QStringLiteral(""), QStringLiteral("/"), home, QStringLiteral("/home"), QStringLiteral("/opt"),
                               QStringLiteral("/usr/local/ravengun"), QStringLiteral("/etc/rg"), QStringLiteral("/usr"), QStringLiteral("/var/lib/rg"),
                               QStringLiteral("/home/deck/my games"), QStringLiteral("/home/deck/a%b"), QStringLiteral("/home/deck/a\"b"),
                               QStringLiteral("/home/deck/a'b"), QStringLiteral("/home/deck/a\\b"), QStringLiteral("/home/deck/../etc"),
                               QStringLiteral("/home/deck/x/.."), QStringLiteral("/home/deck/a\nb")}) {
        why.clear();
        QVERIFY2(!Install::prefixIsSafe(bad, home, &why), qPrintable(bad));
        QVERIFY2(!why.isEmpty(), qPrintable(bad));
    }
    QVERIFY(!Install::prefixIsSafe(QStringLiteral("/home"), QStringLiteral("/home/deck")));
    // Versies en de glibc-eis.
    QCOMPARE(Install::compareVersions(QStringLiteral("2.38"), QStringLiteral("2.43.1")), -1);
    QCOMPARE(Install::compareVersions(QStringLiteral("2.43"), QStringLiteral("2.43.0")), 0);
    QCOMPARE(Install::compareVersions(QStringLiteral("2.100"), QStringLiteral("2.43")), 1);
    QVERIFY(Install::glibcProblem(QStringLiteral("glibc 2.43\nQt 6.11 or newer\n"), QStringLiteral("2.43")).isEmpty());
    QVERIFY(Install::glibcProblem(QStringLiteral("glibc 2.43\n"), QStringLiteral("2.45")).isEmpty());
    QVERIFY(Install::glibcProblem(QStringLiteral("glibc 2.43\n"), QStringLiteral("2.38")).contains(QStringLiteral("needs glibc 2.43 or newer, but this system has 2.38")));
    QVERIFY(Install::glibcProblem(QString(), QStringLiteral("2.38")).isEmpty());   // geen REQUIRES = geen eis
    QVERIFY(Install::glibcProblem(QStringLiteral("glibc >= 2.43\n"), QStringLiteral("2.40")).contains(QStringLiteral("2.43")));
    QVERIFY(!Install::runtimeGlibc().isEmpty());
    // Pad voor een unit/snelkoppeling.
    QVERIFY(Install::unitPathSafe(QStringLiteral("/home/deck/.local/opt/ravengun/bin")));
    QVERIFY(!Install::unitPathSafe(QStringLiteral("/home/de ck/x")) && !Install::unitPathSafe(QStringLiteral("rel")) && !Install::unitPathSafe(QStringLiteral("/a%h")));
    QCOMPARE(Install::renderTemplate(QStringLiteral("ExecStart=@BINDIR@/x\nData=@DATADIR@\n@BINDIR@"), QStringLiteral("/p/bin"), QStringLiteral("/p/share/ravengun")),
             QStringLiteral("ExecStart=/p/bin/x\nData=/p/share/ravengun\n/p/bin"));
    // Het bureaublad uit user-dirs.dirs.
    QCOMPARE(Install::desktopDirFromUserDirs(QStringLiteral("# x\nXDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\nXDG_DESKTOP_DIR=\"$HOME/Bureaublad\"\n"), home),
             QStringLiteral("/home/deck/Bureaublad"));
    QVERIFY(Install::desktopDirFromUserDirs(QStringLiteral("XDG_DESKTOP_DIR=\"relatief\"\n"), home).isEmpty());
    QVERIFY(Install::desktopDirFromUserDirs(QString(), home).isEmpty());
    // De plekken van een installatie volgen XDG.
    const InstallLayout l = InstallLayout::forEnvironment(home, QString(), QStringLiteral("/data"), QStringLiteral("relatief"), QString());
    QCOMPARE(l.prefix, home + QStringLiteral("/.local/opt/ravengun"));
    QCOMPARE(l.unitDir, home + QStringLiteral("/.config/systemd/user"));
    QCOMPARE(l.applicationsDir, QStringLiteral("/data/applications"));
    QCOMPARE(l.userDataDir, QStringLiteral("/data/ravengun"));
    QCOMPARE(l.stateDir, home + QStringLiteral("/.local/state/ravengun"));   // een relatieve XDG-waarde telt niet
    QCOMPARE(l.engineUnitFile(), home + QStringLiteral("/.config/systemd/user/ravengund.service"));
    QCOMPARE(l.binDir(), l.prefix + QStringLiteral("/bin"));
    QCOMPARE(l.dataDir(), l.prefix + QStringLiteral("/share/ravengun"));
    QCOMPARE(InstallLayout::forEnvironment(home, {}, {}, {}, QStringLiteral("/x/y/../z")).prefix, QStringLiteral("/x/z"));
    // Waar ravengund staat bij een datamap: installatie of repo.
    const QString base = tmp_.filePath(QStringLiteral("binfor"));
    QDir().mkpath(base + QStringLiteral("/p/bin"));
    QDir().mkpath(base + QStringLiteral("/p/share/ravengun"));
    QCOMPARE(Install::engineBinDirFor(base + QStringLiteral("/p/share/ravengun")), base + QStringLiteral("/p/bin"));
    QCOMPARE(Install::engineBinDirFor(base + QStringLiteral("/repo")), base + QStringLiteral("/repo/build"));
    // Een boom herkennen.
    const QString rel = makeFakeRelease(tmp_.filePath(QStringLiteral("tree1")));
    QVERIFY(Install::isRavenGunTree(rel));
    QCOMPARE(Install::runningTreePrefix(rel + QStringLiteral("/bin")), QFileInfo(rel).canonicalFilePath());
    QVERIFY(Install::runningTreePrefix(base + QStringLiteral("/p/bin")).isEmpty());   // geen volledige boom
    QVERIFY(Install::runningTreePrefix(QString()).isEmpty());
    QVERIFY(Install::runningTreePrefix(rel + QStringLiteral("/share")).isEmpty());
    QVERIFY(Install::runsFromPrefix(rel + QStringLiteral("/bin"), rel));
    QVERIFY(!Install::runsFromPrefix(rel + QStringLiteral("/bin"), base));
    QVERIFY(!Install::runsFromPrefix(QString(), rel));
    // Een symlink naar de installatie telt als dezelfde map.
    QVERIFY(QFile::link(rel, tmp_.filePath(QStringLiteral("tree1-link"))));
    QVERIFY(Install::runsFromPrefix(tmp_.filePath(QStringLiteral("tree1-link/bin")), rel));
    // Kopiëren behoudt rechten en symlinks.
    QString err;
    QVERIFY(Install::copyTree(rel, tmp_.filePath(QStringLiteral("tree1-copy")), &err));
    QVERIFY(QFileInfo(tmp_.filePath(QStringLiteral("tree1-copy/bin/ravengund"))).isExecutable());
    QVERIFY(QFileInfo(tmp_.filePath(QStringLiteral("tree1-copy/lib/libpsmoveapi.so.1"))).isSymLink());
    QCOMPARE(QFileInfo(tmp_.filePath(QStringLiteral("tree1-copy/lib/libpsmoveapi.so.1"))).symLinkTarget(), tmp_.filePath(QStringLiteral("tree1-copy/lib/libpsmoveapi.so")));
    QVERIFY(!Install::copyTree(tmp_.filePath(QStringLiteral("bestaat-niet")), tmp_.filePath(QStringLiteral("x")), &err) && !err.isEmpty());
}

void AppCoreTest::installUnitAndDesktopFiles() {
    const QString data = QStringLiteral(RAVENGUN_SOURCE_DIR);
    QString err;
    const QString engine = Install::engineUnitText(data, QStringLiteral("/home/deck/.local/opt/ravengun/bin"), &err);
    QVERIFY2(!engine.isEmpty(), qPrintable(err));
    QVERIFY(engine.contains(QStringLiteral("\nExecStart=/home/deck/.local/opt/ravengun/bin/ravengund\n")));
    QVERIFY(engine.contains(QStringLiteral("WantedBy=graphical-session.target")) && engine.contains(QStringLiteral("StandardOutput=append:%h/.local/state/ravengun/logs/ravengund.log")));
    QVERIFY(!engine.contains(QLatin1Char('@')));
    const QString oled = Install::oledUnitText(data, QStringLiteral("/p/bin"), &err);
    QVERIFY(oled.contains(QStringLiteral("ExecStart=") + data + QStringLiteral("/scripts/oled-saver.sh --loop")) && !oled.contains(QLatin1Char('@')));
    const QString desktop = Install::desktopEntryText(data, QStringLiteral("/home/deck/.local/opt/ravengun/bin"), &err);
    QVERIFY(desktop.startsWith(QStringLiteral("[Desktop Entry]\n")) && desktop.contains(QStringLiteral("\nExec=/home/deck/.local/opt/ravengun/bin/ravengun\n")));
    QVERIFY(desktop.contains(QStringLiteral("\nStartupWMClass=ravengun-qt")) && desktop.contains(QStringLiteral("\nType=Application\n")) && !desktop.contains(QLatin1Char('@')));
    // Een pad met spaties of een ontbrekend sjabloon geeft geen unit maar een uitleg.
    err.clear();
    QVERIFY(Install::engineUnitText(data, QStringLiteral("/home/de ck/bin"), &err).isEmpty() && err.contains(QStringLiteral("spaces")));
    err.clear();
    QVERIFY(Install::engineUnitText(tmp_.filePath(QStringLiteral("geen-datamap")), QStringLiteral("/p/bin"), &err).isEmpty() && err.contains(QStringLiteral("Template missing")));
}

void AppCoreTest::installSelfInstall() {
    if (::geteuid() == 0) QSKIP("niet als root");
    const QString base = tmp_.filePath(QStringLiteral("selfinstall"));
    QDir().mkpath(base);
    makeFakeSystemctl(base);
    const QString home = base + QStringLiteral("/home");
    QDir().mkpath(home + QStringLiteral("/Desktop"));
    const QString prefix = home + QStringLiteral("/.local/opt/ravengun");
    const InstallLayout layout = fakeLayout(home, prefix);
    QCOMPARE(layout.desktopDir, home + QStringLiteral("/Desktop"));
    const QString release = makeFakeRelease(base + QStringLiteral("/release"));
    // Gebruikersgegevens die een installatie nooit mag aanraken.
    writeFile(layout.configDir + QStringLiteral("/library.json"), "{\"mijn\":1}\n");
    writeFile(layout.stateDir + QStringLiteral("/logs/oud.log"), "log\n");
    writeFile(layout.userDataDir + QStringLiteral("/runtime/tp.txt"), "tp\n");

    QStringList lines;
    Install::InstallOptions opt;
    opt.updateDesktopDatabase = QStringLiteral("/bin/true");
    opt.procDir = base + QStringLiteral("/proc");
    QDir().mkpath(opt.procDir);
    OpResult r = Install::installTree(release, layout, opt, [&lines](const QString& s) { lines << s; });
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.prefix, prefix);
    // De boom staat er, uitvoerbaar, met de symlink; geen tijdelijke mappen ernaast.
    QVERIFY(Install::isRavenGunTree(prefix));
    for (const char* b : {"ravengun", "ravengund", "ravengunctl", "ravengun-setup-helper"}) QVERIFY(QFileInfo(prefix + QStringLiteral("/bin/") + QLatin1String(b)).isExecutable());
    QVERIFY(QFileInfo(prefix + QStringLiteral("/lib/libpsmoveapi.so.1")).isSymLink());
    QCOMPARE(QDir(QFileInfo(prefix).path()).entryList({QStringLiteral("ravengun.*")}, QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    // Units: wijzen naar de installatie, nooit naar het archief.
    const QString unit = readAll(layout.engineUnitFile());
    QVERIFY(unit.contains(QStringLiteral("ExecStart=") + prefix + QStringLiteral("/bin/ravengund\n")));
    QVERIFY(!unit.contains(release));
    const QString oled = readAll(layout.oledUnitFile());
    QVERIFY(oled.contains(QStringLiteral("ExecStart=") + prefix + QStringLiteral("/share/ravengun/scripts/oled-saver.sh --loop")) && !oled.contains(release));
    QCOMPARE(QFileInfo(layout.engineUnitFile()).permissions() & QFileDevice::WriteGroup, QFileDevice::Permissions());
    // Menu-item, bureaubladpictogram, koppelingen.
    for (const QString& d : {layout.desktopFile(), layout.desktopDir + QStringLiteral("/ravengun-qt.desktop")}) {
        const QString t = readAll(d);
        QVERIFY2(t.contains(QStringLiteral("\nExec=") + prefix + QStringLiteral("/bin/ravengun\n")), qPrintable(d));
        QVERIFY(QFileInfo(d).isExecutable() && !t.contains(release));
    }
    QCOMPARE(QFileInfo(layout.localBinDir + QStringLiteral("/ravengun")).symLinkTarget(), prefix + QStringLiteral("/bin/ravengun"));
    QVERIFY(QFileInfo(layout.localBinDir + QStringLiteral("/ravengunctl")).isSymLink());
    QVERIFY(QFileInfo(layout.stateDir + QStringLiteral("/logs")).isDir());
    // systemctl --user: opnieuw laden en activeren + starten (de engine liep nog niet).
    const QString log = readLog(base);
    QVERIFY(log.contains(QStringLiteral("--user daemon-reload\n")));
    QVERIFY(log.contains(QStringLiteral("--user enable --now ravengund.service\n")));
    QVERIFY(!log.contains(QStringLiteral("restart")));
    // Gebruikersgegevens ongemoeid.
    QCOMPARE(readAll(layout.configDir + QStringLiteral("/library.json")), QStringLiteral("{\"mijn\":1}\n"));
    QCOMPARE(readAll(layout.stateDir + QStringLiteral("/logs/oud.log")), QStringLiteral("log\n"));
    QCOMPARE(readAll(layout.userDataDir + QStringLiteral("/runtime/tp.txt")), QStringLiteral("tp\n"));
    QVERIFY(!lines.isEmpty());

    // Bijwerken met een nieuwere boom terwijl de engine draait en er geen game is: een bestand dat weg is, is weg; herstart (zonder --now).
    QFile::remove(base + QStringLiteral("/systemctl.log"));
    { QFile f(base + QStringLiteral("/active")); QVERIFY(f.open(QIODevice::WriteOnly)); }
    makeFakeRelease(base + QStringLiteral("/release"), QStringLiteral("v2"));
    writeFile(base + QStringLiteral("/release/bin/nieuw"), "x");
    r = Install::installTree(release, layout, opt, {});
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(readAll(prefix + QStringLiteral("/share/ravengun/VERSION")).startsWith(QStringLiteral("v2")));
    QVERIFY(QFileInfo(prefix + QStringLiteral("/bin/nieuw")).exists());
    const QString log2 = readLog(base);
    QVERIFY(log2.contains(QStringLiteral("--user enable ravengund.service\n")) && !log2.contains(QStringLiteral("--now")));
    QVERIFY(log2.contains(QStringLiteral("--user restart ravengund.service\n")));
    QCOMPARE(QDir(QFileInfo(prefix).path()).entryList({QStringLiteral("ravengun.*")}, QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    QCOMPARE(readAll(layout.configDir + QStringLiteral("/library.json")), QStringLiteral("{\"mijn\":1}\n"));
    // Draait er een game (wineserver in /proc), dan geen herstart.
    QFile::remove(base + QStringLiteral("/systemctl.log"));
    writeFile(opt.procDir + QStringLiteral("/4242/comm"), "wineserver\n");
    r = Install::installTree(release, layout, opt, {});
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(!readLog(base).contains(QStringLiteral("restart")));
    QVERIFY(Install::gameRunning(opt.procDir));
    QDir(opt.procDir + QStringLiteral("/4242")).removeRecursively();
    QVERIFY(!Install::gameRunning(opt.procDir));
    // Vanuit de installatie zelf: alleen de units en snelkoppelingen verversen.
    r = Install::installTree(prefix, layout, opt, [&lines](const QString& s) { lines << s; });
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(lines.join(QLatin1Char('\n')).contains(QStringLiteral("already in")));
    QVERIFY(Install::isRavenGunTree(prefix));
    // --no-start: alleen activeren.
    QFile::remove(base + QStringLiteral("/active"));
    QFile::remove(base + QStringLiteral("/systemctl.log"));
    opt.start = false;
    QVERIFY(Install::installTree(release, layout, opt, {}).ok);
    QVERIFY(readLog(base).contains(QStringLiteral("--user enable ravengund.service\n")) && !readLog(base).contains(QStringLiteral("--now")));
    opt.start = true;
    opt.shortcuts = false;
    QFile::remove(layout.desktopFile());
    QVERIFY(Install::installTree(release, layout, opt, {}).ok && !QFileInfo::exists(layout.desktopFile()));
    opt.shortcuts = true;

    // Weigeringen laten de bestaande installatie intact.
    const QString before = readAll(prefix + QStringLiteral("/share/ravengun/VERSION"));
    makeFakeRelease(base + QStringLiteral("/release-oud"), QStringLiteral("v3"), QStringLiteral("glibc 99.0\n"));
    r = Install::installTree(base + QStringLiteral("/release-oud"), layout, opt, {});
    QVERIFY(!r.ok && r.error.contains(QStringLiteral("needs glibc 99.0")));
    QCOMPARE(readAll(prefix + QStringLiteral("/share/ravengun/VERSION")), before);
    QCOMPARE(QDir(QFileInfo(prefix).path()).entryList({QStringLiteral("ravengun.*")}, QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    r = Install::installTree(base + QStringLiteral("/geen-boom"), layout, opt, {});
    QVERIFY(!r.ok && r.error.contains(QStringLiteral("not a complete RavenGun folder")));
    QDir().mkpath(base + QStringLiteral("/geen-boom/bin"));
    QVERIFY(!Install::installTree(base + QStringLiteral("/geen-boom"), layout, opt, {}).ok);
    InstallLayout bad = layout;
    bad.prefix = QStringLiteral("/usr/local/ravengun");
    QVERIFY(!Install::installTree(release, bad, opt, {}).ok);
    bad.prefix = home + QStringLiteral("/mijn map");
    QVERIFY(!Install::installTree(release, bad, opt, {}).ok);
    // Een bestaande map die niet van ons is, blijft met rust.
    bad.prefix = home + QStringLiteral("/fotos");
    writeFile(bad.prefix + QStringLiteral("/vakantie.jpg"), "jpg");
    r = Install::installTree(release, bad, opt, {});
    QVERIFY(!r.ok && r.error.contains(QStringLiteral("not a RavenGun installation")));
    QCOMPARE(readAll(bad.prefix + QStringLiteral("/vakantie.jpg")), QStringLiteral("jpg"));
    // Een lege bestaande map mag wel.
    bad.prefix = home + QStringLiteral("/leeg");
    QDir().mkpath(bad.prefix);
    QVERIFY(Install::installTree(release, bad, opt, {}).ok && Install::isRavenGunTree(bad.prefix));
}

void AppCoreTest::installEngineService() {
    if (::geteuid() == 0) QSKIP("niet als root");
    const QString base = tmp_.filePath(QStringLiteral("engineservice"));
    QDir().mkpath(base);
    makeFakeSystemctl(base);
    const QString home = base + QStringLiteral("/home");
    const InstallLayout layout = fakeLayout(home, home + QStringLiteral("/.local/opt/ravengun"));
    const QString data = makeFakeRelease(base + QStringLiteral("/tree")) + QStringLiteral("/share/ravengun");
    const QString bin = base + QStringLiteral("/tree/bin");
    // Het unitbestand wordt geschreven, de logmap gemaakt; geen systemctl nodig voor het schrijven.
    OpResult r = Install::writeEngineUnit(layout, data, bin, {});
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(readAll(layout.engineUnitFile()).contains(QStringLiteral("ExecStart=") + bin + QStringLiteral("/ravengund\n")));
    QVERIFY(QFileInfo(layout.stateDir + QStringLiteral("/logs")).isDir());
    QVERIFY(Install::writeOledUnit(layout, data, bin, {}).ok);
    QVERIFY(!QFileInfo::exists(base + QStringLiteral("/systemctl.log")));
    // Ontbrekende engine: duidelijke fout, het bestaande unitbestand blijft.
    const QString good = readAll(layout.engineUnitFile());
    r = Install::writeEngineUnit(layout, data, base + QStringLiteral("/leeg"), {});
    QVERIFY(!r.ok && r.error.contains(QStringLiteral("ravengund not found")));
    QCOMPARE(readAll(layout.engineUnitFile()), good);
    // Activeren: daemon-reload, dan enable --now (of alleen enable).
    QVERIFY(Install::enableEngine(true, {}).ok);
    QCOMPARE(readLog(base), QStringLiteral("--user daemon-reload\n--user enable --now ravengund.service\n"));
    QFile::remove(base + QStringLiteral("/systemctl.log"));
    QVERIFY(Install::enableEngine(false, {}).ok);
    QCOMPARE(readLog(base), QStringLiteral("--user daemon-reload\n--user enable ravengund.service\n"));
    // Mislukt systemctl: de fout komt terug.
    { QFile f(base + QStringLiteral("/fail-enable")); QVERIFY(f.open(QIODevice::WriteOnly)); }
    r = Install::enableEngine(true, {});
    QVERIFY(!r.ok && r.error.contains(QStringLiteral("enable --now ravengund.service")) && r.error.contains(QStringLiteral("boom")));
    // Herstarten: alleen als er geen game draait.
    const QString proc = base + QStringLiteral("/proc");
    QDir().mkpath(proc);
    QFile::remove(base + QStringLiteral("/systemctl.log"));
    QVERIFY(Install::restartEngineIfIdle(proc, {}));
    QVERIFY(readLog(base).contains(QStringLiteral("--user restart ravengund.service")));
    writeFile(proc + QStringLiteral("/77/comm"), "gamescope\n");
    QFile::remove(base + QStringLiteral("/systemctl.log"));
    QVERIFY(!Install::restartEngineIfIdle(proc, {}));
    QVERIFY(!readLog(base).contains(QStringLiteral("restart")));
}

void AppCoreTest::installUninstall() {
    if (::geteuid() == 0) QSKIP("niet als root");
    const QString base = tmp_.filePath(QStringLiteral("uninstall"));
    QDir().mkpath(base);
    makeFakeSystemctl(base);
    // Nep-pkexec: schrijft zijn argumenten weg.
    const QString pk = base + QStringLiteral("/fake-pkexec");
    writeFile(pk, QStringLiteral("#!/bin/sh\necho \"$@\" >> '%1/pkexec.log'\necho verwijderd\n").arg(base).toUtf8());
    makeExecutable(pk);
    const QString home = base + QStringLiteral("/home");
    QDir().mkpath(home + QStringLiteral("/Desktop"));
    const QString prefix = home + QStringLiteral("/.local/opt/ravengun");
    const InstallLayout layout = fakeLayout(home, prefix);
    Install::InstallOptions io;
    io.updateDesktopDatabase = QStringLiteral("/bin/true");
    QVERIFY(Install::installTree(makeFakeRelease(base + QStringLiteral("/release")), layout, io, {}).ok);
    writeFile(layout.configDir + QStringLiteral("/library.json"), "{}\n");
    writeFile(layout.userDataDir + QStringLiteral("/runtime/tp.txt"), "tp\n");
    // Een snelkoppeling die niet de onze is, blijft staan.
    writeFile(layout.desktopDir + QStringLiteral("/ander.desktop"), "[Desktop Entry]\nExec=/usr/bin/ander\n");
    QDir().mkpath(layout.unitDir + QStringLiteral("/ravengun-oled-saver.service.d"));
    writeFile(layout.unitDir + QStringLiteral("/ravengun-oled-saver.service.d/ravengun-app.conf"), "[Service]\n");
    QVERIFY(QFile::link(QStringLiteral("/usr/bin/ander"), layout.localBinDir + QStringLiteral("/ravengunctl-vreemd")));
    QFile::remove(base + QStringLiteral("/systemctl.log"));

    Install::UninstallOptions opt;
    opt.keepRoot = true;
    opt.systemPartsPresent = true;
    opt.setupHelper = QStringLiteral("/opt/ravengun/bin/ravengun-setup-helper");
    opt.pkexec = pk;
    opt.updateDesktopDatabase = QStringLiteral("/bin/true");
    // --keep-root: pkexec wordt niet aangeroepen.
    InstallLayout other = layout;   // ander prefix: eerst een tweede installatie voor de keepRoot-proef
    QStringList lines;
    OpResult r = Install::uninstall(layout, opt, [&lines](const QString& s) { lines << s; });
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(!QFileInfo::exists(base + QStringLiteral("/pkexec.log")));
    QVERIFY(lines.join(QLatin1Char('\n')).contains(QStringLiteral("Keeping the system parts")));
    QVERIFY(!QFileInfo::exists(prefix));
    QVERIFY(!QFileInfo::exists(layout.engineUnitFile()) && !QFileInfo::exists(layout.oledUnitFile()) && !QFileInfo::exists(layout.desktopFile()));
    QVERIFY(!QFileInfo::exists(layout.desktopDir + QStringLiteral("/ravengun-qt.desktop")) && !QFileInfo::exists(layout.unitDir + QStringLiteral("/ravengun-oled-saver.service.d")));
    QVERIFY(QFileInfo::exists(layout.desktopDir + QStringLiteral("/ander.desktop")));
    QVERIFY(!QFileInfo(layout.localBinDir + QStringLiteral("/ravengun")).isSymLink() && !QFileInfo(layout.localBinDir + QStringLiteral("/ravengunctl")).isSymLink());
    QVERIFY(QFileInfo(layout.localBinDir + QStringLiteral("/ravengunctl-vreemd")).isSymLink());
    const QString log = readLog(base);
    QVERIFY(log.contains(QStringLiteral("--user disable --now ravengund.service")) && log.contains(QStringLiteral("--user disable --now ravengun-oled-saver.service")));
    QVERIFY(log.contains(QStringLiteral("--user daemon-reload")));
    // Instellingen en gebruikersgegevens blijven.
    QVERIFY(QFileInfo::exists(layout.configDir + QStringLiteral("/library.json")) && QFileInfo::exists(layout.userDataDir + QStringLiteral("/runtime/tp.txt")));

    // Met de systeemonderdelen: pkexec <hulp> --rollback, en --remove-config haalt ook de instellingen weg.
    QVERIFY(Install::installTree(base + QStringLiteral("/release"), layout, io, {}).ok);
    opt.keepRoot = false;
    opt.removeConfig = true;
    r = Install::uninstall(layout, opt, {});
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(readAll(base + QStringLiteral("/pkexec.log")), QStringLiteral("/opt/ravengun/bin/ravengun-setup-helper --rollback\n"));
    QVERIFY(!QFileInfo::exists(prefix) && !QFileInfo::exists(layout.configDir) && !QFileInfo::exists(layout.stateDir));
    QVERIFY(QFileInfo::exists(layout.userDataDir + QStringLiteral("/runtime/tp.txt")));   // nooit automatisch
    // Het hulpprogramma ontbreekt of pkexec mislukt: de rest gaat door, met uitleg voor de gebruiker.
    QVERIFY(Install::installTree(base + QStringLiteral("/release"), layout, io, {}).ok);
    opt.setupHelper.clear();
    lines.clear();
    r = Install::uninstall(layout, opt, [&lines](const QString& s) { lines << s; });
    QVERIFY(r.ok && lines.join(QLatin1Char('\n')).contains(QStringLiteral("could not be removed automatically")));
    // Een onveilig prefix wordt geweigerd; dan blijft alles staan.
    InstallLayout bad = layout;
    bad.prefix = QStringLiteral("/usr");
    QVERIFY(!Install::uninstall(bad, opt, {}).ok);
    Q_UNUSED(other);
}

void AppCoreTest::installAsyncRunner() {
    QObject ctx;
    QStringList logs;
    bool done = false;
    OpResult got;
    QThread* mainThread = QThread::currentThread();
    bool doneInMain = false, workInOther = false;
    runAsync(&ctx,
             [&workInOther, mainThread](const LogFn& log) {
                 workInOther = QThread::currentThread() != mainThread;
                 log(QStringLiteral("een"));
                 log(QStringLiteral("twee"));
                 OpResult r;
                 r.ok = true;
                 r.prefix = QStringLiteral("/x");
                 return r;
             },
             [&logs](const QString& s) { logs << s; },
             [&](const OpResult& r) {
                 done = true;
                 got = r;
                 doneInMain = QThread::currentThread() == mainThread;
             });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(workInOther && doneInMain && got.ok && got.prefix == QStringLiteral("/x"));
    QCOMPARE(logs, (QStringList{QStringLiteral("een"), QStringLiteral("twee")}));
    // Verdwijnt de context eerder, dan komt er geen aanroep meer (en geen crash).
    bool called = false;
    {
        auto* gone = new QObject;
        runAsync(gone, [](const LogFn&) { QThread::msleep(100); return OpResult(); }, {}, [&called](const OpResult&) { called = true; });
        delete gone;
    }
    QTest::qWait(300);
    QVERIFY(!called);
}

// De echte app-binary uit een nagemaakt releasearchief: `ravengun --install` kopieert de boom, schrijft units en snelkoppelingen en de
// geïnstalleerde kopie vindt zijn eigen datamap. Alles in een tijdelijke HOME met een nep-systemctl.
void AppCoreTest::installRealBinary() {
#ifdef RAVENGUN_APP_BIN
    if (::geteuid() == 0) QSKIP("niet als root");
    const QString base = tmp_.filePath(QStringLiteral("realbinary"));
    QDir().mkpath(base);
    makeFakeSystemctl(base);
    const QString home = base + QStringLiteral("/home");
    QDir().mkpath(home);
    const QString release = makeFakeRelease(base + QStringLiteral("/release"));
    QVERIFY(QFile::remove(release + QStringLiteral("/bin/ravengun")));
    QVERIFY(QFile::copy(QStringLiteral(RAVENGUN_APP_BIN), release + QStringLiteral("/bin/ravengun")));
    makeExecutable(release + QStringLiteral("/bin/ravengun"));
    const QString prefix = home + QStringLiteral("/opt/rg");
    QProcessEnvironment env;
    env.insert(QStringLiteral("HOME"), home);
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/.config"));
    env.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/.local/share"));
    env.insert(QStringLiteral("XDG_STATE_HOME"), home + QStringLiteral("/.local/state"));
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), base);
    env.insert(QStringLiteral("RAVENGUN_PREFIX"), prefix);
    env.insert(QStringLiteral("RAVENGUN_SYSTEMCTL"), base + QStringLiteral("/fake-systemctl"));
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    // Nooit het echte pkexec: de uninstall van de systeemonderdelen gaat naar een nep-pkexec dat alleen zijn argumenten bewaart.
    const QString fakePkexec = base + QStringLiteral("/fake-pkexec");
    writeFile(fakePkexec, QStringLiteral("#!/bin/sh\necho \"$@\" >> '%1/pkexec.log'\nexit 0\n").arg(base).toUtf8());
    makeExecutable(fakePkexec);
    env.insert(QStringLiteral("RAVENGUN_PKEXEC"), fakePkexec);
    auto run = [&env](const QString& exe, const QStringList& args, QString* out) {
        QProcess p;
        p.setProcessEnvironment(env);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(exe, args);
        if (!p.waitForFinished(60000)) { p.kill(); return -1; }
        *out = QString::fromUtf8(p.readAll());
        return p.exitCode();
    };
    QString out;
    QCOMPARE(run(release + QStringLiteral("/bin/ravengun"), {QStringLiteral("--install")}, &out), 0);
    QVERIFY2(Install::isRavenGunTree(prefix), qPrintable(out));
    QVERIFY(out.contains(QStringLiteral("Installed in ") + prefix));
    QVERIFY(readAll(home + QStringLiteral("/.config/systemd/user/ravengund.service")).contains(QStringLiteral("ExecStart=") + prefix + QStringLiteral("/bin/ravengund")));
    QVERIFY(readAll(home + QStringLiteral("/.local/share/applications/ravengun-qt.desktop")).contains(QStringLiteral("Exec=") + prefix + QStringLiteral("/bin/ravengun\n")));
    QVERIFY(readLog(base).contains(QStringLiteral("--user enable --now ravengund.service")));
    // De geïnstalleerde kopie draait en kent zijn eigen map.
    QCOMPARE(run(prefix + QStringLiteral("/bin/ravengun"), {QStringLiteral("--print-paths")}, &out), 0);
    QVERIFY2(out.contains(QStringLiteral("data-dir: ") + QFileInfo(prefix).canonicalFilePath() + QStringLiteral("/share/ravengun")), qPrintable(out));
    QVERIFY(out.contains(QStringLiteral("setup-helper: ") + QFileInfo(prefix).canonicalFilePath() + QStringLiteral("/bin/ravengun-setup-helper")));
    // Vanuit de installatie zelf: alleen verversen. Vanuit een ontwikkelbouw (geen volledige boom): een duidelijke fout.
    QCOMPARE(run(prefix + QStringLiteral("/bin/ravengun"), {QStringLiteral("--install"), QStringLiteral("--no-start")}, &out), 0);
    QVERIFY(out.contains(QStringLiteral("already in")));
    QCOMPARE(run(QStringLiteral(RAVENGUN_APP_BIN), {QStringLiteral("--install")}, &out), 1);
    QVERIFY(out.contains(QStringLiteral("not running from a complete release folder")));
    // En de uninstall met dezelfde binary.
    QFile::remove(base + QStringLiteral("/systemctl.log"));
    QCOMPARE(run(prefix + QStringLiteral("/bin/ravengun"), {QStringLiteral("--uninstall")}, &out), 0);
    QVERIFY2(!QFileInfo::exists(prefix) && !QFileInfo::exists(home + QStringLiteral("/.config/systemd/user/ravengund.service")), qPrintable(out));
    QVERIFY(readLog(base).contains(QStringLiteral("--user disable --now ravengund.service")));
    // De rootonderdelen zijn alleen via pkexec op het hulpprogramma van de installatie verwijderd (als deze machine ze heeft).
    if (SetupChecks::systemPartsInstalled())
        QVERIFY2(readAll(base + QStringLiteral("/pkexec.log")).endsWith(QStringLiteral("/bin/ravengun-setup-helper --rollback\n")), qPrintable(readAll(base + QStringLiteral("/pkexec.log"))));
    else
        QVERIFY(!QFileInfo::exists(base + QStringLiteral("/pkexec.log")));
#else
    QSKIP("RAVENGUN_APP_BIN is not defined");
#endif
}

// --- A6: commando's van de wizard (pkexec op het eigen hulpprogramma) -------------------------------------------------------

void AppCoreTest::setupCommands() {
    qputenv("RAVENGUN_ROOT", QByteArray(RAVENGUN_SOURCE_DIR));
    qunsetenv("RAVENGUN_SETUP_HELPER");
    qputenv("RAVENGUN_PKEXEC", "/nep/pkexec");
    // Zonder hulpprogramma naast de app (of in <repo>/build): geen commando's, dus ook geen pkexec met een raadpad.
    const QString helperInRepo = QStringLiteral(RAVENGUN_SOURCE_DIR "/build/ravengun-setup-helper");
    if (!QFileInfo(helperInRepo).isExecutable() && !QFileInfo(AppPaths::binDir() + QStringLiteral("/ravengun-setup-helper")).isExecutable()) {
        QVERIFY(!SetupChecks::permissionsInstall().valid() && !SetupChecks::permissionsCheck().valid() && !SetupChecks::permissionsRemove().valid());
        QVERIFY(SetupChecks::setupHelperBinary().isEmpty());
    }
    const QString fake = tmp_.filePath(QStringLiteral("setup-helper"));
    writeFile(fake, "#!/bin/sh\n");
    makeExecutable(fake);
    qputenv("RAVENGUN_SETUP_HELPER", fake.toUtf8());
    QCOMPARE(SetupChecks::setupHelperBinary(), fake);
    const CommandLine c = SetupChecks::permissionsInstall();
    QCOMPARE(c.program, QStringLiteral("/nep/pkexec"));
    // Vast programma met absoluut pad en vast argument; niets van de gebruiker, geen shell, geen script.
    QCOMPARE(c.args, (QStringList{fake, QStringLiteral("--apply")}));
    QVERIFY(QDir::isAbsolutePath(c.args[0]));
    QCOMPARE(SetupChecks::permissionsRemove().args, (QStringList{fake, QStringLiteral("--rollback")}));
    qunsetenv("RAVENGUN_PKEXEC");
    QCOMPARE(SetupChecks::permissionsInstall().program, QStringLiteral("pkexec"));
    // De controle zonder root draait het programma zelf, zonder pkexec.
    QCOMPARE(SetupChecks::permissionsCheck().program, fake);
    QCOMPARE(SetupChecks::permissionsCheck().args, (QStringList{QStringLiteral("--check")}));
    // Een niet-uitvoerbaar $RAVENGUN_SETUP_HELPER telt niet.
    qputenv("RAVENGUN_SETUP_HELPER", tmp_.filePath(QStringLiteral("bestaat-niet")).toUtf8());
    QVERIFY(SetupChecks::setupHelperBinary().isEmpty() && !SetupChecks::permissionsInstall().valid());
    qunsetenv("RAVENGUN_SETUP_HELPER");
    // De root-kopie in /opt telt alleen als hij van root is en niemand anders hem kan schrijven: een bestand van de gebruiker niet.
    const QString opt = tmp_.filePath(QStringLiteral("opt-ravengun"));
    writeFile(opt + QStringLiteral("/bin/ravengun-setup-helper"), "#!/bin/sh\n");
    makeExecutable(opt + QStringLiteral("/bin/ravengun-setup-helper"));
    QVERIFY(SetupChecks::installedSetupHelper(opt).isEmpty());
    QVERIFY(SetupChecks::installedSetupHelper(tmp_.filePath(QStringLiteral("geen-opt"))).isEmpty());
    // Systeemonderdelen aanwezig? (udev-regel of de Bluetooth-hulp)
    const QString rules = tmp_.filePath(QStringLiteral("rules.d"));
    QDir().mkpath(rules);
    const QString opt2 = tmp_.filePath(QStringLiteral("opt2"));
    QVERIFY(!SetupChecks::systemPartsInstalled(rules, opt2));
    writeFile(rules + QStringLiteral("/72-ravengun-dongle.rules"), "x");
    QVERIFY(SetupChecks::systemPartsInstalled(rules, opt2));
    QFile::remove(rules + QStringLiteral("/72-ravengun-dongle.rules"));
    writeFile(opt2 + QStringLiteral("/bin/ravengun-bt-helper"), "x");
    QVERIFY(SetupChecks::systemPartsInstalled(rules, opt2));
    // Camera via een nep-sysfs: PS3 Eye (1415:2000) wordt herkend.
    const QString sys = tmp_.filePath(QStringLiteral("sys"));
    writeFile(sys + QStringLiteral("/usb/1-1/idVendor"), "1415\n");
    writeFile(sys + QStringLiteral("/usb/1-1/idProduct"), "2000\n");
    QDir().mkpath(sys + QStringLiteral("/usb/1-1/1-1:1.0"));
    writeFile(sys + QStringLiteral("/v4l/video0/name"), "USB Camera-B4.09.24.1\n");
    QVERIFY(QFile::link(sys + QStringLiteral("/usb/1-1/1-1:1.0"), sys + QStringLiteral("/v4l/video0/device")));
    const auto cam = SetupChecks::camera(sys + QStringLiteral("/v4l"));
    QVERIFY(cam.found && cam.ps3eye);
    QCOMPARE(cam.device, QStringLiteral("/dev/video0"));
    QVERIFY(!SetupChecks::camera(sys + QStringLiteral("/geen")).found);
    qunsetenv("RAVENGUN_ROOT");
}

// --- A9: de datamap vinden (installatie zonder repo) ------------------------------------------------------------------

void AppCoreTest::dataRootOrder() {
    auto makeData = [&](const QString& d) {
        writeFile(d + QStringLiteral("/packaging/tp-shm-profiles.json"), "{}\n");
    };
    const QString base = tmp_.filePath(QStringLiteral("a9"));
    const QString prefix = base + QStringLiteral("/opt/ravengun");
    const QString bin = prefix + QStringLiteral("/bin");
    QDir().mkpath(bin);
    const QString home = base + QStringLiteral("/home");
    QDir().mkpath(home);
    // Niets aanwezig.
    QVERIFY(AppPaths::findDataRoot({}, {}, bin, home).isEmpty());
    // Installatie: <prefix>/share/ravengun.
    makeData(prefix + QStringLiteral("/share/ravengun"));
    QCOMPARE(AppPaths::findDataRoot({}, {}, bin, home), prefix + QStringLiteral("/share/ravengun"));
    // RAVENGUN_DATA_DIR gaat voor; een ongeldige wordt overgeslagen.
    const QString other = base + QStringLiteral("/andere");
    makeData(other);
    QCOMPARE(AppPaths::findDataRoot(other, {}, bin, home), other);
    QCOMPARE(AppPaths::findDataRoot(base + QStringLiteral("/bestaat-niet"), {}, bin, home), prefix + QStringLiteral("/share/ravengun"));
    // Repo bij ontwikkeling: build-app/ naast packaging/.
    const QString repo = base + QStringLiteral("/repo");
    makeData(repo);
    QDir().mkpath(repo + QStringLiteral("/build-app"));
    QCOMPARE(AppPaths::findDataRoot({}, {}, repo + QStringLiteral("/build-app"), home), repo);
    // RAVENGUN_ROOT vindt de repo als de binary elders staat; de installatie gaat voor de repo.
    QCOMPARE(AppPaths::findDataRoot({}, repo, base + QStringLiteral("/elders/bin"), home), repo);
    QCOMPARE(AppPaths::findDataRoot({}, repo, bin, home), prefix + QStringLiteral("/share/ravengun"));
    // ~/projects/ravengun als laatste redmiddel.
    makeData(home + QStringLiteral("/projects/ravengun"));
    QCOMPARE(AppPaths::findDataRoot({}, {}, base + QStringLiteral("/elders/bin"), home), home + QStringLiteral("/projects/ravengun"));
    // Zelfde volgorde als de engine.
    QCOMPARE(AppPaths::dataRootCandidates(QStringLiteral("/d"), QStringLiteral("/r"), QStringLiteral("/p/bin"), QStringLiteral("/h")),
             (QStringList{QStringLiteral("/d"), QStringLiteral("/p/share/ravengun"), QStringLiteral("/r"), QStringLiteral("/p"),
                          QStringLiteral("/h/projects/ravengun")}));
}

QTEST_GUILESS_MAIN(AppCoreTest)
#include "test_app_core.moc"
