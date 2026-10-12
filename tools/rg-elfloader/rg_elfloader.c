/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-elfloader -- een eigen i386-ELF-lader voor de ElfLdr2-familie, zonder BudgieLoader en zonder
 * Wine. Eerste doel: GoGoStrike (bowling_usa), de ongepakte, link-complete game uit de familie.
 *
 * Wat deze lader doet (zie patches/ELF-LADER-SPEC.md, "Wat een eigen lader moet doen"):
 *   1. De PT_LOAD-segmenten op hun p_vaddr mappen (ET_EXEC, niet-PIE, basis 0x08048000: vaste adressen).
 *   2. De benodigde gastbibliotheken met dlopen laden (uit ElfLdr2/libs via LD_LIBRARY_PATH); libc/libm
 *      zijn al in dit proces.
 *   3. De relocaties toepassen: R_386_COPY (data uit de bib naar het spel kopieren), R_386_GLOB_DAT en
 *      R_386_JMP_SLOT (= opgezocht adres), plus R_386_RELATIVE/32/PC32 voor de volledigheid.
 *   4. TLS: we erven de eigen glibc-TLS (%gs). GoGoStrike heeft 0 TLS-relocaties en deelt dezelfde
 *      libc.so.6 als deze lader, dus gs:[0x14] is al een geldige, stabiele stackcanarie. Daarom hoeft
 *      deze lader zelf geen TLS op te zetten -- precies het punt waar eerder aan getwijfeld is.
 *   5. Een System V-startstack opbouwen (argc/argv/envp/auxv met AT_PHDR/AT_ENTRY/AT_RANDOM...) en naar
 *      e_entry (_start) springen. __libc_start_main draait daarna zelf de init-array van het spel; we
 *      draaien DT_INIT dus niet dubbel.
 *
 * Deze lader is zelf i386 (gcc -m32) en een gewoon glibc-programma: hij deelt libc.so.6 met het spel,
 * zodat er geen "twee libc's" in het proces zitten (GoGoStrike heeft libc.so.6 + libm.so.6 als
 * NEEDED, de moderne systeem-libc's; alleen libstdc++.so.5/liballeg.so.4.0/libNL.so.1.6 komen uit
 * ElfLdr2/libs).
 *
 * Bouwen: scripts/build-elfloader.sh. Gebruik: LD_LIBRARY_PATH=<ElfLdr2/libs> rg-elfloader <game> [args].
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int rg_verbose;

static void say(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "rg-elfloader: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}
#define dbg(...) do { if (rg_verbose) say(__VA_ARGS__); } while (0)

static void fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "rg-elfloader: FATAAL: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    _exit(127);
}

#define PAGE 0x1000u
#define ALIGN_DOWN(x) ((x) & ~(PAGE - 1))
#define ALIGN_UP(x)   (((x) + PAGE - 1) & ~(PAGE - 1))

/* ------------------------------------------------------------------ het spelbestand inlezen */

static uint8_t *map_file(const char *pad, size_t *len_uit)
{
    int fd = open(pad, O_RDONLY);
    struct stat st;
    uint8_t *m;
    if (fd < 0)
        fatal("kan %s niet openen", pad);
    if (fstat(fd, &st) != 0)
        fatal("fstat %s mislukt", pad);
    m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m == MAP_FAILED)
        fatal("mmap van %s mislukt", pad);
    *len_uit = st.st_size;
    /* fd mag dicht: de mapping blijft. De PT_LOAD-mappings lezen we los via pread, dus houd fd open. */
    return m;
}

/* ------------------------------------------------------------------ segmenten mappen */

struct beeld {
    Elf32_Ehdr *eh;
    Elf32_Phdr *ph;
    int phnum;
    uint32_t phdr_vaddr;   /* adres van de phdr-tabel in het geladen beeld (voor AT_PHDR) */
    Elf32_Dyn *dyn;
};

