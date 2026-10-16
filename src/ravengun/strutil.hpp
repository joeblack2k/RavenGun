#pragma once
// Kleine tekstgereedschappen (alleen ASCII, locale-onafhankelijk gedrag van <cctype> met unsigned char).
#include <cctype>
#include <string>
#include <string_view>

namespace ravengun {

// ASCII-kleine letters.
inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Knipt witruimte (std::isspace) van beide kanten af.
inline std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

}  // namespace ravengun
