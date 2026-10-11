/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hf_probe.c — meetprogramma dat als LD_PRELOAD in het ELF-proces van een ElfLdr2-game gaat en
 * vaststelt HOE de HASP-aanroepen lopen. Antwoord op de kernvraag van deze ronde:
 *   1. staat de HASP-cliënt statisch in de game (dynsym van de game zelf)?
 *   2. is er een PLT-verplaatsing voor hasp (dan zou GOT-herschrijving werken)?
 *   3. is de statisch gelinkte functie echt aanroepbaar (dan werkt inline-hooking)?
 *
 * Het roept hasp_login aan via een ZELF uitgerekend adres en niet via zijn eigen export,
 * zodat de meting niet door symboolinterpositie vervuild raakt.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *uit;

static void zeg(const char *fmt, ...)
{
    va_list ap;
    if (!uit)
        return;
    va_start(ap, fmt);
    vfprintf(uit, fmt, ap);
    va_end(ap);
    fputc('\n', uit);
    fflush(uit);
}

/* ---------------------------------------------------------------- beeld van schijf */

static unsigned char *beeld;
static size_t beeld_n;
static Elf32_Ehdr *eh;
static Elf32_Phdr *ph;

static int lees_elf_pad(const char *pad, const char *waarom)
{
    FILE *f = fopen(pad, "rb");
    long grootte;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    grootte = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (grootte <= 0 || grootte > 256L * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    beeld = malloc((size_t)grootte);
    if (!beeld) {
        fclose(f);
        return 0;
    }
    beeld_n = fread(beeld, 1, (size_t)grootte, f);
    fclose(f);
    if (beeld_n < sizeof(Elf32_Ehdr) || memcmp(beeld, ELFMAG, 4) != 0) {
        free(beeld);
        beeld = NULL;
        return 0;
    }
    zeg("  game-ELF gelezen uit %s (%s)", pad, waarom);
    return 1;
}

static int lees_zelf(void)
{
    char cwd[4096];
    char pad[4096];
    ssize_t n;
    const char *env = getenv("HF_PROBE_GAME");
    int i;

    /* Het proces is wine-preloader; /proc/self/exe is dus NIET de game. De game-ELF staat op
       schijf; we vinden hem via de werkmap van het proces en via de commandoregel. */
    if (env && *env && lees_elf_pad(env, "HF_PROBE_GAME"))
        goto klaar;
    n = readlink("/proc/self/cwd", cwd, sizeof(cwd) - 1);
    if (n > 0) {
        cwd[n] = 0;
        zeg("  werkmap = %s", cwd);
        snprintf(pad, sizeof(pad), "%s/game", cwd);
        if (lees_elf_pad(pad, "werkmap/game"))
            goto klaar;
    }
    {
        FILE *f = fopen("/proc/self/cmdline", "rb");
        if (f) {
            size_t m = fread(pad, 1, sizeof(pad) - 1, f);
            size_t k;
            fclose(f);
            pad[m] = 0;
            for (k = 0; k < m; k++)
                if (pad[k] == 0)
                    pad[k] = '\n';
            zeg("  cmdline = %s", pad);
        }
    }
    for (i = 0; i < 2; i++) {
        const char *vast = i == 0 ? "/home/deck/.local/share/ravengun/runtime/views/JurassicPark/game"
                                  : "/home/deck/.local/share/ravengun/runtime/views/GalagaAssault/game";
        if (lees_elf_pad(vast, "vast pad"))
            goto klaar;
    }
    zeg("  GEEN game-ELF gevonden (zet HF_PROBE_GAME)");
    return 0;

klaar:
    eh = (Elf32_Ehdr *)beeld;
    ph = (Elf32_Phdr *)(beeld + eh->e_phoff);
    zeg("  game-ELF: %zu bytes, klasse %d, type %d, machine %d, %d program headers", beeld_n,
        eh->e_ident[EI_CLASS], eh->e_type, eh->e_machine, eh->e_phnum);
    return 1;
}

/* virtueel adres -> bestands-offset, via de PT_LOAD-segmenten */
static size_t vaddr_naar_offset(uint32_t vaddr)
{
    int i;
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (vaddr >= ph[i].p_vaddr && vaddr < ph[i].p_vaddr + ph[i].p_filesz)
            return (size_t)(vaddr - ph[i].p_vaddr + ph[i].p_offset);
    }
    return 0;
}

