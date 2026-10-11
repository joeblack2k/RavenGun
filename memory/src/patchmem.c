/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * patchmem.c — het hart van de geheugen-motor van RavenGun.
 *
 * Leest de patchlijst, zoekt per module de basis op, controleert `verwacht`, schrijft `nieuw` en
 * houdt een verslag bij. Er zit GEEN spelkennis in dit bestand: alles komt uit de lijst.
 *
 * De platformlaag (entry_linux.c / entry_win.c) levert alleen het opzoeken van modules
 * (patchmem_module_base + patchmem_foreach_module) en het starten van de motor. Het schrijven zelf
 * staat hieronder, in schrijf_geheugen(), met een #ifdef per platform -- dat is de enige plek waar
 * dit bestand het besturingssysteem aanraakt.
 *
 * Wat dit bestand NOOIT doet:
 *   - schrijven als `verwacht` niet klopt (dan alleen een melding);
 *   - naar de NAS schrijven (dit is procesgeheugen; er komt geen bestand aan te pas);
 *   - gokken waar een module staat (niet gevonden = wachten, niet schrijven).
 */
#include "patchmem.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#  include <pthread.h>
#endif

#define PATCHMEM_VERSIE   "1.0"
#define PATCHMEM_REGEL_MAX 8192
#define PATCHMEM_VELDEN_MAX 5          /* 4 velden + eentje om "te veel" te kunnen zien */
#define PATCHMEM_HEX_TONEN  24         /* hoeveel bytes een melding laat zien */

/* ---------------------------------------------------------------- toestand */

static patchmem_patch g_lijst[PATCHMEM_MAX_PATCHES];
static size_t g_aantal;
static int g_gelezen;
static int g_verbose = -1;             /* -1 = nog niet bepaald */
static int g_openstaand_gemeld;

#if defined(_WIN32)
static CRITICAL_SECTION g_slot;
static int g_slot_klaar;
#  define SLOT_NEMEN()  do { if (g_slot_klaar) EnterCriticalSection(&g_slot); } while (0)
#  define SLOT_GEVEN()  do { if (g_slot_klaar) LeaveCriticalSection(&g_slot); } while (0)
#else
static pthread_mutex_t g_slot = PTHREAD_MUTEX_INITIALIZER;
#  define SLOT_NEMEN()  pthread_mutex_lock(&g_slot)
#  define SLOT_GEVEN()  pthread_mutex_unlock(&g_slot)
#endif

static void slot_klaarmaken(void)
{
#if defined(_WIN32)
    if (!g_slot_klaar) {
        InitializeCriticalSection(&g_slot);
        g_slot_klaar = 1;
    }
#endif
}

/* ---------------------------------------------------------------- kleine hulpjes */

const char *patchmem_basisnaam(const char *pad)
{
    const char *laatste = pad;
    const char *p;
    if (!pad) return "";
    for (p = pad; *p; p++) {
        if (*p == '/' || *p == '\\') laatste = p + 1;
    }
    return laatste;
}

int patchmem_zelfde_module(const char *a, const char *b)
{
    const char *x = patchmem_basisnaam(a);
    const char *y = patchmem_basisnaam(b);
    while (*x && *y) {
        int cx = (unsigned char)*x;
        int cy = (unsigned char)*y;
        if (cx >= 'A' && cx <= 'Z') cx += 'a' - 'A';
        if (cy >= 'A' && cy <= 'Z') cy += 'a' - 'A';
        if (cx != cy) return 0;
        x++;
        y++;
    }
    return *x == '\0' && *y == '\0';
}

/* Hex-bytes lezen: "48 89 5c 24", "48895c24", "0x48,0x89" en "48:89" mogen allemaal. */
static int hex_naar_bytes(const char *tekst, unsigned char *uit, size_t max, size_t *len)
{
    size_t n = 0;
    int hoog = -1;
    const char *p = tekst ? tekst : "";

    while (*p) {
        char c = *p;
        int w = -1;
        if (c == ' ' || c == '\t' || c == ':' || c == ',' || c == '-' || c == '_') {
            p++;
            continue;
        }
        if (c == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            continue;
        }
        if (c >= '0' && c <= '9')      w = c - '0';
        else if (c >= 'a' && c <= 'f') w = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') w = c - 'A' + 10;
        else return 0;                              /* geen hex-teken */
        if (hoog < 0) {
            hoog = w;
        } else {
            if (n >= max) return 0;                 /* te veel bytes */
            uit[n++] = (unsigned char)((hoog << 4) | w);
            hoog = -1;
        }
        p++;
    }
    if (hoog >= 0) return 0;                        /* oneven aantal hex-tekens */
    *len = n;
    return 1;
}

