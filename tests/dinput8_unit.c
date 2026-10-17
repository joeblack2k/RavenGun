/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest van de rekenkern van rg-dinput8 (tools/rg-dinput8/rg_dinput8_kern.c), native op Linux:
 *   - de standaard, het lezen en het in zijn geheel afkeuren van een tabel;
 *   - van het bordbeeld naar X/Y (met as.x/as.y en met het bereik van de game), buiten beeld, knoppen;
 *   - flanken: vlaggen en tellers alleen op een opgaande flank, niet in de eerste stap;
 *   - de GUID's en een meegeleverde control.cfg;
 *   - elke meegegeven tabel (argumenten) moet geldig zijn; met --exe <pad> <tabel> ook: elke patch klopt met de bytes van die exe.
 *
 * Gebruik: dinput8_unit [--cfg control.cfg] [--exe exe tabel] [tabel ...]
 */
#include "rg_dinput8_kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fouten;
#define CHECK(c, ...) do { if (!(c)) { fouten++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *lees(const char *pad, size_t *n)
{
    FILE *f = fopen(pad, "rb");
    if (!f) return NULL;
    static char buf[1 << 22];
    size_t l = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[l] = 0;
    if (n) *n = l;
    char *kopie = malloc(l + 1);
    memcpy(kopie, buf, l + 1);
    return kopie;
}

static int afgekeurd(const char *tekst, const char *verwacht)
{
    rgd_tabel t;
    char fout[200] = "";
    if (rgd_lees_tabel(tekst, &t, fout, sizeof fout) == 0) { printf("  niet afgekeurd: %s\n", tekst); return 0; }
    if (verwacht && !strstr(fout, verwacht)) { printf("  reden '%s' bevat geen '%s'\n", fout, verwacht); return 0; }
    return 1;
}

static void test_tabel(void)
{
    rgd_tabel t;
    rgd_standaard(&t);
    CHECK(t.spelers == 2 && t.sp[0].x == 12 && t.sp[0].y == 13 && t.sp[1].x == 14 && t.sp[1].y == 15, "standaard assen");
    CHECK(t.sp[0].knop[0].n == 1 && t.sp[0].knop[0].bit[0] == 7 && t.sp[1].knop[1].bit[0] == 13 && t.sp[1].knop[2].bit[0] == 11, "standaard knoppen");

    char fout[200];
    const char *goed =
        "; commentaar\n"
        "spelers = 2\n"
        "p1.x = 12\np1.y = 13\np1.knop0 = 7, 9\np2.x = 0x0E\np2.y = 15\n"
        "as.x = -999 100\nas.y = -799 1000\nbuiten.x = -1000\n"
        "vlag.0x49E870 = 0\nteller.0x460C1C = 2, 3\n"
        "patch.0x40A03F = 01 -> 00\npatch.0x401000 = 6a 01 -> 6A 00  # twee bytes\n";
    CHECK(rgd_lees_tabel(goed, &t, fout, sizeof fout) == 0, "goede tabel afgekeurd: %s", fout);
    CHECK(t.sp[0].knop[0].n == 2 && t.sp[0].knop[0].bit[1] == 9 && t.sp[1].x == 14, "knop/hex-offset");
    CHECK(t.as_gezet[0] && t.as_lo[0] == -999 && t.as_hi[0] == 100 && t.as_gezet[1] && t.as_lo[1] == -799, "as.x/as.y");
    CHECK(t.buiten_gezet[0] && t.buiten[0] == -1000 && !t.buiten_gezet[1], "buiten");
    CHECK(t.nvlag == 1 && t.vlag[0].adres == 0x49E870 && t.nteller == 1 && t.teller[0].bron.n == 2, "vlag/teller");
    CHECK(t.npatch == 2 && t.patch[0].adres == 0x40A03F && t.patch[0].n == 1 && t.patch[0].oud[0] == 1 && t.patch[0].nieuw[0] == 0, "patch 1");
    CHECK(t.patch[1].n == 2 && t.patch[1].oud[0] == 0x6A && t.patch[1].nieuw[1] == 0, "patch 2");

    CHECK(afgekeurd("p1.x = 12\np1.y = 13\n", "spelers"), "zonder spelers");
    CHECK(afgekeurd("spelers = 2\np1.x = 12\np1.y = 13\n", "p2.x"), "speler 2 zonder assen");
    CHECK(afgekeurd("spelers = 5\n", "spelers"), "te veel spelers");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\np1.x = 14\n", "twee keer"), "dubbel");
    CHECK(afgekeurd("spelers = 1\np1.x = 64\np1.y = 13\n", "offset"), "offset buiten het beeld");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\np1.knop0 = 32\n", "bit"), "bit 32");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\np1.knop32 = 1\n", "knop"), "knop 32");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\npatch.0x401000 = 01 02 -> 00\n", "even veel"), "patch ongelijk");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\npatch.0x401000 = 1 -> 0\n", "even veel"), "patch zonder twee hextekens");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\npatch.401000 = 01 -> 00\n", "0x"), "adres zonder 0x");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\nvlag.0x0 = 1\n", "0x"), "adres 0");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\nas.x = 5 5\n", "as.x"), "as zonder breedte");
    CHECK(afgekeurd("spelers = 1\np1.x = 12\np1.y = 13\nmunt = 2\n", "onbekende naam"), "onbekende naam");
}

