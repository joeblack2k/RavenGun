// SPDX-License-Identifier: GPL-3.0-or-later
// Waar staan de scripts en de packaging-bestanden (de "datamap") van RavenGun? Vaste volgorde, zodat engine en app
// ook zonder de repo werken (installatie onder ~/.local/opt/ravengun, docs/build.md "Installatie-indeling"):
//   1. $RAVENGUN_DATA_DIR (als die een geldige datamap is),
//   2. <prefix>/share/ravengun, met <prefix> = de map boven de map van het programma (bin/..),
//   3. de repo bij ontwikkeling: $RAVENGUN_ROOT, <programmamap>/.. (build/), <programmamap>/../.. , ~/projects/ravengun.
// Een geldige datamap bevat packaging/tp-shm-profiles.json (de gedeelde invoerindelingen). De Qt-app heeft dezelfde volgorde in AppPaths (src/app/core).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace ravengun::paths {

struct Env {
    std::string data_dir;  // $RAVENGUN_DATA_DIR
    std::string root;      // $RAVENGUN_ROOT (repo bij ontwikkeling)
    std::string exe;       // volledig pad van het lopende programma (/proc/self/exe)
    std::string home;      // $HOME
};

enum class Source { None, Env, Prefix, Repo };

struct DataDir {
    std::string path;  // leeg als niets gevonden is
    Source source = Source::None;
    explicit operator bool() const { return !path.empty(); }
};

const char* source_name(Source s);

// packaging/tp-shm-profiles.json aanwezig?
bool looks_like_data_dir(const std::string& dir);

// De kandidaten in volgorde (zonder te controleren of ze bestaan).
std::vector<std::pair<std::string, Source>> data_dir_candidates(const Env& env);

DataDir find_data_dir(const Env& env);

// Env uit het lopende proces (omgevingsvariabelen en /proc/self/exe).
Env env_from_process();

// De map boven de programmamap (bin/..): <prefix>; leeg als exe leeg is.
std::string prefix_of_exe(const std::string& exe);

}  // namespace ravengun::paths
