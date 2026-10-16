// Tests voor de directe route zonder scripts (src/ravengun/launch/direct.*, jvs_pipe.*, tools/rg-direct): het plan (lader, eigen build,
// argumenten als een parameter, omgeving met drop-ins, teknoparrot.ini met DropdownIndex en een regiokeuze), de uitkomsten die naar het
// script gaan of weigeren, het plaatsen van drop-ins in een view met een koppeling naar de (nep-)NAS zonder op de NAS te schrijven, de
// invoerroute, het JSON-plan, de joystick-ids van wine-joyid.sh, en rg-direct --run met een nep-wine (de JVS-pipe over TCP, de helper,
// de lader, de omgeving van het spel). Alles in een tijdelijke map; start geen Wine en geen game, raakt /dev/shm/TeknoParrot_JvsState niet.
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ravengun/launch/direct.hpp"
#include "ravengun/launch/jvs_pipe.hpp"
#include "ravengun/launch/pe_stack.hpp"

namespace fs = std::filesystem;
using namespace ravengun::launch;

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

static void write(const fs::path& p, const std::string& text, bool exec = false) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
    if (exec) ::chmod(p.c_str(), 0755);
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static const char* kGameIni = R"([Game]
Profile = Spel
Name = Testspel
Type = TeknoParrot
EmulationProfile = WartranTroopers
Executable = spel.exe
Is64Bit = true
ProfileRevision = 3
DongleRegion = USA

[Launch]
Mode = direct
Arguments = /k=1 "met spatie"

[Loader]
Build = ravengun
RemoteThread = off

[Wine]
DllOverrides = d3d11=n,b

[Input]
Route = evdev
Layout = WartranTroopers

[Env]
EIGEN = ja

[Identify]
Path = bin/spel.exe
Size = 10

[Config]
General/Windowed = 0
General/DongleRegion = JAPAN
General/Mode = B

[ConfigType]
General/Windowed = Bool
General/DongleRegion = Dropdown
General/Mode = DropdownIndex

[ConfigOptions]
General/DongleRegion = JAPAN|USA
General/Mode = A|B|C
)";

