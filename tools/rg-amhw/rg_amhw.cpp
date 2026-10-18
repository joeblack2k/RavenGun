// SPDX-License-Identifier: GPL-3.0-or-later
// rg-amhw.dll: de kastapparaten van een Sega Ring-kast voor de AM Library in de exe van LGI, LGI3D, LGS, GG, OG, SDR en Transformers,
// zonder de gesloten TeknoParrot-kern. 32-bits Windows-DLL, geladen met Preload = true (TP_PRELOAD_DLL) door de OpenParrot-lader, nadat
// de kern en voordat het spel zijn eerste regel draait. Zoals rg-mfcam zet hij invoeren in de importtabel van de HOOFDmodule om; er wordt
// niets in kernel32 zelf gepatcht (en dus niets dat met de MinGW-haken van onze OpenParrot-kern botst).
//
// Wat hij nabootst (docs/research/segajvs/README.md, "Kastapparaten"):
//   \\.\columba   het DMI/SMBIOS-apparaat: DeviceIoControl 0x9C406104 leest fysiek geheugen (amDongle? nee: amOemstring/amPlatform, zie
//                 amhw_dmi.h). Zonder dit faalt het bordtype en crasht de game op een nulwijzer (LGI 0x6F0515).
//   \\.\mxsram    het backup-SRAM: 0x9C406000 = stuurprogrammaversie (laag woord 1), 0x70000 = geometrie (0x18 bytes), daarna gewone
//                 SetFilePointer/ReadFile/WriteFile in sectoren. Achter een bestand (mxsram.bin) naast deze DLL, zodat instellingen en
//                 scores een herstart overleven (de view is lokaal; nooit de NAS).
//   \\.\mxjvs    de JVS-lijn (stuurprogramma-modus van amJvst): wordt de pipe van onze JVS-brug; DeviceIoControl 0x9C402000 = kader erheen, antwoordkader terug
//                 (amhw_jvs.h), comm-aanroepen slagen, sense-lijn uit TeknoParrot_JvsState.
// Alles wat de game verder met `\\.\...` of SetupDi probeert wordt GELOGD en ongewijzigd doorgegeven: rg-amhw.log is de lijst van wat
// er nog ontbreekt (mxsuperio = hardwaremonitor, de SMBus-driver voor EEPROM en dipschakelaars via SetupDi).
//
// Omgeving: RG_AMHW_OEM (platform-id: AAM RingWide, AAL RingEdge, AAL2 RingEdge 2; standaard AAM), RG_AMHW_VENDOR (Supermicro, NEC, Advantech;
// standaard Supermicro), RG_AMHW_DIR (map voor mxsram.bin; standaard de map van de DLL), RG_AMHW_SRAM_KB (standaard 2048), RG_AMHW_OFF=1.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "amhw_dmi.h"
#include "amhw_jvs.h"
#include "amhw_smbus.h"
#include "amhw_sysdata.h"
#include "amhw_table.h"

namespace {

HINSTANCE g_self = nullptr;
CRITICAL_SECTION g_cs;
char g_log_path[MAX_PATH] = "rg-amhw.log";

void rg_log(const char* fmt, ...) {
    EnterCriticalSection(&g_cs);
    if (FILE* f = std::fopen(g_log_path, "a")) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        std::fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(f, fmt, ap);
        va_end(ap);
        std::fputc('\n', f);
        std::fclose(f);
    }
    LeaveCriticalSection(&g_cs);
}

// --- de apparaten --------------------------------------------------------------------------------------------------------------------
enum class Kind { Columba, Sram, Jvs, Smbus, Superio };
const char* kind_name(Kind k) {
    return k == Kind::Columba ? "columba" : k == Kind::Sram ? "mxsram" : k == Kind::Jvs ? "mxjvs" : k == Kind::Smbus ? "smbus" : "mxsuperio";
}
struct Dev {
    Kind kind;
    long long pos = 0;   // SRAM: de bestandspositie
};
std::map<HANDLE, Dev> g_devs;
rg_amhw::Platform g_platform;
std::vector<uint8_t> g_sram;
char g_sram_path[MAX_PATH] = "mxsram.bin";
bool g_sram_loaded = false;
uint8_t g_region = 0;                          // RG_AMHW_REGION: de regio van de kast voor AM_SYSDATA_STATIC (amhw_sysdata.h); 0 = niet zetten
rg_amhw::HwRegs* g_hw = nullptr;               // SMBus (dipschakelaars, EEPROM) en Super I/O (hardwaremonitor): registerbestanden
char g_eeprom_path[MAX_PATH] = "mxeeprom.bin";

constexpr DWORD kIoctlVersion = 0x9C406000;   // mxsram, mxsuperio, SMBus: 4 bytes, laag woord 1
constexpr DWORD kIoctlPhysRead = 0x9C406104;  // columba
constexpr DWORD kIoctlSramGeometry = 0x70000;

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using DeviceIoControlFn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);
using SetFilePointerFn = DWORD(WINAPI*)(HANDLE, LONG, PLONG, DWORD);
using ReadWriteFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using SetupGetClassDevsAFn = HDEVINFO(WINAPI*)(const GUID*, PCSTR, HWND, DWORD);
CreateFileWFn g_create_w = CreateFileW;
CreateFileAFn g_create_a = CreateFileA;
DeviceIoControlFn g_ioctl = DeviceIoControl;
CloseHandleFn g_close = CloseHandle;
SetFilePointerFn g_seek = SetFilePointer;
ReadWriteFn g_read = reinterpret_cast<ReadWriteFn>(ReadFile);
ReadWriteFn g_write = reinterpret_cast<ReadWriteFn>(WriteFile);
SetupGetClassDevsAFn g_setup_classdevs = nullptr;
using CommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using CommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);
using SetupCommFn = BOOL(WINAPI*)(HANDLE, DWORD, DWORD);
using PurgeCommFn = BOOL(WINAPI*)(HANDLE, DWORD);
using ModemStatusFn = BOOL(WINAPI*)(HANDLE, LPDWORD);
CommStateFn g_get_comm_state = GetCommState, g_set_comm_state = SetCommState;
CommTimeoutsFn g_get_comm_timeouts = GetCommTimeouts, g_set_comm_timeouts = SetCommTimeouts;
SetupCommFn g_setup_comm = SetupComm;
PurgeCommFn g_purge_comm = PurgeComm;
ModemStatusFn g_modem_status = GetCommModemStatus;
CRITICAL_SECTION g_jvs_cs;                                 // een JVS-transactie (schrijven + antwoord lezen) tegelijk
char g_jvs_pipe[128] = "\\\\.\\pipe\\TeknoParrot_JVS";      // de pipe van onze brug (rg-jvsbridge)
HANDLE g_jvs_map = nullptr;
const volatile uint8_t* g_jvs_state = nullptr;             // byte 0 = het bord heeft een adres (door de brug gespiegeld)
constexpr DWORD kJvsReplyMs = 250;

