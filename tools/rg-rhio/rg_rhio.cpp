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
//   Deze DLL heet setupapi.dll en komt naast de exe, met WINEDLLOVERRIDES=setupapi=n. Hij
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

void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

void LogOpen() {
    if (g_log) return;
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

BOOL WINAPI rg_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD gelezen, LPOVERLAPPED ov) {
    if (h == g_com_handle) {
        /* Eerste proef: het laatste opdrachtkader terugsturen (bord aanwezig, geen schakelaars).
         * RG_RHIO_ANTWOORD=0 zet dat uit; RG_RHIO_ANTWOORD=niks laat het antwoord leeg. */
        const char* stand = std::getenv("RG_RHIO_ANTWOORD");
        if (stand && std::strcmp(stand, "niks") == 0) {
            Log("ReadFile(bord, %lu bytes gevraagd) -> geen antwoord (RG_RHIO_ANTWOORD=niks)",
                static_cast<unsigned long>(n));
            if (gelezen) *gelezen = 0;
            SetLastError(ERROR_TIMEOUT);
            return TRUE;
        }
        DWORD m = g_laatste_lengte;
        if (m > n) m = n;
        if (buf && m) std::memcpy(buf, g_laatste_kader, m);
        if (gelezen) *gelezen = m;
        Log("ReadFile(bord, %lu bytes gevraagd) -> %lu bytes antwoord (echo)", static_cast<unsigned long>(n),
            static_cast<unsigned long>(m));
        return TRUE;
    }
    return g_echte_read ? g_echte_read(h, buf, n, gelezen, ov) : FALSE;
}

BOOL WINAPI rg_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD geschreven, LPOVERLAPPED ov) {
    if (h == g_com_handle) {
        char hex[3 * 64 + 1];
        DWORD m = n < 64 ? n : 64;
        for (DWORD i = 0; i < m; ++i) std::snprintf(hex + i * 3, 4, "%02x ", reinterpret_cast<const BYTE*>(buf)[i]);
        hex[m * 3] = '\0';
        Log("WriteFile(bord, %lu bytes): %s", static_cast<unsigned long>(n), hex);
        g_laatste_lengte = n < sizeof g_laatste_kader ? n : (DWORD)(sizeof g_laatste_kader);
        if (buf) std::memcpy(g_laatste_kader, buf, g_laatste_lengte);
        if (geschreven) *geschreven = n;
        return TRUE;
    }
    return g_echte_write ? g_echte_write(h, buf, n, geschreven, ov) : FALSE;
}

BOOL WINAPI rg_PurgeComm(HANDLE h, DWORD vlaggen) {
    if (h == g_com_handle) { Log("PurgeComm(bord, 0x%lx)", static_cast<unsigned long>(vlaggen)); return TRUE; }
    return g_echte_purge ? g_echte_purge(h, vlaggen) : FALSE;
}

BOOL WINAPI rg_ClearCommError(HANDLE h, LPDWORD fouten, LPCOMSTAT stat) {
    if (h == g_com_handle) {
        if (fouten) *fouten = 0;
        if (stat) std::memset(stat, 0, sizeof(COMSTAT));
        return TRUE;
    }
    return g_echte_clear ? g_echte_clear(h, fouten, stat) : FALSE;
}

BOOL WINAPI rg_GetCommState(HANDLE h, LPDCB dcb) {
    if (h == g_com_handle) {
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
        std::memset(t, 0, sizeof(COMMTIMEOUTS));
        t->ReadIntervalTimeout = 10;
        t->ReadTotalTimeoutConstant = 100;
        t->ReadTotalTimeoutMultiplier = 1;
        t->WriteTotalTimeoutConstant = 100;
        t->WriteTotalTimeoutMultiplier = 1;
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

DWORD WINAPI StartDraad(LPVOID) {
    HMODULE exe = GetModuleHandleA(nullptr);
    void* oud = PatchIat(exe, "KERNEL32.dll", "GetProcAddress", reinterpret_cast<void*>(&rg_GetProcAddress));
    Log("importtabel: KERNEL32!GetProcAddress %s", oud ? "omgezet" : "NIET gevonden");
    if (!g_echte_gpa && oud) g_echte_gpa = reinterpret_cast<GetProcAddrFn>(oud);
    void* oud2 = PatchIat(exe, "KERNEL32.dll", "CreateFileA", reinterpret_cast<void*>(&rg_CreateFileA));
    Log("importtabel: KERNEL32!CreateFileA %s", oud2 ? "omgezet" : "NIET gevonden");
    if (!g_echte_createfile && oud2) g_echte_createfile = reinterpret_cast<CreateFileAFn>(oud2);
    void* oud3 = PatchIat(exe, "KERNEL32.dll", "DeviceIoControl", reinterpret_cast<void*>(&rg_DeviceIoControl));
    Log("importtabel: KERNEL32!DeviceIoControl %s", oud3 ? "omgezet" : "NIET gevonden");
    if (!g_echte_ioctl && oud3) g_echte_ioctl = reinterpret_cast<DeviceIoControlFn>(oud3);
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
        Log("importtabel: KERNEL32!%s %s", c.naam, oud ? "omgezet" : "NIET gevonden");
        if (!*c.echt && oud) *c.echt = oud;
    }
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instantie, DWORD reden, LPVOID) {
    if (reden == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instantie);
        LogOpen();
        Log("--- nieuw proces: setupapi.dll van RavenGun geladen ---");
        HANDLE draad = CreateThread(nullptr, 0, StartDraad, nullptr, 0, nullptr);
        if (draad) CloseHandle(draad);
    }
    return TRUE;
}
