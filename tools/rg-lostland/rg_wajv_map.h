/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lost Land Adventure (Namco ES3X): van het gedeelde geheugen van de daemon (tp-shm, indeling LostLandAdventures) naar het
 * invoerblok dat wajvio.dll!WAJVGetInput aan RSLauncher.exe geeft.
 *
 * Zuivere logica, geen Windows-aanroepen: dit bestand wordt door de DLL (wajvio_rg.c) en door een eigen test op Linux
 * (tests/lostland_wajv_map_test.c) gebruikt.
 *
 * HERKOMST VAN DE INDELING (statisch gemeten, niets gestart; docs/research/losse/LostLandAdventure.md):
 *   - wajvio.dll 0x180001300 is de omzetting van de JVS-toestand van een knooppunt naar dit blok (0x2ec bytes). Per speler p zet
 *     zij knop idx (0..63) op blok[0x188 + 0x40*p + idx], met idx = bitpositie in het woord van twee JVS-bytes (idx 0 = START,
 *     1 = SERVICE, 2 = OMHOOG, 3 = OMLAAG, 4 = LINKS, 5 = RECHTS, 6 = PUSH1 ... 13 = PUSH8). Het systeembyte (TEST) staat op 0x187,
 *     de bits van START/SERVICE/OMHOOG/OMLAAG van speler 1 staan ook op 0x183..0x186. De muntenteller van sleuf s is een u16 op
 *     0x2a8 + 8*s, daarna een u16 (0) en een u32 met de muntstatus (0 = normaal).
 *   - RSLauncher.exe (0x140062ee0, 0x140063a60..0x140064100) leest alleen speler 0: TEST, SERVICE, OMHOOG, OMLAAG, PUSH1 (invoer/
 *     Enter van het testmenu), PUSH2 (servicepaneel), PUSH3 (start 1), PUSH4 (start 2), PUSH5/6/8/(0x196) (pincode-toetsen),
 *     muntteller 0x2a8/0x2aa en 0x2c8/0x2ca, en de statusdwords 0x2ac en 0x2cc (moeten 0 zijn, anders een muntfout).
 *   - De trekkers en de richtas staan NIET in dit blok: RSLauncher leest ze niet. Ze horen bij het "gun peripheral" van de
 *     game-exe (zie het onderzoeksdocument); de TeknoParrot-kern vult ze uit bytes 12..24 van de gedeelde toestand.
 */
#ifndef RG_WAJV_MAP_H
#define RG_WAJV_MAP_H

#include <stddef.h>
#include <stdint.h>

#define RG_WAJV_BLOCK_SIZE 0x300u /* het echte blok is 0x2ec bytes; de rest blijft nul */
#define RG_SHM_SIZE 64u
#define RG_SHM_CONTROL_OFFSET 8u

/* Bits van het 32-bit bitveld "Control" op offset 8 van de gedeelde toestand (little-endian). Bit 0 en 1 zijn die van
 * TeknoParrot's LostLandPipe; de rest is onze eigen uitbreiding (packaging/tp-shm-profiles.json, docs/research/losse). */
enum {
    RG_LL_BIT_P1_TRIGGER = 0,
    RG_LL_BIT_P2_TRIGGER = 1,
    RG_LL_BIT_SERVICE = 2,
    RG_LL_BIT_COIN = 3,
    RG_LL_BIT_TEST = 4,
    RG_LL_BIT_P1_START = 5,
    RG_LL_BIT_P2_START = 6,
    RG_LL_BIT_UP = 7,
    RG_LL_BIT_DOWN = 8,
    RG_LL_BIT_ENTER = 9
};

/* The board's identification at the start of the block. RSLauncher.exe 0x14006466a..0x140064691 compares the first 0x2a bytes
 * of the WAJVGetInput block with this string (0x1402bda30) and raises error 5 "03-02 FIRMWARE ERROR" (registered as not playable,
 * so the game shows FATAL ERROR) when they differ. Measured 2026-10-21: with a zero block error 5 is in the launcher's error list. */
#define RG_WAJV_BOARD_ID "namco ltd.;NA-JV;Ver4.00;JPN,Multipurpose."
#define RG_WAJV_BOARD_ID_LEN 42u

/* Offsets in het blok (wajvio.dll 0x180001300). */
enum {
    RG_WAJV_OFF_START = 0x183,
    RG_WAJV_OFF_SERVICE = 0x184,
    RG_WAJV_OFF_UP = 0x185,
    RG_WAJV_OFF_DOWN = 0x186,
    RG_WAJV_OFF_TEST = 0x187,
    RG_WAJV_OFF_BUTTONS = 0x188, /* + 0x40 * speler + idx */
    RG_WAJV_OFF_COIN0 = 0x2a8,   /* + 8 * sleuf: u16 teller, u16 0, u32 status */
    RG_WAJV_OFF_AUX0 = 0x2c8     /* + 8 * speler: u16, u16, u32 (tweede teller van RSLauncher; blijft 0) */
};

/* JVS-knopindex (bit in het woord van twee bytes, idx 0 = bit 15). */
enum {
    RG_WAJV_IDX_START = 0,
    RG_WAJV_IDX_SERVICE = 1,
    RG_WAJV_IDX_UP = 2,
    RG_WAJV_IDX_DOWN = 3,
    RG_WAJV_IDX_PUSH1 = 6,
    RG_WAJV_IDX_PUSH2 = 7,
    RG_WAJV_IDX_PUSH3 = 8,
    RG_WAJV_IDX_PUSH4 = 9
};

/* Onthouden tussen twee aanroepen: de vorige muntbit (voor de stijgende flank) en de teller. */
typedef struct {
    int coin_was_down;
    uint16_t coins;
} rg_wajv_state;

/* Vult `block` (RG_WAJV_BLOCK_SIZE bytes) uit de 64 bytes `shm` (mag NULL zijn: dan alles los). */
void rg_wajv_fill(uint8_t *block, const uint8_t *shm, rg_wajv_state *st);

/* De toets-/knopbits van het bitveld als u32 (little-endian), 0 bij NULL. */
uint32_t rg_wajv_control(const uint8_t *shm);

#endif
