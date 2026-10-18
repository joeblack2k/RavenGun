// De venstermodus van rg-mfcam.dll (tools/rg-mfcam/window_mode.h): het patroon van de volledig-scherm-standaard van Mario Kart DX
// (0x40579F..0x4057B3 in MK_AGP3_FINAL.exe) vinden, precies een treffer vereisen en de imm32 van `mov [ebx+0x30],1` aanwijzen.
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
    // De echte bytes rond 0x40579F (objdump van de exe in de view): ervoor `call 0x404ba0`, erna `mov [ebx+0x34],0x1010100`.
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
    CHECK(real[imm - 3] == 0xC7 && real[imm - 2] == 0x43 && real[imm - 1] == 0x30);   // de imm32 hoort bij mov [ebx+0x30]
    CHECK(real[imm] == 0x01 && real[imm + 1] == 0 && real[imm + 2] == 0 && real[imm + 3] == 0);
    // Op 0 gezet is het patroon weg: een tweede keer laden doet niets meer.
    std::vector<uint8_t> patched = real;
    std::memset(patched.data() + imm, 0, 4);
    CHECK(find_pattern(patched.data(), patched.size(), first) == 0);
    CHECK(imm_to_clear(patched.data(), patched.size()) == patched.size());
    // Twee treffers: niets aanpassen.
    std::vector<uint8_t> twice = real;
    twice.insert(twice.end(), real.begin(), real.end());
    CHECK(find_pattern(twice.data(), twice.size(), first) == 2);
    CHECK(imm_to_clear(twice.data(), twice.size()) == twice.size());
    // Andere afmetingen (een andere versie): geen treffer.
    std::vector<uint8_t> other = real;
    other[8] = 0x00;   // 0x500 in plaats van 0x550
    CHECK(imm_to_clear(other.data(), other.size()) == other.size());
    // Te kort of leeg.
    CHECK(find_pattern(real.data(), 10, first) == 0);
    CHECK(imm_to_clear(nullptr, 0) == 0);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
