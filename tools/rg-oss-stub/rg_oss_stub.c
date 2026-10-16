/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-oss-stub -- een i386 OSS-shim (LD_PRELOAD) voor de Allegro-4-games in de native route.
 *
 * WAAROM (gemeten, docs/research/gogostrike-native-meting.md): GoGoStrike roept in vSystemDeviceInit
 * install_sound(DIGI_AUTODETECT, MIDI_AUTODETECT, argv[0]) aan. Allegro's OSS-driver opent /dev/dsp,
 * /dev/mixer en /dev/sequencer; die bestaan in de sandbox niet (ENOENT), dus install_sound faalt met
 * "Insufficient digital voices available". Het spel springt dan naar zijn vroege uitgang (0x805963f ->
 * jmp 0x8059716) en slaat ALLE verdere apparaatinitialisatie over -- inclusief lpSramDeviceOpen, dat de
 * globale 0x80c2ba8 zet die uwSramCheckFormatCode daarna leest. Gevolg: NULL-deref op 0x08071915.
 *
 * WAT DEZE STUB DOET: hij vangt open/open64/openat/openat64 op /dev/dsp en /dev/mixer af en geeft een
 * echte, bruikbare fd terug (/dev/null, O_RDWR), en beantwoordt de ioctl-verzoeken die deze liballeg
 * werkelijk stuurt (gemeten met objdump op liballeg.so.4.0: SNDCTL_DSP_SPEED/STEREO/SETFMT/SETFRAGMENT/
 * GETFMTS/GETOSPACE en MIXER_WRITE(0xff|4|3)). Er komt GEEN geluid uit: de DSP is stil. Dat is voor de
 * native route voldoende -- het spel moet zijn initialisatie afmaken, het hoeft niet hoorbaar te zijn.
 *
 * /dev/sequencer wordt met opzet NIET nagebootst (alleen gemeld), en dat is een meting, geen luiheid:
 *   - met libaoss (dat /dev/dsp en /dev/mixer afvangt maar /dev/sequencer met rust laat) geeft
 *     install_sound 0 terug: Allegro's OSS-MIDI-driver kan /dev/sequencer niet openen en Allegro valt
 *     terug op zijn eigen digmid-driver (software-MIDI over de digitale stemmen);
 *   - bootst de stub /dev/sequencer WEL na, dan neemt Allegro's OSS-MIDI-driver het over en eindigt
 *     install_sound op -1 met allegro_error = "No support synth type found" (gemeten met gdb op
 *     0x805961c). Het spel gaat dan alsnog naar zijn vroege uitgang en crasht op 0x08071915.
 * Wil iemand de sequencer toch nadoen: RG_OSS_STUB_SEQ=1 zet dat aan (met dezelfde ioctl-antwoorden).
 *
 * WAAROM HEADER-VRIJ: op deze machine ontbreken de 32-bits glibc-ontwikkelheaders (met -m32 klaagt
 * gcc over /usr/include/gnu/stubs-32.h). Daarom alleen <stdarg.h> (van de compiler) en eigen
 * declaraties; de syscalls gaan rechtstreeks via syscall(). Bouwen kan zo met kaal
 * `gcc -m32 -shared -fPIC` -- zie gen.sh.
 *
 * GEBRUIK: in de native route via patches/<Game>/native.json (docs/native-profiel.md): het bestand in "libs"
 *   (verschijnt als /rg/lib/librg-oss-stub.so) en in "preload".
 * Zet RG_OSS_STUB_LOG=1 voor een regel per afgevangen ioctl (max 3 per verzoeknummer) in het logboek.
 */
#include <stdarg.h>

extern long syscall(long, ...);
extern int strcmp(const char *, const char *);
extern char *getenv(const char *);

#define RG_NR_write   4
#define RG_NR_close   6
#define RG_NR_ioctl  54
#define RG_NR_openat 295
#define RG_AT_FDCWD (-100)

#define RG_O_CREAT  0100
#define RG_O_EXCL   0200
#define RG_O_TRUNC  01000
#define RG_O_APPEND 02000

#define RG_MAXFD 1024

/* 0 = niet van ons, 1 = /dev/dsp, 2 = /dev/mixer, 3 = /dev/sequencer */
static unsigned char g_kind[RG_MAXFD];
static unsigned char g_gezien[4][256];
static int g_log = 0;
static int g_seq = 0;
static int g_seq_gemeld = 0;
static int g_klaar = 0;

static void rg_init(void)
{
    if (g_klaar)
        return;
    g_klaar = 1;
    g_log = getenv("RG_OSS_STUB_LOG") != 0;
    g_seq = getenv("RG_OSS_STUB_SEQ") != 0;
}

