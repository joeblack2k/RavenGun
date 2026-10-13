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
#include <errno.h>

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

/*
 * Een bytelijst: "48 83 ec 28", of "00*26" voor 26 keer 00. Geeft het aantal bytes, of -1 bij een
 * fout (geen hex, te veel bytes). Een lege tekst geeft 0.
 */
int rg_pe_lees_bytes(const char *s, unsigned char *uit, int max)
{
    int k = 0;
    const char *q = s;
    while (q && *q) {
        char *eind = NULL;
        unsigned long v, herhaal = 1;
        while (*q == ' ' || *q == '\t' || *q == ',')
            q++;
        if (!*q)
            break;
        v = strtoul(q, &eind, 16);
        if (eind == q || v > 0xff)
            return -1;
        q = eind;
        if (*q == '*') {
            herhaal = strtoul(q + 1, &eind, 10);
            if (eind == q + 1 || herhaal == 0)
                return -1;
            q = eind;
        }
        if (*q && *q != ' ' && *q != '\t' && *q != ',')
            return -1;
        if (k + (int)herhaal > max)
            return -1;
        while (herhaal--)
            uit[k++] = (unsigned char)v;
    }
    return k;
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
    if (!strncmp(waarde, "velden=", 7)) {
        /*
         * velden=rcx:0x110=0x40,0x114=1
         * Zet u32's op vaste afstanden van het object dat in het eerste argument zit, en geef 0
         * terug. Alleen rcx wordt ondersteund (het eerste argument van de Windows x64-ABI); een
         * andere寄存器 wordt geweigerd in plaats van stil verkeerd gedaan.
         */
        char reg[8] = {0};
        const char *dub = strchr(waarde + 7, ':');
        const char *q;
        if (!dub)
            return RG_ANTWOORD_TODO;
        snprintf(reg, sizeof reg, "%.*s", (int)(dub - (waarde + 7)), waarde + 7);
        if (strcmp(reg, "rcx") != 0)
            return RG_ANTWOORD_TODO;   /* rdx/r8/r9: nog niet ondersteund */
        r->veld_reg = 0;
        r->veld_n = 0;
        q = dub + 1;
        while (*q && r->veld_n < 4) {
            unsigned int off = 0, val = 0;
            if (sscanf(q, "%x=%x", &off, &val) != 2)
                break;
            r->veld[r->veld_n].off = off;
            r->veld[r->veld_n].val = val;
            r->veld_n++;
            while (*q && *q != ',')
                q++;
            if (*q == ',')
                q++;
        }
        return r->veld_n ? RG_ANTWOORD_VELDEN : RG_ANTWOORD_TODO;
    }
    return RG_ANTWOORD_TODO;
}

static void rg_regel_schoon(char *s)
{
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
}

/* Een bool uit de lijst: 1/ja/true/aan. */
static int rg_bool(const char *w)
{
    return !strcmp(w, "1") || !strcmp(w, "ja") || !strcmp(w, "true") || !strcmp(w, "aan");
}

/*
 * Leest de lijst. Drie soorten secties:
 *   [naam]            een functiehaak (rva, antwoord, bytes, actief, notitie)
 *   [patch naam]      een gewone byte-patch (rva, verwacht, bytes | tekst, actief, notitie)
 *   [uitgang]         ExitProcess beantwoorden met een `ret` (export, terug_bij, max, actief)
 * Geeft het aantal regels (functies + patches) plus 1 als er een [uitgang] staat; 0 = niets bruikbaars.
 * Er is bewust GEEN commentaar achter een waarde: `#` en `;` gelden alleen aan het begin van een regel.
 */
