/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wajvio.dll van RavenGun: een vervanging voor Namco's WinArc-JVS-bibliotheek (wajvio.dll, "NBGI.;WinArc;Ver2.1.1;JPN") bij
 * Lost Land Adventure (RSLauncher.exe). De originele DLL spreekt JVS over een seriele poort (COM3); de TeknoParrot-kern
 * vervangt die poort. Deze DLL heeft geen poort nodig: hij vult het invoerblok dat RSLauncher via WAJVGetInput leest uit het
 * gedeelde geheugen "TeknoParrot_JvsState" van de directe route (tp-shm, indeling LostLandAdventures).
 *
 * Wat hier wel en niet in zit: zie rg_wajv_map.h (indeling) en docs/research/losse/LostLandAdventure.md (wat nog ontbreekt: de
 * trekkers en de richtas van de game, de dongle-gegevens, de ALL.Net-authenticatie).
 *
 * Exports: dezelfde 18 namen en ordinalen als het origineel (wajvio_rg.def). RSLauncher.exe gebruikt er acht: WAJVOpen,
 * WAJVGetNodeCount, WAJVGetInput, WAJVUpdate, WAJVGetStatus, WAJVSetFunctionSettings, WAJVSetOutput, WAJVSetPLParm. De rest is
 * een stomp met de terugkeerwaarde van het origineel.
 *
 * Aanroepvolgorde van RSLauncher (0x140063070..0x1400631d3): WAJVSetFunctionSettings(1, 11, tabel) -> WAJVOpen("COM3") ->
 * herhaald WAJVGetStatus() tot 0 (maximaal 600 keer, 100 ms ertussen) -> WAJVSetPLParm(1, 180, 8) -> WAJVUpdate().
 * Daarna per beeld WAJVSetOutput(1, uitvoer), WAJVUpdate(), WAJVGetNodeCount() (moet > 0), WAJVGetStatus() (-2 = fout), WAJVGetInput(1).
 *
 * Alleen kernel32, geen C-bibliotheek. Omgeving: RG_WAJV_LOG = pad van het logboek (standaard rg-wajvio.log naast de exe).
 */
#include <windows.h>

#include "rg_wajv_map.h"

static rg_wajv_state g_state;
static uint8_t g_block[RG_WAJV_BLOCK_SIZE];
static const volatile uint8_t *g_shm;
static int g_open;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static unsigned g_logged[32];
static unsigned g_calls[32];
/* WAJVGetStatus terug. GEMETEN 2026-10-21: met 0 blijft RSLauncher ~10x per seconde WAJVGetStatus vragen en komt
 * hij nooit verder; met 1 loopt hij meteen door naar WAJVSetPLParm en daarna de beeldlus (WAJVSetOutput, WAJVUpdate,
 * WAJVGetNodeCount, WAJVGetInput op ~54 Hz). Dus 0 betekent "nog niet klaar" en 1 "klaar". Standaard 1; met
 * RG_WAJV_STATUS is een andere waarde te proberen (de tellers in het logboek laten zien waar hij blijft). */
static int g_status = 1;

static void log_line(const char *s) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    DWORD n = 0;
    unsigned len = 0;
    while (s[len]) len++;
    WriteFile(g_log, s, len, &n, NULL);
    WriteFile(g_log, "\r\n", 2, &n, NULL);
}

/* Elke aanroep hoogstens de eerste keren loggen (het gaat om bewijs dat de DLL geladen is en wat RSLauncher vraagt);
 * alle aanroepen tellen mee in g_calls en komen elke tien seconden als samenvatting in het logboek, zodat te zien is
 * OF en HOE vaak de lader blijft hangen (bijvoorbeeld de GetStatus-lus van 600 rondes). */
static void trace(int id, const char *name) {
    if (id < 0 || id >= 32) return;
    g_calls[id]++;
    if (g_logged[id] >= 3) return;
    g_logged[id]++;
    log_line(name);
}

static const char *const g_names[9] = {"WAJVOpen", "WAJVClose", "WAJVGetStatus", "WAJVGetNodeCount", "WAJVGetInput",
                                       "WAJVUpdate", "WAJVSetOutput", "WAJVSetFunctionSettings", "WAJVSetPLParm"};

