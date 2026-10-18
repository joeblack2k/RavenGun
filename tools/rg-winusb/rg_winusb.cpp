// SPDX-License-Identifier: GPL-3.0-or-later
// winusb.dll (RavenGun, 64-bit): het USIO-bord van Time Crisis 5 (Namco ES3X) zonder de gesloten TeknoParrot-kern.
//
// WAAROM
//   TimeCrisisGame-Win64-Shipping.exe (UE3) praat via zijn eigen klasse CUsioRaw met een Namco USIO-bord: SetupDi*W zoekt de apparaat-
//   interface {E54BF304-66A3-476F-A456-C6DDAB8C7078}, CreateFileW opent het pad (FILE_FLAG_OVERLAPPED), WinUsb_Initialize /
//   QueryDeviceInformation / QueryInterfaceSettings / QueryPipe zoeken twee bulk-pijpen (IN en OUT) en een interrupt-pijp, en daarna
//   lopen alle registers via WinUsb_WritePipe (6-byte opdracht) en WinUsb_ReadPipe (gegevens). Onder Wine is er geen bord; de
//   gesloten kern emuleerde dat in het spelproces. Deze DLL doet het zelf.
//
// HOE
//   * Hij staat als winusb.dll naast het spel en wordt met WINEDLLOVERRIDES (winusb=n) afgedwongen; de exe importeert WINUSB.DLL
//     rechtstreeks, dus hij wordt bij het starten van het spel geladen, ook in het kindproces dat RSLauncher.exe start (de omgeving wordt
//     doorgegeven; de kern hoeft daar niet in).
//   * Bij het laden zet hij in de importtabel van het spel vier SETUPAPI-functies (SetupDiGetClassDevsW, SetupDiEnumDeviceInterfaces,
//     SetupDiGetDeviceInterfaceDetailW, SetupDiDestroyDeviceInfoList) en KERNEL32!CreateFileW om. Alleen de GUID van het USIO-bord wordt
//     nagebootst, al het andere (ook de USB-hostcontroller-GUID van de apparaatinventaris, CM_*) gaat ongemoeid naar de echte functies.
//   * De registers staan in usio_board.h (getoetst in tests/test_usio_board.cpp). Het schakelaarwoord en de aim komen uit het gedeelde
//     geheugen TeknoParrot_JvsState (indeling TimeCrisis5), net als bij de gesloten kern.
//   * Het richten en de trekker: ZE LOPEN NIET OVER HET BORD EN NIET VIA DE MUIS. Het spel praat met een geweer op een seriele poort (COM1,
//     COM2, COM15 .. COM3; 38400 baud 8O1; eenbyte-opdracht 0x08 -> kader van 11 bytes met X1, Y1, vlaggen en controlesom; zie gun_com.h met de
//     adressen). Opent geen enkele poort, dan valt het spel terug op de muis met relatieve delta's en zonder trekker (tv-proef 2026-10-18). Deze
//     DLL doet daarom een nagebootste COM1: CreateFileW("COM1") geeft een gebeurtenis-handvat, en ReadFile, WriteFile, CloseHandle, PurgeComm,
//     ClearCommError, Get/SetCommState en Get/SetCommTimeouts van het spel gedragen zich op dat handvat als het geweer (aim en trekker uit het
//     gedeelde geheugen). RG_TC5_GUN=off zet dat uit. SendInput (absolute muis + linkerknop) is alleen nog een noodtrap: RG_TC5_AIM=on / RG_TC5_TRIGGER=on.
//
// LOGBOEK
//   rg-winusb.log naast deze DLL: het laden, welke importen omgezet zijn, elke stap van de apparaatopsomming en het openen, de eerste
//   aanroep van elke WinUsb-functie, elke verandering van het schakelaarwoord (zo zie je welke fysieke knop bij welke bit hoort) en
//   het aantal gelezen/geschreven registerblokken. Zonder dat logboek is de DLL niet geladen.
//
// OMGEVING
//   RG_USIO_SHM       naam van het gedeelde geheugen (standaard TeknoParrot_JvsState)
//   RG_TC5_BITS       toewijzing overschrijven: "pedal_l=0x800,pedal_r=0x400,gun=0x800000,trigger=0,coin=1" (zie usio_board.h)
//   RG_TC5_GUN        "off" = geen nagebootste COM1; of de afbeelding: "x0=0,x1=1023,y0=0,y1=1023,flipy=1,oof=1,trig=0x20" (zie gun_com.h)
//   RG_TC5_AIM=on     muisbeweging via SendInput (standaard uit)
//   RG_TC5_TRIGGER=on linkermuisknop via SendInput (standaard uit)
//
// Clean-room: alleen de openbare WinUSB-/SetupAPI-interfaces en wat we zelf aan het spel hebben gemeten (docs/research/namco/README.md).
#define WIN32_LEAN_AND_MEAN
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "gun_com.h"
#include "usio_board.h"

