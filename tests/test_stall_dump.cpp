// De logica van de stalldump van rg-mfcam.dll (tools/rg-mfcam/stall_dump.h): welke stapelwaarden op een terugkeeradres lijken en hoe de
// tijdenlijst gelezen wordt.
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

// Zet `bytes` zo neer dat het laatste byte vlak voor `p` staat, en geef p terug.
static bool call_like(std::initializer_list<uint8_t> bytes) {
    uint8_t buf[16] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    // Alles NOP (0x90: geen aanroep, 0x90 & 0xF8 = 0x90 maar p[-6] moet 0xFF zijn, dus geen toeval).
    size_t n = bytes.size(), i = 8 - n;
    for (uint8_t b : bytes) buf[i++] = b;
    return preceded_by_call(buf + 8);
}

int main() {
    // Echte vormen uit MK_AGP3_FINAL.exe: call 0x47e7d0 (E8 rel32), call [0x943870] (FF 15), call eax (FF D0), call [ecx+0x1c] (FF 51 1c)...
    CHECK(call_like({0xE8, 0x1C, 0xDF, 0xD0, 0xFF}));
    CHECK(call_like({0xFF, 0x15, 0x70, 0x38, 0x94, 0x00}));
    CHECK(call_like({0xFF, 0xD0}));
    CHECK(call_like({0xFF, 0xD7}));
    CHECK(call_like({0xFF, 0x51, 0x1C}));
    CHECK(call_like({0xFF, 0x90, 0x84, 0x00, 0x00, 0x00}));
    CHECK(call_like({0xFF, 0x14, 0x24}));
    CHECK(call_like({0xFF, 0x11}));
    // Geen aanroep: gewone bytes, een jmp (E9), push (FF 35), mov.
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

    std::printf("stall_dump: %d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
