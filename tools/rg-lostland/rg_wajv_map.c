/* SPDX-License-Identifier: GPL-3.0-or-later
 * Zie rg_wajv_map.h. Geen C-bibliotheek nodig (de DLL is ffreestanding). */
#include "rg_wajv_map.h"

uint32_t rg_wajv_control(const uint8_t *shm) {
    if (!shm) return 0;
    const uint8_t *c = shm + RG_SHM_CONTROL_OFFSET;
    return (uint32_t)c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
}

static int bit(uint32_t ctl, int b) { return (int)((ctl >> b) & 1u); }

/* Zet knop idx van speler 0 (en de dubbele plek 0x183..0x186 voor idx 0..3). */
static void set_button(uint8_t *blk, int idx, int down) {
    blk[RG_WAJV_OFF_BUTTONS + idx] = (uint8_t)(down ? 1 : 0);
    if (idx >= RG_WAJV_IDX_START && idx <= RG_WAJV_IDX_DOWN) blk[RG_WAJV_OFF_START + idx] = (uint8_t)(down ? 1 : 0);
}

void rg_wajv_fill(uint8_t *block, const uint8_t *shm, rg_wajv_state *st) {
    for (size_t i = 0; i < RG_WAJV_BLOCK_SIZE; i++) block[i] = 0;
    const uint32_t ctl = rg_wajv_control(shm);

    block[RG_WAJV_OFF_TEST] = (uint8_t)bit(ctl, RG_LL_BIT_TEST);
    /* Servicetoets: de JVS-servicebit en het servicepaneel (PUSH2 van speler 1) in een keer. */
    const int service = bit(ctl, RG_LL_BIT_SERVICE);
    set_button(block, RG_WAJV_IDX_SERVICE, service);
    set_button(block, RG_WAJV_IDX_PUSH2, service);
    set_button(block, RG_WAJV_IDX_UP, bit(ctl, RG_LL_BIT_UP));
    set_button(block, RG_WAJV_IDX_DOWN, bit(ctl, RG_LL_BIT_DOWN));
    set_button(block, RG_WAJV_IDX_PUSH1, bit(ctl, RG_LL_BIT_ENTER));
    set_button(block, RG_WAJV_IDX_PUSH3, bit(ctl, RG_LL_BIT_P1_START));
    set_button(block, RG_WAJV_IDX_PUSH4, bit(ctl, RG_LL_BIT_P2_START));

    /* Munt: elke stijgende flank van de muntbit is een munt; de teller loopt door (u16, wikkelt zoals bij een echte teller). */
    if (st) {
        const int coin = bit(ctl, RG_LL_BIT_COIN);
        if (coin && !st->coin_was_down) st->coins = (uint16_t)(st->coins + 1u);
        st->coin_was_down = coin;
        block[RG_WAJV_OFF_COIN0] = (uint8_t)(st->coins & 0xffu);
        block[RG_WAJV_OFF_COIN0 + 1] = (uint8_t)(st->coins >> 8);
    }
    /* 0x2aa (u16), 0x2ac (u32 muntstatus) en de tweede teller 0x2c8..0x2cf blijven 0: geen muntfout. */
}
