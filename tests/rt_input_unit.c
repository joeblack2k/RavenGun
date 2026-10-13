/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest voor de invoerhulp (tools/rt-input-shim/rg_rt_input.c): schakelaars, geweerknoppen en assen uit het bordbeeld naar
 * het geheugen van de game, volgens een tabel per game.
 *
 * Wordt gebouwd door tests/rt_input_test.sh, met de bron van de hulp zelf geinclude (RG_RT_INPUT_TEST laat de draad en de
 * constructor weg). Het "geheugen van de game" is hier een eigen stuk geheugen (rg_mem_basis), zodat de echte adressen van de
 * tabel gebruikt kunnen worden zonder dat er een spel is.
 */
#include RG_BRON

static int fouten;
#define CHECK(c, ...) do { if (!(c)) { fouten++; printf("FAIL (regel %d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define GROOTTE 0x1900000UL     /* genoeg voor de adressen van de ingebouwde standaard (tot 0x181c600) */
static uint8_t *geheugen;

/* --- ZO WAS HET: de werking van de hulp voor deze wijziging, regel voor regel overgenomen uit de oude draad --------------- */
/* De adressen, de signatuur en de tabel staan hier LETTERLIJK, los van de bron: verandert iemand de ingebouwde standaard van de
   hulp, dan wijkt die af van deze kopie en gaat de test rood. */
#define OUD_GETTER 0x61e890UL
#define OUD_BASE 0x180ae80UL
#define OUD_OFF 0xaea0UL
#define OUD_BORD 0x2200UL
#define OUD_INGANG 0x50UL
static const unsigned char oud_sig[8] = {0x85, 0xff, 0x0f, 0x88, 0xd0, 0x05, 0x00, 0x00};
static const struct { int bit, board, k, ms; } OUD_MAP[] = {
    {6, 0, 0, 300}, {11, 1, 0, 300}, {16, 2, 0, 300}, {21, 3, 0, 300},
    {0, 0, 4, 300}, {0, 1, 4, 300}, {0, 2, 4, 300}, {0, 3, 4, 300},
    {1, 0, 3, 300},
    {2, 0, 5, 14}, {3, 1, 5, 14},
    {4, 0, 1, 300}, {5, 0, 2, 300},
};
#define OUD_NMAP (int)(sizeof(OUD_MAP) / sizeof(OUD_MAP[0]))
struct oud { uint32_t prev; uint64_t until[OUD_NMAP], rel_until[OUD_NMAP]; int active[OUD_NMAP]; };
static volatile uint32_t *oud_entry(int board, int k) {
    return (volatile uint32_t *)(rg_mem_basis + OUD_BASE + OUD_OFF + (unsigned long)board * OUD_BORD + (unsigned long)k * OUD_INGANG);
}
static void oud_stap(struct oud *o, const uint8_t *buf, ssize_t gelezen, uint64_t t, int pulse_override) {
#define MAP OUD_MAP
    if (gelezen < 12) return;
    uint32_t bits = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) | ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
    for (int i = 0; i < OUD_NMAP; i++) {
        int now = (bits >> MAP[i].bit) & 1, was = (o->prev >> MAP[i].bit) & 1;
        if (now && !was) { o->until[i] = t + (uint64_t)(pulse_override > 0 ? pulse_override : MAP[i].ms); o->active[i] = 1; { volatile uint32_t *e0 = oud_entry(MAP[i].board, MAP[i].k); e0[6]++; } }
        volatile uint32_t *e = oud_entry(MAP[i].board, MAP[i].k);
        if (!o->active[i] && o->rel_until[i]) { if (t < o->rel_until[i]) e[4] = 1; else { e[4] = 0; o->rel_until[i] = 0; } }
        if (o->active[i]) {
            if (t < o->until[i]) { e[0] = 1; e[3] = 1; e[5] = 1; }
            else { e[3] = 0; e[5] = 0; e[7]++; o->active[i] = 0; o->rel_until[i] = t + 25; }
        }
    }
    o->prev = bits;
#undef MAP
}

/* --- De rekenregel van de IO-brug, letterlijk uit tp_shm_render (src/ravengun/iobridge/tp_shm.cpp), voor een byte-as ------- */
static unsigned brug_as(double f, double amin, double amax, int invert, int offscreen_margin, int out) {
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;                                   /* f = std::min(1.0, std::max(0.0, f)); */
    double v = round(amin + f * (amax - amin));             /* double v = std::round(a.min + f * (a.max - a.min)); */
    if (invert) v = amin + amax - v;                        /* if (a.invert) v = a.min + a.max - v; */
    if (offscreen_margin > 0 && amin <= 1 && amax >= 254) { /* if (a.bytes == 1 && p.offscreen_margin > 0 && a.min <= 1 && a.max >= 254) */
        if (!out) { double hi = 255.0 - offscreen_margin, lo = (double)offscreen_margin; v = v > hi ? hi : v; v = v < lo ? lo : v; }
    }
    if (v > 255.0) v = 255.0;
    if (v < 0.0) v = 0.0;                                   /* v = std::min(255.0, std::max(0.0, v)); */
    return (unsigned)v;
}

static void leeg(void) { memset(geheugen + TABLE_BASE, 0, 0x30000); memset(geheugen + 0x1000, 0, 0x1000); }
static void zet_getter(void) { memcpy(geheugen + GETTER_ADDR, getter_sig, sizeof getter_sig); }
static void beeld(uint8_t *b, uint32_t bits) { memset(b, 0, RG_BEELD); b[8] = bits & 0xff; b[9] = (bits >> 8) & 0xff; b[10] = (bits >> 16) & 0xff; b[11] = (bits >> 24) & 0xff; }
static uint32_t *ing(int board, int k) { return (uint32_t *)(geheugen + TABLE_BASE + TABLE_OFF + (unsigned long)board * BOARD_STRIDE + (unsigned long)k * ENTRY_STRIDE); }
static int lees(const char *tekst, struct rg_tabel *t, char *fout) { fout[0] = 0; return rg_tabel_lees(tekst, t, fout, 256); }

/* 1. Zonder tabel: precies wat er altijd gebeurde. Een reeks beelden door de oude en de nieuwe werking; het geheugen moet na
      elke stap byte voor byte gelijk zijn. */
