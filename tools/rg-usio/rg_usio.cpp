// SPDX-License-Identifier: GPL-3.0-or-later
// bnusio.dll (RavenGun): de USIO-bibliotheek van Namco, eigen werk, voor Point Blank X onder Wine.
//
// WAAROM
//   Point Blank X (Unity, 32-bit) praat met zijn I/O-bord via Plugins/bnusio.dll (P/Invoke "bnusio": 36 usio_* en 12 gun_* functies,
//   klasse BNUsio in Assembly-CSharp-firstpass.dll). Het echte bestand opent het USIO-bord via SetupAPI en WinUSB en het vuurwapen via een
//   seriële poort. Onder Wine is er geen bord; de gesloten TeknoParrot-kern vervangt die laag in het spelproces. Deze DLL doet dat zelf:
//   hij staat in de view van de game op de plaats van het echte bestand (drop-in, docs/dropins.md) en beantwoordt alle functies uit het
//   gedeelde geheugen `TeknoParrot_JvsState` dat de daemon schrijft (tp-shm-indeling PointBlankX).
//
// WAT
//   Elke export heeft dezelfde naam en cdecl-aanroep als het echte bestand (gemeten: alle `ret` zonder argument, ongedecoreerde namen).
//   De afbeelding van bits en assen op wat het spel leest staat in usio_model.h (en wordt daar getoetst); dit bestand is alleen de
//   Windows-schil: gedeeld geheugen openen, een pollende draad (de munt is een korte puls), het logboek en de uitvoerpunten.
//
// LOGBOEK
//   rg-usio.log naast deze DLL (de plugins-map van de view): het laden, het openen van het gedeelde geheugen en de eerste aanroep van elke
//   functie. Zonder dat logboek is de DLL niet geladen (en dan draait het spel op het echte bestand of op niets).
//
// OMGEVING
//   RG_USIO_SHM       naam van het gedeelde geheugen (standaard TeknoParrot_JvsState)
//   RG_USIO_FLIP_Y=0  y niet omkeren (standaard 1: de daemon telt y van boven, het spel van onder)
//
// Clean-room: alleen de functienamen en -types uit het spel zelf (BNUsio.cs) en de waarden die we aan het echte bestand en het spel hebben
// gemeten; geen code van TeknoParrot of van het echte bnusio.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "usio_model.h"

namespace {

constexpr int kRetOk = 0;

CRITICAL_SECTION g_cs;       // beschermt g_model en de logboekbuffer
bool g_cs_ready = false;
rg_usio::PointBlankModel g_model;
HANDLE g_map = nullptr;
const uint8_t* g_view = nullptr;
HANDLE g_thread = nullptr;
volatile LONG g_stop = 0;
wchar_t g_log_path[MAX_PATH];
char g_shm_name[128] = "TeknoParrot_JvsState";
char g_board_name[] = "NBGI.;USIO01;Ver1.00;JPN,Multipurpose.";
uint8_t g_regs[512];          // usio_Get/SetRegisterU8/U16 en usio_Get/SetBuffer: een gewoon registerbeeld
bool g_seen[64];

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

// Logt de eerste aanroep van elke functie (id < 64), zodat het logboek laat zien wat het spel werkelijk gebruikt.
void first_call(int id, const char* name) {
    if (id < 0 || id >= 64 || g_seen[id]) return;
    g_seen[id] = true;
    rg_log("eerste aanroep: %s", name);
}

void sample() {
    if (!g_view) {
        g_map = OpenFileMappingA(FILE_MAP_READ, FALSE, g_shm_name);
        if (g_map) {
            g_view = static_cast<const uint8_t*>(MapViewOfFile(g_map, FILE_MAP_READ, 0, 0, rg_usio::kShmSize));
            if (g_view) rg_log("gedeeld geheugen %s geopend", g_shm_name);
        }
        if (!g_view) return;
    }
    uint8_t copy[rg_usio::kShmSize];
    std::memcpy(copy, g_view, sizeof copy);
    EnterCriticalSection(&g_cs);
    g_model.update(copy);
    LeaveCriticalSection(&g_cs);
}

DWORD WINAPI poll_thread(LPVOID) {
    DWORD last_try = 0;
    while (!g_stop) {
        if (!g_view) {
            const DWORD now = GetTickCount();
            if (now - last_try < 250) { Sleep(10); continue; }
            last_try = now;
        }
        sample();
        Sleep(1);
    }
    return 0;
}

void ensure_started() {
    if (g_thread) return;
    g_thread = CreateThread(nullptr, 0, poll_thread, nullptr, 0, nullptr);
    rg_log("pollende draad %s", g_thread ? "gestart" : "NIET gestart");
}

struct Locked {
    Locked() { EnterCriticalSection(&g_cs); }
    ~Locked() { LeaveCriticalSection(&g_cs); }
};

}  // namespace

