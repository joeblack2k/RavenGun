// The logic of rg-mfcam.dll's own Banapass card reader reply (tools/rg-mfcam/bngrw_fake.h).
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
    // Before Attach there is nothing: requests are refused, nothing is open.
    CHECK(f.simple_request(0) == 0);
    CHECK(f.dev_reset(0) == 0);
    CHECK(f.wait_touch(0) == 0);
    CHECK(f.is_cmd_exec(0) == 0 && f.is_cmd_exec(1) == 0);

    // Two readers, as the game creates them (reader 0 = COM4, reader 1 = COM2).
    CHECK(f.attach(0) == kAccepted);
    CHECK(f.attach(1) == kAccepted);
    CHECK(f.attach(2) == 0 && f.attach(-1) == 0);   // a third reader does not exist
    CHECK(f.dev_reset(0) == kAccepted && f.dev_reset(1) == kAccepted);
    CHECK(f.simple_request(0) == kAccepted && f.simple_request(1) == kAccepted);   // led, beep, url

    // ReqWaitTouch stays open (IsCmdExec = 1) until ReqCancel; independent per reader; two at once is not allowed.
    CHECK(f.wait_touch(0) == kAccepted);
    CHECK(f.is_cmd_exec(0) == 1 && f.is_cmd_exec(1) == 0);
    CHECK(f.wait_touch(0) == 0);
    CHECK(f.wait_touch(1) == kAccepted);
    CHECK(f.is_cmd_exec(1) == 1);
    CHECK(f.cancel(0));
    CHECK(f.is_cmd_exec(0) == 0 && f.is_cmd_exec(1) == 1);
    CHECK(!f.cancel(0));   // nothing open any more
    CHECK(f.wait_touch(0) == kAccepted);   // can be started again

    // A reset removes a pending request.
    CHECK(f.dev_reset(0) == kAccepted && f.is_cmd_exec(0) == 0);

    // Fin resets everything.
    f.fin();
    CHECK(f.is_cmd_exec(1) == 0 && f.simple_request(0) == 0);
    CHECK(!Fake::valid(2) && Fake::valid(0) && Fake::valid(1));

    std::printf("bngrw_fake: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
