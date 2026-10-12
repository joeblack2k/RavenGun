// Dialect hid-board: beschrijving van het bord (hid-boards.json), de HID-rapportbeschrijving en het input-rapport (bytes
// exact), schaal en inversie van de gun-assen, neutraal bij disarm, het dialect met een nep-apparaat, en het uhid-transport
// via een FIFO in plaats van /dev/uhid (UHID_CREATE2/INPUT2/DESTROY, bytes). Zonder hardware.
#include <fcntl.h>
#include <linux/uhid.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ravengun/iobridge/hid_board.hpp"
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

HidBoards load() {
    std::string err;
    HidBoards b = parse_hid_boards(slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/hid-boards.json"), err);
    CHECK(err.empty());
    return b;
}

const HidBoard& sae() {
    static const HidBoards boards = load();
    return *boards.find("SaeGunBoard");
}

const HidBoard& sae_io() {
    static const HidBoards boards = load();
    return *boards.find("SaeIoBoard");
}

IoPlayer aim(double x, double y) {
    IoPlayer p;
    p.valid = true;
    p.x = x;
    p.y = y;
    return p;
}

void test_file() {
    HidBoards bs = load();
    const HidBoard* b = bs.find("SaeGunBoard");
    CHECK(b);
    if (!b) return;
    CHECK(b->transport == "uinput" && b->vendor == 0x1209 && b->product == 0x72F0 && b->button_count == 16);
    CHECK(b->axes.size() == 4 && b->axes[0].name == "X" && b->axes[3].name == "RX");
    CHECK(validate_hid_board(*b).empty());
    // Alle signalen in het bestand zijn geldig; knopindexen binnen het bereik (valideer dekt dit), en elke route met een bord verwijst ernaar.
    std::string err;
    CHECK(!parse_hid_boards("{", err).boards.size() && !err.empty());
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"X","signal":"p1.x"}],"transport":"bluetooth"}}})", err);
    CHECK(!err.empty());
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"X","signal":"p1.x","out_min":-200}]}}})", err);
    CHECK(!err.empty());                                    // out_min zonder out_max
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"X","signal":"p1.x","out_min":-200,"out_max":2000}]}}})", err);
    CHECK(!err.empty());                                    // buiten min..max
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"X","signal":"p1.x","out_min":300,"out_max":-300}]}}})", err);
    CHECK(!err.empty());                                    // out_min >= out_max
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"X","signal":"p1.x","out_min":-300,"out_max":300}]}}})", err);
    CHECK(err.empty());
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"Q","signal":"p1.x"}]}}})", err);
    CHECK(!err.empty());                                    // onbekende as
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"X","signal":"p1.zz"}]}}})", err);
    CHECK(!err.empty());                                    // onbekend signaal
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"X","signal":"p1.x"},{"name":"X","signal":"p1.y"}]}}})", err);
    CHECK(!err.empty());                                    // dubbele as
    parse_hid_boards(R"({"boards":{"A":{"button_count":17,"axes":[{"name":"X","signal":"p1.x"}]}}})", err);
    CHECK(!err.empty());                                    // uinput heeft maximaal 16 knoppen
    parse_hid_boards(R"({"boards":{"A":{"button_count":4,"axes":[{"name":"X","signal":"p1.x"}],"buttons":[{"index":4,"signal":"test"}]}}})", err);
    CHECK(!err.empty());                                    // knopindex buiten het bereik
    HidBoards ok = parse_hid_boards(R"({"boards":{"A":{"vendor":"0x1209","product":"0x7301","transport":"uhid","button_count":24,
        "axes":[{"name":"RZ","signal":"p2.y","min":0,"max":4095,"invert":true,"idle":0}]}}})", err);
    CHECK(err.empty() && ok.find("A") && ok.find("A")->vendor == 0x1209 && ok.find("A")->product == 0x7301 && ok.find("A")->button_count == 24);
    // De drie vormvelden zijn optioneel en vallen terug op de oude waarde: een bord dat ze weglaat verandert niet.
    CHECK(ok.find("A")->application_usage == 0x04 && ok.find("A")->output_report_length == 0 &&
          ok.find("A")->input_report_length == 0);
    HidBoards vorm = parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","application_usage":"0x05",
        "input_report_length":15,"output_report_length":3,"axes":[],"buttons":[]}}})", err);
    CHECK(err.empty() && vorm.find("A") && vorm.find("A")->application_usage == 0x05 &&
          vorm.find("A")->input_report_length == 15 && vorm.find("A")->output_report_length == 3);
    parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","input_report_length":15,
        "axes":[{"name":"X","signal":"p1.x"}]}}})", err);
    CHECK(!err.empty());                                    // een vast rapport gaat samen met lege axes
    parse_hid_boards(R"({"boards":{"A":{"input_report_length":15,"axes":[],"buttons":[]}}})", err);
    CHECK(!err.empty());                                    // een vast rapport vraagt transport uhid
    parse_hid_boards(R"({"boards":{"A":{"axes":[{"name":"X","signal":"p1.x"}],"output_report_length":3}}})", err);
    CHECK(!err.empty());                                    // een uitvoerrapport vraagt transport uhid
    parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","application_usage":0,"axes":[{"name":"X","signal":"p1.x"}]}}})", err);
    CHECK(!err.empty());                                    // usage 0 bestaat niet
    parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","output_report_length":65,"axes":[{"name":"X","signal":"p1.x"}]}}})", err);
    CHECK(!err.empty());                                    // lengte buiten 0..64
    // De antwoordtabel: alleen geldig bij een vast inputrapport, en elke respons moet erin passen.
    HidBoards rt = parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","input_report_length":15,"axes":[],"buttons":[],
        "responses":[{"a":116,"b":224,"offset":12,"value":176},{"a":"0x09","b":"0xE1","offset":12,"value":"0xB1"}]}}})", err);
    CHECK(err.empty() && rt.find("A") && rt.find("A")->responses.size() == 2 &&
          rt.find("A")->responses[0].a == 116 && rt.find("A")->responses[0].value == 176 &&
          rt.find("A")->responses[1].b == 0xE1);
    parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","input_report_length":15,"axes":[],"buttons":[],
        "responses":[{"a":116,"b":224,"offset":15,"value":176}]}}})", err);
    CHECK(!err.empty());                                    // offset 15 valt buiten een rapport van 15 bytes
    parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","input_report_length":15,"axes":[],"buttons":[],
        "responses":[{"a":116,"b":224,"offset":-1,"value":176}]}}})", err);
    CHECK(!err.empty());                                    // negatieve offset
    parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","input_report_length":15,"axes":[],"buttons":[],
        "responses":[{"a":116,"b":224,"offset":12,"value":256}]}}})", err);
    CHECK(!err.empty());                                    // waarde buiten 0..255
    parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","input_report_length":15,"axes":[],"buttons":[],
        "responses":[{"a":116,"b":224,"offset":12,"value":176},{"a":116,"b":224,"offset":13,"value":177}]}}})", err);
    CHECK(!err.empty());                                    // twee keer hetzelfde uitvoerrapport
    parse_hid_boards(R"({"boards":{"A":{"transport":"uhid","axes":[{"name":"X","signal":"p1.x"}],
        "responses":[{"a":116,"b":224,"offset":0,"value":176}]}}})", err);
    CHECK(!err.empty());                                    // responses zonder vast inputrapport
}

