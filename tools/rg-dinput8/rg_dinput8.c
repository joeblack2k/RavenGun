/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-dinput8: RavenGuns eigen dinput8.dll, een drop-in voor een 32-bits game die zijn geweren als DirectInput8-joysticks leest
 * (Bug Busters, BBPC.exe: c_dfDIJoystick2, absolute X/Y, knop 0 = trekker, knop 1 = herladen). De game laadt hem uit zijn eigen
 * map (dropin.ini: DllOverride = native); er draait geen lader en geen kern van TeknoParrot.
 *
 * CLEAN-ROOM: geschreven uit de DirectInput-documentatie (de interfaces en structuren van dinput.h van mingw-w64) en het gebruik
 * in de game (docs/research/wartran/README.md, "Bug Busters"). Geen code van TeknoParrot of Microsoft.
 *
 *   - DirectInput8Create geeft een eigen IDirectInput8A. Die somt ALLEEN de virtuele geweren op (een joystick per speler, vaste
 *     GUID's, rgd_guid) en maakt alleen die apparaten; de echte toetsenborden, muizen en joysticks van de machine ziet de game via
 *     DirectInput niet (vensterberichten zoals WM_KEYDOWN en WM_MOUSEMOVE blijven gewoon werken);
 *   - een apparaat leest bij elke GetDeviceState/Poll het bordbeeld van RavenGun (de Windows-mapping "TeknoParrot_JvsState" van
 *     rg-jvsbridge.exe in de prefix) en geeft X/Y in het bereik dat de game zelf met DIPROP_RANGE zet, of in as.x/as.y uit de tabel;
 *   - dezelfde stap zet de vlaggen en tellers uit de tabel bij een opgaande flank (munt, start, test op de plaatsen waar het I/O-bord
 *     van de kast ze in de game zette), in de draad van de game zelf;
 *   - DllMain past de patches uit de tabel toe op het beeld van de exe, maar alleen als ALLE oude bytes kloppen (anders geen enkele
 *     patch, geen vlag en geen teller: dan is het niet de exe waarvoor de tabel geschreven is);
 *   - rg-dinput8.ini naast deze DLL (patches/<Game>/dropin/) zegt per game welke byte welke as is, welke bit welke knop, en welke
 *     vlaggen, tellers en patches er zijn (rg_dinput8_kern.h). Zonder bestand: de standaard (twee spelers, WartranTroopers, geen
 *     vlaggen, tellers of patches). Een bestand met een fout: geen invoer en geen patch;
 *   - RG_DINPUT8_LOG=pad schrijft een logboek (anders OutputDebugString); een relatief pad is relatief aan de map van deze DLL.
 */
#define DIRECTINPUT_VERSION 0x0800
#define CINTERFACE
#include <windows.h>
#include <dinput.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_dinput8_kern.h"

#define EXPORT __declspec(dllexport)

static HMODULE deze;
static CRITICAL_SECTION slot;
static rgd_tabel tabel;
static int tabel_ok;                 /* 0 = afgekeurd: geen invoer */
static int geheugen_ok;              /* patches klopten: vlaggen en tellers mogen schrijven */
static int vlag_ok[RGD_MAX_ADRES], teller_ok[RGD_MAX_ADRES];
static rgd_flanken flanken;
static const volatile uint8_t *beeld;
static DWORD beeld_poging;
static int gemeld_formaat;

/* ------------------------------------------------------------------------------------------------------------------ logboek */

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
    /* Een relatief pad is relatief aan de map van deze DLL (in de view), niet aan de werkmap van het proces. */
    const char *pad = getenv("RG_DINPUT8_LOG");
    char vol[MAX_PATH + 64];
    if (pad && *pad && !(pad[0] == '\\' || pad[0] == '/' || (pad[0] && pad[1] == ':'))) {
        DWORD l = GetModuleFileNameA(deze, vol, MAX_PATH);
        char *schuin = l ? strrchr(vol, '\\') : NULL;
        if (schuin && strlen(pad) < 60) { strcpy(schuin + 1, pad); pad = vol; }
    }
    FILE *f = pad && *pad ? fopen(pad, "a") : NULL;
    if (f) { fputs("rg-dinput8: ", f); fputs(regel, f); fclose(f); }
    else { OutputDebugStringA("rg-dinput8: "); OutputDebugStringA(regel); }
}

