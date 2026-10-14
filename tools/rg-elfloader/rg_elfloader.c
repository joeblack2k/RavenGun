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
 * Analyseren zonder het spel te starten: rg-elfloader --ontleed-alleen <game> (alias --dry-run): doet
 * segmenten -> dynamic -> lib-defs -> shim -> NEEDED-libs -> relocaties, toont aantallen en stopt.
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
static int rg_ontleed;   /* --ontleed-alleen: analyseren, het spel NIET starten */

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
    uint32_t nchain;   /* aantal dynamische symbolen (DT_HASH, anders (STRTAB-SYMTAB)/16; zie tel_dynsyms) */
    int nchain_bron;   /* TEL_BRON_HASH of TEL_BRON_STRTAB_SYMTAB: waar nchain vandaan komt */
    Elf32_Rel *rel;    uint32_t relsz;
    Elf32_Rel *jmprel; uint32_t pltrelsz;
    uint32_t *init_array; uint32_t init_arraysz;
    void (*init)(void);
};

/* Het aantal dynamische symbolen bepalen. De ELF-spec geeft dat via DT_HASH (nchain), maar moderne
 * linkers (ld --hash-style=gnu, de standaard) leveren ALLEEN DT_GNU_HASH, en dat tag bevat geen
 * totaalaantal. Terugvaller: .dynstr staat bij GNU ld direct achter .dynsym, dus
 * (DT_STRTAB - DT_SYMTAB) / sizeof(Elf32_Sym) is het aantal symbolen (gemeten op een echte
 * GNU_HASH-only game: 1020 == readelf --dyn-syms). We raden NIET: als de afstand niet klopt (een tag
 * ontbreekt, STRTAB niet achter SYMTAB, of geen veelvoud van 16) is dat een duidelijke fout.
 * Geeft de bron (TEL_BRON_*) terug, of 0 met een foutmelding in *fout. Zuivere functie op de
 * tag-waarden (adressen/offsets), zodat hij los te testen is. */
enum { TEL_BRON_HASH = 1, TEL_BRON_STRTAB_SYMTAB = 2 };
#define RG_HEEFT_TEL_DYNSYMS 1   /* voor tests/: de functie bestaat (oudere versies hadden alleen DT_HASH) */

static int tel_dynsyms(uint32_t hash_nchain, uint32_t symtab, uint32_t strtab,
                       uint32_t *aantal, const char **fout)
{
    uint32_t afstand;
    *aantal = 0;
    if (hash_nchain) {
        *aantal = hash_nchain;
        return TEL_BRON_HASH;
    }
    if (!symtab || !strtab) {
        *fout = "geen DT_HASH en ook geen DT_SYMTAB+DT_STRTAB: kan het aantal gast-symbolen niet bepalen";
        return 0;
    }
    if (strtab <= symtab) {
        *fout = "geen DT_HASH en DT_STRTAB ligt niet achter DT_SYMTAB: "
                "aantal gast-symbolen niet af te leiden uit (STRTAB-SYMTAB)";
        return 0;
    }
    afstand = strtab - symtab;
    if (afstand % sizeof(Elf32_Sym) != 0) {
        *fout = "geen DT_HASH en (DT_STRTAB-DT_SYMTAB) is geen veelvoud van 16 (sizeof(Elf32_Sym)): "
                "aantal gast-symbolen niet betrouwbaar af te leiden";
        return 0;
    }
    *aantal = afstand / (uint32_t)sizeof(Elf32_Sym);
    return TEL_BRON_STRTAB_SYMTAB;
}

