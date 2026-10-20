// SPDX-License-Identifier: GPL-3.0-or-later
// The decision logic of rg-mfcam.dll's own Banapass card reader reply (see SEVENTH TASK in rg_mfcam.cpp), without Windows calls,
// so that tests/test_bngrw_fake.cpp can test it on the host.
//
// WHAT THE GAME DOES (MK_AGP3_FINAL.exe v1.18.16, measured with objdump; addresses without ASLR, image base 0x400000)
//   The game imports bngrw.dll (Bandai Namco's Banapassport library, in the game folder; 11 functions, stdcall). The card readers
//   (two: reader 0 on COM4, reader 1 on COM2) are driven by a state machine at 0x601550 (seven states in a jump table at 0x601970)
//   that is driven by the startup checks (0x6027D0..), just before the IO check that opens COM3 (0x6022D0 -> 0x8AB880 -> 0x8AC1B0).
//   Each request gets a result slot [base + reader*8] = {result, reader}: the game presets it to {-1, 2} and waits until the callback
//   makes it {0, reader}. A result of 0xFFFFFED3 (-301) or 0xFFFFFE70 (-400) is an error: the game then logs code 0x43/0x45 (0x6095C0) and sets
//   the "reader present" flags [0xEC2A08]+0x194/0x195 to 0.
//   The callbacks are cdecl: Attach/DevReset/ReqLed/ReqSendUrlTo (a = reader, b = result, user), ReqBeep (same, only sets a byte), ReqWaitTouch
//   (a, b = result, card data (0x2A dwords), user). IsCmdExec(reader) <> 0 means "busy": the game then waits.
//
//   TeknoParrot's closed core replaces these functions (Banapass emulation, the card button is bit 0 of byte 8 of TeknoParrot_JvsState). The real
//   bngrw.dll opens COM4/COM2; under Wine those do not exist (measured in bngrw.dll: SetCommState, SetCommMask, PurgeComm).
//
// WHAT OUR OWN REPLY DOES: two readers that are present and have no card. Attach, DevReset, ReqLed, ReqBeep and ReqSendUrlTo succeed
// (return 1) and call their callback immediately with result 0; ReqWaitTouch stays open (IsCmdExec = 1) until ReqCancel; it ends without a callback carrying an
// error, because we do not know which result the game expects after a cancel.
#pragma once

#include <cstdint>

namespace rg_bngrw {

constexpr int kReaders = 2;
constexpr int kResultOk = 0;
constexpr int kAccepted = 1;   // return value of Attach/Req*: request accepted

// The state the fake library keeps per reader.
struct Reader {
    bool attached = false;
    bool waiting = false;   // a ReqWaitTouch is open
};

struct Fake {
    Reader r[kReaders];

    static bool valid(int reader) { return reader >= 0 && reader < kReaders; }

    // IsCmdExec: 1 as long as a request is open (only ReqWaitTouch can be that: the others are finished immediately).
    int is_cmd_exec(int reader) const { return valid(reader) && r[reader].waiting ? 1 : 0; }

    int attach(int reader) {
        if (!valid(reader)) return 0;
        r[reader].attached = true;
        r[reader].waiting = false;
        return kAccepted;
    }
    int dev_reset(int reader) {
        if (!valid(reader) || !r[reader].attached) return 0;
        r[reader].waiting = false;
        return kAccepted;
    }
    // Led, Beep, SendUrlTo: finished immediately.
    int simple_request(int reader) const { return valid(reader) && r[reader].attached ? kAccepted : 0; }

    int wait_touch(int reader) {
        if (!valid(reader) || !r[reader].attached) return 0;
        if (r[reader].waiting) return 0;   // two at the same time is not allowed
        r[reader].waiting = true;
        return kAccepted;
    }
    // ReqCancel: returns true if something was open.
    bool cancel(int reader) {
        if (!valid(reader)) return false;
        const bool was = r[reader].waiting;
        r[reader].waiting = false;
        return was;
    }
    void fin() {
        for (Reader& x : r) x = Reader();
    }
};

}  // namespace rg_bngrw
