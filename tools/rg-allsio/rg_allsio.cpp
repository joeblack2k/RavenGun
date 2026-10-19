// SPDX-License-Identifier: GPL-3.0-or-later
// rg-allsio.dll (RavenGun, 64-bit): het Sega ALLS USB-IO-bord voor amdaemon.exe (House of the Dead: Scarlet Dawn) zonder de gesloten
// TeknoParrot-kern.
//
// WAAROM
//   amdaemon.exe eist volgens config.json een USB-IO-bord (usb_io node 0) en vindt het als HID-apparaat (vid_0ca3, fabrikant "SEGA", product
//   "I/O CONTROL BD;...") met 64-byte rapporten. Onder Wine is er geen bord. De gesloten kern bootst het na in het daemonproces en voedt het
//   uit de besturingspijp TeknoParrotPipe (het AllsUsbIo-rapport dat onze daemon al schrijft). Deze DLL doet dat zelf.
//
// HOE
//   De lader laadt deze DLL via TP_PRELOAD_DLL in elk proces. amdaemon krijgt HID; Hodzero krijgt optioneel de aparte COM1-gunsensor.
//   Bij het laden zet hij in de
//   importtabel van de daemon om: hid.dll (HidD_GetHidGuid, GetManufacturerString, GetProductString, GetInputReport, SetOutputReport),
//   setupapi.dll (SetupDiGetClassDevsW, EnumDeviceInterfaces, GetDeviceInterfaceDetailW, DestroyDeviceInfoList: alleen de HID-GUID, de echte
//   HID-apparaten van de machine blijven onzichtbaar voor de daemon) en kernel32 (CreateFileW voor ons pad, ReadFile / WriteFile /
//   GetOverlappedResult / CancelIo voor ons handvat). Het protocol staat in allsio_proto.h (getoetst in tests/test_allsio_proto.cpp).
//
// LOGBOEK
//   rg-allsio.log naast deze DLL: het laden, wat omgezet is, elke stap van de apparaatopsomming, elk verzoek van de daemon (de eerste bytes in
//   hex; zo is het protocol te meten), de pijp van het AllsUsbIo-rapport en tellers. Zonder dat logboek is de DLL niet in de daemon geladen.
//
// OMGEVING
//   RG_ALLSIO_PIPE       naam van de besturingspijp (standaard \\.\pipe\TeknoParrotPipe)
//   RG_ALLSIO_PERIOD_MS  periode van het ingangsrapport (standaard 2)
//   RG_ALLSIO_MAIN_ID   optionele 15-teken moederbordidentiteit, alleen in amdaemon
//   RG_ALLSIO_GUN_SENSOR=1  synchrone COM1 uit HOTDSDPipe, uitsluitend in Hodzero
//   RG_ALLSIO_GUN_CONFIG=1  zet alleen de lokale Unreal-gunmodus op GUN (0), met eenmalige backup
//   RG_ALLSIO_OFF=1      niets doen
//
// Clean-room: alleen de openbare HID-/SetupAPI-interfaces en wat we zelf aan amdaemon.exe en onze eigen besturingspijp hebben gemeten.
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
#include <atomic>
#include <cwchar>

#include "allsio_proto.h"

namespace {

CRITICAL_SECTION g_cs;   // beschermt de toestand hieronder; het logboek heeft een eigen, lock-vrij handvat
bool g_cs_ready = false;
wchar_t g_log_path[MAX_PATH];
// Eén logboekhandvat voor de hele levensduur van de DLL, lui geopend. Voorheen opende en sloot rg_log het bestand bij elke regel,
// vaak terwijl de aanroeper g_cs hield: elke DeviceIoControl/ReadFile die logde wachtte dan op twee extra systeemaanroepen onder
// de vergrendeling. FILE_APPEND_DATA zonder bestandspositie: elke WriteFile komt aan het eind, ook met een tweede proces (Hodzero
// en amdaemon laden dezelfde DLL naast hetzelfde logboek). Het handvat blijft open tot het proces eindigt.
std::atomic<HANDLE> g_log_handle{INVALID_HANDLE_VALUE};

HANDLE log_handle() {
    HANDLE h = g_log_handle.load(std::memory_order_acquire);
    if (h != INVALID_HANDLE_VALUE) return h;
    h = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return h;   // volgende regel probeert het opnieuw
    HANDLE expected = INVALID_HANDLE_VALUE;
    if (!g_log_handle.compare_exchange_strong(expected, h, std::memory_order_acq_rel)) {
        CloseHandle(h);   // een andere draad was ons voor
        return expected;
    }
    return h;
}

void rg_log(const char* fmt, ...) {
    char line[640];
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
    // Eén WriteFile met de hele regel: regels van verschillende draden lopen niet door elkaar.
    const HANDLE h = log_handle();
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, line, static_cast<DWORD>(len), &w, nullptr);
    }
}

// --- toestand van het nagebootste bord ---------------------------------------------------------------------------------------------
const GUID kHidGuid = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};

rg_allsio::Board g_board;
HANDLE g_dev = nullptr;                  // ons "apparaatvoorwerp" (een gebeurtenis; sluitbaar met CloseHandle)
LONG g_set_dummy = 0;                    // onze HDEVINFO
uint8_t g_pipe_report[rg_allsio::kReport];   // het laatste AllsUsbIo-rapport
bool g_have_pipe = false;
uint8_t g_frame[rg_allsio::kReport];     // het ingangsrapport dat op een lezing wacht
int g_queued = 0;                        // aantal rapporten dat op een lezing wacht (hooguit 8, allemaal het nieuwste beeld)
// Een uitstaande ReadFile: de daemon geeft de buffer en de OVERLAPPED op en vraagt later met GetOverlappedResult.
LPOVERLAPPED g_read_ov = nullptr;
uint8_t* g_read_buf = nullptr;
bool g_read_done = false;
DWORD g_period_ms = 2;
volatile LONG g_stop = 0;
HANDLE g_thread_pipe = nullptr, g_thread_tick = nullptr;
char g_pipe_name[128] = "\\\\.\\pipe\\TeknoParrotPipe";
LONG g_n_read_complete = 0, g_n_read_pending = 0, g_n_frames = 0;
LONG g_logged_req = 0;

void hexline(char* out, size_t cap, const uint8_t* p, size_t n) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 3 < cap; ++i) o += static_cast<size_t>(std::snprintf(out + o, cap - o, "%02x", p[i]));
    out[o] = 0;
}

