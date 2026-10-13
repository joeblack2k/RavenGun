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
#include <errno.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
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
/* Het laagste PT_LOAD-adres van het beeld. Nodig om het laadadres (de bias) te kunnen
   uitrekenen: bias = hh_base - hh_laag. Voor een ET_EXEC is de bias 0, voor een PIE is
   hh_laag 0. Zonder dit onderscheid telt `hh_base + st_value` bij een ET_EXEC het
   segmentadres dubbel. */
static uint64_t hh_laag;

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
    hh_laag = laag;

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

/* ---------------------------------------------------------------- de adreslijst */

/*
 * De naam-route hierboven werkt alleen als de game zijn hasp_*-functies nog als dynsym-symbool
 * heeft. Bij de Raw Thrills-games is dat niet zo: de HASP-cliënt zit statisch in het beeld en de
 * namen zijn weg (gemeten: Halo's `game` heeft 220 dynsym-symbolen, nul daarvan met `hasp_`;
 * Jurassic Park's `game` ook nul). Voor die games is er een tweede weg: een adreslijst PER GAME,
 * in de patches-repo, die de ingangspunten noemt. De naam-route blijft de standaard en blijft
 * werken; deze weg komt er alleen bij als het bestand bestaat.
 *
 * Het bestand wordt gevonden via RAVENGUN_HASP_ELF_ADRESLIJST, of anders als `hasp-haken.ini`
 * naast deze bibliotheek. Zonder dat bestand verandert er niets.
 *
 * Formaat (ini-achtig, geen JSON-parser nodig):
 *     # commentaar
 *     game   = Halo              ; administratie, wordt alleen gelogd
 *     arch   = x86_64
 *     basis  = absoluut          ; absoluut (standaard) of beeld
 *     hasp_login = 0xb2d630
 *
 * Er wordt NOOIT blind gepatcht. Elk adres moet in een PT_LOAD-segment vallen dat uitvoerbaar is
 * (PF_X) en binnen p_filesz, en de bytes op dat adres moeten een herkenbare functieprologue zijn.
 * Wijkt er iets af, dan meldt de hook dat luid en slaat het adres over. Dat vangt ook een adres af
 * dat al door een andere hook is gepatcht (bijvoorbeeld halo.so van TeknoParrot): dan staan er
 * geen prologue-bytes meer, en een tweede patch zou een ketting van twee vervangingen geven.
 */

#define HH_ADRES_MAX 32
#define HH_REDEN_MAX 220

struct hh_adres {
    char naam[40];
    uint64_t adres;
    int geldig;
    char reden[HH_REDEN_MAX];
};

static struct hh_adres hh_adreslijst[HH_ADRES_MAX];
static int hh_adres_n;
static int hh_adres_basis_beeld;   /* 0 = absoluut, 1 = ten opzichte van het laadadres */
static char hh_adres_pad[4096];
static uint64_t hh_adres_verwacht_grootte;   /* 0 = niet gecontroleerd */
static uint64_t hh_adres_verwacht_ingang;    /* 0 = niet gecontroleerd */

/* Een herkenbare functieprologue? Conservatief: liever afwijzen dan blind patchen. */
static int hh_proloog_ok(const unsigned char *p, size_t n)
{
    if (n < 4)
        return 0;
#if defined(__x86_64__)
    if (p[0] == 0x55 || p[0] == 0x53 || p[0] == 0x56 || p[0] == 0x57)
        return 1;                                            /* push rbp/rbx/rsi/rdi */
    if (p[0] == 0x41 && (p[1] == 0x54 || p[1] == 0x55 || p[1] == 0x56 || p[1] == 0x57))
        return 1;                                            /* push r12..r15 */
    if (p[0] == 0xf3 && p[1] == 0x0f && p[2] == 0x1e && p[3] == 0xfa)
        return 1;                                            /* endbr64 */
    if (p[0] == 0x48 && p[1] == 0x83 && p[2] == 0xec)
        return 1;                                            /* sub rsp,imm8 */
    if (p[0] == 0x48 && p[1] == 0x81 && p[2] == 0xec)
        return 1;                                            /* sub rsp,imm32 */
    if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x5c && p[3] == 0x24)
        return 1;                                            /* mov [rsp+x],rbx */
