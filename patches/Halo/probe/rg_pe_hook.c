/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_pe_hook.c — een generieke inline-hook-engine voor PE-beelden (Windows x64), aangedreven door
 * een PER-GAME lijst. Geen game-specifieke code: alles komt uit het ini-bestand.
 *
 * WAAROM DIT NAST tools/rt-hasphook/hasp_inline_hook.c STAAT
 * ---------------------------------------------------------
 * Die hook is de ELF-kant: hij leest een ELF van schijf, vindt het beeld via /proc/self/maps en
 * leidt de aanroepen naar de HASP-emulator. Luigi's Mansion is een PE (VACUUM.exe) onder Wine, en
 * de aanroepen moeten niet naar een emulator maar naar een ANTWOORD dat per functie in een
 * gegevensbestand staat. De vorm is hetzelfde (adreslijst, keuring per adres, luid weigeren), de
 * aanhaakvorm is anders (een gegenereerde stub per regel in plaats van een vaste vervanger).
 *
 * WAT HET DOET
 * ------------
 *   1. leest de per-game lijst (secties met `rva`, `antwoord`, `bytes`, `notitie`);
 *   2. leest de PE-kop uit het BEELD zelf (offset == RVA), dus geen bestandsparsering: dit werkt
 *      zowel op de eigen module van een DLL als op een geheugendump;
 *   3. keurt elk adres: valt de RVA in een UITVOERBAAR deel, binnen VirtualSize, en staat er de
 *      verwachte prologue? Een adres dat al een sprong is (bijvoorbeeld omdat een andere kern het
 *      al heeft omgezet) wordt LUID geweigerd, niet blind gepatcht;
 *   4. genereert per regel een stub in EEN uitvoerbare pagina, met het antwoord erin GEBAKKEN
 *      (geen dispatcher, geen register-geschuif: de stub is de implementatie);
 *   5. zet de 5-byte prologue om naar `e9 rel32` naar die stub, of naar een 12-byte absolute sprong
 *      als de stub niet binnen 2 GB ligt.
 *
 * ANTWOORDVORMEN (het `antwoord`-veld). Alles wat nog niet gemeten is staat als `TODO` in de lijst;
 * een TODO-stub meldt zich luid bij elke aanroep en geeft 0 terug, zodat een run laat zien WAT er
 * nog ontbreekt in plaats van stil met verzonnen data verder te lopen.
 *
 *   ret0            -> xor eax,eax ; ret                     (0 terug)
 *   ret1            -> mov eax,1 ; ret
 *   ret=<imm32>     -> mov eax,<imm32> ; ret                 (ook negatief: ret=-4)
 *   void            -> ret                                   (niets doen)
 *   buf=<reg>:<hex> -> schrijf de bytes naar de pointer in <reg>, dan 0 terug
 *                      <reg> is een van rcx rdx r8 r9 (de Windows x64-argumentregisters)
 *   u32=<reg>:<imm> -> schrijf een 32-bits waarde naar de pointer in <reg>, dan 0 terug
 *   TODO            -> meld de naam luid en geef 0 terug
 *
 * NIET ondersteund (en dat is met opzet): een `memset`-vorm, en alles wat het dongle-object zelf
 * moet bijwerken. Die velden staan in het spel op vaste globalen (`[0x14049cc68]`), en het
 * bijwerken ervan hoort in een aparte, meetbare stap -- niet in een stub die niets van de
 * spellogica weet. Zulke regels krijgen `antwoord = TODO` met de reden in `notitie`.
 */
#include "rg_pe_hook.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#define RG_PAGINA_GROOTTE 4096u

/* ------------------------------------------------------------------ kleine hulpmiddelen */

static void rg_log(rg_pe_ctx *c, const char *fmt, ...)
{
    va_list ap;
    if (!c->log)
        return;
    fprintf(c->log, "[rg-pe-hook] ");
    va_start(ap, fmt);
    vfprintf(c->log, fmt, ap);
    va_end(ap);
    fputc('\n', c->log);
    fflush(c->log);
}

static int rg_hex(const char *s, uint64_t *uit)
{
    char *eind = NULL;
    unsigned long long v;
    if (!s || !*s)
        return 0;
    v = strtoull(s, &eind, 0);
    if (eind == s)
        return 0;
    *uit = (uint64_t)v;
    return 1;
}

/* ------------------------------------------------------------------ de PE-kop uit het beeld */

static uint16_t rg_u16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rg_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rg_u64(const unsigned char *p)
{
    return (uint64_t)rg_u32(p) | ((uint64_t)rg_u32(p + 4) << 32);
}

