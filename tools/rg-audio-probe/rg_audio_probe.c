/* RavenGun: geluidsmeter voor 32-bits spellen met een ingebouwde SDL 1.2 (Ghostbusters, Walking Dead).
 *
 * Probleem: SDL 1.2 kiest zijn geluidsdriver zelf (pulse, alsa, oss, arts, esd) en laadt die met dlopen/dlsym; mislukt de
 * eerste, dan valt hij stil terug op de volgende en uiteindelijk op "geen geluid", zonder melding. Van buiten (pactl) is dan
 * alleen te zien DAT er geen stroom is, niet WAAROM. Deze bibliotheek is alleen een meter: zij verandert niets aan het geluid.
 *
 * Zij onderschept dlopen en dlsym en meldt (stderr, met RG_AUDIO_LOG=1):
 *   - welke geluidsbibliotheken geladen worden en of dat lukt (dlerror erbij);
 *   - pa_simple_new (richting, formaat, samplefrequentie, kanalen, resultaat, foutcode) -> de Pulse-driver;
 *   - pa_simple_write: per 2 s het aantal bytes en de PIEK van de samples (S16): piek 0 = het spel levert stilte
 *     (volume 0 of niets gemengd), piek > 0 en toch niets te horen = het probleem zit na het spel (sink, kanalen);
 *   - snd_pcm_open (naam, resultaat) en snd_pcm_writei (frames) -> de ALSA-driver.
 * Laden: LD_PRELOAD (32-bits). Bouwen: gcc -m32 -shared -fPIC -O2 -o librg_audio_probe.so rg_audio_probe.c -ldl
 * (met -I naar een map met een lege gnu/stubs-32.h als de 32-bits glibc-headers ontbreken). Eigen code.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int log_on = -1;
static void *(*echte_dlsym)(void *, const char *);
static void *(*echte_dlopen)(const char *, int);

static void meld(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void meld(const char *fmt, ...) {
    if (log_on < 0) { const char *l = getenv("RG_AUDIO_LOG"); log_on = l && *l == '1'; }
    if (!log_on) return;
    char b[400]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    fprintf(stderr, "[rg_audio_probe] %s\n", b); fflush(stderr);
}
static void *zoek(const char *naam) {          /* de echte dlsym/dlopen, zonder onszelf te raken */
    void *p = dlvsym(RTLD_NEXT, naam, "GLIBC_2.34");
    if (!p) p = dlvsym(RTLD_NEXT, naam, "GLIBC_2.1");
    if (!p) p = dlvsym(RTLD_NEXT, naam, "GLIBC_2.0");
    return p;
}
static uint64_t nu_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

typedef struct { int format; unsigned rate; unsigned char channels; } ss_t;
typedef void *(*pa_new_fn)(const char *, const char *, int, const char *, const char *, const ss_t *, const void *, const void *, int *);
typedef int (*pa_write_fn)(void *, const void *, size_t, int *);
typedef int (*pcm_open_fn)(void **, const char *, int, int);
typedef long (*pcm_writei_fn)(void *, const void *, unsigned long);
static pa_new_fn r_pa_new;
static pa_write_fn r_pa_write;
static pcm_open_fn r_pcm_open;
static pcm_writei_fn r_pcm_writei;
static int pa_fmt;

/* De mainloop-API van pulse (pa_stream_*): SDL 1.2.15 gebruikt pa_simple_new alleen voor de "Test stream" in Audio_Available() (U8,
   11025 Hz, mono) en speelt zelf via pa_stream_new/connect_playback/write. Gemeten 2026-10-18 op Ghostbusters: pa_simple_new
   gelogd maar nooit pa_simple_write, terwijl er wel een stroom "Simple DirectMedia Layer" bestond. */
