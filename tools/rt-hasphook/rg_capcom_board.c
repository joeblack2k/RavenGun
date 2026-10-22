/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_capcom_board.c -- an in-process emulation of the Capcom text I/O board that Luigi's Mansion Arcade
 * (VACUUM.exe) talks to over a COM port, fed from RavenGun's board image (/dev/shm/ravengun_JvsState).
 * Part of rg-pe-hook.dll; it does nothing unless RG_CAPCOM_BOARD is set.
 *
 * WHAT THE GAME DOES (measured on our route, 2026-10-22; Wine +file trace and an image of the unpacked game)
 *   - It opens "COM1" (and a second port, "\\.\COM10" or "COM2", for an output-only panel) once each with
 *     CreateFileW, sets 115200 8N1 and polls every ~28 ms from its own thread: WriteFile of a request,
 *     ClearCommError for the number of bytes waiting, ReadFile of that many bytes. A reply ends with 0x03.
 *   - Request (the game's own format string): "/s\n\nCO:%c,%d\nSW:%d,%d\nSO:%d,%d\nMO:%d,%d\nST:%d,%d,%d\n/e\n\x03".
 *     CO carries the number of coins the game has just booked (the board subtracts them); SO/MO/ST are
 *     outputs (logged here, see RG_CAPCOM_BOARD_LOG).
 *   - Reply keys the game looks for (strstr + sscanf, any order): "G1:%x,%x,%x,%x" (P1 X, P1 Y, P2 X, P2 Y;
 *     12-bit position; X bit 12 = START, bit 13 = controller button (Strobulb), bit 14 = controller lever
 *     (vacuum), bit 15 = no position (SCREEN OUT in the game's INPUT TEST; the game keeps the last position);
 *     bit 15 of P1 Y = gun data valid, without it the game drops both guns), "CO:%c,%d" (coins not yet
 *     booked), "SW:%x,%x,%x" (cabinet switches in the first word: 2 = SERVICE, 4 = TEST, 8 = SELECT).
 *   - Aim: the game turns raw into pixels with its stored calibration (min/max per axis); with the values it
 *     ships with (min 0, max 1024 on both axes, solved from INPUT TEST readings) X px = (raw - 256) * 1920 / 1024
 *     and Y px = (1279 - raw) * 1080 / 1024, so the default range below is raw 256..1280 (X) and 1279..255 (Y).
 *   - Outputs seen in CONTROLLER TEST: MO:8,0 / MO:0,8 = motor P1/P2 on, SO:1,0 = solenoid P1 pulse.
 *   Without an answer on COM1 the game has no input at all: no coin, no TEST, no gun.
 *
 * HOW IT GETS IN BETWEEN
 *   The unpacked game calls kernel32 through its own pointer table (in a read-only data section, filled by
 *   the packer). A thread here waits until those pointers appear and swaps the ones for CreateFileW, ReadFile,
 *   WriteFile, ClearCommError and the Comm setup calls for the functions below. Only pointers in
 *   non-executable, non-IAT memory of the main module are touched; every call that is not for the emulated
 *   port goes straight to kernel32. No game code is patched.
 *
 * ENVIRONMENT
 *   RG_CAPCOM_BOARD      port name(s) to emulate, ';'-separated (e.g. "COM1"); "\\.\" in front is accepted
 *   RG_CAPCOM_SHM        board image (default Z:\dev\shm\ravengun_JvsState, layout LuigisMansion)
 *   RG_CAPCOM_BOARD_LOG  log file: start-up, every change of the game's outputs (CO/SW/SO/MO/ST) and inputs
 *   RG_CAPCOM_AIM        raw position at the screen edges: "x_left,x_right,y_top,y_bottom" (12-bit values)
 *                        (default 256,1280,1279,255)
 *   RG_CAPCOM_BITS       G1 X bits: "trigger,grenade,reload,start" in hex (default 4000,2000,8000,1000)
 *   RG_CAPCOM_SW         SW word bits: "test,service,aux1" in hex (default 4,2,8)
 */
#if defined(_WIN32)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

/* ------------------------------------------------------------------ configuration and log */

static char B_ports[4][32];
static int B_nports;
static char B_shm_path[MAX_PATH] = "Z:\\dev\\shm\\ravengun_JvsState";
static FILE *B_log;
static CRITICAL_SECTION B_mu;
static int B_aim[4] = { 256, 1280, 1279, 255 };          /* raw at x=0, x=1, y=0, y=1 (overridden by RG_CAPCOM_AIM) */
static unsigned B_bit_trigger = 0x4000, B_bit_grenade = 0x2000, B_bit_reload = 0x8000, B_bit_start = 0x1000;
static unsigned B_sw_test = 0x4, B_sw_service = 0x2, B_sw_aux1 = 0x8;

static void b_log(const char *fmt, ...)
{
    va_list ap;
    if (!B_log)
        return;
    fprintf(B_log, "[capcom-board] %llu ", (unsigned long long)GetTickCount64());
    va_start(ap, fmt);
    vfprintf(B_log, fmt, ap);
    va_end(ap);
    fputc('\n', B_log);
    fflush(B_log);
}

/* ------------------------------------------------------------------ the real kernel32 functions */

typedef HANDLE (WINAPI *create_w_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL (WINAPI *read_t)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI *write_t)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI *clear_t)(HANDLE, LPDWORD, LPCOMSTAT);
typedef BOOL (WINAPI *dcb_t)(HANDLE, LPDCB);
typedef BOOL (WINAPI *timeouts_t)(HANDLE, LPCOMMTIMEOUTS);
typedef BOOL (WINAPI *prop_t)(HANDLE, LPCOMMPROP);
typedef BOOL (WINAPI *setup_t)(HANDLE, DWORD, DWORD);
typedef BOOL (WINAPI *purge_t)(HANDLE, DWORD);

static create_w_t R_CreateFileW;
static read_t R_ReadFile;
static write_t R_WriteFile;
static clear_t R_ClearCommError;
static dcb_t R_GetCommState, R_SetCommState;
static timeouts_t R_SetCommTimeouts;
static prop_t R_GetCommProperties;
static setup_t R_SetupComm;
static purge_t R_PurgeComm;

/* ------------------------------------------------------------------ board state */

static HANDLE B_port = NULL;          /* the handle handed to the game (an unnamed event; CloseHandle just works) */
static unsigned char B_rx[2048];      /* reply bytes waiting for the game */
static DWORD B_rx_len, B_rx_pos;
static char B_req[1024];              /* request being assembled */
static DWORD B_req_len;
static unsigned B_coins;              /* coins not yet booked by the game */
static int B_last_clear;              /* last CO value from the game */
static char B_co_char = 'F';
static int B_out[10], B_out_seen;      /* last CO.., SW.., SO.., MO.., ST.. from the game, for the change log */
static unsigned long B_requests;
static HANDLE B_shm = INVALID_HANDLE_VALUE;
static unsigned char B_img[64];
static int B_prev_coin[2];
static char B_last_reply[160];

static int b_is_port(LPCWSTR name)
{
    char a[64];
    int i, n;
    if (!name)
        return 0;
    for (n = 0; name[n] && n < 63; n++)
        a[n] = (char)(name[n] < 128 ? name[n] : '?');
    a[n] = 0;
    {
        const char *p = a;
        if (!strncmp(p, "\\\\.\\", 4))
            p += 4;
        for (i = 0; i < B_nports; i++)
            if (!_stricmp(p, B_ports[i]))
                return 1;
    }
    return 0;
}

static int b_bit(int off, int bit) { return (B_img[off + bit / 8] >> (bit % 8)) & 1; }

static void b_read_image(void)
{
    DWORD n = 0;
    if (B_shm == INVALID_HANDLE_VALUE) {
        static ULONGLONG next;
        ULONGLONG now = GetTickCount64();
        if (now < next)
            return;
        next = now + 1000;
        B_shm = CreateFileA(B_shm_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (B_shm == INVALID_HANDLE_VALUE)
            return;
        b_log("board image open: %s", B_shm_path);
    }
    SetFilePointer(B_shm, 0, NULL, FILE_BEGIN);
    if (!R_ReadFile(B_shm, B_img, sizeof B_img, &n, NULL) || n < 24) {
        CloseHandle(B_shm);
        B_shm = INVALID_HANDLE_VALUE;
        memset(B_img, 0, sizeof B_img);
    }
}

/* Raw 12-bit position from a 16-bit axis (layout LuigisMansion, offsets 16..23, 0..65535). */
static unsigned b_raw(int off, int lo, int hi)
{
    unsigned v = (unsigned)B_img[off] | ((unsigned)B_img[off + 1] << 8);
    double f = v / 65535.0;
    double r = lo + f * (hi - lo);
    if (r < 0)
        r = 0;
    if (r > 4095)
        r = 4095;
    return (unsigned)(r + 0.5);
}

/* Build the reply to one request (B_mu held). */
static void b_reply(void)
{
    unsigned x1, y1, x2, y2, sw = 0;
    int k;
    char out[sizeof B_rx];
    int n;

    b_read_image();
    /* coins: a rising edge of coin1/coin2 (offset 8, bits 24/25) counts one */
    for (k = 0; k < 2; k++) {
        int c = b_bit(8, 24 + k);
        if (c && !B_prev_coin[k]) {
            B_coins++;
            b_log("coin %d (coins waiting: %u)", k + 1, B_coins);
        }
        B_prev_coin[k] = c;
    }
    x1 = b_raw(16, B_aim[0], B_aim[1]);
    y1 = b_raw(18, B_aim[2], B_aim[3]);
    x2 = b_raw(20, B_aim[0], B_aim[1]);
    y2 = b_raw(22, B_aim[2], B_aim[3]);
    if (b_bit(8, 2)) x1 |= B_bit_trigger;
    if (b_bit(8, 1)) x1 |= B_bit_grenade;
    if (b_bit(8, 16)) x1 |= B_bit_reload;
    if (b_bit(8, 0)) x1 |= B_bit_start;
    if (b_bit(8, 5)) x2 |= B_bit_trigger;
    if (b_bit(8, 4)) x2 |= B_bit_grenade;
    if (b_bit(8, 17)) x2 |= B_bit_reload;
    if (b_bit(8, 3)) x2 |= B_bit_start;
    y1 |= 0x8000;                                   /* gun data valid */
    if (b_bit(8, 8)) sw |= B_sw_test;
    if (b_bit(8, 10)) sw |= B_sw_service;
    if (b_bit(8, 9)) sw |= B_sw_aux1;
    n = snprintf(out, sizeof out, "G1:%x,%x,%x,%x\nCO:%c,%u\nSW:%x,0,0\n\x03", x1, y1, x2, y2, B_co_char, B_coins, sw);
    if (n <= 0 || n >= (int)sizeof out)
        return;
    {
        /* Log input changes without the raw position (that changes all the time). */
        char key[160];
        snprintf(key, sizeof key, "buttons p1 %x p2 %x sw %x coins %u", x1 & 0xf000, x2 & 0xf000, sw, B_coins);
        if (strcmp(key, B_last_reply)) {
            b_log("in: %s (G1 %x,%x,%x,%x)", key, x1, y1, x2, y2);
            snprintf(B_last_reply, sizeof B_last_reply, "%s", key);
        }
    }
    /* One reply waiting at a time: a new request replaces a reply the game has not read yet. */
    memcpy(B_rx, out, (size_t)n);
    B_rx_len = (DWORD)n;
    B_rx_pos = 0;
}

static void b_request(const char *s)
{
    const char *p;
    int v[10] = { 0 };
    char c = 'F';
    B_requests++;
    if ((p = strstr(s, "CO:")))
        sscanf(p, "CO:%c,%d", &c, &v[0]);
    if ((p = strstr(s, "SW:")))
        sscanf(p, "SW:%d,%d", &v[1], &v[2]);
    if ((p = strstr(s, "SO:")))
        sscanf(p, "SO:%d,%d", &v[3], &v[4]);
    if ((p = strstr(s, "MO:")))
        sscanf(p, "MO:%d,%d", &v[5], &v[6]);
    if ((p = strstr(s, "ST:")))
        sscanf(p, "ST:%d,%d,%d", &v[7], &v[8], &v[9]);
    B_co_char = c;
    /* The game books n coins and repeats "CO:x,n" until the board reports what is left; take them off once. */
    if (v[0] > 0 && v[0] != B_last_clear) {
        unsigned n = (unsigned)v[0];
        B_coins = B_coins > n ? B_coins - n : 0;
        b_log("game booked %d coin(s); coins waiting: %u", v[0], B_coins);
    }
    B_last_clear = v[0];
    if (!B_out_seen || memcmp(v, B_out, sizeof v)) {
        char t[200];
        int k = 0;
        k += snprintf(t + k, sizeof t - k, "out #%lu: CO:%c,%d SW:%d,%d SO:%d,%d MO:%d,%d ST:", B_requests, c, v[0], v[1],
                      v[2], v[3], v[4], v[5], v[6]);
        snprintf(t + k, sizeof t - k, "%d,%d,%d", v[7], v[8], v[9]);
        b_log("%s", t);
        memcpy(B_out, v, sizeof v);
        B_out_seen = 1;
    }
    b_reply();
}

/* ------------------------------------------------------------------ the replacements */

static HANDLE WINAPI h_CreateFileW(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl,
                                   HANDLE tmpl)
{
    if (b_is_port(name)) {
        HANDLE h = CreateEventW(NULL, TRUE, FALSE, NULL);
        EnterCriticalSection(&B_mu);
        B_port = h;
        B_rx_len = B_rx_pos = 0;
        B_req_len = 0;
        LeaveCriticalSection(&B_mu);
        b_log("CreateFileW(%ls): emulated board, handle %p", name, (void *)h);
        return h ? h : INVALID_HANDLE_VALUE;
    }
    return R_CreateFileW(name, acc, share, sa, disp, fl, tmpl);
}

static BOOL WINAPI h_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD done, LPOVERLAPPED ov)
{
    DWORD i;
    if (!h || h != B_port)
        return R_WriteFile(h, buf, n, done, ov);
    EnterCriticalSection(&B_mu);
    for (i = 0; i < n; i++) {
        char ch = ((const char *)buf)[i];
        if (B_req_len < sizeof B_req - 1)
            B_req[B_req_len++] = ch;
        if (ch == 0x03) {
            B_req[B_req_len] = 0;
            b_request(B_req);
            B_req_len = 0;
        }
    }
    LeaveCriticalSection(&B_mu);
    if (done)
        *done = n;
    return TRUE;
}

static BOOL WINAPI h_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD done, LPOVERLAPPED ov)
{
    DWORD k;
    if (!h || h != B_port)
        return R_ReadFile(h, buf, n, done, ov);
    EnterCriticalSection(&B_mu);
    k = B_rx_len - B_rx_pos;
    if (k > n)
        k = n;
    memcpy(buf, B_rx + B_rx_pos, k);
    B_rx_pos += k;
    LeaveCriticalSection(&B_mu);
    if (done)
        *done = k;
    return TRUE;
}

