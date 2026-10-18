/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-drakons: RavenGuns eigen RS_232_DLL.dll voor Drakon's Realm Keepers (Adrenaline/Sarbakan, Unity 2021.3 IL2CPP, 64-bits).
 *
 * WAAROM DEZE DLL. De game heeft geen invoerbord: RS_232_DLL.dll is alleen de seriele laag onder de zitbank (PneumaticSeatController)
 * en de ledbesturing (SerialProtocol_Leds); de aanwijzing komt uit het Unity Input System (InputsManager: Mode Turret / Mouse /
 * Gamepad; Mouse = fireAction + moveAction, een gewone muis). Op de gesloten kern kreeg de game zijn muis uit het bordbeeld. Zonder
 * die kern ([Loader] Core = none) leest de game de echte apparaten van Wine, en de virtuele lightgun van RavenGun is voor het
 * systeem onzichtbaar (LIBINPUT_IGNORE_DEVICE). Deze DLL vult dat gat:
 *
 *   1. de RS232_*-functies bestaan en melden "geen poort" (RS232_OpenComport geeft 1 = mislukt, RS232_GetPortIndexByName -1,
 *      Poll 0): precies wat de game zonder zitbank en ledbord al zag ("PneumaticSeat controller not found");
 *   2. de DLL start een draad die het bordbeeld van RavenGun leest (de Windows-mapping "TeknoParrot_JvsState" van rg-jvsbridge.exe
 *      in de prefix; 32-bits bitveld op offset 8, assen van speler 1 op offset 12/13, indeling WartranTroopers) en
 *        - de muis van speler 1 verplaatst naar het aanwijspunt (SetCursorPos + SendInput absoluut, in het clientgebied van het
 *          Unity-venster van dit proces),
 *        - de linkermuisknop indrukt zolang de trekker (bit 7) aan staat,
 *        - optioneel (toetsen = 1) munt en start als toetsenbordtoets doorgeeft (standaard uit: het virtuele toetsenbord van
 *          RavenGun levert die toetsen al aan het systeem).
 *
 * CLEAN-ROOM: de exporttabel van de originele DLL (objdump -p: 21 namen, de bekende RS-232-bibliotheek voor Windows) en de
 * dump van de game (InputsManager); geen code van de originele DLL.
 *
 * rg-drakons.ini naast de DLL (alle sleutels optioneel):  muis = 1  toetsen = 0  trekker_bit = 7  munt_bit = 2  start_bit = 6
 *   x_offset = 12  y_offset = 13  munt_toets = 53 (VK '5')  start_toets = 49 (VK '1')  interval_ms = 4
 * RG_DRAKONS_LOG=pad schrijft een logboek (relatief pad = relatief aan de map van deze DLL).
 */
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __declspec(dllexport)
#define BEELD_GROOTTE 64

static HMODULE deze;
static HANDLE draad;
static volatile LONG stoppen;

static struct {
    int muis, toetsen, trekker_bit, munt_bit, start_bit, x_off, y_off, munt_vk, start_vk, interval, haak, trekker2_bit, x2_off, y2_off;
} cfg = {1, 0, 7, 2, 6, 12, 13, '5', '1', 4, 1, 12, 14, 15};