int rg_pe_lees_kop(const unsigned char *beeld, size_t n, rg_pe_kop *k)
{
    uint32_t lfanew, optsz;
    uint16_t magic;
    int i;
    memset(k, 0, sizeof *k);
    if (!beeld || n < 0x100 || beeld[0] != 'M' || beeld[1] != 'Z')
        return 0;
    lfanew = rg_u32(beeld + 0x3c);
    if (lfanew + 24 + 2 > n || memcmp(beeld + lfanew, "PE\0\0", 4) != 0)
        return 0;
    k->machine = rg_u16(beeld + lfanew + 4);
    k->aantal_secties = rg_u16(beeld + lfanew + 6);
    optsz = rg_u16(beeld + lfanew + 20);
    if (k->aantal_secties > RG_PE_MAX_SECTIES)
        return 0;
    if (lfanew + 24 + optsz > n)
        return 0;
    magic = rg_u16(beeld + lfanew + 24);
    if (magic == 0x20b) {
        const unsigned char *o = beeld + lfanew + 24;
        k->ingang = rg_u32(o + 16);
        k->imagebase = rg_u64(o + 24);
        k->sectie_uitlijning = rg_u32(o + 32);
        k->grootte_beeld = rg_u32(o + 56);
        k->grootte_kop = rg_u32(o + 60);
        k->subsystem = rg_u16(o + 68);
    } else if (magic == 0x10b) {
        const unsigned char *o = beeld + lfanew + 24;
        k->ingang = rg_u32(o + 16);
        k->imagebase = rg_u32(o + 28);
        k->sectie_uitlijning = rg_u32(o + 32);
        k->grootte_beeld = rg_u32(o + 56);
        k->grootte_kop = rg_u32(o + 60);
        k->subsystem = rg_u16(o + 68);
    } else {
        return 0;
    }
    for (i = 0; i < k->aantal_secties; i++) {
        const unsigned char *s = beeld + lfanew + 24 + optsz + i * 40;
        rg_pe_sectie *d = &k->secties[i];
        if ((size_t)(s - beeld) + 40 > n)
            return 0;
        memcpy(d->naam, s, 8);
        d->naam[8] = 0;
        d->vsize = rg_u32(s + 8);
        d->rva = rg_u32(s + 12);
        d->rawsize = rg_u32(s + 16);
        d->rawptr = rg_u32(s + 20);
        d->flags = rg_u32(s + 36);
    }
    return 1;
}

/* De sectie waarin deze RVA valt, of NULL. */
static const rg_pe_sectie *rg_sectie_van(const rg_pe_kop *k, uint32_t rva)
{
    int i;
    for (i = 0; i < k->aantal_secties; i++) {
        const rg_pe_sectie *s = &k->secties[i];
        uint32_t grens = s->vsize ? s->vsize : s->rawsize;
        if (rva >= s->rva && rva < s->rva + grens)
            return s;
    }
    return NULL;
}

/* ------------------------------------------------------------------ de prologuekeuring */

/*
 * De verwachte prologue. `standaard` is dezelfde conservatieve lijst als de ELF-hook gebruikt: een
 * normale functieprologue. `e9rel32` verwacht juist een 5-byte sprong (de toestand die de gesloten
 * kern achterlaat). Een expliciete `bytes=` in de lijst gaat boven allebei.
 */
static int rg_proloog_ok(const unsigned char *p, size_t n, int vorm)
{
    if (n < 5)
        return 0;
    if (vorm == RG_PROLOOG_E9REL32)
        return p[0] == 0xe9;
    /* standaard */
    if (p[0] == 0x55 || p[0] == 0x53 || p[0] == 0x56 || p[0] == 0x57)
        return 1;
    if (p[0] == 0x41 && (p[1] == 0x54 || p[1] == 0x55 || p[1] == 0x56 || p[1] == 0x57))
        return 1;
    if (p[0] == 0x40 && (p[1] == 0x53 || p[1] == 0x55 || p[1] == 0x56 || p[1] == 0x57))
        return 1;
    if (p[0] == 0xf3 && p[1] == 0x0f && p[2] == 0x1e && p[3] == 0xfa)
        return 1;                                        /* endbr64 */
    if (p[0] == 0x48 && p[1] == 0x83 && p[2] == 0xec)
        return 1;                                        /* sub rsp,imm8 */
    if (p[0] == 0x48 && p[1] == 0x81 && p[2] == 0xec)
        return 1;                                        /* sub rsp,imm32 */
    if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x5c && p[3] == 0x24)
        return 1;                                        /* mov [rsp+x],rbx */
    if (p[0] == 0x48 && p[1] == 0x8b && p[2] == 0xc4)
        return 1;                                        /* mov rax,rsp */
    return 0;
}

