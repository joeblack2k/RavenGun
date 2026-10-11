/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rt-glfix — cdecl-brug voor de 32-bits Raw Thrills-ELF's onder ElfLdr2/BudgieLoader.
 *
 * WAT ER MISGAAT (gemeten; zie docs/rt-glfix.md):
 *   De game is een Linux-ELF en is met cdecl gebouwd. ElfLdr2 zet in de GOT van de game de
 *   adressen van Wine's opengl32.dll. Die exporten zijn echter STDCALL: ze eindigen op
 *   `ret <n>` en halen dus zelf de argumenten van de stapel. De game ruimt ze daarna nog eens
 *   op. De stapelpointer loopt daardoor per GL-aanroep 4*n bytes op; bij de epiloog van
 *   VidStart (0x8357df0) leest de game een verkeerd retouradres en springt naar 0x58000:
 *   EXCEPTION_ACCESS_VIOLATION, en de lader stopt met 255.
 *
 * WAT DEZE BIBLIOTHEEK DOET:
 *   1. Ze wacht tot ElfLdr2 het spelbeeld heeft geplaatst en de GOT heeft gevuld.
 *   2. Ze zoekt de PLT-stubs van het spel (`ff 25 <adres> 68 ef be ad de`) en daarmee alle
 *      GOT-invoeren die de game via de PLT gebruikt.
 *   3. Voor elke invoer die naar een `gl*`-export van opengl32.dll wijst zet ze een eigen
 *      cdecl-stub: een stukje code dat de stdcall-functie aanroept en daarna de stapelpointer
 *      herstelt. De stub hoeft het aantal argumenten NIET te kennen: hij bewaart esp voor de
 *      aanroep en zet hem erna terug (zie rtg_make_bridge()).
 *   4. Ze vervangt de GOT-invoer van glGetIntegerv door een eigen wacht die voor
 *      GL_MAX_TEXTURE_SIZE (0xD33) een veilige waarde > 2047 teruggeeft.
 *   5. Ze vervangt de GOT-invoer van dlsym door een eigen wacht, zodat GLEW-lookups dezelfde
 *      cdecl-stubs krijgen in plaats van de ruwe stdcall-adressen.
 *
 * OPGELADEN VIA LD_AUDIT, NIET VIA LD_PRELOAD: BudgieLoader is een Cygwin-programma en Cygwin
 * leest LD_PRELOAD zelf. Het probeert onze ELF dan als Windows-DLL te laden en sterft
 * ("fatal error - error while loading shared libraries"), nog voordat de game begint. LD_AUDIT
 * kent Cygwin niet, glibc wel. Zie docs/rt-glfix.md, "Het kanaal".
 *
 * Omgeving:
 *   RAVENGUN_GLFIX_GAME      naam van de game (alleen voor het logboek)
 *   RAVENGUN_GLFIX_LOG       pad voor het logbestand (relatief = ten opzichte van de werkmap)
 *   RAVENGUN_GLFIX_VERBOSE   gezet: ook naar stderr
 *   RAVENGUN_GLFIX_MODE      1 = GOT-stubs, 2 = eigen glGetIntegerv, 4 = dlsym-wacht (standaard 7)
 *   RAVENGUN_GLFIX_MAXXTEX   waarde voor GL_MAX_TEXTURE_SIZE (standaard 8192)
 *   RAVENGUN_GLFIX_MODULE    de GL-module om op te richten (standaard opengl32.dll)
 *   RAVENGUN_GLFIX_MS        poll-interval in milliseconden (standaard 1)
 *   RAVENGUN_GLFIX_SECS      hoe lang blijven kijken (standaard 45)
 *
 * Bouwen: scripts/rt-glfix.sh (gcc -m32 -shared -fPIC).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define GL_MAX_TEXTURE_SIZE 0xD33u

#define MODE_GOT   1
#define MODE_GETIV 2
#define MODE_DLSYM 4

/* ---------------------------------------------------------------- logboek */

static FILE *rtg_log;
static int rtg_verbose;
static char rtg_game[64] = "?";

