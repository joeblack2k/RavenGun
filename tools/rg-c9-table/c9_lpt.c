/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * c9_lpt.c -- see c9_lpt.h. Pure C so tests/c9_unit.c can run it natively.
 */
#include "c9_lpt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t errlen, const char *what, const char *at) {
    if (err && errlen) snprintf(err, errlen, "%s at \"%.20s\"", what, at);
    return -1;
}

int c9_lpt_read_map(const char *text, c9_lpt_map *m, char *err, size_t errlen) {
    c9_lpt_map out;
    const char *p = text;
    memset(&out, 0, sizeof out);
    while (*p) {
        char *e = NULL;
        c9_lpt_src *s;
        if (strncmp(p, "off1=", 5) == 0 || strncmp(p, "off2=", 5) == 0) {
            s = &out.off[p[3] - '1'];
            p += 5;
        } else {
            const long cmd = strtol(p, &e, 0);
            long bit;
            if (e == p || *e != '.' || cmd < 0 || cmd > 15) return fail(err, errlen, "command 0..15 expected", p);
            p = e + 1;
            bit = strtol(p, &e, 10);
            if (e == p || *e != '=' || bit < 0 || bit > 7) return fail(err, errlen, "bit 0..7 expected", p);
            p = e + 1;
            s = &out.bit[cmd][bit];
        }
        for (;;) {
            int src;
            if (strncmp(p, "onscreen1", 9) == 0) { src = C9_LPT_ONSCREEN1; p += 9; }
            else if (strncmp(p, "onscreen2", 9) == 0) { src = C9_LPT_ONSCREEN2; p += 9; }
            else {
                const long v = strtol(p, &e, 10);
                if (e == p || v < 0 || v > 31) return fail(err, errlen, "image bit 0..31 expected", p);
                src = (int)v;
                p = e;
            }
            if (s->n >= C9_LPT_SRC) return fail(err, errlen, "too many sources", p);
            s->src[s->n++] = (uint8_t)src;
            if (*p != '|') break;
            ++p;
        }
        if (*p == ',' || *p == ' ') ++p;
        else if (*p) return fail(err, errlen, "',' expected", p);
    }
    *m = out;
    return 0;
}

static int source_on(const c9_lpt_map *m, const c9_lpt_image *im, uint8_t src);

int c9_lpt_offscreen(const c9_lpt_map *m, const c9_lpt_image *im, int p) {
    const uint8_t x = im->axis[2 * p], y = im->axis[2 * p + 1];
    if (x <= 1 || x >= 254 || y <= 1 || y >= 254) return 1;
    if (m)
        for (int i = 0; i < m->off[p].n; ++i)
            if (m->off[p].src[i] < 32 && source_on(m, im, m->off[p].src[i])) return 1;
    return 0;
}

static int source_on(const c9_lpt_map *m, const c9_lpt_image *im, uint8_t src) {
    if (src == C9_LPT_ONSCREEN1) return !c9_lpt_offscreen(m, im, 0);
    if (src == C9_LPT_ONSCREEN2) return !c9_lpt_offscreen(m, im, 1);
    return (im->bits >> src) & 1u;
}

uint8_t c9_lpt_reply(const c9_lpt_map *m, const c9_lpt_image *im, uint8_t cmd) {
    uint8_t r = 0;
    /* 1..4: the raw position bytes, player 1 x, y, player 2 x, y; an off-screen aim reads as 0 (the game ignores the
     * position of a shot without the on-screen bit anyway). */
    if (cmd >= 1 && cmd <= 4) {
        const int p = (cmd - 1) / 2;
        return c9_lpt_offscreen(m, im, p) ? 0 : im->axis[cmd - 1];
    }
    if (cmd > 15) return 0;
    for (int bit = 0; bit < 8; ++bit) {
        const c9_lpt_src *s = &m->bit[cmd][bit];
        for (int i = 0; i < s->n; ++i)
            if (source_on(m, im, s->src[i])) { r |= (uint8_t)(1u << bit); break; }
    }
    return r;
}

void c9_lpt_out(c9_lpt_board *b, const c9_lpt_map *m, const c9_lpt_image *im, uint16_t port, uint8_t value) {
    if (port == 0x378) {
        b->data = value;
        return;
    }
    if (port != 0x37A) return;
    /* Strobe (control bit 0) rising while the data lines drive (bit 5 clear): the byte on the data port is a command. */
    if ((value & 0x01) && !(b->ctrl & 0x01) && !(value & 0x20)) {
        b->cmd = b->data;
        if ((b->cmd & 0x0F) == 0x0D) {          /* low nibble of the output byte (lamps) */
            b->outputs = (uint8_t)((b->outputs & 0xF0) | (b->cmd >> 4));
            ++b->output_writes;
        } else if ((b->cmd & 0x0F) == 0x0E) {   /* high nibble (bit 6/7: recoil of player 1/2) */
            b->outputs = (uint8_t)((b->outputs & 0x0F) | (b->cmd & 0xF0));
            ++b->output_writes;
        }
        b->reply = c9_lpt_reply(m, im, b->cmd);
        ++b->commands[b->cmd & 0x0F];
        if (b->busy_seen) b->busy = b->busy_seen = 0;
        {
            const uint8_t t = c9_lpt_reply(m, im, 5) & 0x11;
            if (t & ~b->triggers) b->busy = 1;
            b->triggers = t;
        }
    }
    b->ctrl = value;
}

uint8_t c9_lpt_in(c9_lpt_board *b, uint16_t port) {
    if (port == 0x378) return b->reply;
    if (port == 0x379 && b->busy) b->busy_seen = 1;
    /* Status: the board is there when bit 1 or 2 is set (0x41ABAE: otherwise no IO thread is started); bit 0x80 is the
     * shot handshake (see c9_lpt.h); bit 0x20 selects command 8 instead of 7 and stays clear. */
    return (uint8_t)(C9_LPT_STATUS | (b->busy ? 0x80 : 0x00));
}
