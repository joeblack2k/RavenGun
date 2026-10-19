// SPDX-License-Identifier: GPL-3.0-or-later
// Mario Kart DX's USB dongle: the game looks for it through SetupAPI (SetupDiGetClassDevsW with
// class {3abf6f2d-71c4-462a-8a92-1e6861e6af27}) and then talks to it via CreateFile/DeviceIoControl.
// In the TeknoParrot comparison run there were NO device handles in the process, so the TP core
// answers this check inside the process itself.
//
// RG_DONGLE=trace only logs what the game asks for. RG_DONGLE=fake presents its own device: the
// class enumeration yields one interface, the detail call yields a path, and CreateFile on that path
// becomes a pipe whose other end we hold. The ioctl codes and buffers are logged; the first answer
// is a zero buffer of the requested length.
//
// Every log line carries the caller address (the game's return address into the hook) so we can find
// the exact check in the game's code.
#pragma once

// SetupDiCreateDeviceInfoW is marked deprecated in the current MinGW headers; we need it to put a
// device of our own into the list.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

namespace rg_dongle {

using setupdi_get_class_devs_w_fn = HDEVINFO(WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);
using setupdi_enum_device_interfaces_fn = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
using setupdi_get_device_interface_detail_w_fn = BOOL(WINAPI*)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD);
using create_file_w_fn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using device_io_control_fn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using read_file_fn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using exit_process_fn = void(WINAPI*)(UINT);
inline exit_process_fn g_exit_process = nullptr;
using crt_exit_fn = void(__cdecl*)(int);
inline crt_exit_fn g_crt_exit = nullptr;
using terminate_process_fn = BOOL(WINAPI*)(HANDLE, UINT);
inline terminate_process_fn g_terminate_process = nullptr;

inline setupdi_get_class_devs_w_fn g_get_class_devs = nullptr;
inline setupdi_enum_device_interfaces_fn g_enum_interfaces = nullptr;
inline setupdi_get_device_interface_detail_w_fn g_get_detail = nullptr;
inline create_file_w_fn g_create_file_w = nullptr;
inline device_io_control_fn g_device_io_control = nullptr;
inline read_file_fn g_read_file = nullptr;
inline void (*g_log)(const char*, ...) = nullptr;
inline bool g_fake = false;
inline bool g_chain = false;   // RG_DONGLE=chain: only log what the original answers
inline HANDLE g_dongle_handle = INVALID_HANDLE_VALUE;   // only this handle is the fake dongle

inline constexpr wchar_t kDonglePath[] = L"\\\\?\\rg-dongle#vid_0000&pid_0000#1";
inline constexpr wchar_t kDonglePipe[] = L"\\\\.\\pipe\\rg-dongle";
inline GUID g_dongle_class = {};   // the class the game asked for
inline bool g_dongle_class_set = false;

inline unsigned long caller_of(void* ra) {
    return static_cast<unsigned long>(reinterpret_cast<uintptr_t>(ra));
}

