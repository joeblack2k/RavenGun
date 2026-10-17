// Tests van de programma's met rootrechten (src/syshelper) zonder root en zonder iets op het systeem te raken: alles in een
// tijdelijke map, met vervangers voor udevadm, visudo en sudo. Daarnaast de echte programma's, alleen met aanroepen die voor root
// niets doen (verkeerde argumenten, niet als root, --version).
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "syshelper/bt_core.hpp"
#include "syshelper/common.hpp"
#include "syshelper/setup_content.hpp"
#include "syshelper/setup_core.hpp"

using namespace ravengun::syshelper;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool exists(const std::string& p) { struct stat st{}; return ::lstat(p.c_str(), &st) == 0; }
int mode_of(const std::string& p) { struct stat st{}; return ::lstat(p.c_str(), &st) == 0 ? (st.st_mode & 07777) : -1; }
void put(const std::string& path, const std::string& data) { std::ofstream(path, std::ios::binary) << data; }

std::string make_tmp() {
    char t[] = "/tmp/rg-syshelper-XXXXXX";
    const char* d = ::mkdtemp(t);
    return d ? d : "";
}
void rm_rf(const std::string& p) {
    if (p.rfind("/tmp/rg-syshelper-", 0) != 0) return;  // alleen onze eigen tijdelijke mappen
    std::vector<std::string> argv{"/bin/rm", "-rf", p};
    run_program(argv, nullptr, 20000);
}

int run_parse(const std::vector<std::string>& args, BtRequest& req) {
    std::vector<const char*> v;
    for (const auto& a : args) v.push_back(a.c_str());
    std::string err;
    return parse_bt_args(static_cast<int>(v.size()), v.data(), req, err);
}

const char* kAdapter = "00:1A:7D:DA:71:13";
const char* kMove = "00:07:04:8C:70:AD";

void test_validators() {
    CHECK(valid_bt_address("00:07:04:8C:70:AD"));
    for (const char* bad : {"00:07:04:8c:70:ad", "00:07:04:8C:70", "00:07:04:8C:70:AD:11", "00-07-04-8C-70-AD", "00:07:04:8C:70:AD ", " 00:07:04:8C:70:AD",
                            "00:07:04:8C:70:AD;id", "$(id)", "../../etc/passwd", "00:07:04:8C:70:AG", "00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF", "",
                            "00:07:04:8C:70:AD\nrm"})
        CHECK(!valid_bt_address(bad));
    CHECK(valid_user_name("deck") && valid_user_name("_x-1") && valid_user_name("a"));
    for (const char* bad : {"", "root", "Deck", "1deck", "de ck", "deck;id", "de\nck", "-x", "a/b", "deck$", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})
        CHECK(!valid_user_name(bad));
}

void test_bt_args() {
    BtRequest r;
    CHECK(run_parse({"register", kMove, "move"}, r) == kBtOk && r.action == BtRequest::Action::Register && r.type == "move" && r.address == kMove);
    CHECK(run_parse({"register", kMove, "move2"}, r) == kBtOk);
    CHECK(run_parse({"register", "00:06:F7:0E:C1:93", "navigation"}, r) == kBtOk);
    CHECK(run_parse({"unregister", kMove}, r) == kBtOk && r.action == BtRequest::Action::Unregister && r.type.empty());
    CHECK(run_parse({}, r) == kBtUsage);
    CHECK(run_parse({"register"}, r) == kBtUsage);
    CHECK(run_parse({"register", kMove}, r) == kBtUsage);
    CHECK(run_parse({"register", kMove, "move", "extra"}, r) == kBtUsage);
    CHECK(run_parse({"unregister", kMove, "move"}, r) == kBtUsage);
    for (const char* act : {"install", "--help", "REGISTER", "", "register ", "../register"}) CHECK(run_parse({act, kMove, "move"}, r) == kBtUsage);
    for (const char* t : {"", "dualshock", "MOVE", "move;id", "move ", "navigation2", "../x", "move\n"}) CHECK(run_parse({"register", kMove, t}, r) == kBtUsage);
    for (const char* a : {"00:07:04:8c:70:ad", "00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF", "$(id)", "00:07:04:8C:70:AD;id"}) {
        CHECK(run_parse({"register", a, "move"}, r) == kBtUsage);
        CHECK(run_parse({"unregister", a}, r) == kBtUsage);
    }
}

