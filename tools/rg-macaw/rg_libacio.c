/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_libacio.c - de vervanger van Konami's libacio.dll voor Silent Scope: Bone Eater (arkndd.dll, 64 bit).
 *
 * Het origineel praat via een seriele poort met de ACIO-bus van de kast (een bmpu-bord voor knoppen en munten, een nddb-bord
 * voor het geweer, een icca-kaartlezer). Deze vervanger exporteert dezelfde 35 functies die arkndd.dll importeert
 * (libacio.exports), meldt dat de bussen er zijn en levert de invoer uit het gedeelde geheugen van de directe route
 * (de benoemde mapping "TeknoParrot_JvsState", indeling KonamiAcio uit packaging/tp-shm-profiles.json).
 *
 * De indeling van het statusblok is gemeten aan arkndd.dll (rg_macaw_logic.h, rgm_acio_pack_bmpu). Wat NIET gemeten is:
 * waar de twee scopeknoppen van het geweer in de bytes zitten. Het spel heeft er drie velden voor (trekker, scope links,
 * scope rechts, arkndd.dll 0x180041c40..0x180041c60); de trekker hangt aan masker-bit 10 (L[4] & 0x20). Daarom twee
 * instelbare uitwijkplekken, zonder herbouwen:
 *   RG_ACIO_SCOPE_L4=0xC0     welke bits van L[4] de scope zet (standaard beide: 0x80 en 0x40)
 *   RG_ACIO_NDDB_SCOPE=O:M,O:M  bytes in het nddb-blok (O = offset in het blok, M = masker) die de scope ook zet
 * Logboek: RG_ACIO_LOG=<bestand> (eerste aanroep per functie + de eerste 20 statusblokken).
 *
 * Geen netwerk, geen seriele poort, geen beveiligingsstekker: arkndd.dll importeert de secplug-functies niet.
 */
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "rg_macaw_logic.h"

static const volatile uint8_t *g_shm;
static DWORD g_next_try;
static uint32_t g_prev_ctl, g_coins;
static CRITICAL_SECTION g_cs;
static int g_cs_ready;
static char g_logpath[MAX_PATH];
static unsigned g_block_logs;
static uint8_t g_scope_l4 = 0xC0;
static struct { int off; uint8_t mask; } g_nddb_scope[8];
static int g_nddb_scope_n;