/* ------------------------------------------------------------------ de ini-lijst */

/*
 * `antwoord = ...` omzetten naar een vorm. Onbekende tekst wordt TODO: dat is de veilige kant --
 * de stub meldt zich dan luid in plaats van stil iets te doen wat niemand heeft gemeten.
 */
int rg_antwoord_soort(const char *waarde, rg_pe_regel *r)
{
    r->ret_waarde = 0;
    r->u32_waarde = 0;
    r->buf_reg[0] = 0;
    if (!strcmp(waarde, "ret0"))
        return RG_ANTWOORD_RET0;
    if (!strcmp(waarde, "ret1"))
        return RG_ANTWOORD_RET1;
    if (!strcmp(waarde, "void"))
        return RG_ANTWOORD_VOID;
    if (!strncmp(waarde, "ret=", 4)) {
        r->ret_waarde = (int32_t)strtol(waarde + 4, NULL, 0);
        return RG_ANTWOORD_RET;
    }
    if (!strncmp(waarde, "buf=", 4)) {
        const char *dub = strchr(waarde + 4, ':');
        unsigned int b[16];
        int k = 0;
        const char *q;
        if (!dub)
            return RG_ANTWOORD_TODO;
        snprintf(r->buf_reg, sizeof r->buf_reg, "%.*s", (int)(dub - (waarde + 4)), waarde + 4);
        q = dub + 1;
        while (k < 16 && *q) {
            while (*q == ' ' || *q == '\t')
                q++;
            if (!*q)
                break;
            if (sscanf(q, "%x", &b[k]) != 1)
                break;
            k++;
            while (*q && *q != ' ' && *q != '\t')
                q++;
        }
        r->bytes_n = k;
        for (int i = 0; i < k; i++)
            r->bytes[i] = (unsigned char)b[i];
        r->heeft_bytes = k > 0;
        return k ? RG_ANTWOORD_BUF : RG_ANTWOORD_TODO;
    }
    if (!strncmp(waarde, "u32=", 4)) {
        const char *dub = strchr(waarde + 4, ':');
        if (!dub)
            return RG_ANTWOORD_TODO;
        snprintf(r->buf_reg, sizeof r->buf_reg, "%.*s", (int)(dub - (waarde + 4)), waarde + 4);
        r->u32_waarde = (uint32_t)strtoul(dub + 1, NULL, 0);
        return RG_ANTWOORD_U32;
    }
    return RG_ANTWOORD_TODO;
}

static void rg_regel_schoon(char *s)
{
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
}

