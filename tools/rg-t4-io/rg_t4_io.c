/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-t4-io -- de kast van Terminator Salvation voor de native route (LD_PRELOAD, i386; eigen code, geen TeknoParrot):
 *   1. de BORDKANT van de seriele JAMMA-lijn op /dev/ttyS0: schakelaars (munt, START, TEST, SERVICE, volume, trekker, pomp) uit het
 *      bordbeeld van de daemon (/dev/shm/ravengun_JvsState, indeling RawThrillsGUN) als frames op de lijn;
 *   2. het RICHTPUNT: de twee kommagetallen per geweer waaruit de game zijn richtpunt leest, uit dezelfde bordbeeld-assen;
 *   3. glutInit: het hardgecodeerde "-display :0.0" van de game vervangen door $DISPLAY.
 * De pty-koppeling zelf (/dev/ttyS0 <-> /tmp/ttyJamma, en /dev/sierra2 <-> /tmp/ttySierra) doet librg_ptylink.so (RG_PTYLINK).
 *
 * WAAROM (alles statisch gemeten aan `game` en op de bench van 2026-10-13, patches/Terminator/ATTRACT-RUN.md):
 *   - Schakelaars komen van een seriele "JAMMA I/O"-lijn, 115200 raw. Frame: LF, 'S', zestien hexcijfers (acht bytes), CR. Byte 1 = 0x80;
 *     bytes 2..8 zijn veertien 4-bit wikkelende overgangstellers, hoge nibble eerst:
 *       2 trekker1/2   3 pomp1/2   4 START1/2   5 MUNT1/2   6 volume op/neer   7 TEST/SERVICE   8 tilt/biljet
 *     Het eerste frame is de nulmeting; elke ophoging is een flank (even -> oneven = indrukken, oneven -> even = loslaten).
 *     De game schrijft zelf continu naar de lijn (P<n><hex>, W03, s): leeglezen, anders "JAMMA: jamma_msg() -1" na ~4,5 min.
 *   - Het richtpunt (statisch, game 0x842e36f get_aim(geweer, float *x, float *y)): is byte[0x8b0f334+geweer] nul, dan leest de functie
 *     rechtstreeks de floats op 0x8b0da00 + geweer*0x30 (x) en +4 (y); is hij niet nul en dword[0x8b0daf4+4*geweer] > 3, dan -1/-1.
 *     De aanroeper (0x815a410) eist x in [0,800] en y in [0,600] (anders telt het geweer niet mee). Zonder cmgr-verbinding (geen
 *     IRTrack-geweer aan een ttyUSB) draait de LED-oplosser van de game niet (0x843a0ac geeft -1) en schrijft niemand anders in dat paar.
 *   De hulp zet dus alleen INVOER op de plek waar de game haar zelf leest; zij emuleert geen beveiliging, dongle of chip.
 *
 * Omgeving:
 *   RG_T4_JAMMA_DEV=pad   het bordpad (standaard /tmp/ttyJamma; de meesterkant van RG_PTYLINK)
 *   RG_T4_DRAIN_DEV=pad   een tweede lijn die alleen leeggelezen wordt (standaard /tmp/ttySierra; de game schrijft ook daar)
 *   RG_T4_SHM=pad         het bordbeeld (standaard /dev/shm/ravengun_JvsState)
 *   RG_T4_AIM=0|1         richtpunt schrijven (standaard 1); RG_T4_AIM_W/H (800/600), RG_T4_AIM_INVX=1, RG_T4_AIM_INVY=1
 *   RG_T4_AIM_X0/X1/Y0/Y1 de strook van het scherm (0..1) waar het spelbeeld in staat, bv. 0.1235/0.8765 voor 4:3 midden in 16:9
 *   RG_T4_PUMP=reload|grenade|beide   welke knop de pomp is (standaard beide)
 *   RG_T4_KBD=pad         een evdev-toetsenbord erbij (1/2 START, 5/6 munt, 7 service, 8 test, 0 biljet, op/neer volume, 9 tilt)
 *   RG_T4_AUTOSTART=<s>   eenmalig START 1 na zoveel seconden (voor "Press Start To Continue")
 *   RG_T4_LOG=1           een regel per flank en per richtpunt-sprong
 *   RG_GLUT_DISPLAY_FROM_ENV=1   glutInit herschrijven
 * Bouwen: kaal gcc -m32 (CMakeLists.txt; op SteamOS ontbreekt gnu/stubs-32.h: een lege kopie in een tijdelijke map is genoeg).
 * Toets: tests/t4_io_test.sh (-DRG_T4_TEST: RG_T4_TEST_MEM legt een eigen geheugen onder de game-adressen).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

