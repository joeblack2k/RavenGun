// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Installeren, de engine als service en verwijderen, alles vanuit de app (docs/build.md, "Installatie-indeling"). Dit vervangt
// scripts/install-user.sh, install-service.sh en uninstall-user.sh voor het product: geen shell, alleen C++ en de
// programma's systemctl, update-desktop-database en pkexec (met het eigen hulpprogramma van RavenGun) met vaste argumenten.
//
// Alles zonder root. De delen die root nodig hebben (udev-regels, Bluetooth-hulp, sudoers) staan in SetupChecks (pkexec op
// ravengun-setup-helper). De padlogica en het genereren van units en snelkoppelingen zijn zuivere functies (getest zonder
// bestanden); de uitvoering werkt op een InstallLayout, zodat tests alles in een tijdelijke HOME kunnen doen.
//
// Omgeving voor tests en ontwikkelaars: $RAVENGUN_PREFIX (andere installatiemap), $RAVENGUN_SYSTEMCTL (vervangt systemctl).
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace rg {

using LogFn = std::function<void(const QString&)>;

// Alle plekken van een installatie voor een gebruiker; XDG wordt gevolgd.
struct InstallLayout {
    QString home;
    QString prefix;           // standaard ~/.local/opt/ravengun
    QString unitDir;          // $XDG_CONFIG_HOME/systemd/user
    QString applicationsDir;  // $XDG_DATA_HOME/applications
    QString desktopDir;       // het bureaublad (XDG_DESKTOP_DIR of ~/Desktop); leeg als die map er niet is
    QString localBinDir;      // ~/.local/bin
    QString configDir;        // $XDG_CONFIG_HOME/ravengun
    QString stateDir;         // $XDG_STATE_HOME/ravengun
    QString userDataDir;      // $XDG_DATA_HOME/ravengun (TeknoParrot-runtime, Wine, ...: wordt nooit verwijderd)

    QString binDir() const { return prefix + QStringLiteral("/bin"); }
    QString dataDir() const { return prefix + QStringLiteral("/share/ravengun"); }
    QString engineUnitFile() const { return unitDir + QStringLiteral("/ravengund.service"); }
    QString oledUnitFile() const { return unitDir + QStringLiteral("/ravengun-oled-saver.service"); }
    QString desktopFile() const { return applicationsDir + QStringLiteral("/ravengun-qt.desktop"); }

    // Uit expliciete waarden (leeg = standaard onder `home`). Kijkt alleen of de bureaublad-map bestaat.
    static InstallLayout forEnvironment(const QString& home, const QString& xdgConfig, const QString& xdgData, const QString& xdgState,
                                        const QString& prefixOverride);
    // Voor de huidige gebruiker: QDir::homePath(), de XDG-variabelen en $RAVENGUN_PREFIX.
    static InstallLayout current();
};

struct OpResult {
    bool ok = false;
    QString error;     // voor de gebruiker (Engels), leeg bij ok
    QString prefix;    // bij installeren: waar het staat
};

class Install {
public:
    // --- Zuivere logica ---------------------------------------------------------------------------------------------------
    // Is dit een veilige installatiemap? Absoluut, schoon, zonder spaties of tekens die in een unit of snelkoppeling stuk
    // gaan (% " ' \), niet "/", de thuismap zelf of erboven, en niet onder een systeemmap. why krijgt de reden.
    static bool prefixIsSafe(const QString& prefix, const QString& home, QString* why = nullptr);
    // Een boom met bin/ravengund, bin/ravengunctl en share/ravengun/packaging (de vorm van een installatie en van een releasearchief).
    static bool isRavenGunTree(const QString& prefix);
    // De map boven bin/ van het draaiende programma als dat een volledige boom is; anders leeg (bv. build-app/ bij ontwikkeling).
    static QString runningTreePrefix(const QString& binDir);
    // Draait het programma uit de installatiemap zelf? (kanonieke paden vergeleken)
    static bool runsFromPrefix(const QString& binDir, const QString& prefix);
    // Versies ("2.38" tegen "2.43.1"): <0, 0, >0.
    static int compareVersions(const QString& a, const QString& b);
    // Uit share/ravengun/REQUIRES ("glibc 2.43"): een melding als deze glibc te oud is, anders leeg.
    static QString glibcProblem(const QString& requiresText, const QString& haveVersion);
    static QString runtimeGlibc();
    // Systeembibliotheken die een programma mist (ld.so --list): regels "libfoo.so.1". ldSo leeg = /lib64/ld-linux-x86-64.so.2.
    static QStringList missingLibraries(const QString& binary, const QString& ldSo = QString());
    // Sjablonen invullen. @BINDIR@ en @DATADIR@ moeten veilig zijn voor een unit (geen spaties, %, aanhalingstekens, backslash).
    static bool unitPathSafe(const QString& path);
    static QString renderTemplate(const QString& text, const QString& binDir, const QString& dataDir);
    // De inhoud van de unit of snelkoppeling uit het sjabloon in <dataDir>/packaging; leeg + err als het sjabloon ontbreekt of een pad niet veilig is.
    static QString engineUnitText(const QString& dataDir, const QString& binDir, QString* err);
    static QString oledUnitText(const QString& dataDir, const QString& binDir, QString* err);
    static QString desktopEntryText(const QString& dataDir, const QString& binDir, QString* err);
    // Waar ravengund staat bij deze datamap: <prefix>/bin bij een installatie, anders <repo>/build ($RAVENGUN_BIN_DIR gaat voor).
    static QString engineBinDirFor(const QString& dataDir);
    // XDG_DESKTOP_DIR uit een user-dirs.dirs (met $HOME ingevuld); leeg als het er niet in staat.
    static QString desktopDirFromUserDirs(const QString& userDirsText, const QString& home);

    // --- Uitvoering (blokkerend; roep ze buiten de GUI-thread aan, zie runAsync) ------------------------------------------------
    // Draait een programma tot het klaar is. $RAVENGUN_SYSTEMCTL vervangt "systemctl" als program == "systemctl".
    static int runProgram(const QString& program, const QStringList& args, QString* output, int timeoutMs = 30000);
    static int systemctlUser(const QStringList& args, QString* output = nullptr);

    struct InstallOptions {
        bool start = true;                 // de engine starten (of herstarten als er geen game draait)
        bool shortcuts = true;             // menu-item, bureaubladpictogram en koppelingen in ~/.local/bin
        QString updateDesktopDatabase;     // programma voor update-desktop-database; leeg = /usr/bin/update-desktop-database als het er is
        QString procDir = QStringLiteral("/proc");  // voor "draait er een game?" (tests)
    };
    // Kopieert de boom van `sourcePrefix` (bin/, lib/, share/) naar layout.prefix (eerst ernaast klaargezet, dan verwisseld;
    // gebruikersgegevens blijven ongemoeid), controleert de eisen, schrijft de units en snelkoppelingen en start de engine.
    static OpResult installTree(const QString& sourcePrefix, const InstallLayout& layout, const InstallOptions& opt, const LogFn& log);
    // De units en snelkoppelingen opnieuw schrijven vanuit de datamap `dataDir` (zonder te kopiëren) en de engine activeren.
    static OpResult writeEngineUnit(const InstallLayout& layout, const QString& dataDir, const QString& binDir, const LogFn& log);
    static OpResult writeOledUnit(const InstallLayout& layout, const QString& dataDir, const QString& binDir, const LogFn& log);
    static OpResult writeShortcuts(const InstallLayout& layout, const InstallOptions& opt, const QString& dataDir, const QString& binDir,
                                   const LogFn& log);
    static OpResult enableEngine(bool start, const LogFn& log);  // daemon-reload + enable [--now]
    // Herstart de engine alleen als er geen game draait. false (en een melding) als er een game draait.
    static bool restartEngineIfIdle(const QString& procDir, const LogFn& log);
    static bool gameRunning(const QString& procDir);

    struct UninstallOptions {
        bool removeConfig = false;   // ook ~/.config/ravengun en ~/.local/state/ravengun
        bool keepRoot = false;       // de systeemonderdelen (udev, Bluetooth-hulp, sudoers) laten staan
        QString pkexec;              // leeg = $RAVENGUN_PKEXEC of "pkexec"
        QString setupHelper;         // wat voor de rootdelen met pkexec gestart wordt (--rollback); leeg = overslaan
        QString updateDesktopDatabase;
        bool systemPartsPresent = false;  // door de aanroeper bepaald (SetupChecks::systemPartsInstalled)
    };
    static OpResult uninstall(const InstallLayout& layout, const UninstallOptions& opt, const LogFn& log);

    // Het bestand/de map recursief kopiëren zoals de installatie het doet (rechten en symlinks behouden). Voor tests en installTree.
    static bool copyTree(const QString& from, const QString& to, QString* err);
};

// Voert `work` uit in een eigen thread en roept `done` aan in de thread van `context` (of nooit, als die eerder verdwijnt).
// De LogFn die work krijgt, mag vanuit die thread worden aangeroepen; de tekst komt in de thread van `context` bij `onLog`.
void runAsync(QObject* context, const std::function<OpResult(const LogFn&)>& work, const LogFn& onLog,
              const std::function<void(const OpResult&)>& done);

}  // namespace rg
