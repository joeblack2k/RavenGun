/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hasp_inline_hook.c — de onderschepping van de HASP-aanroepen van de Linux/ELF-games van
 * RavenGun.
 *
 * WAAROM DIT BESTAAT (gemeten; zie docs/hasp-elf-hook.md)
 * -------------------------------------------------------
 * De HASP-cliënt van de vier gemeten games (Halo Fireteam, Terminator Salvation, Jurassic Park,
 * Galaga Assault) is NIET dynamisch gelinkt. Hij zit STATISCH in het `game`-bestand:
 *
 *   - `nm -D --undefined-only game`  -> 0 ongedefinieerde hasp-symbolen;
 *   - `readelf -r game`              -> 0 verplaatsingen met hasp in de naam;
 *   - `nm -D game`                   -> wél `T hasp_login`, `T hasp_read`, ... : de cliënt is
 *                                       gedefinieerd in de game zelf, op een vast adres;
 *   - de versiegebonden export `comaladdinhlx86envspoony@@HASPE_LINUX_LOADER_1.1` is de
 *     handtekening van de Sentinel LDK HASP Envelope: de pakker heeft de runtime van de fabrikant
 *     in het programma opgenomen.
 *
 * Gevolg: er is geen PLT-slot en geen GOT-invoer om te vullen. GOT-herschrijving (de techniek van
 * tools/rt-glfix) en `LD_PRELOAD`-symboolinterpositie werken hier dus NIET: de aanroepen gaan
 * rechtstreeks naar de code in het eigen beeld.
 *
 * WAT DEZE BIBLIOTHEEK DOET
 * -------------------------
 * Ze haakt de functies aan op hun INGANGSPUNT: de eerste bytes van `hasp_login`, `hasp_read`,
 * enzovoort worden vervangen door een sprong naar onze eigen functie. Dat is dezelfde aanpak die
 * TeknoParrot zelf gebruikt in `Halo Fireteam/g7/halo/halo.so`: `initialize()` op 0x569f roept acht
 * keer `flh_inline_hook` aan met de vaste adressen 0xb2d630, 0xb2fd63, ... van de game.
 *
 * De adressen komen NIET hardgecodeerd uit dit bestand: ze worden tijdens het draaien uit de
 * dynsym-tabel van de game gelezen (het proces is wine-preloader, dus de game wordt via zijn pad
 * van schijf gelezen) en met het laadadres van het spelbeeld verrekend. Daardoor werkt dezelfde
 * bibliotheek voor elke game uit de familie, en blijft hij kloppen als de pakker andere adressen
 * kiest dan TeknoParrot hardcodeerde.
 *
 * DE OMVANG VAN DE PATCH
 * ----------------------
 * 32-bits: `e9 <rel32>` (5 bytes). 64-bits: `48 b8 <abs64> ff e0` (12 bytes) — een absolute sprong,
 * zodat de patch niet van de afstand tussen de game en onze bibliotheek afhangt.
 *
 * De eerste PT_LOAD van de gepakte 32-bits games is RWX (gemeten: `RWE 0x1000`), dus het beeld is
 * al schrijfbaar; waar dat niet zo is doet de bibliotheek een `mprotect`.
 *
 * HET KANAAL
 * ----------
 * De bibliotheek moet in het ELF-proces van de game komen. Twee routes:
 *   1. `LD_AUDIT` van het Wine-proces — de route die werkt (`LD_PRELOAD` sterft aan Cygwin,
 *      `LD_AUDIT` niet);
 *   2. `LD_PRELOAD` van een eigen lader (de native route).
 *
 * De emulator (`ravengun-hasp*.so`) wordt in een EIGEN naamruimte geladen (`dlmopen`), zodat zijn
 * `hasp_*`-exporten niet in de globale naamruimte van het spel terechtkomen. Zonder dat zou het
 * spel de emulator via de gewone symboolopzoeking kunnen vinden in plaats van onze haak.
 *
 * Omgeving:
 *   RAVENGUN_HASP_GAME        de naam van de game (voor het logboek en de donglegegevens)
 *   RAVENGUN_HASP_DIR         de wortel van de donglegegevens (standaard /hasp)
 *   RAVENGUN_HASP_VERBOSE     niet leeg en niet "0": één regel per aangeroepen functie
 *   RAVENGUN_HASP_ELF_LOG     pad voor het logboek van deze bibliotheek
 *   RAVENGUN_HASP_ELF_GAME    pad naar de game-ELF (anders /proc/self/exe, de werkmap, cmdline)
 *   RAVENGUN_HASP_ELF_LIB     pad naar de emulator (anders de standaard installatiemap)
 *   RAVENGUN_HASP_ELF_WACHT   hoe lang op het spelbeeld wachten, in seconden (standaard 90)
 *   RAVENGUN_HASP_ELF_MS      poll-interval in milliseconden (standaard 5)
 *   RAVENGUN_HASP_ELF_UIT     "1": alleen meten, niets patchen
 *
 * Bouwen: scripts/build-hasphook.sh (gcc -m32 -shared -fPIC en 64-bits).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---------------------------------------------------------------- het logboek */

static FILE *hh_log;
static int hh_verbose;

static void hh_say(const char *fmt, ...)
{
    va_list ap;
    if (!hh_log)
        return;
    fprintf(hh_log, "[ravengun-hasphook] ");
    va_start(ap, fmt);
    vfprintf(hh_log, fmt, ap);
    va_end(ap);
    fputc('\n', hh_log);
    fflush(hh_log);
}

/* ---------------------------------------------------------------- het spelbeeld van schijf */

#if defined(__x86_64__)
typedef Elf64_Ehdr hh_ehdr;
typedef Elf64_Phdr hh_phdr;
typedef Elf64_Dyn hh_dyn;
typedef Elf64_Sym hh_sym;
#define HH_PATCH_LEN 12
#else
typedef Elf32_Ehdr hh_ehdr;
typedef Elf32_Phdr hh_phdr;
typedef Elf32_Dyn hh_dyn;
typedef Elf32_Sym hh_sym;
#define HH_PATCH_LEN 5
#endif

