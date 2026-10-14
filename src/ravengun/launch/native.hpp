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
// Een profiel met een blok "lindbergh" is een Sega Lindbergh-game (LindberghSpec hieronder, docs/lindbergh.md).
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
    std::string lindbergh;     // de Lindbergh-laag (scripts/build-lindbergh-loader.sh; leeg = @data/lindbergh-loader/current)
};

struct Mount { std::string mode, src, dst; };
struct SrcDst { std::string src, dst; };

// De Lindbergh-route (docs/lindbergh.md): een Sega Lindbergh-game is een gewone i386-ELF die zonder lader draait, met
// lindbergh.so van lindbergh-loader in LD_PRELOAD (baseboard, EEPROM, dongle, GPU-patches) en de JVS-lijn van het baseboard
// doorgegeven aan het JVS-bord van de daemon (route jvs, de pty van de IO-brug). Het blok "lindbergh" in een profiel:
//   id     de spel-ID die lindbergh.so uit de ELF berekent (CRC32 van 0x4000 bytes vanaf het uitvoerbare segment + 10);
//          plan() rekent hem na op de exe, zodat een ELF die de laag niet kent niet start
//   ini    de instellingen van de laag (lindbergh.ini); de engine zet de vaste sleutels erin (JVS, beeld, libCg) en de
//          gebruikerslaag (patches/<Game>/lindbergh.ini) gaat voor
//   jvs    passthrough (standaard: de JVS-pakketten naar het bord van de daemon) of emulate (lindbergh.so is zelf het bord)
//   libcg  libCg.so 2.0 nodig (de laag compileert de Cg-shaders van de game opnieuw voor Mesa)
struct LindberghSpec {
    bool on = false;
    std::string id, ini, jvs = "passthrough";
    bool libcg = false;
    // librg-stackalign.so (tools/rg-stackalign) VOOR lindbergh.so in LD_PRELOAD: GL/X11/OpenAL-aanroepen van de game met een op
    // 16 bytes uitgelijnde stapel. Nodig als de game op een movaps in een lib32-bibliotheek van SteamOS valt (Too Spicy).
    bool stackalign = false;
};

// Het pad van de JVS-lijn in de sandbox en de plaatsaanduiding in het commando die resolve_jvs() invult.
inline constexpr const char* kLindberghJvsDev = "/dev/ttyRG0";
inline constexpr const char* kJvsPtyPlaceholder = "@jvs-pty";

struct NativeProfile {
    std::string name, title, status, note, arch, preload, exe, cwd, home, route, launcher, loader, res;
    bool enabled = false, share_ipc = false;
    LindberghSpec lindbergh;
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
    enum class Kind { Mkdir, Seed, Link, Write } kind = Kind::Mkdir;
    std::string from;   // Seed: bronbestand of -map; Link: doel van de koppeling; Write: de inhoud
    std::string to;     // het pad in de schrijflaag (of de map bij Mkdir); Write: het bestand op de host (elke start opnieuw)
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
    bool needs_jvs_pty = false;               // het commando bevat kJvsPtyPlaceholder: resolve_jvs() voor het starten
};

bool plan(const NativeProfile& pr, const NativePaths& p, const PlanOptions& o, NativePlan& out, std::string& err);
// Vult de pty van de jvs-route in (na het starten van de route: de daemon maakt de pty dan pas). `link` is de vaste symlink; het
// doel (/dev/pts/N) gaat als apparaat de sandbox in. bwrap kan de symlink zelf niet volgen: na zijn pivot_root zou /dev/pts/N de
// eigen devpts van de sandbox zijn. Niets te doen als het plan geen pty vraagt.
bool resolve_jvs(NativePlan& plan, const std::string& link, std::string& err);

// Lindbergh: de spel-ID zoals lindbergh.so hem berekent ("0x%08x"), leeg als het bestand geen i386-ELF met een uitvoerbaar
// segment op de derde programmakop is. Leest 0x4000 bytes.
std::string lindbergh_game_id(const std::string& elf_path);
// Lindbergh: zet de sleutels `forced` (sectie, sleutel, waarde) in de INI-tekst `base`: een bestaande regel wordt vervangen, een
// ontbrekende komt achteraan in zijn sectie (of in een nieuwe sectie). Commentaar en andere regels blijven staan. Zuiver.
struct IniKey { std::string section, key, value; };
std::string lindbergh_ini(const std::string& base, const std::vector<IniKey>& forced);
// Voert de schrijflaag uit. Seed/Link alleen als het pad er nog niet is (laag en NAS). `log` krijgt een regel per stap.
bool apply_layer(const NativePlan& plan, std::vector<std::string>& log, std::string& err);
// Kleine hex-sha256 van een bestand (leeg = niet te lezen).
std::string sha256_file(const std::string& path);
// Het identiteitsblok: per regel "label: pad sha256 <hex>" (of "niet te lezen").
std::vector<std::string> identity_lines(const NativePlan& plan);

}  // namespace ravengun::launch