namespace {

// --- logboek ---------------------------------------------------------------------------------------------------------------------
CRITICAL_SECTION g_cs;   // beschermt g_board en het logboek
bool g_cs_ready = false;
wchar_t g_log_path[MAX_PATH];

void rg_log(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::snprintf(line, sizeof line, "[%10lu] ", static_cast<unsigned long>(GetTickCount()));
    if (n < 0) n = 0;
    int m = std::vsnprintf(line + n, sizeof line - static_cast<size_t>(n) - 3, fmt, ap);
    va_end(ap);
    size_t len = static_cast<size_t>(n) + (m < 0 ? 0 : static_cast<size_t>(m));
    if (len > sizeof line - 3) len = sizeof line - 3;
    line[len++] = '\r';
    line[len++] = '\n';
    HANDLE h = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, line, static_cast<DWORD>(len), &w, nullptr);
        CloseHandle(h);
    }
}

bool g_seen[32];
void first_call(int id, const char* name) {
    if (id < 0 || id >= 32 || g_seen[id]) return;
    g_seen[id] = true;
    rg_log("eerste aanroep: %s", name);
}
#define RG_FIRST(id) first_call(id, __func__)

// --- toestand ---------------------------------------------------------------------------------------------------------------------
// De GUID van de apparaatinterface van het USIO-bord (gelezen uit de .rdata van het spel op 0x1414a5ba8).
const GUID kUsioGuid = {0xE54BF304, 0x66A3, 0x476F, {0xA4, 0x56, 0xC6, 0xDD, 0xAB, 0x8C, 0x70, 0x78}};
// Het "apparaatpad" dat SetupDiGetDeviceInterfaceDetailW teruggeeft; CreateFileW (onze versie) herkent het. VID 0B9A is Namco; PID en serie
// zijn verzonnen (het spel kijkt er niet naar: het gebruikt alleen de interface-GUID).
const wchar_t kFakePath[] = L"\\\\?\\usb#vid_0b9a&pid_0c20#rgusio000001#{e54bf304-66a3-476f-a456-c6ddab8c7078}";

constexpr UCHAR kPipeOut = 0x01, kPipeIn = 0x82, kPipeIntr = 0x83;

rg_usio5::Board g_board;
rg_usio5::GunCom g_gun;
bool g_gun_on = true;
HANDLE g_com = nullptr;   // ons nagebootste COM-handvat (een gebeurtenis); nullptr = niet open
uint32_t g_gun_log_raw = 0xFFFFFFFFu;
HANDLE g_map = nullptr;
const uint8_t* g_view = nullptr;
HANDLE g_thread = nullptr;
volatile LONG g_stop = 0;
char g_shm_name[128] = "TeknoParrot_JvsState";
bool g_aim_on = false, g_trigger_on = false;   // SendInput: noodtrap, standaard uit
uint32_t g_last_sw = 0xFFFFFFFFu;
uint32_t g_last_ctl = 0xFFFFFFFFu;
LONG g_iface_dummy = 0;   // het adres hiervan is onze WINUSB_INTERFACE_HANDLE
LONG g_hdev_dummy = 0;    // en dit onze HDEVINFO
LONG g_enum_done = 0;
LONG g_reads = 0, g_writes = 0;

// Haalt het nieuwste beeld uit het gedeelde geheugen en geeft het door aan het bordmodel. Roep aan met g_cs vast.
void sample_locked() {
    if (!g_view) {
        if (!g_map) g_map = OpenFileMappingA(FILE_MAP_READ, FALSE, g_shm_name);
        if (g_map) {
            g_view = static_cast<const uint8_t*>(MapViewOfFile(g_map, FILE_MAP_READ, 0, 0, rg_usio5::kShmSize));
            if (g_view) rg_log("gedeeld geheugen %s geopend", g_shm_name);
        }
        if (!g_view) return;
    }
    uint8_t copy[rg_usio5::kShmSize];
    std::memcpy(copy, g_view, sizeof copy);
    g_board.update(copy);
    g_gun.update(copy);
    const uint32_t sw = g_board.switches(), ctl = g_board.control();
    if (sw != g_last_sw || ctl != g_last_ctl) {
        rg_log("invoer: schakelaarwoord 0x%08lx (daemon-bits 0x%08lx), aim %u,%u", static_cast<unsigned long>(sw), static_cast<unsigned long>(ctl),
               g_board.aim_x(), g_board.aim_y());
        g_last_sw = sw;
        g_last_ctl = ctl;
    }
    const uint32_t raw = static_cast<uint32_t>(g_gun.raw_x()) | (static_cast<uint32_t>(g_gun.raw_y()) << 10) | (static_cast<uint32_t>(g_gun.flags()) << 20);
    if (g_com && raw != g_gun_log_raw) {
        // Alleen loggen als een vlag wisselt of het geweer ver verschuift (anders loopt het logboek vol).
        const uint32_t old = g_gun_log_raw;
        const int dx = static_cast<int>(g_gun.raw_x()) - static_cast<int>(old & 0x3FFu);
        const int dy = static_cast<int>(g_gun.raw_y()) - static_cast<int>((old >> 10) & 0x3FFu);
        if (old == 0xFFFFFFFFu || (old >> 20) != (raw >> 20) || dx > 48 || dx < -48 || dy > 48 || dy < -48) {
            rg_log("geweer: X1=%u Y1=%u vlaggen 0x%02x (trekker %s, buiten beeld %s)", g_gun.raw_x(), g_gun.raw_y(), g_gun.flags(),
                   g_gun.trigger() ? "ja" : "nee", g_gun.out_of_frame() ? "ja" : "nee");
            g_gun_log_raw = raw;
        }
    }
}

