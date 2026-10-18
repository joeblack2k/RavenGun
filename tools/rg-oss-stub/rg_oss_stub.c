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
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern int pthread_create(unsigned long *, const void *, void *(*)(void *), void *);
extern int pthread_detach(unsigned long);
extern int usleep(unsigned int);
extern int atoi(const char *);

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
static int g_maxfrag_env = 0;
static int g_play = 0; /* RG_OSS_PLAY=1: de DSP speelt echt af via PulseAudio (zie "Afspelen") */

static void rg_init(void)
{
    if (g_klaar)
        return;
    g_klaar = 1;
    g_log = getenv("RG_OSS_STUB_LOG") != 0;
    g_seq = getenv("RG_OSS_STUB_SEQ") != 0;
    {
        const char *pl = getenv("RG_OSS_PLAY"), *fr = getenv("RG_OSS_FRAGS");
        g_play = pl && *pl && *pl != '0';
        g_maxfrag_env = fr && *fr ? atoi(fr) : 0;
    }
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


/* ===================== Afspelen (RG_OSS_PLAY=1) =====================
 * Zonder dit is de DSP /dev/null: het spel hoort niets. Met RG_OSS_PLAY=1 houdt de shim de OSS-toestand bij
 * (SPEED/SETFMT/CHANNELS/SETFRAGMENT), neemt write() op de DSP-fd over in een ringbuffer en speelt die met een eigen
 * draad af via libpulse-simple (dlopen, 32-bit; PULSE_SERVER wijst in de sandbox naar de pulse-socket van de host).
 * GETOSPACE meldt de werkelijke vrije ruimte van de ring, dus de pacing van het spel (Target: Terror Gold schrijft
 * een fragment zodra GETOSPACE er een vrij meldt) loopt op het afspeeltempo. Is PulseAudio er niet, dan loopt de
 * draad op een virtuele klok: de pacing blijft kloppen, er komt alleen geen geluid. Een stroom (de laatst geopende DSP).
 */
#define RG_RING 131072u            /* macht van twee; groter dan elk gevraagd fragmentenpakket */
#define RG_PA_SAMPLE_U8     0
#define RG_PA_SAMPLE_S16LE  3

struct rg_pa_spec { int format; unsigned int rate; unsigned char channels; };
struct rg_pa_attr { unsigned int maxlength, tlength, prebuf, minreq, fragsize; };

static unsigned char g_ring[RG_RING];
static unsigned int g_head = 0, g_tail = 0;   /* oplopende tellers (producent: write; consument: afspeeldraad) */
static int g_rate = 44100, g_chan = 2, g_fmt = 0x10;
static unsigned int g_fragsize = 4096, g_cap = 2 * 4096;  /* g_cap: gemelde ringgrootte in bytes (frag-veelvoud) */
static int g_gestart = 0;
static int g_conf_gen = 0;                   /* ophogen bij elke wijziging van rate/chan/fmt */

static unsigned int rg_frame(void) { return (unsigned)(g_chan > 0 ? g_chan : 1) * (g_fmt == 0x08 ? 1u : 2u); }
static unsigned int rg_vrij(void)
{
    unsigned int used = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE) - __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE);
    return used >= g_cap ? 0 : g_cap - used;
}

static void rg_set_cap(unsigned int fragsize, int nfrag)
{
    if (fragsize < 256) fragsize = 256;
    if (fragsize > 32768) fragsize = 32768;
    if (nfrag < 2) nfrag = 2;
    if (nfrag > (g_maxfrag_env >= 2 ? g_maxfrag_env : 2)) /* RG_OSS_FRAGS: bovengrens op het aantal fragmenten (latentie) */
        nfrag = g_maxfrag_env >= 2 ? g_maxfrag_env : 2;
    g_fragsize = fragsize;
    g_cap = fragsize * (unsigned)nfrag;
}

static void rg_nu(unsigned long long *ns)
{
    struct { long s; long n; } t;
    syscall(265 /* clock_gettime */, 1 /* CLOCK_MONOTONIC */, &t);
    *ns = (unsigned long long)t.s * 1000000000ull + (unsigned long long)t.n;
}

