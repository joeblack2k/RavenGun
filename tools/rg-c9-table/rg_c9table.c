/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_c9_table.c -- RavenGun's repair of Coopers's 9's runtime call table at 0x512000 (32-bit, inside game.exe). It is
 * compiled into the DINPUT8.dll drop-in of patches/Coopers9 (dinput8_proxy.c), because a Windows game gets exactly ONE
 * TP_PRELOAD_DLL and Coopers9 already uses that one for rg-compat.dll.
 *
 * MEASURED 2026-10-21 (rg-exit.log, RG_TABLE_DUMP=0x512000,256,300, ronde 1): game.exe is a flat dump of a *packed* exe.
 * Its import table is the packer's minimal one (15 descriptors, 55 imports: 40 KERNEL32 plus one each for SETUPAPI,
 * USER32, d3d9, DINPUT8, DSOUND, WINMM, GDI32, ADVAPI32, ole32, OLEAUT32, MSVCR71), but the game's own code does not use
 * it: it calls 989 sites in .text/.protect through a second table at 0x512000 in .data (`call [0x512xxx]`,
 * `jmp [0x512xxx]`, or a slot loaded into a register). The packer filled that table AT RUN TIME with the addresses those
 * APIs had on the machine the dump was taken from -- Windows XP: D3D9 at 0x4FD80000 (slot 232 = 0x4FD8AED0 =
 * Direct3DCreate9, the address every earlier crash jumped to), msvcr71 at 0x7C36xxxx-0x7C39xxxx, kernel32 at 0x7C80xxxx,
 * ole32 at 0x774Cxxxx, user32/gdi32 at 0x77E4xxxx, advapi32 at 0x77F4xxxx, plus 68 slots holding 0x0313xxxx and a few in
 * the heap at 0x02B1xxxx: the packer's own stubs, whose code lived in memory the dump does not contain.
 *
 * The XP addresses can be NAMED from the game itself: because the file is a flat image, each slot of the (small) import
 * table still holds the XP address of its function, and the descriptors carry the name. genmap.py turns that into
 * rg_c9_map.h (53 addresses). This DLL translates every table slot that has a known XP address into the live address of
 * the same API in this Wine process, and replaces the slots it cannot name with a logging stub, so the run keeps going
 * and the log shows which of those slots the game actually calls, with what arguments and from where. Those slots are
 * classified (api wrapper, HASP call, CRC/anti-debug check) from that log; a slot answered by a stub returns
 * RG_C9_RET=<idx:value;...> (default 0) and pops RG_C9_POP=<idx:nargs;...> stack arguments (default 0).
 *
 * The table is written by the game during its start-up, so the repair is a poll loop (RG_C9_POLL ms) that keeps the table
 * correct from the first pass on; a translated slot is idempotent (a live address matches no XP address) and an installed
 * stub is remembered per slot.
 *
 * Environment:
 *   RG_C9_LOG     log file (Windows path; default rg-c9-table.log next to the exe, "-" = no log)
 *   RG_C9_TABLE   table address (default 0x512000)
 *   RG_C9_CELLS   number of dwords to watch (default 256)
 *   RG_C9_POLL    poll interval in ms (default 2)
 *   RG_C9_API     per-slot API of this process, "idx:dll!function,idx:dll!ordinal" (wins over the map and the stub; ',' or
 *                 ';' between the entries -- the engine's drop-in Env parser refuses a ';' inside a value)
 *   RG_C9_DUMP    "<va>,<len>[;<va>,<len>]" memory to hexdump into the log on the first stub call
 *   RG_C9_RET     per-slot return value, "idx:value;idx:value" (default 0)
 *   RG_C9_POP     per-slot number of stack arguments to pop, "idx:nargs;..." (default 0)
 *   RG_C9_VEH / RG_C9_NULLRET / RG_C9_ZEROSTUB / RG_C9_CALLER / RG_C9_OBJECT  fault logging and safety nets (see below)
 *   RG_C9_PATCH   "<va>:<old hex>:<new hex>[,...]" byte patches, applied only when the old bytes match
 *   RG_C9_HASP    "name@va:first bytes hex,..." jumps planted on the linked HASP library (login, logout, read, decrypt);
 *                 RG_C9_HASP_MEM "fff0:<hex>" and RG_C9_HASP_DECRYPT "<32 hex>:<32 hex>,..." are its answers (c9_hasp.h)
 *   RG_C9_LPT_MAP the parallel-port board: which board-image bit drives which board bit (c9_lpt.h); the DLL exports Out32 and
 *                 Inp32 and LoadLibraryA("inpout32") (shim c9_LoadLibraryA) returns this module
 *
 * BUILD: tools/rg-c9-table/build.sh (mingw-w64 i686 in ~/.cache/ravengun-mingw).
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c9_hasp.h"
#include "c9_lpt.h"
#include "rg_c9_map.h"

#define C9_MAX_CELLS 512
#define C9_STUB_SIZE 32

static DWORD g_table;
static DWORD g_cells = 256;
static DWORD g_poll = 2;
static HANDLE g_log;
static int g_log_tried;
static int g_lock_ready;
static CRITICAL_SECTION g_lock;

static DWORD g_stub_slot[C9_MAX_CELLS];    /* value we wrote into that slot (live address or stub), 0 = none */
static int g_stub_done[C9_MAX_CELLS];
static unsigned long g_stub_calls[C9_MAX_CELLS];
static unsigned long g_resolved;
static unsigned long g_stubbed;
static unsigned long g_round;

static void log_line(const char *fmt, ...) {
    char line[512];
    va_list ap;
    int n;
    if (!g_log_tried) {
        char path[MAX_PATH];
        DWORD got = GetEnvironmentVariableA("RG_C9_LOG", path, sizeof path);
        g_log_tried = 1;
        if (!(got == 1 && path[0] == '-')) {
            if (got == 0 || got >= sizeof path) {
                DWORD m = GetModuleFileNameA(NULL, path, sizeof path);
                char *slash = (m && m < sizeof path) ? strrchr(path, '\\') : NULL;
                if (!slash || (size_t)(slash + 1 - path) + sizeof "rg-c9-table.log" > sizeof path) return;
                memcpy(slash + 1, "rg-c9-table.log", sizeof "rg-c9-table.log");
            }
            g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
            if (g_log == INVALID_HANDLE_VALUE) g_log = NULL;
        }
    }
    if (!g_log) return;
    if (g_lock_ready) EnterCriticalSection(&g_lock);
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n > 0) {
        DWORD written = 0;
        if ((size_t)n > sizeof line - 2) n = (int)sizeof line - 2;
        line[n++] = '\n';
        WriteFile(g_log, line, (DWORD)n, &written, NULL);
    }
    if (g_lock_ready) LeaveCriticalSection(&g_lock);
}

