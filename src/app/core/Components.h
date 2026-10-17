// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// De onderdelen die RavenGun nodig heeft in de gebruikersmap (~/.local/share/ravengun): Wine, DXVK, BepInEx, mods, DirectX-DLL's
// uit Steam, 32-bits bibliotheken, PhysX, eigen gebouwde onderdelen en de TeknoParrot-runtime van de gebruiker. De catalogus staat
// als gegevens in packaging/components.json (ingebakken in de binary); dit bestand leest hem en bepaalt per onderdeel of het er is.
// Het installeren zelf staat in ComponentInstaller (geen shellscripts).
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace ravengun::gamesettings { struct Dirs; }

namespace rg {

struct ComponentDownload {
    QString name;      // bestandsnaam in de downloadmap
    QString url;
    QString sha256;    // hex; leeg alleen voor mods (daar bewaakt TreeSha256 de inhoud)
    qint64 size = 0;   // bytes (0 = onbekend)
    QString archive;   // tar.gz | tar.xz | zip | pkg.tar.zst | cab | file (bestand ongewijzigd, bv. een msi)
};

struct Component {
    QString id, name, description, version, group;
    // download: bestanden van het internet; bundled: meegeleverd in <datamap>/components/<id>; userfolder: uit de map van de
    // gebruiker (gamemappen); steam: uit de Steam-installatie van de gebruiker; wine: draait Wine (prefix, PhysX).
    QString kind;
    QString recipe;    // extract (standaard) | redist-directx | redist-vc2010 | gst32 | lib32-sniper | wine-prefix | wine-physx | copy-tree
    QString dest;      // relatief aan de datamap ("" = de datamap zelf)
    QStringList detect;   // paden (relatief aan de datamap) die er moeten zijn
    QList<ComponentDownload> downloads;
    int strip = 0;
    QString subdir;
    QStringList include;
    QList<QPair<QString, QString>> map;
    bool merge = false;   // bestanden in dest zetten zonder de map te vervangen
    QList<QPair<QString, QString>> links;        // pad -> doel (symlink), na het installeren
    QList<QPair<QString, QString>> writeFiles;   // pad -> tekst, na het installeren
    QStringList dependsOn;
    QString treeSha256;   // mods: sha256 over de uitgepakte boom
    QStringList sourceNames;   // userfolder: mapnamen (glob) om te zoeken, bv. "TeknoParrotUi-*"
    QStringList sourceRequire; // userfolder: paden die in de bronmap moeten staan
    QStringList exclude;       // userfolder/bundled: globs die niet gekopieerd worden
    QString note;
    bool optional = false;

    qint64 totalSize() const {
        qint64 s = 0;
        for (const auto& d : downloads) s += d.size;
        return s;
    }
};

enum class ComponentState { Installed, Missing, Outdated, Unavailable };

struct ComponentStatus {
    ComponentState state = ComponentState::Missing;
    QString detail;   // Engelse toelichting (waar gevonden, waarom niet beschikbaar)
    QString source;   // userfolder: gevonden bronmap
};

// Waar gezocht wordt. Tests zetten dit op een tijdelijke map.
struct ComponentEnv {
    QString dataDir;                // ~/.local/share/ravengun
    QString bundledRoot;            // <datamap>/components (leeg = geen)
    QString steamRoot;              // leeg = ~/.local/share/Steam
    QStringList sourceRoots;        // gamemappen van de gebruiker (userfolder-onderdelen zoeken hier)
    QString path(const QString& rel) const { return rel.isEmpty() ? dataDir : dataDir + QLatin1Char('/') + rel; }
    static ComponentEnv standard();   // dataDir uit AppPaths, bundledRoot uit de datamap
};

class Components {
public:
    // Leest de JSON; bij een fout een lege lijst en een melding (Engels) in err.
    static QList<Component> parse(const QByteArray& json, QString* err = nullptr);
    // packaging/components.json (bestand in de datamap, anders de ingebakken kopie).
    static QList<Component> catalogue(QString* err = nullptr);
    // Mods uit [Mod <naam>] in game.ini van de games die de app gebruikt (zoekvolgorde van gamesettings), als onderdelen.
    static QList<Component> modComponents(const ravengun::gamesettings::Dirs& dirs);
    static QList<Component> modComponents();   // met de standaardmappen van de app
    static Component* find(QList<Component>& all, const QString& id);

    static ComponentStatus status(const Component& c, const ComponentEnv& env);
    // Het onderdeel zelf en wat het nodig heeft, in installatievolgorde.
    static QList<Component> withDependencies(const QList<Component>& all, const QStringList& ids);

    // Stempel van een installatie: <data>/components/<id>.json (versie en sha256 van de bestanden).
    static QString stampPath(const ComponentEnv& env, const QString& id);
    static bool writeStamp(const ComponentEnv& env, const Component& c, QString* err = nullptr);
    static QString stampVersion(const ComponentEnv& env, const QString& id);

    // sha256 van een bestand (hex) en van een boom (regels "sha256  ./pad", op pad gesorteerd; gelijk aan scripts/fetch-mods.py).
    static QString sha256File(const QString& path);
    static QString treeSha256(const QString& root);

    // Steam: de bibliotheekmappen (steamapps/common/...) van de gebruiker.
    static QStringList steamLibraries(const ComponentEnv& env);
    // userfolder: de eerste map onder de gamemappen die voldoet (leeg als er geen is).
    static QString findUserFolderSource(const Component& c, const ComponentEnv& env);
    static QString formatSize(qint64 bytes);
    // Steam: waar de bronnen staan (leeg als ze er niet zijn).
    static QString findRedistDir(const ComponentEnv& env);        // .../Steamworks Shared/_CommonRedist/DirectX/Jun2010
    static QString findVcredistDir(const ComponentEnv& env);      // .../Steamworks Shared/_CommonRedist/vcredist/2010
    static QString findProtonGst32(const ComponentEnv& env);      // files/lib/i386-linux-gnu van een Proton met libgstlibav
    static QString findSniperLib32(const ComponentEnv& env);      // var/tmp-*/usr/lib/i386-linux-gnu met libglut
    static QString findSniperPlatformLib32(const ComponentEnv& env);   // sniper_platform_*/files/lib/i386-linux-gnu
};

}  // namespace rg
