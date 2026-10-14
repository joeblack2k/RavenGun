// De stapel van de hoofdthread in de PE-kop (zie pe_stack.hpp).
#include "ravengun/launch/pe_stack.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace ravengun::launch {

namespace {

uint32_t rd32(const std::string& d, size_t at) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(d[at + static_cast<size_t>(i)]);
    return v;
}
uint16_t rd16(const std::string& d, size_t at) {
    return static_cast<uint16_t>(static_cast<unsigned char>(d[at]) | (static_cast<unsigned char>(d[at + 1]) << 8));
}

// Plaats en breedte (4 of 8 bytes) van SizeOfStackReserve. In beide formaten staat het veld op 72 bytes in de optionele kop
// (PE32: SizeOfHeaders 60, CheckSum 64, Subsystem 68, DllCharacteristics 70, SizeOfStackReserve 72; PE32+ idem, maar 8 bytes breed).
bool locate(const std::string& d, size_t& at, size_t& width, std::string& err) {
    if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') { err = "geen MZ-kop"; return false; }
    const uint32_t pe = rd32(d, 0x3c);
    if (static_cast<uint64_t>(pe) + 24 + 2 > d.size() || std::memcmp(d.data() + pe, "PE\0\0", 4) != 0) { err = "geen PE-handtekening"; return false; }
    const uint16_t opt_size = rd16(d, pe + 20);
    const size_t opt = static_cast<size_t>(pe) + 24;
    const uint16_t magic = rd16(d, opt);
    if (magic == 0x10b) width = 4;
    else if (magic == 0x20b) width = 8;
    else {
        char b[48];
        std::snprintf(b, sizeof b, "onbekende optionele kop (magic 0x%x)", magic);
        err = b;
        return false;
    }
    at = opt + 72;
    if (opt_size < 72 + width || at + width > d.size()) { err = "optionele kop te kort"; return false; }
    return true;
}

}  // namespace

bool parse_stack_size(const std::string& text, uint64_t& out) {
    std::string t;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) t += c;
    if (t.empty()) return false;
    uint64_t v = 0;
    size_t i = 0;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        i = 2;
        if (i >= t.size()) return false;
        for (; i < t.size(); ++i) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
            int dgt;
            if (c >= '0' && c <= '9') dgt = c - '0';
            else if (c >= 'a' && c <= 'f') dgt = c - 'a' + 10;
            else return false;
            if (v > (UINT64_MAX >> 4)) return false;
            v = (v << 4) | static_cast<uint64_t>(dgt);
        }
        out = v;
        return true;
    }
    for (; i < t.size() && std::isdigit(static_cast<unsigned char>(t[i])); ++i) {
        if (v > (UINT64_MAX - 9) / 10) return false;
        v = v * 10 + static_cast<uint64_t>(t[i] - '0');
    }
    if (i == 0) return false;
    std::string unit;
    for (; i < t.size(); ++i) unit += static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
    uint64_t mul = 1;
    if (unit.empty() || unit == "b") mul = 1;
    else if (unit == "k" || unit == "kb" || unit == "kib") mul = 1024;
    else if (unit == "m" || unit == "mb" || unit == "mib") mul = 1024ull * 1024;
    else return false;
    if (v > UINT64_MAX / mul) return false;
    out = v * mul;
    return true;
}

std::string stack_size_text(uint64_t bytes) {
    const std::string n = std::to_string(bytes);
    if (bytes != 0 && bytes % (1024ull * 1024) == 0) return std::to_string(bytes / (1024ull * 1024)) + " MiB (" + n + ")";
    if (bytes != 0 && bytes % 1024 == 0) return std::to_string(bytes / 1024) + " KiB (" + n + ")";
    return n;
}

bool pe_stack_reserve(const std::string& data, uint64_t& reserve, std::string& err) {
    size_t at = 0, width = 0;
    if (!locate(data, at, width, err)) return false;
    reserve = rd32(data, at);
    if (width == 8) reserve |= static_cast<uint64_t>(rd32(data, at + 4)) << 32;
    return true;
}

namespace {
constexpr uint32_t kPoly = 0xEDB88320u;   // CRC32 (zlib, OpenParrot GetCRC32), bitomgekeerd
uint32_t crc_step_byte(uint32_t reg, unsigned char b) {
    reg ^= b;
    for (int i = 0; i < 8; ++i) reg = (reg & 1) ? (reg >> 1) ^ kPoly : reg >> 1;
    return reg;
}
// Eén bitstap terug: de vooruitstap schuift naar rechts en xort kPoly als het onderste bit 1 was; kPoly heeft het bovenste bit, dus
// het bovenste bit van het resultaat zegt welke tak het was.
uint32_t crc_unstep_bit(uint32_t reg) {
    return (reg & 0x80000000u) ? ((reg ^ kPoly) << 1) | 1u : reg << 1;
}
}  // namespace

uint32_t crc32_bytes(const std::string& data, size_t length) {
    uint32_t reg = 0xFFFFFFFFu;
    for (size_t i = 0; i < length && i < data.size(); ++i) reg = crc_step_byte(reg, static_cast<unsigned char>(data[i]));
    return reg ^ 0xFFFFFFFFu;
}

bool pe_keep_header_crc(std::string& data, uint32_t crc, std::string& err) {
    if (data.size() < kHeaderCrcLength) { err = "korter dan 0x400 bytes"; return false; }
    if (data.size() < 0x40 || rd32(data, 0x3c) < kHeaderCrcPatchAt + 4) { err = "geen ruimte in de DOS-stub"; return false; }
    const size_t at = kHeaderCrcPatchAt;
    // Het register voor de vier bytes (vooruit vanaf het begin) en het register dat er na moet staan (terug vanaf het einde).
    uint32_t voor = 0xFFFFFFFFu;
    for (size_t i = 0; i < at; ++i) voor = crc_step_byte(voor, static_cast<unsigned char>(data[i]));
    uint32_t na = crc ^ 0xFFFFFFFFu;
    for (size_t i = kHeaderCrcLength; i-- > at + 4;) {
        for (int k = 0; k < 8; ++k) na = crc_unstep_bit(na);
        na ^= static_cast<unsigned char>(data[i]);
    }
    // Vier bytes vooruit is: reg ^= B (32 bits, little-endian), dan 32 bitstappen. Terug: 32 bitstappen terug, dan ^ voor.
    for (int k = 0; k < 32; ++k) na = crc_unstep_bit(na);
    const uint32_t b = na ^ voor;
    for (int i = 0; i < 4; ++i) data[at + static_cast<size_t>(i)] = static_cast<char>((b >> (8 * i)) & 0xff);
    if (crc32_bytes(data, kHeaderCrcLength) != crc) { err = "CRC32 van de kop niet te herstellen"; return false; }
    return true;
}

bool pe_set_stack_reserve(std::string& data, uint64_t reserve, std::string& err) {
    size_t at = 0, width = 0;
    if (!locate(data, at, width, err)) return false;
    if (width == 4 && reserve > 0xffffffffull) { err = "past niet in een PE32-kop"; return false; }
    for (size_t i = 0; i < width; ++i) data[at + i] = static_cast<char>((reserve >> (8 * i)) & 0xff);
    return true;
}

}  // namespace ravengun::launch