static BOOL WINAPI h_ClearCommError(HANDLE h, LPDWORD err, LPCOMSTAT st)
{
    if (!h || h != B_port)
        return R_ClearCommError(h, err, st);
    if (err)
        *err = 0;
    if (st) {
        memset(st, 0, sizeof *st);
        EnterCriticalSection(&B_mu);
        st->cbInQue = B_rx_len - B_rx_pos;
        LeaveCriticalSection(&B_mu);
    }
    return TRUE;
}

static BOOL WINAPI h_GetCommState(HANDLE h, LPDCB d)
{
    if (!h || h != B_port)
        return R_GetCommState(h, d);
    memset(d, 0, sizeof *d);
    d->DCBlength = sizeof *d;
    d->BaudRate = 115200;
    d->fBinary = 1;
    d->ByteSize = 8;
    return TRUE;
}

static BOOL WINAPI h_SetCommState(HANDLE h, LPDCB d)
{
    if (!h || h != B_port)
        return R_SetCommState(h, d);
    b_log("SetCommState: %lu baud, %u bits, parity %u, stop %u", (unsigned long)d->BaudRate, d->ByteSize, d->Parity,
          d->StopBits);
    return TRUE;
}

static BOOL WINAPI h_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t)
{
    return (!h || h != B_port) ? R_SetCommTimeouts(h, t) : TRUE;
}

