// SPDX-License-Identifier: GPL-3.0-or-later
// De beslislogica van "JVS over een COM-poort naar de pipe" in rg-mfcam.dll (zie DERDE TAAK in rg_mfcam.cpp), zonder Windows-aanroepen,
// zodat tests/test_jvs_com.cpp haar op de host kan toetsen.
//
// Wat een Namco ES3-game (Mario Kart Arcade GP DX) doet, gemeten aan de exe (MK_AGP3_FINAL.exe, objdump): CreateFileA("COM3",
// GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) op 0x8ac1f2, daarna GetCommState/SetCommState (115200 8N1),
// SetCommTimeouts, SetupComm, PurgeComm en, als sense-lijn, GetCommModemStatus. Onder Wine is er geen COM3; de eigen JVS-brug
// (third_party/rg-jvsbridge) maakt de pipe \\.\pipe\TeknoParrot_JVS en de daemon is het JVS-bord erachter. De gesloten kern van TeknoParrot
// vervangt COM3 door die pipe (en laat de comm-aanroepen op dat handvat slagen); dit doen wij nu zelf.
#pragma once

#include <cstdint>
#include <cstring>

namespace rg_jvscom {

// Staat `name` voor de poort die we vervangen? "COM3" en "\\.\COM3" (Windows accepteert beide), zonder letterverschil. Een andere poort
// (COM30, COM1) hoort er niet bij: dat is bijvoorbeeld het ticketstation of de kaartlezer.
inline bool port_matches(const char* name, const char* port) {
    if (!name || !port || !*port) return false;
    if (name[0] == '\\' && name[1] == '\\' && name[2] == '.' && name[3] == '\\') name += 4;
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
    for (;; ++name, ++port) {
        if (lower(*name) != lower(*port)) return false;
        if (!*name) return true;
    }
}

// De sense-lijn van JVS in het modemstatuswoord (CTS = 0x10, DSR = 0x20): zolang het bord geen adres heeft staat alleen CTS aan, na de
// adrestoekenning (byte 0 van TeknoParrot_JvsState, door de brug gespiegeld) staan CTS en DSR aan. Dezelfde waarden als de OpenParrot-bron
// voor een niet-Type-X-game (amJvs.cpp, Hook_GetCommModemStatus) en als docs/architecture.md ("geadresseerd is 0x30, anders 0x10").
inline uint32_t modem_status(bool addressed) { return addressed ? 0x30u : 0x10u; }

}  // namespace rg_jvscom
