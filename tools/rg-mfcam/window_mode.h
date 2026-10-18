// SPDX-License-Identifier: GPL-3.0-or-later
// De beslislogica van de venstermodus in rg-mfcam.dll (zie NEGENDE TAAK in rg_mfcam.cpp), zonder Windows-aanroepen, zodat
// tests/test_window_mode.cpp haar op de host kan toetsen.
//
// Waarvoor: Mario Kart DX draait op DXUT (D3D10). De standaard van het spel is volledig scherm: de constructor van het
// instellingenobject (0x405740 in MK_AGP3_FINAL.exe) zet `[obj+0x30] = 1` (0x4057AD, `c7 43 30 01 00 00 00`), naast de
// afmetingen 0x550 x 0x300 (1360x768) op +0x28/+0x2c; geen configuratiebestand overschrijft dat (alleen LAGUAGE_ID uit config.csv).
// Die byte gaat naar `[0xEC29C0]+0xC` en daarmee naar (1) de opdrachtregel voor DXUT (0x770656: "-fullscreen" als hij 1 is,
// anders "-windowed") en (2) DXUTCreateDevice(bWindowed = byte != 1, 1360, 768) op 0x846808. TeknoParrot (profiel Windowed=1)
// laat het spel venstergewijs draaien: in het DXVK-logboek van de TP-run staat geen "Setting display mode"; bij onze kern wel
// (twee keer 1368x768@60, alle vier de proefruns van 2026-10-18 12:03-12:07).
//
// Wat hier staat: het bytepatroon van die drie opdrachten zoeken (exact een treffer, anders niets doen) en de plek van de imm32
// van `mov [ebx+0x30],1` teruggeven. De DLL zet die op 0: het spel kiest dan zelf "-windowed", zonder modewissel.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rg_winmode {

// mov [ebx+0x28],0x550 ; mov [ebx+0x2c],0x300 ; mov [ebx+0x30],1
inline constexpr uint8_t kPattern[] = {0xC7, 0x43, 0x28, 0x50, 0x05, 0x00, 0x00, 0xC7, 0x43, 0x2C, 0x00,
                                       0x03, 0x00, 0x00, 0xC7, 0x43, 0x30, 0x01, 0x00, 0x00, 0x00};
// De imm32 van de derde opdracht begint op patroon + 17.
inline constexpr size_t kImmOffset = 17;

// Aantal treffers in [p, p+n); `first` krijgt de positie van de eerste (of n).
inline size_t find_pattern(const uint8_t* p, size_t n, size_t& first) {
    first = n;
    size_t hits = 0;
    if (n < sizeof kPattern) return 0;
    for (size_t i = 0; i + sizeof kPattern <= n; ++i) {
        if (p[i] == kPattern[0] && std::memcmp(p + i, kPattern, sizeof kPattern) == 0) {
            if (hits == 0) first = i;
            ++hits;
        }
    }
    return hits;
}

// Positie van de imm32 die op 0 moet, of n als het patroon niet precies een keer voorkomt (dan niets aanpassen).
inline size_t imm_to_clear(const uint8_t* p, size_t n) {
    size_t first = 0;
    if (find_pattern(p, n, first) != 1) return n;
    return first + kImmOffset;
}

}  // namespace rg_winmode