typedef void *(*st_new_fn)(void *, const char *, const ss_t *, const void *);
typedef int (*st_conn_fn)(void *, const char *, const void *, int, const void *, void *);
typedef int (*st_write_fn)(void *, const void *, size_t, void *, int64_t, int);
typedef size_t (*st_wsize_fn)(void *);
static st_new_fn r_st_new; static st_conn_fn r_st_conn; static st_write_fn r_st_write; static st_wsize_fn r_st_wsize;
static int st_fmt;
static void *w_st_new(void *c, const char *nm, const ss_t *ss, const void *map) {
    void *r = r_st_new(c, nm, ss, map);
    if (ss) st_fmt = ss->format;
    meld("pa_stream_new(\"%s\") formaat=%d rate=%u kanalen=%u -> %s", nm ? nm : "?", ss ? ss->format : -1, ss ? ss->rate : 0,
         ss ? ss->channels : 0, r ? "GELUKT" : "MISLUKT");
    return r;
}
static int w_st_conn(void *s, const char *dev, const void *at, int fl, const void *vol, void *sync) {
    int r = r_st_conn(s, dev, at, fl, vol, sync);
    meld("pa_stream_connect_playback(vlaggen 0x%x) -> %d", fl, r);
    return r;
}
static size_t w_st_wsize(void *s) {
    static uint64_t t0, n, min = (uint64_t)-1;
    size_t r = r_st_wsize(s);
    n++; if (r < min) min = r;
    uint64_t t = nu_ms();
    if (!t0) t0 = t;
    if (t - t0 >= 2000) { meld("pa_stream_writable_size: %llu aanroepen in 2 s, kleinste %llu", (unsigned long long)n, (unsigned long long)min); t0 = t; n = 0; min = (uint64_t)-1; }
    return r;
}
static int w_st_write(void *s, const void *data, size_t n, void *cb, int64_t off, int seek) {
    static uint64_t t0, bytes, piek, aantal;
    if (st_fmt == 3 /* S16LE */) {
        const int16_t *p = data; size_t k = n / 2;
        for (size_t i = 0; i < k; i++) { uint64_t v = p[i] < 0 ? (uint64_t)-p[i] : (uint64_t)p[i]; if (v > piek) piek = v; }
    } else if (st_fmt == 0 /* U8 */) {
        const uint8_t *p = data;
        for (size_t i = 0; i < n; i++) { uint64_t v = p[i] > 128 ? p[i] - 128u : 128u - p[i]; if (v > piek) piek = v; }
    }
    bytes += n; aantal++;
    uint64_t t = nu_ms();
    if (!t0) t0 = t;
    if (t - t0 >= 2000) {
        meld("pa_stream_write: %llu aanroepen, %llu bytes in 2 s, piek %llu (S16: max 32767, U8: max 128)", (unsigned long long)aantal,
             (unsigned long long)bytes, (unsigned long long)piek);
        t0 = t; bytes = piek = aantal = 0;
    }
    return r_st_write(s, data, n, cb, off, seek);
}

