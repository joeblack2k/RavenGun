/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * c9_hasp.c -- see c9_hasp.h. Pure C so tests/c9_unit.c can run it natively.
 */
#include "c9_hasp.h"

#include <stdlib.h>
#include <string.h>

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly n bytes of hex at p; returns the end, or NULL. */
static const char *hexbytes(const char *p, unsigned char *out, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        const int a = hexval(p[0]), b = a < 0 ? -1 : hexval(p[1]);
        if (b < 0) return NULL;
        out[i] = (unsigned char)(a * 16 + b);
        p += 2;
    }
    return p;
}

int c9_hasp_set_memory(c9_hasp_t *h, const char *text) {
    char *end = NULL;
    const unsigned long fileid = strtoul(text, &end, 16);
    size_t n;
    if (!end || end == text || *end != ':' || fileid > 0xFFFFu) return -1;
    ++end;
    n = strlen(end);
    if (n == 0 || (n % 2) != 0 || n / 2 > C9_HASP_MEM_MAX) return -1;
    if (!hexbytes(end, h->mem, (unsigned)(n / 2))) return -1;
    h->fileid = (unsigned)fileid;
    h->mem_len = (unsigned)(n / 2);
    return 0;
}

int c9_hasp_set_decrypt(c9_hasp_t *h, const char *text) {
    const char *p = text;
    int n = 0;
    while (*p) {
        if (n >= C9_HASP_PAIRS) return -1;
        p = hexbytes(p, h->dec_in[n], 16);
        if (!p || *p != ':') return -1;
        p = hexbytes(p + 1, h->dec_out[n], 16);
        if (!p) return -1;
        ++n;
        if (*p == ',') ++p;
        else if (*p) return -1;
    }
    h->npairs = n;
    return n;
}

unsigned c9_hasp_login(c9_hasp_t *h, unsigned feature, unsigned *handle) {
    /* The library logs in classic features only (0xFFFF0000 | program number, 0x4A3782); everything else it answers
     * 0x1C itself. Without a memory image there is no dongle to find. */
    if ((feature & 0xFFFF0000u) != 0xFFFF0000u) return 0x1Cu;
    if (h->mem_len == 0) return C9_HASP_NOT_FOUND;
    h->logged_in = 1;
    if (handle) *handle = C9_HASP_HANDLE;
    return C9_HASP_OK;
}

unsigned c9_hasp_logout(c9_hasp_t *h, unsigned handle) {
    if (handle != C9_HASP_HANDLE) return C9_HASP_INV_HND;
    h->logged_in = 0;
    return C9_HASP_OK;
}

unsigned c9_hasp_read(const c9_hasp_t *h, unsigned handle, unsigned fileid, unsigned offset, unsigned length,
                      unsigned char *out) {
    if (handle != C9_HASP_HANDLE) return C9_HASP_INV_HND;   /* no session state: the game logs in per check, maybe from two threads */
    if (fileid != h->fileid) return C9_HASP_INV_FILEID;
    if (offset > h->mem_len || length > h->mem_len - offset) return C9_HASP_MEM_RANGE;
    if (length) memcpy(out, h->mem + offset, length);
    return C9_HASP_OK;
}

unsigned c9_hasp_decrypt(const c9_hasp_t *h, unsigned handle, unsigned char *buf, unsigned length, int *hit) {
    if (hit) *hit = 0;
    if (handle != C9_HASP_HANDLE) return C9_HASP_INV_HND;   /* no session state: the game logs in per check, maybe from two threads */
    if (length < 16) return C9_HASP_TOO_SHORT;
    for (int i = 0; i < h->npairs; ++i) {
        if (memcmp(buf, h->dec_in[i], 16) == 0) {
            memcpy(buf, h->dec_out[i], 16);
            if (hit) *hit = 1;
            break;
        }
    }
    return C9_HASP_OK;
}