static int rg_lees_lijst(rg_pe_ctx *c, const char *pad)
{
    FILE *f = fopen(pad, "r");
    char regel[1024];
    rg_pe_regel *nu = NULL;
    if (!f) {
        rg_log(c, "adreslijst %s is niet te lezen", pad);
        return 0;
    }
    snprintf(c->lijst_pad_bewaard, sizeof c->lijst_pad_bewaard, "%s", pad);
    while (fgets(regel, sizeof regel, f)) {
        char *p = regel, *e, *naam, *waarde;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == 0)
            continue;
        if (*p == '[') {                                  /* nieuwe regel */
            e = strchr(p, ']');
            if (!e)
                continue;
            *e = 0;
            if (c->n >= RG_PE_MAX_REGELS) {
                rg_log(c, "lijst: meer dan %d regels; de rest wordt genegeerd", RG_PE_MAX_REGELS);
                break;
            }
            nu = &c->regels[c->n++];
            memset(nu, 0, sizeof *nu);
            snprintf(nu->naam, sizeof nu->naam, "%s", p + 1);
            nu->antwoord = RG_ANTWOORD_TODO;
            nu->heeft_bytes = 0;
            continue;
        }
        naam = p;
        e = strpbrk(p, "=:");
        if (!e)
            continue;
        *e = 0;
        waarde = e + 1;
        while (*waarde == ' ' || *waarde == '\t' || *waarde == '=' || *waarde == ':')
            waarde++;
        rg_regel_schoon(naam);
        rg_regel_schoon(waarde);
        if (!*naam || !*waarde)
            continue;
        /* lijstbrede sleutels */
        if (!strcmp(naam, "beeld")) {
            c->beeld_vorm = strcmp(waarde, "pe") == 0 ? RG_BEELD_PE : RG_BEELD_ELF;
            continue;
        }
        if (!strcmp(naam, "proloog")) {
            c->proloog = strcmp(waarde, "e9rel32") == 0 ? RG_PROLOOG_E9REL32 : RG_PROLOOG_STANDAARD;
            continue;
        }
        if (!strcmp(naam, "basis")) {
            c->basis_beeld = (strcmp(waarde, "beeld") == 0);
            continue;
        }
        if (!strcmp(naam, "bewaar_args")) {
            c->bewaar_args = (strcmp(waarde, "1") == 0 || strcmp(waarde, "ja") == 0);
            continue;
        }
        if (!strcmp(naam, "wacht_s")) {
            c->wacht_s = atoi(waarde);
            continue;
        }
        if (!strcmp(naam, "wacht_ms")) {
            c->wacht_ms = atoi(waarde);
            continue;
        }
        if (!strcmp(naam, "module")) {
            if (nu)
                snprintf(nu->module, sizeof nu->module, "%s", waarde);
            continue;
        }
        if (!strcmp(naam, "export")) {
            if (nu) {
                snprintf(nu->export, sizeof nu->export, "%s", waarde);
                nu->is_export = 1;
            }
            continue;
        }
        if (!strcmp(naam, "grootte")) {
            rg_hex(waarde, &c->verwacht_grootte);
            continue;
        }
        if (!strcmp(naam, "ingang")) {
            rg_hex(waarde, &c->verwacht_ingang);
            continue;
        }
        if (!strcmp(naam, "game") || !strcmp(naam, "arch"))
            continue;
        if (!nu)
            continue;
        if (!strcmp(naam, "rva") || !strcmp(naam, "adres")) {
            uint64_t v = 0;
            if (rg_hex(waarde, &v)) {
                nu->adres = v;
                nu->is_rva = !strcmp(naam, "rva");
            }
            continue;
        }
        if (!strcmp(naam, "notitie")) {
            snprintf(nu->notitie, sizeof nu->notitie, "%s", waarde);
            continue;
        }
        if (!strcmp(naam, "bytes")) {
            unsigned int b[16];
            int k = 0;
            const char *q = waarde;
            while (k < 16 && *q) {
                while (*q == ' ' || *q == '\t')
                    q++;
                if (!*q)
                    break;
                if (sscanf(q, "%x", &b[k]) != 1)
                    break;
                k++;
                while (*q && *q != ' ' && *q != '\t')
                    q++;
            }
            nu->verwacht_n = k;
            for (int i = 0; i < k; i++)
                nu->verwacht_bytes[i] = (unsigned char)b[i];
            nu->heeft_verwacht = k > 0;
            continue;
        }
        if (!strcmp(naam, "antwoord")) {
            nu->antwoord = rg_antwoord_soort(waarde, nu);
            continue;
        }
    }
    fclose(f);
    return c->n;
}

/* ------------------------------------------------------------------ de stubs */

/* De register-modrm voor [reg] (Windows x64-argumentregisters). */
static int rg_modrm(const char *reg, unsigned char *uit)
{
    if (!strcmp(reg, "rcx")) { *uit = 0x01; return 1; }
    if (!strcmp(reg, "rdx")) { *uit = 0x02; return 1; }
    if (!strcmp(reg, "r8"))  { *uit = 0x00; return 2; }   /* met REX.B: 41 C6 00 xx */
    if (!strcmp(reg, "r9"))  { *uit = 0x01; return 2; }
    return 0;
}

typedef struct {
    unsigned char b[256];
    int n;
} rg_buf;

static void rg_em(rg_buf *d, unsigned char x)
{
    if (d->n < (int)sizeof d->b)
        d->b[d->n++] = x;
}
static void rg_em32(rg_buf *d, uint32_t v)
{
    rg_em(d, (unsigned char)(v & 0xff));
    rg_em(d, (unsigned char)((v >> 8) & 0xff));
    rg_em(d, (unsigned char)((v >> 16) & 0xff));
    rg_em(d, (unsigned char)((v >> 24) & 0xff));
}
static void rg_em64(rg_buf *d, uint64_t v)
{
    rg_em32(d, (uint32_t)v);
    rg_em32(d, (uint32_t)(v >> 32));
}

/*
 * De stub. `log_idx` is de index in c->regels, of -1 als de stub niet hoeft te loggen; hij wordt
 * alleen gebruikt door de TODO-vorm, waar het clobberen van het eerste argumentregister niets
 * kwaad kan (die vorm leest geen argumenten).
 */
