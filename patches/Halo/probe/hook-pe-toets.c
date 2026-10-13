/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hook-pe-toets.c — bewijs ZONDER de bench dat de generieke PE-inline-hook werkt.
 *
 * Het mapt een ECHT PE-geheugenbeeld van Luigi's Mansion (VACUUM.exe, gedumpt uit een lopend
 * proces onder de gesloten kern; offset == RVA bij ImageBase 0x140000000) op zijn eigen adressen,
 * laat de engine los, en kan daarna een aangehaakte functie AANROEPEN om te laten zien dat de
 * gegenereerde stub echt draait.
 *
 * Dat is precies dezelfde weg door de code als in het spelproces: de engine leest de PE-kop uit het
 * beeld, keurt elke RVA tegen de secties, genereert de stubs in een uitvoerbare pagina naast het
 * beeld en zet de prologues om. Alleen het proces is anders, niet de weg.
 *
 * GEBRUIK
 *   hook-pe-toets <beeld.bin> <lijst.ini> [--verwacht N] [--bel RVA] [--stil]
 * Exit 0 als het aantal aangehaakte functies gelijk is aan --verwacht (standaard: geen eis).
 */
#define _GNU_SOURCE
#include "rg_pe_hook.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

static rg_pe_ctx G;

/* Simuleert de pakker: schrijft na N ms de prologues op de opgegeven RVA's. Zo kan de wachtroute
   bewezen worden zonder een spel te starten. */
static unsigned char *G_beeld;
static uint64_t G_ib;
static int G_ontpak_ms;
static uint64_t G_rvas[64];
static int G_nrvas;
static int G_stappen;
static volatile int G_vrij;      /* de pakker mag pas als de hoofdthread verdergaat */
static int G_rc;

static void *ontpakker(void *arg)
{
    int i;
    (void)arg;
    /* De pakker kan pas LOPEN als de hoofdthread verdergaat. Dat is precies de situatie in het spel:
       de lader hervat de hoofdthread pas als de preload-DLL is teruggekeerd, en het ontpakken begint
       pas daarna. Zonder deze poort laat de proef de deadlock niet zien. */
    while (!G_vrij)
        usleep(1000);
    if (G_stappen) {
        /* In stappen, zodat het criterium zijn drie gevallen laat zien: eerst int3-opvulling, dan de
           echte prologue. */
        usleep((useconds_t)G_ontpak_ms * 1000);
        for (i = 0; i < G_nrvas; i++)
            memcpy(G_beeld + G_rvas[i], "\xcc\xcc\xcc\xcc\xcc\xcc", 6);
        printf("   [ontpakker] opvulling (cc) geschreven\n");
        usleep((useconds_t)G_ontpak_ms * 1000);
        for (i = 0; i < G_nrvas; i++)
            memcpy(G_beeld + G_rvas[i], "\x40\x53\x48\x83\xec\x28", 6);
        printf("   [ontpakker] echte prologues geschreven\n");
        return NULL;
    }
    usleep((useconds_t)G_ontpak_ms * 1000);
    for (i = 0; i < G_nrvas; i++)
        memcpy(G_beeld + G_rvas[i], "\x40\x53\x48\x83\xec\x28", 6);
    printf("   [ontpakker] %d prologues geschreven (%d ms nadat de hoofdthread verderging)\n",
           G_nrvas, G_ontpak_ms);
    return NULL;
}

static void *haak_thread(void *arg)
{
    (void)arg;
    G_rc = rg_pe_hook(&G);
    return NULL;
}

static void melder(int idx)
{
    if (idx >= 0 && idx < G.n)
        printf("      [stub] %s: ANTWOORD NOG NIET GEMETEN (TODO) -- de stub geeft 0 terug; "
               "vul patches/LuigisMansion/dongle-haken.ini\n", G.regels[idx].naam);
    else
        printf("      [stub] TODO (index %d)\n", idx);
}

