/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fmod_bridge.c -- FMOD-wrapper voor Raw Thrills-games die FMOD Studio 1.10 vragen (libfmod.so.10, libfmodstudio.so.10;
 * Halo: Fireteam Raven); de game levert die bibliotheken niet mee. Dit is GEEN FMOD-implementatie en bevat geen FMOD-code: het
 * zijn de 60 namen die de game opvraagt (tools/fmod-bridge/symbols.txt), met de handtekening van de FMOD Studio 1.10-API. Heeft
 * de gebruiker een native FMOD 1.10 als drop-in (RG_FMOD_NATIVE_DIRS, hieronder), dan gaat elke aanroep rechtstreeks daarheen.
 * Anders gaat hij over een pijp naar rg-fmod-host.exe (rg_fmod_host.c). Dat hulpprogramma draait onder Wine en laadt de echte
 * FMOD Studio 1.10.2 voor Windows (fmod64.dll, fmodstudio64.dll) uit de eigen installatie van de gebruiker; FMOD mengt daar in
 * 5.1 en Wine speelt het af via PipeWire/PulseAudio. Zonder beide blijft het stil.
 *
 * Handles: de waarden van FMOD in het hulpprogramma gaan ongewijzigd naar de game (die ze alleen teruggeeft). Wat de game zelf
 * aan een handle hangt (userdata, callbacks) blijft hier. Callbacks: het hulpprogramma verzamelt ze en geeft ze mee in het
 * antwoord op Studio::System::update(); hier worden ze dan op de draad van de game aangeroepen, zoals FMOD het zelf doet.
 *
 * Valt het hulpprogramma weg (geen Wine, geen DLL's, de pijp breekt), dan gedraagt de wrapper zich als de stille stub: elke
 * aanroep FMOD_OK met lege waarden, en de game draait zonder geluid door.
 *
 * Omgeving: RG_FMOD_WINE (het wine-programma), RG_FMOD_HOST (rg-fmod-host.exe), RG_FMOD_DLLDIR (map met fmod64.dll en
 * fmodstudio64.dll), WINEPREFIX, RG_FMOD_SPEAKERS ("5.1" standaard, "stereo", "7.1", "game" = wat de game vraagt),
 * RG_FMOD_LOG=1 (meldingen op stderr; het hulpprogramma schrijft dan ook naar stderr).
 *
 * BOUWEN: tools/dev/build-fmod-bridge.sh (libfmod.so.10 en libfmodstudio.so.10 uit deze bron).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fmod_bridge_proto.h"

#define FMOD_OK 0
#define FMOD_ERR_INTERNAL 28
#define EXPORT __attribute__((visibility("default")))

extern char **environ;

static int log_on;
static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...) {
    if (!log_on) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("rg_fmod: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* ------------------------------------------------------------------ de pijp */
static pthread_mutex_t pijp_mu = PTHREAD_MUTEX_INITIALIZER;
static int naar = -1, van = -1; /* naar het hulpprogramma (zijn stdin), van het hulpprogramma (zijn stdout) */
static volatile int up;         /* 1 = het hulpprogramma antwoordt */
static pid_t host_pid;

static int schrijf_alles(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += w, n -= (size_t)w;
    }
    return 0;
}
static int lees_alles(int fd, void *buf, size_t n, int timeout_ms) {
    char *p = buf;
    while (n) {
        if (timeout_ms >= 0) {
            struct pollfd pf = {fd, POLLIN, 0};
            int r = poll(&pf, 1, timeout_ms);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) return -1;
        }
        ssize_t g = read(fd, p, n);
        if (g < 0 && errno == EINTR) continue;
        if (g <= 0) return -1;
        p += g, n -= (size_t)g;
    }
    return 0;
}

/* Een verzoek opbouwen. */
typedef struct { unsigned char *b; size_t n, cap; } buf_t;
static void put(buf_t *b, const void *p, size_t n) {
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 256;
        while (c < b->n + n) c *= 2;
        unsigned char *nb = realloc(b->b, c);
        if (!nb) return;
        b->b = nb, b->cap = c;
    }
    memcpy(b->b + b->n, p, n);
    b->n += n;
}
static void put_u32(buf_t *b, uint32_t v) { put(b, &v, 4); }
static void put_i32(buf_t *b, int32_t v) { put(b, &v, 4); }
static void put_f32(buf_t *b, float v) { put(b, &v, 4); }
static void put_u64(buf_t *b, const void *h) { uint64_t v = (uint64_t)(uintptr_t)h; put(b, &v, 8); }
static void put_str(buf_t *b, const char *s) { uint32_t n = s ? (uint32_t)strlen(s) : 0; put_u32(b, n); if (n) put(b, s, n); }

/* Een antwoord lezen. */
typedef struct { const unsigned char *p; size_t n, at; } rd_t;
static int get(rd_t *r, void *o, size_t n) { if (r->at + n > r->n) { memset(o, 0, n); return -1; } memcpy(o, r->p + r->at, n); r->at += n; return 0; }
static uint32_t get_u32(rd_t *r) { uint32_t v; get(r, &v, 4); return v; }
static int32_t get_i32(rd_t *r) { int32_t v; get(r, &v, 4); return v; }
static float get_f32(rd_t *r) { float v; get(r, &v, 4); return v; }
static void *get_h(rd_t *r) { uint64_t v; get(r, &v, 8); return (void *)(uintptr_t)v; }

static void weg(const char *waarom) {
    if (up) fprintf(stderr, "rg_fmod: het hulpprogramma antwoordt niet meer (%s); verder zonder geluid\n", waarom);
    up = 0;
}

/* Verstuur een verzoek; wacht bij RG_FM_ANTWOORD op het antwoord (in *ant, door de aanroeper vrij te geven). Geeft het
   FMOD-resultaat, of -1 als er geen hulpprogramma is (dan doet de aanroeper wat de stub deed). */
static int stuur(uint32_t op, buf_t *b, unsigned char **ant, size_t *antn) {
    if (ant) *ant = NULL;
    if (antn) *antn = 0;
    if (!up) { free(b->b); return -1; }
    uint32_t kop[2] = {op, (uint32_t)b->n};
    int res = FMOD_OK;
    pthread_mutex_lock(&pijp_mu);
    if (schrijf_alles(naar, kop, 8) || (b->n && schrijf_alles(naar, b->b, b->n))) { weg("schrijven"); res = -1; goto klaar; }
    if (op & RG_FM_ANTWOORD) {
        int32_t akop[2];
        if (lees_alles(van, akop, 8, 30000)) { weg("lezen"); res = -1; goto klaar; }
        res = akop[0];
        if (akop[1]) {
            unsigned char *a = malloc((size_t)(uint32_t)akop[1]);
            if (!a || lees_alles(van, a, (size_t)(uint32_t)akop[1], 30000)) { free(a); weg("lezen"); res = -1; goto klaar; }
            if (ant) *ant = a, *antn = (size_t)(uint32_t)akop[1]; else free(a);
        }
    }
klaar:
    pthread_mutex_unlock(&pijp_mu);
    free(b->b);
    return res;
}

