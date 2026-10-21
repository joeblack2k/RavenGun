/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * DK2WIN32.dll -- RavenGun's synthetic Dinkey Pro key for Pigs Might Fly (Sega/Shell/Shell.exe, 32-bit).
 *
 * Shell.exe imports seven ordinals of the closed Dinkey Pro runtime DK2WIN32.dll and asks them for the cabinet's security
 * key; without a key boot step 15 (0x4053f0) falls back to step 16, whose handler (0x405510) prints
 * "ERROR : No Security Key Found" and parks the shell in step 0. This DLL exports those seven ordinals and answers them
 * the way the shell's own code expects (all of it read from Shell.exe, addresses are the shell's file offsets == runtime
 * addresses):
 *
 *   0x415a60  security init: ord 4 (0x442500) != 0 -> the driver is there; then 0x415300 asks for a key and remembers
 *             the 16-bit handle it gets back in the word at 0x48b3e0. A zero handle prints "Unable to find security key."
 *   0x415300  ord 20 (0x4424e2) with (the 15-byte key request block at [0x48b3dc], 0) -> returns the key handle in ax.
 *   0x414df0  the check itself: ord 20 again with the same block; the answer has to be nonzero and equal to the handle
 *             from 0x415300 (compared with 0x48b3e0), otherwise the check fails.
 *   0x414fb0  ord 10 (0x4424fa) with (handle, block, 0x14, start, dst, len): the key's security memory (the shell treats
 *             it as 224 bytes, "Start address is beyond the bounds of the security data"). Byte 0x10 is the number of
 *             16-byte records (< 13), the first record starts at 0x11.
 *   0x414f40  the error reporter: ord 17 (0x4424f4) nonzero = nothing to report; otherwise ord 24 (buf, 0x100) and then
 *             ord 25 fill a text that is printed as "Security Error - %s".
 *
 * What step 15 needs from the key's own security memory (measured by emulating 0x4053f0 with unicorn, see the NoteEN):
 *   byte 0x10 = 1 record, at 0x11 a record of 16 bytes:
 *     [0..1]  word 3                     the record type the shell accepts (cmp word, 3)
 *     [2..4]  0x2a 0x2a 0x2a ('***')     the design marker the shell accepts
 *     [5]     'P' or 'C'                 design: 'P' -> GameSettings.ini DESIGN TYPE = PIG, 'C' -> COW (0x405dc0);
 *                                        picked through 0x412fd0/0x413030 under the name "cred"
 *     [15]    sum of [0..14] & 0xff      the record checksum (0x415170 over 15 bytes)
 * With that record the check returns 1 and step 15 answers step 17, whose handler (0x407180) opens
 * "<game dir>\<game>.exe" and starts the game.
 *
 * Nothing of the real key is used or needed: this is a synthetic key built from the format the shell itself checks.
 * The DLL is a plain drop-in next to Shell.exe (patches/PigsMightFly/dropin/, To = .); the shell imports it by ordinal,
 * so it is loaded by the normal loader -- no preload and no import patching.
 *
 * Environment (optional):
 *   RG_DK2_LOG     log file (Windows path; default rg-dk2.log next to the exe, "-" = no log)
 *   RG_DK2_DESIGN  'P' (default) or 'C': which design type the key reports
 *
 * BUILD: tools/rg-dk2win/build.sh (mingw-w64 cross compiler in ~/.cache/ravengun-mingw, i686).
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SEC_SIZE 0xE0
#define SEC_COUNT_AT 0x10
#define SEC_FIRST_RECORD 0x11
#define REC_LEN 16
#define REC_TYPE 3
#define REC_MARK 0x2a
#define KEY_HANDLE 1

static unsigned char g_sec[SEC_SIZE];
static HANDLE g_log;
static int g_log_tried;
static int g_ready;

static void log_line(const char *fmt, ...) {
    char line[512];
    va_list ap;
    int n;
    if (!g_log_tried) {
        char path[MAX_PATH];
        DWORD got = GetEnvironmentVariableA("RG_DK2_LOG", path, sizeof path);
        g_log_tried = 1;
        if (!(got == 1 && path[0] == '-')) {
            if (got == 0 || got >= sizeof path) {
                DWORD m = GetModuleFileNameA(NULL, path, sizeof path);
                char *slash = (m && m < sizeof path) ? strrchr(path, '\\') : NULL;
                if (!slash || (size_t)(slash + 1 - path) + sizeof "rg-dk2.log" > sizeof path) return;
                memcpy(slash + 1, "rg-dk2.log", sizeof "rg-dk2.log");
            }
            g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
            if (g_log == INVALID_HANDLE_VALUE) g_log = NULL;
        }
    }
    if (!g_log) return;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n > sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    DWORD written = 0;
    WriteFile(g_log, line, (DWORD)n, &written, NULL);
}

