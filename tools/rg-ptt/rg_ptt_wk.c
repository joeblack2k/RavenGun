/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * rg_ptt_wk.c - WkWin32.dll van RavenGun voor Pull The Trigger (PTW.exe, 32-bit).
 *
 * WAAROM
 *   PTW.exe is met WibuKey ingepakt. De stub (.text, VA 0x4d2000) laadt WKWIN32.DLL, opent de WIBU-BOX met firmcode
 *   250010 en usercode 13, laat de doos een blok van 280 bytes ontsleutelen (de importtabel van het spel), en bouwt
 *   daarmee zelf de IAT. De code van het spel is niet versleuteld en roept de stub nooit aan (gemeten).
 *   Deze DLL is de doos: hij beantwoordt de negen aanroepen die de stub gebruikt en geeft bij WkbCrypt2 de platte
 *   tekst van de importtabel (ptt_imports.h). Er wordt geen sleutel, geen algoritme en geen andere dongle-inhoud
 *   nagebootst: alleen wat dit ene spel van deze ene doos nodig heeft.
 *
 * DE NEGEN AANROEPEN (per ordinaal opgezocht door de stub; naam uit de echte WkWin32.dll 3.x)
 *   33 WkbGetVersion2(0)                      -> AX; de stub wil (AL<<8 | AH) >= 0x300
 *   72 WkbGetVersion3(0)                      -> uitkomst wordt genegeerd
 *   45 WkbAccess2(0x101, &gegevens)           -> handvat != 0
 *   34 WkbOpen2(hnd, 0, 0x122, 0, 250010, 13) -> handvat != 0
 *   47 WkbQueryEntry2(hnd, 0x120, buf, 0xd0)  -> niet aangeroepen (tellers 0), toch beantwoord
 *   37 WkbCrypt2(hnd, 0x102, buf, par, grootte, 0) -> != 0; bij 280 bytes versleutelde importtabel: platte tekst
 *   35 WkbClose2(hnd)  46 WkbRelease2(hnd)  20 WkbGetLastError()
 *
 * Alleen kernel32, geen C-runtime. Bouwen: build-wkwin32.sh. Logboek: rg-wkwin32.log naast de DLL (laadbewijs).
 */
#include <windows.h>
#include "rg_ptt_wk_logic.h"

#define H_ACCESS ((HANDLE)0x00005001)
#define H_OPEN   ((HANDLE)0x00005002)

static char g_log[MAX_PATH];
static LONG g_seen[16];

static void log_line(const char *s)
{
    HANDLE f;
    DWORD n;
    unsigned len = 0;
    if (!g_log[0]) return;
    while (s[len]) len++;
    f = CreateFileA(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, s, len, &n, NULL);
    WriteFile(f, "\r\n", 2, &n, NULL);
    CloseHandle(f);
}

/* Alleen de eerste aanroep van elke functie loggen. */
static void first(int i, const char *s)
{
    if (i >= 0 && i < 16 && InterlockedExchange(&g_seen[i], 1) == 0) log_line(s);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID r)
{
    (void)r;
    if (why == DLL_PROCESS_ATTACH) {
        DWORD n = GetModuleFileNameA(h, g_log, MAX_PATH - 16);
        const char *t = "rg-wkwin32.log";
        unsigned i = 0;
        while (n > 0 && g_log[n - 1] != '\\' && g_log[n - 1] != '/') n--;
        while (t[i]) { g_log[n + i] = t[i]; i++; }
        g_log[n + i] = 0;
        log_line("rg-wkwin32: geladen (vervanger van WkWin32.dll voor Pull The Trigger)");
    }
    return TRUE;
}

WORD WINAPI rg_WkbGetVersion2(DWORD a) { (void)a; first(0, "WkbGetVersion2 -> 0x3003"); return 0x3003; }
DWORD WINAPI rg_WkbGetVersion3(DWORD a) { (void)a; first(1, "WkbGetVersion3 -> 0"); return 0; }
HANDLE WINAPI rg_WkbAccess2(DWORD a, void *b) { (void)a; (void)b; first(2, "WkbAccess2 -> handvat"); return H_ACCESS; }
HANDLE WINAPI rg_WkbOpen2(HANDLE h, DWORD a, DWORD flags, DWORD b, DWORD firm, DWORD user)
{
    (void)h; (void)a; (void)flags; (void)b;
    if (InterlockedExchange(&g_seen[3], 1) == 0) {
        char t[80];
        char *p = t;
        const char *k = "WkbOpen2 firm:user geaccepteerd: ";
        unsigned i;
        while (*k) *p++ = *k++;
        for (i = 0; i < 2; i++) {
            char tmp[12];
            int n = 0;
            unsigned v = i ? user : firm;
            do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
            while (n) *p++ = tmp[--n];
            if (!i) *p++ = ':';
        }
        *p = 0;
        log_line(t);
    }
    return H_OPEN;
}
DWORD WINAPI rg_WkbQueryEntry2(HANDLE h, DWORD cmd, void *buf, DWORD len)
{
    (void)h; (void)cmd;
    first(4, "WkbQueryEntry2 -> leeg");
    if (buf) { unsigned char *b = (unsigned char *)buf; DWORD i; for (i = 0; i < len; i++) b[i] = 0; }
    return 1;
}
DWORD WINAPI rg_WkbCrypt2(HANDLE h, DWORD cmd, void *buf, void *par, DWORD size, DWORD z)
{
    int r;
    (void)h; (void)cmd; (void)par; (void)z;
    r = ptt_crypt((unsigned char *)buf, size);
    if (r == PTT_CRYPT_PLAIN) first(5, "WkbCrypt2: importtabel (280 bytes) vervangen door de platte tekst");
    else if (r == PTT_CRYPT_SAME) first(6, "WkbCrypt2: importtabel al plat");
    else first(7, "WkbCrypt2: ander blok (controlevraag) beantwoord zonder wijziging");
    return 1;
}
DWORD WINAPI rg_WkbClose2(HANDLE h) { (void)h; first(8, "WkbClose2"); return 1; }
DWORD WINAPI rg_WkbRelease2(HANDLE h) { (void)h; first(9, "WkbRelease2"); return 1; }
DWORD WINAPI rg_WkbGetLastError(void) { return 0; }
