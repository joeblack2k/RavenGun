/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Het serieele protocol van het Kaboom-bord (munten, toetsenblok, kaartjesuitgifte) van Infinity Blade FX, als zuivere logica:
 * geen Windows-aanroepen, geen bestanden. Dezelfde bron wordt door de DLL (rg_kaboom.c, mingw) en door de eenheidstest
 * (tests/kaboom_unit.c, gcc op de Deck) gebruikt.
 *
 * Herkomst (statisch gemeten, niets uitgevoerd): KaboomComDll.dll in de map van de game (CKaboomCom, 9 exports) en dezelfde
 * bibliotheek ingebakken in SwordGame.exe (InitLib op 0x233ece0, het pakketniveau op 0x233f000-0x233fe90). Beide gebruiken:
 *
 *   pakket   = magic u16 | controlesom u16 | lengte u16 | commando u16 | lading[lengte]      (little endian)
 *   controlesom = som (mod 65536) van de bytes vanaf "lengte" t/m het eind van de lading (lengte + 4 bytes)
 *   magic van de host: 0xF10E (in de exe ook 0x9A44 beschikbaar), magic van het bord: 0xB00F
 *   lengte van de lading hoogstens 0x122
 *   het antwoord herhaalt het commando; de lading heeft per commando een vaste lengte:
 *     0x06  versie/status       18 bytes; bytes 12..15 van de lading zijn de vier toetsenblok-tellers
 *     0x08  kaartjes te geven   2 bytes (u16)
 *     0x09  zet kaartjes te geven (lading u16)  0 bytes
 *     0x0A  kaartjesuitgifte    2 bytes (u16 teller) of 4 bytes (u16 teller, u16 bezig=1)
 *     0x0B  reset kaartjes      0 bytes
 *     0x0C  huidige muntteller  4 bytes: u16 slot 1, u16 slot 2
 *     0x0D  reset muntteller    4 bytes: de tellers van voor de reset (u16, u16)
 *
 * Seriele instellingen (InitLib): \\.\COM<poort>, 57600 baud, 8 databits; in de game is de poort 9 (SwordGame.exe 0x1cdc7b2:
 * push 9 / call 0x233ece0).
 */
#ifndef RG_KABOOM_PROTO_H
#define RG_KABOOM_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define KB_MAGIC_HOST_A 0xF10Eu
#define KB_MAGIC_HOST_B 0x9A44u
#define KB_MAGIC_BORD   0xB00Fu
#define KB_MAX_LADING   0x122u
#define KB_KOP          8u

enum {
    KB_CMD_VERSIE = 0x06,
    KB_CMD_KAARTJES_TE_GEVEN = 0x08,
    KB_CMD_ZET_KAARTJES = 0x09,
    KB_CMD_KAARTJES_UIT = 0x0A,
    KB_CMD_RESET_KAARTJES = 0x0B,
    KB_CMD_MUNTEN = 0x0C,
    KB_CMD_RESET_MUNTEN = 0x0D
};

typedef struct {
    uint16_t munten[2];        /* de tellers die cmd 0x0C geeft; een munt in het gedeelde geheugen telt slot 1 op */
    uint16_t kaartjes_te_geven;
    uint16_t kaartjes_uitgegeven;
    uint8_t toetsen[4];        /* de vier tellers van het toetsenblok (lading 6, bytes 12..15) */
    uint32_t vorige_control;   /* vorige waarde van het controlewoord uit het gedeelde geheugen, voor de flank */
    uint32_t munt_bit;         /* masker van de munt in dat woord (0x08 in de indeling TimeCrisis5) */
    uint8_t aanvraag[8 + KB_MAX_LADING];
    size_t aanvraag_len;
    uint8_t antwoord[(8 + KB_MAX_LADING) * 4];
    size_t antwoord_len, antwoord_pos;
    unsigned verwerkt;         /* aantal geldige aanvragen, voor het logboek */
    unsigned afgekeurd;        /* aantal pakketten met een verkeerde magic of controlesom */
} kb_bord;

void kb_init(kb_bord* b, uint32_t munt_bit);
uint16_t kb_controlesom(const uint8_t* vanaf_lengte, size_t n);          /* n = lengte + 4 */
/* Een munt telt op bij de stijgende flank van munt_bit in het controlewoord (offset 8 van het gedeelde geheugen). */
void kb_controlewoord(kb_bord* b, uint32_t woord);
/* De bytes die de game met WriteFile schrijft. Zodra een heel pakket er staat wordt het beantwoord (antwoord komt in de wachtrij). */
void kb_schrijf(kb_bord* b, const uint8_t* data, size_t n);
/* De bytes die de game met ReadFile leest (de wachtrij, hoogstens n); geeft het aantal bytes. */
size_t kb_lees(kb_bord* b, uint8_t* uit, size_t n);
/* PurgeComm: wachtrij en half pakket weg. */
void kb_spoel(kb_bord* b);

#endif
