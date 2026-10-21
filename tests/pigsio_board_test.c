/* SPDX-License-Identifier: GPL-3.0-or-later
 * Test of tools/rg-pigsio/rg_pigs_board.c: the synthetic HID I/O board of Pigs Might Fly's Shell.exe. The shell side is
 * replayed from its own code (addresses in rg_pigs_board.h): the handshake state machine 0x414030, the switch decoding 0x416710
 * and the transforms 0x4139c0/0x413a20 written as the shell's bit loops. Run by tests/pigsio_test.sh. */
#include <stdio.h>
#include <string.h>

#include "rg_pigs_board.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* The shell's rotation loops, literally: "dl = n; do { dl--; rotate once } while (dl)" rotates n times. */
static uint8_t loop_rotl(uint8_t al, int dl) { do { dl--; uint8_t cl = (al & 0x80) ? 1 : 0; al = (uint8_t)((al << 1) | cl); } while (dl); return al; }
static uint8_t loop_rotr(uint8_t al, int dl) { do { dl--; uint8_t cl = (al & 1) ? 0x80 : 0; al = (uint8_t)((al >> 1) | cl); } while (dl); return al; }
static uint8_t shell_4139c0(uint8_t v, uint8_t key) {
    uint8_t al = (uint8_t)(v ^ 0x92);
    al = loop_rotl(al, 7); al ^= 0x35;
    al = loop_rotr(al, 6); al ^= 0xa7;
    al = loop_rotl(al, 3); return (uint8_t)(al ^ key);
}
static uint8_t shell_413a20(uint8_t v, uint8_t key) {
    uint8_t al = (uint8_t)(v ^ key);
    al = loop_rotr(al, 3); al ^= 0xa7;
    al = loop_rotl(al, 6); al ^= 0x35;
    al = loop_rotr(al, 7); return (uint8_t)(al ^ 0x92);
}

/* 0x416710 + 0x416770: is input code c on in this report? */
static int code_on(const uint8_t *rep, int c) { return (rep[6 + (c - 1) / 8] >> ((c - 1) % 8)) & 1; }

static void set_ctl(uint8_t *shm, uint32_t v) {
    shm[8] = (uint8_t)v; shm[9] = (uint8_t)(v >> 8); shm[10] = (uint8_t)(v >> 16); shm[11] = (uint8_t)(v >> 24);
}

/* The shell's handshake (0x414030 states 1..8), against the board. Returns the state reached (9 = "ALL OK") and the two kept
 * bytes (0x48b52c, 0x48b4e0). */
static int shell_handshake(rg_pigs_board *b, uint8_t kept[2]) {
    uint8_t out[3], in[RG_PIGS_IN_LEN];
    uint8_t e1 = 0x74, fe = 0x09;
    out[0] = 0; out[1] = e1; out[2] = 0xe0; rg_pigs_board_output(b, out, 3);
    rg_pigs_board_input(b, NULL, in); if (in[12] != 0xb0) return 3;
    kept[0] = shell_4139c0(in[13], e1);
    out[1] = fe; out[2] = 0xe1; rg_pigs_board_output(b, out, 3);
    rg_pigs_board_input(b, NULL, in); if (in[12] != 0xb1) return 4;
    kept[1] = shell_4139c0(in[13], fe);
    e1 = shell_413a20(shell_413a20(e1, 0x69), 0x46);
    out[1] = e1; out[2] = 0xf0; rg_pigs_board_output(b, out, 3);
    rg_pigs_board_input(b, NULL, in); if (in[12] != 0xb2) return 7;
    fe = shell_413a20(shell_413a20(fe, 0xe9), 0x3b);
    out[1] = fe; out[2] = 0xf1; rg_pigs_board_output(b, out, 3);
    rg_pigs_board_input(b, NULL, in); if (in[12] != 0xb3) return 8;
    return 9;
}

