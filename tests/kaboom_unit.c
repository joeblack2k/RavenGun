/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest voor het Kaboom-protocol (tools/rg-kaboom/kaboom_proto.c): pakketopbouw, controlesom, de antwoorden per commando,
 * de munt als stijgende flank en het weigeren van kapotte pakketten. Gebouwd door tests/kaboom_test.sh.
 */
#include <stdio.h>
#include <string.h>

#include "kaboom_proto.h"

static int fouten;
#define CHECK(c, ...) do { if (!(c)) { fouten++; printf("FAIL (regel %d): ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static size_t maak_pakket(uint8_t* p, uint16_t magic, uint16_t cmd, const uint8_t* lading, uint16_t len) {
    p[0] = (uint8_t)magic; p[1] = (uint8_t)(magic >> 8);
    p[4] = (uint8_t)len; p[5] = (uint8_t)(len >> 8);
    p[6] = (uint8_t)cmd; p[7] = (uint8_t)(cmd >> 8);
    if (len) memcpy(p + 8, lading, len);
    const uint16_t cs = kb_controlesom(p + 4, (size_t)len + 4);
    p[2] = (uint8_t)cs; p[3] = (uint8_t)(cs >> 8);
    return 8u + len;
}

/* Stuurt een aanvraag en leest het antwoord zoals de game: eerst 8 bytes kop, dan de lading. */
static int vraag(kb_bord* b, uint16_t cmd, const uint8_t* lading, uint16_t len, uint8_t* uit, uint16_t* ulen) {
    uint8_t p[8 + 16];
    const size_t n = maak_pakket(p, KB_MAGIC_HOST_A, cmd, lading, len);
    kb_schrijf(b, p, n);
    uint8_t kop[8];
    if (kb_lees(b, kop, 8) != 8) return 0;
    if (kop[0] != 0x0F || kop[1] != 0xB0) return -1;            /* 0xB00F, little endian */
    *ulen = (uint16_t)(kop[4] | (kop[5] << 8));
    if (kop[6] != cmd || kop[7] != 0) return -2;
    if (*ulen && kb_lees(b, uit, *ulen) != *ulen) return -3;
    uint8_t som[4 + 32];
    memcpy(som, kop + 4, 4);
    memcpy(som + 4, uit, *ulen);
    if (kb_controlesom(som, (size_t)*ulen + 4) != (uint16_t)(kop[2] | (kop[3] << 8))) return -4;
    return 1;
}

