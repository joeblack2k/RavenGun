// SPDX-License-Identifier: GPL-3.0-or-later
// rg-mfcam.dll: a Media Foundation camera for a Windows game under Wine, fed by the RavenGun daemon.
//
// WHY
//   Mario Kart Arcade GP DX takes a player photo with the cabinet's camera. The game finds that camera through Media Foundation:
//   MF.dll!MFEnumDeviceSources with MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE = VIDCAP, then MFCreateSourceReaderFromMediaSource
//   (MFReadWrite.dll) with an IMFSourceReaderCallback (measured: the import table of MK_AGP3_FINAL.exe). Wine (proton-11.0-2) has
//   no video cameras in Media Foundation: MFEnumDeviceSources returns `*count = 0` and S_OK for VIDCAP (measured: the code of
//   mf.dll, i386), and MFCreateDeviceSource is a stub. So the game never sees a camera, whatever is attached at /dev/video.
//
// WHAT
//   On load (the OpenParrot loader loads this DLL via TP_PRELOAD_DLL into the game process, after the core and before the game
//   continues; docs/dropins.md) this DLL redirects the import MFEnumDeviceSources in the game's import table to a version of its own. For VIDCAP
//   that version returns a camera ("RavenGun camera") if the daemon's camera file exists (src/ravengun/vision/
//   camera_feed_format.h, default Z:\dev\shm\ravengun_Camera), and otherwise calls the original function (then everything stays
//   as it was). The camera is an IMFMediaSource of our own with one stream: YUY2 in the dimensions of the file, and also NV12
//   and RGB32 (converted here), 30 frames per second, live. Each request for a frame (RequestSample) gets the newest frame from the
//   file. The daemon remains the only owner of the device.
//
// LOG
//   rg-mfcam.log next to this DLL (in the game's view): the loading, the redirection of the import table, every question from the game
//   (MFEnumDeviceSources, the format it chooses, start/stop) and the number of frames delivered. Without that log the DLL has not
//   been loaded.
//
// SECOND TASK: pHResult of d3dx10
//   The loader takes only one preload DLL per game, so this DLL also repairs something in Wine that Mario Kart trips over. The game
//   loads its effects with D3DX10CreateEffectFromFileA(..., pump = NULL, &effect, &errors, &hr) and then looks ONLY at `hr`
//   (0x838AE9 in MK_AGP3_FINAL.exe: `hr < 0` gives NULL, and 0x76C8C0 then reads through that NULL without a check). Windows sets `*pHResult`
//   even without a pump; Wine (proton-11.0-2) leaves it untouched (measured with a test program: the value stayed 0xDEADBEEF). Then
//   whatever happened to be on the stack decides, and the game crashed after ~3 minutes of attract at "AttractGameDemo Unload" (TireTrace_Nml.cfx).
//   The redirection here: the four d3dx10 functions with a pHResult call the original and then set `*pHResult` to the result
//   if there is no pump (which is what Windows does too).
//
// THIRD TASK: JVS over COM3 to the pipe (without the closed TeknoParrot core)
//   Mario Kart opens its JVS board as `COM3` (CreateFileA at 0x8ac1f2, then SetCommState/SetCommTimeouts/SetupComm/PurgeComm and
//   GetCommModemStatus as the sense line). Under Wine there is no COM3; our own bridge (third_party/rg-jvsbridge) creates \\.\pipe\TeknoParrot_JVS and
//   the daemon is the board behind it. The closed core used to redirect COM3 to that pipe; this DLL does that now: CreateFileA on the
//   configured port returns a handle on the pipe, and the comm calls on that handle succeed without doing anything. GetCommModemStatus returns
//   the sense line (0x10, after the address assignment 0x30) from byte 0 of the shared memory TeknoParrot_JvsState. Only on if
//   RG_JVS_COMPORT is set (dropin.ini, Env). The decision logic is in jvs_com.h and is tested (tests/test_jvs_com.cpp).
//
// FOURTH TASK: an own ShareK object for CoCreateInstance (without the closed TeknoParrot core)
//   At startup Mario Kart starts the ShareK client (ShareKManager.cpp, content distribution between cabinets): CoCreateInstance of the
//   .NET COM class {ccc44b2a-709a-33a8-a1b0-5dbe25b19c2c} (ipccl.dll, registered with RegAsm on the real cabinet) at 0x47e834. That
//   registration does not exist in our prefix: 0x80040154, a _com_error, and the catch handler at 0x47e8f8 then calls a method anyway
//   on the empty interface (access violation at 0x47e95e after ~15 s). With RG_SHAREK_FAKE=1 this DLL redirects ole32!CoCreateInstance in the
//   game's import table; for that one CLSID an own in-process object comes back that says "ShareK is running but waiting for the
//   network" (status 2: the game's tick then does nothing). Every other class, including the IAuth of AMAuthd, goes unchanged to the real
//   CoCreateInstance. With the TeknoParrot core in the process this stays off (it answers that itself). Logic and explanation: sharek_fake.h.
//
// FIFTH TASK: the stalldump (where is the game waiting?)
//   With RG_STALL_DUMP=30,60,90 (seconds after this DLL is loaded) a thread of our own briefly suspends every other thread of the process at those times,
//   reads EIP/ESP/EBP and searches the stack for return addresses in the exe (only values preceded by an x86 call,
//   stall_dump.h). Everything goes to rg-mfcam.log, addresses as exe+RVA and as the VA that objdump shows for the exe. For a black screen where the
//   game logs nothing anymore; without gdb, and without the game noticing (the threads simply keep running after the dump).
//
// SIXTH TASK: drive letter redirection for what patch 0004 of the core misses
//   Our own core redirects `F:/x` to `.\TeknoParrot\F\x`, but only for CreateFile and CreateDirectory. Mario Kart renames its
//   `F:/system/<name>.bin.tmp` with the CRT (`rename`) to `<name>.bin`, searches with FindFirstFileA/W and checks with GetFileAttributesW and
//   `_waccess_s`: those do not go through it, so every start begins with empty settings (measured: only *.bin.tmp in the view). With
//   RG_DRIVE_FIXUP=F this DLL redirects those entries in the game's import table to the same rule (drive_fix.h). Only the letters named;
//   G: is left alone (the game then turns ShareK off, which is wanted). Off with the TeknoParrot core in the process (it does it its own way).
//
// SEVENTH TASK: follow or emulate the Banapass card reader (bngrw.dll)
//   The game imports bngrw.dll from the game folder (the real library, which opens COM4/COM2) and drives two readers in the startup checks,
//   just before the IO check that opens COM3. The closed TeknoParrot core replaces those functions; our core does not. With RG_BNGRW=trace this DLL redirects
//   the eleven imports of bngrw.dll to logger wrappers around the real functions (arguments, result and every callback from the library); with
//   RG_BNGRW=fake it answers itself: two readers without a card (bngrw_fake.h). Off with the TeknoParrot core.
//
// EIGHTH TASK: follow the game's message loop and windows, and patch the F:/ strings like TeknoParrot
//   RG_MSGTRACE=1 redirects the game's PeekMessageW, DispatchMessageW, ShowWindow, SetWindowPos and CreateWindowExW to logger wrappers: the first five
//   PeekMessageW calls (entered and returned, with the duration), the windows of the thread at the first call (visible, foreground, dimensions), the first 40
//   messages, slow calls (>= 200 ms) and a summary of the loop every 5 s. A PeekMessageW that never returns shows up as "entered" without "returned".
//   The TP core patches the game's own strings (`F:/playdata/` becomes `./Fplaydata/`, same length; the game logs that itself); with RG_DRIVE_FIXUP we
//   now do that too (drive_fix.h, patch_strings), for the letters named, in the non-executable sections of the exe. RG_DRIVE_STRINGS=0 leaves that out.
//
// NINTH TASK: window mode like TeknoParrot (Windowed=1)
//   The game is DXUT (D3D10) and sets full screen as the default itself: `mov [ebx+0x30],1` at 0x4057AD (constructor 0x405740 of the
//   settings object), which gives "-fullscreen" for DXUT (0x770656) and DXUTCreateDevice(bWindowed = 0) (0x846808). Under our core DXVK then does an
//   exclusive mode switch ("Setting display mode: 1368x768@60", twice, all test runs of 2026-10-18 12:03-12:07) and the first PeekMessageW
//   (0x83D7D4, DXUTMainLoop) no longer returns: black screen. Under TeknoParrot (profile Windowed=1) that line is not there and the swapchain presents
//   1360x768 in a window (run 10:46). With RG_WINDOWED=1 this DLL sets that imm32 to 0 (pattern in window_mode.h, exactly one hit required), so that the
//   game picks "-windowed" itself. Off with the TeknoParrot core. Log: "window mode on: ...".
//
// TENTH TASK: emulate the STR board (steering and force feedback) on COM1 (without the closed TeknoParrot core)
//   After the window the game stops at E22-12 "STR PCB 通信エラー" (tv 2026-10-18 12:35). The STR board is not on JVS but on \\.\COM1, 19200 8N1
//   (overlapped, ReadFileEx/WriteFileEx); it reports its status in three bytes ("C01" connected, "E00" no error) and the game gives E22-12 if after 4800
//   ticks no "C01" came (0x602E39). With RG_STR_COMPORT=COM1 CreateFileA for that port returns the client end of a pipe of our own; a thread in this DLL
//   is the board (str_board.h): status "C01", "E00", then "C01", and a "C01" per command of 10 bytes. The comm calls succeed on that handle (as with
//   JVS). Off with the TeknoParrot core. Log: "STR board: ...".
//
// ELEVENTH TASK: the content router answers its ping (without the closed TeknoParrot core)
//   A real cabinet sits behind a store content router at 192.168.112.254. The game pings it with IcmpSendEcho at boot (0x71C8A0, error
//   "E05-01" without a reply) and again from its local network monitor (0x71BB70, "E05-05 ローカルネットワークエラー" after ten failed rounds,
//   ~2 min after the start). With RG_CONTENT_ROUTER=192.168.112.254 this DLL redirects IPHLPAPI!IcmpSendEcho in the game's import table
//   and answers echo requests to that one address like a router on the same switch; every other address goes to the real function
//   (content_router.h). Off with the TeknoParrot core. Log: "content router: ...".
//
// TWELFTH TASK: a camera without the daemon, in a module other than the exe (Lost Land Adventure, 64-bit)
//   Lost Land Adventure's launcher (RSLauncher.exe, 64-bit) asks nbamMfCapture.dll's mfCapDevList::EnumerateDevices for its cameras
//   (0x140041450: up to 25 times, 200 ms apart) and raises error 40 "08-01 AUTH CAMERA ERROR" when the count stays 0; that error
//   sets bErrorFatal in the memory it shares with the game, and the game stays on its FATAL ERROR screen. nbamMfCapture.dll, not the
//   exe, imports MF.dll!MFEnumDeviceSources. RG_MFCAM_MODULE=nbamMfCapture.dll puts the redirection in that module's import table;
//   RG_MFCAM_SYNTHETIC=640x480 offers the camera also without the daemon's camera file, with the gray frame below (cam_select.h).
//   This file builds both as the 32-bit DLL (Mario Kart DX) and as a 64-bit one (tools/dev/build-mfcam.sh --arch x86_64); the
//   stalldump reads 32-bit thread registers and is only in the 32-bit build.
//
// THIRTEENTH TASK: Lost Land's gun peripheral on COM2, in the game started by the launcher
//   RSLauncher.exe starts LLA/Binaries/Win64/DomeShooterGame-Win64-Shipping.exe with CreateProcessW. RG_CHILD_PRELOAD=<exe name> loads
//   this DLL into that child too (suspended start, remote LoadLibraryW). In the game RG_DOMEGUN_COMPORT=COM2 answers its gun peripheral:
//   the game reads 12-byte frames (picture spot of each gun's camera and the button bits) from COM2 at 115200 and forgets the gun after
//   60 polls without a frame; the launcher then shows a gun camera error (RSSharedData+0x1a4, RSLauncher 0x1400633ea). The frames come
//   from the shared memory TeknoParrot_JvsState (dome_gun.h). RG_DOMEGUN_MASKS=<hex rva> logs the game's button masks.
//
// ENVIRONMENT
//   RG_CHILD_PRELOAD=name   load this DLL into a child process with that exe name (THIRTEENTH TASK)
//   RG_DOMEGUN_COMPORT=COM2 emulate the gun peripheral on that port (THIRTEENTH TASK)
//   RG_MFCAM_SYNTHETIC=WxH  a camera of that size also without the camera file (TWELFTH TASK)
//   RG_MFCAM_MODULE=name    redirect MFEnumDeviceSources in that module instead of the exe (TWELFTH TASK)
//   RG_CONTENT_ROUTER=a.b.c.d  answer the game's pings to its content router at that address (ELEVENTH TASK)
//   RG_STR_COMPORT=COM1  emulate the STR board (TENTH TASK)
//   RG_WINDOWED=1        let the game start windowed (NINTH TASK)
//   RG_MSGTRACE=1       follow the message loop and windows
//   RG_PEEK_DELAY_MS=N   (experiment) delay the game's first PeekMessageW by N ms, e.g. 25000 (TP takes ~25 s for its first loop)
//   RG_PEEK_SKIP_FIRST=1 (experiment) do not execute the game's first PeekMessageW(PM_NOREMOVE) (0x83D7D4, only creates the message queue)
//   RG_DRIVE_STRINGS=0   with RG_DRIVE_FIXUP, do not patch the strings (only the API redirection)
//   RG_BNGRW=trace|fake  follow or emulate the card reader; empty = off
//   RG_DRIVE_FIXUP=F  the drive letters (without C and Z) for which the additional redirection is on; empty = off
//   RG_STALL_DUMP=30,60  the stalldump on (seconds); empty = off
//   RG_IMAGE_DUMP=40,90  write the loaded exe image to rg-image-<secs>s.bin (seconds); empty = off
//   RG_SHAREK_FAKE=1  the in-process ShareK object (dropin.ini, Env); empty or 0 = off
//   RG_AMAUTH_FAKE=1  the in-process AMAuthd IAuth object (dropin.ini, Env); empty or 0 = off
//   RG_AMAUTH_STATE=3..8  the auth state our IAuth reports (default 8: offline cabinet)
//   RG_DONGLE=fake|trace  answer (or only log) the USB tree walk for the dongle (usb_dongle.h); empty = off
//   RG_DONGLE_USB=vvvv:pppp, RG_DONGLE_SERIAL=...  the dongle the fake presents
//   RG_MFCAM_SOURCE  Windows path of the camera file (default Z:\dev\shm\ravengun_Camera)
//   RG_MFCAM_FPS     frames per second to the game (default 30, 1..60)
//   RG_MFCAM_OFF=1   do nothing (only a line in the log)
//   RG_D3DX10_HRFIX=0  do not turn on the pHResult repair
//   RG_JVS_COMPORT    the COM port the game opens for JVS (e.g. COM3); empty = JVS redirection off
//   RG_JVS_PIPE       the bridge's pipe (default \\.\pipe\TeknoParrot_JVS)
//
// Clean-room: only the public Media Foundation interfaces and what we measured ourselves on the game and on Wine.
#define WIN32_LEAN_AND_MEAN
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <initguid.h>
#include <cguid.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <cwctype>
#include <vector>

#include "../../src/ravengun/vision/camera_feed_format.h"
#include "jvs_com.h"
#include "sharek_fake.h"
#include "am_auth_fake.h"
#include "stall_dump.h"
#include "drive_fix.h"
#include "bngrw_fake.h"
#include "cam_select.h"
#include "dome_gun.h"
#include "window_mode.h"
#include "str_board.h"
#include "usb_dongle.h"
#include "content_router.h"

