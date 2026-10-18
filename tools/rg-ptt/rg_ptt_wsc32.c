/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * rg_ptt_wsc32.c - wsc32.dll van RavenGun voor Pull The Trigger (PTW.exe, 32-bit): de seriele poorten zonder poorten.
 *
 * WAAROM
 *   PTW.exe praat via WSC32.dll (een seriele-poortbibliotheek, 13 ordinalen, stdcall) met twee dingen: de geheugenkaartlezer op
 *   poort 0 (COM1) en het geweerbord op poort 1 (COM2). Het echte WSC32 opent ze met CreateFileA("COMx"); onder Wine bestaan ze niet,
 *   SioReset geeft dan < 0 en het spel toont "Memory Card Port Open Error!" (0x41e87c, gemeten 2026-10-18). Deze DLL is WSC32: hij
 *   staat in de view op de plaats van het echte bestand (drop-in) en bedient beide poorten zelf; er komt nooit een COM-poort aan te pas.
 *
 * WAT
 *   Poort 1 (geweerbord): ptt_board.h (pakketten, commando's, beeld uit het gedeelde geheugen `TeknoParrot_JvsState`, tp-shm-indeling
 *   RawThrillsGUN). SioMessage(poort, venster, 0x400, 1) wordt nagedaan met PostMessageA: het spel leest zijn pakketten in de
 *   vensterprocedure (0x41ecb4) bij bericht 0x400. Poort 0 (geheugenkaart): opent, blijft leeg, CTS actief = "geen kaart".
 *   De negen andere exports (SioStatus enz.) bestaan alleen zodat de ordinalen kloppen; het spel gebruikt ze niet.
 *
 * ORDINALEN (gemeten aan de echte wsc32.dll en aan de importtabel van PTW.exe, IAT 0x4b1364..0x4b1394):
 *   17 Reset 25 TxQue 8 DTR 3 CTS 19 RTS 29 Message 1 Baud 9 Flow 12 Info 20 RxClear 11 Gets 6 Done 15 Puts
 *
 * LOGBOEK rg-wsc32.log naast de DLL: het laden, elke eerste aanroep met argumenten, het openen van het gedeelde geheugen, de eerste
 * pakketten in beide richtingen en elke 5 s een tellerregel. Zonder dat logboek is de DLL niet geladen.
 * OMGEVING  RG_PTT_SHM (naam van het gedeelde geheugen), RG_PTT_GUN_PORT (standaard 1)
 *
 * Alleen kernel32 en user32 (wsprintfA, PostMessageA), geen C-runtime. Bouwen: build-wsc32.sh.
 */
#include <windows.h>
#include "ptt_board.h"

#define NPORT 4

typedef struct {
    int open;
    HWND hwnd;
    UINT msg;
    DWORD mask;
    int pending;      /* er is al een bericht verstuurd dat het spel nog niet met SioMessage heeft bevestigd */
} port_t;

static char g_log[MAX_PATH];
static CRITICAL_SECTION g_cs;
static int g_cs_ready;
static port_t g_port[NPORT];
static ptt_board g_board;
static int g_gun_port = PTT_PORT_GUN;
static char g_shm_name[128] = "TeknoParrot_JvsState";
static HANDLE g_map;
static const unsigned char *g_view;
static HANDLE g_thread;
static volatile LONG g_stop;
static LONG g_seen[40];
static unsigned g_logged_tx, g_logged_rx;
static unsigned g_posted, g_gets_calls, g_gets_bytes;
static unsigned g_msg_logged;
static HWND g_hwnd;                       /* het spelvenster (laatste niet-nul venster van SioMessage) */
/* MUNT. Het spel kent geen munt-pakket (statisch: alleen de toetsen uit de tabel op 0x41e889 en de muis). Een muntdruk uit de shm wordt
 * daarom een toetsdruk naar het spelvenster. Welke toets de munt telt is niet vastgesteld: standaard een PROEF die per muntdruk de volgende
 * kandidaat stuurt (en logt); RG_PTT_COIN_VK=<hex> kiest er een vast. */
