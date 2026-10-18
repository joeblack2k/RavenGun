// SPDX-License-Identifier: GPL-3.0-or-later
// De beslislogica van het eigen Banapass-kaartlezer-antwoord in rg-mfcam.dll (zie ZEVENDE TAAK in rg_mfcam.cpp), zonder Windows-aanroepen,
// zodat tests/test_bngrw_fake.cpp haar op de host kan toetsen.
//
// WAT HET SPEL DOET (MK_AGP3_FINAL.exe v1.18.16, gemeten met objdump; adressen zonder ASLR, imagebase 0x400000)
//   Het spel importeert bngrw.dll (de Banapassport-bibliotheek van Bandai Namco, in de spelmap; 11 functies, stdcall). De kaartlezers
//   (twee: lezer 0 op COM4, lezer 1 op COM2) worden aangestuurd door een toestandsmachine op 0x601550 (zeven toestanden in een sprongtabel op 0x601970)
//   die door de opstartcontroles (0x6027D0..) wordt gedreven, vlak voor de IO-controle die COM3 opent (0x6022D0 -> 0x8AB880 -> 0x8AC1B0).
//   Elke aanvraag krijgt een resultaatslot [basis + lezer*8] = {resultaat, lezer}: het spel zet het vooraf op {-1, 2} en wacht tot de callback
//   het {0, lezer} maakt. Een resultaat 0xFFFFFED3 (-301) of 0xFFFFFE70 (-400) is een fout: dan logt het spel code 0x43/0x45 (0x6095C0) en zet de
//   "lezer aanwezig"-vlaggen [0xEC2A08]+0x194/0x195 op 0.
//   De callbacks zijn cdecl: Attach/DevReset/ReqLed/ReqSendUrlTo (a = lezer, b = resultaat, user), ReqBeep (idem, zet alleen een byte), ReqWaitTouch
//   (a, b = resultaat, kaartgegevens (0x2A dwords), user). IsCmdExec(lezer) <> 0 betekent "bezig": dan wacht het spel.
//
//   De gesloten TeknoParrot-kern vervangt deze functies (Banapass-emulatie, de kaartknop is bit 0 van byte 8 van TeknoParrot_JvsState). De echte
//   bngrw.dll opent COM4/COM2; onder Wine bestaan die niet (gemeten in de bngrw.dll: SetCommState, SetCommMask, PurgeComm).
//
// WAT HET EIGEN ANTWOORD DOET: twee lezers die er zijn en geen kaart hebben. Attach, DevReset, ReqLed, ReqBeep en ReqSendUrlTo slagen
// (return 1) en roepen meteen hun callback met resultaat 0; ReqWaitTouch blijft openstaan (IsCmdExec = 1) tot ReqCancel; zonder callback met een
// fout, want we weten niet welk resultaat het spel na een annulering verwacht.
#pragma once

#include <cstdint>

namespace rg_bngrw {

constexpr int kReaders = 2;
constexpr int kResultOk = 0;
constexpr int kAccepted = 1;   // returnwaarde van Attach/Req*: aanvraag aangenomen

// De toestand die de nepbibliotheek per lezer bijhoudt.
struct Reader {
    bool attached = false;
    bool waiting = false;   // een ReqWaitTouch staat open
};

struct Fake {
    Reader r[kReaders];

    static bool valid(int reader) { return reader >= 0 && reader < kReaders; }

    // IsCmdExec: 1 zolang een aanvraag openstaat (alleen ReqWaitTouch kan dat zijn: de andere zijn meteen klaar).
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
    // Led, Beep, SendUrlTo: meteen klaar.
    int simple_request(int reader) const { return valid(reader) && r[reader].attached ? kAccepted : 0; }

    int wait_touch(int reader) {
        if (!valid(reader) || !r[reader].attached) return 0;
        if (r[reader].waiting) return 0;   // twee tegelijk mag niet
        r[reader].waiting = true;
        return kAccepted;
    }
    // ReqCancel: geeft true als er iets openstond.
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
