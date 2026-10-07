#include "ravengun/config/settings.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

#include "ravengun/config/led_color.hpp"
#include "ravengun/json.hpp"

namespace ravengun::config {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxFileBytes = 64 * 1024;

std::string fmt(const char* f, const char* a, double lo, double hi) {
    char buf[200];
    std::snprintf(buf, sizeof buf, f, a, lo, hi);
    return buf;
}

// Leest typevaste velden uit een object; houdt de eerste fout vast.
struct Section {
    const json::Value& o;
    std::string name;
    std::string& err;
    std::vector<std::string>* warnings;

    std::string full(const char* key) const { return name + "." + key; }
    bool fail(const std::string& m) { if (err.empty()) err = m; return false; }

    bool number(const char* key, double lo, double hi, double& out, bool integer) {
        const json::Value* v = o.find(key);
        if (!v) return true;
        if (!v->is_number() || !std::isfinite(v->num) || (integer && std::floor(v->num) != v->num))
            return fail(full(key) + " moet " + (integer ? "een geheel getal" : "een getal") + " zijn");
        if (!(v->num >= lo && v->num <= hi)) return fail(fmt("%s moet tussen %g en %g liggen", full(key).c_str(), lo, hi));
        out = v->num;
        return true;
    }
    bool integer(const char* key, int lo, int hi, int& out) {
        double d = out;
        if (!number(key, lo, hi, d, true)) return false;
        out = static_cast<int>(d);
        return true;
    }
    bool boolean(const char* key, bool& out) {
        const json::Value* v = o.find(key);
        if (!v) return true;
        if (!v->is_bool()) return fail(full(key) + " moet true of false zijn");
        out = v->b;
        return true;
    }
    bool string(const char* key, std::string& out) {
        const json::Value* v = o.find(key);
        if (!v) return true;
        if (!v->is_string() || v->str.size() > 32) return fail(full(key) + " moet een korte tekst zijn");
        out = v->str;
        return true;
    }
    void unknown(std::initializer_list<const char*> known) {
        if (!warnings) return;
        for (const auto& kv : o.obj) {
            bool k = false;
            for (const char* n : known) if (kv.first == n) k = true;
            if (!k) warnings->push_back("onbekende sleutel '" + name + "." + kv.first + "' genegeerd");
        }
    }
};

bool read_file(const std::string& path, std::string& out, std::string& err, bool& missing) {
    missing = false;
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (errno == ENOENT) { missing = true; err = "bestaat niet"; return false; }
        err = std::strerror(errno);
        return false;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { ::close(fd); err = "geen gewoon bestand"; return false; }
    if (st.st_uid != ::getuid()) { ::close(fd); err = "niet van de eigen gebruiker"; return false; }
    if (st.st_size > static_cast<off_t>(kMaxFileBytes)) { ::close(fd); err = "groter dan 64 KiB"; return false; }
    out.clear();
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; err = std::strerror(errno); ::close(fd); return false; }
        if (n == 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > kMaxFileBytes) { ::close(fd); err = "groter dan 64 KiB"; return false; }
    }
    ::close(fd);
    return true;
}

}  // namespace

const std::vector<CameraMode>& camera_modes() {
    static const std::vector<CameraMode> m = {{640, 480, 60}, {320, 240, 125}, {320, 240, 187}};
    return m;
}
bool camera_mode_valid(int w, int h, int fps) {
    for (const auto& m : camera_modes()) if (m.width == w && m.height == h && m.fps == fps) return true;
    return false;
}
std::string camera_mode_string(int w, int h, int fps) {
    return std::to_string(w) + "x" + std::to_string(h) + "@" + std::to_string(fps);
}
bool parse_camera_mode(const std::string& s, CameraMode& out) {
    int w = 0, h = 0, f = 0;
    char tail = 0;
    if (std::sscanf(s.c_str(), "%dx%d@%d%c", &w, &h, &f, &tail) != 3) return false;
    if (!camera_mode_valid(w, h, f)) return false;
    out = {w, h, f};
    return true;
}

