// The settings of RG_ERRCLEAR in rg-mfcam.dll (tools/rg-mfcam/err_clear.h): the launcher's error manager, its forced clear, the error
// code and the first bytes of that function, all hex.
#include <cstdio>

#include "../tools/rg-mfcam/err_clear.h"

using namespace rg_errclear;

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
    // Lost Land's launcher: manager pointer at 0x1404098e0, forced clear 0x140035260 (mov [rsp+0x10],edx; push rbx; push rbp;
    // push rdi; sub rsp,..), error 58.
    Spec s;
    CHECK(parse("4098e0:35260:3a:8954241053555748", s));
    CHECK(s.mgr_rva == 0x4098e0 && s.fn_rva == 0x35260 && s.code == 58 && s.prologue_len == 8);
    const uint8_t mem[] = {0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x57, 0x48, 0x83, 0xec, 0x20};
    CHECK(prologue_matches(s, mem));
    const uint8_t other[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83};
    CHECK(!prologue_matches(s, other));
    CHECK(parse("4098E0:35260:3A:89542410", s) && s.prologue_len == 4 && s.code == 58);

    // Incomplete or malformed settings are refused (the feature then stays off).
    Spec t;
    CHECK(!parse(nullptr, t));
    CHECK(!parse("", t));
    CHECK(!parse("4098e0", t));
    CHECK(!parse("4098e0:35260", t));
    CHECK(!parse("4098e0:35260:3a", t));
    CHECK(!parse("4098e0:35260:3a:895424", t));        // fewer than 4 bytes to check
    CHECK(!parse("4098e0:35260:3a:8954241", t));       // half a byte
    CHECK(!parse("4098e0:35260:3a:89542410zz", t));    // not hex
    CHECK(!parse("0:35260:3a:89542410", t));
    CHECK(!parse("4098e0:35260:0:89542410", t));
    CHECK(!parse("4098e0:35260:3a:89542410535557488954241053555748ff", t));   // longer than 16 bytes
    CHECK(t.mgr_rva == 0 && t.prologue_len == 0);       // untouched on failure

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
