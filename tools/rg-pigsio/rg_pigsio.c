/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-pigsio.dll -- RavenGun's synthetic USB HID I/O board for Pigs Might Fly (Sega/Shell/Shell.exe, 32-bit). Loaded into the
 * shell by the loader (drop-in with Preload = true, TP_PRELOAD_DLL). The shell finds its board through SetupAPI and HID and
 * talks to it with ReadFile/WriteFile (rg_pigs_board.h has the format, read from the shell's own code). This DLL redirects
 * those imports of the main image:
 *
 *   SETUPAPI  SetupDiGetClassDevsA, SetupDiEnumDeviceInterfaces, SetupDiGetDeviceInterfaceDetailA, SetupDiDestroyDeviceInfoList
 *   HID       HidD_GetAttributes, HidD_GetPreparsedData, HidP_GetCaps
 *   KERNEL32  CreateFileA, ReadFile, WriteFile, CloseHandle
 *
 * The HID class enumeration answers with one interface: the board (VID_1043 PID_0000, GAMEPAD collection). Its path opens to a
 * handle of our own; reads return the input report built from the shared memory "TeknoParrot_JvsState" of the direct route
 * (layout WartranTroopers), writes go to the board logic (handshake replies, lamps). Every other set, path and handle goes to
 * the real function unchanged.
 *
 * Environment (all optional):
 *   RG_PIGSIO_LOG    log file (Windows path; default rg-pigsio.log next to the exe; "-" = no log)
 *   RG_PIGSIO_ID     two hex bytes "xxyy": the data bytes of the 0xB0 and 0xB1 handshake replies (default 0000)
 *
 * BUILD: tools/rg-pigsio/build.sh (the mingw cross compiler in ~/.cache/ravengun-mingw, i686).
 */
#include <windows.h>
#include <setupapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rg_pigs_board.h"

#define RG_PIGS_SET ((HDEVINFO)(ULONG_PTR)0x50160001u)
#define RG_PIGS_HANDLE ((HANDLE)(ULONG_PTR)0x50160004u)
#define RG_PIGS_PREPARSED ((void *)(ULONG_PTR)0x50160008u)
#define RG_PIGS_PATH "\\\\?\\hid#vid_1043&pid_0000#ravengun&pigsio&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"
#define HIDP_STATUS_SUCCESS_ ((LONG)0x00110000)

/* HIDD_ATTRIBUTES and HIDP_CAPS as hid.dll fills them (hidsdi.h / hidpi.h). */
typedef struct {
    ULONG Size;
    USHORT VendorID;
    USHORT ProductID;
    USHORT VersionNumber;
} rg_hidd_attributes;

typedef struct {
    USHORT Usage;
    USHORT UsagePage;
    USHORT InputReportByteLength;
    USHORT OutputReportByteLength;
    USHORT FeatureReportByteLength;
    USHORT Reserved[17];
    USHORT NumberLinkCollectionNodes;
    USHORT NumberInputButtonCaps;
    USHORT NumberInputValueCaps;
    USHORT NumberInputDataIndices;
    USHORT NumberOutputButtonCaps;
    USHORT NumberOutputValueCaps;
    USHORT NumberOutputDataIndices;
    USHORT NumberFeatureButtonCaps;
    USHORT NumberFeatureValueCaps;
    USHORT NumberFeatureDataIndices;
} rg_hidp_caps;

static rg_pigs_board g_board;
static CRITICAL_SECTION g_lock;
static const volatile BYTE *g_shm;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static unsigned g_reads, g_writes, g_opens, g_enums, g_logged_cmds;
static DWORD g_last_summary;

static void log_line(const char *fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD w = 0;
    WriteFile(g_log, buf, (DWORD)n, &w, NULL);
}

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

static void hex_line(const char *label, const BYTE *p, DWORD len) {
    char txt[3 * 32 + 1];
    DWORD show = len < 32 ? len : 32;
    int k = 0;
    for (DWORD i = 0; i < show; ++i) k += snprintf(txt + k, sizeof txt - (size_t)k, "%02x ", p[i]);
    txt[k] = 0;
    log_line("%s%s", label, txt);
}

/* ------------------------------------------------------------------------------------------------ SetupAPI */

typedef HDEVINFO(WINAPI *getclassdevs_fn)(const GUID *, PCSTR, HWND, DWORD);
typedef BOOL(WINAPI *enumif_fn)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL(WINAPI *ifdetail_fn)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_A, DWORD, PDWORD,
                                  PSP_DEVINFO_DATA);