static void test_zonder_tabel_is_het_oude_gedrag(void) {
    static uint8_t na_oud[0x30000];
    struct { uint64_t t; uint32_t bits; int n; } reeks[] = {
        {1000, 0, 64}, {1001, 1u << 6, 64}, {1100, 1u << 6, 64}, {1299, 1u << 6, 64}, {1301, 1u << 6, 64}, {1320, 0, 64}, {1330, 0, 64},
        {2000, 1u << 2, 64}, {2010, 1u << 2, 64}, {2015, 0, 64}, {2041, 0, 64},                                    /* munt: 14 ms */
        {3000, 1u << 0, 64}, {3200, (1u << 0) | (1u << 1), 64}, {3301, 1u << 1, 64}, {3520, 0, 64}, {3600, 0, 64},  /* test op vier borden, dan service erbij */
        {4000, (1u << 11) | (1u << 16) | (1u << 21) | (1u << 3) | (1u << 4) | (1u << 5), 64}, {4013, 0, 64}, {4015, 0, 64}, {4400, 0, 64},
        {5000, 1u << 6, 64}, {5050, 0, 64}, {5100, 1u << 6, 64}, {5290, 1u << 6, 64}, {5310, 0, 64}, {5500, 0, 64},     /* opnieuw indrukken tijdens een puls */
        {6000, 0xffffffffu, 64}, {6400, 0, 64}, {6500, 0, 64},
        {7000, 1u << 6, 11}, {7001, 1u << 6, 12}, {7400, 0, 12}, {7500, 0, 12},                                     /* een beeld dat te kort is, en een van precies 12 bytes */
        {8000, (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10), 64}, {8500, 0, 64},                                  /* de geweerknoppen: de oude hulp deed er niets mee */
    };
    int n = (int)(sizeof reeks / sizeof reeks[0]);
    for (int override = 0; override <= 40; override += 40) {
        uint8_t b[RG_BEELD];
        leeg(); struct oud o; memset(&o, 0, sizeof o);
        static uint8_t stappen_oud[64][0x400];
        for (int i = 0; i < n; i++) { beeld(b, reeks[i].bits); b[12] = 77; b[13] = 200; oud_stap(&o, b, reeks[i].n, reeks[i].t, override); memcpy(stappen_oud[i], ing(0, 0), 0x400); }
        memcpy(na_oud, geheugen + TABLE_BASE, sizeof na_oud);

        leeg(); struct rg_tabel tb; rg_tabel_standaard(&tb); struct rg_staat st; memset(&st, 0, sizeof st);
        CHECK(tb.nsw == OUD_NMAP && tb.nbit == 0 && tb.nas == 0 && tb.nteller == 0 && tb.nzet == 0, "de ingebouwde standaard heeft %d schakelaars, %d bits, %d assen, %d tellers, %d vaste waarden", tb.nsw, tb.nbit, tb.nas, tb.nteller, tb.nzet);
        CHECK(tb.getter == OUD_GETTER && tb.nsig == 8 && memcmp(tb.sig, oud_sig, 8) == 0 && tb.basis == OUD_BASE && tb.eerste == OUD_OFF && tb.per_bord == OUD_BORD
              && tb.per_schakelaar == OUD_INGANG && tb.beeld_bits == 8, "de ingebouwde adressen of de signatuur zijn veranderd");
        for (int i = 0; i < OUD_NMAP && i < tb.nsw; i++)
            CHECK(tb.sw[i].bit == OUD_MAP[i].bit && tb.sw[i].board == OUD_MAP[i].board && tb.sw[i].k == OUD_MAP[i].k && tb.sw[i].ms == OUD_MAP[i].ms, "ingebouwde schakelaar %d is veranderd", i);
        for (int i = 0; i < n; i++) {
            beeld(b, reeks[i].bits); b[12] = 77; b[13] = 200;
            rg_stap(&tb, &st, b, reeks[i].n, reeks[i].t, override);
            CHECK(memcmp(stappen_oud[i], ing(0, 0), 0x400) == 0, "stap %d (t=%llu, bits=%08x, puls-override %d): bord 0 wijkt af van de oude werking", i, (unsigned long long)reeks[i].t, reeks[i].bits, override);
        }
        CHECK(memcmp(na_oud, geheugen + TABLE_BASE, sizeof na_oud) == 0, "na de hele reeks (puls-override %d) wijkt het geheugen af van de oude werking", override);
    }
    /* en de oude hulp deed echt iets (anders vergelijkt deze test twee keer niets) */
    CHECK(ing(0, 0)[6] > 0 && ing(1, 4)[6] > 0 && ing(0, 5)[6] > 0, "de reeks heeft geen indrukken geteld");
}

