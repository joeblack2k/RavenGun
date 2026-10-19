/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * entry_win.c — de Windows-koppeling van de geheugen-motor van RavenGun.
 *
 * Dit is de DLL die onder Proton/Wine (of native Windows) in het spelproces geladen wordt. Hij doet
 * hetzelfde als de Linux-kant:
 *
 *   1. `DllMain` start de motor;
 *   2. een ACHTERGRONDTHREAD (`CreateThread`) past de lijst toe en blijft kijken of een module uit
 *      de lijst inmiddels geladen is;
 *   3. het opzoeken van modules gaat via `CreateToolhelp32Snapshot(TH32CS_SNAPMODULE)`.
 *
 * Alleen kernel32: geen Detours, geen injectie, geen extra afhankelijkheden.
 *
 * WAAROM HET WERK NIET IN DllMain ZELF GEBEURT
 * --------------------------------------------
 * Tijdens `DLL_PROCESS_ATTACH` houdt de thread de loader-lock vast. `CreateToolhelp32Snapshot`
 * vraagt diezelfde lock, dus zouden we onszelf daar laten wachten. Daarom doet DllMain niets anders
 * dan de draad starten; die draad loopt vanzelf verder zodra DllMain klaar is. DllMain wacht nooit op
 * de draad, dus er is geen impasse.
 *
 * Omgevingsvariabelen: zie entry_linux.c (dezelfde vier).
 *
 * Bouwen: tools/dev/build-memory.sh (mingw-w64 in de podman-image localhost/ravengun-mingw)
 *   x86_64-w64-mingw32-gcc -shared -O2 -o ravengun-patchmem.dll   src/patchmem.c src/entry_win.c
 *   i686-w64-mingw32-gcc   -shared -O2 -o ravengun-patchmem32.dll src/patchmem.c src/entry_win.c
 */
#define _WIN32_WINNT 0x0600

#include "patchmem.h"

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- de twee haken */

/* Tijdens de SYNCHRONE eerste ronde (zie DllMain) melden we alleen de hoofdmodule. Reden: die ronde
 * loopt in DllMain, waar de loader-lock van deze draad is; CreateToolhelp32Snapshot zou daar op
 * wachten. GetModuleHandle(NULL) mag wel (de lock is herintreedbaar voor dezelfde draad) en de
 * hoofdmodule is precies wat een spel als Robin Hood nodig heeft: zijn beveiligingscontrole loopt
 * meteen bij het opstarten, nog vóór de achtergrondthread aan de beurt komt. */
static int g_alleen_hoofdmodule = 0;

static int loop_modules(int (*cb)(const char *naam, void *basis, void *user), void *user)
{
    HANDLE momentopname;
    MODULEENTRY32 module;
    int gestopt = 0;

    if (g_alleen_hoofdmodule) {
        HMODULE hoofd = GetModuleHandleW(NULL);
        wchar_t pad[MAX_PATH];
        const wchar_t *naam = L"";
        if (!hoofd) return 0;
        if (GetModuleFileNameW(hoofd, pad, (DWORD)(sizeof pad / sizeof pad[0])) > 0) {
            const wchar_t *schuin = wcsrchr(pad, L'\\');
            naam = schuin ? schuin + 1 : pad;
        }
        {
            char smal[MAX_PATH];
            int i;
            for (i = 0; i < (int)(sizeof smal / sizeof smal[0]) - 1 && naam[i]; ++i)
                smal[i] = (char)(naam[i] < 128 ? naam[i] : '?');
            smal[i] = '\0';
            return cb(smal, (void *)hoofd, user) ? 1 : 0;
        }
    }

    momentopname = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                            GetCurrentProcessId());
    if (momentopname == INVALID_HANDLE_VALUE) return 0;

    module.dwSize = sizeof module;
    if (Module32First(momentopname, &module)) {
        do {
            if (cb(module.szModule, (void *)module.modBaseAddr, user)) {
                gestopt = 1;
                break;
            }
        } while (Module32Next(momentopname, &module));
    }
    CloseHandle(momentopname);
    return gestopt;
}

struct zoekcontext {
    const char *naam;
    void *basis;
    int gevonden;
};

