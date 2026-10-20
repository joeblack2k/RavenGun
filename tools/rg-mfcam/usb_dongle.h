// SPDX-License-Identifier: GPL-3.0-or-later
// A USB dongle for games that find theirs by walking the Windows USB tree (switch RG_DONGLE).
//
// Mario Kart Arcade GP DX reads its dongle in a worker thread (MK_AGP3_FINAL.exe 0x8B17B0) the way
// Microsoft's USBView sample does:
//   1. SetupDiGetClassDevsW(GUID_DEVINTERFACE_USB_HOST_CONTROLLER) and, per host controller,
//      CreateFileW on its interface path;
//   2. IOCTL_GET_HCD_DRIVERKEY_NAME and IOCTL_USB_GET_ROOT_HUB_NAME on the controller, then a walk over
//      the PnP tree (CM_Locate_DevNodeW / CM_Get_DevNode_Registry_PropertyW) for the node with that
//      driver key;
//   3. CreateFileW("\\.\<root hub>"), IOCTL_USB_GET_NODE_INFORMATION for the port count, and per port
//      IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX;
//   4. for the device with the dongle's vendor/product id: IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION
//      for the configuration descriptor and the manufacturer, product and serial number strings.
// The game then wants exactly one such device and checks the serial number string (0x607D18): twelve
// digits, "2710", a digit 0-4 or 9, any digit, "02", four digits. Only then does the boot check get
// past its "drive cabinet board ID / serial" rows (state 3 of the dongle object at 0xEC2A04).
//
// Wine has no USB host controllers to show, so with RG_DONGLE=fake this header answers that walk
// itself: one host controller, one root hub with one port, and on that port one device with the
// vendor/product id and strings from the environment. The handles the game gets are real handles to
// the NUL device, so CloseHandle works as usual; only DeviceIoControl on them is answered here.
// Nothing outside this walk is changed: every other class, path, handle and device node goes to the
// original functions. (Every hook has the exact stdcall signature of its API: CM_Get_Child and friends
// take three arguments, SetupDiGetDeviceInterfaceDetailW six. A hook with one argument too few
// unbalances the caller's stack.)
//
//   RG_DONGLE=fake            answer the walk (RG_DONGLE=trace only logs what the game asks)
//   RG_DONGLE_USB=vvvv:pppp   vendor and product id of the device (hex)
//   RG_DONGLE_SERIAL=...      its serial number string
//   RG_DONGLE_MANUFACTURER=.. its manufacturer string (default "RavenGun")
//   RG_DONGLE_PRODUCT=...     its product string (default "USB dongle")
#pragma once

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cwchar>

namespace rg_dongle {

using get_class_devs_fn = HDEVINFO(WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);
using enum_interfaces_fn = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
using get_detail_fn = BOOL(WINAPI*)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD,
                                    PSP_DEVINFO_DATA);
using create_file_w_fn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using device_io_control_fn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using cm_locate_fn = CONFIGRET(WINAPI*)(PDEVINST, DEVINSTID_W, ULONG);
using cm_prop_fn = CONFIGRET(WINAPI*)(DEVINST, ULONG, PULONG, PVOID, PULONG, ULONG);
using cm_relative_fn = CONFIGRET(WINAPI*)(PDEVINST, DEVINST, ULONG);

inline get_class_devs_fn g_get_class_devs = nullptr;
inline enum_interfaces_fn g_enum_interfaces = nullptr;
inline get_detail_fn g_get_detail = nullptr;
inline create_file_w_fn g_create_file_w = nullptr;
inline device_io_control_fn g_device_io_control = nullptr;
inline cm_locate_fn g_cm_locate = nullptr;
inline cm_prop_fn g_cm_prop = nullptr;
inline cm_relative_fn g_cm_child = nullptr;
inline cm_relative_fn g_cm_parent = nullptr;
inline cm_relative_fn g_cm_sibling = nullptr;
inline void (*g_log)(const char*, ...) = nullptr;

inline bool g_fake = false;   // RG_DONGLE=fake
inline unsigned short g_vid = 0, g_pid = 0;
inline wchar_t g_serial[64] = L"";
inline wchar_t g_manufacturer[64] = L"RavenGun";
inline wchar_t g_product[64] = L"USB dongle";

