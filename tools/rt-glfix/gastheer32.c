/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Kleine 32-bits gastheer om librt-glfix.so te laden zonder een game. De constructor van de
 * bibliotheek doet het werk (en draait de zelftest van de cdecl-brug); daarna drukken we af wat
 * er in het logbestand staat. Zo is de bibliotheek te controleren zonder een spel te starten.
 *
 * Gebruik:
 *   scripts/rt-glfix.sh --selftest
 * of met de hand:
 *   RAVENGUN_GLFIX_LOG=/dev/stderr ./gastheer32 librt-glfix.so
 */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    void *h;
    if (argc < 2) {
        fprintf(stderr, "gebruik: gastheer32 <pad/librt-glfix.so>\n");
        return 2;
    }
    h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "gastheer32: dlopen mislukt: %s\n", dlerror());
        return 1;
    }
    printf("gastheer32: geladen: %s\n", argv[1]);
    return 0;
}
