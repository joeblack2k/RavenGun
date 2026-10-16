#pragma once
// Zaaigoed voor de Wine-routes: per-game bestanden die de game in zijn eigen map verwacht (patches/<Game>/seed/, met dezelfde
// mappen als de spelmap), zoals de geweerkalibratie van Gaia Attack 4 (OpenParrot/GA40.CFG). Bij elke start komt elk bestand in
// de schrijflaag, tenzij het daar al staat: wat de game zelf schrijft (een nieuwe kalibratie in het testmenu) gaat altijd voor.
// Een versie op de NAS telt niet mee; die blijft onaangeroerd en het zaaigoed ligt erover (anders dan `seed` van native.json,
// dat een bestand op de NAS laat winnen). De gebruikerslaag wint per bestand van de meegeleverde laag.
#include <string>
#include <vector>

namespace ravengun::games {

// Een spelmap van de schrijflaag: de echte map (alleen lezen in de unit) en de lokale bovenlaag.
struct LayerMap {
    std::string root, upper;
};

// Kopieert elk bestand onder `seed_dirs` (de eerste map wint per relatief pad) naar `game_dir`/<pad>. Ligt dat doel (de links
// van een view gevolgd) in een spelmap van `layers`, dan gaat de kopie naar de bovenlaag van die map; anders (een lokale map,
// bv. in de view) rechtstreeks. Een doel dat al bestaat blijft staan. Geeft een logregel per bestand; `err` niet leeg = een
// kopie mislukte (de start gaat dan niet door).
std::vector<std::string> seed_files(const std::vector<std::string>& seed_dirs, const std::string& game_dir,
                                    const std::vector<LayerMap>& layers, std::string& err);

}  // namespace ravengun::games
