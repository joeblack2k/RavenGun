/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-aliens-hook.dll: RavenGun-invoer voor Aliens Extermination (Global VR, "aliens dehasped.exe", PE32), zonder TeknoParrot.
 *
 * Geladen door onze eigen OpenParrot-lader (TP_PRELOAD_DLL, `Preload = true` in dropin.ini) in het spelproces. Doet drie dingen:
 *   1. winmm!joyGetPosEx(0/1) uit de importtabel van de game vervangen: speler 1 en 2 krijgen richten en knoppen uit het gedeelde
 *      geheugen `TeknoParrot_JvsState` (tp-shm-indeling AliensExtermination, door de daemon geschreven en door de pipehelper van
 *      rg-direct in de prefix gespiegeld). Het richten wordt op de EIGEN kalibratie van de game gezet (zie aliens_logic.h).
 *   2. user32!GetAsyncKeyState vervangen: munt, start, test, select en volume komen in de game uit het toetsenbord; wij zetten de
 *      toets ook "ingedrukt" als de bijbehorende bit in het gedeelde geheugen staat. Een echte toets blijft werken.
 *   3. (alleen met kloppende bytes) de twee tellers van het opstartscherm ("GVRIO CARD FOUND", "2 ALIENS GUNS FOUND") beantwoorden.
 *
 * Er wordt niets uit de HID-apparaten gelezen door de game, dus er wordt hier geen HID nagebouwd. Geen C-runtime: alleen kernel32
 * en user32 (wsprintfA).
 *
 * Omgeving (dropin.ini `Env`):
 *   RG_ALIENS_LOG         bestand voor het logboek (aanbevolen bij de eerste proef); leeg = geen logboek
 *   RG_ALIENS_INVERT_X/Y  "1": richting omdraaien (als het vizier op de tv gespiegeld loopt)
 *   RG_ALIENS_NO_IOFIX    "1": de tellers van het opstartscherm met rust laten
 *   RG_ALIENS_NO_CAL      "1": de kalibratie van de game niet lezen, altijd het bereik 0..65535
 */
#include <windows.h>
#include <stdarg.h>
#include "aliens_logic.h"

static HANDLE g_log;
static const volatile unsigned char* g_shm;
static DWORD g_shm_next_try;
static int g_inv_x, g_inv_y, g_use_cal = 1, g_game_ok;
static MMRESULT(WINAPI* g_real_joy)(UINT, LPJOYINFOEX);
static SHORT(WINAPI* g_real_gaks)(int);
static LONG g_joy_logged[2];
static LONG g_key_logged[AL_NKEYS];

static void logf2(const char* fmt, ...) {
    char buf[300];
    DWORD n;
    va_list ap;
    if (g_log == INVALID_HANDLE_VALUE || !g_log) return;
    va_start(ap, fmt);
    n = (DWORD)wvsprintfA(buf, fmt, ap);
    va_end(ap);
    buf[n++] = '\r';
    buf[n++] = '\n';
    WriteFile(g_log, buf, n, &n, NULL);
}

static int env_on(const char* name) {
    char v[8];
    return GetEnvironmentVariableA(name, v, sizeof v) > 0 && v[0] == '1';
}

/* De 64 bytes, of NULL als de daemon (nog) niets schrijft. Opent de mapping hooguit eens per seconde. */
static const volatile unsigned char* shm(void) {
    if (!g_shm) {
        HANDLE h;
        const DWORD now = GetTickCount();
        if ((LONG)(now - g_shm_next_try) < 0) return NULL;
        g_shm_next_try = now + 1000;
        h = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (!h) return NULL;
        g_shm = (const volatile unsigned char*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, 64);
        if (g_shm) logf2("tp-shm: TeknoParrot_JvsState gekoppeld");
    }
    return g_shm;
}

static uint32_t ctl_now(void) {
    unsigned char b[12];
    const volatile unsigned char* s = shm();
    int i;
    if (!s) return 0;
    for (i = 0; i < 12; ++i) b[i] = s[i];
    return al_ctl(b);
}

static float rd_float(DWORD va) {
    return *(const volatile float*)(ULONG_PTR)va;
}

static SHORT WINAPI hk_GetAsyncKeyState(int vk) {
    SHORT r = g_real_gaks ? g_real_gaks(vk) : 0;
    if (vk >= 0 && vk < 256) {
        int i;
        for (i = 0; i < AL_NKEYS; ++i)
            if (al_keys[i].vk == (unsigned)vk) {
                if (al_key_down(ctl_now(), (unsigned)vk)) {
                    if (!InterlockedExchange(&g_key_logged[i], 1)) logf2("toets: %s (VK 0x%02X) komt uit tp-shm", al_keys[i].name, vk);
                    return (SHORT)(r | 0x8001);
                }
                break;
            }
    }
    return r;
}

