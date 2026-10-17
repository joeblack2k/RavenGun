/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg-adrio: RavenGuns eigen KaboomAdrioFxPlusFourFeeders64.dll, een drop-in voor de Unity-games van Adrenaline Amusements die het
 * Kaboom/Adrio-I/O-bord (seriele poort) via deze DLL aansturen (Rampage: Game_Data/Plugins/, P/Invoke vanuit
 * KaboomAdrioFxPlusFourFeedersDllWrapper in Assembly-CSharp.dll). Op de kast praat de DLL met het bord; hier beantwoordt hij de game
 * uit het bordbeeld van RavenGun, zonder bord en zonder de kern van TeknoParrot.
 *
 * CLEAN-ROOM: geschreven uit de P/Invoke-declaraties en het gebruik in de game (de handtekeningen, de structuren en hun vlaggen) en
 * de exporttabel van de originele DLL (objdump -p: 22 namen). Geen code van de originele DLL.
 *
 *   - InitLib slaagt op de eerste poort die de game probeert, bewaart de callbacks en start een draad die elke 4 ms het bordbeeld
 *     leest (de Windows-mapping "TeknoParrot_JvsState" van rg-jvsbridge.exe in de prefix; 32-bits bitveld op offset 8);
 *   - een verandering van het bedieningspaneel of de spelersknoppen gaat naar de keyboard- en playerbutton-callback, een munt
 *     (opgaande flank) verhoogt de teller van die deur en gaat naar de coin-callback (de game rekent zelf het verschil);
 *   - welk signaal welke bit is, staat in rg-adrio.ini naast deze DLL (patches/<Game>/dropin/); zonder dat bestand de indeling van
 *     Rampage (rg_adrio_kern.c); een bestand dat niet klopt wordt afgekeurd: dan komt er GEEN invoer (liever niets dan verkeerd);
 *   - versie 9 (de game eist >= 9); de ticketautomaten geven meteen alles (de teller springt naar het gevraagde getal), lampen,
 *     muntuitgangen en motoren doen niets;
 *   - RG_KABOOM_LOG=pad schrijft een logboek (anders OutputDebugString); een relatief pad is relatief aan de map van deze DLL.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rg_adrio_kern.h"

#define EXPORT __declspec(dllexport)
#define KABOOM_OK 0
#define KABOOM_NIET_GESTART 2

typedef struct { uint16_t toetsen; } AdrioKeypadTy;
typedef struct { uint16_t teller; uint16_t opvulling; } AdrioCoinCounterTy;            /* StructLayout Size = 4 */
typedef struct { AdrioCoinCounterTy deur[4]; } AdrioCoinCounterEvtTy;
typedef struct { uint16_t id; uint16_t status; } AdrioTicketFeederEvtTy;
typedef struct { uint16_t knoppen; } AdrioPlayerButtonTy;
typedef struct { uint8_t b[6]; } AdrioSpinnerStatusTy;
typedef struct { uint16_t versie; uint16_t revisie; } AdrioTargetVersionTy;
typedef struct { uint16_t id; uint16_t aantal; } AdrioTicketToFeedTy;
typedef struct { uint16_t aantal[4]; uint16_t leeg; } AdrioFeededTicketTy;
typedef struct { uint16_t sknop; uint16_t ticket_munt; uint16_t toetsen; } AdrioReadInputTy;
typedef struct { uint16_t vlaggen; uint16_t teller[8]; } AdrioPlayerButtonInfoTy;

typedef void (__cdecl *FnToets)(AdrioKeypadTy);
typedef void (__cdecl *FnMunt)(AdrioCoinCounterEvtTy);
typedef void (__cdecl *FnTicket)(AdrioTicketFeederEvtTy);
typedef void (__cdecl *FnKnop)(AdrioPlayerButtonTy);
typedef void (__cdecl *FnSpinner)(AdrioSpinnerStatusTy);

static CRITICAL_SECTION slot;
static HMODULE deze;
static HANDLE draad;
static volatile LONG stoppen;
static int gestart;
static FnToets cb_toets;
static FnMunt cb_munt;
static FnKnop cb_knop;
static rgk_tabel tabel;
static rgk_staat staat;
static uint16_t gevoerd[4], te_voeren[4], lampen, eventmask = 0xF;
static const volatile uint8_t *beeld;

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
    const char *pad = getenv("RG_KABOOM_LOG");
    char vol[MAX_PATH + 64];
    if (pad && *pad && !(pad[0] == '\\' || pad[0] == '/' || (pad[0] && pad[1] == ':'))) {
        DWORD n = GetModuleFileNameA(deze, vol, MAX_PATH);
        char *schuin = n ? strrchr(vol, '\\') : NULL;
        if (schuin && strlen(pad) < 60) { strcpy(schuin + 1, pad); pad = vol; }
    }
    FILE *f = pad && *pad ? fopen(pad, "a") : NULL;
    if (f) { fputs("rg-adrio: ", f); fputs(regel, f); fclose(f); }
    else { OutputDebugStringA("rg-adrio: "); OutputDebugStringA(regel); }
}