#else
    if (p[0] == 0x55 || p[0] == 0x53 || p[0] == 0x56 || p[0] == 0x57)
        return 1;                                            /* push ebp/ebx/esi/edi */
    if (p[0] == 0x83 && p[1] == 0xec)
        return 1;                                            /* sub esp,imm8 */
    if (p[0] == 0x81 && p[1] == 0xec)
        return 1;                                            /* sub esp,imm32 */
    if (p[0] == 0x8d && p[1] == 0x4c && p[2] == 0x24)
        return 1;                                            /* lea ecx,[esp+x] */
    if (p[0] == 0x8b && p[1] == 0xff)
        return 1;                                            /* mov edi,edi (hotpatch) */
#endif
    return 0;
}

/* Leest de adreslijst. Geeft het aantal hasp_-regels terug (0 = niets bruikbaars). */
static int hh_lees_adreslijst(const char *pad)
{
    FILE *f = fopen(pad, "r");
    char regel[512];
    if (!f)
        return 0;
    snprintf(hh_adres_pad, sizeof hh_adres_pad, "%s", pad);
    hh_adres_n = 0;
    hh_adres_basis_beeld = 0;
    hh_adres_verwacht_grootte = 0;
    hh_adres_verwacht_ingang = 0;
    while (fgets(regel, sizeof regel, f)) {
        char *p = regel, *naam, *waarde, *e;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == 0)
            continue;
        naam = p;
        e = strpbrk(p, "=:\t \n");
        if (!e)
            continue;
        *e = 0;
        waarde = e + 1;
        while (*waarde == ' ' || *waarde == '\t' || *waarde == '=' || *waarde == ':')
            waarde++;
        e = waarde + strlen(waarde);
        while (e > waarde && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
        if (!*naam || !*waarde)
            continue;
        if (!strcmp(naam, "basis")) {
            hh_adres_basis_beeld = (strcmp(waarde, "beeld") == 0);
            continue;
        }
        if (!strcmp(naam, "grootte")) {
            hh_adres_verwacht_grootte = strtoull(waarde, NULL, 0);
            continue;
        }
        if (!strcmp(naam, "ingang")) {
            hh_adres_verwacht_ingang = strtoull(waarde, NULL, 0);
            continue;
        }
        if (strncmp(naam, "hasp_", 5) != 0)
            continue;                     /* game, arch, grootte, ingang: administratie */
        if (hh_adres_n >= HH_ADRES_MAX) {
            hh_say("adreslijst: meer dan %d regels; de rest wordt genegeerd", HH_ADRES_MAX);
            break;
        }
        snprintf(hh_adreslijst[hh_adres_n].naam, sizeof hh_adreslijst[hh_adres_n].naam, "%s", naam);
        hh_adreslijst[hh_adres_n].adres = strtoull(waarde, NULL, 0);
        hh_adreslijst[hh_adres_n].geldig = 0;
        snprintf(hh_adreslijst[hh_adres_n].reden, HH_REDEN_MAX, "nog niet gekeurd");
        hh_adres_n++;
    }
    fclose(f);
    return hh_adres_n;
}

/* In welk PT_LOAD-segment valt dit adres, en is dat uitvoerbaar? */
static int hh_adres_in_segment(uint64_t adres, int *uitvoerbaar, uint64_t *seg)
{
    int i;
    for (i = 0; i < hh_eh->e_phnum; i++) {
        if (hh_ph[i].p_type != PT_LOAD)
            continue;
        if (adres >= hh_ph[i].p_vaddr && adres < hh_ph[i].p_vaddr + hh_ph[i].p_memsz) {
            *uitvoerbaar = (hh_ph[i].p_flags & PF_X) ? 1 : 0;
            *seg = hh_ph[i].p_vaddr;
            return 1;
        }
    }
    return 0;
}

