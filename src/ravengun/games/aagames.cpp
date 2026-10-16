#include "ravengun/games/aagames.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"

namespace ravengun::games {

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

template <typename T>
void put(std::vector<uint8_t>& b, T v) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &v, sizeof(T));  // little-endian host (x86-64, aarch64)
    b.insert(b.end(), raw, raw + sizeof(T));
}

template <typename T>
T get(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

double num_or(const json::Value& o, const char* key, double def, bool& ok) {
    const json::Value* v = o.find(key);
    if (!v) return def;
    if (!v->is_number()) { ok = false; return def; }
    return v->num;
}

std::string str_or(const json::Value& o, const char* key, const std::string& def) {
    const json::Value* v = o.find(key);
    return v && v->is_string() ? v->str : def;
}

}  // namespace

const char* to_string(AaType t) { return t == AaType::TombRaider ? "tomb_raider" : "rabbids_hollywood"; }

std::string validate_aagames_game(const AaGame& g) {
    if (g.key.empty()) return "game zonder sleutel";
    if (g.width < 320 || g.width > 8192 || g.height < 240 || g.height > 8192) return g.key + ": ongeldige resolutie";
    if (!(g.scale_x > 0.1 && g.scale_x < 10) || !(g.scale_y > 0.1 && g.scale_y < 10)) return g.key + ": schaal moet tussen 0,1 en 10 liggen";
    if (!(std::fabs(g.offset_x) <= 1) || !(std::fabs(g.offset_y) <= 1)) return g.key + ": offset moet tussen -1 en 1 liggen";
    if (g.y_origin != "bottom" && g.y_origin != "top") return g.key + ": y_origin moet 'bottom' of 'top' zijn";
    if (g.send_hz < 1 || g.send_hz > 1000) return g.key + ": send_hz moet tussen 1 en 1000 liggen";
    return {};
}

bool parse_aagames_game(const std::string& key, const std::string& text, AaGame& out, std::string& err) {
    err.clear();
    const std::string file = "patches/" + key + "/unity.json";
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) { err = file + " is geen geldig JSON-object" + (pr.error.empty() ? std::string() : ": " + pr.error); return false; }
    const json::Value& o = pr.value;
    AaGame g;
    g.key = key;
    g.name = str_or(o, "name", key);
    const std::string type = str_or(o, "type", "");
    if (type == "tomb_raider") g.type = AaType::TombRaider;
    else if (type == "rabbids_hollywood") g.type = AaType::RabbidsHollywood;
    else { err = file + ": onbekend type '" + type + "'"; return false; }
    g.exe = str_or(o, "exe", "Game.exe");
    g.plugin_dir = str_or(o, "plugin_dir", "");
    bool nok = true;
    g.width = static_cast<int>(num_or(o, "width", g.width, nok));
    g.height = static_cast<int>(num_or(o, "height", g.height, nok));
    g.offset_x = num_or(o, "offset_x", 0, nok);
    g.offset_y = num_or(o, "offset_y", 0, nok);
    g.scale_x = num_or(o, "scale_x", 1, nok);
    g.scale_y = num_or(o, "scale_y", 1, nok);
    g.send_hz = static_cast<int>(num_or(o, "send_hz", g.send_hz, nok));
    g.y_origin = str_or(o, "y_origin", g.y_origin);
    if (!nok) { err = file + ": een getalveld bevat geen getal"; return false; }
    if (std::string bad = validate_aagames_game(g); !bad.empty()) { err = file + ": " + bad; return false; }
    out = std::move(g);
    return true;
}

AaPixel to_pixels(const AaGame& g, double u, double v) {
    auto fit = [](double n, double scale, double offset) {
        if (!std::isfinite(n)) n = 0.5;
        return std::clamp((n - 0.5) * scale + 0.5 + offset, 0.0, 1.0);
    };
    const double fu = fit(u, g.scale_x, g.offset_x);
    const double fv = fit(v, g.scale_y, g.offset_y);
    AaPixel p;
    p.x = static_cast<float>(fu * (g.width - 1));
    p.y = static_cast<float>((g.y_origin == "bottom" ? 1.0 - fv : fv) * (g.height - 1));
    return p;
}

