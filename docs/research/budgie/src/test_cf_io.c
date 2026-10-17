/* SPDX-License-Identifier: GPL-3.0-or-later
 * Eigen toets voor rg_cf_io.c (header-vrij, i386). Gebruik: zie docs/research/budgie/src/build.sh.
 * Draait in een werkmap waar adjust.dat staat (het script maakt een proef-adjust.dat met een bekend serienummer). */
extern long read(int, void *, unsigned long);
extern long write(int, const void *, unsigned long);
extern int open(const char *, int, ...);
extern int close(int);
extern int ioctl(int, unsigned long, ...);
extern int iopl(int);
extern int usleep(unsigned int);
extern int printf(const char *, ...);

static int fouten;
#define CHECK(x) do { if (!(x)) { printf("FOUT regel %d: %s\n", __LINE__, #x); ++fouten; } } while (0)

int main(void)
{
    CHECK(iopl(3) == 0);

    /* /dev/p37c: adres 0x81 zetten, data 0x5a in twee nibbles schrijven, terugleze n */
    int p = open("/dev/p37c", 2);
    CHECK(p > 2);
    unsigned char b;
    b = 0x21; CHECK(write(p, &b, 1) == 1);   /* lage nibble van het adres = 1 */
    b = 0x38; CHECK(write(p, &b, 1) == 1);   /* hoge nibble = 8 -> adres 0x81 */
    b = 0x0a; write(p, &b, 1);               /* data, lage nibble a */
    b = 0x15; write(p, &b, 1);               /* data, hoge nibble 5 */
    unsigned char r = 0;
    CHECK(read(p, &r, 1) == 1);
    CHECK(r == 0x5a);
    close(p);

    /* /dev/mjg: een record per 10 ms, daartussen 0; ioctl slaagt */
    int m = open("/dev/mjg", 2);
    CHECK(m > 2);
    unsigned char rec[0xb8];
    CHECK(ioctl(m, 0x40044d03, 0x2001) == 0);
    usleep(15000);
    CHECK(read(m, rec, sizeof rec) == 0xb8);
    unsigned int c1 = *(unsigned int *)(rec + 0x3c);
    CHECK(read(m, rec, sizeof rec) == 0);    /* direct erna: niets nieuws */
    usleep(15000);
    CHECK(read(m, rec, sizeof rec) == 0xb8);
    CHECK(*(unsigned int *)(rec + 0x3c) == c1 + 1);
    CHECK(*(unsigned int *)(rec + 0x38) == 0x7d);
    CHECK(read(m, rec, 10) == 0);            /* te kleine buffer: zoals mj_read */
    close(m);

    /* /dev/hda: serienummer uit adjust.dat op offset 0x14 */
    int h = open("/dev/hda", 0x800);
    CHECK(h > 2);
    unsigned char id[512];
    CHECK(ioctl(h, 0x30d, id) == 0);
    CHECK(id[0x14] == 'E' && id[0x15] == 'M' && id[0x16] == '-');
    CHECK(id[0x14 + 15] == 0);
    close(h);

    /* een gewoon bestand gaat ongemoeid naar de kernel */
    int n = open("/dev/null", 2);
    CHECK(n > 2);
    CHECK(write(n, "x", 1) == 1);
    close(n);

    printf(fouten ? "MISLUKT: %d fouten\n" : "ALLES GOED\n", fouten);
    return fouten != 0;
}
