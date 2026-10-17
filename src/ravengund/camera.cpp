// Camera-acquisitie (V4L2 mmap) en boltracking. Dit is de enige plek in RavenGun die de camera
// opent. Beeldinhoud blijft in RAM en wordt nooit opgeslagen.
#include "camera.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <memory>
#include <thread>
#include <vector>

#include "ravengun/json.hpp"
#include "ravengun/vision/camera_feed.hpp"
#include "ravengun/vision/frame_timing.hpp"
#include "ravengun/vision/latest_queue.hpp"

namespace ravengund {

using namespace ravengun;
using namespace ravengun::vision;

namespace {

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ::ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

std::string real(const std::string& p) {
    char buf[PATH_MAX];
    if (!::realpath(p.c_str(), buf)) return {};
    return buf;
}

std::vector<std::string> list_dir(const char* dir) {
    std::vector<std::string> r;
    DIR* d = ::opendir(dir);
    if (!d) return r;
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        r.push_back(std::string(dir) + "/" + e->d_name);
    }
    ::closedir(d);
    std::sort(r.begin(), r.end());
    return r;
}

struct Identity {
    std::string requested, path, node, by_id, by_path, driver, card, bus;
};

bool query_cap(const std::string& path, std::string& driver, std::string& card, std::string& bus) {
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    v4l2_capability cap{};
    bool ok = xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
    ::close(fd);
    if (!ok) return false;
    driver = reinterpret_cast<const char*>(cap.driver);
    card = reinterpret_cast<const char*>(cap.card);
    bus = reinterpret_cast<const char*>(cap.bus_info);
    return true;
}

// Kiest de camera op stabiele identiteit. Leeg verzoek: de enige ov534-camera onder by-id.
bool resolve(const std::string& requested, Identity& id, std::string& err) {
    id = Identity{};
    id.requested = requested.empty() ? "auto (ov534 onder /dev/v4l/by-id)" : requested;
    if (!requested.empty()) {
        id.path = requested;
    } else {
        std::vector<std::string> found;
        for (const auto& p : list_dir("/dev/v4l/by-id")) {
            if (p.size() < 13 || p.compare(p.size() - 13, 13, "-video-index0") != 0) continue;
            std::string drv, card, bus;
            if (query_cap(p, drv, card, bus) && drv == "ov534") found.push_back(p);
        }
        if (found.empty()) { err = "geen PS3 Eye (driver ov534) gevonden onder /dev/v4l/by-id; geef --camera PAD"; return false; }
        if (found.size() > 1) { err = "meerdere ov534-camera's; kies er een met --camera /dev/v4l/by-path/..."; return false; }
        id.path = found[0];
    }
    id.node = real(id.path);
    if (id.node.empty()) { err = "camerapad bestaat niet: " + id.path; return false; }
    for (const auto& p : list_dir("/dev/v4l/by-id")) if (real(p) == id.node) { id.by_id = p; break; }
    for (const auto& p : list_dir("/dev/v4l/by-path")) if (real(p) == id.node) { id.by_path = p; break; }
    if (!query_cap(id.node, id.driver, id.card, id.bus)) { err = "kan " + id.node + " niet openen of bevragen: " + std::strerror(errno); return false; }
    return true;
}

struct CtrlInfo {
    std::string name;
    uint32_t id = 0;
    uint32_t type = 0;
    int64_t min = 0, max = 0, step = 0, def = 0;
    int64_t before = 0, after = 0;
    bool readable_before = false, readable_after = false;
    uint32_t flags_after = 0;
    std::string requested;  // leeg = niet aangepast
    std::string set_error;
};

const char* ctrl_type(uint32_t t) {
    switch (t) {
        case V4L2_CTRL_TYPE_INTEGER: return "int";
        case V4L2_CTRL_TYPE_BOOLEAN: return "bool";
        case V4L2_CTRL_TYPE_MENU: return "menu";
        case V4L2_CTRL_TYPE_BUTTON: return "button";
        case V4L2_CTRL_TYPE_INTEGER_MENU: return "intmenu";
    }
    return "other";
}

bool get_ctrl(int fd, uint32_t id, int64_t& v) {
    v4l2_control c{};
    c.id = id;
    if (xioctl(fd, VIDIOC_G_CTRL, &c) != 0) return false;
    v = c.value;
    return true;
}

std::vector<CtrlInfo> enum_ctrls(int fd) {
    std::vector<CtrlInfo> r;
    v4l2_query_ext_ctrl q{};
    q.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    while (xioctl(fd, VIDIOC_QUERY_EXT_CTRL, &q) == 0) {
        if (q.type != V4L2_CTRL_TYPE_CTRL_CLASS && !(q.flags & V4L2_CTRL_FLAG_DISABLED)) {
            CtrlInfo c;
            c.name = q.name;
            c.id = q.id;
            c.type = q.type;
            c.min = q.minimum; c.max = q.maximum; c.step = static_cast<int64_t>(q.step); c.def = q.default_value;
            c.readable_before = get_ctrl(fd, q.id, c.before);
            r.push_back(c);
        }
        q.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }
    return r;
}

// Zet een control; noteert gevraagde waarde en eventuele fout bij de bijbehorende CtrlInfo.
void set_ctrl(int fd, std::vector<CtrlInfo>& cs, uint32_t cid, int64_t v) {
    for (auto& c : cs) {
        if (c.id != cid) continue;
        c.requested = std::to_string(v);
        c.set_error.clear();
        v4l2_control vc{};
        vc.id = cid;
        vc.value = static_cast<int32_t>(v);
        if (xioctl(fd, VIDIOC_S_CTRL, &vc) != 0) c.set_error = std::strerror(errno);
        return;
    }
}

void refresh_after(int fd, std::vector<CtrlInfo>& cs) {
    for (auto& c : cs) {
        c.readable_after = get_ctrl(fd, c.id, c.after);
        v4l2_query_ext_ctrl qq{};
        qq.id = c.id;
        if (xioctl(fd, VIDIOC_QUERY_EXT_CTRL, &qq) == 0) c.flags_after = qq.flags;
    }
}

CameraControls read_controls(int fd) {
    CameraControls r;
    int64_t ae = 0, ag = 0, wb = 0, ex = 0, ga = 0;
    bool a = get_ctrl(fd, V4L2_CID_EXPOSURE_AUTO, ae), b = get_ctrl(fd, V4L2_CID_AUTOGAIN, ag),
         c = get_ctrl(fd, V4L2_CID_AUTO_WHITE_BALANCE, wb), d = get_ctrl(fd, V4L2_CID_EXPOSURE, ex),
         e = get_ctrl(fd, V4L2_CID_GAIN, ga);
    r.readable = a && b && c && d && e;
    r.auto_exposure = ae != V4L2_EXPOSURE_MANUAL;
    r.auto_gain = ag != 0;
    r.auto_awb = wb != 0;
    r.exposure = static_cast<int>(ex);
    r.gain = static_cast<int>(ga);
    return r;
}

struct Frame {
    std::vector<uint8_t> data;
    int width = 0, height = 0, stride = 0;
    uint32_t seq = 0;
    int64_t t_frame_ns = 0, t_dq_ns = 0;
};

}  // namespace