/* "idx:value;idx:value" -> value for idx (default 0) */
static unsigned long env_per_slot(const char *var, int idx) {
    char txt[512];
    const DWORD n = GetEnvironmentVariableA(var, txt, sizeof txt);
    if (n == 0 || n >= sizeof txt) return 0;
    for (char *p = txt; *p;) {
        const int i = (int)strtol(p, &p, 0);
        if (*p == ':') ++p;
        const unsigned long v = strtoul(p, &p, 0);
        if (i == idx) return v;
        if (*p == ';' || *p == ',') ++p;
    }
    return 0;
}

/* RG_C9_API=<idx:dll!function[;idx:dll!ordinal...]>: the game's own table wants an API at that slot that the XP map
 * cannot name (the packer's stub). Answering it with the real function of this process is what the call site expects;
 * the classification per slot comes from the code around the call site (RG_C9_DUMP on the first stub call). */
static void *api_per_slot(int idx) {
    char txt[8192];
    if (GetEnvironmentVariableA("RG_C9_API", txt, sizeof txt) == 0) return NULL;
    for (char *p = txt; *p;) {
        char *e = NULL;
        const int i = (int)strtol(p, &e, 0);
        if (!e || *e != ':') break;
        p = e + 1;
        char *sep = strpbrk(p, ";,");   /* the engine's Env parser refuses ';', so ',' is the separator in a profile */
        const size_t n = sep ? (size_t)(sep - p) : strlen(p);
        if (i == idx && n > 0 && n < 200) {
            char spec[200];
            HMODULE mod;
            void *fn;
            char *bang;
            memcpy(spec, p, n);
            spec[n] = 0;
            bang = strchr(spec, '!');
            if (!bang) return NULL;
            *bang = 0;
            /* "self" is this DLL as the loader already has it: naming our own shims by file name makes Wine load a
             * SECOND copy for the game's calls, and then the shims and the fault handler live in different instances
             * (measured: the single-step arm and the device handle were invisible to the VEH). */
            if (_stricmp(spec, "self") == 0) {
                HMODULE self = NULL;
                if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        (LPCSTR)(ULONG_PTR)&api_per_slot, &self))
                    return NULL;
                mod = self;
            } else {
                mod = GetModuleHandleA(spec);
                if (!mod) mod = LoadLibraryA(spec);
            }
            if (!mod) return NULL;
            if (bang[1] >= '0' && bang[1] <= '9')
                fn = GetProcAddress(mod, (LPCSTR)(ULONG_PTR)strtoul(bang + 1, NULL, 0));
            else
                fn = GetProcAddress(mod, bang + 1);
            return fn;
        }
        if (!sep) break;
        p = sep + 1;
    }
    return NULL;
}

static unsigned long g_cb_stub;

static unsigned long slot_value(int idx) {
    char txt[128];
    if (GetEnvironmentVariableA("RG_C9_CBRET", txt, sizeof txt) > 0) {
        for (char *p = txt; *p;) {
            char *e = NULL;
            const long v = strtol(p, &e, 0);
            if (v == idx) return g_cb_stub;
            if (!e || (*e != ',' && *e != ';')) break;
            p = e + 1;
        }
    }
    return env_per_slot("RG_C9_RET", idx);
}

static unsigned long slot_args(int idx) {
    const unsigned long n = env_per_slot("RG_C9_POP", idx);
    return n > 16 ? 16 : n;
}

/* The stub of a slot: save everything, ask C what to answer, put that in the saved EAX, restore, and return, popping the
 * slot's arguments (stdcall) so the caller's stack does not grow. Called from the game exactly like the API it stands in
 * for: [esp] = return address, [esp+4...] = the arguments. */
static void dump_memory(void);
static void dump_range(DWORD va, DWORD len);

/* The last few slot calls, so the crash handler can say which stub the game was in when it died: a wrong argument pop
 * shows up as a fault right after (or inside) the last one. */
#define C9_TRACE_N 16
static struct { int idx; unsigned long caller; unsigned long esp; unsigned long ebp; unsigned long arg0, arg1;
                unsigned long saved_ptr, arg_ptr, saved_val; } g_trace[C9_TRACE_N];
static volatile long g_trace_i;

/* The hardlock/HASP device driver. 0x4A4D30 of the game opens a device file (\\.\FEnteDev on NT, \\.\HARDLOCK.VXD on
 * Win9x, both at 0x54C42C/0x54C418), asks it DeviceIoControl 0x9C4024A8 with a 0x2000-byte buffer and turns the reply
 * into words: count = [buf], then per entry a 64-bit value whose low 16 bits go to [table + i*4] and bits 16..31 to
 * [table + i*4 + 2]. Wine has no such device, CreateFileA fails, the table stays empty, and the game then calls a NULL
 * pointer out of it. These three shims are named from the drop-in (RG_C9_API=76/88/54:...rg-c9-table.dll!c9_*): the
 * device gets a NUL handle and the IOCTL an empty table, every other file is passed straight through. */
static HANDLE g_hl = INVALID_HANDLE_VALUE;
static unsigned long g_hl_io;
static DWORD g_nullret;          /* xor eax,eax; ret -- the answer to a missing import */
static unsigned long g_skips;

static int hardlock_name(const char *p) {
    if (!p) return 0;
    for (; *p; ++p)
        if (strncmp(p, "FEnteDev", 8) == 0 || strncmp(p, "HARDLOCK", 8) == 0) return 1;
    return 0;
}

__declspec(dllexport) HANDLE WINAPI c9_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                                   DWORD disp, DWORD flags, HANDLE tmpl) {
    if (!hardlock_name(name)) {
        static int plain;
        HLOCAL h;
        (void)h;
        if (plain++ < 12) {
            HANDLE r = CreateFileA(name, access, share, sa, disp, flags, tmpl);
            log_line("CreateFileA(\"%s\") access %08lx -> %08lx", name ? name : "(null)", (unsigned long)access,
                     (unsigned long)r);
            return r;
        }
        return CreateFileA(name, access, share, sa, disp, flags, tmpl);
    }
    g_hl = CreateFileA("NUL", 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    log_line("hardlock: CreateFileA(%s) -> NUL handle %08lx", name, (unsigned long)g_hl);
    return g_hl;
}

__declspec(dllexport) BOOL WINAPI c9_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD insz, LPVOID out,
                                                    DWORD outsz, LPDWORD got, LPOVERLAPPED ov) {
    if (h != g_hl || g_hl == INVALID_HANDLE_VALUE) return DeviceIoControl(h, code, in, insz, out, outsz, got, ov);
    if (out && outsz) memset(out, 0, outsz);   /* count 0: an empty table */
    if (got) *got = outsz;
    if (++g_hl_io <= 8)
        log_line("hardlock: DeviceIoControl(%08lx, code %08lx, in %lu bytes at %08lx, out %lu bytes) -> empty table",
                 (unsigned long)h, (unsigned long)code, (unsigned long)insz, (unsigned long)(ULONG_PTR)in,
                 (unsigned long)outsz);
    return TRUE;
}

