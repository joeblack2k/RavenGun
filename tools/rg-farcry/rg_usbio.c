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
 *   RG_USBIO_KEYS    1 = de toetsen van het virtuele toetsenbord van de daemon (1 2 5 6 0 F2..F5) WEL aan de game laten zien.
 *
 * DE TOETSEN VAN DE DAEMON (speelproef 2026-10-18: de muntknop opende het testmenu). Voor elke speler- en systeemknop drukt de daemon
 * OOK een toets op zijn virtuele toetsenbord (de TeknoParrot-afspraak: munt = '5', start = '1', test = '0'/F2). De game leest naast
 * het bord ook het toetsenbord via DirectInput8 (CryInput), en bij Global VR is '5' daar TEST (zelfde conventie als Aliens
 * Extermination, waar hetzelfde gebeurde). Het bord krijgt de knoppen al uit het bordbeeld; het toetsenbord moet ze daarom NIET zien.
 * De DLL haakt DirectInput8Create in de importtabel van de exe, daarna CreateDevice van de IDirectInput8 en GetDeviceState /
 * GetDeviceData van elk toetsenbordapparaat, en haalt die toetsen eruit. Muis en joysticks blijven ongemoeid.
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


/* ------------------------------------------------------------------ */
/* DirectInput8: de toetsen van het virtuele toetsenbord van de daemon verbergen                      */

typedef HRESULT (WINAPI *di_create_fn)(HINSTANCE, DWORD, const GUID *, void **, void *);
typedef HRESULT (WINAPI *di_createdev_fn)(void *, const GUID *, void **, void *);
typedef HRESULT (WINAPI *dev_getstate_fn)(void *, DWORD, void *);
typedef HRESULT (WINAPI *dev_getdata_fn)(void *, DWORD, void *, DWORD *, DWORD);

#define VT_MAX 6
static struct { void **vtbl; void *orig; } g_vt_dev[VT_MAX];     /* vtables van IDirectInput8A/W: originele CreateDevice */
static struct { void **vtbl; void *gs, *gd; } g_vt_kb[VT_MAX];   /* vtables van apparaten: originele GetDeviceState/GetDeviceData */
static void *g_kb_dev[16];                                      /* apparaten waarvan we weten dat het toetsenborden zijn */
static di_create_fn g_real_create;
static int g_keys_pass;
static int g_key_logged[256];

static void patch_slot(void **vtbl, int slot, void *nieuw)
{
    DWORD prot;
    if (!VirtualProtect(&vtbl[slot], sizeof(void *), PAGE_READWRITE, &prot)) return;
    vtbl[slot] = nieuw;
    VirtualProtect(&vtbl[slot], sizeof(void *), prot, &prot);
}

static int is_kb(void *dev)
{
    int i;
    for (i = 0; i < 16; i++) if (g_kb_dev[i] == dev) return 1;
    return 0;
}

static void mark_kb(void *dev)
{
    int i;
    if (is_kb(dev)) return;
    for (i = 0; i < 16; i++) if (!g_kb_dev[i]) { g_kb_dev[i] = dev; return; }
}

static void note_key(unsigned k)
{
    if (k < 256 && !g_key_logged[k]) {
        g_key_logged[k] = 1;
        logf3("toets uit DirectInput onderdrukt (DIK)", k, 0, 0);
    }
}

static void *kb_orig(void *self, int gd)
{
    int i;
    void **vt = *(void ***)self;
    for (i = 0; i < VT_MAX; i++) if (g_vt_kb[i].vtbl == vt) return gd ? g_vt_kb[i].gd : g_vt_kb[i].gs;
    return 0;
}

static HRESULT WINAPI hk_GetDeviceState(void *self, DWORD cb, void *data)
{
    dev_getstate_fn orig = (dev_getstate_fn)kb_orig(self, 0);
    HRESULT hr;
    if (!orig) return E_FAIL;
    hr = orig(self, cb, data);
    if (hr >= 0 && cb == 256 && data && !g_keys_pass) {      /* 256 bytes = toetsenbordtoestand (c_dfDIKeyboard) */
        unsigned char *st = (unsigned char *)data;
        unsigned k;
        mark_kb(self);
        for (k = 0; k < 256; k++)
            if (rg_usbio_dik_blocked(k) && (st[k] & 0x80)) note_key(k);
        rg_usbio_filter_state(st);
    }
    return hr;
}

static HRESULT WINAPI hk_GetDeviceData(void *self, DWORD cbobj, void *rgdod, DWORD *inout, DWORD flags)
{
    dev_getdata_fn orig = (dev_getdata_fn)kb_orig(self, 1);
    HRESULT hr;
    if (!orig) return E_FAIL;
    hr = orig(self, cbobj, rgdod, inout, flags);
    if (hr >= 0 && is_kb(self) && !g_keys_pass && rgdod && inout && cbobj >= 4 && cbobj <= 64) {
        unsigned char *p = (unsigned char *)rgdod;
        DWORD n = *inout, i;
        for (i = 0; i < n; i++) {
            const unsigned char *e = p + (size_t)i * cbobj;
            DWORD ofs = *(const DWORD *)e;
            if (rg_usbio_dik_blocked(ofs)) note_key(ofs);
        }
        *inout = rg_usbio_filter_data(p, n, cbobj);
    }
    return hr;
}

