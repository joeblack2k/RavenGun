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
    }
    return TRUE;
}
