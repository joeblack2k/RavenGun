/* proef_slots.c -- bewijst per sprongstub waar de aanroepwijzer heen wijst.
 *
 * De stub die gen.py maakt heeft een vaste vorm (19 bytes):
 *    e8 00 00 00 00        call 1f                      (0)
 *    58                    pop  %eax                    (5)
 *    05 <imm32>            add  $imm, %eax              (6)
 *    8b 88 <disp32>        mov  disp(%eax), %ecx        (11)
 *    ff e1                 jmp  *%ecx                   (17)
 * Daarmee is de slotwijzer op te zoeken en uit te lezen, en met dladdr is te
 * zien in welke module hij ligt. Verwacht: alle 122 in de echte libglut, nul in
 * deze shim zelf (dat zou recursie zijn) en nul leeg.
 *
 * gebruik: proef_slots <onze.so> <namenbestand>
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    void *h;
    FILE *f;
    char regel[256];
    int n = 0, stubs = 0, eigen = 0, in_ons = 0, leeg = 0, onbekend = 0;
    char modules[8][512];
    int telling[8];
    int nmod = 0, i, j;

    if (argc < 3) {
        fprintf(stderr, "gebruik: %s <onze.so> <namenbestand>\n", argv[0]);
        return 2;
    }
    h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        printf("FAIL: dlopen('%s'): %s\n", argv[1], dlerror());
        return 1;
    }
    f = fopen(argv[2], "r");
    if (!f) {
        printf("FAIL: kan %s niet lezen\n", argv[2]);
        return 1;
    }

    while (fgets(regel, sizeof regel, f)) {
        char *naam = regel;
        size_t l;
        unsigned char *code;
        int32_t disp, off;
        void *got, *slot, *doel;
        Dl_info info;

        while (*naam == ' ' || *naam == '\t')
            naam++;
        l = strlen(naam);
        while (l && (naam[l - 1] == '\n' || naam[l - 1] == '\r'))
            naam[--l] = 0;
        if (!*naam)
            continue;
        n++;
        code = (unsigned char *)dlsym(h, naam);
        if (!code) {
            printf("  ONTBREEKT %s\n", naam);
            continue;
        }
        if (!(code[0] == 0xe8 && code[5] == 0x58 && code[6] == 0x05 &&
              code[11] == 0x8b && code[12] == 0x88 &&
              code[17] == 0xff && code[18] == 0xe1)) {
            eigen++;
            continue;
        }
        stubs++;
        memcpy(&disp, code + 7, 4);
        memcpy(&off, code + 13, 4);
        got = (void *)((char *)code + 5 + disp);
        slot = (void *)((char *)got + off);
        doel = *(void **)slot;
        if (!doel) {
            leeg++;
            printf("  LEEG %s\n", naam);
            continue;
        }
        if (!dladdr(doel, &info) || !info.dli_fname) {
            onbekend++;
            printf("  ONBEKEND %s -> %p\n", naam, doel);
            continue;
        }
        if (strstr(info.dli_fname, "librg-glutapi.so")) {
            in_ons++;
            printf("  RECURSIE %s -> %s\n", naam, info.dli_fname);
        }
        for (j = 0; j < nmod; j++)
            if (strcmp(modules[j], info.dli_fname) == 0)
                break;
        if (j == nmod && nmod < 8) {
            snprintf(modules[nmod], sizeof modules[nmod], "%s", info.dli_fname);
            telling[nmod] = 0;
            nmod++;
        }
        if (j < nmod)
            telling[j]++;
    }
    fclose(f);

    printf("namen bekeken    : %d\n", n);
    printf("sprongstubs      : %d (verwacht 122)\n", stubs);
    printf("eigen functies   : %d (verwacht 39)\n", eigen);
    printf("wijzer in deze .so (recursie): %d (verwacht 0)\n", in_ons);
    printf("wijzer leeg      : %d (verwacht 0)\n", leeg);
    printf("wijzer onbekend  : %d (verwacht 0)\n", onbekend);
    for (i = 0; i < nmod; i++)
        printf("wijzer naar      : %4d x %s\n", telling[i], modules[i]);

    if (stubs == 122 && in_ons == 0 && leeg == 0 && onbekend == 0)
        printf("PASS: alle 122 sprongstubs wijzen naar de echte libglut, geen enkele naar deze shim\n");
    else {
        printf("FAIL\n");
        return 1;
    }
    return 0;
}
