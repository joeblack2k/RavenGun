// SPDX-License-Identifier: GPL-3.0-or-later
// De directe route (A10) zonder scripts; zie direct.hpp. Elke functie hier heeft een tegenhanger in scripts/launch-direct.py,
// scripts/gamesettings.py of scripts/ownprofile.py met dezelfde naam of een vertaling ervan (in het commentaar genoemd), zodat
// een verschil tussen beide in tests/direct_golden.py naar een plek te herleiden is.
//
// Het werk is over meer bestanden verdeeld: dit bestand heeft het plan (plan_direct); de rest staat in direct_util.cpp (tekst, paden,
// XML, kleine publieke hulpen, joystick-id), direct_profile.cpp (instellingen, profiel, teknoparrot.ini, invoerroute), direct_files.cpp
// (drop-ins, view, tweede exe) en direct_apply.cpp (plan uitvoeren en JSON). Wat ze delen staat in direct_internal.hpp.
#include "ravengun/launch/direct.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <regex>
#include <set>
#include <sstream>

#include "ravengun/iobridge/control_pipe.hpp"
#include "ravengun/launch/amdaemon.hpp"
#include "ravengun/launch/direct_internal.hpp"
#include "ravengun/launch/pe_stack.hpp"

namespace ravengun::launch {

namespace iob = ravengun::iobridge;

using namespace direct_detail;

namespace {

// --- weergave van het scherm (display_size) ----------------------------------------------------------------------------------------
std::pair<int, int> xrandr_size(const EnvMap& env) {
    EnvMap e = environ_map();
    e["DISPLAY"] = getenv_or(env, "DISPLAY", ":0");
    std::string out;
    if (run_capture({"xrandr", "--current"}, e, &out) < 0) return {1920, 1080};
    static const std::regex rx(R"( (\d+)x(\d+)\+\d+\+\d+)");
    std::istringstream is(out);
    std::string line;
    while (std::getline(is, line)) {
        if (line.find(" connected primary ") == std::string::npos && line.find(" connected ") == std::string::npos) continue;
        std::smatch m;
        if (std::regex_search(line, m, rx)) return {std::stoi(m[1]), std::stoi(m[2])};
    }
    return {1920, 1080};
}

}  // namespace

// -----------------------------------------------------------------------------------------------------------------------------
// Het plan (build_plan)

Verdict plan_direct(const std::string& name, const DirectPaths& p, const DirectOptions& o, DirectPlan& pl, std::string& why) {
    pl = DirectPlan{};
    pl.profile = name;
    pl.state = p.state;
    pl.log_full = o.log_full;
    const std::string& tp = p.tp;
    auto unsupported = [&](const std::string& m) { why = "Niet direct te starten: " + m; return Verdict::Fallback; };
    auto engine = [&](const std::string& m) { why = "nog niet in de engine: " + m; return Verdict::Fallback; };
    auto setting_refused = [&](const std::string& m) { why = "Instelling geweigerd: " + m; return Verdict::Refused; };
    auto patch_refused = [&](const std::string& m) { why = "Patch geweigerd: " + m; return Verdict::Refused; };

    // 1. Instellingen (met het oude .env eronder) en het eigen profiel.
    bool legacy_fpfix_file = false;
    const gsn::Game settings = load_for_profile(name, p, &pl.notes, &legacy_fpfix_file);
    for (const auto& w : settings.warnings) pl.notes.push_back("instellingen: " + w);
    if (!gsn::has_own_profile(settings.settings))
        return engine("geen eigen profiel in patches/" + name + "/game.ini (alleen UserProfiles/" + name + ".xml)");
    // Een eigen lader en kern (of geen lader): niets uit de TeknoParrot-map, ook niet lezen (tp_free_settings). Wat een plan met de
    // TeknoParrot-map wel leest of gebruikt, komt in pl.tp_files.
    const bool tp_vrij = tp_free_settings(settings.settings);
    auto tp_bestand = [&](const std::string& rel) {
        if (std::find(pl.tp_files.begin(), pl.tp_files.end(), rel) == pl.tp_files.end()) pl.tp_files.push_back(rel);
    };
    if (!tp_vrij) {
        for (const char* ext : {".xml", ".env"})   // load_for_profile(): de hint voor [Game] Type en het oude .env
            if (isfile(tp + "/UserProfiles/" + name + ext)) tp_bestand("UserProfiles/" + name + ext);
    }
    if (!tp_vrij) {   // load_own_profile(): de revisie van het sjabloon in de runtime
        std::string stock;
        if (read_file(tp + "/GameProfiles/" + name + ".xml", stock)) {
            tp_bestand("GameProfiles/" + name + ".xml");
            const std::string want = xml_tag(strip_bom(stock), "GameProfileRevision"), have = settings.get("Game", "ProfileRevision");
            if (want != have)
                return unsupported("profielrevisie " + (have.empty() ? std::string("(geen)") : have) + " in game.ini wijkt af van het sjabloon (" + want +
                                   "); importeer het profiel opnieuw: scripts/import-tp-profiles.py " + name);
        }
    }
    std::string how;
    std::string game = locate(name, settings.settings, p, &how);
    Profile pr = profile_from_settings(name, settings.settings, game, p);
    pl.game_source = how;
    // ParrotData.xml (de sneltoetsen en de ElfLdr2-opties van TeknoParrotUi): alleen met de TeknoParrot-map. Zonder gelden de
    // standaardwaarden van build_ini (ExitKey 0x1B, PauseKey 0x13), gelijk aan wat ParrotData.xml van 2.0.0.130 hier zet (gemeten 2026-10-17).
    std::map<std::string, std::string> pd;
    if (!tp_vrij) {
        std::string x;
        if (read_file(tp + "/ParrotData.xml", x)) {
            tp_bestand("ParrotData.xml");
            x = strip_bom(x);
            for (const char* k : {"ExitGameKey", "PauseGameKey", "ScoreSubmissionID", "ScoreCollapseGUIKey", "Elfldr2LogToFile", "Elfldr2NetworkAdapterName"}) {
                bool found = false;
                const std::string v = xml_tag(x, k, &found);
                if (found) pd[k] = v;
            }
        }
    }
    const std::string emu = pr.get("EmulationProfile");
    // NamcoMkdx (Mario Kart Arcade GP DX) staat er niet in: TeknoParrotUi heeft daar alleen een opruimstap (oude MK_AGP3_FINAL-processen
    // beeindigen; onze start draait in een eigen unit die bij elke stop alles opruimt). ALL.Net/AMCUS beantwoordt de gesloten kern in
    // het spelproces met [Network] uit teknoparrot.ini; AMAuthd start niemand (GameLaunchArguments.cs, ApplyPerGamePreLaunch).
    static const std::set<std::string> unsupported_emu = {"AfterBurnerClimax", "TaitoTypeXBattleGear", "NamcoMachStorm", "NamcoPokken", "GuiltyGearRE2",
                                                          "GuiltyGearAPM3", "SiN", "ALLSSWDC", "ALLSSCHRONO", "EXVS2", "EXVS2XB",
                                                          "SegaToolsIDZ", "APM3Direct", "Vf5Lindbergh", "Vf5cLindbergh"};
    if (unsupported_emu.count(emu)) return unsupported("EmulationProfile " + emu + " heeft eigen startargumenten in TeknoParrotUi (niet nagebouwd)");
    if (game.empty() || !isfile(game))
        return unsupported("game niet gevonden: '" + game + "' (voeg de map met de game toe als gamemap in de app, of zet [Game] GamePath in je eigen game.ini)");
    // De view ([Launch] View): een game op de NAS draait vanuit een lokale view (alleen bij een start gemaakt; o.make_view).
    {
        std::string verr;
        if (!plan_view(name, settings.settings, game, pr.get("EmulatorType"), p, o.make_view, pl.notes, verr)) return setting_refused(verr);
        if (game != pr.get("GamePath")) {
            pr.s["GamePath"] = game;
            pl.game_source = how + " -> view";
        }
    }
    // Twee exe's (HasTwoExecutables): de tweede start, uit het profiel en game.ini (second_start).
    SecondStart second;
    {
        std::string m;
        const int r = second_start(pr, settings.settings, game, p, second, m);
        if (r == -1) return unsupported(m);
        if (r == -2) return setting_refused(m);
        if (r == 0) second.path.clear();
    }
    const bool have_second = !second.path.empty();
    // resolve_loader()
    const std::string et = pr.get("EmulatorType");
    const bool is64 = ld_truthy(pr.get("Is64Bit", "false"));
    std::string exe_rel = is64 ? "OpenParrotx64/OpenParrotLoader64.exe" : "OpenParrotWin32/OpenParrotLoader.exe", dll_rel;
    if (et == "ElfLdr2") exe_rel = is64 ? "ElfLdr2/x64/BudgieLoader_x64.exe" : "ElfLdr2/BudgieLoader.exe";
    else if (et == "Lindbergh") exe_rel = "TeknoParrot/BudgieLoader.exe";
    else if (et == "N2") exe_rel = "N2/BudgieLoader.exe";
    else if (et == "TeknoMacaw") exe_rel = is64 ? "TeknoParrot/TeknoMacaw64.exe" : "TeknoParrot/TeknoMacaw.exe";
    else if (et == "OpenParrot") dll_rel = is64 ? "OpenParrotx64/OpenParrot64" : "OpenParrotWin32/OpenParrot";
    else if (et == "OpenParrotKonami") exe_rel = "OpenParrotWin32/OpenParrotKonamiLoader.exe";
    else if (et == "TeknoParrot") dll_rel = is64 ? "TeknoParrot/TeknoParrot64" : "TeknoParrot/TeknoParrot";
    else return unsupported("EmulatorType " + et + " wordt niet direct gestart (externe emulator of eigen voorbereiding)");
    const std::string gamedir = dirname(game);
    const bool elf_like = et == "ElfLdr2" || et == "N2" || et == "Lindbergh";
    // De lader uit de TeknoParrot-map (of naast een ELF-game); own_build() hieronder kan hem vervangen. Pas daarna wordt gecontroleerd of
    // de lader en de kern die het plan echt gebruikt er zijn: een eigen build vraagt niets uit de TeknoParrot-map.
    std::string loader = pjoin(tp, exe_rel);
    if (elf_like && !(et == "ElfLdr2" && is64) && isfile(pjoin(gamedir, exe_rel))) loader = pjoin(gamedir, exe_rel);

    // 2. Omgeving, keuzes van het profiel, eigen build, patches.
    EnvMap env = o.base_env;
    // De keuze van de eigenaar in Properties > Options staat als [Config] General/<sleutel> in de GEBRUIKERSLAAG; die wint van een vaste
    // [Game]-regio uit het meegeleverde game.ini (LGI, LGI3D, LGS, OG). Zonder deze regel deed de regio-optie bij die games niets.
    const gsn::Ini user_layer = p.dirs.user.empty() ? gsn::Ini{} : gsn::read_layer(p.dirs.user, name);
    for (const char* key : {"DongleRegion", "PcbRegion"}) {   // apply_profile_choices()
        const std::string want = trim(settings.get("Game", key));
        if (want.empty()) continue;
        if (trim(user_layer.get("Game", key)).empty() && !trim(user_layer.get("Config", std::string("General/") + key)).empty()) continue;
        Field* f = pr.field(key);
        if (!f) return setting_refused("game.ini zet " + std::string(key) + " = " + want + ", maar het profiel " + name + " heeft die optie niet");
        if (std::find(f->options.begin(), f->options.end(), want) == f->options.end())
            return setting_refused("game.ini zet " + std::string(key) + " = " + want + "; het profiel " + name + " biedt alleen: " +
                                   (f->options.empty() ? std::string("(geen keuzelijst)") : join(f->options, ", ")));
        f->value = want;
    }
    const EnvMap prof_env = to_env(settings.settings, gamedir, p);
    for (const auto& kv : prof_env) env[kv.first] = kv.second;
    // Welke patches staan aan (legacy RG_LOADER_FPFIX zet de gedeelde lader-patch aan; de eigen lader zet hem weer uit).
    static const std::string kFpfix = "OpenParrotLoader zonder CONTEXT_FLOATING_POINT";
    std::map<std::string, bool> enabled;
    for (const auto& pt : settings.patches) enabled[pt.name] = pt.enabled();
    if ((legacy_fpfix_file || gsn::truthy(getenv_or(env, "RG_LOADER_FPFIX", "0"))) && enabled.count(kFpfix)) enabled[kFpfix] = true;
    // own_build(): [Loader] Build/Core/Path. Core = none: geen lader en geen kern, Wine start de exe zelf (een game zonder
    // TeknoParrot-profiel, zoals Mighty Moto: de TeknoParrot-kern stopt dan met exitcode 57 en OpenParrot met "Unsupported Executable").
    std::string core_dir = tp;
    bool zonder_lader = false;
    {
        const gsn::Section* ls = settings.settings.section("Loader");
        auto lget = [&](const char* k) { return ls ? kv_get(ls->kv, k) : std::string(); };
        std::string build = lower(trim(lget("Build"))), core = lower(trim(lget("Core")));
        for (const auto& [key, val] : {std::pair<std::string, std::string>{"Build", build}, {"Core", core}})
            if (!val.empty() && val != "teknoparrot" && val != "ravengun" && !(key == "Core" && val == "none"))
                return setting_refused("[Loader] " + key + " = " + val + (key == "Core" ? " (kies teknoparrot, ravengun of none)" : " (kies teknoparrot of ravengun)"));
        if (core == "none") {
            zonder_lader = true;
            // Zonder lader is er niemand die de game in zijn eigen map zet: Wine start de exe in de werkmap van het proces. Die is dan
            // de map van de exe (cwd hieronder: geen lader of kern uit de TeknoParrot-map), zoals een dubbelklik in Windows.
            dll_rel.clear();
            core.clear();
            if (enabled.count(kFpfix)) enabled[kFpfix] = false;   // die patch hoort bij de lader
        }
        if (core.empty() && build == "ravengun" && et == "OpenParrot") core = "ravengun";
        if (build == "ravengun" || core == "ravengun") {
            const std::string pth = trim(lget("Path"));
            const std::string base = pth.empty() ? pjoin(p.data, "openparrot/current") : expand_home(pth, p.home);
            std::string own_loader, own_core_rel;
            const bool op_loader = lower(basename(exe_rel)).starts_with("openparrotloader");
            if (build == "ravengun") {
                if (!op_loader) return setting_refused("[Loader] Build = ravengun bestaat alleen voor OpenParrotLoader(64).exe, niet voor " + exe_rel);
                own_loader = pjoin(base, exe_rel);
            }
            if (core == "ravengun") {
                if ((et != "OpenParrot" && et != "TeknoParrot") || dll_rel.empty())
                    return setting_refused("een eigen kern bestaat alleen voor EmulatorType OpenParrot of TeknoParrot (dit is " + et + ")");
                if (!op_loader) return setting_refused("[Loader] Core = ravengun past alleen bij OpenParrotLoader(64).exe, niet bij " + exe_rel);
                own_core_rel = et == "OpenParrot" ? dll_rel : (is64 ? "OpenParrotx64/OpenParrot64" : "OpenParrotWin32/OpenParrot");
            }
            for (const std::string& f : {own_loader, own_core_rel.empty() ? std::string() : pjoin(base, own_core_rel + ".dll")})
                if (!f.empty() && !isfile(f)) return setting_refused("eigen OpenParrot-build ontbreekt: " + f + " (bouwen: scripts/build-openparrot.sh)");
            if (!own_loader.empty()) {
                loader = own_loader;
                if (enabled.count(kFpfix)) enabled[kFpfix] = false;   // de Wine-fix zit in de eigen lader zelf
            }
            if (!own_core_rel.empty()) {
                core_dir = base;
                dll_rel = own_core_rel;
            }
        }
    }
    // De lader en de kern die het plan gebruikt moeten er zijn; uit de TeknoParrot-map alleen wat daar echt vandaan komt.
    if (zonder_lader) loader.clear();
    // [Loader] Exe (8b hieronder): een eigen lader die als drop-in meekomt vervangt deze; dan hoeft die er niet te zijn en komt er
    // niets uit de TeknoParrot-map. Alleen voor een lader zonder kern-DLL (TeknoMacaw) en niet samen met Core = none.
    const std::string eigen_lader = zonder_lader ? std::string() : trim(settings.get("Loader", "Exe"));
    if (!eigen_lader.empty() && et != "TeknoMacaw")
        return setting_refused("[Loader] Exe = " + eigen_lader + ": een eigen lader als drop-in kan alleen bij EmulatorType TeknoMacaw (dit profiel: " + et + ")");
    const bool lader_tp = eigen_lader.empty() && !tp.empty() && !loader.empty() && binnen(abspath(loader), abspath(tp));
    if (eigen_lader.empty() && !loader.empty() && !isfile(loader))
        return unsupported("lader ontbreekt: " + loader + (lader_tp ? " (uit de TeknoParrot-map)" : ""));
    if (lader_tp) tp_bestand(relpath(abspath(loader), abspath(tp)));
    if (!dll_rel.empty()) {
        pl.core_dll = pjoin(core_dir, dll_rel + ".dll");
        pl.core_source = core_dir == tp ? "runtime" : "ravengun";
        if (!isfile(pl.core_dll))
            return unsupported("kern ontbreekt: " + pl.core_dll + (core_dir == tp ? " (de kern van TeknoParrot; een eigen kern: [Loader] Core = ravengun)" : ""));
        if (core_dir == tp) tp_bestand(dll_rel + ".dll");
    }
    // De werkmap van de lader: bij een ELF-lader de map van de game; bij een lader of kern uit de TeknoParrot-map die map (zoals
    // TeknoParrotUi); anders ook de map van de game. De game zelf krijgt van OpenParrotLoader altijd zijn eigen map (of
    // TP_GAME_WORKING_DIRECTORY); de werkmap van de lader telt alleen voor FFBBlaster/ (TeknoParrot, niet voor lightgun-games).
    const bool lader_of_kern_tp = lader_tp || (!dll_rel.empty() && core_dir == tp);
    const std::string cwd = elf_like || !lader_of_kern_tp ? gamedir : tp;
    // plan_file_patches(): een afgewezen patch die aan staat weigert; een ingeschakelde patch past de engine (nog) niet toe.
    for (const auto& e : settings.patch_errors)
        if (e.enabled) return patch_refused(e.message);
    for (const auto& e : settings.patch_errors) pl.patch_lines.push_back("overgeslagen '" + e.name + "' @- [afgewezen: " + e.message + "]");
    for (const auto& pt : settings.patches) {
        if (enabled[pt.name]) return engine("patch '" + pt.name + "' staat aan; de engine past dit soort patch nog niet toe");
        const std::string plek = pt.kind() == "memory" ? (pt.get("Module").empty() ? "?" : pt.get("Module")) + "+" + (pt.get("Offset").empty() ? "?" : pt.get("Offset"))
                                                       : (pt.get("Target").empty() ? "-" : pt.get("Target"));
        pl.patch_lines.push_back("overgeslagen '" + pt.name + "' @" + plek + " [uit]");
    }
    if (!trim(settings.get("Game", "ExeSha256")).empty()) return engine("[Game] ExeSha256 (de versiecontrole van de patchmotor)");

    // 3. Wine en de prefix.
    std::string wine = pr.get("WineRunnerPath");
    if (!(!wine.empty() && isfile(wine))) wine = isfile(getenv_or(env, "TP_WINE")) ? getenv_or(env, "TP_WINE") : p.data + "/wine/proton-11.0-2/bin/wine";
    {
        const std::string bindir = dirname(wine);
        if (basename(dirname(bindir)) == "files" && isfile(pjoin(dirname(dirname(bindir)), "proton")))
            return unsupported("Proton-runner (" + wine + ") wordt niet direct gestart; gebruik plain Wine");
    }
    std::string prefix;
    bool set_prefix = true;
    if (basename(wine) == "wine-prefix") {
        prefix = getenv_or(env, "RG_WINEPREFIX");
        if (prefix.empty()) return unsupported("WineRunnerPath is de wrapper wine-prefix maar RG_WINEPREFIX ontbreekt in " + name + ".env");
        set_prefix = false;
    } else if (!getenv_or(env, "TP_WINEPREFIX").empty()) {
        prefix = getenv_or(env, "TP_WINEPREFIX");
    } else {
        const std::string mode = pr.get("WinePrefixMode"), legacy = pjoin(prefixes_dir(p), name);
        prefix = mode == "Isolated" || (mode.empty() && isfile(pjoin(legacy, "system.reg"))) ? legacy : shared_prefix(p);
    }

    // 4. Wat de launcher zelf in de omgeving zet.
    const bool windowed = std::any_of(pr.fields.begin(), pr.fields.end(), [](const Field& f) {
        return (f.name == "Windowed" && f.value == "1") || (f.name == "DisplayMode" && f.value == "Windowed");
    });
    EnvMap launch;
    if (ld_truthy(pr.get("UseRemoteThread", "false"))) launch["TP_REMOTETHREAD"] = "1";
    {
        long long ms = 0;
        const std::string mt = pr.get("msysType", "0");
        if (py_int(mt.empty() ? "0" : mt, ms) && ms > 0) launch["tp_msysType"] = pr.get("msysType");
    }
    auto has_field_value = [&](const char* n, const char* v) {
        return std::any_of(pr.fields.begin(), pr.fields.end(), [&](const Field& f) { return f.name == n && f.value == v; });
    };
    if (et == "N2" || et == "ElfLdr2") {
        launch["tp_windowed"] = windowed ? "1" : "0";
        launch["TP_LOGTOFILE"] = (o.log_full || ld_truthy(pd.count("Elfldr2LogToFile") ? pd["Elfldr2LogToFile"] : "false")) ? "1" : "0";
        if (!pd["Elfldr2NetworkAdapterName"].empty()) launch["TP_ETH"] = pd["Elfldr2NetworkAdapterName"];
        if (const Field* m = pr.field("MSAA Level")) launch["TP_MSAA"] = m->value;
        if (emu == "Vt3Lindbergh") launch["TEA_DIR"] = wine_path(dirname(gamedir)) + "\\";
        else if (emu == "SegaJvsLetsGoJungle") launch["TEA_DIR"] = wine_path(gamedir) + "\\";
    } else if (et == "Lindbergh") {
        static const std::set<std::string> tea = {"SegaInitialDLindbergh", "Vf5Lindbergh", "Vf5cLindbergh", "SegaRtv", "SegaJvsLetsGoJungle", "Rambo",
                                                  "TooSpicy", "SegaRTuned", "GSEVO", "HummerExtreme"};
        if (windowed) launch["tp_windowed"] = "1";
        if (tea.count(emu)) launch["TEA_DIR"] = wine_path(gamedir) + "\\";
        else if (emu == "Vt3Lindbergh") launch["TEA_DIR"] = wine_path(dirname(gamedir)) + "\\";
        if (has_field_value("EnableAmdFix", "1")) {
            launch["tp_AMDCGGL"] = "1";
            if (emu == "SegaInitialDLindbergh") launch["tp_D4AMDFix"] = "1";
        }
        launch["REGAL_LOAD_GL"] = "opengl32.dll";
    }
    static const std::set<std::string> openssl = {"ALLSSWDC", "IDZ", "ALLSSCHRONO", "NxL2", "RawThrillsFNF", "ALLSHOTDSD", "ALLSFGO", "TimeCrisis5",
                                                  "JojoLastSurvivor", "DenshaDeGo", "ALLSIDTA", "SegaOlympic2020"};
    if (openssl.count(emu)) launch["OPENSSL_ia32cap"] = ":~0x20000000";
    if (set_prefix) launch["WINEPREFIX"] = prefix;
    if (!launch.count("TP_REMOTETHREAD")) launch["TP_REMOTETHREAD"] = getenv_or(env, "TP_REMOTETHREAD", "1");
    {
        const std::string rt = lower(launch["TP_REMOTETHREAD"]);
        if (rt == "off" || rt == "0" || rt == "no" || rt == "false" || rt.empty()) {
            launch.erase("TP_REMOTETHREAD");
            env.erase("TP_REMOTETHREAD");
        }
    }
    if (o.log_full) launch["WINEDEBUG"] = getenv_or(env, "RG_WINEDEBUG_FULL", "-all,+debugstr,err+module");
    else if (!env.count("WINEDEBUG")) launch["WINEDEBUG"] = "-all";
    launch["TP_LAUNCH_SESSION_ID"] = o.session_id.empty() ? new_uuid4() : o.session_id;
    // Geluid: Wine's DirectSound (proton-11.0-2) mengt niet goed naar 8 kanalen. Boven DS_MAX_CHANNELS (6) slaat het volume,
    // pan en 3D over, en een mono-buffer schrijft alleen kanaal 0: de andere 7 houden oude inhoud van tmp_buffer, die elke
    // mixperiode terugkomt (een zoem bij elk mono-effect; Silent Hill: The Arcade op de 7.1-HDMI-uitgang, 2026-10-15).
    // winepulse bepaalt het apparaatformaat met een stream met FIX_CHANNELS; pipewire-pulse geeft die stream de indeling uit
    // de clienteigenschap pulse.fix.position, die libpulse uit PULSE_PROP haalt. [Audio] Channels = stereo (standaard) | 5.1 | native.
    if (!env.count("PULSE_PROP")) {
        const gsn::Section* audio = settings.settings.section("Audio");
        const std::string ch = lower(trim(audio ? kv_get(audio->kv, "Channels") : std::string()));
        if (ch == "5.1") launch["PULSE_PROP"] = "pulse.fix.position='[ FL FR FC LFE RL RR ]'";
        else if (ch != "native") launch["PULSE_PROP"] = "pulse.fix.position='[ FL FR ]'";
    }
    for (const auto& kv : launch) env[kv.first] = kv.second;

    // 5. De opdrachtregel.
    const gsn::Section* launch_ini = settings.settings.section("Launch");
    auto lini = [&](const char* k) { return launch_ini ? kv_get(launch_ini->kv, k) : std::string(); };
    // [Launch] Drives = W=.; Y=~/map: schijfletters voor de duur van de run; een relatief pad is relatief aan de view van het spel.
    // [Launch] Exe = W:\spel.exe: het pad dat de lader voor het spel krijgt (alleen dat pad verandert). Far Cry Paradise Lost
    // controleert in WinMain of hij vanaf W: draait (Runme.bat: subst W: .), 2026-10-15. Zie docs/per-game-patches.md.
    const std::string view_root = view_wortel(dirname(game), p);
    for (std::string stuk : nonempty_split(lini("Drives"), ';')) {
        stuk = trim(stuk);
        if (stuk.empty()) continue;
        const size_t is = stuk.find('=');
        if (is == std::string::npos) return setting_refused("schijfletter '" + stuk + "' is geen LETTER=MAP");
        std::string letter = upper(trim(stuk.substr(0, is)));
        // C:\PC\ALIENS=. : een map op C: (de prefix) die de game met een vast pad opent, voor de duur van de run een koppeling naar
        // een lokale map. Aliens Extermination laadt zijn shaders uit C:\PC\ALIENS\SHADERS\%s (2026-10-17); de TP-kern leidde dat om.
        std::string cmap;
        if (letter.size() > 3 && letter[0] == 'C' && letter[1] == ':' && (letter[2] == '\\' || letter[2] == '/')) {
            cmap = trim(stuk.substr(3, is - 3));
            std::replace(cmap.begin(), cmap.end(), '\\', '/');
            while (!cmap.empty() && cmap.back() == '/') cmap.pop_back();
            if (cmap.empty() || cmap.find("..") != std::string::npos || cmap[0] == '/')
                return setting_refused("map '" + trim(stuk.substr(0, is)) + "' kan niet (een map onder C:\\, zonder ..)");
            letter = "C:\\" + cmap;
            std::replace(letter.begin(), letter.end(), '/', '\\');
        } else if (!letter.empty() && letter.back() == ':') letter.pop_back();
        if (cmap.empty() && (letter.size() != 1 || letter[0] < 'A' || letter[0] > 'Y' || letter == "C"))
            return setting_refused("schijfletter '" + trim(stuk.substr(0, is)) + "' kan niet (een letter van A tot Y, niet C)");
        std::string doel = trim(stuk.substr(is + 1));
        if (doel.starts_with("~/")) doel = p.home + doel.substr(1);
        doel = normpath(doel.size() && doel[0] == '/' ? doel : pjoin(view_root, doel));
        if (!isdir(doel)) {
            // Een map ONDER de view die er nog niet is (Point Blank X: F=TeknoParrot/F, de map waar de schijfletter-omleiding van de kern
            // naartoe wijst) wordt bij een echte start gemaakt; in --plan blijft het bij een melding. Elders of op de NAS: weigeren.
            const bool onder_view = abspath(doel) != abspath(view_root) && binnen(abspath(doel), abspath(view_root)) && !on_nas(doel, p);
            if (!onder_view) return setting_refused("schijf " + letter + ": de map " + doel + " bestaat niet");
            if (o.make_view) {
                std::error_code mec;
                fs::create_directories(doel, mec);
                if (!isdir(doel)) return setting_refused("schijf " + letter + ": de map " + doel + " kon niet gemaakt worden: " + mec.message());
            } else {
                pl.notes.push_back("schijf " + letter + ": de map " + doel + " bestaat nog niet; hij wordt bij de start gemaakt");
            }
        }
        if (isdir(doel) && on_nas(doel, p)) return setting_refused("schijf " + letter + ": de map " + doel + " ligt op de NAS; een schijfletter van ons wijst naar een lokale map (de view van het spel, of een eigen map)");
        DirectDrive d;
        d.letter = letter;
        d.target = doel;
        d.link = cmap.empty() ? pjoin(pjoin(prefix, "dosdevices"), lower(letter) + ":") : pjoin(pjoin(prefix, "drive_c"), cmap);
        if (!cmap.empty() && !isdir(pjoin(prefix, "drive_c")))
            return setting_refused("map " + letter + ": de prefix " + prefix + " heeft geen drive_c (nog niet aangemaakt; start het spel een keer zonder)");
        if (cmap.empty() && !isdir(pjoin(prefix, "dosdevices"))) return setting_refused("schijf " + letter + ": de prefix " + prefix + " heeft geen dosdevices-map (nog niet aangemaakt; start het spel een keer zonder schijfletters)");
        if (lexists(d.link)) {
            if (!islink(d.link) || realpath_(d.link) != realpath_(doel))
                return setting_refused("schijf " + letter + ": bestaat al in de prefix " + prefix + " en wijst naar " + realpath_(d.link) + ", niet naar " + doel);
            d.present = true;
        }
        pl.drives.push_back(d);
    }
    if (std::string ex = trim(lini("Exe")); !ex.empty()) {
        if (ex.size() >= 2 && ex.front() == '"' && ex.back() == '"') ex = ex.substr(1, ex.size() - 2);
        if (ex.size() < 3 || ex[1] != ':' || std::toupper(static_cast<unsigned char>(ex[0])) == 'Z')
            return setting_refused("[Launch] Exe = " + ex + " (een Windows-pad met een schijfletter uit [Launch] Drives, bv. W:\\spel.exe)");
        const std::string letter(1, static_cast<char>(std::toupper(static_cast<unsigned char>(ex[0]))));
        auto it = std::find_if(pl.drives.begin(), pl.drives.end(), [&](const DirectDrive& d) { return d.letter == letter; });
        if (it == pl.drives.end()) return setting_refused("--exe " + ex + ": schijf " + letter + ": bestaat niet in de prefix " + prefix + "; geef hem op met --drive " + letter + "=<map> of [Launch] Drives (bijvoorbeeld " + letter + "=. voor de view van het spel)");
        std::string rest = ex.substr(2);
        std::replace(rest.begin(), rest.end(), '\\', '/');
        // Zoeken zoals Windows: hoofdletters maken niet uit.
        std::string cur = it->target;
        for (const std::string& deel : nonempty_split(rest, '/')) {
            if (deel == ".") continue;
            if (deel == "..") return setting_refused("[Launch] Exe = " + ex + " (geen ..)");
            std::string kand = pjoin(cur, deel);
            if (!lexists(kand)) {
                std::error_code ec;
                std::string gevonden;
                for (const auto& e : fs::directory_iterator(cur, ec))
                    if (lower(e.path().filename().string()) == lower(deel)) gevonden = e.path().string();
                if (gevonden.empty()) return setting_refused("--exe " + ex + ": dat bestand bestaat niet");
                kand = gevonden;
            }
            cur = kand;
        }
        if (!isfile(cur)) return setting_refused("--exe " + ex + ": dat bestand bestaat niet");
        if (!binnen(abspath(cur), abspath(view_root)))
            return setting_refused("--exe " + ex + ": " + cur + " ligt buiten de view " + view_root + " van het spel; de override verandert alleen het pad, niet de opstelling");
        std::string door = ex;
        door[0] = letter[0];
        std::replace(door.begin(), door.end(), '/', '\\');
        pl.exe_override = door;
        pl.notes.push_back("spel (OVERRIDE, [Launch] Exe): " + door + " (" + cur + ")");
    }
    for (const auto& d : pl.drives)
        pl.notes.push_back("schijfletter " + d.letter + ": -> " + d.target + (d.present ? " (staat al in de prefix)" : " (voor deze run gezet, daarna weggehaald)"));
    const std::string spel_win = pl.exe_override.empty() ? wine_path(game) : pl.exe_override;
    if (zonder_lader) {
        loader.clear();
        pl.notes.push_back("[Loader] Core = none: Wine start de exe zelf, zonder lader en kern");
    }
    std::vector<std::string> args{wine};
    if (!zonder_lader) args.push_back(loader);
    if (!dll_rel.empty()) args.push_back(wine_path(pjoin(core_dir, dll_rel)));
    args.push_back(spel_win);
    std::vector<std::string> profiel_delen;
    for (const char* veld : {"CustomArguments", "ExtraParameters"}) {
        const std::string extra = pr.get(veld);
        if (trim(extra).empty()) continue;
        std::string dbl;
        for (char c : extra) { dbl += c; if (c == '\\') dbl += '\\'; }
        bool ok = true;
        const auto delen = shlex_split(dbl, &ok);
        if (!ok) return engine(std::string(veld) + " is niet op te delen (geen sluitend aanhalingsteken)");
        profiel_delen.insert(profiel_delen.end(), delen.begin(), delen.end());
    }
    const std::vector<std::string> basis(args.begin(), args.end() - 1);   // wine, lader, kern (of alleen wine): de tweede exe krijgt dezelfde
    {
        const auto spel_args = spel_argumenten(loader, profiel_delen, lini("Arguments"));
        args.insert(args.end(), spel_args.begin(), spel_args.end());
    }
    // De tweede exe krijgt dezelfde wine, dezelfde lader en dezelfde kern als de eerste: anders haakt de kern er niet aan.
    std::vector<std::string> args2;
    if (have_second) {
        std::vector<std::string> delen2;
        if (!trim(second.profile_args).empty()) {
            std::string dbl;
            for (char c : second.profile_args) { dbl += c; if (c == '\\') dbl += '\\'; }
            bool ok = true;
            delen2 = shlex_split(dbl, &ok);
            if (!ok) return engine("SecondExecutableArguments is niet op te delen (geen sluitend aanhalingsteken)");
        }
        args2 = basis;
        args2.push_back(wine_path(second.path));
        const auto a2 = spel_argumenten(loader, delen2, second.own_args);
        args2.insert(args2.end(), a2.begin(), a2.end());
    }
    std::vector<std::string> gamescope;
    if (getenv_or(env, "TP_GAMESCOPE") == "1" && getenv_or(env, "TP_NO_GAMESCOPE") != "1") {
        std::pair<int, int> wh{1920, 1080};
        static const std::regex res(R"(^(\d{1,5})x(\d{1,5})$)");   // hoogstens 5 cijfers: std::stoi mag niet overlopen
        std::smatch m;
        const std::string rv = getenv_or(env, "RG_GAMESCOPE_RES");
        if (std::regex_match(rv, m, res)) wh = {std::stoi(m[1]), std::stoi(m[2])};
        else wh = o.display_size ? o.display_size(env) : xrandr_size(env);
        std::string gs_bin = getenv_or(env, "TP_GAMESCOPE_PATH");
        if (gs_bin.empty()) gs_bin = "/usr/bin/gamescope";
        std::string backend = getenv_or(env, "TP_GAMESCOPE_BACKEND");
        if (backend.empty()) backend = getenv_or(env, "WAYLAND_DISPLAY").empty() ? "" : "wayland";
        // [Display] GamescopeScale (RG_GAMESCOPE_SCALE): fit (standaard, beeldverhouding van het spel), stretch (schermvullend,
        // voor een spel dat in 4:3 rendert maar met een 16:9-camera), fill, integer, auto.
        static const std::set<std::string> scales = {"auto", "integer", "fit", "fill", "stretch"};
        std::string scale = lower(trim(getenv_or(env, "RG_GAMESCOPE_SCALE")));
        if (!scales.count(scale)) scale = "fit";
        gamescope = {gs_bin, "-W", std::to_string(wh.first), "-H", std::to_string(wh.second), "-S", scale, "-f"};
        // [Display] GamescopeGameResolution (RG_GAMESCOPE_GAME_RES): de interne resolutie (-w/-h). De Xwayland van gamescope biedt
        // dan precies die modus; een game die een vaste modus vraagt (Taito Type X2: 1360x768) krijgt zijn Direct3D-apparaat, en
        // gamescope schaalt op naar de tv (-W/-H). Zonder: de modus van de tv.
        const std::string game_res = getenv_or(env, "RG_GAMESCOPE_GAME_RES");
        std::smatch gm;
        if (std::regex_match(game_res, gm, res)) gamescope.insert(gamescope.end(), {"-w", gm[1].str(), "-h", gm[2].str()});
        if (!backend.empty()) gamescope.insert(gamescope.end(), {"--backend", backend});
        gamescope.push_back("--");
    }
    const Field* wf = pr.field("Windowed");
    pl.windowed_override = !gamescope.empty() && pr.get("GamescopeGameWindowCompatibility") == "RequireWindowed" && wf && wf->value == "0";

    // 6. Bestanden naast de game.
    const std::string ini = build_ini(pr, pd, pl.windowed_override);
    pl.files.emplace_back(pjoin(gamedir, "teknoparrot.ini"), ini);
    if (et == "TeknoMacaw" && basename(gamedir) == "modules") pl.files.emplace_back(pjoin(dirname(gamedir), "teknoparrot.ini"), ini);
    {
        std::string tn, tt;
        if (taito_gun_ini(pr, tn, tt)) pl.files.emplace_back(pjoin(gamedir, tn), tt);
    }
    // De kern leest teknoparrot.ini naast de exe waarin hij geladen is, dus ook naast de tweede (TeknoParrotUi schrijft daar hetzelfde).
    if (have_second && dirname(second.path) != gamedir) pl.files.emplace_back(pjoin(dirname(second.path), "teknoparrot.ini"), ini);

    // 6b. [Launch] StackReserve: de hoofdthread een grotere stapel geven via een lokale kopie van de exe in de view (pe_stack.hpp).
    if (const std::string sr = trim(lini("StackReserve")); !sr.empty()) {
        uint64_t n = 0;
        if (!parse_stack_size(sr, n) || n < kStackReserveMin || n > kStackReserveMax)
            return setting_refused("[Launch] StackReserve = " + sr + " (een grootte van 64K tot en met 256M, bijvoorbeeld 16M)");
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] StackReserve vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): de exe "
                                   "met de grotere stapel wordt een lokaal bestand in de view, nooit op de NAS; " + game + " ligt daar niet");
        const std::string orig = game + kStackOrigSuffix;
        pl.stack_reserve = static_cast<long long>(n);
        pl.stack_dest = game;
        pl.stack_src = islink(orig) ? realpath_(orig) : islink(game) ? realpath_(game) : game;
    }
    // 6b2. [Launch] PeFillPages = true: dezelfde lokale kopie, met secties tot hun bladzijde (pe_fill_section_pages). Wine wist de bytes
    // na SizeOfRawData; Aliens: Extermination heeft daar zijn HASP-emulatie (page fault op 0x456DF1 onder Wine, 2026-10-17).
    if (const std::string pf = lower(trim(lini("PeFillPages"))); !pf.empty() && pf != "false" && pf != "0" && pf != "off") {
        if (pf != "true" && pf != "1" && pf != "on") return setting_refused("[Launch] PeFillPages = " + lini("PeFillPages") + " (true of false)");
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] PeFillPages vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): de aangepaste "
                                   "exe wordt een lokaal bestand in de view, nooit op de NAS; " + game + " ligt daar niet");
        const std::string orig = game + kStackOrigSuffix;
        pl.pe_fill_pages = true;
        pl.stack_dest = game;
        pl.stack_src = islink(orig) ? realpath_(orig) : islink(game) ? realpath_(game) : game;
    }
    // 6c. [Launch] ViewLinks: een bestand in de view wordt een symlink naar een ander bestand van dezelfde game (alleen in de view;
    // Mario Kart DX: Polygon2D_D3D10.fx -> de meegeleverde voorgecompileerde Polygon2D_D3D10.cfx, NETWERK-INVOER-CAMERA.md 5d).
    if (const std::string vl = trim(lini("ViewLinks")); !vl.empty()) {
        std::string verr;
        // De map van de game zelf (op de NAS), voor een doel "origineel:<pad>"; zoals mod_game_root hieronder.
        const std::string orig_exe = game + kStackOrigSuffix;
        const std::string orig_base = dirname(islink(orig_exe) ? realpath_(orig_exe) : realpath_(game));
        if (!parse_view_links(vl, dirname(game), pl.view_links, verr, orig_base))
            return setting_refused("[Launch] ViewLinks = " + vl + " (" + verr + ")");
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] ViewLinks vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): de links "
                                   "komen alleen in de view, nooit op de NAS; " + game + " ligt daar niet");
    }
    // 6c2. [Launch] ViewCopies: bestanden die de game zelf beschrijft (records, instellingen) worden in de view één keer een eigen
    // lokale kopie van het origineel, zodat de game nooit naar de NAS schrijft. Mars Sortie schreef GameRecords1/2.dat via de
    // view-link op de NAS (2026-10-15). Een bestaand lokaal bestand blijft staan: dat is de stand van de game.
    if (const std::string vc = trim(lini("ViewCopies")); !vc.empty()) {
        for (std::string r : nonempty_split(vc, ',')) {
            r = trim(r);
            std::replace(r.begin(), r.end(), '\\', '/');
            const std::string n = normpath(r);
            if (r.empty() || r[0] == '/' || n == "." || n == ".." || n.starts_with("../"))
                return setting_refused("[Launch] ViewCopies = " + vc + " ('" + r + "': een pad binnen de gamemap, relatief en zonder ..)");
            pl.view_copies.push_back(normpath(pjoin(dirname(game), n)));
        }
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] ViewCopies vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): de kopieën "
                                   "komen alleen in de view, nooit op de NAS; " + game + " ligt daar niet");
    }
    // 6c2b. [Launch] ViewWritable: mappen waar de game zelf in schrijft. Ze worden in de view een
    // echte map met een kopie van de NAS-inhoud; de game schrijft dus nooit naar de NAS. Verschil
    // met ViewCopies: dat gaat over losse bestanden, dit over hele mappen.
    if (const std::string vw = trim(lini("ViewWritable")); !vw.empty()) {
        for (std::string r : nonempty_split(vw, ',')) {
            r = trim(r);
            std::replace(r.begin(), r.end(), '\\', '/');
            const std::string n = normpath(r);
            if (r.empty() || r[0] == '/' || n == "." || n == ".." || n.starts_with("../"))
                return setting_refused("[Launch] ViewWritable = " + vw + " ('" + r +
                                       "': een pad binnen de gamemap, relatief en zonder ..)");
            // Ten opzichte van de WORTEL van de view (de werkmap van de game), niet van de map van
            // de exe: Robin Hood start uit de view zelf en schrijft in log/ naast data/.
            std::string wortel = dirname(game);
            for (int diepte = 0; diepte < 8 && dirname(wortel) != normpath(p.views()) && !dirname(wortel).empty(); ++diepte)
                wortel = dirname(wortel);
            pl.view_writable.push_back(normpath(pjoin(wortel, n)));
        }
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] ViewWritable vraagt een view van de game; die mappen "
                                   "komen alleen in de view, nooit op de NAS");
    }
    // 6c3. [Launch] ViewTtcAsTtf: een TrueType-collectie (.ttc) wordt in de view face 0 als losse font (ttc.hpp): Wine's
    // GdipPrivateAddMemoryFont neemt geen collectie aan. Paden ten opzichte van de wortel van de view, zoals ViewRealDirs; de map
    // eromheen moet in de view een echte map zijn (ViewRealDirs), want op de NAS wordt niets geschreven. Silent Scope: Bone Eater
    // laadt zo zijn vier DynaFont-collecties (2026-10-17).
    if (const std::string vt = trim(lini("ViewTtcAsTtf")); !vt.empty()) {
        for (std::string r : nonempty_split(vt, ',')) {
            r = trim(r);
            std::replace(r.begin(), r.end(), '\\', '/');
            const std::string n = normpath(r);
            if (r.empty() || r[0] == '/' || n == "." || n == ".." || n.starts_with("../"))
                return setting_refused("[Launch] ViewTtcAsTtf = " + vt + " ('" + r + "': een pad binnen de gamemap, relatief en zonder ..)");
            pl.view_ttc_fonts.push_back(normpath(pjoin(view_root, n)));
        }
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] ViewTtcAsTtf vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): de fonts "
                                   "komen alleen in de view, nooit op de NAS; " + game + " ligt daar niet");
    }
    // 6c4. [Launch] Mod = <naam> | none: een mod uit [Mod <naam>] (docs/per-game-patches.md, "Mods"), opgehaald door scripts/fetch-mods.py
    // naar <data>/mods/<Game>/<naam>/files. De lijst (List, de indeling van files.csv van de MKDX-patcher: doel in de map van de exe,
    // bestand, offset of "replace") wordt hier gelezen; apply_direct() bouwt en linkt. Mario Kart DX: de Engelse fanvertaling (2026-10-17).
    if (binnen(abspath(game), abspath(p.views())) && !on_nas(dirname(game), p)) {
        std::string mr = getenv_or(env, "RAVENGUN_MODS_ROOT");
        pl.mod_root = pjoin(mr.empty() ? pjoin(p.data, "mods") : mr, name);
        // De doelen zijn paden in de map van de exe (zoals bij de patcher van de mod); het origineel staat naast de exe op de NAS.
        pl.mod_view_root = dirname(game);
        const std::string orig = game + kStackOrigSuffix;
        pl.mod_game_root = dirname(islink(orig) ? realpath_(orig) : realpath_(game));
    }
    if (std::string mn = trim(lini("Mod")); !mn.empty() && lower(mn) != "none" && lower(mn) != "off" && lower(mn) != "false") {
        if (pl.mod_root.empty())
            return setting_refused("[Launch] Mod vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): de bestanden van "
                                   "de mod komen alleen in de view, nooit op de NAS; " + game + " ligt daar niet");
        const gsn::Section* ms = settings.settings.section("Mod " + mn);
        if (!ms) return setting_refused("[Launch] Mod = " + mn + ", maar game.ini heeft geen [Mod " + mn + "]");
        if (!isdir(pl.mod_game_root))
            return setting_refused("[Launch] Mod = " + mn + ": de gamemap achter de view (" + pl.mod_game_root + ") is niet te vinden");
        const std::string dir = pjoin(pl.mod_root, mn), files = pjoin(dir, "files");
        std::string stamp;
        read_file(pjoin(dir, "tree-sha256"), stamp);
        pl.mod_tree = lower(trim(kv_get(ms->kv, "TreeSha256")));
        if (pl.mod_tree.empty() || trim(stamp) != pl.mod_tree || !isdir(files))
            return setting_refused("[Launch] Mod = " + mn + ": de mod staat niet (of niet in deze versie) in " + files + ": draai scripts/fetch-mods.py " + name);
        pl.mod_name = mn;
        pl.mod_built = pjoin(dir, "built");
        pl.mod_exe = pjoin(pl.mod_game_root, basename(game));
        pl.mod_exe_sha256 = lower(trim(kv_get(ms->kv, "ExeSha256")));
        std::string list;
        const std::string lijst = pjoin(files, trim(kv_get(ms->kv, "List", "files.csv")));
        if (!read_file(lijst, list)) return setting_refused("[Launch] Mod = " + mn + ": " + lijst + " is niet te lezen");
        int nr = 0;
        for (std::string regel : nonempty_split(strip_bom(list), '\n')) {
            ++nr;
            if (!regel.empty() && regel.back() == '\r') regel.pop_back();
            if (trim(regel).empty() || (nr == 1 && regel.starts_with("target,"))) continue;
            std::vector<std::string> v = nonempty_split(regel, ',');
            if (v.size() < 3) return setting_refused(lijst + ", regel " + std::to_string(nr) + ": geen doel,bestand,offset");
            DirectPlan::ModEntry e;
            for (int i = 0; i < 2; ++i) {
                std::string r = trim(v[static_cast<size_t>(i)]);
                std::replace(r.begin(), r.end(), '\\', '/');
                const std::string n = normpath(r);
                if (r.empty() || r[0] == '/' || n == "." || n == ".." || n.starts_with("../"))
                    return setting_refused(lijst + ", regel " + std::to_string(nr) + ": '" + r + "' (een pad, relatief en zonder ..)");
                (i == 0 ? e.target : e.src) = n;
            }
            e.src = pjoin(files, e.src);
            if (!isfile(e.src)) return setting_refused(lijst + ", regel " + std::to_string(nr) + ": " + e.src + " ontbreekt");
            const std::string off = lower(trim(v[2]));
            if (off != "replace" && (!py_int(off, e.offset) || e.offset < 0))
                return setting_refused(lijst + ", regel " + std::to_string(nr) + ": offset '" + v[2] + "' (een getal of replace)");
            pl.mod_entries.push_back(std::move(e));
        }
        if (pl.mod_entries.empty()) return setting_refused("[Launch] Mod = " + mn + ": " + lijst + " is leeg");
    }
    // 6d. [Launch] Dxvk: Direct3D 8 tot en met 11 via DXVK (Vulkan) in plaats van wined3d (OpenGL), per game in de view (docs/dxvk.md).
    // Mario Kart DX: met wined3d ontbrak de 2D-laag (geen tekst, geen menu's) en kwamen er duizenden GL-fouten; met DXVK niet.
    if (const std::string dx = lower(trim(lini("Dxvk"))); !dx.empty() && dx != "false" && dx != "0" && dx != "off") {
        if (dx != "true" && dx != "1" && dx != "on") return setting_refused("[Launch] Dxvk = " + lini("Dxvk") + " (true of false)");
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] Dxvk vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): de DLL's van "
                                   "DXVK komen naast de exe in de view, nooit op de NAS; " + game + " ligt daar niet");
        pl.dxvk_dir = find_dxvk(p, env, is64, pl.dxvk_from);
        if (pl.dxvk_dir.empty())
            return setting_refused(std::string("[Launch] Dxvk = true, maar DXVK (") + (is64 ? "64" : "32") + " bits) is niet gevonden: draai "
                                   "scripts/fetch-dxvk.sh, of installeer Proton in Steam (docs/dxvk.md)");
    }
    // 6d2. [Launch] BepInEx = 5.4.21.0: BepInEx (Mono, met de doorstop winhttp.dll) naast de exe in de view, voor een eigen plugin als
    // drop-in (dropin/, To = BepInEx/plugins). Bron: de officiële release in <data>/bepinex/<versie>/<x64|x86> (scripts/fetch-bepinex.sh),
    // nooit in git. Men in Black stopt zichzelf zonder kast-shell (Application.Quit); onze plugin onderdrukt dat (2026-10-17).
    if (const std::string bv = trim(lini("BepInEx")); !bv.empty()) {
        if (bv.find_first_not_of("0123456789.") != std::string::npos || bv.front() == '.' || bv.back() == '.')
            return setting_refused("[Launch] BepInEx = " + bv + " (een versie van BepInEx, bv. 5.4.21.0)");
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] BepInEx vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): BepInEx komt "
                                   "naast de exe in de view, nooit op de NAS; " + game + " ligt daar niet");
        std::string root = getenv_or(env, "RAVENGUN_BEPINEX_ROOT");
        if (root.empty()) root = pjoin(p.data, "bepinex");
        const std::string dir = pjoin(pjoin(root, bv), is64 ? "x64" : "x86");
        if (!isfile(pjoin(dir, "BepInEx/core/BepInEx.Preloader.dll")) || !isfile(pjoin(dir, "winhttp.dll")) || !isfile(pjoin(dir, "doorstop_config.ini")))
            return setting_refused("[Launch] BepInEx = " + bv + ", maar BepInEx " + bv + " (" + (is64 ? "64" : "32") + " bits) staat niet in " + dir +
                                   ": draai scripts/fetch-bepinex.sh " + bv);
        pl.bepinex_dir = dir;
    }
    // 6f. [Launch] NativeDlls: Microsoft-DLL's die Steam al meebrengt (DirectX-herdistribueerbare in Steamworks Shared), klaargezet door
    // scripts/setup-redist.sh in <data>/redist; nooit in git. Operation G.H.O.S.T. viel in Wine's ingebouwde xactengine3_3 (2026-10-15).
    if (const std::string nd = trim(lini("NativeDlls")); !nd.empty()) {
        const std::string arch = is64 ? "x64" : "x86";
        std::string rd = getenv_or(env, "RAVENGUN_REDIST");
        if (rd.empty()) rd = p.data + "/redist";
        const std::string sysdir = pjoin(prefix, std::string("drive_c/windows/") + (is64 ? "system32" : "syswow64"));
        std::vector<std::string> ov;
        for (std::string naam : nonempty_split(nd, ',')) {
            naam = lower(trim(naam));
            if (naam.ends_with(".dll")) naam = naam.substr(0, naam.size() - 4);
            if (naam.empty() || naam.find('/') != std::string::npos || naam.find("..") != std::string::npos)
                return setting_refused("[Launch] NativeDlls = " + nd + " ('" + naam + "' is geen DLL-naam)");
            const std::string bron = pjoin(pjoin(rd, arch), naam + ".dll");
            if (!isfile(bron))
                return setting_refused("[Launch] NativeDlls: " + naam + ".dll (" + arch + ") staat niet in " + pjoin(rd, arch) +
                                       ": draai scripts/setup-redist.sh (haalt hem uit de DirectX-map van Steam; docs/redist.md)");
            pl.native_dlls.emplace_back(bron, pjoin(sysdir, naam + ".dll"));
            ov.push_back(naam + "=n");
        }
        const std::string oud = getenv_or(env, "WINEDLLOVERRIDES");
        launch["WINEDLLOVERRIDES"] = env["WINEDLLOVERRIDES"] = oud.empty() ? join(ov, ";") : oud + ";" + join(ov, ";");
    }
    // 6f2. [Launch] PhysX: de PhysX-systeemsoftware van NVIDIA moet in de prefix staan (scripts/setup-physx.sh). Een game met PhysX 2.x
    // zoekt de kern via het register; zonder installatie geeft NxCreatePhysicsSDK NULL (Elevator Action Death Parade, 2026-10-16).
    if (const std::string px = lower(trim(lini("PhysX"))); !px.empty() && px != "false" && px != "0" && px != "off") {
        if (px != "true" && px != "1" && px != "on") return setting_refused("[Launch] PhysX = " + lini("PhysX") + " (true of false)");
        if (!isfile(pjoin(prefix, "drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.1/PhysXCore.dll")))
            return setting_refused("[Launch] PhysX = true, maar de PhysX-systeemsoftware staat niet in " + prefix +
                                   ": draai scripts/setup-physx.sh " + prefix + " (docs/redist.md)");
    }
    // 6f2. [Launch] Install = bestand uit de spelmap (relatief aan de view-wortel), InstallArgs = stille vlaggen: een installatieprogramma
    // dat de game zelf meelevert, eenmalig per prefix (markering <prefix>/.ravengun-install/<naam>.<grootte>). Aliens Extermination:
    // "divx60 (install for videos to work).exe" /S, anders "Failed To Open The AVI Frame" (2026-10-17).
    if (std::string inst = trim(lini("Install")); !inst.empty()) {
        if (inst.front() == '/' || inst.find("..") != std::string::npos)
            return setting_refused("[Launch] Install = " + inst + " (een bestand in de map van de game, zonder ..)");
        const std::string pad = pjoin(view_wortel(dirname(game), p), inst);
        if (!isfile(pad)) return setting_refused("[Launch] Install: " + pad + " bestaat niet");
        std::error_code fec;
        const auto grootte = std::filesystem::file_size(pad, fec);
        pl.install_cmd = {wine, wine_path(pad)};
        for (const std::string& a : nonempty_split(trim(lini("InstallArgs")), ' ')) if (!trim(a).empty()) pl.install_cmd.push_back(trim(a));
        pl.install_mark = pjoin(pjoin(prefix, ".ravengun-install"), basename(pad) + "." + std::to_string(static_cast<unsigned long long>(fec ? 0 : grootte)));
        pl.notes.push_back("installatie: " + pad + " (eenmalig per prefix)");
    }
    // 6g. [Launch] Registry: .reg-bestanden uit patches/<Game>/ (meestal data/; gebruikerslaag eerst) die rg-direct één keer per prefix inleest met
    // "wine reg import" in de registerweergave van de game (32 of 64 bits; docs/registry.md). Operation G.H.O.S.T.: .wmv via de
    // GStreamer-splitter in plaats van de ASF-lezer van Proton, die bij Stop vastliep (2026-10-15).
    if (const std::string rg = trim(lini("Registry")); !rg.empty()) {
        for (std::string naam : nonempty_split(rg, ',')) {
            naam = trim(naam);
            if (naam.empty() || naam.front() == '/' || naam.find("..") != std::string::npos || !lower(naam).ends_with(".reg"))
                return setting_refused("[Launch] Registry = " + rg + " ('" + naam + "' is geen .reg-bestand in de map van de game, "
                                       "bijvoorbeeld data/naam.reg)");
            std::string pad;
            for (const std::string& root : {p.dirs.user, p.dirs.shipped})
                if (!root.empty() && pad.empty() && isfile(pjoin(pjoin(root, name), naam))) pad = pjoin(pjoin(root, name), naam);
            if (pad.empty()) return setting_refused("[Launch] Registry: " + naam + " staat niet in patches/" + name + "/");
            pl.reg_files.push_back(pad);
            pl.notes.push_back("register: " + pad + " (wine reg import, eenmalig per prefix)");
        }
    }
    // 6h. [Launch] AmDaemon = true: rg-direct is tijdens de run de AM-daemon van de Sega Ring-games (amdaemon.hpp, docs/research/segajvs/):
    // de game belt 127.0.0.1:40100..40115 en wacht op een tekstprotocol. De antwoorden zijn de standaardwaarden voor de regio uit
    // DongleRegion, met [AmDaemon] van game.ini erover (sleutel = waarde; Ports = lijst met poorten, komma's). De regio's komen uit de velden
    // van het profiel: daar staan [Game] DongleRegion/PcbRegion (stap 2) en de keuze uit Properties > Options ([Config] General/...) al in,
    // precies wat ook in teknoparrot.ini komt. Alleen [Game] lezen liet de optie in het menu op de route zonder TP niets doen (SDR: PcbRegion
    // in het menu, toch regio 01 in de EEPROM en "ERROR 0903 WRONG REGION", gemeten 2026-10-18).
    const auto field_value = [&pr](const char* k) { const Field* f = pr.field(k); return f ? trim(f->value) : std::string(); };
    if (ld_truthy(trim(lini("AmDaemon")))) {
        pl.am_daemon = true;
        pl.am_values = amd::default_values(field_value("DongleRegion"));
        std::vector<int> ports(amd::kDefaultPorts, amd::kDefaultPorts + amd::kDefaultPortCount);
        if (const gsn::Section* am = settings.settings.section("AmDaemon")) {
            for (const auto& [k, v] : am->kv) {
                const std::string val = trim(v);
                if (lower(k) == "ports") {
                    ports.clear();
                    for (const std::string& pt : nonempty_split(val, ',')) {
                        long long n = 0;
                        if (!py_int(trim(pt), n) || n < 1024 || n > 65535)
                            return setting_refused("[AmDaemon] Ports = " + val + " ('" + trim(pt) + "' is geen poort van 1024 tot 65535)");
                        ports.push_back(static_cast<int>(n));
                    }
                    continue;
                }
                if (lower(k).rfind("response.", 0) == 0) {
                    if (!amd::valid_response_extra(val))
                        return setting_refused("[AmDaemon] " + k + " = " + val + " (extra velden k=v&k2=v2, hoogstens 200 tekens)");
                    pl.am_values[k] = val;
                    continue;
                }
                if (!amd::valid_value(val))
                    return setting_refused("[AmDaemon] " + k + " = " + val + " (een antwoord bestaat uit letters, cijfers en . _ - : @ % / # { } en een backslash, hoogstens 200 tekens)");
                pl.am_values[k] = val;
            }
        }
        // rg-amhw.dll zet hiermee AM_SYSDATA_STATIC in de EEPROM (tools/rg-amhw/amhw_sysdata.h): de regio van de PRINTPLAAT (TeknoParrot: PcbRegion; de
        // keychip is DongleRegion). Een lege EEPROM heeft regio 0: LGS en SDR melden dan "Error 0903 Wrong Region" en GG toont de verkeerde taal. Standaard
        // dezelfde waarde als de keychip; PcbRegion (veld van het profiel) wint. Een [Env] van game.ini of een drop-in wint van beide.
        if (!env.count("RG_AMHW_REGION")) {
            std::string reg;
            if (auto it = pl.am_values.find("keychip.appboot.region"); it != pl.am_values.end()) reg = it->second;
            if (const std::string pcb = field_value("PcbRegion"); !pcb.empty()) reg = amd::region_code(pcb);
            if (!reg.empty()) env["RG_AMHW_REGION"] = reg;
        }
        if (ports.empty()) return setting_refused("[AmDaemon] Ports is leeg");
        for (int pt : ports) pl.am_ports.push_back(std::to_string(pt));
        pl.notes.push_back("AM-daemon: rg-direct beantwoordt " + std::to_string(pl.am_values.size()) + " sleutels op 127.0.0.1 poort " + join(pl.am_ports, ","));
    }
    // 6e. [Launch] Gst32: een 32-bits GStreamer voor films via DirectShow/Media Foundation (docs/gst32.md). SteamOS heeft er geen; Proton
    // van Steam wel. scripts/setup-gst32.sh zet die klaar in <data>/ravengun/gst32 (proton/, plugins/, lib/). Bug Busters viel zonder
    // na MODE SELECT: "Failed to create filter for GStreamer splitter filter", dan een NULL-lezing (bench 2026-10-15).
    if (const std::string gs = lower(trim(lini("Gst32"))); !gs.empty() && gs != "false" && gs != "0" && gs != "off") {
        if (gs != "true" && gs != "1" && gs != "on") return setting_refused("[Launch] Gst32 = " + lini("Gst32") + " (true of false)");
        std::string g = getenv_or(env, "RAVENGUN_GST32");
        if (g.empty()) g = p.data + "/gst32";  // p.data volgt XDG_DATA_HOME (de portabele map zet die op User/share)
        if (!isdir(pjoin(g, "plugins")) || !isdir(pjoin(g, "proton")) || !isfile(pjoin(g, "proton/libgstreamer-1.0.so.0")))
            return setting_refused("[Launch] Gst32 = true, maar de 32-bits GStreamer staat niet klaar in " + g +
                                   ": draai scripts/setup-gst32.sh (leest hem uit Proton van Steam; docs/gst32.md)");
        auto voeg = [&](const std::string& k, const std::string& v) {
            const std::string oud = getenv_or(env, k);
            launch[k] = oud.empty() ? v : v + ":" + oud;
            env[k] = launch[k];
        };
        voeg("LD_LIBRARY_PATH", pjoin(g, "proton") + ":" + pjoin(g, "lib"));
        launch["GST_PLUGIN_SYSTEM_PATH_1_0"] = env["GST_PLUGIN_SYSTEM_PATH_1_0"] = pjoin(g, "plugins");
        launch["GST_REGISTRY_1_0"] = env["GST_REGISTRY_1_0"] = pjoin(g, "registry.i386.bin");
    }

    // 7. De JVS-brug en de procesnamen.
    static const std::set<std::string> no_pipe = {"EuropaRFordRacing", "EuropaRSegaRally3", "Theatrhythm", "FastIo", "GunslingerStratos3"};
    pl.pipe_mode = no_pipe.count(emu) ? "shm" : "pipe";
    pl.shm_source = o.shm_source.empty() ? "/dev/shm/TeknoParrot_JvsState" : o.shm_source;
    // De JVS-brug: onze rg-jvsbridge.exe (het onderdeel "JVS bridge"). pipehelper.exe uit de TeknoParrot-map alleen als terugval voor
    // een plan dat die map toch al gebruikt.
    const std::string eigen_brug = pjoin(p.data, "jvsbridge/rg-jvsbridge.exe");
    pl.pipehelper = eigen_brug;
    if (!isfile(pl.pipehelper) && !tp_vrij) {
        pl.pipehelper = pjoin(tp, "pipehelper.exe");
        if (isfile(pl.pipehelper)) tp_bestand("pipehelper.exe");
    }
    if (!isfile(pl.pipehelper))
        return unsupported("JVS-brug ontbreekt: " + eigen_brug + " (bouw rg-jvsbridge: scripts/build-jvsbridge.sh, of installeer het onderdeel JVS bridge)" +
                           (tp_vrij ? std::string() : "; ook geen " + pjoin(tp, "pipehelper.exe")));
    if (et == "ElfLdr2" && !zonder_lader) tp_bestand("ElfLdr2/");   // de bibliotheken van ElfLdr2 in de view (plan_view)
    pl.tp = pl.tp_files.empty() ? std::string() : tp;
    // De besturingspipe ([Input] ControlPipe op route tp-shm): de daemon schrijft het rapport, rg-direct stroomt het naar de pipe.
    {
        DirectInput din;
        std::string iwhy;
        if (direct_input(name, p, din, iwhy) == Verdict::Ok && din.route == "tp-shm" && !din.control_pipe.empty()) {
            std::string perr;
            const iob::ControlPipeProfiles cps = iob::builtin_control_pipe_profiles(perr);
            const iob::ControlPipeProfile* cp = cps.find(din.control_pipe);
            if (!cp) return setting_refused("[Input] ControlPipe = " + din.control_pipe + " staat niet in packaging/control-pipe-profiles.json");
            pl.control_pipe = cp->name;
            pl.control_pipe_name = cp->pipe;
            pl.control_source = o.control_source.empty() ? std::string(iob::kDefaultControlPipePath) : o.control_source;
            pl.control_size = cp->size;
            pl.control_interval_ms = cp->interval_ms;
        }
    }
    std::set<std::string> comms = {"budgieloader.ex", "budgieloader_x6", "openparrotloade", "teknomacaw.exe", "teknomacaw64.ex"};
    for (const char* key : {"ExecutableName", "GamePath"})
        for (std::string part : split(pr.get(key), ';')) {
            std::replace(part.begin(), part.end(), '\\', '/');
            std::string b = lower(basename(part));
            if (b.size() > 15) b.resize(15);
            if (!b.empty()) comms.insert(b);
        }
    pl.game_comms.assign(comms.begin(), comms.end());
    // De starts in de orde waarin ze gebeuren. De tweede exe draait buiten gamescope (hij heeft geen venster), in dezelfde werkmap,
    // prefix en omgeving als de eerste.
    {
        DirectStart hoofd;
        hoofd.name = basename(game);
        hoofd.role = "main";
        hoofd.exe = game;
        hoofd.argv = gamescope;
        hoofd.argv.insert(hoofd.argv.end(), args.begin(), args.end());
        hoofd.cwd = cwd;
        hoofd.gamescope = !gamescope.empty();
        hoofd.comms = exe_comms({pr.get("ExecutableName"), game});
        if (!have_second) {
            pl.starts = {hoofd};
        } else {
            DirectStart tweede;
            tweede.name = basename(second.path);
            tweede.role = "second";
            tweede.exe = second.path;
            tweede.argv = args2;
            tweede.cwd = cwd;
            tweede.comms = exe_comms({pr.get("ExecutableName2"), second.path});
            pl.starts = second.first ? std::vector<DirectStart>{tweede, hoofd} : std::vector<DirectStart>{hoofd, tweede};
            pl.starts[1].wait_before = second.wait;
        }
    }

    pl.emulator_type = et;
    pl.emulation_profile = emu;
    pl.is64 = is64;
    pl.game = game;
    pl.cwd = cwd;
    pl.wine = wine;
    pl.prefix = prefix;
    pl.loader = loader;
    pl.gamescope = !gamescope.empty();
    pl.windows = is_pe(game);
    pl.argv = gamescope;
    pl.argv.insert(pl.argv.end(), args.begin(), args.end());
    pl.env = env;
    pl.launch_env = launch;

    // 8. Drop-ins: een fout is een weigering (launch-direct.py dropin_weiger, exit 5).
    const DropinResult dr = dropin_plan(pl, p);
    if (!dr.errors.empty()) {
        std::string m;
        for (const auto& e : dr.errors) m += (m.empty() ? "" : "\n") + std::string("Drop-in geweigerd: ") + e;
        why = m;
        return Verdict::Refused;
    }
    merge_env(pl, dr.env);
    // 8b. [Loader] Exe = <naam>: de eigen lader (een drop-in van deze game, To = .) komt in elke argv op de plaats van de lader.
    if (!eigen_lader.empty()) {
        const auto it = std::find_if(pl.dropin_files.begin(), pl.dropin_files.end(), [&](const DropinFile& f) { return f.name == eigen_lader; });
        if (it == pl.dropin_files.end()) {
            why = "Drop-in geweigerd: [Loader] Exe = " + eigen_lader + ", maar dropin.ini van " + name + " heeft geen sectie [" + eigen_lader + "]";
            return Verdict::Refused;
        }
        const std::string oud = wine_path(loader);
        auto vervang = [&](std::vector<std::string>& argv) {
            for (auto& a : argv)
                if (a == loader) a = it->dest;
                else if (a == oud) a = wine_path(it->dest);
        };
        vervang(pl.argv);
        for (auto& st : pl.starts) vervang(st.argv);
        pl.notes.push_back("lader (eigen, [Loader] Exe): " + it->dest);
        pl.loader = it->dest;
        // Het spel draait in het proces van deze lader (Silent Scope: arkndd.dll in rg-macaw.exe): de daemon moet dat proces als "de game
        // draait" zien, anders meldt hij na de wachttijd "niet gestart" terwijl het spel loopt (gemeten 2026-10-17).
        std::string comm = lower(basename(it->dest));
        if (comm.size() > 15) comm.resize(15);
        if (std::find(pl.game_comms.begin(), pl.game_comms.end(), comm) == pl.game_comms.end()) pl.game_comms.push_back(comm);
    }
    // 9. gamescope heeft cap_sys_nice (setcap): de dynamische linker draait hem dan in secure-execution en haalt LD_LIBRARY_PATH,
    // LD_PRELOAD en LD_AUDIT uit zijn omgeving, dus ook uit die van de game. Achter "--" zet env ze terug. Operation G.H.O.S.T.:
    // zonder deze stap vond de 32-bits winegstreamer.so libgstaudio niet (Gst32) en faalde de WM-lezer op de titelfilm (2026-10-15).
    if (pl.gamescope) {
        std::vector<std::string> terug;
        for (const char* k : {"LD_LIBRARY_PATH", "LD_PRELOAD", "LD_AUDIT"})
            if (const std::string v = getenv_or(pl.env, k); !v.empty()) terug.push_back(std::string(k) + "=" + v);
        if (!terug.empty()) {
            auto zet_terug = [&](std::vector<std::string>& argv) {
                auto it = std::find(argv.begin(), argv.end(), "--");
                if (it == argv.end()) return;
                std::vector<std::string> in = {"/usr/bin/env"};
                in.insert(in.end(), terug.begin(), terug.end());
                argv.insert(it + 1, in.begin(), in.end());
            };
            zet_terug(pl.argv);
            for (auto& s : pl.starts)
                if (s.gamescope) zet_terug(s.argv);
        }
    }
    return Verdict::Ok;
}

bool needs_gst_fetch(const DirectPlan& pl) {
    const std::string gst = getenv_or(pl.env, "GST_PLUGIN_PATH");
    return !gst.empty() && !isfile(pjoin(gst, "libgstasf.so"));
}

}  // namespace ravengun::launch
