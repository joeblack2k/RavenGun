// De beslislogica van de JVS-omleiding in rg-mfcam.dll (tools/rg-mfcam/jvs_com.h): welke bestandsnaam voor de JVS-poort telt en welke
// sense-lijn het spel te zien krijgt. De waarden zijn die van Mario Kart Arcade GP DX (CreateFileA("COM3")) en van de OpenParrot-bron.
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
    // Beide schrijfwijzen van Windows, hoofdletters maken niet uit.
    CHECK(port_matches("COM3", "COM3"));
    CHECK(port_matches("com3", "COM3"));
    CHECK(port_matches("\\\\.\\COM3", "COM3"));
    CHECK(port_matches("\\\\.\\com3", "Com3"));
    // Andere poorten blijven van het spel (kaartlezer, ticket): ook COM30 en COM13 horen er niet bij.
    CHECK(!port_matches("COM1", "COM3"));
    CHECK(!port_matches("COM30", "COM3"));
    CHECK(!port_matches("COM13", "COM3"));
    CHECK(!port_matches("COM", "COM3"));
    CHECK(!port_matches("\\\\.\\pipe\\TeknoParrot_JVS", "COM3"));
    CHECK(!port_matches("F:\\Fsystem\\x.bin", "COM3"));
    // Leeg of ontbrekend = uit.
    CHECK(!port_matches("COM3", ""));
    CHECK(!port_matches("COM3", nullptr));
    CHECK(!port_matches(nullptr, "COM3"));
    // Sense-lijn: zonder adres alleen CTS, met adres CTS+DSR.
    CHECK(modem_status(false) == 0x10u);
    CHECK(modem_status(true) == 0x30u);
    std::printf("jvs_com: %d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