struct CameraService::Impl {
    SharedState& sh;
    CameraOptions opt;
    std::thread cap, trk;
    std::atomic<bool> run{false};
    LatestQueue<Frame> q;

    mutable std::mutex m;  // beschermt alles hieronder
    std::string state = "disabled";
    std::string error;
    Identity ident;
    uint32_t fourcc = 0;
    int width = 0, height = 0, stride = 0;
    uint32_t sizeimage = 0;
    double driver_fps = 0;
    uint32_t ts_flags = 0;
    bool ts_known = false;
    std::vector<CtrlInfo> ctrls;
    FrameTiming timing;
    uint64_t error_frames = 0, short_frames = 0, opens = 0, stream_errors = 0, poll_timeouts = 0;
    // Tracker
    uint64_t processed = 0, valid = 0, roi_hits = 0, full_frame = 0;
    std::array<uint64_t, kReasonCount> reasons{};
    Ring radius{8192};
    TrackResult last;
    SceneStats scene;
    bool have_scene = false;

    // Live wijzigingen (set_camera): de capture-thread haalt ze op en voert ze uit.
    struct Pending { CameraChange ch; bool done = false; CameraApplyResult res; };
    std::mutex pm;
    std::condition_variable pcv;
    std::shared_ptr<Pending> pending;
    std::atomic<bool> has_pending{false};
    // Nieuwe trackerconfiguratie (andere LED-kleur): de tracker-thread neemt hem over.
    std::mutex tm;
    // Een tracker per speler (kleur van zijn bol); speler 1 staat altijd aan, 2..4 alleen met een Move.
    std::array<TrackerConfig, kPlayers> pending_tracker{};
    std::array<bool, kPlayers> tracker_on{{true, false, false, false}};
    std::atomic<uint64_t> tracker_gen{0};
    // De virtuele camera voor een spel (set_game_feed): aan/uit en het pad; de tracker-thread schrijft (en is de enige die `feed` aanraakt).
    std::atomic<bool> feed_on{false};
    std::atomic<uint64_t> feed_gen{0};
    // Foto-belichting (set_game_photo): gevraagd door de IO-brug, uitgevoerd door de capture-thread (de enige met de fd).
    std::atomic<bool> photo_want{false};
    std::mutex fm;
    std::string feed_path = ravengun::vision::kCameraFeedPath;
    ravengun::vision::CameraFeedWriter feed;
    // Per speler (onder m): geldige metingen en het laatste resultaat (speler 1 staat ook in de bestaande velden).
    std::array<uint64_t, kPlayers> p_processed{}, p_valid{};
    std::array<TrackResult, kPlayers> p_last{};
    std::array<TrackerConfig, kPlayers> p_cfg{};

    Impl(SharedState& s, CameraOptions o) : sh(s), opt(std::move(o)), q(static_cast<size_t>(std::max(1, opt.queue_capacity))) {
        pending_tracker[0] = opt.tracker;
        p_cfg[0] = opt.tracker;
    }

    void set_state(const char* st, const std::string& err = {}) {
        {
            std::lock_guard<std::mutex> g(m);
            state = st;
            error = err;
        }
        if (std::strcmp(st, "streaming") != 0) {
            core::OpticalSample o;
            o.camera_active = false;
            o.camera_state = st;
            o.reason = std::strcmp(st, "error") == 0 ? "camera_error" : "no_frames";
            for (int p = 1; p <= kPlayers; ++p) sh.publish_optical(p, o);
        }
    }

    // Zet de controls volgens opt (alleen aanroepen uit de capture-thread). De driver onthoudt eerdere
    // waarden, dus de auto-vlaggen worden altijd expliciet gezet; vaste waarden alleen bij auto uit.
    void apply_controls(int fd, std::vector<CtrlInfo>& cs) {
        set_ctrl(fd, cs, V4L2_CID_EXPOSURE_AUTO, opt.auto_exposure ? V4L2_EXPOSURE_AUTO : V4L2_EXPOSURE_MANUAL);
        set_ctrl(fd, cs, V4L2_CID_AUTOGAIN, opt.auto_gain ? 1 : 0);
        set_ctrl(fd, cs, V4L2_CID_AUTO_WHITE_BALANCE, opt.auto_awb ? 1 : 0);
        if (!opt.auto_exposure) set_ctrl(fd, cs, V4L2_CID_EXPOSURE, opt.exposure);
        if (!opt.auto_gain) set_ctrl(fd, cs, V4L2_CID_GAIN, opt.gain);
    }

