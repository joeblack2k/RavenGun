/* neplibglut.c -- een minimale neplibglut voor de proef.
 *
 * Doel: bewijzen dat de doorverwijzingen in librg-glutapi.so echt aankomen en de
 * argumenten ongeschonden doorgeven. De echte libglut heeft geen functies zonder
 * initialisatiecontrole waarvan we het effect kunnen zien; deze nepperd wel.
 *
 * Hij exporteert glutInit (zodat de resolver hem als libglut accepteert) en
 * glutInitWindowSize met een waarneembaar neveneffect: hij schrijft een merker
 * naar het bestand in RG_NEP_MERK.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

void glutInit(int *argcp, char **argv)
{
    (void)argcp;
    (void)argv;
}

void glutInitWindowSize(int breedte, int hoogte)
{
    const char *merk = getenv("RG_NEP_MERK");
    if (!merk)
        return;
    {
        FILE *f = fopen(merk, "w");
        if (f) {
            fprintf(f, "nepperd glutInitWindowSize %d %d\n", breedte, hoogte);
            fclose(f);
        }
    }
}
