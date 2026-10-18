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
// standaard Supermicro), RG_AMHW_DIR (map voor mxsram.bin; standaard de map van de DLL), RG_AMHW_SRAM_KB (standaard 2048), RG_AMHW_OFF=1,
// RG_AMHW_DIPIN ("FE,FF": invoerpoorten 0 en 1 van de PCA9535, actief laag; LGI: bit 0 aan = standaardkast zonder passagiersstoel),
// RG_AMHW_WINDOW="X,Y,W,H[,popup]" (het eerste hoofdvenster van het spel krijgt die plek/maat, zie "venster"),
// RG_AMHW_AMLOG=1 en RG_AMHW_PEEK="0xADRES,..." (SDR: logboek van de AM Library en game-geheugen in de bewaker),
// RG_AMHW_WATCHDOG=N (na N en 2N s de plek van elke thread in het logboek),
// RG_AMHW_NOREPARSE=1 (een gelinkte map meldt GetFileAttributes als 0x10 in plaats van 0x410; terugval voor SDR, zie "de werkmap van het spel").
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <tlhelp32.h>

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
    const std::string dipin = env("RG_AMHW_DIPIN", "");   // "FF,FF": de invoerpoorten 0 en 1 (dipschakelaars en drukknoppen; actief laag)
    if (!dipin.empty()) {
        unsigned a = 0xFF, b = 0xFF;
        if (std::sscanf(dipin.c_str(), "%x,%x", &a, &b) >= 1) { g_hw->i2c[0x20][0] = static_cast<uint8_t>(a); g_hw->i2c[0x20][1] = static_cast<uint8_t>(b); }
    }
    if (FILE* f = std::fopen(g_eeprom_path, "rb")) {
        const size_t n = std::fread(g_hw->eeprom.data(), 1, g_hw->eeprom.size(), f);
        std::fclose(f);
        rg_log("smbus: EEPROM %s ingelezen (%lu bytes)", g_eeprom_path, static_cast<unsigned long>(n));
    } else {
        rg_log("smbus: EEPROM %s bestaat nog niet; leeg (0xFF)", g_eeprom_path);
    }
    // AM_SYSDATA_STATIC staat in de EEPROM (amhw_sysdata.h). Een lege EEPROM heeft regio 0: LGS en SDR melden dan "Error 0903 Wrong Region" en GG valt terug op
    // een andere taal. Zet een geldig record met de regio van de optie (PcbRegion, anders de keychip) als het ontbreekt, kapot is of een andere regio heeft.
    if (g_region && rg_amhw::seed_static_sysdata(g_hw->eeprom, g_region)) {
        rg_log("smbus: AM_SYSDATA_STATIC in de EEPROM ontbrak, was kapot of had een andere regio; geldig record met regio %02X op offset 0 en 0x200 gezet", g_region);
        eeprom_flush(0, g_hw->eeprom.size());
    }
    rg_log("smbus: dipschakelaars %02X,%02X; invoerpoorten %02X,%02X (FF = niets ingedrukt); Super I/O-chip-id A230 (type 1)", g_hw->i2c[0x20][4], g_hw->i2c[0x20][5], g_hw->i2c[0x20][0], g_hw->i2c[0x20][1]);
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

// Welke bestanden kan de game niet openen? LGI en LGI3D sluiten zich na de start zonder foutmelding af (de scene-flow vindt zijn volgende scene niet,
// LGI 0x677040 -> 0x645830); een bestand dat niet te lezen is geeft precies dat. We loggen de eerste mislukte opens van gewone bestanden (niet de \\.\-apparaten),
// elk pad een keer: "bestand mislukt: <pad> (fout N)" (2/3 = bestaat niet, 5 = geen toegang, 32 = gedeeld).
void log_open_fail(const char* path, DWORD err) {
    if (!path || !*path || (path[0] == '\\' && path[1] == '\\' && (path[2] == '.' || path[2] == '?'))) return;
    if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND && err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION && err != ERROR_INVALID_NAME) return;
    static LONG n = 0;
    static std::map<std::string, int> seen;
    EnterCriticalSection(&g_cs);
    const bool fresh = seen.emplace(path, 1).second;
    LeaveCriticalSection(&g_cs);
    if (fresh && InterlockedIncrement(&n) <= 400) rg_log("bestand mislukt: %s (fout %lu)", path, static_cast<unsigned long>(err));
}