    void close_device(int& fd, std::vector<std::pair<void*, size_t>>& maps) {
        if (fd >= 0) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            xioctl(fd, VIDIOC_STREAMOFF, &type);
        }
        for (auto& mp : maps) ::munmap(mp.first, mp.second);
        maps.clear();
        if (fd >= 0) ::close(fd);
        fd = -1;
    }

    // Voert een set_camera-wijziging uit (capture-thread). fd < 0: geen actieve camera.
    CameraApplyResult do_apply(const CameraChange& ch, int& fd, std::vector<std::pair<void*, size_t>>& maps) {
        CameraApplyResult r;
        if (fd < 0) {
            std::string st, er;
            { std::lock_guard<std::mutex> g(m); st = state; er = error; }
            r.code = "unavailable";
            r.message = "camera niet actief (toestand: " + st + (er.empty() ? "" : ", " + er) + ")";
            return r;
        }
        const CameraOptions old = opt;  // alleen voor het terugdraaien van modus en controls
        bool mode_change = ch.width && ch.height && ch.fps && (*ch.width != opt.width || *ch.height != opt.height || *ch.fps != opt.fps);
        {
            std::lock_guard<std::mutex> g(m);
            if (ch.exposure) opt.exposure = *ch.exposure;
            if (ch.gain) opt.gain = *ch.gain;
            if (ch.auto_exposure) opt.auto_exposure = *ch.auto_exposure;
            if (ch.auto_gain) opt.auto_gain = *ch.auto_gain;
            if (ch.auto_awb) opt.auto_awb = *ch.auto_awb;
            if (mode_change) { opt.width = *ch.width; opt.height = *ch.height; opt.fps = *ch.fps; }
        }
        if (mode_change) {
            r.restarted = true;
            close_device(fd, maps);
            fd = open_device(maps);
            if (fd < 0) {
                std::string e;
                {
                    std::lock_guard<std::mutex> g(m);
                    e = error;
                    opt.width = old.width; opt.height = old.height; opt.fps = old.fps;
                    opt.exposure = old.exposure; opt.gain = old.gain;
                    opt.auto_exposure = old.auto_exposure; opt.auto_gain = old.auto_gain; opt.auto_awb = old.auto_awb;
                }
                logf("waarschuwing", "camera: nieuwe modus %dx%d@%d mislukt (%s); oude modus herstellen", *ch.width, *ch.height, *ch.fps, e.c_str());
                fd = open_device(maps);
                r.code = "internal";
                r.message = "nieuwe modus mislukt: " + e + (fd >= 0 ? "; de oude modus is hersteld" : "; de camera is ook niet hersteld, er volgt automatisch een nieuwe poging");
                return r;
            }
        } else {
            std::vector<CtrlInfo> cs;
            { std::lock_guard<std::mutex> g(m); cs = ctrls; }
            apply_controls(fd, cs);
            refresh_after(fd, cs);
            std::lock_guard<std::mutex> g(m);
            ctrls = cs;
        }
        r.ok = true;
        {
            std::lock_guard<std::mutex> g(m);
            r.width = width; r.height = height; r.fps = opt.fps; r.driver_fps = driver_fps;
        }
        r.controls = read_controls(fd);
        if (!r.controls.readable) r.warnings.push_back("de driver liet niet alle controls teruglezen");
        if (r.controls.auto_exposure != opt.auto_exposure) r.warnings.push_back("auto-belichting: de driver meldt een andere stand dan gevraagd");
        if (r.controls.auto_gain != opt.auto_gain) r.warnings.push_back("auto-gain: de driver meldt een andere stand dan gevraagd");
        if (r.controls.auto_awb != opt.auto_awb) r.warnings.push_back("auto-witbalans: de driver meldt een andere stand dan gevraagd");
        if (!opt.auto_exposure && r.controls.exposure != opt.exposure)
            r.warnings.push_back("belichting: gevraagd " + std::to_string(opt.exposure) + ", de driver meldt " + std::to_string(r.controls.exposure));
        if (!opt.auto_gain && r.controls.gain != opt.gain)
            r.warnings.push_back("gain: gevraagd " + std::to_string(opt.gain) + ", de driver meldt " + std::to_string(r.controls.gain));
        return r;
    }

    void handle_pending(int& fd, std::vector<std::pair<void*, size_t>>& maps) {
        std::shared_ptr<Pending> p;
        {
            std::lock_guard<std::mutex> g(pm);
            p = pending;
            pending.reset();
            has_pending.store(false);
        }
        if (!p) return;
        CameraApplyResult r = do_apply(p->ch, fd, maps);
        {
            std::lock_guard<std::mutex> g(pm);
            p->res = std::move(r);
            p->done = true;
        }
        pcv.notify_all();
    }

    // Open + configureren + mmap + streamon. -1 bij een fout (error gezet).
    int open_device(std::vector<std::pair<void*, size_t>>& maps) {
        Identity id;
        std::string err;
        if (!resolve(opt.device, id, err)) { set_state("error", err); return -1; }
        int fd = ::open(id.path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) { set_state("error", "open " + id.path + ": " + std::strerror(errno)); return -1; }
        auto fail = [&](const std::string& what) {
            std::string e = what + ": " + std::strerror(errno);
            if (errno == EBUSY) e += " (camera al in gebruik door een ander proces?)";
            for (auto& mp : maps) ::munmap(mp.first, mp.second);
            maps.clear();
            ::close(fd);
            set_state("error", e);
            return -1;
        };
        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = static_cast<uint32_t>(opt.width);
        fmt.fmt.pix.height = static_cast<uint32_t>(opt.height);
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) return fail("VIDIOC_S_FMT");
        if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV || static_cast<int>(fmt.fmt.pix.width) != opt.width ||
            static_cast<int>(fmt.fmt.pix.height) != opt.height) {
            errno = EINVAL;
            return fail("camera ondersteunt " + std::to_string(opt.width) + "x" + std::to_string(opt.height) + " YUYV niet");
        }
        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(opt.fps);
        if (xioctl(fd, VIDIOC_S_PARM, &parm) < 0) return fail("VIDIOC_S_PARM");
        double dfps = parm.parm.capture.timeperframe.numerator
                          ? static_cast<double>(parm.parm.capture.timeperframe.denominator) / parm.parm.capture.timeperframe.numerator
                          : 0;

        // Controls: eerst alles lezen, dan de automatiek en vaste waarden volgens de opties zetten.
        std::vector<CtrlInfo> cs = enum_ctrls(fd);
        apply_controls(fd, cs);

        v4l2_requestbuffers req{};
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) return fail("VIDIOC_REQBUFS");
        for (uint32_t i = 0; i < req.count; ++i) {
            v4l2_buffer b{};
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            b.memory = V4L2_MEMORY_MMAP;
            b.index = i;
            if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) return fail("VIDIOC_QUERYBUF");
            void* p = ::mmap(nullptr, b.length, PROT_READ, MAP_SHARED, fd, b.m.offset);
            if (p == MAP_FAILED) return fail("mmap");
            maps.emplace_back(p, b.length);
            if (xioctl(fd, VIDIOC_QBUF, &b) < 0) return fail("VIDIOC_QBUF");
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) return fail("VIDIOC_STREAMON");
        // Na streamon nogmaals de controls lezen: gspca zet ze bij de start opnieuw.
        for (auto& c : cs) {
            c.readable_after = get_ctrl(fd, c.id, c.after);
            v4l2_query_ext_ctrl qq{};
            qq.id = c.id;
            if (xioctl(fd, VIDIOC_QUERY_EXT_CTRL, &qq) == 0) c.flags_after = qq.flags;
        }
        {
            std::lock_guard<std::mutex> g(m);
            ident = id;
            fourcc = fmt.fmt.pix.pixelformat;
            width = static_cast<int>(fmt.fmt.pix.width);
            height = static_cast<int>(fmt.fmt.pix.height);
            stride = static_cast<int>(fmt.fmt.pix.bytesperline);
            sizeimage = fmt.fmt.pix.sizeimage;
            driver_fps = dfps;
            ctrls = cs;
            ++opens;
            timing.reset();
            ts_known = false;
        }
        std::string cl;
        for (const auto& c : cs) {
            if (!c.requested.empty()) cl += " " + c.name + "=" + std::to_string(c.after) + (c.set_error.empty() ? "" : "(FOUT " + c.set_error + ")");
        }
        logf("info", "camera %s (%s, %s) %dx%d YUYV, driver %.2f fps; controls:%s", id.by_id.empty() ? id.path.c_str() : id.by_id.c_str(),
             id.node.c_str(), id.driver.c_str(), width, height, dfps, cl.c_str());
        set_state("streaming");
        return fd;
    }

    void capture_main() {
        std::vector<std::pair<void*, size_t>> maps;
        int fd = -1;
        int64_t next_try = 0;
        int64_t last_frame = 0;
        bool photo_have = false;
        CameraOptions saved;   // de instellingen van voor de foto-belichting
        while (run.load()) {
            if (fd >= 0 && photo_want.load() != photo_have) {
                CameraChange ch;
                if (!photo_have) {
                    { std::lock_guard<std::mutex> g(m); saved = opt; }
                    ch.auto_exposure = true; ch.auto_gain = true; ch.auto_awb = true;
                } else {
                    ch.exposure = saved.exposure; ch.gain = saved.gain;
                    ch.auto_exposure = saved.auto_exposure; ch.auto_gain = saved.auto_gain; ch.auto_awb = saved.auto_awb;
                }
                photo_have = !photo_have;
                const CameraApplyResult r = do_apply(ch, fd, maps);
                if (photo_have) logf("info", "camera: foto-belichting voor het spel aan (automatische belichting, gain en witbalans)%s", r.ok ? "" : (": " + r.message).c_str());
                else logf("info", "camera: foto-belichting uit, terug naar belichting %d, gain %d%s", saved.exposure, saved.gain, r.ok ? "" : (": " + r.message).c_str());
                last_frame = mono_ns();
                continue;
            }
            if (has_pending.load()) {
                handle_pending(fd, maps);
                last_frame = mono_ns();
                if (fd < 0) next_try = mono_ns() + 2'000'000'000;
                continue;
            }
            if (fd < 0) {
                int64_t now = mono_ns();
                if (now < next_try) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
                set_state("opening");
                fd = open_device(maps);
                if (fd < 0) {
                    std::string e;
                    { std::lock_guard<std::mutex> g(m); e = error; }
                    logf("waarschuwing", "camera: %s; opnieuw over 2 s", e.c_str());
                    next_try = mono_ns() + 2'000'000'000;
                    continue;
                }
                last_frame = mono_ns();
            }
            pollfd pf{fd, POLLIN, 0};
            int pr = ::poll(&pf, 1, 200);
            if (pr < 0 && errno != EINTR) break;
            bool broken = false;
            if (pr == 0 || !(pf.revents & POLLIN)) {
                if (pf.revents & (POLLERR | POLLHUP | POLLNVAL)) broken = true;
                else if (mono_ns() - last_frame > 2'000'000'000) {
                    std::lock_guard<std::mutex> g(m);
                    ++poll_timeouts;
                    broken = true;
                }
            }
            while (!broken && (pf.revents & POLLIN)) {
                v4l2_buffer b{};
                b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                b.memory = V4L2_MEMORY_MMAP;
                if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) {
                    if (errno == EAGAIN) break;
                    broken = true;
                    break;
                }
                int64_t t_dq = mono_ns();
                last_frame = t_dq;
                int64_t t_frame = static_cast<int64_t>(b.timestamp.tv_sec) * 1'000'000'000 + static_cast<int64_t>(b.timestamp.tv_usec) * 1000;
                bool monotonic = (b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
                if (!monotonic) t_frame = t_dq;  // geen bruikbare klok: ontvangsttijd (zichtbaar in status)
                bool bad = (b.flags & V4L2_BUF_FLAG_ERROR) != 0;
                bool shrt = b.bytesused < static_cast<uint32_t>(stride) * static_cast<uint32_t>(height);
                {
                    std::lock_guard<std::mutex> g(m);
                    if (!ts_known) { ts_flags = b.flags; ts_known = true; }
                    timing.on_frame(t_frame, b.sequence, t_dq);
                    if (bad) ++error_frames;
                    if (shrt) ++short_frames;
                }
                if (!bad && !shrt && b.index < maps.size()) {
                    Frame f = q.acquire();
                    const uint8_t* p = static_cast<const uint8_t*>(maps[b.index].first);
                    f.data.assign(p, p + static_cast<size_t>(stride) * static_cast<size_t>(height));
                    f.width = width; f.height = height; f.stride = stride;
                    f.seq = b.sequence; f.t_frame_ns = t_frame; f.t_dq_ns = t_dq;
                    if (xioctl(fd, VIDIOC_QBUF, &b) < 0) broken = true;
                    q.push(std::move(f));
                } else if (xioctl(fd, VIDIOC_QBUF, &b) < 0) {
                    broken = true;
                }
            }
            if (broken) {
                {
                    std::lock_guard<std::mutex> g(m);
                    ++stream_errors;
                }
                logf("waarschuwing", "camera: stream onderbroken (%s); opnieuw openen", std::strerror(errno));
                v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                xioctl(fd, VIDIOC_STREAMOFF, &type);
                for (auto& mp : maps) ::munmap(mp.first, mp.second);
                maps.clear();
                ::close(fd);
                fd = -1;
                set_state("error", "stream onderbroken");
                next_try = mono_ns() + 1'000'000'000;
            }
        }
        if (fd >= 0) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            xioctl(fd, VIDIOC_STREAMOFF, &type);
            for (auto& mp : maps) ::munmap(mp.first, mp.second);
            ::close(fd);
        }
        q.close();
        set_state("disabled");
        {
            std::lock_guard<std::mutex> g(pm);
            if (pending) {
                pending->res.code = "unavailable";
                pending->res.message = "camera gestopt";
                pending->done = true;
                pending.reset();
                has_pending.store(false);
            }
        }
        pcv.notify_all();
        logf("info", "camera-thread gestopt");
    }

    void tracker_main() {
        std::array<TrackerConfig, kPlayers> cfg;
        std::array<bool, kPlayers> on;
        { std::lock_guard<std::mutex> g(tm); cfg = pending_tracker; on = tracker_on; }
        std::vector<BallTracker> trackers;
        for (const auto& c : cfg) trackers.emplace_back(c);
        uint64_t seen_gen = tracker_gen.load();
        uint64_t seen_feed = feed_gen.load();
        bool feed_failed = false;
        int seen_w = 0, seen_h = 0;
        while (run.load()) {
            if (tracker_gen.load() != seen_gen) {
                std::lock_guard<std::mutex> g(tm);
                for (int i = 0; i < kPlayers; ++i) {
                    // Andere LED-kleur: nieuwe tint, volgtoestand gewist. Een speler die erbij komt begint ook schoon.
                    if (pending_tracker[i].hue_deg != cfg[i].hue_deg || pending_tracker[i].hue_tol_deg != cfg[i].hue_tol_deg ||
                        pending_tracker[i].min_chroma != cfg[i].min_chroma || (tracker_on[i] && !on[i]))
                        trackers[static_cast<size_t>(i)].set_config(pending_tracker[i]);
                }
                cfg = pending_tracker;
                on = tracker_on;
                seen_gen = tracker_gen.load();
            }
            auto f = q.pop_latest(std::chrono::milliseconds(200));
            if (feed_gen.load() != seen_feed) {   // aan, uit of een ander pad: eerst het oude bestand weg
                seen_feed = feed_gen.load();
                feed_failed = false;
                if (feed.is_open()) {
                    logf("info", "camera voor het spel: %s gestopt na %u beelden", feed.path().c_str(), feed.frames());
                    feed.close(true);
                }
            }
            if (!f) continue;
            if (feed_on.load()) {
                if (!feed.is_open()) {
                    std::string path, e;
                    { std::lock_guard<std::mutex> g(fm); path = feed_path; }
                    const uint32_t fps = static_cast<uint32_t>(std::max(0, opt.fps));
                    if (feed.open(path, static_cast<uint32_t>(f->width), static_cast<uint32_t>(f->height), static_cast<uint32_t>(f->stride), fps, 1, e))
                        logf("info", "camera voor het spel: beeld %dx%d YUY2 naar %s", f->width, f->height, path.c_str());
                    else if (!feed_failed) {
                        logf("waarschuwing", "camera voor het spel: %s", e.c_str());
                        feed_failed = true;
                    }
                } else if (feed.width() != static_cast<uint32_t>(f->width) || feed.height() != static_cast<uint32_t>(f->height)) {
                    feed.close(true);   // modus gewisseld: volgende beeld legt het bestand opnieuw aan
                }
                if (feed.is_open()) feed.write(f->data.data(), static_cast<uint32_t>(f->t_frame_ns / 1'000'000));
            }
            if (f->width != seen_w || f->height != seen_h) {  // modus gewisseld: oude volgtoestand is ongeldig
                if (seen_w) for (auto& t : trackers) t.reset();
                seen_w = f->width; seen_h = f->height;
            }
            ImageView v{f->data.data(), f->width, f->height, f->stride, PixelFormat::YUYV};
            for (int i = 0; i < kPlayers; ++i) {
                if (!on[static_cast<size_t>(i)]) continue;
                BallTracker& tracker = trackers[static_cast<size_t>(i)];
                int64_t t0 = mono_ns();
                TrackResult r = tracker.process(v);
                // Scènestatistiek alleen voor speler 1 (zoals voorheen).
                bool do_scene = i == 0 && opt.scene_every_frames > 0 && (processed % static_cast<uint64_t>(opt.scene_every_frames)) == 0;
                SceneStats sc;
                if (do_scene) sc = tracker.scene_stats(v, 4);
                int64_t t1 = mono_ns();

                core::OpticalSample o;
                o.camera_active = true;
                o.camera_state = "streaming";
                o.valid = r.valid;
                o.reason = to_string(r.reason);
                o.track_state = to_string(r.state);
                o.frame_seq = f->seq;
                o.t_frame_ns = f->t_frame_ns;
                o.t_processed_ns = t1;
                o.width = r.width; o.height = r.height;
                o.x_px = r.x_px; o.y_px = r.y_px; o.x_norm = r.x_norm; o.y_norm = r.y_norm; o.radius_px = r.radius_px;
                o.candidates = r.candidates;
                {
                    std::lock_guard<std::mutex> g(m);
                    ++p_processed[static_cast<size_t>(i)];
                    if (r.valid) ++p_valid[static_cast<size_t>(i)];
                    p_last[static_cast<size_t>(i)] = r;
                    p_cfg[static_cast<size_t>(i)] = cfg[static_cast<size_t>(i)];
                    if (i == 0) {
                        ++processed;
                        o.processed = processed;
                        timing.on_processed(t0 - f->t_frame_ns, static_cast<double>(t1 - t0) / 1e3);
                        if (r.valid) { ++valid; radius.add(r.radius_px); }
                        if (r.used_roi) ++roi_hits;
                        if (r.full_frame_searched) ++full_frame;
                        ++reasons[static_cast<size_t>(r.reason)];
                        last = r;
                        if (do_scene) { scene = sc; have_scene = true; }
                    } else {
                        o.processed = p_processed[static_cast<size_t>(i)];
                    }
                }
                sh.publish_optical(i + 1, o);
            }
            q.recycle(std::move(*f));
        }
        if (feed.is_open()) feed.close(true);
        logf("info", "tracker-thread gestopt");
    }
};