static const unsigned g_coin_probe[] = { 0x35, 0x70, 0x71, 0x20, 0x24, 0x23, 0x2d, 0x2e, 0x21, 0x22, 0xbf, 0x53, 0x31, 0x32, 0x0d };
static unsigned g_coin_fixed;             /* 0 = proef */
static unsigned g_coin_n;
static UINT g_keyup_vk;
static DWORD g_keyup_at;

static void log_raw(const char *s, unsigned len)
{
    HANDLE f;
    DWORD n;
    if (!g_log[0]) return;
    f = CreateFileA(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, s, len, &n, NULL);
    WriteFile(f, "\r\n", 2, &n, NULL);
    CloseHandle(f);
}

static void logf(const char *fmt, ...)
{
    char line[512];
    int n;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    n = wvsprintfA(line, fmt, ap);
    __builtin_va_end(ap);
    if (n < 0) n = 0;
    log_raw(line, (unsigned)n);
}

/* Alleen de eerste aanroep van elke functie loggen (id < 40). Geeft 1 bij de eerste keer. */
static int first(int id)
{
    return id >= 0 && id < 40 && InterlockedExchange(&g_seen[id], 1) == 0;
}

static void hexline(char *out, const unsigned char *p, unsigned n)
{
    unsigned i;
    static const char hx[] = "0123456789abcdef";
    if (n > 24) n = 24;
    for (i = 0; i < n; i++) { out[3 * i] = hx[p[i] >> 4]; out[3 * i + 1] = hx[p[i] & 15]; out[3 * i + 2] = ' '; }
    out[3 * n] = 0;
}

/* --- het gedeelde geheugen en de draad die het bord voedt ---------------------------------------------------------------------- */

static void post_if_armed(int port)
{
    port_t *p = &g_port[port];
    if (p->open && p->hwnd && p->mask && !p->pending && ptt_board_rx_count(&g_board) > 0) {
        p->pending = 1;
        PostMessageA(p->hwnd, p->msg, (WPARAM)port, (LPARAM)p->mask);
        g_posted++;
    }
}

static void send_key(HWND h, unsigned vk, int down)
{
    LPARAM scan = (LPARAM)MapVirtualKeyA(vk, 0) << 16;
    PostMessageA(h, down ? WM_KEYDOWN : WM_KEYUP, (WPARAM)vk, down ? (1 | scan) : (LPARAM)(0xC0000001u | (DWORD)scan));
    if (down && vk >= 0x30 && vk <= 0x39) PostMessageA(h, WM_CHAR, (WPARAM)vk, 1 | scan);
}

static void coin_pulse(unsigned n)
{
    while (n--) {
        unsigned vk = g_coin_fixed ? g_coin_fixed : g_coin_probe[g_coin_n % (sizeof g_coin_probe / sizeof g_coin_probe[0])];
        g_coin_n++;
        if (!g_hwnd) { logf("munt #%u: nog geen spelvenster (SioMessage kreeg nog geen venster); toets 0x%02x niet gestuurd", g_coin_n, vk); continue; }
        if (g_keyup_vk) send_key(g_hwnd, g_keyup_vk, 0);
        send_key(g_hwnd, vk, 1);
        g_keyup_vk = vk;
        g_keyup_at = GetTickCount() + 60;
        logf("munt #%u: toets VK 0x%02x naar venster %p (%s)", g_coin_n, vk, g_hwnd, g_coin_fixed ? "vast" : "proef: kijk of COIN(S) omhoog gaat");
    }
}

