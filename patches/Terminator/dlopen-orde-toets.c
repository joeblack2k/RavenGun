/* dlopen-orde-toets.c - 32-bits, STATISCH, GEEN GAME.
 *
 * Bewijst de oorzaak van de FATAAL in de echte run, en test de workaround.
 *
 * libGLU.so.1 uit ElfLdr2/libs heeft GEEN ENKELE DT_NEEDED-regel: hij vertrouwt erop dat libGL en
 * libm al in de globale scope staan. De loader laadt de DT_NEEDED-lijst van de gast in volgorde:
 * libGL(1), libGLU(2), ..., libm(9). Dus als libGLU geladen wordt, is libm er nog niet en mist
 * libGLU `floor` -> dlopen mislukt -> de echte run doet fatal(). In --ontleed-alleen worden zulke
 * libs later opnieuw geprobeerd, en daarom zag de droogloop er groen uit.
 *
 *   mode A : libGL, libGLU                 (de echte volgorde)  -> verwacht: libGLU MISLUKT
 *   mode B : libGL, libm, libGLU           (workaround)         -> verwacht: alles GELADEN
 *
 * Bouwen (execstack is nodig: libGLU eist een uitvoerbare stack):
 *   gcc -m32 -pie -fPIE -O1 -Wl,-z,execstack -o dlopen-orde-toets dlopen-orde-toets.c -ldl
 * Gebruik: ./dlopen-orde-toets A|B <libGL> <libm> <libGLU>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

static int laad(const char *label, const char *pad)
{
    void *h = dlopen(pad, RTLD_NOW | RTLD_GLOBAL);
    if (h) { printf("  %-12s GELADEN\n", label); return 1; }
    printf("  %-12s MISLUKT -> %s\n", label, dlerror());
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "gebruik: %s A|B <libGL> <libm> <libGLU>\n", argv[0]); return 2; }
    const char *mode = argv[1], *gl = argv[2], *m = argv[3], *glu = argv[4];

    if (!strcmp(mode, "A")) {
        printf("=== mode A: libGL, libGLU  (de echte DT_NEEDED-volgorde, libm komt pas als 9e) ===\n");
        laad("libGL.so.1", gl);
        laad("libGLU.so.1", glu);
    } else {
        printf("=== mode B: libGL, libm, libGLU  (workaround: libm eerder in de globale scope) ===\n");
        laad("libGL.so.1", gl);
        laad("libm.so.6", m);
        laad("libGLU.so.1", glu);
    }
    return 0;
}
