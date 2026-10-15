/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_fmod_host.c -- het Windows-hulpprogramma achter de FMOD-wrapper (fmod_bridge.c). Draait onder Wine, laadt de echte FMOD
 * Studio 1.10.2 voor Windows (fmod64.dll, fmodstudio64.dll) uit de map die de wrapper meegeeft -- de eigen installatie van de
 * gebruiker; RavenGun levert geen FMOD mee -- en voert de aanroepen uit die over stdin binnenkomen (fmod_bridge_proto.h).
 * Antwoorden gaan over stdout. FMOD mengt in de luidsprekeropstelling van argv[2] (standaard 5.1); Wine speelt af via
 * PipeWire/PulseAudio. Eigen code; alleen de C-API van FMOD wordt bij naam opgezocht (GetProcAddress).
 *
 *   rg-fmod-host.exe <DLL-map> [5.1|5.1-zonder-upmix|7.1|stereo|game] [log|stil]   (5.1 = met upmix van stereo)
 *
 * BOUWEN: scripts/build-fmod-bridge.sh (mingw-w64, x86_64).
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "fmod_bridge_proto.h"

typedef int R; /* FMOD_RESULT */
typedef void *H;
static int log_on;
static void logf_(const char *fmt, ...) {
    if (!log_on) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("rg-fmod-host: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

/* ------------------------------------------------------------------ de C-API van FMOD 1.10 (bij naam) */
#define F(ret, naam, args) static ret(*p_##naam) args;
#define FMOD_FUNCS                                                                                                            \
    F(R, FMOD_Studio_System_Create, (H *, unsigned))                                                                          \
    F(R, FMOD_Studio_System_Initialize, (H, int, unsigned, unsigned, void *))                                                \
    F(R, FMOD_Studio_System_Release, (H))                                                                                     \
    F(R, FMOD_Studio_System_Update, (H))                                                                                      \
    F(R, FMOD_Studio_System_FlushCommands, (H))                                                                               \
    F(R, FMOD_Studio_System_GetLowLevelSystem, (H, H *))                                                                      \
    F(R, FMOD_Studio_System_GetEvent, (H, const char *, H *))                                                                 \
    F(R, FMOD_Studio_System_GetBus, (H, const char *, H *))                                                                   \
    F(R, FMOD_Studio_System_SetListenerAttributes, (H, int, const void *))                                                    \
    F(R, FMOD_Studio_System_LoadBankFile, (H, const char *, unsigned, H *))                                                   \
    F(R, FMOD_Studio_Bank_LoadSampleData, (H))                                                                                \
    F(R, FMOD_Studio_Bank_Unload, (H))                                                                                        \
    F(R, FMOD_Studio_Bus_StopAllEvents, (H, int))                                                                             \
    F(R, FMOD_Studio_Bus_SetMute, (H, int))                                                                                   \
    F(R, FMOD_Studio_Bus_SetVolume, (H, float))                                                                               \
    F(R, FMOD_Studio_Bus_GetChannelGroup, (H, H *))                                                                           \
    F(R, FMOD_Studio_EventDescription_CreateInstance, (H, H *))                                                               \
    F(R, FMOD_Studio_EventDescription_GetInstanceList, (H, H *, int, int *))                                                  \
    F(R, FMOD_Studio_EventDescription_GetInstanceCount, (H, int *))                                                           \
    F(R, FMOD_Studio_EventDescription_GetMaximumDistance, (H, float *))                                                       \
    F(R, FMOD_Studio_EventDescription_GetMinimumDistance, (H, float *))                                                       \
    F(R, FMOD_Studio_EventDescription_GetPath, (H, char *, int, int *))                                                       \
    F(R, FMOD_Studio_EventInstance_SetCallback, (H, void *, unsigned))                                                        \
    F(R, FMOD_Studio_EventInstance_Set3DAttributes, (H, const void *))                                                        \
    F(R, FMOD_Studio_EventInstance_Get3DAttributes, (H, void *))                                                              \
    F(R, FMOD_Studio_EventInstance_SetParameterValue, (H, const char *, float))                                               \
    F(R, FMOD_Studio_EventInstance_Stop, (H, int))                                                                            \
    F(R, FMOD_Studio_EventInstance_Start, (H))                                                                                \
    F(R, FMOD_Studio_EventInstance_Release, (H))                                                                              \
    F(R, FMOD_Studio_EventInstance_SetVolume, (H, float))                                                                     \
    F(R, FMOD_Studio_EventInstance_GetDescription, (H, H *))                                                                  \
    F(int, FMOD_Studio_EventInstance_IsValid, (H))                                                                            \
    F(R, FMOD_System_CreateSound, (H, const char *, unsigned, void *, H *))                                                   \
    F(R, FMOD_System_Get3DSettings, (H, float *, float *, float *))                                                           \
    F(R, FMOD_System_Set3DSettings, (H, float, float, float))                                                                 \
    F(R, FMOD_System_GetDriverInfo, (H, int, char *, int, void *, int *, int *, int *))                                       \
    F(R, FMOD_System_GetNumDrivers, (H, int *))                                                                               \
    F(R, FMOD_System_GetDriver, (H, int *))                                                                                   \
    F(R, FMOD_System_SetDriver, (H, int))                                                                                     \
    F(R, FMOD_System_SetSoftwareFormat, (H, int, int, int))                                                                   \
    F(R, FMOD_System_GetSoftwareFormat, (H, int *, int *, int *))                                                             \
    F(R, FMOD_System_GetOutput, (H, int *))                                                                                   \
    F(R, FMOD_System_PlaySound, (H, H, H, int, H *))                                                                          \
    F(R, FMOD_Sound_Release, (H))                                                                                             \
    F(R, FMOD_Channel_SetPosition, (H, unsigned, unsigned))                                                                   \
    F(R, FMOD_Channel_SetLoopCount, (H, int))                                                                                 \
    F(R, FMOD_Channel_SetCallback, (H, void *))                                                                               \
    F(R, FMOD_Channel_Set3DAttributes, (H, const void *, const void *, const void *))                                          \
    F(R, FMOD_Channel_Stop, (H))                                                                                              \
    F(R, FMOD_Channel_GetDSP, (H, int, H *))                                                                                  \
    F(R, FMOD_Channel_SetPan, (H, float))                                                                                     \
    F(R, FMOD_Channel_SetMode, (H, unsigned))                                                                                 \
    F(R, FMOD_Channel_SetPaused, (H, int))                                                                                    \
    F(R, FMOD_Channel_SetVolume, (H, float))                                                                                  \
    F(R, FMOD_ChannelGroup_SetCallback, (H, void *))                                                                          \
    F(R, FMOD_ChannelGroup_Set3DAttributes, (H, const void *, const void *, const void *))                                     \
    F(R, FMOD_ChannelGroup_Stop, (H))                                                                                         \
    F(R, FMOD_ChannelGroup_GetDSP, (H, int, H *))                                                                             \
    F(R, FMOD_ChannelGroup_SetPan, (H, float))                                                                                \
    F(R, FMOD_ChannelGroup_SetMode, (H, unsigned))                                                                            \
    F(R, FMOD_ChannelGroup_SetPaused, (H, int))                                                                               \
    F(R, FMOD_ChannelGroup_SetVolume, (H, float))                                                                             \
    F(R, FMOD_DSP_GetMeteringInfo, (H, void *, void *))                                                                       \
    F(R, FMOD_DSP_SetMeteringEnabled, (H, int, int))                                                                          \
    F(R, FMOD_System_CreateDSP, (H, const void *, H *))                                                                       \
    F(R, FMOD_System_GetMasterChannelGroup, (H, H *))                                                                         \
    F(R, FMOD_ChannelGroup_AddDSP, (H, int, H))                                                                               \
    F(R, FMOD_DSP_SetActive, (H, int))
FMOD_FUNCS
#undef F

static int laad_fmod(const char *map) {
    char pad[MAX_PATH * 2];
    SetDllDirectoryA(map);
    snprintf(pad, sizeof pad, "%s\\fmod64.dll", map);
    HMODULE ll = LoadLibraryA(pad);
    snprintf(pad, sizeof pad, "%s\\fmodstudio64.dll", map);
    HMODULE st = LoadLibraryA(pad);
    if (!ll || !st) { fprintf(stderr, "rg-fmod-host: fmod64.dll/fmodstudio64.dll niet te laden uit %s (fout %lu)\n", map, GetLastError()); return 0; }
    int mist = 0;
#define F(ret, naam, args)                                                                                                    \
    p_##naam = (ret(*) args)(void *)GetProcAddress(strncmp(#naam, "FMOD_Studio_", 12) ? ll : st, #naam);                   \
    if (!p_##naam) { fprintf(stderr, "rg-fmod-host: %s ontbreekt in de FMOD-DLL\n", #naam); mist++; }
    FMOD_FUNCS
#undef F
    return mist == 0;
}

/* ------------------------------------------------------------------ pijp */
static HANDLE hin, huit;
static int lees(void *buf, DWORD n) {
    char *p = buf;
    while (n) {
        DWORD g = 0;
        if (!ReadFile(hin, p, n, &g, NULL) || !g) return -1;
        p += g, n -= g;
    }
    return 0;
}
static int schrijf(const void *buf, DWORD n) {
    const char *p = buf;
    while (n) {
        DWORD w = 0;
        if (!WriteFile(huit, p, n, &w, NULL) || !w) return -1;
        p += w, n -= w;
    }
    return 0;
}

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
static void put_h(buf_t *b, H h) { uint64_t v = (uint64_t)(uintptr_t)h; put(b, &v, 8); }
static void put_u64(buf_t *b, uint64_t v) { put(b, &v, 8); }
static void put_blok(buf_t *b, const void *p, uint32_t n) { put_u32(b, n); if (n) put(b, p, n); }

typedef struct { const unsigned char *p; size_t n, at; } rd_t;
static void get(rd_t *r, void *o, size_t n) { if (r->at + n > r->n) { memset(o, 0, n); r->at = r->n; return; } memcpy(o, r->p + r->at, n); r->at += n; }
static uint32_t get_u32(rd_t *r) { uint32_t v; get(r, &v, 4); return v; }
static int32_t get_i32(rd_t *r) { int32_t v; get(r, &v, 4); return v; }
static float get_f32(rd_t *r) { float v; get(r, &v, 4); return v; }
static H get_h(rd_t *r) { uint64_t v; get(r, &v, 8); return (H)(uintptr_t)v; }
/* Tekst als nieuwe C-string (vrij te geven). */
static char *get_str(rd_t *r) {
    uint32_t n = get_u32(r);
    char *s = malloc((size_t)n + 1);
    if (!s) return NULL;
    get(r, s, n);
    s[n] = 0;
    return s;
}

/* ------------------------------------------------------------------ callbacks: verzamelen tot de volgende update */
static CRITICAL_SECTION cb_cs;
static buf_t cb_buf;
static int32_t cb_n;
static void cb_voeg(uint32_t soort, H h, uint32_t type, uint64_t a, uint64_t b, const void *blok, uint32_t bl) {
    EnterCriticalSection(&cb_cs);
    put_u32(&cb_buf, soort); put_h(&cb_buf, h); put_u32(&cb_buf, type); put_u64(&cb_buf, a); put_u64(&cb_buf, b); put_blok(&cb_buf, blok, bl);
    cb_n++;
    LeaveCriticalSection(&cb_cs);
}
#define EV_TIMELINE_MARKER 0x00000800u
#define EV_TIMELINE_BEAT 0x00001000u
#define EV_SOUND_PLAYED 0x00002000u
#define EV_SOUND_STOPPED 0x00004000u
static R event_cb(unsigned type, H event, void *params) {
    if (type == EV_TIMELINE_MARKER && params) {
        struct { const char *name; int position; } *m = params;
        cb_voeg(RG_FM_CB_EVENT, event, type, (uint64_t)(int64_t)m->position, 0, m->name, m->name ? (uint32_t)strlen(m->name) : 0);
    } else if (type == EV_TIMELINE_BEAT && params) cb_voeg(RG_FM_CB_EVENT, event, type, 0, 0, params, 24);
    else if ((type == EV_SOUND_PLAYED || type == EV_SOUND_STOPPED)) cb_voeg(RG_FM_CB_EVENT, event, type, (uint64_t)(uintptr_t)params, 0, NULL, 0);
    else cb_voeg(RG_FM_CB_EVENT, event, type, 0, 0, NULL, 0);
    return 0;
}
static R kanaal_cb(H cc, int controltype, int callbacktype, void *d1, void *d2) {
    (void)d2;
    cb_voeg(RG_FM_CB_KANAAL, cc, (uint32_t)callbacktype, (uint64_t)(int64_t)controltype, (uint64_t)(uintptr_t)d1, NULL, 0);
    return 0;
}

/* Kanaalgroepen (uit Bus::getChannelGroup) krijgen de ChannelGroup-variant van de C-API, de rest de Channel-variant. */
#define NGROEP 256
static H groepen[NGROEP];
static int ngroepen;
static int is_groep(H h) { for (int i = 0; i < ngroepen; i++) if (groepen[i] == h) return 1; return 0; }
#define CC(naam, h, ...) (is_groep(h) ? p_FMOD_ChannelGroup_##naam(h, __VA_ARGS__) : p_FMOD_Channel_##naam(h, __VA_ARGS__))
#define CC0(naam, h) (is_groep(h) ? p_FMOD_ChannelGroup_##naam(h) : p_FMOD_Channel_##naam(h))

/* ------------------------------------------------------------------ upmix: stereo -> 5.1 */
/* Halo mengt in stereo (de master-bus in de banks is stereo; gemeten: alleen FL/FR, ook als FMOD in 5.1 mengt). Voor 5.1 maakt
   deze DSP op de master-kanaalgroep van FMOD van de stereomix een passieve 5.1-mix: FL/FR blijven, het midden krijgt de som,
   de LFE de som door een laagdoorlaat (120 Hz), links- en rechtsachter het verschil (L-R), 15 ms later en boven 7 kHz zachter.
   Wat de game zelf al naar midden/LFE/achter stuurt blijft erbij. Eigen code tegen de plugin-API van FMOD 1.10 (fmod_dsp.h). */
typedef struct { int numbuffers; int *buffernumchannels; unsigned *bufferchannelmask; float **buffers; int speakermode; } dsp_buffers;
typedef struct {
    unsigned pluginsdkversion; char name[32]; unsigned version; int numinputbuffers, numoutputbuffers;
    void *create, *release, *reset, *read, *process, *setposition; int numparameters; void **paramdesc;
    void *setfloat, *setint, *setbool, *setdata, *getfloat, *getint, *getbool, *getdata, *shouldiprocess; void *userdata;
    void *sys_register, *sys_deregister, *sys_mix;
} dsp_beschrijving;
#define DSP_QUERY 1 /* FMOD_DSP_PROCESS_OPERATION: PERFORM = 0, QUERY = 1 */
typedef struct { double b0, b1, b2, a1, a2, z1, z2; } biquad;
static float bq(biquad *f, float x) {
    double y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return (float)y;
}
static void bq_laag(biquad *f, double fc, double fs) {   /* Butterworth, tweede orde */
    double w = 2 * 3.14159265358979 * fc / fs, c = cos(w), al = sin(w) / (2 * 0.70710678), a0 = 1 + al;
    f->b0 = (1 - c) / 2 / a0; f->b1 = (1 - c) / a0; f->b2 = f->b0; f->a1 = -2 * c / a0; f->a2 = (1 - al) / a0; f->z1 = f->z2 = 0;
}
static biquad lfe_f, achter_l_f, achter_r_f;
#define VERTRAGING 720 /* 15 ms bij 48 kHz */
static float vl[VERTRAGING], vr[VERTRAGING];
static int vpos;
static R upmix_process(void *state, unsigned length, const dsp_buffers *in, dsp_buffers *out, int idle, int op) {
    (void)state;
    if (op == DSP_QUERY) {
        if (out && out->numbuffers > 0) { out->buffernumchannels[0] = 6; out->bufferchannelmask[0] = 0; out->speakermode = RG_FM_SPEAKERMODE_5POINT1; }
        return idle ? 6 /* FMOD_ERR_DSP_DONTPROCESS */ : 0;
    }
    int ich = in->buffernumchannels[0];
    const float *x = in->buffers[0];
    float *y = out->buffers[0];
    for (unsigned i = 0; i < length; i++, x += ich, y += 6) {
        float l = x[0], r = ich > 1 ? x[1] : x[0];
        float som = 0.5f * (l + r), vers = 0.5f * (l - r);
        float dl = vl[vpos], dr = vr[vpos];
        vl[vpos] = vers, vr[vpos] = -vers;
        vpos = (vpos + 1) % VERTRAGING;
        y[0] = l;
        y[1] = r;
        y[2] = 0.70710678f * som + (ich >= 6 ? x[2] : 0);
        y[3] = bq(&lfe_f, som) + (ich >= 6 ? x[3] : 0);
        y[4] = 0.70710678f * bq(&achter_l_f, dl) + (ich >= 6 ? x[4] : 0);
        y[5] = 0.70710678f * bq(&achter_r_f, dr) + (ich >= 6 ? x[5] : 0);
    }
    return 0;
}
static int upmix_aan;
static void upmix_installeer(H ll, int sr) {
    static dsp_beschrijving d;
    memset(&d, 0, sizeof d);
    d.pluginsdkversion = 110; /* FMOD_PLUGIN_SDK_VERSION van 1.10 */
    strcpy(d.name, "RavenGun 5.1-upmix");
    d.version = 1; d.numinputbuffers = 1; d.numoutputbuffers = 1;
    d.process = (void *)upmix_process;
    bq_laag(&lfe_f, 120, sr); bq_laag(&achter_l_f, 7000, sr); bq_laag(&achter_r_f, 7000, sr);
    H dsp = NULL, master = NULL;
    R a = p_FMOD_System_CreateDSP(ll, &d, &dsp);
    R b = a ? -1 : p_FMOD_System_GetMasterChannelGroup(ll, &master);
    R c = (a || b) ? -1 : p_FMOD_ChannelGroup_AddDSP(master, -1 /* FMOD_CHANNELCONTROL_DSP_HEAD */, dsp);
    if (!a && !b && !c) p_FMOD_DSP_SetActive(dsp, 1);
    fprintf(stderr, "rg-fmod-host: 5.1-upmix (stereo -> 5.1) op de master: %s (%d/%d/%d)\n", (!a && !b && !c) ? "aan" : "MISLUKT", a, b, c);
}

/* ------------------------------------------------------------------ uitvoeren */
static int speakers = RG_FM_SPEAKERMODE_5POINT1; /* -1 = wat de game vraagt */
static int rate = 48000;

static const char *naam_modus(int m) {
    static const char *n[] = {"default", "raw", "mono", "stereo", "quad", "surround", "5.1", "7.1"};
    return m >= 0 && m < 8 ? n[m] : "?";
}

static R voer_uit(uint32_t op, rd_t *rd, buf_t *ant) {
    R r = 0;
    H h, o = NULL;
    switch (op) {
    case RG_FM_STUDIO_CREATE: {
        H s = NULL, ll = NULL;
        r = p_FMOD_Studio_System_Create(&s, 0x00011002);
        if (!r) r = p_FMOD_Studio_System_GetLowLevelSystem(s, &ll);
        if (!r && speakers >= 0) {
            R f = p_FMOD_System_SetSoftwareFormat(ll, rate, speakers, 0);
            logf_("luidsprekers %s (%d Hz): %d", naam_modus(speakers), rate, f);
        }
        put_h(ant, s); put_h(ant, ll);
        logf_("Studio::System::create: %d", r);
        break;
    }
    case RG_FM_STUDIO_INITIALIZE: {
        h = get_h(rd);
        int maxch = get_i32(rd);
        unsigned sf = get_u32(rd), f = get_u32(rd);
        r = p_FMOD_Studio_System_Initialize(h, maxch, sf, f, NULL);
        H ll = NULL; int out = -1, sr = 0, sm = 0, raw = 0, drv = -1;
        p_FMOD_Studio_System_GetLowLevelSystem(h, &ll);
        if (ll) { p_FMOD_System_GetOutput(ll, &out); p_FMOD_System_GetSoftwareFormat(ll, &sr, &sm, &raw); p_FMOD_System_GetDriver(ll, &drv); }
        char dn[256] = ""; int drate = 0, dmode = 0, dch = 0;
        if (ll && drv >= 0) p_FMOD_System_GetDriverInfo(ll, drv, dn, sizeof dn, NULL, &drate, &dmode, &dch);
        fprintf(stderr, "rg-fmod-host: FMOD 1.10 klaar: %d; uitvoer %d, menging %s %d Hz, apparaat %d '%s' (%s, %d kanalen)\n", r, out,
                naam_modus(sm), sr, drv, dn, naam_modus(dmode), dch);
        if (!r && ll && upmix_aan && sm == RG_FM_SPEAKERMODE_5POINT1) upmix_installeer(ll, sr > 0 ? sr : 48000);
        break;
    }
    case RG_FM_LOADBANK: {
        h = get_h(rd);
        char *pad = get_str(rd);
        unsigned fl = get_u32(rd);
        r = p_FMOD_Studio_System_LoadBankFile(h, pad, fl, &o);
        logf_("bank %s: %d", pad, r);
        free(pad);
        put_h(ant, o);
        break;
    }
    case RG_FM_FLUSH: r = p_FMOD_Studio_System_FlushCommands(get_h(rd)); break;
    case RG_FM_SET_LISTENER: {
        h = get_h(rd);
        int l = get_i32(rd);
        unsigned char a[48]; get(rd, a, 48);
        r = p_FMOD_Studio_System_SetListenerAttributes(h, l, a);
        break;
    }
    case RG_FM_UPDATE: {
        r = p_FMOD_Studio_System_Update(get_h(rd));
        EnterCriticalSection(&cb_cs);
        put_i32(ant, cb_n);
        put(ant, cb_buf.b, cb_buf.n);
        cb_buf.n = 0, cb_n = 0;
        LeaveCriticalSection(&cb_cs);
        break;
    }
    case RG_FM_STUDIO_RELEASE: r = p_FMOD_Studio_System_Release(get_h(rd)); break;
    case RG_FM_GET_BUS: case RG_FM_GET_EVENT: {
        h = get_h(rd);
        char *pad = get_str(rd);
        r = op == RG_FM_GET_BUS ? p_FMOD_Studio_System_GetBus(h, pad, &o) : p_FMOD_Studio_System_GetEvent(h, pad, &o);
        if (r) logf_("%s %s: %d", op == RG_FM_GET_BUS ? "bus" : "event", pad, r);
        free(pad);
        put_h(ant, o);
        break;
    }
    case RG_FM_BANK_LOADSAMPLE: r = p_FMOD_Studio_Bank_LoadSampleData(get_h(rd)); break;
    case RG_FM_BANK_UNLOAD: r = p_FMOD_Studio_Bank_Unload(get_h(rd)); break;
    case RG_FM_BUS_STOPALL: h = get_h(rd); r = p_FMOD_Studio_Bus_StopAllEvents(h, get_i32(rd)); break;
    case RG_FM_BUS_SETMUTE: h = get_h(rd); r = p_FMOD_Studio_Bus_SetMute(h, get_i32(rd)); break;
    case RG_FM_BUS_SETVOLUME: h = get_h(rd); r = p_FMOD_Studio_Bus_SetVolume(h, get_f32(rd)); break;
    case RG_FM_BUS_GETCG:
        r = p_FMOD_Studio_Bus_GetChannelGroup(get_h(rd), &o);
        if (!r && o && !is_groep(o) && ngroepen < NGROEP) groepen[ngroepen++] = o;
        put_h(ant, o);
        break;
    case RG_FM_DESC_CREATEINST: r = p_FMOD_Studio_EventDescription_CreateInstance(get_h(rd), &o); put_h(ant, o); break;
    case RG_FM_DESC_GETINSTLIST: {
        h = get_h(rd);
        int cap = get_i32(rd), n = 0;
        if (cap < 0) cap = 0;
        if (cap > 4096) cap = 4096;
        H *arr = calloc((size_t)cap + 1, sizeof *arr);
        r = p_FMOD_Studio_EventDescription_GetInstanceList(h, arr, cap, &n);
        put_i32(ant, n);
        for (int i = 0; i < n && i < cap; i++) put_h(ant, arr[i]);
        free(arr);
        break;
    }
    case RG_FM_DESC_GETINSTCOUNT: { int n = 0; r = p_FMOD_Studio_EventDescription_GetInstanceCount(get_h(rd), &n); put_i32(ant, n); break; }
    case RG_FM_DESC_GETMAXDIST: { float d = 0; r = p_FMOD_Studio_EventDescription_GetMaximumDistance(get_h(rd), &d); put_f32(ant, d); break; }
    case RG_FM_DESC_GETMINDIST: { float d = 0; r = p_FMOD_Studio_EventDescription_GetMinimumDistance(get_h(rd), &d); put_f32(ant, d); break; }
    case RG_FM_DESC_GETPATH: {
        h = get_h(rd);
        int size = get_i32(rd), got = 0;
        if (size < 1) size = 1;
        if (size > 4096) size = 4096;
        char *p = calloc((size_t)size, 1);
        r = p_FMOD_Studio_EventDescription_GetPath(h, p, size, &got);
        put_i32(ant, got);
        put_blok(ant, p, (uint32_t)strlen(p));
        free(p);
        break;
    }
    case RG_FM_EI_SETCALLBACK: { h = get_h(rd); unsigned m = get_u32(rd); r = p_FMOD_Studio_EventInstance_SetCallback(h, m ? (void *)event_cb : NULL, m); break; }
    case RG_FM_EI_SET3D: { h = get_h(rd); unsigned char a[48]; get(rd, a, 48); r = p_FMOD_Studio_EventInstance_Set3DAttributes(h, a); break; }
    case RG_FM_EI_SETPARAM: { h = get_h(rd); char *n = get_str(rd); r = p_FMOD_Studio_EventInstance_SetParameterValue(h, n, get_f32(rd)); free(n); break; }
    case RG_FM_EI_STOP: h = get_h(rd); r = p_FMOD_Studio_EventInstance_Stop(h, get_i32(rd)); break;
    case RG_FM_EI_START: r = p_FMOD_Studio_EventInstance_Start(get_h(rd)); break;
    case RG_FM_EI_RELEASE: r = p_FMOD_Studio_EventInstance_Release(get_h(rd)); break;
    case RG_FM_EI_SETVOLUME: h = get_h(rd); r = p_FMOD_Studio_EventInstance_SetVolume(h, get_f32(rd)); break;
    case RG_FM_EI_GETDESC: r = p_FMOD_Studio_EventInstance_GetDescription(get_h(rd), &o); put_h(ant, o); break;
    case RG_FM_EI_GET3D: { unsigned char a[48] = {0}; r = p_FMOD_Studio_EventInstance_Get3DAttributes(get_h(rd), a); put(ant, a, 48); break; }
    case RG_FM_EI_ISVALID: put_i32(ant, p_FMOD_Studio_EventInstance_IsValid(get_h(rd))); break;

    case RG_FM_LL_CREATESOUND: {
        h = get_h(rd);
        unsigned mode = get_u32(rd), heeft = get_u32(rd);
        /* FMOD_CREATESOUNDEXINFO (1.10), alleen de getallen. */
        struct {
            int cbsize; unsigned length, fileoffset; int numchannels, defaultfrequency, format; unsigned decodebuffersize;
            int initialsubsound, numsubsounds; void *inclusionlist; int inclusionlistnum; void *pcmread, *pcmsetpos, *nonblock;
            const char *dlsname, *encryptionkey; int maxpolyphony; void *userdata; int suggestedsoundtype; void *fopen, *fclose,
            *fread, *fseek, *fasyncread, *fasynccancel; void *fileuserdata; int filebuffersize, channelorder; void *initialsoundgroup;
            unsigned initialseekposition; int initialseekpostype, ignoresetfilesystem; unsigned audioqueuepolicy, minmidigranularity;
            int nonblockthreadid; void *fsbguid;
        } ex;
        memset(&ex, 0, sizeof ex);
        char *key = NULL, *dls = NULL;
        if (heeft) {
            ex.cbsize = sizeof ex;
            ex.length = get_u32(rd); ex.fileoffset = get_u32(rd); ex.numchannels = get_i32(rd); ex.defaultfrequency = get_i32(rd);
            ex.format = get_i32(rd); ex.decodebuffersize = get_u32(rd); ex.initialsubsound = get_i32(rd); ex.numsubsounds = get_i32(rd);
            ex.suggestedsoundtype = get_i32(rd); ex.filebuffersize = get_i32(rd); ex.channelorder = get_i32(rd);
            ex.initialseekposition = get_u32(rd); ex.initialseekpostype = get_i32(rd);
            key = get_str(rd); dls = get_str(rd);
            if (key && *key) ex.encryptionkey = key;
            if (dls && *dls) ex.dlsname = dls;
        }
        char *naam;
        if (mode & (0x800u | 0x10000000u)) {
            uint32_t n = get_u32(rd);
            naam = malloc(n ? n : 1);
            get(rd, naam, n);
            mode = (mode & ~0x10000000u) | 0x800u; /* geheugen dat FMOD hier niet mag vasthouden: laten kopiëren */
        } else naam = get_str(rd);
        r = p_FMOD_System_CreateSound(h, naam, mode, heeft ? (void *)&ex : NULL, &o);
        logf_("createSound modus %#x: %d", mode, r);
        free(naam); free(key); free(dls);
        put_h(ant, o);
        break;
    }
    case RG_FM_LL_GET3DSETTINGS: { float a = 0, b = 0, c = 0; r = p_FMOD_System_Get3DSettings(get_h(rd), &a, &b, &c); put_f32(ant, a); put_f32(ant, b); put_f32(ant, c); break; }
    case RG_FM_LL_GETDRIVERINFO: {
        h = get_h(rd);
        int id = get_i32(rd), len = get_i32(rd);
        if (len < 1) len = 1;
        if (len > 1024) len = 1024;
        char *n = calloc((size_t)len, 1);
        unsigned char guid[16] = {0};
        int sr = 0, sm = 0, ch = 0;
        r = p_FMOD_System_GetDriverInfo(h, id, n, len, guid, &sr, &sm, &ch);
        put_blok(ant, n, (uint32_t)strlen(n)); put(ant, guid, 16); put_i32(ant, sr); put_i32(ant, sm); put_i32(ant, ch);
        logf_("apparaat %d: '%s' %d Hz %s (%d kanalen)", id, n, sr, naam_modus(sm), ch);
        free(n);
        break;
    }
    case RG_FM_LL_GETNUMDRIVERS: { int n = 0; r = p_FMOD_System_GetNumDrivers(get_h(rd), &n); put_i32(ant, n); break; }
    case RG_FM_LL_SET3DSETTINGS: { h = get_h(rd); float a = get_f32(rd), b = get_f32(rd), c = get_f32(rd); r = p_FMOD_System_Set3DSettings(h, a, b, c); break; }
    case RG_FM_LL_SETSWFORMAT: {
        h = get_h(rd);
        int sr = get_i32(rd), sm = get_i32(rd), raw = get_i32(rd);
        if (sr > 0) rate = sr;
        int m = speakers >= 0 ? speakers : sm;
        r = p_FMOD_System_SetSoftwareFormat(h, rate, m, speakers >= 0 ? 0 : raw);
        logf_("setSoftwareFormat: de game vraagt %s, het wordt %s (%d Hz): %d", naam_modus(sm), naam_modus(m), rate, r);
        break;
    }
    case RG_FM_LL_GETDRIVER: { int d = 0; r = p_FMOD_System_GetDriver(get_h(rd), &d); put_i32(ant, d); break; }
    case RG_FM_LL_PLAYSOUND: {
        h = get_h(rd);
        H s = get_h(rd), g = get_h(rd);
        int paused = get_i32(rd);
        r = p_FMOD_System_PlaySound(h, s, g, paused, &o);
        put_h(ant, o);
        break;
    }
    case RG_FM_LL_SETDRIVER: h = get_h(rd); r = p_FMOD_System_SetDriver(h, get_i32(rd)); break;
    case RG_FM_CH_SETPOSITION: { h = get_h(rd); unsigned p = get_u32(rd), u = get_u32(rd); r = p_FMOD_Channel_SetPosition(h, p, u); break; }
    case RG_FM_CH_SETLOOPCOUNT: h = get_h(rd); r = p_FMOD_Channel_SetLoopCount(h, get_i32(rd)); break;
    case RG_FM_CC_SETCALLBACK: h = get_h(rd); r = CC(SetCallback, h, get_i32(rd) ? (void *)kanaal_cb : NULL); break;
    case RG_FM_CC_SET3D: {
        h = get_h(rd);
        unsigned w = get_u32(rd);
        float v[3][3]; get(rd, v, sizeof v);
        r = CC(Set3DAttributes, h, (w & 1) ? v[0] : NULL, (w & 2) ? v[1] : NULL, (w & 4) ? v[2] : NULL);
        break;
    }
    case RG_FM_CC_STOP: h = get_h(rd); r = CC0(Stop, h); break;
    case RG_FM_CC_GETDSP: { h = get_h(rd); int i = get_i32(rd); r = CC(GetDSP, h, i, &o); put_h(ant, o); break; }
    case RG_FM_CC_SETPAN: { h = get_h(rd); float v = get_f32(rd); r = CC(SetPan, h, v); break; }
    case RG_FM_CC_SETMODE: { h = get_h(rd); unsigned m = get_u32(rd); r = CC(SetMode, h, m); break; }
    case RG_FM_CC_SETPAUSED: { h = get_h(rd); int p = get_i32(rd); r = CC(SetPaused, h, p); break; }
    case RG_FM_CC_SETVOLUME: { h = get_h(rd); float v = get_f32(rd); r = CC(SetVolume, h, v); break; }
    case RG_FM_DSP_GETMETERING: { unsigned char in[264] = {0}, uit[264] = {0}; r = p_FMOD_DSP_GetMeteringInfo(get_h(rd), in, uit); put(ant, in, 264); put(ant, uit, 264); break; }
    case RG_FM_DSP_SETMETERING: { h = get_h(rd); int i = get_i32(rd), u = get_i32(rd); r = p_FMOD_DSP_SetMeteringEnabled(h, i, u); break; }
    case RG_FM_SOUND_RELEASE: r = p_FMOD_Sound_Release(get_h(rd)); break;
    default: fprintf(stderr, "rg-fmod-host: onbekende opdracht %#x\n", op); r = 28; break;
    }
    return r;
}

int main(int argc, char **argv) {
    hin = GetStdHandle(STD_INPUT_HANDLE);
    huit = GetStdHandle(STD_OUTPUT_HANDLE);
    InitializeCriticalSection(&cb_cs);
    const char *map = argc > 1 ? argv[1] : ".";
    if (argc > 2) {
        if (!strcmp(argv[2], "stereo")) speakers = 3;
        else if (!strcmp(argv[2], "7.1")) speakers = 7;
        else if (!strcmp(argv[2], "game")) speakers = -1;
        else if (!strcmp(argv[2], "5.1-zonder-upmix")) speakers = RG_FM_SPEAKERMODE_5POINT1;
        else speakers = RG_FM_SPEAKERMODE_5POINT1, upmix_aan = 1;
    } else upmix_aan = 1;
    log_on = argc > 3 && !strcmp(argv[3], "log");
    int ok = laad_fmod(map);
    int32_t hallo[3] = {ok ? 0 : 18 /* FMOD_ERR_FILE_NOTFOUND */, 4, (int32_t)RG_FM_MAGIC};
    if (schrijf(hallo, sizeof hallo)) return 1;
    if (!ok) return 1;
    fprintf(stderr, "rg-fmod-host: FMOD Studio 1.10 geladen uit %s, luidsprekers %s\n", map, speakers >= 0 ? naam_modus(speakers) : "zoals de game vraagt");
    unsigned char *payload = NULL;
    size_t cap = 0;
    buf_t ant = {0};
    unsigned long n_ops = 0;
    for (;;) {
        uint32_t kop[2];
        if (lees(kop, 8)) break;
        if (kop[1] > cap) { cap = kop[1] * 2; payload = realloc(payload, cap); if (!payload) return 1; }
        if (kop[1] && lees(payload, kop[1])) break;
        rd_t rd = {payload, kop[1], 0};
        ant.n = 0;
        R r = voer_uit(kop[0], &rd, &ant);
        n_ops++;
        if (r && !(kop[0] == RG_FM_GET_EVENT || kop[0] == RG_FM_EI_ISVALID)) logf_("opdracht %#x: FMOD %d", kop[0], r);
        if (kop[0] & RG_FM_ANTWOORD) {
            int32_t akop[2] = {r, (int32_t)ant.n};
            if (schrijf(akop, 8) || (ant.n && schrijf(ant.b, (DWORD)ant.n))) break;
        }
    }
    fprintf(stderr, "rg-fmod-host: de game is weg na %lu opdrachten; klaar\n", n_ops);
    return 0;
}