/* The key's security memory: one record, built from the format the shell checks (see the header). */
static void build_security_memory(void) {
    unsigned char *rec = g_sec + SEC_FIRST_RECORD;
    char design = 'P';
    char env[8];
    DWORD got = GetEnvironmentVariableA("RG_DK2_DESIGN", env, sizeof env);
    unsigned sum = 0;
    int i;

    if (got == 1 && (env[0] == 'P' || env[0] == 'C' || env[0] == 'p' || env[0] == 'c'))
        design = (char)(env[0] & ~0x20);

    memset(g_sec, 0, sizeof g_sec);
    g_sec[SEC_COUNT_AT] = 1;
    rec[0] = REC_TYPE & 0xff;
    rec[1] = REC_TYPE >> 8;
    rec[2] = rec[3] = rec[4] = REC_MARK;
    rec[5] = (unsigned char)design;
    for (i = 0; i < REC_LEN - 1; i++) sum += rec[i];
    rec[REC_LEN - 1] = (unsigned char)(sum & 0xff);
}

static void ensure_ready(void) {
    if (g_ready) return;
    build_security_memory();
    g_ready = 1;
    log_line("dk2: synthetic key ready (handle %d, record %02x %02x %02x%02x%02x %02x, checksum %02x)", KEY_HANDLE,
             g_sec[SEC_FIRST_RECORD], g_sec[SEC_FIRST_RECORD + 1], g_sec[SEC_FIRST_RECORD + 2],
             g_sec[SEC_FIRST_RECORD + 3], g_sec[SEC_FIRST_RECORD + 4], g_sec[SEC_FIRST_RECORD + 5],
             g_sec[SEC_FIRST_RECORD + REC_LEN - 1]);
}

/* ordinal 4 -- is the DK2 driver there? (0x415a9d: zero prints "DK2 driver not installed.") */
int __stdcall dk2_driver_present(void) {
    ensure_ready();
    log_line("dk2: ord 4 driver present -> 1");
    return 1;
}

/* ordinal 13 -- imported, never called by Shell.exe. */
int __stdcall dk2_unused(void) {
    ensure_ready();
    log_line("dk2: ord 13 (unused) -> 1");
    return 1;
}

/* ordinal 17 -- the error reporter (0x414f40) prints only when this is 0. */
int __stdcall dk2_error_state(void) {
    ensure_ready();
    return 1;
}

/* ordinal 20 -- find the key described by the request block; answers the 16-bit handle in ax. */
unsigned short __stdcall dk2_find_key(void *request, int flags) {
    ensure_ready();
    log_line("dk2: ord 20 find key (request %p, %d) -> %d", request, flags, KEY_HANDLE);
    return KEY_HANDLE;
}

/* ordinal 10 -- read the key's security memory. Arguments as 0x414fb0 passes them: handle, request block, page, start,
 * destination, length. */
int __stdcall dk2_read_memory(void *handle, void *request, int page, unsigned start, void *dst, int len) {
    ensure_ready();
    (void)request;
    if (!dst || len <= 0) {
        log_line("dk2: ord 10 read (start 0x%x, len %d) refused: no destination", start, len);
        return -1;
    }
    if (start >= SEC_SIZE) {
        log_line("dk2: ord 10 read (start 0x%x, len %d) refused: past the end", start, len);
        return -1;
    }
    if ((unsigned)len > SEC_SIZE - start) len = (int)(SEC_SIZE - start);
    memcpy(dst, g_sec + start, (size_t)len);
    log_line("dk2: ord 10 read (handle %p, page %d, start 0x%x, len %d) -> 0", handle, page, start, len);
    return 0;
}

