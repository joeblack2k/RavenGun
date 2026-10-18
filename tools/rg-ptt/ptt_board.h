/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Het geweerbord van Pull The Trigger zonder bord: de logica van onze wsc32.dll (rg_ptt_wsc32.c), zonder Windows-aanroepen, zodat
 * tests/ptt_board_unit.c hem native kan draaien (patroon: tools/rg-usio/usio_model.h).
 *
 * BRON: alles hieronder is statisch gemeten aan PTW.exe (32-bit, 835.584 bytes; VA's in de commentaren), niet aan de kern van
 * TeknoParrot. Het bord hangt aan een seriele poort (WSC32.dll, poortnummer 0-gebaseerd: 1 = COM2). Bytes die het SPEL STUURT
 * (SioPuts, 0x47dc00/0x47dc80/0x47dd00): steeds 5 gelijke tekens (lengte staat in het object, +8 = 5):
 *     'T' x5 / '+' x5   aan/uit van de eerste uitgang (functie 0x42b0b0; zet [obj+0x6b8] = gewenste stand, [obj+0x6b9] = 0)
 *     'G' x5 / 'H' x5   idem, tweede uitgang  (0x42b0f0)
 *     'B' x5 / 'C' x5   idem, derde uitgang   (0x42b120)
 * Het spel zendt 'T'/'+' opnieuw (0x41ff40 -> 0x42b0b0) zolang het antwoord ontbreekt: [obj+0x6b9] blijft 0 tot een 't'-pakket met
 * de gevraagde stand binnenkomt (handler 0x41efca). Dat antwoord geeft dit model, anders overspoelt het spel de lijn.
 *
 * Bytes die het spel LEEST (dispatch 0x47d8e0, lengtetabel 0x47da40 + 0x47dad8, verwerking in de vensterprocedure 0x41ecb4):
 * pakketten [type][lengte][...] met de lengte in byte 1 (type ' ', '+', 'T' hebben vaste lengte 5, ',' '.' 'B' 'C' 'G' 'H' 'M' lengte 1):
 *     'F' 2   [0x46][0x02]               trekker ingedrukt  (handler 0x41ed0f: zet vlag 0x4c8990; het schot volgt bij het VOLGENDE
 *                                         X-pakket, 0x41ef88, dus na elke 'F' hoort een 'X' met de huidige richting)
 *     'X' 4   [0x58][0x04][px][255-py]   richting; (0,0) = geen richting. Het spel rekent y om met 255 - byte3 (0x41ed3a..0x41ed56)
 *                                         en kalibreert met correctionV2.txt (zie ptt_calib_*)
 *     'd' 3   [0x64][0x03][1]            "herladen/pomp" = dezelfde gebeurtenis als de toets S (0x1adb1, 0x417469 -> 0x42b0f0)
 *     't' 3   [0x74][0x03][stand]        antwoord op 'T'/'+' (zie boven)
 * Alle andere pakketten uit de lengtetabel worden door de vensterprocedure genegeerd; wij sturen ze niet.
 *
 * Het GEHEUGENKAARTPOORT (poort 0, 0x47e140 -> 0x47e0f0: SioReset(0,0x400,0x200), SioFlow('N'), SioBaud(7)) wordt bitsgewijs
 * bediend met SioRTS/SioDTR als klok/data en SioCTS als invoer (0x47f070, 0x47f540). Is CTS altijd actief (1), dan zijn alle
 * gelezen bits 0: twee blokken van 16 bytes nul = "geen kaart" (0x47e1d0 geeft -100 = 0xffffff9c, dat 0x42b380 als "geen kaart,
 * verder" behandelt). Meer is er niet nodig om de poort gezond en leeg te laten lijken.
 */
#ifndef RG_PTT_BOARD_H
#define RG_PTT_BOARD_H

#define PTT_SHM_SIZE 64u
#define PTT_RX_SIZE 1024u

/* tp-shm, indeling RawThrillsGUN (packaging/tp-shm-profiles.json): besturingswoord op 8, assen 1 byte op 12 (P1X) en 16 (P1Y). */
#define PTT_OFF_CONTROL 8u
#define PTT_OFF_P1X 12u
#define PTT_OFF_P1Y 16u
#define PTT_BIT_TEST (1u << 0)
#define PTT_BIT_SERVICE (1u << 1)
#define PTT_BIT_COIN1 (1u << 2)
#define PTT_BIT_P1_START (1u << 4)
#define PTT_BIT_P1_TRIGGER (1u << 5)
#define PTT_BIT_P1_GRENADE (1u << 6)
#define PTT_BIT_P1_RELOAD (1u << 7)

/* Poorten zoals het spel ze opent (0-gebaseerd). */
#define PTT_PORT_CARD 0
#define PTT_PORT_GUN 1

typedef struct {
    unsigned char rx[PTT_RX_SIZE];     /* bytes van het bord naar het spel (ring) */
    unsigned rx_head, rx_count;
    unsigned char cmd[8];              /* deel van een commando van het spel */
    unsigned cmd_n;
    unsigned char out_state[3];        /* laatst gevraagde stand van de uitgangen T / G / B (0 = uit) */
    int have_prev;                     /* is er al een monster van de shm geweest? */
    int aim_live;                      /* heeft de richting in de shm ooit bewogen? (anders: muismodus, de shm staat stil) */
    unsigned rises;                    /* opgaande randen van de bits in ptt_key_map sinds de laatste ptt_board_take_rises */
    unsigned n_f_skipped;              /* trekker-randen zonder levende richting: de muis doet het schot */
    unsigned prev_ctl;
    unsigned char prev_x, prev_y;
    /* tellers (logboek en test) */
    unsigned n_cmd[6];                 /* T + G H B C   (index: 0 T, 1 +, 2 G, 3 H, 4 B, 5 C) */
    unsigned n_junk;                   /* bytes van het spel die geen commando waren */
    unsigned n_pkt_f, n_pkt_x, n_pkt_d, n_pkt_t;
    unsigned n_dropped;                /* pakketten die niet in de ring pasten */
} ptt_board;

static void ptt_board_init(ptt_board *b)
{
    unsigned i;
    unsigned char *p = (unsigned char *)b;
    for (i = 0; i < sizeof *b; i++) p[i] = 0;
}

static unsigned ptt_board_rx_count(const ptt_board *b) { return b->rx_count; }

static void ptt_board_rx_clear(ptt_board *b) { b->rx_head = 0; b->rx_count = 0; }

/* Kopieert maximaal max bytes uit de ring en haalt ze eruit (SioGets). */
static unsigned ptt_board_rx_get(ptt_board *b, unsigned char *dst, unsigned max)
{
    unsigned n = b->rx_count < max ? b->rx_count : max, i;
    for (i = 0; i < n; i++) {
        dst[i] = b->rx[b->rx_head];
        b->rx_head = (b->rx_head + 1) % PTT_RX_SIZE;
    }
    b->rx_count -= n;
    return n;
}

/* Een heel pakket in de ring, of niets (nooit een half pakket). */
static int ptt_board_push(ptt_board *b, const unsigned char *pkt, unsigned n)
{
    unsigned i, t;
    if (b->rx_count + n > PTT_RX_SIZE) { b->n_dropped++; return 0; }
    for (i = 0; i < n; i++) {
        t = (b->rx_head + b->rx_count) % PTT_RX_SIZE;
        b->rx[t] = pkt[i];
        b->rx_count++;
    }
    return 1;
}

static void ptt_board_push_t(ptt_board *b, unsigned char state)
{
    unsigned char p[3];
    p[0] = 0x74; p[1] = 0x03; p[2] = state;
    if (ptt_board_push(b, p, 3)) b->n_pkt_t++;
}

/* Richting zoals het bord ze meldt: shm-assen (0..255, boven/links = 0) -> (byte2, byte3). Een as <= 1 of >= 254 is "buiten beeld"
 * (offscreen-regel van de daemon, packaging/tp-shm-profiles.json) en wordt (0,0): "geen richting" (0x41ed40). */
static int ptt_aim_bytes(unsigned char x, unsigned char y, unsigned char *b2, unsigned char *b3)
{
    if (x <= 1 || x >= 254 || y <= 1 || y >= 254) { *b2 = 0; *b3 = 0; return 0; }
    *b2 = x;
    *b3 = (unsigned char)(255u - y);
    return 1;
}

static void ptt_board_push_aim(ptt_board *b, unsigned char x, unsigned char y)
{
    unsigned char p[4];
    p[0] = 0x58; p[1] = 0x04;
    ptt_aim_bytes(x, y, &p[2], &p[3]);
    if (ptt_board_push(b, p, 4)) b->n_pkt_x++;
}

/* Bytes van het spel naar het bord (SioPuts). Geeft het aantal herkende commando's. */
static unsigned ptt_board_tx(ptt_board *b, const unsigned char *d, unsigned n)
{
    unsigned i, done = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = d[i];
        if (b->cmd_n == 0) {
            if (c == 'T' || c == '+' || c == 'G' || c == 'H' || c == 'B' || c == 'C') b->cmd[b->cmd_n++] = c;
            else b->n_junk++;
            continue;
        }
        if (c != b->cmd[0]) {            /* geen gelijk teken: begin opnieuw (en tel dit teken als begin als het er een kan zijn) */
            b->n_junk += b->cmd_n;
            b->cmd_n = 0;
            i--;
            continue;
        }
        b->cmd[b->cmd_n++] = c;
        if (b->cmd_n == 5) {
            unsigned idx = c == 'T' ? 0u : c == '+' ? 1u : c == 'G' ? 2u : c == 'H' ? 3u : c == 'B' ? 4u : 5u;
            b->n_cmd[idx]++;
            if (idx < 2) { b->out_state[0] = (unsigned char)(idx == 0); ptt_board_push_t(b, b->out_state[0]); }
            else if (idx < 4) b->out_state[1] = (unsigned char)(idx == 2);
            else b->out_state[2] = (unsigned char)(idx == 4);
            b->cmd_n = 0;
            done++;
        }
    }
    return done;
}

/* Eén monster van de shm (64 bytes). De eerste keer zet het alleen de uitgangsstand; daarna komt er een pakket per verandering:
 *   trekker omhoog (rand)  -> 'F' en meteen 'X' (het schot gebeurt bij dat X-pakket)
 *   herladen omhoog (rand) -> 'd'
 *   richting veranderd     -> 'X'
 * Zolang de shm niet verandert (stilstaande daemon, muismodus) komt er niets, zodat de muis het spel niet tegenwerkt. */
static void ptt_board_poll(ptt_board *b, const unsigned char *shm)
{
    unsigned ctl = (unsigned)shm[PTT_OFF_CONTROL] | ((unsigned)shm[PTT_OFF_CONTROL + 1] << 8) | ((unsigned)shm[PTT_OFF_CONTROL + 2] << 16) |
                   ((unsigned)shm[PTT_OFF_CONTROL + 3] << 24);
    unsigned char x = shm[PTT_OFF_P1X], y = shm[PTT_OFF_P1Y];
    unsigned rise;
    int moved;
    if (!b->have_prev) {
        b->have_prev = 1;
        b->prev_ctl = ctl; b->prev_x = x; b->prev_y = y;
        return;
    }
    rise = ctl & ~b->prev_ctl;
    moved = (x != b->prev_x) || (y != b->prev_y);
    if (moved) b->aim_live = 1;
    b->rises |= rise;
    if (moved && !(rise & PTT_BIT_P1_TRIGGER)) ptt_board_push_aim(b, x, y);
    if (rise & PTT_BIT_P1_TRIGGER) {
        /* Staat de richting in de shm stil (muismodus: de daemon zet de trekker, de muis zet de richting), dan zou F + X het schot op het
         * vaste punt van de shm leggen in plaats van onder de muis. Dan doet WM_LBUTTONDOWN (0x41f2aa) het schot en sturen wij niets. */
        if (!b->aim_live) b->n_f_skipped++;
        else {
            unsigned char f[2];
            f[0] = 0x46; f[1] = 0x02;
            if (moved) ptt_board_push_aim(b, x, y);   /* eerst de nieuwe richting, dan de trekker, dan weer de richting voor het schot */
            if (ptt_board_push(b, f, 2)) b->n_pkt_f++;
            ptt_board_push_aim(b, x, y);
        }
    }
    if (rise & PTT_BIT_P1_RELOAD) {
        unsigned char d[3];
        d[0] = 0x64; d[1] = 0x03; d[2] = 0x01;
        if (ptt_board_push(b, d, 3)) b->n_pkt_d++;
    }
    b->prev_ctl = ctl; b->prev_x = x; b->prev_y = y;
}

/* MUNT, START, TEST, SERVICE komen NIET als pakket (alle pakkettypen nagelopen: de vensterprocedure kent alleen F, U/X/x, d, t). Het spel
 * leest ze als TOETSEN in de vensterprocedure (WM_KEYDOWN, tabel op 0x41e889). Gemeten:
 *   Ins  (0x2d)  -> gebeurtenis 0x186af -> 0x42af6e: [spel+0x150] (muntteller) + 1, cijfers bijwerken (0x463910), krediet-geluid;
 *                   bij teller >= [spel+0x120] (coinsetting.txt, "1 CREDIT = n COIN") een krediet  => MUNT
 *   Space(0x20)  -> 0x186a2 (keuzetoets): op de titel zonder krediet verschijnt "INSERT COIN" (tv 2026-10-18 13:17)  => START
 *   Home (0x24)  -> 0x1d4c1: opent <SYSTEM MENU> (tv, zelfde run)                                                  => TEST
 *   End  (0x23)  -> 0x1d4c5: kiest in dat menu (<COIN SETTING> verscheen)                                         => SERVICE (kiezen)
 * NIET sturen: Del (0x2e, 0x1d4c2: het spelvenster verdween van de tv) en PgUp/PgDn (0x1d4c3/4, onbekend).
 * De trekker blijft F/X, herladen 'd' (= S). Het toetsenbord werkt in het spel ook zelf: de DLL stuurt de toets alleen vanuit de shm. */
struct ptt_key { unsigned bit; unsigned vk; const char *name; };
static const struct ptt_key ptt_key_map[] = {
    { PTT_BIT_COIN1, 0x2d, "munt (Ins)" },
    { PTT_BIT_P1_START, 0x20, "start (Space)" },
    { PTT_BIT_TEST, 0x24, "test (Home)" },
    { PTT_BIT_SERVICE, 0x23, "service (End)" },
};
#define PTT_KEY_COUNT (sizeof ptt_key_map / sizeof ptt_key_map[0])
static unsigned ptt_board_take_rises(ptt_board *b) { unsigned n = b->rises; b->rises = 0; return n; }

/* De kaartlezer: CTS is altijd actief (1) -> alle gelezen bits 0 -> "geen kaart". Negatief zou een fout zijn. */
static int ptt_card_cts(void) { return 1; }

#ifdef PTT_BOARD_TEST_HELPERS
/* Hoe lang is een pakket dat het spel accepteert (0x47da40)? type + lengtebyte; 0 = ongeldig (het spel gooit dan een byte weg).
 * fixed5 = de waarde op [obj+8] (5, gezet in 0x47d4d2). Alleen voor de test: bewijst dat onze pakketten door de dispatch komen. */
static unsigned ptt_game_packet_len(unsigned char type, unsigned char len_byte)
{
    switch (type) {
    case ' ': case '+': case 'T': return 5;
    case ',': case '.': case 'B': case 'C': case 'G': case 'H': case 'M': return 1;
    case 'F': case 'I': case 'S': case 'f': case 's': case 'O': case 'o': return len_byte == 2 ? 2 : 0;
    case 'U': case 'X': case 'u': case 'x': return len_byte == 4 ? 4 : 0;
    case 'd': case 'g': case 'm': case 't': return len_byte == 3 ? 3 : 0;
    case 'i': return len_byte == 6 ? 6 : 0;
    default: return 0;
    }
}

/* correctionV2.txt: "640 480 10  x1 y1 x2 y2 x3 y3 x4 y4  offX offY" (0x47c850 en 0x41e789: sscanf "%d %d %d   %d x8   %d %d", 13 getallen).
 * Het spel zet de y-waarden van de bovenrand en de onderrand elk op het gemiddelde (0x47de80..0x47dea9) en rekent (0x47deb0, 0x47df60):
 *   t  = (py - ytop) / (ybot - ytop)
 *   xl = x1 + t (x4 - x1);  xr = x2 + t (x3 - x2)
 *   X  = mx + offX + (px - xl) / (xr - xl) * (W - 2 mx)         mx = W * 10 / 100
 *   Y  = my + offY + t' * (H - 2 my)   (t' = t bij een rechthoek; bij een trapezium met breedtecorrectie)
 * Voor een RECHTHOEK (x1 = x4, x2 = x3) is het lineair. Onze correctionV2.txt gebruikt de rechthoek (26,26)-(230,230) en offsets 1,1:
 * dan is schermpositie = bytewaarde * 640/255 (en * 480/255) op ongeveer 1 pixel na. Alleen voor de test. */
#define PTT_CALIB_X0 26
#define PTT_CALIB_X1 230
#define PTT_CALIB_Y0 26
#define PTT_CALIB_Y1 230
#define PTT_CALIB_OFF 1

static double ptt_calib_screen_x(int px, int W)
{
    double mx = W * 10 / 100;
    return mx + PTT_CALIB_OFF + (double)(px - PTT_CALIB_X0) / (PTT_CALIB_X1 - PTT_CALIB_X0) * (W - 2 * mx);
}

static double ptt_calib_screen_y(int py, int H)
{
    double my = H * 10 / 100;
    return my + PTT_CALIB_OFF + (double)(py - PTT_CALIB_Y0) / (PTT_CALIB_Y1 - PTT_CALIB_Y0) * (H - 2 * my);
}

#endif /* PTT_BOARD_TEST_HELPERS */

#endif