static unsigned char *hh_beeld;
static size_t hh_beeld_n;
static hh_ehdr *hh_eh;
static hh_phdr *hh_ph;
static hh_sym *hh_symtab;
static const char *hh_strtab;
static size_t hh_sym_n;

static int hh_lees_elf(const char *pad, const char *waarom)
{
    FILE *f = fopen(pad, "rb");
    long grootte;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    grootte = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (grootte <= 0 || grootte > 512L * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    hh_beeld = malloc((size_t)grootte);
    if (!hh_beeld) {
        fclose(f);
        return 0;
    }
    hh_beeld_n = fread(hh_beeld, 1, (size_t)grootte, f);
    fclose(f);
    if (hh_beeld_n < sizeof(hh_ehdr) || memcmp(hh_beeld, ELFMAG, 4) != 0 ||
        hh_beeld[EI_CLASS] != (unsigned char)(sizeof(void *) == 8 ? ELFCLASS64 : ELFCLASS32)) {
        free(hh_beeld);
        hh_beeld = NULL;
        return 0;
    }
    hh_eh = (hh_ehdr *)hh_beeld;
    hh_ph = (hh_phdr *)(hh_beeld + hh_eh->e_phoff);
    hh_say("spel-ELF gelezen uit %s (%s): %zu bytes, %d program headers", pad, waarom, hh_beeld_n,
           hh_eh->e_phnum);
    return 1;
}

/* De game-ELF staat op schijf. Het proces is wine-preloader (LD_AUDIT-route) of de game zelf
   (LD_PRELOAD-route). /proc/self/exe is dus vaak NIET de game; de commandoregel van het
   Wine-proces noemt het spelpad wel (`Z:\...\game`). We proberen ze in volgorde. */
static int hh_vind_elf(void)
{
    char pad[4096];
    char cwd[4096];
    ssize_t n;
    const char *env = getenv("RAVENGUN_HASP_ELF_GAME");

    if (env && *env && hh_lees_elf(env, "RAVENGUN_HASP_ELF_GAME"))
        return 1;

    /* De commandoregel: zoek het argument dat op de game wijst. */
    {
        FILE *f = fopen("/proc/self/cmdline", "rb");
        if (f) {
            char buf[8192];
            size_t m = fread(buf, 1, sizeof(buf) - 1, f);
            size_t k = 0;
            fclose(f);
            buf[m] = 0;
            while (k < m) {
                char *arg = buf + k;
                size_t len = strlen(arg);
                if (len > 5 && (strstr(arg, "\\game") || strstr(arg, "/game"))) {
                    char echt[4096];
                    size_t i, j = 0;
                    for (i = 0; arg[i] && j < sizeof(echt) - 1; i++)
                        echt[j++] = (arg[i] == '\\') ? '/' : arg[i];
                    echt[j] = 0;
                    if (echt[1] == ':' && (echt[0] == 'Z' || echt[0] == 'z')) {
                        char unix_pad[4096];
                        snprintf(unix_pad, sizeof(unix_pad), "%s", echt + 2);
                        if (hh_lees_elf(unix_pad, "cmdline Z:-pad"))
                            return 1;
                    } else if (hh_lees_elf(echt, "cmdline-pad")) {
                        return 1;
                    }
                }
                k += len + 1;
            }
        }
    }

    /* /proc/self/exe: alleen als het geen wine-preloader is. */
    n = readlink("/proc/self/exe", pad, sizeof(pad) - 1);
    if (n > 0) {
        pad[n] = 0;
        if (!strstr(pad, "wine-preloader") && !strstr(pad, "wine-preloader.so") &&
            hh_lees_elf(pad, "/proc/self/exe"))
            return 1;
    }

    /* De werkmap: ./game. */
    n = readlink("/proc/self/cwd", cwd, sizeof(cwd) - 1);
    if (n > 0) {
        cwd[n] = 0;
        snprintf(pad, sizeof(pad), "%.4000s/game", cwd);
        if (hh_lees_elf(pad, "werkmap/game"))
            return 1;
    }
    return 0;
}

/* virtueel adres -> bestands-offset via de PT_LOAD-segmenten */
static size_t hh_v2o(uint64_t vaddr)
{
    int i;
    for (i = 0; i < hh_eh->e_phnum; i++) {
        if (hh_ph[i].p_type != PT_LOAD)
            continue;
        if (vaddr >= hh_ph[i].p_vaddr && vaddr < hh_ph[i].p_vaddr + hh_ph[i].p_filesz)
            return (size_t)(vaddr - hh_ph[i].p_vaddr + hh_ph[i].p_offset);
    }
    return 0;
}

static int hh_vind_dynsym(void)
{
    hh_dyn *dyn = NULL;
    int i;
    size_t sym_off = 0, str_off = 0, hash_off = 0, gnu_hash_off = 0;
    for (i = 0; i < hh_eh->e_phnum; i++)
        if (hh_ph[i].p_type == PT_DYNAMIC)
            dyn = (hh_dyn *)(hh_beeld + hh_ph[i].p_offset);
    if (!dyn) {
        hh_say("geen PT_DYNAMIC in het spelbeeld");
        return 0;
    }
    for (i = 0; dyn[i].d_tag != DT_NULL; i++) {
        switch (dyn[i].d_tag) {
        case DT_SYMTAB: sym_off = hh_v2o(dyn[i].d_un.d_ptr); break;
        case DT_STRTAB: str_off = hh_v2o(dyn[i].d_un.d_ptr); break;
        case DT_HASH: hash_off = hh_v2o(dyn[i].d_un.d_ptr); break;
#ifdef DT_GNU_HASH
        case DT_GNU_HASH: gnu_hash_off = hh_v2o(dyn[i].d_un.d_ptr); break;
#endif
        default: break;
        }
    }
    if (!sym_off || !str_off) {
        hh_say("DT_SYMTAB of DT_STRTAB valt buiten de PT_LOAD-segmenten");
        return 0;
    }
    if (hash_off && hash_off + 8 <= hh_beeld_n) {
        hh_sym_n = ((const uint32_t *)(hh_beeld + hash_off))[1];
        hh_say("aantal symbolen uit DT_HASH: %zu", hh_sym_n);
    } else if (gnu_hash_off && gnu_hash_off + 16 <= hh_beeld_n) {
        /* DT_GNU_HASH: (nbuckets, symoffset, bloom_size, bloom_shift), dan de buckets.
           Het aantal symbolen is de hoogste bucketwaarde plus de lengte van die keten. */
        const uint32_t *g = (const uint32_t *)(hh_beeld + gnu_hash_off);
        uint32_t nbuckets = g[0], symoffset = g[1], bloom_size = g[2];
        /* De bloom-woorden zijn zo breed als een adres. */
        const uint32_t *buckets = g + 4 + (uint32_t)(bloom_size * (sizeof(void *) / 4));
        uint32_t hoogste = 0, k;
        size_t chain_off;
        for (k = 0; k < nbuckets; k++)
            if (buckets[k] > hoogste)
                hoogste = buckets[k];
        if (hoogste >= symoffset) {
            chain_off = (size_t)((const unsigned char *)(buckets + nbuckets) - hh_beeld);
            hh_sym_n = hoogste + 1;
            while (chain_off + (hh_sym_n - symoffset + 1) * 4 <= hh_beeld_n) {
                const uint32_t *chain = (const uint32_t *)(hh_beeld + chain_off);
                if (chain[hh_sym_n - symoffset] & 1u)
                    break;
                hh_sym_n++;
            }
        }
        hh_say("aantal symbolen uit DT_GNU_HASH: %zu (nbuckets %u, symoffset %u)", hh_sym_n,
               nbuckets, symoffset);
    }
    if (!hh_sym_n)
        hh_say("geen DT_HASH en geen DT_GNU_HASH: aantal symbolen onbekend");
    if (sym_off + hh_sym_n * sizeof(hh_sym) > hh_beeld_n) {
        hh_sym_n = (hh_beeld_n - sym_off) / sizeof(hh_sym);
        hh_say("symtab teruggebracht tot %zu symbolen", hh_sym_n);
    }
    hh_symtab = (hh_sym *)(hh_beeld + sym_off);
    hh_strtab = (const char *)(hh_beeld + str_off);
    hh_say("dynsym: %zu symbolen", hh_sym_n);
    return 1;
}

/* ---------------------------------------------------------------- het spelbeeld in het geheugen */

static uintptr_t hh_base;

/* Staat er een leesbare mapping van n bytes op a? */
static int hh_bereik_leesbaar(uintptr_t a, size_t n)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char regel[512];
    int ok = 0;
    if (!f)
        return 0;
    while (fgets(regel, sizeof(regel), f)) {
        unsigned long lo, hi;
        char rechten[8];
        if (sscanf(regel, "%lx-%lx %7s", &lo, &hi, rechten) != 3)
            continue;
        if (rechten[0] != 'r')
            continue;
        if (a >= lo && a + n <= hi) {
            ok = 1;
            break;
        }
    }
    fclose(f);
    return ok;
}

