// JVS-kern en dialect: framing (sync, escapes, checksum) met vaste byte-reeksen, de parser in stukken en met foute
// packets, adressering van een keten van twee nodes, alle commando's van het bord (identificatie, versies, functies,
// schakelaars, munten, analoog met kalibratievenster, uitgangen), de borden uit packaging/jvs-boards.json, en het dialect
// over een echte pty waarbij de test de game speelt. De verwachte bytes zijn met de hand (en een los script) uit de
// JVS-specificatie berekend, niet uit de implementatie gehaald.
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ravengun/iobridge/jvs.hpp"
#include "ravengun/iobridge/jvs_dialect.hpp"
#include "ravengun/json_parse.hpp"

using namespace ravengun;
using namespace ravengun::iobridge;
namespace fs = std::filesystem;

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

using Bytes = std::vector<uint8_t>;

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string hex(const Bytes& v) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < v.size(); ++i) {
        std::snprintf(b, sizeof b, "%02X", v[i]);
        if (i) s += ' ';
        s += b;
    }
    return s;
}

Bytes from_hex(const char* s) {
    Bytes v;
    unsigned x;
    int n;
    while (std::sscanf(s, " %2x%n", &x, &n) == 1) { v.push_back(static_cast<uint8_t>(x)); s += n; }
    return v;
}

