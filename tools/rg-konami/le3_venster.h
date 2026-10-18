/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-konami: venstermodus voor Lethal Enforcers 3 (alleen met RG_KONAMI_VENSTER=1), in onze iob.dll omdat die al in het spelproces
 * zit en vroeg genoeg laadt. Proef 17/18 (2026-10-18): zwart beeld, met en zonder gamescope, en soms een hang van de hoofddraad in
 * PeekMessageA. Gelezen in game.exe (WinMain 0x40a460):
 *
 *   - IDirect3D8::GetAdapterCount (begrensd op 2) bepaalt hoeveel apparaten de game maakt: een per beeldscherm (0x40a4f5; de kast
 *     heeft twee schermen van 640x480 naast elkaar);
 *   - D3DPRESENT_PARAMETERS (0x40a512..0x40a575): achterbuffer 640x480 in het formaat van de huidige schermmodus, SwapEffect 4
 *     (COPY_VSYNC), hDeviceWindow = het spelvenster, **Windowed = 0** (volledig scherm, exclusief), 60 Hz, interval ONE;
 *   - CreateDevice per adapter (0x42af0c, BehaviorFlags 0x40 = HARDWARE_VERTEXPROCESSING);
 *   - daarna SetWindowPos(spelvenster, 0, 0, 0, 1280, 480, NOZORDER|FRAMECHANGED) (0x40a5d5): het venster beslaat beide schermen.
 *
 * Volledig scherm betekent een modusomschakeling naar 640x480 in Wine (X11/Xwayland), en dat is precies wat hier niet lukt. Deze
 * schil zet de game in een gewoon venster van 640x480 (gamescope schaalt het venster naar de tv):
 *
 *   - de importtabel van game.exe: d3d8!Direct3DCreate8 en user32!SetWindowPos wijzen naar ons (alleen in het geheugen van dit proces,
 *     niets op schijf);
 *   - het IDirect3D8-object krijgt een eigen kopie van zijn vtable met GetAdapterCount = 1 en CreateDevice met Windowed = TRUE,
 *     FullScreen_RefreshRateInHz = 0 en FullScreen_PresentationInterval = 0 (verplicht voor een venster in D3D8); het apparaat krijgt
 *     een kopie van zijn vtable met dezelfde aanpassing in Reset;
 *   - SetWindowPos naar 1280x480 wordt 640x480 (een scherm, een apparaat).
 *
 * Lukt CreateDevice in een venster niet, dan nogmaals met de parameters van de game (en dat staat in het logboek).
 */
#ifndef RG_KONAMI_LE3_VENSTER_H
#define RG_KONAMI_LE3_VENSTER_H

/* D3DPRESENT_PARAMETERS van D3D8 (d3d8types.h), alleen de velden die we zetten. */
typedef struct {
    UINT BackBufferWidth, BackBufferHeight;
    DWORD BackBufferFormat;
    UINT BackBufferCount;
    DWORD MultiSampleType, SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed, EnableAutoDepthStencil;
    DWORD AutoDepthStencilFormat, Flags;
    UINT FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
} le3_d3dpp;

typedef void *(WINAPI *le3_fn_create8)(UINT);
typedef UINT(WINAPI *le3_fn_adapters)(void *);
typedef HRESULT(WINAPI *le3_fn_createdevice)(void *, UINT, DWORD, HWND, DWORD, le3_d3dpp *, void **);
typedef HRESULT(WINAPI *le3_fn_reset)(void *, le3_d3dpp *);
typedef BOOL(WINAPI *le3_fn_setwindowpos)(HWND, HWND, int, int, int, int, UINT);

#define LE3_D3D8_METHODEN 16    /* IDirect3D8 */
#define LE3_DEV8_METHODEN 97    /* IDirect3DDevice8 */

static le3_fn_create8 le3_echt_create8;
static le3_fn_adapters le3_echt_adapters;
static le3_fn_createdevice le3_echt_createdevice;
static le3_fn_reset le3_echt_reset;
static le3_fn_setwindowpos le3_echt_setwindowpos;
static void *le3_vtbl_d3d[LE3_D3D8_METHODEN];
static void *le3_vtbl_dev[LE3_DEV8_METHODEN];

static void le3_naar_venster(le3_d3dpp *pp)
{
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    pp->FullScreen_PresentationInterval = 0;
}

static HRESULT WINAPI le3_reset(void *dev, le3_d3dpp *pp)
{
    le3_d3dpp eigen = *pp;
    le3_naar_venster(&eigen);
    HRESULT hr = le3_echt_reset(dev, &eigen);
    kb_log("venster: Reset(%ux%u, venster) = %x", eigen.BackBufferWidth, eigen.BackBufferHeight, (unsigned)hr);
    if (hr < 0) hr = le3_echt_reset(dev, pp);
    return hr;
}

static UINT WINAPI le3_adapters(void *d3d)
{
    const UINT n = le3_echt_adapters(d3d);
    kb_log("venster: GetAdapterCount = %u, de game krijgt 1 (een apparaat, een venster)", n);
    return n ? 1 : 0;
}