static DWORD WINAPI poll_thread(LPVOID unused)
{
    unsigned char copy[PTT_SHM_SIZE];
    DWORD last_try = 0, last_stat = GetTickCount();
    int warned = 0;
    (void)unused;
    while (!g_stop) {
        unsigned i;
        if (!g_view) {
            DWORD now = GetTickCount();
            if (now - last_try >= 250) {
                last_try = now;
                g_map = OpenFileMappingA(FILE_MAP_READ, FALSE, g_shm_name);
                if (g_map) g_view = (const unsigned char *)MapViewOfFile(g_map, FILE_MAP_READ, 0, 0, PTT_SHM_SIZE);
                if (g_view) logf("gedeeld geheugen %s geopend", g_shm_name);
                else if (!warned) { warned = 1; logf("gedeeld geheugen %s niet te openen (error %lu); nieuwe pogingen elke 250 ms", g_shm_name, GetLastError()); }
            }
        }
        if (g_view) {
            for (i = 0; i < PTT_SHM_SIZE; i++) copy[i] = g_view[i];
            EnterCriticalSection(&g_cs);
            {
                unsigned before = g_board.n_pkt_f + g_board.n_pkt_x + g_board.n_pkt_d;
                ptt_board_poll(&g_board, copy);
                if (g_board.n_pkt_f + g_board.n_pkt_x + g_board.n_pkt_d > before && g_logged_rx < 30) {
                    logf("bord -> spel: pakketten F %u X %u d %u (shm: ctl %02x%02x x %u y %u)", g_board.n_pkt_f, g_board.n_pkt_x, g_board.n_pkt_d,
                         copy[PTT_OFF_CONTROL + 1], copy[PTT_OFF_CONTROL], copy[PTT_OFF_P1X], copy[PTT_OFF_P1Y]);
                    g_logged_rx++;
                }
                if (g_gun_port >= 0 && g_gun_port < NPORT) post_if_armed(g_gun_port);
                {
                    unsigned c = ptt_board_take_coin(&g_board);
                    if (c) coin_pulse(c);
                }
            }
            LeaveCriticalSection(&g_cs);
        }
        if (g_keyup_vk && g_hwnd && (LONG)(GetTickCount() - g_keyup_at) >= 0) { send_key(g_hwnd, g_keyup_vk, 0); g_keyup_vk = 0; }
        if (GetTickCount() - last_stat >= 5000) {
            last_stat = GetTickCount();
            EnterCriticalSection(&g_cs);
            logf("tellers: spel->bord T %u + %u G %u H %u B %u C %u rommel %u | bord->spel F %u X %u d %u t %u weggevallen %u | F overgeslagen (richting stil) %u, munt %u | berichten %u, SioGets %u (%u bytes)",
                 g_board.n_cmd[0], g_board.n_cmd[1], g_board.n_cmd[2], g_board.n_cmd[3], g_board.n_cmd[4], g_board.n_cmd[5], g_board.n_junk,
                 g_board.n_pkt_f, g_board.n_pkt_x, g_board.n_pkt_d, g_board.n_pkt_t, g_board.n_dropped, g_board.n_f_skipped, g_coin_n, g_posted, g_gets_calls, g_gets_bytes);
            LeaveCriticalSection(&g_cs);
        }
        Sleep(4);
    }
    return 0;
}

static void ensure_thread(void)
{
    if (g_thread) return;
    g_thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    logf("pollende draad %s", g_thread ? "gestart" : "NIET gestart");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID r)
{
    (void)r;
    if (why == DLL_PROCESS_ATTACH) {
        DWORD n = GetModuleFileNameA(h, g_log, MAX_PATH - 16);
        const char *t = "rg-wsc32.log";
        unsigned i = 0;
        char env[128];
        while (n > 0 && g_log[n - 1] != '\\' && g_log[n - 1] != '/') n--;
        while (t[i]) { g_log[n + i] = t[i]; i++; }
        g_log[n + i] = 0;
        InitializeCriticalSection(&g_cs);
        g_cs_ready = 1;
        ptt_board_init(&g_board);
        if (GetEnvironmentVariableA("RG_PTT_SHM", env, sizeof env) > 0 && env[0]) lstrcpynA(g_shm_name, env, sizeof g_shm_name);
        if (GetEnvironmentVariableA("RG_PTT_GUN_PORT", env, sizeof env) > 0 && env[0] >= '0' && env[0] <= '9') g_gun_port = env[0] - '0';
        if (GetEnvironmentVariableA("RG_PTT_COIN_VK", env, sizeof env) > 0 && env[0]) {
            unsigned v = 0;
            const char *q = env;
            if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) q += 2;
            for (; *q; q++) v = v * 16 + (unsigned)(*q >= 'a' ? *q - 'a' + 10 : *q >= 'A' ? *q - 'A' + 10 : *q - '0');
            g_coin_fixed = v;
        }
        logf("rg-wsc32: geladen (vervanger van wsc32.dll voor Pull The Trigger); geweerbord op poort %d (COM%d), geheugenkaart op poort %d, shm %s",
             g_gun_port, g_gun_port + 1, PTT_PORT_CARD, g_shm_name);
    } else if (why == DLL_PROCESS_DETACH) {
        g_stop = 1;
    }
    return TRUE;
}