size_t aa_input_size(AaType t) { return t == AaType::TombRaider ? 42 : 38; }

std::vector<uint8_t> build_input_packet(const AaGame& g, const AaInputs& in, bool hide_crosshairs) {
    std::vector<uint8_t> b;
    b.reserve(aa_input_size(g.type));
    AaPixel px[kAaPlayers];
    for (int i = 0; i < kAaPlayers; ++i) {
        const auto& p = in[static_cast<size_t>(i)];
        px[i] = to_pixels(g, p.active ? p.u : 0.5, p.active ? p.v : 0.5);
    }
    for (int i = 0; i < kAaPlayers; ++i) put<float>(b, px[i].x);    // Axis_X
    for (int i = 0; i < kAaPlayers; ++i) put<float>(b, px[i].y);    // Axis_Y
    put<uint8_t>(b, 1);                                              // EnableInputsHack
    put<uint8_t>(b, hide_crosshairs ? 1 : 0);                        // HideCrosshairs
    if (g.type == AaType::TombRaider)
        for (int i = 0; i < kAaPlayers; ++i) put<uint8_t>(b, in[static_cast<size_t>(i)].active && in[static_cast<size_t>(i)].reload ? 1 : 0);  // Reload
    for (int i = 0; i < kAaPlayers; ++i) put<uint8_t>(b, in[static_cast<size_t>(i)].active && in[static_cast<size_t>(i)].trigger ? 1 : 0);    // Trigger
    return b;
}

void AaFrameParser::reset() {
    buf_.clear();
    broken_ = false;
}

void AaFrameParser::feed(const uint8_t* data, size_t n) {
    if (broken_) return;
    buf_.insert(buf_.end(), data, data + n);
}

bool AaFrameParser::next(AaOutputs& out) {
    while (!broken_ && buf_.size() >= 4) {
        const int32_t len = get<int32_t>(buf_.data());
        if (len < 1 || len > 4096) { broken_ = true; return false; }
        const size_t total = 4 + static_cast<size_t>(len);
        if (buf_.size() < total) return false;
        const uint8_t header = buf_[4];
        const size_t payload = static_cast<size_t>(len) - 1;
        bool ok = false;
        if (header == 2 && payload >= kAaOutputPayload) {
            const uint8_t* p = buf_.data() + 5;
            for (int i = 0; i < kAaPlayers; ++i) out.ammo[i] = get<int32_t>(p + 4 * i);
            for (int i = 0; i < kAaPlayers; ++i) out.credits[i] = get<int32_t>(p + 16 + 4 * i);
            for (int i = 0; i < kAaPlayers; ++i) out.damaged[i] = p[32 + i];
            if (type_ == AaType::TombRaider) {
                for (int i = 0; i < kAaPlayers; ++i) out.playing[i] = p[36 + i];
                for (int i = 0; i < kAaPlayers; ++i) out.life[i] = get<float>(p + 40 + 4 * i);
                for (int i = 0; i < kAaPlayers; ++i) out.recoil[i] = p[56 + i];
            } else {
                for (int i = 0; i < kAaPlayers; ++i) out.life[i] = get<float>(p + 36 + 4 * i);
                for (int i = 0; i < kAaPlayers; ++i) out.recoil[i] = p[52 + i];
                for (int i = 0; i < kAaPlayers; ++i) out.playing[i] = p[56 + i];
            }
            ok = true;
        }
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(total));
        if (ok) { ++frames_; return true; }
        ++skipped_;
    }
    return false;
}

// ---- Klant ----

void AaClient::start(const AaGame& g, int port, StateFn state, RecoilFn recoil, const std::string& host, int reconnect_ms) {
    stop();
    game_ = g;
    port_ = port;
    host_ = host;
    reconnect_ms_ = std::max(50, reconnect_ms);
    state_ = std::move(state);
    recoil_ = std::move(recoil);
    {
        std::lock_guard<std::mutex> lk(m_);
        st_ = AaClientStats{};
        st_.started_ms = now_ms();
        sent_ms_.clear();
    }
    stop_.store(false);
    running_.store(true);
    th_ = std::thread(&AaClient::run, this);
}