static void map_segments(int fd, const uint8_t *file, struct beeld *b)
{
    Elf32_Ehdr *eh = (Elf32_Ehdr *)file;
    Elf32_Phdr *ph = (Elf32_Phdr *)(file + eh->e_phoff);
    uint32_t lo = 0xffffffffu, hi = 0;
    int i;

    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32 ||
        eh->e_machine != EM_386)
        fatal("geen 32-bits i386-ELF");
    if (eh->e_type != ET_EXEC)
        fatal("verwacht ET_EXEC (niet-PIE); dit is type %d", eh->e_type);

    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_vaddr < lo)
            lo = ALIGN_DOWN(ph[i].p_vaddr);
        if (ph[i].p_vaddr + ph[i].p_memsz > hi)
            hi = ALIGN_UP(ph[i].p_vaddr + ph[i].p_memsz);
    }
    if (lo == 0xffffffffu)
        fatal("geen PT_LOAD");
    dbg("beeld-bereik %#x..%#x (%u KB)", lo, hi, (hi - lo) / 1024);

    /* Het hele bereik in een keer anoniem (nul-gevuld, dus bss is geregeld); dan per segment de
       bestandsinhoud erin lezen en de rechten zetten. Zo zijn overlap en bestand-offset-uitlijning
       geen probleem. */
    if (mmap((void *)lo, hi - lo, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
        fatal("kan het spelbeeld niet op %#x mappen", lo);

    for (i = 0; i < eh->e_phnum; i++) {
        ssize_t n;
        if (ph[i].p_type != PT_LOAD || ph[i].p_filesz == 0)
            continue;
        n = pread(fd, (void *)ph[i].p_vaddr, ph[i].p_filesz, ph[i].p_offset);
        if (n < 0 || (size_t)n != ph[i].p_filesz)
            fatal("kan segment %d niet inlezen (%zd van %u)", i, n, ph[i].p_filesz);
    }
    for (i = 0; i < eh->e_phnum; i++) {
        int prot = 0;
        uint32_t a, z;
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_flags & PF_R) prot |= PROT_READ;
        if (ph[i].p_flags & PF_W) prot |= PROT_WRITE;
        if (ph[i].p_flags & PF_X) prot |= PROT_EXEC;
        a = ALIGN_DOWN(ph[i].p_vaddr);
        z = ALIGN_UP(ph[i].p_vaddr + ph[i].p_memsz);
        if (mprotect((void *)a, z - a, prot) != 0)
            fatal("mprotect %#x..%#x mislukt", a, z);
    }

    /* AT_PHDR: PT_PHDR als die er is, anders het segment dat e_phoff dekt. */
    b->phdr_vaddr = 0;
    for (i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_PHDR) { b->phdr_vaddr = ph[i].p_vaddr; break; }
    if (!b->phdr_vaddr) {
        for (i = 0; i < eh->e_phnum; i++)
            if (ph[i].p_type == PT_LOAD && eh->e_phoff >= ph[i].p_offset &&
                eh->e_phoff < ph[i].p_offset + ph[i].p_filesz) {
                b->phdr_vaddr = ph[i].p_vaddr + (eh->e_phoff - ph[i].p_offset);
                break;
            }
    }
    b->eh = eh;
    b->ph = ph;
    b->phnum = eh->e_phnum;

    /* PT_DYNAMIC: na het mappen staat die op zijn p_vaddr. */
    b->dyn = NULL;
    for (i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_DYNAMIC)
            b->dyn = (Elf32_Dyn *)ph[i].p_vaddr;
    if (!b->dyn)
        fatal("geen PT_DYNAMIC");
}

/* ------------------------------------------------------------------ dynamische tabel + relocaties */

struct dyninfo {
    Elf32_Sym *symtab;
    const char *strtab;
    Elf32_Rel *rel;    uint32_t relsz;
    Elf32_Rel *jmprel; uint32_t pltrelsz;
    uint32_t *init_array; uint32_t init_arraysz;
    void (*init)(void);
};

static void lees_dyn(struct beeld *b, struct dyninfo *d)
{
    Elf32_Dyn *dy;
    memset(d, 0, sizeof(*d));
    for (dy = b->dyn; dy->d_tag != DT_NULL; dy++) {
        switch (dy->d_tag) {
        case DT_SYMTAB:       d->symtab = (Elf32_Sym *)dy->d_un.d_ptr; break;
        case DT_STRTAB:       d->strtab = (const char *)dy->d_un.d_ptr; break;
        case DT_REL:          d->rel = (Elf32_Rel *)dy->d_un.d_ptr; break;
        case DT_RELSZ:        d->relsz = dy->d_un.d_val; break;
        case DT_JMPREL:       d->jmprel = (Elf32_Rel *)dy->d_un.d_ptr; break;
        case DT_PLTRELSZ:     d->pltrelsz = dy->d_un.d_val; break;
        case DT_INIT:         d->init = (void (*)(void))dy->d_un.d_ptr; break;
        case DT_INIT_ARRAY:   d->init_array = (uint32_t *)dy->d_un.d_ptr; break;
        case DT_INIT_ARRAYSZ: d->init_arraysz = dy->d_un.d_val; break;
        default: break;
        }
    }
    if (!d->symtab || !d->strtab)
        fatal("geen DT_SYMTAB/DT_STRTAB");
}