/* 2. Een tabel met geweerknoppen in de schakelaartabel: puls=0 houdt de knop ingedrukt zolang de bit staat. */
static void test_knop_blijft_ingedrukt(void) {
    struct rg_tabel tb; char fout[256]; struct rg_staat st; memset(&st, 0, sizeof st); uint8_t b[RG_BEELD];
    leeg();
    CHECK(lees("# trekker en herladen van speler 1 en de trekker van speler 2\n"
               "schakelaar bit=6 bord=0 k=0 puls=300\n"
               "schakelaar bit=7 bord=0 k=9 puls=0   ; de trekker: een knop, geen puls\n"
               "schakelaar bit=8 bord=0 k=10 puls=0\n"
               "schakelaar bit=12 bord=1 k=9 puls=0\n", &tb, fout), "tabel afgekeurd: %s", fout);
    CHECK(tb.nsw == 4, "eigen schakelaars vervangen de ingebouwde: %d", tb.nsw);
    uint32_t *trek = ing(0, 9), *herl = ing(0, 10), *trek2 = ing(1, 9), *start = ing(0, 0);
    beeld(b, 0); rg_stap(&tb, &st, b, 64, 1000, 0);
    CHECK(trek[3] == 0 && trek[5] == 0 && trek[6] == 0, "voor de indruk staat er al iets");
    beeld(b, (1u << 7) | (1u << 6)); rg_stap(&tb, &st, b, 64, 1010, 0);
    CHECK(trek[0] == 1 && trek[3] == 1 && trek[5] == 1 && trek[6] == 1, "trekker ingedrukt: [0]=%u [3]=%u [5]=%u [6]=%u", trek[0], trek[3], trek[5], trek[6]);
    CHECK(herl[3] == 0 && trek2[3] == 0, "een andere knop ging mee");
    /* vijf seconden vasthouden: veel langer dan welke puls ook. De start (puls 300) is dan allang los, de trekker niet. */
    for (uint64_t t = 1020; t <= 6000; t += 10) rg_stap(&tb, &st, b, 64, t, 0);
    CHECK(trek[3] == 1 && trek[5] == 1 && trek[6] == 1 && trek[7] == 0, "na 5 s vasthouden: [3]=%u [5]=%u [6]=%u [7]=%u", trek[3], trek[5], trek[6], trek[7]);
    CHECK(start[3] == 0 && start[7] == 1, "de start-puls van 300 ms liep niet af: [3]=%u [7]=%u", start[3], start[7]);
    /* een puls-override (RG_RT_PULSE_MS) maakt van een knop geen puls */
    rg_stap(&tb, &st, b, 64, 6010, 40); rg_stap(&tb, &st, b, 64, 6200, 40);
    CHECK(trek[3] == 1, "de puls-override liet de trekker los");
    beeld(b, 0); rg_stap(&tb, &st, b, 64, 6210, 0);
    CHECK(trek[3] == 0 && trek[5] == 0 && trek[7] == 1 && trek[4] == 0, "loslaten: [3]=%u [5]=%u [7]=%u", trek[3], trek[5], trek[7]);
    rg_stap(&tb, &st, b, 64, 6220, 0);
    CHECK(trek[4] == 1, "na het loslaten hoort [4] (loslating in dit beeld) even te staan");
    rg_stap(&tb, &st, b, 64, 6240, 0);
    CHECK(trek[4] == 0, "[4] bleef staan");
    /* twee keer kort achter elkaar: twee indrukken geteld */
    beeld(b, 1u << 7); rg_stap(&tb, &st, b, 64, 6300, 0); beeld(b, 0); rg_stap(&tb, &st, b, 64, 6310, 0);
    beeld(b, 1u << 7); rg_stap(&tb, &st, b, 64, 6320, 0); beeld(b, 0); rg_stap(&tb, &st, b, 64, 6330, 0);
    CHECK(trek[6] == 3 && trek[7] == 3, "drie indrukken en drie loslatingen: [6]=%u [7]=%u", trek[6], trek[7]);
    /* speler 2 op zijn eigen bord */
    beeld(b, 1u << 12); rg_stap(&tb, &st, b, 64, 7000, 0);
    CHECK(trek2[3] == 1 && trek[3] == 0, "de trekker van speler 2 kwam niet op bord 1");
}

/* 3. Losse bits: bit 7..10 uit het beeld naar een woord in de game; de andere bits van dat woord blijven staan. */
static void test_losse_bits(void) {
    struct rg_tabel tb; char fout[256]; struct rg_staat st; memset(&st, 0, sizeof st); uint8_t b[RG_BEELD];
    leeg();
    CHECK(lees("bit bit=7 adres=0x1100 breedte=4 doelbit=0\n"
               "bit bit=8 adres=0x1100 breedte=4 doelbit=1\n"
               "bit bit=9 adres=0x1100 breedte=4 doelbit=31\n"
               "bit bit=10 adres=0x1110 breedte=1 doelbit=7\n"
               "bit bit=12 adres=0x1120 breedte=2 doelbit=9 inverteer=ja\n", &tb, fout), "tabel afgekeurd: %s", fout);
    CHECK(tb.nsw == NSTANDAARD && tb.nbit == 5, "een tabel zonder schakelaars houdt de ingebouwde: %d schakelaars, %d bits", tb.nsw, tb.nbit);
    uint32_t w; uint8_t w8; uint16_t w16;
    w = 0x00f00f00u; memcpy(geheugen + 0x1100, &w, 4); geheugen[0x1110] = 0x55; geheugen[0x1111] = 0xee; w16 = 0x0101; memcpy(geheugen + 0x1120, &w16, 2);
    beeld(b, (1u << 7) | (1u << 9) | (1u << 10)); rg_stap(&tb, &st, b, 64, 1000, 0);
    memcpy(&w, geheugen + 0x1100, 4); w8 = geheugen[0x1110]; memcpy(&w16, geheugen + 0x1120, 2);
    CHECK(w == (0x00f00f00u | 1u | (1u << 31)), "woord na trekker+granaat: %08x", w);
    CHECK(w8 == (0x55 | 0x80) && geheugen[0x1111] == 0xee, "byte na aux: %02x, buurbyte %02x", w8, geheugen[0x1111]);
    CHECK(w16 == (0x0101 | (1u << 9)), "omgekeerde bit (bron uit = doel aan): %04x", w16);
    beeld(b, (1u << 8) | (1u << 12)); rg_stap(&tb, &st, b, 64, 1010, 0);
    memcpy(&w, geheugen + 0x1100, 4); w8 = geheugen[0x1110]; memcpy(&w16, geheugen + 0x1120, 2);
    CHECK(w == (0x00f00f00u | 2u), "woord na loslaten en herladen: %08x", w);
    CHECK(w8 == 0x55, "aux los: %02x", w8);
    CHECK(w16 == 0x0101, "omgekeerde bit (bron aan = doel uit): %04x", w16);
}