static void rtg_open_log(void)
{
    const char *p = getenv("RAVENGUN_GLFIX_LOG");
    const char *v = getenv("RAVENGUN_GLFIX_VERBOSE");
    const char *g = getenv("RAVENGUN_GLFIX_GAME");
    rtg_verbose = v && *v && strcmp(v, "0") != 0;
    if (g && *g) {
        strncpy(rtg_game, g, sizeof(rtg_game) - 1);
        rtg_game[sizeof(rtg_game) - 1] = 0;
    }
    if (p && *p)
        rtg_log = fopen(p, "a");
    if (!rtg_log && rtg_verbose)
        rtg_log = stderr;
}

static void rtg_say(const char *fmt, ...)
{
    va_list ap;
    if (!rtg_log)
        return;
    fprintf(rtg_log, "[rt-glfix %s] ", rtg_game);
    va_start(ap, fmt);
    vfprintf(rtg_log, fmt, ap);
    va_end(ap);
    fputc('\n', rtg_log);
    fflush(rtg_log);
}

/* ---------------------------------------------------------------- hulp */

static long rtg_env_long(const char *naam, long standaard)
{
    const char *v = getenv(naam);
    char *eind;
    long n;
    if (!v || !*v)
        return standaard;
    n = strtol(v, &eind, 0);
    return (eind == v) ? standaard : n;
}

/* De laagste mapping van een module (het beeld-adres) en het hoogste adres dat erbij hoort. */
static int rtg_module_range(const char *naam, uint32_t *base, uint32_t *eind)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char regel[512];
    uint32_t lo = 0, hi = 0;
    size_t n = strlen(naam);
    if (!f)
        return 0;
    while (fgets(regel, sizeof(regel), f)) {
        char *p = strchr(regel, '/');
        char *q;
        unsigned long a, b;
        if (!p)
            continue;
        q = p + strlen(p);
        while (q > p && (q[-1] == '\n' || q[-1] == ' ' || q[-1] == '\r'))
            q--;
        if ((size_t)(q - p) < n || strncmp(q - n, naam, n) != 0)
            continue;
        if (sscanf(regel, "%lx-%lx", &a, &b) != 2)
            continue;
        if (!lo || a < lo)
            lo = (uint32_t)a;
        if ((uint32_t)b > hi)
            hi = (uint32_t)b;
    }
    fclose(f);
    if (!lo)
        return 0;
    *base = lo;
    *eind = hi;
    return 1;
}

/* Het spelbeeld: een anonieme uitvoerbare mapping die met een 32-bits ELF begint. */
static int rtg_game_image(uint32_t *base, uint32_t *grootte)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char regel[512];
    if (!f)
        return 0;
    while (fgets(regel, sizeof(regel), f)) {
        unsigned long a, b;
        char rechten[8];
        const unsigned char *p;
        if (strchr(regel, '/'))
            continue;                                  /* het spelbeeld is anoniem */
        if (sscanf(regel, "%lx-%lx %7s", &a, &b, rechten) != 3)
            continue;
        if (rechten[0] != 'r' || rechten[2] != 'x')
            continue;
        if (b - a < 4u * 1024u * 1024u)
            continue;
        p = (const unsigned char *)(uintptr_t)a;
        if (p[0] != 0x7f || p[1] != 'E' || p[2] != 'L' || p[3] != 'F')
            continue;
        if (p[4] != 1 || p[5] != 1)                    /* 32-bits, little endian */
            continue;
        *base = (uint32_t)a;
        *grootte = (uint32_t)(b - a);
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------- PE-exporttabel */

#define RTG_RVA_MAX 0x20000000u

static int rtg_pe_dir(uint32_t base, uint32_t *nnam, uint32_t *addr_fun, uint32_t *addr_nam,
                      uint32_t *addr_ord)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)base;
    uint32_t pe, exp;
    uint16_t magic;
    if (m[0] != 'M' || m[1] != 'Z')
        return 0;
    memcpy(&pe, m + 0x3c, 4);
    if (pe > RTG_RVA_MAX || memcmp(m + pe, "PE\0\0", 4) != 0)
        return 0;
    memcpy(&magic, m + pe + 24, 2);
    memcpy(&exp, m + pe + 24 + (magic == 0x10b ? 96 : 112), 4);
    if (!exp || exp > RTG_RVA_MAX)
        return 0;
    memcpy(nnam, m + exp + 24, 4);
    memcpy(addr_fun, m + exp + 28, 4);
    memcpy(addr_nam, m + exp + 32, 4);
    memcpy(addr_ord, m + exp + 36, 4);
    if (*nnam > 65536 || *addr_nam > RTG_RVA_MAX || *addr_ord > RTG_RVA_MAX || *addr_fun > RTG_RVA_MAX)
        return 0;
    return 1;
}