inline void guid_text(const GUID* g, char* out, size_t n) {
    if (!g) { std::snprintf(out, n, "(none)"); return; }
    std::snprintf(out, n, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                  static_cast<unsigned long>(g->Data1), g->Data2, g->Data3,
                  g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
                  g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

inline bool is_dongle_path(const wchar_t* p) {
    return p && _wcsicmp(p, kDonglePath) == 0;
}

// The class the game asked for, or a fixed one as a fallback.
inline const GUID* dongle_class() {
    static const GUID fixed = {0x3abf6f2d, 0x71c4, 0x462a, {0x8a, 0x92, 0x1e, 0x68, 0x61, 0xe6, 0xaf, 0x27}};
    return g_dongle_class_set ? &g_dongle_class : &fixed;
}

inline HDEVINFO WINAPI hook_get_class_devs(const GUID* cls, PCWSTR enumerator, HWND parent, DWORD flags) {
    char g[64];
    guid_text(cls, g, sizeof g);
    if (g_log)
        g_log("dongle: SetupDiGetClassDevsW %s (enumerator %ls, flags 0x%lx) caller 0x%08lx", g,
              enumerator ? enumerator : L"(none)", static_cast<unsigned long>(flags),
              caller_of(__builtin_return_address(0)));
    if (cls) { g_dongle_class = *cls; g_dongle_class_set = true; }
    if (g_fake) {
        // A real list with our own (imaginary) device in it, so the rest of SetupAPI keeps working.
        HDEVINFO list = SetupDiCreateDeviceInfoList(cls, nullptr);
        if (list == INVALID_HANDLE_VALUE) {
            if (g_log) g_log("dongle: SetupDiCreateDeviceInfoList failed (%lu)", static_cast<unsigned long>(GetLastError()));
            return list;
        }
        SP_DEVINFO_DATA info = {};
        info.cbSize = sizeof info;
        if (!SetupDiCreateDeviceInfoW(list, L"RGUSB\\VID_0000&PID_0000", cls, nullptr, nullptr, 0, &info)) {
            if (g_log) g_log("dongle: SetupDiCreateDeviceInfoW failed (%lu)", static_cast<unsigned long>(GetLastError()));
            return list;
        }
        SP_DEVICE_INTERFACE_DATA iface = {};
        iface.cbSize = sizeof iface;
        if (!SetupDiCreateDeviceInterfaceW(list, &info, cls, nullptr, 0, &iface)) {
            if (g_log) g_log("dongle: SetupDiCreateDeviceInterfaceW failed (%lu)", static_cast<unsigned long>(GetLastError()));
        } else if (g_log) {
            g_log("dongle: own device put into the list");
        }
        return list;
    }
    if (!g_get_class_devs) return INVALID_HANDLE_VALUE;
    return g_get_class_devs(cls, enumerator, parent, flags);
}

inline BOOL WINAPI hook_enum_device_interfaces(HDEVINFO set, PSP_DEVINFO_DATA info, const GUID* cls, DWORD index,
                                               PSP_DEVICE_INTERFACE_DATA data) {
    const unsigned long caller = caller_of(__builtin_return_address(0));
    if (g_fake) {
        if (index != 0 || !cls) return FALSE;   // exactly one dongle
        if (data) {
            data->cbSize = sizeof *data;
            data->InterfaceClassGuid = *cls;
            data->Flags = 0;
            data->Reserved = 0;
        }
        if (g_log) g_log("dongle: SetupDiEnumDeviceInterfaces index %lu -> own dongle, caller 0x%08lx",
                         static_cast<unsigned long>(index), caller);
        return TRUE;
    }
    if (g_log) g_log("dongle: SetupDiEnumDeviceInterfaces index %lu caller 0x%08lx",
                     static_cast<unsigned long>(index), caller);
    if (!g_enum_interfaces) return FALSE;
    return g_enum_interfaces(set, info, cls, index, data);
}

inline BOOL WINAPI hook_get_device_interface_detail_w(HDEVINFO set, PSP_DEVICE_INTERFACE_DATA iface,
                                                      PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail, DWORD size,
                                                      PDWORD needed) {
    const unsigned long caller = caller_of(__builtin_return_address(0));
    if (g_fake) {
        const DWORD bytes = static_cast<DWORD>(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + (wcslen(kDonglePath) + 1) * sizeof(wchar_t));
        if (needed) *needed = bytes;
        if (!detail || size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        detail->cbSize = sizeof *detail;
        wcscpy(detail->DevicePath, kDonglePath);
        if (g_log) g_log("dongle: device path %ls caller 0x%08lx", detail->DevicePath, caller);
        return TRUE;
    }
    if (!g_get_detail) return FALSE;
    const BOOL ok = g_get_detail(set, iface, detail, size, needed);
    if (g_log && ok) g_log("dongle: device path %ls caller 0x%08lx", detail ? detail->DevicePath : L"(none)", caller);
    return ok;
}

inline HANDLE WINAPI hook_create_file_w(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                        DWORD create, DWORD flags, HANDLE tmpl) {
    const unsigned long caller = caller_of(__builtin_return_address(0));
    if (g_fake && is_dongle_path(name)) {
        // The path of our device becomes a pipe: the game gets the client end, we keep the other.
        HANDLE pipe = CreateNamedPipeW(kDonglePipe, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                       PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (g_log) g_log("dongle: CreateNamedPipeW failed (%lu)", static_cast<unsigned long>(GetLastError()));
            return INVALID_HANDLE_VALUE;
        }
        HANDLE client = CreateFileW(kDonglePipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (client == INVALID_HANDLE_VALUE) {
            if (g_log) g_log("dongle: connecting to own pipe failed (%lu)", static_cast<unsigned long>(GetLastError()));
            CloseHandle(pipe);
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
        g_dongle_handle = client;
        if (g_log) g_log("dongle: the game opens the dongle -> own pipe, caller 0x%08lx", caller);
        return client;
    }
    if (!g_create_file_w) return INVALID_HANDLE_VALUE;
    return g_create_file_w(name, access, share, sa, create, flags, tmpl);
}

inline BOOL WINAPI hook_device_io_control(HANDLE dev, DWORD code, LPVOID in, DWORD in_len, LPVOID out, DWORD out_len,
                                          LPDWORD back, LPOVERLAPPED ov) {
    const unsigned long caller = caller_of(__builtin_return_address(0));
    if (g_fake && g_dongle_handle != INVALID_HANDLE_VALUE && dev == g_dongle_handle) {
        char hex[3 * 32 + 1] = {};
        const auto* b = static_cast<const unsigned char*>(in);
        if (b)
            for (DWORD i = 0; i < in_len && i < 32; ++i) std::snprintf(hex + i * 3, 4, "%02x ", b[i]);
        if (g_log)
            g_log("dongle: DeviceIoControl code 0x%08lx in %lu [%s] out %lu caller 0x%08lx",
                  static_cast<unsigned long>(code), static_cast<unsigned long>(in_len), hex,
                  static_cast<unsigned long>(out_len), caller);
        if (out && out_len) {
            std::memset(out, 0, out_len);
            // The game asks with a 6-byte buffer first and expects the first dword to be the size of
            // the dongle payload (> 6); it then asks again with that size. Answer that size here.
            if ((code == 0x00220424u || code == 0x00220408u) && out_len >= 6) {
                const unsigned long payload = 0x104;
                std::memcpy(out, &payload, sizeof payload);
                // Probe the payload layout: ASCII digits cycling, so whatever the game shows on its
                // boot check (board id / serial) tells us at which offset it reads.
                // The payload stays zero; only the size prefix is filled in.
            }
            if (back) *back = out_len;
            SetLastError(ERROR_SUCCESS);
            return TRUE;
        }
        if (back) *back = 0;
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    if (g_log)
        g_log("dongle: DeviceIoControl code 0x%08lx in %lu out %lu caller 0x%08lx",
              static_cast<unsigned long>(code), static_cast<unsigned long>(in_len),
              static_cast<unsigned long>(out_len), caller);
    if (!g_device_io_control) return FALSE;
    const BOOL ok = g_device_io_control(dev, code, in, in_len, out, out_len, back, ov);
    if (g_chain && g_log) {
        const DWORD n = back ? *back : 0;
        char hex[3 * 32 + 1] = {};
        const auto* b = static_cast<const unsigned char*>(out);
        if (ok && b)
            for (DWORD i = 0; i < n && i < 32; ++i) std::snprintf(hex + i * 3, 4, "%02x ", b[i]);
        g_log("dongle: chain code 0x%08lx -> %s, %lu bytes: %s", static_cast<unsigned long>(code), ok ? "ok" : "error",
              static_cast<unsigned long>(n), hex);
    }
    return ok;
}

inline BOOL WINAPI hook_terminate_process(HANDLE h, UINT code) {
    if (g_log)
        g_log("dongle: TerminateProcess(%p, %u) caller 0x%08lx", h, static_cast<unsigned>(code),
              caller_of(__builtin_return_address(0)));
    if (g_terminate_process) return g_terminate_process(h, code);
    return FALSE;
}

inline void __cdecl hook_crt_exit(int code) {
    if (g_log)
        g_log("dongle: exit(%d) caller 0x%08lx", code, caller_of(__builtin_return_address(0)));
    if (g_crt_exit) g_crt_exit(code);
}

inline void WINAPI hook_exit_process(UINT code) {
    if (g_log)
        g_log("dongle: ExitProcess(%u) caller 0x%08lx", static_cast<unsigned>(code),
              caller_of(__builtin_return_address(0)));
    if (g_exit_process) g_exit_process(code);
}

// --- inline hooks on ntdll, to catch a direct NtTerminateProcess / RtlExitUserProcess ----------------
using nt_terminate_fn = LONG(WINAPI*)(HANDLE, LONG);
inline unsigned char g_nt_saved[6] = {};
inline void* g_nt_target = nullptr;
inline unsigned char g_rl_saved[6] = {};
inline void* g_rl_target = nullptr;
using exit_thread_fn = void(WINAPI*)(DWORD);
inline exit_thread_fn g_exit_thread = nullptr;

inline bool install_inline(void* target, void* repl, unsigned char* saved) {
    DWORD prot = 0;
    if (!target || !VirtualProtect(target, 6, PAGE_EXECUTE_READWRITE, &prot)) return false;
    std::memcpy(saved, target, 6);
    unsigned char patch[6];
    patch[0] = 0x68;   // push imm32
    const auto addr = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(repl));
    std::memcpy(patch + 1, &addr, 4);
    patch[5] = 0xC3;   // ret
    std::memcpy(target, patch, 6);
    VirtualProtect(target, 6, prot, &prot);
    return true;
}

inline void restore_inline(void* target, unsigned char* saved) {
    DWORD prot = 0;
    if (!target || !VirtualProtect(target, 6, PAGE_EXECUTE_READWRITE, &prot)) return;
    std::memcpy(target, saved, 6);
    VirtualProtect(target, 6, prot, &prot);
}

inline LONG WINAPI hook_nt_terminate(HANDLE proc, LONG code) {
    if (g_log)
        g_log("dongle: NtTerminateProcess(%p, %ld) caller 0x%08lx", proc, static_cast<long>(code),
              caller_of(__builtin_return_address(0)));
    if (!g_nt_target) return 0;
    restore_inline(g_nt_target, g_nt_saved);
    return reinterpret_cast<nt_terminate_fn>(g_nt_target)(proc, code);
}

inline LONG WINAPI hook_rtl_exit_user(HANDLE proc, LONG code) {
    if (g_log)
        g_log("dongle: RtlExitUserProcess(%p, %ld) caller 0x%08lx", proc, static_cast<long>(code),
              caller_of(__builtin_return_address(0)));
    if (!g_rl_target) return 0;
    restore_inline(g_rl_target, g_rl_saved);
    return reinterpret_cast<nt_terminate_fn>(g_rl_target)(proc, code);
}

inline void WINAPI hook_exit_thread(DWORD code) {
    if (g_log) g_log("dongle: ExitThread(%lu) caller 0x%08lx", static_cast<unsigned long>(code),
                     caller_of(__builtin_return_address(0)));
    if (g_exit_thread) g_exit_thread(code);
}

// --- first-chance exceptions: log where the game faults --------------------------------------------
inline LONG CALLBACK hook_vectored(EXCEPTION_POINTERS* ep) {
    if (g_log && ep && ep->ExceptionRecord) {
        g_log("dongle: exception 0x%08lx at 0x%08lx", static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode),
              static_cast<unsigned long>(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress)));
        if (ep->ExceptionRecord->ExceptionCode == 0xC0000005u && ep->ContextRecord) {
            g_log("dongle:  eip 0x%08lx esp 0x%08lx ebp 0x%08lx esi 0x%08lx edi 0x%08lx",
                  static_cast<unsigned long>(ep->ContextRecord->Eip), static_cast<unsigned long>(ep->ContextRecord->Esp),
                  static_cast<unsigned long>(ep->ContextRecord->Ebp), static_cast<unsigned long>(ep->ContextRecord->Esi),
                  static_cast<unsigned long>(ep->ContextRecord->Edi));
            if (!IsBadReadPtr(reinterpret_cast<void*>(static_cast<uintptr_t>(ep->ContextRecord->Esp)), 8)) {
                const auto* sp = reinterpret_cast<const unsigned long*>(static_cast<uintptr_t>(ep->ContextRecord->Esp));
                g_log("dongle:  stack[0] 0x%08lx stack[1] 0x%08lx", sp[0], sp[1]);
            }
            // Walk the frame-pointer chain so the crash can be placed in the game's call tree.
            auto* frame = reinterpret_cast<unsigned long*>(static_cast<uintptr_t>(ep->ContextRecord->Ebp));
            for (int i = 0; i < 8 && frame; ++i) {
                const auto addr = reinterpret_cast<uintptr_t>(frame);
                if (addr < 0x10000 || (addr & 3)) break;
                if (IsBadReadPtr(frame, 8)) break;
                const unsigned long ret = frame[1];
                const unsigned long next = frame[0];
                g_log("dongle:  frame %d ret 0x%08lx", i, ret);
                if (next <= addr || next > addr + 0x20000) break;
                frame = reinterpret_cast<unsigned long*>(static_cast<uintptr_t>(next));
            }
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace rg_dongle