#define PORT_OK(p) ((p) >= 0 && (p) < NPORT)

/* 17 SioReset(poort, rx, tx): 0 = gelukt (negatief = fout, dat is wat het spel op Wine zonder COM-poort kreeg). */
int WINAPI rg_SioReset(int port, int rx, int tx)
{
    if (port == -1) { logf("SioReset(-1, %d, %d) -> 0 (0x41e80b, voor de poort-opens: alle poorten/bibliotheek; genegeerd)", rx, tx); return 0; }
    if (!PORT_OK(port)) { logf("SioReset(%d, %d, %d) -> -1 (ongeldige poort)", port, rx, tx); return -1; }
    EnterCriticalSection(&g_cs);
    g_port[port].open = 1;
    g_port[port].pending = 0;
    if (port == g_gun_port) ptt_board_rx_clear(&g_board);
    LeaveCriticalSection(&g_cs);
    logf("SioReset(poort %d = COM%d, rx %d, tx %d) -> 0%s", port, port + 1, rx, tx, port == g_gun_port ? " [geweerbord]" : port == PTT_PORT_CARD ? " [geheugenkaart]" : "");
    if (port == g_gun_port) ensure_thread();
    return 0;
}
int WINAPI rg_SioDone(int port)
{
    if (PORT_OK(port)) { EnterCriticalSection(&g_cs); g_port[port].open = 0; g_port[port].hwnd = 0; LeaveCriticalSection(&g_cs); }
    logf("SioDone(poort %d)", port);
    return 0;
}
int WINAPI rg_SioBaud(int port, int code) { if (first(2 + (port & 3))) logf("SioBaud(poort %d, code %d) -> 0", port, code); return 0; }
int WINAPI rg_SioFlow(int port, int mode) { if (first(6 + (port & 3))) logf("SioFlow(poort %d, '%c') -> 0", port, mode >= 32 && mode < 127 ? mode : '?'); return 0; }
int WINAPI rg_SioParms(int port, int parity, int stop, int bits) { (void)port; (void)parity; (void)stop; (void)bits; return 0; }
int WINAPI rg_SioInfo(int what)
{
    if (first(10)) logf("SioInfo('%c') -> 0x0510 (versie)", what >= 32 && what < 127 ? what : '?');
    return 0x0510;
}
int WINAPI rg_SioRxClear(int port)
{
    if (PORT_OK(port) && port == g_gun_port) { EnterCriticalSection(&g_cs); ptt_board_rx_clear(&g_board); LeaveCriticalSection(&g_cs); }
    if (first(11 + (port & 3))) logf("SioRxClear(poort %d) -> 0", port);
    return 0;
}
int WINAPI rg_SioTxQue(int port) { (void)port; return 0; }   /* 0 = niets meer te zenden; het spel (0x47f56c) leest !=0 als fout */
int WINAPI rg_SioTxClear(int port) { (void)port; return 0; }
int WINAPI rg_SioRxQue(int port)
{
    int n = 0;
    if (PORT_OK(port) && port == g_gun_port) { EnterCriticalSection(&g_cs); n = (int)ptt_board_rx_count(&g_board); LeaveCriticalSection(&g_cs); }
    return n;
}

/* 29 SioMessage(poort, venster, bericht, masker): masker 1 = bericht bij binnenkomende tekens. Het spel zet het uit (venster 0), wacht
 * even en zet het weer aan (0x47d5c9..0x47d5f2), en bevestigt na elke verwerking opnieuw (0x47dbd0). Staan er dan al bytes klaar,
 * dan gaat er meteen een bericht uit. */
