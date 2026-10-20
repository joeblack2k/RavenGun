// SPDX-License-Identifier: GPL-3.0-or-later
// rg-rhio: de I/O-bordkant van Robin Hood (Wartran, 32-bits) zonder bord en zonder TeknoParrot.
//
// WAAROM
//   game.exe zoekt zijn I/O-bord via de Windows-apparatenlijst. Die functies staan NIET in de
//   importtabel van de exe; de exe laadt ze met de hand: LoadLibraryA + GetProcAddress
//   ("SetupDiGetClassDevsA" 0x5b0d0d, "SetupDiEnumDeviceInterfaces" 0x5b0d32,
//   "SetupDiGetDeviceInterfaceDetailA" 0x5b0d43, alle drie via de thunk op 0x5ec144). Daarna opent
//   hij het apparaatpad (de string \\.\USBPDCOM staat in de exe) en praat er met DeviceIoControl
//   tegen. Zonder bord vindt hij niets en blijft hij op zijn scherm "BOOTING . / I/O ... CHECKING"
//   staan (gemeten 2026-10-19, 13+ minuten, geen time-out).
//
// HOE
// De weg via een eigen setupapi.dll met WINEDLLOVERRIDES=setupapi=n is verlaten: Wine's
// services.exe heeft de ingebouwde setupapi nodig en het spel startte niet meer (gemeten
// 2026-10-19). Deze DLL wordt daarom door de geheugen-motor in het spelproces geladen
// (RAVENGUN_PATCHMEM_HOOK) en zet zijn haken in de importtabel van de exe.
//   exporteert de SetupDi-functies die de exe opvraagt EN zet in de importtabel van de exe
//   kernel32!GetProcAddress, kernel32!CreateFileA en kernel32!DeviceIoControl om, zodat elke
//   opzoeking en elke bordvraag in rg-rhio.log komt te staan -- ook als de exe de module ergens
//   anders vandaan haalt. Dat logboek is het echte doel van deze eerste versie: het laat zien welke
//   apparaatinterface, welk pad en welke IOCTL-codes met welke buffers het bord moet beantwoorden.
//
// WAT ER (NOG) NIET IS
//   De antwoorden van het bord. DeviceIoControl wordt nu met een schone uitvoerbuffer "gelukt"
//   gemeld (RG_RHIO_IOCTL=0 zet dat uit en laat de fout van Wine doorlopen). Zodra het logboek de
//   codes laat zien, kan hier per code een echt antwoord komen (vergelijk tools/rg-winusb en
//   tools/rg-usio voor dezelfde aanpak bij Namco-borden).
//
// OMGEVING EN MARKERBESTANDEN
//   De volledige lijst staat bij BouwAntwoord() verderop; hier de korte vorm. Alle variabelen worden
//   een keer bij het laden van de DLL gelezen (LogOpen/LeesOmgeving), behalve RG_RHIO_ANTWOORD,
//   RG_RHIO_TOETS, RG_RHIO_MUTEX, RG_RHIO_STACK en RG_COMPAT_* (die bij eerste gebruik).
//   RG_RHIO_LOG=<pad>      het logboek (leeg of afwezig = rg-rhio.log naast de DLL); bij elke start
//                          opnieuw begonnen, het vorige blijft als rg-rhio.vorige.log staan
//   RG_RHIO_PAD=<pad>      het apparaatpad dat de opsomming teruggeeft (standaard \\.\USBPDCOM)
//   RG_RHIO_IOCTL=0        DeviceIoControl niet "gelukt" melden maar de fout van Wine doorlaten
//   RG_RHIO_STIL=1         helemaal niet loggen (geen logbestand); elke andere waarde telt als uit
//   RG_RHIO_EERSTEBYTE, RG_RHIO_BORD, RG_RHIO_ANTWOORD, RG_RHIO_TOETS, RG_RHIO_MUTEX, RG_RHIO_STACK,
//   RG_COMPAT_SAMPLE, RG_COMPAT_SAMPLE_LOG, rg-compat.aan, rg-rhio-stack.aan, rg-rhio-mutex.aan:
//   zie het blok bij BouwAntwoord().
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

void LaadMeetdraad();   // de meetdraad van rg-compat, lazy geladen (buiten de anonieme naamruimte)
void LeesWachthandvatten();   // (D) de wachthandvatten van alle draden, lazy (buiten de anonieme naamruimte)
void StartStackKlok();   // (D) de eigen tijdklok voor de stackmeting (review 14, punt b)

namespace {

const char kStandaardPad[] = "\\\\.\\USBPDCOM";
FILE* g_log = nullptr;
bool g_stil = false;
bool g_ioctl_ok = true;
char g_pad[256] = {0};

/* Het logboek (review 10 C4, pass 2). Een regel is een regel, welke functie hem ook schrijft: Log en
 * LogVan delen een teller en een maximum, zodat rg-rhio.log niet onbeperkt groeit (de ReadFile-regels
 * van LogVan zaten er eerder buiten). Na het maximum komt er nog een keer een regel en daarna niets
 * meer. Elke regel wordt meteen geflusht: de nieuwste regels zijn precies die van het moment van een
 * vastloper en gaan verloren als het spel wordt gedood. Het volume is laag genoeg doordat
 * ClearCommError alleen een wijziging (of een levensteken per seconde) logt. */
volatile LONG g_log_regels = 0;
constexpr LONG kLogMax = 20000;

/* De stackregels (LeesWachthandvatten) hebben hun eigen teller en hun eigen maximum. Anders kan een
 * stortvloed ReadFile-regels het maximum opmaken en vallen juist de regels weg waar het om gaat
 * (review 12, punt 2). */
volatile LONG g_log_stack_regels = 0;
constexpr LONG kLogStackMax = 4000;

/* De mutex-regels hebben ook hun eigen budget (review 14, punt a). De CRT roept WaitForSingleObject
 * heel vaak aan; zonder eigen teller aten die regels het algemene maximum op voordat de wachtdienst
 * die er echt toe doet in het logboek stond (gemeten: 18.871 mutex-regels en precies 20.001 regels in
 * totaal, dus het maximum was op). */
volatile LONG g_log_mutex_regels = 0;
constexpr LONG kLogMutexMax = 3000;

void SchrijfRegelMet(const char* tekst, volatile LONG* teller, LONG max) {
    if (!g_log || g_stil) return;
    if (*teller > max) return;   // vooraf kijken: de teller loopt zo nooit door in een lus
    const LONG nr = InterlockedIncrement(teller);
    if (nr > max + 1) return;
    if (nr == max + 1) {
        std::fprintf(g_log, "[log] maximum van %ld regels bereikt; verdere regels worden niet "
                            "meer geschreven (zet RG_RHIO_STIL=1 of verwijder rg-rhio.log)\n", max);
        std::fflush(g_log);
        return;
    }
    std::fputs(tekst, g_log);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

void SchrijfRegel(const char* tekst) { SchrijfRegelMet(tekst, &g_log_regels, kLogMax); }

void LogVan(const void* beller, const char* fmt, ...) {
    if (!g_log || g_stil) return;
    va_list ap;
    va_start(ap, fmt);
    char tekst[512];
    vsnprintf(tekst, sizeof tekst, fmt, ap);
    va_end(ap);
    char regel[640];
    std::snprintf(regel, sizeof regel, "[%8lu] beller=%p %s", static_cast<unsigned long>(GetTickCount()), beller, tekst);
    SchrijfRegel(regel);
}

void Log(const char* fmt, ...) {
    if (!g_log || g_stil) return;
    va_list ap;
    va_start(ap, fmt);
    char regel[640];
    vsnprintf(regel, sizeof regel, fmt, ap);
    va_end(ap);
    SchrijfRegel(regel);
}

/* De mutex-regels: eigen budget, zodat ze het algemene maximum niet opmaken (review 14, punt a). */
void LogMutex(const char* fmt, ...) {
    if (!g_log || g_stil) return;
    va_list ap;
    va_start(ap, fmt);
    char regel[640];
    vsnprintf(regel, sizeof regel, fmt, ap);
    va_end(ap);
    SchrijfRegelMet(regel, &g_log_mutex_regels, kLogMutexMax);
}

/* Zoals Log (alles wordt nu meteen weggeschreven); apart gehouden om de foutregels te kunnen onderscheiden. */
void LogFout(const char* fmt, ...) {
    if (!g_log || g_stil) return;
    va_list ap;
    va_start(ap, fmt);
    char regel[640];
    vsnprintf(regel, sizeof regel, fmt, ap);
    va_end(ap);
    SchrijfRegel(regel);
}

volatile LONG g_log_geprobeerd = 0;

void LogOpen() {
    /* Een keer proberen, ook als het niet lukt: anders doet elke CreateFileA opnieuw getenv/fopen.
     * De vlag is atomair, want meerdere draden van het spel komen hier langs. */
    if (g_log) return;
    if (InterlockedCompareExchange(&g_log_geprobeerd, 1, 0) != 0) return;
    /* Alleen "1" is stil (RG_RHIO_STIL=0 of een andere waarde betekent gewoon loggen). Bij stil
     * wordt het logbestand ook niet aangeraakt: er komt toch niets in. */
    const char* stil = std::getenv("RG_RHIO_STIL");
    g_stil = stil && std::strcmp(stil, "1") == 0;
    if (const char* v = std::getenv("RG_RHIO_IOCTL")) g_ioctl_ok = std::strcmp(v, "0") != 0;
    const char* p = std::getenv("RG_RHIO_PAD");
    std::snprintf(g_pad, sizeof g_pad, "%s", (p && *p) ? p : kStandaardPad);
    if (!g_stil) {
        const char* omgeving = std::getenv("RG_RHIO_LOG");
        char pad[MAX_PATH];
        if (omgeving && *omgeving) {
            std::snprintf(pad, sizeof pad, "%s", omgeving);
        } else {
            HMODULE zelf = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&LogOpen), &zelf);
            GetModuleFileNameA(zelf, pad, sizeof pad);
            char* schuin = std::strrchr(pad, '\\');
            if (schuin) *(schuin + 1) = '\0';
            std::strncat(pad, "rg-rhio.log", sizeof pad - std::strlen(pad) - 1);
        }
        /* Elke start een schoon logboek; het vorige blijft een start lang bewaard als <naam>.vorige.log
         * (bij een pad dat niet op .log eindigt: <pad>.vorige). Lukt het hernoemen niet om een andere
         * reden dan "bestaat nog niet" (bv. een sharing violation omdat een tweede proces het logboek
         * open heeft), dan openen we met "a" en laten het bestand staan in plaats van het leeg te maken. */
        char vorige[MAX_PATH + 16];
        const size_t lp = std::strlen(pad);
        if (lp > 4 && std::strcmp(pad + lp - 4, ".log") == 0)
            std::snprintf(vorige, sizeof vorige, "%.*s.vorige.log", static_cast<int>(lp - 4), pad);
        else
            std::snprintf(vorige, sizeof vorige, "%s.vorige", pad);
        DWORD hernoem_fout = 0;
        if (!MoveFileExA(pad, vorige, MOVEFILE_REPLACE_EXISTING)) hernoem_fout = GetLastError();
        const bool geen_vorige = hernoem_fout == 0 || hernoem_fout == ERROR_FILE_NOT_FOUND ||
                                 hernoem_fout == ERROR_PATH_NOT_FOUND;
        g_log = std::fopen(pad, geen_vorige ? "w" : "a");
        if (!geen_vorige)
            LogFout("logboek: hernoemen naar %s mislukt (fout %lu); logboek geopend met \"a\" (toevoegen)", vorige,
                    static_cast<unsigned long>(hernoem_fout));
    }
    Log("rg-rhio geladen (32-bits): pad=%s, ioctl-gelukt=%d", g_pad, g_ioctl_ok ? 1 : 0);
}

