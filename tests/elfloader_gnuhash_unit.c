/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest voor rg-elfloader: het aantal dynamische symbolen zonder DT_HASH.
 * Echte i386-games (en de moderne systeem-libs) hebben alleen DT_GNU_HASH. De lader moet dan het
 * aantal uit (DT_STRTAB - DT_SYMTAB)/16 halen, en bij een onbetrouwbare afstand netjes weigeren.
 *
 * Wordt gebouwd door tests/elfloader_gnuhash_test.sh (gcc -m32), met de lader-bron zelf geinclude
 * (RG_ELFLOADER_ZONDER_MAIN laat de main() van de lader weg).
 * Gebruik: elfloader_gnuhash_unit <lib-alleen-gnu-hash.so> <lib-met-dt-hash.so>
 */
#include RG_LADER_BRON

static int fouten;
#define CHECK(c, ...) do { if (!(c)) { fouten++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
#ifndef RG_HEEFT_TEL_DYNSYMS
static void gebruik_fouten(void) { (void)fouten; }   /* alleen om de ongebruikt-waarschuwing te dempen */
#endif

int main(int argc, char **argv)
{
#ifndef RG_HEEFT_TEL_DYNSYMS
    /* Oude lader: alleen DT_HASH, geen terugvaller. Dat is precies het gat. */
    (void)argc; (void)argv;
    printf("FAIL: tel_dynsyms() ontbreekt: de lader kan het aantal symbolen alleen uit DT_HASH halen\n");
    return 1;
#else
    uint32_t n;
    const char *fout;
    int bron;
    uint32_t i;
    int b1 = 0, b2 = 0;
    uint32_t n1, n2;

    if (argc < 3) { printf("gebruik: %s <gnu-hash.so> <hash.so>\n", argv[0]); return 2; }

    /* 1. DT_HASH wint als hij er is */
    fout = ""; bron = tel_dynsyms(7, 0x1000, 0x1000 + 16 * 1020, &n, &fout);
    CHECK(bron == TEL_BRON_HASH && n == 7, "DT_HASH moet voorgaan (bron %d, n %u)", bron, n);

    /* 2. Zonder DT_HASH: (STRTAB-SYMTAB)/16, de gemeten waarde van de echte game */
    fout = ""; bron = tel_dynsyms(0, 0x080493e0, 0x0804d3a0, &n, &fout);
    CHECK(bron == TEL_BRON_STRTAB_SYMTAB && n == 1020, "verwacht 1020, kreeg bron %d n %u (%s)", bron, n, fout);

    /* 3. Harde sanity-checks: een duidelijke fout, geen gok */
    fout = ""; bron = tel_dynsyms(0, 0, 0x1000, &n, &fout);
    CHECK(bron == 0 && n == 0 && strstr(fout, "DT_SYMTAB"), "SYMTAB ontbreekt moet falen: '%s'", fout);
    fout = ""; bron = tel_dynsyms(0, 0x1000, 0, &n, &fout);
    CHECK(bron == 0 && strstr(fout, "DT_STRTAB"), "STRTAB ontbreekt moet falen: '%s'", fout);
    fout = ""; bron = tel_dynsyms(0, 0x1000, 0x1000, &n, &fout);
    CHECK(bron == 0 && strstr(fout, "niet achter"), "verschil 0 moet falen: '%s'", fout);
    fout = ""; bron = tel_dynsyms(0, 0x2000, 0x1000, &n, &fout);
    CHECK(bron == 0 && strstr(fout, "niet achter"), "STRTAB voor SYMTAB moet falen: '%s'", fout);
    fout = ""; bron = tel_dynsyms(0, 0x1000, 0x1000 + 16 * 5 + 4, &n, &fout);
    CHECK(bron == 0 && n == 0 && strstr(fout, "veelvoud van 16"), "geen veelvoud van 16 moet falen: '%s'", fout);

    /* 4. Echte lib-bestanden via parse_lib_file(): GNU_HASH-only en met DT_HASH leveren dezelfde defs.
          (parse_lib_file was voorheen blind voor een lib zonder DT_HASH.) */
    n1 = parse_lib_file(argv[1], &b1);
    n2 = parse_lib_file(argv[2], &b2);
    CHECK(n1 > 0 && b1 == TEL_BRON_STRTAB_SYMTAB, "gnu-hash-only lib: n=%u bron=%d (verwacht STRTAB-SYMTAB)", n1, b1);
    CHECK(n2 > 0 && b2 == TEL_BRON_HASH, "lib met DT_HASH: n=%u bron=%d (verwacht DT_HASH)", n2, b2);
    CHECK(n1 == n2, "beide libs zijn dezelfde code: %u != %u", n1, n2);
    qsort(rg_defname, rg_defname_n, sizeof rg_defname[0], def_cmp);
    for (i = 0; i < 3; i++) {
        static const char *w[] = {"fakelib_a", "fakelib_b", "fakelib_c"};
        CHECK(lib_defines(w[i]), "lib_defines(%s) moet waar zijn", w[i]);
    }
    CHECK(!lib_defines("fakelib_bestaat_niet"), "lib_defines van een onbekende naam moet onwaar zijn");

    /* 5. Het plafond van de lib-defs-lijst: vol-raken mag niet stil gebeuren. def_add() telt wat niet past
          in rg_defname_overflow, en de lader meldt dat luid (verzamel_lib_defs). We dwingen het vol-raken af
          door meer namen toe te voegen dan de lijst kan houden, en controleren dat de teller precies het
          overschot is en dat lib_defines() daarna nog werkt voor wat er wel in staat. */
    {
        const int cap = (int)(sizeof(rg_defname) / sizeof(rg_defname[0]));
        char naam[24];
        int extra = 1000;
        rg_defname_n = 0; rg_defbuf_used = 0; rg_defname_overflow = 0;
        for (i = 0; i < (uint32_t)(cap + extra); i++) {
            snprintf(naam, sizeof naam, "s%07u", (unsigned)i);
            def_add(naam);
        }
        CHECK(rg_defname_n == cap, "lijst moet precies vol zijn: %d van %d", rg_defname_n, cap);
        CHECK(rg_defname_overflow == extra, "overflow-teller moet %d zijn, is %d", extra, rg_defname_overflow);
        qsort(rg_defname, rg_defname_n, sizeof rg_defname[0], def_cmp);
        CHECK(lib_defines("s0000000"), "een naam die wel paste moet gevonden worden na vol-raken");
    }

    if (fouten) { printf("%d fout(en)\n", fouten); return 1; }
    printf("ok: tel_dynsyms + parse_lib_file (GNU_HASH-only en DT_HASH)\n");
    return 0;
#endif
}