JvsBoards load() {
    std::string err;
    JvsBoards b = parse_jvs_boards(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/jvs-boards.json"), err);
    CHECK(err.empty());
    if (!err.empty()) std::fprintf(stderr, "%s\n", err.c_str());
    return b;
}

const JvsBoard& lgs() {
    static const JvsBoards bs = load();
    return *bs.find("LGS");
}

// Zet een volledig packet door de bus (parser + commando's) en geeft de bytes voor de draad.
Bytes talk(JvsBus& bus, const char* wire_hex) {
    JvsFrameParser p;
    std::vector<JvsFrame> fr;
    const Bytes in = from_hex(wire_hex);
    p.feed(in.data(), in.size(), fr);
    Bytes out;
    for (const auto& f : fr) {
        Bytes r = bus.process(f);
        out.insert(out.end(), r.begin(), r.end());
    }
    return out;
}

// Idem, maar het verzoek wordt uit node en gegevens opgebouwd (zonder de checksum met de hand te rekenen).
Bytes ask(JvsBus& bus, uint8_t node, const char* data_hex) {
    JvsFrameParser p;
    std::vector<JvsFrame> fr;
    const Bytes in = jvs_encode(node, from_hex(data_hex));
    p.feed(in.data(), in.size(), fr);
    Bytes out;
    for (const auto& f : fr) {
        Bytes r = bus.process(f);
        out.insert(out.end(), r.begin(), r.end());
    }
    return out;
}

IoState armed_state() {
    IoState s;
    s.armed = true;
    return s;
}

// ---------------------------------------------------------------- framing

void test_framing() {
    CHECK(jvs_checksum(from_hex("FF 03 F0 D9")) == 0xCB);                       // de bekende reset
    CHECK(jvs_checksum(from_hex("FF FF")) == 0xFE);                              // modulo 256
    CHECK(hex(jvs_encode(0xFF, from_hex("F0 D9"))) == "E0 FF 03 F0 D9 CB");
    CHECK(hex(jvs_encode(0xFF, from_hex("F1 01"))) == "E0 FF 03 F1 01 F4");
    CHECK(hex(jvs_encode(0x00, from_hex("01 01"))) == "E0 00 03 01 01 05");
    // Escapes: E0 en D0 in de gegevens; de checksum (B7) is over de ongestuffe bytes.
    CHECK(hex(jvs_encode(0x00, from_hex("01 01 E0 D0"))) == "E0 00 05 01 01 D0 DF D0 CF B7");
    CHECK(hex(jvs_escape(from_hex("E0 D0 00 CF DF"))) == "D0 DF D0 CF 00 CF DF");

    // Parser: een packet in een keer, byte voor byte en met troep ervoor.
    JvsFrameParser p;
    std::vector<JvsFrame> fr;
    Bytes b = from_hex("E0 00 05 01 01 D0 DF D0 CF B7");
    p.feed(b.data(), b.size(), fr);
    CHECK(fr.size() == 1 && fr[0].checksum_ok && fr[0].node == 0 && hex(fr[0].data) == "01 01 E0 D0");
    fr.clear();
    for (uint8_t x : b) p.feed(&x, 1, fr);
    CHECK(fr.size() == 1 && fr[0].checksum_ok && hex(fr[0].data) == "01 01 E0 D0");
    fr.clear();
    b = from_hex("12 34 E0 FF 03 F0 D9 CB E0 FF 03 F1 01 F4");
    p.feed(b.data(), b.size(), fr);
    CHECK(fr.size() == 2 && fr[0].node == 0xFF && hex(fr[0].data) == "F0 D9" && hex(fr[1].data) == "F1 01" && p.garbage() == 2);
    // Fout in de checksum: het packet komt wel door, met de vlag.
    fr.clear();
    b = from_hex("E0 FF 03 F1 01 F5");
    p.feed(b.data(), b.size(), fr);
    CHECK(fr.size() == 1 && !fr[0].checksum_ok && fr[0].checksum_wire == 0xF5 && fr[0].checksum_calc == 0xF4);
    // Een sync midden in een packet begint opnieuw (de eerste helft is weg).
    fr.clear();
    const uint64_t rs = p.resyncs();
    b = from_hex("E0 FF 05 F1 E0 FF 03 F1 01 F4");
    p.feed(b.data(), b.size(), fr);
    CHECK(fr.size() == 1 && hex(fr[0].data) == "F1 01" && p.resyncs() == rs + 1);
    // Lengte 0 is geen geldig packet.
    fr.clear();
    b = from_hex("E0 01 00 E0 FF 03 F1 01 F4");
    p.feed(b.data(), b.size(), fr);
    CHECK(fr.size() == 1);
}

// ---------------------------------------------------------------- bord en commando's

void test_board_file() {
    JvsBoards bs = load();
    const JvsBoard* l = bs.find("LGS");
    CHECK(l != nullptr);
    if (!l) return;
    CHECK(l->identify == "SEGA CORPORATION;I/O BD JVS;837-14895;1;1;FE MODE;2005/10");
    CHECK(l->comm_version == 0x30 && l->jvs_version == 0x30 && l->command_rev == 0x30);
    CHECK(l->players == 1 && l->buttons == 22 && jvs_switch_bytes(*l) == 3 && l->coin_slots == 2 && l->gp_outputs == 16);
    const JvsBoard* g = bs.find("SegaGun2P");
    CHECK(g && g->identify == "SEGA CORPORATION;I/O BD JVS;837-14572;Ver1.00;2005/10" && g->comm_version == 0x10 && g->jvs_version == 0x20 &&
          g->command_rev == 0x13 && g->players == 2 && g->buttons == 14 && jvs_switch_bytes(*g) == 2 && g->analog_channels == 8);
    for (const auto& b : bs.boards) CHECK(validate_jvs_board(b).empty());
    // TaitoGun: hetzelfde bord, START van beide spelers op P1 omhoog en omlaag (TeknoParrot-profielen Gaia Attack 4, Haunted Museum).
    const JvsBoard* t = bs.find("TaitoGun");
    CHECK(t && g && t->identify == g->identify && t->analog_channels == g->analog_channels);
    if (t) {
        JvsBus bus;
        bus.add_node(*t);
        talk(bus, "E0 FF 03 F1 01 F4");
        IoState s = armed_state();
        s.player[1].start = true;
        bus.set_input(s);
        CHECK(hex(ask(bus, 0x01, "20 01 03")) == "E0 00 07 01 01 00 10 00 00 19");   // P2 START = P1 omlaag (bit 4)
    }

    std::string err;
    parse_jvs_boards("{", err);
    CHECK(!err.empty());
    parse_jvs_boards(R"({"boards":{"A":{"switches":[{"player":1,"byte":0,"bit":8,"signal":"p1.start"}]}}})", err);
    CHECK(!err.empty());                         // bit 8 bestaat niet
    parse_jvs_boards(R"({"boards":{"A":{"switches":[{"player":1,"byte":0,"bit":1,"signal":"p1.zz"}]}}})", err);
    CHECK(!err.empty());                         // onbekend signaal
    parse_jvs_boards(R"({"boards":{"A":{"switches":[{"player":1,"byte":0,"bit":1,"signal":"p1.start"},{"player":1,"byte":0,"bit":1,"signal":"p1.trigger"}]}}})", err);
    CHECK(!err.empty());                         // dubbele bit
    parse_jvs_boards(R"({"boards":{"A":{"players":1,"buttons":8,"switches":[{"player":1,"byte":1,"bit":1,"signal":"p1.start"}]}}})", err);
    CHECK(!err.empty());                         // byte 1 bestaat niet bij 8 knoppen
    parse_jvs_boards(R"({"boards":{"A":{"analog_channels":1,"analog":[{"signal":"p1.x"},{"signal":"p1.y"}]}}})", err);
    CHECK(!err.empty());                         // meer kanalen dan opgegeven
    parse_jvs_boards(R"({"boards":{"A":{"analog":[{"signal":"p1.x","min":5,"max":5}]}}})", err);
    CHECK(!err.empty());                         // leeg venster
    parse_jvs_boards(R"({"boards":{"A":{"comm_version":300}}})", err);
    CHECK(!err.empty());
    JvsBoards ok = parse_jvs_boards(R"({"boards":{"A":{"comm_version":"0x30","identify":"X;Y"}}})", err);
    CHECK(err.empty() && ok.boards.size() == 1 && ok.boards[0].comm_version == 0x30 && ok.boards[0].coins.size() == 2);
}

void test_commands() {
    JvsBus bus;
    bus.add_node(lgs());
    CHECK(bus.modem_status() == 0x10);           // nog niet geadresseerd: alleen CTS
    // Reset en adressering.
    CHECK(talk(bus, "E0 FF 03 F0 D9 CB").empty());
    CHECK(talk(bus, "E0 FF 03 F0 D9 CB").empty());
    CHECK(hex(talk(bus, "E0 FF 03 F1 01 F4")) == "E0 00 03 01 01 05");
    CHECK(bus.node(0).address() == 1 && bus.modem_status() == 0x30);
    CHECK(talk(bus, "E0 FF 03 F1 02 F5").empty());   // de keten is vol: geen antwoord
    CHECK(bus.node(0).address() == 1);
    // Een commando voor een ander adres wordt genegeerd.
    CHECK(ask(bus, 0x02, "10").empty());
    // Identificatie, versies, functies.
    CHECK(hex(ask(bus, 0x01, "10")) ==
          "E0 00 3D 01 01 53 45 47 41 20 43 4F 52 50 4F 52 41 54 49 4F 4E 3B 49 2F 4F 20 42 44 20 4A 56 53 3B 38 33 37 2D 31 34 38 39 35 3B 31 3B 31 3B 46 45 20 4D 4F 44 45 3B 32 30 30 35 2F 31 30 00 14");
    CHECK(hex(ask(bus, 0x01, "11 12 13")) == "E0 00 08 01 01 30 01 30 01 30 9C");   // drie commando's in een packet
    CHECK(hex(ask(bus, 0x01, "14")) == "E0 00 14 01 01 01 01 16 00 02 02 00 00 03 08 0A 00 12 10 00 00 00 69");
    // Hoofdbord-ID (met nul) en onbekend commando (status 2, rest wordt niet verwerkt).
    CHECK(hex(ask(bus, 0x01, "15 4D 41 49 4E 00")) == "E0 00 03 01 01 05");
    CHECK(bus.node(0).stats().main_id == "MAIN");
    CHECK(hex(ask(bus, 0x01, "7F")) == "E0 00 02 02 04");
    CHECK(hex(ask(bus, 0x01, "11 7F 10")) == "E0 00 04 02 01 30 37");   // het eerste commando wordt beantwoord, bij 7F stopt het
    // Checksumfout: status 3, en alleen als het packet voor dit adres was (een broadcast of ander adres: stil).
    CHECK(hex(talk(bus, "E0 01 02 10 14")) == "E0 00 02 03 05");
    CHECK(talk(bus, "E0 02 02 10 15").empty());
    CHECK(talk(bus, "E0 FF 03 F1 01 F5").empty());
    // Herhaal het laatste antwoord (0x2F).
    ask(bus, 0x01, "21 02");
    const Bytes last = ask(bus, 0x01, "2F");
    CHECK(hex(last) == "E0 00 07 01 01 00 00 00 00 09");
    CHECK(bus.stats().bad_sum == 3);
}

// Gaia Attack 4 (gemeten 2026-10-16): een packet met 0x20, 0x32, 0x21 en 0x22 voor 10 kanalen aan SegaGun2P (8 kanalen). Elk
// commando krijgt zijn rapport; de kanalen 9 en 10 staan in rust. Meer dan 32 kanalen blijft een fout.
void test_more_analog_than_board() {
    JvsBoards bs = load();
    const JvsBoard* g = bs.find("SegaGun2P");
    CHECK(g != nullptr);
    if (!g) return;
    JvsBus bus;
    bus.add_node(*g);
    CHECK(hex(talk(bus, "E0 FF 03 F1 01 F4")) == "E0 00 03 01 01 05");
    const Bytes r = ask(bus, 0x01, "20 01 03 32 03 01 00 00 21 01 22 0A");
    // E0 00 len status | 01 + 4 (schakelaars) | 01 (uitgang) | 01 + 2 (munt) | 01 + 20 (analoog) | checksum
    CHECK(r.size() == 3 + 1 + 5 + 1 + 3 + 21 + 1);
    CHECK(r.size() > 14 && r[3] == kJvsStatusOk && r[13] == kJvsReportOk);
    CHECK(r.size() > 34 && r[32] == 0x80 && r[33] == 0x00);   // kanaal 10: rust
    const Bytes te_veel = ask(bus, 0x01, "22 21");
    CHECK(te_veel.size() == 6 && te_veel[3] == kJvsStatusOk && te_veel[4] == kJvsReportBadParamData);
}

void test_inputs() {
    JvsBus bus;
    bus.add_node(lgs());
    talk(bus, "E0 FF 03 F0 D9 CB");
    talk(bus, "E0 FF 03 F1 01 F4");
    // Schakelaars: niets ingedrukt, dan test + start + trekker (rood = byte 1 bit 7).
    CHECK(hex(ask(bus, 0x01, "20 01 03")) == "E0 00 07 01 01 00 00 00 00 09");
    IoState s = armed_state();
    s.sys.test = true;
    s.player[0].start = true;
    s.player[0].trigger = true;
    s.player[0].valid = true;
    bus.set_input(s);
    CHECK(hex(ask(bus, 0x01, "20 01 03")) == "E0 00 07 01 01 80 80 80 00 89");
    s.sys.test = false;
    s.sys.service = true;
    s.player[0].start = false;
    s.player[0].trigger = false;
    s.player[0].reload = true;
    s.player[0].aux1 = true;
    bus.set_input(s);
    CHECK(hex(ask(bus, 0x01, "20 01 03")) == "E0 00 07 01 01 00 40 50 00 99");   // service = byte 0 bit 6; blauw = bit 6, groen = bit 4
    // Te veel spelers of bytes: parameterfout (rapport 3).
    CHECK(hex(ask(bus, 0x01, "20 02 03")) == "E0 00 03 01 03 07");
    // Niet gewapend: alles neutraal.
    bus.set_input(IoState{});
    CHECK(hex(ask(bus, 0x01, "20 01 03")) == "E0 00 07 01 01 00 00 00 00 09");

    // Munten: een flank telt een munt, vasthouden niet; het aantal is 14 bit.
    s = armed_state();
    s.sys.coin[0] = true;
    bus.set_input(s);
    bus.set_input(s);
    CHECK(hex(ask(bus, 0x01, "21 02")) == "E0 00 07 01 01 00 01 00 00 0A");
    s.sys.coin[0] = false;
    bus.set_input(s);
    s.sys.coin[0] = true;
    s.sys.coin[1] = true;
    bus.set_input(s);
    CHECK(hex(ask(bus, 0x01, "21 02")) == "E0 00 07 01 01 00 02 00 01 0C");
    // Munt aftrekken (cmd 0x30: sleuf 1, 1 stuk) en erbij (0x35).
    CHECK(hex(ask(bus, 0x01, "30 01 00 01")) == "E0 00 03 01 01 05");
    CHECK(bus.node(0).coin_count(0) == 1);
    CHECK(hex(ask(bus, 0x01, "35 01 00 05")) == "E0 00 03 01 01 05");
    CHECK(bus.node(0).coin_count(0) == 6);
    CHECK(hex(ask(bus, 0x01, "30 03 00 01")) == "E0 00 03 01 03 07");              // sleuf 3 bestaat niet

    // Analoog: kanaal 0 = stuur uit p1.x (venster 0..255 x 257), de rest idle (0x8000).
    s = armed_state();
    s.player[0].valid = true;
    s.player[0].x = 1.0;
    bus.set_input(s);
    CHECK(hex(ask(bus, 0x01, "22 02")) == "E0 00 07 01 01 FF FF 80 00 87");
    s.player[0].x = 0.0;
    bus.set_input(s);
    CHECK(hex(ask(bus, 0x01, "22 02")) == "E0 00 07 01 01 00 00 80 00 89");
    s.player[0].x = 0.5;
    bus.set_input(s);
    CHECK(hex(ask(bus, 0x01, "22 01")) == "E0 00 05 01 01 80 00 87");
    // Geen geldige aim: idle (midden). Meer kanalen dan het bord heeft: die staan in rust (test_more_analog_than_board).
    s.player[0].x = 1.0;
    s.player[0].valid = false;
    bus.set_input(s);
    CHECK(hex(ask(bus, 0x01, "22 01")) == "E0 00 05 01 01 80 00 87");
    CHECK(ask(bus, 0x01, "22 09").size() == 3 + 2 + 18 + 1);

    // Uitgangen en de rest die minimaal beantwoord wordt.
    CHECK(hex(ask(bus, 0x01, "32 03 01 02 03")) == "E0 00 03 01 01 05");
    CHECK(hex(bus.node(0).stats().gp_out) == "01 02 03");
    CHECK(hex(ask(bus, 0x01, "37 01 FF")) == "E0 00 03 01 01 05");
    CHECK(hex(bus.node(0).stats().gp_out) == "01 FF 03");
    CHECK(hex(ask(bus, 0x01, "23 01")) == "E0 00 05 01 01 00 00 07");               // roterend: nullen
    CHECK(hex(ask(bus, 0x01, "24")) == "E0 00 04 01 01 00 06");                      // toetscode
    CHECK(hex(ask(bus, 0x01, "26 02")) == "E0 00 05 01 01 00 00 07");               // extra schakelaars
    CHECK(hex(ask(bus, 0x01, "31 01 00 01")) == "E0 00 03 01 01 05");               // uitbetaling zonder hopper: stil aanvaard
}

void test_analog_window_and_escape() {
    // Een eigen bord met een venster waarvan de uiteinden E0xx en D0xx zijn, zodat de antwoorden escapes nodig hebben.
    std::string err;
    JvsBoards bs = parse_jvs_boards(R"({"boards":{"E":{"players":1,"buttons":8,"analog_channels":3,"analog":[
        {"signal":"p1.x","min":53248,"max":57344},{"signal":"p1.y","min":10,"max":200,"invert":true},{"signal":"p2.x","min":0,"max":255,"idle":0.0}]}}})", err);
    CHECK(err.empty() && bs.boards.size() == 1);
    if (bs.boards.empty()) return;
    JvsBus bus;
    bus.add_node(bs.boards[0]);
    talk(bus, "E0 FF 03 F1 01 F4");
    IoState s = armed_state();
    s.player[0].valid = true;
    s.player[0].x = 0.0;     // D000
    s.player[0].y = 0.0;     // invert, venster 10..200 -> x257: 1 - 0 = 1 -> 200 * 257 = 51400 = C8C8
    bus.set_input(s);
    // 0xD000 wordt D0 CF 00; sum = 00 + 0D(len) + 01 + 01 + D0 + 00 + C8 + C8 + 00 + 00 ... zie hieronder: de lengte telt de ongestuffe bytes.
    Bytes r = ask(bus, 0x01, "22 03");
    // ongestuft: 00 | 09 | 01 01 | D0 00 | C8 C8 | 00 00 | sum
    // sum = 00+09+01+01+D0+00+C8+C8 = 0x2 ... = 0x1 + 0x1 + 9 + 0xD0 + 0x190 = 0x26B -> 6B
    CHECK(hex(r) == "E0 00 09 01 01 D0 CF 00 C8 C8 00 00 6B");
    s.player[0].x = 1.0;     // E000 -> D0 DF 00
    bus.set_input(s);
    r = ask(bus, 0x01, "22 01");
    // ongestuft: 00 05 01 01 E0 00 sum(= 05+01+01+E0 = E7) -> E0 00 05 01 01 D0 DF 00 E7
    CHECK(hex(r) == "E0 00 05 01 01 D0 DF 00 E7");
    // Zonder gewapende uitvoer (jvs_analog_value alleen): idle.
    JvsAnalogChannel c{"p1.x", false, 0, 255, 0.25};
    IoState n;
    CHECK(jvs_analog_value(c, n) == static_cast<uint16_t>(std::lround(0.25 * 65535.0)));
    c.min = 63;
    c.max = 207;   // Dream Raiders-venster
    IoState a = armed_state();
    a.player[0].valid = true;
    a.player[0].x = 0.0;
    CHECK(jvs_analog_value(c, a) == 63 * 257);
    a.player[0].x = 1.0;
    CHECK(jvs_analog_value(c, a) == 207 * 257);
    c.invert = true;
    CHECK(jvs_analog_value(c, a) == 63 * 257);
}