int main(int argc, char **argv)
{
    const char *beeld_pad, *lijst;
    unsigned char *ruw = NULL;
    unsigned char *beeld;
    rg_pe_kop kop;
    long grootte;
    size_t pag, mapte;
    int fd, verwacht = -1, i;
    uint64_t bel = 0, bel_net = 0;
    const char *ontpak = NULL;
    const char *bytes_uit = "/tmp/rg-pe-hook-bytes.txt";
    int eigen = 0;
    int rc;

    if (argc < 3) {
        fprintf(stderr, "gebruik: %s <beeld.bin> <lijst.ini> [--verwacht N] [--bel RVA] "
                        "[--bel-net RVA] [--ontpak-na N --ontpak-rvas 0x..,0x..] [--eigen-thread]\n",
                argv[0]);
        return 2;
    }
    beeld_pad = argv[1];
    lijst = argv[2];
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--verwacht") && i + 1 < argc)
            verwacht = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bel-net") && i + 1 < argc)
            bel_net = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--stappen"))
            G_stappen = 1;
        else if (!strcmp(argv[i], "--eigen-thread"))
            eigen = 1;
        else if (!strcmp(argv[i], "--bytes-uit") && i + 1 < argc)
            bytes_uit = argv[++i];
        else if (!strcmp(argv[i], "--ontpak-na") && i + 1 < argc)
            G_ontpak_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ontpak-rvas") && i + 1 < argc)
            ontpak = argv[++i];
        else if (!strcmp(argv[i], "--bel") && i + 1 < argc)
            bel = strtoull(argv[++i], NULL, 0);
    }

    fd = open(beeld_pad, O_RDONLY);
    if (fd < 0) { perror("open"); return 2; }
    grootte = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    ruw = malloc((size_t)grootte);
    if (!ruw || read(fd, ruw, (size_t)grootte) != grootte) { perror("read"); return 2; }
    close(fd);

    if (!rg_pe_lees_kop(ruw, (size_t)grootte, &kop)) {
        fprintf(stderr, "geen geldige PE-kop in %s\n", beeld_pad);
        return 2;
    }
    printf("1. het beeld vastzetten op zijn eigen ImageBase\n");
    printf("   bestand %s, %ld bytes; ImageBase 0x%llx, SizeOfImage 0x%x\n", beeld_pad, grootte,
           (unsigned long long)kop.imagebase, kop.grootte_beeld);

    pag = (size_t)sysconf(_SC_PAGESIZE);
    mapte = ((size_t)kop.grootte_beeld + pag - 1) & ~(pag - 1);
    beeld = mmap((void *)(uintptr_t)kop.imagebase, mapte, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (beeld == MAP_FAILED) {
        beeld = mmap((void *)(uintptr_t)kop.imagebase, mapte, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    }
    if (beeld == MAP_FAILED) { perror("mmap"); return 2; }
    memcpy(beeld, ruw, (size_t)(grootte < (long)mapte ? grootte : (long)mapte));
    printf("   gemapt op 0x%llx, 0x%zx bytes\n", (unsigned long long)kop.imagebase, mapte);

    if (ontpak) {
        char *kopie = strdup(ontpak), *tok;
        for (tok = strtok(kopie, ","); tok && G_nrvas < 64; tok = strtok(NULL, ","))
            G_rvas[G_nrvas++] = strtoull(tok, NULL, 0);
        free(kopie);
        G_beeld = beeld;
        G_ib = kop.imagebase;
    }

    memset(&G, 0, sizeof G);
    G.bytes_pad = bytes_uit;
    G.beeld_naam = beeld_pad;
    G.pid = (unsigned long)getpid();
    G.tid = 0;
    G.proc_uptime_ms = 0;
    G.init_teller = 1;
    G.lijst_pad = lijst;
    G.beeld = beeld;
    G.beeld_n = (size_t)grootte;
    G.log = stdout;
    G.todo_melder = melder;

    if (G_nrvas) {
        pthread_t th;
        printf("1b. de ontpakker start (schrijft de prologues na %d ms)\n", G_ontpak_ms);
        pthread_create(&th, NULL, ontpakker, NULL);
        pthread_detach(th);
    }

    printf("2. de engine loslaten%s\n",
           eigen ? " (op een EIGEN thread, zoals de DLL)" : " (op de HOOFDthread)");
    if (eigen) {
        pthread_t th;
        if (pthread_create(&th, NULL, haak_thread, NULL) != 0) {
            printf("   kon de eigen thread niet starten\n");
            return 2;
        }
        G_vrij = 1;   /* de hoofdthread gaat verder -- zoals de lader doet */
        printf("   [hoofdthread] ik ga verder; de hook draait op zijn eigen thread\n");
        pthread_join(th, NULL);
        rc = G_rc;
    } else {
        rc = rg_pe_hook(&G);
        G_vrij = 1;   /* pas NU mag de pakker: dat is de fout van wachten op de hoofdthread */
        printf("   [hoofdthread] de hook is klaar; nu mag de pakker pas\n");
    }
    printf("   rg_pe_hook gaf %d\n", rc);
    if (rc < 0)
        return 1;

    if (bel) {
        int (*fn)(void *, void *, void *, void *) = (int (*)(void *, void *, void *, void *))
            (uintptr_t)(kop.imagebase + bel);
        int r;
        printf("3. de aangehaakte functie op RVA 0x%llx AANROEPEN\n", (unsigned long long)bel);
        r = fn(NULL, NULL, NULL, NULL);
        printf("   teruggegeven: %d\n", r);
    }

    if (bel_net) {
        /* De Windows x64-ABI zet het eerste argument in RCX; deze gastheer is SysV (RDI). Voor een
           stub die in *(rcx) schrijft zetten we RCX dus expliciet, zoals de game het zou doen. */
        unsigned char buf[32];
        uintptr_t adres = (uintptr_t)(kop.imagebase + bel_net);
        int r;
        memset(buf, 0xAA, sizeof buf);
        printf("3. de aangehaakte functie op RVA 0x%llx aanroepen met rcx = buffer\n",
               (unsigned long long)bel_net);
        __asm__ volatile("mov %2, %%rcx\n\tcall *%1\n\t"
                         : "=a"(r)
                         : "r"(adres), "r"((uintptr_t)buf)
                         : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
        printf("   teruggegeven: %d\n", r);
        printf("   buffer na de aanroep: %02x %02x %02x %02x   (verwacht c0 a8 01 00 = "
               "192.168.1.0)\n", buf[0], buf[1], buf[2], buf[3]);
        if (buf[0] == 0xc0 && buf[1] == 0xa8 && buf[2] == 0x01 && buf[3] == 0x00 && r == 0) {
            printf("   GESLAAGD: de stub gaf 0 terug EN schreef het gemeten netwerkadres\n");
        } else {
            printf("   MISLUKT: de stub gaf %d terug en schreef %02x %02x %02x %02x\n", r,
                   buf[0], buf[1], buf[2], buf[3]);
            return 1;
        }
    }

    if (verwacht >= 0) {
        int ok = (rc == verwacht);
        printf("4. %s: %d van %d aangehaakt (verwacht %d)\n", ok ? "GESLAAGD" : "MISLUKT",
               rc, G.n, verwacht);
        return ok ? 0 : 1;
    }
    printf("4. klaar: %d van %d aangehaakt\n", rc, G.n);
    return 0;
}
