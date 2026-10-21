/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * c9_hasp.h -- the answers of the statically linked HASP HL "classic" library inside Coopers's 9's game.exe, at the level
 * of its own API (hasp_login / hasp_logout / hasp_read / hasp_decrypt), as the game's code checks them. Pure C, no Windows:
 * rg_c9table.c calls it from the stdcall entry points it plants on the library's functions, tests/c9_unit.c natively.
 *
 * What the game checks (measured from game.exe, 2026-10-21; the three callers are 0x406A80, 0x406BF0 and 0x406D80):
 *   hasp_login(0xFFFF0000, vendor code, &handle) == 0           (0x1F/0x07 -> state 2 "no dongle", 10/13 -> state 1)
 *   hasp_read(h, 0xFFF0, 0, 16): 'G' 'W' 'P' A1 04 01 <region> 04, byte 9 == 0x64, byte 15 == 0x64
 *   hasp_read(h, 0xFFF0, 3, 5):  A1 04 01                         (the same bytes seen from offset 3)
 *   hasp_read(h, 0xFFF0, 0x2E, 2): 00 01
 *   hasp_decrypt(h, 16 fixed bytes, 16): byte 0 == 'C' and byte 7 == '9'
 * The memory image and the decrypt answer are synthetic and come from the drop-in (RG_C9_HASP_MEM / RG_C9_HASP_DECRYPT).
 */
#ifndef C9_HASP_H
#define C9_HASP_H

#include <stddef.h>

/* Sentinel HASP HL status codes the game distinguishes. */
#define C9_HASP_OK 0u
#define C9_HASP_MEM_RANGE 1u
#define C9_HASP_TOO_SHORT 8u
#define C9_HASP_INV_HND 9u
#define C9_HASP_INV_FILEID 10u
#define C9_HASP_NOT_FOUND 7u
#define C9_HASP_FEATURE_NOT_FOUND 31u

#define C9_HASP_HANDLE 0x0C9u
#define C9_HASP_MEM_MAX 4096u
#define C9_HASP_PAIRS 8

typedef struct {
    unsigned fileid;                         /* the one memory file the game reads (0xFFF0, classic main memory) */
    unsigned char mem[C9_HASP_MEM_MAX];
    unsigned mem_len;
    unsigned char dec_in[C9_HASP_PAIRS][16];  /* decrypt: a 16-byte block the game sends ... */
    unsigned char dec_out[C9_HASP_PAIRS][16]; /* ... and the block it gets back */
    int npairs;
    int logged_in;
} c9_hasp_t;

/* "fff0:4757...": file id, ':' and the image in hex. Returns 0, or -1 when the text is not that. */
int c9_hasp_set_memory(c9_hasp_t *h, const char *text);
/* "<32 hex>:<32 hex>[,<32 hex>:<32 hex>...]": decrypt answers. Returns the number of pairs, or -1. */
int c9_hasp_set_decrypt(c9_hasp_t *h, const char *text);

unsigned c9_hasp_login(c9_hasp_t *h, unsigned feature, unsigned *handle);
unsigned c9_hasp_logout(c9_hasp_t *h, unsigned handle);
unsigned c9_hasp_read(const c9_hasp_t *h, unsigned handle, unsigned fileid, unsigned offset, unsigned length,
                      unsigned char *out);
/* In place, like the library. A block without an answer is left as it is and the call still returns OK; *hit says whether
 * an answer was used. */
unsigned c9_hasp_decrypt(const c9_hasp_t *h, unsigned handle, unsigned char *buf, unsigned length, int *hit);

#endif
