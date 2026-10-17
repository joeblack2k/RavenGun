/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Aliens Extermination (Global VR, aliens dehasped.exe): de zuivere logica van rg-aliens-hook.dll, zonder windows.h zodat
 * tests/aliens_unit.c ze natief kan testen.
 *
 * Wat de game zelf doet (gemeten uit de exe, docs/research/losse/AliensExtermination.md):
 *   - richten en knoppen: winmm!joyGetPosEx(0) voor speler 1 en (1) voor speler 2; dwButtons bit0 = trekker (spelmasker 0x10),
 *     bit1 = special (0x20), bit2 = vlam (0x40); dwXpos/dwYpos zijn ruwe waarden die de game met zijn EIGEN kalibratie
 *     (opties 0xe5..0xe8 per speler, uit ABH.ADJ) op het scherm zet;
 *   - munt, start, test, select en volume: user32!GetAsyncKeyState met twee toetstabellen (hoofdtoetsen en numeriek blok);
 *   - de HID-apparaten (VID 04B4, PID 6870 voor de geweren, 7013 voor de GVRIO-kaart) worden alleen BESCHREVEN op het opstartscherm
 *     en voor lampen/terugslag beschreven (WriteFile); er wordt nooit uit gelezen.
 * De gesloten TeknoParrot-kern doet dezelfde vertaling vanuit TeknoParrot_JvsState; dit bestand doet haar vanuit dezelfde
 * 64 bytes (tp-shm-indeling AliensExtermination, packaging/tp-shm-profiles.json).
 */
#ifndef RG_ALIENS_LOGIC_H
#define RG_ALIENS_LOGIC_H

#include <stddef.h>
#include <stdint.h>

/* --- de tp-shm-indeling AliensExtermination (TeknoParrotUi AliensExterminationPipe, offset 8, 32 bits) ------------------------ */
#define AL_C_COIN3      0x00000001u   /* munt 3 (speler 2 knop 5) */
#define AL_C_COIN4      0x00000002u   /* munt 4 (speler 2 knop 6) */
#define AL_C_P2_SPECIAL 0x00000004u
#define AL_C_P2_FLAME   0x00000008u
#define AL_C_P1_SPECIAL 0x00000010u
#define AL_C_P1_FLAME   0x00000020u
#define AL_C_TEST       0x00000040u
#define AL_C_SELECT     0x00000080u   /* "Service" in het profiel */
#define AL_C_P1_START   0x00000100u
#define AL_C_P1_TRIGGER 0x00000200u
#define AL_C_COIN1      0x00000400u
#define AL_C_COIN2      0x00000800u
#define AL_C_P2_START   0x00002000u
#define AL_C_P2_TRIGGER 0x00004000u
#define AL_C_VOL_UP     0x00008000u
#define AL_C_VOL_DOWN   0x00010000u

#define AL_JOY_UNPLUGGED 167u         /* JOYERR_UNPLUGGED */

static inline uint32_t al_ctl(const unsigned char* shm) {
    return (uint32_t)shm[8] | ((uint32_t)shm[9] << 8) | ((uint32_t)shm[10] << 16) | ((uint32_t)shm[11] << 24);
}

/* dwButtons van joyGetPosEx voor speler p (0/1): bit0 trekker, bit1 special, bit2 vlam. */
static inline uint32_t al_joy_buttons(uint32_t ctl, int p) {
    uint32_t b = 0;
    if (ctl & (p ? AL_C_P2_TRIGGER : AL_C_P1_TRIGGER)) b |= 1u;
    if (ctl & (p ? AL_C_P2_SPECIAL : AL_C_P1_SPECIAL)) b |= 2u;
    if (ctl & (p ? AL_C_P2_FLAME : AL_C_P1_FLAME)) b |= 4u;
    return b;
}

static inline int al_popcount(uint32_t v) {
    int n = 0;
    while (v) { n += (int)(v & 1u); v >>= 1; }
    return n;
}

