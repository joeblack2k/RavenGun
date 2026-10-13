/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_pe_hook.h — de kop van de generieke PE-inline-hook-engine (zie rg_pe_hook.c).
 *
 * De engine is bewust vrij van OS-specifieke aanroepen in zijn kern: het beeld komt als buffer
 * binnen, en alleen het aanmaken van de uitvoerbare stubpagina verschilt per platform (_WIN32).
 * Daardoor kan dezelfde code zowel in de Windows-DLL (het echte kanaal) als in een Linux-toets op
 * een geheugendump (het bewijs zonder bench) draaien.
 */
#ifndef RG_PE_HOOK_H
#define RG_PE_HOOK_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define RG_PE_MAX_SECTIES 24
#define RG_PE_MAX_REGELS 64

typedef struct {
    char naam[9];
    uint32_t vsize;
    uint32_t rva;
    uint32_t rawsize;
    uint32_t rawptr;
    uint32_t flags;
} rg_pe_sectie;

typedef struct {
    uint16_t machine;
    uint16_t aantal_secties;
    uint16_t subsystem;
    uint32_t ingang;
    uint32_t grootte_beeld;
    uint32_t grootte_kop;
    uint32_t sectie_uitlijning;
    uint64_t imagebase;
    rg_pe_sectie secties[RG_PE_MAX_SECTIES];
} rg_pe_kop;

enum {
    RG_ANTWOORD_TODO = 0,
    RG_ANTWOORD_RET0,
    RG_ANTWOORD_RET1,
    RG_ANTWOORD_RET,
    RG_ANTWOORD_VOID,
    RG_ANTWOORD_BUF,
    RG_ANTWOORD_U32,
    RG_ANTWOORD_OMHUL      /* omhullen: de originele functie blijft bereikbaar via een trampoline */
};

enum { RG_PROLOOG_STANDAARD = 0, RG_PROLOOG_E9REL32, RG_PROLOOG_API };
enum { RG_BEELD_ELF = 0, RG_BEELD_PE };

typedef struct {
    char naam[40];
    uint64_t adres;          /* zoals in de lijst */
    int is_rva;              /* rva = ... of adres = ... */
    char module[64];         /* welke module (leeg = het beeld dat is meegegeven) */
    char export[96];         /* naam in de exporttabel; dan is rva niet nodig */
    int is_export;
    uint32_t rva;            /* uitgerekend */
    uintptr_t doel_adres;    /* runtime-adres in dit beeld */
    unsigned char gezien[8]; /* de bytes die de KEURING zag (vóór het patchen) */
    int geldig;
    int gehaakt;
    int wacht;               /* 1 = de pagina is nog niet uitgepakt; opnieuw proberen */
    int antwoord;            /* RG_ANTWOORD_* */
    int32_t ret_waarde;
    char buf_reg[8];         /* rcx rdx r8 r9 */
    uint32_t u32_waarde;
    unsigned char bytes[16];     /* de payload van een buf=-antwoord */
    int bytes_n;
    int heeft_bytes;
    unsigned char verwacht_bytes[16];  /* de verwachte prologue uit `bytes = ...` */
    int verwacht_n;
    int heeft_verwacht;
    char notitie[200];
    char reden[240];
    char wijze[40];
} rg_pe_regel;

typedef struct {
    const char *lijst_pad;       /* het ini-bestand */
    unsigned char *beeld;        /* het gemapte beeld; offset == RVA */
    size_t beeld_n;
    FILE *log;                   /* NULL = stil */
    void (*todo_melder)(int idx);/* wordt door TODO-stubs aangeroepen */
    /*
     * Het BEWIJSARTEFACT: het pad waar de engine op het moment van de KEURING per adres de acht
     * gelezen bytes wegschrijft, met de pagina-hash. NULL = geen artefact. En `beeld_naam` is de naam
     * van het bestand waar het beeld vandaan komt, zodat er niets te raden valt.
     */
    const char *bytes_pad;
    const char *beeld_naam;
    FILE *bytes_log;
    /*
     * De afhandelaar van een omhul-hook (`antwoord = omhul`). De stub springt er met de ORIGINELE
     * argumentregisters naartoe, dus deze C-functie heeft dezelfde eerste vier integer-argumenten als
     * de afgevangen functie. Welke regel het is leest hij uit `huidige_idx`; de originele functie
     * roept hij aan via `omhul[idx].trampoline`.
     */
    uint64_t (*omhul_handler)(uint64_t, uint64_t, uint64_t, uint64_t);
    volatile int huidige_idx;
    struct {
        uintptr_t adres;
        unsigned char oud[16];
        int lengte;
        int gehaakt;
        char naam[40];
        void *trampoline;
    } omhul[RG_PE_MAX_REGELS];
    int omhul_n;
    int n;
    rg_pe_regel regels[RG_PE_MAX_REGELS];
    int beeld_vorm;
    int proloog;
    int basis_beeld;
    int bewaar_args;         /* zet de argumentregisters in de home space, zoals de gesloten kern doet */
    int wacht_s;             /* seconden wachten op een pagina die nog niet is uitgepakt (0 = niet) */
    int wacht_ms;            /* poll-interval in milliseconden */
    uint64_t verwacht_grootte;
    uint64_t verwacht_ingang;
    int stub_gebruikt;
    char lijst_pad_bewaard[512];
} rg_pe_ctx;

/* Leest de PE-kop uit het beeld (offset == RVA). 0 = geen geldige PE. */
int rg_pe_lees_kop(const unsigned char *beeld, size_t n, rg_pe_kop *k);

/* Zet een `antwoord = ...`-tekst om in de vorm en zijn parameters. */
int rg_antwoord_soort(const char *waarde, rg_pe_regel *r);

/* De hoofdroute: leest de lijst, keurt, genereert stubs, zet de prologues om.
   Geeft het aantal aangehaakte functies terug, of -1 bij een fatale fout. */
int rg_pe_hook(rg_pe_ctx *c);

/* Zet elke omhul-hook terug: de bewaarde originele bytes gaan weer op hun plaats.
   Geeft het aantal teruggezette functies terug. */
int rg_pe_herstel(rg_pe_ctx *c);

#endif /* RG_PE_HOOK_H */
