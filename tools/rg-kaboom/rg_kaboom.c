/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-kaboom.dll: een nagebootst Kaboom-bord (munten, toetsenblok, kaartjes) voor Infinity Blade FX, zonder TeknoParrot.
 *
 * SwordGame.exe heeft de Kaboom-bibliotheek (CKaboomCom) ingebakken en praat via kernel32 met een seriele poort (\\.\COM9,
 * 57600 baud; het pakketprotocol staat in kaboom_proto.h). Deze DLL wordt als drop-in met de OpenParrot-lader (TP_PRELOAD_DLL,
 * [Loader] Build = ravengun) in het spelproces geladen en legt de kernel32-ingangen CreateFileW, ReadFile, WriteFile, CloseHandle,
 * Get/SetCommState, SetCommMask, SetCommTimeouts, PurgeComm en ClearCommError van de hoofdmodule om. Een CreateFileW op een
 * \\.\COM<n>-naam geeft een eigen handle; alles wat de game daarna doet gaat naar het bordmodel, de rest naar kernel32.
 * Munten komen uit het gedeelde geheugen TeknoParrot_JvsState (indeling TimeCrisis5: munt = bit 0x08 van het woord op offset 8;
 * de rg-jvsbridge/pipehelper van rg-direct maakt die mapping), een stijgende flank telt een munt op.
 *
 * Omgeving:
 *   RG_KABOOM_PORT   alleen deze COM-poort nabootsen (bv. 9); leeg = elke \\.\COM<n> die de hoofdmodule opent
 *   RG_KABOOM_COIN   masker van de munt in het woord op offset 8 (standaard 0x08)
 *   RG_KABOOM_LOG    bestand voor het logboek (aan het begin steeds een regel met de hoofdmodule en het aantal omgelegde ingangen)
 */
#include <windows.h>
#include <stdarg.h>
#include <string.h>

#include "kaboom_proto.h"

/* Geen C-runtime (alleen kernel32 en user32): de paar functies die de compiler nodig heeft staan hier zelf. */
void* memset(void* d, int c, size_t n) {
    volatile unsigned char* p = (volatile unsigned char*)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void* memcpy(void* d, const void* s, size_t n) {
    volatile unsigned char* p = (volatile unsigned char*)d;
    const unsigned char* q = (const unsigned char*)s;
    while (n--) *p++ = *q++;
    return d;
}
static unsigned long tot_getal(const char* s) {                 /* decimaal, of hexadecimaal met 0x */
    unsigned long v = 0;
    unsigned basis = 10;
    if (s[0] == '0' && (s[1] | 0x20) == 'x') { basis = 16; s += 2; }
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (basis == 16 && (*s | 0x20) >= 'a' && (*s | 0x20) <= 'f') d = (unsigned)((*s | 0x20) - 'a' + 10);
        else break;
        v = v * basis + d;
    }
    return v;
}

static kb_bord g_bord;
static CRITICAL_SECTION g_cs;
static HANDLE g_fake;                      /* de handle die we uitdelen (een gewone event-handle, zodat CloseHandle en dubbele handles kloppen) */
static const volatile unsigned char* g_shm;
static DWORD g_poort_filter = 0;           /* 0 = elke poort */
static char g_log[MAX_PATH];

static void logf_(const char* fmt, ...) {
    if (!g_log[0]) return;
    HANDLE h = CreateFileA(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);                         /* user32; %u %x %d %s, geen %p */
    va_end(ap);
    if (n < 0 || n > (int)sizeof buf - 3) n = 0;
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD w;
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    CloseHandle(h);
}