/* Bytes als hex in een tekstbuffer, met ".." als het er te veel zijn. */
static void hex_naar_tekst(const unsigned char *d, size_t n, char *uit, size_t max)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    size_t k = 0;
    size_t tonen = n > PATCHMEM_HEX_TONEN ? PATCHMEM_HEX_TONEN : n;

    if (max == 0) return;
    if (n == 0) {
        snprintf(uit, max, "(leeg)");
        return;
    }
    for (i = 0; i < tonen; i++) {
        if (k + 2 >= max) break;
        uit[k++] = digits[(d[i] >> 4) & 0xF];
        uit[k++] = digits[d[i] & 0xF];
    }
    if (tonen < n && k + 2 < max) {
        uit[k++] = '.';
        uit[k++] = '.';
    }
    uit[k < max ? k : max - 1] = '\0';
}

static void zet_melding(patchmem_patch *p, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->melding, sizeof p->melding, fmt, ap);
    va_end(ap);
}

/* Milliseconden sinds een willekeurig maar vast punt. De achtergrondthread in de platformlaag
 * gebruikt dit voor zijn wachttijd. */
int64_t patchmem_nu_ms(void)
{
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

void patchmem_slaap_ms(int ms)
{
    if (ms <= 0) return;
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

/* ---------------------------------------------------------------- het schrijven zelf */

/* Ligt het hele bereik [adres, adres+len) in toegewezen, leesbaar geheugen?
 *
 * Zonder deze controle zou een verkeerde offset in de lijst het spel laten omvallen -- en een
 * patchmotor die de game laat crashen is erger dan een patchmotor die weigert. Een bereik dat over
 * een aantal aaneensluitende gebieden loopt mag; we schuiven het bereik dan op. */
#if defined(_WIN32)

static int bereik_toegewezen(const void *adres, size_t len)
{
    uintptr_t p = (uintptr_t)adres;
    uintptr_t eind = p + len;

    if (len == 0) return 1;
    if (eind < p) return 0;                        /* overloop */
    while (p < eind) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)p, &mbi, sizeof mbi) == 0) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        p = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

#else

static int bereik_toegewezen(const void *adres, size_t len)
{
    uintptr_t cursor = (uintptr_t)adres;
    uintptr_t eind = cursor + len;

    if (len == 0) return 1;
    if (eind < cursor) return 0;                   /* overloop */
    while (cursor < eind) {
        FILE *m = fopen("/proc/self/maps", "r");
        char regel[512];
        int vooruit = 0;

        if (!m) return 1;                          /* geen /proc: dan kunnen we het niet controleren */
        while (fgets(regel, sizeof regel, m)) {
            unsigned long long a = 0, b = 0;
            char rechten[8];
            if (sscanf(regel, "%llx-%llx %7s", &a, &b, rechten) != 3) continue;
            if (rechten[0] != 'r') continue;
            if (cursor >= (uintptr_t)a && cursor < (uintptr_t)b) {
                cursor = (uintptr_t)b;
                vooruit = 1;
                break;
            }
        }
        fclose(m);
        if (!vooruit) return 0;
    }
    return 1;
}

#endif

#if defined(_WIN32)

/* Windows onthoudt de oude rechten zelf, dus terugzetten is exact. */
static int schrijf_geheugen(void *adres, const unsigned char *data, size_t len)
{
    DWORD oud = 0;
    if (!VirtualProtect(adres, len, PAGE_EXECUTE_READWRITE, &oud)) return 0;
    memcpy(adres, data, len);
    FlushInstructionCache(GetCurrentProcess(), adres, len);
    VirtualProtect(adres, len, oud, &oud);
    return 1;
}

#else

/* De rechten van het gebied waarin `adres` ligt, opgezocht in /proc/self/maps. Zonder dit zouden we
 * een .data-gebied (rw-) na het patchen als r-x achterlaten, en dat breekt de game. */