#define RG_API extern "C" __declspec(dllexport)
#define RG_FIRST(id) first_call(id, __func__)

// --- usio_* ------------------------------------------------------------------------------------------------------------------------
RG_API void usio_Initialize() { RG_FIRST(0); ensure_started(); }
RG_API void usio_Finalize() { RG_FIRST(1); }
RG_API int usio_Open() { RG_FIRST(2); ensure_started(); return kRetOk; }
RG_API int usio_Close() { RG_FIRST(3); return kRetOk; }
// Het echte bestand wacht tot de communicatiedraad een ronde klaar heeft; wij hebben het beeld al (eigen draad) en nemen er nu nog een.
RG_API int usio_Communication(int) { RG_FIRST(4); sample(); return kRetOk; }
RG_API int usio_IsConnected() { RG_FIRST(5); return 1; }
RG_API void usio_ResetIoBoard() { RG_FIRST(6); }
RG_API int usio_SetRegisterU16(uint16_t a, uint16_t v) { RG_FIRST(7); if (a + 2u <= sizeof g_regs) std::memcpy(g_regs + a, &v, 2); return kRetOk; }
RG_API int usio_SetRegisterU8(uint16_t a, uint8_t v) { RG_FIRST(8); if (a < sizeof g_regs) g_regs[a] = v; return kRetOk; }
RG_API int usio_SetBuffer(uint16_t a, const void* p, uint16_t len) {
    RG_FIRST(9);
    if (p && a + static_cast<unsigned>(len) <= sizeof g_regs) std::memcpy(g_regs + a, p, len);
    return kRetOk;
}
RG_API uint32_t usio_GetRegisterU16(uint16_t a) { RG_FIRST(10); uint16_t v = 0; if (a + 2u <= sizeof g_regs) std::memcpy(&v, g_regs + a, 2); return v; }
RG_API uint32_t usio_GetRegisterU8(uint16_t a) { RG_FIRST(11); return a < sizeof g_regs ? g_regs[a] : 0; }
RG_API void usio_GetBuffer(uint16_t a, void* p, uint16_t len) {
    RG_FIRST(12);
    if (p && a + static_cast<unsigned>(len) <= sizeof g_regs) std::memcpy(p, g_regs + a, len);
}
RG_API int usio_SetSystemError(uint16_t) { RG_FIRST(13); return kRetOk; }
RG_API uint32_t usio_GetSystemError() { RG_FIRST(14); return 0; }
RG_API uint32_t usio_GetFirmwareVersion() { RG_FIRST(15); return rg_usio::kFirmwareVersion; }
RG_API int usio_SetExpansionMode(uint16_t) { RG_FIRST(16); return kRetOk; }
RG_API uint32_t usio_GetExpansionMode() { RG_FIRST(17); return 0; }
RG_API const char* usio_GetIoBoardNameString() { RG_FIRST(18); return g_board_name; }
RG_API uint32_t usio_GetSwIn() { RG_FIRST(19); Locked l; return g_model.sw_in(); }
// bnusio.dll geeft de 64 bits terug in eax:edx (een uint64 bij cdecl op x86); een uint64_t-retour doet hetzelfde.
RG_API uint64_t usio_GetSwIn64() { RG_FIRST(20); Locked l; return g_model.sw_in64(); }
RG_API uint32_t usio_GetAnalogIn(uint8_t) { RG_FIRST(21); return 0; }   // het spel leest geen analoge ingangen (het vuurwapen loopt via gun_*)
RG_API int usio_SetGout(uint8_t ch, uint8_t v) { RG_FIRST(22); Locked l; g_model.set_gout(ch, v); return kRetOk; }
RG_API uint32_t usio_GetGout(uint8_t ch) { RG_FIRST(23); Locked l; return g_model.gout(ch); }
RG_API int usio_SetPLCounter(uint16_t) { RG_FIRST(24); return kRetOk; }
RG_API uint32_t usio_GetCoin(int slot) { RG_FIRST(25); Locked l; return g_model.coin(slot); }
RG_API uint32_t usio_GetCoinError(int) { RG_FIRST(26); return 0; }
RG_API uint32_t usio_GetService(int slot) { RG_FIRST(27); Locked l; return g_model.service(slot); }
RG_API uint32_t usio_GetServiceError(int) { RG_FIRST(28); return 0; }
RG_API int usio_DecCoin(int slot, uint16_t n) { RG_FIRST(29); Locked l; g_model.dec_coin(slot, n); return kRetOk; }
RG_API int usio_DecService(int slot, uint16_t n) { RG_FIRST(30); Locked l; g_model.dec_service(slot, n); return kRetOk; }
RG_API int usio_ResetCoin() { RG_FIRST(31); Locked l; g_model.reset_coin(); return kRetOk; }
// Niet door het spel gebruikt (niet in BNUsio.cs), maar het echte bestand exporteert ze: veilige standaardantwoorden.
RG_API int usio_SetHopperLimit() { RG_FIRST(32); return kRetOk; }
RG_API int usio_SetHopperRequest() { RG_FIRST(33); return kRetOk; }
RG_API int usio_Sram_Read() { RG_FIRST(34); return kRetOk; }
RG_API int usio_Sram_Write() { RG_FIRST(35); return kRetOk; }