void AaClient::stop() {
    stop_.store(true);
    if (th_.joinable()) th_.join();
    running_.store(false);
    close_socket();
    std::lock_guard<std::mutex> lk(m_);
    st_.connected = false;
}

int64_t AaClient::clock_ms() { return now_ms(); }

AaClientStats AaClient::stats() const {
    std::lock_guard<std::mutex> lk(m_);
    AaClientStats s = st_;
    const int64_t now = now_ms();
    uint64_t n = 0;
    for (auto it = sent_ms_.rbegin(); it != sent_ms_.rend() && now - *it <= 1000; ++it) ++n;
    s.packets_per_s = static_cast<double>(n);
    return s;
}

void AaClient::close_socket() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool AaClient::connect_once() {
    {
        std::lock_guard<std::mutex> lk(m_);
        ++st_.connect_attempts;
    }
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &a.sin_addr) != 1) { ::close(fd); return false; }
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    if (rc != 0 && errno != EINPROGRESS) {
        const int e = errno;
        ::close(fd);
        std::lock_guard<std::mutex> lk(m_);
        st_.last_error = std::string("verbinden: ") + std::strerror(e);
        return false;
    }
    if (rc != 0) {
        pollfd p{fd, POLLOUT, 0};
        if (::poll(&p, 1, 300) <= 0) { ::close(fd); std::lock_guard<std::mutex> lk(m_); st_.last_error = "verbinden: time-out"; return false; }
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) {
            ::close(fd);
            std::lock_guard<std::mutex> lk(m_);
            st_.last_error = std::string("verbinden: ") + std::strerror(soerr);
            return false;
        }
    }
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fd_ = fd;
    std::lock_guard<std::mutex> lk(m_);
    st_.connected = true;
    ++st_.connects;
    st_.connected_since_ms = now_ms();
    st_.last_error.clear();
    return true;
}