static int gebied_rechten(uintptr_t adres, int *prot, uintptr_t *begin, uintptr_t *eind)
{
    FILE *m = fopen("/proc/self/maps", "r");
    char regel[512];

    if (!m) return 0;
    while (fgets(regel, sizeof regel, m)) {
        unsigned long long a = 0, b = 0;
        char rechten[8];
        if (sscanf(regel, "%llx-%llx %7s", &a, &b, rechten) != 3) continue;
        if (a == 0 && b == 0) continue;
        if (adres >= (uintptr_t)a && adres < (uintptr_t)b) {
            int p = 0;
            if (rechten[0] == 'r') p |= PROT_READ;
            if (rechten[1] == 'w') p |= PROT_WRITE;
            if (rechten[2] == 'x') p |= PROT_EXEC;
            *prot = p;
            *begin = (uintptr_t)a;
            *eind = (uintptr_t)b;
            fclose(m);
            return 1;
        }
    }
    fclose(m);
    return 0;
}

static int schrijf_geheugen(void *adres, const unsigned char *data, size_t len)
{
    long pag = sysconf(_SC_PAGESIZE);
    uintptr_t begin = 0, eind = 0;
    int prot = PROT_READ | PROT_EXEC;
    int terug;

    if (pag <= 0) pag = 4096;
    if (!gebied_rechten((uintptr_t)adres, &prot, &begin, &eind)) {
        begin = (uintptr_t)adres & ~(uintptr_t)(pag - 1);
        eind = begin + (uintptr_t)pag;
    }
    if ((uintptr_t)adres + len > eind) {
        eind = ((uintptr_t)adres + len + (uintptr_t)pag - 1) & ~(uintptr_t)(pag - 1);
    }
    if (mprotect((void *)begin, (size_t)(eind - begin), prot | PROT_WRITE) != 0) return 0;
    memcpy(adres, data, len);
    __builtin___clear_cache((char *)adres, (char *)adres + len);
    /* De patch staat er nu in. Lukt het terugzetten niet, dan is dat een melding waard maar geen
     * mislukte patch; we zeggen hier dus niet dat het schrijven mislukt is. */
    terug = mprotect((void *)begin, (size_t)(eind - begin), prot);
    if (terug != 0) {
        fprintf(stderr, "patchmem: let op: de rechten van %p zijn niet teruggezet\n", adres);
    }
    return 1;
}

#endif

/* ---------------------------------------------------------------- de modulebasis */

struct zoekcontext {
    const char *naam;
    void *basis;
    int gevonden;
};

static int zoek_in_lijst(const char *naam, void *basis, void *user)
{
    struct zoekcontext *ctx = (struct zoekcontext *)user;
    if (patchmem_zelfde_module(naam, ctx->naam)) {
        ctx->basis = basis;
        ctx->gevonden = 1;
        return 1;                                   /* gevonden: stoppen */
    }
    return 0;
}

/* De basis van een module. Eerst de snelle haak, en pas als die niets geeft de zekere weg langs
 * alle modules -- want een niet-PIE hoofdprogramma heeft basis 0 en dat is van "niet gevonden" niet
 * te onderscheiden. */
