/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_gdifix.c -- een kleine DLL die in het spelproces geladen wordt (drop-in met Preload = true, dus TP_PRELOAD_DLL van de
 * OpenParrot-lader) en GDI32!GetCharABCWidthsA in de importtabel van de game omleidt.
 *
 * WAAROM
 *   Action Deka (deka.exe, Konami; later overgeslagen volgens de skip-regel, de fix bleef gemeten) meet de laatste twee bytes van een string als een dubbel-byte-teken:
 *   GetCharABCWidthsA(hdc, c, c, &abc) met c = (str[n-2] << 8) | str[n-1] en een buffer van precies EEN ABC (12 bytes) op de
 *   stapel. Met een Japans lettertype (codepagina 932) zet Wine c om naar tekens; zijn het twee enkel-byte-tekens, dan worden
 *   het er twee en schrijft Wine twee ABC's (24 bytes). Dat overschrijft het terugkeeradres en de argumenten, en de game valt
 *   op 0x0049C4A9 (schrijven naar adres 3). Gemeten op 2026-10-15 met WINEDEBUG=+font: "font_GetCharABCWidths ..., 0, 2, 0xb9fb1c",
 *   en 0xb9fb1c is precies die buffer.
 *
 * WAT DE DLL DOET
 *   Bij het laden: in de importtabel van het hoofdbeeld (GetModuleHandle(NULL)) de ingang GDI32!GetCharABCWidthsA vervangen
 *   door rg_GetCharABCWidthsA. Die laat Wine in een eigen, ruime buffer schrijven en geeft de game precies
 *   (last - first + 1) ABC's terug, zoals Windows. Verder niets: geen game-naam, geen game-adres.
 *
 * BOUWEN: scripts/build-gdifix.sh (de mingw-kruiscompiler in ~/.cache/ravengun-mingw, i686 en x86_64).
 */
#include <windows.h>
#include <string.h>

typedef BOOL(WINAPI* abc_fn)(HDC, UINT, UINT, LPABC);
static abc_fn real_abc;

static BOOL WINAPI rg_GetCharABCWidthsA(HDC dc, UINT first, UINT last, LPABC abc) {
    if (last < first || last - first >= 256 || !abc) return real_abc(dc, first, last, abc);
    const UINT n = last - first + 1;
    ABC tmp[2 * 256 + 8];   /* Wine kan per code twee tekens maken */
    const BOOL ok = real_abc(dc, first, last, tmp);
    if (ok) memcpy(abc, tmp, n * sizeof(ABC));
    return ok;
}

static void patch_iat(HMODULE mod) {
    BYTE* base = (BYTE*)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; ++d) {
        if (lstrcmpiA((const char*)(base + d->Name), "gdi32.dll") != 0) continue;
        IMAGE_THUNK_DATA* names = (IMAGE_THUNK_DATA*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA* iat = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, "GetCharABCWidthsA") != 0) continue;
            DWORD old;
            if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old)) return;
            real_abc = (abc_fn)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)rg_GetCharABCWidthsA;
            VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old, &old);
            OutputDebugStringA("rg-gdifix: GDI32!GetCharABCWidthsA omgeleid\n");
            return;
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) patch_iat(GetModuleHandleA(NULL));
    return TRUE;
}
