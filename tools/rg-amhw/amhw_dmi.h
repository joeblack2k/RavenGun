// SPDX-License-Identifier: GPL-3.0-or-later
// De nagebootste SMBIOS/DMI-tabel van een Sega Ring-kast, zonder Windows-koppen (te testen op Linux; tests/test_amhw.cpp).
//
// Waarom: de AM Library in de exe van de Ring-games leest bij de start het bordtype uit de DMI-tabel van het BIOS, via het stuurprogramma
// `\\.\columba` en DeviceIoControl 0x9C406104 (een lees-fysiek-geheugen-opdracht). Onder Wine bestaat dat apparaat niet; amPlatformGetBoardType
// geeft dan een bordtype dat niet in de tabel staat, de game krijgt een nulwijzer en stort in (LGI: 0x006F0515, 2026-10-17).
// Gelezen uit amOemstringGetOemstring (LGI 0x48F6E0), amPlatformGetPlatformId (0x489010) en amPlatformGetBoardType (0x488DF0):
//   1. DeviceIoControl(h, 0x9C406104, in = {adres 0xF0000, 0, 1, 0x10000}, uit = 0x10000 bytes): de BIOS-ruimte. De game zoekt daarin de
//      tekst "_DMI_" (hoogstens op index 0xFFF7), leest de tabellengte (2 bytes op +6; de 2 bytes erna horen bij het adres) en het
//      tabeladres (4 bytes op +8). Is het adres 0, dan faalt hij.
//   2. Dezelfde opdracht met in = {tabeladres, 0, 1, lengte}: weer EXACT 0x10000 bytes terug (anders faalt hij), met de tabel vooraan.
//   3. De structuren worden doorlopen (type, lengte, dan de tekenreeksen tot een dubbele nul; stoppen bij lengte 0 of na tabellengte):
//        type 0  (BIOS)       tekenreeks 2 (index 1) = BIOS-versie, 3 = datum (hoogstens 10 tekens)
//        type 1  (Systeem)    tekenreeks 1 = FABRIKANT  -> "NEC", "Supermicro" of "Advantech"
//        type 11 (OEM)        tekenreeks 3 = PLATFORM-ID -> "AAL", "AAL2" (RingEdge 2) of "AAM"
//   4. Bordtype: AAL + NEC = 0; AAM + Supermicro = 1; AAM + Advantech = 2; AAL2 = 3 (alleen in de nieuwere AM Library van LGI3D en
//      Transformers). Een andere combinatie geeft een type dat de tabel van de game niet kent (-> nulwijzer). amPlatformGetPlatformId
//      neemt tekenreeks 3 zelf als hij AAL of AAM is, anders leidt hij hem af uit de fabrikant (NEC = AAL, Supermicro/Advantech = AAM).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rg_amhw {

constexpr uint32_t kBiosBase = 0xF0000;       // waar de game de ingangsstructuur zoekt
constexpr uint32_t kTableBase = 0xE0000;      // waar wij de tabel zetten (de onderste 16 bits zijn 0: de game leest ze als deel van de lengte)
constexpr size_t kWindow = 0x10000;           // elke leesopdracht geeft precies zoveel bytes

struct Platform {
    std::string oem = "AAM";            // OEM-tekenreeks 3: het platform-id
    std::string vendor = "Supermicro";  // systeemfabrikant
    std::string bios_version = "6.00";
    std::string bios_date = "01/01/2012";   // hoogstens 10 tekens (de game bewaart er 11 inclusief de nul)
};

namespace detail {
inline void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = x & 0xFF; v[at + 1] = x >> 8; }
inline void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + i] = (x >> (8 * i)) & 0xFF; }
inline void add_strings(std::vector<uint8_t>& t, std::initializer_list<std::string> ss) {
    for (const auto& s : ss) { t.insert(t.end(), s.begin(), s.end()); t.push_back(0); }
    if (ss.size() == 0) t.push_back(0);
    t.push_back(0);   // de afsluitende nul
}
inline uint8_t checksum(const std::vector<uint8_t>& v, size_t from, size_t len) {
    uint8_t s = 0;
    for (size_t i = from; i < from + len; ++i) s = static_cast<uint8_t>(s + v[i]);
    return static_cast<uint8_t>(0x100 - s);
}
}  // namespace detail

// De structuren van de tabel (SMBIOS 2.4): BIOS, systeem, OEM-tekenreeksen, einde.
inline std::vector<uint8_t> table(const Platform& p, uint16_t* count = nullptr) {
    std::vector<uint8_t> t;
    auto start = [&](uint8_t type, uint8_t len, uint16_t handle) {
        const size_t at = t.size();
        t.resize(at + len, 0);
        t[at] = type; t[at + 1] = len;
        detail::put16(t, at + 2, handle);
        return at;
    };
    size_t a = start(0, 0x18, 0);
    t[a + 4] = 1; t[a + 5] = 2; detail::put16(t, a + 6, 0xE000); t[a + 8] = 3; t[a + 9] = 0x0F;
    t[a + 0x14] = 1; t[a + 0x16] = 0xFF; t[a + 0x17] = 0xFF;
    detail::add_strings(t, {"Phoenix Technologies", p.bios_version, p.bios_date});
    a = start(1, 0x1B, 1);
    t[a + 4] = 1; t[a + 5] = 2; t[a + 6] = 3; t[a + 7] = 4; t[a + 0x18] = 6;
    detail::add_strings(t, {p.vendor, "RingSystem", "1.0", "0000000000"});
    a = start(11, 5, 2);
    t[a + 4] = 3;
    detail::add_strings(t, {"SEGA", "AM", p.oem});
    start(127, 4, 3);
    detail::add_strings(t, {});
    if (count) *count = 4;
    return t;
}

