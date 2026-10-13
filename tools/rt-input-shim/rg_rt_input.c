/* RavenGun: invoerhulp voor Raw Thrills-games die native op Linux draaien (docs/decisions.md, "Native route").
 *
 * Probleem: de native Raw Thrills-games lezen de kast-schakelaars (munt, start, test/diag, service, volume) van het RIO2-I/O-bord
 * (USB, in de game ingebouwde bibliotheek) en de guns als SDL-joystick. Zonder RIO2-bord komt er geen enkele kastschakelaar binnen,
 * en de game blijft op zijn eigen "Press START or TEST"-scherm staan. De vooraf te laden bibliotheek van TeknoParrot (halo.so)
 * doet alleen de dongle, geen invoer.
 *
 * Deze bibliotheek is invoer en niets anders: zij leest het gedeelde geheugen van de daemon (/dev/shm/TeknoParrot_JvsState,
 * indeling WartranTroopers, geschreven door de IO-brug) en zet de kastschakelaars in de eigen schakelaartabel van de game, op de
 * plek waar de game ze zelf leest (getter op 0x61e890, tabel 0x180ae80 + 0xaea0, stap 0x50 per schakelaar en 0x2200 per bord).
 * Zij emuleert geen RIO2-bord, geen beveiligingschip, geen dongle en omzeilt geen controle van de game: de game ziet nog steeds
 * geen RIO2-bord; alleen de schakelaars komen binnen. De adressen horen bij Halo: Fireteam Raven, Game Rev 57529 (de bytes van de
 * getter worden eerst gecontroleerd; bij een andere build doet de bibliotheek niets).
 *
 * Een schakelaar is een teller van indrukken in dit beeld (de game leest in een beeld [ingang+4] != 0 als "ingedrukt"); een
 * indruk wordt daarom als korte puls gezet (per schakelaar 14 tot 300 ms, RG_RT_PULSE_MS overschrijft), zodat een beeld ze een keer ziet.
 *
 * Gebruik: LD_PRELOAD=/pad/librg_rt_input.so:/pm/g7/halo/halo.so ... De daemon moet de IO-brug (tp-shm, WartranTroopers) draaien.
 * Omgeving: RG_RT_SHM=pad (standaard /dev/shm/TeknoParrot_JvsState), RG_RT_LOG=1 (meldingen op stderr), RG_RT_PULSE_MS (overschrijft de pulsduur).
 * Onderzoek: RG_RT_PROBE=pad (bestand met "bit,bord,k", live gelezen) en RG_RT_ALLTYPES=1 (alle schakelaars als aanwezig markeren).
 * Eigen code.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define GETTER_ADDR 0x61e890UL
#define TABLE_BASE 0x180ae80UL      /* de io-toestand van de game */
#define TABLE_OFF 0xaea0UL          /* eerste schakelaar van bord 0 */
#define BOARD_STRIDE 0x2200UL
#define ENTRY_STRIDE 0x50UL

static const unsigned char getter_sig[8] = {0x85, 0xff, 0x0f, 0x88, 0xd0, 0x05, 0x00, 0x00};

/* Schakelaar k van bord b: k 0 = start, 1 = volume omhoog, 2 = volume omlaag, 3 = service, 4 = diag (test), 5..8 = munt 0..3. */
struct map { int bit; int board; int k; int ms; };
static struct map MAP[] = {
    {6, 0, 0, 300}, {11, 1, 0, 300}, {16, 2, 0, 300}, {21, 3, 0, 300},   /* start speler 1..4 */
    {0, 0, 4, 300}, {0, 1, 4, 300}, {0, 2, 4, 300}, {0, 3, 4, 300},  /* test -> diag van elk bord (het kalibratiescherm per gun wil een langere indruk) */
    {1, 0, 3, 300},                                                 /* service */
    {2, 0, 5, 14}, {3, 1, 5, 14},                                   /* munt 1 en 2: een indruk telt per beeld, dus kort */
    {4, 0, 1, 300}, {5, 0, 2, 300},                                   /* volume omhoog en omlaag */
};
#define NMAP (int)(sizeof(MAP) / sizeof(MAP[0]))

static int log_on;
static void logf_(const char *m) { if (log_on) { fputs("rg_rt_input: ", stderr); fputs(m, stderr); fputc('\n', stderr); } }

static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static volatile uint32_t *entry(int board, int k) {
    return (volatile uint32_t *)(TABLE_BASE + TABLE_OFF + (unsigned long)board * BOARD_STRIDE + (unsigned long)k * ENTRY_STRIDE);
}