static int rg_lees_lijst(rg_pe_ctx *c, const char *pad)
{
    FILE *f = fopen(pad, "r");
    char regel[1024];
    rg_pe_regel *nu = NULL;
    int sec = 0;                      /* 0 functie, 1 patch, 2 uitgang */
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
            const char *sn;
            e = strchr(p, ']');
            if (!e)
                continue;
            *e = 0;
            sn = p + 1;
            if (!strcmp(sn, "uitgang")) {
                sec = 2;
                nu = NULL;
                c->uitgang_aanwezig = 1;
                c->uitgang_actief = 1;
                if (c->uitgang_max <= 0)
                    c->uitgang_max = 4;
                continue;
            }
            if (c->n >= RG_PE_MAX_REGELS) {
                rg_log(c, "lijst: meer dan %d regels; de rest wordt genegeerd", RG_PE_MAX_REGELS);
                break;
            }
            nu = &c->regels[c->n++];
            memset(nu, 0, sizeof *nu);
            if (!strncmp(sn, "patch ", 6)) {
                sn += 6;
                while (*sn == ' ')
                    sn++;
                nu->is_patch = 1;
                sec = 1;
            } else {
                sec = 0;
            }
            snprintf(nu->naam, sizeof nu->naam, "%s", sn);
            nu->antwoord = RG_ANTWOORD_TODO;
            nu->heeft_bytes = 0;
            nu->actief = 1;
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
        if (sec == 2) {
            if (!strcmp(naam, "export")) {
                snprintf(c->uitgang_export, sizeof c->uitgang_export, "%s", waarde);
            } else if (!strcmp(naam, "terug_bij")) {
                const char *q = waarde;
                c->uitgang_terug_n = 0;
                while (*q && c->uitgang_terug_n < RG_UITGANG_MAX) {
                    char *eind = NULL;
                    unsigned long long v;
                    while (*q == ' ' || *q == '\t' || *q == ',')
                        q++;
                    if (!*q)
                        break;
                    v = strtoull(q, &eind, 0);
                    if (eind == q)
                        break;
                    c->uitgang_terug_bij[c->uitgang_terug_n++] = (uint32_t)v;
                    q = eind;
                }
            } else if (!strcmp(naam, "max")) {
                c->uitgang_max = atoi(waarde);
            } else if (!strcmp(naam, "actief")) {
                c->uitgang_actief = rg_bool(waarde);
            }
            continue;
        }
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
            c->bewaar_args = rg_bool(waarde);
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
        if (!strcmp(naam, "trigger")) {
            snprintf(c->trigger, sizeof c->trigger, "%s", waarde);
            continue;
        }
        if (!strcmp(naam, "trigger_sectie")) {
            snprintf(c->trigger_sectie, sizeof c->trigger_sectie, "%s", waarde);
            continue;
        }
        if (!strcmp(naam, "trigger_prot")) {
            uint64_t v = 0;
            if (rg_hex(waarde, &v))
                c->trigger_prot = (uint32_t)v;
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
        if (!strcmp(naam, "actief")) {
            nu->actief = rg_bool(waarde);
            continue;
        }
        if (!strcmp(naam, "verwacht") || (!strcmp(naam, "bytes") && !nu->is_patch)) {
            /* de verwachte bytes: bij een functie de prologue, bij een patch wat er NU moet staan */
            int k = rg_pe_lees_bytes(waarde, nu->verwacht_bytes, RG_PATCH_MAX);
            if (k < 0) {
                rg_log(c, "lijst: [%s] %s = %s is geen geldige bytelijst; genegeerd", nu->naam, naam, waarde);
            } else {
                nu->verwacht_n = k;
                nu->heeft_verwacht = k > 0;
            }
            continue;
        }
        if (nu->is_patch && !strcmp(naam, "bytes")) {
            int k = rg_pe_lees_bytes(waarde, nu->patch_bytes, RG_PATCH_MAX);
            if (k < 0) {
                rg_log(c, "lijst: [patch %s] bytes = %s is geen geldige bytelijst; genegeerd", nu->naam, waarde);
            } else {
                nu->patch_n = k;
                nu->patch_uit_tekst = 0;
            }
            continue;
        }
        if (nu->is_patch && !strcmp(naam, "tekst")) {
            size_t l = strlen(waarde);
            if (l >= 2 && waarde[0] == '"' && waarde[l - 1] == '"') {
                waarde[l - 1] = 0;
                waarde++;
                l -= 2;
            }
            if (l + 1 > RG_PATCH_MAX) {
                rg_log(c, "lijst: [patch %s] tekst is te lang (%zu tekens, hoogstens %d); genegeerd",
                       nu->naam, l, RG_PATCH_MAX - 1);
            } else {
                memcpy(nu->patch_bytes, waarde, l);
                nu->patch_bytes[l] = 0;                   /* de afsluitende NUL hoort erbij */
                nu->patch_n = (int)l + 1;
                nu->patch_uit_tekst = 1;
            }
            continue;
        }
        if (!strcmp(naam, "antwoord")) {
            const char *w = waarde;
            if (!strncmp(w, "log0", 4) && (w[4] == 0 || w[4] == ' ' || w[4] == '\t')) {
                nu->log_aanroep = 1;
                w += 4;
                while (*w == ' ' || *w == '\t')
                    w++;
                if (!*w)
                    w = "ret0";
            }
            nu->antwoord = rg_antwoord_soort(w, nu);
            continue;
        }
    }
    fclose(f);
    return c->n + (c->uitgang_aanwezig ? 1 : 0);
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
    unsigned char b[512];
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

/* De vier argumentregisters naar de schaduwruimte van de aanroeper: [rsp+8] .. [rsp+0x20]. */
static void rg_em_bewaar(rg_buf *d)
{
    static const unsigned char b[20] = {
        0x48, 0x89, 0x4c, 0x24, 0x08,      /* mov [rsp+8],rcx    */
        0x48, 0x89, 0x54, 0x24, 0x10,      /* mov [rsp+0x10],rdx */
        0x4c, 0x89, 0x44, 0x24, 0x18,      /* mov [rsp+0x18],r8  */
        0x4c, 0x89, 0x4c, 0x24, 0x20       /* mov [rsp+0x20],r9  */
    };
    int i;
    for (i = 0; i < 20; i++)
        rg_em(d, b[i]);
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
     * log0: voor het antwoord de aanroep loggen (index, wijzer naar de vier bewaarde argumentregisters,
     * terugkeeradres). Dat vereist dat de argumenten in de schaduwruimte staan, ook als de lijst
     * `bewaar_args` niet vraagt; en na de logaanroep zijn rcx/rdx/r8/r9 vluchtig weg, dus ze worden
     * teruggelezen voor de antwoordvormen die er een wijzer uit nemen (buf=, u32=, velden=).
     */
    if (r->log_aanroep && c->log_melder && !c->bewaar_args)
        rg_em_bewaar(uit);
    /*
     * Argumenten bewaren, als de lijst dat vraagt. De gesloten kern van Luigi doet dit in ELKE
     * implementatie: de argumentregisters gaan naar de home space van de aanroeper ([rsp+8] ..
     * [rsp+0x20]) voordat er iets anders gebeurt. Dit staat vóór de vorm, want geen enkele vorm
     * mag een argumentregister kwijtraken voordat het bewaard is.
     */
    if (c->bewaar_args)
        rg_em_bewaar(uit);
    if (r->log_aanroep && c->log_melder) {
#if defined(_WIN32)
        rg_em(uit, 0x48); rg_em(uit, 0x83); rg_em(uit, 0xec); rg_em(uit, 0x28);        /* sub rsp,0x28 */
        rg_em(uit, 0x48); rg_em(uit, 0xb9); rg_em64(uit, (uint64_t)(uintptr_t)idx);   /* mov rcx,idx */
        rg_em(uit, 0x48); rg_em(uit, 0x8d); rg_em(uit, 0x54); rg_em(uit, 0x24); rg_em(uit, 0x30); /* lea rdx,[rsp+0x30] */
        rg_em(uit, 0x4c); rg_em(uit, 0x8b); rg_em(uit, 0x44); rg_em(uit, 0x24); rg_em(uit, 0x28); /* mov r8,[rsp+0x28] */
        rg_em(uit, 0x48); rg_em(uit, 0xb8); rg_em64(uit, (uint64_t)(uintptr_t)c->log_melder); /* mov rax,melder */
        rg_em(uit, 0xff); rg_em(uit, 0xd0);                                             /* call rax */
        rg_em(uit, 0x48); rg_em(uit, 0x83); rg_em(uit, 0xc4); rg_em(uit, 0x28);        /* add rsp,0x28 */
#else
        rg_em(uit, 0x48); rg_em(uit, 0x83); rg_em(uit, 0xec); rg_em(uit, 0x08);        /* sub rsp,8 */
        rg_em(uit, 0x48); rg_em(uit, 0xbf); rg_em64(uit, (uint64_t)(uintptr_t)idx);   /* mov rdi,idx */
        rg_em(uit, 0x48); rg_em(uit, 0x8d); rg_em(uit, 0x74); rg_em(uit, 0x24); rg_em(uit, 0x10); /* lea rsi,[rsp+0x10] */
        rg_em(uit, 0x48); rg_em(uit, 0x8b); rg_em(uit, 0x54); rg_em(uit, 0x24); rg_em(uit, 0x08); /* mov rdx,[rsp+8] */
        rg_em(uit, 0x48); rg_em(uit, 0xb8); rg_em64(uit, (uint64_t)(uintptr_t)c->log_melder);
        rg_em(uit, 0xff); rg_em(uit, 0xd0);
        rg_em(uit, 0x48); rg_em(uit, 0x83); rg_em(uit, 0xc4); rg_em(uit, 0x08);        /* add rsp,8 */
#endif
        /* de argumentregisters terug uit de schaduwruimte */
        rg_em(uit, 0x48); rg_em(uit, 0x8b); rg_em(uit, 0x4c); rg_em(uit, 0x24); rg_em(uit, 0x08);
        rg_em(uit, 0x48); rg_em(uit, 0x8b); rg_em(uit, 0x54); rg_em(uit, 0x24); rg_em(uit, 0x10);
        rg_em(uit, 0x4c); rg_em(uit, 0x8b); rg_em(uit, 0x44); rg_em(uit, 0x24); rg_em(uit, 0x18);
        rg_em(uit, 0x4c); rg_em(uit, 0x8b); rg_em(uit, 0x4c); rg_em(uit, 0x24); rg_em(uit, 0x20);
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
    case RG_ANTWOORD_VELDEN: {
        /*
         * Per veld: mov dword ptr [rcx+disp32], imm32  =  c7 81 <off32> <val32>   (10 bytes, geen REX
         * nodig omdat rcx de basis is en er 32 bits geschreven wordt). Daarna 0 terug.
         */
        for (int i = 0; i < r->veld_n; i++) {
            rg_em(uit, 0xc7);
            rg_em(uit, 0x81);
            rg_em32(uit, r->veld[i].off);
            rg_em32(uit, r->veld[i].val);
        }
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

/* Opvulling: int3 (cc) of nop (90), het klassieke vulsel van een nog niet geschreven pagina. */
static int rg_opvulling(const unsigned char *p, size_t n)
{
    size_t i;
    unsigned char eerste = p[0];
    if (eerste != 0xCC && eerste != 0x90)
        return 0;
    for (i = 1; i < n; i++)
        if (p[i] != eerste)
            return 0;
    return 1;
}

/* Een korte, domme hash over de eerste 64 bytes van een pagina (FNV-1a, 32 bits). */
static uint32_t rg_pagina_hash(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/*
 * Het BEWIJSARTEFACT: een regel per GELEZEN adres, geschreven OP HET MOMENT VAN DE KEURING -- niet
 * bij het rapporteren, want dan is er al gepatcht en zie je de sprong in plaats van de prologue.
 * Per regel: tijd, het absolute adres, de acht gelezen bytes, de paginabasis en TWEE hashes:
 * de pagina-hash (eerste 64 B van de pagina) en de adres-hash (64 B VANAF HET ADRES). Die laatste
 * beweegt mee op het moment dat de pakker de prologue schrijft; de pagina-hash kan gelijk blijven
 * als het adres niet aan het begin van de pagina ligt.
 */
static void rg_schrijf_bewijs(rg_pe_ctx *c, const rg_pe_regel *r, const unsigned char *p,
                              uint64_t imagebase)
{
    uint32_t pag, h_pag, h_adres;
    unsigned char buf[64];
    size_t k = 0;
    if (!c->bytes_log)
        return;
    pag = r->rva & ~0xFFFu;
    memset(buf, 0, sizeof buf);
    if ((size_t)pag + sizeof buf <= c->beeld_n) {
        memcpy(buf, c->beeld + pag, sizeof buf);
        k = sizeof buf;
    } else if ((size_t)pag < c->beeld_n) {
        k = c->beeld_n - pag;
        memcpy(buf, c->beeld + pag, k);
    }
    h_pag = rg_pagina_hash(buf, k);
    /* de adres-hash: 64 bytes vanaf het adres zelf, dus de buurt die meebeweegt */
    memset(buf, 0, sizeof buf);
    k = 0;
    if ((size_t)r->rva + sizeof buf <= c->beeld_n) {
        memcpy(buf, c->beeld + r->rva, sizeof buf);
        k = sizeof buf;
    } else if ((size_t)r->rva < c->beeld_n) {
        k = c->beeld_n - r->rva;
        memcpy(buf, c->beeld + r->rva, k);
    }
    h_adres = rg_pagina_hash(buf, k);
    /*
     * De eerste kolom is de AANWIJZER DIE WERKELIJK GELEZEN IS (`p`), niet de afgeleide ImageBase+RVA.
     * Dat is het verschil dat een tegenspraak met een andere partij kan verklaren, dus het moet het
     * gemeten getal zijn en niet het berekende.
     */
    fprintf(c->bytes_log,
            "%llu  0x%016llx  %02x %02x %02x %02x %02x %02x %02x %02x  0x%016llx  pag %08x  "
            "adres %08x%s\n",
            (unsigned long long)rg_nu_ms(), (unsigned long long)(uintptr_t)p,
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            (unsigned long long)(imagebase + pag), h_pag, h_adres,
            ((uintptr_t)p == (uintptr_t)r->doel_adres) ? "" : "  != afgeleid!");
    fflush(c->bytes_log);
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
    r->opvulling = 0;
    if (!r->actief) {
        snprintf(r->reden, sizeof r->reden, "UIT: actief = 0 in de lijst; niet gekeurd en niet aangehaakt");
        return;
    }
    if (!r->adres && !r->is_export) {
        snprintf(r->reden, sizeof r->reden, "AFGEWEZEN: geen adres en geen exportnaam in de lijst");
        return;
    }
    rva = r->is_rva ? (uint32_t)r->adres
                    : (uint32_t)(r->adres - (c->basis_beeld ? kop->imagebase : 0));
    r->rva = rva;
    /* Het WERKELIJKE adres: de aanwijzer van het beeld + RVA (gelijk aan ImageBase + RVA als het beeld
       niet verplaatst is; klopt ook als dat wel zo is). */
    r->doel_adres = (uintptr_t)c->beeld + rva;
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
    memset(r->gezien, 0, sizeof r->gezien);
    memcpy(r->gezien, p, 8);   /* wat de KEURING ziet, niet wat er na het patchen staat */
    rg_schrijf_bewijs(c, r, p, kop->imagebase);
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
        if (p[0] == 0xe9) {
            /*
             * Een sprong: dat is de GESLOTEN kern die er al staat. Dat is geen "te vroeg" maar een
             * echte weigering -- anders zouden we op de patch van een andere kern voortbouwen.
             */
            snprintf(r->reden, sizeof r->reden,
                     "AFGEWEZEN: eerste bytes %02x %02x %02x %02x zijn geen prologue maar een "
                     "sprong -- al door een andere kern omgezet?", p[0], p[1], p[2], p[3]);
            return;
        }
        /*
         * NIET NUL, maar ook geen herkenbare prologue. Dat is opvulling (int3/cc, nop/90) of een half
         * geschreven pagina -- en dus NIET "klaar". Dit was de fout: `cc cc cc cc` gold eerder als
         * uitgepakt, waardoor het adres van de poort (amDongleIsAvailableInternal) werd afgekeurd in
         * plaats van dat er gewacht werd. Opvulling betekent: blijven wachten.
         */
        if (rg_opvulling(p, 5)) {
            r->wacht = 1;
            r->opvulling = 1;
            snprintf(r->reden, sizeof r->reden,
                     "OPVULLING, WACHT: eerste bytes %02x %02x %02x %02x zijn opvulling "
                     "(geen prologue); de pakker is hier nog niet klaar", p[0], p[1], p[2], p[3]);
            return;
        }
        r->wacht = 1;
        r->opvulling = 1;
        snprintf(r->reden, sizeof r->reden,
                 "HALF GESCHREVEN, WACHT: eerste bytes %02x %02x %02x %02x zijn geen herkenbare "
                 "prologue en geen sprong; ik wacht op een volledige pagina", p[0], p[1], p[2], p[3]);
        return;
    }
    r->geldig = 1;
    snprintf(r->reden, sizeof r->reden,
             "GELDIG: sectie %s (RVA 0x%x, runtime 0x%llx), prologue %02x %02x %02x %02x",
             s->naam, rva, (unsigned long long)r->doel_adres, p[0], p[1], p[2], p[3]);
}

/* ------------------------------------------------------------------ schrijven in het beeld */

/*
 * DE ENIGE schrijfweg naar het beeld (hooks en patches). Windows: VirtualProtect(RWX), schrijven,
 * de OUDE bescherming terugzetten, instructiecache legen. Mislukt iets, dan staat de fout met
 * GetLastError in het logboek en geeft de functie 0 -- de aanroeper schrijft dan niet verder.
 * Het kan vanuit de NtProtectVirtualMemory-omleiding worden aangeroepen: die laat geneste aanroepen
 * door (de eenmalig-vlag staat dan al op 1).
 */
int rg_pe_schrijf(rg_pe_ctx *c, uintptr_t adres, const void *bytes, size_t n, const char *wie)
{
#if defined(_WIN32)
    DWORD oud = 0, dummy = 0;
    if (!VirtualProtect((LPVOID)adres, n, PAGE_EXECUTE_READWRITE, &oud)) {
        rg_log(c, "SCHRIJVEN MISLUKT (%s): VirtualProtect(RWX) op 0x%llx (%zu B) gaf GetLastError %lu",
               wie, (unsigned long long)adres, n, (unsigned long)GetLastError());
        return 0;
    }
    memcpy((void *)adres, bytes, n);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)adres, n);
    if (!VirtualProtect((LPVOID)adres, n, oud, &dummy))
        rg_log(c, "LET OP (%s): de oude bescherming 0x%lx op 0x%llx terugzetten mislukte, GetLastError %lu "
                  "(de bytes zijn wel geschreven)", wie, (unsigned long)oud, (unsigned long long)adres,
               (unsigned long)GetLastError());
    return 1;
#else
    long pg = sysconf(_SC_PAGESIZE);
    uintptr_t van = adres & ~(uintptr_t)(pg - 1);
    uintptr_t tot = (adres + n + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
    if (mprotect((void *)van, tot - van, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        rg_log(c, "SCHRIJVEN MISLUKT (%s): mprotect op 0x%llx (%zu B) gaf errno %d (%s)", wie,
               (unsigned long long)adres, n, errno, strerror(errno));
        return 0;
    }
    memcpy((void *)adres, bytes, n);
    return 1;       /* de oude rechten zijn hier niet bekend; deze tak is alleen voor de toetsen */
#endif
}

/* "48 83 ec 28 (4 B)", hoogstens 32 bytes getoond. */
static void rg_fmt_bytes(char *uit, size_t groot, const unsigned char *b, int n)
{
    size_t k = 0;
    int i, toon = n > 32 ? 32 : n;
    uit[0] = 0;
    for (i = 0; i < toon && k + 4 < groot; i++)
        k += (size_t)snprintf(uit + k, groot - k, "%s%02x", i ? " " : "", b[i]);
    if (n > toon && k + 8 < groot)
        k += (size_t)snprintf(uit + k, groot - k, " ...");
    if (k + 12 < groot)
        snprintf(uit + k, groot - k, " (%d B)", n);
}

/* ------------------------------------------------------------------ byte-patches */

/*
 * EEN patch: keuren (valt in een sectie, binnen het beeld, `verwacht` klopt), dan schrijven via
 * rg_pe_schrijf en teruglezen. Eén logregel met naam, RVA, voor, na en het oordeel. Is `verwacht`
 * niet opgegeven, dan wordt alleen gelogd wat er stond.
 */
static void rg_doe_patch(rg_pe_ctx *c, const rg_pe_kop *kop, rg_pe_regel *r)
{
    char voor[512], na[512], verw[512], oordeel[160];
    unsigned char *p;
    uint32_t rva;
    int n = r->patch_n, klopt = 1;
    size_t bereik;

    c->patches_totaal++;
    snprintf(r->patch_oordeel, sizeof r->patch_oordeel, "AFGEWEZEN");
    rva = r->is_rva ? (uint32_t)r->adres : (uint32_t)(r->adres - (c->basis_beeld ? kop->imagebase : 0));
    r->rva = rva;
    r->doel_adres = (uintptr_t)c->beeld + rva;
    if (n <= 0) {
        rg_log(c, "  patch %-14s RVA 0x%08x  AFGEWEZEN: geen `bytes =` of `tekst =` in de lijst", r->naam, rva);
        if (r->actief) c->patches_afgekeurd++; else c->patches_uit++;
        return;
    }
    bereik = (size_t)(n > r->verwacht_n ? n : r->verwacht_n);
    if (!rg_sectie_van(kop, rva)) {
        rg_log(c, "  patch %-14s RVA 0x%08x  AFGEWEZEN: valt in geen sectie van het beeld", r->naam, rva);
        if (r->actief) c->patches_afgekeurd++; else c->patches_uit++;
        return;
    }
    if ((size_t)rva + bereik > c->beeld_n) {
        rg_log(c, "  patch %-14s RVA 0x%08x  AFGEWEZEN: %zu bytes vanaf hier vallen buiten het beeld",
               r->naam, rva, bereik);
        if (r->actief) c->patches_afgekeurd++; else c->patches_uit++;
        return;
    }
    p = (unsigned char *)r->doel_adres;
    memset(r->gezien, 0, sizeof r->gezien);
    memcpy(r->gezien, p, 8);
    rg_schrijf_bewijs(c, r, p, kop->imagebase);
    rg_fmt_bytes(voor, sizeof voor, p, n);
    rg_fmt_bytes(na, sizeof na, r->patch_bytes, n);
    if (r->heeft_verwacht) {
        klopt = memcmp(p, r->verwacht_bytes, (size_t)r->verwacht_n) == 0;
        rg_fmt_bytes(verw, sizeof verw, r->verwacht_bytes, r->verwacht_n);
    } else {
        verw[0] = 0;
    }
    if (!r->actief) {
        snprintf(r->patch_oordeel, sizeof r->patch_oordeel, "UIT");
        snprintf(oordeel, sizeof oordeel, "UIT (actief=0), niets geschreven; verwacht %s",
                 !r->heeft_verwacht ? "n.v.t." : klopt ? "komt overeen" : "KOMT NIET OVEREEN");
        c->patches_uit++;
        rg_log(c, "  patch %-14s RVA 0x%08x  voor: %s  na: %s  %s", r->naam, rva, voor, na, oordeel);
        if (r->heeft_verwacht && !klopt)
            rg_log(c, "  patch %-14s   verwacht was: %s", r->naam, verw);
        return;
    }
    if (!klopt) {
        snprintf(r->patch_oordeel, sizeof r->patch_oordeel, "WEIGER");
        c->patches_afgekeurd++;
        rg_log(c, "  patch %-14s RVA 0x%08x  WEIGER: wat er staat wijkt af van `verwacht`; NIETS GESCHREVEN",
               r->naam, rva);
        rg_log(c, "  patch %-14s   gevonden: %s", r->naam, voor);
        rg_log(c, "  patch %-14s   verwacht: %s", r->naam, verw);
        rg_log(c, "  patch %-14s   zou schrijven: %s", r->naam, na);
        return;
    }
    if (!rg_pe_schrijf(c, r->doel_adres, r->patch_bytes, (size_t)n, r->naam)) {
        snprintf(r->patch_oordeel, sizeof r->patch_oordeel, "SCHRIJVEN MISLUKT");
        c->patches_afgekeurd++;
        rg_log(c, "  patch %-14s RVA 0x%08x  voor: %s  na: %s  SCHRIJVEN MISLUKT (zie de regel hierboven)",
               r->naam, rva, voor, na);
        return;
    }
    if (memcmp(p, r->patch_bytes, (size_t)n) != 0) {
        snprintf(r->patch_oordeel, sizeof r->patch_oordeel, "SCHRIJVEN MISLUKT");
        c->patches_afgekeurd++;
        rg_log(c, "  patch %-14s RVA 0x%08x  geschreven maar bij het teruglezen staat er iets anders -- "
                  "SCHRIJVEN MISLUKT", r->naam, rva);
        return;
    }
    snprintf(r->patch_oordeel, sizeof r->patch_oordeel, "TOEGEPAST");
    r->patch_toegepast = 1;
    c->patches_toegepast++;
    rg_log(c, "  patch %-14s RVA 0x%08x  voor: %s  na: %s  TOEGEPAST%s", r->naam, rva, voor, na,
           r->heeft_verwacht ? "" : " (geen `verwacht` opgegeven)");
}

/* ------------------------------------------------------------------ bytefuncties voor de haken */

int rg_pe_nt_stub_ok(const unsigned char *f)
{
    /* mov r10,rcx ; mov eax,<syscall> ; test byte ptr [0x7ffe0308],1 */
    return f[0] == 0x4c && f[1] == 0x8b && f[2] == 0xd1 && f[3] == 0xb8 &&
           f[8] == 0xf6 && f[9] == 0x04 && f[10] == 0x25 && f[11] == 0x08 && f[12] == 0x03 &&
           f[13] == 0xfe && f[14] == 0x7f && f[15] == 0x01;
}

void rg_pe_maak_sprong14(unsigned char *uit, uint64_t doel)
{
    int i;
    uit[0] = 0xff; uit[1] = 0x25; uit[2] = uit[3] = uit[4] = uit[5] = 0;
    for (i = 0; i < 8; i++)
        uit[6 + i] = (unsigned char)(doel >> (8 * i));
}

void rg_pe_maak_trampoline(unsigned char *uit, const unsigned char *f16, uint64_t terug)
{
    memcpy(uit, f16, 16);
    rg_pe_maak_sprong14(uit + 16, terug);
}

int rg_pe_exit_stub_ok(const unsigned char *f)
{
    static const unsigned char begin[6] = { 0x48, 0x83, 0xec, 0x28, 0xff, 0x15 };
    int i;
    if (memcmp(f, begin, sizeof begin) != 0)
        return 0;
    for (i = 10; i < 16; i++)
        if (f[i] != 0x90 && f[i] != 0xcc)
            return 0;
    return 1;
}

void rg_pe_maak_haak12(unsigned char *uit, uint64_t doel)
{
    int i;
    uit[0] = 0x48; uit[1] = 0xb8;
    for (i = 0; i < 8; i++)
        uit[2 + i] = (unsigned char)(doel >> (8 * i));
    uit[10] = 0xff; uit[11] = 0xe0;
}

int rg_pe_uitgang_besluit(const rg_pe_ctx *c, uint64_t ra_rva, long eerder_terug)
{
    int i;
    if (!c->uitgang_actief)
        return 0;
    for (i = 0; i < c->uitgang_terug_n; i++)
        if ((uint64_t)c->uitgang_terug_bij[i] == ra_rva)
            return eerder_terug < c->uitgang_max;
    return 0;
}

/* ------------------------------------------------------------------ de hoofdroute */

int rg_pe_claim(rg_pe_ctx *c)
{
    return __sync_bool_compare_and_swap(&c->toegepast, 0, 1);
}

/* "2026-10-13 17:15:11". Windows: GetLocalTime (geen extra CRT-import); elders de C-bibliotheek. */
static void rg_tijdtekst(char *uit, size_t groot)
{
#if defined(_WIN32)
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(uit, groot, "%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
#else
    time_t nu = time(NULL);
    struct tm *tm = localtime(&nu);
    if (tm)
        strftime(uit, groot, "%Y-%m-%d %H:%M:%S", tm);
    else
        snprintf(uit, groot, "(tijd onbekend)");
#endif
}

void rg_pe_scheiding(rg_pe_ctx *c)
{
    char tijd[40];
    if (c->scheiding_gedaan)
        return;
    c->scheiding_gedaan = 1;
    rg_tijdtekst(tijd, sizeof tijd);
    if (c->log) {
        fprintf(c->log, "\n=================== rg-pe-hook run %s  pid %lu  tid %lu  tick %llu "
                        "===================\n", tijd, c->pid, c->tid, (unsigned long long)rg_nu_ms());
        fflush(c->log);
    }
}

int rg_pe_voorbereid(rg_pe_ctx *c)
{
    int i;
    if (c->voorbereid)
        return c->lijst_afgekeurd ? 1 : 0;
    if (!c->beeld || !c->beeld_n) {
        rg_log(c, "geen beeld meegegeven");
        return -1;
    }
    if (!c->lijst_pad_bewaard[0] && !c->n && !rg_lees_lijst(c, c->lijst_pad)) {
        rg_log(c, "geen bruikbare regels in %s", c->lijst_pad);
        return -1;
    }
    if (!rg_pe_lees_kop(c->beeld, c->beeld_n, &c->kop)) {
        rg_log(c, "geen geldige PE-kop in het beeld (%zu bytes)", c->beeld_n);
        return -1;
    }
    c->kop_ok = 1;
    c->voorbereid = 1;
    {
        int nf = 0, np = 0;
        for (i = 0; i < c->n; i++) {
            if (c->regels[i].is_patch) np++; else nf++;
        }
        rg_log(c, "PE: machine 0x%x, imagebase 0x%llx, SizeOfImage 0x%x, entry 0x%x, %d secties",
               c->kop.machine, (unsigned long long)c->kop.imagebase, c->kop.grootte_beeld, c->kop.ingang,
               c->kop.aantal_secties);
        rg_log(c, "adreslijst: %d functieregel(s), %d patch(es)%s uit %s (beeld=%s, proloog=%s, basis=%s)",
               nf, np, c->uitgang_aanwezig ? ", [uitgang]" : "", c->lijst_pad,
               c->beeld_vorm == RG_BEELD_PE ? "pe" : "elf",
               c->proloog == RG_PROLOOG_E9REL32 ? "e9rel32" : "standaard",
               c->basis_beeld ? "beeld" : "absoluut");
    }

    /*
     * Het bewijsartefact openen en van een kop voorzien, zodat een andere partij het met een eigen
     * instrument kan natrekken: welke module, welke basis, welk beeldbestand, en hoe de kolommen
     * gelezen moeten worden.
     */
    if (c->bytes_pad && *c->bytes_pad) {
        c->bytes_log = fopen(c->bytes_pad, "a");
        if (c->bytes_log) {
            const rg_pe_kop *kop = &c->kop;
            char tijd[40];
            rg_tijdtekst(tijd, sizeof tijd);
            fprintf(c->bytes_log, "\n");
            fprintf(c->bytes_log, "# =================== rg-pe-hook-bytes run %s  pid %lu ===================\n",
                    tijd, c->pid);
            fprintf(c->bytes_log, "# rg-pe-hook-bytes -- wat de KEURING las, op het moment van de keuring\n");
            fprintf(c->bytes_log, "# tijd_ms        : %llu (monotoon, ms sinds de start van dit proces)\n",
                    (unsigned long long)rg_nu_ms());
            fprintf(c->bytes_log, "# modulebasis    : 0x%016llx\n", (unsigned long long)kop->imagebase);
            fprintf(c->bytes_log, "# ImageBase      : 0x%016llx\n", (unsigned long long)kop->imagebase);
            fprintf(c->bytes_log, "# SizeOfImage    : 0x%08x   entry 0x%08x   secties %d\n",
                    kop->grootte_beeld, kop->ingang, kop->aantal_secties);
            fprintf(c->bytes_log, "# beeldbestand   : %s\n",
                    c->beeld_naam && *c->beeld_naam ? c->beeld_naam : "(niet opgegeven)");
            fprintf(c->bytes_log, "# adreslijst     : %s\n", c->lijst_pad);
            fprintf(c->bytes_log,
                    "# moduleaanwijzer: %p  (hier wordt werkelijk gelezen: moduleaanwijzer + RVA)\n",
                    (void *)c->beeld);
            fprintf(c->bytes_log, "# ImageBase (kop): 0x%016llx\n", (unsigned long long)kop->imagebase);
            if ((uintptr_t)c->beeld != (uintptr_t)kop->imagebase)
                fprintf(c->bytes_log,
                        "# LET OP: de moduleaanwijzer (%p) is NIET gelijk aan de ImageBase uit de kop "
                        "(0x%llx); de engine gebruikt de moduleaanwijzer.\n",
                        (void *)c->beeld, (unsigned long long)kop->imagebase);
            fprintf(c->bytes_log, "# pid            : %lu   thread: %lu\n", c->pid, c->tid);
            fprintf(c->bytes_log, "# proces loopt al: %llu ms   initialisatie nummer: %d\n",
                    (unsigned long long)c->proc_uptime_ms, c->init_teller);
            fprintf(c->bytes_log, "# geheugenvenster: %p .. %p (%zu bytes)\n", (void *)c->beeld,
                    (void *)(c->beeld + c->beeld_n), c->beeld_n);
            fprintf(c->bytes_log, "# kolommen: tijd_ms  VA  acht bytes  paginabasis  pagina-hash"
                                  "(eerste 64 B van de pagina)  adres-hash(64 B vanaf het VA)\n");
            fflush(c->bytes_log);
        } else {
            rg_log(c, "kon het bewijsartefact %s niet openen", c->bytes_pad);
        }
    }

    if (c->verwacht_grootte && c->verwacht_grootte != (uint64_t)c->kop.grootte_beeld) {
        rg_log(c, "lijst AFGEKEURD: grootte 0x%llx in de lijst, maar SizeOfImage is 0x%x",
               (unsigned long long)c->verwacht_grootte, c->kop.grootte_beeld);
        for (i = 0; i < c->n; i++)
            snprintf(c->regels[i].reden, sizeof c->regels[i].reden,
                     "AFGEWEZEN: verkeerde build (SizeOfImage 0x%x != 0x%llx)", c->kop.grootte_beeld,
                     (unsigned long long)c->verwacht_grootte);
        c->lijst_afgekeurd = 1;
        return 1;
    }
    if (c->verwacht_ingang && c->verwacht_ingang != (uint64_t)c->kop.ingang) {
        rg_log(c, "lijst AFGEKEURD: ingang 0x%llx in de lijst, maar die van het beeld is 0x%x",
               (unsigned long long)c->verwacht_ingang, c->kop.ingang);
        for (i = 0; i < c->n; i++)
            snprintf(c->regels[i].reden, sizeof c->regels[i].reden,
                     "AFGEWEZEN: verkeerde build (entry 0x%x != 0x%llx)", c->kop.ingang,
                     (unsigned long long)c->verwacht_ingang);
        c->lijst_afgekeurd = 1;
        return 1;
    }

    /* de trigger: de sectie waarvan het verzegelen het moment is */
    if (c->trigger[0]) {
        if (strcmp(c->trigger, "protect") != 0) {
            rg_log(c, "trigger = %s is onbekend (alleen `protect`); er is geen trigger", c->trigger);
            c->trigger[0] = 0;
        } else {
            for (i = 0; i < c->kop.aantal_secties; i++)
                if (!strcmp(c->kop.secties[i].naam, c->trigger_sectie))
                    c->trigger_base = (uintptr_t)c->beeld + c->kop.secties[i].rva;
            if (!c->trigger_base || !c->trigger_prot) {
                rg_log(c, "trigger = protect: sectie `%s` niet gevonden of trigger_prot ontbreekt; er is "
                          "geen trigger", c->trigger_sectie);
                c->trigger[0] = 0;
                c->trigger_base = 0;
            } else {
                rg_log(c, "trigger = protect: sectie %s op 0x%llx, nieuwe bescherming 0x%x", c->trigger_sectie,
                       (unsigned long long)c->trigger_base, c->trigger_prot);
            }
        }
    }
    return 0;
}

void rg_pe_wacht(rg_pe_ctx *c)
{
    /* keuring, met de wachtroute: een pagina die nog niet is uitgepakt is geen fout maar te vroeg.
       Alleen ACTIEVE functieregels tellen mee; patches wachten nergens op. */
    uint64_t t0 = rg_nu_ms();
    int ronde = 0, nog, i;
    int vorige_nul = -1, vorige_opv = -1;
    for (;;) {
        int nu_nul = 0, nu_opv = 0;
        nog = 0;
        for (i = 0; i < c->n; i++) {
            rg_pe_regel *r = &c->regels[i];
            if (r->is_patch || !r->actief)
                continue;
            rg_keur_een(c, &c->kop, r);
            if (r->wacht) {
                nog++;
                if (r->opvulling)
                    nu_opv++;
                else
                    nu_nul++;
            }
        }
        /* de drie gevallen zichtbaar maken, alleen wanneer de toestand verandert */
        if (nu_nul != vorige_nul || nu_opv != vorige_opv) {
            if (nu_nul && !nu_opv)
                rg_log(c, "wachtroute: %d adres(sen) nog NUL -- NOG NIET UITGEPAKT; ik blijf kijken "
                          "(max %d s, elke %d ms)", nu_nul, c->wacht_s, c->wacht_ms > 0 ? c->wacht_ms : 10);
            else if (nu_opv && !nu_nul)
                rg_log(c, "wachtroute: %d adres(sen) is niet-nul maar OPVULLING of half geschreven "
                          "(geen prologue) -- ook wachten, dit is NIET klaar", nu_opv);
            else if (nu_nul && nu_opv)
                rg_log(c, "wachtroute: %d adres(sen) nog NUL (niet ontpakt), %d op OPVULLING of half "
                          "geschreven -- ik blijf kijken", nu_nul, nu_opv);
            vorige_nul = nu_nul;
            vorige_opv = nu_opv;
        }
        if (!nog || c->wacht_s <= 0)
            break;
        if (c->toegepast) {                 /* de trigger was ons voor */
            rg_log(c, "wachtroute: een ander heeft de regels al toegepast; ik stop met wachten");
            return;
        }
        if (rg_nu_ms() - t0 > (uint64_t)c->wacht_s * 1000u) {
            rg_log(c, "wachtroute: na %d s gestopt: %d nog NUL (niet ontpakt), %d nog OPVULLING of "
                      "half geschreven -- de pagina's zijn niet binnen de tijd klaar gekomen",
                   c->wacht_s, nu_nul, nu_opv);
            break;
        }
        ronde++;
        rg_slaap_ms(c->wacht_ms > 0 ? c->wacht_ms : 10);
    }
    if (nog == 0 && ronde > 0)
        rg_log(c, "wachtroute: de pagina's zijn uitgepakt na %llu ms; nu omzetten",
               (unsigned long long)(rg_nu_ms() - t0));
}

int rg_pe_pas_toe(rg_pe_ctx *c, const char *bron)
{
    unsigned char *stub_pagina = NULL;
    size_t pag;
    int i, aangehaakt = 0, afgekeurd = 0, todo = 0, fn_totaal = 0, fn_uit = 0, patch_afgekeurd;
    uintptr_t pagina_adres = 0;
    const rg_pe_kop *kop = &c->kop;

    if (!c->voorbereid || !c->kop_ok) {
        rg_log(c, "pas_toe(%s): niet voorbereid; er wordt niets gedaan", bron);
        return -1;
    }
    if (c->lijst_afgekeurd)
        return 0;
    if (!rg_pe_claim(c)) {
        rg_log(c, "pas_toe(%s): niet aan de beurt -- de regels zijn al toegepast of worden nu toegepast "
                  "door %s; ik doe niets", bron, c->toegepast_door ? c->toegepast_door : "een ander");
        return -2;
    }
    c->toegepast_door = bron;
    rg_log(c, "toegepast door: %s (tick %llu)", bron, (unsigned long long)rg_nu_ms());
    if (!strcmp(bron, "FALLBACK"))
        rg_log(c, "FALLBACK: de regels zijn toegepast door de TERUGVAL-poll, NIET door de trigger -- de "
                  "trigger is niet (op tijd) gekomen");

    /* keuring van de functieregels op dit moment */
    for (i = 0; i < c->n; i++) {
        if (c->regels[i].is_patch)
            continue;
        fn_totaal++;
        if (!c->regels[i].actief)
            fn_uit++;
        rg_keur_een(c, kop, &c->regels[i]);
    }

    /* de stubpagina: een uitvoerbare pagina. We proberen hem dicht bij het beeld te leggen, zodat
       de 5-byte `e9 rel32`-omzetting past; lukt dat niet, dan volgt een 12-byte absolute sprong. */
    pag = RG_PAGINA_GROOTTE;
    for (i = 0; i < c->n; i++)
        if (!c->regels[i].is_patch && c->regels[i].geldig)
            break;
    if (i < c->n) {
#if defined(_WIN32)
        uintptr_t wens = (uintptr_t)c->beeld - 0x10000;
        stub_pagina = (unsigned char *)VirtualAlloc((LPVOID)wens, pag, MEM_COMMIT | MEM_RESERVE,
                                                   PAGE_EXECUTE_READWRITE);
        if (!stub_pagina)
            stub_pagina = (unsigned char *)VirtualAlloc(NULL, pag, MEM_COMMIT | MEM_RESERVE,
                                                       PAGE_EXECUTE_READWRITE);
#else
        void *wens = (void *)((uintptr_t)c->beeld - 0x10000);
        stub_pagina = mmap(wens, pag, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (stub_pagina == MAP_FAILED)
            stub_pagina = mmap(NULL, pag, PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stub_pagina == MAP_FAILED)
            stub_pagina = NULL;
#endif
        if (!stub_pagina) {
            rg_log(c, "GEEN uitvoerbare pagina voor de stubs; er wordt geen functie aangehaakt");
            for (i = 0; i < c->n; i++)
                c->regels[i].geldig = 0;
        } else {
            pagina_adres = (uintptr_t)stub_pagina;
            rg_log(c, "stubpagina op 0x%llx (%zu bytes)", (unsigned long long)pagina_adres, pag);
        }
    }

    /* per geldige regel een stub, en de prologue omzetten */
    for (i = 0; i < c->n; i++) {
        rg_pe_regel *r = &c->regels[i];
        rg_buf stub;
        uintptr_t plek;
        int64_t afstand;
        if (r->is_patch || !r->actief)
            continue;
        if (!r->geldig) {
            afgekeurd++;
            continue;
        }
        if (!rg_maak_stub(c, r, i, &stub)) {
            snprintf(r->reden, sizeof r->reden, "AFGEWEZEN: antwoordvorm kon niet worden gebouwd");
            r->geldig = 0;
            afgekeurd++;
            continue;
        }
        if (c->stub_gebruikt + stub.n > (int)pag) {
            rg_log(c, "  %s: stubpagina vol; overgeslagen", r->naam);
            snprintf(r->reden, sizeof r->reden, "AFGEWEZEN: stubpagina vol");
            r->geldig = 0;
            afgekeurd++;
            continue;
        }
        plek = pagina_adres + (uintptr_t)c->stub_gebruikt;
        memcpy((void *)plek, stub.b, (size_t)stub.n);
        c->stub_gebruikt += stub.n;

        afstand = (int64_t)plek - (int64_t)(r->doel_adres + 5);
        {
            unsigned char nieuw[12];
            size_t nn;
            if (afstand >= -2147483648LL && afstand <= 2147483647LL) {
                int32_t rel = (int32_t)afstand;
                nieuw[0] = 0xe9;
                memcpy(nieuw + 1, &rel, 4);
                nn = 5;
                snprintf(r->wijze, sizeof r->wijze, "e9 rel32 (5 bytes)");
            } else {
                uint64_t doel = (uint64_t)plek;
                nieuw[0] = 0x48; nieuw[1] = 0xb8;
                memcpy(nieuw + 2, &doel, 8);
                nieuw[10] = 0xff; nieuw[11] = 0xe0;
                nn = 12;
                snprintf(r->wijze, sizeof r->wijze, "48 b8 abs64 ff e0 (12 bytes)");
            }
            if (!rg_pe_schrijf(c, r->doel_adres, nieuw, nn, r->naam)) {
                snprintf(r->reden, sizeof r->reden, "SCHRIJVEN MISLUKT (zie het logboek)");
                r->geldig = 0;
                afgekeurd++;
                continue;
            }
        }
        r->gehaakt = 1;
        aangehaakt++;
        if (r->antwoord == RG_ANTWOORD_TODO)
            todo++;
        rg_log(c, "  aangehaakt: %-34s RVA 0x%06x -> stub 0x%llx  [%s]%s%s", r->naam, r->rva,
               (unsigned long long)plek, r->wijze, (r->log_aanroep && c->log_melder) ? "  [log0]" : "",
               r->antwoord == RG_ANTWOORD_TODO ? "  ANTWOORD = TODO" : "");
    }

    /* de byte-patches, door dezelfde schrijfweg */
    for (i = 0; i < c->n; i++)
        if (c->regels[i].is_patch)
            rg_doe_patch(c, kop, &c->regels[i]);

    /*
     * Het oordeel per functieregel: de AANWIJZER die werkelijk is gederefereerd met de MODULEBASIS en
     * de bytes die op dat moment op die plek stonden.
     */
    for (i = 0; i < c->n; i++) {
        rg_pe_regel *r = &c->regels[i];
        if (r->is_patch)
            continue;
        if (!r->actief) {
            rg_log(c, "  %-34s UIT (actief = 0): niet gekeurd, niet aangehaakt", r->naam);
            continue;
        }
        rg_log(c, "  %-34s RVA 0x%08x  VA 0x%016llx (basis 0x%llx)  bytes %02x %02x %02x %02x "
                  "%02x %02x %02x %02x", r->naam, r->rva, (unsigned long long)r->doel_adres,
               (unsigned long long)kop->imagebase, r->gezien[0], r->gezien[1], r->gezien[2],
               r->gezien[3], r->gezien[4], r->gezien[5], r->gezien[6], r->gezien[7]);
        rg_log(c, "  %-34s %s", "", r->reden);
    }
    for (i = 0; i < c->n; i++) {
        rg_pe_regel *r = &c->regels[i];
        if (r->is_patch || r->gehaakt || !r->actief)
            continue;
        if (!r->geldig)
            rg_log(c, "  niet aangehaakt: %-34s (in de adreslijst maar afgekeurd: %s)", r->naam,
                   r->reden + (strncmp(r->reden, "AFGEWEZEN: ", 11) == 0 ? 11 : 0));
        else
            rg_log(c, "  niet aangehaakt: %-34s (in de adreslijst en goedgekeurd, maar het "
                      "aanhaken lukte niet)", r->naam);
    }
    patch_afgekeurd = c->patches_afgekeurd;
    rg_log(c, "totaal aangehaakt: %d van %d (%d uit); patches toegepast: %d van %d (%d uit); afgekeurd: %d; "
              "waarvan antwoord TODO: %d; uitgang: %s; TERUG %ld; doorgelaten %ld",
           aangehaakt, fn_totaal, fn_uit, c->patches_toegepast, c->patches_totaal, c->patches_uit,
           afgekeurd + patch_afgekeurd, todo,
           !c->uitgang_aanwezig ? "niet geconfigureerd" : c->uitgang_staat ? "haak staat" : "haak staat niet",
           (long)c->uitgang_terug, (long)c->uitgang_door);
    __sync_synchronize();
    c->toegepast = 2;
    return aangehaakt;
}

int rg_pe_hook(rg_pe_ctx *c)
{
    int v;
    rg_pe_scheiding(c);
    v = rg_pe_voorbereid(c);
    if (v < 0)
        return -1;
    if (v == 1)
        return 0;
    rg_pe_wacht(c);
    return rg_pe_pas_toe(c, c->fallback ? "FALLBACK" : "poll");
}