/* 4. De acht asbytes (offset 12..19) naar de game, met de rekenregel van de IO-brug en alle randgevallen. */
static void test_assen(void) {
    struct rg_tabel tb; char fout[256]; struct rg_staat st; memset(&st, 0, sizeof st); uint8_t b[RG_BEELD];
    leeg();
    CHECK(lees("as bron=12 adres=0x1200 soort=u8 min=0 max=255 inverteer=nee marge=2\n"
               "as bron=13 adres=0x1201 soort=u8 min=0 max=255 inverteer=nee marge=2\n"
               "as bron=14 adres=0x1202 marge=2\n"                                    /* soort, min, max en inverteer hebben een standaard */
               "as bron=15 adres=0x1203 marge=2\n"
               "as bron=16 adres=0x1204 marge=2\nas bron=17 adres=0x1205 marge=2\nas bron=18 adres=0x1206 marge=2\nas bron=19 adres=0x1207 marge=2\n", &tb, fout), "tabel afgekeurd: %s", fout);
    CHECK(tb.nas == 8, "acht assen: %d", tb.nas);
    /* de acht bytes op hun plek */
    beeld(b, 0); for (int i = 0; i < 8; i++) b[12 + i] = (uint8_t)(20 + 25 * i);
    geheugen[0x11ff] = 0xaa; geheugen[0x1208] = 0xbb;
    rg_stap(&tb, &st, b, 64, 1000, 0);
    for (int i = 0; i < 8; i++) CHECK(geheugen[0x1200 + i] == 20 + 25 * i, "as %d (bron %d): %u, verwacht %d", i, 12 + i, geheugen[0x1200 + i], 20 + 25 * i);
    CHECK(geheugen[0x11ff] == 0xaa && geheugen[0x1208] == 0xbb, "er is naast de assen geschreven");
    /* de uiterste waarden: 0 en 255 (en 1 en 254) betekenen buiten beeld en blijven staan; binnen het scherm 2..253 */
    struct { int in, uit; } rand[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {127, 127}, {128, 128}, {252, 252}, {253, 253}, {254, 254}, {255, 255}};
    for (unsigned i = 0; i < sizeof rand / sizeof rand[0]; i++) {
        b[12] = (uint8_t)rand[i].in; rg_stap(&tb, &st, b, 64, 1000, 0);
        CHECK(geheugen[0x1200] == rand[i].uit, "asbyte %d -> %u, verwacht %d", rand[i].in, geheugen[0x1200], rand[i].uit);
    }
    /* een korter beeld: de assen die er niet in staan blijven wat ze waren, de schakelaars werken */
    b[12] = 99; b[13] = 98; geheugen[0x1200] = 7; geheugen[0x1201] = 8;
    rg_stap(&tb, &st, b, 13, 1000, 0);
    CHECK(geheugen[0x1200] == 99 && geheugen[0x1201] == 8, "beeld van 13 bytes: as 0 = %u (99), as 1 = %u (8, onaangeroerd)", geheugen[0x1200], geheugen[0x1201]);

    /* de marge aan de kant van de game: binnen het scherm nooit dichter dan de marge bij de rand; buiten beeld wel de rand */
    CHECK(lees("as bron=12 adres=0x1200 soort=u8 min=0 max=255 marge=10\n", &tb, fout), "tabel afgekeurd: %s", fout);
    struct { int in, uit; } marge[] = {{0, 0}, {1, 1}, {2, 10}, {5, 10}, {10, 10}, {11, 11}, {244, 244}, {245, 245}, {250, 245}, {253, 245}, {254, 254}, {255, 255}};
    for (unsigned i = 0; i < sizeof marge / sizeof marge[0]; i++) {
        b[12] = (uint8_t)marge[i].in; rg_stap(&tb, &st, b, 64, 1000, 0);
        CHECK(geheugen[0x1200] == marge[i].uit, "marge 10: asbyte %d -> %u, verwacht %d", marge[i].in, geheugen[0x1200], marge[i].uit);
    }
    /* inverteren: het complement, en buiten beeld wisselt de rand van kant */
    CHECK(lees("as bron=12 adres=0x1200 soort=u8 min=0 max=255 inverteer=ja marge=2\n", &tb, fout), "tabel afgekeurd: %s", fout);
    struct { int in, uit; } inv[] = {{0, 255}, {1, 254}, {2, 253}, {3, 252}, {100, 155}, {127, 128}, {252, 3}, {253, 2}, {254, 1}, {255, 0}};
    for (unsigned i = 0; i < sizeof inv / sizeof inv[0]; i++) {
        b[12] = (uint8_t)inv[i].in; rg_stap(&tb, &st, b, 64, 1000, 0);
        CHECK(geheugen[0x1200] == inv[i].uit, "omgekeerd: asbyte %d -> %u, verwacht %d", inv[i].in, geheugen[0x1200], inv[i].uit);
    }
    /* EERST afronden, DAN inverteren: bij een bereik van 0..127,5 geeft byte 3 de waarde 1,5 -> afgerond 2 -> omgekeerd 125,5 -> 125.
       Andersom (eerst omkeren, dan afronden) zou het 126 zijn. */
    CHECK(lees("as bron=12 adres=0x1200 soort=u8 min=0 max=127.5 inverteer=ja bron_marge=0\n", &tb, fout), "tabel afgekeurd: %s", fout);
    b[12] = 3; rg_stap(&tb, &st, b, 64, 1000, 0);
    CHECK(geheugen[0x1200] == 125, "eerst afronden dan inverteren: %u, verwacht 125 (126 = verkeerde orde)", geheugen[0x1200]);
}

/* 5. Letterlijk de regel van de IO-brug: voor elke fractie en elke instelling dezelfde byte als tp_shm_render. */
static void test_rekenregel_is_die_van_de_brug(void) {
    int verschillen = 0, gevallen = 0;
    for (int invert = 0; invert <= 1; invert++)
        for (int marge = 0; marge <= 10; marge += (marge < 2 ? 2 : 8))
            for (int out = 0; out <= 1; out++)
                for (int k = 0; k <= 2000; k++) {
                    double f = k / 2000.0;
                    if (out && k != 0 && k != 2000) continue;        /* buiten beeld bestaat alleen op de rand (f = 0 of 1) */
                    struct rg_as a = {12, 0, RG_U8, 0, 255, invert, marge, 2};
                    double v = rg_as_waarde(&a, f, out);
                    unsigned onze = (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v);
                    unsigned brug = brug_as(f, 0, 255, invert, marge, out);
                    gevallen++;
                    if (onze != brug && verschillen++ < 5) printf("FAIL: f=%.4f invert=%d marge=%d buiten=%d: hulp %u, brug %u\n", f, invert, marge, out, onze, brug);
                }
    CHECK(verschillen == 0, "%d van %d gevallen wijken af van de regel van de IO-brug", verschillen, gevallen);
    CHECK(gevallen > 10000, "te weinig gevallen vergeleken: %d", gevallen);
    /* De rondgang: wat de brug in het beeld zet, zet de hulp ongewijzigd in de game (zelfde bereik, zelfde marge). */
    int rond = 0;
    for (int out = 0; out <= 1; out++)
        for (int k = 0; k <= 2000; k++) {
            double f = k / 2000.0;
            if (out && k != 0 && k != 2000) continue;
            unsigned in_beeld = brug_as(f, 0, 255, 0, 2, out);
            struct rg_as a = {12, 0, RG_U8, 0, 255, 0, 2, 2};
            int buiten; double ff = rg_as_bron(&a, (uint8_t)in_beeld, &buiten);
            unsigned in_game = (unsigned)rg_as_waarde(&a, ff, buiten);
            if (in_game != in_beeld || buiten != out) { if (rond++ < 5) printf("FAIL: f=%.4f buiten=%d: beeld %u -> game %u (buiten gelezen als %d)\n", f, out, in_beeld, in_game, buiten); }
        }
    CHECK(rond == 0, "%d waarden veranderen onderweg van het beeld naar de game", rond);
}

