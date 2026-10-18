/* Proefproces voor tests/cf_io_test.sh: praat met librg-cf-io.so (LD_PRELOAD) zoals de game van Crossfire het doet. Geen spel.
 *   - de iButton: dezelfde byte-reeks als de controle op 0x805e7ca van med_pball (reset, 0x33, 0x66, controlebyte, ID, wachtwoord, data)
 *     over /dev/p37c, met de verwachte constanten in een eigen geheugenstuk (RG_CF_TEST_MEM);
 *   - /dev/mjg: records uit een proef-bordbeeld; irq-teller strikt oplopend, munttellers, schot, richtpunt zoals 0x804a63b het terugrekent;
 *   - /dev/hda: het serienummer uit adjust.dat. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int p37;
static void poort(unsigned char b) { if (write(p37, &b, 1) != 1) fails++; }
static void reg_schrijf(unsigned a, unsigned v)
{
    poort((unsigned char)(0x20 | (a & 15))); poort((unsigned char)(0x30 | ((a >> 4) & 15)));
    poort((unsigned char)(v & 15)); poort((unsigned char)(0x10 | ((v >> 4) & 15)));
}
static unsigned reg_lees(unsigned a)
{
    poort((unsigned char)(0x20 | (a & 15))); poort((unsigned char)(0x30 | ((a >> 4) & 15)));
    unsigned char b = 0xee;
    if (read(p37, &b, 1) != 1) fails++;
    return b;
}
static void ow_byte_uit(unsigned b) { reg_schrijf(0x10, 0x20 | ((b >> 4) & 15)); reg_schrijf(0x10, 0x30 | (b & 15)); while (reg_lees(0x81) & 15) {} }
static unsigned ow_byte_in(void) { reg_schrijf(0x10, 0x10); while (reg_lees(0x81) & 15) {} return reg_lees(0x80); }

static unsigned char crc8(const unsigned char *p, int n)
{
    unsigned char c = 0;
    for (int i = 0; i < n; i++) { unsigned char b = p[i]; for (int k = 0; k < 8; k++) { unsigned char m = (c ^ b) & 1; c >>= 1; if (m) c ^= 0x8c; b >>= 1; } }
    return c;
}

static void ibutton(const unsigned char *id, const unsigned char *data, int sub)
{
    reg_schrijf(0x10, 0x00);
    CHECK(reg_lees(0x81) & 0x10, "geen presence na reset");
    ow_byte_uit(0x33);
    unsigned char rom[8];
    for (int i = 0; i < 8; i++) rom[i] = (unsigned char)ow_byte_in();
    CHECK(rom[0] == 0x02, "familiecode %02x", rom[0]);
    CHECK(crc8(rom, 7) == rom[7], "crc van het ROM klopt niet");
    ow_byte_uit(0x66);
    unsigned ctrl = (unsigned)(((sub & 3) << 6) | 0x10);
    ow_byte_uit(ctrl);
    ow_byte_uit(~ctrl & 0xff);
    unsigned char g[48];
    for (int i = 0; i < 8; i++) g[i] = (unsigned char)ow_byte_in();
    CHECK(memcmp(g, id, 8) == 0, "ID van subsleutel %d", sub);
    for (int i = 0; i < 8; i++) ow_byte_uit((unsigned)(0x40 + i));
    for (int i = 0; i < 48; i++) g[i] = (unsigned char)ow_byte_in();
    CHECK(memcmp(g, data, 48) == 0, "data van subsleutel %d", sub);
}

/* zoals de game een record verwerkt (0x804a63b): een geweer -> scherm_x, scherm_y of -1 */
static void spel_richtpunt(const unsigned char *rec, int gun, int *x, int *y)
{
    unsigned misc = *(const unsigned *)rec;
    int n = (int)((misc >> (gun ? 4 : 0)) & 7);
    if (n == 0) { *x = *y = -1; return; }
    int first = n - 4 < 0 ? 0 : n - 4, cnt = 0;
    double sx = 0, sy = 0;
    for (int i = first; i <= n - 1; i++) {
        unsigned v = *(const unsigned *)(rec + 0x10 + (gun * 4 + (i & 3)) * 4);
        sx += v & 0xfff; sy += (v >> 12) & 0xfff; cnt++;
    }
    *x = (int)(sx / cnt * 0.6946107745170593f + -170.0f);
    *y = (int)(sy / cnt + -25.0f);
}

static void zet_beeld(int fd, unsigned bits, int p1x, int p1y)
{
    unsigned char b[64];
    memset(b, 0, sizeof b);
    memcpy(b + 8, &bits, 4);
    b[12] = (unsigned char)p1x; b[16] = (unsigned char)p1y; b[20] = 0; b[24] = 0;
    if (pwrite(fd, b, sizeof b, 0) != 64) fails++;
}

