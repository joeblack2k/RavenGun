/* SPDX-License-Identifier: GPL-3.0-or-later
 * Eenheidstest van de rekenregel van onze Usbioextreme.dll voor Far Cry: Paradise Lost (tools/rg-farcry/rg_usbio_logic.h):
 * bordbeeld van de daemon (tp-shm FarCryPipe) naar het record van 0x34 bytes. De posities komen uit de ontleding van
 * Farcry_R.exe (functie op 0x7af480, tabel op 0x7ac66c, teller-lezer op 0x7ac7e0); deze test legt ze vast.
 */
#include <stdio.h>
#include <string.h>
#include "../tools/rg-farcry/rg_usbio_logic.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void put_ctl(unsigned char *shm, unsigned ctl)
{
    shm[8] = (unsigned char)ctl;
    shm[9] = (unsigned char)(ctl >> 8);
    shm[10] = (unsigned char)(ctl >> 16);
    shm[11] = (unsigned char)(ctl >> 24);
}

static unsigned rd32(const unsigned char *r, unsigned o)
{
    return r[o] | (r[o + 1] << 8) | (r[o + 2] << 16) | ((unsigned)r[o + 3] << 24);
}

int main(void)
{
    rg_usbio_state s;
    unsigned char shm[64], rec[RG_REC_SIZE];

    CHECK(RG_REC_SIZE == 0x34);

    /* assen: nooit 0 (de game neemt 0 + 0 voor "geen volgsignaal"), 0 -> 1, 255 -> 255, midden in het midden */
    CHECK(rg_usbio_axis(0, 0) == 1);
    CHECK(rg_usbio_axis(255, 0) == 255);
    CHECK(rg_usbio_axis(128, 0) >= 127 && rg_usbio_axis(128, 0) <= 129);
    CHECK(rg_usbio_axis(0, 1) == 255);
    CHECK(rg_usbio_axis(255, 1) == 1);
    { int v, prev = 0, ok = 1; for (v = 0; v < 256; v++) { int a = rg_usbio_axis((unsigned char)v, 0); if (a < prev || a < 1) ok = 0; prev = a; } CHECK(ok); }

    /* plaatsing van de assen in het record: byte 7 = X1, 6 = Y1, 9 = X2, 8 = Y2 */
    rg_usbio_state_init(&s);
    memset(shm, 0, sizeof shm);
    shm[12] = 255; shm[16] = 0; shm[20] = 0; shm[24] = 255;
    rg_usbio_fill(&s, shm, rec, 1234);
    CHECK(rec[RG_REC_X1] == 255 && rec[RG_REC_Y1] == 1 && rec[RG_REC_X2] == 1 && rec[RG_REC_Y2] == 255);
    CHECK(rd32(rec, RG_REC_TICK) == 1234);
    CHECK(rd32(rec, RG_REC_MODE) == 2);

    /* omkeren per as */
    s.invert_y = 1;
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_Y1] == 255 && rec[RG_REC_Y2] == 1 && rec[RG_REC_X1] == 255);

    /* geen bordbeeld: alles los, assen in het midden, nooit 0 */
    rg_usbio_state_init(&s);
    rg_usbio_fill(&s, 0, rec, 0);
    CHECK(rec[RG_REC_X1] == 128 && rec[RG_REC_Y1] == 128 && rec[RG_REC_BTN_HI] == 0 && rec[RG_REC_BTN_LO] == 0);

    /* knoppen: trekker 1 = 0xf bit 2, trekker 2 = 0xf bit 3, granaat 1 = 0xe bit 0, granaat 2 = 0xe bit 1 */
    memset(shm, 0, sizeof shm);
    put_ctl(shm, RG_CTL_P1_TRIGGER);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_LO] == 0x04 && rec[RG_REC_BTN_HI] == 0);
    put_ctl(shm, RG_CTL_P2_TRIGGER);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_LO] == 0x08 && rec[RG_REC_BTN_HI] == 0);
    put_ctl(shm, RG_CTL_P1_GRENADE);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_HI] == 0x01 && rec[RG_REC_BTN_LO] == 0);
    put_ctl(shm, RG_CTL_P2_GRENADE);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_HI] == 0x02 && rec[RG_REC_BTN_LO] == 0);
    put_ctl(shm, RG_CTL_P1_TRIGGER | RG_CTL_P1_GRENADE);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_LO] == 0x04 && rec[RG_REC_BTN_HI] == 0x01);

    /* start-knoppen bestaan niet op het bord ("pull trigger to start"): geen bit */
    put_ctl(shm, RG_CTL_P1_START | RG_CTL_P2_START);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_LO] == 0 && rec[RG_REC_BTN_HI] == 0);

    /* de vier bedienknoppen: standaard T S U D op 0xf.0 0xf.1 0xe.2 0xe.3 */
    put_ctl(shm, RG_CTL_TEST);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_LO] == 0x01 && rec[RG_REC_BTN_HI] == 0);
    put_ctl(shm, RG_CTL_SERVICE);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_LO] == 0x02);
    put_ctl(shm, RG_CTL_UP);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_HI] == 0x04);
    put_ctl(shm, RG_CTL_DOWN);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_HI] == 0x08);

    /* omzetten van de bedienknoppen */
    CHECK(rg_usbio_set_ops(&s, "DUST") == 1);
    put_ctl(shm, RG_CTL_DOWN);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_LO] == 0x01);
    put_ctl(shm, RG_CTL_TEST);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_HI] == 0x08);
    CHECK(rg_usbio_set_ops(&s, "TTUD") == 0);   /* dubbel */
    CHECK(rg_usbio_set_ops(&s, "TSU") == 0);    /* te kort */
    CHECK(rg_usbio_set_ops(&s, "TSUDX") == 0);  /* te lang */
    CHECK(rg_usbio_set_ops(&s, "TSUX") == 0);   /* onbekende letter */
    put_ctl(shm, RG_CTL_DOWN);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[RG_REC_BTN_LO] == 0x01);          /* een ongeldige reeks laat de vorige staan */

    /* munt: een teller die per indrukken (rand) met een ophoogt; vasthouden telt niet dubbel */
    rg_usbio_state_init(&s);
    put_ctl(shm, 0);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rd32(rec, RG_REC_COIN1) == 0);
    put_ctl(shm, RG_CTL_COIN1);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rd32(rec, RG_REC_COIN1) == 1);
    rg_usbio_fill(&s, shm, rec, 0);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rd32(rec, RG_REC_COIN1) == 1);
    put_ctl(shm, 0);
    rg_usbio_fill(&s, shm, rec, 0);
    put_ctl(shm, RG_CTL_COIN1);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rd32(rec, RG_REC_COIN1) == 2);
    CHECK(rd32(rec, RG_REC_COIN2) == 0);
    s.coin1 = 0xffff;
    put_ctl(shm, 0);
    rg_usbio_fill(&s, shm, rec, 0);
    put_ctl(shm, RG_CTL_COIN1);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rd32(rec, RG_REC_COIN1) == 0);        /* 16 bit, zoals het bord */

    /* een byte van de bedienbits raakt geen asbyte */
    memset(shm, 0xff, sizeof shm);
    rg_usbio_fill(&s, shm, rec, 0);
    CHECK(rec[0] == 0 && rec[1] == 0 && rec[2] == 0 && rec[3] == 0 && rec[4] == 0 && rec[5] == 0);

    /* het toetsenbord van de game: de toetsen van de daemon (1 2 5 6 0 F2..F5) eruit, de rest blijft */
    {
        unsigned char st[256], ev[6 * 8];
        unsigned n, i;
        memset(st, 0, sizeof st);
        st[0x06] = 0x80; st[0x02] = 0x80; st[0x0b] = 0x80; st[0x3e] = 0x80;   /* 5, 1, 0, F4: weg */
        st[0x11] = 0x80; st[0x39] = 0x80; st[0x0a] = 0x80; st[0x3b] = 0x80;   /* W, spatie, 9, F1: blijven */
        CHECK(rg_usbio_filter_state(st) == 4);
        CHECK(st[0x06] == 0 && st[0x02] == 0 && st[0x0b] == 0 && st[0x3e] == 0);
        CHECK(st[0x11] == 0x80 && st[0x39] == 0x80 && st[0x0a] == 0x80 && st[0x3b] == 0x80);
        CHECK(rg_usbio_filter_state(st) == 0);
        CHECK(!rg_usbio_dik_blocked(0x01) && !rg_usbio_dik_blocked(0x04) && !rg_usbio_dik_blocked(0x08) && !rg_usbio_dik_blocked(0x0c) &&
              !rg_usbio_dik_blocked(0x3b) && !rg_usbio_dik_blocked(0x40));
        /* gebufferd: DIDEVICEOBJECTDATA's van 8 bytes (dwOfs, dwData); 5-neer, W-neer, 5-op, F3-neer, spatie-neer, 6-neer */
        memset(ev, 0, sizeof ev);
        { static const unsigned char ofs[6] = {0x06, 0x11, 0x06, 0x3d, 0x39, 0x07}; for (i = 0; i < 6; i++) { ev[i * 8] = ofs[i]; ev[i * 8 + 4] = (unsigned char)(i + 1); } }
        n = rg_usbio_filter_data(ev, 6, 8);
        CHECK(n == 2 && ev[0] == 0x11 && ev[4] == 2 && ev[8] == 0x39 && ev[12] == 5);
        CHECK(rg_usbio_filter_data(ev, 2, 2) == 2);   /* een onmogelijke objectgrootte: ongemoeid */
    }

    if (fails) { printf("%d fout(en)\n", fails); return 1; }
    printf("farcry_usbio_unit: ok\n");
    return 0;
}
