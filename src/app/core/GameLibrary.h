// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// De gamebibliotheek van de app: gamemappen ("Add Folder"), scannen en herkennen, bewaren in ~/.config/ravengun/library.json.
//
// Herkenning (LibraryScanner::scan, zonder Qt-GUI, in een eigen thread):
//  1. TeknoParrot-profielen: elk UserProfiles/<P>.xml in de TeknoParrot-map is een game (kind TeknoParrot), met naam en
//     platform uit Metadata/<P>.json, icoon uit Icons/.
//  2. Unity-route: packaging/aagames-games.json (nas_dir) herkent Tomb Raider en Rabbids Hollywood; een TeknoParrot-profiel
//     met dezelfde naam gaat voor (dat start via play-halo.sh ook de Unity-route).
//  1b. Games met een eigen profiel in game.ini (patches/<Game>/ met de gebruikerslaag; ScanInput::ownGames): het pad wordt niet opgeslagen
//     maar afgeleid, uit de lokale view van de game of uit de gamemappen aan [Identify] (Dolphin: ISOPath0..N en het game-ID). Zo'n
//     game staat zonder UserProfiles-XML in de lijst; een profiel dat ook als XML bestaat komt uit game.ini.
//  3. Elke submap van een gamemap is een kandidaat-game. Wijst een gebruikersprofiel (GamePath, ook via een view-symlink)
//     naar een bestand in die map, dan is het die game. Anders worden de bestandsnamen in de map (recursief tot diepte 6,
//     of 1 zonder "Search subfolders") vergeleken met ExecutableName van de sjablonen in GameProfiles/: één kandidaat, of
//     de beste op naamgelijkenis met de mapnaam, geeft kind Detected (sjabloon bekend, nog geen gebruikersprofiel).
//  4. De rest is Unconfigured; de gebruiker kan hem koppelen aan een sjabloon (Properties > General).
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include "ravengun/gamesettings/gamesettings.hpp"

namespace rg {

enum class GameKind { TeknoParrot, Unity, Detected, Unconfigured };
QString kindLabel(GameKind k);  // Engels
QString platformLabel(const QString& emulatorType);  // EmulatorType -> neutrale platformnaam

struct GameEntry {
    QString id;          // tp:<profiel>, unity:<sleutel>, dir:<map>
    GameKind kind = GameKind::Unconfigured;
    QString name;
    QString profile;     // TeknoParrot-gebruikersprofiel (UserProfiles/<profile>.xml), of de Unity-sleutel
    QString templateProfile;  // sjabloon in GameProfiles/ (Detected, of gekoppeld door de gebruiker)
    QStringList candidates;   // andere mogelijke sjablonen (Unconfigured/Detected)
    QString folder;      // gamemap (zoals gevonden in een gamemap van de bibliotheek)
    QString exePath;     // GamePath of het herkende uitvoerbare bestand
    QString iconPath;    // leeg = geen
    QString platform;    // bv. "Namco ES3X", "Unity (PC)"
    QString emulator;    // EmulatorType uit het profiel
    QString genre;
    bool gunGame = false;
    bool exeExists = true;
    QString sourceFolder;  // de bibliotheekmap waarin hij gevonden is (leeg = TeknoParrot-map)

    // Van de gebruiker (blijft bij herscannen staan, zie LibraryData::overrides).
    QString userName, userNote;

    QString displayName() const { return userName.isEmpty() ? name : userName; }
    bool hasProfile() const { return kind == GameKind::TeknoParrot || (kind == GameKind::Unity && !profile.isEmpty()); }
    QJsonObject toJson() const;
    static GameEntry fromJson(const QJsonObject& o);
};

struct LibraryFolder {
    QString path;
    bool recursive = true;
};

// Instellingen van de gebruiker per game. Dit is de bron van waarheid (besluit 2026-10-10); profielbestanden van de
// launcher-backend worden hieruit gegenereerd (rg::ProfileSync).
struct GameOverride {
    QString name, note, templateProfile;
    bool hidden = false;
    QVariantMap options;   // game-options.json: sleutel -> waarde (opslagvorm, tekst)
    QVariantMap tpFields;  // losse profielvelden: "Categorie/Veld" -> waarde
};

// Inhoud van library.json.
struct LibraryData {
    int schema = 1;
    QList<LibraryFolder> folders;
    QString tpDir;          // leeg = standaard (AppPaths::defaultTpDir)
    QString defaultGame;    // id
    QString launcher;       // launch-backend: auto (standaard, ook als leeg), direct of teknoparrot-ui (core/Launcher.h)
    QHash<QString, GameOverride> overrides;
    QList<GameEntry> games; // laatste scan (cache voor een snelle start)
    QDateTime scanned;