// --- de importtabel van het spel -----------------------------------------------------------------------------------------------
// Zet in de importtabel van `mod` de invoer `dll!func` om naar `repl`; geeft het oude adres (of nullptr als hij er niet is).
void* patch_iat(HMODULE mod, const char* dll, const char* func, void* repl) {
    auto* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (lstrcmpiA(reinterpret_cast<const char*>(base + imp->Name), dll) != 0) continue;
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        auto* names = imp->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk) : nullptr;
        if (!names) continue;
        for (size_t i = 0; iat[i].u1.Function; ++i) {
            if (names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(ibn->Name), func) != 0) continue;
            void* old = reinterpret_cast<void*>(iat[i].u1.Function);
            DWORD prot = 0;
            if (!VirtualProtect(&iat[i].u1.Function, sizeof(void*), PAGE_READWRITE, &prot)) return nullptr;
            iat[i].u1.Function = reinterpret_cast<decltype(iat[i].u1.Function)>(repl);
            VirtualProtect(&iat[i].u1.Function, sizeof(void*), prot, &prot);
            return old;
        }
    }
    return nullptr;
}

// --- SetupAPI-nabootsing (alleen de GUID van het USIO-bord) -------------------------------------------------------------------
struct SpDevIfData {   // SP_DEVICE_INTERFACE_DATA (x64: 32 bytes)
    DWORD cbSize;
    GUID InterfaceClassGuid;
    DWORD Flags;
    ULONG_PTR Reserved;
};
struct SpDevIfDetailW {   // SP_DEVICE_INTERFACE_DETAIL_DATA_W
    DWORD cbSize;
    WCHAR DevicePath[1];
};

using GetClassDevsWFn = HANDLE(WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);
using EnumDevIfFn = BOOL(WINAPI*)(HANDLE, void*, const GUID*, DWORD, SpDevIfData*);
using GetDevIfDetailWFn = BOOL(WINAPI*)(HANDLE, SpDevIfData*, SpDevIfDetailW*, DWORD, PDWORD, void*);
using DestroyFn = BOOL(WINAPI*)(HANDLE);
using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

GetClassDevsWFn g_real_getclass = nullptr;
EnumDevIfFn g_real_enum = nullptr;
GetDevIfDetailWFn g_real_detail = nullptr;
DestroyFn g_real_destroy = nullptr;
CreateFileWFn g_real_createfile = nullptr;

HANDLE WINAPI rg_SetupDiGetClassDevsW(const GUID* g, PCWSTR enumerator, HWND hwnd, DWORD flags) {
    if (g && std::memcmp(g, &kUsioGuid, sizeof(GUID)) == 0) {
        rg_log("SetupDiGetClassDevsW(USIO-GUID, vlaggen 0x%lx) -> nagebootste apparatenlijst", static_cast<unsigned long>(flags));
        InterlockedExchange(&g_enum_done, 0);
        return &g_hdev_dummy;
    }
    return g_real_getclass ? g_real_getclass(g, enumerator, hwnd, flags) : INVALID_HANDLE_VALUE;
}

