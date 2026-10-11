/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * De Windows-variant van de vooraf te laden bibliotheek: een PE-DLL die dezelfde hasp_-
 * functies levert als de runtime van de fabrikant (hasp_windows_x64_105293.dll enzovoort).
 *
 * De games onder Proton zijn Windows-programma's; daar werkt LD_PRELOAD niet. In plaats
 * daarvan krijgt de DLL de naam die het spel importeert, zet de launcher hem in de view van
 * de game (nooit op de NAS) en dwingt Wine hem af met WINEDLLOVERRIDES.
 *
 * De DllMain doet hetzelfde als de constructor van hasp_preload.c: de dongle uit de
 * omgeving laden en installeren. De DLL is dus geen koppeling die aanroepen onderschept,
 * maar een vervanging van de runtime zelf: het spel laadt hem in plaats van de originele.
 *
 * Bouwen (mingw-w64, zie scripts/build-hasp.sh):
 *   x86_64-w64-mingw32-gcc -shared -O2 -o hasp_windows_x64.dll \
 *       dongle.c hasp_preload_win.c hasp_windows.def
 */
#include <windows.h>

#include "dongle.h"

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
    (void)self; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        (void)dongle_install_from_env();
    } else if (reason == DLL_PROCESS_DETACH) {
        dongle_uninstall();
    }
    return TRUE;
}