static int lees_record(int fd, unsigned char *rec)
{
    for (int t = 0; t < 100; t++) {
        long r = read(fd, rec, 0xb8);
        if (r == 0xb8) return 1;
        usleep(5000);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const char *shm = argv[1], *adj = argv[2];
    /* eigen geheugen onder de game-adressen: 0x806fbc0.. -> buf */
    unsigned char *buf = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned char id0[8] = {0xaa, 0x55, 1, 2, 3, 4, 5, 6}, id1[8] = {0xaa, 0x55, 6, 7, 8, 9, 10, 11}, d0[48], d1[48];
    for (int i = 0; i < 48; i++) { d0[i] = (unsigned char)(0x20 + i); d1[i] = (unsigned char)(0x90 + i); }
    memcpy(buf + 0x08, id0, 8); memcpy(buf + 0x20, d0, 48); memcpy(buf + 0x58, id1, 8); memcpy(buf + 0x60, d1, 48);
    char v[32];
    snprintf(v, sizeof v, "%lx", (unsigned long)((uintptr_t)buf - 0x806fbc0u));
    setenv("RG_CF_TEST_MEM", v, 1);
    setenv("RG_CF_SHM", shm, 1);
    setenv("RG_CF_LOG", "1", 1);
    int sfd = open(shm, O_RDWR);
    if (sfd < 0) return 3;
    zet_beeld(sfd, 0, 128, 128);

    p37 = open("/dev/p37c", O_RDWR);
    CHECK(p37 > 0, "open /dev/p37c");
    ibutton(id0, d0, 0);
    ibutton(id1, d1, 1);

    int hda = open("/dev/hda", O_NONBLOCK);
    CHECK(hda > 0, "open /dev/hda");
    unsigned char ident[512];
    CHECK(ioctl(hda, 0x30d, ident) == 0, "HDIO_GET_IDENTITY");
    FILE *f = fopen(adj, "rb");
    char a[24] = {0};
    if (f) { if (fread(a, 1, 20, f) == 0) fails++; fclose(f); }
    CHECK(memcmp(ident + 0x14, a, 20) == 0, "serienummer komt niet uit adjust.dat");

    int mjg = open("/dev/mjg", O_RDWR);
    CHECK(mjg > 0, "open /dev/mjg");
    unsigned char rec[0xb8];
    unsigned lastirq = 0;
    CHECK(lees_record(mjg, rec), "geen eerste record");
    lastirq = *(unsigned *)(rec + 0xc);
    int x, y;
    spel_richtpunt(rec, 0, &x, &y);
    CHECK(abs(x - 320) <= 3 && abs(y - 240) <= 3, "richtpunt midden: %d,%d", x, y);   /* byte 128 -> bijna het midden van 640x480 */
    CHECK((*(unsigned *)rec & 0x3000) == 0, "schotbit zonder trekker");
    for (int i = 0; i < 5; i++) { CHECK(lees_record(mjg, rec), "record %d", i); unsigned q = *(unsigned *)(rec + 0xc); CHECK(q > lastirq, "irq-teller niet strikt oplopend"); lastirq = q; }

    /* munt 1 (bit 2) tweemaal, START 1 (bit 4) vast, trekker 1 (bit 5) eenmaal */
    zet_beeld(sfd, 1u << 2, 2, 253);
    lees_record(mjg, rec);
    zet_beeld(sfd, 0, 2, 253);
    lees_record(mjg, rec);
    zet_beeld(sfd, 1u << 2, 2, 253);
    lees_record(mjg, rec);
    CHECK(*(unsigned *)(rec + 0x30) == 2, "muntenteller %u", *(unsigned *)(rec + 0x30));
    CHECK(*(unsigned *)(rec + 0x04) & 0x200000u, "munt-bit niet gezet");
    spel_richtpunt(rec, 0, &x, &y);
    CHECK(abs(x - 0) <= 3 && abs(y - 480) <= 3, "richtpunt linksonder: %d,%d", x, y);
    zet_beeld(sfd, (1u << 4) | (1u << 5), 253, 2);
    int schoten = 0, startgezien = 0;
    for (int i = 0; i < 6; i++) {
        lees_record(mjg, rec);
        if (*(unsigned *)rec & 0x1000) schoten++;
        if (*(unsigned *)(rec + 4) & 0x80000u) startgezien = 1;
    }
    CHECK(schoten == 1, "trekker gaf %d schoten in plaats van 1", schoten);
    CHECK(startgezien, "START-bit niet gezet");
    spel_richtpunt(rec, 0, &x, &y);
    CHECK(abs(x - 640) <= 3 && abs(y - 0) <= 3, "richtpunt rechtsboven: %d,%d", x, y);
    /* buiten beeld: geen samples */
    zet_beeld(sfd, 0, 255, 255);
    lees_record(mjg, rec);
    spel_richtpunt(rec, 0, &x, &y);
    CHECK(x == -1 && y == -1, "buiten beeld moet -1,-1 geven: %d,%d", x, y);
    if (fails) { fprintf(stderr, "%d fouten\n", fails); return 1; }
    puts("ALLES GOED");
    return 0;
}
