// The content router's echo reply of rg-mfcam.dll (tools/rg-mfcam/content_router.h): the address parser and the reply buffer,
// checked the way Mario Kart DX reads it (0x71CA70..0x71CA96 and 0x720536..0x720566: a nonzero result and DataSize equal to the
// request size).
#include <cstdio>
#include <cstring>
#include <vector>

#include "../tools/rg-mfcam/content_router.h"

using namespace rg_router;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static uint32_t get32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
static uint16_t get16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

int main() {
    // The address in memory order: 192.168.112.254 -> bytes c0 a8 70 fe.
    uint32_t a = 0;
    CHECK(parse_ipv4("192.168.112.254", &a));
    const uint8_t want[4] = {0xc0, 0xa8, 0x70, 0xfe};
    CHECK(std::memcmp(&a, want, 4) == 0);
    CHECK(parse_ipv4("0.0.0.0", &a) && a == 0);
    CHECK(parse_ipv4("255.255.255.255", &a) && a == 0xffffffffu);
    // Not an address: the switch then stays off.
    CHECK(!parse_ipv4("", &a));
    CHECK(!parse_ipv4(nullptr, &a));
    CHECK(!parse_ipv4("192.168.112", &a));
    CHECK(!parse_ipv4("192.168.112.254.1", &a));
    CHECK(!parse_ipv4("192.168.112.256", &a));
    CHECK(!parse_ipv4("192.168.0112.1", &a));
    CHECK(!parse_ipv4("192.168.112.x", &a));
    CHECK(!parse_ipv4("192..112.254", &a));
    CHECK(!parse_ipv4(" 192.168.112.254", &a));

    // The game's ping: the wide string of the address object as data, request size = character count + 1, buffer = 28 + that.
    const char payload[] = "1\0009\0002\0.\0";
    const uint16_t size = 8;
    std::vector<uint8_t> buf(kEchoReplySize + size, 0xcc);
    parse_ipv4("192.168.112.254", &a);
    CHECK(fill_echo_reply(buf.data(), buf.size(), a, payload, size, 0x12345678u) == 1);
    CHECK(get32(&buf[0]) == a);           // Address
    CHECK(get32(&buf[4]) == 0);           // Status: IP_SUCCESS
    CHECK(get32(&buf[8]) == kRoundTripMs);
    CHECK(get16(&buf[12]) == size);       // what the game compares with its request size
    CHECK(get16(&buf[14]) == 0);
    CHECK(get32(&buf[16]) == 0x12345678u);  // Data
    CHECK(buf[20] == kTtl);
    CHECK(buf[21] == 0 && buf[22] == 0 && buf[23] == 0 && get32(&buf[24]) == 0);
    CHECK(std::memcmp(&buf[kEchoReplySize], payload, size) == 0);

    // One byte too small: no reply, nothing written (IcmpSendEcho returns 0).
    std::vector<uint8_t> small(kEchoReplySize + size - 1, 0xcc);
    CHECK(fill_echo_reply(small.data(), small.size(), a, payload, size, 0) == 0);
    CHECK(small[0] == 0xcc);
    CHECK(fill_echo_reply(nullptr, 64, a, payload, size, 0) == 0);

    // No data: just the structure.
    std::vector<uint8_t> empty(kEchoReplySize, 0xcc);
    CHECK(fill_echo_reply(empty.data(), empty.size(), a, nullptr, 0, 0) == 1);
    CHECK(get16(&empty[12]) == 0);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