typedef BOOL(WINAPI *destroyset_fn)(HDEVINFO);
static getclassdevs_fn real_getclassdevs;
static enumif_fn real_enumif;
static ifdetail_fn real_ifdetail;
static destroyset_fn real_destroyset;

/* The shell asks for the HID interface class (HidD_GetHidGuid) and nothing else; any other class goes to the real SetupAPI. */
static int is_hid_class(const GUID *cls) {
    static const GUID hid = {0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    return cls && memcmp(cls, &hid, sizeof hid) == 0;
}

static HDEVINFO WINAPI rg_SetupDiGetClassDevsA(const GUID *cls, PCSTR enumerator, HWND parent, DWORD flags) {
    if (!is_hid_class(cls) || enumerator) return real_getclassdevs(cls, enumerator, parent, flags);
    log_line("SetupDiGetClassDevsA(HID, flags=0x%lx) -> the board's set", (unsigned long)flags);
    SetLastError(ERROR_SUCCESS);
    return RG_PIGS_SET;
}

static BOOL WINAPI rg_SetupDiEnumDeviceInterfaces(HDEVINFO set, PSP_DEVINFO_DATA info, const GUID *iface, DWORD index,
                                                  PSP_DEVICE_INTERFACE_DATA data) {
    if (set != RG_PIGS_SET) return real_enumif(set, info, iface, index, data);
    g_enums++;
    if (index != 0) {
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (data) {
        data->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        if (iface) data->InterfaceClassGuid = *iface;
        data->Flags = SPINT_ACTIVE;
        data->Reserved = 0;
    }
    log_line("SetupDiEnumDeviceInterfaces(0) -> the board");
    return TRUE;
}

static BOOL WINAPI rg_SetupDiGetDeviceInterfaceDetailA(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA data,
                                                       PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail, DWORD size, PDWORD needed,
                                                       PSP_DEVINFO_DATA info) {
    if (set != RG_PIGS_SET) return real_ifdetail(set, data, detail, size, needed, info);
    const DWORD need = (DWORD)(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_A, DevicePath) + sizeof RG_PIGS_PATH);
    if (needed) *needed = need;
    if (!detail || size < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(detail->DevicePath, RG_PIGS_PATH, sizeof RG_PIGS_PATH);
    if (info) {
        info->cbSize = sizeof(SP_DEVINFO_DATA);
        memset(&info->ClassGuid, 0, sizeof info->ClassGuid);
        info->DevInst = 1;
        info->Reserved = 0;
    }
    log_line("SetupDiGetDeviceInterfaceDetailA -> %s", RG_PIGS_PATH);
    return TRUE;
}

static BOOL WINAPI rg_SetupDiDestroyDeviceInfoList(HDEVINFO set) {
    if (set == RG_PIGS_SET) return TRUE;
    return real_destroyset(set);
}

/* ------------------------------------------------------------------------------------------------ HID */

typedef BOOLEAN(WINAPI *getattr_fn)(HANDLE, rg_hidd_attributes *);
typedef BOOLEAN(WINAPI *getprep_fn)(HANDLE, void **);
typedef LONG(WINAPI *getcaps_fn)(void *, rg_hidp_caps *);
static getattr_fn real_getattr;
static getprep_fn real_getprep;
static getcaps_fn real_getcaps;

static BOOLEAN WINAPI rg_HidD_GetAttributes(HANDLE h, rg_hidd_attributes *a) {
    if (h != RG_PIGS_HANDLE) return real_getattr ? real_getattr(h, a) : FALSE;
    if (!a) return FALSE;
    a->Size = sizeof *a;
    a->VendorID = (USHORT)RG_PIGS_VID;
    a->ProductID = (USHORT)RG_PIGS_PID;
    a->VersionNumber = (USHORT)RG_PIGS_VERSION;
    log_line("HidD_GetAttributes -> VID 0x%04x PID 0x%04x version 0x%04x", RG_PIGS_VID, RG_PIGS_PID, RG_PIGS_VERSION);
    return TRUE;
}

static BOOLEAN WINAPI rg_HidD_GetPreparsedData(HANDLE h, void **out) {
    if (h != RG_PIGS_HANDLE) return real_getprep ? real_getprep(h, out) : FALSE;
    if (!out) return FALSE;
    *out = RG_PIGS_PREPARSED;
    return TRUE;
}

static LONG WINAPI rg_HidP_GetCaps(void *prep, rg_hidp_caps *caps) {
    if (prep != RG_PIGS_PREPARSED) return real_getcaps ? real_getcaps(prep, caps) : (LONG)0xC0110001;
    if (!caps) return (LONG)0xC0110001;
    memset(caps, 0, sizeof *caps);
    caps->Usage = RG_PIGS_USAGE;
    caps->UsagePage = RG_PIGS_USAGE_PAGE;
    caps->InputReportByteLength = RG_PIGS_IN_LEN;
    caps->OutputReportByteLength = RG_PIGS_OUT_LEN;
    caps->NumberLinkCollectionNodes = 1;
    log_line("HidP_GetCaps -> UsagePage 0x%02x Usage 0x%02x (GAMEPAD), in %u, out %u", RG_PIGS_USAGE_PAGE, RG_PIGS_USAGE,
             RG_PIGS_IN_LEN, RG_PIGS_OUT_LEN);
    return HIDP_STATUS_SUCCESS_;
}

/* ------------------------------------------------------------------------------------------------ KERNEL32 */

typedef HANDLE(WINAPI *createa_fn)(LPCSTR, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, HANDLE);
typedef BOOL(WINAPI *read_fn)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *write_fn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI *close_fn)(HANDLE);
static createa_fn real_createa;
static read_fn real_read;
static write_fn real_write;
static close_fn real_close;

static HANDLE WINAPI rg_CreateFileA(LPCSTR name, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD share, DWORD disp, DWORD flags,
                                    HANDLE tmpl) {
    if (name && lstrcmpiA(name, RG_PIGS_PATH) == 0) {
        g_opens++;
        log_line("CreateFileA(board, access=0x%08lx) -> board handle", (unsigned long)access);
        SetLastError(ERROR_SUCCESS);
        return RG_PIGS_HANDLE;
    }
    return real_createa(name, access, sa, share, disp, flags, tmpl);
}

static void summary_maybe(void) {
    const DWORD now = GetTickCount();
    if (now - g_last_summary < 10000u) return;
    g_last_summary = now;
    log_line("counters: enums=%u opens=%u reads=%u writes=%u commands=%u lamp_writes=%u lamps=0x%04x coins=%u shm=%s", g_enums,
             g_opens, g_reads, g_writes,
             g_board.commands, g_board.lamp_writes, g_board.lamps, g_board.coins, g_shm ? "yes" : "no");
}

/* A HID read returns when the device sends its next input report; a read that always returns at once lets the shell's main
 * loop (0x4129e5, one read per round) spin: measured 2026-10-21, about 110000 reads per second. Each read therefore waits until
 * RG_PIGS_REPORT_MS after the previous report, like an interrupt endpoint with that interval. */
#define RG_PIGS_REPORT_MS 4u
static void pace_report(void) {
    static volatile LONG last_ms;
    const DWORD now = GetTickCount();
    const DWORD prev = (DWORD)InterlockedExchange(&last_ms, (LONG)now);
    const DWORD since = now - prev;
    if (since < RG_PIGS_REPORT_MS) {
        Sleep(RG_PIGS_REPORT_MS - since);
        InterlockedExchange(&last_ms, (LONG)GetTickCount());
    }
}

static BOOL WINAPI rg_ReadFile(HANDLE h, LPVOID buf, DWORD len, LPDWORD got, LPOVERLAPPED ov) {
    if (h != RG_PIGS_HANDLE) return real_read(h, buf, len, got, ov);
    pace_report();
    BYTE rep[RG_PIGS_IN_LEN];
    BYTE copy[RG_PIGS_SHM_SIZE];
    EnterCriticalSection(&g_lock);
    if (!g_shm) {
        HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (m) {
            g_shm = (const volatile BYTE *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, RG_PIGS_SHM_SIZE);
            if (g_shm) log_line("shared memory TeknoParrot_JvsState mapped");
        }
    }
    if (g_shm)
        for (unsigned i = 0; i < RG_PIGS_SHM_SIZE; i++) copy[i] = g_shm[i];
    rg_pigs_board_input(&g_board, g_shm ? copy : NULL, rep);
    g_reads++;
    /* The first reads, and the first read after each of the first commands (the one that carries its reply). */
    if (g_reads <= 3 || (g_board.commands != g_logged_cmds && g_board.commands <= 20)) {
        g_logged_cmds = g_board.commands;
        hex_line("ReadFile -> ", rep, RG_PIGS_IN_LEN);
    }
    summary_maybe();
    LeaveCriticalSection(&g_lock);
    const DWORD n = len < RG_PIGS_IN_LEN ? len : RG_PIGS_IN_LEN;
    if (buf) memcpy(buf, rep, n);
    if (got) *got = n;
    if (ov) {
        ov->Internal = 0;
        ov->InternalHigh = n;
        if (ov->hEvent) SetEvent(ov->hEvent);
    }
    return TRUE;
}

static BOOL WINAPI rg_WriteFile(HANDLE h, LPCVOID buf, DWORD len, LPDWORD wrote, LPOVERLAPPED ov) {
    if (h != RG_PIGS_HANDLE) return real_write(h, buf, len, wrote, ov);
    EnterCriticalSection(&g_lock);
    g_writes++;
    const int cmd = rg_pigs_board_output(&g_board, (const BYTE *)buf, len);
    if (cmd) {
        const BYTE *p = (const BYTE *)buf;
        log_line("command 0x%02x data 0x%02x -> reply 0x%02x 0x%02x", p[2], p[1], g_board.reply[0], g_board.reply[1]);
        if (p[2] == RG_PIGS_CMD_ID1 || p[2] == RG_PIGS_CMD_ID2)
            log_line("   the shell keeps 0x%02x (0x4139c0 of 0x%02x with key 0x%02x)",
                     rg_pigs_shell_reply_xform(g_board.reply[1], p[1]), g_board.reply[1], p[1]);
    } else if (g_board.lamp_writes <= 20) {
        log_line("lamps 0x%04x (%lu bytes)", g_board.lamps, (unsigned long)len);
    }
    LeaveCriticalSection(&g_lock);
    if (wrote) *wrote = len;
    if (ov) {
        ov->Internal = 0;
        ov->InternalHigh = len;
        if (ov->hEvent) SetEvent(ov->hEvent);
    }
    return TRUE;
}

static BOOL WINAPI rg_CloseHandle(HANDLE h) {
    if (h == RG_PIGS_HANDLE) {
        log_line("CloseHandle(board)");
        return TRUE;
    }
    return real_close(h);
}

/* ------------------------------------------------------------------------------------------------ start */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void open_log(HMODULE exe) {
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("RG_PIGSIO_LOG", path, sizeof path);
    if (n == 1 && path[0] == '-') return;
    if (n == 0 || n >= sizeof path) {
        DWORD m = GetModuleFileNameA(exe, path, sizeof path);
        if (m == 0 || m >= sizeof path) return;
        char *slash = strrchr(path, '\\');
        if (!slash || (size_t)(slash + 1 - path) + sizeof "rg-pigsio.log" > sizeof path) return;
        memcpy(slash + 1, "rg-pigsio.log", sizeof "rg-pigsio.log");
    }
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void start(void) {
    HMODULE exe = GetModuleHandleA(NULL);
    InitializeCriticalSection(&g_lock);
    rg_pigs_board_init(&g_board);
    open_log(exe);
    char id[8];
    DWORD n = GetEnvironmentVariableA("RG_PIGSIO_ID", id, sizeof id);
    if (n == 4) {
        int a = hexval(id[0]), b = hexval(id[1]), c = hexval(id[2]), d = hexval(id[3]);
        if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
            g_board.id_reply[0] = (uint8_t)(a * 16 + b);
            g_board.id_reply[1] = (uint8_t)(c * 16 + d);
        }
    }
    char name[MAX_PATH];
    if (!GetModuleFileNameA(exe, name, sizeof name)) name[0] = 0;
    log_line("rg-pigsio: loaded in %s (pid %lu), handshake reply data 0x%02x 0x%02x", name, (unsigned long)GetCurrentProcessId(),
             g_board.id_reply[0], g_board.id_reply[1]);

    real_getclassdevs = (getclassdevs_fn)patch_import(exe, "setupapi.dll", "SetupDiGetClassDevsA", (void *)rg_SetupDiGetClassDevsA);
    real_enumif = (enumif_fn)patch_import(exe, "setupapi.dll", "SetupDiEnumDeviceInterfaces", (void *)rg_SetupDiEnumDeviceInterfaces);
    real_ifdetail = (ifdetail_fn)patch_import(exe, "setupapi.dll", "SetupDiGetDeviceInterfaceDetailA",
                                              (void *)rg_SetupDiGetDeviceInterfaceDetailA);
    real_destroyset = (destroyset_fn)patch_import(exe, "setupapi.dll", "SetupDiDestroyDeviceInfoList",
                                                  (void *)rg_SetupDiDestroyDeviceInfoList);
    real_getattr = (getattr_fn)patch_import(exe, "hid.dll", "HidD_GetAttributes", (void *)rg_HidD_GetAttributes);
    real_getprep = (getprep_fn)patch_import(exe, "hid.dll", "HidD_GetPreparsedData", (void *)rg_HidD_GetPreparsedData);
    real_getcaps = (getcaps_fn)patch_import(exe, "hid.dll", "HidP_GetCaps", (void *)rg_HidP_GetCaps);
    real_createa = (createa_fn)patch_import(exe, "kernel32.dll", "CreateFileA", (void *)rg_CreateFileA);
    real_read = (read_fn)patch_import(exe, "kernel32.dll", "ReadFile", (void *)rg_ReadFile);
    real_write = (write_fn)patch_import(exe, "kernel32.dll", "WriteFile", (void *)rg_WriteFile);
    real_close = (close_fn)patch_import(exe, "kernel32.dll", "CloseHandle", (void *)rg_CloseHandle);

    const int ok = real_getclassdevs && real_enumif && real_ifdetail && real_destroyset && real_getattr && real_getprep &&
                   real_getcaps && real_createa && real_read && real_write && real_close;
    log_line("imports redirected: %s (setupapi %d%d%d%d, hid %d%d%d, kernel32 %d%d%d%d)", ok ? "all" : "NOT ALL",
             !!real_getclassdevs, !!real_enumif, !!real_ifdetail, !!real_destroyset, !!real_getattr, !!real_getprep,
             !!real_getcaps, !!real_createa, !!real_read, !!real_write, !!real_close);
    /* An exe that does not import these (another program loading this DLL) keeps its real functions: put them back where
     * one is missing, so no redirected import is left without its original. */
    if (!real_createa) real_createa = (createa_fn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileA");
    if (!real_read) real_read = (read_fn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "ReadFile");
    if (!real_write) real_write = (write_fn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "WriteFile");
    if (!real_close) real_close = (close_fn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CloseHandle");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) start();
    return TRUE;
}
