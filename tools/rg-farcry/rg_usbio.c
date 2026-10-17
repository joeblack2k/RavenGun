/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_usbio.c - RavenGun-vervanger voor Usbioextreme.dll (Far Cry: Paradise Lost, Farcry_R.exe, 32 bits).
 *
 * Het origineel is de USB-HID-bibliotheek van het IO-bord van de kast ("USBIO Extreme"): de game importeert zes namen van
 * de klasse CUSBIO (constructor, destructor, CUSBIO_Init, CUSBIO_Update, CUSBIO_GetLastError, CUSBIO_Close) en leest de
 * invoer uit een record van 0x34 bytes per bord. Zonder bord meldt Init 0 en valt de game terug op DirectInput. Deze vervanger
 * meldt een bord en vult het record uit het bordbeeld van de daemon (tp-shm, indeling FarCryPipe), zonder TeknoParrot.
 * De rekenregel staat in rg_usbio_logic.h (daar ook de herkomst van elk veld).
 *
 * De methoden zijn thiscall (this in ecx, de rest op de stapel, de aangeroepene ruimt op). Hier fastcall met een
 * schaduwargument voor edx, wat byte voor byte dezelfde opstelling is; de namen komen uit usbioextreme.def.
 *
 * Alleen kernel32, geen C-runtime. Bouw: tools/rg-farcry/build.sh.
 *
 * Omgeving:
 *   RG_USBIO_OPS     vier letters uit T S U D: welke bedienknop op welk van de vier overige knopbits zit (standaard TSUD).
 *   RG_USBIO_INVERT  "x", "y" of "xy": as omdraaien.
 *   RG_USBIO_LOG     0 = geen logboek; anders pad van het logboek (standaard rg-usbio.log naast de DLL).
 */
#include <windows.h>
#include "rg_usbio_logic.h"

#define FC __attribute__((fastcall))

