/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_adrio_kern.h — de rekenkern van rg-adrio (de eigen KaboomAdrioFxPlusFourFeeders64.dll, zie rg_adrio.c): van het bordbeeld
 * van RavenGun (het 32-bits bitveld op offset 8 van TeknoParrot_JvsState, indeling WartranTroopers uit packaging/tp-shm-profiles.json)
 * naar wat het Adrenaline/Kaboom-bord de game zou melden: de toetsen van het bedieningspaneel, de spelersknoppen, de muntentellers
 * per deur en de knoppen van GetReadInputAdrio5.
 *
 * Geen Windows-aanroepen: dezelfde code draait in de DLL en in de eenheidstest op Linux (tests/adrio_unit.c).
 *
 * WELK signaal welke bit is, staat in een tabel per game (patches/<Game>/dropin/rg-adrio.ini, naast de DLL), niet in de code:
 *
 *   ; naam = bit[, bit ...]      een bron is aan als een van de bits aan staat
 *   toets.enter = 0
 *   p1.omhoog = 7
 *   munt1 = 5
 *
 * Namen (de vlaggen van het bord, gelezen in de wrapper KaboomAdrioFxPlusFourFeedersDll van Rampage):
 *   toets.enter/.omlaag/.kies/.omhoog         AdrioKeypadTy: 1, 2, 4, 8
 *   pN.omlaag/.omhoog/.links/.rechts (N=1..3) AdrioPlayerButtonTy: speler N gebruikt de vier bits vanaf 4*(N-1), in die orde
 *   muntN (N=1..4)                            deur N, ingang 1: een teller die bij elke opgaande flank een hoger gaat
 *   knop.pNb1/.pNb2 (N=1..4)                  AdrioReadInputTy.sButton: 1 << (2*(N-1)), 2 << (2*(N-1))
 * Een naam die er niet staat, is nooit aan. Zonder tabel geldt rgk_standaard() (die van Rampage).
 */
#ifndef RG_KABOOM_KERN_H
#define RG_KABOOM_KERN_H

#include <stddef.h>
#include <stdint.h>

#define RGK_MAX_BRON 6

enum {
    RGK_TOETS_ENTER, RGK_TOETS_OMLAAG, RGK_TOETS_KIES, RGK_TOETS_OMHOOG,
    RGK_P1_OMLAAG, RGK_P1_OMHOOG, RGK_P1_LINKS, RGK_P1_RECHTS,
    RGK_P2_OMLAAG, RGK_P2_OMHOOG, RGK_P2_LINKS, RGK_P2_RECHTS,
    RGK_P3_OMLAAG, RGK_P3_OMHOOG, RGK_P3_LINKS, RGK_P3_RECHTS,
    RGK_MUNT1, RGK_MUNT2, RGK_MUNT3, RGK_MUNT4,
    RGK_KNOP_P1B1, RGK_KNOP_P1B2, RGK_KNOP_P2B1, RGK_KNOP_P2B2,
    RGK_KNOP_P3B1, RGK_KNOP_P3B2, RGK_KNOP_P4B1, RGK_KNOP_P4B2,
    RGK_AANTAL
};

typedef struct {
    int n;
    uint8_t bit[RGK_MAX_BRON];
} rgk_bron;

typedef struct {
    rgk_bron bron[RGK_AANTAL];
} rgk_tabel;

typedef struct {
    uint16_t toetsen;     /* AdrioKeypadTy.un16AllKeys */
    uint16_t knoppen;     /* AdrioPlayerButtonTy.un16AllButton */
    uint16_t sknop;       /* AdrioReadInputTy.sButton.un16AllBits */
    uint16_t munt[4];     /* tellers per deur (ingang 1) */
    uint32_t vorige;      /* het bitveld van de vorige stap */
    int begonnen;
} rgk_staat;

/* Wat er in een stap veranderd is (voor de callbacks van de game). */
enum { RGK_VERANDERD_TOETSEN = 1, RGK_VERANDERD_KNOPPEN = 2, RGK_VERANDERD_MUNT = 4 };

const char *rgk_naam(int i);
void rgk_standaard(rgk_tabel *t);
/* 0 = goed; anders -1 met de reden (regelnummer en wat er mis is) in fout. Een tabel met een fout wordt NIET half gebruikt. */
int rgk_lees_tabel(const char *tekst, rgk_tabel *t, char *fout, size_t foutlen);
/* Een nieuw bitveld verwerken; geeft een combinatie van RGK_VERANDERD_*. De eerste stap telt geen munt (geen flank zonder vorige). */
int rgk_stap(const rgk_tabel *t, rgk_staat *s, uint32_t bits);
/* ResetCoinCountAdrio5: per deur twee bits (in1, in2); de teller van een deur gaat terug naar 0 als een van zijn bits aan staat. */
void rgk_munt_terug(rgk_staat *s, uint16_t deuren);

#endif
