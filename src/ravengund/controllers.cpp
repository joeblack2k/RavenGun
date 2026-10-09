#include "controllers.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <linux/hidraw.h>
#include <poll.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hid_thread.hpp"
#include "ravengun/config/settings.hpp"
#include "ravengun/core/bias_store.hpp"
#include "ravengun/core/input_model.hpp"
#include "ravengun/core/magnetometer.hpp"
#include "ravengun/json.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/parse.hpp"

namespace ravengund {

using namespace ravengun;
namespace pr = ravengun::pairing;

namespace {

// Start een programma met een vaste argumentenlijst (execve, geen shell, vaste kleine omgeving), leest stdout+stderr en wacht
// hoogstens timeout_ms. Exitcode, of -1 bij een fout of time-out (err gevuld).
int run_capture(const std::vector<std::string>& argv, std::string& out, int timeout_ms, std::string& err) {
    out.clear();
    if (argv.empty()) { err = "geen programma"; return -1; }
    int pipefd[2];
    if (::pipe2(pipefd, O_CLOEXEC) != 0) { err = std::strerror(errno); return -1; }
    std::vector<char*> av;
    for (const auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    char env0[] = "PATH=/usr/bin:/bin", env1[] = "LC_ALL=C";
    char* envp[] = {env0, env1, nullptr};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 1);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], 2);
    pid_t pid = 0;
    int rc = ::posix_spawn(&pid, av[0], &fa, nullptr, av.data(), envp);
    posix_spawn_file_actions_destroy(&fa);
    ::close(pipefd[1]);
    if (rc != 0) { ::close(pipefd[0]); err = argv[0] + ": " + std::strerror(rc); return -1; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buf[8192];
    bool eof = false;
    while (!eof) {
        int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        if (left <= 0) break;
        pollfd pf{pipefd[0], POLLIN, 0};
        if (::poll(&pf, 1, left) <= 0) continue;
        ssize_t n = ::read(pipefd[0], buf, sizeof buf);
        if (n > 0) { if (out.size() < (1u << 20)) out.append(buf, static_cast<size_t>(n)); }
        else if (n == 0 || errno != EINTR) eof = true;
    }
    ::close(pipefd[0]);
    for (;;) {
        int status = 0;
        pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (w < 0) { err = "waitpid mislukt"; return -1; }
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
            err = argv[0] + " reageerde niet binnen " + std::to_string(timeout_ms / 1000) + " s";
            return -1;
        }
        ::usleep(10000);
    }
}

std::string type_from_modalias(const std::string& m, std::string& model) {
    // "usb:v054Cp042Fd0000" of "bluetooth:v054Cp03D5d0001"
    std::string u = m;
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u.find("V054CP03D5") != std::string::npos) { model = "ZCM1"; return "move"; }
    if (u.find("V054CP0C5E") != std::string::npos) { model = "ZCM2"; return "move"; }
    if (u.find("V054CP042F") != std::string::npos) { model.clear(); return "navigation"; }
    return {};
}

class SystemOps final : public ControllerOps {
public:
    std::vector<sysinfo::HidrawDevice> scan() override { return sysinfo::scan_sony_hidraw(); }

    bool bluez_list(std::vector<BluezDevice>& out, std::string& adapter, std::string& err) override {
        out.clear();
        std::string text;
        int rc = run_capture({"/usr/bin/busctl", "--system", "--json=short", "call", "org.bluez", "/",
                              "org.freedesktop.DBus.ObjectManager", "GetManagedObjects"}, text, 3000, err);
        if (rc != 0) { if (err.empty()) err = "busctl: " + text.substr(0, 200); return false; }
        auto p = json::parse(text);
        const json::Value* data = p.ok ? p.value.find("data") : nullptr;
        if (!data || !data->is_array() || data->arr.empty() || !data->arr[0].is_object()) { err = "onverwacht antwoord van BlueZ"; return false; }
        auto prop = [](const json::Value& iface, const char* k) -> const json::Value* {
            const json::Value* v = iface.find(k);
            return v ? v->find("data") : nullptr;
        };
        for (const auto& kv : data->arr[0].obj) {
            if (const json::Value* a = kv.second.find("org.bluez.Adapter1")) {
                if (const json::Value* ad = prop(*a, "Address"); ad && ad->is_string() && adapter.empty()) adapter = config::normalize_address(ad->str);
            }
            const json::Value* d = kv.second.find("org.bluez.Device1");
            if (!d) continue;
            const json::Value* mod = prop(*d, "Modalias");
            const json::Value* ad = prop(*d, "Address");
            if (!mod || !mod->is_string() || !ad || !ad->is_string()) continue;
            BluezDevice b;
            b.path = kv.first;
            b.type = type_from_modalias(mod->str, b.model);
            if (b.type.empty()) continue;
            b.address = config::normalize_address(ad->str);
            if (b.address.empty()) continue;
            if (const json::Value* n = prop(*d, "Name"); n && n->is_string()) b.name = n->str;
            if (const json::Value* x = prop(*d, "Paired"); x && x->is_bool()) b.paired = x->b;
            if (const json::Value* x = prop(*d, "Bonded"); x && x->is_bool()) b.paired = b.paired || x->b;
            if (const json::Value* x = prop(*d, "Connected"); x && x->is_bool()) b.connected = x->b;
            out.push_back(b);
        }
        return true;
    }

