/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-c9-table.dll -- how the call-table repair gets into Coopers's 9's game process.
 *
 * A Windows game under the OpenParrot loader gets exactly one TP_PRELOAD_DLL, and this game's slot was already taken by
 * rg-compat.dll. The game also imports DINPUT8.dll, but the core resolves the imports of a title lazily: the module list
 * of a run (rg-compat's RG_MAPLOG) shows SETUPAPI, USER32, d3d9, WINMM, GDI32, ADVAPI32 and ole32 but no DINPUT8.dll,
 * DSOUND.dll, OLEAUT32.dll or MSVCR71.dll, so a DINPUT8.dll drop-in is never loaded and cannot repair the table.
 * Hence this DLL takes the one preload slot and loads rg-compat.dll itself (same directory), so the existing
 * instruments keep working. DllMain repairs the table at once (rg_c9_table_start in rg_c9table.c) and keeps polling.
 *
 * BUILD: tools/rg-c9-table/build.sh (mingw-w64 i686 in ~/.cache/ravengun-mingw).
 */
#include <windows.h>
#include <string.h>

void rg_c9_table_start(void);

static void load_compat(void) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof path);
    HMODULE mod;
    if (n == 0 || n >= sizeof path) return;
    char *slash = strrchr(path, '\\');
    if (!slash) return;
    if ((size_t)(slash + 1 - path) + sizeof "rg-compat.dll" > sizeof path) return;
    memcpy(slash + 1, "rg-compat.dll", sizeof "rg-compat.dll");
    mod = LoadLibraryA(path);
    (void)mod;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    load_compat();
    rg_c9_table_start();
    return TRUE;
}
