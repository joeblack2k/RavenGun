// ravengund: RavenGun-daemon. Voorgrondproces; enige eigenaar van de Move-hardware.
#include <cerrno>
#include <cmath>
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

#include "camera.hpp"
#include "ravengun/config/led_color.hpp"
#include "ravengun/config/settings.hpp"
#include "hid_thread.hpp"
#include "ravengun/core/bias_store.hpp"
#include "ipc_server.hpp"
#include "output_service.hpp"
#include "recorder.hpp"
#include "ravengun/rec/recording.hpp"
#include "ravengun/ipc_client.hpp"
#include "shared.hpp"
#include "web_server.hpp"

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
        "  --dt-source move|rx              integratie-dt uit de interne Move-stempel (standaard) of de\n"
        "                                   ontvangsttijd\n"
        "  --bias-dir MAP                   bewaarde gyro-bias per controller (standaard\n"
        "                                   ~/.local/state/ravengun/gyro-bias)\n"
        "  --no-bias-store                  gyro-bias niet laden en niet bewaren\n"
        "  --bias-mode new|legacy           biasschatter: stilstand + bewaarde waarde + quasi-stil (standaard) of\n"
        "                                   alleen stilstand (oud gedrag)\n"
        "  --fusion mahony|mahony_mag       oriëntatieschatter (standaard uit config.json, anders mahony;\n"
        "                                   mahony_mag = Mahony plus yaw-correctie met de magnetometer)\n"
        "\n"
        "uitvoer (virtueel lightgun-apparaat 1209:7202; standaard DISARMED, geen events; 'ravengunctl arm'):\n"
        "  --out-invert-x, --out-invert-y   spiegel de as vóór het apparaat\n"
        "  --out-scale-x S, --out-scale-y S schaal rond het midden (standaard 1)\n"
        "  --out-offset-x D, --out-offset-y D  verschuiving in viewport-eenheden (standaard 0)\n"
        "  --out-content-aspect B/H         aspect van het gamebeeld; samen met --out-screen-aspect (standaard\n"
        "                                   16/9) wordt een letterbox/pillarbox-viewport berekend\n"
        "  --out-screen-aspect B/H          aspect van het scherm (standaard 16/9)\n"
        "\n"
        "camera en boltracking (beelden blijven in RAM, nooit op schijf):\n"
        "  --camera PAD                     camera-apparaat (bv. /dev/v4l/by-path/...); standaard: de enige\n"
        "                                   PS3 Eye (driver ov534) onder /dev/v4l/by-id\n"
        "  --no-camera                      geen camera openen\n"
        "  --camera-mode BxH@FPS            standaard 640x480@60 (YUYV)\n"
        "  --exposure N                     vaste belichting 0..255 (standaard 50)\n"
        "  --gain N                         vaste gain 0..63 (standaard 0)\n"
        "  --camera-auto                    automatische belichting/gain/witbalans laten staan\n"
        "  --led KLEUR|R,G,B                LED-kleur: cyaan (standaard met camera), magenta, rood, groen,\n"
        "                                   blauw, geel, uit, of drie getallen 0..255\n"
        "  --hue-deg G, --hue-tol-deg G     tint-sector van de tracker (standaard: gemeten tint van de\n"
        "                                   LED-kleur, anders nominaal; tolerantie 30)\n"
        "  --min-chroma N                   minimale chroma van een kleurpixel (standaard 28)\n"
        "\n"
        "webinterface (in de daemon; standaard aan, alleen op 127.0.0.1):\n"
        "  --web / --no-web                 webinterface aan (standaard) of uit\n"
        "  --web-port N                     poort (standaard 8765)\n"
        "  --web-lan                        ook op het LAN (0.0.0.0); vereist het token uit\n"
        "                                   ~/.config/ravengun/web-token (0600, wordt gemaakt)\n"
        "\n"
        "instellingen (bewaard in ~/.config/ravengun/config.json; wijzigingen in de webinterface worden daar\n"
        "opgeslagen en bij de volgende start toegepast; opties op de opdrachtregel gaan altijd voor):\n"
        "  --config PAD                     ander instellingenbestand\n"
        "  --no-config                      geen instellingenbestand lezen of schrijven\n"
        "\n"
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
    // Instellingenbestand: eerst lezen, dan de standaardwaarden ervan afleiden; de opdrachtregel
    // hieronder overschrijft daarna elke waarde (opdrachtregel gaat voor).
    std::string config_path = ravengun::config::SettingsStore::default_path();
    bool use_config = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-config") use_config = false;
        else if (a == "--config" && i + 1 < argc) config_path = argv[++i];
    }
    ravengun::config::SettingsStore settings_store(use_config ? config_path : std::string());
    ravengun::config::LoadInfo settings_load;
    if (use_config) settings_load = settings_store.load();
    const ravengun::config::Settings cfg = settings_store.get();

    HidOptions opt;
    CameraOptions cam;
    opt.pipeline.filter.enabled = cfg.filter.enabled;
    opt.pipeline.filter.min_cutoff_hz = cfg.filter.min_cutoff_hz;
    opt.pipeline.filter.beta = cfg.filter.beta;
    opt.pipeline.filter.d_cutoff_hz = cfg.filter.d_cutoff_hz;
    opt.pipeline.aim.h_edge_deg = cfg.aim.h_edge_deg;
    opt.pipeline.aim.v_edge_deg = cfg.aim.v_edge_deg;
    ravengun::core::parse_fusion_mode(cfg.fusion.mode.c_str(), opt.pipeline.fusion_mode);
    opt.bias_dir = ravengun::core::default_bias_dir();
    ravengun::core::parse_ext_mode(cfg.sharp_shooter.extension.c_str(), opt.pipeline.ext_mode);
    opt.pipeline.pump.on = cfg.gun_buttons.trigger_on;
    opt.pipeline.pump.off = cfg.gun_buttons.trigger_off;
    opt.gun_buttons = ravengun::output::build_live_map(cfg.gun_buttons);
    cam.width = cfg.camera.width; cam.height = cfg.camera.height; cam.fps = cfg.camera.fps;
    cam.auto_exposure = cfg.camera.auto_exposure; cam.auto_gain = cfg.camera.auto_gain; cam.auto_awb = cfg.camera.auto_awb;
    cam.exposure = cfg.camera.exposure; cam.gain = cfg.camera.gain;
    cam.tracker.hue_deg = cfg.tracker.hue_deg; cam.tracker.hue_tol_deg = cfg.tracker.hue_tol_deg;
    cam.tracker.min_chroma = cfg.tracker.min_chroma;
    ravengun::output::GunTransform out_transform;
    double content_aspect = NAN, screen_aspect = 16.0 / 9.0;
    std::string led_arg;
    bool web_on = true, web_lan = false;
    double web_port = cfg.web.port;
    double hue = NAN, hue_tol = NAN, min_chroma = NAN;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](double lo, double hi, double& out) {
            if (i + 1 >= argc || !parse_double(argv[++i], lo, hi, out)) {
                std::fprintf(stderr, "ravengund: ongeldige waarde voor %s\n", a.c_str());
                std::exit(64);
            }
        };
        if (a == "-h" || a == "--help") { usage(stdout); return 0; }
        else if (a == "--no-config") { /* al verwerkt */ }
        else if (a == "--config" && i + 1 < argc) ++i;  // al verwerkt
        else if (a == "--no-hid") opt.enabled = false;
        else if (a == "--bias-dir" && i + 1 < argc) opt.bias_dir = argv[++i];
        else if (a == "--no-bias-store") opt.bias_dir.clear();
        else if (a == "--bias-mode" && i + 1 < argc) {
            if (!ravengun::core::parse_bias_mode(argv[++i], opt.pipeline.bias.mode)) {
                std::fprintf(stderr, "ravengund: --bias-mode verwacht new of legacy\n");
                return 64;
            }
        }
        else if (a == "--no-filter") opt.pipeline.filter.enabled = false;
        else if (a == "--no-auto-center") opt.pipeline.auto_center_on_start = false;
        else if (a == "--dt-source" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v != "move" && v != "rx") { std::fprintf(stderr, "ravengund: --dt-source verwacht move of rx\n"); return 64; }
            opt.pipeline.use_move_clock = v == "move";
        }
        else if (a == "--fusion" && i + 1 < argc) {
            if (!ravengun::core::parse_fusion_mode(argv[++i], opt.pipeline.fusion_mode)) {
                std::fprintf(stderr, "ravengund: --fusion verwacht mahony of mahony_mag\n");
                return 64;
            }
        }
        else if (a == "--min-cutoff") need(0.001, 100, opt.pipeline.filter.min_cutoff_hz);
        else if (a == "--beta") need(0, 1000, opt.pipeline.filter.beta);
        else if (a == "--h-edge-deg") need(1, 89, opt.pipeline.aim.h_edge_deg);
        else if (a == "--v-edge-deg") need(1, 89, opt.pipeline.aim.v_edge_deg);
        else if (a == "--web") web_on = true;
        else if (a == "--no-web") web_on = false;
        else if (a == "--web-lan") web_lan = true;
        else if (a == "--web-port") need(1, 65535, web_port);
        else if (a == "--out-invert-x") out_transform.invert_x = true;
        else if (a == "--out-invert-y") out_transform.invert_y = true;
        else if (a == "--out-scale-x") need(0.05, 20, out_transform.scale_x);
        else if (a == "--out-scale-y") need(0.05, 20, out_transform.scale_y);
        else if (a == "--out-offset-x") need(-1, 1, out_transform.offset_x);
        else if (a == "--out-offset-y") need(-1, 1, out_transform.offset_y);
        else if ((a == "--out-content-aspect" || a == "--out-screen-aspect") && i + 1 < argc) {
            int bw = 0, bh = 0;
            char tail = 0;
            if (std::sscanf(argv[++i], "%d/%d%c", &bw, &bh, &tail) != 2 || bw < 1 || bh < 1) {
                std::fprintf(stderr, "ravengund: ongeldige %s (verwacht B/H, bv. 4/3)\n", a.c_str());
                return 64;
            }
            (a == "--out-content-aspect" ? content_aspect : screen_aspect) = static_cast<double>(bw) / bh;
        }
        else if (a == "--no-camera") cam.enabled = false;
        else if (a == "--camera-auto") cam.auto_exposure = cam.auto_gain = cam.auto_awb = true;
        else if (a == "--camera" && i + 1 < argc) cam.device = argv[++i];
        else if (a == "--camera-mode" && i + 1 < argc) {
            int w = 0, h = 0, f = 0;
            char tail = 0;
            if (std::sscanf(argv[++i], "%dx%d@%d%c", &w, &h, &f, &tail) != 3 || w < 16 || h < 16 || f < 1 || f > 300) {
                std::fprintf(stderr, "ravengund: ongeldige --camera-mode (verwacht bv. 640x480@60)\n");
                return 64;
            }
            cam.width = w; cam.height = h; cam.fps = f;
        }
        else if (a == "--exposure") { double v; need(0, 255, v); cam.exposure = static_cast<int>(v); }
        else if (a == "--gain") { double v; need(0, 63, v); cam.gain = static_cast<int>(v); }
        else if (a == "--led" && i + 1 < argc) led_arg = argv[++i];
        else if (a == "--hue-deg") need(-180, 180, hue);
        else if (a == "--hue-tol-deg") need(1, 90, hue_tol);
        else if (a == "--min-chroma") need(1, 180, min_chroma);
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

    // LED-kleur en tint-sector van de tracker. Voorrang: --led, dan het instellingenbestand, dan de
    // standaard (cyaan, alleen met camera). De tint volgt de LED-kleur (de tracker zoekt op tint).
    ravengun::vision::Rgb led{};
    bool led_on = false;
    if (!led_arg.empty()) {
        int r = 0, g = 0, b = 0;
        char tail = 0;
        if (std::sscanf(led_arg.c_str(), "%d,%d,%d%c", &r, &g, &b, &tail) == 3 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
            led = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        } else if (!ravengun::vision::led_color_by_name(led_arg, led)) {
            std::fprintf(stderr, "ravengund: onbekende LED-kleur '%s'\n", led_arg.c_str());
            return 64;
        }
        led_on = true;  // ook "uit" (0,0,0) wordt dan actief gezet
        if (led.r || led.g || led.b) {
            double mh = ravengun::vision::measured_hue_deg(led_arg);
            cam.tracker.hue_deg = std::isnan(mh) ? ravengun::vision::nominal_hue_deg(led) : mh;
            cam.tracker.hue_tol_deg = ravengun::config::kPresetHueTolDeg;
        }
    } else if (cfg.led.enabled) {
        led = {static_cast<uint8_t>(cfg.led.r), static_cast<uint8_t>(cfg.led.g), static_cast<uint8_t>(cfg.led.b)};
        led_on = true;  // tint en tolerantie komen al uit cfg.tracker
        led_arg = cfg.led.preset;
    } else if (cam.enabled) {
        led_arg = "cyaan";  // zie docs/decisions.md (kleurkeuze)
        ravengun::vision::led_color_by_name(led_arg, led);
        led_on = true;
    }
    opt.led_enabled = led_on;
    opt.led_r = led.r; opt.led_g = led.g; opt.led_b = led.b;
    if (!std::isnan(hue)) cam.tracker.hue_deg = hue;
    if (!std::isnan(hue_tol)) cam.tracker.hue_tol_deg = hue_tol;
    if (!std::isnan(min_chroma)) cam.tracker.min_chroma = static_cast<int>(min_chroma);

    if (web_lan && !web_on) {
        std::fprintf(stderr, "ravengund: --web-lan samen met --no-web is tegenstrijdig\n");
        return 64;
    }
    if (!std::isnan(content_aspect)) out_transform.viewport = ravengun::output::letterbox_viewport(screen_aspect, content_aspect);

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

    logf("info", "ravengund gestart (pid %d), socket %s, HID %s, filter %s, schatter %s", static_cast<int>(::getpid()), sock.c_str(),
         opt.enabled ? "aan" : "uit (--no-hid)", opt.pipeline.filter.enabled ? "1€" : "uit", ravengun::core::to_string(opt.pipeline.fusion_mode));
    logf("info", "gun-knoppen (live; PS-knop altijd recenter, ook ongewapend; de rest alleen als de uitvoer gewapend is): trekker = fire-bit van de Sharp Shooter, anders analoge T (in %d / uit %d); pomp = T zonder fire-bit",
         cfg.gun_buttons.trigger_on, cfg.gun_buttons.trigger_off);
    logf("info", "knoppenindeling: %s", ravengun::output::describe_layout(opt.gun_buttons).c_str());

    SharedState shared;
    shared.settings = &settings_store;
    shared.web_port = web_on ? static_cast<int>(web_port) : 0;
    shared.web_lan = web_lan;
    if (use_config) {
        if (settings_load.state == ravengun::config::LoadInfo::State::Missing)
            logf("info", "instellingen: %s bestaat nog niet (standaardwaarden; het bestand wordt bij de eerste wijziging gemaakt)", config_path.c_str());
        else if (settings_load.state == ravengun::config::LoadInfo::State::Loaded)
            logf("info", "instellingen geladen uit %s", config_path.c_str());
        else
            logf("waarschuwing", "instellingen: %s", settings_load.message.c_str());
        for (const auto& w : settings_load.warnings) logf("waarschuwing", "instellingen: %s", w.c_str());
    } else {
        logf("info", "instellingen: --no-config, er wordt niets gelezen of bewaard");
    }
    OutputService output;  // standaard disarmed; maakt pas bij 'arm' het uinput-apparaat
    output.set_initial_transform(out_transform);
    output.set_layout(opt.gun_buttons.layout);
    shared.output = &output;
    // Opnamen (fase 0): de recorder leeft tot het einde van main; de HID-thread stopt hem bij het
    // afsluiten. De tracker-thread levert optische metingen via de hook.
    Recorder recorder(ravengun::rec::default_recordings_dir());
    opt.recorder = &recorder;
    shared.optical_hook = [&recorder](const ravengun::core::OpticalSample& o) { recorder.push_optical(o); };
    CameraService camera(shared, cam);
    shared.camera = &camera;
    shared.camera_status = [&camera] { return camera.status_json(); };
    if (cam.enabled)
        logf("info", "camera aan (%s, %dx%d@%d, %s), LED %s (%d,%d,%d), tracker-tint %.0f +- %.0f graden",
             cam.device.empty() ? "automatisch" : cam.device.c_str(), cam.width, cam.height, cam.fps,
             (std::string("belichting ") + (cam.auto_exposure ? "auto" : std::to_string(cam.exposure).c_str()) + ", gain " +
             (cam.auto_gain ? "auto" : std::to_string(cam.gain).c_str()) + ", AWB " + (cam.auto_awb ? "auto" : "uit")).c_str(),
             led_on ? "aan" : "uit", led.r, led.g, led.b, cam.tracker.hue_deg, cam.tracker.hue_tol_deg);
    std::thread hid(hid_thread_main, std::ref(shared), opt);
    std::thread ipc(ipc_thread_main, std::ref(shared), sfd, wake);
    camera.start();

    // Webinterface: een eigen thread; als hij niet kan starten draait de daemon gewoon door.
    std::thread web;
    int wfd = -1;
    if (web_on) {
        ravengun::web::WebConfig wc;
        wc.port = static_cast<int>(web_port);
        wc.lan = web_lan;
        wc.csrf = ravengun::web::random_hex(16);
        std::string werr;
        if (web_lan) {
            std::string tpath = ravengun::web::default_token_path();
            wc.token = ravengun::web::load_or_create_token(tpath, werr);
            char hn[256] = {0};
            if (::gethostname(hn, sizeof hn - 1) == 0) wc.hostname = hn;
            if (!wc.token.empty())
                logf("info", "web: LAN-toegang aan; token staat in %s (niet in deze log)", tpath.c_str());
        }
        if (werr.empty()) werr = ravengun::web::validate(wc);
        if (!werr.empty()) {
            logf("fout", "web-interface niet gestart: %s", werr.c_str());
        } else if ((wfd = web_listen(wc)) >= 0) {
            if (web_lan)
                logf("info", "web-interface op http://%s:%d/ (LAN; open eenmalig /?token=<token uit het tokenbestand>)",
                     wc.hostname.empty() ? "<dit-apparaat>" : wc.hostname.c_str(), wc.port);
            else
                logf("info", "web-interface op http://127.0.0.1:%d/ (alleen localhost)", wc.port);
            web = std::thread(web_thread_main, std::ref(shared), wc, wfd, wake);
        } else {
            logf("fout", "web-interface niet gestart (poort bezet?); de daemon draait verder zonder");
        }
    } else {
        logf("info", "web-interface uit (--no-web)");
    }

    int sig = 0;
    sigwait(&set, &sig);
    logf("info", "signaal %s ontvangen, stoppen", sig == SIGINT ? "SIGINT" : "SIGTERM");
    shared.running.store(false);
    uint64_t one = 1;
    if (::write(wake, &one, sizeof one) < 0) { /* poll heeft ook een time-out */ }
    camera.stop();
    hid.join();
    ipc.join();
    if (web.joinable()) web.join();
    if (wfd >= 0) ::close(wfd);
    output.shutdown();  // patroon stoppen, alle knoppen vrijgeven, apparaat verwijderen
    ::close(sfd);
    ::unlink(sock.c_str());
    ::close(wake);
    ::close(lfd);  // lockfile blijft bestaan; alleen het slot vervalt (geen unlink-race)
    logf("info", "ravengund gestopt");
    return 0;
}
