#pragma once
// De map van een game in de spelmappen van de app (library.json). Gedeeld door de native route (@nas in native.json), de Unity-route
// (unity.json "folder") en de app (de bibliotheek), zodat ze dezelfde map vinden.
#include <string>
#include <vector>

namespace ravengun::launch {

// Eerst <root>/<folder>, dan een niveau dieper (<root>/*/<folder>, op naam gesorteerd; een spelmap die de map Teknoparrot zelf
// bevat). De naam moet exact kloppen (ook een spatie aan het eind). Leeg = nergens (of een naam met '/').
std::string find_game_folder(const std::string& folder, const std::vector<std::string>& roots);

}  // namespace ravengun::launch
