/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libusb-tap.c — meetinstrument voor het FTDI-pistoolbord van Halo: Fireteam Raven.
 *
 * WAAROM DIT ER IS
 * ----------------
 * libftdi en libusb zijn STATISCH in `game` gelinkt (geen DT_NEEDED voor beide; de strings
 * van beide bibliotheken zitten in het bestand). `LD_PRELOAD` kan die aanroepen dus niet
 * onderscheppen zonder de ingangspunten met de hand te herschrijven.
 *
 * Maar libusb praat met de kernel via gewone libc-aanroepen, en DIE zijn wél dynamisch:
 * `game` importeert `open`, `ioctl`, `syscall`, `read`, `write`, `close` uit libc. Deze tap
 * hangt ze aan en logt:
 *
 *   1. /dev/bus/usb/<bus>/<dev>   — het echte apparaatverkeer (USBDEVFS_CONTROL, _BULK,
 *      _SUBMITURB, _REAPURB{NDELAY}) met de buffers erbij: dit zijn de bytes op de draad;
 *   2. /sys/bus/usb/devices/...   — de opsomming. libusb leest op Linux de apparaatlijst en de
 *      descriptors uit sysfs, niet via usbfs. Zonder bord is dit het enige wat je ziet, en het
 *      laat wél zien WELKE apparaten de game heeft bekeken (en met welke VID/PID, uit de
 *      `descriptors`-bytes).
 *
 * Daarom werkt dit zonder één adres van de game te kennen.
 *
 * GEBRUIK (in het spelproces, naast halo.so en librg_rt_input.so):
 *   LD_PRELOAD=/rg/lib/libusb-tap.so RG_USB_LOG=/tmp/usb-tap.log ./game -f1920x1080 -n
 *   RG_USB_LOG_STDOUT=1    ook naar stderr
 *   RG_USB_MAX=<n>         maximum aantal regels (standaard 20000)
 *   RG_USB_RAW=1           volledige buffers in plaats van de eerste 64 bytes
 *   RG_USB_DEVROOT=<pad>   andere usbfs-wortel (standaard /dev/bus/usb)
 *   RG_USB_SYSFS=<pad>     andere sysfs-wortel (standaard /sys/bus/usb/devices)
 *   RG_USB_SYSFS_UIT=1     alleen /dev/bus/usb loggen, sysfs overslaan
 *
 * De tap verandert geen gedrag: hij roept altijd de echte functie aan en geeft het resultaat
 * onveranderd terug. Hij schrijft niets naar de NAS.
 *
 * De eerste regel van het logboek is altijd `tap start ...`. Staat die er niet, dan is de tap
 * niet geladen (LD_PRELOAD of een glibc-eis) — dat is iets anders dan "geen verkeer".
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <time.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>

static FILE *g_log;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static long g_max = 20000, g_lines, g_raw, g_sysfs_uit;
static char g_devroot[4096] = "/dev/bus/usb/";
static char g_sysfs[4096] = "/sys/bus/usb/devices/";

#define KIND_NONE 0
#define KIND_USB 1
#define KIND_SYSFS_DESC 2
#define KIND_SYSFS_ATTR 3
static char g_fdname[4096][64];
static unsigned char g_fdkind[4096];

static int (*real_open)(const char *, int, ...);
static int (*real_open64)(const char *, int, ...);
static int (*real_openat)(int, const char *, int, ...);
static int (*real_ioctl)(int, unsigned long, ...);
static long (*real_syscall)(long, ...);
static ssize_t (*real_read)(int, void *, size_t);
static ssize_t (*real_write)(int, const void *, size_t);
static int (*real_close)(int);
static int g_stdout;

static void say(const char *fmt, ...);
static void sysfs_attr_meld(const char *path, int fd);

static void tap_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    real_open = dlsym(RTLD_NEXT, "open");
    real_open64 = dlsym(RTLD_NEXT, "open64");
    real_openat = dlsym(RTLD_NEXT, "openat");
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    real_syscall = dlsym(RTLD_NEXT, "syscall");
    real_read = dlsym(RTLD_NEXT, "read");
    real_write = dlsym(RTLD_NEXT, "write");
    real_close = dlsym(RTLD_NEXT, "close");
    const char *p = getenv("RG_USB_LOG");
    g_log = p && *p ? fopen(p, "a") : NULL;
    const char *m = getenv("RG_USB_MAX");
    if (m && *m) g_max = atol(m);
    const char *r = getenv("RG_USB_RAW");
    g_raw = r && *r == '1';
    const char *d = getenv("RG_USB_DEVROOT");
    if (d && *d) snprintf(g_devroot, sizeof g_devroot, "%s", d);
    const char *s = getenv("RG_USB_SYSFS");
    if (s && *s) snprintf(g_sysfs, sizeof g_sysfs, "%s", s);
    const char *u = getenv("RG_USB_SYSFS_UIT");
    g_sysfs_uit = u && *u == '1';
    g_stdout = getenv("RG_USB_LOG_STDOUT") != NULL;
    say("tap start usbfs=%s sysfs=%s%s max=%ld", g_devroot, g_sysfs,
        g_sysfs_uit ? " (uit)" : "", g_max);
}

