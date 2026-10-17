/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_dinput8_kern.c -- zie rg_dinput8_kern.h. Geen Windows-aanroepen.
 */
#include "rg_dinput8_kern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void zet(rgd_bron *b, const uint8_t *bits, int n)
{
    b->n = n;
    for (int k = 0; k < n; k++) b->bit[k] = bits[k];
}

/* De indeling WartranTroopers (packaging/tp-shm-profiles.json): per speler start/trekker/herladen op bit 6/7/8, 11/12/13, ...;
   richtpunt P1X/P1Y/P2X/P2Y... vanaf offset 12. */
void rgd_standaard(rgd_tabel *t)
{
    memset(t, 0, sizeof *t);
    t->spelers = 2;
    for (int n = 0; n < RGD_MAX_SPELERS; n++) {
        t->sp[n].x = 12 + 2 * n;
        t->sp[n].y = 13 + 2 * n;
        zet(&t->sp[n].knop[0], (const uint8_t[]){(uint8_t)(7 + 5 * n)}, 1);
        zet(&t->sp[n].knop[1], (const uint8_t[]){(uint8_t)(8 + 5 * n)}, 1);
        zet(&t->sp[n].knop[2], (const uint8_t[]){(uint8_t)(6 + 5 * n)}, 1);
    }
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

/* Een geheel getal (decimaal, of hex met 0x); 0 = goed. */
static int getal(const char *s, long *uit)
{
    char *rest = NULL;
    if (!*s) return -1;
    long v = strtol(s, &rest, 0);
    if (*rest) return -1;
    *uit = v;
    return 0;
}

static int lees_bits(char *waarde, rgd_bron *b, int nr, const char *naam, char *fout, size_t foutlen)
{
    memset(b, 0, sizeof *b);
    char *stuk = waarde;
    while (*stuk) {
        char *komma = strchr(stuk, ',');
        if (komma) *komma = 0;
        char *g = trim(stuk);
        long v;
        if (getal(g, &v) != 0 || v < 0 || v > 31) { snprintf(fout, foutlen, "regel %d: '%s' is geen bit (0..31)", nr, g); return -1; }
        if (b->n == RGD_MAX_BITS) { snprintf(fout, foutlen, "regel %d: meer dan %d bits voor '%s'", nr, RGD_MAX_BITS, naam); return -1; }
        b->bit[b->n++] = (uint8_t)v;
        if (!komma) break;
        stuk = komma + 1;
    }
    if (b->n == 0) { snprintf(fout, foutlen, "regel %d: '%s' zonder bits (weglaten = nooit aan)", nr, naam); return -1; }
    return 0;
}

/* "01 02 ff" -> bytes; aantal of -1. */
static int lees_bytes(char *s, uint8_t *uit, int max)
{
    int n = 0;
    char *tok = strtok(s, " \t");
    while (tok) {
        char *rest = NULL;
        if (strlen(tok) != 2) return -1;
        long v = strtol(tok, &rest, 16);
        if (*rest || v < 0 || v > 255 || n == max) return -1;
        uit[n++] = (uint8_t)v;
        tok = strtok(NULL, " \t");
    }
    return n;
}

static int adres_uit_naam(const char *naam, const char *voorvoegsel, uint32_t *adres)
{
    size_t l = strlen(voorvoegsel);
    if (strncmp(naam, voorvoegsel, l) != 0) return 0;
    long v;
    const char *a = naam + l;
    if (strncmp(a, "0x", 2) != 0 && strncmp(a, "0X", 2) != 0) return -1;
    if (getal(a, &v) != 0 || v <= 0 || (unsigned long)v > 0xFFFFFFFFul) return -1;
    *adres = (uint32_t)v;
    return 1;
}

int rgd_lees_tabel(const char *tekst, rgd_tabel *uit, char *fout, size_t foutlen)
{
    rgd_tabel t;
    memset(&t, 0, sizeof t);
    t.spelers = -1;
    for (int n = 0; n < RGD_MAX_SPELERS; n++) t.sp[n].x = t.sp[n].y = -1;
    char gezien[64][32];
    int ngezien = 0;
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
        for (int i = 0; i < ngezien; i++)
            if (strcmp(gezien[i], naam) == 0) { snprintf(fout, foutlen, "regel %d: '%s' staat er twee keer", nr, naam); return -1; }
        if (ngezien < 64 && strlen(naam) < 32) strcpy(gezien[ngezien++], naam);
        else { snprintf(fout, foutlen, "regel %d: te veel of te lange namen", nr); return -1; }
        long v;
        uint32_t adres;
        int s, k, rk;
        char as;
        if (strcmp(naam, "spelers") == 0) {
            if (getal(waarde, &v) != 0 || v < 1 || v > RGD_MAX_SPELERS) { snprintf(fout, foutlen, "regel %d: spelers = 1..%d", nr, RGD_MAX_SPELERS); return -1; }
            t.spelers = (int)v;
        } else if (sscanf(naam, "p%d.%c%n", &s, &as, &rk) == 2 && naam[rk] == 0 && (as == 'x' || as == 'y')) {
            if (s < 1 || s > RGD_MAX_SPELERS) { snprintf(fout, foutlen, "regel %d: speler %d bestaat niet (1..%d)", nr, s, RGD_MAX_SPELERS); return -1; }
            if (getal(waarde, &v) != 0 || v < 0 || v >= RGD_BEELD) { snprintf(fout, foutlen, "regel %d: '%s' is geen offset (0..%d)", nr, waarde, RGD_BEELD - 1); return -1; }
            if (as == 'x') t.sp[s - 1].x = (int)v; else t.sp[s - 1].y = (int)v;
        } else if (sscanf(naam, "p%d.knop%d%n", &s, &k, &rk) == 2 && naam[rk] == 0) {
            if (s < 1 || s > RGD_MAX_SPELERS || k < 0 || k >= RGD_MAX_KNOPPEN) { snprintf(fout, foutlen, "regel %d: '%s' (speler 1..%d, knop 0..%d)", nr, naam, RGD_MAX_SPELERS, RGD_MAX_KNOPPEN - 1); return -1; }
            if (lees_bits(waarde, &t.sp[s - 1].knop[k], nr, naam, fout, foutlen) != 0) return -1;
        } else if (strcmp(naam, "as.x") == 0 || strcmp(naam, "as.y") == 0) {
            int i = naam[3] == 'x' ? 0 : 1;
            long lo, hi;
            char rest;
            if (sscanf(waarde, "%ld %ld %c", &lo, &hi, &rest) != 2 || lo == hi) { snprintf(fout, foutlen, "regel %d: %s = <rand links/boven> <rand rechts/onder>", nr, naam); return -1; }
            t.as_gezet[i] = 1, t.as_lo[i] = lo, t.as_hi[i] = hi;
        } else if (strcmp(naam, "buiten.x") == 0 || strcmp(naam, "buiten.y") == 0) {
            int i = naam[7] == 'x' ? 0 : 1;
            if (getal(waarde, &v) != 0) { snprintf(fout, foutlen, "regel %d: '%s' is geen getal", nr, waarde); return -1; }
            t.buiten_gezet[i] = 1, t.buiten[i] = v;
        } else if ((rk = adres_uit_naam(naam, "vlag.", &adres)) != 0 || (rk = adres_uit_naam(naam, "teller.", &adres)) != 0) {
            const int is_vlag = naam[0] == 'v';
            if (rk < 0) { snprintf(fout, foutlen, "regel %d: '%s': adres als 0x... (niet 0)", nr, naam); return -1; }
            int *n = is_vlag ? &t.nvlag : &t.nteller;
            rgd_adres *lijst = is_vlag ? t.vlag : t.teller;
            if (*n == RGD_MAX_ADRES) { snprintf(fout, foutlen, "regel %d: meer dan %d %s", nr, RGD_MAX_ADRES, is_vlag ? "vlaggen" : "tellers"); return -1; }
            lijst[*n].adres = adres;
            if (lees_bits(waarde, &lijst[*n].bron, nr, naam, fout, foutlen) != 0) return -1;
            (*n)++;
        } else if ((rk = adres_uit_naam(naam, "patch.", &adres)) != 0) {
            if (rk < 0) { snprintf(fout, foutlen, "regel %d: '%s': adres als 0x... (niet 0)", nr, naam); return -1; }
            if (t.npatch == RGD_MAX_PATCH) { snprintf(fout, foutlen, "regel %d: meer dan %d patches", nr, RGD_MAX_PATCH); return -1; }
            char *pijl = strstr(waarde, "->");
            if (!pijl) { snprintf(fout, foutlen, "regel %d: %s = <oude bytes> -> <nieuwe bytes>", nr, naam); return -1; }
            *pijl = 0;
            rgd_patch *pt = &t.patch[t.npatch];
            int no = lees_bytes(trim(waarde), pt->oud, RGD_MAX_PATCHBYTES), nn = lees_bytes(trim(pijl + 2), pt->nieuw, RGD_MAX_PATCHBYTES);
            if (no <= 0 || nn != no) { snprintf(fout, foutlen, "regel %d: %s: even veel oude als nieuwe bytes (hex, 1..%d, met spaties)", nr, naam, RGD_MAX_PATCHBYTES); return -1; }
            pt->adres = adres;
            pt->n = no;
            t.npatch++;
        } else {
            snprintf(fout, foutlen, "regel %d: onbekende naam '%s'", nr, naam);
            return -1;
        }
    }
    if (t.spelers < 0) { snprintf(fout, foutlen, "'spelers' ontbreekt"); return -1; }
    for (int n = 0; n < t.spelers; n++)
        if (t.sp[n].x < 0 || t.sp[n].y < 0) { snprintf(fout, foutlen, "speler %d: p%d.x en p%d.y ontbreken", n + 1, n + 1, n + 1); return -1; }
    *uit = t;
    return 0;
}

