// SPDX-License-Identifier: GPL-3.0-or-later
#include "syshelper/bt_core.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include "syshelper/common.hpp"

// De SDP-record hieronder (de HID-beschrijving van een Sixaxis-achtige controller; de kernel, hid-sony, vervangt de beschrijving
// voor de Move en de Navigation Controller door een eigen) komt uit PSMoveAPI, src/platform/psmove_port_linux.cpp
// (BLUEZ5_CACHE_ENTRY). PSMoveAPI: Copyright (c) 2011, 2012 Thomas Perl <m@thp.io>, BSD-2-Clause:
//   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
//   following conditions are met: 1. Redistributions of source code must retain the above copyright notice, this list of
//   conditions and the following disclaimer. 2. Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation and/or other materials provided with the
//   distribution. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
//   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING IN ANY WAY OUT OF
//   THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#define RG_SDP "3601920900000A000100000900013503191124090004350D35061901000900113503190011090006350909656E09006A0901000900093508350619112409010009000D350F350D350619010009001335031900110901002513576972656C65737320436F6E74726F6C6C65720901012513576972656C65737320436F6E74726F6C6C6572090102251B536F6E7920436F6D707574657220456E7465727461696E6D656E740902000901000902010901000902020800090203082109020428010902052801090206359A35980822259405010904A101A102850175089501150026FF00810375019513150025013500450105091901291381027501950D0600FF8103150026FF0005010901A10075089504350046FF0009300931093209358102C0050175089527090181027508953009019102750895300901B102C0A1028502750895300901B102C0A10285EE750895300901B102C0A10285EF750895300901B102C0C0090207350835060904090901000902082800090209280109020A280109020B09010009020C093E8009020D280009020E2800"