static void lees_dyn(struct beeld *b, struct dyninfo *d)
{
    Elf32_Dyn *dy;
    uint32_t hash_nchain = 0;
    const char *fout = "";
    memset(d, 0, sizeof(*d));
    for (dy = b->dyn; dy->d_tag != DT_NULL; dy++) {
        switch (dy->d_tag) {
        case DT_SYMTAB:       d->symtab = (Elf32_Sym *)dy->d_un.d_ptr; break;
        case DT_STRTAB:       d->strtab = (const char *)dy->d_un.d_ptr; break;
        case DT_HASH:         hash_nchain = ((uint32_t *)dy->d_un.d_ptr)[1]; break;  /* nchain = #symbolen */
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
    d->nchain_bron = tel_dynsyms(hash_nchain, (uint32_t)(uintptr_t)d->symtab, (uint32_t)(uintptr_t)d->strtab,
                                 &d->nchain, &fout);
    if (!d->nchain_bron)
        fatal("%s", fout);
    dbg("%u gast-symbolen (bron: %s)", d->nchain,
        d->nchain_bron == TEL_BRON_HASH ? "DT_HASH" : "(DT_STRTAB-DT_SYMTAB)/16, geen DT_HASH");
}

/* -------- lib-defs: welke symbolen definiëren de NEEDED-libs (uit hun BESTAND, zonder dlopen) --------
 * Voor de SMALLE shim: we exporteren alleen gast-symbolen die GEEN geladen lib zelf definieert, zodat we
 * de eigen (BSS-)globals van de libs (liballeg's _accel_active/_blender_* enz.) niet preempten. De libs
 * importeren echter gast-functies die in een data-tabel staan (R_386_GLOB_DAT), dus ze eisen de shim al
 * bij dlopen -- daarom moet de shim VÓÓR het laden klaar zijn, en lezen we de lib-definities hier uit het
 * bestand in plaats van via dlsym. Zie docs/research/eigen-elf-lader.md. */
/* Ruim boven de praktijk: libc + libGL + de overige moderne 32-bits systeemlibs leveren samen ~16k
 * symbolen via de STRTAB-SYMTAB-route (sinds de DT_GNU_HASH-terugval), dus 16384 was al te krap. 262144
 * namen en 8 MiB tekst geven ~16x marge. def_add() telt bovendien wat er tóch niet past en de lader
 * weigert dan te starten (zie verzamel_lib_defs): een stille afkap gaf anders een smalle shim die een
 * lib-symbool overschrijft -- precies het soort fout dat later onverklaarbaar is. */
static char rg_defbuf[1u << 23];
static uint32_t rg_defbuf_used;
static char *rg_defname[262144];
static int rg_defname_n;
static int rg_defname_overflow;  /* aantal namen dat niet paste (0 = alles paste) */

static void def_add(const char *naam)
{
    size_t n = strlen(naam) + 1;
    if (rg_defname_n >= (int)(sizeof(rg_defname) / sizeof(rg_defname[0])) ||
        rg_defbuf_used + n > sizeof(rg_defbuf)) {
        rg_defname_overflow++;
        return;
    }
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

/* Per gevonden bibliotheek wat we eruit lazen (voor --ontleed-alleen). */
struct libinfo { char naam[128]; char pad[512]; int gevonden; uint32_t nsym; int bron; };
static struct libinfo rg_libinfo[64];
static int rg_libinfo_n;

/* Leest de gedefinieerde symbolen van een lib uit het BESTAND. Geeft het aantal dynsyms terug (0 =
   onleesbaar) en zet *bron_uit (TEL_BRON_*). Heeft de lib alleen DT_GNU_HASH (geen DT_HASH), dan telt
   tel_dynsyms() via (STRTAB-SYMTAB)/16; vaddr-verschillen binnen een segment zijn gelijk aan
   bestands-verschillen, dus dat werkt hier ook. */
static uint32_t parse_lib_file(const char *pad, int *bron_uit)
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
    const char *fout = "";
    int bron;
    *bron_uit = 0;
    if (fd < 0) return 0;
    if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(Elf32_Ehdr)) { close(fd); return 0; }
    len = st.st_size;
    m = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return 0;
    eh = (Elf32_Ehdr *)m;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_ident[EI_CLASS] != ELFCLASS32 ||
        eh->e_machine != EM_386 || (size_t)eh->e_phoff + eh->e_phnum * sizeof(Elf32_Phdr) > len)
        { munmap(m, len); return 0; }
    ph = (Elf32_Phdr *)(m + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD && nl < 24) { L[nl].v = ph[i].p_vaddr; L[nl].o = ph[i].p_offset; L[nl].sz = ph[i].p_filesz; nl++; }
        if (ph[i].p_type == PT_DYNAMIC && (size_t)ph[i].p_offset < len) dyn = (Elf32_Dyn *)(m + ph[i].p_offset);
    }
    if (!dyn) { munmap(m, len); return 0; }
    for (dy = dyn; (uint8_t *)dy + sizeof(*dy) <= m + len && dy->d_tag != DT_NULL; dy++) {
        if (dy->d_tag == DT_SYMTAB) symv = dy->d_un.d_ptr;
        else if (dy->d_tag == DT_STRTAB) strv = dy->d_un.d_ptr;
        else if (dy->d_tag == DT_HASH) { uint32_t o = v2o(L, nl, dy->d_un.d_ptr); if (o + 8 <= len) nchain = *(uint32_t *)(m + o + 4); }
    }
    /* Geen DT_HASH (of een lege)? Val terug op (STRTAB-SYMTAB)/16. Een lib waarvan het aantal niet
       betrouwbaar af te leiden is slaan we over (zoals voorheen een lib zonder DT_HASH): geen gok. */
    bron = tel_dynsyms(nchain, symv, strv, &nchain, &fout);
    so = v2o(L, nl, symv);
    to = v2o(L, nl, strv);
    if (bron && so && to && so + (size_t)nchain * sizeof(Elf32_Sym) <= len) {
        Elf32_Sym *syms = (Elf32_Sym *)(m + so);
        uint32_t k;
        for (k = 1; k < nchain; k++) {
            uint32_t na = syms[k].st_name;
            if (syms[k].st_shndx == SHN_UNDEF || !na || to + na >= len)
                continue;
            def_add((const char *)(m + to + na));
        }
        *bron_uit = bron;
    } else {
        nchain = 0;
    }
    munmap(m, len);
    return nchain;
}

static void parse_en_noteer(const char *naam, const char *pad)
{
    struct libinfo *li = NULL;
    if (rg_libinfo_n < (int)(sizeof(rg_libinfo) / sizeof(rg_libinfo[0]))) {
        li = &rg_libinfo[rg_libinfo_n++];
        snprintf(li->naam, sizeof li->naam, "%s", naam);
        snprintf(li->pad, sizeof li->pad, "%s", pad ? pad : "");
        li->gevonden = pad != NULL;
    }
    if (pad) {
        int bron = 0;
        uint32_t n = parse_lib_file(pad, &bron);
        if (li) { li->nsym = n; li->bron = bron; }
    }
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
            if (access(pad, R_OK) == 0) { parse_en_noteer(naam, pad); return; }
        }
    }
    for (k = 0; stdd[k]; k++) {
        snprintf(pad, sizeof pad, "%s/%s", stdd[k], naam);
        if (access(pad, R_OK) == 0) { parse_en_noteer(naam, pad); return; }
    }
    parse_en_noteer(naam, NULL);   /* niet gevonden in LD_LIBRARY_PATH/standaardmappen (dlopen zoekt zelf verder) */
}

