/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fmod_bridge_proto.h -- het protocol tussen de FMOD-wrapper in het spel (fmod_bridge.c, Linux: libfmod.so.10 en
 * libfmodstudio.so.10) en het hulpprogramma onder Wine (rg_fmod_host.c, Windows: laadt de echte FMOD Studio 1.10.2-DLL's van
 * de gebruiker). Eigen code; er zit geen FMOD-code in, alleen de namen en vormen van de openbare API.
 *
 * Over een pijp (stdin/stdout van het hulpprogramma). Een verzoek: {uint32 op; uint32 len} en len bytes. Een antwoord (alleen
 * voor de opdrachten met RG_FM_ANTWOORD): {int32 res; uint32 len} en len bytes. Velden achter elkaar, little-endian, zonder
 * uitlijning: u32/i32/f32 = 4 bytes, u64 (een handle) = 8 bytes, tekst = u32 lengte + bytes (zonder NUL).
 * Eén schrijver tegelijk (de wrapper houdt een slot vast van verzoek tot antwoord), dus antwoorden komen in volgorde.
 */
#ifndef RG_FMOD_BRIDGE_PROTO_H
#define RG_FMOD_BRIDGE_PROTO_H

#define RG_FM_MAGIC 0x4d464752u /* "RGFM": het eerste antwoord van het hulpprogramma (hallo) */
#define RG_FM_ANTWOORD 0x100u   /* bit in op: de opdracht wacht op een antwoord */

