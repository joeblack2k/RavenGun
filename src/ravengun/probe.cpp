#include "ravengun/probe.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ravengun/parse.hpp"
#include "ravengun/paths.hpp"

namespace fs = std::filesystem;

namespace ravengun {
namespace {

std::optional<std::string> read_file(const fs::path& p) {
    std::ifstream f(p);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool can_rw(const fs::path& p) { return ::access(p.c_str(), R_OK | W_OK) == 0; }

std::vector<fs::path> list_dir_sorted(const fs::path& dir) {
    std::vector<fs::path> v;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) v.push_back(it->path());
    std::sort(v.begin(), v.end());
    return v;
}

bool has_prefix(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Loop van een sysfs-apparaat omhoog tot een map met idVendor/idProduct.
std::optional<UsbId> find_usb_id(const fs::path& start) {
    std::error_code ec;
    fs::path p = fs::canonical(start, ec);
    if (ec) return std::nullopt;
    for (int i = 0; i < 8 && p.has_parent_path() && p != p.root_path(); ++i, p = p.parent_path()) {
        auto v = read_file(p / "idVendor");
        auto d = read_file(p / "idProduct");
        if (v && d) {
            auto vid = parse_hex16(*v), pid = parse_hex16(*d);
            if (vid && pid) return UsbId{*vid, *pid};
        }
    }
    return std::nullopt;
}

}  // namespace

RunResult run_capture(const std::vector<std::string>& argv, int timeout_ms) {
    RunResult r;
    if (argv.empty()) return r;
    int pipefd[2];
    if (::pipe2(pipefd, O_CLOEXEC) != 0) return r;
    pid_t pid = ::fork();
    if (pid < 0) { ::close(pipefd[0]); ::close(pipefd[1]); return r; }
    if (pid == 0) {
        ::dup2(pipefd[1], STDOUT_FILENO);
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) { ::dup2(devnull, STDIN_FILENO); ::dup2(devnull, STDERR_FILENO); }
        std::vector<char*> args;
        for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        ::execvp(args[0], args.data());
        ::_exit(127);
    }
    ::close(pipefd[1]);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool timed_out = false;
    char buf[4096];
    for (;;) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) { timed_out = true; break; }
        pollfd pf{pipefd[0], POLLIN, 0};
        int pr = ::poll(&pf, 1, static_cast<int>(left));
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) { timed_out = true; break; }
        ssize_t n = ::read(pipefd[0], buf, sizeof buf);
        if (n > 0) r.out.append(buf, static_cast<size_t>(n));
        else break;  // EOF
    }
    ::close(pipefd[0]);
    if (timed_out) ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
    r.ok = !timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return r;
}

std::vector<std::string> default_psmoveapi_prefixes() {
    std::vector<std::string> v;
    if (const char* e = std::getenv("RAVENGUN_PSMOVEAPI_PREFIX"); e && *e) v.push_back(e);
    // Installatie (scripts/install-user.sh): libpsmoveapi.so staat in <prefix>/lib naast <prefix>/bin/ravengunctl.
    if (const std::string pre = paths::prefix_of_exe(paths::env_from_process().exe); !pre.empty()) v.push_back(pre);
    std::string data;
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) data = x;
    else if (const char* h = std::getenv("HOME"); h && *h) data = std::string(h) + "/.local/share";
    if (!data.empty()) v.push_back(data + "/ravengun/psmoveapi");
    v.push_back("/usr/local");
    v.push_back("/usr");
    return v;
}

