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
 * 5. Meetmeter voor een apparaat, alleen met RG_DONGLE2_LOG=<pad> in de omgeving: leent \\.\ITOKEN2 (CreateFileA/W,
 *    DeviceIoControl, ReadFile, WriteFile, CloseHandle) en schrijft op wat het spel vraagt, met nullen als antwoord.
 *    Voor After Dark, wiens dongle (ATL CCoDongle2) dat apparaat zelf opent; zonder de variabele doet deze meter niets.
 *
 * 6. RG_COMPAT_PATCH=<rva>:<hex>[;...] schrijft die bytes synchroon in het hoofdbeeld en schrijft de oude
 *    bytes op; zonder de variabele gebeurt er niets.
 *
 * BOUWEN: tools/dev/build-compat.sh (de mingw-kruiscompiler in ~/.cache/ravengun-mingw, i686 en x86_64).
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <setupapi.h>
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


/* ------------------------------------------- 5. RG_DONGLE2_LOG (apparaatmeter) */

/* After Dark (Virtools GamePlayer.exe) maakt zijn dongle zelf aan (ATL CCoDongle2, dus geen CoCreateInstance) en die
 * praat met het apparaat \\.\ITOKEN2 via CreateFileA plus DeviceIoControl. Deze meter leent dat apparaat en schrijft op
 * welke aanroepen en bytes het spel doet, zodat de antwoorden uit de code van het spel zelf te halen zijn. Alleen met
 * RG_DONGLE2_LOG in de omgeving; zonder dat blijft alles bij de originelen. */

#define RG_D2_HANDLE ((HANDLE)(ULONG_PTR)0xD0D61002u)

typedef HANDLE(WINAPI *d2_create_a_fn)(LPCSTR, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI *d2_create_w_fn)(LPCWSTR, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL(WINAPI *d2_ioctl_fn)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *d2_read_fn)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *d2_write_fn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *d2_close_fn)(HANDLE);

static d2_create_a_fn real_d2_create_a;
static d2_create_w_fn real_d2_create_w;
static d2_ioctl_fn real_d2_ioctl;
static d2_read_fn real_d2_read;
static d2_write_fn real_d2_write;
static d2_close_fn real_d2_close;
static FILE *d2_log;

static void d2_line(const char *fmt, ...) {
    if (!d2_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(d2_log, fmt, ap);
    va_end(ap);
    fputc('\n', d2_log);
    fflush(d2_log);
}


static int d2_has_hid(const char *s) {
    if (!s) return 0;
    for (; *s; ++s) {
        const char *p = "vid_0471";
        int i = 0;
        while (p[i] && s[i] && (s[i] == p[i] || s[i] == p[i] - 32)) ++i;
        if (!p[i]) return 1;
    }
    return 0;
}

static int d2_has_token2(const char *s) {
    if (!s) return 0;
    for (; *s; ++s) {
        const char *p = "ITOKEN2";
        int i = 0;
        while (p[i] && s[i] && (s[i] == p[i] || s[i] == p[i] + 32)) ++i;   /* kleine letters toestaan */
        if (!p[i]) return 1;
    }
    return 0;
}

static int d2_has_token2_w(const WCHAR *s) {
    char narrow[260];
    int i = 0;
    if (!s) return 0;
    for (; s[i] && i < 255; ++i) narrow[i] = (char)(s[i] < 128 ? s[i] : '?');
    narrow[i] = 0;
    return d2_has_token2(narrow);
}

/* Antwoord van het geleende apparaat: nullen, of RG_DONGLE2_REPLY=<hex> als die gezet is (dan herhaalt de
 * reeks zich), zodat een antwoord zonder herbouwen te proberen is. */
static unsigned char d2_reply[64];
static DWORD d2_reply_len;

static void d2_reply_init(void) {
    char txt[160];
    DWORD n = GetEnvironmentVariableA("RG_DONGLE2_REPLY", txt, sizeof txt);
    if (n == 0 || n >= sizeof txt) return;
    DWORD i = 0;
    for (; i + 1 < n && d2_reply_len < sizeof d2_reply; i += 2) {
        char pair[3] = {txt[i], txt[i + 1], 0};
        char *end = NULL;
        long v = strtol(pair, &end, 16);
        if (end == pair) break;
        d2_reply[d2_reply_len++] = (unsigned char)v;
    }
    d2_line("antwoordreeks uit RG_DONGLE2_REPLY: %lu bytes", (unsigned long)d2_reply_len);
}

static void d2_fill(unsigned char *buf, DWORD len) {
    if (!buf || !len) return;
    if (d2_reply_len == 0) {
        memset(buf, 0, len);
        return;
    }
    for (DWORD i = 0; i < len; ++i) buf[i] = d2_reply[i % d2_reply_len];
}

static void d2_hex(const char *label, const void *buf, DWORD len) {
    const unsigned char *p = (const unsigned char *)buf;
    char line[3 * 24 + 8];
    DWORD show = len < 24 ? len : 24, i;
    int k = 0;
    for (i = 0; i < show; ++i) k += snprintf(line + k, sizeof line - (size_t)k, "%02x ", p[i]);
    line[k] = 0;
    d2_line("   %s%s", label, line);
}


typedef DWORD(WINAPI *d2_getfiletype_fn)(HANDLE);
typedef BOOL(WINAPI *d2_getoverlapped_fn)(HANDLE, LPOVERLAPPED, LPDWORD, BOOL);
static d2_getfiletype_fn real_d2_getfiletype;
static d2_getoverlapped_fn real_d2_getoverlapped;

static DWORD WINAPI rg_d2_GetFileType(HANDLE h) {
    if (h == RG_D2_HANDLE) {
        d2_line("GetFileType(nephandvat) -> FILE_TYPE_UNKNOWN");
        SetLastError(ERROR_SUCCESS);
        return FILE_TYPE_UNKNOWN;
    }
    return real_d2_getfiletype(h);
}

static BOOL WINAPI rg_d2_GetOverlappedResult(HANDLE h, LPOVERLAPPED ov, LPDWORD got, BOOL wait) {
    if (h == RG_D2_HANDLE) {
        d2_line("GetOverlappedResult(nephandvat, wait=%d) -> klaar", wait);
        if (got) *got = 0;
        return TRUE;
    }
    return real_d2_getoverlapped(h, ov, got, wait);
}

static HANDLE WINAPI rg_d2_CreateFileA(LPCSTR name, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD share, DWORD disp,
                                       HANDLE tmpl) {
    if (d2_has_token2(name) || d2_has_hid(name)) {
        d2_line("CreateFileA(%s, access=0x%08lx, share=%lu) -> nephandvat", name, (unsigned long)access,
                (unsigned long)share);
        SetLastError(ERROR_SUCCESS);
        return RG_D2_HANDLE;
    }
    return real_d2_create_a(name, access, sa, share, disp, tmpl);
}

static HANDLE WINAPI rg_d2_CreateFileW(LPCWSTR name, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD share, DWORD disp,
                                       HANDLE tmpl) {
    if (d2_has_token2_w(name)) {
        d2_line("CreateFileW(wijde naam \"ITOKEN2\", access=0x%08lx) -> nephandvat", (unsigned long)access);
        SetLastError(ERROR_SUCCESS);
        return RG_D2_HANDLE;
    }
    return real_d2_create_w(name, access, sa, share, disp, tmpl);
}

static BOOL WINAPI rg_d2_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_len, LPVOID out, DWORD out_len,
                                         LPDWORD ret, LPOVERLAPPED ov) {
    if (h != RG_D2_HANDLE) return real_d2_ioctl(h, code, in, in_len, out, out_len, ret, ov);
    d2_line("DeviceIoControl code=0x%08lx in=%lu out=%lu", (unsigned long)code, (unsigned long)in_len,
            (unsigned long)out_len);
    if (in && in_len) d2_hex("invoer: ", in, in_len);
    if (out && out_len) d2_fill((unsigned char *)out, out_len);
    if (ret) *ret = out_len;
    return TRUE;
}

static BOOL WINAPI rg_d2_ReadFile(HANDLE h, LPVOID buf, DWORD len, LPDWORD got, LPOVERLAPPED ov) {
    if (h != RG_D2_HANDLE) return real_d2_read(h, buf, len, got, ov);
    d2_line("ReadFile %lu bytes -> antwoord", (unsigned long)len);
    if (buf && len) d2_fill((unsigned char *)buf, len);
    if (got) *got = len;
    return TRUE;
}

static BOOL WINAPI rg_d2_WriteFile(HANDLE h, LPCVOID buf, DWORD len, LPDWORD wrote, LPOVERLAPPED ov) {
    if (h != RG_D2_HANDLE) return real_d2_write(h, buf, len, wrote, ov);
    d2_line("WriteFile %lu bytes", (unsigned long)len);
    if (buf && len) d2_hex("bytes: ", buf, len);
    if (wrote) *wrote = len;
    return TRUE;
}

static BOOL WINAPI rg_d2_CloseHandle(HANDLE h) {
    if (h == RG_D2_HANDLE) {
        d2_line("CloseHandle(nephandvat)");
        return TRUE;
    }
    return real_d2_close(h);
}

/* SetupAPI: één synthetisch apparaat, zodat de dongle van After Dark gevonden wordt, en een regel per vraag.
 * Het spel importeert alleen GetClassDevsA, EnumDeviceInfo, GetDeviceRegistryPropertyA en DestroyDeviceInfoList:
 * het zoekt een apparaat-INSTANTIE en leest daar registry-eigenschappen van (daar zit de VID/PID- en recordkeuze).
 * Deze haak antwoordt met één apparaat en schrijft op welke eigenschap het spel vraagt, zodat de waarden uit de
 * vragen van het spel zelf komen en niet gegokt worden. */