// --- de importtabel van de daemon ----------------------------------------------------------------------------------------------------
void* patch_iat(HMODULE mod, const char* dll, const char* func, void* repl) {
    auto* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    // Een DLL kan in meer dan een beschrijving staan (setupapi.dll staat twee keer in amdaemon.exe): alle doorzoeken.
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

// --- Afzonderlijke synchrone COM1-sensor in Hodzero (geen daemon-HID-handvatten) ---
rg_allsio::GunSensor g_sensor;
// Ons tijdelijk COM1-handvat. De kernel32-hooks vergelijken het op elk bestand dat het spel
// opent; daarom atomair, zodat die vergelijking zonder de globale vergrendeling kan.
std::atomic<HANDLE> g_sensor_handle{nullptr};
HANDLE g_sensor_map = nullptr;
// Snel pad voor de kernel32-hooks: vrijwel elke ReadFile/WriteFile/CloseHandle in het spel gaat
// over een eigen bestand. Alleen het handvat vergelijken is dan genoeg; de globale vergrendeling
// (die het logboek en de toestand beschermt) komt er niet aan te pas. Klopt het wel, dan volgt
// binnen de vergrendeling nog een tweede controle, want het handvat kan net gesloten zijn.
inline bool is_sensor_handle(HANDLE h) {
    return h != nullptr && h == g_sensor_handle.load(std::memory_order_acquire);
}

const uint8_t* g_sensor_view = nullptr;
DCB g_sensor_dcb = {};
COMMTIMEOUTS g_sensor_timeouts = {};
DWORD g_sensor_map_retry = 0;
LONG g_sensor_writes = 0;
LONG g_sensor_fails = 0;
using CreateFileAFn = decltype(&CreateFileA);
CreateFileAFn g_sensor_create = &CreateFileA;
decltype(&ReadFile) g_sensor_read = &ReadFile;
decltype(&WriteFile) g_sensor_write = &WriteFile;
decltype(&CloseHandle) g_sensor_close = &CloseHandle;
decltype(&GetCommState) g_sensor_getstate = &GetCommState;
decltype(&SetCommState) g_sensor_setstate = &SetCommState;
decltype(&GetCommTimeouts) g_sensor_gettimeouts = &GetCommTimeouts;
decltype(&SetCommTimeouts) g_sensor_settimeouts = &SetCommTimeouts;

// Alleen aanroepen onder g_cs. Lees uitsluitend: RavenGun is eigenaar/schrijver.
const uint8_t* sensor_input() {
    if (g_sensor_view) return g_sensor_view;
    const DWORD now = GetTickCount();
    if (g_sensor_map_retry && now - g_sensor_map_retry < 1000) return nullptr;
    g_sensor_map_retry = now;
    // HOTDSDPipe is de byte-indeling; rg-jvsbridge publiceert deze onder de vaste mappingnaam.
    g_sensor_map = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
    if (!g_sensor_map) return nullptr;
    g_sensor_view = static_cast<const uint8_t*>(MapViewOfFile(g_sensor_map, FILE_MAP_READ, 0, 0, 64));
    if (!g_sensor_view) { CloseHandle(g_sensor_map); g_sensor_map = nullptr; }
    else rg_log("gunsensor: HOTDSDPipe geopend (alleen lezen)");
    return g_sensor_view;
}

HANDLE WINAPI sensor_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                 DWORD disp, DWORD flags, HANDLE tmpl) {
    if (!name || (_stricmp(name, "COM1") != 0 && _stricmp(name, "\\\\.\\COM1") != 0))
        return g_sensor_create(name, access, share, sa, disp, flags, tmpl);
    if (disp != OPEN_EXISTING || (flags & FILE_FLAG_OVERLAPPED)) {
        SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE;
    }
    EnterCriticalSection(&g_cs);
    if (g_sensor_handle.load(std::memory_order_relaxed)) {
        LeaveCriticalSection(&g_cs);
        SetLastError(ERROR_SHARING_VIOLATION); return INVALID_HANDLE_VALUE;
    }
    HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_sensor_handle.store(h, std::memory_order_release);
    g_sensor.close();
    g_sensor_dcb = {};
    g_sensor_dcb.DCBlength = sizeof(DCB);
    g_sensor_dcb.BaudRate = CBR_9600;
    g_sensor_dcb.fBinary = TRUE;
    g_sensor_dcb.ByteSize = 8;
    g_sensor_timeouts = {};
    LeaveCriticalSection(&g_cs);
    rg_log("gunsensor: COM1 geopend -> %p", h);
    return h ? h : INVALID_HANDLE_VALUE;
}

BOOL WINAPI sensor_CloseHandle(HANDLE h) {
    if (!is_sensor_handle(h)) return g_sensor_close(h);
    bool ours = false;
    EnterCriticalSection(&g_cs);
    if (g_sensor_handle && h == g_sensor_handle) {
        g_sensor_handle.store(nullptr, std::memory_order_release);
        g_sensor.close();
        if (g_sensor_view) UnmapViewOfFile(g_sensor_view);
        if (g_sensor_map) CloseHandle(g_sensor_map);
        g_sensor_view = nullptr; g_sensor_map = nullptr; g_sensor_map_retry = 0;
        ours = true;
    }
    LeaveCriticalSection(&g_cs);
    // Het echte CloseHandle buiten de vergrendeling: dat kan even duren (Wine) en andere
    // processen hoeven daar niet op te wachten.
    const BOOL ok = g_sensor_close(h);
    if (ours) rg_log("gunsensor: COM1 gesloten");
    return ok;
}

