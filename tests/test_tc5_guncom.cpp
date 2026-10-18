// Het seriele geweer van Time Crisis 5 (tools/rg-winusb/gun_com.h): opdrachtbyte -> kader van 11 bytes met X1, Y1, vlaggen en controlesom, en de
// afbeelding van het gedeelde geheugen (indeling TimeCrisis5) op die velden. De kaderindeling en de controlesom zijn die van de code van het spel
// (transactie 0x140a547b0, ontleding opdracht 0 op 0x140a549e6, vlaggen 0x140a55035..0x140a550b4); de test leest het kader zoals het spel dat doet.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../tools/rg-winusb/gun_com.h"

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
    uint8_t b[kGunShmSize] = {};
    void control(uint32_t v) { std::memcpy(b + kGunOffControl, &v, 4); }
    void aim(uint8_t x, uint8_t y) { b[kGunOffP1X] = x; b[kGunOffP1Y] = y; }
};

// Zoals het spel het kader leest (0x140a549e6): X1 = b0<<8|b1, Y1 = b2<<8|b3, vlaggen = b8; controlesom = ~som(b0..b9) in b10.
struct Parsed { unsigned x, y, flags; bool checksum_ok; };
Parsed parse_poll(const uint8_t* b) {
    Parsed p;
    p.x = static_cast<unsigned>(b[0] << 8 | b[1]);
    p.y = static_cast<unsigned>(b[2] << 8 | b[3]);
    p.flags = b[8];
    unsigned sum = 0;
    for (int i = 0; i < 10; ++i) sum += b[i];
    p.checksum_ok = static_cast<uint8_t>(~sum & 0xFFu) == b[10];
    return p;
}

// Een volledige transactie: WriteFile(code), dan ReadFile in stukken zoals het spel dat doet (één keer de hele lengte).
size_t transact(GunCom& g, uint8_t code, uint8_t* out, size_t cap) {
    CHECK(g.write(&code, 1) == gun_reply_len(code));
    return g.read(out, cap);
}

void test_poll_kader_en_controlesom() {
    GunCom g;
    Shm s;
    s.aim(128, 128);
    g.update(s.b);
    uint8_t b[11] = {};
    CHECK(transact(g, 0x08, b, sizeof b) == 11);
    const Parsed p = parse_poll(b);
    CHECK(p.checksum_ok);
    CHECK(p.x == 514 || p.x == 513);   // 128/255 van 1023 = 513.5
    CHECK(p.y == 509 || p.y == 510);   // omgekeerd: 255-128 = 127 -> 509.5
    CHECK(p.flags == 0);
    CHECK(g.polls() == 1);
    CHECK(g.pending() == 0);           // alles gelezen
}

void test_assen_en_richting() {
    GunCom g;
    Shm s;
    uint8_t b[11];
    // Linksboven (0,0): X1 = 0, Y1 = 1023 (Y1 = 0 is onderaan: het script doet 0x3ff - Y1 en krijgt dan 0 = boven).
    s.aim(0, 0);
    g.update(s.b);
    transact(g, 0x08, b, sizeof b);
    Parsed p = parse_poll(b);
    CHECK(p.x == 0 && p.y == 1023 && p.checksum_ok);
    // Rechtsonder (255,255): X1 = 1023, Y1 = 0.
    s.aim(255, 255);
    g.update(s.b);
    transact(g, 0x08, b, sizeof b);
    p = parse_poll(b);
    CHECK(p.x == 1023 && p.y == 0 && p.checksum_ok);
    // Het script rekent X1/1024 en (1023-Y1)/1024: linksboven moet (0,0) geven, rechtsonder bijna (1,1).
    const double ox = p.x / 1024.0, oy = (1023 - p.y) / 1024.0;
    CHECK(ox > 0.99 && oy > 0.99);
    // Monotoon: een grotere aim-X geeft een grotere X1.
    unsigned prev = 0;
    for (int ax = 0; ax <= 255; ax += 5) {
        s.aim(static_cast<uint8_t>(ax), 100);
        g.update(s.b);
        transact(g, 0x08, b, sizeof b);
        const Parsed q = parse_poll(b);
        CHECK(q.x >= prev && q.x <= 1023);
        prev = q.x;
    }
}

void test_trekker_en_buiten_beeld() {
    GunCom g;
    Shm s;
    uint8_t b[11];
    // De daemon-bit 0x10 (p1.trigger) wordt vlag bit 5 (0x20) in byte 8: Trigger1 van het script (+0x2fc).
    s.aim(100, 100);
    s.control(0);
    g.update(s.b);
    transact(g, 0x08, b, sizeof b);
    CHECK((parse_poll(b).flags & 0x20) == 0);
    s.control(kGunCtlTrigger);
    g.update(s.b);
    transact(g, 0x08, b, sizeof b);
    Parsed p = parse_poll(b);
    CHECK((p.flags & 0x20) != 0 && p.checksum_ok);
    CHECK((p.flags & 0x80) == 0);   // binnen beeld
    // Andere daemon-bits (gunknop 0x20, pedalen 0x40/0x80, munt 0x08) raken de trekkervlag niet: die lopen over het USIO-bord of toets.
    s.control(0x20 | 0x40 | 0x80 | 0x08 | 0x400);
    g.update(s.b);
    transact(g, 0x08, b, sizeof b);
    CHECK((parse_poll(b).flags & 0x20) == 0);
    // Buiten beeld: een as <= 1 of >= 254 zet bit 7 (de offscreen-regel van het profiel).
    s.control(0);
    s.aim(1, 100);
    g.update(s.b);
    transact(g, 0x08, b, sizeof b);
    CHECK((parse_poll(b).flags & 0x80) != 0);
    s.aim(100, 254);
    g.update(s.b);
    transact(g, 0x08, b, sizeof b);
    CHECK((parse_poll(b).flags & 0x80) != 0);
    s.aim(2, 253);
    g.update(s.b);
    transact(g, 0x08, b, sizeof b);
    CHECK((parse_poll(b).flags & 0x80) == 0);   // 2..253 is binnen beeld
}

