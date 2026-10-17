/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_unis_kern.h — de rekenkern van rg-unis (de eigen UsbPluginsDll.dll van UNIS voor Elevator Action Invasion en Raccoon Rampage,
 * zie rg_unis.c): van het bordbeeld van RavenGun (TeknoParrot_JvsState, indeling WartranTroopers uit packaging/tp-shm-profiles.json:
 * een 32-bits bitveld op offset 8, acht aanwijsassen van 1 byte op offset 12..19) naar wat het UNIS-bord de game zou melden.
 *
 * Geen Windows-aanroepen: dezelfde code draait in de DLL en in de eenheidstest op Linux (tests/unis_unit.c).
 *
 * BEWIJS (statisch, uit de PDB's van de games; docs/research/wartran/README.md, sectie "UNIS"):
 *   Raccoon Rampage  ABPGameState::GetIoData (RVA 0x9b0ef0) vult FCoUnisIoData (49 bytes, PDB-typen) met BIND_GetIoData;
 *                    ABPGameState::ParseIOData (0x9bfd00) leest de bytes (zie rgu_bouw_rr).
 *   Elevator Action  ABPGameState::Tick (0x7b2f90) vult FESIoData (16 bytes) met BIND_GetESIoData en FPACKET (92 bytes) met
 *   Invasion         BIND_GetGunData; ABPGameState::ParseIOData (0x792410) leest de bytes (zie rgu_bouw_eai).
 *
 * WELK signaal welke bit van het bordbeeld is, staat in een tabel per game (rg-unis.ini naast de DLL), niet in de code:
 *
 *   ; naam = bit[, bit ...]      een bron is aan als een van de bits aan staat
 *   enter = 4
 *   trig1 = 7
 *
 * Namen: test, service, enter, up, down, start1..start4, trig1..trig4, coin1..coin4. Een naam die er niet staat is nooit aan.
 * Daarnaast: game = eai|rr, usbio = N, water = 0|1, aim.mode = column|full, aim.out = BREEDTExHOOGTE (de uitvoer waarin het spelbeeld staat).
 */
#ifndef RG_UNIS_KERN_H
#define RG_UNIS_KERN_H

#include <stddef.h>
#include <stdint.h>

#define RGU_MAX_BRON 6

enum { RGU_GAME_ONBEKEND = 0, RGU_GAME_EAI = 1, RGU_GAME_RR = 2 };

enum {
    RGU_TEST, RGU_SERVICE, RGU_ENTER, RGU_UP, RGU_DOWN,
    RGU_START1, RGU_START2, RGU_START3, RGU_START4,
    RGU_TRIG1, RGU_TRIG2, RGU_TRIG3, RGU_TRIG4,
    RGU_COIN1, RGU_COIN2, RGU_COIN3, RGU_COIN4,
    RGU_AANTAL
};

typedef struct {
    int n;
    uint8_t bit[RGU_MAX_BRON];
} rgu_bron;

typedef struct {
    int game;                  /* RGU_GAME_* */
    int usbio;                 /* FCoUnisIoData.UsbIo (GEGOKT: 1) */
    int water;                 /* Raccoon: waterstand "hoog" aan (GEGOKT: 1) */
    int aim_kolom;             /* 1 = het spelbeeld is een kolom midden in de uitvoer (staande kast), 0 = vult de uitvoer */
    int uit_b, uit_h;          /* afmetingen van de uitvoer waarin het beeld staat (standaard 1920x1080) */
    rgu_bron bron[RGU_AANTAL];
} rgu_cfg;

typedef struct {
    uint32_t vorige;           /* het bitveld van de vorige stap */
    int begonnen;
    int munt[4];               /* de tellers van BIND_GetCoinNumByPlayer */
} rgu_staat;

typedef struct {
    uint32_t bits;             /* offset 8 van het bordbeeld */
    uint8_t as[8];             /* offset 12..19: P1X P1Y P2X P2Y P3X P3Y P4X P4Y, 0 = links/boven */
} rgu_beeld;

/* Afmetingen van de structuren van de games (PDB: FCoUnisIoData, FESIoData, FPACKET). */
#define RGU_RR_IO_LEN 49
#define RGU_EAI_IO_LEN 17      /* FCoUnisIoData van EAI (alleen Usb_IoGetIoData; de C++ van EAI gebruikt hem niet) */
#define RGU_EAI_ES_LEN 16
#define RGU_PAKKET_LEN 92

const char *rgu_naam(int i);
void rgu_standaard(rgu_cfg *c, int game);
/* 0 = goed; anders -1 met de reden in fout. Een tabel met een fout wordt NIET half gebruikt (c blijft ongemoeid). Begint bij c als
 * ingevuld door rgu_standaard: sleutels die er niet staan houden hun standaard; een signaalnaam die er staat vervangt de standaard. */
int rgu_lees_ini(const char *tekst, rgu_cfg *c, char *fout, size_t foutlen);
/* Een nieuw bordbeeld verwerken: muntflanken tellen. De eerste stap telt geen munt (geen flank zonder vorige). */
void rgu_stap(const rgu_cfg *c, rgu_staat *s, uint32_t bits);
int rgu_aan(const rgu_cfg *c, int signaal, uint32_t bits);

/* Raccoon Rampage: de 49 bytes van FCoUnisIoData (byte 0 UsbIo, 1 Mark 0xAA, 2.. IoData1.., 48 Crc). */
void rgu_bouw_rr(const rgu_cfg *c, const rgu_beeld *b, uint8_t uit[RGU_RR_IO_LEN]);
/* Elevator Action Invasion: de 16 bytes van FESIoData (byte 0 Mark, 1.. IoData1.., 15 Crc). */
void rgu_bouw_eai_es(const rgu_cfg *c, const rgu_beeld *b, uint8_t uit[RGU_EAI_ES_LEN]);
/* Elevator Action Invasion: aanwijspunt van speler p (0..1) in beeldpunten van het spel (b x h), en buiten beeld ja/nee. */
void rgu_eai_punt(const rgu_cfg *c, const rgu_beeld *b, int p, int spel_b, int spel_h, int *x, int *y, int *buiten);
/* FPACKET (92 bytes) van speler p voor BIND_GetGunData; alleen de velden die het bord invult, de rest blijft zoals de game hem liet. */
void rgu_eai_pakket(const rgu_cfg *c, const rgu_beeld *b, int p, int spel_b, int spel_h, uint8_t pakket[RGU_PAKKET_LEN]);

#endif
