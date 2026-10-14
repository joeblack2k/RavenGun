#pragma once
// De game goedkoop vinden (gamesettings::locate_game): een sha256-cache en de hints uit library.json.
//
// - HashCache: sha256 van een bestand, onthouden per (pad, grootte, mtime) in het geheugen en op schijf in de cachemap van de gebruiker
//   ($XDG_CACHE_HOME/ravengun/sha256-cache.tsv, anders ~/.cache/...; RAVENGUN_HASH_CACHE kiest een ander bestand, "off" = alleen in het
//   geheugen). Een tweede start of lijst hasht dus niets opnieuw van de NAS. Er wordt nooit iets naast het gehashte bestand geschreven.
//   Het bestand deelt hij met scripts/ownprofile.py (zelfde indeling): "<sha256>\t<grootte>\t<mtime in ns>\t<pad>" per regel.
// - library_roots(): de mappen die de app in library.json bij dit profiel heeft gezet ("games": folder en de map van exe), als eerste
//   kandidaten voor locate_game, voordat hij de gamemappen doorzoekt.
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ravengun::launch {

class HashCache {
public:
    using Hasher = std::function<std::string(const std::string& path)>;
    // file leeg = alleen in het geheugen. hasher leeg = sha256_file.
    explicit HashCache(std::string file = {}, Hasher hasher = {});
    // Kleine hex-sha256 van het bestand (symlinks gevolgd); leeg = niet te lezen.
    std::string sha256(const std::string& path);
    // Zo vaak is er echt gehasht (tests en metingen).
    size_t hashed() const;
    const std::string& file() const { return file_; }

private:
    struct Entry {
        long long size = -1;
        long long mtime_ns = 0;
        std::string sha;
    };
    void load_locked();
    void save_locked();
    std::string file_;
    Hasher hasher_;
    mutable std::mutex m_;
    bool loaded_ = false;
    size_t hashed_ = 0;
    std::map<std::string, Entry> entries_;
};

// Het cachebestand volgens de omgeving: RAVENGUN_HASH_CACHE ("off" of leeg-gezet = geen bestand), anders
// $XDG_CACHE_HOME/ravengun/sha256-cache.tsv (absoluut), anders $HOME/.cache/ravengun/sha256-cache.tsv. Leeg = alleen in het geheugen.
std::string default_hash_cache_file();
// De cache van dit proces (daemon, rg-direct), met default_hash_cache_file().
HashCache& default_hash_cache();

// De kandidaatmappen voor `profile` uit library.json ("games": "folder", en de map van "exe" als exe op `/<id_path>` eindigt), in die
// volgorde, zonder dubbele. ~ wordt uitgeklapt met home. Leeg bij een ontbrekend of kapot bestand.
std::vector<std::string> library_roots(const std::string& library_file, const std::string& profile, const std::string& id_path,
                                       const std::string& home);

}  // namespace ravengun::launch