CameraService::CameraService(SharedState& shared, CameraOptions opt) : d_(std::make_unique<Impl>(shared, std::move(opt))) {}
CameraService::~CameraService() { stop(); }

void CameraService::start() {
    if (!d_->opt.enabled || d_->run.load()) return;
    d_->run.store(true);
    d_->cap = std::thread([this] { d_->capture_main(); });
    d_->trk = std::thread([this] { d_->tracker_main(); });
}

void CameraService::stop() {
    if (!d_->run.exchange(false)) return;
    d_->q.close();
    if (d_->cap.joinable()) d_->cap.join();
    if (d_->trk.joinable()) d_->trk.join();
}

bool CameraService::enabled() const { return d_->opt.enabled; }

void CameraService::set_game_feed(bool on, const std::string& why, const std::string& path) {
    {
        std::lock_guard<std::mutex> g(d_->fm);
        const std::string p = path.empty() ? std::string(ravengun::vision::kCameraFeedPath) : path;
        if (on == d_->feed_on.load() && p == d_->feed_path) return;
        d_->feed_path = p;
    }
    d_->feed_on.store(on);
    d_->feed_gen.fetch_add(1);
    if (!on) ::unlink(d_->feed_path.c_str());   // ook zonder lopende tracker (camera uit): geen oud bestand laten staan
    logf("info", "camera voor het spel %s: %s", on ? "aan" : "uit", why.c_str());
}