/* De tabel naast deze DLL (rg-adrio.ini); geen bestand = de standaard (Rampage); een fout = geen invoer. */
static void lees_tabel(void)
{
    char pad[MAX_PATH + 32];
    DWORD n = GetModuleFileNameA(deze, pad, MAX_PATH);
    char *schuin = n ? strrchr(pad, '\\') : NULL;
    if (!schuin) { rgk_standaard(&tabel); logf_("tabel: standaard (geen pad van de DLL)"); return; }
    strcpy(schuin + 1, "rg-adrio.ini");
    FILE *f = fopen(pad, "rb");
    if (!f) { rgk_standaard(&tabel); logf_("tabel: %s ontbreekt, de standaard (Rampage)", pad); return; }
    static char tekst[16384];
    size_t lengte = fread(tekst, 1, sizeof tekst - 1, f);
    fclose(f);
    tekst[lengte] = 0;
    char fout[160];
    if (rgk_lees_tabel(tekst, &tabel, fout, sizeof fout) != 0) {
        memset(&tabel, 0, sizeof tabel);
        logf_("tabel %s AFGEKEURD (%s): er komt GEEN invoer", pad, fout);
        return;
    }
    logf_("tabel: %s", pad);
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
    DWORD volgende_poging = 0;
    while (!stoppen) {
        if (!beeld && GetTickCount() - volgende_poging < 0x80000000u) {
            beeld = open_beeld();
            volgende_poging = GetTickCount() + 500;
        }
        if (beeld) {
            uint32_t bits;
            memcpy(&bits, (const void *)(beeld + 8), 4);
            EnterCriticalSection(&slot);
            int v = rgk_stap(&tabel, &staat, bits);
            AdrioKeypadTy toets = {staat.toetsen};
            AdrioPlayerButtonTy knop = {staat.knoppen};
            AdrioCoinCounterEvtTy munt;
            memset(&munt, 0, sizeof munt);
            for (int d = 0; d < 4; d++) munt.deur[d].teller = staat.munt[d];
            FnToets ft = (eventmask & 1) ? cb_toets : NULL;
            FnMunt fm = (eventmask & 2) ? cb_munt : NULL;
            FnKnop fk = (eventmask & 8) ? cb_knop : NULL;
            LeaveCriticalSection(&slot);
            if ((v & RGK_VERANDERD_TOETSEN) && ft) ft(toets);
            if ((v & RGK_VERANDERD_KNOPPEN) && fk) fk(knop);
            if ((v & RGK_VERANDERD_MUNT) && fm) fm(munt);
        }
        Sleep(4);
    }
    return 0;
}

EXPORT uint8_t __cdecl InitLib(int poort, FnToets toets, FnMunt munt, FnTicket ticket, FnKnop knop, FnSpinner spinner)
{
    (void)ticket;
    (void)spinner;
    EnterCriticalSection(&slot);
    cb_toets = toets;
    cb_munt = munt;
    cb_knop = knop;
    int was = gestart;
    if (!gestart) {
        lees_tabel();
        memset(&staat, 0, sizeof staat);
        memset(gevoerd, 0, sizeof gevoerd);
        memset(te_voeren, 0, sizeof te_voeren);
        gestart = 1;
    }
    LeaveCriticalSection(&slot);
    if (!was) {
        stoppen = 0;
        draad = CreateThread(NULL, 0, lus, NULL, 0, NULL);
        logf_("InitLib(poort %d): bord van RavenGun actief", poort);
    }
    return KABOOM_OK;
}

EXPORT void __cdecl CloseLib(void)
{
    if (draad) {
        InterlockedExchange(&stoppen, 1);
        WaitForSingleObject(draad, 1000);
        CloseHandle(draad);
        draad = NULL;
    }
    EnterCriticalSection(&slot);
    gestart = 0;
    cb_toets = NULL;
    cb_munt = NULL;
    cb_knop = NULL;
    LeaveCriticalSection(&slot);
}

EXPORT uint8_t __cdecl GetTargetVersion(AdrioTargetVersionTy *v)
{
    if (!v) return KABOOM_NIET_GESTART;
    v->versie = 9;
    v->revisie = 0;
    return gestart ? KABOOM_OK : KABOOM_NIET_GESTART;
}

EXPORT BOOL __cdecl GetInvalidTargetVersion(AdrioTargetVersionTy *v)
{
    if (v) { v->versie = 0; v->revisie = 0; }
    return FALSE;
}

EXPORT uint8_t __cdecl ResetTarget(void) { return KABOOM_OK; }

EXPORT uint8_t __cdecl GetReadInputAdrio5(AdrioReadInputTy *r)
{
    if (!r) return KABOOM_NIET_GESTART;
    EnterCriticalSection(&slot);
    r->sknop = staat.sknop;
    r->toetsen = staat.toetsen;
    r->ticket_munt = 0;
    for (int d = 0; d < 4; d++)
        if (tabel.bron[RGK_MUNT1 + d].n && staat.begonnen) {
            for (int k = 0; k < tabel.bron[RGK_MUNT1 + d].n; k++)
                if ((staat.vorige >> tabel.bron[RGK_MUNT1 + d].bit[k]) & 1u) r->ticket_munt |= (uint16_t)(16u << (2 * d));
        }
    LeaveCriticalSection(&slot);
    return KABOOM_OK;
}