// --- de importtabel van het hoofdprogramma ------------------------------------------------------
/* Het adres van het importtabel-slot zelf, of nullptr. Zo kunnen we eerst lezen en dan pas schrijven. */
void** ZoekIatSlot(HMODULE mod, const char* dll, const char* func) {
    auto* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (lstrcmpiA(reinterpret_cast<const char*>(base + imp->Name), dll) != 0) continue;
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        auto* namen = imp->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk) : nullptr;
        if (!namen) continue;
        for (size_t i = 0; iat[i].u1.Function; ++i) {
            if (namen[i].u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + namen[i].u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(ibn->Name), func) != 0) continue;
            return reinterpret_cast<void**>(&iat[i].u1.Function);
        }
    }
    return nullptr;
}

/* De huidige waarde van een slot, zonder iets te veranderen. */
void* LeesIat(HMODULE mod, const char* dll, const char* func) {
    void** slot = ZoekIatSlot(mod, dll, func);
    return slot ? *slot : nullptr;
}

void* PatchIat(HMODULE mod, const char* dll, const char* func, void* repl) {
    void** slot = ZoekIatSlot(mod, dll, func);
    if (!slot) return nullptr;
    void* oud = *slot;
    DWORD prot = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &prot)) return nullptr;
    *slot = repl;
    VirtualProtect(slot, sizeof(void*), prot, &prot);
    return oud;
}

// --- de SetupDi-functies die de exe opzoekt -----------------------------------------------------
struct SpDevIfData {
    DWORD cbSize;
    GUID guid;
    DWORD vlaggen;
    ULONG_PTR gereserveerd;
};
struct SpDevIfDetailA {
    DWORD cbSize;
    char pad[1];
};

using GetProcAddrFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using DeviceIoControlFn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using WriteFileFn = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
using PurgeCommFn = BOOL(WINAPI*)(HANDLE, DWORD);
using ClearCommErrorFn = BOOL(WINAPI*)(HANDLE, LPDWORD, LPCOMSTAT);
using GetCommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using SetCommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using GetCommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);
using SetCommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);
using EscapeCommFunctionFn = BOOL(WINAPI*)(HANDLE, DWORD);
using SetCommMaskFn = BOOL(WINAPI*)(HANDLE, DWORD);
using WaitCommEventFn = BOOL(WINAPI*)(HANDLE, LPDWORD, LPOVERLAPPED);
using GetOverlappedResultFn = BOOL(WINAPI*)(HANDLE, LPOVERLAPPED, LPDWORD, BOOL);

GetProcAddrFn g_echte_gpa = nullptr;
CreateFileAFn g_echte_createfile = nullptr;
DeviceIoControlFn g_echte_ioctl = nullptr;
HANDLE g_nep_handle = reinterpret_cast<HANDLE>(0x52480001);
HANDLE g_com_handle = reinterpret_cast<HANDLE>(0x52480002);   /* het nagebootste seriele bord */
ReadFileFn g_echte_read = nullptr;
WriteFileFn g_echte_write = nullptr;
PurgeCommFn g_echte_purge = nullptr;
ClearCommErrorFn g_echte_clear = nullptr;
GetCommStateFn g_echte_getstate = nullptr;
SetCommStateFn g_echte_setstate = nullptr;
GetCommTimeoutsFn g_echte_gettimeouts = nullptr;
SetCommTimeoutsFn g_echte_settimeouts = nullptr;
EscapeCommFunctionFn g_echte_escape = nullptr;
SetCommMaskFn g_echte_setmask = nullptr;
WaitCommEventFn g_echte_wait = nullptr;
GetOverlappedResultFn g_echte_ovl = nullptr;
unsigned char g_laatste_kader[64];
DWORD g_laatste_lengte = 0;
/* Staat er een antwoord klaar in de invoerwachtrij? De klassieke seriele lus van een spel is:
 * ClearCommError (kijk naar cbInQue) en dan ReadFile alleen als er iets klaarstaat. Zonder dit
 * leest het spel nooit en lijkt het alsof het de poort niet leest. */
DWORD g_wachtend_antwoord = 0;
/* Hoeveel bytes van het antwoord al aan het spel gegeven zijn (het leest eerst 1 byte, dan 6). */
DWORD g_antwoord_gelezen = 0;
bool g_antwoord_op = false;   // het antwoord is helemaal gelezen: cbInQue moet 0 zijn
/* Het aantal bytes van de laatste ReadFile/WriteFile op het bord: dat meldt GetOverlappedResult
 * (niet de lengte van het hele kader). Onder g_bord_cs gezet en gelezen. */
DWORD g_laatste_overdracht = 0;
/* De laatste cbInQue die ClearCommError gelogd heeft (onder g_bord_cs); de aanroep staat in een
 * strakke lus en logde elke keer dezelfde regel. 0xFFFFFFFF = nog niets gelogd. */
DWORD g_clear_gelogd = 0xFFFFFFFFu;
/* Aantal ClearCommError-aanroepen sinds de laatste ClearCommError-logregel en het tijdstip van die
 * regel (onder g_bord_cs): zonder deze twee is "het spel pollt nog" niet te onderscheiden van "het
 * spel roept niet meer aan" als cbInQue op 0 of 7 blijft staan. */
DWORD g_clear_aanroepen = 0;
DWORD g_clear_tick = 0;
/* RG_RHIO_EERSTEBYTE en RG_RHIO_BORD, een keer gelezen in LeesOmgeving() (DllMain), dus niet per
 * frame onder g_bord_cs. */
bool g_eerstebyte_gezet = false;
unsigned char g_eerstebyte = 0;
char g_bord[64] = {0};
bool g_met_bord = true;
/* Meerdere draden van het spel komen bij deze poort; alles hieronder is met dit slot beschermd. */
CRITICAL_SECTION g_bord_cs;
bool g_bord_cs_klaar = false;
/* Het laatste opdrachtkader, apart bewaard voor het logboek (het antwoordkader wordt overschreven). */
unsigned char g_opdracht_kader[64];

FARPROC WINAPI rg_GetProcAddress(HMODULE mod, LPCSTR naam);
HANDLE WINAPI rg_CreateFileA(LPCSTR pad, DWORD toegang, DWORD deel, LPSECURITY_ATTRIBUTES sa, DWORD aanmaak, DWORD vlaggen, HANDLE sjabloon);
BOOL WINAPI rg_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD inl, LPVOID uit, DWORD uitl, LPDWORD terug, LPOVERLAPPED ov);