BOOL WINAPI sensor_ReadFile(HANDLE h, LPVOID buf, DWORD len, LPDWORD got, LPOVERLAPPED ov) {
    if (!is_sensor_handle(h)) return g_sensor_read(h, buf, len, got, ov);
    EnterCriticalSection(&g_cs);
    if (!g_sensor_handle || h != g_sensor_handle) {
        LeaveCriticalSection(&g_cs);
        return g_sensor_read(h, buf, len, got, ov);
    }
    if (got) *got = 0;
    if ((!buf && len) || !got || ov) {
        LeaveCriticalSection(&g_cs); SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    *got = static_cast<DWORD>(g_sensor.read(static_cast<uint8_t*>(buf), len));
    LeaveCriticalSection(&g_cs);
    return TRUE;
}

BOOL WINAPI sensor_WriteFile(HANDLE h, LPCVOID buf, DWORD len, LPDWORD written, LPOVERLAPPED ov) {
    if (!is_sensor_handle(h)) return g_sensor_write(h, buf, len, written, ov);
    EnterCriticalSection(&g_cs);
    if (!g_sensor_handle || h != g_sensor_handle) {
        LeaveCriticalSection(&g_cs);
        return g_sensor_write(h, buf, len, written, ov);
    }
    if (written) *written = 0;
    if ((!buf && len) || !written || ov) {
        LeaveCriticalSection(&g_cs); SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
    }
    const bool ok = !len || g_sensor.write(static_cast<const uint8_t*>(buf), len, sensor_input());
    if (ok) *written = len;
    // Het logboek mag niet vollopen: de eerste dertig schrijfacties volledig, daarna af en
    // toe een voorbeeld, en een storing ook maar tot dertig regels (daarna zeldzaam).
    const long n = ++g_sensor_writes;
    bool log_now = n <= 30 || (n % 1000) == 0;
    if (!ok) {
        const long f = ++g_sensor_fails;
        log_now = f <= 30 || (f % 100) == 0;
    }
    if (log_now) {
        char hex[80];
        hexline(hex, sizeof hex, static_cast<const uint8_t*>(buf), len < 16 ? len : 16);
        rg_log("gunsensor: schrijf %s -> %s, antwoord %lu bytes, invoer %s", hex, ok ? "ok" : "onbekend",
               static_cast<unsigned long>(g_sensor.pending()), g_sensor_view ? "ja" : "nee");
    }
    LeaveCriticalSection(&g_cs);
    if (!ok) SetLastError(ERROR_INVALID_DATA);
    return ok ? TRUE : FALSE;
}

BOOL WINAPI sensor_GetCommState(HANDLE h, LPDCB state) {
    if (!is_sensor_handle(h)) return g_sensor_getstate(h, state);
    EnterCriticalSection(&g_cs);
    if (!g_sensor_handle || h != g_sensor_handle) {
        LeaveCriticalSection(&g_cs); return g_sensor_getstate(h, state);
    }
    // GetCommState vult DCBlength zelf; Hodzero geeft een nulgevulde DCB door.
    const bool ok = state != nullptr;
    if (ok) *state = g_sensor_dcb;
    LeaveCriticalSection(&g_cs);
    if (!ok) SetLastError(ERROR_INVALID_PARAMETER);
    return ok ? TRUE : FALSE;
}
BOOL WINAPI sensor_SetCommState(HANDLE h, LPDCB state) {
    if (!is_sensor_handle(h)) return g_sensor_setstate(h, state);
    EnterCriticalSection(&g_cs);
    if (!g_sensor_handle || h != g_sensor_handle) {
        LeaveCriticalSection(&g_cs); return g_sensor_setstate(h, state);
    }
    const bool ok = state && state->DCBlength == sizeof(DCB) && state->ByteSize == 8 &&
                    (state->Parity == NOPARITY || state->Parity == ODDPARITY) && state->StopBits == ONESTOPBIT;
    if (state) rg_log("gunsensor: SetCommState lengte=%lu baud=%lu bits=%u parity=%u stop=%u -> %s",
                      static_cast<unsigned long>(state->DCBlength), static_cast<unsigned long>(state->BaudRate),
                      static_cast<unsigned>(state->ByteSize), static_cast<unsigned>(state->Parity),
                      static_cast<unsigned>(state->StopBits), ok ? "ok" : "ongeldig");
    if (ok) g_sensor_dcb = *state;
    LeaveCriticalSection(&g_cs);
    if (!ok) SetLastError(ERROR_INVALID_PARAMETER);
    return ok ? TRUE : FALSE;
}
BOOL WINAPI sensor_GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS timeouts) {
    if (!is_sensor_handle(h)) return g_sensor_gettimeouts(h, timeouts);
    EnterCriticalSection(&g_cs);
    if (!g_sensor_handle || h != g_sensor_handle) {
        LeaveCriticalSection(&g_cs); return g_sensor_gettimeouts(h, timeouts);
    }
    if (timeouts) *timeouts = g_sensor_timeouts;
    LeaveCriticalSection(&g_cs);
    if (!timeouts) SetLastError(ERROR_INVALID_PARAMETER);
    return timeouts ? TRUE : FALSE;
}
BOOL WINAPI sensor_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS timeouts) {
    if (!is_sensor_handle(h)) return g_sensor_settimeouts(h, timeouts);
    EnterCriticalSection(&g_cs);
    if (!g_sensor_handle || h != g_sensor_handle) {
        LeaveCriticalSection(&g_cs); return g_sensor_settimeouts(h, timeouts);
    }
    if (timeouts) g_sensor_timeouts = *timeouts;
    LeaveCriticalSection(&g_cs);
    if (!timeouts) SetLastError(ERROR_INVALID_PARAMETER);
    return timeouts ? TRUE : FALSE;
}

