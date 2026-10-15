/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fmod_stub.c -- "stille FMOD" voor Raw Thrills-games die FMOD Studio 1.10 vragen (libfmod.so.10, libfmodstudio.so.10) terwijl
 * die propriëtaire bibliotheken er niet zijn. Dit is GEEN FMOD-implementatie en bevat geen FMOD-code: alleen de 60 namen die
 * Halo: Fireteam Raven opvraagt (symbols.txt; de game zoekt ze zelf op met dlsym), elk met de handtekening van de FMOD Studio
 * 1.10-API, en elk antwoordt FMOD_OK met lege maar geldige waarden. Er klinkt niets.
 *
 * Waarom niet langer een foutcode: de vorige stub gaf overal 52 ("initialisatie mislukt"). De attract liep daarmee, maar START
 * begon geen spel: het laden van de geluidsbank van het level faalde ("Failed to load FMOD sound bank: level1 (ec 52)" in
 * pmuser/gamelog.txt, op het moment van START, 2026-10-15) en de game viel terug naar de attract.
 *
 * Objecten: elke "handle" wijst naar een eigen klein blok (userdata, beschrijving, vrijgegeven-vlag). Event-instanties worden bij
 * release niet vrijgegeven maar gemarkeerd, zodat isValid() en getUserData() op een oude handle niets ongeldigs lezen.
 * Callbacks worden nooit aangeroepen.
 *
 * BOUWEN: scripts/build-fmod-stub.sh (zet libfmod.so.10 en libfmodstudio.so.10 uit dezelfde bron in een map).
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define FMOD_OK 0
#define EXPORT __attribute__((visibility("default")))

typedef struct obj {
    unsigned magic;
    void *userdata;
    struct obj *desc;
    bool released;
} obj;

static obj g_studio = {0x5f5f, 0, 0, false}, g_lowlevel = {0x5f5f, 0, 0, false}, g_bank = {0x5f5f, 0, 0, false},
           g_bus = {0x5f5f, 0, 0, false}, g_group = {0x5f5f, 0, 0, false}, g_desc = {0x5f5f, 0, 0, false},
           g_sound = {0x5f5f, 0, 0, false}, g_channel = {0x5f5f, 0, 0, false}, g_dsp = {0x5f5f, 0, 0, false};

static obj *nieuw(obj *desc) {
    obj *o = calloc(1, sizeof *o);
    if (!o) return &g_channel;
    o->magic = 0x5f5f;
    o->desc = desc;
    return o;
}

#define OUT(p, v) do { if (p) *(p) = (v); } while (0)

/* ------------------------------------------------------------------ libfmod (laag niveau) */
EXPORT int FMOD_Debug_Initialize(unsigned flags, int mode, void *cb, const char *file) { (void)flags, (void)mode, (void)cb, (void)file; return FMOD_OK; }
EXPORT int FMOD_Memory_Initialize(void *pool, int len, void *a, void *r, void *f, unsigned type) {
    (void)pool, (void)len, (void)a, (void)r, (void)f, (void)type;
    return FMOD_OK;
}