static MMRESULT WINAPI hk_joyGetPosEx(UINT id, LPJOYINFOEX pji) {
    const volatile unsigned char* s;
    unsigned char b[32];
    uint32_t ctl, btn;
    float xlo = 0, xhi = 0, ylo = 0, yhi = 0;
    int i;
    if (!pji) return JOYERR_PARMS;
    if (id > 1) return AL_JOY_UNPLUGGED;          /* alleen de twee geweren; geen echte joystick laten meedoen */
    s = shm();
    if (!s) return AL_JOY_UNPLUGGED;
    for (i = 0; i < 32; ++i) b[i] = s[i];
    ctl = al_ctl(b);
    btn = al_joy_buttons(ctl, (int)id);
    if (g_use_cal && g_game_ok) {
        const DWORD off = id * AL_PLAYER_STRIDE;
        xlo = rd_float(AL_CAL_X_VA + off);
        xhi = rd_float(AL_CAL_X_VA + off + 4);
        ylo = rd_float(AL_CAL_Y_VA + off);
        yhi = rd_float(AL_CAL_Y_VA + off + 4);
    }
    pji->dwXpos = al_aim_raw(b[12 + 8 * id], xlo, xhi, g_inv_x);
    pji->dwYpos = al_aim_raw(b[16 + 8 * id], ylo, yhi, g_inv_y);
    pji->dwZpos = pji->dwRpos = pji->dwUpos = pji->dwVpos = 32767;
    pji->dwButtons = btn;
    pji->dwButtonNumber = (DWORD)al_popcount(btn);
    pji->dwPOV = 0xFFFF;                          /* JOY_POVCENTERED */
    if (!InterlockedExchange(&g_joy_logged[id], 1))
        logf2("joyGetPosEx(%u): eerste aanroep; kalibratie x %d..%d y %d..%d (%s), ruw %u,%u",
              id, (int)xlo, (int)xhi, (int)ylo, (int)yhi, al_cal_ok(xlo, xhi) && al_cal_ok(ylo, yhi) ? "van de game" : "bereik 0..65535",
              (unsigned)pji->dwXpos, (unsigned)pji->dwYpos);
    return JOYERR_NOERROR;
}

static int patch_slot(unsigned char* base, uint32_t rva, void* fn, void** old) {
    void** slot = (void**)(base + rva);
    DWORD prot;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &prot)) return 0;
    if (old) *old = *slot;
    *slot = fn;
    VirtualProtect(slot, sizeof *slot, prot, &prot);
    return 1;
}

static void io_fix(unsigned char* base) {
    struct { uint32_t rva, counter; unsigned count; const char* what; } t[2] = {
        { AL_GETTER_GUNS_RVA, AL_COUNTER_GUNS_VA, 2, "geweren" }, { AL_GETTER_CARD_RVA, AL_COUNTER_CARD_VA, 1, "GVRIO-kaart" } };
    int i;
    for (i = 0; i < 2; ++i) {
        unsigned char* p = base + t[i].rva;
        DWORD prot;
        if (!al_getter_matches(p, t[i].counter)) { logf2("opstartscherm: teller %s: bytes kloppen niet, met rust gelaten", t[i].what); continue; }
        if (!VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &prot)) continue;
        al_getter_patch(p, t[i].count);
        VirtualProtect(p, 6, prot, &prot);
        FlushInstructionCache(GetCurrentProcess(), p, 6);
        logf2("opstartscherm: teller %s -> %u", t[i].what, t[i].count);
    }
}

/* De exe heeft geen OriginalFirstThunk (OFT = 0, gemeten 2026-10-17): na het laden staan in de IAT alleen adressen, geen namen meer,
 * dus al_find_import vindt niets. Dan de plek zoeken waar het adres van de functie zelf staat. */