bool is_dev_path(const char* n, const char* tail) { return n && lstrcmpiA(n, tail) == 0; }

void sram_load() {
    if (g_sram_loaded) return;
    g_sram_loaded = true;
    if (FILE* f = std::fopen(g_sram_path, "rb")) {
        const size_t n = std::fread(g_sram.data(), 1, g_sram.size(), f);
        std::fclose(f);
        rg_log("mxsram: %s ingelezen (%lu van %lu bytes)", g_sram_path, static_cast<unsigned long>(n), static_cast<unsigned long>(g_sram.size()));
    } else {
        rg_log("mxsram: %s bestaat nog niet; leeg SRAM (%lu bytes)", g_sram_path, static_cast<unsigned long>(g_sram.size()));
    }
}
void sram_flush(long long off, size_t n) {
    FILE* f = std::fopen(g_sram_path, "r+b");
    if (!f) f = std::fopen(g_sram_path, "w+b");
    if (!f) {
        static bool once = false;
        if (!once) { once = true; rg_log("mxsram: %s is niet te schrijven; het SRAM blijft alleen in het geheugen", g_sram_path); }
        return;
    }
    std::fseek(f, 0, SEEK_END);
    if (static_cast<size_t>(std::ftell(f)) < g_sram.size()) {
        std::fseek(f, 0, SEEK_SET);
        std::fwrite(g_sram.data(), 1, g_sram.size(), f);   // eerste keer: het hele bestand, daarna alleen wat verandert
    } else {
        std::fseek(f, static_cast<long>(off), SEEK_SET);
        std::fwrite(g_sram.data() + off, 1, n, f);
    }
    std::fclose(f);
}

HANDLE make_handle(Kind k, const char* name) {
    HANDLE h = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // een echt, uniek handvat; CloseHandle werkt er gewoon op
    if (!h) return INVALID_HANDLE_VALUE;
    EnterCriticalSection(&g_cs);
    g_devs[h] = Dev{k, 0};
    LeaveCriticalSection(&g_cs);
    rg_log("%s: geopend (nagebootst), handvat %p", name, h);
    return h;
}
bool lookup(HANDLE h, Dev& d) {
    EnterCriticalSection(&g_cs);
    auto it = g_devs.find(h);
    const bool found = it != g_devs.end();
    if (found) d = it->second;
    LeaveCriticalSection(&g_cs);
    return found;
}

// \\.\mxjvs: het stuurprogramma van de I/O-kaart. Wij geven de pipe van onze JVS-brug; die is er pas als de brug draait, dus even wachten.
HANDLE open_jvs() {
    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD e = 0;
    for (int i = 0; i < 50; ++i) {
        h = g_create_a(g_jvs_pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) break;
        e = GetLastError();
        if (e == ERROR_PIPE_BUSY) WaitNamedPipeA(g_jvs_pipe, 100);
        else Sleep(100);
    }
    if (h == INVALID_HANDLE_VALUE) {
        rg_log("mxjvs: de pipe %s is niet te openen (fout %lu); is de JVS-brug gestart?", g_jvs_pipe, static_cast<unsigned long>(e));
        SetLastError(e);
        return INVALID_HANDLE_VALUE;
    }
    EnterCriticalSection(&g_cs);
    g_devs[h] = Dev{Kind::Jvs, 0};
    LeaveCriticalSection(&g_cs);
    rg_log("mxjvs: geopend -> pipe %s, handvat %p", g_jvs_pipe, h);
    return h;
}

// De AM Library leest bij de dongle een "<spel-id>_Table.dat" (amDongleSetAuthConfig): in de games zonder zo'n bestand op de NAS (LGS, GG, OG, SDR) faalt de
// init daarop ("Failed to set Auth Config File", dan "Keychip Not Found"). De game leest er alleen blokken uit (bytes aantal gecontroleerd, geen som):
// een bestand van de goede grootte met vaste pseudo-willekeurige bytes volstaat. Wij maken het naast de DLL (de view; nooit de NAS) en laten de game
// dat bestand openen als het opgegeven pad niet bestaat.
bool is_table_name(const char* n) {
    const size_t len = n ? std::strlen(n) : 0;
    return len > 10 && lstrcmpiA(n + len - 10, "_Table.dat") == 0;
}
std::string g_table_dir;
std::string g_dir;
// De Ring-games (teaArcade2) schrijven hun eigen log naar "debugout.txt" in de werkmap: ArcadeManager::initialize, Dongle, HwMonitor, JvsManager ... en de reden van
// een mislukte start. In de view is dat een symlink naar de (alleen-lezen) NAS, dus de schrijfactie mislukt stil. Wij sturen schrijfacties naar een gewoon bestand
// naast de DLL ("debugout-spel.txt"); gelezen wordt het origineel nog steeds van de NAS.
bool is_debugout_write(const char* n, DWORD acc) {
    if (!n || !(acc & (GENERIC_WRITE | FILE_APPEND_DATA | FILE_WRITE_DATA))) return false;
    const char* b = std::strrchr(n, '\\');
    const char* b2 = std::strrchr(n, '/');
    if (b2 && (!b || b2 > b)) b = b2;
    return lstrcmpiA(b ? b + 1 : n, "debugout.txt") == 0;
}
std::string table_fallback(const char* n) {
    const char* base = std::strrchr(n, '\\');
    const char* base2 = std::strrchr(n, '/');
    if (base2 && (!base || base2 > base)) base = base2;
    base = base ? base + 1 : n;
    CreateDirectoryA(g_table_dir.c_str(), nullptr);
    const std::string path = g_table_dir + "\\" + base;
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const std::vector<uint8_t> data = rg_amhw::fake_table(base);   // dezelfde bytes als de AM-daemon van rg-direct genereert (amhw_table.h)
        if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); }
        rg_log("tabel: %s ontbreekt; vervangen door %s (%lu bytes, vast patroon)", n, path.c_str(), static_cast<unsigned long>(data.size()));
    }
    return path;
}