static void *worker(void *arg) {
    (void)arg;
    const char *env = getenv("RG_RT_LOG"); log_on = env && *env == '1';
    const char *shm = getenv("RG_RT_SHM"); if (!shm) shm = "/dev/shm/TeknoParrot_JvsState";
    const char *pm = getenv("RG_RT_PULSE_MS"); int pulse_override = pm ? atoi(pm) : 0;

    /* Onderzoek: RG_RT_PROBE=pad naar een bestand met "bit,bord,k" zet een schakelaar van de tabel (live, elke 250 ms gelezen) op een andere plek. */
    const char *probe = getenv("RG_RT_PROBE");
    uint64_t probe_t = 0;
    const char *at = getenv("RG_RT_ALLTYPES"); int alltypes = at && *at == '1';

    /* Alleen op de bekende build. */
    if (memcmp((const void *)GETTER_ADDR, getter_sig, sizeof getter_sig) != 0) { logf_("andere game-build, geen invoer"); return 0; }

    /* LET OP: hier stond een mmap van het schakelaarbeeld. Dat is gevaarlijk: een schrijver die het
       bestand ook maar even afkapt (open met "wb" doet dat) maakt de mapping ongeldig, en dan sterft
       het spel met SIGBUS op de eerstvolgende lezing. Dat is precies wat in run 4 gebeurde
       (librg_rt_input.so+0x1f44, lezend op st+8). pread kan daar niet op stuklopen. */
    int shm_fd = -1;
    uint8_t buf[64];
    uint32_t prev = 0;
    uint64_t until[NMAP]; memset(until, 0, sizeof until);
    int active[NMAP]; memset(active, 0, sizeof active);
    uint64_t rel_until[NMAP]; memset(rel_until, 0, sizeof rel_until);
    for (;;) {
        if (shm_fd < 0) {
            shm_fd = open(shm, O_RDONLY | O_CLOEXEC);
            if (shm_fd < 0) { usleep(250000); continue; }
            logf_("gedeeld geheugen gekoppeld (pread)");
        }
        if (probe && now_ms() > probe_t + 250) {
            probe_t = now_ms();
            FILE *pf = fopen(probe, "r");
            if (pf) { int b, bd, k; if (fscanf(pf, "%d,%d,%d", &b, &bd, &k) == 3) for (int i = 0; i < NMAP; i++) if (MAP[i].bit == b) { MAP[i].board = bd; MAP[i].k = k; } fclose(pf); }
        }
        if (alltypes) for (int b = 0; b < 4; b++) for (int k = 0; k < 13; k++) entry(b, k)[0] = 1;   /* onderzoek: alle schakelaars als "aanwezig" markeren */
        ssize_t gelezen = pread(shm_fd, buf, sizeof buf, 0);
        if (gelezen < 12) { usleep(10000); continue; }   /* bestand (nog) te kort: niets lezen */
        uint32_t bits = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) | ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
        uint64_t t = now_ms();
        for (int i = 0; i < NMAP; i++) {
            int now = (bits >> MAP[i].bit) & 1, was = (prev >> MAP[i].bit) & 1;
            if (now && !was) { until[i] = t + (uint64_t)(pulse_override > 0 ? pulse_override : MAP[i].ms); active[i] = 1; { volatile uint32_t *e0 = entry(MAP[i].board, MAP[i].k); e0[6]++;
                if (log_on) { char b[160]; snprintf(b, sizeof b, "tabel bord %d k %d NA de indruk: [0]=%u [3]=%u [4]=%u [5]=%u [6]=%u", MAP[i].board, MAP[i].k, e0[0], e0[3], e0[4], e0[5], e0[6]); logf_(b); } } if (log_on) { char b[64]; snprintf(b, sizeof b, "indruk bit %d -> bord %d schakelaar %d", MAP[i].bit, MAP[i].board, MAP[i].k); logf_(b); } }
            volatile uint32_t *e = entry(MAP[i].board, MAP[i].k);
            if (!active[i] && rel_until[i]) { if (t < rel_until[i]) e[4] = 1; else { e[4] = 0; rel_until[i] = 0; } }
            if (active[i]) {
                if (t < until[i]) { e[0] = 1; e[3] = 1; e[5] = 1; }  /* soort 1 = actief; ingang+4 (= e[3]) = indruk in dit beeld; e[5] = vastgehouden */
                else { e[3] = 0; e[5] = 0; e[7]++; active[i] = 0; rel_until[i] = t + 25;
                       if (log_on) { char b[160]; snprintf(b, sizeof b, "tabel bord %d k %d bij LOSLATEN: [0]=%u [3]=%u [4]=%u [5]=%u [7]=%u", MAP[i].board, MAP[i].k, e[0], e[3], e[4], e[5], e[7]); logf_(b); } }   /* loslaten: ingang+8 (= e[4]) = loslating in dit beeld */
            }
        }
        prev = bits;
        usleep(1000);
    }
    return 0;
}

__attribute__((constructor)) static void rg_rt_input_init(void) {
    /* Alleen in het proces van de game zelf (de game start hulpprogramma's, bv. sha1sum, die de preload erven). */
    char exe[256]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n < 5) return;
    exe[n] = 0;
    if (strcmp(exe + n - 5, "/game") != 0) return;
    pthread_t th;
    if (pthread_create(&th, 0, worker, 0) == 0) pthread_detach(th);
}
