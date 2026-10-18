/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Eenheidstest van tools/rg-ptt/ptt_board.h: het geweerbord van Pull The Trigger (wsc32.dll) uit het gedeelde geheugen, zonder Windows. */
#define PTT_BOARD_TEST_HELPERS 1
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ptt_board.h"

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s (regel %d)\n", msg, __LINE__); fails++; } } while (0)

static void shm_set(unsigned char *shm, unsigned ctl, unsigned char x, unsigned char y)
{
    memset(shm, 0, PTT_SHM_SIZE);
    shm[8] = ctl & 0xff; shm[9] = (ctl >> 8) & 0xff; shm[10] = (ctl >> 16) & 0xff; shm[11] = (ctl >> 24) & 0xff;
    shm[PTT_OFF_P1X] = x; shm[PTT_OFF_P1Y] = y;
}

/* Leest alles uit de ring en controleert dat de dispatch van het spel (0x47da40) elk pakket zou accepteren. */
static unsigned drain(ptt_board *b, unsigned char *out, unsigned max, int *valid)
{
    unsigned n = ptt_board_rx_get(b, out, max), i = 0;
    *valid = 1;
    while (i < n) {
        unsigned l;
        if (n - i < 2) { *valid = 0; break; }              /* het spel wacht op minstens 2 bytes (0x47d932) */
        l = ptt_game_packet_len(out[i], out[i + 1]);
        if (!l || i + l > n) { *valid = 0; break; }
        i += l;
    }
    return n;
}

