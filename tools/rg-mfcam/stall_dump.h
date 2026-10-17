// SPDX-License-Identifier: GPL-3.0-or-later
// De beslislogica van de "stalldump" in rg-mfcam.dll (zie VIJFDE TAAK in rg_mfcam.cpp), zonder Windows-aanroepen, zodat
// tests/test_stall_dump.cpp haar op de host kan toetsen.
//
// Waarvoor: staat een spel stil (zwart beeld, geen logregels meer), dan wil je weten waar elke draad wacht. Met gdb kan dat, maar onder
// Wine zonder framepointers geeft dat weinig. Deze DLL kan het in het proces zelf: op ingestelde tijdstippen elke draad even stilzetten,
// zijn registers lezen en de stapel afzoeken naar terugkeeradressen in de exe. Een dword op de stapel telt alleen als terugkeeradres als
// de bytes ervoor een x86-aanroep zijn (E8 rel32, FF 15 abs32, FF /2 in zijn korte vormen): dat filtert het gros van het toeval weg.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace rg_stall {

// `p` wijst naar het adres van de instructie NA de aanroep; `p[-1]` is de laatste byte van de aanroep. Het geheugen ervoor (6 bytes) moet
// leesbaar zijn; de aanroeper controleert dat.
inline bool preceded_by_call(const uint8_t* p) {
    if (p[-5] == 0xE8) return true;                                     // call rel32
    if (p[-6] == 0xFF && p[-5] == 0x15) return true;                    // call [abs32]
    if (p[-6] == 0xFF && (p[-5] & 0xF8) == 0x90) return true;           // call [reg+disp32]
    if (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50) return true;           // call [reg+disp8]
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) return true;           // call reg
    if (p[-3] == 0xFF && p[-2] == 0x14) return true;                    // call [reg*scale+...]  (korte SIB-vorm)
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0x10) return true;           // call [reg]
    return false;
}

// "30,60,90" -> {30, 60, 90} seconden (1..86400, stijgend niet vereist); rommel wordt overgeslagen.
inline std::vector<unsigned> parse_times(const char* s) {
    std::vector<unsigned> out;
    if (!s) return out;
    while (*s) {
        while (*s == ',' || *s == ' ') ++s;
        if (!*s) break;
        char* end = nullptr;
        const unsigned long v = std::strtoul(s, &end, 10);
        if (end == s) {
            ++s;
            continue;
        }
        if (v >= 1 && v <= 86400) out.push_back(static_cast<unsigned>(v));
        s = end;
    }
    return out;
}

}  // namespace rg_stall