#define RG_D2_DEVSET ((HDEVINFO)(ULONG_PTR)0xD0D61003u)

typedef HDEVINFO(WINAPI *d2_getclassdevs_fn)(const GUID *, PCSTR, HWND, DWORD);
typedef BOOL(WINAPI *d2_enumdevinfo_fn)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
typedef BOOL(WINAPI *d2_getprop_fn)(HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);
typedef BOOL(WINAPI *d2_destroyset_fn)(HDEVINFO);
static d2_getclassdevs_fn real_d2_getclassdevs;
static d2_enumdevinfo_fn real_d2_enumdevinfo;
static d2_getprop_fn real_d2_getprop;
static d2_destroyset_fn real_d2_destroyset;

static void d2_guid(const char *label, const GUID *g) {
    if (!g) {
        d2_line("%s(NULL)", label);
        return;
    }
    d2_line("%s{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", label, (unsigned long)g->Data1, g->Data2,
            g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6],
            g->Data4[7]);
}

/* De eigenschappen die een HID-dongle normaal heeft. De waarden zijn synthetisch maar passen bij de vorm die
 * het spel vraagt (VID/PID van het bord dat in zijn eigen code genoemd wordt). */
static const char kD2_hwid[] = "HID\\VID_0471&PID_485E&REV_0100\0HID_DEVICE\0\0";
static const char kD2_desc[] = "USB HID Token";
static const char kD2_class[] = "HIDClass";

static HDEVINFO WINAPI rg_d2_SetupDiGetClassDevsA(const GUID *cls, PCSTR enumerator, HWND parent, DWORD flags) {
    HDEVINFO echt = real_d2_getclassdevs ? real_d2_getclassdevs(cls, enumerator, parent, flags) : NULL;
    d2_line("SetupDiGetClassDevsA(enumerator=%s, flags=0x%08lx) echt=%p -> synthetische set", 
            enumerator ? enumerator : "(geen)", (unsigned long)flags, (void *)echt);
    d2_guid("   klasse: ", cls);
    if (echt) real_d2_destroyset(echt);   /* de echte set wordt niet gebruikt */
    SetLastError(ERROR_SUCCESS);
    return RG_D2_DEVSET;
}

static BOOL WINAPI rg_d2_SetupDiEnumDeviceInfo(HDEVINFO set, DWORD index, PSP_DEVINFO_DATA data) {
    if (set != RG_D2_DEVSET) return real_d2_enumdevinfo(set, index, data);
    if (index != 0) {
        d2_line("SetupDiEnumDeviceInfo(index=%lu) -> nee (einde)", (unsigned long)index);
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (data) {
        data->cbSize = sizeof(SP_DEVINFO_DATA);
        memset(&data->ClassGuid, 0, sizeof data->ClassGuid);
        data->DevInst = 1;
        data->Reserved = 0;
    }
    d2_line("SetupDiEnumDeviceInfo(index=0) -> een synthetisch apparaat");
    return TRUE;
}

static BOOL WINAPI rg_d2_SetupDiGetDeviceRegistryPropertyA(HDEVINFO set, PSP_DEVINFO_DATA data, DWORD prop,
                                                           PDWORD type, PBYTE buf, DWORD size, PDWORD needed) {
    if (set != RG_D2_DEVSET) return real_d2_getprop(set, data, prop, type, buf, size, needed);
    const void *value = NULL;
    DWORD value_len = 0, value_type = REG_SZ;
    switch (prop) {
        case SPDRP_HARDWAREID: value = kD2_hwid; value_len = (DWORD)sizeof kD2_hwid; value_type = REG_MULTI_SZ; break;
        case SPDRP_DEVICEDESC:
        case SPDRP_FRIENDLYNAME: value = kD2_desc; value_len = (DWORD)sizeof kD2_desc; break;
        case SPDRP_CLASS: value = kD2_class; value_len = (DWORD)sizeof kD2_class; break;
        default: break;
    }
    d2_line("SetupDiGetDeviceRegistryPropertyA(prop=0x%08lx \"%s\", maat=%lu)", (unsigned long)prop,
            prop == SPDRP_HARDWAREID ? "HARDWAREID"
            : prop == SPDRP_DEVICEDESC ? "DEVICEDESC"
            : prop == SPDRP_FRIENDLYNAME ? "FRIENDLYNAME"
            : prop == SPDRP_CLASS ? "CLASS"
            : "?",
            (unsigned long)size);
    if (!value) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;   /* onbekende eigenschap: het spel leert niets, maar gaat wel verder */
    }
    if (needed) *needed = value_len;
    if (type) *type = value_type;
    if (!buf || size < value_len) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(buf, value, value_len);
    d2_line("   antwoord: %s", (const char *)value);
    return TRUE;
}

static BOOL WINAPI rg_d2_SetupDiDestroyDeviceInfoList(HDEVINFO set) {
    if (set == RG_D2_DEVSET) {
        d2_line("SetupDiDestroyDeviceInfoList(synthetische set)");
        return TRUE;
    }
    return real_d2_destroyset(set);
}

/* Het spel haalt twee SetupAPI-functies DYNAMISCH op (GetProcAddress van "SetupDiEnumDeviceInterfaces" en
 * "SetupDiGetDeviceInterfaceDetailA", gevonden in zijn code op 0x4369e2/0x4369ea), dus die zijn niet in de
 * importtabel te haken: hier wordt GetProcAddress zelf omgeleid. Zo krijgt het spel één synthetische HID-interface
 * met een pad, en wordt de CreateFile daarop door de apparaatmeter geleend. */

static int strnicmp_(const char *a, const char *b, int n) {
    for (int i = 0; i < n; ++i) {
        const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? (char)(a[i] + 32) : a[i];
        const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? (char)(b[i] + 32) : b[i];
        if (ca != cb) return 1;
        if (!ca) return 0;
    }
    return 0;
}

typedef BOOL(WINAPI *d2_enumif_fn)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL(WINAPI *d2_ifdetail_fn)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_A, DWORD,
                                     PDWORD, PSP_DEVINFO_DATA);
typedef FARPROC(WINAPI *d2_getproc_fn)(HMODULE, LPCSTR);
static d2_getproc_fn real_d2_getproc;

static BOOL WINAPI rg_d2_SetupDiEnumDeviceInterfaces(HDEVINFO set, PSP_DEVINFO_DATA info, const GUID *iface,
                                                     DWORD index, PSP_DEVICE_INTERFACE_DATA data) {
    if (set != RG_D2_DEVSET) {
        /* niet onze set: laat de echte functie het doen (die is niet gehaakt, dus zoeken we hem zelf) */
        d2_enumif_fn echt = (d2_enumif_fn)(void *)real_d2_getproc(GetModuleHandleA("setupapi.dll"),
                                                          "SetupDiEnumDeviceInterfaces");
        return echt ? echt(set, info, iface, index, data) : FALSE;
    }
    if (index != 0) {
        d2_line("SetupDiEnumDeviceInterfaces(index=%lu) -> nee (einde)", (unsigned long)index);
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (data) {
        data->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        if (iface) data->InterfaceClassGuid = *iface;
        data->Flags = 1;   /* SPINT_ACTIVE */
        data->Reserved = 0;
    }
    d2_line("SetupDiEnumDeviceInterfaces(index=0) -> een synthetische interface");
    d2_guid("   klasse: ", iface);
    return TRUE;
}

static BOOL WINAPI rg_d2_SetupDiGetDeviceInterfaceDetailA(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA data,
                                                          PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail, DWORD size,
                                                          PDWORD needed, PSP_DEVINFO_DATA info) {
    char pad[256];
    if (set != RG_D2_DEVSET) {
        d2_ifdetail_fn echt = (d2_ifdetail_fn)(void *)real_d2_getproc(GetModuleHandleA("setupapi.dll"),
                                                              "SetupDiGetDeviceInterfaceDetailA");
        return echt ? echt(set, data, detail, size, needed, info) : FALSE;
    }
    snprintf(pad, sizeof pad, "\\\\?\\hid#vid_0471&pid_485e#7&1a2b3c4d&0&0000#{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
             (unsigned long)(data ? data->InterfaceClassGuid.Data1 : 0), data ? data->InterfaceClassGuid.Data2 : 0,
             data ? data->InterfaceClassGuid.Data3 : 0, data ? data->InterfaceClassGuid.Data4[0] : 0,
             data ? data->InterfaceClassGuid.Data4[1] : 0, data ? data->InterfaceClassGuid.Data4[2] : 0,
             data ? data->InterfaceClassGuid.Data4[3] : 0, data ? data->InterfaceClassGuid.Data4[4] : 0,
             data ? data->InterfaceClassGuid.Data4[5] : 0, data ? data->InterfaceClassGuid.Data4[6] : 0,
             data ? data->InterfaceClassGuid.Data4[7] : 0);
    const DWORD nodig = (DWORD)(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A) + strlen(pad));
    d2_line("SetupDiGetDeviceInterfaceDetailA(maat=%lu, nodig=%lu) -> pad %s", (unsigned long)size,
            (unsigned long)nodig, pad);
    if (needed) *needed = nodig;
    if (!detail || size < nodig) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    memcpy(detail->DevicePath, pad, strlen(pad) + 1);
    if (info) {
        info->cbSize = sizeof(SP_DEVINFO_DATA);
        memset(&info->ClassGuid, 0, sizeof info->ClassGuid);
        info->DevInst = 1;
        info->Reserved = 0;
    }
    return TRUE;
}

/* Het spel praat met de dongle via HID-feature-rapporten (HidD_SetFeature als opdracht, HidD_GetFeature als
 * antwoord), ook dynamisch opgehaald. Die twee worden hier geleend en opgeschreven: de opdrachtbytes komen in het
 * log, het antwoord komt uit d2_fill (nullen, of RG_DONGLE2_REPLY). */
