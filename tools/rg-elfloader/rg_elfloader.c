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
    Elf32_Ehdr *eh;        /* ALLEEN geldig zolang het bestand gemapt is (map_segments); niet in start_spel! */
    Elf32_Phdr *ph;
    int phnum;
    uint32_t entry;        /* e_entry als losse waarde (overleeft munmap van het bestand) */
    uint16_t phent;        /* e_phentsize */
    uint32_t phdr_vaddr;   /* adres van de phdr-tabel in het geladen beeld (voor AT_PHDR) */
    uint32_t lo, hi;       /* geladen beeld-bereik (voor het valideren van GOT-slotadressen) */
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
    b->entry = eh->e_entry;       /* losse kopieen: b->eh wijst in het bestand, dat na munmap weg is */
    b->phent = eh->e_phentsize;
    b->lo = lo;
    b->hi = hi;

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
    uint32_t nchain;   /* aantal dynamische symbolen (uit DT_HASH) */
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
        case DT_HASH:         d->nchain = ((uint32_t *)dy->d_un.d_ptr)[1]; break;  /* nchain = #symbolen */
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

/* -------- lib-defs: welke symbolen definiëren de NEEDED-libs (uit hun BESTAND, zonder dlopen) --------
 * Voor de SMALLE shim: we exporteren alleen gast-symbolen die GEEN geladen lib zelf definieert, zodat we
 * de eigen (BSS-)globals van de libs (liballeg's _accel_active/_blender_* enz.) niet preempten. De libs
 * importeren echter gast-functies die in een data-tabel staan (R_386_GLOB_DAT), dus ze eisen de shim al
 * bij dlopen -- daarom moet de shim VÓÓR het laden klaar zijn, en lezen we de lib-definities hier uit het
 * bestand in plaats van via dlsym. Zie docs/research/eigen-elf-lader.md. */
static char rg_defbuf[1u << 20];
static uint32_t rg_defbuf_used;
static char *rg_defname[16384];
static int rg_defname_n;

static void def_add(const char *naam)
{
    size_t n = strlen(naam) + 1;
    if (rg_defname_n >= (int)(sizeof(rg_defname) / sizeof(rg_defname[0])) ||
        rg_defbuf_used + n > sizeof(rg_defbuf))
        return;
    memcpy(rg_defbuf + rg_defbuf_used, naam, n);
    rg_defname[rg_defname_n++] = rg_defbuf + rg_defbuf_used;
    rg_defbuf_used += (uint32_t)n;
}
static int def_cmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static int lib_defines(const char *naam)
{
    int lo = 0, hi = rg_defname_n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2, c = strcmp(rg_defname[m], naam);
        if (!c) return 1;
        if (c < 0) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

struct rg_load { uint32_t v, o, sz; };
static uint32_t v2o(struct rg_load *L, int nl, uint32_t va)
{
    int i;
    for (i = 0; i < nl; i++)
        if (va >= L[i].v && va < L[i].v + L[i].sz)
            return L[i].o + (va - L[i].v);
    return 0;
}

static void parse_lib_file(const char *pad)
{
    int fd = open(pad, O_RDONLY);
    struct stat st;
    uint8_t *m;
    Elf32_Ehdr *eh;
    Elf32_Phdr *ph;
    Elf32_Dyn *dyn = NULL, *dy;
    struct rg_load L[24];
    int i, nl = 0;
    uint32_t symv = 0, strv = 0, nchain = 0, so, to;
    size_t len;
    if (fd < 0) return;
    if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(Elf32_Ehdr)) { close(fd); return; }
    len = st.st_size;
    m = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return;
    eh = (Elf32_Ehdr *)m;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_ident[EI_CLASS] != ELFCLASS32 ||
        eh->e_machine != EM_386 || (size_t)eh->e_phoff + eh->e_phnum * sizeof(Elf32_Phdr) > len)
        { munmap(m, len); return; }
    ph = (Elf32_Phdr *)(m + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD && nl < 24) { L[nl].v = ph[i].p_vaddr; L[nl].o = ph[i].p_offset; L[nl].sz = ph[i].p_filesz; nl++; }
        if (ph[i].p_type == PT_DYNAMIC && (size_t)ph[i].p_offset < len) dyn = (Elf32_Dyn *)(m + ph[i].p_offset);
    }
    if (!dyn) { munmap(m, len); return; }
    for (dy = dyn; (uint8_t *)dy + sizeof(*dy) <= m + len && dy->d_tag != DT_NULL; dy++) {
        if (dy->d_tag == DT_SYMTAB) symv = dy->d_un.d_ptr;
        else if (dy->d_tag == DT_STRTAB) strv = dy->d_un.d_ptr;
        else if (dy->d_tag == DT_HASH) { uint32_t o = v2o(L, nl, dy->d_un.d_ptr); if (o + 8 <= len) nchain = *(uint32_t *)(m + o + 4); }
    }
    so = v2o(L, nl, symv);
    to = v2o(L, nl, strv);
    if (symv && strv && nchain && so && to && so + (size_t)nchain * sizeof(Elf32_Sym) <= len) {
        Elf32_Sym *syms = (Elf32_Sym *)(m + so);
        uint32_t k;
        for (k = 1; k < nchain; k++) {
            uint32_t na = syms[k].st_name;
            if (syms[k].st_shndx == SHN_UNDEF || !na || to + na >= len)
                continue;
            def_add((const char *)(m + to + na));
        }
    }
    munmap(m, len);
}