void *memset(void *d, int c, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

/* ------------------------------------------------------------------ */
/* logboek                                                             */

static HANDLE g_log = INVALID_HANDLE_VALUE;
static int g_log_lines;
static char g_log_path[MAX_PATH];

static char *put_s(char *p, char *end, const char *s)
{
    while (*s && p < end) *p++ = *s++;
    return p;
}

static char *put_u(char *p, char *end, unsigned v, int hex)
{
    char t[12];
    int n = 0;
    const unsigned base = hex ? 16u : 10u;
    do { t[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    while (n && p < end) *p++ = t[--n];
    return p;
}

static void logf3(const char *what, unsigned a, unsigned b, unsigned c)
{
    char line[160], *p = line, *e = line + sizeof line - 2;
    DWORD w;
    if (g_log == INVALID_HANDLE_VALUE || g_log_lines > 400) return;
    g_log_lines++;
    p = put_u(p, e, GetTickCount(), 0);
    p = put_s(p, e, " ");
    p = put_s(p, e, what);
    p = put_s(p, e, " a=");
    p = put_u(p, e, a, 1);
    p = put_s(p, e, " b=");
    p = put_u(p, e, b, 1);
    p = put_s(p, e, " c=");
    p = put_u(p, e, c, 1);
    *p++ = '\r';
    *p++ = '\n';
    WriteFile(g_log, line, (DWORD)(p - line), &w, NULL);
}

static void open_log(HMODULE self)
{
    char env[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("RG_USBIO_LOG", env, sizeof env);
    if (n && n < sizeof env && env[0] == '0' && env[1] == 0) return;
    if (n && n < sizeof env) {
        DWORD i;
        for (i = 0; i <= n; i++) g_log_path[i] = env[i];
    } else {
        DWORD len = GetModuleFileNameA(self, g_log_path, sizeof g_log_path - 16);
        while (len && g_log_path[len - 1] != '\\' && g_log_path[len - 1] != '/') len--;
        {
            const char *name = "rg-usbio.log";
            DWORD i = 0;
            while (name[i]) { g_log_path[len + i] = name[i]; i++; }
            g_log_path[len + i] = 0;
        }
    }
    g_log = CreateFileA(g_log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* ------------------------------------------------------------------ */
/* toestand                                                            */

static rg_usbio_state g_st;
static const volatile unsigned char *g_shm;
static DWORD g_shm_next_try;
static unsigned g_updates;
static unsigned g_last_ctl = 0xffffffffu;

static const volatile unsigned char *shm(void)
{
    if (!g_shm) {
        HANDLE h;
        const DWORD now = GetTickCount();
        if ((LONG)(now - g_shm_next_try) < 0) return 0;
        g_shm_next_try = now + 1000;
        h = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (!h) return 0;
        g_shm = (const volatile unsigned char *)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 64);
        if (g_shm) logf3("shm gekoppeld", 0, 0, 0);
    }
    return g_shm;
}

/* ------------------------------------------------------------------ */
/* de zes namen die de game gebruikt                                   */

/* De klasse in de game: +0 aantal borden (int), +4 wijzer naar de records. */
static unsigned char **recs(void *self) { return (unsigned char **)((unsigned char *)self + 4); }

FC void *rg_ctor(void *self) __asm__("_rg_ctor");
FC void *rg_ctor(void *self)
{
    *(int *)self = 0;
    *recs(self) = 0;
    return self;
}

FC void rg_close(void *self) __asm__("_rg_close");
FC void rg_close(void *self)
{
    unsigned char *b = *recs(self);
    if (b) HeapFree(GetProcessHeap(), 0, b);
    *recs(self) = 0;
    *(int *)self = 0;
}

FC int rg_init(void *self) __asm__("_rg_init");
FC int rg_init(void *self)
{
    if (*recs(self)) HeapFree(GetProcessHeap(), 0, *recs(self));
    *recs(self) = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RG_REC_SIZE * 2);
    if (!*recs(self)) { *(int *)self = 0; return 0; }
    *(int *)self = 1;
    logf3("Init: een bord gemeld", (unsigned)(UINT_PTR)self, 1, 0);
    return 1;
}

/* thiscall(this, int i): fastcall(this, edx-schaduw, i); de i ligt op de stapel, ret 4 */
FC void rg_update(void *self, int shadow_edx, int idx) __asm__("_rg_update");
FC void rg_update(void *self, int shadow_edx, int idx)
{
    unsigned char *base = *recs(self);
    unsigned char *rec;
    const volatile unsigned char *m;
    unsigned char snap[64];
    (void)shadow_edx;
    if (!base || idx < 0 || idx > 1) return;
    rec = base + idx * RG_REC_SIZE;
    if (idx != 0) {
        memset(rec, 0, RG_REC_SIZE);
        return;
    }
    m = shm();
    if (m) {
        int i;
        for (i = 0; i < 64; i++) snap[i] = m[i];
    }
    rg_usbio_fill(&g_st, m ? snap : 0, rec, GetTickCount());
    g_updates++;
    {
        const unsigned ctl = m ? rg_usbio_ctl(snap) : 0;
        if (ctl != g_last_ctl) {
            logf3("ctl", ctl, rec[RG_REC_BTN_HI] | (rec[RG_REC_BTN_LO] << 8), g_st.coin1);
            g_last_ctl = ctl;
        }
        if (g_updates == 1 || g_updates == 600)
            logf3("as", rec[RG_REC_X1] | (rec[RG_REC_Y1] << 8), rec[RG_REC_X2] | (rec[RG_REC_Y2] << 8), g_updates);
    }
}

FC const char *rg_lasterror(void *self, int shadow_edx, int idx) __asm__("_rg_lasterror");
FC const char *rg_lasterror(void *self, int shadow_edx, int idx)
{
    (void)self; (void)shadow_edx; (void)idx;
    return 0;
}

/* ------------------------------------------------------------------ */
/* de rest van de uitvoer: zelfde aantal stapelargumenten als het origineel, geen werking */

#define STUB0(name, ret) FC ret name(void *s) __asm__("_" #name); FC ret name(void *s) { (void)s; return (ret)0; }
#define STUB1(name, ret) FC ret name(void *s, int e, int a) __asm__("_" #name); FC ret name(void *s, int e, int a) { (void)s; (void)e; (void)a; return (ret)0; }
#define STUB2(name, ret) FC ret name(void *s, int e, int a, int b) __asm__("_" #name); FC ret name(void *s, int e, int a, int b) { (void)s; (void)e; (void)a; (void)b; return (ret)0; }
#define STUB3(name, ret) FC ret name(void *s, int e, int a, int b, int c) __asm__("_" #name); FC ret name(void *s, int e, int a, int b, int c) { (void)s; (void)e; (void)a; (void)b; (void)c; return (ret)0; }
#define STUB4(name, ret) FC ret name(void *s, int e, int a, int b, int c, int d) __asm__("_" #name); FC ret name(void *s, int e, int a, int b, int c, int d) { (void)s; (void)e; (void)a; (void)b; (void)c; (void)d; return (ret)0; }

STUB2(rg_get_coin_counter_mode, int)
STUB2(rg_get_coin_multiplier, int)
STUB1(rg_get_coin_pulse_time, int)
STUB1(rg_get_encoder_scale, int)
STUB0(rg_get_encoder_thread_state, int)
STUB1(rg_get_id, int)
STUB1(rg_get_input_mode, int)
STUB2(rg_get_mech_coin_multiplier, int)
STUB3(rg_read_spi, int)
STUB3(rg_set_coin_counter_mode, int)
STUB3(rg_set_coin_multiplier, int)
STUB2(rg_set_coin_pulse_time, int)
STUB2(rg_set_encoder_scale, int)
STUB1(rg_set_encoder_thread_state, int)
STUB2(rg_set_input_mode, int)
STUB3(rg_set_mech_coin_multiplier, int)
STUB4(rg_set_output_value, int)
STUB3(rg_write_spi, int)
STUB1(rg_enum_close_hid, int)
STUB2(rg_enum_find_hid, int)
STUB3(rg_enum_open_hid, int)

/* operator= van CUSBIO en van CEnumerator: de acht bytes van het origineel kopieren (CUSBIO) of niets (CEnumerator geeft this) */
FC void *rg_assign_usbio(void *self, int shadow_edx, const unsigned *src) __asm__("_rg_assign_usbio");
FC void *rg_assign_usbio(void *self, int shadow_edx, const unsigned *src)
{
    (void)shadow_edx;
    ((unsigned *)self)[0] = src[0];
    ((unsigned *)self)[1] = src[1];
    return self;
}

FC void *rg_assign_enum(void *self, int shadow_edx, const void *src) __asm__("_rg_assign_enum");
FC void *rg_assign_enum(void *self, int shadow_edx, const void *src)
{
    (void)shadow_edx; (void)src;
    return self;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID res)
{
    char env[16];
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        open_log(h);
        rg_usbio_state_init(&g_st);
        if (GetEnvironmentVariableA("RG_USBIO_OPS", env, sizeof env) && env[0])
            if (!rg_usbio_set_ops(&g_st, env)) logf3("RG_USBIO_OPS ongeldig, standaard blijft", 0, 0, 0);
        if (GetEnvironmentVariableA("RG_USBIO_INVERT", env, sizeof env)) {
            int i;
            for (i = 0; env[i]; i++) {
                if (env[i] == 'x' || env[i] == 'X') g_st.invert_x = 1;
                if (env[i] == 'y' || env[i] == 'Y') g_st.invert_y = 1;
            }
        }
        logf3("geladen (rg-usbio, Far Cry)", g_st.ops[0], g_st.invert_x, g_st.invert_y);
    }
    return TRUE;
}
