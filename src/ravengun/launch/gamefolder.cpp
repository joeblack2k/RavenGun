#include "ravengun/launch/gamefolder.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace ravengun::launch {
namespace {

bool dir_exists(const std::string& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

}  // namespace

std::string find_game_folder(const std::string& folder, const std::vector<std::string>& roots) {
    if (folder.empty() || folder.find('/') != std::string::npos) return {};
    for (const std::string& r : roots)
        if (!r.empty() && dir_exists(r + "/" + folder)) return r + "/" + folder;
    for (const std::string& r : roots) {
        if (r.empty()) continue;
        std::vector<std::string> subs;
        std::error_code ec;
        for (const auto& d : fs::directory_iterator(r, fs::directory_options::skip_permission_denied, ec))
            if (d.is_directory(ec)) subs.push_back(d.path().filename().string());
        std::sort(subs.begin(), subs.end());
        for (const std::string& sd : subs)
            if (dir_exists(r + "/" + sd + "/" + folder)) return r + "/" + sd + "/" + folder;
    }
    return {};
}

}  // namespace ravengun::launch
