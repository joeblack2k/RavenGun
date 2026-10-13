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
    RG_ANTWOORD_OMHUL,     /* omhullen: de originele functie blijft bereikbaar via een trampoline */
    RG_ANTWOORD_VELDEN     /* velden=rcx:0x110=0x40,... : u32's in het object zetten, dan 0 terug */
};

#define RG_PATCH_MAX 64        /* hoogstens zoveel bytes per byte-patch (verwacht en nieuw) */
#define RG_UITGANG_MAX 8       /* hoogstens zoveel terugkeeradressen in [uitgang] terug_bij */

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
    int opvulling;           /* 1 = wel niet-nul, maar opvulling/half geschreven; ook opnieuw proberen */
    unsigned char veld_reg;  /* 0 = rcx (de enige die nu wordt ondersteund) */
    int veld_n;
    struct {
        uint32_t off;
        uint32_t val;
    } veld[4];
    int antwoord;            /* RG_ANTWOORD_* */
    int32_t ret_waarde;
    char buf_reg[8];         /* rcx rdx r8 r9 */
    uint32_t u32_waarde;
    unsigned char bytes[16];     /* de payload van een buf=-antwoord */
    int bytes_n;
    int heeft_bytes;
    unsigned char verwacht_bytes[RG_PATCH_MAX];  /* de verwachte prologue (`bytes = ...`) of, bij een patch, `verwacht = ...` */
    int verwacht_n;
    int heeft_verwacht;
    char notitie[200];
    char reden[240];
    char wijze[40];
    /* --- uitbreidingen --- */
    int actief;              /* actief = 0|1 (standaard 1): 0 = ingelezen en gemeld, niet toegepast */
    int log_aanroep;         /* antwoord = log0 [antwoord]: de stub logt elke aanroep voor hij antwoordt */
    int is_patch;            /* [patch naam]: een gewone byte-patch, geen functiestub */
    unsigned char patch_bytes[RG_PATCH_MAX];   /* wat er geschreven wordt (bytes = ... of tekst = "...", met NUL) */
    int patch_n;
    int patch_uit_tekst;     /* 1 = uit `tekst =` */
    int patch_toegepast;     /* 1 = geschreven en teruggelezen */
    char patch_oordeel[24];  /* TOEGEPAST | UIT | WEIGER | AFGEWEZEN | SCHRIJVEN MISLUKT */
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
     * Herkomst van deze lezing, zodat een andere partij kan uitsluiten dat er naar een ANDERE
     * procesinstantie of een tweede initialisatie gekeken wordt: pid, thread-id, hoe lang het proces
     * al loopt, en de hoeveelste initialisatie dit is.
     */
    unsigned long pid;
    unsigned long tid;
    uint64_t proc_uptime_ms;
    int init_teller;
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
    /*
     * De aanroep-logger van `antwoord = log0`: (index, wijzer naar de vier argumentregisters rcx rdx r8 r9
     * zoals de stub ze in de schaduwruimte van de aanroeper zette, terugkeeradres). Windows-ABI in de
     * DLL; op Linux (toets) de SysV-variant (rdi, rsi, rdx).
     */
    void (*log_melder)(int idx, const uint64_t *args, uint64_t terugkeer);
    /* De trigger: wanneer de pakker zijn sectie verzegelt (NtProtectVirtualMemory), niet pollen. */
    char trigger[16];            /* "" = alleen pollen, "protect" = NtProtectVirtualMemory */
    char trigger_sectie[9];      /* bijvoorbeeld ".text" */
    uint32_t trigger_prot;       /* nieuwe bescherming die de trigger is, bijvoorbeeld 0x20 */
    uintptr_t trigger_base;      /* uitgerekend: ImageBase + RVA van de sectie; 0 = niet gevonden */
    /* Eenmalig toepassen: 0 = vrij, 1 = iemand is bezig, 2 = klaar. Alleen via rg_pe_claim() wijzigen. */
    volatile int toegepast;
    const char *toegepast_door;  /* "trigger", "poll" of "FALLBACK" */
    int fallback;                /* 1 = dit is de terugval-poll na het verstrijken van wacht_s */
    int voorbereid;              /* rg_pe_voorbereid() is gedaan */
    int lijst_afgekeurd;         /* verkeerde build: alle regels afgewezen */
    rg_pe_kop kop;
    int kop_ok;
    int patches_toegepast, patches_totaal, patches_uit, patches_afgekeurd;
    /* [uitgang]: kernel32!ExitProcess beantwoorden door naar de aanroeper terug te keren (zie rg_pe_hook_dll.c) */
    int uitgang_aanwezig;        /* er staat een [uitgang]-sectie in de lijst */
    int uitgang_actief;
    char uitgang_export[96];     /* "kernel32.dll!ExitProcess" */
    uint32_t uitgang_terug_bij[RG_UITGANG_MAX];   /* RVA's in de hoofdmodule */
    int uitgang_terug_n;
    int uitgang_max;             /* hoogstens zoveel keer terugkeren; daarna doorlaten */
    volatile int uitgang_staat;  /* door de DLL gezet: 1 = haak staat */
    volatile long uitgang_terug; /* door de DLL bijgehouden */
    volatile long uitgang_door;
    int scheiding_gedaan;
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
   Geeft het aantal aangehaakte functies terug, of -1 bij een fatale fout. (= voorbereid + wacht + pas_toe) */
