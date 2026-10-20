// SPDX-License-Identifier: GPL-3.0-or-later
// Logic of our own AMAuthd IAuth object in rg-mfcam.dll, without Windows calls, so the host tests
// (tests/test_am_auth_fake.cpp) can check it.
//
// WHAT THE GAME DOES (measured on MK_AGP3_FINAL.exe v1.18.16, objdump; addresses without ASLR,
// image base 0x400000)
//   Right after the boot check the game asks CoCreateInstance for the ALL.Net authentication
//   object of AMAuthd:
//       CoCreateInstance(CLSID {4603BB03-058D-43D9-B96F-639BE908C1ED}, NULL, CLSCTX_LOCAL_SERVER,
//                        IID {045A5150-D2B3-4590-A38B-C1158678E1AC}, &iface)   (0x8B2723)
//   On a cabinet AMAuthd.exe is the local server behind that class; under Wine without it the call
//   returns REGDB_E_CLASSNOTREG and the game waits forever on the ALL.Net check. This object answers
//   the class itself, so the game gets a working IAuth without AMAuthd, Mucha or a network.
//
// THE INTERFACE (IUnknown + 29 methods, MIDL proxy/stub iauthdll.dll; the game calls the proxy
// vtable in its own process). Every method is stdcall with the interface pointer as the first stack
// argument. The slots the boot check calls, measured at the call site:
//   slot 5  (+0x14)  0x0C argument bytes (this + two dwords: 0 and -1)   0x8B2735, 0x7613AC
//                    Created and initialised right after CoCreateInstance.
//   slot 9  (+0x24)  0x08 argument bytes (this + pointer to a 0xD4 status block)
//                    0x4018D8, 0x401A83, 0x4618C6, ... The block is filled from the Mucha state
//                    object; see the offsets below.
//   slot 19 (+0x4C)  0x08 argument bytes (this + pointer to a 4-byte status / 4 doubles)
//                    0x401A2D: 0 = no data, 1 = use a constant, other = the four doubles.
//   slot 10 (+0x28)  0x08 argument bytes (this + pointer to a network-info block)   0x803D00
//                    The game only looks at the HRESULT; 0 is "no error".
// The slots the game calls later (attract mode and the network code), measured at the call sites
// 2026-10-20:
//   slot 14 (+0x38)  0x08 argument bytes (this + the game's server-name buffer)      0x461A05
//                    Offline (no IAuth) the game keeps its own buffer, so ours leaves it untouched.
//   slot 23 (+0x5C)  0x08 argument bytes (this + a dword, 0)                         0x803938
//                    Called when the status block's +0x24 is above 0; it only has to succeed.
//   slots 12, 13, 15, 29, 30 (+0x30, +0x34, +0x3C, +0x74, +0x78)  0x04 argument bytes (this only)
//                    0x4617C9, 0x4617B5, 0x4621A9, 0x768E9C, 0x7634AD / 0x80640F.
// Every slot must pop exactly its argument bytes (stdcall): a stub with one dword too few leaves the
// caller's stack off by four, and the game falls over later (slot 23: ~4 minutes into attract mode).
//
// THE STATUS BLOCK (what the game reads, from the accessors around 0x4015C0 and the real AMAuthd
// implementation at 0x40ACB0):
//   +0x04  daemon error       (copied from the Mucha state object)
//   +0x08  auth state: 1..8. The game treats 3..8 as "authenticated / connected" (0x4015C0); the
//          real daemon maps its internal result onto 2, 4, 5, 6, 7 and 8.
//   +0x0C  hop count / RTT source
//   +0x10  round-trip value; the game uses it as a fallback timing value
//   +0x18  copy of the Mucha configuration block (place id, PCB id, ...)
//   +0x1C  last error code, meaningful in -14..-1 (0x4018A0)
//   +0x3C..+0x42  four WORD state/error fields, read by the boot-check logging (0x4016C0..)
//   +0xA0  "daemon reached" flag
//
// WHAT OUR OBJECT DOES: it reports an authenticated cabinet with no online connection: auth state
// 8 (the real daemon's INT_MIN result, one of the states the game accepts), everything else zero
// and empty. Slot 19 says "no data", slot 10 succeeds, slot 5 succeeds. No authentication is
// bypassed: this is the local "no ALL.Net server" answer of a cabinet that runs offline.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace rg_amauth {

