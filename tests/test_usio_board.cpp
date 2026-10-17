// Het USIO-bordmodel van winusb.dll voor Time Crisis 5 (tools/rg-winusb/usio_board.h): het 6-byte opdrachtprotocol dat TimeCrisisGame via
// WinUsb_WritePipe/ReadPipe spreekt, de registers die het spel leest en de afbeelding van het gedeelde geheugen (indeling TimeCrisis5).
// De opdrachtbytes zijn die van de code van het spel (0x140002f90 lezen, 0x1400030b0 schrijven).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../tools/rg-winusb/usio_board.h"

using namespace rg_usio5;

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
    void aim(uint8_t x, uint8_t y) { b[kOffP1X] = x; b[kOffP1Y] = y; }
};

// De opdracht die het spel schrijft: lezen (0x140002fbd..0x140002fcd) en schrijven (0x1400030e0..0x1400030f2).
void cmd_read(Board& bd, uint16_t addr, uint16_t len) {
    const uint8_t c[6] = {0x10, 0x00, static_cast<uint8_t>(addr), static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8)};
    CHECK(bd.out_transfer(c, 6));
}
void cmd_write_head(Board& bd, uint16_t addr, uint16_t len) {
    const uint8_t c[6] = {0x90, static_cast<uint8_t>(~(addr >> 8) & 0xF0), static_cast<uint8_t>(addr), static_cast<uint8_t>(addr >> 8),
                          static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8)};
    CHECK(bd.out_transfer(c, 6));
}
std::vector<uint8_t> do_read(Board& bd, uint16_t addr, uint16_t len) {
    cmd_read(bd, addr, len);
    std::vector<uint8_t> v(len, 0xEE);
    const size_t n = bd.in_transfer(v.data(), v.size());
    CHECK(n == len);
    return v;
}
void do_write(Board& bd, uint16_t addr, const std::vector<uint8_t>& data) {
    cmd_write_head(bd, addr, static_cast<uint16_t>(data.size()));
    CHECK(bd.pending_write_data());
    CHECK(bd.out_transfer(data.data(), data.size()));
    uint8_t ack = 0xEE;
    CHECK(bd.in_transfer(&ack, 1) == 1);   // de bevestiging van 1 byte; het spel kijkt alleen of de lezing slaagt
}
uint32_t le32(const std::vector<uint8_t>& v, size_t o) { return v[o] | (v[o + 1] << 8) | (v[o + 2] << 16) | (static_cast<uint32_t>(v[o + 3]) << 24); }

void test_opstart_zoals_het_spel() {
    // 0x140a553a0: foutwoord @2 moet 0 zijn, dan 0x220 bytes @0x2000 schrijven, woord @0x28 op 0x8173 zetten.
    Board bd;
    Shm s;
    bd.update(s.b);
    auto head = do_read(bd, 0x0000, 0x100);
    CHECK(head.size() == 0x100);
    CHECK(head[2] == 0 && head[3] == 0);   // geen foutscherm
    std::vector<uint8_t> cfg(0x220, 0xFF);
    do_write(bd, 0x2000, cfg);
    CHECK(do_read(bd, 0x2000, 4)[0] == 0xFF);
    do_write(bd, 0x0028, {0x73, 0x81});
    auto w = do_read(bd, 0x0028, 2);
    CHECK(w[0] == 0x73 && w[1] == 0x81);
    CHECK(bd.writes() == 2);
    CHECK(bd.strays() == 0);
}

void test_schrijfopdracht_bytes() {
    // byte 1 = ~(adres>>8) & 0xF0: voor 0x1420 is dat 0xE0, voor 0x2000 is het 0xD0 (zoals de code van het spel het uitrekent).
    const uint16_t a = 0x1420;
    CHECK(static_cast<uint8_t>(~(a >> 8) & 0xF0) == 0xE0);
    const uint16_t b = 0x2000;
    CHECK(static_cast<uint8_t>(~(b >> 8) & 0xF0) == 0xD0);
}

void test_uitgangen_en_alleen_lezen() {
    Board bd;
    Shm s;
    s.control(kCtlTest);
    bd.update(s.b);
    // Uitgangenblok (0x20 bytes @0x1420) wordt bewaard en is terug te lezen.
    std::vector<uint8_t> outs(0x20);
    for (size_t i = 0; i < outs.size(); ++i) outs[i] = static_cast<uint8_t>(i + 1);
    do_write(bd, kRegOutputs, outs);
    auto back = do_read(bd, kRegOutputs, 0x20);
    CHECK(back == outs);
    // Het schakelaarwoord en de munttellers zijn van het bord: schrijven verandert ze niet.
    const uint32_t before = bd.switches();
    do_write(bd, kRegSwitches, {0, 0, 0, 0});
    CHECK(bd.switches() == before && before == Mapping{}.test);
}

