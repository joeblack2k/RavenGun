/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-cf-io -- OPSTARTSKELET (niet gespeeld, niet op de bench getest) voor Crossfire: Maximum Paintball op de native route.
 * i386 LD_PRELOAD-shim, header-vrij (zoals tools/rg-oss-stub), bouwen met kaal `gcc -m32 -shared -fPIC`.
 *
 * WAAROM (alles statisch gemeten aan med_pball en mjg.o in de spelmap, zie docs/research/budgie/README.md):
 *   - de game (1996-1998 PC-motor, 161 KB, ET_EXEC) praat met het MegaJamma-kastbord via twee apparaten:
 *       /dev/p37c  een byte per read()/write(): poort 0x37c. Is euid 0, dan doet de game zelf iopl(3) + in/out
 *                  (inb op 0x805e940, outb op 0x805e95f); anders opent hij /dev/p37c O_RDWR (0x805e824) en doet
 *                  write(fd,&byte,1) / read(fd,&byte,1). In de sandbox is euid niet 0, dus de apparaatroute geldt.
 *       /dev/mjg   de kernelmodule mjg.o (kernel 2.4.22): read() geeft een record van 0xb8 bytes of 0 (niets nieuws),
 *                  ioctl 0x40044d02..0x40044d09 (_IOW('M',2..9,int)) zet registers; open mislukt = bail (exit).
 *   - /dev/hda: HDIO_GET_IDENTITY (ioctl 0x30d) levert het serienummer (offset 0x14, 20 bytes) dat de game met de eerste
 *     0x15 bytes van adjust.dat vergelijkt (streepjes tellen niet mee; 0x8056ec7). Open mislukt = melding "open failed".
 *   - /proc/version moet met "Linux version 2.4.22 (root@midas) (gcc version 3.2.3) #6 Tue Sep 2 17:43:01 PDT 2003"
 *     beginnen (0x8056f51). fopen() is niet te onderscheppen: dat gaat met een mount in native.json (files), niet hier.
 *
 * WAT DEZE SHIM DOET: open/open64/openat/openat64 op die drie paden geven een echte fd (/dev/null); read/write/ioctl/close op
 * die fd's worden hier beantwoord; iopl() slaagt. Alles anders gaat ongewijzigd naar de kernel (syscall).
 *
 * WAT NIET BEWEZEN IS (eerlijk):
 *   - de polariteit en de bits van de schakelaars (record +0x04, 4 bytes uit adres 0x00 van het bord) en de codering van de
 *     geweergegevens (record +0x00 en +0x10..0x2f) zijn NIET ontcijferd; dit skelet geeft een stil bord (alles 0, of
 *     RG_CF_SWITCHES=<hex> voor +0x04). De munt-tellers rekenen op stijgende flanken van bit 0x200000 en 0x100000.
 *   - het registerbeeld van /dev/p37c is een model (adresnibbles 0x2n/0x3n, datanibbles 0x0n/0x1n, zoals mj_set_addr en
 *     mj_write_reg_now in mjg.o); of de game er meer van verwacht is niet gemeten.
 *   - geen enkele regel hiervan is ooit in het spelproces gedraaid.
 *
 * Omgeving: RG_CF_LOG=1 (een regel per afgevangen aanroep, hoogstens 3 per soort), RG_CF_SWITCHES=<hex>, RG_CF_HDSERIAL=<tekst>
 * (anders de eerste 20 bytes van adjust.dat in de werkmap).
 */
#include <stdarg.h>

extern long syscall(long, ...);
extern char *getenv(const char *);
extern int strcmp(const char *, const char *);

#define NR_read 3
#define NR_write 4
#define NR_open 5
#define NR_close 6
#define NR_openat 295
#define NR_ioctl 54
#define NR_clock_gettime 265
#define AT_FDCWD_ (-100)
#define MAXFD 1024

enum { K_NONE = 0, K_P37C = 1, K_MJG = 2, K_HDA = 3 };
static unsigned char g_kind[MAXFD];
static unsigned char g_reg[256];   /* registerbeeld van het bord (model) */
static unsigned int g_addr;        /* huidig adres: lage nibble 0x2n, hoge nibble 0x3n */
static unsigned long long g_laatste_ms;
static unsigned int g_teller;
static unsigned int g_schakelaars;
static int g_log, g_klaar;
static int g_gemeld[8];
static char g_serial[21];

