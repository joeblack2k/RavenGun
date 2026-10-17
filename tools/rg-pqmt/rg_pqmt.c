/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PQMTClient.dll van RavenGun: vervanging van de client van de PQ-Labs-multitouchserver voor Infinity Blade FX ("TouchFX" in
 * SwordGame.exe). De echte client praat via TCP met een server op 127.0.0.1 (de game geeft ConnectServer("<ip>", 0x5433)); zonder
 * server geeft ConnectServer een fout en blijft de game na "TouchFX Error: ConnectServer() failed" op een wit scherm staan
 * (gemeten op de tv, 2026-10-17). Deze DLL heeft dezelfde 16 exports (namen en ordinalen) maar praat met niemand: ConnectServer,
 * SendRequest en SendThreshold slagen (0 = geslaagd, zoals de game ze leest, SwordGame.exe 0x1cde140), en de punten die de game via
 * SetOnReceivePointFrame verwacht komen uit het gedeelde geheugen TeknoParrot_JvsState (indeling TimeCrisis5): trekker = aanraking,
 * X/Y-bytes = positie. De logica staat in pqmt_logic.h. Alles cdecl, behalve de operator= (thiscall).
 *
 * Omgeving:
 *   RG_PQMT_TRIGGER   masker van de aanrakingsknop in het woord op offset 8 (standaard 0x10 = p1.trigger)
 *   RG_PQMT_W, _H     vaste client-grootte in pixels; leeg = de rechthoek van het grootste zichtbare venster van het proces
 *   RG_PQMT_LOG       logbestand
 */
#include <windows.h>
#include <stdarg.h>

#include "pqmt_logic.h"

#define FC __attribute__((fastcall))

void* memset(void* d, int c, size_t n) {
    volatile unsigned char* p = (volatile unsigned char*)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void* memcpy(void* d, const void* s, size_t n) {
    volatile unsigned char* p = (volatile unsigned char*)d;
    const unsigned char* q = (const unsigned char*)s;
    while (n--) *p++ = *q++;
    return d;
}

typedef void(__cdecl* pfunc_frame)(int frame_id, int time_stamp, int count, const pq_punt* punten, void* ctx);

static pfunc_frame g_frame_cb;
static void* g_frame_ctx;
static volatile LONG g_verbonden;
static volatile LONG g_draad_loopt;
static char g_log[MAX_PATH];
static uint32_t g_trekker = 0x10;
static int32_t g_vast_w, g_vast_h;

static unsigned long tot_getal(const char* s) {
    unsigned long v = 0;
    unsigned basis = 10;
    if (s[0] == '0' && (s[1] | 0x20) == 'x') { basis = 16; s += 2; }
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (basis == 16 && (*s | 0x20) >= 'a' && (*s | 0x20) <= 'f') d = (unsigned)((*s | 0x20) - 'a' + 10);
        else break;
        v = v * basis + d;
    }
    return v;
}

static void logf_(const char* fmt, ...) {
    if (!g_log[0]) return;
    HANDLE h = CreateFileA(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof buf - 3) n = 0;
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD w;
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    CloseHandle(h);
}

/* --- de grootte van het venster --------------------------------------------------------------------------------------- */
typedef struct { DWORD pid; LONG best_w, best_h; } zoek;
static BOOL CALLBACK venster_cb(HWND hw, LPARAM lp) {
    zoek* z = (zoek*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hw, &pid);
    if (pid != z->pid || !IsWindowVisible(hw)) return TRUE;
    RECT r;
    if (!GetClientRect(hw, &r)) return TRUE;
    const LONG w = r.right - r.left, h = r.bottom - r.top;
    if ((long long)w * h > (long long)z->best_w * z->best_h) { z->best_w = w; z->best_h = h; }
    return TRUE;
}
static void client_grootte(int32_t* w, int32_t* h) {
    if (g_vast_w > 0 && g_vast_h > 0) { *w = g_vast_w; *h = g_vast_h; return; }
    zoek z = {GetCurrentProcessId(), 0, 0};
    EnumWindows(venster_cb, (LPARAM)&z);
    *w = z.best_w > 1 ? z.best_w : 1920;
    *h = z.best_h > 1 ? z.best_h : 1080;
}

