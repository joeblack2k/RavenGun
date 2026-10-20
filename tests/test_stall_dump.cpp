// The logic of the stall dump of rg-mfcam.dll (tools/rg-mfcam/stall_dump.h): which stack values look like a return address and how the
// list of times is parsed.
#include <cstdio>
#include <initializer_list>

#include "../tools/rg-mfcam/stall_dump.h"

using namespace rg_stall;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

// Place `bytes` so that the last byte sits right before `p`, and check whether it looks like a call.
static bool call_like(std::initializer_list<uint8_t> bytes) {
    uint8_t buf[16] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    // All NOPs (0x90: not a call; 0x90 & 0xF8 = 0x90 but p[-6] must be 0xFF, so no accidental match).
    size_t n = bytes.size(), i = 8 - n;
    for (uint8_t b : bytes) buf[i++] = b;
    return preceded_by_call(buf + 8);
}

int main() {
    // Real forms from MK_AGP3_FINAL.exe: call 0x47e7d0 (E8 rel32), call [0x943870] (FF 15), call eax (FF D0), call [ecx+0x1c] (FF 51 1c)...
    CHECK(call_like({0xE8, 0x1C, 0xDF, 0xD0, 0xFF}));
    CHECK(call_like({0xFF, 0x15, 0x70, 0x38, 0x94, 0x00}));
    CHECK(call_like({0xFF, 0xD0}));
    CHECK(call_like({0xFF, 0xD7}));
    CHECK(call_like({0xFF, 0x51, 0x1C}));
    CHECK(call_like({0xFF, 0x90, 0x84, 0x00, 0x00, 0x00}));
    CHECK(call_like({0xFF, 0x14, 0x24}));
    CHECK(call_like({0xFF, 0x11}));
    // Not a call: plain bytes, a jmp (E9), push (FF 35), mov.
    CHECK(!call_like({0x8B, 0xEC, 0x83, 0xEC, 0x10}));
    CHECK(!call_like({0xE9, 0x1C, 0xDF, 0xD0, 0xFF}));
    CHECK(!call_like({0xFF, 0x35, 0x70, 0x38, 0x94, 0x00}));
    CHECK(!call_like({0xFF, 0x25, 0x70, 0x38, 0x94, 0x00}));   // jmp [abs32]
    CHECK(!call_like({0xFF, 0xE0}));                           // jmp eax

    auto t = parse_times("30,60, 90");
    CHECK(t.size() == 3 && t[0] == 30 && t[1] == 60 && t[2] == 90);
    t = parse_times("45");
    CHECK(t.size() == 1 && t[0] == 45);
    CHECK(parse_times("").empty());
    CHECK(parse_times(nullptr).empty());
    CHECK(parse_times("x,0,-5,99999999").empty());
    t = parse_times("abc,20,,x");
    CHECK(t.size() == 1 && t[0] == 20);

    std::printf("stall_dump: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
