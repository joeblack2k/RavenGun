/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-unis: RavenGuns eigen UsbPluginsDll.dll, een drop-in voor de Unreal Engine 4-games van WartranTroopers/UNIS die hun UNIS-I/O-bord
 * (USB-HID, dongle, munten) via deze DLL aansturen: Elevator Action Invasion (ESGame/Plugins/) en Raccoon Rampage (RSGame/Plugins/).
 * Op de kast praat de DLL met het bord; hier beantwoordt hij de game uit het bordbeeld van RavenGun, zonder bord en zonder de kern
 * van TeknoParrot.
 *
 * CLEAN-ROOM: geschreven uit de PDB's van de games (de typen FCoUnisIoData/FESIoData/FPACKET, de prototypes van de m_dll*-pointers in
 * UUsbBPLibrary, de code van ParseIOData/Tick/IsGun/IsGunCalibrate die de bytes leest) en de exporttabel van de originele DLL's
 * (objdump -p: 75 resp. 107 namen). De originele DLL is alleen gelezen om te zien hoe hij zijn rapport samenstelt (49 bytes, byte 1 =
 * 0xAA, byte 48 = ~som(1..47), munttellers per speler); geen code ervan is overgenomen.
 *
 *   - De game laadt <ProjectPluginsDir>/UsbPluginsDll.dll (UUsbBPLibrary::Usb_ImportDLL, FPlatformProcess::GetDllHandle) en zoekt de
 *     BIND_*-namen met GetDllExport; alle namen van beide originelen staan hier (de niet gebruikte als stub);
 *   - een draad leest elke 2 ms het bordbeeld (Windows-mapping "TeknoParrot_JvsState" van rg-jvsbridge.exe in de prefix: bitveld op
 *     offset 8, aanwijsassen op 12..19) en telt de muntflanken per speler;
 *   - BIND_GetIoData (Raccoon: 49 bytes), BIND_GetESIoData (EAI: 16 bytes) en BIND_GetGunData (EAI: FPACKET) bouwen hun antwoord uit
 *     dat beeld (rg_unis_kern.c); welke bit welke ingang is staat in rg-unis.ini naast de DLL;
 *   - de dongle zegt ja, het bord is er, lampen/motoren/netwerk doen niets;
 *   - RG_UNIS_LOG=pad schrijft een logboek (relatief pad = naast de DLL): de tabel, de eerste aanroep van elke BIND-functie en elke 10 s
 *     een telregel met het aantal aanroepen: het bewijs dat ONZE DLL laadt en wat de game ervan vraagt.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rg_unis_kern.h"

#define EXPORT __declspec(dllexport)

static CRITICAL_SECTION slot;
static HMODULE deze;
static HANDLE draad;
static volatile LONG stoppen;
static rgu_cfg cfg;
static rgu_staat staat;
static rgu_beeld beeld;
static const volatile uint8_t *kaart;
static int spel_b = 1080, spel_h = 1920;   /* BIND_OpenGunDevice: de afmetingen van het spelbeeld (EAI: 1080x1920) */
static volatile LONG n_io, n_es, n_gun, n_munt;
static int gestart;

#define MAX_NAMEN 160
static const char *gezien_naam[MAX_NAMEN];
static int gezien_n;

static void logf_(const char *fmt, ...)
{
    char regel[640];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(regel, sizeof regel - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof regel - 2) n = (int)sizeof regel - 2;
    regel[n] = '\n';
    regel[n + 1] = 0;
    /* Een relatief pad is relatief aan de map van deze DLL (in de view), niet aan de werkmap van het proces. */
    const char *pad = getenv("RG_UNIS_LOG");
    char vol[MAX_PATH + 64];
    if (pad && *pad && !(pad[0] == '\\' || pad[0] == '/' || (pad[0] && pad[1] == ':'))) {
        DWORD m = GetModuleFileNameA(deze, vol, MAX_PATH);
        char *schuin = m ? strrchr(vol, '\\') : NULL;
        if (schuin && strlen(pad) < 60) { strcpy(schuin + 1, pad); pad = vol; }
    }
    FILE *f = pad && *pad ? fopen(pad, "a") : NULL;
    if (f) { fputs("rg-unis: ", f); fputs(regel, f); fclose(f); }
    else { OutputDebugStringA("rg-unis: "); OutputDebugStringA(regel); }
}

