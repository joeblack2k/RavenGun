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
 * hangt ze aan en logt elke USB-overdracht die naar /dev/bus/usb gaat. Dat geeft de bytes op
 * de draad — de besturingsdescriptor met VID/PID, de setup-pakketten en de bulk-buffers —
 * zonder dat er één adres van de game bekend hoeft te zijn.
 *
 * GEBRUIK (in het spelproces, naast halo.so en librg_rt_input.so):
 *   LD_PRELOAD=/rg/lib/libusb-tap.so RG_USB_LOG=/tmp/usb-tap.log ./game -f1920x1080 -n
 *   RG_USB_LOG_STDOUT=1   ook naar stderr
 *   RG_USB_MAX=<n>        maximum aantal regels (standaard 20000)
 *   RG_USB_RAW=1          volledige buffers in plaats van de eerste 64 bytes
 *   RG_USB_DEVROOT=<pad>  andere wortel dan /dev/bus/usb
 *
 * De tap verandert geen gedrag: hij roept altijd de echte functie aan en geeft het resultaat
 * onveranderd terug. Hij schrijft niets naar de NAS.
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
static long g_max = 20000, g_lines, g_raw;
static char g_devroot[4096] = "/dev/bus/usb/";
static char g_fdname[4096][64];

static int (*real_open)(const char *, int, ...);
static int (*real_open64)(const char *, int, ...);
static int (*real_openat)(int, const char *, int, ...);
static int (*real_ioctl)(int, unsigned long, ...);
static long (*real_syscall)(long, ...);
static ssize_t (*real_read)(int, void *, size_t);
static ssize_t (*real_write)(int, const void *, size_t);
static int (*real_close)(int);
static int g_stdout;

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
    g_stdout = getenv("RG_USB_LOG_STDOUT") != NULL;
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

static void note_fd(int fd, const char *path)
{
    if (fd >= 0 && fd < 4096) {
        pthread_mutex_lock(&g_lock);
        snprintf(g_fdname[fd], sizeof g_fdname[fd], "%s", path);
        pthread_mutex_unlock(&g_lock);
    }
}

static int fd_is_usb(int fd)
{
    return fd >= 0 && fd < 4096 && g_fdname[fd][0] != 0;
}

/* ---- de URB-afhandeling, gedeeld door ioctl() en syscall(SYS_ioctl) ---- */

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
        if (u && u->endpoint & USB_DIR_IN)
            hexdump("  in", fd, u->buffer, (size_t)u->actual_length);
        else if (u)
            hexdump("  out", fd, u->buffer, (size_t)(u->actual_length ? u->actual_length : u->buffer_length));
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
    if (!fd_is_usb(fd)) return real_ioctl(fd, req, arg);

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

    if (number == SYS_ioctl && fd_is_usb((int)a1)) {
        void *buf;
        int len;
        int kind = tap_urb_pre((int)a1, (unsigned long)a2, (void *)a3, &buf, &len);
        long r = real_syscall(number, a1, a2, a3, a4, a5, a6);
        if (kind) tap_urb_post((int)a1, (unsigned long)a2, (void *)a3, buf, len, r);
        return r;
    }
    if ((number == SYS_openat || number == SYS_open) && is_usb((const char *)(number == SYS_open ? a1 : a2))) {
        long r = real_syscall(number, a1, a2, a3, a4, a5, a6);
        const char *path = (const char *)(number == SYS_open ? a1 : a2);
        say("syscall-open %s fd=%ld", path, r);
        if (r >= 0) note_fd((int)r, path);
        return r;
    }
    if ((number == SYS_read || number == SYS_write) && fd_is_usb((int)a1)) {
        long r = real_syscall(number, a1, a2, a3, a4, a5, a6);
        if (r > 0)
            hexdump(number == SYS_read ? "syscall-read" : "syscall-write",
                    (int)a1, (const void *)a2, (size_t)r);
        return r;
    }
    return real_syscall(number, a1, a2, a3, a4, a5, a6);
}

int open(const char *path, int flags, ...)
{
    tap_init();
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    int fd = real_open(path, flags, mode);
    if (is_usb(path)) { say("open %s fd=%d", path, fd); note_fd(fd, path); }
    return fd;
}

int open64(const char *path, int flags, ...)
{
    tap_init();
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    int fd = real_open64 ? real_open64(path, flags, mode) : open(path, flags, mode);
    if (is_usb(path)) { say("open64 %s fd=%d", path, fd); note_fd(fd, path); }
    return fd;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    tap_init();
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list ap; va_start(ap, flags); mode = va_arg(ap, mode_t); va_end(ap); }
    int fd = real_openat ? real_openat(dirfd, path, flags, mode) : -1;
    if (is_usb(path)) { say("openat %s fd=%d", path, fd); note_fd(fd, path); }
    return fd;
}

int close(int fd)
{
    tap_init();
    if (fd_is_usb(fd)) {
        say("close fd=%d (%s)", fd, g_fdname[fd]);
        pthread_mutex_lock(&g_lock);
        g_fdname[fd][0] = 0;
        pthread_mutex_unlock(&g_lock);
    }
    return real_close ? real_close(fd) : -1;
}

ssize_t read(int fd, void *buf, size_t n)
{
    tap_init();
    ssize_t r = real_read(fd, buf, n);
    if (fd_is_usb(fd) && r > 0) hexdump("read", fd, buf, (size_t)r);
    return r;
}

ssize_t write(int fd, const void *buf, size_t n)
{
    tap_init();
    ssize_t r = real_write(fd, buf, n);
    if (fd_is_usb(fd)) hexdump("write", fd, buf, n);
    return r;
}