/* Adressen in de game (ET_EXEC, sha256 798dec07...): de bytes op AIM_GETTER moeten kloppen, anders schrijft de hulp niets. */
#define AIM_GETTER 0x842e36fu
static const unsigned char AIM_SIG[13] = {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x0f, 0xb6, 0x80, 0x34, 0xf3, 0xb0, 0x08};
#define AIM_BASE 0x8b0da00u
#define AIM_STAP 0x30u

/* De veertien schakelaars van het bord: index = byte*2 + nibble (byte 0 = frame-byte 2). */
enum { T_TRIG1, T_TRIG2, T_PUMP1, T_PUMP2, T_START1, T_START2, T_COIN1, T_COIN2, T_VOLUP, T_VOLDN, T_TEST, T_SERVICE, T_TILT, T_BILL, T_N };
static const char *const NAAM[T_N] = {"TREKKER 1", "TREKKER 2", "POMP 1", "POMP 2", "START 1", "START 2", "MUNT 1", "MUNT 2",
                                      "VOLUME OP", "VOLUME NEER", "TEST", "SERVICE", "TILT", "BILJET"};

static int g_log, g_fd = -1, g_dr = -1, g_pomp;   /* g_pomp: 0 reload+grenade, 1 reload, 2 grenade */
static unsigned char g_cnt[T_N];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t g_mem;   /* 0 voor de game; de toets legt er een eigen stuk geheugen onder */

/* bordbeeld (RawThrillsGUN, packaging/tp-shm-profiles.json): bits op offset 8: 0 test, 1 service, 2/3 munt, 4 start1, 5 trekker1,
   6 granaat1, 7 herladen1, 8 start2, 9 trekker2, 10 granaat2, 11 herladen2, 12/13 volume op/neer. Assen: offset 12, 16, 20, 24. */
static uint32_t stand_t(uint32_t b, int t)
{
    uint32_t pm = g_pomp == 1 ? 0x80u : g_pomp == 2 ? 0x40u : 0xC0u;   /* pomp 1: herladen (bit 7) en/of granaat (bit 6) */
    uint32_t pm2 = pm << 4;                                              /* pomp 2: bit 11 / 10 */
    switch (t) {
    case T_TRIG1: return (b >> 5) & 1;
    case T_TRIG2: return (b >> 9) & 1;
    case T_PUMP1: return (b & pm) != 0;
    case T_PUMP2: return (b & pm2) != 0;
    case T_START1: return (b >> 4) & 1;
    case T_START2: return (b >> 8) & 1;
    case T_COIN1: return (b >> 2) & 1;
    case T_COIN2: return (b >> 3) & 1;
    case T_VOLUP: return (b >> 12) & 1;
    case T_VOLDN: return (b >> 13) & 1;
    case T_TEST: return b & 1;
    case T_SERVICE: return (b >> 1) & 1;
    default: return 0;
    }
}

static void stuur_frame(void)
{
    char f[48];
    int n = snprintf(f, sizeof f, "\nS80");
    for (int i = 0; i < T_N; i += 2) n += snprintf(f + n, sizeof f - (size_t)n, "%02x", (g_cnt[i] << 4) | g_cnt[i + 1]);
    n += snprintf(f + n, sizeof f - (size_t)n, "\r");
    if (g_fd >= 0 && write(g_fd, f, (size_t)n) < 0 && g_log) fprintf(stderr, "rg-t4-io: write: %s\n", strerror(errno));
}

static void flank(int t, int nu)
{
    pthread_mutex_lock(&g_mu);
    g_cnt[t] = (unsigned char)((g_cnt[t] + 1) & 0xf);
    stuur_frame();
    pthread_mutex_unlock(&g_mu);
    if (g_log) fprintf(stderr, "rg-t4-io: %s %s (teller %x)\n", NAAM[t], nu ? "in" : "los", g_cnt[t]);
}

static void druk(int t)
{
    flank(t, 1);
    struct timespec ts = {0, 120 * 1000 * 1000};
    nanosleep(&ts, NULL);
    flank(t, 0);
}

static int in_maps(uintptr_t a, size_t len, int schrijven)
{
    if (g_mem) return 1;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char l[256];
    int ok = 0;
    while (fgets(l, sizeof l, f)) {
        unsigned long lo, hi; char p[8];
        if (sscanf(l, "%lx-%lx %7s", &lo, &hi, p) == 3 && a >= lo && a + len <= hi && p[0] == 'r' && (!schrijven || p[1] == 'w')) { ok = 1; break; }
    }
    fclose(f);
    return ok;
}

