// SPDX-License-Identifier: GPL-3.0-or-later
// De "systeemgegevens" (amSysData) die de AM Library van de Sega Ring-games in het backup-SRAM bewaart, en dan vooral AM_SYSDATA_STATIC: daar staat de regio van
// de KAST in (naast het serienummer). Gelezen in LGS (GameSampR_RingWide.exe, amSysData Ver.1.05):
//
//   0x41B880  GetInfo: leest AM_SYSDATA_STATIC in ctx+0x68 via amBackupRecordReadDup (0x463AF0): eerst de eerste kopie, bij een CRC-fout de tweede.
//             "GetInfo: AM_SYSDATA_STATIC broken.(code=%d, use default)" -> standaardwaarden (0x470C00): alles 0, serienummer "INVALID_SERIALNO".
//   0x415440  GetRegion() = byte ctx+0x68+0xC = byte 0xC van dat record.
//   0x414A10  controle bij de start: de regio moet 2 (USA), 4 (Export) of 8 (China) zijn, anders fout 0x387 = "Error 0903 Wrong Region". Een leeg
//             SRAM geeft dus 0 en dus 0903, ook als de keychip (keychip.appboot.region) een goede regio meldt: de twee zijn onafhankelijk.
//
// Opslag in het SRAM: record 0 (descriptor 0x52F51C): 0x20 bytes op SRAM-offset 0 en een tweede kopie op 0x200. Indeling van een record:
//   [0..3]   CRC32 (IEEE, beginwaarde 0, zoals zlib.crc32) over bytes [4..0x20)         (amBackupRecordCheckValid 0x463710, tabel 0x475DD0)
//   [4..0x1F] 0x1C bytes: byte 0xC = regio, byte 0xD = 0, vanaf 0xF het serienummer (17 bytes, "INVALID_SERIALNO" + NUL)
// Alleen standaardbibliotheek; gebruikt door rg_amhw.cpp en de tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rg_amhw {

constexpr size_t kStaticRecordSize = 0x20;
constexpr size_t kStaticOffsets[2] = {0x0, 0x200};

inline uint32_t crc32_ieee(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

inline bool static_record_valid(const uint8_t* r) {
    uint32_t stored;
    std::memcpy(&stored, r, 4);
    return stored == crc32_ieee(r + 4, kStaticRecordSize - 4);
}

inline void make_static_record(uint8_t* out, uint8_t region, const char* serial = "INVALID_SERIALNO") {
    std::memset(out, 0, kStaticRecordSize);
    out[0xC] = region;
    const size_t sl = std::strlen(serial) < 16 ? std::strlen(serial) : 16;
    std::memcpy(out + 0xF, serial, sl);
    const uint32_t c = crc32_ieee(out + 4, kStaticRecordSize - 4);
    std::memcpy(out, &c, 4);
}

// Zet in een (leeg of kapot) SRAM een geldig AM_SYSDATA_STATIC met deze regio. Raakt een record met een geldige CRC en een regio die al 2/4/8/1 is
// niet aan (de eigenaar kan de regio in het testmenu van de game hebben gezet). Geeft terug of er iets veranderd is.
inline bool seed_static_sysdata(std::vector<uint8_t>& sram, uint8_t region) {
    if (region == 0 || sram.size() < 0x200 + kStaticRecordSize) return false;
    bool good = false;
    for (size_t off : kStaticOffsets)
        if (static_record_valid(&sram[off]) && sram[off + 0xC] != 0) good = true;
    if (good) return false;
    uint8_t rec[kStaticRecordSize];
    make_static_record(rec, region);
    for (size_t off : kStaticOffsets) std::memcpy(&sram[off], rec, sizeof rec);
    return true;
}

}  // namespace rg_amhw