void AaClient::run() {
    const auto period = std::chrono::microseconds(1'000'000 / std::max(1, game_.send_hz));
    while (!stop_.load()) {
        if (!connect_once()) {
            for (int waited = 0; waited < reconnect_ms_ && !stop_.load(); waited += 20) ::usleep(20'000);
            continue;
        }
        AaFrameParser parser(game_.type);
        bool bad = false;
        auto next = std::chrono::steady_clock::now();
        uint8_t rbuf[2048];
        while (!stop_.load() && !bad) {
            // Pakket sturen.
            AaInputs in{};
            if (state_) state_(in);
            const std::vector<uint8_t> pkt = build_input_packet(game_, in);
            const ssize_t w = ::send(fd_, pkt.data(), pkt.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
            if (w == static_cast<ssize_t>(pkt.size())) {
                std::lock_guard<std::mutex> lk(m_);
                ++st_.packets_sent;
                st_.bytes_sent += pkt.size();
                const int64_t t = now_ms();
                sent_ms_.push_back(t);
                while (!sent_ms_.empty() && t - sent_ms_.front() > 2000) sent_ms_.erase(sent_ms_.begin());
            } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::lock_guard<std::mutex> lk(m_);
                ++st_.send_dropped;  // de plugin leest niet snel genoeg: dit pakket vervalt
            } else {
                std::lock_guard<std::mutex> lk(m_);
                st_.last_error = w < 0 ? std::string("sturen: ") + std::strerror(errno) : "sturen: onvolledig pakket";
                bad = true;
                break;
            }
            // Wachten tot het volgende pakket, ondertussen uitvoer lezen.
            next += period;
            const auto now = std::chrono::steady_clock::now();
            if (next < now) next = now;  // achterop: geen inhaalsalvo
            while (!bad && !stop_.load()) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(next - std::chrono::steady_clock::now()).count();
                if (left <= 0) break;
                pollfd p{fd_, POLLIN, 0};
                const int pr = ::poll(&p, 1, static_cast<int>(std::min<int64_t>(left, 20)));
                if (pr < 0 && errno != EINTR) { bad = true; break; }
                if (pr > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR))) {
                    const ssize_t r = ::recv(fd_, rbuf, sizeof rbuf, MSG_DONTWAIT);
                    if (r == 0) {
                        std::lock_guard<std::mutex> lk(m_);
                        st_.last_error = "de game sloot de verbinding";
                        bad = true;
                    } else if (r < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                            std::lock_guard<std::mutex> lk(m_);
                            st_.last_error = std::string("lezen: ") + std::strerror(errno);
                            bad = true;
                        }
                    } else {
                        parser.feed(rbuf, static_cast<size_t>(r));
                        AaOutputs o;
                        while (parser.next(o)) {
                            std::array<bool, kAaPlayers> shot{};
                            {
                                std::lock_guard<std::mutex> lk(m_);
                                st_.last = o;
                                st_.have_outputs = true;
                                st_.output_frames = parser.frames();
                                for (int i = 0; i < kAaPlayers; ++i)
                                    if (o.recoil[i]) { ++st_.recoils[i]; shot[static_cast<size_t>(i)] = true; }
                            }
                            if (recoil_)
                                for (int i = 0; i < kAaPlayers; ++i)
                                    if (shot[static_cast<size_t>(i)]) recoil_(i + 1);
                        }
                        {
                            std::lock_guard<std::mutex> lk(m_);
                            st_.output_skipped = parser.skipped();
                        }
                        if (parser.broken()) {
                            std::lock_guard<std::mutex> lk(m_);
                            st_.last_error = "ongeldige lengte in de uitvoer van de plugin";
                            bad = true;
                        }
                    }
                }
            }
        }
        close_socket();
        {
            std::lock_guard<std::mutex> lk(m_);
            if (st_.connected) { ++st_.disconnects; st_.last_disconnect_ms = now_ms(); }
            st_.connected = false;
        }
        for (int waited = 0; waited < reconnect_ms_ && !stop_.load(); waited += 20) ::usleep(20'000);
    }
}

std::string AaClient::status_json() const {
    const AaClientStats s = stats();
    json::Writer w;
    w.begin_object();
    w.key("running").value(running_.load());
    w.key("game").value(game_.key).key("name").value(game_.name).key("type").value(to_string(game_.type));
    w.key("connected").value(s.connected);
    w.key("resolution").begin_object().key("width").value(game_.width).key("height").value(game_.height).end_object();
    w.key("send_hz").value(game_.send_hz);
    w.key("packets_per_s").value(s.packets_per_s);
    w.key("packets_sent").value(static_cast<unsigned long long>(s.packets_sent));
    w.key("send_dropped").value(static_cast<unsigned long long>(s.send_dropped));
    w.key("connect_attempts").value(static_cast<unsigned long long>(s.connect_attempts));
    w.key("connects").value(static_cast<unsigned long long>(s.connects));
    w.key("disconnects").value(static_cast<unsigned long long>(s.disconnects));
    w.key("output_frames").value(static_cast<unsigned long long>(s.output_frames));
    w.key("output_skipped").value(static_cast<unsigned long long>(s.output_skipped));
    if (s.last_error.empty()) w.key("last_error").null(); else w.key("last_error").value(s.last_error);
    w.key("players").begin_array();
    for (int i = 0; i < kAaPlayers; ++i) {
        w.begin_object().key("player").value(i + 1);
        if (s.have_outputs) {
            w.key("playing").value(s.last.playing[i] != 0).key("ammo").value(s.last.ammo[i]).key("life").value(static_cast<double>(s.last.life[i]));
            w.key("credits").value(s.last.credits[i]);
        } else {
            w.key("playing").null().key("ammo").null().key("life").null().key("credits").null();
        }
        w.key("recoils").value(static_cast<unsigned long long>(s.recoils[i]));
        w.end_object();
    }
    w.end_array();
    w.end_object();
    return w.str();
}

}  // namespace ravengun::games