/* --- het gedeelde geheugen -------------------------------------------------------------------------------------------- */
static void poll_shm(void) {
    if (!g_shm) {
        HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (!m) return;
        g_shm = (const volatile unsigned char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 64);
        if (!g_shm) return;
        logf_("gedeeld geheugen TeknoParrot_JvsState geopend");
    }
    const uint32_t w = (uint32_t)g_shm[8] | ((uint32_t)g_shm[9] << 8) | ((uint32_t)g_shm[10] << 16) | ((uint32_t)g_shm[11] << 24);
    EnterCriticalSection(&g_cs);
    const uint16_t voor = g_bord.munten[0];
    kb_controlewoord(&g_bord, w);
    if (g_bord.munten[0] != voor) logf_("munt: teller nu %u", (unsigned)g_bord.munten[0]);
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI poll_draad(LPVOID p) {
    (void)p;
    for (;;) {
        poll_shm();
        Sleep(4);
    }
    return 0;
}

/* --- de omgelegde kernel32-functies ----------------------------------------------------------------------------------- */
static HANDLE(WINAPI* o_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL(WINAPI* o_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL(WINAPI* o_WriteFile)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL(WINAPI* o_CloseHandle)(HANDLE);
static BOOL(WINAPI* o_GetCommState)(HANDLE, LPDCB);
static BOOL(WINAPI* o_SetCommState)(HANDLE, LPDCB);
static BOOL(WINAPI* o_SetCommMask)(HANDLE, DWORD);
static BOOL(WINAPI* o_SetCommTimeouts)(HANDLE, LPCOMMTIMEOUTS);
static BOOL(WINAPI* o_PurgeComm)(HANDLE, DWORD);
static BOOL(WINAPI* o_ClearCommError)(HANDLE, LPDWORD, LPCOMSTAT);

static int is_fake(HANDLE h) { return g_fake && h == g_fake; }

/* \\.\COM9, \\.\com9 of COM9; geeft het poortnummer of 0 */
static DWORD com_nummer(LPCWSTR n) {
    if (!n) return 0;
    if (n[0] == L'\\' && n[1] == L'\\' && n[2] == L'.' && n[3] == L'\\') n += 4;
    if ((n[0] | 0x20) != L'c' || (n[1] | 0x20) != L'o' || (n[2] | 0x20) != L'm') return 0;
    DWORD v = 0;
    int cijfers = 0;
    for (n += 3; *n >= L'0' && *n <= L'9'; n++, cijfers++) v = v * 10 + (DWORD)(*n - L'0');
    return (cijfers && !*n) ? v : 0;
}

static HANDLE WINAPI h_CreateFileW(LPCWSTR n, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl) {
    const DWORD com = com_nummer(n);
    if (com && (!g_poort_filter || g_poort_filter == com)) {
        EnterCriticalSection(&g_cs);
        if (!g_fake) g_fake = CreateEventW(NULL, TRUE, FALSE, NULL);
        kb_spoel(&g_bord);
        LeaveCriticalSection(&g_cs);
        logf_("CreateFileW COM%u -> nagebootst Kaboom-bord (handle 0x%x)", (unsigned)com, (unsigned)(UINT_PTR)g_fake);
        return g_fake;
    }
    return o_CreateFileW(n, acc, share, sa, disp, fl, tmpl);
}

static BOOL WINAPI h_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD w, LPOVERLAPPED ov) {
    if (!is_fake(h)) return o_WriteFile(h, buf, n, w, ov);
    EnterCriticalSection(&g_cs);
    const unsigned voor = g_bord.verwerkt, af = g_bord.afgekeurd;
    kb_schrijf(&g_bord, (const uint8_t*)buf, n);
    if (g_bord.verwerkt != voor) logf_("pakket van %u bytes beantwoord (cmd 0x%02x)", (unsigned)n, n >= 8 ? ((const uint8_t*)buf)[6] : 0u);
    if (g_bord.afgekeurd != af) logf_("pakket van %u bytes AFGEKEURD (magic of controlesom)", (unsigned)n);
    LeaveCriticalSection(&g_cs);
    if (w) *w = n;
    return TRUE;
}

static BOOL WINAPI h_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD r, LPOVERLAPPED ov) {
    if (!is_fake(h)) return o_ReadFile(h, buf, n, r, ov);
    EnterCriticalSection(&g_cs);
    const size_t k = kb_lees(&g_bord, (uint8_t*)buf, n);
    LeaveCriticalSection(&g_cs);
    if (r) *r = (DWORD)k;
    return TRUE;
}

static BOOL WINAPI h_CloseHandle(HANDLE h) {
    if (is_fake(h)) {
        logf_("CloseHandle op het nagebootste bord");
        return TRUE;                           /* de event-handle blijft bestaan; de game opent hem desnoods opnieuw */
    }
    return o_CloseHandle(h);
}

static BOOL WINAPI h_GetCommState(HANDLE h, LPDCB d) {
    if (!is_fake(h)) return o_GetCommState(h, d);
    memset(d, 0, sizeof *d);
    d->DCBlength = sizeof *d;
    d->BaudRate = 57600;
    d->ByteSize = 8;
    d->fBinary = 1;
    return TRUE;
}
static BOOL WINAPI h_SetCommState(HANDLE h, LPDCB d) { return is_fake(h) ? TRUE : o_SetCommState(h, d); }
static BOOL WINAPI h_SetCommMask(HANDLE h, DWORD m) { return is_fake(h) ? TRUE : o_SetCommMask(h, m); }
static BOOL WINAPI h_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_fake(h) ? TRUE : o_SetCommTimeouts(h, t); }
static BOOL WINAPI h_PurgeComm(HANDLE h, DWORD f) {
    if (!is_fake(h)) return o_PurgeComm(h, f);
    EnterCriticalSection(&g_cs);
    kb_spoel(&g_bord);
    LeaveCriticalSection(&g_cs);
    return TRUE;
}
static BOOL WINAPI h_ClearCommError(HANDLE h, LPDWORD e, LPCOMSTAT s) {
    if (!is_fake(h)) return o_ClearCommError(h, e, s);
    if (e) *e = 0;
    if (s) memset(s, 0, sizeof *s);
    return TRUE;
}