/* The packer keeps its own GetProcAddress in a heap slot, so every import the game resolves at run time goes through
 * it: logging the names shows what the packer hides and which lookup comes back NULL. */
__declspec(dllexport) FARPROC WINAPI c9_GetProcAddressA(HMODULE mod, LPCSTR name) {
    static int logged;
    FARPROC fn;
    if ((ULONG_PTR)name > 0x10000 && logged < 200) {
        ++logged;
        fn = GetProcAddress(mod, name);
        log_line("GetProcAddress(%08lx, \"%s\") -> %08lx", (unsigned long)(ULONG_PTR)mod, name, (unsigned long)(ULONG_PTR)fn);
        return fn;
    }
    return GetProcAddress(mod, name);
}

__declspec(dllexport) HLOCAL WINAPI c9_LocalAlloc(UINT flags, SIZE_T bytes) {
    static int logged;
    HLOCAL r = LocalAlloc(flags, bytes);
    if (logged++ < 12) log_line("LocalAlloc(%08lx, %lu) -> %08lx", (unsigned long)flags, (unsigned long)bytes,
                                (unsigned long)(ULONG_PTR)r);
    return r;
}

/* RG_C9_STEP=<n>: single-step the game for n instructions as soon as c9_GetModuleHandleA is called, logging every
 * EIP. The path into a call of a NULL pointer is otherwise invisible: the crash says "eip 0" and nothing else. */
static volatile long g_step_left;
static void step_arm(long n) {
    CONTEXT c;
    g_step_left = n;
    c.ContextFlags = CONTEXT_CONTROL;
    if (GetThreadContext(GetCurrentThread(), &c)) {
        c.EFlags |= 0x100;                       /* TF */
        SetThreadContext(GetCurrentThread(), &c);
        log_line("stepping from %08lx for %ld instructions", (unsigned long)c.Eip, n);
    }
}

__declspec(dllexport) HMODULE WINAPI c9_GetModuleHandleA(LPCSTR name) {
    static int stepped;
    char txt[32];
    HMODULE h = GetModuleHandleA(name);
    log_line("GetModuleHandleA(\"%s\") -> %08lx", name ? name : "(null)", (unsigned long)(ULONG_PTR)h);
    if (!stepped++ && GetEnvironmentVariableA("RG_C9_STEP", txt, sizeof txt) > 0) step_arm(strtol(txt, NULL, 0));
    return h;
}

/* msvcr71!fopen (cdecl), logged: the game's data loader (0x406F70) shows "DISK READ ERROR (11)" when one of its files does
 * not open, and the name is only on the stack. */
__declspec(dllexport) void *__cdecl c9_fopen(const char *name, const char *mode) {
    typedef void *(__cdecl * fopen_t)(const char *, const char *);
    static fopen_t real;
    static int n;
    void *f;
    if (!real) {
        HMODULE m = GetModuleHandleA("msvcr71.dll");
        if (!m) m = LoadLibraryA("msvcr71.dll");
        real = m ? (fopen_t)(void *)GetProcAddress(m, "fopen") : NULL;
        if (!real) return NULL;
    }
    f = real(name, mode);
    if (!f || n < 40) {
        ++n;
        log_line("fopen(\"%s\", \"%s\") -> %08lx", name ? name : "(null)", mode ? mode : "(null)",
                 (unsigned long)(ULONG_PTR)f);
    }
    return f;
}

__declspec(dllexport) BOOL WINAPI c9_CloseHandle(HANDLE h) {
    if (h == g_hl && g_hl != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        g_hl = INVALID_HANDLE_VALUE;
        return TRUE;
    }
    return CloseHandle(h);
}

__attribute__((used)) static void c9_on_call(int idx, unsigned long *saved_eax, const unsigned long *arg0) {
    if (idx < 0) {
        /* Not a slot but the callback stub (RG_C9_CBRET): the game called a pointer a slot handed it. */
        log_line("callback from %08lx: arg %08lx %08lx %08lx %08lx", arg0[-1], arg0[0], arg0[1], arg0[2], arg0[3]);
        *saved_eax = 0;
        return;
    }
    const unsigned long n = ++g_stub_calls[idx];
    const unsigned long caller = arg0[-1];
    static int dumped;
    const long t = InterlockedIncrement(&g_trace_i) - 1;
    g_trace[t % C9_TRACE_N].idx = idx;
    g_trace[t % C9_TRACE_N].caller = caller;
    g_trace[t % C9_TRACE_N].esp = (unsigned long)(ULONG_PTR)(arg0 - 1);
    g_trace[t % C9_TRACE_N].ebp = (unsigned long)(ULONG_PTR)__builtin_frame_address(0);
    g_trace[t % C9_TRACE_N].arg0 = arg0[0];
    g_trace[t % C9_TRACE_N].arg1 = arg0[1];
    g_trace[t % C9_TRACE_N].saved_ptr = (unsigned long)(ULONG_PTR)saved_eax;
    g_trace[t % C9_TRACE_N].arg_ptr = (unsigned long)(ULONG_PTR)arg0;
    g_trace[t % C9_TRACE_N].saved_val = *saved_eax;
    if (!dumped) {
        /* The packer writes its trampolines at run time: dump the code around the caller once a slot is used, so the
         * argument order and the way the result is used can be read from the log. */
        dumped = 1;
        dump_memory();
    }
    if (n == 1) {
        /* RG_C9_OBJECT=<va>[,<len>]: dump an object and, when its first dword is a pointer, the first dwords of the
         * vtable it points at. The game takes code pointers out of its own objects, so this is where a hole shows. */
        char txt[64];
        if (GetEnvironmentVariableA("RG_C9_OBJECT", txt, sizeof txt) > 0) {
            char *e = NULL;
            const DWORD va = (DWORD)strtoul(txt, &e, 0);
            DWORD len = 0x60;
            if (e && *e == ',') len = (DWORD)strtoul(e + 1, NULL, 0);
            log_line("object at %08lx on slot[%d]:", (unsigned long)va, idx);
            dump_range(va, len);
            {
                const DWORD vt = *(const DWORD *)(ULONG_PTR)va;
                if (vt > 0x10000 && vt < 0x80000000ul) {
                    log_line("vtable %08lx of object %08lx:", (unsigned long)vt, (unsigned long)va);
                    dump_range(vt, 0x40);
                }
            }
        }
    }
    if (n == 1 && GetEnvironmentVariableA("RG_C9_CALLER", NULL, 0)) {
        /* The first call of a slot: the code just before the return address is the call site, i.e. what the game
         * expects of this instant. Dumped only on request (RG_C9_CALLER=1) to keep the log small. */
        dump_range(caller - 0x60, 0x60 + 0x30);
    }
    *saved_eax = slot_value(idx);
    if (n <= 8) {
        log_line("slot[%d] called from %08lx: arg %08lx %08lx %08lx %08lx %08lx %08lx -> %08lx (nr %lu)", idx,
                 caller, arg0[0], arg0[1], arg0[2], arg0[3], arg0[4], arg0[5], *saved_eax, n);
    } else if ((n % 20000) == 0) {
        log_line("slot[%d] called %lu times", idx, n);
    }
}

