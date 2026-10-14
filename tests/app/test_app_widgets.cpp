// SPDX-License-Identifier: GPL-3.0-or-later
// Tests met vensters (offscreen), zonder engine: Properties voor alle games uit de echte packaging/game-options.json, de
// route-keuze, de fullscreen-modus (vizier, schot, toetsen), de wizard met een nep-pkexec en "Add to Steam" op een kopie.
// Alles in een tijdelijke map (HOME, XDG_*); de "NAS" is een tijdelijke map via $RAVENGUN_NAS_PREFIXES.
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <QDesktopServices>
#include <QTreeWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include "core/AppCore.h"
#include "core/BigPicture.h"
#include "core/FileSafety.h"
#include "core/SteamShortcuts.h"
#include "widgets/BigPictureWindow.h"
#include "widgets/PropertiesDialog.h"
#include "widgets/SaverWindow.h"
#include "widgets/SetupWizard.h"
#include "widgets/SteamIntegration.h"

using namespace rg;

namespace {

QStringList g_messages;  // waarschuwingen en fouten van Qt tijdens een test
QStringList g_opened;    // mappen die "Open ... folder" zou openen

void collect(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) g_messages << msg;
}

QString readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(data);
}

}  // namespace

class AppWidgetTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void propertiesForAllGames();
    void routeChoiceInProperties();
    void patchesTab();
    void dongleGroup();
public Q_SLOTS:
    void recordUrl(const QUrl& u) { g_opened << u.toLocalFile(); }
private Q_SLOTS:
    void bigPictureLightgun();
    void bigPictureKeys();
    void aimStreamFallback();
    void wizardWithFakePkexec();
    void artwork();
    void addToSteamOnCopy();
    void saverWindow();
    void saverStatusParsing();
    void saverQuestionAndFeed();

private:
    QTemporaryDir tmp_;
    QString nas_, tp_;
};

void AppWidgetTest::initTestCase() {
    QVERIFY(tmp_.isValid());
    nas_ = tmp_.filePath(QStringLiteral("nas"));
    tp_ = tmp_.filePath(QStringLiteral("tp"));
    QDir().mkpath(nas_);
    qputenv("RAVENGUN_ROOT", QByteArray(RAVENGUN_SOURCE_DIR));
    qputenv("RAVENGUN_NAS_PREFIXES", nas_.toUtf8());
    qputenv("XDG_DATA_HOME", tmp_.filePath(QStringLiteral("data")).toUtf8());
    qputenv("XDG_CONFIG_HOME", tmp_.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_STATE_HOME", tmp_.filePath(QStringLiteral("state")).toUtf8());
    qputenv("RAVENGUN_SYSTEMCTL", "/bin/false");
    qputenv("RAVENGUN_NO_GAMEPAD", "1");
    qputenv("RAVENGUN_STEAM_ROOT", tmp_.filePath(QStringLiteral("steam")).toUtf8());
    // Bibliotheek met de runtime-map in de tijdelijke map.
    QJsonObject lib{{QStringLiteral("schema"), 1}, {QStringLiteral("tp_dir"), tp_}};
    QVERIFY(writeJsonAtomic(tmp_.filePath(QStringLiteral("library.json")), lib));
}

void AppWidgetTest::propertiesForAllGames() {
    // Elke game uit de echte game-options.json: Properties opent zonder Qt-waarschuwingen en toont al zijn opties (in Options,
    // of onder Display voor resolutie/venster/gamescope, of door de app geregeld: Input API).
    AppCore core(nullptr, tmp_.filePath(QStringLiteral("geen.sock")), tmp_.filePath(QStringLiteral("library.json")));
    core.library()->load();
    const GameOptionsDb& db = core.gameData()->options();
    // De tellers staan in het bestand zelf (counts); een verwijderde game hoort dus niet langer een vast getal in de test te breken.
    QFile countsFile(QStringLiteral(RAVENGUN_SOURCE_DIR "/packaging/game-options.json"));
    QVERIFY(countsFile.open(QIODevice::ReadOnly));
    const QJsonObject counts = QJsonDocument::fromJson(countsFile.readAll()).object().value(QStringLiteral("counts")).toObject();
    QCOMPARE(db.entries().size(), counts.value(QStringLiteral("games")).toInt());
    const QString tc5 = readAll(QStringLiteral(RAVENGUN_TEST_DATA "/TC5.xml"));
    g_messages.clear();
    const QtMessageHandler old = qInstallMessageHandler(collect);
    int dialogs = 0, rows = 0, total = 0;
    for (const GameOptionsEntry& e : db.entries()) {
        GameEntry g;
        g.name = e.title;
        g.folder = nas_ + QLatin1Char('/') + e.nasFolder;
        g.exePath = g.folder + QLatin1Char('/') + e.exe.section(QStringLiteral(" ("), 0, 0);
        if (!e.profile.isEmpty()) {
            g.id = QStringLiteral("tp:") + e.profile;
            g.kind = GameKind::TeknoParrot;
            g.profile = e.profile;
            // Een gebruikersprofiel (zoals Set Up Game het maakt) in de runtime-map; de game zelf "op de NAS".
            QString xml = tc5;
            xml.replace(QStringLiteral("@GAMEPATH@"), g.exePath);
            writeFile(tp_ + QStringLiteral("/UserProfiles/") + e.profile + QStringLiteral(".xml"), xml.toUtf8());
        } else {
            g.id = QStringLiteral("dir:") + g.folder;
            g.kind = GameKind::Unconfigured;
        }
        writeFile(g.exePath, "x");
        auto* d = new ui::PropertiesDialog(&core, g);
        for (int pane = 0; pane < 5; ++pane) d->ActivatePane(pane);
        d->show();
        QCoreApplication::processEvents();
        QVERIFY2(d->OptionRowCount() + d->OptionsElsewhere() == e.options.size(),
                 qPrintable(QStringLiteral("%1: %2 + %3 != %4").arg(e.id).arg(d->OptionRowCount()).arg(d->OptionsElsewhere()).arg(e.options.size())));
        rows += d->OptionRowCount();
        total += static_cast<int>(e.options.size());
        // Niets geschreven bij alleen openen (de NAS en het profiel blijven ongewijzigd).
        if (!e.profile.isEmpty())
            QCOMPARE(QFileInfo::exists(tp_ + QStringLiteral("/UserProfiles/") + e.profile + QStringLiteral(".xml.ravengun-bak")), false);
        d->close();
        delete d;
        ++dialogs;
    }
    qInstallMessageHandler(old);
    QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(QLatin1Char('\n'))));
    QCOMPARE(dialogs, counts.value(QStringLiteral("games")).toInt());
    QCOMPARE(total, counts.value(QStringLiteral("options")).toInt());
    qInfo("Properties: %d dialogs, %d option rows shown in Options (%d of %d elsewhere)", dialogs, rows, total - rows, total);
}