EXPORT int _ZN4FMOD14ChannelControl11getUserDataEPPv(obj *self, void **ud) { OUT(ud, self ? self->userdata : NULL); return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl11setCallbackEPF11FMOD_RESULTP19FMOD_CHANNELCONTROL24FMOD_CHANNELCONTROL_TYPE33FMOD_CHANNELCONTROL_CALLBACK_TYPEPvS6_E(obj *self, void *cb) { (void)self, (void)cb; return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl11setUserDataEPv(obj *self, void *ud) { if (self) self->userdata = ud; return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl15set3DAttributesEPK11FMOD_VECTORS3_S3_(obj *self, const void *p, const void *v, const void *a) { (void)self, (void)p, (void)v, (void)a; return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl4stopEv(obj *self) { (void)self; return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl6getDSPEiPPNS_3DSPE(obj *self, int i, obj **dsp) { (void)self, (void)i; OUT(dsp, &g_dsp); return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl6setPanEf(obj *self, float v) { (void)self, (void)v; return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl7setModeEj(obj *self, unsigned m) { (void)self, (void)m; return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl9setPausedEb(obj *self, bool p) { (void)self, (void)p; return FMOD_OK; }
EXPORT int _ZN4FMOD14ChannelControl9setVolumeEf(obj *self, float v) { (void)self, (void)v; return FMOD_OK; }

/* FMOD_DSP_METERING_INFO: int numsamples; float peaklevel[32]; float rmslevel[32]; short numchannels (264 bytes met uitlijning). */
EXPORT int _ZN4FMOD3DSP15getMeteringInfoEP22FMOD_DSP_METERING_INFOS2_(obj *self, void *in, void *out) {
    (void)self;
    if (in) memset(in, 0, 264);
    if (out) memset(out, 0, 264);
    return FMOD_OK;
}
EXPORT int _ZN4FMOD3DSP18setMeteringEnabledEbb(obj *self, bool i, bool o) { (void)self, (void)i, (void)o; return FMOD_OK; }
EXPORT int _ZN4FMOD5Sound7releaseEv(obj *self) { (void)self; return FMOD_OK; }

EXPORT int _ZN4FMOD6System11createSoundEPKcjP22FMOD_CREATESOUNDEXINFOPPNS_5SoundE(obj *self, const char *n, unsigned m, void *ex, obj **s) {
    (void)self, (void)n, (void)m, (void)ex;
    OUT(s, &g_sound);
    return FMOD_OK;
}
EXPORT int _ZN4FMOD6System13get3DSettingsEPfS1_S1_(obj *self, float *doppler, float *dist, float *rolloff) {
    (void)self;
    OUT(doppler, 1.0f);
    OUT(dist, 1.0f);
    OUT(rolloff, 1.0f);
    return FMOD_OK;
}
EXPORT int _ZN4FMOD6System13getDriverInfoEiPciP9FMOD_GUIDPiP16FMOD_SPEAKERMODES4_(obj *self, int id, char *name, int len, void *guid, int *rate,
                                                                                    int *mode, int *channels) {
    (void)self, (void)id;
    if (name && len > 0) {
        strncpy(name, "RavenGun (stil)", (size_t)len - 1);
        name[len - 1] = 0;
    }
    if (guid) memset(guid, 0, 16);
    OUT(rate, 48000);
    OUT(mode, 3); /* FMOD_SPEAKERMODE_STEREO */
    OUT(channels, 2);
    return FMOD_OK;
}
EXPORT int _ZN4FMOD6System13getNumDriversEPi(obj *self, int *n) { (void)self; OUT(n, 1); return FMOD_OK; }
EXPORT int _ZN4FMOD6System13set3DSettingsEfff(obj *self, float a, float b, float c) { (void)self, (void)a, (void)b, (void)c; return FMOD_OK; }
EXPORT int _ZN4FMOD6System17setSoftwareFormatEi16FMOD_SPEAKERMODEi(obj *self, int rate, int mode, int raw) { (void)self, (void)rate, (void)mode, (void)raw; return FMOD_OK; }
EXPORT int _ZN4FMOD6System9getDriverEPi(obj *self, int *d) { (void)self; OUT(d, 0); return FMOD_OK; }
EXPORT int _ZN4FMOD6System9playSoundEPNS_5SoundEPNS_12ChannelGroupEbPPNS_7ChannelE(obj *self, obj *s, obj *g, bool paused, obj **ch) {
    (void)self, (void)s, (void)g, (void)paused;
    OUT(ch, nieuw(NULL));
    return FMOD_OK;
}
EXPORT int _ZN4FMOD6System9setDriverEi(obj *self, int d) { (void)self, (void)d; return FMOD_OK; }
EXPORT int _ZN4FMOD7Channel11setPositionEjj(obj *self, unsigned p, unsigned u) { (void)self, (void)p, (void)u; return FMOD_OK; }
EXPORT int _ZN4FMOD7Channel12setLoopCountEi(obj *self, int n) { (void)self, (void)n; return FMOD_OK; }

/* ------------------------------------------------------------------ libfmodstudio */
EXPORT int _ZN4FMOD6Studio6System6createEPPS1_j(obj **sys, unsigned version) { (void)version; OUT(sys, &g_studio); return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio6System10initializeEijjPv(obj *self, int maxch, unsigned sf, unsigned f, void *extra) {
    (void)self, (void)maxch, (void)sf, (void)f, (void)extra;
    return FMOD_OK;
}
EXPORT int _ZN4FMOD6Studio6System12loadBankFileEPKcjPPNS0_4BankE(obj *self, const char *file, unsigned flags, obj **bank) {
    (void)self, (void)file, (void)flags;
    OUT(bank, &g_bank);
    return FMOD_OK;
}
EXPORT int _ZN4FMOD6Studio6System13flushCommandsEv(obj *self) { (void)self; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio6System21setListenerAttributesEiPK18FMOD_3D_ATTRIBUTES(obj *self, int l, const void *a) { (void)self, (void)l, (void)a; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio6System6updateEv(obj *self) { (void)self; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio6System7releaseEv(obj *self) { (void)self; return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio6System17getLowLevelSystemEPPNS_6SystemE(const obj *self, obj **ll) { (void)self; OUT(ll, &g_lowlevel); return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio6System6getBusEPKcPPNS0_3BusE(const obj *self, const char *path, obj **bus) { (void)self, (void)path; OUT(bus, &g_bus); return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio6System8getEventEPKcPPNS0_16EventDescriptionE(const obj *self, const char *path, obj **d) {
    (void)self, (void)path;
    OUT(d, &g_desc);
    return FMOD_OK;
}

EXPORT int _ZN4FMOD6Studio4Bank14loadSampleDataEv(obj *self) { (void)self; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio4Bank6unloadEv(obj *self) { (void)self; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio3Bus13stopAllEventsE21FMOD_STUDIO_STOP_MODE(obj *self, int m) { (void)self, (void)m; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio3Bus7setMuteEb(obj *self, bool m) { (void)self, (void)m; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio3Bus9setVolumeEf(obj *self, float v) { (void)self, (void)v; return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio3Bus15getChannelGroupEPPNS_12ChannelGroupE(const obj *self, obj **g) { (void)self; OUT(g, &g_group); return FMOD_OK; }

EXPORT int _ZNK4FMOD6Studio16EventDescription14createInstanceEPPNS0_13EventInstanceE(const obj *self, obj **inst) {
    OUT(inst, nieuw((obj *)self));
    return FMOD_OK;
}
EXPORT int _ZNK4FMOD6Studio16EventDescription15getInstanceListEPPNS0_13EventInstanceEiPi(const obj *self, obj **arr, int cap, int *count) {
    (void)self, (void)arr, (void)cap;
    OUT(count, 0);
    return FMOD_OK;
}
EXPORT int _ZNK4FMOD6Studio16EventDescription16getInstanceCountEPi(const obj *self, int *count) { (void)self; OUT(count, 0); return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio16EventDescription18getMaximumDistanceEPf(const obj *self, float *d) { (void)self; OUT(d, 100.0f); return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio16EventDescription18getMinimumDistanceEPf(const obj *self, float *d) { (void)self; OUT(d, 1.0f); return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio16EventDescription7getPathEPciPi(const obj *self, char *path, int size, int *got) {
    (void)self;
    if (path && size > 0) path[0] = 0;
    OUT(got, 1);
    return FMOD_OK;
}

EXPORT int _ZN4FMOD6Studio13EventInstance11setCallbackEPF11FMOD_RESULTjP25FMOD_STUDIO_EVENTINSTANCEPvEj(obj *self, void *cb, unsigned mask) {
    (void)self, (void)cb, (void)mask;
    return FMOD_OK;
}
EXPORT int _ZN4FMOD6Studio13EventInstance11setUserDataEPv(obj *self, void *ud) { if (self) self->userdata = ud; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio13EventInstance15set3DAttributesEPK18FMOD_3D_ATTRIBUTES(obj *self, const void *a) { (void)self, (void)a; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio13EventInstance17setParameterValueEPKcf(obj *self, const char *n, float v) { (void)self, (void)n, (void)v; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio13EventInstance4stopE21FMOD_STUDIO_STOP_MODE(obj *self, int m) { (void)self, (void)m; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio13EventInstance5startEv(obj *self) { (void)self; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio13EventInstance7releaseEv(obj *self) { if (self) self->released = true; return FMOD_OK; }
EXPORT int _ZN4FMOD6Studio13EventInstance9setVolumeEf(obj *self, float v) { (void)self, (void)v; return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio13EventInstance11getUserDataEPPv(const obj *self, void **ud) { OUT(ud, self ? self->userdata : NULL); return FMOD_OK; }
EXPORT int _ZNK4FMOD6Studio13EventInstance14getDescriptionEPPNS0_16EventDescriptionE(const obj *self, obj **d) {
    OUT(d, self && self->desc ? self->desc : &g_desc);
    return FMOD_OK;
}
/* FMOD_3D_ATTRIBUTES: vier FMOD_VECTOR's (positie, snelheid, voor, boven) = 48 bytes. */
EXPORT int _ZNK4FMOD6Studio13EventInstance15get3DAttributesEP18FMOD_3D_ATTRIBUTES(const obj *self, void *a) {
    (void)self;
    if (a) memset(a, 0, 48);
    return FMOD_OK;
}
EXPORT bool _ZNK4FMOD6Studio13EventInstance7isValidEv(const obj *self) { return self && self->magic == 0x5f5f && !self->released; }