/* The slot calls that led to the fault, oldest first. */
static void log_trace(void) {
    const long total = g_trace_i;
    long first = total > C9_TRACE_N ? total - C9_TRACE_N : 0;
    for (long i = first; i < total; ++i) {
        const int k = (int)(i % C9_TRACE_N);
        log_line("trace %ld: slot[%d] from %08lx, caller esp %08lx ebp %08lx, arg %08lx %08lx, saved %08lx, argp %08lx, saved was %08lx", i, g_trace[k].idx,
                 g_trace[k].caller, g_trace[k].esp, g_trace[k].ebp, g_trace[k].arg0, g_trace[k].arg1,
                 g_trace[k].saved_ptr, g_trace[k].arg_ptr, g_trace[k].saved_val);
    }
}

__attribute__((naked, used)) static void c9_common(void) {
    /* pushal pushed EAX first, so the saved EAX sits at the highest address of the 32-byte block: at entry
     * esp points at the return address of the stub's `call c9_common`, so the block is [esp+12 .. esp+43] and
     * the saved EAX is at [esp+40]. (This was [esp+12], the saved EDI slot, so a stub never set its return value
     * and the caller kept its own EAX -- measured 2026-10-21 18:17: slot 198 returned 0x0001006A and the game
     * called it.) */
    __asm__ volatile("leal 40(%esp), %ecx\n\t"      /* saved EAX of pushal */
                     "leal 48(%esp), %edx\n\t"      /* first argument */
                     "movl 4(%esp), %eax\n\t"       /* index */
                     "push %edx\n\t"
                     "push %ecx\n\t"
                     "push %eax\n\t"
                     "call _c9_on_call\n\t"
                     "add $12, %esp\n\t"
                     "ret $4\n\t");   /* also drop the stub's push idx; without this the stub's popfd/popal/ret
                                          read one dword too high and the stub returns to the value it meant to return */
}

static void build_stub(BYTE *b, int idx, unsigned long nargs) {
    /* pushal; pushfl; push idx; call c9_common; popfl; popal; ret n */
    DWORD rel;
    b[0] = 0x60;                                  /* pushal  */
    b[1] = 0x9c;                                  /* pushfl  */
    b[2] = 0x68;                                  /* push imm32 */
    *(DWORD *)(b + 3) = (DWORD)idx;
    b[7] = 0xE8;                                  /* call rel32 */
    rel = (DWORD)(ULONG_PTR)c9_common - (DWORD)(ULONG_PTR)(b + 12);
    *(DWORD *)(b + 8) = rel;
    b[12] = 0x9d;                                 /* popfl   */
    b[13] = 0x61;                                 /* popal   */
    b[14] = 0xC2;                                 /* ret imm16 */
    *(WORD *)(b + 15) = (WORD)(nargs * 4);
}

static int xp_cmp(const void *key, const void *row) {
    const unsigned long v = *(const unsigned long *)key, x = ((const struct rg_xp_map *)row)->xp;
    return v < x ? -1 : v > x;
}

/* The map is sorted by address (genmap.py) and holds every msvcr71 export of the dump's build, so bsearch it: the
 * worker looks up every cell on every poll. */
static int lookup_xp(DWORD v, const char **dll, const char **name, WORD *ordinal) {
    const unsigned long key = v;
    const struct rg_xp_map *m = bsearch(&key, k_xp_map, RG_XP_MAP_N, sizeof k_xp_map[0], xp_cmp);
    if (!m) return 0;
    *dll = m->dll;
    *name = m->name;
    *ordinal = m->ordinal;
    return 1;
}

/* Translate one cell; returns 1 when the cell was written this round. */
static int fix_cell(BYTE *pool, DWORD *cell, int idx) {
    const DWORD v = *cell;
    const char *dll, *name;
    WORD ordinal;
    /* A cell the packer would have filled at run time and that the game still calls: with 0 the call lands at EIP 0.
     * RG_C9_ZEROSTUB=1 answers such a cell with a logging stub like any other unknown, which both keeps the game alive
     * and names the hole (the call site shows which API it should have been). */
    if (v == 0 && GetEnvironmentVariableA("RG_C9_ZEROSTUB", NULL, 0) == 0) return 0;
    {
        void *named = api_per_slot(idx);
        if (named) {
            if (v == (DWORD)(ULONG_PTR)named) return 0;
            *cell = (DWORD)(ULONG_PTR)named;
            g_stub_slot[idx] = *cell;
            g_stub_done[idx] = 1;
            ++g_resolved;
            log_line("slot[%d] value %08lx -> named API %08lx (RG_C9_API)", idx, (unsigned long)v,
                     (unsigned long)(ULONG_PTR)named);
            return 1;
        }
    }
    if (lookup_xp(v, &dll, &name, &ordinal)) {
        HMODULE mod = GetModuleHandleA(dll);
        void *live = NULL;
        if (!mod) mod = LoadLibraryA(dll);
        if (mod) live = GetProcAddress(mod, name[0] ? name : (LPCSTR)(ULONG_PTR)ordinal);
        if (live) {
            *cell = (DWORD)(ULONG_PTR)live;
            /* Remember what we wrote: a live address is no XP address, so the next pass would stub it otherwise. */
            g_stub_slot[idx] = *cell;
            g_stub_done[idx] = 1;
            ++g_resolved;
            log_line("slot[%d] XP %08lx %s!%s%s -> live %08lx", idx, (unsigned long)v, dll, name,
                     name[0] ? "" : " (ordinal)", (unsigned long)(ULONG_PTR)live);
            return 1;
        }
        /* Named but not exported by this Wine: a logging stub (below) says when the game really calls it. */
        log_line("slot[%d] XP %08lx %s!%s%s: no live address, stubbed", idx, (unsigned long)v, dll, name,
                 name[0] ? "" : " (ordinal)");
    }
    if (g_stub_done[idx] && v == g_stub_slot[idx]) return 0;
    if (g_stub_done[idx] && v != g_stub_slot[idx]) {
        /* The game wrote the slot again: forget our own value and look at the new one. */
        g_stub_done[idx] = 0;
        g_stub_slot[idx] = 0;
    }
    {
        BYTE *stub = pool + (size_t)idx * C9_STUB_SIZE;
        build_stub(stub, idx, slot_args(idx));
        *cell = (DWORD)(ULONG_PTR)stub;
        g_stub_slot[idx] = *cell;
        g_stub_done[idx] = 1;
        ++g_stubbed;
        log_line("slot[%d] unknown %08lx -> logging stub (returns %08lx, pops %lu)", idx, (unsigned long)v,
                 slot_value(idx), slot_args(idx));
        return 1;
    }
}

