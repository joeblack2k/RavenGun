// Tests voor het vinden van de datamap (src/ravengun/paths.*): vaste volgorde RAVENGUN_DATA_DIR, <prefix>/share/ravengun,
// repo bij ontwikkeling. Gebouwd met tijdelijke mappen; raakt niets buiten de tijdelijke map.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "ravengun/paths.hpp"

namespace fs = std::filesystem;
using namespace ravengun::paths;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static void make_data_dir(const fs::path& d) {
    fs::create_directories(d / "packaging");
    std::ofstream(d / "packaging/tp-shm-profiles.json") << "{}\n";
}

int main() {
    char tmpl[] = "/tmp/rg-paths-XXXXXX";
    const char* t = mkdtemp(tmpl);
    if (!t) { std::perror("mkdtemp"); return 2; }
    const fs::path root = t;

    // Geldige datamap of niet.
    const fs::path a = root / "a";
    CHECK(!looks_like_data_dir(a.string()));
    make_data_dir(a);
    CHECK(looks_like_data_dir(a.string()));
    CHECK(!looks_like_data_dir("relatief/pad"));
    CHECK(!looks_like_data_dir(""));
    fs::create_directories(root / "leeg/packaging");
    CHECK(!looks_like_data_dir((root / "leeg").string()));  // packaging/ zonder tp-shm-profiles.json

    // Installatie: <prefix>/bin/ravengund, <prefix>/share/ravengun met scripts en packaging.
    const fs::path prefix = root / "opt/ravengun";
    fs::create_directories(prefix / "bin");
    make_data_dir(prefix / "share/ravengun");
    {
        Env e;
        e.exe = (prefix / "bin/ravengund").string();
        e.home = (root / "geen-home").string();
        const DataDir d = find_data_dir(e);
        CHECK(d && d.path == (prefix / "share/ravengun").string());
        CHECK(d.source == Source::Prefix);
        CHECK(prefix_of_exe(e.exe) == prefix.string());
    }

    // RAVENGUN_DATA_DIR gaat voor de installatie.
    {
        Env e;
        e.exe = (prefix / "bin/ravengund").string();
        e.data_dir = a.string();
        const DataDir d = find_data_dir(e);
        CHECK(d.path == a.string() && d.source == Source::Env);
    }

    // Een ongeldige RAVENGUN_DATA_DIR wordt overgeslagen: dan de installatie.
    {
        Env e;
        e.exe = (prefix / "bin/ravengund").string();
        e.data_dir = (root / "bestaat-niet").string();
        const DataDir d = find_data_dir(e);
        CHECK(d.source == Source::Prefix);
    }

    // Repo bij ontwikkeling: <repo>/build/ravengund en <repo>/scripts, <repo>/packaging.
    const fs::path repo = root / "repo";
    fs::create_directories(repo / "build");
    make_data_dir(repo);
    {
        Env e;
        e.exe = (repo / "build/ravengund").string();
        const DataDir d = find_data_dir(e);
        CHECK(d.path == repo.string() && d.source == Source::Repo);
    }
    // ... ook een build-map een niveau dieper (build/bin/ravengund).
    fs::create_directories(repo / "build/bin");
    {
        Env e;
        e.exe = (repo / "build/bin/ravengund").string();
        CHECK(find_data_dir(e).path == repo.string());
    }
    // De installatie gaat voor de repo als beide bestaan (bin/.. is de prefix, share/ravengun komt eerst).
    {
        const fs::path both = root / "beide";
        fs::create_directories(both / "bin");
        make_data_dir(both);                   // repo-achtig: scripts/ en packaging/ direct onder de prefix
        make_data_dir(both / "share/ravengun");
        Env e;
        e.exe = (both / "bin/ravengun").string();
        const DataDir d = find_data_dir(e);
        CHECK(d.path == (both / "share/ravengun").string() && d.source == Source::Prefix);
    }
    // RAVENGUN_ROOT (ontwikkeling) vindt de repo ook als het programma elders staat; de installatie gaat er wel voor.
    {
        Env e;
        e.exe = (root / "elders/bin/ravengund").string();
        e.root = repo.string();
        CHECK(find_data_dir(e).path == repo.string());
    }
    // ~/projects/ravengun als laatste redmiddel.
    {
        const fs::path home = root / "home";
        make_data_dir(home / "projects/ravengun");
        Env e;
        e.exe = (root / "elders/bin/ravengund").string();
        e.home = home.string();
        const DataDir d = find_data_dir(e);
        CHECK(d.path == (home / "projects/ravengun").string() && d.source == Source::Repo);
    }
    // Niets gevonden.
    {
        Env e;
        e.exe = (root / "niets/bin/ravengund").string();
        e.home = (root / "geen-home").string();
        const DataDir d = find_data_dir(e);
        CHECK(!d && d.path.empty() && d.source == Source::None);
    }
    // Zonder exe en zonder home: alleen de omgevingsvariabelen tellen, en er is geen crash.
    {
        Env e;
        CHECK(!find_data_dir(e));
        e.data_dir = a.string();
        CHECK(find_data_dir(e).path == a.string());
    }
    // De volgorde van de kandidaten.
    {
        Env e;
        e.data_dir = "/d";
        e.root = "/r";
        e.exe = "/p/bin/x";
        e.home = "/h";
        const auto c = data_dir_candidates(e);
        CHECK(c.size() == 5);
        CHECK(c.size() == 5 && c[0].first == "/d" && c[1].first == "/p/share/ravengun" && c[2].first == "/r" &&
              c[3].first == "/p" && c[4].first == "/h/projects/ravengun");
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