int main(void)
{
    ptt_board b;
    unsigned char shm[PTT_SHM_SIZE], buf[256];
    unsigned n, i;
    int valid;
    unsigned char b2, b3;

    /* 1. aim -> pakketbytes */
    CHECK(ptt_aim_bytes(100, 50, &b2, &b3) == 1 && b2 == 100 && b3 == 205, "y wordt 255 - y (spel doet 0xff - byte3)");
    CHECK(ptt_aim_bytes(0, 100, &b2, &b3) == 0 && b2 == 0 && b3 == 0, "x = 0 is buiten beeld -> (0,0)");
    CHECK(ptt_aim_bytes(255, 100, &b2, &b3) == 0 && b2 == 0 && b3 == 0, "x = 255 is buiten beeld");
    CHECK(ptt_aim_bytes(100, 1, &b2, &b3) == 0, "y = 1 is buiten beeld");
    CHECK(ptt_aim_bytes(2, 253, &b2, &b3) == 1 && b2 == 2 && b3 == 2, "binnen de offscreen-marge (2..253)");

    /* 2. eerste monster geeft niets (ook niet bij een vastgehouden trekker), een stilstaande shm ook niet */
    ptt_board_init(&b);
    shm_set(shm, PTT_BIT_P1_TRIGGER, 128, 128);
    ptt_board_poll(&b, shm);
    CHECK(ptt_board_rx_count(&b) == 0, "eerste monster: geen pakketten");
    ptt_board_poll(&b, shm); ptt_board_poll(&b, shm);
    CHECK(ptt_board_rx_count(&b) == 0, "stilstaande shm (vastgehouden trekker): geen pakketten (muismodus blijft vrij)");

    /* 3. richting verandert: een X-pakket */
    shm_set(shm, PTT_BIT_P1_TRIGGER, 140, 90);
    ptt_board_poll(&b, shm);
    n = drain(&b, buf, sizeof buf, &valid);
    CHECK(n == 4 && valid && buf[0] == 0x58 && buf[1] == 4 && buf[2] == 140 && buf[3] == 165, "X-pakket [58 04 140 165]");

    /* 4. trekker omlaag en weer omhoog: F, X, en F alleen op de opgaande rand */
    shm_set(shm, 0, 140, 90); ptt_board_poll(&b, shm);
    CHECK(ptt_board_rx_count(&b) == 0, "trekker los: niets");
    shm_set(shm, PTT_BIT_P1_TRIGGER, 140, 90); ptt_board_poll(&b, shm);
    n = drain(&b, buf, sizeof buf, &valid);
    CHECK(n == 6 && valid && buf[0] == 0x46 && buf[1] == 2 && buf[2] == 0x58 && buf[3] == 4 && buf[4] == 140 && buf[5] == 165,
          "trekker: [46 02] gevolgd door [58 04 ..] (het schot gebeurt bij het X-pakket)");
    CHECK(b.n_pkt_f == 1, "een F");

    /* 5. trekker met gelijktijdige beweging: X, F, X */
    shm_set(shm, 0, 140, 90); ptt_board_poll(&b, shm); ptt_board_rx_clear(&b);
    shm_set(shm, PTT_BIT_P1_TRIGGER, 160, 70); ptt_board_poll(&b, shm);
    n = drain(&b, buf, sizeof buf, &valid);
    CHECK(n == 10 && valid && buf[0] == 0x58 && buf[4] == 0x46 && buf[6] == 0x58 && buf[8] == 160 && buf[9] == 185, "X F X bij trekker en beweging");

    /* 6. herladen: 'd' op de rand, niet bij loslaten */
    shm_set(shm, 0, 160, 70); ptt_board_poll(&b, shm); ptt_board_rx_clear(&b);
    shm_set(shm, PTT_BIT_P1_RELOAD, 160, 70); ptt_board_poll(&b, shm);
    n = drain(&b, buf, sizeof buf, &valid);
    CHECK(n == 3 && valid && buf[0] == 0x64 && buf[1] == 3 && buf[2] == 1, "d-pakket [64 03 01]");
    shm_set(shm, 0, 160, 70); ptt_board_poll(&b, shm);
    CHECK(ptt_board_rx_count(&b) == 0, "loslaten van herladen: niets");

    /* 7. buiten beeld: (0,0) */
    shm_set(shm, 0, 0, 0); ptt_board_poll(&b, shm);
    n = drain(&b, buf, sizeof buf, &valid);
    CHECK(n == 4 && valid && buf[2] == 0 && buf[3] == 0, "buiten beeld: X met (0,0)");

    /* 7b. muismodus: de richting in de shm staat stil -> een trekkerdruk geeft GEEN F/X (de muis doet het schot), wel een teller */
    ptt_board_init(&b);
    shm_set(shm, 0, 128, 128); ptt_board_poll(&b, shm);
    shm_set(shm, PTT_BIT_P1_TRIGGER, 128, 128); ptt_board_poll(&b, shm);
    shm_set(shm, 0, 128, 128); ptt_board_poll(&b, shm);
    shm_set(shm, PTT_BIT_P1_TRIGGER, 128, 128); ptt_board_poll(&b, shm);
    CHECK(ptt_board_rx_count(&b) == 0 && b.n_f_skipped == 2, "stilstaande richting: trekker wordt overgeslagen");
    shm_set(shm, 0, 130, 128); ptt_board_poll(&b, shm); ptt_board_rx_clear(&b);
    shm_set(shm, PTT_BIT_P1_TRIGGER, 130, 128); ptt_board_poll(&b, shm);
    CHECK(ptt_board_rx_count(&b) == 6 && b.n_pkt_f == 1, "zodra de richting beweegt wel: F + X");

    /* 7c. munt: randen worden geteld en afgehaald */
    ptt_board_init(&b);
    shm_set(shm, 0, 100, 100); ptt_board_poll(&b, shm);
    shm_set(shm, PTT_BIT_COIN1, 100, 100); ptt_board_poll(&b, shm);
    CHECK(ptt_board_take_rises(&b) == PTT_BIT_COIN1, "munt: opgaande rand");
    shm_set(shm, PTT_BIT_COIN1, 100, 100); ptt_board_poll(&b, shm);
    CHECK(ptt_board_take_rises(&b) == 0, "munt vastgehouden: geen tweede rand");
    shm_set(shm, 0, 100, 100); ptt_board_poll(&b, shm);
    shm_set(shm, PTT_BIT_COIN1 | PTT_BIT_P1_START | PTT_BIT_TEST, 100, 100); ptt_board_poll(&b, shm);
    CHECK(ptt_board_take_rises(&b) == (PTT_BIT_COIN1 | PTT_BIT_P1_START | PTT_BIT_TEST), "munt, start en test samen");
    CHECK(ptt_board_rx_count(&b) == 0, "munt/start/test geven geen pakket");
    {
        unsigned bits = 0, i2;
        for (i2 = 0; i2 < PTT_KEY_COUNT; i2++) bits |= ptt_key_map[i2].bit;
        CHECK(ptt_key_map[0].bit == PTT_BIT_COIN1 && ptt_key_map[0].vk == 0x2d, "munt = Insert");
        CHECK(bits == (PTT_BIT_COIN1 | PTT_BIT_P1_START | PTT_BIT_TEST | PTT_BIT_SERVICE), "toetsentabel");
        for (i2 = 0; i2 < PTT_KEY_COUNT; i2++) CHECK(ptt_key_map[i2].vk != 0x2e, "nooit Del sturen");
    }

    /* 8. commando's van het spel: 'TTTTT' -> antwoord t,3,1; '+++++' -> t,3,0; ook in stukken en met rommel ervoor */
    ptt_board_init(&b);
    CHECK(ptt_board_tx(&b, (const unsigned char *)"TTTTT", 5) == 1, "TTTTT is een commando");
    n = drain(&b, buf, sizeof buf, &valid);
    CHECK(n == 3 && valid && buf[0] == 0x74 && buf[1] == 3 && buf[2] == 1, "antwoord t,3,1");
    CHECK(ptt_board_tx(&b, (const unsigned char *)"+++", 3) == 0, "halve commando: nog niets");
    CHECK(ptt_board_tx(&b, (const unsigned char *)"++", 2) == 1, "tweede helft maakt het af");
    n = drain(&b, buf, sizeof buf, &valid);
    CHECK(n == 3 && buf[2] == 0, "antwoord t,3,0");
    CHECK(ptt_board_tx(&b, (const unsigned char *)"zzTTTTT", 7) == 1 && b.n_junk == 2, "rommel wordt geteld en overgeslagen");
    ptt_board_rx_clear(&b);
    CHECK(ptt_board_tx(&b, (const unsigned char *)"TTGGGGG", 7) == 1 && b.n_cmd[2] == 1 && b.out_state[1] == 1, "onderbroken T, daarna GGGGG");
    CHECK(ptt_board_rx_count(&b) == 0, "G en H krijgen geen antwoord");
    ptt_board_tx(&b, (const unsigned char *)"HHHHHCCCCCBBBBB", 15);
    CHECK(b.n_cmd[3] == 1 && b.n_cmd[5] == 1 && b.n_cmd[4] == 1 && b.out_state[1] == 0 && b.out_state[2] == 1, "H, C, B");

    /* 9. de ring loopt niet over; een pakket past geheel of niet */
    ptt_board_init(&b);
    shm_set(shm, 0, 100, 100); ptt_board_poll(&b, shm);
    for (i = 0; i < 400; i++) { shm_set(shm, 0, (unsigned char)(10 + (i % 200)), (unsigned char)(10 + (i % 100))); ptt_board_poll(&b, shm); }
    CHECK(ptt_board_rx_count(&b) <= PTT_RX_SIZE && ptt_board_rx_count(&b) % 4 == 0, "ring vol: alleen hele pakketten");
    CHECK(b.n_dropped > 0, "weggevallen pakketten worden geteld");
    n = drain(&b, buf, 4, &valid);
    CHECK(n == 4 && valid, "lezen in stukken van 4");

    /* 10. de kalibratie van onze correctionV2.txt: byte -> scherm is x * 640/255 en y * 480/255 op ~1,5 pixel */
    {
        double worst_x = 0, worst_y = 0;
        for (i = 2; i <= 253; i++) {
            double ex = fabs(ptt_calib_screen_x((int)i, 640) - i * 640.0 / 255.0);
            double ey = fabs(ptt_calib_screen_y((int)i, 480) - i * 480.0 / 255.0);
            if (ex > worst_x) worst_x = ex;
            if (ey > worst_y) worst_y = ey;
        }
        printf("kalibratie: grootste afwijking x %.2f px, y %.2f px\n", worst_x, worst_y);
        CHECK(worst_x < 1.6 && worst_y < 1.6, "kalibratie benadert byte -> scherm");
    }

    /* 11. de lengtetabel van het spel voor wat wij sturen */
    CHECK(ptt_game_packet_len('F', 2) == 2 && ptt_game_packet_len('F', 3) == 0, "F heeft lengte 2");
    CHECK(ptt_game_packet_len('X', 4) == 4 && ptt_game_packet_len('d', 3) == 3 && ptt_game_packet_len('t', 3) == 3, "X 4, d 3, t 3");
    CHECK(ptt_card_cts() == 1, "CTS actief");

    if (fails) { printf("%d mislukt\n", fails); return 1; }
    printf("ptt_board_unit: ok\n");
    return 0;
}
