/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_kaboom_kern.c — zie rg_kaboom_kern.h. Geen Windows-aanroepen.
 */
#include "rg_kaboom_kern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const namen[RGK_AANTAL] = {
    "toets.enter", "toets.omlaag", "toets.kies", "toets.omhoog",
    "p1.omlaag", "p1.omhoog", "p1.links", "p1.rechts",
    "p2.omlaag", "p2.omhoog", "p2.links", "p2.rechts",
    "p3.omlaag", "p3.omhoog", "p3.links", "p3.rechts",
    "munt1", "munt2", "munt3", "munt4",
    "knop.p1b1", "knop.p1b2", "knop.p2b1", "knop.p2b2",
    "knop.p3b1", "knop.p3b2", "knop.p4b1", "knop.p4b2",
};

const char *rgk_naam(int i) { return i >= 0 && i < RGK_AANTAL ? namen[i] : "?"; }

static void zet(rgk_tabel *t, int i, const uint8_t *bits, int n)
{
    t->bron[i].n = n;
    for (int k = 0; k < n; k++) t->bron[i].bit[k] = bits[k];
}

/* Rampage (Adrenaline): het WartranTroopers-profiel van de game (feiten uit het profiel, welke knop op welke bit) en de vlaggen van
 * het bord. Testmenu = enter, Test Select (service) = kies; de aanval van speler 1/2/3 = omlaag/kies/omhoog van het paneel (zo leest
 * de game ze: OnAdrioKeyboardEvent); de start van een speler telt ook als zijn aanval (de game heeft geen startknop). */
