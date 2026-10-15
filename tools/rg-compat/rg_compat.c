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
 * 3. Meetinstrument, alleen met RG_COMPAT_SAMPLE=<ms> in de omgeving: een draad die elke <ms> milliseconden de andere draden
 *    van het proces kort stilzet (SuspendThread + GetThreadContext) en hun instructiewijzer telt, met de eerste twee
 *    terugkeeradressen op de stapel die in het hoofdbeeld vallen. Elke 5 s schrijft hij per draad de meest geziene plekken als
 *    module+RVA naar RG_COMPAT_SAMPLE_LOG (Windows-pad; standaard %TEMP%\rg-compat-<pid>.txt, in de prefix). Zo is een
 *    bezige lus of een wachtende draad te vinden zonder ptrace of debugger (winedbg legt de games om; ptrace_scope = 1).
 *
 * 4. WM_PAINT dat nooit geldig wordt, alleen met RG_COMPAT_PAINT=1 (user32!UpdateWindow en DispatchMessageA/W in de importtabel)
 *    Wine's UpdateWindow (win32u update_now) blijft WM_PAINT sturen zolang het venster ongeldig is; Windows stuurt het een keer.
 *    Een game die WM_PAINT zelf afhandelt zonder BeginPaint/ValidateRect draait dan eindeloos in UpdateWindow (Far Cry Paradise
 *    Lost en Friction onder gamescope, gemeten met de meetdraad en perf, 2026-10-15), en in een PeekMessage-lus krijgt hij steeds
 *    weer WM_PAINT zodat de teken-stap nooit aan de beurt komt. De fix: UpdateWindow stuurt WM_PAINT een keer en maakt het venster
 *    dan geldig; na elk verdeeld WM_PAINT wordt het venster geldig gemaakt.
 *
 * BOUWEN: scripts/build-compat.sh (de mingw-kruiscompiler in ~/.cache/ravengun-mingw, i686 en x86_64).
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ------------------------------------------- 4. RG_COMPAT_PAINT (WM_PAINT) */

typedef BOOL(WINAPI *updwin_fn)(HWND);
typedef LRESULT(WINAPI *dispatch_fn)(const MSG *);
static updwin_fn real_updatewindow;
static dispatch_fn real_dispatch_a, real_dispatch_w;
static volatile LONG paint_updates, paint_dispatched, paint_validated;

static BOOL WINAPI rg_UpdateWindow(HWND hwnd) {
    InterlockedIncrement(&paint_updates);
    if (!IsWindow(hwnd)) return real_updatewindow(hwnd);
    if (GetUpdateRect(hwnd, NULL, FALSE)) {
        SendMessageW(hwnd, WM_PAINT, 0, 0);
        if (GetUpdateRect(hwnd, NULL, FALSE)) ValidateRect(hwnd, NULL), InterlockedIncrement(&paint_validated);
    }
    return TRUE;
}

static void after_paint(const MSG *m) {
    if (!m || m->message != WM_PAINT || !m->hwnd) return;
    InterlockedIncrement(&paint_dispatched);
    if (GetUpdateRect(m->hwnd, NULL, FALSE)) ValidateRect(m->hwnd, NULL), InterlockedIncrement(&paint_validated);
}

static LRESULT WINAPI rg_DispatchMessageA(const MSG *m) {
    const LRESULT r = real_dispatch_a(m);
    after_paint(m);
    return r;
}

static LRESULT WINAPI rg_DispatchMessageW(const MSG *m) {
    const LRESULT r = real_dispatch_w(m);
    after_paint(m);
    return r;
}

static void paint_start(HMODULE exe) {
    char v[8];
    if (!GetEnvironmentVariableA("RG_COMPAT_PAINT", v, sizeof v) || v[0] != '1') return;
    real_updatewindow = (updwin_fn)patch_import(exe, "user32.dll", "UpdateWindow", (void *)rg_UpdateWindow);
    real_dispatch_a = (dispatch_fn)patch_import(exe, "user32.dll", "DispatchMessageA", (void *)rg_DispatchMessageA);
    real_dispatch_w = (dispatch_fn)patch_import(exe, "user32.dll", "DispatchMessageW", (void *)rg_DispatchMessageW);
    char msg[160];
    snprintf(msg, sizeof msg, "rg-compat: WM_PAINT-fix (UpdateWindow %s, DispatchMessageA %s, DispatchMessageW %s)\n",
             real_updatewindow ? "ja" : "nee", real_dispatch_a ? "ja" : "nee", real_dispatch_w ? "ja" : "nee");
    OutputDebugStringA(msg);
}

/* --------------------------------------------- 3. RG_COMPAT_SAMPLE (meten) */

#define SAMPLE_SLOTS 8192
typedef struct {
    DWORD tid;
    ULONG_PTR pc, c1, c2;
    unsigned count;
} sample_t;
static sample_t samples[SAMPLE_SLOTS];
static ULONG_PTR exe_lo, exe_hi;

static void sample_add(DWORD tid, ULONG_PTR pc, ULONG_PTR c1, ULONG_PTR c2) {
    size_t h = ((size_t)tid * 2654435761u ^ pc ^ (c1 << 7) ^ (c2 << 13)) % SAMPLE_SLOTS;
    for (size_t i = 0; i < SAMPLE_SLOTS; ++i, h = (h + 1) % SAMPLE_SLOTS) {
        sample_t *s = &samples[h];
        if (s->count && (s->tid != tid || s->pc != pc || s->c1 != c1 || s->c2 != c2)) continue;
        s->tid = tid, s->pc = pc, s->c1 = c1, s->c2 = c2, s->count++;
        return;
    }
}

