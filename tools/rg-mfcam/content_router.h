// SPDX-License-Identifier: GPL-3.0-or-later
// The content router's echo reply in rg-mfcam.dll (see ELEVENTH TASK in rg_mfcam.cpp), without Windows calls, so that
// tests/test_content_router.cpp can test it on the host.
//
// What Mario Kart DX does (measured statically on MK_AGP3_FINAL.exe, objdump, and on the bench 2026-10-20):
// * A real cabinet sits behind a store "content router" at the fixed address 192.168.112.254 (the string at 0x9B0800). The game
//   pings it with IPHLPAPI!IcmpSendEcho (timeout 100 ms) and only checks that a reply came back with as many data bytes as it sent:
//     - the boot check, 0x71C8A0: up to four tries; no reply gives error 0x10 (screen "E05-01");
//     - the local network monitor, 0x71BB70, through a ping object (0x720190, its thread at 0x7204A0): after ten failed rounds
//       error 0x14, screen "E05-05 ローカルネットワークエラー" (disconnected from the local network), measured ~2 min after the start.
// * Off a store network nobody answers that address, so both checks fail on our route.
//
// The emulation: an IcmpSendEcho for exactly that one address (RG_CONTENT_ROUTER) answers like a router on the same switch
// (status IP_SUCCESS, a round trip of 1 ms, the request data echoed). Every other address goes to the real IcmpSendEcho.
//
// The reply buffer layout is the 32-bit ICMP_ECHO_REPLY (the game is a 32-bit process), followed by the echoed data:
//   +0  Address        IPAddr, the replying address (network byte order)
//   +4  Status         0 = IP_SUCCESS
//   +8  RoundTripTime  milliseconds
//   +12 DataSize       WORD, number of echoed bytes
//   +14 Reserved       WORD
//   +16 Data           pointer to the echoed bytes (here: right after this structure)
//   +20 Options        IP_OPTION_INFORMATION: Ttl, Tos, Flags, OptionsSize (one byte each), OptionsData (pointer)
// The game sizes its buffer as exactly 28 + the request size (0x7204F1, 0x71CA1A), so no more than that is required here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rg_router {

constexpr size_t kEchoReplySize = 28;   // sizeof(ICMP_ECHO_REPLY) in a 32-bit process
constexpr uint32_t kRoundTripMs = 1;
constexpr uint8_t kTtl = 128;

// "a.b.c.d" -> the address in network byte order as Windows keeps an IPAddr in memory (a in the lowest byte).
// Returns false for anything that is not four decimal numbers 0..255 separated by dots.
inline bool parse_ipv4(const char* text, uint32_t* out) {
    if (!text || !out) return false;
    uint32_t addr = 0;
    for (int part = 0; part < 4; ++part) {
        if (*text < '0' || *text > '9') return false;
        unsigned value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + static_cast<unsigned>(*text - '0');
            if (++digits > 3 || value > 255) return false;
            ++text;
        }
        addr |= static_cast<uint32_t>(value) << (8 * part);
        if (part < 3) {
            if (*text != '.') return false;
            ++text;
        }
    }
    if (*text != '\0') return false;
    *out = addr;
    return true;
}

inline void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
inline void put16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); }

// Writes one echo reply from `addr` into `reply` (`reply_size` bytes): the structure above and then the `size` request bytes.
// `data_ptr` is the 32-bit address the Data field gets (in the DLL: reply + kEchoReplySize). Returns the number of replies
// written (1), or 0 if the buffer is too small, as IcmpSendEcho does.
inline unsigned fill_echo_reply(uint8_t* reply, size_t reply_size, uint32_t addr, const void* data, uint16_t size,
                                uint32_t data_ptr) {
    if (!reply || reply_size < kEchoReplySize + size) return 0;
    std::memset(reply, 0, kEchoReplySize);
    put32(reply + 0, addr);
    put32(reply + 4, 0);   // IP_SUCCESS
    put32(reply + 8, kRoundTripMs);
    put16(reply + 12, size);
    put32(reply + 16, data_ptr);
    reply[20] = kTtl;
    if (size && data) std::memmove(reply + kEchoReplySize, data, size);
    return 1;
}

}  // namespace rg_router
