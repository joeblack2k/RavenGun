/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * lijstpad_test.c — proef voor het onthouden van het pad van de patchlijst (patchmem.c, lees_lijst).
 *
 * Anders dan de andere proefprogramma's draait dit de motor NIET als LD_PRELOAD maar is hij erin
 * gelinkt: de proef roept patchmem_apply_file() zelf, een paar keer achter elkaar, en verandert
 * ertussen van werkmap. Dat is wat de Windows-kant doet (tweede ronde na DllMain) en wat op Linux
 * met een bibliotheekaanroep ook kan.
 *
 * Gebruik: lijstpad_test <mapA> <mapB>
 *   mapA/rel.txt    module modA.dll   (plus een regel met een vormfout)
 *   mapB/rel.txt    module modB.dll   (zelfde naam, ander bestand: de valstrik)
 *   mapB/ander.txt  module modC.dll
 * Geen enkele module bestaat, dus elke patch krijgt de status "wacht"; er wordt niets geschreven.
 * De uitvoer is `sleutel=waarde`, zoals bij probe_linux.
 */
#include "patchmem.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void toon(const char *stap, int rc)
{
    size_t n = 0;
    const patchmem_patch *l = patchmem_lijst(&n);
    /* Eerste echte patch (een vormfout-regel heeft module "regel N"). */
    const char *module = "(leeg)";
    size_t i;
    for (i = 0; i < n; i++) {
        if (!l[i].vormfout) { module = l[i].module; break; }
    }
    printf("%s_rc=%d\n%s_aantal=%zu\n%s_module=%s\n", stap, rc, stap, n, stap, module);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int rc;

    if (argc != 3) { fprintf(stderr, "gebruik: %s mapA mapB\n", argv[0]); return 2; }

    /* 1. Een betrekkelijk pad, in mapA. */
    if (chdir(argv[1]) != 0) { perror("chdir A"); return 2; }
    rc = patchmem_apply_file("rel.txt");
    toon("een", rc);

    /* 2. Van werkmap veranderen en HETZELFDE pad nog eens vragen (de tweede ronde): het moet nog
     *    steeds mapA/rel.txt zijn, niet het bestand met dezelfde naam in mapB. */
    if (chdir(argv[2]) != 0) { perror("chdir B"); return 2; }
    rc = patchmem_apply_file("rel.txt");
    toon("opnieuw", rc);

    /* 3. Een ANDER pad: dat moet gelezen worden, niet het eerste bestand. */
    rc = patchmem_apply_file("ander.txt");
    toon("ander", rc);

    /* 4. Een pad dat niet bestaat: mislukt, en de lijst van daarnet blijft staan. */
    rc = patchmem_apply_file("bestaat-niet.txt");
    toon("weg", rc);

    /* 5. Terug naar mapA en nog eens ander.txt: de mislukte poging van stap 4 heeft het onthouden
     *    pad niet gewist, dus het is nog steeds mapB/ander.txt. */
    if (chdir(argv[1]) != 0) { perror("chdir A"); return 2; }
    rc = patchmem_apply_file("ander.txt");
    toon("laatst", rc);
    return 0;
}