/* Aimbyte (0 = links/boven, 255 = rechts/onder) -> ruwe joystickwaarde in de kalibratie van de game.
 * cal_min/cal_max zijn de twee floats die de game zelf uit ABH.ADJ leest (min en max van die as, in ruwe 16-bits eenheden).
 * Is de kalibratie niet te gebruiken (NaN, gelijk, buiten 0..65535) dan geldt het hele bereik 0..65535.
 * De game wisselt zelf min en max als min > max (en keert dan de schaal om); wij houden die volgorde aan, dus byte 0 -> cal_min. */
static inline int al_cal_ok(float lo, float hi) {
    if (!(lo == lo) || !(hi == hi)) return 0;                    /* NaN */
    if (lo < 0.0f || lo > 65535.0f || hi < 0.0f || hi > 65535.0f) return 0;
    return (hi - lo > 1.0f) || (lo - hi > 1.0f);
}

static inline uint32_t al_aim_raw(unsigned char b, float cal_min, float cal_max, int invert) {
    float lo = 0.0f, hi = 65535.0f, f, v;
    if (al_cal_ok(cal_min, cal_max)) { lo = cal_min; hi = cal_max; }
    f = (float)b / 255.0f;
    if (invert) f = 1.0f - f;
    v = lo + f * (hi - lo) + 0.5f;
    if (v < 0.0f) v = 0.0f;
    if (v > 65535.0f) v = 65535.0f;
    return (uint32_t)v;
}

/* --- toetsen: virtuele toetscode -> bit in de tp-shm-besturing ------------------------------------------------------------ */
/* Twee tabellen in de game (0x85edf0 en 0x85ee48): dezelfde functies, hoofdtoetsen en numeriek blok. De game kiest per speler/modus;
 * wij beantwoorden allebei, zodat de keuze er niet toe doet. Spelmasker erbij voor het logboek. */
typedef struct { unsigned vk; uint32_t ctl_bit; uint32_t game_mask; const char* name; } al_key;
static const al_key al_keys[] = {
    { 0xA2, AL_C_COIN1,    0x01000000u, "COIN 1" },    /* L-CTRL    (CONTROLS.txt: credits L-CTRL, L-SHIFT, L-ALT) */
    { 0xA0, AL_C_COIN2,    0x02000000u, "COIN 2" },    /* L-SHIFT   */
    { 0xA4, AL_C_COIN3,    0x04000000u, "COIN 3" },    /* L-ALT     */
    { 0x5B, AL_C_COIN4,    0x08000000u, "COIN 4" },    /* L-Win     */
    { 0x31, AL_C_P1_START, 0x00010000u, "START 1" },   /* '1'       */
    { 0x32, AL_C_P2_START, 0x00020000u, "START 2" },   /* '2'       */
    { 0x35, AL_C_TEST,     0x00100000u, "TEST" },      /* '5' servicemenu */
    { 0x36, AL_C_SELECT,   0x00200000u, "SELECT" },    /* '6' bevestigen  */
    { 0x39, AL_C_VOL_UP,   0x00400000u, "VOLUME UP" }, /* '9' omhoog in het menu */
    { 0x30, AL_C_VOL_DOWN, 0x00800000u, "VOLUME DOWN" },/* '0' omlaag in het menu */
    { 0x60, AL_C_COIN1,    0x01000000u, "COIN 1" },    /* numeriek blok: 0 */
    { 0x63, AL_C_COIN2,    0x02000000u, "COIN 2" },    /* 3 */
    { 0x64, AL_C_COIN3,    0x04000000u, "COIN 3" },    /* 4 */
    { 0x65, AL_C_COIN4,    0x08000000u, "COIN 4" },    /* 5 */
    { 0x61, AL_C_P1_START, 0x00010000u, "START 1" },   /* 1 */
    { 0x62, AL_C_P2_START, 0x00020000u, "START 2" },   /* 2 */
    { 0x67, AL_C_TEST,     0x00100000u, "TEST" },      /* 7 */
    { 0x68, AL_C_SELECT,   0x00200000u, "SELECT" },    /* 8 */
    { 0x69, AL_C_VOL_UP,   0x00400000u, "VOLUME UP" }, /* 9 */
    { 0x66, AL_C_VOL_DOWN, 0x00800000u, "VOLUME DOWN" } /* 6 */
};
#define AL_NKEYS ((int)(sizeof al_keys / sizeof al_keys[0]))

