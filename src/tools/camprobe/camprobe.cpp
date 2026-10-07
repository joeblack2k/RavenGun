// camprobe: meet de werkelijke framesnelheid en timing van een V4L2-camera.
// Alleen getallen: frame-inhoud wordt nooit gelezen, bewaard of geschreven
// (buffers worden direct teruggegeven aan de driver).
//
// Gebruik: camprobe [--device /dev/video0] [--width 640] [--height 480]
//                   [--fps 60] [--format YUYV|GRBG] [--seconds 10] [--json]
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string device = "/dev/video0";
    uint32_t width = 640, height = 480, fps = 60;
    std::string format = "YUYV";
    double seconds = 10.0;
    bool json = false;
};

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

double now_monotonic() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = p * static_cast<double>(v.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(idx));
    const size_t hi = static_cast<size_t>(std::ceil(idx));
    return v[lo] + (v[hi] - v[lo]) * (idx - static_cast<double>(lo));
}

const char* ts_type(uint32_t flags) {
    switch (flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) {
        case V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN: return "unknown";
        case V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC: return "monotonic";
        case V4L2_BUF_FLAG_TIMESTAMP_COPY: return "copy";
    }
    return "?";
}

const char* ts_src(uint32_t flags) {
    switch (flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK) {
        case V4L2_BUF_FLAG_TSTAMP_SRC_EOF: return "EOF";
        case V4L2_BUF_FLAG_TSTAMP_SRC_SOE: return "SOE";
    }
    return "?";
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { fprintf(stderr, "camprobe: %s vraagt een waarde\n", name); return nullptr; }
            return argv[++i];
        };
        const char* v = nullptr;
        if (a == "--json") o.json = true;
        else if (a == "--device") { if (!(v = next("--device"))) return false; o.device = v; }
        else if (a == "--width") { if (!(v = next("--width"))) return false; o.width = static_cast<uint32_t>(atoi(v)); }
        else if (a == "--height") { if (!(v = next("--height"))) return false; o.height = static_cast<uint32_t>(atoi(v)); }
        else if (a == "--fps") { if (!(v = next("--fps"))) return false; o.fps = static_cast<uint32_t>(atoi(v)); }
        else if (a == "--format") { if (!(v = next("--format"))) return false; o.format = v; }
        else if (a == "--seconds") { if (!(v = next("--seconds"))) return false; o.seconds = atof(v); }
        else { fprintf(stderr, "camprobe: onbekende optie %s\n", a.c_str()); return false; }
    }
    return o.width && o.height && o.fps && o.seconds > 0 && (o.format == "YUYV" || o.format == "GRBG");
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        fprintf(stderr, "gebruik: camprobe [--device D] [--width W] [--height H] [--fps N] [--format YUYV|GRBG] [--seconds S] [--json]\n");
        return 2;
    }
    int fd = open(o.device.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) { perror("open"); return 1; }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = o.width;
    fmt.fmt.pix.height = o.height;
    fmt.fmt.pix.pixelformat = o.format == "YUYV" ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_SGRBG8;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { perror("VIDIOC_S_FMT"); return 1; }

    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = o.fps;
    if (xioctl(fd, VIDIOC_S_PARM, &parm) < 0) { perror("VIDIOC_S_PARM"); return 1; }
    const double nominal = parm.parm.capture.timeperframe.numerator
        ? static_cast<double>(parm.parm.capture.timeperframe.denominator) / parm.parm.capture.timeperframe.numerator : 0.0;

    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) { perror("VIDIOC_REQBUFS"); return 1; }
    std::vector<std::pair<void*, size_t>> maps;
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) { perror("VIDIOC_QUERYBUF"); return 1; }
        void* p = mmap(nullptr, b.length, PROT_READ, MAP_SHARED, fd, b.m.offset);
        if (p == MAP_FAILED) { perror("mmap"); return 1; }
        maps.emplace_back(p, b.length);
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) { perror("VIDIOC_QBUF"); return 1; }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) { perror("VIDIOC_STREAMON"); return 1; }

    std::vector<double> ts;            // frametimestamps (s)
    std::vector<double> lat_ms;        // dequeue-tijd minus timestamp (alleen zinvol bij monotonic)
    uint32_t last_seq = 0, missed = 0, errors = 0, short_frames = 0, flags_seen = 0;
    bool have_seq = false;
    const size_t expected = o.format == "YUYV" ? size_t(o.width) * o.height * 2 : size_t(o.width) * o.height;
    const double t_start = now_monotonic();
    bool first = true;
    while (now_monotonic() - t_start < o.seconds) {
        pollfd pf{fd, POLLIN, 0};
        int pr = poll(&pf, 1, 2000);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) { fprintf(stderr, "camprobe: time-out zonder frame\n"); break; }
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) { if (errno == EAGAIN) continue; perror("VIDIOC_DQBUF"); break; }
        const double tnow = now_monotonic();
        if (first) { first = false; flags_seen = b.flags; }  // eerste frame is voor warm-up; wel getelt als sequence-referentie
        const double t = static_cast<double>(b.timestamp.tv_sec) + static_cast<double>(b.timestamp.tv_usec) * 1e-6;
        if (have_seq && b.sequence > last_seq + 1) missed += b.sequence - last_seq - 1;
        last_seq = b.sequence; have_seq = true;
        if (b.flags & V4L2_BUF_FLAG_ERROR) ++errors;
        if (b.bytesused < expected) ++short_frames;
        ts.push_back(t);
        lat_ms.push_back((tnow - t) * 1e3);
        xioctl(fd, VIDIOC_QBUF, &b);  // inhoud nooit gelezen
    }
    const double elapsed = now_monotonic() - t_start;
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    for (auto& m : maps) munmap(m.first, m.second);
    close(fd);

    std::vector<double> dt_ms;
    for (size_t i = 1; i < ts.size(); ++i) dt_ms.push_back((ts[i] - ts[i - 1]) * 1e3);
    double mean = 0, mx = 0, mn = 0;
    if (!dt_ms.empty()) {
        for (double d : dt_ms) mean += d;
        mean /= static_cast<double>(dt_ms.size());
        mx = *std::max_element(dt_ms.begin(), dt_ms.end());
        mn = *std::min_element(dt_ms.begin(), dt_ms.end());
    }
    double var = 0;
    for (double d : dt_ms) var += (d - mean) * (d - mean);
    const double sd = dt_ms.size() > 1 ? std::sqrt(var / static_cast<double>(dt_ms.size() - 1)) : 0.0;
    const double span = ts.size() > 1 ? ts.back() - ts.front() : 0.0;
    const double fps_ts = span > 0 ? static_cast<double>(ts.size() - 1) / span : 0.0;
    const double p50 = percentile(dt_ms, 0.50), p95 = percentile(dt_ms, 0.95), p99 = percentile(dt_ms, 0.99);
    const double lat_mean = lat_ms.empty() ? 0 : [&] { double s = 0; for (double x : lat_ms) s += x; return s / static_cast<double>(lat_ms.size()); }();
    const double lat_p95 = percentile(lat_ms, 0.95);

    if (o.json) {
        printf("{\"device\":\"%s\",\"format\":\"%s\",\"width\":%u,\"height\":%u,\"requested_fps\":%u,\"driver_fps\":%.3f,"
               "\"seconds\":%.2f,\"frames\":%zu,\"fps_measured_timestamps\":%.3f,\"fps_measured_wall\":%.3f,"
               "\"interval_ms\":{\"mean\":%.3f,\"min\":%.3f,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f,\"stddev\":%.3f},"
               "\"missed_frames\":%u,\"error_frames\":%u,\"short_frames\":%u,"
               "\"timestamp_type\":\"%s\",\"timestamp_source\":\"%s\","
               "\"dequeue_minus_timestamp_ms\":{\"mean\":%.3f,\"p95\":%.3f}}\n",
               o.device.c_str(), o.format.c_str(), o.width, o.height, o.fps, nominal, elapsed, ts.size(), fps_ts,
               static_cast<double>(ts.size()) / elapsed, mean, mn, p50, p95, p99, mx, sd, missed, errors, short_frames,
               ts_type(flags_seen), ts_src(flags_seen), lat_mean, lat_p95);
    } else {
        printf("camprobe %s %s %ux%u gevraagd %u fps (driver: %.2f fps)\n", o.device.c_str(), o.format.c_str(), o.width, o.height, o.fps, nominal);
        printf("  duur %.2f s, %zu frames\n", elapsed, ts.size());
        printf("  fps (uit timestamps) %.3f, (wandklok) %.3f\n", fps_ts, static_cast<double>(ts.size()) / elapsed);
        printf("  interval ms: gem %.3f  min %.3f  p50 %.3f  p95 %.3f  p99 %.3f  max %.3f  sd %.3f\n", mean, mn, p50, p95, p99, mx, sd);
        printf("  gemiste frames (sequence-gaten) %u, error-flag %u, te korte frames %u\n", missed, errors, short_frames);
        printf("  timestamp-type %s, bron %s; dequeue min timestamp: gem %.3f ms, p95 %.3f ms\n", ts_type(flags_seen), ts_src(flags_seen), lat_mean, lat_p95);
    }
    return 0;
}