typedef BOOL(WINAPI *d2_getfeature_fn)(HANDLE, PVOID, ULONG);
typedef BOOL(WINAPI *d2_setfeature_fn)(HANDLE, PVOID, ULONG);
static d2_getfeature_fn real_d2_getfeature;
static d2_setfeature_fn real_d2_setfeature;

static BOOL WINAPI rg_d2_HidD_SetFeature(HANDLE h, PVOID buf, ULONG len) {
    if (h != RG_D2_HANDLE) {
        if (!real_d2_setfeature) real_d2_setfeature = (d2_setfeature_fn)(void *)real_d2_getproc(GetModuleHandleA("hid.dll"), "HidD_SetFeature");
        return real_d2_setfeature ? real_d2_setfeature(h, buf, len) : FALSE;
    }
    d2_line("HidD_SetFeature %lu bytes", (unsigned long)len);
    if (buf && len) d2_hex("   opdracht: ", buf, len);
    return TRUE;
}

static BOOL WINAPI rg_d2_HidD_GetFeature(HANDLE h, PVOID buf, ULONG len) {
    if (h != RG_D2_HANDLE) {
        if (!real_d2_getfeature) real_d2_getfeature = (d2_getfeature_fn)(void *)real_d2_getproc(GetModuleHandleA("hid.dll"), "HidD_GetFeature");
        return real_d2_getfeature ? real_d2_getfeature(h, buf, len) : FALSE;
    }
    d2_line("HidD_GetFeature %lu bytes -> antwoord", (unsigned long)len);
    if (buf && len) d2_fill((unsigned char *)buf, len);
    if (buf && len) d2_hex("   antwoord: ", buf, len);
    return TRUE;
}

static FARPROC WINAPI rg_d2_GetProcAddress(HMODULE mod, LPCSTR name) {
    FARPROC echt = real_d2_getproc(mod, name);
    if (!name) return echt;
    if (strcmp(name, "HidD_SetFeature") == 0) {
        d2_line("GetProcAddress(HidD_SetFeature) -> eigen haak");
        return (FARPROC)(void *)rg_d2_HidD_SetFeature;
    }
    if (strcmp(name, "HidD_GetFeature") == 0) {
        d2_line("GetProcAddress(HidD_GetFeature) -> eigen haak");
        return (FARPROC)(void *)rg_d2_HidD_GetFeature;
    }
    if (strcmp(name, "SetupDiEnumDeviceInterfaces") == 0) {
        d2_line("GetProcAddress(SetupDiEnumDeviceInterfaces) -> eigen haak");
        return (FARPROC)(void *)rg_d2_SetupDiEnumDeviceInterfaces;
    }
    if (strcmp(name, "SetupDiGetDeviceInterfaceDetailA") == 0) {
        d2_line("GetProcAddress(SetupDiGetDeviceInterfaceDetailA) -> eigen haak");
        return (FARPROC)(void *)rg_d2_SetupDiGetDeviceInterfaceDetailA;
    }
    if (strnicmp_(name, "Hid", 3) == 0 || strstr(name, "hid") || strstr(name, "HID")) {
        static int geteld;
        if (geteld < 40) {
            ++geteld;
            d2_line("GetProcAddress(%s) -> doorgegeven (%p)", name, (void *)echt);
        }
    }
    return echt;
}

static void dongle2_start(HMODULE exe) {
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("RG_DONGLE2_LOG", path, sizeof path);
    if (n == 0 || n >= sizeof path) return;
    d2_log = fopen(path, "w");
    if (!d2_log) return;
    d2_line("rg-compat: apparaatmeter aan (pid %lu), log %s", (unsigned long)GetCurrentProcessId(), path);
    real_d2_create_a = (d2_create_a_fn)patch_import(exe, "kernel32.dll", "CreateFileA", (void *)rg_d2_CreateFileA);
    real_d2_create_w = (d2_create_w_fn)patch_import(exe, "kernel32.dll", "CreateFileW", (void *)rg_d2_CreateFileW);
    real_d2_ioctl = (d2_ioctl_fn)patch_import(exe, "kernel32.dll", "DeviceIoControl", (void *)rg_d2_DeviceIoControl);
    real_d2_read = (d2_read_fn)patch_import(exe, "kernel32.dll", "ReadFile", (void *)rg_d2_ReadFile);
    real_d2_write = (d2_write_fn)patch_import(exe, "kernel32.dll", "WriteFile", (void *)rg_d2_WriteFile);
    real_d2_close = (d2_close_fn)patch_import(exe, "kernel32.dll", "CloseHandle", (void *)rg_d2_CloseHandle);
    real_d2_getclassdevs = (d2_getclassdevs_fn)patch_import(exe, "setupapi.dll", "SetupDiGetClassDevsA",
                                                              (void *)rg_d2_SetupDiGetClassDevsA);
    real_d2_enumdevinfo = (d2_enumdevinfo_fn)patch_import(exe, "setupapi.dll", "SetupDiEnumDeviceInfo",
                                                            (void *)rg_d2_SetupDiEnumDeviceInfo);
    real_d2_getprop = (d2_getprop_fn)patch_import(exe, "setupapi.dll", "SetupDiGetDeviceRegistryPropertyA",
                                                       (void *)rg_d2_SetupDiGetDeviceRegistryPropertyA);
    real_d2_destroyset = (d2_destroyset_fn)patch_import(exe, "setupapi.dll", "SetupDiDestroyDeviceInfoList",
                                                          (void *)rg_d2_SetupDiDestroyDeviceInfoList);
    real_d2_getproc = (d2_getproc_fn)patch_import(exe, "kernel32.dll", "GetProcAddress",
                                                       (void *)rg_d2_GetProcAddress);
    d2_line("GetProcAddress-haak: %d", real_d2_getproc != NULL);
    real_d2_getfiletype = (d2_getfiletype_fn)patch_import(exe, "kernel32.dll", "GetFileType",
                                                          (void *)rg_d2_GetFileType);
    real_d2_getoverlapped = (d2_getoverlapped_fn)patch_import(exe, "kernel32.dll", "GetOverlappedResult",
                                                                (void *)rg_d2_GetOverlappedResult);
    d2_reply_init();
    d2_line("setupapi-haken: GetClassDevs=%d EnumDeviceInfo=%d GetProperty=%d DestroyList=%d",
            real_d2_getclassdevs != NULL, real_d2_enumdevinfo != NULL, real_d2_getprop != NULL,
            real_d2_destroyset != NULL);
    d2_line("haken: CreateFileA=%d CreateFileW=%d DeviceIoControl=%d ReadFile=%d WriteFile=%d CloseHandle=%d",
            real_d2_create_a != NULL, real_d2_create_w != NULL, real_d2_ioctl != NULL, real_d2_read != NULL,
            real_d2_write != NULL, real_d2_close != NULL);
}

/* ------------------------------------- 6. RG_COMPAT_PATCH (bytes in het hoofdbeeld) */

/* Kleine, algemene byte-patch in het hoofdbeeld, synchroon in DllMain: formaat
 *   RG_COMPAT_PATCH=<rva>:<hex>[;<rva>:<hex>...]        (hex = de nieuwe bytes, 0x… of decimaal)
 * De oude bytes worden opgeschreven zodat het log laat zien wat er stond en of het klopte. Alleen voor gevallen
 * waar de geheugen-motor (patchmem) te laat komt of niet zichtbaar is; zonder de variabele gebeurt er niets. */
/* Kop van het beeld opschrijven: zo is te zien of het geladen beeld dezelfde indeling heeft als het bestand. */
static void rg_image_probe(BYTE *base) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        d2_line("beeld: geen MZ (%04x)", dos->e_magic);
        return;
    }
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        d2_line("beeld: geen PE (%08lx)", (unsigned long)nt->Signature);
        return;
    }
    d2_line("beeld: ImageBase=%08lx SizeOfImage=%08lx SectionAlignment=%08lx FileAlignment=%08lx",
            (unsigned long)nt->OptionalHeader.ImageBase, (unsigned long)nt->OptionalHeader.SizeOfImage,
            (unsigned long)nt->OptionalHeader.SectionAlignment, (unsigned long)nt->OptionalHeader.FileAlignment);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections && i < 8; ++i) {
        d2_line("   sectie %.8s va=%08lx vsize=%08lx raw=%08lx rawsize=%08lx", (const char *)s[i].Name,
                (unsigned long)s[i].VirtualAddress, (unsigned long)s[i].Misc.VirtualSize,
                (unsigned long)s[i].PointerToRawData, (unsigned long)s[i].SizeOfRawData);
    }
}

/* Zoeken naar een bytepatroon in het beeld en daar de laatste bytes van vervangen (RG_COMPAT_PATCH_SEARCH=<patroon>,<nieuw>).
 * Nodig als de indeling in het geheugen niet die van het bestand blijkt te zijn: dan klopt een vaste RVA niet. */