extern "C" {

HANDLE WINAPI SetupDiGetClassDevsA(const GUID* g, PCSTR enumerator, HWND hwnd, DWORD vlaggen) {
    LogOpen();
    Log("SetupDiGetClassDevsA(guid=%p, enumerator=%s, vlaggen=0x%lx) -> nagebootste lijst",
        reinterpret_cast<const void*>(g), enumerator ? enumerator : "(null)", static_cast<unsigned long>(vlaggen));
    return g_nep_handle;
}

HANDLE WINAPI SetupDiGetClassDevsW(const GUID* g, PCWSTR enumerator, HWND hwnd, DWORD vlaggen) {
    LogOpen();
    Log("SetupDiGetClassDevsW(guid=%p, vlaggen=0x%lx) -> nagebootste lijst",
        reinterpret_cast<const void*>(g), static_cast<unsigned long>(vlaggen));
    return g_nep_handle;
}

BOOL WINAPI SetupDiEnumDeviceInterfaces(HANDLE lijst, void* info, const GUID* g, DWORD index, SpDevIfData* uit) {
    LogOpen();
    if (!uit) return FALSE;
    if (index != 0) {
        Log("SetupDiEnumDeviceInterfaces(index=%lu) -> FALSE (ERROR_NO_MORE_ITEMS)", static_cast<unsigned long>(index));
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    uit->cbSize = sizeof(SpDevIfData);
    if (g) uit->guid = *g;
    uit->vlaggen = 0;
    uit->gereserveerd = 0;
    Log("SetupDiEnumDeviceInterfaces(index=0) -> TRUE (guid=%p)", reinterpret_cast<const void*>(g));
    return TRUE;
}

BOOL WINAPI SetupDiGetDeviceInterfaceDetailA(HANDLE lijst, SpDevIfData* id, SpDevIfDetailA* detail, DWORD grootte,
                                             DWORD* nodig, void* devinfo) {
    LogOpen();
    const DWORD n = static_cast<DWORD>(sizeof(SpDevIfDetailA) + std::strlen(g_pad) + 1);
    if (nodig) *nodig = n;
    if (!detail || grootte < n) {
        Log("SetupDiGetDeviceInterfaceDetailA: te kleine buffer (%lu < %lu) -> FALSE/ERROR_INSUFFICIENT_BUFFER",
            static_cast<unsigned long>(grootte), static_cast<unsigned long>(n));
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    detail->cbSize = sizeof(SpDevIfDetailA);
    std::snprintf(detail->pad, grootte - sizeof(DWORD), "%s", g_pad);
    Log("SetupDiGetDeviceInterfaceDetailA -> TRUE, pad=%s (grootte %lu)", detail->pad,
        static_cast<unsigned long>(grootte));
    return TRUE;
}

BOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HANDLE lijst, SpDevIfData* id, void* detail, DWORD grootte,
                                             DWORD* nodig, void* devinfo) {
    LogOpen();
    WCHAR breed[256];
    int i = 0;
    for (; g_pad[i] && i < 255; ++i) breed[i] = static_cast<WCHAR>(g_pad[i]);
    breed[i] = 0;
    const DWORD n = static_cast<DWORD>(sizeof(DWORD) + (i + 1) * sizeof(WCHAR));
    if (nodig) *nodig = n;
    if (!detail || grootte < n) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    auto* d = reinterpret_cast<DWORD*>(detail);
    *d = 8;   // cbSize van de W-variant op 32-bits
    std::memcpy(d + 1, breed, (i + 1) * sizeof(WCHAR));
    Log("SetupDiGetDeviceInterfaceDetailW -> TRUE, pad=%s", g_pad);
    return TRUE;
}

BOOL WINAPI SetupDiGetDeviceRegistryPropertyA(HANDLE lijst, void* info, DWORD eigenschap, DWORD* soort, PBYTE buffer,
                                              DWORD grootte, DWORD* nodig) {
    LogOpen();
    Log("SetupDiGetDeviceRegistryPropertyA(eigenschap=0x%lx, grootte=%lu) -> FALSE/ERROR_INVALID_DATA",
        static_cast<unsigned long>(eigenschap), static_cast<unsigned long>(grootte));
    if (soort) *soort = 0;
    if (nodig) *nodig = 0;
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
}

BOOL WINAPI SetupDiEnumDeviceInfo(HANDLE lijst, DWORD index, void* info) {
    LogOpen();
    Log("SetupDiEnumDeviceInfo(index=%lu) -> FALSE/ERROR_NO_MORE_ITEMS", static_cast<unsigned long>(index));
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

BOOL WINAPI SetupDiDestroyDeviceInfoList(HANDLE lijst) {
    LogOpen();
    Log("SetupDiDestroyDeviceInfoList -> TRUE");
    return TRUE;
}

}  // extern "C"

FARPROC WINAPI rg_GetProcAddress(HMODULE mod, LPCSTR naam) {
    LogOpen();
    static const char* const kNamen[] = {"SetupDiGetClassDevsA", "SetupDiGetClassDevsW",
                                         "SetupDiEnumDeviceInterfaces", "SetupDiGetDeviceInterfaceDetailA",
                                         "SetupDiGetDeviceInterfaceDetailW", "SetupDiGetDeviceRegistryPropertyA",
                                         "SetupDiEnumDeviceInfo", "SetupDiDestroyDeviceInfoList"};
    static void* const kAdressen[] = {reinterpret_cast<void*>(&SetupDiGetClassDevsA),
                                      reinterpret_cast<void*>(&SetupDiGetClassDevsW),
                                      reinterpret_cast<void*>(&SetupDiEnumDeviceInterfaces),
                                      reinterpret_cast<void*>(&SetupDiGetDeviceInterfaceDetailA),
                                      reinterpret_cast<void*>(&SetupDiGetDeviceInterfaceDetailW),
                                      reinterpret_cast<void*>(&SetupDiGetDeviceRegistryPropertyA),
                                      reinterpret_cast<void*>(&SetupDiEnumDeviceInfo),
                                      reinterpret_cast<void*>(&SetupDiDestroyDeviceInfoList)};
    /* Een ordinale opzoeking geeft geen pointer maar een klein getal; strcmp daarop zou het spel
     * laten omvallen. Alles onder 0x10000 is een ordinal, geen naam. */
    if (naam && reinterpret_cast<uintptr_t>(naam) <= 0xFFFF) {
        return g_echte_gpa ? g_echte_gpa(mod, naam) : nullptr;
    }
    if (naam) {
        for (size_t i = 0; i < sizeof kNamen / sizeof kNamen[0]; ++i)
            if (std::strcmp(naam, kNamen[i]) == 0) {
                Log("GetProcAddress(module=%p, %s) -> nagebootst", reinterpret_cast<void*>(mod), naam);
                return reinterpret_cast<FARPROC>(kAdressen[i]);
            }
        if (std::strncmp(naam, "SetupDi", 7) == 0)
            Log("GetProcAddress(module=%p, %s) -> NIET nagebootst (echte functie)", reinterpret_cast<void*>(mod), naam);
    }
    return g_echte_gpa ? g_echte_gpa(mod, naam) : nullptr;
}

HANDLE WINAPI rg_CreateFileA(LPCSTR pad, DWORD toegang, DWORD deel, LPSECURITY_ATTRIBUTES sa, DWORD aanmaak,
                             DWORD vlaggen, HANDLE sjabloon) {
    LogOpen();
    /* Pas NA LogOpen: deze twee loggen zelf, en de eenmalige vlag werd anders verbruikt voordat er
     * een logboek was (daardoor waren de "meetdraad"- en "stack"-regels eerder onzichtbaar). */
    LaadMeetdraad();   // lazy: niet in DllMain (daar kan LoadLibrary de loader-lock pakken)
    StartStackKlok();  // en een eigen tijdklok, zodat de meting ook loopt als CreateFileA uitblijft
    LeesWachthandvatten();
    if (pad && std::strcmp(pad, g_pad) == 0) {
        Log("CreateFileA(%s) -> nagebootst handvat %p", pad, g_nep_handle);
        SetLastError(ERROR_SUCCESS);
        return g_nep_handle;
    }
    /* Het seriele bord: de exe probeert COM1..COM4 en blijft daarna op het eerste antwoord wachten
     * (gemeten 2026-10-19: COM1..COM3 fout 5, COM4 lukte, en daarna bleef "I/O ... CHECKING"
     * staan). Wij geven voor elke COM-poort hetzelfde nagebootste handvat terug, zodat het spel
     * verder komt en wij de opdrachten in het logboek zien. */
    if (pad && std::strncmp(pad, "COM", 3) == 0 && pad[3] >= '0' && pad[3] <= '9') {
        Log("CreateFileA(%s, toegang=0x%lx) -> nagebootst bordhandvat %p", pad,
            static_cast<unsigned long>(toegang), g_com_handle);
        SetLastError(ERROR_SUCCESS);
        return g_com_handle;
    }

    /* Alles wat op een apparaat lijkt ook loggen: dan zie je waar de I/O-controle van het spel
     * werkelijk naar kijkt (het bord, een poort, een ander stuurprogramma). */
    if (pad && (std::strstr(pad, "\\\\.\\") || std::strstr(pad, "COM") || std::strstr(pad, "usb") ||
                std::strstr(pad, "USB") || std::strstr(pad, "JVS") || std::strstr(pad, "pdcom"))) {
        HANDLE h = g_echte_createfile ? g_echte_createfile(pad, toegang, deel, sa, aanmaak, vlaggen, sjabloon)
                                      : INVALID_HANDLE_VALUE;
        Log("CreateFileA(%s, toegang=0x%lx) -> %p (fout %lu)", pad, static_cast<unsigned long>(toegang), h,
            static_cast<unsigned long>(GetLastError()));
        return h;
    }
    return g_echte_createfile ? g_echte_createfile(pad, toegang, deel, sa, aanmaak, vlaggen, sjabloon)
                              : INVALID_HANDLE_VALUE;
}

BOOL WINAPI rg_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD inl, LPVOID uit, DWORD uitl, LPDWORD terug,
                               LPOVERLAPPED ov) {
    LogOpen();
    if (h == g_com_handle) {
        Log("DeviceIoControl(bord, code=0x%08lx, in=%lu, uit=%lu)", static_cast<unsigned long>(code),
            static_cast<unsigned long>(inl), static_cast<unsigned long>(uitl));
        if (uit && uitl) std::memset(uit, 0, uitl);
        if (terug) *terug = uitl;
        return TRUE;
    }
    if (h == g_nep_handle) {
        Log("DeviceIoControl(handvat=%p, code=0x%08lx, in=%lu, uit=%lu)", h, static_cast<unsigned long>(code),
            static_cast<unsigned long>(inl), static_cast<unsigned long>(uitl));
        if (in && inl) {
            char hex[3 * 32 + 1];
            DWORD n = inl < 32 ? inl : 32;
            for (DWORD i = 0; i < n; ++i) std::snprintf(hex + i * 3, 4, "%02x ", reinterpret_cast<BYTE*>(in)[i]);
            hex[n * 3] = '\0';
            Log("   invoer: %s", hex);
        }
        if (!g_ioctl_ok) {
            SetLastError(ERROR_INVALID_FUNCTION);
            return FALSE;
        }
        if (uit && uitl) std::memset(uit, 0, uitl);
        if (terug) *terug = uitl;
        return TRUE;
    }
    return g_echte_ioctl ? g_echte_ioctl(h, code, in, inl, uit, uitl, terug, ov) : FALSE;
}

bool CodeGoed(unsigned char c);
void BouwAntwoord();

const char* AntwoordStand();

BOOL WINAPI rg_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD gelezen, LPOVERLAPPED ov) {
    if (h == g_com_handle) {
        /* Eerste proef: het laatste opdrachtkader terugsturen (bord aanwezig, geen schakelaars).
         * RG_RHIO_ANTWOORD=0 (of echo) is hetzelfde als niet gezet: echo; RG_RHIO_ANTWOORD=niks laat
         * het antwoord leeg. */
        const char* stand = AntwoordStand();
        if (stand && std::strcmp(stand, "niks") == 0) {
            Log("ReadFile(bord, %lu bytes gevraagd) -> geen antwoord (RG_RHIO_ANTWOORD=niks)",
                static_cast<unsigned long>(n));
            if (gelezen) *gelezen = 0;
            /* N3: ook het wachtende antwoord opruimen, anders blijft ClearCommError cbInQue > 0
             * melden en draait het spel zijn leeslus warm. */
            if (g_bord_cs_klaar) EnterCriticalSection(&g_bord_cs);
            g_wachtend_antwoord = 0;
            g_antwoord_gelezen = 0;
            g_antwoord_op = true;   // niets klaarstaands: cbInQue 0 (review 10, C2)
            g_laatste_overdracht = 0;
            if (g_bord_cs_klaar) LeaveCriticalSection(&g_bord_cs);
            SetLastError(ERROR_TIMEOUT);
            return TRUE;
        }
        /* Het antwoord in stukken: het spel leest eerst 1 byte (de code) en dan 6 (de rest). De
         * leeswijzer houdt bij waar we zijn; anders kreeg het spel de eerste zes bytes twee keer. */
        if (g_bord_cs_klaar) EnterCriticalSection(&g_bord_cs);
        const DWORD vanaf = g_antwoord_gelezen;
        DWORD beschikbaar = g_laatste_lengte > vanaf ? g_laatste_lengte - vanaf : 0;
        DWORD m = beschikbaar;
        if (m > n) m = n;
        if (buf && m) std::memcpy(buf, g_laatste_kader + vanaf, m);
        g_antwoord_gelezen += m;
        if (g_antwoord_gelezen >= g_laatste_lengte) {
            g_antwoord_gelezen = 0;
            g_antwoord_op = true;
            g_wachtend_antwoord = 0;   /* het hele antwoord is opgehaald */
        }
        g_laatste_overdracht = m;
        if (gelezen) *gelezen = m;
        if (g_bord_cs_klaar) LeaveCriticalSection(&g_bord_cs);
        LogVan(__builtin_return_address(0), "ReadFile(bord, %lu bytes gevraagd, vanaf %lu) -> %lu bytes",
               static_cast<unsigned long>(n), static_cast<unsigned long>(vanaf),
               static_cast<unsigned long>(m));
        return TRUE;
    }
    return g_echte_read ? g_echte_read(h, buf, n, gelezen, ov) : FALSE;
}