    bool bluez_remove(const std::string& address, std::string& err) override {
        const std::string a = config::normalize_address(address);
        if (a.empty()) { err = "ongeldig adres"; return false; }
        // Het D-Bus-pad van het apparaat (en daarmee de adapter) uit de BlueZ-lijst; anders hci0.
        std::vector<BluezDevice> devs;
        std::string adapter, lerr, path;
        if (bluez_list(devs, adapter, lerr))
            for (const auto& d : devs) if (d.address == a) path = d.path;
        if (path.empty()) {
            std::string up = a;
            for (auto& c : up) c = c == ':' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            path = "/org/bluez/hci0/dev_" + up;
        }
        const std::string adapter_path = path.substr(0, path.rfind('/'));
        std::string out;
        int rc = run_capture({"/usr/bin/busctl", "--system", "call", "org.bluez", adapter_path, "org.bluez.Adapter1", "RemoveDevice", "o", path},
                             out, 5000, err);
        if (rc == 0) return true;
        if (err.empty()) err = out.empty() ? "busctl gaf exitcode " + std::to_string(rc) : out.substr(0, 200);
        while (!err.empty() && (err.back() == '\n' || err.back() == ' ')) err.pop_back();
        return false;
    }

    bool helper_available() override {
        struct stat st{};
        if (::stat(pr::kBtHelperPath, &st) != 0 || st.st_uid != 0 || (st.st_mode & 022) || !(st.st_mode & S_IXUSR)) return false;
        std::string out, err;
        return run_capture({"/usr/bin/sudo", "-n", "-l", pr::kBtHelperPath, "register", "00:00:00:00:00:01", "move"}, out, 3000, err) == 0;
    }

    int run_helper(const std::vector<std::string>& argv, std::string& output) override {
        std::string err;
        int rc = run_capture(argv, output, 30000, err);
        if (rc < 0 && !err.empty()) output = err;
        return rc;
    }

    bool read_usb(const sysinfo::HidrawDevice& d, std::string& own, std::string& master, std::string& err) override {
        int fd = ::open(d.node.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) { err = d.node + ": " + std::strerror(errno); return false; }
        bool ok = false;
        if (d.type() == "move") {
            uint8_t buf[pr::kMoveGetSize] = {pr::kMoveGetAddr};
            int n = ::ioctl(fd, HIDIOCGFEATURE(sizeof buf), buf);
            ok = n > 0 && pr::decode_move_addresses(buf, static_cast<size_t>(n), own, master);
            if (!ok) err = "feature-rapport 0x04 lezen mislukt";
        } else {
            uint8_t a[pr::kNavOwnSize] = {pr::kNavOwnAddr};
            uint8_t m[pr::kNavMasterSize] = {pr::kNavMaster};
            int n1 = ::ioctl(fd, HIDIOCGFEATURE(sizeof a), a);
            int n2 = ::ioctl(fd, HIDIOCGFEATURE(sizeof m), m);
            ok = n1 >= 10 && n2 >= 8 && pr::decode_nav_own(a, sizeof a, own) && pr::decode_nav_master(m, sizeof m, master);
            if (!ok) err = "feature-rapport 0xF2/0xF5 lezen mislukt";
        }
        ::close(fd);
        return ok;
    }