struct Guid {
    uint32_t d1;
    uint16_t d2, d3;
    uint8_t d4[8];
};
inline bool same(const Guid& a, const Guid& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// CLSID of the AMAuthd IAuth object and the IID of IAuth (both also in the exe at 0xA17634/0xA17644).
constexpr Guid kClsid = {0x4603bb03u, 0x058d, 0x43d9, {0xb9, 0x6f, 0x63, 0x9b, 0xe9, 0x08, 0xc1, 0xed}};
constexpr Guid kIid = {0x045a5150u, 0xd2b3, 0x4590, {0xa3, 0x8b, 0xc1, 0x15, 0x86, 0x78, 0xe1, 0xac}};
constexpr Guid kIidUnknown = {0x00000000u, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

inline bool wants_clsid(const Guid& clsid) { return same(clsid, kClsid); }
// The caller in rg-mfcam has a Windows GUID; compare it as raw bytes so no GUID type has to cross.
inline bool wants_clsid_bytes(const void* g) { return g && std::memcmp(g, &kClsid, sizeof kClsid) == 0; }
// IUnknown is the only other interface the game asks for (the proxy and the game itself).
inline bool knows_iid(const Guid& iid) { return same(iid, kIid) || same(iid, kIidUnknown); }

enum class Kind : uint8_t {
    Nop,      // S_OK, writes nothing
    Init,     // slot 5: S_OK, init call, writes nothing
    Status,   // slot 9: fills the status block (see below)
    AuthInfo, // slot 19: [out] dword = 0 ("no data")
    Network,  // slot 10: [out] block = zeroed, S_OK
};
struct Slot {
    int index;       // vtable offset / 4
    int arg_bytes;   // stdcall: interface pointer + arguments, popped by the callee
    Kind kind;
    const char* use; // where the game calls it (address of the call)
};

// Every vtable call the boot check makes, measured in MK_AGP3_FINAL.exe.
constexpr Slot kSlots[] = {
    {5, 12, Kind::Init, "0x8B2735 (after CoCreateInstance), 0x7613AC"},
    {9, 8, Kind::Status, "0x4018D8, 0x401A83, 0x4618C6, 0x768EA0"},
    {10, 8, Kind::Network, "0x803D00"},
    {19, 8, Kind::AuthInfo, "0x401A2D"},
};
constexpr int kSlotCount = static_cast<int>(sizeof kSlots / sizeof kSlots[0]);
constexpr int kVtableSize = 32;   // 3 IUnknown slots + 29 IAuth methods

constexpr const Slot* find_slot(int index) {
    for (int i = 0; i < kSlotCount; ++i)
        if (kSlots[i].index == index) return &kSlots[i];
    return nullptr;
}

// The status block. The game reserves 0xD0..0xD4 bytes; the largest field the real daemon writes
// is +0xC4, so 0xC8 bytes are cleared and the caller's stack cookie (at +0xCC) stays untouched.
constexpr int kStatusSize = 0xC8;
constexpr int kAuthModeOffset = 0x00;    // the game's AllNetAuthManager::IsAuthOk wants 8 here
constexpr int kAuthStateOffset = 0x08;
constexpr int kDaemonErrorOffset = 0x04;
constexpr int kHopOffset = 0x0C;
constexpr int kRoundTripOffset = 0x10;
constexpr int kAuthTypeOffset = 0x18;    // first field of the Mucha configuration block
constexpr int kLastErrorOffset = 0x1C;
constexpr int kWOrdStateOffset = 0x3C;   // four WORDs, 0x3C..0x42
constexpr int kDaemonResultOffset = 0xC0; // 0x0F = the daemon finished the request
constexpr uint32_t kDaemonResultDone = 0x0F;
constexpr int kDaemonReachedOffset = 0xA0;
// The real daemon maps its internal result onto these states; the game accepts 3..8 as connected.
constexpr int kAuthStateMin = 2;
constexpr int kAuthStateMax = 8;
constexpr int kAuthStateDefault = 8;
// 0x462830 (IsAuthOk) wants exactly 8 in the mode field; 0x401270 accepts auth type 8 or 15.
constexpr int kAuthModeDefault = 8;
constexpr int kAuthTypeDefault = 8;

// The auth state can be steered with RG_AMAUTH_STATE so a bench run can try the other accepted
// states (3..8) without rebuilding. Anything else falls back to the default.
inline int auth_state_from_text(const char* v, int fallback = kAuthStateDefault) {
    if (v && *v) {
        char* end = nullptr;
        const long n = std::strtol(v, &end, 10);
        if (end && *end == '\0' && n >= kAuthStateMin && n <= kAuthStateMax) return static_cast<int>(n);
    }
    return fallback;
}
inline int auth_choice() {
    static int chosen = -1;   /* read once; the boot check asks often */
    if (chosen < 0) chosen = auth_state_from_text(std::getenv("RG_AMAUTH_STATE"));
    return chosen;
}

// Fill the status block the game hands to slot 9. The whole block is cleared first, so no stack
// garbage reaches the game; then the accepted auth state and type are written.
//
// Offset +0xC0 is the daemon result: 0x0F means "request finished". The game's own IAuth client
// (MK_AGP3_FINAL.exe 0x401AB0, state 3) only leaves its wait loop when it finds 0x0F there, and the
// boot check waits on that client before it draws the remaining items. Without it the auth client
// stays in state 3 forever and the check screen keeps its items blank.
inline void fill_status(unsigned char* block, int auth_state = auth_choice()) {
    if (!block) return;
    std::memset(block, 0, kStatusSize);
    const uint32_t state = static_cast<uint32_t>(auth_state);
    std::memcpy(block + kAuthStateOffset, &state, sizeof state);
    const uint32_t mode = kAuthModeDefault;
    std::memcpy(block + kAuthModeOffset, &mode, sizeof mode);
    const uint32_t type = kAuthTypeDefault;
    std::memcpy(block + kAuthTypeOffset, &type, sizeof type);
    const uint32_t result = kDaemonResultDone;
    std::memcpy(block + kDaemonResultOffset, &result, sizeof result);
    // 0x4012F0 (the update/server predicate) wants +0x20 == 2 and +0x24 > 0.
    const uint32_t phase = 2;
    std::memcpy(block + 0x20, &phase, sizeof phase);
    const uint32_t detail = 1;
    std::memcpy(block + 0x24, &detail, sizeof detail);
}

}  // namespace rg_amauth