/* De eerste aanroep van elke BIND-functie in het logboek (welke vraagt de game werkelijk?). */
static void aanroep(const char *naam)
{
    EnterCriticalSection(&slot);
    int nieuw = 1;
    for (int i = 0; i < gezien_n; i++)
        if (gezien_naam[i] == naam) { nieuw = 0; break; }
    if (nieuw && gezien_n < MAX_NAMEN) gezien_naam[gezien_n++] = naam;
    LeaveCriticalSection(&slot);
    if (nieuw) logf_("eerste aanroep: %s", naam);
}

static int detecteer_game(void)
{
    char pad[MAX_PATH];
    if (!GetModuleFileNameA(NULL, pad, MAX_PATH)) return RGU_GAME_ONBEKEND;
    if (strstr(pad, "ESGame")) return RGU_GAME_EAI;
    if (strstr(pad, "RSGame")) return RGU_GAME_RR;
    return RGU_GAME_ONBEKEND;
}

static void lees_tabel(void)
{
    int game = detecteer_game();
    rgu_standaard(&cfg, game ? game : RGU_GAME_RR);
    char pad[MAX_PATH + 32];
    DWORD n = GetModuleFileNameA(deze, pad, MAX_PATH);
    char *schuin = n ? strrchr(pad, '\\') : NULL;
    if (!schuin) { logf_("tabel: standaard (geen pad van de DLL), game=%d", cfg.game); return; }
    strcpy(schuin + 1, "rg-unis.ini");
    FILE *f = fopen(pad, "rb");
    if (!f) { logf_("tabel: %s ontbreekt, de standaard van game %d (%s)", pad, cfg.game, game ? "uit de exe" : "GEGOKT: Raccoon"); return; }
    static char tekst[16384];
    size_t lengte = fread(tekst, 1, sizeof tekst - 1, f);
    fclose(f);
    tekst[lengte] = 0;
    char fout[160];
    rgu_cfg t = cfg;
    if (rgu_lees_ini(tekst, &t, fout, sizeof fout) != 0) {
        logf_("tabel %s AFGEKEURD (%s): de standaard geldt", pad, fout);
        return;
    }
    if (t.game != cfg.game) {            /* game = ... in het bestand: eerst de standaard van die game, dan het bestand er weer overheen */
        rgu_standaard(&cfg, t.game);
        rgu_lees_ini(tekst, &cfg, fout, sizeof fout);
    } else {
        cfg = t;
    }
    logf_("tabel: %s (game %s, usbio %d, aim %s %dx%d)", pad, cfg.game == RGU_GAME_EAI ? "eai" : "rr", cfg.usbio,
          cfg.aim_kolom ? "column" : "full", cfg.uit_b, cfg.uit_h);
}

static const volatile uint8_t *open_beeld(void)
{
    HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
    if (!m) return NULL;
    const volatile uint8_t *v = (const volatile uint8_t *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 64);
    if (v) logf_("bordbeeld TeknoParrot_JvsState geopend");
    return v;
}

static DWORD WINAPI lus(LPVOID arg)
{
    (void)arg;
    DWORD volgende_poging = 0, volgende_tel = GetTickCount() + 10000;
    while (!stoppen) {
        if (!kaart && GetTickCount() - volgende_poging < 0x80000000u) {
            kaart = open_beeld();
            volgende_poging = GetTickCount() + 500;
        }
        if (kaart) {
            rgu_beeld nieuw;
            memcpy(&nieuw.bits, (const void *)(kaart + 8), 4);
            memcpy(nieuw.as, (const void *)(kaart + 12), 8);
            EnterCriticalSection(&slot);
            beeld = nieuw;
            rgu_stap(&cfg, &staat, nieuw.bits);
            LeaveCriticalSection(&slot);
        }
        if (GetTickCount() - volgende_tel < 0x80000000u) {
            volgende_tel = GetTickCount() + 10000;
            logf_("telling: GetIoData=%ld GetESIoData=%ld GetGunData=%ld munt-aanroepen=%ld bits=0x%08x as=%u,%u,%u,%u munt=%d,%d,%d,%d%s",
                  (long)n_io, (long)n_es, (long)n_gun, (long)n_munt, (unsigned)beeld.bits, beeld.as[0], beeld.as[1], beeld.as[2],
                  beeld.as[3], staat.munt[0], staat.munt[1], staat.munt[2], staat.munt[3], kaart ? "" : " (GEEN bordbeeld)");
        }
        Sleep(2);
    }
    return 0;
}

