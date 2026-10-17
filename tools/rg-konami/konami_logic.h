/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-konami: de logica van RavenGuns eigen Konami-I/O-bibliotheken, zonder Windows-afhankelijkheden (zodat tools/rg-konami/test.sh
 * hem op Linux kan testen). De DLL's (rg_le3_*.c, rg_gsh_*.c) lezen het bordbeeld en roepen deze functies aan.
 *
 * CLEAN-ROOM: elke indeling hieronder is gelezen uit de AANROEPEN in de game en uit wat de originele DLL in de structuur schrijft
 * (objdump -d, adressen in docs/research/konami/README.md). Geen code van de originele DLL's.
 *
 * Het bordbeeld is de mapping "TeknoParrot_JvsState" (64 bytes) van rg-jvsbridge, indeling WartranTroopers of
 * WartranTroopersKonami (packaging/tp-shm-profiles.json): een 32-bits bitveld op offset 8, en per speler een byte X en een byte Y op
 * offset 12 + 2 * speler (links/boven = 0). Een as <= 1 of >= 254 is "buiten beeld" (de offscreen-regel van die indeling).
 */
#ifndef RG_KONAMI_LOGIC_H
#define RG_KONAMI_LOGIC_H

#include <stdint.h>
#include <string.h>

/* Bits van het bordbeeld (WartranTroopers, offset 8). */
#define KB_TEST 0
#define KB_SERVICE 1
#define KB_MUNT1 2
#define KB_MUNT2 3
#define KB_START(p) (6 + 5 * (p))     /* p = 0..3 */
#define KB_TREKKER(p) (7 + 5 * (p))
#define KB_HERLADEN(p) (8 + 5 * (p))  /* "reload" = Button2 van TeknoParrot; bij LE3 heet die knop Option */
#define KB_GRANAAT(p) (9 + 5 * (p))   /* Button3; bij LE3 "Reload" in het TeknoParrot-profiel */

typedef struct {
    uint32_t bits;
    uint8_t as[8];   /* P1X P1Y P2X P2Y P3X P3Y P4X P4Y */
    int geldig;      /* 0 = geen bordbeeld (nog niet geopend): alles los, niemand mikt */
} kb_beeld;

static inline void kb_lees(const volatile uint8_t *b, kb_beeld *uit)
{
    memset(uit, 0, sizeof *uit);
    if (!b) return;
    uit->bits = (uint32_t)b[8] | ((uint32_t)b[9] << 8) | ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);
    for (int i = 0; i < 8; i++) uit->as[i] = b[12 + i];
    uit->geldig = 1;
}

static inline int kb_bit(const kb_beeld *b, int bit) { return b->geldig && ((b->bits >> bit) & 1u); }

/* De offscreen-regel van de indeling: een as op de rand (<= 1 of >= 254) betekent "buiten beeld". */
static inline int kb_buiten(uint8_t a) { return a <= 1 || a >= 254; }
static inline int kb_speler_buiten(const kb_beeld *b, int p)
{
    return !b->geldig || kb_buiten(b->as[2 * p]) || kb_buiten(b->as[2 * p + 1]);
}

/* ===================================================================================================================== */
/* Lethal Enforcers 3: iob_get_digital_in(IOB_DIGITAL_IN_W*) en gunio_read_data(...)                                    */
/* ===================================================================================================================== */

/* IOB_DIGITAL_IN_W: 0x2a bytes, gepakt. Gelezen uit iob.dll 0x10006400 (wat het origineel schrijft) en uit game.exe (het I/O-object
 * op [0x694150], de structuur staat daarin op +0xe438, de vorige kopie op +0xe464):
 *   +0x00 int   geldig (game.exe 0x51db80: munten tellen alleen als nu en vorige keer geldig)
 *   +0x04 u8    TEST      (0x51d860 flank, 0x51d8a0 niveau; testmenu 0x5477bd, I/O-test rij "systeem" item 0)
 *   +0x07 u8    SERVICE   (0x51d8b0 flank: 0x5238a1 telt 1 krediet bij; I/O-test item 1)
 *   +0x05 u8    in de I/O-test 10 beelden zichtbaar als <> 0 (0x547781, item 2: de munt); de munten tellen via +0x26
 *   +0x08 u8[2] START per speler, niveau (0x51d840; I/O-test spelersrij item 0)
 *   +0x0a u8[2] TREKKER per speler, niveau (0x51d710; richtlus 0x51ef3c/0x51efbd: "trekker vast")
 *   +0x0c u8[2] OPTION per speler, niveau (0x51d760; I/O-test item 2)
 *   +0x0e u8[2] START-teller: de game kijkt alleen of hij veranderd is (0x51d810)
 *   +0x10 u8[2] TREKKER-teller (0x51d6e0; richtlus 0x51ef9a: "schot")
 *   +0x12 u8[2] OPTION-teller (0x51d730)
 *   +0x26 u8    munten, 6 bits (0x51db80 telt het verschil modulo 0x40; een waarde >= 0x40 laat die lus nooit stoppen!)
 *   de rest (+0x14..+0x25, +0x27..+0x29): 0. */
