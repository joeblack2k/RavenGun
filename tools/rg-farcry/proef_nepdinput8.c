/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * proef_nepdinput8.c -- een nep-dinput8.dll ALLEEN voor de proef van de toetsenbordfilter (tools/rg-farcry/wine-proef.sh). Zonder scherm
 * komt een toets uit SendInput onder Wine niet bij DirectInput aan (gemeten: GetAsyncKeyState ziet hem, het DirectInput-apparaat niet),
 * dus levert deze DLL zelf een toetsenbordapparaat dat de toetsen '5', 'W' en '6' vasthoudt: GetDeviceState met die drie toetsen aan,
 * GetDeviceData met drie gebeurtenissen (neer). De echte filter (rg_usbio.c) krijgt hiermee dezelfde interfaces te zien als bij Wine's
 * dinput8: een IDirectInput8A met CreateDevice op vtable-plaats 3 en een apparaat met GetDeviceState/GetDeviceData op 9 en 10.
 * De proef met Wine's eigen dinput8 (zonder deze DLL) controleert dat de haken de echte aanroepen ongemoeid doorgeven.
 */
#define DIRECTINPUT_VERSION 0x0800
#define CINTERFACE
#include <windows.h>
#include <dinput.h>

typedef struct { const IDirectInputDevice8AVtbl *lpVtbl; LONG ref; int data_gegeven; } nep_dev;
typedef struct { const IDirectInput8AVtbl *lpVtbl; LONG ref; } nep_di;

static HRESULT WINAPI d_qi(IDirectInputDevice8A *s, REFIID r, void **o) { (void)r; *o = s; return S_OK; }
static ULONG WINAPI d_addref(IDirectInputDevice8A *s) { return (ULONG)InterlockedIncrement(&((nep_dev *)s)->ref); }
static ULONG WINAPI d_release(IDirectInputDevice8A *s) { return (ULONG)InterlockedDecrement(&((nep_dev *)s)->ref); }
static HRESULT WINAPI d_ok0(IDirectInputDevice8A *s) { (void)s; return S_OK; }
static HRESULT WINAPI d_setprop(IDirectInputDevice8A *s, REFGUID g, LPCDIPROPHEADER h) { (void)s; (void)g; (void)h; return S_OK; }
static HRESULT WINAPI d_setfmt(IDirectInputDevice8A *s, LPCDIDATAFORMAT f) { (void)s; (void)f; return S_OK; }
static HRESULT WINAPI d_setcoop(IDirectInputDevice8A *s, HWND w, DWORD f) { (void)s; (void)w; (void)f; return S_OK; }

static HRESULT WINAPI d_getstate(IDirectInputDevice8A *s, DWORD cb, LPVOID data)
{
    unsigned char *st = (unsigned char *)data;
    DWORD i;
    (void)s;
    for (i = 0; i < cb; i++) st[i] = 0;
    if (cb == 256) { st[DIK_5] = 0x80; st[DIK_W] = 0x80; st[DIK_6] = 0x80; }
    return DI_OK;
}

static HRESULT WINAPI d_getdata(IDirectInputDevice8A *s, DWORD cb, LPDIDEVICEOBJECTDATA ev, LPDWORD n, DWORD flags)
{
    nep_dev *d = (nep_dev *)s;
    static const DWORD ofs[3] = { DIK_5, DIK_W, DIK_6 };
    unsigned char *p = (unsigned char *)ev;
    DWORD i, b;
    (void)flags;
    if (!ev || !n) return DI_OK;
    if (d->data_gegeven || *n < 3 || cb < 8) { *n = 0; return DI_OK; }
    for (i = 0; i < 3; i++) {
        for (b = 0; b < cb; b++) p[i * cb + b] = 0;
        *(DWORD *)(p + i * cb) = ofs[i];
        *(DWORD *)(p + i * cb + 4) = 0x80;
    }
    *n = 3;
    d->data_gegeven = 1;
    return DI_OK;
}

static IDirectInputDevice8AVtbl g_dvt;
static nep_dev g_dev = { &g_dvt, 1, 0 };

static HRESULT WINAPI i_qi(IDirectInput8A *s, REFIID r, void **o) { (void)r; *o = s; return S_OK; }
static ULONG WINAPI i_addref(IDirectInput8A *s) { return (ULONG)InterlockedIncrement(&((nep_di *)s)->ref); }
static ULONG WINAPI i_release(IDirectInput8A *s) { return (ULONG)InterlockedDecrement(&((nep_di *)s)->ref); }

static HRESULT WINAPI i_createdevice(IDirectInput8A *s, REFGUID g, LPDIRECTINPUTDEVICE8A *out, LPUNKNOWN outer)
{
    (void)s; (void)outer;
    if (!IsEqualGUID(g, &GUID_SysKeyboard)) return DIERR_DEVICENOTREG;
    *out = (LPDIRECTINPUTDEVICE8A)&g_dev;
    return DI_OK;
}

static IDirectInput8AVtbl g_ivt;
static nep_di g_di = { &g_ivt, 1 };

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN outer)
{
    (void)h; (void)ver; (void)riid; (void)outer;
    g_dvt.QueryInterface = d_qi;
    g_dvt.AddRef = d_addref;
    g_dvt.Release = d_release;
    g_dvt.SetProperty = d_setprop;
    g_dvt.Acquire = d_ok0;
    g_dvt.Unacquire = d_ok0;
    g_dvt.GetDeviceState = d_getstate;
    g_dvt.GetDeviceData = d_getdata;
    g_dvt.SetDataFormat = d_setfmt;
    g_dvt.SetCooperativeLevel = d_setcoop;
    g_ivt.QueryInterface = i_qi;
    g_ivt.AddRef = i_addref;
    g_ivt.Release = i_release;
    g_ivt.CreateDevice = i_createdevice;
    *out = &g_di;
    return DI_OK;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { (void)h; (void)reason; (void)r; return TRUE; }