static void *w_pa_new(const char *srv, const char *nm, int dir, const char *dev, const char *sn, const ss_t *ss, const void *map, const void *at, int *err) {
    int e = 0;
    void *r = r_pa_new(srv, nm, dir, dev, sn, ss, map, at, err ? err : &e);
    if (ss) pa_fmt = ss->format;
    meld("pa_simple_new dir=%d formaat=%d rate=%u kanalen=%u -> %s (fout %d)", dir, ss ? ss->format : -1, ss ? ss->rate : 0,
         ss ? ss->channels : 0, r ? "GELUKT" : "MISLUKT", err ? *err : e);
    return r;
}
static int w_pa_write(void *s, const void *data, size_t n, int *err) {
    static uint64_t t0, bytes, piek, aantal;
    if (pa_fmt == 3 /* PA_SAMPLE_S16LE */) {
        const int16_t *p = data; size_t k = n / 2; int pk = 0;
        for (size_t i = 0; i < k; i++) { int v = p[i] < 0 ? -p[i] : p[i]; if (v > pk) pk = v; }
        if ((uint64_t)pk > piek) piek = (uint64_t)pk;
    }
    bytes += n; aantal++;
    uint64_t t = nu_ms();
    if (!t0) t0 = t;
    if (t - t0 >= 2000) {
        meld("pa_simple_write: %llu aanroepen, %llu bytes in 2 s, piek %llu (S16: max 32767)", (unsigned long long)aantal,
             (unsigned long long)bytes, (unsigned long long)piek);
        t0 = t; bytes = piek = aantal = 0;
    }
    return r_pa_write(s, data, n, err);
}
static int w_pcm_open(void **h, const char *nm, int st, int mode) {
    int r = r_pcm_open(h, nm, st, mode);
    meld("snd_pcm_open(\"%s\", stream %d) -> %d", nm ? nm : "(null)", st, r);
    return r;
}
static long w_pcm_writei(void *h, const void *b, unsigned long fr) {
    static uint64_t t0, frames, piek;
    frames += fr;
    { /* aanname S16 (SDL 1.2 vraagt dat gewoonlijk): de eerste 4096 bytes van elke schrijfactie */
        const int16_t *p = b; unsigned long k = fr * 4 > 4096 ? 2048 : fr * 2;
        for (unsigned long i = 0; i < k; i++) { uint64_t v = p[i] < 0 ? (uint64_t)-p[i] : (uint64_t)p[i]; if (v > piek) piek = v; }
    }
    uint64_t t = nu_ms();
    if (!t0) t0 = t;
    if (t - t0 >= 2000) { meld("snd_pcm_writei: %llu frames in 2 s, piek %llu (aanname S16)", (unsigned long long)frames, (unsigned long long)piek); t0 = t; frames = piek = 0; }
    return r_pcm_writei(h, b, fr);
}

__attribute__((visibility("default"))) void *dlopen(const char *f, int m) {
    if (!echte_dlopen) { void *p = zoek("dlopen"); memcpy(&echte_dlopen, &p, sizeof p); }
    void *h = echte_dlopen(f, m);
    if (f && (strstr(f, "pulse") || strstr(f, "asound") || strstr(f, "esd") || strstr(f, "arts") || strstr(f, "SDL") || strstr(f, "oss"))) {
        if (h) meld("dlopen(\"%s\") -> ok", f);
        else { const char *e = dlerror(); meld("dlopen(\"%s\") -> MISLUKT: %s", f, e ? e : "?"); }
    }
    return h;
}
__attribute__((visibility("default"))) void *dlsym(void *h, const char *n) {
    if (!echte_dlsym) { void *p = zoek("dlsym"); memcpy(&echte_dlsym, &p, sizeof p); }
    void *p = echte_dlsym(h, n);
    if (p && n) {
        if (!strcmp(n, "pa_simple_new")) { memcpy(&r_pa_new, &p, sizeof p); pa_new_fn w = w_pa_new; memcpy(&p, &w, sizeof p); }
        else if (!strcmp(n, "pa_simple_write")) { memcpy(&r_pa_write, &p, sizeof p); pa_write_fn w = w_pa_write; memcpy(&p, &w, sizeof p); }
        else if (!strcmp(n, "pa_stream_new")) { memcpy(&r_st_new, &p, sizeof p); st_new_fn w = w_st_new; memcpy(&p, &w, sizeof p); }
        else if (!strcmp(n, "pa_stream_connect_playback")) { memcpy(&r_st_conn, &p, sizeof p); st_conn_fn w = w_st_conn; memcpy(&p, &w, sizeof p); }
        else if (!strcmp(n, "pa_stream_write")) { memcpy(&r_st_write, &p, sizeof p); st_write_fn w = w_st_write; memcpy(&p, &w, sizeof p); }
        else if (!strcmp(n, "pa_stream_writable_size")) { memcpy(&r_st_wsize, &p, sizeof p); st_wsize_fn w = w_st_wsize; memcpy(&p, &w, sizeof p); }
        else if (!strcmp(n, "snd_pcm_open")) { memcpy(&r_pcm_open, &p, sizeof p); pcm_open_fn w = w_pcm_open; memcpy(&p, &w, sizeof p); }
        else if (!strcmp(n, "snd_pcm_writei")) { memcpy(&r_pcm_writei, &p, sizeof p); pcm_writei_fn w = w_pcm_writei; memcpy(&p, &w, sizeof p); }
    }
    return p;
}