int main(void) {
    kb_bord b;
    uint8_t u[32];
    uint16_t ul;

    /* controlesom: som van de bytes, 16 bits */
    {
        const uint8_t x[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x03};
        CHECK(kb_controlesom(x, 5) == (uint16_t)(0xFF * 4 + 3), "som over 16 bits");
        uint8_t groot[600];
        memset(groot, 0xFF, sizeof groot);
        CHECK(kb_controlesom(groot, 600) == (uint16_t)(600 * 255), "som loopt over (mod 65536)");
    }

    /* versie/status: 18 bytes, toetsentellers op 12..15 */
    kb_init(&b, 0x08);
    b.toetsen[0] = 1; b.toetsen[1] = 2; b.toetsen[2] = 3; b.toetsen[3] = 4;
    CHECK(vraag(&b, KB_CMD_VERSIE, NULL, 0, u, &ul) == 1 && ul == 18, "cmd 6 geeft 18 bytes (kreeg %u)", ul);
    CHECK(u[12] == 1 && u[13] == 2 && u[14] == 3 && u[15] == 4, "toetsentellers op 12..15");

    /* munten: een stijgende flank telt een keer, vasthouden telt niet door, loslaten en opnieuw telt weer */
    kb_controlewoord(&b, 0);
    kb_controlewoord(&b, 0x08);
    kb_controlewoord(&b, 0x08);
    kb_controlewoord(&b, 0x18);
    CHECK(b.munten[0] == 1, "een flank, een munt (kreeg %u)", b.munten[0]);
    kb_controlewoord(&b, 0x10);
    kb_controlewoord(&b, 0x08);
    CHECK(b.munten[0] == 2, "tweede flank, tweede munt (kreeg %u)", b.munten[0]);
    kb_controlewoord(&b, 0x20);                          /* een ander bit telt niet */
    CHECK(b.munten[0] == 2, "andere bits tellen niet");
    CHECK(vraag(&b, KB_CMD_MUNTEN, NULL, 0, u, &ul) == 1 && ul == 4, "cmd 0xC geeft 4 bytes");
    CHECK(u[0] == 2 && u[1] == 0 && u[2] == 0 && u[3] == 0, "tellers 2 en 0 (kreeg %u %u)", u[0], u[2]);
    CHECK(vraag(&b, KB_CMD_RESET_MUNTEN, NULL, 0, u, &ul) == 1 && ul == 4 && u[0] == 2, "reset geeft de teller van voor de reset");
    CHECK(vraag(&b, KB_CMD_MUNTEN, NULL, 0, u, &ul) == 1 && u[0] == 0, "na de reset is de teller 0");

    /* kaartjes: zet 5, lees 5; uitgifte-teller en reset */
    {
        const uint8_t v[2] = {5, 0};
        CHECK(vraag(&b, KB_CMD_ZET_KAARTJES, v, 2, u, &ul) == 1 && ul == 0, "cmd 9 geeft een lege bevestiging");
        CHECK(vraag(&b, KB_CMD_KAARTJES_TE_GEVEN, NULL, 0, u, &ul) == 1 && ul == 2 && u[0] == 5, "cmd 8 geeft 5 terug");
        CHECK(vraag(&b, KB_CMD_KAARTJES_UIT, NULL, 0, u, &ul) == 1 && ul == 2 && u[0] == 0, "cmd 0xA geeft 2 bytes, teller 0");
        CHECK(vraag(&b, KB_CMD_RESET_KAARTJES, NULL, 0, u, &ul) == 1 && ul == 0, "cmd 0xB geeft een lege bevestiging");
    }

    /* onbekend commando: lege bevestiging met hetzelfde commando */
    CHECK(vraag(&b, 0x14, NULL, 0, u, &ul) == 1 && ul == 0, "onbekend commando krijgt een lege bevestiging");

    /* kapotte pakketten: verkeerde controlesom en verkeerde magic krijgen geen antwoord */
    {
        uint8_t p[8];
        const unsigned af = b.afgekeurd;
        maak_pakket(p, KB_MAGIC_HOST_A, KB_CMD_MUNTEN, NULL, 0);
        p[2] ^= 0x55;
        kb_schrijf(&b, p, 8);
        CHECK(kb_lees(&b, u, 8) == 0 && b.afgekeurd == af + 1, "verkeerde controlesom: geen antwoord");
        maak_pakket(p, 0x1234, KB_CMD_MUNTEN, NULL, 0);
        kb_schrijf(&b, p, 8);
        CHECK(kb_lees(&b, u, 8) == 0 && b.afgekeurd == af + 2, "verkeerde magic: geen antwoord");
        /* en daarna werkt een goed pakket weer */
        CHECK(vraag(&b, KB_CMD_MUNTEN, NULL, 0, u, &ul) == 1, "herstel na een kapot pakket");
    }

    /* de alternatieve host-magic van de exe (0x9A44) wordt ook aanvaard */
    {
        uint8_t p[8];
        maak_pakket(p, KB_MAGIC_HOST_B, KB_CMD_MUNTEN, NULL, 0);
        kb_schrijf(&b, p, 8);
        CHECK(kb_lees(&b, u, 8) == 8, "magic 0x9A44 aanvaard");
        kb_spoel(&b);                                   /* de 4 bytes lading van het antwoord laten we liggen */
    }

    /* een pakket in stukjes (byte voor byte, zoals een seriele lijn) wordt pas aan het eind beantwoord */
    {
        uint8_t p[8];
        maak_pakket(p, KB_MAGIC_HOST_A, KB_CMD_MUNTEN, NULL, 0);
        for (int i = 0; i < 7; i++) kb_schrijf(&b, p + i, 1);
        CHECK(kb_lees(&b, u, 8) == 0, "half pakket: nog geen antwoord");
        kb_schrijf(&b, p + 7, 1);
        CHECK(kb_lees(&b, u, 8) == 8, "heel pakket: antwoord");
    }

    /* PurgeComm gooit wachtrij en half pakket weg */
    {
        uint8_t p[8];
        maak_pakket(p, KB_MAGIC_HOST_A, KB_CMD_MUNTEN, NULL, 0);
        kb_schrijf(&b, p, 8);
        kb_spoel(&b);
        CHECK(kb_lees(&b, u, 8) == 0, "na spoelen is de wachtrij leeg");
    }

    /* lees van minder bytes dan er staan: de rest blijft in de wachtrij */
    {
        uint8_t p[8];
        maak_pakket(p, KB_MAGIC_HOST_A, KB_CMD_MUNTEN, NULL, 0);
        kb_schrijf(&b, p, 8);
        CHECK(kb_lees(&b, u, 3) == 3 && kb_lees(&b, u, 100) == 9, "deellezen (8 + 4 lading = 12 bytes: 3 + 9)");
    }

    if (fouten) { printf("%d fouten\n", fouten); return 1; }
    printf("kaboom_unit: ok\n");
    return 0;
}
