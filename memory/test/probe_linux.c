/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * probe_linux.c — proefprogramma voor de geheugen-motor van RavenGun (Linux).
 *
 * Het programma heeft twee doelen met een bekend patroon en drukt van beide het adres (als RVA ten
 * opzichte van de eigen modulebasis) en de inhoud af. Daarmee kan een proef buiten het programma
 * een patchlijst maken en controleren of de motor de bytes inderdaad veranderd heeft.
 *
 * Gebruik:
 *   probe_linux zelf                     de twee doelen in het programma zelf
 *   probe_linux laat <pad naar .so>      een bibliotheek die pas na een seconde geladen wordt
 *
 * De uitvoer is `sleutel=waarde` per regel, zodat tests/test_memory_engine.py hem kan lezen.
 */
#define _GNU_SOURCE 1

#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Het eerste doel: zestien bytes met een patroon dat je uit elkaar kunt houden. */
unsigned char ravengun_doel[16] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00
};

/* Het tweede doel: een functie die 0 teruggeeft. De proef patcht hem naar "mov eax,1 ; ret"
 * (b8 01 00 00 00 c3). */
__attribute__((noinline)) int ravengun_functie(void)
{
    return 0;
}

/* Via een vluchtige functiewijzer aanroepen, zodat de compiler de uitkomst niet vast invult: het
 * hele punt is dat de bytes tijdens het draaien veranderen. */
int (*volatile ravengun_roep)(void) = ravengun_functie;

/* ---------------------------------------------------------------- hulpjes */

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
        if (*p == '/') laatste = p + 1;
    }
    return laatste;
}

struct basiszoek {
    uintptr_t basis;
    int gevonden;
};

static int cb_eigen(struct dl_phdr_info *info, size_t grootte, void *data)
{
    struct basiszoek *z = (struct basiszoek *)data;
    (void)grootte;
    if (!info->dlpi_name || !info->dlpi_name[0]) {
        z->basis = (uintptr_t)info->dlpi_addr;
        z->gevonden = 1;
        return 1;
    }
    return 0;
}

/* De basis van het programma zelf. Let op: bij een niet-PIE programma is dat 0, en dan is de RVA
 * gelijk aan het adres zelf. Dat is precies hoe de motor het ook ziet. */
static uintptr_t eigen_basis(void)
{
    struct basiszoek z;
    z.basis = 0;
    z.gevonden = 0;
    dl_iterate_phdr(cb_eigen, &z);
    return z.basis;
}

static void toon_eigen_module(void)
{
    char pad[512];
    ssize_t n = readlink("/proc/self/exe", pad, sizeof pad - 1);
    if (n > 0) {
        pad[n] = '\0';
        printf("module=%s\n", basisnaam(pad));
    } else {
        printf("module=(onbekend)\n");
    }
}

/* ---------------------------------------------------------------- de twee proeven */

static void proef_zelf(void)
{
    uintptr_t basis = eigen_basis();
    unsigned char fb[6];

    toon_eigen_module();
    printf("doel_offset=0x%llx\n", (unsigned long long)((uintptr_t)ravengun_doel - basis));
    toon_hex("doel", ravengun_doel, sizeof ravengun_doel);
    printf("functie_offset=0x%llx\n", (unsigned long long)((uintptr_t)ravengun_functie - basis));
    memcpy(fb, (const void *)ravengun_functie, sizeof fb);
    toon_hex("functie_bytes", fb, sizeof fb);
    printf("functie=%d\n", ravengun_roep());
}

static void proef_late_module(const char *pad, const char *sym_doel, const char *sym_functie,
                              const char *prefix)
{
    void *greep;
    unsigned char *doel;
    int (*functie)(void);
    Dl_info info;
    uintptr_t basis;
    unsigned long long offset;
    char sleutel[64];

    toon_eigen_module();

    /* Eerst de bibliotheek laden. De motor onderschept dlopen, dus als het goed is staat de patch
     * er al op het moment dat deze regel klaar is. */
    greep = dlopen(pad, RTLD_NOW | RTLD_LOCAL);
    if (!greep) {
        printf("%s_fout=%s\n", prefix, dlerror() ? dlerror() : "dlopen mislukte");
        return;
    }
    doel = (unsigned char *)dlsym(greep, sym_doel);
    *(void **)(&functie) = dlsym(greep, sym_functie);
    if (!doel || !functie) {
        printf("%s_fout=symbolen niet gevonden\n", prefix);
        return;
    }
    if (!dladdr((void *)doel, &info) || !info.dli_fbase || !info.dli_fname) {
        printf("%s_fout=dladdr gaf geen basis\n", prefix);
        return;
    }
    basis = (uintptr_t)info.dli_fbase;
    offset = (unsigned long long)((uintptr_t)doel - basis);

    snprintf(sleutel, sizeof sleutel, "%s_module", prefix);
    printf("%s=%s\n", sleutel, basisnaam(info.dli_fname));
    snprintf(sleutel, sizeof sleutel, "%s_offset", prefix);
    printf("%s=0x%llx\n", sleutel, offset);
    snprintf(sleutel, sizeof sleutel, "%s_functie_offset", prefix);
    printf("%s=0x%llx\n", sleutel, (unsigned long long)((uintptr_t)functie - basis));

    snprintf(sleutel, sizeof sleutel, "%s_direct", prefix);
    toon_hex(sleutel, doel, 16);
    snprintf(sleutel, sizeof sleutel, "%s_functie_direct", prefix);
    printf("%s=%d\n", sleutel, functie());

    /* En dan wachten: als dlopen NIET onderschept wordt, moet de achtergrondthread het doen. */
    sleep(2);
    snprintf(sleutel, sizeof sleutel, "%s_na", prefix);
    toon_hex(sleutel, doel, 16);
    snprintf(sleutel, sizeof sleutel, "%s_functie_na", prefix);
    printf("%s=%d\n", sleutel, functie());
}

int main(int argc, char **argv)
{
    /* Niet bufferen: als het programma door de patch omvalt willen we toch de uitvoer zien. */
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc >= 2 && strcmp(argv[1], "zelf") == 0) {
        proef_zelf();
        return 0;
    }
    /* Een module die pas later geladen wordt (docs/community-model.md, geval 1). */
    if (argc >= 3 && strcmp(argv[1], "laat") == 0) {
        proef_late_module(argv[2], "ravengun_laat_doel", "ravengun_laat_functie", "laat");
        return 0;
    }
    /* Een nagebootste GEPAKTE module: in het bestand staan andere bytes dan in het geheugen
     * (docs/community-model.md, geval 2). */
    if (argc >= 3 && strcmp(argv[1], "gepakt") == 0) {
        proef_late_module(argv[2], "ravengun_gepakt_doel", "ravengun_gepakt_functie", "gepakt");
        return 0;
    }
    fprintf(stderr, "gebruik: %s zelf | %s laat <pad naar .so> | %s gepakt <pad naar .so>\n",
            argv[0], argv[0], argv[0]);
    return 2;
}