static void dump_range(DWORD va, DWORD len) {
    if (len > 0x800) len = 0x800;
    log_line("dump %08lx, %lu bytes:", (unsigned long)va, (unsigned long)len);
    for (DWORD off = 0; off < len; off += 16) {
        char line[128];
        int k = 0;
        for (DWORD i = 0; i < 16 && off + i < len; ++i)
            k += snprintf(line + k, sizeof line - (size_t)k, "%02x ", ((const unsigned char *)(ULONG_PTR)va)[off + i]);
        for (DWORD i = 0; i < 16 && off + i < len; ++i) {
            const unsigned char c = ((const unsigned char *)(ULONG_PTR)va)[off + i];
            line[k++] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        line[k] = 0;
        log_line("dump %08lx: %s", (unsigned long)(va + off), line);
    }
}

/* RG_C9_DUMP=<va>,<len>[;<va>,<len>...]: hexdump of the process' memory into the log (the packer writes its trampolines
 * at run time, so the code that calls a slot is only readable in the running process). */
static void dump_memory(void) {
    char txt[512];
    if (GetEnvironmentVariableA("RG_C9_DUMP", txt, sizeof txt) == 0) return;
    /* pairs: "va,len[,va,len...]" (';' also works); the engine's Env parser only accepts ',' inside a value */
    for (char *p = txt; *p;) {
        char *e = NULL;
        const DWORD va = (DWORD)strtoul(p, &e, 0);
        DWORD len = 0x100;
        if (e && *e == ',') len = (DWORD)strtoul(e + 1, &e, 0);
        dump_range(va, len);
        while (e && (*e == ',' || *e == ';')) ++e;
        p = e ? e : p + strlen(p);
    }
}

/* RG_C9_POKE=<va>:<value>[,<va>:<value>...]: write into the game's own memory just before its code runs. Coopers9 needs
 * one: when the packer's own device-enumeration step has not run, the global at 0x54B560 ("the dongle was found") stays
 * 0, the driver path at 0x4A4D30 then never picks a device name, CreateFileA gets NULL and the table it builds stays
 * empty -- and the game later calls a NULL pointer out of it. Poking the flag makes the game take its own dongle route,
 * where our c9_CreateFileA/c9_DeviceIoControl answer the device. */
static void poke_memory(void) {
    char txt[512];
    if (GetEnvironmentVariableA("RG_C9_POKE", txt, sizeof txt) == 0) return;
    for (char *p = txt; *p;) {
        char *e = NULL;
        const DWORD va = (DWORD)strtoul(p, &e, 0);
        DWORD val = 0;
        if (e && *e == ':') val = (DWORD)strtoul(e + 1, &e, 0);
        {
            DWORD old = 0;
            if (VirtualProtect((void *)(ULONG_PTR)(va & ~0xFFFu), 0x1000, PAGE_READWRITE, &old)) {
                *(volatile DWORD *)(ULONG_PTR)va = val;
                VirtualProtect((void *)(ULONG_PTR)(va & ~0xFFFu), 0x1000, old, &old);
                log_line("poke %08lx = %08lx", (unsigned long)va, (unsigned long)val);
            } else {
                log_line("poke %08lx = %08lx FAILED", (unsigned long)va, (unsigned long)val);
            }
        }
        if (e && (*e == ',' || *e == ';')) ++e;
        p = e ? e : p + strlen(p);
    }
}

/* The HASP HL library is linked into game.exe; its device path (SetupDi enumeration, \\.\FEnteDev IOCTLs, an obfuscated
 * version probe) has nothing to talk to under Wine. The game itself only uses four of its functions, so those four are
 * answered here, at the level of the library's own API, from c9_hasp.c:
 *
 *   RG_C9_HASP=<name>@<va>:<first bytes hex>,...   name = login | logout | read | decrypt; the entry gets a jmp to the
 *                                                 matching function below, but only when its first bytes are the given
 *                                                 ones (another build of the exe is left alone and logged)
 *   RG_C9_HASP_MEM=<fileid hex>:<image hex>       the dongle memory the game reads (synthetic, from its own checks)
 *   RG_C9_HASP_DECRYPT=<32 hex>:<32 hex>[,...]    decrypt answers (synthetic, from its own check)
 */
static c9_hasp_t g_hasp;

static void hex_line(const unsigned char *b, unsigned n, char *out, size_t outsz) {
    size_t k = 0;
    for (unsigned i = 0; i < n && k + 3 < outsz; ++i) k += (size_t)snprintf(out + k, outsz - k, "%02x", b[i]);
    out[k] = 0;
}

static unsigned __stdcall c9_hasp_login_entry(unsigned feature, const void *vendor_code, unsigned *handle) {
    const unsigned st = c9_hasp_login(&g_hasp, feature, handle);
    static int n;
    (void)vendor_code;
    if (n++ < 16) log_line("hasp_login(feature %08x) -> %u", feature, st);
    return st;
}

static unsigned __stdcall c9_hasp_logout_entry(unsigned handle) {
    return c9_hasp_logout(&g_hasp, handle);
}

static unsigned __stdcall c9_hasp_read_entry(unsigned handle, unsigned fileid, unsigned offset, unsigned length,
                                             unsigned char *buf) {
    const unsigned st = c9_hasp_read(&g_hasp, handle, fileid, offset, length, buf);
    static int n;
    if (n++ < 16) {
        char hex[80];
        hex_line(buf, st == C9_HASP_OK ? (length < 32 ? length : 32) : 0, hex, sizeof hex);
        log_line("hasp_read(file %04x, offset %u, %u bytes) -> %u %s", fileid, offset, length, st, hex);
    }
    return st;
}

static unsigned __stdcall c9_hasp_decrypt_entry(unsigned handle, unsigned char *buf, unsigned length) {
    char before[40], after[40];
    int hit = 0;
    unsigned st;
    static int n;
    hex_line(buf, length < 16 ? length : 16, before, sizeof before);
    st = c9_hasp_decrypt(&g_hasp, handle, buf, length, &hit);
    hex_line(buf, length < 16 ? length : 16, after, sizeof after);
    if (n++ < 16) log_line("hasp_decrypt(%u bytes %s) -> %u %s%s", length, before, st, after, hit ? "" : " (no answer)");
    return st;
}

static void hasp_install(void) {
    char txt[512];
    {
        static char mem[2 * C9_HASP_MEM_MAX + 16];
        const DWORD n = GetEnvironmentVariableA("RG_C9_HASP_MEM", mem, sizeof mem);
        if (n > 0 && n < sizeof mem) {
            if (c9_hasp_set_memory(&g_hasp, mem) == 0)
                log_line("hasp: memory file %04x, %u bytes", g_hasp.fileid, g_hasp.mem_len);
            else
                log_line("hasp: RG_C9_HASP_MEM not understood");
        }
    }
    {
        static char dec[C9_HASP_PAIRS * 70 + 8];
        const DWORD n = GetEnvironmentVariableA("RG_C9_HASP_DECRYPT", dec, sizeof dec);
        if (n > 0 && n < sizeof dec) log_line("hasp: %d decrypt answer(s)", c9_hasp_set_decrypt(&g_hasp, dec));
    }
    if (GetEnvironmentVariableA("RG_C9_HASP", txt, sizeof txt) == 0) return;
    for (char *p = txt; *p;) {
        char *sep = strchr(p, ',');
        char *at = strchr(p, '@');
        if (sep) *sep = 0;
        if (at) {
            char *colon = NULL;
            const DWORD va = (DWORD)strtoul(at + 1, &colon, 0);
            unsigned char want[8];
            unsigned nwant = 0;
            void *fn = NULL;
            *at = 0;
            if (_stricmp(p, "login") == 0) fn = (void *)c9_hasp_login_entry;
            else if (_stricmp(p, "logout") == 0) fn = (void *)c9_hasp_logout_entry;
            else if (_stricmp(p, "read") == 0) fn = (void *)c9_hasp_read_entry;
            else if (_stricmp(p, "decrypt") == 0) fn = (void *)c9_hasp_decrypt_entry;
            if (colon && *colon == ':')
                for (const char *h = colon + 1; h[0] && h[1] && nwant < sizeof want; h += 2) {
                    char two[3] = {h[0], h[1], 0};
                    want[nwant++] = (unsigned char)strtoul(two, NULL, 16);
                }
            if (!fn || !va) {
                log_line("hasp: entry '%s' not understood", p);
            } else if (nwant < 2 || memcmp((const void *)(ULONG_PTR)va, want, nwant) != 0) {
                log_line("hasp: %s at %08lx does not start with the expected bytes: left alone", p, (unsigned long)va);
            } else {
                DWORD old = 0;
                BYTE *code = (BYTE *)(ULONG_PTR)va;
                if (VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old)) {
                    code[0] = 0xE9;   /* jmp rel32: same arguments, same stdcall pop as the library function */
                    *(DWORD *)(code + 1) = (DWORD)(ULONG_PTR)fn - (va + 5);
                    VirtualProtect(code, 5, old, &old);
                    FlushInstructionCache(GetCurrentProcess(), code, 5);
                    log_line("hasp: %s at %08lx answered by rg-c9-table", p, (unsigned long)va);
                }
            }
        }
        if (!sep) break;
        p = sep + 1;
    }
}