enum {
    /* Studio */
    RG_FM_STUDIO_CREATE = 1 | RG_FM_ANTWOORD,      /* -> u64 studio, u64 laag niveau */
    RG_FM_STUDIO_INITIALIZE = 2 | RG_FM_ANTWOORD,  /* i32 maxch, u32 studioflags, u32 flags */
    RG_FM_LOADBANK = 3 | RG_FM_ANTWOORD,           /* tekst pad, u32 flags -> u64 bank */
    RG_FM_FLUSH = 4 | RG_FM_ANTWOORD,
    RG_FM_SET_LISTENER = 5,                        /* i32 luisteraar, 48 bytes attributen */
    RG_FM_UPDATE = 6 | RG_FM_ANTWOORD,             /* -> callbacks (zie onder) */
    RG_FM_STUDIO_RELEASE = 7 | RG_FM_ANTWOORD,
    RG_FM_GET_BUS = 8 | RG_FM_ANTWOORD,            /* tekst -> u64 */
    RG_FM_GET_EVENT = 9 | RG_FM_ANTWOORD,          /* tekst -> u64 */
    RG_FM_BANK_LOADSAMPLE = 10 | RG_FM_ANTWOORD,   /* u64 bank */
    RG_FM_BANK_UNLOAD = 11 | RG_FM_ANTWOORD,       /* u64 bank */
    RG_FM_BUS_STOPALL = 12,                        /* u64 bus, i32 modus */
    RG_FM_BUS_SETMUTE = 13,                        /* u64 bus, i32 aan */
    RG_FM_BUS_SETVOLUME = 14,                      /* u64 bus, f32 */
    RG_FM_BUS_GETCG = 15 | RG_FM_ANTWOORD,         /* u64 bus -> u64 kanaalgroep */
    RG_FM_DESC_CREATEINST = 16 | RG_FM_ANTWOORD,   /* u64 desc -> u64 instantie */
    RG_FM_DESC_GETINSTLIST = 17 | RG_FM_ANTWOORD,  /* u64 desc, i32 cap -> i32 n, n x u64 */
    RG_FM_DESC_GETINSTCOUNT = 18 | RG_FM_ANTWOORD, /* u64 desc -> i32 */
    RG_FM_DESC_GETMAXDIST = 19 | RG_FM_ANTWOORD,   /* u64 desc -> f32 */
    RG_FM_DESC_GETMINDIST = 20 | RG_FM_ANTWOORD,   /* u64 desc -> f32 */
    RG_FM_DESC_GETPATH = 21 | RG_FM_ANTWOORD,      /* u64 desc, i32 size -> i32 retrieved, tekst */
    RG_FM_EI_SETCALLBACK = 22,                     /* u64 inst, u32 masker (0 = uit) */
    RG_FM_EI_SET3D = 23,                           /* u64 inst, 48 bytes */
    RG_FM_EI_SETPARAM = 24,                        /* u64 inst, tekst naam, f32 */
    RG_FM_EI_STOP = 25,                            /* u64 inst, i32 modus */
    RG_FM_EI_START = 26,                           /* u64 inst */
    RG_FM_EI_RELEASE = 27,                         /* u64 inst */
    RG_FM_EI_SETVOLUME = 28,                       /* u64 inst, f32 */
    RG_FM_EI_GETDESC = 29 | RG_FM_ANTWOORD,        /* u64 inst -> u64 desc */
    RG_FM_EI_GET3D = 30 | RG_FM_ANTWOORD,          /* u64 inst -> 48 bytes */
    RG_FM_EI_ISVALID = 31 | RG_FM_ANTWOORD,        /* u64 inst -> i32 */
    /* laag niveau */
    RG_FM_LL_CREATESOUND = 40 | RG_FM_ANTWOORD,    /* u64 sys, u32 modus, u32 heeft_exinfo, [exinfo-velden], tekst naam of bytes -> u64 */
    RG_FM_LL_GET3DSETTINGS = 41 | RG_FM_ANTWOORD,  /* u64 sys -> 3 x f32 */
    RG_FM_LL_GETDRIVERINFO = 42 | RG_FM_ANTWOORD,  /* u64 sys, i32 id, i32 naamlengte -> tekst, 16 bytes guid, i32 rate, i32 modus, i32 kanalen */
    RG_FM_LL_GETNUMDRIVERS = 43 | RG_FM_ANTWOORD,  /* u64 sys -> i32 */
    RG_FM_LL_SET3DSETTINGS = 44,                   /* u64 sys, 3 x f32 */
    RG_FM_LL_SETSWFORMAT = 45 | RG_FM_ANTWOORD,    /* u64 sys, i32 rate, i32 modus, i32 raw */
    RG_FM_LL_GETDRIVER = 46 | RG_FM_ANTWOORD,      /* u64 sys -> i32 */
    RG_FM_LL_PLAYSOUND = 47 | RG_FM_ANTWOORD,      /* u64 sys, u64 sound, u64 groep, i32 gepauzeerd -> u64 kanaal */
    RG_FM_LL_SETDRIVER = 48 | RG_FM_ANTWOORD,      /* u64 sys, i32 */
    RG_FM_CH_SETPOSITION = 49,                     /* u64 kanaal, u32 positie, u32 eenheid */
    RG_FM_CH_SETLOOPCOUNT = 50,                    /* u64 kanaal, i32 */
    RG_FM_CC_SETCALLBACK = 51,                     /* u64 cc, i32 aan */
    RG_FM_CC_SET3D = 52,                           /* u64 cc, u32 welke (bit 0 pos, 1 vel, 2 alt), 3 x 12 bytes */
    RG_FM_CC_STOP = 53,                            /* u64 cc */
    RG_FM_CC_GETDSP = 54 | RG_FM_ANTWOORD,         /* u64 cc, i32 index -> u64 */
    RG_FM_CC_SETPAN = 55,                          /* u64 cc, f32 */
    RG_FM_CC_SETMODE = 56,                         /* u64 cc, u32 */
    RG_FM_CC_SETPAUSED = 57,                       /* u64 cc, i32 */
    RG_FM_CC_SETVOLUME = 58,                       /* u64 cc, f32 */
    RG_FM_DSP_GETMETERING = 59 | RG_FM_ANTWOORD,   /* u64 dsp -> 264 bytes in, 264 bytes uit */
    RG_FM_DSP_SETMETERING = 60,                    /* u64 dsp, i32 in, i32 uit */
    RG_FM_SOUND_RELEASE = 61,                      /* u64 sound */
};

/* Callbacks, in het antwoord op RG_FM_UPDATE: i32 aantal, en per callback u32 soort (1 = event, 2 = kanaal), u64 handle,
   u32 type, u64 a, u64 b, blok (u32 lengte + bytes). Event: type = FMOD_STUDIO_EVENT_CALLBACK_TYPE; bij een tijdlijnmarker
   a = positie en blok = de naam, bij een beat blok = de 24 bytes van FMOD_STUDIO_TIMELINE_BEAT_PROPERTIES, bij SOUND_PLAYED/
   STOPPED a = de sound-handle. Kanaal: type = callbacktype, a = controltype, b = commanddata1. */
#define RG_FM_CB_EVENT 1u
#define RG_FM_CB_KANAAL 2u

#define RG_FM_SPEAKERMODE_5POINT1 6

#endif