static void verzamel_lib_defs(struct beeld *b, struct dyninfo *d)
{
    Elf32_Dyn *dy;
    for (dy = b->dyn; dy->d_tag != DT_NULL; dy++)
        if (dy->d_tag == DT_NEEDED)
            vind_en_parse(d->strtab + dy->d_un.d_val);
    qsort(rg_defname, rg_defname_n, sizeof rg_defname[0], def_cmp);
    dbg("lib-defs: %d gedefinieerde symbolen uit de NEEDED-libs gelezen", rg_defname_n);
    if (rg_defname_overflow) {
        /* De lijst was vol en lib_defines() mist nu symbolen, dus de smalle shim kan er exporteren die een
         * lib ook definieert. Een spel starten met een shim waarvan we weten dat hij niet klopt doen we
         * niet: een waarschuwing op stderr verdwijnt in een log en de crash daarna wijst nergens naar.
         * Alleen --ontleed-alleen gaat door (dat start niets) en herhaalt het in zijn samenvatting.
         * Verhoog rg_defname[] en rg_defbuf[], of zoek uit waarom er zoveel libs meedoen. */
        if (!rg_ontleed)
            fatal("lib-defs-lijst vol, %d symbolen weggevallen (van %d+%d); verhoog "
                  "rg_defname[]/rg_defbuf[]", rg_defname_overflow, rg_defname_n, rg_defname_overflow);
        fprintf(stderr, "rg-elfloader: WAARSCHUWING: lib-defs-lijst vol, %d symbolen weggevallen "
                "(van %d+%d); verhoog rg_defname[]/rg_defbuf[]\n",
                rg_defname_overflow, rg_defname_n, rg_defname_overflow);
    }
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

static int rg_shim_symbolen;   /* aantal geexporteerde gast-symbolen in de shim (voor --ontleed-alleen) */

/* De muur voor de Allegro-4-games: de gastbibliotheken (liballeg.so.4.0) importeren honderden symbolen
   UIT het spel-executable (de asm-scanline-routines zitten in de app). dlopen kan een met de hand
   gemapte ET_EXEC niet in de globale zoekruimte zetten, dus die imports zouden niet oplossen.
   Oplossing (bewezen door de eigenaar): een shim-bibliotheek met de GEDEFINIEERDE dynamische symbolen
   van de gast als ABSOLUTE adressen (`.set naam, 0xADDR`), die we RTLD_GLOBAL als EERSTE laden. De
   gast staat op een vast adres (ET_EXEC, 0x08048000), dus die adressen kloppen; de shim moet per gast
   opnieuw worden gemaakt. Zie docs/research/eigen-elf-lader.md. */
/* De shim wordt met gcc gebouwd, en dat zijn 64-bits hulpprocessen (sh, gcc, cc1, as, collect2, ld). Een
   LD_PRELOAD of LD_AUDIT die voor het SPEL bedoeld is (een 32-bits bibliotheek, bv. de HASP-hook) erven
   zij ook, en elk ervan meldt dan "ERROR: ld.so: object ... cannot be preloaded (wrong ELF class:
   ELFCLASS32): ignored" -- zes regels ruis per start. In ons eigen proces is de preload al geladen toen
   het begon; de variabele hoeft dus alleen even uit de omgeving terwijl de hulpprocessen starten, en
   komt daarna ongewijzigd terug (de gast en wat hij zelf start zien wat ze altijd zagen). */
static int bouw_zonder_preload(const char *cmd)
{
    static const char *vars[] = {"LD_PRELOAD", "LD_AUDIT"};
    char *bewaard[2] = {NULL, NULL};
    int i, rc;
    for (i = 0; i < 2; i++) {
        const char *v = getenv(vars[i]);
        if (v) {
            bewaard[i] = strdup(v);
            unsetenv(vars[i]);
        }
    }
    rc = system(cmd);
    for (i = 0; i < 2; i++)
        if (bewaard[i]) {
            setenv(vars[i], bewaard[i], 1);
            free(bewaard[i]);
        }
    return rc;
}

static void maak_shim(struct dyninfo *d)
{
    const char *tmp = getenv("XDG_RUNTIME_DIR");
    char dir[256], src[300], so[300], cmd[800];
    FILE *f;
    uint32_t i;
    int cnt = 0;
    const char *inc = getenv("RG_ELFLOADER_INC");   /* evt. pad met gnu/stubs-32.h; meestal niet nodig */

    if (!d->nchain)   /* lees_dyn() heeft het aantal al bepaald of afgebroken; dit is een vangnet */
        fatal("aantal gast-symbolen onbekend (shim)");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    snprintf(dir, sizeof dir, "%s/rg-elfloader-%d", tmp, (int)getpid());
    mkdir(dir, 0700);
    snprintf(src, sizeof src, "%s/shim.c", dir);
    snprintf(so, sizeof so, "%s/shim.so", dir);

    /* COPY-reloc-doelen (screen, font, _blender_*, _rgb_*, ...) BLIJVEN in de shim: zij moeten gedeeld zijn
       zodat een runtime-schrijf door de lib (bv. set_gfx_mode zet `screen`) de gast-kopie bereikt. De
       beginwaarde komt los via de COPY-relocatie (uit de lib, post-reloc; zie pas_rel_toe/zoek_in_libs).
       Alleen lib-gedefinieerde NIET-COPY-symbolen (de code-botsingen) laten we weg. */
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
        if (lib_defines(naam)) {
            int c, is_copy = 0;
            for (c = 0; c < ncopy; c++)
                if (strcmp(copyset[c], naam) == 0) { is_copy = 1; break; }
            if (!is_copy)
                continue;   /* lib-gedefinieerd en geen COPY -> de lib houdt zijn eigen (geen preemptie) */
        }
        fprintf(f, "__asm__(\".globl %s\\n.set %s, 0x%x\\n\");\n", naam, naam, s->st_value);
        cnt++;
    }
    fclose(f);

    snprintf(cmd, sizeof cmd, "gcc -m32 -shared -fPIC -Wl,-z,execstack %s%s -o '%s' '%s' 2>&1",
             inc && *inc ? "-I" : "", inc && *inc ? inc : "", so, src);
    dbg("shim bouwen (%d gast-symbolen): %s", cnt, cmd);
    if (bouw_zonder_preload(cmd) != 0)
        fatal("kon de shim niet bouwen: %s", cmd);
    if (!dlopen(so, RTLD_NOW | RTLD_GLOBAL))
        fatal("dlopen(shim) mislukt: %s", dlerror());
    rg_shim_symbolen = cnt;
    if (rg_ontleed) {   /* analyse laat geen tijdelijke bestanden achter; de shim blijft geldig na unlink */
        unlink(src);
        unlink(so);
        rmdir(dir);
    }
    say("shim geladen: %d gast-symbolen in de globale scope", cnt);
}