static int rg_maak_stub(rg_pe_ctx *c, rg_pe_regel *r, int idx, rg_buf *uit)
{
    uit->n = 0;
    /*
     * Argumenten bewaren, als de lijst dat vraagt. De gesloten kern van Luigi doet dit in ELKE
     * implementatie: de argumentregisters gaan naar de home space van de aanroeper ([rsp+8] ..
     * [rsp+0x20]) voordat er iets anders gebeurt. Dit staat vóór de vorm, want geen enkele vorm
     * mag een argumentregister kwijtraken voordat het bewaard is.
     */
    if (c->bewaar_args) {
        rg_em(uit, 0x48); rg_em(uit, 0x89); rg_em(uit, 0x4c); rg_em(uit, 0x24); rg_em(uit, 0x08);
        rg_em(uit, 0x48); rg_em(uit, 0x89); rg_em(uit, 0x54); rg_em(uit, 0x24); rg_em(uit, 0x10);
        rg_em(uit, 0x4c); rg_em(uit, 0x89); rg_em(uit, 0x44); rg_em(uit, 0x24); rg_em(uit, 0x18);
        rg_em(uit, 0x4c); rg_em(uit, 0x89); rg_em(uit, 0x4c); rg_em(uit, 0x24); rg_em(uit, 0x20);
    }
    switch (r->antwoord) {
    case RG_ANTWOORD_RET0:
        rg_em(uit, 0x31); rg_em(uit, 0xc0); rg_em(uit, 0xc3);           /* xor eax,eax; ret */
        return 1;
    case RG_ANTWOORD_RET1:
        rg_em(uit, 0xb8); rg_em32(uit, 1); rg_em(uit, 0xc3);
        return 1;
    case RG_ANTWOORD_RET:
        rg_em(uit, 0xb8); rg_em32(uit, r->ret_waarde); rg_em(uit, 0xc3);
        return 1;
    case RG_ANTWOORD_VOID:
        rg_em(uit, 0xc3);
        return 1;
    case RG_ANTWOORD_BUF: {
        unsigned char modrm;
        int rex = rg_modrm(r->buf_reg, &modrm);
        if (!rex) {
            rg_log(c, "  %s: onbekend argumentregister '%s' (rcx rdx r8 r9)", r->naam, r->buf_reg);
            return 0;
        }
        for (int i = 0; i < r->bytes_n; i++) {                          /* mov byte [reg+i], imm8 */
            if (rex == 2)
                rg_em(uit, 0x41);
            rg_em(uit, 0xc6);
            rg_em(uit, (unsigned char)(modrm + (i ? 0x40 : 0)));
            if (i)
                rg_em(uit, (unsigned char)i);
            rg_em(uit, r->bytes[i]);
        }
        rg_em(uit, 0x31); rg_em(uit, 0xc0); rg_em(uit, 0xc3);
        return 1;
    }
    case RG_ANTWOORD_U32: {
        unsigned char modrm;
        int rex = rg_modrm(r->buf_reg, &modrm);
        if (!rex)
            return 0;
        if (rex == 2)
            rg_em(uit, 0x41);
        rg_em(uit, 0xc7);
        rg_em(uit, modrm);
        rg_em32(uit, r->u32_waarde);
        rg_em(uit, 0x31); rg_em(uit, 0xc0); rg_em(uit, 0xc3);
        return 1;
    }
    case RG_ANTWOORD_TODO:
    default:
        /* De aanroep van de melder moet de ABI volgen: schaduwruimte en uitlijning. */
#if defined(_WIN32)
        rg_em(uit, 0x48); rg_em(uit, 0x83); rg_em(uit, 0xec); rg_em(uit, 0x28); /* sub rsp,0x28 */
        rg_em(uit, 0x48); rg_em(uit, 0xb9); rg_em64(uit, (uint64_t)(uintptr_t)idx); /* mov rcx,idx */
#else
        rg_em(uit, 0x48); rg_em(uit, 0x83); rg_em(uit, 0xec); rg_em(uit, 0x08); /* sub rsp,8 */
        rg_em(uit, 0x48); rg_em(uit, 0xbf); rg_em64(uit, (uint64_t)(uintptr_t)idx); /* mov rdi,idx */
#endif
        rg_em(uit, 0x48); rg_em(uit, 0xb8);
        rg_em64(uit, (uint64_t)(uintptr_t)c->todo_melder);                          /* mov rax,melder */
        rg_em(uit, 0xff); rg_em(uit, 0xd0);                                         /* call rax */
#if defined(_WIN32)
        rg_em(uit, 0x48); rg_em(uit, 0x83); rg_em(uit, 0xc4); rg_em(uit, 0x28); /* add rsp,0x28 */
#else
        rg_em(uit, 0x48); rg_em(uit, 0x83); rg_em(uit, 0xc4); rg_em(uit, 0x08); /* add rsp,8 */
#endif
        rg_em(uit, 0x31); rg_em(uit, 0xc0); rg_em(uit, 0xc3);                       /* xor eax,eax; ret */
        return 1;
    }
}

