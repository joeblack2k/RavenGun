/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_usbio_logic.h - de rekenregel van onze Usbioextreme.dll (Far Cry: Paradise Lost): het bordbeeld van de daemon
 * (tp-shm, indeling FarCryPipe, 64 bytes) naar het record van 0x34 bytes dat de game na CUSBIO_Update leest.
 *
 * Alleen logica, geen Windows: de DLL (rg_usbio.c) en de eenheidstest (tests/farcry_usbio_unit.c) nemen dit bestand over.
 *
 * HERKOMST (alles gemeten aan Farcry_R.exe en de originele Usbioextreme.dll, alleen gelezen; docs/research/losse/FarCryParadiseLost.md):
 *  - De game bezit een object CUSBIO op +0xac van zijn invoerklasse: +0 = aantal borden (int), +4 = wijzer naar 0x34 bytes per bord.
 *    CUSBIO_Init(this) vult die twee velden; CUSBIO_Update(this, i) schrijft record i; de game leest daarna het record.
 *  - De game leest (functie op 0x7af480) uit record 0 alleen: byte 6 t/m 9 (de assen), byte 0xe en 0xf (de knoppen) en,
 *    via de teller-lezer op 0x7ac7e0, de dwords op 0x10 en 0x14 (muntentellers van de twee munttellers/-schachten).
 *  - Assen: byte 7 = X speler 1, byte 6 = Y speler 1, byte 9 = X speler 2, byte 8 = Y speler 2 (de game maakt er
 *    (ruw - min) / (max - min) * 2 - 1 van; min en max komen uit Arcade_Calibration.txt, zie de drop-in). 0 + 0 betekent
 *    "geen volgsignaal" (vizier weg), daarom komt hier nooit 0 uit.
 *  - Knoppen: de tabel op 0x7ac66c (acht gevallen) en de directe tests op 0x7af6xx geven: byte 0xf bit 2 = trekker speler 1,
 *    byte 0xe bit 0 = tweede knop speler 1 (granaat), byte 0xf bit 3 = trekker speler 2, byte 0xe bit 1 = tweede knop speler 2.
 *    De vier overige bits (0xf bit 0, 0xf bit 1, 0xe bit 2, 0xe bit 3) zijn de bedienknoppen aan de kast; WELKE van de vier
 *    test/selecteren/omhoog/omlaag is, is NIET gemeten (de lezer van die bits is niet gevonden). Standaard: test, selecteren,
 *    omhoog, omlaag in die volgorde; omzetten kan met RG_USBIO_OPS (vier letters uit T S U D).
 */
#ifndef RG_USBIO_LOGIC_H
#define RG_USBIO_LOGIC_H

#define RG_REC_SIZE      0x34
#define RG_REC_Y1        6
#define RG_REC_X1        7
#define RG_REC_Y2        8
#define RG_REC_X2        9
#define RG_REC_BTN_HI    0xe   /* bits van het hoge nibble van byte 11 van het bordrapport */
#define RG_REC_BTN_LO    0xf   /* bits van het lage nibble */
#define RG_REC_COIN1     0x10  /* dword: teller munt 1 */
#define RG_REC_COIN2     0x14  /* dword: teller munt 2 */
#define RG_REC_TICK      0x1c  /* dword: GetTickCount van de laatste update (het origineel zet dit ook) */
#define RG_REC_MODE      0x28  /* dword: toestand van het bord (origineel: 1 = niets, >= 2 = bord gelezen) */

/* Bits van Control (offset 8 van het bordbeeld) volgens TeknoParrotUI FarCryPipe.cs en packaging/tp-shm-profiles.json. */
#define RG_CTL_P2_GRENADE 0x00008u
#define RG_CTL_P1_GRENADE 0x00020u
#define RG_CTL_TEST       0x00040u
#define RG_CTL_SERVICE    0x00080u   /* "select" */
#define RG_CTL_P1_START   0x00100u
#define RG_CTL_P1_TRIGGER 0x00200u
#define RG_CTL_COIN1      0x00400u
#define RG_CTL_P2_START   0x02000u
#define RG_CTL_P2_TRIGGER 0x04000u
#define RG_CTL_UP         0x08000u
#define RG_CTL_DOWN       0x10000u

typedef struct {
    unsigned prev_ctl;       /* vorige Control, voor de muntrand */
    unsigned coin1, coin2;   /* tellers, 16 bit zoals het bord (b13:b14) */
    unsigned char invert_x, invert_y;
    unsigned ops[4];         /* Control-bit per bedienknop: record 0xf bit 0, 0xf bit 1, 0xe bit 2, 0xe bit 3 */
} rg_usbio_state;