BOOL WINAPI rg_SetupDiEnumDeviceInterfaces(HANDLE set, void* info, const GUID* g, DWORD index, SpDevIfData* data) {
    if (set == &g_hdev_dummy) {
        if (index == 0 && data && data->cbSize >= sizeof(SpDevIfData)) {
            data->InterfaceClassGuid = kUsioGuid;
            data->Flags = 1;   // SPINT_ACTIVE
            data->Reserved = 0;
            rg_log("SetupDiEnumDeviceInterfaces(0) -> het USIO-bord");
            return TRUE;
        }
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (!g_real_enum) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return g_real_enum(set, info, g, index, data);
}

BOOL WINAPI rg_SetupDiGetDeviceInterfaceDetailW(HANDLE set, SpDevIfData* data, SpDevIfDetailW* detail, DWORD size, PDWORD required, void* devinfo) {
    if (set == &g_hdev_dummy) {
        const DWORD need = static_cast<DWORD>(offsetof(SpDevIfDetailW, DevicePath) + sizeof kFakePath);
        if (required) *required = need;
        if (!detail || size < need) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        std::memcpy(detail->DevicePath, kFakePath, sizeof kFakePath);
        rg_log("SetupDiGetDeviceInterfaceDetailW -> pad %ls", kFakePath);
        return TRUE;
    }
    if (!g_real_detail) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return g_real_detail(set, data, detail, size, required, devinfo);
}

BOOL WINAPI rg_SetupDiDestroyDeviceInfoList(HANDLE set) {
    if (set == &g_hdev_dummy) return TRUE;
    return g_real_destroy ? g_real_destroy(set) : FALSE;
}

// CreateFileW: ons nagebootste pad geeft een gewoon, sluitbaar handvat (een gebeurtenis); al het andere gaat naar het echte CreateFileW.
bool is_com1(LPCWSTR name) {
    if (!name) return false;
    if (_wcsnicmp(name, L"\\\\.\\", 4) == 0) name += 4;
    return _wcsicmp(name, L"COM1") == 0;
}

HANDLE WINAPI rg_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (g_gun_on && is_com1(name)) {
        // Het spel probeert COM1, COM2, COM15 .. COM3 en houdt de eerste die antwoordt (0x140a54c30). Wij zijn COM1.
        EnterCriticalSection(&g_cs);
        if (g_com) {
            LeaveCriticalSection(&g_cs);
            rg_log("CreateFileW(COM1): al open, geweigerd (zoals een poort die in gebruik is)");
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
        HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_com = h;
        g_gun_log_raw = 0xFFFFFFFFu;
        g_gun.purge();
        LeaveCriticalSection(&g_cs);
        rg_log("CreateFileW(COM1) -> nagebootst geweer, handvat %p (toegang 0x%lx)", h, static_cast<unsigned long>(access));
        return h ? h : INVALID_HANDLE_VALUE;
    }
    if (name && _wcsicmp(name, kFakePath) == 0) {
        HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        rg_log("CreateFileW(USIO-pad) -> handvat %p (toegang 0x%lx, vlaggen 0x%lx)", h, static_cast<unsigned long>(access), static_cast<unsigned long>(flags));
        return h ? h : INVALID_HANDLE_VALUE;
    }
    return g_real_createfile(name, access, share, sa, disp, flags, tmpl);
}

// --- het nagebootste geweer: de seriele API's van het spel op ons COM1-handvat --------------------------------------------------
using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using WriteFileFn = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);
using PurgeCommFn = BOOL(WINAPI*)(HANDLE, DWORD);
using ClearCommErrorFn = BOOL(WINAPI*)(HANDLE, LPDWORD, LPCOMSTAT);
using GetCommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using SetCommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using GetCommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);
using SetCommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);

ReadFileFn g_real_readfile = nullptr;
WriteFileFn g_real_writefile = nullptr;
CloseHandleFn g_real_closehandle = nullptr;
PurgeCommFn g_real_purge = nullptr;
ClearCommErrorFn g_real_clearerr = nullptr;
GetCommStateFn g_real_getstate = nullptr;
SetCommStateFn g_real_setstate = nullptr;
GetCommTimeoutsFn g_real_gettimeouts = nullptr;
SetCommTimeoutsFn g_real_settimeouts = nullptr;

bool is_gun(HANDLE h) { return g_com != nullptr && h == g_com; }

// De aantallen per soort aanroep (voor het logboek: "eerste aanroep" en een eindtelling bij het sluiten).
LONG g_com_n_write = 0, g_com_n_read = 0;

BOOL WINAPI rg_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED ov) {
    if (!is_gun(h)) return g_real_writefile(h, buf, n, written, ov);
    EnterCriticalSection(&g_cs);
    sample_locked();   // het kader moet het nieuwste beeld van de daemon dragen
    const size_t got = g_gun.write(static_cast<const uint8_t*>(buf), n);
    const uint8_t code = n ? static_cast<const uint8_t*>(buf)[0] : 0;
    LeaveCriticalSection(&g_cs);
    const LONG k = InterlockedIncrement(&g_com_n_write);
    if (k <= 3 || got == 0) rg_log("geweer: WriteFile %lu byte(s), code 0x%02x -> antwoord van %lu bytes%s", static_cast<unsigned long>(n), code,
                                   static_cast<unsigned long>(got), got ? "" : " (ONBEKENDE CODE: het spel telt dat als mislukte transactie)");
    if (written) *written = n;
    if (ov) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }   // het spel gebruikt hier geen OVERLAPPED
    return TRUE;
}

