// SPDX-License-Identifier: GPL-3.0-or-later
// The decision logic of "JVS over a COM port to the pipe" in rg-mfcam.dll (see THIRD TASK in rg_mfcam.cpp), without Windows calls,
// so that tests/test_jvs_com.cpp can test it on the host.
//
// What a Namco ES3 game (Mario Kart Arcade GP DX) does, measured on the exe (MK_AGP3_FINAL.exe, objdump): CreateFileA("COM3",
// GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) at 0x8ac1f2, then GetCommState/SetCommState (115200 8N1),
// SetCommTimeouts, SetupComm, PurgeComm and, as the sense line, GetCommModemStatus. Under Wine there is no COM3; our own JVS bridge
// (third_party/rg-jvsbridge) creates the pipe \\.\pipe\TeknoParrot_JVS and the daemon is the JVS board behind it. TeknoParrot's closed
// core replaces COM3 with that pipe (and lets the comm calls on that handle succeed); we now do this ourselves.
#pragma once

#include <cstdint>
#include <cstring>

namespace rg_jvscom {

// Does `name` refer to the port we replace? "COM3" and "\\.\COM3" (Windows accepts both), case-insensitive. Another port
// (COM30, COM1) does not belong to it: that is, for example, the ticket station or the card reader.
inline bool port_matches(const char* name, const char* port) {
    if (!name || !port || !*port) return false;
    if (name[0] == '\\' && name[1] == '\\' && name[2] == '.' && name[3] == '\\') name += 4;
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
    for (;; ++name, ++port) {
        if (lower(*name) != lower(*port)) return false;
        if (!*name) return true;
    }
}

// The JVS sense line in the modem status word (CTS = 0x10, DSR = 0x20): as long as the board has no address only CTS is on; after the
// address assignment (byte 0 of TeknoParrot_JvsState, mirrored by the bridge) CTS and DSR are on. Same values as the OpenParrot source
// for a non-Type-X game (amJvs.cpp, Hook_GetCommModemStatus) and as docs/architecture.md ("addressed is 0x30, otherwise 0x10").
inline uint32_t modem_status(bool addressed) { return addressed ? 0x30u : 0x10u; }

}  // namespace rg_jvscom
