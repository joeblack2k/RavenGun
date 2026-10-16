/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fmod_upmix.h -- de 5.1-upmix van de FMOD-wrapper, gedeeld door rg_fmod_host.c (Windows, FMOD onder Wine) en fmod_bridge.c
 * (Linux, de native FMOD uit de drop-in van de gebruiker). Eigen code tegen de plugin-API van FMOD 1.10 (fmod_dsp.h).
 */
#ifndef RG_FMOD_UPMIX_H
#define RG_FMOD_UPMIX_H
#include <math.h>
#include <string.h>

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
static int rg_upmix_process(void *state, unsigned length, const dsp_buffers *in, dsp_buffers *out, int idle, int op) {
    (void)state;
    if (op == DSP_QUERY) {
        if (out && out->numbuffers > 0) { out->buffernumchannels[0] = 6; out->bufferchannelmask[0] = 0; out->speakermode = 6 /* FMOD_SPEAKERMODE_5POINT1 */; }
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
/* Zet de upmix op de master-kanaalgroep van een FMOD-systeem (ll = FMOD::System). De vier functies zijn die van de C-API van
   FMOD (System_CreateDSP, System_GetMasterChannelGroup, ChannelGroup_AddDSP, DSP_SetActive), opgezocht door de aanroeper.
   Geeft 0 als de DSP erop staat; fouten[3] krijgt de drie FMOD-resultaten (voor het logboek). */
static int rg_upmix_installeer(void *ll, int sr, int (*createdsp)(void *, const void *, void **), int (*getmaster)(void *, void **),
                               int (*adddsp)(void *, int, void *), int (*setactive)(void *, int), int fouten[3]) {
    static dsp_beschrijving d;
    memset(&d, 0, sizeof d);
    d.pluginsdkversion = 110; /* FMOD_PLUGIN_SDK_VERSION van 1.10 */
    strcpy(d.name, "RavenGun 5.1-upmix");
    d.version = 1; d.numinputbuffers = 1; d.numoutputbuffers = 1;
    d.process = (void *)rg_upmix_process;
    bq_laag(&lfe_f, 120, sr); bq_laag(&achter_l_f, 7000, sr); bq_laag(&achter_r_f, 7000, sr);
    void *dsp = NULL, *master = NULL;
    int a = createdsp(ll, &d, &dsp);
    int b = a ? -1 : getmaster(ll, &master);
    int c = (a || b) ? -1 : adddsp(master, -1 /* FMOD_CHANNELCONTROL_DSP_HEAD */, dsp);
    if (!a && !b && !c) setactive(dsp, 1);
    fouten[0] = a, fouten[1] = b, fouten[2] = c;
    return (a || b || c) ? -1 : 0;
}

#endif