// Unreal leest deze gebruikersconfiguratie, niet Saved naast de exe op de NAS.
// Alleen op expliciet verzoek van het profiel; andere sleutels en kalibratie blijven intact.
bool configure_sensor_mode() {
    wchar_t path[MAX_PATH];
    const DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    const wchar_t suffix[] = L"\\Hodzero\\Saved\\Config\\WindowsNoEditor\\Game.ini";
    if (!len || len >= MAX_PATH || len + std::wcslen(suffix) + 1 > MAX_PATH) {
        rg_log("gunsensor: LOCALAPPDATA ontbreekt of is te lang; gunconfiguratie niet gewijzigd");
        return false;
    }
    std::wcscat(path, suffix);
    for (wchar_t* part = path + len + 1; *part; ++part) {
        if (*part != L'\\') continue;
        *part = 0;
        const BOOL made = CreateDirectoryW(path, nullptr);
        const DWORD error = GetLastError();
        *part = L'\\';
        if (!made && error != ERROR_ALREADY_EXISTS) {
            rg_log("gunsensor: configuratiemap niet te maken, fout %lu", static_cast<unsigned long>(error));
            return false;
        }
    }
    const wchar_t* section = L"/Script/Hodzero.AppGameInstance";
    const wchar_t* key = L"DevelopSetting_PlayerGunDevice";
    wchar_t value[16];
    GetPrivateProfileStringW(section, key, L"", value, 16, path);
    if (std::wcscmp(value, L"0") == 0) {
        rg_log("gunsensor: gebruikersconfiguratie staat al op GUN (0)");
        return true;
    }
    // Bewaar de oorspronkelijke gebruikersconfiguratie eenmalig, ook als een latere start opnieuw wijzigt.
    wchar_t backup[MAX_PATH];
    const int n = _snwprintf(backup, MAX_PATH, L"%ls.ravengun-backup", path);
    if (n < 0 || n >= MAX_PATH) return false;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES && !CopyFileW(path, backup, TRUE) && GetLastError() != ERROR_FILE_EXISTS) {
        rg_log("gunsensor: backup niet te maken; configuratie niet gewijzigd");
        return false;
    }
    if (!WritePrivateProfileStringW(section, key, L"0", path)) {
        rg_log("gunsensor: GUN (0) niet te bewaren, fout %lu", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    rg_log("gunsensor: gebruikersconfiguratie op GUN (0) gezet; overige instellingen behouden");
    return true;
}

void install_sensor_hooks() {
    struct Hook { const char* name; void* replacement; void** original; };
    const Hook hooks[] = {
        {"CreateFileA", reinterpret_cast<void*>(&sensor_CreateFileA), reinterpret_cast<void**>(&g_sensor_create)},
        {"ReadFile", reinterpret_cast<void*>(&sensor_ReadFile), reinterpret_cast<void**>(&g_sensor_read)},
        {"WriteFile", reinterpret_cast<void*>(&sensor_WriteFile), reinterpret_cast<void**>(&g_sensor_write)},
        {"CloseHandle", reinterpret_cast<void*>(&sensor_CloseHandle), reinterpret_cast<void**>(&g_sensor_close)},
        {"GetCommState", reinterpret_cast<void*>(&sensor_GetCommState), reinterpret_cast<void**>(&g_sensor_getstate)},
        {"SetCommState", reinterpret_cast<void*>(&sensor_SetCommState), reinterpret_cast<void**>(&g_sensor_setstate)},
        {"GetCommTimeouts", reinterpret_cast<void*>(&sensor_GetCommTimeouts), reinterpret_cast<void**>(&g_sensor_gettimeouts)},
        {"SetCommTimeouts", reinterpret_cast<void*>(&sensor_SetCommTimeouts), reinterpret_cast<void**>(&g_sensor_settimeouts)},
    };
    HMODULE exe = GetModuleHandleW(nullptr);
    size_t installed = 0;
    for (const auto& hook : hooks) {
        void* old = patch_iat(exe, "kernel32.dll", hook.name, hook.replacement);
        rg_log("gunsensor import %s: %s", hook.name, old ? "omgezet" : "ontbreekt");
        if (!old) break;
        *hook.original = old;
        ++installed;
    }
    if (installed != sizeof hooks / sizeof hooks[0]) {
        // Geen gedeeltelijke adapter die een event-handvat aan echte seriële APIs doorgeeft.
        for (size_t i = 0; i < installed; ++i)
            patch_iat(exe, "kernel32.dll", hooks[i].name, *hooks[i].original);
        rg_log("gunsensor: installatie teruggedraaid; vereiste import ontbreekt");
    } else rg_log("gunsensor: synchrone COM1-adapter actief in Hodzero, pid %lu", static_cast<unsigned long>(GetCurrentProcessId()));
}

// --- SetupAPI: de HID-GUID geeft precies een apparaat, het onze ----------------------------------------------------------------------
struct SpDevIfData { DWORD cbSize; GUID InterfaceClassGuid; DWORD Flags; ULONG_PTR Reserved; };
struct SpDevIfDetailW { DWORD cbSize; WCHAR DevicePath[1]; };

using GetClassDevsWFn = HANDLE(WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);
using EnumDevIfFn = BOOL(WINAPI*)(HANDLE, void*, const GUID*, DWORD, SpDevIfData*);
using GetDevIfDetailWFn = BOOL(WINAPI*)(HANDLE, SpDevIfData*, SpDevIfDetailW*, DWORD, PDWORD, void*);
using DestroyFn = BOOL(WINAPI*)(HANDLE);
using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using WriteFileFn = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
using GetOvResFn = BOOL(WINAPI*)(HANDLE, LPOVERLAPPED, LPDWORD, BOOL);
using CancelIoFn = BOOL(WINAPI*)(HANDLE);
using HidGuidFn = void(WINAPI*)(GUID*);
using HidStrFn = BOOLEAN(WINAPI*)(HANDLE, PVOID, ULONG);
using HidReportFn = BOOLEAN(WINAPI*)(HANDLE, PVOID, ULONG);

GetClassDevsWFn g_real_getclass; EnumDevIfFn g_real_enum; GetDevIfDetailWFn g_real_detail; DestroyFn g_real_destroy;
CreateFileWFn g_real_createfile; ReadFileFn g_real_readfile; WriteFileFn g_real_writefile; GetOvResFn g_real_getovres; CancelIoFn g_real_cancelio;
HidGuidFn g_real_hidguid; HidStrFn g_real_manuf, g_real_prod; HidReportFn g_real_getin, g_real_setout;

bool is_dev(HANDLE h) { return g_dev && h == g_dev; }

using IoctlFn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseFn = BOOL(WINAPI*)(HANDLE);
IoctlFn g_real_ioctl = &DeviceIoControl;
CloseFn g_real_close = &CloseHandle;
bool g_platform_enabled = false;
// De platformhandvatten (FddDriver, sghwmonitor, sghwreset). Het handvat is atomair en wordt pas gepubliceerd nadat `device` gezet
// is (release); DeviceIoControl en CloseHandle zoeken zonder g_cs. `device` is zelf ook atomair (relaxed): sluit een andere draad het
// handvat en hergebruikt hij de plek terwijl wij `device` lezen, dan is dat een gewone (goedaardige) race op een atomair, geen
// dataracen op een gewone variabele. g_platform_open telt de geopende handvatten: staat die op 0 (het
// spel opent ze meestal pas na het laden, en sluit ze weer), dan is er niets te vergelijken en slaan beide hooks de zoektocht over.
// Het opschrijven (CreateFileW) blijft onder g_cs: dat gebeurt een paar keer per run.
struct PlatformHandle {
    std::atomic<HANDLE> handle{nullptr};
    std::atomic<rg_allsio::PlatformDevice> device{rg_allsio::PlatformDevice::Keychip};
};
PlatformHandle g_platform_handles[16];
std::atomic<int> g_platform_open{0};
LONG g_platform_logs = 0;
bool platform_device(HANDLE handle, rg_allsio::PlatformDevice& device) {
    if (!handle || g_platform_open.load(std::memory_order_acquire) == 0) return false;
    for (const auto& entry : g_platform_handles) {
        if (entry.handle.load(std::memory_order_acquire) == handle) { device = entry.device.load(std::memory_order_relaxed); return true; }
    }
    return false;
}
BOOL WINAPI rg_CloseHandle(HANDLE h) {
    if (h && g_platform_open.load(std::memory_order_acquire) > 0) {
        for (auto& entry : g_platform_handles) {
            // Eerst vergelijken, alleen bij een treffer een CAS: de meeste CloseHandle-aanroepen in het spel gaan over andere handvatten.
            if (entry.handle.load(std::memory_order_acquire) != h) continue;
            HANDLE expected = h;
            if (entry.handle.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
                g_platform_open.fetch_sub(1, std::memory_order_release);
                break;
            }
        }
    }
    return g_real_close(h);
}
BOOL WINAPI rg_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_len,
                               LPVOID out, DWORD out_len, LPDWORD got, LPOVERLAPPED ov) {
    rg_allsio::PlatformDevice device;
    if (!platform_device(h, device)) return g_real_ioctl(h, code, in, in_len, out, out_len, got, ov);
    size_t bytes = 0;
    const auto result = rg_allsio::platform_ioctl(device, code, out, out_len, bytes);
    if (InterlockedIncrement(&g_platform_logs) <= 300)
        rg_log("platform IOCTL apparaat=%d code=0x%08lx in=%lu uit=%lu -> %d (%lu bytes)",
               static_cast<int>(device), static_cast<unsigned long>(code), static_cast<unsigned long>(in_len),
               static_cast<unsigned long>(out_len), static_cast<int>(result), static_cast<unsigned long>(bytes));
    if (got) *got = static_cast<DWORD>(bytes);
    if (result != rg_allsio::IoctlResult::Ok) {
        SetLastError(result == rg_allsio::IoctlResult::Unsupported ? ERROR_INVALID_FUNCTION : ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (ov) {
        ov->Internal = 0;
        ov->InternalHigh = bytes;
        if (ov->hEvent) SetEvent(reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(ov->hEvent) & ~static_cast<ULONG_PTR>(1)));
    }
    return TRUE;
}

HANDLE WINAPI rg_SetupDiGetClassDevsW(const GUID* g, PCWSTR en, HWND hwnd, DWORD flags) {
    if (g && std::memcmp(g, &kHidGuid, sizeof(GUID)) == 0) {
        rg_log("SetupDiGetClassDevsW(HID-GUID, 0x%lx) -> nagebootste lijst met een apparaat", static_cast<unsigned long>(flags));
        return &g_set_dummy;
    }
    return g_real_getclass ? g_real_getclass(g, en, hwnd, flags) : INVALID_HANDLE_VALUE;
}
BOOL WINAPI rg_SetupDiEnumDeviceInterfaces(HANDLE set, void* info, const GUID* g, DWORD index, SpDevIfData* d) {
    if (set == &g_set_dummy) {
        if (index == 0 && d && d->cbSize >= sizeof(SpDevIfData)) {
            d->InterfaceClassGuid = kHidGuid;
            d->Flags = 1;
            d->Reserved = 0;
            return TRUE;
        }
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (!g_real_enum) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return g_real_enum(set, info, g, index, d);
}
BOOL WINAPI rg_SetupDiGetDeviceInterfaceDetailW(HANDLE set, SpDevIfData* d, SpDevIfDetailW* det, DWORD size, PDWORD req, void* dev) {
    if (set == &g_set_dummy) {
        const wchar_t* path = rg_allsio::device_path();
        const DWORD bytes = static_cast<DWORD>((wcslen(path) + 1) * sizeof(wchar_t));
        const DWORD need = static_cast<DWORD>(offsetof(SpDevIfDetailW, DevicePath)) + bytes;
        if (req) *req = need;
        if (!det || size < need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
        std::memcpy(det->DevicePath, path, bytes);
        return TRUE;
    }
    if (!g_real_detail) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    return g_real_detail(set, d, det, size, req, dev);
}
BOOL WINAPI rg_SetupDiDestroyDeviceInfoList(HANDLE set) {
    if (set == &g_set_dummy) return TRUE;
    return g_real_destroy ? g_real_destroy(set) : FALSE;
}

// Optionele platformidentiteit. De echte API blijft voor alle andere gevallen intact.
using GetComputerNameAFn = BOOL(WINAPI*)(LPSTR, LPDWORD);
GetComputerNameAFn g_real_computername = &GetComputerNameA;
char g_main_id[16] = {};
BOOL WINAPI rg_GetComputerNameA(LPSTR name, LPDWORD size) {
    if (!g_main_id[0]) return g_real_computername(name, size);
    if (!size) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    if (!name || *size < sizeof g_main_id) {
        *size = sizeof g_main_id;
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
    std::memcpy(name, g_main_id, sizeof g_main_id);
    *size = sizeof g_main_id - 1;
    return TRUE;
}

// --- HID --------------------------------------------------------------------------------------------------------------------------------
void WINAPI rg_HidD_GetHidGuid(GUID* g) { if (g) *g = kHidGuid; }

BOOLEAN copy_string(const std::wstring& s, PVOID buf, ULONG bytes) {
    const size_t need = (s.size() + 1) * sizeof(wchar_t);
    if (!buf || bytes < need) return FALSE;
    std::memcpy(buf, s.c_str(), need);
    return TRUE;
}
BOOLEAN WINAPI rg_HidD_GetManufacturerString(HANDLE h, PVOID buf, ULONG len) {
    if (!is_dev(h)) return g_real_manuf ? g_real_manuf(h, buf, len) : FALSE;
    return copy_string(rg_allsio::manufacturer_string(), buf, len);
}
BOOLEAN WINAPI rg_HidD_GetProductString(HANDLE h, PVOID buf, ULONG len) {
    if (!is_dev(h)) return g_real_prod ? g_real_prod(h, buf, len) : FALSE;
    return copy_string(rg_allsio::product_string(), buf, len);
}

void log_request(const uint8_t* rpt, rg_allsio::Pipe via) {
    const LONG n = InterlockedIncrement(&g_logged_req);
    if (n > 400) return;   // het begin van de opstart zegt genoeg; daarna alleen de teller
    char hex[200];
    hexline(hex, sizeof hex, rpt, 24);
    rg_log("verzoek #%ld via %s: %s", static_cast<long>(n), via == rg_allsio::Pipe::Control ? "besturing (SetOutputReport)" : "interrupt (WriteFile)", hex);
}

BOOLEAN WINAPI rg_HidD_SetOutputReport(HANDLE h, PVOID buf, ULONG len) {
    if (!is_dev(h)) return g_real_setout ? g_real_setout(h, buf, len) : FALSE;
    if (len < rg_allsio::kReport) { rg_log("HidD_SetOutputReport: lengte %lu (< 64)", static_cast<unsigned long>(len)); return FALSE; }
    EnterCriticalSection(&g_cs);
    const bool ok = g_board.on_output(static_cast<const uint8_t*>(buf), rg_allsio::Pipe::Control);
    log_request(static_cast<const uint8_t*>(buf), rg_allsio::Pipe::Control);
    LeaveCriticalSection(&g_cs);
    return ok ? TRUE : FALSE;
}
BOOLEAN WINAPI rg_HidD_GetInputReport(HANDLE h, PVOID buf, ULONG len) {
    if (!is_dev(h)) return g_real_getin ? g_real_getin(h, buf, len) : FALSE;
    if (len < rg_allsio::kReport) return FALSE;
    EnterCriticalSection(&g_cs);
    g_board.control_ack(static_cast<uint8_t*>(buf));
    LeaveCriticalSection(&g_cs);
    return TRUE;
}

// --- CreateFileW / ReadFile / WriteFile / GetOverlappedResult / CancelIo voor ons handvat -------------------------------------------
HANDLE WINAPI rg_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (name && _wcsicmp(name, rg_allsio::device_path()) == 0) {
        EnterCriticalSection(&g_cs);
        if (!g_dev) g_dev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE h = g_dev;
        LeaveCriticalSection(&g_cs);
        rg_log("CreateFileW(USB-IO-pad) -> handvat %p (toegang 0x%lx, vlaggen 0x%lx)", h, static_cast<unsigned long>(access), static_cast<unsigned long>(flags));
        return h ? h : INVALID_HANDLE_VALUE;
    }
    if (g_platform_enabled && name) {
        rg_allsio::PlatformDevice device;
        bool match = true;
        if (_wcsicmp(name, L"\\??\\FddDriver") == 0 || _wcsicmp(name, L"\\\\.\\FddDriver") == 0)
            device = rg_allsio::PlatformDevice::Keychip;
        else if (_wcsicmp(name, L"\\\\.\\sghwmonitor") == 0)
            device = rg_allsio::PlatformDevice::Hwmon;
        else if (_wcsicmp(name, L"\\\\.\\sghwreset") == 0)
            device = rg_allsio::PlatformDevice::Reset;
        else match = false;
        if (match) {
            HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!h) return INVALID_HANDLE_VALUE;
            bool stored = false;
            EnterCriticalSection(&g_cs);
            for (auto& entry : g_platform_handles) {
                if (!entry.handle.load(std::memory_order_relaxed)) {
                    entry.device.store(device, std::memory_order_relaxed);   // eerst het apparaat,
                    entry.handle.store(h, std::memory_order_release);        // dan pas het handvat zichtbaar maken
                    g_platform_open.fetch_add(1, std::memory_order_release);
                    stored = true;
                    break;
                }
            }
            LeaveCriticalSection(&g_cs);
            if (!stored) { CloseHandle(h); SetLastError(ERROR_TOO_MANY_OPEN_FILES); return INVALID_HANDLE_VALUE; }
            rg_log("platform open %ls -> %p", name, h);
            return h;
        }
    }
    HANDLE h = g_real_createfile(name, access, share, sa, disp, flags, tmpl);
    const DWORD error = GetLastError();
    if (name && h == INVALID_HANDLE_VALUE && (wcsstr(name, L"sghw") || wcsstr(name, L"Fdd")))
        rg_log("apparaat open mislukt %ls fout %lu", name, static_cast<unsigned long>(error));
    SetLastError(error);
    return h;
}

BOOL WINAPI rg_WriteFile(HANDLE h, LPCVOID buf, DWORD len, LPDWORD written, LPOVERLAPPED ov) {
    if (!is_dev(h)) return g_real_writefile(h, buf, len, written, ov);
    if (!buf || len < rg_allsio::kReport) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    EnterCriticalSection(&g_cs);
    g_board.on_output(static_cast<const uint8_t*>(buf), rg_allsio::Pipe::Interrupt);
    log_request(static_cast<const uint8_t*>(buf), rg_allsio::Pipe::Interrupt);
    LeaveCriticalSection(&g_cs);
    if (written) *written = len;
    if (ov) {
        ov->Internal = 0;
        ov->InternalHigh = len;
        if (ov->hEvent) SetEvent(reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(ov->hEvent) & ~static_cast<ULONG_PTR>(1)));
    }
    return TRUE;   // meteen klaar; de daemon wacht anders hooguit 16 ms op de gebeurtenis
}

BOOL WINAPI rg_ReadFile(HANDLE h, LPVOID buf, DWORD len, LPDWORD got, LPOVERLAPPED ov) {
    if (!is_dev(h)) return g_real_readfile(h, buf, len, got, ov);
    if (!buf || len < rg_allsio::kReport || !ov) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    EnterCriticalSection(&g_cs);
    if (g_queued > 0) {
        std::memcpy(buf, g_frame, rg_allsio::kReport);
        --g_queued;
        LeaveCriticalSection(&g_cs);
        InterlockedIncrement(&g_n_read_complete);
        if (got) *got = rg_allsio::kReport;
        return TRUE;
    }
    // Nog geen nieuw rapport: de lezing blijft open staan en de tikdraad voltooit hem (zoals een echte interruptpijp).
    g_read_ov = ov;
    g_read_buf = static_cast<uint8_t*>(buf);
    g_read_done = false;
    ov->Internal = 0x103;   // STATUS_PENDING
    ov->InternalHigh = 0;
    LeaveCriticalSection(&g_cs);
    InterlockedIncrement(&g_n_read_pending);
    SetLastError(ERROR_IO_PENDING);
    return FALSE;
}

BOOL WINAPI rg_GetOverlappedResult(HANDLE h, LPOVERLAPPED ov, LPDWORD n, BOOL wait) {
    if (!is_dev(h)) return g_real_getovres(h, ov, n, wait);
    for (int i = 0;; ++i) {
        EnterCriticalSection(&g_cs);
        const bool mine = ov && ov == g_read_ov;
        const bool done = mine && g_read_done;
        if (done) { g_read_ov = nullptr; g_read_buf = nullptr; g_read_done = false; }
        LeaveCriticalSection(&g_cs);
        if (!mine) { if (n) *n = ov ? static_cast<DWORD>(ov->InternalHigh) : 0; return TRUE; }   // schrijfverzoek: altijd klaar
        if (done) { if (n) *n = rg_allsio::kReport; return TRUE; }
        if (!wait || i > 200) { SetLastError(ERROR_IO_INCOMPLETE); return FALSE; }
        Sleep(2);
    }
}

BOOL WINAPI rg_CancelIo(HANDLE h) {
    if (!is_dev(h)) return g_real_cancelio ? g_real_cancelio(h) : FALSE;
    EnterCriticalSection(&g_cs);
    g_read_ov = nullptr;
    g_read_buf = nullptr;
    g_read_done = false;
    LeaveCriticalSection(&g_cs);
    return TRUE;
}

// --- de draden: de besturingspijp lezen, het ingangsrapport tikken ------------------------------------------------------------------
DWORD WINAPI pipe_thread(LPVOID) {
    HANDLE p = INVALID_HANDLE_VALUE;
    uint8_t acc[4096 + 64];
    size_t have = 0;
    bool announced = false;
    while (!g_stop) {
        if (p == INVALID_HANDLE_VALUE) {
            p = CreateFileA(g_pipe_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (p == INVALID_HANDLE_VALUE) { Sleep(250); continue; }
            rg_log("besturingspijp %s geopend", g_pipe_name);
            have = 0;
        }
        DWORD got = 0;
        if (!ReadFile(p, acc + have, 4096, &got, nullptr) || got == 0) {
            CloseHandle(p);
            p = INVALID_HANDLE_VALUE;
            rg_log("besturingspijp gesloten of leeg (fout %lu); opnieuw proberen", static_cast<unsigned long>(GetLastError()));
            Sleep(250);
            continue;
        }
        have += got;
        // Records van 64 bytes; het nieuwste telt. Wat over is blijft voor de volgende lezing.
        const size_t whole = have / rg_allsio::kReport * rg_allsio::kReport;
        if (whole >= rg_allsio::kReport) {
            EnterCriticalSection(&g_cs);
            std::memcpy(g_pipe_report, acc + whole - rg_allsio::kReport, rg_allsio::kReport);
            g_have_pipe = true;
            LeaveCriticalSection(&g_cs);
            if (!announced) {
                char hex[200];
                hexline(hex, sizeof hex, g_pipe_report, 40);
                rg_log("eerste AllsUsbIo-rapport: %s", hex);
                announced = true;
            }
            std::memmove(acc, acc + whole, have - whole);
            have -= whole;
        }
    }
    return 0;
}

DWORD WINAPI tick_thread(LPVOID) {
    DWORD last_report = 0;
    while (!g_stop) {
        Sleep(g_period_ms);
        EnterCriticalSection(&g_cs);
        if (g_dev) {   // pas tikken als de daemon het apparaat heeft geopend
            uint8_t frame[rg_allsio::kReport];
            rg_allsio::Board::input_frame(g_have_pipe ? g_pipe_report : nullptr, frame);
            InterlockedIncrement(&g_n_frames);
            if (g_read_ov && !g_read_done && g_read_buf) {
                std::memcpy(g_read_buf, frame, sizeof frame);
                g_read_done = true;
                g_read_ov->Internal = 0;
                g_read_ov->InternalHigh = rg_allsio::kReport;
                if (g_read_ov->hEvent) SetEvent(reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(g_read_ov->hEvent) & ~static_cast<ULONG_PTR>(1)));
            } else {
                std::memcpy(g_frame, frame, sizeof frame);
                if (g_queued < 8) ++g_queued;
            }
        }
        const DWORD now = GetTickCount();
        const bool report = g_dev && now - last_report >= 10000;
        if (report) last_report = now;
        LeaveCriticalSection(&g_cs);
        if (report)
            rg_log("stand: verzoeken %lu (ongeldig %lu), ingangsrapporten %ld, lezingen direct %ld / wachtend %ld, pijp %s", static_cast<unsigned long>(g_board.requests()),
                   static_cast<unsigned long>(g_board.bad()), static_cast<long>(g_n_frames), static_cast<long>(g_n_read_complete),
                   static_cast<long>(g_n_read_pending), g_have_pipe ? "ja" : "nee");
    }
    return 0;
}

bool is_process(const wchar_t* name) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
    const wchar_t* base = wcsrchr(exe, L'\\');
    base = base ? base + 1 : exe;
    return _wcsicmp(base, name) == 0;
}

}  // namespace