/* The gun/IO board on the parallel port (c9_lpt.h). The game LoadLibrary's "inpout32" and calls its Out32/Inp32; slot 57
 * (LoadLibraryA) is answered by c9_LoadLibraryA, which hands back this DLL for that name, so GetProcAddress finds the
 * Out32/Inp32 exported below. The board answers from RavenGun's board image (the mapping TeknoParrot_JvsState of
 * rg-jvsbridge.exe, layout WartranTroopers) through the table RG_C9_LPT_MAP.
 */
static c9_lpt_map g_lpt_map;
static c9_lpt_board g_lpt;
static CRITICAL_SECTION g_lpt_lock;
static int g_lpt_ready;
static const volatile BYTE *g_image;
static DWORD g_image_retry;
static c9_lpt_image g_last_image;

static void lpt_setup(void) {
    char txt[1024], err[96];
    InitializeCriticalSection(&g_lpt_lock);
    g_lpt_ready = 1;
    if (GetEnvironmentVariableA("RG_C9_LPT_MAP", txt, sizeof txt) > 0) {
        if (c9_lpt_read_map(txt, &g_lpt_map, err, sizeof err) == 0) log_line("lpt: map read");
        else log_line("lpt: RG_C9_LPT_MAP refused: %s", err);
    }
}

static c9_lpt_image lpt_image(void) {
    c9_lpt_image im;
    memset(&im, 0, sizeof im);
    if (!g_image && GetTickCount() - g_image_retry < 0x80000000u) {
        HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        g_image_retry = GetTickCount() + 500;
        if (m) {
            g_image = (const volatile BYTE *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 64);
            log_line("lpt: board image TeknoParrot_JvsState %s", g_image ? "opened" : "not mapped");
        }
    }
    if (g_image) {
        memcpy(&im.bits, (const void *)(g_image + 8), 4);
        memcpy(im.axis, (const void *)(g_image + 12), 8);
    } else {
        /* No board image: everything off and both aims off-screen. */
        memset(im.axis, 0, sizeof im.axis);
    }
    return im;
}

__declspec(dllexport) void __stdcall Out32(short port, short value) {
    static unsigned long n;
    static BYTE logged_outputs = 0xFF;
    if (!g_lpt_ready) return;
    EnterCriticalSection(&g_lpt_lock);
    {
        const BYTE before = g_lpt.ctrl;
        c9_lpt_image im = g_last_image;
        if ((unsigned short)port == 0x37A && (value & 1) && !(before & 1)) {
            static uint32_t logged_bits = 0xFFFFFFFFu;
            static int changes;
            im = g_last_image = lpt_image();
            if (im.bits != logged_bits && changes < 400) {
                /* every change of the board image's buttons, with what the board answers for them: the measurement of
                 * which image bit is which game input */
                ++changes;
                logged_bits = im.bits;
                log_line("lpt: image bits %08lx axes %u,%u %u,%u -> c5 %02x c7 %02x", (unsigned long)im.bits, im.axis[0],
                         im.axis[1], im.axis[2], im.axis[3], c9_lpt_reply(&g_lpt_map, &im, 5),
                         c9_lpt_reply(&g_lpt_map, &im, 7));
            }
        }
        {
            const unsigned long reads = g_lpt.commands[1];
            static int shots;
            c9_lpt_out(&g_lpt, &g_lpt_map, &im, (unsigned short)port, (BYTE)value);
            if (g_lpt.commands[1] != reads && shots < 400) {
                /* a gun read with positions (command 1 = player 1 x): the shot the game sees, raw */
                ++shots;
                log_line("lpt: gun read %lu: p1 %u,%u p2 %u,%u gun bits %02x", g_lpt.commands[1], c9_lpt_reply(&g_lpt_map, &im, 1),
                         c9_lpt_reply(&g_lpt_map, &im, 2), c9_lpt_reply(&g_lpt_map, &im, 3),
                         c9_lpt_reply(&g_lpt_map, &im, 4), c9_lpt_reply(&g_lpt_map, &im, 5));
            }
        }
        if (++n == 1) log_line("lpt: first Out32(%04x, %02x)", (unsigned short)port, (BYTE)value);
        if (g_lpt.outputs != logged_outputs && g_lpt.output_writes) {
            logged_outputs = g_lpt.outputs;
            log_line("lpt: outputs %x (output command %lu)", g_lpt.outputs, g_lpt.output_writes);
        }
        if ((n % 200000) == 0) {
            const c9_lpt_image li = g_last_image;
            log_line("lpt: %lu writes, cmds 1:%lu 5:%lu 6:%lu 7:%lu c:%lu, image bits %08lx axes %u,%u %u,%u -> c5 %02x c7 %02x",
                     n, g_lpt.commands[1], g_lpt.commands[5], g_lpt.commands[6], g_lpt.commands[7], g_lpt.commands[12],
                     (unsigned long)li.bits, li.axis[0], li.axis[1], li.axis[2], li.axis[3],
                     c9_lpt_reply(&g_lpt_map, &li, 5), c9_lpt_reply(&g_lpt_map, &li, 7));
        }
    }
    LeaveCriticalSection(&g_lpt_lock);
}