// USB ioctls (usbioctl.h); 0x220408 means "root hub name" on a controller and "node information" on a hub.
constexpr DWORD kIoctlHcdDriverKeyName = 0x220424;
constexpr DWORD kIoctlRootHubName = 0x220408;
constexpr DWORD kIoctlNodeInformation = 0x220408;
constexpr DWORD kIoctlConnectionInfoEx = 0x220448;
constexpr DWORD kIoctlDescriptorFromConnection = 0x220410;

// GUID_DEVINTERFACE_USB_HOST_CONTROLLER
constexpr GUID kHostControllerClass = {0x3abf6f2d, 0x71c4, 0x462a, {0x8a, 0x92, 0x1e, 0x68, 0x61, 0xe6, 0xaf, 0x27}};
constexpr wchar_t kControllerPath[] = L"\\\\?\\rg-usb#hc#0#{3abf6f2d-71c4-462a-8a92-1e6861e6af27}";
constexpr wchar_t kControllerDriverKey[] = L"{36fc9e60-c465-11cf-8056-444553540000}\\rg-0000";
constexpr wchar_t kControllerDescription[] = L"RavenGun USB host controller";
constexpr wchar_t kRootHubName[] = L"RG#ROOT_HUB#0#{f18a0e88-c30c-11d0-8815-00a0c906bed8}";
constexpr wchar_t kRootHubPath[] = L"\\\\.\\RG#ROOT_HUB#0#{f18a0e88-c30c-11d0-8815-00a0c906bed8}";
constexpr ULONG kDonglePort = 1;
constexpr DEVINST kControllerNode = 0x52470001;   // a device node number Wine does not hand out

inline HDEVINFO g_list = INVALID_HANDLE_VALUE;     // the (empty, real) list we handed out for the controller class
inline HANDLE g_controller = INVALID_HANDLE_VALUE;
inline HANDLE g_hub = INVALID_HANDLE_VALUE;

inline unsigned long caller_of(void* ra) {
    return static_cast<unsigned long>(reinterpret_cast<uintptr_t>(ra));
}

inline void env_wide(const char* name, wchar_t* out, size_t n) {
    char v[64];
    const DWORD len = GetEnvironmentVariableA(name, v, sizeof v);
    if (len == 0 || len >= sizeof v) return;
    MultiByteToWideChar(CP_ACP, 0, v, -1, out, static_cast<int>(n));
}

// Reads the device from the environment. Returns false when RG_DONGLE_USB is missing or malformed.
inline bool load_device() {
    char v[32];
    unsigned vid = 0, pid = 0;
    if (GetEnvironmentVariableA("RG_DONGLE_USB", v, sizeof v) == 0 || std::sscanf(v, "%x:%x", &vid, &pid) != 2) return false;
    g_vid = static_cast<unsigned short>(vid);
    g_pid = static_cast<unsigned short>(pid);
    env_wide("RG_DONGLE_SERIAL", g_serial, 64);
    env_wide("RG_DONGLE_MANUFACTURER", g_manufacturer, 64);
    env_wide("RG_DONGLE_PRODUCT", g_product, 64);
    return true;
}

// The answer for a "name" ioctl: ULONG ActualLength, then the name. A buffer too small for the name
// still gets ActualLength, the way the USB stack does it, so the caller can ask again with that size.
inline DWORD put_name(void* out, DWORD out_len, const wchar_t* name) {
    const DWORD total = static_cast<DWORD>(sizeof(ULONG) + (wcslen(name) + 1) * sizeof(wchar_t));
    if (!out || out_len < sizeof(ULONG)) return 0;
    std::memset(out, 0, out_len);
    std::memcpy(out, &total, sizeof total);
    const DWORD room = out_len - static_cast<DWORD>(sizeof(ULONG));
    const DWORD want = total - static_cast<DWORD>(sizeof(ULONG));
    std::memcpy(static_cast<unsigned char*>(out) + sizeof(ULONG), name, room < want ? room : want);
    return out_len < total ? out_len : total;
}

// USB_DEVICE_DESCRIPTOR of the dongle: USB 2.0, vendor class, strings 1/2/3.
inline void device_descriptor(unsigned char* d) {
    const unsigned char desc[18] = {18, 1, 0x00, 0x02, 0, 0, 0, 64,
                                    static_cast<unsigned char>(g_vid), static_cast<unsigned char>(g_vid >> 8),
                                    static_cast<unsigned char>(g_pid), static_cast<unsigned char>(g_pid >> 8),
                                    0x00, 0x01, 1, 2, 3, 1};
    std::memcpy(d, desc, sizeof desc);
}