void AppWidgetTest::routeChoiceInProperties() {
    AppCore core(nullptr, tmp_.filePath(QStringLiteral("geen.sock")), tmp_.filePath(QStringLiteral("library.json")));
    GameEntry g;
    g.id = QStringLiteral("tp:TC5");
    g.kind = GameKind::TeknoParrot;
    g.profile = QStringLiteral("TC5");
    g.name = QStringLiteral("Time Crisis 5");
    ui::PropertiesDialog d(&core, g);
    QComboBox* c = d.RouteCombo();
    QVERIFY(c);
    QVERIFY(c->isEnabled());
    QCOMPARE(c->currentData().toString(), QStringLiteral("tp-shm"));
    QVERIFY(c->itemText(0).contains(QStringLiteral("recommended")));
    const int evdev = c->findData(QStringLiteral("evdev"));
    QVERIFY(evdev > 0);
    c->setCurrentIndex(evdev);
    emit c->activated(evdev);
    const QString user = tmp_.filePath(QStringLiteral("data/ravengun/patches/TC5/game.ini"));
    QCOMPARE(readAll(user), QStringLiteral("[Input]\nRoute = evdev\n"));
    QCOMPARE(core.gameData()->route(QStringLiteral("TC5")).route, QStringLiteral("evdev"));
    // Terug naar de aanbevolen route: de keuze verdwijnt.
    c->setCurrentIndex(0);
    emit c->activated(0);
    QVERIFY(!readAll(user).contains(QStringLiteral("Route")));
    // Een Unity-game heeft één route: de keuze staat uit.
    GameEntry u;
    u.id = QStringLiteral("tp:TombRaider");
    u.kind = GameKind::TeknoParrot;
    u.profile = QStringLiteral("TombRaider");
    ui::PropertiesDialog du(&core, u);
    QVERIFY(!du.RouteCombo()->isEnabled());
}

void AppWidgetTest::patchesTab() {
    AppCore core(nullptr, tmp_.filePath(QStringLiteral("geen.sock")), tmp_.filePath(QStringLiteral("library.json")));
    const QString userRoot = tmp_.filePath(QStringLiteral("data/ravengun/patches"));
    QDir(userRoot + QStringLiteral("/GaiaAttack4")).removeRecursively();
    GameEntry g;
    g.id = QStringLiteral("tp:GaiaAttack4");
    g.kind = GameKind::TeknoParrot;
    g.profile = QStringLiteral("GaiaAttack4");
    g.name = QStringLiteral("Gaia Attack 4");
    ui::PropertiesDialog d(&core, g);
    QTreeWidget* t = d.PatchTree();
    QVERIFY(t);
    QCOMPARE(t->topLevelItemCount(), 3);                                               // de lader-patch en de twee geheugenpatches (informatie)
    QTreeWidgetItem* it = t->topLevelItem(0);
    QCOMPARE(t->topLevelItem(1)->checkState(0), Qt::Unchecked);
    QVERIFY(t->topLevelItem(1)->text(2).contains(QStringLiteral("informational")));
    QVERIFY(t->topLevelItem(1)->toolTip(0).contains(QStringLiteral("OpenParrot core already applies")));
    QCOMPARE(it->text(0), QStringLiteral("OpenParrotLoader zonder CONTEXT_FLOATING_POINT"));
    QCOMPARE(it->checkState(0), Qt::Checked);
    QVERIFY(it->toolTip(0).contains(QStringLiteral("MXCSR")));                       // de reden als tooltip
    QCOMPARE(it->text(1), QStringLiteral("loader"));                                 // de categorie
    QVERIFY(t->topLevelItemCount() >= 1);
    t->setCurrentItem(it);
    QVERIFY(it->data(0, Qt::UserRole + 1).toString().contains(QStringLiteral("MXCSR")));
    // Uitvinken schrijft de gebruikerslaag (niet de meegeleverde laag).
    it->setCheckState(0, Qt::Unchecked);
    QCoreApplication::processEvents();                                                 // de lijst wordt na het signaal opnieuw opgebouwd
    const QString userFile = userRoot + QStringLiteral("/GaiaAttack4/patches.ini");
    QVERIFY(readAll(userFile).contains(QStringLiteral("Enabled = false")));
    QCOMPARE(core.gameData()->patches(QStringLiteral("GaiaAttack4"))[0].enabled, false);
    QCOMPARE(t->topLevelItem(0)->checkState(0), Qt::Unchecked);
    QVERIFY(t->topLevelItem(0)->text(3).contains(QStringLiteral("choice")));
    t->topLevelItem(0)->setCheckState(0, Qt::Checked);                                 // gelijk aan de meegeleverde stand: keuze weg
    QCoreApplication::processEvents();
    QVERIFY(!readAll(userFile).contains(QStringLiteral("Enabled")));
    // De effectieve instellingen staan er read-only bij, met hun laag.
    QTreeWidget* s = d.SettingsTree();
    QVERIFY(s && s->topLevelItemCount() > 5);
    bool route = false;
    for (int i = 0; i < s->topLevelItemCount(); ++i)
        if (s->topLevelItem(i)->text(0) == QStringLiteral("Input") && s->topLevelItem(i)->text(1) == QStringLiteral("Layout")) {
            route = s->topLevelItem(i)->text(2) == QStringLiteral("MusicGunGun2Pipe") && s->topLevelItem(i)->text(3) == QStringLiteral("Shipped");
        }
    QVERIFY(route);
    // De knoppen openen de mappen (de gebruikersmap wordt aangemaakt); de URL-handler vervangt de bestandsbeheerder.
    QDesktopServices::setUrlHandler(QStringLiteral("file"), this, "recordUrl");
    g_opened.clear();
    QVERIFY(d.OpenUserFolderButton()->isEnabled() && d.OpenShippedFolderButton()->isEnabled());
    d.OpenUserFolderButton()->click();
    d.OpenShippedFolderButton()->click();
    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
    QCOMPARE(g_opened.size(), 2);
    QCOMPARE(g_opened[0], userRoot + QStringLiteral("/GaiaAttack4"));
    QVERIFY(QFileInfo(userRoot + QStringLiteral("/GaiaAttack4")).isDir());
    QCOMPARE(g_opened[1], QStringLiteral(RAVENGUN_SOURCE_DIR "/patches/GaiaAttack4"));
    // Een game zonder patches: lege lijst met uitleg; een game zonder profiel: knoppen uit.
    GameEntry tc5;
    tc5.id = QStringLiteral("tp:TC5");
    tc5.kind = GameKind::TeknoParrot;
    tc5.profile = QStringLiteral("TC5");
    tc5.name = QStringLiteral("Time Crisis 5");
    ui::PropertiesDialog d2(&core, tc5);
    QCOMPARE(d2.PatchTree()->topLevelItemCount(), 0);
    GameEntry none;
    none.id = QStringLiteral("dir:x");
    none.kind = GameKind::Detected;
    none.name = QStringLiteral("Unknown");
    ui::PropertiesDialog d3(&core, none);
    QVERIFY(!d3.OpenUserFolderButton()->isEnabled());
}

