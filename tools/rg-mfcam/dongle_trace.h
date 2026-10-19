// SPDX-License-Identifier: GPL-3.0-or-later
// De USB-dongel van Mario Kart DX: het spel zoekt hem via SetupAPI (SetupDiGetClassDevsW met de klasse
// {3abf6f2d-71c4-462a-8a92-1e6861e6af27}) en praat er daarna mee via CreateFile/DeviceIoControl.
// In de TP-vergelijking zijn er GEEN apparaat-handvatten in het proces te zien: de TP-kern beantwoordt
// dit dus in het proces zelf.
//
// Met RG_DONGLE=trace loggen we alleen wat het spel vraagt. Met RG_DONGLE=fake wijzen we een eigen
// apparaat aan: de klasse-enumeratie geeft één interface, de detail-aanroep geeft een pad, en
// CreateFile op dat pad geeft een pijp waarvan wij het andere eind vasthouden. De ioctl-codes en de
// antwoorden worden gelogd; een eerste antwoord is een nul-buffer van de gevraagde lengte.
#pragma once

// SetupDiCreateDeviceInfoW is in de huidige MinGW-koppen als verouderd gemarkeerd; wij hebben hem
// juist nodig om een eigen apparaat in de lijst te zetten.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace rg_dongle {

using setupdi_get_class_devs_w_fn = HDEVINFO(WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);
using setupdi_enum_device_interfaces_fn = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
using setupdi_get_device_interface_detail_w_fn = BOOL(WINAPI*)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD);
using create_file_w_fn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using device_io_control_fn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using read_file_fn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);

inline setupdi_get_class_devs_w_fn g_get_class_devs = nullptr;
inline setupdi_enum_device_interfaces_fn g_enum_interfaces = nullptr;
inline setupdi_get_device_interface_detail_w_fn g_get_detail = nullptr;
inline create_file_w_fn g_create_file_w = nullptr;
inline device_io_control_fn g_device_io_control = nullptr;
inline read_file_fn g_read_file = nullptr;
inline void (*g_log)(const char*, ...) = nullptr;
inline bool g_fake = false;

inline constexpr wchar_t kDonglePad[] = L"\\\\?\\rg-dongle#vid_0000&pid_0000#1";
inline constexpr wchar_t kDonglePijp[] = L"\\\\.\\pipe\\rg-dongle";
inline GUID g_dongle_klasse = {};   // de klasse die het spel vroeg
inline bool g_dongle_klasse_gezet = false;