extern "C" __declspec(dllexport) int rg_allsio_present() { return 1; }

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        g_cs_ready = true;
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(inst, buf, MAX_PATH);
        wchar_t* slash = wcsrchr(buf, L'\\');
        if (slash) *slash = 0;
        _snwprintf(g_log_path, MAX_PATH, L"%ls\\rg-allsio.log", buf);
        char env[256];
        if (GetEnvironmentVariableA("RG_ALLSIO_OFF", env, sizeof env) > 0 && env[0] == '1') { rg_log("RG_ALLSIO_OFF=1: niets doen"); return TRUE; }
        if (is_process(L"Hodzero-Win64-Shipping.exe")) {
            if (GetEnvironmentVariableA("RG_ALLSIO_GUN_SENSOR", env, sizeof env) == 1 && env[0] == '1') {
                // De ini aanpassen is hulp, geen voorwaarde: als het niet lukt blijven de hooks
                // gewoon staan (het profiel kan de waarde ook zelf zetten) in plaats van dat de
                // hele DLL het spel laat mislukken.
                if (GetEnvironmentVariableA("RG_ALLSIO_GUN_CONFIG", env, sizeof env) == 1 && env[0] == '1' && !configure_sensor_mode())
                    rg_log("gunsensor: RG_ALLSIO_GUN_CONFIG kon Game.ini niet aanpassen; de hooks blijven actief");
                install_sensor_hooks();
            }
            return TRUE;
        }
        if (!is_process(L"amdaemon.exe")) return TRUE;
        if (GetEnvironmentVariableA("RG_ALLSIO_PIPE", env, sizeof env) > 0 && env[0]) std::snprintf(g_pipe_name, sizeof g_pipe_name, "%s", env);
        if (GetEnvironmentVariableA("RG_ALLSIO_PERIOD_MS", env, sizeof env) > 0 && env[0]) {
            const long v = std::strtol(env, nullptr, 10);
            if (v >= 1 && v <= 100) g_period_ms = static_cast<DWORD>(v);
        }
        rg_log("rg-allsio geladen in amdaemon.exe (pid %lu); pijp %s, periode %lu ms; productstring: %ls", static_cast<unsigned long>(GetCurrentProcessId()), g_pipe_name,
               static_cast<unsigned long>(g_period_ms), rg_allsio::product_string().c_str());
        struct Hook { const char* dll; const char* fn; void* repl; void** orig; };
        const Hook hooks[] = {
            {"hid.dll", "HidD_GetHidGuid", reinterpret_cast<void*>(&rg_HidD_GetHidGuid), reinterpret_cast<void**>(&g_real_hidguid)},
            {"hid.dll", "HidD_GetManufacturerString", reinterpret_cast<void*>(&rg_HidD_GetManufacturerString), reinterpret_cast<void**>(&g_real_manuf)},
            {"hid.dll", "HidD_GetProductString", reinterpret_cast<void*>(&rg_HidD_GetProductString), reinterpret_cast<void**>(&g_real_prod)},
            {"hid.dll", "HidD_GetInputReport", reinterpret_cast<void*>(&rg_HidD_GetInputReport), reinterpret_cast<void**>(&g_real_getin)},
            {"hid.dll", "HidD_SetOutputReport", reinterpret_cast<void*>(&rg_HidD_SetOutputReport), reinterpret_cast<void**>(&g_real_setout)},
            {"setupapi.dll", "SetupDiGetClassDevsW", reinterpret_cast<void*>(&rg_SetupDiGetClassDevsW), reinterpret_cast<void**>(&g_real_getclass)},
            {"setupapi.dll", "SetupDiEnumDeviceInterfaces", reinterpret_cast<void*>(&rg_SetupDiEnumDeviceInterfaces), reinterpret_cast<void**>(&g_real_enum)},
            {"setupapi.dll", "SetupDiGetDeviceInterfaceDetailW", reinterpret_cast<void*>(&rg_SetupDiGetDeviceInterfaceDetailW), reinterpret_cast<void**>(&g_real_detail)},
            {"setupapi.dll", "SetupDiDestroyDeviceInfoList", reinterpret_cast<void*>(&rg_SetupDiDestroyDeviceInfoList), reinterpret_cast<void**>(&g_real_destroy)},
            {"kernel32.dll", "CreateFileW", reinterpret_cast<void*>(&rg_CreateFileW), reinterpret_cast<void**>(&g_real_createfile)},
            {"kernel32.dll", "ReadFile", reinterpret_cast<void*>(&rg_ReadFile), reinterpret_cast<void**>(&g_real_readfile)},
            {"kernel32.dll", "WriteFile", reinterpret_cast<void*>(&rg_WriteFile), reinterpret_cast<void**>(&g_real_writefile)},
            {"kernel32.dll", "GetOverlappedResult", reinterpret_cast<void*>(&rg_GetOverlappedResult), reinterpret_cast<void**>(&g_real_getovres)},
            {"kernel32.dll", "CancelIo", reinterpret_cast<void*>(&rg_CancelIo), reinterpret_cast<void**>(&g_real_cancelio)},
        };
        HMODULE exe = GetModuleHandleW(nullptr);
        const DWORD id_len = GetEnvironmentVariableA("RG_ALLSIO_MAIN_ID", env, sizeof env);
        if (id_len > 0) {
            if (id_len < sizeof env && rg_allsio::valid_main_id(env)) {
                void* old = patch_iat(exe, "kernel32.dll", "GetComputerNameA", reinterpret_cast<void*>(&rg_GetComputerNameA));
                if (old) {
                    g_real_computername = reinterpret_cast<GetComputerNameAFn>(old);
                    std::memcpy(g_main_id, env, sizeof g_main_id);
                }
                rg_log("platformidentiteit: GetComputerNameA %s", old ? "omgezet (15 tekens)" : "NIET in de daemon");
            } else {
                rg_log("RG_ALLSIO_MAIN_ID ongeldig: verwacht 15 tekens A-Z, 0-9 of '-'; echte computernaam behouden");
            }
        }
        g_platform_enabled = GetEnvironmentVariableA("RG_ALLSIO_PLATFORM", env, sizeof env) == 1 && env[0] == '1';
        if (g_platform_enabled) {
            void* old = patch_iat(exe, "kernel32.dll", "DeviceIoControl", reinterpret_cast<void*>(&rg_DeviceIoControl));
            if (old) g_real_ioctl = reinterpret_cast<IoctlFn>(old);
            void* close = patch_iat(exe, "kernel32.dll", "CloseHandle", reinterpret_cast<void*>(&rg_CloseHandle));
            if (close) g_real_close = reinterpret_cast<CloseFn>(close);
            g_platform_enabled = old && close;
            rg_log("platformapparaten %s", g_platform_enabled ? "ingeschakeld" : "NIET ingeschakeld: import ontbreekt");
        }
        for (const Hook& h : hooks) {
            void* old = patch_iat(exe, h.dll, h.fn, h.repl);
            *h.orig = old;
            rg_log("importtabel: %s!%s %s", h.dll, h.fn, old ? "omgezet" : "NIET in de daemon");
        }
        // Zonder omgezette CreateFileW/ReadFile/WriteFile/GetOverlappedResult zou de daemon ons handvat aan het echte systeem geven.
        if (!g_real_createfile) g_real_createfile = &CreateFileW;
        if (!g_real_readfile) g_real_readfile = &ReadFile;
        if (!g_real_writefile) g_real_writefile = &WriteFile;
        if (!g_real_getovres) g_real_getovres = &GetOverlappedResult;
        g_thread_pipe = CreateThread(nullptr, 0, pipe_thread, nullptr, 0, nullptr);
        g_thread_tick = CreateThread(nullptr, 0, tick_thread, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH && g_cs_ready) {
        InterlockedExchange(&g_stop, 1);
    }
    return TRUE;
}