void rgd_guid(int n, uint8_t out[16])
{
    /* {52617645-6E47-756E-0000-00000000000n}: "RavE" "nG" "un", dan het spelernummer; vast, zodat control.cfg hem kan noemen. */
    static const uint8_t basis[16] = {0x45, 0x76, 0x61, 0x52, 0x47, 0x6E, 0x6E, 0x75, 0, 0, 0, 0, 0, 0, 0, 0};
    memcpy(out, basis, 16);
    out[15] = (uint8_t)(n + 1);
}

uint32_t rgd_bits(const uint8_t beeld[RGD_BEELD])
{
    return (uint32_t)beeld[8] | (uint32_t)beeld[9] << 8 | (uint32_t)beeld[10] << 16 | (uint32_t)beeld[11] << 24;
}

static int aan(const rgd_bron *b, uint32_t bits)
{
    for (int k = 0; k < b->n; k++)
        if ((bits >> b->bit[k]) & 1u) return 1;
    return 0;
}

/* byte 0..255 -> lo..hi, afgerond naar het dichtstbijzijnde gehele getal. */
static int32_t schaal(uint8_t b, long lo, long hi)
{
    long long d = (long long)(hi - lo) * b;
    long long q = d >= 0 ? (d + 127) / 255 : -((-d + 127) / 255);
    return (int32_t)(lo + q);
}

