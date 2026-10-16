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
 * Bouwen: tools/dev/rt-glfix.sh (gcc -m32 -shared -fPIC).
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
#include <signal.h>
#include <ucontext.h>
#include <sys/prctl.h>

#define GL_MAX_TEXTURE_SIZE 0xD33u

#define MODE_GOT     1
#define MODE_GETIV   2
#define MODE_DLSYM   4
#define MODE_STUBLOG 8   /* de lege ElfLdr2-stubs vervangen door een wacht die telt en logt */

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
static uint32_t rtg_slots[64 * 4];      /* per thread: [0]=esp, [4]=retouradres, [8]=eax */

#define RTG_SLOTS 64
#define RTG_STUB_LEN 10

static void rtg_em8(uint8_t **p, uint8_t b) { *(*p)++ = b; }
static void rtg_em32(uint8_t **p, uint32_t v) { memcpy(*p, &v, 4); *p += 4; }

/* Berekent in edx de reddingsplek van deze thread.
   In `common` mag daarvoor eax gebruikt worden; in `fixer` niet, want daar staat de retourwaarde
   van de zojuist afgelopen functie in eax. Daarom is er een variant die ecx gebruikt. */
static void rtg_em_slot_eax(uint8_t **p)
{
    uint32_t tabel = (uint32_t)(uintptr_t)&rtg_slot_table;
    rtg_em8(p, 0x8b); rtg_em8(p, 0x15); rtg_em32(p, tabel);      /* mov edx,[tabel] */
    rtg_em8(p, 0x65); rtg_em8(p, 0xa1); rtg_em32(p, 0x08);       /* mov eax,gs:[8] */
    rtg_em8(p, 0xc1); rtg_em8(p, 0xe8); rtg_em8(p, 4);           /* shr eax,4 */
    rtg_em8(p, 0x83); rtg_em8(p, 0xe0); rtg_em8(p, RTG_SLOTS - 1);/* and eax,63 */
    rtg_em8(p, 0xc1); rtg_em8(p, 0xe0); rtg_em8(p, 4);           /* shl eax,4 */
    rtg_em8(p, 0x01); rtg_em8(p, 0xc2);                          /* add edx,eax */
}

static void rtg_em_slot_ecx(uint8_t **p)
{
    uint32_t tabel = (uint32_t)(uintptr_t)&rtg_slot_table;
    rtg_em8(p, 0x8b); rtg_em8(p, 0x15); rtg_em32(p, tabel);      /* mov edx,[tabel] */
    rtg_em8(p, 0x65); rtg_em8(p, 0x8b); rtg_em8(p, 0x0d); rtg_em32(p, 0x08); /* mov ecx,gs:[8] */
    rtg_em8(p, 0xc1); rtg_em8(p, 0xe9); rtg_em8(p, 4);           /* shr ecx,4 */
    rtg_em8(p, 0x83); rtg_em8(p, 0xe1); rtg_em8(p, RTG_SLOTS - 1);/* and ecx,63 */
    rtg_em8(p, 0xc1); rtg_em8(p, 0xe1); rtg_em8(p, 4);           /* shl ecx,4 */
    rtg_em8(p, 0x01); rtg_em8(p, 0xca);                          /* add edx,ecx */
}