static int is_kb_guid(const GUID *g)
{
    if (!g) return 0;
    return (g->Data1 == 0x6F1D2B61u || g->Data1 == 0x6F1D2B82u || g->Data1 == 0x6F1D2B83u) && g->Data2 == 0xD5A0 && g->Data3 == 0x11CF;
}

static HRESULT WINAPI hk_CreateDevice(void *self, const GUID *guid, void **ppdev, void *outer)
{
    di_createdev_fn orig = 0;
    HRESULT hr;
    int i;
    void **vt = *(void ***)self;
    for (i = 0; i < VT_MAX; i++) if (g_vt_dev[i].vtbl == vt) orig = (di_createdev_fn)g_vt_dev[i].orig;
    if (!orig) return E_FAIL;
    hr = orig(self, guid, ppdev, outer);
    if (hr >= 0 && ppdev && *ppdev) {
        void **dvt = *(void ***)*ppdev;
        int known = 0, free_i = -1;
        for (i = 0; i < VT_MAX; i++) {
            if (g_vt_kb[i].vtbl == dvt) known = 1;
            else if (!g_vt_kb[i].vtbl && free_i < 0) free_i = i;
        }
        if (!known && free_i >= 0) {       /* een vtable (A of W) die we nog niet hadden: haken */
            g_vt_kb[free_i].vtbl = dvt;
            g_vt_kb[free_i].gs = dvt[9];
            g_vt_kb[free_i].gd = dvt[10];
            patch_slot(dvt, 9, (void *)hk_GetDeviceState);
            patch_slot(dvt, 10, (void *)hk_GetDeviceData);
        }
        if (is_kb_guid(guid)) { mark_kb(*ppdev); logf3("toetsenbordapparaat gehaakt", (unsigned)(UINT_PTR)*ppdev, 0, 0); }
    }
    return hr;
}

static HRESULT WINAPI hk_DirectInput8Create(HINSTANCE h, DWORD ver, const GUID *riid, void **out, void *outer)
{
    HRESULT hr = g_real_create ? g_real_create(h, ver, riid, out, outer) : E_FAIL;
    if (hr >= 0 && out && *out) {
        void **vt = *(void ***)*out;
        int i, known = 0, free_i = -1;
        for (i = 0; i < VT_MAX; i++) {
            if (g_vt_dev[i].vtbl == vt) known = 1;
            else if (!g_vt_dev[i].vtbl && free_i < 0) free_i = i;
        }
        if (!known && free_i >= 0) {
            g_vt_dev[free_i].vtbl = vt;
            g_vt_dev[free_i].orig = vt[3];
            patch_slot(vt, 3, (void *)hk_CreateDevice);
            logf3("IDirectInput8 gehaakt", (unsigned)(UINT_PTR)vt, 0, 0);
        }
    }
    return hr;
}

static int same_name_nocase(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* Zet DirectInput8Create in de importtabel van de exe om. Staat dinput8 (nog) niet in het proces of vindt de tabel hem niet, dan gebeurt er niets. */
static void hook_dinput(void)
{
    HMODULE di = GetModuleHandleA("dinput8.dll");
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_DATA_DIRECTORY dd;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    int gehaakt = 0;
    if (!di || !base || dos->e_magic != IMAGE_DOS_SIGNATURE) { logf3("dinput8 niet gehaakt: module ontbreekt", 0, 0, 0); return; }
    g_real_create = (di_create_fn)(void *)GetProcAddress(di, "DirectInput8Create");
    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!g_real_create || !dd.VirtualAddress) { logf3("dinput8 niet gehaakt: geen DirectInput8Create of importtabel", 0, 0, 0); return; }
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dd.VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA32 *t;
        if (!same_name_nocase((const char *)(base + imp->Name), "dinput8.dll")) continue;
        for (t = (IMAGE_THUNK_DATA32 *)(base + imp->FirstThunk); t->u1.Function; t++) {
            if ((void *)(UINT_PTR)t->u1.Function == (void *)g_real_create) {
                DWORD prot;
                if (VirtualProtect(&t->u1.Function, sizeof(DWORD), PAGE_READWRITE, &prot)) {
                    t->u1.Function = (DWORD)(UINT_PTR)hk_DirectInput8Create;
                    VirtualProtect(&t->u1.Function, sizeof(DWORD), prot, &prot);
                    gehaakt++;
                }
            }
        }
    }
    logf3("dinput8: DirectInput8Create omgezet in de importtabel", (unsigned)gehaakt, g_keys_pass, 0);
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
        if (GetEnvironmentVariableA("RG_USBIO_KEYS", env, sizeof env) && env[0] == '1') g_keys_pass = 1;
        logf3("geladen (rg-usbio, Far Cry)", g_st.ops[0], g_st.invert_x, g_st.invert_y);
        hook_dinput();
    }
    return TRUE;
}