int main(void) {
    rg_pigs_board b;
    uint8_t shm[RG_PIGS_SHM_SIZE], rep[RG_PIGS_IN_LEN];
    memset(shm, 0, sizeof shm);

    /* The ported transforms equal the shell's loops, and each undoes the other, for every byte and key. */
    for (int v = 0; v < 256; v++)
        for (int k = 0; k < 256; k++) {
            if (rg_pigs_shell_reply_xform((uint8_t)v, (uint8_t)k) != shell_4139c0((uint8_t)v, (uint8_t)k)) { CHECK(0); goto done_x; }
            if (rg_pigs_shell_unlock_xform((uint8_t)v, (uint8_t)k) != shell_413a20((uint8_t)v, (uint8_t)k)) { CHECK(0); goto done_x; }
            if (rg_pigs_shell_reply_xform(rg_pigs_shell_unlock_xform((uint8_t)v, (uint8_t)k), (uint8_t)k) != v) { CHECK(0); goto done_x; }
        }
done_x:

    /* Rest: report id 0, all switches off, firmware 1.0 (BCD), coins 0, no reply yet, gun centred without shared memory. */
    rg_pigs_board_init(&b);
    rg_pigs_board_input(&b, NULL, rep);
    CHECK(rep[0] == 0 && rep[6] == 0 && rep[7] == 0 && rep[9] == 0 && rep[10] == 0 && rep[12] == 0);
    CHECK(rep[11] == 0x10);
    CHECK(rep[1] == 0x80 && rep[2] == 0x80);

    /* The handshake reaches ALL OK; the kept bytes are the shell's transform of the configured reply data. */
    uint8_t kept[2] = {0, 0};
    rg_pigs_board_init(&b);
    CHECK(shell_handshake(&b, kept) == 9);
    CHECK(kept[0] == shell_4139c0(0, 0x74) && kept[1] == shell_4139c0(0, 0x09));
    CHECK(b.commands == 4 && b.lamp_writes == 0);
    rg_pigs_board_init(&b);
    b.id_reply[0] = 0x12; b.id_reply[1] = 0xab;
    CHECK(shell_handshake(&b, kept) == 9);
    CHECK(kept[0] == shell_4139c0(0x12, 0x74) && kept[1] == shell_4139c0(0xab, 0x09));

    /* The reply stays in every report until the next command (the shell's main loop polls the same handle). */
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[12] == 0xb3);
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[12] == 0xb3);

    /* A lamp word is not a command: the reply stays, the lamps are kept. */
    uint8_t lamp[3] = {0, 0x0f, 0x00};
    CHECK(rg_pigs_board_output(&b, lamp, 3) == 0);
    CHECK(b.lamps == 0x000f && b.lamp_writes == 1);
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[12] == 0xb3);

    /* The test menu's command 0x81 gets the reply 0x80 the shell compares with (0x410663). */
    uint8_t t81[3] = {0, 1, 0x81};
    CHECK(rg_pigs_board_output(&b, t81, 3) == 1);
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[12] == 0x80);
    /* Too short: ignored. */
    CHECK(rg_pigs_board_output(&b, t81, 2) == 0);

    /* Switches from the shared memory (WartranTroopers bits) to the shell's input codes. */
    rg_pigs_board_init(&b);
    set_ctl(shm, 1u << RG_PIGS_BIT_TEST);
    rg_pigs_board_input(&b, shm, rep);
    CHECK(code_on(rep, 13) && !code_on(rep, 14) && !code_on(rep, 1) && !code_on(rep, 5));
    CHECK(rep[7] == 0x10 && rep[6] == 0);
    set_ctl(shm, 1u << RG_PIGS_BIT_SERVICE);
    rg_pigs_board_input(&b, shm, rep); CHECK(code_on(rep, 14) && rep[7] == 0x20);
    set_ctl(shm, 1u << RG_PIGS_BIT_P1_START);
    rg_pigs_board_input(&b, shm, rep); CHECK(code_on(rep, 1) && rep[6] == 0x01 && rep[7] == 0);
    set_ctl(shm, 1u << RG_PIGS_BIT_P1_TRIGGER);
    rg_pigs_board_input(&b, shm, rep); CHECK(code_on(rep, 5) && rep[6] == 0x10);
    /* Other players' bits do not reach player 1's switches. */
    set_ctl(shm, (1u << 11) | (1u << 12) | (1u << 3));
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[6] == 0 && rep[7] == 0);

    /* Gun: P1X/P1Y raw into bytes 1 and 2. */
    shm[12] = 0x23; shm[13] = 0xd4;
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[1] == 0x23 && rep[2] == 0xd4);

    /* Coin: each rising edge adds one to the u16 counter in bytes 9/10; holding does not count again; COIN INPUT (15) while held. */
    rg_pigs_board_init(&b);
    set_ctl(shm, 0);
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[9] == 0);
    set_ctl(shm, 1u << RG_PIGS_BIT_COIN1);
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[9] == 1 && code_on(rep, 15));
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[9] == 1);
    set_ctl(shm, 0);
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[9] == 1 && !code_on(rep, 15));
    b.coins = 0x00ff;
    set_ctl(shm, 1u << RG_PIGS_BIT_COIN1);
    rg_pigs_board_input(&b, shm, rep); CHECK(rep[9] == 0x00 && rep[10] == 0x01);

    if (fails) { printf("%d failures\n", fails); return 1; }
    return 0;
}