namespace ravengun::syshelper {

namespace {

struct TypeInfo {
    const char* type;
    const char* name;
    const char* cls;
    int source, product, version;
};
constexpr TypeInfo kTypes[] = {
    {"move", "Motion Controller", "0x002508", 1, 981, 1},
    {"move2", "Motion Controller", "0x002508", 1, 3166, 1},
    {"navigation", "Navigation Controller", "0x000508", 2, 1071, 0},
};

const TypeInfo* find_type(const std::string& t) {
    for (const auto& i : kTypes)
        if (t == i.type) return &i;
    return nullptr;
}

std::string random_key_hex() {
    unsigned char b[16];
    size_t got = 0;
    while (got < sizeof b) {
        const ssize_t n = ::getrandom(b + got, sizeof b - got, 0);
        if (n < 0) { if (errno == EINTR) continue; return {}; }
        got += static_cast<size_t>(n);
    }
    static const char* hex = "0123456789ABCDEF";
    std::string s;
    for (unsigned char c : b) { s += hex[c >> 4]; s += hex[c & 15]; }
    return s;
}

bool is_32_hex_upper(const std::string& k) {
    if (k.size() != 32) return false;
    for (char c : k)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

// Regels van een tekst.
std::vector<std::string> lines_of(const std::string& t) {
    std::vector<std::string> v;
    size_t p = 0;
    while (p <= t.size()) {
        const size_t e = t.find('\n', p);
        v.push_back(t.substr(p, e == std::string::npos ? std::string::npos : e - p));
        if (e == std::string::npos) break;
        p = e + 1;
    }
    return v;
}

// ^<key>=<cijfers>$ in de sectie [DeviceID]; -1 als het ontbreekt of geen getal is.
long deviceid_value(const std::string& info, const std::string& key) {
    bool in = false;
    for (const std::string& l : lines_of(info)) {
        if (!l.empty() && l[0] == '[') { in = (l == "[DeviceID]"); continue; }
        if (!in || l.compare(0, key.size() + 1, key + "=") != 0) continue;
        const std::string v = l.substr(key.size() + 1);
        if (v.empty() || v.size() > 9) return -1;
        long n = 0;
        for (char c : v) {
            if (c < '0' || c > '9') return -1;
            n = n * 10 + (c - '0');
        }
        return n;
    }
    return -1;
}

bool name_is_controller(const std::string& info) {
    for (const std::string& l : lines_of(info))
        if (l == "Name=Motion Controller" || l == "Name=Navigation Controller") return true;
    return false;
}

// Een bestandsbeschrijving voor een map, zonder symlinks te volgen.
int open_dir_at(int parent, const std::string& name) {
    return ::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

// Eigenschappen van dir/naam zonder te volgen: 0 = bestaat niet, 1 = gewoon bestand, 2 = map, 3 = symlink of iets anders.
int kind_at(int dir, const std::string& name) {
    struct stat st{};
    if (::fstatat(dir, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT ? 0 : 3;
    if (S_ISREG(st.st_mode)) return 1;
    if (S_ISDIR(st.st_mode)) return 2;
    return 3;
}

bool read_at(int dir, const std::string& name, std::string& out) {
    const int fd = ::openat(dir, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return false;
    const bool ok = read_fd_all(fd, out);
    ::close(fd);
    return ok;
}

bool write_at(int dir, const std::string& name, const std::string& data, std::string& err) {
    std::string tmp;
    int fd = -1;
    for (int i = 0; i < 100 && fd < 0; ++i) {
        tmp = ".ravengun." + std::to_string(::getpid()) + "." + std::to_string(i);
        fd = ::openat(dir, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) break;
    }
    if (fd < 0) { err = name + ": " + std::strerror(errno); return false; }
    bool ok = true;
    size_t off = 0;
    while (ok && off < data.size()) {
        const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) { if (errno == EINTR) continue; ok = false; break; }
        off += static_cast<size_t>(n);
    }
    if (ok) ok = ::fchmod(fd, 0600) == 0 && ::fsync(fd) == 0;
    const int e = errno;
    ::close(fd);
    if (ok) ok = ::renameat(dir, tmp.c_str(), dir, name.c_str()) == 0;
    if (!ok) {
        err = name + ": " + std::strerror(errno ? errno : e);
        ::unlinkat(dir, tmp.c_str(), 0);
    }
    return ok;
}

// cp -p voor het eerste exemplaar: bestaat `backup` al, dan niets.
void backup_once(int dir, const std::string& name, const std::string& backup) {
    if (kind_at(dir, name) != 1 || kind_at(dir, backup) != 0) return;
    std::string data, err;
    if (read_at(dir, name, data)) write_at(dir, backup, data, err);
}

// rm -rf van dir/naam (een map zonder symlinks te volgen).
void remove_tree_at(int parent, const std::string& name) {
    const int k = kind_at(parent, name);
    if (k == 0) return;
    if (k != 2) { ::unlinkat(parent, name.c_str(), 0); return; }
    const int fd = open_dir_at(parent, name);
    if (fd >= 0) {
        DIR* d = ::fdopendir(::dup(fd));
        std::vector<std::string> names;
        if (d) {
            while (const dirent* e = ::readdir(d)) {
                const std::string n = e->d_name;
                if (n != "." && n != "..") names.push_back(n);
            }
            ::closedir(d);
        }
        for (const auto& n : names) remove_tree_at(fd, n);
        ::close(fd);
    }
    ::unlinkat(parent, name.c_str(), AT_REMOVEDIR);
}

struct Fd {
    int fd = -1;
    ~Fd() { if (fd >= 0) ::close(fd); }
};

// Zorgt dat Bluetooth altijd weer start, ook na een fout (zoals de trap EXIT in het script).
struct BtStopGuard {
    BtEnv& env;
    bool stopped = false;
    explicit BtStopGuard(BtEnv& e) : env(e) {}
    bool stop() {
        if (env.stop_bluetooth && !env.stop_bluetooth()) return false;
        stopped = true;
        return true;
    }
    void start() {
        if (stopped && env.start_bluetooth) env.start_bluetooth();
        stopped = false;
    }
    ~BtStopGuard() { start(); }
};

}  // namespace

const char* bt_device_name(const std::string& type) {
    const TypeInfo* t = find_type(type);
    return t ? t->name : "";
}

int parse_bt_args(int argc, const char* const* argv, BtRequest& req, std::string& err) {
    static const char* usage = "usage: ravengun-bt-helper register AA:BB:CC:DD:EE:FF move|move2|navigation | unregister AA:BB:CC:DD:EE:FF";
    if (argc < 2 || argc > 3) { err = usage; return kBtUsage; }
    const std::string action = argv[0];
    req.address = argv[1];
    req.type.clear();
    if (action == "register") {
        if (argc != 3) { err = usage; return kBtUsage; }
        req.action = BtRequest::Action::Register;
        req.type = argv[2];
    } else if (action == "unregister") {
        if (argc != 2) { err = usage; return kBtUsage; }
        req.action = BtRequest::Action::Unregister;
    } else {
        err = usage;
        return kBtUsage;
    }
    if (!valid_bt_address(req.address)) { err = "invalid address (expected AA:BB:CC:DD:EE:FF, upper case)"; return kBtUsage; }
    if (req.action == BtRequest::Action::Register && !find_type(req.type)) { err = "unknown type (move, move2 or navigation)"; return kBtUsage; }
    return kBtOk;
}

bool bt_info_is_sony(const std::string& info) {
    const long vendor = deviceid_value(info, "Vendor");
    const long product = deviceid_value(info, "Product");
    return vendor == 1356 && (product == 981 || product == 3166 || product == 1071);
}

std::string bt_info_text(const std::string& type, const std::string& key) {
    const TypeInfo* t = find_type(type);
    if (!t || !is_32_hex_upper(key)) return {};
    return std::string("[General]\nName=") + t->name + "\nClass=" + t->cls +
           "\nSupportedTechnologies=BR/EDR\nTrusted=true\nBlocked=false\nServices=00001124-0000-1000-8000-00805f9b34fb;\n\n"
           "[DeviceID]\nSource=" + std::to_string(t->source) + "\nVendor=1356\nProduct=" + std::to_string(t->product) +
           "\nVersion=" + std::to_string(t->version) + "\n\n[LinkKey]\nKey=" + key + "\nType=0\nPINLength=0\n";
}

std::string bt_cache_text(const std::string& type) {
    const TypeInfo* t = find_type(type);
    if (!t) return {};
    return std::string("[General]\nName=") + t->name + "\n\n[ServiceRecords]\n0x00010000=" RG_SDP "\n";
}

int run_bt_request(const BtRequest& req, BtEnv& env, std::string& out, std::string& err) {
    auto log = [&env](const std::string& m) { if (env.log) env.log(m); };
    const bool reg = req.action == BtRequest::Action::Register;
    if (!valid_bt_address(req.address) || (reg && !find_type(req.type))) { err = "invalid request"; return kBtUsage; }

    // De adapter: precies een map met een adres onder /var/lib/bluetooth (geen symlink).
    Fd base;
    base.fd = ::open(env.btdir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (base.fd < 0) { err = env.btdir + ": " + std::strerror(errno); return kBtFailed; }
    std::vector<std::string> adapters;
    if (DIR* d = ::fdopendir(::dup(base.fd))) {
        while (const dirent* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (is_bt_dir_name(n) && kind_at(base.fd, n) == 2) adapters.push_back(n);
        }
        ::closedir(d);
    }
    if (adapters.size() != 1) {
        log("geweigerd: " + std::to_string(adapters.size()) + " adapters gevonden");
        err = "expected exactly one Bluetooth adapter, found: " + std::to_string(adapters.size());
        return kBtRefused;
    }
    const std::string host = adapters[0];
    if (req.address == host) {
        log("geweigerd: " + req.address + " is de adapter zelf");
        err = "that is the address of the adapter itself";
        return kBtRefused;
    }
    Fd ad;
    ad.fd = open_dir_at(base.fd, host);
    if (ad.fd < 0) { err = host + ": " + std::strerror(errno); return kBtFailed; }

    const std::string info_name = "info";
    const int dir_kind = kind_at(ad.fd, req.address);
    if (dir_kind == 3) { log("geweigerd: symlink bij " + req.address); err = "refused: symlink"; return kBtRefused; }
    const int cache_kind = kind_at(ad.fd, "cache");
    if (cache_kind == 3) { err = "refused: symlink"; return kBtRefused; }

    // De bestaande registratie (als die er is).
    std::string old_info;
    bool have_info = false;
    Fd dev;
    if (dir_kind == 2) {
        dev.fd = open_dir_at(ad.fd, req.address);
        if (dev.fd >= 0) {
            const int ik = kind_at(dev.fd, info_name);
            if (ik == 3) { log("geweigerd: symlink bij " + req.address); err = "refused: symlink"; return kBtRefused; }
            if (ik == 1) have_info = read_at(dev.fd, info_name, old_info);
        }
    } else if (dir_kind == 1) {
        err = "refused: unexpected file";
        return kBtRefused;
    }
    Fd cache;
    if (cache_kind == 2) cache.fd = open_dir_at(ad.fd, "cache");
    if (cache.fd >= 0 && kind_at(cache.fd, req.address) == 3) { log("geweigerd: symlink bij " + req.address); err = "refused: symlink"; return kBtRefused; }

    if (!reg) {
        if (!have_info) {
            log("niets te doen: " + req.address + " is niet geregistreerd");
            out = "niet geregistreerd: " + req.address;
            return kBtOk;
        }
        if (!bt_info_is_sony(old_info)) {
            log("geweigerd: " + req.address + " is geen Sony-HID-registratie");
            err = "refused: not a Sony controller";
            return kBtRefused;
        }
        BtStopGuard g(env);
        if (!g.stop()) { err = "could not stop bluetooth.service"; return kBtFailed; }
        remove_tree_at(ad.fd, req.address);
        if (cache.fd >= 0) {
            ::unlinkat(cache.fd, req.address.c_str(), 0);
            ::unlinkat(cache.fd, (req.address + ".ravengun-backup").c_str(), 0);
        }
        g.start();
        log("verwijderd: " + req.address + " van adapter " + host);
        out = "verwijderd: " + req.address;
        return kBtOk;
    }

    if (have_info && !bt_info_is_sony(old_info) && !name_is_controller(old_info)) {
        log("geweigerd: " + req.address + " is al geregistreerd als een ander apparaat");
        err = "there is already another device at this address";
        return kBtRefused;
    }
    const std::string key = env.random_key ? env.random_key() : random_key_hex();
    if (!is_32_hex_upper(key)) { log("mislukt: geen koppelsleutel"); err = "could not make a pairing key"; return kBtFailed; }
    const std::string info = bt_info_text(req.type, key);
    const std::string cache_text = bt_cache_text(req.type);

    BtStopGuard g(env);
    if (!g.stop()) { err = "could not stop bluetooth.service"; return kBtFailed; }
    if (dir_kind == 0 && ::mkdirat(ad.fd, req.address.c_str(), 0700) != 0 && errno != EEXIST) { err = req.address + ": " + std::strerror(errno); return kBtFailed; }
    if (dev.fd < 0) dev.fd = open_dir_at(ad.fd, req.address);
    if (dev.fd < 0) { err = req.address + ": " + std::strerror(errno); return kBtFailed; }
    ::fchmod(dev.fd, 0700);
    if (cache.fd < 0) {
        if (::mkdirat(ad.fd, "cache", 0700) != 0 && errno != EEXIST) { err = std::string("cache: ") + std::strerror(errno); return kBtFailed; }
        cache.fd = open_dir_at(ad.fd, "cache");
        if (cache.fd < 0) { err = std::string("cache: ") + std::strerror(errno); return kBtFailed; }
    }
    backup_once(dev.fd, info_name, "info.ravengun-backup");
    if (!write_at(dev.fd, info_name, info, err)) return kBtFailed;
    backup_once(cache.fd, req.address, req.address + ".ravengun-backup");
    if (!write_at(cache.fd, req.address, cache_text, err)) return kBtFailed;
    g.start();
    log("geregistreerd: " + req.address + " (" + req.type + ") op adapter " + host);
    out = "geregistreerd: " + req.address + " (" + bt_device_name(req.type) + ")";
    return kBtOk;
}

}  // namespace ravengun::syshelper