/* Keurt elk adres en logt per regel wat het oordeel is. Patcht zelf niets. */
static void hh_keur_adreslijst(void)
{
    int i;
    if (!hh_adres_n) {
        hh_say("adreslijst: geen hasp_-regels in %s", hh_adres_pad);
        return;
    }
    hh_say("adreslijst: %d regel(s) uit %s (basis: %s)", hh_adres_n, hh_adres_pad,
           hh_adres_basis_beeld ? "beeld" : "absoluut");
    /* Eerst de twee goedkope versiecontroles. Klopt een daarvan niet, dan is dit de verkeerde
       build van het spel en keuren we de HELE lijst af: een adres uit een andere versie is een
       crash die niemand terugvindt. */
    if (hh_adres_verwacht_grootte && hh_adres_verwacht_grootte != (uint64_t)hh_beeld_n) {
        hh_say("adreslijst AFGEKEURD: grootte %llu in de lijst, maar het beeld is %zu bytes",
               (unsigned long long)hh_adres_verwacht_grootte, hh_beeld_n);
        for (i = 0; i < hh_adres_n; i++)
            snprintf(hh_adreslijst[i].reden, HH_REDEN_MAX,
                     "AFGEWEZEN: verkeerde build (grootte %llu != %zu)",
                     (unsigned long long)hh_adres_verwacht_grootte, hh_beeld_n);
        return;
    }
    if (hh_adres_verwacht_ingang && hh_adres_verwacht_ingang != (uint64_t)hh_eh->e_entry) {
        hh_say("adreslijst AFGEKEURD: ingang 0x%llx in de lijst, maar het beeld heeft 0x%llx",
               (unsigned long long)hh_adres_verwacht_ingang,
               (unsigned long long)hh_eh->e_entry);
        for (i = 0; i < hh_adres_n; i++)
            snprintf(hh_adreslijst[i].reden, HH_REDEN_MAX,
                     "AFGEWEZEN: verkeerde build (ingang 0x%llx != 0x%llx)",
                     (unsigned long long)hh_adres_verwacht_ingang,
                     (unsigned long long)hh_eh->e_entry);
        return;
    }
    for (i = 0; i < hh_adres_n; i++) {
        struct hh_adres *a = &hh_adreslijst[i];
        unsigned char bytes[16];
        uint64_t seg = 0, off;
        int x = 0;
        if (a->adres == 0) {
            snprintf(a->reden, HH_REDEN_MAX, "AFGEWEZEN: adres is 0");
            continue;
        }
        if (!hh_adres_in_segment(a->adres, &x, &seg)) {
            snprintf(a->reden, HH_REDEN_MAX, "AFGEWEZEN: valt in geen PT_LOAD-segment van het beeld");
            continue;
        }
        if (!x) {
            snprintf(a->reden, HH_REDEN_MAX,
                     "AFGEWEZEN: segment 0x%llx is niet uitvoerbaar (PF_X ontbreekt)",
                     (unsigned long long)seg);
            continue;
        }
        off = hh_v2o(a->adres);
        if (off == 0) {
            snprintf(a->reden, HH_REDEN_MAX,
                     "AFGEWEZEN: wijst naar het begin van het bestand (ELF-kop), geen functie");
            continue;
        }
        if (off + HH_PATCH_LEN > hh_beeld_n) {
            snprintf(a->reden, HH_REDEN_MAX,
                     "AFGEWEZEN: valt buiten p_filesz (staat niet in het bestand)");
            continue;
        }
        memcpy(bytes, hh_beeld + off, sizeof bytes);
        if (!hh_proloog_ok(bytes, sizeof bytes)) {
            snprintf(a->reden, HH_REDEN_MAX,
                     "AFGEWEZEN: eerste bytes %02x %02x %02x %02x zijn geen prologue "
                     "(mogelijk al door een andere hook gepatcht)",
                     bytes[0], bytes[1], bytes[2], bytes[3]);
            continue;
        }
        a->geldig = 1;
        snprintf(a->reden, HH_REDEN_MAX,
                 "GELDIG: segment 0x%llx (R+X), bestand 0x%llx, prologue %02x %02x %02x %02x",
                 (unsigned long long)seg, (unsigned long long)off,
                 bytes[0], bytes[1], bytes[2], bytes[3]);
    }
    for (i = 0; i < hh_adres_n; i++)
        hh_say("  %-22s 0x%08llx  %s", hh_adreslijst[i].naam,
               (unsigned long long)hh_adreslijst[i].adres, hh_adreslijst[i].reden);
}

static struct hh_adres *hh_adres_zoek(const char *naam)
{
    int i;
    for (i = 0; i < hh_adres_n; i++)
        if (hh_adreslijst[i].geldig && strcmp(hh_adreslijst[i].naam, naam) == 0)
            return &hh_adreslijst[i];
    return NULL;
}

/* Elk adres in de lijst met deze naam, ook als het is afgekeurd. Alleen voor de rapportage: de
   aanhaakroute hierboven gebruikt hh_adres_zoek(), dat uitsluitend goedgekeurde adressen geeft. */