static void test_joystick(void)
{
    rgd_tabel t;
    char fout[200];
    const char *bb = "spelers = 2\np1.x = 12\np1.y = 13\np1.knop0 = 7\np1.knop1 = 8\np2.x = 14\np2.y = 15\np2.knop0 = 12\n"
                     "as.x = -999 100\nas.y = -799 1000\nbuiten.x = -1000\nbuiten.y = -800\n";
    CHECK(rgd_lees_tabel(bb, &t, fout, sizeof fout) == 0, "%s", fout);
    uint8_t beeld[RGD_BEELD] = {0};
    const long lo[2] = {-1000, -1000}, hi[2] = {1000, 1000};
    rgd_joy j;
    beeld[12] = 128, beeld[13] = 128;
    rgd_joystick(&t, 0, beeld, lo, hi, &j);
    CHECK(!j.buiten && j.lX == -999 + (1099 * 128 + 127) / 255 && j.lY == -799 + (1799 * 128 + 127) / 255, "midden: %d %d", j.lX, j.lY);
    beeld[12] = 2, beeld[13] = 253;
    rgd_joystick(&t, 0, beeld, lo, hi, &j);
    CHECK(!j.buiten && j.lX == -999 + 9 && j.lY == -799 + 1785, "randen binnen: %d %d", j.lX, j.lY);
    /* Wat de game ervan maakt (BBPC.exe 0x410c51): x = (lX+1000)*640/1100, y = (lY+800)*480/1800. */
    CHECK((j.lX + 1000) * 640 / 1100 <= 6 && (j.lY + 800) * 480 / 1800 >= 475, "beeld van de game: %d %d", (j.lX + 1000) * 640 / 1100, (j.lY + 800) * 480 / 1800);
    beeld[12] = 255;
    rgd_joystick(&t, 0, beeld, lo, hi, &j);
    CHECK(j.buiten && j.lX == -1000 && j.lY == -800, "buiten beeld: %d %d", j.lX, j.lY);
    beeld[12] = 100, beeld[13] = 1;
    rgd_joystick(&t, 0, beeld, lo, hi, &j);
    CHECK(j.buiten && j.lX == -1000 && j.lY == -800, "buiten beeld (y): %d %d", j.lX, j.lY);
    /* Knoppen: bit 7 (P1 trekker) en bit 12 (P2 trekker). */
    beeld[12] = 100, beeld[13] = 100;
    beeld[8] = 0x80, beeld[9] = 0x10;
    rgd_joystick(&t, 0, beeld, lo, hi, &j);
    CHECK(j.knop[0] == 0x80 && j.knop[1] == 0, "p1 knoppen %02x %02x", j.knop[0], j.knop[1]);
    rgd_joystick(&t, 1, beeld, lo, hi, &j);
    CHECK(j.knop[0] == 0x80, "p2 trekker");
    rgd_joystick(&t, 2, beeld, lo, hi, &j);
    CHECK(j.lX == 0 && j.knop[0] == 0, "speler 3 bestaat niet");

    /* Zonder as.x/as.y: het bereik dat de game zette (en zonder buiten.*: de waarde van de byte). */
    rgd_standaard(&t);
    memset(beeld, 0, sizeof beeld);
    beeld[12] = 255, beeld[13] = 0;
    const long lo2[2] = {0, 0}, hi2[2] = {65535, 65535};
    rgd_joystick(&t, 0, beeld, lo2, hi2, &j);
    CHECK(j.buiten && j.lX == 65535 && j.lY == 0, "standaardbereik: %d %d", j.lX, j.lY);
    const long lo3[2] = {-1000, -1000}, hi3[2] = {1000, 1000};
    beeld[12] = 128, beeld[13] = 127;
    rgd_joystick(&t, 0, beeld, lo3, hi3, &j);
    CHECK(j.lX == 4 && j.lY == -4, "symmetrisch bereik: %d %d", j.lX, j.lY);
}