void test_descriptor_and_report() {
    const HidBoard& b = sae();
    Bytes expect = {0x05, 0x01, 0x09, 0x04, 0xA1, 0x01,
                    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02};
    for (uint8_t usage : Bytes{0x30, 0x31, 0x32, 0x33})
        for (uint8_t v : Bytes{0x05, 0x01, 0x09, usage, 0x16, 0x18, 0xFC, 0x26, 0xE8, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02}) expect.push_back(v);
    expect.push_back(0xC0);
    CHECK(hid_board_descriptor(b) == expect);
    CHECK(hid_board_report_size(b) == 10);
    HidBoardFrame f;
    f.axis = {-1000, 1000, 0, 500};
    f.buttons = 0x0005;
    CHECK(hid_board_report(b, f) == (Bytes{0x05, 0x00, 0x18, 0xFC, 0xE8, 0x03, 0x00, 0x00, 0xF4, 0x01}));
    // Knoppen die geen veelvoud van 8 zijn krijgen opvulling in de rapportbeschrijving en in het rapport.
    HidBoard small = b;
    small.button_count = 6;
    small.axes.resize(1);
    Bytes d = hid_board_descriptor(small);
    Bytes pad = {0x05, 0x09, 0x19, 0x01, 0x29, 0x06, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x06, 0x81, 0x02, 0x75, 0x01, 0x95, 0x02, 0x81, 0x03};
    CHECK(d.size() > 6 + pad.size() && Bytes(d.begin() + 6, d.begin() + 6 + static_cast<long>(pad.size())) == pad);
    CHECK(hid_board_report_size(small) == 3);
    // 24 knoppen: drie bytes bitveld, laagste byte eerst.
    HidBoard big = b;
    big.button_count = 24;
    f.buttons = 0x00A1B2C3;
    big.axes.resize(1);
    f.axis = {-1};
    CHECK(hid_board_report(big, f) == (Bytes{0xC3, 0xB2, 0xA1, 0xFF, 0xFF}));
}

