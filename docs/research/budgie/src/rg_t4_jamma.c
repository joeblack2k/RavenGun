/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-t4-jamma -- OPSTARTSKELET (niet op de bench getest) voor Terminator Salvation op de native route: de kant van het
 * JAMMA-bord die het spel op /dev/ttyS0 verwacht. i386 LD_PRELOAD-bibliotheek; de pty-koppeling zelf (/dev/ttyS0 <-> ander pad)
 * doet librg_ptylink.so (RG_PTYLINK), deze bibliotheek is de "bordkant" erachter.
 *
 * WAAROM (gemeten in patches/Terminator/ATTRACT-RUN.md §2.2-2.5, op de bench van 2026-10-13):
 *   - de schakelaars (START, munt, TEST, trekker, pomp, volume, tilt) komen NIET van /dev/js0, het toetsenbord of gedeeld
 *     geheugen, maar van een seriele "JAMMA I/O"-lijn: /dev/ttyS0, 115200 raw. Frame: LF, 'S', zestien hexcijfers (acht bytes),
 *     CR. Byte 1 heeft bit 7 gezet (0x80); bytes 2 t/m 8 zijn veertien 4-bit wikkelende overgangstellers, hoge nibble eerst:
 *       2 trigger1/trigger2   3 pomp1/pomp2   4 START1/START2   5 COIN1/COIN2   6 volume-op/-neer   7 TEST/SERVICE   8 tilt/biljet
 *     Het eerste geldige frame is de nulmeting; elke volgende ophoging is een flank (even -> oneven = indrukken, oneven -> even
 *     = loslaten). START 1 indrukken: "\nS8000000000000000\r" (nulmeting), "\nS8000001000000000\r", "\nS8000002000000000\r".
 *   - het spel schrijft zelf continu naar de lijn (P<n><hex>, W03, s); wordt dat niet gelezen, dan loopt de buffer na ~4,5 min
 *     vol en meldt het spel "JAMMA: jamma_msg() -1" (run r3). Deze bibliotheek leest dus altijd leeg.
 *
 * WAT HET DOET: een draad die het bordpad opent (standaard /tmp/ttyJamma, RG_T4_JAMMA_DEV), alles wat het spel schrijft weggooit,
 * de nulmeting stuurt en schakelaarflanken uitzendt. Bronnen van flanken:
 *   RG_T4_AUTOSTART=<s>  eenmalig START 1 indrukken na zoveel seconden (voor de waarschuwing "Press Start To Continue")
 *   RG_T4_KBD=<pad>      een evdev-toetsenbord (bv. /dev/input/ravengun-kbd): 1/2 START, 5/6 munt, 7 service, 8 test, 0 biljet,
 *                        pijl omhoog/omlaag volume, 9 tilt
 *   RG_T4_LOG=1          een regel per verzonden flank
 *
 * Ook hier: glutInit() (ATTRACT-RUN.md §5.5 punt 5): het spel geeft GLUT hard "-display :0.0" mee en sterft na ~3 s als dat niet het
 * scherm van gamescope is. Met RG_GLUT_DISPLAY_FROM_ENV=1 vervangt deze bibliotheek de waarde achter "-display" door $DISPLAY (zelfde
 * regels als de proefbibliotheek exit-log.c van de bench).
 *
 * WAT NIET BEWEZEN IS: de bibliotheek is nooit in het spelproces gedraaid (alleen tegen een proef-pty, zie build.sh); het
 * richten (de geweren) zit hier NIET in; trekker en pomp zijn niet aan toetsen of muis gekoppeld; of het spel antwoorden
 * op zijn eigen commando's nodig heeft is niet vastgesteld (het haalde attract en het testmenu zonder).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

static int g_fd = -1, g_log;
static unsigned char g_cnt[7][2];   /* tellers per schakelaar: [byte 2..8][hoog=0 / laag=1] */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

/* schakelaar -> (byte-index 0..6, nibble 0 = hoog / 1 = laag) */
struct sw { int byte, nib; };
static const struct sw SW_TRIGGER1 = {0, 0}, SW_START1 = {2, 0}, SW_START2 = {2, 1}, SW_COIN1 = {3, 0}, SW_COIN2 = {3, 1},
                       SW_VOLUP = {4, 0}, SW_VOLDN = {4, 1}, SW_TEST = {5, 0}, SW_SERVICE = {5, 1}, SW_TILT = {6, 0}, SW_BILL = {6, 1};

