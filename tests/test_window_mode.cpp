// The window mode of rg-mfcam.dll (tools/rg-mfcam/window_mode.h): find the pattern of Mario Kart DX's fullscreen default
// (0x40579F..0x4057B3 in MK_AGP3_FINAL.exe), require exactly one match and point at the imm32 of `mov [ebx+0x30],1`.
#include <cstdio>
#include <cstring>
#include <vector>

#include "../tools/rg-mfcam/window_mode.h"

using namespace rg_winmode;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

int main() {
    // The real bytes around 0x40579F (objdump of the exe in the view): before it `call 0x404ba0`, after it `mov [ebx+0x34],0x1010100`.
    const std::vector<uint8_t> real = {0xE8, 0x01, 0xF4, 0xFF, 0xFF,                         // call 0x404ba0
                                       0xC7, 0x43, 0x28, 0x50, 0x05, 0x00, 0x00,             // mov [ebx+0x28],0x550
                                       0xC7, 0x43, 0x2C, 0x00, 0x03, 0x00, 0x00,             // mov [ebx+0x2c],0x300
                                       0xC7, 0x43, 0x30, 0x01, 0x00, 0x00, 0x00,             // mov [ebx+0x30],0x1
                                       0xC7, 0x43, 0x34, 0x00, 0x01, 0x01, 0x01};            // mov [ebx+0x34],0x1010100
    size_t first = 0;
    CHECK(find_pattern(real.data(), real.size(), first) == 1);
    CHECK(first == 5);
    const size_t imm = imm_to_clear(real.data(), real.size());
    CHECK(imm == 5 + 17);
    CHECK(real[imm - 3] == 0xC7 && real[imm - 2] == 0x43 && real[imm - 1] == 0x30);   // the imm32 belongs to mov [ebx+0x30]
    CHECK(real[imm] == 0x01 && real[imm + 1] == 0 && real[imm + 2] == 0 && real[imm + 3] == 0);
    // Once set to 0 the pattern is gone: loading a second time does nothing.
    std::vector<uint8_t> patched = real;
    std::memset(patched.data() + imm, 0, 4);
    CHECK(find_pattern(patched.data(), patched.size(), first) == 0);
    CHECK(imm_to_clear(patched.data(), patched.size()) == patched.size());
    // Two matches: change nothing.
    std::vector<uint8_t> twice = real;
    twice.insert(twice.end(), real.begin(), real.end());
    CHECK(find_pattern(twice.data(), twice.size(), first) == 2);
    CHECK(imm_to_clear(twice.data(), twice.size()) == twice.size());
    // Different dimensions (another version): no match.
    std::vector<uint8_t> other = real;
    other[8] = 0x00;   // 0x500 instead of 0x550
    CHECK(imm_to_clear(other.data(), other.size()) == other.size());
    // Too short or empty.
    CHECK(find_pattern(real.data(), 10, first) == 0);
    CHECK(imm_to_clear(nullptr, 0) == 0);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