#define LE3_DIN_GROOTTE 0x2a

typedef struct {
    kb_beeld vorig;
    uint8_t teller_start[2], teller_trekker[2], teller_option[2];
    uint8_t munten;    /* 0..63 */
    int begonnen;
} le3_iob_staat;

static inline int kb_flank(const kb_beeld *nu, const kb_beeld *vorig, int bit) { return kb_bit(nu, bit) && !kb_bit(vorig, bit); }

static inline void le3_digital_in(le3_iob_staat *s, const kb_beeld *nu, uint8_t uit[LE3_DIN_GROOTTE])
{
    if (s->begonnen) {
        for (int p = 0; p < 2; p++) {
            if (kb_flank(nu, &s->vorig, KB_START(p))) s->teller_start[p]++;
            if (kb_flank(nu, &s->vorig, KB_TREKKER(p))) s->teller_trekker[p]++;
            if (kb_flank(nu, &s->vorig, KB_HERLADEN(p))) s->teller_option[p]++;
        }
        /* Kast met een muntproever: munt 1 en munt 2 tellen allebei als munt (de game heeft er maar een teller voor). */
        if (kb_flank(nu, &s->vorig, KB_MUNT1)) s->munten = (uint8_t)((s->munten + 1) & 0x3f);
        if (kb_flank(nu, &s->vorig, KB_MUNT2)) s->munten = (uint8_t)((s->munten + 1) & 0x3f);
    }
    s->vorig = *nu;
    s->begonnen = 1;
    memset(uit, 0, LE3_DIN_GROOTTE);
    uit[0] = 1;   /* geldig (int, little endian) */
    uit[0x04] = (uint8_t)kb_bit(nu, KB_TEST);
    uit[0x07] = (uint8_t)kb_bit(nu, KB_SERVICE);
    /* GEGOKT (alleen de I/O-test toont het): +0x05 als niveau van de muntschakelaar; het tellen gaat via +0x26. */
    uit[0x05] = (uint8_t)(kb_bit(nu, KB_MUNT1) || kb_bit(nu, KB_MUNT2));
    for (int p = 0; p < 2; p++) {
        uit[0x08 + p] = (uint8_t)kb_bit(nu, KB_START(p));
        uit[0x0a + p] = (uint8_t)kb_bit(nu, KB_TREKKER(p));
        uit[0x0c + p] = (uint8_t)kb_bit(nu, KB_HERLADEN(p));
        uit[0x0e + p] = s->teller_start[p];
        uit[0x10 + p] = s->teller_trekker[p];
        uit[0x12 + p] = s->teller_option[p];
    }
    uit[0x26] = s->munten;
}

/* gunio_read_data(int geweer, float *x, float *y, int *buiten, int *ruw_x, int *ruw_y). Gelezen uit gunio.dll 0x10002dd5: ruw_x en
 * ruw_y zijn een byte (0..255) per as, *buiten is bit (7 - geweer) van het statusbyte (1 = geen signaal). De game (0x51d540) gebruikt
 * alleen ruw_x/ruw_y en *buiten; x/y rekent hij zelf opnieuw uit met de kalibratie uit SAVE/io.sav:
 *     ruw > midden: scherm = 320 - (ruw - midden) * schaal_plus   (Y: 240 - ... * schaal_plus_y)
 *     anders:       scherm = 320 + (midden - ruw) * schaal_min
 * en <> 0 in *buiten maakt het punt -1 (buiten beeld). De standaardschalen van de game (0x51e9c0: 2,5 en 1,875 = 320/128 en
 * 240/128) laten zien dat een byte het hele scherm beslaat, met hoog = links/boven. Wij geven daarom ruw = 255 - as, met de
 * kalibratie in dropin/SAVE/io.sav (midden 127, schalen 2,5 en 1,875): scherm-X = 2,5 * as, scherm-Y = 1,875 * as. */
#define LE3_MIDDEN 127

