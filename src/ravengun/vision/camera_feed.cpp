#include "ravengun/vision/camera_feed.hpp"
#include "ravengun/strutil.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <regex>

namespace ravengun::vision {

static_assert(sizeof(rg_camfeed_header) == RG_CAMFEED_HEADER, "kop van het camerabestand moet 64 bytes zijn");

namespace {

void store_seq(uint8_t* map, uint32_t v) {
    // De seq staat op een uitgelijnd adres (offset 8 in een pagina-uitgelijnde mapping): een atomaire 32-bits schrijfactie, met een
    // release-volgorde zodat de lezer na een even seq ook het beeld ziet.
    __atomic_store_n(reinterpret_cast<uint32_t*>(map + 8), v, __ATOMIC_RELEASE);
}

}  // namespace

bool CameraFeedWriter::open(const std::string& path, uint32_t width, uint32_t height, uint32_t stride, uint32_t fps_num, uint32_t fps_den,
                            std::string& err) {
    rg_camfeed_header h{};
    std::memcpy(h.magic, RG_CAMFEED_MAGIC, 8);
    h.width = width; h.height = height; h.stride = stride; h.fourcc = RG_CAMFEED_FOURCC_YUY2;
    h.fps_num = fps_num; h.fps_den = fps_den;
    if (!rg_camfeed_header_ok(&h)) { err = "camerabestand: ongeldige beeldvorm " + std::to_string(width) + "x" + std::to_string(height); return false; }
    if (map_ && path == path_ && width == w_ && height == h_ && stride == stride_) return true;
    close(false);
    const size_t len = RG_CAMFEED_HEADER + static_cast<size_t>(stride) * height;
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) { err = "camerabestand " + path + ": " + std::strerror(errno); return false; }
    if (::ftruncate(fd, static_cast<off_t>(len)) != 0) {
        err = "camerabestand " + path + ": ftruncate: " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    void* m = ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (m == MAP_FAILED) { err = "camerabestand " + path + ": mmap: " + std::strerror(errno); return false; }
    map_ = static_cast<uint8_t*>(m);
    len_ = len;
    path_ = path;
    w_ = width; h_ = height; stride_ = stride;
    frames_ = 0;
    seq_ = 1;   // oneven: nog geen beeld
    store_seq(map_, seq_);
    h.seq = seq_;
    std::memcpy(map_, &h, sizeof h);
    return true;
}

double feed_gain_target(const uint8_t* frame, uint32_t width, uint32_t height, uint32_t stride) {
    // Gemiddelde luma over een rooster (elke 8e pixel, elke 8e regel); YUY2: Y op de even bytes.
    uint64_t som = 0, n = 0;
    for (uint32_t y = 0; y < height; y += 8)
        for (uint32_t x = 0; x < width; x += 8) { som += frame[static_cast<size_t>(y) * stride + 2u * x]; ++n; }
    const double mean = n ? static_cast<double>(som) / static_cast<double>(n) : kFeedTargetLuma;
    return std::clamp(kFeedTargetLuma / std::max(mean, 1.0), 1.0, kFeedMaxGain);
}

void CameraFeedWriter::write(const uint8_t* frame, uint32_t t_ms) {
    if (!map_ || !frame) return;
    if ((seq_ & 1u) == 0) store_seq(map_, ++seq_);   // oneven: bezig
    // De PS Eye staat afgesteld op het volgen van de Move (korte belichting, gain 0): een foto in het spel wordt dan bijna zwart
    // (Mario Kart DX, gemiddelde luma 10 van 255, 2026-10-17). Het spel krijgt daarom een versterkte luma (gain_, langzaam naar
    // feed_gain_target); de tracker leest de camera zelf en merkt er niets van. Een goed belicht beeld blijft gelijk (gain 1).
    const double doel = feed_gain_target(frame, w_, h_, stride_);
    gain_ = frames_ == 0 ? doel : gain_ + 0.1 * (doel - gain_);
    if (gain_ <= 1.01) {
        std::memcpy(map_ + RG_CAMFEED_HEADER, frame, static_cast<size_t>(stride_) * h_);
    } else {
        uint8_t lut[256];
        for (int i = 0; i < 256; ++i) lut[i] = static_cast<uint8_t>(std::min(255.0, i * gain_ + 0.5));
        const size_t n = static_cast<size_t>(stride_) * h_;
        uint8_t* uit = map_ + RG_CAMFEED_HEADER;
        for (size_t i = 0; i < n; i += 2) { uit[i] = lut[frame[i]]; uit[i + 1] = frame[i + 1]; }
    }
    ++frames_;
    std::memcpy(map_ + 36, &frames_, 4);
    std::memcpy(map_ + 40, &t_ms, 4);
    store_seq(map_, ++seq_);   // even: klaar
}

void CameraFeedWriter::close(bool unlink) {
    if (map_) ::munmap(map_, len_);
    map_ = nullptr;
    len_ = 0;
    if (unlink && !path_.empty()) ::unlink(path_.c_str());
    if (unlink) path_.clear();
}

bool camera_feed_read(const uint8_t* file, size_t len, CameraFeedFrame& out) {
    if (!file || len < RG_CAMFEED_HEADER) return false;
    for (int poging = 0; poging < 4; ++poging) {
        const uint32_t s1 = __atomic_load_n(reinterpret_cast<const uint32_t*>(file + 8), __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;
        rg_camfeed_header h;
        std::memcpy(&h, file, sizeof h);
        const uint32_t n = rg_camfeed_frame_bytes(&h);
        if (!n || h.frames == 0 || len < RG_CAMFEED_HEADER + n) return false;
        out.data.assign(file + RG_CAMFEED_HEADER, file + RG_CAMFEED_HEADER + n);
        const uint32_t s2 = __atomic_load_n(reinterpret_cast<const uint32_t*>(file + 8), __ATOMIC_ACQUIRE);
        if (s1 != s2) continue;
        h.seq = s1;
        out.head = h;
        return true;
    }
    return false;
}

CameraChoice camera_choice(const std::string& use_raw, const std::string& device_raw, const std::string& app_raw) {
    static const std::regex dev(R"(^/dev/video[0-9]+$)");
    CameraChoice c;
    c.role = lower(trim(use_raw));
    if (c.role.empty()) c.role = "none";
    auto apparaat = [&](const std::string& w_raw, const char* bron, std::string& out) {
        const std::string w = trim(w_raw), lw = lower(w);
        if (lw.empty() || lw == "auto") { out = "auto"; return true; }
        if (lw == "none" || lw == "geen" || lw == "off") { out = "none"; return true; }
        if (!std::regex_match(w, dev)) {
            c.error = true;
            c.why = std::string(bron) + ": '" + w_raw + "' is geen camera (auto, none of /dev/videoN)";
            return false;
        }
        out = w;
        return true;
    };
    if (c.role != "game" && c.role != "presence" && c.role != "setup" && c.role != "none") {
        c.error = true;
        c.why = "[Camera] Use = '" + use_raw + "' is geen rol (game, presence, setup of none)";
        c.role = "none";
        return c;
    }
    if (c.role == "none") { c.why = "dit spel vraagt geen camera"; return c; }
    if (c.role == "setup") { c.why = "het spel wil de camera alleen voor de inrichting: niet doorgegeven"; return c; }
    if (c.role == "presence") { c.why = "het spel wil alleen dat er een camera is (een kunstbeeld bestaat nog niet): niets doorgegeven"; return c; }
    std::string eigen, keuze;
    if (!apparaat(device_raw, "[Camera] Device", eigen)) return c;
    if (eigen == "none") { c.why = "[Camera] Device = none"; return c; }
    if (eigen != "auto") { c.device = eigen; c.why = "het spel gebruikt de camera; gekozen in game.ini"; return c; }
    if (!apparaat(app_raw, "de instellingen van de app ([Games] camera)", keuze)) return c;
    if (keuze == "none") { c.why = "het spel gebruikt de camera, maar in de instellingen staat 'geen camera'"; return c; }
    if (keuze != "auto") { c.device = keuze; c.why = "het spel gebruikt de camera; gekozen in de instellingen"; return c; }
    c.device = "auto";
    c.why = "het spel gebruikt de camera; de camera van de daemon";
    return c;
}

std::string app_camera_setting(const std::string& text) {
    std::string sectie;
    size_t i = 0;
    while (i <= text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        const std::string r = trim(text.substr(i, e - i));
        i = e + 1;
        if (r.size() >= 2 && r.front() == '[' && r.back() == ']') { sectie = lower(trim(r.substr(1, r.size() - 2))); continue; }
        const size_t eq = r.find('=');
        if (sectie == "games" && eq != std::string::npos && lower(trim(r.substr(0, eq))) == "camera") {
            std::string v = trim(r.substr(eq + 1));
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            return v.empty() ? "auto" : v;
        }
        if (e == text.size()) break;
    }
    return "auto";
}

}  // namespace ravengun::vision
