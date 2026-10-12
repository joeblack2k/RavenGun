/* rg_shim.c -- de lader/resolver van librg-glutapi (i386).
 *
 * Wat deze bibliotheek is: een vervanger voor de 161 glut*-namen die
 * BudgieLoader.exe exporteert. 122 daarvan worden doorverwezen naar de echte
 * libglut (onze eigen 32-bits libglut.so.3), 39 maken wij zelf (31
 * *Ucall-varianten, glutStrokeLengthf/Widthf en 6 kleine uitbreidingen).
 *
 * Waarom dlopen/dlsym en niet gewoon de naam aanroepen: als wij glutInit zelf
 * definieren en daarbinnen glutInit aanroepen, roepen we onszelf aan. Daarom
 * wordt de echte libglut expliciet geladen en wordt per naam de aanroep
 * opgezocht; de stubs in rg_stubs.S springen naar die aanroepwijzer.
 *
 * Zelfbescherming: de gezochte bibliotheek wordt geweigerd als de gevonden
 * glutInit in ons eigen adresbereik blijkt te zitten (dan zou de doorverwijzing
 * recursie zijn).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_shim.h"

/* Ons eigen ankerpunt: via dladdr op dit adres kennen we het basisadres van
 * deze bibliotheek, zodat we een dlopen die onszelf teruggeeft kunnen weigeren. */
static void rg_anker(void) { }

static void *rg_eigen_basis = 0;   /* basisadres van deze .so */
static void *rg_echt = 0;          /* handle van de echte libglut */
static char rg_echt_pad[512];      /* het pad dat dladdr teruggaf */
static int rg_al_herprobeerd = 0;  /* is de noodpoging al gedaan? */
static int rg_log = -1;            /* -1 = nog niet bekeken, anders 0/1 */

static int rg_wil_log(void)
{
    if (rg_log < 0) {
        const char *e = getenv("RG_GLUTAPI_LOG");
        rg_log = (e && *e && *e != '0');
    }
    return rg_log;
}