// --- GUIDs that are not in every MinGW import library (our own names, same values as in mfidl.h / mfapi.h) -------------
DEFINE_GUID(RG_DEVSOURCE_SOURCE_TYPE, 0xc60ac5fe, 0x252a, 0x478f, 0xa0, 0xef, 0xbc, 0x8f, 0xa5, 0xf7, 0xca, 0xd3);
DEFINE_GUID(RG_DEVSOURCE_VIDCAP_GUID, 0x8ac3587a, 0x4ae7, 0x42d8, 0x99, 0xe0, 0x0a, 0x60, 0x13, 0xee, 0xf9, 0x0f);
DEFINE_GUID(RG_DEVSOURCE_FRIENDLY_NAME, 0x60d0e559, 0x52f8, 0x4fa2, 0xbb, 0xce, 0xac, 0xdb, 0x34, 0xa8, 0xec, 0x01);
DEFINE_GUID(RG_DEVSOURCE_VIDCAP_SYMLINK, 0x58f0aad8, 0x22bf, 0x4f8a, 0xbb, 0x3d, 0xd2, 0xc4, 0x97, 0x8c, 0x6e, 0x2f);
DEFINE_GUID(RG_SAMPLE_TOKEN, 0x8294da66, 0xf328, 0x4805, 0xb5, 0x51, 0x00, 0xde, 0xb4, 0xc5, 0x7a, 0x61);
DEFINE_GUID(RG_MT_VIDEO, 0x73646976, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(RG_FMT_YUY2, 0x32595559, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(RG_FMT_NV12, 0x3231564e, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(RG_FMT_RGB32, 0x00000016, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

namespace {

// --- log -------------------------------------------------------------------------------------------------------------------------
CRITICAL_SECTION g_log_cs;
wchar_t g_log_path[MAX_PATH];
HMODULE g_self = nullptr;

void rg_log(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = std::snprintf(line, sizeof line, "[%10lu] ", static_cast<unsigned long>(GetTickCount()));
    if (n < 0) n = 0;
    int m = std::vsnprintf(line + n, sizeof line - static_cast<size_t>(n) - 2, fmt, ap);
    va_end(ap);
    size_t len = static_cast<size_t>(n) + (m < 0 ? 0 : std::min<size_t>(static_cast<size_t>(m), sizeof line - static_cast<size_t>(n) - 3));
    line[len++] = '\r';
    line[len++] = '\n';
    EnterCriticalSection(&g_log_cs);
    HANDLE h = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, line, static_cast<DWORD>(len), &w, nullptr);
        CloseHandle(h);
    }
    LeaveCriticalSection(&g_log_cs);
}

const char* fmt_name(const GUID& g) {
    if (g == RG_FMT_YUY2) return "YUY2";
    if (g == RG_FMT_NV12) return "NV12";
    if (g == RG_FMT_RGB32) return "RGB32";
    return "?";
}

// --- settings ---------------------------------------------------------------------------------------------------------------------
wchar_t g_source[MAX_PATH] = RG_CAMFEED_PATH_WINE;
int g_fps = 30;
uint32_t g_synth_w = 0, g_synth_h = 0;   // RG_MFCAM_SYNTHETIC; 0 = off

// --- reading the camera file ----------------------------------------------------------------------------------------------------------
// Reads header and frame with the seqlock from camera_feed_format.h. true = `head` and `frame` are one whole frame.
bool read_feed(rg_camfeed_header& head, std::vector<BYTE>& frame) {
    HANDLE h = CreateFileW(g_source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    for (int attempt = 0; attempt < 4 && !ok; ++attempt) {
        DWORD got = 0;
        rg_camfeed_header hd;
        SetFilePointer(h, 0, nullptr, FILE_BEGIN);
        if (!ReadFile(h, &hd, sizeof hd, &got, nullptr) || got != sizeof hd) break;
        if (hd.seq & 1u) { Sleep(1); continue; }
        const uint32_t n = rg_camfeed_frame_bytes(&hd);
        if (!n || hd.frames == 0) break;
        frame.resize(n);
        if (!ReadFile(h, frame.data(), n, &got, nullptr) || got != n) break;
        uint32_t seq2 = 0;
        SetFilePointer(h, 8, nullptr, FILE_BEGIN);
        if (!ReadFile(h, &seq2, 4, &got, nullptr) || got != 4) break;
        if (seq2 != hd.seq) continue;
        head = hd;
        ok = true;
    }
    CloseHandle(h);
    return ok;
}

// Only the header (for MFEnumDeviceSources): is there a camera, and what dimensions.
bool feed_present(uint32_t& w, uint32_t& h) {
    rg_camfeed_header hd;
    std::vector<BYTE> f;
    if (!read_feed(hd, f)) {
        if (!g_synth_w) return false;
        w = g_synth_w;   // no camera file: the synthetic camera (the stream delivers gray frames)
        h = g_synth_h;
        return true;
    }
    w = hd.width;
    h = hd.height;
    return true;
}

// --- conversion -------------------------------------------------------------------------------------------------------------------------
inline BYTE clamp8(int v) { return static_cast<BYTE>(v < 0 ? 0 : v > 255 ? 255 : v); }

void yuy2_to_nv12(const BYTE* src, uint32_t stride, uint32_t w, uint32_t h, BYTE* dst) {
    BYTE* y = dst;
    BYTE* uv = dst + static_cast<size_t>(w) * h;
    for (uint32_t r = 0; r < h; ++r) {
        const BYTE* s = src + static_cast<size_t>(r) * stride;
        for (uint32_t c = 0; c < w; c += 2) {
            y[static_cast<size_t>(r) * w + c] = s[c * 2];
            y[static_cast<size_t>(r) * w + c + 1] = s[c * 2 + 2];
            if ((r & 1u) == 0) {
                BYTE* o = uv + static_cast<size_t>(r / 2) * w + c;
                o[0] = s[c * 2 + 1];
                o[1] = s[c * 2 + 3];
            }
        }
    }
}

void yuy2_to_rgb32(const BYTE* src, uint32_t stride, uint32_t w, uint32_t h, BYTE* dst) {
    // BT.601, limited range; RGB32 in Media Foundation is top to bottom with a positive stride (B, G, R, X).
    for (uint32_t r = 0; r < h; ++r) {
        const BYTE* s = src + static_cast<size_t>(r) * stride;
        BYTE* d = dst + static_cast<size_t>(r) * w * 4;
        for (uint32_t c = 0; c < w; c += 2) {
            const int y0 = s[c * 2] - 16, u = s[c * 2 + 1] - 128, y1 = s[c * 2 + 2] - 16, v = s[c * 2 + 3] - 128;
            for (int k = 0; k < 2; ++k) {
                const int yy = 298 * (k ? y1 : y0);
                BYTE* p = d + (c + static_cast<uint32_t>(k)) * 4;
                p[0] = clamp8((yy + 516 * u + 128) >> 8);
                p[1] = clamp8((yy - 100 * u - 208 * v + 128) >> 8);
                p[2] = clamp8((yy + 409 * v + 128) >> 8);
                p[3] = 0xFF;
            }
        }
    }
}

uint32_t out_bytes(const GUID& fmt, uint32_t w, uint32_t h) {
    if (fmt == RG_FMT_NV12) return w * h * 3 / 2;
    if (fmt == RG_FMT_RGB32) return w * h * 4;
    return w * h * 2;
}

HRESULT make_type(const GUID& fmt, uint32_t w, uint32_t h, IMFMediaType** out) {
    IMFMediaType* t = nullptr;
    HRESULT hr = MFCreateMediaType(&t);
    if (FAILED(hr)) return hr;
    t->SetGUID(MF_MT_MAJOR_TYPE, RG_MT_VIDEO);
    t->SetGUID(MF_MT_SUBTYPE, fmt);
    t->SetUINT64(MF_MT_FRAME_SIZE, (static_cast<UINT64>(w) << 32) | h);
    t->SetUINT64(MF_MT_FRAME_RATE, (static_cast<UINT64>(g_fps) << 32) | 1u);
    t->SetUINT64(MF_MT_PIXEL_ASPECT_RATIO, (1ull << 32) | 1u);
    t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    t->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    t->SetUINT32(MF_MT_SAMPLE_SIZE, out_bytes(fmt, w, h));
    t->SetUINT32(MF_MT_DEFAULT_STRIDE, fmt == RG_FMT_NV12 ? w : fmt == RG_FMT_RGB32 ? w * 4 : w * 2);
    *out = t;
    return S_OK;
}

LONGLONG now_100ns() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<LONGLONG>(static_cast<double>(c.QuadPart) * 1e7 / static_cast<double>(freq.QuadPart));
}

class CamSource;

// --- the stream ------------------------------------------------------------------------------------------------------------------------
class CamStream final : public IMFMediaStream {
public:
    CamStream(CamSource* src, IMFStreamDescriptor* sd) : src_(src), sd_(sd) {
        sd_->AddRef();
        InitializeCriticalSection(&cs_);
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        MFCreateEventQueue(&queue_);
    }
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator || riid == IID_IMFMediaStream) {
            *ppv = static_cast<IMFMediaStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    // IMFMediaEventGenerator
    STDMETHODIMP GetEvent(DWORD f, IMFMediaEvent** e) override { return queue_ ? queue_->GetEvent(f, e) : MF_E_SHUTDOWN; }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* st) override { return queue_ ? queue_->BeginGetEvent(cb, st) : MF_E_SHUTDOWN; }
    STDMETHODIMP EndGetEvent(IMFAsyncResult* r, IMFMediaEvent** e) override { return queue_ ? queue_->EndGetEvent(r, e) : MF_E_SHUTDOWN; }
    STDMETHODIMP QueueEvent(MediaEventType t, REFGUID g, HRESULT hr, const PROPVARIANT* v) override {
        return queue_ ? queue_->QueueEventParamVar(t, g, hr, v) : MF_E_SHUTDOWN;
    }
    // IMFMediaStream
    STDMETHODIMP GetMediaSource(IMFMediaSource** out) override;
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** out) override {
        if (!out) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        *out = sd_;
        sd_->AddRef();
        return S_OK;
    }
    STDMETHODIMP RequestSample(IUnknown* token) override {
        EnterCriticalSection(&cs_);
        HRESULT hr = S_OK;
        if (shutdown_) hr = MF_E_SHUTDOWN;
        else if (!started_) hr = MF_E_MEDIA_SOURCE_WRONGSTATE;
        else {
            if (token) token->AddRef();
            tokens_.push_back(token);
        }
        LeaveCriticalSection(&cs_);
        if (SUCCEEDED(hr)) SetEvent(wake_);
        return hr;
    }

    // For the source.
    void start(const GUID& fmt, uint32_t w, uint32_t h, LONGLONG t0) {
        EnterCriticalSection(&cs_);
        fmt_ = fmt; w_ = w; h_ = h; t0_ = t0;
        started_ = true;
        if (!worker_) worker_ = CreateThread(nullptr, 0, &CamStream::worker_main, this, 0, nullptr);
        LeaveCriticalSection(&cs_);
        PROPVARIANT v;
        PropVariantInit(&v);
        v.vt = VT_I8;
        v.hVal.QuadPart = 0;
        QueueEvent(MEStreamStarted, GUID_NULL, S_OK, &v);
    }
    void stop() {
        EnterCriticalSection(&cs_);
        started_ = false;
        for (IUnknown* t : tokens_) if (t) t->Release();
        tokens_.clear();
        LeaveCriticalSection(&cs_);
        QueueEvent(MEStreamStopped, GUID_NULL, S_OK, nullptr);
    }
    void shutdown() {
        EnterCriticalSection(&cs_);
        shutdown_ = true;
        started_ = false;
        for (IUnknown* t : tokens_) if (t) t->Release();
        tokens_.clear();
        HANDLE w = worker_;
        worker_ = nullptr;
        LeaveCriticalSection(&cs_);
        SetEvent(wake_);
        if (w) { WaitForSingleObject(w, 2000); CloseHandle(w); }
        if (queue_) { queue_->Shutdown(); queue_->Release(); queue_ = nullptr; }
        rg_log("stream closed: %lu frames delivered, %lu times without a new frame", static_cast<unsigned long>(delivered_),
               static_cast<unsigned long>(stale_));
    }
    void detach_source() { src_ = nullptr; }

private:
    ~CamStream() {
        if (sd_) sd_->Release();
        if (queue_) { queue_->Shutdown(); queue_->Release(); }
        if (wake_) CloseHandle(wake_);
        DeleteCriticalSection(&cs_);
    }
    static DWORD WINAPI worker_main(void* p) {
        static_cast<CamStream*>(p)->worker();
        return 0;
    }
    void worker() {
        const DWORD period = static_cast<DWORD>(1000 / (g_fps > 0 ? g_fps : 30));
        DWORD next = GetTickCount();
        std::vector<BYTE> raw;
        rg_camfeed_header head{};
        bool have = false;
        for (;;) {
            WaitForSingleObject(wake_, period);
            EnterCriticalSection(&cs_);
            const bool stop = shutdown_;
            const bool go = started_ && !tokens_.empty();
            LeaveCriticalSection(&cs_);
            if (stop) break;
            if (!go) continue;
            const DWORD t = GetTickCount();
            if (static_cast<LONG>(t - next) < 0) { Sleep(next - t); }
            next = GetTickCount() + period;
            rg_camfeed_header hd;
            if (read_feed(hd, raw)) { head = hd; have = true; }
            else ++stale_;
            deliver(have ? &head : nullptr, raw);
        }
    }
    void deliver(const rg_camfeed_header* head, const std::vector<BYTE>& raw) {
        EnterCriticalSection(&cs_);
        if (!started_ || tokens_.empty()) { LeaveCriticalSection(&cs_); return; }
        IUnknown* token = tokens_.front();
        tokens_.pop_front();
        const GUID fmt = fmt_;
        const uint32_t w = w_, h = h_;
        const LONGLONG t0 = t0_;
        LeaveCriticalSection(&cs_);
        IMFSample* s = nullptr;
        IMFMediaBuffer* b = nullptr;
        const DWORD n = out_bytes(fmt, w, h);
        HRESULT hr = MFCreateMemoryBuffer(n, &b);
        if (SUCCEEDED(hr)) {
            BYTE* p = nullptr;
            DWORD maxl = 0;
            hr = b->Lock(&p, &maxl, nullptr);
            if (SUCCEEDED(hr)) {
                if (head && head->width == w && head->height == h && !raw.empty()) {
                    if (fmt == RG_FMT_NV12) yuy2_to_nv12(raw.data(), head->stride, w, h, p);
                    else if (fmt == RG_FMT_RGB32) yuy2_to_rgb32(raw.data(), head->stride, w, h, p);
                    else for (uint32_t r = 0; r < h; ++r) std::memcpy(p + static_cast<size_t>(r) * w * 2, raw.data() + static_cast<size_t>(r) * head->stride, w * 2);
                } else {
                    // No frame (or a different shape than agreed): gray, so that the game gets a valid frame.
                    if (fmt == RG_FMT_NV12) std::memset(p, 0x80, n);
                    else if (fmt == RG_FMT_RGB32) std::memset(p, 0x80, n);
                    else for (DWORD i = 0; i + 1 < n; i += 2) { p[i] = 0x80; p[i + 1] = 0x80; }
                }
                b->Unlock();
                b->SetCurrentLength(n);
            }
        }
        if (SUCCEEDED(hr)) hr = MFCreateSample(&s);
        if (SUCCEEDED(hr)) hr = s->AddBuffer(b);
        if (SUCCEEDED(hr)) {
            const LONGLONG ts = now_100ns() - t0;
            s->SetSampleTime(ts < 0 ? 0 : ts);
            s->SetSampleDuration(10000000LL / (g_fps > 0 ? g_fps : 30));
            if (token) s->SetUnknown(RG_SAMPLE_TOKEN, token);
            hr = queue_ ? queue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, s) : MF_E_SHUTDOWN;
        }
        if (SUCCEEDED(hr)) {
            ++delivered_;
            if (delivered_ == 1 || delivered_ % 900 == 0)
                rg_log("frame %lu delivered (%s %ux%u%s)", static_cast<unsigned long>(delivered_), fmt_name(fmt), w, h, head ? "" : ", gray: no frame from the daemon yet");
        } else {
            rg_log("ERROR: delivering frame failed (hr=0x%08lx)", static_cast<unsigned long>(hr));
        }
        if (s) s->Release();
        if (b) b->Release();
        if (token) token->Release();
    }

    LONG ref_ = 1;
    CamSource* src_;
    IMFStreamDescriptor* sd_;
    IMFMediaEventQueue* queue_ = nullptr;
    CRITICAL_SECTION cs_;
    HANDLE wake_ = nullptr, worker_ = nullptr;
    std::deque<IUnknown*> tokens_;
    bool started_ = false, shutdown_ = false;
    GUID fmt_ = RG_FMT_YUY2;
    uint32_t w_ = 0, h_ = 0;
    LONGLONG t0_ = 0;
    unsigned long delivered_ = 0, stale_ = 0;
};

// --- the source --------------------------------------------------------------------------------------------------------------------------
class CamSource final : public IMFMediaSource {
public:
    CamSource(uint32_t w, uint32_t h) : w_(w), h_(h) { InitializeCriticalSection(&cs_); }
    HRESULT init() {
        HRESULT hr = MFCreateEventQueue(&queue_);
        IMFMediaType* types[3] = {};
        const GUID fmts[3] = {RG_FMT_YUY2, RG_FMT_NV12, RG_FMT_RGB32};
        for (int i = 0; i < 3 && SUCCEEDED(hr); ++i) hr = make_type(fmts[i], w_, h_, &types[i]);
        IMFStreamDescriptor* sd = nullptr;
        if (SUCCEEDED(hr)) hr = MFCreateStreamDescriptor(0, 3, types, &sd);
        IMFMediaTypeHandler* th = nullptr;
        if (SUCCEEDED(hr)) hr = sd->GetMediaTypeHandler(&th);
        if (SUCCEEDED(hr)) hr = th->SetCurrentMediaType(types[0]);
        if (SUCCEEDED(hr)) hr = MFCreatePresentationDescriptor(1, &sd, &pd_);
        if (SUCCEEDED(hr)) hr = pd_->SelectStream(0);
        if (SUCCEEDED(hr)) stream_ = new CamStream(this, sd);
        if (th) th->Release();
        if (sd) sd->Release();
        for (auto* t : types) if (t) t->Release();
        return hr;
    }
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator || riid == IID_IMFMediaSource) {
            *ppv = static_cast<IMFMediaSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    // IMFMediaEventGenerator
    STDMETHODIMP GetEvent(DWORD f, IMFMediaEvent** e) override { return queue_ ? queue_->GetEvent(f, e) : MF_E_SHUTDOWN; }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* cb, IUnknown* st) override { return queue_ ? queue_->BeginGetEvent(cb, st) : MF_E_SHUTDOWN; }
    STDMETHODIMP EndGetEvent(IMFAsyncResult* r, IMFMediaEvent** e) override { return queue_ ? queue_->EndGetEvent(r, e) : MF_E_SHUTDOWN; }
    STDMETHODIMP QueueEvent(MediaEventType t, REFGUID g, HRESULT hr, const PROPVARIANT* v) override {
        return queue_ ? queue_->QueueEventParamVar(t, g, hr, v) : MF_E_SHUTDOWN;
    }
    // IMFMediaSource
    STDMETHODIMP GetCharacteristics(DWORD* c) override {
        if (!c) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        *c = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** out) override {
        if (!out) return E_POINTER;
        if (shutdown_) return MF_E_SHUTDOWN;
        return pd_->Clone(out);
    }
    STDMETHODIMP Start(IMFPresentationDescriptor* pd, const GUID* tf, const PROPVARIANT* pos) override {
        if (shutdown_) return MF_E_SHUTDOWN;
        if (!pd) return E_INVALIDARG;
        if (tf && *tf != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;
        (void)pos;   // live: always from now
        // The format that the game (or the source reader) chose: the current shape of the stream in this presentation descriptor.
        GUID fmt = RG_FMT_YUY2;
        uint32_t w = w_, h = h_;
        BOOL selected = FALSE;
        IMFStreamDescriptor* sd = nullptr;
        if (SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &selected, &sd)) && sd) {
            IMFMediaTypeHandler* th = nullptr;
            IMFMediaType* mt = nullptr;
            if (SUCCEEDED(sd->GetMediaTypeHandler(&th)) && th && SUCCEEDED(th->GetCurrentMediaType(&mt)) && mt) {
                GUID sub;
                UINT64 fs = 0;
                if (SUCCEEDED(mt->GetGUID(MF_MT_SUBTYPE, &sub)) && (sub == RG_FMT_YUY2 || sub == RG_FMT_NV12 || sub == RG_FMT_RGB32)) fmt = sub;
                if (SUCCEEDED(mt->GetUINT64(MF_MT_FRAME_SIZE, &fs))) { w = static_cast<uint32_t>(fs >> 32); h = static_cast<uint32_t>(fs); }
            }
            if (mt) mt->Release();
            if (th) th->Release();
            sd->Release();
        }
        if (w != w_ || h != h_) { rg_log("note: requested %ux%u, the frame is %ux%u; gray frames", w, h, w_, h_); }
        rg_log("Start: %s %ux%u, stroom %s", fmt_name(fmt), w, h, selected ? "selected" : "NOT selected");
        EnterCriticalSection(&cs_);
        const bool first = !stream_announced_;
        stream_announced_ = true;
        if (!t0_) t0_ = now_100ns();
        const LONGLONG t0 = t0_;
        LeaveCriticalSection(&cs_);
        if (selected) {
            QueueEventUnk(first ? MENewStream : MEUpdatedStream, static_cast<IMFMediaStream*>(stream_));
            stream_->start(fmt, w, h, t0);
        }
        PROPVARIANT v;
        PropVariantInit(&v);
        v.vt = VT_I8;
        v.hVal.QuadPart = 0;
        return QueueEvent(MESourceStarted, GUID_NULL, S_OK, &v);
    }
    STDMETHODIMP Stop() override {
        if (shutdown_) return MF_E_SHUTDOWN;
        rg_log("Stop");
        stream_->stop();
        return QueueEvent(MESourceStopped, GUID_NULL, S_OK, nullptr);
    }
    STDMETHODIMP Pause() override { return shutdown_ ? MF_E_SHUTDOWN : MF_E_INVALID_STATE_TRANSITION; }   // live: no pause
    STDMETHODIMP Shutdown() override {
        if (shutdown_) return MF_E_SHUTDOWN;
        shutdown_ = true;
        rg_log("Shutdown");
        if (stream_) { stream_->shutdown(); stream_->detach_source(); stream_->Release(); stream_ = nullptr; }
        if (queue_) { queue_->Shutdown(); queue_->Release(); queue_ = nullptr; }
        if (pd_) { pd_->Release(); pd_ = nullptr; }
        return S_OK;
    }

private:
    ~CamSource() {
        if (!shutdown_) Shutdown();
        DeleteCriticalSection(&cs_);
    }
    HRESULT QueueEventUnk(MediaEventType t, IUnknown* u) { return queue_ ? queue_->QueueEventParamUnk(t, GUID_NULL, S_OK, u) : MF_E_SHUTDOWN; }

    LONG ref_ = 1;
    uint32_t w_, h_;
    CRITICAL_SECTION cs_;
    IMFMediaEventQueue* queue_ = nullptr;
    IMFPresentationDescriptor* pd_ = nullptr;
    CamStream* stream_ = nullptr;
    bool shutdown_ = false, stream_announced_ = false;
    LONGLONG t0_ = 0;
    friend class CamStream;
};

STDMETHODIMP CamStream::GetMediaSource(IMFMediaSource** out) {
    if (!out) return E_POINTER;
    if (shutdown_ || !src_) return MF_E_SHUTDOWN;
    *out = src_;
    src_->AddRef();
    return S_OK;
}