std::string validate(const Settings& s) {
    auto in = [](double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    static const char* kPresets[] = {"cyaan", "magenta", "rood", "groen", "blauw", "wit", "custom"};
    bool pr = false;
    for (const char* p : kPresets) if (s.led.preset == p) pr = true;
    if (!pr) return "led.preset is onbekend (cyaan, magenta, rood, groen, blauw, wit of custom)";
    for (int c : {s.led.r, s.led.g, s.led.b}) if (c < 0 || c > 255) return "led.r/g/b moeten tussen 0 en 255 liggen";
    if (!in(s.tracker.hue_deg, -180, 180)) return "tracker.hue_deg moet tussen -180 en 180 liggen";
    if (!in(s.tracker.hue_tol_deg, 1, 90)) return "tracker.hue_tol_deg moet tussen 1 en 90 liggen";
    if (s.tracker.min_chroma < 1 || s.tracker.min_chroma > 180) return "tracker.min_chroma moet tussen 1 en 180 liggen";
    if (!camera_mode_valid(s.camera.width, s.camera.height, s.camera.fps)) return "camera.mode is geen toegestane modus (640x480@60, 320x240@125, 320x240@187)";
    if (s.camera.exposure < 0 || s.camera.exposure > 255) return "camera.exposure moet tussen 0 en 255 liggen";
    if (s.camera.gain < 0 || s.camera.gain > 63) return "camera.gain moet tussen 0 en 63 liggen";
    if (!in(s.filter.min_cutoff_hz, 0.001, 100)) return "filter.min_cutoff_hz moet tussen 0,001 en 100 liggen";
    if (!in(s.filter.beta, 0, 1000)) return "filter.beta moet tussen 0 en 1000 liggen";
    if (!in(s.filter.d_cutoff_hz, 0.001, 100)) return "filter.d_cutoff_hz moet tussen 0,001 en 100 liggen";
    if (!in(s.aim.h_edge_deg, 1, 89) || !in(s.aim.v_edge_deg, 1, 89)) return "aim.h_edge_deg en aim.v_edge_deg moeten tussen 1 en 89 liggen";
    if (s.web.port < 1 || s.web.port > 65535) return "web.port moet tussen 1 en 65535 liggen";
    return {};
}

std::string to_json(const Settings& s, bool pretty) {
    const char* nl = pretty ? "\n" : "";
    const char* ind = pretty ? "  " : "";
    auto section = [&](const char* name, auto&& fill) {
        json::Writer w;
        w.begin_object();
        fill(w);
        w.end_object();
        return std::string(ind) + "\"" + name + "\":" + (pretty ? " " : "") + w.str();
    };
    std::string out = std::string("{") + nl + ind + "\"schema\":" + (pretty ? " " : "") + std::to_string(kSchemaVersion) + "," + nl;
    out += section("led", [&](json::Writer& w) {
        w.key("enabled").value(s.led.enabled).key("preset").value(s.led.preset);
        w.key("r").value(s.led.r).key("g").value(s.led.g).key("b").value(s.led.b);
    }) + "," + nl;
    out += section("tracker", [&](json::Writer& w) {
        w.key("hue_deg").value(s.tracker.hue_deg).key("hue_tol_deg").value(s.tracker.hue_tol_deg).key("min_chroma").value(s.tracker.min_chroma);
    }) + "," + nl;
    out += section("camera", [&](json::Writer& w) {
        w.key("mode").value(camera_mode_string(s.camera.width, s.camera.height, s.camera.fps));
        w.key("auto_exposure").value(s.camera.auto_exposure).key("auto_gain").value(s.camera.auto_gain).key("auto_awb").value(s.camera.auto_awb);
        w.key("exposure").value(s.camera.exposure).key("gain").value(s.camera.gain);
    }) + "," + nl;
    out += section("filter", [&](json::Writer& w) {
        w.key("enabled").value(s.filter.enabled).key("min_cutoff_hz").value(s.filter.min_cutoff_hz);
        w.key("beta").value(s.filter.beta).key("d_cutoff_hz").value(s.filter.d_cutoff_hz);
    }) + "," + nl;
    out += section("aim", [&](json::Writer& w) {
        w.key("h_edge_deg").value(s.aim.h_edge_deg).key("v_edge_deg").value(s.aim.v_edge_deg);
    }) + "," + nl;
    out += section("web", [&](json::Writer& w) { w.key("port").value(s.web.port); }) + nl + "}" + nl;
    return out;
}

const std::vector<MigrationStep>& builtin_migrations() {
    static const std::vector<MigrationStep> none;
    return none;
}

bool parse_settings(const std::string& text, Settings& out, std::string& err, std::vector<std::string>* warnings,
                    const std::vector<MigrationStep>* migrations) {
    err.clear();
    auto pr = json::parse(text);
    if (!pr.ok) { err = "ongeldige JSON: " + pr.error + " (positie " + std::to_string(pr.offset) + ")"; return false; }
    json::Value root = std::move(pr.value);
    if (!root.is_object()) { err = "het bestand moet een JSON-object zijn"; return false; }
    const json::Value* sv = root.find("schema");
    if (!sv || !sv->is_number() || std::floor(sv->num) != sv->num) { err = "schema (geheel getal) ontbreekt"; return false; }
    int ver = static_cast<int>(sv->num);
    if (ver > kSchemaVersion) {
        err = "schema " + std::to_string(ver) + " is nieuwer dan deze daemon kent (" + std::to_string(kSchemaVersion) + ")";
        return false;
    }
    if (ver < 0) { err = "schema " + std::to_string(ver) + " is ongeldig"; return false; }
    const auto& steps = migrations ? *migrations : builtin_migrations();
    while (ver < kSchemaVersion) {
        const MigrationStep* st = nullptr;
        for (const auto& m : steps) if (m.from == ver) st = &m;
        if (!st) { err = "geen migratie van schema " + std::to_string(ver); return false; }
        std::string merr;
        if (!st->apply(root, merr)) { err = "migratie van schema " + std::to_string(ver) + " mislukt: " + merr; return false; }
        ++ver;
    }
    if (warnings) {
        for (const auto& kv : root.obj) {
            static const char* known[] = {"schema", "led", "tracker", "camera", "filter", "aim", "web"};
            bool k = false;
            for (const char* n : known) if (kv.first == n) k = true;
            if (!k) warnings->push_back("onbekende sleutel '" + kv.first + "' genegeerd");
        }
    }

    Settings s;
    std::string e;
    auto sec = [&](const char* name) -> const json::Value* {
        const json::Value* v = root.find(name);
        if (v && !v->is_object()) { if (e.empty()) e = std::string(name) + " moet een object zijn"; return nullptr; }
        return v;
    };
    if (const json::Value* o = sec("led")) {
        Section c{*o, "led", e, warnings};
        c.boolean("enabled", s.led.enabled); c.string("preset", s.led.preset);
        c.integer("r", 0, 255, s.led.r); c.integer("g", 0, 255, s.led.g); c.integer("b", 0, 255, s.led.b);
        c.unknown({"enabled", "preset", "r", "g", "b"});
    }
    if (const json::Value* o = sec("tracker")) {
        Section c{*o, "tracker", e, warnings};
        c.number("hue_deg", -180, 180, s.tracker.hue_deg, false);
        c.number("hue_tol_deg", 1, 90, s.tracker.hue_tol_deg, false);
        c.integer("min_chroma", 1, 180, s.tracker.min_chroma);
        c.unknown({"hue_deg", "hue_tol_deg", "min_chroma"});
    }
    if (const json::Value* o = sec("camera")) {
        Section c{*o, "camera", e, warnings};
        std::string mode;
        if (c.string("mode", mode) && !mode.empty()) {
            CameraMode m{};
            if (!parse_camera_mode(mode, m)) c.fail("camera.mode '" + mode + "' is geen toegestane modus (640x480@60, 320x240@125, 320x240@187)");
            else { s.camera.width = m.width; s.camera.height = m.height; s.camera.fps = m.fps; }
        }
        c.boolean("auto_exposure", s.camera.auto_exposure); c.boolean("auto_gain", s.camera.auto_gain); c.boolean("auto_awb", s.camera.auto_awb);
        c.integer("exposure", 0, 255, s.camera.exposure); c.integer("gain", 0, 63, s.camera.gain);
        c.unknown({"mode", "auto_exposure", "auto_gain", "auto_awb", "exposure", "gain"});
    }
    if (const json::Value* o = sec("filter")) {
        Section c{*o, "filter", e, warnings};
        c.boolean("enabled", s.filter.enabled);
        c.number("min_cutoff_hz", 0.001, 100, s.filter.min_cutoff_hz, false);
        c.number("beta", 0, 1000, s.filter.beta, false);
        c.number("d_cutoff_hz", 0.001, 100, s.filter.d_cutoff_hz, false);
        c.unknown({"enabled", "min_cutoff_hz", "beta", "d_cutoff_hz"});
    }
    if (const json::Value* o = sec("aim")) {
        Section c{*o, "aim", e, warnings};
        c.number("h_edge_deg", 1, 89, s.aim.h_edge_deg, false);
        c.number("v_edge_deg", 1, 89, s.aim.v_edge_deg, false);
        c.unknown({"h_edge_deg", "v_edge_deg"});
    }
    if (const json::Value* o = sec("web")) {
        Section c{*o, "web", e, warnings};
        c.integer("port", 1, 65535, s.web.port);
        c.unknown({"port"});
    }
    if (e.empty()) e = validate(s);  // o.a. de presetnaam
    if (!e.empty()) { err = e; return false; }
    out = s;
    return true;
}

bool write_file_atomic(const std::string& path, const std::string& content, std::string& err) {
    if (path.empty()) { err = "geen configuratiepad bekend (HOME/XDG_CONFIG_HOME)"; return false; }
    std::error_code ec;
    fs::path dir = fs::path(path).parent_path();
    if (!dir.empty()) {
        fs::create_directories(dir, ec);
        if (ec) { err = "kan " + dir.string() + " niet maken: " + ec.message(); return false; }
        ::chmod(dir.c_str(), 0700);
    }
    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0 && errno == EEXIST) {  // restant van een eerdere crash met hetzelfde pid
        ::unlink(tmp.c_str());
        fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    }
    if (fd < 0) { err = "kan " + tmp + " niet maken: " + std::strerror(errno); return false; }
    size_t off = 0;
    while (off < content.size()) {
        ssize_t n = ::write(fd, content.data() + off, content.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            err = std::string("schrijven mislukt: ") + std::strerror(errno);
            ::close(fd); ::unlink(tmp.c_str());
            return false;
        }
        off += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) { err = std::string("fsync mislukt: ") + std::strerror(errno); ::close(fd); ::unlink(tmp.c_str()); return false; }
    ::close(fd);
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "hernoemen naar " + path + " mislukt: " + std::strerror(errno);
        ::unlink(tmp.c_str());
        return false;
    }
    if (!dir.empty()) {
        int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dfd >= 0) { (void)::fsync(dfd); ::close(dfd); }
    }
    return true;
}

