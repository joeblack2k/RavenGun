// SPDX-License-Identifier: GPL-3.0-or-later
// The decision logic of the window mode in rg-mfcam.dll (see NINTH TASK in rg_mfcam.cpp), without Windows calls, so that
// tests/test_window_mode.cpp can test it on the host.
//
// Purpose: Mario Kart DX runs on DXUT (D3D10). The game's default is full screen: the constructor of the
// settings object (0x405740 in MK_AGP3_FINAL.exe) sets `[obj+0x30] = 1` (0x4057AD, `c7 43 30 01 00 00 00`), next to the
// dimensions 0x550 x 0x300 (1360x768) at +0x28/+0x2c; no configuration file overrides that (only LAGUAGE_ID from config.csv).
// That byte goes to `[0xEC29C0]+0xC` and from there to (1) the command line for DXUT (0x770656: "-fullscreen" if it is 1,
// otherwise "-windowed") and (2) DXUTCreateDevice(bWindowed = byte != 1, 1360, 768) at 0x846808. TeknoParrot (profile Windowed=1)
// lets the game run windowed: in the DXVK log of the TP run there is no "Setting display mode"; with our core there is
// (twice 1368x768@60, all four test runs of 2026-10-18 12:03-12:07).
//
// What is here: search for the byte pattern of those three instructions (exactly one hit, otherwise do nothing) and return the position of the imm32
// of `mov [ebx+0x30],1`. The DLL sets that to 0: the game then picks "-windowed" itself, without a mode switch.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rg_winmode {

// mov [ebx+0x28],0x550 ; mov [ebx+0x2c],0x300 ; mov [ebx+0x30],1
inline constexpr uint8_t kPattern[] = {0xC7, 0x43, 0x28, 0x50, 0x05, 0x00, 0x00, 0xC7, 0x43, 0x2C, 0x00,
                                       0x03, 0x00, 0x00, 0xC7, 0x43, 0x30, 0x01, 0x00, 0x00, 0x00};
// The imm32 of the third instruction starts at pattern + 17.
inline constexpr size_t kImmOffset = 17;

// Number of hits in [p, p+n); `first` receives the position of the first (or n).
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

// Position of the imm32 that must become 0, or n if the pattern does not occur exactly once (then change nothing).
inline size_t imm_to_clear(const uint8_t* p, size_t n) {
    size_t first = 0;
    if (find_pattern(p, n, first) != 1) return n;
    return first + kImmOffset;
}

}  // namespace rg_winmode
