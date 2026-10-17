/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest voor tools/rg-pqmt/pqmt_logic.h: trekker + richtpunt uit het gedeelde geheugen naar aanraakpunten (neer, beweegt, op)
 * voor de nagebootste PQMTClient.dll van Infinity Blade FX. Gebouwd door tests/pqmt_test.sh.
 */
#include <stdio.h>

#include "pqmt_logic.h"

static int fouten;
#define CHECK(c, ...) do { if (!(c)) { fouten++; printf("FAIL (regel %d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define TRK 0x10u

int main(void) {
    pq_toestand t = {0, 0, 0, 0};
    pq_punt p;
    int32_t f;

    CHECK(sizeof(pq_punt) == 16, "TouchPoint is 16 bytes (4 dwords, zoals de operator= van PQMTClient.dll)");

    /* pixels: randen en midden */
    CHECK(pq_pixels(0, 1920) == 0, "links");
    CHECK(pq_pixels(255, 1920) == 1919, "rechts");
    CHECK(pq_pixels(128, 1920) == 963, "midden (kreeg %d)", (int)pq_pixels(128, 1920));
    CHECK(pq_pixels(10, 1) == 0, "te kleine rechthoek geeft 0");

    /* zonder trekker: geen frame */
    CHECK(pq_stap(&t, 0, TRK, 128, 128, 1920, 1080, &p, &f) == 0, "geen trekker, geen frame");

    /* trekker buiten beeld: geen aanraking (offscreen-regel 2..253) */
    CHECK(pq_stap(&t, TRK, TRK, 255, 128, 1920, 1080, &p, &f) == 0, "buiten beeld: niet aanraken");
    CHECK(pq_stap(&t, TRK, TRK, 128, 1, 1920, 1080, &p, &f) == 0, "buiten beeld (y=1): niet aanraken");

    /* neer */
    CHECK(pq_stap(&t, TRK, TRK, 128, 64, 1920, 1080, &p, &f) == 1 && p.gebeurtenis == PQ_NEER, "neer");
    CHECK(p.x == 963 && p.y == pq_pixels(64, 1080) && f == 1, "neer op de plek (x %d y %d frame %d)", (int)p.x, (int)p.y, (int)f);
    /* vasthouden zonder te bewegen: geen frame */
    CHECK(pq_stap(&t, TRK, TRK, 128, 64, 1920, 1080, &p, &f) == 0, "stil vasthouden: geen frame");
    /* bewegen */
    CHECK(pq_stap(&t, TRK, TRK, 200, 64, 1920, 1080, &p, &f) == 1 && p.gebeurtenis == PQ_BEWEEGT && p.x == pq_pixels(200, 1920) && f == 2,
          "beweegt");
    /* loslaten: op, op de laatste plek */
    CHECK(pq_stap(&t, 0, TRK, 10, 10, 1920, 1080, &p, &f) == 1 && p.gebeurtenis == PQ_OP, "op");
    CHECK(p.x == pq_pixels(200, 1920) && p.y == pq_pixels(64, 1080) && f == 3, "op op de laatste plek");
    CHECK(pq_stap(&t, 0, TRK, 10, 10, 1920, 1080, &p, &f) == 0, "daarna niets meer");

    /* van het scherm af terwijl de trekker vastgehouden wordt: op, en niet opnieuw neer zolang buiten beeld */
    CHECK(pq_stap(&t, TRK, TRK, 100, 100, 1920, 1080, &p, &f) == 1 && p.gebeurtenis == PQ_NEER, "neer (2e keer)");
    CHECK(pq_stap(&t, TRK, TRK, 254, 100, 1920, 1080, &p, &f) == 1 && p.gebeurtenis == PQ_OP, "buiten beeld: op");
    CHECK(pq_stap(&t, TRK, TRK, 254, 100, 1920, 1080, &p, &f) == 0, "buiten beeld: niets");
    CHECK(pq_stap(&t, TRK, TRK, 100, 100, 1920, 1080, &p, &f) == 1 && p.gebeurtenis == PQ_NEER, "terug in beeld: weer neer");

    /* een ander bit dan de aanrakingsknop telt niet */
    {
        pq_toestand u = {0, 0, 0, 0};
        CHECK(pq_stap(&u, 0x08 | 0x20 | 0x40, TRK, 100, 100, 1920, 1080, &p, &f) == 0, "munt/gunknop/pedaal raken niet aan");
    }

    if (fouten) { printf("%d fouten\n", fouten); return 1; }
    printf("pqmt_unit: ok\n");
    return 0;
}
