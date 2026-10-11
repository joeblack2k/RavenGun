/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hasp_probe.exe — meetprogramma voor de Windows-kant van de HASP-laag.
 *
 * Time Crisis 5 importeert hasp_windows_x64_105293.dll op ORDINALEN 2, 13, 14 en 15 (gemeten; zie
 * docs/hasp-layer.md). Dit programma doet precies hetzelfde als het spel: de DLL laden en die vier
 * functies op ordinalen opzoeken en aanroepen. Zo is te bewijzen dat onze DLL onder Wine de juiste
 * uitvoertabel heeft en dat de dongle uit RAVENGUN_HASP_DIR + RAVENGUN_HASP_GAME gelezen wordt.
 *
 * Bouwen (mingw-w64, in de podman-image localhost/ravengun-mingw):
 *   x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -o hasp_probe.exe tests/hasp_win_probe.c
 *
 * Draaien onder de Wine van de games:
 *   RAVENGUN_HASP_VERBOSE=1 RAVENGUN_HASP_DIR=<patches> RAVENGUN_HASP_GAME=TC5 \
 *     wine hasp_probe.exe hasp_windows_x64_105293.dll
 *
 * Geeft 0 als alle vier de ordinalen bestaan en hasp_login succes meldt.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* De gemeten signaturen (docs/hasp-layer.md). Win64: rcx, rdx, r8, r9, dan de stapel. */
typedef int (*fn_login)(unsigned int vendor_code, const char *scope, void **handle);
typedef int (*fn_logout)(void *handle);
typedef int (*fn_read)(void *handle, unsigned int fileid, unsigned int offset, unsigned int length, void *buffer);
typedef int (*fn_decrypt)(void *handle, void *buffer, unsigned int length);

static int fouten = 0;

static void check(int ok, const char *wat)
{
    printf("  [%s] %s\n", ok ? "ok " : "FOUT", wat);
    if (!ok) fouten++;
}

static FARPROC ordinal(HMODULE h, int n, const char *naam)
{
    FARPROC p = GetProcAddress(h, MAKEINTRESOURCEA(n));
    printf("  [%s] ordinaal %d (%s) -> %p\n", p ? "ok " : "FOUT", n, naam, (void *)p);
    if (!p) fouten++;
    return p;
}

int main(int argc, char **argv)
{
    const char *naam = (argc > 1) ? argv[1] : "hasp_windows_x64_105293.dll";
    const char *game = getenv("RAVENGUN_HASP_GAME");
    const char *root = getenv("RAVENGUN_HASP_DIR");
    unsigned char buf[256];
    void *handle = NULL;
    int rc;

    printf("HASP-meetprogramma (Windows-kant)\n");
    printf("  DLL:  %s\n", naam);
    printf("  dongle: %s in %s\n\n", game ? game : "(RAVENGUN_HASP_GAME niet gezet)",
           root ? root : "(RAVENGUN_HASP_DIR niet gezet)");

    HMODULE h = LoadLibraryA(naam);
    if (!h) {
        printf("  [FOUT] %s niet te laden (fout %lu)\n", naam, (unsigned long)GetLastError());
        return 1;
    }
    printf("1. de DLL is geladen: %p\n", (void *)h);

    printf("\n2. de vier ordinalen die Time Crisis 5 importeert\n");
    fn_decrypt f_decrypt = (fn_decrypt)ordinal(h, 2, "hasp_decrypt");
    fn_login f_login = (fn_login)ordinal(h, 13, "hasp_login");
    fn_logout f_logout = (fn_logout)ordinal(h, 14, "hasp_logout");
    fn_read f_read = (fn_read)ordinal(h, 15, "hasp_read");
    if (!f_login || !f_read) {
        printf("\nMISLUKT — %d fout(en)\n", fouten);
        return 1;
    }

    printf("\n3. de volledige uitvoertabel (moet 25 namen hebben)\n");
    int gevonden = 0;
    static const struct { int n; const char *naam; } verwacht[] = {
        {1, "hasp_datetime_to_hasptime"}, {2, "hasp_decrypt"}, {3, "hasp_encrypt"}, {4, "hasp_free"},
        {5, "hasp_get_rtc"}, {6, "hasp_get_sessioninfo"}, {7, "hasp_get_size"}, {8, "hasp_hasptime_to_datetime"},
        {9, "hasp_legacy_decrypt"}, {10, "hasp_legacy_encrypt"}, {11, "hasp_legacy_set_idletime"},
        {12, "hasp_legacy_set_rtc"}, {13, "hasp_login"}, {14, "hasp_logout"}, {15, "hasp_read"},
        {16, "hasp_update"}, {17, "hasp_write"}, {18, "hasp_get_trace"}, {19, "hasp_login_port"},
        {20, "hasp_enable_trace"}, {21, "hasp_login_ex"}, {22, "hasp_get_info"}, {23, "hasp_login_scope"},
        {24, "hasp_get_version"}, {25, "hasp_detach"},
    };
    for (unsigned i = 0; i < sizeof(verwacht) / sizeof(verwacht[0]); i++) {
        FARPROC p = GetProcAddress(h, MAKEINTRESOURCEA(verwacht[i].n));
        if (!p) { printf("  [FOUT] ordinaal %d ontbreekt (%s)\n", verwacht[i].n, verwacht[i].naam); fouten++; }
        else gevonden++;
    }
    check(gevonden == 25, "alle 25 ordinalen bestaan");

    printf("\n4. de aanroepen, net als het spel\n");
    rc = f_login(0, NULL, &handle);
    check(rc == 0, "hasp_login geeft HASP_STATUS_OK (0)");
    printf("     greep: 0x%llx\n", (unsigned long long)(size_t)handle);
    if (rc != 0) {
        printf("\nMISLUKT — %d fout(en); is de dongle wel geladen? Zet RAVENGUN_HASP_DIR en RAVENGUN_HASP_GAME.\n", fouten);
        return 1;
    }
    memset(buf, 0, sizeof(buf));
    rc = f_read(handle, 65524, 0, 112, buf);
    check(rc == 0, "hasp_read(fileid 65524, 112 bytes) geeft HASP_STATUS_OK");
    printf("     eerste 8 bytes:");
    for (int i = 0; i < 8; i++) printf(" %02x", buf[i]);
    printf("\n");
    check(buf[0] == 0x2c && buf[1] == 0x03 && buf[2] == 0xab && buf[3] == 0x63,
          "de image begint met 2c 03 ab 63 (zoals de echte dongle)");
    if (f_decrypt) check(f_decrypt(handle, buf, 112) == 0, "hasp_decrypt meldt succes");
    check(f_logout(handle) == 0, "hasp_logout meldt succes");

    printf("\n%s — %d fout(en)\n", fouten ? "MISLUKT" : "GESLAAGD", fouten);
    return fouten ? 1 : 0;
}
