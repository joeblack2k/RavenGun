/* hasplijst-toets.c - 32-bits testprogramma. STATISCH BEWIJS, GEEN GAME.
 *
 * Het mapt de PT_LOAD-segmenten van een gast-ELF op hun EIGEN virtuele adressen (zoals rg-elfloader
 * dat doet), en laadt dan de generieke HASP-inline-hook met dlopen. De constructor van de hook leest
 * het gastbeeld van schijf, keurt de adreslijst tegen dat beeld, en haakt aan wat geldig is.
 *
 * Doel: bewijzen dat de adreslijst van deze game-versie klopt, ZONDER de bench te gebruiken.
 *
 * Veiligheid:
 *   - het gastbestand wordt alleen GELEZEN (O_RDONLY), nooit geschreven;
 *   - het beeld wordt MAP_PRIVATE + anoniem gemapt, dus elke patch van de hook raakt alleen deze
 *     proceseigen kopie, nooit het bestand en nooit de NAS;
 *   - de hook wordt door dit programma geladen, niet via LD_PRELOAD in een gameproces;
 *   - met RAVENGUN_HASP_ELF_UIT=1 keurt de hook alleen en patcht hij niets.
 *
 * Bouwen (moet 32-bits en PIE zijn: PIE laat 0x08048000 vrij, waar het gastbeeld hoort):
 *     gcc -m32 -pie -fPIE -O1 -o hasplijst-toets hasplijst-toets.c -ldl
 *
 * Gebruik:
 *     ./hasplijst-toets <gast-ELF> <hookbibliotheek>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <elf.h>

#define PAGE 4096u
static uint32_t down(uint32_t a) { return a & ~(PAGE - 1); }
static uint32_t up(uint32_t a)   { return (a + PAGE - 1) & ~(PAGE - 1); }

int main(int argc, char **argv)
{
    const char *elf, *hook;
    int fd, i;
    Elf32_Ehdr eh;
    Elf32_Phdr ph[64];
    uint32_t lo = 0xffffffffu, hi = 0;
    void *probe, *img;

    if (argc < 3) {
        fprintf(stderr, "gebruik: %s <gast-ELF> <hookbibliotheek>\n", argv[0]);
        return 2;
    }
    elf = argv[1];
    hook = argv[2];

    fd = open(elf, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "open(%s): %s\n", elf, strerror(errno)); return 1; }
    if (pread(fd, &eh, sizeof eh, 0) != (ssize_t)sizeof eh) { perror("pread eh"); return 1; }
    if (memcmp(eh.e_ident, ELFMAG, 4) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "geen 32-bits ELF\n"); return 1;
    }
    if (eh.e_phnum > 64) { fprintf(stderr, "te veel program headers\n"); return 1; }
    if (pread(fd, ph, eh.e_phnum * sizeof(Elf32_Phdr), eh.e_phoff)
            != (ssize_t)(eh.e_phnum * sizeof(Elf32_Phdr))) { perror("pread phdrs"); return 1; }

    for (i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (down(ph[i].p_vaddr) < lo) lo = down(ph[i].p_vaddr);
        if (up(ph[i].p_vaddr + ph[i].p_memsz) > hi) hi = up(ph[i].p_vaddr + ph[i].p_memsz);
    }
    if (lo == 0xffffffffu || hi <= lo) { fprintf(stderr, "geen PT_LOAD\n"); return 1; }

    {
        struct stat st;
        if (fstat(fd, &st) == 0)
            printf("gast-ELF      : %s (%lld bytes)\n", elf, (long long)st.st_size);
        else
            printf("gast-ELF      : %s\n", elf);
    }
    printf("beeld-bereik  : 0x%08x .. 0x%08x  (%u KB)\n", lo, hi, (hi - lo) / 1024);
    printf("e_entry       : 0x%08x   phnum=%d\n", eh.e_entry, eh.e_phnum);

    /* Vooraf controleren of het bereik vrij is: anders zou MAP_FIXED onze eigen mappings
       overschrijven en meten we niets zinnigs. */
    probe = mmap((void *)(uintptr_t)lo, hi - lo, PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (probe == MAP_FAILED) { perror("probe mmap"); return 1; }
    if ((uintptr_t)probe != (uintptr_t)lo) {
        fprintf(stderr, "BEZET: het bereik 0x%08x..0x%08x is al in gebruik (probe gaf %p); "
                        "herbouw dit programma als PIE (-pie -fPIE)\n",
                lo, hi, probe);
        munmap(probe, hi - lo);
        return 1;
    }
    munmap(probe, hi - lo);

    img = mmap((void *)(uintptr_t)lo, hi - lo, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
    if (img == MAP_FAILED) { perror("mmap beeld"); return 1; }
    if ((uintptr_t)img != (uintptr_t)lo) { fprintf(stderr, "MAP_FIXED landde niet op 0x%08x\n", lo); return 1; }

    for (i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_filesz == 0) continue;
        if (pread(fd, (void *)(uintptr_t)ph[i].p_vaddr, ph[i].p_filesz, ph[i].p_offset)
                != (ssize_t)ph[i].p_filesz) {
            fprintf(stderr, "segment %d niet volledig ingelezen\n", i); return 1;
        }
    }
    for (i = 0; i < eh.e_phnum; i++) {
        int prot = 0;
        uint32_t a, z;
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_flags & PF_R) prot |= PROT_READ;
        if (ph[i].p_flags & PF_W) prot |= PROT_WRITE;
        if (ph[i].p_flags & PF_X) prot |= PROT_EXEC;
        a = down(ph[i].p_vaddr);
        z = up(ph[i].p_vaddr + ph[i].p_memsz);
        if (mprotect((void *)(uintptr_t)a, z - a, prot) != 0) { perror("mprotect"); return 1; }
    }
    close(fd);

    /* Controle dat het beeld er echt staat en uitvoerbaar is. */
    {
        unsigned char *p = (unsigned char *)(uintptr_t)eh.e_entry;
        printf("beeld gemapt  : ELF-magic op 0x%08x = %02x %02x %02x %02x, e_entry[0..3] = %02x %02x %02x %02x\n",
               lo, ((unsigned char *)(uintptr_t)lo)[0], ((unsigned char *)(uintptr_t)lo)[1],
               ((unsigned char *)(uintptr_t)lo)[2], ((unsigned char *)(uintptr_t)lo)[3],
               p[0], p[1], p[2], p[3]);
    }

    printf("dlopen        : %s\n", hook);
    fflush(stdout);
    if (!dlopen(hook, RTLD_NOW)) {
        fprintf(stderr, "dlopen mislukt: %s\n", dlerror());
        return 3;
    }
    printf("hook geladen  : constructor heeft gedraaid\n");
    return 0;
}
