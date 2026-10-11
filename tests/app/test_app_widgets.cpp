// SPDX-License-Identifier: GPL-3.0-or-later
// Tests met vensters (offscreen), zonder engine: Properties voor alle games uit de echte packaging/game-options.json, de
// route-keuze, de fullscreen-modus (vizier, schot, toetsen), de wizard met een nep-pkexec en "Add to Steam" op een kopie.
// Alles in een tijdelijke map (HOME, XDG_*); de "NAS" is een tijdelijke map via $RAVENGUN_NAS_PREFIXES.
#include <QApplication>
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
    QCOMPARE(db.entries().size(), 37);
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
    QCOMPARE(dialogs, 37);
    QCOMPARE(total, 564);
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
    QCOMPARE(t->topLevelItemCount(), 1);
    QTreeWidgetItem* it = t->topLevelItem(0);
    QCOMPARE(it->text(0), QStringLiteral("OpenParrotLoader zonder CONTEXT_FLOATING_POINT"));
    QCOMPARE(it->checkState(0), Qt::Checked);
    QVERIFY(it->toolTip(0).contains(QStringLiteral("MXCSR")));                       // de reden als tooltip
    t->setCurrentItem(it);
    QVERIFY(it->data(0, Qt::UserRole + 1).toString().contains(QStringLiteral("MXCSR")));
    // Uitvinken schrijft de gebruikerslaag (niet de meegeleverde laag).
    it->setCheckState(0, Qt::Unchecked);
    const QString userFile = userRoot + QStringLiteral("/GaiaAttack4/patches.ini");
    QVERIFY(readAll(userFile).contains(QStringLiteral("Enabled = false")));
    QCOMPARE(core.gameData()->patches(QStringLiteral("GaiaAttack4"))[0].enabled, false);
    QCOMPARE(t->topLevelItem(0)->checkState(0), Qt::Unchecked);
    QVERIFY(t->topLevelItem(0)->text(2).contains(QStringLiteral("your choice")) || t->topLevelItem(0)->text(2).contains(QStringLiteral("choice")));
    t->topLevelItem(0)->setCheckState(0, Qt::Checked);                                 // gelijk aan de meegeleverde stand: keuze weg
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
    QCOMPARE(w.GameCount(), 4);  // Jurassic Park is geparkeerd: alleen met "Show All Games"
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
    // OLED-saver: schermvullend, zonder rand, zwart, met een klokje dat bijwerkt en verspringt.
    ui::SaverWindow w;
    w.Open();
    QVERIFY(w.isFullScreen());
    QVERIFY(w.windowFlags() & Qt::FramelessWindowHint);
    QVERIFY(w.windowFlags() & Qt::WindowStaysOnTopHint);
    QCOMPARE(w.cursor().shape(), Qt::BlankCursor);
    QCOMPARE(w.palette().color(QPalette::Window), QColor(Qt::black));
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    const QImage img = w.grab().toImage();
    QVERIFY(!img.isNull());
    // Alleen zwart en het gedimde klokje: geen pixel helderder dan 45.
    int maxLevel = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) maxLevel = qMax(maxLevel, qGray(img.pixel(x, y)));
    QVERIFY2(maxLevel <= 45, qPrintable(QString::number(maxLevel)));
    QVERIFY(QRegularExpression(QStringLiteral("^\\d\\d:\\d\\d$")).match(w.ClockText()).hasMatch());
    QVERIFY(QRect(QPoint(0, 0), w.size()).contains(w.ClockGeometry()));
    // Het klokje springt naar steeds andere plekken, altijd binnen beeld.
    QSet<QPair<int, int>> seen;
    for (int i = 0; i < 20; ++i) {
        w.Reposition();
        QVERIFY(QRect(QPoint(0, 0), w.size()).contains(w.ClockGeometry()));
        seen.insert({w.ClockGeometry().x(), w.ClockGeometry().y()});
    }
    QVERIFY(seen.size() > 1);
    if (const QString out = qEnvironmentVariable("RAVENGUN_SAVER_SHOT"); !out.isEmpty()) QVERIFY(w.grab().save(out));
}

QTEST_MAIN(AppWidgetTest)
#include "test_app_widgets.moc"
