/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * fakecom_game.c - een nep-"arkndd.dll" voor tools/rg-macaw/fakecom-test.sh: doet precies de seriele-poortaanroepen die arkndd.dll in zijn
 * IO-initialisatie doet (COM2 openen, SetupComm, PurgeComm, Get/SetCommState, SetCommTimeouts, 6 bytes schrijven, 0x68 bytes lezen,
 * ClearCommError, sluiten) en schrijft de uitkomst naar fakecom-result.txt. Zo is de haak van rg-macaw te testen zonder het spel.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

__declspec(dllexport) int dll_entry_init(const char *sid, void *cfg)
{
    (void)sid; (void)cfg;
    return 1;
}

__declspec(dllexport) int dll_entry_main(void)
{
    FILE *f = fopen("fakecom-result.txt", "w");
    HANDLE h = CreateFileA("COM2", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DCB dcb;
    COMMTIMEOUTS to;
    COMSTAT st;
    DWORD err = 0, n = 0, i;
    unsigned char cmd[6] = {0x03, 0x8a, 0x00, 0x00, 0x00, 0x8d}, buf[0x68];
    int ok = 1;
    if (!f) return 1;
    if (h == INVALID_HANDLE_VALUE) { fprintf(f, "FOUT: CreateFileA(COM2) gaf INVALID_HANDLE_VALUE\n"); fclose(f); return 1; }
    ok &= SetupComm(h, 0x400, 0x400) != 0;
    ok &= PurgeComm(h, 0xf) != 0;
    memset(&dcb, 0, sizeof dcb);
    ok &= GetCommState(h, &dcb) != 0;
    dcb.BaudRate = 9600; dcb.ByteSize = 8;
    ok &= SetCommState(h, &dcb) != 0;
    memset(&to, 0, sizeof to);
    ok &= GetCommTimeouts(h, &to) != 0;
    to.ReadTotalTimeoutConstant = 1000;
    ok &= SetCommTimeouts(h, &to) != 0;
    ok &= ClearCommError(h, &err, &st) != 0;
    ok &= WriteFile(h, cmd, sizeof cmd, &n, NULL) != 0 && n == sizeof cmd;
    memset(buf, 0xAA, sizeof buf);
    n = 0;
    ok &= ReadFile(h, buf, sizeof buf, &n, NULL) != 0 && n == sizeof buf;
    for (i = 0; i < sizeof buf; i++) if (buf[i] != 0) ok = 0;
    ok &= CloseHandle(h) != 0;
    fprintf(f, ok ? "OK\n" : "FOUT: een van de seriele aanroepen gaf het verkeerde\n");
    fclose(f);
    return 0;
}