static void logf_(const char *fmt, ...)
{
    char regel[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(regel, sizeof regel - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof regel - 2) n = (int)sizeof regel - 2;
    regel[n] = '\n';
    regel[n + 1] = 0;
    const char *pad = getenv("RG_DRAKONS_LOG");
    char vol[MAX_PATH + 64];
    if (pad && *pad && !(pad[0] == '\\' || pad[0] == '/' || (pad[0] && pad[1] == ':'))) {
        DWORD l = GetModuleFileNameA(deze, vol, MAX_PATH);
        char *schuin = l ? strrchr(vol, '\\') : NULL;
        if (schuin && strlen(pad) < 60) { strcpy(schuin + 1, pad); pad = vol; }
    }
    FILE *f = pad && *pad ? fopen(pad, "a") : NULL;
    if (f) { fprintf(f, "[%lu] rg-drakons: %s", (unsigned long)GetTickCount(), regel); fclose(f); }
    else { OutputDebugStringA("rg-drakons: "); OutputDebugStringA(regel); }
}

/* Een sleutel = getal in rg-drakons.ini; onbekende sleutels worden genegeerd, een getal buiten bereik wordt afgekeurd. */
static void lees_ini(void)
{
    char pad[MAX_PATH + 32];
    DWORD n = GetModuleFileNameA(deze, pad, MAX_PATH);
    char *schuin = n ? strrchr(pad, '\\') : NULL;
    if (!schuin) return;
    strcpy(schuin + 1, "rg-drakons.ini");
    FILE *f = fopen(pad, "rb");
    if (!f) { logf_("ini: %s ontbreekt, de standaard", pad); return; }
    char regel[256];
    while (fgets(regel, sizeof regel, f)) {
        char *p = regel;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '#' || *p == '\r' || *p == '\n' || !*p) continue;
        char *is = strchr(p, '=');
        if (!is) continue;
        *is = 0;
        char *k = p;
        char *e = is - 1;
        while (e >= k && (*e == ' ' || *e == '\t')) *e-- = 0;
        char *eind = NULL;
        long w = strtol(is + 1, &eind, 10);
        if (eind == is + 1) { logf_("ini: waarde bij '%s' is geen getal, genegeerd", k); continue; }
        int *doel = NULL;
        int lo = 0, hi = 255;
        if (!strcmp(k, "muis")) { doel = &cfg.muis; hi = 1; }
        else if (!strcmp(k, "toetsen")) { doel = &cfg.toetsen; hi = 1; }
        else if (!strcmp(k, "trekker_bit")) { doel = &cfg.trekker_bit; hi = 31; }
        else if (!strcmp(k, "munt_bit")) { doel = &cfg.munt_bit; hi = 31; }
        else if (!strcmp(k, "start_bit")) { doel = &cfg.start_bit; hi = 31; }
        else if (!strcmp(k, "x_offset")) { doel = &cfg.x_off; hi = BEELD_GROOTTE - 1; }
        else if (!strcmp(k, "y_offset")) { doel = &cfg.y_off; hi = BEELD_GROOTTE - 1; }
        else if (!strcmp(k, "munt_toets")) { doel = &cfg.munt_vk; lo = 1; }
        else if (!strcmp(k, "start_toets")) { doel = &cfg.start_vk; lo = 1; }
        else if (!strcmp(k, "haak")) { doel = &cfg.haak; hi = 1; }
        else if (!strcmp(k, "trekker2_bit")) { doel = &cfg.trekker2_bit; hi = 31; }
        else if (!strcmp(k, "x2_offset")) { doel = &cfg.x2_off; hi = BEELD_GROOTTE - 1; }
        else if (!strcmp(k, "y2_offset")) { doel = &cfg.y2_off; hi = BEELD_GROOTTE - 1; }
        else if (!strcmp(k, "interval_ms")) { doel = &cfg.interval; lo = 1; hi = 100; }
        if (!doel) continue;
        if (w < lo || w > hi) { logf_("ini: %s = %ld buiten %d..%d, genegeerd", k, w, lo, hi); continue; }
        *doel = (int)w;
    }
    fclose(f);
    logf_("ini: %s muis=%d toetsen=%d trekker_bit=%d munt_bit=%d start_bit=%d x=%d y=%d interval=%d", pad, cfg.muis, cfg.toetsen,
          cfg.trekker_bit, cfg.munt_bit, cfg.start_bit, cfg.x_off, cfg.y_off, cfg.interval);
}

/* ---- het Unity-venster van dit proces ---- */
struct zoek { DWORD pid; HWND venster; };