// --- gun_* (het echte bestand praat hier met een vuurwapenkast aan een COM-poort; wij hebben de positie uit het gedeelde geheugen) -------
RG_API int gun_Open(uint8_t) { RG_FIRST(40); ensure_started(); return kRetOk; }
RG_API int gun_OpenEx(uint8_t, uint32_t) { RG_FIRST(41); ensure_started(); return kRetOk; }
RG_API void gun_Close() { RG_FIRST(42); }
RG_API int gun_SendCommand(uint8_t) { RG_FIRST(43); return kRetOk; }
RG_API uint32_t gun_GetRomVersion() { RG_FIRST(44); return 0x10; }
RG_API uint32_t gun_GetPositionX(int p) { RG_FIRST(45); Locked l; return g_model.gun_x(p); }
RG_API uint32_t gun_GetPositionY(int p) { RG_FIRST(46); Locked l; return g_model.gun_y(p); }
RG_API int gun_IsTriggered(int p) { RG_FIRST(47); Locked l; return g_model.gun_triggered(p) ? 1 : 0; }
RG_API int gun_IsInRange(int p) { RG_FIRST(48); Locked l; return g_model.gun_in_range(p) ? 1 : 0; }
RG_API int gun_IsLedChecked(int, int) { RG_FIRST(49); return 1; }
RG_API uint32_t gun_GetLed(int) { RG_FIRST(50); return 0; }
RG_API int gun_StrikeOnce(int, uint8_t) { RG_FIRST(51); return kRetOk; }

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        g_cs_ready = true;
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(inst, buf, MAX_PATH);
        wchar_t* slash = wcsrchr(buf, L'\\');
        if (slash) *slash = 0;
        _snwprintf(g_log_path, MAX_PATH, L"%ls\\rg-usio.log", buf);
        char env[128];
        if (GetEnvironmentVariableA("RG_USIO_SHM", env, sizeof env) > 0 && env[0]) std::snprintf(g_shm_name, sizeof g_shm_name, "%s", env);
        bool flip = true;
        if (GetEnvironmentVariableA("RG_USIO_FLIP_Y", env, sizeof env) > 0 && env[0] == '0') flip = false;
        g_model = rg_usio::PointBlankModel(flip);
        rg_log("rg-usio (bnusio.dll) geladen; geheugen %s, y %s", g_shm_name, flip ? "omgekeerd (onderaan = 0)" : "zoals de daemon (bovenaan = 0)");
    } else if (reason == DLL_PROCESS_DETACH && g_cs_ready) {
        InterlockedExchange(&g_stop, 1);
    }
    return TRUE;
}