static void say(const char *fmt, ...)
{
    if (!g_log && !g_stdout) return;
    pthread_mutex_lock(&g_lock);
    if (g_lines++ > g_max) { pthread_mutex_unlock(&g_lock); return; }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (g_log) {
        fprintf(g_log, "%ld.%06ld %s\n", (long)ts.tv_sec, ts.tv_nsec / 1000, b);
        fflush(g_log);
    }
    if (g_stdout)
        fprintf(stderr, "[usb-tap] %ld.%06ld %s\n", (long)ts.tv_sec, ts.tv_nsec / 1000, b);
    pthread_mutex_unlock(&g_lock);
}

static void hexdump(const char *what, int fd, const void *p, size_t n)
{
    if (!p || !n) return;
    const unsigned char *q = p;
    size_t lim = g_raw ? n : (n > 64 ? 64 : n);
    char line[512];
    size_t o = 0;
    o += snprintf(line + o, sizeof line - o, "%s fd=%d len=%zu data:", what, fd, n);
    for (size_t i = 0; i < lim && o + 4 < sizeof line; i++)
        o += snprintf(line + o, sizeof line - o, " %02x", q[i]);
    if (lim < n) snprintf(line + o, sizeof line - o, " ... (%zu more)", n - lim);
    say("%s", line);
}

static int is_usb(const char *path)
{
    return path && strncmp(path, g_devroot, strlen(g_devroot)) == 0;
}