static BOOL CALLBACK zoek_venster(HWND h, LPARAM lp)
{
    struct zoek *z = (struct zoek *)lp;
    DWORD pid = 0;
    char klasse[64];
    GetWindowThreadProcessId(h, &pid);
    if (pid != z->pid || !IsWindowVisible(h)) return TRUE;
    if (GetClassNameA(h, klasse, sizeof klasse) && !strcmp(klasse, "UnityWndClass")) { z->venster = h; return FALSE; }
    return TRUE;
}

static HWND unity_venster(void)
{
    struct zoek z = {GetCurrentProcessId(), NULL};
    EnumWindows(zoek_venster, (LPARAM)&z);
    return z.venster;
}

/* Beeldpunt in schermcoördinaten uit een as 0..255 (links/boven = 0); <=1 en >=254 is "buiten beeld" en klemt aan de rand. */
static int as_naar_pixel(uint8_t v, int begin, int breedte)
{
    if (breedte < 2) return begin;
    int p = v <= 1 ? 0 : (v >= 254 ? 255 : v);
    return begin + (int)(((int64_t)p * (breedte - 1) + 127) / 255);
}

static void muis_naar(int sx, int sy)
{
    SetCursorPos(sx, sy);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type = INPUT_MOUSE;
    in.mi.dx = sw > 1 ? (LONG)(((int64_t)sx * 65535) / (sw - 1)) : 0;
    in.mi.dy = sh > 1 ? (LONG)(((int64_t)sy * 65535) / (sh - 1)) : 0;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &in, sizeof in);
}

static void muisknop(int omlaag)
{
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = omlaag ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof in);
}

static void toets(int vk, int omlaag)
{
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)vk;
    in.ki.wScan = (WORD)MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = omlaag ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof in);
}

/* ---- de haak in InputsManager.Update (IL2CPP) ----
 * Gemeten aan GameAssembly.dll (imagebase 0x180000000, il2cpp-dump + objdump): InputsManager.Update (RVA 0x3F0100) loopt over playersData
 * (veld +0x28, array: lengte +0x18, elementen +0x20); een PlayerData heeft ID (+0x10) en InputMode (+0x14; 0 Turret, 1 Mouse, 2 Gamepad). Zonder
 * zitbank is de modus Mouse (PlayerData-ctor RVA 0x3F2D30 kiest Turret alleen als de zitbank bestaat). In Mouse-modus leest Update de OUDE Unity-
 * invoer (Input.GetMouseButton(ID) en Input.mousePosition, icalls op RVA 0x16C4EB0 / 0x16C50B0), zet de muis om naar -1..1 en roept
 * SendButtonEvent(this, ID, schiet) (RVA 0x3EFBE0) en SendPositionEvent(this, ID, &Vector3) (RVA 0x3EFCC0). SendInput kwam daar in Wine niet
 * aan (tv-ronde 43: de trekker veranderde niets). Daarom vervangen we Update zelf: dezelfde twee aanroepen, maar met het bordbeeld als bron. */
#define RVA_UPDATE 0x3F0100u
#define RVA_SENDBUTTON 0x3EFBE0u
#define RVA_SENDPOS 0x3EFCC0u
typedef struct { float x, y, z; } Vec3;
typedef void (*FnSendButton)(void *self, int speler, unsigned char schiet, void *mi);
typedef void (*FnSendPos)(void *self, int speler, Vec3 *pos, void *mi);
static const volatile uint8_t *g_beeld;
static FnSendButton g_sendbutton;
static FnSendPos g_sendpos;
static volatile LONG g_haak_aan;
static volatile LONG g_haak_aanroepen;

static float as_ndc(uint8_t v, int omkeren)
{
    int p = v <= 1 ? 0 : (v >= 254 ? 255 : v);
    float f = (float)p / 255.0f * 2.0f - 1.0f;
    return omkeren ? -f : f;
}

