/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Eenheidstest voor de logica van rg-aliens-hook.dll (tools/rg-aliens/aliens_logic.h): bordbeeld naar joystick en toetsen, het
 * richten op de kalibratie van de game, het zoeken van een importplek in een PE32-beeld, en de patch van de twee getters.
 * Gebouwd en gedraaid door tests/aliens_test.sh. Met `--dump` schrijft hij de tabel signaal -> bit, die het script vergelijkt met
 * packaging/tp-shm-profiles.json (de indeling mag niet uit de pas lopen).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../tools/rg-aliens/aliens_logic.h"

static int fouten, controles;
#define CHECK(c) do { ++controles; if (!(c)) { ++fouten; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void put32(unsigned char* p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }

static void test_knoppen(void) {
    /* trekker/special/vlam per speler naar bit 0/1/2; andere bits doen niets */
    CHECK(al_joy_buttons(0, 0) == 0 && al_joy_buttons(0, 1) == 0);
    CHECK(al_joy_buttons(AL_C_P1_TRIGGER, 0) == 1 && al_joy_buttons(AL_C_P1_TRIGGER, 1) == 0);
    CHECK(al_joy_buttons(AL_C_P1_SPECIAL, 0) == 2 && al_joy_buttons(AL_C_P1_FLAME, 0) == 4);
    CHECK(al_joy_buttons(AL_C_P2_TRIGGER, 1) == 1 && al_joy_buttons(AL_C_P2_TRIGGER, 0) == 0);
    CHECK(al_joy_buttons(AL_C_P2_SPECIAL, 1) == 2 && al_joy_buttons(AL_C_P2_FLAME, 1) == 4);
    CHECK(al_joy_buttons(AL_C_P1_TRIGGER | AL_C_P1_SPECIAL | AL_C_P1_FLAME, 0) == 7);
    CHECK(al_joy_buttons(AL_C_COIN1 | AL_C_P1_START | AL_C_TEST | AL_C_VOL_UP, 0) == 0);   /* munt/start gaan via toetsen */
    CHECK(al_popcount(7) == 3 && al_popcount(0) == 0 && al_popcount(0x8000) == 1);
}

static void test_ctl(void) {
    unsigned char s[64];
    memset(s, 0, sizeof s);
    s[8] = 0x00; s[9] = 0x02; s[10] = 0x01; s[11] = 0x00;       /* 0x00010200 = vol omlaag + trekker 1 */
    CHECK(al_ctl(s) == (AL_C_VOL_DOWN | AL_C_P1_TRIGGER));
    s[8] = 0xff; s[9] = 0xff; s[10] = 0xff; s[11] = 0xff;
    CHECK(al_ctl(s) == 0xffffffffu);
}

static void test_richten(void) {
    /* zonder kalibratie: het hele 16-bits bereik, uiteinden exact */
    CHECK(al_aim_raw(0, 0, 0, 0) == 0 && al_aim_raw(255, 0, 0, 0) == 65535);
    CHECK(al_aim_raw(128, 0, 0, 0) >= 32896 - 1 && al_aim_raw(128, 0, 0, 0) <= 32896 + 1);
    /* inverteren */
    CHECK(al_aim_raw(0, 0, 0, 1) == 65535 && al_aim_raw(255, 0, 0, 1) == 0);
    /* met de kalibratie van de game: byte 0 -> min, byte 255 -> max (de echte waarden uit ABH.ADJ van de speler-2-kant) */
    CHECK(al_aim_raw(0, 3700.0f, 8100.0f, 0) == 3700 && al_aim_raw(255, 3700.0f, 8100.0f, 0) == 8100);
    CHECK(al_aim_raw(0, 0.0f, 65535.0f, 0) == 0 && al_aim_raw(255, 0.0f, 65535.0f, 0) == 65535);
    /* de game wisselt zelf min > max om en keert zijn schaal om; wij volgen: byte 0 -> de eerste waarde */
    CHECK(al_aim_raw(0, 8100.0f, 3700.0f, 0) == 8100 && al_aim_raw(255, 8100.0f, 3700.0f, 0) == 3700);
    /* onbruikbare kalibratie (nul, gelijk, NaN, buiten bereik) -> hele bereik */
    {
        volatile float nul = 0.0f, nan = nul / nul;
        CHECK(!al_cal_ok(0.0f, 0.0f) && !al_cal_ok(5.0f, 5.0f) && !al_cal_ok(nan, 100.0f) && !al_cal_ok(-5.0f, 100.0f) && !al_cal_ok(0.0f, 70000.0f));
        CHECK(al_cal_ok(0.0f, 65535.0f) && al_cal_ok(65535.0f, 0.0f) && al_cal_ok(3700.0f, 8100.0f));
        CHECK(al_aim_raw(255, nan, 100.0f, 0) == 65535);
        CHECK(al_aim_raw(255, 0.0f, 0.0f, 0) == 65535);
    }
    /* monotoon over alle bytes */
    {
        uint32_t prev = 0;
        int b, ok = 1;
        for (b = 0; b < 256; ++b) { uint32_t v = al_aim_raw((unsigned char)b, 1000.0f, 60000.0f, 0); if (v < prev) ok = 0; prev = v; }
        CHECK(ok);
    }
}