void rgd_joystick(const rgd_tabel *t, int n, const uint8_t beeld[RGD_BEELD], const long lo[2], const long hi[2], rgd_joy *uit)
{
    memset(uit, 0, sizeof *uit);
    if (n < 0 || n >= t->spelers) return;
    const rgd_speler *sp = &t->sp[n];
    const uint8_t b[2] = {beeld[sp->x], beeld[sp->y]};
    /* De offscreen-regel van de indeling: een as <= 1 of >= 254 is buiten beeld. */
    uit->buiten = b[0] <= 1 || b[0] >= 254 || b[1] <= 1 || b[1] >= 254;
    int32_t w[2];
    for (int i = 0; i < 2; i++) {
        const long l = t->as_gezet[i] ? t->as_lo[i] : lo[i], h = t->as_gezet[i] ? t->as_hi[i] : hi[i];
        w[i] = uit->buiten && t->buiten_gezet[i] ? (int32_t)t->buiten[i] : schaal(b[i], l, h);
    }
    uit->lX = w[0];
    uit->lY = w[1];
    const uint32_t bits = rgd_bits(beeld);
    for (int k = 0; k < RGD_MAX_KNOPPEN; k++) uit->knop[k] = aan(&sp->knop[k], bits) ? 0x80 : 0;
}

void rgd_stap(const rgd_tabel *t, rgd_flanken *f, uint32_t bits, uint32_t *vlaggen, int tellers[RGD_MAX_ADRES])
{
    *vlaggen = 0;
    for (int i = 0; i < RGD_MAX_ADRES; i++) tellers[i] = 0;
    if (f->begonnen) {
        for (int i = 0; i < t->nvlag; i++)
            if (aan(&t->vlag[i].bron, bits) && !aan(&t->vlag[i].bron, f->vorige)) *vlaggen |= 1u << i;
        for (int i = 0; i < t->nteller; i++)
            if (aan(&t->teller[i].bron, bits) && !aan(&t->teller[i].bron, f->vorige)) tellers[i] = 1;
    }
    f->vorige = bits;
    f->begonnen = 1;
}