static void rg_patch_search(BYTE *base, const char *txt) {
    const char *sep = strchr(txt, ',');
    if (!sep) {
        d2_line("zoekpatch: geen ',' in de opgave");
        return;
    }
    unsigned char pat[64], nieuw[64];
    DWORD pat_len = 0, nieuw_len = 0;
    for (const char *h = txt; h < sep && pat_len < sizeof pat; h += 2) {
        char pair[3] = {h[0], h[1], 0};
        char *end = NULL;
        const unsigned long v = strtoul(pair, &end, 16);
        if (end == pair) break;
        pat[pat_len++] = (unsigned char)v;
    }
    for (const char *h = sep + 1; h[0] && h[1] && nieuw_len < sizeof nieuw; h += 2) {
        char pair[3] = {h[0], h[1], 0};
        char *end = NULL;
        const unsigned long v = strtoul(pair, &end, 16);
        if (end == pair) break;
        nieuw[nieuw_len++] = (unsigned char)v;
    }
    if (!pat_len || !nieuw_len || nieuw_len > pat_len) {
        d2_line("zoekpatch: patroon %lu, nieuw %lu -- onbruikbaar", (unsigned long)pat_len, (unsigned long)nieuw_len);
        return;
    }
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    const DWORD grootte = nt->OptionalHeader.SizeOfImage;
    DWORD gevonden = 0, waar = 0;
    for (DWORD i = 0; i + pat_len <= grootte; ++i) {
        if (memcmp(base + i, pat, pat_len) == 0) {
            ++gevonden;
            waar = i;
            if (gevonden > 1) break;
        }
    }
    if (gevonden != 1) {
        d2_line("zoekpatch: %lu treffers (0 of meer dan 1) -- niets geschreven", (unsigned long)gevonden);
        return;
    }
    BYTE *target = base + waar + (pat_len - nieuw_len);
    char before[3 * 64 + 1];
    int k = 0;
    for (DWORD i = 0; i < nieuw_len; ++i) k += snprintf(before + k, sizeof before - (size_t)k, "%02x", target[i]);
    DWORD old = 0;
    if (!VirtualProtect(target, nieuw_len, PAGE_EXECUTE_READWRITE, &old)) {
        d2_line("zoekpatch: VirtualProtect geweigerd");
        return;
    }
    memcpy(target, nieuw, nieuw_len);
    VirtualProtect(target, nieuw_len, old, &old);
    d2_line("zoekpatch: patroon op +0x%lx, %s -> %s", (unsigned long)(waar + pat_len - nieuw_len), before, sep + 1);
}

static void rg_patch_alle(BYTE *base, const char *txt) {
    char werk[512];
    snprintf(werk, sizeof werk, "%s", txt);
    char *save = NULL;
    for (char *item = strtok_r(werk, "|", &save); item; item = strtok_r(NULL, "|", &save)) {
        rg_patch_search(base, item);
    }
}

static void rg_patch_start(void) {
    char txt[512], mod[128], zoek[256];
    DWORD n = GetEnvironmentVariableA("RG_COMPAT_PATCH", txt, sizeof txt);
    const DWORD zn = GetEnvironmentVariableA("RG_COMPAT_PATCH_SEARCH", zoek, sizeof zoek);
    if ((n == 0 || n >= sizeof txt) && (zn == 0 || zn >= sizeof zoek)) return;
    HMODULE image = NULL;
    const DWORD mn = GetEnvironmentVariableA("RG_COMPAT_PATCH_MODULE", mod, sizeof mod);
    if (mn > 0 && mn < sizeof mod) image = GetModuleHandleA(mod);   /* het spel kan in een kindproces zitten */
    if (!image) image = GetModuleHandleA(NULL);
    BYTE *base = (BYTE *)image;
    d2_line("patch-start: module %s op %p", (mn > 0 && mn < sizeof mod) ? mod : "(hoofdbeeld)", (void *)base);
    rg_image_probe(base);
    d2_line("   base+0=%02x%02x  base+0x1000=%02x%02x%02x", base[0], base[1], base[0x1000], base[0x1001], base[0x1002]);
    d2_line("   poort 0x647b=%02x%02x  transport 0x332cc=%02x%02x%02x%02x  transport 0x332df=%02x%02x%02x%02x",
            base[0x647b], base[0x647c], base[0x332cc], base[0x332cd], base[0x332ce], base[0x332cf], base[0x332df],
            base[0x332e0], base[0x332e1], base[0x332e2]);
    char *save = NULL;
    if (zn > 0 && zn < sizeof zoek) rg_patch_alle(base, zoek);
    for (char *item = n ? strtok_r(txt, ";", &save) : NULL; item; item = strtok_r(NULL, ";", &save)) {
        char *colon = strchr(item, ':');
        if (!colon) continue;
        *colon = 0;
        const unsigned long rva = strtoul(item, NULL, 0);
        const char *hex = colon + 1;
        unsigned char bytes[32];
        DWORD len = 0;
        for (; hex[0] && hex[1] && len < sizeof bytes; hex += 2) {
            char pair[3] = {hex[0], hex[1], 0};
            char *end = NULL;
            const unsigned long v = strtoul(pair, &end, 16);
            if (end == pair) break;
            bytes[len++] = (unsigned char)v;
        }
        if (!len) continue;
        BYTE *target = base + rva;
        char before[3 * 32 + 1];
        int k = 0;
        for (DWORD i = 0; i < len; ++i) k += snprintf(before + k, sizeof before - (size_t)k, "%02x", target[i]);
        DWORD old = 0;
        if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old)) {
            OutputDebugStringA("rg-compat: RG_COMPAT_PATCH VirtualProtect geweigerd\n");
            continue;
        }
        memcpy(target, bytes, len);
        VirtualProtect(target, len, old, &old);
        d2_line("patch op +0x%lx: %s -> %s", rva, before, "");
        char after[3 * 32 + 4];
        k = 0;
        for (DWORD i = 0; i < len; ++i) k += snprintf(after + k, sizeof after - (size_t)k, "%02x", target[i]);
        d2_line("   nieuwe bytes: %s", after);
    }
}

/* ------------------------------------- 7. RG_DONGLE2_WRAP (vtable-trampoline) */

/* After Dark's own dongle object (ATL CCoDongle2, vtable 0x7d3ca4 = image+0x3d3ca4) keeps its records in a
 * table at [obj+0x20] with count [obj+0x24]; the gate in 0x415ca0 looks records up there by an 8-char name
 * ("InsGame"). This instrument replaces the entries of that vtable with transparent thunks: every thunk saves
 * the registers, logs the raw stack arguments (an argument that points at printable text is shown as text) and
 * hands the call unchanged to the original; on the way back a small return-thunk logs EAX and returns to the
 * caller. The thunk never touches the arguments or the stack cleanup, so the calling convention of the entry
 * does not matter. The table fill (slot 8) also dumps the whole table into the log: name at +0x60, payload at
 * +0x00. Only with RG_DONGLE2_WRAP=<rva> (the vtable, relative to the main image) and RG_DONGLE2_WRAP_SLOTS. */

#define W7_MAX 32

static void *w7_orig[W7_MAX];
__attribute__((used)) static void *w7_cont[W7_MAX];
static const int w7_map2[16] = {4, 12, 20, 22, 14, 5, 6, 7, 8, 9, 10, 11, 13, 15, 16, 17};
static int w7_answer;
__attribute__((used)) static int w7_busy[W7_MAX];
static void *w7_last_obj[W7_MAX];
static DWORD *w7_last_st[W7_MAX];

static BYTE *w7_base;
static DWORD w7_size;

static int w7_text_ok(const char *s) {
    int n = 0;
    for (; n < 24 && s[n]; ++n) {
        const unsigned char c = (unsigned char)s[n];
        if (c < 0x20 || c > 0x7e) return 0;
    }
    return n >= 2;
}

/* Een argument als tekst tonen alleen als het in het hoofdbeeld zelf ligt: die pagina's zijn leesbaar, dus
 * dit kan geen fout veroorzaken (de namen "InsGame" en "3DGame" liggen in .rdata van het beeld). */
static void w7_arg(char *out, size_t cap, DWORD v, BYTE *base, DWORD size) {
    if (base && v >= (DWORD)(ULONG_PTR)base && v + 24 < (DWORD)(ULONG_PTR)base + size && w7_text_ok((const char *)v)) {
        snprintf(out, cap, "%p \"%.24s\"", (void *)v, (const char *)v);
        return;
    }
    snprintf(out, cap, "%p", (void *)v);
}

static void w7_dump_table(void *o) {
    if (!o) return;
    const DWORD *obj = (const DWORD *)o;
    const DWORD count = obj[0x24 / 4];
    const BYTE *tab = (const BYTE *)obj[0x20 / 4];
    d2_line("   tabel: %lu records op %p", (unsigned long)count, (const void *)tab);
    if (!tab || count > 0x100) return;
    for (DWORD i = 0; i < count; ++i) {
        const BYTE *e = tab + (size_t)i * 0x6c;
        char naam[9];
        memcpy(naam, e + 0x60, 8);
        naam[8] = 0;
        for (int k = 0; k < 8; ++k) if (naam[k] < 0x20 || naam[k] > 0x7e) naam[k] = '.';
        char hex[3 * 32 + 1];
        int p = 0;
        for (DWORD b = 0; b < 32; ++b) p += snprintf(hex + p, sizeof hex - (size_t)p, "%02x", e[b]);
        d2_line("   record %lu naam \"%s\" inhoud %s", (unsigned long)i, naam, hex);
    }
}

