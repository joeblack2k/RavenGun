/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-konami: RavenGuns eigen bin/iob.dll voor Gashaaaan!!!! (Konami, F24-kast). Op de kast praat Konami's iob.dll met het I/O-bord
 * (knoppen, muntproevers, lampen, blacklight en de balautomaten die de ballen teruggeven); hier beantwoordt deze DLL de game uit
 * het bordbeeld van RavenGun, zonder bord en zonder de kern van TeknoParrot. Dezelfde 30 exports (namen en ordinalen) als het
 * origineel: gsh_iob.def. Ook bin/ballio.dll importeert hem (iob_f24_set_lamp_status); die vervangen we ook (rg_gsh_ballio.c).
 *
 * CLEAN-ROOM: geschreven uit de aanroepen in system.dll (opstartmachine 0x10005400), testmode.dll (INPUT CHECK 0x100045b0),
 * gcoin.dll (munten 0x100015f8) en uit iob.dll zelf (welke knop welk bit is: 0x1000eea0). Geen code van het origineel.
 *
 * Wat system.dll wil: iob_get_status() == 3 (bord draait; 4 = fout, 5 = firmware bijwerken), iob_check_plug() == 1 (2 = fout),
 * iob_f24_ballout_status_req() <> 0 en iob_f24_ballout_status_get() <> 0 met een tweede argument 0 (anders "fout balautomaat").
 */
#define KB_NAAM "iob.dll(Gashaaaan)"
#include "kb_win.h"

static gsh_iob_staat staat;
static uint8_t mujinkun;
static DWORD laatste_stap;
static int ooit_gestapt;

void gsh_iob_step(void);

/* Vangnet: roept niemand iob_step aan (langer dan 100 ms), dan bemonstert een knopvraag het bordbeeld zelf, zodat knoppen niet
 * blijven hangen in een fase waarin de game geen iob_step doet. */
static void misschien_stap(void)
{
    if (!ooit_gestapt || GetTickCount() - laatste_stap > 100) gsh_iob_step();
}

/* De game roept iob_step elk beeld aan (system.dll); daar wordt het bordbeeld bemonsterd: button_trg is "nu wel, vorige stap niet". */
void gsh_iob_step(void)
{
    laatste_stap = GetTickCount();
    ooit_gestapt = 1;
    kb_beeld b;
    kb_beeld_nu(&b);
    EnterCriticalSection(&kb_slot);
    uint8_t vorig[GSH_KNOPPEN];
    memcpy(vorig, staat.nu, sizeof vorig);
    const uint8_t munt_voor[2] = {staat.munt[0], staat.munt[1]};
    gsh_iob_stap(&staat, &b);
    uint8_t nu[GSH_KNOPPEN];
    memcpy(nu, staat.nu, sizeof nu);
    const uint8_t munt_na[2] = {staat.munt[0], staat.munt[1]};
    LeaveCriticalSection(&kb_slot);
    static const char *namen[GSH_KNOPPEN] = {"START P1", "START P2", "START P3", "START P4", "TEST", "SERVICE", "6", "7", "8", "9"};
    for (int i = 0; i < GSH_KNOPPEN; i++)
        if (nu[i] && !vorig[i] && kb_meldingen < 200) { kb_meldingen++; kb_log("invoer: %s ingedrukt", namen[i]); }
    for (int m = 0; m < 2; m++)
        if (munt_na[m] != munt_voor[m] && kb_meldingen < 200) { kb_meldingen++; kb_log("invoer: munt %d (teller %u)", m + 1, munt_na[m]); }
}

void gsh_iob_boot(int a, char *b)
{
    kb_meld_geladen("iob_boot");
    kb_log("iob_boot(%d, %s): bord van RavenGun", a, b ? b : "(null)");
}

void gsh_iob_shutdown(void) { kb_log("iob_shutdown"); }

int gsh_iob_get_status(void)
{
    static int gemeld;
    if (!gemeld) { gemeld = 1; kb_meld_geladen("iob_get_status"); kb_log("iob_get_status: 3 (bord draait)"); }
    return 3;
}

void gsh_iob_get_last_error(int *fout, unsigned *extra)
{
    if (fout) *fout = 0;
    if (extra) *extra = 0;
}