/* 6. Andere soorten bestemming: 16 en 32 bits, met teken, en een kommagetal. */
static void test_soorten(void) {
    struct rg_tabel tb; char fout[256]; struct rg_staat st; memset(&st, 0, sizeof st); uint8_t b[RG_BEELD];
    leeg();
    CHECK(lees("as bron=12 adres=0x1300 soort=u16 min=0 max=1000\n"
               "as bron=13 adres=0x1310 soort=u32 min=0 max=65535 inverteer=ja\n"
               "as bron=14 adres=0x1320 soort=i32 min=-512 max=511 marge=1\n"
               "as bron=15 adres=0x1330 soort=f32 min=0 max=1 bron_marge=0\n", &tb, fout), "tabel afgekeurd: %s", fout);
    beeld(b, 0); b[12] = 100; b[13] = 255; b[14] = 2; b[15] = 51;
    memset(geheugen + 0x1300, 0xcc, 0x40);
    rg_stap(&tb, &st, b, 64, 1000, 0);
    uint16_t u16; uint32_t u32; int32_t i32; float f32;
    memcpy(&u16, geheugen + 0x1300, 2); memcpy(&u32, geheugen + 0x1310, 4); memcpy(&i32, geheugen + 0x1320, 4); memcpy(&f32, geheugen + 0x1330, 4);
    CHECK(u16 == 392 && geheugen[0x1302] == 0xcc, "u16: %u (392 = round(100/255*1000)), buurbyte %02x", u16, geheugen[0x1302]);
    CHECK(u32 == 0, "u32 omgekeerd op de rand: %u", u32);
    CHECK(i32 == -504, "i32: %d (-504 = round(-512 + 2/255*1023) = round(-503,98))", i32);
    /* een kommagetal wordt ook eerst afgerond: bij 0..1 is dat 0 of 1. Wie een fractie wil, kiest een groter bereik. */
    CHECK(f32 == 0.0f, "f32 0..1 met byte 51: %f (round(0,2) = 0)", (double)f32);
    b[15] = 200; rg_stap(&tb, &st, b, 64, 1000, 0); memcpy(&f32, geheugen + 0x1330, 4);
    CHECK(f32 == 1.0f, "f32 0..1 met byte 200: %f", (double)f32);
}

/* 6b. Een knop als overgangsteller (zo leest de geweerlaag van een Raw Thrills-game zijn knoppen: een telling die omloopt, bit 0 =
       ingedrukt), vaste waarden die vastgehouden worden, en een as als kommagetal met een eigen bereik. */
