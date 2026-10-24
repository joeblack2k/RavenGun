/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hook-name-route-test.c — bewijs ZONDER de bench wat de naam-route van de HASP-inline-hook doet
 * op een echt spelbeeld, en of de rekenwijze-reparatie (hh_base - hh_laag + st_value) verschil maakt.
 *
 * WAT HET DOET
 * ------------
 * Het zet de PT_LOAD-segmenten van een echte game-ELF vast op hun eigen virtuele adressen (vanuit een
 * PIE-gastheer, dus zonder botsing), vult ze met de bytes uit het bestand, onthoudt de bytes op elk
 * opgegeven adres, en laadt dan de hook met dlopen. Daarna leest het die adressen terug en kijkt of de
 * hook er een sprong heeft gezet.
 *
 * Werkt voor ELF32 en ELF64 (de gastheer moet dezelfde breedte hebben als de game en de hook).
 *
 * GEBRUIK
 *   hook-name-route-test <game-ELF> <hook.so> <adres> [adres...]
 * Exit 0 = minstens één adres is aangehaakt; de uitslag per adres staat op stdout.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <elf.h>
#include <dlfcn.h>

#if defined(__x86_64__)
typedef Elf64_Ehdr EH;
typedef Elf64_Phdr PH;
#define ELFCLASS_MIJN ELFCLASS64
#else
typedef Elf32_Ehdr EH;
typedef Elf32_Phdr PH;
#define ELFCLASS_MIJN ELFCLASS32
#endif

#define MAXA 64

static uint64_t adressen[MAXA];
static unsigned char voor[MAXA][16];
static int n;

static void hex(const unsigned char *p, int k, char *uit, size_t m)
{
    size_t o = 0;
    for (int i = 0; i < k && o + 4 < m; i++)
        o += snprintf(uit + o, m - o, "%02x ", p[i]);
    if (o) uit[o - 1] = 0;
}

int main(int argc, char **argv)
{
    const char *game, *hook;
    EH *eh;
    PH *ph;
    unsigned char kop[4096];
    long pag;
    int fd, i, aangehaakt = 0;
    uint64_t laag = ~(uint64_t)0;

    if (argc < 4) {
        fprintf(stderr, "gebruik: %s <game-ELF> <hook.so> <adres> [adres...]\n", argv[0]);
        return 2;
    }
    game = argv[1];
    hook = argv[2];
    for (i = 3; i < argc && n < MAXA; i++)
        adressen[n++] = strtoull(argv[i], NULL, 0);

    fd = open(game, O_RDONLY);
    if (fd < 0) { perror("open"); return 2; }
    if (pread(fd, kop, sizeof kop, 0) != (ssize_t)sizeof kop) { fprintf(stderr, "kop?\n"); return 2; }
    eh = (EH *)kop;
    if (memcmp(eh->e_ident, ELFMAG, 4) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS_MIJN) {
        fprintf(stderr, "verkeerde ELF-klasse voor deze gastheer\n");
        return 2;
    }
    ph = malloc((size_t)eh->e_phnum * sizeof(PH));
    if (pread(fd, ph, (size_t)eh->e_phnum * sizeof(PH), eh->e_phoff) <= 0) {
        fprintf(stderr, "phdr?\n");
        return 2;
    }
    pag = sysconf(_SC_PAGESIZE);

    for (i = 0; i < eh->e_phnum; i++) {
        uint64_t start, eind;
        void *p;
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_vaddr < laag)
            laag = ph[i].p_vaddr;
        start = ph[i].p_vaddr & ~(uint64_t)(pag - 1);
        eind = (ph[i].p_vaddr + ph[i].p_filesz + (uint64_t)pag - 1) & ~(uint64_t)(pag - 1);
#ifdef MAP_FIXED_NOREPLACE
        p = mmap((void *)(uintptr_t)start, (size_t)(eind - start),
                 PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED)
#endif
            p = mmap((void *)(uintptr_t)start, (size_t)(eind - start),
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (p == MAP_FAILED) { perror("mmap"); return 2; }
        if (pread(fd, (void *)(uintptr_t)ph[i].p_vaddr, ph[i].p_filesz, ph[i].p_offset)
            != (ssize_t)ph[i].p_filesz) {
            fprintf(stderr, "segment %d lezen mislukt\n", i);
            return 2;
        }
        printf("  PT_LOAD[%d] op 0x%08llx, 0x%llx bytes, %s\n", i,
               (unsigned long long)ph[i].p_vaddr, (unsigned long long)ph[i].p_filesz,
               (ph[i].p_flags & PF_X) ? "R+X" : "geen X");
    }
    close(fd);
    printf("  laagste PT_LOAD-vaddr: 0x%08llx\n", (unsigned long long)laag);

    for (i = 0; i < n; i++)
        memcpy(voor[i], (void *)(uintptr_t)adressen[i], 16);

    if (!dlopen(hook, RTLD_NOW)) {
        fprintf(stderr, "  dlopen: %s\n", dlerror());
        return 1;
    }

    for (i = 0; i < n; i++) {
        unsigned char nu[16];
        char a[64], b[64];
        int veranderd;
        memcpy(nu, (void *)(uintptr_t)adressen[i], 16);
        veranderd = memcmp(voor[i], nu, 16) != 0;
        hex(voor[i], 4, a, sizeof a);
        hex(nu, 8, b, sizeof b);
        printf("  0x%08llx  voor [%s]  na [%s]  %s\n", (unsigned long long)adressen[i], a, b,
               veranderd ? "AANGEHAAKT" : "NIET AANGERAAKT");
        if (veranderd)
            aangehaakt++;
    }
    printf("  RESULTAAT: %d van %d aangehaakt\n", aangehaakt, n);
    return aangehaakt > 0 ? 0 : 1;
}