static void stuur_frame(void)
{
    char f[32];
    int n = snprintf(f, sizeof f, "\nS80%02x%02x%02x%02x%02x%02x%02x\r",
                     (g_cnt[0][0] << 4) | g_cnt[0][1], (g_cnt[1][0] << 4) | g_cnt[1][1], (g_cnt[2][0] << 4) | g_cnt[2][1],
                     (g_cnt[3][0] << 4) | g_cnt[3][1], (g_cnt[4][0] << 4) | g_cnt[4][1], (g_cnt[5][0] << 4) | g_cnt[5][1],
                     (g_cnt[6][0] << 4) | g_cnt[6][1]);
    if (g_fd >= 0 && write(g_fd, f, (size_t)n) < 0 && g_log) fprintf(stderr, "rg-t4-jamma: write: %s\n", strerror(errno));
}

static void flank(struct sw s)
{
    pthread_mutex_lock(&g_mu);
    g_cnt[s.byte][s.nib] = (unsigned char)((g_cnt[s.byte][s.nib] + 1) & 0xf);
    stuur_frame();
    pthread_mutex_unlock(&g_mu);
}

static void druk(struct sw s, const char *naam)
{
    if (g_log) fprintf(stderr, "rg-t4-jamma: %s\n", naam);
    flank(s);                      /* indrukken */
    struct timespec t = {0, 120 * 1000 * 1000};
    nanosleep(&t, NULL);
    flank(s);                      /* loslaten */
}

static void *draad(void *arg)
{
    (void)arg;
    const char *dev = getenv("RG_T4_JAMMA_DEV");
    if (!dev || !*dev) dev = "/tmp/ttyJamma";
    for (int i = 0; i < 600 && g_fd < 0; ++i) {   /* tot 60 s wachten tot het pad bestaat */
        g_fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (g_fd < 0) { struct timespec t = {0, 100 * 1000 * 1000}; nanosleep(&t, NULL); }
    }
    if (g_fd < 0) { fprintf(stderr, "rg-t4-jamma: %s niet te openen: %s\n", dev, strerror(errno)); return NULL; }
    struct termios tio;
    if (tcgetattr(g_fd, &tio) == 0) { cfmakeraw(&tio); tcsetattr(g_fd, TCSANOW, &tio); }
    stuur_frame();   /* de nulmeting */

    const char *kbd = getenv("RG_T4_KBD");
    int kfd = kbd && *kbd ? open(kbd, O_RDONLY | O_NONBLOCK) : -1;
    const char *as = getenv("RG_T4_AUTOSTART");
    double auto_s = as ? atof(as) : -1;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int auto_gedaan = 0;

    for (;;) {
        struct pollfd p[2] = {{g_fd, POLLIN, 0}, {kfd, POLLIN, 0}};
        poll(p, kfd >= 0 ? 2 : 1, 100);
        char buf[512];
        while (read(g_fd, buf, sizeof buf) > 0) {}   /* wat het spel schrijft: leeglezen en weggooien */
        if (kfd >= 0 && (p[1].revents & POLLIN)) {
            struct input_event ev;
            while (read(kfd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
                if (ev.type != EV_KEY || ev.value != 1) continue;   /* alleen indrukken; druk() doet het loslaten */
                switch (ev.code) {
                case KEY_1: druk(SW_START1, "START 1"); break;
                case KEY_2: druk(SW_START2, "START 2"); break;
                case KEY_5: druk(SW_COIN1, "COIN 1"); break;
                case KEY_6: druk(SW_COIN2, "COIN 2"); break;
                case KEY_7: druk(SW_SERVICE, "SERVICE"); break;
                case KEY_8: druk(SW_TEST, "TEST"); break;
                case KEY_9: druk(SW_TILT, "TILT"); break;
                case KEY_0: druk(SW_BILL, "BILL"); break;
                case KEY_UP: druk(SW_VOLUP, "VOLUME OP"); break;
                case KEY_DOWN: druk(SW_VOLDN, "VOLUME NEER"); break;
                default: break;
                }
            }
        }
        if (auto_s >= 0 && !auto_gedaan) {
            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if ((double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9 >= auto_s) {
                auto_gedaan = 1;
                druk(SW_START1, "START 1 (autostart)");
            }
        }
    }
    return NULL;
}

/* glutInit: de waarde achter "-display" uit het spel vervangen door $DISPLAY (alleen met RG_GLUT_DISPLAY_FROM_ENV). */
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
    (void)SW_TRIGGER1;
    pthread_t t;
    pthread_create(&t, NULL, draad, NULL);
    pthread_detach(t);
}