static void test_teller_zet_en_schaal(void) {
    struct rg_tabel tb; char fout[256]; struct rg_staat st; memset(&st, 0, sizeof st); uint8_t b[RG_BEELD];
    leeg();
    CHECK(lees("teller bit=7 adres=0x1500 breedte=4 modulo=16\n"
               "teller bit=8 adres=0x1504 breedte=4 modulo=16\n"
               "teller bit=9 adres=0x1508 breedte=1 modulo=4 inverteer=ja\n"
               "zet adres=0x1510 breedte=4 waarde=1\n"
               "zet adres=0x1514 breedte=4 waarde=3\n"
               "zet adres=0x1518 breedte=1 waarde=0x7f\n", &tb, fout), "tabel afgekeurd: %s", fout);
    CHECK(tb.nsw == NSTANDAARD && tb.nteller == 3 && tb.nzet == 3 && tb.nbit == 0 && tb.nas == 0, "%d schakelaars, %d tellers, %d vaste waarden", tb.nsw, tb.nteller, tb.nzet);
    uint32_t w; geheugen[0x1509] = 0xee; geheugen[0x1519] = 0xdd;
    /* geen knop: de tellers blijven 0 (even = los); de omgekeerde gaat een stap (bron uit = ingedrukt); de vaste waarden staan er */
    beeld(b, 0); rg_stap(&tb, &st, b, 64, 1000, 0);
    memcpy(&w, geheugen + 0x1500, 4); CHECK(w == 0, "teller zonder indruk: %u", w);
    CHECK(geheugen[0x1508] == 1 && geheugen[0x1509] == 0xee, "omgekeerde teller: %u, buurbyte %02x", geheugen[0x1508], geheugen[0x1509]);
    memcpy(&w, geheugen + 0x1510, 4); CHECK(w == 1, "vaste waarde 1: %u", w);
    memcpy(&w, geheugen + 0x1514, 4); CHECK(w == 3, "vaste waarde 3: %u", w);
    CHECK(geheugen[0x1518] == 0x7f && geheugen[0x1519] == 0xdd, "vaste byte: %02x, buurbyte %02x", geheugen[0x1518], geheugen[0x1519]);
    /* indrukken = een stap (oneven), vasthouden = geen stap, loslaten = een stap (even) */
    beeld(b, 1u << 7); rg_stap(&tb, &st, b, 64, 1010, 0); memcpy(&w, geheugen + 0x1500, 4); CHECK(w == 1, "na indrukken: %u", w);
    for (int i = 0; i < 500; i++) rg_stap(&tb, &st, b, 64, 1020 + i, 0);
    memcpy(&w, geheugen + 0x1500, 4); CHECK(w == 1, "vasthouden telt niet door: %u", w);
    memcpy(&w, geheugen + 0x1504, 4); CHECK(w == 0, "een andere knop telde mee: %u", w);
    beeld(b, 0); rg_stap(&tb, &st, b, 64, 2000, 0); memcpy(&w, geheugen + 0x1500, 4); CHECK(w == 2, "na loslaten: %u", w);
    /* acht keer indrukken en loslaten: de teller loopt om bij 16 en bit 0 blijft kloppen (2 + 16 = 18 -> 2) */
    for (int i = 0; i < 8; i++) { beeld(b, 1u << 7); rg_stap(&tb, &st, b, 64, 3000 + 2 * i, 0); memcpy(&w, geheugen + 0x1500, 4); CHECK((w & 1) == 1 && w < 16, "ingedrukt hoort oneven en < 16: %u", w);
                                  beeld(b, 0); rg_stap(&tb, &st, b, 64, 3001 + 2 * i, 0); memcpy(&w, geheugen + 0x1500, 4); CHECK((w & 1) == 0 && w < 16, "los hoort even en < 16: %u", w); }
    CHECK(w == 2, "na 8 indrukken is de teller een keer omgelopen: %u (verwacht 2)", w);
    /* uit de pas (iemand schreef erin, of de game maakte zijn toestand leeg): een stap en het klopt weer; een waarde boven modulo ook */
    w = 7; memcpy(geheugen + 0x1500, &w, 4); beeld(b, 0); rg_stap(&tb, &st, b, 64, 4000, 0);
    memcpy(&w, geheugen + 0x1500, 4); CHECK(w == 8, "oneven terwijl de knop los is: een stap naar 8, niet %u", w);
    w = 0x1235; memcpy(geheugen + 0x1500, &w, 4); rg_stap(&tb, &st, b, 64, 4001, 0);
    memcpy(&w, geheugen + 0x1500, 4); CHECK(w == (0x1236 % 16), "een waarde boven modulo: %u", w);
    /* de game maakt een vaste waarde leeg: de hulp zet hem terug */
    w = 0; memcpy(geheugen + 0x1510, &w, 4); rg_stap(&tb, &st, b, 64, 4002, 0);
    memcpy(&w, geheugen + 0x1510, 4); CHECK(w == 1, "een vaste waarde wordt niet vastgehouden: %u", w);

    /* een as als kommagetal: na de rekenregel (afronden, inverteren, klemmen) maal schaal plus plus; buiten beeld een vaste waarde */
    leeg(); memset(&st, 0, sizeof st);
    CHECK(lees("as bron=12 adres=0x1600 soort=f32 marge=2 schaal=0.00392156862745098\n"                 /* 0..1 */
               "as bron=13 adres=0x1604 soort=f32 marge=2 schaal=0.00784313725490196 plus=-1 buiten=-1\n" /* -1..1, buiten beeld -1 */
               "as bron=14 adres=0x1608 soort=f32 marge=2 inverteer=ja schaal=4 plus=10\n", &tb, fout), "tabel afgekeurd: %s", fout);
    float f0, f1, f2;
    beeld(b, 0); b[12] = 102; b[13] = 128; b[14] = 100; rg_stap(&tb, &st, b, 64, 1000, 0);
    memcpy(&f0, geheugen + 0x1600, 4); memcpy(&f1, geheugen + 0x1604, 4); memcpy(&f2, geheugen + 0x1608, 4);
    CHECK(f0 > 0.3999f && f0 < 0.4001f, "0..1: byte 102 -> %f (0,4)", (double)f0);
    CHECK(f1 > 0.0038f && f1 < 0.0040f, "-1..1: byte 128 -> %f (128*2/255-1 = 0,0039)", (double)f1);
    CHECK(f2 == 630.0f, "eerst inverteren dan schalen: byte 100 -> 155 -> 155*4+10 = %f", (double)f2);
    b[12] = 2; b[13] = 253; rg_stap(&tb, &st, b, 64, 1000, 0); memcpy(&f0, geheugen + 0x1600, 4); memcpy(&f1, geheugen + 0x1604, 4);
    CHECK(f0 > 0.0078f && f0 < 0.0079f && f1 > 0.984f && f1 < 0.985f, "de rand van het scherm: %f en %f", (double)f0, (double)f1);
    b[12] = 0; b[13] = 255; rg_stap(&tb, &st, b, 64, 1000, 0); memcpy(&f0, geheugen + 0x1600, 4); memcpy(&f1, geheugen + 0x1604, 4);
    CHECK(f0 == 0.0f, "buiten beeld zonder 'buiten=': de uiterste waarde, %f", (double)f0);
    CHECK(f1 == -1.0f, "buiten beeld met 'buiten=-1': %f", (double)f1);
    /* zonder schaal/plus/buiten is een as precies wat hij was */
    CHECK(lees("as bron=12 adres=0x1600 soort=u8 marge=2\n", &tb, fout) && !tb.as[0].geschaald && !tb.as[0].heeft_buiten, "een gewone as kreeg een schaal: %s", fout);
}

