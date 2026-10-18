/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * proef_harnas.c -- een proefprogramma (geen spel) voor de toetsenbordfilter van onze Usbioextreme.dll (Far Cry: Paradise Lost) onder
 * Wine. Het importeert DINPUT8!DirectInput8Create statisch, net als Farcry_R.exe (dat is de importtabel die de DLL omzet), laadt de
 * DLL, maakt het toetsenbordapparaat zoals CryInput (GUID_SysKeyboard, c_dfDIKeyboard, achtergrond, niet exclusief), drukt met
 * keybd_event de toetsen '5' (munt van de daemon) en 'W' (een toets die de game zelf gebruikt) en leest de toestand, en bevraagt
 * ook de gebufferde route (GetDeviceData).
 *
 * Gebruik: harnas.exe blokkeren | doorlaten | doorgifte
 *   blokkeren / doorlaten  het apparaat houdt '5', 'W' en '6' vast (proef_nepdinput8.c, native dinput8): '5' en '6' moeten weg
 *                          (blokkeren) of er zijn (doorlaten = RG_USBIO_KEYS=1, de controle op de proef zelf; zonder die controle zegt
 *                          "5 ontbreekt" niets). 'W' moet er altijd zijn.
 *   doorgifte              Wine's eigen dinput8: alleen controleren dat de haken de echte aanroepen doorgeven (zonder scherm komt
 *                          een toets uit SendInput niet bij DirectInput aan, dus de inhoud van de toestand zegt hier niets).
 * Exitcode 0 = verwacht resultaat.
 */
#define DIRECTINPUT_VERSION 0x0800
#define CINTERFACE
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>

static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

static void pomp(int ms)
{
    DWORD t0 = GetTickCount();
    MSG m;
    while ((int)(GetTickCount() - t0) < ms) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
        Sleep(5);
    }
}

static void toets(WORD vk, int neer)
{
    INPUT in;
    memset(&in, 0, sizeof in);
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyA(vk, 0);
    in.ki.dwFlags = neer ? 0 : KEYEVENTF_KEYUP;
    printf("SendInput(vk 0x%02x, %s) = %u\n", vk, neer ? "neer" : "op", SendInput(1, &in, sizeof in));
}

int main(int argc, char **argv)
{
    int doorlaten = argc > 1 && !strcmp(argv[1], "doorlaten");
    int doorgifte = argc > 1 && !strcmp(argv[1], "doorgifte");   /* alleen: de haken geven de echte aanroepen van Wine ongemoeid door */
    WNDCLASSA wc;
    HWND hw;
    LPDIRECTINPUT8A di = NULL;
    LPDIRECTINPUTDEVICE8A kb = NULL;
    HRESULT hr;
    unsigned char st[256];
    DIDEVICEOBJECTDATA ev[32];
    DWORD n = 32, i;
    int zag5_state = 0, zagw_state = 0, zag5_data = 0, zagw_data = 0;
    HMODULE usb;

    usb = LoadLibraryA("Usbioextreme.dll");
    printf("Usbioextreme.dll: %s\n", usb ? "geladen" : "NIET geladen");
    if (!usb) return 2;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "rgproef";
    RegisterClassA(&wc);
    hw = CreateWindowA("rgproef", "rgproef", WS_POPUP, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    if (!hw) hw = CreateWindowA("rgproef", "rgproef", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);   /* zonder scherm */
    if (!hw) hw = GetDesktopWindow();
    printf("venster: %p\n", (void *)hw);

    hr = DirectInput8Create(GetModuleHandleA(NULL), 0x0800, &IID_IDirectInput8A, (void **)&di, NULL);
    printf("DirectInput8Create hr=0x%lx\n", (unsigned long)hr);
    if (hr < 0) return 2;
    hr = IDirectInput8_CreateDevice(di, &GUID_SysKeyboard, &kb, NULL);
    printf("CreateDevice(GUID_SysKeyboard) hr=0x%lx\n", (unsigned long)hr);
    if (hr < 0) return 2;
    hr = IDirectInputDevice8_SetDataFormat(kb, &c_dfDIKeyboard);
    printf("SetDataFormat hr=0x%lx\n", (unsigned long)hr);
    hr = IDirectInputDevice8_SetCooperativeLevel(kb, hw, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    printf("SetCooperativeLevel hr=0x%lx\n", (unsigned long)hr);
    {   /* gebufferd: zonder buffergrootte geeft GetDeviceData niets */
        DIPROPDWORD pd;
        pd.diph.dwSize = sizeof pd; pd.diph.dwHeaderSize = sizeof pd.diph; pd.diph.dwObj = 0; pd.diph.dwHow = DIPH_DEVICE; pd.dwData = 64;
        hr = IDirectInputDevice8_SetProperty(kb, DIPROP_BUFFERSIZE, &pd.diph);
        printf("DIPROP_BUFFERSIZE hr=0x%lx\n", (unsigned long)hr);
    }
    hr = IDirectInputDevice8_Acquire(kb);
    printf("Acquire hr=0x%lx\n", (unsigned long)hr);
    pomp(300);

    toets('5', 1);
    toets('W', 1);
    pomp(400);
    printf("GetAsyncKeyState('W') = 0x%04x\n", (unsigned)GetAsyncKeyState('W'));
    memset(st, 0, sizeof st);
    hr = IDirectInputDevice8_GetDeviceState(kb, sizeof st, st);
    printf("GetDeviceState hr=0x%lx DIK_5=%02x DIK_W=%02x\n", (unsigned long)hr, st[DIK_5], st[DIK_W]);
    zag5_state = (st[DIK_5] & 0x80) != 0;
    zagw_state = (st[DIK_W] & 0x80) != 0;
    /* GetDeviceState leest de buffer niet leeg; GetDeviceData doet dat wel */
    hr = IDirectInputDevice8_GetDeviceData(kb, sizeof ev[0], ev, &n, 0);
    printf("GetDeviceData hr=0x%lx n=%lu\n", (unsigned long)hr, (unsigned long)n);
    for (i = 0; hr >= 0 && i < n; i++) {
        printf("  gebeurtenis dwOfs=0x%02lx dwData=0x%02lx\n", (unsigned long)ev[i].dwOfs, (unsigned long)ev[i].dwData);
        if (ev[i].dwOfs == DIK_5) zag5_data = 1;
        if (ev[i].dwOfs == DIK_W) zagw_data = 1;
    }
    toets('5', 0);
    toets('W', 0);
    pomp(100);

    if (doorgifte) { printf("doorgifte: ok (hr van alle aanroepen >= 0)\n"); return 0; }
    printf("resultaat: state 5=%d W=%d; data 5=%d W=%d (modus %s)\n", zag5_state, zagw_state, zag5_data, zagw_data, doorlaten ? "doorlaten" : "blokkeren");
    if (!zagw_state && !zagw_data) { printf("FOUT: ook de toets W bereikt DirectInput niet: deze proef zegt niets over de filter\n"); return 3; }
    if (doorlaten) return (zag5_state || zag5_data) ? 0 : 4;
    return (!zag5_state && !zag5_data) ? 0 : 5;
}
