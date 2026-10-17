/* SPDX-License-Identifier: GPL-3.0-or-later
 * Zie kaboom_proto.h. */
#include "kaboom_proto.h"

#include <string.h>

void kb_init(kb_bord* b, uint32_t munt_bit) {
    memset(b, 0, sizeof *b);
    b->munt_bit = munt_bit;
}

uint16_t kb_controlesom(const uint8_t* p, size_t n) {
    uint16_t s = 0;
    for (size_t i = 0; i < n; i++) s = (uint16_t)(s + p[i]);
    return s;
}

void kb_controlewoord(kb_bord* b, uint32_t woord) {
    if ((woord & b->munt_bit) && !(b->vorige_control & b->munt_bit) && b->munten[0] < 0xFFFF) b->munten[0]++;
    b->vorige_control = woord;
}

static void zet16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t haal16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Zet een antwoordpakket achter in de wachtrij. */
static void antwoord(kb_bord* b, uint16_t cmd, const uint8_t* lading, uint16_t len) {
    if (b->antwoord_pos == b->antwoord_len) b->antwoord_pos = b->antwoord_len = 0;
    if (b->antwoord_len + KB_KOP + len > sizeof b->antwoord) return;   /* de game leest niet door: niets meer bijschrijven */
    uint8_t* p = b->antwoord + b->antwoord_len;
    zet16(p, KB_MAGIC_BORD);
    zet16(p + 4, len);
    zet16(p + 6, cmd);
    if (len) memcpy(p + 8, lading, len);
    zet16(p + 2, kb_controlesom(p + 4, (size_t)len + 4));
    b->antwoord_len += KB_KOP + len;
}

static void verwerk(kb_bord* b, uint16_t cmd, const uint8_t* lading, uint16_t len) {
    uint8_t u[20];
    memset(u, 0, sizeof u);
    switch (cmd) {
    case KB_CMD_VERSIE:
        memcpy(u + 12, b->toetsen, 4);
        antwoord(b, cmd, u, 18);
        break;
    case KB_CMD_KAARTJES_TE_GEVEN:
        zet16(u, b->kaartjes_te_geven);
        antwoord(b, cmd, u, 2);
        break;
    case KB_CMD_ZET_KAARTJES:
        if (len >= 2) b->kaartjes_te_geven = haal16(lading);
        antwoord(b, cmd, u, 0);
        break;
    case KB_CMD_KAARTJES_UIT:
        zet16(u, b->kaartjes_uitgegeven);
        antwoord(b, cmd, u, 2);          /* 2 bytes: geen "bezig"-vlag; de game leest dan 0 */
        break;
    case KB_CMD_RESET_KAARTJES:
        b->kaartjes_uitgegeven = 0;
        antwoord(b, cmd, u, 0);
        break;
    case KB_CMD_MUNTEN:
        zet16(u, b->munten[0]);
        zet16(u + 2, b->munten[1]);
        antwoord(b, cmd, u, 4);
        break;
    case KB_CMD_RESET_MUNTEN:
        zet16(u, b->munten[0]);
        zet16(u + 2, b->munten[1]);
        b->munten[0] = b->munten[1] = 0;
        antwoord(b, cmd, u, 4);
        break;
    default:
        antwoord(b, cmd, u, 0);          /* een onbekend commando krijgt een lege bevestiging in plaats van stilte */
        break;
    }
}

void kb_schrijf(kb_bord* b, const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (b->aanvraag_len < sizeof b->aanvraag) b->aanvraag[b->aanvraag_len++] = data[i];
        if (b->aanvraag_len < KB_KOP) continue;
        const uint16_t len = haal16(b->aanvraag + 4);
        if (len > KB_MAX_LADING) { b->afgekeurd++; b->aanvraag_len = 0; continue; }
        if (b->aanvraag_len < KB_KOP + len) continue;
        const uint16_t magic = haal16(b->aanvraag);
        const int ok = (magic == KB_MAGIC_HOST_A || magic == KB_MAGIC_HOST_B) &&
                       haal16(b->aanvraag + 2) == kb_controlesom(b->aanvraag + 4, (size_t)len + 4);
        if (ok) {
            b->verwerkt++;
            verwerk(b, haal16(b->aanvraag + 6), b->aanvraag + 8, len);
        } else {
            b->afgekeurd++;
        }
        b->aanvraag_len = 0;
    }
}

size_t kb_lees(kb_bord* b, uint8_t* uit, size_t n) {
    const size_t beschikbaar = b->antwoord_len - b->antwoord_pos;
    const size_t k = n < beschikbaar ? n : beschikbaar;
    if (k) memcpy(uit, b->antwoord + b->antwoord_pos, k);
    b->antwoord_pos += k;
    if (b->antwoord_pos == b->antwoord_len) b->antwoord_pos = b->antwoord_len = 0;
    return k;
}

void kb_spoel(kb_bord* b) {
    b->aanvraag_len = 0;
    b->antwoord_len = b->antwoord_pos = 0;
}