static void test_toetsen(void) {
    /* elke toets in de tabel hoort bij precies zijn bit, en de twee tabellen (hoofdtoetsen, numeriek blok) geven hetzelfde */
    CHECK(al_key_down(AL_C_COIN1, 0xA2) && al_key_down(AL_C_COIN1, 0x60) && !al_key_down(AL_C_COIN2, 0xA2));
    CHECK(al_key_down(AL_C_P1_START, 0x31) && al_key_down(AL_C_P1_START, 0x61) && !al_key_down(AL_C_P2_START, 0x31));
    CHECK(al_key_down(AL_C_P2_START, 0x32) && al_key_down(AL_C_P2_START, 0x62));
    CHECK(al_key_down(AL_C_TEST, 0x35) && al_key_down(AL_C_TEST, 0x67));
    CHECK(al_key_down(AL_C_SELECT, 0x36) && al_key_down(AL_C_SELECT, 0x68));
    CHECK(al_key_down(AL_C_VOL_UP, 0x39) && al_key_down(AL_C_VOL_UP, 0x69));
    CHECK(al_key_down(AL_C_VOL_DOWN, 0x30) && al_key_down(AL_C_VOL_DOWN, 0x66));
    CHECK(al_key_down(AL_C_COIN3, 0xA4) && al_key_down(AL_C_COIN4, 0x5B));
    CHECK(!al_key_down(0, 0xA2) && !al_key_down(0xffffffffu, 0x41) && !al_key_down(0xffffffffu, 0x1b));   /* onbekende toets nooit */
    CHECK(!al_key_down(AL_C_P1_TRIGGER | AL_C_P1_SPECIAL | AL_C_P1_FLAME, 0xA2));                        /* wapenknoppen zijn geen toetsen */
    {   /* geen dubbele (toets, bit)-paren en elke toets komt in de tabel precies twee keer per bit voor hoogstens */
        int i, j, dup = 0;
        for (i = 0; i < AL_NKEYS; ++i) for (j = i + 1; j < AL_NKEYS; ++j) if (al_keys[i].vk == al_keys[j].vk) dup++;
        CHECK(dup == 0);
    }
}

/* Een minimaal PE32-beeld met twee importbibliotheken, zoals in de exe van de game (importtabel buiten de gewone volgorde). */
static void bouw_pe(unsigned char* img, size_t size) {
    memset(img, 0, size);
    img[0] = 'M'; img[1] = 'Z'; put32(img + 0x3c, 0x80);
    img[0x80] = 'P'; img[0x81] = 'E';
    img[0x80 + 24] = 0x0b; img[0x80 + 25] = 0x01;                 /* PE32 */
    put32(img + 0x80 + 24 + 0x68, 0x400);                          /* importtabel op RVA 0x400 */
    /* descriptor 0: user32.dll, INT 0x500, IAT 0x600 */
    put32(img + 0x400 + 0, 0x500); put32(img + 0x400 + 12, 0x700); put32(img + 0x400 + 16, 0x600);
    /* descriptor 1: WINMM.dll, INT 0x540, IAT 0x640 */
    put32(img + 0x414 + 0, 0x540); put32(img + 0x414 + 12, 0x710); put32(img + 0x414 + 16, 0x640);
    strcpy((char*)img + 0x700, "user32.dll");
    strcpy((char*)img + 0x710, "WINMM.dll");
    /* user32: ordinaal 0x8001, GetKeyState, GetAsyncKeyState */
    put32(img + 0x500, 0x80000001u); put32(img + 0x504, 0x800); put32(img + 0x508, 0x820); put32(img + 0x50c, 0);
    strcpy((char*)img + 0x802, "GetKeyState");
    strcpy((char*)img + 0x822, "GetAsyncKeyState");
    /* winmm: joyGetPosEx, timeGetTime */
    put32(img + 0x540, 0x840); put32(img + 0x544, 0x860); put32(img + 0x548, 0);
    strcpy((char*)img + 0x842, "joyGetPosEx");
    strcpy((char*)img + 0x862, "timeGetTime");
}

