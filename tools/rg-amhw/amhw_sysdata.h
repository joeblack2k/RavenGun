// SPDX-License-Identifier: GPL-3.0-or-later
// De "systeemgegevens" (amSysData) die de AM Library van de Sega Ring-games in de EEPROM bewaart (gebied 0 en 1, 4 KB elk, lees/schrijf = amEepromRead/Write;
// het SRAM is gebied 2 en 3), en dan vooral AM_SYSDATA_STATIC: daar staat de regio van de KAST in (naast het serienummer). Gelezen in LGS
// (GameSampR_RingWide.exe, amSysData Ver.1.05; gebiedstabel 0x52FC30, lees-/schrijffuncties 0x463000/0x4631C0 = EEPROM, 0x4679C0/0x467BD0 = SRAM):
//
//   0x41B880  GetInfo: leest AM_SYSDATA_STATIC in ctx+0x68 via amBackupRecordReadDup (0x463AF0): eerst de eerste kopie, bij een CRC-fout de tweede.
//             "GetInfo: AM_SYSDATA_STATIC broken.(code=%d, use default)" -> standaardwaarden (0x470C00): alles 0, serienummer "INVALID_SERIALNO".
//   0x415440  GetRegion() = byte ctx+0x68+0xC = byte 0xC van dat record.
//   0x414A10  controle bij de start: de regio moet 2 (USA), 4 (Export) of 8 (China) zijn, anders fout 0x387 = "Error 0903 Wrong Region". Een leeg
//             SRAM geeft dus 0 en dus 0903, ook als de keychip (keychip.appboot.region) een goede regio meldt: de twee zijn onafhankelijk.
//
// Opslag in de EEPROM: record 0 (descriptor 0x52F51C, gebied 0): 0x20 bytes op EEPROM-offset 0 en een tweede kopie op 0x200. Indeling van een record:
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

// Zet in de EEPROM-afbeelding een geldig AM_SYSDATA_STATIC met deze regio, als hij leeg of kapot is of een ANDERE regio heeft. De regio is
// een optie in Properties (PcbRegion, via RG_AMHW_REGION) en die wint: eerst bleef een geldig record staan, en dan deed een andere keuze in
// het menu niets meer (SDR hield regio 01 uit een eerdere run: "ERROR 0903 WRONG REGION", gemeten 2026-10-18). Een record met dezelfde
// regio en een geldige CRC blijft ongemoeid (het serienummer erin blijft dan ook staan). Geeft terug of er iets veranderd is.
inline bool seed_static_sysdata(std::vector<uint8_t>& sram, uint8_t region) {   // `sram` = de afbeelding van de EEPROM (naam uit de eerste versie)
    if (region == 0 || sram.size() < 0x200 + kStaticRecordSize) return false;
    bool good = true;
    for (size_t off : kStaticOffsets)
        if (!static_record_valid(&sram[off]) || sram[off + 0xC] != region) good = false;
    if (good) return false;
    uint8_t rec[kStaticRecordSize];
    make_static_record(rec, region);
    for (size_t off : kStaticOffsets) std::memcpy(&sram[off], rec, sizeof rec);
    return true;
}

// Een leeg backup-record (amBackup, "BACKUP DATA CLEAR"): [CRC32 over de rest][size-4 nulbytes]. De game zelf schrijft dat voor zijn eigen records (LGS: SRAM op 0,
// 0x200 bytes, CRC 0x0FBB6D54), en alleen SDR heeft een record dat hij nooit zelf aanmaakt: record 7 (Game.exe 0x4DB9F5: SRAM 0x0 en kopie 0x1000, 0x200 bytes) wordt bij
// de invoerstart (0x4DB990) alleen GELEZEN; is het ongeldig dan stopt de start met fout 0x15 (0x4DAF30), na `amBackupRecordCheckValid: Line329 crc error`.
// Zet dit record (beide kopieen, offsets a en b) als het geen enkele geldige kopie heeft; een geldige kopie blijft ongemoeid (de game herstelt de andere zelf).
inline bool backup_record_valid(const std::vector<uint8_t>& m, size_t off, size_t size) {
    if (size < 8 || off + size > m.size()) return false;
    uint32_t stored;
    std::memcpy(&stored, &m[off], 4);
    return stored == crc32_ieee(&m[off + 4], size - 4);
}
inline bool seed_empty_backup_record(std::vector<uint8_t>& m, size_t a, size_t b, size_t size) {
    if (size < 8 || a + size > m.size() || b + size > m.size()) return false;
    if (backup_record_valid(m, a, size) || backup_record_valid(m, b, size)) return false;
    for (size_t off : {a, b}) {
        std::memset(&m[off], 0, size);
        const uint32_t c = crc32_ieee(&m[off + 4], size - 4);
        std::memcpy(&m[off], &c, 4);
    }
    return true;
}

}  // namespace rg_amhw