__declspec(dllexport) short __stdcall Inp32(short port) {
    BYTE v;
    if (!g_lpt_ready) return 0;
    EnterCriticalSection(&g_lpt_lock);
    v = c9_lpt_in(&g_lpt, (unsigned short)port);
    LeaveCriticalSection(&g_lpt_lock);
    return (short)v;
}

__declspec(dllexport) HMODULE WINAPI c9_LoadLibraryA(LPCSTR name) {
    if (name && (_stricmp(name, "inpout32") == 0 || _stricmp(name, "inpout32.dll") == 0)) {
        HMODULE self = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(ULONG_PTR)&Out32, &self);
        log_line("LoadLibraryA(\"%s\") -> rg-c9-table %08lx (the parallel-port board)", name,
                 (unsigned long)(ULONG_PTR)self);
        return self;
    }
    {
        HMODULE h = LoadLibraryA(name);
        static int n;
        if (n++ < 24) log_line("LoadLibraryA(\"%s\") -> %08lx", name ? name : "(null)", (unsigned long)(ULONG_PTR)h);
        return h;
    }
}

/* RG_C9_PATCH=<va>:<old hex>:<new hex>[,...]: replace bytes of the image, only where they are the expected old bytes
 * (another build of the exe is left alone and logged). Coopers9: the data prefix "z:/data/bin/" at 0x5168D0 (the
 * cabinet's drive; every asset path is copied from it, 11 sites) becomes "data/bin/", relative to the game directory,
 * which is the working directory the game also writes its expdat/extdat into. */
static int hexbuf(const char *p, BYTE *out, size_t max, const char **end) {
    size_t n = 0;
    while (p[0] && p[1] && p[0] != ':' && p[0] != ',' && n < max) {
        char two[3] = {p[0], p[1], 0};
        char *e = NULL;
        out[n++] = (BYTE)strtoul(two, &e, 16);
        if (!e || *e) return -1;
        p += 2;
    }
    *end = p;
    return (int)n;
}

static void patch_memory(void) {
    char txt[1024];
    if (GetEnvironmentVariableA("RG_C9_PATCH", txt, sizeof txt) == 0) return;
    for (const char *p = txt; *p;) {
        char *e = NULL;
        const DWORD va = (DWORD)strtoul(p, &e, 0);
        BYTE oldb[64], newb[64];
        int no = -1, nn = -1;
        const char *q = e;
        if (q && *q == ':') no = hexbuf(q + 1, oldb, sizeof oldb, &q);
        if (no > 0 && *q == ':') nn = hexbuf(q + 1, newb, sizeof newb, &q);
        if (no <= 0 || nn != no) {
            log_line("patch: entry at \"%.24s\" not understood (old and new must be the same length)", p);
        } else if (memcmp((const void *)(ULONG_PTR)va, oldb, (size_t)no) != 0) {
            log_line("patch %08lx: the bytes there are not the expected ones, left alone", (unsigned long)va);
        } else {
            DWORD old = 0;
            if (VirtualProtect((void *)(ULONG_PTR)va, (SIZE_T)no, PAGE_EXECUTE_READWRITE, &old)) {
                memcpy((void *)(ULONG_PTR)va, newb, (size_t)no);
                VirtualProtect((void *)(ULONG_PTR)va, (SIZE_T)no, old, &old);
                log_line("patch %08lx: %d bytes", (unsigned long)va, no);
            }
        }
        while (*q && *q != ',') ++q;
        p = *q ? q + 1 : q;
    }
}

static unsigned long g_steps[64];
static unsigned long g_steps_i;
__attribute__((unused)) static void count_step(unsigned long eip) {
    g_steps[g_steps_i++ % 64] = eip;
    if ((g_steps_i % 32) == 0) {
        char line[512];
        int k = 0;
        const unsigned long n = g_steps_i < 64 ? g_steps_i : 64;
        for (unsigned long i = 0; i < n && k < (int)sizeof line - 12; ++i)
            k += snprintf(line + k, sizeof line - (size_t)k, " %08lx", g_steps[i]);
        line[k] = 0;
        log_line("steps:%s", line);
    }
}

static DWORD WINAPI worker(LPVOID arg) {
    BYTE *pool;
    DWORD *const table = (DWORD *)(ULONG_PTR)g_table;
    DWORD old = 0;
    unsigned long last_resolved = 0, last_stubbed = 0, stable = 0;
    (void)arg;
    pool = (BYTE *)VirtualAlloc(NULL, (size_t)(g_cells + 1) * C9_STUB_SIZE, MEM_COMMIT | MEM_RESERVE,
                                PAGE_EXECUTE_READWRITE);
    if (!pool) {
        log_line("geen geheugen voor de stubs");
        return 0;
    }
    build_stub(pool + (size_t)g_cells * C9_STUB_SIZE, -1, 0);
    g_nullret = (DWORD)(ULONG_PTR)(pool + (size_t)g_cells * C9_STUB_SIZE + 24);   /* 31 c0 c3: xor eax,eax; ret */
    ((BYTE *)(ULONG_PTR)g_nullret)[0] = 0x31;
    ((BYTE *)(ULONG_PTR)g_nullret)[1] = 0xC0;
    ((BYTE *)(ULONG_PTR)g_nullret)[2] = 0xC3;
    g_cb_stub = (DWORD)(ULONG_PTR)(pool + (size_t)g_cells * C9_STUB_SIZE);
    poke_memory();
    if (!VirtualProtect(table, (size_t)g_cells * 4, PAGE_READWRITE, &old)) {
        log_line("table %08lx not writable", (unsigned long)g_table);
        return 0;
    }
    log_line("table %08lx, %lu cells, poll %lu ms, %u XP addresses in the map", (unsigned long)g_table,
             (unsigned long)g_cells, (unsigned long)g_poll, (unsigned)RG_XP_MAP_N);
    for (;;) {
        int wrote = 0;
        ++g_round;
        for (DWORD i = 0; i < g_cells; ++i) wrote |= fix_cell(pool, &table[i], (int)i);
        if (g_round == 1) dump_memory();
        if (wrote || g_round == 1) {
            unsigned long seen = 0, zero = 0;
            for (DWORD i = 0; i < g_cells; ++i)
                if (table[i]) ++seen; else ++zero;
            log_line("round %lu: cells written, %lu seen / %lu zero, resolved %lu, stubbed %lu", (unsigned long)g_round,
                     seen, zero, g_resolved, g_stubbed);
        }
        if (g_resolved == last_resolved && g_stubbed == last_stubbed) {
            if (++stable > 1500) {
                log_line("done: resolved %lu, stubbed %lu, %lu cells seen", g_resolved, g_stubbed, (unsigned long)g_cells);
                return 0;
            }
        } else {
            stable = 0;
            last_resolved = g_resolved;
            last_stubbed = g_stubbed;
        }
        Sleep(g_poll);
    }
}