__attribute__((used)) static void w7_on_call(int idx, void *regs) {
    if (!d2_log || idx < 0 || idx >= W7_MAX) return;
    /* De aanwijzer komt van na "pushal; pushfl", dus de vlaggen staan vooraan; daarachter de pushal-reeks. */
    const DWORD *r = (const DWORD *)regs + 1;  /* pushal: edi esi ebp esp ebx edx ecx eax */
    const DWORD *st = (const DWORD *)r[3];     /* bij de aanroep: [0]=terugkeer, [1..]=argumenten */
    char a1[80], a2[80], a3[80], a4[80], a5[80];
    w7_arg(a1, sizeof a1, st[1], w7_base, w7_size);
    w7_arg(a2, sizeof a2, st[2], w7_base, w7_size);
    w7_arg(a3, sizeof a3, st[3], w7_base, w7_size);
    w7_arg(a4, sizeof a4, st[4], w7_base, w7_size);
    w7_arg(a5, sizeof a5, st[5], w7_base, w7_size);
    const char *vt = idx < 16 ? "dongle" : "record";
    const DWORD off = (DWORD)(idx < 16 ? idx * 4 : w7_map2[idx - 16] * 4);
    static unsigned long telling[W7_MAX];
    const unsigned long n = ++telling[idx];
    if (n <= 30 || (n % 200) == 0)
        d2_line("%s[+0x%02x] arg1=%s arg2=%s arg3=%s arg4=%s arg5=%s ecx=%p (nr %lu)", vt, off, a1, a2, a3, a4,
                a5, (void *)r[6], n);
    w7_last_obj[idx] = (void *)st[1];
    w7_last_st[idx] = (DWORD *)st;
}

/* Antwoordstand: na de mislukte tabelvulling ([vt+0x20]) leggen we een synthetische tabel in het object:
 * een record van 0x6c bytes (hetzelfde formaat dat de vulling zelf gebruikt, zie 0x40a632: count en 0x6c via
 * [0x7d039c]) met de naam "InsGame" op +0x60 en een lege sleutel van 32 bytes op +0x00. [vt+0x14] zoekt op die
 * naam en [vt+0x10] vergelijkt de 32 bytes van het record met wat de vrager kreeg (0x40a580), dus nullen aan
 * beide kanten matchen. De teller en de wijzer staan op dezelfde plaatsen als de vulling ze gebruikt: +0x24 en
 * +0x20. */
static void *w7_synth_table(void *obj) {
    if (!obj || !w7_base) return NULL;
    DWORD *const veld = (DWORD *)obj;
    if (veld[0x24 / 4] != 0 || veld[0x20 / 4] != 0) return NULL;
    typedef void *(CDECL *alloc_fn)(DWORD, DWORD);
    alloc_fn alloc = *(alloc_fn *)((BYTE *)w7_base + 0x3d039c);
    if (!alloc) return NULL;
    /* Het spel vraagt dezelfde record ook met een oplopende teller op (0x40a7b0 slaat zoveel treffers over:
     * gemeten arg4=0xFF), dus de dongle hoort een rij van 0x100 gelijke "InsGame"-records te hebben -- precies
     * het aantal slots dat de poort zelf afloopt (0x415ca0 tot 0x8987b0). */
    const DWORD aantal = 0x100;
    BYTE *e = (BYTE *)alloc(aantal, 0x6c);
    if (!e) return NULL;
    memset(e, 0, (size_t)aantal * 0x6c);
    /* De velden die de spellogica zelf toetst (0x42f9e0 leest het record vanaf +0x10 als een structuur:
     * [0] < 0x7f, [4] >= 0x20000, [0x58] != 0, en [0x10] is de id voor de apparaatvraag). */
    for (DWORD i = 0; i < aantal; ++i) {
        BYTE *r = e + (size_t)i * 0x6c;
        *(DWORD *)(r + 0x10) = 1;              /* < 0x7f            (0x42f9e0) */
        *(DWORD *)(r + 0x14) = 0x20000;        /* >= 0x20000        (0x42f9e0) */
        *(DWORD *)(r + 0x20) = i;              /* id van deze slot  (apparaatvraag 0x432bc0) */
        *(DWORD *)(r + 0x24) = 0;
        *(DWORD *)(r + 0x68) = i + 1;          /* != 0              (0x42f9e0) */
        memcpy(r + 0x60, "InsGame", 8);
    }
    veld[0x20 / 4] = (DWORD)(ULONG_PTR)e;
    veld[0x24 / 4] = aantal;
    d2_line("   synthetische tabel: %lu records \"InsGame\" op %p", (unsigned long)aantal, (void *)e);
    return e;
}

/* Overslaan in antwoordstand: de lees-methoden van het recordobject wachten op het apparaat (de hoofd- en de
 * bewaakdraad staan beiden in een wacht binnen 0x40c110) en komen dus nooit terug. Voor die entries antwoordt
 * de thunk zelf met S_OK (en nullen in de uitbuffer) en ruimt hij zijn eigen argumenten op, met de maat die de
 * methode zelf gebruikt (0x40bbf0 ret 8, 0x40c110 ret 0x14, 0x40bab0 ret 0xc, 0x40bc90 ret 4). */
__attribute__((used)) static void w7_on_skip(int idx, void *regs) {
    if (!d2_log || idx < 0 || idx >= W7_MAX) return;
    const DWORD *r = (const DWORD *)regs + 1;
    const DWORD *st = (const DWORD *)r[3];
    const DWORD slot = idx < 16 ? (DWORD)idx : (DWORD)w7_map2[idx - 16];
    if (slot == 20 && st[3] && (ULONG_PTR)st[3] > 0x10000) {
        const DWORD len = st[4] > 0x40 ? 0x40 : st[4];
        memset((void *)st[3], 0, len);
    } else if (slot == 12 && st[2] && (ULONG_PTR)st[2] > 0x10000) {
        memset((void *)st[2], 0, 8);
    }
    static unsigned long n;
    ++n;
    if (n <= 20 || (n % 500) == 0)
        d2_line("record[+0x%02x] overgeslagen (antwoordstand, obj %p, vrager +0x%lx, totaal %lu)", slot * 4,
                (void *)st[1], (unsigned long)(st[0] - (DWORD)(ULONG_PTR)w7_base), n);
}

#define W7_SKIP(n, bytes)                                                                \
    __attribute__((naked, used)) static void w7_sk_##n(void) {                            \
        __asm__ __volatile__(                                                            \
            "pushal\n\tpushfl\n\t"                                                    \
            "movl %esp, %eax\n\t"                                                       \
            "pushl %eax\n\tpushl $" #n "\n\t"                                          \
            "call _w7_on_skip\n\taddl $8, %esp\n\t"                                    \
            "popfl\n\tpopal\n\t"                                                      \
            "xorl %eax, %eax\n\t"                                                       \
            "ret $" #bytes "\n\t");                                                    \
    }

W7_SKIP(17, 0x8) W7_SKIP(18, 0x14) W7_SKIP(19, 0xc) W7_SKIP(20, 0x4)

__attribute__((used)) static unsigned long w7_on_ret(int idx, unsigned long ret) {
    if (!d2_log || idx < 0 || idx >= W7_MAX) return ret;
    const char *vt = idx < 16 ? "dongle" : "record";
    const DWORD off = (DWORD)(idx < 16 ? idx * 4 : w7_map2[idx - 16] * 4);
    d2_line("%s[+0x%02x] -> %08lx", vt, off, ret);
    if (idx == 8) {
        if (w7_answer && ret == 0x80004005u && w7_synth_table(w7_last_obj[idx])) ret = 0;
        w7_dump_table(w7_last_obj[idx]);
    }
    /* Antwoordstand op het recordobject: de lees-methoden gaan naar hetzelfde apparaat als de vulling en geven
     * daar zonder dongle een fout; met een leeg record erin is de inhoud van die 8 bytes voor het spel zelf niet
     * te toetsen (het leest ze alleen terug in de bewaakdraad), dus antwoorden we met S_OK en nullen. */
    if (w7_answer && idx >= 16 && (ret & 0x80000000u) != 0) {
        const DWORD slot = (DWORD)w7_map2[idx - 16];
        if (slot == 20 || slot == 12 || slot == 22) {
            if (slot == 20 && w7_last_st[idx]) {
                const DWORD *st = w7_last_st[idx];
                BYTE *out = (BYTE *)st[3];
                const DWORD len = st[4] > 0x40 ? 0x40 : st[4];
                if (out && (ULONG_PTR)out > 0x10000) memset(out, 0, len);
            }
            if (slot != 12 || w7_last_obj[idx]) {
                d2_line("   antwoordstand: %08lx -> 0 (record +0x%02x)", ret, slot * 4);
                ret = 0;
            }
        }
    }
    if (idx == 5 && (ret & 0x80000000u) == 0 && w7_last_st[idx]) {
        const DWORD *st = w7_last_st[idx];
        const BYTE *out = (const BYTE *)st[2];
        if (out) {
            char hex[3 * 32 + 1];
            int p = 0;
            for (DWORD b = 0; b < 32; ++b) p += snprintf(hex + p, sizeof hex - (size_t)p, "%02x", out[b]);
            d2_line("   recordinhoud bij de vrager: %s", hex);
        }
    }
    return ret;
}

#define W7_THUNK(n)                                                                      \
    __attribute__((naked, used)) static void w7_call_##n(void) {                               \
        __asm__ __volatile__(                                                            \
            "pushal\n\tpushfl\n\t"                                                       \
            "movl %esp, %eax\n\t"                                                        \
            "pushl %eax\n\tpushl $" #n "\n\t"                                            \
            "call _w7_on_call\n\taddl $8, %esp\n\t"                                      \
            "popfl\n\tpopal\n\t"                                                         \
            "movl (%esp), %eax\n\t"                                                      \
            "cmpl $0, _w7_busy+" #n "*4\n\t"                                             \
            "jne 1f\n\t"                                                                 \
            "movl $1, _w7_busy+" #n "*4\n\t"                                             \
            "movl %eax, _w7_cont+" #n "*4\n\t"                                           \
            "movl $_w7_ret_" #n ", (%esp)\n\t"                                           \
            "1:\n\t"                                                                     \
            "movl _w7_orig+" #n "*4, %edx\n\t"                                           \
            "jmp *%edx\n\t");                                                            \
    }                                                                                    \
    __attribute__((naked, used)) static void w7_ret_##n(void) {                                \
        __asm__ __volatile__(                                                            \
            "pushal\n\t"                                                                 \
            "pushl %eax\n\tpushl $" #n "\n\t"                                            \
            "call _w7_on_ret\n\taddl $8, %esp\n\t"                                       \
            "movl $0, _w7_busy+" #n "*4\n\t"                                             \
            "movl %eax, 28(%esp)\n\t"                                                    \
            "popal\n\tmovl _w7_cont+" #n "*4, %eax\n\tjmp *%eax\n\t");                   \
    }