// --- the activator (what MFEnumDeviceSources returns) ---------------------------------------------------------------------------------
// IMFActivate is an IMFAttributes; those parts go to a plain attribute store from mfplat.
#define RG_FWD(sig, call) STDMETHODIMP sig override { return attrs_->call; }
class CamActivate final : public IMFActivate {
public:
    CamActivate(uint32_t w, uint32_t h) : w_(w), h_(h) {}
    HRESULT init() {
        HRESULT hr = MFCreateAttributes(&attrs_, 4);
        if (FAILED(hr)) return hr;
        attrs_->SetGUID(RG_DEVSOURCE_SOURCE_TYPE, RG_DEVSOURCE_VIDCAP_GUID);
        attrs_->SetString(RG_DEVSOURCE_FRIENDLY_NAME, L"RavenGun camera");
        attrs_->SetString(RG_DEVSOURCE_VIDCAP_SYMLINK, L"\\\\?\\ravengun#camera#0#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global");
        return S_OK;
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IMFAttributes || riid == IID_IMFActivate) {
            *ppv = static_cast<IMFActivate*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    RG_FWD(GetItem(REFGUID k, PROPVARIANT* v), GetItem(k, v))
    RG_FWD(GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t), GetItemType(k, t))
    RG_FWD(CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r), CompareItem(k, v, r))
    RG_FWD(Compare(IMFAttributes* a, MF_ATTRIBUTES_MATCH_TYPE m, BOOL* r), Compare(a, m, r))
    RG_FWD(GetUINT32(REFGUID k, UINT32* v), GetUINT32(k, v))
    RG_FWD(GetUINT64(REFGUID k, UINT64* v), GetUINT64(k, v))
    RG_FWD(GetDouble(REFGUID k, double* v), GetDouble(k, v))
    RG_FWD(GetGUID(REFGUID k, GUID* v), GetGUID(k, v))
    RG_FWD(GetStringLength(REFGUID k, UINT32* v), GetStringLength(k, v))
    RG_FWD(GetString(REFGUID k, LPWSTR v, UINT32 n, UINT32* l), GetString(k, v, n, l))
    RG_FWD(GetAllocatedString(REFGUID k, LPWSTR* v, UINT32* l), GetAllocatedString(k, v, l))
    RG_FWD(GetBlobSize(REFGUID k, UINT32* v), GetBlobSize(k, v))
    RG_FWD(GetBlob(REFGUID k, UINT8* b, UINT32 n, UINT32* l), GetBlob(k, b, n, l))
    RG_FWD(GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* l), GetAllocatedBlob(k, b, l))
    RG_FWD(GetUnknown(REFGUID k, REFIID r, LPVOID* p), GetUnknown(k, r, p))
    RG_FWD(SetItem(REFGUID k, REFPROPVARIANT v), SetItem(k, v))
    RG_FWD(DeleteItem(REFGUID k), DeleteItem(k))
    RG_FWD(DeleteAllItems(), DeleteAllItems())
    RG_FWD(SetUINT32(REFGUID k, UINT32 v), SetUINT32(k, v))
    RG_FWD(SetUINT64(REFGUID k, UINT64 v), SetUINT64(k, v))
    RG_FWD(SetDouble(REFGUID k, double v), SetDouble(k, v))
    RG_FWD(SetGUID(REFGUID k, REFGUID v), SetGUID(k, v))
    RG_FWD(SetString(REFGUID k, LPCWSTR v), SetString(k, v))
    RG_FWD(SetBlob(REFGUID k, const UINT8* b, UINT32 n), SetBlob(k, b, n))
    RG_FWD(SetUnknown(REFGUID k, IUnknown* u), SetUnknown(k, u))
    RG_FWD(LockStore(), LockStore())
    RG_FWD(UnlockStore(), UnlockStore())
    RG_FWD(GetCount(UINT32* n), GetCount(n))
    RG_FWD(GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v), GetItemByIndex(i, k, v))
    RG_FWD(CopyAllItems(IMFAttributes* d), CopyAllItems(d))
    // IMFActivate
    STDMETHODIMP ActivateObject(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (!source_) {
            CamSource* s = new CamSource(w_, h_);
            const HRESULT hr = s->init();
            if (FAILED(hr)) {
                rg_log("ERROR: creating camera failed (hr=0x%08lx)", static_cast<unsigned long>(hr));
                s->Release();
                return hr;
            }
            source_ = s;
            rg_log("ActivateObject: camera %ux%u created (YUY2, NV12, RGB32 @ %d fps)", w_, h_, g_fps);
        }
        return source_->QueryInterface(riid, ppv);
    }
    STDMETHODIMP ShutdownObject() override {
        if (source_) { source_->Shutdown(); source_->Release(); source_ = nullptr; }
        return S_OK;
    }
    STDMETHODIMP DetachObject() override {
        if (source_) { source_->Release(); source_ = nullptr; }
        return S_OK;
    }

private:
    ~CamActivate() {
        if (source_) source_->Release();
        if (attrs_) attrs_->Release();
    }
    LONG ref_ = 1;
    uint32_t w_, h_;
    IMFAttributes* attrs_ = nullptr;
    CamSource* source_ = nullptr;
};
#undef RG_FWD

// --- MFEnumDeviceSources ---------------------------------------------------------------------------------------------------------------
using EnumFn = HRESULT(WINAPI*)(IMFAttributes*, IMFActivate***, UINT32*);
EnumFn g_orig_enum = nullptr;
LONG g_enum_calls = 0;

HRESULT WINAPI rg_MFEnumDeviceSources(IMFAttributes* attrs, IMFActivate*** sources, UINT32* count) {
    const LONG n = InterlockedIncrement(&g_enum_calls);
    GUID type = GUID_NULL;
    const bool vidcap = attrs && SUCCEEDED(attrs->GetGUID(RG_DEVSOURCE_SOURCE_TYPE, &type)) && type == RG_DEVSOURCE_VIDCAP_GUID;
    uint32_t w = 0, h = 0;
    if (vidcap && sources && count && feed_present(w, h)) {
        CamActivate* a = new CamActivate(w, h);
        HRESULT hr = a->init();
        IMFActivate** arr = static_cast<IMFActivate**>(CoTaskMemAlloc(sizeof(IMFActivate*)));
        if (FAILED(hr) || !arr) {
            a->Release();
            if (arr) CoTaskMemFree(arr);
            rg_log("MFEnumDeviceSources #%ld: ERROR while creating (hr=0x%08lx); passing on to the original", static_cast<long>(n),
                   static_cast<unsigned long>(hr));
        } else {
            arr[0] = a;
            *sources = arr;
            *count = 1;
            if (n <= 3 || n % 100 == 0) rg_log("MFEnumDeviceSources #%ld (VIDCAP): 1 camera, RavenGun camera %ux%u", static_cast<long>(n), w, h);
            return S_OK;
        }
    }
    const HRESULT hr = g_orig_enum ? g_orig_enum(attrs, sources, count) : E_NOTIMPL;
    if (n <= 3 || n % 100 == 0)
        rg_log("MFEnumDeviceSources #%ld (%s): %s; original: hr=0x%08lx, %u devices", static_cast<long>(n), vidcap ? "VIDCAP" : "other type",
               vidcap ? "no camera file from the daemon" : "not our business", static_cast<unsigned long>(hr),
               (SUCCEEDED(hr) && count) ? *count : 0u);
    return hr;
}

// --- the game's import table -------------------------------------------------------------------------------------------------------
// Redirects the entry `dll!func` in the import table of `mod` to `repl`; returns the old address (or nullptr if it is not there).
void* patch_iat(HMODULE mod, const char* dll, const char* func, void* repl) {
    auto* base = reinterpret_cast<BYTE*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (lstrcmpiA(reinterpret_cast<const char*>(base + imp->Name), dll) != 0) continue;
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        auto* names = imp->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk) : nullptr;
        for (size_t i = 0; iat[i].u1.Function; ++i) {
            bool hit = false;
            if (names && !(names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
                hit = std::strcmp(reinterpret_cast<const char*>(ibn->Name), func) == 0;
            } else if (!names) {
                HMODULE m = GetModuleHandleA(dll);
                hit = m && reinterpret_cast<void*>(iat[i].u1.Function) == reinterpret_cast<void*>(GetProcAddress(m, func));
            }
            if (!hit) continue;
            void* old = reinterpret_cast<void*>(iat[i].u1.Function);
            DWORD prot = 0;
            if (!VirtualProtect(&iat[i].u1.Function, sizeof(void*), PAGE_READWRITE, &prot)) return nullptr;
            iat[i].u1.Function = reinterpret_cast<decltype(iat[i].u1.Function)>(repl);
            VirtualProtect(&iat[i].u1.Function, sizeof(void*), prot, &prot);
            return old;
        }
    }
    return nullptr;
}

// --- the USB dongle (usb_dongle.h; switch RG_DONGLE) ---------------------------------------------------------
// The game finds its dongle by walking the USB tree (SetupAPI, CM_* and USB ioctls on the host controller
// and its root hub). RG_DONGLE=trace only logs the calls; RG_DONGLE=fake answers the walk with one
// device described by RG_DONGLE_USB / RG_DONGLE_SERIAL (and optionally RG_DONGLE_MANUFACTURER /
// RG_DONGLE_PRODUCT). Off with the TeknoParrot core in the process (that answers the dongle itself).
void setup_dongle() {
    char v[16];
    if (GetEnvironmentVariableA("RG_DONGLE", v, sizeof v) == 0 || !v[0]) return;   // off
    if (GetModuleHandleA("TeknoParrot.dll")) {
        rg_log("dongle: off, the TeknoParrot core is loaded");
        return;
    }
    rg_dongle::g_log = &rg_log;
    rg_dongle::g_fake = lstrcmpiA(v, "fake") == 0;
    if (rg_dongle::g_fake && !rg_dongle::load_device()) {
        rg_log("dongle: RG_DONGLE=fake without a valid RG_DONGLE_USB=vvvv:pppp; only tracing");
        rg_dongle::g_fake = false;
    }
    if (rg_dongle::g_fake)
        rg_log("dongle: on (fake): USB device %04x:%04x, serial \"%ls\"", rg_dongle::g_vid, rg_dongle::g_pid, rg_dongle::g_serial);
    else
        rg_log("dongle: on (%s): logging the USB tree walk", v);
    HMODULE self = GetModuleHandleW(nullptr);
    auto hook = [self](const char* dll, const char* fn, void* repl, void** old_fn) {
        void* old = patch_iat(self, dll, fn, repl);
        if (old) *old_fn = old;
        else rg_log("dongle: import table %s!%s NOT found", dll, fn);
    };
    hook("KERNEL32.dll", "CreateFileW", reinterpret_cast<void*>(&rg_dongle::hook_create_file_w),
         reinterpret_cast<void**>(&rg_dongle::g_create_file_w));
    hook("KERNEL32.dll", "DeviceIoControl", reinterpret_cast<void*>(&rg_dongle::hook_device_io_control),
         reinterpret_cast<void**>(&rg_dongle::g_device_io_control));
    hook("setupapi.dll", "SetupDiGetClassDevsW", reinterpret_cast<void*>(&rg_dongle::hook_get_class_devs),
         reinterpret_cast<void**>(&rg_dongle::g_get_class_devs));
    hook("setupapi.dll", "SetupDiEnumDeviceInterfaces", reinterpret_cast<void*>(&rg_dongle::hook_enum_interfaces),
         reinterpret_cast<void**>(&rg_dongle::g_enum_interfaces));
    hook("setupapi.dll", "SetupDiGetDeviceInterfaceDetailW", reinterpret_cast<void*>(&rg_dongle::hook_get_detail),
         reinterpret_cast<void**>(&rg_dongle::g_get_detail));
    hook("setupapi.dll", "CM_Locate_DevNodeW", reinterpret_cast<void*>(&rg_dongle::hook_cm_locate),
         reinterpret_cast<void**>(&rg_dongle::g_cm_locate));
    hook("setupapi.dll", "CM_Get_DevNode_Registry_PropertyW", reinterpret_cast<void*>(&rg_dongle::hook_cm_prop),
         reinterpret_cast<void**>(&rg_dongle::g_cm_prop));
    hook("setupapi.dll", "CM_Get_Child", reinterpret_cast<void*>(&rg_dongle::hook_cm_child),
         reinterpret_cast<void**>(&rg_dongle::g_cm_child));
    hook("setupapi.dll", "CM_Get_Parent", reinterpret_cast<void*>(&rg_dongle::hook_cm_parent),
         reinterpret_cast<void**>(&rg_dongle::g_cm_parent));
    hook("setupapi.dll", "CM_Get_Sibling", reinterpret_cast<void*>(&rg_dongle::hook_cm_sibling),
         reinterpret_cast<void**>(&rg_dongle::g_cm_sibling));
}

// --- d3dx10: pHResult like Windows (see SECOND TASK at the top) ---------------------------------------------------------------------------
// Only the arguments we touch have a type; the rest passes through unseen. A sentinel in *pHResult shows in the log
// whether the original set it itself (then the repair is unnecessary) or left it untouched.
constexpr HRESULT kHrSentinel = static_cast<HRESULT>(0x5247A11Bu);
using FxFileAFn = HRESULT(WINAPI*)(const char*, const void*, void*, const char*, UINT, UINT, void*, void*, void*, void**, void**, HRESULT*);
using FxFileWFn = HRESULT(WINAPI*)(const wchar_t*, const void*, void*, const char*, UINT, UINT, void*, void*, void*, void**, void**, HRESULT*);
using SrvFileAFn = HRESULT(WINAPI*)(void*, const char*, void*, void*, void**, HRESULT*);
using SrvMemFn = HRESULT(WINAPI*)(void*, const void*, SIZE_T, void*, void*, void**, HRESULT*);
FxFileAFn g_fx_a = nullptr;
FxFileWFn g_fx_w = nullptr;
SrvFileAFn g_srv_a = nullptr;
SrvMemFn g_srv_mem = nullptr;
LONG g_hr_fixed = 0, g_hr_set_by_orig = 0;

void hr_fix(const char* fn, HRESULT hr, void* pump, HRESULT* phr) {
    if (!phr || pump) return;
    if (*phr == kHrSentinel) {
        *phr = hr;
        const LONG n = InterlockedIncrement(&g_hr_fixed);
        if (n <= 3 || n % 500 == 0) rg_log("d3dx10: %s left pHResult untouched; now 0x%08lx (%ld times)", fn, static_cast<unsigned long>(hr), static_cast<long>(n));
    } else if (InterlockedIncrement(&g_hr_set_by_orig) <= 3) {
        rg_log("d3dx10: %s set pHResult itself (0x%08lx)", fn, static_cast<unsigned long>(*phr));
    }
}

HRESULT WINAPI rg_FxFileA(const char* f, const void* d, void* i, const char* p, UINT h, UINT x, void* dev, void* pool, void* pump, void** fx, void** err, HRESULT* phr) {
    if (phr && !pump) *phr = kHrSentinel;
    const HRESULT hr = g_fx_a(f, d, i, p, h, x, dev, pool, pump, fx, err, phr);
    hr_fix("D3DX10CreateEffectFromFileA", hr, pump, phr);
    return hr;
}
HRESULT WINAPI rg_FxFileW(const wchar_t* f, const void* d, void* i, const char* p, UINT h, UINT x, void* dev, void* pool, void* pump, void** fx, void** err, HRESULT* phr) {
    if (phr && !pump) *phr = kHrSentinel;
    const HRESULT hr = g_fx_w(f, d, i, p, h, x, dev, pool, pump, fx, err, phr);
    hr_fix("D3DX10CreateEffectFromFileW", hr, pump, phr);
    return hr;
}
HRESULT WINAPI rg_SrvFileA(void* dev, const char* f, void* info, void* pump, void** srv, HRESULT* phr) {
    if (phr && !pump) *phr = kHrSentinel;
    const HRESULT hr = g_srv_a(dev, f, info, pump, srv, phr);
    hr_fix("D3DX10CreateShaderResourceViewFromFileA", hr, pump, phr);
    return hr;
}
HRESULT WINAPI rg_SrvMem(void* dev, const void* data, SIZE_T size, void* info, void* pump, void** srv, HRESULT* phr) {
    if (phr && !pump) *phr = kHrSentinel;
    const HRESULT hr = g_srv_mem(dev, data, size, info, pump, srv, phr);
    hr_fix("D3DX10CreateShaderResourceViewFromMemory", hr, pump, phr);
    return hr;
}

template <typename Fn>
void hook_d3dx10(const char* func, Fn repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), "d3dx10_43.dll", func, reinterpret_cast<void*>(repl));
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("import table: d3dx10_43.dll!%s %s", func, old ? "redirected (pHResult like Windows)" : "not in the game");
}

void setup_d3dx10_hrfix() {
    wchar_t buf[8];
    if (GetEnvironmentVariableW(L"RG_D3DX10_HRFIX", buf, 8) > 0 && buf[0] == L'0') {
        rg_log("RG_D3DX10_HRFIX=0: d3dx10 pHResult not repaired");
        return;
    }
    hook_d3dx10("D3DX10CreateEffectFromFileA", &rg_FxFileA, g_fx_a);
    hook_d3dx10("D3DX10CreateEffectFromFileW", &rg_FxFileW, g_fx_w);
    hook_d3dx10("D3DX10CreateShaderResourceViewFromFileA", &rg_SrvFileA, g_srv_a);
    hook_d3dx10("D3DX10CreateShaderResourceViewFromMemory", &rg_SrvMem, g_srv_mem);
}

// --- JVS over COM3 to the pipe (see THIRD TASK at the top) ---------------------------------------------------------------------------------
char g_jvs_port[32];
char g_jvs_pipe[128] = "\\\\.\\pipe\\TeknoParrot_JVS";
volatile HANDLE g_jvs_handle = INVALID_HANDLE_VALUE;
HANDLE g_jvs_map = nullptr;
const volatile uint8_t* g_jvs_state = nullptr;

using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using SetCommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using GetCommStateFn = BOOL(WINAPI*)(HANDLE, LPDCB);
using SetCommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);
using GetCommTimeoutsFn = BOOL(WINAPI*)(HANDLE, LPCOMMTIMEOUTS);
using SetupCommFn = BOOL(WINAPI*)(HANDLE, DWORD, DWORD);
using PurgeCommFn = BOOL(WINAPI*)(HANDLE, DWORD);
using GetCommModemStatusFn = BOOL(WINAPI*)(HANDLE, LPDWORD);
CreateFileAFn g_create_file_a = nullptr;
SetCommStateFn g_set_comm_state = nullptr;
GetCommStateFn g_get_comm_state = nullptr;
SetCommTimeoutsFn g_set_comm_timeouts = nullptr;
GetCommTimeoutsFn g_get_comm_timeouts = nullptr;
SetupCommFn g_setup_comm = nullptr;
PurgeCommFn g_purge_comm = nullptr;
GetCommModemStatusFn g_get_modem_status = nullptr;

bool is_jvs(HANDLE h) { return h != INVALID_HANDLE_VALUE && h == g_jvs_handle; }

// The emulated STR board (TENTH TASK); the functions are further down.
char g_str_port[32];
volatile HANDLE g_str_handle = INVALID_HANDLE_VALUE;
bool is_str(HANDLE h) { return h != INVALID_HANDLE_VALUE && h == g_str_handle; }
// The gun peripheral of Lost Land Adventure (THIRTEENTH TASK); the functions are further down.
volatile HANDLE g_gun_handle = INVALID_HANDLE_VALUE;
bool is_gun(HANDLE h) { return h != INVALID_HANDLE_VALUE && h == g_gun_handle; }
bool is_fake_com(HANDLE h) { return is_jvs(h) || is_str(h) || is_gun(h); }
HANDLE str_open(LPCSTR name, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD flags);