/* 7. Een tabel die niet klopt wordt afgekeurd, met de reden; en een andere build krijgt niets. */
static void test_afkeuren(void) {
    struct rg_tabel tb; char fout[256];
    struct { const char *tekst, *reden; } slecht[] = {
        {"trekker bit=7 bord=0 k=9\n", "onbekend woord 'trekker'"},
        {"schakelaar bit=7 bord=0 k=9\n", "'puls' ontbreekt"},
        {"schakelaar bit=7 bord=0 k=negen puls=0\n", "'k' ontbreekt of is geen getal"},
        {"schakelaar bit=40 bord=0 k=9 puls=0\n", "bit 0-31"},
        {"schakelaar bit=7 bord=0 k=9 puls=0 kleur=rood\n", "onbekende sleutel 'kleur'"},
        {"schakelaar bit=7 bord=0 k=9 puls\n", "sleutel=waarde"},
        {"bit bit=7 adres=0x1100 breedte=3 doelbit=0\n", "breedte 1, 2 of 4"},
        {"bit bit=7 adres=0x1100 breedte=1 doelbit=8\n", "doelbit binnen die breedte"},
        {"bit bit=7 adres=hier breedte=1 doelbit=0\n", "'adres' ontbreekt of is geen adres"},
        {"as bron=12 adres=0x1200 soort=u64\n", "'soort' is u8"},
        {"as bron=64 adres=0x1200\n", "bron 0-63"},
        {"as bron=12 adres=0x1200 min=10 max=10\n", "max groter dan min"},
        {"as bron=12 adres=0x1200 marge=128\n", "marge kleiner dan de helft"},
        {"as bron=12 adres=0x1200 inverteer=misschien\n", "'inverteer' is ja of nee"},
        {"as bron=12\n", "'adres' ontbreekt"},
        {"build getter=0x61e890\n", "'signatuur' ontbreekt"},
        {"build getter=0x61e890 signatuur=85f\n", "geen reeks hex-bytes"},
        {"tabel basis=0x180ae80 eerste=0xaea0\n", "'per_bord' ontbreekt"},
        {"beeld bits=62\n", "'bits' moet 0 tot 60"},
        {"as bron=12 adres=0x7f0000000000\n", "kan niet geschreven worden"},
        {"as bron=12 adres=0x1200 schaal=0\n", "'schaal' is geen getal of nul"},
        {"as bron=12 adres=0x1200 buiten=ver\n", "'buiten' is geen getal"},
        {"teller bit=7 adres=0x1500 breedte=4\n", "'modulo' ontbreekt"},
        {"teller bit=7 adres=0x1500 breedte=4 modulo=15\n", "modulo even"},
        {"teller bit=7 adres=0x1500 breedte=1 modulo=512\n", "passend in de breedte"},
        {"teller bit=7 adres=0x7f0000000000 breedte=4 modulo=16\n", "kan niet geschreven worden"},
        {"zet adres=0x1510 breedte=4\n", "'waarde' ontbreekt"},
        {"zet adres=0x1510 breedte=1 waarde=256\n", "de waarde moet erin passen"},
        {"zet adres=0x7f0000000000 breedte=4 waarde=1\n", "kan niet geschreven worden"},
        {"bit bit=7 adres=0x7f0000000000 breedte=4 doelbit=0\n", "kan niet geschreven worden"},
        {"tabel basis=0x7f0000000000 eerste=0 per_bord=0x2200 per_schakelaar=0x50\n", "kan niet geschreven worden"},
    };
    for (unsigned i = 0; i < sizeof slecht / sizeof slecht[0]; i++) {
        int ok = lees(slecht[i].tekst, &tb, fout);
        CHECK(!ok, "niet afgekeurd: %s", slecht[i].tekst);
        CHECK(strstr(fout, slecht[i].reden) != 0, "reden '%s' ontbreekt in '%s' (%s)", slecht[i].reden, fout, slecht[i].tekst);
    }
    /* leeg en alleen commentaar: de ingebouwde standaard */
    CHECK(lees("", &tb, fout) && tb.nsw == NSTANDAARD && tb.nas == 0, "een lege tabel: %s", fout);
    CHECK(lees("# niets\n\n   ; ook niets\n", &tb, fout) && tb.nsw == NSTANDAARD, "alleen commentaar: %s", fout);
    /* build en tabel vervangen de ingebouwde adressen */
    CHECK(lees("build getter=0x2000 signatuur=DEADbeef01\ntabel basis=0x3000 eerste=0x100 per_bord=0x400 per_schakelaar=0x20\nbeeld bits=4\n", &tb, fout), "build/tabel: %s", fout);
    CHECK(tb.getter == 0x2000 && tb.nsig == 5 && tb.sig[0] == 0xde && tb.sig[4] == 0x01 && tb.basis == 0x3000 && tb.eerste == 0x100 && tb.per_bord == 0x400 && tb.per_schakelaar == 0x20 && tb.beeld_bits == 4, "build/tabel verkeerd gelezen");
    /* de build-controle */
    rg_tabel_standaard(&tb); zet_getter();
    CHECK(rg_build_klopt(&tb), "de bekende build wordt niet herkend");
    geheugen[GETTER_ADDR + 3] ^= 0xff;
    CHECK(!rg_build_klopt(&tb), "een andere build wordt voor de bekende aangezien");
    zet_getter();
    /* beeld bits=4: het bitveld op een andere plek in het beeld */
    struct rg_staat st; memset(&st, 0, sizeof st); uint8_t b[RG_BEELD]; leeg();
    CHECK(lees("beeld bits=4\nschakelaar bit=7 bord=0 k=9 puls=0\n", &tb, fout), "beeld: %s", fout);
    memset(b, 0, sizeof b); b[4] = 0x80; rg_stap(&tb, &st, b, 64, 1000, 0);
    CHECK(ing(0, 9)[3] == 1, "het bitveld op offset 4 werd niet gelezen");
}

/* 8. Alles samen, zoals een tabel voor een spel met vier geweren eruit ziet: de ingebouwde schakelaars blijven, de knoppen en
      de assen komen erbij. */
