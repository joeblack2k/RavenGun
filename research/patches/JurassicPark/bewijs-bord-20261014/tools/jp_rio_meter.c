/* Meetinstrument voor het RIO-bord van Jurassic Park (game 1.08, sha256 c18a9329...), 32 bits, via LD_PRELOAD in het
 * spelproces van rg-elfloader. Geen fix: het leest de eigen toestand van de RIO-bibliotheek in het spel en schrijft die elke
 * seconde naar $RG_JP_METER_DIR/meter.log. Optioneel (positieve controle): regels in $RG_JP_METER_DIR/inject
 *   sw <id> <teller>     een schakelaar-gebeurtenis (type 3, overgangsteller) in de EIGEN callback-wachtrij van de bibliotheek
 *   adc <id> <waarde>    een kanaal-gebeurtenis (type 3, waarde)
 * precies zoals process_sw/process_adc (0x836ce8e/0x836cf82) ze daar zetten. Alles via /proc/self/mem: geen crash als het
 * beeld er (nog) niet is. Adressen uit de dynamische symbooltabel van dit ene binair; een ander binair wordt niet aangeraakt.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define A_RIO_PROCESSCALLBACKS 0x0836b5f2u
#define A_IO_ACTIVE_MASK       0x08b7f628u /* io_loop/io_reinit: bit 0x2 = RIO-backend actief */
#define A_DEVP                 0x08bb32a0u /* handvat van pmrt_usb_open */
#define A_CONNECTED            0x08e7d61cu /* rpdata+0x1c, gezet door RIO_Connect */
#define A_THREAD_TICKS         0x08bb32a8u /* g_rio_thread_ticks: +1 per RIO_GetReport in de leesdraad */
#define A_CB_WR                0x08bb32d0u
#define A_CB_RD                0x08bb32d4u
#define A_CALLBACKS            0x08e7ddc0u /* 13 bytes: +0 fn, +4 id, +8 type, +9 waarde */
#define A_RODATA               0x08e7d2a0u /* 13 bytes per id: +0 gebruikt, +5 callback */
#define A_RIOLOOP_DT           0x08baabc8u /* io_rio_loop schrijft hier elke aanroep zijn argument */
#define A_RECORD_PTR           0x08baabc4u /* io_rio_init: invoerrecord van de RIO-backend */
#define A_SWTAB                0x08b26b80u /* 19 x {id, offset in record} */

static const unsigned char SIG_PCB[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x38, 0xc7, 0x45, 0xe8, 0x00, 0x00, 0x00, 0x00, 0xa1};

static int memfd = -1;
static int rd(uint32_t a, void *b, size_t n) { return pread(memfd, b, n, (off_t)a) == (ssize_t)n ? 0 : -1; }
static int wr(uint32_t a, const void *b, size_t n) { return pwrite(memfd, b, n, (off_t)a) == (ssize_t)n ? 0 : -1; }
static uint32_t u32(uint32_t a) { uint32_t v = 0xdeadbeef; rd(a, &v, 4); return v; }