BOOL WINAPI rg_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD read, LPOVERLAPPED ov) {
    if (!is_gun(h)) return g_real_readfile(h, buf, n, read, ov);
    EnterCriticalSection(&g_cs);
    const size_t got = g_gun.read(static_cast<uint8_t*>(buf), n);
    LeaveCriticalSection(&g_cs);
    const LONG k = InterlockedIncrement(&g_com_n_read);
    if (k <= 3 || got == 0) rg_log("geweer: ReadFile %lu byte(s) gevraagd -> %lu geleverd", static_cast<unsigned long>(n), static_cast<unsigned long>(got));
    if (read) *read = static_cast<DWORD>(got);
    if (ov) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    return TRUE;
}

BOOL WINAPI rg_CloseHandle(HANDLE h) {
    if (is_gun(h)) {
        EnterCriticalSection(&g_cs);
        g_com = nullptr;
        g_gun.purge();
        LeaveCriticalSection(&g_cs);
        rg_log("CloseHandle(geweer-COM1): %ld WriteFile, %ld ReadFile, %lu kaders gepold", static_cast<long>(g_com_n_write), static_cast<long>(g_com_n_read),
               static_cast<unsigned long>(g_gun.polls()));
    }
    return g_real_closehandle(h);
}

BOOL WINAPI rg_PurgeComm(HANDLE h, DWORD flags) {
    if (!is_gun(h)) return g_real_purge(h, flags);
    EnterCriticalSection(&g_cs);
    g_gun.purge();
    LeaveCriticalSection(&g_cs);
    return TRUE;
}

BOOL WINAPI rg_ClearCommError(HANDLE h, LPDWORD errors, LPCOMSTAT stat) {
    if (!is_gun(h)) return g_real_clearerr(h, errors, stat);
    if (errors) *errors = 0;
    if (stat) {
        std::memset(stat, 0, sizeof *stat);
        EnterCriticalSection(&g_cs);
        stat->cbInQue = static_cast<DWORD>(g_gun.pending());
        LeaveCriticalSection(&g_cs);
    }
    return TRUE;
}

BOOL WINAPI rg_GetCommState(HANDLE h, LPDCB dcb) {
    if (!is_gun(h)) return g_real_getstate(h, dcb);
    if (dcb) {
        std::memset(dcb, 0, sizeof *dcb);
        dcb->DCBlength = sizeof *dcb;
        dcb->BaudRate = 38400;
        dcb->ByteSize = 8;
    }
    return TRUE;
}

BOOL WINAPI rg_SetCommState(HANDLE h, LPDCB dcb) {
    if (!is_gun(h)) return g_real_setstate(h, dcb);
    rg_log("geweer: SetCommState %lu baud, %u databits, pariteit %u, stopbits %u", dcb ? static_cast<unsigned long>(dcb->BaudRate) : 0ul,
           dcb ? static_cast<unsigned>(dcb->ByteSize) : 0u, dcb ? static_cast<unsigned>(dcb->Parity) : 0u, dcb ? static_cast<unsigned>(dcb->StopBits) : 0u);
    return TRUE;
}

BOOL WINAPI rg_GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) {
    if (!is_gun(h)) return g_real_gettimeouts(h, t);
    if (t) std::memset(t, 0, sizeof *t);
    return TRUE;
}

BOOL WINAPI rg_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) {
    if (!is_gun(h)) return g_real_settimeouts(h, t);
    (void)t;
    return TRUE;
}

// --- de invoerdraad: gedeeld geheugen -> bordmodel, aim en trekker -> SendInput -----------------------------------------------------
void send_mouse(DWORD flags, LONG dx = 0, LONG dy = 0) {
    INPUT in;
    std::memset(&in, 0, sizeof in);
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof in);
}

DWORD WINAPI input_thread(LPVOID) {
    int last_x = -1, last_y = -1;
    bool last_trigger = false;
    DWORD last_open = 0;
    while (!g_stop) {
        if (!g_view) {
            const DWORD now = GetTickCount();
            if (now - last_open < 250) { Sleep(10); continue; }
            last_open = now;
        }
        EnterCriticalSection(&g_cs);
        sample_locked();
        const int x = g_board.aim_x(), y = g_board.aim_y();
        const bool trig = g_board.trigger();
        const bool have = g_view != nullptr;
        LeaveCriticalSection(&g_cs);
        if (have) {
            if (g_aim_on && (x != last_x || y != last_y)) {
                // 0..255 -> 0..65535 over het hele scherm (MOUSEEVENTF_ABSOLUTE). De daemon houdt de assen binnen het scherm op 2..253.
                send_mouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, x * 65535 / 255, y * 65535 / 255);
                last_x = x;
                last_y = y;
            }
            if (g_trigger_on && trig != last_trigger) {
                send_mouse(trig ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
                last_trigger = trig;
            }
        }
        Sleep(4);
    }
    return 0;
}

