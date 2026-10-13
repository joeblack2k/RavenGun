/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * pe_hook_engine_test.c -- toets de PE-haakengine (tools/rt-hasphook/rg_pe_hook.c) op Linux, op een
 * KLEIN SYNTHETISCH PE-beeld (geen Wine, geen spel, niets op de NAS). Dekt: functiehaken (actief
 * 1/0), de poort met `bytes = cc cc cc cc cc`, de byte-patch-regel (verwacht klopt / wijkt af /
 * actief=0 / tekst met NUL / bytes zonder verwacht), `antwoord = log0` (+ buf=), de eenmalig-vlag (ook
 * met threads), de [uitgang]- en trigger-sleutels, de zuivere bytefuncties van de NtProtect- en
 * ExitProcess-omleiding, de bytelijst-parser, een mislukte schrijfactie en een verkeerde build.
 *
 * Wat deze toets NIET kan: de Windows-kant (VirtualProtect, de echte omleidingen, DllMain) -- zie
 * tests/pe_hook_engine_test.sh voor de lijst.
 */
#define _GNU_SOURCE
#include "rg_pe_hook.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int fouten, gedaan;
#define CHECK(c, ...) do { gedaan++; if (!(c)) { fouten++; printf("  FOUT  %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok    "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define IB   0x150000000ULL
#define GROOTTE 0x6000

static unsigned char *beeld;

static void w16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void w32(unsigned char *p, unsigned v) { w16(p, v & 0xffff); w16(p + 2, v >> 16); }
static void w64(unsigned char *p, unsigned long long v) { w32(p, (unsigned)v); w32(p + 4, (unsigned)(v >> 32)); }

static void sectie(unsigned char *s, const char *naam, unsigned rva, unsigned vsize, unsigned flags)
{
    memset(s, 0, 40);
    memcpy(s, naam, strlen(naam));
    w32(s + 8, vsize); w32(s + 12, rva); w32(s + 16, vsize); w32(s + 20, rva); w32(s + 36, flags);
}

