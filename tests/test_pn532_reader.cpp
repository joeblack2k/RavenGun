// Lost Land Adventure's card reader in rg-mfcam.dll (tools/rg-mfcam/pn532_reader.h): PN532 host frames in, ACK plus answer frame out,
// as RSLauncher.exe's card library (0x140132260) reads them.
#include <cstdio>
#include <vector>

#include "../tools/rg-mfcam/pn532_reader.h"

using namespace rg_pn532;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

// A host frame the way 0x140132260 builds it: 00 00 FF LEN LCS D4 cmd data DCS 00.
static std::vector<uint8_t> host_frame(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> f = {0x00, 0x00, 0xff, static_cast<uint8_t>(body.size()), static_cast<uint8_t>(0x100 - body.size())};
    uint8_t sum = 0;
    for (uint8_t b : body) {
        f.push_back(b);
        sum = static_cast<uint8_t>(sum + b);
    }
    f.push_back(static_cast<uint8_t>(0x100 - sum));
    f.push_back(0x00);
    return f;
}

static std::vector<uint8_t> drain(Reader& r) {
    std::vector<uint8_t> v(r.pending());
    r.read(v.data(), v.size());
    return v;
}

// The answer payload (D5 ...) after the ACK, with the checks the launcher makes (LCS, DCS, postamble).
static std::vector<uint8_t> payload_of(const std::vector<uint8_t>& v) {
    if (v.size() < 6 + 7) return {};
    for (int i = 0; i < 6; ++i)
        if (v[i] != kAck[i]) return {};
    const uint8_t* f = v.data() + 6;
    if (f[0] != 0 || f[1] != 0 || f[2] != 0xff || static_cast<uint8_t>(f[3] + f[4]) != 0) return {};
    const size_t len = f[3];
    if (v.size() != 6 + 5 + len + 2) return {};
    uint8_t sum = 0;
    for (size_t k = 0; k < len; ++k) sum = static_cast<uint8_t>(sum + f[5 + k]);
    if (static_cast<uint8_t>(sum + f[5 + len]) != 0 || f[6 + len] != 0) return {};
    return std::vector<uint8_t>(f + 5, f + 5 + len);
}