static void test_pe(void) {
    unsigned char img[0x1000];
    bouw_pe(img, sizeof img);
    CHECK(al_find_import(img, sizeof img, "user32.dll", "GetAsyncKeyState") == 0x600 + 8);
    CHECK(al_find_import(img, sizeof img, "USER32.DLL", "getkeystate") == 0x600 + 4);
    CHECK(al_find_import(img, sizeof img, "winmm.dll", "joyGetPosEx") == 0x640);
    CHECK(al_find_import(img, sizeof img, "winmm.dll", "timeGetTime") == 0x644);
    CHECK(al_find_import(img, sizeof img, "winmm.dll", "GetAsyncKeyState") == 0);        /* andere bibliotheek */
    CHECK(al_find_import(img, sizeof img, "kernel32.dll", "Sleep") == 0);                /* onbekende bibliotheek */
    CHECK(al_find_import(img, sizeof img, "user32.dll", "MessageBoxA") == 0);            /* ontbrekende functie */
    /* kapotte beelden: nooit buiten het beeld lezen, nooit een plek teruggeven */
    CHECK(al_find_import(img, 0x100, "user32.dll", "GetAsyncKeyState") == 0);
    img[0] = 'X';
    CHECK(al_find_import(img, sizeof img, "user32.dll", "GetAsyncKeyState") == 0);
    bouw_pe(img, sizeof img);
    put32(img + 0x80 + 24 + 0x68, 0xfffff000u);                                           /* importtabel buiten het beeld */
    CHECK(al_find_import(img, sizeof img, "user32.dll", "GetAsyncKeyState") == 0);
    bouw_pe(img, sizeof img);
    img[0x80 + 24] = 0x0b; img[0x80 + 25] = 0x02;                                         /* PE32+ weigeren */
    CHECK(al_find_import(img, sizeof img, "user32.dll", "GetAsyncKeyState") == 0);
    bouw_pe(img, sizeof img);
    put32(img + 0x400 + 12, 0x2000);                                                      /* naam buiten het beeld */
    CHECK(al_find_import(img, sizeof img, "user32.dll", "GetAsyncKeyState") == 0);
    bouw_pe(img, sizeof img);
    put32(img + 0x400, 0);                                                                /* zonder INT: via de IAT zelf (hier gelijk aan 0x600) */
    put32(img + 0x600, 0x800); put32(img + 0x604, 0x820);
    CHECK(al_find_import(img, sizeof img, "user32.dll", "GetAsyncKeyState") == 0x600 + 4);
}

static void test_getters(void) {
    unsigned char g[8] = { 0xa1, 0x88, 0xae, 0x27, 0x05, 0xc3, 0x90, 0x90 };
    CHECK(al_getter_matches(g, AL_COUNTER_GUNS_VA));
    CHECK(!al_getter_matches(g, AL_COUNTER_CARD_VA));                  /* verkeerde teller */
    g[5] = 0xc2;
    CHECK(!al_getter_matches(g, AL_COUNTER_GUNS_VA));                  /* geen ret meer */
    g[5] = 0xc3;
    al_getter_patch(g, 2);
    CHECK(g[0] == 0xb8 && g[1] == 2 && g[2] == 0 && g[3] == 0 && g[4] == 0 && g[5] == 0xc3 && g[6] == 0x90);
    CHECK(!al_getter_matches(g, AL_COUNTER_GUNS_VA));                  /* na de patch past hij niet meer: nooit twee keer */
    CHECK(AL_GETTER_GUNS_RVA + 0x400000u == 0x42ed00u && AL_GETTER_CARD_RVA + 0x400000u == 0x42ed10u);
    CHECK(AL_CAL_X_VA + AL_PLAYER_STRIDE == 0x596b2a0u);               /* speler 2 volgens de init van de game (0x44874f) */
}