BOOL WINAPI rg_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD geschreven, LPOVERLAPPED ov) {
    if (h == g_com_handle) {
        char hex[3 * 64 + 1];
        DWORD m = n < 64 ? n : 64;
        if (n > 0 && !buf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        for (DWORD i = 0; i < m; ++i) std::snprintf(hex + i * 3, 4, "%02x ", reinterpret_cast<const BYTE*>(buf)[i]);
        hex[m * 3] = '\0';
        Log("WriteFile(bord, %lu bytes): %s", static_cast<unsigned long>(n), hex);
        if (g_bord_cs_klaar) EnterCriticalSection(&g_bord_cs);
        g_laatste_lengte = n < sizeof g_laatste_kader ? n : (DWORD)(sizeof g_laatste_kader);
        if (buf) std::memcpy(g_laatste_kader, buf, g_laatste_lengte);
        std::memcpy(g_opdracht_kader, g_laatste_kader, g_laatste_lengte);
        BouwAntwoord();
        g_wachtend_antwoord = g_laatste_lengte;
        g_antwoord_gelezen = 0;
        /* Een nieuw antwoord staat klaar, ook als het frame korter dan 7 bytes is (BouwAntwoord doet dan
         * niets): anders bleef g_antwoord_op van het vorige, helemaal gelezen frame staan en meldde
         * ClearCommError cbInQue 0 (review 10, C1). */
        g_antwoord_op = false;
        g_laatste_overdracht = n;
        {
            char opdracht_hex[3 * 64 + 1], antwoord_hex[3 * 64 + 1];
            for (DWORD i = 0; i < g_laatste_lengte; ++i)
                std::snprintf(opdracht_hex + i * 3, 4, "%02x ", g_opdracht_kader[i]);
            for (DWORD i = 0; i < g_laatste_lengte; ++i)
                std::snprintf(antwoord_hex + i * 3, 4, "%02x ", g_laatste_kader[i]);
            opdracht_hex[g_laatste_lengte * 3] = '\0';
            antwoord_hex[g_laatste_lengte * 3] = '\0';
            Log("frame-paar: opdracht %s -> antwoord %s", opdracht_hex, antwoord_hex);
        }
        if (g_bord_cs_klaar) LeaveCriticalSection(&g_bord_cs);
        if (geschreven) *geschreven = n;
        return TRUE;
    }
    return g_echte_write ? g_echte_write(h, buf, n, geschreven, ov) : FALSE;
}

BOOL WINAPI rg_PurgeComm(HANDLE h, DWORD vlaggen) {
    if (h == g_com_handle) {
        if (g_bord_cs_klaar) EnterCriticalSection(&g_bord_cs);
        g_laatste_lengte = 0;
        g_antwoord_gelezen = 0;
        g_antwoord_op = false;
        g_wachtend_antwoord = 0;
        g_laatste_overdracht = 0;
        if (g_bord_cs_klaar) LeaveCriticalSection(&g_bord_cs);
        Log("PurgeComm(bord, 0x%lx) -> wachtend antwoord gewist", static_cast<unsigned long>(vlaggen));
        return TRUE;
    }
    return g_echte_purge ? g_echte_purge(h, vlaggen) : FALSE;
}

/* De codes die het spel als eerste antwoordbyte goedkeurt (0x49036f-0x4903c1). */
bool CodeGoed(unsigned char c) {
    switch (c) {
        case 0x00: case 0xff: case 0x94: case 0x95: case 0x96: case 0x97: case 0x98:
        case 0x23: case 0x24: case 0x29: case 0x30:
            return true;
        default:
            return false;
    }
}

/* Het antwoord op een opdrachtkader opbouwen: het kader terug, maar met een eerste byte die het spel
 * goedkeurt (anders gaat het naar het foutpad) en de som van de eerste zes bytes in byte 6.
 *
 * ALLE omgevingsvariabelen en markerbestanden van deze DLL (review 10, C4; bijgewerkt in pass 2).
 * Een variabele "afwezig" is ook een lege waarde. Waar hieronder "bij het laden" staat, is hij een
 * keer gelezen (LogOpen/LeesOmgeving in DllMain); de rest bij het eerste gebruik.
 *   RG_RHIO_LOG        = pad van het logboek; leeg of afwezig = rg-rhio.log naast de DLL. Bij elke
 *                        start opnieuw begonnen; het vorige logboek wordt hernoemd naar
 *                        <naam>.vorige.log (bij een pad zonder .log: <pad>.vorige). Maximaal 20000
 *                        regels (Log en LogVan tellen samen), elke regel meteen geflusht; ClearCommError logt een
 *                        wijziging van cbInQue en verder een levensteken per ~1 s met het aantal
 *                        aanroepen sinds de vorige regel.
 *   RG_RHIO_STIL       = alleen de waarde "1" zet het logboek helemaal uit (er wordt dan ook geen
 *                        bestand aangemaakt of hernoemd); elke andere waarde telt als uit.
 *   RG_RHIO_PAD        = het apparaatpad dat de SetupDi-opsomming teruggeeft (standaard \\.\USBPDCOM)
 *   RG_RHIO_IOCTL      = "0": DeviceIoControl op het apparaatpad meldt een fout (ERROR_INVALID_FUNCTION)
 *                        in plaats van "gelukt"; elke andere waarde of afwezig = gelukt (bij het laden).
 *   RG_RHIO_ANTWOORD   = (eerste gebruik; waarden die de code kent)
 *                          afwezig of iets anders (ook "0" en "echo") = echo met een geldige code
 *                          "nul"  = alle zeven bytes 0 (proef: de beveiligingscontrole valt om)
 *                          "niks" = geen antwoord; ReadFile geeft 0 bytes en wist het wachtende antwoord (daarna
 *                                meldt ClearCommError 0)
 *   RG_RHIO_EERSTEBYTE = hex (bv. 94, 0x94); forceert alleen byte 0 van het antwoord. Afwezig of leeg
 *                        = byte 0 uit de opdracht als het een goedgekeurde code is, anders 0x94.
 *                        Bij het laden gelezen.
 *   RG_RHIO_BORD       = "0" (eerste teken): geen bordtoestand uit het gedeelde geheugen, alleen
 *                        echo. Afwezig of iets anders = toestand uit TeknoParrot_JvsState. Bij het
 *                        laden gelezen; past de waarde niet in 63 tekens, dan telt hij als afwezig
 *                        (een keer gelogd).
 *   RG_RHIO_TOETS      = (eerste gebruik) cyclus | nul | vol | buiten: proefstanden op de besturings-
 *                        byte/richtas; afwezig = toestand uit het bordbeeld (zie ProefToestand).
 *   RG_RHIO_MUTEX      = (eerste gebruik) afwezig: kijk naar rg-rhio-mutex.aan; gezet: "0" is uit,
 *                        al het andere aan. Zet de logregels "mutex:" aan en installeert dan pas de
 *                        haken op WaitForSingleObject/ReleaseMutex/CreateMutexA (die Robin Hood bij
 *                        elke start liet stoppen, daarom standaard uit).
 *   RG_RHIO_STACK      = (eerste gebruik) idem voor rg-rhio-stack.aan: "0" uit, anders aan; zet de
 *                        stapelregels "stack:" aan (LeesWachthandvatten, een keer per seconde).
 *   RG_COMPAT_SAMPLE   = ms; laadt de meetdraad van rg-compat (lazy, bij de eerste CreateFileA).
 *                        Zoals hierboven: gezet beslist ("0" is uit, ook met rg-compat.aan);
 *                        afwezig: kijk naar rg-compat.aan
 *   RG_COMPAT_SAMPLE_LOG = pad van het meetlogboek; afwezig = rg-compat-sample.txt naast de DLL (wordt
 *                        door LaadMeetdraad gezet als er een meting is)
 *   Markerbestanden, gezocht in de map van deze DLL (alleen of ze bestaan; de inhoud telt niet,
 *   behalve bij rg-compat.aan):
 *     rg-compat.aan      = zet de meetdraad aan; de inhoud is het aantal ms (leeg/onleesbaar = 200) en
 *                          wordt RG_COMPAT_SAMPLE
 *     rg-rhio-stack.aan  = zelfde als RG_RHIO_STACK=1
 *     rg-rhio-mutex.aan  = zelfde als RG_RHIO_MUTEX=1
 *   RAVENGUN_PATCHMEM_HOOK = de DLL die de motor laadt (dit is rg-rhio.dll)
 *   RAVENGUN_PATCHMEM_FILE / RAVENGUN_PATCHMEM_VERBOSE = van de geheugenmotor (zie memory/) */
/* RG_RHIO_ANTWOORD, een keer gelezen (deze functie loopt per bordopdracht):
 *   (niet gezet) of 0 = echo met een geldige code (het gewone bedrijf)
 *   nul              = alle bytes 0 (proef: de beveiligingscontrole valt om)
 *   niks             = geen antwoord; ClearCommError meldt dan ook niets klaarstaands
 * RG_RHIO_EERSTEBYTE=<hex> forceert alleen de eerste byte van het antwoord. */
const char* AntwoordStand() {
    static const char* stand = std::getenv("RG_RHIO_ANTWOORD");
    return stand;
}

/* RG_RHIO_EERSTEBYTE en RG_RHIO_BORD een keer lezen, in DllMain na het aanmaken van het slot (dus
 * voordat een andere draad BouwAntwoord kan aanroepen). Was per frame onder g_bord_cs een getenv en
 * een GetEnvironmentVariableA op een niet-geinitialiseerde buffer van 8 bytes: bij een waarde van 8+
 * tekens geeft de API alleen de benodigde grootte terug en laat de buffer onaangeroerd. */
void LeesOmgeving() {
    const char* keuze = std::getenv("RG_RHIO_EERSTEBYTE");
    if (keuze && *keuze) {
        g_eerstebyte = static_cast<unsigned char>(std::strtoul(keuze, nullptr, 16));
        g_eerstebyte_gezet = true;
    }
    g_bord[0] = '\0';
    const DWORD n = GetEnvironmentVariableA("RG_RHIO_BORD", g_bord, sizeof g_bord);
    if (n >= sizeof g_bord) {   // past niet (de API geeft dan de benodigde grootte terug)
        g_bord[0] = '\0';
        LogFout("RG_RHIO_BORD is te lang (%lu tekens, maximaal %lu); telt als niet gezet",
                static_cast<unsigned long>(n), static_cast<unsigned long>(sizeof g_bord - 1));
    } else if (n == 0) {
        g_bord[0] = '\0';
    }
    g_met_bord = g_bord[0] != '0';
    Log("omgeving: RG_RHIO_EERSTEBYTE=%s, RG_RHIO_BORD=%s (bordtoestand uit gedeeld geheugen: %s)",
        g_eerstebyte_gezet ? "gezet" : "afwezig", g_bord[0] ? g_bord : "afwezig", g_met_bord ? "ja" : "nee");
}

// --- Het bordbeeld van de daemon (ons eigen gedeelde geheugen) --------------------------------
// De daemon schrijft de mapping "TeknoParrot_JvsState" (indeling WartranTroopers): besturing als
// 32-bits bitveld op offset 8 (bit 0 test, 1 service, 2 munt 1, 3 munt 2, 4 menu omhoog, 5 omlaag,
// per speler start/trekker/herladen/granaat/extra) en de richtbytes op 12..19 (P1X, P1Y, P2X, ...).
// Zo hoeft het bord niet zelf bedacht te worden: het is de toestand die de daemon al bijhoudt.
unsigned char* g_bordbeeld = nullptr;

using OpenFileMappingAFn2 = HANDLE(WINAPI*)(DWORD, BOOL, LPCSTR);
using MapViewOfFileFn2 = LPVOID(WINAPI*)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);

