// SPDX-License-Identifier: GPL-3.0-or-later
#include "syshelper/setup_core.hpp"

#include <pwd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "syshelper/common.hpp"

namespace ravengun::syshelper {

namespace {

constexpr const char* kSudoersTemplateUser = "USER";

std::string btHelperPath(const SetupDirs& d) { return d.opt_dir + "/bin/" + kBtHelperName; }
std::string setupHelperPath(const SetupDirs& d) { return d.opt_dir + "/bin/" + kSetupHelperName; }
std::string parentOf(const std::string& p) {
    const size_t s = p.rfind('/');
    return s == std::string::npos || s == 0 ? std::string("/") : p.substr(0, s);
}

uid_t rootUid(const SetupDirs& d) { return d.set_root_owner ? 0 : static_cast<uid_t>(-1); }
gid_t rootGid(const SetupDirs& d) { return d.set_root_owner ? 0 : static_cast<gid_t>(-1); }

void reloadUdevReal() {
    const char* const triggers[][3] = {{"--subsystem-match=input", nullptr, nullptr},
                                       {"--subsystem-match=misc", "--sysname-match=uhid", nullptr},
                                       {"--subsystem-match=usb", "--attr-match=idVendor=0529", nullptr},
                                       {"--subsystem-match=usb", "--attr-match=idVendor=04b9", nullptr},
                                       {"--subsystem-match=usb", "--attr-match=idVendor=064f", nullptr}};
    run_program({"/usr/bin/udevadm", "control", "--reload"}, nullptr, 30000);
    for (const auto& t : triggers) {
        std::vector<std::string> a{"/usr/bin/udevadm", "trigger"};
        for (const char* x : t)
            if (x) a.push_back(x);
        a.push_back("--action=change");
        run_program(a, nullptr, 30000);
    }
}

const char* visudoPath() {
    static const char* const candidates[] = {"/usr/bin/visudo", "/usr/sbin/visudo"};
    for (const char* c : candidates)
        if (::access(c, X_OK) == 0) return c;
    return nullptr;
}

bool visudoCheckFile(const SetupDirs& d, const std::string& path, std::string& why) {
    if (d.visudo_check_file) return d.visudo_check_file(path, why);
    const char* v = visudoPath();
    if (!v) { why = "visudo not found"; return false; }
    std::string o;
    const bool ok = run_program({v, "-cf", path}, &o, 30000) == 0;
    if (!ok) why = o;
    return ok;
}

bool visudoCheckAll(const SetupDirs& d, std::string& why) {
    if (d.visudo_check_all) return d.visudo_check_all(why);
    const char* v = visudoPath();
    if (!v) { why = "visudo not found"; return false; }
    std::string o;
    const bool ok = run_program({v, "-c"}, &o, 30000) == 0;
    if (!ok) why = o;
    return ok;
}

bool sudoAllows(const SetupDirs& d, const std::string& helper) {
    if (d.sudo_allows) return d.sudo_allows(d.sudo_binary, helper);
    return run_program({d.sudo_binary, "-n", "-l", helper, "register", "00:00:00:00:00:01", "move"}, nullptr, 10000) == 0;
}

// Maakt /opt/ravengun en /opt/ravengun/bin, root:root 0755 (en niet via een symlink).
bool ensureOptDirs(const SetupDirs& d, std::string& err) {
    struct stat st{};
    if (::lstat(d.opt_dir.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) { err = d.opt_dir + " is not a folder"; return false; }
    if (!make_dirs(d.opt_dir + "/bin", 0755, err)) return false;
    for (const std::string& p : {d.opt_dir, d.opt_dir + "/bin"}) {
        if (::lstat(p.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) { err = p + " is not a folder"; return false; }
        if (d.set_root_owner && ::chown(p.c_str(), 0, 0) != 0) { err = p + ": " + std::strerror(errno); return false; }
        if (::chmod(p.c_str(), 0755) != 0) { err = p + ": " + std::strerror(errno); return false; }
    }
    return true;
}

void logLine(const std::string& m) { ::syslog(LOG_INFO, "%s", m.c_str()); }

void unlinkMsg(const std::string& path, std::string& out) {
    if (::unlink(path.c_str()) == 0) out += "Removed: " + path + "\n";
}

}  // namespace

int parse_setup_args(int argc, const char* const* argv, SetupMode& mode, std::string& err) {
    static const char* usage = "usage: ravengun-setup-helper --apply | --rollback | --check | --version";
    if (argc != 1) { err = usage; return kSetupUsage; }
    const std::string a = argv[0];
    if (a == "--apply") mode = SetupMode::Apply;
    else if (a == "--rollback") mode = SetupMode::Rollback;
    else if (a == "--check") mode = SetupMode::Check;
    else if (a == "--version") mode = SetupMode::Version;
    else { err = usage; return kSetupUsage; }
    return kSetupOk;
}

std::string sudoers_text(const std::string& user, const std::string& helper) {
    if (!valid_user_name(user) || helper.empty() || helper[0] != '/') return {};
    for (char c : helper)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.')) return {};
    return "# RavenGun: the user " + user + " may start ONLY the Bluetooth helper as root without a password (pairing or\n"
           "# unpairing a PS Move or Navigation Controller). Installed by ravengun-setup-helper; removed again with --rollback.\n"
           "Defaults!" + helper + " env_reset\n" +
           user + " ALL=(root) NOPASSWD: " + helper + " register *, " + helper + " unregister *\n";
}

std::string content_id(const SetupContent& c) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const std::string& s) {
        for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ULL; }
        h ^= 0xFF; h *= 1099511628211ULL;
    };
    for (const Rule& r : c.rules) { mix(r.name); mix(r.content); }
    mix(c.bt_helper);
    mix(sudoers_text(kSudoersTemplateUser, std::string("/opt/ravengun/bin/") + kBtHelperName));
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool resolve_invoking_user(const char* pkexec_uid, const char* sudo_uid, const std::function<bool(unsigned, std::string&)>& lookup,
                           std::string& user, std::string& err) {
    const char* raw = (pkexec_uid && *pkexec_uid) ? pkexec_uid : sudo_uid;
    if (!raw || !*raw) { err = "run this through the RavenGun app (pkexec); no invoking user was given"; return false; }
    unsigned long uid = 0;
    for (const char* p = raw; *p; ++p) {
        if (*p < '0' || *p > '9' || uid > 4000000000UL / 10) { err = "invalid user id"; return false; }
        uid = uid * 10 + static_cast<unsigned>(*p - '0');
    }
    if (uid == 0 || uid > 4294967294UL) { err = "refused: the invoking user is root"; return false; }
    std::string name;
    bool ok = false;
    if (lookup) {
        ok = lookup(static_cast<unsigned>(uid), name);
    } else {
        char buf[4096];
        passwd pw{};
        passwd* res = nullptr;
        if (::getpwuid_r(static_cast<uid_t>(uid), &pw, buf, sizeof buf, &res) == 0 && res && res->pw_name) { name = res->pw_name; ok = true; }
    }
    if (!ok || !valid_user_name(name)) { err = "no valid user account for uid " + std::string(raw); return false; }
    user = name;
    return true;
}

int setup_apply(const SetupDirs& d, const SetupContent& c, const std::string& user, std::string& out, std::string& err) {
    if (!valid_user_name(user)) { err = "invalid user name"; return kSetupFailed; }
    if (c.rules.empty() || c.bt_helper.empty()) { err = "this build has no embedded content"; return kSetupFailed; }
    const std::string sudoers = sudoers_text(user, btHelperPath(d));
    if (sudoers.empty()) { err = "invalid path for the Bluetooth helper"; return kSetupFailed; }

    // 1. De udev-regels.
    out += "== Installing the udev rules\n";
    if (!make_dirs(d.udev_rules_dir, 0755, err)) return kSetupFailed;
    for (const Rule& r : c.rules) {
        if (!write_file_atomic(d.udev_rules_dir, r.name, r.content, 0644, rootUid(d), rootGid(d), err)) return kSetupFailed;
        out += "Installed: " + d.udev_rules_dir + "/" + r.name + "\n";
    }
    if (d.reload_udev) d.reload_udev(); else reloadUdevReal();

    // 2. De Bluetooth-hulp en (voor volgende keren) een kopie van dit programma, in een map die alleen root kan wijzigen.
    out += "== Installing the Bluetooth helper for " + user + "\n";
    if (!ensureOptDirs(d, err)) return kSetupFailed;
    const std::string bin = d.opt_dir + "/bin";
    if (!write_file_atomic(bin, kBtHelperName, c.bt_helper, 0755, rootUid(d), rootGid(d), err)) return kSetupFailed;
    out += "Installed: " + btHelperPath(d) + "\n";
    if (!c.self_exe.empty()) {
        if (!write_file_atomic(bin, kSetupHelperName, c.self_exe, 0755, rootUid(d), rootGid(d), err)) return kSetupFailed;
        out += "Installed: " + setupHelperPath(d) + "\n";
    }

    // 3. De sudoers-regel: eerst als tijdelijk bestand (met een punt in de naam, dat sudo negeert) laten controleren.
    if (!make_dirs(d.sudoers_dir, 0750, err)) return kSetupFailed;  // bestaat op elk systeem met sudo al
    const std::string tmpName = ".ravengun-bt-helper.check";
    if (!write_file_atomic(d.sudoers_dir, tmpName, sudoers, 0440, rootUid(d), rootGid(d), err)) return kSetupFailed;
    const std::string tmpPath = d.sudoers_dir + "/" + tmpName;
    std::string why;
    if (!visudoCheckFile(d, tmpPath, why)) {
        ::unlink(tmpPath.c_str());
        err = "visudo rejects the sudoers rule; nothing changed in " + d.sudoers_dir + (why.empty() ? "" : ": " + why);
        return kSetupFailed;
    }
    const std::string finalPath = d.sudoers_dir + "/" + kSudoersName;
    if (::rename(tmpPath.c_str(), finalPath.c_str()) != 0) {
        err = finalPath + ": " + std::strerror(errno);
        ::unlink(tmpPath.c_str());
        return kSetupFailed;
    }
    if (!visudoCheckAll(d, why)) {
        ::unlink(finalPath.c_str());
        err = "visudo reports an error in the sudoers configuration; the RavenGun rule was removed again" + (why.empty() ? "" : ": " + why);
        return kSetupFailed;
    }
    out += "Installed: " + finalPath + "\n";
    logLine("installed for user " + user);
    out += "Done.\n";
    return kSetupOk;
}

int setup_rollback(const SetupDirs& d, const SetupContent& c, std::string& out, std::string& err) {
    (void)err;
    out += "== Removing the Bluetooth helper and the sudoers rule\n";
    unlinkMsg(d.sudoers_dir + "/" + kSudoersName, out);
    unlinkMsg(btHelperPath(d), out);
    unlinkMsg(setupHelperPath(d), out);  // dit programma zelf, als het daar draait: op Linux kan dat
    ::rmdir((d.opt_dir + "/bin").c_str());
    ::rmdir(d.opt_dir.c_str());
    out += "== Removing the udev rules\n";
    for (const Rule& r : c.rules) unlinkMsg(d.udev_rules_dir + "/" + r.name, out);
    if (d.reload_udev) d.reload_udev(); else reloadUdevReal();
    logLine("removed");
    out += "Done.\n";
    return kSetupOk;
}

int setup_check(const SetupDirs& d, const SetupContent& c, std::string& out) {
    int rc = kSetupOk;
    out += "== udev rules\n";
    for (const Rule& r : c.rules) {
        const std::string p = d.udev_rules_dir + "/" + r.name;
        std::string have;
        struct stat st{};
        if (::lstat(p.c_str(), &st) != 0) { out += "Not installed: " + p + "\n"; rc = kSetupFailed; continue; }
        if (read_file_nofollow(p, have) && have == r.content) out += "Up to date: " + p + "\n";
        else { out += "Outdated: " + p + " differs from this version of RavenGun\n"; rc = kSetupFailed; }
    }
    out += "== Bluetooth helper\n";
    const std::string helper = btHelperPath(d);
    struct stat st{};
    if (::lstat(helper.c_str(), &st) != 0) {
        out += "Not installed: " + helper + "\n";
        rc = kSetupFailed;
    } else {
        if (d.set_root_owner && (st.st_uid != 0 || st.st_gid != 0 || (st.st_mode & 07777) != 0755)) {
            out += "Wrong owner or permissions: " + helper + " (expected root:root 755)\n";
            rc = kSetupFailed;
        }
        std::string have;
        if (read_file_nofollow(helper, have) && have == c.bt_helper) out += "Helper up to date: " + helper + "\n";
        else { out += "Helper outdated: " + helper + " differs from this version of RavenGun\n"; rc = kSetupFailed; }
        if (d.set_root_owner) {
            for (const std::string& p : {parentOf(d.opt_dir), d.opt_dir, d.opt_dir + "/bin"}) {
                struct stat ds{};
                if (::lstat(p.c_str(), &ds) != 0 || !S_ISDIR(ds.st_mode) || ds.st_uid != 0 || (ds.st_mode & 022)) {
                    out += "Folder not safe: " + p + "\n";
                    rc = kSetupFailed;
                }
            }
        }
    }
    if (sudoAllows(d, helper)) out += "sudo: allowed without a password\n";
    else { out += "sudo: not (yet) allowed (" + d.sudoers_dir + "/" + kSudoersName + " is missing?)\n"; rc = kSetupFailed; }
    return rc;
}

}  // namespace ravengun::syshelper