static void haak_update(void *self, void *mi)
{
    (void)mi;
    LONG n = InterlockedIncrement(&g_haak_aanroepen);
    char *rij = *(char **)((char *)self + 0x28);
    if (!rij) return;
    int lengte = *(int *)(rij + 0x18);
    const volatile uint8_t *b = g_beeld;
    static int vorige[2] = {-1, -1};
    for (int i = 0; i < lengte; i++) {
        char *pd = *(char **)(rij + 0x20 + 8 * i);
        if (!pd || *(int *)(pd + 0x14) != 1) continue;
        int id = *(int *)(pd + 0x10);
        int schiet = 0;
        Vec3 v = {0.0f, 0.0f, 0.0f};
        if (b && (id == 0 || id == 1)) {
            uint32_t bits;
            memcpy(&bits, (const void *)(b + 8), 4);
            schiet = (int)((bits >> (id == 0 ? cfg.trekker_bit : cfg.trekker2_bit)) & 1);
            v.x = as_ndc(b[id == 0 ? cfg.x_off : cfg.x2_off], 0);
            v.y = as_ndc(b[id == 0 ? cfg.y_off : cfg.y2_off], 1);   /* bordbeeld: boven = 0; Unity: onder = -1 */
        }
        g_sendbutton(self, id, (unsigned char)schiet, NULL);
        g_sendpos(self, id, &v, NULL);
        if (id < 2 && vorige[id] != schiet) {
            vorige[id] = schiet;
            logf_("haak: speler %d schiet=%d pos %.2f,%.2f (aanroep %ld)", id, schiet, v.x, v.y, (long)n);
        }
    }
}

/* Schrijft 14 bytes (jmp [rip+0]; adres) op het begin van Update, eerst de staart en dan het eerste aangelijnde woord in een keer. */
static int zet_haak(void)
{
    HMODULE ga = GetModuleHandleA("GameAssembly.dll");
    if (!ga) return 0;
    unsigned char *basis = (unsigned char *)ga;
    unsigned char *doel = basis + RVA_UPDATE;
    static const unsigned char verwacht[16] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x57};
    if (memcmp(doel, verwacht, 16) != 0) {
        logf_("haak: bytes op GameAssembly+0x%x kloppen niet (andere build); GEEN haak, terugval op SendInput", RVA_UPDATE);
        return -1;
    }
    g_sendbutton = (FnSendButton)(basis + RVA_SENDBUTTON);
    g_sendpos = (FnSendPos)(basis + RVA_SENDPOS);
    DWORD was;
    if (!VirtualProtect(doel, 16, PAGE_EXECUTE_READWRITE, &was)) { logf_("haak: VirtualProtect mislukt (%lu)", (unsigned long)GetLastError()); return -1; }
    uint64_t adres = (uint64_t)(uintptr_t)&haak_update;
    unsigned char nieuw[16];
    memcpy(nieuw, doel, 16);
    nieuw[0] = 0xFF; nieuw[1] = 0x25; nieuw[2] = nieuw[3] = nieuw[4] = nieuw[5] = 0;
    memcpy(nieuw + 6, &adres, 8);
    uint64_t staart, kop;
    memcpy(&staart, nieuw + 8, 8);
    memcpy(&kop, nieuw, 8);
    InterlockedExchange64((volatile LONG64 *)(doel + 8), (LONG64)staart);
    InterlockedExchange64((volatile LONG64 *)doel, (LONG64)kop);
    VirtualProtect(doel, 16, was, &was);
    FlushInstructionCache(GetCurrentProcess(), doel, 16);
    InterlockedExchange(&g_haak_aan, 1);
    logf_("haak: InputsManager.Update (GameAssembly %p + 0x%x) omgeleid naar onze functie", (void *)basis, RVA_UPDATE);
    return 1;
}

static const volatile uint8_t *open_beeld(void)
{
    HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
    if (!m) return NULL;
    const volatile uint8_t *v = (const volatile uint8_t *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, BEELD_GROOTTE);
    if (v) logf_("bordbeeld geopend");
    return v;
}

