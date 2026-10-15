/*
 * shaiolib_rg.c - RavenGun replacement for Konami's shaiolib.dll
 * (Silent Hill: The Arcade, KSHG.exe, 2007).
 *
 * Exports the same 52 names as the original (see shaiolib.def). All cdecl,
 * int return, -1 = success/true, 0 = failure/false, as measured in the
 * original's export wrappers (0x10001180..0x10001ba2) and in KSHG.exe.
 *
 * This is a boot stub: no serial ports, no boards, no security plug.
 * The input bridge is rg_poll_inputs() below (tp-shm layout SilentHill);
 * it reads the named mapping TeknoParrot_JvsState of the RavenGun direct route.
 *
 * kernel32 only, no C runtime. Build: see build.sh.
 *
 * Environment:
 *   RG_SHAIO_MODEL  8-character security model code the "plug" answers to.
 *                   Default GNGHLAAA (see report: GKGHLEAA needs speaker.1).
 *   RG_SHAIO_LOG    log file path. Default: rg-shaiolib.log next to this DLL.
 */
#include <windows.h>

#define RG_OK   (-1)
#define RG_FAIL (0)

/* ------------------------------------------------------------------ */
/* tiny runtime (no CRT)                                               */

void *memset(void *d, int c, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

static unsigned rg_strlen(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static char *rg_puts(char *p, char *end, const char *s)
{
    while (*s && p < end) *p++ = *s++;
    return p;
}

static char *rg_putu(char *p, char *end, unsigned v)
{
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n && p < end) *p++ = tmp[--n];
    return p;
}

/* ------------------------------------------------------------------ */
/* export indices, in the original's export order (ordinal = index+1)  */

enum {
    E_10Key_disable, E_10Key_enable, E_10Key_status, E_BUTTON_status,
    E_CARD_start, E_CARD_status, E_CARD_status_clear, E_CARD_stop,
    E_COIN_blocker_close, E_COIN_blocker_open, E_COIN_consume, E_COIN_count,
    E_COIN_countup, E_DEBUG_GUN_realtime_pos, E_DEBUG_GetComPort1Buffer,
    E_DEBUG_GetComPort2Buffer, E_DEBUG_secure_get_eeprom_network,
    E_DEBUG_secure_get_eeprom_software, E_DEBUG_secure_get_id_network,
    E_DEBUG_secure_get_id_software, E_GUN_disable, E_GUN_enable,
    E_GUN_flash_off, E_GUN_flash_on, E_GUN_reaction,
    E_GUN_set_reaction_pattern, E_GUN_status, E_LED_change,
    E_QCMODE_get_error, E_QCMODE_secure_get_id_network,
    E_QCMODE_secure_get_id_software, E_QCMODE_secure_get_status_network,
    E_QCMODE_secure_get_status_software, E_SECURE_check,
    E_SECURE_check_no_error, E_SECURE_is_renewal, E_SECURE_renew,
    E_SYSINFO_get_hardware_id, E_SYSINFO_get_mother_board,
    E_SYSINFO_get_software_id, E_SYSINFO_get_system_id, E_get_lasterror,
    E_initialize, E_is_boot_all, E_is_boot_board_button, E_is_boot_board_gun,
    E_is_boot_comport, E_is_error, E_reset_error, E_start_boot, E_terminate,
    E_update, E_COUNT
};

static const char *const g_names[E_COUNT] = {
    "10Key_disable", "10Key_enable", "10Key_status", "BUTTON_status",
    "CARD_start", "CARD_status", "CARD_status_clear", "CARD_stop",
    "COIN_blocker_close", "COIN_blocker_open", "COIN_consume", "COIN_count",
    "COIN_countup", "DEBUG_GUN_realtime_pos", "DEBUG_GetComPort1Buffer",
    "DEBUG_GetComPort2Buffer", "DEBUG_secure_get_eeprom_network",
    "DEBUG_secure_get_eeprom_software", "DEBUG_secure_get_id_network",
    "DEBUG_secure_get_id_software", "GUN_disable", "GUN_enable",
    "GUN_flash_off", "GUN_flash_on", "GUN_reaction",
    "GUN_set_reaction_pattern", "GUN_status", "LED_change",
    "QCMODE_get_error", "QCMODE_secure_get_id_network",
    "QCMODE_secure_get_id_software", "QCMODE_secure_get_status_network",
    "QCMODE_secure_get_status_software", "SECURE_check",
    "SECURE_check_no_error", "SECURE_is_renewal", "SECURE_renew",
    "SYSINFO_get_hardware_id", "SYSINFO_get_mother_board",
    "SYSINFO_get_software_id", "SYSINFO_get_system_id", "get_lasterror",
    "initialize", "is_boot_all", "is_boot_board_button", "is_boot_board_gun",
    "is_boot_comport", "is_error", "reset_error", "start_boot", "terminate",
    "update"
};

/* ------------------------------------------------------------------ */
/* state                                                               */

#define RG_DEFAULT_MODEL "GNGHLAAA"

static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_cs;
static LONG g_cs_ready;
static LONG g_calls[E_COUNT];
static LONG g_seen[E_COUNT];
static LONG g_last_flush_calls[E_COUNT];
static DWORD g_last_flush_tick;
static char g_model[9] = RG_DEFAULT_MODEL;
static HINSTANCE g_self;

/* Synthetic IDs. KSHG.exe 0x4d0f40 does strchr(id, '-') on the returned
 * pointer without a NULL check and regroups it in fours into 28-byte
 * buffers, so these must be non-NULL, dash-free and at most 20 chars.
 * Shapes follow KSHG's built-in defaults at 0x6ce74c / 0x6ce768:
 * system id 20 chars ("0140" + 16 hex), hardware id 16 chars ("0100" + 12). */
static char g_system_id[]   = "01405247000000000001";
static char g_software_id[] = "01405247000000000002";
static char g_hardware_id[] = "0100524700000003";
static char g_mother_board[] = "A"; /* what the original returns once booted */

/* ------------------------------------------------------------------ */
/* log                                                                 */

static void rg_log_write(const char *buf, unsigned len)
{
    DWORD w;
    if (g_log == INVALID_HANDLE_VALUE) return;
    SetFilePointer(g_log, 0, NULL, FILE_END);
    WriteFile(g_log, buf, len, &w, NULL);
}

static void rg_log_line(const char *a, const char *b, const char *c)
{
    char buf[700];
    char *p = buf, *end = buf + sizeof(buf) - 2;
    p = rg_puts(p, end, "[rg-shaiolib t=");
    p = rg_putu(p, end, GetTickCount());
    p = rg_puts(p, end, "] ");
    if (a) p = rg_puts(p, end, a);
    if (b) p = rg_puts(p, end, b);
    if (c) p = rg_puts(p, end, c);
    *p++ = '\r'; *p++ = '\n';
    rg_log_write(buf, (unsigned)(p - buf));
}

static void rg_log_open(void)
{
    char path[MAX_PATH + 32];
    DWORD n = GetEnvironmentVariableA("RG_SHAIO_LOG", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        /* next to the DLL, never the current directory: the game changes
         * its current directory to Data\<lang> */
        char *slash = NULL, *p;
        n = GetModuleFileNameA(g_self, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return;
        for (p = path; *p; p++)
            if (*p == '\\' || *p == '/') slash = p;
        p = slash ? slash + 1 : path;
        memcpy(p, "rg-shaiolib.log", 16);
    }
    g_log = CreateFileA(path, GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    /* failure leaves g_log invalid; every writer checks that */
}

static void rg_flush_counters(int force)
{
    char buf[2048];
    char *p, *end = buf + sizeof(buf) - 2;
    int i, any = 0;
    DWORD now = GetTickCount();

    if (g_log == INVALID_HANDLE_VALUE || !g_cs_ready) return;
    if (!force && (DWORD)(now - g_last_flush_tick) < 2000) return;
    if (!force) {
        if (!TryEnterCriticalSection(&g_cs)) return;
    } else {
        EnterCriticalSection(&g_cs);
    }
    if (force || (DWORD)(now - g_last_flush_tick) >= 2000) {
        g_last_flush_tick = now;
        p = buf;
        p = rg_puts(p, end, "[rg-shaiolib t=");
        p = rg_putu(p, end, now);
        p = rg_puts(p, end, force ? "] counters(final):" : "] counters:");
        for (i = 0; i < E_COUNT; i++) {
            LONG c = g_calls[i];
            if (c == 0) continue;
            if (!force && c == g_last_flush_calls[i]) continue;
            g_last_flush_calls[i] = c;
            if ((unsigned)(end - p) < 64) break;
            *p++ = ' ';
            p = rg_puts(p, end, g_names[i]);
            *p++ = '=';
            p = rg_putu(p, end, (unsigned)c);
            any = 1;
        }
        if (any || force) {
            *p++ = '\r'; *p++ = '\n';
            rg_log_write(buf, (unsigned)(p - buf));
        }
    }
    LeaveCriticalSection(&g_cs);
}

static void rg_hit(int idx)
{
    InterlockedIncrement(&g_calls[idx]);
    if (InterlockedExchange(&g_seen[idx], 1) == 0)
        rg_log_line("first call: shaiolib_", g_names[idx], NULL);
    rg_flush_counters(0);
}

/* ------------------------------------------------------------------ */
/* input state                                                         */

struct rg_gun { int x, y, trigger, offscreen; };

static unsigned char g_buttons[9];   /* per byte: bit7 = held, bit0 = edge */
static unsigned char g_tenkey[12];   /* same bit scheme                    */
static struct rg_gun g_guns[2] = { { 320, 240, 0, 0 }, { 320, 240, 0, 0 } };
static int g_coins;

/*
 * ===================== INPUT BRIDGE GOES HERE =====================
 * Called once per shaiolib_update(), i.e. once per game frame
 * (KSHG.exe 0x4d2215). A future bridge reads the RavenGun daemon here
 * and fills g_buttons, g_tenkey, g_guns and g_coins.
 *
 * g_buttons[i] -> game mask (KSHG.exe 0x4d0700):
 *   [0] Test   [1] Service   [2] unidentified (mask 0x4)
 *   [3] P1 Start      [4] P2 Start      [5] P1 Option L
 *   [6] P1 Option R   [7] P2 Option L   [8] P2 Option R
 *   Measured 2026-10-15 in the game's own I/O CHECK > INPUT CHECK screen ([7] not yet: the bridge has no P2 Option L
 *   source); the earlier table had Start and Option R swapped, so START did nothing outside the coin path.
 * g_guns[p]: KSHG only reads x/y while trigger != 0 (0x4d1464), and
 *   offscreen != 0 marks the shot as off-screen (reload).
 * g_coins: KSHG reads the count and consumes it in the same frame
 *   (0x4d1218 / 0x4d121f).
 * Until then everything stays idle.
 * ==================================================================
 */
/*
 * The bridge (2026-10-15): the RavenGun daemon writes the TeknoParrot input record (tp-shm layout
 * "SilentHill", packaging/tp-shm-profiles.json, after TeknoParrotUi's SilentHillPipe) and the pipe
 * helper of rg-direct mirrors it into the named mapping "TeknoParrot_JvsState" inside the prefix.
 * Offset 8: Test 0x1, Service 0x2, P1 Start 0x4, P1 Trigger 0x8, P1 Option L 0x10, P1 Option R 0x20,
 * Coin 0x40, P2 Start 0x80, P2 Trigger 0x100, P2 Option L 0x200, P2 Option R 0x400, P1 Reload 0x800,
 * P2 Reload 0x1000. Offsets 12/16/20/24: P1 X/Y, P2 X/Y, one byte each, 0 = left/top.
 */
static const volatile unsigned char *g_shm;
static DWORD g_shm_next_try;
static DWORD g_prev_ctl;

static void rg_button(int idx, int held)
{
    const int was = (g_buttons[idx] & 0x80) != 0;
    g_buttons[idx] = (unsigned char)((held ? 0x80 : 0) | (held && !was ? 0x01 : 0));
}

static void rg_poll_inputs(void)
{
    DWORD ctl;
    int p;
    if (!g_shm) {
        HANDLE h;
        const DWORD now = GetTickCount();
        if ((LONG)(now - g_shm_next_try) < 0) return;
        g_shm_next_try = now + 1000;
        h = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (!h) return;
        g_shm = (const volatile unsigned char *)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 64);
        if (!g_shm) return;
    }
    ctl = (DWORD)g_shm[8] | ((DWORD)g_shm[9] << 8) | ((DWORD)g_shm[10] << 16) | ((DWORD)g_shm[11] << 24);
    rg_button(0, (ctl & 0x0001) != 0);   /* Test        */
    rg_button(1, (ctl & 0x0002) != 0);   /* Service     */
    rg_button(3, (ctl & 0x0004) != 0);   /* P1 Start    */
    rg_button(4, (ctl & 0x0080) != 0);   /* P2 Start    */
    rg_button(5, (ctl & 0x0010) != 0);   /* P1 Option L */
    rg_button(6, (ctl & 0x0020) != 0);   /* P1 Option R */
    rg_button(7, (ctl & 0x0200) != 0);   /* P2 Option L */
    rg_button(8, (ctl & 0x0400) != 0);   /* P2 Option R */
    if ((ctl & 0x0040) && !(g_prev_ctl & 0x0040) && g_coins < 99) g_coins++;
    for (p = 0; p < 2; p++) {
        const DWORD trig = p ? 0x0100 : 0x0008, rel = p ? 0x1000 : 0x0800;
        const unsigned bx = g_shm[12 + 8 * p], by = g_shm[16 + 8 * p];
        g_guns[p].x = (int)(bx * 640u / 256u);
        g_guns[p].y = (int)(by * 480u / 256u);
        g_guns[p].offscreen = (ctl & rel) != 0;
        g_guns[p].trigger = (ctl & (trig | rel)) != 0;   /* reload = a shot off-screen */
    }
    g_prev_ctl = ctl;
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */

int __cdecl shaiolib_initialize(void)        { rg_hit(E_initialize); return RG_OK; }
int __cdecl shaiolib_terminate(void)         { rg_hit(E_terminate);  return RG_OK; }
int __cdecl shaiolib_start_boot(int flag)    { (void)flag; rg_hit(E_start_boot); return RG_OK; }

int __cdecl shaiolib_update(void)
{
    rg_hit(E_update);
    rg_poll_inputs();
    return RG_OK;
}

int __cdecl shaiolib_is_boot_comport(void)      { rg_hit(E_is_boot_comport);      return RG_OK; }
int __cdecl shaiolib_is_boot_board_button(void) { rg_hit(E_is_boot_board_button); return RG_OK; }
int __cdecl shaiolib_is_boot_board_gun(void)    { rg_hit(E_is_boot_board_gun);    return RG_OK; }
int __cdecl shaiolib_is_boot_all(void)          { rg_hit(E_is_boot_all);          return RG_OK; }

int __cdecl shaiolib_is_error(void)    { rg_hit(E_is_error);    return RG_FAIL; }
int __cdecl shaiolib_reset_error(void) { rg_hit(E_reset_error); return RG_OK; }

/* The original returns 0 here when no error is pending (0x10001280).
 * KSHG's only caller (0x4db9e4) passes a 20-byte record: code string at
 * +0, subcode at +0x10. */
int __cdecl shaiolib_get_lasterror(void *out)
{
    rg_hit(E_get_lasterror);
    if (out) memset(out, 0, 20);
    return RG_FAIL;
}

/* ------------------------------------------------------------------ */
/* guns                                                                */

int __cdecl shaiolib_GUN_enable(void)             { rg_hit(E_GUN_enable);  return RG_OK; }
int __cdecl shaiolib_GUN_disable(void)            { rg_hit(E_GUN_disable); return RG_OK; }
int __cdecl shaiolib_GUN_flash_on(int player)     { (void)player; rg_hit(E_GUN_flash_on);  return RG_OK; }
int __cdecl shaiolib_GUN_flash_off(int player)    { (void)player; rg_hit(E_GUN_flash_off); return RG_OK; }
int __cdecl shaiolib_GUN_reaction(int player)     { (void)player; rg_hit(E_GUN_reaction);  return RG_OK; }

int __cdecl shaiolib_GUN_set_reaction_pattern(int player, void *pattern)
{
    (void)player; (void)pattern;
    rg_hit(E_GUN_set_reaction_pattern);
    return RG_OK;
}

int __cdecl shaiolib_GUN_status(int *out)
{
    rg_hit(E_GUN_status);
    if (!out) return RG_FAIL;
    memcpy(out, g_guns, sizeof(g_guns)); /* 32 bytes */
    return RG_OK;
}

int __cdecl shaiolib_DEBUG_GUN_realtime_pos(int *x1, int *y1, int *x2, int *y2)
{
    rg_hit(E_DEBUG_GUN_realtime_pos);
    if (x1) *x1 = g_guns[0].x;
    if (y1) *y1 = g_guns[0].y;
    if (x2) *x2 = g_guns[1].x;
    if (y2) *y2 = g_guns[1].y;
    return RG_OK;
}

/* ------------------------------------------------------------------ */
/* buttons, coins, 10-key, LEDs                                        */

int __cdecl shaiolib_BUTTON_status(unsigned char *out)
{
    rg_hit(E_BUTTON_status);
    if (!out) return RG_FAIL;
    memcpy(out, g_buttons, 9);
    return RG_OK;
}

int __cdecl shaiolib_10Key_enable(void)  { rg_hit(E_10Key_enable);  return RG_OK; }
int __cdecl shaiolib_10Key_disable(void) { rg_hit(E_10Key_disable); return RG_OK; }

int __cdecl shaiolib_10Key_status(unsigned char *out)
{
    rg_hit(E_10Key_status);
    if (!out) return RG_FAIL;
    memcpy(out, g_tenkey, 12);
    return RG_OK;
}

int __cdecl shaiolib_COIN_count(int *out)
{
    rg_hit(E_COIN_count);
    if (!out) return RG_FAIL;
    *out = g_coins;
    return RG_OK;
}

int __cdecl shaiolib_COIN_consume(int n)
{
    rg_hit(E_COIN_consume);
    if (n < 0 || n > 99 || n > g_coins) return RG_FAIL; /* as 0x10003d80 */
    g_coins -= n;
    return RG_OK;
}

int __cdecl shaiolib_COIN_countup(int enable)   { (void)enable; rg_hit(E_COIN_countup); return RG_OK; }
int __cdecl shaiolib_COIN_blocker_open(void)    { rg_hit(E_COIN_blocker_open);  return RG_OK; }
int __cdecl shaiolib_COIN_blocker_close(void)   { rg_hit(E_COIN_blocker_close); return RG_OK; }

int __cdecl shaiolib_LED_change(int idx, int on)
{
    (void)idx; (void)on;
    rg_hit(E_LED_change);
    return RG_OK;
}

/* ------------------------------------------------------------------ */
/* IC card                                                             */

int __cdecl shaiolib_CARD_start(int player) { (void)player; rg_hit(E_CARD_start); return RG_OK; }
int __cdecl shaiolib_CARD_stop(int player)  { (void)player; rg_hit(E_CARD_stop);  return RG_OK; }
int __cdecl shaiolib_CARD_status_clear(void) { rg_hit(E_CARD_status_clear); return RG_OK; }

/* KSHG (0x4d1c23) treats a non-zero first dword as "card present". */
int __cdecl shaiolib_CARD_status(int player, void *out)
{
    (void)player;
    rg_hit(E_CARD_status);
    if (!out) return RG_FAIL;
    memset(out, 0, 32);
    return RG_OK;
}

/* ------------------------------------------------------------------ */
/* security plug                                                       */

static int rg_model_matches(const char *model)
{
    int i;
    if (!model) return 0;
    for (i = 0; i < 8; i++) {
        if (model[i] == 0 || model[i] != g_model[i]) return 0;
    }
    return 1;
}

int __cdecl shaiolib_SECURE_renew(void)      { rg_hit(E_SECURE_renew);      return RG_OK; }
int __cdecl shaiolib_SECURE_is_renewal(void) { rg_hit(E_SECURE_is_renewal); return RG_OK; }

int __cdecl shaiolib_SECURE_check(const char *model)
{
    rg_hit(E_SECURE_check);
    return rg_model_matches(model) ? RG_OK : RG_FAIL;
}

int __cdecl shaiolib_SECURE_check_no_error(const char *model)
{
    rg_hit(E_SECURE_check_no_error);
    return rg_model_matches(model) ? RG_OK : RG_FAIL;
}

/* ------------------------------------------------------------------ */
/* system information (pointer returns)                                */

char *__cdecl shaiolib_SYSINFO_get_system_id(void)    { rg_hit(E_SYSINFO_get_system_id);    return g_system_id; }
char *__cdecl shaiolib_SYSINFO_get_software_id(void)  { rg_hit(E_SYSINFO_get_software_id);  return g_software_id; }
char *__cdecl shaiolib_SYSINFO_get_hardware_id(void)  { rg_hit(E_SYSINFO_get_hardware_id);  return g_hardware_id; }
char *__cdecl shaiolib_SYSINFO_get_mother_board(void) { rg_hit(E_SYSINFO_get_mother_board); return g_mother_board; }

/* ------------------------------------------------------------------ */
/* DEBUG_ / QCMODE_ exports (not imported by KSHG.exe)                 */

int __cdecl shaiolib_DEBUG_GetComPort1Buffer(void *a, int b)
{
    (void)a; (void)b;
    rg_hit(E_DEBUG_GetComPort1Buffer);
    return RG_OK;
}

int __cdecl shaiolib_DEBUG_GetComPort2Buffer(void *a, int b)
{
    (void)a; (void)b;
    rg_hit(E_DEBUG_GetComPort2Buffer);
    return RG_OK;
}

/* The original zero-fills 0x20 bytes for all four of these (0x10006f10). */
int __cdecl shaiolib_DEBUG_secure_get_eeprom_network(void *out)
{
    rg_hit(E_DEBUG_secure_get_eeprom_network);
    if (out) memset(out, 0, 32);
    return RG_OK;
}

int __cdecl shaiolib_DEBUG_secure_get_eeprom_software(void *out)
{
    rg_hit(E_DEBUG_secure_get_eeprom_software);
    if (out) memset(out, 0, 32);
    return RG_OK;
}

int __cdecl shaiolib_DEBUG_secure_get_id_network(void *out)
{
    rg_hit(E_DEBUG_secure_get_id_network);
    if (out) memset(out, 0, 32);
    return RG_OK;
}

int __cdecl shaiolib_DEBUG_secure_get_id_software(void *out)
{
    rg_hit(E_DEBUG_secure_get_id_software);
    if (out) memset(out, 0, 32);
    return RG_OK;
}

/* In the original these two share their code with the DEBUG_ pair. */
int __cdecl shaiolib_QCMODE_secure_get_id_network(void *out)
{
    rg_hit(E_QCMODE_secure_get_id_network);
    if (out) memset(out, 0, 32);
    return RG_OK;
}

int __cdecl shaiolib_QCMODE_secure_get_id_software(void *out)
{
    rg_hit(E_QCMODE_secure_get_id_software);
    if (out) memset(out, 0, 32);
    return RG_OK;
}

/* The original writes one status byte (0x10006fc0). */
int __cdecl shaiolib_QCMODE_secure_get_status_network(unsigned char *out)
{
    rg_hit(E_QCMODE_secure_get_status_network);
    if (out) *out = 0;
    return RG_OK;
}

int __cdecl shaiolib_QCMODE_secure_get_status_software(unsigned char *out)
{
    rg_hit(E_QCMODE_secure_get_status_software);
    if (out) *out = 0;
    return RG_OK;
}

int __cdecl shaiolib_QCMODE_get_error(int a)
{
    (void)a;
    rg_hit(E_QCMODE_get_error);
    return RG_OK;
}

/* ------------------------------------------------------------------ */
/* in-memory patches from a file next to the DLL                        */
/*
 * rg-shaiolib.patches, one patch per line:
 *     <address> <expected bytes> <new bytes>      (all hex; '#' starts a comment)
 * A patch is written only if the bytes at <address> are exactly the expected ones, and both byte
 * strings must have the same length. Every line is reported in the log: applied, or why not.
 * The patches are knowledge of one game build and live as DATA beside this DLL; nothing here knows
 * an address. They are applied in memory because the game checksums its own executable on disk.
 * This DLL is a static import of the game, so it is loaded before the game's first instruction runs.
 */
static int rg_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const char *rg_skip_blank(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    return p;
}

/* reads hex digit pairs into out[]; returns the number of bytes, or -1 on an odd/empty/too long run */
static int rg_read_bytes(const char **pp, const char *end, unsigned char *out, int max)
{
    const char *p = *pp;
    int n = 0;
    while (p + 1 < end && rg_hexval(p[0]) >= 0 && rg_hexval(p[1]) >= 0) {
        if (n >= max) return -1;
        out[n++] = (unsigned char)(rg_hexval(p[0]) * 16 + rg_hexval(p[1]));
        p += 2;
    }
    if (p < end && rg_hexval(*p) >= 0) return -1;   /* odd number of digits */
    *pp = p;
    return n ? n : -1;
}

static void rg_apply_patches(void)
{
    static char text[4096];
    char path[MAX_PATH + 32], msg[160];
    char *slash = NULL, *q;
    const char *p, *end, *eol;
    HANDLE f;
    DWORD n, got = 0;
    int applied = 0, skipped = 0;

    n = GetModuleFileNameA(g_self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    for (q = path; *q; q++)
        if (*q == '\\' || *q == '/') slash = q;
    q = slash ? slash + 1 : path;
    { const char *nm = "rg-shaiolib.patches"; while ((*q++ = *nm++) != 0) {} }
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { rg_log_line("patches: no rg-shaiolib.patches beside the DLL; nothing patched", NULL, NULL); return; }
    if (!ReadFile(f, text, sizeof(text) - 1, &got, NULL)) got = 0;
    CloseHandle(f);
    p = text; end = text + got;
    while (p < end) {
        unsigned char want[16], repl[16];
        unsigned addr = 0;
        int nw, nr, digits = 0, i, same = 1;
        DWORD old = 0, tmp = 0;
        char *m = msg, *mend = msg + sizeof(msg) - 1;

        for (eol = p; eol < end && *eol != '\n' && *eol != '\r'; eol++) {}
        p = rg_skip_blank(p, eol);
        if (p == eol || *p == '#') goto next;
        if (p + 1 < eol && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        while (p < eol && rg_hexval(*p) >= 0) { addr = addr * 16 + (unsigned)rg_hexval(*p); p++; digits++; }
        p = rg_skip_blank(p, eol);
        nw = rg_read_bytes(&p, eol, want, 16);
        p = rg_skip_blank(p, eol);
        nr = rg_read_bytes(&p, eol, repl, 16);
        m = rg_puts(m, mend, "patch at 0x");
        { static const char hx[] = "0123456789abcdef"; for (i = 28; i >= 0; i -= 4) if (m < mend) *m++ = hx[(addr >> i) & 15]; }
        if (!digits || nw <= 0 || nr != nw) { m = rg_puts(m, mend, ": SKIPPED, line not understood"); skipped++; goto report; }
        if (IsBadReadPtr((const void *)(ULONG_PTR)addr, (UINT_PTR)nw)) { m = rg_puts(m, mend, ": SKIPPED, address not readable"); skipped++; goto report; }
        for (i = 0; i < nw; i++) if (((const unsigned char *)(ULONG_PTR)addr)[i] != want[i]) same = 0;
        if (!same) { m = rg_puts(m, mend, ": SKIPPED, the bytes there are not the expected ones (another build?)"); skipped++; goto report; }
        if (!VirtualProtect((void *)(ULONG_PTR)addr, (SIZE_T)nw, PAGE_EXECUTE_READWRITE, &old)) { m = rg_puts(m, mend, ": SKIPPED, VirtualProtect failed"); skipped++; goto report; }
        for (i = 0; i < nw; i++) ((unsigned char *)(ULONG_PTR)addr)[i] = repl[i];
        VirtualProtect((void *)(ULONG_PTR)addr, (SIZE_T)nw, old, &tmp);
        FlushInstructionCache(GetCurrentProcess(), (const void *)(ULONG_PTR)addr, (SIZE_T)nw);
        m = rg_puts(m, mend, ": applied, ");
        m = rg_putu(m, mend, (unsigned)nw);
        m = rg_puts(m, mend, " byte(s)");
        applied++;
report:
        *m = 0;
        rg_log_line(msg, NULL, NULL);
next:
        p = eol;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
    }
    { char *m = msg, *mend = msg + sizeof(msg) - 1;
      m = rg_puts(m, mend, "patches: "); m = rg_putu(m, mend, (unsigned)applied); m = rg_puts(m, mend, " applied, ");
      m = rg_putu(m, mend, (unsigned)skipped); m = rg_puts(m, mend, " skipped"); *m = 0; rg_log_line(msg, NULL, NULL); }
}

/* ------------------------------------------------------------------ */
/* DLL entry                                                           */

static void rg_read_model(void)
{
    char tmp[32];
    DWORD n = GetEnvironmentVariableA("RG_SHAIO_MODEL", tmp, sizeof(tmp));
    if (n == 8 && rg_strlen(tmp) == 8) {
        memcpy(g_model, tmp, 8);
        g_model[8] = 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char line[MAX_PATH + 160];
        char mod[MAX_PATH];
        char *p = line, *end = line + sizeof(line) - 1;
        DWORD n;

        g_self = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        g_cs_ready = 1;
        g_last_flush_tick = GetTickCount();
        rg_read_model();
        rg_log_open();

        n = GetModuleFileNameA(inst, mod, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) mod[0] = 0;
        p = rg_puts(p, end, "loaded: pid=");
        p = rg_putu(p, end, GetCurrentProcessId());
        p = rg_puts(p, end, " module=");
        p = rg_puts(p, end, mod);
        p = rg_puts(p, end, " model=");
        p = rg_puts(p, end, g_model);
        p = rg_puts(p, end, " (RavenGun, input from tp-shm SilentHill)");
        *p = 0;
        rg_log_line(line, NULL, NULL);
        rg_apply_patches();
    } else if (reason == DLL_PROCESS_DETACH) {
        rg_flush_counters(1);
        rg_log_line("detach", NULL, NULL);
        if (g_log != INVALID_HANDLE_VALUE) {
            CloseHandle(g_log);
            g_log = INVALID_HANDLE_VALUE;
        }
        if (g_cs_ready) {
            g_cs_ready = 0;
            DeleteCriticalSection(&g_cs);
        }
    }
    return TRUE;
}