static double nu(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static void inject(FILE *log, const char *soort, uint32_t id, uint32_t waarde) {
    unsigned char e[13];
    uint32_t fn = 0;
    if (id > 63 || rd(A_RODATA + 13 * id + 5, &fn, 4)) { fprintf(log, "INJECT %s %u: buiten bereik\n", soort, id); return; }
    if (!fn) { fprintf(log, "INJECT %s %u %u: geen callback geregistreerd (RIO_Uses)\n", soort, id, waarde); return; }
    uint32_t w = u32(A_CB_WR), r = u32(A_CB_RD), volg = w + 1 > 0xfff ? 0 : w + 1;
    if (volg == r) { fprintf(log, "INJECT %s %u: wachtrij vol\n", soort, id); return; }
    memcpy(e, &fn, 4); memcpy(e + 4, &id, 4); e[8] = 3; memcpy(e + 9, &waarde, 4);
    wr(A_CALLBACKS + 13 * w, e, 13);
    __sync_synchronize();
    wr(A_CB_WR, &volg, 4);
    fprintf(log, "INJECT %s id=%u waarde=%u fn=0x%08x wachtrij %u->%u (lees %u)\n", soort, id, waarde, fn, w, volg, r);
}

static void *draad(void *arg) {
    const char *dir = arg;
    char pad[512];
    snprintf(pad, sizeof pad, "%s/meter.log", dir);
    FILE *log = fopen(pad, "a");
    if (!log) return NULL;
    setvbuf(log, NULL, _IOLBF, 0);
    memfd = open("/proc/self/mem", O_RDWR);
    fprintf(log, "meter pid %d start; /proc/self/mem fd %d\n", getpid(), memfd);
    double t0 = nu();
    unsigned char sig[sizeof SIG_PCB];
    while (rd(A_RIO_PROCESSCALLBACKS, sig, sizeof sig) || memcmp(sig, SIG_PCB, sizeof sig)) {
        if (nu() - t0 > 120) { fprintf(log, "beeld niet gevonden (ander binair?): meter stopt\n"); return NULL; }
        usleep(100000);
    }
    unsigned char kop[3]; rd(0x0836b320, kop, 3);
    unsigned char kop2[3]; rd(0x0836b128, kop2, 3);
    fprintf(log, "beeld herkend na %.1f s; RIO_Connected begint %02x %02x %02x, RIO_Connect begint %02x %02x %02x\n",
            nu() - t0, kop[0], kop[1], kop[2], kop2[0], kop2[1], kop2[2]);
    snprintf(pad, sizeof pad, "%s/inject", dir);
    long gedaan = 0;
    for (int tik = 0;; tik++) {
        FILE *f = fopen(pad, "r");
        if (f) {
            char regel[128]; long n = 0;
            while (fgets(regel, sizeof regel, f)) {
                if (++n <= gedaan) continue;
                char soort[8]; unsigned id, w;
                if (sscanf(regel, "%7s %u %u", soort, &id, &w) == 3) {
                    fprintf(log, "%.1f ", nu() - t0); inject(log, soort, id, w);
                }
            }
            gedaan = n; fclose(f);
        }
        if (tik % 10 == 0) {
            uint32_t rec = u32(A_RECORD_PTR);
            int gebruikt = 0;
            for (int i = 0; i < 64; i++) { unsigned char g = 0; rd(A_RODATA + 13 * i, &g, 1); gebruikt += g == 1; }
            fprintf(log, "%.1f io-masker=0x%x rio-actief=%d devp=0x%x verbonden=%u leesdraad-tikken=%u wachtrij schrijf=%u lees=%u "
                         "ids-geregistreerd=%d io_rio_loop-arg=%u record=0x%x sw[druk/los]:",
                    nu() - t0, u32(A_IO_ACTIVE_MASK), (u32(A_IO_ACTIVE_MASK) & 2) != 0, u32(A_DEVP), u32(A_CONNECTED),
                    u32(A_THREAD_TICKS), u32(A_CB_WR), u32(A_CB_RD), gebruikt, u32(A_RIOLOOP_DT), rec);
            for (int i = 0; i < 19 && rec && rec != 0xdeadbeef; i++) {
                uint32_t id = u32(A_SWTAB + 8 * i), off = u32(A_SWTAB + 8 * i + 4);
                uint32_t p = u32(rec + off + 4), l = u32(rec + off + 8);
                if (p || l) fprintf(log, " %u:%u/%u", id, p, l);
            }
            fprintf(log, "\n");
        }
        usleep(100000);
    }
    return NULL;
}

__attribute__((constructor)) static void start(void) {
    const char *dir = getenv("RG_JP_METER_DIR");
    if (!dir) return;
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &at, draad, (void *)dir);
}