W7_THUNK(0) W7_THUNK(1) W7_THUNK(2) W7_THUNK(3) W7_THUNK(4) W7_THUNK(5) W7_THUNK(6) W7_THUNK(7)
W7_THUNK(8) W7_THUNK(9) W7_THUNK(10) W7_THUNK(11) W7_THUNK(12) W7_THUNK(13) W7_THUNK(14) W7_THUNK(15)
W7_THUNK(16) W7_THUNK(17) W7_THUNK(18) W7_THUNK(19) W7_THUNK(20) W7_THUNK(21) W7_THUNK(22) W7_THUNK(23)
W7_THUNK(24) W7_THUNK(25) W7_THUNK(26) W7_THUNK(27) W7_THUNK(28) W7_THUNK(29) W7_THUNK(30) W7_THUNK(31)

static void *const w7_thunks[W7_MAX] = {
    (void *)w7_call_0, (void *)w7_call_1, (void *)w7_call_2, (void *)w7_call_3, (void *)w7_call_4,
    (void *)w7_call_5, (void *)w7_call_6, (void *)w7_call_7, (void *)w7_call_8, (void *)w7_call_9,
    (void *)w7_call_10, (void *)w7_call_11, (void *)w7_call_12, (void *)w7_call_13, (void *)w7_call_14,
    (void *)w7_call_15, (void *)w7_call_16, (void *)w7_call_17, (void *)w7_call_18, (void *)w7_call_19,
    (void *)w7_call_20, (void *)w7_call_21, (void *)w7_call_22, (void *)w7_call_23, (void *)w7_call_24,
    (void *)w7_call_25, (void *)w7_call_26, (void *)w7_call_27, (void *)w7_call_28, (void *)w7_call_29,
    (void *)w7_call_30, (void *)w7_call_31,
};

/* Een entry omwikkelen: thunknummer n (dat de log en de originelen indexeert) op vtable-entry slot. */
static void w7_wrap_one(void **vt, int n, int slot, const char *naam) {
    DWORD old = 0;
    if (!VirtualProtect(&vt[slot], sizeof(void *), PAGE_READWRITE, &old)) {
        d2_line("wrap: VirtualProtect geweigerd op %s entry %d", naam, slot);
        return;
    }
    w7_orig[n] = vt[slot];
    vt[slot] = w7_thunks[n];
    VirtualProtect(&vt[slot], sizeof(void *), old, &old);
    d2_line("wrap: %s entry %d (+0x%02x) %p -> thunk", naam, slot, slot * 4, w7_orig[n]);
}

static void w7_start(void) {
    char txt[160];
    DWORD n = GetEnvironmentVariableA("RG_DONGLE2_WRAP", txt, sizeof txt);
    if (n == 0 || n >= sizeof txt) return;
    HMODULE image = GetModuleHandleA(NULL);
    const unsigned long rva = strtoul(txt, NULL, 0);
    if (!image || !rva) {
        d2_line("wrap: geen hoofdbeeld of rva");
        return;
    }
    w7_base = (BYTE *)image;
    {
        IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)image + dos->e_lfanew);
        w7_size = nt->OptionalHeader.SizeOfImage;
    }
    char antw[8];
    w7_answer = GetEnvironmentVariableA("RG_DONGLE2_WRAP_ANSWER", antw, sizeof antw) > 0 && antw[0] == '1';
    d2_line("wrap: antwoordstand %s", w7_answer ? "aan" : "uit");

    /* Het dongle-object zelf: RG_DONGLE2_WRAP=<rva> met RG_DONGLE2_WRAP_SLOTS=<entry,...>. */
    void **vt = (void **)((BYTE *)image + rva);
    char slots[160];
    if (GetEnvironmentVariableA("RG_DONGLE2_WRAP_SLOTS", slots, sizeof slots)) {
        d2_line("wrap: dongle-vtable %p (+0x%lx)", (void *)vt, rva);
        for (char *p = slots; *p;) {
            const long slot = strtol(p, &p, 0);
            if (slot < 0 || slot >= 16) break;
            while (*p == ',' || *p == ' ') ++p;
            w7_wrap_one(vt, (int)slot, (int)slot, "dongle");
        }
    }

    /* Het recordobject dat uit een record wordt gemaakt (0x40ae20 zet zijn vptr op 0x7d2564). */
    n = GetEnvironmentVariableA("RG_DONGLE2_WRAP2", txt, sizeof txt);
    if (n > 0 && n < sizeof txt) {
        const unsigned long rva2 = strtoul(txt, NULL, 0);
        void **vt2 = (void **)((BYTE *)image + rva2);
        d2_line("wrap: record-vtable %p (+0x%lx), antwoordstand %s", (void *)vt2, rva2,
                w7_answer ? "slaat de leesmethoden over" : "alleen meten");
        for (int k = 0; k < 16; ++k) {
            const int idx = 16 + k;
            const BOOL overslaan = w7_answer && (w7_map2[k] == 12 || w7_map2[k] == 14 || w7_map2[k] == 20 || w7_map2[k] == 22);
            DWORD old = 0;
            if (overslaan) {
                void *stub = NULL;
                if (idx == 17) stub = (void *)w7_sk_17;
                else if (idx == 18) stub = (void *)w7_sk_18;
                else if (idx == 19) stub = (void *)w7_sk_19;
                else if (idx == 20) stub = (void *)w7_sk_20;
                if (stub && VirtualProtect(&vt2[w7_map2[k]], sizeof(void *), PAGE_READWRITE, &old)) {
                    w7_orig[idx] = vt2[w7_map2[k]];
                    vt2[w7_map2[k]] = stub;
                    VirtualProtect(&vt2[w7_map2[k]], sizeof(void *), old, &old);
                    d2_line("wrap: record entry %d (+0x%02x) %p -> overslaan-thunk", w7_map2[k],
                            w7_map2[k] * 4, w7_orig[idx]);
                    continue;
                }
            }
            w7_wrap_one(vt2, idx, w7_map2[k], "record");
        }
    }
}

/* ------------------------------- 8. RG_DONGLE2_CALLTRACE (directe calls volgen) */

/* Voor de stappen die geen vtable gebruiken maar een directe `call rel32` (het maken van het recordobject
 * 0x40ae20 -> 0x403820, en het vullen 0x40af60 -> 0x40b350). RG_DONGLE2_CALLTRACE=<rva>,<rva> vervangt die
 * call door een kleine thunk die de aanroep met zijn argumenten opschrijft en dan naar het originele doel
 * springt; de stapel en de registers blijven zoals ze waren. Zo is te zien welke stap nog loopt als het spel
 * daarna stopt. */

#define W7C_MAX 4
static void *w7c_target[W7C_MAX];
static unsigned long w7c_site[W7C_MAX];

__attribute__((used)) static void w7c_on_call(int idx, void *regs) {
    if (!d2_log || idx < 0 || idx >= W7C_MAX) return;
    const DWORD *r = (const DWORD *)regs;
    const DWORD *st = (const DWORD *)r[3];
    char a1[80], a2[80], a3[80];
    w7_arg(a1, sizeof a1, st[1], w7_base, w7_size);
    w7_arg(a2, sizeof a2, st[2], w7_base, w7_size);
    w7_arg(a3, sizeof a3, st[3], w7_base, w7_size);
    d2_line("call +0x%lx -> %p arg1=%s arg2=%s arg3=%s", w7c_site[idx], w7c_target[idx], a1, a2, a3);
}

#define W7CALL(n)                                                                        \
    __attribute__((naked, used)) static void w7c_##n(void) {                              \
        __asm__ __volatile__(                                                            \
            "pushal\n\t"                                                                \
            "movl %esp, %eax\n\t"                                                       \
            "pushl %eax\n\tpushl $" #n "\n\t"                                          \
            "call _w7c_on_call\n\taddl $8, %esp\n\t"                                   \
            "popal\n\t"                                                                 \
            "movl _w7c_target+" #n "*4, %eax\n\t"                                       \
            "jmp *%eax\n\t");                                                           \
    }

W7CALL(0) W7CALL(1) W7CALL(2) W7CALL(3)

static void *const w7c_stubs[W7C_MAX] = {(void *)w7c_0, (void *)w7c_1, (void *)w7c_2, (void *)w7c_3};

static void w7_calltrace_start(void) {
    char txt[160];
    DWORD n = GetEnvironmentVariableA("RG_DONGLE2_CALLTRACE", txt, sizeof txt);
    if (n == 0 || n >= sizeof txt) return;
    HMODULE image = GetModuleHandleA(NULL);
    if (!image) return;
    int idx = 0;
    for (char *p = txt; *p && idx < W7C_MAX;) {
        const long rva = strtol(p, &p, 0);
        while (*p == ',' || *p == ' ') ++p;
        if (rva <= 0) break;
        BYTE *site = (BYTE *)image + rva;
        if (site[0] != 0xe8) {
            d2_line("calltrace: +0x%lx is geen call (%02x)", (unsigned long)rva, site[0]);
            continue;
        }
        const long rel = (long)*(int *)(site + 1);
        w7c_target[idx] = (void *)(site + 5 + rel);
        w7c_site[idx] = (unsigned long)rva;
        const long nieuw = (long)((BYTE *)w7c_stubs[idx] - (site + 5));
        DWORD old = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) {
            d2_line("calltrace: VirtualProtect geweigerd op +0x%lx", (unsigned long)rva);
            continue;
        }
        const DWORD bewaar = *(DWORD *)(site + 1);
        site[0] = 0xe8;
        *(int *)(site + 1) = (int)nieuw;
        VirtualProtect(site, 5, old, &old);
        d2_line("calltrace: +0x%lx (was -> %p) -> thunk %d", (unsigned long)rva, w7c_target[idx], idx);
        (void)bewaar;
        ++idx;
    }
}