    bool set_master(const sysinfo::HidrawDevice& d, const std::string& host, std::string& err) override {
        std::vector<uint8_t> buf = d.type() == "move" ? pr::encode_move_set_master(host) : pr::encode_nav_set_master(host);
        if (buf.empty()) { err = "ongeldig adres van de adapter"; return false; }
        int fd = ::open(d.node.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) { err = d.node + ": " + std::strerror(errno); return false; }
        int n = ::ioctl(fd, HIDIOCSFEATURE(buf.size()), buf.data());
        int e = errno;
        // Teruglezen ter controle.
        std::string own, master, rerr;
        ::close(fd);
        if (n < 0) { err = std::string("feature-rapport schrijven mislukt: ") + std::strerror(e); return false; }
        if (!read_usb(d, own, master, rerr)) { err = rerr; return false; }
        if (master != config::normalize_address(host)) { err = "de controller meldt na het zetten een ander master-adres (" + redact_mac(master) + ")"; return false; }
        return true;
    }

    sysinfo::NavconStatus battery(const std::string& address) override {
        const std::string dir = "/sys/class/power_supply/sony_controller_battery_" + config::normalize_address(address);
        std::ifstream cap(dir + "/capacity"), st(dir + "/status");
        if (!cap && !st) return {};
        int pct = -1;
        if (cap) { cap >> pct; if (!cap) pct = -1; }
        std::string s;
        if (st) std::getline(st, s);
        return sysinfo::classify_battery(pct, s);
    }

    void remove_file(const std::string& path) override { ::unlink(path.c_str()); }
};

std::string find_address(const config::Settings& s, const std::vector<BluezDevice>& bz, const std::string& sel) {
    if (std::string a = config::normalize_address(sel); !a.empty()) return a;
    for (const auto& kv : s.controllers) if (core::controller_key(kv.first) == sel) return kv.first;
    for (const auto& b : bz) if (core::controller_key(b.address) == sel) return b.address;
    return {};
}

}  // namespace

std::unique_ptr<ControllerOps> make_system_controller_ops() { return std::make_unique<SystemOps>(); }

namespace {

// Nep-systeem uit een JSON-bestand (ravengund --controllers-fake PAD), voor tests en schermafdrukken zonder hardware.
// Verandert niets op het systeem; koppelen en verwijderen werken alleen op de nepgegevens.
class FakeOps final : public ControllerOps {
public:
    std::mutex m;
    std::vector<sysinfo::HidrawDevice> devs;
    std::vector<BluezDevice> bluez;
    std::string adapter;
    bool helper = false;
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> usb;  // node -> (eigen adres, master)
    std::vector<std::pair<std::string, sysinfo::NavconStatus>> batteries;

    std::vector<sysinfo::HidrawDevice> scan() override { std::lock_guard<std::mutex> g(m); return devs; }
    bool bluez_list(std::vector<BluezDevice>& out, std::string& a, std::string&) override {
        std::lock_guard<std::mutex> g(m);
        out = bluez;
        a = adapter;
        return true;
    }
    bool bluez_remove(const std::string& address, std::string& err) override {
        std::lock_guard<std::mutex> g(m);
        for (auto it = bluez.begin(); it != bluez.end(); ++it)
            if (it->address == address) { bluez.erase(it); return true; }
        err = "Does Not Exist";
        return false;
    }
    bool helper_available() override { std::lock_guard<std::mutex> g(m); return helper; }
    int run_helper(const std::vector<std::string>& argv, std::string& output) override {
        std::lock_guard<std::mutex> g(m);
        if (argv.size() < 5) return 2;
        const std::string a = config::normalize_address(argv[4]);
        if (argv[3] == "register") {
            BluezDevice b;
            b.address = a;
            b.type = argv.size() > 5 && argv[5] == "navigation" ? "navigation" : "move";
            b.model = b.type == "move" ? "ZCM1" : "";
            b.paired = true;
            b.name = b.type == "move" ? "Motion Controller" : "Navigation Controller";
            bluez.push_back(b);
            output = "geregistreerd (nep)";
            return 0;
        }
        output = "verwijderd (nep)";
        return 0;
    }
    bool read_usb(const sysinfo::HidrawDevice& d, std::string& own, std::string& master, std::string& err) override {
        std::lock_guard<std::mutex> g(m);
        for (const auto& u : usb) if (u.first == d.node) { own = u.second.first; master = u.second.second; return true; }
        err = "geen nepadres";
        return false;
    }
    bool set_master(const sysinfo::HidrawDevice& d, const std::string& host, std::string&) override {
        std::lock_guard<std::mutex> g(m);
        for (auto& u : usb) if (u.first == d.node) u.second.second = config::normalize_address(host);
        return true;
    }
    sysinfo::NavconStatus battery(const std::string& address) override {
        std::lock_guard<std::mutex> g(m);
        for (const auto& b : batteries) if (b.first == address) return b.second;
        return {};
    }
    void remove_file(const std::string&) override {}
};

}  // namespace

