// The decision logic of the JVS redirection in rg-mfcam.dll (tools/rg-mfcam/jvs_com.h): which file name counts as the JVS port and which
// sense line the game gets to see. The values are those of Mario Kart Arcade GP DX (CreateFileA("COM3")) and of the OpenParrot source.
#include <cstdio>

#include "../tools/rg-mfcam/jvs_com.h"

using namespace rg_jvscom;

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
    // Both Windows spellings; case does not matter.
    CHECK(port_matches("COM3", "COM3"));
    CHECK(port_matches("com3", "COM3"));
    CHECK(port_matches("\\\\.\\COM3", "COM3"));
    CHECK(port_matches("\\\\.\\com3", "Com3"));
    // Other ports stay with the game (card reader, ticket): COM30 and COM13 do not match either.
    CHECK(!port_matches("COM1", "COM3"));
    CHECK(!port_matches("COM30", "COM3"));
    CHECK(!port_matches("COM13", "COM3"));
    CHECK(!port_matches("COM", "COM3"));
    CHECK(!port_matches("\\\\.\\pipe\\TeknoParrot_JVS", "COM3"));
    CHECK(!port_matches("F:\\Fsystem\\x.bin", "COM3"));
    // Empty or missing = off.
    CHECK(!port_matches("COM3", ""));
    CHECK(!port_matches("COM3", nullptr));
    CHECK(!port_matches(nullptr, "COM3"));
    // Sense line: without an address only CTS, with an address CTS+DSR.
    CHECK(modem_status(false) == 0x10u);
    CHECK(modem_status(true) == 0x30u);
    std::printf("jvs_com: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
