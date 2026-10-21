/* SPDX-License-Identifier: GPL-3.0-or-later
 * See rg_pigs_board.h. No C library needed. */
#include "rg_pigs_board.h"

static uint8_t rotl(uint8_t v, unsigned n) { n &= 7u; return (uint8_t)((v << n) | (v >> ((8u - n) & 7u))); }
static uint8_t rotr(uint8_t v, unsigned n) { n &= 7u; return (uint8_t)((v >> n) | (v << ((8u - n) & 7u))); }

/* 0x4139c0: xor 0x92, seven left rotations, xor 0x35, six right rotations, xor 0xA7, three left rotations, xor key. */
uint8_t rg_pigs_shell_reply_xform(uint8_t v, uint8_t key) {
    v ^= 0x92u;
    v = rotl(v, 7);
    v ^= 0x35u;
    v = rotr(v, 6);
    v ^= 0xA7u;
    v = rotl(v, 3);
    return (uint8_t)(v ^ key);
}

/* 0x413a20: xor key, three right rotations, xor 0xA7, six left rotations, xor 0x35, seven right rotations, xor 0x92. */
uint8_t rg_pigs_shell_unlock_xform(uint8_t v, uint8_t key) {
    v ^= key;
    v = rotr(v, 3);
    v ^= 0xA7u;
    v = rotl(v, 6);
    v ^= 0x35u;
    v = rotr(v, 7);
    return (uint8_t)(v ^ 0x92u);
}

void rg_pigs_board_init(rg_pigs_board *b) {
    uint8_t *p = (uint8_t *)b;
    for (size_t i = 0; i < sizeof *b; i++) p[i] = 0;
}

int rg_pigs_board_output(rg_pigs_board *b, const uint8_t *rep, size_t len) {
    if (!b || !rep || len < 3) return 0;
    const uint8_t data = rep[1], cmd = rep[2];
    uint8_t code;
    uint8_t d0 = 0;
    switch (cmd) {
        case RG_PIGS_CMD_ID1: code = RG_PIGS_REPLY_ID1; d0 = b->id_reply[0]; break;
        case RG_PIGS_CMD_ID2: code = RG_PIGS_REPLY_ID2; d0 = b->id_reply[1]; break;
        case RG_PIGS_CMD_UNLOCK1: code = RG_PIGS_REPLY_UNLOCK1; break;
        case RG_PIGS_CMD_UNLOCK2: code = RG_PIGS_REPLY_UNLOCK2; break;
        case RG_PIGS_CMD_TEST81: code = RG_PIGS_REPLY_TEST81; break;
        default:
            /* The lamp word (0x413fac) uses the same two bytes: low byte, high byte. */
            b->lamps = (uint16_t)(data | (cmd << 8));
            b->lamp_writes++;
            return 0;
    }
    b->reply[0] = code;
    b->reply[1] = d0;
    b->reply[2] = 0;
    b->last_cmd = cmd;
    b->last_data = data;
    b->commands++;
    return 1;
}

static void set_code(uint8_t *rep, int code, int on) {
    if (!on || code < 1 || code > 16) return;
    rep[RG_PIGS_IN_SW0 + (code - 1) / 8] |= (uint8_t)(1u << ((code - 1) % 8));
}

void rg_pigs_board_input(rg_pigs_board *b, const uint8_t *shm, uint8_t *rep) {
    for (unsigned i = 0; i < RG_PIGS_IN_LEN; i++) rep[i] = 0;
    uint32_t ctl = 0;
    if (shm) {
        const uint8_t *c = shm + RG_PIGS_SHM_CONTROL;
        ctl = (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
        rep[RG_PIGS_IN_GUN_X] = shm[RG_PIGS_SHM_P1X];
        rep[RG_PIGS_IN_GUN_Y] = shm[RG_PIGS_SHM_P1Y];
    } else {
        rep[RG_PIGS_IN_GUN_X] = 0x80;
        rep[RG_PIGS_IN_GUN_Y] = 0x80;
    }
#define BIT(n) ((int)((ctl >> (n)) & 1u))
    const int coin = BIT(RG_PIGS_BIT_COIN1);
    set_code(rep, RG_PIGS_CODE_START, BIT(RG_PIGS_BIT_P1_START));
    set_code(rep, RG_PIGS_CODE_SEESAW, BIT(RG_PIGS_BIT_P1_TRIGGER));
    set_code(rep, RG_PIGS_CODE_TEST, BIT(RG_PIGS_BIT_TEST));
    set_code(rep, RG_PIGS_CODE_SERVICE, BIT(RG_PIGS_BIT_SERVICE));
    set_code(rep, RG_PIGS_CODE_COIN, coin);
#undef BIT
    if (b) {
        /* Every rising edge of the coin bit is one coin on the board's counter. */
        if (coin && !b->coin_was_down) b->coins = (uint16_t)(b->coins + 1u);
        b->coin_was_down = coin;
        rep[RG_PIGS_IN_COIN_LO] = (uint8_t)(b->coins & 0xffu);
        rep[RG_PIGS_IN_COIN_HI] = (uint8_t)(b->coins >> 8);
        rep[RG_PIGS_IN_REPLY] = b->reply[0];
        rep[RG_PIGS_IN_REPLY_D0] = b->reply[1];
        rep[RG_PIGS_IN_REPLY_D1] = b->reply[2];
    }
    rep[RG_PIGS_IN_FIRMWARE] = RG_PIGS_FIRMWARE;
}