std::string env(const char* k, const char* def);

// De SMBus-driver heeft geen vast pad: de game vraagt het via SetupDi (GUID {5C49E1FE-...}) en krijgt van ons dit pad.
const char* const kSmbusPath = "\\\\?\\rg-amhw-smbus";
const GUID kSmbusGuid = {0x5C49E1FE, 0x3FEC, 0x4B8D, {0xA4, 0xB5, 0x76, 0xBE, 0x70, 0x25, 0xD8, 0x42}};
bool is_smbus_guid(const GUID* g) { return g && std::memcmp(g, &kSmbusGuid, sizeof(GUID)) == 0; }

void eeprom_flush(size_t off, size_t n);
void hw_load() {
    if (g_hw) return;
    g_hw = new rg_amhw::HwRegs();
    const std::string dip = env("RG_AMHW_DIPSW", "");   // "70,00": de twee dip-bytes in hex
    if (!dip.empty()) {
        unsigned a = 0x70, b = 0;
        if (std::sscanf(dip.c_str(), "%x,%x", &a, &b) >= 1) g_hw->set_dip(static_cast<uint8_t>(a), static_cast<uint8_t>(b));
    }
    if (FILE* f = std::fopen(g_eeprom_path, "rb")) {
        const size_t n = std::fread(g_hw->eeprom.data(), 1, g_hw->eeprom.size(), f);
        std::fclose(f);
        rg_log("smbus: EEPROM %s ingelezen (%lu bytes)", g_eeprom_path, static_cast<unsigned long>(n));
    } else {
        rg_log("smbus: EEPROM %s bestaat nog niet; leeg (0xFF)", g_eeprom_path);
    }
    // AM_SYSDATA_STATIC staat in de EEPROM (amhw_sysdata.h). Een lege EEPROM heeft regio 0: LGS en SDR melden dan "Error 0903 Wrong Region" en GG valt terug op
    // een andere taal. Zet een geldig record met de regio van de keychip als het ontbreekt, kapot is of regio 0 heeft.
    if (g_region && rg_amhw::seed_static_sysdata(g_hw->eeprom, g_region)) {
        rg_log("smbus: AM_SYSDATA_STATIC in de EEPROM ontbrak of was kapot; geldig record met regio %02X op offset 0 en 0x200 gezet", g_region);
        eeprom_flush(0, g_hw->eeprom.size());
    }
    rg_log("smbus: dipschakelaars %02X,%02X; Super I/O-chip-id A230 (type 1)", g_hw->i2c[0x20][4], g_hw->i2c[0x20][5]);
}
void eeprom_flush(size_t off, size_t n) {
    FILE* f = std::fopen(g_eeprom_path, "r+b");
    if (!f) f = std::fopen(g_eeprom_path, "w+b");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    if (static_cast<size_t>(std::ftell(f)) < g_hw->eeprom.size()) {
        std::fseek(f, 0, SEEK_SET);
        std::fwrite(g_hw->eeprom.data(), 1, g_hw->eeprom.size(), f);
    } else {
        std::fseek(f, static_cast<long>(off), SEEK_SET);
        std::fwrite(g_hw->eeprom.data() + off, 1, n, f);
    }
    std::fclose(f);
}

HANDLE open_named_a(const char* n) {
    if (is_dev_path(n, "\\\\.\\columba")) return make_handle(Kind::Columba, "columba");
    if (is_dev_path(n, "\\\\.\\mxsram")) { sram_load(); return make_handle(Kind::Sram, "mxsram"); }
    if (is_dev_path(n, "\\\\.\\mxjvs")) return open_jvs();
    if (is_dev_path(n, "\\\\.\\mxsuperio")) { hw_load(); return make_handle(Kind::Superio, "mxsuperio"); }
    if (is_dev_path(n, kSmbusPath)) { hw_load(); return make_handle(Kind::Smbus, "smbus"); }
    return nullptr;
}

HANDLE WINAPI rg_CreateFileW(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (name && name[0] == L'\\' && name[1] == L'\\' && (name[2] == L'.' || name[2] == L'?')) {
        char a[260];
        WideCharToMultiByte(CP_ACP, 0, name, -1, a, sizeof a, nullptr, nullptr);
        if (HANDLE h = open_named_a(a)) return h;
        HANDLE r = g_create_w(name, acc, share, sa, disp, flags, tmpl);
        const DWORD e = GetLastError();
        static LONG n = 0;
        if (InterlockedIncrement(&n) <= 60) rg_log("CreateFileW(%s) niet nagebootst -> %p (fout %lu)", a, r, static_cast<unsigned long>(e));
        SetLastError(e);
        return r;
    }
    if (name) {
        char a[300];
        WideCharToMultiByte(CP_ACP, 0, name, -1, a, sizeof a, nullptr, nullptr);
        if (is_debugout_write(a, acc)) {
            const std::string fb = g_dir + "\\debugout-spel.txt";
            static LONG nd = 0;
            if (InterlockedIncrement(&nd) <= 2) rg_log("debugout.txt (schrijven) omgeleid naar %s", fb.c_str());
            return g_create_a(fb.c_str(), acc, share, sa, disp, flags, tmpl);
        }
    }
    if (name && (acc & GENERIC_WRITE) == 0 && (disp == OPEN_EXISTING)) {
        char a[300];
        WideCharToMultiByte(CP_ACP, 0, name, -1, a, sizeof a, nullptr, nullptr);
        if (is_table_name(a) && GetFileAttributesA(a) == INVALID_FILE_ATTRIBUTES) {
            const std::string fb = table_fallback(a);
            return g_create_a(fb.c_str(), acc, share, sa, disp, flags, tmpl);
        }
    }
    return g_create_w(name, acc, share, sa, disp, flags, tmpl);
}
HANDLE WINAPI rg_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (name && name[0] == '\\' && name[1] == '\\' && (name[2] == '.' || name[2] == '?')) {
        if (HANDLE h = open_named_a(name)) return h;
        HANDLE r = g_create_a(name, acc, share, sa, disp, flags, tmpl);
        const DWORD e = GetLastError();
        static LONG n = 0;
        if (InterlockedIncrement(&n) <= 60) rg_log("CreateFileA(%s) niet nagebootst -> %p (fout %lu)", name, r, static_cast<unsigned long>(e));
        SetLastError(e);
        return r;
    }
    if (is_debugout_write(name, acc)) {
        const std::string fb = g_dir + "\\debugout-spel.txt";
        static LONG nd = 0;
        if (InterlockedIncrement(&nd) <= 2) rg_log("debugout.txt (schrijven) omgeleid naar %s", fb.c_str());
        return g_create_a(fb.c_str(), acc, share, sa, disp, flags, tmpl);
    }
    if (name && (acc & GENERIC_WRITE) == 0 && disp == OPEN_EXISTING && is_table_name(name) && GetFileAttributesA(name) == INVALID_FILE_ATTRIBUTES) {
        const std::string fb = table_fallback(name);
        return g_create_a(fb.c_str(), acc, share, sa, disp, flags, tmpl);
    }
    return g_create_a(name, acc, share, sa, disp, flags, tmpl);
}