void test_frame() {
    const HidBoard& b = sae();
    IoState s;
    s.armed = true;
    s.player[0] = aim(0.0, 1.0);
    s.player[1] = aim(0.75, 0.25);
    s.player[0].trigger = true;
    s.player[0].start = true;
    s.player[1].trigger = true;
    s.sys.test = true;
    s.sys.service = true;
    HidBoardFrame f = hid_board_frame(b, s);
    // Transformers (gemeten): X = P1 y, Y = P1 x, Z = P2 y, RX = P2 x; uitvoerbereik -217..557 binnen -1000..1000.
    // P1 (0, 1): X = 557, Y = -217. P2 (0.75, 0.25): Z = -217 + 0.25*774 = -23.5 -> -24, RX = -217 + 0.75*774 = 363.5 -> 364.
    CHECK(f.axis == (std::vector<int32_t>{557, -217, -24, 364}));
    CHECK(f.buttons == ((1u << 0) | (1u << 2) | (1u << 3) | (1u << 12) | (1u << 13)));
    // Schaal: midden van het scherm is het midden van het uitvoerbereik (170), afgerond.
    s.player[0] = aim(0.5, 0.5);
    CHECK(hid_board_frame(b, s).axis[0] == 170 && hid_board_frame(b, s).axis[1] == 170);
    s.player[0] = aim(0.25, 0.9995);
    f = hid_board_frame(b, s);
    CHECK(f.axis[1] == -24 && f.axis[0] == 557);
    // Een speler zonder geldige aim blijft op idle (midden), ook als de x/y iets anders zeggen; knoppen blijven.
    s.player[1].valid = false;
    s.player[1].x = 1.0;
    f = hid_board_frame(b, s);
    CHECK(f.axis[2] == 170 && f.axis[3] == 170);
    // Disarm: alles neutraal (assen op idle, geen knoppen).
    s.armed = false;
    f = hid_board_frame(b, s);
    CHECK(f == hid_board_neutral(b) && f.buttons == 0 && f.axis == (std::vector<int32_t>{170, 170, 170, 170}));
    // Inversie en eigen bereik.
    HidBoard inv = b;
    inv.axes[0].signal = "p1.x";
    inv.axes[0].has_out = false;
    inv.axes[0].invert = true;
    inv.axes[0].min = 0;
    inv.axes[0].max = 4095;
    inv.axes[0].idle = 0;
    s.armed = true;
    s.player[0] = aim(0.0, 0.5);
    CHECK(hid_board_frame(inv, s).axis[0] == 4095);
    s.armed = false;
    CHECK(hid_board_frame(inv, s).axis[0] == 0);
    // Buiten 0..1 wordt begrensd.
    s.armed = true;
    s.player[0] = aim(2.0, -1.0);
    f = hid_board_frame(b, s);
    CHECK(f.axis[0] == -217 && f.axis[1] == 557);   // X = p1.y (-1 -> 0), Y = p1.x (2 -> 1), binnen het uitvoerbereik
}