static const char *base(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* Soort aan de hand van het pad; -1 = niet interessant. */
static int pad_soort(const char *path)
{
    if (!path) return -1;
    if (is_usb(path)) return KIND_USB;
    if (g_sysfs_uit) return -1;
    if (strncmp(path, g_sysfs, strlen(g_sysfs)) != 0) return -1;
    return strcmp(base(path), "descriptors") == 0 ? KIND_SYSFS_DESC : KIND_SYSFS_ATTR;
}

static void note_fd(int fd, const char *path, int soort)
{
    if (fd >= 0 && fd < 4096) {
        pthread_mutex_lock(&g_lock);
        snprintf(g_fdname[fd], sizeof g_fdname[fd], "%s", path);
        g_fdkind[fd] = (unsigned char)soort;
        pthread_mutex_unlock(&g_lock);
    }
}

static int fd_kind(int fd)
{
    return (fd >= 0 && fd < 4096) ? g_fdkind[fd] : KIND_NONE;
}

/* ---- usbfs: de URB-afhandeling, gedeeld door ioctl() en syscall(SYS_ioctl) ---- */

static int tap_urb_pre(int fd, unsigned long req, void *arg, void **buf, int *len)
{
    *buf = NULL;
    *len = 0;
    switch (req) {
    case USBDEVFS_CONTROL: {
        struct usbdevfs_ctrltransfer *c = arg;
        say("ctrl fd=%d bmRequestType=0x%02x bRequest=0x%02x wValue=0x%04x wIndex=0x%04x wLength=%u",
            fd, c->bRequestType, c->bRequest, c->wValue, c->wIndex, c->wLength);
        *buf = c->data;
        *len = c->wLength;
        return 1;
    }
    case USBDEVFS_BULK: {
        struct usbdevfs_bulktransfer *b = arg;
        say("bulk fd=%d ep=0x%02x len=%u timeout=%u", fd, b->ep, b->len, b->timeout);
        *buf = b->data;
        *len = b->len;
        return 1;
    }
    case USBDEVFS_SUBMITURB: {
        struct usbdevfs_urb *u = arg;
        say("urbsub fd=%d type=%d ep=0x%02x flags=0x%x len=%d",
            fd, u->type, u->endpoint, u->flags, u->buffer_length);
        if (u->type == USBDEVFS_URB_TYPE_CONTROL && u->buffer && u->buffer_length >= 8) {
            struct usb_ctrlrequest *r = u->buffer;
            say("  setup bmRequestType=0x%02x bRequest=0x%02x wValue=0x%04x wIndex=0x%04x wLength=%u",
                r->bRequestType, r->bRequest, r->wValue, r->wIndex, r->wLength);
            if (u->buffer_length > 8)
                hexdump("  setup-data", fd, (unsigned char *)u->buffer + 8,
                        u->buffer_length - 8);
        } else {
            hexdump("  out", fd, u->buffer, u->buffer_length);
        }
        return 1;
    }
    case USBDEVFS_REAPURB:
    case USBDEVFS_REAPURBNDELAY:
        return 2;   /* na de aanroep uitlezen */
    default:
        say("ioctl fd=%d req=0x%lx (USBDEVFS)", fd, req);
        return 0;
    }
}

static void tap_urb_post(int fd, unsigned long req, void *arg, void *buf, int len, long r)
{
    say("  -> ret=%ld errno=%d", r, r < 0 ? errno : 0);
    if (r < 0) return;
    if (req == USBDEVFS_CONTROL) {
        struct usbdevfs_ctrltransfer *c = arg;
        if (c->bRequestType & USB_DIR_IN) hexdump("  in", fd, buf, (size_t)c->wLength);
    } else if (req == USBDEVFS_BULK) {
        struct usbdevfs_bulktransfer *b = arg;
        if (b->ep & USB_DIR_IN) hexdump("  in", fd, buf, (size_t)b->len);
    } else if (req == USBDEVFS_REAPURB || req == USBDEVFS_REAPURBNDELAY) {
        struct usbdevfs_urb *u = *(struct usbdevfs_urb **)arg;
        if (u)
            say("urbreap fd=%d type=%d ep=0x%02x status=%d actual=%d len=%d",
                fd, u->type, u->endpoint, u->status, u->actual_length, u->buffer_length);
        if (u && (u->endpoint & USB_DIR_IN))
            hexdump("  in", fd, u->buffer, (size_t)u->actual_length);
        else if (u)
            hexdump("  out", fd, u->buffer,
                    (size_t)(u->actual_length ? u->actual_length : u->buffer_length));
    }
    (void)len;
}

int ioctl(int fd, unsigned long req, ...)
{
    tap_init();
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (!real_ioctl) return -1;
    if (fd_kind(fd) != KIND_USB) return real_ioctl(fd, req, arg);

    void *buf;
    int len;
    int kind = tap_urb_pre(fd, req, arg, &buf, &len);
    int r = real_ioctl(fd, req, arg);
    if (kind) tap_urb_post(fd, req, arg, buf, len, r);
    return r;
}

/* libusb kan ook via syscall() gaan; dezelfde afhandeling, als vangnet. */
long syscall(long number, ...)
{
    tap_init();
    va_list ap;
    va_start(ap, number);
    long a1 = va_arg(ap, long), a2 = va_arg(ap, long), a3 = va_arg(ap, long);
    long a4 = va_arg(ap, long), a5 = va_arg(ap, long), a6 = va_arg(ap, long);
    va_end(ap);
    if (!real_syscall) { errno = ENOSYS; return -1; }

    if (number == SYS_ioctl && fd_kind((int)a1) == KIND_USB) {
        void *buf;
        int len;
        int kind = tap_urb_pre((int)a1, (unsigned long)a2, (void *)a3, &buf, &len);
        long r = real_syscall(number, a1, a2, a3, a4, a5, a6);
        if (kind) tap_urb_post((int)a1, (unsigned long)a2, (void *)a3, buf, len, r);
        return r;
    }
    if (number == SYS_openat || number == SYS_open) {
        const char *path = (const char *)(number == SYS_open ? a1 : a2);
        int soort = pad_soort(path);
        long r = real_syscall(number, a1, a2, a3, a4, a5, a6);
        if (soort >= 0 && r >= 0) {
            if (soort == KIND_SYSFS_ATTR)
                sysfs_attr_meld(path, (int)r);
            else
                say("syscall-open %s fd=%ld", path, r);
            note_fd((int)r, path, soort);
        }
        return r;
    }
    if ((number == SYS_read || number == SYS_write) && fd_kind((int)a1) >= KIND_USB) {
        long r = real_syscall(number, a1, a2, a3, a4, a5, a6);
        if (r > 0 && (fd_kind((int)a1) == KIND_USB || fd_kind((int)a1) == KIND_SYSFS_DESC))
            hexdump(number == SYS_read ? "syscall-read" : "syscall-write",
                    (int)a1, (const void *)a2, (size_t)r);
        return r;
    }
    return real_syscall(number, a1, a2, a3, a4, a5, a6);
}

/*
 * De sysfs-attributen. libusb leest de APPARAATLIJST hierlangs -- idVendor, idProduct, busnum, devnum
 * -- en NIET via `descriptors`. Zonder deze regels ziet een GESLAAGDE opsomming zonder bord er
 * precies zo uit als een spel dat zijn FTDI-pad nooit bereikt: `tap start` en verder niets. Dat is de
 * valkuil die run 1 onuitlegbaar maakte, dus nu wordt elk attribuut gelogd MET zijn waarde, en elk
 * gevonden apparaat één keer samengevat als VID:PID.
 */
#define MAX_DEV 32
static struct {
    char dir[256];
    char vid[16];
    char pid[16];
    int gemeld;
} g_dev[MAX_DEV];
static int g_ndev;

static void sysfs_attr_meld(const char *path, int fd)
{
    char buf[64];
    char dir[256];
    const char *basis = base(path);
    const char *slash = strrchr(path, '/');
    ssize_t n;
    int i, plek = -1;

    memset(buf, 0, sizeof buf);
    /* pread: het bestand lezen zonder de positie van de aanroeper te verstoren */
    n = pread(fd, buf, sizeof buf - 1, 0);
    if (n < 0)
        n = 0;
    buf[n] = 0;
    {
        char *p;
        for (p = buf; *p; p++)
            if (*p == '\n' || *p == '\r')
                *p = ' ';
    }
    /* overtollige spaties eraf: sysfs levert "0403\n" en de vergelijking hieronder moet exact zijn */
    {
        char *p = buf + strlen(buf);
        while (p > buf && (p[-1] == ' ' || p[-1] == '\t'))
            *--p = 0;
    }
    say("attr %s = %s   (%s)", basis, n ? buf : "?", path);

    if (!slash || (size_t)(slash - path) + 1 > sizeof dir)
        return;
    memcpy(dir, path, (size_t)(slash - path));
    dir[slash - path] = 0;
    if (strcmp(basis, "idVendor") != 0 && strcmp(basis, "idProduct") != 0)
        return;
    for (i = 0; i < g_ndev; i++)
        if (strcmp(g_dev[i].dir, dir) == 0) {
            plek = i;
            break;
        }
    if (plek < 0) {
        if (g_ndev >= MAX_DEV)
            return;
        plek = g_ndev++;
        snprintf(g_dev[plek].dir, sizeof g_dev[plek].dir, "%s", dir);
    }
    snprintf(strcmp(basis, "idVendor") == 0 ? g_dev[plek].vid : g_dev[plek].pid,
             sizeof g_dev[plek].vid, "%s", buf);
    if (!g_dev[plek].gemeld && g_dev[plek].vid[0] && g_dev[plek].pid[0]) {
        int ftdi = (strcmp(g_dev[plek].vid, "0403") == 0 && strcmp(g_dev[plek].pid, "6001") == 0);
        g_dev[plek].gemeld = 1;
        say("APPARAAT %s idVendor=%s idProduct=%s%s", g_dev[plek].dir, g_dev[plek].vid,
            g_dev[plek].pid, ftdi ? "   <<< DIT IS HET FTDI-BORD 0403:6001" : "");
    }
}

static int open_common(const char *path, int fd, int soort)
{
    if (soort < 0 || fd < 0) return fd;
    if (soort == KIND_SYSFS_ATTR)
        sysfs_attr_meld(path, fd);
    else
        say("open %s fd=%d", path, fd);
    note_fd(fd, path, soort);
    return fd;
}

int open(const char *path, int flags, ...)
{
    tap_init();
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    return open_common(path, real_open(path, flags, mode), pad_soort(path));
}

int open64(const char *path, int flags, ...)
{
    tap_init();
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    int fd = real_open64 ? real_open64(path, flags, mode) : open(path, flags, mode);
    return open_common(path, fd, pad_soort(path));
}

int openat(int dirfd, const char *path, int flags, ...)
{
    tap_init();
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    int fd = real_openat ? real_openat(dirfd, path, flags, mode) : -1;
    return open_common(path, fd, pad_soort(path));
}

int close(int fd)
{
    tap_init();
    int k = fd_kind(fd);
    if (k != KIND_NONE) {
        if (k != KIND_SYSFS_ATTR) say("close fd=%d (%s)", fd, g_fdname[fd]);
        pthread_mutex_lock(&g_lock);
        g_fdkind[fd] = KIND_NONE;
        g_fdname[fd][0] = 0;
        pthread_mutex_unlock(&g_lock);
    }
    return real_close ? real_close(fd) : -1;
}

ssize_t read(int fd, void *buf, size_t n)
{
    tap_init();
    ssize_t r = real_read(fd, buf, n);
    int k = fd_kind(fd);
    if (r > 0 && (k == KIND_USB || k == KIND_SYSFS_DESC)) hexdump("read", fd, buf, (size_t)r);
    return r;
}

ssize_t write(int fd, const void *buf, size_t n)
{
    tap_init();
    ssize_t r = real_write(fd, buf, n);
    if (fd_kind(fd) == KIND_USB) hexdump("write", fd, buf, n);
    return r;
}
