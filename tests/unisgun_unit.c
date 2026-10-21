/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unit test of the core of rg-unisgun (tools/rg-unisgun/rg_unisgun_kern.c): the table (read, refused as a whole), the 26-byte report
 * After Dark reads from COM1 (header, button bytes, big-endian axes in calibration units, terminator and XOR as the game's reader
 * 0x40e340 checks them), y inversion, off-screen, the probe bytes, and the scanner for what the game writes (polls and output frames,
 * also in pieces). No Windows needed.
 *
 *   unisgun_unit [TABLE.ini ...]     the shipped tables (patches/<Game>/dropin/rg-unisgun.ini) must be valid
 */
#include "rg_unisgun_kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

/* The game's own frame check (0x40e340 + 0x410580). */
static int game_accepts(const uint8_t *r)
{
    uint8_t x = 0;
    for (int i = 0; i < RGG_REPORT_LEN - 1; i++) x ^= r[i];
    return r[0] == 0x40 && r[1] == 0x80 && r[RGG_REPORT_LEN - 2] == 0x0d && r[RGG_REPORT_LEN - 1] == x;
}

static int be16(const uint8_t *r, int off) { return r[off] << 8 | r[off + 1]; }

int main(int argc, char **argv)
{
    char err[200];
    rgg_cfg c, t;
    rgg_default(&c);

    /* 1. Defaults: only the coin bits the game's code proves; full 12-bit range; off-screen = 0. */
    CHECK(c.button[0][7].n == 1 && c.button[0][7].bit[0] == 2, "coin A = board bit 2");
    CHECK(c.button[1][7].n == 1 && c.button[1][7].bit[0] == 3, "coin B = board bit 3");
    CHECK(c.button[0][0].n == 0 && c.button[0][1].n == 0, "no guessed buttons by default");
    CHECK(c.cal[0][1] == 4095 && c.offscreen == 0 && c.invert_y == 0, "default range/off-screen/inversion");

    /* 2. The table: valid lines are taken, every error refuses the whole file and leaves c untouched. */
    t = c;
    CHECK(rgg_read_ini("; comment\na.0 = 6\na.1 = 7, 8\nb.1 = 12\na.7 =\noffscreen = edge\ny.invert = 1\n", &t, err, sizeof err) == 0, "valid: %s", err);
    CHECK(t.button[0][0].n == 1 && t.button[0][0].bit[0] == 6, "a.0");
    CHECK(t.button[0][1].n == 2 && t.button[0][1].bit[1] == 8, "a.1 two bits");
    CHECK(t.button[0][7].n == 0, "an empty value clears the default");
    CHECK(t.button[1][7].n == 1, "a key that is not named keeps its default");
    CHECK(t.offscreen == -1 && t.invert_y == 1, "offscreen edge, y.invert");
    const char *bad[] = {"a.8 = 1\n", "c.0 = 1\n", "a.0 = 32\n", "a.0 = x\n", "a.0 = 1,2,3,4,5\n", "offscreen = -1\n",
                         "y.invert = 2\n", "a.0 1\n", "gun = 1\n"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        t = c;
        t.button[0][3].n = 0;
        CHECK(rgg_read_ini(bad[i], &t, err, sizeof err) == -1, "refused: %s", bad[i]);
        CHECK(!memcmp(&t.button, &c.button, sizeof c.button) && t.offscreen == c.offscreen, "untouched after: %s", bad[i]);
    }

    /* 3. Calibration: reversed or empty ranges are refused. */
    t = c;
    CHECK(rgg_set_cal(&t, 0, 286, 3967, 1575, 4092) == 0 && t.cal[0][0] == 286 && t.cal[0][3] == 4092, "coin.ini range of gun 1");
    CHECK(rgg_set_cal(&t, 1, 100, 100, 0, 10) == -1 && rgg_set_cal(&t, 1, 0, 10, 20, 5) == -1 && rgg_set_cal(&t, 2, 0, 1, 0, 1) == -1,
          "bad ranges refused");
    CHECK(rgg_set_cal(&t, 1, -1, -1, -1, -1) == -1 && t.cal[1][1] == 4095, "missing keys (-1) keep the previous range");

    /* 4. The report from a board image: P1 start + trigger, P2 trigger, coin 1, aim. */
    rgg_cfg g = c;
    CHECK(rgg_read_ini("a.0 = 6\na.1 = 7\na.3 = 0\nb.0 = 11\nb.1 = 12\n", &g, err, sizeof err) == 0, "game table: %s", err);
    rgg_set_cal(&g, 0, 286, 3967, 1575, 4092);
    rgg_set_cal(&g, 1, 65, 3678, 517, 3972);
    rgg_image im = {0};
    im.bits = (1u << 6) | (1u << 7) | (1u << 12) | (1u << 2);
    im.axis[0] = 0x80; im.axis[1] = 2; im.axis[2] = 253; im.axis[3] = 0x80;
    uint8_t r[RGG_REPORT_LEN];
    rgg_report(&g, &im, NULL, r);
    CHECK(game_accepts(r), "frame accepted by the game's check");
    CHECK(r[2] == 0x83, "Gun(A) Buttons start|trigger|coin = 0x83, got %02x", r[2]);
    CHECK(r[3] == 0x02, "Gun(B) Buttons trigger = 0x02, got %02x", r[3]);
    CHECK(r[4] == 0 && r[5] == 0 && r[6] == 0, "no gun error, no extra bits");
    CHECK(be16(r, 7) == (int)(286 + 128 / 255.0 * (3967 - 286) + 0.5), "A x big-endian in calibration units: %d", be16(r, 7));
    CHECK(be16(r, 9) == (int)(1575 + 2 / 255.0 * (4092 - 1575) + 0.5), "A y: %d", be16(r, 9));
    CHECK(be16(r, 11) == (int)(65 + 253 / 255.0 * (3678 - 65) + 0.5), "B x: %d", be16(r, 11));
    for (int i = 15; i < 24; i++) CHECK(r[i] == 0, "byte %d is zero", i);

    /* 5. y inversion: the top of the screen (axis byte 2) is near the y maximum. */
    g.invert_y = 1;
    CHECK(rgg_raw(&g, &im, 0, 1) == (int)(1575 + (1.0 - 2 / 255.0) * (4092 - 1575) + 0.5), "inverted y: %d", rgg_raw(&g, &im, 0, 1));
    CHECK(rgg_raw(&g, &im, 0, 0) == be16(r, 7), "x is not inverted");

    /* 6. Off-screen (the core's rule: an axis <= 1 or >= 254): the configured raw value, or the edge. */
    im.axis[0] = 0;
    CHECK(rgg_offscreen(&im, 0) && !rgg_offscreen(&im, 1), "P1 off-screen, P2 not");
    CHECK(rgg_raw(&g, &im, 0, 0) == 0 && rgg_raw(&g, &im, 0, 1) == 0, "offscreen = 0 on both axes");
    g.offscreen = -1;
    CHECK(rgg_raw(&g, &im, 0, 0) == 286, "offscreen = edge clamps to the calibration minimum: %d", rgg_raw(&g, &im, 0, 0));
    g.offscreen = 0;

    /* 7. The probe ORs bytes 2..23 in before the checksum; the header and terminator stay. */
    uint8_t probe[RGG_REPORT_LEN] = {0};
    probe[0] = 0xff; probe[2] = 0x10; probe[5] = 0x01; probe[24] = 0xff;
    rgg_report(&g, &im, probe, r);
    CHECK(game_accepts(r) && (r[2] & 0x10) && r[5] == 0x01, "probe bits in, frame still valid");

    /* 8. The scanner: polls and output frames, whole and in pieces, with noise in between. */
    rgg_scanner s = {0};
    rgg_scan_result res = {0};
    const uint8_t poll[] = {0x50, 0x90, 0x0d};
    rgg_scan(&s, poll, 3, &res);
    CHECK(res.polls == 1, "one poll");
    const uint8_t mixed[] = {0x00, 0x50, 0x90, 0x40, 0x80, 0x61, 0, 0, 0, 0, 0x0d, 0xac, 0x00, 0x50};
    rgg_scan(&s, mixed, 5, &res);
    rgg_scan(&s, mixed + 5, sizeof mixed - 5, &res);
    rgg_scan(&s, poll + 1, 2, &res);
    CHECK(res.outputs == 1 && res.last_output[2] == 0x61, "output frame 40 80 61 .. (got %d, %02x)", res.outputs, res.last_output[2]);
    CHECK(res.polls == 2, "a broken poll is not counted, the poll split over writes is (got %d)", res.polls);

    /* 9. Shipped tables. */
    for (int i = 1; i < argc; i++) {
        char *text = slurp(argv[i]);
        CHECK(text != NULL, "cannot read %s", argv[i]);
        if (!text) continue;
        rgg_cfg x;
        rgg_default(&x);
        CHECK(rgg_read_ini(text, &x, err, sizeof err) == 0, "%s: %s", argv[i], err);
    }

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
