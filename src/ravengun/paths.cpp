// SPDX-License-Identifier: GPL-3.0-or-later
#include "ravengun/paths.hpp"

#include <climits>
#include <cstdlib>
#include <unistd.h>

namespace ravengun::paths {

namespace {
bool abs_path(const std::string& p) { return !p.empty() && p[0] == '/'; }

// Verwijdert de laatste padcomponent; "/a/b/c" -> "/a/b". Blijft er niets over ("/a"), dan leeg.
std::string parent(const std::string& p) {
    size_t sl = p.rfind('/');
    if (sl == std::string::npos || sl == 0) return {};
    return p.substr(0, sl);
}
}  // namespace

const char* source_name(Source s) {
    switch (s) {
        case Source::Env: return "RAVENGUN_DATA_DIR";
        case Source::Prefix: return "installatie (<prefix>/share/ravengun)";
        case Source::Repo: return "repo (ontwikkeling)";
        case Source::None: break;
    }
    return "niet gevonden";
}

bool looks_like_data_dir(const std::string& dir) {
    if (!abs_path(dir)) return false;
    return ::access((dir + "/packaging/tp-shm-profiles.json").c_str(), F_OK) == 0;
}

std::string prefix_of_exe(const std::string& exe) { return parent(parent(exe)); }

std::vector<std::pair<std::string, Source>> data_dir_candidates(const Env& env) {
    std::vector<std::pair<std::string, Source>> c;
    if (!env.data_dir.empty()) c.emplace_back(env.data_dir, Source::Env);
    const std::string prefix = prefix_of_exe(env.exe);
    if (!prefix.empty()) c.emplace_back(prefix + "/share/ravengun", Source::Prefix);
    if (!env.root.empty()) c.emplace_back(env.root, Source::Repo);
    if (!prefix.empty()) {
        c.emplace_back(prefix, Source::Repo);  // build/ravengund -> de repo
        const std::string up = parent(prefix);
        if (!up.empty()) c.emplace_back(up, Source::Repo);
    }
    if (!env.home.empty()) c.emplace_back(env.home + "/projects/ravengun", Source::Repo);
    return c;
}

DataDir find_data_dir(const Env& env) {
    for (const auto& [dir, src] : data_dir_candidates(env))
        if (looks_like_data_dir(dir)) return {dir, src};
    return {};
}

std::string xdg_data_base(const char* xdg_data_home, const std::string& home) {
    if (xdg_data_home && xdg_data_home[0] == '/') return xdg_data_home;
    return home.empty() ? std::string() : home + "/.local/share";
}

Env env_from_process() {
    Env e;
    if (const char* v = std::getenv("RAVENGUN_DATA_DIR")) e.data_dir = v;
    if (const char* v = std::getenv("RAVENGUN_ROOT")) e.root = v;
    if (const char* v = std::getenv("HOME")) e.home = v;
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) e.exe.assign(buf, static_cast<size_t>(n));
    return e;
}

}  // namespace ravengun::paths