void AppWidgetTest::dongleGroup() {
    // Properties > Options > Dongle: vinkje, bron, statusregel en Refresh; schrijft alleen verschillen naar de gebruikerslaag.
    QTemporaryDir fake;
    QVERIFY(fake.isValid());
    const QString sys = fake.filePath(QStringLiteral("sys")), proc = fake.filePath(QStringLiteral("proc"));
    QDir().mkpath(sys);
    QDir().mkpath(proc);
    qputenv("RAVENGUN_USB_SYSFS", sys.toUtf8());
    qputenv("RAVENGUN_PROC_ROOT", proc.toUtf8());
    qputenv("RAVENGUN_DONGLE_ROOT", fake.filePath(QStringLiteral("root")).toUtf8());
    AppCore core(nullptr, tmp_.filePath(QStringLiteral("geen.sock")), tmp_.filePath(QStringLiteral("library.json")));
    const QString user = tmp_.filePath(QStringLiteral("data/ravengun/patches/TC5/game.ini"));
    QFile::remove(user);
    GameEntry g;
    g.id = QStringLiteral("tp:TC5");
    g.kind = GameKind::TeknoParrot;
    g.profile = QStringLiteral("TC5");
    g.name = QStringLiteral("Time Crisis 5");
    {
        ui::PropertiesDialog d(&core, g);
        QVERIFY(d.DongleCheck() && d.DongleSourceCombo() && d.DongleStatusLabel() && d.DongleRefreshButton());
        QCOMPARE(d.DongleCheck()->text(), QStringLiteral("Use dongle"));
        QVERIFY(d.DongleCheck()->isEnabled());
        QVERIFY(!d.DongleCheck()->isChecked());                                           // standaard uit
        QCOMPARE(d.DongleSourceCombo()->count(), 2);                                      // usb-passthrough en drop-in (dongleSources())
        QCOMPARE(d.DongleSourceCombo()->itemText(0), QStringLiteral("USB pass-through (physical dongle)"));
        QCOMPARE(d.DongleSourceCombo()->itemData(1).toString(), QStringLiteral("drop-in"));
        QCOMPARE(d.DongleSourceCombo()->currentData().toString(), QStringLiteral("usb-passthrough"));
        QVERIFY(!d.DongleSourceCombo()->isEnabled() && !d.DongleStatusLabel()->isEnabled() && !d.DongleRefreshButton()->isEnabled());   // uit: grijs
        QVERIFY(d.DongleStatusLabel()->text().isEmpty());
        // Aan: bron en status actief, de gebruikerslaag krijgt alleen Enabled = true, de statusregel zegt dat er geen dongle is.
        d.DongleCheck()->setChecked(true);
        QVERIFY(d.DongleSourceCombo()->isEnabled() && d.DongleStatusLabel()->isEnabled() && d.DongleRefreshButton()->isEnabled());
        QCOMPARE(readAll(user), QStringLiteral("[Dongle]\nEnabled = true\n"));
        QTRY_COMPARE_WITH_TIMEOUT(d.DongleStatusLabel()->text(), QStringLiteral("No dongle found"), 15000);
        // Een dongle erin (met serienummer) en Refresh: merk, product, ID's en runtime-status; nooit het serienummer.
        writeFile(sys + QStringLiteral("/1-1/idVendor"), "0529\n");
        writeFile(sys + QStringLiteral("/1-1/idProduct"), "0001\n");
        writeFile(sys + QStringLiteral("/1-1/manufacturer"), "Aladdin Knowledge Systems\n");
        writeFile(sys + QStringLiteral("/1-1/product"), "HASP HL 3.25\n");
        writeFile(sys + QStringLiteral("/1-1/serial"), "SN-SECRET-0042\n");
        writeFile(proc + QStringLiteral("/9/comm"), "hasplmd\n");
        d.DongleRefreshButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(d.DongleStatusLabel()->text(),
                                  QStringLiteral("Dongle detected: Aladdin Knowledge Systems HASP HL 3.25 (0529:0001) \u2014 runtime: running"), 15000);
        QVERIFY(!d.DongleStatusLabel()->text().contains(QStringLiteral("SECRET")));
        // De bron kiezen die de standaard is, schrijft niets extra's.
        emit d.DongleSourceCombo()->activated(0);
        QCOMPARE(readAll(user), QStringLiteral("[Dongle]\nEnabled = true\n"));
    }
    {
        ui::PropertiesDialog d(&core, g);                                                  // opnieuw openen: de keuze staat er nog
        QVERIFY(d.DongleCheck()->isChecked());
        QVERIFY(d.DongleSourceCombo()->isEnabled());
        d.DongleCheck()->setChecked(false);                                                // terug op de standaard: de sleutel verdwijnt
        QVERIFY(!readAll(user).contains(QStringLiteral("Enabled")));
        QVERIFY(!d.DongleSourceCombo()->isEnabled() && !d.DongleStatusLabel()->isEnabled() && !d.DongleRefreshButton()->isEnabled());
        QVERIFY(d.DongleStatusLabel()->text().isEmpty());
    }
    for (const char* v : {"RAVENGUN_USB_SYSFS", "RAVENGUN_PROC_ROOT", "RAVENGUN_DONGLE_ROOT"}) qunsetenv(v);
}