void gsh_iob_get_error_count(int i, int *a, int *b)
{
    (void)i;
    if (a) *a = 0;
    if (b) *b = 0;
}

int gsh_iob_check_plug(int *uit)
{
    static int gemeld;
    if (!gemeld) { gemeld = 1; kb_log("iob_check_plug: 1 (in orde)"); }
    if (uit) *uit = 0;
    return 1;
}

void gsh_iob_reload_plug_init(void) {}
uint8_t gsh_iob_reload_plug_wait(void) { return 1; }
void gsh_iob_firmupdate_start(void) {}
int gsh_iob_firmupdate_get_status(void) { return 1; }

uint8_t gsh_iob_f24_button(int i)
{
    misschien_stap();
    EnterCriticalSection(&kb_slot);
    const uint8_t v = gsh_knop(&staat, i);
    LeaveCriticalSection(&kb_slot);
    return v;
}

uint8_t gsh_iob_f24_button_trg(int i)
{
    misschien_stap();
    EnterCriticalSection(&kb_slot);
    const uint8_t v = gsh_knop_trg(&staat, i);
    LeaveCriticalSection(&kb_slot);
    return v;
}

int gsh_iob_f24_nr_coin(int mech, int *modulo)
{
    int n = 0, m = 0;
    misschien_stap();
    if (mech == 0 || mech == 1) {
        EnterCriticalSection(&kb_slot);
        n = staat.munt[mech];
        LeaveCriticalSection(&kb_slot);
        m = 0x3f;
    }
    if (modulo) *modulo = m;
    return n;
}

void gsh_iob_f24_add_coin_counter(int mech, int n) { (void)mech; (void)n; }

/* De balautomaten: geen hardware. Een aanvraag lukt meteen en de toestand is "klaar, geen fout". */
void gsh_iob_f24_ballout_go(int automaat, int aantal)
{
    if (kb_meldingen < 200) { kb_meldingen++; kb_log("balautomaat %d: %d ballen (geen hardware)", automaat, aantal); }
}
void gsh_iob_f24_ballout_stop(int automaat) { (void)automaat; }
void gsh_iob_f24_ballout_stop_immediate(int automaat) { (void)automaat; }
uint8_t gsh_iob_f24_ballout_status_req(int automaat) { (void)automaat; return 1; }

uint8_t gsh_iob_f24_ballout_status_get(int automaat, uint8_t *a, uint8_t *fout, uint8_t *c)
{
    (void)automaat;
    if (a) *a = 0;
    if (fout) *fout = 0;
    if (c) *c = 0;
    return 1;
}

void gsh_iob_f24_set_black_light_status(uint8_t a, uint8_t b) { (void)a; (void)b; }
void gsh_iob_f24_set_coinblocker_status(int mech, uint8_t v) { (void)mech; (void)v; }
void gsh_iob_f24_set_lamp_status(int lamp, uint8_t v) { (void)lamp; (void)v; }

static uint8_t schrijf_id(char *buf, int lengte, const char *id)
{
    if (!buf || lengte <= 0) return 0;
    kb_kopieer(buf, (size_t)lengte, id);
    return 1;
}

uint8_t gsh_iob_get_software_id(char *buf, int lengte) { return schrijf_id(buf, lengte, "RAVENGUN"); }
uint8_t gsh_iob_get_network_id(char *buf, int lengte) { return schrijf_id(buf, lengte, "RAVENGUN"); }

void gsh_iob_get_hardware_id(char *buf, int lengte, uint8_t *extra)
{
    schrijf_id(buf, lengte, "RAVENGUN");
    if (extra) *extra = 0;
}

void gsh_iob_get_firmware_version(int bord, char *buf, int lengte)
{
    (void)bord;
    schrijf_id(buf, lengte, "RG");
}

uint8_t gsh_mujinkun_get_mode(void) { return mujinkun; }
void gsh_mujinkun_set_mode(uint8_t m) { mujinkun = m; }

const char *gsh_GetDllStampStr(void) { return "rg-konami iob.dll (RavenGun)"; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reden, LPVOID r)
{
    (void)r;
    if (reden == DLL_PROCESS_ATTACH) kb_dllmain_attach(h);
    return TRUE;
}