static void start(void)
{
    EnterCriticalSection(&slot);
    int was = gestart;
    if (!gestart) {
        lees_tabel();
        memset(&staat, 0, sizeof staat);
        memset(&beeld, 0, sizeof beeld);
        gestart = 1;
    }
    LeaveCriticalSection(&slot);
    if (!was) {
        stoppen = 0;
        draad = CreateThread(NULL, 0, lus, NULL, 0, NULL);
        logf_("bord van RavenGun actief");
    }
}

static rgu_beeld momentopname(void)
{
    EnterCriticalSection(&slot);
    rgu_beeld b = beeld;
    LeaveCriticalSection(&slot);
    return b;
}

/* ---- het bord ---------------------------------------------------------------------------------------------------------------- */

EXPORT BOOLEAN BIND_InitUnisIo(void) { aanroep("BIND_InitUnisIo"); start(); return TRUE; }
EXPORT BOOLEAN BIND_IsIoWorking(void) { aanroep("BIND_IsIoWorking"); start(); return TRUE; }

/* Raccoon Rampage: FCoUnisIoData, 49 bytes. Elevator Action Invasion: FCoUnisIoData van 17 bytes (alleen via de blauwdruk; de C++ gebruikt
 * BIND_GetESIoData). Een game krijgt nooit meer bytes dan zijn eigen structuur groot is. */
EXPORT BOOLEAN BIND_GetIoData(uint8_t *uit)
{
    aanroep("BIND_GetIoData");
    if (!uit) return FALSE;
    start();
    InterlockedIncrement(&n_io);
    rgu_beeld b = momentopname();
    if (cfg.game == RGU_GAME_EAI) {
        uint8_t es[RGU_EAI_ES_LEN];
        rgu_bouw_eai_es(&cfg, &b, es);
        memset(uit, 0, RGU_EAI_IO_LEN);
        uit[0] = (uint8_t)cfg.usbio;
        memcpy(uit + 1, es, RGU_EAI_IO_LEN - 1);
    } else {
        rgu_bouw_rr(&cfg, &b, uit);
    }
    return TRUE;
}

EXPORT BOOLEAN BIND_OpenESIODevice(int poort) { aanroep("BIND_OpenESIODevice"); (void)poort; start(); return TRUE; }
EXPORT void BIND_CloseESIODevice(void) { aanroep("BIND_CloseESIODevice"); }
EXPORT BOOLEAN BIND_IsExistESIODevice(void) { aanroep("BIND_IsExistESIODevice"); start(); return TRUE; }

EXPORT BOOLEAN BIND_GetESIoData(uint8_t *uit)
{
    aanroep("BIND_GetESIoData");
    if (!uit) return FALSE;
    start();
    InterlockedIncrement(&n_es);
    rgu_beeld b = momentopname();
    rgu_bouw_eai_es(&cfg, &b, uit);
    return TRUE;
}

/* De geweren van EAI (HID): de DLL rekent zelf naar beeldpunten. OpenGunDevice(kalibratie-ini, reserve-ini, breedte, hoogte, bool). */
EXPORT BOOLEAN BIND_OpenGunDevice(const wchar_t *ini, const wchar_t *reserve, int b, int h, BOOLEAN vlag)
{
    aanroep("BIND_OpenGunDevice");
    (void)ini; (void)reserve; (void)vlag;
    if (b > 0 && h > 0) { spel_b = b; spel_h = h; }
    start();
    logf_("OpenGunDevice: spelbeeld %dx%d", spel_b, spel_h);
    return TRUE;
}

EXPORT int BIND_GetGunData(int speler, uint8_t *pakket)
{
    aanroep("BIND_GetGunData");
    if (!pakket || speler < 0 || speler > 1) return 0;
    start();
    InterlockedIncrement(&n_gun);
    rgu_beeld b = momentopname();
    rgu_eai_pakket(&cfg, &b, speler, spel_b, spel_h, pakket);
    return 1;
}

EXPORT int BIND_GetDeviceNumber(void) { aanroep("BIND_GetDeviceNumber"); return 2; }
EXPORT int BIND_GetMessage(void) { aanroep("BIND_GetMessage"); return 0; }
EXPORT int BIND_EnterCalibrate(int speler, int a, int b, int c) { aanroep("BIND_EnterCalibrate"); (void)speler; (void)a; (void)b; (void)c; return 1; }
EXPORT int BIND_GunSwap(void) { aanroep("BIND_GunSwap"); return 1; }

/* ---- munten (zoals het origineel: tellers per speler; de game rekent zelf het verschil) ------------------------------------------ */