static int vind_basis(const char *naam, void **basis)
{
    struct zoekcontext ctx;
    void *snel = patchmem_module_base(naam);

    if (snel) {
        *basis = snel;
        return 1;
    }
    ctx.naam = naam;
    ctx.basis = NULL;
    ctx.gevonden = 0;
    patchmem_foreach_module(zoek_in_lijst, &ctx);
    if (ctx.gevonden) {
        *basis = ctx.basis;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- het verslag */

static const char *status_tekst(int s)
{
    switch (s) {
    case PATCHMEM_TOEGEPAST:     return "toegepast";
    case PATCHMEM_AL_GOED:       return "stond er al";
    case PATCHMEM_GEEN_MODULE:   return "wacht";
    case PATCHMEM_VERWACHT_FOUT: return "GEWEIGERD";
    case PATCHMEM_MISLUKT:       return "MISLUKT";
    default:                     return "nieuw";
    }
}

static void meld(const patchmem_patch *p, const unsigned char *voor, size_t voor_len)
{
    char h_voor[3 * PATCHMEM_HEX_TONEN + 8];
    char h_nieuw[3 * PATCHMEM_HEX_TONEN + 8];

    hex_naar_tekst(voor, voor_len, h_voor, sizeof h_voor);
    hex_naar_tekst(p->nieuw, p->nieuw_len, h_nieuw, sizeof h_nieuw);
    fprintf(stderr, "patchmem: %-11s %s+0x%llx  [%s -> %s]  %s\n",
            status_tekst(p->status), p->module, p->offset, h_voor, h_nieuw, p->melding);
}

/* ---------------------------------------------------------------- één patch toepassen */

static void pas_toe(patchmem_patch *p)
{
    unsigned char voor[PATCHMEM_MAX_BYTES];
    unsigned char *adres;
    void *basis = NULL;
    size_t lees;
    int oude_status = p->status;

    /* Al afgehandeld of onherstelbaar: niet blijven proberen. */
    if (p->status == PATCHMEM_TOEGEPAST || p->status == PATCHMEM_AL_GOED ||
        p->status == PATCHMEM_VERWACHT_FOUT || p->status == PATCHMEM_MISLUKT) {
        return;
    }

    if (!vind_basis(p->module, &basis)) {
        p->status = PATCHMEM_GEEN_MODULE;
        p->adres = 0;
        zet_melding(p, "module %s is niet geladen; de patch wacht", p->module);
        if (p->status != oude_status && patchmem_verbose()) meld(p, NULL, 0);
        return;
    }

    p->adres = (unsigned long long)(uintptr_t)basis + p->offset;
    adres = (unsigned char *)(uintptr_t)p->adres;

    /* Zoveel lezen als we nodig hebben: voor de controle én om te zien of het al gebeurd is. */
    lees = p->nieuw_len > p->verwacht_len ? p->nieuw_len : p->verwacht_len;
    if (lees > sizeof voor) lees = sizeof voor;

    /* Eerst kijken of daar wel geheugen ligt. Anders valt het spel om, en dat is het laatste wat een
     * patchmotor mag doen. */
    if (!bereik_toegewezen(adres, lees)) {
        p->status = PATCHMEM_MISLUKT;
        zet_melding(p, "%s+0x%llx (%zu bytes) ligt niet in toegewezen geheugen; er is NIETS gelezen "
                       "of geschreven (klopt de module en de offset?)",
                    p->module, p->offset, lees);
        meld(p, NULL, 0);
        return;
    }

    memcpy(voor, adres, lees);

    /* 1. De controle. Klopt `verwacht` niet, dan wordt er NIETS geschreven. */
    if (p->verwacht_len > 0 && memcmp(voor, p->verwacht, p->verwacht_len) != 0) {
        char h_gevonden[3 * PATCHMEM_HEX_TONEN + 8];
        char h_verwacht[3 * PATCHMEM_HEX_TONEN + 8];
        hex_naar_tekst(voor, p->verwacht_len, h_gevonden, sizeof h_gevonden);
        hex_naar_tekst(p->verwacht, p->verwacht_len, h_verwacht, sizeof h_verwacht);
        p->status = PATCHMEM_VERWACHT_FOUT;
        zet_melding(p, "de bytes op %s+0x%llx zijn %s, verwacht zegt %s (verkeerde spelversie?); "
                       "er is NIETS geschreven", p->module, p->offset, h_gevonden, h_verwacht);
        meld(p, voor, lees);
        return;
    }

    /* 2. Stond het al goed? Dan niet nog eens schrijven. */
    if (memcmp(voor, p->nieuw, p->nieuw_len) == 0) {
        p->status = PATCHMEM_AL_GOED;
        zet_melding(p, "de bytes op %s+0x%llx stonden al goed; niets geschreven",
                    p->module, p->offset);
        if (patchmem_verbose()) meld(p, voor, lees);
        return;
    }

    /* 3. Schrijven. */
    if (!schrijf_geheugen(adres, p->nieuw, p->nieuw_len)) {
        p->status = PATCHMEM_MISLUKT;
        zet_melding(p, "de bytes op %s+0x%llx konden niet beschreven worden (geheugen niet "
                       "schrijfbaar te maken)", p->module, p->offset);
        meld(p, voor, lees);
        return;
    }
    {
        unsigned char na[PATCHMEM_MAX_BYTES];
        memcpy(na, adres, p->nieuw_len);
        if (memcmp(na, p->nieuw, p->nieuw_len) != 0) {
            p->status = PATCHMEM_MISLUKT;
            zet_melding(p, "de bytes op %s+0x%llx zijn geschreven maar staan er niet "
                           "(geheugen teruggezet?)", p->module, p->offset);
            meld(p, voor, lees);
            return;
        }
    }
    p->status = PATCHMEM_TOEGEPAST;
    zet_melding(p, "op %s+0x%llx", p->module, p->offset);
    if (patchmem_verbose()) meld(p, voor, lees);
}

/* ---------------------------------------------------------------- de lijst lezen */

static char *trim(char *s)
{
    char *eind;
    while (*s == ' ' || *s == '\t') s++;
    eind = s + strlen(s);
    while (eind > s && (eind[-1] == ' ' || eind[-1] == '\t' || eind[-1] == '\r' || eind[-1] == '\n')) {
        *--eind = '\0';
    }
    return s;
}

static int splits_velden(char *regel, char *velden[], int max)
{
    int n = 0;
    char *p = regel;

    velden[n++] = p;
    while (*p) {
        if (*p == '|') {
            *p = '\0';
            if (n >= max) return -1;               /* te veel velden */
            velden[n++] = p + 1;
        }
        p++;
    }
    return n;
}

/* "decimaal of 0x...". Bewust geen strtoull(...,0): die leest "010" als octaal. */
static int lees_offset(const char *tekst, unsigned long long *uit)
{
    char *eind = NULL;
    unsigned long long v;
    int basis = 10;

    if (!tekst || !*tekst) return 0;
    if (tekst[0] == '0' && (tekst[1] == 'x' || tekst[1] == 'X')) {
        basis = 16;
        tekst += 2;
        if (!*tekst) return 0;
    }
    v = strtoull(tekst, &eind, basis);
    if (eind == tekst) return 0;
    while (*eind == ' ' || *eind == '\t') eind++;
    if (*eind) return 0;
    *uit = v;
    return 1;
}

static int lees_lijst(const char *pad)
{
    FILE *fh;
    char regel[PATCHMEM_REGEL_MAX];
    unsigned int nummer = 0;
    size_t k;

    g_aantal = 0;
    g_gelezen = 0;
    fh = fopen(pad, "r");
    if (!fh) {
        fprintf(stderr, "patchmem: kan de patchlijst %s niet openen\n", pad);
        return 0;
    }

    while (fgets(regel, sizeof regel, fh)) {
        char *velden[PATCHMEM_VELDEN_MAX];
        char *werk;
        int n, i;

        nummer++;
        werk = trim(regel);
        if (*werk == '\0' || *werk == '#') continue;      /* leeg of commentaar */
        if (g_aantal >= PATCHMEM_MAX_PATCHES) {
            fprintf(stderr, "patchmem: meer dan %d patches in %s; de rest wordt overgeslagen\n",
                    PATCHMEM_MAX_PATCHES, pad);
            break;
        }

        n = splits_velden(werk, velden, PATCHMEM_VELDEN_MAX);
        if (n != 4) {
            /* Een vormfout is een patch die niet toegepast wordt; we tellen hem mee zodat het
             * verslag niet zegt dat alles gelukt is. De melding zelf wordt onderaan deze functie
             * op stderr gezet, samen met alle andere vormfouten. */
            if (g_aantal < PATCHMEM_MAX_PATCHES) {
                patchmem_patch *p = &g_lijst[g_aantal++];
                memset(p, 0, sizeof *p);
                p->regel = nummer;
                p->status = PATCHMEM_MISLUKT;
                p->vormfout = 1;
                snprintf(p->module, sizeof p->module, "regel %u", nummer);
                zet_melding(p, "verwacht 4 velden (module|offset|verwacht|nieuw), gevonden %d; "
                               "overgeslagen", n < 0 ? PATCHMEM_VELDEN_MAX : n);
            }
            continue;
        }

        for (i = 0; i < 4; i++) velden[i] = trim(velden[i]);

        {
            patchmem_patch *p = &g_lijst[g_aantal];
            memset(p, 0, sizeof *p);
            p->regel = nummer;

            if (!*velden[0]) {
                p->status = PATCHMEM_MISLUKT;
                snprintf(p->module, sizeof p->module, "regel %u", nummer);
                zet_melding(p, "de modulenaam is leeg");
                g_aantal++;
                continue;
            }
            snprintf(p->module, sizeof p->module, "%s", velden[0]);

            if (!lees_offset(velden[1], &p->offset)) {
                p->status = PATCHMEM_MISLUKT;
                p->vormfout = 1;
                zet_melding(p, "offset '%s' is geen getal (decimaal of 0x...) -- module %s",
                            velden[1], p->module);
                g_aantal++;
                continue;
            }
            if (!hex_naar_bytes(velden[2], p->verwacht, sizeof p->verwacht, &p->verwacht_len)) {
                p->status = PATCHMEM_MISLUKT;
                p->vormfout = 1;
                zet_melding(p, "verwacht '%s' is geen hex-bytreeks -- module %s", velden[2], p->module);
                g_aantal++;
                continue;
            }
            if (!hex_naar_bytes(velden[3], p->nieuw, sizeof p->nieuw, &p->nieuw_len) ||
                p->nieuw_len == 0) {
                p->status = PATCHMEM_MISLUKT;
                p->vormfout = 1;
                zet_melding(p, "nieuw '%s' is geen hex-bytreeks of is leeg -- module %s",
                            velden[3], p->module);
                g_aantal++;
                continue;
            }
            if (p->offset + p->nieuw_len < p->offset) {
                p->status = PATCHMEM_MISLUKT;
                p->vormfout = 1;
                zet_melding(p, "offset 0x%llx + %zu bytes loopt over -- module %s",
                            p->offset, p->nieuw_len, p->module);
                g_aantal++;
                continue;
            }
            p->status = PATCHMEM_NIEUW;
            g_aantal++;
            g_gelezen++;
        }
    }
    fclose(fh);

    /* De vormfouten één keer melden. Deze regels komen nooit bij pas_toe() langs (er valt niets toe
     * te passen), dus zonder dit zou een verkeerde regel stil verdwijnen. */
    for (k = 0; k < g_aantal; k++) {
        if (g_lijst[k].vormfout) {
            fprintf(stderr, "patchmem: regel %u van %s: %s\n",
                    g_lijst[k].regel, pad, g_lijst[k].melding);
        }
    }
    return 1;
}

/* ---------------------------------------------------------------- het verslag opmaken */

static patchmem_verslag maak_verslag(void)
{
    patchmem_verslag v;
    size_t i;

    memset(&v, 0, sizeof v);
    for (i = 0; i < g_aantal; i++) {
        switch (g_lijst[i].status) {
        case PATCHMEM_TOEGEPAST:     v.toegepast++; break;
        case PATCHMEM_AL_GOED:       v.al_goed++; break;
        case PATCHMEM_GEEN_MODULE:   v.geen_module++; break;
        case PATCHMEM_VERWACHT_FOUT: v.verwacht_fout++; break;
        case PATCHMEM_MISLUKT:
            if (g_lijst[i].vormfout) v.vormfout++;
            else v.mislukt++;
            break;
        default: break;
        }
    }
    v.gelezen = g_gelezen;
    return v;
}

/* ---------------------------------------------------------------- de openbare functies */

int patchmem_verbose(void)
{
    if (g_verbose < 0) {
        const char *v = getenv("RAVENGUN_PATCHMEM_VERBOSE");
        g_verbose = (v && *v) ? 1 : 0;
    }
    return g_verbose;
}

const char *patchmem_version(void)
{
    return "ravengun-patchmem " PATCHMEM_VERSIE;
}

patchmem_verslag patchmem_verslag_van(void)
{
    patchmem_verslag v;
    SLOT_NEMEN();
    v = maak_verslag();
    SLOT_GEVEN();
    return v;
}

const patchmem_patch *patchmem_lijst(size_t *aantal)
{
    if (aantal) *aantal = g_aantal;
    return g_lijst;
}

int patchmem_pending(void)
{
    size_t i;
    int n = 0;

    SLOT_NEMEN();
    for (i = 0; i < g_aantal; i++) {
        if (g_lijst[i].status == PATCHMEM_NIEUW || g_lijst[i].status == PATCHMEM_GEEN_MODULE) n++;
    }
    SLOT_GEVEN();
    return n;
}

int patchmem_apply_pending(void)
{
    size_t i;
    int voor, na;
    patchmem_verslag v;

    if (!g_gelezen) return 0;
    SLOT_NEMEN();
    voor = 0;
    for (i = 0; i < g_aantal; i++) {
        if (g_lijst[i].status == PATCHMEM_NIEUW || g_lijst[i].status == PATCHMEM_GEEN_MODULE) voor++;
    }
    for (i = 0; i < g_aantal; i++) pas_toe(&g_lijst[i]);
    na = 0;
    for (i = 0; i < g_aantal; i++) {
        if (g_lijst[i].status == PATCHMEM_NIEUW || g_lijst[i].status == PATCHMEM_GEEN_MODULE) na++;
    }
    v = maak_verslag();
    SLOT_GEVEN();

    /* De overgang van "wacht" naar "toegepast" is het bewijs dat de achtergrondthread werkt; die
     * melden we ook zonder VERBOSE, anders is er van buiten niets van te zien. */
    if (na < voor) {
        if (na == 0) {
            fprintf(stderr, "patchmem: klaar -- %zu toegepast, %zu stond er al, %zu geweigerd, "
                            "%zu mislukt\n",
                    v.toegepast, v.al_goed, v.verwacht_fout + v.vormfout, v.mislukt);
        } else {
            fprintf(stderr, "patchmem: %d van de %d wachtende patches toegepast; %d wachten nog\n",
                    voor - na, voor, na);
        }
    }
    return na;
}

void patchmem_rapporteer_openstaand(void)
{
    size_t i;

    if (g_openstaand_gemeld) return;
    g_openstaand_gemeld = 1;
    SLOT_NEMEN();
    for (i = 0; i < g_aantal; i++) {
        if (g_lijst[i].status == PATCHMEM_NIEUW || g_lijst[i].status == PATCHMEM_GEEN_MODULE) {
            fprintf(stderr, "patchmem: NIET toegepast -- %s+0x%llx: module %s is niet geladen\n",
                    g_lijst[i].module, g_lijst[i].offset, g_lijst[i].module);
        }
    }
    SLOT_GEVEN();
}

/* De geladen modules tonen. Dit gebruikt de tweede haak (patchmem_foreach_module) en is het
 * hulpmiddel bij "de module wordt niet gevonden": dan zie je meteen hoe hij werkelijk heet. */
struct tooncontext {
    size_t aantal;
};

static int cb_toon(const char *naam, void *basis, void *user)
{
    struct tooncontext *t = (struct tooncontext *)user;
    t->aantal++;
    fprintf(stderr, "patchmem:   geladen: %-40s basis %p\n", naam, basis);
    return 0;
}

void patchmem_toon_modules(void)
{
    struct tooncontext t;

    t.aantal = 0;
    fprintf(stderr, "patchmem: geladen modules:\n");
    patchmem_foreach_module(cb_toon, &t);
    if (t.aantal == 0) {
        fprintf(stderr, "patchmem:   (geen enkele module gevonden -- de haak werkt niet)\n");
    }
}

int patchmem_apply_file(const char *pad)
{
    size_t i;
    patchmem_verslag v;

    slot_klaarmaken();
    if (!pad || !*pad) {
        fprintf(stderr, "patchmem: geen patchlijst opgegeven\n");
        return -1;
    }

    SLOT_NEMEN();
    if (!lees_lijst(pad)) {
        SLOT_GEVEN();
        return -1;
    }
    for (i = 0; i < g_aantal; i++) pas_toe(&g_lijst[i]);
    v = maak_verslag();
    SLOT_GEVEN();

    fprintf(stderr, "patchmem: %s: %zu patch(es) gelezen -- %zu toegepast, %zu stond er al, "
                    "%zu wacht op een module, %zu geweigerd (verwacht klopte niet), %zu mislukt\n",
            pad, v.gelezen, v.toegepast, v.al_goed, v.geen_module,
            v.verwacht_fout + v.vormfout, v.mislukt);

    if (v.verwacht_fout || v.vormfout || v.mislukt) return (int)(v.verwacht_fout + v.vormfout + v.mislukt);
    return (int)v.geen_module;
}

int patchmem_apply_from_env(void)
{
    const char *pad = getenv("RAVENGUN_PATCHMEM_FILE");

    if (!pad || !*pad) {
        /* Zonder RAVENGUN_PATCHMEM_FILE gebeurt er niets. Dat is met opzet: een drop-in die in elke
         * game geladen wordt mag niet uit zichzelf gaan schrijven. */
        return -1;
    }
    return patchmem_apply_file(pad);
}