/* ------------------------------------------------------------------ tijd */

static uint64_t rg_nu_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)(t.tv_nsec / 1000000);
#endif
}

static void rg_slaap_ms(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
#endif
}

/* Staat hier nog niets? Dan heeft de pakker deze pagina nog niet geschreven. */
static int rg_alles_nul(const unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i])
            return 0;
    return 1;
}

/*
 * De keuring van EEN regel. Zet `geldig`, of `wacht` als de pagina nog niet is uitgepakt, of een
 * reden waarom het adres definitief wordt afgewezen. Apart gehouden omdat de wachtroute hem
 * opnieuw moet kunnen aanroepen zonder de rest van de hoofdroute te herhalen.
 */
static void rg_keur_een(rg_pe_ctx *c, const rg_pe_kop *kop, rg_pe_regel *r)
{
    const rg_pe_sectie *s;
    uint32_t rva;
    const unsigned char *p;
    r->geldig = 0;
    r->wacht = 0;
    if (!r->adres && !r->is_export) {
        snprintf(r->reden, sizeof r->reden, "AFGEWEZEN: geen adres en geen exportnaam in de lijst");
        return;
    }
    rva = r->is_rva ? (uint32_t)r->adres
                    : (uint32_t)(r->adres - (c->basis_beeld ? kop->imagebase : 0));
    r->rva = rva;
    r->doel_adres = (uintptr_t)(kop->imagebase + rva);
    s = rg_sectie_van(kop, rva);
    if (!s) {
        snprintf(r->reden, sizeof r->reden,
                 "AFGEWEZEN: RVA 0x%x valt in geen sectie van het beeld", rva);
        return;
    }
    if (!(s->flags & 0x20000000)) {
        snprintf(r->reden, sizeof r->reden,
                 "AFGEWEZEN: sectie %s is niet uitvoerbaar (IMAGE_SCN_MEM_EXECUTE ontbreekt)",
                 s->naam);
        return;
    }
    if ((size_t)rva + 12 > c->beeld_n) {
        snprintf(r->reden, sizeof r->reden,
                 "AFGEWEZEN: RVA 0x%x valt buiten het beeld (%zu bytes)", rva, c->beeld_n);
        return;
    }
    p = c->beeld + rva;
    if (rg_alles_nul(p, 5)) {
        /* Nog niets: de pakker heeft deze pagina nog niet geschreven. Geen fout, wel te vroeg. */
        r->wacht = 1;
        snprintf(r->reden, sizeof r->reden,
                 "NOG NIET UITGEPAKT: de prologue op RVA 0x%x is nog nul", rva);
        return;
    }
    if (r->heeft_verwacht) {
        if (memcmp(p, r->verwacht_bytes, (size_t)r->verwacht_n) != 0) {
            snprintf(r->reden, sizeof r->reden,
                     "AFGEWEZEN: eerste bytes %02x %02x %02x %02x komen niet overeen met de "
                     "lijst (%02x %02x %02x %02x)", p[0], p[1], p[2], p[3],
                     r->verwacht_bytes[0], r->verwacht_n > 1 ? r->verwacht_bytes[1] : 0,
                     r->verwacht_n > 2 ? r->verwacht_bytes[2] : 0,
                     r->verwacht_n > 3 ? r->verwacht_bytes[3] : 0);
            return;
        }
    } else if (!rg_proloog_ok(p, 5, c->proloog)) {
        if (p[0] == 0xe9)
            snprintf(r->reden, sizeof r->reden,
                     "AFGEWEZEN: eerste bytes %02x %02x %02x %02x zijn geen prologue maar een "
                     "sprong -- al door een andere kern omgezet?", p[0], p[1], p[2], p[3]);
        else
            snprintf(r->reden, sizeof r->reden,
                     "AFGEWEZEN: eerste bytes %02x %02x %02x %02x zijn geen herkenbare "
                     "functieprologue", p[0], p[1], p[2], p[3]);
        return;
    }
    r->geldig = 1;
    snprintf(r->reden, sizeof r->reden,
             "GELDIG: sectie %s (RVA 0x%x, runtime 0x%llx), prologue %02x %02x %02x %02x",
             s->naam, rva, (unsigned long long)r->doel_adres, p[0], p[1], p[2], p[3]);
}

/* ------------------------------------------------------------------ de hoofdroute */

