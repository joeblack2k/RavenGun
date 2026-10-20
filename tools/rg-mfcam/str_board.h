// SPDX-License-Identifier: GPL-3.0-or-later
// The decision logic of the emulated STR board (steering/force feedback) in rg-mfcam.dll (see TENTH TASK in rg_mfcam.cpp), without
// Windows calls, so that tests/test_str_board.cpp can test it on the host.
//
// What Mario Kart DX does (measured statically on MK_AGP3_FINAL.exe, objdump):
// * The STR object (constructor 0x5F5F40, vtable 0x9B4EC8) has a serial connection at +0x188 (constructor 0x5F5560) with port index 1 and
//   baud index 2 (0x5F5FD9/0x5F5FEA). The serial class (vtable 0xA11764) opens `\\.\COM<n>` from the table at 0xA1166C (0x8537D0) with
//   CreateFileA(GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED|FILE_ATTRIBUTE_NORMAL) at 0x853E1D, then SetupComm(0x2000,
//   0x2000), GetCommState, BuildCommDCBA("COM1: baud=19200 parity=N data=8 stop=1 ...", tables 0x98BC10..0x98BC7C) + SetCommState and
//   PurgeComm(0xF). Reading and writing use ReadFileEx/WriteFileEx and WaitForSingleObjectEx(INFINITE, alertable) (0x854350, 0x853AF0).
//   So: **COM1, 19200 8N1**, separate from JVS (COM3).
// * The read thread (0x5F57D0) reads **3 bytes** each round (0x5F5D00): "C01" sets [connection+0x49] = 1 (board connected), "C06" sets it to 0,
//   "E00" sets [connection+0x4A] = 1 (no error); "E02".."E08", "E10".., "E20" give error codes (0x56..0x5B, 0x5C.., 0x61) via 0x6095C0. Any other
//   three bytes do nothing. Only when the startup check is done ([connection+0x4B] = 1, 0x602EC4) does the game each round first send a command
//   of **10 bytes** (0x5F5C80; not writing 10 = error 0x61) and then read 3 bytes again.
// * The startup check (0x602DA6..0x602EE2): state 0 waits for +0x4A ("E00"), state 1 looks at +0x49 ("C01"; if not: connect
//   again); after 0x12C0 (4800) ticks without +0x49 comes error 0x61 = **E22-12 "STR PCB 通信エラー"** (0x602E39). The card reader follows after that.
//
// The board here is the minimum: connected and no error. As long as the game sends nothing, the board itself gives "C01", "E00", then "C01" over and over (a
// board reporting its status); as soon as commands arrive it answers with "C01" per 10 bytes and sends nothing more on its own. The contents of the
// commands (force feedback) are ignored.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rg_strpcb {

constexpr size_t kReplyLen = 3;
constexpr size_t kCommandLen = 10;

struct Board {
    unsigned status_sent = 0;   // status reports on its own
    uint64_t rx_bytes = 0;      // received command bytes
    uint64_t replies = 0;       // replies to commands
    bool command_mode = false;  // the game has sent something: replies only from now on

    // A status report on its own (listening phase), or nullptr if the game is already sending commands.
    const char* idle_status() {
        if (command_mode) return nullptr;
        const char* s = status_sent == 1 ? "E00" : "C01";
        ++status_sent;
        return s;
    }

    // `n` bytes received from the game: how many replies ("C01") must go out now (one per complete command of 10 bytes)?
    unsigned on_bytes(size_t n) {
        if (n == 0) return 0;
        command_mode = true;
        const uint64_t before = rx_bytes / kCommandLen;
        rx_bytes += n;
        const unsigned due = static_cast<unsigned>(rx_bytes / kCommandLen - before);
        replies += due;
        return due;
    }

    static const char* reply() { return "C01"; }
};

}  // namespace rg_strpcb
