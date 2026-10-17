/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * proef_harnas.c -- een proefprogramma (geen spel) voor rg-dinput8 onder Wine: importeert DINPUT8!DirectInput8Create statisch,
 * net als BBPC.exe, en doorloopt de aanroepen van Bug Busters (EnumDevices, CreateDevice, SetCooperativeLevel, GetCapabilities,
 * Acquire, SetDataFormat(c_dfDIJoystick2), EnumObjects + DIPROP_RANGE per as -1000..1000, Poll + GetDeviceState). Het maakt zelf de
 * mapping TeknoParrot_JvsState en een tabel met een vlag, een teller en een patch op zijn eigen globale variabelen, en controleert
 * schaal, munt (teller +1), start (vlag := 1), buiten beeld, trekker en de patch. Draaien: tools/rg-dinput8/wine-proef.sh.
 */
#define DIRECTINPUT_VERSION 0x0800
#define CINTERFACE
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

volatile LONG vlag_doel = 0, teller_doel = 3;
volatile unsigned char patch_doel[4] = {0x6A, 0x01, 0x90, 0x90};
static GUID gevonden[8];
static int ngevonden;
static LPDIRECTINPUTDEVICE8A dev;

static BOOL CALLBACK enum_cb(LPCDIDEVICEINSTANCEA di, LPVOID r)
{
    (void)r;
    printf("enum: %s type 0x%lx guid %08lx..%02x\n", di->tszInstanceName, di->dwDevType, di->guidInstance.Data1, di->guidInstance.Data4[7]);
    gevonden[ngevonden++] = di->guidInstance;
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK obj_cb(LPCDIDEVICEOBJECTINSTANCEA o, LPVOID r)
{
    (void)r;
    if (o->dwType & DIDFT_AXIS) {
        DIPROPRANGE pr;
        pr.diph.dwSize = sizeof pr; pr.diph.dwHeaderSize = sizeof pr.diph; pr.diph.dwObj = o->dwType; pr.diph.dwHow = DIPH_BYID;
        pr.lMin = -1000; pr.lMax = 1000;
        HRESULT hr = IDirectInputDevice8_SetProperty(dev, DIPROP_RANGE, &pr.diph);
        printf("obj %s type 0x%lx ofs %lu range hr 0x%lx\n", o->tszName, o->dwType, o->dwOfs, hr);
    }
    return DIENUM_CONTINUE;
}

int main(void)
{
    HANDLE m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 64, "TeknoParrot_JvsState");
    unsigned char *b = MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 64);
    memset(b, 0, 64);
    b[12] = 128; b[13] = 128; b[14] = 0; b[15] = 0;
    FILE *f = fopen("rg-dinput8.ini", "w");
    fprintf(f, "spelers = 2\np1.x = 12\np1.y = 13\np1.knop0 = 7\np1.knop1 = 8\np2.x = 14\np2.y = 15\np2.knop0 = 12\n"
               "as.x = -999 100\nas.y = -799 1000\nbuiten.x = -1000\nbuiten.y = -800\n"
               "vlag.0x%08lX = 6\nteller.0x%08lX = 2, 3\npatch.0x%08lX = 01 -> 00\n",
            (unsigned long)(ULONG_PTR)&vlag_doel, (unsigned long)(ULONG_PTR)&teller_doel, (unsigned long)(ULONG_PTR)(patch_doel + 1));
    fclose(f);
    /* De DLL leest zijn tabel en patcht in DllMain, dus vóór main: de tabel moet er al zijn als het proces start. Daarom start dit
       programma zichzelf een tweede keer (argument "twee"); de eerste houdt de mapping open en zet de bits. */
    if (!strstr(GetCommandLineA(), " twee")) {
        STARTUPINFOA si = {sizeof si};
        PROCESS_INFORMATION pi;
        char cmd[MAX_PATH + 16];
        GetModuleFileNameA(NULL, cmd, MAX_PATH);
        strcat(cmd, " twee");
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 9;
        /* de mapping moet blijven bestaan: dit proces houdt hem open en zet de bits */
        Sleep(1500);
        b[8] = 0x04;               /* munt 1 */
        Sleep(1500);
        b[8] = 0x80 | 0x40;        /* trekker + start 1 */
        b[12] = 255;               /* buiten beeld */
        WaitForSingleObject(pi.hProcess, 20000);
        DWORD rc = 1;
        GetExitCodeProcess(pi.hProcess, &rc);
        printf("kind: exitcode %lu\n", rc);
        return (int)rc;
    }
    printf("patch_doel na DllMain: %02x %02x\n", patch_doel[0], patch_doel[1]);
    LPDIRECTINPUT8A di = NULL;
    HRESULT hr = DirectInput8Create(GetModuleHandleA(NULL), 0x0800, &IID_IDirectInput8A, (void **)&di, NULL);
    printf("DirectInput8Create hr 0x%lx\n", hr);
    if (FAILED(hr)) return 2;
    hr = IDirectInput8_EnumDevices(di, 0, enum_cb, NULL, DIEDFL_ATTACHEDONLY);
    printf("EnumDevices hr 0x%lx, %d apparaten\n", hr, ngevonden);
    if (ngevonden != 2) return 3;
    hr = IDirectInput8_CreateDevice(di, &gevonden[0], &dev, NULL);
    printf("CreateDevice hr 0x%lx\n", hr);
    if (FAILED(hr)) return 4;
    IDirectInputDevice8_SetCooperativeLevel(dev, NULL, 6);
    DIDEVCAPS caps = {sizeof caps};
    IDirectInputDevice8_GetCapabilities(dev, &caps);
    printf("caps axes %lu buttons %lu type 0x%lx\n", caps.dwAxes, caps.dwButtons, caps.dwDevType);
    IDirectInputDevice8_Acquire(dev);
    hr = IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick2);
    printf("SetDataFormat hr 0x%lx\n", hr);
    IDirectInputDevice8_EnumObjects(dev, obj_cb, NULL, 0);
    DIJOYSTATE2 js;
    int fout = 0;
    for (int t = 0; t < 40; t++) {
        IDirectInputDevice8_Poll(dev);
        hr = IDirectInputDevice8_GetDeviceState(dev, sizeof js, &js);
        if (t == 0 || t == 20 || t == 39)
            printf("t%02d hr 0x%lx lX %ld lY %ld knop0 %02x pov0 %lx vlag %ld teller %ld\n", t, hr, js.lX, js.lY, js.rgbButtons[0],
                   js.rgdwPOV[0], vlag_doel, teller_doel);
        Sleep(100);
    }
    /* verwacht: midden -> lX = -999 + round(1099*128/255) = -447, lY = -799 + round(1799*128/255) = 104; aan het eind buiten beeld,
       trekker, munt 1x geteld, start-vlag 1, patch toegepast */
    if (patch_doel[1] != 0x00) { printf("FOUT patch\n"); fout++; }
    if (teller_doel != 4) { printf("FOUT teller %ld\n", teller_doel); fout++; }
    if (vlag_doel != 1) { printf("FOUT vlag %ld\n", vlag_doel); fout++; }
    if (js.lX != -1000 || js.lY != -800 || js.rgbButtons[0] != 0x80) { printf("FOUT eindtoestand\n"); fout++; }
    IDirectInputDevice8_Release(dev);
    IDirectInput8_Release(di);
    printf(fout ? "harnas: FOUT\n" : "harnas: ok\n");
    return fout ? 1 : 0;
}