struct Fake : HidBoardDevice {
    bool made = false, gone = false;
    std::vector<HidBoardFrame> sent;
    std::vector<uint8_t> out;      // het laatste uitvoerrapport van de "game"
    uint64_t outputs = 0;
    bool create(const HidBoard&, std::string&) override { made = true; return true; }
    bool send(const HidBoardFrame& f) override { sent.push_back(f); return true; }
    void destroy() override { made = false; gone = true; }
    bool created() const override { return made; }
    bool last_output(std::vector<uint8_t>& o) const override {
        if (out.empty()) return false;
        o = out;
        return true;
    }
    uint64_t output_reports() const override { return outputs; }
};

void test_dialect() {
    const HidBoard& b = sae();
    auto fk = std::make_unique<Fake>();
    Fake* fp = fk.get();
    HidBoardDialect d(b, "TransformersShadowsRising", std::move(fk));
    std::string err;
    CHECK(d.open(err) && fp->made && fp->sent.size() == 1);
    CHECK(fp->sent[0] == hid_board_neutral(b));
    IoState s;
    s.armed = true;
    s.player[0] = aim(0.1, 0.9);
    d.write(s);
    CHECK(fp->sent.size() == 2);
    d.write(s);   // niets veranderd: geen nieuw beeld (tot de verversing na 25 tikken)
    CHECK(fp->sent.size() == 2);
    for (int i = 0; i < 30; ++i) d.write(s);
    CHECK(fp->sent.size() >= 3 && fp->sent.size() <= 4);   // verversing zonder verandering
    s.player[0].trigger = true;
    const size_t before = fp->sent.size();
    d.write(s);
    CHECK(fp->sent.size() == before + 1 && (fp->sent.back().buttons & (1u << 2)));
    d.neutral();
    CHECK(fp->sent.back() == hid_board_neutral(b));
    CHECK(d.status_json().find("\"board\":\"SaeGunBoard\"") != std::string::npos && json::parse(d.status_json()).ok);
    d.close();
    CHECK(fp->gone && !fp->made && fp->sent.back() == hid_board_neutral(b));   // netjes opruimen: eerst neutraal, dan weg
    d.close();   // idempotent
    HidBoardDialect nodev(b, "X", nullptr);
    CHECK(!nodev.open(err) && !err.empty());
}