static void rg_zeg(const char *regel)
{
    fputs(regel, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

static void rg_basis_bepaal(void)
{
    if (!rg_eigen_basis) {
        Dl_info info;
        if (dladdr((void *)rg_anker, &info))
            rg_eigen_basis = info.dli_fbase;
    }
}

/* Zoekt de echte libglut. Volgorde: RG_GLUT_REAL, dan de paden die in deze
 * omgeving voorkomen, dan de systeempaden, dan de kale sonaam. */
static void *rg_zoek_echt(void)
{
    char thuis[512];
    const char *kand[8];
    int nk = 0, i;
    const char *env = getenv("RG_GLUT_REAL");
    const char *home = getenv("HOME");

    if (env && *env)
        kand[nk++] = env;
    kand[nk++] = "/rg/lib/libglut.so.3.real";
    kand[nk++] = "/rg/lib/libglut.so.3";
    if (home && *home) {
        snprintf(thuis, sizeof thuis,
                 "%s/.local/share/ravengun/runtime/lib32/libglut.so.3", home);
        kand[nk++] = thuis;
    }
    kand[nk++] = "/usr/lib32/libglut.so.3";
    kand[nk++] = "/usr/lib/i386-linux-gnu/libglut.so.3";
    kand[nk++] = "libglut.so.3";

    rg_basis_bepaal();
    for (i = 0; i < nk; i++) {
        void *h = dlopen(kand[i], RTLD_LAZY | RTLD_GLOBAL);
        void *p;
        Dl_info info;
        char b[640];

        if (!h)
            continue;
        p = dlsym(h, "glutInit");
        if (!p) {
            dlclose(h);
            continue;
        }
        if (dladdr(p, &info) && info.dli_fbase == rg_eigen_basis) {
            /* Dat is deze shim zelf; doorlopen zou oneindige recursie geven. */
            if (rg_wil_log()) {
                snprintf(b, sizeof b, "rg-glutapi: %s is deze shim zelf, overgeslagen", kand[i]);
                rg_zeg(b);
            }
            dlclose(h);
            continue;
        }
        snprintf(rg_echt_pad, sizeof rg_echt_pad, "%s",
                 (info.dli_fname && *info.dli_fname) ? info.dli_fname : kand[i]);
        return h;
    }
    return 0;
}

/* Vult alle 122 slots. Geeft het aantal namen terug dat niet gevonden is. */
static unsigned rg_vul(void)
{
    unsigned i;
    unsigned ontbreekt = 0;

    if (!rg_echt)
        rg_echt = rg_zoek_echt();

    for (i = 0; i < rg_nslots; i++) {
        void *p = rg_echt ? dlsym(rg_echt, rg_slots[i].naam) : 0;
        if (p) {
            *rg_slots[i].slot = p;
        } else {
            ontbreekt++;
            if (rg_echt && ontbreekt <= 8) {
                char b[640];
                snprintf(b, sizeof b,
                         "rg-glutapi: let op: '%s' zit niet in %s; de aanroep wordt een no-op",
                         rg_slots[i].naam, rg_echt_pad[0] ? rg_echt_pad : "de echte libglut");
                rg_zeg(b);
            }
        }
    }
    if (!rg_echt) {
        char b[640];
        snprintf(b, sizeof b,
                 "rg-glutapi: geen echte libglut gevonden (%u van %u namen onopgelost); "
                 "zet RG_GLUT_REAL of zet de echte libglut in het zoekpad",
                 ontbreekt, rg_nslots);
        rg_zeg(b);
    }
    return ontbreekt;
}

/* Wordt door de noodstubs in rg_stubs.S aangeroepen. Die stubs bewaren de
 * argumenten op de stapel (deze functie raakt ze niet aan), dus de aanroep
 * zelf gaat verloren (de stub geeft 0 terug), maar alle namen die daarna wel
 * opgelost kunnen worden, werken vanaf de volgende aanroep. */
void rg_herprobeer(unsigned idx)
{
    char b[640];

    if (idx >= rg_nslots)
        return;
    if (rg_al_herprobeerd) {
        if (rg_wil_log()) {
            snprintf(b, sizeof b, "rg-glutapi: '%s' is nog steeds onopgelost", rg_slots[idx].naam);
            rg_zeg(b);
        }
        return;
    }
    rg_al_herprobeerd = 1;
    {
        unsigned ontbreekt = rg_vul();
        if (rg_echt && ontbreekt < rg_nslots) {
            snprintf(b, sizeof b,
                     "rg-glutapi: de echte libglut (%s) is nu wel gevonden; de aanroep van '%s' "
                     "die hierboven binnenkwam is als no-op teruggegeven",
                     rg_echt_pad, rg_slots[idx].naam);
        } else {
            snprintf(b, sizeof b,
                     "rg-glutapi: '%s' kon niet naar de echte libglut (%u namen onopgelost)",
                     rg_slots[idx].naam, ontbreekt);
        }
        rg_zeg(b);
    }
}

/* Bij het laden: alle slots vullen. Deze constructor draait onder de gewone
 * dlopen/ld.so-route. Een lader die zelf .so's mapt en geen DT_INIT_ARRAY
 * afspeelt, valt terug op de noodstubs hierboven. */
__attribute__((constructor)) static void rg_start(void)
{
    unsigned ontbreekt = rg_vul();
    if (rg_wil_log()) {
        char b[640];
        snprintf(b, sizeof b,
                 "rg-glutapi: echte libglut = %s; %u van %u namen opgelost, %u onopgelost",
                 rg_echt_pad[0] ? rg_echt_pad : "(niet gevonden)",
                 rg_nslots - ontbreekt, rg_nslots, ontbreekt);
        rg_zeg(b);
    }
}