void ensure_thread() {
    if (g_thread) return;
    g_thread = CreateThread(nullptr, 0, input_thread, nullptr, 0, nullptr);
    rg_log("invoerdraad %s (aim via SendInput %s, trekker via SendInput %s)", g_thread ? "gestart" : "NIET gestart", g_aim_on ? "aan" : "uit",
           g_trigger_on ? "aan" : "uit");
}

// Voltooit een overdracht zoals WinUSB met een OVERLAPPED doet: FALSE + ERROR_IO_PENDING, de gebeurtenis gezet en de lengte in het
// OVERLAPPED. Het spel wacht daarna op de gebeurtenis (INFINITE) en roept WinUsb_GetOverlappedResult aan, dus de gebeurtenis MOET gezet zijn.
BOOL complete(LPOVERLAPPED ov, PULONG transferred, ULONG n) {
    if (transferred) *transferred = n;
    if (ov) {
        ov->Internal = 0;   // STATUS_SUCCESS
        ov->InternalHigh = n;
        if (ov->hEvent) SetEvent(reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(ov->hEvent) & ~static_cast<ULONG_PTR>(1)));
        SetLastError(ERROR_IO_PENDING);
        return FALSE;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

struct PipeInfo {   // WINUSB_PIPE_INFORMATION (PipeType staat op 0, PipeId op 4, MaximumPacketSize op 6, Interval op 8)
    int PipeType;   // 0 control, 1 isochroon, 2 bulk, 3 interrupt
    UCHAR PipeId;
    USHORT MaximumPacketSize;
    UCHAR Interval;
};

}  // namespace

#define RG_API extern "C" __declspec(dllexport)

// --- de WinUsb-functies die het spel importeert (+ een paar onschuldige extra's) ----------------------------------------------------------
RG_API BOOL WINAPI WinUsb_Initialize(HANDLE device, void** iface) {
    RG_FIRST(0);
    if (!iface) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *iface = &g_iface_dummy;
    rg_log("WinUsb_Initialize(handvat %p) -> interface %p", device, *iface);
    ensure_thread();
    return TRUE;
}

RG_API BOOL WINAPI WinUsb_Free(void* iface) {
    RG_FIRST(1);
    rg_log("WinUsb_Free(%p); %ld registerbereiken gelezen, %ld geschreven", iface, static_cast<long>(g_reads), static_cast<long>(g_writes));
    return TRUE;
}