static DWORD WINAPI lus(LPVOID arg)
{
    (void)arg;
    const volatile uint8_t *beeld = NULL;
    HWND venster = NULL;
    int gezien = 0, laatste_x = -1, laatste_y = -1, trekker = 0, munt = 0, start = 0, aantal_bewegingen = 0, aantal_knop = 0;
    uint32_t bits_vorig = 0;
    DWORD volgende_poging = 0, volgende_venster = 0;
    int venster_gemeld = 0, haak_gepoogd = 0;
    DWORD volgende_haak = 0;

    while (!stoppen) {
        DWORD nu = GetTickCount();
        if (!beeld && (LONG)(nu - volgende_poging) >= 0) { beeld = open_beeld(); g_beeld = beeld; volgende_poging = nu + 500; }
        if (cfg.haak && !haak_gepoogd && beeld && (LONG)(nu - volgende_haak) >= 0) {
            int r = zet_haak();
            if (r != 0) haak_gepoogd = 1;
            volgende_haak = nu + 250;
        }
        if (beeld) {
            uint32_t bits;
            memcpy(&bits, (const void *)(beeld + 8), 4);
            uint8_t ax = beeld[cfg.x_off], ay = beeld[cfg.y_off];
            if (!gezien) {
                /* Het eerste beeld is de uitgangsstand, geen beweging: anders springt de muis naar een hoek zodra de game start. */
                gezien = 1;
                laatste_x = ax; laatste_y = ay; bits_vorig = bits;
                trekker = (bits >> cfg.trekker_bit) & 1; munt = (bits >> cfg.munt_bit) & 1; start = (bits >> cfg.start_bit) & 1;
                logf_("eerste beeld: bits=%08x x=%u y=%u", (unsigned)bits, ax, ay);
            } else {
                if (bits != bits_vorig) { logf_("bits %08x -> %08x", (unsigned)bits_vorig, (unsigned)bits); bits_vorig = bits; }
                if (!venster && (LONG)(nu - volgende_venster) >= 0) { venster = unity_venster(); volgende_venster = nu + 500; }
                if (venster && !IsWindow(venster)) venster = NULL;
                if (venster && !venster_gemeld) {
                    RECT r;
                    GetClientRect(venster, &r);
                    logf_("venster %p client %ldx%ld, voorgrond %s", (void *)venster, r.right, r.bottom,
                          GetForegroundWindow() == venster ? "ja" : "nee");
                    venster_gemeld = 1;
                }
                if (cfg.muis && venster && !g_haak_aan) {
                    if (ax != laatste_x || ay != laatste_y) {
                        RECT r;
                        POINT o = {0, 0};
                        GetClientRect(venster, &r);
                        ClientToScreen(venster, &o);
                        int sx = as_naar_pixel(ax, o.x, r.right), sy = as_naar_pixel(ay, o.y, r.bottom);
                        muis_naar(sx, sy);
                        laatste_x = ax; laatste_y = ay;
                        if (aantal_bewegingen < 5 || aantal_bewegingen % 500 == 0) {
                            POINT g;
                            GetCursorPos(&g);
                            logf_("muis #%d: as %u,%u -> scherm %d,%d (cursor nu %ld,%ld)", aantal_bewegingen, ax, ay, sx, sy, g.x, g.y);
                        }
                        aantal_bewegingen++;
                    }
                    int t = (bits >> cfg.trekker_bit) & 1;
                    if (t != trekker) {
                        trekker = t;
                        muisknop(t);
                        if (aantal_knop++ < 10) logf_("trekker %s", t ? "omlaag" : "omhoog");
                    }
                }
                if (cfg.toetsen) {
                    int m = (bits >> cfg.munt_bit) & 1, s = (bits >> cfg.start_bit) & 1;
                    if (m != munt) { munt = m; toets(cfg.munt_vk, m); logf_("munttoets %s", m ? "omlaag" : "omhoog"); }
                    if (s != start) { start = s; toets(cfg.start_vk, s); logf_("starttoets %s", s ? "omlaag" : "omhoog"); }
                }
            }
        }
        Sleep((DWORD)cfg.interval);
    }
    if (trekker) muisknop(0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reden, LPVOID res)
{
    (void)res;
    if (reden == DLL_PROCESS_ATTACH) {
        deze = (HMODULE)inst;
        DisableThreadLibraryCalls(inst);
        lees_ini();
        logf_("geladen in pid %lu", (unsigned long)GetCurrentProcessId());
        stoppen = 0;
        draad = CreateThread(NULL, 0, lus, NULL, 0, NULL);
    } else if (reden == DLL_PROCESS_DETACH && !res) {
        InterlockedExchange(&stoppen, 1);
        if (draad) { WaitForSingleObject(draad, 500); CloseHandle(draad); draad = NULL; }
    }
    return TRUE;
}

/* ---- de seriele laag: geen poorten ---- */
EXPORT int __cdecl RS232_OpenComport(int poort, int baud, const char *modus, int flow) { (void)poort; (void)baud; (void)modus; (void)flow; return 1; }
EXPORT int __cdecl RS232_PollComport(int poort, unsigned char *buf, int grootte) { (void)poort; (void)buf; (void)grootte; return 0; }
EXPORT int __cdecl RS232_SendByte(int poort, unsigned char b) { (void)poort; (void)b; return 1; }
EXPORT int __cdecl RS232_SendBuf(int poort, unsigned char *buf, int grootte) { (void)poort; (void)buf; (void)grootte; return -1; }
EXPORT void __cdecl RS232_CloseComport(int poort) { (void)poort; }
EXPORT void __cdecl RS232_cputs(int poort, const char *tekst) { (void)poort; (void)tekst; }
EXPORT int __cdecl RS232_GetPortnr(const char *naam) { (void)naam; return -1; }
/* Gemeten aan de originele DLL (objdump, RVA 0x1080): een lineaire, exacte vergelijking met een tabel van 48 namen "\\.\COM1".."\\.\COM48";
 * gevonden = de 0-gebaseerde plaats, anders 48 (de tabellengte), NOOIT -1. De game gebruikt het getal als index in een array
 * (SBK.RS232.RS232DLL.UsedPort): -1 gaf in tv-ronde 40 "IndexOutOfRangeException" in RS232DLL.Open en hield het laden tegen. De game geeft
 * de naam zonder voorvoegsel ("COM2") door, dus het origineel gaf 48 en de game sloeg die poort over. */
EXPORT int __cdecl RS232_GetPortIndexByName(const char *naam)
{
    if (naam && !strncmp(naam, "\\\\.\\COM", 7)) {
        int n = 0, i = 7;
        if (naam[i] < '1' || naam[i] > '9') return 48;
        while (naam[i] >= '0' && naam[i] <= '9' && n < 1000) n = n * 10 + (naam[i++] - '0');
        if (!naam[i] && n >= 1 && n <= 48) return n - 1;
    }
    return 48;
}
EXPORT int __cdecl RS232_IsDCDEnabled(int poort) { (void)poort; return 0; }
EXPORT int __cdecl RS232_IsRINGEnabled(int poort) { (void)poort; return 0; }
EXPORT int __cdecl RS232_IsCTSEnabled(int poort) { (void)poort; return 0; }
EXPORT int __cdecl RS232_IsDSREnabled(int poort) { (void)poort; return 0; }
EXPORT void __cdecl RS232_enableDTR(int poort) { (void)poort; }
EXPORT void __cdecl RS232_disableDTR(int poort) { (void)poort; }
EXPORT void __cdecl RS232_enableRTS(int poort) { (void)poort; }
EXPORT void __cdecl RS232_disableRTS(int poort) { (void)poort; }
EXPORT void __cdecl RS232_enableBREAK(int poort) { (void)poort; }
EXPORT void __cdecl RS232_disableBREAK(int poort) { (void)poort; }
EXPORT void __cdecl RS232_flushRX(int poort) { (void)poort; }
EXPORT void __cdecl RS232_flushTX(int poort) { (void)poort; }
EXPORT void __cdecl RS232_flushRXTX(int poort) { (void)poort; }
