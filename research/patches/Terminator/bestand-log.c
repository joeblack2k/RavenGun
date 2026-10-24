/* bestand-log.c - 32-bits LD_PRELOAD-bibliotheek, de BESTANDSWAARNEMER (geen tracer).
 *
 * Waarom geen strace: strace is zelf een tracer, en die run zou de gast op zijn EIGEN ptrace-poort
 * laten sterven (zie BESCHERMINGSLAAG-PTRACE.md §8.1: kind 2 krijgt EPERM zodra een vreemde al
 * meeleest). Met strace zouden we dus nooit het venster bereiken waarin de bestanden geopend worden.
 * Deze bibliotheek kijkt vanuit het proces zelf mee en verandert niets.
 *
 * De gast importeert dynamisch: open, open64, fopen, fopen64, __xstat, __lxstat, __fxstat, __xstat64,
 * opendir, getcwd (gemeten met readelf). Die dekken we. op i386 loopt `stat()` via `__xstat(_STAT_VER,..)`,
 * dus een echte `stat`-aanroep van de gast komt als `__xstat` binnen.
 *
 * BELANGRIJK: het logboek wordt met RAUWE syscalls geschreven (syscall(SYS_open/SYS_write)), niet met
 * fopen. Anders zou de waarnemer zijn eigen fopen aanroepen en oneindig recurseren.
 *
 * Logboek: $RAVENGUN_BESTAND_LOG, of anders /tmp/ravengun-bestand.log
 * Regelvorm: +<seconden> pid=<n> ppid=<n> <functie> "<pad>" -> <resultaat|FOUT errno=N (naam)>
 *
 * Bouwen: gcc -m32 -shared -fPIC -O1 -o libravengun-bestand-log.so bestand-log.c -ldl
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>

static double t0;
static int t0_gezet;

static double nu(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* RAUWE syscalls: nooit via fopen/open, anders recursie in onze eigen wrappers. */
static void meld(const char *fmt, ...)
{
    char buf[1024];
    char kern[768];
    va_list ap;
    int n, fd, pre;
    const char *pad = getenv("RAVENGUN_BESTAND_LOG");

    if (!pad || !*pad) pad = "/tmp/ravengun-bestand.log";
    if (!t0_gezet) { t0 = nu(); t0_gezet = 1; }

    va_start(ap, fmt);
    vsnprintf(kern, sizeof kern, fmt, ap);
    va_end(ap);

    pre = snprintf(buf, sizeof buf, "+%9.6f pid=%-6d ppid=%-6d ", nu() - t0,
                   (int)getpid(), (int)getppid());
    n = pre + snprintf(buf + pre, sizeof buf - pre, "%s\n", kern);
    if (n <= 0 || n >= (int)sizeof buf) n = (int)sizeof buf - 1;

    fd = (int)syscall(SYS_open, pad, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        ssize_t r = syscall(SYS_write, fd, buf, (size_t)n);
        (void)r;
        syscall(SYS_close, fd);
    }
}

/* De vlaggen meeloggen is GEEN luxe: of de gast een bestand wil LEZEN of SCHRIJVEN bepaalt of je het
   met een kopie van bestaande inhoud moet leveren of met een leeg schrijfbaar bestand. Zonder dit veld
   zou "hij mist syslog_bb3.txt" tot de verkeerde maatregel leiden. */
static const char *flags_naam(int f)
{
    static char buf[128];
    int acc = f & O_ACCMODE;
    snprintf(buf, sizeof buf, "%s%s%s%s%s%s",
             acc == O_RDONLY ? "O_RDONLY" : acc == O_WRONLY ? "O_WRONLY" : "O_RDWR",
             (f & O_CREAT)   ? "|O_CREAT"   : "",
             (f & O_APPEND)  ? "|O_APPEND"  : "",
             (f & O_TRUNC)   ? "|O_TRUNC"   : "",
             (f & O_EXCL)    ? "|O_EXCL"    : "",
             (f & O_NONBLOCK)? "|O_NONBLOCK": "");
    return buf;
}

static void meld_resultaat(const char *fn, const char *pad, long r, int bewaar_errno)
{
    if (r >= 0)
        meld("%-10s \"%s\" -> ok (%ld)", fn, pad ? pad : "(null)", r);
    else
        meld("%-10s \"%s\" -> FOUT errno=%d (%s)", fn, pad ? pad : "(null)", bewaar_errno,
             strerror(bewaar_errno));
}

static void meld_resultaat_flags(const char *fn, const char *pad, int flags, long r, int bewaar_errno)
{
    if (r >= 0)
        meld("%-10s \"%s\" flags=0x%x(%s) -> ok (%ld)", fn, pad ? pad : "(null)", flags,
             flags_naam(flags), r);
    else
        meld("%-10s \"%s\" flags=0x%x(%s) -> FOUT errno=%d (%s)", fn, pad ? pad : "(null)", flags,
             flags_naam(flags), bewaar_errno, strerror(bewaar_errno));
}

static void meld_resultaat_mode(const char *fn, const char *pad, const char *mode, long r, int bewaar_errno)
{
    if (r >= 0)
        meld("%-10s \"%s\" mode=\"%s\" -> ok (%ld)", fn, pad ? pad : "(null)", mode ? mode : "?", r);
    else
        meld("%-10s \"%s\" mode=\"%s\" -> FOUT errno=%d (%s)", fn, pad ? pad : "(null)",
             mode ? mode : "?", bewaar_errno, strerror(bewaar_errno));
}

