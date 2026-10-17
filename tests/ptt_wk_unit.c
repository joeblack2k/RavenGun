/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Eenheidstest van tools/rg-ptt/rg_ptt_wk_logic.h: de keuze van onze WkWin32.dll bij WkbCrypt2 (Pull The Trigger). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "rg_ptt_wk_logic.h"

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static uint32_t rd(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int main(void)
{
    unsigned char b[PTT_BLOCK_SIZE];
    unsigned char klein[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char kopie[8];
    unsigned i, n;

    CHECK(PTT_BLOCK_SIZE == 280, "280 bytes = 14 descriptors");
    CHECK(PTT_BLOCK_SIZE % 20 == 0, "veelvoud van 20");

    /* 1. versleutelde tabel wordt de platte tekst */
    memcpy(b, ptt_cipher, sizeof b);
    CHECK(ptt_crypt(b, sizeof b) == PTT_CRYPT_PLAIN, "cipher -> plain");
    CHECK(memcmp(b, ptt_plain, sizeof b) == 0, "inhoud is de platte tekst");
    /* 2. tweede aanroep: ongemoeid */
    CHECK(ptt_crypt(b, sizeof b) == PTT_CRYPT_SAME, "tweede keer: al plat");
    CHECK(memcmp(b, ptt_plain, sizeof b) == 0, "nog steeds de platte tekst");
    /* 3. andere inhoud van dezelfde grootte: niet aanraken */
    memset(b, 0x5a, sizeof b);
    CHECK(ptt_crypt(b, sizeof b) == PTT_CRYPT_OTHER, "onbekende inhoud");
    for (i = 0; i < sizeof b; i++) if (b[i] != 0x5a) { CHECK(0, "onbekende inhoud is gewijzigd"); break; }
    /* 4. controlevraag van 8 bytes en nulwijzer */
    memcpy(kopie, klein, 8);
    CHECK(ptt_crypt(klein, 8) == PTT_CRYPT_OTHER, "8 bytes");
    CHECK(memcmp(kopie, klein, 8) == 0, "8 bytes ongewijzigd");
    CHECK(ptt_crypt(0, PTT_BLOCK_SIZE) == PTT_CRYPT_OTHER, "nulwijzer");

    /* 5. de platte tekst is een geldige importtabel: 13 descriptors + nul, ELKE verwijzing in de sectie van de tabel */
    n = PTT_BLOCK_SIZE / 20;
    for (i = 0; i < n - 1; i++) {
        const unsigned char *d = ptt_plain + 20 * i;
        uint32_t oft = rd(d), tds = rd(d + 4), fwd = rd(d + 8), name = rd(d + 12), ft = rd(d + 16);
        CHECK(oft >= 0xb1000 && oft < 0xc0000, "OFT in .wkcrypt-sectie");
        CHECK(ft >= 0xb1000 && ft < 0xc0000, "FT in .wkcrypt-sectie");
        CHECK(name >= 0xb1000 && name < 0xc0000, "Name in .wkcrypt-sectie");
        CHECK(tds == 0 && fwd == 0, "geen bound import, geen forwarder");
    }
    for (i = 0; i < 20; i++) CHECK(ptt_plain[20 * (n - 1) + i] == 0, "laatste descriptor is nul");
    /* de cipher is geen geldige tabel (anders was er niets te ontsleutelen) */
    CHECK(rd(ptt_cipher + 12 + 20 * 12) != 0 && !(rd(ptt_cipher + 12) >= 0xb1000 && rd(ptt_cipher + 12) < 0xc0000),
          "de cipher lijkt niet op een tabel");

    if (fails) { printf("%d mislukt\n", fails); return 1; }
    printf("ptt_wk_unit: ok\n");
    return 0;
}