HANDLE WINAPI rg_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    if (g_str_port[0] && rg_jvscom::port_matches(name, g_str_port)) return str_open(name, access, sa, flags);
    if (!g_jvs_port[0] || !rg_jvscom::port_matches(name, g_jvs_port)) return g_create_file_a(name, access, share, sa, disp, flags, tmpl);
    // The bridge only creates the pipe when the game needs it or just before: wait briefly until it is there (at most ~5 s).
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 50; ++i) {
        h = g_create_file_a(g_jvs_pipe, access, share, sa, OPEN_EXISTING, flags, nullptr);
        if (h != INVALID_HANDLE_VALUE) break;
        const DWORD e = GetLastError();
        if (e == ERROR_PIPE_BUSY) WaitNamedPipeA(g_jvs_pipe, 100);
        else Sleep(100);
    }
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        rg_log("JVS: CreateFileA(%s) -> pipe %s could NOT be opened (error %lu)", name, g_jvs_pipe, static_cast<unsigned long>(e));
        SetLastError(e);
        return INVALID_HANDLE_VALUE;
    }
    g_jvs_handle = h;
    rg_log("JVS: CreateFileA(%s) -> pipe %s, handle %p", name, g_jvs_pipe, h);
    return h;
}
BOOL WINAPI rg_SetCommState(HANDLE h, LPDCB d) { return is_fake_com(h) ? TRUE : g_set_comm_state(h, d); }
BOOL WINAPI rg_GetCommState(HANDLE h, LPDCB d) { return is_fake_com(h) ? TRUE : g_get_comm_state(h, d); }
BOOL WINAPI rg_SetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_fake_com(h) ? TRUE : g_set_comm_timeouts(h, t); }
BOOL WINAPI rg_GetCommTimeouts(HANDLE h, LPCOMMTIMEOUTS t) { return is_fake_com(h) ? TRUE : g_get_comm_timeouts(h, t); }
BOOL WINAPI rg_SetupComm(HANDLE h, DWORD i, DWORD o) { return is_fake_com(h) ? TRUE : g_setup_comm(h, i, o); }
BOOL WINAPI rg_PurgeComm(HANDLE h, DWORD f) { return is_fake_com(h) ? TRUE : g_purge_comm(h, f); }
BOOL WINAPI rg_GetCommModemStatus(HANDLE h, LPDWORD status) {
    if (is_str(h)) {
        if (status) *status = 0x30;   // CTS and DSR: the board is there
        return TRUE;
    }
    if (!is_jvs(h)) return g_get_modem_status(h, status);
    if (!g_jvs_state) {
        g_jvs_map = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (g_jvs_map) g_jvs_state = static_cast<const volatile uint8_t*>(MapViewOfFile(g_jvs_map, FILE_MAP_READ, 0, 0, 64));
    }
    const bool addressed = g_jvs_state && g_jvs_state[0] != 0;
    if (status) *status = rg_jvscom::modem_status(addressed);
    return TRUE;
}

template <typename Fn>
void hook_kernel32(const char* func, Fn repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), "KERNEL32.dll", func, reinterpret_cast<void*>(repl));
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("import table: KERNEL32.dll!%s %s", func, old ? "redirected (COM redirection)" : "not in the game");
}

// Redirect CreateFileA and the comm calls once, for the JVS redirection and the STR board together (redirecting twice would make the wrappers point at
// themselves).
void hook_comm_once() {
    static bool done = false;
    if (done) return;
    done = true;
    hook_kernel32("CreateFileA", &rg_CreateFileA, g_create_file_a);
    hook_kernel32("SetCommState", &rg_SetCommState, g_set_comm_state);
    hook_kernel32("GetCommState", &rg_GetCommState, g_get_comm_state);
    hook_kernel32("SetCommTimeouts", &rg_SetCommTimeouts, g_set_comm_timeouts);
    hook_kernel32("GetCommTimeouts", &rg_GetCommTimeouts, g_get_comm_timeouts);
    hook_kernel32("SetupComm", &rg_SetupComm, g_setup_comm);
    hook_kernel32("PurgeComm", &rg_PurgeComm, g_purge_comm);
    hook_kernel32("GetCommModemStatus", &rg_GetCommModemStatus, g_get_modem_status);
    if (!g_create_file_a) rg_log("COM: CreateFileA is not in the game's import table; the redirection cannot catch anything");
}

void setup_jvs_com() {
    char port[32];
    if (GetEnvironmentVariableA("RG_JVS_COMPORT", port, sizeof port) == 0 || !port[0]) return;   // off
    // With the closed TeknoParrot core in the process (the game picks Core = teknoparrot for online play) that core hooks COM3 itself; two
    // redirections on top of each other do not help, so then this one stays off.
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("JVS redirection off: the TeknoParrot core is loaded and handles COM3 itself");
        return;
    }
    std::snprintf(g_jvs_port, sizeof g_jvs_port, "%s", port);
    char pipe[128];
    if (GetEnvironmentVariableA("RG_JVS_PIPE", pipe, sizeof pipe) > 0 && pipe[0]) std::snprintf(g_jvs_pipe, sizeof g_jvs_pipe, "%s", pipe);
    rg_log("JVS redirection on: %s -> %s", g_jvs_port, g_jvs_pipe);
    hook_comm_once();
}

// --- the emulated STR board on COM1 (see TENTH TASK at the top; logic in str_board.h) ------------------------------------------------------
// The game gets the client end of a pipe of our own (overlapped, the way it opens the COM port); a thread in this DLL is the board at the
// server end. If the game closes the handle (to reconnect), that thread ends and the next CreateFileA makes a new pair.
LONG g_str_serial = 0;

bool str_write(HANDLE pipe, HANDLE ev, const char* data) {
    OVERLAPPED ov = {};
    ov.hEvent = ev;
    ResetEvent(ev);
    DWORD done = 0;
    if (!WriteFile(pipe, data, static_cast<DWORD>(rg_strpcb::kReplyLen), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return false;
    return GetOverlappedResult(pipe, &ov, &done, TRUE) && done == rg_strpcb::kReplyLen;
}

struct StrPair {
    HANDLE server, client;
};

DWORD WINAPI str_board_thread(LPVOID arg) {
    auto* pair = static_cast<StrPair*>(arg);
    HANDLE pipe = pair->server;
    HANDLE rd_ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE wr_ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    rg_strpcb::Board board;
    uint8_t buf[256];
    OVERLAPPED ov = {};
    bool pending = false, ok = true;
    unsigned logged = 0;
    DWORD last_summary = GetTickCount();
    while (ok) {
        if (!pending) {
            ov = OVERLAPPED{};
            ov.hEvent = rd_ev;
            ResetEvent(rd_ev);
            if (!ReadFile(pipe, buf, sizeof buf, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) break;   // closed
            pending = true;
        }
        const DWORD w = WaitForSingleObject(rd_ev, 20);
        if (w == WAIT_OBJECT_0) {
            pending = false;
            DWORD got = 0;
            if (!GetOverlappedResult(pipe, &ov, &got, FALSE)) break;   // the game has closed the handle
            if (logged < 5) {
                char hex[16 * 3 + 1] = {};
                for (DWORD i = 0; i < got && i < 16; ++i) std::snprintf(hex + 3 * i, 4, "%02x ", buf[i]);
                rg_log("STR board: command from the game (%lu bytes): %s", static_cast<unsigned long>(got), hex);
                ++logged;
            }
            const unsigned due = board.on_bytes(got);
            for (unsigned i = 0; i < due && ok; ++i) ok = str_write(pipe, wr_ev, rg_strpcb::Board::reply());
        } else if (w == WAIT_TIMEOUT) {
            if (const char* st = board.idle_status()) {
                if (board.status_sent <= 3) rg_log("STR board: status %s sent", st);
                ok = str_write(pipe, wr_ev, st);
            }
        } else {
            break;
        }
        if (GetTickCount() - last_summary >= 10000) {
            last_summary = GetTickCount();
            rg_log("STR board: %u status reports, %lu command bytes, %lu replies", board.status_sent,
                   static_cast<unsigned long>(board.rx_bytes), static_cast<unsigned long>(board.replies));
        }
    }
    rg_log("STR board: connection closed (%u status reports, %lu command bytes, %lu replies)", board.status_sent,
           static_cast<unsigned long>(board.rx_bytes), static_cast<unsigned long>(board.replies));
    if (pending) {
        CancelIoEx(pipe, &ov);
        DWORD got = 0;
        GetOverlappedResult(pipe, &ov, &got, TRUE);   // wait for the cancelled read before buf and ov go off the stack
    }
    CloseHandle(pipe);
    CloseHandle(rd_ev);
    CloseHandle(wr_ev);
    // The game has closed the client end: that handle number may soon be something else (e.g. the JVS handle), so stop recognizing it as STR.
    InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(&g_str_handle), INVALID_HANDLE_VALUE, pair->client);
    delete pair;
    return 0;
}

HANDLE str_open(LPCSTR name, DWORD access, LPSECURITY_ATTRIBUTES sa, DWORD flags) {
    char pipe_name[96];
    std::snprintf(pipe_name, sizeof pipe_name, "\\\\.\\pipe\\rg_str_%lu_%ld", static_cast<unsigned long>(GetCurrentProcessId()),
                  static_cast<long>(InterlockedIncrement(&g_str_serial)));
    HANDLE server = CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0x2000, 0x2000, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE) {
        rg_log("STR board: CreateNamedPipe failed (error %lu)", static_cast<unsigned long>(GetLastError()));
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE client = g_create_file_a(pipe_name, access, 0, sa, OPEN_EXISTING, flags, nullptr);
    if (client == INVALID_HANDLE_VALUE) {
        rg_log("STR board: client end could not be opened (error %lu)", static_cast<unsigned long>(GetLastError()));
        CloseHandle(server);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ConnectNamedPipe(server, &ov) && GetLastError() == ERROR_IO_PENDING) WaitForSingleObject(ov.hEvent, 1000);   // the client is already there
    CloseHandle(ov.hEvent);
    g_str_handle = client;
    HANDLE t = CreateThread(nullptr, 0, &str_board_thread, new StrPair{server, client}, 0, nullptr);
    if (t) CloseHandle(t);
    rg_log("STR board: CreateFileA(%s, flags 0x%lx) -> own board (pipe %s), handle %p", name, static_cast<unsigned long>(flags), pipe_name, client);
    return client;
}

void setup_str_board() {
    char port[32];
    if (GetEnvironmentVariableA("RG_STR_COMPORT", port, sizeof port) == 0 || !port[0]) return;   // off
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("STR board off: the TeknoParrot core is loaded (it answers the steering board itself)");
        return;
    }
    std::snprintf(g_str_port, sizeof g_str_port, "%s", port);
    rg_log("STR board on: %s (19200 8N1) becomes a board of our own that reports C01/E00", g_str_port);
    hook_comm_once();
}

// --- the content router's echo reply (see ELEVENTH TASK at the top; logic in content_router.h) ---------------------------------------
using IcmpSendEchoFn = DWORD(WINAPI*)(HANDLE, ULONG, LPVOID, WORD, PVOID, LPVOID, DWORD, DWORD);
IcmpSendEchoFn g_icmp_send_echo = nullptr;
uint32_t g_router_addr = 0;
volatile LONG g_router_echoes = 0;

// Same eight stdcall arguments as IPHLPAPI!IcmpSendEcho; only the router's address is answered here.
DWORD WINAPI rg_IcmpSendEcho(HANDLE icmp, ULONG dest, LPVOID data, WORD size, PVOID options, LPVOID reply, DWORD reply_size,
                             DWORD timeout) {
    if (dest != g_router_addr)
        return g_icmp_send_echo ? g_icmp_send_echo(icmp, dest, data, size, options, reply, reply_size, timeout) : 0;
    const auto data_ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(reply) + rg_router::kEchoReplySize);
    const unsigned n = rg_router::fill_echo_reply(static_cast<uint8_t*>(reply), reply_size, dest, data, size, data_ptr);
    if (!n) {
        SetLastError(11001);   // IP_BUF_TOO_SMALL
        rg_log("content router: echo of %u bytes does not fit a reply buffer of %lu bytes", static_cast<unsigned>(size),
               static_cast<unsigned long>(reply_size));
        return 0;
    }
    const LONG count = InterlockedIncrement(&g_router_echoes);
    if (count <= 3 || count % 100 == 0) rg_log("content router: echo %ld answered (%u bytes)", static_cast<long>(count), static_cast<unsigned>(size));
    return n;
}

void setup_content_router() {
    char v[32];
    if (GetEnvironmentVariableA("RG_CONTENT_ROUTER", v, sizeof v) == 0 || !v[0]) return;   // off
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("content router: off, the TeknoParrot core is loaded");
        return;
    }
    if (!rg_router::parse_ipv4(v, &g_router_addr)) {
        rg_log("content router: RG_CONTENT_ROUTER=%s is not an IPv4 address; off", v);
        return;
    }
    void* old = patch_iat(GetModuleHandleW(nullptr), "IPHLPAPI.DLL", "IcmpSendEcho", reinterpret_cast<void*>(&rg_IcmpSendEcho));
    if (!old) {
        rg_log("content router: import table IPHLPAPI.DLL!IcmpSendEcho NOT found; off");
        return;
    }
    g_icmp_send_echo = reinterpret_cast<IcmpSendEchoFn>(old);
    rg_log("content router: on, echo requests to %s are answered", v);
}

// --- an own ShareK object (see FOURTH TASK at the top; logic and addresses in sharek_fake.h) -------------------------------------------------
struct SharekObject {
    const void* const* vtbl;
    volatile LONG ref;
};
static_assert(sizeof(GUID) == sizeof(rg_sharek::Guid), "GUID and rg_sharek::Guid must have the same layout");
const rg_sharek::Guid& as_guid(const GUID& g) { return *reinterpret_cast<const rg_sharek::Guid*>(&g); }

