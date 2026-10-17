/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_dinput8_kern.h -- de rekenkern van rg-dinput8 (RavenGuns eigen dinput8.dll, zie rg_dinput8.c): de tabel per game lezen en
 * van het bordbeeld van RavenGun (TeknoParrot_JvsState, 64 bytes; indeling WartranTroopers uit packaging/tp-shm-profiles.json:
 * 32-bits bitveld op offset 8, richtpunt een byte per as vanaf offset 12, links/boven = 0) naar de toestand van virtuele
 * DirectInput-joysticks en naar flanken voor vlaggen en tellers in het geheugen van de game.
 *
 * Geen Windows-aanroepen: dezelfde code draait in de DLL en in de eenheidstest op Linux (tests/dinput8_unit.c).
 *
 * WAT er per game anders is, staat in rg-dinput8.ini naast de DLL (patches/<Game>/dropin/), niet in de code:
 *
 *   spelers = 2                      ; aantal virtuele joysticks (1..4)
 *   p1.x = 12                        ; byte van het bordbeeld met de X van speler 1 (0..63)
 *   p1.y = 13
 *   p1.knop0 = 7                     ; rgbButtons[0] is aan als een van deze bits aan staat (knop0..knop31)
 *   as.x = -999 100                  ; apparaatwaarde aan de linker- en rechterrand van het beeld (standaard: het DIPROP_RANGE
 *   as.y = -799 1000                 ;   dat de game zelf zet, anders 0..65535)
 *   buiten.x = -1000                 ; waarde als de speler buiten beeld mikt (as-byte <= 1 of >= 254); zonder sleutel: de rand
 *   buiten.y = -800
 *   vlag.0x49E870 = 0                ; dword op dat adres := 1 bij een opgaande flank van een van de bits (de game zet hem terug)
 *   teller.0x460C1C = 2, 3           ; dword op dat adres += 1 bij elke opgaande flank
 *   patch.0x40A03F = 01 -> 00        ; bytes in het beeld van de exe, alleen als de oude bytes kloppen (anders NIETS)
 *
 * Een bestand met een fout wordt in zijn geheel afgekeurd: dan komt er geen invoer en geen patch (liever niets dan verkeerd).
 */
#ifndef RG_DINPUT8_KERN_H
#define RG_DINPUT8_KERN_H

#include <stddef.h>
#include <stdint.h>

#define RGD_MAX_SPELERS 4
#define RGD_MAX_KNOPPEN 32
#define RGD_MAX_BITS 6
#define RGD_MAX_ADRES 16
#define RGD_MAX_PATCH 16
#define RGD_MAX_PATCHBYTES 16
#define RGD_BEELD 64

typedef struct {
    int n;
    uint8_t bit[RGD_MAX_BITS];
} rgd_bron;

typedef struct {
    int x, y;                         /* offset in het bordbeeld, -1 = geen as */
    rgd_bron knop[RGD_MAX_KNOPPEN];
} rgd_speler;

typedef struct {
    uint32_t adres;
    rgd_bron bron;
} rgd_adres;

typedef struct {
    uint32_t adres;
    int n;
    uint8_t oud[RGD_MAX_PATCHBYTES], nieuw[RGD_MAX_PATCHBYTES];
} rgd_patch;

typedef struct {
    int spelers;
    rgd_speler sp[RGD_MAX_SPELERS];
    int as_gezet[2];                  /* as.x / as.y gegeven */
    long as_lo[2], as_hi[2];
    int buiten_gezet[2];
    long buiten[2];
    int nvlag, nteller, npatch;
    rgd_adres vlag[RGD_MAX_ADRES], teller[RGD_MAX_ADRES];
    rgd_patch patch[RGD_MAX_PATCH];
} rgd_tabel;

/* De toestand van een virtuele joystick, in de volgorde van DIJOYSTATE2 voor zover de game hem leest. */
typedef struct {
    int32_t lX, lY;
    uint8_t knop[RGD_MAX_KNOPPEN];    /* 0x80 = ingedrukt */
    int buiten;                       /* de speler mikt buiten beeld */
} rgd_joy;

typedef struct {
    uint32_t vorige;
    int begonnen;
} rgd_flanken;

/* Een ingebouwde standaard: twee spelers, X/Y van de indeling WartranTroopers, knop0 = trekker, knop1 = herladen, knop2 = start;
   geen vlaggen, tellers of patches. */
void rgd_standaard(rgd_tabel *t);
/* 0 = goed; anders -1 met de reden (regelnummer en wat er mis is) in fout. */
int rgd_lees_tabel(const char *tekst, rgd_tabel *t, char *fout, size_t foutlen);
/* De GUID van virtuele joystick n (0..3), 16 bytes zoals in het geheugen (Data1/2/3 little-endian). Ook de inhoud van control.cfg. */
void rgd_guid(int n, uint8_t out[16]);
/* Het bitveld (offset 8) uit het bordbeeld. */
uint32_t rgd_bits(const uint8_t beeld[RGD_BEELD]);
/* De toestand van speler n uit het bordbeeld; lo/hi = het bereik dat de game met DIPROP_RANGE zette (voor een as zonder as.x/as.y). */
void rgd_joystick(const rgd_tabel *t, int n, const uint8_t beeld[RGD_BEELD], const long lo[2], const long hi[2], rgd_joy *uit);
/* De opgaande flanken van deze stap: welke vlaggen (bit i van *vlaggen) en hoeveel per teller (tellers[i]). De eerste stap telt niet. */
void rgd_stap(const rgd_tabel *t, rgd_flanken *f, uint32_t bits, uint32_t *vlaggen, int tellers[RGD_MAX_ADRES]);

#endif
