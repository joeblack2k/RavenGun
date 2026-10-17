/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-konami: RavenGuns eigen bin/ballio.dll voor Gashaaaan!!!!. Op de kast meet een balsensor (BS2) waar een geworpen bal het scherm
 * raakt; Konami's ballio.dll geeft die inslagen elke stap aan de game (ball_io_point_get). Hier wordt een worp van speler N (de
 * trekker gaat in) een inslag op het richtpunt van speler N, uit het bordbeeld van RavenGun, zonder sensor en zonder de kern van
 * TeknoParrot. Dezelfde 63 exports (namen en ordinalen) als het origineel: gsh_ballio.def.
 *
 * CLEAN-ROOM: geschreven uit de aanroepen in system.dll (opstart: bs2_get_status, bs2_calibration_get_state), gamemode.dll
 * (ball_io_mode_set, ball_io_enable), player.dll en de stage-DLL's (ball_io_point_get) en testmode.dll, en uit wat het origineel in
 * zijn structuren schrijft (ballio.dll 0x10008840 punten, 0x10002220 t_bs_version = 9 bytes, 0x10002410 t_bs_calibout = 0x24 bytes).
 * Geen code van het origineel. Zie konami_logic.h en docs/research/konami/README.md.
 *
 *   - ball_io_step (elk beeld, system.dll) bouwt de inslagen van dit beeld; ball_io_point_get geeft ze zonder ze te verbruiken
 *     (player.dll en de stage-DLL vragen ze in hetzelfde beeld allebei op, zoals bij het origineel);
 *   - ball_io_mode_set(0) = geen inslagen (zoals het origineel zonder terugroepfunctie), 1 = inslagen; ball_io_disable() = geen;
 *   - de coordinaten zijn schermpixels: x op sfgl_get_screen_width() van util.dll (player.dll deelt x door breedte / 4 om de speler
 *     te bepalen), y op sfgl_get_screen_height();
 *   - de sensor zegt altijd "klaar" (bs2_get_status = 2, kalibratie klaar, geen fout).
 */
#define KB_NAAM "ballio.dll(Gashaaaan)"
#include "kb_win.h"

typedef int(__cdecl *FnPunten)(gsh_punt *, int);

static gsh_bal_staat staat;
static int modus;            /* ball_io_mode_set */
static int uitgezet;         /* ball_io_disable */
static FnPunten terugroep;   /* ball_io_point_get_func_set */
static HWND venster;
static DWORD laatste_stap;
static int ooit_gestapt;

static void scherm(int *b, int *h)
{
    static int (__cdecl *breedte)(void), (__cdecl *hoogte)(void);
    static int gezocht;
    if (!gezocht) {
        gezocht = 1;
        HMODULE u = GetModuleHandleA("util.dll");
        if (u) {
            breedte = (int(__cdecl *)(void))(void *)GetProcAddress(u, "?sfgl_get_screen_width@@YAHXZ");
            hoogte = (int(__cdecl *)(void))(void *)GetProcAddress(u, "?sfgl_get_screen_height@@YAHXZ");
        }
        kb_log("schermmaat: %s", breedte && hoogte ? "util.dll sfgl_get_screen_width/height" : "venster (util.dll niet gevonden)");
    }
    *b = breedte ? breedte() : 0;
    *h = hoogte ? hoogte() : 0;
    if (*b <= 0 || *h <= 0) {
        RECT r;
        if (venster && GetClientRect(venster, &r) && r.right > 0 && r.bottom > 0) { *b = r.right; *h = r.bottom; }
        else { *b = GetSystemMetrics(SM_CXSCREEN); *h = GetSystemMetrics(SM_CYSCREEN); }
    }
}

