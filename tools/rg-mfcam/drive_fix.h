// SPDX-License-Identifier: GPL-3.0-or-later
// De beslislogica van de aanvullende schijfletter-herleiding in rg-mfcam.dll (zie ZESDE TAAK in rg_mfcam.cpp), zonder Windows-aanroepen, zodat
// tests/test_drive_fix.cpp haar op de host kan toetsen.
//
// Waarom: patch 0004 van de eigen OpenParrot-kern herleidt `<L>:\rest` naar `.\TeknoParrot\<L>\rest`, maar alleen voor CreateFileA/W en
// CreateDirectoryA/W. Mario Kart Arcade GP DX (MK_AGP3_FINAL.exe) schrijft zijn staat (testmenu, boekhouding, dongle-teller, ShareK-lijsten) als
// `F:/system/<naam>.bin.tmp` en hernoemt die dan met de CRT-functie `rename` naar `<naam>.bin`; zoekt ze met FindFirstFileA/W; en controleert
// met GetFileAttributesW en `_waccess_s`. Gemeten in de view (2026-10-18): na zes starts staan er alleen `*.bin.tmp` in
// `TeknoParrot/F/{system,DongleData,TestMode,Bookkeeping}`; de rename (msvcrt -> MoveFileExA) en de zoekopdrachten gaan niet door de herleiding.
// Gevolg: elke start begint met lege instellingen (camera uit enz.) en het spel meldt `... load failed ... file reset`.
//
// Dezelfde regel als de kern, zodat beide naar dezelfde plek wijzen: een pad dat begint met `<L>:\` of `<L>:/` (L niet C of Z) wordt
// `.\TeknoParrot\<L>\rest`. Hier alleen voor de letters die in RG_DRIVE_FIXUP staan (bijvoorbeeld "F"): `G:/Contents` laten we met rust,
// want het spel zet ShareK uit als die map niet te maken is (onschadelijk en gewenst).
#pragma once

#include <cstddef>
#include <cstring>

namespace rg_drivefix {

// Staat `letter` in de lijst `letters` (hoofdletterongevoelig; bv. "F" of "FG")? C en Z nooit.
inline bool letter_enabled(char letter, const char* letters) {
    if (!letters) return false;
    if (letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
    if (letter < 'A' || letter > 'Z' || letter == 'C' || letter == 'Z') return false;
    for (const char* p = letters; *p; ++p) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (c == letter) return true;
    }
    return false;
}

// Herleid `in` naar `out` (".\TeknoParrot\<L>\rest"). Geeft false (en laat `out` ongemoeid) als het pad niet herleid hoeft te worden of niet past.
template <typename Ch>
inline bool redirect(const Ch* in, const char* letters, Ch* out, size_t cap) {
    if (!in || !in[0] || !in[1] || !in[2]) return false;
    if (in[0] > 0x7f) return false;
    const char L = static_cast<char>(in[0]);
    if (in[1] != static_cast<Ch>(':')) return false;
    if (in[2] != static_cast<Ch>('\\') && in[2] != static_cast<Ch>('/')) return false;
    if (!letter_enabled(L, letters)) return false;
    size_t rest = 0;
    while (in[3 + rest]) ++rest;
    static const char prefix[] = ".\\TeknoParrot\\";   // 14 tekens
    const size_t pl = sizeof prefix - 1;
    if (pl + 2 + rest + 1 > cap) return false;           // prefix + letter + '\' + rest + nul
    size_t o = 0;
    for (size_t i = 0; i < pl; ++i) out[o++] = static_cast<Ch>(prefix[i]);
    char up = L;
    if (up >= 'a' && up <= 'z') up = static_cast<char>(up - 'a' + 'A');
    out[o++] = static_cast<Ch>(up);
    out[o++] = static_cast<Ch>('\\');
    for (size_t i = 0; i <= rest; ++i) out[o++] = in[3 + i];
    return true;
}

}  // namespace rg_drivefix
