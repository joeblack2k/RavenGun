// SPDX-License-Identifier: GPL-3.0-or-later
// ravengun-setup-helper: zie setup_core.hpp. Alleen vaste argumenten (--apply, --rollback, --check, --version); geen invoer van
// de gebruiker behalve wie hem startte (PKEXEC_UID, door pkexec zelf gezet). Dit bestand: de rootcontrole, de omgeving leegmaken
// en de koppeling met de vaste systeempaden.
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "syshelper/common.hpp"
#include "syshelper/setup_content.hpp"
#include "syshelper/setup_core.hpp"

using namespace ravengun::syshelper;

int main(int argc, char** argv) {
    // De waarden die we nodig hebben, VOORDAT de omgeving leeg gaat (pkexec zet een schone omgeving en PKEXEC_UID zelf).
    const char* pk = std::getenv("PKEXEC_UID");
    const char* su = std::getenv("SUDO_UID");
    const std::string pkexecUid = pk ? pk : "", sudoUid = su ? su : "";
    sanitize_process();

    SetupMode mode{};
    std::string err, out;
    if (parse_setup_args(argc - 1, argv + 1, mode, err) != kSetupOk) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return kSetupUsage;
    }
    ::openlog("ravengun-setup-helper", LOG_PID, LOG_AUTH);
    SetupContent content = embedded_content();
    const SetupDirs dirs;  // de vaste systeempaden

    int rc = kSetupOk;
    if (mode == SetupMode::Version) {
        std::printf("content-id: %s\n", content_id(content).c_str());
        return kSetupOk;
    }
    if (mode == SetupMode::Check) {
        rc = setup_check(dirs, content, out);
        std::fputs(out.c_str(), stdout);
        return rc;
    }
    if (::geteuid() != 0) {
        std::fprintf(stderr, "Administrator rights are needed: RavenGun starts this program through pkexec.\n");
        return kSetupNotRoot;
    }
    if (mode == SetupMode::Apply) {
        std::string user;
        if (!resolve_invoking_user(pkexecUid.c_str(), sudoUid.c_str(), nullptr, user, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return kSetupUsage;
        }
        // Een kopie van dit programma voor de volgende keren: de bytes van het draaiende bestand (geen padzoekactie).
        const int fd = ::open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            read_fd_all(fd, content.self_exe);
            ::close(fd);
        }
        rc = setup_apply(dirs, content, user, out, err);
    } else {
        rc = setup_rollback(dirs, content, out, err);
    }
    std::fputs(out.c_str(), stdout);
    if (!err.empty()) std::fprintf(stderr, "%s\n", err.c_str());
    return rc;
}