/* De benodigde bibliotheken laden. libc.so.6/libm.so.6 zijn al in dit proces; dlopen geeft dan het
   bestaande handvat terug. De oude libs (libstdc++.so.5 enz.) komen via LD_LIBRARY_PATH uit
   ElfLdr2/libs. RTLD_GLOBAL zodat hun symbolen in de globale zoekruimte komen (voor R_386_COPY). */
static void *rg_libs[64];   /* handvatten van de NEEDED-libs, voor R_386_COPY (de DEFINIËRENDE lib) */
static int rg_libs_n;

/* De laadorde. De dynamische linker van het systeem zet eerst ALLE DT_NEEDED-libs van het programma in
   de globale zoekruimte en past daarna pas de relocaties toe; een lib mag dus leunen op een lib die
   verderop in de lijst staat, ook zonder er zelf een DT_NEEDED voor te hebben. Dat komt voor: libGLU.so.1
   uit ElfLdr2/libs heeft geen enkele DT_NEEDED en gebruikt `floor` (libm) en `operator new[]` (libstdc++),
   en libtheora leunt op libogg. Wij laden met dlopen een voor een, dus zo'n lib faalt bij de eerste
   poging ("undefined symbol: floor") omdat zijn leverancier nog niet geladen is.
   Daarom: de DT_NEEDED-volgorde aflopen, een lib die niet laadt apart zetten, en die opnieuw proberen
   zolang een ronde iets oplevert. Geen lijst van "systeemlibs eerst": welke lib op welke leunt staat
   nergens, en de volgorde blijft voor alles wat meteen laadt die van het spel. Beide modi lopen
   dezelfde lus; alleen het einde verschilt: de gewone modus stopt als er een lib overblijft,
   --ontleed-alleen meldt hem en gaat door (dat is juist wat de analyse moet laten zien).
   Grens: de zoekvolgorde van de globale scope is de LAADvolgorde, dus een lib uit een latere ronde komt
   achteraan. Definieren twee libs hetzelfde symbool, dan kan een andere winnen dan bij het systeem. */
static char rg_nietgeladen[16][400];
static int rg_nietgeladen_n;
static uint32_t rg_nietgeladen_totaal;
static char rg_laat[16][96];        /* libs die pas in een latere ronde laadden, met de fout van de eerste poging */
static char rg_laat_fout[16][300];
static int rg_laat_n;
static uint32_t rg_laat_totaal, rg_needed_totaal;

