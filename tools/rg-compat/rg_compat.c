/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_compat.c -- een kleine DLL die in het spelproces geladen wordt (drop-in met Preload = true, dus TP_PRELOAD_DLL van de
 * OpenParrot-lader) en een paar plekken waar Wine strenger of anders is dan Windows rechtzet. Elke fix grijpt alleen in bij
 * precies de situatie die hij oplost; anders gaat de aanroep ongewijzigd door. Geen game-naam, geen game-adres.
 *
 * 1. GDI32!GetCharABCWidthsA (importtabel van het hoofdbeeld)
 *    Action Deka (deka.exe) meet de laatste twee bytes van een string als een dubbel-byte-teken met een buffer van precies EEN
 *    ABC (12 bytes). Met een Japans lettertype (codepagina 932) maakt Wine van twee enkel-byte-tekens twee tekens en schrijft
 *    het twee ABC's (24 bytes) over het terugkeeradres heen (crash op 0x0049C4A9; WINEDEBUG=+font: "font_GetCharABCWidths ...,
 *    0, 2, 0xb9fb1c", 2026-10-15). De fix laat Wine in een ruime buffer schrijven en geeft precies (last - first + 1) ABC's terug.
 *
 * 2. IDirectInput(A)::Initialize met een lege HINSTANCE (via ole32!CoCreateInstance in de importtabel)
 *    After Dark (Virtools GamePlayer.exe) maakt DirectInput met CoCreateInstance(CLSID_DirectInput) en roept Initialize(NULL,
 *    0x700 / 0x500 / 0x300) aan; Wine weigert een lege HINSTANCE met DIERR_INVALIDPARAM en de speler stopt met "Initalization
 *    Error - Cannot Initalize Input Manager" (WINEDEBUG=+dinput, 2026-10-15). De fix vult dan GetModuleHandle(NULL) in.
 *
 * BOUWEN: scripts/build-compat.sh (de mingw-kruiscompiler in ~/.cache/ravengun-mingw, i686 en x86_64).
 */
#include <windows.h>
#include <string.h>

/* ---------------------------------------------------------------- IAT */

static void *patch_import(HMODULE mod, const char *dll, const char *func, void *repl) {
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; ++d) {
        if (lstrcmpiA((const char *)(base + d->Name), dll) != 0) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, func) != 0) continue;
            DWORD old;
            if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old)) return NULL;
            void *orig = (void *)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)repl;
            VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old, &old);
            return orig;
        }
    }
    return NULL;
}

/* ------------------------------------------------- 1. GetCharABCWidthsA */

typedef BOOL(WINAPI *abc_fn)(HDC, UINT, UINT, LPABC);
static abc_fn real_abc;

static BOOL WINAPI rg_GetCharABCWidthsA(HDC dc, UINT first, UINT last, LPABC abc) {
    if (last < first || last - first >= 256 || !abc) return real_abc(dc, first, last, abc);
    const UINT n = last - first + 1;
    ABC tmp[2 * 256 + 8]; /* Wine kan per code twee tekens maken */
    const BOOL ok = real_abc(dc, first, last, tmp);
    if (ok) memcpy(abc, tmp, n * sizeof(ABC));
    return ok;
}

/* ------------------------------------- 2. IDirectInput::Initialize(NULL) */

static const GUID kClsidDirectInput = {0x25e609e0, 0xb259, 0x11cf, {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
typedef HRESULT(WINAPI *cocreate_fn)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
typedef HRESULT(WINAPI *dinit_fn)(void *self, HINSTANCE, DWORD);
static cocreate_fn real_cocreate;
static void **patched_vtbl;
static dinit_fn real_dinit;

static HRESULT WINAPI rg_DirectInputInitialize(void *self, HINSTANCE hinst, DWORD version) {
    if (!hinst) hinst = GetModuleHandleA(NULL);
    return real_dinit(self, hinst, version);
}

static HRESULT WINAPI rg_CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *out) {
    const HRESULT hr = real_cocreate(clsid, outer, ctx, iid, out);
    if (SUCCEEDED(hr) && out && *out && memcmp(clsid, &kClsidDirectInput, sizeof(GUID)) == 0) {
        void **vtbl = *(void ***)*out;
        /* IDirectInputA/W: QueryInterface, AddRef, Release, CreateDevice, EnumDevices, GetDeviceStatus, RunControlPanel,
           Initialize (index 7). */
        if (vtbl && vtbl != patched_vtbl && vtbl[7] != (void *)rg_DirectInputInitialize) {
            DWORD old;
            if (VirtualProtect(&vtbl[7], sizeof(void *), PAGE_READWRITE, &old)) {
                real_dinit = (dinit_fn)vtbl[7];
                vtbl[7] = (void *)rg_DirectInputInitialize;
                VirtualProtect(&vtbl[7], sizeof(void *), old, &old);
                patched_vtbl = vtbl;
                OutputDebugStringA("rg-compat: IDirectInput::Initialize omgeleid (lege HINSTANCE)\n");
            }
        }
    }
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE exe = GetModuleHandleA(NULL);
        real_abc = (abc_fn)patch_import(exe, "gdi32.dll", "GetCharABCWidthsA", (void *)rg_GetCharABCWidthsA);
        if (real_abc) OutputDebugStringA("rg-compat: GDI32!GetCharABCWidthsA omgeleid\n");
        real_cocreate = (cocreate_fn)patch_import(exe, "ole32.dll", "CoCreateInstance", (void *)rg_CoCreateInstance);
        if (real_cocreate) OutputDebugStringA("rg-compat: ole32!CoCreateInstance omgeleid\n");
    }
    return TRUE;
}
