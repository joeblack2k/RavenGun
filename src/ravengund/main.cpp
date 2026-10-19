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
#include <pwd.h>
#include <unistd.h>

#include "aagames_service.hpp"
#include "iobridge_service.hpp"
#include "camera.hpp"
#include "commands.hpp"
#include "controllers.hpp"
#include "ravengun/games/aagames.hpp"
#include "ravengun/gamesettings/controls_settings.hpp"
#include "ravengun/gamesettings/gamesettings.hpp"
#include "ravengun/iobridge/hid_board.hpp"
#include "ravengun/iobridge/routes.hpp"
#include "ravengun/launch/direct.hpp"
#include "ravengun/iobridge/tp_shm.hpp"
#include "ravengun/games/games.hpp"
#include "ravengun/paths.hpp"
#include "ravengun/web/web_core.hpp"
#include "ravengun/config/led_color.hpp"
#include "ravengun/config/settings.hpp"
#include "hid_thread.hpp"
#include "ravengun/core/bias_store.hpp"
#include "ipc_server.hpp"
#include "mouse_service.hpp"
#include "gamepad_service.hpp"
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
        "  --min-cutoff HZ                  1€ min_cutoff (standaard uit config.json, anders 0.3)\n"
        "  --beta B                         1€ beta (standaard uit config.json, anders 10)\n"
        "  --predict-ms MS                  voorspel de oriëntatie zoveel ms vooruit (0 = uit; standaard uit config.json,\n"
        "                                   anders 40); live instelbaar met 'ravengunctl predict'\n"
        "  --clock-hz HZ                    uitvoerklok in Hz (0 = uit: positie alleen per rapport; standaard 250)\n"
        "  --extrapolate-ms MS              de klok extrapoleert maximaal zo lang na het laatste rapport (standaard 40)\n"
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
        "  --no-recenter-learning           yaw-trim niet bijwerken uit centreerdrukken (standaard aan, of\n"
        "                                   fusion.recenter_learning in config.json)\n"
        "  --fusion mahony|mahony_mag|vqf|vqf_mag\n"
        "                                   oriëntatieschatter (standaard uit config.json, anders mahony;\n"
        "                                   mahony_mag = Mahony plus yaw-correctie met de magnetometer;\n"
        "                                   vqf = VQF 6D, vqf_mag = VQF 9D met de magnetometer)\n"
        "  --vqf-bias external|own          gyro-bias voor vqf/vqf_mag: die van onze schatter (standaard, of\n"
        "                                   fusion.vqf_bias in config.json) of VQF's eigen schatting\n"
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
        "  --web-lan                        ook op het thuisnetwerk (0.0.0.0); ook via web.lan in config.json\n"
        "  --web-require-token              met LAN: apparaten op het netwerk moeten het token uit\n"
        "                                   ~/.config/ravengun/web-token meegeven (ook via web.require_token)\n"
        "\n"
        "controllers (koppelen en verwijderen vanuit de webinterface en ravengunctl):\n"
        "  --controllers-fake PAD           nep-systeem (JSON) voor de controllerlijst en de koppelwizard; voor tests en\n"
        "                                   schermafdrukken zonder hardware (raakt Bluetooth en hidraw niet)\n"
        "\n"
        "games (starten vanuit de webinterface; de daemon start elke game zelf in de unit ravengun-tpui):\n"
        "  --tp-dir MAP                     TeknoParrot-map met UserProfiles/ en Icons/ (standaard\n"
        "                                   ~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64)\n"
        "  --data-dir MAP                   datamap met packaging/ en patches/ (standaard $RAVENGUN_DATA_DIR,\n"
        "                                   <prefix>/share/ravengun of de repo)\n"
        "  --aagames-config PAD             gedeelde instellingen van de Unity-route (poort, versies; Tomb Raider, Rabbids Hollywood\n"
        "                                   zonder TeknoParrot; standaard packaging/unity-route.json in de datamap, anders de\n"
        "                                   standaardwaarden). De games zelf: patches/<Game>/unity.json\n"
        "  --games-dry-run                  nooit echt starten of stoppen (voor tests): alleen de opdrachtregel loggen;\n"
        "                                   draaiende processen op de host tellen dan niet mee\n"
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
    opt.pipeline.predict.horizon_ms = cfg.aim.predict_ms;
    ravengun::output::OutputClockConfig out_clock;
    out_clock.hz = cfg.output.clock_hz;
    out_clock.max_extrap_ms = cfg.output.extrapolate_ms;
    ravengun::core::parse_fusion_mode(cfg.fusion.mode.c_str(), opt.pipeline.fusion_mode);
    ravengun::core::parse_vqf_bias(cfg.fusion.vqf_bias.c_str(), opt.pipeline.vqf.bias);
    opt.pipeline.trim.enabled = cfg.fusion.recenter_learning;
    opt.bias_dir = ravengun::core::default_bias_dir();
    opt.rumble = cfg.rumble;
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
    bool web_on = true, web_lan = false, web_require_token = false;
    double web_port = cfg.web.port;
    double hue = NAN, hue_tol = NAN, min_chroma = NAN;
    const char* home_env = std::getenv("HOME");
    std::string home = home_env && *home_env ? home_env : "";
    if (home.empty())   // zonder HOME: de thuismap van deze gebruiker volgens het systeem (zoals default_direct_paths)
        if (const struct passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_dir) home = pw->pw_dir;
    const char* xd0 = std::getenv("XDG_DATA_HOME");
    // Zonder HOME en zonder XDG_DATA_HOME is er geen datamap: dan ook geen standaard-runtime in
    // plaats van "/.local/share/ravengun/..." (een pad dat niet van deze gebruiker is).
    const std::string data_base = ravengun::paths::xdg_data_base(xd0, home);
    std::string tp_dir = data_base.empty() ? std::string() : data_base + "/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64";
    bool tp_dir_given = false;   // --tp-dir; anders gaat de keuze van de app voor (tp_dir in library.json)
    std::string data_dir_opt;
    std::string aagames_config;
    bool games_dry_run = false;
    std::string controllers_fake;  // nep-systeem voor de controllerlijst en het koppelen (tests, schermafdrukken)
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
        else if (a == "--no-filter") { opt.pipeline.filter.enabled = false; opt.cli_keys.insert("filter.enabled"); }
        else if (a == "--no-auto-center") opt.pipeline.auto_center_on_start = false;
        else if (a == "--no-recenter-learning") { opt.pipeline.trim.enabled = false; opt.cli_keys.insert("fusion.recenter_learning"); }
        else if (a == "--dt-source" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v != "move" && v != "rx") { std::fprintf(stderr, "ravengund: --dt-source verwacht move of rx\n"); return 64; }
            opt.pipeline.use_move_clock = v == "move";
        }
        else if (a == "--fusion" && i + 1 < argc) {
            opt.cli_keys.insert("fusion.mode");
            if (!ravengun::core::parse_fusion_mode(argv[++i], opt.pipeline.fusion_mode)) {
                std::fprintf(stderr, "ravengund: --fusion verwacht mahony, mahony_mag, vqf of vqf_mag\n");
                return 64;
            }
        }
        else if (a == "--vqf-bias" && i + 1 < argc) {
            opt.cli_keys.insert("fusion.vqf_bias");
            if (!ravengun::core::parse_vqf_bias(argv[++i], opt.pipeline.vqf.bias) || opt.pipeline.vqf.bias == ravengun::core::VqfBias::Hybrid) {
                std::fprintf(stderr, "ravengund: --vqf-bias verwacht external of own\n");
                return 64;
            }
        }
        else if (a == "--min-cutoff") { need(0.001, 100, opt.pipeline.filter.min_cutoff_hz); opt.cli_keys.insert("filter.min_cutoff_hz"); }
        else if (a == "--beta") { need(0, 1000, opt.pipeline.filter.beta); opt.cli_keys.insert("filter.beta"); }
        else if (a == "--predict-ms") { need(0, 200, opt.pipeline.predict.horizon_ms); opt.cli_keys.insert("aim.predict_ms"); }
        else if (a == "--clock-hz") need(0, 1000, out_clock.hz);
        else if (a == "--extrapolate-ms") need(0, 100, out_clock.max_extrap_ms);
        else if (a == "--h-edge-deg") { need(1, 89, opt.pipeline.aim.h_edge_deg); opt.cli_keys.insert("aim.h_edge_deg"); }
        else if (a == "--v-edge-deg") { need(1, 89, opt.pipeline.aim.v_edge_deg); opt.cli_keys.insert("aim.v_edge_deg"); }
        else if (a == "--web") web_on = true;
        else if (a == "--no-web") web_on = false;
        else if (a == "--web-lan") web_lan = true;
        else if (a == "--web-require-token") web_require_token = true;
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
        else if (a == "--tp-dir" && i + 1 < argc) { tp_dir = argv[++i]; tp_dir_given = true; }
        else if (a == "--data-dir" && i + 1 < argc) data_dir_opt = argv[++i];
        else if (a == "--aagames-config" && i + 1 < argc) aagames_config = argv[++i];
        else if (a == "--games-dry-run") games_dry_run = true;
        else if (a == "--controllers-fake" && i + 1 < argc) controllers_fake = argv[++i];
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
        else if (a == "--led" && i + 1 < argc) { led_arg = argv[++i]; for (const char* k : {"led.r", "tracker.hue_deg", "tracker.hue_tol_deg"}) opt.cli_keys.insert(k); }
        else if (a == "--hue-deg") { need(-180, 180, hue); opt.cli_keys.insert("tracker.hue_deg"); }
        else if (a == "--hue-tol-deg") { need(1, 90, hue_tol); opt.cli_keys.insert("tracker.hue_tol_deg"); }
        else if (a == "--min-chroma") { need(1, 180, min_chroma); opt.cli_keys.insert("tracker.min_chroma"); }
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
    // Zonder vlag bepaalt config.json (web.lan, web.require_token) de netwerktoegang.
    if (web_on && cfg.web.lan) web_lan = true;
    if (cfg.web.require_token) web_require_token = true;
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
    logf("info", "voorspelling %.0f ms, uitvoerklok %s", opt.pipeline.predict.horizon_ms,
         out_clock.enabled() ? (std::to_string(static_cast<int>(out_clock.hz)) + " Hz, extrapolatie max. " + std::to_string(static_cast<int>(out_clock.max_extrap_ms)) + " ms").c_str() : "uit");
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
    output.set_clock(out_clock);
    shared.output = &output;
    // Gewone muis als bron voor de gun van een speler (runtime-toewijzingen via IPC `set_mouse_gun`, niet in config.json).
    MouseService mouse(output);
    shared.mouse = &mouse;
    mouse.start();
    // Opnamen (fase 0): de recorder leeft tot het einde van main; de HID-thread stopt hem bij het
    // afsluiten. De tracker-thread levert optische metingen via de hook.
    Recorder recorder(ravengun::rec::default_recordings_dir());
    opt.recorder = &recorder;
    // Een opname is per controller: alleen de optische metingen van de speler die opneemt.
    shared.optical_hook = [&recorder, &shared](int player, const ravengun::core::OpticalSample& o) {
        if (player == shared.recording_player.load()) recorder.push_optical(o);
    };
    // Games: de datamap (packaging/ en patches/): --data-dir, anders RAVENGUN_DATA_DIR, <prefix>/share/ravengun of de repo bij
    // ontwikkeling (src/ravengun/paths.hpp).
    std::string data_root = data_dir_opt;
    if (data_root.empty()) {
        if (const auto dd = ravengun::paths::find_data_dir(ravengun::paths::env_from_process())) {
            data_root = dd.path;
            logf("info", "datamap: %s (%s)", dd.path.c_str(), ravengun::paths::source_name(dd.source));
        } else {
            logf("waarschuwing", "datamap met packaging/ niet gevonden (RAVENGUN_DATA_DIR, <prefix>/share/ravengun of de repo): games starten werkt niet");
        }
    }
    ravengun::games::Config games_cfg;
    games_cfg.tp_dir = tp_dir;
    games_cfg.tpui_procs = ravengun::games::default_tpui_procs(static_cast<unsigned>(::getuid()));
    {
        const char* xs = std::getenv("XDG_STATE_HOME");
        std::string state = xs && *xs ? xs : (home.empty() ? std::string() : home + "/.local/state");
        if (!state.empty()) games_cfg.log_path = state + "/ravengun/logs/play-last.log";
    }
    // Notities per game ([Status] Note in patches/<Game>/game.ini); de gebruikerslaag alleen met de gewone configuratie.
    std::string patches_base = data_root;   // zonder patches/ in de datamap zoeken we de gewone datamap
    if (patches_base.empty() || ::access((patches_base + "/patches").c_str(), R_OK) != 0)
        if (const auto dd = ravengun::paths::find_data_dir(ravengun::paths::env_from_process())) patches_base = dd.path;
    games_cfg.dirs = ravengun::gamesettings::default_dirs(patches_base);
    if (!use_config) games_cfg.dirs.user.clear();
    // Games met een eigen profiel in game.ini (zonder UserProfiles-XML): de gamemappen van de app (library.json) en de lokale views.
    if (use_config && !home.empty()) {
        const char* xc = std::getenv("XDG_CONFIG_HOME");
        games_cfg.home = home;
        games_cfg.library_file = (xc && *xc == '/' ? std::string(xc) : home + "/.config") + "/ravengun/library.json";
        if (!data_base.empty()) games_cfg.views_dir = data_base + "/ravengun/runtime/views";   // data_base: zie boven (HOME of XDG_DATA_HOME)
        // De runtime die de gebruiker in de app koos (Settings > Paths); zonder keuze de standaardmap.
        if (!tp_dir_given)
            if (const std::string t = ravengun::games::library_tp_dir(games_cfg.library_file, home); !t.empty()) {
                tp_dir = t;
                games_cfg.tp_dir = t;
            }
    }
    // De native route (patches/<Game>/native.json, src/ravengun/launch/native.*): de daemon start zo'n game zelf.
    {
        auto envs = [](const char* k) { const char* v = std::getenv(k); return std::string(v && *v ? v : ""); };
        ravengun::launch::NativePaths& np = games_cfg.native;
        // Dezelfde data_base als hierboven. Zonder HOME en zonder XDG_DATA_HOME is er geen datamap: dan blijft de native setup op de
        // gegevensmap uit (np.data, de schrijflaag, lib32) in plaats van "/.local/share/ravengun" (een pad dat niet van deze gebruiker is).
        const std::string data_dir = data_base.empty() ? std::string() : data_base + "/ravengun";
        if (!data_dir.empty()) {
            np.data = data_dir;
            games_cfg.write_layer_dir = data_dir + "/writelayer";   // de schrijflaag van de Wine-routes (write_layer in games.cpp)
            np.lib32 = data_dir + "/runtime/lib32";
        } else {
            logf("waarschuwing", "games: geen HOME en geen XDG_DATA_HOME: de native setup en de schrijflagen staan uit");
        }
        // @nas: de spelmappen van de app (library.json), bij elke start opnieuw gelezen (GameService); RAVENGUN_NAS_TP zet er een vast.
        np.nas = envs("RAVENGUN_NAS_TP");
        np.tpui = !envs("RAVENGUN_TPUI").empty() ? envs("RAVENGUN_TPUI") : tp_dir;
        // Eigen bibliotheken zoals scripts/rg-paths.sh: <prefix>/lib bij een installatie (<prefix>/share/ravengun), build/ in de repo.
        std::string lib = envs("RAVENGUN_LIB_DIR");
        if (lib.empty() && !data_root.empty()) {
            const std::string suffix = "/share/ravengun";
            const bool installed = data_root.size() > suffix.size() && data_root.compare(data_root.size() - suffix.size(), suffix.size(), suffix) == 0;
            lib = installed ? data_root.substr(0, data_root.size() - suffix.size()) + "/lib" : data_root + "/build";
        }
        np.build = lib;
        np.patches = games_cfg.dirs.shipped;
        np.user_patches = games_cfg.dirs.user;
        np.loader = envs("RAVENGUN_ELFLOADER");
        // De Lindbergh-laag (scripts/build-lindbergh-loader.sh): standaard <data>/lindbergh-loader/current.
        np.lindbergh = envs("RAVENGUN_LINDBERGH");
        np.runtime_dir = envs("XDG_RUNTIME_DIR");
        np.home = home;
        {   // rg-exec staat naast ravengund (<prefix>/bin bij een installatie, build/ in de repo)
            char self[4096];
            const ssize_t n = ::readlink("/proc/self/exe", self, sizeof self - 1);
            if (n > 0) { self[n] = 0; std::string d(self); np.exec_helper = d.substr(0, d.rfind('/')) + "/rg-exec"; }
        }
        if (!games_cfg.log_path.empty()) games_cfg.native_log_dir = games_cfg.log_path.substr(0, games_cfg.log_path.rfind('/'));
        games_cfg.lightgun_ready = [&shared] {
            auto l = shared.get().sample.link;
            const bool move = l != ravengun::core::LinkState::Lost && l != ravengun::core::LinkState::Disconnected;
            // Een verbonden gamepad van een speler telt ook: een racespel (Mario Kart) speel je met de pad, niet met een Move.
            return move || (shared.mouse && shared.mouse->any_connected()) || (shared.gamepad && shared.gamepad->any_player_connected());
        };
        // De poort vóór de start (games/readiness.hpp, docs/prelaunch.md): per speler een Move (config.json) en of die verbonden is,
        // en een muis als lightgun. Speler 1 met een Move wacht op die Move; zonder Move geldt lightgun_ready hierboven.
        games_cfg.ready_inputs = [&shared] {
            ravengun::games::readiness::Inputs in;
            const ravengun::config::Settings s = shared.settings ? shared.settings->get() : ravengun::config::Settings{};
            const auto slots = shared.slot_infos();
            for (int p = 1; p <= ravengun::games::readiness::kPlayers; ++p) {
                auto& pi = in.players[static_cast<size_t>(p - 1)];
                pi.move_assigned = !ravengun::config::move_for_player(s, p).empty();
                const auto l = shared.get(p).sample.link;
                pi.move_connected = slots[static_cast<size_t>(p - 1)].connected && l != ravengun::core::LinkState::Lost &&
                                    l != ravengun::core::LinkState::Disconnected;
                if (shared.mouse) shared.mouse->player_state(p, pi.mouse_assigned, pi.mouse_connected);
                in.any_move_connected = in.any_move_connected || pi.move_connected;
            }
            in.any_pad_connected = shared.gamepad && shared.gamepad->any_player_connected();
            return in;
        };
        // Lightgun of pad-game, en hoeveel guns: [Input] van game.ini en de indelingen van de IO-brug (tp-shm, JVS, hid-borden).
        games_cfg.game_kind = [&shared, dirs = games_cfg.dirs](const std::string& profile) {
            namespace R = ravengun::games::readiness;
            const ravengun::gamesettings::Game g = ravengun::gamesettings::load_game(dirs, profile);
            std::string err;
            const ravengun::iobridge::IoRoute route = ravengun::iobridge::io_route_from_settings(g, err);
            if (!shared.iobridge) return R::classify({});
            const auto& ic = shared.iobridge->config();
            return R::classify(R::input_info(&route, ic.profiles, ic.boards, ic.jvs_boards, g.get("Input", "GunPlayers")));
        };
        games_cfg.arm = [&shared]() -> std::string {
            if (!shared.output) return "geen uitvoer";
            const auto r = shared.output->arm();
            return r.ok ? std::string() : r.message;
        };
        games_cfg.start_route = [&shared](const std::string& game, const std::string& route) -> std::string {
            return shared.iobridge ? shared.iobridge->start(game, route) : std::string("geen IO-brug");
        };
        // [Display] AimAspect: de viewport van de lightgun per game; daarna (en bij stoppen) weer de standaard van de start.
        const ravengun::output::GunTransform basis_transform = out_transform;
        games_cfg.set_aim_aspect = [&shared, basis_transform, screen_aspect](double a) {
            if (!shared.output) return;
            ravengun::output::GunTransform t = basis_transform;
            if (a > 0) t.viewport = ravengun::output::letterbox_viewport(screen_aspect, a);
            shared.output->set_transform(t);
            logf("info", "games: aim-ruimte %s", a > 0 ? ("op het beeld van het spel (" + std::to_string(a).substr(0, 5) + ")").c_str() : "standaard");
        };
        // [Controls] per game: de Move-knoppen van de gebruiker voor deze game over de indeling heen (apply_game_controls); stoppen zet
        // de globale indeling terug. Een game zonder keuzes laat alles zoals het is.
        games_cfg.set_controls = [&shared, dirs = games_cfg.dirs](const std::string& profile) {
            std::array<std::vector<std::pair<std::string, std::string>>, ravengund::kPlayers> per;
            const auto g = ravengun::gamesettings::load_game(dirs, profile);
            for (int p = 1; p <= ravengund::kPlayers; ++p) {
                std::vector<std::string> warn;
                per[static_cast<size_t>(p - 1)] = ravengun::gamesettings::controls_overrides(g.settings, p, &warn);
                for (const auto& w : warn) logf("waarschuwing", "games: %s: %s", profile.c_str(), w.c_str());
            }
            ravengund::apply_game_controls(shared, per);
            for (int p = 1; p <= ravengund::kPlayers; ++p)
                if (!per[static_cast<size_t>(p - 1)].empty()) logf("info", "games: eigen knoppen van %s voor speler %d (%zu)", profile.c_str(), p, per[static_cast<size_t>(p - 1)].size());
        };
        games_cfg.stop_inputs = [&shared, basis_transform] {
            ravengund::apply_game_controls(shared, {});   // de globale knoppenindeling terug (doet niets als er geen eigen knoppen waren)
            if (shared.iobridge) shared.iobridge->stop();
            if (shared.aagames) shared.aagames->stop();   // de TCP-klant van de Unity-plugin
            if (shared.output) {
                shared.output->disarm();
                shared.output->set_transform(basis_transform);
            }
        };
        logf("info", "games: native profielen uit patches/<Game>/native.json (gebruikerslaag %s, meegeleverd %s)", np.user_patches.c_str(), np.patches.c_str());
        // De Unity-route (patches/<Game>/unity.json, src/ravengun/launch/unity.*): view, Proton, TCP-klant. De gedeelde instellingen
        // (poort, versies) staan in packaging/unity-route.json.
        ravengun::launch::UnityPaths& up = games_cfg.unity;
        if (!aagames_config.empty()) up.route_json = aagames_config;
        else if (!data_root.empty()) up.route_json = data_root + "/packaging/unity-route.json";
        up.patches = games_cfg.dirs.shipped;
        up.user_patches = games_cfg.dirs.user;
        up.home = home;
        up.data = data_dir;
        // De plugin (scripts/build-unity-plugin.sh): $RAVENGUN_UNITY_PLUGIN_OUT, de gebruikersdata, dan naast de eigen bibliotheken
        // (<prefix>/lib/unity-plugin bij een installatie, build/unity-plugin in de repo).
        up.plugin_dirs = {envs("RAVENGUN_UNITY_PLUGIN_OUT"), data_dir.empty() ? std::string() : data_dir + "/unity-plugin", lib.empty() ? std::string() : lib + "/unity-plugin"};
        if (!data_root.empty()) up.plugin_src = data_root + "/plugins/unity/src";
        // GE-Proton 11-7 als ons onderdeel (packaging/components.json, ge-proton), niet meer uit de map van TeknoParrotUi.
        up.proton = !envs("RAVENGUN_UNITY_PROTON").empty() ? envs("RAVENGUN_UNITY_PROTON")
                    : data_dir.empty() ? std::string() : ravengun::launch::default_unity_proton(data_dir, home);
        logf("info", "games: Unity-games uit patches/<Game>/unity.json (gebruikerslaag %s, meegeleverd %s), instellingen %s, Proton %s", up.user_patches.c_str(),
             up.patches.c_str(), up.route_json.c_str(), up.proton.c_str());
        // De directe route (A10, [Launch] Mode = direct): de daemon start zo'n game zelf met rg-direct als hoofdproces van de unit,
        // met rg-direct als hoofdproces. Wat de engine nog niet kan, start niet (de melding zegt waarom).
        ravengun::launch::DirectPaths& dp = games_cfg.direct;
        dp = ravengun::launch::default_direct_paths(ravengun::launch::environ_map());
        if (std::getenv("RAVENGUN_TP_DIR") == nullptr && !tp_dir.empty()) dp.tp = tp_dir;   // dezelfde runtime als de rest van de daemon (--tp-dir of de app)
        dp.dirs = games_cfg.dirs;
        if (!games_cfg.library_file.empty()) dp.library_file = games_cfg.library_file;
        if (!np.exec_helper.empty()) {
            const std::string runner = np.exec_helper.substr(0, np.exec_helper.rfind('/')) + "/rg-direct";
            if (::access(runner.c_str(), X_OK) == 0) games_cfg.direct_runner = runner;
        }
        {
            const size_t sl = sock.rfind('/');
            if (sl != std::string::npos) games_cfg.jvs_link = sock.substr(0, sl) + "/jvs-com";   // gelijk aan io_cfg.jvs_link hieronder
        }
        games_cfg.input_by_id = "/dev/input/by-id";
        auto packaging_text = [data_root](const char* file) {
            std::string text;
            if (data_root.empty()) return text;
            if (FILE* f = std::fopen((data_root + "/packaging/" + file).c_str(), "rb")) {
                char buf[4096];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 256 * 1024) text.append(buf, n);
                std::fclose(f);
            }
            return text;
        };
        games_cfg.hid_vidpid = [packaging_text](const std::string& board) -> std::string {
            std::string text = packaging_text("hid-boards.json"), err;
            if (text.empty())
                if (const auto* a = ravengun::web::find_asset("hid-boards.json")) text = a->data;
            const auto boards = ravengun::iobridge::parse_hid_boards(text, err);
            const auto* b = boards.find(board);
            if (!b) return {};
            char v[16];
            std::snprintf(v, sizeof v, "%04X:%04X", b->vendor & 0xFFFF, b->product & 0xFFFF);
            return v;
        };
        games_cfg.unity_game = [unity = games_cfg.unity](const std::string& profile) { return ravengun::launch::unity_game_known(unity, profile); };
        if (!games_cfg.direct_runner.empty())
            logf("info", "games: directe starten ([Launch] Mode = direct) doet de daemon zelf met %s", games_cfg.direct_runner.c_str());
        else
            logf("waarschuwing", "games: rg-direct ontbreekt naast ravengund; directe starten werken niet");
    }
    const ravengun::launch::UnityPaths unity_paths = games_cfg.unity;   // voor AagamesService; een droge run leegt de lagen hieronder
    ravengun::games::GameService::Spawner spawner = ravengun::games::run_process;
    if (games_dry_run) {
        // Geen invloed van echte processen op de host (tests): geen cgroup van de unit, een niet-bestaande procesmap.
        games_cfg.tpui_procs.clear();
        games_cfg.proc_root = "/nonexistent-ravengun-proc";
        // De directe route zet drop-ins en teknoparrot.ini in de view voordat hij iets start: in een droge run niet.
        games_cfg.direct_runner.clear();
        games_cfg.unity.patches.clear();   // de Unity-route zet de view klaar voordat hij iets start: in een droge run ook niet
        games_cfg.unity.user_patches.clear();
        spawner = [](const std::vector<std::string>& args, std::string&) {
            std::string line;
            for (const auto& a : args) line += (line.empty() ? "" : " ") + a;
            logf("info", "games (dry-run): %s", line.c_str());
            return 0;
        };
    }
    ravengun::games::GameService games(games_cfg, spawner);
    shared.games = &games;
    // IO-brug: routes per game (patches/<Game>/game.ini) en de gedeelde bibliotheken (JSON in de datamap, anders ingebouwd).
    auto load_text = [&](const std::string& file, const std::string& asset) {
        std::string text;
        std::string cand = data_root;
        if (!cand.empty()) {
            cand += "/packaging/" + file;
            if (FILE* f = std::fopen(cand.c_str(), "rb")) {
                char buf[4096];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 256 * 1024) text.append(buf, n);
                std::fclose(f);
            }
        }
        if (text.empty())
            if (const auto* a = ravengun::web::find_asset(asset.c_str())) text = a->data;
        return text;
    };
    IoBridgeService::Config io_cfg;
    {
        std::string e2, e3;
        io_cfg.dirs = games_cfg.dirs;   // [Input] van patches/<Game>/game.ini, bij elke start opnieuw gelezen
        if (io_cfg.dirs.shipped.empty()) logf("waarschuwing", "IO-brug: de map patches/ is niet gevonden (alle games gebruiken evdev)");
        io_cfg.profiles = ravengun::iobridge::parse_tp_shm_profiles(load_text("tp-shm-profiles.json", "tp-shm-profiles.json"), e2);
        std::string e5;
        io_cfg.control_pipes =
            ravengun::iobridge::parse_control_pipe_profiles(load_text("control-pipe-profiles.json", "control-pipe-profiles.json"), e5);
        if (!e5.empty()) logf("waarschuwing", "IO-brug: %s (geen besturingspipes)", e5.c_str());
        io_cfg.boards = ravengun::iobridge::parse_hid_boards(load_text("hid-boards.json", "hid-boards.json"), e3);
        std::string e4;
        io_cfg.jvs_boards = ravengun::iobridge::parse_jvs_boards(load_text("jvs-boards.json", "jvs-boards.json"), e4);
        if (!e4.empty()) logf("waarschuwing", "IO-brug: %s (geen JVS-borden)", e4.c_str());
        if (!e3.empty()) logf("waarschuwing", "IO-brug: %s (geen hid-borden)", e3.c_str());
        if (!e2.empty()) logf("waarschuwing", "IO-brug: %s (geen tp-shm-indelingen)", e2.c_str());
        size_t sl = sock.rfind('/');
        if (sl != std::string::npos) io_cfg.state_path = sock.substr(0, sl) + "/iobridge.json";
        if (sl != std::string::npos) io_cfg.jvs_link = sock.substr(0, sl) + "/jvs-com";
        io_cfg.tpui_procs = games_cfg.tpui_procs;
        if (use_config && !home.empty()) {
            const char* xc = std::getenv("XDG_CONFIG_HOME");
            io_cfg.app_conf = (xc && *xc == '/' ? std::string(xc) : home + "/.config") + "/ravengun/ravengun-app.conf";
        }
    }
    // Gamepad naast de lightgun (docs/gamepad.md): toewijzingen bewaard in config.json, [Gamepad] van de game die de IO-brug start.
    // Met --no-hid worden er geen apparaten geopend (tests).
    GamepadServiceConfig pad_cfg;
    pad_cfg.dirs = games_cfg.dirs;
    pad_cfg.open_devices = opt.enabled;
    if (use_config) {
        pad_cfg.load = [&settings_store] { return settings_store.get().gamepads; };
        pad_cfg.save = [&settings_store](const GamepadServiceConfig::Assignments& a) {
            const auto r = settings_store.update([&](ravengun::config::Settings& s) { s.gamepads = a; });
            return r.ok ? std::string() : r.message;
        };
    }
    GamepadService gamepad(output, std::move(pad_cfg));
    shared.gamepad = &gamepad;
    gamepad.start();
    if (!opt.enabled) logf("info", "gamepads: --no-hid, er worden geen gamepads geopend");
    // De Unity-route leest patches/<Game>/unity.json bij elke start (de lagen van de games, ook in een droge run).
    AagamesService aagames(shared, unity_paths);
    shared.aagames = &aagames;
    IoBridgeService iobridge(shared, std::move(io_cfg));
    shared.iobridge = &iobridge;
    iobridge.resume();   // na een herstart: een lopende tp-shm-route weer oppakken
    // Controllers: lijst, koppelwizard, verwijderen (BlueZ via busctl, de hulp met rootrechten via sudo -n).
    std::unique_ptr<ControllerOps> ctl_ops;
    if (!controllers_fake.empty()) {
        std::string text, ferr;
        if (FILE* f = std::fopen(controllers_fake.c_str(), "rb")) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() < 256 * 1024) text.append(buf, n);
            std::fclose(f);
        }
        ctl_ops = make_fake_controller_ops(text, ferr);
        if (!ctl_ops) { std::fprintf(stderr, "ravengund: --controllers-fake: %s\n", ferr.c_str()); return 64; }
        logf("info", "controllers: nep-systeem uit %s (geen Bluetooth of hidraw)", controllers_fake.c_str());
    } else {
        ctl_ops = make_system_controller_ops();
    }
    ControllerService controllers(shared, std::move(ctl_ops));
    // Zonder HID (tests): de gekoppelde controllers van deze pc niet in het instellingenbestand opnemen.
    controllers.set_auto_register(opt.enabled || !controllers_fake.empty());
    shared.controllers = &controllers;
    CameraService camera(shared, cam);
    shared.camera = &camera;
    shared.camera_status = [&camera] { return camera.status_json(); };
    shared.set_game_camera = [&camera](bool on, const std::string& why) { camera.set_game_feed(on, why); };
    shared.game_camera_on = [&camera] { return camera.game_feed(); };
    shared.set_game_camera_photo = [&camera](bool photo) { camera.set_game_photo(photo); };
    shared.camera_device = [&camera] { return camera.device_path(); };
    shared.camera_enabled = [&camera] { return camera.enabled(); };
    if (cam.enabled)
        logf("info", "camera aan (%s, %dx%d@%d, %s), LED %s (%d,%d,%d), tracker-tint %.0f +- %.0f graden",
             cam.device.empty() ? "automatisch" : cam.device.c_str(), cam.width, cam.height, cam.fps,
             (std::string("belichting ") + (cam.auto_exposure ? "auto" : std::to_string(cam.exposure).c_str()) + ", gain " +
             (cam.auto_gain ? "auto" : std::to_string(cam.gain).c_str()) + ", AWB " + (cam.auto_awb ? "auto" : "uit")).c_str(),
             led_on ? "aan" : "uit", led.r, led.g, led.b, cam.tracker.hue_deg, cam.tracker.hue_tol_deg);
    std::thread hid(hid_thread_main, std::ref(shared), opt);
    std::thread ipc(ipc_thread_main, std::ref(shared), sfd, wake);
    camera.start();
    controllers.start();

    // Webinterface: een eigen thread; als hij niet kan starten draait de daemon gewoon door.
    std::thread web;
    int wfd = -1;
    if (web_on) {
        ravengun::web::WebConfig wc;
        wc.port = static_cast<int>(web_port);
        wc.lan = web_lan;
        wc.token_required = web_require_token;
        wc.csrf = ravengun::web::random_hex(16);
        std::string werr;
        if (web_lan) {
            char hn[256] = {0};
            if (::gethostname(hn, sizeof hn - 1) == 0) wc.hostname = hn;
            if (web_require_token) {
                std::string tpath = ravengun::web::default_token_path();
                wc.token = ravengun::web::load_or_create_token(tpath, werr);
                if (!wc.token.empty())
                    logf("info", "web: LAN-toegang aan met token; het token staat in %s (niet in deze log)", tpath.c_str());
            } else {
                logf("info", "web: LAN-toegang aan zonder token (web.require_token staat uit)");
            }
        }
        if (werr.empty()) werr = ravengun::web::validate(wc);
        if (!werr.empty()) {
            logf("fout", "web-interface niet gestart: %s", werr.c_str());
        } else if ((wfd = web_listen(wc)) >= 0) {
            if (web_lan)
                logf("info", "web-interface op http://%s:%d/ (LAN%s)",
                     wc.hostname.empty() ? "<dit-apparaat>" : wc.hostname.c_str(), wc.port,
                     web_require_token ? "; open eenmalig /?token=<token uit het tokenbestand>" : ", zonder token");
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
    games.shutdown();      // een start die nog op de Move wacht: afbreken; de worker gebruikt de services hieronder
    uint64_t one = 1;
    if (::write(wake, &one, sizeof one) < 0) { /* poll heeft ook een time-out */ }
    mouse.stop();          // geeft de knoppen van alle muisspelers vrij
    gamepad.stop();        // idem voor de gamepadspelers
    camera.stop();
    controllers.stop();
    aagames.stop();
    iobridge.shutdown();   // invoer neutraal; de route blijft staan voor een herstart
    hid.join();
    ipc.join();
    if (web.joinable()) web.join();
    // De camera gaat eerder uit de lucht dan de IO-brug (die bij het stoppen de camera voor het spel uitzet).
    shared.camera = nullptr;
    shared.camera_status = nullptr;
    shared.set_game_camera = nullptr;
    shared.set_game_camera_photo = nullptr;
    shared.game_camera_on = nullptr;
    shared.camera_device = nullptr;
    shared.camera_enabled = nullptr;
    if (wfd >= 0) ::close(wfd);
    output.shutdown();  // patroon stoppen, alle knoppen vrijgeven, apparaat verwijderen
    ::close(sfd);
    ::unlink(sock.c_str());
    ::close(wake);
    ::close(lfd);  // lockfile blijft bestaan; alleen het slot vervalt (geen unlink-race)
    logf("info", "ravengund gestopt");
    return 0;
}
