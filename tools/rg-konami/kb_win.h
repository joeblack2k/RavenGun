/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-konami: het Windows-deel dat elke DLL deelt (elke DLL neemt dit bestand op en heeft zijn eigen kopie van de toestand):
 *   - het bordbeeld: de mapping "TeknoParrot_JvsState" van rg-jvsbridge.exe in de prefix, lui geopend (elke 500 ms een nieuwe poging
 *     zolang hij er niet is; de brug kan later starten dan de game);
 *   - het logboek: RG_KONAMI_LOG=pad (relatief = naast deze DLL, dus in de view en nooit op de NAS); anders OutputDebugString.
 *
 * Zonder C-runtime (zoals tools/rg-pqmt en tools/rg-kaboom): alleen kernel32 en user32 (wvsprintfA), zodat de DLL in elke 32-bits
 * prefix laadt, ook zonder ucrtbase. Daarom hier ook memset/memcpy (de compiler roept ze zelf aan voor structuurkopieen).
 */
#ifndef RG_KONAMI_WIN_H
#define RG_KONAMI_WIN_H

#include <windows.h>
#include <stdarg.h>

#include "konami_logic.h"

#ifndef KB_NAAM
#error "definieer KB_NAAM (de naam van de DLL in het logboek)"
#endif

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

static size_t kb_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Kopieert hoogstens lengte - 1 tekens en sluit altijd af (als lengte > 0). */
static void kb_kopieer(char *d, size_t lengte, const char *s)
{
    if (!d || !lengte) return;
    size_t i = 0;
    for (; s[i] && i + 1 < lengte; i++) d[i] = s[i];
    d[i] = 0;
}

static HMODULE kb_deze;
static CRITICAL_SECTION kb_slot;
static const volatile uint8_t *kb_map;
static DWORD kb_volgende_poging;
static int kb_meldingen;   /* begrenst het aantal invoerregels in het logboek */

/* Formaat zoals wsprintf: %d %u %x %s %c (geen %f en geen %p). */
static void kb_log(const char *fmt, ...)
{
    char regel[1100];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(regel, fmt, ap);   /* hoogstens 1024 tekens */
    va_end(ap);
    if (n < 0) return;
    regel[n] = '\r';
    regel[n + 1] = '\n';
    regel[n + 2] = 0;
    char pad[MAX_PATH + 64];
    DWORD lp = GetEnvironmentVariableA("RG_KONAMI_LOG", pad, MAX_PATH);
    if (lp == 0 || lp >= MAX_PATH) {
        OutputDebugStringA(KB_NAAM ": ");
        OutputDebugStringA(regel);
        return;
    }
    if (!(pad[0] == '\\' || pad[0] == '/' || pad[1] == ':')) {   /* relatief: naast deze DLL */
        char vol[MAX_PATH + 64];
        DWORD m = GetModuleFileNameA(kb_deze, vol, MAX_PATH);
        char *schuin = NULL;
        for (DWORD i = 0; i < m; i++) if (vol[i] == '\\' || vol[i] == '/') schuin = vol + i;
        if (schuin && kb_strlen(pad) < 60) {
            kb_kopieer(schuin + 1, 64, pad);
            kb_kopieer(pad, sizeof pad, vol);
        }
    }
    HANDLE f = CreateFileA(pad, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, KB_NAAM ": ", (DWORD)kb_strlen(KB_NAAM ": "), &w, NULL);
    WriteFile(f, regel, (DWORD)kb_strlen(regel), &w, NULL);
    CloseHandle(f);
}

/* Leest het bordbeeld; zonder mapping een leeg beeld (alles los, niemand in beeld). */
static void kb_beeld_nu(kb_beeld *b)
{
    if (!kb_map && GetTickCount() - kb_volgende_poging < 0x80000000u) {
        HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (m) {
            kb_map = (const volatile uint8_t *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 64);
            if (kb_map) kb_log("bordbeeld TeknoParrot_JvsState geopend");
        }
        kb_volgende_poging = GetTickCount() + 500;
    }
    kb_lees(kb_map, b);
}

static void kb_dllmain_attach(HINSTANCE h)
{
    kb_deze = h;
    DisableThreadLibraryCalls(h);
    InitializeCriticalSection(&kb_slot);
    kb_volgende_poging = GetTickCount();
}

/* De eerste regel van het logboek, bij de eerste aanroep van de game (niet in DllMain: daar geen bestanden onder de loader-lock). */
static void kb_meld_geladen(const char *wat)
{
    static int gemeld;
    if (gemeld) return;
    gemeld = 1;
    char pad[MAX_PATH];
    if (!GetModuleFileNameA(kb_deze, pad, MAX_PATH)) kb_kopieer(pad, sizeof pad, "?");
    kb_log("geladen: %s (RavenGun, zonder TeknoParrot); eerste aanroep: %s", pad, wat);
}

/* De exports staan in de .def-bestanden (de C++-namen van Konami, met dezelfde ordinalen als het origineel); de C-namen hieronder
 * worden niet zelf geexporteerd. */

#endif
