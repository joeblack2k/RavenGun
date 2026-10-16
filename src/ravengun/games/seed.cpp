#include "ravengun/games/seed.hpp"

#include <filesystem>
#include <map>

namespace ravengun::games {

namespace fs = std::filesystem;

namespace {

// Het doel in de bovenlaag als `real` in een spelmap ligt, anders `real` zelf.
fs::path layer_target(const fs::path& real, const std::vector<LayerMap>& layers) {
    const std::string s = real.string();
    for (const LayerMap& l : layers) {
        if (s.compare(0, l.root.size() + 1, l.root + "/") == 0) return fs::path(l.upper) / s.substr(l.root.size() + 1);
    }
    return real;
}

}  // namespace

std::vector<std::string> seed_files(const std::vector<std::string>& seed_dirs, const std::string& game_dir,
                                    const std::vector<LayerMap>& layers, std::string& err) {
    std::vector<std::string> log;
    err.clear();
    // Relatief pad -> bron; de eerste map (de gebruikerslaag) wint.
    std::map<std::string, fs::path> files;
    for (const std::string& d : seed_dirs) {
        std::error_code ec;
        if (d.empty() || !fs::is_directory(d, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(d, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            files.emplace(fs::relative(it->path(), d, ec).generic_string(), it->path());
        }
    }
    for (const auto& [rel, src] : files) {
        std::error_code ec;
        // De links van een view volgen: de map waar de game het bestand zoekt (op de NAS), zonder dat het bestand moet bestaan.
        const fs::path real = fs::weakly_canonical(fs::path(game_dir) / rel, ec);
        if (ec) { err = "zaaigoed " + rel + ": " + ec.message(); return log; }
        const fs::path dst = layer_target(real, layers);
        if (fs::exists(dst, ec)) { log.push_back("zaaigoed: " + rel + " staat er al (" + dst.string() + ")"); continue; }
        fs::create_directories(dst.parent_path(), ec);
        if (!ec) fs::copy_file(src, dst, fs::copy_options::none, ec);
        if (ec) { err = "zaaigoed " + rel + " naar " + dst.string() + ": " + ec.message(); return log; }
        log.push_back("zaaigoed: " + rel + " -> " + dst.string());
    }
    return log;
}

}  // namespace ravengun::games
