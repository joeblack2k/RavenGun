// SPDX-License-Identifier: GPL-3.0-or-later
// The decision logic of "our own ShareK object" in rg-mfcam.dll (see FOURTH TASK in rg_mfcam.cpp), without Windows calls, so that
// tests/test_sharek_fake.cpp can test it on the host.
//
// WHAT THE GAME DOES (measured on MK_AGP3_FINAL.exe v1.18.16, objdump; addresses without ASLR, image base 0x400000)
//   ShareKManager.cpp (0x47d9c6..0x482ced) is the client of "ShareK", the helper program that on the real cabinet does the content distribution
//   (updates between cabinets, NET_USE_SHAREK in Data/System/Config/NetConfig.csv). ShareK.exe is a .NET program; the client side
//   is ipccl.dll (.NET, registered with RegAsm.exe on the real cabinet: init_w10.ps1) and that is a COM class:
//       CoCreateInstance(CLSID {ccc44b2a-709a-33a8-a1b0-5dbe25b19c2c}, NULL, 0x17, IID {c2116f7f-cf46-3c68-b187-9d0fe9f70ed0}, &M->iface)
//   at 0x47e834 (StartIpcClient) and 0x48107f (restart after an error). Both GUIDs are name-based (version 3), as .NET derives them for a
//   ComVisible class without [Guid]; they are only in the exe, not in ipccl.dll/ShareK.exe.
//   The registration is missing in our prefix: CoCreateInstance returns 0x80040154 (REGDB_E_CLASSNOTREG), the game throws a _com_error,
//   the catch handler at 0x47e8f8 logs "*ERR* ComError Code = 80040154" and then calls method +0x20 anyway on the now empty interface
//   (0x47e95e: mov eax,[eax] = 0; mov ecx,[eax] -> access violation). So this is NOT the IAuth of AMAuthd (CLSID 4603BB03..., 0x4010c0):
//   the constructor of that wrapper catches a failed CoCreateInstance itself (flag byte 0, 0x4010dd/0x4010d9).
//
// THE INTERFACE (IUnknown + IDispatch + .NET methods; the slot numbers are vtable offset/4, measured at every call site)
//   All methods are stdcall and return an HRESULT; the game throws a _com_error for HRESULT < 0. Each call has this + an output
//   pointer or input value (8 bytes of arguments), except the six methods with four extra arguments (20 bytes).
//   What the game reads from the replies:
//     slot 7  (+0x1c)  StartIpcClient([out] VARIANT_BOOL*): WORD <> 0 = "Startup ShareK-Client." (0x47e892); 0 = "StartIpcClient() error"
//     slot 8  (+0x20)  stop/shut down, result unread; called just before Release (0x47e77f) and in the catch (0x47e965)
//     slot 33 (+0x84)  ShareK process restarted? [out] WORD, 1 = yes: sets M+0x39b, then "ShareKプロセス再起動により再初期化" (0x47ed68)
//     slot 35 (+0x8c)  ShareK status [out] WORD, kept in M+0x3a8 (previous value in +0x3aa); the tick at 0x47ecd0 translates it:
//                        0 = do work unchanged, 1 = "配信機能OFF" (clean up once, then work again), 2 = "ネットワーク未初期化により
//                        自動復旧待機" (0x47eea5): with status 2 the tick skips the work (0x47eee3: cmp [+0x3a8],2 -> done), every
//                        tick; other values = "未実装のShareKステータスコード"
//   The other results that are read (slot 9, 11, 27, 31, 41, 43, 55) are first set to 0 by the caller (mov [ebp-x],ebx) and left
//   untouched by an empty reply; 0 means "nothing to do" there.
//
// WHAT OUR OWN OBJECT DOES: a ShareK that is running but has nothing to distribute. StartIpcClient succeeds (-1), the status is
// 1 by default (配信機能OFF: the game cleans up the distribution once and then simply works) and can be chosen with RG_SHAREK_STATUS
// (2 = wait for the network, then the tick does nothing; 0 = simply work), no restart requested, and every other method returns S_OK
// without writing anything.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rg_sharek {

