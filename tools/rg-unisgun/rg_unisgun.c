/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-unisgun: RavenGun's own serial gun board for After Dark (UNIS 2012, Virtools GamePlayer.exe, 32-bit), in the game process.
 * On the cabinet the game talks to a gun I/O board on COM1 (19200 8N1); here this DLL answers the game from RavenGun's board
 * image, without a board, without a COM port and without TeknoParrot. Protocol and evidence: rg_unisgun_kern.h.
 *
 * How it gets into the process: rg-compat.dll (the drop-in's single Preload DLL) loads it with RG_COMPAT_LOAD=rg-unisgun.dll.
 * DllMain redirects the serial API in the import table of the main image (kernel32: CreateFileA, ReadFile, WriteFile,
 * CloseHandle, GetOverlappedResult, WaitCommEvent, ClearCommError, SetupComm, Get/SetCommState, SetCommTimeouts, SetCommMask,
 * EscapeCommFunction, PurgeComm). Only a handle this DLL handed out for "\\.\COMn" (n = RG_UNISGUN_PORT, default 1) is served
 * here; every other call goes on unchanged to whatever was in the import table before (Wine, or another hook such as rg-compat's).
 *
 *   - the board streams: a 26-byte report built from the board image at that moment every RG_UNISGUN_PERIOD_MS (default 14, about
 *     what 19200 baud carries) while the port is open. The game's I/O thread (0x40ffd0) only writes 50 90 0d when its event wait
 *     of 15 s times out (a signalled event ends that thread), so the board cannot wait for polls (measured 2026-10-21: 2 polls in
 *     20 s); each poll also queues one report;
 *   - WaitCommEvent/ClearCommError/ReadFile hand the queued bytes back synchronously (an OVERLAPPED is completed at once);
 *   - a thread reads the board image every 2 ms (the Windows mapping "TeknoParrot_JvsState" of rg-jvsbridge.exe in the prefix)
 *     and re-reads the calibration (coin.ini in the working directory: [Gun1xMin] 1xMin ...) every 2 s;
 *   - the button table is rg-unisgun.ini next to this DLL (a table with an error is refused as a whole: the defaults hold);
 *   - RG_UNISGUN_LOG=path writes a log (relative = next to this DLL), with a count line every 10 s;
 *   - RG_UNISGUN_PROBE=path (relative = next to this DLL): a text file with "index=hex" pairs (report bytes 2..23) that is OR-ed into
 *     every report while it exists, re-read every 300 ms. A measuring tool for unknown bits; never set in a shipped profile.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_unisgun_kern.h"

static HMODULE self;
static CRITICAL_SECTION lock;
static rgg_cfg cfg;
static rgg_image image;
static uint8_t probe[RGG_REPORT_LEN];
static int probe_on;
static const volatile uint8_t *map_view;

/* The one emulated port. handle is a private event (a real, unique kernel object); data signals queued bytes. */
static HANDLE port_handle;
static HANDLE data_event;
static DWORD comm_mask;
static uint8_t queue[4096];
static int queued;
static rgg_scanner scanner;
static rgg_scan_result scanned;
static long n_reads, n_bytes_out, n_opens, n_stream;
static DWORD period_ms = 14, next_report;
static uint8_t last_output_logged[RGG_OUTPUT_LEN];
static char port_name[32] = "\\\\.\\COM1";

static FILE *log_file;

static void path_next_to_dll(const char *name, char *out, size_t len)
{
    if (name[0] && (name[1] == ':' || name[0] == '\\' || name[0] == '/')) {
        snprintf(out, len, "%s", name);
        return;
    }
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(self, dir, MAX_PATH);
    char *slash = n ? strrchr(dir, '\\') : NULL;
    if (slash) slash[1] = 0;
    else dir[0] = 0;
    snprintf(out, len, "%s%s", dir, name);
}

static void logf_(const char *fmt, ...)
{
    char line[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (log_file) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(log_file, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
        fflush(log_file);
    } else {
        char d[640];
        snprintf(d, sizeof d, "rg-unisgun: %s\n", line);
        OutputDebugStringA(d);
    }
}

static void read_table(void)
{
    char path[MAX_PATH + 32];
    path_next_to_dll("rg-unisgun.ini", path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) { logf_("table: %s missing, defaults (coin bits only)", path); return; }
    static char text[16384];
    size_t len = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[len] = 0;
    char err[160];
    if (rgg_read_ini(text, &cfg, err, sizeof err) != 0) { logf_("table %s REFUSED (%s): defaults hold", path, err); return; }
    logf_("table: %s (offscreen %d)", path, cfg.offscreen);
}

/* coin.ini of the game (working directory), the same keys the game reads. */
static void read_cal(void)
{
    static int last[2][4] = {{-2}, {-2}};
    char path[MAX_PATH];
    DWORD n = GetCurrentDirectoryA(MAX_PATH - 16, path);
    if (!n) return;
    strcat(path, "\\coin.ini");
    for (int g = 0; g < 2; g++) {
        int v[4];
        static const char *const what[4] = {"xMin", "xMax", "yMin", "yMax"};
        for (int k = 0; k < 4; k++) {
            char sec[16], key[16];
            snprintf(sec, sizeof sec, "Gun%d%s", g + 1, what[k]);
            snprintf(key, sizeof key, "%d%s", g + 1, what[k]);
            v[k] = (int)GetPrivateProfileIntA(sec, key, -1, path);
        }
        if (!memcmp(v, last[g], sizeof v)) continue;
        memcpy(last[g], v, sizeof v);
        EnterCriticalSection(&lock);
        int ok = rgg_set_cal(&cfg, g, v[0], v[1], v[2], v[3]);
        LeaveCriticalSection(&lock);
        logf_("calibration gun %d from %s: x %d..%d y %d..%d%s", g + 1, path, v[0], v[1], v[2], v[3],
              ok == 0 ? "" : " (refused: the previous range holds)");
    }
}

static void read_probe(void)
{
    const char *env = getenv("RG_UNISGUN_PROBE");
    if (!env || !*env) return;
    char path[MAX_PATH + 32];
    path_next_to_dll(env, path, sizeof path);
    uint8_t p[RGG_REPORT_LEN] = {0};
    int on = 0;
    FILE *f = fopen(path, "rb");
    if (f) {
        int idx;
        unsigned val;
        while (fscanf(f, " %d = %x", &idx, &val) == 2)
            if (idx >= 2 && idx < 24) { p[idx] = (uint8_t)val; on = 1; }
        fclose(f);
    }
    EnterCriticalSection(&lock);
    if (memcmp(p, probe, sizeof p) || on != probe_on) {
        memcpy(probe, p, sizeof p);
        probe_on = on;
        logf_("probe %s: b2=%02x b3=%02x b4=%02x b5=%02x b6=%02x", on ? "ON" : "off", p[2], p[3], p[4], p[5], p[6]);
    }
    LeaveCriticalSection(&lock);
}

static const volatile uint8_t *open_image(void)
{
    HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
    if (!m) return NULL;
    const volatile uint8_t *v = (const volatile uint8_t *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 64);
    if (v) logf_("board image TeknoParrot_JvsState opened");
    return v;
}

static DWORD WINAPI loop(LPVOID arg)
{
    (void)arg;
    DWORD next_try = 0, next_count = GetTickCount() + 10000, next_cal = 0, next_probe = 0;
    for (;;) {
        DWORD now = GetTickCount();
        if (!map_view && now - next_try < 0x80000000u) {
            map_view = open_image();
            next_try = now + 500;
        }
        if (map_view) {
            rgg_image im;
            memcpy(&im.bits, (const void *)(map_view + 8), 4);
            memcpy(im.axis, (const void *)(map_view + 12), 8);
            EnterCriticalSection(&lock);
            image = im;
            LeaveCriticalSection(&lock);
        }
        if (now - next_cal < 0x80000000u) { read_cal(); next_cal = now + 2000; }
        if (now - next_probe < 0x80000000u) { read_probe(); next_probe = now + 300; }
        if (now - next_count < 0x80000000u) {
            next_count = now + 10000;
            EnterCriticalSection(&lock);
            rgg_image im = image;
            int polls = scanned.polls, outs = scanned.outputs;
            uint8_t a = rgg_buttons(&cfg, &im, 0), b = rgg_buttons(&cfg, &im, 1);
            int ax = rgg_raw(&cfg, &im, 0, 0), ay = rgg_raw(&cfg, &im, 0, 1), bx = rgg_raw(&cfg, &im, 1, 0), by = rgg_raw(&cfg, &im, 1, 1);
            LeaveCriticalSection(&lock);
            logf_("count: opens=%ld streamed=%ld polls=%d outputs=%d reads=%ld bytes=%ld bits=0x%08x axes=%u,%u,%u,%u -> A %02x %d,%d B %02x %d,%d%s",
                  n_opens, n_stream, polls, outs, n_reads, n_bytes_out, (unsigned)im.bits, im.axis[0], im.axis[1], im.axis[2], im.axis[3], a, ax,
                  ay, b, bx, by, map_view ? "" : " (NO board image)");
        }
        Sleep(2);
    }
    return 0;
}

/* ------------------------------------------------------------------ the port */

typedef HANDLE(WINAPI *createfilea_fn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL(WINAPI *readfile_fn)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *writefile_fn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *closehandle_fn)(HANDLE);
typedef BOOL(WINAPI *getovl_fn)(HANDLE, LPOVERLAPPED, LPDWORD, BOOL);
typedef BOOL(WINAPI *waitcomm_fn)(HANDLE, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *clearcomm_fn)(HANDLE, LPDWORD, LPCOMSTAT);
typedef BOOL(WINAPI *setupcomm_fn)(HANDLE, DWORD, DWORD);
typedef BOOL(WINAPI *commstate_fn)(HANDLE, LPDCB);
typedef BOOL(WINAPI *commtimeouts_fn)(HANDLE, LPCOMMTIMEOUTS);
typedef BOOL(WINAPI *commmask_fn)(HANDLE, DWORD);
typedef BOOL(WINAPI *escape_fn)(HANDLE, DWORD);
typedef BOOL(WINAPI *purge_fn)(HANDLE, DWORD);

static createfilea_fn real_createfilea;
static readfile_fn real_readfile;
static writefile_fn real_writefile;
static closehandle_fn real_closehandle;
static getovl_fn real_getovl;
static waitcomm_fn real_waitcomm;
static clearcomm_fn real_clearcomm;
static setupcomm_fn real_setupcomm;
static commstate_fn real_getcommstate, real_setcommstate;
static commtimeouts_fn real_settimeouts;
static commmask_fn real_setcommmask;
static escape_fn real_escape;
static purge_fn real_purge;

static int ours(HANDLE h) { return h && h != INVALID_HANDLE_VALUE && h == port_handle; }

static void complete(LPOVERLAPPED ov, DWORD n)
{
    if (!ov) return;
    ov->Internal = 0;
    ov->InternalHigh = n;
    if (ov->hEvent) SetEvent(ov->hEvent);
}

static HANDLE WINAPI my_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags,
                                    HANDLE tmpl)
{
    if (name && !lstrcmpiA(name, port_name)) {
        EnterCriticalSection(&lock);
        if (!port_handle) port_handle = CreateEventA(NULL, TRUE, FALSE, NULL);
        queued = 0;
        scanner.n = 0;
        ResetEvent(data_event);
        n_opens++;
        HANDLE h = port_handle;
        LeaveCriticalSection(&lock);
        logf_("CreateFileA(%s, flags=0x%08lx) -> emulated board, handle %p", name, (unsigned long)flags, (void *)h);
        SetLastError(ERROR_SUCCESS);
        return h;
    }
    return real_createfilea(name, access, share, sa, disp, flags, tmpl);
}

static BOOL WINAPI my_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED ov)
{
    if (!ours(h)) return real_writefile(h, buf, n, written, ov);
    EnterCriticalSection(&lock);
    int before = scanned.polls, outs_before = scanned.outputs;
    rgg_scan(&scanner, (const uint8_t *)buf, n, &scanned);
    for (int i = before; i < scanned.polls; i++) {
        if (queued + RGG_REPORT_LEN > (int)sizeof queue) queued = 0;   /* nobody reads: drop the backlog, keep the newest */
        rgg_report(&cfg, &image, probe_on ? probe : NULL, queue + queued);
        queued += RGG_REPORT_LEN;
    }
    if (queued) SetEvent(data_event);
    int new_output = scanned.outputs != outs_before && memcmp(scanned.last_output, last_output_logged, RGG_OUTPUT_LEN);
    uint8_t o[RGG_OUTPUT_LEN];
    memcpy(o, scanned.last_output, sizeof o);
    if (new_output) memcpy(last_output_logged, o, sizeof o);
    LeaveCriticalSection(&lock);
    if (new_output)
        logf_("output frame %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8], o[9]);
    if (written) *written = n;
    complete(ov, n);
    return TRUE;
}

static DWORD take(void *buf, DWORD n)
{
    EnterCriticalSection(&lock);
    DWORD k = (DWORD)queued < n ? (DWORD)queued : n;
    memcpy(buf, queue, k);
    memmove(queue, queue + k, (size_t)queued - k);
    queued -= (int)k;
    if (!queued) ResetEvent(data_event);
    n_reads++;
    n_bytes_out += (long)k;
    LeaveCriticalSection(&lock);
    return k;
}

static BOOL WINAPI my_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov)
{
    if (!ours(h)) return real_readfile(h, buf, n, got, ov);
    DWORD k = take(buf, n);
    if (got) *got = k;
    complete(ov, k);
    return TRUE;
}

static BOOL WINAPI my_GetOverlappedResult(HANDLE h, LPOVERLAPPED ov, LPDWORD n, BOOL wait)
{
    if (!ours(h)) return real_getovl(h, ov, n, wait);
    if (n) *n = ov ? (DWORD)ov->InternalHigh : 0;
    return TRUE;
}

/* The game calls WaitCommEvent(h, &mask, NULL) in its reader thread and then reads what ClearCommError says is there. With nothing
 * queued the next streamed report is built when its time comes (at most period_ms of waiting) and the answer is EV_RXCHAR. */
static BOOL WINAPI my_WaitCommEvent(HANDLE h, LPDWORD mask, LPOVERLAPPED ov)
{
    if (!ours(h)) return real_waitcomm(h, mask, ov);
    EnterCriticalSection(&lock);
    int empty = queued == 0;
    LeaveCriticalSection(&lock);
    if (empty) {
        DWORD wait = next_report - GetTickCount();
        if (wait < 0x80000000u && wait > 0) Sleep(wait > period_ms ? period_ms : wait);
        EnterCriticalSection(&lock);
        if (queued + RGG_REPORT_LEN > (int)sizeof queue) queued = 0;
        rgg_report(&cfg, &image, probe_on ? probe : NULL, queue + queued);
        queued += RGG_REPORT_LEN;
        n_stream++;
        next_report = GetTickCount() + period_ms;
        LeaveCriticalSection(&lock);
    }
    EnterCriticalSection(&lock);
    DWORD m = queued ? (EV_RXCHAR & (comm_mask ? comm_mask : EV_RXCHAR)) : 0;
    LeaveCriticalSection(&lock);
    if (mask) *mask = m;
    complete(ov, 0);
    return TRUE;
}

static BOOL WINAPI my_ClearCommError(HANDLE h, LPDWORD errors, LPCOMSTAT st)
{
    if (!ours(h)) return real_clearcomm(h, errors, st);
    if (errors) *errors = 0;
    if (st) {
        memset(st, 0, sizeof *st);
        EnterCriticalSection(&lock);
        st->cbInQue = (DWORD)queued;
        LeaveCriticalSection(&lock);
    }
    return TRUE;
}

static BOOL WINAPI my_SetupComm(HANDLE h, DWORD in, DWORD out)
{
    return ours(h) ? TRUE : real_setupcomm(h, in, out);
}

static BOOL WINAPI my_GetCommState(HANDLE h, LPDCB d)
{
    if (!ours(h)) return real_getcommstate(h, d);
    DWORD len = d->DCBlength;
    memset(d, 0, sizeof *d);
    d->DCBlength = len ? len : sizeof *d;
    d->BaudRate = 19200;
    d->fBinary = 1;
    d->ByteSize = 8;
    d->Parity = NOPARITY;
    d->StopBits = ONESTOPBIT;
    return TRUE;
}

static BOOL WINAPI my_SetCommState(HANDLE h, LPDCB d)
{
    if (!ours(h)) return real_setcommstate(h, d);
    logf_("SetCommState: %lu baud, %u bits, parity %u, stop %u", (unsigned long)d->BaudRate, d->ByteSize, d->Parity, d->StopBits);
    return TRUE;
}

static BOOL WINAPI my_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t)
{
    return ours(h) ? TRUE : real_settimeouts(h, t);
}