void test_overige_opdrachten() {
    GunCom g;
    uint8_t b[64];
    // 0x14 / 0x18 (SetLEDFreqForUE), 0x0c, 0x04, 0xff: twee bytes {b, ~b}.
    const uint8_t codes[] = {0x14, 0x18, 0x0c, 0x04, 0xff};
    for (uint8_t c : codes) {
        CHECK(transact(g, c, b, sizeof b) == 2);
        CHECK(static_cast<uint8_t>(~b[0] & 0xFF) == b[1]);
    }
    // 0x1c: kader van 11 bytes met tienbits-pakket en geldige controlesom.
    CHECK(transact(g, 0x1c, b, sizeof b) == 11);
    CHECK(parse_poll(b).checksum_ok);
    // 0x10 (de versievraag): 31 bytes, het eerste byte is 'R', per byte te lezen zoals het spel dat doet.
    const uint8_t v = 0x10;
    CHECK(g.write(&v, 1) == 31);
    uint8_t id[31];
    for (int i = 0; i < 31; ++i) CHECK(g.read(&id[i], 1) == 1);
    CHECK(id[0] == 'R');
    CHECK(g.read(b, 1) == 0);
    // Een onbekende code geeft geen antwoord (het spel telt dat als mislukte transactie).
    const uint8_t onbekend = 0x55;
    CHECK(g.write(&onbekend, 1) == 0);
    CHECK(g.read(b, sizeof b) == 0);
    CHECK(g.unknown() == 1);
}

void test_lezen_in_stukken_en_purge() {
    GunCom g;
    Shm s;
    s.aim(10, 20);
    g.update(s.b);
    const uint8_t cmd = 0x08;
    g.write(&cmd, 1);
    uint8_t a[4], b[7], c[1];
    CHECK(g.read(a, 4) == 4);
    CHECK(g.pending() == 7);
    CHECK(g.read(b, 7) == 7);
    CHECK(g.read(c, 1) == 0);   // meer vragen dan er ligt: 0 bytes, het spel ziet een mislukte lezing
    // PurgeComm gooit een onafgemaakt antwoord weg.
    g.write(&cmd, 1);
    CHECK(g.pending() == 11);
    g.purge();
    CHECK(g.pending() == 0);
    // Een nieuwe opdracht vervangt een niet-afgelezen antwoord (de volgende transactie begint met PurgeComm, maar toch).
    g.write(&cmd, 1);
    g.write(&cmd, 1);
    CHECK(g.pending() == 11);
}

void test_afbeelding_overschrijven() {
    GunMap m;
    int unknown = 0;
    CHECK(parse_gunmap("x0=100,x1=900,y0=50,y1=950,flipy=0,oof=0,trig=0x10", m, &unknown) == 7);
    CHECK(unknown == 0);
    CHECK(m.x0 == 100 && m.x1 == 900 && m.y0 == 50 && m.y1 == 950 && !m.flip_y && !m.oof && m.trig_flag == 0x10);
    CHECK(parse_gunmap("onzin=1,x0=abc", m, &unknown) == 0);
    CHECK(unknown == 2);
    GunCom g(m);
    Shm s;
    s.aim(0, 0);
    s.control(kGunCtlTrigger);
    g.update(s.b);
    CHECK(g.raw_x() == 100);
    CHECK(g.raw_y() == 50);          // flipy=0: aim 0 -> y0
    CHECK(g.flags() == 0x10);        // trekker op bit 4, en oof uit (aim 0 is anders buiten beeld)
    s.aim(255, 255);
    g.update(s.b);
    CHECK(g.raw_x() == 900 && g.raw_y() == 950);
    // Standaard 0..1023 wordt nooit overschreden, ook niet met een rare afbeelding.
    GunMap wild;
    wild.x0 = -500; wild.x1 = 5000;
    GunCom w(wild);
    s.aim(0, 0); w.update(s.b);
    CHECK(w.raw_x() == 0);
    s.aim(255, 0); w.update(s.b);
    CHECK(w.raw_x() == 1023);
}

}  // namespace

int main() {
    test_poll_kader_en_controlesom();
    test_assen_en_richting();
    test_trekker_en_buiten_beeld();
    test_overige_opdrachten();
    test_lezen_in_stukken_en_purge();
    test_afbeelding_overschrijven();
    std::printf("%d controles, %d fouten\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