void OpenBordbeeld() {
    static HANDLE bewaard = nullptr;      // het handvat blijft leven; niet elke aanroep een nieuw
    static bool al_gemeld = false;
    if (g_bordbeeld) return;
    HMODULE k = GetModuleHandleA("KERNEL32.dll");
    if (!k) return;
    auto open = reinterpret_cast<OpenFileMappingAFn2>(GetProcAddress(k, "OpenFileMappingA"));
    auto view = reinterpret_cast<MapViewOfFileFn2>(GetProcAddress(k, "MapViewOfFile"));
    if (!open || !view) return;
    if (!bewaard) bewaard = open(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
    if (!bewaard) {
        if (!al_gemeld) {   // een keer melden; hij blijft het daarna wel proberen
            LogFout("bordbeeld: mapping TeknoParrot_JvsState (nog) niet gevonden (fout %lu)",
                static_cast<unsigned long>(GetLastError()));
            al_gemeld = true;
        }
        return;
    }
    g_bordbeeld = static_cast<unsigned char*>(view(bewaard, FILE_MAP_READ, 0, 0, 0));
    Log("bordbeeld: mapping TeknoParrot_JvsState %s", g_bordbeeld ? "gekoppeld" : "NIET gekoppeld");
}

/* De toestand in het antwoord zetten. De kaderregel blijft: byte 0 is de code, byte 6 de som van de
 * eerste zes bytes. Bytes 1-5 dragen de toestand: 1 = besturingsbyte (munt/start/trekker), 2 = de
 * richtas van speler 1 (X), 3 = die van speler 1 (Y), 4 en 5 blijven 0. */
/* Proefstanden (RG_RHIO_TOETS) om te zien of de I/O-controle *invoer* wil in plaats van een vaste
 * waarde. De bits van de indeling WartranTroopers: munt 1 = bit 2 (0x04), start van speler 1 = bit 6
 * (0x40), trekker van speler 1 = bit 7 (0x80). De assen staan op offset 12 (p1.x) en 13 (p1.y).
 *   cyclus  munt 1 s, dan start 1 s, dan trekker 1 s, dan niets (blijft rondgaan)
 *   nul     besturingsbyte 0x00        vol   besturingsbyte 0xFF
 *   buiten  richtas van speler 1 op 0x20/0xE0 in plaats van het midden
 * Zonder deze variabele (of met een lege waarde) komt de toestand gewoon uit het bordbeeld. */
void ProefToestand(unsigned char* k) {
    static const std::string stand = [] {
        char v[16] = {0};
        GetEnvironmentVariableA("RG_RHIO_TOETS", v, sizeof v);
        return std::string(v);
    }();
    if (stand.empty()) return;
    if (stand == "nul") {
        k[1] = 0x00;
        return;
    }
    if (stand == "vol") {
        k[1] = 0xff;
        return;
    }
    if (stand == "buiten") {
        k[2] = 0x20;
        k[3] = 0xe0;
        return;
    }
    if (stand == "cyclus") {
        static const unsigned char stappen[4] = {0x04, 0x40, 0x80, 0x00};   /* munt, start, trekker, niets */
        k[1] = stappen[(GetTickCount() / 1000u) % 4u];
    }
}

void VulToestand(unsigned char* k) {
    if (!g_bordbeeld) return;
    const unsigned besturing = *reinterpret_cast<const unsigned*>(g_bordbeeld + 8);
    k[1] = static_cast<unsigned char>(besturing & 0xff);
    k[2] = g_bordbeeld[12];
    k[3] = g_bordbeeld[13];
    k[4] = 0;
    k[5] = 0;
    ProefToestand(k);
}

void BouwAntwoord() {
    if (g_laatste_lengte < 7) return;
    unsigned char* k = g_laatste_kader;
    const char* mode = AntwoordStand();
    if (mode && std::strcmp(mode, "nul") == 0) {
        std::memset(k, 0, g_laatste_lengte);
        return;
    }
    /* Eerste byte en bordtoestand komen uit g_eerstebyte/g_met_bord, een keer gelezen in LeesOmgeving. */
    if (g_eerstebyte_gezet) {
        k[0] = g_eerstebyte;
    } else if (k[0] == 0x25) {
        /* The poll command. The game's reply loop (0x49075e..0x49094c, helper 0x4902c0) sends it again until the
         * first byte of the reply EQUALS the command byte; a first byte from the "approved" list (0x94...) is only
         * a status code and never ends the loop (it polled 0x25 forever, "I/O CHECKING"). 0x25 is not in the list,
         * so the reader takes it as a data frame: 6 more bytes with byte 6 = sum of bytes 0..5 (done below). Its
         * handler (0x49085f) latches the board flags and sets ds:0xde7000. */
    } else if (!CodeGoed(k[0])) {
        k[0] = 0x94;   /* deze opdrachtcode is geen geldig antwoord: "bord aanwezig" */
    }
    /* De toestand uit ons eigen gedeelde geheugen (de daemon schrijft die), tenzij de proefstand
     * "niks" of "nul" aan staat. Met RG_RHIO_BORD=0 blijft het een echo. */
    if (g_met_bord) {
        OpenBordbeeld();
        VulToestand(k);
    }
    unsigned som = 0;
    for (int i = 0; i < 6; ++i) som += k[i];
    k[6] = static_cast<unsigned char>(som & 0xff);
}

BOOL WINAPI rg_ClearCommError(HANDLE h, LPDWORD fouten, LPCOMSTAT stat) {
    if (h == g_com_handle) {
        DWORD rest;
        if (g_bord_cs_klaar) EnterCriticalSection(&g_bord_cs);
        /* Geen spookdata: is het antwoord helemaal gelezen (of is er geen), dan is cbInQue 0. Zonder
         * dit las het spel na een volledig kader opnieuw 7 bytes en begon het rondje opnieuw. */
        rest = g_antwoord_op ? 0 : (g_laatste_lengte > g_antwoord_gelezen ? g_laatste_lengte - g_antwoord_gelezen : 0);
        /* Loggen bij een wijziging van cbInQue, en verder een levensteken zodra de vorige regel minstens
         * 1000 ms geleden is (meegenomen op deze aanroep, geen extra draad). */
        const DWORD nu = GetTickCount();
        ++g_clear_aanroepen;
        const bool loggen = rest != g_clear_gelogd || nu - g_clear_tick >= 1000;
        const DWORD aanroepen = g_clear_aanroepen;
        if (loggen) {
            g_clear_gelogd = rest;
            g_clear_aanroepen = 0;
            g_clear_tick = nu;
        }
        if (g_bord_cs_klaar) LeaveCriticalSection(&g_bord_cs);
        if (fouten) *fouten = 0;
        if (stat) {
            std::memset(stat, 0, sizeof(COMSTAT));
            /* N1: de RESTERENDE bytes. Het spel leest eerst 1 byte; daarna moet hier 6 staan, anders
             * leest het de rest nooit (en dat was precies het oude misverstand). */
            stat->cbInQue = rest;
        }
        if (loggen)
            Log("ClearCommError: cbInQue=%lu (%lu aanroepen sinds vorige regel, tick %lu)",
                static_cast<unsigned long>(rest), static_cast<unsigned long>(aanroepen),
                static_cast<unsigned long>(nu));
        return TRUE;
    }
    return g_echte_clear ? g_echte_clear(h, fouten, stat) : FALSE;
}

BOOL WINAPI rg_GetCommState(HANDLE h, LPDCB dcb) {
    if (h == g_com_handle) {
        if (!dcb) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        std::memset(dcb, 0, sizeof(DCB));
        dcb->DCBlength = sizeof(DCB);
        dcb->BaudRate = 38400;          /* Wartran-borden: 38400 8O1 (zelfde als het geweer bij TC5) */
        dcb->ByteSize = 8;
        dcb->Parity = ODDPARITY;
        dcb->StopBits = ONESTOPBIT;
        dcb->fBinary = 1;
        Log("GetCommState(bord) -> 38400 8O1");
        return TRUE;
    }
    return g_echte_getstate ? g_echte_getstate(h, dcb) : FALSE;
}

BOOL WINAPI rg_SetCommState(HANDLE h, LPDCB dcb) {
    if (h == g_com_handle) {
        Log("SetCommState(bord, %lu baud, %u bits, pariteit %u)", static_cast<unsigned long>(dcb ? dcb->BaudRate : 0),
            dcb ? dcb->ByteSize : 0, dcb ? dcb->Parity : 0);
        return TRUE;
    }
    return g_echte_setstate ? g_echte_setstate(h, dcb) : FALSE;
}

BOOL WINAPI rg_GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) {
    if (h == g_com_handle) {
        if (!t) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        std::memset(t, 0, sizeof(COMMTIMEOUTS));
        t->ReadIntervalTimeout = 10;
        t->ReadTotalTimeoutConstant = 100;
        t->ReadTotalTimeoutMultiplier = 1;
        t->WriteTotalTimeoutConstant = 100;
        t->WriteTotalTimeoutMultiplier = 1;
        Log("GetCommTimeouts(bord) -> lees %lu/%lu, schrijf %lu/%lu", static_cast<unsigned long>(t->ReadIntervalTimeout),
            static_cast<unsigned long>(t->ReadTotalTimeoutConstant),
            static_cast<unsigned long>(t->WriteTotalTimeoutConstant),
            static_cast<unsigned long>(t->WriteTotalTimeoutMultiplier));
        return TRUE;
    }
    return g_echte_gettimeouts ? g_echte_gettimeouts(h, t) : FALSE;
}

BOOL WINAPI rg_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) {
    if (h == g_com_handle) { Log("SetCommTimeouts(bord)"); return TRUE; }
    return g_echte_settimeouts ? g_echte_settimeouts(h, t) : FALSE;
}

BOOL WINAPI rg_EscapeCommFunction(HANDLE h, DWORD code) {
    if (h == g_com_handle) { Log("EscapeCommFunction(bord, 0x%lx)", static_cast<unsigned long>(code)); return TRUE; }
    return g_echte_escape ? g_echte_escape(h, code) : FALSE;
}

BOOL WINAPI rg_SetCommMask(HANDLE h, DWORD masker) {
    if (h == g_com_handle) { Log("SetCommMask(bord, 0x%lx)", static_cast<unsigned long>(masker)); return TRUE; }
    return g_echte_setmask ? g_echte_setmask(h, masker) : FALSE;
}

BOOL WINAPI rg_WaitCommEvent(HANDLE h, LPDWORD masker, LPOVERLAPPED ov) {
    if (h == g_com_handle) {
        /* Er is altijd een antwoord klaar: het bord heeft net een opdracht gehad. */
        if (masker) *masker = EV_RXCHAR;
        if (ov && ov->hEvent) {
            Log("WaitCommEvent(bord, overlapped) -> gebeurtenis gezet");
            SetEvent(ov->hEvent);
            SetLastError(ERROR_IO_PENDING);
            return FALSE;
        }
        Log("WaitCommEvent(bord) -> EV_RXCHAR");
        return TRUE;
    }
    return g_echte_wait ? g_echte_wait(h, masker, ov) : FALSE;
}

BOOL WINAPI rg_GetOverlappedResult(HANDLE h, LPOVERLAPPED ov, LPDWORD aantal, BOOL wachten) {
    if (h == g_com_handle) {
        /* Het aantal bytes van de laatste ReadFile/WriteFile, niet de lengte van het hele kader. */
        DWORD overdracht;
        if (g_bord_cs_klaar) EnterCriticalSection(&g_bord_cs);
        overdracht = g_laatste_overdracht;
        if (g_bord_cs_klaar) LeaveCriticalSection(&g_bord_cs);
        Log("GetOverlappedResult(bord) -> %lu bytes", static_cast<unsigned long>(overdracht));
        if (aantal) *aantal = overdracht;
        return TRUE;
    }
    return g_echte_ovl ? g_echte_ovl(h, ov, aantal, wachten) : FALSE;
}

/* De originelen eerst ophalen en dan pas omzetten: anders kan een draad van het spel de haak
 * binnengaan terwijl g_echte_* nog leeg is en dan faalt de aanroep. */
void onthoud_originelen() {
    HMODULE k = GetModuleHandleA("KERNEL32.dll");
    if (!k) return;
    if (!g_echte_gpa) g_echte_gpa = reinterpret_cast<GetProcAddrFn>(GetProcAddress(k, "GetProcAddress"));
    if (!g_echte_createfile) g_echte_createfile = reinterpret_cast<CreateFileAFn>(GetProcAddress(k, "CreateFileA"));
    if (!g_echte_ioctl) g_echte_ioctl = reinterpret_cast<DeviceIoControlFn>(GetProcAddress(k, "DeviceIoControl"));
    if (!g_echte_read) g_echte_read = reinterpret_cast<ReadFileFn>(GetProcAddress(k, "ReadFile"));
    if (!g_echte_write) g_echte_write = reinterpret_cast<WriteFileFn>(GetProcAddress(k, "WriteFile"));
    if (!g_echte_purge) g_echte_purge = reinterpret_cast<PurgeCommFn>(GetProcAddress(k, "PurgeComm"));
    if (!g_echte_clear) g_echte_clear = reinterpret_cast<ClearCommErrorFn>(GetProcAddress(k, "ClearCommError"));
    if (!g_echte_getstate) g_echte_getstate = reinterpret_cast<GetCommStateFn>(GetProcAddress(k, "GetCommState"));
    if (!g_echte_setstate) g_echte_setstate = reinterpret_cast<SetCommStateFn>(GetProcAddress(k, "SetCommState"));
    if (!g_echte_gettimeouts) g_echte_gettimeouts = reinterpret_cast<GetCommTimeoutsFn>(GetProcAddress(k, "GetCommTimeouts"));
    if (!g_echte_settimeouts) g_echte_settimeouts = reinterpret_cast<SetCommTimeoutsFn>(GetProcAddress(k, "SetCommTimeouts"));
    if (!g_echte_escape) g_echte_escape = reinterpret_cast<EscapeCommFunctionFn>(GetProcAddress(k, "EscapeCommFunction"));
    if (!g_echte_setmask) g_echte_setmask = reinterpret_cast<SetCommMaskFn>(GetProcAddress(k, "SetCommMask"));
    if (!g_echte_wait) g_echte_wait = reinterpret_cast<WaitCommEventFn>(GetProcAddress(k, "WaitCommEvent"));
    if (!g_echte_ovl) g_echte_ovl = reinterpret_cast<GetOverlappedResultFn>(GetProcAddress(k, "GetOverlappedResult"));
}

