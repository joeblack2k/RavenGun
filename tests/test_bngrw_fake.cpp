// De logica van het eigen Banapass-kaartlezer-antwoord van rg-mfcam.dll (tools/rg-mfcam/bngrw_fake.h).
#include <cstdio>

#include "../tools/rg-mfcam/bngrw_fake.h"

using namespace rg_bngrw;

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
    Fake f;
    // Voor Attach is er niets: aanvragen worden geweigerd, niets staat open.
    CHECK(f.simple_request(0) == 0);
    CHECK(f.dev_reset(0) == 0);
    CHECK(f.wait_touch(0) == 0);
    CHECK(f.is_cmd_exec(0) == 0 && f.is_cmd_exec(1) == 0);

    // Twee lezers, zoals het spel ze aanmaakt (lezer 0 = COM4, lezer 1 = COM2).
    CHECK(f.attach(0) == kAccepted);
    CHECK(f.attach(1) == kAccepted);
    CHECK(f.attach(2) == 0 && f.attach(-1) == 0);   // een derde lezer bestaat niet
    CHECK(f.dev_reset(0) == kAccepted && f.dev_reset(1) == kAccepted);
    CHECK(f.simple_request(0) == kAccepted && f.simple_request(1) == kAccepted);   // led, beep, url

    // ReqWaitTouch blijft open (IsCmdExec = 1) tot ReqCancel; per lezer onafhankelijk; twee tegelijk mag niet.
    CHECK(f.wait_touch(0) == kAccepted);
    CHECK(f.is_cmd_exec(0) == 1 && f.is_cmd_exec(1) == 0);
    CHECK(f.wait_touch(0) == 0);
    CHECK(f.wait_touch(1) == kAccepted);
    CHECK(f.is_cmd_exec(1) == 1);
    CHECK(f.cancel(0));
    CHECK(f.is_cmd_exec(0) == 0 && f.is_cmd_exec(1) == 1);
    CHECK(!f.cancel(0));   // niets meer open
    CHECK(f.wait_touch(0) == kAccepted);   // opnieuw kan

    // Een reset haalt een openstaande aanvraag weg.
    CHECK(f.dev_reset(0) == kAccepted && f.is_cmd_exec(0) == 0);

    // Fin zet alles terug.
    f.fin();
    CHECK(f.is_cmd_exec(1) == 0 && f.simple_request(0) == 0);
    CHECK(!Fake::valid(2) && Fake::valid(0) && Fake::valid(1));

    std::printf("bngrw_fake: %d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