// De Ring-games (teaArcade2) schrijven debugout.txt met fopen uit MSVCR90.dll (dynamische CRT): dat roept CreateFileW aan via de EIGEN importtabel van msvcr90, niet
// die van de game. Daarom haken we fopen/fopen_s/_wfopen/_fsopen in de importtabel van de game en CreateFileW/A in die van de CRT-DLL's (zie setup()).
using FopenFn = void*(__cdecl*)(const char*, const char*);
using FopenSFn = int(__cdecl*)(void**, const char*, const char*);
using WfopenFn = void*(__cdecl*)(const wchar_t*, const wchar_t*);
using FsopenFn = void*(__cdecl*)(const char*, const char*, int);
FopenFn g_fopen = nullptr;
FopenSFn g_fopen_s = nullptr;
WfopenFn g_wfopen = nullptr;
FsopenFn g_fsopen = nullptr;
bool mode_writes(const char* m) { return m && (std::strchr(m, 'w') || std::strchr(m, 'a') || std::strchr(m, '+')); }
std::string debugout_target(const char* path, const char* mode) {
    if (!mode_writes(mode) || !is_debugout_write(path, GENERIC_WRITE)) return {};
    static LONG nd = 0;
    const std::string fb = g_dir + "\\debugout-spel.txt";
    if (InterlockedIncrement(&nd) <= 2) rg_log("debugout.txt (fopen, schrijven) omgeleid naar %s", fb.c_str());
    return fb;
}
void* __cdecl rg_fopen(const char* path, const char* mode) {
    const std::string t = debugout_target(path, mode);
    return g_fopen(t.empty() ? path : t.c_str(), mode);
}
int __cdecl rg_fopen_s(void** f, const char* path, const char* mode) {
    const std::string t = debugout_target(path, mode);
    return g_fopen_s(f, t.empty() ? path : t.c_str(), mode);
}
void* __cdecl rg_fsopen(const char* path, const char* mode, int share) {
    const std::string t = debugout_target(path, mode);
    return g_fsopen(t.empty() ? path : t.c_str(), mode, share);
}
void* __cdecl rg_wfopen(const wchar_t* path, const wchar_t* mode) {
    char a[300] = {}, m[16] = {};
    WideCharToMultiByte(CP_ACP, 0, path, -1, a, sizeof a, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, mode, -1, m, sizeof m, nullptr, nullptr);
    const std::string t = debugout_target(a, m);
    if (t.empty()) return g_wfopen(path, mode);
    wchar_t w[300] = {};
    MultiByteToWideChar(CP_ACP, 0, t.c_str(), -1, w, 300);
    return g_wfopen(w, mode);
}

// De JVS-transactie van het stuurprogramma (0x9C402000): de bytes van de bovenlaag naar de lijn, dan het antwoordkader terug, en de OVERLAPPED
// meteen klaar (InternalHigh = aantal bytes, gebeurtenis gezet): Windows meldt een direct voltooide overlapped-opdracht ook met TRUE.
BOOL jvs_transfer(HANDLE h, LPVOID in, DWORD in_n, LPVOID out, DWORD out_n, LPDWORD ret, LPOVERLAPPED ov) {
    static LONG count = 0, timeouts = 0;
    const LONG nr = InterlockedIncrement(&count);
    uint8_t acc[1024];
    size_t have = 0, got = 0;
    DWORD wrote = 0;
    EnterCriticalSection(&g_jvs_cs);
    BOOL ok = in && in_n && WriteFile(h, in, in_n, &wrote, nullptr);
    const DWORD werr = ok ? 0 : GetLastError();
    const bool reply = ok && rg_amhw::expects_reply(static_cast<const uint8_t*>(in), in_n);
    if (reply) {
        const DWORD deadline = GetTickCount() + kJvsReplyMs;
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) break;
            if (avail) {
                DWORD r = 0;
                if (!ReadFile(h, acc + have, static_cast<DWORD>(std::min<size_t>(avail, sizeof acc - have)), &r, nullptr) || !r) break;
                have += r;
                size_t skip = 0;
                const size_t fl = rg_amhw::frame_length(acc, have, &skip);
                if (fl) { std::memmove(acc, acc + skip, fl); have = fl; got = fl; break; }
                if (have >= sizeof acc) break;
            } else if (static_cast<LONG>(GetTickCount() - deadline) >= 0) {
                break;
            } else {
                Sleep(1);
            }
        }
    }
    LeaveCriticalSection(&g_jvs_cs);
    if (got > out_n) got = out_n;
    if (got && out) std::memcpy(out, acc, got);
    auto hex = [](const uint8_t* b, size_t n) {
        std::string t;
        char c[4];
        for (size_t i = 0; i < n && i < 24; ++i) { std::snprintf(c, sizeof c, "%02X", b[i]); t += c; if (i + 1 < n) t += ' '; }
        if (n > 24) t += "...";
        return t;
    };
    if (!ok) {
        rg_log("mxjvs: schrijven mislukt (fout %lu) bij %lu bytes", static_cast<unsigned long>(werr), static_cast<unsigned long>(in_n));
        SetLastError(werr ? werr : ERROR_BROKEN_PIPE);
        return FALSE;
    }
    if (reply && !got) {
        const LONG t = InterlockedIncrement(&timeouts);
        if (t <= 10 || t % 200 == 0)
            rg_log("mxjvs: geen antwoord binnen %lu ms op [%s] (%ld keer)", static_cast<unsigned long>(kJvsReplyMs), hex(static_cast<const uint8_t*>(in), in_n).c_str(), static_cast<long>(t));
    }
    if (nr <= 6 || nr % 2000 == 0)
        rg_log("mxjvs: #%ld [%s] -> %s [%s]", static_cast<long>(nr), hex(static_cast<const uint8_t*>(in), in_n).c_str(),
               reply ? "antwoord" : "geen antwoord verwacht", hex(acc, got).c_str());
    if (ret) *ret = static_cast<DWORD>(got);
    if (ov) {
        ov->Internal = 0;
        ov->InternalHigh = static_cast<ULONG_PTR>(got);
        if (ov->hEvent) SetEvent(ov->hEvent);
    }
    SetLastError(0);
    return TRUE;
}