int main() {
    char tmpl[] = "/tmp/rg-direct-XXXXXX";
    const char* t = mkdtemp(tmpl);
    if (!t) { std::perror("mkdtemp"); return 2; }
    const fs::path root = t;
    const fs::path home = root / "home";

    // --- kleine hulpen -----------------------------------------------------------------------------------------------------------
    CHECK((shlex_split(R"(a "b c" 'd e' f\ g)") == std::vector<std::string>{"a", "b c", "d e", "f g"}));
    CHECK((shlex_split(R"(x "" y)") == std::vector<std::string>{"x", "", "y"}));
    { bool ok = true; shlex_split("\"open", &ok); CHECK(!ok); }
    CHECK((win_split(R"(/k=1 "met spatie" C:\pad\ "a\"b")") == std::vector<std::string>{"/k=1", "met spatie", R"(C:\pad\)", "a\"b"}));
    CHECK(wine_path("/a/b c") == "Z:\\a\\b c");
    CHECK(new_uuid4().size() == 36 && new_uuid4()[14] == '4');

    // --- welke routes het gedeelde geheugen van de daemon laten spiegelen (play-halo.sh --shm-source) ---------------------------
    {
        DirectInput in;
        in.route = "tp-shm";
        CHECK(in.writes_tp_shm());
        in.route = "jvs";
        CHECK(!in.writes_tp_shm());
        in.layout = "LGS";   // Let's Go Safari: de daemon schrijft het stuur in het bestand van rg-direct, de spiegel blijft dat lezen
        CHECK(!in.writes_tp_shm());
        in.route = "hid-board";
        CHECK(!in.writes_tp_shm());
    }

    // --- joystick-ids (wine-joyid.sh) --------------------------------------------------------------------------------------------
    const std::string reg =
        "HKEY_CURRENT_USER\\System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\DirectInput\\VID_054C&PID_03D5\\Calibration\\0\r\n"
        "    Joystick Id    REG_BINARY    00000000\r\n\r\n"
        "HKEY_CURRENT_USER\\System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\DirectInput\\VID_1209&PID_72F0\\Calibration\\0\r\n"
        "    Joystick Id    REG_BINARY    02000000\r\n";
    const auto ids = parse_joy_ids(reg);
    CHECK(ids.size() == 2 && ids[0].key == "VID_054C&PID_03D5\\Calibration\\0" && ids[0].id == 0 && ids[1].id == 2);
    const auto ch = joy_id_changes(ids, "1209:72f0");
    CHECK(ch.size() == 2 && ch[0].key == "VID_054C&PID_03D5\\Calibration\\0" && ch[0].id == 1 && ch[1].key == "VID_1209&PID_72F0\\Calibration\\0" && ch[1].id == 0);
    const auto nieuw = joy_id_changes({}, "1209:72F0");
    CHECK(nieuw.size() == 1 && nieuw[0].key == "VID_1209&PID_72F0\\Calibration\\0" && nieuw[0].id == 0);
    {   // uitvoeren met een nep-runner: query, twee keer add, wineserver -w
        std::vector<std::vector<std::string>> calls;
        std::vector<std::string> log;
        fs::create_directories(root / "pfx");
        wine_joystick_zero("/w/bin/wine", (root / "pfx").string(), "1209:72F0", {}, [&](const std::vector<std::string>& a, const EnvMap& e, std::string* out) {
            calls.push_back(a);
            CHECK(e.at("WINEPREFIX") == (root / "pfx").string() && e.at("WINEDEBUG") == "-all");
            if (out) *out = reg;
            return 0;
        }, log);
        CHECK(calls.size() == 4 && calls[0][1] == "reg" && calls[0][2] == "query" && calls[1][2] == "add" && calls[1].back() == "/f");
        CHECK(std::find(calls[1].begin(), calls[1].end(), "01000000") != calls[1].end());
        CHECK(calls[3][0] == "/w/bin/wineserver" && calls[3][1] == "-w");
    }

    // --- JVS -----------------------------------------------------------------------------------------------------------------------
    {
        namespace J = jvspipe;
        J::Bytes rest;
        auto fr = J::frames({0x00, 0xE0, 0xFF, 0x03, 0xF1, 0x01, 0xF4, 0xE0, 0x01}, rest);
        CHECK(fr.size() == 1 && J::hex(fr[0]) == "ff03f101f4" && J::hex(rest) == "e001");
        fr = J::frames({0xE0, 0x01, 0x02, 0xD0, 0xDF, 0x03}, rest);   // escape: D0 DF = E0
        CHECK(fr.size() == 1 && J::hex(fr[0]) == "0102e003" && rest.empty());
        const J::Board b = J::default_board();
        CHECK(!J::answer(b, {0xF0}));
        CHECK(J::hex(*J::answer(b, {0x11})) == "010113");
        CHECK(J::hex(*J::answer(b, {0x77})) == "0102");
        CHECK(J::hex(J::pack({0x01, 0xD0})) == "e00003" "01d0cf" "d4");
        J::Board c = b;
        CHECK(J::configure(c, "X", "01 02 00") && J::hex(c.ident) == "5800" && J::hex(c.features) == "010200");
        CHECK(!J::configure(c, "", "zz"));
    }

    // --- de opstelling ---------------------------------------------------------------------------------------------------------------
    DirectPaths p;
    p.home = home.string();
    p.tp = (home / "tp").string();
    p.data = (home / ".local/share/ravengun").string();
    p.state = (home / ".local/state/ravengun").string();
    p.dirs = {(root / "patches").string(), (root / "user").string()};
    p.library_file = (home / "library.json").string();
    p.mounts = (root / "mounts").string();
    write(p.mounts, "proc /proc proc rw 0 0\n");
    for (const char* rel : {"OpenParrotx64/OpenParrotLoader64.exe", "TeknoParrot/TeknoParrot64.dll", "pipehelper.exe"}) write(fs::path(p.tp) / rel, "x");
    write(fs::path(p.tp) / "ParrotData.xml", "\xEF\xBB\xBF<ParrotData><ExitGameKey>0x1B</ExitGameKey><Elfldr2NetworkAdapterName /></ParrotData>");
    write(fs::path(p.tp) / "GameProfiles/Spel.xml", "<GameProfile><GameProfileRevision>3</GameProfileRevision></GameProfile>");
    const fs::path own = fs::path(p.data) / "openparrot/current/OpenParrotx64/OpenParrotLoader64.exe";
    write(own, "x");
    const fs::path wine = home / ".local/share/ravengun/wine/proton-11.0-2/bin/wine";
    write(fs::path(p.dirs.shipped) / "Spel/game.ini", kGameIni);
    write(fs::path(p.dirs.shipped) / "Spel/dropin/hook.dll", "MZhook");
    write(fs::path(p.dirs.shipped) / "Spel/dropin/data/x.ini", "x=1\n");
    write(fs::path(p.dirs.shipped) / "Spel/dropin/dropin.ini",
          "; commentaar\n[hook.dll]\nTo = .\nDllOverride = native\nPreload = true\nEnv = A=1;B=2\n\n[data/x.ini]\nTo = sub\n");
    const fs::path view = fs::path(p.views()) / "Spel";
    write(view / "bin/spel.exe", std::string("MZ") + std::string(8, '\0'));
    const fs::path nas = home / "mnt/nas/sub";
    write(nas / "y.dat", "nas");
    fs::create_directory_symlink(nas, view / "bin/sub");
    write(root / "src", std::string(64, '\0'));

    DirectOptions o;
    o.base_env = {{"PATH", std::getenv("PATH") ? std::getenv("PATH") : "/usr/bin:/bin"}, {"HOME", p.home}, {"WINEDLLOVERRIDES", "basis=b"}};
    o.session_id = "sessie";
    o.shm_source = (root / "src").string();
    DirectPlan pl;
    std::string why;

    // De standaard-wine ontbreekt nog niet: launch-direct.py controleert hem niet (alleen de wrapper en TP_WINE).
    Verdict v = plan_direct("Spel", p, o, pl, why);
    CHECK(v == Verdict::Ok);
    if (v != Verdict::Ok) std::fprintf(stderr, "plan: %s\n", why.c_str());
    const std::string game = (view / "bin/spel.exe").string();
    CHECK(pl.game == game && pl.game_source == "view" && pl.cwd == p.tp);
    CHECK((pl.argv == std::vector<std::string>{wine.string(), own.string(), wine_path(p.tp + "/TeknoParrot/TeknoParrot64"), wine_path(game),
                                                "/k=1 \"met spatie\""}));   // de OpenParrot-lader: een parameter, letterlijk
    CHECK(pl.core_dll == p.tp + "/TeknoParrot/TeknoParrot64.dll" && pl.core_source == "runtime");
    CHECK(pl.prefix == p.home + "/.local/share/ravengun/prefixes/shared/wine" && pl.env.at("WINEPREFIX") == pl.prefix);
    CHECK(!pl.env.count("TP_REMOTETHREAD") && pl.env.at("WINEDEBUG") == "-all" && pl.env.at("TP_LAUNCH_SESSION_ID") == "sessie");
    CHECK(pl.env.at("WINEDLLOVERRIDES") == "d3d11=n,b;hook=n");   // game.ini wint van de basis, de drop-in komt erbij
    CHECK(pl.env.at("EIGEN") == "ja" && pl.env.at("A") == "1" && pl.env.at("B") == "2");
    CHECK(pl.env.at("TP_PRELOAD_DLL") == wine_path((view / "bin/hook.dll").string()));
    CHECK(pl.env.at("RG_DROPIN_PRELOAD") == (view / "bin/hook.dll").string() && !pl.env.count("LD_AUDIT"));
    CHECK(pl.launch_env.at("A") == "1" && !pl.launch_env.count("EIGEN"));
    CHECK(pl.files.size() == 1 && pl.files[0].first == (view / "bin/teknoparrot.ini").string());
    CHECK(pl.files[0].second == "[GlobalHotkeys]\nExitKey=0x1B\nPauseKey=0x13\n[General]\nWindowed=0\nDongleRegion=USA\nMode=1\n");
    CHECK(pl.dropin_materialise.size() == 1 && pl.dropin_materialise[0] == (view / "bin/sub").string());
    CHECK(pl.dropin_files.size() == 2 && pl.dropin_files[1].dest == (view / "bin/sub/data/x.ini").string());
    CHECK(pl.pipe_mode == "pipe" && pl.pipehelper == p.tp + "/pipehelper.exe" && pl.shm_source == (root / "src").string());
    CHECK(std::find(pl.game_comms.begin(), pl.game_comms.end(), "spel.exe") != pl.game_comms.end());
    CHECK(!needs_gst_fetch(pl));

    // JSON heen en terug.
    {
        DirectPlan back;
        std::string err;
        CHECK(plan_from_json(plan_to_json(pl), back, err));
        CHECK(back.argv == pl.argv && back.env == pl.env && back.files == pl.files && back.dropin_files.size() == 2 && back.game_comms == pl.game_comms);
        CHECK(!plan_from_json("{}", back, err));
    }

    // Plaatsen: de koppeling naar de NAS wordt een echte map met koppelingen; op de NAS komt niets bij.
    {
        std::vector<std::string> log;
        std::string err;
        CHECK(apply_direct(pl, p, log, err));
        CHECK(!fs::is_symlink(view / "bin/sub") && fs::is_symlink(view / "bin/sub/y.dat"));
        CHECK(slurp(view / "bin/sub/data/x.ini") == "x=1\n" && slurp(view / "bin/hook.dll") == "MZhook");
        CHECK(!fs::exists(nas / "data"));
        CHECK(slurp(view / "bin/teknoparrot.ini") == pl.files[0].second);
        CHECK(std::any_of(log.begin(), log.end(), [](const std::string& l) { return contains(l, "was een symlink naar de NAS"); }));
        log.clear();
        CHECK(apply_direct(pl, p, log, err));
        CHECK(std::count_if(log.begin(), log.end(), [](const std::string& l) { return contains(l, "is al actueel"); }) == 3);
        // Een gegenereerd bestand op de NAS wordt nooit geschreven.
        DirectPlan nasplan = pl;
        nasplan.dropin_files.clear();
        nasplan.files = {{(nas / "teknoparrot.ini").string(), "x"}};
        log.clear();
        CHECK(apply_direct(nasplan, p, log, err) && !fs::exists(nas / "teknoparrot.ini"));
        CHECK(contains(log.back(), "ligt op de NAS"));
    }

    // [Launch] StackReserve (pe_stack.hpp): de waarde, de PE-kop, het plan en de lokale kopie in de view.
    {
        uint64_t n = 0;
        CHECK(parse_stack_size("16M", n) && n == 16u << 20);
        CHECK(parse_stack_size(" 16 MiB ", n) && n == 16u << 20);
        CHECK(parse_stack_size("512k", n) && n == 512u << 10);
        CHECK(parse_stack_size("0x1000000", n) && n == 16u << 20);
        CHECK(parse_stack_size("16777216", n) && n == 16u << 20);
        CHECK(!parse_stack_size("", n) && !parse_stack_size("16G", n) && !parse_stack_size("zestien", n) && !parse_stack_size("0x", n));
        CHECK(stack_size_text(16u << 20) == "16 MiB (16777216)" && stack_size_text(96u << 10) == "96 KiB (98304)" && stack_size_text(100) == "100");
        // Een kleine PE32 en PE32+: e_lfanew 0x80, optionele kop op 0x98, SizeOfStackReserve op 0x98 + 72.
        auto pe = [](bool plus, uint64_t reserve) {
            std::string d(0x400, '\0');
            d[0] = 'M'; d[1] = 'Z'; d[0x3c] = '\x80';
            d.replace(0x80, 4, std::string("PE\0\0", 4));
            d[0x84] = plus ? '\x64' : '\x4c'; d[0x85] = plus ? '\x86' : '\x01';
            d[0x94] = static_cast<char>(plus ? 0xF0 : 0xE0);
            d[0x98] = '\x0b'; d[0x99] = plus ? '\x02' : '\x01';
            for (int i = 0; i < (plus ? 8 : 4); ++i) d[0x98 + 72 + i] = static_cast<char>((reserve >> (8 * i)) & 0xff);
            return d;
        };
        std::string d32 = pe(false, 1u << 20), d64 = pe(true, 1u << 20), perr;
        uint64_t r = 0;
        CHECK(pe_stack_reserve(d32, r, perr) && r == 1u << 20);
        CHECK(pe_stack_reserve(d64, r, perr) && r == 1u << 20);
        const std::string vooraf = d32;
        CHECK(pe_set_stack_reserve(d32, 16u << 20, perr) && pe_stack_reserve(d32, r, perr) && r == 16u << 20 && d32.size() == vooraf.size());
        size_t anders = 0;
        for (size_t i = 0; i < d32.size(); ++i) anders += d32[i] != vooraf[i];
        CHECK(anders == 2);   // 0x00100000 -> 0x01000000: alleen de bytes van dat veld
        CHECK(pe_set_stack_reserve(d64, 0x100000000ull, perr) && pe_stack_reserve(d64, r, perr) && r == 0x100000000ull);
        CHECK(!pe_set_stack_reserve(d32, 0x100000000ull, perr) && contains(perr, "PE32"));
        std::string geen = "MZ" + std::string(8, '\0');
        CHECK(!pe_stack_reserve(geen, r, perr));
        // De kop-CRC herstellen: bekende waarde (zlib.crc32(b"123456789") = cbf43926), en na een wijziging in de kop weer gelijk.
        CHECK(crc32_bytes("123456789", 9) == 0xcbf43926u);
        {
            std::string k = pe(false, 1u << 20);
            for (size_t i = 0x100; i < 0x400; ++i) k[i] = static_cast<char>(i * 7);
            const uint32_t c = crc32_bytes(k, kHeaderCrcLength);
            CHECK(pe_set_stack_reserve(k, 16u << 20, perr) && crc32_bytes(k, kHeaderCrcLength) != c);
            CHECK(pe_keep_header_crc(k, c, perr) && crc32_bytes(k, kHeaderCrcLength) == c && pe_stack_reserve(k, r, perr) && r == 16u << 20);
            std::string kort = k.substr(0, 0x200);
            CHECK(!pe_keep_header_crc(kort, c, perr));
        }

        // Het plan: de exe in de view is een koppeling naar de (nep-)NAS.
        const fs::path nas_exe = home / "mnt/nas/spel/spel.exe";
        write(nas_exe, pe(false, 1u << 20));
        const fs::path vexe = view / "bin/spel.exe";
        const std::string oud = slurp(vexe);
        fs::remove(vexe);
        fs::create_symlink(nas_exe, vexe);
        const fs::path ugi = fs::path(p.dirs.user) / "Spel/game.ini";
        write(ugi, "[Identify]\nSize = 1024\n[Launch]\nStackReserve = 16M\n");
        DirectPlan sp;
        std::string w;
        Verdict sv = plan_direct("Spel", p, o, sp, w);
        CHECK(sv == Verdict::Ok);
        if (sv != Verdict::Ok) std::fprintf(stderr, "stapelplan: %s\n", w.c_str());
        CHECK(sp.stack_reserve == 16ll << 20 && sp.stack_dest == vexe.string() && sp.stack_src == fs::canonical(nas_exe).string());
        {
            DirectPlan back;
            std::string err;
            CHECK(plan_from_json(plan_to_json(sp), back, err) && back.stack_reserve == sp.stack_reserve && back.stack_src == sp.stack_src &&
                  back.stack_dest == sp.stack_dest);
        }
        // Plaatsen: de koppeling blijft als spel.exe.ravengun-orig staan, spel.exe wordt een lokale kopie met 16 MiB; de NAS blijft gelijk.
        std::vector<std::string> log;
        std::string err;
        CHECK(apply_direct(sp, p, log, err));
        CHECK(!fs::is_symlink(vexe) && fs::is_symlink(view / "bin/spel.exe.ravengun-orig"));
        {
            const std::string kopie = slurp(vexe);
            CHECK(pe_stack_reserve(kopie, r, perr) && r == 16u << 20 && kopie.size() == 0x400);
            // De kern herkent de game aan CRC32(eerste 0x400 bytes): die is gelijk aan die van het origineel; alleen het stapelveld
            // en de vier bytes in de DOS-stub verschillen.
            const std::string bron = slurp(nas_exe);
            CHECK(crc32_bytes(kopie, kHeaderCrcLength) == crc32_bytes(bron, kHeaderCrcLength));
            size_t verschil = 0;
            for (size_t i = 0; i < kopie.size(); ++i)
                if (kopie[i] != bron[i]) {
                    ++verschil;
                    CHECK((i >= kHeaderCrcPatchAt && i < kHeaderCrcPatchAt + 4) || (i >= 0x98 + 72 && i < 0x98 + 76));
                }
            CHECK(verschil >= 2 && verschil <= 6);
            CHECK(pe_stack_reserve(slurp(nas_exe), r, perr) && r == 1u << 20);
        }
        CHECK(std::any_of(log.begin(), log.end(), [](const std::string& l) { return contains(l, "stapel: ") && contains(l, "-> 16 MiB"); }));
        // Een volgende start gaat van het origineel uit (de -orig-koppeling) en schrijft niets.
        CHECK(plan_direct("Spel", p, o, sp, w) == Verdict::Ok && sp.stack_src == fs::canonical(nas_exe).string());
        log.clear();
        CHECK(apply_direct(sp, p, log, err) && contains(log.back(), "is al actueel"));
        // Is de exe zelf al groot genoeg, dan blijft alles zoals het is.
        write(ugi, "[Identify]\nSize = 1024\n[Launch]\nStackReserve = 1M\n");
        CHECK(plan_direct("Spel", p, o, sp, w) == Verdict::Ok);
        log.clear();
        CHECK(apply_direct(sp, p, log, err) && contains(log.back(), "heeft al SizeOfStackReserve"));
        // Weigeren: een onzinnige waarde, en een game buiten een view (de kopie zou naast het origineel komen).
        write(ugi, "[Identify]\nSize = 1024\n[Launch]\nStackReserve = 1G\n");
        CHECK(plan_direct("Spel", p, o, sp, w) == Verdict::Refused && contains(w, "StackReserve = 1G"));
        DirectPlan buiten = sp;
        buiten.stack_reserve = 16ll << 20;
        buiten.stack_src = nas_exe.string();
        buiten.stack_dest = nas_exe.string();
        buiten.dropin_files.clear();
        buiten.files.clear();
        log.clear();
        CHECK(!apply_direct(buiten, p, log, err) && contains(err, "NAS") && pe_stack_reserve(slurp(nas_exe), r, perr) && r == 1u << 20);
        // Opruimen voor de rest van de test.
        fs::remove(ugi);
        fs::remove(vexe);
        fs::remove(view / "bin/spel.exe.ravengun-orig");
        write(vexe, oud);
    }

    // [Launch] ViewLinks: een bestand in de view wordt een symlink naar een ander bestand van de game; de mappen op de weg worden lokaal
    // en de NAS blijft gelijk (Mario Kart DX: Polygon2D_D3D10.fx -> Polygon2D_D3D10.cfx).
    {
        const fs::path nas_data = home / "mnt/nas/spel/Data";
        write(nas_data / "Shader/a.fx", "bron");
        write(nas_data / "Shader/a.cfx", "DXBC-voorgecompileerd");
        write(nas_data / "Shader/b.cfx", "ander");
        const fs::path vdata = view / "bin/Data";
        fs::create_symlink(nas_data, vdata);
        const fs::path ugi = fs::path(p.dirs.user) / "Spel/game.ini";
        write(ugi, "[Launch]\nViewLinks = Data/Shader/a.fx -> Data/Shader/a.cfx\n");
        DirectPlan vp;
        std::string w;
        Verdict vv = plan_direct("Spel", p, o, vp, w);
        CHECK(vv == Verdict::Ok);
        if (vv != Verdict::Ok) std::fprintf(stderr, "viewlinkplan: %s\n", w.c_str());
        const fs::path link = vdata / "Shader/a.fx";
        CHECK(vp.view_links.size() == 1 && vp.view_links[0].first == link.string() && vp.view_links[0].second == (vdata / "Shader/a.cfx").string());
        {
            DirectPlan back;
            std::string err;
            CHECK(plan_from_json(plan_to_json(vp), back, err) && back.view_links == vp.view_links);
        }
        std::vector<std::string> log;
        std::string err;
        CHECK(apply_direct(vp, p, log, err));
        CHECK(!fs::is_symlink(vdata) && !fs::is_symlink(vdata / "Shader") && fs::is_symlink(link));
        CHECK(fs::read_symlink(link) == fs::canonical(nas_data / "Shader/a.cfx") && slurp(link) == "DXBC-voorgecompileerd");
        CHECK(slurp(nas_data / "Shader/a.fx") == "bron" && fs::is_symlink(vdata / "Shader/b.cfx"));
        CHECK(std::any_of(log.begin(), log.end(), [](const std::string& l) { return contains(l, "viewlink: ") && contains(l, "alleen in de view"); }));
        log.clear();
        CHECK(apply_direct(vp, p, log, err) && std::any_of(log.begin(), log.end(), [](const std::string& l) { return contains(l, "is al actueel (->"); }));
        // Een ander doel: de link wordt omgezet.
        write(ugi, "[Launch]\nViewLinks = Data/Shader/a.fx -> Data/Shader/b.cfx\n");
        CHECK(plan_direct("Spel", p, o, vp, w) == Verdict::Ok);
        CHECK(apply_direct(vp, p, log, err) && slurp(link) == "ander");
        // Een doel dat er niet is: niet starten.
        write(ugi, "[Launch]\nViewLinks = Data/Shader/a.fx -> Data/Shader/weg.cfx\n");
        CHECK(plan_direct("Spel", p, o, vp, w) == Verdict::Ok);
        CHECK(!apply_direct(vp, p, log, err) && contains(err, "bestaat niet"));
        // Weigeren: buiten de gamemap, geen pijl, link gelijk aan doel.
        for (const char* fout : {"../x -> a", "/abs -> a", "a.fx", "a -> ./a", "a -> b -> c"}) {
            write(ugi, std::string("[Launch]\nViewLinks = ") + fout + "\n");
            CHECK(plan_direct("Spel", p, o, vp, w) == Verdict::Refused && contains(w, "ViewLinks"));
        }
        std::vector<std::pair<std::string, std::string>> twee;
        std::string perr;
        CHECK(parse_view_links("x\\y.fx -> y.cfx, , a -> b", "/v", twee, perr) && twee.size() == 2 && twee[0].first == "/v/x/y.fx" &&
              twee[1].second == "/v/b");
        fs::remove(ugi);
        fs::remove_all(vdata);
        fs::remove_all(nas_data);
    }

    // [Launch] ViewCopies: een link in de view wordt een lokale kopie; de NAS blijft gelijk en een lokaal bestand blijft staan
    // (Mars Sortie: GameRecords1/2.dat).
    {
        const fs::path nas_rec = home / "mnt/nas/spel/GameRecords1.dat";
        write(nas_rec, "records van de NAS");
        const fs::path vrec = view / "bin/GameRecords1.dat";
        fs::create_symlink(nas_rec, vrec);
        const fs::path ugi = fs::path(p.dirs.user) / "Spel/game.ini";
        write(ugi, "[Launch]\nViewCopies = GameRecords1.dat\n");
        DirectPlan vp;
        std::string w;
        CHECK(plan_direct("Spel", p, o, vp, w) == Verdict::Ok && vp.view_copies == std::vector<std::string>{vrec.string()});
        {
            DirectPlan back;
            std::string err;
            CHECK(plan_from_json(plan_to_json(vp), back, err) && back.view_copies == vp.view_copies);
        }
        std::vector<std::string> log;
        std::string err;
        CHECK(apply_direct(vp, p, log, err));
        CHECK(!fs::is_symlink(vrec) && slurp(vrec) == "records van de NAS" && slurp(nas_rec) == "records van de NAS");
        write(vrec, "stand van de game");
        log.clear();
        CHECK(apply_direct(vp, p, log, err) && slurp(vrec) == "stand van de game" && slurp(nas_rec) == "records van de NAS");
        CHECK(std::any_of(log.begin(), log.end(), [](const std::string& l) { return contains(l, "al een lokaal bestand"); }));
        for (const char* fout : {"../x", "/abs"}) {
            write(ugi, std::string("[Launch]\nViewCopies = ") + fout + "\n");
            CHECK(plan_direct("Spel", p, o, vp, w) == Verdict::Refused && contains(w, "ViewCopies"));
        }
        fs::remove(ugi);
        fs::remove(vrec);
        fs::remove_all(home / "mnt/nas/spel/GameRecords1.dat");
    }

    // [Launch] Dxvk: de DLL's van DXVK (bitbreedte van de game; Spel is 64 bits) als drop-ins in de view met <naam>=n. Bron: eerst
    // RAVENGUN_DXVK, dan ~/.local/share/ravengun/dxvk/current, dan de nieuwste Proton van Steam; niets gevonden = niet starten.
    {
        const fs::path ugi = fs::path(p.dirs.user) / "Spel/game.ini";
        const char* dlls[] = {"d3d8.dll", "d3d9.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll"};
        auto vul = [&](const fs::path& dir, const std::string& merk) { for (const char* d : dlls) write(dir / d, merk + d); };
        write(ugi, "[Launch]\nDxvk = true\n");
        DirectPlan dp;
        std::string w;
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "fetch-dxvk.sh") && contains(w, "64 bits"));
        // Steam: twee Protons; de nieuwste DXVK-versie wint, niet de naam.
        const fs::path steam = home / ".local/share/Steam";
        vul(steam / "compatibilitytools.d/GE-Proton10-4/files/lib/wine/dxvk/x86_64-windows", "oud-");
        write(steam / "compatibilitytools.d/GE-Proton10-4/files/lib/wine/dxvk/version", "aaa dxvk (v2.6-12-gaaa)\n");
        vul(steam / "steamapps/common/Proton 10.0/files/lib/wine/dxvk/x86_64-windows", "nieuw-");
        write(steam / "steamapps/common/Proton 10.0/files/lib/wine/dxvk/version", "bbb dxvk (v2.7.1-509-gbbb)\n");
        vul(steam / "steamapps/common/Proton 10.0/files/lib/wine/dxvk/i386-windows", "x32-");
        Verdict dv = plan_direct("Spel", p, o, dp, w);
        CHECK(dv == Verdict::Ok);
        if (dv != Verdict::Ok) std::fprintf(stderr, "dxvkplan: %s\n", w.c_str());
        CHECK(contains(dp.dxvk_dir, "Proton 10.0") && contains(dp.dxvk_dir, "x86_64-windows") && contains(dp.dxvk_from, "v2.7.1-509"));
        const std::string ov = dp.env.count("WINEDLLOVERRIDES") ? dp.env.at("WINEDLLOVERRIDES") : std::string();
        for (const char* n : {"d3d8=n", "d3d9=n", "d3d10core=n", "d3d11=n", "dxgi=n"}) CHECK(contains(ov, n));
        const fs::path vbin = view / "bin";
        CHECK(std::any_of(dp.dropin_files.begin(), dp.dropin_files.end(), [&](const DropinFile& f) {
            return f.dest == (vbin / "d3d11.dll").string() && contains(f.src, "Proton 10.0");
        }));
        CHECK(std::any_of(dp.dropin_lines.begin(), dp.dropin_lines.end(), [](const std::string& l) { return l.rfind("dxvk: ", 0) == 0; }));
        // RAVENGUN_DXVK gaat voor Steam; een onvolledige map telt niet.
        const fs::path eigen = root / "dxvk-eigen";
        vul(eigen / "x64", "eigen-");
        write(eigen / "version", "dxvk 9.9 (proef)\n");
        DirectOptions o2 = o;
        o2.base_env["RAVENGUN_DXVK"] = eigen.string();
        CHECK(plan_direct("Spel", p, o2, dp, w) == Verdict::Ok && dp.dxvk_dir == (eigen / "x64").string() && contains(dp.dxvk_from, "9.9"));
        fs::remove(eigen / "x64/dxgi.dll");
        CHECK(plan_direct("Spel", p, o2, dp, w) == Verdict::Ok && contains(dp.dxvk_dir, "Proton 10.0"));
        // Een eigen drop-in met dezelfde naam wint van DXVK.
        write(fs::path(p.dirs.user) / "Spel/dropin/dropin.ini", "[d3d9.dll]\nTo = .\nDllOverride = builtin,native\n");
        write(fs::path(p.dirs.user) / "Spel/dropin/d3d9.dll", "eigen d3d9");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Ok);
        CHECK(std::count_if(dp.dropin_files.begin(), dp.dropin_files.end(), [](const DropinFile& f) { return f.name == "d3d9.dll"; }) == 1);
        CHECK(dp.env.count("WINEDLLOVERRIDES") && contains(dp.env.at("WINEDLLOVERRIDES"), "d3d9=n,b"));
        fs::remove_all(fs::path(p.dirs.user) / "Spel/dropin");
        // Uit, en een onzinnige waarde.
        write(ugi, "[Launch]\nDxvk = false\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Ok && dp.dxvk_dir.empty());
        write(ugi, "[Launch]\nDxvk = misschien\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "Dxvk = misschien"));
        // [Launch] Gst32: de 32-bits GStreamer uit <data>/gst32 (of RAVENGUN_GST32) in LD_LIBRARY_PATH en het plug-inpad.
        write(ugi, "[Launch]\nGst32 = true\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "setup-gst32.sh"));
        const fs::path gst = root / "gst32";
        write(gst / "proton/libgstreamer-1.0.so.0", "x");
        fs::create_directories(gst / "plugins");
        fs::create_directories(gst / "lib");
        DirectOptions o3 = o;
        o3.base_env["RAVENGUN_GST32"] = gst.string();
        o3.base_env["LD_LIBRARY_PATH"] = "/al/eerder";
        CHECK(plan_direct("Spel", p, o3, dp, w) == Verdict::Ok);
        CHECK(dp.env.count("LD_LIBRARY_PATH") && dp.env.at("LD_LIBRARY_PATH") == (gst / "proton").string() + ":" + (gst / "lib").string() + ":/al/eerder");
        CHECK(dp.env.count("GST_PLUGIN_SYSTEM_PATH_1_0") && dp.env.at("GST_PLUGIN_SYSTEM_PATH_1_0") == (gst / "plugins").string());
        CHECK(dp.launch_env.count("GST_REGISTRY_1_0"));
        write(ugi, "[Launch]\nGst32 = soms\n");
        CHECK(plan_direct("Spel", p, o3, dp, w) == Verdict::Refused && contains(w, "Gst32 = soms"));
        fs::remove_all(gst);
        // [Launch] NativeDlls: uit <data>/redist/<x64> (Spel is 64 bits) naar system32 van de prefix, met <naam>=n; de ingebouwde
        // van Wine wordt bewaard; een tweede keer schrijft niets.
        write(ugi, "[Launch]\nNativeDlls = XactEngine3_3.dll\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "setup-redist.sh"));
        const fs::path rd = root / "redist";
        write(rd / "x64/xactengine3_3.dll", "MZ native");
        DirectOptions o4 = o;
        o4.base_env["RAVENGUN_REDIST"] = rd.string();
        CHECK(plan_direct("Spel", p, o4, dp, w) == Verdict::Ok);
        CHECK(dp.native_dlls.size() == 1 && dp.native_dlls[0].first == (rd / "x64/xactengine3_3.dll").string() &&
              contains(dp.native_dlls[0].second, "drive_c/windows/system32/xactengine3_3.dll"));
        CHECK(dp.env.count("WINEDLLOVERRIDES") && contains(dp.env.at("WINEDLLOVERRIDES"), "xactengine3_3=n"));
        {
            DirectPlan back;
            std::string jerr;
            CHECK(plan_from_json(plan_to_json(dp), back, jerr) && back.native_dlls == dp.native_dlls);
        }
        write(dp.native_dlls[0].second, "Wine builtin");
        std::vector<std::string> nlog;
        std::string nerr;
        DirectPlan alleen = dp;
        alleen.files.clear();
        alleen.dropin_files.clear();
        alleen.view_links.clear();
        alleen.stack_reserve = 0;
        CHECK(apply_direct(alleen, p, nlog, nerr));
        CHECK(slurp(dp.native_dlls[0].second) == "MZ native" && slurp(dp.native_dlls[0].second + ".wine-bewaard") == "Wine builtin");
        nlog.clear();
        CHECK(apply_direct(alleen, p, nlog, nerr) && std::any_of(nlog.begin(), nlog.end(), [](const std::string& l) { return contains(l, "al actueel"); }));
        write(ugi, "[Launch]\nNativeDlls = ../x\n");
        CHECK(plan_direct("Spel", p, o4, dp, w) == Verdict::Refused && contains(w, "geen DLL-naam"));
        fs::remove_all(rd);
        // [Launch] PhysX: zonder de PhysX-systeemsoftware in de prefix geen start (de melding noemt het script), met wel.
        write(ugi, "[Launch]\nPhysX = true\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "setup-physx.sh"));
        write(ugi, "[Launch]\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Ok && !dp.prefix.empty());
        const fs::path physx = fs::path(dp.prefix) / "drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.1/PhysXCore.dll";
        write(physx, "MZ physx");
        write(ugi, "[Launch]\nPhysX = true\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Ok);
        write(ugi, "[Launch]\nPhysX = soms\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "PhysX = soms"));
        fs::remove(physx);
        // [Launch] Registry: een .reg uit de map van de game (gebruikerslaag eerst) in het plan; ontbreekt hij, of is het geen
        // .reg binnen de map, dan geen start.
        write(ugi, "[Launch]\nRegistry = data/wmv.reg\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "data/wmv.reg staat niet in patches/Spel/"));
        const fs::path regfile = fs::path(p.dirs.user) / "Spel/data/wmv.reg";
        write(regfile, "Windows Registry Editor Version 5.00\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Ok && dp.reg_files == std::vector<std::string>{regfile.string()});
        {
            DirectPlan back;
            std::string jerr;
            CHECK(plan_from_json(plan_to_json(dp), back, jerr) && back.reg_files == dp.reg_files);
        }
        write(ugi, "[Launch]\nRegistry = ../wmv.reg\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "geen .reg-bestand"));
        write(ugi, "[Launch]\nRegistry = data/wmv.txt\n");
        CHECK(plan_direct("Spel", p, o, dp, w) == Verdict::Refused && contains(w, "geen .reg-bestand"));
        fs::remove_all(fs::path(p.dirs.user) / "Spel/data");
        // In gamescope (cap_sys_nice: secure-execution) komen LD_LIBRARY_PATH en LD_PRELOAD niet door; env zet ze achter "--" terug.
        write(ugi, "[Launch]\n");
        DirectOptions o5 = o;
        o5.base_env["TP_GAMESCOPE"] = "1";
        o5.base_env["RG_GAMESCOPE_RES"] = "1920x1080";
        o5.base_env["LD_LIBRARY_PATH"] = "/gst/lib";
        CHECK(plan_direct("Spel", p, o5, dp, w) == Verdict::Ok && dp.gamescope);
        {
            const auto it = std::find(dp.argv.begin(), dp.argv.end(), "--");
            CHECK(it != dp.argv.end() && it + 2 < dp.argv.end() && *(it + 1) == "/usr/bin/env" && *(it + 2) == "LD_LIBRARY_PATH=/gst/lib");
            CHECK(!dp.starts.empty() && dp.starts[0].argv == dp.argv);
        }
        o5.base_env.erase("LD_LIBRARY_PATH");
        // De interne resolutie van gamescope ([Display] GamescopeGameResolution): -w/-h voor games met een vaste modus.
        CHECK(std::find(dp.argv.begin(), dp.argv.end(), "-w") == dp.argv.end());
        o5.base_env["RG_GAMESCOPE_GAME_RES"] = "1360x768";
        CHECK(plan_direct("Spel", p, o5, dp, w) == Verdict::Ok);
        {
            auto seq = [&](const std::vector<std::string>& q) { return std::search(dp.argv.begin(), dp.argv.end(), q.begin(), q.end()) != dp.argv.end(); };
            CHECK(seq({"-W", "1920", "-H", "1080"}) && seq({"-w", "1360", "-h", "768"}));
        }
        o5.base_env["RG_GAMESCOPE_GAME_RES"] = "groot";
        CHECK(plan_direct("Spel", p, o5, dp, w) == Verdict::Ok && std::find(dp.argv.begin(), dp.argv.end(), "-w") == dp.argv.end());
        o5.base_env.erase("RG_GAMESCOPE_GAME_RES");
        CHECK(plan_direct("Spel", p, o5, dp, w) == Verdict::Ok);
        if (!dp.env.count("LD_LIBRARY_PATH") && !dp.env.count("LD_PRELOAD") && !dp.env.count("LD_AUDIT"))
            CHECK(std::find(dp.argv.begin(), dp.argv.end(), "/usr/bin/env") == dp.argv.end());
        fs::remove(ugi);
        fs::remove_all(steam);
        fs::remove_all(eigen);
    }

    // De invoer: evdev met een indeling wordt tp-shm; zonder indeling via het script.
    DirectInput in;
    CHECK(direct_input("Spel", p, in, why) == Verdict::Ok && in.route == "tp-shm" && !in.note.empty());
    CHECK(launch_mode("Spel", p) == "direct");
    const fs::path user = fs::path(p.dirs.user) / "Spel/game.ini";
    write(user, "[Input]\nLayout =\n");
    CHECK(direct_input("Spel", p, in, why) == Verdict::Fallback && contains(why, "evdev"));
    // jvs: over de pipe (het oude .env zet RG_JVS_TRANSPORT) door de engine, over een COM-poort via het script.
    write(user, "[Input]\nRoute = jvs\nBoard = LGI\n");
    CHECK(direct_input("Spel", p, in, why) == Verdict::Fallback && contains(why, "COM-poort"));
    write(fs::path(p.tp) / "UserProfiles/Spel.env", "RG_JVS_TRANSPORT=pipe\nRG_WINEPREFIX=" + p.home + "/.local/share/ravengun/prefixes/eigen  # oud\n");
    CHECK(direct_input("Spel", p, in, why) == Verdict::Ok && in.route == "jvs" && in.board == "LGI" && in.transport == "pipe");
    CHECK(profile_env("Spel", p).at("RG_WINEPREFIX") == p.home + "/.local/share/ravengun/prefixes/eigen");
    // Een ongeldige gebruikerslaag telt niet mee (de meegeleverde route geldt).
    write(user, "[Input]\nRoute = onzin\n");
    CHECK(direct_input("Spel", p, in, why) == Verdict::Ok && in.route == "tp-shm");
    fs::remove(fs::path(p.tp) / "UserProfiles/Spel.env");

    // Weigeren (launch-direct.py exit 5) en via het script.
    write(user, "[Game]\nDongleRegion = EUR\n");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "Instelling geweigerd") && contains(why, "JAPAN, USA"));
    write(user, "[Loader]\nBuild = eigen\n");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "kies teknoparrot of ravengun"));
    fs::rename(own, own.string() + ".weg");
    write(user, "[Game]\nDongleRegion =\n");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "eigen OpenParrot-build ontbreekt"));
    fs::rename(own.string() + ".weg", own);
    write(user, "[Game]\nHasTwoExecutables = true\n");
    // Twee exe's zonder tweede pad: launch-direct.py zegt "niet direct te starten" (de engine plant twee exe's zelf, zie hieronder).
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Fallback && contains(why, "Niet direct te starten") && contains(why, "de tweede is niet gevonden: ''"));
    // [Launch] Drives en Exe: een schijfletter voor de run en het spel vanaf die letter (Far Cry Paradise Lost: W:).
    write(user, "");
    {
        DirectPlan basis;
        CHECK(plan_direct("Spel", p, o, basis, why) == Verdict::Ok);
        std::string exe_naam = fs::relative(basis.game, view).string();   // relatief aan de view-wortel (X: = .)
        std::replace(exe_naam.begin(), exe_naam.end(), '/', '\\');
        fs::create_directories(fs::path(basis.prefix) / "dosdevices");
        write(user, "[Launch]\nDrives = X=.\nExe = x:\\" + exe_naam + "\n");
        const Verdict dv = plan_direct("Spel", p, o, pl, why);
        CHECK(dv == Verdict::Ok);
        if (dv != Verdict::Ok) std::fprintf(stderr, "drives/exe: %s\n", why.c_str());
        CHECK(pl.drives.size() == 1 && pl.drives[0].letter == "X" && !pl.drives[0].present &&
              pl.drives[0].link == (fs::path(basis.prefix) / "dosdevices/x:").string());
        CHECK(pl.exe_override == "X:\\" + exe_naam && std::find(pl.argv.begin(), pl.argv.end(), "X:\\" + exe_naam) != pl.argv.end());
        {
            DirectPlan back;
            std::string jerr;
            CHECK(plan_from_json(plan_to_json(pl), back, jerr) && back.exe_override == pl.exe_override && back.drives.size() == 1 &&
                  back.drives[0].link == pl.drives[0].link);
        }
        // Een letter die al naar dezelfde map wijst blijft staan (present); een die ergens anders heen wijst: niet starten.
        // (Uit tests/test_exe_override.py, toen launch-direct.py de tweeling was.)
        const fs::path xlink = fs::path(basis.prefix) / "dosdevices/x:";
        fs::create_directory_symlink(view, xlink);
        write(user, "[Launch]\nDrives = X=.\n");
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Ok && pl.drives.size() == 1 && pl.drives[0].present);
        fs::remove(xlink);
        fs::create_directory_symlink(root, xlink);
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "bestaat al in de prefix"));
        fs::remove(xlink);
        write(user, "[Launch]\nDrives = C=.\n");
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "niet C"));
        write(user, "[Launch]\nDrives = X=.\nExe = Y:\\spel.exe\n");
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "bestaat niet in de prefix"));
        write(user, "[Launch]\nDrives = X=.\nExe = X:\\bestaat-niet.exe\n");
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "bestaat niet"));
    }
    write(user, "[Game]\nProfileRevision = 2\n");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Fallback && contains(why, "profielrevisie 2"));
    write(user, "[Game]\nEmulationProfile = NamcoPokken\n");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Fallback && contains(why, "Niet direct te starten"));
    write(user, "");
    write(fs::path(p.dirs.user) / "Spel/patches.ini", "[Patch: test]\nEnabled = true\nKind = file\nCategory = loader\nTarget = loader\n");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Fallback && contains(why, "patch 'test' staat aan"));
    write(fs::path(p.dirs.user) / "Spel/patches.ini", "[Patch: test]\nEnabled = true\nKind = file\n");   // geen Category: afgewezen en aan
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "Patch geweigerd"));
    write(fs::path(p.dirs.user) / "Spel/patches.ini", "[Patch: test]\nEnabled = false\nKind = file\nCategory = loader\nTarget = loader\n");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Ok);
    CHECK(pl.patch_lines.size() == 1 && pl.patch_lines[0] == "overgeslagen 'test' @loader [uit]");
    // Een drop-in die buiten de view zou komen: weigeren.
    write(fs::path(p.dirs.user) / "Spel/dropin/dropin.ini", "[weg.dll]\nTo = ../..\n");
    write(fs::path(p.dirs.user) / "Spel/dropin/weg.dll", "x");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "Drop-in geweigerd") && contains(why, "buiten de view"));
    fs::remove_all(fs::path(p.dirs.user) / "Spel/dropin");
    // Preload onder de OpenParrot-lader: die laadt via TP_PRELOAD_DLL precies een DLL. Twee preloads, of een TP_PRELOAD_DLL die al
    // ergens anders heen wijst: niet starten, in plaats van zonder drop-in door te gaan (uit tests/test_dropin_materialiseer.py).
    write(fs::path(p.dirs.user) / "Spel/dropin/dropin.ini", "[twee.dll]\nTo = .\nPreload = true\n");
    write(fs::path(p.dirs.user) / "Spel/dropin/twee.dll", "MZtwee");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "precies een"));
    if (!contains(why, "precies een")) std::fprintf(stderr, "twee preloads: %s\n", why.c_str());
    fs::remove_all(fs::path(p.dirs.user) / "Spel/dropin");
    write(user, "[Env]\nTP_PRELOAD_DLL = Z:\\\\anders.dll\n");
    CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "TP_PRELOAD_DLL staat al op"));
    if (!contains(why, "TP_PRELOAD_DLL staat al op")) std::fprintf(stderr, "TP_PRELOAD_DLL al gezet: %s\n", why.c_str());
    // [Loader] Core = ravengun bij EmulatorType TeknoParrot: onze kern in plaats van TeknoParrot64.dll als eerste argument van de
    // lader; ontbreekt hij, dan niet starten met de bouwhint (uit tests/launch_direct_test.sh).
    {
        const fs::path own_core = fs::path(p.data) / "openparrot/current/OpenParrotx64/OpenParrot64.dll";
        write(own_core, "x");
        write(user, "[Loader]\nCore = ravengun\n");
        const Verdict cv = plan_direct("Spel", p, o, pl, why);
        CHECK(cv == Verdict::Ok);
        if (cv != Verdict::Ok) std::fprintf(stderr, "Core = ravengun: %s\n", why.c_str());
        CHECK(pl.argv.size() > 2 && pl.argv[1] == own.string() && pl.argv[2] == wine_path((fs::path(p.data) / "openparrot/current/OpenParrotx64/OpenParrot64").string()));
        CHECK(pl.core_source == "ravengun" && pl.core_dll == own_core.string());
        fs::remove(own_core);
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "eigen OpenParrot-build ontbreekt") && contains(why, "build-openparrot.sh"));
    }
    write(user, "");

    // --- de besturingspipe ([Input] ControlPipe): het ALLS USB-IO-rapport naar \\.\pipe\TeknoParrotPipe ----------------------------
    {
        CHECK(pl.control_pipe.empty() && pl.control_size == 0);   // zonder ControlPipe: geen besturingspipe
        write(user, "[Input]\nControlPipe = AllsUsbIo\n");
        CHECK(direct_input("Spel", p, in, why) == Verdict::Ok && in.route == "tp-shm" && in.control_pipe == "AllsUsbIo");   // evdev + Layout
        o.control_source = (root / "control").string();
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Ok);
        CHECK(pl.control_pipe == "AllsUsbIo" && pl.control_pipe_name == "TeknoParrotPipe" && pl.control_source == (root / "control").string());
        CHECK(pl.control_size == 64 && pl.control_interval_ms == 15);
        DirectPlan back;
        std::string err;
        CHECK(plan_from_json(plan_to_json(pl), back, err) && back.control_pipe == "AllsUsbIo" && back.control_pipe_name == "TeknoParrotPipe" &&
              back.control_source == pl.control_source && back.control_size == 64 && back.control_interval_ms == 15);
        DirectPlan kapot = pl;
        kapot.control_size = 0;
        CHECK(!plan_from_json(plan_to_json(kapot), back, err) && contains(err, "besturingspipe"));
        DirectOptions o2 = o;
        o2.control_source.clear();
        CHECK(plan_direct("Spel", p, o2, pl, why) == Verdict::Ok && pl.control_source == "/dev/shm/ravengun_ControlPipe");
        // Een onbekende indeling weigert (zoals launch-direct.py: Instelling geweigerd).
        write(user, "[Input]\nControlPipe = Bestaatniet\n");
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Refused && contains(why, "Instelling geweigerd") && contains(why, "ControlPipe = Bestaatniet"));
        // ControlPipe hoort bij tp-shm: bij route jvs is de gebruikerslaag ongeldig (de meegeleverde route geldt, zonder pipe).
        write(user, "[Input]\nRoute = jvs\nBoard = LGI\nControlPipe = AllsUsbIo\n");
        CHECK(direct_input("Spel", p, in, why) == Verdict::Ok && in.route == "tp-shm" && in.control_pipe.empty());
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Ok && pl.control_pipe.empty());
        write(user, "");
    }

    // --- twee exe's (HasTwoExecutables, zoals HOTDSD): amdaemon.exe eerst, wachten, dan het spel ------------------------------------
    write(fs::path(p.dirs.shipped) / "Twee/game.ini",
          "[Game]\nProfile = Twee\nType = TeknoParrot\nEmulationProfile = ALLSHOTDSD\nExecutable = Spel-Win64-Shipping-lang.exe\n"
          "Executable2 = amdaemon.exe\nIs64Bit = true\nHasTwoExecutables = true\nLaunchSecondExecutableFirst = true\n\n"
          "[Launch]\nArguments = -UNATTENDED\nMode = direct\n\n[Input]\nRoute = evdev\nLayout = HOTDSDPipe\n\n"
          "[Identify]\nPath = App/bin/Spel-Win64-Shipping-lang.exe\nPath2 = App/daemon/amdaemon.exe\n");
    const fs::path tview = fs::path(p.views()) / "Twee";
    const std::string tgame = (tview / "App/bin/Spel-Win64-Shipping-lang.exe").string(), tdaemon = (tview / "App/daemon/amdaemon.exe").string();
    write(tgame, "MZ");
    write(tdaemon, "MZ");
    {
        CHECK(plan_direct("Twee", p, o, pl, why) == Verdict::Ok);
        if (!pl.starts.empty() && pl.starts.size() != 2) std::fprintf(stderr, "twee: %s\n", why.c_str());
        CHECK(pl.starts.size() == 2);
        if (pl.starts.size() == 2) {
            const DirectStart &a = pl.starts[0], &b = pl.starts[1];
            const std::string ldr = p.tp + "/OpenParrotx64/OpenParrotLoader64.exe", kern = wine_path(p.tp + "/TeknoParrot/TeknoParrot64");
            CHECK(a.role == "second" && a.name == "amdaemon.exe" && a.exe == tdaemon && a.wait_before == 0.0 && !a.gamescope);
            CHECK((a.argv == std::vector<std::string>{wine.string(), ldr, kern, wine_path(tdaemon)}));   // geen argumenten van het spel
            CHECK((a.comms == std::vector<std::string>{"amdaemon.exe"}));
            CHECK(b.role == "main" && b.name == "Spel-Win64-Shipping-lang.exe" && b.wait_before == 5.0 && b.argv == pl.argv);
            CHECK((b.argv == std::vector<std::string>{wine.string(), ldr, kern, wine_path(tgame), "-UNATTENDED"}));
            CHECK((b.comms == std::vector<std::string>{"spel-win64-ship"}));
            CHECK(a.cwd == p.tp && b.cwd == p.tp);
        }
        // teknoparrot.ini ook naast de tweede exe (dezelfde inhoud); de procesnamen van het plan dekken beide.
        CHECK(pl.files.size() == 2 && pl.files[1].first == (tview / "App/daemon/teknoparrot.ini").string() && pl.files[1].second == pl.files[0].second);
        CHECK(std::find(pl.game_comms.begin(), pl.game_comms.end(), "amdaemon.exe") == pl.game_comms.end());   // zoals launch-direct.py
        const auto all = plan_comms(pl);
        CHECK(std::find(all.begin(), all.end(), "amdaemon.exe") != all.end() && std::find(all.begin(), all.end(), "spel-win64-ship") != all.end());
        DirectPlan back;
        std::string err;
        CHECK(plan_from_json(plan_to_json(pl), back, err) && back.starts.size() == 2 && back.starts[0].argv == pl.starts[0].argv &&
              back.starts[1].wait_before == 5.0 && back.starts[0].comms == pl.starts[0].comms && back.starts[1].role == "main");
        const fs::path tuser = fs::path(p.dirs.user) / "Twee/game.ini";
        write(tuser, "[Game]\nLaunchSecondExecutableFirst = false\n\n[Launch]\nSecondWait = 0,5\nSecondArguments = -x \"a b\"\n");
        CHECK(plan_direct("Twee", p, o, pl, why) == Verdict::Ok && pl.starts.size() == 2 && pl.starts[0].role == "main" &&
              pl.starts[1].role == "second" && pl.starts[1].wait_before == 0.5 && pl.starts[1].argv.back() == "-x \"a b\"");
        write(tuser, "[Launch]\nSecondWait = 700\n");
        CHECK(plan_direct("Twee", p, o, pl, why) == Verdict::Refused && contains(why, "SecondWait = 700"));
        write(tuser, "[Launch]\nSecondWait = vijf\n");
        CHECK(plan_direct("Twee", p, o, pl, why) == Verdict::Refused && contains(why, "SecondWait = vijf"));
        write(tuser, "[Identify]\nPath2 = App/weg/amdaemon.exe\n");
        CHECK(plan_direct("Twee", p, o, pl, why) == Verdict::Fallback && contains(why, "de tweede is niet gevonden") && contains(why, "App/weg/amdaemon.exe"));
        // Een tweede exe buiten de view van de eerste (rechtstreeks een ander pad): niet stil buiten de view draaien.
        write(root / "elders/amdaemon.exe", "MZ");
        write(tuser, "[Game]\nGamePath2 = " + (root / "elders/amdaemon.exe").string() + "\n");
        CHECK(plan_direct("Twee", p, o, pl, why) == Verdict::Fallback && contains(why, "ligt buiten de view"));
        fs::remove(tuser);
    }

    // --- [Launch] View = off: nooit een view, ook niet een die er al staat (Dream Raiders, 2026-10-16) ----------------------------
    {
        const fs::path suser = fs::path(p.dirs.user) / "Spel/game.ini";
        write(suser, "[Launch]\nView = off\n");
        const Verdict vo = plan_direct("Spel", p, o, pl, why);
        CHECK(vo != Verdict::Ok || pl.game_source != "view");   // Spel staat alleen in de view: zonder view geen spel uit de view
        fs::remove(suser);
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Ok && pl.game_source == "view");
    }

    // --- de automatische view ([Launch] View = auto): een game op de (nep-)NAS krijgt bij een start een lokale view -----------------
    // (de boom zelf vergelijkt tests/test_gameview.py met tp-add-game.py --view; hier het plan en het plaatsen van teknoparrot.ini)
    {
        const fs::path ngame = home / "mnt/nas/Nas Spel";
        write(ngame / "bin/nas.exe", "MZnas");
        write(ngame / "bin/teknoparrot.ini", "oud van de NAS");
        write(ngame / "Data/d.bin", "d");
        write(fs::path(p.dirs.shipped) / "Nas/game.ini",
              "[Game]\nProfile = Nas\nType = TeknoParrot\nEmulationProfile = WartranTroopers\nExecutable = nas.exe\nIs64Bit = true\n"
              "GamePath = ~/mnt/nas/Nas Spel/bin/nas.exe\n\n[Launch]\nMode = direct\n\n[Input]\nRoute = evdev\nLayout = WartranTroopers\n\n"
              "[Identify]\nPath = bin/nas.exe\n\n[Config]\nGeneral/Windowed = 1\n\n[ConfigType]\nGeneral/Windowed = Bool\n");
        const fs::path nview = fs::path(p.views()) / "Nas";
        DirectOptions mo = o;
        // Droog (--plan): niets gemaakt, het plan rekent met de NAS en zegt dat de view bij de start komt.
        CHECK(plan_direct("Nas", p, o, pl, why) == Verdict::Ok && pl.game == (ngame / "bin/nas.exe").string() && !fs::exists(nview));
        CHECK(std::any_of(pl.notes.begin(), pl.notes.end(), [](const std::string& l) { return contains(l, "wordt bij de start gemaakt"); }));
        // Een start: de view komt er (wortel = waar [Identify] Path begint), het plan rekent ermee.
        mo.make_view = true;
        CHECK(plan_direct("Nas", p, mo, pl, why) == Verdict::Ok);
        CHECK(pl.game == (nview / "bin/nas.exe").string() && pl.game_source == "override -> view");
        CHECK(fs::is_directory(nview / "bin") && !fs::is_symlink(nview / "bin") && fs::is_symlink(nview / "Data") && fs::is_symlink(nview / "bin/nas.exe"));
        CHECK(pl.files.size() == 1 && pl.files[0].first == (nview / "bin/teknoparrot.ini").string());
        // teknoparrot.ini: de link naar het bestand op de NAS wordt een eigen lokaal bestand; dat op de NAS blijft zoals het was.
        std::vector<std::string> log;
        std::string err;
        CHECK(apply_direct(pl, p, log, err));
        CHECK(!fs::is_symlink(nview / "bin/teknoparrot.ini") && slurp(nview / "bin/teknoparrot.ini") == pl.files[0].second);
        CHECK(slurp(ngame / "bin/teknoparrot.ini") == "oud van de NAS");
        // Een volgende start: de view blijft (het lokale teknoparrot.ini ook), alleen de links worden ververst.
        write(ngame / "bin/nieuw.dll", "MZ");
        CHECK(plan_direct("Nas", p, mo, pl, why) == Verdict::Ok && pl.game == (nview / "bin/nas.exe").string());
        CHECK(fs::is_symlink(nview / "bin/nieuw.dll") && !fs::is_symlink(nview / "bin/teknoparrot.ini"));
        // View = off: de oude route; een ongeldige waarde weigert.
        const fs::path nuser = fs::path(p.dirs.user) / "Nas/game.ini";
        write(nuser, "[Launch]\nView = off\n");
        CHECK(plan_direct("Nas", p, mo, pl, why) == Verdict::Ok && pl.game == (ngame / "bin/nas.exe").string());
        write(nuser, "[Launch]\nView = misschien\n");
        CHECK(plan_direct("Nas", p, mo, pl, why) == Verdict::Refused && contains(why, "View = misschien"));
        write(nuser, "[Launch]\nViewEmptyDirs = ../buiten\n");
        CHECK(plan_direct("Nas", p, mo, pl, why) == Verdict::Refused && contains(why, "buiten de view"));
        // Een lege echte map zonder ViewEmptyDirs (overblijfsel van een eerdere proef) wordt bij de volgende start weer een link
        // (Bug Busters, video/); staat hij wel in ViewEmptyDirs, dan blijft hij leeg.
        fs::remove(nuser);
        fs::remove(nview / "Data");
        fs::create_directory(nview / "Data");
        CHECK(plan_direct("Nas", p, mo, pl, why) == Verdict::Ok && fs::is_symlink(nview / "Data") && slurp(nview / "Data/d.bin") == "d");
        write(nuser, "[Launch]\nViewEmptyDirs = Data\n");
        CHECK(plan_direct("Nas", p, mo, pl, why) == Verdict::Ok && fs::is_directory(nview / "Data") && !fs::is_symlink(nview / "Data") &&
              fs::is_empty(nview / "Data"));
        CHECK(plan_direct("Nas", p, mo, pl, why) == Verdict::Ok && !fs::is_symlink(nview / "Data") && fs::is_empty(nview / "Data"));
        fs::remove(nuser);
        CHECK(!fs::exists(ngame / "Data/teknoparrot.ini") && !fs::exists(ngame / "teknoparrot.ini"));
    }

    // --- rg-direct --run met een nep-wine -----------------------------------------------------------------------------------------