static inline int al_key_down(uint32_t ctl, unsigned vk) {
    int i;
    for (i = 0; i < AL_NKEYS; ++i)
        if (al_keys[i].vk == vk) return (ctl & al_keys[i].ctl_bit) != 0;
    return 0;
}

/* --- PE: een plek in de importtabel van een 32-bits beeld zoeken -------------------------------------------------------------- */
static inline uint16_t al_u16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t al_u32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static inline int al_ieq(const char* a, const char* b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* base = begin van het beeld in geheugen (size = SizeOfImage, om niet buiten het beeld te lezen). Geeft de RVA van de
 * FirstThunk-plek (de IAT-plek die de lader invult) van functie `fn` uit `dll`, of 0 als hij er niet is of het beeld niet klopt. */
static inline uint32_t al_find_import(const unsigned char* base, size_t size, const char* dll, const char* fn) {
    uint32_t lfanew, imp_rva, d, name_rva;
    if (size < 0x200 || base[0] != 'M' || base[1] != 'Z') return 0;
    lfanew = al_u32(base + 0x3c);
    if (lfanew + 0x100 > size || base[lfanew] != 'P' || base[lfanew + 1] != 'E') return 0;
    if (al_u16(base + lfanew + 24) != 0x10b) return 0;                 /* PE32 */
    imp_rva = al_u32(base + lfanew + 24 + 0x68);                       /* DataDirectory[1].VirtualAddress */
    if (!imp_rva) return 0;
    for (d = imp_rva; d + 20 <= size; d += 20) {
        uint32_t oft, ft, t;
        name_rva = al_u32(base + d + 12);
        if (!name_rva) break;
        if (name_rva >= size) return 0;
        if (!al_ieq((const char*)base + name_rva, dll)) continue;
        oft = al_u32(base + d);
        ft = al_u32(base + d + 16);
        if (!oft) oft = ft;
        if (!ft || oft >= size || ft >= size) return 0;
        for (t = 0; oft + t + 4 <= size && ft + t + 4 <= size; t += 4) {
            uint32_t e = al_u32(base + oft + t), nm;
            if (!e) break;
            if (e & 0x80000000u) continue;                             /* op ordinaal */
            nm = e + 2;
            if (nm >= size) continue;
            if (!al_ieq((const char*)base + nm, fn)) continue;
            return ft + t;
        }
        return 0;
    }
    return 0;
}

/* --- de twee getters van het opstartscherm ------------------------------------------------------------------------------------ */
/* 0x42ed00: mov eax,[0x527ae88]; ret     (aantal gevonden geweren)     -> mov eax,2; ret
 * 0x42ed10: mov eax,[0x527ae8c]; ret     (aantal gevonden GVRIO-kaarten) -> mov eax,1; ret
 * Alleen het opstartscherm leest ze; de HID-teller zelf (voor lampen/terugslag) blijft 0, dus de game schrijft niet naar
 * apparaten die er niet zijn. Past alleen als de bytes EXACT kloppen. */
#define AL_GETTER_GUNS_RVA  0x2ed00u    /* VA 0x42ed00 - 0x400000 */
#define AL_GETTER_CARD_RVA  0x2ed10u
static inline int al_getter_matches(const unsigned char* p, uint32_t counter_va) {
    return p[0] == 0xa1 && al_u32(p + 1) == counter_va && p[5] == 0xc3;
}
static inline void al_getter_patch(unsigned char* p, unsigned count) {
    p[0] = 0xb8; p[1] = (unsigned char)count; p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0xc3;
}
#define AL_COUNTER_GUNS_VA 0x527ae88u
#define AL_COUNTER_CARD_VA 0x527ae8cu

/* Kalibratie die de game voor speler p in het geheugen zet: xmin, xmax, (bereik, schaal), ymin, ymax; stap 0x318 per speler. */
#define AL_CAL_X_VA 0x596af88u
#define AL_CAL_Y_VA 0x596af98u
#define AL_PLAYER_STRIDE 0x318u

#endif
