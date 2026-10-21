/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DINPUT8.dll -- the drop-in that carries rg_c9_table.c into Coopers's 9's game process.
 *
 * game.exe imports DINPUT8.dll (15 import descriptors, and DirectInput8Create is the only function it takes from it), so
 * this DLL is loaded by the normal loader from the exe's own directory -- no preload channel is needed. Coopers9's one
 * TP_PRELOAD_DLL already carries rg-compat.dll, and the launcher refuses two.
 *
 * DllMain starts the call-table repair (rg_c9_table_start in rg_c9table.c: translate the XP addresses of the table at
 * 0x512000 into live ones, stub the rest and log them), and DirectInput8Create forwards to the real dinput8 of the
 * prefix, loaded on first use. Nothing else is exported or intercepted.
 *
 * BUILD: tools/rg-c9-table/build.sh (mingw-w64 i686 in ~/.cache/ravengun-mingw).
 */
#include <windows.h>
#include <string.h>

void rg_c9_table_start(void);

/* DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter) */
typedef HRESULT(WINAPI *direct_input8_create_fn)(HINSTANCE, DWORD, const void *, void **, void *);

/* The system's own dinput8: loaded by full path once, so it cannot resolve to this DLL again. */
static direct_input8_create_fn resolve_create(void) {
    static direct_input8_create_fn fn;
    static LONG busy;
    char path[MAX_PATH];
    HMODULE mod;
    if (fn) return fn;
    if (InterlockedExchange(&busy, 1)) return NULL;          /* another thread is loading it */
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    if (n > 0 && n + sizeof "\\dinput8.dll" <= MAX_PATH) {
        memcpy(path + n, "\\dinput8.dll", sizeof "\\dinput8.dll");
        mod = LoadLibraryA(path);
        if (mod) fn = (direct_input8_create_fn)(void *)GetProcAddress(mod, "DirectInput8Create");
    }
    InterlockedExchange(&busy, 0);
    return fn;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, const void *riid, void **out, void *unknown) {
    direct_input8_create_fn fn = resolve_create();
    if (!fn) return (HRESULT)0x80004005u;                    /* E_FAIL: no real dinput8 */
    return fn(inst, version, riid, out, unknown);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) rg_c9_table_start();
    return TRUE;
}