void test_bt_text() {
    const std::string key = "0123456789ABCDEF0123456789ABCDEF";
    const std::string info = bt_info_text("move", key);
    CHECK(info.find("[General]\nName=Motion Controller\nClass=0x002508\n") == 0);
    CHECK(info.find("Trusted=true\n") != std::string::npos && info.find("Vendor=1356\nProduct=981\nVersion=1\n") != std::string::npos);
    CHECK(info.find("[LinkKey]\nKey=" + key + "\nType=0\nPINLength=0\n") != std::string::npos);
    CHECK(bt_info_text("move2", key).find("Product=3166") != std::string::npos);
    const std::string nav = bt_info_text("navigation", key);
    CHECK(nav.find("Name=Navigation Controller") != std::string::npos && nav.find("Class=0x000508") != std::string::npos &&
          nav.find("Source=2") != std::string::npos && nav.find("Product=1071\nVersion=0") != std::string::npos);
    CHECK(bt_info_text("dualshock", key).empty() && bt_info_text("move", "zzz").empty() && bt_info_text("move", "0123456789abcdef0123456789abcdef").empty());
    CHECK(bt_cache_text("move").find("[ServiceRecords]\n0x00010000=3601920900000A") != std::string::npos);
    CHECK(bt_info_is_sony(info) && bt_info_is_sony(nav) && bt_info_is_sony(bt_info_text("move2", key)));
    CHECK(!bt_info_is_sony("[General]\nName=Keyboard\n\n[DeviceID]\nVendor=1234\nProduct=981\n"));
    CHECK(!bt_info_is_sony("[General]\nName=x\n\n[DeviceID]\nVendor=1356\nProduct=1\n"));
    CHECK(!bt_info_is_sony("[DeviceID]\nVendor=1356x\nProduct=981\n"));
    CHECK(!bt_info_is_sony("Vendor=1356\nProduct=981\n"));  // buiten [DeviceID] telt niet
}

struct Calls {
    std::vector<std::string> v;
};

BtEnv make_env(const std::string& btdir, Calls& calls, bool stop_ok = true) {
    BtEnv e;
    e.btdir = btdir;
    e.stop_bluetooth = [&calls, stop_ok] { calls.v.push_back("stop"); return stop_ok; };
    e.start_bluetooth = [&calls] { calls.v.push_back("start"); return true; };
    e.log = [&calls](const std::string& m) { calls.v.push_back("log:" + m); };
    e.random_key = [] { return std::string("AABBCCDDEEFF00112233445566778899"); };
    return e;
}

int bt(const std::string& dir, Calls& calls, BtRequest::Action a, const std::string& addr, const std::string& type, std::string* out = nullptr,
       std::string* err = nullptr) {
    BtRequest r;
    r.action = a;
    r.address = addr;
    r.type = type;
    BtEnv e = make_env(dir, calls);
    std::string o, er;
    const int rc = run_bt_request(r, e, o, er);
    if (out) *out = o;
    if (err) *err = er;
    return rc;
}

