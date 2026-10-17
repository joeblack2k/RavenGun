// De virtuele camera voor een spel (src/ravengun/vision/camera_feed.*): het bestand dat de daemon schrijft en dat rg-mfcam.dll in
// het spelproces leest (kop, seqlock, beeld), de keuze welke game de camera krijgt ([Camera] in game.ini en [Games] camera in de app,
// dezelfde regels als scripts/camera.py), en de kop-controle van camera_feed_format.h die de DLL ook gebruikt.
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "ravengun/vision/camera_feed.hpp"

using namespace ravengun::vision;
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

std::vector<uint8_t> slurp(const std::string& path) {
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return v;
    uint8_t b[65536];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
    std::fclose(f);
    return v;
}

void test_header() {
    CHECK(sizeof(rg_camfeed_header) == 64);
    rg_camfeed_header h{};
    std::memcpy(h.magic, RG_CAMFEED_MAGIC, 8);
    h.width = 640; h.height = 480; h.stride = 1280; h.fourcc = RG_CAMFEED_FOURCC_YUY2;
    CHECK(rg_camfeed_header_ok(&h));
    CHECK(rg_camfeed_frame_bytes(&h) == 1280u * 480u);
    // De vaste plekken uit camera_feed_format.h (de DLL leest seq op 8, frames op 36).
    CHECK(offsetof(rg_camfeed_header, seq) == 8 && offsetof(rg_camfeed_header, width) == 12 && offsetof(rg_camfeed_header, fourcc) == 24 &&
          offsetof(rg_camfeed_header, frames) == 36 && offsetof(rg_camfeed_header, t_ms) == 40);
    rg_camfeed_header b = h;
    b.magic[0] = 'X';
    CHECK(!rg_camfeed_header_ok(&b));
    b = h; b.fourcc = 0x3231564e;                // NV12 is geen geldig bestandsformaat
    CHECK(!rg_camfeed_header_ok(&b));
    b = h; b.width = 641;                        // YUY2: even breedte
    CHECK(!rg_camfeed_header_ok(&b));
    b = h; b.stride = 1000;                      // stride kleiner dan een regel
    CHECK(!rg_camfeed_header_ok(&b));
    b = h; b.height = 0;
    CHECK(!rg_camfeed_header_ok(&b) && rg_camfeed_frame_bytes(&b) == 0);
    CHECK(RG_CAMFEED_FOURCC_YUY2 == (uint32_t('Y') | uint32_t('U') << 8 | uint32_t('Y') << 16 | uint32_t('2') << 24));
}

void test_writer_reader(const std::string& dir) {
    const std::string path = dir + "/ravengun_Camera";
    CameraFeedWriter w;
    std::string err;
    CHECK(!w.open(path, 641, 480, 1282, 60, 1, err) && !err.empty());   // oneven breedte geweigerd
    CHECK(!fs::exists(path));
    CHECK(w.open(path, 4, 2, 8, 60, 1, err));
    CHECK(fs::file_size(path) == 64 + 16);
    // Nog geen beeld: de lezer weigert (frames = 0, seq oneven).
    {
        const auto f = slurp(path);
        CameraFeedFrame fr;
        CHECK(!camera_feed_read(f.data(), f.size(), fr));
    }
    // Goed belichte beelden (luma rond kFeedTargetLuma): ongewijzigd doorgegeven.
    const uint8_t a[16] = {111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126};
    w.write(a, 1234);
    {
        const auto f = slurp(path);
        CameraFeedFrame fr;
        CHECK(camera_feed_read(f.data(), f.size(), fr));
        CHECK(fr.head.width == 4 && fr.head.height == 2 && fr.head.stride == 8 && fr.head.fps_num == 60 && fr.head.fps_den == 1);
        CHECK(fr.head.frames == 1 && fr.head.t_ms == 1234 && (fr.head.seq & 1u) == 0);
        CHECK(fr.data.size() == 16 && std::memcmp(fr.data.data(), a, 16) == 0);
    }
    uint8_t b[16];
    for (int i = 0; i < 16; ++i) b[i] = static_cast<uint8_t>(120 + i);
    w.write(b, 1250);
    uint32_t seq2 = 0;
    {
        const auto f = slurp(path);
        CameraFeedFrame fr;
        CHECK(camera_feed_read(f.data(), f.size(), fr));
        CHECK(fr.head.frames == 2 && fr.data[0] == 120 && fr.data[15] == 135 && w.gain() == 1.0);
        seq2 = fr.head.seq;
    }
    // Een beeld halverwege (seq oneven, zoals de lezer het tijdens het schrijven ziet): geweigerd.
    {
        auto f = slurp(path);
        const uint32_t odd = seq2 + 1;
        std::memcpy(f.data() + 8, &odd, 4);
        CameraFeedFrame fr;
        CHECK(!camera_feed_read(f.data(), f.size(), fr));
        CHECK(!camera_feed_read(f.data(), 40, fr));   // te kort
    }
    // Een tweede schrijver (de daemon na een herstart) hergebruikt het bestand; dezelfde vorm = geen nieuwe aanleg.
    CHECK(w.open(path, 4, 2, 8, 60, 1, err) && w.frames() == 2);
    // Een andere vorm: opnieuw aangelegd, nog geen beeld.
    CHECK(w.open(path, 2, 2, 4, 30, 1, err) && w.frames() == 0);
    CHECK(fs::file_size(path) == 64 + 8);
    w.close(true);
    CHECK(!fs::exists(path));   // uit = weg: het spel ziet geen camera meer
}