std::unique_ptr<ControllerOps> make_fake_controller_ops(const std::string& text, std::string& err) {
    auto p = json::parse(text);
    if (!p.ok || !p.value.is_object()) { err = "ongeldige JSON"; return nullptr; }
    auto f = std::make_unique<FakeOps>();
    const json::Value& o = p.value;
    auto str = [](const json::Value& v, const char* k) { const json::Value* x = v.find(k); return x && x->is_string() ? x->str : std::string(); };
    auto boolean = [](const json::Value& v, const char* k) { const json::Value* x = v.find(k); return x && x->is_bool() && x->b; };
    f->adapter = config::normalize_address(str(o, "adapter"));
    f->helper = boolean(o, "helper");
    if (const json::Value* b = o.find("bluez"); b && b->is_array())
        for (const auto& x : b->arr) {
            BluezDevice d;
            d.address = config::normalize_address(str(x, "address"));
            d.type = str(x, "type");
            d.model = str(x, "model");
            d.name = str(x, "name");
            d.paired = boolean(x, "paired");
            d.connected = boolean(x, "connected");
            if (!d.address.empty()) f->bluez.push_back(d);
        }
    if (const json::Value* h = o.find("hidraw"); h && h->is_array())
        for (const auto& x : h->arr) {
            sysinfo::HidrawDevice d;
            d.node = str(x, "node");
            const json::Value* bus = x.find("bus");
            d.bus = bus && bus->is_number() ? static_cast<unsigned>(bus->num) : 5;
            d.vendor = sysinfo::kSonyVendor;
            const std::string t = str(x, "type");
            d.product = t == "navigation" ? sysinfo::kNavigation : str(x, "model") == "ZCM2" ? sysinfo::kMoveZcm2 : sysinfo::kMoveZcm1;
            d.uniq = str(x, "uniq");
            f->devs.push_back(d);
            if (d.usb()) f->usb.push_back({d.node, {config::normalize_address(str(x, "own")), config::normalize_address(str(x, "master"))}});
        }
    if (const json::Value* b = o.find("battery"); b && b->is_object())
        for (const auto& kv : b->obj) {
            const json::Value* pc = kv.second.find("percent");
            f->batteries.push_back({config::normalize_address(kv.first),
                                    sysinfo::classify_battery(pc && pc->is_number() ? static_cast<int>(pc->num) : -1, str(kv.second, "status"))});
        }
    return f;
}

ControllerService::ControllerService(SharedState& sh, std::unique_ptr<ControllerOps> ops) : sh_(sh), ops_(std::move(ops)) {}

ControllerService::~ControllerService() { stop(); }

void ControllerService::start() {
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread(&ControllerService::main_loop, this);
}