namespace {
// Een bibliotheek met een paar speelbare games (uit de cache van library.json; geen scan).
QString libraryWithGames(QTemporaryDir& tmp) {
    QJsonArray games;
    for (const char* p : {"Halo", "TC5", "Terminator", "TombRaider", "JurassicPark"}) {
        GameEntry g;
        g.id = QStringLiteral("tp:") + QLatin1String(p);
        g.kind = GameKind::TeknoParrot;
        g.profile = QLatin1String(p);
        g.name = QLatin1String(p);
        games.append(g.toJson());
    }
    const QString path = tmp.filePath(QStringLiteral("library-bp.json"));
    writeJsonAtomic(path, QJsonObject{{QStringLiteral("schema"), 1}, {QStringLiteral("games"), games}});
    return path;
}
}  // namespace

void AppWidgetTest::bigPictureLightgun() {
    AppCore core(nullptr, tmp_.filePath(QStringLiteral("geen.sock")), libraryWithGames(tmp_));
    core.library()->load();
    ui::BigPictureWindow w(&core);
    w.resize(1920, 1080);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    // Zonder "Show All Games" alleen de games die de meegeleverde game.ini playable of working noemt (de status verandert met het
    // werk aan een game, dus geen vast getal): hier uit dezelfde gegevens geteld.
    int speelbaar = 0;
    for (const char* p : {"Halo", "TC5", "Terminator", "TombRaider", "JurassicPark"}) {
        const QString st = core.gameData()->playState(QLatin1String(p));
        if (st == QStringLiteral("playable") || st == QStringLiteral("working")) ++speelbaar;
    }
    QVERIFY(speelbaar > 0);
    QCOMPARE(w.GameCount(), speelbaar);
    QString message;
    connect(core.launcher(), &GameLauncher::message, this, [&message](const QString& t, bool) { message = t; });
    // Vizier op tegel 1, één schot: nog niets; tweede schot: starten (hier: "engine is not running").
    const QPointF t1 = w.TileCenterNorm(1);
    w.InjectAim(t1, true, false);
    w.InjectAim(t1, true, true);
    w.InjectAim(t1, true, false);
    QVERIFY(message.isEmpty());
    w.InjectAim(t1, true, true);
    QVERIFY2(message.contains(QStringLiteral("engine")), qPrintable(message));
    w.InjectAim(t1, true, false);
    // Een tik tussen twee frames (alleen een flank) telt ook.
    message.clear();
    rg::bigpicture::AimState s;
    s.valid = true;
    s.pos = w.TileCenterNorm(2);
    w.InjectAim(s.pos, true, false);
    w.InjectAim(s.pos, true, true);
    w.InjectAim(s.pos, true, false);
    QVERIFY(message.isEmpty());
    // Ongeldige aim (geen Move): geen vizier, geen start.
    w.InjectAim(t1, false, true);
    w.InjectAim(t1, false, false);
    QVERIFY(message.isEmpty());
    // Schermafdruk zonder fouten.
    QVERIFY(!w.grab().isNull());
}

void AppWidgetTest::bigPictureKeys() {
    AppCore core(nullptr, tmp_.filePath(QStringLiteral("geen.sock")), libraryWithGames(tmp_));
    core.library()->load();
    ui::BigPictureWindow w(&core);
    bool exited = false;
    connect(&w, &ui::BigPictureWindow::ExitRequested, this, [&exited] { exited = true; });
    w.resize(1280, 720);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    // Naar de knoppen (Show All Games) en Enter: alle startklare games.
    for (int i = 0; i < 5; ++i) QTest::keyClick(&w, Qt::Key_Down);
    QTest::keyClick(&w, Qt::Key_Return);
    QVERIFY(w.ShowAll());
    QCOMPARE(w.GameCount(), 5);
    QTest::keyClick(&w, Qt::Key_Return);
    QVERIFY(!w.ShowAll());
    // Escape: de vraag; Escape weer: terug; Escape + links + Enter: afsluiten.
    QTest::keyClick(&w, Qt::Key_Escape);
    QTest::keyClick(&w, Qt::Key_Escape);
    QVERIFY(!exited);
    QTest::keyClick(&w, Qt::Key_Escape);
    QTest::keyClick(&w, Qt::Key_Return);  // Cancel is voorgeselecteerd
    QVERIFY(!exited);
    QTest::keyClick(&w, Qt::Key_Escape);
    QTest::keyClick(&w, Qt::Key_Left);
    QTest::keyClick(&w, Qt::Key_Return);
    QVERIFY(exited);
}

