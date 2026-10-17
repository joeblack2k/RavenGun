/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Aanraking voor Infinity Blade FX als zuivere logica: van trekker en richtpunt uit het gedeelde geheugen naar de punten die de
 * PQ-Labs-client (PQMTClient.dll, "TouchFX" in SwordGame.exe) aan de game doorgeeft. Geen Windows-aanroepen; dezelfde bron wordt
 * door de DLL (rg_pqmt.c) en de eenheidstest (tests/pqmt_unit.c) gebruikt.
 *
 * Gemeten in SwordGame.exe (statisch, niets uitgevoerd): de callback die de game bij SetOnReceivePointFrame aanmeldt (0x1cde110)
 * krijgt cdecl (frame_id, time_stamp, aantal, const TouchPoint*, ctx) en geeft ze door aan 0x1cdd380, dat van het EERSTE punt
 * bewaart: u16 gebeurtenis, u16 frame_id, dword x (= veld 1), dword y (= veld 2). TouchPoint is dus 16 bytes: {int gebeurtenis;
 * int x; int y; int rest;} (de operator= van PQMTClient.dll kopieert 4 dwords). De gebeurtenis: 0 = neer, 1 = beweegt, 2 = op
 * (switch op 0x1ce4da2-0x1ce4dba). De x en y worden door de game als muispositie in de client-rechthoek van het venster gebruikt
 * (zonder aanraking valt hij terug op GetCursorPos + ScreenToClient), dus het zijn pixels van het venster.
 */
#ifndef RG_PQMT_LOGIC_H
#define RG_PQMT_LOGIC_H

#include <stdint.h>

enum { PQ_NEER = 0, PQ_BEWEEGT = 1, PQ_OP = 2 };

typedef struct {
    int32_t gebeurtenis;
    int32_t x;
    int32_t y;
    int32_t rest;
} pq_punt;

typedef struct {
    int aanraking;      /* 1 zolang een vinger "neer" is */
    int32_t laatste_x, laatste_y;
    int32_t frame;
} pq_toestand;

/* Een as van 0 of 1 / 254 of 255 betekent: buiten het scherm (offscreen-regel van de indeling TimeCrisis5 in tp-shm-profiles.json). */
static inline int pq_binnen(uint8_t as) { return as >= 2 && as <= 253; }

static inline int32_t pq_pixels(uint8_t as, int32_t grootte) {
    if (grootte < 2) return 0;
    int32_t v = (int32_t)(((int64_t)as * (grootte - 1) + 127) / 255);
    return v < 0 ? 0 : (v > grootte - 1 ? grootte - 1 : v);
}

/* Een stap van de lus. ctl = het woord op offset 8, trekker_masker = de aanrakingsknop (0x10 in TimeCrisis5), ax/ay = offset 12/16,
   w/h = de client-rechthoek van het venster in pixels. Geeft 1 als er een frame met een punt naar de game moet (uit[0] gevuld,
   *frame_id is het nummer), anders 0. Een frame gaat alleen uit bij neer, bij een verplaatsing en bij op. */
static inline int pq_stap(pq_toestand* t, uint32_t ctl, uint32_t trekker_masker, uint8_t ax, uint8_t ay, int32_t w, int32_t h,
                          pq_punt* uit, int32_t* frame_id) {
    const int ingedrukt = (ctl & trekker_masker) != 0;
    const int binnen = pq_binnen(ax) && pq_binnen(ay);
    if (!t->aanraking) {
        if (!(ingedrukt && binnen)) return 0;
        t->aanraking = 1;
        t->laatste_x = pq_pixels(ax, w);
        t->laatste_y = pq_pixels(ay, h);
        uit->gebeurtenis = PQ_NEER;
    } else if (!ingedrukt || !binnen) {
        t->aanraking = 0;                    /* op, op de laatste plek */
        uit->gebeurtenis = PQ_OP;
    } else {
        const int32_t x = pq_pixels(ax, w), y = pq_pixels(ay, h);
        if (x == t->laatste_x && y == t->laatste_y) return 0;
        t->laatste_x = x;
        t->laatste_y = y;
        uit->gebeurtenis = PQ_BEWEEGT;
    }
    uit->x = t->laatste_x;
    uit->y = t->laatste_y;
    uit->rest = 0;
    *frame_id = ++t->frame;
    return 1;
}

#endif
