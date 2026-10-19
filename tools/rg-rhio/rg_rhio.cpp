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
// OMGEVING
//   RG_RHIO_LOG=<pad>      het logboek (standaard rg-rhio.log naast de DLL)
//   RG_RHIO_PAD=<pad>      het apparaatpad dat de opsomming teruggeeft (standaard \\.\USBPDCOM)
//   RG_RHIO_IOCTL=0        DeviceIoControl niet "gelukt" melden maar de fout van Wine doorlaten
//   RG_RHIO_STIL=1         alleen fouten loggen
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const char kStandaardPad[] = "\\\\.\\USBPDCOM";
FILE* g_log = nullptr;
bool g_stil = false;
bool g_ioctl_ok = true;
char g_pad[256] = {0};

void LogVan(const void* beller, const char* fmt, ...) {
    if (!g_log || g_stil) return;
    va_list ap;
    va_start(ap, fmt);
    char tekst[512];
    vsnprintf(tekst, sizeof tekst, fmt, ap);
    va_end(ap);
    std::fprintf(g_log, "[%8lu] beller=%p %s\n", static_cast<unsigned long>(GetTickCount()), beller, tekst);
    std::fflush(g_log);
}

void Log(const char* fmt, ...) {
    if (!g_log || g_stil) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

volatile LONG g_log_geprobeerd = 0;

void LogOpen() {
    /* Een keer proberen, ook als het niet lukt: anders doet elke CreateFileA opnieuw getenv/fopen.
     * De vlag is atomair, want meerdere draden van het spel komen hier langs. */
    if (g_log) return;
    if (InterlockedCompareExchange(&g_log_geprobeerd, 1, 0) != 0) return;
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
    g_log = std::fopen(pad, "a");
    g_stil = std::getenv("RG_RHIO_STIL") != nullptr;
    if (const char* v = std::getenv("RG_RHIO_IOCTL")) g_ioctl_ok = std::strcmp(v, "0") != 0;
    const char* p = std::getenv("RG_RHIO_PAD");
    std::snprintf(g_pad, sizeof g_pad, "%s", (p && *p) ? p : kStandaardPad);
    Log("rg-rhio geladen (32-bits): pad=%s, ioctl-gelukt=%d", g_pad, g_ioctl_ok ? 1 : 0);
}

// --- de importtabel van het hoofdprogramma ------------------------------------------------------
void* PatchIat(HMODULE mod, const char* dll, const char* func, void* repl) {
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
            void* oud = reinterpret_cast<void*>(iat[i].u1.Function);
            DWORD prot = 0;
            if (!VirtualProtect(&iat[i].u1.Function, sizeof(void*), PAGE_READWRITE, &prot)) return nullptr;
            iat[i].u1.Function = reinterpret_cast<decltype(iat[i].u1.Function)>(repl);
            VirtualProtect(&iat[i].u1.Function, sizeof(void*), prot, &prot);
            return oud;
        }
    }
    return nullptr;
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
         * RG_RHIO_ANTWOORD=0 zet dat uit; RG_RHIO_ANTWOORD=niks laat het antwoord leeg. */
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
            g_wachtend_antwoord = 0;   /* het hele antwoord is opgehaald */
        }
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
    if (h == g_com_handle) { Log("PurgeComm(bord, 0x%lx)", static_cast<unsigned long>(vlaggen)); return TRUE; }
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
 * goedkeurt (anders gaat het naar het foutpad) en de som van de eerste zes bytes in byte 6. Met
 * RG_RHIO_EERSTEBYTE is de eerste byte te kiezen (hex); RG_RHIO_ANTWOORD=nul blijft als proefstand. */
/* RG_RHIO_ANTWOORD, een keer gelezen (deze functie loopt per bordopdracht):
 *   (niet gezet) of 0 = echo met een geldige code (het gewone bedrijf)
 *   nul              = alle bytes 0 (proef: de beveiligingscontrole valt om)
 *   niks             = geen antwoord; ClearCommError meldt dan ook niets klaarstaands
 * RG_RHIO_EERSTEBYTE=<hex> forceert alleen de eerste byte van het antwoord. */
