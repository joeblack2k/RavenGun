// SPDX-License-Identifier: GPL-3.0-or-later
// Lost Land Adventure's card reader on COM4, emulated by rg-mfcam.dll (see FOURTEENTH TASK in rg_mfcam.cpp), without Windows calls,
// so that tests/test_pn532_reader.cpp can test it on the host.
//
// WHAT THE LAUNCHER DOES (RSLauncher.exe, 64-bit, objdump; the card library is linked into the exe)
//   - WinMain calls 0x14002eab0, which attaches reader 0 on "COM4" (0x140131c20) only when the cabinet type read from
//     Data/romVersion.txt (0x1400392e0, global 0x1404098f8) is 1 modulo 3, and spins on the reader's busy flag (0x1401314f0) until the
//     attach finishes. A failed attach raises error 39 "07-12 CARD R/W INITIALIZE ERROR" (0x14002eb91).
//   - The port layer (0x140138860..0x140138bc0) opens the port with CreateFileA (no overlapped I/O), sets it up with
//     GetCommState/SetCommState/SetCommMask/SetCommTimeouts, clears it with PurgeComm and FlushFileBuffers, writes with WriteFile and
//     reads with ReadFile in a loop until enough bytes are there or its own timeout ends (0x1401389d0).
//   - The protocol is the NXP PN532 host frame (0x140132260): it sends 00 00 FF LEN LCS D4 <command> <data> DCS 00 (an extended frame
//     00 00 FF FF FF LENH LENL LCS ... above 255 bytes), then reads 6 bytes and compares them with the ACK 00 00 FF 00 FF 00
//     (0x14028be40), then reads the answer frame 00 00 FF LEN LCS D5 <command+1> <data> DCS 00 and checks LCS and DCS. To abort it sends
//     an ACK itself (0x14028be38, 0x140131fd0). Before the first command it sends one wake-up byte 0x55 (0x140132160).
//   - What it checks in the answers (all after D5 <command+1>):
//       0x06 ReadRegister (0x140132cb0): one byte per register; the first eight are compared in pairs, equal values need no fix-up.
//       0x08 WriteRegister (0x14013346f): a status byte that must be 0.
//       0x0C (0x1401341d0): up to three bytes, kept as reader information.
//       0x42 InCommunicateThru / 0xA0 (0x140132700): a status byte first; 1 means "timeout" (no card answered).
//       0x4A InListPassiveTarget: the number of targets first; 0 means no card.
//       0x02 GetFirmwareVersion (0x140132fb0) has no caller in this launcher, so it gets no data; the others (0x0E, 0x10, 0x12, 0x18, 0x32, 0x44, 0x52) want
//       exactly D5 <command+1> (for example 0x140133325: length 2, D5 33).
// WHAT OUR READER DOES: it is present and has no card. Every complete command frame gets the ACK and then the answer above; the host's
// own ACK and the wake-up bytes are swallowed, a frame with a wrong checksum is dropped (the launcher then times out and retries).
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace rg_pn532 {

inline constexpr uint8_t kHostTfi = 0xd4;
inline constexpr uint8_t kReaderTfi = 0xd5;
inline constexpr uint8_t kAck[6] = {0x00, 0x00, 0xff, 0x00, 0xff, 0x00};

// The data of the answer to one command frame (without D5 and command+1). `data` is the command's data after D4 <command>.
inline std::vector<uint8_t> answer_data(uint8_t cmd, const uint8_t* data, size_t n) {
    switch (cmd) {
    case 0x06: return std::vector<uint8_t>(n / 2, 0x00);          // ReadRegister: every register reads 0
    case 0x08: return {0x00};                                      // WriteRegister: status OK
    case 0x0c: return {0x00, 0x00, 0x00};                          // reader information (kept, not checked)
    case 0x42: return {0x01};                                      // InCommunicateThru: timeout, no card
    case 0x40: return {0x01};                                      // InDataExchange: timeout, no card
    case 0xa0: return {0x01};                                      // CommunicateThruEX: timeout, no card
    case 0x4a: return {0x00};                                      // InListPassiveTarget: no target
    default: (void)data; return {};
    }
}

// A normal answer frame for `payload` (D5 included).
inline std::vector<uint8_t> frame(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f = {0x00, 0x00, 0xff};
    const uint8_t len = static_cast<uint8_t>(payload.size());
    f.push_back(len);
    f.push_back(static_cast<uint8_t>(0x100 - len));
    uint8_t sum = 0;
    for (uint8_t b : payload) {
        f.push_back(b);
        sum = static_cast<uint8_t>(sum + b);
    }
    f.push_back(static_cast<uint8_t>(0x100 - sum));
    f.push_back(0x00);
    return f;
}

// The emulated reader: write() takes what the launcher sends, read() hands out the answers.
struct Reader {
    std::vector<uint8_t> in;      // host bytes not handled yet
    std::deque<uint8_t> out;      // reader bytes not read yet
    int last_cmd = -1;            // the last command answered (for the log)
    unsigned commands = 0;        // command frames answered
    unsigned dropped = 0;         // frames dropped for a bad checksum

    void write(const uint8_t* p, size_t n) {
        in.insert(in.end(), p, p + n);
        parse();
    }
    size_t read(uint8_t* p, size_t n) {
        size_t k = 0;
        while (k < n && !out.empty()) {
            p[k++] = out.front();
            out.pop_front();
        }
        return k;
    }
    size_t pending() const { return out.size(); }

private:
    void consume(size_t n) { in.erase(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(n)); }

    void parse() {
        for (;;) {
            // Find the start code 00 FF (the leading 00 of the preamble is optional for the parser).
            size_t i = 0;
            while (i + 1 < in.size() && !(in[i] == 0x00 && in[i + 1] == 0xff)) ++i;
            if (i + 1 >= in.size()) {   // no start code: keep at most the last byte (it may be the 00 of a start code)
                if (in.size() > 1) consume(in.size() - 1);
                return;
            }
            consume(i);   // wake-up bytes, preamble zeros
            // in[0..1] = 00 FF
            if (in.size() < 4) return;
            const uint8_t len = in[2], lcs = in[3];
            if (len == 0x00 && lcs == 0xff) {   // the host's ACK (abort): swallow it and its postamble
                consume(in.size() >= 5 && in[4] == 0x00 ? 5 : 4);
                continue;
            }
            size_t hdr = 4, dlen = len;
            if (len == 0xff && lcs == 0xff) {   // extended frame: 00 FF FF FF LENH LENL LCS
                if (in.size() < 7) return;
                dlen = (static_cast<size_t>(in[4]) << 8) | in[5];
                if (static_cast<uint8_t>(in[4] + in[5] + in[6]) != 0) {
                    ++dropped;
                    consume(2);
                    continue;
                }
                hdr = 7;
            } else if (static_cast<uint8_t>(len + lcs) != 0) {
                ++dropped;
                consume(2);
                continue;
            }
            if (in.size() < hdr + dlen + 1) return;   // wait for the data and the DCS
            uint8_t sum = 0;
            for (size_t k = 0; k < dlen; ++k) sum = static_cast<uint8_t>(sum + in[hdr + k]);
            const bool ok = static_cast<uint8_t>(sum + in[hdr + dlen]) == 0 && dlen >= 2 && in[hdr] == kHostTfi;
            const std::vector<uint8_t> body(in.begin() + static_cast<std::ptrdiff_t>(hdr),
                                            in.begin() + static_cast<std::ptrdiff_t>(hdr + dlen));
            size_t used = hdr + dlen + 1;
            if (in.size() > used && in[used] == 0x00) ++used;   // postamble
            consume(used);
            if (!ok) {
                ++dropped;
                continue;
            }
            const uint8_t cmd = body[1];
            std::vector<uint8_t> payload = {kReaderTfi, static_cast<uint8_t>(cmd + 1)};
            const std::vector<uint8_t> d = answer_data(cmd, body.data() + 2, body.size() - 2);
            payload.insert(payload.end(), d.begin(), d.end());
            out.insert(out.end(), kAck, kAck + sizeof kAck);
            const std::vector<uint8_t> f = frame(payload);
            out.insert(out.end(), f.begin(), f.end());
            last_cmd = cmd;
            ++commands;
        }
    }
};

}  // namespace rg_pn532
