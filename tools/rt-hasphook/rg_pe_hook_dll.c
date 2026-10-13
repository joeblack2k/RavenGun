/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rg_pe_hook_dll.c — de Windows-kant van de generieke PE-inline-hook: een DLL die in het spelproces
 * geladen wordt en daar de per-game lijst toepast op het BEELD VAN HET SPEL.
 *
 * WAAROM EEN DLL EN GEEN ELF-PRELOAD
 * ----------------------------------
 * Luigi's Mansion is een PE (VACUUM.exe) onder Wine. Uit B0 (patches/LuigisMansion/B0-LEGE-DLL.md)
 * is gemeten dat de lader een DLL in het spelproces laadt -- `OpenParrotLoader64.exe <kern-DLL>
 * <exe>` -- en dat zelfs een DLL zonder entry point verdragen wordt. Dat is dus het bewezen kanaal,
 * en het is dezelfde plek waar de gesloten kern zit.
 *
 * WAT HET DOET
 * ------------
 * In DllMain(PROCESS_ATTACH): het beeld van het HOOFDPROGRAMMA (het spel, niet deze DLL) opzoeken,
 * de PE-kop eruit lezen, en `rg_pe_hook()` loslaten. Die keurt elk adres en zet de prologues om;
 * alle kennis zit in het ini-bestand, niet hier. Deze DLL bevat daarom geen enkele game-naam.
 *
 * OMGEVING
 *   RG_HOOK_INI   het ini-bestand (verplicht; zonder dit gebeurt er niets)
 *   RG_HOOK_LOG   pad voor het logboek (standaard rg-pe-hook.log in de werkmap)
 *
 * BOUWEN (mingw in de podman-image van het project; zie scripts/build-hasp.sh voor hetzelfde recept)
 *   x86_64-w64-mingw32-gcc -shared -O2 -o rg-pe-hook.dll \
 *       tools/rt-hasphook/rg_pe_hook_dll.c tools/rt-hasphook/rg_pe_hook.c \
 *       -Wl,--image-base,0x180000000 -Wl,--no-insert-timestamp
 */
#if defined(_WIN32)

#include "rg_pe_hook.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static rg_pe_ctx G;
static FILE *G_log;

/* Wordt door TODO-stubs aangeroepen: de index komt van de stub zelf. */
static void rg_todo_melder(int idx)
{
    if (G_log) {
        fprintf(G_log, "[rg-pe-hook]   [stub] %s: ANTWOORD NOG NIET GEMETEN (TODO) -- geeft 0 terug\n",
                (idx >= 0 && idx < G.n) ? G.regels[idx].naam : "?");
        fflush(G_log);
    }
}

static void rg_meld(const char *fmt, ...)
{
    va_list ap;
    if (!G_log)
        return;
    fprintf(G_log, "[rg-pe-hook] ");
    va_start(ap, fmt);
    vfprintf(G_log, fmt, ap);
    va_end(ap);
    fputc('\n', G_log);
    fflush(G_log);
}

static void rg_doe_het_werk(void);

/* De eigen thread: hier gebeurt het kijken en omzetten, niet op de hoofdthread (zie DllMain). */
static DWORD WINAPI rg_werk_thread(LPVOID arg)
{
    (void)arg;
    rg_doe_het_werk();
    return 0;
}

static void rg_doe_het_werk(void)
{
    const char *ini = getenv("RG_HOOK_INI");
    const char *logpad = getenv("RG_HOOK_LOG");
    HMODULE spel;
    rg_pe_kop kop;
    int n;

    if (!ini || !*ini) {
        /* Niets doen is de veilige kant: zonder lijst weten we niet wat we mogen aanraken. */
        return;
    }
    G_log = fopen(logpad && *logpad ? logpad : "rg-pe-hook.log", "a");
    if (!G_log)
        G_log = fopen("C:\\rg-pe-hook.log", "a");
    if (!G_log)
        return;

    rg_meld("wachtroute: op een eigen thread gezet; de hoofdthread gaat verder");
    spel = GetModuleHandleW(NULL);            /* het spel, niet deze DLL */
    if (!spel) {
        rg_meld("geen hoofdmodule gevonden");
        return;
    }
    if (!rg_pe_lees_kop((const unsigned char *)spel, 0x1000, &kop)) {
        rg_meld("het hoofdprogramma op %p heeft geen leesbare PE-kop", (void *)spel);
        return;
    }
    rg_meld("hoofdprogramma op %p, ImageBase 0x%llx, SizeOfImage 0x%x", (void *)spel,
            (unsigned long long)kop.imagebase, kop.grootte_beeld);

    memset(&G, 0, sizeof G);
    G.lijst_pad = ini;
    G.beeld = (unsigned char *)spel;
    G.beeld_n = (size_t)kop.grootte_beeld;
    G.log = G_log;
    G.todo_melder = rg_todo_melder;

    n = rg_pe_hook(&G);
    rg_meld("resultaat: %d van %d aangehaakt", n, G.n);
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    HANDLE h;
    (void)hinst;
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    /*
     * NIET op de hoofdthread wachten. Deze DLL wordt via TP_PRELOAD_DLL geladen, dus op de
     * HOOFDTHREAD, in de aanhechtingsfase, VOORDAT het spel zijn entry point uitvoert. Het spel kan
     * pas ontpakken nadat de lader die hoofdthread hervat, en de lader hervat pas als deze functie
     * terugkeert. Wachten hier is dus een deadlock by construction. Dat is gemeten: de wachtroute
     * wachtte 10 s op de hoofdthread, `.text` bleef 100 procent nul (de pagina's vullen pas rond
     * t=4,7 s), en het proces leefde 16,2 s in plaats van ongeveer 6 s -- het was niet geholpen, het
     * werd alleen vastgehouden. Daarom: meteen terugkeren, en het kijken en omzetten op een eigen
     * thread die in dezelfde initialisatie wordt gemaakt.
     *
     * (CreateThread vanuit DllMain is de gangbare vorm hiervoor: het gevaar zit in het synchroon
     * wachten onder de loader-lock, niet in het starten van een thread. De thread raakt de
     * loader-lock niet aan: hij leest `GetModuleHandleW(NULL)` -- de PEB, geen lock -- en schrijft
     * alleen naar het beeld en naar een logbestand.)
     */
    h = CreateThread(NULL, 0, rg_werk_thread, NULL, 0, NULL);
    if (h)
        CloseHandle(h);
    else if (G_log)
        rg_meld("kon de eigen thread niet starten; er wordt niets aangehaakt");
    return TRUE;
}

/* Dwingt een .reloc af, zodat de DLL verplaatsbaar is en LoadLibraryW niet op een bezet ImageBase
   stukloopt (dezelfde les als in B0-LEGE-DLL.md). */
void *const rg_pe_hook_ankers[2] = { (void *)&DllMain, (void *)&rg_pe_hook_ankers };

#endif /* _WIN32 */