void test_chain() {
    // Twee nodes (Halo-achtig: spelers 1-2 en 3-4). Alleen de eerste zonder adres ziet de sense-lijn.
    JvsBoards bs = load();
    JvsBus bus;
    bus.add_node(*bs.find("SegaGun2P"));
    bus.add_node(lgs());
    CHECK(bus.node_count() == 2 && bus.modem_status() == 0x10);
    talk(bus, "E0 FF 03 F0 D9 CB");
    CHECK(hex(talk(bus, "E0 FF 03 F1 01 F4")) == "E0 00 03 01 01 05");
    CHECK(bus.node(0).address() == 1 && bus.node(1).address() == 0 && !bus.all_addressed());
    CHECK(hex(talk(bus, "E0 FF 03 F1 02 F5")) == "E0 00 03 01 01 05");
    CHECK(bus.node(1).address() == 2 && bus.all_addressed());
    CHECK(talk(bus, "E0 FF 03 F1 03 F6").empty());
    // Elke node antwoordt met zijn eigen ID.
    Bytes a = ask(bus, 0x01, "10");
    Bytes b = ask(bus, 0x02, "10");
    CHECK(a.size() > 10 && b.size() > 10 && a != b);
    CHECK(hex(a).find("37 2D 31 34 35 37 32") != std::string::npos);   // "837-14572"
    CHECK(hex(b).find("37 2D 31 34 38 39 35") != std::string::npos);   // "837-14895"
    // Reset zet alle nodes terug.
    talk(bus, "E0 FF 03 F0 D9 CB");
    CHECK(bus.node(0).address() == 0 && bus.node(1).address() == 0 && bus.modem_status() == 0x10);
    // Na een nieuwe reeks adressering bij een tweede reset: de volgorde blijft.
    talk(bus, "E0 FF 03 F1 05 F8");
    CHECK(bus.node(0).address() == 5 && bus.node(1).address() == 0);
}