static BOOL WINAPI h_GetCommProperties(HANDLE h, LPCOMMPROP p)
{
    if (!h || h != B_port)
        return R_GetCommProperties(h, p);
    memset(p, 0, sizeof *p);
    p->wPacketLength = sizeof *p;
    p->dwMaxTxQueue = p->dwMaxRxQueue = 0x2000;
    p->dwCurrentTxQueue = p->dwCurrentRxQueue = 0x2000;
    p->dwMaxBaud = BAUD_115200;
    p->dwProvSubType = PST_RS232;
    return TRUE;
}

static BOOL WINAPI h_SetupComm(HANDLE h, DWORD a, DWORD b)
{
    return (!h || h != B_port) ? R_SetupComm(h, a, b) : TRUE;
}

static BOOL WINAPI h_PurgeComm(HANDLE h, DWORD f)
{
    if (!h || h != B_port)
        return R_PurgeComm(h, f);
    EnterCriticalSection(&B_mu);
    if (f & PURGE_RXCLEAR)
        B_rx_len = B_rx_pos = 0;
    if (f & PURGE_TXCLEAR)
        B_req_len = 0;
    LeaveCriticalSection(&B_mu);
    return TRUE;
}

/* ------------------------------------------------------------------ swapping the game's pointers */

typedef struct { const char *name; void **real; void *mine; int done; } b_swap;

