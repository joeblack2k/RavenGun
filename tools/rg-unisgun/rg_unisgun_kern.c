/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_unisgun_kern.c -- see rg_unisgun_kern.h. No Windows calls.
 */
#define _POSIX_C_SOURCE 200809L   /* strtok_r */
#include "rg_unisgun_kern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_src(rgg_src *s, const uint8_t *bits, int n)
{
    s->n = n;
    for (int k = 0; k < n; k++) s->bit[k] = bits[k];
}

/* Board image bits (WartranTroopers): 0 test, 1 service, 2 coin 1, 3 coin 2, 4 menu up, 5 menu down, then five bits per player
 * from 6/11: start, trigger, reload, grenade, aux1. The defaults only hold what the game's code proves (the coin bit 0x80); the
 * other button bits come from the table in patches/AfterDark/dropin/rg-unisgun.ini. */
void rgg_default(rgg_cfg *c)
{
    memset(c, 0, sizeof *c);
    set_src(&c->button[0][7], (const uint8_t[]){2}, 1);
    set_src(&c->button[1][7], (const uint8_t[]){3}, 1);
    for (int g = 0; g < 2; g++) {
        c->cal[g][0] = 0;
        c->cal[g][1] = 4095;
        c->cal[g][2] = 0;
        c->cal[g][3] = 4095;
    }
    c->offscreen = 0;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

int rgg_read_ini(const char *text, rgg_cfg *out, char *err, size_t errlen)
{
    rgg_cfg t = *out;
    int line_no = 0;
    const char *p = text;
    while (p && *p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char line[256];
        line_no++;
        if (len >= sizeof line) { snprintf(err, errlen, "line %d: too long", line_no); return -1; }
        memcpy(line, p, len);
        line[len] = 0;
        p = end ? end + 1 : NULL;
        char *c = strchr(line, ';');
        if (c) *c = 0;
        c = strchr(line, '#');
        if (c) *c = 0;
        char *r = trim(line);
        if (!*r) continue;
        char *eq = strchr(r, '=');
        if (!eq) { snprintf(err, errlen, "line %d: no '=' in '%s'", line_no, r); return -1; }
        *eq = 0;
        char *name = trim(r), *value = trim(eq + 1);
        if (!strcmp(name, "y.invert")) {
            if (strcmp(value, "0") && strcmp(value, "1")) { snprintf(err, errlen, "line %d: y.invert = 0 or 1", line_no); return -1; }
            t.invert_y = value[0] == '1';
            continue;
        }
        if (!strcmp(name, "offscreen")) {
            if (!strcmp(value, "edge")) { t.offscreen = -1; continue; }
            char *rest = NULL;
            long v = strtol(value, &rest, 10);
            if (!*value || *rest || v < 0 || v > 65535) { snprintf(err, errlen, "line %d: offscreen = 0..65535 or edge", line_no); return -1; }
            t.offscreen = (int)v;
            continue;
        }
        if ((name[0] != 'a' && name[0] != 'b') || name[1] != '.' || name[2] < '0' || name[2] > '7' || name[3]) {
            snprintf(err, errlen, "line %d: unknown key '%s' (a.0..a.7, b.0..b.7, offscreen, y.invert)", line_no, name);
            return -1;
        }
        rgg_src s = {0};
        if (*value) {
            char list[128];
            snprintf(list, sizeof list, "%s", value);
            char *save = NULL;
            for (char *tok = strtok_r(list, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
                char *x = trim(tok), *rest = NULL;
                long v = strtol(x, &rest, 10);
                if (!*x || *rest || v < 0 || v > 31) { snprintf(err, errlen, "line %d: board bit 0..31, not '%s'", line_no, x); return -1; }
                if (s.n >= RGG_MAX_SRC) { snprintf(err, errlen, "line %d: at most %d bits", line_no, RGG_MAX_SRC); return -1; }
                s.bit[s.n++] = (uint8_t)v;
            }
        }
        t.button[name[0] - 'a'][name[2] - '0'] = s;
    }
    *out = t;
    return 0;
}

int rgg_set_cal(rgg_cfg *c, int gun, int xmin, int xmax, int ymin, int ymax)
{
    if (gun < 0 || gun > 1) return -1;
    if (xmin < 0 || ymin < 0 || xmax > 65535 || ymax > 65535 || xmax <= xmin || ymax <= ymin) return -1;
    c->cal[gun][0] = xmin;
    c->cal[gun][1] = xmax;
    c->cal[gun][2] = ymin;
    c->cal[gun][3] = ymax;
    return 0;
}

int rgg_offscreen(const rgg_image *im, int gun)
{
    const uint8_t x = im->axis[2 * gun], y = im->axis[2 * gun + 1];
    return x <= 1 || x >= 254 || y <= 1 || y >= 254;
}

int rgg_raw(const rgg_cfg *c, const rgg_image *im, int gun, int a)
{
    const int lo = c->cal[gun][2 * a], hi = c->cal[gun][2 * a + 1];
    const uint8_t b = im->axis[2 * gun + a];
    if (rgg_offscreen(im, gun) && c->offscreen >= 0) return c->offscreen;
    /* The engine writes round(f * 255) and keeps an on-screen axis in 2..253; f = byte / 255 is its inverse. */
    double f = b / 255.0;
    if (a == 1 && c->invert_y) f = 1.0 - f;
    return (int)(lo + f * (hi - lo) + 0.5);
}

uint8_t rgg_buttons(const rgg_cfg *c, const rgg_image *im, int gun)
{
    uint8_t v = 0;
    for (int k = 0; k < 8; k++) {
        const rgg_src *s = &c->button[gun][k];
        for (int i = 0; i < s->n; i++)
            if (im->bits & (1u << s->bit[i])) { v = (uint8_t)(v | (1u << k)); break; }
    }
    return v;
}

void rgg_report(const rgg_cfg *c, const rgg_image *im, const uint8_t *extra, uint8_t o[RGG_REPORT_LEN])
{
    memset(o, 0, RGG_REPORT_LEN);
    o[0] = 0x40;
    o[1] = 0x80;
    o[2] = rgg_buttons(c, im, 0);
    o[3] = rgg_buttons(c, im, 1);
    for (int g = 0; g < 2; g++) {
        for (int a = 0; a < 2; a++) {
            const int v = rgg_raw(c, im, g, a);
            o[7 + 4 * g + 2 * a] = (uint8_t)(v >> 8);
            o[8 + 4 * g + 2 * a] = (uint8_t)(v & 0xff);
        }
    }
    if (extra)
        for (int i = 2; i < 24; i++) o[i] = (uint8_t)(o[i] | extra[i]);
    o[24] = 0x0d;
    uint8_t x = 0;
    for (int i = 0; i < RGG_REPORT_LEN - 1; i++) x ^= o[i];
    o[25] = x;
}

void rgg_scan(rgg_scanner *s, const uint8_t *p, size_t n, rgg_scan_result *r)
{
    for (size_t i = 0; i < n; i++) {
        if (s->n >= (int)sizeof s->buf) s->n = 0;
        s->buf[s->n++] = p[i];
        /* Drop leading bytes that cannot start a poll (50) or an output frame (40). */
        while (s->n > 0 && s->buf[0] != 0x50 && s->buf[0] != 0x40) {
            memmove(s->buf, s->buf + 1, (size_t)--s->n);
        }
        if (s->n == 0) continue;
        if (s->buf[0] == 0x50) {
            if (s->n >= 2 && s->buf[1] != 0x90) { memmove(s->buf, s->buf + 1, (size_t)--s->n); continue; }
            if (s->n >= 3) {
                if (s->buf[2] == 0x0d) { r->polls++; s->n = 0; }
                else memmove(s->buf, s->buf + 1, (size_t)--s->n);
            }
        } else {
            if (s->n >= 2 && s->buf[1] != 0x80) { memmove(s->buf, s->buf + 1, (size_t)--s->n); continue; }
            if (s->n >= RGG_OUTPUT_LEN) {
                r->outputs++;
                memcpy(r->last_output, s->buf, RGG_OUTPUT_LEN);
                s->n = 0;
            }
        }
    }
}