void ControllerService::stop() {
    {
        std::lock_guard<std::mutex> g(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void ControllerService::post_reconfigure() {
    // Niet wachten: de HID-thread voert het uit bij zijn volgende ronde.
    (void)sh_.post([](HidContext& h) { h.reconfigure(); return std::string("1"); });
}

void ControllerService::auto_register(const std::vector<BluezDevice>& devs) {
    if (!sh_.settings || !auto_register_) return;
    bool any = false;
    const config::Settings cur = sh_.settings->get();
    for (const auto& d : devs) {
        if (!d.paired) continue;
        auto it = cur.controllers.find(d.address);
        if (it != cur.controllers.end()) continue;  // bekend (ook verwijderd-met-instellingen: dat blijft zo tot hij verbindt)
        any = true;
    }
    if (!any) return;
    int added_n = 0;
    sh_.settings->update([&](config::Settings& s) {
        for (const auto& d : devs) {
            if (!d.paired || s.controllers.count(d.address)) continue;
            bool added = false;
            config::register_controller(s, d.address, d.type, d.model, static_cast<int64_t>(std::time(nullptr)), &added);
            if (added) ++added_n;
        }
    });
    if (added_n) {
        logf("info", "controllers: %d gekoppelde controller(s) uit BlueZ opgenomen", added_n);
        post_reconfigure();
    }
}

bool ControllerService::refresh_bluez_locked(std::unique_lock<std::mutex>& lk) {
    lk.unlock();
    std::vector<BluezDevice> devs;
    std::string adapter, err;
    bool ok = ops_->bluez_list(devs, adapter, err);
    lk.lock();
    bluez_at_ = mono_ns();
    if (!ok) { bluez_error_ = err; return false; }
    bluez_ = devs;
    adapter_ = adapter;
    bluez_error_.clear();
    lk.unlock();
    auto_register(devs);
    lk.lock();
    return true;
}

void ControllerService::refresh() {
    std::unique_lock<std::mutex> lk(m_);
    refresh_bluez_locked(lk);
}

void ControllerService::main_loop() {
    std::unique_lock<std::mutex> lk(m_);
    refresh_bluez_locked(lk);
    while (!stop_) {
        const bool pairing = machine_.active();
        cv_.wait_for(lk, std::chrono::milliseconds(pairing ? 250 : 1000), [&] { return stop_; });
        if (stop_) break;
        if (machine_.active()) {
            lk.unlock();
            pair_round();
            lk.lock();
        } else if (mono_ns() - bluez_at_ > 10'000'000'000LL) {
            refresh_bluez_locked(lk);
        }
    }
}

void ControllerService::pair_round() {
    std::unique_lock<std::mutex> lk(m_);
    if (!machine_.active() || machine_.busy()) return;
    const int64_t now = mono_ns();
    if (now - bluez_at_ > 2'000'000'000LL) refresh_bluez_locked(lk);
    const bool helper_due = now - helper_at_ > 3'000'000'000LL &&
                            (machine_.state() == pr::State::NeedHelper || machine_.state() == pr::State::Register ||
                             machine_.state() == pr::State::SetMaster || helper_at_ == 0);
    lk.unlock();
    // Wat is er nu te zien (sysfs, zonder lock).
    const auto devs = ops_->scan();
    bool helper = false;
    if (helper_due) helper = ops_->helper_available();
    lk.lock();
    if (helper_due) { helper_ok_ = helper; helper_at_ = now; }
    pr::Observation obs;
    obs.host = adapter_;
    obs.helper_installed = helper_ok_;
    for (const auto& b : bluez_) if (b.paired) obs.bluez_paired.insert(b.address);
    for (const auto& d : devs) {
        if (d.bluetooth()) { if (auto a = config::normalize_address(d.uniq); !a.empty()) obs.bt_connected.insert(a); continue; }
        if (!d.usb()) continue;
        // Adressen eenmaal per aangesloten controller lezen (alleen lezen: HIDIOCGFEATURE).
        UsbRead* rd = nullptr;
        for (auto& u : usb_reads_) if (u.first == d.node) rd = &u.second;
        if (!rd) {
            UsbRead r;
            lk.unlock();
            ops_->read_usb(d, r.own, r.master, r.err);
            lk.lock();
            usb_reads_.push_back({d.node, r});
            rd = &usb_reads_.back().second;
        }
        pr::UsbCandidate c;
        c.node = d.node; c.type = d.type(); c.model = d.model(); c.address = rd->own; c.master = rd->master;
        obs.usb.push_back(c);
    }
    // Vergeten wat niet meer aan USB hangt (opnieuw aansluiten = opnieuw lezen).
    for (auto it = usb_reads_.begin(); it != usb_reads_.end();) {
        bool present = false;
        for (const auto& d : devs) present = present || (d.usb() && d.node == it->first);
        it = present ? std::next(it) : usb_reads_.erase(it);
    }
    for (const auto& si : sh_.slot_infos()) if (si.connected) obs.bt_connected.insert(si.address);
    pr::Action act = machine_.step(now, obs);
    if (act.kind == pr::Action::Kind::None) {
        if (machine_.state() == pr::State::Done && !registered_.empty()) {
            const std::string a = registered_;
            registered_.clear();
            lk.unlock();
            if (sh_.settings) sh_.settings->update([&](config::Settings& s) {
                if (auto it = s.controllers.find(a); it != s.controllers.end()) it->second.last_seen_unix = static_cast<int64_t>(std::time(nullptr));
            });
            logf("info", "koppelen: %s verbonden", redact_mac(a).c_str());
        }
        return;
    }
    lk.unlock();
    sysinfo::HidrawDevice dev;
    for (const auto& d : devs) if (d.node == act.target.node) dev = d;
    bool ok = false;
    std::string msg;
    if (act.kind == pr::Action::Kind::SetMaster) {
        ok = !dev.node.empty() && ops_->set_master(dev, act.host, msg);
        if (dev.node.empty()) msg = "de controller is niet meer aangesloten";
        logf(ok ? "info" : "waarschuwing", "koppelen: master-adres van %s %s%s", redact_mac(act.target.address).c_str(), ok ? "gezet" : "NIET gezet: ", ok ? "" : msg.c_str());
        if (ok) {
            std::lock_guard<std::mutex> g(m_);
            for (auto& u : usb_reads_) if (u.first == act.target.node) u.second.master = config::normalize_address(act.host);
        }
    } else {
        auto argv = pr::helper_argv("register", act.target.address, pr::helper_type(act.target.type, act.target.model));
        if (argv.empty()) { msg = "ongeldig adres of type"; }
        else {
            std::string out;
            int rc = ops_->run_helper(argv, out);
            ok = rc == 0;
            while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
            if (!ok) msg = out.empty() ? "exitcode " + std::to_string(rc) : out;
            logf(ok ? "info" : "fout", "koppelen: registratie van %s bij BlueZ %s%s", redact_mac(act.target.address).c_str(), ok ? "gelukt" : "mislukt: ",
                 ok ? "" : msg.c_str());
        }
        if (ok && sh_.settings) {
            // In de lijst opnemen (nieuwe Move: het laagste vrije spelernummer) en de spelers opnieuw instellen.
            int player = 0;
            sh_.settings->update([&](config::Settings& s) {
                player = config::register_controller(s, act.target.address, act.target.type, act.target.model, static_cast<int64_t>(std::time(nullptr)));
            });
            std::lock_guard<std::mutex> g(m_);
            registered_ = act.target.address;
            registered_player_ = player;
        }
        if (ok) post_reconfigure();
    }
    lk.lock();
    machine_.done(mono_ns(), ok, msg);
    if (ok && act.kind == pr::Action::Kind::Register) bluez_at_ = 0;  // de BlueZ-lijst is veranderd
}

std::string ControllerService::pair_json_locked() const {
    json::Writer w;
    w.begin_object();
    w.key("state").value(pr::to_string(machine_.state()));
    w.key("active").value(machine_.active());
    w.key("busy").value(machine_.busy());
    w.key("instruction").value(machine_.instruction());
    if (machine_.message().empty()) w.key("message").null(); else w.key("message").value(machine_.message());
    const auto& t = machine_.target();
    w.key("target").begin_object();
    w.key("type").value(t.type).key("model").value(t.model);
    w.key("address").value(redact_mac(t.address)).key("key").value(t.address.empty() ? std::string() : core::controller_key(t.address));
    w.end_object();
    if (!registered_.empty() || registered_player_) w.key("player").value(registered_player_); else w.key("player").null();
    w.key("helper").begin_object().key("installed").value(helper_ok_).key("path").value(pr::kBtHelperPath)
        .key("install_command").value(pr::kBtHelperInstall).end_object();
    w.key("adapter_found").value(!adapter_.empty());
    w.end_object();
    return w.str();
}

std::string ControllerService::pair(const std::string& action, std::string& code, std::string& msg) {
    std::unique_lock<std::mutex> lk(m_);
    const int64_t now = mono_ns();
    if (action == "start") {
        if (machine_.active()) { code = "unavailable"; msg = "er loopt al een koppeling"; return {}; }
        machine_.start(now);
        registered_.clear();
        registered_player_ = 0;
        usb_reads_.clear();
        helper_at_ = 0;
        bluez_at_ = 0;
        logf("info", "koppelen gestart: wacht op een controller via USB");
    } else if (action == "confirm") {
        if (!machine_.confirm(now)) { code = "unavailable"; msg = "er is nu niets te bevestigen"; return {}; }
        helper_at_ = 0;
    } else if (action == "cancel") {
        machine_.cancel();
    }
    std::string out = pair_json_locked();
    lk.unlock();
    cv_.notify_all();
    return out;
}

std::string ControllerService::unpair(const std::string& sel, bool keep, std::string& code, std::string& msg) {
    std::vector<BluezDevice> bz;
    {
        std::lock_guard<std::mutex> g(m_);
        bz = bluez_;
    }
    const config::Settings s = sh_.settings ? sh_.settings->get() : config::Settings{};
    const std::string a = find_address(s, bz, sel);
    bool known = !a.empty() && s.controllers.count(a);
    bool in_bluez = false;
    for (const auto& b : bz) in_bluez = in_bluez || b.address == a;
    if (a.empty() || (!known && !in_bluez)) { code = "invalid_argument"; msg = "onbekende controller"; return {}; }
    // 1. Bij BlueZ ontkoppelen (D-Bus, zonder root); lukt dat niet en is de hulp er, dan de hulp.
    std::string how = "niet bij Bluetooth bekend";
    if (in_bluez) {
        std::string err;
        if (ops_->bluez_remove(a, err)) {
            how = "bij Bluetooth verwijderd";
        } else if (ops_->helper_available()) {
            std::string out;
            int rc = ops_->run_helper(pr::helper_argv("unregister", a, ""), out);
            if (rc != 0) { code = "internal"; msg = "Bluetooth weigerde het verwijderen (" + err + ") en de hulp ook: " + out; return {}; }
            how = "bij Bluetooth verwijderd met de hulp (Bluetooth is even herstart)";
        } else {
            code = "internal";
            msg = "Bluetooth weigerde het verwijderen: " + err;
            return {};
        }
    }
    // 2. Uit de lijst, eventueel met bewaarde instellingen; kalibratie en bias alleen weg als de instellingen weg mogen.
    if (known && sh_.settings) {
        sh_.settings->update([&](config::Settings& st) {
            auto it = st.controllers.find(a);
            if (it == st.controllers.end()) return;
            if (keep) { it->second.removed = true; it->second.player = 0; }
            else st.controllers.erase(it);
        });
        if (!keep) {
            const std::string key = core::controller_key(a);
            if (auto d = core::default_mag_calibration_dir(); !d.empty()) ops_->remove_file(d + "/" + key + ".json");
            if (auto d = core::default_bias_dir(); !d.empty()) ops_->remove_file(d + "/" + key + ".json");
        }
    }
    post_reconfigure();
    {
        std::lock_guard<std::mutex> g(m_);
        bluez_at_ = 0;
    }
    logf("info", "controller %s verwijderd (%s; instellingen %s)", redact_mac(a).c_str(), how.c_str(), keep ? "bewaard" : "gewist");
    json::Writer w;
    w.begin_object().key("removed").value(true).key("bluez").value(how).key("settings_kept").value(keep && known).end_object();
    return w.str();
}

std::string ControllerService::list_json(bool full_ids) {
    std::vector<BluezDevice> bz;
    std::string adapter, berr, pairing;
    bool helper;
    {
        // De hulp hoogstens eens per 30 s controleren (sudo -n -l), niet bij elke vraag.
        std::unique_lock<std::mutex> lk(m_);
        if (helper_at_ == 0 || mono_ns() - helper_at_ > 30'000'000'000LL) {
            helper_at_ = mono_ns();
            lk.unlock();
            const bool h = ops_->helper_available();
            lk.lock();
            helper_ok_ = h;
        }
        if (mono_ns() - bluez_at_ > 5'000'000'000LL && !machine_.active()) refresh_bluez_locked(lk);
        bz = bluez_;
        adapter = adapter_;
        berr = bluez_error_;
        helper = helper_ok_;
        pairing = pair_json_locked();
    }
    const config::Settings s = sh_.settings ? sh_.settings->get() : config::Settings{};
    const auto devs = ops_->scan();
    const auto slots = sh_.slot_infos();
    // Alle bekende adressen: config.json (ook verwijderd-met-instellingen) en BlueZ.
    std::vector<std::string> addrs;
    for (const auto& kv : s.controllers) addrs.push_back(kv.first);
    for (const auto& b : bz) if (!s.controllers.count(b.address)) addrs.push_back(b.address);
    // Volgorde: Moves op spelernummer (zonder speler achteraan), dan de Navigation Controllers, dan wat alleen BlueZ kent.
    auto rank = [&](const std::string& a) {
        auto it = s.controllers.find(a);
        if (it == s.controllers.end()) return 1000;
        const auto& e = it->second;
        return (e.type == "move" ? 0 : 100) + (e.removed ? 50 : 0) + (e.player > 0 ? e.player : 9);
    };
    std::stable_sort(addrs.begin(), addrs.end(), [&](const std::string& x, const std::string& y) { return rank(x) < rank(y); });
    json::Writer w;
    w.begin_object();
    w.key("controllers").begin_array();
    for (const auto& a : addrs) {
        auto it = s.controllers.find(a);
        const config::ControllerEntry* e = it == s.controllers.end() ? nullptr : &it->second;
        const BluezDevice* b = nullptr;
        for (const auto& x : bz) if (x.address == a) b = &x;
        const std::string type = e ? e->type : b->type;
        std::string connection;
        for (const auto& d : devs) {
            if (d.bluetooth() && config::normalize_address(d.uniq) == a) connection = "bluetooth";
        }
        int slot_player = 0;
        for (size_t i = 0; i < slots.size(); ++i) if (slots[i].connected && slots[i].address == a) { slot_player = static_cast<int>(i) + 1; connection = "bluetooth"; }
        w.begin_object();
        w.key("key").value(core::controller_key(a));
        w.key("address").value(full_ids ? a : redact_mac(a));
        w.key("type").value(type);
        w.key("model").value(e && !e->model.empty() ? e->model : (b ? b->model : std::string()));
        w.key("name").value(e && !e->name.empty() ? e->name : (b && !b->name.empty() ? b->name : std::string(type == "move" ? "Move" : "Navigation Controller")));
        w.key("player").value(e ? e->player : 0);
        w.key("known").value(e != nullptr);
        w.key("removed").value(e && e->removed);
        w.key("paired").value(b && b->paired);
        w.key("connected").value(!connection.empty());
        if (connection.empty()) w.key("connection").null(); else w.key("connection").value(connection);
        w.key("in_use").value(slot_player > 0);
        // Batterij: van de Move uit de pijplijn (PSMoveAPI-niveaus), anders uit power_supply (hid-sony).
        w.key("battery").begin_object();
        if (slot_player > 0 && slots[static_cast<size_t>(slot_player - 1)].battery_raw >= 0) {
            core::BatteryInfo bi = core::battery_info(slots[static_cast<size_t>(slot_player - 1)].battery_raw);
            w.key("known").value(true).key("code").value(bi.code).key("text").value(bi.text).key("charging").value(bi.charging).key("percent").null();
        } else {
            sysinfo::NavconStatus ns = connection.empty() ? sysinfo::NavconStatus{} : ops_->battery(a);
            if (ns.connected) w.key("known").value(true).key("code").value(ns.code).key("text").value(ns.text).key("charging").value(ns.status == "charging");
            else w.key("known").value(false).key("code").value("unknown").key("text").value(connection.empty() ? "niet verbonden" : "onbekend").key("charging").value(false);
            if (ns.connected && ns.percent >= 0) w.key("percent").value(ns.percent); else w.key("percent").null();
        }
        w.end_object();
        if (e && e->last_seen_unix) w.key("last_seen_unix").value(static_cast<long long>(e->last_seen_unix)); else w.key("last_seen_unix").null();
        if (e && e->added_unix) w.key("added_unix").value(static_cast<long long>(e->added_unix)); else w.key("added_unix").null();
        // LED-kleur: zoals de pijplijn hem nu stuurt, anders de instelling.
        w.key("led").begin_object();
        if (type == "move") {
            const config::Settings eff = config::effective(s, a);
            int rgb[3] = {eff.led.r, eff.led.g, eff.led.b};
            bool en = eff.led.enabled;
            if (slot_player > 0) { const auto& si = slots[static_cast<size_t>(slot_player - 1)]; en = si.led_enabled; for (int k = 0; k < 3; ++k) rgb[k] = si.led[k]; }
            else if (e && e->player >= 2 && !en) { auto pl = config::player_led(e->player); en = true; rgb[0] = pl.r; rgb[1] = pl.g; rgb[2] = pl.b; }
            w.key("enabled").value(en).key("rgb").begin_array().value(rgb[0]).value(rgb[1]).value(rgb[2]).end_array();
        } else {
            w.key("enabled").value(false).key("rgb").null();
        }
        w.end_object();
        w.key("overrides").value(e ? static_cast<int>(e->keys.size()) : 0);
        w.end_object();
    }
    w.end_array();
    // USB: wat er nu via een kabel aan hangt (voor de wizard en de lijst).
    w.key("usb").begin_array();
    for (const auto& d : devs) if (d.usb()) w.begin_object().key("type").value(d.type()).key("model").value(d.model()).end_object();
    w.end_array();
    w.key("adapter").begin_object().key("found").value(!adapter.empty()).key("address").value(full_ids ? adapter : redact_mac(adapter));
    if (berr.empty()) w.key("error").null(); else w.key("error").value(berr);
    w.end_object();
    w.key("helper").begin_object().key("installed").value(helper).key("path").value(pr::kBtHelperPath).key("install_command").value(pr::kBtHelperInstall).end_object();
    w.key("players").value(config::kMaxPlayers);
    w.key("pairing").raw(pairing);
    w.end_object();
    return w.str();
}

}  // namespace ravengund
