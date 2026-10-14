/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-ptylink -- twee paden die samen één seriële verbinding zijn, zonder socat en zonder shellscript (LD_PRELOAD, i386).
 *
 * WAAROM (gemeten, objdump/readelf op de dump; patches/TargetTerrorGold/): Target: Terror Gold komt met een eigen emulator van
 * zijn kastbord (tt_emu.so, LD_PRELOAD). Op de kast koppelde tt_daemon.sh twee pseudoterminals met
 *     socat pty,link=/tmp/ttyJamma,raw,echo=0 pty,link=/dev/ttyS0,raw,echo=0
 * waarna `game` het JAMMA-bord op /dev/ttyS0 opent en tt_emu.so (in hetzelfde proces) het bord speelt op /tmp/ttyJamma. Beide
 * openen met open() (geïmporteerd uit libc; tt_emu.so ook tcgetattr/tcsetattr). In de sandbox bestaat geen van beide paden en
 * draait er geen socat; de eigenaar wil geen shellketen. Deze bibliotheek maakt het koppel zelf: bij de eerste open() van een
 * van de twee paden een pty (posix_openpt), de slaaf rauw (cfmakeraw), en elke open() van pad A krijgt een kopie van de slaaf,
 * elke open() van pad B een kopie van de meester. Wat de ene kant schrijft, leest de andere -- zoals bij socat.
 *
 * Gebruik: LD_PRELOAD=/pad/librg_ptylink.so[:andere] (VOOR een bibliotheek die zelf open() aanroept, zoals tt_emu.so), met
 *   RG_PTYLINK="/dev/ttyS0=/tmp/ttyJamma"       A=B: A krijgt de slaaf (een echte tty: baudrate en termios werken), B de meester.
 *                                                Meer koppels met ';' ertussen (hoogstens 4).
 *   RG_PTYLINK_LOG=1                             een regel per koppel en per open() op stderr.
 * Alleen open/open64/openat/openat64 met precies dat pad; al het andere gaat ongewijzigd naar libc. O_NONBLOCK uit de vlaggen
 * wordt op de kopie gezet; O_CREAT/O_TRUNC hebben geen betekenis en worden genegeerd. Eigen code.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define RG_MAX_KOPPEL 4

struct koppel { char a[256], b[256]; int slaaf, meester; };
static struct koppel K[RG_MAX_KOPPEL];
static int nk = -1, log_on;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

typedef int (*open_fn)(const char *, int, ...);
typedef int (*openat_fn)(int, const char *, int, ...);

static void meld(const char *fmt, ...) {
    if (!log_on) return;
    char b[600];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    fprintf(stderr, "[rg_ptylink] %s\n", b);
    fflush(stderr);
}

static void lees_omgeving(void) {
    if (nk >= 0) return;
    nk = 0;
    const char *l = getenv("RG_PTYLINK_LOG"); log_on = l && *l == '1';
    const char *v = getenv("RG_PTYLINK");
    if (!v || !*v) return;
    char buf[2048]; snprintf(buf, sizeof buf, "%s", v);
    char *save = 0;
    for (char *p = strtok_r(buf, ";", &save); p && nk < RG_MAX_KOPPEL; p = strtok_r(0, ";", &save)) {
        char *is = strchr(p, '=');
        if (!is || is == p || !is[1] || p[0] != '/' || is[1] != '/') { meld("koppel '%s' genegeerd (verwacht /pad/a=/pad/b)", p); continue; }
        *is = 0;
        snprintf(K[nk].a, sizeof K[nk].a, "%s", p);
        snprintf(K[nk].b, sizeof K[nk].b, "%s", is + 1);
        K[nk].slaaf = K[nk].meester = -1;
        meld("koppel %d: %s (slaaf) <-> %s (meester)", nk, K[nk].a, K[nk].b);
        nk++;
    }
}

static open_fn echte_open(void) {
    static open_fn f;
    if (!f) { void *p = dlsym(RTLD_NEXT, "open"); memcpy(&f, &p, sizeof p); }
    return f;
}

