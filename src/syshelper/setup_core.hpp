// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// ravengun-setup-helper: de onderdelen van RavenGun die root nodig hebben, in een keer en met een beheerderswachtwoord
// (de app start het met pkexec; docs/build.md, "Rechten"). Het vervangt scripts/install-permissions.sh, udev-ravengun.sh en
// install-bt-helper.sh. Een programma met vaste argumenten, zonder shell:
//
//   ravengun-setup-helper --apply      installeren (of bijwerken) voor de gebruiker die pkexec startte (PKEXEC_UID)
//   ravengun-setup-helper --rollback   alles weer weghalen
//   ravengun-setup-helper --check      zonder root: is alles aanwezig en actueel? Exitcode 0 = ja
//   ravengun-setup-helper --version    het "content-id" van wat dit programma zou installeren (zonder root)
//
// Wat het installeert, komt UIT DIT PROGRAMMA ZELF (tijdens het bouwen ingebakken): de udev-regels uit packaging/udev, de
// Bluetooth-hulp ravengun-bt-helper en een kopie van zichzelf. Het leest als root dus geen enkel bestand uit een map die de
// gebruiker kan schrijven (behalve zijn eigen programmabestand, dat pkexec al aan de gebruiker toont). De kopie van zichzelf
// komt in /opt/ravengun/bin (root:root 0755): vanaf de volgende keer (controle, terugdraaien, uninstall) draait pkexec die
// kopie, die de gebruiker niet kan wijzigen.
#include <sys/types.h>

#include <functional>
#include <string>
#include <vector>

namespace ravengun::syshelper {

inline constexpr int kSetupOk = 0, kSetupFailed = 1, kSetupUsage = 2, kSetupNotRoot = 3;

enum class SetupMode { Apply, Rollback, Check, Version };
// Precies een argument uit de vaste lijst; alles anders is een gebruiksfout (kSetupUsage).
int parse_setup_args(int argc, const char* const* argv, SetupMode& mode, std::string& err);

struct Rule {
    std::string name;     // 70-ravengun.rules
    std::string content;
};

// Wat er geïnstalleerd wordt.
struct SetupContent {
    std::vector<Rule> rules;
    std::string bt_helper;    // het programma ravengun-bt-helper (bytes)
    std::string self_exe;     // dit programma (bytes), voor de kopie in /opt/ravengun/bin; leeg = geen kopie
};

// Waar het naartoe gaat; in productie de vaste systeempaden, in tests een tijdelijke map.
struct SetupDirs {
    std::string udev_rules_dir = "/etc/udev/rules.d";
    std::string sudoers_dir = "/etc/sudoers.d";
    std::string opt_dir = "/opt/ravengun";  // met bin/ eronder
    bool set_root_owner = true;             // chown root:root (alleen uit in tests zonder root)
    std::string sudo_binary = "/usr/bin/sudo";
    // Vervangbaar voor tests; leeg = de echte programma's (/usr/bin/udevadm, /usr/bin/visudo).
    std::function<void()> reload_udev;
    std::function<bool(const std::string& path, std::string& why)> visudo_check_file;
    std::function<bool(std::string& why)> visudo_check_all;
    std::function<bool(const std::string& sudo_binary, const std::string& helper)> sudo_allows;
};

inline const char* kBtHelperName = "ravengun-bt-helper";
inline const char* kSetupHelperName = "ravengun-setup-helper";
inline const char* kSudoersName = "ravengun-bt-helper";

// De tekst van de sudoers-regel voor deze gebruiker (leeg als de naam niet geldig is): alleen register en unregister van de hulp.
std::string sudoers_text(const std::string& user, const std::string& helper_path);
// Een kort id van alles wat geïnstalleerd wordt (voor "is de kopie in /opt gelijk aan deze?").
std::string content_id(const SetupContent& c);

// Wie startte pkexec/sudo? Uit PKEXEC_UID (anders SUDO_UID); `lookup` geeft de naam bij een uid (leeg = getpwuid_r).
bool resolve_invoking_user(const char* pkexec_uid, const char* sudo_uid, const std::function<bool(unsigned, std::string&)>& lookup,
                           std::string& user, std::string& err);

// De drie handelingen. out krijgt de voortgang voor de gebruiker, err de reden bij een fout. De rootcontrole doet main.
int setup_apply(const SetupDirs& d, const SetupContent& c, const std::string& user, std::string& out, std::string& err);
int setup_rollback(const SetupDirs& d, const SetupContent& c, std::string& out, std::string& err);
int setup_check(const SetupDirs& d, const SetupContent& c, std::string& out);

}  // namespace ravengun::syshelper