/* Optioneel: de echte exe in een beeld zetten (secties op hun RVA) en de twee importplekken zoeken, zoals de DLL het in het proces doet. */
static int test_exe(const char* pad) {
    FILE* f = fopen(pad, "rb");
    unsigned char hdr[0x400];
    unsigned char* img;
    uint32_t lfanew, nsec, opt_size, size_img, i, g, k;
    if (!f) { printf("aliens_unit: exe niet te lezen, overgeslagen\n"); return 0; }
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) { fclose(f); return 1; }
    lfanew = al_u32(hdr + 0x3c);
    nsec = al_u16(hdr + lfanew + 6);
    opt_size = al_u16(hdr + lfanew + 20);
    size_img = al_u32(hdr + lfanew + 24 + 56);
    img = (unsigned char*)calloc(size_img, 1);
    if (!img) { fclose(f); return 1; }
    memcpy(img, hdr, sizeof hdr);
    for (i = 0; i < nsec; ++i) {
        const unsigned char* sh = hdr + lfanew + 24 + opt_size + 40 * i;
        uint32_t va = al_u32(sh + 12), raw_sz = al_u32(sh + 16), raw_off = al_u32(sh + 20);
        if (!raw_sz || va + raw_sz > size_img) continue;
        /* de grote .data-sectie slaan we over: de importtabel en de getters staan in .text/.rdata/.mackt */
        if (raw_sz > 0x1000000u) continue;
        fseek(f, (long)raw_off, SEEK_SET);
        if (fread(img + va, 1, raw_sz, f) != raw_sz) { free(img); fclose(f); return 1; }
    }
    fclose(f);
    g = al_find_import(img, size_img, "user32.dll", "GetAsyncKeyState");
    k = al_find_import(img, size_img, "winmm.dll", "joyGetPosEx");
    printf("aliens_unit: exe: GetAsyncKeyState-plek RVA 0x%x (VA 0x%x), joyGetPosEx-plek RVA 0x%x (VA 0x%x)\n", g, g + 0x400000, k, k + 0x400000);
    CHECK(g + 0x400000 == 0x59892b0u);      /* gemeten: call ds:0x59892b0 in de pollthread en de toetstabel */
    CHECK(k + 0x400000 == 0x59892b8u);      /* gemeten: call ds:0x59892b8 in 0x42ed70 en 0x42fcc0 */
    CHECK(al_getter_matches(img + AL_GETTER_GUNS_RVA, AL_COUNTER_GUNS_VA) && al_getter_matches(img + AL_GETTER_CARD_RVA, AL_COUNTER_CARD_VA));
    free(img);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--dump") == 0) {
        /* munt 3 en 4 (0x1, 0x2) staan niet in het profiel van de daemon en worden dus niet vergeleken */
        printf("coin1 0x%x\ncoin2 0x%x\ntest 0x%x\nservice 0x%x\nvolume_up 0x%x\nvolume_down 0x%x\n",
               AL_C_COIN1, AL_C_COIN2, AL_C_TEST, AL_C_SELECT, AL_C_VOL_UP, AL_C_VOL_DOWN);
        printf("p1.start 0x%x\np1.trigger 0x%x\np1.grenade 0x%x\np1.aux1 0x%x\n", AL_C_P1_START, AL_C_P1_TRIGGER, AL_C_P1_SPECIAL, AL_C_P1_FLAME);
        printf("p2.start 0x%x\np2.trigger 0x%x\np2.grenade 0x%x\np2.aux1 0x%x\n", AL_C_P2_START, AL_C_P2_TRIGGER, AL_C_P2_SPECIAL, AL_C_P2_FLAME);
        return 0;
    }
    test_knoppen();
    test_ctl();
    test_richten();
    test_toetsen();
    test_pe();
    test_getters();
    if (argc > 2 && strcmp(argv[1], "--exe") == 0 && test_exe(argv[2])) fouten++;
    if (fouten) { fprintf(stderr, "aliens_unit: %d van %d controles mislukt\n", fouten, controles); return 1; }
    printf("aliens_unit: %d controles ok\n", controles);
    return 0;
}