void test_bt_run() {
    const std::string root = make_tmp();
    CHECK(!root.empty());
    const std::string btdir = root + "/bluetooth";
    ::mkdir(btdir.c_str(), 0755);
    Calls c;
    // Geen adapter, twee adapters: geweigerd.
    CHECK(bt(btdir, c, BtRequest::Action::Register, kMove, "move") == kBtRefused);
    ::mkdir((btdir + "/" + kAdapter).c_str(), 0755);
    ::mkdir((btdir + "/11:22:33:44:55:66").c_str(), 0755);
    CHECK(bt(btdir, c, BtRequest::Action::Register, kMove, "move") == kBtRefused);
    rm_rf(btdir + "/11:22:33:44:55:66");
    // Een bestand of symlink met een adresnaam telt niet als adapter.
    put(btdir + "/22:33:44:55:66:77", "x");
    CHECK(::symlink("/etc", (btdir + "/33:44:55:66:77:88").c_str()) == 0);
    // Het adres van de adapter zelf: geweigerd.
    CHECK(bt(btdir, c, BtRequest::Action::Register, kAdapter, "move") == kBtRefused);
    CHECK(c.v.empty() || c.v.back().rfind("log:", 0) == 0);

    // Registreren.
    c.v.clear();
    std::string out, err;
    CHECK(bt(btdir, c, BtRequest::Action::Register, kMove, "move", &out, &err) == kBtOk);
    const std::string dev = btdir + "/" + kAdapter + "/" + kMove;
    const std::string info = slurp(dev + "/info");
    CHECK(info == bt_info_text("move", "AABBCCDDEEFF00112233445566778899"));
    CHECK(mode_of(dev) == 0700 && mode_of(dev + "/info") == 0600);
    const std::string cache = btdir + "/" + kAdapter + "/cache/" + kMove;
    CHECK(slurp(cache) == bt_cache_text("move") && mode_of(cache) == 0600);
    CHECK(out.find("geregistreerd") == 0);
    // Bluetooth eerst gestopt en daarna gestart.
    CHECK(c.v.size() >= 3 && c.v[0] == "stop" && c.v[c.v.size() - 2] == "start");
    // Er blijven geen tijdelijke bestanden achter.
    for (const char* sub : {"", "/cache"}) {
        const std::string d = (sub[0] ? btdir + "/" + kAdapter + sub : dev);
        std::vector<std::string> names;
        if (FILE* p = ::popen(("/bin/ls -A '" + d + "'").c_str(), "r")) {
            char b[256];
            while (std::fgets(b, sizeof b, p)) names.push_back(b);
            ::pclose(p);
        }
        for (const auto& n : names) CHECK(n.rfind(".ravengun", 0) != 0);
    }
    // Opnieuw registreren (ander type): de eerste versie gaat naar de backup, die niet opnieuw wordt overschreven.
    CHECK(bt(btdir, c, BtRequest::Action::Register, kMove, "move2") == kBtOk);
    CHECK(slurp(dev + "/info.ravengun-backup") == info);
    CHECK(slurp(dev + "/info").find("Product=3166") != std::string::npos);
    CHECK(bt(btdir, c, BtRequest::Action::Register, kMove, "navigation") == kBtOk);
    CHECK(slurp(dev + "/info.ravengun-backup") == info);
    CHECK(slurp(cache + ".ravengun-backup") == bt_cache_text("move"));
    // Ontkoppelen: map en cache weg.
    c.v.clear();
    CHECK(bt(btdir, c, BtRequest::Action::Unregister, kMove, "", &out) == kBtOk);
    CHECK(!exists(dev) && !exists(cache) && !exists(cache + ".ravengun-backup") && out.find("verwijderd") == 0);
    CHECK(c.v.size() >= 3 && c.v[0] == "stop" && c.v[c.v.size() - 2] == "start");
    // Niet geregistreerd: niets te doen (geen fout).
    c.v.clear();
    CHECK(bt(btdir, c, BtRequest::Action::Unregister, kMove, "", &out) == kBtOk && out.find("niet geregistreerd") == 0);
    CHECK(c.v.size() == 1);  // alleen de logregel, Bluetooth niet aangeraakt

    // Een ander apparaat op dit adres: register en unregister weigeren, niets verandert.
    const std::string other = btdir + "/" + kAdapter + "/00:11:22:33:44:99";
    ::mkdir(other.c_str(), 0700);
    put(other + "/info", "[General]\nName=Headphones\n\n[DeviceID]\nVendor=76\nProduct=1\n");
    const std::string before = slurp(other + "/info");
    CHECK(bt(btdir, c, BtRequest::Action::Register, "00:11:22:33:44:99", "move") == kBtRefused);
    CHECK(bt(btdir, c, BtRequest::Action::Unregister, "00:11:22:33:44:99", "") == kBtRefused);
    CHECK(slurp(other + "/info") == before && exists(other));
    // Een Sony-registratie die niet "Motion Controller" heet mag wel (eigen controller van de gebruiker).
    // Een symlink op de plaats van de map of het info-bestand: geweigerd.
    CHECK(::symlink("/etc", (btdir + "/" + kAdapter + "/00:11:22:33:44:AA").c_str()) == 0);
    CHECK(bt(btdir, c, BtRequest::Action::Register, "00:11:22:33:44:AA", "move") == kBtRefused);
    CHECK(bt(btdir, c, BtRequest::Action::Unregister, "00:11:22:33:44:AA", "") == kBtRefused);
    const std::string d2 = btdir + "/" + kAdapter + "/00:11:22:33:44:BB";
    ::mkdir(d2.c_str(), 0700);
    CHECK(::symlink("/etc/passwd", (d2 + "/info").c_str()) == 0);
    CHECK(bt(btdir, c, BtRequest::Action::Register, "00:11:22:33:44:BB", "move") == kBtRefused);
    CHECK(slurp("/etc/passwd").find("RavenGun") == std::string::npos);

    // Bluetooth start weer, ook als het schrijven mislukt: een bestand in plaats van de map.
    c.v.clear();
    put(btdir + "/" + kAdapter + "/00:11:22:33:44:CC", "geen map");
    CHECK(bt(btdir, c, BtRequest::Action::Register, "00:11:22:33:44:CC", "move") == kBtRefused);
    // Stoppen mislukt: fout, geen bestanden.
    BtRequest r; r.address = "00:11:22:33:44:DD"; r.type = "move";
    Calls c2;
    BtEnv e = make_env(btdir, c2, false);
    CHECK(run_bt_request(r, e, out, err) == kBtFailed && !exists(btdir + "/" + kAdapter + "/00:11:22:33:44:DD"));
    // Een ongeldige sleutel van de bron wordt geweigerd.
    e = make_env(btdir, c2);
    e.random_key = [] { return std::string("zz"); };
    CHECK(run_bt_request(r, e, out, err) == kBtFailed);
    rm_rf(root);
}

