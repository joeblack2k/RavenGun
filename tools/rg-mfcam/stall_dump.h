// SPDX-License-Identifier: GPL-3.0-or-later
// The decision logic of the "stalldump" in rg-mfcam.dll (see FIFTH TASK in rg_mfcam.cpp), without Windows calls, so that
// tests/test_stall_dump.cpp can test it on the host.
//
// Purpose: if a game stalls (black screen, no more log lines), you want to know where each thread is waiting. That is possible with gdb, but under
// Wine without frame pointers it gives little. This DLL can do it inside the process itself: at configured times it briefly suspends each thread,
// reads its registers and searches the stack for return addresses in the exe. A dword on the stack only counts as a return address if
// the bytes before it are an x86 call (E8 rel32, FF 15 abs32, FF /2 in its short forms): that filters out most of the coincidences.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace rg_stall {

// `p` points to the address of the instruction AFTER the call; `p[-1]` is the last byte of the call. The memory before it (6 bytes) must be
// readable; the caller checks that.
inline bool preceded_by_call(const uint8_t* p) {
    if (p[-5] == 0xE8) return true;                                     // call rel32
    if (p[-6] == 0xFF && p[-5] == 0x15) return true;                    // call [abs32]
    if (p[-6] == 0xFF && (p[-5] & 0xF8) == 0x90) return true;           // call [reg+disp32]
    if (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50) return true;           // call [reg+disp8]
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) return true;           // call reg
    if (p[-3] == 0xFF && p[-2] == 0x14) return true;                    // call [reg*scale+...]  (short SIB form)
    if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0x10) return true;           // call [reg]
    return false;
}

// "30,60,90" -> {30, 60, 90} seconds (1..86400, ascending not required); garbage is skipped.
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
