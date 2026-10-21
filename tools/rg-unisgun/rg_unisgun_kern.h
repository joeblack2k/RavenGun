/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_unisgun_kern.h -- the pure core of rg-unisgun (the serial gun board of After Dark, UNIS 2012, Virtools GamePlayer.exe; see
 * rg_unisgun.c): from RavenGun's board image (TeknoParrot_JvsState, layout WartranTroopers from packaging/tp-shm-profiles.json: a
 * 32-bit field at offset 8, eight 1-byte aim axes at offset 12..19) to the 26-byte report the game reads from COM1.
 *
 * No Windows calls: the same code runs in the DLL and in the unit test on Linux (tests/unisgun_unit.c).
 *
 * EVIDENCE (static, GamePlayer.exe sha256 d4ffc802..., read 2026-10-21):
 *   0x40e5d0 opens port 1 ("\\.\COM%d", 0x416008), 19200 baud 8 bits, report header 0x40 and length 0x1a.
 *   0x40ffd0 (I/O thread) writes the poll 50 90 0d (3 bytes) each time its event fires.
 *   0x40e340 (reader) accepts a frame when byte 0 = 0x40, byte len-2 = 0x0d and byte len-1 = XOR of bytes 0..len-2.
 *   0x410580 (frame handler) takes it only when byte 1 = 0x80 and copies bytes 0..23 to the board object (+0xcc).
 *   Readers of the copy: 0x4101d0/0x4101f0 Gun(A)/Gun(B) Buttons = bytes 2/3; 0x410210..0x410270 Gun A X, A Y, B X, B Y =
 *   bytes 7-8, 9-10, 11-12, 13-14, big-endian; 0x410160 "UserBB Gun Error" = byte 4 (not 0 = error).
 *   Coins: 0x410580 counts a coin for gun A (B) when bit 0x80 of byte 2 (3) is set and was not seen for more than 100 ms.
 *   Calibration: coin.ini [Gun1xMin] 1xMin ... [Gun2yMax] 2yMax (raw board units, 12 bits).
 *   Game -> board outputs: 0x40fe20 writes 40 80 <bits> 00 .. 0d <xor> (10 bytes): lamps/vibration; logged, not needed.
 *
 * Which board-image bit drives which bit of the Buttons bytes is a table per game (rg-unisgun.ini next to the DLL):
 *
 *   ; a.N / b.N = board bit[, bit ...]     bit N (0..7) of Gun(A) / Gun(B) Buttons is on when one of those bits is on
 *   a.7 = 2
 *   offscreen = 0                         raw value sent on both axes while the aim is off-screen (0..65535), or "edge"
 *   y.invert = 1                          the top of the screen is the y maximum of the calibration (0 = the minimum)
 */
#ifndef RG_UNISGUN_KERN_H
#define RG_UNISGUN_KERN_H

#include <stddef.h>
#include <stdint.h>

#define RGG_REPORT_LEN 26
#define RGG_OUTPUT_LEN 10
#define RGG_MAX_SRC 4

typedef struct {
    int n;
    uint8_t bit[RGG_MAX_SRC];
} rgg_src;

typedef struct {
    rgg_src button[2][8];      /* [gun][bit]: Gun(A)/Gun(B) Buttons = report bytes 2/3 */
    int cal[2][4];             /* [gun]: x min, x max, y min, y max in raw board units (coin.ini) */
    int offscreen;             /* raw value on both axes while off-screen; -1 = clamp to the calibration edge */
    int invert_y;              /* 1 = the top of the screen is the y maximum of the calibration */
} rgg_cfg;

typedef struct {
    uint32_t bits;             /* offset 8 of the board image */
    uint8_t axis[8];           /* offset 12..19: P1X P1Y P2X P2Y ..., 0 = left/top */
} rgg_image;

/* Game -> board byte stream: polls and output frames are counted; the bytes may arrive in pieces. */
typedef struct {
    uint8_t buf[32];
    int n;
} rgg_scanner;

typedef struct {
    int polls;
    int outputs;
    uint8_t last_output[RGG_OUTPUT_LEN];
} rgg_scan_result;

void rgg_default(rgg_cfg *c);
/* 0 = ok; otherwise -1 with the reason in err. A table with an error is NOT half applied (c stays as it was). */
int rgg_read_ini(const char *text, rgg_cfg *c, char *err, size_t errlen);
/* Calibration of gun g (0/1) from coin.ini values; an empty or reversed range is refused (returns -1, c unchanged). */
int rgg_set_cal(rgg_cfg *c, int gun, int xmin, int xmax, int ymin, int ymax);
/* 1 when the aim of gun g is off-screen in the board image (the core's rule: an axis <= 1 or >= 254). */
int rgg_offscreen(const rgg_image *im, int gun);
/* Raw board value of gun g, axis a (0 = x, 1 = y). */
int rgg_raw(const rgg_cfg *c, const rgg_image *im, int gun, int a);
/* The Buttons byte of gun g. */
uint8_t rgg_buttons(const rgg_cfg *c, const rgg_image *im, int gun);
/* The 26-byte report; extra (may be NULL) is OR-ed into bytes 2..23 before the checksum (a probe for unknown bits). */
void rgg_report(const rgg_cfg *c, const rgg_image *im, const uint8_t *extra, uint8_t out[RGG_REPORT_LEN]);
/* Feed bytes the game wrote; counts complete polls (50 90 0d) and output frames (40 80 .. 10 bytes) into r. */
void rgg_scan(rgg_scanner *s, const uint8_t *p, size_t n, rgg_scan_result *r);

#endif
