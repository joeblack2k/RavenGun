// SPDX-License-Identifier: GPL-3.0-or-later
#include "ravengun/launch/ttc.hpp"

#include <cstdint>

namespace ravengun::launch {

namespace {

uint32_t be32(const std::string& d, size_t o) {
    return (uint32_t(uint8_t(d[o])) << 24) | (uint32_t(uint8_t(d[o + 1])) << 16) | (uint32_t(uint8_t(d[o + 2])) << 8) |
           uint32_t(uint8_t(d[o + 3]));
}
uint16_t be16(const std::string& d, size_t o) { return uint16_t((uint8_t(d[o]) << 8) | uint8_t(d[o + 1])); }
void put32(std::string& d, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) d.push_back(char((v >> s) & 0xFF));
}

}  // namespace

bool is_ttc(const std::string& data) { return data.size() >= 4 && data.compare(0, 4, "ttcf") == 0; }

bool ttc_face(const std::string& ttc, unsigned face, std::string& out, std::string& err) {
    out.clear();
    if (!is_ttc(ttc)) { err = "geen TrueType-collectie (de kop is geen 'ttcf')"; return false; }
    if (ttc.size() < 12) { err = "collectie te kort voor de kop"; return false; }
    const uint32_t n = be32(ttc, 8);
    if (face >= n) { err = "face " + std::to_string(face) + " bestaat niet (de collectie heeft er " + std::to_string(n) + ")"; return false; }
    if (ttc.size() < 12 + 4 * size_t(n)) { err = "collectie te kort voor de lijst met faces"; return false; }
    const size_t dir = be32(ttc, 12 + 4 * size_t(face));
    if (dir + 12 > ttc.size()) { err = "tabelmap van face " + std::to_string(face) + " ligt buiten het bestand"; return false; }
    const uint16_t tables = be16(ttc, dir + 4);
    const size_t recs = dir + 12;
    if (recs + 16 * size_t(tables) > ttc.size()) { err = "tabelrecords van face " + std::to_string(face) + " lopen buiten het bestand"; return false; }
    // Kop van de losse font: sfntVersion, numTables, searchRange, entrySelector, rangeShift zoals in de collectie.
    out.assign(ttc, dir, 12);
    size_t pos = 12 + 16 * size_t(tables);
    std::string body;
    for (uint16_t i = 0; i < tables; ++i) {
        const size_t r = recs + 16 * size_t(i);
        const uint32_t off = be32(ttc, r + 8), len = be32(ttc, r + 12);
        if (size_t(off) + len > ttc.size()) { err = "tabel " + ttc.substr(r, 4) + " ligt buiten het bestand"; return false; }
        out.append(ttc, r, 8);   // tag en checksum
        put32(out, uint32_t(pos + body.size()));
        put32(out, len);
        body.append(ttc, off, len);
        body.append((4 - body.size() % 4) % 4, '\0');
    }
    out += body;
    return true;
}

}  // namespace ravengun::launch