void AppWidgetTest::aimStreamFallback() {
    // Een nep-engine die `lite` niet kent (zoals de engine van vóór deze wijziging): AimStream valt terug op de volledige watch
    // en leest dan de samples. Daarna een nieuwe engine met lite.
    for (const bool supportsLite : {false, true}) {
        const QString path = tmp_.filePath(supportsLite ? QStringLiteral("lite.sock") : QStringLiteral("old.sock"));
        QLocalServer server;
        QVERIFY(server.listen(path));
        QStringList requests;
        QLocalSocket* client = nullptr;
        connect(&server, &QLocalServer::newConnection, this, [&] {
            client = server.nextPendingConnection();
            connect(client, &QLocalSocket::readyRead, this, [&, supportsLite] {
                while (client->canReadLine()) {
                    const QJsonObject r = QJsonDocument::fromJson(client->readLine()).object();
                    requests << QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Compact));
                    const QJsonValue id = r.value(QStringLiteral("id"));
                    if (r.contains(QStringLiteral("lite")) && !supportsLite) {
                        client->write(QJsonDocument(QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("ok"), false}, {QStringLiteral("id"), id},
                                                                {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("bad_request")},
                                                                                                      {QStringLiteral("message"), QStringLiteral("onbekend veld 'lite'")}}}})
                                          .toJson(QJsonDocument::Compact) + '\n');
                        continue;
                    }
                    client->write(QJsonDocument(QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("ok"), true}, {QStringLiteral("id"), id}})
                                      .toJson(QJsonDocument::Compact) + '\n');
                    if (supportsLite)
                        client->write(R"({"v":1,"event":"aim","data":{"seq":1,"link":"fresh","aim":{"valid":true,"x":0.2,"y":0.8,"off_screen":false},"trigger":0,"down":["t"],"pressed":{"t":1},"released":{}}})"
                                      "\n");
                    else
                        client->write(R"({"v":1,"event":"sample","data":{"sample":{"link":"fresh","aim":{"valid":true,"x_clamped":0.2,"y_clamped":0.8,"off_screen":false},"buttons":{"down":["t"]}},"edges_since_last_frame":{"pressed":{"t":1},"released":{}}}})"
                                      "\n");
                }
            });
        });
        AimStream s(path);
        rg::bigpicture::AimState got;
        int events = 0;
        connect(&s, &AimStream::aimChanged, this, [&](const rg::bigpicture::AimState& a) {
            if (a.valid) {
                got = a;
                ++events;
            }
        });
        s.start();
        QTRY_VERIFY_WITH_TIMEOUT(events > 0, 5000);
        QVERIFY(got.trigger);
        QCOMPARE(got.pos, QPointF(0.2, 0.8));
        QCOMPARE(s.liteSupported(), supportsLite);
        QCOMPARE(requests.size(), supportsLite ? 1 : 2);
        QVERIFY(requests.first().contains(QStringLiteral("\"lite\":true")));
        if (!supportsLite) QVERIFY(!requests.last().contains(QStringLiteral("lite")));
        s.stop();
    }
}

