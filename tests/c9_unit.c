/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * c9_unit.c -- unit test of the pure C parts of rg-c9-table (Coopers's 9): the HASP HL answers (tools/rg-c9-table/c9_hasp.c)
 * and the parallel-port gun/IO board (tools/rg-c9-table/c9_lpt.c). Run by tests/c9_test.sh; optional argument: the shipped
 * drop-in (patches/Coopers9/dropin/dropin.ini), whose RG_C9_HASP_MEM, RG_C9_HASP_DECRYPT and RG_C9_LPT_MAP must parse and
 * give the answers the game's code checks.
 */
#include "c9_hasp.h"
#include "c9_lpt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #c);                                              \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static c9_hasp_t hasp;

static void test_hasp(void) {
    unsigned h = 0;
    unsigned char buf[32];
    int hit = -1;
    memset(&hasp, 0, sizeof hasp);
    CHECK(c9_hasp_login(&hasp, 0xFFFF0000u, &h) == C9_HASP_NOT_FOUND); /* no memory image: no dongle */
    CHECK(c9_hasp_set_memory(&hasp, "fff0:4757") == 0);
    CHECK(hasp.fileid == 0xFFF0u && hasp.mem_len == 2 && hasp.mem[0] == 0x47 && hasp.mem[1] == 0x57);
    CHECK(c9_hasp_set_memory(&hasp, "fff0:475") == -1);   /* odd hex */
    CHECK(c9_hasp_set_memory(&hasp, "fff0:47zz") == -1);
    CHECK(c9_hasp_set_memory(&hasp, "4757") == -1);       /* no file id */
    CHECK(hasp.mem_len == 2);                             /* a bad text leaves the image */
    CHECK(c9_hasp_login(&hasp, 0x00000001u, &h) == 0x1Cu); /* not a classic feature */
    CHECK(c9_hasp_login(&hasp, 0xFFFF0000u, &h) == C9_HASP_OK && h == C9_HASP_HANDLE);
    CHECK(c9_hasp_read(&hasp, h, 0xFFF0u, 0, 2, buf) == C9_HASP_OK && buf[0] == 0x47 && buf[1] == 0x57);
    CHECK(c9_hasp_read(&hasp, h, 0xFFF0u, 1, 2, buf) == C9_HASP_MEM_RANGE);
    CHECK(c9_hasp_read(&hasp, h, 0xFFF1u, 0, 1, buf) == C9_HASP_INV_FILEID);
    CHECK(c9_hasp_read(&hasp, h + 1, 0xFFF0u, 0, 1, buf) == C9_HASP_INV_HND);
    CHECK(c9_hasp_set_decrypt(&hasp, "000102030405060708090a0b0c0d0e0f:ffeeddccbbaa99887766554433221100") == 1);
    CHECK(c9_hasp_set_decrypt(&hasp, "0001:ff") == -1);
    for (int i = 0; i < 16; ++i) buf[i] = (unsigned char)i;
    CHECK(c9_hasp_decrypt(&hasp, h, buf, 16, &hit) == C9_HASP_OK && hit == 1 && buf[0] == 0xFF && buf[15] == 0x00);
    CHECK(c9_hasp_decrypt(&hasp, h, buf, 16, &hit) == C9_HASP_OK && hit == 0 && buf[0] == 0xFF); /* unknown block: as is */
    CHECK(c9_hasp_decrypt(&hasp, h, buf, 8, &hit) == C9_HASP_TOO_SHORT);
    CHECK(c9_hasp_logout(&hasp, h) == C9_HASP_OK && hasp.logged_in == 0);
    CHECK(c9_hasp_logout(&hasp, 5) == C9_HASP_INV_HND);
}

static void send_cmd(c9_lpt_board *b, const c9_lpt_map *m, const c9_lpt_image *im, uint8_t cmd) {
    /* the game's send (0x41A260) */
    c9_lpt_out(b, m, im, 0x37A, 0xD0);
    c9_lpt_out(b, m, im, 0x378, cmd);
    c9_lpt_out(b, m, im, 0x37A, 0xD1);
    c9_lpt_out(b, m, im, 0x37A, 0xD0);
}

static uint8_t recv_byte(c9_lpt_board *b, const c9_lpt_map *m, const c9_lpt_image *im) {
    /* the game's receive (0x41A3E0): the strobe with bit 5 set must not count as a command */
    uint8_t r;
    c9_lpt_out(b, m, im, 0x37A, 0xF8);
    c9_lpt_out(b, m, im, 0x37A, 0xF9);
    r = c9_lpt_in(b, 0x378);
    c9_lpt_out(b, m, im, 0x37A, 0xF8);
    c9_lpt_out(b, m, im, 0x37A, 0xF0);
    return r;
}

static void test_lpt(void) {
    c9_lpt_map m;
    c9_lpt_board b;
    c9_lpt_image im;
    char err[128];
    memset(&b, 0, sizeof b);
    memset(&im, 0, sizeof im);
    CHECK(c9_lpt_read_map("5.0=7,5.2=onscreen1,7.4=2|3", &m, err, sizeof err) == 0);
    {
        c9_lpt_map keep = m;
        CHECK(c9_lpt_read_map("5.0=7,5.8=1", &m, err, sizeof err) == -1);    /* bit 8 */
        CHECK(c9_lpt_read_map("16.0=1", &m, err, sizeof err) == -1);         /* command 16 */
        CHECK(c9_lpt_read_map("5.0=32", &m, err, sizeof err) == -1);         /* image bit 32 */
        CHECK(c9_lpt_read_map("5.0=1|2|3|4|5", &m, err, sizeof err) == -1);  /* too many sources */
        CHECK(c9_lpt_read_map("5.0=1;5.1=2", &m, err, sizeof err) == -1);
        CHECK(memcmp(&keep, &m, sizeof m) == 0);                             /* all or nothing */
    }
    /* off-screen: an axis <= 1 or >= 254 */
    im.axis[0] = 128; im.axis[1] = 1;
    CHECK(c9_lpt_offscreen(&m, &im, 0));
    im.axis[1] = 2;
    CHECK(!c9_lpt_offscreen(&m, &im, 0));
    im.axis[0] = 254;
    CHECK(c9_lpt_offscreen(&m, &im, 0));
    /* replies */
    im.axis[0] = 40; im.axis[1] = 200; im.axis[2] = 0; im.axis[3] = 0;
    im.bits = 1u << 7;
    CHECK(c9_lpt_reply(&m, &im, 1) == 40 && c9_lpt_reply(&m, &im, 2) == 200);
    CHECK(c9_lpt_reply(&m, &im, 3) == 0 && c9_lpt_reply(&m, &im, 4) == 0);   /* player 2 off-screen reads 0 */
    CHECK(c9_lpt_reply(&m, &im, 5) == 0x05);
    CHECK(c9_lpt_reply(&m, &im, 7) == 0x00);
    im.bits = 1u << 3;
    CHECK(c9_lpt_reply(&m, &im, 7) == 0x10);   /* either coin */
    {
        /* a reload button as an off-screen trigger pull */
        c9_lpt_map r;
        CHECK(c9_lpt_read_map("5.0=7|8,5.2=onscreen1,off1=8", &r, err, sizeof err) == 0);
        im.bits = 1u << 8;
        CHECK(c9_lpt_offscreen(&r, &im, 0) && !c9_lpt_offscreen(NULL, &im, 0));
        CHECK(c9_lpt_reply(&r, &im, 5) == 0x01 && c9_lpt_reply(&r, &im, 1) == 0 && c9_lpt_reply(&r, &im, 2) == 0);
        im.bits = 1u << 7;
        CHECK(c9_lpt_reply(&r, &im, 5) == 0x05 && c9_lpt_reply(&r, &im, 1) == 40);
        CHECK(c9_lpt_read_map("off3=1", &r, err, sizeof err) == -1);
    }
    im.bits = 0;
    /* the protocol: status, a command and its reply, the receive strobe is not a command */
    CHECK(c9_lpt_in(&b, 0x379) == C9_LPT_STATUS);
    send_cmd(&b, &m, &im, 1);
    CHECK(recv_byte(&b, &m, &im) == 40);
    send_cmd(&b, &m, &im, 2);
    CHECK(recv_byte(&b, &m, &im) == 200);
    CHECK(b.commands[1] == 1 && b.commands[2] == 1 && b.commands[0] == 0);
    /* outputs: (v << 4) | 0x0D */
    send_cmd(&b, &m, &im, 0x3D);
    CHECK(b.outputs == 0x03 && b.output_writes == 1);
    send_cmd(&b, &m, &im, 0x4E);                       /* recoil of player 1 (bit 6) */
    CHECK(b.outputs == 0x43 && b.output_writes == 2);
    send_cmd(&b, &m, &im, 0x0D);
    CHECK(b.outputs == 0x40);
    /* the shot handshake: a trigger raises 0x80, the game sees it, the next command drops it */
    send_cmd(&b, &m, &im, 7);
    CHECK((c9_lpt_in(&b, 0x379) & 0x80) == 0);
    im.bits = 1u << 7;
    send_cmd(&b, &m, &im, 7);
    CHECK((c9_lpt_in(&b, 0x379) & 0x80) == 0x80);   /* the poll sees it: gun read state 0 -> 4 */
    send_cmd(&b, &m, &im, 7);                         /* the same poll's command 7 */
    CHECK((c9_lpt_in(&b, 0x379) & 0x80) == 0);        /* next poll: state 4 reads the positions */
    send_cmd(&b, &m, &im, 7);
    CHECK((c9_lpt_in(&b, 0x379) & 0x80) == 0);        /* held trigger: no second shot */
    im.bits = 0;
    send_cmd(&b, &m, &im, 7);
    im.bits = 1u << 7;
    send_cmd(&b, &m, &im, 7);
    CHECK((c9_lpt_in(&b, 0x379) & 0x80) == 0x80);   /* a new pull: a new shot */
}

/* The value of KEY= inside the Env = line of the [rg-c9-table.dll] section, or NULL. */
static char *env_value(const char *ini, const char *key, char *out, size_t outlen) {
    const char *sec = strstr(ini, "[rg-c9-table.dll]");
    const char *env, *p, *e;
    size_t kl = strlen(key);
    if (!sec) return NULL;
    env = strstr(sec, "\nEnv = ");
    if (!env) return NULL;
    env += 7;
    for (p = env; *p && *p != '\n';) {
        e = p;
        while (*e && *e != ';' && *e != '\n') ++e;
        if ((size_t)(e - p) > kl && strncmp(p, key, kl) == 0 && p[kl] == '=') {
            size_t n = (size_t)(e - p) - kl - 1;
            if (n >= outlen) return NULL;
            memcpy(out, p + kl + 1, n);
            out[n] = 0;
            return out;
        }
        p = *e == ';' ? e + 1 : e;
    }
    return NULL;
}

static void test_shipped(const char *path) {
    static char ini[1 << 16], v[16384];
    char err[128];
    FILE *f = fopen(path, "rb");
    size_t n;
    unsigned h = 0;
    unsigned char buf[16];
    static const unsigned char block[16] = {0xaf, 0xcf, 0x89, 0x33, 0xfe, 0xa2, 0xcb, 0x21,
                                            0x29, 0x03, 0x0d, 0x94, 0xa6, 0x04, 0x0a, 0xdd};
    int hit = 0;
    c9_lpt_map m;
    c9_lpt_image im;
    if (!f) { fprintf(stderr, "FAIL: cannot open %s\n", path); ++failures; return; }
    n = fread(ini, 1, sizeof ini - 1, f);
    fclose(f);
    ini[n] = 0;
    memset(&hasp, 0, sizeof hasp);
    CHECK(env_value(ini, "RG_C9_HASP_MEM", v, sizeof v) && c9_hasp_set_memory(&hasp, v) == 0);
    CHECK(env_value(ini, "RG_C9_HASP_DECRYPT", v, sizeof v) && c9_hasp_set_decrypt(&hasp, v) >= 1);
    /* what the game's three callers (0x406A80, 0x406BF0, 0x406D80) check */
    CHECK(c9_hasp_login(&hasp, 0xFFFF0000u, &h) == C9_HASP_OK);
    CHECK(c9_hasp_read(&hasp, h, 0xFFF0u, 0, 16, buf) == C9_HASP_OK);
    CHECK(buf[0] == 'G' && buf[1] == 'W' && buf[2] == 'P' && buf[3] == 0xA1 && buf[4] == 0x04 && buf[5] == 0x01 &&
          buf[6] < 7 && buf[7] == 0x04 && buf[9] == 0x64 && buf[15] == 0x64);
    CHECK(c9_hasp_read(&hasp, h, 0xFFF0u, 0x2E, 2, buf) == C9_HASP_OK && buf[0] == 0x00 && buf[1] == 0x01);
    memcpy(buf, block, 16);
    CHECK(c9_hasp_decrypt(&hasp, h, buf, 16, &hit) == C9_HASP_OK && hit && buf[0] == 'C' && buf[7] == '9');
    /* the board map: test, service, coin, starts and the guns of the WartranTroopers board image */
    CHECK(env_value(ini, "RG_C9_LPT_MAP", v, sizeof v) && c9_lpt_read_map(v, &m, err, sizeof err) == 0);
    memset(&im, 0, sizeof im);
    im.axis[0] = im.axis[1] = 128;
    im.axis[2] = im.axis[3] = 0;
    im.bits = 1u << 0;  CHECK(c9_lpt_reply(&m, &im, 7) == 0x04);   /* test -> input mask 0x10 (0x41E210) */
    im.bits = 1u << 1;  CHECK(c9_lpt_reply(&m, &im, 7) == 0x08);   /* service */
    im.bits = 1u << 2;  CHECK(c9_lpt_reply(&m, &im, 7) == 0x10);   /* coin -> input bit 6 (0x41A9C2) */
    im.bits = 1u << 6;  CHECK(c9_lpt_reply(&m, &im, 7) == 0x01);   /* P1 start -> mask 4 */
    im.bits = 1u << 11; CHECK(c9_lpt_reply(&m, &im, 7) == 0x02);   /* P2 start -> mask 8 */
    im.bits = 1u << 7;  CHECK(c9_lpt_reply(&m, &im, 5) == 0x05);   /* P1 trigger + on screen; P2 off */
    im.bits = 1u << 8;  CHECK(c9_lpt_reply(&m, &im, 5) == 0x01 && c9_lpt_reply(&m, &im, 1) == 0); /* P1 reload: off-screen pull */
    im.bits = 1u << 13; im.axis[2] = im.axis[3] = 100;
    CHECK(c9_lpt_reply(&m, &im, 5) == 0x14 && c9_lpt_reply(&m, &im, 3) == 0);                    /* P2 reload */
    im.axis[2] = im.axis[3] = 0;
    im.bits = 1u << 12; im.axis[2] = im.axis[3] = 100;
    CHECK(c9_lpt_reply(&m, &im, 5) == 0x54);                       /* P2 trigger + both on screen */
}

int main(int argc, char **argv) {
    test_hasp();
    test_lpt();
    for (int i = 1; i < argc; ++i) test_shipped(argv[i]);
    if (failures) {
        fprintf(stderr, "c9_unit: %d failure(s)\n", failures);
        return 1;
    }
    printf("c9_unit: ok (%d shipped drop-in(s))\n", argc - 1);
    return 0;
}
