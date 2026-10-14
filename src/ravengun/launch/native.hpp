#pragma once
// De native route zonder shellscripts. Een profiel uit packaging/native-games.json wordt hier een kant-en-klaar commando
// (env -i -> gamescope -> bwrap -> [rg-elfloader] -> game, zonder shell en zonder tweede start van een script binnen gamescope)
// plus de voorbereiding van de lokale schrijflaag. De daemon voert het uit bij een start uit de app (GameService). Dit vervangt
// scripts/play-native.sh voor die start; het script blijft als benchgereedschap (--seconds, --strace, --gdb, --headless).
//
// Gelijk aan het script: dezelfde mounts (overlay = NAS als lezende onderlaag, schrijfacties in
// <data>/native/<profiel>/<n>/{upper,work}), files, seed en links (alleen als het pad er nog niet is, in de laag noch op de NAS),
// libs (/rg/lib), libdirs, extra_ro, binds_rw, de invoertabellen rt-invoer.ini en rio-invoer.ini (gebruikerslaag voor de
// meegeleverde), de eigen lader, preload alleen in de sandbox, het profiel-env. Anders dan het script: het X-scherm van gamescope is
// bij het plannen nog niet bekend, dus de hele map /tmp/.X11-unix gaat alleen-lezen mee; gamescope start via `env -i` en zijn kind
// is `rg-exec --keep DISPLAY -- bwrap ...`, zodat het spel van gamescope alleen DISPLAY krijgt (geen ENABLE_GAMESCOPE_WSI, vk_*, ...). De camera
// ([Camera] in game.ini) zit hier nog niet in: zo'n game gaat nog via het script (zie GameService).
#include <string>
#include <utility>
#include <vector>

namespace ravengun::launch {

struct NativePaths {
    std::string json;          // packaging/native-games.json
    std::string nas;           // @nas: de map Teknoparrot op de NAS
    std::string tpui;          // @tpui: de lokale TeknoParrotUi-map
    std::string data;          // @data: ~/.local/share/ravengun
    std::string lib32;         // @lib32: @data/runtime/lib32
    std::string build;         // @build: eigen bibliotheken (<prefix>/lib, of build/ in de repo)
    std::string patches;       // @patches: meegeleverde patches/
    std::string user_patches;  // gebruikerslaag van patches/ (leeg = geen)
    std::string loader;        // rg-elfloader (leeg = @data/bin/rg-elfloader)
    std::string runtime_dir;   // XDG_RUNTIME_DIR
    std::string home;          // HOME van de gebruiker (voor gamescope)
    std::string exec_helper;   // rg-exec (tools/rg-exec): tussen gamescope en bwrap, laat alleen DISPLAY van gamescope door
};

struct Mount { std::string mode, src, dst; };
struct SrcDst { std::string src, dst; };

struct NativeProfile {
    std::string name, title, status, note, arch, preload, exe, cwd, home, route, launcher, loader, res;
    bool enabled = false, share_ipc = false;
    std::vector<Mount> mounts;
    std::vector<SrcDst> files, seeds, links /* src = doel van de link */, libdirs, binds_rw;
    std::vector<std::string> libs, extra_ro, args;
    std::vector<std::pair<std::string, std::string>> env;
};

// Leest profiel `game` met de @-paden ingevuld. false + err (Nederlands) bij een fout, een onbekend profiel of een ongeldige waarde.
bool load_profile(const std::string& json_text, const std::string& game, const NativePaths& p, NativeProfile& out, std::string& err);
// Staat dit profiel in het bestand op enabled? (false bij elke fout)
bool profile_enabled(const std::string& json_text, const std::string& game);

struct LayerStep {
    enum class Kind { Mkdir, Seed, Link } kind = Kind::Mkdir;
    std::string from;   // Seed: bronbestand of -map; Link: doel van de koppeling
    std::string to;     // het pad in de schrijflaag (of de map bij Mkdir)
    std::string lower;  // Seed/Link: hetzelfde pad op de NAS (onderlaag); bestaat het daar, dan niets doen
    std::string label;  // het pad in de sandbox, voor het logboek
};

struct PlanOptions {
    std::string res_override;                 // leeg = uit het profiel of de standaard
    std::string wayland_display = "wayland-0";
    std::string host_display;                 // zonder gamescope: DISPLAY van de host (leeg = :0)
    std::string host_xauthority;              // zonder gamescope: XAUTHORITY van de host (leeg = geen)
    int out_w = 1920, out_h = 1080;           // uitvoer van gamescope (de tv)
};

struct NativePlan {
    std::vector<std::string> argv;            // het hele commando, zonder shell
    std::vector<LayerStep> layer;             // op de host, voor het starten
    std::vector<std::string> identity;        // "label=pad" voor het identiteitsblok in het logboek
    std::vector<std::string> notes;           // extra regels voor het logboek
    std::string res;                          // de gekozen resolutie
    std::string comm;                         // naam van het spelproces in /proc/<pid>/comm (hoogstens 15 tekens)
};

bool plan(const NativeProfile& pr, const NativePaths& p, const PlanOptions& o, NativePlan& out, std::string& err);
// Voert de schrijflaag uit. Seed/Link alleen als het pad er nog niet is (laag en NAS). `log` krijgt een regel per stap.
bool apply_layer(const NativePlan& plan, std::vector<std::string>& log, std::string& err);
// Kleine hex-sha256 van een bestand (leeg = niet te lezen).
std::string sha256_file(const std::string& path);
// Het identiteitsblok: per regel "label: pad sha256 <hex>" (of "niet te lezen").
std::vector<std::string> identity_lines(const NativePlan& plan);

}  // namespace ravengun::launch
