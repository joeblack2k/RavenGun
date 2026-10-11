/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * probe_win.c — proefprogramma voor de geheugen-motor van RavenGun (Windows, onder Wine/Proton).
 *
 * Dit programma IMPORTEERT ravengun-patchmem.dll. Dat is nodig: WINEDLLOVERRIDES=<naam>=n doet
 * alleen iets voor een module die het programma ook werkelijk importeert. Zo laden we de DLL op
 * dezelfde manier als een game dat zou doen.
 *
 * De uitvoer is `sleutel=waarde` per regel, zodat tests/test_memory_engine.py hem kan lezen.
 *
 * Bouwen: zie scripts/build-memory.sh
 *   x86_64-w64-mingw32-gcc -O2 -o probe_win.exe probe_win.c ravengun-patchmem.dll
 */
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* RAVENGUN_GEEN_IMPORT: deze proef importeert de DLL NIET. Dat is nodig om te kunnen meten of een
 * andere laadweg werkt (AppInit_DLLs), want WINEDLLOVERRIDES doet alleen iets voor een module die
 * het programma werkelijk importeert. */
#ifndef RAVENGUN_GEEN_IMPORT
#  include "patchmem.h"        /* alleen voor de versie; dat dwingt de import af */
#endif

/* De DLL die pas tijdens het draaien geladen wordt. Per breedte anders, want een 64-bits programma
 * kan geen 32-bits DLL laden. */
#ifndef RAVENGUN_LAAT_DLL
#  define RAVENGUN_LAAT_DLL "ravengun-laat.dll"
#endif

unsigned char ravengun_doel_win[16] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
};

__attribute__((noinline)) int ravengun_functie_win(void)
{
    return 0;
}

int (*volatile ravengun_roep_win)(void) = ravengun_functie_win;

static void toon_hex(const char *sleutel, const unsigned char *d, size_t n)
{
    size_t i;
    printf("%s=", sleutel);
    for (i = 0; i < n; i++) printf("%02x", d[i]);
    printf("\n");
}

static const char *basisnaam(const char *pad)
{
    const char *laatste = pad;
    const char *p;
    if (!pad) return "";
    for (p = pad; *p; p++) {
        if (*p == '/' || *p == '\\') laatste = p + 1;
    }
    return laatste;
}

/* GetProcAddress met en zonder leidend streepje: mingw noemt een export op 32-bits soms met "_". */
static void *vind_export(HMODULE m, const char *naam)
{
    char met[128];
    void *p = (void *)GetProcAddress(m, naam);
    if (p) return p;
    snprintf(met, sizeof met, "_%s", naam);
    return (void *)GetProcAddress(m, met);
}

int main(void)
{
    HMODULE eigen;
    char pad[MAX_PATH];
    DWORD n;
    HMODULE laat;
    unsigned char *laat_doel;
    int (*laat_functie)(void);
    unsigned char fb[6];
    unsigned char doel_nul[16];
    DWORD begin;
    int gemeten = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    eigen = GetModuleHandleA(NULL);
    n = GetModuleFileNameA(eigen, pad, sizeof pad);
    if (n == 0) pad[0] = '\0';
    else pad[sizeof pad - 1] = '\0';

    memcpy(doel_nul, ravengun_doel_win, sizeof doel_nul);
    memcpy(fb, (const void *)ravengun_functie_win, sizeof fb);

    printf("module=%s\n", basisnaam(pad));
    printf("offset=0x%llx\n",
           (unsigned long long)((uintptr_t)ravengun_doel_win - (uintptr_t)eigen));
    toon_hex("doel", ravengun_doel_win, sizeof ravengun_doel_win);
    printf("functie_offset=0x%llx\n",
           (unsigned long long)((uintptr_t)ravengun_functie_win - (uintptr_t)eigen));
    toon_hex("functie_bytes", fb, sizeof fb);
    printf("functie=%d\n", ravengun_roep_win());

    /* De motor zelf noemen, zodat de import van de DLL gegarandeerd is. */
#ifdef RAVENGUN_GEEN_IMPORT
    printf("motor=(niet geimporteerd)\n");
#else
    printf("motor=%s\n", patchmem_version());
#endif

    /* Wanneer komt de patch aan? De DLL doet zijn eerste ronde niet in DllMain (de loader-lock is
     * dan vast), maar in een draad. Hier meten we hoeveel milliseconden dat na de start is. Dat is
     * precies de eerlijke grens van de Windows-kant. */
    begin = GetTickCount();
    while (GetTickCount() - begin < 2000) {
        if (memcmp(ravengun_doel_win, doel_nul, sizeof doel_nul) != 0) {
            printf("doel_na_ms=%lu\n", (unsigned long)(GetTickCount() - begin));
            gemeten = 1;
            break;
        }
        Sleep(2);
    }
    if (!gemeten) printf("doel_na_ms=geen\n");
    toon_hex("doel_na", ravengun_doel_win, sizeof ravengun_doel_win);
    printf("functie_na=%d\n", ravengun_roep_win());

    /* En nu een module die pas tijdens het draaien geladen wordt: de achtergrondthread moet hem
     * oppikken. */
    laat = LoadLibraryA(RAVENGUN_LAAT_DLL);
    if (!laat) {
        printf("laat_fout=LoadLibrary mislukte (%lu)\n", (unsigned long)GetLastError());
        return 0;
    }
    *(void **)(&laat_doel) = vind_export(laat, "ravengun_laat_doel_win");
    *(void **)(&laat_functie) = vind_export(laat, "ravengun_laat_functie_win");
    if (!laat_doel || !laat_functie) {
        printf("laat_fout=export niet gevonden\n");
        return 0;
    }
    printf("laat_module=%s\n", RAVENGUN_LAAT_DLL);
    printf("laat_offset=0x%llx\n",
           (unsigned long long)((uintptr_t)laat_doel - (uintptr_t)laat));
    toon_hex("laat_direct", laat_doel, 16);
    printf("laat_functie_direct=%d\n", laat_functie());

    begin = GetTickCount();
    gemeten = 0;
    while (GetTickCount() - begin < 3000) {
        if (memcmp(laat_doel, "\xaa\xbb\xcc\xdd\xee\xff\x01\x02", 8) != 0) {
            printf("laat_na_ms=%lu\n", (unsigned long)(GetTickCount() - begin));
            gemeten = 1;
            break;
        }
        Sleep(2);
    }
    if (!gemeten) printf("laat_na_ms=geen\n");
    toon_hex("laat_na", laat_doel, 16);
    printf("laat_functie_na=%d\n", laat_functie());
    return 0;
}