static void rg_log(const char *fmt, ...)
{
    FILE *f;
    va_list ap;
    if (!g_logpath[0]) return;
    f = fopen(g_logpath, "a");
    if (!f) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static int g_first[64];
#define FIRST(n) do { if (!g_first[(n)]) { g_first[(n)] = 1; rg_log("eerste aanroep: %s", __func__); } } while (0)

static void parse_env(void)
{
    char buf[256];
    DWORD n = GetEnvironmentVariableA("RG_ACIO_LOG", g_logpath, sizeof g_logpath);
    if (n == 0 || n >= sizeof g_logpath) g_logpath[0] = 0;
    n = GetEnvironmentVariableA("RG_ACIO_SCOPE_L4", buf, sizeof buf);
    if (n > 0 && n < sizeof buf) g_scope_l4 = (uint8_t)strtoul(buf, NULL, 0);
    n = GetEnvironmentVariableA("RG_ACIO_NDDB_SCOPE", buf, sizeof buf);
    if (n > 0 && n < sizeof buf) {
        char *p = buf;
        while (*p && g_nddb_scope_n < 8) {
            char *e;
            long off = strtol(p, &e, 0);
            if (*e != ':') break;
            g_nddb_scope[g_nddb_scope_n].off = (int)off;
            g_nddb_scope[g_nddb_scope_n].mask = (uint8_t)strtoul(e + 1, &p, 0);
            g_nddb_scope_n++;
            if (*p == ',') p++;
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res)
{
    (void)h; (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&g_cs);
        g_cs_ready = 1;
        parse_env();
        rg_log("rg-libacio geladen (scope_l4=0x%02x, nddb_scope_n=%d)", g_scope_l4, g_nddb_scope_n);
    }
    return TRUE;
}

/* ctl en assen uit het gedeelde geheugen; zonder mapping (de daemon draait niet) alles nul. */
static void poll(uint32_t *ctl, uint16_t *x, uint16_t *y)
{
    *ctl = 0; *x = 0x8000; *y = 0x8000;
    if (!g_shm) {
        HANDLE h;
        const DWORD now = GetTickCount();
        if ((LONG)(now - g_next_try) < 0) return;
        g_next_try = now + 1000;
        h = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (!h) return;
        g_shm = (const volatile uint8_t *)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 64);
        if (!g_shm) return;
        rg_log("tp-shm gekoppeld");
    }
    *ctl = (uint32_t)g_shm[8] | ((uint32_t)g_shm[9] << 8) | ((uint32_t)g_shm[10] << 16) | ((uint32_t)g_shm[11] << 24);
    *x = (uint16_t)(g_shm[16] | (g_shm[17] << 8));
    *y = (uint16_t)(g_shm[20] | (g_shm[21] << 8));
}

#define API __declspec(dllexport)

/* ---- het bmpu-apparaat: knoppen, munten, geweerpositie ------------------------------------------------------------------ */

API int ac_io_bmpu_update_control_status_buffer(void)
{
    uint32_t ctl; uint16_t x, y;
    FIRST(0);
    EnterCriticalSection(&g_cs);
    poll(&ctl, &x, &y);
    g_coins = rgm_acio_coin_total(g_coins, g_prev_ctl, ctl);
    g_prev_ctl = ctl;
    LeaveCriticalSection(&g_cs);
    return 1;
}

API int ac_io_bmpu_get_control_status_buffer(uint8_t *out)
{
    uint32_t ctl; uint16_t x, y;
    FIRST(1);
    if (!out) return 0;
    poll(&ctl, &x, &y);
    rgm_acio_pack_bmpu(ctl, x, y, out);
    /* instelbare scope: alleen de bits die de standaard zette vervangen */
    if (ctl & RGM_C_SCOPE) {
        out[4] = (uint8_t)((out[4] & ~0xC0) | (g_scope_l4 & 0xC0));
    }
    if (g_block_logs < 20 && (ctl || g_block_logs == 0)) {
        g_block_logs++;
        rg_log("bmpu-blok: ctl=%08x x=%04x y=%04x -> L4=%02x L5=%02x L7=%02x L8..11=%02x %02x %02x %02x", ctl, x, y, out[4], out[5],
               out[7], out[8], out[9], out[10], out[11]);
    }
    return 1;
}

API int ac_io_bmpu_current_coinstock(int idx, uint32_t *out)
{
    (void)idx;
    FIRST(2);
    if (out) *out = g_coins;
    return 1;
}

/* ---- het nddb-apparaat: geweer (alleen uitgangen en een leeg statusblok) ----------------------------------------------- */

API int ac_io_nddb_update_control_status_buffer(void) { FIRST(3); return 1; }

API int ac_io_nddb_get_control_status_buffer(uint8_t *out)
{
    int i;
    uint32_t ctl; uint16_t x, y;
    FIRST(4);
    if (!out) return 0;
    /* Het spel geeft het blok op +0x38 van een lokale struct van 0xac bytes; de rest van de struct wordt daarna overschreven
     * (arkndd.dll 0x18000b028), maar schrijf toch niet verder dan de eerste 0x40 bytes. */
    memset(out, 0, 0x40);
    poll(&ctl, &x, &y);
    if (ctl & RGM_C_SCOPE)
        for (i = 0; i < g_nddb_scope_n; i++)
            if (g_nddb_scope[i].off >= 0 && g_nddb_scope[i].off < 0x40) out[g_nddb_scope[i].off] |= g_nddb_scope[i].mask;
    return 1;
}

API int ac_io_nddb_create_get_status_thread(void) { FIRST(5); return 1; }
API int ac_io_nddb_destroy_get_status_thread(void) { FIRST(6); return 1; }
API int ac_io_nddb_control_pwm(int a, int b) { (void)a; (void)b; FIRST(7); return 1; }
API int ac_io_nddb_control_solenoide(int a, int b) { (void)a; (void)b; FIRST(8); return 1; }
API int ac_io_nddb_req_solenoide_control(const void *p) { (void)p; FIRST(9); return 1; }

/* ---- bus en beheer --------------------------------------------------------------------------------------------------- */

API int ac_io_begin(int a, const char *port, void *b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    FIRST(10);
    rg_log("ac_io_begin(poort=%s)", port ? port : "(null)");
    return 1;
}
API int ac_io_begin_get_status(int a, int b) { (void)a; (void)b; FIRST(11); return 1; }
API int ac_io_end(int a) { (void)a; FIRST(12); return 1; }
API int ac_io_update(int a) { (void)a; FIRST(13); return 1; }

API int ac_io_is_active2(int dev, int *out, int x)
{
    (void)dev; (void)x;
    FIRST(14);
    if (out) *out = 0;
    return 1;
}
API int ac_io_is_active_device(int dev, int x) { (void)dev; (void)x; FIRST(15); return 1; }

API int ac_io_get_version(uint8_t *buf, int n, int x)
{
    (void)x;
    FIRST(16);
    if (buf && n > 0) memset(buf, 0, n < 8 ? (size_t)n : 8);
    return 1;
}

API int ac_io_get_rs232c_status(void *buf, int x)
{
    (void)x;
    FIRST(17);
    if (buf) memset(buf, 0, 0x34);     /* tellers van seriele fouten: allemaal nul */
    return 1;
}

API int ac_io_bmpu_create_get_status_thread(void) { FIRST(18); return 1; }
API int ac_io_bmpu_destroy_get_status_thread(void) { FIRST(19); return 1; }
API int ac_io_bmpu_set_output_mode(int m) { (void)m; FIRST(20); return 1; }
API int ac_io_bmpu_control_coin_blocker_close(int a) { (void)a; FIRST(21); return 1; }
API int ac_io_bmpu_control_coin_blocker_open(int a) { (void)a; FIRST(22); return 1; }
API int ac_io_bmpu_lock_coincounter(int a) { (void)a; FIRST(23); return 1; }
API int ac_io_bmpu_unlock_coincounter(int a) { (void)a; FIRST(24); return 1; }
API int ac_io_bmpu_control_led_bright(int a, int b) { (void)a; (void)b; FIRST(25); return 1; }
API int ac_io_bmpu_control_led_bright_pack(int a, const void *p, int n) { (void)a; (void)p; (void)n; FIRST(26); return 1; }

/* ---- de kaartlezer (icca): niet aanwezig; regio Asia zet de kaart toch uit ----------------------------------------------- */

API int ac_io_icca_cardunit_init(int a) { (void)a; FIRST(27); return 1; }
API int ac_io_icca_cardunit_init_isfinished(int a, int *out)
{
    (void)a;
    FIRST(28);
    if (out) *out = 4;                  /* 4 = klaar (arkndd.dll 0x18000b63e: 4 en 5 zijn de uitkomsten die het spel kent) */
    return 1;
}
API int ac_io_icca_device_control_iccard_power_supply_on(int a) { (void)a; FIRST(29); return 1; }
API int ac_io_icca_device_control_iccard_power_supply_off(int a) { (void)a; FIRST(30); return 1; }
API int ac_io_icca_device_control_isfinished(int a, int *out) { (void)a; FIRST(31); if (out) *out = 4; return 1; }
API int ac_io_icca_req_uid(int a) { (void)a; FIRST(32); return 1; }
API int ac_io_icca_req_uid_isfinished(int a, int *out) { (void)a; FIRST(33); if (out) *out = 0; return 1; }
API int ac_io_icca_get_uid_felica(int a, void *p) { (void)a; (void)p; FIRST(34); return 0; }   /* geen kaart */