int main() {
    // The wake-up byte alone gives no answer.
    {
        Reader r;
        const uint8_t wake = 0x55;
        r.write(&wake, 1);
        CHECK(r.pending() == 0);
    }
    // 0x18 0x01 (0x140133110) wants exactly D5 19.
    {
        Reader r;
        const uint8_t wake = 0x55;
        r.write(&wake, 1);
        const auto f = host_frame({0xd4, 0x18, 0x01});
        r.write(f.data(), f.size());
        const auto p = payload_of(drain(r));
        CHECK((p == std::vector<uint8_t>{0xd5, 0x19}));
        CHECK(r.last_cmd == 0x18 && r.commands == 1);
    }
    // RFConfiguration 0x32 (0x140133325): D5 33, also when the frame arrives in pieces.
    {
        Reader r;
        const auto f = host_frame({0xd4, 0x32, 0x02, 0x00, 0x00});
        for (uint8_t b : f) r.write(&b, 1);
        CHECK((payload_of(drain(r)) == std::vector<uint8_t>{0xd5, 0x33}));
    }
    // WriteRegister 0x08 (0x1401334e8): D5 09 00.
    {
        Reader r;
        const auto f = host_frame({0xd4, 0x08, 0x63, 0x0d, 0x00});
        r.write(f.data(), f.size());
        CHECK((payload_of(drain(r)) == std::vector<uint8_t>{0xd5, 0x09, 0x00}));
    }
    // ReadRegister 0x06 with eight registers (0x140132cb0): D5 07 and eight equal bytes (length 10).
    {
        Reader r;
        const auto f = host_frame({0xd4, 0x06, 0xa0, 0x1c, 0xa0, 0x1d, 0xa0, 0x1e, 0xa0, 0x22, 0xa0, 0xcb, 0xa0, 0xcc, 0xa0, 0xcd, 0xa0, 0xce});
        r.write(f.data(), f.size());
        const auto p = payload_of(drain(r));
        CHECK(p.size() == 10 && p[0] == 0xd5 && p[1] == 0x07);
        CHECK(p.size() == 10 && p[2] == p[6] && p[3] == p[7] && p[4] == p[8] && p[5] == p[9]);
    }
    // 0x0C (0x1401341d0): at most 5 bytes.
    {
        Reader r;
        const auto f = host_frame({0xd4, 0x0c});
        r.write(f.data(), f.size());
        const auto p = payload_of(drain(r));
        CHECK(p.size() >= 2 && p.size() <= 5 && p[1] == 0x0d);
    }
    // No card: InCommunicateThru answers status 1 (timeout), InListPassiveTarget 0 targets.
    {
        Reader r;
        auto f = host_frame({0xd4, 0x42, 0x06, 0x00, 0xff, 0xff, 0x00, 0x00});
        r.write(f.data(), f.size());
        CHECK((payload_of(drain(r)) == std::vector<uint8_t>{0xd5, 0x43, 0x01}));
        f = host_frame({0xd4, 0x4a, 0x01, 0x01, 0x00, 0xff, 0xff, 0x00, 0x00});
        r.write(f.data(), f.size());
        CHECK((payload_of(drain(r)) == std::vector<uint8_t>{0xd5, 0x4b, 0x00}));
    }
    // The host's own ACK (abort, 0x140131fd0) is swallowed; a frame with a bad checksum is dropped; the next good frame is answered.
    {
        Reader r;
        r.write(kAck, sizeof kAck);
        CHECK(r.pending() == 0);
        auto bad = host_frame({0xd4, 0x32, 0x01, 0x01});
        bad[bad.size() - 2] ^= 0x55;
        r.write(bad.data(), bad.size());
        CHECK(r.pending() == 0 && r.dropped == 1);
        const auto f = host_frame({0xd4, 0x12, 0x14});
        r.write(f.data(), f.size());
        CHECK((payload_of(drain(r)) == std::vector<uint8_t>{0xd5, 0x13}));
    }
    // Two frames in one write get two answers, in order.
    {
        Reader r;
        auto a = host_frame({0xd4, 0x32, 0x01, 0x00});
        const auto b = host_frame({0xd4, 0x44, 0x01});
        a.insert(a.end(), b.begin(), b.end());
        r.write(a.data(), a.size());
        std::vector<uint8_t> all = drain(r);
        CHECK(all.size() == 2 * (6 + 9));
        std::vector<uint8_t> first(all.begin(), all.begin() + 15), second(all.begin() + 15, all.end());
        CHECK((payload_of(first) == std::vector<uint8_t>{0xd5, 0x33}));
        CHECK((payload_of(second) == std::vector<uint8_t>{0xd5, 0x45}));
    }
    // An extended host frame (00 00 FF FF FF LENH LENL LCS ...) is answered too.
    {
        Reader r;
        std::vector<uint8_t> body(300, 0x11);
        body[0] = 0xd4;
        body[1] = 0x40;
        body[2] = 0x01;
        std::vector<uint8_t> f = {0x00, 0x00, 0xff, 0xff, 0xff, 0x01, 0x2c, static_cast<uint8_t>(0x100 - 0x01 - 0x2c)};
        uint8_t sum = 0;
        for (uint8_t x : body) {
            f.push_back(x);
            sum = static_cast<uint8_t>(sum + x);
        }
        f.push_back(static_cast<uint8_t>(0x100 - sum));
        f.push_back(0x00);
        r.write(f.data(), f.size());
        CHECK((payload_of(drain(r)) == std::vector<uint8_t>{0xd5, 0x41, 0x01}));
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
