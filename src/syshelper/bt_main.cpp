// SPDX-License-Identifier: GPL-3.0-or-later
// ravengun-bt-helper: zie bt_core.hpp. Dit bestand is alleen de rootcontrole, de echte omgeving (/var/lib/bluetooth,
// /usr/bin/systemctl, syslog) en de uitvoer.
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "syshelper/bt_core.hpp"
#include "syshelper/common.hpp"

using namespace ravengun::syshelper;

namespace {
constexpr const char* kLogFile = "/var/log/ravengun-bt-helper.log";

void log_line(const std::string& msg) {
    ::syslog(LOG_INFO, "%s", msg.c_str());
    if (::geteuid() != 0) return;
    const int fd = ::open(kLogFile, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return;
    char ts[40];
    const time_t now = ::time(nullptr);
    struct tm tm{};
    ::localtime_r(&now, &tm);
    ::strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S%z", &tm);
    const std::string line = std::string(ts) + " " + msg + "\n";
    if (::write(fd, line.data(), line.size()) < 0) { /* loggen is nooit een reden om te falen */ }
    ::close(fd);
}
}  // namespace

int main(int argc, char** argv) {
    sanitize_process();  // geen geerfde omgeving of bestandsbeschrijvingen; umask 077
    BtRequest req;
    std::string err, out;
    // Eerst de invoer, ook zonder root (dan is het te testen en lekt niets over het systeem).
    if (parse_bt_args(argc - 1, argv + 1, req, err) != kBtOk) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return kBtUsage;
    }
    if (::geteuid() != 0) {
        std::fprintf(stderr, "ravengun-bt-helper must run as root (through sudo)\n");
        return kBtNotRoot;
    }
    ::openlog("ravengun-bt-helper", LOG_PID, LOG_AUTH);
    BtEnv env;
    env.log = log_line;
    env.stop_bluetooth = [] { return run_program({"/usr/bin/systemctl", "stop", "bluetooth.service"}, nullptr, 30000) == 0; };
    env.start_bluetooth = [] { return run_program({"/usr/bin/systemctl", "start", "bluetooth.service"}, nullptr, 30000) == 0; };
    const int rc = run_bt_request(req, env, out, err);
    if (!out.empty()) std::printf("%s\n", out.c_str());
    if (!err.empty()) std::fprintf(stderr, "%s\n", err.c_str());
    return rc;
}