static void laad_needed(struct beeld *b, struct dyninfo *d)
{
    Elf32_Dyn *dy;
    const char *wacht[64];
    static char eerste_fout[64][300];
    int nw = 0, i, voortgang;
    for (dy = b->dyn; dy->d_tag != DT_NULL; dy++) {
        const char *naam;
        void *h;
        if (dy->d_tag != DT_NEEDED)
            continue;
        rg_needed_totaal++;
        naam = d->strtab + dy->d_un.d_val;
        h = dlopen(naam, RTLD_NOW | RTLD_GLOBAL);   /* RTLD_NOW past ook liballeg's eigen relocaties toe */
        if (!h) {
            const char *fout = dlerror();
            if (nw >= (int)(sizeof(wacht) / sizeof(wacht[0])))
                fatal("dlopen(%s) mislukt en de wachtlijst is vol: %s", naam, fout ? fout : "?");
            snprintf(eerste_fout[nw], sizeof eerste_fout[0], "%s", fout ? fout : "dlopen mislukt");
            wacht[nw++] = naam;
            dbg("nog niet geladen (volgende ronde): %s: %s", naam, eerste_fout[nw - 1]);
            continue;
        }
        if (rg_libs_n < (int)(sizeof(rg_libs) / sizeof(rg_libs[0])))
            rg_libs[rg_libs_n++] = h;
        dbg("geladen: %s", naam);
    }
    do {   /* nog een ronde zolang er voortgang is */
        voortgang = 0;
        for (i = 0; i < nw; i++) {
            void *h;
            if (!wacht[i])
                continue;
            h = dlopen(wacht[i], RTLD_NOW | RTLD_GLOBAL);
            if (!h)
                continue;
            if (rg_libs_n < (int)(sizeof(rg_libs) / sizeof(rg_libs[0])))
                rg_libs[rg_libs_n++] = h;
            say("geladen in een latere ronde: %s (eerste poging: %s)", wacht[i], eerste_fout[i]);
            rg_laat_totaal++;
            if (rg_laat_n < (int)(sizeof(rg_laat) / sizeof(rg_laat[0]))) {
                snprintf(rg_laat[rg_laat_n], sizeof rg_laat[0], "%s", wacht[i]);
                snprintf(rg_laat_fout[rg_laat_n++], sizeof rg_laat_fout[0], "%.299s", eerste_fout[i]);
            }
            wacht[i] = NULL;
            voortgang = 1;
        }
    } while (voortgang);
    for (i = 0; i < nw; i++) {
        const char *fout;
        void *h;
        if (!wacht[i])
            continue;
        h = dlopen(wacht[i], RTLD_NOW | RTLD_GLOBAL);   /* nog een keer, alleen voor de fout van DEZE lib nu */
        if (h) {                                        /* kan niet na een ronde zonder voortgang, maar dan is hij er */
            if (rg_libs_n < (int)(sizeof(rg_libs) / sizeof(rg_libs[0])))
                rg_libs[rg_libs_n++] = h;
            continue;
        }
        fout = dlerror();
        if (!rg_ontleed)
            fatal("dlopen(%s) mislukt, ook nadat de andere bibliotheken geladen zijn: %s", wacht[i],
                  fout ? fout : eerste_fout[i]);
        rg_nietgeladen_totaal++;
        if (rg_nietgeladen_n < (int)(sizeof(rg_nietgeladen) / sizeof(rg_nietgeladen[0])))
            snprintf(rg_nietgeladen[rg_nietgeladen_n++], sizeof rg_nietgeladen[0], "%.90s: %.299s", wacht[i],
                     fout ? fout : eerste_fout[i]);
    }
}

static void *zoek_symbool(const char *naam)
{
    void *a = dlsym(RTLD_DEFAULT, naam);
    return a;   /* RTLD_DEFAULT doorzoekt de globale scope incl. onze RTLD_GLOBAL-dlopens + libc/libm */
}

/* De DEFINIËRENDE lib van een symbool (NIET de gast/shim). Voor R_386_COPY: de beginwaarde moet uit de
   lib komen NÁ diens eigen relocaties (bv. liballeg's `font` wordt pas &_default_font door een R_386_32
   bij dlopen). dlsym(RTLD_DEFAULT) zou via de shim de lege gast-kopie teruggeven en zichzelf kopiëren. */
static void *zoek_in_libs(const char *naam)
{
    int i;
    for (i = 0; i < rg_libs_n; i++) {
        void *a = dlsym(rg_libs[i], naam);
        if (a)
            return a;
    }
    return NULL;
}

/* Boekhouding van toegepaste relocaties, voor --ontleed-alleen: totaal per soort + de eerste paar. */
struct relrec { const char *welk; uint32_t type, waar; const char *naam; uint32_t waarde; };
static struct relrec rg_relrec[12];
static int rg_relrec_n;
static uint32_t rg_rel_toegepast, rg_rel_relatief, rg_rel_onopgelost;
/* Een zwak ongedefinieerd symbool (__gmon_start__, _Jv_RegisterClasses) hoort onopgelost te blijven; een
   sterk niet. Alleen het totaal tonen laat een sterk gemis schuilgaan achter de onschuldige. */
static uint32_t rg_rel_onopgelost_zwak;

/* Naar WELKE module wees elke toegepaste relocatie (--ontleed-alleen). "Toegepast" zegt alleen dat er
   een adres is ingevuld, niet wiens adres: toen de lader nog met -rdynamic gebouwd werd loste het door de
   gast geimporteerde `main` op naar de main van de lader zelf, en ook dat telde als toegepast. Een adres
   zonder module erbij is dus geen bewijs (dezelfde les als het terugkeeradres in de exit-trace,
   docs/research/luigismansion-exit-trace.md). */
struct relmod { char naam[160]; uint32_t n; };
static struct relmod rg_relmod[48];
static int rg_relmod_n;
static uint32_t rg_relmod_rest;             /* doelen in modules die niet meer in de tabel pasten */
static uint32_t rg_beeld_lo, rg_beeld_hi;   /* het gemapte gastbeeld */

static void relmod_tel(const void *doel)
{
    Dl_info di, zelf;
    const char *m;
    uint32_t a = (uint32_t)(uintptr_t)doel;
    int i;
    if (a >= rg_beeld_lo && a < rg_beeld_hi)
        m = "(de gast zelf, via de shim)";
    else if (!dladdr(doel, &di) || !di.dli_fname || !*di.dli_fname)
        m = "(buiten elke module)";
    else if (dladdr((void *)(uintptr_t)relmod_tel, &zelf) && zelf.dli_fbase == di.dli_fbase)
        m = "(DE LADER ZELF)";
    else
        m = di.dli_fname;
    for (i = 0; i < rg_relmod_n; i++)
        if (strcmp(rg_relmod[i].naam, m) == 0) { rg_relmod[i].n++; return; }
    if (rg_relmod_n >= (int)(sizeof(rg_relmod) / sizeof(rg_relmod[0]))) { rg_relmod_rest++; return; }
    snprintf(rg_relmod[rg_relmod_n].naam, sizeof rg_relmod[0].naam, "%s", m);
    rg_relmod[rg_relmod_n++].n = 1;
}