/* ------------------------------------------------------------------ het hulpprogramma starten */
static char *unix_naar_windows(const char *p) {
    /* Een Linux-pad wordt Z:\...; Wine koppelt Z: aan /. */
    size_t n = strlen(p);
    char *w = malloc(n + 3);
    if (!w) return NULL;
    w[0] = 'Z', w[1] = ':';
    for (size_t i = 0; i <= n; i++) w[i + 2] = p[i] == '/' ? '\\' : p[i];
    return w;
}

static void start_host(void) {
    const char *e = getenv("RG_FMOD_LOG");
    log_on = e && *e == '1';
    const char *wine = getenv("RG_FMOD_WINE"), *host = getenv("RG_FMOD_HOST"), *dlls = getenv("RG_FMOD_DLLDIR");
    /* De keuze uit de opties van de game: "STEREO" of "LPCM 5.1" (de daemon maakt er "stereo" of "5.1" van; beide vormen gaan). */
    const char *spk_env = getenv("RG_FMOD_SPEAKERS"), *spk = "5.1";
    if (spk_env && (strcasestr(spk_env, "stereo"))) spk = "stereo";
    else if (spk_env && strstr(spk_env, "7.1")) spk = "7.1";
    else if (spk_env && !strcasecmp(spk_env, "game")) spk = "game";
    if (!wine || !host || !dlls) { fprintf(stderr, "rg_fmod: RG_FMOD_WINE, RG_FMOD_HOST of RG_FMOD_DLLDIR ontbreekt; geen geluid\n"); return; }
    if (access(wine, X_OK) || access(host, R_OK)) { fprintf(stderr, "rg_fmod: %s of %s ontbreekt; geen geluid\n", wine, host); return; }
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC) || pipe2(out, O_CLOEXEC)) return;
    char *wdll = unix_naar_windows(dlls);
    char *argv[] = {(char *)wine, (char *)host, wdll ? wdll : (char *)dlls, (char *)spk, (char *)(log_on ? "log" : "stil"), NULL};
    /* Een schone omgeving: geen LD_PRELOAD (de hulpen van de game horen niet in Wine) en geen bibliotheekpad van de game. */
    size_t ne = 0;
    while (environ[ne]) ne++;
    char **env = calloc(ne + 8, sizeof *env);
    size_t k = 0;
    for (size_t i = 0; i < ne; i++) {
        if (!strncmp(environ[i], "LD_PRELOAD=", 11) || !strncmp(environ[i], "LD_LIBRARY_PATH=", 16) || !strncmp(environ[i], "WINEDEBUG=", 10) ||
            !strncmp(environ[i], "WINEDLLOVERRIDES=", 17) || !strncmp(environ[i], "DISPLAY=", 8) || !strncmp(environ[i], "WAYLAND_DISPLAY=", 16))
            continue; /* zonder scherm: een eerste prefix-opbouw zet dan geen venster over het spel */
        env[k++] = environ[i];
    }
    env[k++] = (char *)(log_on ? "WINEDEBUG=err+all,fixme-all" : "WINEDEBUG=-all");
    env[k++] = "WINEDLLOVERRIDES=mscoree=;mshtml=;winemenubuilder.exe=d";
    env[k] = NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, out[1], 1);
    int r = posix_spawn(&host_pid, wine, &fa, NULL, argv, env);
    posix_spawn_file_actions_destroy(&fa);
    free(env);
    free(wdll);
    close(in[0]);
    close(out[1]);
    if (r) { fprintf(stderr, "rg_fmod: %s starten mislukt (%s); geen geluid\n", wine, strerror(r)); close(in[1]); close(out[0]); return; }
    naar = in[1], van = out[0];
    signal(SIGPIPE, SIG_IGN);
    /* Hallo: de eerste start van een nieuwe prefix kan even duren. */
    int32_t kop[2];
    uint32_t magic = 0;
    if (lees_alles(van, kop, 8, 90000) || kop[1] != 4 || lees_alles(van, &magic, 4, 5000) || magic != RG_FM_MAGIC) {
        fprintf(stderr, "rg_fmod: het hulpprogramma meldde zich niet (FMOD-DLL's in %s?); geen geluid\n", dlls);
        close(naar), close(van), naar = van = -1;
        return;
    }
    if (kop[0] != FMOD_OK) { fprintf(stderr, "rg_fmod: het hulpprogramma kon FMOD niet laden (%d); geen geluid\n", kop[0]); return; }
    up = 1;
    fprintf(stderr, "rg_fmod: geluid via FMOD 1.10.2 onder Wine, uitvoer %s\n", !strcmp(spk, "5.1") ? "LPCM 5.1" : spk);
    logf_("hulpprogramma draait (pid %d)", (int)host_pid);
}
static pthread_once_t start_once = PTHREAD_ONCE_INIT;
static void zorg_voor_host(void) { pthread_once(&start_once, start_host); }

/* ------------------------------------------------------------------ wat de game aan handles hangt */
typedef struct { uint64_t h; void *ud; void *cb; uint32_t masker; bool bezet; } hang_t;
#define NHANG 8192
static hang_t hang[NHANG];
static pthread_mutex_t hang_mu = PTHREAD_MUTEX_INITIALIZER;

static hang_t *zoek_hang(const void *hp, bool maak) {
    uint64_t h = (uint64_t)(uintptr_t)hp;
    if (!h) return NULL;
    size_t i = (size_t)((h >> 4) * 0x9E3779B97F4A7C15ull >> 51) % NHANG, leeg = NHANG;
    for (size_t n = 0; n < NHANG; n++, i = (i + 1) % NHANG) {
        if (hang[i].bezet && hang[i].h == h) return &hang[i];
        if (!hang[i].bezet) { if (leeg == NHANG) leeg = i; if (!hang[i].h) break; } /* h == 0 en vrij: nooit gebruikt, einde van de keten */
    }
    if (!maak || leeg == NHANG) return NULL;
    hang[leeg] = (hang_t){h, NULL, NULL, 0, true};
    return &hang[leeg];
}
static void zet_ud(const void *h, void *ud) {
    pthread_mutex_lock(&hang_mu);
    hang_t *x = zoek_hang(h, true);
    if (x) x->ud = ud;
    pthread_mutex_unlock(&hang_mu);
}
static void *lees_ud(const void *h) {
    pthread_mutex_lock(&hang_mu);
    hang_t *x = zoek_hang(h, false);
    void *ud = x ? x->ud : NULL;
    pthread_mutex_unlock(&hang_mu);
    return ud;
}
static void zet_cb(const void *h, void *cb, uint32_t masker) {
    pthread_mutex_lock(&hang_mu);
    hang_t *x = zoek_hang(h, true);
    if (x) x->cb = cb, x->masker = masker;
    pthread_mutex_unlock(&hang_mu);
}
static void *lees_cb(const void *h) {
    pthread_mutex_lock(&hang_mu);
    hang_t *x = zoek_hang(h, false);
    void *cb = x ? x->cb : NULL;
    pthread_mutex_unlock(&hang_mu);
    return cb;
}
static void vergeet(const void *h) {
    pthread_mutex_lock(&hang_mu);
    hang_t *x = zoek_hang(h, false);
    if (x) x->bezet = false, x->ud = x->cb = NULL; /* h blijft staan: de keten loopt door */
    pthread_mutex_unlock(&hang_mu);
}