static void test_flanken(void)
{
    rgd_tabel t;
    char fout[200];
    CHECK(rgd_lees_tabel("spelers = 1\np1.x = 12\np1.y = 13\nvlag.0x1000 = 0\nvlag.0x2000 = 6, 11\nteller.0x3000 = 2, 3\n", &t, fout, sizeof fout) == 0, "%s", fout);
    rgd_flanken f = {0};
    uint32_t v;
    int n[RGD_MAX_ADRES];
    rgd_stap(&t, &f, 1u << 0 | 1u << 2, &v, n);
    CHECK(v == 0 && n[0] == 0, "eerste stap telt niet");
    rgd_stap(&t, &f, 1u << 0 | 1u << 2, &v, n);
    CHECK(v == 0 && n[0] == 0, "vasthouden is geen flank");
    rgd_stap(&t, &f, 0, &v, n);
    CHECK(v == 0 && n[0] == 0, "loslaten is geen flank");
    rgd_stap(&t, &f, 1u << 0 | 1u << 3, &v, n);
    CHECK(v == 1 && n[0] == 1, "test + munt 2: %u %d", v, n[0]);
    rgd_stap(&t, &f, 1u << 0 | 1u << 3 | 1u << 2, &v, n);
    CHECK(v == 0 && n[0] == 0, "tweede muntbit terwijl de eerste al aan is: geen nieuwe flank van de bron");
    rgd_stap(&t, &f, 1u << 11, &v, n);
    CHECK(v == 2 && n[0] == 0, "start 2: %u", v);
    rgd_stap(&t, &f, 1u << 11 | 1u << 6, &v, n);
    CHECK(v == 0, "start 1 terwijl start 2 al aan is (zelfde vlag)");
    CHECK(rgd_bits((const uint8_t[RGD_BEELD]){[8] = 0x01, [9] = 0x02, [10] = 0x04, [11] = 0x80}) == 0x80040201u, "bitveld little-endian");
}

static void test_cfg(const char *pad)
{
    uint8_t g[16];
    rgd_guid(0, g);
    CHECK(g[0] == 0x45 && g[15] == 1, "guid 1");
    rgd_guid(3, g);
    CHECK(g[15] == 4, "guid 4");
    if (!pad) return;
    size_t n = 0;
    char *d = lees(pad, &n);
    CHECK(d && n == 40, "%s: 40 bytes verwacht (%zu)", pad, n);
    if (!d || n != 40) return;
    for (int s = 0; s < 2; s++) {
        const uint8_t *p = (const uint8_t *)d + 20 * s;
        rgd_guid(s, g);
        CHECK(p[0] == 2 && p[1] == 0 && p[2] == 0 && p[3] == 0, "%s: speler %d niet modus 2 (joystick)", pad, s + 1);
        CHECK(memcmp(p + 4, g, 16) == 0, "%s: speler %d heeft niet de GUID van geweer %d", pad, s + 1, s + 1);
    }
    free(d);
}