static HRESULT WINAPI le3_createdevice(void *d3d, UINT adapter, DWORD type, HWND focus, DWORD gedrag, le3_d3dpp *pp, void **dev)
{
    le3_d3dpp eigen = *pp;
    le3_naar_venster(&eigen);
    HRESULT hr = le3_echt_createdevice(d3d, adapter, type, focus, gedrag, &eigen, dev);
    kb_log("venster: CreateDevice(adapter %u, %ux%u fmt %u swap %u, venster) = %x", adapter, eigen.BackBufferWidth,
           eigen.BackBufferHeight, (unsigned)eigen.BackBufferFormat, (unsigned)eigen.SwapEffect, (unsigned)hr);
    if (hr < 0) {
        hr = le3_echt_createdevice(d3d, adapter, type, focus, gedrag, pp, dev);
        kb_log("venster: CreateDevice met de parameters van de game (volledig scherm) = %x", (unsigned)hr);
        return hr;
    }
    if (dev && *dev) {
        void ***obj = (void ***)*dev;
        if (!le3_echt_reset) {
            memcpy(le3_vtbl_dev, *obj, sizeof le3_vtbl_dev);
            le3_echt_reset = (le3_fn_reset)le3_vtbl_dev[14];
            le3_vtbl_dev[14] = (void *)le3_reset;
        }
        *obj = le3_vtbl_dev;
    }
    return hr;
}

static void *WINAPI le3_create8(UINT sdk)
{
    void *d3d = le3_echt_create8(sdk);
    kb_log("venster: Direct3DCreate8(%u) = %x", sdk, (unsigned)(uintptr_t)d3d);
    if (d3d) {
        void ***obj = (void ***)d3d;
        if (!le3_echt_adapters) {
            memcpy(le3_vtbl_d3d, *obj, sizeof le3_vtbl_d3d);
            le3_echt_adapters = (le3_fn_adapters)le3_vtbl_d3d[4];
            le3_echt_createdevice = (le3_fn_createdevice)le3_vtbl_d3d[15];
            le3_vtbl_d3d[4] = (void *)le3_adapters;
            le3_vtbl_d3d[15] = (void *)le3_createdevice;
        }
        *obj = le3_vtbl_d3d;
    }
    return d3d;
}

static BOOL WINAPI le3_setwindowpos(HWND w, HWND na, int x, int y, int cx, int cy, UINT vlaggen)
{
    if (cx == 1280 && cy == 480) {
        kb_log("venster: SetWindowPos 1280x480 -> 640x480");
        cx = 640;
    }
    return le3_echt_setwindowpos(w, na, x, y, cx, cy, vlaggen);
}

/* Zet een import van de exe om naar `nieuw`; geeft de oude waarde (of NULL). Alleen in het geheugen van dit proces. */
static void *le3_zet_import(const char *dll, const char *functie, void *nieuw)
{
    uint8_t *b = (uint8_t *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(b + *(const LONG *)(b + 0x3c));
    const IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress) return NULL;
    for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(b + dir->VirtualAddress); d->Name; d++) {
        if (lstrcmpiA((const char *)(b + d->Name), dll) != 0 || !d->OriginalFirstThunk) continue;
        const IMAGE_THUNK_DATA32 *naam = (const IMAGE_THUNK_DATA32 *)(b + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA32 *iat = (IMAGE_THUNK_DATA32 *)(b + d->FirstThunk);
        for (; naam->u1.AddressOfData; naam++, iat++) {
            if (naam->u1.Ordinal & IMAGE_ORDINAL_FLAG32) continue;
            const IMAGE_IMPORT_BY_NAME *ibn = (const IMAGE_IMPORT_BY_NAME *)(b + naam->u1.AddressOfData);
            if (lstrcmpA((const char *)ibn->Name, functie) != 0) continue;
            DWORD oud;
            if (!VirtualProtect(&iat->u1.Function, sizeof iat->u1.Function, PAGE_READWRITE, &oud)) return NULL;
            void *was = (void *)(uintptr_t)iat->u1.Function;
            iat->u1.Function = (DWORD)(uintptr_t)nieuw;
            VirtualProtect(&iat->u1.Function, sizeof iat->u1.Function, oud, &oud);
            return was;
        }
    }
    return NULL;
}

/* In DllMain (DLL_PROCESS_ATTACH): de imports van game.exe zijn dan al ingevuld (d3d8 en user32 staan voor iob in de importtabel en
 * Wine/Windows vullen alle imports in voor de eerste DllMain). Alleen kernel32; het logboek met kb_schrijf. */
static void le3_venster_start(void)
{
    char w[8];
    const DWORD n = GetEnvironmentVariableA("RG_KONAMI_VENSTER", w, sizeof w);
    if (!(n > 0 && n < sizeof w && w[0] == '1')) return;
    le3_echt_create8 = (le3_fn_create8)le3_zet_import("d3d8.dll", "Direct3DCreate8", (void *)le3_create8);
    le3_echt_setwindowpos = (le3_fn_setwindowpos)le3_zet_import("USER32.dll", "SetWindowPos", (void *)le3_setwindowpos);
    if (!le3_echt_create8) kb_schrijf("venster: Direct3DCreate8 niet in de importtabel van de exe gevonden; geen venstermodus");
    if (!le3_echt_setwindowpos) kb_schrijf("venster: SetWindowPos niet in de importtabel van de exe gevonden");
    if (le3_echt_create8 && le3_echt_setwindowpos) kb_schrijf("venster: RG_KONAMI_VENSTER=1, d3d8 en SetWindowPos omgeleid (venster 640x480)");
}

#endif