/* ------------------------------------------------------------------ zonder hulpprogramma: wat de stub deed */
static char stub_obj[16];
#define STUB ((void *)stub_obj)
#define OUT(p, v) do { if (p) *(p) = (v); } while (0)

/* Kortere schrijfwijzen voor de vaste vormen. */
#define B buf_t b = {0}
#define VUUR(op) do { stuur((op), &b, NULL, NULL); return FMOD_OK; } while (0) /* zonder antwoord: FMOD_OK */

static int vraag_handle(uint32_t op, buf_t *b, void **uit) {
    unsigned char *a; size_t n;
    int r = stuur(op, b, &a, &n);
    if (r < 0) { OUT(uit, STUB); return FMOD_OK; }
    rd_t rd = {a, n, 0};
    OUT(uit, r == FMOD_OK ? get_h(&rd) : NULL);
    free(a);
    return r;
}

/* ------------------------------------------------------------------ de native FMOD uit de drop-in (voorkeur) */
/* RG_FMOD_NATIVE_DIRS: mappen (gescheiden door ':') waar de gebruiker libfmod.so.10 en libfmodstudio.so.10 (FMOD Studio 1.10,
   Linux x86_64; bijvoorbeeld uit lib64/ van Celeste) neerzet: patches/<Game>/dropin/fmod/, de gebruikerslaag eerst. Staan ze er,
   dan gaat elke aanroep van de game rechtstreeks naar die echte FMOD, in hetzelfde proces, zonder Wine; alleen de luidsprekerkeuze
   en de upmix komen erbij. RTLD_GLOBAL: de echte studio-bibliotheek vindt zijn libfmod.so.10 dan via het globale bereik (de naam
   libfmod.so.10 is al van deze wrapper; de 60 gedeelde namen lopen via de wrapper en gaan van hier door naar de echte). */