static int build_klopt(void)
{
    if (g_mem) return 1;
    return in_maps(AIM_GETTER, sizeof AIM_SIG, 0) && memcmp((const void *)(uintptr_t)AIM_GETTER, AIM_SIG, sizeof AIM_SIG) == 0;
}

static double env_d(const char *k, double d) { const char *v = getenv(k); return v && *v ? atof(v) : d; }

/* een asbyte uit het bordbeeld -> pixel in 0..max; buiten beeld (<=1 of >=254) = -1; binnen het scherm loopt de as 2..253 */
static double g_lo = 0.0, g_hi = 1.0;   /* de strook van het scherm waar de game in staat (RG_T4_AIM_X0/X1/Y0/Y1; gamescope zet een 4:3-venster midden) */
static float as_naar_px(uint8_t b, double max, int inv, double lo, double hi)
{
    if (b <= 1 || b >= 254) return -1.0f;
    double f = ((double)b - 2.0) / 251.0;
    if (inv) f = 1.0 - f;
    f = (f - lo) / (hi - lo);
    if (f < 0.0 || f > 1.0) return -1.0f;   /* naast het speelveld = geen richtpunt */
    return (float)(f * max);
}

static double g_ylo = 0.0, g_yhi = 1.0;

static void schrijf_aim(const uint8_t *beeld, int inx, int iny, double w, double h, float *laatst)
{
    for (int g = 0; g < 2; g++) {
        float x = as_naar_px(beeld[12 + 8 * g], w, inx, g_lo, g_hi), y = as_naar_px(beeld[16 + 8 * g], h, iny, g_ylo, g_yhi);
        if (x < 0 || y < 0) x = y = -1.0f;
        volatile float *p = (volatile float *)(g_mem + AIM_BASE + (uintptr_t)g * AIM_STAP);
        p[0] = x;
        p[1] = y;
        if (g_log && ((x < 0) != (laatst[2 * g] < 0) || (x >= 0 && ((int)x / 200 != (int)laatst[2 * g] / 200))))
            fprintf(stderr, "rg-t4-io: richtpunt %d = %.0f,%.0f\n", g + 1, (double)x, (double)y);
        laatst[2 * g] = x;
        laatst[2 * g + 1] = y;
    }
}

static int open_lijn(const char *dev, int wacht)
{
    int fd = -1;
    for (int i = 0; i < (wacht ? 600 : 1) && fd < 0; ++i) {   /* tot 60 s wachten tot de lader het pad heeft */
        fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0 && wacht) { struct timespec ts = {0, 100 * 1000 * 1000}; nanosleep(&ts, NULL); }
    }
    if (fd < 0) { if (wacht) fprintf(stderr, "rg-t4-io: %s niet te openen: %s\n", dev, strerror(errno)); return -1; }
    struct termios tio;
    if (tcgetattr(fd, &tio) == 0) { cfmakeraw(&tio); tcsetattr(fd, TCSANOW, &tio); }
    return fd;
}