typedef struct {
    int ruw_x, ruw_y, buiten;
    float x, y;   /* alleen ter informatie (de game overschrijft ze): het punt op een scherm van 640 x 480 */
} le3_schot;

static inline void le3_gun(const kb_beeld *b, int geweer, le3_schot *uit)
{
    const int p = geweer & 1;
    const uint8_t ax = b->as[2 * p], ay = b->as[2 * p + 1];
    uit->ruw_x = 255 - ax;
    uit->ruw_y = 255 - ay;
    /* Buiten beeld: de rand van de as, of de knop "Reload" van het TeknoParrot-profiel (Button3 = granaat in de indeling) die het
     * geweer van het scherm haalt (herladen is in deze game buiten het scherm schieten). */
    uit->buiten = kb_speler_buiten(b, p) || kb_bit(b, KB_GRANAAT(p));
    uit->x = 2.5f * ax;
    uit->y = 1.875f * ay;
}

/* SAVE/io.sav: kop van 0x14 bytes ("I/OD", 0, controle2, controle1, lengte 0x3c) en 0x3c bytes gegevens (game.exe 0x51dc10 schrijft,
 * 0x51dc80 leest, 0x520020/0x5202d0 doen het bestand). De controles zijn een CRC-32 met het (niet-standaard) polynoom 0xEDB88340,
 * gespiegeld, begin ~0, eind ~: controle1 over de gegevens, controle2 over de 4 bytes van controle1 (game.exe 0x5315b0..0x5316a0).
 * Gegevens: frequentie-id (int), gekalibreerd P1 en P2 (int), en per speler midden X, midden Y (int) en de schalen X+, Y+, X-, Y-
 * (float). Nagerekend tegen de io.sav op de NAS: beide controles kloppen. */
#define LE3_IOSAV_GROOTTE 0x50

static inline uint32_t le3_crc(const uint8_t *d, size_t n)
{
    uint32_t s = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = (s ^ d[i]) & 0xffu;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xedb88340u : c >> 1;
        s = (s >> 8) ^ c;
    }
    return ~s;
}

static inline void kb_zet32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static inline void kb_zetf(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    kb_zet32(p, v);
}

static inline void le3_maak_iosav(uint8_t uit[LE3_IOSAV_GROOTTE])
{
    memset(uit, 0, LE3_IOSAV_GROOTTE);
    memcpy(uit, "I/OD", 4);
    kb_zet32(uit + 0x10, 0x3c);
    uint8_t *g = uit + 0x14;
    kb_zet32(g + 0x00, 0);   /* frequentie-id (gunio_change_freq_id) */
    kb_zet32(g + 0x04, 1);   /* P1 gekalibreerd */
    kb_zet32(g + 0x08, 1);   /* P2 gekalibreerd */
    for (int p = 0; p < 2; p++) {
        uint8_t *s = g + 0x0c + 0x18 * p;
        kb_zet32(s + 0x00, LE3_MIDDEN);
        kb_zet32(s + 0x04, LE3_MIDDEN);
        kb_zetf(s + 0x08, 2.5f);     /* X, ruw > midden */
        kb_zetf(s + 0x0c, 1.875f);   /* Y, ruw > midden */
        kb_zetf(s + 0x10, 2.5f);     /* X, ruw <= midden */
        kb_zetf(s + 0x14, 1.875f);   /* Y, ruw <= midden */
    }
    const uint32_t c1 = le3_crc(g, 0x3c);
    uint8_t c1b[4];
    kb_zet32(c1b, c1);
    kb_zet32(uit + 0x08, le3_crc(c1b, 4));
    kb_zet32(uit + 0x0c, c1);
}

/* Wat game.exe 0x51d540 van ruw maakt, met de kalibratie hierboven (voor de test). */
static inline float le3_scherm(int ruw, int midden, float plus, float min, float centrum)
{
    return midden < ruw ? centrum - (float)(ruw - midden) * plus : centrum + (float)(midden - ruw) * min;
}

/* ===================================================================================================================== */
/* Gashaaaan: iob_f24_* (bin/iob.dll) en ball_io_* (bin/ballio.dll)                                                       */
/* ===================================================================================================================== */

/* iob_f24_button(i) (iob.dll 0x1000eea0, tien knoppen) en de labels van INPUT CHECK in testmode.dll (0x100045b0..0x10004be1):
 * 0..3 = START P1..P4, 4 = TEST, 5 = SERVICE; 6..9 komen niet in INPUT CHECK voor (0). iob_f24_button_trg(i) = nu ingedrukt en bij
 * de vorige iob_step niet (iob.dll 0x1000ef90). iob_f24_nr_coin(mech, &modulo): een 6-bits teller per muntproever (0x1000f0b0,
 * modulo 0x3f; gcoin.dll rekent het verschil). */