static void rel_noteer(const char *welk, uint32_t type, uint32_t *waar, const char *naam, uint32_t waarde,
                       const void *doel)
{
    rg_rel_toegepast++;
    if (rg_ontleed)
        relmod_tel(doel);
    if (rg_relrec_n < (int)(sizeof(rg_relrec) / sizeof(rg_relrec[0]))) {
        struct relrec *x = &rg_relrec[rg_relrec_n++];
        x->welk = welk; x->type = type; x->waar = (uint32_t)(uintptr_t)waar; x->naam = naam; x->waarde = waarde;
    }
}

static const char *reltype_naam(uint32_t t)
{
    switch (t) {
    case R_386_32: return "R_386_32";
    case R_386_PC32: return "R_386_PC32";
    case R_386_GLOB_DAT: return "R_386_GLOB_DAT";
    case R_386_JMP_SLOT: return "R_386_JMP_SLOT";
    case R_386_COPY: return "R_386_COPY";
    case R_386_RELATIVE: return "R_386_RELATIVE";
    default: return "R_386_?";
    }
}

/* Gewone modus: elke onopgeloste naam meteen melden (zoals altijd). --ontleed-alleen: er kunnen er
   honderden zijn als een lib ontbreekt; dan bewaren we de eerste paar voor de samenvatting. */
static char rg_onopgelost_namen[10][96];
static int rg_onopgelost_namen_n;
static char rg_onopgelost_zwak_namen[10][96];   /* apart, zodat zwakke namen de sterke niet verdringen */
static int rg_onopgelost_zwak_namen_n;

static void onopgelost_noteer(const char *welk, const char *naam, int zwak)
{
    if (!rg_ontleed) {
        say("  onopgelost %s: %s%s", welk, naam, zwak ? " (zwak)" : "");
        return;
    }
    if (zwak) {
        if (rg_onopgelost_zwak_namen_n < (int)(sizeof(rg_onopgelost_zwak_namen) / sizeof(rg_onopgelost_zwak_namen[0])))
            snprintf(rg_onopgelost_zwak_namen[rg_onopgelost_zwak_namen_n++], sizeof rg_onopgelost_zwak_namen[0],
                     "%s %s", welk, naam);
        return;
    }
    if (rg_onopgelost_namen_n < (int)(sizeof(rg_onopgelost_namen) / sizeof(rg_onopgelost_namen[0])))
        snprintf(rg_onopgelost_namen[rg_onopgelost_namen_n++], sizeof rg_onopgelost_namen[0], "%s %s", welk, naam);
}

static void pas_rel_toe(struct dyninfo *d, Elf32_Rel *r, uint32_t n, const char *welk)
{
    uint32_t i;
    uint32_t onopgelost = 0, zwakke = 0, copies = 0, slots = 0;
    for (i = 0; i < n; i++) {
        uint32_t type = ELF32_R_TYPE(r[i].r_info);
        uint32_t sidx = ELF32_R_SYM(r[i].r_info);
        uint32_t *waar = (uint32_t *)r[i].r_offset;
        const char *naam = sidx ? d->strtab + d->symtab[sidx].st_name : "";
        int zwak = sidx && ELF32_ST_BIND(d->symtab[sidx].st_info) == STB_WEAK;
        void *sym = NULL;

        if (type == R_386_RELATIVE) {
            /* basis = 0 voor ET_EXEC; *waar blijft gelijk. */
            rg_rel_relatief++;
            continue;
        }
        if (sidx && *naam)
            sym = zoek_symbool(naam);

        switch (type) {
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:
            if (!sym) { onopgelost++; zwakke += zwak; if (*naam) onopgelost_noteer(welk, naam, zwak); break; }
            *waar = (uint32_t)(uintptr_t)sym; slots++;
            rel_noteer(welk, type, waar, naam, *waar, sym);
            break;
        case R_386_32:
            if (!sym) { onopgelost++; zwakke += zwak; if (*naam) onopgelost_noteer(welk, naam, zwak); break; }
            *waar += (uint32_t)(uintptr_t)sym;
            rel_noteer(welk, type, waar, naam, *waar, sym);
            break;
        case R_386_PC32:
            if (!sym) { onopgelost++; zwakke += zwak; if (*naam) onopgelost_noteer(welk, naam, zwak); break; }
            *waar += (uint32_t)(uintptr_t)sym - (uint32_t)(uintptr_t)waar;
            rel_noteer(welk, type, waar, naam, *waar, sym);
            break;
        case R_386_COPY: {
            /* De bron MOET de DEFINIËRENDE lib zijn (post-reloc), niet de shim/gast -- anders kopieert
               hij de lege gast-kopie op zichzelf. Zie zoek_in_libs(). */
            void *src = zoek_in_libs(naam);
            if (!src) { onopgelost++; zwakke += zwak; if (*naam) onopgelost_noteer("COPY", naam, zwak); break; }
            memcpy(waar, src, d->symtab[sidx].st_size); copies++;
            rel_noteer(welk, type, waar, naam, (uint32_t)(uintptr_t)src, src);
        }
            break;
        default:
            say("  onbekend reloctype %u (%s) overgeslagen", type, naam);
            break;
        }
    }
    rg_rel_onopgelost += onopgelost;
    rg_rel_onopgelost_zwak += zwakke;
    dbg("%s: %u toegepast, %u COPY, %u onopgelost", welk, slots, copies, onopgelost);
    if (onopgelost && !rg_ontleed)
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

static int rg_slottabel_gelezen;   /* gezet door pas_slottabel_toe (verderop) */

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
    /* Met een slottabel hoort er niets meer op de no-op te staan: een stil nul-antwoord is dan een gat in de tabel. */
    if (rg_slottabel_gelezen && tramp)
        say("LET OP: slottabel gelezen en toch %d stale slot(s) op de no-op-trampoline: de tabel dekt dit beeld niet volledig", tramp);
}