BOOL WINAPI rg_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_n, LPVOID out, DWORD out_n, LPDWORD ret, LPOVERLAPPED ov) {
    Dev d;
    if (!lookup(h, d)) return g_ioctl(h, code, in, in_n, out, out_n, ret, ov);
    if (d.kind == Kind::Jvs) {
        if (code == rg_amhw::kIoctlJvsTransfer) return jvs_transfer(h, in, in_n, out, out_n, ret, ov);
        rg_log("mxjvs: onbekende DeviceIoControl 0x%08lX (in %lu, uit %lu); FALSE", static_cast<unsigned long>(code), static_cast<unsigned long>(in_n), static_cast<unsigned long>(out_n));
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    if (ret) *ret = 0;
    if (d.kind == Kind::Columba && code == kIoctlPhysRead) {
        // De uitvoerlengte is de lengte van de buffer van de aanroeper: LGI vraagt 0x10000 per keer, de nieuwere AM Library (LGS, LGI3D, ...) leest de
        // BIOS-ruimte in stukken van 0x8010 (zodat "_DMI_" over een grens heen gevonden wordt) en daarna de tabel met 0x10000. De game eist dat
        // lpBytesReturned gelijk is aan wat hij vroeg.
        if (!in || in_n < 16 || !out || out_n == 0 || out_n > 0x100000) {
            rg_log("columba: 0x%08lX met onbruikbare buffers (in %lu, uit %lu)", static_cast<unsigned long>(code), static_cast<unsigned long>(in_n), static_cast<unsigned long>(out_n));
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        uint32_t req[4];
        std::memcpy(req, in, sizeof req);
        rg_amhw::phys_read(g_platform, req[0], static_cast<uint8_t*>(out), out_n);
        if (ret) *ret = out_n;
        static LONG n = 0;
        const LONG nr = InterlockedIncrement(&n);
        if (nr <= 4 || (req[0] == rg_amhw::kBiosBase && nr <= 12) || req[0] == rg_amhw::kTableBase)
            rg_log("columba: fysiek geheugen 0x%08lX (lengte 0x%lX, buffer 0x%lX) gelezen -> %s", static_cast<unsigned long>(req[0]), static_cast<unsigned long>(req[3]),
                   static_cast<unsigned long>(out_n), req[0] == rg_amhw::kBiosBase ? "BIOS-ruimte met _DMI_" : req[0] == rg_amhw::kTableBase ? "DMI-tabel" : "nullen");
        return TRUE;
    }
    if ((d.kind == Kind::Smbus || d.kind == Kind::Superio) && code != kIoctlVersion && code != 0x9C406008) {
        // Registerbestanden (amhw_smbus.h). Een opdracht met een OVERLAPPED wordt direct voltooid: gebeurtenis gezet, TRUE.
        bool ok = false;
        size_t eoff = 0, elen = 0;
        EnterCriticalSection(&g_cs);
        if (d.kind == Kind::Smbus && code == rg_amhw::kIoctlSmbusRequest && in_n >= rg_amhw::kSmbusPacket && in && out && out_n >= rg_amhw::kSmbusPacket) {
            if (out != in) std::memcpy(out, in, rg_amhw::kSmbusPacket);
            ok = g_hw->smbus_request(static_cast<uint8_t*>(out));
            if (ok && ret) *ret = rg_amhw::kSmbusPacket;
        } else if (d.kind == Kind::Smbus && code == rg_amhw::kIoctlSmbusBlock && in_n >= rg_amhw::kEepromPacket && in && out && out_n >= rg_amhw::kEepromPacket) {
            if (out != in) std::memcpy(out, in, rg_amhw::kEepromPacket);
            ok = g_hw->eeprom_block(static_cast<uint8_t*>(out), &eoff, &elen);
            if (ok && ret) *ret = rg_amhw::kEepromPacket;
        } else if (d.kind == Kind::Superio && code == rg_amhw::kIoctlSmbusRequest && in_n >= 4 && in && out && out_n >= 4) {
            if (out != in) std::memcpy(out, in, 4);
            ok = g_hw->superio_read(static_cast<uint8_t*>(out));
            if (ok && ret) *ret = 4;
        } else if (d.kind == Kind::Superio && code == rg_amhw::kIoctlSmbusBlock && in_n >= 3 && in && out && out_n >= 3) {
            if (out != in) std::memcpy(out, in, 3);
            ok = g_hw->lpc_read(static_cast<uint8_t*>(out));
            if (ok && ret) *ret = 3;
        } else if (d.kind == Kind::Superio && code == rg_amhw::kIoctlLpcWrite && in_n >= 3 && in) {
            ok = g_hw->lpc_write(static_cast<const uint8_t*>(in));
            if (ok && ret) *ret = 0;
        }
        LeaveCriticalSection(&g_cs);
        if (ok && elen) eeprom_flush(eoff, elen);
        static LONG nlog = 0;
        if (InterlockedIncrement(&nlog) <= 40 || !ok) {
            const uint8_t* b = static_cast<const uint8_t*>(in);
            rg_log("%s: 0x%08lX (in %lu, uit %lu) [%02X %02X %02X %02X %02X %02X %02X] -> %s", kind_name(d.kind), static_cast<unsigned long>(code),
                   static_cast<unsigned long>(in_n), static_cast<unsigned long>(out_n), b ? b[0] : 0, b ? b[1] : 0, b ? b[2] : 0, b ? b[3] : 0, b ? b[4] : 0, b ? b[5] : 0,
                   b ? b[6] : 0, ok ? "goed" : "niet nagebootst");
        }
        if (!ok) { SetLastError(ERROR_INVALID_FUNCTION); return FALSE; }
        if (ov) {
            ov->Internal = 0;
            ov->InternalHigh = ret ? *ret : 0;
            if (ov->hEvent) SetEvent(ov->hEvent);
        }
        SetLastError(0);
        return TRUE;
    }
    if ((code == kIoctlVersion || code == 0x9C406008) && out && out_n >= 4) {
        *static_cast<uint32_t*>(out) = 1;
        if (ret) *ret = 4;
        return TRUE;
    }
    if (d.kind == Kind::Sram && code == 0x9C406004 && out && out_n >= 4) {   // sectorgrootte (de nieuwere AM Library; faalt hij, dan geldt die uit de geometrie)
        *static_cast<uint32_t*>(out) = 0x200;
        if (ret) *ret = 4;
        return TRUE;
    }
    if (d.kind == Kind::Sram && code == kIoctlSramGeometry && out && out_n >= 0x18) {
        // O+0x0/0x4: deler van het aantal sectoren (64 bit), O+0xC x O+0x10 = totaal in bytes, O+0x14 = sectorgrootte (hoogstens 0x800).
        uint32_t g[6] = {1, 0, 0, 0x1000, 0, 0x200};
        g[4] = static_cast<uint32_t>(g_sram.size() / 0x1000);   // O+0x10: 0x1000 x (size/0x1000) = size
        std::memcpy(out, g, sizeof g);
        if (ret) *ret = 0x18;
        rg_log("mxsram: geometrie %lu bytes, sectoren van 0x200", static_cast<unsigned long>(g_sram.size()));
        return TRUE;
    }
    rg_log("%s: onbekende DeviceIoControl 0x%08lX (in %lu, uit %lu); FALSE", kind_name(d.kind), static_cast<unsigned long>(code),
           static_cast<unsigned long>(in_n), static_cast<unsigned long>(out_n));
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

BOOL WINAPI rg_CloseHandle(HANDLE h) {
    Dev d;
    if (lookup(h, d)) {
        EnterCriticalSection(&g_cs);
        g_devs.erase(h);
        LeaveCriticalSection(&g_cs);
        rg_log("%s: gesloten", kind_name(d.kind));
    }
    return g_close(h);
}

DWORD WINAPI rg_SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method) {
    Dev d;
    if (!lookup(h, d) || d.kind != Kind::Sram) return g_seek(h, lo, hi, method);
    long long off = (static_cast<long long>(hi ? *hi : 0) << 32) | static_cast<unsigned long>(lo);
    EnterCriticalSection(&g_cs);
    Dev& dd = g_devs[h];
    if (method == FILE_BEGIN) dd.pos = off;
    else if (method == FILE_CURRENT) dd.pos += off;
    else dd.pos = static_cast<long long>(g_sram.size()) + off;
    const long long p = dd.pos;
    LeaveCriticalSection(&g_cs);
    if (hi) *hi = static_cast<LONG>(p >> 32);
    return static_cast<DWORD>(p & 0xFFFFFFFF);
}

BOOL sram_io(HANDLE h, LPVOID buf, DWORD n, LPDWORD done, bool write) {
    EnterCriticalSection(&g_cs);
    Dev& dd = g_devs[h];
    long long pos = dd.pos;
    DWORD got = 0;
    if (pos >= 0 && static_cast<size_t>(pos) < g_sram.size()) {
        got = static_cast<DWORD>(std::min<size_t>(n, g_sram.size() - static_cast<size_t>(pos)));
        if (write) std::memcpy(g_sram.data() + pos, buf, got);
        else std::memcpy(buf, g_sram.data() + pos, got);
    }
    dd.pos = pos + got;
    LeaveCriticalSection(&g_cs);
    if (write && got) sram_flush(pos, got);
    if (done) *done = got;
    return TRUE;
}
BOOL WINAPI rg_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD done, LPOVERLAPPED ov) {
    Dev d;
    if (!lookup(h, d) || d.kind != Kind::Sram) return g_read(h, buf, n, done, ov);
    return sram_io(h, buf, n, done, false);
}
BOOL WINAPI rg_WriteFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD done, LPOVERLAPPED ov) {
    Dev d;
    if (!lookup(h, d) || d.kind != Kind::Sram) return g_write(h, buf, n, done, ov);
    return sram_io(h, buf, n, done, true);
}