static void where(ULONG_PTR a, char *out, size_t n) {
    HMODULE m = NULL;
    char path[MAX_PATH];
    if (!a) {
        snprintf(out, n, "-");
        return;
    }
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m &&
        GetModuleFileNameA(m, path, sizeof path)) {
        const char *b = strrchr(path, '\\');
        snprintf(out, n, "%s+0x%lx", b ? b + 1 : path, (unsigned long)(a - (ULONG_PTR)m));
    } else {
        snprintf(out, n, "%p", (void *)a);
    }
}

static int by_count(const void *a, const void *b) {
    const sample_t *x = a, *y = b;
    if (x->tid != y->tid) return x->tid < y->tid ? -1 : 1;
    return (int)y->count - (int)x->count;
}

static void sample_dump(const char *path, unsigned rounds) {
    static sample_t copy[SAMPLE_SLOTS];
    size_t n = 0;
    for (size_t i = 0; i < SAMPLE_SLOTS; ++i)
        if (samples[i].count) copy[n++] = samples[i];
    qsort(copy, n, sizeof *copy, by_count);
    FILE *f = fopen(path, "w");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "rg-compat sample %02d:%02d:%02d pid %lu, %u rondes; per draad de 12 meest geziene plekken (aantal pc | aanroeper 1 | aanroeper 2)\n",
            t.wHour, t.wMinute, t.wSecond, (unsigned long)GetCurrentProcessId(), rounds);
    fprintf(f, "WM_PAINT-fix: UpdateWindow %ld, WM_PAINT verdeeld %ld, geldig gemaakt %ld\n", (long)paint_updates, (long)paint_dispatched,
            (long)paint_validated);
    DWORD cur = 0;
    int shown = 0;
    for (size_t i = 0; i < n; ++i) {
        if (copy[i].tid != cur) cur = copy[i].tid, shown = 0, fprintf(f, "draad %lu\n", (unsigned long)cur);
        if (shown++ >= 12) continue;
        char a[300], b[300], c[300];
        where(copy[i].pc, a, sizeof a), where(copy[i].c1, b, sizeof b), where(copy[i].c2, c, sizeof c);
        fprintf(f, "  %6u %s | %s | %s\n", copy[i].count, a, b, c);
    }
    fclose(f);
}

static DWORD WINAPI sample_thread(LPVOID arg) {
    const DWORD ms = (DWORD)(ULONG_PTR)arg, self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("RG_COMPAT_SAMPLE_LOG", path, sizeof path)) {
        char tmp[MAX_PATH - 32];
        if (!GetTempPathA(sizeof tmp, tmp)) tmp[0] = 0;
        snprintf(path, sizeof path, "%srg-compat-%lu.txt", tmp, (unsigned long)pid);
    }
    unsigned rounds = 0;
    DWORD last = GetTickCount();
    for (;;) {
        Sleep(ms);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) continue;
        THREADENTRY32 te = {.dwSize = sizeof te};
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!th) continue;
            if (SuspendThread(th) != (DWORD)-1) {
                CONTEXT ctx = {.ContextFlags = CONTEXT_CONTROL};
                if (GetThreadContext(th, &ctx)) {
#ifdef _WIN64
                    const ULONG_PTR pc = ctx.Rip, sp = ctx.Rsp;
#else
                    const ULONG_PTR pc = ctx.Eip, sp = ctx.Esp;
#endif
                    ULONG_PTR c[2] = {0, 0};
                    MEMORY_BASIC_INFORMATION mi;
                    if (VirtualQuery((LPCVOID)sp, &mi, sizeof mi) && mi.State == MEM_COMMIT) {
                        const ULONG_PTR end = (ULONG_PTR)mi.BaseAddress + mi.RegionSize;
                        int k = 0;
                        for (ULONG_PTR p = sp; p + sizeof(ULONG_PTR) <= end && p < sp + 4096 && k < 2; p += sizeof(ULONG_PTR)) {
                            const ULONG_PTR v = *(const ULONG_PTR *)p;
                            if (v >= exe_lo && v < exe_hi && v != pc) c[k++] = v;
                        }
                    }
                    sample_add(te.th32ThreadID, pc, c[0], c[1]);
                }
                ResumeThread(th);
            }
            CloseHandle(th);
        }
        CloseHandle(snap);
        ++rounds;
        if (GetTickCount() - last >= 5000) last = GetTickCount(), sample_dump(path, rounds);
    }
    return 0;
}

static void sample_start(void) {
    char v[32];
    if (!GetEnvironmentVariableA("RG_COMPAT_SAMPLE", v, sizeof v)) return;
    const long ms = strtol(v, NULL, 10);
    if (ms <= 0) return;
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    exe_lo = (ULONG_PTR)base, exe_hi = exe_lo + nt->OptionalHeader.SizeOfImage;
    HANDLE h = CreateThread(NULL, 0, sample_thread, (LPVOID)(ULONG_PTR)ms, 0, NULL);
    if (h) CloseHandle(h), OutputDebugStringA("rg-compat: meetdraad gestart (RG_COMPAT_SAMPLE)\n");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        sample_start();
        HMODULE exe = GetModuleHandleA(NULL);
        real_abc = (abc_fn)patch_import(exe, "gdi32.dll", "GetCharABCWidthsA", (void *)rg_GetCharABCWidthsA);
        if (real_abc) OutputDebugStringA("rg-compat: GDI32!GetCharABCWidthsA omgeleid\n");
        real_cocreate = (cocreate_fn)patch_import(exe, "ole32.dll", "CoCreateInstance", (void *)rg_CoCreateInstance);
        if (real_cocreate) OutputDebugStringA("rg-compat: ole32!CoCreateInstance omgeleid\n");
        paint_start(exe);
    }
    return TRUE;
}