    bool load(const QString& path, QString* err = nullptr);
    bool save(const QString& path, QString* err = nullptr) const;
    QString effectiveTpDir() const;
};

// Gegevens over een sjabloon in GameProfiles/ (voor herkenning en de koppelkeuze).
struct TemplateInfo {
    QString profile;      // bestandsnaam zonder .xml
    QString name;         // Metadata game_name, anders de profielnaam
    QString platform, genre, emulator, iconName;
    QStringList exeNames; // kleine letters
    bool gunGame = false;
};

// Een game die game-options.json kent (mapnaam op de NAS, titel, profiel): herkent een map ook zonder uitvoerbaar bestand.
struct KnownGame {
    QString folder, title, profile, platform, exe;
};

// Een game met een eigen profiel in game.ini (patches/<Game>/ met de gebruikerslaag): de bibliotheek herkent hem in de gamemappen aan
// [Identify], zoals Dolphin games herkent op hun ID (docs/per-game-patches.md).
struct OwnGame {
    QString profile, name;
    ravengun::gamesettings::Ini settings;   // samengevoegd (standaard van het soort, meegeleverd, gebruiker)
};

struct ScanInput {
    QList<LibraryFolder> folders;
    QList<KnownGame> known;
    QList<OwnGame> ownGames;   // games met een eigen profiel; de views en de gamemappen worden doorzocht
    QString viewsDir;          // runtime/views (leeg = geen)
    QString tpDir;
    QString aagamesJson;  // pad naar packaging/aagames-games.json (leeg = geen)
    int maxDepth = 6;
    int maxEntriesPerGame = 6000;
};

struct ScanResult {
    QList<GameEntry> games;
    QStringList log;             // Engels, voor Advanced > log
    QString detectedTpDir;       // een TeknoParrot-installatie gevonden in een gamemap
    int templates = 0;
};

class LibraryScanner {
public:
    using Progress = std::function<void(const QString& what)>;
    static ScanResult scan(const ScanInput& in, const std::atomic<bool>* cancel = nullptr, const Progress& progress = {});
    static QList<TemplateInfo> loadTemplates(const QString& tpDir, const std::atomic<bool>* cancel = nullptr);
    static TemplateInfo templateInfo(const QString& tpDir, const QString& profile);
    // Een TeknoParrot-installatie (UserProfiles/ of GameProfiles/ plus TeknoParrotUi)?
    static bool isTeknoParrotDir(const QString& dir);
    // Woordgelijkenis 0..1 tussen een mapnaam en een gamenaam ("Time Crisis 5" ~ "Time Crisis 5 (Ver. 2)").
    static double nameSimilarity(const QString& a, const QString& b);
    static QString humanize(const QString& id);
};

// QObject-laag rond LibraryData + LibraryScanner: bewaart, scant in de achtergrond en meldt wijzigingen. Bedoeld voor het
// hoofdvenster en (later) de fullscreen-modus; zonder widgets.
class GameLibrary : public QObject {
    Q_OBJECT
public:
    explicit GameLibrary(QString file = {}, QObject* parent = nullptr);
    ~GameLibrary() override;

    const LibraryData& data() const { return data_; }
    QList<GameEntry> visibleGames() const;  // zonder verborgen, met overrides toegepast
    const GameEntry* find(const QString& id) const;
    QString file() const { return file_; }
    QString tpDir() const { return data_.effectiveTpDir(); }
    bool scanning() const { return scanning_; }
    QStringList lastLog() const { return log_; }

    void load();
    bool save(QString* err = nullptr);

    void addFolder(const QString& path, bool recursive = true);
    void removeFolder(const QString& path);
    void setFolderRecursive(const QString& path, bool recursive);
    void setTpDir(const QString& dir);
    void setDefaultGame(const QString& id);
    void setLauncher(const QString& backend);
    void setOverride(const QString& id, const GameOverride& o);
    GameOverride override_(const QString& id) const { return data_.overrides.value(id); }
    void hideGame(const QString& id);
    void unhideAll();

    // Start een scan in de achtergrond (een lopende scan wordt eerst afgebroken).
    void rescan();
    void setKnownGames(const QList<KnownGame>& known) { known_ = known; }
    void setOwnGames(const QList<OwnGame>& own) { own_ = own; }

signals:
    void gamesChanged();
    void foldersChanged();
    void scanStarted();
    void scanProgress(const QString& what);
    void scanFinished(int games);
    void tpDirDetected(const QString& dir);

private:
    void applyResult(const ScanResult& r, quint64 gen);
    void stopScan();
    QString file_;
    LibraryData data_;
    QStringList log_;
    QList<KnownGame> known_;
    QList<OwnGame> own_;
    bool scanning_ = false;
    quint64 gen_ = 0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::thread worker_;
};

}  // namespace rg