static struct hh_adres *hh_adres_zoek_ruw(const char *naam)
{
    int i;
    for (i = 0; i < hh_adres_n; i++)
        if (strcmp(hh_adreslijst[i].naam, naam) == 0)
            return &hh_adreslijst[i];
    return NULL;
}

/* De reden zonder het "AFGEWEZEN: "-voorvoegsel, zodat de samenvatting niet "afgekeurd: AFGEWEZEN"
   zegt. De regel in het keuringsblok hierboven houdt het voorvoegsel wel. */
static const char *hh_reden_kort(const struct hh_adres *a)
{
    const char *p = a->reden;
    if (!a->geldig && strncmp(p, "AFGEWEZEN: ", 11) == 0)
        return p + 11;
    return p;
}

/* De adreslijst opzoeken: expliciet via de omgeving, anders naast deze bibliotheek. */
static int hh_vind_adreslijst(void)
{
    const char *env = getenv("RAVENGUN_HASP_ELF_ADRESLIJST");
    char pad[4096];
    Dl_info info;
    if (env && *env) {
        if (hh_lees_adreslijst(env))
            return 1;
        hh_say("adreslijst %s is niet te lezen; alleen de naam-route", env);
        return 0;
    }
    if (dladdr((void *)hh_lees_adreslijst, &info) && info.dli_fname && *info.dli_fname) {
        const char *s = strrchr(info.dli_fname, '/');
        if (s) {
            snprintf(pad, sizeof pad, "%.*s/hasp-haken.ini", (int)(s - info.dli_fname),
                     info.dli_fname);
            if (hh_lees_adreslijst(pad))
                return 1;
        }
    }
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
    unsigned char nieuw[16];   /* de sprong die we hebben geschreven (voor de controle achteraf) */
    size_t nieuw_n;
    size_t lengte;
    int gezet;
    int herstel;               /* hoe vaak de sprong opnieuw is gezet nadat de lader hem wiste */
};

static struct hh_haak hh_haken[HH_MAX];
static int hh_haak_n;

/* ---------------------------------------------------------------- de levende code controleren */

/*
 * WAAROM DIT BESTAAT (gemeten 2026-10-13 aan Terminator Salvation onder rg-elfloader):
 * de lader mapt het hele beeld eerst als nulpagina's (rwx) en schrijft de echte code er daarna in,
 * binnen ongeveer een halve milliseconde. De ELF-kop komt als eerste, dus `hh_zoek_beeld()` vindt
 * het beeld al terwijl de code er nog niet staat. De sleutel was: byte 0 van elke hasp_*-functie
 * ging 0x00 -> 0xe9 (onze sprong) -> 0x53 (de lader schrijft de echte code, onze sprong WEG).
 * De keuring van de adreslijst kijkt naar het BESTAND (hh_beeld), niet naar het levende geheugen,
 * dus zij liet dat door.
 *
 * Daarom patchen we pas als de levende bytes op het adres gelijk zijn aan de bytes van het
 * bestand op hetzelfde virtuele adres, en daarna bewaken we de sprong nog even (hh_bewaak).
 */

static long hh_nu_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)(t.tv_sec * 1000L + t.tv_nsec / 1000000L);
}

static long hh_t0_ms;

/* Lees n bytes uit het eigen geheugen zonder een signaal te riskeren: process_vm_readv geeft
   EFAULT terug waar een gewone lezing SIGSEGV/SIGBUS zou geven. */
static int hh_lees_levend(uintptr_t a, unsigned char *buf, size_t n)
{
    struct iovec lok, rem;
    ssize_t r;
    lok.iov_base = buf;
    lok.iov_len = n;
    rem.iov_base = (void *)a;
    rem.iov_len = n;
    r = process_vm_readv(getpid(), &lok, 1, &rem, 1, 0);
    if (r == (ssize_t)n)
        return 1;
    if (r < 0 && errno != EFAULT) {
        /* de aanroep zelf is niet beschikbaar (seccomp o.i.d.): de aanroeper heeft het bereik al
           als leesbaar bevestigd, dus dan lezen we gewoon. */
        memcpy(buf, (const void *)a, n);
        return 1;
    }
    return 0;
}

/* Hoeveel bytes van het levende prologue vergelijken we met het bestand? Genoeg om een nulpagina,
   een half geschreven functie of een andere functie uit te sluiten. */
#define HH_PROLOOG_VERGELIJK 16

