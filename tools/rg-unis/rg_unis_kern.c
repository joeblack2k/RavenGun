/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_unis_kern.c — zie rg_unis_kern.h. Geen Windows-aanroepen.
 */
#include "rg_unis_kern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const namen[RGU_AANTAL] = {
    "test", "service", "enter", "up", "down",
    "start1", "start2", "start3", "start4",
    "trig1", "trig2", "trig3", "trig4",
    "coin1", "coin2", "coin3", "coin4",
};

const char *rgu_naam(int i) { return i >= 0 && i < RGU_AANTAL ? namen[i] : "?"; }

static void zet(rgu_cfg *c, int i, const uint8_t *bits, int n)
{
    c->bron[i].n = n;
    for (int k = 0; k < n; k++) c->bron[i].bit[k] = bits[k];
}

/* De bits van het bordbeeld (packaging/tp-shm-profiles.json, WartranTroopers): 0 test, 1 service, 2 munt 1, 3 munt 2, 4 menu omhoog
 * (volume omhoog, toets F4), 5 menu omlaag (F5), dan per speler vijf bits vanaf 6/11/16/21: start, trekker, herladen, granaat, aux1. */
void rgu_standaard(rgu_cfg *c, int game)
{
    memset(c, 0, sizeof *c);
    c->game = game;
    c->usbio = 1;
    c->water = 1;
    c->uit_b = 1920;
    c->uit_h = 1080;
    if (game == RGU_GAME_RR) {
        /* Raccoon Rampage. TeknoParrot-profiel RaccoonRampage.xml: Test = "Test Menu Up", Service1 = "Test Menu Down", Enter = P1Button5,
         * "Credits P1..P4" = de startknoppen (P1ButtonStart, P2ButtonStart, JvsTwoP1ButtonStart, JvsTwoP2ButtonStart) en de trekker
         * van speler N is de startknop in het spel (StartPlay bij de opgaande flank van de trekker, ParseIOData). Enter: F4 = volume
         * omhoog (game.ini: "service (F3) = omlaag, F4 = enter"). De munttoetsen 5 en 6 tellen mee als munt 1 en 2 (GEGOKT, onschadelijk). */
        zet(c, RGU_TEST, (const uint8_t[]){0}, 1);
        zet(c, RGU_SERVICE, (const uint8_t[]){1}, 1);
        zet(c, RGU_ENTER, (const uint8_t[]){4}, 1);
        zet(c, RGU_TRIG1, (const uint8_t[]){7}, 1);
        zet(c, RGU_TRIG2, (const uint8_t[]){12}, 1);
        zet(c, RGU_TRIG3, (const uint8_t[]){17}, 1);
        zet(c, RGU_TRIG4, (const uint8_t[]){22}, 1);
        zet(c, RGU_COIN1, (const uint8_t[]){6, 2}, 2);
        zet(c, RGU_COIN2, (const uint8_t[]){11, 3}, 2);
        zet(c, RGU_COIN3, (const uint8_t[]){16}, 1);
        zet(c, RGU_COIN4, (const uint8_t[]){21}, 1);
        c->aim_kolom = 0;
    } else {
        /* Elevator Action Invasion. Profiel ElevatorActionInvasion.xml: Coin 1/2, Test, Menu Up = P1Button5, Menu Down = P1Button6,
         * Start P1/P2, trekker P1/P2. In het spel: IoData2 bit 0 = Enter (Control_Enter), bit 1 = omlaag, bit 2 = omhoog. */
        zet(c, RGU_ENTER, (const uint8_t[]){0}, 1);
        zet(c, RGU_UP, (const uint8_t[]){4}, 1);
        zet(c, RGU_DOWN, (const uint8_t[]){5}, 1);
        zet(c, RGU_START1, (const uint8_t[]){6}, 1);
        zet(c, RGU_START2, (const uint8_t[]){11}, 1);
        zet(c, RGU_TRIG1, (const uint8_t[]){7}, 1);
        zet(c, RGU_TRIG2, (const uint8_t[]){12}, 1);
        zet(c, RGU_COIN1, (const uint8_t[]){2}, 1);
        zet(c, RGU_COIN2, (const uint8_t[]){3}, 1);
        c->aim_kolom = 1;   /* staande kast, 1080x1920: het beeld is een smalle kolom midden in het scherm (findings: "Staande kast") */
    }
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int rgu_lees_ini(const char *tekst, rgu_cfg *uit, char *fout, size_t foutlen)
{
    rgu_cfg t = *uit;
    int gezien[RGU_AANTAL] = {0};
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
        if (!strcmp(naam, "game")) {
            if (!strcmp(waarde, "eai")) t.game = RGU_GAME_EAI;
            else if (!strcmp(waarde, "rr")) t.game = RGU_GAME_RR;
            else { snprintf(fout, foutlen, "regel %d: game = eai of rr, niet '%s'", nr, waarde); return -1; }
            continue;
        }
        if (!strcmp(naam, "usbio")) {
            char *rest = NULL;
            long v = strtol(waarde, &rest, 10);
            if (!*waarde || *rest || v < 0 || v > 255) { snprintf(fout, foutlen, "regel %d: usbio is 0..255", nr); return -1; }
            t.usbio = (int)v;
            continue;
        }
        if (!strcmp(naam, "water")) {
            if (strcmp(waarde, "0") && strcmp(waarde, "1")) { snprintf(fout, foutlen, "regel %d: water = 0 of 1", nr); return -1; }
            t.water = waarde[0] == '1';
            continue;
        }
        if (!strcmp(naam, "aim.mode")) {
            if (!strcmp(waarde, "column")) t.aim_kolom = 1;
            else if (!strcmp(waarde, "full")) t.aim_kolom = 0;
            else { snprintf(fout, foutlen, "regel %d: aim.mode = column of full", nr); return -1; }
            continue;
        }
        if (!strcmp(naam, "aim.out")) {
            int b = 0, h = 0;
            char x = 0;
            if (sscanf(waarde, "%d%c%d", &b, &x, &h) != 3 || (x != 'x' && x != 'X') || b < 16 || h < 16 || b > 16384 || h > 16384) {
                snprintf(fout, foutlen, "regel %d: aim.out = BREEDTExHOOGTE", nr);
                return -1;
            }
            t.uit_b = b;
            t.uit_h = h;
            continue;
        }
        int i = 0;
        while (i < RGU_AANTAL && strcmp(namen[i], naam) != 0) i++;
        if (i == RGU_AANTAL) { snprintf(fout, foutlen, "regel %d: onbekende naam '%s'", nr, naam); return -1; }
        if (gezien[i]++) { snprintf(fout, foutlen, "regel %d: '%s' staat er twee keer", nr, naam); return -1; }
        t.bron[i].n = 0;
        char *stuk = waarde;
        while (*stuk) {
            char *komma = strchr(stuk, ',');
            if (komma) *komma = 0;
            char *getal = trim(stuk), *rest = NULL;
            long b = strtol(getal, &rest, 10);
            if (!*getal || *rest || b < 0 || b > 31) { snprintf(fout, foutlen, "regel %d: '%s' is geen bit (0..31)", nr, getal); return -1; }
            if (t.bron[i].n == RGU_MAX_BRON) { snprintf(fout, foutlen, "regel %d: meer dan %d bits voor '%s'", nr, RGU_MAX_BRON, naam); return -1; }
            t.bron[i].bit[t.bron[i].n++] = (uint8_t)b;
            if (!komma) break;
            stuk = komma + 1;
        }
        if (t.bron[i].n == 0) { snprintf(fout, foutlen, "regel %d: '%s' zonder bits (weglaten = standaard, 'nooit' bestaat niet: gebruik bit 31)", nr, naam); return -1; }
    }
    *uit = t;
    return 0;
}