bool is_jvs(HANDLE h) { Dev d; return lookup(h, d) && d.kind == Kind::Jvs; }
BOOL WINAPI rg_GetCommState(HANDLE h, LPDCB d) { return is_jvs(h) ? TRUE : g_get_comm_state(h, d); }
BOOL WINAPI rg_SetCommState(HANDLE h, LPDCB d) { return is_jvs(h) ? TRUE : g_set_comm_state(h, d); }
BOOL WINAPI rg_GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_jvs(h) ? TRUE : g_get_comm_timeouts(h, t); }
BOOL WINAPI rg_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_jvs(h) ? TRUE : g_set_comm_timeouts(h, t); }
BOOL WINAPI rg_SetupComm(HANDLE h, DWORD i, DWORD o) { return is_jvs(h) ? TRUE : g_setup_comm(h, i, o); }
BOOL WINAPI rg_PurgeComm(HANDLE h, DWORD f) { return is_jvs(h) ? TRUE : g_purge_comm(h, f); }
// De sense-lijn: zolang het bord geen adres heeft staat alleen CTS (0x10) aan, na de adrestoekenning CTS en DSR (0x30); dezelfde waarden als
// OpenParrot's amJvs.cpp voor een niet-Type-X-game. Byte 0 van TeknoParrot_JvsState wordt door de brug gespiegeld.
BOOL WINAPI rg_GetCommModemStatus(HANDLE h, LPDWORD status) {
    if (!is_jvs(h)) return g_modem_status(h, status);
    if (!g_jvs_state) {
        g_jvs_map = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (g_jvs_map) g_jvs_state = static_cast<const volatile uint8_t*>(MapViewOfFile(g_jvs_map, FILE_MAP_READ, 0, 0, 64));
    }
    const bool addressed = g_jvs_state && g_jvs_state[0] != 0;
    if (status) *status = addressed ? 0x30u : 0x10u;
    return TRUE;
}

