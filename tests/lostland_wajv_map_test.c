/* SPDX-License-Identifier: GPL-3.0-or-later
 * Test van tools/rg-lostland/rg_wajv_map.c: de afbeelding van het gedeelde geheugen naar het WAJV-invoerblok van Lost Land
 * Adventure. Natief te bouwen: cc -I tools/rg-lostland tests/lostland_wajv_map_test.c tools/rg-lostland/rg_wajv_map.c
 * (tools/rg-lostland/build.sh doet dat en draait hem). Voorgesteld CTest-doel: zie docs/research/losse/LostLandAdventure.md. */
#include <stdio.h>
#include <string.h>

#include "rg_wajv_map.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void set_ctl(uint8_t *shm, uint32_t v) {
    shm[8] = (uint8_t)v; shm[9] = (uint8_t)(v >> 8); shm[10] = (uint8_t)(v >> 16); shm[11] = (uint8_t)(v >> 24);
}

int main(void) {
    uint8_t shm[RG_SHM_SIZE], blk[RG_WAJV_BLOCK_SIZE];
    rg_wajv_state st = {0, 0};
    memset(shm, 0, sizeof shm);

    /* Rest: the board ID at the start (RSLauncher compares 0x2a bytes), the rest zero, no coin error (status dwords 0x2ac and 0x2cc zero). */
    rg_wajv_fill(blk, shm, &st);
    CHECK(sizeof RG_WAJV_BOARD_ID - 1 == RG_WAJV_BOARD_ID_LEN && RG_WAJV_BOARD_ID_LEN == 0x2a);
    CHECK(memcmp(blk, "namco ltd.;NA-JV;Ver4.00;JPN,Multipurpose.", 0x2a) == 0);
    for (size_t i = RG_WAJV_BOARD_ID_LEN; i < RG_WAJV_BLOCK_SIZE; i++) CHECK(blk[i] == 0);

    /* Without shared memory: also rest. */
    rg_wajv_fill(blk, NULL, &st);
    CHECK(memcmp(blk, RG_WAJV_BOARD_ID, RG_WAJV_BOARD_ID_LEN) == 0);
    for (size_t i = RG_WAJV_BOARD_ID_LEN; i < RG_WAJV_BLOCK_SIZE; i++) CHECK(blk[i] == 0);

    /* TEST. */
    set_ctl(shm, 1u << RG_LL_BIT_TEST);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x187] == 1);
    CHECK(blk[0x189] == 0);

    /* SERVICE: JVS-servicebit (0x184 en idx 1 = 0x189) en servicepaneel (PUSH2 = idx 7 = 0x18f). */
    set_ctl(shm, 1u << RG_LL_BIT_SERVICE);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x184] == 1 && blk[0x189] == 1 && blk[0x18f] == 1);
    CHECK(blk[0x187] == 0);

    /* Omhoog/omlaag (testmenu): idx 2 en 3, ook op 0x185/0x186. */
    set_ctl(shm, 1u << RG_LL_BIT_UP);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x185] == 1 && blk[0x18a] == 1 && blk[0x186] == 0 && blk[0x18b] == 0);
    set_ctl(shm, 1u << RG_LL_BIT_DOWN);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x186] == 1 && blk[0x18b] == 1 && blk[0x185] == 0 && blk[0x18a] == 0);

    /* Enter = PUSH1 (idx 6 = 0x18e); start 1 = PUSH3 (idx 8 = 0x190); start 2 = PUSH4 (idx 9 = 0x191). */
    set_ctl(shm, 1u << RG_LL_BIT_ENTER);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x18e] == 1 && blk[0x190] == 0);
    set_ctl(shm, (1u << RG_LL_BIT_P1_START) | (1u << RG_LL_BIT_P2_START));
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x190] == 1 && blk[0x191] == 1 && blk[0x18e] == 0);
    /* De trekkers staan niet in dit blok (RSLauncher leest ze niet). */
    set_ctl(shm, (1u << RG_LL_BIT_P1_TRIGGER) | (1u << RG_LL_BIT_P2_TRIGGER));
    rg_wajv_fill(blk, shm, &st);
    for (size_t i = RG_WAJV_BOARD_ID_LEN; i < RG_WAJV_BLOCK_SIZE; i++) CHECK(blk[i] == 0);

    /* Munt: elke stijgende flank telt, vasthouden niet; de teller blijft staan als de bit weer los is. */
    st.coin_was_down = 0; st.coins = 0;
    set_ctl(shm, 0);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x2a8] == 0);
    set_ctl(shm, 1u << RG_LL_BIT_COIN);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x2a8] == 1 && blk[0x2a9] == 0);
    rg_wajv_fill(blk, shm, &st);   /* vastgehouden: geen tweede munt */
    CHECK(blk[0x2a8] == 1);
    set_ctl(shm, 0);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x2a8] == 1);
    set_ctl(shm, 1u << RG_LL_BIT_COIN);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x2a8] == 2);

    /* The launcher takes coins with WAJVSetOutput (out+0 = u16 to take off): the counter goes down by that, never below 0, and the
     * next fill shows it. Zero or NULL changes nothing. */
    {
        uint8_t out[0x50];
        memset(out, 0, sizeof out);
        rg_wajv_output(out, &st);
        rg_wajv_output(NULL, &st);
        rg_wajv_fill(blk, shm, &st);
        CHECK(blk[0x2a8] == 2);
        out[0] = 2;
        rg_wajv_output(out, &st);
        rg_wajv_fill(blk, shm, &st);
        CHECK(blk[0x2a8] == 0 && blk[0x2a9] == 0);
        out[0] = 5;
        rg_wajv_output(out, &st);
        CHECK(st.coins == 0);
        /* Held coin bit after the take: still no new coin; a new press counts one. */
        set_ctl(shm, 0);
        rg_wajv_fill(blk, shm, &st);
        set_ctl(shm, 1u << RG_LL_BIT_COIN);
        rg_wajv_fill(blk, shm, &st);
        CHECK(blk[0x2a8] == 1);
        out[0] = 1;
        rg_wajv_output(out, &st);
        rg_wajv_fill(blk, shm, &st);
        CHECK(blk[0x2a8] == 0);
        set_ctl(shm, 0);
        st.coins = 300;
        out[0] = 0x2c; out[1] = 0x01;   /* 300, little-endian */
        rg_wajv_output(out, &st);
        CHECK(st.coins == 0);
    }
    /* Statusdwords en tweede teller blijven nul. */
    CHECK(blk[0x2aa] == 0 && blk[0x2ac] == 0 && blk[0x2ad] == 0 && blk[0x2cc] == 0 && blk[0x2c8] == 0 && blk[0x2ca] == 0);

    /* De teller wikkelt als u16. */
    st.coins = 0xffff; st.coin_was_down = 0;
    set_ctl(shm, 0);
    rg_wajv_fill(blk, shm, &st);
    set_ctl(shm, 1u << RG_LL_BIT_COIN);
    rg_wajv_fill(blk, shm, &st);
    CHECK(blk[0x2a8] == 0 && blk[0x2a9] == 0);

    /* Bitveld little-endian over de vier bytes. */
    shm[8] = 0x01; shm[9] = 0x02; shm[10] = 0x03; shm[11] = 0x04;
    CHECK(rg_wajv_control(shm) == 0x04030201u);
    CHECK(rg_wajv_control(NULL) == 0);

    if (fails) { printf("%d mislukt\n", fails); return 1; }
    printf("lostland_wajv_map_test: ok\n");
    return 0;
}