/* Slottabel per game (gepakte klasse). Bij een image dat uit een draaiende kast is gedumpt staan de namen van de
 * eerste-PLT-imports niet meer in het bestand: elke stub pusht 0xdeadbeef en elk GOT-slot houdt het adres dat de functie
 * op DE KAST had. De stale-map hierboven vindt alleen de namen die toevallig ook in de tweede tabel staan (Jurassic Park:
 * 33 van de 577). De rest is kennis van die ene game-versie en hoort dus als DATA bij de game (patches/<Game>/), niet in
 * deze code: een TSV met per regel  stub <tab> slot <tab> stale <tab> naam [<tab> ...]  (hex; '#'-regels en de kopregel
 * worden overgeslagen). RG_ELF_SLOT_TABLE wijst het bestand aan; zonder die variabele doet dit niets.
 *
 * Veilig: een slot wordt alleen geschreven als het NOG de stale waarde uit de tabel bevat. Een tabel van een andere
 * versie van het spel raakt zo niets, en een slot dat JMPREL al gezet heeft (fork) blijft staan. Een naam die geen
 * enkele geladen bibliotheek kent wordt gemeld en overgeslagen; dat slot valt daarna onder repareer_eerste_plt. */
static int rg_slottabel_gelezen = 0;
static void pas_slottabel_toe(struct beeld *b)
{
    const char *pad = getenv("RG_ELF_SLOT_TABLE");
    FILE *f; char regel[2048];
    int ok = 0, nietgevonden = 0, anders = 0, buiten = 0, zwak = 0;
    if (!pad || !*pad) return;
    f = fopen(pad, "r");
    if (!f) fatal("kan slottabel %s niet openen", pad);
    while (fgets(regel, sizeof regel, f)) {
        unsigned stub, slot, stale; char naam[512]; void *sym; uint32_t nu;
        if (regel[0] == '#' || sscanf(regel, "%x\t%x\t%x\t%511[^\t\n]", &stub, &slot, &stale, naam) != 4) continue;
        if (slot < b->lo || slot + 4 > b->hi) { buiten++; continue; }
        nu = *(volatile uint32_t *)(uintptr_t)slot;
        if (stale == 0) { zwak++; continue; }                  /* zwak (__gmon_start__): blijft 0 */
        if (nu != stale) { anders++; continue; }               /* al door JMPREL gezet (fork) of een andere game-versie */
        sym = zoek_symbool(naam);
        if (!sym) { nietgevonden++; say("  slottabel: %s niet gevonden (slot %#x)", naam, slot); continue; }
        *(volatile uint32_t *)(uintptr_t)slot = (uint32_t)(uintptr_t)sym;
        if (rg_ontleed) relmod_tel(sym);
        ok++;
    }
    fclose(f);
    rg_slottabel_gelezen = 1;
    say("slottabel %s: %d slots op naam gezet, %d naam-niet-gevonden, %d slot-had-andere-waarde, %d zwak/0, %d buiten beeld",
        pad, ok, nietgevonden, anders, zwak, buiten);
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

/* Samenvatting voor --ontleed-alleen: wat de lader gezien heeft, zonder het spel te starten. */
static void toon_ontleding(struct dyninfo *d, const char *game)
{
    int i;
    printf("ontleed-alleen: %s\n", game);
    printf("gast-dynsyms: %u (bron: %s)\n", d->nchain,
           d->nchain_bron == TEL_BRON_HASH ? "DT_HASH" : "(DT_STRTAB-DT_SYMTAB)/16, geen DT_HASH");
    printf("shim geladen: %d van de %u gast-symbolen in de globale scope\n", rg_shim_symbolen, d->nchain);
    printf("bibliotheken (%d):\n", rg_libinfo_n);
    for (i = 0; i < rg_libinfo_n; i++) {
        struct libinfo *li = &rg_libinfo[i];
        if (!li->gevonden)
            printf("  %-28s niet gevonden in LD_LIBRARY_PATH/standaardmappen (dlopen zoekt zelf)\n", li->naam);
        else if (!li->bron)
            printf("  %-28s %s: dynsym-aantal niet af te leiden, defs overgeslagen\n", li->naam, li->pad);
        else
            printf("  %-28s %s: %u dynsyms (%s)\n", li->naam, li->pad, li->nsym,
                   li->bron == TEL_BRON_HASH ? "DT_HASH" : "STRTAB-SYMTAB");
    }
    printf("geladen: %u van de %u bibliotheken, waarvan %u pas in een latere ronde\n",
           rg_needed_totaal - rg_nietgeladen_totaal, rg_needed_totaal, rg_laat_totaal);
    for (i = 0; i < rg_laat_n; i++)
        printf("  latere ronde: %s (eerste poging: %s)\n", rg_laat[i], rg_laat_fout[i]);
    if (rg_nietgeladen_totaal) {
        printf("NIET geladen (%u):\n", rg_nietgeladen_totaal);
        for (i = 0; i < rg_nietgeladen_n; i++)
            printf("  %s\n", rg_nietgeladen[i]);
    }
    printf("lib-defs: %d gedefinieerde symbolen uit de NEEDED-libs\n", rg_defname_n);
    if (rg_defname_overflow)
        printf("lib-defs: LIJST VOL, %d symbolen weggevallen; de shim hieronder is niet betrouwbaar en "
               "de gewone modus weigert te starten\n", rg_defname_overflow);
    printf("relocaties: %u toegepast, %u RELATIVE (ongewijzigd), %u onopgelost (%u zwak, %u sterk)\n",
           rg_rel_toegepast, rg_rel_relatief, rg_rel_onopgelost, rg_rel_onopgelost_zwak,
           rg_rel_onopgelost - rg_rel_onopgelost_zwak);
    printf("relocatiedoelen per module:\n");
    for (i = 0; i < rg_relmod_n; i++)
        printf("  %6u  %s\n", rg_relmod[i].n, rg_relmod[i].naam);
    if (rg_relmod_rest)
        printf("  %6u  (in modules die niet meer in de tabel pasten)\n", rg_relmod_rest);
    if (rg_onopgelost_namen_n) {
        printf("eerste onopgeloste symbolen, STERK (de gast kan hierop stuklopen):\n");
        for (i = 0; i < rg_onopgelost_namen_n; i++)
            printf("  %s\n", rg_onopgelost_namen[i]);
    }
    if (rg_onopgelost_zwak_namen_n) {
        printf("eerste onopgeloste symbolen, zwak (mogen leeg blijven):\n");
        for (i = 0; i < rg_onopgelost_zwak_namen_n; i++)
            printf("  %s\n", rg_onopgelost_zwak_namen[i]);
    }
    printf("eerste toegepaste relocaties:\n");
    for (i = 0; i < rg_relrec_n; i++) {
        struct relrec *x = &rg_relrec[i];
        printf("  %-9s %-14s @%#010x %-32s -> %#x\n", x->welk, reltype_naam(x->type), x->waar,
               x->naam, x->waarde);
    }
    printf("spel NIET gestart (--ontleed-alleen)\n");
    fflush(stdout);
}

#ifndef RG_ELFLOADER_ZONDER_MAIN   /* tests/ kan dit bestand zo includen om losse functies te testen */
int main(int argc, char **argv)
{
    const char *game;
    size_t flen;
    uint8_t *file;
    int fd;
    struct beeld b;
    struct dyninfo d;

    rg_verbose = getenv("RG_ELFLOADER_VERBOSE") != NULL;
    /* --ontleed-alleen (alias --dry-run): alles doen t/m de relocaties, de samenvatting tonen en stoppen
       VOOR de sprong naar het spel. Alleen als eerste argument. */
    if (argc >= 2 && (strcmp(argv[1], "--ontleed-alleen") == 0 || strcmp(argv[1], "--dry-run") == 0)) {
        rg_ontleed = 1;
        argc--;
        argv++;
    }
    if (argc < 2)
        fatal("gebruik: rg-elfloader [--ontleed-alleen] <game-elf> [args...]");
    game = argv[1];

    fd = open(game, O_RDONLY);
    if (fd < 0)
        fatal("kan %s niet openen", game);
    file = map_file(game, &flen);

    map_segments(fd, file, &b);
    rg_beeld_lo = b.lo;
    rg_beeld_hi = b.hi;
    lees_dyn(&b, &d);
    verzamel_lib_defs(&b, &d);  /* uit de lib-BESTANDEN: welke symbolen definiëren zij (voor de smalle shim) */
    maak_shim(&d);        /* smalle shim: alleen gast-symbolen die geen lib definieert; zie maak_shim() */
    laad_needed(&b, &d);  /* libs laden (RTLD_NOW); hun gast-imports lossen tegen de shim op */
    bouw_stale_map(&d);   /* VOOR de JMPREL-pass: de originele stale 2e-tabel-waarden + namen vastleggen */
    if (d.rel && d.relsz)
        pas_rel_toe(&d, d.rel, d.relsz / sizeof(Elf32_Rel), "DT_REL");
    if (d.jmprel && d.pltrelsz)
        pas_rel_toe(&d, d.jmprel, d.pltrelsz / sizeof(Elf32_Rel), "DT_JMPREL");
    pas_slottabel_toe(&b);       /* gepakte klasse: namen uit de slottabel van de game (patches/<Game>/), indien gegeven */
    repareer_eerste_plt(&b, &d);   /* gepakte klasse: eerste-PLT-slots op naam herstellen (gemapt => veilig) */

    if (rg_ontleed) {
        toon_ontleding(&d, game);
        munmap(file, flen);
        close(fd);
        return 0;   /* hier eindigt de analyse: start_spel()/spring() worden NIET aangeroepen */
    }

    munmap(file, flen);
    close(fd);

    /* Init draaien we NIET zelf: _start -> __libc_start_main -> __libc_csu_init draait DT_INIT en de
       init-array van het spel. De NEEDED-libs zijn al door dlopen geinitialiseerd. */
    say("geladen; spel %s krijgt argv[0]=%s", game, argv[1]);
    start_spel(&b, argc - 1, &argv[1]);
    return 0;
}
#endif /* RG_ELFLOADER_ZONDER_MAIN */