/* ------------------------------- 9. RG_COMPAT_EXITLOG (waarom sluit het spel?) */

/* Hookt ExitProcess en schrijft de retourketen op de stapel naar <pad>: alleen woorden die in het
 * hoofdbeeld vallen (beeld .. beeld+grootte), als RVA. Zo is te zien WIE de afsluiting aanvraagt,
 * ook zonder foutcode (het geval van Coopers 9, dat met code 0 vertrekt). */
static void (*real_exitprocess)(UINT);
static void WINAPI rg_exitlog_ExitProcess(UINT code);
static HMODULE exitlog_image;
static DWORD exitlog_size;

static void exitlog_start(HMODULE exe) {
    char path[MAX_PATH];
    if (GetEnvironmentVariableA("RG_COMPAT_EXITLOG", path, sizeof path) == 0) return;
    exitlog_image = exe;
    const BYTE *b = (const BYTE *)exe;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)exe;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(b + dos->e_lfanew);
    exitlog_size = nt->OptionalHeader.SizeOfImage;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void *oud = patch_import(exe, "kernel32.dll", "ExitProcess", (void *)rg_exitlog_ExitProcess);
    if (!oud) {
        OutputDebugStringA("rg-compat: ExitProcess niet gevonden in de IAT\n");
        return;
    }
    real_exitprocess = (void (*)(UINT))oud;
    d2_line("exitlog: ExitProcess gehaakt, beeld %p + 0x%lx", (void *)exe, (unsigned long)exitlog_size);
    (void)k32;
}

static void WINAPI rg_exitlog_ExitProcess(UINT code) {
    d2_line("ExitProcess(%u): retourketen", (unsigned)code);
    const DWORD *p = (const DWORD *)&code;
    int geteld = 0;
    for (int i = 0; i < 400 && geteld < 24; ++i) {
        const DWORD w = p[i];
        if (w > (DWORD)(uintptr_t)exitlog_image && w < (DWORD)(uintptr_t)exitlog_image + exitlog_size) {
            d2_line("   stapel[%d] = +0x%lx", i, (unsigned long)(w - (DWORD)(uintptr_t)exitlog_image));
            ++geteld;
        }
    }
    if (real_exitprocess) real_exitprocess(code);
}


/* ------------------------------- 10. RG_COMPAT_IATFIX (importtabel van een dump rechtzetten) */

/* Coopers's 9 is een vlakke geheugendump van een beschermd XP-proces: de descriptors staan er (met namen in
 * de OriginalFirstThunk), maar de FirstThunk-sleutels bevatten de adressen uit dat XP-proces (0x7C80xxxx).
 * Deze fix loopt alle descriptors langs en zet in elke FirstThunk-sleutel het adres dat de naam in deze
 * processruimte werkelijk heeft; alles wordt naar RG_DONGLE2_LOG geschreven zodat de indeling te lezen is.
 * Eerste ronde meteen (voor de modules die de lader al heeft), tweede ronde vanuit een draad voor de rest. */

static void iatfix_pass(HMODULE exe, int alleen_geladen) {
    const BYTE *b = (const BYTE *)exe;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)exe;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(b + dos->e_lfanew);
    const IMAGE_IMPORT_DESCRIPTOR *desc =
        (const IMAGE_IMPORT_DESCRIPTOR *)(b + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    if (!nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress) {
        d2_line("iatfix: geen importmap");
        return;
    }
    for (; desc->Name; ++desc) {
        const char *dll = (const char *)(b + desc->Name);
        HMODULE mod = GetModuleHandleA(dll);
        if (!mod) {
            if (alleen_geladen) continue;
            mod = LoadLibraryA(dll);
            if (!mod) {
                d2_line("iatfix: %s niet te laden", dll);
                continue;
            }
            d2_line("iatfix: %s geladen op %p", dll, (void *)mod);
        }
        const DWORD oft = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
        const DWORD ft = desc->FirstThunk ? desc->FirstThunk : desc->OriginalFirstThunk;
        const DWORD *namen = (const DWORD *)(b + oft);
        DWORD *sleutels = (DWORD *)(b + ft);
        int gezet = 0, mis = 0;
        for (int i = 0; namen[i]; ++i) {
            FARPROC proc = NULL;
            if (namen[i] & 0x80000000u) {
                proc = GetProcAddress(mod, (LPCSTR)(ULONG_PTR)(namen[i] & 0xffff));
            } else {
                const char *naam = (const char *)(b + namen[i] + 2);
                proc = GetProcAddress(mod, naam);
                if (!proc) {
                    d2_line("iatfix: %s!%s niet gevonden", dll, naam);
                    ++mis;
                    continue;
                }
                if (alleen_geladen && sleutels[i] != (DWORD)(ULONG_PTR)proc) {
                    d2_line("iatfix: %s!%s sleutel %08lx -> %p", dll, naam, (unsigned long)sleutels[i], (void *)proc);
                }
            }
            if (proc) {
                sleutels[i] = (DWORD)(ULONG_PTR)proc;
                ++gezet;
            }
        }
        d2_line("iatfix: %s %d sleutels gezet, %d niet gevonden", dll, gezet, mis);
    }
}

static DWORD WINAPI iatfix_thread(void *arg) {
    HMODULE exe = (HMODULE)arg;
    Sleep(0);
    iatfix_pass(exe, 0);
    d2_line("iatfix: tweede ronde klaar");
    return 0;
}

static void iatfix_start(HMODULE exe) {
    if (GetEnvironmentVariableA("RG_COMPAT_IATFIX", NULL, 0) == 0) return;
    d2_line("iatfix: aan");
    iatfix_pass(exe, 1);
    CreateThread(NULL, 0, iatfix_thread, (void *)exe, 0, NULL);
}


/* ------------------------------- 11. RG_COMPAT_TABLEPROBE (aanroeptabel van een beschermd spel) */

/* Coopers's 9 roept zijn beschermingslaag aan via een tabel in .rdata (0x512000): `call *0x512xxx`. Deze
 * sonde zet in elke sleutel van dat bereik een klein stubje dat de index, de vrager en de eerste vier
 * stapelargumenten naar RG_DONGLE2_LOG schrijft en daarna 0 teruggeeft (of de waarde uit
 * RG_COMPAT_TABLEPROBE_RET=<idx:waarde,...>). Zo is de volgorde en het gebruik van die laag te lezen zonder
 * de bescherming zelf te hebben. Stub: pushad; pushfd; push idx; call gemeen; popfd; popad; ret. */

#define TP_MAX 512
static void *tp_stubs;
static DWORD tp_base;

__attribute__((used)) static unsigned long tp_ret_waarde(int idx) {
    char txt[256];
    const DWORD n = GetEnvironmentVariableA("RG_COMPAT_TABLEPROBE_RET", txt, sizeof txt);
    if (n == 0 || n >= sizeof txt) return 0;
    for (char *p = txt; *p;) {
        const int i = (int)strtol(p, &p, 0);
        if (*p == ':') ++p;
        const unsigned long w = strtoul(p, &p, 0);
        if (i == idx) return w;
        if (*p == ',') ++p;
    }
    return 0;
}

__attribute__((used)) static unsigned long tp_on_call(int idx, const DWORD *arg0) {
    if (idx >= 0 && idx < TP_MAX) {
        const DWORD vrager = arg0[-1];
        static unsigned long telling[TP_MAX];
        const unsigned long n = ++telling[idx];
        if (n <= 40 || (n % 500) == 0)
            d2_line("tabel[%d] (VA %08lx) vrager %08lx -> 0, arg %08lx %08lx %08lx %08lx (nr %lu)", idx,
                    (unsigned long)(tp_base + (DWORD)idx * 4), (unsigned long)vrager, (unsigned long)arg0[0],
                    (unsigned long)arg0[1], (unsigned long)arg0[2], (unsigned long)arg0[3], n);
    }
    return tp_ret_waarde(idx);
}

/* De stubjes springen hierheen met op de stapel: [esp] = retour naar het stubje, [esp+4] = index,
 * [esp+8] = vlaggen, [esp+12..40] = de acht registers van pushal, [esp+44] = de vrager, [esp+48] = het
 * eerste argument. Wij geven het adres van dat eerste argument door en laten het resultaat in EAX staan;
 * het stubje zet dat in het bewaarde EAX van pushal en herstelt daarna alles. */
__attribute__((naked, used)) static void tp_common(void) {
    __asm__ volatile("leal 48(%esp), %edx\n\t"
                     "movl 4(%esp), %eax\n\t"
                     "push %edx\n\t"
                     "push %eax\n\t"
                     "call _tp_on_call\n\t"
                     "add $8, %esp\n\t"
                     "ret\n\t");
}