void test_schakelaars() {
    Board bd;
    Shm s;
    bd.update(s.b);
    CHECK(do_read(bd, kRegSwitches, 0x80).size() == 0x80);
    CHECK(bd.switches() == 0);
    s.control(kCtlEnter | kCtlService | kCtlPedalL | kCtlPedalR | kCtlGun);
    bd.update(s.b);
    auto v = do_read(bd, kRegSwitches, 0x80);
    const uint32_t sw = le32(v, 0);
    CHECK(sw == (0x200u | 0x4000u | 0x800u | 0x400u | 0x800000u));
    // De vijf bits die het spel zelf in UpdateUSIOForUE leest: 9, 10, 11, 14 en 23.
    const uint32_t spel_leest = (1u << 9) | (1u << 10) | (1u << 11) | (1u << 14) | (1u << 23);
    CHECK((sw & spel_leest) == spel_leest);
    // Test en selectie hebben hun eigen bits, de trekker staat standaard niet op het bord.
    s.control(kCtlTest | kCtlSelUp | kCtlSelDown | kCtlTrigger);
    bd.update(s.b);
    CHECK(bd.switches() == (0x80u | 0x2000u | 0x1000u));
    CHECK(bd.trigger());
}

void test_toewijzing_overschrijven() {
    Mapping m;
    int unknown = 0;
    CHECK(parse_mapping("pedal_l=0x100,pedal_r=0x200;gun=4,trigger=0x10,coin=1,onzin=3,test=xyz", m, &unknown) == 5);
    CHECK(unknown == 2);
    CHECK(m.pedal_l == 0x100 && m.pedal_r == 0x200 && m.gun == 4 && m.trigger == 0x10 && m.coin);
    CHECK(m.test == 0x80);   // niet genoemd: ongewijzigd
    Board bd(m);
    Shm s;
    s.control(kCtlPedalL | kCtlTrigger | kCtlGun);
    bd.update(s.b);
    CHECK(bd.switches() == (0x100u | 0x10u | 4u));
    CHECK(parse_mapping(nullptr, m) == 0);
    CHECK(parse_mapping("", m) == 0);
}

void test_munt() {
    // Standaard uit: de daemon geeft de munt als Shift+T aan het spel, dus het bord telt niet mee (anders dubbel tegoed).
    {
        Board bd;
        Shm s;
        s.control(kCtlCoin);
        bd.update(s.b);
        s.control(0);
        bd.update(s.b);
        CHECK(bd.coin1() == 0);
    }
    Mapping m;
    m.coin = true;
    Board bd(m);
    Shm s;
    bd.update(s.b);
    for (int i = 0; i < 3; ++i) {
        s.control(kCtlCoin);
        bd.update(s.b);
        bd.update(s.b);   // ingedrukt houden telt maar één keer
        s.control(0);
        bd.update(s.b);
    }
    CHECK(bd.coin1() == 3);
    auto v = do_read(bd, kRegCoin1, 4);
    CHECK(v[0] == 3 && v[1] == 0 && v[2] == 0 && v[3] == 0);   // teller 3, status 0 (bits 14-15), tweede teller 0
    // Omslag van de 14-bits teller laat de statusbits met rust.
    for (int i = 0; i < 0x4000; ++i) {
        s.control(kCtlCoin);
        bd.update(s.b);
        s.control(0);
        bd.update(s.b);
    }
    CHECK(bd.coin1() == 3);
    CHECK((bd.coin1() & 0xC000) == 0);
}

void test_aim() {
    Board bd;
    Shm s;
    s.aim(2, 253);
    bd.update(s.b);
    CHECK(bd.aim_x() == 2 && bd.aim_y() == 253);
}

void test_onbekende_overdrachten() {
    Board bd;
    const uint8_t kort[3] = {1, 2, 3};
    CHECK(!bd.out_transfer(kort, 3));
    const uint8_t onbekend[6] = {0x55, 0, 0, 0, 1, 0};
    CHECK(!bd.out_transfer(onbekend, 6));
    uint8_t x = 0;
    CHECK(bd.in_transfer(&x, 1) == 0);   // niets in behandeling
    CHECK(bd.strays() == 3);
    // Een schrijfopdracht met een gegevensblok van de verkeerde lengte wordt verworpen en laat het bord weer vrij.
    cmd_write_head(bd, 0x2000, 8);
    const uint8_t vier[4] = {1, 2, 3, 4};
    CHECK(!bd.out_transfer(vier, 4));
    CHECK(!bd.pending_write_data());
    // Buiten het registerbereik lezen geeft nullen in plaats van een crash.
    cmd_read(bd, 0x2FFE, 8);
    uint8_t buf[8];
    std::memset(buf, 0xEE, sizeof buf);
    CHECK(bd.in_transfer(buf, 8) == 8);
    CHECK(buf[2] == 0 && buf[7] == 0);
}

}  // namespace

int main() {
    test_opstart_zoals_het_spel();
    test_schrijfopdracht_bytes();
    test_uitgangen_en_alleen_lezen();
    test_schakelaars();
    test_toewijzing_overschrijven();
    test_munt();
    test_aim();
    test_onbekende_overdrachten();
    std::printf("%d controles, %d fouten\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