static void rtg_make_bridge(void)
{
    uint8_t *p = rtg_page;
    uint8_t *plek;
    uint32_t fix;

    rtg_common = p;
    rtg_em_slot_eax(&p);
    rtg_em8(&p, 0x89); rtg_em8(&p, 0x22);                        /* mov [edx],esp */
    rtg_em8(&p, 0x8b); rtg_em8(&p, 0x04); rtg_em8(&p, 0x24);     /* mov eax,[esp] */
    rtg_em8(&p, 0x89); rtg_em8(&p, 0x42); rtg_em8(&p, 0x04);     /* mov [edx+4],eax */
    rtg_em8(&p, 0xc7); rtg_em8(&p, 0x04); rtg_em8(&p, 0x24);     /* mov dword [esp],fixer */
    plek = p;
    p += 4;
    rtg_em8(&p, 0xff); rtg_em8(&p, 0xe1);                        /* jmp ecx */

    rtg_fixer = p;
    rtg_em_slot_ecx(&p);                                         /* laat eax met rust */
    rtg_em8(&p, 0x89); rtg_em8(&p, 0x42); rtg_em8(&p, 0x08);     /* mov [edx+8],eax (retourwaarde) */
    rtg_em8(&p, 0x8b); rtg_em8(&p, 0x22);                        /* mov esp,[edx] */
    rtg_em8(&p, 0x8b); rtg_em8(&p, 0x42); rtg_em8(&p, 0x04);     /* mov eax,[edx+4] */
    rtg_em8(&p, 0x89); rtg_em8(&p, 0x04); rtg_em8(&p, 0x24);     /* mov [esp],eax */
    rtg_em8(&p, 0x83); rtg_em8(&p, 0xc4); rtg_em8(&p, 0x04);     /* add esp,4 */
    rtg_em8(&p, 0x8b); rtg_em8(&p, 0x42); rtg_em8(&p, 0x08);     /* mov eax,[edx+8] */
    rtg_em8(&p, 0xff); rtg_em8(&p, 0x62); rtg_em8(&p, 0x04);     /* jmp dword [edx+4] */

    rtg_stub_vrij = p;
    fix = (uint32_t)(uintptr_t)rtg_fixer;
    memcpy(plek, &fix, 4);

    if (rtg_verbose) {
        int i;
        char regel[3 * 16 + 4];
        char *r;
        rtg_say("brug: common op %p (fixer op %p, plek op %p)", (void *)rtg_common,
                (void *)rtg_fixer, (void *)plek);
        for (i = 0; i < 128; i += 16) {
            int j;
            r = regel;
            for (j = 0; j < 16; j++)
                r += sprintf(r, "%02x ", rtg_page[i + j]);
            *r = 0;
            rtg_say("  +%02x: %s", i, regel);
        }
    }
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

/*
 * Zelftest van de brug. `rtg_test_stdcall2` is een neppe stdcall-functie: ze telt haar twee
 * argumenten op en haalt ze zelf van de stapel (`ret $8`), precies zoals opengl32.dll dat doet.
 * rtg_selftest_asm roept die functie aan via een gegenereerde stub, met de cdecl-verwachting van
 * de game, en controleert daarna dat de stapelpointer EXACT op de cdecl-stand staat: na een
 * `call` met twee argumenten is dat esp+4, dus 8 bytes onder de stand van voor de twee pushes.
 * Zonder die controle zou een fout in de brug pas als een crash in de game opduiken.
 */
__asm__(
".text\n"
".globl rtg_test_stdcall2\n"
".type rtg_test_stdcall2,@function\n"
"rtg_test_stdcall2:\n"
"    movl 4(%esp), %eax\n"
"    addl 8(%esp), %eax\n"
"    ret $8\n"
".size rtg_test_stdcall2,.-rtg_test_stdcall2\n"
".globl rtg_selftest_asm\n"
".type rtg_selftest_asm,@function\n"
"rtg_selftest_asm:\n"
"    pushl %ebp\n"
"    movl %esp, %ebp\n"
"    pushl %ebx\n"
"    pushl %esi\n"
"    pushl %edi\n"
"    movl 8(%ebp), %ebx\n"
"    movl %esp, %esi\n"
"    pushl $22\n"
"    pushl $20\n"
"    call *%ebx\n"
"    movl %esp, %ecx\n"
"    movl %esi, %esp\n"
"    xorl %edx, %edx\n"
"    leal -8(%esi), %edi\n"
"    cmpl %edi, %ecx\n"
"    je 1f\n"
"    movl $1, %edx\n"
"    jmp 2f\n"
"1:  cmpl $42, %eax\n"
"    je 2f\n"
"    movl $2, %edx\n"
"2:  movl %edx, %eax\n"
"    popl %edi\n"
"    popl %esi\n"
"    popl %ebx\n"
"    popl %ebp\n"
"    ret\n"
".size rtg_selftest_asm,.-rtg_selftest_asm\n"
);
extern int rtg_test_stdcall2(int, int);
extern int rtg_selftest_asm(void *stub);

/* 0 = goed, 1 = stapel verschoven, 2 = verkeerde uitkomst, 3 = geen stub. */
static int rtg_selftest(void)
{
    void *stub = rtg_make_stub((uint32_t)(uintptr_t)rtg_test_stdcall2);
    if (!stub)
        return 3;
    return rtg_selftest_asm(stub);
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

/* ---------------------------------------------------------------- de lege stubs van ElfLdr2

   Van de 93 GOT-invoeren die in het bestand een libGL-adres hebben, zet ElfLdr2 er maar 22 om
   naar een echte opengl32-export. De andere 71 komen uit op één adres in zijn nagebootste gebied,
   en dat adres is:

       55 8b ec 33 c0 5d c3     push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret

   Een cdecl no-op die nul teruggeeft: hij raakt de stapel niet, dus hij kan geen verschuiving
   veroorzaken, maar de GL-functie doet ook niets. Met MODE_STUBLOG vervangen we zulke invoeren
   door een wacht die telt hoe vaak hij aangeroepen wordt en met welk retouradres, zodat te meten
   is of de game die invoeren überhaupt gebruikt. */

static const unsigned char rtg_noop_sig[7] = {0x55, 0x8b, 0xec, 0x33, 0xc0, 0x5d, 0xc3};

struct rtg_stubteller {
    uint32_t slot;
    uint32_t keer;
    uint32_t eerste_ret;
};

static struct rtg_stubteller rtg_tellers[128];
static int rtg_tellers_n;

/* Niet statisch: de assembler-stub hieronder roept deze functie bij naam aan. */
void rt_stub_note(uint32_t slot, uint32_t ret)
{
    int i;
    for (i = 0; i < rtg_tellers_n; i++) {
        if (rtg_tellers[i].slot == slot) {
            if (rtg_tellers[i].keer++ == 0) {
                rtg_tellers[i].eerste_ret = ret;
                rtg_say("lege stub AANGEROEPEN: GOT 0x%08x, eerste keer vanaf 0x%08x", slot, ret);
            }
            return;
        }
    }
    if (rtg_tellers_n >= (int)(sizeof(rtg_tellers) / sizeof(rtg_tellers[0])))
        return;
    rtg_tellers[rtg_tellers_n].slot = slot;
    rtg_tellers[rtg_tellers_n].keer = 1;
    rtg_tellers[rtg_tellers_n].eerste_ret = ret;
    rtg_tellers_n++;
    rtg_say("lege stub AANGEROEPEN: GOT 0x%08x, eerste keer vanaf 0x%08x", slot, ret);
}

__asm__(
".text\n"
".globl rt_stub_log\n"
".type rt_stub_log,@function\n"
"rt_stub_log:\n"
"    ret\n"
".size rt_stub_log,.-rt_stub_log\n"
);
extern void rt_stub_log(void);

/* De teller-wacht zelf, als machinecode in de RWX-pagina (zo hoeft er geen enkele relocatie in
   de tekst te staan; het adres van rt_stub_note gaat er als onmiddellijke waarde in).
   Bij binnenkomst: eax = het adres van de GOT-invoer, [esp] = retouradres van de game,
   [esp+4..] = de argumenten van de game. */
static uint8_t *rtg_notecode;

static void rtg_make_notecode(void)
{
    uint8_t *p = rtg_stub_vrij;
    uint32_t note = (uint32_t)(uintptr_t)rt_stub_note;
    if (!p || p + 32 > rtg_page_eind)
        return;
    rtg_em8(&p, 0x50);                                     /* push eax (slotadres) */
    rtg_em8(&p, 0xff); rtg_em8(&p, 0x74); rtg_em8(&p, 0x24); rtg_em8(&p, 0x04);  /* push [esp+4] */
    rtg_em8(&p, 0xb8); rtg_em32(&p, note);                 /* mov eax, rt_stub_note */
    rtg_em8(&p, 0xff); rtg_em8(&p, 0xd0);                  /* call eax */
    rtg_em8(&p, 0x83); rtg_em8(&p, 0xc4); rtg_em8(&p, 0x08); /* add esp,8 */
    rtg_em8(&p, 0x31); rtg_em8(&p, 0xc0);                  /* xor eax,eax */
    rtg_em8(&p, 0xc3);                                     /* ret */
    rtg_notecode = rtg_stub_vrij;
    rtg_stub_vrij = p;
}

static void *rtg_make_stublog(uint32_t slot)
{
    uint8_t *s;
    int32_t rel;
    if (!rtg_notecode)
        rtg_make_notecode();
    if (!rtg_notecode || !rtg_stub_vrij || rtg_stub_vrij + RTG_STUB_LEN > rtg_page_eind)
        return NULL;
    s = rtg_stub_vrij;
    s[0] = 0xb8;
    memcpy(s + 1, &slot, 4);                        /* mov eax, <slotadres> */
    s[5] = 0xe9;
    rel = (int32_t)(rtg_notecode - (s + RTG_STUB_LEN));
    memcpy(s + 6, &rel, 4);                         /* jmp <teller-wacht> */
    rtg_stub_vrij += RTG_STUB_LEN;
    return s;
}

/* Is dit adres uitvoerbaar gemapt? (om niet blind in een GOT-waarde te lezen)
   De lijst wordt één keer opgebouwd: per invoer /proc/self/maps lezen zou de ronde zo traag maken
   dat het aanbrengen de eerste GL-aanroepen van de game mist. */
static struct { uint32_t lo, hi; } rtg_exec[1024];
static int rtg_exec_n;
static int rtg_exec_gelezen;

static void rtg_lees_exec(void)
{
    FILE *f;
    char regel[512];
    rtg_exec_gelezen = 1;
    f = fopen("/proc/self/maps", "r");
    if (!f)
        return;
    while (fgets(regel, sizeof(regel), f)) {
        unsigned long a, b;
        char rechten[8];
        if (rtg_exec_n >= (int)(sizeof(rtg_exec) / sizeof(rtg_exec[0])))
            break;
        if (sscanf(regel, "%lx-%lx %7s", &a, &b, rechten) != 3)
            continue;
        if (rechten[0] == 'r' && rechten[2] == 'x') {
            rtg_exec[rtg_exec_n].lo = (uint32_t)a;
            rtg_exec[rtg_exec_n].hi = (uint32_t)b;
            rtg_exec_n++;
        }
    }
    fclose(f);
}

static int rtg_is_uitvoerbaar(uint32_t adres)
{
    int i;
    static unsigned lang;
    if (!rtg_exec_gelezen)
        rtg_lees_exec();
    for (i = 0; i < rtg_exec_n; i++)
        if (adres >= rtg_exec[i].lo && adres < rtg_exec[i].hi)
            return 1;
    /* Niet gevonden. ElfLdr2 mapt zijn nagebootste bibliotheken later dan dat het de GOT vult,
       dus de eerste keer kan de lijst te vroeg zijn. Hoogstens tien keer opnieuw lezen, en niet
       vaker dan één keer per 200 ms: anders wordt de ronde te traag en mist hij de eerste
       GL-aanroepen van de game. */
    if (lang < 10) {
        static long teller;
        if (teller++ % 200 == 0) {
            lang++;
            rtg_exec_n = 0;
            rtg_lees_exec();
            for (i = 0; i < rtg_exec_n; i++)
                if (adres >= rtg_exec[i].lo && adres < rtg_exec[i].hi)
                    return 1;
        }
    }
    return 0;
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
    static int diagnose_gedaan;
    if ((rtg_mode & MODE_STUBLOG) && !diagnose_gedaan) {
        int buiten = 0, uitv = 0, sig = 0;
        diagnose_gedaan = 1;
        for (i = 0; i < rtg_lijst_n; i++) {
            uint32_t w = *(volatile uint32_t *)(uintptr_t)rtg_lijst[i].adres;
            if (!w || (w >= rtg_glbase && w < rtg_gleind))
                continue;
            buiten++;
            if (!rtg_is_uitvoerbaar(w))
                continue;
            uitv++;
            if (memcmp((const void *)(uintptr_t)w, rtg_noop_sig, sizeof(rtg_noop_sig)) == 0)
                sig++;
        }
        rtg_say("diagnose: %d invoeren buiten de GL-module; %d daarvan uitvoerbaar; %d met de "
                "lege-stub-handtekening (uitvoerbare gebieden in de cache: %d)",
                buiten, uitv, sig, rtg_exec_n);
    }
    for (i = 0; i < rtg_lijst_n; i++) {
        uint32_t waarde;
        char naam[64];
        void *stub;
        waarde = *(volatile uint32_t *)(uintptr_t)rtg_lijst[i].adres;
        if (waarde < rtg_glbase || waarde >= rtg_gleind) {
            /* Geen opengl32-export. Is het een lege ElfLdr2-stub? Dan tellen we hem (MODE_STUBLOG). */
            if ((rtg_mode & MODE_STUBLOG) && waarde && rtg_lijst[i].nieuw == 0 &&
                rtg_is_uitvoerbaar(waarde) &&
                memcmp((const void *)(uintptr_t)waarde, rtg_noop_sig, sizeof(rtg_noop_sig)) == 0) {
                stub = rtg_make_stublog(rtg_lijst[i].adres);
                if (stub) {
                    snprintf(rtg_lijst[i].naam, sizeof(rtg_lijst[i].naam), "(lege ElfLdr2-stub)");
                    rtg_lijst[i].nieuw = (uint32_t)(uintptr_t)stub;
                    *(volatile uint32_t *)(uintptr_t)rtg_lijst[i].adres = rtg_lijst[i].nieuw;
                    nieuw++;
                }
            }
            continue;
        }
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
    /* Met MODE_STUBLOG blijven we de tellers uitlezen zolang het spel leeft; de thread verdwijnt
       vanzelf met het proces. Dat geeft de eindstand van elke lege stub. */
    if (rtg_mode & MODE_STUBLOG) {
        for (;;) {
            int i;
            unsigned lang = 0;
            sleep(5);
            for (i = 0; i < rtg_tellers_n; i++) {
                rtg_say("stub 0x%08x: %u keer aangeroepen (eerste keer vanaf 0x%08x)",
                        rtg_tellers[i].slot, rtg_tellers[i].keer, rtg_tellers[i].eerste_ret);
                lang++;
            }
            if (lang == 0)
                rtg_say("stubtellers: nog geen enkele lege stub aangeroepen");
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------- opstarten */

static pthread_t rtg_thread;
static int rtg_gestart;

/* ---------------------------------------------------------------- foutdiagnose
 * Opt-in via RAVENGUN_GLFIX_FAULT=1 (standaard uit, zodat normale runs en de andere games
 * ongewijzigd blijven). Bij een crash legt deze afhandelaar eenmalig de registers, de mappings van
 * eip/esp/foutadres, de bytes op eip en een stapel-scan naar uitvoerbare terugadressen vast (ebp=0
 * breekt een gewone backtrace). Daarna herstelt hij de vorige afhandelaar (bv. die van Wine) en
 * keert terug, zodat de oorspronkelijke crash-afhandeling onveranderd volgt. Zie docs/rt-glfix.md §6. */

struct rtg_map { uint32_t lo, hi; char perms[5]; char naam[64]; };

static int rtg_read_maps(struct rtg_map *v, int max)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char regel[512];
    int n = 0;
    if (!f)
        return 0;
    while (n < max && fgets(regel, sizeof(regel), f)) {
        unsigned long a, b;
        char perms[8] = {0};
        char pad[256] = {0};
        char *naam, *s;
        if (sscanf(regel, "%lx-%lx %7s %*x %*s %*u %255[^\n]", &a, &b, perms, pad) < 3)
            continue;
        v[n].lo = (uint32_t)a;
        v[n].hi = (uint32_t)b;
        strncpy(v[n].perms, perms, 4);
        v[n].perms[4] = 0;
        naam = pad;
        while (*naam == ' ')
            naam++;
        if (!*naam)
            naam = (char *)"(anoniem)";
        s = strrchr(naam, '/');
        if (s)
            naam = s + 1;
        strncpy(v[n].naam, naam, sizeof(v[n].naam) - 1);
        v[n].naam[sizeof(v[n].naam) - 1] = 0;
        n++;
    }
    fclose(f);
    return n;
}

static struct rtg_map *rtg_vind(struct rtg_map *v, int n, uint32_t addr)
{
    int i;
    for (i = 0; i < n; i++)
        if (addr >= v[i].lo && addr < v[i].hi)
            return &v[i];
    return NULL;
}

static void rtg_dump_addr(struct rtg_map *v, int n, const char *naam, uint32_t addr)
{
    struct rtg_map *m = rtg_vind(v, n, addr);
    if (m)
        rtg_say("  %-5s %08x  in %s %s", naam, addr, m->perms, m->naam);
    else
        rtg_say("  %-5s %08x  (niet gemapt)", naam, addr);
}

static volatile sig_atomic_t rtg_in_fault;
static const int rtg_sigs[4] = { SIGSEGV, SIGILL, SIGBUS, SIGFPE };
static struct sigaction rtg_old[4];

static void rtg_fault(int sig, siginfo_t *si, void *ucv)
{
    static struct rtg_map v[1024];
    ucontext_t *uc = (ucontext_t *)ucv;
    greg_t *g = uc->uc_mcontext.gregs;
    uint32_t eip = (uint32_t)g[REG_EIP], esp = (uint32_t)g[REG_ESP], ebp = (uint32_t)g[REG_EBP];
    uint32_t fout = (uint32_t)(uintptr_t)si->si_addr;
    struct rtg_map *me, *mesp;
    int n, i, gevonden = 0;

    if (rtg_in_fault) {
        signal(sig, SIG_DFL);
        return;
    }
    rtg_in_fault = 1;

    n = rtg_read_maps(v, 1024);
    rtg_say("==== FOUT: signaal %d, si_addr %08x, si_code %d ====", sig, fout, si->si_code);
    rtg_say("  eip %08x esp %08x ebp %08x eflags %08x", eip, esp, ebp, (uint32_t)g[REG_EFL]);
    rtg_say("  eax %08x ebx %08x ecx %08x edx %08x", (uint32_t)g[REG_EAX], (uint32_t)g[REG_EBX],
            (uint32_t)g[REG_ECX], (uint32_t)g[REG_EDX]);
    rtg_say("  esi %08x edi %08x", (uint32_t)g[REG_ESI], (uint32_t)g[REG_EDI]);
    rtg_dump_addr(v, n, "eip", eip);
    rtg_dump_addr(v, n, "esp", esp);
    rtg_dump_addr(v, n, "fout", fout);

    me = rtg_vind(v, n, eip);
    if (me && me->perms[0] == 'r') {
        const uint8_t *p = (const uint8_t *)(uintptr_t)eip;
        rtg_say("  bytes@eip %02x %02x %02x %02x %02x %02x %02x %02x", p[0], p[1], p[2], p[3], p[4],
                p[5], p[6], p[7]);
    } else {
        rtg_say("  bytes@eip: eip wijst niet naar een leesbare mapping (wilde sprong)");
    }

    mesp = rtg_vind(v, n, esp);
    if (mesp && mesp->perms[0] == 'r') {
        uint32_t top = esp + 512;
        if (top > mesp->hi)
            top = mesp->hi;
        rtg_say("  stapel-scan (uitvoerbare doelen = mogelijke terugadressen; ebp=0):");
        for (i = 0; esp + (uint32_t)i * 4 + 4 <= top && gevonden < 16; i++) {
            uint32_t w = *(const uint32_t *)(uintptr_t)(esp + (uint32_t)i * 4);
            struct rtg_map *mw = rtg_vind(v, n, w);
            if (mw && mw->perms[0] == 'r' && mw->perms[2] == 'x') {
                rtg_say("    [esp+%3d] %08x -> %s %s", i * 4, w, mw->perms, mw->naam);
                gevonden++;
            }
        }
        if (!gevonden)
            rtg_say("    (geen uitvoerbaar terugadres in de eerste 512 bytes)");
    } else {
        rtg_say("  stapel-scan: esp niet leesbaar");
    }
    rtg_say("==== einde foutdump; terug naar de vorige afhandelaar ====");

    for (i = 0; i < 4; i++)
        if (rtg_sigs[i] == sig) {
            sigaction(sig, &rtg_old[i], NULL);
            break;
        }
}

static void rtg_install_fault(void)
{
    static char stapel[64 * 1024];
    stack_t ss;
    struct sigaction sa;
    int i;
    if (!rtg_env_long("RAVENGUN_GLFIX_FAULT", 0))
        return;
    /* ptrace_scope=1 blokkeert een losse gdb-attach; dit staat elke gelijke-gebruiker-debugger toe
       (alleen in de diagnose-run, niet normaal). Zo kan gdb de vastgelopen hoofddraad bekijken. */
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    ss.ss_sp = stapel;
    ss.ss_size = sizeof(stapel);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = rtg_fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < 4; i++)
        sigaction(rtg_sigs[i], &sa, &rtg_old[i]);
    rtg_say("foutdiagnose aan (RAVENGUN_GLFIX_FAULT): afhandelaar op SEGV/ILL/BUS/FPE");
}

static void rtg_ensure_watcher(void)
{
    if (rtg_gestart)
        return;
    rtg_gestart = 1;
    rtg_install_fault();
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
    {
        int zelftest = rtg_selftest();
        static const char *uitleg[] = {"goed", "stapelpointer verschoven", "verkeerde uitkomst",
                                       "geen stub kunnen maken"};
        rtg_say("zelftest brug: %s", uitleg[zelftest < 0 || zelftest > 3 ? 3 : zelftest]);
    }
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