/* VA -> bestandsoffset in een PE32 (zonder Windows-headers). */
static long va_naar_offset(const uint8_t *d, size_t n, uint32_t va)
{
    if (n < 0x40) return -1;
    uint32_t e = d[0x3c] | d[0x3d] << 8 | (uint32_t)d[0x3e] << 16 | (uint32_t)d[0x3f] << 24;
    if (e + 0xF8 > n || memcmp(d + e, "PE\0\0", 4) != 0) return -1;
    unsigned nsec = d[e + 6] | d[e + 7] << 8, opt = d[e + 20] | d[e + 21] << 8;
    uint32_t base = d[e + 52] | d[e + 53] << 8 | (uint32_t)d[e + 54] << 16 | (uint32_t)d[e + 55] << 24;
    const uint8_t *s = d + e + 24 + opt;
    for (unsigned i = 0; i < nsec; i++, s += 40) {
        uint32_t va_s = s[12] | s[13] << 8 | (uint32_t)s[14] << 16 | (uint32_t)s[15] << 24;
        uint32_t rs = s[16] | s[17] << 8 | (uint32_t)s[18] << 16 | (uint32_t)s[19] << 24;
        uint32_t ro = s[20] | s[21] << 8 | (uint32_t)s[22] << 16 | (uint32_t)s[23] << 24;
        if (va - base >= va_s && va - base < va_s + rs) return (long)(ro + (va - base - va_s));
    }
    return -1;
}

static void test_exe(const char *exe, const char *tabelpad)
{
    size_t n = 0;
    char *d = lees(exe, &n);
    char *tekst = lees(tabelpad, NULL);
    if (!d) { printf("overgeslagen: %s niet te lezen (alleen-lezen NAS niet gekoppeld?)\n", exe); free(tekst); return; }
    rgd_tabel t;
    char fout[200];
    CHECK(tekst && rgd_lees_tabel(tekst, &t, fout, sizeof fout) == 0, "%s: %s", tabelpad, fout);
    for (int i = 0; tekst && i < t.npatch; i++) {
        long o = va_naar_offset((const uint8_t *)d, n, t.patch[i].adres);
        CHECK(o >= 0 && (size_t)o + (size_t)t.patch[i].n <= n && memcmp(d + o, t.patch[i].oud, (size_t)t.patch[i].n) == 0,
              "%s: patch 0x%08X klopt niet met %s", tabelpad, (unsigned)t.patch[i].adres, exe);
    }
    printf("exe %s: %d patches gecontroleerd\n", exe, tekst ? t.npatch : 0);
    free(d);
    free(tekst);
}

int main(int argc, char **argv)
{
    test_tabel();
    test_joystick();
    test_flanken();
    const char *cfg = NULL;
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--cfg") == 0 && i + 1 < argc) cfg = argv[++i];
        else if (strcmp(argv[i], "--exe") == 0 && i + 2 < argc) { test_exe(argv[i + 1], argv[i + 2]); i += 2; }
        else {
            char *tekst = lees(argv[i], NULL);
            rgd_tabel t;
            char fout[200] = "";
            CHECK(tekst && rgd_lees_tabel(tekst, &t, fout, sizeof fout) == 0, "tabel %s: %s", argv[i], tekst ? fout : "niet te lezen");
            free(tekst);
        }
    }
    test_cfg(cfg);
    if (fouten) { printf("dinput8_unit: %d fout(en)\n", fouten); return 1; }
    printf("dinput8_unit: ok\n");
    return 0;
}