static void test_alles_samen(void) {
    struct rg_tabel tb; char fout[256]; struct rg_staat st; memset(&st, 0, sizeof st); uint8_t b[RG_BEELD];
    leeg();
    char tekst[4096] = ""; int n = 0;
    for (int p = 0; p < 4; p++) {
        n += snprintf(tekst + n, sizeof tekst - n, "bit bit=%d adres=0x%x breedte=4 doelbit=0\nbit bit=%d adres=0x%x breedte=4 doelbit=1\nbit bit=%d adres=0x%x breedte=4 doelbit=2\nbit bit=%d adres=0x%x breedte=4 doelbit=3\n",
                      7 + 5 * p, 0x1400 + 0x10 * p, 8 + 5 * p, 0x1400 + 0x10 * p, 9 + 5 * p, 0x1400 + 0x10 * p, 10 + 5 * p, 0x1400 + 0x10 * p);
        n += snprintf(tekst + n, sizeof tekst - n, "as bron=%d adres=0x%x marge=2\nas bron=%d adres=0x%x marge=2\n", 12 + 2 * p, 0x1404 + 0x10 * p, 13 + 2 * p, 0x1405 + 0x10 * p);
    }
    CHECK(lees(tekst, &tb, fout), "tabel afgekeurd: %s", fout);
    CHECK(tb.nsw == NSTANDAARD && tb.nbit == 16 && tb.nas == 8, "%d schakelaars, %d bits, %d assen", tb.nsw, tb.nbit, tb.nas);
    /* speler 3 schiet en mikt rechtsonder; speler 1 drukt start; speler 4 mikt buiten beeld */
    beeld(b, (1u << 17) | (1u << 6)); b[16] = 250; b[17] = 240; b[18] = 0; b[19] = 255; b[12] = 2; b[13] = 2;
    rg_stap(&tb, &st, b, 64, 1000, 0);
    uint32_t w; memcpy(&w, geheugen + 0x1420, 4);
    CHECK(w == 1, "knoppen van speler 3: %08x", w);
    for (int p = 0; p < 4; p++) if (p != 2) { memcpy(&w, geheugen + 0x1400 + 0x10 * p, 4); CHECK(w == 0, "knoppen van speler %d: %08x", p + 1, w); }
    CHECK(geheugen[0x1424] == 250 && geheugen[0x1425] == 240, "aim van speler 3: %u,%u", geheugen[0x1424], geheugen[0x1425]);
    CHECK(geheugen[0x1434] == 0 && geheugen[0x1435] == 255, "speler 4 buiten beeld: %u,%u", geheugen[0x1434], geheugen[0x1435]);
    CHECK(geheugen[0x1404] == 2 && geheugen[0x1405] == 2, "speler 1 linksboven in het scherm: %u,%u", geheugen[0x1404], geheugen[0x1405]);
    CHECK(ing(0, 0)[3] == 1 && ing(0, 0)[6] == 1, "de ingebouwde start van speler 1 werkt niet meer naast de tabel");
}

/* 9. Het voorbeeld van de tabel (tools/rt-input-shim/rt-invoer.voorbeeld.ini) is zelf een geldige tabel, en geeft precies de
      ingebouwde standaard: wie het naar een patchmap kopieert verandert daarmee nog niets. */
static void test_het_voorbeeld(void) {
#ifdef RG_VOORBEELD
    static char tekst[65536]; char fout[256]; struct rg_tabel tb, std;
    FILE *f = fopen(RG_VOORBEELD, "r");
    CHECK(f != 0, "het voorbeeld %s is niet te lezen", RG_VOORBEELD);
    if (!f) return;
    size_t n = fread(tekst, 1, sizeof tekst - 1, f); fclose(f); tekst[n] = 0;
    CHECK(lees(tekst, &tb, fout), "het voorbeeld wordt afgekeurd: %s", fout);
    rg_tabel_standaard(&std);
    CHECK(tb.nsw == std.nsw && tb.nbit == 0 && tb.nas == 0, "het voorbeeld: %d schakelaars, %d bits, %d assen", tb.nsw, tb.nbit, tb.nas);
    CHECK(memcmp(&tb, &std, sizeof tb) == 0, "het voorbeeld geeft een andere tabel dan de ingebouwde standaard");
#else
    printf("let op: het voorbeeld is niet meegetest (RG_VOORBEELD ontbreekt)\n");
#endif
}

/* 10. Welke tabel de hulp kiest: RG_RT_TABLE, en een tabel die niet klopt of niet te lezen is geeft GEEN invoer (0), niet stil
       de standaard. Zonder tabel de ingebouwde standaard. */
static void test_tabel_kiezen(const char *tmp) {
    struct rg_tabel tb; char pad[512];
    unsetenv("RG_RT_TABLE");
    CHECK(rg_tabel_kies(&tb) == 1 && tb.nsw == OUD_NMAP && tb.nas == 0, "zonder tabel hoort de ingebouwde standaard te gelden");
    snprintf(pad, sizeof pad, "%s/goed.ini", tmp);
    FILE *f = fopen(pad, "w"); fputs("as bron=12 adres=0x1200 marge=2\nschakelaar bit=7 bord=0 k=9 puls=0\n", f); fclose(f);
    setenv("RG_RT_TABLE", pad, 1);
    CHECK(rg_tabel_kies(&tb) == 1 && tb.nsw == 1 && tb.nas == 1, "RG_RT_TABLE wordt niet gelezen: %d schakelaars, %d assen", tb.nsw, tb.nas);
    snprintf(pad, sizeof pad, "%s/fout.ini", tmp);
    f = fopen(pad, "w"); fputs("as bron=12 adres=0x1200 marge=2\ntrekker bit=7\n", f); fclose(f);
    setenv("RG_RT_TABLE", pad, 1);
    fprintf(stderr, "(de volgende twee FOUT-regels horen bij de test)\n");
    CHECK(rg_tabel_kies(&tb) == 0, "een tabel die niet klopt hoort GEEN invoer te geven");
    snprintf(pad, sizeof pad, "%s/bestaat-niet.ini", tmp);
    setenv("RG_RT_TABLE", pad, 1);
    CHECK(rg_tabel_kies(&tb) == 0, "een tabel die er niet is hoort GEEN invoer te geven (niet stil de standaard)");
    unsetenv("RG_RT_TABLE");
    /* een getter-adres dat in dit proces niet bestaat: een andere build, geen crash */
    rg_tabel_standaard(&tb); tb.getter = 0x7f0000000000UL;
    CHECK(!rg_build_klopt(&tb), "een getter-adres dat niet bestaat hoort 'andere build' te zijn");
}

int main(int argc, char **argv) {
    geheugen = mmap(0, GROOTTE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (geheugen == MAP_FAILED) { printf("geen geheugen\n"); return 2; }
    rg_mem_basis = (uintptr_t)geheugen;
    zet_getter();
    test_zonder_tabel_is_het_oude_gedrag();
    test_knop_blijft_ingedrukt();
    test_losse_bits();
    test_assen();
    test_rekenregel_is_die_van_de_brug();
    test_soorten();
    test_teller_zet_en_schaal();
    test_afkeuren();
    test_alles_samen();
    test_het_voorbeeld();
    test_tabel_kiezen(argc > 1 ? argv[1] : "/tmp");
    if (fouten) { printf("%d fout(en)\n", fouten); return 1; }
    printf("ok: schakelaars (oud gedrag), geweerknoppen, losse bits, tellers, vaste waarden, assen met de regel van de IO-brug, en afkeuren\n");
    return 0;
}