/*
 * Het spelbeeld zoeken. Drie gevallen:
 *   1. het hoofdprogramma is de game zelf (LD_PRELOAD-route): het laadadres komt van
 *      dl_iterate_phdr, dat we EEN KEER in de constructor opvragen (daar is het veilig: de
 *      laadvergrendeling is al van deze thread). In de kijker-thread zou die aanroep vastlopen;
 *   2. de game staat op zijn vaste adres (0x08048000 voor de 32-bits familie) en de ELF-magic
 *      staat er: dan is dat het beeld (de LD_AUDIT-route onder ElfLdr2);
 *   3. anders zoeken we in /proc/self/maps naar een mapping die met ELF begint.
 */
static uintptr_t hh_hoofd_base;
static int hh_hoofd_base_bekend;

static int hh_phdr_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    (void)data;
    if (info->dlpi_name == NULL || info->dlpi_name[0] == 0) {
        hh_hoofd_base = (uintptr_t)info->dlpi_addr;
        hh_hoofd_base_bekend = 1;
        return 1;
    }
    return 0;
}

/* Alleen in de constructor aanroepen. */
static void hh_vraag_hoofd_base(void)
{
    dl_iterate_phdr(hh_phdr_cb, NULL);
}

/*
 * Is dit het spelbeeld? We vergelijken de ELF-kop in het geheugen met die van het bestand op
 * schijf: klasse, gegevenscodering, machine, type, het ingangspunt en het aantal program headers.
 * Alleen naar de ELF-magic kijken is niet genoeg: in dit proces staan meer 32-bits ELF's (de
 * wine-preloader bijvoorbeeld), en die gaven eerder een valse treffer.
 */
static int hh_is_spelbeeld(uintptr_t a)
{
    const unsigned char *p;
    hh_ehdr *g;
    if (!hh_bereik_leesbaar(a, sizeof(hh_ehdr)))
        return 0;
    p = (const unsigned char *)a;
    if (p[0] != 0x7f || p[1] != 'E' || p[2] != 'L' || p[3] != 'F')
        return 0;
    g = (hh_ehdr *)a;
    if (g->e_ident[EI_CLASS] != hh_eh->e_ident[EI_CLASS])
        return 0;
    if (g->e_ident[EI_DATA] != hh_eh->e_ident[EI_DATA])
        return 0;
    if (g->e_machine != hh_eh->e_machine)
        return 0;
    if (g->e_type != hh_eh->e_type)
        return 0;
    if (g->e_entry != hh_eh->e_entry)
        return 0;
    if (g->e_phnum != hh_eh->e_phnum)
        return 0;
    if (g->e_phoff != hh_eh->e_phoff)
        return 0;
    return 1;
}