int rg_pe_hook(rg_pe_ctx *c);

/* Stap 1: lijst lezen, PE-kop, bewijsartefact openen, trigger-adres uitrekenen. Idempotent.
   0 = klaar, 1 = lijst afgekeurd (verkeerde build), -1 = fataal. Veilig vanuit DllMain. */
int rg_pe_voorbereid(rg_pe_ctx *c);
/* De run-scheiding (tijd, pid, tid, tick) bovenaan het logboek. Eenmalig per ctx. */
void rg_pe_scheiding(rg_pe_ctx *c);
/* Stap 2 (alleen de poll-route): wacht tot alle ACTIEVE functieregels uitgepakt zijn, of wacht_s verstreken. */
void rg_pe_wacht(rg_pe_ctx *c);
/*
 * Stap 3: keur en pas ALLES toe (functiehaken + byte-patches), precies eenmaal per ctx. `bron` is
 * "trigger", "poll" of "FALLBACK" en komt in het logboek. -2 = een ander was al bezig of klaar.
 */
int rg_pe_pas_toe(rg_pe_ctx *c, const char *bron);
/* Eenmalig-vlag: 1 als de aanroeper hem heeft gekregen. Thread-safe (compare-and-swap). */
int rg_pe_claim(rg_pe_ctx *c);

/* De enige weg waarlangs de engine in het beeld schrijft: zet de pagina schrijfbaar (VirtualProtect),
   schrijft, zet de oude bescherming terug en meldt een mislukking met de systeemfout in het logboek.
   1 = gelukt, 0 = mislukt. Op Linux (toets) mprotect zonder terugzetten van de oude rechten. */
int rg_pe_schrijf(rg_pe_ctx *c, uintptr_t adres, const void *bytes, size_t n, const char *wie);

/* Leest een bytelijst "48 83 ec 28", met "00*26" voor 26 keer 00. Aantal bytes, of -1 bij een fout. */
int rg_pe_lees_bytes(const char *s, unsigned char *uit, int max);

/* --- NtProtectVirtualMemory-omleiding (zuivere bytefuncties, toetsbaar op Linux) --- */
/* 1 als de 16 bytes het verwachte begin zijn: 4c 8b d1 b8 <imm32> f6 04 25 08 03 fe 7f 01 */
int rg_pe_nt_stub_ok(const unsigned char *f16);
/* De 14-byte absolute sprong ff 25 00000000 <doel>. */
void rg_pe_maak_sprong14(unsigned char *uit14, uint64_t doel);
/* De trampoline: de 16 gekopieerde bytes + sprong14 naar `terug` (= functie + 16). 30 bytes. */
void rg_pe_maak_trampoline(unsigned char *uit30, const unsigned char *f16, uint64_t terug);

/* --- [uitgang] (ExitProcess) --- */
/* 1 als het begin van ExitProcess `48 83 ec 28 ff 15` is en bytes 10..15 opvulling (90/cc) zijn. */
int rg_pe_exit_stub_ok(const unsigned char *f16);
/* De 12-byte sprong 48 b8 <imm64> ff e0. */
void rg_pe_maak_haak12(unsigned char *uit12, uint64_t doel);
/* Besluit voor een ExitProcess-aanroep: 1 = terugkeren, 0 = doorlaten. `ra_rva` = terugkeeradres min
   ImageBase, `eerder_terug` = hoeveel keer al teruggekeerd (dit geteld NIET mee). */
int rg_pe_uitgang_besluit(const rg_pe_ctx *c, uint64_t ra_rva, long eerder_terug);

/* Zet elke omhul-hook terug: de bewaarde originele bytes gaan weer op hun plaats.
   Geeft het aantal teruggezette functies terug. */
int rg_pe_herstel(rg_pe_ctx *c);

#endif /* RG_PE_HOOK_H */
