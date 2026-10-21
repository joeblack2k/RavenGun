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

static DWORD g_stub_slot[C9_MAX_CELLS];    /* stub address installed in that slot, 0 = none */
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

static unsigned long slot_value(int idx) {
    return env_per_slot("RG_C9_RET", idx);
}

static unsigned long slot_args(int idx) {
    const unsigned long n = env_per_slot("RG_C9_POP", idx);
    return n > 16 ? 16 : n;
}

/* The stub of a slot: save everything, ask C what to answer, put that in the saved EAX, restore, and return, popping the
 * slot's arguments (stdcall) so the caller's stack does not grow. Called from the game exactly like the API it stands in
 * for: [esp] = return address, [esp+4...] = the arguments. */
__attribute__((used)) static void c9_on_call(int idx, unsigned long *saved_eax, const unsigned long *arg0) {
    const unsigned long n = ++g_stub_calls[idx];
    const unsigned long caller = arg0[-1];
    *saved_eax = slot_value(idx);
    if (n <= 8) {
        log_line("slot[%d] called from %08lx: arg %08lx %08lx %08lx %08lx %08lx %08lx -> %08lx (nr %lu)", idx,
                 caller, arg0[0], arg0[1], arg0[2], arg0[3], arg0[4], arg0[5], *saved_eax, n);
    } else if ((n % 200) == 0) {
        log_line("slot[%d] called %lu times", idx, n);
    }
}

__attribute__((naked, used)) static void c9_common(void) {
    __asm__ volatile("leal 12(%esp), %ecx\n\t"      /* saved EAX of pushal */
                     "leal 48(%esp), %edx\n\t"      /* first argument */
                     "movl 4(%esp), %eax\n\t"       /* index */
                     "push %edx\n\t"
                     "push %ecx\n\t"
                     "push %eax\n\t"
                     "call _c9_on_call\n\t"
                     "add $12, %esp\n\t"
                     "ret\n\t");
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
    if (lookup_xp(v, &dll, &name, &ordinal)) {
        HMODULE mod = GetModuleHandleA(dll);
        void *live = NULL;
        if (!mod) mod = LoadLibraryA(dll);
        if (mod) live = GetProcAddress(mod, name[0] ? name : (LPCSTR)(ULONG_PTR)ordinal);
        if (live) {
            *cell = (DWORD)(ULONG_PTR)live;
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

static DWORD WINAPI worker(LPVOID arg) {
    BYTE *pool;
    DWORD *const table = (DWORD *)(ULONG_PTR)g_table;
    DWORD old = 0;
    unsigned long last_resolved = 0, last_stubbed = 0, stable = 0;
    (void)arg;
    pool = (BYTE *)VirtualAlloc(NULL, (size_t)g_cells * C9_STUB_SIZE, MEM_COMMIT | MEM_RESERVE,
                                PAGE_EXECUTE_READWRITE);
    if (!pool) {
        log_line("geen geheugen voor de stubs");
        return 0;
    }
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
    CreateThread(NULL, 0, worker, NULL, 0, NULL);
}