// ---------------------------------------------------------------- dialect over een pty

// Leest van fd tot er `want` bytes zijn of de tijd om is.
Bytes read_some(int fd, size_t want, int timeout_ms) {
    Bytes out;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (out.size() < want) {
        const int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count());
        if (left <= 0) break;
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, left) <= 0) break;
        uint8_t buf[256];
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n <= 0) break;
        out.insert(out.end(), buf, buf + n);
    }
    return out;
}

void test_dialect_pty() {
    const fs::path dir = fs::temp_directory_path() / ("rg-jvs-test-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string link = (dir / "jvs-com").string();
    const std::string modem = (dir / "modem").string();
    std::vector<std::string> traced;
    {
        JvsDialect d({lgs()}, "LGS", make_pty_transport(link), modem);
        d.set_trace([&](const std::string& l) { traced.push_back(l); });
        std::string err;
        CHECK(d.open(err) && err.empty());
        CHECK(fs::is_symlink(link));
        CHECK(std::string(d.status_json()).find("\"path\":\"/dev/pts/") != std::string::npos);
        CHECK(slurp(modem) == std::string(1, '\x10'));
        // De game: opent de poort via de symlink (zoals Wine via dosdevices/com4) en praat.
        int fd = ::open(link.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        CHECK(fd >= 0);
        if (fd >= 0) {
            termios tio{};
            CHECK(::tcgetattr(fd, &tio) == 0);                        // een echte tty: Wine herkent hem als seriële poort
            auto send = [&](const char* h) { const Bytes b = from_hex(h); CHECK(::write(fd, b.data(), b.size()) == static_cast<ssize_t>(b.size())); };
            send("E0 FF 03 F0 D9 CB");
            send("E0 FF 03 F1 01 F4");
            Bytes r = read_some(fd, 6, 1000);
            CHECK(hex(r) == "E0 00 03 01 01 05");
            CHECK(d.modem_status() == 0x30);
            for (int i = 0; i < 100 && slurp(modem) != std::string(1, '\x30'); ++i) ::usleep(10000);
            CHECK(slurp(modem) == std::string(1, '\x30'));
            IoState s = armed_state();
            s.player[0].start = true;
            s.player[0].valid = true;
            s.player[0].x = 1.0;
            d.write(s);
            send("E0 01 04 20 01 03 29");
            r = read_some(fd, 10, 1000);
            CHECK(hex(r) == "E0 00 07 01 01 00 80 00 00 89");
            send("E0 01 03 22 01 27");
            r = read_some(fd, 8, 1000);
            CHECK(hex(r) == "E0 00 05 01 01 FF FF 05");
            // Een packet in twee stukken geschreven.
            send("E0 01 02 7F");
            ::usleep(20000);
            send("82");
            r = read_some(fd, 5, 1000);
            CHECK(hex(r) == "E0 00 02 02 04");
            const JvsStats st = d.stats();
            CHECK(st.frames == 5 && st.identifies == 0 && st.switch_polls == 1 && st.analog_polls == 1 && st.write_errors == 0);
            CHECK(!traced.empty());
            const std::string js = d.status_json();
            CHECK(js.find("\"switch_polls\":1") != std::string::npos && js.find("\"0x20\":1") != std::string::npos);
            auto pr = json::parse(js);
            CHECK(pr.ok);
            ::close(fd);
        }
        d.neutral();
        d.close();
        CHECK(!fs::exists(link));        // opgeruimd
        CHECK(!fs::exists(modem));
    }
    fs::remove_all(dir);
}

// ---------------------------------------------------------------- Namco-bord (Mario Kart Arcade GP DX)

// Het antwoord van de bus als gegevens (status en rapporten, zonder sync, lengte en checksum).
Bytes reply_data(JvsBus& bus, uint8_t node, const char* data_hex) {
    const Bytes wire = ask(bus, node, data_hex);
    JvsFrameParser p;
    std::vector<JvsFrame> fr;
    p.feed(wire.data(), wire.size(), fr);
    CHECK(fr.size() == 1);
    if (fr.size() != 1) return {};
    CHECK(fr[0].checksum_ok && fr[0].node == 0x00);
    return fr[0].data;
}

void test_namco_mkdx() {
    static const JvsBoards bs = load();
    const JvsBoard* mk = bs.find("MKDX");
    CHECK(mk != nullptr);
    if (!mk) return;
    // Het bord volgens JvsSetup.cs (NamcoMkdx): NBGI_MarioKart3, versies 0x31, 0x18 schakelaars, 2 spelers.
    CHECK(mk->namco);
    CHECK(mk->identify == "NBGI.;NA-JV;Ver6.01;JPN,MK3100-1-NA-APR0-A01");
    CHECK(mk->comm_version == 0x31 && mk->jvs_version == 0x31 && mk->command_rev == 0x31);
    CHECK(mk->players == 2 && mk->buttons == 24 && jvs_switch_bytes(*mk) == 3 && mk->analog_channels == 8 && mk->gp_outputs == 20);
    CHECK(!lgs().namco);   // een gewoon bord blijft een gewoon bord

    // Lengtes van de Namco-commando's (met de commandobyte).
    CHECK(jvs_namco_command_length(from_hex("70 03 00 00 11"), 0) == 4);
    CHECK(jvs_namco_command_length(from_hex("70 15 00 00"), 0) == 4);
    CHECK(jvs_namco_command_length(from_hex("70 16 00"), 0) == 3);              // afgekapt op wat er is
    CHECK(jvs_namco_command_length(from_hex("70 18 01 02 03"), 0) == 5);        // de rest van het packet
    CHECK(jvs_namco_command_length(from_hex("70 05 05 AA BB 11"), 0) == 5);     // derde byte = lengte
    CHECK(jvs_namco_command_length(from_hex("70 05 01 11"), 0) == 3);           // nooit korter dan commando, sub en lengte
    CHECK(jvs_namco_command_length(from_hex("70 99 01 02"), 0) == 4);           // onbekend sub: de rest
    CHECK(jvs_namco_command_length(from_hex("11 78 00 00 00 00 00 00 00 00 00 00 00 00 00 00 11"), 1) == 15);
    CHECK(jvs_namco_command_length(from_hex("80 00"), 0) == 2);
    CHECK(jvs_namco_command_length(from_hex("11"), 0) == 0);                    // geen Namco-commando
    CHECK(jvs_namco_command_length(from_hex("77"), 0) == 0);

    JvsBus bus;
    bus.add_node(*mk);
    talk(bus, "E0 FF 03 F0 D9 CB");
    CHECK(hex(talk(bus, "E0 FF 03 F1 01 F4")) == "E0 00 03 01 01 05");
    CHECK(hex(reply_data(bus, 0x01, "11 12 13")) == "01 01 31 01 31 01 31");
    // Functies: 2 spelers x 24 schakelaars, 2 muntsleuven, 8 kanalen van 10 bit, 20 uitgangen.
    CHECK(hex(reply_data(bus, 0x01, "14")) == "01 01 01 02 18 00 02 02 00 00 03 08 0A 00 12 14 00 00 00");
    // Identificatie: de ID-string met de afsluitende nul.
    {
        const Bytes d = reply_data(bus, 0x01, "10");
        CHECK(d.size() > 2 && d[0] == 0x01 && d[1] == 0x01);
        const std::string id(d.size() > 2 ? d.begin() + 2 : d.end(), d.end());
        CHECK(id == std::string("NBGI.;NA-JV;Ver6.01;JPN,MK3100-1-NA-APR0-A01") + '\0');
    }
    // Namco 0x70: een rapport per commando, daarna gaat het packet gewoon verder.
    CHECK(hex(reply_data(bus, 0x01, "70 03 00 00")) == "01 01 00");
    CHECK(hex(reply_data(bus, 0x01, "70 18 01 02 03")) == "01 01 01");
    CHECK(hex(reply_data(bus, 0x01, "70 15 00 00 11")) == "01 01 01 01 31");
    CHECK(hex(reply_data(bus, 0x01, "70 05 05 AA BB 20 02 01")) == "01 01 01 01 00 00 00");
    // 0x78..0x80: vaste lengte 15, alleen een rapport.
    CHECK(hex(reply_data(bus, 0x01, "78 00 00 00 00 00 00 00 00 00 00 00 00 00 00 11")) == "01 01 01 31");
    // Een onbekend commando wordt overgeslagen (een gewoon bord stopt daar met status 2).
    CHECK(hex(reply_data(bus, 0x01, "99 11")) == "01 01 31");
    CHECK(bus.node(0).stats().unknown == 1);

    // Invoer: stuur, gas en rem van een gamepad op kanaal 0..2; Item = trekker, Mario-knop = herladen, kaart = aux1.
    IoState s = armed_state();
    s.player[0].pad = true;
    s.player[0].wheel_from_pad = true;
    s.player[0].wheel = 1.0;
    s.player[0].gas = 0.5;
    s.player[0].brake = 0.0;
    s.player[0].start = true;
    s.player[0].aux1 = true;
    s.player[0].trigger = true;
    s.player[0].reload = true;
    bus.set_input(s);
    CHECK(hex(reply_data(bus, 0x01, "22 03")) == "01 01 FF FF 80 00 00 00");
    CHECK(hex(reply_data(bus, 0x01, "20 01 03")) == "01 01 00 81 20 02");
    s.player[0] = IoPlayer{};
    s.player[0].grenade = true;      // Enter
    s.player[0].btn[0] = true;       // Menu Up
    s.sys.test = true;
    s.sys.service = true;
    bus.set_input(s);
    CHECK(hex(reply_data(bus, 0x01, "20 02 03")) == "01 01 80 62 00 00 00 00 00");
    // Zonder gamepad en zonder aim: stuur in het midden, pedalen los.
    bus.set_input(armed_state());
    CHECK(hex(reply_data(bus, 0x01, "22 03")) == "01 01 80 00 00 00 00 00");
}

}  // namespace

// Hoeveel bytes de leesdraad van lindbergh-loader (passthrough.c, JVS-doorgifte) als een antwoord neemt: vanaf de sync tot de
// lengte uit de lengtebyte + 3. Zonder de patch (third_party/lindbergh-loader/patches/0001-*) telt hij ruwe bytes, met de patch
// telt een escape een byte extra. Een model van het gedrag, voor de test.
size_t lindbergh_reader(const Bytes& wire, bool patched) {
    int count = 0, ack = 0;
    bool wait = false, esc = false;
    for (uint8_t b : wire) {
        if (count == 0 && b == 0xE0) wait = true;
        if (count == 2) ack = (patched ? static_cast<int>(b) : static_cast<int>(static_cast<int8_t>(b))) + 3;
        else if (patched && count > 2 && wait) {
            if (esc) esc = false;
            else if (b == 0xD0) { esc = true; ++ack; }
        }
        if (wait) ++count;
        if (count == ack) return static_cast<size_t>(count);
    }
    return static_cast<size_t>(count);
}

// De borden van de Lindbergh-route (docs/lindbergh.md): gelijk aan wat lindbergh-loader zelf emuleert (jvs.c, SEGA_TYPE_3 en
// SEGA_TYPE_1), de herlaadschakelaar op BUTTON_2, en een antwoord met een escape dat de doorgifte heel moet doorgeven.
void test_lindbergh() {
    const JvsBoards bs = load();
    const JvsBoard* t3 = bs.find("Lindbergh");
    const JvsBoard* t1 = bs.find("LindberghType1");
    CHECK(t3 && t1);
    if (!t3 || !t1) return;
    CHECK(t3->comm_version == 0x10 && t3->jvs_version == 0x30 && t3->command_rev == 0x13 && t3->analog_bits == 10 && t3->gp_outputs == 20);
    CHECK(t1->comm_version == 0x10 && t1->jvs_version == 0x30 && t1->command_rev == 0x11 && t1->analog_bits == 8 && t1->gp_outputs == 6);
    JvsBus bus;
    bus.add_node(*t3);
    talk(bus, "E0 FF 03 F1 01 F4");
    // Functielijst: spelers 2 x 14, munten 2, analoog 8 x 10 bit, uitgangen 20 (de volgorde van writeFeatures in jvs.c).
    const Bytes f = ask(bus, 0x01, "14");
    const std::string fh = hex(f);
    CHECK(fh.find("01 02 0E 00 02 02 00 00 03 08 0A 00") != std::string::npos && fh.find("12 14 00 00 00") != std::string::npos);
    // Naast het scherm richten = BUTTON_2 (byte 0 bit 0) van die speler; de trigger is BUTTON_1 (bit 1).
    IoState s = armed_state();
    s.player[0].valid = true;
    s.player[0].x = 0.0;
    s.player[0].offscreen = true;
    s.player[0].trigger = true;
    bus.set_input(s);
    const Bytes sw = ask(bus, 0x01, "20 02 02");   // E0 00 len 01 01 sys p1b0 p1b1 p2b0 p2b1 som
    CHECK(sw.size() >= 11 && sw[6] == 0x03 && sw[8] == 0x00);
    // Een as met hoge byte D0 (x = 0xD0 / 0xFF): het antwoord heeft een escape; de doorgifte zonder patch kapt het af.
    s.player[0].offscreen = false;
    s.player[0].x = 208.0 / 255.0;
    bus.set_input(s);
    const Bytes an = ask(bus, 0x01, "22 01");
    CHECK(hex(an).find("D0 CF") != std::string::npos);
    CHECK(lindbergh_reader(an, true) == an.size());
    CHECK(lindbergh_reader(an, false) < an.size());   // 208 x 257 = 0xD0D0: twee escapes, twee bytes te weinig
    // Zonder escape lezen beide hetzelfde.
    const Bytes plain = ask(bus, 0x01, "10");
    CHECK(lindbergh_reader(plain, false) == plain.size() && lindbergh_reader(plain, true) == plain.size());
    CHECK(hex(plain).find(hex(Bytes(t3->identify.begin(), t3->identify.end()))) != std::string::npos);
}

int main() {
    test_framing();
    test_lindbergh();
    test_board_file();
    test_commands();
    test_inputs();
    test_analog_window_and_escape();
    test_more_analog_than_board();
    test_chain();
    test_dialect_pty();
    test_namco_mkdx();
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