/* ----------------------------------------------------------------------------------------------------- tabel en geheugen */

static void lees_tabel(void)
{
    char pad[MAX_PATH + 32];
    DWORD n = GetModuleFileNameA(deze, pad, MAX_PATH);
    char *schuin = n ? strrchr(pad, '\\') : NULL;
    if (!schuin) { rgd_standaard(&tabel); tabel_ok = 1; logf_("tabel: standaard (geen pad van de DLL)"); return; }
    strcpy(schuin + 1, "rg-dinput8.ini");
    HANDLE h = CreateFileA(pad, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { rgd_standaard(&tabel); tabel_ok = 1; logf_("tabel: %s ontbreekt, de standaard", pad); return; }
    static char tekst[16384];
    DWORD lengte = 0;
    BOOL ok = ReadFile(h, tekst, sizeof tekst - 1, &lengte, NULL);
    CloseHandle(h);
    tekst[ok ? lengte : 0] = 0;
    char fout[200];
    if (!ok || rgd_lees_tabel(tekst, &tabel, fout, sizeof fout) != 0) {
        memset(&tabel, 0, sizeof tabel);
        tabel_ok = 0;
        logf_("tabel %s AFGEKEURD (%s): er komt GEEN invoer en geen patch", pad, ok ? fout : "niet te lezen");
        return;
    }
    tabel_ok = 1;
    logf_("tabel: %s (%d spelers, %d vlaggen, %d tellers, %d patches)", pad, tabel.spelers, tabel.nvlag, tabel.nteller, tabel.npatch);
}

/* Ligt [a, a+n) in het beeld van de exe? */
static int in_exe(uint32_t a, uint32_t n)
{
    const uint8_t *basis = (const uint8_t *)GetModuleHandleA(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)basis;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(basis + dos->e_lfanew);
    const uintptr_t lo = (uintptr_t)basis, hi = lo + nt->OptionalHeader.SizeOfImage;
    return a >= lo && (uintptr_t)a + n <= hi && (uintptr_t)a + n > a;
}

static int schrijfbaar(uint32_t a)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((const void *)(uintptr_t)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0;
    const DWORD p = mbi.Protect & 0xFF;
    return p == PAGE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

static void patch_en_controleer(void)
{
    if (!tabel_ok) return;
    for (int i = 0; i < tabel.npatch; i++) {
        const rgd_patch *p = &tabel.patch[i];
        if (!in_exe(p->adres, (uint32_t)p->n) || memcmp((const void *)(uintptr_t)p->adres, p->oud, (size_t)p->n) != 0) {
            const uint8_t *nu = in_exe(p->adres, (uint32_t)p->n) ? (const uint8_t *)(uintptr_t)p->adres : NULL;
            logf_("patch 0x%08X: oude bytes kloppen NIET (verwacht %02X.., gevonden %s%02X): andere exe, GEEN patch, vlag of teller",
                  (unsigned)p->adres, p->oud[0], nu ? "" : "buiten de exe ", nu ? nu[0] : 0);
            return;
        }
    }
    for (int i = 0; i < tabel.npatch; i++) {
        const rgd_patch *p = &tabel.patch[i];
        void *a = (void *)(uintptr_t)p->adres;
        DWORD oud;
        if (!VirtualProtect(a, (SIZE_T)p->n, PAGE_EXECUTE_READWRITE, &oud)) { logf_("patch 0x%08X: VirtualProtect faalt (%lu)", (unsigned)p->adres, GetLastError()); continue; }
        memcpy(a, p->nieuw, (size_t)p->n);
        VirtualProtect(a, (SIZE_T)p->n, oud, &oud);
        FlushInstructionCache(GetCurrentProcess(), a, (SIZE_T)p->n);
        logf_("patch 0x%08X: %d byte(s) %02X.. -> %02X..", (unsigned)p->adres, p->n, p->oud[0], p->nieuw[0]);
    }
    geheugen_ok = 1;
    for (int i = 0; i < tabel.nvlag; i++) {
        vlag_ok[i] = in_exe(tabel.vlag[i].adres, 4) && schrijfbaar(tabel.vlag[i].adres);
        if (!vlag_ok[i]) logf_("vlag 0x%08X ligt niet in schrijfbaar geheugen van de exe: uit", (unsigned)tabel.vlag[i].adres);
    }
    for (int i = 0; i < tabel.nteller; i++) {
        teller_ok[i] = in_exe(tabel.teller[i].adres, 4) && schrijfbaar(tabel.teller[i].adres);
        if (!teller_ok[i]) logf_("teller 0x%08X ligt niet in schrijfbaar geheugen van de exe: uit", (unsigned)tabel.teller[i].adres);
    }
}

/* -------------------------------------------------------------------------------------------------------------- bordbeeld */

static const volatile uint8_t *open_beeld(void)
{
    HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
    if (!m) return NULL;
    const volatile uint8_t *v = (const volatile uint8_t *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, RGD_BEELD);
    if (v) logf_("bordbeeld TeknoParrot_JvsState geopend");
    return v;
}

/* Een stap: het bordbeeld lezen en de flanken naar vlaggen en tellers brengen. Draait in de draad van de game (GetDeviceState). */
static int stap(uint8_t kopie[RGD_BEELD])
{
    EnterCriticalSection(&slot);
    if (!beeld && GetTickCount() - beeld_poging >= 1000) {
        beeld_poging = GetTickCount();
        beeld = open_beeld();
    }
    int er = beeld != NULL && tabel_ok;
    if (er) {
        for (int i = 0; i < RGD_BEELD; i++) kopie[i] = beeld[i];
        uint32_t vlaggen;
        int tellers[RGD_MAX_ADRES];
        rgd_stap(&tabel, &flanken, rgd_bits(kopie), &vlaggen, tellers);
        if (geheugen_ok) {
            for (int i = 0; i < tabel.nvlag; i++)
                if (((vlaggen >> i) & 1u) && vlag_ok[i]) {
                    *(volatile LONG *)(uintptr_t)tabel.vlag[i].adres = 1;
                    logf_("vlag 0x%08X := 1", (unsigned)tabel.vlag[i].adres);
                }
            for (int i = 0; i < tabel.nteller; i++)
                if (tellers[i] && teller_ok[i]) {
                    LONG nu = InterlockedExchangeAdd((volatile LONG *)(uintptr_t)tabel.teller[i].adres, tellers[i]) + tellers[i];
                    logf_("teller 0x%08X += %d (nu %ld)", (unsigned)tabel.teller[i].adres, tellers[i], (long)nu);
                }
        }
    } else {
        memset(kopie, 0, RGD_BEELD);
    }
    LeaveCriticalSection(&slot);
    return er;
}

/* ------------------------------------------------------------------------------------------------- GUID's en structuren */

static const GUID kIUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID kIDI8A = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static const GUID kIDI8W = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
static const GUID kIDev8A = {0x54D41080, 0xDC15, 0x4833, {0xA4, 0x1B, 0x74, 0x8F, 0x73, 0xA3, 0x81, 0x79}};
static const GUID kXAxis = {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kYAxis = {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kButton = {0xA36D02F0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

#define TYPE_X (DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(0))
#define TYPE_Y (DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(1))
#define KNOPPEN 8
#define DEVTYPE (DI8DEVTYPE_JOYSTICK | (DI8DEVTYPEJOYSTICK_STANDARD << 8))

static void speler_guid(int n, GUID *g)
{
    uint8_t b[16];
    rgd_guid(n, b);
    memcpy(g, b, 16);
}

static int guid_speler(REFGUID g)
{
    for (int n = 0; tabel_ok && n < tabel.spelers; n++) {
        GUID s;
        speler_guid(n, &s);
        if (memcmp(g, &s, sizeof s) == 0) return n;
    }
    return -1;
}

static void vul_instantie(int n, DIDEVICEINSTANCEA *di)
{
    DWORD grootte = di->dwSize >= sizeof(DIDEVICEINSTANCEA) || di->dwSize == 0 ? sizeof(DIDEVICEINSTANCEA) : di->dwSize;
    memset(di, 0, grootte);
    di->dwSize = grootte;
    speler_guid(n, &di->guidInstance);
    speler_guid(n, &di->guidProduct);
    di->guidProduct.Data4[0] = 'P';
    di->dwDevType = DEVTYPE;
    snprintf(di->tszInstanceName, sizeof di->tszInstanceName, "RavenGun Gun %d", n + 1);
    snprintf(di->tszProductName, sizeof di->tszProductName, "RavenGun Gun");
}

/* ------------------------------------------------------------------------------------------------- het apparaat (joystick) */

typedef struct {
    const void *const *vtbl;
    LONG ref;
    int speler;
    long lo[2], hi[2];               /* DIPROP_RANGE van X en Y */
} RgDev;

#define D(x) ((RgDev *)(x))

static HRESULT WINAPI dev_QueryInterface(void *s, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(riid, &kIUnknown) || IsEqualGUID(riid, &kIDev8A)) {
        *out = s;
        InterlockedIncrement(&D(s)->ref);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI dev_AddRef(void *s) { return (ULONG)InterlockedIncrement(&D(s)->ref); }
static ULONG WINAPI dev_Release(void *s)
{
    LONG r = InterlockedDecrement(&D(s)->ref);
    if (r == 0) HeapFree(GetProcessHeap(), 0, s);
    return (ULONG)r;
}
static HRESULT WINAPI dev_GetCapabilities(void *s, LPDIDEVCAPS caps)
{
    (void)s;
    if (!caps || caps->dwSize < 24) return DIERR_INVALIDPARAM;
    DWORD grootte = caps->dwSize;
    memset(caps, 0, grootte);
    caps->dwSize = grootte;
    caps->dwFlags = DIDC_ATTACHED;
    caps->dwDevType = DEVTYPE;
    caps->dwAxes = 2;
    caps->dwButtons = KNOPPEN;
    caps->dwPOVs = 0;
    return DI_OK;
}
static HRESULT WINAPI dev_EnumObjects(void *s, LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD flags)
{
    (void)s;
    if (!cb) return DIERR_INVALIDPARAM;
    const int assen = flags == DIDFT_ALL || (flags & DIDFT_AXIS) != 0, knoppen = flags == DIDFT_ALL || (flags & DIDFT_BUTTON) != 0;
    DIDEVICEOBJECTINSTANCEA o;
    for (int i = 0; i < 2 + KNOPPEN; i++) {
        if (i < 2 ? !assen : !knoppen) continue;
        memset(&o, 0, sizeof o);
        o.dwSize = sizeof o;
        if (i < 2) {
            o.guidType = i == 0 ? kXAxis : kYAxis;
            o.dwOfs = i == 0 ? DIJOFS_X : DIJOFS_Y;
            o.dwType = i == 0 ? TYPE_X : TYPE_Y;
            snprintf(o.tszName, sizeof o.tszName, "%s Axis", i == 0 ? "X" : "Y");
        } else {
            o.guidType = kButton;
            o.dwOfs = DIJOFS_BUTTON(i - 2);
            o.dwType = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(i - 2);
            snprintf(o.tszName, sizeof o.tszName, "Button %d", i - 2);
        }
        if (cb(&o, ref) == DIENUM_STOP) break;
    }
    return DI_OK;
}
/* Welke as bedoelt een DIPROPHEADER: bit 0 = X, bit 1 = Y. */
static int welke_assen(LPCDIPROPHEADER h)
{
    if (h->dwHow == DIPH_DEVICE) return 3;
    if (h->dwHow == DIPH_BYOFFSET) return h->dwObj == DIJOFS_X ? 1 : h->dwObj == DIJOFS_Y ? 2 : 0;
    if (h->dwHow == DIPH_BYID) {
        const DWORD id = h->dwObj & 0x00FFFFFF;
        return id == (TYPE_X & 0x00FFFFFF) ? 1 : id == (TYPE_Y & 0x00FFFFFF) ? 2 : 0;
    }
    return 0;
}
static HRESULT WINAPI dev_GetProperty(void *s, REFGUID prop, LPDIPROPHEADER h)
{
    if (!h) return DIERR_INVALIDPARAM;
    if ((uintptr_t)prop == (uintptr_t)DIPROP_RANGE) {
        const int a = welke_assen(h);
        if (a != 1 && a != 2) return DIERR_OBJECTNOTFOUND;
        LPDIPROPRANGE r = (LPDIPROPRANGE)h;
        r->lMin = D(s)->lo[a - 1];
        r->lMax = D(s)->hi[a - 1];
        return DI_OK;
    }
    return DIERR_UNSUPPORTED;
}
static HRESULT WINAPI dev_SetProperty(void *s, REFGUID prop, LPCDIPROPHEADER h)
{
    if (!h) return DIERR_INVALIDPARAM;
    if ((uintptr_t)prop == (uintptr_t)DIPROP_RANGE) {
        const int a = welke_assen(h);
        if (!a) return DIERR_OBJECTNOTFOUND;
        LPCDIPROPRANGE r = (LPCDIPROPRANGE)h;
        if (r->lMin >= r->lMax) return DIERR_INVALIDPARAM;
        for (int i = 0; i < 2; i++)
            if ((a >> i) & 1) D(s)->lo[i] = r->lMin, D(s)->hi[i] = r->lMax;
        logf_("speler %d: DIPROP_RANGE %s = %ld..%ld", D(s)->speler + 1, a == 3 ? "X+Y" : a == 1 ? "X" : "Y", (long)r->lMin, (long)r->lMax);
        return DI_OK;
    }
    return DI_OK;   /* dode zone, verzadiging, buffergrootte, asmodus: geen betekenis voor een geweer */
}
static HRESULT WINAPI dev_Ok(void *s) { (void)s; return DI_OK; }
static HRESULT WINAPI dev_GetDeviceState(void *s, DWORD cb, LPVOID data)
{
    if (!data || cb < 8) return DIERR_INVALIDPARAM;
    uint8_t kopie[RGD_BEELD];
    const int er = stap(kopie);
    memset(data, 0, cb);
    uint8_t *d = (uint8_t *)data;
    if (cb >= 48) memset(d + 32, 0xFF, 16);   /* rgdwPOV: midden */
    if (!er) return DI_OK;                    /* geen bordbeeld of een afgekeurde tabel: niets ingedrukt */
    rgd_joy j;
    rgd_joystick(&tabel, D(s)->speler, kopie, D(s)->lo, D(s)->hi, &j);
    memcpy(d + DIJOFS_X, &j.lX, 4);
    memcpy(d + DIJOFS_Y, &j.lY, 4);
    for (int k = 0; k < RGD_MAX_KNOPPEN && (DWORD)DIJOFS_BUTTON(k) < cb; k++) d[DIJOFS_BUTTON(k)] = j.knop[k];
    return DI_OK;
}
static HRESULT WINAPI dev_GetDeviceData(void *s, DWORD cb, LPDIDEVICEOBJECTDATA od, LPDWORD n, DWORD flags)
{
    (void)s, (void)cb, (void)od, (void)flags;
    if (n) *n = 0;
    return DI_OK;
}
static HRESULT WINAPI dev_SetDataFormat(void *s, LPCDIDATAFORMAT df)
{
    if (!df) return DIERR_INVALIDPARAM;
    if (df->dwDataSize != sizeof(DIJOYSTATE2) && df->dwDataSize != sizeof(DIJOYSTATE) && !gemeld_formaat++)
        logf_("speler %d: SetDataFormat met %lu bytes (geen DIJOYSTATE/DIJOYSTATE2): X/Y/knoppen staan toch op hun DIJOYSTATE-plaats",
              D(s)->speler + 1, (unsigned long)df->dwDataSize);
    return DI_OK;
}
static HRESULT WINAPI dev_SetEventNotification(void *s, HANDLE e) { (void)s, (void)e; return DI_OK; }
static HRESULT WINAPI dev_SetCooperativeLevel(void *s, HWND w, DWORD f) { (void)s, (void)w, (void)f; return DI_OK; }
static HRESULT WINAPI dev_GetObjectInfo(void *s, LPDIDEVICEOBJECTINSTANCEA o, DWORD obj, DWORD how)
{
    (void)s, (void)o, (void)obj, (void)how;
    return DIERR_OBJECTNOTFOUND;
}
static HRESULT WINAPI dev_GetDeviceInfo(void *s, LPDIDEVICEINSTANCEA di)
{
    /* Minstens DIDEVICEINSTANCE_DX3 (tot en met tszProductName): vul_instantie schrijft niet verder. */
    if (!di || di->dwSize < offsetof(DIDEVICEINSTANCEA, guidFFDriver)) return DIERR_INVALIDPARAM;
    vul_instantie(D(s)->speler, di);
    return DI_OK;
}
static HRESULT WINAPI dev_RunControlPanel(void *s, HWND w, DWORD f) { (void)s, (void)w, (void)f; return DI_OK; }
static HRESULT WINAPI dev_Initialize(void *s, HINSTANCE h, DWORD v, REFGUID g) { (void)s, (void)h, (void)v, (void)g; return DI_OK; }
static HRESULT WINAPI dev_CreateEffect(void *s, REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *out, LPUNKNOWN u)
{
    (void)s, (void)g, (void)e, (void)u;
    if (out) *out = NULL;
    return DIERR_UNSUPPORTED;
}
static HRESULT WINAPI dev_EnumEffects(void *s, LPDIENUMEFFECTSCALLBACKA cb, LPVOID r, DWORD t) { (void)s, (void)cb, (void)r, (void)t; return DI_OK; }
static HRESULT WINAPI dev_GetEffectInfo(void *s, LPDIEFFECTINFOA e, REFGUID g) { (void)s, (void)e, (void)g; return DIERR_DEVICENOTREG; }
static HRESULT WINAPI dev_GetForceFeedbackState(void *s, LPDWORD o) { (void)s, (void)o; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI dev_SendForceFeedbackCommand(void *s, DWORD f) { (void)s, (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI dev_EnumCreatedEffectObjects(void *s, LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb, LPVOID r, DWORD f)
{
    (void)s, (void)cb, (void)r, (void)f;
    return DI_OK;
}
static HRESULT WINAPI dev_Escape(void *s, LPDIEFFESCAPE e) { (void)s, (void)e; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI dev_Poll(void *s)
{
    (void)s;
    uint8_t kopie[RGD_BEELD];
    stap(kopie);
    return DI_OK;
}
static HRESULT WINAPI dev_SendDeviceData(void *s, DWORD cb, LPCDIDEVICEOBJECTDATA od, LPDWORD n, DWORD f)
{
    (void)s, (void)cb, (void)od, (void)f;
    if (n) *n = 0;
    return DI_OK;
}
static HRESULT WINAPI dev_EnumEffectsInFile(void *s, LPCSTR f, LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID r, DWORD fl)
{
    (void)s, (void)f, (void)cb, (void)r, (void)fl;
    return DI_OK;
}
static HRESULT WINAPI dev_WriteEffectToFile(void *s, LPCSTR f, DWORD n, LPDIFILEEFFECT e, DWORD fl)
{
    (void)s, (void)f, (void)n, (void)e, (void)fl;
    return DIERR_UNSUPPORTED;
}
static HRESULT WINAPI dev_BuildActionMap(void *s, LPDIACTIONFORMATA a, LPCSTR u, DWORD f) { (void)s, (void)a, (void)u, (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI dev_SetActionMap(void *s, LPDIACTIONFORMATA a, LPCSTR u, DWORD f) { (void)s, (void)a, (void)u, (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI dev_GetImageInfo(void *s, LPDIDEVICEIMAGEINFOHEADERA i) { (void)s, (void)i; return DIERR_UNSUPPORTED; }

/* De volgorde van IDirectInputDevice8A (dinput.h): 32 methoden. */
static const void *const dev_vtbl[32] = {
    (const void *)dev_QueryInterface, (const void *)dev_AddRef, (const void *)dev_Release, (const void *)dev_GetCapabilities,
    (const void *)dev_EnumObjects, (const void *)dev_GetProperty, (const void *)dev_SetProperty, (const void *)dev_Ok /* Acquire */,
    (const void *)dev_Ok /* Unacquire */, (const void *)dev_GetDeviceState, (const void *)dev_GetDeviceData, (const void *)dev_SetDataFormat,
    (const void *)dev_SetEventNotification, (const void *)dev_SetCooperativeLevel, (const void *)dev_GetObjectInfo, (const void *)dev_GetDeviceInfo,
    (const void *)dev_RunControlPanel, (const void *)dev_Initialize, (const void *)dev_CreateEffect, (const void *)dev_EnumEffects,
    (const void *)dev_GetEffectInfo, (const void *)dev_GetForceFeedbackState, (const void *)dev_SendForceFeedbackCommand,
    (const void *)dev_EnumCreatedEffectObjects, (const void *)dev_Escape, (const void *)dev_Poll, (const void *)dev_SendDeviceData,
    (const void *)dev_EnumEffectsInFile, (const void *)dev_WriteEffectToFile, (const void *)dev_BuildActionMap,
    (const void *)dev_SetActionMap, (const void *)dev_GetImageInfo,
};

/* --------------------------------------------------------------------------------------------------- IDirectInput8A */

typedef struct {
    const void *const *vtbl;
    LONG ref;
} RgDI;

static HRESULT WINAPI di_QueryInterface(void *s, REFIID riid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(riid, &kIUnknown) || IsEqualGUID(riid, &kIDI8A)) {
        *out = s;
        InterlockedIncrement(&((RgDI *)s)->ref);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI di_AddRef(void *s) { return (ULONG)InterlockedIncrement(&((RgDI *)s)->ref); }
static ULONG WINAPI di_Release(void *s)
{
    LONG r = InterlockedDecrement(&((RgDI *)s)->ref);
    if (r == 0) HeapFree(GetProcessHeap(), 0, s);
    return (ULONG)r;
}
static HRESULT WINAPI di_CreateDevice(void *s, REFGUID g, LPDIRECTINPUTDEVICE8A *out, LPUNKNOWN outer)
{
    (void)s, (void)outer;
    if (!out || !g) return DIERR_INVALIDPARAM;
    *out = NULL;
    const int n = guid_speler(g);
    if (n < 0) {
        logf_("CreateDevice {%08lX-...}: geen geweer van RavenGun (alleen de geweren bestaan hier)", (unsigned long)g->Data1);
        return DIERR_DEVICENOTREG;
    }
    RgDev *d = (RgDev *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *d);
    if (!d) return DIERR_OUTOFMEMORY;
    d->vtbl = dev_vtbl;
    d->ref = 1;
    d->speler = n;
    for (int i = 0; i < 2; i++) d->lo[i] = 0, d->hi[i] = 65535;   /* het standaardbereik van DirectInput */
    *out = (LPDIRECTINPUTDEVICE8A)d;
    logf_("CreateDevice: geweer van speler %d", n + 1);
    return DI_OK;
}
static HRESULT WINAPI di_EnumDevices(void *s, DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags)
{
    (void)s, (void)flags;
    if (!cb) return DIERR_INVALIDPARAM;
    const DWORD t = type & 0xFF;
    if (!(t == 0 || t == DI8DEVCLASS_GAMECTRL || t == DI8DEVTYPE_JOYSTICK)) return DI_OK;
    for (int n = 0; tabel_ok && n < tabel.spelers; n++) {
        DIDEVICEINSTANCEA di;
        di.dwSize = sizeof di;
        vul_instantie(n, &di);
        if (cb(&di, ref) == DIENUM_STOP) break;
    }
    return DI_OK;
}
static HRESULT WINAPI di_GetDeviceStatus(void *s, REFGUID g) { (void)s; return guid_speler(g) >= 0 ? DI_OK : DI_NOTATTACHED; }
static HRESULT WINAPI di_RunControlPanel(void *s, HWND w, DWORD f) { (void)s, (void)w, (void)f; return DI_OK; }
static HRESULT WINAPI di_Initialize(void *s, HINSTANCE h, DWORD v) { (void)s, (void)h, (void)v; return DI_OK; }
static HRESULT WINAPI di_FindDevice(void *s, REFGUID g, LPCSTR n, LPGUID out) { (void)s, (void)g, (void)n, (void)out; return DIERR_DEVICENOTREG; }
static HRESULT WINAPI di_EnumDevicesBySemantics(void *s, LPCSTR u, LPDIACTIONFORMATA a, LPDIENUMDEVICESBYSEMANTICSCBA cb, LPVOID r, DWORD f)
{
    (void)s, (void)u, (void)a, (void)cb, (void)r, (void)f;
    return DI_OK;
}
static HRESULT WINAPI di_ConfigureDevices(void *s, LPDICONFIGUREDEVICESCALLBACK cb, LPDICONFIGUREDEVICESPARAMSA p, DWORD f, LPVOID r)
{
    (void)s, (void)cb, (void)p, (void)f, (void)r;
    return DIERR_UNSUPPORTED;
}

static const void *const di_vtbl[11] = {
    (const void *)di_QueryInterface, (const void *)di_AddRef, (const void *)di_Release, (const void *)di_CreateDevice,
    (const void *)di_EnumDevices, (const void *)di_GetDeviceStatus, (const void *)di_RunControlPanel, (const void *)di_Initialize,
    (const void *)di_FindDevice, (const void *)di_EnumDevicesBySemantics, (const void *)di_ConfigureDevices,
};

/* ------------------------------------------------------------------------------------------------------------- exports */

EXPORT HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD versie, REFIID riid, LPVOID *out, LPUNKNOWN outer)
{
    (void)inst, (void)outer;
    if (!out) return DIERR_INVALIDPARAM;
    *out = NULL;
    if (!IsEqualGUID(riid, &kIDI8A)) {
        logf_("DirectInput8Create: interface %s niet geleverd (alleen IDirectInput8A)", IsEqualGUID(riid, &kIDI8W) ? "IDirectInput8W" : "onbekend");
        return DIERR_NOINTERFACE;
    }
    RgDI *di = (RgDI *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *di);
    if (!di) return DIERR_OUTOFMEMORY;
    di->vtbl = di_vtbl;
    di->ref = 1;
    *out = di;
    logf_("DirectInput8Create(versie 0x%04lX): IDirectInput8A van RavenGun (%d geweren)", (unsigned long)versie, tabel_ok ? tabel.spelers : 0);
    return DI_OK;
}

EXPORT HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
EXPORT HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID i, LPVOID *o)
{
    (void)c, (void)i;
    if (o) *o = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}
EXPORT HRESULT WINAPI DllRegisterServer(void) { return S_OK; }
EXPORT HRESULT WINAPI DllUnregisterServer(void) { return S_OK; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reden, LPVOID r)
{
    (void)r;
    if (reden == DLL_PROCESS_ATTACH) {
        deze = h;
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&slot);
        char exe[MAX_PATH];
        if (!GetModuleFileNameA(NULL, exe, sizeof exe)) strcpy(exe, "?");
        logf_("geladen in pid %lu (%s): dinput8.dll van RavenGun, geen TeknoParrot (laadbewijs)", (unsigned long)GetCurrentProcessId(), exe);
        lees_tabel();
        patch_en_controleer();
    }
    return TRUE;
}