static void *rg_speel_draad(void *arg)
{
    /* RTLD_DEEPBIND: libpulse bindt eerst aan zijn eigen libc, niet aan symbolen van de game-exe. De exe van Target: Terror Gold
     * exporteert een eigen gettimeofday (via ftime, geeft geen 0 terug) en libpulse (pa_gettimeofday) stopte erop met een assert. */
    void *lib = dlopen("libpulse-simple.so.0", 2 /* RTLD_NOW */ | 8 /* RTLD_DEEPBIND */);
    void *(*p_new)(const char *, const char *, int, const char *, const char *, const struct rg_pa_spec *, const void *,
                   const struct rg_pa_attr *, int *) = 0;
    int (*p_write)(void *, const void *, unsigned long, int *) = 0;
    void (*p_free)(void *) = 0;
    void *pa = 0;
    int gen = -1, pa_geprobeerd = 0;
    static unsigned char tmp[RG_RING];
    unsigned long long vklok = 0;
    (void)arg;
    if (lib) {
        p_new = (void *(*)(const char *, const char *, int, const char *, const char *, const struct rg_pa_spec *,
                           const void *, const struct rg_pa_attr *, int *))dlsym(lib, "pa_simple_new");
        p_write = (int (*)(void *, const void *, unsigned long, int *))dlsym(lib, "pa_simple_write");
        p_free = (void (*)(void *))dlsym(lib, "pa_simple_free");
    }
    if (g_log && !(p_new && p_write))
        rg_meld("libpulse-simple.so.0 niet te laden: virtuele klok, geen geluid", 0, 0, -1);
    for (;;) {
        unsigned int avail, n, frame, chunk;
        if (gen != __atomic_load_n(&g_conf_gen, __ATOMIC_ACQUIRE)) {
            gen = __atomic_load_n(&g_conf_gen, __ATOMIC_ACQUIRE);
            if (pa && p_free) { p_free(pa); pa = 0; }
            pa_geprobeerd = 0;
        }
        if (!pa && !pa_geprobeerd && p_new && p_write) {
            struct rg_pa_spec ss;
            struct rg_pa_attr at;
            int err = 0;
            const char *naam = getenv("RG_OSS_NAME");
            ss.format = g_fmt == 0x08 ? RG_PA_SAMPLE_U8 : RG_PA_SAMPLE_S16LE;
            ss.rate = (unsigned)g_rate;
            ss.channels = (unsigned char)(g_chan < 1 ? 1 : g_chan > 2 ? 2 : g_chan);
            at.maxlength = (unsigned)-1;
            at.tlength = ((unsigned)g_rate / 12u) * rg_frame();   /* ~80 ms server-side buffer */
            at.prebuf = 0;                 /* meteen afspelen; onderloop = korte stilte, geen wachten op volle buffer */
            at.minreq = (unsigned)-1;
            at.fragsize = (unsigned)-1;
            pa_geprobeerd = 1;
            pa = p_new(0, naam && *naam ? naam : "OSS /dev/dsp", 1 /* PA_STREAM_PLAYBACK */, 0, "OSS /dev/dsp", &ss, 0,
                       &at, &err);
            if (g_log) rg_meld(pa ? "pulse-stroom open" : "pulse-stroom FAALT (virtuele klok)", 0, (unsigned)g_rate, pa ? 0 : (long)err);
        }
        frame = rg_frame();
        avail = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE) - __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE);
        if (avail == 0) { usleep(2000); vklok = 0; continue; }
        chunk = ((unsigned)g_rate / 100u) * frame;   /* 10 ms */
        if (chunk < frame) chunk = frame;
        n = avail < chunk ? avail : chunk;
        if (n > frame) n -= n % frame;
        {
            unsigned int t = __atomic_load_n(&g_tail, __ATOMIC_RELAXED), i;
            for (i = 0; i < n; i++) tmp[i] = g_ring[(t + i) & (RG_RING - 1)];
        }
        if (pa) {
            int err = 0;
            if (p_write(pa, tmp, n, &err) < 0) {
                if (g_log) rg_meld("pulse_write faalt: virtuele klok", 0, 0, (long)err);
                if (p_free) p_free(pa);
                pa = 0; pa_geprobeerd = 1;
            }
        } else {
            /* virtuele klok: n bytes duren n/(rate*frame) seconde */
            unsigned long long nu, dur = (unsigned long long)n * 1000000000ull / ((unsigned long long)g_rate * frame);
            rg_nu(&nu);
            if (!vklok || vklok + 200000000ull < nu) vklok = nu;
            vklok += dur;
            if (vklok > nu) usleep((unsigned)((vklok - nu) / 1000ull));
        }
        __atomic_store_n(&g_tail, __atomic_load_n(&g_tail, __ATOMIC_RELAXED) + n, __ATOMIC_RELEASE);
    }
    return 0;
}

static void rg_start_draad(void)
{
    unsigned long th;
    if (__atomic_exchange_n(&g_gestart, 1, __ATOMIC_ACQ_REL))
        return;
    if (pthread_create(&th, 0, rg_speel_draad, 0) == 0)
        pthread_detach(th);
    else
        g_gestart = 0;
}