static void rg_usbio_state_init(rg_usbio_state *s)
{
    unsigned i;
    unsigned char *p = (unsigned char *)s;
    for (i = 0; i < sizeof *s; i++) p[i] = 0;
    s->ops[0] = RG_CTL_TEST;
    s->ops[1] = RG_CTL_SERVICE;
    s->ops[2] = RG_CTL_UP;
    s->ops[3] = RG_CTL_DOWN;
}

/* "TSUD" -> volgorde van de vier bedienknoppen; geeft 0 bij een ongeldige reeks (dan blijft de standaard staan). */
static int rg_usbio_set_ops(rg_usbio_state *s, const char *letters)
{
    unsigned want[4];
    unsigned seen = 0;
    int i;
    if (!letters) return 0;
    for (i = 0; i < 4; i++) {
        unsigned bit;
        switch (letters[i]) {
        case 'T': case 't': bit = RG_CTL_TEST; break;
        case 'S': case 's': bit = RG_CTL_SERVICE; break;
        case 'U': case 'u': bit = RG_CTL_UP; break;
        case 'D': case 'd': bit = RG_CTL_DOWN; break;
        default: return 0;
        }
        if (seen & bit) return 0;
        seen |= bit;
        want[i] = bit;
    }
    if (letters[4] != 0) return 0;
    for (i = 0; i < 4; i++) s->ops[i] = want[i];
    return 1;
}

/* Een byte van de tp-shm-as (0 = links/boven, 255 = rechts/onder) naar de ruwe as van het bord. De game neemt 0 + 0 voor "geen
 * volgsignaal", en de kalibratie in de drop-in (Arcade_Calibration.txt) loopt van 1 tot 255; dus 1..255. */
static unsigned char rg_usbio_axis(unsigned char tp, int invert)
{
    unsigned v = tp;
    if (invert) v = 255u - v;
    return (unsigned char)(1u + (v * 254u + 127u) / 255u);
}

static unsigned rg_usbio_ctl(const unsigned char *shm)
{
    return (unsigned)shm[8] | ((unsigned)shm[9] << 8) | ((unsigned)shm[10] << 16) | ((unsigned)shm[11] << 24);
}

static void rg_usbio_put32(unsigned char *rec, unsigned off, unsigned v)
{
    rec[off] = (unsigned char)v;
    rec[off + 1] = (unsigned char)(v >> 8);
    rec[off + 2] = (unsigned char)(v >> 16);
    rec[off + 3] = (unsigned char)(v >> 24);
}

/* Vult het record voor bord 0 uit het bordbeeld. shm == 0: bordbeeld niet beschikbaar (alles los, vizier in het midden). */
static void rg_usbio_fill(rg_usbio_state *s, const unsigned char *shm, unsigned char *rec, unsigned tick)
{
    unsigned ctl = 0;
    unsigned char hi = 0, lo = 0;
    int i;
    for (i = 0; i < RG_REC_SIZE; i++) rec[i] = 0;
    if (shm) {
        ctl = rg_usbio_ctl(shm);
        rec[RG_REC_X1] = rg_usbio_axis(shm[12], s->invert_x);
        rec[RG_REC_Y1] = rg_usbio_axis(shm[16], s->invert_y);
        rec[RG_REC_X2] = rg_usbio_axis(shm[20], s->invert_x);
        rec[RG_REC_Y2] = rg_usbio_axis(shm[24], s->invert_y);
    } else {
        rec[RG_REC_X1] = rec[RG_REC_Y1] = rec[RG_REC_X2] = rec[RG_REC_Y2] = 128;
    }
    if (ctl & RG_CTL_P1_TRIGGER) lo |= 1u << 2;
    if (ctl & RG_CTL_P2_TRIGGER) lo |= 1u << 3;
    if (ctl & RG_CTL_P1_GRENADE) hi |= 1u << 0;
    if (ctl & RG_CTL_P2_GRENADE) hi |= 1u << 1;
    if (ctl & s->ops[0]) lo |= 1u << 0;
    if (ctl & s->ops[1]) lo |= 1u << 1;
    if (ctl & s->ops[2]) hi |= 1u << 2;
    if (ctl & s->ops[3]) hi |= 1u << 3;
    rec[RG_REC_BTN_HI] = hi;
    rec[RG_REC_BTN_LO] = lo;
    /* munt: een teller die per indrukken met een ophoogt (het bord telt pulsen; de game kijkt naar het verschil) */
    if ((ctl & RG_CTL_COIN1) && !(s->prev_ctl & RG_CTL_COIN1)) s->coin1 = (s->coin1 + 1u) & 0xffffu;
    s->prev_ctl = ctl;
    rg_usbio_put32(rec, RG_REC_COIN1, s->coin1);
    rg_usbio_put32(rec, RG_REC_COIN2, s->coin2);
    rg_usbio_put32(rec, RG_REC_TICK, tick);
    rg_usbio_put32(rec, RG_REC_MODE, 2);
}

#endif