/* De benodigde bibliotheken laden. libc.so.6/libm.so.6 zijn al in dit proces; dlopen geeft dan het
   bestaande handvat terug. De oude libs (libstdc++.so.5 enz.) komen via LD_LIBRARY_PATH uit
   ElfLdr2/libs. RTLD_GLOBAL zodat hun symbolen in de globale zoekruimte komen (voor R_386_COPY). */
static void laad_needed(struct beeld *b, struct dyninfo *d)
{
    Elf32_Dyn *dy;
    for (dy = b->dyn; dy->d_tag != DT_NULL; dy++) {
        const char *naam;
        if (dy->d_tag != DT_NEEDED)
            continue;
        naam = d->strtab + dy->d_un.d_val;
        if (!dlopen(naam, RTLD_NOW | RTLD_GLOBAL))
            fatal("dlopen(%s) mislukt: %s", naam, dlerror());
        dbg("geladen: %s", naam);
    }
}

static void *zoek_symbool(const char *naam)
{
    void *a = dlsym(RTLD_DEFAULT, naam);
    return a;   /* RTLD_DEFAULT doorzoekt de globale scope incl. onze RTLD_GLOBAL-dlopens + libc/libm */
}

static void pas_rel_toe(struct dyninfo *d, Elf32_Rel *r, uint32_t n, const char *welk)
{
    uint32_t i;
    uint32_t onopgelost = 0, copies = 0, slots = 0;
    for (i = 0; i < n; i++) {
        uint32_t type = ELF32_R_TYPE(r[i].r_info);
        uint32_t sidx = ELF32_R_SYM(r[i].r_info);
        uint32_t *waar = (uint32_t *)r[i].r_offset;
        const char *naam = sidx ? d->strtab + d->symtab[sidx].st_name : "";
        void *sym = NULL;

        if (type == R_386_RELATIVE) {
            /* basis = 0 voor ET_EXEC; *waar blijft gelijk. */
            continue;
        }
        if (sidx && *naam)
            sym = zoek_symbool(naam);

        switch (type) {
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:
            if (!sym) { onopgelost++; if (rg_verbose) say("  onopgelost %s: %s", welk, naam); break; }
            *waar = (uint32_t)(uintptr_t)sym; slots++;
            break;
        case R_386_32:
            if (!sym) { onopgelost++; break; }
            *waar += (uint32_t)(uintptr_t)sym;
            break;
        case R_386_PC32:
            if (!sym) { onopgelost++; break; }
            *waar += (uint32_t)(uintptr_t)sym - (uint32_t)(uintptr_t)waar;
            break;
        case R_386_COPY:
            if (!sym) { onopgelost++; if (rg_verbose) say("  onopgelost COPY: %s", naam); break; }
            memcpy(waar, sym, d->symtab[sidx].st_size); copies++;
            break;
        default:
            say("  onbekend reloctype %u (%s) overgeslagen", type, naam);
            break;
        }
    }
    dbg("%s: %u toegepast, %u COPY, %u onopgelost", welk, slots, copies, onopgelost);
    if (onopgelost)
        say("LET OP: %u onopgeloste symbolen in %s", onopgelost, welk);
}

/* ------------------------------------------------------------------ de startstack + sprong */

extern char **environ;

/* Springt naar e_entry met %esp op de opgebouwde SysV-stack. Niets op de stack aanraken hierna. */
static void spring(uint32_t entry, uint32_t *esp)
{
    __asm__ volatile(
        "mov %0, %%esp\n\t"
        "xor %%edx, %%edx\n\t"   /* rtld_fini = NULL */
        "xor %%ebp, %%ebp\n\t"
        "jmp *%1\n\t"
        : : "r"(esp), "r"(entry) : "memory");
    __builtin_unreachable();
}