static int rtg_pe_find(uint32_t base, const char *naam, uint32_t *rva)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)base;
    uint32_t nnam, addr_fun, addr_nam, addr_ord, i;
    if (!rtg_pe_dir(base, &nnam, &addr_fun, &addr_nam, &addr_ord))
        return 0;
    for (i = 0; i < nnam; i++) {
        uint32_t nr, ordi, frva;
        const char *s;
        memcpy(&nr, m + addr_nam + i * 4, 4);
        if (nr > RTG_RVA_MAX)
            continue;
        s = (const char *)(m + nr);
        if (strncmp(s, naam, 64) != 0)
            continue;
        memcpy(&ordi, m + addr_ord + i * 2, 2);
        memcpy(&frva, m + addr_fun + ordi * 4, 4);
        *rva = frva;
        return 1;
    }
    return 0;
}

static int rtg_pe_name(uint32_t base, uint32_t adres, char *buf, size_t n)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)base;
    uint32_t nnam, addr_fun, addr_nam, addr_ord, i;
    if (!rtg_pe_dir(base, &nnam, &addr_fun, &addr_nam, &addr_ord))
        return 0;
    for (i = 0; i < nnam; i++) {
        uint32_t nr, ordi, frva;
        memcpy(&nr, m + addr_nam + i * 4, 4);
        memcpy(&ordi, m + addr_ord + i * 2, 2);
        memcpy(&frva, m + addr_fun + ordi * 4, 4);
        if (base + frva != adres)
            continue;
        if (nr > RTG_RVA_MAX)
            return 0;
        snprintf(buf, n, "%s", (const char *)(m + nr));
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- de cdecl-brug */

/*
 * rtg_make_bridge() bouwt twee stukjes code in de RWX-pagina:
 *
 *   common  komt binnen met esp = E (de stapel van de game), [E] = het retouradres van de game,
 *           [E+4..] = de argumenten, en ecx = het adres van de echte (stdcall-)functie. Het
 *           bewaart E en het retouradres in een reddingsplek, zet het adres van `fixer` op [E]
 *           en springt naar de echte functie.
 *   fixer   wordt bereikt doordat de echte functie zijn `ret <n>` uitvoert. Het zet esp terug op
 *           E, zet het echte retouradres terug op [E] en springt daarheen. De game ziet dan
 *           precies de cdecl-situatie die hij verwacht en ruimt zelf zijn argumenten op.
 *
 * De reddingsplek wordt gekozen met de thread-sleutel uit fs:[0x8] (in een 32-bits Wine-proces
 * de TEB, in glibc de thread-descriptor; in beide gevallen per thread verschillend), zodat twee
 * threads elkaar niet in de weg zitten. Alleen eax en edx worden gebruikt; dat zijn vluchtige
 * registers, dus de game merkt er niets van.
 */
static uint8_t *rtg_page;
static uint8_t *rtg_common;
static uint8_t *rtg_fixer;
static uint8_t *rtg_stub_vrij;
static uint8_t *rtg_page_eind;
static void *volatile rtg_slot_table;
static uint32_t rtg_slots[64 * 2];

#define RTG_SLOTS 64
#define RTG_STUB_LEN 10

static void rtg_em8(uint8_t **p, uint8_t b) { *(*p)++ = b; }
static void rtg_em32(uint8_t **p, uint32_t v) { memcpy(*p, &v, 4); *p += 4; }

/* `mov edx, <&rtg_slot_table>; mov eax, fs:[8]; edx += (fs:[8]>>4 & 63)*8` */
static void rtg_em_slot(uint8_t **p)
{
    uint32_t tabel = (uint32_t)(uintptr_t)&rtg_slot_table;
    rtg_em8(p, 0x8b); rtg_em8(p, 0x15); rtg_em32(p, tabel);      /* mov edx,[tabel] */
    rtg_em8(p, 0x64); rtg_em8(p, 0xa1); rtg_em32(p, 0x08);       /* mov eax,fs:[8] */
    rtg_em8(p, 0xc1); rtg_em8(p, 0xe8); rtg_em8(p, 4);           /* shr eax,4 */
    rtg_em8(p, 0x83); rtg_em8(p, 0xe0); rtg_em8(p, RTG_SLOTS - 1);/* and eax,63 */
    rtg_em8(p, 0xc1); rtg_em8(p, 0xe0); rtg_em8(p, 3);           /* shl eax,3 */
    rtg_em8(p, 0x01); rtg_em8(p, 0xc2);                          /* add edx,eax */
}

static void rtg_make_bridge(void)
{
    uint8_t *p = rtg_page;
    uint8_t *plek;
    uint32_t fix;

    rtg_common = p;
    rtg_em_slot(&p);
    rtg_em8(&p, 0x89); rtg_em8(&p, 0x22);                        /* mov [edx],esp */
    rtg_em8(&p, 0x8b); rtg_em8(&p, 0x04); rtg_em8(&p, 0x24);     /* mov eax,[esp] */
    rtg_em8(&p, 0x89); rtg_em8(&p, 0x42); rtg_em8(&p, 0x04);     /* mov [edx+4],eax */
    rtg_em8(&p, 0xc7); rtg_em8(&p, 0x04); rtg_em8(&p, 0x24);     /* mov dword [esp],fixer */
    plek = p;
    p += 4;
    rtg_em8(&p, 0xff); rtg_em8(&p, 0xe1);                        /* jmp ecx */

    rtg_fixer = p;
    rtg_em_slot(&p);
    rtg_em8(&p, 0x8b); rtg_em8(&p, 0x22);                        /* mov esp,[edx] */
    rtg_em8(&p, 0x8b); rtg_em8(&p, 0x42); rtg_em8(&p, 0x04);     /* mov eax,[edx+4] */
    rtg_em8(&p, 0x89); rtg_em8(&p, 0x04); rtg_em8(&p, 0x24);     /* mov [esp],eax */
    rtg_em8(&p, 0xff); rtg_em8(&p, 0xe0);                        /* jmp eax */

    rtg_stub_vrij = p;
    fix = (uint32_t)(uintptr_t)rtg_fixer;
    memcpy(plek, &fix, 4);
}

/* Een stub voor één GOT-invoer: `mov ecx,<echte functie>; jmp common`. */
static void *rtg_make_stub(uint32_t echt)
{
    uint8_t *s;
    int32_t rel;
    if (!rtg_stub_vrij || rtg_stub_vrij + RTG_STUB_LEN > rtg_page_eind)
        return NULL;
    s = rtg_stub_vrij;
    s[0] = 0xb9;
    memcpy(s + 1, &echt, 4);
    s[5] = 0xe9;
    rel = (int32_t)(rtg_common - (s + RTG_STUB_LEN));
    memcpy(s + 6, &rel, 4);
    rtg_stub_vrij += RTG_STUB_LEN;
    return s;
}

/* ---------------------------------------------------------------- eigen wachten */

static void *(*rtg_orig_dlsym)(void *, const char *);
static void (*rtg_glget_cdecl)(unsigned int, int *);   /* onze eigen cdecl-stub */
static uint32_t rtg_max_tex = 8192;
static int rtg_mode = MODE_GOT | MODE_GETIV | MODE_DLSYM;
static uint32_t rtg_glbase;
static uint32_t rtg_gleind;

static void rt_glGetIntegerv(unsigned int pname, int *params)
{
    if (pname == GL_MAX_TEXTURE_SIZE) {
        if (params)
            *params = (int)rtg_max_tex;
        return;
    }
    if (rtg_glget_cdecl)
        rtg_glget_cdecl(pname, params);
}

static void *rtg_stub_voor_naam(const char *naam)
{
    uint32_t rva;
    if (!rtg_glbase)
        return NULL;
    if (strcmp(naam, "glGetIntegerv") == 0 && rtg_glget_cdecl)
        return (void *)rt_glGetIntegerv;
    if (!rtg_pe_find(rtg_glbase, naam, &rva))
        return NULL;
    return rtg_make_stub(rtg_glbase + rva);
}

static void *rt_dlsym(void *handle, const char *naam)
{
    if (naam && naam[0] == 'g' && naam[1] == 'l' && naam[2] && naam[2] != 'X') {
        void *p = rtg_stub_voor_naam(naam);
        if (p)
            return p;
    }
    if (rtg_orig_dlsym)
        return rtg_orig_dlsym(handle, naam);
    return NULL;
}

/* ---------------------------------------------------------------- het aanbrengen */

struct rtg_slot {
    uint32_t adres;
    uint32_t nieuw;
    char naam[64];
};

static struct rtg_slot *rtg_lijst;
static int rtg_lijst_n;

static int rtg_scan_plt(uint32_t base, uint32_t grootte)
{
    const uint8_t *img = (const uint8_t *)(uintptr_t)base;
    uint32_t i;
    int n = 0;
    rtg_lijst = malloc(sizeof(struct rtg_slot) * 1024);
    if (!rtg_lijst)
        return 0;
    for (i = 0; i + 11 <= grootte; i++) {
        uint32_t adres;
        int j, dubbel = 0;
        if (img[i] != 0xff || img[i + 1] != 0x25)
            continue;
        if (memcmp(img + i + 6, "\x68\xef\xbe\xad\xde", 5) != 0)
            continue;
        memcpy(&adres, img + i + 2, 4);
        for (j = 0; j < n; j++)
            if (rtg_lijst[j].adres == adres) {
                dubbel = 1;
                break;
            }
        if (dubbel)
            continue;
        if (n >= 1024)
            break;
        rtg_lijst[n].adres = adres;
        rtg_lijst[n].nieuw = 0;
        rtg_lijst[n].naam[0] = 0;
        n++;
    }
    rtg_lijst_n = n;
    return n;
}

static int rtg_is_gl_naam(const char *s)
{
    return s[0] == 'g' && s[1] == 'l' && s[2] >= 'A' && s[2] <= 'Z';
}

static int rtg_patch_ronde(int *gezien)
{
    int i, nieuw = 0;
    for (i = 0; i < rtg_lijst_n; i++) {
        uint32_t waarde;
        char naam[64];
        void *stub;
        waarde = *(volatile uint32_t *)(uintptr_t)rtg_lijst[i].adres;
        if (waarde < rtg_glbase || waarde >= rtg_gleind)
            continue;
        if (!rtg_pe_name(rtg_glbase, waarde, naam, sizeof(naam)))
            continue;
        if (!rtg_is_gl_naam(naam))
            continue;
        (*gezien)++;
        if ((rtg_mode & MODE_GETIV) && strcmp(naam, "glGetIntegerv") == 0) {
            if (!rtg_glget_cdecl)
                rtg_glget_cdecl = rtg_make_stub(waarde);
            stub = (void *)rt_glGetIntegerv;
        } else {
            stub = rtg_make_stub(waarde);
        }
        if (!stub)
            continue;
        snprintf(rtg_lijst[i].naam, sizeof(rtg_lijst[i].naam), "%s", naam);
        rtg_lijst[i].nieuw = (uint32_t)(uintptr_t)stub;
        *(volatile uint32_t *)(uintptr_t)rtg_lijst[i].adres = rtg_lijst[i].nieuw;
        nieuw++;
    }
    return nieuw;
}

static int rtg_patch_dlsym(void)
{
    uint32_t msys, msys_eind;
    int i;
    if (!(rtg_mode & MODE_DLSYM))
        return 0;
    if (!rtg_module_range("msys-2.0.dll", &msys, &msys_eind))
        return 0;
    for (i = 0; i < rtg_lijst_n; i++) {
        uint32_t w = *(volatile uint32_t *)(uintptr_t)rtg_lijst[i].adres;
        char naam[64];
        if (w < msys || w >= msys_eind)
            continue;
        if (!rtg_pe_name(msys, w, naam, sizeof(naam)) || strcmp(naam, "dlsym") != 0)
            continue;
        rtg_orig_dlsym = (void *(*)(void *, const char *))(uintptr_t)w;
        *(volatile uint32_t *)(uintptr_t)rtg_lijst[i].adres = (uint32_t)(uintptr_t)rt_dlsym;
        rtg_say("dlsym-wacht geplaatst: GOT 0x%08x was 0x%08x", rtg_lijst[i].adres, w);
        return 1;
    }
    return 0;
}

static void *rtg_watch(void *arg)
{
    uint32_t base = 0, grootte = 0;
    long ms = rtg_env_long("RAVENGUN_GLFIX_MS", 1);
    long secs = rtg_env_long("RAVENGUN_GLFIX_SECS", 45);
    long rondes, r;
    int gedaan = 0, stil = 0, totaal = 0;
    const char *mod = getenv("RAVENGUN_GLFIX_MODULE");

    (void)arg;
    if (!mod || !*mod)
        mod = "opengl32.dll";
    if (ms < 1)
        ms = 1;
    rondes = (secs * 1000) / ms;

    for (r = 0; r < rondes; r++) {
        int gezien = 0, nieuw;
        if (!base) {
            if (!rtg_game_image(&base, &grootte)) {
                usleep((useconds_t)(ms * 1000));
                continue;
            }
            rtg_say("spelbeeld gevonden op 0x%08x (%u bytes)", base, grootte);
            rtg_say("PLT-invoeren: %d", rtg_scan_plt(base, grootte));
        }
        if (!rtg_glbase) {
            if (!rtg_module_range(mod, &rtg_glbase, &rtg_gleind)) {
                usleep((useconds_t)(ms * 1000));
                continue;
            }
            rtg_say("%s op 0x%08x-0x%08x", mod, rtg_glbase, rtg_gleind);
        }
        nieuw = rtg_patch_ronde(&gezien);
        totaal += nieuw;
        if (nieuw) {
            rtg_say("ronde %ld: %d GL-invoeren omgezet (%d gezien)", r, nieuw, gezien);
            gedaan = 1;
        }
        if (gedaan && !rtg_orig_dlsym)
            rtg_patch_dlsym();
        if (gedaan && gezien == 0 && nieuw == 0) {
            if (++stil >= 50) {
                rtg_say("klaar: %d GL-invoeren omgezet, %d invoeren bekeken", totaal, rtg_lijst_n);
                break;
            }
        } else {
            stil = 0;
        }
        usleep((useconds_t)(ms * 1000));
    }
    if (!gedaan) {
        rtg_say("geen enkele GL-invoer gevonden in %ld s", secs);
    } else {
        int i;
        for (i = 0; i < rtg_lijst_n; i++)
            if (rtg_lijst[i].nieuw)
                rtg_say("  0x%08x -> 0x%08x  %s", rtg_lijst[i].adres, rtg_lijst[i].nieuw, rtg_lijst[i].naam);
    }
    return NULL;
}

/* ---------------------------------------------------------------- opstarten */

static pthread_t rtg_thread;
static int rtg_gestart;

static void rtg_ensure_watcher(void)
{
    if (rtg_gestart)
        return;
    rtg_gestart = 1;
    rtg_mode = (int)rtg_env_long("RAVENGUN_GLFIX_MODE", rtg_mode);
    rtg_max_tex = (uint32_t)rtg_env_long("RAVENGUN_GLFIX_MAXXTEX", 8192);
    rtg_page = mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (rtg_page == MAP_FAILED) {
        rtg_page = NULL;
        rtg_say("geen uitvoerbare pagina (mmap mislukt); niets gedaan");
        return;
    }
    rtg_page_eind = rtg_page + 8192;
    rtg_slot_table = rtg_slots;
    memset(rtg_slots, 0, sizeof(rtg_slots));
    rtg_make_bridge();
    rtg_say("geladen (pid %d, mode %d, maxtex %u, brug op %p)", (int)getpid(), rtg_mode, rtg_max_tex,
            (void *)rtg_common);
    if (pthread_create(&rtg_thread, NULL, rtg_watch, NULL) != 0) {
        rtg_say("kon de kijker-thread niet starten");
        rtg_gestart = 0;
    }
}

__attribute__((constructor))
static void rtg_init(void)
{
    rtg_open_log();
    rtg_ensure_watcher();
}

/* LD_AUDIT: glibc roept deze twee aan. la_preinit valt vlak voor de start van het programma;
   dat is een veilige plek om de kijker alsnog te starten (libc en pthreads staan dan). */
unsigned int la_version(unsigned int v)
{
    return v;
}

void la_preinit(uintptr_t *cookie)
{
    (void)cookie;
    rtg_ensure_watcher();
}