/* Synchroon installeren (DllMain): het spel mag geen aanroep doen voordat de haken er staan. Dit is
 * alleen geheugen schrijven, geen LoadLibrary, dus het mag daar. */
/* Geeft terug of elke haak gevonden is; dan is een tweede ronde zinloos. */
/* De map van deze DLL (voor markerbestanden en het logboek van de meting). */
std::string EigenMap() {
    char eigen[MAX_PATH] = {0};
    HMODULE zelf = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&EigenMap), &zelf))
        GetModuleFileNameA(zelf, eigen, sizeof eigen);
    if (!eigen[0]) return std::string();
    std::string map(eigen);
    const size_t schuin = map.find_last_of("\\/");
    return schuin == std::string::npos ? std::string() : map.substr(0, schuin);
}

bool Bestaat(const std::string& pad) {
    FILE* f = std::fopen(pad.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

/* --- (D) Wie houdt het slot? ---------------------------------------------------------------
 * De hoofddraad van Robin Hood wacht met WaitForSingleObject(mutex, INFINITE) in de lock-helper op
 * 0x580fa0-0x580fde, met de unlock op 0x580ff6. Deze drie haken laten zien welk handvat dat is en welke
 * draad er lang op wacht (de wachtregels); ReleaseMutex en (na de eerste 32 regels) CreateMutexA worden
 * alleen nog geteld (review 14, ronde 2). Alleen aan met RG_RHIO_MUTEX=1. */
bool MutexAan() {
    static const bool aan = [] {
        char v[8] = {0};
        if (GetEnvironmentVariableA("RG_RHIO_MUTEX", v, sizeof v) != 0) return v[0] != '0';
        const std::string map = EigenMap();
        return !map.empty() && Bestaat(map + "\\rg-rhio-mutex.aan");
    }();
    return aan;
}

/* Vanaf deze duur noemen we een wachtdienst "lang" en loggen we het einde (review 14, punt a; begrensd
 * door kMutexMiddelMax, review 14 ronde 2). */
constexpr DWORD kMutexLangMs = 50;

using WaitForSingleObjectFn = DWORD(WINAPI*)(HANDLE, DWORD);
using ReleaseMutexFn = BOOL(WINAPI*)(HANDLE);
using CreateMutexAFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
WaitForSingleObjectFn g_echte_waitobj = nullptr;
ReleaseMutexFn g_echte_release = nullptr;
CreateMutexAFn g_echte_createmutex = nullptr;

/* De beller als game.exe+RVA, zodat een adres in het logboek meteen een functie is. */
std::string BellerTekst(const void* beller) {
    char buf[64];
    HMODULE exe = GetModuleHandleA(nullptr);
    if (exe && beller >= static_cast<const void*>(exe)) {
        const ULONG_PTR basis = reinterpret_cast<ULONG_PTR>(exe);
        const ULONG_PTR b = reinterpret_cast<ULONG_PTR>(beller);
        if (b - basis < 0x2000000) {
            std::snprintf(buf, sizeof buf, "game.exe+0x%lx", static_cast<unsigned long>(b - basis));
            return buf;
        }
    }
    std::snprintf(buf, sizeof buf, "%p", beller);
    return buf;
}

/* De originelen van de sloten, uit kernel32 zelf en niet uit de importtabel. Twee redenen (review 12,
 * punt 3): de importtabel kan op dat moment al ons eigen haak-adres bevatten (tweede ronde van
 * installeer_haken -> de haak roept zichzelf aan, eindeloze herhaling), en tussen het omzetten van het
 * slot en het onthouden van het origineel zit anders een ogenblik waarin de haak WAIT_FAILED of NULL
 * teruggeeft. */
void onthoud_sloten() {
    HMODULE k = GetModuleHandleA("KERNEL32.dll");
    if (!k) return;
    if (!g_echte_waitobj)
        g_echte_waitobj = reinterpret_cast<WaitForSingleObjectFn>(GetProcAddress(k, "WaitForSingleObject"));
    if (!g_echte_release) g_echte_release = reinterpret_cast<ReleaseMutexFn>(GetProcAddress(k, "ReleaseMutex"));
    if (!g_echte_createmutex)
        g_echte_createmutex = reinterpret_cast<CreateMutexAFn>(GetProcAddress(k, "CreateMutexA"));
}

/* Het mutex-budget moet blijven tot er echt iets hangt (review 14, ronde 2). Daarom per aanroep alleen
 * tellen en af en toe een samenvatting: ReleaseMutex en CreateMutexA lieten elk een regel achter, en
 * een spel dat een slot vaak pakt en teruggeeft at zo het hele budget op. De eerste kMutexNamenMax
 * CreateMutexA-regels blijven wel volledig (naam en handvat): daarmee is een handvat uit een
 * wachtregel terug te vinden. */
constexpr LONG kMutexNamenMax = 32;
constexpr DWORD kMutexTellingMs = 10000;
/* Vanaf deze duur kan een wachtdienst een hang zijn: dan komt het BEGIN in het logboek, voor het verder
 * blokkeren. Wat eerder terugkomt is gewoon verkeer. */
constexpr DWORD kMutexHangMs = 1000;
/* Wachtdiensten tussen kMutexLangMs en kMutexHangMs (een slot dat even bezet was) krijgen samen
 * hoogstens zoveel regels; daarna alleen de teller. Zo blijft het grootste deel van het budget over
 * voor de wachtdiensten van een seconde en langer. */
constexpr LONG kMutexMiddelMax = 500;
volatile LONG g_mutex_create = 0;
volatile LONG g_mutex_release = 0;
volatile LONG g_mutex_wacht = 0;
volatile LONG g_mutex_verlopen = 0;
volatile LONG g_mutex_middel = 0;
volatile LONG g_mutex_hang = 0;

/* Hoogstens een keer per kMutexTellingMs een regel met de tellers. Atomair, zoals de tempo-poort van
 * LeesWachthandvatten: twee draden tegelijk schrijven niet allebei. Staat het spel stil, dan roept het
 * deze haken niet meer aan en komt er ook geen telling meer bij. */
void MeldMutexTelling() {
    static volatile LONG g_mutex_tik = 0;
    const DWORD nu = GetTickCount();
    const LONG vorige = g_mutex_tik;
    if (vorige != 0 && nu - static_cast<DWORD>(vorige) < kMutexTellingMs) return;
    if (InterlockedCompareExchange(&g_mutex_tik, static_cast<LONG>(nu ? nu : 1), vorige) != vorige) return;
    if (vorige == 0) return;   // de eerste aanroep zet alleen de klok
    LogMutex("mutex: telling: CreateMutexA=%ld ReleaseMutex=%ld wachtdiensten=%ld (verlopen=%ld, "
             ">=%lu ms=%ld, >=%lu ms=%ld)",
             g_mutex_create, g_mutex_release, g_mutex_wacht, g_mutex_verlopen,
             static_cast<unsigned long>(kMutexLangMs), g_mutex_middel, static_cast<unsigned long>(kMutexHangMs),
             g_mutex_hang);
}

/* GetLastError blijft in deze drie haken precies wat de echte functie achterliet (review 14, ronde 2):
 * MutexAan (bij de eerste aanroep: omgeving en markerbestand), LogMutex en MeldMutexTelling kunnen hem
 * veranderen, en het spel kan hem na de aanroep lezen. Daarom: de waarde van de beller bewaren en voor
 * elke echte aanroep terugzetten, de waarde na de laatste echte aanroep bewaren en vlak voor het
 * teruggeven terugzetten. */
HANDLE WINAPI rg_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL eigenaar, LPCSTR naam) {
    HANDLE h = g_echte_createmutex ? g_echte_createmutex(sa, eigenaar, naam) : nullptr;
    const DWORD fout = GetLastError();
    if (MutexAan()) {
        if (InterlockedIncrement(&g_mutex_create) <= kMutexNamenMax)
            LogMutex("mutex: CreateMutexA(naam=%s, eigenaar=%d) -> %p  beller=%s",
                     naam ? naam : "(geen)", (int)eigenaar, h, BellerTekst(__builtin_return_address(0)).c_str());
        MeldMutexTelling();
    }
    SetLastError(fout);
    return h;
}

DWORD WINAPI rg_WaitForSingleObject(HANDLE h, DWORD ms) {
    const DWORD fout_in = GetLastError();
    if (!MutexAan() || ms == 0) {
        SetLastError(fout_in);
        return g_echte_waitobj ? g_echte_waitobj(h, ms) : WAIT_FAILED;
    }
    /* Een wachtdienst van 0 ms kan niet blijven hangen (die komt meteen terug), en juist die vulden het
     * budget: in de hermeting waren alle 3000 mutex-regels zo'n poll van de CRT (game.exe+0x90bdd, 0 ms).
     * Ze lopen hierboven ongemeten door.
     *
     * De rest (review 14, ronde 2): het budget moet blijven tot er echt iets hangt, dus alleen een
     * wachtdienst die LANG blijkt komt in het logboek -- ook bij INFINITE, want de lock-helper van het
     * spel wacht INFINITE op een slot dat bijna altijd vrij is. Een wachtdienst met INFINITE of een
     * time-out boven kMutexHangMs wachten we eerst zelf hoogstens kMutexHangMs. Komt hij binnen die tijd
     * terug (het gewone geval), dan is dat de uitkomst. Loopt die eerste ronde af, dan schrijven we het
     * BEGIN weg en wachten pas daarna de rest (INFINITE blijft INFINITE): een wachter die nooit
     * terugkomt staat dus vóór het verder blokkeren in het logboek (review 12, punt 3). Voor het spel is
     * dit hetzelfde: een WaitForSingleObject die afloopt verandert niets aan het object, en de totale
     * wachttijd blijft gelijk. Een wachtdienst die op zijn eigen korte time-out afloopt is een poll en
     * wordt alleen geteld. */
    InterlockedIncrement(&g_mutex_wacht);
    const void* beller = __builtin_return_address(0);
    const DWORD begin = GetTickCount();
    const bool kan_hangen = ms == INFINITE || ms > kMutexHangMs;
    SetLastError(fout_in);
    DWORD r = g_echte_waitobj ? g_echte_waitobj(h, kan_hangen ? kMutexHangMs : ms) : WAIT_FAILED;
    DWORD fout = GetLastError();
    if (!kan_hangen || r != WAIT_TIMEOUT) {
        const DWORD duur = GetTickCount() - begin;
        if (r == WAIT_TIMEOUT) {
            InterlockedIncrement(&g_mutex_verlopen);
        } else if (duur >= kMutexLangMs) {
            if (InterlockedIncrement(&g_mutex_middel) <= kMutexMiddelMax)
                LogMutex("mutex: draad %lu wachtte op %p (%lu ms, geduurd %lu ms) -> %lu  beller=%s",
                         static_cast<unsigned long>(GetCurrentThreadId()), h, static_cast<unsigned long>(ms),
                         static_cast<unsigned long>(duur), static_cast<unsigned long>(r),
                         BellerTekst(beller).c_str());
        }
        MeldMutexTelling();
        SetLastError(fout);
        return r;
    }
    /* Mogelijk een hang: nu het begin, voor het verder blokkeren. */
    InterlockedIncrement(&g_mutex_hang);
    const DWORD tid = GetCurrentThreadId();
    LogMutex("mutex: draad %lu GAAT lang wachten op %p (%lu ms, al %lu ms bezet)  beller=%s",
             static_cast<unsigned long>(tid), h, static_cast<unsigned long>(ms),
             static_cast<unsigned long>(GetTickCount() - begin), BellerTekst(beller).c_str());
    DWORD rest = INFINITE;
    if (ms != INFINITE) {
        const DWORD al = GetTickCount() - begin;
        rest = al < ms ? ms - al : 0;
    }
    SetLastError(fout_in);
    r = g_echte_waitobj(h, rest);
    fout = GetLastError();
    LogMutex("mutex: draad %lu wachtte op %p (%lu ms, geduurd %lu ms) -> %lu  beller=%s",
             static_cast<unsigned long>(tid), h, static_cast<unsigned long>(ms),
             static_cast<unsigned long>(GetTickCount() - begin), static_cast<unsigned long>(r),
             BellerTekst(beller).c_str());
    MeldMutexTelling();
    SetLastError(fout);
    return r;
}

BOOL WINAPI rg_ReleaseMutex(HANDLE h) {
    BOOL r = g_echte_release ? g_echte_release(h) : FALSE;
    const DWORD fout = GetLastError();
    if (MutexAan()) {
        InterlockedIncrement(&g_mutex_release);
        MeldMutexTelling();
    }
    SetLastError(fout);
    return r;
}

bool installeer_haken() {
    bool alles = true;
    HMODULE exe = GetModuleHandleA(nullptr);
    onthoud_originelen();
    void* oud = PatchIat(exe, "KERNEL32.dll", "GetProcAddress", reinterpret_cast<void*>(&rg_GetProcAddress));
    Log("importtabel: KERNEL32!GetProcAddress %s", oud ? "omgezet" : "NIET gevonden");
    void* oud2 = PatchIat(exe, "KERNEL32.dll", "CreateFileA", reinterpret_cast<void*>(&rg_CreateFileA));
    Log("importtabel: KERNEL32!CreateFileA %s", oud2 ? "omgezet" : "NIET gevonden");
    void* oud3 = PatchIat(exe, "KERNEL32.dll", "DeviceIoControl", reinterpret_cast<void*>(&rg_DeviceIoControl));
    Log("importtabel: KERNEL32!DeviceIoControl %s", oud3 ? "omgezet" : "NIET gevonden");
    // Niet elke exe importeert elke comm-functie. De essentiële moeten er zijn (anders werkt de
    // bordkant niet); de rest is optioneel en mag ontbreken zonder dat de herprobeer-draad start.
    struct { const char* naam; void* nep; void** echt; bool nodig; } comm[] = {
        {"ReadFile", reinterpret_cast<void*>(&rg_ReadFile), reinterpret_cast<void**>(&g_echte_read), true},
        {"WriteFile", reinterpret_cast<void*>(&rg_WriteFile), reinterpret_cast<void**>(&g_echte_write), true},
        {"PurgeComm", reinterpret_cast<void*>(&rg_PurgeComm), reinterpret_cast<void**>(&g_echte_purge), false},
        {"ClearCommError", reinterpret_cast<void*>(&rg_ClearCommError), reinterpret_cast<void**>(&g_echte_clear), true},
        {"GetCommState", reinterpret_cast<void*>(&rg_GetCommState), reinterpret_cast<void**>(&g_echte_getstate), false},
        {"SetCommState", reinterpret_cast<void*>(&rg_SetCommState), reinterpret_cast<void**>(&g_echte_setstate), false},
        {"GetCommTimeouts", reinterpret_cast<void*>(&rg_GetCommTimeouts), reinterpret_cast<void**>(&g_echte_gettimeouts), true},
        {"SetCommTimeouts", reinterpret_cast<void*>(&rg_SetCommTimeouts), reinterpret_cast<void**>(&g_echte_settimeouts), false},
        {"EscapeCommFunction", reinterpret_cast<void*>(&rg_EscapeCommFunction), reinterpret_cast<void**>(&g_echte_escape), false},
        {"SetCommMask", reinterpret_cast<void*>(&rg_SetCommMask), reinterpret_cast<void**>(&g_echte_setmask), false},
        {"WaitCommEvent", reinterpret_cast<void*>(&rg_WaitCommEvent), reinterpret_cast<void**>(&g_echte_wait), false},
        {"GetOverlappedResult", reinterpret_cast<void*>(&rg_GetOverlappedResult), reinterpret_cast<void**>(&g_echte_ovl), false},
    };
    /* De mutex-haken zijn optioneel: niet elke exe importeert ze. */
    struct { const char* naam; void* nep; void** echt; } sloten[] = {
        {"WaitForSingleObject", reinterpret_cast<void*>(&rg_WaitForSingleObject),
         reinterpret_cast<void**>(&g_echte_waitobj)},
        {"ReleaseMutex", reinterpret_cast<void*>(&rg_ReleaseMutex), reinterpret_cast<void**>(&g_echte_release)},
        {"CreateMutexA", reinterpret_cast<void*>(&rg_CreateMutexA), reinterpret_cast<void**>(&g_echte_createmutex)},
    };
    /* De mutex-haken alleen als erom gevraagd is (RG_RHIO_MUTEX=1 of rg-rhio-mutex.aan naast deze
     * DLL). Ze op elke start installeren bleek Robin Hood meteen te laten stoppen (review 11): het
     * zijn haken op WaitForSingleObject/CreateMutexA, die de CRT al heel vroeg aanroept. */
    if (MutexAan()) {
        onthoud_sloten();   // eerst de originelen, dan pas omzetten
        for (auto& sl : sloten) {
            void* nu = LeesIat(exe, "KERNEL32.dll", sl.naam);
            if (!nu) {
                Log("importtabel: KERNEL32!%s NIET gevonden", sl.naam);
                continue;
            }
            if (nu == sl.nep) {   // al van ons (tweede ronde): niets doen
                Log("importtabel: KERNEL32!%s was al omgezet", sl.naam);
                continue;
            }
            PatchIat(exe, "KERNEL32.dll", sl.naam, sl.nep);
            Log("importtabel: KERNEL32!%s omgezet", sl.naam);
        }
    }
    for (auto& c : comm) {
        void* nu = LeesIat(exe, "KERNEL32.dll", c.naam);
        if (!nu) {
            if (c.nodig) alles = false;
            Log("importtabel: KERNEL32!%s NIET gevonden%s", c.naam, c.nodig ? "" : " (optioneel)");
            continue;
        }
        if (nu == c.nep) {   // al van ons (tweede ronde): niets doen
            Log("importtabel: KERNEL32!%s was al omgezet", c.naam);
            continue;
        }
        PatchIat(exe, "KERNEL32.dll", c.naam, c.nep);
        Log("importtabel: KERNEL32!%s omgezet", c.naam);
    }
    return alles;
}

}  // namespace