inline void guid_tekst(const GUID* g, char* out, size_t n) {
    if (!g) { std::snprintf(out, n, "(geen)"); return; }
    std::snprintf(out, n, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                  static_cast<unsigned long>(g->Data1), g->Data2, g->Data3,
                  g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
                  g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

inline bool is_dongle_pad(const wchar_t* p) {
    return p && _wcsicmp(p, kDonglePad) == 0;
}

// De bedoelde klasse: die van het spel, of anders een vaste (alleen voor de zekerheid).
inline const GUID* klasse() {
    static const GUID vast = {0x3abf6f2d, 0x71c4, 0x462a, {0x8a, 0x92, 0x1e, 0x68, 0x61, 0xe6, 0xaf, 0x27}};
    return g_dongle_klasse_gezet ? &g_dongle_klasse : &vast;
}

inline HDEVINFO WINAPI hook_get_class_devs(const GUID* cls, PCWSTR enumerator, HWND parent, DWORD flags) {
    char g[64];
    guid_tekst(cls, g, sizeof g);
    if (g_log)
        g_log("dongel: SetupDiGetClassDevsW %s (enumerator %ls, vlaggen 0x%lx)", g,
              enumerator ? enumerator : L"(geen)", static_cast<unsigned long>(flags));
    if (cls) { g_dongle_klasse = *cls; g_dongle_klasse_gezet = true; }
    if (g_fake) {
        // Een echte lijst met een eigen (denkbeeldig) apparaat erin, zodat de rest van SetupAPI werkt.
        HDEVINFO lijst = SetupDiCreateDeviceInfoList(cls, nullptr);
        if (lijst == INVALID_HANDLE_VALUE) {
            if (g_log) g_log("dongel: SetupDiCreateDeviceInfoList mislukt (%lu)", static_cast<unsigned long>(GetLastError()));
            return lijst;
        }
        SP_DEVINFO_DATA info = {};
        info.cbSize = sizeof info;
        if (!SetupDiCreateDeviceInfoW(lijst, L"RGUSB\\VID_0000&PID_0000", cls, nullptr, nullptr, 0, &info)) {
            if (g_log) g_log("dongel: SetupDiCreateDeviceInfoW mislukt (%lu)", static_cast<unsigned long>(GetLastError()));
            return lijst;
        }
        SP_DEVICE_INTERFACE_DATA iface = {};
        iface.cbSize = sizeof iface;
        if (!SetupDiCreateDeviceInterfaceW(lijst, &info, cls, nullptr, 0, &iface)) {
            if (g_log) g_log("dongel: SetupDiCreateDeviceInterfaceW mislukt (%lu)", static_cast<unsigned long>(GetLastError()));
        } else if (g_log) {
            g_log("dongel: eigen apparaat in de lijst gezet");
        }
        return lijst;
    }
    if (!g_get_class_devs) return INVALID_HANDLE_VALUE;
    return g_get_class_devs(cls, enumerator, parent, flags);
}

inline BOOL WINAPI hook_enum_device_interfaces(HDEVINFO set, PSP_DEVINFO_DATA info, const GUID* cls, DWORD index,
                                               PSP_DEVICE_INTERFACE_DATA data) {
    if (g_fake) {
        if (index != 0 || !cls) return FALSE;   // precies één dongel
        if (data) {
            data->cbSize = sizeof *data;
            data->InterfaceClassGuid = *cls;
            data->Flags = 0;
            data->Reserved = 0;
        }
        if (g_log) g_log("dongel: SetupDiEnumDeviceInterfaces index %lu -> eigen dongel", static_cast<unsigned long>(index));
        return TRUE;
    }
    if (g_log) g_log("dongel: SetupDiEnumDeviceInterfaces index %lu", static_cast<unsigned long>(index));
    if (!g_enum_interfaces) return FALSE;
    return g_enum_interfaces(set, info, cls, index, data);
}

inline BOOL WINAPI hook_get_device_interface_detail_w(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                      PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail, DWORD size,
                                                      PDWORD nodig) {
    if (g_fake) {
        const DWORD bytes = static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + (wcslen(kDonglePad) + 1) * sizeof(wchar_t));
        if (nodig) *nodig = bytes;
        if (!detail || size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        detail->cbSize = sizeof *detail;
        wcscpy(detail->DevicePath, kDonglePad);
        if (g_log) g_log("dongel: apparaatpad %ls", detail->DevicePath);
        return TRUE;
    }
    if (!g_get_detail) return FALSE;
    const BOOL ok = g_get_detail(set, iface, detail, size, nodig);
    if (g_log && ok) g_log("dongel: apparaatpad %ls", detail ? detail->DevicePath : L"(geen)");
    return ok;
}

inline HANDLE WINAPI hook_create_file_w(LPCWSTR naam, DWORD toegang, DWORD deel, LPSECURITY_ATTRIBUTES sa,
                                        DWORD maken, DWORD vlaggen, HANDLE sjabloon) {
    if (g_fake && is_dongle_pad(naam)) {
        // Het pad van ons apparaat wordt een pijp: het spel krijgt het clienteinde, wij het andere.
        HANDLE pijp = CreateNamedPipeW(kDonglePijp, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                      PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
        if (pijp == INVALID_HANDLE_VALUE) {
            if (g_log) g_log("dongel: CreateNamedPipeW mislukt (%lu)", static_cast<unsigned long>(GetLastError()));
            return INVALID_HANDLE_VALUE;
        }
        HANDLE client = CreateFileW(kDonglePijp, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (client == INVALID_HANDLE_VALUE) {
            if (g_log) g_log("dongel: verbinden met eigen pijp mislukt (%lu)", static_cast<unsigned long>(GetLastError()));
            CloseHandle(pijp);
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
        if (g_log) g_log("dongel: het spel opent de dongel -> eigen pijp");
        return client;
    }
    if (!g_create_file_w) return INVALID_HANDLE_VALUE;
    return g_create_file_w(naam, toegang, deel, sa, maken, vlaggen, sjabloon);
}

inline BOOL WINAPI hook_device_io_control(HANDLE dev, DWORD code, LPVOID in, DWORD in_len, LPVOID out, DWORD out_len,
                                          LPDWORD terug, LPOVERLAPPED ov) {
    if (g_fake && dev && dev != INVALID_HANDLE_VALUE) {
        // Wat vraagt het spel, en met welke buffer? (De eerste versie antwoordt met nullen.)
        if (g_log)
            g_log("dongel: DeviceIoControl code 0x%08lx in %lu uit %lu%s", static_cast<unsigned long>(code),
                  static_cast<unsigned long>(in_len), static_cast<unsigned long>(out_len),
                  in && in_len ? " (invoer aanwezig)" : "");
        if (out && out_len) {
            std::memset(out, 0, out_len);
            if (terug) *terug = out_len;
            SetLastError(ERROR_SUCCESS);
            return TRUE;
        }
        if (terug) *terug = 0;
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (g_log)
        g_log("dongel: DeviceIoControl code 0x%08lx in %lu uit %lu", static_cast<unsigned long>(code),
              static_cast<unsigned long>(in_len), static_cast<unsigned long>(out_len));
    if (!g_device_io_control) return FALSE;
    return g_device_io_control(dev, code, in, in_len, out, out_len, terug, ov);
}

}  // namespace rg_dongle