const char* to_string(LoadInfo::State s) {
    switch (s) {
        case LoadInfo::State::NotLoaded: return "not_loaded";
        case LoadInfo::State::Missing: return "missing";
        case LoadInfo::State::Loaded: return "loaded";
        case LoadInfo::State::Recovered: return "recovered";
        case LoadInfo::State::Invalid: return "invalid";
    }
    return "?";
}

std::string SettingsStore::default_path() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const char* h = std::getenv("HOME");
    std::string base = x && *x ? x : (h && *h ? std::string(h) + "/.config" : std::string());
    if (base.empty()) return {};
    return base + "/ravengun/config.json";
}

SettingsStore::SettingsStore(std::string path) : path_(std::move(path)) {}

LoadInfo SettingsStore::load() {
    LoadInfo li;
    Settings s;
    std::string text, rerr, perr;
    bool missing = false;
    if (path_.empty()) {
        li.state = LoadInfo::State::Missing;
        li.message = "geen configuratiepad bekend (HOME/XDG_CONFIG_HOME); instellingen worden niet bewaard";
    } else if (!read_file(path_, text, rerr, missing)) {
        if (missing) {
            li.state = LoadInfo::State::Missing;
        } else {
            rerr = path_ + ": " + rerr;
            std::string btext, berr, bperr;
            bool bmissing = false;
            Settings bs;
            if (read_file(path_ + ".bak", btext, berr, bmissing) && parse_settings(btext, bs, bperr, &li.warnings)) {
                s = bs;
                li.state = LoadInfo::State::Recovered;
                li.message = rerr + "; laatst-goede kopie (config.json.bak) gebruikt";
            } else {
                li.state = LoadInfo::State::Invalid;
                li.message = rerr + "; standaardwaarden gebruikt";
            }
        }
    } else if (parse_settings(text, s, perr, &li.warnings)) {
        li.state = LoadInfo::State::Loaded;
    } else {
        s = Settings{};
        li.warnings.clear();
        std::string btext, berr, bperr;
        bool bmissing = false;
        Settings bs;
        std::string base = "config.json is ongeldig (" + perr + ")";
        if (read_file(path_ + ".bak", btext, berr, bmissing) && parse_settings(btext, bs, bperr, &li.warnings)) {
            s = bs;
            li.state = LoadInfo::State::Recovered;
            li.message = base + "; laatst-goede kopie (config.json.bak) gebruikt";
        } else {
            li.state = LoadInfo::State::Invalid;
            li.message = base + "; standaardwaarden gebruikt. Het bestand blijft staan tot de eerstvolgende wijziging en wordt dan hernoemd naar config.json.corrupt.";
        }
    }
    std::lock_guard<std::mutex> g(m_);
    cur_ = s;
    info_ = li;
    return li;
}