/*
 * Staat op `adres` in het levende geheugen dezelfde code als in het bestand?
 * Geeft 1 (gelijk, of niet te beoordelen omdat het bestand daar niets heeft), 0 (nog niet geladen /
 * wijkt af). `nul` krijgt 1 als de levende bytes allemaal 0 zijn (de lader heeft nog niet geschreven).
 */
static int hh_live_gelijk(uintptr_t adres, int *nul)
{
    unsigned char live[HH_PROLOOG_VERGELIJK];
    size_t off, n = HH_PROLOOG_VERGELIJK, i;
    uint64_t vaddr = (uint64_t)(adres - (uintptr_t)(hh_base - (uintptr_t)hh_laag));
    int alles_nul = 1;
    if (nul)
        *nul = 0;
    off = hh_v2o(vaddr);
    if (off == 0 || off + HH_PATCH_LEN > hh_beeld_n)
        return 1;                  /* geen bestandsbytes om mee te vergelijken: oud gedrag */
    if (off + n > hh_beeld_n)
        n = hh_beeld_n - off;
    if (!hh_lees_levend(adres, live, n)) {
        if (nul)
            *nul = 1;
        return 0;
    }
    for (i = 0; i < n; i++)
        if (live[i])
            alles_nul = 0;
    if (nul)
        *nul = alles_nul;
    return memcmp(live, hh_beeld + off, n) == 0;
}

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

/* Zorg dat de pagina('s) van de haak beschrijfbaar zijn. */
static int hh_maak_schrijfbaar(struct hh_haak *h)
{
    if (!hh_schrijfbaar(h->adres, h->lengte)) {
        size_t pag = (size_t)sysconf(_SC_PAGESIZE);
        uintptr_t lo = h->adres & ~(pag - 1);
        uintptr_t hi = (h->adres + h->lengte + pag - 1) & ~(pag - 1);
        if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
            return 0;
    }
    return 1;
}

static void hh_schrijf_sprong(struct hh_haak *h)
{
    unsigned char *p = (unsigned char *)h->adres;
    memcpy(p, h->nieuw, h->nieuw_n);
    __builtin___clear_cache((char *)p, (char *)p + h->nieuw_n);
}

static void hh_patch(struct hh_haak *h)
{
    unsigned char *p = (unsigned char *)h->adres;
    size_t i;
    if (h->gezet)
        return;
    if (!hh_maak_schrijfbaar(h)) {
        hh_say("  %s: mprotect mislukt; niet aangehaakt", h->naam);
        return;
    }
    for (i = 0; i < h->lengte; i++)
        h->oud[i] = p[i];
    h->origineel = hh_maak_trampoline(h);
    if (!h->origineel) {
        hh_say("  %s: geen uitvoerbare pagina voor de trampoline", h->naam);
        return;
    }
    {
        unsigned char *nieuw = h->nieuw;
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
        h->nieuw_n = n;
        hh_schrijf_sprong(h);
    }
    h->gezet = 1;
    hh_say("  aangehaakt: %-24s op 0x%08lx -> %p (trampoline %p, %zu bytes) [+%ld ms]", h->naam,
           (unsigned long)h->adres, h->vervanger, h->origineel, h->lengte,
           hh_nu_ms() - hh_t0_ms);
}

/* Hoeveel doelen hebben we in deze ronde NIET aangehaakt omdat de echte code er nog niet staat?
   Zolang dat er meer dan nul zijn, moet de kijker blijven kijken. */
static int hh_uitgesteld;
static long hh_laatste_wijziging_ms;   /* laatste keer dat we een sprong zetten of herstelden */

#define HH_HERSTEL_MAX 8

/*
 * Controleer de sprongen die we gezet hebben. Wist de lader (of iets anders) er een door de
 * echte code terug te schrijven, dan zetten we hem terug -- met een plafond, zodat een
 * proces dat telkens opnieuw schrijft ons niet in een eindeloze strijd trekt.
 * Staat er (nog) iets anders dan onze sprong of de echte code (bijvoorbeeld nullen), dan doen we
 * niets: pas als de echte code er staat is het veilig.
 */