RG_API BOOL WINAPI WinUsb_QueryDeviceInformation(void* iface, ULONG type, PULONG len, PVOID buf) {
    RG_FIRST(2);
    if (iface != &g_iface_dummy || !len || !buf) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (type == 1 /* DEVICE_SPEED */) {
        if (*len < 1) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        *static_cast<UCHAR*>(buf) = 3;   // HighSpeed; het spel eist alleen niet-nul
        *len = 1;
        return TRUE;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

RG_API BOOL WINAPI WinUsb_QueryInterfaceSettings(void* iface, UCHAR alt, void* desc) {
    RG_FIRST(3);
    if (iface != &g_iface_dummy || alt != 0 || !desc) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
    // USB_INTERFACE_DESCRIPTOR: lengte 9, type 4 (interface), nummer 0, alternatief 0, drie eindpunten, klasse 0xFF (leverancier).
    const UCHAR d[9] = {9, 4, 0, 0, 3, 0xFF, 0, 0, 0};
    std::memcpy(desc, d, sizeof d);
    return TRUE;
}

RG_API BOOL WINAPI WinUsb_QueryPipe(void* iface, UCHAR alt, UCHAR index, void* out) {
    RG_FIRST(4);
    if (iface != &g_iface_dummy || alt != 0 || !out) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    PipeInfo p;
    std::memset(&p, 0, sizeof p);
    switch (index) {
        case 0: p = {2, kPipeOut, 512, 0}; break;     // bulk-OUT
        case 1: p = {2, kPipeIn, 512, 0}; break;      // bulk-IN
        case 2: p = {3, kPipeIntr, 64, 1}; break;     // interrupt-IN (het spel gebruikt hem niet)
        default: SetLastError(ERROR_NO_MORE_ITEMS); return FALSE;
    }
    std::memcpy(out, &p, sizeof p);
    return TRUE;
}

RG_API BOOL WINAPI WinUsb_WritePipe(void* iface, UCHAR pipe, PUCHAR buf, ULONG len, PULONG transferred, LPOVERLAPPED ov) {
    RG_FIRST(5);
    if (iface != &g_iface_dummy || pipe != kPipeOut || (!buf && len)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    EnterCriticalSection(&g_cs);
    const bool ok = g_board.out_transfer(buf, len);
    LeaveCriticalSection(&g_cs);
    InterlockedIncrement(&g_writes);
    if (!ok) rg_log("WinUsb_WritePipe: onbekende overdracht van %lu bytes (eerste byte 0x%02x)", static_cast<unsigned long>(len), len && buf ? buf[0] : 0u);
    return complete(ov, transferred, len);
}

RG_API BOOL WINAPI WinUsb_ReadPipe(void* iface, UCHAR pipe, PUCHAR buf, ULONG len, PULONG transferred, LPOVERLAPPED ov) {
    RG_FIRST(6);
    if (iface != &g_iface_dummy || pipe != kPipeIn || (!buf && len)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    EnterCriticalSection(&g_cs);
    sample_locked();   // het schakelaarwoord moet vers zijn op het moment dat het spel het leest
    const size_t n = g_board.in_transfer(buf, len);
    LeaveCriticalSection(&g_cs);
    InterlockedIncrement(&g_reads);
    // Wat we niet leverden blijft nul: het spel leest `len` bytes, ook als het bord er minder zou geven.
    if (n < len && buf) std::memset(buf + n, 0, len - n);
    return complete(ov, transferred, len);
}

RG_API BOOL WINAPI WinUsb_GetOverlappedResult(void* iface, LPOVERLAPPED ov, PULONG transferred, BOOL wait) {
    RG_FIRST(7);
    (void)iface;
    (void)wait;   // de overdrachten zijn al klaar
    if (!ov) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (transferred) *transferred = static_cast<ULONG>(ov->InternalHigh);
    return TRUE;
}

// Niet door het spel gebruikt; veilige antwoorden voor het geval een andere module ze aanroept.
RG_API BOOL WINAPI WinUsb_SetPipePolicy(void*, UCHAR, ULONG, ULONG, PVOID) { RG_FIRST(8); return TRUE; }
RG_API BOOL WINAPI WinUsb_ResetPipe(void*, UCHAR) { RG_FIRST(9); return TRUE; }
RG_API BOOL WINAPI WinUsb_AbortPipe(void*, UCHAR) { RG_FIRST(10); return TRUE; }
RG_API BOOL WINAPI WinUsb_FlushPipe(void*, UCHAR) { RG_FIRST(11); return TRUE; }

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        g_cs_ready = true;
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(inst, buf, MAX_PATH);
        wchar_t* slash = wcsrchr(buf, L'\\');
        if (slash) *slash = 0;
        _snwprintf(g_log_path, MAX_PATH, L"%ls\\rg-winusb.log", buf);
        char env[256];
        if (GetEnvironmentVariableA("RG_USIO_SHM", env, sizeof env) > 0 && env[0]) std::snprintf(g_shm_name, sizeof g_shm_name, "%s", env);
        if (GetEnvironmentVariableA("RG_TC5_AIM", env, sizeof env) > 0 && _stricmp(env, "on") == 0) g_aim_on = true;
        if (GetEnvironmentVariableA("RG_TC5_TRIGGER", env, sizeof env) > 0 && _stricmp(env, "on") == 0) g_trigger_on = true;
        rg_usio5::GunMap gm;
        int gun_unknown = 0, gun_applied = 0;
        if (GetEnvironmentVariableA("RG_TC5_GUN", env, sizeof env) > 0 && env[0]) {
            if (_stricmp(env, "off") == 0) g_gun_on = false;
            else gun_applied = rg_usio5::parse_gunmap(env, gm, &gun_unknown);
        }
        g_gun.set_map(gm);
        rg_log("geweer (nagebootste COM1): %s; X1 %d..%d, Y1 %d..%d, Y omgekeerd %s, buiten-beeld-vlag %s, trekkervlag 0x%02x; RG_TC5_GUN: %d toegepast, %d onbekend",
               g_gun_on ? "aan" : "UIT", gm.x0, gm.x1, gm.y0, gm.y1, gm.flip_y ? "ja" : "nee", gm.oof ? "aan" : "uit", gm.trig_flag, gun_applied, gun_unknown);
        rg_usio5::Mapping m;
        int unknown = 0, applied = 0;
        if (GetEnvironmentVariableA("RG_TC5_BITS", env, sizeof env) > 0 && env[0]) applied = rg_usio5::parse_mapping(env, m, &unknown);
        g_board.set_mapping(m);
        rg_log("rg-winusb (winusb.dll) geladen in pid %lu; geheugen %s; RG_TC5_BITS: %d toegepast, %d onbekend", static_cast<unsigned long>(GetCurrentProcessId()),
               g_shm_name, applied, unknown);
        rg_log("toewijzing: test 0x%lx service 0x%lx enter 0x%lx pedaal-L 0x%lx pedaal-R 0x%lx gun 0x%lx trekker 0x%lx munt %s",
               static_cast<unsigned long>(m.test), static_cast<unsigned long>(m.service), static_cast<unsigned long>(m.enter),
               static_cast<unsigned long>(m.pedal_l), static_cast<unsigned long>(m.pedal_r), static_cast<unsigned long>(m.gun),
               static_cast<unsigned long>(m.trigger), m.coin ? "via bord" : "niet via bord");

        // De importtabel van het hoofdprogramma (de exe van het spel). Staat een functie er niet in, dan is dat een regel in het logboek.
        HMODULE exe = GetModuleHandleW(nullptr);
        struct Hook { const char* dll; const char* fn; void* repl; void** orig; };
        const Hook hooks[] = {
            {"SETUPAPI.dll", "SetupDiGetClassDevsW", reinterpret_cast<void*>(&rg_SetupDiGetClassDevsW), reinterpret_cast<void**>(&g_real_getclass)},
            {"SETUPAPI.dll", "SetupDiEnumDeviceInterfaces", reinterpret_cast<void*>(&rg_SetupDiEnumDeviceInterfaces), reinterpret_cast<void**>(&g_real_enum)},
            {"SETUPAPI.dll", "SetupDiGetDeviceInterfaceDetailW", reinterpret_cast<void*>(&rg_SetupDiGetDeviceInterfaceDetailW), reinterpret_cast<void**>(&g_real_detail)},
            {"SETUPAPI.dll", "SetupDiDestroyDeviceInfoList", reinterpret_cast<void*>(&rg_SetupDiDestroyDeviceInfoList), reinterpret_cast<void**>(&g_real_destroy)},
            {"KERNEL32.dll", "CreateFileW", reinterpret_cast<void*>(&rg_CreateFileW), reinterpret_cast<void**>(&g_real_createfile)},
            {"KERNEL32.dll", "ReadFile", reinterpret_cast<void*>(&rg_ReadFile), reinterpret_cast<void**>(&g_real_readfile)},
            {"KERNEL32.dll", "WriteFile", reinterpret_cast<void*>(&rg_WriteFile), reinterpret_cast<void**>(&g_real_writefile)},
            {"KERNEL32.dll", "CloseHandle", reinterpret_cast<void*>(&rg_CloseHandle), reinterpret_cast<void**>(&g_real_closehandle)},
            {"KERNEL32.dll", "PurgeComm", reinterpret_cast<void*>(&rg_PurgeComm), reinterpret_cast<void**>(&g_real_purge)},
            {"KERNEL32.dll", "ClearCommError", reinterpret_cast<void*>(&rg_ClearCommError), reinterpret_cast<void**>(&g_real_clearerr)},
            {"KERNEL32.dll", "GetCommState", reinterpret_cast<void*>(&rg_GetCommState), reinterpret_cast<void**>(&g_real_getstate)},
            {"KERNEL32.dll", "SetCommState", reinterpret_cast<void*>(&rg_SetCommState), reinterpret_cast<void**>(&g_real_setstate)},
            {"KERNEL32.dll", "GetCommTimeouts", reinterpret_cast<void*>(&rg_GetCommTimeouts), reinterpret_cast<void**>(&g_real_gettimeouts)},
            {"KERNEL32.dll", "SetCommTimeouts", reinterpret_cast<void*>(&rg_SetCommTimeouts), reinterpret_cast<void**>(&g_real_settimeouts)},
        };
        for (const Hook& h : hooks) {
            void* old = patch_iat(exe, h.dll, h.fn, h.repl);
            *h.orig = old;
            rg_log("importtabel: %s!%s %s", h.dll, h.fn, old ? "omgezet" : "NIET in het hoofdprogramma (dan zoekt dit proces het bord niet via SetupAPI)");
        }
        // Als CreateFileW ontbreekt moet er toch een echte functie zijn om naar door te verwijzen.
        // (Het adres van CreateFileW in deze DLL is dat van kernel32: onze eigen importtabel is niet omgezet.)
        if (!g_real_createfile) g_real_createfile = &CreateFileW;
        if (!g_real_readfile) g_real_readfile = &ReadFile;
        if (!g_real_writefile) g_real_writefile = &WriteFile;
        if (!g_real_closehandle) g_real_closehandle = &CloseHandle;
        if (!g_real_purge) g_real_purge = &PurgeComm;
        if (!g_real_clearerr) g_real_clearerr = &ClearCommError;
        if (!g_real_getstate) g_real_getstate = &GetCommState;
        if (!g_real_setstate) g_real_setstate = &SetCommState;
        if (!g_real_gettimeouts) g_real_gettimeouts = &GetCommTimeouts;
        if (!g_real_settimeouts) g_real_settimeouts = &SetCommTimeouts;
    } else if (reason == DLL_PROCESS_DETACH && g_cs_ready) {
        InterlockedExchange(&g_stop, 1);
    }
    return TRUE;
}