/*
 * Een lezing van het spelbeeld kan mislukken terwijl /proc/self/maps hem "leesbaar" noemt: ElfLdr2
 * beeldt het beeld in delen af, en een pagina die net is toegewezen maar nog niet gevuld is geeft
 * SIGBUS. Dat is geen fout van ons maar een race met de lader. Daarom vangen we SIGSEGV en SIGBUS
 * rond de lezing af en behandelen we een fout als "nog niet klaar".
 */
static sigjmp_buf hh_lezing;
static volatile sig_atomic_t hh_lezing_actief;

static void hh_lezing_fout(int sig)
{
    (void)sig;
    if (hh_lezing_actief)
        siglongjmp(hh_lezing, 1);
}

static int hh_is_spelbeeld_veilig(uintptr_t a)
{
    struct sigaction segv, bus, oud_segv, oud_bus;
    int r;
    memset(&segv, 0, sizeof(segv));
    segv.sa_handler = hh_lezing_fout;
    sigemptyset(&segv.sa_mask);
    segv.sa_flags = SA_NODEFER;
    bus = segv;
    sigaction(SIGSEGV, &segv, &oud_segv);
    sigaction(SIGBUS, &bus, &oud_bus);
    hh_lezing_actief = 1;
    if (sigsetjmp(hh_lezing, 1) == 0)
        r = hh_is_spelbeeld(a);
    else
        r = 0;
    hh_lezing_actief = 0;
    sigaction(SIGSEGV, &oud_segv, NULL);
    sigaction(SIGBUS, &oud_bus, NULL);
    return r;
}

