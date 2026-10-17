// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Steam-integratie (A8): de app als niet-Steam-game in de bibliotheek van de ingelogde gebruiker.
//
// Steam bewaart niet-Steam-snelkoppelingen in ~/.local/share/Steam/userdata/<account-id>/config/shortcuts.vdf, in het
// binaire KeyValues-formaat ("binary VDF"). Eigen lezer en schrijver (geen code van derden):
//   bestand = items, dan 0x08 (einde van de wortel)
//   item    = type (1 byte), sleutel (UTF-8, NUL-afgesloten), waarde
//   type    0x00 map (items tot 0x08), 0x01 tekst (NUL-afgesloten), 0x02 int32 LE, 0x03 float32, 0x04 pointer (4 bytes),
//           0x05 brede tekst (UTF-16, afgesloten met twee nulbytes), 0x06 kleur (4 bytes), 0x07 uint64, 0x0A int64,
//           0x08 einde van een map (0x0B wordt ook als einde gelezen)
// Onbekende typen maken het bestand onleesbaar: dan wordt er niets geschreven. Waarden worden byte voor byte bewaard,
// dus lezen en weer schrijven geeft precies hetzelfde bestand.
//
// Een snelkoppeling: {"shortcuts": {"0": {appid, appname, exe, StartDir, icon, ShortcutPath, LaunchOptions, IsHidden,
// AllowDesktopConfig, AllowOverlay, OpenVR, Devkit, DevkitGameID, DevkitOverrideAppID, LastPlayTime, FlatpakAppID, sortas,
// tags{}}, "1": {...}}}. Sleutels worden hoofdletterongevoelig vergeleken (oudere Steam schrijft AppName/Exe).
//
// appid: we berekenen hem zoals de bekende hulpprogramma's (crc32 van exe + appname, hoogste bit aan) en schrijven hem in het
// item; Steam gebruikt het opgeslagen appid, en de afbeeldingen in config/grid/ heten naar dezelfde (unsigned) waarde:
// <id>p.png (staand, 600x900), <id>.png (liggend, 920x430), <id>_hero.png (1920x620), <id>_logo.png en <id>_icon.png.
//
// Steam leest en overschrijft shortcuts.vdf zelf; daarom alleen schrijven als Steam niet draait (steamRunning()).
#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

namespace rg {

struct VdfNode {
    enum Type : quint8 { Map = 0x00, String = 0x01, Int32 = 0x02, Float = 0x03, Ptr = 0x04, WString = 0x05, Color = 0x06,
                         UInt64 = 0x07, Int64 = 0x0A };
    Type type = Map;
    QByteArray key;
    QByteArray value;         // tekst (zonder NUL), brede tekst (ruwe bytes zonder afsluiter) of de ruwe bytes van een getal
    QList<VdfNode> children;  // Map

    static VdfNode map(const QByteArray& key) { VdfNode n; n.type = Map; n.key = key; return n; }
    static VdfNode string(const QByteArray& key, const QString& v);
    static VdfNode int32(const QByteArray& key, qint32 v);
    QString text() const { return QString::fromUtf8(value); }
    qint32 toInt32() const;
    // Kind op sleutel (hoofdletterongevoelig); nullptr als hij ontbreekt.
    VdfNode* child(const QByteArray& key);
    const VdfNode* child(const QByteArray& key) const;
    // Zet (of voegt toe) een kind; bestaande sleutel behoudt zijn schrijfwijze en plaats.
    void set(const VdfNode& n);
};

namespace vdf {
// Leest een binair VDF-bestand in `root` (een Map zonder sleutel). false + err (Engels) bij een kapot of onbekend bestand.
bool parseBinary(const QByteArray& data, VdfNode* root, QString* err = nullptr);
QByteArray writeBinary(const VdfNode& root);
}  // namespace vdf

struct SteamUser {
    QString accountId;   // map onder userdata/ (32-bits account-id)
    QString name;        // PersonaName uit config/loginusers.vdf (kan leeg zijn)
    bool mostRecent = false;
    QString configDir() const;  // <steam>/userdata/<id>/config (via SteamShortcuts::steamRoot)
};

struct ShortcutSpec {
    QString appName = QStringLiteral("RavenGun");
    QString exe;            // absoluut pad (zonder aanhalingstekens; die zet de schrijver)
    QString startDir;       // map (zonder aanhalingstekens)
    QString icon;           // pad naar een PNG of ICO (leeg = geen)
    QString launchOptions = QStringLiteral("--fullscreen");
};

struct ShortcutResult {
    bool ok = false;
    bool updated = false;   // een bestaand item is bijgewerkt (anders toegevoegd)
    quint32 appId = 0;      // unsigned, naam van de grid-afbeeldingen
    QString backup;         // pad van de back-up (leeg als er nog geen bestand was)
    QString error;          // Engels
    QStringList images;     // geschreven grid-afbeeldingen
};

class SteamShortcuts {
public:
    // $RAVENGUN_STEAM_ROOT, anders ~/.local/share/Steam, anders ~/.steam/steam; leeg als geen van beide bestaat.
    static QString steamRoot();
    // Gebruikers met een userdata-map (numeriek, niet 0); de laatst ingelogde (loginusers.vdf, MostRecent) eerst.
    static QList<SteamUser> users(const QString& root = SteamShortcuts::steamRoot());
    // Draait Steam (een proces met comm "steam")? Leest alleen /proc.
    static bool steamRunning(const QString& procDir = QStringLiteral("/proc"));
    static quint32 crc32(const QByteArray& data);
    // crc32(exe-met-aanhalingstekens + appname) | 0x80000000.
    static quint32 shortcutId(const QString& exe, const QString& appName);
    static QString quoted(const QString& path);

    // Voegt de snelkoppeling toe of werkt een bestaande bij (zelfde appname of zelfde exe; een snelkoppeling per game met
    // LaunchOptions "--play PROFIEL" alleen bij zelfde exe en precies die opties, en RavenGun zelf nooit bij een item met --play) in
    // `vdfPath`. Eerst een back-up
    // (<pad>.ravengun-bak-JJJJMMDD-HHMMSS), dan atomisch schrijven. Andere items blijven byte voor byte gelijk.
    static ShortcutResult addOrUpdate(const QString& vdfPath, const ShortcutSpec& spec);
    // Schrijft de grid-afbeeldingen (config/grid/<id>...) uit de gegeven plaatjes; bestaande bestanden krijgen een back-up.
    static QStringList writeGrid(const QString& gridDir, quint32 appId, const QImage& portrait, const QImage& landscape,
                                 const QImage& hero, const QImage& logo, const QImage& icon, QString* err = nullptr);
    // Leest de snelkoppelingen terug (voor de controle en tests): appname, exe, LaunchOptions en appid per item.
    struct Entry {
        QString appName, exe, startDir, icon, launchOptions;
        quint32 appId = 0;
    };
    static QList<Entry> list(const QString& vdfPath, QString* err = nullptr);
};

// Afbeeldingen voor Steam en het app-icoon, getekend (zwart met een rood vizier; geen bestanden van derden).
namespace artwork {
QImage portrait();   // 600x900
QImage landscape();  // 920x430
QImage hero();       // 1920x620
QImage logo();       // 800x260, doorzichtig
QImage icon(int size = 256);
}  // namespace artwork

}  // namespace rg