static long rg_play_write(const void *buf, unsigned long n)
{
    const unsigned char *b = (const unsigned char *)buf;
    unsigned long klaar = 0;
    int stil = 0;
    rg_start_draad();
    while (klaar < n) {
        unsigned int vrij = rg_vrij(), k, h, i;
        if (!vrij) {
            if (++stil > 3000) break;   /* 3 s zonder voortgang: weggooien i.p.v. het spel te laten vastlopen */
            usleep(1000);
            continue;
        }
        stil = 0;
        k = n - klaar < vrij ? (unsigned)(n - klaar) : vrij;
        h = __atomic_load_n(&g_head, __ATOMIC_RELAXED);
        for (i = 0; i < k; i++) g_ring[(h + i) & (RG_RING - 1)] = b[klaar + i];
        __atomic_store_n(&g_head, h + k, __ATOMIC_RELEASE);
        klaar += k;
    }
    return (long)n;
}

/* De DSP-ioctls in afspeelmodus. Geeft -2 terug voor "niet van ons" (dan volgt de stille afhandeling). */
static long rg_dsp_play(unsigned long req, void *arg)
{
    int *ip = (int *)arg;
    if (((unsigned)(req >> 8) & 0xffu) != 'P')
        return 0;    /* mixer-ioctls ('M') op de DSP-fd (het spel doet SetMixer daarop): aannemen, niets aanpassen */
    switch (RG_IOC_NR(req)) {
    case 2:  /* SPEED */
        if (ip) {
            if (*ip < 4000 || *ip > 192000) *ip = 44100;
            if (g_rate != *ip) { g_rate = *ip; __atomic_add_fetch(&g_conf_gen, 1, __ATOMIC_ACQ_REL); }
        }
        return 0;
    case 3:  /* STEREO */
        if (ip) {
            int c = *ip ? 2 : 1;
            if (g_chan != c) { g_chan = c; __atomic_add_fetch(&g_conf_gen, 1, __ATOMIC_ACQ_REL); }
            *ip = c - 1;
        }
        return 0;
    case 4:  /* GETBLKSIZE */
        if (ip) *ip = (int)g_fragsize;
        return 0;
    case 5:  /* SETFMT: U8 en S16LE; de rest wordt S16LE */
        if (ip) {
            int f = (*ip == 0x08) ? 0x08 : 0x10;
            if (g_fmt != f) { g_fmt = f; __atomic_add_fetch(&g_conf_gen, 1, __ATOMIC_ACQ_REL); }
            *ip = f;
        }
        return 0;
    case 6:  /* CHANNELS: 1 of 2 */
        if (ip) {
            int c = *ip == 1 ? 1 : 2;
            if (g_chan != c) { g_chan = c; __atomic_add_fetch(&g_conf_gen, 1, __ATOMIC_ACQ_REL); }
            *ip = c;
        }
        return 0;
    case 10: /* SETFRAGMENT: hoge 16 bits = max aantal fragmenten, lage 16 = log2(grootte) */
        if (ip) {
            unsigned v = (unsigned)*ip, sh = v & 0xffffu;
            rg_set_cap(sh >= 8 && sh <= 15 ? 1u << sh : 4096u, (int)(v >> 16));
        }
        return 0;
    case 12: /* GETOSPACE */
        if (ip) {
            unsigned int vrij = rg_vrij();
            ip[0] = (int)(vrij / g_fragsize);
            ip[1] = (int)(g_cap / g_fragsize);
            ip[2] = (int)g_fragsize;
            ip[3] = (int)vrij;
        }
        return 0;
    case 23: /* GETODELAY: bytes in de ring */
        if (ip) *ip = (int)(g_cap - rg_vrij());
        return 0;
    case 0:  /* RESET: ring leegmaken */
        __atomic_store_n(&g_tail, __atomic_load_n(&g_head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
        return 0;
    default:
        return -2;
    }
}

/* De verzoeken die deze liballeg werkelijk stuurt (gemeten met objdump -d op liballeg.so.4.0). */
static long rg_dsp(unsigned long req, void *arg)
{
    int *ip = (int *)arg;
    if (g_play) {
        long r = rg_dsp_play(req, arg);
        if (r != -2)
            return r;
    }
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

long write(int fd, const void *buf, unsigned long n)
{
    if (g_play && fd >= 0 && fd < RG_MAXFD && g_kind[fd] == 1)
        return rg_play_write(buf, n);
    return syscall(RG_NR_write, fd, buf, n);
}

int close(int fd)
{
    if (fd >= 0 && fd < RG_MAXFD)
        g_kind[fd] = 0;
    return (int)syscall(RG_NR_close, fd);
}