#include <dlfcn.h>
#include "fmod_upmix.h"
static void *nat_ll, *nat_st;
static pthread_once_t kies_once = PTHREAD_ONCE_INIT;
static int luidsprekers = 6, upmix = 1;   /* FMOD_SPEAKERMODE: 3 stereo, 6 5.1, 7 7.1; -1 = wat de game vraagt */
static const char *luidsprekers_naam = "5.1";
static void kies_luidsprekers(void) {
    const char *e = getenv("RG_FMOD_SPEAKERS");
    if (e && strcasestr(e, "stereo")) luidsprekers = 3, upmix = 0, luidsprekers_naam = "stereo";
    else if (e && strstr(e, "7.1")) luidsprekers = 7, upmix = 0, luidsprekers_naam = "7.1";
    else if (e && !strcasecmp(e, "game")) luidsprekers = -1, upmix = 0, luidsprekers_naam = "game";
}
static void kies(void) {
    const char *l = getenv("RG_FMOD_LOG");
    log_on = l && *l == '1';
    kies_luidsprekers();
    const char *dirs = getenv("RG_FMOD_NATIVE_DIRS");
    if (!dirs || !*dirs) return;
    char *kopie = strdup(dirs), *bewaar = NULL;
    for (char *d = strtok_r(kopie, ":", &bewaar); d; d = strtok_r(NULL, ":", &bewaar)) {
        char pl[1024], ps[1024];
        snprintf(pl, sizeof pl, "%s/libfmod.so.10", d);
        snprintf(ps, sizeof ps, "%s/libfmodstudio.so.10", d);
        if (access(pl, R_OK) || access(ps, R_OK)) continue;
        void *ll = dlopen(pl, RTLD_NOW | RTLD_GLOBAL);
        void *st = ll ? dlopen(ps, RTLD_NOW | RTLD_GLOBAL) : NULL;
        if (!st) { fprintf(stderr, "rg_fmod: native FMOD in %s niet te laden (%s)\n", d, dlerror()); continue; }
        nat_ll = ll, nat_st = st;
        fprintf(stderr, "rg_fmod: geluid via de native FMOD uit %s (drop-in), uitvoer %s\n", d, upmix ? "LPCM 5.1" : luidsprekers_naam);
        break;
    }
    free(kopie);
}
static int direct_aan(void) { pthread_once(&kies_once, kies); return nat_st != NULL; }
static void *nat_sym(const char *naam) { return dlsym(strstr(naam, "6Studio") || !strncmp(naam, "FMOD_Studio", 11) ? nat_st : nat_ll, naam); }
/* Rechtstreeks naar de echte functie met dezelfde naam (de typering komt van de eigen functie). */
#define DIRECT(naam, ...)                                                                                                      \
    do {                                                                                                                       \
        if (direct_aan()) {                                                                                                    \
            static __typeof__(&naam) d_;                                                                                       \
            if (!d_) d_ = (__typeof__(&naam))nat_sym(#naam);                                                                   \
            if (d_) return d_(__VA_ARGS__);                                                                                    \
        }                                                                                                                      \
    } while (0)
/* Na Studio::System::create: de luidsprekeropstelling (voor initialize), en het versienummer in het logboek. */
static void nat_na_create(void *studio) {
    int (*getll)(void *, void **) = nat_sym("FMOD_Studio_System_GetLowLevelSystem");
    int (*fmt)(void *, int, int, int) = nat_sym("FMOD_System_SetSoftwareFormat");
    int (*ver)(void *, unsigned *) = nat_sym("FMOD_System_GetVersion");
    void *ll = NULL; unsigned v = 0;
    if (!getll || getll(studio, &ll) || !ll) return;
    if (ver && !ver(ll, &v)) fprintf(stderr, "rg_fmod: FMOD %x.%02x.%02x\n", v >> 16, (v >> 8) & 0xff, v & 0xff);
    if (luidsprekers >= 0 && fmt) logf_("luidsprekers %s: %d", luidsprekers_naam, fmt(ll, 48000, luidsprekers, 0));
}
/* Na Studio::System::initialize: de upmix voor LPCM 5.1, en wat FMOD er echt van maakte. */
static void nat_na_initialize(void *studio) {
    int (*getll)(void *, void **) = nat_sym("FMOD_Studio_System_GetLowLevelSystem");
    int (*getfmt)(void *, int *, int *, int *) = nat_sym("FMOD_System_GetSoftwareFormat");
    int (*getout)(void *, int *) = nat_sym("FMOD_System_GetOutput");
    void *ll = NULL; int sr = 0, sm = 0, raw = 0, out = -1;
    if (!getll || getll(studio, &ll) || !ll) return;
    if (getfmt) getfmt(ll, &sr, &sm, &raw);
    if (getout) getout(ll, &out);
    fprintf(stderr, "rg_fmod: FMOD klaar: uitvoer %d, menging %d (%s), %d Hz\n", out, sm, sm == 6 ? "5.1" : sm == 3 ? "stereo" : sm == 7 ? "7.1" : "?", sr);
    if (upmix && sm == 6) {
        int f[3];
        int ok = rg_upmix_installeer(ll, sr > 0 ? sr : 48000, (int (*)(void *, const void *, void **))nat_sym("FMOD_System_CreateDSP"),
                                     (int (*)(void *, void **))nat_sym("FMOD_System_GetMasterChannelGroup"),
                                     (int (*)(void *, int, void *))nat_sym("FMOD_ChannelGroup_AddDSP"), (int (*)(void *, int))nat_sym("FMOD_DSP_SetActive"), f) == 0;
        fprintf(stderr, "rg_fmod: 5.1-upmix (stereo -> 5.1) op de master: %s (%d/%d/%d)\n", ok ? "aan" : "MISLUKT", f[0], f[1], f[2]);
    }
}

/* ------------------------------------------------------------------ libfmod (laag niveau) */
EXPORT int FMOD_Debug_Initialize(unsigned flags, int mode, void *cb, const char *file) {
    DIRECT(FMOD_Debug_Initialize, flags, mode, cb, file); (void)flags, (void)mode, (void)cb, (void)file; return FMOD_OK; }
EXPORT int FMOD_Memory_Initialize(void *pool, int len, void *a, void *r, void *f, unsigned type) {
    DIRECT(FMOD_Memory_Initialize, pool, len, a, r, f, type);
    (void)pool, (void)len, (void)a, (void)r, (void)f, (void)type;
    return FMOD_OK;
}

EXPORT int _ZN4FMOD14ChannelControl11getUserDataEPPv(void *self, void **ud) {
    DIRECT(_ZN4FMOD14ChannelControl11getUserDataEPPv, self, ud); OUT(ud, lees_ud(self)); return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl11setCallbackEPF11FMOD_RESULTP19FMOD_CHANNELCONTROL24FMOD_CHANNELCONTROL_TYPE33FMOD_CHANNELCONTROL_CALLBACK_TYPEPvS6_E(void *self, void *cb) {
    DIRECT(_ZN4FMOD14ChannelControl11setCallbackEPF11FMOD_RESULTP19FMOD_CHANNELCONTROL24FMOD_CHANNELCONTROL_TYPE33FMOD_CHANNELCONTROL_CALLBACK_TYPEPvS6_E, self, cb);
    zet_cb(self, cb, 0);
    B; put_u64(&b, self); put_i32(&b, cb != NULL); VUUR(RG_FM_CC_SETCALLBACK);
}
EXPORT int _ZN4FMOD14ChannelControl11setUserDataEPv(void *self, void *ud) {
    DIRECT(_ZN4FMOD14ChannelControl11setUserDataEPv, self, ud); zet_ud(self, ud); return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl15set3DAttributesEPK11FMOD_VECTORS3_S3_(void *self, const void *p, const void *v, const void *a) {
    DIRECT(_ZN4FMOD14ChannelControl15set3DAttributesEPK11FMOD_VECTORS3_S3_, self, p, v, a);
    static const char nul[12];
    B; put_u64(&b, self); put_u32(&b, (p ? 1u : 0u) | (v ? 2u : 0u) | (a ? 4u : 0u));
    put(&b, p ? p : nul, 12); put(&b, v ? v : nul, 12); put(&b, a ? a : nul, 12);
    VUUR(RG_FM_CC_SET3D);
}
EXPORT int _ZN4FMOD14ChannelControl4stopEv(void *self) {
    DIRECT(_ZN4FMOD14ChannelControl4stopEv, self); B; put_u64(&b, self); VUUR(RG_FM_CC_STOP); }
EXPORT int _ZN4FMOD14ChannelControl6getDSPEiPPNS_3DSPE(void *self, int i, void **dsp) {
    DIRECT(_ZN4FMOD14ChannelControl6getDSPEiPPNS_3DSPE, self, i, dsp); B; put_u64(&b, self); put_i32(&b, i); return vraag_handle(RG_FM_CC_GETDSP, &b, dsp); }
EXPORT int _ZN4FMOD14ChannelControl6setPanEf(void *self, float v) {
    DIRECT(_ZN4FMOD14ChannelControl6setPanEf, self, v); B; put_u64(&b, self); put_f32(&b, v); VUUR(RG_FM_CC_SETPAN); }
EXPORT int _ZN4FMOD14ChannelControl7setModeEj(void *self, unsigned m) {
    DIRECT(_ZN4FMOD14ChannelControl7setModeEj, self, m); B; put_u64(&b, self); put_u32(&b, m); VUUR(RG_FM_CC_SETMODE); }
EXPORT int _ZN4FMOD14ChannelControl9setPausedEb(void *self, bool p) {
    DIRECT(_ZN4FMOD14ChannelControl9setPausedEb, self, p); B; put_u64(&b, self); put_i32(&b, p); VUUR(RG_FM_CC_SETPAUSED); }
EXPORT int _ZN4FMOD14ChannelControl9setVolumeEf(void *self, float v) {
    DIRECT(_ZN4FMOD14ChannelControl9setVolumeEf, self, v); B; put_u64(&b, self); put_f32(&b, v); VUUR(RG_FM_CC_SETVOLUME); }

/* FMOD_DSP_METERING_INFO: int numsamples; float peaklevel[32]; float rmslevel[32]; short numchannels (264 bytes met uitlijning). */
EXPORT int _ZN4FMOD3DSP15getMeteringInfoEP22FMOD_DSP_METERING_INFOS2_(void *self, void *in, void *out) {
    DIRECT(_ZN4FMOD3DSP15getMeteringInfoEP22FMOD_DSP_METERING_INFOS2_, self, in, out);
    B; put_u64(&b, self);
    unsigned char *a; size_t n;
    int r = stuur(RG_FM_DSP_GETMETERING, &b, &a, &n);
    if (r < 0 || n < 528) {
        if (in) memset(in, 0, 264);
        if (out) memset(out, 0, 264);
        free(a);
        return FMOD_OK;
    }
    if (in) memcpy(in, a, 264);
    if (out) memcpy(out, a + 264, 264);
    free(a);
    return r;
}
EXPORT int _ZN4FMOD3DSP18setMeteringEnabledEbb(void *self, bool i, bool o) {
    DIRECT(_ZN4FMOD3DSP18setMeteringEnabledEbb, self, i, o); B; put_u64(&b, self); put_i32(&b, i); put_i32(&b, o); VUUR(RG_FM_DSP_SETMETERING); }
EXPORT int _ZN4FMOD5Sound7releaseEv(void *self) {
    DIRECT(_ZN4FMOD5Sound7releaseEv, self); B; put_u64(&b, self); VUUR(RG_FM_SOUND_RELEASE); }

/* FMOD_CREATESOUNDEXINFO (1.10): we sturen de getallen mee; wijzers (callbacks, lijsten, userdata) kunnen niet naar een ander
   proces en blijven leeg. Gelijke indeling op Linux (LP64) en Windows (LLP64): er zit geen 'long' in. */
typedef struct {
    int cbsize; unsigned length, fileoffset; int numchannels, defaultfrequency, format; unsigned decodebuffersize;
    int initialsubsound, numsubsounds; void *inclusionlist; int inclusionlistnum; void *pcmread, *pcmsetpos, *nonblock;
    const char *dlsname, *encryptionkey; int maxpolyphony; void *userdata; int suggestedsoundtype; void *fopen, *fclose, *fread,
    *fseek, *fasyncread, *fasynccancel; void *fileuserdata; int filebuffersize, channelorder; void *initialsoundgroup;
    unsigned initialseekposition; int initialseekpostype, ignoresetfilesystem; unsigned audioqueuepolicy, minmidigranularity;
    int nonblockthreadid; void *fsbguid;
} exinfo_t;
#define FMOD_OPENUSER 0x00000008u
#define FMOD_OPENMEMORY 0x00000800u
#define FMOD_OPENMEMORY_POINT 0x10000000u
EXPORT int _ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE(void *self, const char *n, unsigned m, void *ex, void **s) {
    DIRECT(_ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE, self, n, m, ex, s);
    zorg_voor_host();
    const exinfo_t *x = ex;
    B; put_u64(&b, self); put_u32(&b, m); put_u32(&b, x != NULL);
    if (x) {
        put_u32(&b, x->length); put_u32(&b, x->fileoffset); put_i32(&b, x->numchannels); put_i32(&b, x->defaultfrequency);
        put_i32(&b, x->format); put_u32(&b, x->decodebuffersize); put_i32(&b, x->initialsubsound); put_i32(&b, x->numsubsounds);
        put_i32(&b, x->suggestedsoundtype); put_i32(&b, x->filebuffersize); put_i32(&b, x->channelorder);
        put_u32(&b, x->initialseekposition); put_i32(&b, x->initialseekpostype);
        put_str(&b, x->encryptionkey); put_str(&b, x->dlsname);
        if (x->pcmread || x->nonblock || x->fopen || x->userdata) logf_("createSound: exinfo met callbacks/userdata (genegeerd), modus %#x", m);
    }
    if ((m & (FMOD_OPENMEMORY | FMOD_OPENMEMORY_POINT)) && x) { put_u32(&b, x->length); put(&b, n, x->length); }
    else if (m & FMOD_OPENUSER) put_str(&b, NULL);
    else {
        char *w = n && n[0] == '/' ? unix_naar_windows(n) : NULL;
        put_str(&b, w ? w : n);
        free(w);
    }
    logf_("createSound %s modus %#x", (m & (FMOD_OPENMEMORY | FMOD_OPENMEMORY_POINT)) ? "(geheugen)" : n ? n : "(leeg)", m);
    return vraag_handle(RG_FM_LL_CREATESOUND, &b, s);
}
EXPORT int _ZN4FMOD6System13get3DSettingsEPfS1_S1_(void *self, float *doppler, float *dist, float *rolloff) {
    DIRECT(_ZN4FMOD6System13get3DSettingsEPfS1_S1_, self, doppler, dist, rolloff);
    B; put_u64(&b, self);
    unsigned char *a; size_t n;
    int r = stuur(RG_FM_LL_GET3DSETTINGS, &b, &a, &n);
    rd_t rd = {a, n, 0};
    float d = r < 0 ? 1.0f : get_f32(&rd), di = r < 0 ? 1.0f : get_f32(&rd), ro = r < 0 ? 1.0f : get_f32(&rd);
    OUT(doppler, d); OUT(dist, di); OUT(rolloff, ro);
    free(a);
    return r < 0 ? FMOD_OK : r;
}
EXPORT int _ZN4FMOD6System13getDriverInfoEiPciP9FMOD_GUIDPiP16FMOD_SPEAKERMODES4_(void *self, int id, char *name, int len, void *guid, int *rate,
                                                                                    int *mode, int *channels) {
    DIRECT(_ZN4FMOD6System13getDriverInfoEiPciP9FMOD_GUIDPiP16FMOD_SPEAKERMODES4_, self, id, name, len, guid, rate, mode, channels);
    B; put_u64(&b, self); put_i32(&b, id); put_i32(&b, len);
    unsigned char *a; size_t n;
    int r = stuur(RG_FM_LL_GETDRIVERINFO, &b, &a, &n);
    if (r < 0) {
        if (name && len > 0) snprintf(name, (size_t)len, "%s", "RavenGun (stil)");
        if (guid) memset(guid, 0, 16);
        OUT(rate, 48000); OUT(mode, 3); OUT(channels, 2);
        return FMOD_OK;
    }
    rd_t rd = {a, n, 0};
    uint32_t sl = get_u32(&rd);
    if (name && len > 0) {
        size_t c = sl < (uint32_t)len - 1 ? sl : (size_t)len - 1;
        get(&rd, name, c);
        name[c] = 0;
        rd.at += sl - c;
    } else rd.at += sl;
    unsigned char g[16]; get(&rd, g, 16);
    if (guid) memcpy(guid, g, 16);
    int ra = get_i32(&rd), mo = get_i32(&rd), ch = get_i32(&rd);
    OUT(rate, ra); OUT(mode, mo); OUT(channels, ch);
    logf_("getDriverInfo %d: %s, %d Hz, modus %d, %d kanalen", id, name ? name : "", ra, mo, ch);
    free(a);
    return r;
}
static int vraag_i32(uint32_t op, buf_t *b, int *uit, int stub) {
    unsigned char *a; size_t n;
    int r = stuur(op, b, &a, &n);
    rd_t rd = {a, n, 0};
    OUT(uit, r < 0 ? stub : get_i32(&rd));
    free(a);
    return r < 0 ? FMOD_OK : r;
}
static int vraag_f32(uint32_t op, buf_t *b, float *uit, float stub) {
    unsigned char *a; size_t n;
    int r = stuur(op, b, &a, &n);
    rd_t rd = {a, n, 0};
    OUT(uit, r < 0 ? stub : get_f32(&rd));
    free(a);
    return r < 0 ? FMOD_OK : r;
}
EXPORT int _ZN4FMOD6System13getNumDriversEPi(void *self, int *n) {
    DIRECT(_ZN4FMOD6System13getNumDriversEPi, self, n); B; put_u64(&b, self); return vraag_i32(RG_FM_LL_GETNUMDRIVERS, &b, n, 1); }
EXPORT int _ZN4FMOD6System13set3DSettingsEfff(void *self, float a, float c, float d) {
    DIRECT(_ZN4FMOD6System13set3DSettingsEfff, self, a, c, d); B; put_u64(&b, self); put_f32(&b, a); put_f32(&b, c); put_f32(&b, d); VUUR(RG_FM_LL_SET3DSETTINGS); }
EXPORT int _ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi(void *self, int rate, int mode, int raw) {
    if (direct_aan()) {
        static __typeof__(&_ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi) d_;
        if (!d_) d_ = (__typeof__(&_ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi))nat_sym("_ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi");
        logf_("setSoftwareFormat: de game vraagt modus %d, het wordt %d", mode, luidsprekers >= 0 ? luidsprekers : mode);
        return d_ ? d_(self, rate, luidsprekers >= 0 ? luidsprekers : mode, luidsprekers >= 0 ? 0 : raw) : 28;
    }
    logf_("setSoftwareFormat %d Hz, modus %d, raw %d (de game)", rate, mode, raw);
    B; put_u64(&b, self); put_i32(&b, rate); put_i32(&b, mode); put_i32(&b, raw);
    int r = stuur(RG_FM_LL_SETSWFORMAT, &b, NULL, NULL);
    return r < 0 ? FMOD_OK : r;
}
EXPORT int _ZN4FMOD6System9getDriverEPi(void *self, int *d) {
    DIRECT(_ZN4FMOD6System9getDriverEPi, self, d); B; put_u64(&b, self); return vraag_i32(RG_FM_LL_GETDRIVER, &b, d, 0); }
EXPORT int _ZN4FMOD6System9playSoundEPNS_5SoundEPNS_12ChannelGroupEbPPNS_7ChannelE(void *self, void *s, void *g, bool paused, void **ch) {
    DIRECT(_ZN4FMOD6System9playSoundEPNS_5SoundEPNS_12ChannelGroupEbPPNS_7ChannelE, self, s, g, paused, ch);
    B; put_u64(&b, self); put_u64(&b, s); put_u64(&b, g); put_i32(&b, paused);
    return vraag_handle(RG_FM_LL_PLAYSOUND, &b, ch);
}
EXPORT int _ZN4FMOD6System9setDriverEi(void *self, int d) {
    DIRECT(_ZN4FMOD6System9setDriverEi, self, d);
    logf_("setDriver %d (de game)", d);
    B; put_u64(&b, self); put_i32(&b, d);
    int r = stuur(RG_FM_LL_SETDRIVER, &b, NULL, NULL);
    return r < 0 ? FMOD_OK : r;
}
EXPORT int _ZN4FMOD7Channel11setPositionEjj(void *self, unsigned p, unsigned u) {
    DIRECT(_ZN4FMOD7Channel11setPositionEjj, self, p, u); B; put_u64(&b, self); put_u32(&b, p); put_u32(&b, u); VUUR(RG_FM_CH_SETPOSITION); }
EXPORT int _ZN4FMOD7Channel12setLoopCountEi(void *self, int n) {
    DIRECT(_ZN4FMOD7Channel12setLoopCountEi, self, n); B; put_u64(&b, self); put_i32(&b, n); VUUR(RG_FM_CH_SETLOOPCOUNT); }

/* ------------------------------------------------------------------ libfmodstudio */
static void *laag_niveau; /* het laag-niveausysteem van FMOD in het hulpprogramma */
EXPORT int _ZN4FMOD6Studio6System6createEPPS1_j(void **sys, unsigned version) {
    if (direct_aan()) {
        static __typeof__(&_ZN4FMOD6Studio6System6createEPPS1_j) d_;
        if (!d_) d_ = (__typeof__(&_ZN4FMOD6Studio6System6createEPPS1_j))nat_sym("_ZN4FMOD6Studio6System6createEPPS1_j");
        int r = d_ ? d_(sys, version) : 28;
        if (r == 0 && sys && *sys) nat_na_create(*sys);
        return r;
    }
    zorg_voor_host();
    logf_("Studio::System::create (headerversie %#x)", version);
    B;
    unsigned char *a; size_t n;
    int r = stuur(RG_FM_STUDIO_CREATE, &b, &a, &n);
    if (r < 0) { OUT(sys, STUB); return FMOD_OK; }
    rd_t rd = {a, n, 0};
    OUT(sys, get_h(&rd));
    laag_niveau = get_h(&rd);
    free(a);
    return r;
}
EXPORT int _ZN4FMOD6Studio6System10initializeEijjPv(void *self, int maxch, unsigned sf, unsigned f, void *extra) {
    if (direct_aan()) {
        static __typeof__(&_ZN4FMOD6Studio6System10initializeEijjPv) d_;
        if (!d_) d_ = (__typeof__(&_ZN4FMOD6Studio6System10initializeEijjPv))nat_sym("_ZN4FMOD6Studio6System10initializeEijjPv");
        int r = d_ ? d_(self, maxch, sf, f, extra) : 28;
        if (r == 0) nat_na_initialize(self);
        return r;
    }
    (void)extra;
    logf_("Studio::System::initialize maxch %d, studioflags %#x, flags %#x", maxch, sf, f);
    B; put_u64(&b, self); put_i32(&b, maxch); put_u32(&b, sf); put_u32(&b, f);
    int r = stuur(RG_FM_STUDIO_INITIALIZE, &b, NULL, NULL);
    return r < 0 ? FMOD_OK : r;
}
EXPORT int _ZN4FMOD6Studio6System12loadBankFileEPKcjPPNS0_4BankE(void *self, const char *file, unsigned flags, void **bank) {
    DIRECT(_ZN4FMOD6Studio6System12loadBankFileEPKcjPPNS0_4BankE, self, file, flags, bank);
    char *w = file && file[0] == '/' ? unix_naar_windows(file) : NULL;
    B; put_u64(&b, self); put_str(&b, w ? w : file); put_u32(&b, flags);
    free(w);
    int r = vraag_handle(RG_FM_LOADBANK, &b, bank);
    logf_("loadBankFile %s: %d", file ? file : "", r);
    return r;
}
EXPORT int _ZN4FMOD6Studio6System13flushCommandsEv(void *self) {
    DIRECT(_ZN4FMOD6Studio6System13flushCommandsEv, self); B; put_u64(&b, self); int r = stuur(RG_FM_FLUSH, &b, NULL, NULL); return r < 0 ? FMOD_OK : r; }
EXPORT int _ZN4FMOD6Studio6System21setListenerAttributesEiPK18FMOD_3D_ATTRIBUTES(void *self, int l, const void *a) {
    DIRECT(_ZN4FMOD6Studio6System21setListenerAttributesEiPK18FMOD_3D_ATTRIBUTES, self, l, a);
    if (!a) return FMOD_OK;
    B; put_u64(&b, self); put_i32(&b, l); put(&b, a, 48); VUUR(RG_FM_SET_LISTENER);
}

/* De callbacks die het hulpprogramma sinds de vorige update verzamelde, nu op de draad van de game. */
typedef int (*event_cb_t)(unsigned type, void *event, void *params);
typedef int (*kanaal_cb_t)(void *cc, int controltype, int callbacktype, void *d1, void *d2);
#define FMOD_STUDIO_EVENT_CALLBACK_DESTROYED 0x00000010u
#define FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER 0x00000800u
#define FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_BEAT 0x00001000u
#define FMOD_STUDIO_EVENT_CALLBACK_SOUND_PLAYED 0x00002000u
#define FMOD_STUDIO_EVENT_CALLBACK_SOUND_STOPPED 0x00004000u
static void doe_callbacks(rd_t *rd) {
    int32_t n = get_i32(rd);
    for (int32_t i = 0; i < n && rd->at < rd->n; i++) {
        uint32_t soort = get_u32(rd);
        void *h = get_h(rd);
        uint32_t type = get_u32(rd);
        uint64_t a, bb;
        get(rd, &a, 8), get(rd, &bb, 8);
        uint32_t bl = get_u32(rd);
        char blok[256] = {0};
        size_t c = bl < sizeof blok - 1 ? bl : sizeof blok - 1;
        get(rd, blok, c);
        rd->at += bl - c;
        if (soort == RG_FM_CB_EVENT) {
            event_cb_t cb = (event_cb_t)lees_cb(h);
            if (cb) {
                struct { const char *name; int position; } marker = {blok, (int)a};
                void *params = NULL;
                if (type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_MARKER) params = &marker;
                else if (type == FMOD_STUDIO_EVENT_CALLBACK_TIMELINE_BEAT) params = blok;
                else if (type == FMOD_STUDIO_EVENT_CALLBACK_SOUND_PLAYED || type == FMOD_STUDIO_EVENT_CALLBACK_SOUND_STOPPED) params = (void *)(uintptr_t)a;
                cb(type, h, params);
            }
            if (type == FMOD_STUDIO_EVENT_CALLBACK_DESTROYED) vergeet(h);
        } else if (soort == RG_FM_CB_KANAAL) {
            kanaal_cb_t cb = (kanaal_cb_t)lees_cb(h);
            if (cb) cb(h, (int)a, (int)type, (void *)(uintptr_t)bb, NULL);
        }
    }
}
EXPORT int _ZN4FMOD6Studio6System6updateEv(void *self) {
    DIRECT(_ZN4FMOD6Studio6System6updateEv, self);
    B; put_u64(&b, self);
    unsigned char *a; size_t n;
    int r = stuur(RG_FM_UPDATE, &b, &a, &n);
    if (r < 0) return FMOD_OK;
    rd_t rd = {a, n, 0};
    doe_callbacks(&rd);
    free(a);
    return r;
}
EXPORT int _ZN4FMOD6Studio6System7releaseEv(void *self) {
    DIRECT(_ZN4FMOD6Studio6System7releaseEv, self); B; put_u64(&b, self); int r = stuur(RG_FM_STUDIO_RELEASE, &b, NULL, NULL); return r < 0 ? FMOD_OK : r; }
EXPORT int _ZNK4FMOD6Studio6System17getLowLevelSystemEPPNS_6SystemE(const void *self, void **ll) {
    DIRECT(_ZNK4FMOD6Studio6System17getLowLevelSystemEPPNS_6SystemE, self, ll);
    (void)self;
    OUT(ll, up ? laag_niveau : STUB); /* kwam mee met create */
    return FMOD_OK;
}
EXPORT int _ZNK4FMOD6Studio6System6getBusEPKcPPNS0_3BusE(const void *self, const char *path, void **bus) {
    DIRECT(_ZNK4FMOD6Studio6System6getBusEPKcPPNS0_3BusE, self, path, bus);
    B; put_u64(&b, self); put_str(&b, path);
    int r = vraag_handle(RG_FM_GET_BUS, &b, bus);
    logf_("getBus %s: %d", path ? path : "", r);
    return r;
}
EXPORT int _ZNK4FMOD6Studio6System8getEventEPKcPPNS0_16EventDescriptionE(const void *self, const char *path, void **d) {
    DIRECT(_ZNK4FMOD6Studio6System8getEventEPKcPPNS0_16EventDescriptionE, self, path, d);
    B; put_u64(&b, self); put_str(&b, path);
    int r = vraag_handle(RG_FM_GET_EVENT, &b, d);
    if (r) logf_("getEvent %s: %d", path ? path : "", r);
    return r;
}

EXPORT int _ZN4FMOD6Studio4Bank14loadSampleDataEv(void *self) {
    DIRECT(_ZN4FMOD6Studio4Bank14loadSampleDataEv, self); B; put_u64(&b, self); int r = stuur(RG_FM_BANK_LOADSAMPLE, &b, NULL, NULL); return r < 0 ? FMOD_OK : r; }
EXPORT int _ZN4FMOD6Studio4Bank6unloadEv(void *self) {
    DIRECT(_ZN4FMOD6Studio4Bank6unloadEv, self); B; put_u64(&b, self); int r = stuur(RG_FM_BANK_UNLOAD, &b, NULL, NULL); return r < 0 ? FMOD_OK : r; }
EXPORT int _ZN4FMOD6Studio3Bus13stopAllEventsE21FMOD_STUDIO_STOP_MODE(void *self, int m) {
    DIRECT(_ZN4FMOD6Studio3Bus13stopAllEventsE21FMOD_STUDIO_STOP_MODE, self, m); B; put_u64(&b, self); put_i32(&b, m); VUUR(RG_FM_BUS_STOPALL); }
EXPORT int _ZN4FMOD6Studio3Bus7setMuteEb(void *self, bool m) {
    DIRECT(_ZN4FMOD6Studio3Bus7setMuteEb, self, m); B; put_u64(&b, self); put_i32(&b, m); VUUR(RG_FM_BUS_SETMUTE); }
EXPORT int _ZN4FMOD6Studio3Bus9setVolumeEf(void *self, float v) {
    DIRECT(_ZN4FMOD6Studio3Bus9setVolumeEf, self, v); B; put_u64(&b, self); put_f32(&b, v); VUUR(RG_FM_BUS_SETVOLUME); }
EXPORT int _ZNK4FMOD6Studio3Bus15getChannelGroupEPPNS_12ChannelGroupE(const void *self, void **g) {
    DIRECT(_ZNK4FMOD6Studio3Bus15getChannelGroupEPPNS_12ChannelGroupE, self, g); B; put_u64(&b, self); return vraag_handle(RG_FM_BUS_GETCG, &b, g); }

EXPORT int _ZNK4FMOD6Studio16EventDescription14createInstanceEPPNS0_13EventInstanceE(const void *self, void **inst) {
    DIRECT(_ZNK4FMOD6Studio16EventDescription14createInstanceEPPNS0_13EventInstanceE, self, inst);
    B; put_u64(&b, self); return vraag_handle(RG_FM_DESC_CREATEINST, &b, inst);
}
EXPORT int _ZNK4FMOD6Studio16EventDescription15getInstanceListEPPNS0_13EventInstanceEiPi(const void *self, void **arr, int cap, int *count) {
    DIRECT(_ZNK4FMOD6Studio16EventDescription15getInstanceListEPPNS0_13EventInstanceEiPi, self, arr, cap, count);
    B; put_u64(&b, self); put_i32(&b, cap);
    unsigned char *a; size_t n;
    int r = stuur(RG_FM_DESC_GETINSTLIST, &b, &a, &n);
    if (r < 0) { OUT(count, 0); return FMOD_OK; }
    rd_t rd = {a, n, 0};
    int c = get_i32(&rd);
    for (int i = 0; i < c && i < cap; i++) { void *h = get_h(&rd); if (arr) arr[i] = h; }
    OUT(count, c < cap ? c : cap);
    free(a);
    return r;
}
EXPORT int _ZNK4FMOD6Studio16EventDescription16getInstanceCountEPi(const void *self, int *count) {
    DIRECT(_ZNK4FMOD6Studio16EventDescription16getInstanceCountEPi, self, count); B; put_u64(&b, self); return vraag_i32(RG_FM_DESC_GETINSTCOUNT, &b, count, 0); }
EXPORT int _ZNK4FMOD6Studio16EventDescription18getMaximumDistanceEPf(const void *self, float *d) {
    DIRECT(_ZNK4FMOD6Studio16EventDescription18getMaximumDistanceEPf, self, d); B; put_u64(&b, self); return vraag_f32(RG_FM_DESC_GETMAXDIST, &b, d, 100.0f); }
EXPORT int _ZNK4FMOD6Studio16EventDescription18getMinimumDistanceEPf(const void *self, float *d) {
    DIRECT(_ZNK4FMOD6Studio16EventDescription18getMinimumDistanceEPf, self, d); B; put_u64(&b, self); return vraag_f32(RG_FM_DESC_GETMINDIST, &b, d, 1.0f); }
EXPORT int _ZNK4FMOD6Studio16EventDescription7getPathEPciPi(const void *self, char *path, int size, int *got) {
    DIRECT(_ZNK4FMOD6Studio16EventDescription7getPathEPciPi, self, path, size, got);
    B; put_u64(&b, self); put_i32(&b, size);
    unsigned char *a; size_t n;
    int r = stuur(RG_FM_DESC_GETPATH, &b, &a, &n);
    if (r < 0) { if (path && size > 0) path[0] = 0; OUT(got, 1); return FMOD_OK; }
    rd_t rd = {a, n, 0};
    int g = get_i32(&rd);
    uint32_t sl = get_u32(&rd);
    if (path && size > 0) { size_t c = sl < (uint32_t)size - 1 ? sl : (size_t)size - 1; get(&rd, path, c); path[c] = 0; }
    OUT(got, g);
    free(a);
    return r;
}

EXPORT int _ZN4FMOD6Studio13EventInstance11setCallbackEPF11FMOD_RESULTjP25FMOD_STUDIO_EVENTINSTANCEPvEj(void *self, void *cb, unsigned mask) {
    DIRECT(_ZN4FMOD6Studio13EventInstance11setCallbackEPF11FMOD_RESULTjP25FMOD_STUDIO_EVENTINSTANCEPvEj, self, cb, mask);
    zet_cb(self, cb, mask);
    B; put_u64(&b, self); put_u32(&b, cb ? mask : 0); VUUR(RG_FM_EI_SETCALLBACK);
}
EXPORT int _ZN4FMOD6Studio13EventInstance11setUserDataEPv(void *self, void *ud) {
    DIRECT(_ZN4FMOD6Studio13EventInstance11setUserDataEPv, self, ud); zet_ud(self, ud); return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio13EventInstance15set3DAttributesEPK18FMOD_3D_ATTRIBUTES(void *self, const void *a) {
    DIRECT(_ZN4FMOD6Studio13EventInstance15set3DAttributesEPK18FMOD_3D_ATTRIBUTES, self, a);
    if (!a) return FMOD_OK;
    B; put_u64(&b, self); put(&b, a, 48); VUUR(RG_FM_EI_SET3D);
}
EXPORT int _ZN4FMOD6Studio13EventInstance17setParameterValueEPKcf(void *self, const char *n, float v) {
    DIRECT(_ZN4FMOD6Studio13EventInstance17setParameterValueEPKcf, self, n, v); B; put_u64(&b, self); put_str(&b, n); put_f32(&b, v); VUUR(RG_FM_EI_SETPARAM); }
EXPORT int _ZN4FMOD6Studio13EventInstance4stopE21FMOD_STUDIO_STOP_MODE(void *self, int m) {
    DIRECT(_ZN4FMOD6Studio13EventInstance4stopE21FMOD_STUDIO_STOP_MODE, self, m); B; put_u64(&b, self); put_i32(&b, m); VUUR(RG_FM_EI_STOP); }
EXPORT int _ZN4FMOD6Studio13EventInstance5startEv(void *self) {
    DIRECT(_ZN4FMOD6Studio13EventInstance5startEv, self); B; put_u64(&b, self); VUUR(RG_FM_EI_START); }
EXPORT int _ZN4FMOD6Studio13EventInstance7releaseEv(void *self) {
    DIRECT(_ZN4FMOD6Studio13EventInstance7releaseEv, self); B; put_u64(&b, self); VUUR(RG_FM_EI_RELEASE); }
EXPORT int _ZN4FMOD6Studio13EventInstance9setVolumeEf(void *self, float v) {
    DIRECT(_ZN4FMOD6Studio13EventInstance9setVolumeEf, self, v); B; put_u64(&b, self); put_f32(&b, v); VUUR(RG_FM_EI_SETVOLUME); }
EXPORT int _ZNK4FMOD6Studio13EventInstance11getUserDataEPPv(const void *self, void **ud) {
    DIRECT(_ZNK4FMOD6Studio13EventInstance11getUserDataEPPv, self, ud); OUT(ud, lees_ud(self)); return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio13EventInstance14getDescriptionEPPNS0_16EventDescriptionE(const void *self, void **d) {
    DIRECT(_ZNK4FMOD6Studio13EventInstance14getDescriptionEPPNS0_16EventDescriptionE, self, d);
    B; put_u64(&b, self); return vraag_handle(RG_FM_EI_GETDESC, &b, d);
}
/* FMOD_3D_ATTRIBUTES: vier FMOD_VECTOR's (positie, snelheid, voor, boven) = 48 bytes. */
EXPORT int _ZNK4FMOD6Studio13EventInstance15get3DAttributesEP18FMOD_3D_ATTRIBUTES(const void *self, void *a) {
    DIRECT(_ZNK4FMOD6Studio13EventInstance15get3DAttributesEP18FMOD_3D_ATTRIBUTES, self, a);
    B; put_u64(&b, self);
    unsigned char *an; size_t n;
    int r = stuur(RG_FM_EI_GET3D, &b, &an, &n);
    if (a) { if (r >= 0 && n >= 48) memcpy(a, an, 48); else memset(a, 0, 48); }
    free(an);
    return r < 0 ? FMOD_OK : r;
}
EXPORT bool _ZNK4FMOD6Studio13EventInstance7isValidEv(const void *self) {
    DIRECT(_ZNK4FMOD6Studio13EventInstance7isValidEv, self);
    if (!self) return false;
    B; put_u64(&b, self);
    unsigned char *a; size_t n;
    int r = stuur(RG_FM_EI_ISVALID, &b, &a, &n);
    if (r < 0) return self != STUB;
    rd_t rd = {a, n, 0};
    bool v = get_i32(&rd) != 0;
    free(a);
    return v;
}
