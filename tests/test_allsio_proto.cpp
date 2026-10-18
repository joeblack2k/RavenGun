// Het USB-IO-protocol van rg-allsio.dll (tools/rg-allsio/allsio_proto.h): de productstring die amdaemon.exe moet accepteren, de rapportindeling en
// de afbeelding van de AllsUsbIo-pijp op het ingangsrapport. De eisen aan de string zijn uit ParseNodeInfo in amdaemon.exe gelezen.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../tools/rg-allsio/allsio_proto.h"

using namespace rg_allsio;

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

void test_productstring() {
    CHECK(check_product_string(product_string()) == 0);
    CHECK(product_string().size() < 0x7f);   // GetProductString: 0xfe bytes = 127 tekens met afsluiter
    // Elke eis uit de code van het spel wordt echt afgedwongen (de controlefunctie vangt wat ParseNodeInfo ook weigert).
    CHECK(check_product_string(L"I/O CONTROL BD;15257;01;90;1831;6679A") != 0);                       // te weinig velden
    CHECK(check_product_string(L"I/O CONTROL B;15257;01;90;1831;6679A;00;COININ=2_SWIN=2,8_GOUT=14") != 0);   // verkeerde naam
    CHECK(check_product_string(L"I/O CONTROL BD;152571234;01;90;1831;6679A;00;COININ=2_SWIN=2,8_GOUT=14") != 0);   // model te lang
    CHECK(check_product_string(L"I/O CONTROL BD;15257;01;90;1831;6679AAA;00;COININ=2_SWIN=2,8_GOUT=14") != 0);   // veld 5 te lang
    CHECK(check_product_string(L"I/O CONTROL BD;15257;01;90;1831;6679A;00;SWIN=2,8") != 0);           // staart mist munt en uitgang
    // config.json van het spel: munt 2 sloten, schakelaars speler 2 / 8 knoppen, uitgang 20 (hex 14), eigen commando 65 (hex 41) param 6.
    const std::wstring p = product_string();
    CHECK(p.find(L"COININ=2") != std::wstring::npos);
    CHECK(p.find(L"SWIN=2,E") != std::wstring::npos);   // E = 14 knoppen >= 8
    CHECK(p.find(L"GOUT=14") != std::wstring::npos);    // 0x14 = 20
    CHECK(p.find(L"UQ1=41,6") != std::wstring::npos);   // 0x41 = 65
    CHECK(manufacturer_string().compare(0, 4, L"SEGA") == 0);
}

void test_platformidentiteit() {
    CHECK(valid_main_id("RAVENGUN0000001"));
    CHECK(valid_main_id("RG-000000000001"));
    CHECK(!valid_main_id(nullptr));
    CHECK(!valid_main_id(""));
    CHECK(!valid_main_id("steamdeck"));
    CHECK(!valid_main_id("RAVENGUN00000001"));
    CHECK(!valid_main_id("ravengun0000001"));
    CHECK(!valid_main_id("RAVENGUN000000!"));
    CHECK(!valid_main_id("RAVENGUN00000 1"));
}

void test_platformapparaten() {
    uint8_t out[12];
    std::memset(out, 0xcc, sizeof out);
    size_t written = 123;
    CHECK(platform_ioctl(PlatformDevice::Keychip, 0x22a114, nullptr, 0, written) == IoctlResult::Ok);
    CHECK(written == 0);
    CHECK(platform_ioctl(PlatformDevice::Hwmon, 0x80006000, out, 3, written) == IoctlResult::ShortBuffer);
    CHECK(written == 0 && out[0] == 0xcc);
    CHECK(platform_ioctl(PlatformDevice::Hwmon, 0x80006000, nullptr, 4, written) == IoctlResult::ShortBuffer);
    CHECK(platform_ioctl(PlatformDevice::Hwmon, 0x80006000, out, 4, written) == IoctlResult::Ok);
    CHECK(written == 4 && out[0] == 52 && out[1] == 0 && out[4] == 0xcc);
    CHECK(platform_ioctl(PlatformDevice::Keychip, 0x22e24c, out, 8, written) == IoctlResult::Ok);
    CHECK(written == 8 && out[0] == 10 && out[4] == 0 && out[5] == 16 && out[8] == 0xcc);
    CHECK(platform_ioctl(PlatformDevice::Keychip, 0xdeadbeef, out, sizeof out, written) == IoctlResult::Unsupported);
    CHECK(written == 0);
    CHECK(platform_ioctl(PlatformDevice::Hwmon, 0x22a114, out, sizeof out, written) == IoctlResult::Unsupported);
    CHECK(platform_ioctl(PlatformDevice::Reset, 0x80002000, out, 4, written) == IoctlResult::Ok);
    CHECK(written == 4 && out[0] == 1);
}

void test_pad() {
    // amdaemon zet het pad in kleine letters en zoekt "vid_0ca3".
    const std::wstring path = device_path();
    CHECK(path.find(L"vid_0ca3") != std::wstring::npos);
    for (wchar_t c : path) CHECK(!(c >= L'A' && c <= L'Z'));
}

void test_verzoeken() {
    Board b;
    uint8_t rpt[kReport] = {};
    rpt[0] = kIdOut;
    rpt[1] = 0x88;   // reset (besturingspijp)
    rpt[2] = 0xD9;
    Request r;
    CHECK(b.on_output(rpt, Pipe::Control, &r));
    CHECK(r.cmd == 0x88 && r.args[0] == 0xD9 && r.pipe == Pipe::Control);
    CHECK(b.last_cmd() == 0x88);
    uint8_t ack[kReport];
    b.control_ack(ack);
    CHECK(ack[0] == kIdIn && ack[1] == 0x88 && ack[2] == 0);
    rpt[1] = 0x02;   // samplingtelling
    rpt[2] = 5;
    CHECK(b.on_output(rpt, Pipe::Interrupt, &r));
    CHECK(b.sampling() == 5);
    rpt[1] = 0x01;
    rpt[2] = 33;
    CHECK(b.on_output(rpt, Pipe::Interrupt));
    CHECK(b.comm_timeout() == 33);
    rpt[1] = 0x03;
    CHECK(b.on_output(rpt, Pipe::Interrupt));
    CHECK(b.status_cleared());
    CHECK(b.requests() == 4);
    // Een rapport met een ander id is niet van ons.
    rpt[0] = 0x55;
    CHECK(!b.on_output(rpt, Pipe::Interrupt));
    CHECK(b.bad() == 1);
}

void test_ingang() {
    // pijp[k] staat op rapportbyte k+1: het rapport-id gaat ervoor, de laatste pijpbyte valt eraf.
    uint8_t pipe[kReport];
    for (size_t i = 0; i < kReport; ++i) pipe[i] = static_cast<uint8_t>(i + 0x40);
    uint8_t rpt[kReport];
    Board::input_frame(pipe, rpt);
    CHECK(rpt[0] == kIdIn);
    CHECK(rpt[1] == 0x40 && rpt[29] == pipe[28] && rpt[32] == pipe[31] && rpt[63] == pipe[62]);   // schakelaars speler 1/2 op 28..31
    Board::input_frame(nullptr, rpt);   // nog geen pijp: stil bord
    CHECK(rpt[0] == kIdIn);
    for (size_t i = 1; i < kReport; ++i) CHECK(rpt[i] == 0);
}

}  // namespace

int main() {
    test_productstring();
    test_platformidentiteit();
    test_platformapparaten();
    test_pad();
    test_verzoeken();
    test_ingang();
    std::printf("%d controles, %d fouten\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