void AppWidgetTest::wizardWithFakePkexec() {
    // Nep-pkexec: schrijft zijn argumenten weg en eindigt met 0. Er wordt niets als root uitgevoerd.
    const QString fake = tmp_.filePath(QStringLiteral("fake-pkexec"));
    const QString log = tmp_.filePath(QStringLiteral("fake-pkexec.args"));
    writeFile(fake, QStringLiteral("#!/bin/sh\nfor a in \"$@\"; do printf '%s\\n' \"$a\"; done > '%1'\necho nep-pkexec klaar\n").arg(log).toUtf8());
    QFile::setPermissions(fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("RAVENGUN_PKEXEC", fake.toUtf8());
    AppCore core(nullptr, tmp_.filePath(QStringLiteral("geen.sock")), tmp_.filePath(QStringLiteral("library.json")));
    ui::SetupWizard wiz(&core);
    wiz.show();
    QVERIFY(QTest::qWaitForWindowExposed(&wiz));
    // Alle acht stappen doorlopen zonder iets aan te klikken.
    QList<int> seen{wiz.currentId()};
    for (int i = 1; i < ui::SetupWizard::Count; ++i) {
        wiz.next();
        seen << wiz.currentId();
    }
    QCOMPARE(seen, (QList<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    // Stap 2: Install start pkexec met precies het script en --apply.
    wiz.restart();
    wiz.next();
    QPushButton* install = nullptr;
    for (QPushButton* b : wiz.findChildren<QPushButton*>())
        if (b->text().startsWith(QStringLiteral("Install (asks"))) install = b;
    QVERIFY(install);
    install->click();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(log), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(install->isEnabled(), 5000);
    const QStringList args = readAll(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(args, (QStringList{QDir::cleanPath(QStringLiteral(RAVENGUN_SOURCE_DIR "/scripts/install-permissions.sh")), QStringLiteral("--apply")}));
    qunsetenv("RAVENGUN_PKEXEC");
    // Afronden markeert de wizard als gedaan.
    ui::SetupWizard::MarkDone(false);
    wiz.done(QDialog::Accepted);
    QVERIFY(!ui::SetupWizard::ShouldShowAtStart());
}

void AppWidgetTest::artwork() {
    QCOMPARE(artwork::portrait().size(), QSize(600, 900));
    QCOMPARE(artwork::landscape().size(), QSize(920, 430));
    QCOMPARE(artwork::hero().size(), QSize(1920, 620));
    QCOMPARE(artwork::logo().size(), QSize(800, 260));
    QCOMPARE(artwork::icon(256).size(), QSize(256, 256));
    // Zwart blijft zwart: de hoeken van de afbeeldingen zijn zwart (of doorzichtig bij logo en icoon).
    QCOMPARE(artwork::portrait().pixelColor(0, 0), QColor(Qt::black));
    QCOMPARE(artwork::logo().pixelColor(0, 0).alpha(), 0);
}

void AppWidgetTest::addToSteamOnCopy() {
    // RAVENGUN_STEAM_ROOT wijst naar een tijdelijke "Steam"-map met een kopie van het voorbeeld; nooit de echte bestanden.
    const QString root = tmp_.filePath(QStringLiteral("steam"));
    QVERIFY(qEnvironmentVariable("RAVENGUN_STEAM_ROOT") == root);
    const QString cfg = root + QStringLiteral("/userdata/4242/config");
    QDir().mkpath(cfg);
    QVERIFY(QFile::copy(QStringLiteral(RAVENGUN_TEST_DATA "/shortcuts-sample.vdf"), cfg + QStringLiteral("/shortcuts.vdf")));
    QVERIFY(!ui::IsInSteam());
    const ui::SteamAddOutcome o = ui::AddToSteamQuiet();
    QVERIFY2(o.ok, qPrintable(o.message));
    QVERIFY(ui::IsInSteam());
    const auto list = SteamShortcuts::list(cfg + QStringLiteral("/shortcuts.vdf"));
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.last().launchOptions, QStringLiteral("--fullscreen"));
    QCOMPARE(list.last().exe, SteamShortcuts::quoted(QCoreApplication::applicationFilePath()));
    QVERIFY(QFileInfo::exists(list.last().icon));
    const QString id = QString::number(list.last().appId);
    for (const QString& suffix : {QStringLiteral("p.png"), QStringLiteral(".png"), QStringLiteral("_hero.png"), QStringLiteral("_logo.png"),
                                  QStringLiteral("_icon.png")})
        QVERIFY2(QFileInfo::exists(cfg + QStringLiteral("/grid/") + id + suffix), qPrintable(suffix));
    // Een back-up van het oude bestand staat ernaast.
    QCOMPARE(QDir(cfg).entryList({QStringLiteral("shortcuts.vdf.ravengun-bak-*")}, QDir::Files).size(), 1);
    // Onbekende gebruiker: nette melding.
    QVERIFY(!ui::AddToSteamQuiet(QStringLiteral("999")).ok);
}

void AppWidgetTest::saverWindow() {
    // OLED-saver: schermvullend, zonder rand, zwart, met een statusbord dat bijwerkt en verspringt.
    QTemporaryDir state;
    qputenv("XDG_STATE_HOME", state.path().toUtf8());
    QVERIFY(QDir().mkpath(state.path() + QStringLiteral("/ravengun")));
    const auto write = [&](const QString& name, const QString& text) {
        QFile f(state.path() + QStringLiteral("/ravengun/") + name);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text.toUtf8());
    };
    ui::SaverWindow w;
    w.Open();
    QVERIFY(w.isFullScreen());
    QVERIFY(w.windowFlags() & Qt::FramelessWindowHint);
    QVERIFY(w.windowFlags() & Qt::WindowStaysOnTopHint);
    QCOMPARE(w.cursor().shape(), Qt::BlankCursor);
    QCOMPARE(w.palette().color(QPalette::Window), QColor(Qt::black));
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    // Zonder statusbestand zegt het bord dat er geen status is -- niet iets dat op "goed" lijkt.
    w.Reposition();
    QCOMPARE(w.ShownLines().value(0), QStringLiteral("Onbekend of er nu een spel draait"));
    QCOMPARE(w.ShownLines().value(1), QStringLiteral("Geen status beschikbaar"));
    QVERIFY(w.ShownLines().value(2).contains(QStringLiteral("het statusbestand ontbreekt")));
    QVERIFY(!w.ShownLines().join(QLatin1Char('\n')).contains(QStringLiteral("goed")));

    // Met een geldig statusbestand staat alles er in gewone taal.
    write(QStringLiteral("saver-status.txt"),
          QStringLiteral("spel: Halo\nbezig: Richten en schieten testen\nstand: goed\nstap: 4 van 5\n"
                         "doel: Zien of de trekker in het spel aankomt\nklaar: Beeld op de tv; Kalibratie overgeslagen\nnog: Twee minuten spelen\n"
                         "gehaald: Rabbids\n"));
    write(QStringLiteral("saver-game.txt"), QStringLiteral("onzichtbaar\n"));
    w.Reposition();
    const QStringList lines = w.ShownLines();
    QCOMPARE(lines.value(0), QStringLiteral("Er draait nu een spel, zonder beeld (dat is nog geen uitslag)"));
    QCOMPARE(lines.value(1), QStringLiteral("Halo: Richten en schieten testen"));
    QCOMPARE(lines.value(2), QStringLiteral("Het gaat goed · stap 4 van 5 · bijna klaar"));
    QCOMPARE(lines.value(3), QStringLiteral("Waarvoor: Zien of de trekker in het spel aankomt"));
    QCOMPARE(lines.value(4), QStringLiteral("Al gedaan: Beeld op de tv · Kalibratie overgeslagen"));
    QCOMPARE(lines.value(5), QStringLiteral("Nog te doen: Twee minuten spelen"));
    QCOMPARE(lines.value(6), QStringLiteral("Gehaald (5 minuten attract of spel): Rabbids"));
    QVERIFY(lines.value(7).contains(QStringLiteral("bijgewerkt zojuist")));

    // Het scherm blijft vrijwel helemaal zwart (OLED): het bord is tekst, geen vlak.
    const QImage img = w.grab().toImage();
    QVERIFY(!img.isNull());
    qint64 lit = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) lit += qGray(img.pixel(x, y)) > 16 ? 1 : 0;
    QVERIFY2(lit * 100 < qint64(img.width()) * img.height() * 12, qPrintable(QString::number(lit)));
    QVERIFY(QRegularExpression(QStringLiteral("^\\d\\d:\\d\\d$")).match(w.ClockText()).hasMatch());
    QVERIFY(QRect(QPoint(0, 0), w.size()).contains(w.ClockGeometry()));
    // Het bord springt naar steeds andere plekken, altijd binnen beeld.
    QSet<QPair<int, int>> seen;
    for (int i = 0; i < 20; ++i) {
        w.Reposition();
        QVERIFY(QRect(QPoint(0, 0), w.size()).contains(w.ClockGeometry()));
        seen.insert({w.ClockGeometry().x(), w.ClockGeometry().y()});
    }
    QVERIFY(seen.size() > 1);
    if (const QString out = qEnvironmentVariable("RAVENGUN_SAVER_SHOT"); !out.isEmpty()) QVERIFY(w.grab().save(out));
    qunsetenv("XDG_STATE_HOME");
}

void AppWidgetTest::saverStatusParsing() {
    // Een verkeerd gelezen status is erger dan geen status: alles wat niet klopt wordt afgekeurd, met de reden.
    using ui::SaverGame;
    using ui::SaverWindow;
    const QString good = QStringLiteral("# de stand\nspel: Halo\nbezig: Testen\nstand: bezig\n");
    const ui::SaverStatus ok = SaverWindow::ParseStatus(good);
    QVERIFY2(ok.ok, qPrintable(ok.error));
    QCOMPARE(ok.game, QStringLiteral("Halo"));
    QCOMPARE(ok.steps, 0);
    QCOMPARE(SaverWindow::Lines(ok, SaverGame::None, 1000).value(2), QStringLiteral("Bezig, nog geen uitslag"));
    QVERIFY(SaverWindow::ParseStatus(good + QStringLiteral("stap: 5/5\n")).ok);
    QCOMPARE(SaverWindow::Lines(SaverWindow::ParseStatus(good + QStringLiteral("stap: 5 van 5\n")), SaverGame::None, 1000).value(2),
             QStringLiteral("Bezig, nog geen uitslag · stap 5 van 5 · de laatste stap"));
    QCOMPARE(SaverWindow::Lines(SaverWindow::ParseStatus(QStringLiteral("spel: X\nbezig: Y\nstand: slecht\n")), SaverGame::None, 1000).value(2),
             QStringLiteral("Het gaat niet goed"));

    const QList<QPair<QString, QString>> bad = {
        {QString(), QStringLiteral("leeg")},
        {QStringLiteral("\n# alleen commentaar\n"), QStringLiteral("leeg")},
        {QStringLiteral("Luigi's Mansion starten via onze eigen app\n"), QStringLiteral("geen 'sleutel: waarde'")},
        {QStringLiteral("spel: Halo\nbezig: Testen\n"), QStringLiteral("'stand' ontbreekt")},
        {QStringLiteral("spel: Halo\nstand: goed\n"), QStringLiteral("'bezig' ontbreekt")},
        {QStringLiteral("bezig: Testen\nstand: goed\n"), QStringLiteral("'spel' ontbreekt")},
        {QStringLiteral("spel:\nbezig: Testen\nstand: goed\n"), QStringLiteral("'spel' ontbreekt")},
        {good.left(good.size() - 6) + QStringLiteral("prima\n"), QStringLiteral("goed, slecht of bezig")},
        {good + QStringLiteral("stap: 6 van 5\n"), QStringLiteral("dat kan niet")},
        {good + QStringLiteral("stap: 0 van 5\n"), QStringLiteral("dat kan niet")},
        {good + QStringLiteral("stap: derde\n"), QStringLiteral("3 van 5")},
        {good + QStringLiteral("kleur: groen\n"), QStringLiteral("onbekende sleutel 'kleur'")},
        {good + QStringLiteral("spel: Rabbids\n"), QStringLiteral("twee keer")},
        {good + QStringLiteral("doel: ") + QString(200, QLatin1Char('x')) + QStringLiteral("\n"), QStringLiteral("te lang")},
        {good + QStringLiteral("nog: a;b;c;d;e;f;g;h;i\n"), QStringLiteral("9 punten")},
        {QStringLiteral("{\"spel\": \"Halo\"}\n"), QStringLiteral("onbekende sleutel")},
    };
    for (const auto& [text, why] : bad) {
        const ui::SaverStatus s = SaverWindow::ParseStatus(text);
        QVERIFY2(!s.ok, qPrintable(text));
        QVERIFY2(s.error.contains(why), qPrintable(s.error + QStringLiteral(" <- ") + why));
        const QStringList lines = SaverWindow::Lines(s, SaverGame::None, 1000);
        QCOMPARE(lines.value(1), QStringLiteral("Geen status beschikbaar"));
        QVERIFY(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("Het gaat goed")));
    }

    // Een bestand dat ontbreekt.
    QTemporaryDir dir;
    const ui::SaverStatus missing = SaverWindow::LoadStatus(dir.path() + QStringLiteral("/bestaat-niet.txt"));
    QVERIFY(!missing.ok);
    QVERIFY(missing.error.contains(QStringLiteral("ontbreekt")));

    // Een oude status wordt als oud gemeld, niet als actueel.
    ui::SaverStatus old = ok;
    old.updated = 1000;
    QVERIFY(SaverWindow::Lines(old, SaverGame::None, 1000 + 60).last().contains(QStringLiteral("bijgewerkt zojuist")));
    QVERIFY(SaverWindow::Lines(old, SaverGame::None, 1000 + 20 * 60).last().contains(QStringLiteral("bijgewerkt 20 min geleden")));
    const QString stale = SaverWindow::Lines(old, SaverGame::None, 1000 + 3 * 3600).last();
    QVERIFY2(stale.contains(QStringLiteral("LET OP: deze status is oud")), qPrintable(stale));

    // Draait er nu een spel? Alleen een vers bestand met een bekend woord telt; anders weten we het niet.
    QCOMPARE(SaverWindow::ParseGame(QStringLiteral("geen\n")), SaverGame::None);
    QCOMPARE(SaverWindow::ParseGame(QStringLiteral("zichtbaar\n")), SaverGame::Visible);
    QCOMPARE(SaverWindow::ParseGame(QStringLiteral("onzichtbaar\n")), SaverGame::Hidden);
    QCOMPARE(SaverWindow::ParseGame(QStringLiteral("misschien\n")), SaverGame::Unknown);
    QCOMPARE(SaverWindow::ParseGame(QString()), SaverGame::Unknown);
    QFile g(dir.path() + QStringLiteral("/saver-game.txt"));
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write("geen\n");
    g.close();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QCOMPARE(SaverWindow::LoadGame(g.fileName(), now), SaverGame::None);
    QCOMPARE(SaverWindow::LoadGame(g.fileName(), now + 120), SaverGame::Unknown);          // niemand houdt het bij
    QCOMPARE(SaverWindow::LoadGame(dir.path() + QStringLiteral("/nee.txt"), now), SaverGame::Unknown);
    QCOMPARE(SaverWindow::Lines(ok, SaverGame::Unknown, 1000).value(0), QStringLiteral("Onbekend of er nu een spel draait"));
    QCOMPARE(SaverWindow::Lines(ok, SaverGame::None, 1000).value(0), QStringLiteral("Er draait nu geen spel"));
}