void guid_text(const GUID& g, char* out, size_t n) {
    std::snprintf(out, n, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0],
                  g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

void* g_sharek_vtbl[rg_sharek::kVtableSize];
SharekObject g_sharek = {nullptr, 1};
volatile LONG g_sharek_seen[rg_sharek::kVtableSize];
volatile LONG g_qi_known = 0;
volatile LONG g_qi_unknown = 0;

// The first use of each slot in the log: this way, after a tv run, it shows which methods the game really called.
void sk_note_from(int slot, void* caller) {
    if (slot >= 0 && slot < rg_sharek::kVtableSize && InterlockedExchange(&g_sharek_seen[slot], 1) == 0)
        rg_log("ShareK object: slot %d called (caller %p)", slot, caller);
}
void sk_note(int slot) { sk_note_from(slot, nullptr); }

HRESULT STDMETHODCALLTYPE sk_query_interface(SharekObject* self, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    char text[48];
    guid_text(riid, text, sizeof text);
    const bool known = rg_sharek::knows_iid(as_guid(riid));
    static const bool accept_any = [] {
        char any[8];
        return GetEnvironmentVariableA("RG_SHAREK_ANY_IID", any, sizeof any) != 0 && any[0] == '1';
    }();
    if (!known && !accept_any) {
        /* Unknown IIDs are rare; we always log those. Known IIDs only a few times,
         * otherwise every call writes a line (the game asks for this often). */
        if (InterlockedIncrement(&g_qi_unknown) <= 8)
            rg_log("ShareK object: QueryInterface(%s) -> E_NOINTERFACE (unknown)", text);
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    if (InterlockedIncrement(&g_qi_known) <= 8)
        rg_log("ShareK object: QueryInterface(%s) -> same object%s", text, known ? "" : " (unknown, RG_SHAREK_ANY_IID)");
    *ppv = self;
    InterlockedIncrement(&self->ref);
    return S_OK;
}
ULONG STDMETHODCALLTYPE sk_add_ref(SharekObject* self) { return static_cast<ULONG>(InterlockedIncrement(&self->ref)); }
// The object is static and stays alive; Release only counts down (never below 1: after a restart the game simply creates a new one).
ULONG STDMETHODCALLTYPE sk_release(SharekObject* self) {
    const LONG r = InterlockedDecrement(&self->ref);
    if (r < 1) {
        InterlockedExchange(&self->ref, 1);
        return 0;
    }
    return static_cast<ULONG>(r);
}
// IDispatch (slot 3..6): the game only uses the vtable methods; we do not offer late binding.
HRESULT STDMETHODCALLTYPE sk_get_type_info_count(SharekObject*, UINT* n) {
    if (n) *n = 0;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE sk_get_type_info(SharekObject*, UINT, LCID, void**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE sk_get_ids_of_names(SharekObject*, REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE sk_invoke(SharekObject*, DISPID, REFIID, LCID, WORD, void*, void*, void*, UINT*) { return E_NOTIMPL; }

template <int N>
HRESULT STDMETHODCALLTYPE sk_nop8(SharekObject*, void*) {
    sk_note_from(N, __builtin_return_address(0));
    return S_OK;
}
template <int N>
HRESULT STDMETHODCALLTYPE sk_nop20(SharekObject*, void*, void*, void*, void*) {
    sk_note_from(N, __builtin_return_address(0));
    return S_OK;
}
template <int N, rg_sharek::Kind K>
HRESULT STDMETHODCALLTYPE sk_word(SharekObject*, int16_t* out) {
    sk_note(N);
    int16_t v = 0;
    if (out && rg_sharek::out_word(K, v)) *out = v;
    return S_OK;
}

template <int N>
void sk_install(void** vt) {
    constexpr const rg_sharek::Slot* s = rg_sharek::find_slot(N);
    if constexpr (s == nullptr) {
        vt[N] = reinterpret_cast<void*>(&sk_nop8<N>);   // unused slot: never called by the game
    } else if constexpr (s->arg_bytes == 20) {
        vt[N] = reinterpret_cast<void*>(&sk_nop20<N>);
    } else if constexpr (s->kind == rg_sharek::Kind::Nop) {
        vt[N] = reinterpret_cast<void*>(&sk_nop8<N>);
    } else {
        vt[N] = reinterpret_cast<void*>(&sk_word<N, s->kind>);
    }
}
template <int N>
struct SkFill {
    static void run(void** vt) {
        SkFill<N - 1>::run(vt);
        sk_install<N>(vt);
    }
};
template <>
struct SkFill<2> {
    static void run(void**) {}
};

void sk_build() {
    g_sharek_vtbl[0] = reinterpret_cast<void*>(&sk_query_interface);
    g_sharek_vtbl[1] = reinterpret_cast<void*>(&sk_add_ref);
    g_sharek_vtbl[2] = reinterpret_cast<void*>(&sk_release);
    SkFill<rg_sharek::kVtableSize - 1>::run(g_sharek_vtbl);
    g_sharek_vtbl[3] = reinterpret_cast<void*>(&sk_get_type_info_count);
    g_sharek_vtbl[4] = reinterpret_cast<void*>(&sk_get_type_info);
    g_sharek_vtbl[5] = reinterpret_cast<void*>(&sk_get_ids_of_names);
    g_sharek_vtbl[6] = reinterpret_cast<void*>(&sk_invoke);
    g_sharek.vtbl = reinterpret_cast<const void* const*>(g_sharek_vtbl);
}

// --- AMAuthd IAuth: an in-process object for the ALL.Net authentication step --------------------
// The game asks CoCreateInstance(CLSID {4603BB03-...}, IID {045A5150-...}) and gets
// CLASS_E_CLASSNOTREG without AMAuthd. This object answers instead: it reports an authenticated
// cabinet with no online connection (see am_auth_fake.h for the measured interface and the status
// block). Every call is logged with its slot and caller, so the boot check stays observable.
volatile LONG g_am_seen[rg_amauth::kVtableSize];
struct AmAuthObject { const void* const* vtbl; volatile LONG ref; };
AmAuthObject g_am_auth;
void* g_am_auth_vtbl[rg_amauth::kVtableSize];

HRESULT STDMETHODCALLTYPE am_query(void* self, const void* riid, void** ppv) {
    if (!ppv) return E_POINTER;
    (void)riid;
    *ppv = self;
    InterlockedIncrement(&g_am_auth.ref);
    rg_log("AMAuth: QueryInterface -> the same object");
    return S_OK;
}
ULONG STDMETHODCALLTYPE am_addref(void*) { return static_cast<ULONG>(InterlockedIncrement(&g_am_auth.ref)); }
ULONG STDMETHODCALLTYPE am_release(void*) {
    const LONG r = InterlockedDecrement(&g_am_auth.ref);
    if (r < 1) { InterlockedExchange(&g_am_auth.ref, 1); return 0; }
    return static_cast<ULONG>(r);
}
void am_note(int slot, const char* what, void* caller) {
    if (slot >= 0 && slot < rg_amauth::kVtableSize && InterlockedExchange(&g_am_seen[slot], 1) == 0)
        rg_log("AMAuth-object: slot %d %s called (caller %p)", slot, what, caller);
}
// Temporary bench probe: dump the game's clBootCheck object so we can see which item still waits.
// Only with RG_AMAUTH_DUMP=1; the addresses come from the disassembly (image base 0x400000).
void am_dump_check_state() {
    static volatile LONG n = 0;
    static const bool on = [] {
        char v[8];
        return GetEnvironmentVariableA("RG_AMAUTH_DUMP", v, sizeof v) != 0 && v[0] == '1';
    }();
    if (!on) return;
    if (InterlockedIncrement(&n) % 500 != 1) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!base) return;
    const auto read = [base](uintptr_t va) -> uint32_t {
        return *reinterpret_cast<const uint32_t*>(base + (va - 0x400000));
    };
    const uint32_t check = read(0xec2a10);   // the check object pointer (global)
    const uint32_t holder = read(0xec29fc);
    const uint32_t g1 = read(0xeaee58);
    const uint32_t g2 = read(0xec2a00);
    const uint32_t g4 = read(0xec2a0c);
    rg_log("AMAuth dump: holder=%08lx net=%08lx(net3c=%ld net34=%lu net35=%lu) state=%08lx(st9=%lu s18=%ld s1c=%ld s20=%ld s24=%ld s2c=%ld)",
           static_cast<unsigned long>(holder), static_cast<unsigned long>(g1),
           g1 ? static_cast<long>(read(g1 + 0x3c)) : -1L, g1 ? static_cast<unsigned long>(read(g1 + 0x34)) : 0UL,
           g1 ? static_cast<unsigned long>(read(g1 + 0x35)) : 0UL, static_cast<unsigned long>(g2),
           g2 ? static_cast<unsigned long>(read(g2 + 9)) : 0UL, g2 ? static_cast<long>(read(g2 + 0x18)) : -1L,
           g2 ? static_cast<long>(read(g2 + 0x1c)) : -1L, g2 ? static_cast<long>(read(g2 + 0x20)) : -1L,
           g2 ? static_cast<long>(read(g2 + 0x24)) : -1L, g2 ? static_cast<long>(read(g2 + 0x2c)) : -1L);
    rg_log("AMAuth dump: chk=%08lx state=%lu sub=%lu c1640=%lu 4c=%ld 50=%ld 54=%ld 5e=%lu b98=%ld b9c=%ld bb4=%ld ui=%08lx db0=%lu cdc=%lu",
           static_cast<unsigned long>(check), check ? static_cast<unsigned long>(read(check)) : 0UL,
           check ? static_cast<unsigned long>(read(check + 4)) : 0UL, check ? static_cast<unsigned long>(read(check + 0x1640)) : 0UL,
           check ? static_cast<long>(read(check + 0x4c)) : -1L, check ? static_cast<long>(read(check + 0x50)) : -1L,
           check ? static_cast<long>(read(check + 0x54)) : -1L, check ? static_cast<unsigned long>(read(check + 0x5e)) : 0UL,
           check ? static_cast<long>(read(check + 0xb98)) : -1L, check ? static_cast<long>(read(check + 0xb9c)) : -1L,
           check ? static_cast<long>(read(check + 0xbb4)) : -1L, static_cast<unsigned long>(g4),
           g4 ? static_cast<unsigned long>(read(g4 + 0xdb0)) : 0UL, g4 ? static_cast<unsigned long>(read(g4 + 0xcdc)) : 0UL);
    if (check) {
        char rec[200]; size_t o = 0;
        for (int i = 0; i < 14 && o + 16 < sizeof rec; ++i)
            o += static_cast<size_t>(std::snprintf(rec + o, sizeof rec - o, " [%ld,%lu]",
                     static_cast<long>(read(check + 0x1430 + 0x1c * i)), static_cast<unsigned long>(read(check + 0x1434 + 0x1c * i))));
        rg_log("AMAuth dump: bootcheck records%s", rec);
    }
    const uint32_t allnet = read(0xec2a00);   // the ALL.Net check state machine object
    // 0xec2a0c points at the static "network/auth manager" instance (0xf48 bytes, ctor 0x463ac0).
    const uint32_t netmgr = read(0xec2a0c);
    rg_log("AMAuth dump: netmgr=%08lx s60=%ld s64=%ld s70=%ld s98=%ld s9c=%ld sb98=%ld sb9c=%ld sbb4=%ld sdb0=%lu sdb6=%lu sdc8=%ld sdcc=%ld sf3c=%lu",
           static_cast<unsigned long>(netmgr), netmgr ? static_cast<long>(read(netmgr + 0x60)) : -1L,
           netmgr ? static_cast<long>(read(netmgr + 0x64)) : -1L, netmgr ? static_cast<long>(read(netmgr + 0x70)) : -1L,
           netmgr ? static_cast<long>(read(netmgr + 0x98)) : -1L, netmgr ? static_cast<long>(read(netmgr + 0x9c)) : -1L,
           netmgr ? static_cast<long>(read(netmgr + 0xb98)) : -1L, netmgr ? static_cast<long>(read(netmgr + 0xb9c)) : -1L,
           netmgr ? static_cast<long>(read(netmgr + 0xbb4)) : -1L, netmgr ? static_cast<unsigned long>(read(netmgr + 0xdb0)) : 0UL,
           netmgr ? static_cast<unsigned long>(read(netmgr + 0xdb6)) : 0UL, netmgr ? static_cast<long>(read(netmgr + 0xdc8)) : -1L,
           netmgr ? static_cast<long>(read(netmgr + 0xdcc)) : -1L, netmgr ? static_cast<unsigned long>(read(netmgr + 0xf3c)) : 0UL);
    rg_log("AMAuth dump: allnet=%08lx state=%lu c10=%lu s18=%ld s1c=%ld s20=%ld s2c=%ld s30=%ld",
           static_cast<unsigned long>(allnet), allnet ? static_cast<unsigned long>(read(allnet)) : 0UL,
           allnet ? static_cast<unsigned long>(read(allnet + 0x10)) : 0UL, allnet ? static_cast<long>(read(allnet + 0x18)) : -1L,
           allnet ? static_cast<long>(read(allnet + 0x1c)) : -1L, allnet ? static_cast<long>(read(allnet + 0x20)) : -1L,
           allnet ? static_cast<long>(read(allnet + 0x2c)) : -1L, allnet ? static_cast<long>(read(allnet + 0x30)) : -1L);
    rg_log("AMAuth dump: holder18=%ld holder20=%ld",
           holder ? static_cast<long>(read(holder + 0x18)) : -1L, holder ? static_cast<long>(read(holder + 0x20)) : -1L);
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0) {
        const auto* patch = reinterpret_cast<const unsigned char*>(base + (0x802fd2 - 0x400000));
        rg_log("AMAuth dump: bytes at 0x802fd2: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
               patch[0], patch[1], patch[2], patch[3], patch[4], patch[5], patch[6], patch[7], patch[8], patch[9], patch[10]);
    }
    const uint32_t im = read(0xec29f4);
    const uint32_t im10 = im ? read(im + 0x10) : 0;
    rg_log("AMAuth dump: inputmgr=%08lx obj=%08lx i80=%08lx i84=%08lx i88=%08lx (testbit=%lu)",
           static_cast<unsigned long>(im), static_cast<unsigned long>(im10),
           im10 ? static_cast<unsigned long>(read(im10 + 0x80)) : 0UL,
           im10 ? static_cast<unsigned long>(read(im10 + 0x84)) : 0UL,
           im10 ? static_cast<unsigned long>(read(im10 + 0x88)) : 0UL,
           im10 ? ((read(im10 + 0x80) >> 4) & 1u) : 0UL);
    const uint32_t scene = read(0xec2a08);
    rg_log("AMAuth dump: scene=%08lx s3c=%ld s40=%lu s94=%lu s95=%lu s9c=%ld sa0=%lu sa1=%lu",
           static_cast<unsigned long>(scene), scene ? static_cast<long>(read(scene + 0x3c)) : -1L,
           scene ? static_cast<unsigned long>(read(scene + 0x40)) : 0UL,
           scene ? static_cast<unsigned long>(read(scene + 0x94)) : 0UL,
           scene ? static_cast<unsigned long>(read(scene + 0x95)) : 0UL,
           scene ? static_cast<long>(read(scene + 0x9c)) : -1L,
           scene ? static_cast<unsigned long>(read(scene + 0xa0)) : 0UL,
           scene ? static_cast<unsigned long>(read(scene + 0xa1)) : 0UL);
    const uint32_t sys = read(0xec29c8);
    const uint32_t e4 = read(0xec2a04);
    rg_log("AMAuth dump: sys=%08lx s34=%lu s35=%lu gate2a04=%08lx cdc=%lu cd8=%lu db0=%lu",
           static_cast<unsigned long>(sys), sys ? static_cast<unsigned long>(read(sys + 0x34)) : 0UL,
           sys ? static_cast<unsigned long>(read(sys + 0x35)) : 0UL, static_cast<unsigned long>(e4),
           e4 ? static_cast<unsigned long>(read(e4 + 0xcdc)) : 0UL, e4 ? static_cast<unsigned long>(read(e4 + 0xcd8)) : 0UL,
           e4 ? static_cast<unsigned long>(read(e4 + 0xdb0)) : 0UL);
}
// Slot 5: interface pointer + two dwords (measured 0x8B2735: push 0; push -1). stdcall ret 0x0C.
HRESULT STDMETHODCALLTYPE am_slot5(void*, int a, int b) {
    am_note(5, "init", __builtin_return_address(0));
    rg_log("AMAuth-object: slot 5 init(%d, %d) -> S_OK", a, b);
    return S_OK;
}
// Slot 9: interface pointer + a status block (see am_auth_fake.h). stdcall ret 0x08.
HRESULT STDMETHODCALLTYPE am_slot9(void*, void* block) {
    const int state = rg_amauth::auth_choice();
    if (block) rg_amauth::fill_status(static_cast<unsigned char*>(block), state);
    am_dump_check_state();
    // Temporary probes (RG_AMAUTH_POKE): 1 = force the holder request state to 6 ("auth done");
    // 2 = also mark the boot check object as finished (state 5, substate 7). Only to prove where the
    // boot sequence waits; not product behaviour.
    static const char poke = [] {
        char v[8];
        return GetEnvironmentVariableA("RG_AMAUTH_POKE", v, sizeof v) ? v[0] : '0';
    }();
    static volatile LONG poke_done = 0;
    if ((poke == '1' || poke == '2') && poke_done == 0) {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto* holder = reinterpret_cast<uint32_t*>(base + (0xec29fc - 0x400000));
        const uint32_t h = holder[0];
        auto* checkp = reinterpret_cast<uint32_t*>(base + (0xec2a10 - 0x400000));
        const uint32_t c = poke == '2' ? checkp[0] : 1;
        if (h && c) {
            InterlockedExchange(&poke_done, 1);
            auto* s = reinterpret_cast<uint32_t*>(h + 0x20);
            if (*s != 6) { rg_log("AMAuth poke: holder state %lu -> 6", static_cast<unsigned long>(*s)); *s = 6; }
            if (poke == '2' && c != 1) {
                auto* st = reinterpret_cast<uint32_t*>(c);
                if (st[0] != 5 || st[1] != 7) {
                    rg_log("AMAuth poke: boot check state %lu/%lu -> 5/7", static_cast<unsigned long>(st[0]), static_cast<unsigned long>(st[1]));
                    st[0] = 5; st[1] = 7;
                }
            }
        }
    }
    if (InterlockedExchange(&g_am_seen[9], 1) == 0) {
        rg_log("AMAuth-object: slot 9 status called (caller %p), auth state %d, block %p",
               __builtin_return_address(0), state, block);
        if (block) {
            const unsigned char* b = static_cast<const unsigned char*>(block);
            rg_log("AMAuth-object: status block +0x00=%08lx +0x04=%08lx +0x08=%08lx +0x0c=%08lx +0x10=%08lx +0x18=%08lx +0x1c=%08lx",
                   static_cast<unsigned long>(*reinterpret_cast<const uint32_t*>(b + 0x00)),
                   static_cast<unsigned long>(*reinterpret_cast<const uint32_t*>(b + 0x04)),
                   static_cast<unsigned long>(*reinterpret_cast<const uint32_t*>(b + 0x08)),
                   static_cast<unsigned long>(*reinterpret_cast<const uint32_t*>(b + 0x0c)),
                   static_cast<unsigned long>(*reinterpret_cast<const uint32_t*>(b + 0x10)),
                   static_cast<unsigned long>(*reinterpret_cast<const uint32_t*>(b + 0x18)),
                   static_cast<unsigned long>(*reinterpret_cast<const uint32_t*>(b + 0x1c)));
        }
    }
    return S_OK;
}
// Slot 10: interface pointer + a network-info block. The game only reads the HRESULT. stdcall ret 0x08.
HRESULT STDMETHODCALLTYPE am_slot10(void*, void* out) {
    am_note(10, "network info", __builtin_return_address(0));
    if (out) *static_cast<uint32_t*>(out) = 0;
    return S_OK;
}
// Slot 19: interface pointer + a 4-byte status / four doubles. 0 means "no data" to the caller
// (0x401A2F skips the block). stdcall ret 0x08.
HRESULT STDMETHODCALLTYPE am_slot19(void*, void* out) {
    am_note(19, "auth info", __builtin_return_address(0));
    // The block the game reads is 0x40 bytes; its first dword selects the variant (0 = no info,
    // 1 = fixed ping, 2..4 = measured hop/round-trip values). Bench knob: RG_AMAUTH_NET.
    static const uint32_t kind = [] {
        char v[8];
        if (GetEnvironmentVariableA("RG_AMAUTH_NET", v, sizeof v) == 0) return 0u;
        return static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
    }();
    // The game reads only the first dword of the block before it has data; keep the write to four
    // bytes (the caller's block is 0x3C bytes, so clearing more would trip its stack cookie).
    if (out) *static_cast<uint32_t*>(out) = kind;
    return S_OK;
}
// Slot 14: interface pointer + the game's server-name buffer (0x461A00). Offline the game keeps its own
// contents of that buffer (0x4619F0), so it is left untouched here. stdcall ret 0x08.
HRESULT STDMETHODCALLTYPE am_slot14(void*, void*) {
    am_note(14, "server name", __builtin_return_address(0));
    return S_OK;
}
// Slot 23: interface pointer + one dword (0x803935, the game passes 0 once the status block reports a
// pending update detail). There is no update offline; it only has to succeed. stdcall ret 0x08.
// (Measured 2026-10-20: with the one-argument stub below the game's stack was off by four bytes here
// and the game exited ~4 minutes into attract mode.)
HRESULT STDMETHODCALLTYPE am_slot23(void*, DWORD) {
    am_note(23, "update request", __builtin_return_address(0));
    return S_OK;
}
// Anything else: a stub with only the interface pointer (stdcall ret 4). The game calls slots 12, 13, 15,
// 29 and 30 that way (am_auth_fake.h lists the call sites). If it ever calls a slot not in that list, the
// log shows it; the table then gets extended from the call site with the right argument count.
HRESULT STDMETHODCALLTYPE am_unused(void*) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0)
        rg_log("AMAuth-object: an unmeasured vtable slot was called (caller %p); add it to am_auth_fake.h",
               __builtin_return_address(0));
    return S_OK;
}

void setup_am_auth() {
    static bool done = false;
    if (done) return;
    done = true;
    for (int i = 0; i < rg_amauth::kVtableSize; ++i) g_am_auth_vtbl[i] = reinterpret_cast<void*>(&am_unused);
    g_am_auth_vtbl[0] = reinterpret_cast<void*>(&am_query);
    g_am_auth_vtbl[1] = reinterpret_cast<void*>(&am_addref);
    g_am_auth_vtbl[2] = reinterpret_cast<void*>(&am_release);
    g_am_auth_vtbl[5] = reinterpret_cast<void*>(&am_slot5);
    g_am_auth_vtbl[9] = reinterpret_cast<void*>(&am_slot9);
    g_am_auth_vtbl[10] = reinterpret_cast<void*>(&am_slot10);
    g_am_auth_vtbl[14] = reinterpret_cast<void*>(&am_slot14);
    g_am_auth_vtbl[19] = reinterpret_cast<void*>(&am_slot19);
    g_am_auth_vtbl[23] = reinterpret_cast<void*>(&am_slot23);
    g_am_auth.vtbl = g_am_auth_vtbl;
    g_am_auth.ref = 1;
}

using CoCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
CoCreateInstanceFn g_co_create_instance = nullptr;
volatile LONG g_co_seen = 0;

HRESULT WINAPI rg_CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD ctx, REFIID riid, LPVOID* ppv) {
    char c[48], i[48];
    char am_on[8];
    const bool am_enabled = GetEnvironmentVariableA("RG_AMAUTH_FAKE", am_on, sizeof am_on) != 0 && am_on[0] == '1';
    if (am_enabled && rg_amauth::wants_clsid_bytes(&clsid)) {
        guid_text(riid, i, sizeof i);
        if (outer) {
            rg_log("AMAuth: CoCreateInstance with aggregation refused");
            return CLASS_E_NOAGGREGATION;
        }
        setup_am_auth();
        HRESULT hr = am_query(&g_am_auth, &riid, ppv);
        rg_log("AMAuth: CoCreateInstance(%s, ctx 0x%lx) -> own object, hr 0x%08lx", i,
               static_cast<unsigned long>(ctx), static_cast<unsigned long>(hr));
        return hr;
    }
    if (rg_sharek::wants_clsid(as_guid(clsid))) {
        guid_text(clsid, c, sizeof c);
        guid_text(riid, i, sizeof i);
        if (outer) {
            rg_log("ShareK: CoCreateInstance(%s) with aggregation refused", c);
            return CLASS_E_NOAGGREGATION;
        }
        HRESULT hr = sk_query_interface(&g_sharek, riid, ppv);
        rg_log("ShareK: CoCreateInstance(%s, ctx 0x%lx, %s) -> own object, hr 0x%08lx", c, static_cast<unsigned long>(ctx), i, static_cast<unsigned long>(hr));
        return hr;
    }
    HRESULT hr = g_co_create_instance(clsid, outer, ctx, riid, ppv);
    // Which other classes does the game ask for? (The IAuth of AMAuthd, {4603BB03-...}, should appear here with its real result.)
    if (InterlockedIncrement(&g_co_seen) <= 16) {
        guid_text(clsid, c, sizeof c);
        guid_text(riid, i, sizeof i);
        rg_log("CoCreateInstance(%s, ctx 0x%lx, %s) -> hr 0x%08lx (real call)", c, static_cast<unsigned long>(ctx), i, static_cast<unsigned long>(hr));
    }
    return hr;
}

// --- bench probe: follow the game's OutputDebugString (RG_ODBG=1) --------------------------------
// The boot check logs its state with OutputDebugString ("ALL.NetCheck_Step1/2/3..."); Wine would
// drop that on the floor. With RG_ODBG=1 it lands in rg-mfcam.log, so the check stays observable.
std::string to_utf8(const wchar_t* w);
using OutputDebugStringAFn = void(WINAPI*)(LPCSTR);
using OutputDebugStringWFn = void(WINAPI*)(LPCWSTR);
OutputDebugStringAFn g_odbg_a = nullptr;
OutputDebugStringWFn g_odbg_w = nullptr;
volatile LONG g_odbg_count = 0;