static void vind_en_parse(const char *naam)
{
    const char *ldp = getenv("LD_LIBRARY_PATH");
    static const char *stdd[] = {"/usr/lib32", "/usr/lib/i386-linux-gnu", "/lib/i386-linux-gnu", NULL};
    char pad[1024];
    int k;
    if (ldp && *ldp) {
        char buf[4096], *save, *dir;
        strncpy(buf, ldp, sizeof buf - 1);
        buf[sizeof buf - 1] = 0;
        for (dir = strtok_r(buf, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
            snprintf(pad, sizeof pad, "%s/%s", dir, naam);
            if (access(pad, R_OK) == 0) { parse_lib_file(pad); return; }
        }
    }
    for (k = 0; stdd[k]; k++) {
        snprintf(pad, sizeof pad, "%s/%s", stdd[k], naam);
        if (access(pad, R_OK) == 0) { parse_lib_file(pad); return; }
    }
}

static void verzamel_lib_defs(struct beeld *b, struct dyninfo *d)
{
    Elf32_Dyn *dy;
    for (dy = b->dyn; dy->d_tag != DT_NULL; dy++)
        if (dy->d_tag == DT_NEEDED)
            vind_en_parse(d->strtab + dy->d_un.d_val);
    qsort(rg_defname, rg_defname_n, sizeof rg_defname[0], def_cmp);
    dbg("lib-defs: %d gedefinieerde symbolen uit de NEEDED-libs gelezen", rg_defname_n);
}

/* Symbolen die de eigen CRT van de shim al definieert; die overslaan, anders "multiple definition". */
static int shim_skip(const char *naam)
{
    static const char *vast[] = {"_init", "_fini", "_start", "__bss_start", "_edata", "_end",
                                 "_GLOBAL_OFFSET_TABLE_", "_DYNAMIC", "__data_start", "data_start",
                                 "__dso_handle", "_IO_stdin_used", "__TMC_END__", "__gmon_start__",
                                 "__bss_start__", "_bss_end__", "__bss_end__", "__end__", NULL};
    int i;
    for (i = 0; vast[i]; i++)
        if (strcmp(naam, vast[i]) == 0)
            return 1;
    if (strncmp(naam, "__init_array_", 13) == 0 || strncmp(naam, "__fini_array_", 13) == 0)
        return 1;
    return 0;
}

/* De muur voor de Allegro-4-games: de gastbibliotheken (liballeg.so.4.0) importeren honderden symbolen
   UIT het spel-executable (de asm-scanline-routines zitten in de app). dlopen kan een met de hand
   gemapte ET_EXEC niet in de globale zoekruimte zetten, dus die imports zouden niet oplossen.
   Oplossing (bewezen door de eigenaar): een shim-bibliotheek met de GEDEFINIEERDE dynamische symbolen
   van de gast als ABSOLUTE adressen (`.set naam, 0xADDR`), die we RTLD_GLOBAL als EERSTE laden. De
   gast staat op een vast adres (ET_EXEC, 0x08048000), dus die adressen kloppen; de shim moet per gast
   opnieuw worden gemaakt. Zie docs/research/eigen-elf-lader.md. */
static void maak_shim(struct dyninfo *d)
{
    const char *tmp = getenv("XDG_RUNTIME_DIR");
    char dir[256], src[300], so[300], cmd[800];
    FILE *f;
    uint32_t i;
    int cnt = 0;
    const char *inc = getenv("RG_ELFLOADER_INC");   /* evt. pad met gnu/stubs-32.h; meestal niet nodig */

    if (!d->nchain)
        fatal("geen DT_HASH: kan het aantal gast-symbolen niet bepalen (shim)");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    snprintf(dir, sizeof dir, "%s/rg-elfloader-%d", tmp, (int)getpid());
    mkdir(dir, 0700);
    snprintf(src, sizeof src, "%s/shim.c", dir);
    snprintf(so, sizeof so, "%s/shim.so", dir);

    /* COPY-reloc-symbolen (screen, font, _blender_*, _accel_*) zijn gedeelde data-globals: de libs MOETEN
       de kopie van de gast gebruiken (ld.so-canonicalisatie), dus die blijven in de shim, ook al definieert
       een lib ze. Alleen lib-gedefinieerde NIET-COPY-symbolen (de paar code-botsingen) laten we weg. */
    const char *copyset[512];
    int ncopy = 0;
    if (d->rel && d->relsz) {
        uint32_t r, rn = d->relsz / sizeof(Elf32_Rel);
        for (r = 0; r < rn && ncopy < 512; r++) {
            uint32_t si;
            if (ELF32_R_TYPE(d->rel[r].r_info) != R_386_COPY)
                continue;
            si = ELF32_R_SYM(d->rel[r].r_info);
            if (si)
                copyset[ncopy++] = d->strtab + d->symtab[si].st_name;
        }
    }

    f = fopen(src, "w");
    if (!f)
        fatal("kan %s niet schrijven", src);
    for (i = 1; i < d->nchain; i++) {
        Elf32_Sym *s = &d->symtab[i];
        int bind = ELF32_ST_BIND(s->st_info);
        const char *naam = d->strtab + s->st_name;
        if (s->st_shndx == SHN_UNDEF || !s->st_name || !s->st_value)
            continue;
        if (s->st_shndx >= SHN_LORESERVE && s->st_shndx != SHN_ABS)
            continue;
        if (bind != STB_GLOBAL && bind != STB_WEAK)
            continue;
        if (shim_skip(naam))
            continue;
        /* SMALLE shim: een door een NEEDED-lib gedefinieerd symbool laten we weg (de lib gebruikt zijn
           eigen) -- TENZIJ het een COPY-reloc-doel is: dan moet de lib juist de gast-kopie delen. Zo
           verdwijnen de code-preempties maar blijven screen/font/_blender gedeeld, en worden de symbolen
           die de libs uit de gast nodig hebben (_poly_scanline_gcol8) geleverd. */
        if (lib_defines(naam)) {
            int c, is_copy = 0;
            for (c = 0; c < ncopy; c++)
                if (strcmp(copyset[c], naam) == 0) { is_copy = 1; break; }
            if (!is_copy)
                continue;
        }
        fprintf(f, "__asm__(\".globl %s\\n.set %s, 0x%x\\n\");\n", naam, naam, s->st_value);
        cnt++;
    }
    fclose(f);

    snprintf(cmd, sizeof cmd, "gcc -m32 -shared -fPIC -Wl,-z,execstack %s%s -o '%s' '%s' 2>&1",
             inc && *inc ? "-I" : "", inc && *inc ? inc : "", so, src);
    dbg("shim bouwen (%d gast-symbolen): %s", cnt, cmd);
    if (system(cmd) != 0)
        fatal("kon de shim niet bouwen: %s", cmd);
    if (!dlopen(so, RTLD_NOW | RTLD_GLOBAL))
        fatal("dlopen(shim) mislukt: %s", dlerror());
    say("shim geladen: %d gast-symbolen in de globale scope", cnt);
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
            if (!sym) { onopgelost++; if (*naam) say("  onopgelost %s: %s", welk, naam); break; }
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
            if (!sym) { onopgelost++; if (*naam) say("  onopgelost COPY: %s", naam); break; }
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

/* ------------------------------------------------------------------ gepakte klasse: eerste PLT repareren
 * De gepakte games (Galaga/JP/Pacman) hebben TWEE PLT/GOT-systemen (gemeten, eigenaar): de DT_JMPREL-
 * relocaties horen bij het tweede systeem (een tweede tabel, via %ebx), maar de stubs die de game
 * aanroept zitten in het EERSTE PLT, met absolute GOT-slots die nog de voorgevulde 0xb7xxxxxx-waarde van
 * de originele kast houden. Onze JMPREL-reparatie landt dus in de verkeerde tabel en de game crasht op
 * zo'n stale slot (bv. 0xb7508f60 = fopen).
 *
 * Statische naamterugwinning (methode eigenaar, re/_naamterugwinning.py): elke JMPREL-entry heeft een
 * symboolnaam; waar de tweede-tabel-inhoud een stale 0xb7-adres is, levert dat een map adres->naam.
 * Elk eerste-PLT-GOT-slot met datzelfde stale adres krijgt zo zijn naam. Voor Galaga: 31 van de 357
 * stubs, inclusief fopen. We lossen die op naam op (dlsym) en schrijven het echte adres in het
 * EERSTE-PLT-slot. De rest houdt zijn stale waarde tot de geheugendump er is. Veilig: we schrijven alleen
 * als de slotwaarde in de map staat; op ongepakte games is de map leeg en doet dit niets. */

struct staleref { uint32_t addr; const char *naam; };
static struct staleref rg_stale[1024];
static int rg_stale_n;

static void bouw_stale_map(struct dyninfo *d)
{
    uint32_t i, n;
    if (!d->jmprel || !d->pltrelsz)
        return;
    n = d->pltrelsz / sizeof(Elf32_Rel);
    for (i = 0; i < n; i++) {
        uint32_t sidx = ELF32_R_SYM(d->jmprel[i].r_info);
        uint32_t orig = *(volatile uint32_t *)(uintptr_t)d->jmprel[i].r_offset;  /* originele 2e-tabel-inhoud */
        const char *naam = sidx ? d->strtab + d->symtab[sidx].st_name : "";
        if (!*naam)
            continue;
        if (orig < 0xb7000000u || orig >= 0xb8000000u)   /* alleen stale kast-adressen */
            continue;
        if (rg_stale_n < (int)(sizeof(rg_stale) / sizeof(rg_stale[0]))) {
            rg_stale[rg_stale_n].addr = orig;
            rg_stale[rg_stale_n].naam = naam;
            rg_stale_n++;
        }
    }
    dbg("stale-map: %u JMPREL-entries, %d met stale 0xb7-adres + naam", n, rg_stale_n);
}

static const char *stale_naam(uint32_t v)
{
    int i;
    for (i = 0; i < rg_stale_n; i++)
        if (rg_stale[i].addr == v)
            return rg_stale[i].naam;
    return NULL;
}

/* Een gedeelde cdecl no-op (`push ebp; mov ebp,esp; xor eax,eax; pop ebp; ret`) die 0 teruggeeft en de
   stapel met rust laat -- exact wat ElfLdr2 in zijn onopgeloste slots zet (gemeten). Eén keer gemaakt. */
static uint32_t rg_trampoline(void)
{
    static uint8_t *tr;
    static const uint8_t noop[7] = {0x55, 0x8b, 0xec, 0x33, 0xc0, 0x5d, 0xc3};
    if (!tr) {
        tr = mmap(NULL, PAGE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (tr == MAP_FAILED) { tr = NULL; return 0; }
        memcpy(tr, noop, sizeof noop);
    }
    return (uint32_t)(uintptr_t)tr;
}

/* Scan de uitvoerbare segmenten naar eerste-PLT-stubs (`ff 25 <abs-slot>`), en herstel elk GOT-slot
   waarvan de waarde een stale adres uit de map is, naar dlsym(naam). b->ph wijst nog in het (nog niet
   ge-munmap'te) bestand. */
static void repareer_eerste_plt(struct beeld *b, struct dyninfo *d)
{
    int i, hersteld = 0, gemist = 0, tramp = 0;
    /* Trampoline standaard aan: een onopgelost 0xb7-slot zou een wilde sprong naar niet-gemapte ruimte
       zijn (crash). Een no-op is strikt beter en is wat ElfLdr2 doet. RG_ELF_NO_TRAMPOLINE=1 zet het uit. */
    int do_tramp = getenv("RG_ELF_NO_TRAMPOLINE") == NULL;
    uint32_t tr = do_tramp ? rg_trampoline() : 0;
    (void)d;
    if (!rg_stale_n)
        return;
    for (i = 0; i < b->phnum; i++) {
        const uint8_t *p, *eind;
        if (b->ph[i].p_type != PT_LOAD || !(b->ph[i].p_flags & PF_X))
            continue;
        p = (const uint8_t *)(uintptr_t)b->ph[i].p_vaddr;
        eind = p + b->ph[i].p_memsz - 6;
        for (; p < eind; p++) {
            uint32_t slot, val;
            const char *naam;
            void *sym;
            if (p[0] != 0xff || p[1] != 0x25)        /* jmp *disp32 (absolute, eerste PLT) */
                continue;
            slot = *(const uint32_t *)(p + 2);
            if (slot < b->lo || slot >= b->hi)       /* slot moet in het geladen beeld liggen */
                continue;
            val = *(volatile uint32_t *)(uintptr_t)slot;
            naam = stale_naam(val);
            if (naam) {
                sym = zoek_symbool(naam);
                if (!sym) { gemist++; if (rg_verbose) say("  eerste-PLT: %s niet gevonden (slot %#x)", naam, slot); continue; }
                *(volatile uint32_t *)(uintptr_t)slot = (uint32_t)(uintptr_t)sym;
                hersteld++;
                if (rg_verbose) say("  eerste-PLT: slot %#x (%#x) -> %s @ %p", slot, val, naam, sym);
            } else if (tr && val >= 0xb7000000u && val < 0xb8000000u) {
                /* onbenoemd stale slot: no-op i.p.v. wilde sprong (zoals ElfLdr2) */
                *(volatile uint32_t *)(uintptr_t)slot = tr;
                tramp++;
            }
        }
    }
    say("eerste-PLT: %d op naam hersteld, %d no-op-trampoline, %d naam-niet-gevonden (van %d stale-namen)",
        hersteld, tramp, gemist, rg_stale_n);
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
        AUX(AT_PHENT, b->phent);
        AUX(AT_PHNUM, b->phnum);
        AUX(AT_PAGESZ, PAGE);
        AUX(AT_BASE, 0);            /* geen interp geladen */
        AUX(AT_FLAGS, 0);
        AUX(AT_ENTRY, b->entry);
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
        b->entry, (void *)sp, argc, envc, naux);
    spring(b->entry, sp);
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
    verzamel_lib_defs(&b, &d);  /* uit de lib-BESTANDEN: welke symbolen definiëren zij (voor de smalle shim) */
    maak_shim(&d);        /* smalle shim: alleen gast-symbolen die geen lib definieert; zie maak_shim() */
    laad_needed(&b, &d);  /* libs laden (RTLD_NOW); hun gast-imports lossen tegen de shim op */
    bouw_stale_map(&d);   /* VOOR de JMPREL-pass: de originele stale 2e-tabel-waarden + namen vastleggen */
    if (d.rel && d.relsz)
        pas_rel_toe(&d, d.rel, d.relsz / sizeof(Elf32_Rel), "DT_REL");
    if (d.jmprel && d.pltrelsz)
        pas_rel_toe(&d, d.jmprel, d.pltrelsz / sizeof(Elf32_Rel), "DT_JMPREL");
    repareer_eerste_plt(&b, &d);   /* gepakte klasse: eerste-PLT-slots op naam herstellen (gemapt => veilig) */

    munmap(file, flen);
    close(fd);

    /* Init draaien we NIET zelf: _start -> __libc_start_main -> __libc_csu_init draait DT_INIT en de
       init-array van het spel. De NEEDED-libs zijn al door dlopen geinitialiseerd. */
    say("geladen; spel %s krijgt argv[0]=%s", game, argv[1]);
    start_spel(&b, argc - 1, &argv[1]);
    return 0;
}