/* ---------------------------------------------------------------- open / openat */

int open(const char *pad, int flags, ...)
{
    static int (*echt)(const char *, int, ...);
    mode_t mode = 0;
    int r;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap);
    }
    if (!echt) echt = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
    if (!echt) { errno = ENOSYS; return -1; }
    errno = 0; r = echt(pad, flags, mode);
    { int e = errno; meld_resultaat_flags("open", pad, flags, r, e); errno = e; }
    return r;
}

int open64(const char *pad, int flags, ...)
{
    static int (*echt)(const char *, int, ...);
    mode_t mode = 0;
    int r;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap);
    }
    if (!echt) echt = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open64");
    if (!echt) { errno = ENOSYS; return -1; }
    errno = 0; r = echt(pad, flags, mode);
    { int e = errno; meld_resultaat_flags("open64", pad, flags, r, e); errno = e; }
    return r;
}

int openat(int dirfd, const char *pad, int flags, ...)
{
    static int (*echt)(int, const char *, int, ...);
    mode_t mode = 0;
    int r;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap);
    }
    if (!echt) echt = (int (*)(int, const char *, int, ...))dlsym(RTLD_NEXT, "openat");
    if (!echt) { errno = ENOSYS; return -1; }
    errno = 0; r = echt(dirfd, pad, flags, mode);
    { int e = errno; meld_resultaat_flags("openat", pad, flags, r, e); errno = e; }
    return r;
}

/* ---------------------------------------------------------------- fopen / opendir */

FILE *fopen(const char *pad, const char *mode)
{
    static FILE *(*echt)(const char *, const char *);
    FILE *r;
    if (!echt) echt = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen");
    if (!echt) { errno = ENOSYS; return NULL; }
    errno = 0; r = echt(pad, mode);
    { int e = errno; meld_resultaat_mode("fopen", pad, mode, r ? 1 : -1, e); errno = e; }
    return r;
}

FILE *fopen64(const char *pad, const char *mode)
{
    static FILE *(*echt)(const char *, const char *);
    FILE *r;
    if (!echt) echt = (FILE *(*)(const char *, const char *))dlsym(RTLD_NEXT, "fopen64");
    if (!echt) { errno = ENOSYS; return NULL; }
    errno = 0; r = echt(pad, mode);
    { int e = errno; meld_resultaat_mode("fopen64", pad, mode, r ? 1 : -1, e); errno = e; }
    return r;
}

DIR *opendir(const char *pad)
{
    static DIR *(*echt)(const char *);
    DIR *r;
    if (!echt) echt = (DIR *(*)(const char *))dlsym(RTLD_NEXT, "opendir");
    if (!echt) { errno = ENOSYS; return NULL; }
    errno = 0; r = echt(pad);
    { int e = errno; meld_resultaat("opendir", pad, r ? 1 : -1, e); errno = e; }
    return r;
}

/* ---------------------------------------------------------------- de __xstat-familie
   Op i386 gaat `stat(path,buf)` van de gast naar `__xstat(_STAT_VER, path, buf)`. */

int __xstat(int ver, const char *pad, struct stat *st)
{
    static int (*echt)(int, const char *, struct stat *);
    int r;
    if (!echt) echt = (int (*)(int, const char *, struct stat *))dlsym(RTLD_NEXT, "__xstat");
    if (!echt) { errno = ENOSYS; return -1; }
    errno = 0; r = echt(ver, pad, st);
    { int e = errno; meld_resultaat("__xstat", pad, r, e); errno = e; }
    return r;
}

int __lxstat(int ver, const char *pad, struct stat *st)
{
    static int (*echt)(int, const char *, struct stat *);
    int r;
    if (!echt) echt = (int (*)(int, const char *, struct stat *))dlsym(RTLD_NEXT, "__lxstat");
    if (!echt) { errno = ENOSYS; return -1; }
    errno = 0; r = echt(ver, pad, st);
    { int e = errno; meld_resultaat("__lxstat", pad, r, e); errno = e; }
    return r;
}

int __fxstat(int ver, int fd, struct stat *st)
{
    static int (*echt)(int, int, struct stat *);
    int r;
    if (!echt) echt = (int (*)(int, int, struct stat *))dlsym(RTLD_NEXT, "__fxstat");
    if (!echt) { errno = ENOSYS; return -1; }
    errno = 0; r = echt(ver, fd, st);
    if (r < 0) meld("__fxstat    fd=%d -> FOUT errno=%d (%s)", fd, errno, strerror(errno));
    return r;
}

int __xstat64(int ver, const char *pad, void *st)
{
    static int (*echt)(int, const char *, void *);
    int r;
    if (!echt) echt = (int (*)(int, const char *, void *))dlsym(RTLD_NEXT, "__xstat64");
    if (!echt) { errno = ENOSYS; return -1; }
    errno = 0; r = echt(ver, pad, st);
    { int e = errno; meld_resultaat("__xstat64", pad, r, e); errno = e; }
    return r;
}