static uint32_t find_import_by_addr(unsigned char* base, size_t size, const char* dll, const char* fn) {
    HMODULE m = GetModuleHandleA(dll);
    FARPROC want = m ? GetProcAddress(m, fn) : NULL;
    uint32_t lf = *(uint32_t*)(base + 0x3c), imp = *(uint32_t*)(base + lf + 24 + 0x68), d;
    if (!want || !imp) return 0;
    for (d = imp; d + 20 <= size; d += 20) {
        uint32_t nm = *(uint32_t*)(base + d + 12), ft = *(uint32_t*)(base + d + 16), t;
        if (!nm) break;
        if (nm >= size || ft >= size || !al_ieq((const char*)base + nm, dll)) continue;
        for (t = ft; t + 4 <= size && *(uint32_t*)(base + t); t += 4)
            if (*(uint32_t*)(base + t) == (uint32_t)(uintptr_t)want) return t;
    }
    return 0;
}

static DWORD WINAPI install(LPVOID unused) {
    unsigned char* base = (unsigned char*)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    size_t size = nt->OptionalHeader.SizeOfImage;
    uint32_t rva;
    void* old = NULL;
    (void)unused;
    logf2("rg-aliens-hook: hoofdmodule %p, ImageBase 0x%08X, SizeOfImage 0x%X", (void*)base, (unsigned)nt->OptionalHeader.ImageBase, (unsigned)size);

    /* Is dit de bekende exe? Dan mogen we zijn kalibratie lezen en het opstartscherm bijwerken. */
    /* SizeOfImage is een RVA-grootte: de importplekken liggen op RVA 0x55892b0..b8 (VA 0x59892b0..b8); 0x5989000 was de VA */
    g_game_ok = base == (unsigned char*)0x400000 && size > 0x5589000u &&
                al_getter_matches(base + AL_GETTER_GUNS_RVA, AL_COUNTER_GUNS_VA) && al_getter_matches(base + AL_GETTER_CARD_RVA, AL_COUNTER_CARD_VA);
    logf2("bekende exe (adressen en bytes): %s", g_game_ok ? "ja" : "NEE (alleen hoofdinvoer, geen kalibratie, geen opstartscherm)");

    rva = al_find_import(base, size, "winmm.dll", "joyGetPosEx");
    if (!rva) rva = find_import_by_addr(base, size, "winmm.dll", "joyGetPosEx");
    if (rva && patch_slot(base, rva, (void*)hk_joyGetPosEx, &old)) { g_real_joy = (MMRESULT(WINAPI*)(UINT, LPJOYINFOEX))old; logf2("joyGetPosEx: importplek RVA 0x%X omgezet", (unsigned)rva); }
    else {
        /* Meten in plaats van raden: wat staat er in het beeld waar de importtabel hoort (2026-10-17: niet gevonden op de tv). */
        uint32_t lf = *(uint32_t*)(base + 0x3c), imp = *(uint32_t*)(base + lf + 24 + 0x68), d;
        logf2("joyGetPosEx: importplek NIET gevonden of niet te beschrijven (rva %X); importtabel op RVA 0x%X", (unsigned)rva, (unsigned)imp);
        for (d = imp; imp && d + 20 <= size && d < imp + 20 * 40; d += 20) {
            uint32_t nm = *(uint32_t*)(base + d + 12);
            if (!nm) break;
            logf2("  import: %.40s (OFT 0x%X, FT 0x%X)", nm < size ? (const char*)base + nm : "?", *(unsigned*)(base + d), *(unsigned*)(base + d + 16));
        }
    }
    old = NULL;
    rva = al_find_import(base, size, "user32.dll", "GetAsyncKeyState");
    if (!rva) rva = find_import_by_addr(base, size, "user32.dll", "GetAsyncKeyState");
    if (rva && patch_slot(base, rva, (void*)hk_GetAsyncKeyState, &old)) { g_real_gaks = (SHORT(WINAPI*)(int))old; logf2("GetAsyncKeyState: importplek RVA 0x%X omgezet", (unsigned)rva); }
    else logf2("GetAsyncKeyState: importplek NIET gevonden of niet te beschrijven");

    if (g_game_ok && !env_on("RG_ALIENS_NO_IOFIX")) io_fix(base);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID reserved) {
    (void)reserved;
    if (why == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH];
        DisableThreadLibraryCalls(h);
        g_log = INVALID_HANDLE_VALUE;
        if (GetEnvironmentVariableA("RG_ALIENS_LOG", path, sizeof path) > 0)
            g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        g_inv_x = env_on("RG_ALIENS_INVERT_X");
        g_inv_y = env_on("RG_ALIENS_INVERT_Y");
        g_use_cal = !env_on("RG_ALIENS_NO_CAL");
        /* Direct, in de laadfunctie van de lader: de importtabel van de game is al ingevuld, het spel zelf draait nog niet of net. */
        install(NULL);
    }
    return TRUE;
}