void WINAPI rg_OutputDebugStringA(LPCSTR s) {
    if (s && InterlockedIncrement(&g_odbg_count) <= 2000) rg_log("ODBG: %s", s);
    if (g_odbg_a) g_odbg_a(s);
}
void WINAPI rg_OutputDebugStringW(LPCWSTR s) {
    if (s && InterlockedIncrement(&g_odbg_count) <= 2000) rg_log("ODBG: %s", to_utf8(s).c_str());
    if (g_odbg_w) g_odbg_w(s);
}
BOOL WINAPI rg_IsDebuggerPresent() { return TRUE; }   // the game logs only when a debugger is attached
void setup_odbg() {
    char v[8];
    if (GetEnvironmentVariableA("RG_ODBG", v, sizeof v) == 0 || v[0] != '1') return;
    HMODULE game = GetModuleHandleW(nullptr);
    void* old_a = patch_iat(game, "KERNEL32.dll", "OutputDebugStringA", reinterpret_cast<void*>(&rg_OutputDebugStringA));
    void* old_w = patch_iat(game, "KERNEL32.dll", "OutputDebugStringW", reinterpret_cast<void*>(&rg_OutputDebugStringW));
    void* old_d = patch_iat(game, "KERNEL32.dll", "IsDebuggerPresent", reinterpret_cast<void*>(&rg_IsDebuggerPresent));
    if (old_a) g_odbg_a = reinterpret_cast<OutputDebugStringAFn>(old_a);
    if (old_w) g_odbg_w = reinterpret_cast<OutputDebugStringWFn>(old_w);
    rg_log("ODBG: OutputDebugString hooked (A %s, W %s), IsDebuggerPresent -> true (%s)", old_a ? "yes" : "no",
           old_w ? "yes" : "no", old_d ? "hooked" : "not found");
}
// The measuring instrument of rg-compat (docs/rg-compat.md, RG_COMPAT_SAMPLE=<ms>): a thread that
// samples the other threads and writes the hottest spots as module+RVA. Mario Kart has only
// one preload (this DLL), so we load rg-compat.dll here ourselves. Without RG_COMPAT_SAMPLE nothing
// happens and the file is not needed either.
// The memory engine (memory/src, docs/memory-engine.md) here too: Mario Kart has only one preload
// (this DLL), so we load ravengun-patchmem32.dll ourselves if RAVENGUN_PATCHMEM_FILE is set. The
// engine then does its own synchronous round on the main module and reads its patch list.
void setup_memory_engine() {
    char v[MAX_PATH];
    if (GetEnvironmentVariableA("RAVENGUN_PATCHMEM_FILE", v, sizeof v) == 0) return;
    // The patches are for our own route (offline cabinet). The closed TeknoParrot core (Core = teknoparrot,
    // online play) patches the game its own way and needs the network paths these patches switch off.
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("memory engine: off, the TeknoParrot core is loaded");
        return;
    }
    HMODULE m = LoadLibraryA("ravengun-patchmem32.dll");
    rg_log("memory engine: ravengun-patchmem32.dll %s (list %s)", m ? "loaded" : "NOT loaded", v);
}

void setup_sampler_thread() {
    char v[8];
    if (GetEnvironmentVariableA("RG_COMPAT_SAMPLE", v, sizeof v) == 0) return;
    HMODULE m = LoadLibraryA("rg-compat.dll");
    rg_log("sampler thread: rg-compat.dll %s (RG_COMPAT_SAMPLE=%s)", m ? "loaded" : "NOT loaded", v);
}

DWORD WINAPI load_helper_dlls(LPVOID) {
    setup_memory_engine();
    setup_sampler_thread();
    return 0;
}

void setup_sharek_fake() {
    char sk[8], am[8];
    const bool sharek_on = GetEnvironmentVariableA("RG_SHAREK_FAKE", sk, sizeof sk) != 0 && sk[0] == '1';
    const bool am_on = GetEnvironmentVariableA("RG_AMAUTH_FAKE", am, sizeof am) != 0 && am[0] == '1';
    if (!sharek_on && !am_on) return;   // both off
    // The closed TeknoParrot core answers these classes itself (Core = teknoparrot, online play):
    // there everything stays as it was.
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("CoCreateInstance objects off: the TeknoParrot core is loaded");
        return;
    }
    if (sharek_on) sk_build();
    void* old = patch_iat(GetModuleHandleW(nullptr), "ole32.dll", "CoCreateInstance", reinterpret_cast<void*>(&rg_CoCreateInstance));
    if (old) {
        g_co_create_instance = reinterpret_cast<CoCreateInstanceFn>(old);
        rg_log("import table: ole32.dll!CoCreateInstance redirected (ShareK %s, AMAuth %s, old address %p)",
               sharek_on ? "on" : "off", am_on ? "on" : "off", old);
    } else {
        rg_log("import table: ole32.dll!CoCreateInstance NOT found in the game; no in-process objects");
    }
}

// --- additional drive letter redirection (see SIXTH TASK at the top; logic in drive_fix.h) ------------------------------------------------------
char g_drive_letters[28];
volatile LONG g_drive_logged = 0;

std::string to_utf8(const wchar_t* w) {
    char b[600];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b - 1, nullptr, nullptr);
    return n > 0 ? std::string(b) : std::string("?");
}
void drive_note(const char* what, const std::string& from, const std::string& to, long result) {
    if (InterlockedIncrement(&g_drive_logged) <= 40) rg_log("drive letter: %s(%s) -> %s = %ld", what, from.c_str(), to.c_str(), result);
}

using RenameFn = int(__cdecl*)(const char*, const char*);
using WRenameFn = int(__cdecl*)(const wchar_t*, const wchar_t*);
using RemoveFn = int(__cdecl*)(const char*);
using WRemoveFn = int(__cdecl*)(const wchar_t*);
using WAccessSFn = int(__cdecl*)(const wchar_t*, int);
using FindFirstAFn = HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
using FindFirstWFn = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
using GetAttrWFn = DWORD(WINAPI*)(LPCWSTR);
RenameFn g_rename = nullptr;
WRenameFn g_wrename = nullptr;
RemoveFn g_remove = nullptr;
WRemoveFn g_wremove = nullptr;
WAccessSFn g_waccess_s = nullptr;
FindFirstAFn g_find_first_a = nullptr;
FindFirstWFn g_find_first_w = nullptr;
GetAttrWFn g_get_attr_w = nullptr;

// Returns the redirected path (in `buf`) or the original.
const char* fix_a(const char* in, char* buf, size_t n) { return rg_drivefix::redirect<char>(in, g_drive_letters, buf, n) ? buf : in; }
const wchar_t* fix_w(const wchar_t* in, wchar_t* buf, size_t n) { return rg_drivefix::redirect<wchar_t>(in, g_drive_letters, buf, n) ? buf : in; }

int __cdecl rg_rename(const char* a, const char* b) {
    char x[520], y[520];
    const char *pa = fix_a(a, x, sizeof x), *pb = fix_a(b, y, sizeof y);
    const int r = g_rename(pa, pb);
    if (pa != a || pb != b) drive_note("rename", std::string(a ? a : "") + " => " + (b ? b : ""), std::string(pa ? pa : "") + " => " + (pb ? pb : ""), r);
    return r;
}
int __cdecl rg_wrename(const wchar_t* a, const wchar_t* b) {
    wchar_t x[520], y[520];
    const wchar_t *pa = fix_w(a, x, 520), *pb = fix_w(b, y, 520);
    const int r = g_wrename(pa, pb);
    if (pa != a || pb != b) drive_note("_wrename", to_utf8(a) + " => " + to_utf8(b), to_utf8(pa) + " => " + to_utf8(pb), r);
    return r;
}
int __cdecl rg_remove(const char* a) {
    char x[520];
    const char* pa = fix_a(a, x, sizeof x);
    const int r = g_remove(pa);
    if (pa != a) drive_note("remove", a, pa, r);
    return r;
}
int __cdecl rg_wremove(const wchar_t* a) {
    wchar_t x[520];
    const wchar_t* pa = fix_w(a, x, 520);
    const int r = g_wremove(pa);
    if (pa != a) drive_note("_wremove", to_utf8(a), to_utf8(pa), r);
    return r;
}
int __cdecl rg_waccess_s(const wchar_t* a, int mode) {
    wchar_t x[520];
    const wchar_t* pa = fix_w(a, x, 520);
    const int r = g_waccess_s(pa, mode);
    if (pa != a) drive_note("_waccess_s", to_utf8(a), to_utf8(pa), r);
    return r;
}
HANDLE WINAPI rg_FindFirstFileA(LPCSTR a, LPWIN32_FIND_DATAA d) {
    char x[520];
    const char* pa = fix_a(a, x, sizeof x);
    HANDLE h = g_find_first_a(pa, d);
    if (pa != a) drive_note("FindFirstFileA", a, pa, h == INVALID_HANDLE_VALUE ? -1L : 0L);
    return h;
}
HANDLE WINAPI rg_FindFirstFileW(LPCWSTR a, LPWIN32_FIND_DATAW d) {
    wchar_t x[520];
    const wchar_t* pa = fix_w(a, x, 520);
    HANDLE h = g_find_first_w(pa, d);
    if (pa != a) drive_note("FindFirstFileW", to_utf8(a), to_utf8(pa), h == INVALID_HANDLE_VALUE ? -1L : 0L);
    return h;
}
DWORD WINAPI rg_GetFileAttributesW(LPCWSTR a) {
    wchar_t x[520];
    const wchar_t* pa = fix_w(a, x, 520);
    const DWORD r = g_get_attr_w(pa);
    if (pa != a) drive_note("GetFileAttributesW", to_utf8(a), to_utf8(pa), static_cast<long>(r));
    return r;
}

template <typename Fn>
void hook_import(const char* dll, const char* func, void* repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), dll, func, repl);
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("import table: %s!%s %s", dll, func, old ? "redirected" : "not in the game");
}

void patch_drive_strings();

void setup_drive_fix() {
    char v[28];
    if (GetEnvironmentVariableA("RG_DRIVE_FIXUP", v, sizeof v) == 0 || !v[0]) return;   // off
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("drive letter redirection off: the TeknoParrot core is loaded");
        return;
    }
    std::snprintf(g_drive_letters, sizeof g_drive_letters, "%s", v);
    rg_log("drive letter redirection on for: %s", g_drive_letters);
    char ds[8];
    if (!(GetEnvironmentVariableA("RG_DRIVE_STRINGS", ds, sizeof ds) > 0 && ds[0] == '0')) patch_drive_strings();
    hook_import("MSVCR100.dll", "rename", reinterpret_cast<void*>(&rg_rename), g_rename);
    hook_import("MSVCR100.dll", "_wrename", reinterpret_cast<void*>(&rg_wrename), g_wrename);
    hook_import("MSVCR100.dll", "remove", reinterpret_cast<void*>(&rg_remove), g_remove);
    hook_import("MSVCR100.dll", "_wremove", reinterpret_cast<void*>(&rg_wremove), g_wremove);
    hook_import("MSVCR100.dll", "_waccess_s", reinterpret_cast<void*>(&rg_waccess_s), g_waccess_s);
    hook_import("KERNEL32.dll", "FindFirstFileA", reinterpret_cast<void*>(&rg_FindFirstFileA), g_find_first_a);
    hook_import("KERNEL32.dll", "FindFirstFileW", reinterpret_cast<void*>(&rg_FindFirstFileW), g_find_first_w);
    hook_import("KERNEL32.dll", "GetFileAttributesW", reinterpret_cast<void*>(&rg_GetFileAttributesW), g_get_attr_w);
}

// --- the Banapass card reader (see SEVENTH TASK at the top; logic in bngrw_fake.h) ----------------------------------------------------------------
bool g_bng_fake = false;
rg_bngrw::Fake g_bng;
CRITICAL_SECTION g_bng_cs;
volatile LONG g_bng_logged = 0;

#define BNG_LOG(...) do { if (InterlockedIncrement(&g_bng_logged) <= 400) rg_log(__VA_ARGS__); } while (0)

// The library's callbacks are cdecl: (reader, result, user) or, for ReqWaitTouch, (reader, result, card, user).
using BngCb3 = void(__cdecl*)(int, int, void*);
using BngCb4 = void(__cdecl*)(int, int, void*, void*);
struct BngThunk {
    void* cb;
    void* user;
    int reader;
    const char* what;
};
void __cdecl bng_thunk3(int a, int b, void* t_) {
    auto* t = static_cast<BngThunk*>(t_);
    BNG_LOG("bngrw: callback %s(reader %d): a=%d b=0x%x", t->what, t->reader, a, static_cast<unsigned>(b));
    reinterpret_cast<BngCb3>(t->cb)(a, b, t->user);
}
void __cdecl bng_thunk4(int a, int b, void* c, void* t_) {
    auto* t = static_cast<BngThunk*>(t_);
    BNG_LOG("bngrw: callback %s(reader %d): a=%d b=0x%x card=%p", t->what, t->reader, a, static_cast<unsigned>(b), c);
    reinterpret_cast<BngCb4>(t->cb)(a, b, c, t->user);
}
BngThunk* bng_thunk(void* cb, void* user, int reader, const char* what) {
    return new BngThunk{cb, user, reader, what};   // small, deliberately not freed: the library may still call the callback later
}

using BngInitFn = int(__stdcall*)();
using BngFinFn = int(__stdcall*)();
using BngAttachFn = int(__stdcall*)(int, const char*, int, int, void*, void*);
using BngIsExecFn = int(__stdcall*)(int);
using BngLedPowerFn = int(__stdcall*)(int, int, int);
using BngDevResetFn = int(__stdcall*)(int, void*, void*);
using BngReq4Fn = int(__stdcall*)(int, int, void*, void*);   // ReqLed, ReqBeep
using BngWaitTouchFn = int(__stdcall*)(int, int, int, void*, void*);
using BngCancelFn = int(__stdcall*)(int);
using BngSendUrlFn = int(__stdcall*)(int, int, int, void*, void*, const char*, void*, void*);
BngInitFn g_bng_init = nullptr;
BngFinFn g_bng_fin = nullptr;
BngAttachFn g_bng_attach = nullptr;
BngIsExecFn g_bng_is_exec = nullptr;
BngLedPowerFn g_bng_led_power = nullptr;
BngDevResetFn g_bng_dev_reset = nullptr;
BngReq4Fn g_bng_req_led = nullptr;
BngReq4Fn g_bng_req_beep = nullptr;
BngWaitTouchFn g_bng_wait_touch = nullptr;
BngCancelFn g_bng_cancel = nullptr;
BngSendUrlFn g_bng_send_url = nullptr;

// Fake: call the callback immediately with result 0.
void bng_done(void* cb, void* user, int reader) {
    if (cb) reinterpret_cast<BngCb3>(cb)(reader, rg_bngrw::kResultOk, user);
}

int __stdcall rg_BngRwInit() {
    if (g_bng_fake) {
        BNG_LOG("bngrw: Init (fake)");
        return 0;
    }
    const int r = g_bng_init();
    BNG_LOG("bngrw: Init -> %d", r);
    return r;
}
int __stdcall rg_BngRwFin() {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        g_bng.fin();
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: Fin (fake)");
        return 0;
    }
    const int r = g_bng_fin();
    BNG_LOG("bngrw: Fin -> %d", r);
    return r;
}
int __stdcall rg_BngRwAttach(int reader, const char* port, int a3, int a4, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.attach(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: Attach(reader %d, %s, %d, 0x%x) -> %d (fake)", reader, port ? port : "?", a3, a4, r);
        if (r == rg_bngrw::kAccepted) bng_done(cb, user, reader);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, "Attach");
    const int r = g_bng_attach(reader, port, a3, a4, cb ? reinterpret_cast<void*>(&bng_thunk3) : nullptr, t);
    BNG_LOG("bngrw: Attach(reader %d, %s, %d, 0x%x) -> %d", reader, port ? port : "?", a3, a4, r);
    return r;
}
int __stdcall rg_BngRwIsCmdExec(int reader) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.is_cmd_exec(reader);
        LeaveCriticalSection(&g_bng_cs);
        return r;
    }
    const int r = g_bng_is_exec(reader);
    static LONG last[4] = {-2, -2, -2, -2};   // only log changes: the game asks for this every tick
    if (reader >= 0 && reader < 4 && InterlockedExchange(&last[reader], r) != r) BNG_LOG("bngrw: IsCmdExec(reader %d) -> %d", reader, r);
    return r;
}
int __stdcall rg_BngRwSetLedPower(int a, int b, int c) {
    if (g_bng_fake) {
        BNG_LOG("bngrw: SetLedPower(%d, %d, %d) (fake)", a, b, c);
        return 0;
    }
    const int r = g_bng_led_power(a, b, c);
    BNG_LOG("bngrw: SetLedPower(%d, %d, %d) -> %d", a, b, c, r);
    return r;
}
int __stdcall rg_BngRwDevReset(int reader, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.dev_reset(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: DevReset(reader %d) -> %d (fake)", reader, r);
        if (r == rg_bngrw::kAccepted) bng_done(cb, user, reader);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, "DevReset");
    const int r = g_bng_dev_reset(reader, cb ? reinterpret_cast<void*>(&bng_thunk3) : nullptr, t);
    BNG_LOG("bngrw: DevReset(reader %d) -> %d", reader, r);
    return r;
}
int bng_req4(const char* name, BngReq4Fn real, int reader, int v, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.simple_request(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: %s(reader %d, %d) -> %d (fake)", name, reader, v, r);
        if (r == rg_bngrw::kAccepted) bng_done(cb, user, reader);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, name);
    const int r = real(reader, v, cb ? reinterpret_cast<void*>(&bng_thunk3) : nullptr, t);
    BNG_LOG("bngrw: %s(reader %d, %d) -> %d", name, reader, v, r);
    return r;
}
int __stdcall rg_BngRwReqLed(int reader, int v, void* cb, void* user) { return bng_req4("ReqLed", g_bng_req_led, reader, v, cb, user); }
int __stdcall rg_BngRwReqBeep(int reader, int v, void* cb, void* user) { return bng_req4("ReqBeep", g_bng_req_beep, reader, v, cb, user); }
int __stdcall rg_BngRwReqWaitTouch(int reader, int timeout, int mode, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.wait_touch(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: ReqWaitTouch(reader %d, %d, 0x%x) -> %d (fake, no card)", reader, timeout, mode, r);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, "WaitTouch");
    const int r = g_bng_wait_touch(reader, timeout, mode, cb ? reinterpret_cast<void*>(&bng_thunk4) : nullptr, t);
    BNG_LOG("bngrw: ReqWaitTouch(reader %d, %d, 0x%x) -> %d", reader, timeout, mode, r);
    return r;
}
int __stdcall rg_BngRwReqCancel(int reader) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const bool was = g_bng.cancel(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: ReqCancel(reader %d) -> %d (fake)", reader, was ? 1 : 0);
        return rg_bngrw::kAccepted;
    }
    const int r = g_bng_cancel(reader);
    BNG_LOG("bngrw: ReqCancel(reader %d) -> %d", reader, r);
    return r;
}
int __stdcall rg_BngRwReqSendUrlTo(int reader, int a2, int a3, void* a4, void* a5, const char* url, void* cb, void* user) {
    if (g_bng_fake) {
        EnterCriticalSection(&g_bng_cs);
        const int r = g_bng.simple_request(reader);
        LeaveCriticalSection(&g_bng_cs);
        BNG_LOG("bngrw: ReqSendUrlTo(reader %d, %s) -> %d (fake)", reader, url ? url : "?", r);
        if (r == rg_bngrw::kAccepted) bng_done(cb, user, reader);
        return r;
    }
    BngThunk* t = bng_thunk(cb, user, reader, "SendUrlTo");
    const int r = g_bng_send_url(reader, a2, a3, a4, a5, url, cb ? reinterpret_cast<void*>(&bng_thunk3) : nullptr, t);
    BNG_LOG("bngrw: ReqSendUrlTo(reader %d, %s) -> %d", reader, url ? url : "?", r);
    return r;
}

template <typename Fn>
void hook_bng(const char* name, void* repl, Fn& orig) {
    void* old = patch_iat(GetModuleHandleW(nullptr), "bngrw.dll", name, repl);
    if (old) orig = reinterpret_cast<Fn>(old);
    rg_log("import table: bngrw.dll!%s %s", name, old ? "redirected (card reader)" : "not in the game");
}