static b_swap B_swaps[] = {
    { "CreateFileW", (void **)&R_CreateFileW, (void *)h_CreateFileW, 0 },
    { "ReadFile", (void **)&R_ReadFile, (void *)h_ReadFile, 0 },
    { "WriteFile", (void **)&R_WriteFile, (void *)h_WriteFile, 0 },
    { "ClearCommError", (void **)&R_ClearCommError, (void *)h_ClearCommError, 0 },
    { "GetCommState", (void **)&R_GetCommState, (void *)h_GetCommState, 0 },
    { "SetCommState", (void **)&R_SetCommState, (void *)h_SetCommState, 0 },
    { "SetCommTimeouts", (void **)&R_SetCommTimeouts, (void *)h_SetCommTimeouts, 0 },
    { "GetCommProperties", (void **)&R_GetCommProperties, (void *)h_GetCommProperties, 0 },
    { "SetupComm", (void **)&R_SetupComm, (void *)h_SetupComm, 0 },
    { "PurgeComm", (void **)&R_PurgeComm, (void *)h_PurgeComm, 0 },
};
#define B_NSWAP ((int)(sizeof B_swaps / sizeof B_swaps[0]))

/* One pass over the main module: swap every pointer to a real function in readable, non-executable memory
   outside the loader's IAT. Returns the number of pointers swapped. */