static const char *rg_naam(int k)
{
    return k == 1 ? "dsp" : k == 2 ? "mixer" : k == 3 ? "seq" : "?";
}

/* --- minimale opmaak, zonder libc-kop --- */
static unsigned int rg_zet(char *b, unsigned int p, const char *s)
{
    while (*s)
        b[p++] = *s++;
    return p;
}

static unsigned int rg_hex(char *b, unsigned int p, unsigned long v)
{
    char t[16];
    int i = 0;
    b[p++] = '0';
    b[p++] = 'x';
    if (!v) {
        b[p++] = '0';
        return p;
    }
    while (v) {
        unsigned long d = v & 0xf;
        t[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v >>= 4;
    }
    while (i)
        b[p++] = t[--i];
    return p;
}

static unsigned int rg_dec(char *b, unsigned int p, unsigned long v)
{
    char t[16];
    int i = 0;
    if (!v) {
        b[p++] = '0';
        return p;
    }
    while (v) {
        t[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i)
        b[p++] = t[--i];
    return p;
}

static void rg_meld(const char *wat, int k, unsigned long req, long ret)
{
    char b[128];
    unsigned int p = 0;
    p = rg_zet(b, p, "rg-oss-stub: ");
    p = rg_zet(b, p, wat);
    if (k) {
        p = rg_zet(b, p, " ");
        p = rg_zet(b, p, rg_naam(k));
    }
    if (req) {
        p = rg_zet(b, p, " req=");
        p = rg_hex(b, p, req);
    }
    p = rg_zet(b, p, " ret=");
    p = rg_dec(b, p, (unsigned long)ret);
    b[p++] = '\n';
    syscall(RG_NR_write, 2, b, (unsigned long)p);
}

static int rg_kind_van(const char *pad)
{
    if (!pad)
        return 0;
    if (!strcmp(pad, "/dev/dsp") || !strcmp(pad, "/dev/dspW") || !strcmp(pad, "/dev/audio"))
        return 1;
    if (!strcmp(pad, "/dev/mixer") || !strcmp(pad, "/dev/mixer1"))
        return 2;
    if (!strcmp(pad, "/dev/sequencer") || !strcmp(pad, "/dev/sequencer2") || !strcmp(pad, "/dev/music"))
        return 3;
    return 0;
}

static int rg_nep_open(const char *pad, int flags, int mode, int dirfd)
{
    int k;
    long fd;
    rg_init();
    k = rg_kind_van(pad);
    if (k == 3 && !g_seq) {
        /* Zie de kop: /dev/sequencer met opzet niet nadoen -- dan valt Allegro terug op digmid. */
        if (!g_seq_gemeld) {
            g_seq_gemeld = 1;
            rg_meld("seq doorgelaten (geen nep)", k, 0, -1);
        }
        return (int)syscall(RG_NR_openat, dirfd, pad, flags, mode);
    }
    if (!k)
        return (int)syscall(RG_NR_openat, dirfd, pad, flags, mode);
    fd = syscall(RG_NR_openat, RG_AT_FDCWD, "/dev/null", 2 /*O_RDWR*/, 0);
    if (fd >= 0 && fd < RG_MAXFD)
        g_kind[(int)fd] = (unsigned char)k;
    rg_meld("nep-open", k, 0, fd);
    return (int)fd;
}

#define RG_IOC_NR(r)   ((unsigned)(r) & 0xffu)
#define RG_IOC_DIR(r)  (((unsigned)(r) >> 30) & 3u)

/* De verzoeken die deze liballeg werkelijk stuurt (gemeten met objdump -d op liballeg.so.4.0). */
static long rg_dsp(unsigned long req, void *arg)
{
    int *ip = (int *)arg;
    switch (RG_IOC_NR(req)) {
    case 2:  /* SNDCTL_DSP_SPEED: de gevraagde snelheid accepteren */
        if (ip && (*ip < 4000 || *ip > 192000))
            *ip = 44100;
        return 0;
    case 3:  /* SNDCTL_DSP_STEREO */
        if (ip)
            *ip = 1;
        return 0;
    case 4:  /* SNDCTL_DSP_GETBLKSIZE */
        if (ip)
            *ip = 4096;
        return 0;
    case 5:  /* SNDCTL_DSP_SETFMT: alles accepteren, anders AFMT_S16_LE */
        if (ip && !*ip)
            *ip = 0x10;
        return 0;
    case 6:  /* SNDCTL_DSP_CHANNELS */
        if (ip)
            *ip = 2;
        return 0;
    case 9:  /* SNDCTL_DSP_SUBDIVIDE */
    case 10: /* SNDCTL_DSP_SETFRAGMENT */
    case 14: /* SNDCTL_DSP_NONBLOCK */
    case 8:  /* SNDCTL_DSP_POST */
        return 0;
    case 11: /* SNDCTL_DSP_GETFMTS: AFMT_U8|AFMT_S16_LE */
        if (ip)
            *ip = 0x18;
        return 0;
    case 12: /* SNDCTL_DSP_GETOSPACE */
    case 13: /* SNDCTL_DSP_GETISPACE */
        if (ip) {
            ip[0] = 16;          /* fragments */
            ip[1] = 16;          /* fragstotal */
            ip[2] = 4096;        /* fragsize */
            ip[3] = 16 * 4096;   /* bytes */
        }
        return 0;
    case 15: /* SNDCTL_DSP_GETCAPS: geen trigger/mmap, Allegro gebruikt de write-route */
        if (ip)
            *ip = 0;
        return 0;
    case 16: /* GETTRIGGER (lezen) / SETTRIGGER (schrijven) */
        if (ip && RG_IOC_DIR(req) == 2u)
            *ip = 1;
        return 0;
    case 17: /* SNDCTL_DSP_GETIPTR */
    case 18: /* SNDCTL_DSP_GETOPTR */
        if (ip) {
            ip[0] = 0;
            ip[1] = 0;
            ip[2] = 0;
        }
        return 0;
    case 23: /* SNDCTL_DSP_GETODELAY */
        if (ip)
            *ip = 0;
        return 0;
    case 0:  /* RESET */
    case 1:  /* SYNC */
        return 0;
    default:
        return 0;
    }
}

static long rg_mixer(unsigned long req, void *arg)
{
    int *ip = (int *)arg;
    if (RG_IOC_DIR(req) == 2u && ip) { /* lees-ioctl */
        unsigned n = RG_IOC_NR(req);
        if (n == 30u || n == 32u)      /* DEVMASK / STEREODEVS */
            *ip = 0x0000ffff;
        else if (n == 31u || n == 33u) /* RECMASK / RECSRC */
            *ip = 0;
        else
            *ip = 0x6464;              /* volumekanaal: 100/100 */
    }
    return 0;                          /* schrijf-ioctls: aannemen */
}

static long rg_seq(unsigned long req, void *arg)
{
    int *ip = (int *)arg;
    switch (RG_IOC_NR(req)) {
    case 6:  /* SNDCTL_SEQ_NRMIDIS */
        if (ip)
            *ip = 1;
        return 0;
    case 7:  /* SNDCTL_SEQ_NRSYNTHS */
        if (ip)
            *ip = 0;
        return 0;
    case 8:  /* SNDCTL_SEQ_NRMIX */
        if (ip)
            *ip = 1;
        return 0;
    case 4:  /* GETOUTCOUNT */
    case 5:  /* GETINCOUNT */
    case 10: /* GETTIME */
        if (ip)
            *ip = 0;
        return 0;
    default:
        if (ip && RG_IOC_DIR(req) == 2u)
            *ip = 0;
        return 0;
    }
}

static long rg_nep_ioctl(int k, unsigned long req, void *arg)
{
    long r;
    unsigned nr = RG_IOC_NR(req);
    if (k == 1)
        r = rg_dsp(req, arg);
    else if (k == 2)
        r = rg_mixer(req, arg);
    else
        r = rg_seq(req, arg);
    if (g_log && g_gezien[k][nr] < 3) {
        g_gezien[k][nr]++;
        rg_meld("ioctl", k, req, r);
    }
    return r;
}

/* --- de afgevangen functies --- */
int open(const char *pad, int flags, ...)
{
    int mode = 0;
    if (flags & RG_O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return rg_nep_open(pad, flags, mode, RG_AT_FDCWD);
}

int open64(const char *pad, int flags, ...)
{
    int mode = 0;
    if (flags & RG_O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return rg_nep_open(pad, flags, mode, RG_AT_FDCWD);
}

int openat(int dirfd, const char *pad, int flags, ...)
{
    int mode = 0;
    if (flags & RG_O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return rg_nep_open(pad, flags, mode, dirfd);
}

int openat64(int dirfd, const char *pad, int flags, ...)
{
    int mode = 0;
    if (flags & RG_O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return rg_nep_open(pad, flags, mode, dirfd);
}

int ioctl(int fd, unsigned long req, ...)
{
    void *arg = 0;
    va_list ap;
    rg_init();
    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (fd < 0 || fd >= RG_MAXFD || !g_kind[fd])
        return (int)syscall(RG_NR_ioctl, fd, req, arg);
    return (int)rg_nep_ioctl(g_kind[fd], req, arg);
}

int close(int fd)
{
    if (fd >= 0 && fd < RG_MAXFD)
        g_kind[fd] = 0;
    return (int)syscall(RG_NR_close, fd);
}
