// SPDX-License-Identifier: GPL-3.0-or-later
// De beslislogica van het nagebootste STR-bord (stuur/krachtterugkoppeling) in rg-mfcam.dll (zie TIENDE TAAK in rg_mfcam.cpp), zonder
// Windows-aanroepen, zodat tests/test_str_board.cpp haar op de host kan toetsen.
//
// Wat Mario Kart DX doet (statisch gemeten aan MK_AGP3_FINAL.exe, objdump):
// * Het STR-object (constructor 0x5F5F40, vtable 0x9B4EC8) heeft op +0x188 een seriele verbinding (constructor 0x5F5560) met poortindex 1 en
//   baudindex 2 (0x5F5FD9/0x5F5FEA). De seriele klasse (vtable 0xA11764) opent `\\.\COM<n>` uit de tabel op 0xA1166C (0x8537D0) met
//   CreateFileA(GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED|FILE_ATTRIBUTE_NORMAL) op 0x853E1D, dan SetupComm(0x2000,
//   0x2000), GetCommState, BuildCommDCBA("COM1: baud=19200 parity=N data=8 stop=1 ...", tabellen 0x98BC10..0x98BC7C) + SetCommState en
//   PurgeComm(0xF). Lezen en schrijven gaan met ReadFileEx/WriteFileEx en WaitForSingleObjectEx(INFINITE, alertable) (0x854350, 0x853AF0).
//   Dus: **COM1, 19200 8N1**, los van JVS (COM3).
// * De leesdraad (0x5F57D0) leest elke ronde **3 bytes** (0x5F5D00): "C01" zet [verbinding+0x49] = 1 (bord verbonden), "C06" zet hem op 0,
//   "E00" zet [verbinding+0x4A] = 1 (geen fout); "E02".."E08", "E10".., "E20" geven foutcodes (0x56..0x5B, 0x5C.., 0x61) via 0x6095C0. Andere
//   drie bytes doen niets. Pas als de opstartcontrole klaar is ([verbinding+0x4B] = 1, 0x602EC4) stuurt het spel elke ronde eerst een opdracht
//   van **10 bytes** (0x5F5C80; geen 10 geschreven = fout 0x61) en leest dan weer 3 bytes.
// * De opstartcontrole (0x602DA6..0x602EE2): toestand 0 wacht op +0x4A ("E00"), toestand 1 kijkt naar +0x49 ("C01"; zo niet: opnieuw
//   verbinden); na 0x12C0 (4800) ticks zonder +0x49 komt fout 0x61 = **E22-12 "STR PCB 通信エラー"** (0x602E39). Daarna volgt de kaartlezer.
//
// Het bord hier is het minimum: verbonden en geen fout. Zolang het spel niets stuurt geeft het bord zelf "C01", "E00", daarna steeds "C01" (een
// bord dat zijn status meldt); zodra er opdrachten komen antwoordt het per 10 bytes met "C01" en stuurt het niets meer uit zichzelf. De inhoud van de
// opdrachten (krachtterugkoppeling) wordt genegeerd.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rg_strpcb {

constexpr size_t kReplyLen = 3;
constexpr size_t kCommandLen = 10;

struct Board {
    unsigned status_sent = 0;   // statusmeldingen uit zichzelf
    uint64_t rx_bytes = 0;      // ontvangen opdrachtbytes
    uint64_t replies = 0;       // antwoorden op opdrachten
    bool command_mode = false;  // het spel heeft iets gestuurd: alleen nog antwoorden

    // Een statusmelding uit zichzelf (luisterfase), of nullptr als het spel al opdrachten stuurt.
    const char* idle_status() {
        if (command_mode) return nullptr;
        const char* s = status_sent == 1 ? "E00" : "C01";
        ++status_sent;
        return s;
    }

    // `n` bytes van het spel ontvangen: hoeveel antwoorden ("C01") moeten er nu uit (een per volledige opdracht van 10 bytes)?
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