DWORD WINAPI Herprobeer(LPVOID) {
    /* Alleen als iets niet gevonden is bij het laden: na een seconde nog een keer kijken (dan is de
     * importtabel van het spel zeker compleet). */
    Sleep(1000);
    installeer_haken();
    return 0;
}

/* De meetdraad van rg-compat (docs/rg-compat.md, RG_COMPAT_SAMPLE=<ms>) ook hier: Robin Hood
 * heeft maar een haak-DLL (deze), dus laden we rg-compat.dll zelf als de variabele gezet is. Hij
 * laat per draad zien waar het spel zit -- precies wat nodig is om te zien waar het op wacht. */
/* Niet in DllMain (daar kan LoadLibrary de loader-lock pakken): pas bij de eerste echte haak-aanroep.
 * eenmalig, en alleen als RG_COMPAT_SAMPLE gezet is. */
void LaadMeetdraad() {
    static volatile LONG gedaan = 0;
    if (InterlockedCompareExchange(&gedaan, 1, 0) != 0) return;   // geen call_once (winpthreads)

    /* Waar staat deze DLL? Daar zoeken we het markerbestand en schrijven we het logboek van de
     * meting, zodat het altijd terug te vinden is (het %TEMP%-pad van de prefix wisselt). */
    const std::string map = EigenMap();

    char v[32] = {0};
    /* Nul betekent UIT, net als bij RG_RHIO_MUTEX en RG_RHIO_STACK (review 14, punt d): eerst telde
     * elke waarde als "aan", dus RG_COMPAT_SAMPLE=0 zette de meetdraad juist aan. En zoals daar beslist
     * een GEZETTE variabele, ook "0": eerder telde "0" als niet gezet, zodat een marker hem alsnog
     * overschreef en de meetdraad toch laadde (review 14, ronde 2). */
    const bool omgeving_gezet = GetEnvironmentVariableA("RG_COMPAT_SAMPLE", v, sizeof v) != 0;
    bool aan = omgeving_gezet && v[0] != '0';

    /* Markerbestand: <map van deze DLL>\rg-compat.aan. De inhoud is het aantal milliseconden
     * (leeg of onleesbaar = 200). Zo kan de meetdraad aan zonder de omgeving aan te raken.
     * BELANGRIJK (review 12, punt 4): een expliciet gezette omgevingsvariabele wint. Eerder zette het
     * markerbestand de variabele overschrijvend, waardoor een marker in de view een bewust gekozen
     * waarde stil veranderde. */
    if (!map.empty()) {
        const std::string marker = map + "\\rg-compat.aan";
        FILE* m = std::fopen(marker.c_str(), "rb");
        if (m) {
            char inhoud[32] = {0};
            const size_t n = std::fread(inhoud, 1, sizeof inhoud - 1, m);
            std::fclose(m);
            if (omgeving_gezet) {
                Log("meetdraad: markerbestand %s gevonden, maar RG_COMPAT_SAMPLE staat al expliciet op "
                    "%s (%s); de omgeving wint",
                    marker.c_str(), v, aan ? "aan" : "uit");
            } else {
                aan = true;
                long ms = n ? std::strtol(inhoud, nullptr, 10) : 0;
                if (ms <= 0) ms = 200;
                char tekst[32];
                std::snprintf(tekst, sizeof tekst, "%ld", ms);
                SetEnvironmentVariableA("RG_COMPAT_SAMPLE", tekst);
                Log("meetdraad: markerbestand %s gevonden; RG_COMPAT_SAMPLE=%s", marker.c_str(), tekst);
            }
        }
    }
    if (!aan) {
        if (omgeving_gezet) Log("meetdraad: RG_COMPAT_SAMPLE=%s betekent uit", v);
        return;
    }

    /* Het logboek van de meting naast deze DLL zetten (vaste naam; er draait er maar een). */
    if (!map.empty() && GetEnvironmentVariableA("RG_COMPAT_SAMPLE_LOG", v, sizeof v) == 0) {
        const std::string logpad = map + "\\rg-compat-sample.txt";
        SetEnvironmentVariableA("RG_COMPAT_SAMPLE_LOG", logpad.c_str());
        Log("meetdraad: logboek wordt %s", logpad.c_str());
    }

    char ms[32] = {0};
    GetEnvironmentVariableA("RG_COMPAT_SAMPLE", ms, sizeof ms);
    HMODULE m = LoadLibraryA("rg-compat.dll");
    Log("meetdraad: rg-compat.dll %s (RG_COMPAT_SAMPLE=%s)", m ? "geladen" : "NIET geladen", ms);
}

