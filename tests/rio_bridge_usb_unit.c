/* Eenheidstest voor de USB-aanhechting van de RIO-brug (tools/rio-bridge/rg_rio_bridge.c, 32-bits): het lightgun-bord als
 * USB-apparaat 0c70:0780, beantwoord in het geheugen van de game (Jurassic Park; Aliens Armageddon en Walking Dead zonder
 * versleuteling). Dit programma speelt de game: vier pmrt_usb-functies en een xrtea op vaste adressen (gebouwd zonder PIE),
 * die rio_bridge_usb_test.sh in een tabel zet. De brug haakt ze aan zoals in het spelbeeld; hier wordt gecontroleerd:
 *   1. het bord 0c70:0780 krijgt het handvat van de brug, een ander apparaat gaat via de trampoline naar de echte functie;
 *   2. een lezing geeft een rapport type 2 met de teller van de schakelaar en het 12-bits kanaal uit het bordbeeld, en de
 *      controlesom; met `versleuteling` versleuteld met de functie van de game, met `versleuteling geen` zoals het is;
 *   3. schrijven en sluiten op het bord worden door de brug aangenomen, op een ander apparaat gaan ze naar de echte functie.
 * Gebruik: rio_bridge_usb_unit <geen|xor> <bordbeeld>. Eigen code. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ANDER ((void *)0x1234)
unsigned char nep_sleutel[16] = {0x5a};

__attribute__((noinline, used)) void *nep_usb_open(int vid, int pid) {
    return (vid == 0x1111 && pid == 0x2222) ? ANDER : 0;
}
__attribute__((noinline, used)) int nep_usb_lees(void *dev, unsigned char *buf, int len) {
    if (dev != ANDER) return -1;
    memset(buf, 0xaa, (size_t)len);
    return 7;
}
__attribute__((noinline, used)) int nep_usb_schrijf(void *dev, unsigned char *buf, int len) {
    (void)buf; (void)len;
    return dev == ANDER ? 5 : -1;
}
__attribute__((noinline, used)) int nep_usb_sluit(void *dev) { return dev == ANDER ? 9 : -1; }
__attribute__((noinline, used)) void nep_xrtea(unsigned char *buf, int richting, unsigned char *sleutel) {
    (void)richting;
    for (int i = 0; i < 64; i++) buf[i] ^= sleutel[0];
}

/* Via volatile wijzers: de compiler mag de aanroepen niet vooraf uitrekenen of inlinen. */
static void *(*volatile p_open)(int, int) = nep_usb_open;
static int (*volatile p_lees)(void *, unsigned char *, int) = nep_usb_lees;
static int (*volatile p_schrijf)(void *, unsigned char *, int) = nep_usb_schrijf;
static int (*volatile p_sluit)(void *) = nep_usb_sluit;

static int fouten;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FOUT: " __VA_ARGS__); fputc('\n', stderr); fouten++; } } while (0)

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "gebruik: %s <geen|xor> <bordbeeld>\n", argv[0]); return 2; }
    const int versleuteld = !strcmp(argv[1], "xor");
    /* 1. wachten tot de brug de haken gezet heeft (zijn draad kijkt elke milliseconde) */
    void *bord = 0;
    for (int i = 0; i < 300 && !bord; i++) { bord = p_open(0x0c70, 0x0780); if (!bord) usleep(10000); }
    if (!bord) { fprintf(stderr, "FOUT: het bord 0c70:0780 kreeg geen handvat (geen haken gezet)\n"); return 1; }
    CHECK(bord != ANDER, "het bord kreeg het handvat van een ander apparaat");
    CHECK(p_open(0x1111, 0x2222) == ANDER, "een ander apparaat ging niet via de trampoline naar de echte open");
    CHECK(p_open(0x0c70, 0x0781) == 0, "een ander pid kreeg toch een handvat");
    unsigned char b[64];
    CHECK(p_lees(ANDER, b, 64) == 7 && b[0] == 0xaa && b[63] == 0xaa, "lezen op een ander apparaat ging niet naar de echte functie");
    CHECK(p_schrijf(ANDER, b, 64) == 5, "schrijven op een ander apparaat ging niet naar de echte functie");
    CHECK(p_sluit(ANDER) == 9, "sluiten op een ander apparaat ging niet naar de echte functie");

    /* 2. het bordbeeld: bit 4 (p1.start in RawThrillsGUN) ingedrukt, as op offset 12 helemaal rechts */
    unsigned char beeld[64] = {0};
    uint32_t bits = 1u << 4; memcpy(beeld + 8, &bits, 4);
    beeld[12] = 255;
    FILE *f = fopen(argv[2], "wb");
    if (!f || fwrite(beeld, 1, 64, f) != 64) { fprintf(stderr, "FOUT: bordbeeld niet te schrijven\n"); return 1; }
    fclose(f);
    usleep(100000);
    memset(b, 0, sizeof b);
    CHECK(p_lees(bord, b, 64) == 0, "een lezing op het bord gaf geen 0");
    unsigned char r[64]; memcpy(r, b, 64);
    if (versleuteld) {
        CHECK(b[0] != 2, "het rapport is niet versleuteld");
        nep_xrtea(r, -1, nep_sleutel);
    }
    CHECK(r[0] == 2, "rapporttype %d, verwacht 2", r[0]);
    CHECK(((r[8] >> 4) & 1) == 1, "schakelaar 7 (byte 8, hoog) staat niet op ingedrukt: 0x%02x", r[8]);
    int ch = ((r[14] >> 4) << 8) | r[16];
    CHECK(ch == 4095, "kanaal 19 = %d, verwacht 4095", ch);
    uint8_t som = 0; for (int i = 0; i < 63; i++) som += r[i];
    CHECK(r[63] == som, "controlesom 0x%02x, verwacht 0x%02x", r[63], som);

    /* loslaten: de teller wordt even */
    memset(beeld + 8, 0, 4);
    f = fopen(argv[2], "wb"); fwrite(beeld, 1, 64, f); fclose(f);
    usleep(100000);
    CHECK(p_lees(bord, b, 64) == 0, "tweede lezing gaf geen 0");
    if (versleuteld) nep_xrtea(b, -1, nep_sleutel);
    CHECK(((b[8] >> 4) & 0xf) == 2, "schakelaar 7 na loslaten: teller %d, verwacht 2", (b[8] >> 4) & 0xf);
    CHECK(b[1] != r[1] || b[2] != r[2], "het volgnummer loopt niet op");

    /* 3. schrijven en sluiten op het bord neemt de brug aan */
    CHECK(p_schrijf(bord, b, 64) == 0, "schrijven op het bord ging niet naar de brug");
    CHECK(p_sluit(bord) == 0, "sluiten op het bord ging niet naar de brug");
    if (fouten) return 1;
    printf("usb-bord (%s): handvat, trampolines, rapport, controlesom en tellers in orde\n", versleuteld ? "versleuteld" : "zonder versleuteling");
    return 0;
}
