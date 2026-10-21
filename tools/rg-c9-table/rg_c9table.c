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
 *
 * BUILD: tools/rg-c9-table/build.sh (mingw-w64 i686 in ~/.cache/ravengun-mingw).
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char txt[1024];
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
            mod = GetModuleHandleA(spec);
            if (!mod) mod = LoadLibraryA(spec);
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

static int lookup_xp(DWORD v, const char **dll, const char **name, WORD *ordinal) {
    for (size_t i = 0; i < RG_XP_MAP_N; i++) {
        if (k_xp_map[i].xp == v) {
            *dll = k_xp_map[i].dll;
            *name = k_xp_map[i].name;
            *ordinal = k_xp_map[i].ordinal;
            return 1;
        }
    }
    return 0;
}

/* Translate one cell; returns 1 when the cell was written this round. */
static int fix_cell(BYTE *pool, DWORD *cell, int idx) {
    const DWORD v = *cell;
    const char *dll, *name;
    WORD ordinal;
    if (v == 0) return 0;
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
        log_line("slot[%d] XP %08lx %s!%s%s: no live address, left alone", idx, (unsigned long)v, dll, name,
                 name[0] ? "" : " (ordinal)");
        return 0;
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
    for (char *p = txt; *p;) {
        char *e = NULL;
        const DWORD va = (DWORD)strtoul(p, &e, 0);
        DWORD len = 0x100;
        if (e && *e == ',') len = (DWORD)strtoul(e + 1, &e, 0);
        dump_range(va, len);
        if (e && *e == ';') ++e;
        p = e ? e : p + strlen(p);
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
    g_cb_stub = (DWORD)(ULONG_PTR)(pool + (size_t)g_cells * C9_STUB_SIZE);
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
    CreateThread(NULL, 0, worker, NULL, 0, NULL);
}