static int b_swap_pass(unsigned char *base, size_t size, size_t iat_lo, size_t iat_hi)
{
    size_t off = 0;
    int swapped = 0;
    while (off < size) {
        MEMORY_BASIC_INFORMATION mi;
        size_t len, i;
        DWORD prot;
        if (!VirtualQuery(base + off, &mi, sizeof mi))
            break;
        len = (size_t)((unsigned char *)mi.BaseAddress + mi.RegionSize - (base + off));
        if (off + len > size)
            len = size - off;
        prot = mi.Protect & 0xff;
        if (mi.State == MEM_COMMIT && !(mi.Protect & PAGE_GUARD) &&
            (prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_WRITECOPY)) {
            for (i = 0; i + 8 <= len; i += 8) {
                size_t rva = off + i;
                uint64_t v = *(volatile uint64_t *)(base + rva);
                int k;
                if (!v || (rva >= iat_lo && rva < iat_hi))
                    continue;
                for (k = 0; k < B_NSWAP; k++) {
                    if (v == (uint64_t)(ULONG_PTR)*B_swaps[k].real) {
                        DWORD old;
                        if (VirtualProtect(base + rva, 8, PAGE_READWRITE, &old)) {
                            *(void **)(base + rva) = B_swaps[k].mine;
                            VirtualProtect(base + rva, 8, old, &old);
                            B_swaps[k].done++;
                            swapped++;
                            b_log("pointer for %s at rva 0x%zx swapped", B_swaps[k].name, rva);
                        }
                        break;
                    }
                }
            }
        }
        off += len;
    }
    return swapped;
}