static int hh_bewaak(void)
{
    int i, hersteld = 0;
    for (i = 0; i < hh_haak_n; i++) {
        struct hh_haak *h = &hh_haken[i];
        unsigned char cur[16];
        if (!h->gezet || h->nieuw_n == 0)
            continue;
        if (!hh_lees_levend(h->adres, cur, h->nieuw_n))
            continue;
        if (memcmp(cur, h->nieuw, h->nieuw_n) == 0)
            continue;                                  /* de sprong staat nog */
        if (memcmp(cur, h->oud, h->nieuw_n) != 0) {
            continue;                                  /* geen echte code: wachten */
        }
        if (h->herstel >= HH_HERSTEL_MAX) {
            if (h->herstel == HH_HERSTEL_MAX) {
                hh_say("  %s op 0x%08lx: de sprong is %d keer gewist; opgegeven", h->naam,
                       (unsigned long)h->adres, HH_HERSTEL_MAX);
                h->herstel++;
            }
            continue;
        }
        if (!hh_maak_schrijfbaar(h))
            continue;
        hh_schrijf_sprong(h);
        h->herstel++;
        hersteld++;
        hh_laatste_wijziging_ms = hh_nu_ms();
        hh_say("  HERSTELD: %-24s op 0x%08lx: de sprong was weggeschreven (herstel %d) [+%ld ms]",
               h->naam, (unsigned long)h->adres, h->herstel, hh_nu_ms() - hh_t0_ms);
    }
    return hersteld;
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

/* Staat deze naam in de dynsym van het spel, ongeacht of hij bruikbaar was? Alleen voor de
   rapportage: de aanhaakroute gebruikt hh_haak_alles(), dat op shndx en st_value let. */
static int hh_naam_in_dynsym(const char *naam)
{
    size_t i;
    for (i = 0; i < hh_sym_n; i++) {
        const char *n = hh_sym_naam(i);
        if (n && strcmp(n, naam) == 0)
            return 1;
    }
    return 0;
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

static uintptr_t hh_wacht_gemeld[HH_MAX];
static int hh_wacht_gemeld_n;

/* Eén functie aanhaken. Doet niets als het adres al aangehaakt is of (nog) niet leesbaar is. */
static void hh_haak_een(const char *naam, uintptr_t adres, void *vervanger)
{
    struct hh_haak *h;
    if (hh_al_gehaakt(adres))
        return;
    if (hh_haak_n >= HH_MAX) {
        hh_say("  geen ruimte meer in de hakenlijst");
        return;
    }
    h = &hh_haken[hh_haak_n];
    snprintf(h->naam, sizeof(h->naam), "%s", naam);
    h->adres = adres;
    h->vervanger = vervanger;
    h->lengte = HH_PATCH_LEN;
    if (!hh_bereik_leesbaar(h->adres, h->lengte)) {
        hh_uitgesteld++;
        hh_say("  %s op 0x%08lx is (nog) niet leesbaar; volgende ronde", naam,
               (unsigned long)h->adres);
        return;
    }
    /* De levende code moet de code uit het bestand zijn. Zo niet (nulpagina, half geschreven),
       dan patchen we NIET: de lader zou onze sprong overschrijven. De kijker probeert het opnieuw. */
    {
        int nul = 0;
        if (!hh_live_gelijk(adres, &nul)) {
            int i, al_gemeld = 0;
            hh_uitgesteld++;
            for (i = 0; i < hh_wacht_gemeld_n; i++)
                if (hh_wacht_gemeld[i] == adres)
                    al_gemeld = 1;
            if (!al_gemeld && hh_wacht_gemeld_n < HH_MAX) {
                hh_wacht_gemeld[hh_wacht_gemeld_n++] = adres;
                hh_say("  %s op 0x%08lx: levende bytes %s het bestand; niet aangehaakt, "
                       "wacht op de lader [+%ld ms]", naam, (unsigned long)adres,
                       nul ? "zijn nul/onleesbaar, anders dan" : "wijken af van",
                       hh_nu_ms() - hh_t0_ms);
            }
            return;
        }
    }
    hh_haak_n++;
    hh_patch(h);
    if (h->gezet) {
        hh_koppel_echt(naam, h->origineel);
        hh_laatste_wijziging_ms = hh_nu_ms();
    } else
        hh_haak_n--;   /* niet aangehaakt: de lijst niet vervuilen */
}

static void hh_haak_alles(void)
{
    const size_t doelen_n = sizeof(hh_doelen) / sizeof(hh_doelen[0]);
    size_t i, d;

    hh_uitgesteld = 0;
    /* 1. De naam-route: de standaard, en het gedrag zoals het was. */
    for (d = 0; d < doelen_n; d++) {
        for (i = 0; i < hh_sym_n; i++) {
            const char *naam = hh_sym_naam(i);
            uintptr_t adres;
            if (!naam || strcmp(naam, hh_doelen[d].naam) != 0)
                continue;
            if (hh_symtab[i].st_shndx == SHN_UNDEF || hh_symtab[i].st_value == 0)
                break;
            if (hh_symtab[i].st_size != 0 && (size_t)hh_symtab[i].st_size < HH_PATCH_LEN) {
                hh_say("  %s is maar %llu bytes; overgeslagen (patch is %d bytes)", naam,
                       (unsigned long long)hh_symtab[i].st_size, HH_PATCH_LEN);
                break;
            }
            /* Het laadadres (de bias) is hh_base - hh_laag, NIET hh_base: bij een ET_EXEC is de
               bias 0 en zou `hh_base + st_value` het laagste segmentadres dubbel tellen
               (gemeten: Halo hh_laag = 0x400000, Jurassic Park hh_laag = 0x08048000). */
            adres = (uintptr_t)(hh_base - (uintptr_t)hh_laag) + (uintptr_t)hh_symtab[i].st_value;
            hh_haak_een(naam, adres, hh_doelen[d].vervanger);
            break;
        }
    }

    /* 2. De adres-route: alleen als er een gekeurde adreslijst is. Elk adres is in
          hh_keur_adreslijst() al langs het segment en de prologue gehaald. */
    if (!hh_adres_n)
        return;
    for (d = 0; d < doelen_n; d++) {
        struct hh_adres *a = hh_adres_zoek(hh_doelen[d].naam);
        uintptr_t adres;
        if (!a)
            continue;
        adres = (uintptr_t)a->adres;
        if (hh_adres_basis_beeld)
            adres += (uintptr_t)(hh_base - (uintptr_t)hh_laag);
        hh_haak_een(hh_doelen[d].naam, adres, hh_doelen[d].vervanger);
    }
}

/*
 * Welke doelen zijn niet aangehaakt, en waarom niet? Deze drie gevallen moeten uit elkaar blijven,
 * want juist deze samenvatting leest iemand die een benchrun uitzoekt:
 *   1. niet in de dynsym en niet in de adreslijst;
 *   2. wel in de adreslijst, maar bij de keuring afgewezen -- met de reden erbij;
 *   3. wel aangehaakt (die komt hier niet langs).
 * Daarnaast twee gevallen die anders weer een verkeerd beeld geven: een adres dat goedgekeurd is
 * maar toch niet aangehaakt raakte, en een naam die wel in de dynsym staat maar daar onbruikbaar was.
 */
static void hh_meld_ontbrekend(void)
{
    const size_t doelen_n = sizeof(hh_doelen) / sizeof(hh_doelen[0]);
    size_t d, i;
    int n = 0;
    for (d = 0; d < doelen_n; d++) {
        const char *naam = hh_doelen[d].naam;
        struct hh_adres *ruw = hh_adres_n ? hh_adres_zoek_ruw(naam) : NULL;
        int gehaakt = 0;
        for (i = 0; i < (size_t)hh_haak_n; i++)
            if (!strcmp(hh_haken[i].naam, naam)) {
                gehaakt = 1;
                break;
            }
        if (gehaakt)
            continue;
        if (ruw && !ruw->geldig)
            hh_say("  niet aangehaakt: %-22s (in de adreslijst maar afgekeurd: %s)", naam,
                   hh_reden_kort(ruw));
        else if (ruw)
            hh_say("  niet aangehaakt: %-22s (in de adreslijst en goedgekeurd, maar het aanhaken "
                   "lukte niet; zie het logboek)", naam);
        else if (hh_naam_in_dynsym(naam))
            hh_say("  niet aangehaakt: %-22s (staat in de dynsym, maar was daar niet bruikbaar; "
                   "zie het logboek)", naam);
        else
            hh_say("  niet aangehaakt: %-22s (niet in de dynsym en niet in de adreslijst)", naam);
        n++;
    }
    hh_say("totaal aangehaakt: %d van %d; niet aangehaakt: %d", hh_haak_n, (int)doelen_n, n);
}

/* ---------------------------------------------------------------- de kijker */

static int hh_gestart;
static int hh_gedaan;

static void *hh_kijker(void *arg)
{
    long ms = 5, secs = 90, bewaak_ms = 2000;
    const char *v;
    long rondes, r;
    int gemeld = 0, beeld_gevonden = 0, stabiel_gemeld = 0;
    (void)arg;

    if ((v = getenv("RAVENGUN_HASP_ELF_MS")) && *v)
        ms = strtol(v, NULL, 0);
    if ((v = getenv("RAVENGUN_HASP_ELF_WACHT")) && *v)
        secs = strtol(v, NULL, 0);
    /* Hoe lang we na de laatste sprong nog blijven controleren of hij blijft staan. */
    if ((v = getenv("RAVENGUN_HASP_ELF_BEWAAK")) && *v)
        bewaak_ms = strtol(v, NULL, 0);
    if (ms < 1)
        ms = 1;
    if (secs < 1)
        secs = 1;
    if (bewaak_ms < 0)
        bewaak_ms = 0;
    rondes = (secs * 1000) / ms;
    hh_laatste_wijziging_ms = hh_nu_ms();

    for (r = 0; r < rondes && !hh_gedaan; r++) {
        /* Het beeld zoeken wisselt tijdelijk de SIGSEGV/SIGBUS-afhandelaars om; zodra we het
           beeld hebben doen we dat niet meer elke ronde. */
        if (!beeld_gevonden) {
            if (!hh_zoek_beeld()) {
                usleep((useconds_t)(ms * 1000));
                continue;
            }
            beeld_gevonden = 1;
        }
        if (!gemeld) {
            gemeld = 1;
            hh_say("spelbeeld op 0x%08lx [+%ld ms]", (unsigned long)hh_base,
                   hh_nu_ms() - hh_t0_ms);
            if (!hh_laad_emulator()) {
                hh_say("zonder emulator niets aangehaakt");
                hh_gedaan = 1;
                return NULL;
            }
            hh_koppel_emulator();
        }
        hh_haak_alles();
        hh_bewaak();
        /* Klaar als er niets meer wacht op de lader EN de sprongen een tijd ongestoord hebben
           gestaan. Doelen die nooit aan te haken zijn (bijvoorbeeld een afgekeurde hasp_free)
           staan niet in `uitgesteld` en houden de kijker dus niet vast. */
        if (hh_haak_n > 0 && hh_uitgesteld == 0) {
            if (!stabiel_gemeld && hh_haak_n >= (int)(sizeof(hh_doelen) / sizeof(hh_doelen[0]))) {
                stabiel_gemeld = 1;
                hh_say("alle %d functies aangehaakt; bewaak nog %ld ms", hh_haak_n, bewaak_ms);
            }
            if (hh_nu_ms() - hh_laatste_wijziging_ms >= bewaak_ms) {
                hh_gedaan = 1;
                hh_say("klaar: %d functies aangehaakt en %ld ms ongestoord [+%ld ms]", hh_haak_n,
                       bewaak_ms, hh_nu_ms() - hh_t0_ms);
                break;
            }
        }
        usleep((useconds_t)(ms * 1000));
    }
    if (!hh_gedaan) {
        if (hh_haak_n > 0)
            hh_say("gestopt na %ld s: %d functies aangehaakt, %d wachten nog op de echte code",
                   secs, hh_haak_n, hh_uitgesteld);
        else
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
    if (!hh_t0_ms)
        hh_t0_ms = hh_nu_ms();
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
    hh_meld_ontbrekend();
    n = hh_haak_n;
    if (n >= (int)(sizeof(hh_doelen) / sizeof(hh_doelen[0]))) {
        hh_say("klaar (synchroon): %d functies aangehaakt", n);
        return 1;
    }
    hh_say("synchroon %d van %d functies aangehaakt; de kijker probeert de rest",
           n, (int)(sizeof(hh_doelen) / sizeof(hh_doelen[0])));
    /* Wacht er nog iets op de lader (echte code nog niet geschreven), dan is dit NIET gelukt:
       de kijker moet het overnemen. Zonder dit stopte de hook na een gedeeltelijk resultaat. */
    if (hh_uitgesteld > 0)
        return 0;
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
    /* De adreslijst is de tweede weg, naast de naam-route. Zonder bestand verandert er niets;
       met een bestand wordt elk adres eerst gekeurd en pas daarna gepatcht. */
    if (hh_vind_adreslijst())
        hh_keur_adreslijst();
    else
        hh_say("geen adreslijst; alleen de naam-route (dat is de standaard)");
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