const char* AntwoordStand() {
    static const char* stand = std::getenv("RG_RHIO_ANTWOORD");
    return stand;
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
    if (g_bordbeeld) return;
    HMODULE k = GetModuleHandleA("KERNEL32.dll");
    if (!k) return;
    auto open = reinterpret_cast<OpenFileMappingAFn2>(GetProcAddress(k, "OpenFileMappingA"));
    auto view = reinterpret_cast<MapViewOfFileFn2>(GetProcAddress(k, "MapViewOfFile"));
    if (!open || !view) return;
    /* Onze eigen haken staan op de importtabel van het spel, niet op deze aanroepen; we gebruiken
     * daarom de echte functies rechtstreeks via GetProcAddress. */
    HANDLE h = open(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
    if (!h) {
        Log("bordbeeld: mapping TeknoParrot_JvsState niet gevonden (fout %lu)",
            static_cast<unsigned long>(GetLastError()));
        return;
    }
    g_bordbeeld = static_cast<unsigned char*>(view(h, FILE_MAP_READ, 0, 0, 0));
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
    char stand[16];
    if (GetEnvironmentVariableA("RG_RHIO_TOETS", stand, sizeof stand) == 0 || !stand[0]) return;
    if (std::strcmp(stand, "nul") == 0) {
        k[1] = 0x00;
        return;
    }
    if (std::strcmp(stand, "vol") == 0) {
        k[1] = 0xff;
        return;
    }
    if (std::strcmp(stand, "buiten") == 0) {
        k[2] = 0x20;
        k[3] = 0xe0;
        return;
    }
    if (std::strcmp(stand, "cyclus") == 0) {
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
    const char* keuze = std::getenv("RG_RHIO_EERSTEBYTE");
    if (keuze && *keuze) {
        k[0] = static_cast<unsigned char>(std::strtoul(keuze, nullptr, 16));
    } else if (!CodeGoed(k[0])) {
        k[0] = 0x94;   /* deze opdrachtcode is geen geldig antwoord: "bord aanwezig" */
    }
    /* De toestand uit ons eigen gedeelde geheugen (de daemon schrijft die), tenzij de proefstand
     * "niks" of "nul" aan staat. Met RG_RHIO_BORD=0 blijft het een echo. */
    char bord[8];
    const bool met_bord = GetEnvironmentVariableA("RG_RHIO_BORD", bord, sizeof bord) == 0 || bord[0] != '0';
    if (met_bord) {
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
        rest = g_laatste_lengte > g_antwoord_gelezen ? g_laatste_lengte - g_antwoord_gelezen : 0;
        if (g_bord_cs_klaar) LeaveCriticalSection(&g_bord_cs);
        if (fouten) *fouten = 0;
        if (stat) {
            std::memset(stat, 0, sizeof(COMSTAT));
            /* N1: de RESTERENDE bytes. Het spel leest eerst 1 byte; daarna moet hier 6 staan, anders
             * leest het de rest nooit (en dat was precies het oude misverstand). */
            stat->cbInQue = rest;
        }
        Log("ClearCommError(bord) -> cbInQue=%lu", static_cast<unsigned long>(rest));
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
        Log("GetOverlappedResult(bord) -> %lu bytes", static_cast<unsigned long>(g_laatste_lengte));
        if (aantal) *aantal = g_laatste_lengte;
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
    struct { const char* naam; void* nep; void** echt; } comm[] = {
        {"ReadFile", reinterpret_cast<void*>(&rg_ReadFile), reinterpret_cast<void**>(&g_echte_read)},
        {"WriteFile", reinterpret_cast<void*>(&rg_WriteFile), reinterpret_cast<void**>(&g_echte_write)},
        {"PurgeComm", reinterpret_cast<void*>(&rg_PurgeComm), reinterpret_cast<void**>(&g_echte_purge)},
        {"ClearCommError", reinterpret_cast<void*>(&rg_ClearCommError), reinterpret_cast<void**>(&g_echte_clear)},
        {"GetCommState", reinterpret_cast<void*>(&rg_GetCommState), reinterpret_cast<void**>(&g_echte_getstate)},
        {"SetCommState", reinterpret_cast<void*>(&rg_SetCommState), reinterpret_cast<void**>(&g_echte_setstate)},
        {"GetCommTimeouts", reinterpret_cast<void*>(&rg_GetCommTimeouts), reinterpret_cast<void**>(&g_echte_gettimeouts)},
        {"SetCommTimeouts", reinterpret_cast<void*>(&rg_SetCommTimeouts), reinterpret_cast<void**>(&g_echte_settimeouts)},
        {"EscapeCommFunction", reinterpret_cast<void*>(&rg_EscapeCommFunction), reinterpret_cast<void**>(&g_echte_escape)},
        {"SetCommMask", reinterpret_cast<void*>(&rg_SetCommMask), reinterpret_cast<void**>(&g_echte_setmask)},
        {"WaitCommEvent", reinterpret_cast<void*>(&rg_WaitCommEvent), reinterpret_cast<void**>(&g_echte_wait)},
        {"GetOverlappedResult", reinterpret_cast<void*>(&rg_GetOverlappedResult), reinterpret_cast<void**>(&g_echte_ovl)},
    };
    for (auto& c : comm) {
        void* oud = PatchIat(exe, "KERNEL32.dll", c.naam, c.nep);
        if (!oud) alles = false;
        Log("importtabel: KERNEL32!%s %s", c.naam, oud ? "omgezet" : "NIET gevonden");
        (void)oud;
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
void LaadMeetdraad() {
    char v[8];
    if (GetEnvironmentVariableA("RG_COMPAT_SAMPLE", v, sizeof v) == 0) return;
    HMODULE m = LoadLibraryA("rg-compat.dll");
    Log("meetdraad: rg-compat.dll %s (RG_COMPAT_SAMPLE=%s)", m ? "geladen" : "NIET geladen", v);
}

BOOL WINAPI DllMain(HINSTANCE instantie, DWORD reden, LPVOID) {
    if (reden == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instantie);
        LogOpen();
        Log("--- nieuw proces: de I/O-bordkant van RavenGun geladen ---");
        /* N6: alleen een draad als er iets niet gevonden is; anders herhaalt hij hetzelfde werk en
         * schrijft hij dezelfde regels nog eens. */
        if (!installeer_haken()) {
            HANDLE draad = CreateThread(nullptr, 0, Herprobeer, nullptr, 0, nullptr);
            if (draad) CloseHandle(draad);
        }
        LaadMeetdraad();
        InitializeCriticalSection(&g_bord_cs);
        g_bord_cs_klaar = true;
    }
    return TRUE;
}
