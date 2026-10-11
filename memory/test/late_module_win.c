/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * late_module_win.c — de DLL die de Windows-proef PAS LATER laadt.
 *
 * Zelfde gedachte als late_module.c aan de Linux-kant: een module die niet in de exe zit en dus
 * alleen door de geheugen-motor te patchen is.
 */
#define _WIN32_WINNT 0x0600

#include <windows.h>

__declspec(dllexport) unsigned char ravengun_laat_doel_win[16] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a
};

__declspec(dllexport) __attribute__((noinline)) int ravengun_laat_functie_win(void)
{
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instantie, DWORD reden, LPVOID gereserveerd)
{
    (void)instantie;
    (void)reden;
    (void)gereserveerd;
    return TRUE;
}
