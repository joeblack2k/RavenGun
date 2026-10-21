// The synthetic camera size of rg-mfcam.dll (tools/rg-mfcam/cam_select.h): RG_MFCAM_SYNTHETIC=WxH within the feed's limits.
#include <cstdio>

#include "../tools/rg-mfcam/cam_select.h"

using namespace rg_camsel;

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
    uint32_t w = 7, h = 9;
    CHECK(parse_size("640x480", w, h) && w == 640 && h == 480);
    CHECK(parse_size("1280X720", w, h) && w == 1280 && h == 720);
    CHECK(parse_size("4096x4096", w, h) && w == 4096 && h == 4096);
    CHECK(parse_size("2x1", w, h) && w == 2 && h == 1);
    // Rejected: w and h stay as they were.
    w = 7;
    h = 9;
    const char* bad[] = {"", "x", "640", "640x", "x480", "640x480 ", " 640x480", "641x480", "0x480", "640x0",
                         "4098x480", "640x4097", "640*480", "-640x480", "640x480x2", "99999999999x1"};
    for (const char* b : bad) {
        const bool ok = parse_size(b, w, h);
        CHECK(!ok);
        if (ok) std::fprintf(stderr, "  accepted: \"%s\"\n", b);
    }
    CHECK(!parse_size(nullptr, w, h));
    CHECK(w == 7 && h == 9);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