/* --- de lus die de punten aan de game geeft ------------------------------------------------------------------------- */
static DWORD WINAPI draad(LPVOID p) {
    (void)p;
    const volatile unsigned char* shm = NULL;
    pq_toestand t = {0, 0, 0, 0};
    int32_t laatste_w = 0, laatste_h = 0;
    for (;;) {
        Sleep(8);
        if (!g_verbonden || !g_frame_cb) continue;
        if (!shm) {
            HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
            if (!m) { Sleep(500); continue; }
            shm = (const volatile unsigned char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 64);
            if (!shm) continue;
            logf_("gedeeld geheugen TeknoParrot_JvsState geopend");
        }
        const uint32_t ctl = (uint32_t)shm[8] | ((uint32_t)shm[9] << 8) | ((uint32_t)shm[10] << 16) | ((uint32_t)shm[11] << 24);
        int32_t w, h;
        client_grootte(&w, &h);
        if (w != laatste_w || h != laatste_h) { logf_("client-rechthoek %d x %d", (int)w, (int)h); laatste_w = w; laatste_h = h; }
        pq_punt pt;
        int32_t frame;
        if (pq_stap(&t, ctl, g_trekker, shm[12], shm[16], w, h, &pt, &frame)) {
            logf_("frame %d: gebeurtenis %d x %d y %d", (int)frame, (int)pt.gebeurtenis, (int)pt.x, (int)pt.y);
            g_frame_cb((int)frame, (int)GetTickCount(), 1, &pt, g_frame_ctx);
        }
    }
    return 0;
}

/* --- de exports (volgorde en ordinalen van de echte PQMTClient.dll) ------------------------------------------------- */
/* operator= (thiscall: this in ecx, een argument op de stapel, ret 4); hier fastcall met een schaduw-edx. Kopieert n dwords. */
#define OPGELIJK(naam, dwords) \
    FC void* naam(void* self, int edx_schaduw, const void* ander) __asm__("_" #naam); \
    FC void* naam(void* self, int edx_schaduw, const void* ander) { \
        (void)edx_schaduw; \
        volatile unsigned* d = (volatile unsigned*)self; const unsigned* s = (const unsigned*)ander; \
        for (int i = 0; i < (dwords); i++) d[i] = s[i]; \
        return self; }
OPGELIJK(rg_op_clientrequest, 0x25)
OPGELIJK(rg_op_deviceinfo, 0x22)
OPGELIJK(rg_op_gesture, 0x0e)
OPGELIJK(rg_op_touchpoint, 4)

int rg_connect(const char* ip, int port) {
    (void)ip;
    logf_("ConnectServer(%d): nagebootst, geslaagd", port);
    if (!InterlockedExchange(&g_draad_loopt, 1)) CreateThread(NULL, 0, draad, NULL, 0, NULL);
    InterlockedExchange(&g_verbonden, 1);
    return 0;
}
int rg_disconnect(void) { InterlockedExchange(&g_verbonden, 0); return 0; }
const char* rg_gesturename(int g) { (void)g; return ""; }
int rg_getserverresolution(void* cb, void* obj) { (void)cb; (void)obj; return 0; }
int rg_sendrequest(const void* req) { (void)req; logf_("SendRequest: geslaagd"); return 0; }
int rg_sendthreshold(int t) { logf_("SendThreshold(%d): geslaagd", t); return 0; }
int rg_setondeviceinfo(void* cb, void* obj) { (void)cb; (void)obj; return 0; }
int rg_setonreceiveerror(void* cb, void* obj) { (void)cb; (void)obj; return 0; }
int rg_setonreceivegesture(void* cb, void* obj) { (void)cb; (void)obj; return 0; }
int rg_setonreceivepointframe(pfunc_frame cb, void* obj) {
    g_frame_cb = cb;
    g_frame_ctx = obj;
    logf_("SetOnReceivePointFrame: callback gezet");
    return 0;
}
int rg_setonserverbreak(void* cb, void* obj) { (void)cb; (void)obj; return 0; }
int rg_setrawdataresolution(int w, int h) { (void)w; (void)h; return 0; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID res) {
    (void)res;
    if (why != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    char tmp[64];
    if (GetEnvironmentVariableA("RG_PQMT_LOG", g_log, sizeof g_log) >= sizeof g_log) g_log[0] = 0;
    if (GetEnvironmentVariableA("RG_PQMT_TRIGGER", tmp, sizeof tmp) && tmp[0]) g_trekker = (uint32_t)tot_getal(tmp);
    if (GetEnvironmentVariableA("RG_PQMT_W", tmp, sizeof tmp) && tmp[0]) g_vast_w = (int32_t)tot_getal(tmp);
    if (GetEnvironmentVariableA("RG_PQMT_H", tmp, sizeof tmp) && tmp[0]) g_vast_h = (int32_t)tot_getal(tmp);
    logf_("PQMTClient.dll (RavenGun) geladen, aanrakingsmasker 0x%x", (unsigned)g_trekker);
    return TRUE;
}