void setup_bngrw() {
    char v[16];
    if (GetEnvironmentVariableA("RG_BNGRW", v, sizeof v) == 0 || !v[0]) return;   // off
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("card reader wrapper off: the TeknoParrot core is loaded");
        return;
    }
    g_bng_fake = std::strcmp(v, "fake") == 0;
    if (!g_bng_fake && std::strcmp(v, "trace") != 0) {
        rg_log("RG_BNGRW=%s unknown (choose trace or fake); off", v);
        return;
    }
    InitializeCriticalSection(&g_bng_cs);
    rg_log("card reader: %s", g_bng_fake ? "own reply (two readers without a card)" : "following the real bngrw.dll");
    hook_bng("_BngRwInit@0", reinterpret_cast<void*>(&rg_BngRwInit), g_bng_init);
    hook_bng("_BngRwFin@0", reinterpret_cast<void*>(&rg_BngRwFin), g_bng_fin);
    hook_bng("_BngRwAttach@24", reinterpret_cast<void*>(&rg_BngRwAttach), g_bng_attach);
    hook_bng("_BngRwIsCmdExec@4", reinterpret_cast<void*>(&rg_BngRwIsCmdExec), g_bng_is_exec);
    hook_bng("_BngRwSetLedPower@12", reinterpret_cast<void*>(&rg_BngRwSetLedPower), g_bng_led_power);
    hook_bng("_BngRwDevReset@12", reinterpret_cast<void*>(&rg_BngRwDevReset), g_bng_dev_reset);
    hook_bng("_BngRwReqLed@16", reinterpret_cast<void*>(&rg_BngRwReqLed), g_bng_req_led);
    hook_bng("_BngRwReqBeep@16", reinterpret_cast<void*>(&rg_BngRwReqBeep), g_bng_req_beep);
    hook_bng("_BngRwReqWaitTouch@20", reinterpret_cast<void*>(&rg_BngRwReqWaitTouch), g_bng_wait_touch);
    hook_bng("_BngRwReqCancel@4", reinterpret_cast<void*>(&rg_BngRwReqCancel), g_bng_cancel);
    hook_bng("_BngRwReqSendUrlTo@32", reinterpret_cast<void*>(&rg_BngRwReqSendUrlTo), g_bng_send_url);
}

// --- following the message loop and windows (see EIGHTH TASK at the top) -----------------------------------------------------------------------------------
volatile LONG g_peek_n = 0, g_peek_msgs = 0, g_disp_n = 0;
volatile LONG g_peek_slow = 0;
DWORD g_peek_max_ms = 0;
DWORD g_peek_last_sum = 0;
DWORD g_peek_delay_ms = 0;       // RG_PEEK_DELAY_MS: delay the first PeekMessageW by this many ms (experiment: a race with the window?)
bool g_peek_skip_first = false;  // RG_PEEK_SKIP_FIRST=1: do not execute the first PeekMessageW (PM_NOREMOVE) (experiment)

using PeekMessageWFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using DispatchMessageWFn = LRESULT(WINAPI*)(const MSG*);
using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using CreateWindowExWFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
PeekMessageWFn g_peek_w = nullptr;
DispatchMessageWFn g_dispatch_w = nullptr;
ShowWindowFn g_show_window = nullptr;
SetWindowPosFn g_set_window_pos = nullptr;
CreateWindowExWFn g_create_window_ex_w = nullptr;

BOOL CALLBACK enum_thread_window(HWND h, LPARAM) {
    wchar_t cls[80] = {};
    GetClassNameW(h, cls, 79);
    RECT r = {};
    GetWindowRect(h, &r);
    rg_log("  window %p class %s visible %d iconic %d rect %ld,%ld %ldx%ld", h, to_utf8(cls).c_str(), IsWindowVisible(h) ? 1 : 0, IsIconic(h) ? 1 : 0,
           static_cast<long>(r.left), static_cast<long>(r.top), static_cast<long>(r.right - r.left), static_cast<long>(r.bottom - r.top));
    return TRUE;
}

void window_state(const char* why) {
    rg_log("windows of this thread (%s): foreground %p, active %p, focus %p", why, GetForegroundWindow(), GetActiveWindow(), GetFocus());
    EnumThreadWindows(GetCurrentThreadId(), enum_thread_window, 0);
}

BOOL WINAPI rg_PeekMessageW(LPMSG m, HWND h, UINT a, UINT b, UINT f) {
    const LONG n = InterlockedIncrement(&g_peek_n);
    if (n <= 5) {
        rg_log("PeekMessageW #%ld: entered (hwnd %p, %u..%u, flags 0x%x)", static_cast<long>(n), h, a, b, f);
        if (n == 1) window_state("first PeekMessageW");
    }
    if (n == 1 && g_peek_delay_ms) {
        rg_log("PeekMessageW #1: delayed %lu ms (RG_PEEK_DELAY_MS)", static_cast<unsigned long>(g_peek_delay_ms));
        Sleep(g_peek_delay_ms);
    }
    if (n == 1 && g_peek_skip_first && f == 0) {   // the game's first PeekMessageW(PM_NOREMOVE) (0x83D7D4) only creates the message queue; msg stays empty
        rg_log("PeekMessageW #1 skipped (RG_PEEK_SKIP_FIRST): FALSE without the real call");
        if (m) std::memset(m, 0, sizeof *m);
        return FALSE;
    }
    const DWORD t0 = GetTickCount();
    const BOOL r = g_peek_w(m, h, a, b, f);
    const DWORD dt = GetTickCount() - t0;
    if (n <= 5) rg_log("PeekMessageW #%ld: returned %d after %lu ms", static_cast<long>(n), r ? 1 : 0, static_cast<unsigned long>(dt));
    if (dt >= 200 && InterlockedIncrement(&g_peek_slow) <= 20) rg_log("PeekMessageW #%ld took %lu ms", static_cast<long>(n), static_cast<unsigned long>(dt));
    if (dt > g_peek_max_ms) g_peek_max_ms = dt;
    if (r) {
        const LONG k = InterlockedIncrement(&g_peek_msgs);
        if (k <= 40 && m) rg_log("message #%ld: hwnd %p msg 0x%04x wParam 0x%lx lParam 0x%lx", static_cast<long>(k), m->hwnd, m->message, static_cast<unsigned long>(m->wParam),
                                 static_cast<unsigned long>(m->lParam));
    }
    const DWORD now = GetTickCount();
    if (now - g_peek_last_sum >= 5000) {   // a summary every 5 s (only if the loop is running: it does not come during a hang)
        g_peek_last_sum = now;
        rg_log("message loop: %ld calls, %ld messages, longest call %lu ms", static_cast<long>(g_peek_n), static_cast<long>(g_peek_msgs), static_cast<unsigned long>(g_peek_max_ms));
    }
    return r;
}
LRESULT WINAPI rg_DispatchMessageW(const MSG* m) {
    const LONG n = InterlockedIncrement(&g_disp_n);
    if (n <= 5 && m) rg_log("DispatchMessageW #%ld: hwnd %p msg 0x%04x", static_cast<long>(n), m->hwnd, m->message);
    return g_dispatch_w(m);
}
BOOL WINAPI rg_ShowWindow(HWND h, int cmd) {
    const BOOL r = g_show_window(h, cmd);
    rg_log("ShowWindow(%p, %d) -> %d", h, cmd, r ? 1 : 0);
    return r;
}
BOOL WINAPI rg_SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT fl) {
    const BOOL r = g_set_window_pos(h, after, x, y, cx, cy, fl);
    static volatile LONG k = 0;
    if (InterlockedIncrement(&k) <= 20) rg_log("SetWindowPos(%p, %p, %d,%d %dx%d, 0x%x) -> %d", h, after, x, y, cx, cy, fl, r ? 1 : 0);
    return r;
}
HWND WINAPI rg_CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style, int x, int y, int w, int hh, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p) {
    HWND h = g_create_window_ex_w(ex, cls, name, style, x, y, w, hh, parent, menu, inst, p);
    rg_log("CreateWindowExW(ex 0x%lx, %s, %s, style 0x%lx, %d,%d %dx%d) -> %p", static_cast<unsigned long>(ex), (reinterpret_cast<uintptr_t>(cls) > 0xFFFF) ? to_utf8(cls).c_str() : "(atom)",
           name ? to_utf8(name).c_str() : "", static_cast<unsigned long>(style), x, y, w, hh, h);
    return h;
}

void setup_msg_trace() {
    char v[8];
    if (GetEnvironmentVariableA("RG_MSGTRACE", v, sizeof v) == 0 || v[0] != '1') return;   // off
    char pv[16];
    if (GetEnvironmentVariableA("RG_PEEK_DELAY_MS", pv, sizeof pv) > 0) g_peek_delay_ms = static_cast<DWORD>(std::strtoul(pv, nullptr, 10));
    if (GetEnvironmentVariableA("RG_PEEK_SKIP_FIRST", pv, sizeof pv) > 0 && pv[0] == '1') g_peek_skip_first = true;
    rg_log("message loop tracing on (delay %lu ms, skip first %d)", static_cast<unsigned long>(g_peek_delay_ms), g_peek_skip_first ? 1 : 0);
    hook_import("USER32.dll", "PeekMessageW", reinterpret_cast<void*>(&rg_PeekMessageW), g_peek_w);
    hook_import("USER32.dll", "DispatchMessageW", reinterpret_cast<void*>(&rg_DispatchMessageW), g_dispatch_w);
    hook_import("USER32.dll", "ShowWindow", reinterpret_cast<void*>(&rg_ShowWindow), g_show_window);
    hook_import("USER32.dll", "SetWindowPos", reinterpret_cast<void*>(&rg_SetWindowPos), g_set_window_pos);
    hook_import("USER32.dll", "CreateWindowExW", reinterpret_cast<void*>(&rg_CreateWindowExW), g_create_window_ex_w);
}

// TeknoParrot style: patch the game's own F:/ strings (drive_fix.h, patch_strings) in the data sections of the exe.
void patch_drive_strings() {
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    size_t total_n = 0, total_w = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;   // not code
        BYTE* start = exe + sec[i].VirtualAddress;
        const DWORD size = sec[i].Misc.VirtualSize;
        DWORD old = 0;
        if (!size || !VirtualProtect(start, size, PAGE_READWRITE, &old)) continue;
        const size_t n = rg_drivefix::patch_strings(start, size, g_drive_letters, 1);
        const size_t w = rg_drivefix::patch_strings(start, size, g_drive_letters, 2);
        VirtualProtect(start, size, old, &old);
        char name[9] = {};
        std::memcpy(name, sec[i].Name, 8);
        rg_log("drive letter: strings in %s: %lu narrow, %lu wide patched (<L>:/ -> ./<L>, like TeknoParrot)", name, static_cast<unsigned long>(n), static_cast<unsigned long>(w));
        total_n += n;
        total_w += w;
    }
    rg_log("drive letter: %lu narrow and %lu wide strings patched", static_cast<unsigned long>(total_n), static_cast<unsigned long>(total_w));
}

// --- window mode like TeknoParrot Windowed=1 (see NINTH TASK at the top) ----------------------------------------------------------------
void setup_windowed() {
    char v[8];
    if (GetEnvironmentVariableA("RG_WINDOWED", v, sizeof v) == 0 || v[0] != '1') return;   // off
    if (GetModuleHandleA("TeknoParrot.dll") || GetModuleHandleA("TeknoParrot64.dll")) {
        rg_log("window mode off: the TeknoParrot core is loaded (it does Windowed=1 itself)");
        return;
    }
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;   // code only
        BYTE* start = exe + sec[i].VirtualAddress;
        const DWORD size = sec[i].Misc.VirtualSize;
        size_t first = 0;
        const size_t hits = rg_winmode::find_pattern(start, size, first);
        if (hits == 0) continue;
        if (hits != 1) {
            rg_log("window mode: pattern found %lu times, nothing patched (exactly 1 expected)", static_cast<unsigned long>(hits));
            return;
        }
        BYTE* imm = start + first + rg_winmode::kImmOffset;
        DWORD old = 0;
        if (!VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old)) {
            rg_log("window mode: VirtualProtect failed (error %lu), nothing patched", static_cast<unsigned long>(GetLastError()));
            return;
        }
        const DWORD zero = 0;
        std::memcpy(imm, &zero, 4);
        VirtualProtect(imm, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), imm, 4);
        const DWORD at = static_cast<DWORD>(nt->OptionalHeader.ImageBase + (imm - 3 - exe));
        rg_log("window mode on: mov [ebx+0x30],1 -> 0 at VA 0x%lx (DXUT gets -windowed, no mode switch; like TeknoParrot Windowed=1)",
               static_cast<unsigned long>(at));
        return;
    }
    rg_log("window mode: pattern not found in the exe, nothing patched (other version of the game?)");
}

// --- the stalldump (see FIFTH TASK at the top) -------------------------------------------------------------------------------------------------
#if defined(__i386__)
// Where is `a`? In the exe: "exe+0xRVA (VA 0x...)", otherwise "module+0xoffset".
void where_text(DWORD a, char* out, size_t n) {
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const DWORD base = reinterpret_cast<DWORD>(exe), size = nt->OptionalHeader.SizeOfImage;
    if (a >= base && a < base + size) {
        std::snprintf(out, n, "exe+0x%lx (VA 0x%lx)", static_cast<unsigned long>(a - base), static_cast<unsigned long>(nt->OptionalHeader.ImageBase + (a - base)));
        return;
    }
    HMODULE m = nullptr;
    char name[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(a)), &m) &&
        GetModuleFileNameA(m, name, MAX_PATH)) {
        const char* b = std::strrchr(name, '\\');
        std::snprintf(out, n, "%s+0x%lx", b ? b + 1 : name, static_cast<unsigned long>(a - reinterpret_cast<DWORD>(m)));
    } else {
        std::snprintf(out, n, "0x%lx", static_cast<unsigned long>(a));
    }
}

bool read_self(DWORD addr, void* buf, SIZE_T n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(addr)), buf, n, &got) && got == n;
}

void stall_dump_once(unsigned secs) {
    rg_log("stalldump after %u s:", secs);
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const DWORD base = reinterpret_cast<DWORD>(exe), size = nt->OptionalHeader.SizeOfImage;
    const DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        rg_log("  no thread list (error %lu)", static_cast<unsigned long>(GetLastError()));
        return;
    }
    THREADENTRY32 te = {};
    te.dwSize = sizeof te;
    int threads = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        ++threads;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) {
            rg_log("  thread %lu: could not be opened (error %lu)", static_cast<unsigned long>(te.th32ThreadID), static_cast<unsigned long>(GetLastError()));
            continue;
        }
        if (SuspendThread(h) == static_cast<DWORD>(-1)) {
            rg_log("  thread %lu: SuspendThread failed (error %lu)", static_cast<unsigned long>(te.th32ThreadID), static_cast<unsigned long>(GetLastError()));
            CloseHandle(h);
            continue;
        }
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(h, &c)) {
            char w[160];
            where_text(c.Eip, w, sizeof w);
            rg_log("  thread %lu: EIP %s, ESP 0x%lx, EBP 0x%lx", static_cast<unsigned long>(te.th32ThreadID), w, static_cast<unsigned long>(c.Esp),
                   static_cast<unsigned long>(c.Ebp));
            // The stack (ESP..+4 KiB): values in the exe that are preceded by a call. At most 14 per thread.
            BYTE stack[4096];
            // In pieces of 32 bytes: close to the top of the stack a single large read would fail.
            SIZE_T got = 0;
            while (got + 32 <= sizeof stack && read_self(c.Esp + static_cast<DWORD>(got), stack + got, 32)) got += 32;
            if (got >= 4) {
                int shown = 0, shown_other = 0;
                for (SIZE_T o = 0; o + 4 <= got && (shown < 14 || shown_other < 16); o += 4) {
                    DWORD v;
                    std::memcpy(&v, stack + o, 4);
                    const bool in_exe = v >= base + 8 && v < base + size;
                    if (in_exe ? shown >= 14 : shown_other >= 16) continue;
                    if (!in_exe) {
                        // other modules (user32, win32u, winex11 bridge, dinput8, d3d11, ...): only real code in a loaded image
                        if (v < 0x10000 || v >= 0x7ff00000) continue;
                        MEMORY_BASIC_INFORMATION mbi = {};
                        if (!VirtualQuery(reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(v)), &mbi, sizeof mbi) || mbi.Type != MEM_IMAGE) continue;
                        if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) continue;
                    }
                    BYTE before[8];
                    if (!read_self(v - 8, before, 8)) continue;
                    if (!rg_stall::preceded_by_call(before + 8)) continue;   // reads p[-6]..p[-1]: the 8 bytes just before v
                    where_text(v, w, sizeof w);
                    rg_log("      [ESP+0x%lx] %s%s", static_cast<unsigned long>(o), in_exe ? "" : "(module) ", w);
                    if (in_exe) ++shown; else ++shown_other;
                }
            }
        } else {
            rg_log("  thread %lu: GetThreadContext failed (error %lu)", static_cast<unsigned long>(te.th32ThreadID), static_cast<unsigned long>(GetLastError()));
        }
        ResumeThread(h);
        CloseHandle(h);
    }
    CloseHandle(snap);
    rg_log("  %d threads inspected", threads);
}

DWORD WINAPI stall_thread(LPVOID) {
    char spec[128];
    if (GetEnvironmentVariableA("RG_STALL_DUMP", spec, sizeof spec) == 0) return 0;
    const DWORD t0 = GetTickCount();
    for (unsigned secs : rg_stall::parse_times(spec)) {
        for (;;) {
            const DWORD el = GetTickCount() - t0;
            if (el >= secs * 1000u) break;
            Sleep(static_cast<DWORD>(secs * 1000u - el > 500 ? 500 : secs * 1000u - el));
        }
        stall_dump_once(secs);
    }
    return 0;
}

#endif  // __i386__

void setup_stall_dump() {
    char spec[128];
    if (GetEnvironmentVariableA("RG_STALL_DUMP", spec, sizeof spec) == 0 || !spec[0]) return;
#if !defined(__i386__)
    rg_log("stalldump: RG_STALL_DUMP=%s ignored, only in the 32-bit build", spec);
    return;
#else
    HANDLE t = CreateThread(nullptr, 0, &stall_thread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    rg_log("stalldump on: %s s after load", spec);
#endif
}

// --- image dump (RG_IMAGE_DUMP=40,90) -----------------------------------------------------------
// Writes the game's whole loaded image (SizeOfImage bytes from the image base) to
// rg-image-<secs>s.bin next to the log, plus every loaded module with its base and size in the log.
// Diffing that file against the exe on disk shows exactly which code bytes and import slots were
// changed at runtime (by our own drop-ins, or by another core in a comparison run). Unreadable pages
// are written as zeros and listed in the log. Off unless RG_IMAGE_DUMP is set.
void image_dump_once(unsigned secs) {
    auto* exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(exe + reinterpret_cast<IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    const SIZE_T size = nt->OptionalHeader.SizeOfImage;
    wchar_t path[MAX_PATH];
    lstrcpynW(path, g_log_path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    wchar_t name[64];
    wsprintfW(name, L"rg-image-%us.bin", secs);
    if (slash) lstrcpynW(slash + 1, name, static_cast<int>(MAX_PATH - (slash + 1 - path)));
    else lstrcpynW(path, name, MAX_PATH);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        rg_log("image dump at %u s: cannot create file (error %lu)", secs, static_cast<unsigned long>(GetLastError()));
        return;
    }
    static BYTE page[0x1000];
    unsigned unreadable = 0;
    for (SIZE_T off = 0; off < size; off += sizeof page) {
        const SIZE_T n = size - off < sizeof page ? size - off : sizeof page;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), exe + off, page, n, &got) || got != n) {
            std::memset(page, 0, n);
            ++unreadable;
        }
        DWORD wrote = 0;
        WriteFile(f, page, static_cast<DWORD>(n), &wrote, nullptr);
    }
    CloseHandle(f);
    rg_log("image dump at %u s: base %p, %lu bytes, %u unreadable pages", secs, static_cast<void*>(exe),
           static_cast<unsigned long>(size), unreadable);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me = {};
    me.dwSize = sizeof me;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        char mod[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, mod, MAX_PATH, nullptr, nullptr);
        rg_log("  module %p %08lx %s", static_cast<void*>(me.modBaseAddr), static_cast<unsigned long>(me.modBaseSize), mod);
    }
    CloseHandle(snap);
}

DWORD WINAPI image_dump_thread(LPVOID) {
    char spec[128];
    if (GetEnvironmentVariableA("RG_IMAGE_DUMP", spec, sizeof spec) == 0) return 0;
    const DWORD t0 = GetTickCount();
    for (unsigned secs : rg_stall::parse_times(spec)) {
        const DWORD due = secs * 1000u;
        while (GetTickCount() - t0 < due) Sleep(500);
        image_dump_once(secs);
    }
    return 0;
}