/* ---------------------------------------------------------------- het laadadres */

static uintptr_t hoofd_base;

static int phdr_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    (void)data;
    if (info->dlpi_name == NULL || info->dlpi_name[0] == 0) {
        hoofd_base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- dynsym */

struct sym {
    char naam[64];
    uint32_t waarde;
    uint32_t grootte;
};

static struct sym gevonden[128];
static int gevonden_n;

static Elf32_Sym *dyn_symtab;
static const char *dyn_strtab;
static size_t dyn_sym_n;

static int vind_dynsym(void)
{
    Elf32_Dyn *dyn = NULL;
    int i;
    size_t sym_off = 0, str_off = 0;
    for (i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_DYNAMIC)
            dyn = (Elf32_Dyn *)(beeld + ph[i].p_offset);
    if (!dyn) {
        zeg("  geen PT_DYNAMIC");
        return 0;
    }
    for (i = 0; dyn[i].d_tag != DT_NULL; i++) {
        switch (dyn[i].d_tag) {
        case DT_SYMTAB:
            sym_off = vaddr_naar_offset((uint32_t)dyn[i].d_un.d_ptr);
            break;
        case DT_STRTAB:
            str_off = vaddr_naar_offset((uint32_t)dyn[i].d_un.d_ptr);
            break;
        case DT_HASH: {
            size_t o = vaddr_naar_offset((uint32_t)dyn[i].d_un.d_ptr);
            const uint32_t *h = (const uint32_t *)(beeld + o);
            if (o && o + 8 <= beeld_n)
                dyn_sym_n = h[1];
            break;
        }
        default:
            break;
        }
    }
    if (!sym_off || !str_off) {
        zeg("  DT_SYMTAB of DT_STRTAB valt buiten de PT_LOAD-segmenten (sym 0x%zx, str 0x%zx)",
            sym_off, str_off);
        return 0;
    }
    dyn_symtab = (Elf32_Sym *)(beeld + sym_off);
    dyn_strtab = (const char *)(beeld + str_off);
    /* Beide tabellen moeten binnen het beeld vallen; anders lezen we willekeurig geheugen. */
    if (sym_off + dyn_sym_n * sizeof(Elf32_Sym) > beeld_n) {
        zeg("  symtab (0x%zx + %zu*%zu) valt buiten het beeld van %zu bytes", sym_off, dyn_sym_n,
            sizeof(Elf32_Sym), beeld_n);
        dyn_sym_n = (beeld_n - sym_off) / sizeof(Elf32_Sym);
        zeg("  symtab teruggebracht tot %zu symbolen", dyn_sym_n);
    }
    zeg("  DT_SYMTAB op bestands-offset 0x%zx, DT_STRTAB op 0x%zx, %zu dynamische symbolen",
        sym_off, str_off, dyn_sym_n);
    return 1;
}

static void verzamel_hasp(void)
{
    size_t i;
    size_t str_off = (size_t)(dyn_strtab - (const char *)beeld);
    size_t str_max = beeld_n - str_off;
    zeg("    [dbg] symtab=%p strtab=%p str_off=0x%zx str_max=0x%zx n=%zu",
        (const void *)dyn_symtab, (const void *)dyn_strtab, str_off, str_max, dyn_sym_n);
    for (i = 0; i < dyn_sym_n && gevonden_n < 128; i++) {
        const char *n;
        size_t no;
        if (dyn_symtab[i].st_name == 0)
            continue;
        no = dyn_symtab[i].st_name;
        if (no >= str_max)
            continue;
        n = dyn_strtab + no;
        if (memchr(n, 0, str_max - no) == NULL)
            continue;
        if (i < 5)
            zeg("    [dbg] sym[%zu] naam='%s' waarde=0x%08x", i, n, dyn_symtab[i].st_value);
        if (strncmp(n, "hasp_", 5) != 0 && strncmp(n, "comaladdin", 10) != 0 &&
            strncmp(n, "__hasp", 6) != 0 && strncmp(n, "_hasp", 5) != 0)
            continue;
        snprintf(gevonden[gevonden_n].naam, sizeof(gevonden[0].naam), "%s", n);
        gevonden[gevonden_n].waarde = dyn_symtab[i].st_value;
        gevonden[gevonden_n].grootte = dyn_symtab[i].st_size;
        gevonden_n++;
    }
    zeg("  %d hasp-symbolen in de dynsym van de GAME:", gevonden_n);
    for (i = 0; i < (size_t)gevonden_n; i++)
        zeg("    %-30s waarde 0x%08x  grootte %u", gevonden[i].naam, gevonden[i].waarde,
            gevonden[i].grootte);
}

/* ---------------------------------------------------------------- PLT/GOT */

/* Staat er een ELF op dit adres? Dan is de game daar afgebeeld.
   LET OP: 0x08048000 is pas leesbaar NADAT ElfLdr2 de game heeft afgebeeld. Vóór die tijd zou een
   directe lezing een segfault geven (gemeten). Daarom eerst /proc/self/maps raadplegen. */
static uintptr_t game_base = 0x08048000u;

static int bereik_leesbaar(uintptr_t a, size_t n)
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

static int controleer_base(uintptr_t b)
{
    const unsigned char *p;
    if (!bereik_leesbaar(b, 4))
        return 0;
    p = (const unsigned char *)b;
    return p[0] == 0x7f && p[1] == 'E' && p[2] == 'L' && p[3] == 'F';
}

static void bekijk_verplaatsingen(void)
{
    Elf32_Dyn *dyn = NULL;
    int i;
    uint32_t jmprel = 0, pltrelsz = 0, pltgot = 0;
    for (i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_DYNAMIC)
            dyn = (Elf32_Dyn *)(beeld + ph[i].p_offset);
    if (!dyn)
        return;
    for (i = 0; dyn[i].d_tag != DT_NULL; i++) {
        switch (dyn[i].d_tag) {
        case DT_JMPREL: jmprel = (uint32_t)dyn[i].d_un.d_ptr; break;
        case DT_PLTRELSZ: pltrelsz = (uint32_t)dyn[i].d_un.d_val; break;
        case DT_PLTGOT: pltgot = (uint32_t)dyn[i].d_un.d_ptr; break;
        default: break;
        }
    }
    zeg("  PLTGOT 0x%08x  JMPREL 0x%08x  PLTRELSZ %u (%u stuks)", pltgot, jmprel, pltrelsz,
        pltrelsz / (uint32_t)sizeof(Elf32_Rel));
    if (!jmprel || !dyn_symtab || !dyn_strtab) {
        zeg("  geen PLT-verplaatsingen in dit bestand");
        return;
    }
    {
        size_t off = vaddr_naar_offset(jmprel);
        Elf32_Rel *rel = (Elf32_Rel *)(beeld + off);
        unsigned n = pltrelsz / (unsigned)sizeof(Elf32_Rel);
        unsigned k, hasp = 0, onopgelost = 0, bekeken = 0;
        int in_geheugen = controleer_base(game_base);
        for (k = 0; k < n; k++) {
            unsigned symi = ELF32_R_SYM(rel[k].r_info);
            const char *naam = dyn_strtab + dyn_symtab[symi].st_name;
            uint32_t got = rel[k].r_offset;
            uint32_t waar = in_geheugen ? *(volatile uint32_t *)(uintptr_t)got : 0xffffffffu;
            if (strstr(naam, "hasp")) {
                hasp++;
                zeg("    HASP-verplaatsing! GOT 0x%08x = 0x%08x  %s", got, waar, naam);
            }
            if (in_geheugen && waar == got + 4)
                onopgelost++;
            if (bekeken < 10) {
                zeg("    GOT 0x%08x = 0x%08x  %s%s", got, waar, naam,
                    (in_geheugen && waar == got + 4) ? "   [ONOPGELOST]" : "");
                bekeken++;
            }
        }
        zeg("  PLT-verplaatsingen: %u   met 'hasp': %u   onopgelost: %u", n, hasp, onopgelost);
    }
}

/* ---------------------------------------------------------------- de echte aanroep */

typedef int (*hasp_login_fn)(unsigned, const char *, void **);

static void zoek_game_base(void)
{
    FILE *f;
    char regel[512];
    if (controleer_base(game_base)) {
        zeg("  game_base 0x%08lx bevestigd (ELF-magic in het geheugen)", (unsigned long)game_base);
        return;
    }
    zeg("  game_base 0x%08lx GEEN ELF-magic; /proc/self/maps doorzoeken", (unsigned long)game_base);
    f = fopen("/proc/self/maps", "r");
    if (!f)
        return;
    while (fgets(regel, sizeof(regel), f)) {
        unsigned long a;
        char rechten[8];
        if (sscanf(regel, "%lx-%*x %7s", &a, rechten) != 2)
            continue;
        if (rechten[0] != 'r')
            continue;
        if (controleer_base((uintptr_t)a)) {
            game_base = (uintptr_t)a;
            zeg("  game_base gevonden op 0x%08lx", (unsigned long)game_base);
            break;
        }
    }
    fclose(f);
}

static void probeer_login(void)
{
    int i;
    for (i = 0; i < gevonden_n; i++) {
        uintptr_t adres;
        unsigned char *p;
        hasp_login_fn fn;
        void *greep = NULL;
        int rc;
        if (strcmp(gevonden[i].naam, "hasp_login") != 0)
            continue;
        adres = game_base + gevonden[i].waarde;
        p = (unsigned char *)adres;
        fn = (hasp_login_fn)adres;
        zeg("  hasp_login op runtime-adres 0x%08lx; eerste bytes: %02x %02x %02x %02x %02x %02x",
            (unsigned long)adres, p[0], p[1], p[2], p[3], p[4], p[5]);
        rc = fn(0, NULL, &greep);
        zeg("  AANROEP van de STATISCH gelinkte hasp_login -> %d, greep = %p", rc, greep);
    }
}

static void hf_doe_meting(void)
{
    const char *log = getenv("HF_PROBE_LOG");
    if (log && *log)
        uit = fopen(log, "a");
    if (!uit)
        uit = stderr;
    zeg("=== hf_probe geladen (pid %d) ===", (int)getpid());
    dl_iterate_phdr(phdr_cb, NULL);
    zeg("  hoofd_base = 0x%08lx", (unsigned long)hoofd_base);
    if (!lees_zelf())
        return;
    if (!vind_dynsym())
        return;
    zoek_game_base();
    verzamel_hasp();
    bekijk_verplaatsingen();
    if (getenv("HF_PROBE_CALL"))
        probeer_login();
    zeg("=== einde hf_probe ===");
}

__attribute__((constructor))
static void hf_init(void)
{
    hf_doe_meting();
}

/* LD_AUDIT: glibc roept deze twee aan. la_version moet er zijn, anders laadt glibc de module niet;
   la_preinit valt vlak voor de start van het programma. Beide zijn nodig om de module ook als
   audit-module te kunnen gebruiken (het kanaal dat in de praktijk werkt: LD_PRELOAD sterft aan
   Cygwin, LD_AUDIT niet). */
unsigned int la_version(unsigned int v)
{
    return v;
}

void la_preinit(uintptr_t *cookie)
{
    (void)cookie;
}