/* "teller 12 s: WAJVGetStatus=600 WAJVUpdate=41" -- per aanroep die al geweest is. */
static void summary(unsigned secs) {
    char buf[400];
    unsigned len = 0;
    const char *head = (secs ? "teller " : "einde ");
    for (unsigned i = 0; head[i]; i++) buf[len++] = head[i];
    if (secs) {
        char digs[12];
        unsigned n = 0, v = secs;
        do { digs[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n) buf[len++] = digs[--n];
        buf[len++] = ' ';
        buf[len++] = 's';
        buf[len++] = ':';
    }
    for (unsigned id = 0; id < 9; id++) {
        if (!g_calls[id]) continue;
        buf[len++] = ' ';
        for (unsigned k = 0; g_names[id][k]; k++) buf[len++] = g_names[id][k];
        buf[len++] = '=';
        char digs[12];
        unsigned n = 0, v = g_calls[id];
        do { digs[n++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (n) buf[len++] = digs[--n];
        if (len > sizeof buf - 40) break;
    }
    buf[len] = 0;
    SetFilePointer(g_log, 0, NULL, FILE_END);
    log_line(buf);
}

static DWORD WINAPI summary_thread(LPVOID unused) {
    (void)unused;
    for (unsigned i = 1;; i++) {
        Sleep(10000);
        summary(i * 10);
    }
    return 0;
}

static void open_log(void) {
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("RG_WAJV_LOG", path, sizeof path);
    if (n == 0 || n >= sizeof path) {
        if (!GetModuleFileNameA(NULL, path, sizeof path)) return;
        int i = 0, last = -1;
        while (path[i]) { if (path[i] == '\\' || path[i] == '/') last = i; i++; }
        const char *name = "rg-wajvio.log";
        int k = 0;
        while (name[k] && last + 1 + k < (int)sizeof path - 1) { path[last + 1 + k] = name[k]; k++; }
        path[last + 1 + k] = 0;
    }
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void refresh(void) {
    if (!g_shm) {
        HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (h) g_shm = (const volatile uint8_t *)MapViewOfFile(h, FILE_MAP_READ, 0, 0, RG_SHM_SIZE);
    }
    uint8_t copy[RG_SHM_SIZE];
    if (g_shm) for (unsigned i = 0; i < RG_SHM_SIZE; i++) copy[i] = g_shm[i];
    rg_wajv_fill(g_block, g_shm ? copy : NULL, &g_state);
}

#define EXP __declspec(dllexport)

/* BOOL WAJVOpen(const char *port): de poortnaam doet er niet toe, er is geen poort. */
EXP BOOL WAJVOpen(const char *port) {
    (void)port;
    trace(0, "WAJVOpen");
    g_open = 1;
    refresh();
    return TRUE;
}

EXP BOOL WAJVClose(void) { trace(1, "WAJVClose"); g_open = 0; return TRUE; }

/* Origineel: 0 = klaar, 1 / 2 = tussenstanden, -1 = niet geopend, -2 = fout. */
EXP int WAJVGetStatus(void) { trace(2, "WAJVGetStatus"); return g_open ? g_status : -1; }

EXP int WAJVGetNodeCount(void) { trace(3, "WAJVGetNodeCount"); return g_open ? 1 : 0; }

/* Knooppunt 1 is het I/O-bord; het blok blijft geldig tot de volgende aanroep. */
EXP void *WAJVGetInput(int node) {
    trace(4, "WAJVGetInput");
    if (node != 1 || !g_open) return NULL;
    refresh();
    return g_block;
}

EXP void WAJVUpdate(void) { trace(5, "WAJVUpdate"); refresh(); }

EXP BOOL WAJVSetOutput(int node, const void *out) { (void)node; (void)out; trace(6, "WAJVSetOutput"); return TRUE; }

EXP BOOL WAJVSetFunctionSettings(int node, int count, const void *table) {
    (void)node; (void)count; (void)table;
    trace(7, "WAJVSetFunctionSettings");
    return TRUE;
}

EXP void WAJVSetPLParm(int a, int b, int c) { (void)a; (void)b; (void)c; trace(8, "WAJVSetPLParm"); }

EXP BOOL WAJVEnableWatchDogTimer(int on) { (void)on; return TRUE; }
EXP const char *WAJVGetLibraryVersion(void) { return "NBGI.;WinArc;Ver2.1.1;JPN"; }
EXP int WAJVGetRetryCount(void) { return 0; }
EXP BOOL WAJVRestartPL(void) { return TRUE; }
EXP BOOL WAJVSetAsyncComm(int on) { (void)on; return TRUE; }
EXP int WAJVSioRead(void *buf, int len) { (void)buf; (void)len; return 0; }
EXP BOOL WAJVSioSetMode(int mode) { (void)mode; return TRUE; }
EXP int WAJVSioWrite(const void *buf, int len) { (void)buf; (void)len; return 0; }
EXP BOOL WAJVUseSio(int on) { (void)on; return TRUE; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        open_log();
        {
            char v[16];
            DWORD n = GetEnvironmentVariableA("RG_WAJV_STATUS", v, sizeof v);
            if (n > 0 && n < sizeof v) {
                int sign = 1, k = 0, val = 0;
                if (v[0] == '-') { sign = -1; k = 1; }
                for (; v[k] >= '0' && v[k] <= '9'; k++) val = val * 10 + (v[k] - '0');
                g_status = sign * val;
            }
        }
        log_line("rg wajvio: geladen (vervangt wajvio.dll van Namco; geen seriele poort)");
        if (g_log != INVALID_HANDLE_VALUE) {
            HANDLE t = CreateThread(NULL, 0, &summary_thread, NULL, 0, NULL);
            if (t) CloseHandle(t);
        }
    }
    return TRUE;
}