// The descriptor behind a GET_DESCRIPTOR request (type in the high byte of wValue, index in the low).
// Returns its length, 0 for one the device does not have.
inline DWORD descriptor(unsigned type, unsigned index, unsigned char* d) {
    if (type == 1) { device_descriptor(d); return 18; }
    if (type == 2 && index == 0) {
        // One configuration with one vendor-specific interface and no endpoints, bus powered, 100 mA.
        const unsigned char conf[18] = {9, 2, 18, 0, 1, 1, 0, 0x80, 50,
                                        9, 4, 0, 0, 0, 0xff, 0xff, 0xff, 0};
        std::memcpy(d, conf, sizeof conf);
        return sizeof conf;
    }
    if (type == 3) {
        if (index == 0) {   // the language list: US English only
            const unsigned char langs[4] = {4, 3, 0x09, 0x04};
            std::memcpy(d, langs, sizeof langs);
            return sizeof langs;
        }
        const wchar_t* s = index == 1 ? g_manufacturer : index == 2 ? g_product : index == 3 ? g_serial : nullptr;
        if (!s) return 0;
        size_t n = wcslen(s);
        if (n > 126) n = 126;
        d[0] = static_cast<unsigned char>(2 + 2 * n);
        d[1] = 3;
        std::memcpy(d + 2, s, 2 * n);
        return d[0];
    }
    return 0;
}

