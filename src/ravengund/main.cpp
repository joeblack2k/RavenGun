// ravengund: RavenGun-daemon. Voorgrondproces; enige eigenaar van de Move-hardware.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "hid_thread.hpp"
#include "ipc_server.hpp"
#include "ravengun/ipc_client.hpp"
#include "shared.hpp"

using namespace ravengund;

namespace {

void usage(FILE* f) {
    std::fputs(
        "gebruik: ravengund [opties]\n"
        "\n"
        "Draait op de voorgrond tot SIGINT (Ctrl-C) of SIGTERM. Eén instantie per gebruiker\n"
        "(lockfile in $XDG_RUNTIME_DIR/ravengun/). Beheer via ravengunctl.\n"
        "\n"
        "opties:\n"
        "  --controller AA:BB:CC:DD:EE:FF   alleen deze Move gebruiken (standaard: eerste via Bluetooth)\n"
        "  --no-filter                      1€-filter uit (ruwe aim)\n"
        "  --min-cutoff HZ                  1€ min_cutoff (standaard 1.0)\n"
        "  --beta B                         1€ beta (standaard 0.5)\n"
        "  --h-edge-deg G                   yaw tot de linker/rechter rand (standaard 20)\n"
        "  --v-edge-deg G                   pitch tot de boven/onderrand (standaard 11.25)\n"
        "  --no-auto-center                 geen automatisch midden bij de eerste oriëntatie\n"
        "  --no-hid                         geen PSMoveAPI openen (alleen IPC; voor tests)\n"
        "  -h, --help                       deze tekst\n"
        "\n"
        "exitcodes: 0 normaal gestopt, 64 verkeerd gebruik, 69 al actief of geen runtime-map,\n"
        "           71 systeemfout (socket, threads)\n",
        f);
}

bool parse_double(const char* s, double lo, double hi, double& out) {
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(s, &end);
    if (errno || !end || *end || !(v >= lo && v <= hi)) return false;
    out = v;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    HidOptions opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](double lo, double hi, double& out) {
            if (i + 1 >= argc || !parse_double(argv[++i], lo, hi, out)) {
                std::fprintf(stderr, "ravengund: ongeldige waarde voor %s\n", a.c_str());
                std::exit(64);
            }
        };
        if (a == "-h" || a == "--help") { usage(stdout); return 0; }
        else if (a == "--no-hid") opt.enabled = false;
        else if (a == "--no-filter") opt.pipeline.filter.enabled = false;
        else if (a == "--no-auto-center") opt.pipeline.auto_center_on_start = false;
        else if (a == "--min-cutoff") need(0.001, 100, opt.pipeline.filter.min_cutoff_hz);
        else if (a == "--beta") need(0, 1000, opt.pipeline.filter.beta);
        else if (a == "--h-edge-deg") need(1, 89, opt.pipeline.aim.h_edge_deg);
        else if (a == "--v-edge-deg") need(1, 89, opt.pipeline.aim.v_edge_deg);
        else if (a == "--controller" && i + 1 < argc) {
            std::string id = argv[++i];
            for (auto& ch : id) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            opt.preferred_controller = id;
        } else {
            std::fprintf(stderr, "ravengund: onbekende optie '%s'\n\n", a.c_str());
            usage(stderr);
            return 64;
        }
    }

    std::string dir = ravengun::ipc::runtime_dir();
    if (dir.empty()) { std::fprintf(stderr, "ravengund: XDG_RUNTIME_DIR is niet gezet\n"); return 69; }
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "ravengund: kan %s niet maken: %s\n", dir.c_str(), std::strerror(errno));
        return 69;
    }
    struct stat st{};
    if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid()) {
        std::fprintf(stderr, "ravengund: %s is geen eigen map\n", dir.c_str());
        return 69;
    }
    ::chmod(dir.c_str(), 0700);

    // Eén instantie: flock op de lockfile; het slot vervalt vanzelf als het proces stopt.
    std::string lock = ravengun::ipc::lock_path();
    int lfd = ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lfd < 0) { std::fprintf(stderr, "ravengund: lockfile: %s\n", std::strerror(errno)); return 69; }
    if (::flock(lfd, LOCK_EX | LOCK_NB) != 0) {
        char buf[32] = {0};
        ssize_t n = ::pread(lfd, buf, sizeof buf - 1, 0);
        (void)n;
        std::fprintf(stderr, "ravengund: er draait al een instantie (pid %s)\n", buf[0] ? buf : "?");
        return 69;
    }
    if (::ftruncate(lfd, 0) == 0) {
        std::string pid = std::to_string(::getpid()) + "\n";
        if (::pwrite(lfd, pid.data(), pid.size(), 0) < 0) { /* alleen informatief */ }
    }

    // Signalen alleen in de hoofdthread afhandelen (sigwait), vóór het starten van threads.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    std::signal(SIGPIPE, SIG_IGN);

    std::string sock = ravengun::ipc::socket_path();
    int sfd = ipc_listen(sock);
    if (sfd < 0) return 71;
    int wake = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wake < 0) { logf("fout", "eventfd: %s", std::strerror(errno)); return 71; }

    logf("info", "ravengund gestart (pid %d), socket %s, HID %s, filter %s", static_cast<int>(::getpid()), sock.c_str(),
         opt.enabled ? "aan" : "uit (--no-hid)", opt.pipeline.filter.enabled ? "1€" : "uit");

    SharedState shared;
    std::thread hid(hid_thread_main, std::ref(shared), opt);
    std::thread ipc(ipc_thread_main, std::ref(shared), sfd, wake);

    int sig = 0;
    sigwait(&set, &sig);
    logf("info", "signaal %s ontvangen, stoppen", sig == SIGINT ? "SIGINT" : "SIGTERM");
    shared.running.store(false);
    uint64_t one = 1;
    if (::write(wake, &one, sizeof one) < 0) { /* poll heeft ook een time-out */ }
    hid.join();
    ipc.join();
    ::close(sfd);
    ::unlink(sock.c_str());
    ::close(wake);
    ::close(lfd);  // lockfile blijft bestaan; alleen het slot vervalt (geen unlink-race)
    logf("info", "ravengund gestopt");
    return 0;
}