// De SMBus-driver (dipschakelaars, EEPROM, hardwaremonitor) zoekt zijn apparaat via SetupDi: wij geven een eigen lijst met een apparaat en het pad kSmbusPath.
HDEVINFO const kFakeDevInfo = reinterpret_cast<HDEVINFO>(static_cast<uintptr_t>(0x52474D48));
using SetupEnumFn = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
using SetupDetailFn = BOOL(WINAPI*)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_A, DWORD, PDWORD, PSP_DEVINFO_DATA);
using SetupDestroyFn = BOOL(WINAPI*)(HDEVINFO);
SetupEnumFn g_setup_enum = nullptr;
SetupDetailFn g_setup_detail = nullptr;
SetupDestroyFn g_setup_destroy = nullptr;
BOOL WINAPI rg_SetupDiEnumDeviceInterfaces(HDEVINFO set, PSP_DEVINFO_DATA dd, const GUID* g, DWORD idx, PSP_DEVICE_INTERFACE_DATA data) {
    if (set != kFakeDevInfo) return g_setup_enum(set, dd, g, idx, data);
    if (idx != 0 || !data) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
    data->InterfaceClassGuid = kSmbusGuid;
    data->Flags = SPINT_ACTIVE;
    data->Reserved = 0;
    return TRUE;
}
BOOL WINAPI rg_SetupDiGetDeviceInterfaceDetailA(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA data, PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail, DWORD size, PDWORD req,
                                                PSP_DEVINFO_DATA dd) {
    if (set != kFakeDevInfo) return g_setup_detail(set, data, detail, size, req, dd);
    const DWORD need = static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_A, DevicePath) + std::strlen(kSmbusPath) + 1);
    if (req) *req = need;
    if (!detail || size < need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    std::strcpy(detail->DevicePath, kSmbusPath);
    return TRUE;
}
BOOL WINAPI rg_SetupDiDestroyDeviceInfoList(HDEVINFO set) { return set == kFakeDevInfo ? TRUE : g_setup_destroy(set); }

HDEVINFO WINAPI rg_SetupDiGetClassDevsA(const GUID* g, PCSTR e, HWND w, DWORD f) {
    if (is_smbus_guid(g)) {
        static LONG nn = 0;
        if (InterlockedIncrement(&nn) <= 4) rg_log("SetupDiGetClassDevsA(SMBus-GUID, flags 0x%lX) -> nagebootst apparaatlijst met %s", static_cast<unsigned long>(f), kSmbusPath);
        return kFakeDevInfo;
    }
    HDEVINFO r = g_setup_classdevs(g, e, w, f);
    const DWORD err = GetLastError();
    static LONG n = 0;
    if (InterlockedIncrement(&n) <= 20 && g)
        rg_log("SetupDiGetClassDevsA({%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}, flags 0x%lX) -> %p (niet de SMBus-GUID: doorgegeven)",
               g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7],
               static_cast<unsigned long>(f), r);
    SetLastError(err);
    return r;
}

// --- de importtabel van het spel (gelijk aan rg-mfcam) ---------------------------------------------------------------------------------
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
        for (size_t i = 0; iat[i].u1.Function; ++i) {
            bool hit = false;
            if (names && !(names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
                hit = std::strcmp(reinterpret_cast<const char*>(ibn->Name), func) == 0;
            } else if (!names) {   // gedumpte exe (Transformers): geen namentabel; vergelijk met het adres dat Wine al heeft ingevuld
                HMODULE m = GetModuleHandleA(dll);
                hit = m && reinterpret_cast<void*>(iat[i].u1.Function) == reinterpret_cast<void*>(GetProcAddress(m, func));
            }
            if (!hit) continue;
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

template <typename Fn>
void hook(const char* dll, const char* func, Fn repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), dll, func, reinterpret_cast<void*>(repl));
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("importtabel: %s!%s %s", dll, func, old ? "omgezet" : "niet in het spel");
}

std::string env(const char* k, const char* def) {
    char b[260];
    const DWORD n = GetEnvironmentVariableA(k, b, sizeof b);
    return n > 0 && n < sizeof b ? std::string(b) : std::string(def);
}

// Een gedumpte exe (Transformers: TF_Gun_R_Ring_dumped.exe) heeft de sectiekenmerken van het dumpprogramma: .rdata staat op lezen+SCHRIJVEN (0xC0000040).
// De Visual C-opstartcode (_cinit) roept _FPinit alleen aan als _IsNonwritableInCurrentImage zegt dat het pointerveld in een NIET-schrijfbare sectie
// staat; anders blijft de drijvende-komma-ondersteuning ongeladen en meldt de eerste printf met %f "R6002 - floating point support not loaded".
// De kenmerken in de kop in het geheugen terugzetten naar alleen-lezen (de paginabeveiliging verandert er niet door) herstelt dat voor de CRT start.
void fix_rdata_writable() {
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sec[i].Name, ".rdata\0\0", 8) != 0 || !(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        DWORD prot = 0;
        if (!VirtualProtect(&sec[i].Characteristics, sizeof(DWORD), PAGE_READWRITE, &prot)) { rg_log("kop: .rdata is schrijfbaar maar de kop is niet aan te passen"); return; }
        const DWORD old = sec[i].Characteristics;
        sec[i].Characteristics &= ~static_cast<DWORD>(IMAGE_SCN_MEM_WRITE);
        VirtualProtect(&sec[i].Characteristics, sizeof(DWORD), prot, &prot);
        rg_log("kop: .rdata stond op schrijfbaar (0x%08lX, een gedumpte exe); nu 0x%08lX zodat _FPinit (R6002) draait", static_cast<unsigned long>(old),
               static_cast<unsigned long>(sec[i].Characteristics));
    }
}