std::vector<VideoDevice> probe_video(const ProbeOptions& opt) {
    std::vector<VideoDevice> out;
    for (const auto& p : list_dir_sorted(fs::path(opt.sys_root) / "class/video4linux")) {
        std::string base = p.filename().string();
        if (!has_prefix(base, "video")) continue;
        VideoDevice d;
        d.node = (fs::path(opt.dev_root) / base).string();
        if (auto n = read_file(p / "name")) d.name = trim(*n);
        std::error_code ec;
        auto drv = fs::read_symlink(p / "device/driver", ec);
        if (!ec) d.driver = drv.filename().string();
        d.usb = find_usb_id(p / "device");
        d.accessible = can_rw(d.node);
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<HidDevice> probe_hid(const ProbeOptions& opt) {
    std::vector<HidDevice> out;
    for (const auto& p : list_dir_sorted(fs::path(opt.sys_root) / "class/hidraw")) {
        std::string base = p.filename().string();
        if (!has_prefix(base, "hidraw")) continue;
        auto txt = read_file(p / "device/uevent");
        if (!txt) continue;
        auto kv = parse_keyvalue(*txt);
        auto it = kv.find("HID_ID");
        if (it == kv.end()) continue;
        auto hid = parse_hid_id(it->second);
        if (!hid) continue;
        HidDevice d;
        d.node = (fs::path(opt.dev_root) / base).string();
        d.bus = hid->bus;
        d.id = hid->id;
        if (auto n = kv.find("HID_NAME"); n != kv.end()) d.name = n->second;
        d.accessible = can_rw(d.node);
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<UsbDevice> probe_usb(const ProbeOptions& opt) {
    std::vector<UsbDevice> out;
    for (const auto& p : list_dir_sorted(fs::path(opt.sys_root) / "bus/usb/devices")) {
        auto v = read_file(p / "idVendor");
        auto d = read_file(p / "idProduct");
        if (!v || !d) continue;  // interfaces hebben deze bestanden niet
        auto vid = parse_hex16(*v), pid = parse_hex16(*d);
        if (!vid || !pid) continue;
        UsbDevice u;
        u.sysname = p.filename().string();
        u.id = {*vid, *pid};
        if (auto pr = read_file(p / "product")) u.product = trim(*pr);
        out.push_back(std::move(u));
    }
    return out;
}

std::vector<RfkillDevice> probe_rfkill(const ProbeOptions& opt) {
    std::vector<RfkillDevice> out;
    for (const auto& p : list_dir_sorted(fs::path(opt.sys_root) / "class/rfkill")) {
        if (!has_prefix(p.filename().string(), "rfkill")) continue;
        RfkillDevice r;
        if (auto n = read_file(p / "name")) r.name = trim(*n);
        if (auto t = read_file(p / "type")) r.type = trim(*t);
        if (auto s = read_file(p / "soft")) r.soft_blocked = trim(*s) == "1";
        if (auto h = read_file(p / "hard")) r.hard_blocked = trim(*h) == "1";
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<std::string> probe_bt_adapters(const ProbeOptions& opt) {
    std::vector<std::string> out;
    for (const auto& p : list_dir_sorted(fs::path(opt.sys_root) / "class/bluetooth")) {
        std::string n = p.filename().string();
        // hci0 is een adapter; hci0:256 is een verbinding
        if (has_prefix(n, "hci") && n.find(':') == std::string::npos) out.push_back(n);
    }
    return out;
}

void probe_psmoveapi(const ProbeOptions& opt, Snapshot& snap) {
    std::vector<std::string> prefixes = opt.psmoveapi_prefixes;
    if (prefixes.empty()) prefixes = default_psmoveapi_prefixes();
    snap.psmoveapi_searched = prefixes;
    for (const auto& pre : prefixes) {
        fs::path root(pre);
        std::error_code ec;
        bool lib = false;
        for (const char* d : {"lib", "lib64", "lib/x86_64-linux-gnu"})
            if (fs::exists(root / d / "libpsmoveapi.so", ec)) lib = true;
        if (!lib) continue;
        snap.psmoveapi_found = true;
        snap.psmoveapi_prefix = pre;
        // RavenGun-installatie (scripts/install-user.sh): alleen de bibliotheek is nodig, geen headers of psmove-programma.
        if (fs::exists(root / "share/ravengun/packaging", ec)) return;
        const bool hdr = fs::exists(root / "include/psmoveapi/psmove.h", ec) || fs::exists(root / "include/psmove.h", ec);
        if (!hdr) snap.psmoveapi_missing.push_back("include/psmoveapi/psmove.h");
        if (!fs::exists(root / "bin/psmove", ec)) snap.psmoveapi_missing.push_back("bin/psmove");
        return;
    }
}

Snapshot probe(const ProbeOptions& opt) {
    Snapshot s;
    s.video = probe_video(opt);
    s.hid = probe_hid(opt);
    s.usb = probe_usb(opt);
    s.rfkill = probe_rfkill(opt);
    s.bt_adapters = probe_bt_adapters(opt);

    if (auto c = read_file(opt.input_conf)) {
        s.input_conf_readable = true;
        s.classic_bonded_only = parse_classic_bonded_only(*c);
    }

    if (opt.use_bluetoothctl) {
        auto ver = run_capture({"bluetoothctl", "--version"});
        s.bluetoothctl_available = ver.ok;
        if (ver.ok) s.bluez_version = parse_bluez_version(ver.out);
        if (s.bluetoothctl_available) {
            auto paired = run_capture({"bluetoothctl", "devices", "Paired"});
            if (paired.ok) s.bt_paired = parse_bluetoothctl_devices(paired.out);
            auto known = run_capture({"bluetoothctl", "devices"});
            if (known.ok) s.bt_known = parse_bluetoothctl_devices(known.out);
        }
    }

    fs::path uinput = fs::path(opt.dev_root) / "uinput";
    std::error_code ec;
    s.uinput_exists = fs::exists(uinput, ec);
    s.uinput_accessible = s.uinput_exists && can_rw(uinput);

    probe_psmoveapi(opt, s);
    return s;
}

}  // namespace ravengun
