/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * patchmem.h — de geheugen-motor van RavenGun: bytepatches in een LOPEND proces.
 *
 * Dit is het tweede deel van de patchmotor. Het eerste deel (scripts/patchfile.py +
 * scripts/addrmap.py) schrijft in een BESTAND; zie docs/patchmotor.md. Dit deel schrijft in het
 * GEHEUGEN van een proces dat al draait, en wel om de drie gevallen die een bestandspatch niet
 * dekt (docs/community-model.md, paragraaf 3):
 *
 *   1. een patch in een DLL die pas later geladen wordt -- die bytes staan niet in de exe;
 *   2. een GEPAKTE exe -- de code op schijf is dan niet de code die draait;
 *   3. iets dat pas tijdens het draaien aangepast moet worden.
 *
 * De motor leest een tekstbestand met een patchlijst (het formaat staat in docs/memory-engine.md):
 *
 *     # module|offset|verwacht|nieuw
 *     game.exe|0x113E80||90909090
 *     UnityPlayer.dll|0x1234|48895c24|deadbeef
 *
 * `module` is de BESTANDSNAAM van de geladen module (case-ongevoelig), `offset` een RVA ten
 * opzichte van de basis van die module, `verwacht` de hex-bytes die er moeten staan (leeg = niet
 * controleren) en `nieuw` de hex-bytes die er moeten komen.
 *
 * Twee regels die niet onderhandelbaar zijn:
 *   - klopt `verwacht` niet, dan wordt er NIETS geschreven en komt er een duidelijke melding;
 *   - er wordt nooit naar de NAS geschreven -- dit is procesgeheugen en verder niets.
 *
 * Platform-onafhankelijk: het opzoeken van modules gaat via twee haken die de platformlaag
 * (entry_linux.c / entry_win.c) levert. Er zit geen enkele spel- of gamekennis in dit bestand.
 */
#ifndef RAVENGUN_PATCHMEM_H
#define RAVENGUN_PATCHMEM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zichtbaarheid van de functies die buiten de bibliotheek te gebruiken zijn. */
#if defined(_WIN32)
#  define PATCHMEM_API __declspec(dllexport)
#else
#  define PATCHMEM_API __attribute__((visibility("default")))
#endif

/* Bovengrenzen. Ruim genomen: een patchlijst is klein en een bytepatch is kort. */
#define PATCHMEM_MAX_PATCHES 1024     /* regels per lijst */
#define PATCHMEM_MAX_MODULE  256      /* tekens in een modulenaam */
#define PATCHMEM_MAX_BYTES   256      /* bytes per verwacht/nieuw */
#define PATCHMEM_MAX_MELDING 512      /* tekens in een melding */

/* De toestand van één patch. */
enum patchmem_status {
    PATCHMEM_NIEUW = 0,       /* nog niet geprobeerd */
    PATCHMEM_TOEGEPAST,       /* geschreven */
    PATCHMEM_AL_GOED,         /* de bytes stonden er al; niets geschreven */
    PATCHMEM_GEEN_MODULE,     /* de module is (nog) niet geladen; de patch wacht */
    PATCHMEM_VERWACHT_FOUT,   /* `verwacht` klopt niet; NIETS geschreven */
    PATCHMEM_MISLUKT         /* schrijven of lezen mislukte */
};

typedef struct patchmem_patch {
    /* uit de lijst */
    char module[PATCHMEM_MAX_MODULE];
    unsigned long long offset;                  /* RVA ten opzichte van de modulebasis */
    unsigned char verwacht[PATCHMEM_MAX_BYTES];
    size_t verwacht_len;                        /* 0 = niet controleren */
    unsigned char nieuw[PATCHMEM_MAX_BYTES];
    size_t nieuw_len;
    unsigned int regel;                         /* regelnummer in de lijst, voor meldingen */
    int vormfout;                               /* 1 = de regel was niet te lezen */

    /* toestand na het toepassen */
    int status;
    unsigned long long adres;                   /* het werkelijke adres (0 = nog onbekend) */
    char melding[PATCHMEM_MAX_MELDING];         /* waarom deze toestand */
} patchmem_patch;