// Het cabinet-IO-bord van de Sega Game Shell (SaeIoBoard): een Game Pad-collectie met een vast inputrapport van 15 bytes
// en een uitvoerrapport van 3 bytes. Deze test houdt de vorm vast die uit Shell.exe is gemeten; zie
// patches/TransformersShadowsRising/REPORT-io-board-and-loader.md. Dit bord is iets anders dan de gun (SaeGunBoard).
void test_io_board() {
    const HidBoard& io = sae_io();
    CHECK(io.transport == "uhid" && io.application_usage == 0x05 && io.input_report_length == 15 &&
          io.output_report_length == 3);
    CHECK(validate_hid_board(io).empty());

    // (a) De Application-collectie is Game Pad (0x0005). De joystick van de gun blijft 0x0004 -- de hele descriptor
    //     daarvan staat byte voor byte in test_descriptor_and_report, dus dat dekt meteen "geen bestaand bord veranderd".
    const Bytes d = hid_board_descriptor(io);
    const Bytes want = {0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,                          // Generic Desktop, Game Pad, Application
                        0x06, 0x00, 0xFF, 0x09, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00,  // leverancierspagina 0xFF00, usage 1, 0..255
                        0x75, 0x08, 0x95, 0x0F, 0x81, 0x02,                          // Report Size 8, Count 15, Input
                        0x75, 0x08, 0x95, 0x03, 0x91, 0x02,                          // Report Size 8, Count 3, Output
                        0xC0};
    CHECK(d == want);
    CHECK(hid_board_descriptor(sae())[3] == 0x04);                                   // de gun is en blijft Joystick

    // (b)+(c) Het uitvoerrapport is precies 3 bytes en die lengte komt uit het bord, niet uit een constante.
    //         Het Output-item zet ReportCount op output_report_length; HidP_GetCaps() maakt daar
    //         OutputReportByteLength van, en dat is precies het aantal bytes dat de shell schrijft.
    CHECK(d[19] == 0x0F && d[25] == 0x03);                                           // de twee ReportCounts
    HidBoard zeven = io;
    zeven.output_report_length = 7;
    const Bytes d7 = hid_board_descriptor(zeven);
    CHECK(d7.size() == d.size() && d7[25] == 0x07 && d[25] == 0x03);
    // Wat de shell stuurt is 3 bytes in de vorm 00 <a> <b>; zo lezen wij het, en 'size' komt uit het bord.
    const HidBoardOutput o = hid_board_parse_output(io, Bytes{0x00, 0x74, 0xE0});
    CHECK(o.size == 3 && !o.truncated && o.id == 0x00 && o.a == 0x74 && o.b == 0xE0);
    const HidBoardOutput o7 = hid_board_parse_output(zeven, Bytes{0x00, 0x74, 0xE0});
    CHECK(o7.size == 7 && o7.truncated);                                             // het bord, niet de ontvangen lengte
    CHECK(hid_board_parse_output(io, Bytes{0x00}).size == 3 && hid_board_parse_output(io, Bytes{0x00}).truncated);

    // (d) De antwoordbytes 0xB0..0xB4 passen in het vaste inputrapport: byte 12 bestaat en wij kunnen hem zetten.
    //     Dit is de eis waaraan het formaat moest voldoen; kon het dat niet, dan was het formaat nog niet af.
    CHECK(hid_board_report_size(io) == 15);
    HidBoardFrame f;
    for (uint8_t antwoord : Bytes{0xB0, 0xB1, 0xB2, 0xB3, 0xB4}) {
        f.raw.assign(15, 0);
        f.raw[12] = antwoord;
        const Bytes r = hid_board_report(io, f);
        CHECK(r.size() == 15 && r[12] == antwoord);
    }
    // Te weinig of te veel bytes wordt precies 15; niet-gevulde bytes zijn 0.
    f.raw.assign(4, 0xAA);
    Bytes r = hid_board_report(io, f);
    CHECK(r.size() == 15 && r[0] == 0xAA && r[4] == 0x00 && r[12] == 0x00);
    f.raw.assign(20, 0xAA);
    CHECK(hid_board_report(io, f).size() == 15);
}