static DWORD WINAPI b_swap_thread(LPVOID arg)
{
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    size_t size = nt->OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY *iat = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    size_t iat_lo = iat->VirtualAddress, iat_hi = (size_t)iat->VirtualAddress + iat->Size;
    ULONGLONG t0 = GetTickCount64();
    int total = 0, extra_passes = -1;
    (void)arg;
    b_log("waiting for the game's pointer table (image %p, 0x%zx bytes, IAT rva 0x%zx..0x%zx)", (void *)base, size,
          iat_lo, iat_hi);
    while (GetTickCount64() - t0 < 120000) {
        int n = b_swap_pass(base, size, iat_lo, iat_hi);
        total += n;
        if (B_swaps[0].done && extra_passes < 0) {
            extra_passes = 5;      /* a few more passes in case the table is still being written */
            b_log("CreateFileW swapped after %llu ms", (unsigned long long)(GetTickCount64() - t0));
        }
        if (extra_passes >= 0 && extra_passes-- == 0)
            break;
        Sleep(extra_passes >= 0 ? 100 : 5);
    }
    {
        int k;
        for (k = 0; k < B_NSWAP; k++)
            b_log("%-18s swapped %d time(s)", B_swaps[k].name, B_swaps[k].done);
    }
    b_log("swap thread done: %d pointer(s) in %llu ms", total, (unsigned long long)(GetTickCount64() - t0));
    return 0;
}

static void b_parse_hex_list(const char *s, unsigned *out, int n)
{
    int i;
    for (i = 0; i < n && s && *s; i++) {
        char *end;
        unsigned long v = strtoul(s, &end, 16);
        if (end == s)
            break;
        out[i] = (unsigned)v;
        s = (*end == ',') ? end + 1 : end;
    }
}

void rg_capcom_board_start(void)
{
    const char *ports = getenv("RG_CAPCOM_BOARD");
    const char *s;
    HMODULE k32;
    int k;
    if (!ports || !*ports)
        return;
    InitializeCriticalSection(&B_mu);
    if ((s = getenv("RG_CAPCOM_BOARD_LOG")) && *s)
        B_log = fopen(s, "a");
    {
        char buf[160], *save = NULL, *p;
        snprintf(buf, sizeof buf, "%s", ports);
        for (p = strtok_r(buf, ";", &save); p && B_nports < 4; p = strtok_r(NULL, ";", &save)) {
            if (!strncmp(p, "\\\\.\\", 4))
                p += 4;
            snprintf(B_ports[B_nports++], sizeof B_ports[0], "%s", p);
        }
    }
    if ((s = getenv("RG_CAPCOM_SHM")) && *s)
        snprintf(B_shm_path, sizeof B_shm_path, "%s", s);
    if ((s = getenv("RG_CAPCOM_AIM")) && *s)
        sscanf(s, "%d,%d,%d,%d", &B_aim[0], &B_aim[1], &B_aim[2], &B_aim[3]);
    if ((s = getenv("RG_CAPCOM_BITS")) && *s) {
        unsigned v[4] = { B_bit_trigger, B_bit_grenade, B_bit_reload, B_bit_start };
        b_parse_hex_list(s, v, 4);
        B_bit_trigger = v[0], B_bit_grenade = v[1], B_bit_reload = v[2], B_bit_start = v[3];
    }
    if ((s = getenv("RG_CAPCOM_SW")) && *s) {
        unsigned v[3] = { B_sw_test, B_sw_service, B_sw_aux1 };
        b_parse_hex_list(s, v, 3);
        B_sw_test = v[0], B_sw_service = v[1], B_sw_aux1 = v[2];
    }
    b_log("start (pid %lu): ports %s, image %s, aim x %d..%d y %d..%d, G1 bits trigger %x grenade %x reload %x start %x, "
          "SW test %x service %x aux1 %x", (unsigned long)GetCurrentProcessId(), ports, B_shm_path, B_aim[0], B_aim[1],
          B_aim[2], B_aim[3], B_bit_trigger, B_bit_grenade, B_bit_reload, B_bit_start, B_sw_test, B_sw_service, B_sw_aux1);
    k32 = GetModuleHandleA("kernel32.dll");
    for (k = 0; k < B_NSWAP; k++) {
        FARPROC f = k32 ? GetProcAddress(k32, B_swaps[k].name) : NULL;
        if (!f) {
            b_log("kernel32!%s not found; the board stays off", B_swaps[k].name);
            return;
        }
        memcpy(B_swaps[k].real, &f, sizeof f);
    }
    {
        HANDLE h = CreateThread(NULL, 0, b_swap_thread, NULL, 0, NULL);
        if (h)
            CloseHandle(h);
        else
            b_log("could not start the swap thread; the board stays off");
    }
}

#endif /* _WIN32 */
