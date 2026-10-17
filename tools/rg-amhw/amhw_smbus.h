// SPDX-License-Identifier: GPL-3.0-or-later
// De SMBus-driver ("mxsmbus", interface-GUID {5C49E1FE-3FEC-4B8D-A4B5-76BE7025D842}) en de Super I/O-driver (\\.\mxsuperio) van een Sega Ring-kast, voor zover de
// AM Library van de games ze gebruikt. Gelezen in Operation G.H.O.S.T. (gs2.exe, amDipsw/amEeprom/amHm) en LGS:
//
// SMBus (alle opdrachten op hetzelfde handvat; de GUID levert via SetupDi het pad):
//   0x9C406008  versie: 4 bytes, laag woord moet 1 zijn ("Unknown SMBUS Driver Protocol")
//   0x9C402004  pakket van 0x25 bytes, in en uit hetzelfde:  [0] status (0 = goed)  [1] opdracht: 5 = lees byte, 4 = schrijf byte
//               [2] I2C-adres  [3] register  [5] gegevens (lees: uit, schrijf: in)  [6] 0xFF bij schrijven.
//               Opdracht 3 (pakket [18 03 adres 0 0 0 0]) is de "is de EEPROM klaar?"-controle van amEeprom (OG 0x58EDC0, vóór elke lees- of schrijfactie): de game zet
//               [0] op 0x18 (NAK) en wacht hoogstens [63B4EC] ms tot [0] 0 wordt (ACK). Beantwoordt het stuurprogramma dat niet, dan geeft amEepromRead -8 en faalt de
//               init van de game ("GetInfo: Faild to backup (code=-8)", OG: 3005).
//               Dipschakelaars: PCA9535 op adres 0x20 (amDipsw 0x58AAE0/0x58ACB0); register 4 en 5 zijn de twee dip-bytes, register 3 is een uitvoerpoort.
//               De waarde (reg4 >> 4) & 7 kiest de schermresolutie (tabel 0x5F72A4 in OG: 640x480, 640x480, 1024x600, 1024x768, 1280x720, 1280x1024, 1360x768, 1920x1080).
//   0x9C40200C  blokopdracht van 0x27 bytes (EEPROM, amEeprom 0x58E9D0/0x58EAE0):
//               [0] status  [1] 9 = lees blok, 8 = schrijf blok  [2..3] adres (0x57)  [4..5] offset  [6] lengte (hoogstens 0x20)  [7..] gegevens
//               lpBytesReturned moet 0x27 zijn.
// Super I/O (\\.\mxsuperio, amHm 0x592640 en 0x592F10):
//   0x9C406000  versie, laag woord 1
//   0x9C402004  4 bytes {chip, 0x0B, register, 0}: leest een Super I/O-register, resultaat in [3]. Chip-id = register 0x20 (hoog) en 0x21 (laag);
//               (id & 0xFFF0) == 0xA230 is type 1 (een chip), 0xA020 is type 2 (twee chips). Chip 1 bestaat niet in type 1.
//   0x9C40200C  3 bytes {chip, register, 0}: leest een hardwaremonitor-register via LPC, resultaat in [2]. Register 0x48 = het I2C-adres van de monitor (0x10..0x6F).
//   0x9C40A010  3 bytes {chip, register, waarde}: schrijft zo'n register (geen uitvoer).
// Alles is een registerbestand dat onthoudt wat er geschreven is; de monitor meldt dus 0 graden en 0 volt en alle controles lezen terug wat ze schreven.
// Alleen standaardbibliotheek; gebruikt door rg_amhw.cpp en de tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rg_amhw {

constexpr uint32_t kIoctlSmbusRequest = 0x9C402004;
constexpr uint32_t kIoctlSmbusBlock = 0x9C40200C;
constexpr uint32_t kIoctlLpcWrite = 0x9C40A010;
constexpr size_t kSmbusPacket = 0x25, kEepromPacket = 0x27;
constexpr size_t kEepromSize = 0x10000;

struct HwRegs {
    uint8_t i2c[256][256];
    uint8_t lpc[2][256];
    std::vector<uint8_t> eeprom;

    HwRegs() : eeprom(kEepromSize, 0xFF) {
        std::memset(i2c, 0, sizeof i2c);
        std::memset(lpc, 0, sizeof lpc);
        set_dip(0x70, 0x00);
        lpc[0][0x48] = 0x2D;   // I2C-adres van de hardwaremonitor (alleen gebruikt door type 2)
    }
    // PCA9535 op 0x20: register 4 en 5 zijn de dip-bytes. 0x70 = resolutie 1920x1080.
    void set_dip(uint8_t a, uint8_t b) { i2c[0x20][4] = a; i2c[0x20][5] = b; }

    // Een pakket van 0x25 bytes (lees/schrijf byte). Geeft false bij een opdracht die we niet kennen.
    bool smbus_request(uint8_t* b) {
        const uint8_t cmd = b[1], addr = b[2], reg = b[3];
        if (cmd == 3) { b[0] = 0; return true; }   // adres-ACK ("klaar")
        if (cmd == 5) b[5] = i2c[addr][reg];
        else if (cmd == 4) i2c[addr][reg] = b[5];
        else return false;
        b[0] = 0;
        return true;
    }

    // Een blokopdracht van 0x27 bytes (EEPROM). `touched` = (offset, lengte) van een schrijfactie, voor het bestand.
    bool eeprom_block(uint8_t* b, size_t* touched_off, size_t* touched_len) {
        const uint8_t cmd = b[1];
        const size_t off = static_cast<size_t>(b[4]) | (static_cast<size_t>(b[5]) << 8), len = b[6];
        if (len == 0 || len > 0x20 || off + len > eeprom.size()) return false;
        if (cmd == 9) std::memcpy(b + 7, &eeprom[off], len);
        else if (cmd == 8) {
            std::memcpy(&eeprom[off], b + 7, len);
            if (touched_off) *touched_off = off;
            if (touched_len) *touched_len = len;
        } else return false;
        b[0] = 0;
        return true;
    }

    // Super I/O-register lezen: {chip, 0x0B, register, 0} -> [3].
    bool superio_read(uint8_t* b) {
        if (b[1] != 0x0B) return false;
        const uint8_t chip = b[0], reg = b[2];
        uint8_t v = 0;
        if (chip == 0) v = reg == 0x20 ? 0xA2 : reg == 0x21 ? 0x30 : 0;   // type 1: id 0xA230
        b[3] = v;
        return true;
    }
    bool lpc_read(uint8_t* b) {
        if (b[0] > 1) return false;
        b[2] = lpc[b[0]][b[1]];
        return true;
    }
    bool lpc_write(const uint8_t* b) {
        if (b[0] > 1) return false;
        lpc[b[0]][b[1]] = b[2];
        return true;
    }
};

}  // namespace rg_amhw