/* Het koppel maken (onder mu). 0 = gelukt. */
static int maak(struct koppel *k) {
    if (k->meester >= 0) return 0;
    int m = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) { int e = errno; if (m >= 0) close(m); errno = e; return -1; }
    char naam[128];
    if (ptsname_r(m, naam, sizeof naam) != 0) { int e = errno; close(m); errno = e; return -1; }
    int s = echte_open()(naam, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (s < 0) { int e = errno; close(m); errno = e; return -1; }
    struct termios t;
    if (tcgetattr(s, &t) == 0) { cfmakeraw(&t); tcsetattr(s, TCSANOW, &t); }   /* raw,echo=0 zoals socat */
    k->meester = m; k->slaaf = s;
    meld("koppel %s <-> %s gemaakt (%s)", k->a, k->b, naam);
    return 0;
}

/* -1 met errno als het pad bij een koppel hoort maar het koppel niet te maken was; -2 = geen koppelpad. */
static int koppel_open(const char *pad, int vlaggen) {
    lees_omgeving();
    if (!pad || nk <= 0) return -2;
    for (int i = 0; i < nk; i++) {
        int kant = !strcmp(pad, K[i].a) ? 1 : !strcmp(pad, K[i].b) ? 2 : 0;
        if (!kant) continue;
        pthread_mutex_lock(&mu);
        int r = maak(&K[i]);
        int fd = r == 0 ? fcntl(kant == 1 ? K[i].slaaf : K[i].meester, (vlaggen & O_CLOEXEC) ? F_DUPFD_CLOEXEC : F_DUPFD, 3) : -1;
        int e = errno;
        pthread_mutex_unlock(&mu);
        if (fd >= 0 && (vlaggen & O_NONBLOCK)) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        meld("open(%s) -> %d (%s)", pad, fd, kant == 1 ? "slaaf" : "meester");
        errno = e;
        return fd;
    }
    return -2;
}

static mode_t modus_van(int vlaggen, va_list ap) {
    return (vlaggen & O_CREAT) || (vlaggen & __O_TMPFILE) == __O_TMPFILE ? (mode_t)va_arg(ap, int) : 0;
}

__attribute__((visibility("default"))) int open(const char *pad, int vlaggen, ...) {
    va_list ap; va_start(ap, vlaggen); mode_t m = modus_van(vlaggen, ap); va_end(ap);
    int fd = koppel_open(pad, vlaggen);
    return fd != -2 ? fd : echte_open()(pad, vlaggen, m);
}
__attribute__((visibility("default"))) int open64(const char *pad, int vlaggen, ...) {
    va_list ap; va_start(ap, vlaggen); mode_t m = modus_van(vlaggen, ap); va_end(ap);
    int fd = koppel_open(pad, vlaggen);
    if (fd != -2) return fd;
    static open_fn f;
    if (!f) { void *p = dlsym(RTLD_NEXT, "open64"); memcpy(&f, &p, sizeof p); }
    return f(pad, vlaggen, m);
}
static int via_openat(const char *naam, int dirfd, const char *pad, int vlaggen, mode_t m) {
    int fd = (pad && pad[0] == '/') || dirfd == AT_FDCWD ? koppel_open(pad, vlaggen) : -2;
    if (fd != -2) return fd;
    static openat_fn f[2];
    int i = !strcmp(naam, "openat64");
    if (!f[i]) { void *p = dlsym(RTLD_NEXT, naam); memcpy(&f[i], &p, sizeof p); }
    return f[i](dirfd, pad, vlaggen, m);
}
__attribute__((visibility("default"))) int openat(int dirfd, const char *pad, int vlaggen, ...) {
    va_list ap; va_start(ap, vlaggen); mode_t m = modus_van(vlaggen, ap); va_end(ap);
    return via_openat("openat", dirfd, pad, vlaggen, m);
}
__attribute__((visibility("default"))) int openat64(int dirfd, const char *pad, int vlaggen, ...) {
    va_list ap; va_start(ap, vlaggen); mode_t m = modus_van(vlaggen, ap); va_end(ap);
    return via_openat("openat64", dirfd, pad, vlaggen, m);
}
