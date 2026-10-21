// SPDX-License-Identifier: GPL-3.0-or-later
// The settings of rg-mfcam.dll's RG_ERRCLEAR (see FIFTEENTH TASK in rg_mfcam.cpp), without Windows calls, so that
// tests/test_err_clear.cpp can test them on the host.
//
// WHY (Lost Land Adventure's launcher, RSLauncher.exe, 64-bit, objdump)
//   WinMain (0x140038600) raises error 58 "20-02 VERSION UP ERROR 2" with 0x140035190 right before its message loop (0x1400388db),
//   with no condition. The error is registered (0x140033c62) as class 2 with the playable flag 0, so as long as it is in the
//   launcher's error list (manager at [0x1404098e0], std::map at +0x38) the fatal-error sum (0x140034360) writes 1 to
//   RSSharedData+0x30 (bErrorFatal) and the game stays on FATAL ERROR. Nothing in the exe takes it out again: the only ways out of
//   that list are the plain clear 0x140035e70 (which for a class 1 or 2 error only zeroes the entry's time and keeps it), the
//   test-mode sweep 0x140035310 (class 1 only), the clock check (error 59 only) and the forced clear 0x140035260, which the
//   launcher itself uses for its USB dongle errors 53..56 (0x14003490b); no caller passes 58 to any of them.
//   RG_ERRCLEAR=<manager rva>:<clear function rva>:<error code>:<first bytes of that function> calls the launcher's own forced clear
//   for that code once, on the launcher's main thread (its first GetMessageW, right after the raise), the way it clears its dongle
//   errors. The first bytes must match, so another build of the exe is left alone.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rg_errclear {

inline constexpr size_t kMaxPrologue = 16;

struct Spec {
    uint64_t mgr_rva = 0;      // where the exe keeps the pointer to its error manager
    uint64_t fn_rva = 0;       // the forced clear: int fn(manager, code, force)
    int code = 0;              // the error code to clear
    uint8_t prologue[kMaxPrologue] = {};
    size_t prologue_len = 0;   // at least 4 bytes are required
};

inline int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "4098e0:35260:3a:8954241053555748": all hex. Returns false for anything incomplete or malformed.
inline bool parse(const char* s, Spec& out) {
    if (!s || !*s) return false;
    Spec r;
    char* end = nullptr;
    r.mgr_rva = std::strtoull(s, &end, 16);
    if (!end || *end != ':' || !r.mgr_rva) return false;
    r.fn_rva = std::strtoull(end + 1, &end, 16);
    if (!end || *end != ':' || !r.fn_rva) return false;
    const unsigned long long code = std::strtoull(end + 1, &end, 16);
    if (!end || *end != ':' || code == 0 || code > 0xffff) return false;
    r.code = static_cast<int>(code);
    const char* p = end + 1;
    while (*p) {
        const int hi = hexval(p[0]);
        const int lo = p[1] ? hexval(p[1]) : -1;
        if (hi < 0 || lo < 0 || r.prologue_len >= kMaxPrologue) return false;
        r.prologue[r.prologue_len++] = static_cast<uint8_t>(hi * 16 + lo);
        p += 2;
    }
    if (r.prologue_len < 4) return false;
    out = r;
    return true;
}

// The function's first bytes as they are in memory against the expected ones.
inline bool prologue_matches(const Spec& s, const uint8_t* code) {
    return code && std::memcmp(code, s.prologue, s.prologue_len) == 0;
}

}  // namespace rg_errclear