static BOOL WINAPI my_SetCommMask(HANDLE h, DWORD m)
{
    if (!ours(h)) return real_setcommmask(h, m);
    comm_mask = m;
    return TRUE;
}

static BOOL WINAPI my_EscapeCommFunction(HANDLE h, DWORD f)
{
    return ours(h) ? TRUE : real_escape(h, f);
}

static BOOL WINAPI my_PurgeComm(HANDLE h, DWORD f)
{
    if (!ours(h)) return real_purge(h, f);
    if (f & PURGE_RXCLEAR) {
        EnterCriticalSection(&lock);
        queued = 0;
        ResetEvent(data_event);
        LeaveCriticalSection(&lock);
    }
    return TRUE;
}

/* The handle stays valid (the same event) so a reader thread that is still inside WaitCommEvent never touches a closed object;
 * a new CreateFileA hands it out again with an empty queue. */
static BOOL WINAPI my_CloseHandle(HANDLE h)
{
    if (!ours(h)) return real_closehandle(h);
    logf_("CloseHandle(emulated board)");
    EnterCriticalSection(&lock);
    queued = 0;
    SetEvent(data_event);
    LeaveCriticalSection(&lock);
    return TRUE;
}

/* ------------------------------------------------------------------ import table */

static void *patch_import(HMODULE mod, const char *dll, const char *func, void *repl)
{
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
            void *old = (void *)iat->u1.Function;
            DWORD prot;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &prot)) return NULL;
            iat->u1.Function = (ULONG_PTR)repl;
            VirtualProtect(&iat->u1.Function, sizeof(void *), prot, &prot);
            return old;
        }
    }
    return NULL;
}