bool CameraService::game_feed() const { return d_->feed_on.load(); }

void CameraService::set_game_photo(bool photo) { d_->photo_want.store(photo); }

std::string CameraService::device_path() const {
    std::lock_guard<std::mutex> g(d_->m);
    return d_->ident.path;
}

vision::TrackerConfig CameraService::tracker(int player) const {
    if (player <= 1) {
        std::lock_guard<std::mutex> g(d_->m);
        return d_->opt.tracker;
    }
    std::lock_guard<std::mutex> g(d_->tm);
    return d_->pending_tracker[static_cast<size_t>(SharedState::clamp_player(player) - 1)];
}

void CameraService::set_tracker(int player, const vision::TrackerConfig& cfg) {
    const size_t i = static_cast<size_t>(SharedState::clamp_player(player) - 1);
    {
        std::lock_guard<std::mutex> g(d_->tm);
        d_->pending_tracker[i] = cfg;
        d_->tracker_gen.fetch_add(1);
    }
    if (i == 0) {
        std::lock_guard<std::mutex> g(d_->m);
        d_->opt.tracker = cfg;
    }
}

void CameraService::set_tracker_enabled(int player, bool on) {
    if (player <= 1) return;  // speler 1 zoekt altijd
    const size_t i = static_cast<size_t>(SharedState::clamp_player(player) - 1);
    std::lock_guard<std::mutex> g(d_->tm);
    if (d_->tracker_on[i] == on) return;
    d_->tracker_on[i] = on;
    d_->tracker_gen.fetch_add(1);
}