static int cb_zoek(const char *naam, void *basis, void *user)
{
    struct zoekcontext *z = (struct zoekcontext *)user;

    if (patchmem_zelfde_module(naam, z->naam)) {
        z->basis = basis;
        z->gevonden = 1;
        return 1;
    }
    return 0;
}

void *patchmem_module_base(const char *naam)
{
    struct zoekcontext z;

    if (!naam || !*naam) return NULL;
    z.naam = naam;
    z.basis = NULL;
    z.gevonden = 0;
    loop_modules(cb_zoek, &z);
    return z.gevonden ? z.basis : NULL;
}

int patchmem_foreach_module(int (*cb)(const char *naam, void *basis, void *user), void *user)
{
    if (!cb) return 0;
    return loop_modules(cb, user);
}

/* ---------------------------------------------------------------- de achtergrondthread */

static int env_int(const char *naam, int standaard)
{
    const char *v = getenv(naam);
    if (!v || !*v) return standaard;
    return atoi(v);
}

static DWORD WINAPI achtergrond(LPVOID arg)
{
    int wacht, poll;
    ULONGLONG begin;

    (void)arg;

    /* De lijst is al gelezen en de hoofdmodule is al gedaan in DllMain (synchrone ronde). Hier dus
     * NIET opnieuw lezen: dat zou de lijst opnieuw opbouwen en elke al toegepaste patch weer als
     * "wachtend" zetten. Alleen wat nog op een module wacht komt hier aan bod. */
    if (patchmem_verbose()) patchmem_toon_modules();
    if (patchmem_pending() == 0) {
        if (patchmem_verbose())
            fprintf(stderr, "patchmem: niets meer te doen na de synchrone ronde\n");
        return 0;
    }

    wacht = env_int("RAVENGUN_PATCHMEM_WACHT", 30);
    poll = env_int("RAVENGUN_PATCHMEM_POLL", 250);
    if (poll < 20) poll = 20;
    if (wacht <= 0) {
        fprintf(stderr, "patchmem: RAVENGUN_PATCHMEM_WACHT=0 -- geen achtergrondthread\n");
        return 0;
    }

    begin = GetTickCount64();
    for (;;) {
        if (patchmem_pending() == 0) break;
        if (GetTickCount64() - begin >= (ULONGLONG)wacht * 1000) {
            patchmem_rapporteer_openstaand();
            break;
        }
        Sleep((DWORD)poll);
        patchmem_apply_pending();
    }
    return 0;
}

/* ---------------------------------------------------------------- het begin */

BOOL WINAPI DllMain(HINSTANCE instantie, DWORD reden, LPVOID gereserveerd)
{
    (void)gereserveerd;

    if (reden == DLL_PROCESS_ATTACH) {
        HANDLE draad;
        int verslag;
        DisableThreadLibraryCalls(instantie);

        /* EERST synchroon de hoofdmodule: de lader laadt deze DLL vlak voordat het spel zijn eerste
         * code draait, en een controle die meteen bij het opstarten loopt (Robin Hood: game.exe
         * 0x4913c0, aangeroepen vanuit de wikkel op 0x401093) is anders al voorbij voordat de
         * achtergrondthread aan de beurt is -- gemeten: de patch stond in het geheugen, maar de vlag
         * van het spel stond al op 2. Hier patchen we dus alleen de exe van het spel zelf; alle
         * andere modules blijven voor de achtergrondthread. */
        g_alleen_hoofdmodule = 1;
        verslag = patchmem_apply_from_env();
        g_alleen_hoofdmodule = 0;
        if (verslag < 0) return TRUE;   /* geen RAVENGUN_PATCHMEM_FILE: niets doen */

        /* Wacht er na deze ronde niets meer, dan is een thread overbodig. */
        if (patchmem_pending() == 0) {
            if (patchmem_verbose())
                fprintf(stderr, "patchmem: alles in de synchrone ronde gedaan; geen achtergrondthread\n");
            return TRUE;
        }

        draad = CreateThread(NULL, 0, achtergrond, NULL, 0, NULL);
        if (draad) {
            CloseHandle(draad);
        } else {
            fprintf(stderr, "patchmem: kon de achtergrondthread niet starten (fout %lu)\n",
                    (unsigned long)GetLastError());
        }
    }
    return TRUE;
}