#ifdef RG_DIRECT_BIN
    {
        const fs::path out = root / "out";
        fs::create_directories(out);
        write(wine,
              "#!/usr/bin/env bash\n"
              "case \"$1\" in\n"
              "  */pipehelper.exe|*/rg-jvsbridge.exe)\n"
              "    shift; echo \"$@\" > \"$OUT/helper-args\"\n"
              "    exec 3<>/dev/tcp/127.0.0.1/$3\n"
              "    printf '\\xe0\\xff\\x03\\xf1\\x01\\xf4' >&3\n"
              "    head -c 6 <&3 | od -An -tx1 | tr -d ' \\n' > \"$OUT/helper-reply\"\n"
              // Gaia Attack 4 sluit de pipe na de eerste bordcontrole en opent hem opnieuw: ook die verbinding krijgt antwoord.
              "    exec 3>&-; exec 3<>/dev/tcp/127.0.0.1/$3\n"
              "    printf '\\xe0\\xff\\x03\\xf1\\x01\\xf4' >&3\n"
              "    head -c 6 <&3 | od -An -tx1 | tr -d ' \\n' > \"$OUT/helper-reply2\"\n"
              "    echo \"pipehelper: pipe created\" >&2\n"
              "    exec sleep 30 ;;\n"
              "  *)\n"
              "    pwd > \"$OUT/game-cwd\"; env | sort > \"$OUT/game-env\"\n"
              "    echo \"BudgieLoader 1.0 start\"\n"
              "    printf 'h\\0i\\0\\n\\0'\n"
              "    exit 3 ;;\n"
              "esac\n",
              true);
        o.base_env["OUT"] = out.string();
        CHECK(plan_direct("Spel", p, o, pl, why) == Verdict::Ok);
        pl.state = (root / "state").string();
        const fs::path planf = root / "plan.json";
        write(planf, plan_to_json(pl));
        const fs::path logf = root / "run.log";
        setenv("RG_DIRECT_SHM_PATH", (root / "shm").c_str(), 1);
        write(root / "shm", std::string(64, '\xAB'));   // een vorige run liet rommel achter, ook een sense-byte die aan staat
        write(root / "src", std::string(64, '\x80'));   // en het bronbestand van de daemon bytes van een andere game (TC5 na Rampage)
        setenv("RG_ALLEEN_IN_DE_TEST", "1", 1);   // mag niet in het spel komen: de omgeving komt uit het plan
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, logf.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        std::string bin = RG_DIRECT_BIN;
        std::vector<char*> av{bin.data(), const_cast<char*>("--run"), const_cast<char*>(planf.c_str()), nullptr};
        extern char** environ;
        pid_t pid = 0;
        CHECK(posix_spawn(&pid, bin.c_str(), &fa, nullptr, av.data(), environ) == 0);
        posix_spawn_file_actions_destroy(&fa);
        int st = 0;
        waitpid(pid, &st, 0);
        const std::string log = slurp(logf);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 3);
        CHECK(contains(log, "pipehelper: " + p.tp + "/pipehelper.exe TeknoParrot_JVS 127.0.0.1 "));
        CHECK(contains(log, "JVS-pipe: game verbonden") && contains(log, "helper: pipehelper: pipe created"));
        CHECK(contains(log, "start: cwd=" + p.tp) && contains(log, "BudgieLoader 1.0 start") && contains(log, "\nhi\n"));
        CHECK(contains(log, "lader gestopt (exitcode 3); de game volgen") && contains(log, "klaar (exitcode 3)"));
        CHECK(contains(log, "A11: overgeslagen 0, toegepast 0, koppeling 0, versie 1, fout 0"));
        CHECK(slurp(out / "helper-reply") == "e00003010105");                       // het antwoord op de adrestoekenning
        CHECK(slurp(out / "helper-reply2") == "e00003010105");                      // ook op de tweede verbinding
        CHECK(contains(log, "JVS-pipe: game verbonden (pipehelper via TCP, verbinding 2)"));
        CHECK(slurp(root / "shm").size() == 64 && slurp(root / "shm")[0] == 1);      // verbind-vlag aan
        CHECK(slurp(root / "shm").substr(1) == std::string(63, '\0'));               // de rest op nul: elke start begint schoon
        // Het bronbestand ook op nul (de daemon zet zijn bytes terug), behalve de verbind-vlag: die moet ook daar staan, anders zet de
        // spiegel van de helper hem bij het volgende antwoord weer op 0 (Gaia Attack 4: adres na adres, nooit pollen).
        CHECK(slurp(root / "src").size() == 64 && slurp(root / "src")[0] == 1 && slurp(root / "src").substr(1) == std::string(63, '\0'));
        const std::string args = slurp(out / "helper-args");
        CHECK(contains(args, "TeknoParrot_JvsState 64 " + wine_path((root / "src").string())));
        CHECK(slurp(out / "game-cwd") == p.tp + "\n");
        const std::string genv = "\n" + slurp(out / "game-env");
        CHECK(contains(genv, "\nA=1\n") && contains(genv, "TP_PRELOAD_DLL=") && contains(genv, "WINEDLLOVERRIDES=d3d11=n,b;hook=n\n"));
        CHECK(!contains(genv, "RG_ALLEEN_IN_DE_TEST") && !contains(genv, "TP_PIPEHELPER_OWNER_PID"));
        CHECK(fs::exists(root / "state/a11"));
        if (g_failures) std::fprintf(stderr, "--- rg-direct log ---\n%s\n", log.c_str());
    }
    // Twee exe's: amdaemon.exe eerst (blijft lopen), wachten, dan het spel; na het spel wordt de daemon gestopt. En de halve opstelling:
    // stopt de daemon voordat het spel begint, dan start het spel niet en is de exitcode 7.
    {
        const fs::path out = root / "out2";
        fs::create_directories(out);
        write(wine,
              "#!/usr/bin/env bash\n"
              "case \"$1\" in\n"
              "  */pipehelper.exe|*/rg-jvsbridge.exe)\n"
              "    echo \"pipehelper: pipe created\" >&2\n"
              "    exec sleep 30 ;;\n"
              "esac\n"
              "case \"$*\" in\n"
              "  *amdaemon.exe*)\n"
              "    echo \"daemon $(date +%s%N) $*\" >> \"$OUT/order\"; pwd > \"$OUT/daemon-cwd\"; env | sort > \"$OUT/daemon-env\"\n"
              "    echo \"AM Daemon klaar\"\n"
              "    [ -n \"$DAEMON_STOPT\" ] && exit 9\n"
              "    trap 'kill $k; echo daemon-term >> \"$OUT/order\"; exit 143' TERM\n"
              "    sleep 30 >/dev/null 2>&1 & k=$!; wait ;;\n"
              "  *)\n"
              "    echo \"spel $(date +%s%N) $*\" >> \"$OUT/order\"; env | sort > \"$OUT/game-env\"\n"
              "    echo \"BudgieLoader 1.0 start\"\n"
              "    exit 3 ;;\n"
              "esac\n",
              true);
        auto run = [&](const std::string& extra_ini, std::string& log) {
            write(fs::path(p.dirs.user) / "Twee/game.ini", "[Launch]\nSecondWait = 0.5\n" + extra_ini);
            o.base_env["OUT"] = out.string();
            DirectPlan tp;
            std::string w;
            if (plan_direct("Twee", p, o, tp, w) != Verdict::Ok) { std::fprintf(stderr, "plan Twee: %s\n", w.c_str()); return -1; }
            tp.state = (root / "state2").string();
            const fs::path planf = root / "plan2.json", logf = root / "run2.log";
            write(planf, plan_to_json(tp));
            posix_spawn_file_actions_t fa;
            posix_spawn_file_actions_init(&fa);
            posix_spawn_file_actions_addopen(&fa, 1, logf.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            posix_spawn_file_actions_adddup2(&fa, 1, 2);
            std::string bin = RG_DIRECT_BIN;
            std::vector<char*> av{bin.data(), const_cast<char*>("--run"), const_cast<char*>(planf.c_str()), nullptr};
            extern char** environ;
            pid_t pid = 0;
            if (posix_spawn(&pid, bin.c_str(), &fa, nullptr, av.data(), environ) != 0) return -1;
            posix_spawn_file_actions_destroy(&fa);
            int st = 0;
            waitpid(pid, &st, 0);
            log = slurp(logf);
            return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        };
        std::string log;
        CHECK(run("", log) == 3);
        const std::string order = slurp(out / "order");
        const size_t d = order.find("daemon "), s = order.find("spel "), term = order.find("daemon-term");
        CHECK(d != std::string::npos && s != std::string::npos && d < s && term != std::string::npos && s < term);   // daemon, spel, daemon gestopt
        if (d != std::string::npos && s != std::string::npos) {
            const long long td = std::atoll(order.c_str() + d + 7), ts = std::atoll(order.c_str() + s + 5);
            CHECK(ts - td >= 400000000LL);   // minstens de wachttijd (0,5 s) tussen de twee starts
        }
        CHECK(contains(order, "amdaemon.exe") && contains(order, "-UNATTENDED") && !contains(order.substr(0, s), "-UNATTENDED"));
        CHECK(slurp(out / "daemon-cwd") == p.tp + "\n");
        CHECK(slurp(out / "daemon-env") == slurp(out / "game-env"));   // dezelfde omgeving voor elke start
        CHECK(contains(log, "start 1/2: amdaemon.exe (tweede exe) -- ") && contains(log, "wachten: 0.5 s; daarna alleen verder"));
        CHECK(contains(log, "start 2/2: Spel-Win64-Shipping-lang.exe (het spel) -- "));
        CHECK(contains(log, "[amdaemon.exe] AM Daemon klaar") && contains(log, "[Spel-Win64-Shipping-lang.exe] BudgieLoader 1.0 start"));
        CHECK(contains(log, "lader van Spel-Win64-Shipping-lang.exe gestopt (exitcode 3); het spel volgen"));
        CHECK(contains(log, "(gestopt door de launcher: het spel is gestopt)") && contains(log, "klaar (exitcode 3)"));
        CHECK(contains(log, "A11: overgeslagen 0, toegepast 0, koppeling 0, versie 1, fout 0"));   // alleen de uitvoer van het spel telt
        {
            std::vector<fs::path> dirs;
            for (const auto& e : fs::directory_iterator(root / "state2/starts")) dirs.push_back(e.path());
            CHECK(dirs.size() == 1);
            if (dirs.size() == 1) {
                const std::string sj = slurp(dirs[0] / "starts.json");
                CHECK(contains(sj, "\"exitcode_run\":3") && contains(sj, "\"stopped_by_launcher\":\"het spel is gestopt\""));
                CHECK(contains(slurp(dirs[0] / "1-amdaemon.exe.log"), "AM Daemon klaar") && contains(slurp(dirs[0] / "2-Spel-Win64-Shipping-lang.exe.log"), "BudgieLoader"));
            }
        }
        if (g_failures) std::fprintf(stderr, "--- rg-direct log (twee exe's) ---\n%s\n", log.c_str());
        // De halve opstelling.
        fs::remove(out / "order");
        const int f0 = g_failures;
        CHECK(run("\n[Env]\nDAEMON_STOPT = 1\n", log) == 7);
        CHECK(contains(log, "FOUT: start 1/2 (amdaemon.exe) is GESTOPT (exitcode 9) voordat start 2/2 (Spel-Win64-Shipping-lang.exe) begon"));
        CHECK(contains(log, "Halve opstelling: start 1/2 (amdaemon.exe)") && contains(log, "klaar (exitcode 7)"));
        CHECK(!contains(slurp(out / "order"), "spel "));
        if (g_failures != f0) std::fprintf(stderr, "--- rg-direct log (halve opstelling) ---\n%s\n", log.c_str());
        fs::remove(fs::path(p.dirs.user) / "Twee/game.ini");
    }
    // De besturingspipe: een tweede helper ("TeknoParrotPipe 127.0.0.1 <poort>") verbindt over TCP en krijgt elke 15 ms het rapport
    // uit het bestand van de daemon, ongewijzigd; het logboek noemt de knoppen die aan staan (hier START van speler 1) en de munt.
    {
        const fs::path out = root / "out3";
        fs::create_directories(out);
        write(wine,
              "#!/usr/bin/env bash\n"
              "case \"$1\" in\n"
              "  */pipehelper.exe|*/rg-jvsbridge.exe)\n"
              "    shift\n"
              "    if [ \"$1\" = TeknoParrotPipe ]; then\n"
              "      echo \"$@\" > \"$OUT/control-args\"\n"
              "      echo \"pipehelper: pipe TeknoParrotPipe created, waiting for game...\" >&2\n"
              "      exec 3<>/dev/tcp/127.0.0.1/$3\n"
              "      head -c 640 <&3 > \"$OUT/control-reports.tmp\"; mv \"$OUT/control-reports.tmp\" \"$OUT/control-reports\"\n"
              "      exec sleep 30\n"
              "    fi\n"
              "    echo \"$@\" > \"$OUT/helper-args\"\n"
              "    echo \"pipehelper: pipe created\" >&2\n"
              "    exec sleep 30 ;;\n"
              "  *)\n"
              "    for i in $(seq 100); do [ -f \"$OUT/control-reports\" ] && break; sleep 0.05; done\n"
              "    echo \"BudgieLoader 1.0 start\"\n"
              "    exit 3 ;;\n"
              "esac\n",
              true);
        std::string report(64, '\0');
        report[0] = 0x01;              // kanaal 0 (de aim zelf maakt niet uit: rg-direct stuurt het rapport ongewijzigd door)
        report[25] = 3;                // munt 1: teller 3
        report[28] = static_cast<char>(0x80);   // START van speler 1
        write(root / "control", report);
        write(fs::path(p.dirs.user) / "Spel/game.ini", "[Input]\nControlPipe = AllsUsbIo\n");
        o.base_env["OUT"] = out.string();
        o.control_source = (root / "control").string();
        DirectPlan cp;
        CHECK(plan_direct("Spel", p, o, cp, why) == Verdict::Ok && cp.control_pipe == "AllsUsbIo");
        cp.state = (root / "state3").string();
        const fs::path planf = root / "plan3.json", logf = root / "run3.log";
        write(planf, plan_to_json(cp));
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, logf.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        std::string bin = RG_DIRECT_BIN;
        std::vector<char*> av{bin.data(), const_cast<char*>("--run"), const_cast<char*>(planf.c_str()), nullptr};
        extern char** environ;
        pid_t pid = 0;
        CHECK(posix_spawn(&pid, bin.c_str(), &fa, nullptr, av.data(), environ) == 0);
        posix_spawn_file_actions_destroy(&fa);
        int st = 0;
        waitpid(pid, &st, 0);
        const std::string log = slurp(logf);
        const int f0 = g_failures;
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 3);
        CHECK(slurp(out / "control-args").rfind("TeknoParrotPipe 127.0.0.1 ", 0) == 0);   // alleen pipe, host en poort: geen gedeeld geheugen
        CHECK(contains(log, "besturingspipe AllsUsbIo: " + cp.pipehelper + " TeknoParrotPipe 127.0.0.1 "));
        CHECK(contains(log, "helper (TeknoParrotPipe): pipehelper: pipe TeknoParrotPipe created"));
        CHECK(contains(log, "besturingspipe: game verbonden (TeknoParrotPipe via TCP, verbinding 1)"));
        CHECK(contains(log, "besturingspipe: eerste rapport verstuurd (64 bytes)") && contains(log, "besturingspipe: aan: p1.start"));
        CHECK(!contains(log, "besturingspipe: aan: p1.trigger"));
        CHECK(contains(log, "besturingspipe: verbinding 1 dicht (") && contains(log, "klaar (exitcode 3)"));
        std::string want;
        for (int i = 0; i < 10; ++i) want += report;
        CHECK(slurp(out / "control-reports") == want);   // tien rapporten, byte voor byte het bestand van de daemon
        // Het JVS-deel blijft zoals het was.
        CHECK(contains(slurp(out / "helper-args"), "TeknoParrot_JVS 127.0.0.1 ") && contains(log, "helper: pipehelper: pipe created"));
        // Zonder het bestand van de daemon start er niets (zoals bij het gedeelde geheugen).
        fs::remove(root / "control");
        DirectPlan kort = cp;
        write(planf, plan_to_json(kort));
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, logf.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        CHECK(posix_spawn(&pid, bin.c_str(), &fa, nullptr, av.data(), environ) == 0);
        posix_spawn_file_actions_destroy(&fa);
        waitpid(pid, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 1 && contains(slurp(logf), "van de besturingspipe bestaat niet"));
        if (g_failures != f0) std::fprintf(stderr, "--- rg-direct log (besturingspipe) ---\n%s\n", log.c_str());
        fs::remove(fs::path(p.dirs.user) / "Spel/game.ini");
    }
    // [Launch] Drives: de schijfletter staat er precies zolang de run duurt (ervoor gezet, erna weg); een letter die er al goed stond
    // blijft staan (uit tests/test_exe_override.py, toen launch-direct.py de tweeling was).
    {
        const fs::path out = root / "out4";
        fs::create_directories(out);
        write(wine,
              "#!/usr/bin/env bash\n"
              "case \"$1\" in\n"
              "  */pipehelper.exe|*/rg-jvsbridge.exe) echo \"pipehelper: pipe created\" >&2; exec sleep 30 ;;\n"
              "  *) readlink \"$WINEPREFIX/dosdevices/x:\" > \"$OUT/drive-x\"; exit 3 ;;\n"
              "esac\n",
              true);
        write(root / "src", std::string(64, '\0'));
        write(fs::path(p.dirs.user) / "Spel/game.ini", "[Launch]\nDrives = X=.\n");
        o.base_env["OUT"] = out.string();
        o.control_source.clear();
        auto run = [&](std::string& log) {
            DirectPlan dp;
            std::string w;
            if (plan_direct("Spel", p, o, dp, w) != Verdict::Ok || dp.drives.size() != 1) { std::fprintf(stderr, "plan drives: %s\n", w.c_str()); return -1; }
            dp.state = (root / "state4").string();
            const fs::path planf = root / "plan4.json", logf = root / "run4.log";
            write(planf, plan_to_json(dp));
            posix_spawn_file_actions_t fa;
            posix_spawn_file_actions_init(&fa);
            posix_spawn_file_actions_addopen(&fa, 1, logf.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            posix_spawn_file_actions_adddup2(&fa, 1, 2);
            std::string bin = RG_DIRECT_BIN;
            std::vector<char*> av{bin.data(), const_cast<char*>("--run"), const_cast<char*>(planf.c_str()), nullptr};
            extern char** environ;
            pid_t pid = 0;
            if (posix_spawn(&pid, bin.c_str(), &fa, nullptr, av.data(), environ) != 0) return -1;
            posix_spawn_file_actions_destroy(&fa);
            int st = 0;
            waitpid(pid, &st, 0);
            log = slurp(logf);
            return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        };
        DirectPlan bp;
        CHECK(plan_direct("Spel", p, o, bp, why) == Verdict::Ok);
        const fs::path xlink = fs::path(bp.prefix) / "dosdevices/x:";
        const int f0 = g_failures;
        std::string log;
        CHECK(run(log) == 3);
        CHECK(slurp(out / "drive-x") == fs::canonical(view).string() + "\n" || slurp(out / "drive-x") == view.string() + "\n");   // tijdens de run
        CHECK(!fs::exists(fs::symlink_status(xlink)));                                                                        // erna weg
        CHECK(contains(log, "schijfletter X: -> ") && contains(log, "schijfletter X: weggehaald"));
        // Stond hij er al goed, dan blijft hij staan.
        fs::create_directory_symlink(view, xlink);
        CHECK(run(log) == 3 && fs::is_symlink(xlink) && contains(log, "schijfletter X: staat al in de prefix"));
        fs::remove(xlink);
        if (g_failures != f0) std::fprintf(stderr, "--- rg-direct log (schijfletters) ---\n%s\n", log.c_str());
        fs::remove(fs::path(p.dirs.user) / "Spel/game.ini");
    }
#endif

    fs::remove_all(root);
    std::printf("direct-launch: %d controles, %d fout\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
