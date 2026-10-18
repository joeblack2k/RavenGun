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
static DWORD kb_start_tick;   /* GetTickCount bij het laden: elke regel krijgt de tijd sinds de start van het proces */

/* Een getal als tekst (zonder opmaakfuncties: ook bruikbaar in DllMain). */
static void kb_getal(char *d, size_t lengte, unsigned long v, int basis)
{
    char t[16];
    int n = 0;
    do { unsigned c = (unsigned)(v % (unsigned)basis); t[n++] = (char)(c < 10 ? '0' + c : 'a' + c - 10); v /= (unsigned)basis; } while (v && n < 15);
    size_t i = 0;
    while (n > 0 && i + 1 < lengte) d[i++] = t[--n];
    if (lengte) d[i] = 0;
}

/* Plakt s achter d (d is een afgesloten tekst in een buffer van lengte bytes). */
static void kb_plak(char *d, size_t lengte, const char *s)
{
    size_t n = kb_strlen(d);
    if (n < lengte) kb_kopieer(d + n, lengte - n, s);
}

/* Schrijft een kant-en-klare regel (zonder regeleinde) naar het logboek. Alleen kernel32: veilig in DllMain.
 * RG_KONAMI_LOG=pad; een relatief pad is relatief aan de map van DEZE DLL (niet aan de werkmap van het proces). Lukt het openen
 * daar niet, dan %TEMP%\rg-konami.log. Zonder RG_KONAMI_LOG: OutputDebugString. */
