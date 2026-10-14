#pragma once
// De view van een game (docs/per-game-patches.md, "De view"): een lokale map ~/.local/share/ravengun/runtime/views/<Profiel> met
// symlinks naar de gamemap (meestal op de NAS), met echte lokale mappen waar de game of wij schrijven (de map van de exe en elke map
// erboven tot de wortel, BepInEx/, lege mappen uit [Launch] ViewEmptyDirs) en daarin ons teknoparrot.ini. Dezelfde opbouw als
// `scripts/tp-add-game.py <Spel> <exe> --view [--view-up N] [--empty-dir REL]...` (scripts/gameview.py is de Python-tweeling;
// tests/test_gameview.py vergelijkt beide bomen op een nep-NAS).
//
// Nooit op de NAS schrijven: elke stap (map maken, link zetten of weghalen, bestand kopieren) controleert eerst of de map waarin hij
// schrijft lokaal is; zo niet, dan stopt de opbouw met een melding. Na de opbouw controleert view_write_errors() onze schrijfpaden
// (zoals pruef_schrijfpaden in scripts/aagames-setup.py en nas_write_errors in unity.cpp).
#include <functional>
#include <string>
#include <vector>

namespace ravengun::launch {

// true = dit pad (gevolgd) ligt op de NAS; de directe route geeft on_nas() mee (~/mnt of een netwerkkoppeling).
using NasFn = std::function<bool(const std::string& path)>;

struct GameViewSpec {
    std::string source;                    // de gamemap die de view spiegelt (de exe staat op source/exe_rel)
    std::string view;                      // de view zelf: views/<Profiel>
    std::string exe_rel;                   // de exe ten opzichte van source (en van view)
    std::vector<std::string> empty_dirs;   // padonderdelen ten opzichte van source die een lege lokale map worden (--empty-dir)
    std::string elfldr2;                   // doel van de link ElfLdr2 in de view (leeg = geen; 32-bit ElfLdr2-games)
};

// Maakt de view zoals tp-add-game.py --view: links van de vorige keer op het hoogste niveau weg, lege mappen weg, alles linken
// (BepInEx/ lokaal gekopieerd, de lege mappen leeg), de keten naar de exe echte mappen, en ElfLdr2. false + err bij een fout.
bool make_game_view(const GameViewSpec& s, const NasFn& on_nas, std::vector<std::string>& log, std::string& err);

// Een bestaande view alleen verversen: in de wortel en in de mappen op weg naar de exe (echte mappen) een link voor wat er in de
// gamemap bij is gekomen, en links die naar een verdwenen bestand in de gamemap wijzen weg. Echte bestanden en mappen blijven staan
// (ook lege mappen en BepInEx/).
bool refresh_game_view(const GameViewSpec& s, const NasFn& on_nas, std::vector<std::string>& log, std::string& err);

// Onze schrijfpaden in de view die op de NAS uitkomen (leeg = goed): de view zelf, de mappen op weg naar de exe (daar komt
// teknoparrot.ini), BepInEx/ met alles erin en de lege mappen.
std::vector<std::string> view_write_errors(const GameViewSpec& s, const NasFn& on_nas);

// [Launch] View en ViewEmptyDirs. View: auto (leeg; een view als de game op de NAS staat, met de wortel waar [Identify] Path
// begint), off (de oude route: rechtstreeks van de NAS), of een getal N (altijd een view, de wortel N mappen boven die van de exe;
// --view-up N). ViewEmptyDirs: paden ten opzichte van de view-wortel, gescheiden door komma of puntkomma (--empty-dir).
struct ViewSetting {
    enum class Mode { Auto, Off, Levels } mode = Mode::Auto;
    int up = 0;
    std::vector<std::string> empty_dirs;
};
bool parse_view_setting(const std::string& view, const std::string& empty_dirs, ViewSetting& out, std::string& err);

}  // namespace ravengun::launch
