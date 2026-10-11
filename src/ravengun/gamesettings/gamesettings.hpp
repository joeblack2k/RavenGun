// SPDX-License-Identifier: GPL-3.0-or-later
// Instellingen en patches per game (docs/per-game-patches.md): patches/<Game>/game.ini en patches.ini, met drie lagen die per sleutel
// worden samengevoegd (een latere laag wint, een lege waarde haalt de sleutel weg):
//   <meegeleverd>/_defaults/<Type>.ini   standaard per soort emulator
//   <meegeleverd>/<Game>/game.ini        meegeleverd (repo: patches/, installatie: <datadir>/patches/)
//   <gebruiker>/<Game>/game.ini          $XDG_DATA_HOME/ravengun/patches (standaard ~/.local/share/ravengun/patches)
// Patches (patches.ini, secties "[Patch: <naam>]") volgen dezelfde lagen, met onderaan de gedeelde definities in
// <meegeleverd>/_defaults/<Type>.patches.ini.
//
// Alleen de standaardbibliotheek, zodat engine en app (Qt) hetzelfde lezen. Het INI-formaat is gelijk aan dat van
// scripts/gamesettings.py (Python, configparser): sectienamen en sleutels hoofdlettergevoelig; commentaar op een regel die met # of ;
// begint, of een ';' na witruimte achter een waarde; geen waarden over meerdere regels; voorloop-witruimte wordt genegeerd.
// tests/data/gamesettings-cases.ini wordt door beide gelezen en vergeleken.
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ravengun::gamesettings {

struct Section {
    std::string name;
    std::vector<std::pair<std::string, std::string>> kv;   // in volgorde van voorkomen
};

class Ini {
public:
    std::vector<Section> sections;
    const Section* section(const std::string& name) const;
    const std::string* find(const std::string& sec, const std::string& key) const;   // nullptr als hij ontbreekt
    std::string get(const std::string& sec, const std::string& key, const std::string& def = {}) const;
    bool has(const std::string& sec, const std::string& key) const { return find(sec, key) != nullptr; }
    void set(const std::string& sec, const std::string& key, const std::string& value);
    void erase(const std::string& sec, const std::string& key);
};

// Leest INI-tekst. Regels die niet te begrijpen zijn (sleutel zonder '=', sleutel buiten een sectie) vervallen met een waarschuwing.
Ini parse_ini(const std::string& text, std::vector<std::string>* warnings = nullptr);
// Voegt `over` per sleutel in `base` samen; een lege waarde haalt de sleutel weg.
void merge_into(Ini& base, const Ini& over);

bool truthy(const std::string& v);   // true, 1, yes, on (hoofdletters maken niet uit)

struct Patch {
    std::string name;
    std::vector<std::pair<std::string, std::string>> fields;
    std::string get(const std::string& key, const std::string& def = {}) const;
    bool enabled() const { return truthy(get("Enabled", "false")); }
    std::string kind() const;     // file | registry | memory (kleine letters), leeg als niet gezet
    std::string reason() const { return get("Reason"); }
};

struct Dirs {
    std::string shipped;   // <datamap>/patches
    std::string user;      // gebruikerslaag; leeg = geen
};

// $RAVENGUN_PATCHES_DIR of <data_dir>/patches; $RAVENGUN_USER_PATCHES_DIR of $XDG_DATA_HOME/ravengun/patches (anders
// $HOME/.local/share/ravengun/patches).
Dirs default_dirs(const std::string& data_dir);

struct Game {
    std::string profile;
    std::string type;                   // [Game] Type (de gebruikerslaag gaat voor), anders de hint
    Ini settings;                       // samengevoegd (standaard + meegeleverd + gebruiker)
    std::vector<Patch> patches;         // in volgorde van voorkomen
    std::vector<std::string> sources;   // gelezen bestanden
    std::vector<std::string> warnings;
    bool exists = false;                // er is een game.ini in een van beide lagen
    std::string get(const std::string& sec, const std::string& key, const std::string& def = {}) const { return settings.get(sec, key, def); }
    const Patch* patch(const std::string& name) const;
};

Game load_game(const Dirs& dirs, const std::string& profile, const std::string& type_hint = {});
// Namen van de games met een map in een van beide lagen (zonder _-mappen), gesorteerd zonder hoofdletters.
std::vector<std::string> list_games(const Dirs& dirs);

// Tekst van een INI-bestand met één sleutel gezet (value) of weggehaald (erase); de rest (commentaar, volgorde) blijft staan.
std::string edit_ini_text(const std::string& text, const std::string& section, const std::string& key, const std::string& value, bool erase);

// Gebruikerslaag schrijven (de app). Maken de map aan; geven false met een Engelse melding in err bij een fout.
bool set_user_value(const Dirs& dirs, const std::string& profile, const std::string& section, const std::string& key,
                    const std::string& value, std::string* err = nullptr);
bool erase_user_value(const Dirs& dirs, const std::string& profile, const std::string& section, const std::string& key,
                      std::string* err = nullptr);
// Zet Enabled van een patch in het gebruikers-patches.ini (sectie "Patch: <naam>").
bool set_user_patch_enabled(const Dirs& dirs, const std::string& profile, const std::string& patch, bool enabled, std::string* err = nullptr);
// Haalt de keuze van de gebruiker voor een patch weg (de meegeleverde stand geldt weer).
bool erase_user_patch_enabled(const Dirs& dirs, const std::string& profile, const std::string& patch, std::string* err = nullptr);

// Paden van de bestanden van een game in een laag (voor "Open map" en weergave).
std::string game_dir(const std::string& layer_dir, const std::string& profile);

}  // namespace ravengun::gamesettings
