// Het USIO-model van rg-usio.dll (tools/rg-usio/usio_model.h): de afbeelding van het gedeelde geheugen (indeling PointBlankX) op wat Point
// Blank X via bnusio.dll leest. Zelfde klasse als in de DLL; de waarden staan vast aan wat het spel zelf eist (BNUsio.cs, BNUsioController.cs).
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../tools/rg-usio/usio_model.h"

using namespace rg_usio;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

struct Shm {
    uint8_t b[kShmSize] = {};
    void control(uint32_t v) { std::memcpy(b + kOffControl, &v, 4); }
    void aim(uint8_t p1x, uint8_t p1y, uint8_t p2x = 0, uint8_t p2y = 0) {
        b[kOffP1X] = p1x; b[kOffP1Y] = p1y; b[kOffP2X] = p2x; b[kOffP2Y] = p2y;
    }
};

void test_axis_scale() {
    // 0 -> 0 en 255 -> 1023 << 6 (0xFFC0); de tussenwaarden stijgen strikt en blijven onder 0x10000 (de teller deelt door 65535).
    CHECK(PointBlankModel::scale_axis(0) == 0);
    CHECK(PointBlankModel::scale_axis(255) == 0xFFC0);
    uint32_t prev = 0;
    for (int v = 1; v < 256; ++v) {
        const uint32_t cur = PointBlankModel::scale_axis(static_cast<uint8_t>(v));
        CHECK(cur > prev);
        CHECK((cur & 0x3F) == 0);   // lage zes bits blijven nul zoals bij het echte bord
        prev = cur;
    }
    CHECK(PointBlankModel::scale_axis(128) == ((128u * 1023u + 127u) / 255u) << 6);
}

void test_aim_and_flip() {
    Shm s;
    s.aim(0, 0, 255, 255);
    PointBlankModel m(true);
    m.update(s.b);
    CHECK(m.gun_x(1) == 0);                    // links = 0
    CHECK(m.gun_y(1) == 0xFFC0);               // boven in de daemon (0) is hoog in Unity: omgekeerd
    CHECK(m.gun_x(2) == 0xFFC0);
    CHECK(m.gun_y(2) == 0);
    CHECK(m.gun_x(0) == 0 && m.gun_y(3) == 0); // onbekende speler

    PointBlankModel n(false);                  // RG_USIO_FLIP_Y=0
    n.update(s.b);
    CHECK(n.gun_y(1) == 0);
    CHECK(n.gun_y(2) == 0xFFC0);
}

void test_switches() {
    Shm s;
    PointBlankModel m;
    m.update(s.b);
    CHECK(m.sw_in() == 0);

    s.control(kP1Start);
    m.update(s.b);
    CHECK(m.sw_in() == 0x02000000u);           // usio_IsSwOn(33554432) in BNUsioController
    s.control(kP2Start | kTest);
    m.update(s.b);
    CHECK(m.sw_in() == (0x01000000u | 0x80u));
    s.control(kUp | kDown | kEnter);
    m.update(s.b);
    CHECK(m.sw_in() == (0x2000u | 0x1000u | 0x200u));
    CHECK(m.sw_in64() == m.sw_in());
    // De triggers zijn geen schakelaars: ze lopen via gun_IsTriggered.
    s.control(kP1Trigger | kP2Trigger);
    m.update(s.b);
    CHECK(m.sw_in() == 0);
    CHECK(m.gun_triggered(1) && m.gun_triggered(2) && !m.gun_triggered(0));
    s.control(kP2Trigger);
    m.update(s.b);
    CHECK(!m.gun_triggered(1) && m.gun_triggered(2));
}

void test_coins() {
    Shm s;
    PointBlankModel m;
    m.update(s.b);
    CHECK(m.coin(0) == 0);
    // Vasthouden telt maar een keer; loslaten en opnieuw drukken telt weer.
    s.control(kCoin);
    m.update(s.b); m.update(s.b); m.update(s.b);
    CHECK(m.coin(0) == 1);
    s.control(0);
    m.update(s.b);
    s.control(kCoin);
    m.update(s.b);
    CHECK(m.coin(0) == 2);
    CHECK(m.coin(1) == 0);                     // slot 2 bestaat niet
    m.dec_coin(0, 1);
    CHECK(m.coin(0) == 1);
    m.dec_coin(0, 5);                          // niet onder nul
    CHECK(m.coin(0) == 0);
    // Service: eigen teller en ook de schakelaar SW_SERVICE.
    s.control(kService);
    m.update(s.b);
    CHECK(m.service(0) == 1);
    CHECK((m.sw_in() & 0x4000u) != 0);
    m.dec_service(0, 1);
    CHECK(m.service(0) == 0);
    s.control(kCoin);
    s.control(kCoin | kService);
    m.update(s.b);
    m.reset_coin();
    CHECK(m.coin(0) == 0 && m.service(0) == 0);
}

void test_coin_saturation() {
    Shm s;
    PointBlankModel m;
    for (int i = 0; i < 70000; ++i) {
        s.control(i % 2 ? 0u : static_cast<uint32_t>(kCoin));
        m.update(s.b);
    }
    CHECK(m.coin(0) == 35000);                 // 70000 / 2 pulsen, nog onder de grens
    for (int i = 0; i < 80000; ++i) {
        s.control(i % 2 ? 0u : static_cast<uint32_t>(kCoin));
        m.update(s.b);
    }
    CHECK(m.coin(0) == 0xFFFF);                // verzadigt, loopt niet over
}

void test_outputs_and_limits() {
    PointBlankModel m;
    m.set_gout(15, 15);
    m.set_gout(16, 0);
    CHECK(m.gout(15) == 15 && m.gout(16) == 0);
    m.set_gout(200, 1);                        // buiten de tabel: genegeerd
    CHECK(m.gout(200) == 0);
    CHECK(m.gun_in_range(1) && m.gun_in_range(2) && !m.gun_in_range(0));
    CHECK(kFirmwareVersion >= 120 && kFirmwareVersion <= 126);   // het spel meldt anders I/O ERROR 2
}

}  // namespace

int main() {
    test_axis_scale();
    test_aim_and_flip();
    test_switches();
    test_coins();
    test_coin_saturation();
    test_outputs_and_limits();
    std::printf("usio_model: %d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