int WINAPI rg_SioMessage(int port, HWND hwnd, UINT msg, DWORD mask)
{
    if (!PORT_OK(port)) return -1;
    EnterCriticalSection(&g_cs);
    g_port[port].hwnd = hwnd;
    if (hwnd) g_hwnd = hwnd;
    g_port[port].msg = msg;
    g_port[port].mask = mask;
    g_port[port].pending = 0;
    if (port == g_gun_port) post_if_armed(port);
    LeaveCriticalSection(&g_cs);
    if (g_msg_logged++ < 8) logf("SioMessage(poort %d, venster %p, bericht 0x%x, masker %lu) -> 0", port, hwnd, msg, mask);
    return 0;
}

/* 11 SioGets(poort, buffer, lengte): aantal gelezen bytes. */
int WINAPI rg_SioGets(int port, unsigned char *buf, unsigned len)
{
    unsigned n = 0;
    if (PORT_OK(port) && port == g_gun_port && buf) {
        EnterCriticalSection(&g_cs);
        n = ptt_board_rx_get(&g_board, buf, len);
        g_gets_calls++;
        g_gets_bytes += n;
        LeaveCriticalSection(&g_cs);
    }
    if (n && first(19)) logf("SioGets(poort %d, %u) eerste keer met data: %u bytes", port, len, n);
    return (int)n;
}

/* 15 SioPuts(poort, buffer, lengte): aantal geschreven bytes. */
int WINAPI rg_SioPuts(int port, const unsigned char *buf, unsigned len)
{
    if (PORT_OK(port) && port == g_gun_port && buf) {
        unsigned cmds;
        EnterCriticalSection(&g_cs);
        cmds = ptt_board_tx(&g_board, buf, len);
        if (g_logged_tx < 20) {
            char hex[100];
            hexline(hex, buf, len);
            logf("spel -> bord: %s(%u bytes, %u commando's)", hex, len, cmds);
            g_logged_tx++;
        }
        post_if_armed(port);
        LeaveCriticalSection(&g_cs);
    } else if (first(20 + (port & 3))) {
        logf("SioPuts(poort %d, %u bytes) op een poort zonder bord: genegeerd", port, len);
    }
    return (int)len;
}

/* 8 DTR, 19 RTS: stuurlijn zetten/lezen; het spel (geheugenkaart) leest alleen "niet -1/-3". 3 CTS: 1 = actief. */
int WINAPI rg_SioDTR(int port, int cmd) { (void)cmd; if (first(24 + (port & 3))) logf("SioDTR(poort %d, '%c') -> 0 (eerste aanroep)", port, cmd >= 32 && cmd < 127 ? cmd : '?'); return 0; }
int WINAPI rg_SioRTS(int port, int cmd) { (void)cmd; if (first(28 + (port & 3))) logf("SioRTS(poort %d, '%c') -> 0 (eerste aanroep)", port, cmd >= 32 && cmd < 127 ? cmd : '?'); return 0; }
int WINAPI rg_SioCTS(int port) { if (first(32 + (port & 3))) logf("SioCTS(poort %d) -> %d (eerste aanroep; 1 = altijd actief = geen kaart)", port, ptt_card_cts()); return ptt_card_cts(); }

/* De rest bestaat alleen voor de ordinalen. */
int WINAPI rg_SioBrkSig(int port, int cmd) { (void)port; (void)cmd; return 0; }
int WINAPI rg_SioDCD(int port) { (void)port; return 0; }
int WINAPI rg_SioDebug(int flag) { (void)flag; return 0; }
int WINAPI rg_SioDSR(int port) { (void)port; return 0; }
int WINAPI rg_SioEvent(int port, DWORD mask) { (void)port; (void)mask; return 0; }
int WINAPI rg_SioGetc(int port) { (void)port; return -1; }
int WINAPI rg_SioPutc(int port, int ch) { (void)port; (void)ch; return 0; }
int WINAPI rg_SioRead(int port, int code) { (void)port; (void)code; return 0; }
int WINAPI rg_SioRI(int port) { (void)port; return 0; }
int WINAPI rg_SioStatus(int port, DWORD mask) { (void)port; (void)mask; return 0; }
int WINAPI rg_SioTimer(int ms) { (void)ms; return 0; }
int WINAPI rg_SioUnGetc(int port, int ch) { (void)port; (void)ch; return 0; }
int WINAPI rg_SioWinError(char *buf, int len) { if (buf && len > 0) buf[0] = 0; return 0; }