// De antwoordtabel: de vier uitwisselingen uit de meting plus de keypadstap, en het geval dat er GEEN respons past.
void test_responses() {
    const HidBoard& io = sae_io();
    CHECK(io.responses.size() == 5);
    struct Geval { int a, b, want; };
    const Geval gevallen[] = {{0x74, 0xE0, 0xB0}, {0x09, 0xE1, 0xB1}, {0xE0, 0xF0, 0xB2},
                              {0xCA, 0xF1, 0xB3}, {0x00, 0x82, 0xB4}};
    for (const Geval& g : gevallen) {
        const HidBoardResponse* r = hid_board_find_response(io, g.a, g.b);
        CHECK(r && r->offset == 12 && r->value == g.want);
    }
    CHECK(!hid_board_find_response(io, 0x11, 0x22));       // geen respons voor een onbekend rapport
    CHECK(!hid_board_find_response(sae(), 0x74, 0xE0));    // de gun heeft geen antwoordtabel

    auto fk = std::make_unique<Fake>();
    Fake* fp = fk.get();
    HidBoardDialect d(io, "TransformersShadowsRising", std::move(fk));
    std::vector<std::string> log;
    d.set_trace([&](const std::string& l) { log.push_back(l); });
    std::string err;
    CHECK(d.open(err) && fp->made && fp->sent.size() == 1);
    CHECK(fp->sent[0].raw.size() == 15 && fp->sent[0].raw[12] == 0x00);   // begint op nul
    IoState s;
    s.armed = true;
    // De vier uitwisselingen: elk uitvoerrapport zet byte 12 van ons inputrapport, en dat komt in het logboek.
    for (const Geval& g : gevallen) {
        fp->out = {0x00, static_cast<uint8_t>(g.a), static_cast<uint8_t>(g.b)};
        ++fp->outputs;
        d.write(s);
        CHECK(fp->sent.back().raw.size() == 15 && fp->sent.back().raw[12] == g.want);
        CHECK(!log.empty() && log.back().find("->") != std::string::npos);
    }
    CHECK(d.stats().responses_set == 5);
    // Geen respons: het rapport blijft ongemoeid en er wordt niets gezet (wel een regel in het logboek).
    const size_t nlog = log.size();
    fp->out = {0x00, 0x11, 0x22};
    ++fp->outputs;
    d.write(s);
    CHECK(fp->sent.back().raw[12] == 0xB4);
    CHECK(d.stats().responses_set == 5);
    CHECK(log.size() == nlog + 1 && log.back().find("geen respons") != std::string::npos);
    // Hetzelfde rapport nog eens: geen nieuw uitvoerrapport, dus geen nieuwe regel en geen nieuwe zending.
    const size_t nsend = fp->sent.size();
    d.write(s);
    CHECK(log.size() == nlog + 1 && fp->sent.size() == nsend);
}

// uhid-transport tegen een FIFO: de bytes die naar /dev/uhid zouden gaan.
void test_uhid(const std::string& dir) {
    const HidBoard& b0 = sae();
    HidBoard b = b0;
    b.transport = "uhid";
    const std::string path = dir + "/uhid";
    CHECK(::mkfifo(path.c_str(), 0600) == 0);
    int rd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    CHECK(rd >= 0);
    auto dev = make_uhid_board_device(path);
    std::string err;
    CHECK(dev->create(b, err) && dev->created());
    auto next = [&](uhid_event& ev) {
        std::memset(&ev, 0, sizeof ev);
        ssize_t n = ::read(rd, &ev, sizeof ev);
        return n == static_cast<ssize_t>(sizeof ev);
    };
    uhid_event ev;
    CHECK(next(ev) && ev.type == UHID_CREATE2);
    const Bytes rdesc = hid_board_descriptor(b);
    CHECK(ev.u.create2.rd_size == rdesc.size() && Bytes(ev.u.create2.rd_data, ev.u.create2.rd_data + rdesc.size()) == rdesc);
    CHECK(ev.u.create2.vendor == 0x1209 && ev.u.create2.product == 0x72F0 && ev.u.create2.bus == 3);   // BUS_USB
    CHECK(std::strcmp(reinterpret_cast<char*>(ev.u.create2.name), "RavenGun SAE Gun Board") == 0);
    HidBoardFrame f;
    f.axis = {-1000, 1000, 0, 500};
    f.buttons = 0x0005;
    CHECK(dev->send(f));
    CHECK(next(ev) && ev.type == UHID_INPUT2 && ev.u.input2.size == 10);
    CHECK(Bytes(ev.u.input2.data, ev.u.input2.data + 10) == hid_board_report(b, f));
    dev->destroy();
    CHECK(!dev->created() && next(ev) && ev.type == UHID_DESTROY);
    // Zonder rechten (bestaat niet): duidelijke fout.
    auto bad = make_uhid_board_device(dir + "/bestaat-niet");
    CHECK(!bad->create(b, err) && !err.empty());
    ::close(rd);
}

}  // namespace

int main() {
    const std::string dir = (fs::temp_directory_path() / ("rg_hidboard_" + std::to_string(::getpid()))).string();
    fs::create_directories(dir);
    test_file();
    test_descriptor_and_report();
    test_frame();
    test_dialect();
    test_io_board();
    test_responses();
    test_uhid(dir);
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