static void *draad(void *arg)
{
    (void)arg;
    const char *dev = getenv("RG_T4_JAMMA_DEV");
    if (!dev || !*dev) dev = "/tmp/ttyJamma";
    g_fd = open_lijn(dev, 1);
    if (g_fd < 0) return NULL;
    const char *dd = getenv("RG_T4_DRAIN_DEV");
    if (!dd) dd = "/tmp/ttySierra";
    if (*dd) g_dr = open_lijn(dd, 1);
    pthread_mutex_lock(&g_mu);
    stuur_frame();   /* de nulmeting */
    pthread_mutex_unlock(&g_mu);

    const char *kbd = getenv("RG_T4_KBD");
    int kfd = kbd && *kbd ? open(kbd, O_RDONLY | O_NONBLOCK) : -1;
    const char *as = getenv("RG_T4_AUTOSTART");
    double auto_s = as && *as ? atof(as) : -1;
    const char *shm = getenv("RG_T4_SHM");
    if (!shm || !*shm) shm = "/dev/shm/ravengun_JvsState";
    int sfd = -1;
    const char *pk = getenv("RG_T4_PUMP");
    g_pomp = pk && !strcmp(pk, "reload") ? 1 : pk && !strcmp(pk, "grenade") ? 2 : 0;
    const char *ea = getenv("RG_T4_AIM");
    int aim_aan = !(ea && !strcmp(ea, "0"));
    double aw = env_d("RG_T4_AIM_W", 800), ah = env_d("RG_T4_AIM_H", 600);
    int inx = getenv("RG_T4_AIM_INVX") != NULL, iny = getenv("RG_T4_AIM_INVY") != NULL;
    g_lo = env_d("RG_T4_AIM_X0", 0.0); g_hi = env_d("RG_T4_AIM_X1", 1.0); g_ylo = env_d("RG_T4_AIM_Y0", 0.0); g_yhi = env_d("RG_T4_AIM_Y1", 1.0);
    if (g_hi <= g_lo) { g_lo = 0.0; g_hi = 1.0; }
    if (g_yhi <= g_ylo) { g_ylo = 0.0; g_yhi = 1.0; }
    int bk = aim_aan ? build_klopt() : 0;
    int aim_ok = aim_aan && bk && in_maps(AIM_BASE, 2 * AIM_STAP, 1);
    fprintf(stderr, "rg-t4-io: lijn %s, richtpunt %s (build %s)\n", dev, aim_ok ? "AAN" : "UIT", !aim_aan ? "n.v.t." : bk ? "klopt" : "klopt NIET");
    uint32_t vorig = 0;
    float laatst[4] = {-9, -9, -9, -9};
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int auto_gedaan = 0, tel = 0;

    for (;;) {
        struct pollfd p[3] = {{g_fd, POLLIN, 0}, {g_dr, POLLIN, 0}, {kfd, POLLIN, 0}};
        poll(p, 3, 10);   /* een negatieve fd wordt genegeerd */
        char buf[512];
        while (read(g_fd, buf, sizeof buf) > 0) {}
        if (g_dr >= 0) while (read(g_dr, buf, sizeof buf) > 0) {}
        if (kfd >= 0 && (p[2].revents & POLLIN)) {
            struct input_event ev;
            while (read(kfd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
                if (ev.type != EV_KEY || ev.value != 1) continue;
                switch (ev.code) {
                case KEY_1: druk(T_START1); break;
                case KEY_2: druk(T_START2); break;
                case KEY_5: druk(T_COIN1); break;
                case KEY_6: druk(T_COIN2); break;
                case KEY_7: druk(T_SERVICE); break;
                case KEY_8: druk(T_TEST); break;
                case KEY_9: druk(T_TILT); break;
                case KEY_0: druk(T_BILL); break;
                case KEY_UP: druk(T_VOLUP); break;
                case KEY_DOWN: druk(T_VOLDN); break;
                default: break;
                }
            }
        }
        if (sfd < 0 && (tel++ % 100) == 0) sfd = open(shm, O_RDONLY);
        if (sfd >= 0) {
            uint8_t beeld[64];
            if (pread(sfd, beeld, sizeof beeld, 0) == (ssize_t)sizeof beeld) {
                uint32_t bits = (uint32_t)beeld[8] | ((uint32_t)beeld[9] << 8) | ((uint32_t)beeld[10] << 16) | ((uint32_t)beeld[11] << 24);
                for (int t = 0; t < T_N; t++) {
                    uint32_t nu = stand_t(bits, t), was = stand_t(vorig, t);
                    if (nu != was) flank(t, (int)nu);
                }
                vorig = bits;
                if (aim_ok) schrijf_aim(beeld, inx, iny, aw, ah, laatst);
            } else { close(sfd); sfd = -1; }
        }
        if (auto_s >= 0 && !auto_gedaan) {
            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if ((double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9 >= auto_s) { auto_gedaan = 1; druk(T_START1); }
        }
    }
    return NULL;
}

void glutInit(int *argcp, char **argv)
{
    static void (*echt)(int *, char **);
    if (!echt) echt = (void (*)(int *, char **))dlsym(RTLD_NEXT, "glutInit");
    const char *d = getenv("DISPLAY");
    if (argcp && argv && getenv("RG_GLUT_DISPLAY_FROM_ENV") && d && *d)
        for (int i = 0; i + 1 < *argcp; i++)
            if (argv[i] && !strcmp(argv[i], "-display")) argv[i + 1] = (char *)d;
    if (echt) echt(argcp, argv);
}

__attribute__((constructor)) static void start(void)
{
    g_log = getenv("RG_T4_LOG") != NULL;
#ifdef RG_T4_TEST
    const char *m = getenv("RG_T4_TEST_MEM");
    if (m) g_mem = (uintptr_t)strtoul(m, NULL, 16);
#endif
    pthread_t t;
    pthread_create(&t, NULL, draad, NULL);
    pthread_detach(t);
}