EXPORT uint8_t __cdecl GetCurrentCoinCountAdrio5(uint16_t deur, AdrioCoinCounterTy *c)
{
    if (!c) return KABOOM_NIET_GESTART;
    EnterCriticalSection(&slot);
    c->teller = deur < 4 ? staat.munt[deur] : 0;
    c->opvulling = 0;
    LeaveCriticalSection(&slot);
    return KABOOM_OK;
}

EXPORT uint8_t __cdecl ResetCoinCountAdrio5(uint16_t deuren)
{
    EnterCriticalSection(&slot);
    rgk_munt_terug(&staat, deuren);
    LeaveCriticalSection(&slot);
    return KABOOM_OK;
}

EXPORT uint8_t __cdecl GetPlayerButtonStatus(AdrioPlayerButtonInfoTy *i)
{
    if (!i) return KABOOM_NIET_GESTART;
    EnterCriticalSection(&slot);
    memset(i, 0, sizeof *i);
    i->vlaggen = (uint16_t)((staat.knoppen & 0x0FFFu) | (uint16_t)((lampen & 0xFu) << 12));
    LeaveCriticalSection(&slot);
    return KABOOM_OK;
}

EXPORT uint8_t __cdecl ResetPlayerButtonCount(uint16_t knoppen) { (void)knoppen; return KABOOM_OK; }
EXPORT uint8_t __cdecl SetPlayerButtonLight(uint16_t l) { lampen |= l; return KABOOM_OK; }
EXPORT uint8_t __cdecl ClrPlayerButtonLight(uint16_t l) { lampen &= (uint16_t)~l; return KABOOM_OK; }
EXPORT uint8_t __cdecl SetCoinOutput(uint16_t o) { (void)o; return KABOOM_OK; }
EXPORT uint8_t __cdecl ClrCoinOutput(uint16_t o) { (void)o; return KABOOM_OK; }
EXPORT uint8_t __cdecl GetEventMaskAdrio5(uint16_t *m) { if (m) *m = eventmask; return KABOOM_OK; }
EXPORT uint8_t __cdecl SetEventMaskAdrio5(uint16_t m) { eventmask |= m; return KABOOM_OK; }
EXPORT uint8_t __cdecl ClrEventMaskAdrio5(uint16_t m) { eventmask &= (uint16_t)~m; return KABOOM_OK; }
EXPORT uint8_t __cdecl SetMotorPwmDutyCycle(uint8_t motor, uint8_t duty, uint16_t ms) { (void)motor; (void)duty; (void)ms; return KABOOM_OK; }
EXPORT uint8_t __cdecl GetMotorPwmDutyCycle(uint8_t motor, uint8_t *duty) { (void)motor; if (duty) *duty = 0; return KABOOM_OK; }
EXPORT uint8_t __cdecl StopMotorPwm(uint8_t motor) { (void)motor; return KABOOM_OK; }

/* De ticketautomaten: SetTicketFeederCountToFeed zet het doel (de tellerstand die bereikt moet worden, zie SendTicketsToFeeder in de
 * game); de automaat geeft meteen alles, dus de teller staat direct op het doel en is nooit leeg. */
EXPORT uint8_t __cdecl SetTicketFeederCountToFeedAdrio5(AdrioTicketToFeedTy t)
{
    if (t.id > 3) return KABOOM_OK;
    EnterCriticalSection(&slot);
    te_voeren[t.id] = t.aantal;
    gevoerd[t.id] = t.aantal;
    LeaveCriticalSection(&slot);
    return KABOOM_OK;
}

EXPORT uint8_t __cdecl GetTicketFeederCountToFeedAdrio5(uint16_t id, AdrioTicketToFeedTy *t)
{
    if (!t) return KABOOM_NIET_GESTART;
    t->id = id;
    t->aantal = id < 4 ? te_voeren[id] : 0;
    return KABOOM_OK;
}

EXPORT uint8_t __cdecl GetTicketFeederCountAdrio5(AdrioFeededTicketTy *f)
{
    if (!f) return KABOOM_NIET_GESTART;
    EnterCriticalSection(&slot);
    for (int i = 0; i < 4; i++) f->aantal[i] = gevoerd[i];
    f->leeg = 0;
    LeaveCriticalSection(&slot);
    return KABOOM_OK;
}

EXPORT uint8_t __cdecl ResetTicketFeederAdrio5(uint16_t automaten)
{
    EnterCriticalSection(&slot);
    for (int i = 0; i < 4; i++)
        if ((automaten >> i) & 1u) gevoerd[i] = te_voeren[i] = 0;
    LeaveCriticalSection(&slot);
    return KABOOM_OK;
}

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