Settings SettingsStore::get() const {
    std::lock_guard<std::mutex> g(m_);
    return cur_;
}

LoadInfo SettingsStore::info() const {
    std::lock_guard<std::mutex> g(m_);
    return info_;
}

std::string SettingsStore::last_save_error() const {
    std::lock_guard<std::mutex> g(m_);
    return last_save_error_;
}

bool SettingsStore::file_present() const {
    std::error_code ec;
    return !path_.empty() && fs::exists(path_, ec);
}

SaveResult SettingsStore::update(const std::function<void(Settings&)>& fn) {
    std::lock_guard<std::mutex> g(m_);
    Settings n = cur_;
    fn(n);
    std::string v = validate(n);
    if (!v.empty()) return {false, "ongeldige instelling: " + v};
    cur_ = n;
    if (path_.empty()) {
        last_save_error_ = "niet bewaard: geen instellingenbestand (--no-config, of HOME en XDG_CONFIG_HOME ontbreken)";
        return {false, last_save_error_};
    }
    // Voor het overschrijven: een geldig bestaand bestand wordt de laatst-goede kopie; een ongeldig
    // bestand wordt opzij gezet (nooit stil weggegooid).
    std::string old, rerr, perr;
    bool missing = false;
    if (read_file(path_, old, rerr, missing)) {
        Settings tmp;
        if (parse_settings(old, tmp, perr)) {
            std::string werr;
            if (!write_file_atomic(path_ + ".bak", old, werr)) { /* kopie is best effort */ }
        } else {
            ::rename(path_.c_str(), (path_ + ".corrupt").c_str());
        }
    }
    std::string werr;
    if (!write_file_atomic(path_, to_json(n), werr)) {
        last_save_error_ = werr;
        return {false, "niet bewaard: " + werr};
    }
    last_save_error_.clear();
    if (info_.state == LoadInfo::State::Missing || info_.state == LoadInfo::State::Invalid || info_.state == LoadInfo::State::Recovered)
        info_.state = LoadInfo::State::Loaded, info_.message.clear();
    return {true, {}};
}

}  // namespace ravengun::config