void rgk_standaard(rgk_tabel *t)
{
    memset(t, 0, sizeof *t);
    zet(t, RGK_TOETS_ENTER, (const uint8_t[]){0}, 1);
    zet(t, RGK_TOETS_OMLAAG, (const uint8_t[]){4, 6}, 2);
    zet(t, RGK_TOETS_KIES, (const uint8_t[]){1, 22, 11}, 3);
    zet(t, RGK_TOETS_OMHOOG, (const uint8_t[]){23, 16}, 2);
    zet(t, RGK_P1_OMHOOG, (const uint8_t[]){7}, 1);
    zet(t, RGK_P1_OMLAAG, (const uint8_t[]){8}, 1);
    zet(t, RGK_P1_LINKS, (const uint8_t[]){9}, 1);
    zet(t, RGK_P1_RECHTS, (const uint8_t[]){10}, 1);
    zet(t, RGK_P2_OMHOOG, (const uint8_t[]){12}, 1);
    zet(t, RGK_P2_OMLAAG, (const uint8_t[]){13}, 1);
    zet(t, RGK_P2_LINKS, (const uint8_t[]){14}, 1);
    zet(t, RGK_P2_RECHTS, (const uint8_t[]){15}, 1);
    zet(t, RGK_P3_OMHOOG, (const uint8_t[]){17}, 1);
    zet(t, RGK_P3_OMLAAG, (const uint8_t[]){18}, 1);
    zet(t, RGK_P3_LINKS, (const uint8_t[]){19}, 1);
    zet(t, RGK_P3_RECHTS, (const uint8_t[]){20}, 1);
    zet(t, RGK_MUNT1, (const uint8_t[]){5}, 1);
    zet(t, RGK_MUNT2, (const uint8_t[]){2}, 1);
    zet(t, RGK_MUNT3, (const uint8_t[]){3}, 1);
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int rgk_lees_tabel(const char *tekst, rgk_tabel *uit, char *fout, size_t foutlen)
{
    rgk_tabel t;
    memset(&t, 0, sizeof t);
    int gezien[RGK_AANTAL] = {0};
    int nr = 0;
    const char *p = tekst;
    while (p && *p) {
        const char *eind = strchr(p, '\n');
        size_t len = eind ? (size_t)(eind - p) : strlen(p);
        char regel[256];
        nr++;
        if (len >= sizeof regel) { snprintf(fout, foutlen, "regel %d: te lang", nr); return -1; }
        memcpy(regel, p, len);
        regel[len] = 0;
        p = eind ? eind + 1 : NULL;
        char *c = strchr(regel, ';');
        if (c) *c = 0;
        c = strchr(regel, '#');
        if (c) *c = 0;
        char *r = trim(regel);
        if (!*r) continue;
        char *is = strchr(r, '=');
        if (!is) { snprintf(fout, foutlen, "regel %d: geen '=' in '%s'", nr, r); return -1; }
        *is = 0;
        char *naam = trim(r), *waarde = trim(is + 1);
        int i = 0;
        while (i < RGK_AANTAL && strcmp(namen[i], naam) != 0) i++;
        if (i == RGK_AANTAL) { snprintf(fout, foutlen, "regel %d: onbekende naam '%s'", nr, naam); return -1; }
        if (gezien[i]++) { snprintf(fout, foutlen, "regel %d: '%s' staat er twee keer", nr, naam); return -1; }
        char *stuk = waarde;
        while (*stuk) {
            char *komma = strchr(stuk, ',');
            if (komma) *komma = 0;
            char *getal = trim(stuk), *rest = NULL;
            long b = strtol(getal, &rest, 10);
            if (!*getal || *rest || b < 0 || b > 31) { snprintf(fout, foutlen, "regel %d: '%s' is geen bit (0..31)", nr, getal); return -1; }
            if (t.bron[i].n == RGK_MAX_BRON) { snprintf(fout, foutlen, "regel %d: meer dan %d bits voor '%s'", nr, RGK_MAX_BRON, naam); return -1; }
            t.bron[i].bit[t.bron[i].n++] = (uint8_t)b;
            if (!komma) break;
            stuk = komma + 1;
        }
        if (t.bron[i].n == 0) { snprintf(fout, foutlen, "regel %d: '%s' zonder bits (weglaten = nooit aan)", nr, naam); return -1; }
    }
    *uit = t;
    return 0;
}

static int aan(const rgk_bron *b, uint32_t bits)
{
    for (int k = 0; k < b->n; k++)
        if ((bits >> b->bit[k]) & 1u) return 1;
    return 0;
}

int rgk_stap(const rgk_tabel *t, rgk_staat *s, uint32_t bits)
{
    uint16_t toetsen = 0, knoppen = 0, sknop = 0;
    for (int i = RGK_TOETS_ENTER; i <= RGK_TOETS_OMHOOG; i++)
        if (aan(&t->bron[i], bits)) toetsen |= (uint16_t)(1u << (i - RGK_TOETS_ENTER));
    for (int i = RGK_P1_OMLAAG; i <= RGK_P3_RECHTS; i++)
        if (aan(&t->bron[i], bits)) knoppen |= (uint16_t)(1u << (i - RGK_P1_OMLAAG));
    for (int i = RGK_KNOP_P1B1; i <= RGK_KNOP_P4B2; i++)
        if (aan(&t->bron[i], bits)) sknop |= (uint16_t)(1u << (i - RGK_KNOP_P1B1));
    int veranderd = 0;
    if (s->begonnen) {
        for (int d = 0; d < 4; d++)
            if (aan(&t->bron[RGK_MUNT1 + d], bits) && !aan(&t->bron[RGK_MUNT1 + d], s->vorige)) {
                s->munt[d]++;
                veranderd |= RGK_VERANDERD_MUNT;
            }
    }
    if (!s->begonnen || toetsen != s->toetsen) veranderd |= RGK_VERANDERD_TOETSEN;
    if (!s->begonnen || knoppen != s->knoppen) veranderd |= RGK_VERANDERD_KNOPPEN;
    s->toetsen = toetsen;
    s->knoppen = knoppen;
    s->sknop = sknop;
    s->vorige = bits;
    s->begonnen = 1;
    return veranderd;
}

void rgk_munt_terug(rgk_staat *s, uint16_t deuren)
{
    for (int d = 0; d < 4; d++)
        if ((deuren >> (2 * d)) & 3u) s->munt[d] = 0;
}