void gsh_ball_io_step(void)
{
    laatste_stap = GetTickCount();
    ooit_gestapt = 1;
    kb_beeld b;
    kb_beeld_nu(&b);
    int w, h;
    scherm(&w, &h);
    EnterCriticalSection(&kb_slot);
    gsh_bal_stap(&staat, &b, w, h);
    if (uitgezet) staat.aantal = 0;
    const int n = staat.aantal;
    gsh_punt p[GSH_MAX_INSLAGEN];
    memcpy(p, staat.punt, sizeof p);
    LeaveCriticalSection(&kb_slot);
    for (int i = 0; i < n && kb_meldingen < 200; i++) {
        kb_meldingen++;
        kb_log("worp: inslag %d,%d op een scherm van %dx%d (modus %d)", p[i].x, p[i].y, w, h, modus);
    }
}

int gsh_ball_io_point_get(gsh_punt *buf, int bytes)
{
    if (modus == 0) return terugroep ? terugroep(buf, bytes) : 0;
    /* Vangnet: geen ball_io_step meer (langer dan 100 ms)? Dan zelf bemonsteren, anders blijft een oude inslag terugkomen. */
    if (!ooit_gestapt || GetTickCount() - laatste_stap > 100) gsh_ball_io_step();
    EnterCriticalSection(&kb_slot);
    const int n = gsh_bal_punten(&staat, buf, bytes);
    LeaveCriticalSection(&kb_slot);
    return n;
}

void gsh_ball_io_boot(HWND h, int a)
{
    kb_meld_geladen("ball_io_boot");
    venster = h;
    kb_log("ball_io_boot(venster %08lx, %d): balsensor van RavenGun", (unsigned long)(uintptr_t)h, a);
}

void gsh_ball_io_finish(void) { kb_log("ball_io_finish"); }
void gsh_ball_io_enable(void) { uitgezet = 0; }
void gsh_ball_io_disable(void) { uitgezet = 1; }

void gsh_ball_io_mode_set(int m)
{
    if (m != modus) kb_log("ball_io_mode_set(%d)", m);
    modus = m;
}

void gsh_ball_io_point_disp(void) {}
void gsh_ball_io_point_get_func_set(FnPunten f) { terugroep = f; }

/* De sensor (BS2): altijd klaar. */
int gsh_bs2_get_status(void)
{
    static int gemeld;
    if (!gemeld) { gemeld = 1; kb_meld_geladen("bs2_get_status"); kb_log("bs2_get_status: 2 (sensor klaar)"); }
    return 2;
}
int gsh_bs2_get_error_code(void) { return 0; }
void gsh_bs2_clear_error_code(void) {}
void gsh_bs2_get_error_count(int *a, int *b) { if (a) *a = 0; if (b) *b = 0; }
uint8_t gsh_bs2_calibration_get_state(void) { return 1; }
void gsh_bs2_calibration_init(int a) { (void)a; }
void gsh_bs2_calibration_init_2(void) {}
int gsh_bs2_calibration_wait(void) { return 1; }
void gsh_bs2_calibration_set(void) {}

void gsh_bs2_calibration_point_get(int *a, int *b, int *c, int *d, int *e, int *f, int *g, int *h)
{
    int *p[8] = {a, b, c, d, e, f, g, h};
    for (int i = 0; i < 8; i++) if (p[i]) *p[i] = 0;
}

void gsh_bs2_calibration_point_rate_get(int a, int b, int c, int d, int e, int f, int g, int h, float *r0, float *r1, float *r2, float *r3,
                                        float *r4, float *r5, float *r6, float *r7)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
    float *p[8] = {r0, r1, r2, r3, r4, r5, r6, r7};
    for (int i = 0; i < 8; i++) if (p[i]) *p[i] = 1.0f;
}

/* t_bs_calibout: 0x24 bytes (ballio.dll 0x10002410 kopieert 9 dwords); het eerste byte <> 0 betekent "geldig". */
uint8_t gsh_bs2_calibdata_get_wait(void) { return 1; }
uint8_t gsh_bs2_calibdata_get_data(void *uit)
{
    if (uit) memset(uit, 0, 0x24);
    return 0;
}

/* t_bs_version: 9 bytes (ballio.dll 0x10002220 wist er 9). */
uint8_t gsh_bs2_version_get_req(void *v)
{
    if (v) memset(v, 0, 9);
    return 1;
}
uint8_t gsh_bs2_version_get_wait(void *v) { (void)v; return 1; }