int rgu_aan(const rgu_cfg *c, int signaal, uint32_t bits)
{
    const rgu_bron *b = &c->bron[signaal];
    for (int k = 0; k < b->n; k++)
        if ((bits >> b->bit[k]) & 1u) return 1;
    return 0;
}

void rgu_stap(const rgu_cfg *c, rgu_staat *s, uint32_t bits)
{
    if (s->begonnen) {
        for (int d = 0; d < 4; d++)
            if (rgu_aan(c, RGU_COIN1 + d, bits) && !rgu_aan(c, RGU_COIN1 + d, s->vorige)) s->munt[d]++;
    }
    s->vorige = bits;
    s->begonnen = 1;
}

static uint8_t crc_van(const uint8_t *b, int van, int tot)
{
    uint8_t som = 0;
    for (int i = van; i < tot; i++) som = (uint8_t)(som + b[i]);
    return (uint8_t)~som;
}

/* Raccoon Rampage, ParseIOData (RVA 0x9bfd00 in RSGame-Win64-Shipping.exe), byte i = this+0x2c0+i:
 *   byte 2 (IoData1): bit 2 = Test (Control_Move_Up), bit 1 = Service (Control_Move_Down), bit 0 = Enter (Control_Enter);
 *                     bit 7 -> [GI+0x2a4] (iIOState), bit 6 -> [GI+0x2a8] (bResetFlag): beide 0.
 *   byte 5 (IoData4): bit 7/6/5/4 = trekker speler 1/2/3/4 (FPACKET.bLeftBtn; de opgaande flank roept ABPPC::StartPlay(speler) aan);
 *             bit 3/2/1 = waterstand laag/midden/hoog (CheckIsOpenWaterTank, RVA 0x9ab780: zonder een van de drie meldt de game een lege
 *                     tank; GEGOKT dat dit de goede lezing is: hier staat "hoog" aan, tenzij water = 0 in rg-unis.ini);
 *   byte 7 (IoData6): muntingangen: die telt de DLL zelf (BIND_GetCoinNumByPlayer), ParseIOData slaat deze byte over;
 *   byte 10..25: ruwe 16-bits aanwijspunten big-endian X,Y van speler 1..4 (FPACKET.iOriginal_X/Y), omgerekend door GunDataToScreen
 *                (RVA 0x9b3110) met het 4x4-rooster van Setting_Hardware.ini;
 *   byte 48: Crc = ~som(byte 1..47) (zo controleert het origineel zijn eigen rapport; de game leest hem niet).
 * De kalibratie van de eigenaar (Setting_Hardware.ini: Calibrate_1P_n_.. = 4112 = 16*257 ... 61423 = 239*257) is gemaakt met ruwe
 * punten b*257 (hoog byte = laag byte): daarom hier ook b*257. */