#define GSH_KNOPPEN 10

typedef struct {
    uint8_t nu[GSH_KNOPPEN], vorig[GSH_KNOPPEN];
    uint8_t munt[2];          /* 0..63 */
    kb_beeld beeld_vorig;
    int begonnen;
} gsh_iob_staat;

static inline int gsh_bit_van_knop(int i)
{
    if (i >= 0 && i < 4) return KB_START(i);
    if (i == 4) return KB_TEST;
    if (i == 5) return KB_SERVICE;
    return -1;
}

static inline void gsh_iob_stap(gsh_iob_staat *s, const kb_beeld *b)
{
    memcpy(s->vorig, s->nu, sizeof s->nu);
    for (int i = 0; i < GSH_KNOPPEN; i++) {
        const int bit = gsh_bit_van_knop(i);
        s->nu[i] = (uint8_t)(bit >= 0 && kb_bit(b, bit));
    }
    if (s->begonnen) {
        if (kb_flank(b, &s->beeld_vorig, KB_MUNT1)) s->munt[0] = (uint8_t)((s->munt[0] + 1) & 0x3f);
        if (kb_flank(b, &s->beeld_vorig, KB_MUNT2)) s->munt[1] = (uint8_t)((s->munt[1] + 1) & 0x3f);
    }
    s->beeld_vorig = *b;
    s->begonnen = 1;
}

static inline uint8_t gsh_knop(const gsh_iob_staat *s, int i) { return i >= 0 && i < GSH_KNOPPEN ? s->nu[i] : 0; }
static inline uint8_t gsh_knop_trg(const gsh_iob_staat *s, int i)
{
    return i >= 0 && i < GSH_KNOPPEN ? (uint8_t)(s->nu[i] && !s->vorig[i]) : 0;
}

/* T_BALL_IO_POINT = { short x; short y; } in schermpixels (ballio.dll 0x10008840 kopieert 4 bytes per punt; player.dll 0x10001206
 * deelt x door schermbreedte / 4 om de speler te vinden die de bal gooide: elke speler heeft een kwart van het scherm). De
 * originele ballio.dll zet ook een muisklik als inslag in die lijst (0x100084f7: check_kbd_trg(1) + mouse_util_get_point, in
 * vensterpixels). Een worp is hier: de trekker van speler p gaat in (opgaande flank) terwijl hij in beeld mikt. */
#define GSH_MAX_INSLAGEN 16

typedef struct { int16_t x, y; } gsh_punt;

typedef struct {
    gsh_punt punt[GSH_MAX_INSLAGEN];
    int aantal;
    kb_beeld vorig;
    int begonnen;
} gsh_bal_staat;

static inline int gsh_as_naar_pixel(uint8_t a, int lengte)
{
    if (lengte <= 1) return 0;
    int v = (int)a * (lengte - 1) / 255;
    return v < 0 ? 0 : v > lengte - 1 ? lengte - 1 : v;
}

/* Bouwt de inslagen van dit beeld (zoals ball_io_step: elke stap een nieuwe lijst). */
static inline void gsh_bal_stap(gsh_bal_staat *s, const kb_beeld *b, int breedte, int hoogte)
{
    s->aantal = 0;
    if (s->begonnen) {
        for (int p = 0; p < 4 && s->aantal < GSH_MAX_INSLAGEN; p++) {
            if (!kb_flank(b, &s->vorig, KB_TREKKER(p)) || kb_speler_buiten(b, p)) continue;
            s->punt[s->aantal].x = (int16_t)gsh_as_naar_pixel(b->as[2 * p], breedte);
            s->punt[s->aantal].y = (int16_t)gsh_as_naar_pixel(b->as[2 * p + 1], hoogte);
            s->aantal++;
        }
    }
    s->vorig = *b;
    s->begonnen = 1;
}

/* ball_io_point_get(buf, bytes): hoogstens bytes / 4 punten, geeft het aantal. */
static inline int gsh_bal_punten(const gsh_bal_staat *s, gsh_punt *buf, int bytes)
{
    if (!buf || bytes < 4) return 0;
    int n = s->aantal;
    if (n > bytes / 4) n = bytes / 4;
    for (int i = 0; i < n; i++) buf[i] = s->punt[i];
    return n;
}

#endif