CameraApplyResult CameraService::apply(const CameraChange& ch) {
    CameraApplyResult r;
    r.code = "unavailable";
    if (!d_->opt.enabled) { r.message = "de camera staat uit (ravengund draait met --no-camera)"; return r; }
    if (!d_->run.load()) { r.message = "de camera-service draait niet"; return r; }
    auto p = std::make_shared<Impl::Pending>();
    p->ch = ch;
    std::unique_lock<std::mutex> lk(d_->pm);
    if (d_->pending) { r.message = "er loopt al een camerawijziging; probeer het zo opnieuw"; return r; }
    d_->pending = p;
    d_->has_pending.store(true);
    if (!d_->pcv.wait_for(lk, std::chrono::seconds(5), [&] { return p->done; })) {
        if (d_->pending == p) { d_->pending.reset(); d_->has_pending.store(false); }
        r.message = "de camera reageerde niet binnen 5 s";
        return r;
    }
    return p->res;
}

std::string CameraService::status_json() const {
    const Impl& d = *d_;
    auto qc = d.q.counters();
    std::lock_guard<std::mutex> g(d.m);
    json::Writer w;
    w.begin_object();
    w.key("enabled").value(d.opt.enabled);
    w.key("state").value(d.state);
    if (d.error.empty()) w.key("error").null(); else w.key("error").value(d.error);
    w.key("device").begin_object();
    w.key("requested").value(d.ident.requested).key("path").value(d.ident.path).key("node").value(d.ident.node);
    w.key("by_id").value(d.ident.by_id).key("by_path").value(d.ident.by_path);
    w.key("driver").value(d.ident.driver).key("card").value(d.ident.card).key("bus_info").value(d.ident.bus);
    w.key("opens").value(static_cast<unsigned long long>(d.opens));
    w.end_object();
    w.key("game_feed").value(d.feed_on.load());   // de virtuele camera voor een spel (set_game_feed)

    w.key("mode").begin_object();
    w.key("format").value(d.fourcc == V4L2_PIX_FMT_YUYV ? "YUYV" : (d.fourcc ? "?" : ""));
    w.key("width").value(d.width).key("height").value(d.height);
    w.key("bytesperline").value(d.stride).key("sizeimage").value(static_cast<unsigned long long>(d.sizeimage));
    w.key("requested_fps").value(d.opt.fps).key("driver_fps").value(d.driver_fps);
    w.key("v4l2_buffers").value(4).key("queue_capacity").value(static_cast<unsigned long long>(d.q.capacity()));
    w.end_object();

    w.key("timestamps").begin_object();
    if (d.ts_known) {
        uint32_t t = d.ts_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK, s = d.ts_flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
        w.key("type").value(t == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC ? "monotonic" : t == V4L2_BUF_FLAG_TIMESTAMP_COPY ? "copy" : "unknown");
        w.key("source").value(s == V4L2_BUF_FLAG_TSTAMP_SRC_SOE ? "SOE" : "EOF");
    } else {
        w.key("type").null().key("source").null();
    }
    w.key("meaning").value("t_frame = V4L2-buffertimestamp (CLOCK_MONOTONIC, einde van het frame); geen belichtingstijd");
    w.end_object();

    const FrameTiming& t = d.timing;
    w.key("frames").begin_object();
    w.key("received").value(static_cast<unsigned long long>(t.frames));
    w.key("fps_total").value(t.fps_total()).key("fps_5s").value(t.fps_window(5.0));
    w.key("missed_by_sequence").value(static_cast<unsigned long long>(t.missed));
    w.key("gap_events").value(static_cast<unsigned long long>(t.gap_events));
    w.key("non_monotonic").value(static_cast<unsigned long long>(t.non_monotonic));
    w.key("sequence_resets").value(static_cast<unsigned long long>(t.sequence_reset));
    w.key("error_frames").value(static_cast<unsigned long long>(d.error_frames));
    w.key("short_frames").value(static_cast<unsigned long long>(d.short_frames));
    w.key("stream_errors").value(static_cast<unsigned long long>(d.stream_errors));
    w.end_object();
    auto ring = [&](const char* k, const Ring& r) {
        w.key(k).begin_object();
        w.key("n").value(static_cast<unsigned long long>(r.size()));
        w.key("mean").value(r.mean()).key("p50").value(r.percentile(0.5)).key("p95").value(r.percentile(0.95));
        w.key("p99").value(r.percentile(0.99)).key("max").value(r.max());
        w.end_object();
    };
    ring("interval_ms", t.intervals_ms());
    w.key("interval_histogram").begin_array();
    double lo = 0;
    for (size_t i = 0; i < t.buckets.size(); ++i) {
        w.begin_object().key("from_ms").value(lo);
        if (i + 1 < t.buckets.size()) w.key("to_ms").value(FrameTiming::kBucketUpperMs[i]); else w.key("to_ms").null();
        w.key("count").value(static_cast<unsigned long long>(t.buckets[i])).end_object();
        lo = FrameTiming::kBucketUpperMs[i];
    }
    w.end_array();
    ring("dequeue_delay_ms", t.dequeue_delay_ms());
    ring("age_at_processing_ms", t.age_ms());
    ring("processing_us", t.processing_us());

    w.key("queue").begin_object();
    w.key("capacity").value(static_cast<unsigned long long>(d.q.capacity()));
    w.key("pushed").value(static_cast<unsigned long long>(qc.pushed)).key("popped").value(static_cast<unsigned long long>(qc.popped));
    w.key("dropped_full").value(static_cast<unsigned long long>(qc.dropped_full));
    w.key("dropped_stale").value(static_cast<unsigned long long>(qc.dropped_stale));
    w.key("depth").value(static_cast<unsigned long long>(qc.depth));
    w.end_object();

    w.key("controls").begin_array();
    for (const auto& c : d.ctrls) {
        w.begin_object();
        w.key("name").value(c.name).key("type").value(ctrl_type(c.type));
        w.key("min").value(static_cast<long long>(c.min)).key("max").value(static_cast<long long>(c.max));
        w.key("default").value(static_cast<long long>(c.def));
        if (c.readable_before) w.key("before").value(static_cast<long long>(c.before)); else w.key("before").null();
        if (c.readable_after) w.key("value").value(static_cast<long long>(c.after)); else w.key("value").null();
        if (c.requested.empty()) w.key("requested").null(); else w.key("requested").value(c.requested);
        if (c.set_error.empty()) w.key("set_error").null(); else w.key("set_error").value(c.set_error);
        w.key("inactive").value((c.flags_after & V4L2_CTRL_FLAG_INACTIVE) != 0);
        w.end_object();
    }
    w.end_array();

    const TrackerConfig& tc = d.opt.tracker;
    w.key("tracker").begin_object();
    w.key("config").begin_object();
    w.key("color_space").value("YUYV-chroma (U,V), tint = atan2(V-128, U-128)");
    w.key("hue_deg").value(tc.hue_deg).key("hue_tol_deg").value(tc.hue_tol_deg);
    w.key("min_chroma").value(tc.min_chroma).key("min_luma").value(tc.min_luma).key("sat_luma").value(tc.sat_luma);
    w.key("min_area_px").value(tc.min_area_px).key("min_color_frac").value(tc.min_color_frac);
    w.key("max_elongation").value(tc.max_elongation).key("fill").begin_array().value(tc.min_fill).value(tc.max_fill).end_array();
    w.key("confirm_frames").value(tc.confirm_frames).key("lost_after_frames").value(tc.lost_after_frames);
    w.end_object();
    w.key("processed").value(static_cast<unsigned long long>(d.processed));
    w.key("valid").value(static_cast<unsigned long long>(d.valid));
    w.key("roi_hits").value(static_cast<unsigned long long>(d.roi_hits));
    w.key("full_frame_searches").value(static_cast<unsigned long long>(d.full_frame));
    w.key("reasons").begin_object();
    for (int i = 0; i < kReasonCount; ++i)
        if (d.reasons[static_cast<size_t>(i)]) w.key(to_string(static_cast<Reason>(i))).value(static_cast<unsigned long long>(d.reasons[static_cast<size_t>(i)]));
    w.end_object();
    w.key("radius_px").begin_object();
    w.key("n").value(static_cast<unsigned long long>(d.radius.size()));
    w.key("p5").value(d.radius.percentile(0.05)).key("p50").value(d.radius.percentile(0.5)).key("p95").value(d.radius.percentile(0.95));
    w.end_object();
    const TrackResult& l = d.last;
    w.key("last").begin_object();
    w.key("valid").value(l.valid).key("reason").value(to_string(l.reason)).key("state").value(to_string(l.state));
    w.key("candidates").value(l.candidates).key("components").value(l.components);
    if (l.valid) {
        w.key("x_px").value(l.x_px).key("y_px").value(l.y_px).key("radius_px").value(l.radius_px);
        w.key("area_px").value(l.area_px).key("color_px").value(l.color_px).key("sat_px").value(l.sat_px);
        w.key("elongation").value(l.elongation).key("fill").value(l.fill);
        w.key("mean_luma").value(l.mean_luma).key("mean_hue_deg").value(l.mean_hue_deg).key("mean_chroma").value(l.mean_chroma);
    }
    w.end_object();
    w.end_object();

    // Alle spelers: een tracker per bolkleur (speler 1 staat ook hierboven).
    {
        std::array<bool, kPlayers> on;
        std::array<TrackerConfig, kPlayers> pc;
        {
            std::lock_guard<std::mutex> tg(d_->tm);
            on = d.tracker_on;
            pc = d.pending_tracker;
        }
        w.key("players").begin_array();
        for (int i = 0; i < kPlayers; ++i) {
            const size_t k = static_cast<size_t>(i);
            const TrackerConfig& c = i == 0 ? tc : pc[k];
            const TrackResult& lr = d.p_last[k];
            w.begin_object().key("player").value(i + 1).key("enabled").value(on[k]);
            w.key("hue_deg").value(c.hue_deg).key("hue_tol_deg").value(c.hue_tol_deg).key("min_chroma").value(c.min_chroma);
            w.key("processed").value(static_cast<unsigned long long>(d.p_processed[k]));
            w.key("valid").value(static_cast<unsigned long long>(d.p_valid[k]));
            w.key("last").begin_object().key("valid").value(lr.valid).key("reason").value(to_string(lr.reason));
            if (lr.valid) w.key("x_px").value(lr.x_px).key("y_px").value(lr.y_px).key("radius_px").value(lr.radius_px).key("mean_hue_deg").value(lr.mean_hue_deg);
            w.end_object().end_object();
        }
        w.end_array();
    }

    w.key("scene").begin_object();
    w.key("available").value(d.have_scene);
    if (d.have_scene) {
        const SceneStats& s = d.scene;
        w.key("samples").value(s.samples);
        w.key("mean_yuv").begin_array().value(s.mean_y).value(s.mean_u).value(s.mean_v).end_array();
        w.key("mean_rgb").begin_array().value(s.mean_r).value(s.mean_g).value(s.mean_b).end_array();
        w.key("y_p50").value(s.p50_y).key("y_p99").value(s.p99_y);
        w.key("saturated_frac").value(s.sat_frac);
        w.key("color_match_frac").value(s.color_match_frac);
        w.key("chromatic_frac").value(s.chromatic_frac);
        w.key("hue_hist_30deg_from_-180").begin_array();
        for (double h : s.hue_hist_frac) w.value(h);
        w.end_array();
    }
    w.end_object();
    w.end_object();
    return w.str();
}

}  // namespace ravengund