// Een donker beeld (de camera staat op het volgen van de Move) krijgt een versterkte luma, de chroma blijft; begrensd op kFeedMaxGain
// en langzaam bijgesteld (Mario Kart DX, foto bijna zwart, 2026-10-17).
void test_writer_gain(const std::string& dir) {
    const std::string path = dir + "/ravengun_Camera_gain";
    CameraFeedWriter w;
    std::string err;
    CHECK(w.open(path, 4, 2, 8, 60, 1, err));
    uint8_t d[16];
    for (int i = 0; i < 16; i += 2) { d[i] = 22; d[i + 1] = 100; }
    CHECK(feed_gain_target(d, 4, 2, 8) == 5.0);
    w.write(d, 1);
    {
        const auto f = slurp(path);
        CameraFeedFrame fr;
        CHECK(camera_feed_read(f.data(), f.size(), fr) && fr.data[0] == 110 && fr.data[1] == 100 && fr.data[14] == 110 && w.gain() == 5.0);
    }
    for (int i = 0; i < 16; i += 2) d[i] = 1;   // bijna zwart: hooguit kFeedMaxGain, en niet in een keer
    CHECK(feed_gain_target(d, 4, 2, 8) == kFeedMaxGain);
    w.write(d, 2);
    CHECK(w.gain() > 5.0 && w.gain() < kFeedMaxGain);
    for (int i = 0; i < 16; i += 2) d[i] = 250;  // fel: nooit donkerder maken
    CHECK(feed_gain_target(d, 4, 2, 8) == 1.0);
    w.close(true);
}

void test_choice() {
    // Zonder [Camera]: niets.
    auto c = camera_choice("", "", "auto");
    CHECK(c.role == "none" && c.device.empty() && !c.error);
    c = camera_choice("setup", "auto", "auto");
    CHECK(c.device.empty() && !c.error);
    c = camera_choice("presence", "", "auto");
    CHECK(c.device.empty() && !c.error);
    // game: auto = de camera van de daemon, tenzij de app 'geen camera' zegt.
    c = camera_choice("game", "auto", "auto");
    CHECK(c.role == "game" && c.device == "auto");
    c = camera_choice(" Game ", "", "");
    CHECK(c.device == "auto");
    c = camera_choice("game", "auto", "none");
    CHECK(c.device.empty() && !c.error);
    c = camera_choice("game", "auto", "/dev/video2");
    CHECK(c.device == "/dev/video2");
    c = camera_choice("game", "/dev/video1", "none");   // game.ini wint van de app bij een eigen apparaat (zoals camera.py)
    CHECK(c.device == "/dev/video1");
    c = camera_choice("game", "none", "auto");
    CHECK(c.device.empty());
    // Fouten: een ander pad dan /dev/videoN, een onbekende rol.
    c = camera_choice("game", "/etc/passwd", "auto");
    CHECK(c.error && c.device.empty());
    c = camera_choice("game", "auto", "/tmp/x");
    CHECK(c.error && c.device.empty());
    c = camera_choice("photo", "auto", "auto");
    CHECK(c.error && c.device.empty());

    CHECK(app_camera_setting("") == "auto");
    CHECK(app_camera_setting("[Display]\ncamera = none\n") == "auto");        // alleen [Games] telt
    CHECK(app_camera_setting("[Games]\ncamera = none\n") == "none");
    CHECK(app_camera_setting("[games]\r\n Camera=\"/dev/video3\"\r\n") == "/dev/video3");
    CHECK(app_camera_setting("[Games]\ncamera =\n") == "auto");
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/rg-camfeed-XXXXXX";
    const char* d = ::mkdtemp(tmpl);
    CHECK(d != nullptr);
    test_header();
    if (d) { test_writer_reader(d); test_writer_gain(d); }
    test_choice();
    if (d) fs::remove_all(d);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
