/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest van de rekenkern van rg-unis (tools/rg-unis/rg_unis_kern.c): de tabel lezen en afkeuren, muntflanken, en van
 * bordbeeld naar de bytes die Raccoon Rampage (FCoUnisIoData, 49 bytes) en Elevator Action Invasion (FESIoData, 16 bytes; FPACKET,
 * 92 bytes) uit hun PDB's verwachten. Geen Windows nodig.
 *
 *   unis_unit [TABEL.ini ...]     de extra tabellen (docs/research/wartran/patches/<Game>/dropin/rg-unis.ini) moeten geldig zijn
 */
#include "rg_unis_kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fouten;
#define EIS(c, ...) do { if (!(c)) { fouten++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *lees(const char *pad)
{
    FILE *f = fopen(pad, "rb");
    if (!f) return NULL;
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static uint32_t b(int bit) { return 1u << bit; }
static int32_t i32(const uint8_t *p, int off) { int32_t v; memcpy(&v, p + off, 4); return v; }

static uint8_t crc(const uint8_t *o, int van, int tot)
{
    uint8_t s = 0;
    for (int i = van; i < tot; i++) s = (uint8_t)(s + o[i]);
    return (uint8_t)~s;
}

int main(int argc, char **argv)
{
    char fout[200];
    rgu_cfg rr, eai, t;
    rgu_standaard(&rr, RGU_GAME_RR);
    rgu_standaard(&eai, RGU_GAME_EAI);

    /* 1. De tabel: een geldig bestand, en elke fout wordt afgekeurd zonder de configuratie aan te raken. */
    t = rr;
    EIS(rgu_lees_ini("; commentaar\ngame = rr\nusbio = 3\nenter = 9, 10\naim.mode = column\naim.out = 1280x720\n", &t, fout, sizeof fout) == 0, "geldig bestand: %s", fout);
    EIS(t.usbio == 3 && t.bron[RGU_ENTER].n == 2 && t.bron[RGU_ENTER].bit[1] == 10 && t.aim_kolom == 1 && t.uit_b == 1280 && t.uit_h == 720, "waarden niet overgenomen");
    EIS(t.bron[RGU_TEST].n == 1 && t.bron[RGU_TEST].bit[0] == 0, "een niet genoemde sleutel houdt zijn standaard");
    const char *slecht[] = {"nonsens = 1\n", "enter = 32\n", "enter = x\n", "enter = 1\nenter = 2\n", "enter\n", "game = pacman\n",
                            "aim.out = 10x10\n", "aim.mode = rond\n", "usbio = 300\n", "enter = \n", "trig1 = 1,2,3,4,5,6,7\n"};
    for (size_t i = 0; i < sizeof slecht / sizeof *slecht; i++) {
        t = rr;
        EIS(rgu_lees_ini(slecht[i], &t, fout, sizeof fout) != 0, "slecht bestand %zu is toch goedgekeurd", i);
        EIS(memcmp(&t, &rr, sizeof t) == 0, "slecht bestand %zu heeft de configuratie veranderd", i);
    }

    t = rr;
    EIS(rgu_lees_ini("water = 0\n", &t, fout, sizeof fout) == 0 && t.water == 0, "water = 0");
    EIS(rgu_lees_ini("water = 2\n", &t, fout, sizeof fout) != 0, "water = 2 hoort afgekeurd");

    /* 2. Muntflanken per speler; de eerste stap telt niet; vasthouden telt een keer. */
    rgu_staat s;
    memset(&s, 0, sizeof s);
    rgu_stap(&rr, &s, b(6));                 /* de knop staat al aan bij de eerste stap: geen munt */
    EIS(s.munt[0] == 0, "eerste stap telt geen munt");
    rgu_stap(&rr, &s, 0);
    rgu_stap(&rr, &s, b(6));
    rgu_stap(&rr, &s, b(6));
    EIS(s.munt[0] == 1, "munt 1 na een flank: %d", s.munt[0]);
    rgu_stap(&rr, &s, b(11) | b(16) | b(21));
    EIS(s.munt[1] == 1 && s.munt[2] == 1 && s.munt[3] == 1 && s.munt[0] == 1, "munten 2..4: %d %d %d %d", s.munt[0], s.munt[1], s.munt[2], s.munt[3]);
    rgu_stap(&rr, &s, 0);
    rgu_stap(&rr, &s, b(3));                 /* de muntknop van de kast telt als munt 2 (Raccoon, gegokt) */
    EIS(s.munt[1] == 2, "muntknop 2");

    /* 3. Raccoon Rampage: FCoUnisIoData, 49 bytes. */
    rgu_beeld be;
    memset(&be, 0, sizeof be);
    uint8_t rrio[RGU_RR_IO_LEN];
    rgu_bouw_rr(&rr, &be, rrio);
    EIS(rrio[0] == 1 && rrio[1] == 0xAA, "UsbIo/Mark: %02x %02x", rrio[0], rrio[1]);
    EIS(rrio[2] == 0 && rrio[5] == 0x0c, "rest: no buttons, water tank OK = low + middle on, high off (byte 5: %02x)", rrio[5]);
    EIS(rrio[48] == crc(rrio, 1, 48), "Crc");
    be.bits = b(0) | b(1) | b(4);
    rgu_bouw_rr(&rr, &be, rrio);
    EIS(rrio[2] == 7, "test=bit2, service=bit1, enter=bit0 (F4): %02x", rrio[2]);
    be.bits = b(7) | b(22);
    rgu_bouw_rr(&rr, &be, rrio);
    EIS(rrio[5] == (0x80 | 0x10 | 0x0c), "trekker 1 en 4: %02x", rrio[5]);
    be.bits = 0;
    for (int i = 0; i < 8; i++) be.as[i] = (uint8_t)(10 + i * 30);
    rgu_bouw_rr(&rr, &be, rrio);
    for (int i = 0; i < 8; i++)
        EIS(rrio[10 + 2 * i] == be.as[i] && rrio[11 + 2 * i] == be.as[i], "as %d: %02x%02x", i, rrio[10 + 2 * i], rrio[11 + 2 * i]);
    EIS(rrio[48] == crc(rrio, 1, 48), "Crc met assen");
    be.as[0] = 128;
    rgu_bouw_rr(&rr, &be, rrio);
    EIS(((rrio[10] << 8) | rrio[11]) == 128 * 257, "ruw punt = b*257 (de kalibratie van de eigenaar is zo gemaakt)");

    /* 4. Elevator Action Invasion: FESIoData, 16 bytes. */
    uint8_t es[RGU_EAI_ES_LEN];
    memset(&be, 0, sizeof be);
    rgu_bouw_eai_es(&eai, &be, es);
    EIS(es[0] == 0xAA && es[2] == 0 && es[13] == 0, "rust");
    be.bits = b(6) | b(11);
    rgu_bouw_eai_es(&eai, &be, es);
    EIS(es[2] == (0x20 | 0x10), "start 1 = bit 5, start 2 = bit 4: %02x", es[2]);
    be.bits = b(0) | b(4) | b(5);
    rgu_bouw_eai_es(&eai, &be, es);
    EIS(es[2] == 7, "enter bit 0, omlaag bit 1, omhoog bit 2: %02x", es[2]);
    be.bits = b(7) | b(12);
    rgu_bouw_eai_es(&eai, &be, es);
    EIS(es[13] == 3, "trekkers in IoData13: %02x", es[13]);

    /* 5. EAI: het aanwijspunt. Kolom: het spelbeeld 1080x1920 staat in 1920x1080 als kolom van 607,5 breed (links 656,25). */
    memset(&be, 0, sizeof be);
    int x, y, buiten;
    be.as[0] = 128; be.as[1] = 128;
    rgu_eai_punt(&eai, &be, 0, 1080, 1920, &x, &y, &buiten);
    EIS(x > 530 && x < 560 && y > 950 && y < 970 && !buiten, "midden van de kolom: %d,%d buiten=%d", x, y, buiten);
    be.as[0] = 40;   /* links van de kolom */
    rgu_eai_punt(&eai, &be, 0, 1080, 1920, &x, &y, &buiten);
    EIS(buiten, "links van de kolom is buiten beeld (x=%d)", x);
    be.as[0] = 0;
    rgu_eai_punt(&eai, &be, 0, 1080, 1920, &x, &y, &buiten);
    EIS(buiten, "as 0 is buiten beeld");
    t = eai;
    EIS(rgu_lees_ini("aim.mode = full\n", &t, fout, sizeof fout) == 0, "aim.mode full");
    be.as[0] = 255 / 2; be.as[1] = 255 / 4;
    rgu_eai_punt(&t, &be, 0, 1080, 1920, &x, &y, &buiten);
    EIS(x > 525 && x < 545 && y > 470 && y < 490 && !buiten, "volle breedte: %d,%d", x, y);

    /* 6. EAI: FPACKET. */
    uint8_t pk[RGU_PAKKET_LEN];
    memset(pk, 0xEE, sizeof pk);
    be.as[0] = 128; be.as[1] = 128; be.as[2] = 128; be.as[3] = 128;
    be.bits = b(12);
    rgu_eai_pakket(&eai, &be, 1, 1080, 1920, pk);
    EIS(i32(pk, 0) == 1, "iPlayer");
    EIS(i32(pk, 52) == 1, "iUSB >= 0 (de Tick van de game eist het)");
    EIS(pk[89] == 1, "bIsHandleVerify");
    EIS(pk[81] == 1, "bLeftBtn van speler 2");
    EIS(pk[80] == 0, "bOutRange");
    EIS(i32(pk, 20) == i32(pk, 12) && i32(pk, 24) == i32(pk, 16), "iX/iY = iCalibrate");
    EIS(i32(pk, 20) > 530 && i32(pk, 20) < 560, "iX midden: %d", i32(pk, 20));
    /* IsGunCalibrate: Middle en RightBotton niet nul, niet gelijk aan elkaar of aan LeftTop */
    EIS((i32(pk, 64) || i32(pk, 68)) && (i32(pk, 72) || i32(pk, 76)), "Middle/RightBotton niet nul");
    EIS(!(i32(pk, 64) == i32(pk, 56) && i32(pk, 68) == i32(pk, 60)), "Middle != LeftTop");
    EIS(!(i32(pk, 72) == i32(pk, 64) && i32(pk, 76) == i32(pk, 68)), "RightBotton != Middle");
    EIS(pk[90] == 0xEE, "een veld dat het bord niet invult blijft zoals de game hem liet");

    /* 7. De meegeleverde tabellen. */
    for (int i = 1; i < argc; i++) {
        char *tekst = lees(argv[i]);
        EIS(tekst != NULL, "tabel %s niet te lezen", argv[i]);
        if (!tekst) continue;
        rgu_cfg u;
        rgu_standaard(&u, RGU_GAME_EAI);
        EIS(rgu_lees_ini(tekst, &u, fout, sizeof fout) == 0, "tabel %s: %s", argv[i], fout);
        /* Een meegeleverde tabel is de standaard van zijn game, expliciet gemaakt (net als de DLL doet: eerst de standaard van de game
         * uit het bestand, dan het bestand er overheen). */
        rgu_cfg ref, v;
        rgu_standaard(&ref, u.game);
        v = ref;
        rgu_lees_ini(tekst, &v, fout, sizeof fout);
        int gelijk = v.game == ref.game && v.usbio == ref.usbio && v.water == ref.water && v.aim_kolom == ref.aim_kolom &&
                     v.uit_b == ref.uit_b && v.uit_h == ref.uit_h;
        for (int k = 0; k < RGU_AANTAL; k++) {
            gelijk = gelijk && v.bron[k].n == ref.bron[k].n;
            for (int m = 0; m < v.bron[k].n && m < RGU_MAX_BRON; m++) gelijk = gelijk && v.bron[k].bit[m] == ref.bron[k].bit[m];
        }
        EIS(gelijk, "tabel %s wijkt af van de standaard van game %d", argv[i], u.game);
    }

    if (fouten) { printf("%d fout(en)\n", fouten); return 1; }
    printf("unis_unit: alles goed\n");
    return 0;
}