/* Welke markers staan er in de map van deze DLL? Een keer loggen, zodat een meting later te
 * verklaren is (review 12, punt 4). Een marker mag nooit in de git-kluis belanden; daar hoort de
 * .gitignore-regel bij. */
void MeldMarkers() {
    const std::string map = EigenMap();
    if (map.empty()) {
        Log("markers: de map van deze DLL is niet te bepalen");
        return;
    }
    struct { const char* naam; const char* wat; } lijst[] = {
        {"rg-rhio-stack.aan", "stackregels (RG_RHIO_STACK)"},
        {"rg-rhio-mutex.aan", "mutex-haken (RG_RHIO_MUTEX)"},
        {"rg-compat.aan", "meetdraad (RG_COMPAT_SAMPLE)"},
    };
    /* Voor alle drie geldt dezelfde regel: een gezette variabele beslist (ook "0" = uit), de marker
     * telt alleen als de variabele niet gezet is (MutexAan, StackAan, LaadMeetdraad; review 14,
     * ronde 2). De regel noemt daarom de waarde en wat die betekent. */
    for (const auto& l : lijst) {
        const bool aanwezig = Bestaat(map + "\\" + l.naam);
        const char* var = std::strcmp(l.naam, "rg-compat.aan") == 0       ? "RG_COMPAT_SAMPLE"
                          : std::strcmp(l.naam, "rg-rhio-mutex.aan") == 0 ? "RG_RHIO_MUTEX"
                                                                          : "RG_RHIO_STACK";
        char omg[32] = {0};
        const bool via_omg = GetEnvironmentVariableA(var, omg, sizeof omg) != 0;
        if (via_omg)
            Log("marker %s: %s, omgeving expliciet %s=%s (%s)%s (%s)", l.naam, aanwezig ? "aanwezig" : "afwezig",
                var, omg, omg[0] != '0' ? "aan" : "uit", aanwezig ? " -- de omgeving wint" : "", l.wat);
        else
            Log("marker %s: %s (%s)", l.naam, aanwezig ? "aanwezig" : "afwezig", l.wat);
    }
}

/* --- (D) Wie wacht waarop, zonder een import-haak -------------------------------------------
 * De mutex-haken (WaitForSingleObject/ReleaseMutex/CreateMutexA) laten de game stoppen. Deze functie
 * raakt de importtabel daarom niet aan: ze zet elke draad heel even stil, leest de instructiewijzer
 * en de stapel, en laat hem weer door. Dat is precies wat de meetdraad van rg-compat ook doet en dat
 * is veilig gebleken. Alleen aan met rg-rhio-stack.aan of RG_RHIO_STACK=1. */
bool StackAan() {
    static const bool aan = [] {
        char v[8] = {0};
        if (GetEnvironmentVariableA("RG_RHIO_STACK", v, sizeof v) != 0) return v[0] != '0';
        const std::string map = EigenMap();
        return !map.empty() && Bestaat(map + "\\rg-rhio-stack.aan");
    }();
    return aan;
}

/* Waar ligt dit adres? module+rva, zodat een treffer met de meting van rg-compat te vergelijken is. */
std::string WaarTekst(ULONG_PTR a) {
    char buf[96];
    HMODULE m = nullptr;
    if (a && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(a), &m)) {
        char pad[MAX_PATH] = {0};
        GetModuleFileNameA(m, pad, sizeof pad);
        const char* b = std::strrchr(pad, '\\');
        std::snprintf(buf, sizeof buf, "%s+0x%lx", b ? b + 1 : pad, static_cast<unsigned long>(a - reinterpret_cast<ULONG_PTR>(m)));
    } else {
        std::snprintf(buf, sizeof buf, "%p", reinterpret_cast<void*>(a));
    }
    return buf;
}

/* Een meting van een stilstaande draad. Alleen getallen: geen std::string, geen logboek, geen
 * toewijzing. Dat is punt 1 van review 12: als we tijdens de stilstand loggen of geheugen vragen en de
 * stilgezette draad houdt net de sluis van het logboek (FILE), de heap of de loader, dan staat alles
 * stil. */
struct DraadMeting {
    DWORD draad;
    ULONG_PTR pc;
    ULONG_PTR esp;
    ULONG_PTR args[4];
    bool heeft_ctx;
};

/* Waar begon deze draad? NtQueryInformationThread(ThreadQuerySetWin32StartAddress = 9). Zo slaan we
 * de meetdraad van rg-compat over: die zet zelf draden stil en mag niet zelf stilgezet worden. */
ULONG_PTR DraadStart(HANDLE th) {
    using NtQueryInformationThreadFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static NtQueryInformationThreadFn q = reinterpret_cast<NtQueryInformationThreadFn>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread"));
    if (!q) return 0;
    ULONG_PTR start = 0;
    ULONG terug = 0;
    if (q(th, 9, &start, sizeof start, &terug) != 0) return 0;
    return start;
}

/* Ligt dit adres in deze module? (Voor het overslaan van onze eigen draden.) */
bool InModule(HMODULE m, ULONG_PTR a) {
    if (!m || !a) return false;
    const ULONG_PTR basis = reinterpret_cast<ULONG_PTR>(m);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(basis);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(basis + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    return a >= basis && a < basis + nt->OptionalHeader.SizeOfImage;
}

bool OnzeDraad(ULONG_PTR start) {
    if (!start) return false;
    return InModule(GetModuleHandleA("rg-compat.dll"), start) ||
           InModule(GetModuleHandleA("rg-rhio.dll"), start);
}

/* De stapelwoorden van een stilstaande draad. ReadProcessMemory op het eigen proces: een ongeldig
 * adres geeft een fout in plaats van een uitzondering in deze draad. Geen log, geen toewijzing. */
void LeesStackWoorden(ULONG_PTR esp, ULONG_PTR* uit, int n) {
    for (int k = 0; k < n; ++k) uit[k] = 0;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(esp), &mi, sizeof mi)) return;
    if (mi.State != MEM_COMMIT || (mi.Protect & PAGE_GUARD) || (mi.Protect & PAGE_NOACCESS)) return;
    const ULONG_PTR eind = reinterpret_cast<ULONG_PTR>(mi.BaseAddress) + mi.RegionSize;
    HANDLE zelf = GetCurrentProcess();
    for (int k = 0; k < n; ++k) {
        const ULONG_PTR adres = esp + static_cast<ULONG_PTR>(k) * sizeof(ULONG_PTR);
        if (adres + sizeof(ULONG_PTR) > eind) continue;
        SIZE_T gelezen = 0;
        if (!ReadProcessMemory(zelf, reinterpret_cast<LPCVOID>(adres), &uit[k], sizeof(ULONG_PTR),
                               &gelezen) ||
            gelezen != sizeof(ULONG_PTR))
            uit[k] = 0;
    }
}

/* Een kleine eigen tijdklok (review 14, punt b): LeesWachthandvatten hing alleen aan rg_CreateFileA,
 * en juist als het spel vastloopt roept het geen CreateFileA meer aan -- dan kwam er op het moment dat
 * het erom gaat geen enkele stackregel meer. Deze draad begint na twintig seconden en doet het daarna
 * elke twee seconden; de functie heeft zelf al een tempo-poort van een seconde. */
DWORD WINAPI StackKlok(LPVOID) {
    Sleep(20000);
    for (;;) {
        LeesWachthandvatten();
        Sleep(2000);
    }
    return 0;
}

void StartStackKlok() {
    /* Alleen als de stackmeting aanstaat: anders deed de draad elke twee seconden niets, het hele
     * spel lang (review 14, ronde 2). StackAan wordt een keer bepaald en verandert daarna niet. */
    if (!StackAan()) return;
    static volatile LONG gedaan = 0;
    if (InterlockedCompareExchange(&gedaan, 1, 0) != 0) return;
    HANDLE d = CreateThread(nullptr, 0, StackKlok, nullptr, 0, nullptr);
    if (d) CloseHandle(d);
}

void LeesWachthandvatten() {
    if (!StackAan()) return;
    /* Een keer per seconde, maar wel atomair: twee draden die hier tegelijk langskomen mogen niet
     * allebei een ronde doen (review 12, punt 1). */
    static volatile LONG g_stack_tik = 0;
    const DWORD nu = GetTickCount();
    const LONG vorige = g_stack_tik;
    if (nu - static_cast<DWORD>(vorige) < 1000) return;
    if (InterlockedCompareExchange(&g_stack_tik, static_cast<LONG>(nu), vorige) != vorige) return;

    constexpr int kMaxDraden = 256;
    DraadMeting metingen[kMaxDraden];
    int aantal = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te;
    te.dwSize = sizeof te;
    const DWORD ik = GetCurrentThreadId();
    if (Thread32First(snap, &te)) {
        do {
            if (aantal >= kMaxDraden) break;
            if (te.dwSize < FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(DWORD)) continue;
            if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
            if (te.th32ThreadID == ik) continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                   FALSE, te.th32ThreadID);
            if (!th) continue;
            /* Eerst kijken of het onze eigen draad is (buiten de stilstand om, want GetModuleHandleA
             * pakt de loader-sluis). */
            if (OnzeDraad(DraadStart(th))) {
                CloseHandle(th);
                continue;
            }
            DraadMeting m = {te.th32ThreadID, 0, 0, {0, 0, 0, 0}, false};
            if (SuspendThread(th) != static_cast<DWORD>(-1)) {
                CONTEXT ctx = {0};
                ctx.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(th, &ctx)) {
                    m.pc = ctx.Eip;
                    m.esp = ctx.Esp;
                    m.heeft_ctx = true;
                    LeesStackWoorden(ctx.Esp, m.args, 4);
                }
                ResumeThread(th);
            }
            CloseHandle(th);
            if (m.heeft_ctx) metingen[aantal++] = m;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    /* Pas nu, met alle draden weer aan het werk, tekst maken en wegschrijven. */
    for (int i = 0; i < aantal; ++i) {
        const DraadMeting& m = metingen[i];
        char regel[512];
        std::snprintf(regel, sizeof regel, "stack: draad %lu pc=%s esp=%p args=%p %p %p %p",
                      static_cast<unsigned long>(m.draad), WaarTekst(m.pc).c_str(),
                      reinterpret_cast<void*>(m.esp), reinterpret_cast<void*>(m.args[0]),
                      reinterpret_cast<void*>(m.args[1]), reinterpret_cast<void*>(m.args[2]),
                      reinterpret_cast<void*>(m.args[3]));
        SchrijfRegelMet(regel, &g_log_stack_regels, kLogStackMax);
    }
}


BOOL WINAPI DllMain(HINSTANCE instantie, DWORD reden, LPVOID) {
    if (reden == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instantie);
        /* Eerst het slot: de haken kunnen meteen door een andere draad gebruikt worden. */
        InitializeCriticalSection(&g_bord_cs);
        g_bord_cs_klaar = true;
        LogOpen();
        Log("--- nieuw proces: de I/O-bordkant van RavenGun geladen ---");
        LeesOmgeving();   // na het slot en het logboek; voor installeer_haken, dus voor elke bordaanroep
        MeldMarkers();    // een keer opschrijven welke markers in de view aanstaan (review 12, punt 4)
        /* N6: alleen een draad als er iets niet gevonden is; anders herhaalt hij hetzelfde werk en
         * schrijft hij dezelfde regels nog eens. */
        if (!installeer_haken()) {
            HANDLE draad = CreateThread(nullptr, 0, Herprobeer, nullptr, 0, nullptr);
            if (draad) CloseHandle(draad);
        }
    }
    return TRUE;
}