typedef struct patchmem_verslag {
    size_t gelezen;          /* regels die een patch waren */
    size_t toegepast;        /* geschreven */
    size_t al_goed;          /* stond er al */
    size_t geen_module;      /* wacht nog op een module */
    size_t verwacht_fout;    /* verwacht klopte niet; niets geschreven */
    size_t mislukt;          /* lezen/schrijven mislukte */
    size_t vormfout;         /* de regel was niet te lezen */
} patchmem_verslag;

/* ---------------------------------------------------------------- de motor */

/* De lijst lezen en toepassen. Geeft het aantal patches dat NIET toegepast is. */
PATCHMEM_API int patchmem_apply_file(const char *pad);

/* Hetzelfde, maar het pad komt uit RAVENGUN_PATCHMEM_FILE. Zonder die variabele gebeurt er niets
 * en geeft de functie -1. */
PATCHMEM_API int patchmem_apply_from_env(void);

/* Nog een keer langs de patches die op een module wachten. Geeft het aantal dat nog wacht. */
PATCHMEM_API int patchmem_apply_pending(void);

/* Het aantal patches dat nog op een module wacht. */
PATCHMEM_API int patchmem_pending(void);

/* Eén regel per wachtende patch op stderr; voor als de wachttijd afloopt. */
PATCHMEM_API void patchmem_rapporteer_openstaand(void);

/* Alle geladen modules op stderr. Diagnose: hiermee zie je of een modulenaam in de lijst niet
 * overeenkomt met de naam die het proces gebruikt. */
PATCHMEM_API void patchmem_toon_modules(void);

/* Het verslag van de laatste keer lezen/toepassen. */
PATCHMEM_API patchmem_verslag patchmem_verslag_van(void);

/* De gelezen lijst (voor de proeven). */
PATCHMEM_API const patchmem_patch *patchmem_lijst(size_t *aantal);

/* 1 als RAVENGUN_PATCHMEM_VERBOSE gezet is. */
PATCHMEM_API int patchmem_verbose(void);

/* De versie van de motor; de proeven gebruiken hem om de DLL te importeren. */
PATCHMEM_API const char *patchmem_version(void);

/* ---------------------------------------------------------------- de haken (platformlaag) */

/* De basis van een geladen module, of NULL als hij niet geladen is.
 *
 * LET OP: een niet-PIE hoofdprogramma op Linux heeft basis 0, en dat is hier niet van NULL te
 * onderscheiden. Daarom is patchmem_foreach_module de doorslaggevende hulp: die meldt apart of de
 * module gevonden is. patchmem_module_base is de snelle weg, foreach is de zekere weg. */
void *patchmem_module_base(const char *naam);

/* Loop langs alle geladen modules. `cb` krijgt de BESTANDSNAAM van de module en zijn basis.
 * Geeft 1 terug om te stoppen met lopen, 0 om door te gaan. */
int patchmem_foreach_module(int (*cb)(const char *naam, void *basis, void *user), void *user);

/* ---------------------------------------------------------------- hulpmiddelen (binnen de bibliotheek) */

/* Vergelijkt twee modulenaamgevingen op hun bestandsnaam, case-ongevoelig. */
int patchmem_zelfde_module(const char *a, const char *b);

/* Het laatste stuk van een pad, na de laatste '/' of '\'. */
const char *patchmem_basisnaam(const char *pad);

/* Tijd, voor de achtergrondthread in de platformlaag (milliseconden). */
int64_t patchmem_nu_ms(void);
void patchmem_slaap_ms(int ms);

#ifdef __cplusplus
}
#endif

#endif /* RAVENGUN_PATCHMEM_H */