void setup() {
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&g_jvs_cs);
    char self[MAX_PATH] = {};
    GetModuleFileNameA(g_self, self, MAX_PATH);
    std::string dir(self);
    const size_t slash = dir.find_last_of("\\/");
    dir = slash == std::string::npos ? std::string(".") : dir.substr(0, slash);
    std::snprintf(g_log_path, sizeof g_log_path, "%s\\rg-amhw.log", dir.c_str());
    g_table_dir = dir + "\\rg-amhw-tables";
    g_dir = dir;
    std::snprintf(g_jvs_pipe, sizeof g_jvs_pipe, "%s", env("RG_AMHW_JVS_PIPE", g_jvs_pipe).c_str());
    g_platform.oem = env("RG_AMHW_OEM", "AAM");
    g_platform.vendor = env("RG_AMHW_VENDOR", "Supermicro");
    const std::string sdir = env("RG_AMHW_DIR", dir.c_str());
    std::snprintf(g_sram_path, sizeof g_sram_path, "%s\\mxsram.bin", sdir.c_str());
    std::snprintf(g_eeprom_path, sizeof g_eeprom_path, "%s\\mxeeprom.bin", sdir.c_str());
    {
        unsigned r = 0;
        if (std::sscanf(env("RG_AMHW_REGION", "").c_str(), "%x", &r) == 1 && r <= 0xFF) g_region = static_cast<uint8_t>(r);
    }
    int kb = std::atoi(env("RG_AMHW_SRAM_KB", "2048").c_str());
    if (kb < 256 || kb > 8192) kb = 2048;
    g_sram.assign(static_cast<size_t>(kb) * 1024, 0);
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    rg_log("rg-amhw geladen in %s: platform %s, fabrikant %s, SRAM %d KB in %s", exe, g_platform.oem.c_str(), g_platform.vendor.c_str(), kb, g_sram_path);
    if (env("RG_AMHW_OFF", "") == "1") { rg_log("RG_AMHW_OFF=1: niets omgezet"); return; }
    fix_rdata_writable();
    hook("KERNEL32.dll", "CreateFileW", rg_CreateFileW, g_create_w);
    hook("KERNEL32.dll", "CreateFileA", rg_CreateFileA, g_create_a);
    // De CRT: de game importeert fopen e.d. uit MSVCR90.dll (of een andere versie); haak ze in de game en haak CreateFileW/A in de importtabel van de CRT-DLL zelf.
    for (const char* crt : {"MSVCR90.dll", "MSVCR100.dll", "MSVCR80.dll", "MSVCR110.dll", "MSVCRT.dll", "api-ms-win-crt-stdio-l1-1-0.dll", "ucrtbase.dll"}) {
        if (void* o = patch_iat(GetModuleHandleW(nullptr), crt, "fopen", reinterpret_cast<void*>(rg_fopen))) { g_fopen = reinterpret_cast<FopenFn>(o); rg_log("importtabel: %s!fopen omgezet", crt); }
        if (void* o = patch_iat(GetModuleHandleW(nullptr), crt, "fopen_s", reinterpret_cast<void*>(rg_fopen_s))) { g_fopen_s = reinterpret_cast<FopenSFn>(o); rg_log("importtabel: %s!fopen_s omgezet", crt); }
        if (void* o = patch_iat(GetModuleHandleW(nullptr), crt, "_fsopen", reinterpret_cast<void*>(rg_fsopen))) { g_fsopen = reinterpret_cast<FsopenFn>(o); rg_log("importtabel: %s!_fsopen omgezet", crt); }
        if (void* o = patch_iat(GetModuleHandleW(nullptr), crt, "_wfopen", reinterpret_cast<void*>(rg_wfopen))) { g_wfopen = reinterpret_cast<WfopenFn>(o); rg_log("importtabel: %s!_wfopen omgezet", crt); }
        if (HMODULE m = GetModuleHandleA(crt)) {
            const bool w = patch_iat(m, "KERNEL32.dll", "CreateFileW", reinterpret_cast<void*>(rg_CreateFileW)) != nullptr;
            const bool a = patch_iat(m, "KERNEL32.dll", "CreateFileA", reinterpret_cast<void*>(rg_CreateFileA)) != nullptr;
            rg_log("importtabel van %s: CreateFileW %s, CreateFileA %s", crt, w ? "omgezet" : "niet gevonden", a ? "omgezet" : "niet gevonden");
        }
    }
    hook("KERNEL32.dll", "DeviceIoControl", rg_DeviceIoControl, g_ioctl);
    hook("KERNEL32.dll", "CloseHandle", rg_CloseHandle, g_close);
    hook("KERNEL32.dll", "SetFilePointer", rg_SetFilePointer, g_seek);
    hook("KERNEL32.dll", "ReadFile", rg_ReadFile, g_read);
    hook("KERNEL32.dll", "WriteFile", rg_WriteFile, g_write);
    hook("KERNEL32.dll", "GetCommState", rg_GetCommState, g_get_comm_state);
    hook("KERNEL32.dll", "SetCommState", rg_SetCommState, g_set_comm_state);
    hook("KERNEL32.dll", "GetCommTimeouts", rg_GetCommTimeouts, g_get_comm_timeouts);
    hook("KERNEL32.dll", "SetCommTimeouts", rg_SetCommTimeouts, g_set_comm_timeouts);
    hook("KERNEL32.dll", "SetupComm", rg_SetupComm, g_setup_comm);
    hook("KERNEL32.dll", "PurgeComm", rg_PurgeComm, g_purge_comm);
    hook("KERNEL32.dll", "GetCommModemStatus", rg_GetCommModemStatus, g_modem_status);
    HMODULE setupapi = LoadLibraryA("setupapi.dll");
    g_setup_classdevs = reinterpret_cast<SetupGetClassDevsAFn>(reinterpret_cast<void*>(GetProcAddress(setupapi, "SetupDiGetClassDevsA")));
    g_setup_enum = reinterpret_cast<SetupEnumFn>(reinterpret_cast<void*>(GetProcAddress(setupapi, "SetupDiEnumDeviceInterfaces")));
    g_setup_detail = reinterpret_cast<SetupDetailFn>(reinterpret_cast<void*>(GetProcAddress(setupapi, "SetupDiGetDeviceInterfaceDetailA")));
    g_setup_destroy = reinterpret_cast<SetupDestroyFn>(reinterpret_cast<void*>(GetProcAddress(setupapi, "SetupDiDestroyDeviceInfoList")));
    if (g_setup_classdevs) hook("SETUPAPI.dll", "SetupDiGetClassDevsA", rg_SetupDiGetClassDevsA, g_setup_classdevs);
    if (g_setup_enum) hook("SETUPAPI.dll", "SetupDiEnumDeviceInterfaces", rg_SetupDiEnumDeviceInterfaces, g_setup_enum);
    if (g_setup_detail) hook("SETUPAPI.dll", "SetupDiGetDeviceInterfaceDetailA", rg_SetupDiGetDeviceInterfaceDetailA, g_setup_detail);
    if (g_setup_destroy) hook("SETUPAPI.dll", "SetupDiDestroyDeviceInfoList", rg_SetupDiDestroyDeviceInfoList, g_setup_destroy);
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        setup();
    }
    return TRUE;
}