EXPORT int BIND_GetCoinNumByPlayer(int speler)
{
    aanroep("BIND_GetCoinNumByPlayer");
    if (speler < 0 || speler > 3) return 0;
    InterlockedIncrement(&n_munt);
    EnterCriticalSection(&slot);
    int v = staat.munt[speler];
    LeaveCriticalSection(&slot);
    return v;
}

EXPORT int BIND_GetCoinNum(int speler)
{
    aanroep("BIND_GetCoinNum");
    return BIND_GetCoinNumByPlayer(speler);
}

EXPORT int BIND_AddKeyboardCoins(int speler, int n)
{
    aanroep("BIND_AddKeyboardCoins");
    if (speler < 0 || speler > 3) return 0;
    EnterCriticalSection(&slot);
    staat.munt[speler] += n;
    LeaveCriticalSection(&slot);
    return 1;
}

EXPORT int BIND_DecCoins(int speler, int n)
{
    aanroep("BIND_DecCoins");
    if (speler < 0 || speler > 3) return 0;
    EnterCriticalSection(&slot);
    staat.munt[speler] -= n;
    if (staat.munt[speler] < 0) staat.munt[speler] = 0;
    LeaveCriticalSection(&slot);
    return 1;
}

/* ---- de dongle: ja ---------------------------------------------------------------------------------------------------------- */

EXPORT BOOLEAN BIND_InitDongle(void) { aanroep("BIND_InitDongle"); return TRUE; }
EXPORT BOOLEAN BIND_IsDongleExist(void) { aanroep("BIND_IsDongleExist"); return TRUE; }
EXPORT BOOLEAN BIND_UnInitDongle(void) { aanroep("BIND_UnInitDongle"); return TRUE; }
EXPORT BOOLEAN BIND_IsDongleVerify(const uint8_t *sleutel) { aanroep("BIND_IsDongleVerify"); (void)sleutel; return TRUE; }
EXPORT int BIND_GetDongleVerifyLic(void) { aanroep("BIND_GetDongleVerifyLic"); return 1; }

/* ---- hulpfuncties van het origineel (dunne omhulsels om Win32; gezien in de disassemblage van de originele DLL) ------------------ */

/* (sectie, sleutel, bestand, uit[260], bool) -> GetPrivateProfileStringW met lege standaard en 0x104 tekens. */
EXPORT int BIND_GetPrivateProfileTCHAR(const wchar_t *sec, const wchar_t *sl, const wchar_t *bestand, wchar_t *uit, BOOLEAN vlag)
{
    aanroep("BIND_GetPrivateProfileTCHAR");
    (void)vlag;
    return (int)GetPrivateProfileStringW(sec, sl, L"", uit, 0x104, bestand);
}

EXPORT BOOLEAN BIND_WritePrivateProfileString(const wchar_t *sec, const wchar_t *sl, const wchar_t *waarde, const wchar_t *bestand, BOOLEAN vlag)
{
    aanroep("BIND_WritePrivateProfileString");
    (void)vlag;
    return WritePrivateProfileStringW(sec, sl, waarde, bestand) != 0;
}

EXPORT int BIND_GetUTCTime(void) { aanroep("BIND_GetUTCTime"); return (int)time(NULL); }

