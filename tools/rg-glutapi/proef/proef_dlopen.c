/* proef_dlopen.c -- laadt librg-glutapi.so in een 32-bits proces en zoekt alle
 * 161 namen op. Met de modes:
 *   proef_dlopen <onze.so> <namenbestand>            alle namen opzoeken
 *   proef_dlopen <onze.so> <namenbestand> nep <merk>  plus een doorverwijzing
 *                                                      waarneembaar maken via de
 *                                                      neplibglut (RG_GLUT_REAL)
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *so, *namenbestand, *modus = "";
    const char *merk = NULL;
    void *h;
    FILE *f;
    char regel[256];
    int n = 0, mis = 0;

    if (argc < 3) {
        fprintf(stderr, "gebruik: %s <onze.so> <namenbestand> [nep <merker>]\n", argv[0]);
        return 2;
    }
    so = argv[1];
    namenbestand = argv[2];
    if (argc >= 5) {
        modus = argv[3];
        merk = argv[4];
    }

    h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        printf("FAIL: dlopen('%s'): %s\n", so, dlerror());
        return 1;
    }
    printf("dlopen gelukt, handle %p\n", h);

    f = fopen(namenbestand, "r");
    if (!f) {
        printf("FAIL: kan %s niet lezen\n", namenbestand);
        return 1;
    }
    while (fgets(regel, sizeof regel, f)) {
        char *p = regel;
        size_t l;
        while (*p == ' ' || *p == '\t')
            p++;
        l = strlen(p);
        while (l && (p[l - 1] == '\n' || p[l - 1] == '\r'))
            p[--l] = 0;
        if (!*p)
            continue;
        n++;
        if (!dlsym(h, p)) {
            mis++;
            printf("  ONTBREEKT %s\n", p);
        }
    }
    fclose(f);
    printf("dlsym: %d van %d namen gevonden, %d ontbrekend\n", n - mis, n, mis);

    {
        Dl_info info;
        void *g = dlsym(h, "glutInit");
        if (g && dladdr(g, &info))
            printf("onze glutInit zit in: %s\n", info.dli_fname ? info.dli_fname : "(?)");
    }

    if (strcmp(modus, "nep") == 0) {
        void (*gsz)(int, int) = (void (*)(int, int))dlsym(h, "glutInitWindowSize");
        char buf[256] = "";
        FILE *m;
        if (!gsz) {
            printf("FAIL: glutInitWindowSize niet gevonden\n");
            return 1;
        }
        gsz(11, 22);
        m = merk ? fopen(merk, "r") : NULL;
        if (!m) {
            printf("FAIL: de nepperd heeft geen merker geschreven -> de doorverwijzing kwam niet aan\n");
            return 1;
        }
        if (fgets(buf, sizeof buf, m))
            printf("nepperd zag: %s", buf);
        fclose(m);
        if (strstr(buf, "11 22")) {
            printf("PASS: de doorverwijzing naar de neplibglut kwam aan, met de juiste argumenten\n");
            return 0;
        }
        printf("FAIL: onverwachte merkerinhoud\n");
        return 1;
    }

    if (mis) {
        printf("FAIL: %d namen niet gevonden\n", mis);
        return 1;
    }

    /* Eén doorverwijzing echt uitvoeren tegen de echte libglut. glutInitWindowSize
     * heeft (gemeten) geen initialisatiecontrole. Een stub die zichzelf zou
     * aanroepen zou hier de stapel laten overlopen; de shim meldt op stderr als
     * een aanroep op de noodstub belandt. */
    {
        void (*gsz)(int, int) = (void (*)(int, int))dlsym(h, "glutInitWindowSize");
        if (gsz) {
            gsz(33, 44);
            printf("aanroep glutInitWindowSize(33,44) door de wip: geen crash\n");
        }
    }

    printf("PASS: alle %d namen zijn op te zoeken in de geladen bibliotheek\n", n);
    return 0;
}