int rg_pe_hook(rg_pe_ctx *c)
{
    rg_pe_kop kop;
    unsigned char *stub_pagina = NULL;
    size_t pag;
    int i, aangehaakt = 0, afgekeurd = 0, todo = 0, niet_gehaakt = 0;
    uintptr_t pagina_adres = 0;

    if (!c->beeld || !c->beeld_n) {
        rg_log(c, "geen beeld meegegeven");
        return -1;
    }
    if (!c->n && !rg_lees_lijst(c, c->lijst_pad)) {
        rg_log(c, "geen bruikbare regels in %s", c->lijst_pad);
        return -1;
    }
    if (!rg_pe_lees_kop(c->beeld, c->beeld_n, &kop)) {
        rg_log(c, "geen geldige PE-kop in het beeld (%zu bytes)", c->beeld_n);
        return -1;
    }
    rg_log(c, "PE: machine 0x%x, imagebase 0x%llx, SizeOfImage 0x%x, entry 0x%x, %d secties",
           kop.machine, (unsigned long long)kop.imagebase, kop.grootte_beeld, kop.ingang,
           kop.aantal_secties);
    rg_log(c, "adreslijst: %d regel(s) uit %s (beeld=%s, proloog=%s, basis=%s)", c->n,
           c->lijst_pad, c->beeld_vorm == RG_BEELD_PE ? "pe" : "elf",
           c->proloog == RG_PROLOOG_E9REL32 ? "e9rel32" : "standaard",
           c->basis_beeld ? "beeld" : "absoluut");

    if (c->verwacht_grootte && c->verwacht_grootte != (uint64_t)kop.grootte_beeld) {
        rg_log(c, "lijst AFGEKEURD: grootte 0x%llx in de lijst, maar SizeOfImage is 0x%x",
               (unsigned long long)c->verwacht_grootte, kop.grootte_beeld);
        for (i = 0; i < c->n; i++)
            snprintf(c->regels[i].reden, sizeof c->regels[i].reden,
                     "AFGEWEZEN: verkeerde build (SizeOfImage 0x%x != 0x%llx)",
                     kop.grootte_beeld, (unsigned long long)c->verwacht_grootte);
        return 0;
    }
    if (c->verwacht_ingang && c->verwacht_ingang != (uint64_t)kop.ingang) {
        rg_log(c, "lijst AFGEKEURD: ingang 0x%llx in de lijst, maar die van het beeld is 0x%x",
               (unsigned long long)c->verwacht_ingang, kop.ingang);
        for (i = 0; i < c->n; i++)
            snprintf(c->regels[i].reden, sizeof c->regels[i].reden,
                     "AFGEWEZEN: verkeerde build (entry 0x%x != 0x%llx)", kop.ingang,
                     (unsigned long long)c->verwacht_ingang);
        return 0;
    }

    /* keuring, met de wachtroute: een pagina die nog niet is uitgepakt is geen fout maar te vroeg.
       De prologues bestaan pas NADAT de pakker de code heeft weggeschreven (gemeten: onder onze kern
       van t=4,6 tot 4,7 s), dus de hook moet ze op dat moment kunnen omzetten en niet bij het laden. */
    {
        uint64_t t0 = rg_nu_ms();
        int ronde = 0, nog;
        for (;;) {
            nog = 0;
            for (i = 0; i < c->n; i++) {
                rg_keur_een(c, &kop, &c->regels[i]);
                if (c->regels[i].wacht)
                    nog++;
            }
            if (!nog || c->wacht_s <= 0)
                break;
            if (rg_nu_ms() - t0 > (uint64_t)c->wacht_s * 1000u) {
                rg_log(c, "wachtroute: %d pagina('s) na %d s nog steeds niet uitgepakt; gestopt",
                       nog, c->wacht_s);
                break;
            }
            if (!ronde)
                rg_log(c, "wachtroute: %d pagina('s) nog niet uitgepakt; ik blijf kijken (max %d s, "
                          "elke %d ms)", nog, c->wacht_s, c->wacht_ms > 0 ? c->wacht_ms : 10);
            ronde++;
            rg_slaap_ms(c->wacht_ms > 0 ? c->wacht_ms : 10);
        }
        if (nog == 0 && ronde > 0)
            rg_log(c, "wachtroute: de pagina's zijn uitgepakt na %llu ms; nu omzetten",
                   (unsigned long long)(rg_nu_ms() - t0));
    }
    /* de stubpagina: een uitvoerbare pagina. We proberen hem dicht bij het beeld te leggen, zodat
       de 5-byte `e9 rel32`-omzetting past; lukt dat niet, dan volgt een 12-byte absolute sprong. */
    pag = RG_PAGINA_GROOTTE;
#if defined(_WIN32)
    {
        uintptr_t wens = (uintptr_t)kop.imagebase - 0x10000;
        stub_pagina = (unsigned char *)VirtualAlloc((LPVOID)wens, pag,
                                                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!stub_pagina)
            stub_pagina = (unsigned char *)VirtualAlloc(NULL, pag, MEM_COMMIT | MEM_RESERVE,
                                                       PAGE_EXECUTE_READWRITE);
    }
#else
    {
        void *wens = (void *)(uintptr_t)(kop.imagebase - 0x10000);
        stub_pagina = mmap(wens, pag, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (stub_pagina == MAP_FAILED)
            stub_pagina = mmap(NULL, pag, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stub_pagina == MAP_FAILED)
            stub_pagina = NULL;
    }
#endif
    if (!stub_pagina) {
        rg_log(c, "GEEN uitvoerbare pagina voor de stubs; er wordt niets aangehaakt");
        return -1;
    }
    pagina_adres = (uintptr_t)stub_pagina;
    rg_log(c, "stubpagina op 0x%llx (%zu bytes)", (unsigned long long)pagina_adres, pag);

    /* per geldige regel een stub, en de prologue omzetten */
    for (i = 0; i < c->n; i++) {
        rg_pe_regel *r = &c->regels[i];
        rg_buf stub;
        uintptr_t plek;
        int64_t afstand;
        if (!r->geldig) {
            afgekeurd++;
            continue;
        }
        if (c->stub_gebruikt + 64 > (int)pag) {
            rg_log(c, "  %s: stubpagina vol; overgeslagen", r->naam);
            niet_gehaakt++;
            continue;
        }
        if (!rg_maak_stub(c, r, i, &stub)) {
            snprintf(r->reden, sizeof r->reden, "AFGEWEZEN: antwoordvorm kon niet worden gebouwd");
            r->geldig = 0;
            afgekeurd++;
            continue;
        }
        plek = pagina_adres + (uintptr_t)c->stub_gebruikt;
        memcpy((void *)plek, stub.b, (size_t)stub.n);
        c->stub_gebruikt += stub.n;

        afstand = (int64_t)plek - (int64_t)(r->doel_adres + 5);
        if (afstand >= -2147483648LL && afstand <= 2147483647LL) {
            unsigned char nieuw[12];
            int32_t rel = (int32_t)afstand;
            nieuw[0] = 0xe9;
            memcpy(nieuw + 1, &rel, 4);
            memcpy((void *)r->doel_adres, nieuw, 5);
            snprintf(r->wijze, sizeof r->wijze, "e9 rel32 (5 bytes)");
        } else {
            unsigned char nieuw[12];
            uint64_t doel = (uint64_t)plek;
            nieuw[0] = 0x48; nieuw[1] = 0xb8;
            memcpy(nieuw + 2, &doel, 8);
            nieuw[10] = 0xff; nieuw[11] = 0xe0;
            memcpy((void *)r->doel_adres, nieuw, 12);
            snprintf(r->wijze, sizeof r->wijze, "48 b8 abs64 ff e0 (12 bytes)");
        }
        r->gehaakt = 1;
        aangehaakt++;
        if (r->antwoord == RG_ANTWOORD_TODO)
            todo++;
        rg_log(c, "  aangehaakt: %-34s RVA 0x%06x -> stub 0x%llx  [%s]%s", r->naam, r->rva,
               (unsigned long long)plek, r->wijze,
               r->antwoord == RG_ANTWOORD_TODO ? "  ANTWOORD = TODO" : "");
    }

    /* het oordeel per regel, en de samenvatting in dezelfde vijf gevallen als de ELF-hook */
    for (i = 0; i < c->n; i++)
        rg_log(c, "  %-34s 0x%08x  %s", c->regels[i].naam, c->regels[i].rva, c->regels[i].reden);
    for (i = 0; i < c->n; i++) {
        rg_pe_regel *r = &c->regels[i];
        if (r->gehaakt)
            continue;
        if (!r->geldig)
            rg_log(c, "  niet aangehaakt: %-34s (in de adreslijst maar afgekeurd: %s)", r->naam,
                   r->reden + (strncmp(r->reden, "AFGEWEZEN: ", 11) == 0 ? 11 : 0));
        else
            rg_log(c, "  niet aangehaakt: %-34s (in de adreslijst en goedgekeurd, maar het "
                      "aanhaken lukte niet)", r->naam);
    }
    rg_log(c, "totaal aangehaakt: %d van %d; afgekeurd: %d; waarvan antwoord TODO: %d",
           aangehaakt, c->n, afgekeurd, todo);
    (void)niet_gehaakt;
    return aangehaakt;
}