// De ingangsstructuur (legacy SMBIOS 2.x: "_SM_" en op +0x10 "_DMI_"), 31 bytes.
inline std::vector<uint8_t> entry_point(uint16_t table_len, uint32_t table_addr, uint16_t count) {
    std::vector<uint8_t> e(0x1F, 0);
    std::memcpy(&e[0], "_SM_", 4);
    e[5] = 0x1F; e[6] = 2; e[7] = 4;
    detail::put16(e, 8, 0x30);
    std::memcpy(&e[0x10], "_DMI_", 5);
    detail::put16(e, 0x16, table_len);
    detail::put32(e, 0x18, table_addr);
    detail::put16(e, 0x1C, count);
    e[0x1E] = 0x24;
    e[0x15] = detail::checksum(e, 0x10, 0x0F);
    e[4] = detail::checksum(e, 0, 0x1F);
    return e;
}

// Het fysieke geheugen zoals het stuurprogramma het zou geven: n bytes vanaf `addr`, nullen waar niets staat.
inline void phys_read(const Platform& p, uint32_t addr, uint8_t* out, size_t n) {
    std::memset(out, 0, n);
    uint16_t count = 0;
    const std::vector<uint8_t> tab = table(p, &count);
    const std::vector<uint8_t> eps = entry_point(static_cast<uint16_t>(tab.size()), kTableBase, count);
    auto copy = [&](uint32_t base, const std::vector<uint8_t>& src) {
        const uint64_t lo = base, hi = base + src.size(), alo = addr, ahi = static_cast<uint64_t>(addr) + n;
        if (hi <= alo || lo >= ahi) return;
        const uint64_t from = lo > alo ? lo : alo, to = hi < ahi ? hi : ahi;
        std::memcpy(out + (from - alo), src.data() + (from - lo), static_cast<size_t>(to - from));
    };
    copy(kBiosBase, eps);
    copy(kTableBase, tab);
}

// Wat de game eruit haalt: een nauwkeurige nabootsing van amOemstringGetOemstring (LGI 0x48F6E0 en 0x48F660). `read` leest fysiek geheugen
// (addr, buffer van kWindow bytes), zoals de DeviceIoControl dat doet; false = de opdracht faalt.
struct Parsed {
    bool ok = false;
    std::string vendor, oem, bios_version, bios_date;
};
template <typename ReadFn>
inline Parsed parse_like_game(ReadFn read) {
    Parsed r;
    std::vector<uint8_t> buf(kWindow);
    if (!read(kBiosBase, buf.data())) return r;
    size_t i = 0;
    for (; i < 0xFFF8; ++i)
        if (std::memcmp(&buf[i], "_DMI_", 5) == 0) break;
    if (i >= 0xFFF8) return r;
    uint32_t limit = 0, addr = 0;
    std::memcpy(&limit, &buf[i + 6], 4);   // de game vergelijkt tegen deze hele dword (lengte + 2 bytes adres)
    std::memcpy(&addr, &buf[i + 8], 4);
    if (!addr) return r;
    if (!read(addr, buf.data())) return r;
    size_t pos = 0, edi = 0;
    auto cstr = [&](size_t at) { return std::string(reinterpret_cast<const char*>(&buf[at])); };
    while (pos + 1 < buf.size() && buf[pos + 1] != 0) {
        const uint8_t type = buf[pos];
        const size_t len = buf[pos + 1];
        edi += len; pos += len;
        if (edi >= limit) break;
        size_t idx = 0;
        do {   // elke structuur heeft minstens een (mogelijk lege) tekenreeks; de game doet het ook zo
            const std::string s = cstr(pos);
            if (type == 0 && idx == 1) r.bios_version = s;
            if (type == 0 && idx == 2) r.bios_date = s;
            if (type == 1 && idx == 0) r.vendor = s;
            if (type == 11 && idx == 2) r.oem = s;
            pos += s.size(); edi += s.size();
            ++pos; ++edi; ++idx;
        } while (pos < buf.size() && buf[pos] != 0);
        ++pos; ++edi;
    }
    r.ok = true;
    return r;
}

// Het bordtype dat amPlatformGetBoardType uit (platform, fabrikant) maakt. -1 = een combinatie die de game niet kent.
inline int board_type(const Parsed& p) {
    if (p.oem == "AAL2") return 3;
    if (p.oem == "AAL") return p.vendor == "NEC" ? 0 : 3;   // oudere AM Library: 3 is daar ook onbekend
    if (p.oem == "AAM") return p.vendor == "Supermicro" ? 1 : p.vendor == "Advantech" ? 2 : 3;
    return -1;
}

}  // namespace rg_amhw