/* --- IAT omleggen ----------------------------------------------------------------------------------------------------- */
typedef struct { const char* naam; void* nieuw; void** oud; } omleg;
static omleg g_omleg[] = {
    {"CreateFileW", (void*)h_CreateFileW, (void**)&o_CreateFileW},
    {"ReadFile", (void*)h_ReadFile, (void**)&o_ReadFile},
    {"WriteFile", (void*)h_WriteFile, (void**)&o_WriteFile},
    {"CloseHandle", (void*)h_CloseHandle, (void**)&o_CloseHandle},
    {"GetCommState", (void*)h_GetCommState, (void**)&o_GetCommState},
    {"SetCommState", (void*)h_SetCommState, (void**)&o_SetCommState},
    {"SetCommMask", (void*)h_SetCommMask, (void**)&o_SetCommMask},
    {"SetCommTimeouts", (void*)h_SetCommTimeouts, (void**)&o_SetCommTimeouts},
    {"PurgeComm", (void*)h_PurgeComm, (void**)&o_PurgeComm},
    {"ClearCommError", (void*)h_ClearCommError, (void**)&o_ClearCommError},
};

static int leg_om(HMODULE mod) {
    const BYTE* base = (const BYTE*)mod;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY* dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int gedaan = 0;
    if (!dd->VirtualAddress) return 0;
    for (const IMAGE_IMPORT_DESCRIPTOR* d = (const IMAGE_IMPORT_DESCRIPTOR*)(base + dd->VirtualAddress); d->Name; d++) {
        if (lstrcmpiA((const char*)(base + d->Name), "KERNEL32.dll") != 0) continue;
        const IMAGE_THUNK_DATA* nm = (const IMAGE_THUNK_DATA*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        void** slot = (void**)(base + d->FirstThunk);
        for (; nm->u1.AddressOfData; nm++, slot++) {
            if (IMAGE_SNAP_BY_ORDINAL(nm->u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME* ibn = (const IMAGE_IMPORT_BY_NAME*)(base + nm->u1.AddressOfData);
            for (size_t i = 0; i < sizeof g_omleg / sizeof g_omleg[0]; i++) {
                if (lstrcmpA((const char*)ibn->Name, g_omleg[i].naam) != 0) continue;
                DWORD oudprot;
                if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &oudprot)) continue;
                if (!*g_omleg[i].oud) *g_omleg[i].oud = *slot;
                *slot = g_omleg[i].nieuw;
                VirtualProtect(slot, sizeof *slot, oudprot, &oudprot);
                gedaan++;
            }
        }
    }
    return gedaan;
}

/* RG_KABOOM_PATCH=<rva>:<verwacht>:<nieuw> (hex, bv. 0x33e553:752e:9090): een bytepatch in de hoofdmodule, alleen als de verwachte bytes
 * er staan. Voor Infinity Blade FX: de RHI-keuze van SwordGame.exe (0x73e553 = RVA 0x33e553) zonder D3D10 (zie patches/InfinityBladeFX/game.ini). */
static int hex_bytes(const char** p, unsigned char* uit, int max) {
    int n = 0;
    while (**p && **p != ':' && n < max) {
        unsigned hi, lo;
        char c = **p;
        hi = (c >= '0' && c <= '9') ? (unsigned)(c - '0') : ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') ? (unsigned)((c | 0x20) - 'a' + 10) : 99u;
        c = (*p)[1];
        lo = (c >= '0' && c <= '9') ? (unsigned)(c - '0') : ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') ? (unsigned)((c | 0x20) - 'a' + 10) : 99u;
        if (hi > 15 || lo > 15) return -1;
        uit[n++] = (unsigned char)(hi * 16 + lo);
        *p += 2;
    }
    return n;
}

static void pas_patch_toe(void) {
    char spec[160];
    if (!GetEnvironmentVariableA("RG_KABOOM_PATCH", spec, sizeof spec) || !spec[0]) return;
    const char* p = spec;
    unsigned long rva = tot_getal(p);
    while (*p && *p != ':') p++;
    if (*p != ':') { logf_("patch: formaat <rva>:<verwacht>:<nieuw> klopt niet: %s", spec); return; }
    p++;
    unsigned char oud[16], nieuw[16];
    int no = hex_bytes(&p, oud, 16);
    if (no <= 0 || *p != ':') { logf_("patch: verwachte bytes kloppen niet: %s", spec); return; }
    p++;
    int nn = hex_bytes(&p, nieuw, 16);
    if (nn != no || *p) { logf_("patch: nieuwe bytes kloppen niet (andere lengte of rommel): %s", spec); return; }
    unsigned char* doel = (unsigned char*)GetModuleHandleA(NULL) + rva;
    for (int i = 0; i < no; i++)
        if (doel[i] != oud[i]) { logf_("patch: op rva 0x%x staat byte %d = 0x%x, verwacht 0x%x; NIET geschreven", (unsigned)rva, i, (unsigned)doel[i], (unsigned)oud[i]); return; }
    DWORD was;
    if (!VirtualProtect(doel, (SIZE_T)no, PAGE_EXECUTE_READWRITE, &was)) { logf_("patch: VirtualProtect mislukt (%u)", (unsigned)GetLastError()); return; }
    for (int i = 0; i < no; i++) doel[i] = nieuw[i];
    VirtualProtect(doel, (SIZE_T)no, was, &was);
    FlushInstructionCache(GetCurrentProcess(), doel, (SIZE_T)no);
    logf_("patch: rva 0x%x, %d bytes geschreven", (unsigned)rva, no);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID res) {
    (void)res;
    if (why != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    char tmp[64];
    InitializeCriticalSection(&g_cs);
    if (GetEnvironmentVariableA("RG_KABOOM_LOG", g_log, sizeof g_log) >= sizeof g_log) g_log[0] = 0;
    uint32_t munt = 0x08;
    if (GetEnvironmentVariableA("RG_KABOOM_COIN", tmp, sizeof tmp) && tmp[0]) munt = (uint32_t)tot_getal(tmp);
    if (GetEnvironmentVariableA("RG_KABOOM_PORT", tmp, sizeof tmp) && tmp[0]) g_poort_filter = (DWORD)tot_getal(tmp);
    kb_init(&g_bord, munt);
    /* Oude functies bij naam ophalen: ook als de IAT-lus ze niet vindt (bv. de game importeert ze niet) blijft het veilig. */
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    o_CreateFileW = (void*)GetProcAddress(k32, "CreateFileW");
    o_ReadFile = (void*)GetProcAddress(k32, "ReadFile");
    o_WriteFile = (void*)GetProcAddress(k32, "WriteFile");
    o_CloseHandle = (void*)GetProcAddress(k32, "CloseHandle");
    o_GetCommState = (void*)GetProcAddress(k32, "GetCommState");
    o_SetCommState = (void*)GetProcAddress(k32, "SetCommState");
    o_SetCommMask = (void*)GetProcAddress(k32, "SetCommMask");
    o_SetCommTimeouts = (void*)GetProcAddress(k32, "SetCommTimeouts");
    o_PurgeComm = (void*)GetProcAddress(k32, "PurgeComm");
    o_ClearCommError = (void*)GetProcAddress(k32, "ClearCommError");
    const int n = leg_om(GetModuleHandleA(NULL));
    logf_("rg-kaboom.dll geladen: %d kernel32-ingangen van de hoofdmodule omgelegd, poortfilter %u, muntmasker 0x%x", n, (unsigned)g_poort_filter, (unsigned)munt);
    pas_patch_toe();
    CreateThread(NULL, 0, poll_draad, NULL, 0, NULL);
    return TRUE;
}
