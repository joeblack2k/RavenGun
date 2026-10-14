// SPDX-License-Identifier: GPL-3.0-or-later
// De game goedkoop vinden; zie gamefind.hpp. Tegenhanger in Python: scripts/ownprofile.py (sha256_cached, library_roots).
#include "ravengun/launch/gamefind.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ravengun/json_parse.hpp"
#include "ravengun/launch/native.hpp"

namespace ravengun::launch {

namespace {

constexpr const char* kHeader = "# ravengun sha256-cache v1: sha256, grootte, mtime (ns), pad";

bool stat_of(const std::string& path, long long& size, long long& mtime_ns) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    size = static_cast<long long>(st.st_size);
    mtime_ns = static_cast<long long>(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
    return true;
}

bool is_hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool parse_ll(const std::string& s, long long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    out = std::strtoll(s.c_str(), &end, 10);
    return errno == 0 && end && *end == '\0';
}

std::string env_str(const char* k) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string();
}

}  // namespace

HashCache::HashCache(std::string file, Hasher hasher) : file_(std::move(file)), hasher_(std::move(hasher)) {
    if (!hasher_) hasher_ = [](const std::string& p) { return sha256_file(p); };
}

size_t HashCache::hashed() const {
    std::lock_guard<std::mutex> g(m_);
    return hashed_;
}

// Regels die niet kloppen worden overgeslagen; een ontbrekend bestand is een lege cache.
void HashCache::load_locked() {
    loaded_ = true;
    if (file_.empty()) return;
    std::ifstream f(file_, std::ios::binary);
    std::string line;
    while (f && std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts;
        size_t start = 0;
        for (int i = 0; i < 3; ++i) {
            const size_t tab = line.find('\t', start);
            if (tab == std::string::npos) break;
            parts.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (parts.size() != 3) continue;
        const std::string path = line.substr(start);
        Entry e;
        if (!is_hex64(parts[0]) || !parse_ll(parts[1], e.size) || !parse_ll(parts[2], e.mtime_ns) || path.empty() || path[0] != '/') continue;
        e.sha = parts[0];
        if (!entries_.count(path)) entries_[path] = e;   // wat dit proces zelf al weet, wint
    }
}

// Atomair herschrijven (tijdelijk bestand en rename), met wat een ander proces intussen toevoegde.
void HashCache::save_locked() {
    if (file_.empty()) return;
    {
        HashCache other(file_, hasher_);
        other.load_locked();
        for (auto& [p, e] : other.entries_)
            if (!entries_.count(p)) entries_[p] = e;
    }
    std::error_code ec;
    const std::filesystem::path fp(file_);
    std::filesystem::create_directories(fp.parent_path(), ec);
    const std::string tmp = file_ + ".tmp-" + std::to_string(::getpid());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << kHeader << "\n";
        for (const auto& [p, e] : entries_) f << e.sha << '\t' << e.size << '\t' << e.mtime_ns << '\t' << p << '\n';
        if (!f) { std::filesystem::remove(tmp, ec); return; }
    }
    std::filesystem::rename(tmp, file_, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

std::string HashCache::sha256(const std::string& path) {
    long long size = 0, mtime = 0;
    if (!stat_of(path, size, mtime)) return {};
    // Een pad met een tab of regeleinde past niet in het bestand: dan alleen in het geheugen.
    const bool storable = path.find_first_of("\t\r\n") == std::string::npos && !path.empty() && path[0] == '/';
    {
        std::lock_guard<std::mutex> g(m_);
        if (!loaded_) load_locked();
        auto it = entries_.find(path);
        if (it != entries_.end() && it->second.size == size && it->second.mtime_ns == mtime) return it->second.sha;
    }
    // Hashen buiten het slot: een andere aanroeper hoeft niet op de NAS te wachten.
    const std::string sha = hasher_(path);
    if (sha.empty()) return {};
    long long size2 = 0, mtime2 = 0;
    std::lock_guard<std::mutex> g(m_);
    ++hashed_;
    // Veranderde het bestand tijdens het hashen, dan niet onthouden.
    if (!stat_of(path, size2, mtime2) || size2 != size || mtime2 != mtime) return sha;
    entries_[path] = Entry{size, mtime, sha};
    if (storable) save_locked();
    return sha;
}

std::string default_hash_cache_file() {
    if (const char* v = std::getenv("RAVENGUN_HASH_CACHE")) {
        const std::string s = v;
        if (s.empty() || s == "off") return {};
        return s;
    }
    const std::string xc = env_str("XDG_CACHE_HOME");
    if (!xc.empty() && xc[0] == '/') return xc + "/ravengun/sha256-cache.tsv";
    const std::string home = env_str("HOME");
    if (home.empty() || home[0] != '/') return {};
    return home + "/.cache/ravengun/sha256-cache.tsv";
}

HashCache& default_hash_cache() {
    static HashCache cache(default_hash_cache_file());
    return cache;
}

std::vector<std::string> library_roots(const std::string& library_file, const std::string& profile, const std::string& id_path,
                                       const std::string& home) {
    std::vector<std::string> out;
    if (library_file.empty() || profile.empty()) return out;
    std::ifstream f(library_file, std::ios::binary);
    if (!f) return out;
    std::stringstream ss;
    ss << f.rdbuf();
    const auto r = ravengun::json::parse(ss.str());
    if (!r.ok || !r.value.is_object()) return out;
    const auto* games = r.value.find("games");
    if (!games || !games->is_array()) return out;
    auto norm = [&](std::string p) {
        if (!home.empty() && (p == "~" || p.rfind("~/", 0) == 0)) p = home + p.substr(1);
        std::string n = std::filesystem::path(p).lexically_normal().string();
        while (n.size() > 1 && n.back() == '/') n.pop_back();
        return n;
    };
    auto add = [&](const std::string& p) {
        if (p.empty() || p[0] != '/') return;
        for (const auto& o : out)
            if (o == p) return;
        out.push_back(p);
    };
    for (const auto& g : games->arr) {
        if (!g.is_object()) continue;
        const auto* pr = g.find("profile");
        if (!pr || !pr->is_string() || pr->str != profile) continue;
        if (const auto* fo = g.find("folder"); fo && fo->is_string() && !fo->str.empty()) add(norm(fo->str));
        if (const auto* ex = g.find("exe"); ex && ex->is_string() && !ex->str.empty() && !id_path.empty()) {
            const std::string exe = norm(ex->str), suffix = "/" + id_path;
            if (exe.size() > suffix.size() && exe.compare(exe.size() - suffix.size(), suffix.size(), suffix) == 0)
                add(exe.substr(0, exe.size() - suffix.size()));
        }
    }
    return out;
}

}  // namespace ravengun::launch