static int hh_zoek_beeld(void)
{
    uint64_t laag = ~(uint64_t)0;
    FILE *f;
    char regel[512];
    int i;

    for (i = 0; i < hh_eh->e_phnum; i++)
        if (hh_ph[i].p_type == PT_LOAD && hh_ph[i].p_vaddr < laag)
            laag = hh_ph[i].p_vaddr;
    if (laag == ~(uint64_t)0)
        return 0;

    /* Geval 1: het hoofdprogramma is de game (LD_PRELOAD-route). */
    if (hh_hoofd_base_bekend && hh_is_spelbeeld_veilig(hh_hoofd_base + (uintptr_t)laag)) {
        hh_base = hh_hoofd_base + (uintptr_t)laag;
        return 1;
    }
    /* Geval 2: het beeld op zijn vaste adres (LD_AUDIT-route onder ElfLdr2). */
    if (hh_is_spelbeeld_veilig((uintptr_t)laag)) {
        hh_base = (uintptr_t)laag;
        return 1;
    }
    /* Geval 3: zoeken in de mappings. */
    f = fopen("/proc/self/maps", "r");
    if (!f)
        return 0;
    while (fgets(regel, sizeof(regel), f)) {
        unsigned long lo;
        char rechten[8];
        if (sscanf(regel, "%lx-%*x %7s", &lo, rechten) != 2)
            continue;
        if (rechten[0] != 'r')
            continue;
        if (hh_is_spelbeeld_veilig((uintptr_t)lo)) {
            hh_base = (uintptr_t)lo;
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/* ---------------------------------------------------------------- de inline-hook */

#define HH_MAX 32

struct hh_haak {
    char naam[40];
    uintptr_t adres;
    void *vervanger;
    void *origineel;
    unsigned char oud[16];
    size_t lengte;
    int gezet;
};

static struct hh_haak hh_haken[HH_MAX];
static int hh_haak_n;

static int hh_schrijfbaar(uintptr_t a, size_t n)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char regel[512];
    int ok = 0;
    if (!f)
        return 0;
    while (fgets(regel, sizeof(regel), f)) {
        unsigned long lo, hi;
        char rechten[8];
        if (sscanf(regel, "%lx-%lx %7s", &lo, &hi, rechten) != 3)
            continue;
        if (rechten[1] != 'w')
            continue;
        if (a >= lo && a + n <= hi) {
            ok = 1;
            break;
        }
    }
    fclose(f);
    return ok;
}

/*
 * De trampoline: de originele eerste bytes, gevolgd door een sprong terug naar `adres + lengte`.
 * Zo kan een vervanger de echte functie alsnog aanroepen (via hh_echt_*).
 */
static void *hh_maak_trampoline(struct hh_haak *h)
{
    size_t pag = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *p = mmap(NULL, pag, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
    memcpy(p, h->oud, h->lengte);
#if defined(__x86_64__)
    p[h->lengte] = 0xff;
    p[h->lengte + 1] = 0x25;
    {
        int32_t nul = 0;
        uint64_t doel = (uint64_t)(h->adres + h->lengte);
        memcpy(p + h->lengte + 2, &nul, 4);
        memcpy(p + h->lengte + 6, &doel, 8);
    }
#else
    p[h->lengte] = 0xe9;
    {
        int32_t rel = (int32_t)((intptr_t)(h->adres + h->lengte) - (intptr_t)(p + h->lengte + 5));
        memcpy(p + h->lengte + 1, &rel, 4);
    }
#endif
    return p;
}

static void hh_patch(struct hh_haak *h)
{
    unsigned char *p = (unsigned char *)h->adres;
    size_t i;
    if (h->gezet)
        return;
    if (!hh_schrijfbaar(h->adres, h->lengte)) {
        size_t pag = (size_t)sysconf(_SC_PAGESIZE);
        uintptr_t lo = h->adres & ~(pag - 1);
        uintptr_t hi = (h->adres + h->lengte + pag - 1) & ~(pag - 1);
        if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            hh_say("  %s: mprotect mislukt; niet aangehaakt", h->naam);
            return;
        }
    }
    for (i = 0; i < h->lengte; i++)
        h->oud[i] = p[i];
    h->origineel = hh_maak_trampoline(h);
    if (!h->origineel) {
        hh_say("  %s: geen uitvoerbare pagina voor de trampoline", h->naam);
        return;
    }
    {
        unsigned char nieuw[16];
        size_t n;
#if defined(__x86_64__)
        uint64_t doel = (uint64_t)(uintptr_t)h->vervanger;
        nieuw[0] = 0x48;
        nieuw[1] = 0xb8;
        memcpy(nieuw + 2, &doel, 8);
        nieuw[10] = 0xff;
        nieuw[11] = 0xe0;
        n = 12;
#else
        int32_t rel = (int32_t)((intptr_t)h->vervanger - (intptr_t)(h->adres + 5));
        nieuw[0] = 0xe9;
        memcpy(nieuw + 1, &rel, 4);
        n = 5;
#endif
        memcpy(p, nieuw, n);
        __builtin___clear_cache((char *)p, (char *)p + n);
    }
    h->gezet = 1;
    hh_say("  aangehaakt: %-24s op 0x%08lx -> %p (trampoline %p, %zu bytes)", h->naam,
           (unsigned long)h->adres, h->vervanger, h->origineel, h->lengte);
}

/* ---------------------------------------------------------------- de emulator in een eigen naamruimte */

static void *hh_emulator;

static void *hh_open_geisoleerd(const char *pad)
{
    /*
     * De emulator het liefst in een EIGEN naamruimte (dlmopen), zodat zijn hasp_*-exporten niet in
     * de globale naamruimte van het spel komen. Onder de Wine-preloader is dlmopen echter niet
     * betrouwbaar gebleken (het proces sterft), dus daar gebruiken we dlopen met RTLD_LOCAL.
     * RTLD_LOCAL is hier voldoende: de game vindt zijn eigen, statisch gelinkte hasp-code via zijn
     * eigen adressen (geen PLT, geen GOT), dus een export in de globale naamruimte is voor het spel
     * niet nodig — en RTLD_LOCAL houdt hem er juist buiten.
     */
    {
        const char *wil_dlmopen = getenv("RAVENGUN_HASP_ELF_DLMOPEN");
        if (wil_dlmopen && *wil_dlmopen && strcmp(wil_dlmopen, "0") != 0) {
#ifdef LM_ID_NEWLM
            void *h = dlmopen(LM_ID_NEWLM, pad, RTLD_NOW | RTLD_LOCAL);
            if (h) {
                hh_say("emulator in een EIGEN naamruimte geladen (dlmopen): %s", pad);
                return h;
            }
            hh_say("dlmopen mislukt (%s); terug naar dlopen", dlerror());
#endif
        }
    }
    {
        void *h = dlopen(pad, RTLD_NOW | RTLD_LOCAL);
        if (h)
            hh_say("emulator met RTLD_LOCAL geladen (dlopen): %s", pad);
        return h;
    }
}

static void *hh_emul_sym(const char *naam)
{
    void *p;
    if (!hh_emulator)
        return NULL;
    p = dlsym(hh_emulator, naam);
    if (!p)
        hh_say("  emulator mist %s", naam);
    return p;
}

static int hh_laad_emulator(void)
{
    const char *paden[3];
    const char *lib = getenv("RAVENGUN_HASP_ELF_LIB");
    int i;
    paden[0] = (lib && *lib) ? lib : NULL;
#if defined(__x86_64__)
    paden[1] = "/home/deck/.local/share/ravengun/hasp/ravengun-hasp-x64.so";
    paden[2] = "ravengun-hasp-x64.so";
#else
    paden[1] = "/home/deck/.local/share/ravengun/hasp/ravengun-hasp.so";
    paden[2] = "ravengun-hasp.so";
#endif
    for (i = 0; i < 3; i++) {
        if (!paden[i] || !*paden[i])
            continue;
        hh_emulator = hh_open_geisoleerd(paden[i]);
        if (hh_emulator)
            return 1;
    }
    hh_say("GEEN emulator kunnen laden");
    return 0;
}

/* ---------------------------------------------------------------- de vervangers */

static int (*hh_echt_hasp_login)(unsigned, const char *, void **);
static int (*hh_echt_hasp_login_scope)(unsigned, const char *, const char *, void **);
static int (*hh_echt_hasp_logout)(void *);
static int (*hh_echt_hasp_read)(void *, unsigned, unsigned, unsigned, void *);
static int (*hh_echt_hasp_write)(void *, unsigned, unsigned, unsigned, const void *);
static int (*hh_echt_hasp_encrypt)(void *, void *, unsigned);
static int (*hh_echt_hasp_decrypt)(void *, void *, unsigned);
static int (*hh_echt_hasp_get_size)(void *, unsigned, unsigned *);
static int (*hh_echt_hasp_get_info)(void *, const char *, const char *, char **);
static int (*hh_echt_hasp_get_sessioninfo)(void *, const char *, char **);
static int (*hh_echt_hasp_free)(void *);

static int (*hh_emul_hasp_login)(unsigned, const char *, void **);
static int (*hh_emul_hasp_login_scope)(unsigned, const char *, const char *, void **);
static int (*hh_emul_hasp_logout)(void *);
static int (*hh_emul_hasp_read)(void *, unsigned, unsigned, unsigned, void *);
static int (*hh_emul_hasp_write)(void *, unsigned, unsigned, unsigned, const void *);
static int (*hh_emul_hasp_encrypt)(void *, void *, unsigned);
static int (*hh_emul_hasp_decrypt)(void *, void *, unsigned);
static int (*hh_emul_hasp_get_size)(void *, unsigned, unsigned *);
static int (*hh_emul_hasp_get_info)(void *, const char *, const char *, char **);
static int (*hh_emul_hasp_get_sessioninfo)(void *, const char *, char **);
static int (*hh_emul_hasp_free)(void *);

static void hh_meld(const char *naam)
{
    if (hh_verbose)
        hh_say("aanroep: %s", naam);
}

static int hh_vervanger_hasp_login(unsigned feature, const char *scope, void **handle)
{
    hh_meld("hasp_login");
    if (hh_emul_hasp_login)
        return hh_emul_hasp_login(feature, scope, handle);
    if (hh_echt_hasp_login)
        return hh_echt_hasp_login(feature, scope, handle);
    return 0;
}

static int hh_vervanger_hasp_login_scope(unsigned feature, const char *scope, const char *format,
                                         void **handle)
{
    hh_meld("hasp_login_scope");
    if (hh_emul_hasp_login_scope)
        return hh_emul_hasp_login_scope(feature, scope, format, handle);
    if (hh_echt_hasp_login_scope)
        return hh_echt_hasp_login_scope(feature, scope, format, handle);
    return 0;
}

static int hh_vervanger_hasp_logout(void *handle)
{
    hh_meld("hasp_logout");
    if (hh_emul_hasp_logout)
        return hh_emul_hasp_logout(handle);
    if (hh_echt_hasp_logout)
        return hh_echt_hasp_logout(handle);
    return 0;
}

static int hh_vervanger_hasp_read(void *handle, unsigned fileid, unsigned offset, unsigned length,
                                  void *buffer)
{
    hh_meld("hasp_read");
    if (hh_emul_hasp_read)
        return hh_emul_hasp_read(handle, fileid, offset, length, buffer);
    if (hh_echt_hasp_read)
        return hh_echt_hasp_read(handle, fileid, offset, length, buffer);
    return 0;
}

static int hh_vervanger_hasp_write(void *handle, unsigned fileid, unsigned offset, unsigned length,
                                   const void *buffer)
{
    hh_meld("hasp_write");
    if (hh_emul_hasp_write)
        return hh_emul_hasp_write(handle, fileid, offset, length, buffer);
    if (hh_echt_hasp_write)
        return hh_echt_hasp_write(handle, fileid, offset, length, buffer);
    return 0;
}

static int hh_vervanger_hasp_encrypt(void *handle, void *buffer, unsigned length)
{
    hh_meld("hasp_encrypt");
    if (hh_emul_hasp_encrypt)
        return hh_emul_hasp_encrypt(handle, buffer, length);
    if (hh_echt_hasp_encrypt)
        return hh_echt_hasp_encrypt(handle, buffer, length);
    return 0;
}

static int hh_vervanger_hasp_decrypt(void *handle, void *buffer, unsigned length)
{
    hh_meld("hasp_decrypt");
    if (hh_emul_hasp_decrypt)
        return hh_emul_hasp_decrypt(handle, buffer, length);
    if (hh_echt_hasp_decrypt)
        return hh_echt_hasp_decrypt(handle, buffer, length);
    return 0;
}

static int hh_vervanger_hasp_get_size(void *handle, unsigned fileid, unsigned *size)
{
    hh_meld("hasp_get_size");
    if (hh_emul_hasp_get_size)
        return hh_emul_hasp_get_size(handle, fileid, size);
    if (hh_echt_hasp_get_size)
        return hh_echt_hasp_get_size(handle, fileid, size);
    return 0;
}

static int hh_vervanger_hasp_get_info(void *handle, const char *format, const char *scope,
                                      char **info)
{
    hh_meld("hasp_get_info");
    if (hh_emul_hasp_get_info)
        return hh_emul_hasp_get_info(handle, format, scope, info);
    if (hh_echt_hasp_get_info)
        return hh_echt_hasp_get_info(handle, format, scope, info);
    return 0;
}

static int hh_vervanger_hasp_get_sessioninfo(void *handle, const char *format, char **info)
{
    hh_meld("hasp_get_sessioninfo");
    if (hh_emul_hasp_get_sessioninfo)
        return hh_emul_hasp_get_sessioninfo(handle, format, info);
    if (hh_echt_hasp_get_sessioninfo)
        return hh_echt_hasp_get_sessioninfo(handle, format, info);
    return 0;
}

static int hh_vervanger_hasp_free(void *p)
{
    hh_meld("hasp_free");
    if (hh_emul_hasp_free)
        return hh_emul_hasp_free(p);
    if (hh_echt_hasp_free)
        return hh_echt_hasp_free(p);
    return 0;
}

struct hh_doel {
    const char *naam;
    void *vervanger;
};

static const struct hh_doel hh_doelen[] = {
    {"hasp_login", (void *)hh_vervanger_hasp_login},
    {"hasp_login_scope", (void *)hh_vervanger_hasp_login_scope},
    {"hasp_logout", (void *)hh_vervanger_hasp_logout},
    {"hasp_read", (void *)hh_vervanger_hasp_read},
    {"hasp_write", (void *)hh_vervanger_hasp_write},
    {"hasp_encrypt", (void *)hh_vervanger_hasp_encrypt},
    {"hasp_decrypt", (void *)hh_vervanger_hasp_decrypt},
    {"hasp_get_size", (void *)hh_vervanger_hasp_get_size},
    {"hasp_get_info", (void *)hh_vervanger_hasp_get_info},
    {"hasp_get_sessioninfo", (void *)hh_vervanger_hasp_get_sessioninfo},
    {"hasp_free", (void *)hh_vervanger_hasp_free},
};

/* De naam van een symbool uit de dynsym van de game. */
static const char *hh_sym_naam(size_t i)
{
    size_t str_off = (size_t)(hh_strtab - (const char *)hh_beeld);
    size_t no = hh_symtab[i].st_name;
    if (!hh_strtab || no == 0 || str_off + no >= hh_beeld_n)
        return NULL;
    if (memchr(hh_strtab + no, 0, hh_beeld_n - str_off - no) == NULL)
        return NULL;
    return hh_strtab + no;
}

static void hh_koppel_emulator(void)
{
    hh_emul_hasp_login = (int (*)(unsigned, const char *, void **))hh_emul_sym("hasp_login");
    hh_emul_hasp_login_scope =
        (int (*)(unsigned, const char *, const char *, void **))hh_emul_sym("hasp_login_scope");
    hh_emul_hasp_logout = (int (*)(void *))hh_emul_sym("hasp_logout");
    hh_emul_hasp_read =
        (int (*)(void *, unsigned, unsigned, unsigned, void *))hh_emul_sym("hasp_read");
    hh_emul_hasp_write =
        (int (*)(void *, unsigned, unsigned, unsigned, const void *))hh_emul_sym("hasp_write");
    hh_emul_hasp_encrypt = (int (*)(void *, void *, unsigned))hh_emul_sym("hasp_encrypt");
    hh_emul_hasp_decrypt = (int (*)(void *, void *, unsigned))hh_emul_sym("hasp_decrypt");
    hh_emul_hasp_get_size = (int (*)(void *, unsigned, unsigned *))hh_emul_sym("hasp_get_size");
    hh_emul_hasp_get_info =
        (int (*)(void *, const char *, const char *, char **))hh_emul_sym("hasp_get_info");
    hh_emul_hasp_get_sessioninfo =
        (int (*)(void *, const char *, char **))hh_emul_sym("hasp_get_sessioninfo");
    hh_emul_hasp_free = (int (*)(void *))hh_emul_sym("hasp_free");
}

static void hh_koppel_echt(const char *naam, void *trampoline)
{
    if (!strcmp(naam, "hasp_login"))
        hh_echt_hasp_login = (int (*)(unsigned, const char *, void **))trampoline;
    else if (!strcmp(naam, "hasp_login_scope"))
        hh_echt_hasp_login_scope =
            (int (*)(unsigned, const char *, const char *, void **))trampoline;
    else if (!strcmp(naam, "hasp_logout"))
        hh_echt_hasp_logout = (int (*)(void *))trampoline;
    else if (!strcmp(naam, "hasp_read"))
        hh_echt_hasp_read =
            (int (*)(void *, unsigned, unsigned, unsigned, void *))trampoline;
    else if (!strcmp(naam, "hasp_write"))
        hh_echt_hasp_write =
            (int (*)(void *, unsigned, unsigned, unsigned, const void *))trampoline;
    else if (!strcmp(naam, "hasp_encrypt"))
        hh_echt_hasp_encrypt = (int (*)(void *, void *, unsigned))trampoline;
    else if (!strcmp(naam, "hasp_decrypt"))
        hh_echt_hasp_decrypt = (int (*)(void *, void *, unsigned))trampoline;
    else if (!strcmp(naam, "hasp_get_size"))
        hh_echt_hasp_get_size = (int (*)(void *, unsigned, unsigned *))trampoline;
    else if (!strcmp(naam, "hasp_get_info"))
        hh_echt_hasp_get_info =
            (int (*)(void *, const char *, const char *, char **))trampoline;
    else if (!strcmp(naam, "hasp_get_sessioninfo"))
        hh_echt_hasp_get_sessioninfo = (int (*)(void *, const char *, char **))trampoline;
    else if (!strcmp(naam, "hasp_free"))
        hh_echt_hasp_free = (int (*)(void *))trampoline;
}

/* Welke hasp-symbolen staan er in de game, en op welk adres? */
static int hh_verzamel(void)
{
    size_t i;
    int n = 0;
    for (i = 0; i < hh_sym_n; i++) {
        const char *naam = hh_sym_naam(i);
        if (!naam || strncmp(naam, "hasp_", 5) != 0)
            continue;
        if (hh_symtab[i].st_shndx == SHN_UNDEF || hh_symtab[i].st_value == 0)
            continue;
        n++;
        if (hh_verbose)
            hh_say("  in de game: %-30s 0x%08llx (%llu bytes)", naam,
                   (unsigned long long)hh_symtab[i].st_value,
                   (unsigned long long)hh_symtab[i].st_size);
    }
    hh_say("%d hasp-symbolen in de dynsym van de game (gedefinieerd in de game zelf)", n);
    return n;
}

/* Is dit adres al aangehaakt? Dan niet nog eens patchen (de tweede keer zou de trampoline naar de
   eerste patch wijzen en zo een lus maken). */
static struct hh_haak *hh_al_gehaakt(uintptr_t adres)
{
    int i;
    for (i = 0; i < hh_haak_n; i++)
        if (hh_haken[i].adres == adres)
            return &hh_haken[i];
    return NULL;
}

static void hh_haak_alles(void)
{
    size_t i, d;
    for (d = 0; d < sizeof(hh_doelen) / sizeof(hh_doelen[0]); d++) {
        for (i = 0; i < hh_sym_n; i++) {
            const char *naam = hh_sym_naam(i);
            struct hh_haak *h;
            uintptr_t adres;
            if (!naam || strcmp(naam, hh_doelen[d].naam) != 0)
                continue;
            if (hh_symtab[i].st_shndx == SHN_UNDEF || hh_symtab[i].st_value == 0)
                break;
            adres = hh_base + (uintptr_t)hh_symtab[i].st_value;
            if (hh_al_gehaakt(adres))
                break;
            if (hh_symtab[i].st_size != 0 && (size_t)hh_symtab[i].st_size < HH_PATCH_LEN) {
                hh_say("  %s is maar %llu bytes; overgeslagen (patch is %d bytes)", naam,
                       (unsigned long long)hh_symtab[i].st_size, HH_PATCH_LEN);
                break;
            }
            if (hh_haak_n >= HH_MAX) {
                hh_say("  geen ruimte meer in de hakenlijst");
                break;
            }
            h = &hh_haken[hh_haak_n];
            snprintf(h->naam, sizeof(h->naam), "%s", naam);
            h->adres = adres;
            h->vervanger = hh_doelen[d].vervanger;
            h->lengte = HH_PATCH_LEN;
            if (!hh_bereik_leesbaar(h->adres, h->lengte)) {
                hh_say("  %s op 0x%08lx is (nog) niet leesbaar; volgende ronde", naam,
                       (unsigned long)h->adres);
                break;
            }
            hh_haak_n++;
            hh_patch(h);
            if (h->gezet)
                hh_koppel_echt(naam, h->origineel);
            else
                hh_haak_n--;   /* niet aangehaakt: de lijst niet vervuilen */
            break;
        }
    }
}

/* ---------------------------------------------------------------- de kijker */

static int hh_gestart;
static int hh_gedaan;

static void *hh_kijker(void *arg)
{
    long ms = 5, secs = 90;
    const char *v;
    long rondes, r;
    int gemeld = 0, geprobeerd = 0;
    (void)arg;

    if ((v = getenv("RAVENGUN_HASP_ELF_MS")) && *v)
        ms = strtol(v, NULL, 0);
    if ((v = getenv("RAVENGUN_HASP_ELF_WACHT")) && *v)
        secs = strtol(v, NULL, 0);
    if (ms < 1)
        ms = 1;
    if (secs < 1)
        secs = 1;
    rondes = (secs * 1000) / ms;

    for (r = 0; r < rondes && !hh_gedaan; r++) {
        if (!hh_zoek_beeld()) {
            usleep((useconds_t)(ms * 1000));
            continue;
        }
        if (!gemeld) {
            gemeld = 1;
            hh_say("spelbeeld op 0x%08lx", (unsigned long)hh_base);
            if (!hh_laad_emulator()) {
                hh_say("zonder emulator niets aangehaakt");
                hh_gedaan = 1;
                return NULL;
            }
            hh_koppel_emulator();
        }
        hh_haak_alles();
        if (hh_haak_n >= (int)(sizeof(hh_doelen) / sizeof(hh_doelen[0]))) {
            hh_gedaan = 1;
            hh_say("klaar: %d functies aangehaakt", hh_haak_n);
            break;
        }
        /* De functies kunnen pas later leesbaar worden (het beeld wordt in delen gemapt).
           Na een tijdje stoppen we met proberen en melden wat er wel is gelukt. */
        if (hh_haak_n > 0 && ++geprobeerd > (int)(2000 / ms) + 1) {
            hh_gedaan = 1;
            hh_say("gestopt met zoeken: %d functies aangehaakt", hh_haak_n);
            break;
        }
        usleep((useconds_t)(ms * 1000));
    }
    if (!hh_gedaan) {
        hh_say("het spelbeeld is niet gevonden binnen %ld s (of geen hasp-symbolen)", secs);
        hh_gedaan = 1;
    }
    return NULL;
}

static void hh_open_log(void)
{
    const char *p = getenv("RAVENGUN_HASP_ELF_LOG");
    const char *v = getenv("RAVENGUN_HASP_VERBOSE");
    const char *g = getenv("RAVENGUN_HASP_GAME");
    char comm[64] = "?";
    FILE *f;
    hh_verbose = v && *v && strcmp(v, "0") != 0;
    if (p && *p)
        hh_log = fopen(p, "a");
    if (!hh_log)
        hh_log = stderr;
    f = fopen("/proc/self/comm", "r");
    if (f) {
        if (fgets(comm, sizeof(comm), f)) {
            size_t n = strlen(comm);
            if (n && comm[n - 1] == '\n')
                comm[n - 1] = 0;
        }
        fclose(f);
    }
    if (g && *g)
        hh_say("=== ravengun-hasphook voor '%s' (pid %d, %s) ===", g, (int)getpid(), comm);
    else
        hh_say("=== ravengun-hasphook (pid %d, %s) ===", (int)getpid(), comm);
}

/*
 * De hele aanbrenging in één keer, SYNCHROON. Dit is de hoofdroute: het spelbeeld staat al in het
 * geheugen voordat de constructor van deze bibliotheek loopt (gemeten: 0x08048000-0x09128000 rwxp
 * vanaf het moment dat de lader het beeld heeft geplaatst), en een synchrone aanbrenging is
 * betrouwbaarder dan een kijker-thread: geen race met het opstarten van het spel.
 *
 * Lukt het niet (beeld nog niet afgebeeld, of een functie nog niet leesbaar), dan neemt de
 * kijker-thread het over.
 */
static int hh_breng_aan(void)
{
    int n;
    if (!hh_zoek_beeld())
        return 0;
    if (!hh_emulator && !hh_laad_emulator())
        return 0;
    hh_koppel_emulator();
    hh_haak_alles();
    n = hh_haak_n;
    if (n >= (int)(sizeof(hh_doelen) / sizeof(hh_doelen[0]))) {
        hh_say("klaar (synchroon): %d functies aangehaakt", n);
        return 1;
    }
    hh_say("synchroon %d van %d functies aangehaakt; de kijker probeert de rest",
           n, (int)(sizeof(hh_doelen) / sizeof(hh_doelen[0])));
    return n > 0;
}

static void hh_begin(void)
{
    pthread_t t;
    if (hh_gestart)
        return;
    hh_gestart = 1;
    hh_open_log();
    hh_vraag_hoofd_base();
    if (!hh_vind_elf()) {
        hh_say("geen spel-ELF gevonden; niets gedaan");
        return;
    }
    if (!hh_vind_dynsym())
        return;
    hh_verzamel();
    if (getenv("RAVENGUN_HASP_ELF_UIT")) {
        hh_say("RAVENGUN_HASP_ELF_UIT gezet: alleen gemeten, niets aangehaakt");
        return;
    }
    if (hh_breng_aan()) {
        hh_gedaan = 1;
        return;
    }
    hh_say("synchroon niet gelukt; de kijker-thread neemt het over");
    if (pthread_create(&t, NULL, hh_kijker, NULL) != 0) {
        hh_say("kon de kijker-thread niet starten; niets gedaan");
        hh_gestart = 0;
    }
}

__attribute__((constructor))
static void hh_init(void)
{
    hh_begin();
}

/* LD_AUDIT: glibc roept deze twee aan. la_version moet bestaan, anders laadt glibc de module niet
   als audit-module (het kanaal dat onder ElfLdr2 werkt). */
unsigned int la_version(unsigned int v)
{
    return v;
}

void la_preinit(uintptr_t *cookie)
{
    (void)cookie;
    hh_begin();
}