void setup_image_dump() {
    char spec[128];
    if (GetEnvironmentVariableA("RG_IMAGE_DUMP", spec, sizeof spec) == 0 || !spec[0]) return;
    HANDLE t = CreateThread(nullptr, 0, &image_dump_thread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    rg_log("image dump on: %s s after load", spec);
}

// --- the child process (see THIRTEENTH TASK at the top) ----------------------------------------------------------------------------------
// RG_CHILD_PRELOAD=<exe name>: when this process starts that exe with CreateProcessW, the child is created suspended, this DLL is
// loaded into it (a remote thread on LoadLibraryW, the same way the OpenParrot loader loads its core) and the child then continues.
// The child inherits the environment, so the parts switched on there (RG_DOMEGUN_COMPORT) work in it.
using CreateProcessWFn = BOOL(WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR,
                                       LPSTARTUPINFOW, LPPROCESS_INFORMATION);
CreateProcessWFn g_create_process_w = nullptr;
wchar_t g_child_exe[MAX_PATH];

bool child_matches(LPCWSTR app, LPCWSTR cmd) {
    for (LPCWSTR s : {app, static_cast<LPCWSTR>(cmd)}) {
        if (!s) continue;
        std::wstring low(s);
        for (auto& c : low) c = static_cast<wchar_t>(towlower(c));
        std::wstring want(g_child_exe);
        for (auto& c : want) c = static_cast<wchar_t>(towlower(c));
        if (low.find(want) != std::wstring::npos) return true;
    }
    return false;
}

bool inject_self(HANDLE proc) {
    wchar_t self[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(g_self, self, MAX_PATH);
    if (!len) return false;
    const SIZE_T bytes = (len + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;
    SIZE_T put = 0;
    bool ok = false;
    if (WriteProcessMemory(proc, remote, self, bytes, &put) && put == bytes) {
        auto* load = reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
        HANDLE t = load ? CreateRemoteThread(proc, nullptr, 0, load, remote, 0, nullptr) : nullptr;
        if (t) {
            const DWORD w = WaitForSingleObject(t, 15000);
            DWORD code = 0;
            GetExitCodeThread(t, &code);
            ok = w == WAIT_OBJECT_0 && code != 0;
            rg_log("child: LoadLibraryW in the child %s (wait %lu, low half of the module 0x%lx)", ok ? "done" : "FAILED",
                   static_cast<unsigned long>(w), static_cast<unsigned long>(code));
            CloseHandle(t);
        } else {
            rg_log("child: CreateRemoteThread failed (error %lu)", static_cast<unsigned long>(GetLastError()));
        }
    }
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    return ok;
}

BOOL WINAPI rg_CreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags,
                              LPVOID env, LPCWSTR dir, LPSTARTUPINFOW si, LPPROCESS_INFORMATION pi) {
    if (!child_matches(app, cmd) || !pi) return g_create_process_w(app, cmd, pa, ta, inherit, flags, env, dir, si, pi);
    const BOOL r = g_create_process_w(app, cmd, pa, ta, inherit, flags | CREATE_SUSPENDED, env, dir, si, pi);
    if (!r) return r;
    const std::string c8 = cmd ? to_utf8(cmd) : std::string();
    rg_log("child: started pid %lu suspended: %s", static_cast<unsigned long>(pi->dwProcessId), c8.c_str());
    inject_self(pi->hProcess);
    if (!(flags & CREATE_SUSPENDED)) ResumeThread(pi->hThread);
    return r;
}

void setup_child_preload() {
    if (GetEnvironmentVariableW(L"RG_CHILD_PRELOAD", g_child_exe, MAX_PATH) == 0 || !g_child_exe[0]) return;   // off
    void* old = patch_iat(GetModuleHandleW(nullptr), "KERNEL32.dll", "CreateProcessW", reinterpret_cast<void*>(&rg_CreateProcessW));
    if (old) g_create_process_w = reinterpret_cast<CreateProcessWFn>(old);
    const std::string e8 = to_utf8(g_child_exe);
    rg_log("child preload %s: %s", e8.c_str(), old ? "CreateProcessW redirected" : "CreateProcessW not in this exe, off");
}

// --- the gun peripheral on a COM port (see THIRTEENTH TASK at the top) -------------------------------------------------------------------
// RG_DOMEGUN_COMPORT=COM2: CreateFileW on that port gives a handle of our own (an event, so that CloseHandle stays valid), and the
// game's ReadFile/WriteFile/ClearCommError on it are answered from the shared memory TeknoParrot_JvsState (dome_gun.h).
char g_gun_port[32];
CRITICAL_SECTION g_gun_cs;
rg_domegun::Port g_gun;
HANDLE g_gun_map = nullptr;
const uint8_t* g_gun_shm = nullptr;
LONG g_gun_frames = 0;
int g_gun_feedback[2] = {-1, -1};
uint32_t g_gun_mask_rva = 0;   // RG_DOMEGUN_MASKS=<hex rva>: log the game's two button masks when they change
uint32_t g_gun_masks[2] = {~0u, ~0u};

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using ClearCommErrorFn = BOOL(WINAPI*)(HANDLE, LPDWORD, LPCOMSTAT);
using ReadFileFn = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using WriteFileFn = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);
CreateFileWFn g_create_file_w = nullptr;
ClearCommErrorFn g_clear_comm_error = nullptr;
ReadFileFn g_read_file = nullptr;
WriteFileFn g_write_file = nullptr;
CloseHandleFn g_close_handle = nullptr;

HANDLE WINAPI rg_CreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    const std::string n8 = name ? to_utf8(name) : std::string();
    if (!g_gun_port[0] || !rg_jvscom::port_matches(n8.c_str(), g_gun_port))
        return g_create_file_w(name, access, share, sa, disp, flags, tmpl);
    HANDLE h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    EnterCriticalSection(&g_gun_cs);
    g_gun = rg_domegun::Port{};
    if (!g_gun_shm) {
        g_gun_map = OpenFileMappingA(FILE_MAP_READ, FALSE, "TeknoParrot_JvsState");
        if (g_gun_map) g_gun_shm = static_cast<const uint8_t*>(MapViewOfFile(g_gun_map, FILE_MAP_READ, 0, 0, 64));
    }
    g_gun_handle = h;
    LeaveCriticalSection(&g_gun_cs);
    rg_log("gun: CreateFileW(%s) -> our gun peripheral, handle %p; shared memory TeknoParrot_JvsState %s", n8.c_str(), h,
           g_gun_shm ? "open" : "NOT there (centre, no buttons)");
    return h;
}

void gun_note_masks() {
    if (!g_gun_mask_rva) return;
    const auto* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    uint32_t m[2];
    std::memcpy(m, base + g_gun_mask_rva, sizeof m);
    if (m[0] != g_gun_masks[0] || m[1] != g_gun_masks[1]) {
        g_gun_masks[0] = m[0];
        g_gun_masks[1] = m[1];
        rg_log("gun: button masks now 0x%lx / 0x%lx", static_cast<unsigned long>(m[0]), static_cast<unsigned long>(m[1]));
    }
}

BOOL WINAPI rg_ClearCommError(HANDLE h, LPDWORD errors, LPCOMSTAT st) {
    if (!is_gun(h)) return g_clear_comm_error(h, errors, st);
    EnterCriticalSection(&g_gun_cs);
    g_gun.refill(GetTickCount(), 8, g_gun_shm);
    const DWORD n = static_cast<DWORD>(g_gun.n);
    LeaveCriticalSection(&g_gun_cs);
    if (errors) *errors = 0;
    if (st) {
        std::memset(st, 0, sizeof *st);
        st->cbInQue = n;
    }
    gun_note_masks();
    return TRUE;
}

BOOL WINAPI rg_ReadFile(HANDLE h, LPVOID buf, DWORD want, LPDWORD got, LPOVERLAPPED ov) {
    if (!is_gun(h)) return g_read_file(h, buf, want, got, ov);
    EnterCriticalSection(&g_gun_cs);
    const size_t k = g_gun.read(static_cast<uint8_t*>(buf), want);
    LeaveCriticalSection(&g_gun_cs);
    if (got) *got = static_cast<DWORD>(k);
    if (k >= 11) {
        const LONG f = InterlockedIncrement(&g_gun_frames);
        if (f == 1 || f % 3600 == 0) rg_log("gun: frame %ld read by the game", static_cast<long>(f));
    }
    return TRUE;
}

BOOL WINAPI rg_WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD put, LPOVERLAPPED ov) {
    if (!is_gun(h)) return g_write_file(h, buf, n, put, ov);
    const auto* b = static_cast<const uint8_t*>(buf);
    for (DWORD i = 0; i < n && i < 2; ++i) {
        const int lvl = rg_domegun::feedback_level(b[i], static_cast<int>(i));
        if (lvl != g_gun_feedback[i]) {
            g_gun_feedback[i] = lvl;
            rg_log("gun: feedback player %lu -> %d ('%c')", static_cast<unsigned long>(i + 1), lvl, b[i] >= 32 && b[i] < 127 ? b[i] : '?');
        }
    }
    if (put) *put = n;
    return TRUE;
}

BOOL WINAPI rg_CloseHandle(HANDLE h) {
    if (is_gun(h)) {
        g_gun_handle = INVALID_HANDLE_VALUE;
        rg_log("gun: the game closed the port");
    }
    return g_close_handle(h);
}

void setup_dome_gun() {
    char port[32];
    if (GetEnvironmentVariableA("RG_DOMEGUN_COMPORT", port, sizeof port) == 0 || !port[0]) return;   // off
    HMODULE game = GetModuleHandleW(nullptr);
    // Only where the game itself opens the port with CreateFileW (not in the launcher, which inherits the setting).
    void* old = patch_iat(game, "KERNEL32.dll", "CreateFileW", reinterpret_cast<void*>(&rg_CreateFileW));
    if (!old) {
        rg_log("gun peripheral %s: CreateFileW not in this exe, off", port);
        return;
    }
    // Only the exe that uses ClearCommError on its port; another exe gets its CreateFileW back.
    void* clear = patch_iat(game, "KERNEL32.dll", "ClearCommError", reinterpret_cast<void*>(&rg_ClearCommError));
    if (!clear) {
        patch_iat(game, "KERNEL32.dll", "CreateFileW", old);
        rg_log("gun peripheral %s: ClearCommError not in this exe, off", port);
        return;
    }
    InitializeCriticalSection(&g_gun_cs);
    g_create_file_w = reinterpret_cast<CreateFileWFn>(old);
    g_clear_comm_error = reinterpret_cast<ClearCommErrorFn>(clear);
    std::snprintf(g_gun_port, sizeof g_gun_port, "%s", port);
    hook_kernel32("ReadFile", &rg_ReadFile, g_read_file);
    hook_kernel32("WriteFile", &rg_WriteFile, g_write_file);
    hook_kernel32("CloseHandle", &rg_CloseHandle, g_close_handle);
    hook_comm_once();   // GetCommState/SetCommState on our handle succeed
    char rva[32];
    if (GetEnvironmentVariableA("RG_DOMEGUN_MASKS", rva, sizeof rva) > 0 && rva[0]) {
        const auto* base = reinterpret_cast<const BYTE*>(game);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        const unsigned long v = std::strtoul(rva, nullptr, 16);
        if (v && v + 8 <= nt->OptionalHeader.SizeOfImage) g_gun_mask_rva = static_cast<uint32_t>(v);
    }
    rg_log("gun peripheral on: %s answered from TeknoParrot_JvsState (12-byte frames, both players)%s", g_gun_port,
           g_gun_mask_rva ? ", button masks logged" : "");
}

// --- the error set of a game (RG_ERRMAP) -------------------------------------------------------------------------------------------------
// RG_ERRMAP=<hex rva>:<hex offset>: the exe keeps a pointer to an object at that RVA, with an MSVC 2010 std::map<int, ...> at that
// offset (map+8 = head node; node: left 0, parent 8, right 0x10, key 0x18, isnil byte 0x29). Every 2 s the keys are logged when they
// changed. For Lost Land's launcher: 4098e0:38 is its error list (RSLauncher 0x140035190 raises an error by adding its code there,
// 0x1400362a0 is the map's find), so the log names the errors that are up, e.g. 40 = "08-01 AUTH CAMERA ERROR".
uintptr_t g_errmap_rva = 0, g_errmap_off = 0;

bool safe_read(const void* p, void* out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), p, out, n, &got) && got == n;
}

void errmap_walk(uintptr_t node, uintptr_t head, std::vector<int>& keys, int depth) {
    if (!node || node == head || depth > 64 || keys.size() > 256) return;
    uint8_t isnil = 1;
    if (!safe_read(reinterpret_cast<void*>(node + 0x29), &isnil, 1) || isnil) return;
    uintptr_t left = 0, right = 0;
    int key = 0;
    safe_read(reinterpret_cast<void*>(node), &left, sizeof left);
    safe_read(reinterpret_cast<void*>(node + 0x10), &right, sizeof right);
    safe_read(reinterpret_cast<void*>(node + 0x18), &key, sizeof key);
    errmap_walk(left, head, keys, depth + 1);
    keys.push_back(key);
    errmap_walk(right, head, keys, depth + 1);
}

DWORD WINAPI errmap_thread(LPVOID) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::string last = "-";
    for (;;) {
        Sleep(2000);
        uintptr_t obj = 0, head = 0, root = 0;
        std::string now;
        if (!safe_read(reinterpret_cast<void*>(base + g_errmap_rva), &obj, sizeof obj) || !obj) now = "(no object yet)";
        else if (!safe_read(reinterpret_cast<void*>(obj + g_errmap_off + 8), &head, sizeof head) || !head ||
                 !safe_read(reinterpret_cast<void*>(head + 8), &root, sizeof root))
            now = "(map not readable)";
        else {
            std::vector<int> keys;
            errmap_walk(root, head, keys, 0);
            now = keys.empty() ? "none" : "";
            for (int k : keys) now += (now.empty() ? "" : " ") + std::to_string(k);
        }
        if (now != last) {
            rg_log("error map: %s", now.c_str());
            last = now;
        }
    }
}

void setup_errmap() {
    char spec[64];
    if (GetEnvironmentVariableA("RG_ERRMAP", spec, sizeof spec) == 0 || !spec[0]) return;
    char* end = nullptr;
    g_errmap_rva = std::strtoull(spec, &end, 16);
    g_errmap_off = (end && *end == ':') ? std::strtoull(end + 1, nullptr, 16) : 0;
    // Only in the exe the RVA belongs to: the module must be larger than the RVA and must not be the game child.
    const auto* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char want[MAX_PATH] = {};
    if (GetEnvironmentVariableA("RG_ERRMAP_EXE", want, sizeof want) > 0 && want[0] && !std::strstr(exe, want)) return;
    if (!g_errmap_rva || g_errmap_rva + 8 > nt->OptionalHeader.SizeOfImage) return;
    HANDLE t = CreateThread(nullptr, 0, &errmap_thread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    rg_log("error map on: pointer at exe+0x%llx, map at +0x%llx", static_cast<unsigned long long>(g_errmap_rva),
           static_cast<unsigned long long>(g_errmap_off));
}

void setup() {
    InitializeCriticalSection(&g_log_cs);
    // Log next to this DLL.
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(g_self, self, MAX_PATH);
    lstrcpynW(g_log_path, self, MAX_PATH);
    wchar_t* slash = wcsrchr(g_log_path, L'\\');
    if (slash) lstrcpynW(slash + 1, L"rg-mfcam.log", static_cast<int>(MAX_PATH - (slash + 1 - g_log_path)));
    else lstrcpynW(g_log_path, L"rg-mfcam.log", MAX_PATH);
    wchar_t buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"RG_MFCAM_SOURCE", buf, MAX_PATH) > 0) lstrcpynW(g_source, buf, MAX_PATH);
    if (GetEnvironmentVariableW(L"RG_MFCAM_FPS", buf, MAX_PATH) > 0) {
        const int f = _wtoi(buf);
        if (f >= 1 && f <= 60) g_fps = f;
    }
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char src8[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, g_source, -1, src8, MAX_PATH, nullptr, nullptr);
    rg_log("rg-mfcam loaded in %s (camera file %s, %d fps)", exe, src8, g_fps);
    setup_d3dx10_hrfix();   // independent of the camera: also with RG_MFCAM_OFF=1
    setup_jvs_com();        // likewise: only with RG_JVS_COMPORT
    setup_str_board();      // likewise: only with RG_STR_COMPORT
    setup_dongle();         // likewise: only with RG_DONGLE
    setup_content_router(); // likewise: only with RG_CONTENT_ROUTER
    // These two both load a DLL and therefore must NOT run in DllMain (loader lock): in a thread,
    // like the stalldump above.
    {
        HANDLE t = CreateThread(nullptr, 0, &load_helper_dlls, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    setup_sharek_fake();    // likewise: only with RG_SHAREK_FAKE=1
    setup_odbg();           // likewise: only with RG_ODBG=1
    setup_stall_dump();     // likewise: only with RG_STALL_DUMP
    setup_image_dump();     // only with RG_IMAGE_DUMP
    setup_drive_fix();      // likewise: only with RG_DRIVE_FIXUP
    setup_bngrw();          // likewise: only with RG_BNGRW
    setup_msg_trace();      // likewise: only with RG_MSGTRACE=1
    setup_windowed();       // likewise: only with RG_WINDOWED=1
    setup_child_preload();  // likewise: only with RG_CHILD_PRELOAD
    setup_dome_gun();       // likewise: only with RG_DOMEGUN_COMPORT
    setup_errmap();         // likewise: only with RG_ERRMAP
    if (GetEnvironmentVariableW(L"RG_MFCAM_OFF", buf, MAX_PATH) > 0 && buf[0] == L'1') {
        rg_log("RG_MFCAM_OFF=1: nothing redirected");
        return;
    }
    char synth[32];
    if (GetEnvironmentVariableA("RG_MFCAM_SYNTHETIC", synth, sizeof synth) > 0 && synth[0]) {
        if (rg_camsel::parse_size(synth, g_synth_w, g_synth_h)) rg_log("synthetic camera on: %ux%u when there is no camera file", g_synth_w, g_synth_h);
        else rg_log("RG_MFCAM_SYNTHETIC=%s not understood (WxH, even width, at most 4096x4096); synthetic camera off", synth);
    }
    char modname[MAX_PATH] = {};
    HMODULE target = GetModuleHandleW(nullptr);
    if (GetEnvironmentVariableA("RG_MFCAM_MODULE", modname, sizeof modname) > 0 && modname[0]) {
        target = GetModuleHandleA(modname);
        if (!target) {
            rg_log("import table: module %s is not loaded; no camera", modname);
            return;
        }
        rg_log("import table: the redirection goes into %s", modname);
    }
    void* old = patch_iat(target, "MF.dll", "MFEnumDeviceSources", reinterpret_cast<void*>(&rg_MFEnumDeviceSources));
    if (old) {
        g_orig_enum = reinterpret_cast<EnumFn>(old);
        rg_log("import table: MF.dll!MFEnumDeviceSources redirected (old address %p)", old);
    } else {
        rg_log("import table: MF.dll!MFEnumDeviceSources NOT found in the game; no camera");
    }
    uint32_t w = 0, h = 0;
    rg_camfeed_header fh;
    std::vector<BYTE> ff;
    if (read_feed(fh, ff)) rg_log("camera file now: %ux%u with frame", fh.width, fh.height);
    else if (feed_present(w, h)) rg_log("camera file now: there is no frame; the synthetic camera (%ux%u, gray) answers", w, h);
    else rg_log("camera file now: there is no frame (yet) (the daemon turns it on for a game with [Camera] Use = game)");
}

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        setup();
    }
    return TRUE;
}

// For an experiment without the game (a small program under Wine that loads this DLL): the same function the game gets after the redirection.
extern "C" __declspec(dllexport) HRESULT WINAPI RgMfcamEnumDeviceSources(IMFAttributes* a, IMFActivate*** s, UINT32* c) {
    return rg_MFEnumDeviceSources(a, s, c);
}
