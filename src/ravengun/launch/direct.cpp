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
    pl.tp = p.tp;
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
    {   // load_own_profile(): de revisie van het sjabloon in de runtime
        std::string stock;
        if (read_file(tp + "/GameProfiles/" + name + ".xml", stock)) {
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
    std::map<std::string, std::string> pd;
    {
        std::string x;
        if (read_file(tp + "/ParrotData.xml", x)) {
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
    const std::string cwd = elf_like ? gamedir : tp;
    std::string loader = pjoin(tp, exe_rel);
    if (elf_like && !(et == "ElfLdr2" && is64) && isfile(pjoin(gamedir, exe_rel))) loader = pjoin(gamedir, exe_rel);
    if (!isfile(loader)) return unsupported("lader ontbreekt: " + loader);
    if (!dll_rel.empty() && !isfile(pjoin(tp, dll_rel + ".dll"))) return unsupported("kern ontbreekt: " + pjoin(tp, dll_rel) + ".dll");

    // 2. Omgeving, keuzes van het profiel, eigen build, patches.
    EnvMap env = o.base_env;
    for (const char* key : {"DongleRegion", "PcbRegion"}) {   // apply_profile_choices()
        const std::string want = trim(settings.get("Game", key));
        if (want.empty()) continue;
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
    // own_build(): [Loader] Build/Core/Path
    std::string core_dir = tp;
    {
        const gsn::Section* ls = settings.settings.section("Loader");
        auto lget = [&](const char* k) { return ls ? kv_get(ls->kv, k) : std::string(); };
        std::string build = lower(trim(lget("Build"))), core = lower(trim(lget("Core")));
        for (const auto& [key, val] : {std::pair<std::string, std::string>{"Build", build}, {"Core", core}})
            if (!val.empty() && val != "teknoparrot" && val != "ravengun") return setting_refused("[Loader] " + key + " = " + val + " (kies teknoparrot of ravengun)");
        if (core.empty() && build == "ravengun" && et == "OpenParrot") core = "ravengun";
        if (build == "ravengun" || core == "ravengun") {
            const std::string pth = trim(lget("Path"));
            const std::string base = expand_home(pth.empty() ? std::string("~/.local/share/ravengun/openparrot/current") : pth, p.home);
            std::string own_loader, own_core_rel;
            const bool op_loader = starts_with(lower(basename(exe_rel)), "openparrotloader");
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
    if (!dll_rel.empty()) {
        pl.core_dll = pjoin(core_dir, dll_rel + ".dll");
        pl.core_source = core_dir == tp ? "runtime" : "ravengun";
    }
    // plan_file_patches(): een afgewezen patch die aan staat weigert; een ingeschakelde patch past de engine (nog) niet toe.
    for (const auto& e : settings.patch_errors)
        if (e.enabled) return patch_refused(e.message);
    for (const auto& e : settings.patch_errors) pl.patch_lines.push_back("overgeslagen '" + e.name + "' @- [afgewezen: " + e.message + "]");
    for (const auto& pt : settings.patches) {
        if (enabled[pt.name]) return engine("patch '" + pt.name + "' staat aan (de patchmotor draait alleen in launch-direct.py)");
        const std::string plek = pt.kind() == "memory" ? (pt.get("Module").empty() ? "?" : pt.get("Module")) + "+" + (pt.get("Offset").empty() ? "?" : pt.get("Offset"))
                                                       : (pt.get("Target").empty() ? "-" : pt.get("Target"));
        pl.patch_lines.push_back("overgeslagen '" + pt.name + "' @" + plek + " [uit]");
    }
    if (!trim(settings.get("Game", "ExeSha256")).empty()) return engine("[Game] ExeSha256 (de versiecontrole van de patchmotor)");

    // 3. Wine en de prefix.
    std::string wine = pr.get("WineRunnerPath");
    if (!(!wine.empty() && isfile(wine))) wine = isfile(getenv_or(env, "TP_WINE")) ? getenv_or(env, "TP_WINE") : p.home + "/.local/share/ravengun/wine/proton-11.0-2/bin/wine";
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
        if (!letter.empty() && letter.back() == ':') letter.pop_back();
        if (letter.size() != 1 || letter[0] < 'A' || letter[0] > 'Y' || letter == "C")
            return setting_refused("schijfletter '" + trim(stuk.substr(0, is)) + "' kan niet (een letter van A tot Y, niet C)");
        std::string doel = trim(stuk.substr(is + 1));
        if (starts_with(doel, "~/")) doel = p.home + doel.substr(1);
        doel = normpath(doel.size() && doel[0] == '/' ? doel : pjoin(view_root, doel));
        if (!isdir(doel)) return setting_refused("schijf " + letter + ": de map " + doel + " bestaat niet");
        if (on_nas(doel, p)) return setting_refused("schijf " + letter + ": de map " + doel + " ligt op de NAS; een schijfletter van ons wijst naar een lokale map (de view van het spel, of een eigen map)");
        DirectDrive d;
        d.letter = letter;
        d.target = doel;
        d.link = pjoin(pjoin(prefix, "dosdevices"), lower(letter) + ":");
        if (!isdir(pjoin(prefix, "dosdevices"))) return setting_refused("schijf " + letter + ": de prefix " + prefix + " heeft geen dosdevices-map (nog niet aangemaakt; start het spel een keer zonder schijfletters)");
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
    std::vector<std::string> args{wine, loader};
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
    const std::vector<std::string> basis(args.begin(), args.end() - 1);   // wine, lader, kern: de tweede exe krijgt dezelfde
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
        static const std::regex res(R"(^(\d+)x(\d+)$)");
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
    // 6c. [Launch] ViewLinks: een bestand in de view wordt een symlink naar een ander bestand van dezelfde game (alleen in de view;
    // Mario Kart DX: Polygon2D_D3D10.fx -> de meegeleverde voorgecompileerde Polygon2D_D3D10.cfx, NETWERK-INVOER-CAMERA.md 5d).
    if (const std::string vl = trim(lini("ViewLinks")); !vl.empty()) {
        std::string verr;
        if (!parse_view_links(vl, dirname(game), pl.view_links, verr)) return setting_refused("[Launch] ViewLinks = " + vl + " (" + verr + ")");
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
            if (r.empty() || r[0] == '/' || n == "." || n == ".." || starts_with(n, "../"))
                return setting_refused("[Launch] ViewCopies = " + vc + " ('" + r + "': een pad binnen de gamemap, relatief en zonder ..)");
            pl.view_copies.push_back(normpath(pjoin(dirname(game), n)));
        }
        if (!binnen(abspath(game), abspath(p.views())) || on_nas(dirname(game), p))
            return setting_refused("[Launch] ViewCopies vraagt een view van de game (scripts/tp-add-game.py <Game> <exe> --view): de kopieën "
                                   "komen alleen in de view, nooit op de NAS; " + game + " ligt daar niet");
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
    // 6f. [Launch] NativeDlls: Microsoft-DLL's die Steam al meebrengt (DirectX-herdistribueerbare in Steamworks Shared), klaargezet door
    // scripts/setup-redist.sh in <data>/redist; nooit in git. Operation G.H.O.S.T. viel in Wine's ingebouwde xactengine3_3 (2026-10-15).
    if (const std::string nd = trim(lini("NativeDlls")); !nd.empty()) {
        const std::string arch = is64 ? "x64" : "x86";
        std::string rd = getenv_or(env, "RAVENGUN_REDIST");
        if (rd.empty()) rd = p.home + "/.local/share/ravengun/redist";
        const std::string sysdir = pjoin(prefix, std::string("drive_c/windows/") + (is64 ? "system32" : "syswow64"));
        std::vector<std::string> ov;
        for (std::string naam : nonempty_split(nd, ',')) {
            naam = lower(trim(naam));
            if (ends_with(naam, ".dll")) naam = naam.substr(0, naam.size() - 4);
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
    // 6g. [Launch] Registry: .reg-bestanden uit patches/<Game>/ (meestal data/; gebruikerslaag eerst) die rg-direct één keer per prefix inleest met
    // "wine reg import" in de registerweergave van de game (32 of 64 bits; docs/registry.md). Operation G.H.O.S.T.: .wmv via de
    // GStreamer-splitter in plaats van de ASF-lezer van Proton, die bij Stop vastliep (2026-10-15).
    if (const std::string rg = trim(lini("Registry")); !rg.empty()) {
        for (std::string naam : nonempty_split(rg, ',')) {
            naam = trim(naam);
            if (naam.empty() || naam.front() == '/' || naam.find("..") != std::string::npos || !ends_with(lower(naam), ".reg"))
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
    // 6e. [Launch] Gst32: een 32-bits GStreamer voor films via DirectShow/Media Foundation (docs/gst32.md). SteamOS heeft er geen; Proton
    // van Steam wel. scripts/setup-gst32.sh zet die klaar in <data>/ravengun/gst32 (proton/, plugins/, lib/). Bug Busters viel zonder
    // na MODE SELECT: "Failed to create filter for GStreamer splitter filter", dan een NULL-lezing (bench 2026-10-15).
    if (const std::string gs = lower(trim(lini("Gst32"))); !gs.empty() && gs != "false" && gs != "0" && gs != "off") {
        if (gs != "true" && gs != "1" && gs != "on") return setting_refused("[Launch] Gst32 = " + lini("Gst32") + " (true of false)");
        std::string g = getenv_or(env, "RAVENGUN_GST32");
        if (g.empty()) g = p.home + "/.local/share/ravengun/gst32";
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
    pl.pipehelper = p.home + "/.local/share/ravengun/jvsbridge/rg-jvsbridge.exe";
    if (!isfile(pl.pipehelper)) pl.pipehelper = pjoin(tp, "pipehelper.exe");
    if (!isfile(pl.pipehelper))
        return unsupported("JVS-brug ontbreekt: bouw rg-jvsbridge (scripts/build-jvsbridge.sh) of zorg voor pipehelper.exe in de runtime-map");
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