// GAME.INI (SDR): welke waarden ziet de game? Bij de eerste fopen van een bestand met die naam leest rg-amhw het zelf en logt de schermsleutels.
void log_game_ini(const char* path) {
    const char* b = std::strrchr(path, '\\');
    const char* b2 = std::strrchr(path, '/');
    if (b2 && (!b || b2 > b)) b = b2;
    if (lstrcmpiA(b ? b + 1 : path, "GAME.INI") != 0) return;
    static LONG n = 0;
    if (InterlockedIncrement(&n) > 2) return;
    char full[MAX_PATH] = {};
    GetFullPathNameA(path, sizeof full, full, nullptr);
    std::string keys;
    if (FILE* f = std::fopen(path, "rb")) {
        char line[200];
        while (std::fgets(line, sizeof line, f)) {
            for (char* c = line; *c; ++c) if (*c == '\r' || *c == '\n') *c = 0;
            if (!std::strncmp(line, "FULLSCREEN", 10) || !std::strncmp(line, "SCREEN_", 7) || !std::strncmp(line, "ENABLE_VSYNC", 12) || !std::strncmp(line, "SEATENABLE", 10)) {
                for (char* c = line; *c; ++c) if (*c == '\t') *c = '=';
                for (char* c = line; *c; ++c) if (*c == ' ') *c = '=';
                keys += std::string(" [") + line + "]";
            }
        }
        std::fclose(f);
    }
    rg_log("GAME.INI geopend door het spel: %s;%s", full, keys.empty() ? " (niet te lezen)" : keys.c_str());
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
    HANDLE r = g_create_w(name, acc, share, sa, disp, flags, tmpl);
    if (r == INVALID_HANDLE_VALUE && name) {
        const DWORD e = GetLastError();
        char a2[300];
        WideCharToMultiByte(CP_ACP, 0, name, -1, a2, sizeof a2, nullptr, nullptr);
        log_open_fail(a2, e);
        SetLastError(e);
    }
    return r;
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
    HANDLE r = g_create_a(name, acc, share, sa, disp, flags, tmpl);
    if (r == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        log_open_fail(name, e);
        SetLastError(e);
    } else if (name && (acc & GENERIC_WRITE) == 0) {
        log_game_ini(name);
    }
    return r;
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
// Vensters van het spel: stijl, grootte, ouder (de eerste zes). Alleen loggen.
using CreateWinAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateWinWFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
CreateWinAFn g_createwin_a = CreateWindowExA;
CreateWinWFn g_createwin_w = CreateWindowExW;
void log_window(const char* api, const char* cls, const char* title, DWORD ex, DWORD style, int x, int y, int w, int h, HWND parent, HWND r, DWORD st0 = 0, int w0 = 0, int h0 = 0) {
    static LONG n = 0;
    if (InterlockedIncrement(&n) > 6) return;
    char head[200];
    std::snprintf(head, sizeof head, api, static_cast<unsigned long>(st0), w0, h0);
    rg_log("venster: %s klasse '%s' titel '%s' stijl 0x%08lX ex 0x%08lX op %d,%d maat %dx%d ouder %p -> %p", head, cls, title, static_cast<unsigned long>(style),
           static_cast<unsigned long>(ex), x, y, w, h, static_cast<void*>(parent), static_cast<void*>(r));
}
// RG_AMHW_WINDOW="X,Y,W,H[,popup]": het eerste hoofdvenster van het spel (geen ouder, breedte >= 640) krijgt deze plek en maat, bij "popup" zonder rand en titelbalk
// (WS_POPUP). SDR maakt zijn venster 1926x1112 (1920x1080 plus kader) op CW_USEDEFAULT; dat past niet op de 1920x1080 van gamescope en de hoofdthread blijft dan in
// NtUserPeekMessage rekenen (Meting 22, nog te bewijzen). SetWindowPos op dat venster krijgt dezelfde maat, zodat het spel het niet terugzet.
struct WinForce { bool on = false, popup = false; int x = 0, y = 0, w = 0, h = 0; HWND hwnd = nullptr; } g_winforce;
void parse_winforce() {
    const std::string v = env("RG_AMHW_WINDOW", "");
    if (v.empty()) return;
    int x, y, w, h;
    if (std::sscanf(v.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w >= 64 && h >= 64) {
        g_winforce.on = true; g_winforce.x = x; g_winforce.y = y; g_winforce.w = w; g_winforce.h = h;
        g_winforce.popup = v.find("popup") != std::string::npos;
        rg_log("venster: RG_AMHW_WINDOW=%s: het eerste hoofdvenster wordt %d,%d %dx%d%s", v.c_str(), x, y, w, h, g_winforce.popup ? " (popup, zonder rand)" : "");
    }
}
void force_window(DWORD& st, int& x, int& y, int& w, int& h, HWND parent) {
    if (!g_winforce.on || g_winforce.hwnd || parent || w < 640) return;
    x = g_winforce.x; y = g_winforce.y; w = g_winforce.w; h = g_winforce.h;
    if (g_winforce.popup) st = (st & ~static_cast<DWORD>(WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_BORDER | WS_DLGFRAME | WS_SYSMENU | WS_THICKFRAME)) | WS_POPUP;
}
HWND WINAPI rg_CreateWindowExA(DWORD ex, LPCSTR c, LPCSTR t, DWORD st, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID l) {
    const bool was = g_winforce.on && !g_winforce.hwnd && !p && w >= 640;
    const DWORD st0 = st; const int w0 = w, h0 = h;
    force_window(st, x, y, w, h, p);
    HWND r = g_createwin_a(ex, c, t, st, x, y, w, h, p, m, i, l);
    if (was && r) g_winforce.hwnd = r;
    log_window(was ? "CreateWindowExA (aangepast; spel vroeg stijl/maat 0x%08lX / %dx%d)" : "CreateWindowExA", reinterpret_cast<uintptr_t>(c) > 0xFFFF && c ? c : "(atoom)", t ? t : "", ex, st, x, y, w, h, p, r, st0, w0, h0);
    return r;
}
HWND WINAPI rg_CreateWindowExW(DWORD ex, LPCWSTR c, LPCWSTR t, DWORD st, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID l) {
    const bool was = g_winforce.on && !g_winforce.hwnd && !p && w >= 640;
    const DWORD st0 = st; const int w0 = w, h0 = h;
    force_window(st, x, y, w, h, p);
    HWND r = g_createwin_w(ex, c, t, st, x, y, w, h, p, m, i, l);
    if (was && r) g_winforce.hwnd = r;
    char ca[100] = "(atoom)", ta[100] = "";
    if (reinterpret_cast<uintptr_t>(c) > 0xFFFF && c) WideCharToMultiByte(CP_ACP, 0, c, -1, ca, sizeof ca, nullptr, nullptr);
    if (t) WideCharToMultiByte(CP_ACP, 0, t, -1, ta, sizeof ta, nullptr, nullptr);
    log_window(was ? "CreateWindowExW (aangepast; spel vroeg stijl/maat 0x%08lX / %dx%d)" : "CreateWindowExW", ca, ta, ex, st, x, y, w, h, p, r, st0, w0, h0);
    return r;
}
// Wat doet het spel verder met zijn venster? De eerste tien aanroepen van elk.
using SetWinPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using ShowWinFn = BOOL(WINAPI*)(HWND, int);
using SetWinLongAFn = LONG(WINAPI*)(HWND, int, LONG);
SetWinPosFn g_setwinpos = SetWindowPos;
ShowWinFn g_showwin = ShowWindow;
SetWinLongAFn g_setwinlong_a = SetWindowLongA;
BOOL WINAPI rg_SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT f) {
    static LONG n = 0;
    const bool mine = g_winforce.on && h == g_winforce.hwnd;
    const int x0 = x, y0 = y, cx0 = cx, cy0 = cy;
    if (mine && !(f & SWP_NOSIZE)) { cx = g_winforce.w; cy = g_winforce.h; }
    if (mine && !(f & SWP_NOMOVE)) { x = g_winforce.x; y = g_winforce.y; }
    if (InterlockedIncrement(&n) <= 10)
        rg_log("venster: SetWindowPos(%p, vlaggen 0x%X) spel %d,%d %dx%d%s", static_cast<void*>(h), f, x0, y0, cx0, cy0, mine ? " (aangepast naar de gevraagde maat)" : "");
    return g_setwinpos(h, after, x, y, cx, cy, f);
}
BOOL WINAPI rg_ShowWindow(HWND h, int cmd) {
    static LONG n = 0;
    if (InterlockedIncrement(&n) <= 10) rg_log("venster: ShowWindow(%p, %d)", static_cast<void*>(h), cmd);
    return g_showwin(h, cmd);
}
LONG WINAPI rg_SetWindowLongA(HWND h, int idx, LONG v) {
    static LONG n = 0;
    if (InterlockedIncrement(&n) <= 10) rg_log("venster: SetWindowLongA(%p, %d, 0x%lX)", static_cast<void*>(h), idx, static_cast<unsigned long>(v));
    return g_setwinlong_a(h, idx, v);
}
void* __cdecl rg_fopen(const char* path, const char* mode) {
    const std::string t = debugout_target(path, mode);
    void* f = g_fopen(t.empty() ? path : t.c_str(), mode);
    if (f && path && !mode_writes(mode)) log_game_ini(path);
    if (!f) log_open_fail(path, ERROR_FILE_NOT_FOUND);
    return f;
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

// --- de log van de teaArcade2-games -------------------------------------------------------------------------------------------------------
// LGI en LGI3D loggen ("[teaArcade2] JvsManager::initialize() failed.", ...) via drie kleine functies die een bericht aan de sinks van een globale logger geven
// (LGI 0x57B600/0x57B620/0x57B640 voor niveau 0/1/2); in de uitgebrachte exe is er geen sink, dus de tekst gaat nergens heen en debugout.txt wordt niet
// geschreven. Wij vinden die functies aan hun bytes (ze staan op andere adressen in LGI3D), zetten een sprong naar een eigen stukje code op hun begin, en
// schrijven het bericht in rg-amhw.log. Alleen lezen wat de game al zegt: de game zelf verandert niet (de functie geeft nog steeds "gelukt" terug).
//   8B 0D <logger>    mov  ecx,[logger]
//   85 C9 74 0D       test ecx,ecx ; je .. (uit)
//   8B 44 24 04       mov  eax,[esp+4]
//   50 6A <niveau>    push eax ; push niveau
//   E8 <rel32>        call dispatch
//   C3 B0 01 C3       ret ; mov al,1 ; ret
extern "C" void __cdecl rg_game_log(int level, const char* msg) {
    static LONG n = 0;
    if (InterlockedIncrement(&n) > 4000 || !msg) return;
    char b[700];
    size_t i = 0;
    for (; msg[i] && i < sizeof b - 1; ++i) b[i] = (msg[i] == '\r' || msg[i] == '\n') ? ' ' : msg[i];
    b[i] = 0;
    while (i > 0 && b[i - 1] == ' ') b[--i] = 0;
    rg_log("spel[%d]: %s", level, b);
}

void hook_game_log() {
    static const unsigned char pat[] = {0x8B, 0x0D, 0, 0, 0, 0, 0x85, 0xC9, 0x74, 0x0D, 0x8B, 0x44, 0x24, 0x04, 0x50, 0x6A, 0, 0xE8, 0, 0, 0, 0, 0xC3, 0xB0, 0x01, 0xC3};
    static const bool wild[sizeof pat] = {false, false, true, true, true, true, false, false, false, false, false, false, false, false, false, false, true, false, true, true, true, true, false, false, false, false};
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    int found = 0;
    for (unsigned si = 0; si < nt->FileHeader.NumberOfSections; ++si) {
        if (!(sec[si].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE* lo = base + sec[si].VirtualAddress;
        const size_t len = sec[si].Misc.VirtualSize;
        for (size_t off = 0; off + sizeof pat <= len; ++off) {
            BYTE* q = lo + off;
            bool ok = true;
            for (size_t k = 0; k < sizeof pat; ++k)
                if (!wild[k] && q[k] != pat[k]) { ok = false; break; }
            if (!ok) continue;
            const int level = q[16];
            if (level < 0 || level > 3) continue;
            auto* stub = static_cast<BYTE*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (!stub) return;
            BYTE* c = stub;
            *c++ = 0xFF; *c++ = 0x74; *c++ = 0x24; *c++ = 0x04;                           // push [esp+4]
            *c++ = 0x68; const int lv = level; std::memcpy(c, &lv, 4); c += 4;            // push niveau
            *c++ = 0xE8; const int rel = static_cast<int>(reinterpret_cast<BYTE*>(&rg_game_log) - (c + 4)); std::memcpy(c, &rel, 4); c += 4;
            *c++ = 0x83; *c++ = 0xC4; *c++ = 0x08;                                        // add esp,8
            *c++ = 0xB0; *c++ = 0x01; *c++ = 0xC3;                                        // mov al,1 ; ret
            DWORD prot = 0;
            if (!VirtualProtect(q, 5, PAGE_EXECUTE_READWRITE, &prot)) continue;
            q[0] = 0xE9;
            const int jr = static_cast<int>(stub - (q + 5));
            std::memcpy(q + 1, &jr, 4);
            VirtualProtect(q, 5, prot, &prot);
            FlushInstructionCache(GetCurrentProcess(), q, 5);
            rg_log("spellog: logfunctie voor niveau %d op 0x%08lX omgeleid naar rg-amhw.log", level, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(q)));
            ++found;
        }
    }
    if (!found) rg_log("spellog: geen teaArcade2-logfunctie gevonden (geen LGI/LGI3D?)");
}

// --- wie vraagt om af te sluiten? --------------------------------------------------------------------------------------------------------
// LGI en LGI3D sluiten zichzelf af via fwFraw::requestExit (LGI 0x645830, LGI3D 0x656660): `mov eax,[ecx+4] ; or dword [eax+4],1 ; ret`. Dertien plekken in de game
// roepen die aan (scene-flow, flow-opdrachten, ...). We zetten een sprong op het begin van die functie naar een stukje code dat eerst de aanroeper en de stapel
// logt ("afsluiten: requestExit vanaf 0x..; stapel: ...") en dan de oorspronkelijke 7 bytes uitvoert en terugspringt: de functie doet hetzelfde als voorheen.
extern "C" void __cdecl rg_exit_trace(const uint32_t* sp) {
    static LONG n = 0;
    if (InterlockedIncrement(&n) > 30) return;
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    uintptr_t lo = 0, hi = 0;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            if (!lo) lo = reinterpret_cast<uintptr_t>(base) + sec[i].VirtualAddress;
            hi = reinterpret_cast<uintptr_t>(base) + sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
        }
    const uintptr_t top = reinterpret_cast<uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    // De knoop (this = ecx bij het begin van requestExit, door de stub bewaard): klassenaam uit de RTTI en de naam van de knoop (std::string op [[this+4]+12], MSVC 9).
    std::string node;
    {
        const uint32_t self = sp[-3];
        auto ok = [](uintptr_t a, size_t n) { return a > 0x10000 && !IsBadReadPtr(reinterpret_cast<const void*>(a), n); };
        if (ok(self, 8)) {
            const uintptr_t vt = *reinterpret_cast<const uint32_t*>(self);
            if (ok(vt - 4, 8)) {
                const uintptr_t col = *reinterpret_cast<const uint32_t*>(vt - 4);
                if (ok(col, 16)) {
                    const uintptr_t td = *reinterpret_cast<const uint32_t*>(col + 12);
                    if (ok(td, 40)) {
                        char nm[96] = {};
                        std::strncpy(nm, reinterpret_cast<const char*>(td + 8), sizeof nm - 1);
                        for (char& ch : nm) if (ch && (ch < 0x20 || ch > 0x7E)) ch = '?';
                        node += nm;
                    }
                }
            }
            const uintptr_t st = *reinterpret_cast<const uint32_t*>(self + 4);
            if (ok(st, 0x30)) {
                const uint32_t size = *reinterpret_cast<const uint32_t*>(st + 0x1C), res = *reinterpret_cast<const uint32_t*>(st + 0x20);
                const uintptr_t strp = res >= 16 ? *reinterpret_cast<const uint32_t*>(st + 12) : st + 12;
                if (size > 0 && size < 120 && ok(strp, size)) {
                    char nm[128] = {};
                    std::memcpy(nm, reinterpret_cast<const void*>(strp), size);
                    for (char& ch : nm) if (ch && (ch < 0x20 || ch > 0x7E)) ch = '?';
                    node += std::string(" '") + nm + "'";
                }
            }
            char tail[40];
            std::snprintf(tail, sizeof tail, " this=0x%08lX vtable=0x%08lX", static_cast<unsigned long>(self), static_cast<unsigned long>(vt));
            node += tail;
        }
    }
    char line[1100];
    int len = std::snprintf(line, sizeof line, "afsluiten: requestExit vanaf 0x%08lX (thread %lu) knoop: %s; stapel:", static_cast<unsigned long>(sp[0]),
                            static_cast<unsigned long>(GetCurrentThreadId()), node.c_str());
    int found = 0;
    for (int i = 1; i < 256 && found < 14 && len < 1000; ++i) {
        if (reinterpret_cast<uintptr_t>(sp + i + 1) > top) break;
        const uintptr_t v = sp[i];
        if (v < lo + 6 || v >= hi) continue;
        const BYTE* c = reinterpret_cast<const BYTE*>(v);
        if (c[-5] == 0xE8 || c[-6] == 0xFF || c[-2] == 0xFF || c[-3] == 0xFF) {   // lijkt op een terugkeeradres (na call rel32 / call [..] / call reg)
            len += std::snprintf(line + len, sizeof line - len, " 0x%08lX", static_cast<unsigned long>(v));
            ++found;
        }
    }
    rg_log("%s", line);
}

void hook_exit_trace() {
    static const unsigned char pat[] = {0x8B, 0x41, 0x04, 0x83, 0x48, 0x04, 0x01, 0xC3};
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    int found = 0;
    for (unsigned si = 0; si < nt->FileHeader.NumberOfSections; ++si) {
        if (!(sec[si].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        BYTE* lo = base + sec[si].VirtualAddress;
        const size_t len = sec[si].Misc.VirtualSize;
        for (size_t off = 0; off + sizeof pat <= len; ++off) {
            BYTE* q = lo + off;
            if (std::memcmp(q, pat, sizeof pat) != 0) continue;
            if (found) continue;   // alleen de eerste (in LGI en LGI3D is het patroon uniek)
            auto* stub = static_cast<BYTE*>(VirtualAlloc(nullptr, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (!stub) return;
            BYTE* c = stub;
            *c++ = 0x9C; *c++ = 0x60;                                       // pushfd ; pushad
            *c++ = 0x8D; *c++ = 0x44; *c++ = 0x24; *c++ = 0x24;             // lea eax,[esp+0x24]  (adres van het terugkeeradres)
            *c++ = 0x50;                                                    // push eax
            *c++ = 0xE8; { const int rel = static_cast<int>(reinterpret_cast<BYTE*>(&rg_exit_trace) - (c + 4)); std::memcpy(c, &rel, 4); c += 4; }
            *c++ = 0x83; *c++ = 0xC4; *c++ = 0x04;                          // add esp,4
            *c++ = 0x61; *c++ = 0x9D;                                       // popad ; popfd
            std::memcpy(c, q, 7); c += 7;                                   // de oorspronkelijke instructies (mov eax,[ecx+4] ; or [eax+4],1)
            *c++ = 0xE9; { const int rel = static_cast<int>((q + 7) - (c + 4)); std::memcpy(c, &rel, 4); c += 4; }
            DWORD prot = 0;
            if (!VirtualProtect(q, 5, PAGE_EXECUTE_READWRITE, &prot)) continue;
            q[0] = 0xE9;
            const int jr = static_cast<int>(stub - (q + 5));
            std::memcpy(q + 1, &jr, 4);
            VirtualProtect(q, 5, prot, &prot);
            FlushInstructionCache(GetCurrentProcess(), q, 8);
            rg_log("afsluitspoor: requestExit op 0x%08lX gevolgd", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(q)));
            ++found;
        }
    }
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

// --- de werkmap van het spel (SDR) ------------------------------------------------------------------------------------------------------
// Dream Raiders (Game.exe 0x4019C0, vlak na de start) doet: `if (GetFileAttributesA(".\data") != 0x10) SetCurrentDirectoryA("..\")`: de game is bedoeld om uit een
// submap (`prg\`) te draaien en gaat een map omhoog als `.\data` er niet is. In een view is `Data` een link naar de NAS en Proton meldt zo'n gelinkte map als
// map plus reparse point (0x410, gemeten 2026-10-16): de game stapt dan uit de view, en elk relatief pad (GAME.INI, .\data\displabel.txt, het geluid) faalt.
// Wij loggen de werkmap bij het laden en elke SetCurrentDirectory van het spel, en de eerste attributen met een reparse-bit op een map. Met
// RG_AMHW_NOREPARSE=1 (alleen als terugval; de nette oplossing is [Launch] ViewRealDirs) maken we van 0x410 weer 0x10, zoals een gewone map op Windows.
using GetFileAttrAFn = DWORD(WINAPI*)(LPCSTR);
using GetFileAttrWFn = DWORD(WINAPI*)(LPCWSTR);
using SetCurDirAFn = BOOL(WINAPI*)(LPCSTR);
using SetCurDirWFn = BOOL(WINAPI*)(LPCWSTR);
GetFileAttrAFn g_getattr_a = GetFileAttributesA;
GetFileAttrWFn g_getattr_w = GetFileAttributesW;
SetCurDirAFn g_setcwd_a = SetCurrentDirectoryA;
SetCurDirWFn g_setcwd_w = SetCurrentDirectoryW;
bool g_noreparse = false;
void log_cwd(const char* what, const char* arg) {
    char cwd[MAX_PATH] = {};
    GetCurrentDirectoryA(sizeof cwd, cwd);
    rg_log("werkmap: %s%s%s -> nu %s", what, arg ? " " : "", arg ? arg : "", cwd);
}
DWORD attr_filter(const char* name, DWORD r) {
    if (r == INVALID_FILE_ATTRIBUTES || !(r & FILE_ATTRIBUTE_DIRECTORY) || !(r & FILE_ATTRIBUTE_REPARSE_POINT)) return r;
    static LONG n = 0;
    if (InterlockedIncrement(&n) <= 12)
        rg_log("attributen: map %s is 0x%lX (map + reparse point, een gelinkte map)%s", name, static_cast<unsigned long>(r),
               g_noreparse ? "; RG_AMHW_NOREPARSE=1: doorgegeven als 0x10" : "");
    return g_noreparse ? (r & ~static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT)) : r;
}
DWORD WINAPI rg_GetFileAttributesA(LPCSTR p) { return attr_filter(p ? p : "", g_getattr_a(p)); }
DWORD WINAPI rg_GetFileAttributesW(LPCWSTR p) {
    const DWORD r = g_getattr_w(p);
    if (r == INVALID_FILE_ATTRIBUTES || !(r & FILE_ATTRIBUTE_REPARSE_POINT)) return r;
    char a[300] = {};
    if (p) WideCharToMultiByte(CP_ACP, 0, p, -1, a, sizeof a, nullptr, nullptr);
    return attr_filter(a, r);
}
BOOL WINAPI rg_SetCurrentDirectoryA(LPCSTR p) {
    const BOOL r = g_setcwd_a(p);
    static LONG n = 0;
    if (InterlockedIncrement(&n) <= 20) log_cwd(r ? "SetCurrentDirectoryA" : "SetCurrentDirectoryA (mislukt)", p);
    return r;
}
BOOL WINAPI rg_SetCurrentDirectoryW(LPCWSTR p) {
    const BOOL r = g_setcwd_w(p);
    static LONG n = 0;
    if (InterlockedIncrement(&n) <= 20) {
        char a[300] = {};
        if (p) WideCharToMultiByte(CP_ACP, 0, p, -1, a, sizeof a, nullptr, nullptr);
        log_cwd(r ? "SetCurrentDirectoryW" : "SetCurrentDirectoryW (mislukt)", a);
    }
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

// --- de bewaker: waar staan de threads van een game die vastzit? -----------------------------------------------------------------------
// RG_AMHW_WATCHDOG=N (seconden; standaard uit): na N en na 2N seconden schrijft een eigen thread van elke andere thread van het proces het adres waar hij staat
// (module+offset) en de terugkeeradressen die op de stapel lijken (zoals het afsluitspoor), in rg-amhw.log. Een thread die wacht staat in ntdll; de terugkeeradressen in
// de game wijzen dan naar de functie die wacht. Alleen lezen: elke thread wordt kort stilgezet en weer gestart.
// Berichtenteller: SDR bleef in zijn berichtenlus hangen (PeekMessageA geeft steeds een bericht, de game tekent nooit een beeld; 0x401600). De bewaker toont welke berichten
// het zijn (WM_PAINT = 0x0F zonder dat het venster ooit geldig wordt, WM_SIZE/WM_ACTIVATE bij een schermwissel, ...). Alleen tellen; het bericht gaat ongewijzigd door.
using PeekMsgAFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using PeekMsgWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
PeekMsgAFn g_peek_a = PeekMessageA;
PeekMsgWFn g_peek_w = PeekMessageW;
volatile LONG g_msg_count[0x400];
volatile LONG g_msg_other = 0, g_peek_calls = 0, g_peek_empty = 0, g_peek_enter = 0;
void count_msg(BOOL r, const MSG* m) {
    InterlockedIncrement(&g_peek_calls);
    if (!r) { InterlockedIncrement(&g_peek_empty); return; }
    if (m->message < 0x400) InterlockedIncrement(&g_msg_count[m->message]);
    else InterlockedIncrement(&g_msg_other);
}
BOOL WINAPI rg_PeekMessageA(LPMSG m, HWND w, UINT a, UINT b, UINT f) { InterlockedIncrement(&g_peek_enter); const BOOL r = g_peek_a(m, w, a, b, f); if (m) count_msg(r, m); return r; }
BOOL WINAPI rg_PeekMessageW(LPMSG m, HWND w, UINT a, UINT b, UINT f) { InterlockedIncrement(&g_peek_enter); const BOOL r = g_peek_w(m, w, a, b, f); if (m) count_msg(r, m); return r; }
void log_msgs(int round) {
    std::string t;
    char b[48];
    for (int pass = 0; pass < 6; ++pass) {   // de zes meest voorkomende berichten
        int best = -1;
        LONG bc = 0;
        for (int i = 0; i < 0x400; ++i)
            if (g_msg_count[i] > bc) { bc = g_msg_count[i]; best = i; }
        if (best < 0) break;
        std::snprintf(b, sizeof b, " 0x%X=%ld", best, static_cast<long>(bc));
        t += b;
        g_msg_count[best] = -g_msg_count[best];   // tijdelijk wegzetten
    }
    for (int i = 0; i < 0x400; ++i) if (g_msg_count[i] < 0) g_msg_count[i] = -g_msg_count[i];
    rg_log("bewaker %d: PeekMessage %ld aangeroepen, %ld teruggekeerd, %ld zonder bericht, ander (>=0x400) %ld; berichten (0x0F=WM_PAINT 0x05=WM_SIZE 0x06=WM_ACTIVATE 0x1C=WM_ACTIVATEAPP 0x113=WM_TIMER 0x200=WM_MOUSEMOVE):%s", round,
           static_cast<long>(g_peek_enter), static_cast<long>(g_peek_calls), static_cast<long>(g_peek_empty), static_cast<long>(g_msg_other), t.c_str());
}

// --- de logboeken van de AM Library van SDR ----------------------------------------------------------------------------------------------
// Game.exe (Dream Raiders, AM Library 1.08) heeft een logger (0x70B5B0 -> 0x70B480) die alleen schrijft als een niveauvariabele van de module >= 1 is, en dan naar een
// sink-functie op [0xA21048] (leeg: naar stderr). In de uitgebrachte exe staan alle niveaus op 0. RG_AMHW_AMLOG=1 zet de veertien niveaus die voor een logaanroep op 1
// gelezen worden op 1 (alleen als de bytes van 0x70B5B0 kloppen) en zet een eigen sink; elke regel staat in rg-amhw.log als `am: ...`. RG_AMHW_PEEK="0xADRES,..." logt
// bij elke bewaker-ronde de dwords op die adressen (SDR: 0x348A090/0x348A094 = foutcode/-detail van de invoerstart, 0x348A0BC = vlaggen, 0xA07FD8 = AM-invoer klaar).
extern "C" void __cdecl rg_am_sink(const char* msg) {
    static LONG n = 0;
    if (!msg || InterlockedIncrement(&n) > 600) return;
    char b[500];
    size_t i = 0;
    for (; msg[i] && i < sizeof b - 1; ++i) b[i] = (msg[i] == '\r' || msg[i] == '\n') ? ' ' : msg[i];
    b[i] = 0;
    rg_log("am: %s", b);
}
void amlog_enable() {
    static const uint8_t sig[] = {0x8B, 0x4C, 0x24, 0x0C, 0x8B, 0x54, 0x24, 0x08, 0x8D, 0x44, 0x24, 0x10, 0x50, 0x8B, 0x44, 0x24, 0x08};
    static const uintptr_t levels[] = {0xA07F90, 0xA07F98, 0xA082B8, 0xA08548, 0xA08554, 0xA08558, 0xA148A8, 0xA14CB8, 0xA14ED0, 0xA14ED8, 0xA18DA8, 0xA18E30, 0xA1CE10, 0xA21058};
    const uintptr_t logger = 0x70B5B0, sink = 0xA21048;
    if (IsBadReadPtr(reinterpret_cast<const void*>(logger), sizeof sig) || std::memcmp(reinterpret_cast<const void*>(logger), sig, sizeof sig) != 0) {
        rg_log("am: RG_AMHW_AMLOG=1, maar 0x70B5B0 heeft de bytes van de SDR-logger niet (andere game?); niets gezet");
        return;
    }
    int set = 0;
    for (uintptr_t a : levels) {
        DWORD prot = 0;
        auto* p = reinterpret_cast<volatile LONG*>(a);
        if (IsBadWritePtr(const_cast<LONG*>(p), 4) && !VirtualProtect(const_cast<LONG*>(p), 4, PAGE_READWRITE, &prot)) continue;
        if (*p < 1) *p = 1;
        ++set;
    }
    auto* sk = reinterpret_cast<volatile uintptr_t*>(sink);
    if (!IsBadWritePtr(const_cast<uintptr_t*>(sk), 4)) *sk = reinterpret_cast<uintptr_t>(&rg_am_sink);
    rg_log("am: logniveaus van %d modules op 1 gezet en een eigen sink op 0xA21048 (regels als `am: ...`)", set);
}
std::vector<uintptr_t> g_peek;
void parse_peek() {
    const std::string v = env("RG_AMHW_PEEK", "");
    size_t i = 0;
    while (i < v.size()) {
        const size_t j = v.find(',', i);
        const std::string t = v.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!t.empty()) g_peek.push_back(static_cast<uintptr_t>(std::strtoul(t.c_str(), nullptr, 16)));
        if (j == std::string::npos) break;
        i = j + 1;
    }
}
void log_peek(int round) {
    if (g_peek.empty()) return;
    std::string t;
    char b[48];
    for (uintptr_t a : g_peek) {
        if (IsBadReadPtr(reinterpret_cast<const void*>(a), 4)) std::snprintf(b, sizeof b, " 0x%lX=?", static_cast<unsigned long>(a));
        else std::snprintf(b, sizeof b, " 0x%lX=0x%lX", static_cast<unsigned long>(a), static_cast<unsigned long>(*reinterpret_cast<const uint32_t*>(a)));
        t += b;
    }
    rg_log("bewaker %d: geheugen:%s", round, t.c_str());
}

std::string where(uintptr_t a) {
    HMODULE m = nullptr;
    char nm[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(a), &m) && m) {
        GetModuleFileNameA(m, nm, sizeof nm);
        const char* b = std::strrchr(nm, '\\');
        char o[64];
        std::snprintf(o, sizeof o, "+0x%lX", static_cast<unsigned long>(a - reinterpret_cast<uintptr_t>(m)));
        return std::string(b ? b + 1 : nm) + o;
    }
    char o[32];
    std::snprintf(o, sizeof o, "0x%08lX", static_cast<unsigned long>(a));
    return o;
}
void dump_threads(int round) {
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const uintptr_t lo = reinterpret_cast<uintptr_t>(base), hi = lo + nt->OptionalHeader.SizeOfImage;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) { rg_log("bewaker: geen threadlijst (fout %lu)", static_cast<unsigned long>(GetLastError())); return; }
    struct Seen { DWORD tid; uintptr_t eip, esp; uintptr_t ret[10]; int nret; unsigned long long user_ms, kern_ms; };
    static Seen seen[64];   // vast geheugen: een stilgezette thread kan de heap-slot vasthouden, dus tussen Suspend en Resume geen allocatie en geen logboek
    int n = 0;
    THREADENTRY32 te = {};
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok && n < 64; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) == static_cast<DWORD>(-1)) { CloseHandle(th); continue; }
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(th, &c)) {
            Seen& e = seen[n++];
            e.tid = te.th32ThreadID; e.eip = c.Eip; e.esp = c.Esp; e.nret = 0; e.user_ms = e.kern_ms = 0;
            const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(c.Esp);
            for (int i = 0; i < 1024 && e.nret < 10; ++i) {
                if (IsBadReadPtr(sp + i, sizeof(uintptr_t))) break;
                const uintptr_t v = sp[i];
                if (v < lo + 6 || v >= hi || IsBadReadPtr(reinterpret_cast<const void*>(v - 6), 6)) continue;
                const BYTE* q = reinterpret_cast<const BYTE*>(v);
                if (q[-5] == 0xE8 || q[-6] == 0xFF || q[-2] == 0xFF || q[-3] == 0xFF) e.ret[e.nret++] = v;   // lijkt op een terugkeeradres (na call)
            }
        }
        {   // processortijd van de thread: een thread die rekent heeft een groeiende user-tijd, een thread die wacht niet (twee rondes vergelijken)
            FILETIME cr, ex, kt, ut;
            if (n > 0 && seen[n - 1].tid == te.th32ThreadID && GetThreadTimes(th, &cr, &ex, &kt, &ut)) {
                seen[n - 1].user_ms = ((static_cast<unsigned long long>(ut.dwHighDateTime) << 32) | ut.dwLowDateTime) / 10000;
                seen[n - 1].kern_ms = ((static_cast<unsigned long long>(kt.dwHighDateTime) << 32) | kt.dwLowDateTime) / 10000;
            }
        }
        ResumeThread(th);
        CloseHandle(th);
    }
    CloseHandle(snap);
    {
        IO_COUNTERS io = {};
        if (GetProcessIoCounters(GetCurrentProcess(), &io))
            rg_log("bewaker %d: proces leest %llu bytes in %llu opdrachten, schrijft %llu bytes", round, static_cast<unsigned long long>(io.ReadTransferCount),
                   static_cast<unsigned long long>(io.ReadOperationCount), static_cast<unsigned long long>(io.WriteTransferCount));
    }
    for (int i = 0; i < n; ++i) {
        std::string line = "bewaker " + std::to_string(round) + ": thread " + std::to_string(seen[i].tid) + " op " + where(seen[i].eip);
        char t[128];
        std::snprintf(t, sizeof t, " (esp 0x%08lX, user %llu ms, kern %llu ms); terugkeer:", static_cast<unsigned long>(seen[i].esp), seen[i].user_ms, seen[i].kern_ms);
        line += t;
        for (int k = 0; k < seen[i].nret; ++k) {
            char r[24];
            std::snprintf(r, sizeof r, " 0x%08lX", static_cast<unsigned long>(seen[i].ret[k]));
            line += r;
        }
        rg_log("%s", line.c_str());
    }
    log_msgs(round);
    log_peek(round);
    rg_log("bewaker %d: %d threads bekeken", round, n);
}
DWORD WINAPI watchdog(LPVOID arg) {
    const DWORD secs = static_cast<DWORD>(reinterpret_cast<uintptr_t>(arg));
    for (int round = 1; round <= 2; ++round) {
        Sleep(secs * 1000);
        dump_threads(round);
    }
    return 0;
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
    g_noreparse = env("RG_AMHW_NOREPARSE", "") == "1";
    if (const int w = std::atoi(env("RG_AMHW_WATCHDOG", "0").c_str()); w > 0 && w <= 600) {
        CreateThread(nullptr, 0, watchdog, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(w)), 0, nullptr);
        rg_log("bewaker: aan, na %d en %d s", w, 2 * w);
    }
    log_cwd("bij het laden", nullptr);
    hook("KERNEL32.dll", "GetFileAttributesA", rg_GetFileAttributesA, g_getattr_a);
    hook("KERNEL32.dll", "GetFileAttributesW", rg_GetFileAttributesW, g_getattr_w);
    hook("KERNEL32.dll", "SetCurrentDirectoryA", rg_SetCurrentDirectoryA, g_setcwd_a);
    hook("KERNEL32.dll", "SetCurrentDirectoryW", rg_SetCurrentDirectoryW, g_setcwd_w);
    hook("USER32.dll", "PeekMessageA", rg_PeekMessageA, g_peek_a);
    hook("USER32.dll", "PeekMessageW", rg_PeekMessageW, g_peek_w);
    hook("USER32.dll", "CreateWindowExA", rg_CreateWindowExA, g_createwin_a);
    hook("USER32.dll", "CreateWindowExW", rg_CreateWindowExW, g_createwin_w);
    parse_winforce();
    parse_peek();
    if (env("RG_AMHW_AMLOG", "") == "1") amlog_enable();
    hook("USER32.dll", "SetWindowPos", rg_SetWindowPos, g_setwinpos);
    hook("USER32.dll", "ShowWindow", rg_ShowWindow, g_showwin);
    hook("USER32.dll", "SetWindowLongA", rg_SetWindowLongA, g_setwinlong_a);
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
    if (env("RG_AMHW_GAMELOG", "1") != "0") hook_game_log();
    if (env("RG_AMHW_GAMELOG", "1") != "0") hook_exit_trace();
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
