// The gun peripheral of Lost Land Adventure in rg-mfcam.dll (tools/rg-mfcam/dome_gun.h): 12-byte frames '_' player x y bits 0xf5,
// the shm mapping and the pacing of the emulated port.
#include <cstdio>
#include <cstring>

#include "../tools/rg-mfcam/dome_gun.h"

using namespace rg_domegun;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static float f_at(const uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

int main() {
    // A frame as 0x140a2edb0 reads it: sync, player, two floats, bits, end byte.
    uint8_t f[kFrame];
    make_frame(f, 1, 0.25f, 0.75f, 0x05);
    CHECK(f[0] == 0x5f && f[1] == 1 && f[10] == 0x05 && f[11] == 0xf5);
    CHECK(f_at(f + 2) == 0.25f && f_at(f + 6) == 0.75f);

    CHECK(axis(0) == 0.0f && axis(255) == 1.0f);
    CHECK(axis(128) > 0.50f && axis(128) < 0.51f);

    CHECK(buttons(0) == 0);
    CHECK(buttons(0x1) == 0x01);        // player 1 trigger -> bit 0 (bits & 3)
    CHECK(buttons(0x2) == 0x04);        // player 2 trigger -> bit 2 (bits & 0xc)
    CHECK(buttons(0x3 | 0x10) == 0x05); // other control bits (TEST, coin, ...) do not reach the gun

    // The port: one frame per player per refill, paced, never more than the game keeps.
    uint8_t shm[64] = {};
    shm[8] = 0x1;
    shm[12] = 255;
    shm[16] = 0;
    shm[20] = 51;
    shm[24] = 204;
    Port port;
    port.refill(1000, 16, shm);
    CHECK(port.n == 2 * kFrame);
    uint8_t got[64];
    CHECK(port.read(got, 1) == 1 && got[0] == 0x5f);
    CHECK(port.read(got, 11) == 11 && got[0] == 0 && f_at(got + 1) == 1.0f && f_at(got + 5) == 0.0f && got[9] == 0x01 && got[10] == 0xf5);
    CHECK(port.n == kFrame);
    port.refill(2000, 16, shm);                     // a frame still waiting: nothing added
    CHECK(port.n == kFrame);
    CHECK(port.read(got, 12) == 12 && got[1] == 1 && f_at(got + 2) == 0.2f && f_at(got + 6) == 0.8f);
    port.refill(1010, 16, shm);                     // too soon after the last refill
    CHECK(port.n == 0);
    port.refill(1016, 16, shm);
    CHECK(port.n == 2 * kFrame && port.n <= kQueueMax);
    CHECK(port.read(got, 64) == 2 * kFrame && port.n == 0);
    // Without shared memory: centre, no buttons.
    Port p2;
    p2.refill(5, 16, nullptr);
    CHECK(p2.read(got, 12) == 12 && got[10] == 0 && f_at(got + 2) == axis(128));

    CHECK(feedback_level('n', 0) == 0 && feedback_level('f', 0) == 1 && feedback_level('m', 0) == 2);
    CHECK(feedback_level('N', 1) == 0 && feedback_level('M', 1) == 2);
    CHECK(feedback_level('N', 0) == -1 && feedback_level(0, 1) == -1);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