static void melding(const char *wat, int soort)
{
    if (!g_log || soort < 0 || soort >= 8 || g_gemeld[soort]++ >= 3)
        return;
    char b[96];
    unsigned int p = 0;
    const char *v = "rg-cf-io: ";
    while (*v) b[p++] = *v++;
    while (*wat && p < 90) b[p++] = *wat++;
    b[p++] = '\n';
    syscall(NR_write, 2, b, (unsigned long)p);
}

static unsigned long long nu_ms(void)
{
    struct { long s; long ns; } t;
    syscall(NR_clock_gettime, 1 /* CLOCK_MONOTONIC */, &t);
    return (unsigned long long)t.s * 1000ull + (unsigned long long)t.ns / 1000000ull;
}

static unsigned int hexwaarde(const char *s)
{
    unsigned int v = 0;
    for (; *s; ++s) {
        unsigned int d = (*s >= '0' && *s <= '9') ? (unsigned)(*s - '0') : (*s >= 'a' && *s <= 'f') ? (unsigned)(*s - 'a' + 10) : (*s >= 'A' && *s <= 'F') ? (unsigned)(*s - 'A' + 10) : 16u;
        if (d > 15) break;
        v = v * 16 + d;
    }
    return v;
}

static void init(void)
{
    if (g_klaar) return;
    g_klaar = 1;
    g_log = getenv("RG_CF_LOG") != 0;
    const char *sw = getenv("RG_CF_SWITCHES");
    g_schakelaars = sw ? hexwaarde(sw) : 0;
    const char *hs = getenv("RG_CF_HDSERIAL");
    unsigned int n = 0;
    if (hs) {
        while (hs[n] && n < 20) { g_serial[n] = hs[n]; ++n; }
    } else {
        long fd = syscall(NR_open, "adjust.dat", 0, 0);
        if (fd >= 0) {
            long r = syscall(NR_read, fd, g_serial, 20);
            n = r > 0 ? (unsigned)r : 0;
            syscall(NR_close, fd);
        }
    }
    for (; n < 20; ++n) g_serial[n] = 0;
    g_serial[20] = 0;
}

static int soort_van(const char *pad)
{
    if (!pad) return K_NONE;
    if (!strcmp(pad, "/dev/p37c")) return K_P37C;
    if (!strcmp(pad, "/dev/mjg")) return K_MJG;
    if (!strcmp(pad, "/dev/hda")) return K_HDA;
    return K_NONE;
}

/* Geeft een fd voor een nagebootst apparaat (of -2 als het pad niet van ons is). */
static long nep_open(const char *pad)
{
    init();
    int k = soort_van(pad);
    if (!k) return -2;
    long fd = syscall(NR_open, "/dev/null", 2 /* O_RDWR */, 0);
    if (fd >= 0 && fd < MAXFD) g_kind[fd] = (unsigned char)k;
    melding(k == K_P37C ? "open /dev/p37c" : k == K_MJG ? "open /dev/mjg" : "open /dev/hda", k);
    return fd;
}

int open(const char *pad, int vlaggen, ...)
{
    long fd = nep_open(pad);
    if (fd != -2) return (int)fd;
    unsigned int mode = 0;
    if (vlaggen & 0100) { va_list ap; va_start(ap, vlaggen); mode = va_arg(ap, unsigned int); va_end(ap); }
    return (int)syscall(NR_open, pad, vlaggen, mode);
}
int open64(const char *pad, int vlaggen, ...)
{
    long fd = nep_open(pad);
    if (fd != -2) return (int)fd;
    unsigned int mode = 0;
    if (vlaggen & 0100) { va_list ap; va_start(ap, vlaggen); mode = va_arg(ap, unsigned int); va_end(ap); }
    return (int)syscall(NR_open, pad, vlaggen, mode);
}
int openat(int dirfd, const char *pad, int vlaggen, ...)
{
    long fd = nep_open(pad);
    if (fd != -2) return (int)fd;
    unsigned int mode = 0;
    if (vlaggen & 0100) { va_list ap; va_start(ap, vlaggen); mode = va_arg(ap, unsigned int); va_end(ap); }
    return (int)syscall(NR_openat, dirfd, pad, vlaggen, mode);
}
int openat64(int dirfd, const char *pad, int vlaggen, ...)
{
    long fd = nep_open(pad);
    if (fd != -2) return (int)fd;
    unsigned int mode = 0;
    if (vlaggen & 0100) { va_list ap; va_start(ap, vlaggen); mode = va_arg(ap, unsigned int); va_end(ap); }
    return (int)syscall(NR_openat, dirfd, pad, vlaggen, mode);
}