EXPORT double BIND_GetRunTime(BOOLEAN opnieuw)
{
    static LARGE_INTEGER freq;
    aanroep("BIND_GetRunTime");
    if (opnieuw || !freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000000.0 / (double)freq.QuadPart;
}

EXPORT int BIND_GetIP(wchar_t *uit)
{
    aanroep("BIND_GetIP");
    if (uit) { wcscpy(uit, L"127.0.0.1"); return 8; }
    return 0;
}

static int tchar_naar_char(const wchar_t *w, char *c)
{
    return w && c ? WideCharToMultiByte(CP_ACP, 0, w, -1, c, 0x104, NULL, NULL) : 0;
}

static int char_naar_tchar(const char *c, wchar_t *w)
{
    return c && w ? MultiByteToWideChar(CP_ACP, 0, c, -1, w, 0x104) : 0;
}

EXPORT int BIND_ConvertLPSTRToLPWSTR(const char *c, wchar_t *w) { return char_naar_tchar(c, w); }
EXPORT int BIND_ConvertLPWSTRToLPSTR(const wchar_t *w, char *c) { return tchar_naar_char(w, c); }
EXPORT int BIND_InterpretIP(const wchar_t *w, char *c) { return tchar_naar_char(w, c); }

/* ---- al het andere: stubs. Uitgangen (lampen, motoren, meters, geluid) zeggen "gelukt"; netwerk, kaartlezers en ticketautomaten
 *      bestaan niet (false/0). Alle namen van de originelen van EAI (75) en Raccoon (107) staan er, zodat GetDllExport nooit mislukt. */

#define STUB_JA(naam) EXPORT int64_t BIND_##naam(void) { aanroep("BIND_" #naam); return 1; }
#define STUB_NEE(naam) EXPORT int64_t BIND_##naam(void) { aanroep("BIND_" #naam); return 0; }

EXPORT int64_t BIND_ResetMachine(void) { aanroep("BIND_ResetMachine"); return 1; }
STUB_JA(SetDoorData) STUB_JA(SetLightVal) STUB_JA(SetGameLightVal) STUB_JA(SetGunShakeVal) STUB_JA(SetShakeFloorVal)
STUB_JA(SetSightseeingLiftLight) STUB_JA(SetTestCard) STUB_JA(SetCoinLock) STUB_JA(SetCoinsPerGame) STUB_JA(PlaySound)
STUB_JA(SetLocalTime) STUB_JA(SetSystemVolume) STUB_JA(SHDeleteFolder)
STUB_JA(SetAtmosphereLight_Color) STUB_JA(SetAtmosphereLight_State) STUB_JA(SetCardLight) STUB_JA(SetCardMeter_12P)
STUB_JA(SetCardMeter_34P) STUB_JA(SetCardOut) STUB_JA(SetCoinMeter) STUB_JA(SetConsoleLight_Color) STUB_JA(SetConsoleLight_State)
STUB_JA(SetDisinfection) STUB_JA(SetDrainage) STUB_JA(SetEnableTicketHalve) STUB_JA(SetGameState) STUB_JA(SetGear)
STUB_JA(SetGetReaderData) STUB_JA(SetGunLight_Color) STUB_JA(SetGunLight_State) STUB_JA(SetGunShock) STUB_JA(SetICO_1)
STUB_JA(SetICO_2) STUB_JA(SetLRSideLight_LR) STUB_JA(SetLRSideLight_State) STUB_JA(SetLackTicketLight_12P)
STUB_JA(SetLackTicketLight_34P) STUB_JA(SetLockCoin) STUB_JA(SetNewGunLight) STUB_JA(SetPedalLight_Color)
STUB_JA(SetPedalLight_State) STUB_JA(SetState) STUB_JA(SetTicketMeter) STUB_JA(SetTicketOut) STUB_JA(SetWingMotor)
STUB_NEE(ClearHeartbeatIndex) STUB_NEE(ClearUserId) STUB_NEE(CloseAmusementParkDevice) STUB_NEE(CloseZlbDevice)
STUB_NEE(GetArcTime) STUB_NEE(GetGameAuthNet) STUB_NEE(GetGameReportReplay) STUB_NEE(GetGameStartReplay) STUB_NEE(GetHeartbeatIndex)
STUB_NEE(GetPlayerData) STUB_NEE(GetRQCode) STUB_NEE(GetSystemVolume) STUB_NEE(GetWindowUserName) STUB_NEE(ICMPPing)
STUB_NEE(IsArcEnable) STUB_NEE(IsExistAmusementParkDevice) STUB_NEE(IsExistZlb) STUB_NEE(OpenAmusementParkDevice)
STUB_NEE(OpenZlbDevice) STUB_NEE(RequestArcTime) STUB_NEE(RequestRQCode) STUB_NEE(SendGameReportReplay) STUB_NEE(SendGameStart)
STUB_NEE(SendHeartbeatReplay) STUB_NEE(SendPlayerDataReplay) STUB_NEE(UpdateArcFault) STUB_NEE(UpdateDailyRevenue)
STUB_NEE(UpdateGameAuthLocal) STUB_NEE(UpdateGamePlay) STUB_NEE(UpdateMatchData) STUB_NEE(UpdatePlayerState)
STUB_NEE(DWGetCardReaderCount) STUB_NEE(DWGetCardReaderPortByIndex) STUB_NEE(DWInit) STUB_NEE(DWPopCardReaderString) STUB_NEE(DWUnInit)

BOOL WINAPI DllMain(HINSTANCE h, DWORD reden, LPVOID r)
{
    (void)r;
    if (reden == DLL_PROCESS_ATTACH) {
        deze = h;
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&slot);
    }
    return TRUE;
}