static void start_spel(struct beeld *b, int argc, char **argv)
{
    /* onze eigen auxv lezen (voor AT_RANDOM, AT_SYSINFO, AT_HWCAP, AT_PAGESZ enz.) */
    static uint32_t aux[64 * 2];
    int naux = 0;
    unsigned long sysinfo_ehdr = getauxval(AT_SYSINFO_EHDR);
    unsigned long sysinfo = getauxval(AT_SYSINFO);
    unsigned long random = getauxval(AT_RANDOM);
    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long clktck = getauxval(AT_CLKTCK);
    int envc = 0, i;
    uint32_t *stack, *sp, *base;
    size_t woorden;

    while (environ[envc]) envc++;

    /* stack opbouwen: [argc][argv..][0][envp..][0][auxv..][AT_NULL,0] */
    woorden = 1 + (argc + 1) + (envc + 1) + (32 * 2);
    base = mmap(NULL, 4u * 1024 * 1024, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (base == MAP_FAILED)
        fatal("kan startstack niet mappen");
    stack = base + (4u * 1024 * 1024 / 4);           /* top */
    /* esp 16-byte uitgelijnd op argc: reken terug */
    sp = stack - woorden;
    sp = (uint32_t *)((uintptr_t)sp & ~0xfu);

    {
        uint32_t *p = sp;
        *p++ = (uint32_t)argc;
        for (i = 0; i < argc; i++) *p++ = (uint32_t)(uintptr_t)argv[i];
        *p++ = 0;
        for (i = 0; i < envc; i++) *p++ = (uint32_t)(uintptr_t)environ[i];
        *p++ = 0;
#define AUX(t, v) do { aux[naux*2]=(t); aux[naux*2+1]=(uint32_t)(v); naux++; } while (0)
        AUX(AT_PHDR, b->phdr_vaddr);
        AUX(AT_PHENT, b->eh->e_phentsize);
        AUX(AT_PHNUM, b->eh->e_phnum);
        AUX(AT_PAGESZ, PAGE);
        AUX(AT_BASE, 0);            /* geen interp geladen */
        AUX(AT_FLAGS, 0);
        AUX(AT_ENTRY, b->eh->e_entry);
        AUX(AT_UID, getuid()); AUX(AT_EUID, geteuid());
        AUX(AT_GID, getgid()); AUX(AT_EGID, getegid());
        AUX(AT_SECURE, 0);
        if (clktck) AUX(AT_CLKTCK, clktck);
        if (hwcap) AUX(AT_HWCAP, hwcap);
        if (random) AUX(AT_RANDOM, random);
        if (sysinfo) AUX(AT_SYSINFO, sysinfo);
        if (sysinfo_ehdr) AUX(AT_SYSINFO_EHDR, sysinfo_ehdr);
        for (i = 0; i < naux; i++) { *p++ = aux[i*2]; *p++ = aux[i*2+1]; }
        *p++ = AT_NULL; *p++ = 0;
    }

    say("spring naar e_entry %#x (esp %p, argc %d, envc %d, %d auxv)",
        b->eh->e_entry, (void *)sp, argc, envc, naux);
    spring(b->eh->e_entry, sp);
}

int main(int argc, char **argv)
{
    const char *game;
    size_t flen;
    uint8_t *file;
    int fd;
    struct beeld b;
    struct dyninfo d;

    rg_verbose = getenv("RG_ELFLOADER_VERBOSE") != NULL;
    if (argc < 2)
        fatal("gebruik: rg-elfloader <game-elf> [args...]");
    game = argv[1];

    fd = open(game, O_RDONLY);
    if (fd < 0)
        fatal("kan %s niet openen", game);
    file = map_file(game, &flen);

    map_segments(fd, file, &b);
    lees_dyn(&b, &d);
    laad_needed(&b, &d);
    if (d.rel && d.relsz)
        pas_rel_toe(&d, d.rel, d.relsz / sizeof(Elf32_Rel), "DT_REL");
    if (d.jmprel && d.pltrelsz)
        pas_rel_toe(&d, d.jmprel, d.pltrelsz / sizeof(Elf32_Rel), "DT_JMPREL");

    munmap(file, flen);
    close(fd);

    /* Init draaien we NIET zelf: _start -> __libc_start_main -> __libc_csu_init draait DT_INIT en de
       init-array van het spel. De NEEDED-libs zijn al door dlopen geinitialiseerd. */
    say("geladen; spel %s krijgt argv[0]=%s", game, argv[1]);
    start_spel(&b, argc - 1, &argv[1]);
    return 0;
}