/* ordinal 24 -- text for the error reporter (char *buf, int size). */
void *__stdcall dk2_error_text(void *buf, int size) {
    ensure_ready();
    log_line("dk2: ord 24 error text (buf %p, size %d)", buf, size);
    if (buf && size > 0) {
        const char *text = "no error";
        size_t n = strlen(text) + 1;
        if (n > (size_t)size) n = (size_t)size - 1;
        memcpy(buf, text, n);
        ((char *)buf)[n] = 0;
    }
    return buf;
}

/* ordinal 25 -- the reporter prints the pointer this returns as "%s". */
void *__stdcall dk2_error_text_ptr(void *text) {
    ensure_ready();
    return text;
}


/* ---------------------------------------------------------------------------------------------------------------
 * The launcher step (17) after the key: 0x407180 builds "<dir>\<name>.exe" and starts it with CreateProcessA, then goes
 * to step 19 (0x13) on success or step 21 (0x15) with "MAINTHREAD : Failed to launch game" on failure. Which of the two
 * shell checks fails is not visible from the host, so this DLL logs the CreateProcessA call of the shell and its result
 * (one line each) and forwards it unchanged. The import is patched in the main image's import table.
 * ------------------------------------------------------------------------------------------------------------- */

typedef BOOL(WINAPI *create_process_a_fn)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
                                          LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
static create_process_a_fn real_create_process;

static void *patch_import(HMODULE mod, const char *dll, const char *func, void *repl) {
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY dir;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; ++d) {
        IMAGE_THUNK_DATA *names, *iat;
        if (lstrcmpiA((const char *)(base + d->Name), dll) != 0) continue;
        names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            IMAGE_IMPORT_BY_NAME *ibn;
            DWORD old = 0;
            void *orig;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, func) != 0) continue;
            if (!VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old)) return NULL;
            orig = (void *)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)repl;
            VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old, &old);
            return orig;
        }
    }
    return NULL;
}

/* The shell does not wait for the game it starts; a watcher reports how long it lived and with which exit code, so a
 * hand-over says whether the game itself refused to start or was killed later. */
static DWORD WINAPI dk2_watch_child(LPVOID arg) {
    HANDLE proc = (HANDLE)arg;
    const DWORD start = GetTickCount();
    DWORD code = 0;
    if (WaitForSingleObject(proc, 60000) == WAIT_OBJECT_0) {
        GetExitCodeProcess(proc, &code);
        log_line("dk2: game process %lu exited with %lu after %lu ms", (unsigned long)GetProcessId(proc),
                 (unsigned long)code, (unsigned long)(GetTickCount() - start));
    } else {
        log_line("dk2: game process %lu still running after 60 s", (unsigned long)GetProcessId(proc));
    }
    CloseHandle(proc);
    return 0;
}

static BOOL WINAPI dk2_CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta,
                                      BOOL inherit, DWORD flags, LPVOID env, LPCSTR cwd, LPSTARTUPINFOA si,
                                      LPPROCESS_INFORMATION pi) {
    BOOL ok;
    DWORD err;
    log_line("dk2: CreateProcessA(\"%s\", \"%s\", cwd \"%s\", flags %#lx)", app ? app : "(null)", cmd ? cmd : "(null)",
             cwd ? cwd : "(null)", (unsigned long)flags);
    if (!real_create_process) real_create_process = (create_process_a_fn)(void *)GetProcAddress(
        GetModuleHandleA("kernel32.dll"), "CreateProcessA");
    if (!real_create_process) {
        log_line("dk2: CreateProcessA: no real function");
        return FALSE;
    }
    ok = real_create_process(app, cmd, pa, ta, inherit, flags, env, cwd, si, pi);
    err = ok ? 0 : GetLastError();
    log_line("dk2: CreateProcessA -> %s (pid %lu, error %lu)", ok ? "ok" : "FAILED",
             ok && pi ? (unsigned long)pi->dwProcessId : 0ul, (unsigned long)err);
    if (ok && pi && pi->hProcess) CreateThread(NULL, 0, dk2_watch_child, pi->hProcess, 0, NULL);
    return ok;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        /* Nothing but data here: the log is opened on the first call, outside the loader lock. */
        g_ready = 0;
        g_log = NULL;
        g_log_tried = 0;
        real_create_process = (create_process_a_fn)patch_import(GetModuleHandleA(NULL), "KERNEL32.dll",
                                                               "CreateProcessA", (void *)dk2_CreateProcessA);
    }
    return TRUE;
}