void AppWidgetTest::saverQuestionAndFeed() {
    // Nieuwe sleutels: vraag en doelen. Een oud bestand zonder ze blijft gewoon werken.
    using ui::SaverGame;
    using ui::SaverWindow;
    const QString base = QStringLiteral("spel: Halo\nbezig: Testen\nstand: bezig\n");
    const ui::SaverStatus old = SaverWindow::ParseStatus(base);
    QVERIFY(old.ok);
    QVERIFY(old.question.isEmpty());
    QVERIFY(old.goals.isEmpty());
    const ui::SaverStatus q = SaverWindow::ParseStatus(base + QStringLiteral("vraag: Met of zonder kalibratie?\ndoelen: Alles halen; Wiki\n"));
    QVERIFY2(q.ok, qPrintable(q.error));
    QCOMPARE(q.question, QStringLiteral("Met of zonder kalibratie?"));
    QCOMPARE(q.goals, (QStringList{QStringLiteral("Alles halen"), QStringLiteral("Wiki")}));
    const QStringList lines = SaverWindow::Lines(q, SaverGame::None, 1000);
    QCOMPARE(lines.value(1), QStringLiteral("Vraag voor jou: Met of zonder kalibratie?"));
    QVERIFY(lines.join(QLatin1Char('\n')).contains(QStringLiteral("Doelen: Alles halen · Wiki")));
    QVERIFY(!SaverWindow::ParseStatus(base + QStringLiteral("vraag: ") + QString(301, QLatin1Char('x')) + QLatin1Char('\n')).ok);
    QVERIFY(SaverWindow::ParseStatus(base + QStringLiteral("vraag: ") + QString(250, QLatin1Char('x')) + QLatin1Char('\n')).ok);
    QVERIFY(!SaverWindow::ParseStatus(base + QStringLiteral("vraag: a\nvraag: b\n")).ok);

    // De feed: nieuwste eerst, onzin overgeslagen, afgekapt op het gevraagde aantal.
    const auto feed = SaverWindow::ParseFeed(QStringLiteral("2026-10-14T10:00:00 eerste\nonzin\n\n2026-10-14T10:05:00 tweede\n2026-10-14T10:09:00 derde\n"), 2);
    QCOMPARE(feed.size(), 2);
    QCOMPARE(feed.at(0).text, QStringLiteral("derde"));
    QCOMPARE(feed.at(1).text, QStringLiteral("tweede"));
    QCOMPARE(QDateTime::fromSecsSinceEpoch(feed.at(0).time).time().toString(QStringLiteral("HH:mm")), QStringLiteral("10:09"));
    QVERIFY(SaverWindow::ParseFeed(QStringLiteral("zomaar tekst\n"), 5).isEmpty());

    // Snapshot met een vraag: de balk is fel, de rest blijft donker; zonder bestanden is er geen balk en geen feed.
    QTemporaryDir state;
    qputenv("XDG_STATE_HOME", state.path().toUtf8());
    QVERIFY(QDir().mkpath(state.path() + QStringLiteral("/ravengun")));
    writeFile(state.path() + QStringLiteral("/ravengun/saver-status.txt"), (base + QStringLiteral("vraag: Welke Move?\n")).toUtf8());
    writeFile(state.path() + QStringLiteral("/ravengun/saver-feed.txt"), "2026-10-14T10:00:00 Halo gestart\n");
    ui::SaverWindow w;
    const QString png = tmp_.filePath(QStringLiteral("saver.png"));
    QVERIFY(w.SaveSnapshot(png, QSize(1920, 1080)));
    const QImage img(png);
    QCOMPARE(img.size(), QSize(1920, 1080));
    QVERIFY(w.ShownLines().value(1).startsWith(QStringLiteral("Vraag voor jou")));
    qint64 lit = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) lit += qGray(img.pixel(x, y)) > 16 ? 1 : 0;
    QVERIFY2(lit * 100 < qint64(img.width()) * img.height() * 25, qPrintable(QString::number(lit)));
    QVERIFY(lit > 20000);   // er staat echt iets
    // Een oude status: dezelfde snapshot werkt en toont geen vraag.
    writeFile(state.path() + QStringLiteral("/ravengun/saver-status.txt"), base.toUtf8());
    QVERIFY(w.SaveSnapshot(png, QSize(1280, 720)));
    QVERIFY(!w.ShownLines().join(QLatin1Char('\n')).contains(QStringLiteral("Vraag voor jou")));
    qunsetenv("XDG_STATE_HOME");
}

QTEST_MAIN(AppWidgetTest)
#include "test_app_widgets.moc"