SetupContent test_content() {
    SetupContent c;
    c.rules = {{"70-a.rules", "rule A\n"}, {"71-b.rules", "rule B\n"}};
    c.bt_helper = std::string("\x7f" "ELF fake helper", 15);
    c.self_exe = "fake setup helper";
    return c;
}

SetupDirs test_dirs(const std::string& root, Calls& calls) {
    SetupDirs d;
    d.udev_rules_dir = root + "/etc/udev/rules.d";
    d.sudoers_dir = root + "/etc/sudoers.d";
    d.opt_dir = root + "/opt/ravengun";
    d.set_root_owner = false;
    d.reload_udev = [&calls] { calls.v.push_back("udev-reload"); };
    d.visudo_check_file = [&calls](const std::string& p, std::string&) { calls.v.push_back("visudo-file"); return exists(p); };
    d.visudo_check_all = [&calls](std::string&) { calls.v.push_back("visudo-all"); return true; };
    d.sudo_allows = [root](const std::string&, const std::string&) { return exists(root + "/etc/sudoers.d/ravengun-bt-helper"); };
    return d;
}

void test_setup_text() {
    const std::string s = sudoers_text("deck", "/opt/ravengun/bin/ravengun-bt-helper");
    CHECK(s.find("\nDefaults!/opt/ravengun/bin/ravengun-bt-helper env_reset\n") != std::string::npos);
    CHECK(s.find("\ndeck ALL=(root) NOPASSWD: /opt/ravengun/bin/ravengun-bt-helper register *, /opt/ravengun/bin/ravengun-bt-helper unregister *\n") !=
          std::string::npos);
    size_t n = 0, p = 0;
    while ((p = s.find("NOPASSWD", p)) != std::string::npos) { ++n; ++p; }
    CHECK(n == 1);
    CHECK(sudoers_text("root", "/opt/x").empty() && sudoers_text("de ck", "/opt/x").empty() && sudoers_text("deck\nALL", "/opt/x").empty());
    CHECK(sudoers_text("deck", "relative").empty() && sudoers_text("deck", "/opt/x y").empty() && sudoers_text("deck", "/opt/x,/bin/sh").empty() &&
          sudoers_text("deck", "/opt/x*").empty());
    // De regel is geldige sudoers: laat visudo (als die er is) hem controleren, als gewone gebruiker.
    if (::access("/usr/bin/visudo", X_OK) == 0 && ::geteuid() != 0) {
        const std::string root = make_tmp();
        put(root + "/s", s);
        ::chmod((root + "/s").c_str(), 0440);
        std::string o;
        CHECK(run_program({"/usr/bin/visudo", "-cf", root + "/s"}, &o, 20000) == 0);
        put(root + "/bad", s + "deck ALL=(root) NOPASSWD: /opt/x, ,\n");
        CHECK(run_program({"/usr/bin/visudo", "-cf", root + "/bad"}, &o, 20000) != 0);
        rm_rf(root);
    }
    // Wie startte het? PKEXEC_UID gaat voor SUDO_UID; root, rommel en onbekende uid's worden geweigerd.
    auto look = [](unsigned uid, std::string& n) { if (uid == 1000) { n = "deck"; return true; } if (uid == 1001) { n = "Bad Name"; return true; } return false; };
    std::string user, err;
    CHECK(resolve_invoking_user("1000", "", look, user, err) && user == "deck");
    CHECK(resolve_invoking_user("", "1000", look, user, err) && user == "deck");
    CHECK(resolve_invoking_user("1000", "1002", look, user, err) && user == "deck");
    for (const char* bad : {"0", "00", "-1", "1000x", "1e3", " 1000", "4294967295", "99999999999999999999", "1002", "1001"})
        CHECK(!resolve_invoking_user(bad, "", look, user, err));
    CHECK(!resolve_invoking_user(nullptr, nullptr, look, user, err) && !resolve_invoking_user("", "", look, user, err));
    // Argumenten.
    SetupMode m{};
    auto parse = [&m](std::vector<std::string> a) {
        std::vector<const char*> v;
        for (const auto& x : a) v.push_back(x.c_str());
        std::string e;
        return parse_setup_args(static_cast<int>(v.size()), v.data(), m, e);
    };
    CHECK(parse({"--apply"}) == kSetupOk && m == SetupMode::Apply);
    CHECK(parse({"--rollback"}) == kSetupOk && m == SetupMode::Rollback);
    CHECK(parse({"--check"}) == kSetupOk && m == SetupMode::Check);
    CHECK(parse({"--version"}) == kSetupOk && m == SetupMode::Version);
    for (std::vector<std::string> bad : std::vector<std::vector<std::string>>{{}, {"apply"}, {"--APPLY"}, {"--apply", "--rollback"}, {"--apply", "x"},
                                                                          {"-h"}, {"--help"}, {""}, {"--apply "}, {"--dry-run"}})
        CHECK(parse(bad) == kSetupUsage);
}