struct Guid {
    uint32_t d1;
    uint16_t d2, d3;
    uint8_t d4[8];
};
inline bool same(const Guid& a, const Guid& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

constexpr Guid kClsid = {0xccc44b2au, 0x709a, 0x33a8, {0xa1, 0xb0, 0x5d, 0xbe, 0x25, 0xb1, 0x9c, 0x2c}};
constexpr Guid kIid = {0xc2116f7fu, 0xcf46, 0x3c68, {0xb1, 0x87, 0x9d, 0x0f, 0xe9, 0xf7, 0x0e, 0xd0}};
constexpr Guid kIidUnknown = {0x00000000u, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr Guid kIidDispatch = {0x00020400u, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// For which CLSIDs we supply our own object. Everything else (including the IAuth of AMAuthd) goes to the real CoCreateInstance.
inline bool wants_clsid(const Guid& clsid) { return same(clsid, kClsid); }
// Which interfaces our object knows (QueryInterface).
inline bool knows_iid(const Guid& iid) { return same(iid, kIid) || same(iid, kIidUnknown) || same(iid, kIidDispatch); }

enum class Kind : uint8_t {
    Nop,      // S_OK, writes nothing
    Start,    // [out] WORD = -1 (VARIANT_TRUE)
    Status,   // [out] WORD = the ShareK status (see status_keuze)
    Restart,  // [out] WORD = 0
};
constexpr int16_t kStartOk = -1;     // VARIANT_TRUE
// Status that our own object returns. 0 = simply work, 1 = "配信機能OFF" (the game cleans up the
// distribution once and then simply works), 2 = "wait for the network" (the game's tick
// then does nothing, hence the black screen). This can be chosen with RG_SHAREK_STATUS; the
// default is 1, because a standalone cabinet has nothing to distribute but must keep running. Only 0,
// 1 and 2 are valid: anything else (or anything non-numeric) falls back to the default, so that a
// typo in the environment does not make the game run on an unknown status code.
// Convert the text from the environment. Only 0, 1 and 2 are valid; everything else (empty,
// non-numeric, out of range) falls back to the default. Separate from the reading, so that the test can check this
// without starting a process.
inline int16_t status_from_text(const char* v, int16_t fallback = 1) {
    if (v && *v) {
        char* end = nullptr;
        const long w = std::strtol(v, &end, 10);
        if (end && *end == '\0' && w >= 0 && w <= 2) return static_cast<int16_t>(w);
    }
    return fallback;
}

inline int16_t status_keuze() {
    static int chosen = -1;   /* read once and remember; the tick asks for this often */
    if (chosen < 0) chosen = status_from_text(std::getenv("RG_SHAREK_STATUS"));
    return static_cast<int16_t>(chosen);
}
constexpr int16_t kRestartNo = 0;

struct Slot {
    int index;       // vtable offset / 4
    int arg_bytes;   // stdcall: this + arguments, cleaned up by the callee
    Kind kind;
    const char* use; // where the game calls it (address of the call)
};

// Every vtable call in ShareKManager.cpp (0x47c000..0x483400), found with a scanner for "mov r,[r+off] ... call r".
constexpr Slot kSlots[] = {
    {7, 8, Kind::Start, "StartIpcClient 0x47e86b, 0x4810b6"},
    {8, 8, Kind::Nop, "shutdown 0x47e77f, 0x47e965, 0x4811bb, 0x48131c"},
    {9, 8, Kind::Nop, "0x4802eb: result set to 0; 3 = action"},
    {10, 8, Kind::Nop, "0x4802ff (input 1)"},
    {11, 8, Kind::Nop, "0x480298"},
    {14, 20, Kind::Nop, "0x480828"},
    {15, 20, Kind::Nop, "0x480998"},
    {17, 20, Kind::Nop, "0x480b05"},
    {18, 20, Kind::Nop, "0x480c68"},
    {19, 20, Kind::Nop, "0x480dd9"},
    {20, 20, Kind::Nop, "0x480f48"},
    {25, 8, Kind::Nop, "0x482c33"},
    {26, 8, Kind::Nop, "0x482c09"},
    {27, 8, Kind::Nop, "0x481b77"},
    {28, 8, Kind::Nop, "0x481c9f"},
    {29, 8, Kind::Nop, "0x481db8"},
    {30, 8, Kind::Nop, "0x481eef"},
    {31, 8, Kind::Nop, "0x482007"},
    {32, 8, Kind::Nop, "0x482130"},
    {33, 8, Kind::Restart, "restart query 0x48128e"},
    {34, 8, Kind::Nop, "0x481398"},
    {35, 8, Kind::Status, "status 0x4814b7"},
    {38, 8, Kind::Nop, "0x482247"},
    {40, 8, Kind::Nop, "0x482357"},
    {41, 8, Kind::Nop, "0x4815e7"},
    {43, 8, Kind::Nop, "0x481717"},
    {44, 8, Kind::Nop, "0x481831"},
    {46, 8, Kind::Nop, "0x482467"},
    {47, 8, Kind::Nop, "0x482579"},
    {52, 8, Kind::Nop, "0x482697"},
    {53, 8, Kind::Nop, "0x4827a9"},
    {54, 8, Kind::Nop, "0x4828c1"},
    {55, 8, Kind::Nop, "0x481947"},
    {56, 8, Kind::Nop, "0x481a61"},
    {57, 8, Kind::Nop, "0x4829de"},
    {58, 8, Kind::Nop, "0x482af7"},
};
constexpr int kSlotCount = static_cast<int>(sizeof kSlots / sizeof kSlots[0]);
constexpr int kVtableSize = 64;   // well above the highest slot in use (58)

constexpr const Slot* find_slot(int index) {
    for (int i = 0; i < kSlotCount; ++i)
        if (kSlots[i].index == index) return &kSlots[i];
    return nullptr;
}

// The reply of a slot as a value for its [out] WORD; returns false if the slot writes nothing.
inline bool out_word(Kind k, int16_t& v) {
    switch (k) {
        case Kind::Start: v = kStartOk; return true;
        case Kind::Status: v = status_keuze(); return true;
        case Kind::Restart: v = kRestartNo; return true;
        case Kind::Nop: break;
    }
    return false;
}

}  // namespace rg_sharek
