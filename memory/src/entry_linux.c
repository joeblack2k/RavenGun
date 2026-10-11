/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * entry_linux.c — de Linux-koppeling van de geheugen-motor van RavenGun.
 *
 * Dit bestand doet drie dingen en verder niets:
 *
 *   1. een `__attribute__((constructor))` die de patchlijst uit RAVENGUN_PATCHMEM_FILE toepast op
 *      het moment dat deze bibliotheek in het proces geladen wordt (via LD_PRELOAD);
 *   2. een ACHTERGRONDTHREAD die periodiek kijkt of een module uit de lijst inmiddels geladen is en
 *      hem dan toepast. Dat is precies wat een DLL dekt die pas later geladen wordt;
 *   3. het onderscheppen van `dlopen`, zodat een module die net geladen is ONMIDDELLIJK gepatcht
 *      wordt en niet pas bij de volgende ronde van de thread. Dat is nodig als de patch klaar moet
 *      zijn voordat de constructors van die module iets doen.
 *
 * Het opzoeken van modules gaat via `dl_iterate_phdr` (de twee haken patchmem_module_base en
 * patchmem_foreach_module). De motor zelf staat in patchmem.c.
 *
 * Omgevingsvariabelen:
 *   RAVENGUN_PATCHMEM_FILE     het pad van de patchlijst (verplicht; zonder gebeurt er niets)
 *   RAVENGUN_PATCHMEM_VERBOSE  gezet: één regel per patch op stderr, plus de geladen modules
 *   RAVENGUN_PATCHMEM_WACHT    seconden dat de achtergrondthread blijft kijken (standaard 30; 0 = uit)
 *   RAVENGUN_PATCHMEM_POLL     milliseconden tussen twee rondes (standaard 250)
 *   RAVENGUN_PATCHMEM_GEEN_DLOPEN  gezet: dlopen niet onderscheppen (alleen voor de proef; dan doet
 *                              de achtergrondthread het werk)
 *
 * Bouwen: scripts/build-memory.sh
 *   gcc -shared -fPIC -O2 -o ravengun-patchmem.so   src/patchmem.c src/entry_linux.c -lpthread -ldl
 *   gcc -m32 -shared -fPIC -O2 -o ravengun-patchmem32.so src/patchmem.c src/entry_linux.c -lpthread -ldl
 */
#define _GNU_SOURCE 1

#include "patchmem.h"

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------------- de naam van het hoofdprogramma */

/* `dl_iterate_phdr` geeft voor de hoofdmodule een LEGE dlpi_name; de naam staat dan in
 * /proc/self/exe. Zonder dit zou een patch op de exe van de game nooit gevonden worden. */
static const char *hoofd_naam(void)
{
    static char naam[512];
    static int klaar;

    if (!klaar) {
        ssize_t n = readlink("/proc/self/exe", naam, sizeof naam - 1);
        if (n > 0) naam[n] = '\0';
        else naam[0] = '\0';
        klaar = 1;
    }
    return naam;
}

static const char *module_naam(const struct dl_phdr_info *info)
{
    if (info->dlpi_name && info->dlpi_name[0]) return info->dlpi_name;
    return hoofd_naam();
}

/* ---------------------------------------------------------------- de twee haken */

struct zoekcontext {
    const char *naam;
    void *basis;
    int gevonden;
};

static int cb_zoek(struct dl_phdr_info *info, size_t grootte, void *data)
{
    struct zoekcontext *z = (struct zoekcontext *)data;
    (void)grootte;

    if (patchmem_zelfde_module(module_naam(info), z->naam)) {
        z->basis = (void *)(uintptr_t)info->dlpi_addr;
        z->gevonden = 1;
        return 1;                                  /* gevonden: stoppen met lopen */
    }
    return 0;
}

void *patchmem_module_base(const char *naam)
{
    struct zoekcontext z;

    if (!naam || !*naam) return NULL;
    z.naam = naam;
    z.basis = NULL;
    z.gevonden = 0;
    dl_iterate_phdr(cb_zoek, &z);
    /* Let op: bij een niet-PIE hoofdprogramma is de basis 0 en geeft deze functie dus NULL terug,
     * ook al is de module gevonden. patchmem.c valt daarom terug op patchmem_foreach_module. */
    return z.gevonden ? z.basis : NULL;
}

struct loopcontext {
    int (*cb)(const char *naam, void *basis, void *user);
    void *user;
};