void test_setup_run() {
    const std::string root = make_tmp();
    Calls calls;
    const SetupDirs d = test_dirs(root, calls);
    const SetupContent c = test_content();
    std::string out, err;
    // Nog niets: de controle zegt wat ontbreekt.
    CHECK(setup_check(d, c, out) == kSetupFailed && out.find("Not installed") != std::string::npos);
    // Verkeerde gebruiker: geweigerd, niets geschreven.
    CHECK(setup_apply(d, c, "root", out, err) == kSetupFailed && !exists(root + "/etc"));
    CHECK(setup_apply(d, c, "de;ck", out, err) == kSetupFailed && !exists(root + "/etc"));
    out.clear(); err.clear();
    CHECK(setup_apply(d, c, "deck", out, err) == kSetupOk);
    if (!err.empty()) std::fprintf(stderr, "apply: %s\n", err.c_str());
    CHECK(slurp(d.udev_rules_dir + "/70-a.rules") == "rule A\n" && mode_of(d.udev_rules_dir + "/70-a.rules") == 0644);
    CHECK(slurp(d.udev_rules_dir + "/71-b.rules") == "rule B\n");
    CHECK(slurp(d.opt_dir + "/bin/ravengun-bt-helper") == c.bt_helper && mode_of(d.opt_dir + "/bin/ravengun-bt-helper") == 0755);
    CHECK(slurp(d.opt_dir + "/bin/ravengun-setup-helper") == "fake setup helper" && mode_of(d.opt_dir + "/bin/ravengun-setup-helper") == 0755);
    CHECK(mode_of(d.opt_dir) == 0755 && mode_of(d.opt_dir + "/bin") == 0755);
    CHECK(slurp(d.sudoers_dir + "/ravengun-bt-helper") == sudoers_text("deck", d.opt_dir + "/bin/ravengun-bt-helper"));
    CHECK(mode_of(d.sudoers_dir + "/ravengun-bt-helper") == 0440);
    CHECK(!exists(d.sudoers_dir + "/.ravengun-bt-helper.check"));
    // Volgorde: regels laden, daarna controleren voor het plaatsen van de sudoers-regel.
    CHECK(calls.v.size() == 3 && calls.v[0] == "udev-reload" && calls.v[1] == "visudo-file" && calls.v[2] == "visudo-all");
    // Alles actueel.
    out.clear();
    CHECK(setup_check(d, c, out) == kSetupOk);
    // Verouderd: een regel en de hulp wijken af.
    SetupContent newer = c;
    newer.rules[0].content = "rule A v2\n";
    newer.bt_helper += "2";
    out.clear();
    CHECK(setup_check(d, newer, out) == kSetupFailed && out.find("Outdated") != std::string::npos && out.find("Helper outdated") != std::string::npos);
    CHECK(content_id(c) != content_id(newer) && content_id(c) == content_id(test_content()) && content_id(c).size() == 16);
    // Bijwerken (zelfde gebruiker) overschrijft.
    CHECK(setup_apply(d, newer, "deck", out, err) == kSetupOk && slurp(d.udev_rules_dir + "/70-a.rules") == "rule A v2\n");
    // Zonder sudoers-regel: de controle meldt het.
    ::unlink((d.sudoers_dir + "/ravengun-bt-helper").c_str());
    out.clear();
    CHECK(setup_check(d, newer, out) == kSetupFailed && out.find("sudo: not (yet) allowed") != std::string::npos);
    // visudo keurt af: geen sudoers-regel en geen tijdelijk bestand.
    SetupDirs strict = d;
    strict.visudo_check_file = [](const std::string&, std::string& why) { why = "syntax error"; return false; };
    err.clear();
    CHECK(setup_apply(strict, newer, "deck", out, err) == kSetupFailed && err.find("visudo rejects") != std::string::npos);
    CHECK(!exists(d.sudoers_dir + "/ravengun-bt-helper") && !exists(d.sudoers_dir + "/.ravengun-bt-helper.check"));
    // De globale controle mislukt: onze regel wordt weer weggehaald.
    SetupDirs strict2 = d;
    strict2.visudo_check_all = [](std::string& why) { why = "broken"; return false; };
    CHECK(setup_apply(strict2, newer, "deck", out, err) == kSetupFailed && !exists(d.sudoers_dir + "/ravengun-bt-helper"));
    // Terugdraaien.
    CHECK(setup_apply(d, newer, "deck", out, err) == kSetupOk);
    out.clear();
    CHECK(setup_rollback(d, newer, out, err) == kSetupOk);
    CHECK(!exists(d.udev_rules_dir + "/70-a.rules") && !exists(d.udev_rules_dir + "/71-b.rules") && !exists(d.sudoers_dir + "/ravengun-bt-helper") &&
          !exists(d.opt_dir));
    // Terugdraaien zonder dat er iets staat is geen fout.
    CHECK(setup_rollback(d, newer, out, err) == kSetupOk);
    // /opt/ravengun als symlink naar een andere map: geweigerd.
    const std::string elsewhere = root + "/elsewhere";
    ::mkdir(elsewhere.c_str(), 0755);
    CHECK(::symlink(elsewhere.c_str(), d.opt_dir.c_str()) == 0);
    err.clear();
    CHECK(setup_apply(d, newer, "deck", out, err) == kSetupFailed && !exists(elsewhere + "/bin"));
    rm_rf(root);
}

