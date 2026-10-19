#pragma once
// De view van een game (docs/per-game-patches.md, "De view"): een lokale map ~/.local/share/ravengun/runtime/views/<Profiel> met
// symlinks naar de gamemap (meestal op de NAS), met echte lokale mappen waar de game of wij schrijven (de map van de exe en elke map
// erboven tot de wortel, BepInEx/, lege mappen uit [Launch] ViewEmptyDirs, echte mappen met links uit [Launch] ViewRealDirs) en
// daarin ons teknoparrot.ini. Dezelfde opbouw als
// `scripts/tp-add-game.py <Spel> <exe> --view [--view-up N] [--empty-dir REL]... [--real-dir REL]...` (scripts/gameview.py is de Python-tweeling;
// tests/test_gameview.py vergelijkt beide bomen op een nep-NAS).
//
// Nooit op de NAS schrijven: elke stap (map maken, link zetten of weghalen, bestand kopieren) controleert eerst of de map waarin hij
// schrijft lokaal is; zo niet, dan stopt de opbouw met een melding. Na de opbouw controleert view_write_errors() onze schrijfpaden
// (zoals pruef_schrijfpaden in tools/dev/aagames-setup.py en nas_write_errors in unity.cpp).
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
    // Paden ten opzichte van source die een ECHTE lokale map met links naar de inhoud worden, net als de map van de exe (--real-dir).
    // Proton meldt een gelinkte map als map plus reparse point (GetFileAttributes 0x410, gemeten 2026-10-16); een game die op precies
    // FILE_ATTRIBUTE_DIRECTORY (0x10) controleert, mist hem dan (Haunted Museum II: ".\data" -> chdir("..\"), SOUND ERROR).
    std::vector<std::string> real_dirs;
    // [Launch] ViewWritable: paden ten opzichte van de view-wortel waar de game zelf in schrijft (zonder hoofdletters vergeleken).
    // Opbouw en verversing zetten NOOIT een link IN zo'n map (review 10, A6): een link daarin laat een schrijvende game alsnog op de
    // NAS uitkomen. Een echte schrijfmap blijft altijd staan, ook leeg (dat is de stand van de game); een pad uit real_dirs houdt op
    // bij de map erboven. Ontbreekt de schrijfmap, dan een link naar de gamemap als die hem heeft (zoals in een nieuwe view; de
    // directe route maakt er bij de start een echte map met een kopie van, place_view_writable), anders een lege echte map.
    std::vector<std::string> writable_dirs;
};

// Maakt de view zoals tp-add-game.py --view: links van de vorige keer op het hoogste niveau weg, lege mappen weg, alles linken
// (BepInEx/ lokaal gekopieerd, de lege mappen leeg), de keten naar de exe echte mappen, en ElfLdr2. false + err bij een fout.
bool make_game_view(const GameViewSpec& s, const NasFn& on_nas, std::vector<std::string>& log, std::string& err);

// Een bestaande view alleen verversen: in de wortel, in de mappen op weg naar de exe en in de echte mappen van real_dirs een link
// voor wat er in de gamemap bij is gekomen, en links die naar een verdwenen bestand in de gamemap wijzen weg. Een pad uit real_dirs
// dat nog een link is, wordt een echte map. Echte bestanden en mappen blijven staan (ook lege mappen en BepInEx/).
bool refresh_game_view(const GameViewSpec& s, const NasFn& on_nas, std::vector<std::string>& log, std::string& err);

// Onze schrijfpaden in de view die op de NAS uitkomen (leeg = goed): de view zelf, de mappen op weg naar de exe (daar komt
// teknoparrot.ini), BepInEx/ met alles erin en de lege mappen.
std::vector<std::string> view_write_errors(const GameViewSpec& s, const NasFn& on_nas);

// [Launch] View en ViewEmptyDirs. View: auto (leeg; een view als de game op de NAS staat, met de wortel waar [Identify] Path
// begint), off (de oude route: rechtstreeks van de NAS), of een getal N (altijd een view, de wortel N mappen boven die van de exe;
// --view-up N). ViewEmptyDirs: paden ten opzichte van de view-wortel, gescheiden door komma of puntkomma (--empty-dir).
// ViewRealDirs: idem, voor de echte mappen met links (--real-dir).
struct ViewSetting {
    enum class Mode { Auto, Off, Levels } mode = Mode::Auto;
    int up = 0;
    std::vector<std::string> empty_dirs, real_dirs;
};
bool parse_view_setting(const std::string& view, const std::string& empty_dirs, ViewSetting& out, std::string& err,
                        const std::string& real_dirs = {});

}  // namespace ravengun::launch
