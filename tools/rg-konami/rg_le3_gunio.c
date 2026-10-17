/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-konami: RavenGuns eigen gunio.dll voor Lethal Enforcers 3. Op de kast praat Konami's gunio.dll over een seriele poort met het
 * geweerbord; hier geeft deze DLL het richtpunt uit het bordbeeld van RavenGun, zonder bord en zonder de kern van TeknoParrot.
 * Dezelfde 18 exports (namen en ordinalen) als het origineel: le3_gunio.def.
 *
 * CLEAN-ROOM: geschreven uit de aanroepen in game.exe (opstart 0x51ec95..0x51ee1e, richtlus 0x51ee59, 0x51d540) en uit wat het
 * origineel in gunio_read_data teruggeeft (gunio.dll 0x10002dd5). Geen code van het origineel. Zie konami_logic.h.
 *
 * De game wil: gunio_boot_status(0) == 1 ("CHECKING GUN...OK"), gunio_check_error() != 1. Het richtpunt rekent de game zelf uit met
 * de kalibratie in SAVE/io.sav; die hoort bij de ruwe waarden van deze DLL (dropin/SAVE/io.sav, le3_maak_iosav()).
 */
#define KB_NAAM "gunio.dll(LE3)"
#include "kb_win.h"

void le3_gunio_read_data(int geweer, float *x, float *y, int *buiten, int *ruw_x, int *ruw_y)
{
    kb_meld_geladen("gunio_read_data");
    kb_beeld b;
    kb_beeld_nu(&b);
    le3_schot s;
    le3_gun(&b, geweer, &s);
    if (x) *x = s.x;
    if (y) *y = s.y;
    if (buiten) *buiten = s.buiten;
    if (ruw_x) *ruw_x = s.ruw_x;
    if (ruw_y) *ruw_y = s.ruw_y;
    /* Bewijs in het logboek: het eerste schot in beeld per geweer, en daarna af en toe een punt. */
    static int gemeld[2];
    static DWORD laatst[2];
    const int p = geweer & 1;
    if (b.geldig && !s.buiten && kb_meldingen < 200 && (!gemeld[p] || GetTickCount() - laatst[p] > 10000)) {
        gemeld[p] = 1;
        laatst[p] = GetTickCount();
        kb_meldingen++;
        kb_log("richtpunt geweer %d: as %u,%u -> ruw %d,%d (scherm ~%d,%d van 640x480)", p + 1, b.as[2 * p], b.as[2 * p + 1],
               s.ruw_x, s.ruw_y, (int)s.x, (int)s.y);
    }
}

void le3_gunio_init(int a, int b) { kb_meld_geladen("gunio_init"); kb_log("gunio_init(%d, %d): geweerbord van RavenGun", a, b); }
void le3_gunio_run(void) { kb_log("gunio_run"); }
void le3_gunio_stop(void) {}
void le3_gunio_finish(void) {}

int le3_gunio_boot_status(int bord)
{
    static int gemeld;
    if (!gemeld) { gemeld = 1; kb_log("gunio_boot_status(%d): 1 (CHECKING GUN...OK)", bord); }
    return 1;
}

int le3_gunio_check_error(void) { return 0; }

int le3_gunio_get_errorcode(char *buf, int lengte)
{
    if (buf && lengte > 0) buf[0] = 0;
    return 0;
}

int le3_gunio_get_ver(int bord) { (void)bord; return 1; }
int le3_gunio_get_serial_error_count(int bord) { (void)bord; return 0; }

int le3_gunio_get_data_error_count(int bord, uint8_t *n)
{
    (void)bord;
    if (n) *n = 0;
    return 0;
}

void le3_gunio_change_freq_id(int id) { (void)id; }
void le3_gunio_update_dipsw_data(void) {}

void le3_gunio_dipsw_data(int bord, uint8_t *v)
{
    (void)bord;
    if (v) *v = 0;   /* een byte (gunio.dll 0x10002f97) */
}

void le3_gunio_update_ledcheck_data(void) {}

void le3_gunio_get_ledcheck_data(int geweer, uint16_t *v)
{
    (void)geweer;
    if (v) *v = 0;   /* een woord (gunio.dll 0x10002eef) */
}

/* Niet door game.exe gebruikt (die kalibreert zelf); de grootte van T_GUNIO_CALIBRATION_DATA is niet nodig: niets lezen of schrijven. */
void le3_gunio_get_calibration(int geweer, void *data) { (void)geweer; (void)data; }
void le3_gunio_set_calibration(int geweer, void *data) { (void)geweer; (void)data; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reden, LPVOID r)
{
    if (reden == DLL_PROCESS_ATTACH) kb_dllmain_attach(h);
    else if (reden == DLL_PROCESS_DETACH) kb_dllmain_detach(r);
    return TRUE;
}