static int cb_alles(struct dl_phdr_info *info, size_t grootte, void *data)
{
    struct loopcontext *l = (struct loopcontext *)data;
    const char *naam = module_naam(info);
    (void)grootte;

    if (!naam || !*naam) return 0;
    return l->cb(patchmem_basisnaam(naam), (void *)(uintptr_t)info->dlpi_addr, l->user);
}

int patchmem_foreach_module(int (*cb)(const char *naam, void *basis, void *user), void *user)
{
    struct loopcontext l;

    if (!cb) return 0;
    l.cb = cb;
    l.user = user;
    return dl_iterate_phdr(cb_alles, &l);
}

/* ---------------------------------------------------------------- de achtergrondthread */

static int env_int(const char *naam, int standaard)
{
    const char *v = getenv(naam);
    if (!v || !*v) return standaard;
    return atoi(v);
}

static void *achtergrond(void *arg)
{
    int wacht = env_int("RAVENGUN_PATCHMEM_WACHT", 30);
    int poll = env_int("RAVENGUN_PATCHMEM_POLL", 250);
    int64_t begin;

    (void)arg;
    if (poll < 20) poll = 20;
    begin = patchmem_nu_ms();

    for (;;) {
        if (patchmem_pending() == 0) break;
        if (patchmem_nu_ms() - begin >= (int64_t)wacht * 1000) {
            /* De wachttijd is om. Zeggen wat er niet gelukt is is beter dan stil ophouden. */
            patchmem_rapporteer_openstaand();
            break;
        }
        patchmem_slaap_ms(poll);
        patchmem_apply_pending();
    }
    return NULL;
}

static void start_achtergrond(void)
{
    int wacht = env_int("RAVENGUN_PATCHMEM_WACHT", 30);
    pthread_t draad;
    pthread_attr_t attr;

    if (wacht <= 0) {
        fprintf(stderr, "patchmem: RAVENGUN_PATCHMEM_WACHT=0 -- geen achtergrondthread; "
                        "patches in later geladen modules worden niet toegepast\n");
        return;
    }
    if (patchmem_pending() == 0) return;           /* alles al gedaan: geen draad nodig */

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&draad, &attr, achtergrond, NULL) != 0) {
        fprintf(stderr, "patchmem: kon de achtergrondthread niet starten; patches in later geladen "
                        "modules blijven liggen (dlopen wordt nog wel onderschept)\n");
    }
    pthread_attr_destroy(&attr);
}

/* ---------------------------------------------------------------- dlopen onderscheppen */

typedef void *(*dlopen_echt_t)(const char *, int);

static dlopen_echt_t dlopen_echt(void)
{
    static dlopen_echt_t echt;
    static int gezocht;

    if (!gezocht) {
        echt = (dlopen_echt_t)dlsym(RTLD_NEXT, "dlopen");
        if (!echt) echt = (dlopen_echt_t)dlsym(RTLD_DEFAULT, "dlopen");
        gezocht = 1;
    }
    return echt;
}

/* Wordt door het hele proces gebruikt zodra wij via LD_PRELOAD binnen zijn. We doen niets met de
 * argumenten; we kijken alleen of er nu een module bij gekomen is die in de lijst staat.
 *
 * RAVENGUN_PATCHMEM_GEEN_DLOPEN zet de onderschepping uit. Dat is er voor de proef: alleen dan is
 * te zien dat de ACHTERGRONDTHREAD het werk doet en niet deze haak. */
void *dlopen(const char *bestand, int vlaggen)
{
    static int haak_uit = -1;
    dlopen_echt_t echt;
    void *greep;

    if (haak_uit < 0) {
        const char *v = getenv("RAVENGUN_PATCHMEM_GEEN_DLOPEN");
        haak_uit = (v && *v) ? 1 : 0;
    }
    echt = dlopen_echt();
    if (!echt) {
        /* Zonder de echte dlopen kunnen we niets; liever eerlijk falen dan iets anders teruggeven. */
        fprintf(stderr, "patchmem: kon de echte dlopen niet vinden; dlopen wordt niet onderschept\n");
        return NULL;
    }
    greep = echt(bestand, vlaggen);
    if (greep && !haak_uit && patchmem_pending() > 0) patchmem_apply_pending();
    return greep;
}

/* ---------------------------------------------------------------- het begin */

__attribute__((constructor))
static void ravengun_patchmem_init(void)
{
    if (patchmem_apply_from_env() < 0) return;     /* geen RAVENGUN_PATCHMEM_FILE: niets doen */
    if (patchmem_verbose()) patchmem_toon_modules();
    start_achtergrond();
}