static void tableprobe_start(HMODULE exe) {
    char txt[128];
    const DWORD n = GetEnvironmentVariableA("RG_COMPAT_TABLEPROBE", txt, sizeof txt);
    if (n == 0 || n >= sizeof txt) return;
    char *einde = NULL;
    const DWORD va = (DWORD)strtoul(txt, &einde, 0);
    DWORD aantal = (DWORD)strtoul(einde && *einde == ':' ? einde + 1 : "256", NULL, 0);
    if (aantal > TP_MAX) aantal = TP_MAX;
    if (!va) return;
    tp_base = va;
    tp_stubs = VirtualAlloc(NULL, (size_t)aantal * 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tp_stubs) {
        d2_line("tabelsonde: geen geheugen");
        return;
    }
    DWORD oud = 0;
    if (!VirtualProtect((void *)(uintptr_t)va, (size_t)aantal * 4, PAGE_READWRITE, &oud)) {
        d2_line("tabelsonde: tabel op %08lx niet schrijfbaar", (unsigned long)va);
        return;
    }
    BYTE *code = (BYTE *)tp_stubs;
    DWORD *slot = (DWORD *)(uintptr_t)va;
    for (DWORD i = 0; i < aantal; ++i) {
        BYTE *b = code + (size_t)i * 16;
        b[0] = 0x60;                                        /* pushal          */
        b[1] = 0x9c;                                        /* pushfl          */
        b[2] = 0x68;                                        /* push imm32      */
        *(DWORD *)(b + 3) = i;
        b[7] = 0xe8;                                        /* call tp_common  */
        *(int *)(b + 8) = (int)((BYTE *)tp_common - (b + 12));
        b[12] = 0xc3;                                       /* ret             */
        b[13] = 0x90;
        b[14] = 0x90;
        b[15] = 0x90;
        if (slot[i]) slot[i] = (DWORD)(uintptr_t)b;
    }
    d2_line("tabelsonde: %lu sleutels vanaf %08lx bezet (stubjes %p)", (unsigned long)aantal, (unsigned long)va,
            tp_stubs);
    (void)exe;
}

/* ------------------------------- 12. RG_COMPAT_HOOK (functie-entry volgen) */

/* Sommige functies worden alleen via een functiewijzer aangeroepen (de Virtools UserBB's van After Dark),
 * dus een calltrace op een aanroepsite bestaat daar niet. Deze haak zet op de entry van zo een functie een
 * sprong naar een stubje dat de vrager en de eerste vier stapelargumenten schrijft en daarna de eerste
 * zes verplaatste bytes uitvoert en terugsprong. RG_COMPAT_HOOK=<rva>,<rva>,... (maximaal 4). */

#define HK_MAX 4
__attribute__((used)) static BYTE *hk_tramp[HK_MAX];
static DWORD hk_rva[HK_MAX];

__attribute__((used)) static void hk_log(int idx, const DWORD *p) {
    if (idx < 0 || idx >= HK_MAX) return;
    static unsigned long telling[HK_MAX];
    const unsigned long n = ++telling[idx];
    if (n <= 60 || (n % 500) == 0) {
        char a1[64], a2[64], a3[64], a4[64];
        w7_arg(a1, sizeof a1, p[1], w7_base, w7_size);
        w7_arg(a2, sizeof a2, p[2], w7_base, w7_size);
        w7_arg(a3, sizeof a3, p[3], w7_base, w7_size);
        w7_arg(a4, sizeof a4, p[4], w7_base, w7_size);
        d2_line("haak[%d] +0x%lx vrager %08lx arg %s | %s | %s | %s (nr %lu)", idx, (unsigned long)hk_rva[idx],
                (unsigned long)p[0], a1, a2, a3, a4, n);
    }
}

#define HK_STUB(n)                                                                                             \
    __attribute__((naked, used)) static void hk_stub_##n(void) {                                               \
        __asm__ volatile("pushal\n\t"                                                                          \
                         "pushfl\n\t"                                                                          \
                         "leal 36(%esp), %eax\n\t"                                                             \
                         "push %eax\n\t"                                                                       \
                         "push $" #n "\n\t"                                                                    \
                         "call _hk_log\n\t"                                                                    \
                         "add $8, %esp\n\t"                                                                    \
                         "popfl\n\t"                                                                           \
                         "popal\n\t"                                                                           \
                         "movl _hk_tramp+" #n "*4, %eax\n\t"                                                   \
                         "jmp *%eax\n\t");                                                                     \
    }
HK_STUB(0)
HK_STUB(1)
HK_STUB(2)
HK_STUB(3)

static void *const hk_stubs[HK_MAX] = {(void *)hk_stub_0, (void *)hk_stub_1, (void *)hk_stub_2,
                                       (void *)hk_stub_3};

static void hook_start(HMODULE exe) {
    char txt[128];
    const DWORD n = GetEnvironmentVariableA("RG_COMPAT_HOOK", txt, sizeof txt);
    if (n == 0 || n >= sizeof txt) return;
    int idx = 0;
    for (char *p = txt; *p && idx < HK_MAX;) {
        const DWORD rva = (DWORD)strtoul(p, &p, 0);
        BYTE *doel = (BYTE *)exe + rva;
        BYTE *tramp = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!tramp) {
            d2_line("haak: geen geheugen voor +0x%lx", (unsigned long)rva);
            break;
        }
        DWORD oud = 0;
        if (!VirtualProtect(doel, 6, PAGE_EXECUTE_READWRITE, &oud)) {
            d2_line("haak: +0x%lx niet schrijfbaar", (unsigned long)rva);
            break;
        }
        memcpy(tramp, doel, 6);
        tramp[6] = 0xe9;
        *(int *)(tramp + 7) = (int)((doel + 6) - (tramp + 11));
        doel[0] = 0xe9;
        *(int *)(doel + 1) = (int)((BYTE *)hk_stubs[idx] - (doel + 5));
        doel[5] = 0x90;
        VirtualProtect(doel, 6, oud, &oud);
        hk_tramp[idx] = tramp;
        hk_rva[idx] = rva;
        d2_line("haak: +0x%lx -> stub %d (trampoline %p)", (unsigned long)rva, idx, tramp);
        ++idx;
        if (*p == ',') ++p;
    }
}

/* ------------------------------- 13. RG_COMPAT_CALLRES (aanroep plus uitkomst) */

/* Voor een aanroepsite waarvan de UITKOMST het antwoord is (bestaat het Virtools-object wel?), vervangt deze
 * fix de `call rel32` door een sprong naar een stubje dat eerst het echte doel aanroept met dezelfde stapel,
 * daarna het resultaat (en de eerste twee argumenten) opschrijft en met dat resultaat terugkeert.
 * RG_COMPAT_CALLRES=<rva>,<rva>,... (maximaal 4). */

#define CR_MAX 4
__attribute__((used)) static void *cr_doel[CR_MAX];
static DWORD cr_site[CR_MAX];

__attribute__((used)) static void cr_log(int idx, unsigned long resultaat, const DWORD *p) {
    if (idx < 0 || idx >= CR_MAX) return;
    static unsigned long telling[CR_MAX];
    const unsigned long n = ++telling[idx];
    if (n <= 60 || (n % 500) == 0) {
        char a1[64], a2[64];
        w7_arg(a1, sizeof a1, p[0], w7_base, w7_size);
        w7_arg(a2, sizeof a2, p[1], w7_base, w7_size);
        d2_line("callres[%d] +0x%lx -> %08lx arg %s | %s (nr %lu)", idx, (unsigned long)cr_site[idx], resultaat, a1,
                a2, n);
    }
}

#define CR_STUB(n)                                                                                             \
    __attribute__((naked, used)) static void cr_stub_##n(void) {                                               \
        __asm__ volatile("movl _cr_doel+" #n "*4, %eax\n\t"                                                    \
                         "call *%eax\n\t"                                                                     \
                         "pushal\n\t"                                                                         \
                         "leal 36(%esp), %edx\n\t"                                                            \
                         "push %edx\n\t"                                                                      \
                         "push %eax\n\t"                                                                      \
                         "push $" #n "\n\t"                                                                   \
                         "call _cr_log\n\t"                                                                   \
                         "add $12, %esp\n\t"                                                                  \
                         "popal\n\t"                                                                          \
                         "ret\n\t");                                                                          \
    }
CR_STUB(0)
CR_STUB(1)
CR_STUB(2)
CR_STUB(3)

static void *const cr_stubs[CR_MAX] = {(void *)cr_stub_0, (void *)cr_stub_1, (void *)cr_stub_2,
                                       (void *)cr_stub_3};

static void callres_start(HMODULE exe) {
    char txt[128];
    const DWORD n = GetEnvironmentVariableA("RG_COMPAT_CALLRES", txt, sizeof txt);
    if (n == 0 || n >= sizeof txt) return;
    int idx = 0;
    for (char *p = txt; *p && idx < CR_MAX;) {
        const DWORD rva = (DWORD)strtoul(p, &p, 0);
        BYTE *site = (BYTE *)exe + rva;
        if (site[0] != 0xe8) {
            d2_line("callres: +0x%lx is geen call (%02x)", (unsigned long)rva, site[0]);
            continue;
        }
        const long rel = (long)*(int *)(site + 1);
        cr_doel[idx] = (void *)(site + 5 + rel);
        cr_site[idx] = rva;
        DWORD oud = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oud)) {
            d2_line("callres: +0x%lx niet schrijfbaar", (unsigned long)rva);
            break;
        }
        const long nieuw = (long)((BYTE *)cr_stubs[idx] - (site + 5));
        site[0] = 0xe8;
        *(int *)(site + 1) = (int)nieuw;
        VirtualProtect(site, 5, oud, &oud);
        d2_line("callres: +0x%lx -> %p -> stub %d", (unsigned long)rva, cr_doel[idx], idx);
        ++idx;
        if (*p == ',') ++p;
    }
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
        dongle2_start(exe);
        rg_patch_start();
        w7_start();
        w7_calltrace_start();
        exitlog_start(exe);
        iatfix_start(exe);
        tableprobe_start(exe);
        hook_start(exe);
        callres_start(exe);
    }
    return TRUE;
}