int iopl(int niveau)
{
    (void)niveau;
    return 0;
}

int close(int fd)
{
    if (fd >= 0 && fd < MAXFD) g_kind[fd] = K_NONE;
    return (int)syscall(NR_close, fd);
}

/* /dev/p37c: een byte schrijven = een poortschrijfactie (mj_set_addr / mj_write_reg_now), een byte lezen = in(0x37c). */
static void poort_uit(unsigned char b)
{
    switch (b >> 4) {
    case 2: g_addr = (g_addr & 0xf0) | (b & 0x0f); break;
    case 3: g_addr = (g_addr & 0x0f) | ((unsigned)(b & 0x0f) << 4); break;
    case 0: g_reg[g_addr & 0xff] = (unsigned char)((g_reg[g_addr & 0xff] & 0xf0) | (b & 0x0f)); break;
    case 1: g_reg[g_addr & 0xff] = (unsigned char)((g_reg[g_addr & 0xff] & 0x0f) | ((b & 0x0f) << 4)); break;
    default: break;
    }
}

long read(int fd, void *buf, unsigned long n)
{
    if (fd < 0 || fd >= MAXFD || !g_kind[fd]) return syscall(NR_read, fd, buf, n);
    unsigned char *p = (unsigned char *)buf;
    if (g_kind[fd] == K_P37C) {
        if (n < 1) return 0;
        p[0] = g_reg[g_addr & 0xff];
        return 1;
    }
    if (g_kind[fd] == K_MJG) {
        /* Een record van 0xb8 bytes per 10 ms, anders 0 ("niets nieuws", zoals mj_read in mjg.o). */
        if (n < 0xb8) return 0;
        unsigned long long t = nu_ms();
        if (t < g_laatste_ms + 10) return 0;
        g_laatste_ms = t;
        for (unsigned int i = 0; i < 0xb8; ++i) p[i] = 0;
        *(unsigned int *)(p + 0x04) = g_schakelaars;   /* schakelaars: polariteit niet ontcijferd */
        *(unsigned int *)(p + 0x38) = 0x7d;
        *(unsigned int *)(p + 0x3c) = g_teller++;
        melding("read /dev/mjg (record)", 5);
        return 0xb8;
    }
    return 0;
}

long write(int fd, const void *buf, unsigned long n)
{
    if (fd < 0 || fd >= MAXFD || !g_kind[fd]) return syscall(NR_write, fd, buf, n);
    if (g_kind[fd] == K_P37C && n >= 1) poort_uit(*(const unsigned char *)buf);
    return (long)n;
}

int ioctl(int fd, unsigned long verzoek, ...)
{
    va_list ap;
    va_start(ap, verzoek);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    if (fd < 0 || fd >= MAXFD || !g_kind[fd]) return (int)syscall(NR_ioctl, fd, verzoek, arg);
    if (g_kind[fd] == K_HDA && verzoek == 0x30d /* HDIO_GET_IDENTITY */) {
        unsigned char *p = (unsigned char *)arg;
        for (unsigned int i = 0; i < 512; ++i) p[i] = 0;
        for (unsigned int i = 0; i < 20; ++i) p[0x14 + i] = (unsigned char)g_serial[i];
        melding("ioctl HDIO_GET_IDENTITY", 6);
        return 0;
    }
    if (g_kind[fd] == K_MJG) {
        melding("ioctl /dev/mjg", 7);
        return 0;
    }
    return 0;
}
