/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hook-address-route-test.c — bewijs ZONDER de bench dat de adres-route van de HASP-inline-hook
 * werkt op een beeld waar de naam-route nul vond.
 *
 * WAT HET DOET
 * ------------
 * Het maakt de twee PT_LOAD-segmenten van een echte `game`-ELF vast op hun eigen virtuele adressen
 * (MAP_FIXED op een PIE-gastheer, dus er botsen geen adressen), vult ze met de bytes uit het
 * bestand, en laadt dan de hook met dlopen. De hook ziet daardoor een spelbeeld op precies de
 * adressen die in de adreslijst staan, en patcht zijn zeven functies.
 *
 * Daarna leest dit programma de eerste twaalf bytes op elk van de zeven adressen terug en eist dat
 * daar de absolute sprong staat die de hook plaatst: `48 b8 <8 bytes> ff e0`.
 *
 * WAAROM DIT EEN ECHT BEWIJS IS
 * -----------------------------
 * Dezelfde hook, hetzelfde spelbeeld, dezelfde adreslijst, en hetzelfde pad door de code als in het
 * spelproces: hh_vind_elf -> hh_vind_dynsym -> hh_verzamel -> hh_vind_adreslijst ->
 * hh_keur_adreslijst -> hh_zoek_beeld -> hh_laad_emulator -> hh_koppel_emulator -> hh_haak_alles ->
 * hh_patch. Alleen het proces is anders, niet de weg. De naam-route vindt in dit beeld nul namen
 * (dat staat in het logboek van de hook zelf), dus alles wat aangehaakt raakt komt uit de adreslijst.
 *
 * WAT HET NIET BEWIJST
 * --------------------
 * Niet dat de game de aangehaakte functies ook aanroept, en niet dat de emulator het juiste
 * antwoord geeft. Dat vraagt de bench. Dit bewijst de vondst en de patch, niet het gedrag.
 *
 * GEBRUIK (de omgevingsvariabelen zet het aanroepende script; zie address-route-test.sh):
 *   hook-address-route-test <game-ELF> <libravengun-hasphook-x64.so>
 * Exit 0 = alle zeven adressen zijn aangehaakt.
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

static const uint64_t ADRESSEN[] = {
    0xb2e1aa, 0xb2d630, 0xb2dd48, 0xb2fd63, 0xb2fe60, 0xb2e391, 0xb2f5ac
};
#define N (int)(sizeof ADRESSEN / sizeof ADRESSEN[0])

static void hex(const unsigned char *p, char *uit, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 4 < 200; i++)
        o += snprintf(uit + o, 200 - o, "%02x ", p[i]);
    if (o) uit[o - 1] = 0;
}

int main(int argc, char **argv)
{
    const char *game = argc > 1 ? argv[1] : NULL;
    const char *hook = argc > 2 ? argv[2] : NULL;
    unsigned char *kop, voor[N][16], nu[16];
    Elf64_Ehdr *eh;
    Elf64_Phdr *ph;
    long pag;
    int fd, i, ok = 0, fout = 0;

    if (!game || !hook) {
        fprintf(stderr, "gebruik: %s <game-ELF> <libravengun-hasphook-x64.so>\n", argv[0]);
        return 2;
    }

    fd = open(game, O_RDONLY);
    if (fd < 0) { perror("open"); return 2; }
    kop = malloc(4096);
    if (pread(fd, kop, 4096, 0) != 4096) { fprintf(stderr, "ELF-kop lezen mislukt\n"); return 2; }
    eh = (Elf64_Ehdr *)kop;
    if (memcmp(eh->e_ident, ELFMAG, 4) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_machine != EM_X86_64) {
        fprintf(stderr, "geen x86-64 ELF64\n");
        return 2;
    }
    ph = malloc((size_t)eh->e_phnum * sizeof(Elf64_Phdr));
    if (pread(fd, ph, (size_t)eh->e_phnum * sizeof(Elf64_Phdr), eh->e_phoff) <= 0) {
        fprintf(stderr, "program headers lezen mislukt\n");
        return 2;
    }
    pag = sysconf(_SC_PAGESIZE);

    printf("1. het spelbeeld vastzetten op zijn eigen adressen\n");
    for (i = 0; i < eh->e_phnum; i++) {
        uint64_t start, eind, len;
        void *p;
        if (ph[i].p_type != PT_LOAD)
            continue;
        start = ph[i].p_vaddr & ~(uint64_t)(pag - 1);
        eind = (ph[i].p_vaddr + ph[i].p_filesz + (uint64_t)pag - 1) & ~(uint64_t)(pag - 1);
        len = eind - start;
#ifdef MAP_FIXED_NOREPLACE
        p = mmap((void *)(uintptr_t)start, (size_t)len, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED)
#endif
            p = mmap((void *)(uintptr_t)start, (size_t)len, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (p == MAP_FAILED) { perror("mmap"); return 2; }
        if (pread(fd, (void *)(uintptr_t)ph[i].p_vaddr, ph[i].p_filesz, ph[i].p_offset)
            != (ssize_t)ph[i].p_filesz) {
            fprintf(stderr, "segment %d lezen mislukt\n", i);
            return 2;
        }
        printf("   PT_LOAD[%d] op 0x%08llx, 0x%llx bytes, %s\n", i,
               (unsigned long long)ph[i].p_vaddr, (unsigned long long)ph[i].p_filesz,
               (ph[i].p_flags & PF_X) ? "R+X" : "geen X");
    }
    close(fd);

    for (i = 0; i < N; i++)
        memcpy(voor[i], (void *)(uintptr_t)ADRESSEN[i], 16);

    printf("2. de hook laden (zijn constructor doet de rest)\n");
    if (!dlopen(hook, RTLD_NOW)) {
        fprintf(stderr, "   dlopen: %s\n", dlerror());
        return 1;
    }
    printf("   geladen\n");

    printf("3. de zeven adressen teruggelezen\n");
    for (i = 0; i < N; i++) {
        char a[200], b[200];
        int gepatcht, veranderd;
        memcpy(nu, (void *)(uintptr_t)ADRESSEN[i], 16);
        hex(voor[i], a, 4);
        hex(nu, b, 12);
        gepatcht = (nu[0] == 0x48 && nu[1] == 0xb8 && nu[10] == 0xff && nu[11] == 0xe0);
        veranderd = memcmp(voor[i], nu, 16) != 0;
        printf("   0x%08llx  voor [%s]  na [%s]  %s\n", (unsigned long long)ADRESSEN[i], a, b,
               gepatcht ? "AANGEHAAKT" : (veranderd ? "ANDERS" : "NIET AANGERAAKT"));
        if (gepatcht)
            ok++;
        else
            fout++;
    }
    printf("4. %s: %d van %d aangehaakt\n", fout ? "MISLUKT" : "GESLAAGD", ok, N);
    return fout ? 1 : 0;
}