inline BOOL answer(LPDWORD back, DWORD n) {
    if (back) *back = n;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

inline BOOL refuse(LPDWORD back, DWORD error) {
    if (back) *back = 0;
    SetLastError(error);
    return FALSE;
}

inline BOOL controller_ioctl(DWORD code, void* out, DWORD out_len, LPDWORD back) {
    if (code == kIoctlHcdDriverKeyName) return answer(back, put_name(out, out_len, kControllerDriverKey));
    if (code == kIoctlRootHubName) return answer(back, put_name(out, out_len, kRootHubName));
    return refuse(back, ERROR_INVALID_FUNCTION);
}

inline BOOL hub_ioctl(DWORD code, const void* in, DWORD in_len, void* out, DWORD out_len, LPDWORD back) {
    auto* o = static_cast<unsigned char*>(out);
    if (code == kIoctlNodeInformation) {
        // USB_NODE_INFORMATION (76 bytes): NodeType UsbHub, then the hub descriptor with one port.
        if (!o || out_len < 76) return refuse(back, ERROR_INSUFFICIENT_BUFFER);
        std::memset(o, 0, 76);
        o[4] = 9;      // bDescriptorLength
        o[5] = 0x29;   // bDescriptorType: hub
        o[6] = 1;      // bNumberOfPorts
        return answer(back, 76);
    }
    if (code == kIoctlConnectionInfoEx) {
        // USB_NODE_CONNECTION_INFORMATION_EX, packed: ConnectionIndex(0) DeviceDescriptor(4, 18 bytes)
        // CurrentConfigurationValue(22) Speed(23) DeviceIsHub(24) DeviceAddress(25) NumberOfOpenPipes(27)
        // ConnectionStatus(31), then the pipe list (none here).
        if (!o || !in || in_len < 4 || out_len < 35) return refuse(back, ERROR_INSUFFICIENT_BUFFER);
        ULONG port = 0;
        std::memcpy(&port, in, sizeof port);
        std::memset(o, 0, out_len);
        std::memcpy(o, &port, sizeof port);
        if (port == kDonglePort) {
            device_descriptor(o + 4);
            o[22] = 1;    // configured
            o[23] = 1;    // full speed
            o[25] = 1;    // device address
            o[31] = 1;    // DeviceConnected
        }
        return answer(back, 35);
    }
    if (code == kIoctlDescriptorFromConnection) {
        // USB_DESCRIPTOR_REQUEST: ConnectionIndex(0), setup packet bmRequest(4) bRequest(5) wValue(6)
        // wIndex(8) wLength(10), then the data (12).
        if (!o || !in || in_len < 12 || out_len < 12) return refuse(back, ERROR_INSUFFICIENT_BUFFER);
        const auto* r = static_cast<const unsigned char*>(in);
        ULONG port = 0;
        std::memcpy(&port, r, sizeof port);
        const unsigned value = r[6] | (r[7] << 8);
        const unsigned length = r[10] | (r[11] << 8);
        unsigned char d[256] = {};
        const DWORD n = port == kDonglePort ? descriptor(value >> 8, value & 0xff, d) : 0;
        if (!n) return refuse(back, ERROR_GEN_FAILURE);   // a STALL on the real bus
        DWORD give = n < length ? n : length;
        if (give > out_len - 12) give = out_len - 12;
        if (o != r) std::memmove(o, r, 12);
        std::memcpy(o + 12, d, give);
        return answer(back, 12 + give);
    }
    return refuse(back, ERROR_INVALID_FUNCTION);   // e.g. the driver key of a port: the game then skips that lookup
}

// --- SetupAPI ------------------------------------------------------------------------------------------
inline HDEVINFO WINAPI hook_get_class_devs(const GUID* cls, PCWSTR enumerator, HWND parent, DWORD flags) {
    const bool ours = g_fake && cls && IsEqualGUID(*cls, kHostControllerClass);
    if (g_log)
        g_log("dongle: SetupDiGetClassDevsW(%s, flags 0x%lx) caller 0x%08lx", ours ? "USB host controllers, answered" : "passed on",
              static_cast<unsigned long>(flags), caller_of(__builtin_return_address(0)));
    if (!ours) return g_get_class_devs ? g_get_class_devs(cls, enumerator, parent, flags) : INVALID_HANDLE_VALUE;
    // A real (empty) list, so SetupDiDestroyDeviceInfoList works on it; the interface in it is ours.
    g_list = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    return g_list;
}

inline BOOL WINAPI hook_enum_interfaces(HDEVINFO set, PSP_DEVINFO_DATA info, const GUID* cls, DWORD index,
                                        PSP_DEVICE_INTERFACE_DATA data) {
    if (!(g_fake && set == g_list && set != INVALID_HANDLE_VALUE))
        return g_enum_interfaces ? g_enum_interfaces(set, info, cls, index, data) : FALSE;
    if (index != 0 || !data) return refuse(nullptr, ERROR_NO_MORE_ITEMS);   // exactly one host controller
    data->cbSize = sizeof *data;
    data->InterfaceClassGuid = kHostControllerClass;
    data->Flags = SPINT_ACTIVE;
    data->Reserved = 0;
    return TRUE;
}

inline BOOL WINAPI hook_get_detail(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface, PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail,
                                   DWORD size, PDWORD needed, PSP_DEVINFO_DATA info) {
    if (!(g_fake && set == g_list && set != INVALID_HANDLE_VALUE))
        return g_get_detail ? g_get_detail(set, iface, detail, size, needed, info) : FALSE;
    const DWORD bytes = static_cast<DWORD>(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + sizeof kControllerPath);
    if (needed) *needed = bytes;
    if (!detail || size < bytes) return refuse(nullptr, ERROR_INSUFFICIENT_BUFFER);
    std::memcpy(detail->DevicePath, kControllerPath, sizeof kControllerPath);
    return TRUE;
}

// --- the controller and hub handles ----------------------------------------------------------------------
inline HANDLE open_null() {
    if (!g_create_file_w) return INVALID_HANDLE_VALUE;
    return g_create_file_w(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
}

inline HANDLE WINAPI hook_create_file_w(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD create,
                                        DWORD flags, HANDLE tmpl) {
    if (g_fake && name && (_wcsicmp(name, kControllerPath) == 0 || _wcsicmp(name, kRootHubPath) == 0)) {
        const bool controller = _wcsicmp(name, kControllerPath) == 0;
        HANDLE h = open_null();
        // The game closes the controller before it opens the hub, and Wine hands out the freed handle
        // value again: the role of a handle is the one of its latest open.
        (controller ? g_controller : g_hub) = h;
        if ((controller ? g_hub : g_controller) == h) (controller ? g_hub : g_controller) = INVALID_HANDLE_VALUE;
        if (g_log) g_log("dongle: the game opens the %s -> handle %p", controller ? "host controller" : "root hub", h);
        return h;
    }
    if (!g_create_file_w) return INVALID_HANDLE_VALUE;
    return g_create_file_w(name, access, share, sa, create, flags, tmpl);
}

inline BOOL WINAPI hook_device_io_control(HANDLE dev, DWORD code, LPVOID in, DWORD in_len, LPVOID out, DWORD out_len,
                                          LPDWORD back, LPOVERLAPPED ov) {
    const bool controller = g_fake && dev != INVALID_HANDLE_VALUE && dev == g_controller;
    const bool hub = g_fake && dev != INVALID_HANDLE_VALUE && dev == g_hub;
    if (!controller && !hub) {
        if (g_log)
            g_log("dongle: DeviceIoControl(code 0x%08lx, in %lu, out %lu) passed on, caller 0x%08lx",
                  static_cast<unsigned long>(code), static_cast<unsigned long>(in_len), static_cast<unsigned long>(out_len),
                  caller_of(__builtin_return_address(0)));
        return g_device_io_control ? g_device_io_control(dev, code, in, in_len, out, out_len, back, ov) : FALSE;
    }
    const BOOL ok = controller ? controller_ioctl(code, out, out_len, back) : hub_ioctl(code, in, in_len, out, out_len, back);
    if (g_log) {
        const unsigned port = in && in_len >= 4 ? *static_cast<const unsigned char*>(in) : 0;
        g_log("dongle: %s ioctl 0x%08lx (port %u, out %lu) -> %s, %lu bytes", controller ? "controller" : "hub",
              static_cast<unsigned long>(code), port, static_cast<unsigned long>(out_len), ok ? "ok" : "refused",
              back ? static_cast<unsigned long>(*back) : 0ul);
    }
    return ok;
}

// --- the PnP tree: the controller's own device node -----------------------------------------------------
// The game looks for the node whose driver key is the controller's (CM_DRP_DRIVER) and reads its
// description (CM_DRP_DEVICEDESC). The walk starts at the root, so the root it gets is our controller
// node; a walk from any other node goes to Wine.
inline CONFIGRET WINAPI hook_cm_locate(PDEVINST dev, DEVINSTID_W id, ULONG flags) {
    if (g_fake && dev && !id) {
        *dev = kControllerNode;
        return CR_SUCCESS;
    }
    return g_cm_locate ? g_cm_locate(dev, id, flags) : CR_FAILURE;
}

inline CONFIGRET WINAPI hook_cm_prop(DEVINST dev, ULONG prop, PULONG type, PVOID buf, PULONG len, ULONG flags) {
    if (dev != kControllerNode) return g_cm_prop ? g_cm_prop(dev, prop, type, buf, len, flags) : CR_FAILURE;
    const wchar_t* val = prop == CM_DRP_DRIVER ? kControllerDriverKey : prop == CM_DRP_DEVICEDESC ? kControllerDescription : nullptr;
    if (!val) return CR_NO_SUCH_VALUE;
    const ULONG bytes = static_cast<ULONG>((wcslen(val) + 1) * sizeof(wchar_t));
    if (type) *type = REG_SZ;
    if (!len) return CR_INVALID_POINTER;
    if (!buf || *len < bytes) { *len = bytes; return CR_BUFFER_SMALL; }
    std::memcpy(buf, val, bytes);
    *len = bytes;
    if (g_log && prop == CM_DRP_DRIVER) g_log("dongle: device tree: host controller node found");
    return CR_SUCCESS;
}

inline CONFIGRET WINAPI hook_cm_child(PDEVINST out, DEVINST dev, ULONG flags) {
    if (dev == kControllerNode) return CR_NO_SUCH_DEVNODE;
    return g_cm_child ? g_cm_child(out, dev, flags) : CR_FAILURE;
}

inline CONFIGRET WINAPI hook_cm_parent(PDEVINST out, DEVINST dev, ULONG flags) {
    if (dev == kControllerNode) return CR_NO_SUCH_DEVNODE;
    return g_cm_parent ? g_cm_parent(out, dev, flags) : CR_FAILURE;
}

inline CONFIGRET WINAPI hook_cm_sibling(PDEVINST out, DEVINST dev, ULONG flags) {
    if (dev == kControllerNode) return CR_NO_SUCH_DEVNODE;
    return g_cm_sibling ? g_cm_sibling(out, dev, flags) : CR_FAILURE;
}

}  // namespace rg_dongle
