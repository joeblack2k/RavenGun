/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * selftest.c - zelftest van de Matrix-donglelaag, zonder Windows en zonder game.
 *
 * Deze bron gebruikt alleen stdio/stdlib/string, dus hij compileert ook native op Linux. Daarmee
 * kun je de teruggeefwaarden controleren zonder Wine, Proton of een game te starten:
 *
 *   gcc -O2 -Wall -o matrix/out/selftest matrix/src/selftest.c matrix/src/matrix.c
 *   matrix/out/selftest
 *
 * De waarden die de game echt nodig heeft, staan in docs/matrix-layer.md: Init_MatrixAPI moet >= 0
 * zijn, Dongle_Count(85) > 0 en Dongle_ReadSerNr(...,85) > 0. Deze test faalt (exitcode 1) als een
 * van die drie niet klopt.
 */
#include "matrix.h"

#include <stdio.h>

static int fouten;

static void verwacht(const char *wat, long gekregen, long minstens)
{
    int goed = gekregen >= minstens;
    printf("  %-46s = %-8ld %s\n", wat, gekregen, goed ? "ok" : "FOUT");
    if (!goed) {
        fouten++;
    }
}

int main(void)
{
    printf("Zelftest van de Matrix-donglelaag\n\n");
    printf("Wat de game nodig heeft (Rabbids Hollywood, SBK.Matrix.MatrixManager):\n");
    verwacht("Init_MatrixAPI()", Init_MatrixAPI(), 0);
    verwacht("Dongle_Count(85)", Dongle_Count(MX_RABBIDS_POORT), 1);
    verwacht("Dongle_ReadSerNr(0, 85, 85)", Dongle_ReadSerNr(0, MX_RABBIDS_POORT, MX_RABBIDS_POORT), 1);

    printf("\nWat de rest van de API teruggeeft:\n");
    verwacht("Dongle_Find()", Dongle_Find(), 0);
    verwacht("Release_MatrixAPI()", Release_MatrixAPI(), 0);
    verwacht("Dongle_Exit()", Dongle_Exit(), 0);
    verwacht("Dongle_MemSize(85, 0)", Dongle_MemSize(MX_RABBIDS_POORT, 0), 0);
    verwacht("Dongle_ReadData zonder image", Dongle_ReadData(0, NULL, MX_RABBIDS_POORT, 0, 16), 1);
    verwacht("Dongle_EncryptData", Dongle_EncryptData(0, NULL, MX_RABBIDS_POORT, 16), 0);
    verwacht("Dongle_DecryptData", Dongle_DecryptData(0, NULL, MX_RABBIDS_POORT, 16), 0);
    verwacht("GetVersionAPI()", GetVersionAPI(), 0);

    printf("\nDe 64-bits en 32-bits DLL exporteren allebei 113 namen;\n");
    printf("scripts/build-matrix.sh controleert dat en vergelijkt ook de `ret N` met het origineel.\n");

    if (fouten) {
        printf("\n%d controle(s) mislukt\n", fouten);
        return 1;
    }
    printf("\nalle controles ok\n");
    return 0;
}
