// The gun peripheral of Lost Land Adventure in rg-mfcam.dll (tools/rg-mfcam/dome_gun.h): 12-byte frames '_' player x y bits 0xf5,
// the shm mapping and the pacing of the emulated port.
#include <cstdio>
#include <cstring>
#include <string>

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

    // Aim grid (dome_grid.h): a bent forward map, inverted back onto the wanted screen point.
    {
        // Forward: x straight, y stretched in the top half (like the game: 0.3 -> 0.04, 0.5 -> 0.44, 0.75 -> 0.70).
        auto fx = [](float a, float b) { return 0.03f + 1.04f * a + 0.02f * (b - 0.5f); };
        auto fy = [](float a, float b) { return b < 0.5f ? 0.44f + 2.0f * (b - 0.5f) + 0.01f * a : 0.44f + 1.04f * (b - 0.5f) + 0.01f * a; };
        const float us[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        const float vs[] = {0.25f, 0.375f, 0.5f, 0.75f, 1.0f};
        std::string text = "# test grid\nplayer 1\nu 0 0.25 0.5 0.75 1\nv 0.25 0.375 0.5 0.75 1\n";
        for (float b : vs) {
            text += "row";
            for (float a : us) {
                char buf[64];
                std::snprintf(buf, sizeof buf, " %.5f %.5f", fx(a, b), fy(a, b));
                text += buf;
            }
            text += "\n";
        }
        Grid g[2];
        CHECK(parse_grids(text.c_str(), g) == 1);
        CHECK(g[0].valid() && !g[1].valid() && g[0].nu == 5 && g[0].nv == 5);
        // forward() reproduces the lattice points and interpolates in between.
        float x, y;
        g[0].forward(0.5f, 0.5f, x, y);
        CHECK(x > 0.549f && x < 0.551f && y > 0.444f && y < 0.446f);
        // invert(): the spot whose crosshair lands on the target, within 0.1 % of the screen.
        int bad = 0;
        for (float tx = 0.05f; tx < 0.96f; tx += 0.1f)
            for (float ty = 0.05f; ty < 0.96f; ty += 0.1f) {
                float a, b;
                g[0].invert(tx, ty, a, b);
                g[0].forward(a, b, x, y);
                if (x - tx > 1e-3f || tx - x > 1e-3f || y - ty > 1e-3f || ty - y > 1e-3f) ++bad;
            }
        CHECK(bad == 0);
        // A target beyond the reach of 0..1 is clamped, not wild.
        float a, b;
        g[0].invert(0.5f, -5.0f, a, b);
        CHECK(a >= 0 && a <= 1 && b == 0.0f);
        // The port sends the inverted spot for player 1 and the byte itself for player 2 (no grid).
        uint8_t sh[64] = {};
        sh[12] = 128;
        sh[16] = 128;
        sh[20] = 51;
        sh[24] = 204;
        Port pg;
        pg.grid[0] = &g[0];
        pg.grid[1] = &g[1];
        pg.refill(0, 16, sh);
        uint8_t fr[2 * kFrame];
        CHECK(pg.read(fr, sizeof fr) == sizeof fr);
        g[0].invert(axis(128), axis(128), a, b);
        CHECK(f_at(fr + 2) == a && f_at(fr + 6) == b && b > 0.52f);
        CHECK(f_at(fr + kFrame + 2) == axis(51) && f_at(fr + kFrame + 6) == axis(204));
        // Malformed or incomplete grids give no grid.
        Grid h[2];
        CHECK(parse_grids("player 1\nu 0 1\nv 0 1\nrow 0 0 1 0\n", h) == 0);                       // one row missing
        CHECK(parse_grids("player 2\nu 0 1\nv 0 1\nrow 0 0 1\nrow 0 1 1 1\n", h) == 0);           // short row
        CHECK(parse_grids("player 1\nu 1 0\nv 0 1\nrow 0 0 1 0\nrow 0 1 1 1\n", h) == 0);         // not increasing
        CHECK(parse_grids("player 2\nu 0 1\nv 0 1\nrow 0 0 1 0\nrow 0 1 1 1 # ok\r\n", h) == 1 && h[1].valid() && !h[0].valid());
        CHECK(parse_grids("", h) == 0 && parse_grids("u 0 1\n", h) == 0);
    }

    // The inverse form: the lattice lies over the screen and holds the spot to send; a bilinear lookup, clamped to the lattice.
    {
        // spot = (0.1 + 0.8 x, 0.3 + 0.5 y^2) sampled at x, y = 0, 0.5, 1 (bilinear is exact in x, approximate in y).
        const char* text = "player 2\nx 0 0.5 1\ny 0 0.5 1\nspot 0.1 0.3 0.5 0.3 0.9 0.3\nspot 0.1 0.425 0.5 0.425 0.9 0.425\nspot 0.1 0.8 0.5 0.8 0.9 0.8\n";
        Grid g[2];
        CHECK(parse_grids(text, g) == 1 && !g[0].valid() && g[1].valid() && g[1].inverse && g[1].nu == 3 && g[1].nv == 3);
        float a, b;
        g[1].invert(0.25f, 0.5f, a, b);
        CHECK(a > 0.299f && a < 0.301f && b > 0.424f && b < 0.426f);
        g[1].invert(1.0f, 1.0f, a, b);
        CHECK(a == 0.9f && b == 0.8f);
        g[1].invert(-3.0f, 7.0f, a, b);               // beyond the lattice: the edge value, no jump
        CHECK(a == 0.1f && b == 0.8f);
        CHECK(parse_grids("player 1\nx 0 1\ny 0 1\nspot 0 0 1 0\n", g) == 0);   // one row missing
    }

    CHECK(feedback_level('n', 0) == 0 && feedback_level('f', 0) == 1 && feedback_level('m', 0) == 2);
    CHECK(feedback_level('N', 1) == 0 && feedback_level('M', 1) == 2);
    CHECK(feedback_level('N', 0) == -1 && feedback_level(0, 1) == -1);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
