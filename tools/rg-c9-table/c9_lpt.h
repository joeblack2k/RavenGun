/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * c9_lpt.h -- Coopers's 9's gun/IO board on the parallel port, answered from RavenGun's board image. Pure C, no Windows: the
 * DLL (rg_c9table.c) hands the game's Out32/Inp32 (inpout32) calls to it, tests/c9_unit.c runs it natively.
 *
 * EVIDENCE (static, game.exe, read 2026-10-21; the IO object is new'ed at 0x419CE6 and kept in [0x23E7F5C]):
 *   0x41A690 LoadLibrary("inpout32") and GetProcAddress "Out32" -> [io+0x3C], "Inp32" -> [io+0x40].
 *   send(b)  0x41A260: Out32(0x37A, 0xD0); Out32(0x378, b); Out32(0x37A, 0xD1); Out32(0x37A, 0xD0)   (strobe, data out)
 *   recv     0x41A3E0: Out32(0x37A, 0xF8); Out32(0x37A, 0xF9); r = Inp32(0x378); Out32(0x37A, 0xF8); Out32(0x37A, 0xF0)
 *   ack      0x41A530: the same without the read (after an output command)
 *   start    0x41AB20: s = Inp32(0x379); only when s & 6 is the board there and the IO thread 0x41AAF0 created
 *   poll     0x41A980 (thread 0x41AAF0, Sleep(0) between rounds): s = Inp32(0x379); command 7 (8 when s & 0x20) -> bits 0..4
 *            into bits 2..6 of the input word [io+0x1C]; bit 6 (command-7 bit 4) is the coin, counted on its rising edge
 *            (0x41A9C2 -> 0x406A30); when s & 0x80 or [io+0x26] (set every 8th frame by 0x4190E0): command 0x0C and the gun
 *            cycle 0x41A7F0: commands 1, 2 (player 1 x, y), 3, 4 (player 2 x, y) and 5 (gun bits); otherwise, when [io+0x28],
 *            command 6 (gun bits without positions).
 *   outputs  0x41A740, value v = [io+0x24] (the input manager's byte at 0x23E7F48): command (v << 4) | 0x0D carries the low
 *            nibble, then after a short wait (v & 0xF0) | 0x0E the high nibble. Bit 6 / 7 is the recoil solenoid of player
 *            1 / 2 (set by 0x419470/0x4194B0 when that player's recoil is enabled, cleared when its frame counter runs out,
 *            0x419C66); bit 0 / 1 is the start lamp of player 1 / 2 (measured in I/O CHECK: TEST+P1 START -> 0x01, P1 START+
 *            P1 TRIGGER -> a 0x40 pulse).
 *   gun bits (commands 5 and 6): bit 0 = player 1 trigger (input word bit 0), bit 4 = player 2 trigger (input word bit 1);
 *            bit 2 / bit 6 = the gun of player 1 / 2 sees the screen: a trigger without it zeroes that player's x/y (0x41A921)
 *            -- the off-screen shot; bits 1 and 5 are kept in [io+0x12] / [io+0x1A].
 *   The positions are raw bytes; the game maps them to the screen with its own GUN CALIBRATION (0x419B30).
 *   gun read 0x41A7F0, a state machine in [io+0x20] that runs while [io+0x21] is set; [io+0x21] is set by the poll when
 *            s & 0x80 or [io+0x26]:
 *              s & 0x80 alone: state 0 sets [io+0x30] = 1 (a shot) -> state 4 waits until s & 0x80 is clear (0x41A81D)
 *              -> commands 1, 2 -> 3, 4 -> 5 -> state 0 and [io+0x21] = 0: the full read with positions;
 *              [io+0x26] (every 8th frame): command 0x0C, state 5 -> 3 -> command 5 only (buttons, no positions).
 *            So s & 0x80 is the board's "a shot was taken": it must go up on a trigger and come down again by itself.
 *            c9_lpt raises it when a trigger goes down and drops it at the first command strobe after the game has read
 *            the status with it set (the poll's command 7), so the next poll reads the positions.
 *
 * Which board-image bit drives which board bit is a table (RG_C9_LPT_MAP in the drop-in):
 *   "<command>.<bit>=<image bit>[|<image bit>...]"  e.g. "5.0=7,7.4=2|3"; image bits are those of the WartranTroopers
 *   layout (offset 8: test 0, service 1, coin 2/3, p1 start 6, trigger 7, reload 8, p2 start 11, trigger 12, reload 13).
 *   "5.2=onscreen1" / "5.6=onscreen2": on while that player's aim is on the screen.
 *   "off1=8" / "off2=13": while that image bit is held the player counts as off-screen (no on-screen bit, aim bytes 0).
 *   The game reloads on a trigger pull off the screen (gun bits without the on-screen bit zero the position, 0x41A921;
 *   gun bits 1/5 go to [io+0x12]/[io+0x1A], which the IO code 0x418000-0x41B000 never reads), so "5.0=7|8,off1=8" makes
 *   the reload button that off-screen pull.
 */
#ifndef C9_LPT_H
#define C9_LPT_H

#include <stddef.h>
#include <stdint.h>

#define C9_LPT_SRC 4
#define C9_LPT_STATUS 0x06   /* status port: board present (0x41ABAE), command 7; busy (0x80) is added */
#define C9_LPT_ONSCREEN1 200
#define C9_LPT_ONSCREEN2 201

typedef struct {
    int n;
    uint8_t src[C9_LPT_SRC];   /* image bit 0..31, or C9_LPT_ONSCREEN1/2 */
} c9_lpt_src;

typedef struct {
    c9_lpt_src bit[16][8];     /* [command][bit] */
    c9_lpt_src off[2];         /* image bits that put player 1 / 2 off-screen while held ("off1=8") */
} c9_lpt_map;

typedef struct {
    uint32_t bits;             /* offset 8 of the board image */
    uint8_t axis[8];           /* offset 12..19: P1X P1Y P2X P2Y ..., 0 = left/top */
} c9_lpt_image;

typedef struct {
    uint8_t ctrl, data, cmd, reply;
    uint8_t outputs;           /* the output byte: low nibble from command 0x?D, high nibble from 0x?E */
    uint8_t busy;              /* status bit 0x80: a trigger went down and the game has not read the positions yet */
    uint8_t busy_seen;         /* the game read the status while busy was up */
    uint8_t triggers;          /* trigger bits (0x11) of command 5 at the last strobe */
    unsigned long commands[16];
    unsigned long output_writes;
} c9_lpt_board;

/* 0 = ok, -1 with the reason in err; on an error the map is left as it was. */
int c9_lpt_read_map(const char *text, c9_lpt_map *m, char *err, size_t errlen);
/* 1 when the aim of player p (0/1) is off-screen: the core writes an axis <= 1 or >= 254 then, or one of the map's
 * off<p> bits is held (m may be NULL). */
int c9_lpt_offscreen(const c9_lpt_map *m, const c9_lpt_image *im, int p);
/* The reply byte of a command for the board image at this moment. */
uint8_t c9_lpt_reply(const c9_lpt_map *m, const c9_lpt_image *im, uint8_t cmd);
/* The game's Out32 / Inp32 on ports 0x378..0x37A. Every command strobe samples the board image: a trigger that goes down
 * raises the busy bit (status 0x80); the first strobe after a status read that saw it drops it again. */
void c9_lpt_out(c9_lpt_board *b, const c9_lpt_map *m, const c9_lpt_image *im, uint16_t port, uint8_t value);
uint8_t c9_lpt_in(c9_lpt_board *b, uint16_t port);

#endif