/* Report where the game dies: the first fault is logged with its address, the registers and the return chain on the
 * stack (words inside the image as RVA), so the next blocker is visible without the host being able to ptrace. */
static LONG WINAPI c9_veh(EXCEPTION_POINTERS *info) {
    static int seen;
    if (seen) return EXCEPTION_CONTINUE_SEARCH;
    seen = 1;
    if (info && info->ExceptionRecord) {
        const DWORD code = info->ExceptionRecord->ExceptionCode;
        const DWORD at = (DWORD)(ULONG_PTR)info->ExceptionRecord->ExceptionAddress;
        log_line("fault %08lx at %08lx: eip %08lx eax %08lx ebp %08lx esp %08lx", (unsigned long)code,
                 (unsigned long)at, (unsigned long)info->ContextRecord->Eip, (unsigned long)info->ContextRecord->Eax,
                 (unsigned long)info->ContextRecord->Ebp, (unsigned long)info->ContextRecord->Esp);
        log_line("fault regs: ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx",
                 (unsigned long)info->ContextRecord->Ebx, (unsigned long)info->ContextRecord->Ecx,
                 (unsigned long)info->ContextRecord->Edx, (unsigned long)info->ContextRecord->Esi,
                 (unsigned long)info->ContextRecord->Edi);
        if (code == 0x80000004ul && g_step_left > 0) {      /* single step while RG_C9_STEP is armed */
            log_line("step %08lx", (unsigned long)info->ContextRecord->Eip);
            info->ContextRecord->EFlags |= 0x100;      /* Wine drops TF on continue: set it again every time */
            if (--g_step_left == 0) {
                CONTEXT c = *info->ContextRecord;
                c.EFlags &= ~0x100u;
                *info->ContextRecord = c;
                log_line("stepping done");
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (code == 0xc0000005ul && g_nullret && (ULONG_PTR)info->ExceptionRecord->ExceptionAddress < 0x10000 &&
            GetEnvironmentVariableA("RG_C9_NULLRET", NULL, 0)) {
            /* The game called an import that its own (packer-rebuilt) table does not have: the pointer is small or
             * NULL. Answer it the way a missing import has to look -- return 0 -- and continue, instead of dying. */
            const DWORD eip = (DWORD)(ULONG_PTR)info->ExceptionRecord->ExceptionAddress;
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery((void *)(ULONG_PTR)eip, &mbi, sizeof mbi) != sizeof mbi || mbi.State != MEM_COMMIT) {
                if (g_skips < 200)
                    log_line("null call at %08lx from %08lx (skip %lu): answering 0", (unsigned long)eip,
                             (unsigned long)*(const DWORD *)(ULONG_PTR)info->ContextRecord->Esp, ++g_skips);
                info->ContextRecord->Eip = g_nullret;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        if (code == 0xc0000005ul) {
            const DWORD *sp = (const DWORD *)(ULONG_PTR)info->ContextRecord->Esp;
            char line[256];
            int k = 0;
            for (int i = 0; i < 48 && k < (int)sizeof line - 16; ++i) {
                const DWORD v = sp[i];
                if (v >= 0x400000 && v < 0x3000000)
                    k += snprintf(line + k, sizeof line - (size_t)k, " rva+%lx", (unsigned long)(v - 0x400000));
            }
            line[k] = 0;
            log_line("fault chain:%s", line);
            log_trace();
            /* The stack around esp: the words a shifted frame or a lost argument shows up in, plus the code at the
             * return address so the faulting instruction can be found without a ptrace. */
            {
                const DWORD esp = (DWORD)(ULONG_PTR)info->ContextRecord->Esp;
                dump_range(esp - 0x20, 0x80);
                if (sp[0] >= 0x400000 && sp[0] < 0x3000000)
                    dump_range(sp[0] - 0x20, 0x40);
            }
            {
                MEMORY_BASIC_INFORMATION mbi;
                if (VirtualQuery((void *)(ULONG_PTR)at, &mbi, sizeof mbi) == sizeof mbi)
                    log_line("fault at %08lx: state %08lx prot %08lx type %08lx base %08lx size %08lx",
                             (unsigned long)at, (unsigned long)mbi.State, (unsigned long)mbi.Protect,
                             (unsigned long)mbi.Type, (unsigned long)(ULONG_PTR)mbi.AllocationBase,
                             (unsigned long)mbi.RegionSize);
                else
                    log_line("fault at %08lx: no query", (unsigned long)at);
                dump_memory();
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Called from the DINPUT8.dll proxy's DllMain: set the knobs and start the repair thread. */
void rg_c9_table_start(void) {
    char txt[64];
    InitializeCriticalSection(&g_lock);
    g_lock_ready = 1;
    g_table = 0x512000;
    if (GetEnvironmentVariableA("RG_C9_TABLE", txt, sizeof txt) > 0) g_table = (DWORD)strtoul(txt, NULL, 0);
    if (GetEnvironmentVariableA("RG_C9_CELLS", txt, sizeof txt) > 0) g_cells = (DWORD)strtoul(txt, NULL, 0);
    if (GetEnvironmentVariableA("RG_C9_POLL", txt, sizeof txt) > 0) g_poll = (DWORD)strtoul(txt, NULL, 0);
    if (g_cells == 0 || g_cells > C9_MAX_CELLS) g_cells = C9_MAX_CELLS;
    if (g_poll < 1) g_poll = 1;
    if (!g_table) return;
    log_line("rg-c9-table: image %08lx, table %08lx, %lu cells, poll %lu ms, %u XP addresses in the map",
             (unsigned long)(ULONG_PTR)GetModuleHandleA(NULL), (unsigned long)g_table, (unsigned long)g_cells,
             (unsigned long)g_poll, (unsigned)RG_XP_MAP_N);
    if (GetEnvironmentVariableA("RG_C9_VEH", txt, sizeof txt) > 0 && txt[0] == '1') AddVectoredExceptionHandler(1, c9_veh);
    hasp_install();   /* before the game's code runs: DllMain of the preload DLL */
    patch_memory();
    lpt_setup();
    CreateThread(NULL, 0, worker, NULL, 0, NULL);
}