void rgu_bouw_rr(const rgu_cfg *c, const rgu_beeld *b, uint8_t o[RGU_RR_IO_LEN])
{
    memset(o, 0, RGU_RR_IO_LEN);
    o[0] = (uint8_t)c->usbio;
    o[1] = 0xAA;
    o[2] = (uint8_t)((rgu_aan(c, RGU_TEST, b->bits) << 2) | (rgu_aan(c, RGU_SERVICE, b->bits) << 1) | rgu_aan(c, RGU_ENTER, b->bits));
    o[5] = (uint8_t)((rgu_aan(c, RGU_TRIG1, b->bits) << 7) | (rgu_aan(c, RGU_TRIG2, b->bits) << 6) |
                     (rgu_aan(c, RGU_TRIG3, b->bits) << 5) | (rgu_aan(c, RGU_TRIG4, b->bits) << 4));
    if (c->water) o[5] |= 0x02;
    for (int i = 0; i < 8; i++) {
        o[10 + 2 * i] = b->as[i];
        o[11 + 2 * i] = b->as[i];
    }
    o[48] = crc_van(o, 1, 48);
}

/* Elevator Action Invasion, ParseIOData (RVA 0x792410 in ESGame-Win64-Shipping.exe), byte i = this+0x2cc+i (FESIoData):
 *   byte 2 (IoData2): bit 0 = Enter (Control_Enter), bit 1 = omlaag, bit 2 = omhoog, bit 5 = start speler 1 (StartPlay(0)),
 *                     bit 4 = start speler 2 (StartPlay(1));
 *   byte 3/4 (IoData3/4): bit 7 en 6 en het lage nibble zijn deursensoren (Infrared/Door): 0;
 *   byte 5..12: ruwe aanwijspunten (alleen als USettingData+0x144 == 1, "ES-geweren"; anders komt het punt uit BIND_GetGunData);
 *   byte 13 (IoData13): bit 1 / bit 0 = trekker speler 1 / 2 in die ES-modus.
 * De Crc is GEGOKT (zelfde som als bij Raccoon); de game leest hem niet. */