static void kb_schrijf(const char *regel)
{
    char pad[MAX_PATH + 64];
    DWORD lp = GetEnvironmentVariableA("RG_KONAMI_LOG", pad, MAX_PATH);
    if (lp == 0 || lp >= MAX_PATH) {
        OutputDebugStringA(KB_NAAM ": ");
        OutputDebugStringA(regel);
        OutputDebugStringA("\n");
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
    if (f == INVALID_HANDLE_VALUE) {
        DWORD t = GetTempPathA(MAX_PATH, pad);
        if (t == 0 || t >= MAX_PATH) return;
        kb_plak(pad, sizeof pad, "rg-konami.log");
        f = CreateFileA(pad, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) return;
    }
    DWORD w;
    char tijd[32] = "[+";
    char ms[16];
    const DWORD verstreken = GetTickCount() - kb_start_tick;
    kb_getal(ms, sizeof ms, verstreken / 1000, 10);
    kb_plak(tijd, sizeof tijd, ms);
    kb_plak(tijd, sizeof tijd, ".");
    kb_getal(ms, sizeof ms, 1000 + verstreken % 1000, 10);
    kb_plak(tijd, sizeof tijd, ms + 1);
    kb_plak(tijd, sizeof tijd, " s] ");
    WriteFile(f, tijd, (DWORD)kb_strlen(tijd), &w, NULL);
    WriteFile(f, KB_NAAM ": ", (DWORD)kb_strlen(KB_NAAM ": "), &w, NULL);
    WriteFile(f, regel, (DWORD)kb_strlen(regel), &w, NULL);
    WriteFile(f, "\r\n", 2, &w, NULL);
    CloseHandle(f);
}

/* Formaat zoals wsprintf: %d %u %x %s %c (geen %f en geen %p). Niet in DllMain gebruiken (user32). */
static void kb_log(const char *fmt, ...)
{
    char regel[1100];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(regel, fmt, ap);   /* hoogstens 1024 tekens */
    va_end(ap);
    if (n < 0) return;
    regel[n] = 0;
    kb_schrijf(regel);
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

/* DllMain, DLL_PROCESS_ATTACH: de laadregel. Het bewijs dat Wine DEZE DLL in het spelproces laadde, ook als de game daarna nooit
 * een functie aanroept: welke DLL, in welk proces (exe) en met welke werkmap. Alleen kernel32 (kb_schrijf), geen opmaak. */
static void kb_dllmain_attach(HINSTANCE h)
{
    kb_start_tick = GetTickCount();
    kb_deze = h;
    DisableThreadLibraryCalls(h);
    InitializeCriticalSection(&kb_slot);
    kb_volgende_poging = GetTickCount();
    char regel[3 * MAX_PATH + 160], stuk[MAX_PATH + 1];
    kb_kopieer(regel, sizeof regel, "DllMain: geladen ");
    if (!GetModuleFileNameA(h, stuk, MAX_PATH)) kb_kopieer(stuk, sizeof stuk, "?");
    kb_plak(regel, sizeof regel, stuk);
    kb_plak(regel, sizeof regel, " in proces ");
    if (!GetModuleFileNameA(NULL, stuk, MAX_PATH)) kb_kopieer(stuk, sizeof stuk, "?");
    kb_plak(regel, sizeof regel, stuk);
    kb_plak(regel, sizeof regel, ", werkmap ");
    if (!GetCurrentDirectoryA(MAX_PATH, stuk)) kb_kopieer(stuk, sizeof stuk, "?");
    kb_plak(regel, sizeof regel, stuk);
    kb_plak(regel, sizeof regel, " (RavenGun rg-konami, zonder TeknoParrot)");
    kb_schrijf(regel);
}

/* ---- Meetinstrument: wie roept wat aan. De code van de exe (de uitvoerbare secties van het hoofdmodule) en of een adres op de
 * stapel een terugkeeradres is (de bytes ervoor zijn een call). Alleen kernel32; bruikbaar in DllMain. ---- */
static uintptr_t kb_code_lo, kb_code_hi;

static void kb_zoek_code(void)
{
    const uint8_t *b = (const uint8_t *)GetModuleHandleA(NULL);
    if (!b || b[0] != 'M' || b[1] != 'Z') return;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(b + *(const LONG *)(b + 0x3c));
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            const uintptr_t lo = (uintptr_t)b + sec[i].VirtualAddress, hi = lo + sec[i].Misc.VirtualSize;
            if (!kb_code_lo || lo < kb_code_lo) kb_code_lo = lo;
            if (hi > kb_code_hi) kb_code_hi = hi;
        }
}

static int kb_is_terugkeer(uintptr_t a)
{
    if (a < kb_code_lo + 7 || a >= kb_code_hi) return 0;
    const uint8_t *q = (const uint8_t *)a;
    if (q[-5] == 0xE8) return 1;                                   /* call rel32 */
    if (q[-6] == 0xFF && (q[-5] & 0x38) == 0x10) return 1;         /* call [mem] (ff 15 / ff 9x) */
    if (q[-2] == 0xFF && (q[-1] & 0xF8) == 0xD0) return 1;         /* call reg */
    if (q[-3] == 0xFF && (q[-2] & 0x38) == 0x10) return 1;         /* call [reg+disp8] */
    return 0;
}

/* Schrijft "<wat>: eip=... terugkeer: a b c ..." voor een stapel van esp tot top (hoogstens 64 KB, 24 adressen). */
static void kb_meld_stapel(const char *wat, uintptr_t eip, uintptr_t esp, uintptr_t top)
{
    char regel[600], getal[16];
    kb_kopieer(regel, sizeof regel, wat);
    if (eip) {
        kb_plak(regel, sizeof regel, ": eip=");
        kb_getal(getal, sizeof getal, eip, 16);
        kb_plak(regel, sizeof regel, getal);
    }
    kb_plak(regel, sizeof regel, " terugkeer in de exe:");
    if (top > esp + 0x10000) top = esp + 0x10000;
    int n = 0;
    for (uintptr_t a = esp & ~(uintptr_t)3; a + 4 <= top && n < 24; a += 4) {
        const uintptr_t v = *(const uintptr_t *)a;
        if (!kb_is_terugkeer(v)) continue;
        kb_plak(regel, sizeof regel, " ");
        kb_getal(getal, sizeof getal, v, 16);
        kb_plak(regel, sizeof regel, getal);
        n++;
    }
    kb_schrijf(regel);
}

/* ---- Waakhond (alleen als kb_waakhond_start is aangeroepen): komt de hoofdlus van de game niet binnen 15 s (kb_hoofdlus_gezien
 * blijft 0), dan elke 15 s (vier keer) waar de hoofddraad staat (eip + terugkeeradressen in de exe) en welke vensters hij heeft. ---- */
static volatile LONG kb_hoofdlus_gezien;
static HANDLE kb_hoofddraad;
static DWORD kb_hoofd_tid;
static uintptr_t kb_hoofd_top;

static BOOL CALLBACK kb_meld_venster(HWND w, LPARAM l)
{
    (void)l;
    char klasse[64], titel[64];
    if (!GetClassNameA(w, klasse, sizeof klasse)) klasse[0] = 0;
    if (!GetWindowTextA(w, titel, sizeof titel)) titel[0] = 0;
    RECT r = {0, 0, 0, 0};
    GetWindowRect(w, &r);
    kb_log("waakhond: venster %x klasse '%s' titel '%s' zichtbaar %d voorgrond %d rect %d,%d-%d,%d stijl %x",
           (unsigned)(uintptr_t)w, klasse, titel, IsWindowVisible(w) ? 1 : 0, GetForegroundWindow() == w ? 1 : 0,
           (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, (unsigned)GetWindowLongA(w, GWL_STYLE));
    return TRUE;
}

static DWORD WINAPI kb_waakhond(LPVOID arg)
{
    (void)arg;
    for (int ronde = 0; ronde < 4; ronde++) {
        Sleep(15000);
        if (kb_hoofdlus_gezien) return 0;
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL;
        if (SuspendThread(kb_hoofddraad) == (DWORD)-1) { kb_log("waakhond: SuspendThread faalt (%u)", (unsigned)GetLastError()); return 0; }
        const BOOL ok = GetThreadContext(kb_hoofddraad, &c);
        if (ok) kb_meld_stapel("waakhond: hoofddraad, geen hoofdlus na 15 s", c.Eip, c.Esp, kb_hoofd_top);
        ResumeThread(kb_hoofddraad);
        if (!ok) kb_log("waakhond: GetThreadContext faalt (%u)", (unsigned)GetLastError());
        EnumThreadWindows(kb_hoofd_tid, kb_meld_venster, 0);
    }
    return 0;
}

/* Aanroepen in DllMain (DLL_PROCESS_ATTACH) van een DLL die de exe statisch importeert: dan is dit de hoofddraad. */
static __attribute__((unused)) void kb_waakhond_start(void)
{
    kb_zoek_code();
    kb_hoofd_tid = GetCurrentThreadId();
    kb_hoofd_top = (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &kb_hoofddraad,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) return;
    HANDLE t = CreateThread(NULL, 0, kb_waakhond, NULL, 0, NULL);   /* start pas na de loader-lock */
    if (t) CloseHandle(t);
}

/* DllMain, DLL_PROCESS_DETACH: het proces stopt (r != NULL) of de DLL wordt losgelaten. Na een crash komt deze regel niet. Bij het
 * stoppen ook wie ExitProcess aanriep (terugkeeradressen in de exe op de stapel van deze draad): bv. de foutroutine van libjpeg
 * (game.exe 0x44f584: exit(1)) of het einde van WinMain. */
static void kb_dllmain_detach(LPVOID r)
{
    kb_schrijf(r ? "DllMain: proces stopt normaal (ExitProcess)" : "DllMain: DLL losgelaten (FreeLibrary)");
    if (r && kb_code_hi) {
        uintptr_t hier = (uintptr_t)&r;
        kb_meld_stapel("DllMain: wie stopt het proces", 0, hier, (uintptr_t)((NT_TIB *)NtCurrentTeb())->StackBase);
    }
}

/* De eerste aanroep van de game, als tweede bewijs: de game is tot zijn I/O-code gekomen. */
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