/* Een minimaal PE32+-beeld: .text (RVA 0x1000, 0x2000, uitvoerbaar) en .rdata (RVA 0x3000, 0x1000). */
static void bouw_beeld(void)
{
    unsigned char *h;
    if (beeld)
        munmap(beeld, GROOTTE);
    beeld = mmap((void *)(uintptr_t)IB, GROOTTE, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (beeld == MAP_FAILED) { perror("mmap"); exit(2); }
    memset(beeld, 0, GROOTTE);
    h = beeld;
    h[0] = 'M'; h[1] = 'Z'; w32(h + 0x3c, 0x80);
    memcpy(h + 0x80, "PE\0\0", 4);
    w16(h + 0x84, 0x8664); w16(h + 0x86, 2); w16(h + 0x94, 0xf0);
    w16(h + 0x98, 0x20b); w32(h + 0x98 + 16, 0x1000); w64(h + 0x98 + 24, IB);
    w32(h + 0x98 + 32, 0x1000); w32(h + 0x98 + 56, GROOTTE); w32(h + 0x98 + 60, 0x400);
    sectie(h + 0x98 + 0xf0, ".text", 0x1000, 0x2000, 0x60000020);
    sectie(h + 0x98 + 0xf0 + 40, ".rdata", 0x3000, 0x1000, 0x40000040);
    /* functies */
    memcpy(beeld + 0x1100, "\x40\x53\x48\x83\xec\x20\x90\x90", 8);     /* ampA   : actief          */
    memcpy(beeld + 0x1200, "\x48\x83\xec\x28\x90\x90\x90\x90", 8);     /* ampB   : actief = 0      */
    memset(beeld + 0x1300, 0xcc, 32);                                   /* de poort: int3-slot      */
    memcpy(beeld + 0x1400, "\x40\x53\x48\x83\xec\x20\x90\x90", 8);     /* ampL   : log0            */
    memcpy(beeld + 0x1450, "\x40\x53\x48\x83\xec\x20\x90\x90", 8);     /* ampN   : log0 + buf=     */
    /* patches */
    memcpy(beeld + 0x1500, "\x41\x0f\x45\xc9\x90\x90", 6);              /* klopt                    */
    memcpy(beeld + 0x1510, "\x11\x22\x33\x44\x90\x90", 6);              /* verwacht wijkt af        */
    memcpy(beeld + 0x1520, "\x74\x18", 2);                              /* actief = 0               */
    memcpy(beeld + 0x1530, "\x55\x66\x77\x88\x99", 5);                  /* geen verwacht            */
    memset(beeld + 0x3100, 0, 32);                                      /* datapad: 26 nullen       */
    beeld[0x311a] = 0x7e;                                               /* de byte erna blijft heel */
}

/* Roept een stub aan met de Windows-x64-argumentregisters, met 0x40 bytes ruimte (schaduw + uitlijning). */
static long roep_w64(uintptr_t f, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    long r;
    register uint64_t r8v __asm__("r8") = c;
    register uint64_t r9v __asm__("r9") = d;
    __asm__ volatile("sub $0x40,%%rsp\n\tcall *%1\n\tadd $0x40,%%rsp"
                     : "=a"(r)
                     : "S"(f), "c"(a), "d"(b), "r"(r8v), "r"(r9v)
                     : "r10", "r11", "memory", "cc", "rdi");
    return r;
}

static char logbuf[65536];
static FILE *nieuw_log(void) { return tmpfile(); }
static void lees_log(FILE *f)
{
    size_t n;
    fflush(f);
    rewind(f);
    n = fread(logbuf, 1, sizeof logbuf - 1, f);
    logbuf[n] = 0;
}
static int bevat(const char *s) { return strstr(logbuf, s) != NULL; }

static int melder_n, melder_idx;
static uint64_t melder_args[4], melder_ra;
static void log_melder(int idx, const uint64_t *a, uint64_t ra)
{
    melder_n++; melder_idx = idx; memcpy(melder_args, a, sizeof melder_args); melder_ra = ra;
}

static const char *INI =
    "game = Test\nbeeld = pe\nbasis = beeld\nproloog = standaard\nbewaar_args = 1\n"
    "grootte = 0x6000\ningang = 0x1000\nwacht_s = 0\nwacht_ms = 10\n"
    "trigger = protect\ntrigger_sectie = .text\ntrigger_prot = 0x20\n\n"
    "[ampA]\nrva = 0x1100\nantwoord = ret0\n\n"
    "[ampB]\nrva = 0x1200\nantwoord = ret0\nactief = 0\n\n"
    "[poort]\nrva = 0x1300\nbytes = cc cc cc cc cc\nantwoord = ret0\n\n"
    "[ampL]\nrva = 0x1400\nantwoord = log0\n\n"
    "[ampN]\nrva = 0x1450\nantwoord = log0 buf=rcx:c0 a8 01 00\n\n"
    "[patch match]\nrva = 0x1500\nverwacht = 41 0f 45 c9\nbytes = 33 c9 ff c1\nactief = 1\n\n"
    "[patch afwijkend]\nrva = 0x1510\nverwacht = aa bb\nbytes = cc dd\nactief = 1\n\n"
    "[patch uit]\nrva = 0x1520\nverwacht = 74\nbytes = eb\nactief = 0\n\n"
    "[patch zonder-verwacht]\nrva = 0x1530\nbytes = 01 02\n\n"
    "[patch datapad]\nrva = 0x3100\nverwacht = 00*26\ntekst = \"../../DataGlobal/EN/Data/\"\nactief = 1\n\n"
    "[uitgang]\nexport = kernel32.dll!ExitProcess\nterug_bij = 0xf1374d\nmax = 4\nactief = 1\n";

static void schrijf_ini(const char *pad, const char *tekst)
{
    FILE *f = fopen(pad, "w");
    fputs(tekst, f);
    fclose(f);
}

static void init_ctx(rg_pe_ctx *c, FILE *log, const char *ini)
{
    memset(c, 0, sizeof *c);
    c->lijst_pad = ini;
    c->beeld = beeld;
    c->beeld_n = GROOTTE;
    c->log = log;
    c->pid = (unsigned long)getpid();
    c->tid = 7;
    c->init_teller = 1;
    c->log_melder = log_melder;
}

static int g_claims;
static rg_pe_ctx *g_ctx;
static void *claim_thread(void *a)
{
    (void)a;
    if (rg_pe_pas_toe(g_ctx, "trigger") != -2)
        __sync_fetch_and_add(&g_claims, 1);
    return NULL;
}

int main(void)
{
    char ini[] = "/tmp/pe_hook_engine_test_XXXXXX.ini";
    int fd = mkstemps(ini, 4);
    rg_pe_ctx C;
    FILE *log;
    int rc, i;
    unsigned char buf[16];

    close(fd);
    schrijf_ini(ini, INI);
    bouw_beeld();

    printf("== 1. bytelijst-parser\n");
    {
        unsigned char b[64];
        CHECK(rg_pe_lees_bytes("48 83 ec 28", b, 64) == 4 && b[0] == 0x48 && b[3] == 0x28, "vier bytes");
        CHECK(rg_pe_lees_bytes("00*26", b, 64) == 26 && b[25] == 0, "00*26 = 26 nullen");
        CHECK(rg_pe_lees_bytes("ab*3 01", b, 64) == 4 && b[2] == 0xab && b[3] == 1, "herhaling gevolgd door een byte");
        CHECK(rg_pe_lees_bytes("zz", b, 64) == -1, "geen hex wordt geweigerd");
        CHECK(rg_pe_lees_bytes("00*65", b, 64) == -1, "te veel bytes wordt geweigerd");
        CHECK(rg_pe_lees_bytes("", b, 64) == 0, "leeg = 0");
    }

    printf("== 2. de lijst, de haken en de patches (poll-route, wacht_s = 0)\n");
    log = nieuw_log();
    init_ctx(&C, log, ini);
    rc = rg_pe_hook(&C);
    lees_log(log);
    CHECK(rc == 4, "4 functies aangehaakt (ampA, poort, ampL, ampN); rc = %d", rc);
    CHECK(C.uitgang_aanwezig && C.uitgang_actief && C.uitgang_terug_n == 1 && C.uitgang_terug_bij[0] == 0xf1374d &&
          C.uitgang_max == 4 && !strcmp(C.uitgang_export, "kernel32.dll!ExitProcess"),
          "[uitgang] ingelezen (terug_bij 0x%x, max %d, %s)", C.uitgang_terug_bij[0], C.uitgang_max, C.uitgang_export);
    CHECK(!strcmp(C.trigger, "protect") && C.trigger_prot == 0x20 && C.trigger_base == (uintptr_t)beeld + 0x1000,
          "trigger-sleutels ingelezen, trigger_base = beeld + 0x1000");
    CHECK(beeld[0x1100] == 0xe9, "ampA is omgezet naar e9");
    CHECK(memcmp(beeld + 0x1200, "\x48\x83\xec\x28\x90\x90\x90\x90", 8) == 0, "ampB (actief = 0) is ONGEWIJZIGD");
    CHECK(bevat("UIT (actief = 0)") , "ampB staat als UIT in het logboek");
    CHECK(beeld[0x1300] == 0xe9 && beeld[0x1305] == 0xcc, "de poort (cc-slot, bytes = cc ...) is omgezet; de rest van het slot blijft cc");
    CHECK(roep_w64((uintptr_t)beeld + 0x1100, 1, 2, 3, 4) == 0, "ampA geeft 0 terug");
    CHECK(roep_w64((uintptr_t)beeld + 0x1300, 0, 0, 0, 0) == 0, "de poort geeft 0 terug");
    CHECK(memcmp(beeld + 0x1500, "\x33\xc9\xff\xc1", 4) == 0 && beeld[0x1504] == 0x90, "patch match: nieuwe bytes, buurbytes heel");
    CHECK(memcmp(beeld + 0x1510, "\x11\x22\x33\x44", 4) == 0, "patch afwijkend: NIET geschreven");
    CHECK(bevat("WEIGER") && bevat("gevonden: 11 22") && bevat("verwacht: aa bb"), "patch afwijkend: beide byte-rijen in het logboek");
    CHECK(beeld[0x1520] == 0x74, "patch uit (actief = 0): NIET geschreven");
    CHECK(bevat("UIT (actief=0), niets geschreven; verwacht komt overeen"), "patch uit: gemeld, verwacht klopt");
    CHECK(beeld[0x1530] == 0x01 && beeld[0x1531] == 0x02 && beeld[0x1532] == 0x77, "patch zonder verwacht: geschreven");
    CHECK(bevat("(geen `verwacht` opgegeven)"), "patch zonder verwacht: dat staat in de regel");
    CHECK(memcmp(beeld + 0x3100, "../../DataGlobal/EN/Data/", 25) == 0 && beeld[0x3100 + 25] == 0 && beeld[0x311a] == 0x7e,
          "datapad: 25 tekens + NUL (26 bytes), byte 27 ongemoeid");
    CHECK(bevat("TOEGEPAST"), "TOEGEPAST in het logboek");
    CHECK(C.patches_toegepast == 3 && C.patches_totaal == 5 && C.patches_uit == 1 && C.patches_afgekeurd == 1,
          "patchtellers: toegepast %d van %d, uit %d, afgekeurd %d", C.patches_toegepast, C.patches_totaal, C.patches_uit, C.patches_afgekeurd);
    CHECK(bevat("totaal aangehaakt: 4 van 5 (1 uit); patches toegepast: 3 van 5 (1 uit); afgekeurd: 1; waarvan antwoord TODO: 0; "
                "uitgang: haak staat niet; TERUG 0; doorgelaten 0"), "de samenvattingsregel (exact)");
    CHECK(bevat("=================== rg-pe-hook run "), "run-scheiding aanwezig");
    {
        char pidtekst[48];
        snprintf(pidtekst, sizeof pidtekst, "pid %lu  tid 7", C.pid);
        CHECK(bevat(pidtekst), "de scheiding noemt de echte pid (%lu), niet 0", C.pid);
    }
    CHECK(bevat("toegepast door: poll"), "toegepast door: poll");
    CHECK(!bevat("FALLBACK"), "geen FALLBACK-regel bij de gewone poll-route");

    printf("== 3. log0\n");
    melder_n = 0;
    CHECK(roep_w64((uintptr_t)beeld + 0x1400, 11, 22, 33, 44) == 0, "ampL (log0) geeft 0 terug");
    CHECK(melder_n == 1 && melder_args[0] == 11 && melder_args[1] == 22 && melder_args[2] == 33 && melder_args[3] == 44 &&
          melder_ra != 0 && C.regels[3].log_aanroep, "de logger kreeg index %d, de vier argumenten en een terugkeeradres", melder_idx);
    memset(buf, 0xAA, sizeof buf);
    melder_n = 0;
    CHECK(roep_w64((uintptr_t)beeld + 0x1450, (uint64_t)(uintptr_t)buf, 2, 3, 4) == 0 && melder_n == 1 &&
          melder_args[0] == (uint64_t)(uintptr_t)buf, "ampN (log0 + buf=): gelogd EN rcx bleef bruikbaar");
    CHECK(buf[0] == 0xc0 && buf[1] == 0xa8 && buf[2] == 0x01 && buf[3] == 0x00 && buf[4] == 0xAA, "ampN schreef zijn 4 bytes (c0 a8 01 00)");
    fclose(log);

    printf("== 4. eenmalig: tweede aanroep en threads\n");
    log = nieuw_log();
    bouw_beeld();
    init_ctx(&C, log, ini);
    g_ctx = &C;
    CHECK(rg_pe_voorbereid(&C) == 0, "voorbereid gelukt");
    {
        pthread_t t[8];
        for (i = 0; i < 8; i++) pthread_create(&t[i], NULL, claim_thread, NULL);
        for (i = 0; i < 8; i++) pthread_join(t[i], NULL);
    }
    CHECK(g_claims == 1, "van 8 gelijktijdige aanroepers past er precies 1 toe (%d)", g_claims);
    CHECK(C.toegepast == 2, "toestand = klaar (2)");
    CHECK(rg_pe_pas_toe(&C, "poll") == -2, "een latere aanroep geeft -2 en doet niets");
    lees_log(log);
    CHECK(bevat("niet aan de beurt"), "dat staat in het logboek");
    fclose(log);

    printf("== 5. FALLBACK wordt als zodanig gemeld\n");
    log = nieuw_log();
    bouw_beeld();
    init_ctx(&C, log, ini);
    C.fallback = 1;
    rc = rg_pe_hook(&C);
    lees_log(log);
    CHECK(rc == 4 && bevat("toegepast door: FALLBACK") && bevat("TERUGVAL-poll, NIET door de trigger"), "FALLBACK-regels aanwezig");
    fclose(log);

    printf("== 6. verkeerde build: niets gepatcht\n");
    {
        char ini2[] = "/tmp/pe_hook_engine_test2_XXXXXX.ini";
        int fd2 = mkstemps(ini2, 4);
        char tekst[4096];
        close(fd2);
        snprintf(tekst, sizeof tekst, "%s", INI);
        { char *p = strstr(tekst, "grootte = 0x6000"); memcpy(p, "grootte = 0x9999", 16); }
        schrijf_ini(ini2, tekst);
        log = nieuw_log();
        bouw_beeld();
        init_ctx(&C, log, ini2);
        rc = rg_pe_hook(&C);
        lees_log(log);
        CHECK(rc == 0 && bevat("lijst AFGEKEURD") && beeld[0x1100] == 0x40 && beeld[0x1500] == 0x41 && beeld[0x3100] == 0,
              "lijst afgekeurd, beeld ongewijzigd");
        fclose(log);
        unlink(ini2);
    }

    printf("== 7. een mislukte schrijfactie wordt gelogd\n");
    log = nieuw_log();
    init_ctx(&C, log, ini);
    CHECK(rg_pe_schrijf(&C, 0x10, "x", 1, "toets") == 0, "schrijven op een ongemapt adres geeft 0");
    lees_log(log);
    CHECK(bevat("SCHRIJVEN MISLUKT (toets)") && bevat("errno"), "foutregel met errno");
    fclose(log);

    printf("== 8. NtProtectVirtualMemory-omleiding: bytefuncties\n");
    {
        static const unsigned char echt[16] = { 0x4c,0x8b,0xd1,0xb8,0x50,0,0,0,0xf6,0x04,0x25,0x08,0x03,0xfe,0x7f,0x01 };
        unsigned char f[16], s[14], t[30];
        memcpy(f, echt, 16);
        CHECK(rg_pe_nt_stub_ok(f), "de gemeten Proton-bytes worden herkend");
        f[4] = 0x77;
        CHECK(rg_pe_nt_stub_ok(f), "een ander syscallnummer mag");
        f[9] = 0x05;
        CHECK(!rg_pe_nt_stub_ok(f), "een afwijkende test-instructie wordt geweigerd");
        memcpy(f, echt, 16); f[0] = 0xe9;
        CHECK(!rg_pe_nt_stub_ok(f), "een al omgeleide functie (e9) wordt geweigerd");
        rg_pe_maak_sprong14(s, 0x1122334455667788ULL);
        CHECK(s[0] == 0xff && s[1] == 0x25 && s[2] == 0 && s[5] == 0 && s[6] == 0x88 && s[13] == 0x11, "sprong14 = ff 25 00000000 <doel>");
        rg_pe_maak_trampoline(t, echt, 0x7ffff0010ULL);
        CHECK(memcmp(t, echt, 16) == 0 && t[16] == 0xff && t[17] == 0x25 && t[22] == 0x10 && t[23] == 0x00 && t[24] == 0xff, "trampoline = 16 bytes + sprong naar f+16");
    }

    printf("== 9. [uitgang]: stubcontrole en besluit\n");
    {
        static const unsigned char echt[16] = { 0x48,0x83,0xec,0x28,0xff,0x15,0xce,0xf9,0x03,0x00,0x90,0x90,0x90,0x90,0x90,0x90 };
        unsigned char f[16], h[12];
        memcpy(f, echt, 16);
        CHECK(rg_pe_exit_stub_ok(f), "de gemeten ExitProcess-ingang wordt herkend");
        f[12] = 0x55;
        CHECK(!rg_pe_exit_stub_ok(f), "geen opvulling op byte 12: geweigerd");
        memcpy(f, echt, 16); f[5] = 0x25;
        CHECK(!rg_pe_exit_stub_ok(f), "ander begin: geweigerd");
        rg_pe_maak_haak12(h, 0x0102030405060708ULL);
        CHECK(h[0] == 0x48 && h[1] == 0xb8 && h[2] == 0x08 && h[9] == 0x01 && h[10] == 0xff && h[11] == 0xe0, "haak12 = 48 b8 <imm64> ff e0");
        init_ctx(&C, NULL, ini);
        C.uitgang_actief = 1; C.uitgang_max = 2; C.uitgang_terug_n = 2;
        C.uitgang_terug_bij[0] = 0xf1374d; C.uitgang_terug_bij[1] = 0x1234;
        CHECK(rg_pe_uitgang_besluit(&C, 0xf1374d, 0) == 1, "juiste terugkeer-RVA, eerste keer: TERUG");
        CHECK(rg_pe_uitgang_besluit(&C, 0x1234, 1) == 1, "tweede adres uit de lijst, tweede keer: TERUG");
        CHECK(rg_pe_uitgang_besluit(&C, 0xf1374d, 2) == 0, "na max keer: DOORGELATEN");
        CHECK(rg_pe_uitgang_besluit(&C, 0xf1374e, 0) == 0, "een RVA die er een byte naast zit: DOORGELATEN (exacte match)");
        CHECK(rg_pe_uitgang_besluit(&C, 0xd77780, 0) == 0, "een ander adres: DOORGELATEN");
        C.uitgang_actief = 0;
        CHECK(rg_pe_uitgang_besluit(&C, 0xf1374d, 0) == 0, "actief = 0: altijd doorlaten");
    }

    unlink(ini);
    printf("\n%s: %d controles, %d fout(en)\n", fouten ? "MISLUKT" : "GESLAAGD", gedaan, fouten);
    return fouten ? 1 : 0;
}