void rgu_bouw_eai_es(const rgu_cfg *c, const rgu_beeld *b, uint8_t o[RGU_EAI_ES_LEN])
{
    memset(o, 0, RGU_EAI_ES_LEN);
    o[0] = 0xAA;
    o[2] = (uint8_t)((rgu_aan(c, RGU_START1, b->bits) << 5) | (rgu_aan(c, RGU_START2, b->bits) << 4) |
                     ((rgu_aan(c, RGU_UP, b->bits)) << 2) | (rgu_aan(c, RGU_DOWN, b->bits) << 1) | rgu_aan(c, RGU_ENTER, b->bits));
    for (int i = 0; i < 4; i++) {          /* P1X P1Y P2X P2Y als 16-bits big-endian */
        o[5 + 2 * i] = b->as[i];
        o[6 + 2 * i] = b->as[i];
    }
    o[13] = (uint8_t)((rgu_aan(c, RGU_TRIG1, b->bits) << 1) | rgu_aan(c, RGU_TRIG2, b->bits));
    o[15] = crc_van(o, 0, 15);
}

void rgu_eai_punt(const rgu_cfg *c, const rgu_beeld *b, int p, int spel_b, int spel_h, int *x, int *y, int *buiten)
{
    int ax = b->as[2 * p], ay = b->as[2 * p + 1];
    double fx = ax / 255.0, fy = ay / 255.0;
    double gx, gy;
    if (c->aim_kolom && spel_h > 0 && spel_b > 0) {
        /* Het spelbeeld (spel_b x spel_h) wordt met behoud van verhouding in de uitvoer (uit_b x uit_h) gezet: een kolom. */
        double kb = (double)c->uit_h * spel_b / spel_h;            /* breedte van de kolom in uitvoerpunten */
        double links = ((double)c->uit_b - kb) / 2.0;
        gx = (fx * c->uit_b - links) * spel_b / kb;
    } else {
        gx = fx * spel_b;
    }
    gy = fy * spel_h;
    *x = (int)(gx + (gx >= 0 ? 0.5 : -0.5));
    *y = (int)(gy + 0.5);
    /* De regel van de kern: een as <= 1 of >= 254 is buiten beeld (packaging/tp-shm-profiles.json). */
    *buiten = (ax <= 1 || ax >= 254 || ay <= 1 || ay >= 254 || *x < 0 || *x >= spel_b);
}

static void zet32(uint8_t *p, int off, int32_t v) { memcpy(p + off, &v, 4); }

/* FPACKET (PDB van ESGame): iPlayer 0, iOriginal_X/Y 4/8, iCalibrate_X/Y 12/16, iX 20, iY 24, iUSB 52, LeftTop 56, Middle 64,
 * RightBotton 72, bOutRange 80, bLeftBtn 81, bIsHandleVerify 89. De Tick van de game eist iUSB >= 0 en leest iX/iY (klem 70..1010 en
 * 70..1850) en bLeftBtn (OnFireDown/OnMouseUp). IsGunCalibrate (RVA 0x773620) wil een Middle en RightBotton die niet 0 zijn en niet
 * gelijk aan elkaar of aan LeftTop: daarom drie vaste, verschillende punten. */
void rgu_eai_pakket(const rgu_cfg *c, const rgu_beeld *b, int p, int spel_b, int spel_h, uint8_t k[RGU_PAKKET_LEN])
{
    int x, y, buiten;
    rgu_eai_punt(c, b, p, spel_b, spel_h, &x, &y, &buiten);
    zet32(k, 0, p);
    zet32(k, 4, b->as[2 * p] * 257);
    zet32(k, 8, b->as[2 * p + 1] * 257);
    zet32(k, 12, x);
    zet32(k, 16, y);
    zet32(k, 20, x);
    zet32(k, 24, y);
    zet32(k, 52, 1);
    zet32(k, 56, spel_b / 10);   zet32(k, 60, spel_h / 10);
    zet32(k, 64, spel_b / 2);    zet32(k, 68, spel_h / 2);
    zet32(k, 72, spel_b - spel_b / 10); zet32(k, 76, spel_h - spel_h / 10);
    k[80] = (uint8_t)buiten;
    k[81] = (uint8_t)rgu_aan(c, RGU_TRIG1 + p, b->bits);
    k[89] = 1;
}
