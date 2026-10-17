// SPDX-License-Identifier: GPL-3.0-or-later
// De JVS-kant van `\\.\mxjvs` in rg-amhw, zonder Windows-koppen (te testen op Linux; tests/test_amhw.cpp).
//
// Wat de game doet [gelezen, LGI_RingW_F_safe.exe]: JvsManager::initialize (0x6EE520) roept amJvsInitEx(2) aan, dus de STUURPROGRAMMA-modus
// van amJvst (amJvstDriverSetup 0x490B30) en niet de COM-modus: CreateFileA("\\\\.\\mxjvs", GENERIC_READ|WRITE, 0, NULL, OPEN_EXISTING,
// FILE_FLAG_OVERLAPPED), dan GetCommState/SetCommState/SetCommTimeouts/SetupComm/PurgeComm (amJvstDriverSetupComm 0x490A20) en als
// sense-lijn GetCommModemStatus (0x4909B1). De verkeersopdracht is een enkele DeviceIoControl 0x9C402000 (0x490780): invoer = de JVS-bytes
// die de bovenlaag bouwt (volledig kader: E0, knoop, aantal, gegevens, som, met D0-escapes), lengte = nInBufferSize; uitvoer = 0x100 bytes
// voor het antwoordkader; overlapped, en het aantal ontvangen bytes leest de game uit OVERLAPPED.InternalHigh (0x4908A0) nadat de
// gebeurtenis van de OVERLAPPED gezet is. De COM-modus (0x4901xx) ontleedt hetzelfde: een antwoord begint met E0 en eindigt na `aantal`
// logische bytes (D0 xx = xx+1). Daarom is het stuurprogramma hier niets anders dan "schrijf het kader naar de lijn, lees een kader terug".
#pragma once
#include <cstddef>
#include <cstdint>

namespace rg_amhw {

constexpr uint32_t kIoctlJvsTransfer = 0x9C402000;

// Lengte (in ruwe bytes) van het eerste complete JVS-kader in buf[0..n), 0 als het nog niet compleet is. Leidende bytes voor de eerste E0 tellen
// niet mee als kader maar wel als verbruikt: `skip` krijgt het aantal ruwe bytes voor de E0. Een kader is E0, knoop, aantal en dan `aantal`
// bytes (de laatste is de som); D0 maakt van de volgende byte (+1) een logische byte.
inline size_t frame_length(const uint8_t* buf, size_t n, size_t* skip = nullptr) {
    size_t i = 0;
    while (i < n && buf[i] != 0xE0) ++i;
    if (skip) *skip = i;
    if (i >= n) return 0;
    size_t start = i;
    ++i;                       // voorbij E0
    size_t logical = 0, need = 0;   // logisch: knoop, aantal, gegevens...
    bool have_count = false;
    while (i < n) {
        uint8_t b = buf[i++];
        if (b == 0xD0) {
            if (i >= n) return 0;
            b = static_cast<uint8_t>(buf[i++] + 1);
        } else if (b == 0xE0 && logical > 0) {
            return 0;          // een nieuwe sync midden in het kader: kapot, de aanroeper bepaalt wat te doen
        }
        ++logical;
        if (logical == 2) { need = 2 + b; have_count = true; }
        if (have_count && logical == need) return i - start;
    }
    return 0;
}

// Antwoordt het bord op dit kader? Een globale reset (knoop FF, opdracht F0) niet; al het andere wel. buf is het volledige kader met E0.
inline bool expects_reply(const uint8_t* buf, size_t n) {
    if (n < 4 || buf[0] != 0xE0) return true;
    return !(buf[1] == 0xFF && buf[3] == 0xF0);
}

}  // namespace rg_amhw