int gsh_bs2_environment_check(float *a, float *b)
{
    if (a) *a = 0.0f;
    if (b) *b = 0.0f;
    return 0;   /* demo.dll 0x100027ab: 0 = "OK" */
}

/* Niet gebruikt door de game (geen import): geen onbekende structuren aanraken. */
uint8_t gsh_bs2_environment_judge(void *hist, float *a, float *b, float c, float d)
{
    (void)hist; (void)a; (void)b; (void)c; (void)d;
    return 0;
}
void gsh_bs2_environment_param(float a, float b) { (void)a; (void)b; }
void gsh_bs2_alive_check(void) {}
int gsh_bs2_boot(int a) { (void)a; return 0; }
int gsh_bs2_get_hitdata(void *d, int n) { (void)d; (void)n; return 0; }
uint8_t gsh_bs2_histogram_get_req(void *h) { (void)h; return 0; }
uint8_t gsh_bs2_histogram_get_wait(void *h) { (void)h; return 0; }
void gsh_bs2_histogram_log_force_write(void) {}
void gsh_bs2_histogram_log_status(uint8_t s) { (void)s; }
uint8_t gsh_bs2_param1_get_req(void *p) { (void)p; return 0; }
uint8_t gsh_bs2_param1_get_wait(void *p) { (void)p; return 0; }
int gsh_bs2_param1_set_req(void *p) { (void)p; return 0; }
uint8_t gsh_bs2_param2_get_req(void *p) { (void)p; return 0; }
uint8_t gsh_bs2_param2_get_wait(void *p) { (void)p; return 0; }
int gsh_bs2_param2_set_req(void *p) { (void)p; return 0; }
int gsh_bs2_req_alivecheck(void) { return 0; }
int gsh_bs2_req_area(int a, int b, int c, int d, int e, int f, int g, int h)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
    return 0;
}
int gsh_bs2_req_modeset(int m) { (void)m; return 0; }
int gsh_bs2_req_reset(void) { return 0; }
void gsh_bs2_set_correct_mode(int m) { (void)m; }
void gsh_bs2_set_criterion(int a, int b, int c, int d, int e, int f, int g, int h)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
}
void gsh_bs2_set_rawmode(uint8_t m) { (void)m; }
int gsh_bs2_set_send_command(int a, uint8_t *b, int c) { (void)a; (void)b; (void)c; return 0; }
int gsh_bs2_set_send_command_2(int a, uint8_t *b, int c, void *d) { (void)a; (void)b; (void)c; (void)d; return 0; }
int gsh_bs2_shutdown(void) { return 0; }

/* De oudere BS-familie (bs_*): niet gebruikt door de game. */
int gsh_bs_boot(int a) { (void)a; return 0; }
int gsh_bs_get_error_code(void) { return 0; }
void gsh_bs_get_error_info(unsigned *a, int *b, int *c, int *d, int *e)
{
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
    if (d) *d = 0;
    if (e) *e = 0;
}
void gsh_bs_get_error_info_2(unsigned *a, int *b, int *c)
{
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
}
int gsh_bs_get_hitdata(void *d, int n) { (void)d; (void)n; return 0; }
int gsh_bs_get_status(void) { return 2; }
void gsh_bs_set_correct_mode(int m) { (void)m; }
void gsh_bs_set_criterion(int a, int b, int c, int d, int e, int f, int g, int h)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
}
void gsh_bs_set_range(int a, int b) { (void)a; (void)b; }
void gsh_bs_set_rawmode(uint8_t m) { (void)m; }
void gsh_bs_set_yoffset(int y) { (void)y; }
int gsh_bs_shutdown(void) { return 0; }

const char *gsh_GetDllStampStr(void) { return "rg-konami ballio.dll (RavenGun)"; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reden, LPVOID r)
{
    (void)r;
    if (reden == DLL_PROCESS_ATTACH) kb_dllmain_attach(h);
    return TRUE;
}
