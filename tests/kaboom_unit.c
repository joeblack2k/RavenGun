/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest van de rekenkern van rg-kaboom (tools/rg-kaboom/rg_kaboom_kern.c): de tabel lezen en afkeuren, het voorbeeld is de
 * standaard, en van bitveld naar paneel, spelersknoppen, muntentellers en GetReadInputAdrio5. Geen Windows nodig.
 *
 *   kaboom_unit VOORBEELD.ini [TABEL.ini ...]     de extra tabellen (patches/<Game>/dropin/rg-kaboom.ini) moeten geldig zijn
 */
#include "rg_kaboom_kern.h"

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

int main(int argc, char **argv)
{
    char fout[200];
    rgk_tabel std, t;
    rgk_standaard(&std);

    /* 1. Het voorbeeld is precies de standaard. */
    if (argc > 1) {
        char *tekst = lees(argv[1]);
        EIS(tekst != NULL, "voorbeeld %s niet te lezen", argv[1]);
        if (tekst) {
            EIS(rgk_lees_tabel(tekst, &t, fout, sizeof fout) == 0, "voorbeeld afgekeurd: %s", fout);
            for (int i = 0; i < RGK_AANTAL; i++) {
                EIS(t.bron[i].n == std.bron[i].n, "%s: %d bits in het voorbeeld, %d in de standaard", rgk_naam(i), t.bron[i].n, std.bron[i].n);
                for (int k = 0; k < t.bron[i].n && k < std.bron[i].n; k++)
                    EIS(t.bron[i].bit[k] == std.bron[i].bit[k], "%s: bit %d verschilt", rgk_naam(i), k);
            }
        }
    }
    /* 2. De meegeleverde tabellen zijn geldig. */
    for (int a = 2; a < argc; a++) {
        char *tekst = lees(argv[a]);
        EIS(tekst != NULL, "%s niet te lezen", argv[a]);
        if (tekst) EIS(rgk_lees_tabel(tekst, &t, fout, sizeof fout) == 0, "%s afgekeurd: %s", argv[a], fout);
    }

    /* 3. Afkeuren: alles of niets, met een reden. */
    const char *slecht[] = {"p1.omhoog = 7\nonzin = 3\n", "p1.omhoog = 32\n", "p1.omhoog = -1\n", "p1.omhoog 7\n", "p1.omhoog = 7\np1.omhoog = 8\n",
                            "p1.omhoog = 7x\n", "p1.omhoog = 1,2,3,4,5,6,7\n", "p1.omhoog = \n"};
    for (size_t i = 0; i < sizeof slecht / sizeof *slecht; i++) {
        rgk_tabel x;
        memset(&x, 0x5a, sizeof x);
        fout[0] = 0;
        EIS(rgk_lees_tabel(slecht[i], &x, fout, sizeof fout) != 0, "niet afgekeurd: %s", slecht[i]);
        EIS(fout[0] != 0, "afgekeurd zonder reden: %s", slecht[i]);
        EIS(((unsigned char *)&x)[0] == 0x5a, "afgekeurde tabel is toch (half) geschreven: %s", slecht[i]);
    }
    EIS(rgk_lees_tabel("; alleen commentaar\n\n# ook\n munt1 = 5 ; met commentaar erachter\r\n", &t, fout, sizeof fout) == 0, "%s", fout);
    EIS(t.bron[RGK_MUNT1].n == 1 && t.bron[RGK_MUNT1].bit[0] == 5, "munt1 met commentaar en CRLF");
    EIS(t.bron[RGK_P1_OMHOOG].n == 0, "een naam die er niet staat is leeg");

    /* 4. Van bitveld naar het bord (standaard = Rampage). */
    rgk_staat s;
    memset(&s, 0, sizeof s);
    int v = rgk_stap(&std, &s, 0);
    EIS(v == (RGK_VERANDERD_TOETSEN | RGK_VERANDERD_KNOPPEN), "eerste stap meldt paneel en knoppen (v=%d)", v);
    EIS(s.toetsen == 0 && s.knoppen == 0 && s.munt[0] == 0, "rust");
    v = rgk_stap(&std, &s, 0);
    EIS(v == 0, "geen verandering, geen melding (v=%d)", v);

    /* aanval speler 1 (bit 4) = paneel omlaag (2); start speler 1 (bit 6) ook */
    v = rgk_stap(&std, &s, b(4));
    EIS(s.toetsen == 2 && v == RGK_VERANDERD_TOETSEN, "aanval 1 -> omlaag (toetsen=%u v=%d)", s.toetsen, v);
    rgk_stap(&std, &s, b(6));
    EIS(s.toetsen == 2, "start 1 -> omlaag (toetsen=%u)", s.toetsen);
    rgk_stap(&std, &s, b(0) | b(1) | b(23));
    EIS(s.toetsen == (1 | 4 | 8), "test/service/aanval 3 -> enter/kies/omhoog (toetsen=%u)", s.toetsen);

    /* spelersknoppen: speler 1 omhoog = 2, omlaag = 1, links = 4, rechts = 8; speler 2 x16; speler 3 x256 */
    rgk_stap(&std, &s, b(7));
    EIS(s.knoppen == 2, "p1 omhoog (knoppen=%u)", s.knoppen);
    rgk_stap(&std, &s, b(8) | b(9) | b(10));
    EIS(s.knoppen == (1 | 4 | 8), "p1 omlaag/links/rechts (knoppen=%u)", s.knoppen);
    rgk_stap(&std, &s, b(12) | b(20));
    EIS(s.knoppen == (0x20 | 0x800), "p2 omhoog, p3 rechts (knoppen=%#x)", s.knoppen);
    EIS(s.sknop == 0, "geen knop.* in de standaard");

    /* munten: een teller per deur, alleen op de opgaande flank; P1 = bit 5 = deur 1, P2 = bit 2 = deur 2, P3 = bit 3 = deur 3 */
    rgk_stap(&std, &s, 0);
    v = rgk_stap(&std, &s, b(5));
    EIS(s.munt[0] == 1 && (v & RGK_VERANDERD_MUNT), "munt deur 1");
    v = rgk_stap(&std, &s, b(5));
    EIS(s.munt[0] == 1 && !(v & RGK_VERANDERD_MUNT), "vasthouden telt niet");
    rgk_stap(&std, &s, 0);
    rgk_stap(&std, &s, b(5) | b(2) | b(3));
    EIS(s.munt[0] == 2 && s.munt[1] == 1 && s.munt[2] == 1 && s.munt[3] == 0, "drie deuren tegelijk (%u %u %u %u)", s.munt[0], s.munt[1], s.munt[2], s.munt[3]);
    rgk_munt_terug(&s, 0x0C);   /* deur 2: in1 + in2 */
    EIS(s.munt[0] == 2 && s.munt[1] == 0 && s.munt[2] == 1, "terugzetten van deur 2 alleen");
    rgk_munt_terug(&s, 0xFF);
    EIS(s.munt[0] == 0 && s.munt[2] == 0, "alles terug");

    /* de eerste stap telt geen munt (een ingedrukte knop bij de start is geen flank) */
    rgk_staat s2;
    memset(&s2, 0, sizeof s2);
    rgk_stap(&std, &s2, b(5));
    EIS(s2.munt[0] == 0, "eerste stap zonder munt");

    /* knop.* naar sButton */
    EIS(rgk_lees_tabel("knop.p1b1 = 7\nknop.p4b2 = 25\n", &t, fout, sizeof fout) == 0, "%s", fout);
    rgk_staat s3;
    memset(&s3, 0, sizeof s3);
    rgk_stap(&t, &s3, b(7) | b(25));
    EIS(s3.sknop == (1 | 0x80), "sButton p1b1 en p4b2 (%#x)", s3.sknop);

    if (fouten) { printf("%d fout(en)\n", fouten); return 1; }
    printf("ok: rg-kaboom (tabel, paneel, knoppen, munten, sButton)\n");
    return 0;
}