// De ingebakken inhoud is gelijk aan de bestanden in de repo en aan het gebouwde programma.
void test_embedded() {
    const SetupContent c = embedded_content();
    CHECK(c.rules.size() == 3);
    for (const Rule& r : c.rules) CHECK(!r.content.empty() && r.content == slurp(std::string(RAVENGUN_SOURCE_DIR) + "/packaging/udev/" + r.name));
    CHECK(c.bt_helper.size() > 1000 && c.bt_helper.compare(0, 4, "\x7f" "ELF") == 0);
    CHECK(c.bt_helper == slurp(RG_BT_HELPER_BIN));
}

int run_exe(const std::vector<std::string>& argv, std::string* out = nullptr) {
    // Zonder PATH of andere omgeving (run_program geeft het kind een lege omgeving).
    return run_program(argv, out, 20000);
}

// De echte programma's: alleen aanroepen die niets op het systeem doen.
void test_programs() {
    const std::string bt = RG_BT_HELPER_BIN, setup = RG_SETUP_HELPER_BIN;
    std::string out;
    // Ongeldige invoer: exit 2, ook als root zou draaien (de invoer wordt eerst gecontroleerd).
    CHECK(run_exe({bt}, &out) == kBtUsage);
    CHECK(run_exe({bt, "register", kMove}, &out) == kBtUsage);
    CHECK(run_exe({bt, "register", "00:07:04:8c:70:ad", "move"}, &out) == kBtUsage);
    CHECK(run_exe({bt, "register", kMove, "move", "extra"}, &out) == kBtUsage);
    CHECK(run_exe({bt, "install", kMove, "move"}, &out) == kBtUsage);
    CHECK(run_exe({bt, "unregister", "00:00:00:00:00:00"}, &out) == kBtUsage);
    CHECK(run_exe({setup}, &out) == kSetupUsage);
    CHECK(run_exe({setup, "--bogus"}, &out) == kSetupUsage);
    CHECK(run_exe({setup, "--apply", "--rollback"}, &out) == kSetupUsage);
    CHECK(run_exe({setup, "apply"}, &out) == kSetupUsage);
    if (::geteuid() != 0) {
        // Geldige invoer maar geen root: exit 3 en niets gedaan.
        CHECK(run_exe({bt, "register", kMove, "move"}, &out) == kBtNotRoot);
        CHECK(run_exe({bt, "unregister", kMove}, &out) == kBtNotRoot);
        CHECK(run_exe({setup, "--apply"}, &out) == kSetupNotRoot);
        CHECK(run_exe({setup, "--rollback"}, &out) == kSetupNotRoot);
    }
    CHECK(run_exe({setup, "--version"}, &out) == 0 && out == "content-id: " + content_id(embedded_content()) + "\n");
    // --check raakt niets aan; de uitkomst hangt van deze machine af (0 of 1), maar de uitvoer heeft de twee delen.
    const int rc = run_exe({setup, "--check"}, &out);
    CHECK((rc == 0 || rc == 1) && out.find("== udev rules") != std::string::npos && out.find("== Bluetooth helper") != std::string::npos);
    // Relatieve paden voert run_program niet uit (geen PATH-zoekopdracht).
    CHECK(run_program({"ls"}, nullptr) == -1);
}

}  // namespace

int main() {
    test_validators();
    test_bt_args();
    test_bt_text();
    test_bt_run();
    test_setup_text();
    test_setup_run();
    test_embedded();
    test_programs();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