#define HOOK(name, var, type)                                                                        \
    do {                                                                                             \
        var = (type)patch_import(exe, "kernel32.dll", #name, (void *)my_##name);                     \
        if (!var) {                                                                                  \
            var = (type)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), #name);             \
            missing++;                                                                               \
        }                                                                                            \
    } while (0)

static void start(void)
{
    InitializeCriticalSection(&lock);
    data_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    const char *log_env = getenv("RG_UNISGUN_LOG");
    if (log_env && *log_env) {
        char path[MAX_PATH + 32];
        path_next_to_dll(log_env, path, sizeof path);
        log_file = fopen(path, "a");
    }
    const char *port = getenv("RG_UNISGUN_PORT");
    int n = port && *port ? atoi(port) : 1;
    if (n < 1 || n > 255) n = 1;
    snprintf(port_name, sizeof port_name, "\\\\.\\COM%d", n);
    const char *per = getenv("RG_UNISGUN_PERIOD_MS");
    if (per && atoi(per) >= 1 && atoi(per) <= 1000) period_ms = (DWORD)atoi(per);
    rgg_default(&cfg);
    read_table();
    HMODULE exe = GetModuleHandleA(NULL);
    int missing = 0;
    HOOK(CreateFileA, real_createfilea, createfilea_fn);
    HOOK(ReadFile, real_readfile, readfile_fn);
    HOOK(WriteFile, real_writefile, writefile_fn);
    HOOK(CloseHandle, real_closehandle, closehandle_fn);
    HOOK(GetOverlappedResult, real_getovl, getovl_fn);
    HOOK(WaitCommEvent, real_waitcomm, waitcomm_fn);
    HOOK(ClearCommError, real_clearcomm, clearcomm_fn);
    HOOK(SetupComm, real_setupcomm, setupcomm_fn);
    HOOK(GetCommState, real_getcommstate, commstate_fn);
    HOOK(SetCommState, real_setcommstate, commstate_fn);
    HOOK(SetCommTimeouts, real_settimeouts, commtimeouts_fn);
    HOOK(SetCommMask, real_setcommmask, commmask_fn);
    HOOK(EscapeCommFunction, real_escape, escape_fn);
    HOOK(PurgeComm, real_purge, purge_fn);
    logf_("loaded: port %s, report every %lu ms, %d of 14 kernel32 imports redirected", port_name, (unsigned long)period_ms, 14 - missing);
    HANDLE t = CreateThread(NULL, 0, loop, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self = inst;
        DisableThreadLibraryCalls(inst);
        start();
    }
    return TRUE;
}
