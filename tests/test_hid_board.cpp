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
    // X = P1 x, Y = P1 y, Z = P2 x, RX = P2 y; -1000..1000.
    CHECK(f.axis == (std::vector<int32_t>{-1000, 1000, 500, -500}));
    CHECK(f.buttons == ((1u << 0) | (1u << 2) | (1u << 3) | (1u << 12) | (1u << 13)));
    // Schaal: midden is 0, afgerond.
    s.player[0] = aim(0.5, 0.5);
    CHECK(hid_board_frame(b, s).axis[0] == 0 && hid_board_frame(b, s).axis[1] == 0);
    s.player[0] = aim(0.25, 0.9995);
    f = hid_board_frame(b, s);
    CHECK(f.axis[0] == -500 && f.axis[1] == 999);
    // Een speler zonder geldige aim blijft op idle (midden), ook als de x/y iets anders zeggen; knoppen blijven.
    s.player[1].valid = false;
    s.player[1].x = 1.0;
    f = hid_board_frame(b, s);
    CHECK(f.axis[2] == 0 && f.axis[3] == 0);
    // Disarm: alles neutraal (assen op idle, geen knoppen).
    s.armed = false;
    f = hid_board_frame(b, s);
    CHECK(f == hid_board_neutral(b) && f.buttons == 0 && f.axis == (std::vector<int32_t>{0, 0, 0, 0}));
    // Inversie en eigen bereik.
    HidBoard inv = b;
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
    CHECK(f.axis[0] == 1000 && f.axis[1] == -1000);
}

struct Fake : HidBoardDevice {
    bool made = false, gone = false;
    std::vector<HidBoardFrame> sent;
    bool create(const HidBoard&, std::string&) override { made = true; return true; }
    bool send(const HidBoardFrame& f) override { sent.push_back(f); return true; }
    void destroy() override { made = false; gone = true; }
    bool created() const override { return made; }
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
    test_uhid(dir);
    fs::remove_all(dir);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
