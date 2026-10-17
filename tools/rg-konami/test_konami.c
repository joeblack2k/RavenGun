/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest voor de logica van rg-konami (konami_logic.h), op Linux met de gewone gcc. Draait zonder game, zonder Wine en zonder
 * bordbeeld. Met "--schrijf-iosav PAD" schrijft hij ook de io.sav voor Lethal Enforcers 3 (de drop-in SAVE/io.sav).
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -o test_konami tools/rg-konami/test_konami.c -lm && ./test_konami
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "konami_logic.h"

static int fouten, getest;

#define CHECK(c, ...)                                                       \
    do {                                                                    \
        getest++;                                                           \
        if (!(c)) {                                                         \
            fouten++;                                                       \
            fprintf(stderr, "FOUT %s:%d: %s -- ", __FILE__, __LINE__, #c); \
            fprintf(stderr, __VA_ARGS__);                                   \
            fputc('\n', stderr);                                            \
        }                                                                   \
    } while (0)

static void zet(uint8_t *shm, uint32_t bits, const uint8_t as[8])
{
    memset(shm, 0, 64);
    shm[8] = (uint8_t)bits; shm[9] = (uint8_t)(bits >> 8); shm[10] = (uint8_t)(bits >> 16); shm[11] = (uint8_t)(bits >> 24);
    for (int i = 0; i < 8; i++) shm[12 + i] = as[i];
}

static uint32_t lees32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float leesf(const uint8_t *p) { uint32_t v = lees32(p); float f; memcpy(&f, &v, 4); return f; }

static void test_le3_digital_in(void)
{
    uint8_t shm[64], uit[LE3_DIN_GROOTTE];
    const uint8_t midden[8] = {128, 128, 128, 128, 128, 128, 128, 128};
    le3_iob_staat s;
    memset(&s, 0, sizeof s);
    kb_beeld b;

    zet(shm, 0, midden);
    kb_lees(shm, &b);
    le3_digital_in(&s, &b, uit);
    CHECK(lees32(uit) == 1, "geldig = %u", lees32(uit));
    for (int i = 4; i < LE3_DIN_GROOTTE; i++) CHECK(uit[i] == 0, "byte 0x%x = %u in rust", i, uit[i]);

    /* TEST, SERVICE, START P1, TREKKER P2, OPTION P1 ingedrukt. */
    zet(shm, 1u << KB_TEST | 1u << KB_SERVICE | 1u << KB_START(0) | 1u << KB_TREKKER(1) | 1u << KB_HERLADEN(0), midden);
    kb_lees(shm, &b);
    le3_digital_in(&s, &b, uit);
    CHECK(uit[0x04] == 1 && uit[0x07] == 1, "test %u service %u", uit[0x04], uit[0x07]);
    CHECK(uit[0x08] == 1 && uit[0x09] == 0, "start %u %u", uit[0x08], uit[0x09]);
    CHECK(uit[0x0a] == 0 && uit[0x0b] == 1, "trekker %u %u", uit[0x0a], uit[0x0b]);
    CHECK(uit[0x0c] == 1 && uit[0x0d] == 0, "option %u %u", uit[0x0c], uit[0x0d]);
    CHECK(uit[0x0e] == 1 && uit[0x11] == 1 && uit[0x12] == 1, "tellers %u %u %u", uit[0x0e], uit[0x11], uit[0x12]);

    /* Vasthouden telt niet opnieuw; loslaten ook niet. */
    le3_digital_in(&s, &b, uit);
    CHECK(uit[0x0e] == 1 && uit[0x11] == 1, "vasthouden telde mee: %u %u", uit[0x0e], uit[0x11]);
    zet(shm, 0, midden);
    kb_lees(shm, &b);
    le3_digital_in(&s, &b, uit);
    CHECK(uit[0x0e] == 1 && uit[0x08] == 0, "loslaten: teller %u niveau %u", uit[0x0e], uit[0x08]);

    /* Munten: 6 bits, 70 munten = 70 mod 64 = 6; nooit >= 0x40 (game.exe 0x51db80 zou dan blijven hangen). */
    for (int i = 0; i < 70; i++) {
        zet(shm, 1u << (i % 2 ? KB_MUNT2 : KB_MUNT1), midden);
        kb_lees(shm, &b);
        le3_digital_in(&s, &b, uit);
        CHECK(uit[0x26] < 0x40, "munt %u", uit[0x26]);
        zet(shm, 0, midden);
        kb_lees(shm, &b);
        le3_digital_in(&s, &b, uit);
    }
    CHECK(uit[0x26] == 70 % 64, "munten %u", uit[0x26]);

    /* Geen bordbeeld: alles los, maar wel geldig. */
    kb_lees(NULL, &b);
    le3_digital_in(&s, &b, uit);
    CHECK(lees32(uit) == 1 && uit[0x08] == 0 && uit[0x0a] == 0, "zonder bordbeeld");
}

static void test_le3_iosav_en_richten(void)
{
    /* De CRC: de io.sav op de NAS (middelpunt 0, schalen 192/176, gemaakt met de kern van TeknoParrot) heeft controle1 0xa9e5c7e1 en
     * controle2 0xf0aabc2c. Dezelfde gegevens hier opgebouwd moeten dezelfde controles geven. */
    uint8_t g[0x3c];
    memset(g, 0, sizeof g);
    kb_zet32(g + 4, 1);
    kb_zet32(g + 8, 1);
    for (int p = 0; p < 2; p++) {
        uint8_t *s = g + 0x0c + 0x18 * p;
        kb_zetf(s + 0x08, 192.0f); kb_zetf(s + 0x0c, 176.0f); kb_zetf(s + 0x10, 192.0f); kb_zetf(s + 0x14, 176.0f);
    }
    const uint32_t c1 = le3_crc(g, sizeof g);
    uint8_t c1b[4];
    kb_zet32(c1b, c1);
    CHECK(c1 == 0xa9e5c7e1u, "controle1 0x%08x", c1);
    CHECK(le3_crc(c1b, 4) == 0xf0aabc2cu, "controle2 0x%08x", le3_crc(c1b, 4));

    uint8_t sav[LE3_IOSAV_GROOTTE];
    le3_maak_iosav(sav);
    CHECK(memcmp(sav, "I/OD", 4) == 0 && lees32(sav + 0x10) == 0x3c, "kop");
    CHECK(lees32(sav + 0x0c) == le3_crc(sav + 0x14, 0x3c), "controle1 van de eigen io.sav");
    uint8_t k[4];
    kb_zet32(k, lees32(sav + 0x0c));
    CHECK(lees32(sav + 0x08) == le3_crc(k, 4), "controle2 van de eigen io.sav");

    /* Wat game.exe 0x51d540 met onze ruwe waarden en deze kalibratie uitrekent: X = 2,5 * as, Y = 1,875 * as, binnen [0, 640) x
     * [0, 480) (anders maakt de game er -1 van), en stijgend met de as. */
    const uint8_t *s1 = sav + 0x14 + 0x0c;
    const int mx = (int)lees32(s1), my = (int)lees32(s1 + 4);
    const float xp = leesf(s1 + 8), yp = leesf(s1 + 12), xm = leesf(s1 + 16), ym = leesf(s1 + 20);
    float vorig_x = -1, vorig_y = -1;
    for (int a = 0; a < 256; a++) {
        uint8_t shm[64];
        const uint8_t as[8] = {(uint8_t)a, (uint8_t)a, (uint8_t)(255 - a), (uint8_t)(255 - a), 0, 0, 0, 0};
        zet(shm, 0, as);
        kb_beeld b;
        kb_lees(shm, &b);
        le3_schot sch;
        le3_gun(&b, 0, &sch);
        const float x = le3_scherm(sch.ruw_x, mx, xp, xm, 320.0f), y = le3_scherm(sch.ruw_y, my, yp, ym, 240.0f);
        CHECK(sch.ruw_x >= 0 && sch.ruw_x <= 255 && sch.ruw_y >= 0 && sch.ruw_y <= 255, "ruw %d %d", sch.ruw_x, sch.ruw_y);
        CHECK(fabsf(x - 2.5f * a) < 3.0f, "as %d: x %.1f", a, x);
        CHECK(fabsf(y - 1.875f * a) < 2.5f, "as %d: y %.1f", a, y);
        CHECK(x >= 0 && x < 640 && y >= 0 && y < 480, "as %d buiten het scherm: %.1f,%.1f", a, x, y);
        CHECK(x > vorig_x && y > vorig_y, "as %d niet stijgend", a);
        vorig_x = x;
        vorig_y = y;
        CHECK(sch.buiten == (a <= 1 || a >= 254), "as %d: buiten %d", a, sch.buiten);
        le3_gun(&b, 1, &sch);
        CHECK(sch.ruw_x == a, "geweer 2: ruw %d bij as %d", sch.ruw_x, 255 - a);
    }

    /* "Reload" (Button3, bit granaat) haalt het geweer van het scherm. */
    uint8_t shm[64];
    const uint8_t midden[8] = {128, 128, 128, 128, 0, 0, 0, 0};
    zet(shm, 1u << KB_GRANAAT(0), midden);
    kb_beeld b;
    kb_lees(shm, &b);
    le3_schot sch;
    le3_gun(&b, 0, &sch);
    CHECK(sch.buiten == 1, "reload P1");
    le3_gun(&b, 1, &sch);
    CHECK(sch.buiten == 0, "reload P1 raakt P2");
}

static void test_gsh_iob(void)
{
    uint8_t shm[64];
    const uint8_t midden[8] = {128, 128, 128, 128, 128, 128, 128, 128};
    gsh_iob_staat s;
    memset(&s, 0, sizeof s);
    kb_beeld b;
    zet(shm, 0, midden);
    kb_lees(shm, &b);
    gsh_iob_stap(&s, &b);
    for (int i = 0; i < GSH_KNOPPEN; i++) CHECK(!gsh_knop(&s, i) && !gsh_knop_trg(&s, i), "knop %d in rust", i);

    zet(shm, 1u << KB_START(0) | 1u << KB_START(1) | 1u << KB_START(2) | 1u << KB_START(3) | 1u << KB_TEST | 1u << KB_SERVICE, midden);
    kb_lees(shm, &b);
    gsh_iob_stap(&s, &b);
    for (int i = 0; i < 6; i++) CHECK(gsh_knop(&s, i) && gsh_knop_trg(&s, i), "knop %d ingedrukt", i);
    for (int i = 6; i < GSH_KNOPPEN; i++) CHECK(!gsh_knop(&s, i), "knop %d hoort 0", i);
    gsh_iob_stap(&s, &b);
    for (int i = 0; i < 6; i++) CHECK(gsh_knop(&s, i) && !gsh_knop_trg(&s, i), "knop %d vast: geen flank", i);
    CHECK(!gsh_knop(&s, -1) && !gsh_knop(&s, 10), "buiten bereik");

    /* De trekker en aux1 zijn geen knoppen van het I/O-bord (de trekker is de worp, ballio). */
    zet(shm, 1u << KB_TREKKER(0) | 1u << KB_HERLADEN(0), midden);
    kb_lees(shm, &b);
    gsh_iob_stap(&s, &b);
    for (int i = 0; i < GSH_KNOPPEN; i++) CHECK(!gsh_knop(&s, i), "trekker gaf knop %d", i);

    for (int i = 0; i < 65; i++) {
        zet(shm, 1u << KB_MUNT1, midden);
        kb_lees(shm, &b);
        gsh_iob_stap(&s, &b);
        zet(shm, 0, midden);
        kb_lees(shm, &b);
        gsh_iob_stap(&s, &b);
    }
    CHECK(s.munt[0] == 1 && s.munt[1] == 0, "munten %u %u", s.munt[0], s.munt[1]);
}

static void test_gsh_bal(void)
{
    uint8_t shm[64];
    gsh_bal_staat s;
    memset(&s, 0, sizeof s);
    kb_beeld b;
    const uint8_t as[8] = {0x00 + 2, 2, 85, 128, 170, 200, 253, 253};
    zet(shm, 0, as);
    kb_lees(shm, &b);
    gsh_bal_stap(&s, &b, 1360, 768);
    CHECK(s.aantal == 0, "geen worp in rust: %d", s.aantal);

    zet(shm, 1u << KB_TREKKER(0) | 1u << KB_TREKKER(1) | 1u << KB_TREKKER(2) | 1u << KB_TREKKER(3), as);
    kb_lees(shm, &b);
    gsh_bal_stap(&s, &b, 1360, 768);
    CHECK(s.aantal == 4, "vier worpen: %d", s.aantal);
    CHECK(s.punt[0].x == 2 * 1359 / 255 && s.punt[0].y == 2 * 767 / 255, "P1 %d,%d", s.punt[0].x, s.punt[0].y);
    CHECK(s.punt[3].x == 253 * 1359 / 255 && s.punt[3].y == 253 * 767 / 255, "P4 %d,%d", s.punt[3].x, s.punt[3].y);
    /* player.dll: speler = x / (breedte / 4). */
    for (int i = 0; i < 4; i++) CHECK(s.punt[i].x / (1360 / 4) == i, "inslag %d valt in kwart %d", i, s.punt[i].x / (1360 / 4));

    gsh_punt buf[20];
    CHECK(gsh_bal_punten(&s, buf, 0x50) == 4, "point_get 0x50");
    CHECK(gsh_bal_punten(&s, buf, 8) == 2, "point_get 8 bytes = 2 punten");
    CHECK(gsh_bal_punten(&s, buf, 0x50) == 4, "point_get verbruikt niet");

    gsh_bal_stap(&s, &b, 1360, 768);
    CHECK(s.aantal == 0, "vasthouden is geen nieuwe worp: %d", s.aantal);

    /* Buiten beeld: geen inslag. */
    zet(shm, 0, as);
    kb_lees(shm, &b);
    gsh_bal_stap(&s, &b, 1360, 768);
    const uint8_t buiten[8] = {255, 128, 0, 128, 128, 1, 128, 254};
    zet(shm, 1u << KB_TREKKER(0) | 1u << KB_TREKKER(1) | 1u << KB_TREKKER(2) | 1u << KB_TREKKER(3), buiten);
    kb_lees(shm, &b);
    gsh_bal_stap(&s, &b, 1360, 768);
    CHECK(s.aantal == 0, "buiten beeld gaf %d inslagen", s.aantal);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--schrijf-iosav") == 0) {
        uint8_t sav[LE3_IOSAV_GROOTTE];
        le3_maak_iosav(sav);
        FILE *f = fopen(argv[2], "wb");
        if (!f || fwrite(sav, 1, sizeof sav, f) != sizeof sav || fclose(f) != 0) { perror(argv[2]); return 1; }
        printf("io.sav geschreven: %s (%zu bytes)\n", argv[2], sizeof sav);
        return 0;
    }
    test_le3_digital_in();
    test_le3_iosav_en_richten();
    test_gsh_iob();
    test_gsh_bal();
    printf("%d controles, %d fout\n", getest, fouten);
    return fouten ? 1 : 0;
}
