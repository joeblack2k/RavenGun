// ravengunctl: diagnose (alleen-lezen) en beheer van ravengund via de Unix-socket.
#include <atomic>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>
#include <time.h>
#include <vector>

#include "ravengun/core/mag_yaw.hpp"
#include "ravengun/doctor.hpp"
#include "ravengun/ipc_client.hpp"
#include "ravengun/ipc_protocol.hpp"
#include "ravengun/json_parse.hpp"
#include "ravengun/probe.hpp"
#include "ravengun/report.hpp"
#include "ravengun/vision/ball_tracker.hpp"
#include "ctl_common.hpp"

namespace {

using namespace ravengun;
using namespace ravengunctl;

void usage(FILE* f) {
    std::fputs(
        "gebruik: ravengunctl <commando> [opties]\n"
        "\n"
        "commando's (alleen-lezen, zonder daemon):\n"
        "  doctor      controleert camera, Move, Bluetooth, uinput en PSMoveAPI en geeft per punt een herstelactie\n"
        "  devices     toont de gevonden camera's, Sony HID-apparaten, Bluetooth-adapters en uinput\n"
        "\n"
        "commando's (via ravengund):\n"
        "  status      verbinding, batterij, oriëntatie, aim, geldigheid, rapportfrequentie en gaten\n"
        "  recenter    legt de huidige richting van de Move vast als midden van het scherm\n"
        "  trim        toont de geleerde yaw-trim (uit centreerdrukken); 'trim reset' wist hem\n"
        "  watch       toont live aim en knoppen (rate-limited) tot Ctrl-C of --duration\n"
        "  filter      zet het 1€-filter: --on/--off, --min-cutoff HZ, --beta B, --d-cutoff HZ\n"
        "  predict     voorspel de oriëntatie MS vooruit (latentiecompensatie; 0 = uit): 'predict 40'; zonder waarde\n"
        "              toont de huidige; live en bewaard in config.json (aim.predict_ms)\n"
        "  clock       uitvoerklok: 'clock 250' (Hz, 0 = uit: positie alleen per rapport), optioneel\n"
        "              --extrapolate-ms MS (max. extrapolatie na het laatste rapport); zonder waarde toont de huidige\n"
        "  rumble      trillen bij schieten: 'rumble on|off', 'rumble --strength 0..255 --pulse-ms 10..500 --repeat-hz 0..30\n"
        "              --pump-strength 0..255' (repeat-hz 0 = één puls per schot; pump-strength 0 = geen pomppuls),\n"
        "              'rumble test [ms]' (één puls, ook ongewapend); zonder argumenten toont de instelling; live en bewaard\n"
        "  camera      camera-identiteit, modus, gemeten fps en intervallen, controls en trackerstatistiek\n"
        "  record      opname (alleen getallen) naar ~/.local/state/ravengun/recordings/:\n"
        "                record [--tag T] [--duration S] [--max-mb N]   opnemen tot S s of Ctrl-C\n"
        "                record --start|--stop|--status, record --mark LABEL\n"
        "  led         LED-kleur: led cyaan|rood|groen|blauw|wit|magenta|geel|uit of led R,G,B\n"
        "  fusion      oriëntatieschatter: fusion mahony (IMU-only), mahony_mag (met magnetometer-yaw), vqf (VQF 6D)\n"
        "              of vqf_mag (VQF 9D met magnetometer); wordt bewaard in config.json\n"
        "  calibrate   calibrate mag [--holder H] [--duration S]: magnetometer kalibreren (ronddraaien;\n"
        "              stopt zelf bij >= 80 % dekking en >= 300 samples), of --start|--stop|--abort|--status;\n"
        "              calibrate mag --check [--duration 3]: veldcheck (norm en dip t.o.v. de kalibratie)\n"
        "\n"
        "controllers (meerdere Moves, Navigation Controller, koppelen en verwijderen):\n"
        "  controllers lijst van bekende controllers: naam, type, speler, verbonden, batterij, laatst gezien, LED\n"
        "  pair        nieuwe controller koppelen (stap voor stap, zoals de wizard in de app): USB-kabel erin, het\n"
        "              adres van deze pc wordt in de controller gezet, registratie bij Bluetooth (hulp met rootrechten),\n"
        "              kabel eruit en PS-knop; --status toont de toestand, --abort breekt af\n"
        "  unpair ADRES  controller verwijderen (ADRES = Bluetooth-adres of de sleutel uit 'controllers');\n"
        "              standaard blijven de instellingen bewaard voor als hij terugkomt, --forget wist ze\n"
        "  controller ADRES [--name NAAM] [--player 1..4] [--led KLEUR|R,G,B] [--reset] [--blink]\n"
        "              naam, spelernummer (bezet = ruilen), LED-kleur, eigen instellingen wissen, of laten knipperen\n"
        "  --player N  bij status, watch, recenter, trim, filter, predict, rumble, led, fusion, record, calibrate:\n"
        "              voor speler N (standaard speler 1)\n"
        "  calibrate-ui  opent het fullscreen kalibratiescherm (stappenwizard, bediening met de controller) in een\n"
        "              kiosk-Brave op de tv; --close sluit alleen dat kioskproces, --status toont of het open is\n"
        "\n"
        "uitvoer (virtueel lightgun-apparaat; standaard DISARMED en dan gaan er geen events heen):\n"
        "  arm         schakelt de uitvoer in en maakt het apparaat aan (RavenGun Virtual Lightgun, 1209:7202)\n"
        "  disarm      stopt een patroon, geeft alle knoppen vrij en schakelt de uitvoer uit\n"
        "  output      status van de uitvoer (armed, apparaat, positie, knoppen, transformatie, patroon)\n"
        "  test-pattern center|corners|grid|sweep|stop   synthetische posities zonder Move (vereist arm)\n"
        "  test-pattern point X Y                         één punt, X en Y in [0,1] (linksboven 0 0)\n"
        "  output-button trigger|reload|grenade|aux1|aux2 [down|up|tap]   testknop (standaard tap)\n"
        "              test-pattern en output-button: --player N voor speler N (standaard 1; bv. P2 kalibreren in een testmenu)\n"
        "  key NAAM [down|up|tap]    toets op het virtuele toetsenbord (munt 5, start 1..4, test 0), standaard tap;\n"
        "              --player N kiest de speler; vereist arm\n"
        "  aagames start GAME|stop|status   Unity-route zonder TeknoParrot (Tomb Raider, Rabbids Hollywood): de daemon wordt\n"
        "              klant van de BepInEx-plugin in de game; GAME = TombRaider of RabbidsHollywood\n"
        "  game start PROFIEL|stop|status   een game starten of stoppen zoals een dubbelklik in de app (de daemon start hem zelf,\n"
        "              zonder script); status = de toestand van de laatste start\n"
        "  game ready PROFIEL   de poort vóór de start: lightgun- of pad-game, op wiens PS Move de start wacht, wie kalibreert\n"
        "  iobridge start GAME [--route evdev|tp-shm|unity-tcp|hid-board|jvs] | stop | status | route GAME   IO-brug van de daemon: de\n"
        "              route van een game ([Input] in patches/<Game>/game.ini) starten of stoppen, de actieve route tonen\n"
        "  mice        gewone muizen (evdev) die als lightgun-bron kunnen dienen en de toewijzing per speler\n"
        "  mouse-gun --player N --mouse ID [--counts C] | --off   muis ID (uit 'mice') als bron van de virtuele lightgun van speler N,\n"
        "              of --off om los te koppelen; C = counts per schermbreedte (100..20000, standaard 1920). Alleen\n"
        "              runtime: niet bewaard, de app past het opnieuw toe bij het verbinden. De muis wordt niet gegrabd\n"
        "  pads [--all] gamepads (evdev) naast de lightgun: aangesloten pads met live stand, toewijzing per speler en de\n"
    "              indeling van de actieve game; --all toont per invoerapparaat waarom het wel of geen gamepad is\n"
    "  pad --player N --pad ID | --off   gamepad ID (uit 'pads') bij speler N, of --off om los te koppelen (speler 1 krijgt dan\n"
    "              automatisch de enige aangesloten pad). Bewaard in config.json; de pad wordt niet gegrabd\n"
    "  set-output  transformatie van de uitvoer: --invert-x 0|1, --invert-y 0|1, --scale-x/-y S,\n"
        "              --offset-x/-y D, --letterbox SCHERM_ASPECT CONTENT_ASPECT (bv. 1.7778 1.3333)\n"
        "\n"
        "opties:\n"
        "  --json             machineleesbare uitvoer (JSON op stdout; bij watch één regel per frame)\n"
        "  --full-addresses   toon volledige Bluetooth-adressen (standaard geredigeerd)\n"
        "  --rate HZ          watch: max. frames per seconde (1..250, standaard 10; --json: 30)\n"
        "  --duration S       watch/record/calibrate: stop na S seconden; test-pattern sweep: duur (standaard 12)\n"
        "  --dwell MS         test-pattern corners/grid: tijd per punt (standaard 2000)\n"
        "  --margin M         test-pattern: marge vanaf de rand (corners 0, grid 0,1, sweep 0,05)\n"
        "  --tag T            record: label in de bestandsnaam (a-z, 0-9, _ en -)\n"
        "  --max-mb N         record: maximale bestandsgrootte (standaard 64)\n"
        "  --holder H         calibrate mag: houder, bijv. sharp_shooter of los\n"
        "  -h, --help         deze tekst\n"
        "\n"
        "exitcodes:\n"
        "  0   doctor: alles in orde; overige: gelukt\n"
        "  1   doctor: minstens een waarschuwing, geen fouten\n"
        "  2   doctor: minstens een fout\n"
        "  64  verkeerd gebruik (onbekend commando of optie)\n"
        "  69  ravengund niet bereikbaar (draait de daemon?)\n"
        "  70  interne fout\n"
        "  75  daemon weigert tijdelijk (bijv. recenter zonder verbonden Move)\n"
        "  76  onverwacht antwoord van de daemon\n"
        "\n"
        "doctor en devices wijzigen niets op het systeem.\n", f);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace ravengun;
    try {
        std::string cmd;
        bool json = false, full = false;
        double rate = -1, duration = -1, min_cutoff = -1, beta = -1, d_cutoff = -1, extrap_ms = -1, dwell = -1, margin = -1, max_mb = -1;
        double r_strength = -1, r_pulse = -1, r_repeat = -1, r_pump = -1;
        double inv_x = -1, inv_y = -1, sc_x = -1, sc_y = -1, off_x = NAN, off_y = NAN, lb_screen = -1, lb_content = -1;
        std::vector<std::string> pos;
        int filter_on = -1;
        std::string arg2, tag, holder, mark, action;
        std::string ctl_name, ctl_led;
        std::string io_route;   // iobridge start: route afdwingen
        std::string mouse_id;   // mouse-gun --mouse
        bool have_mouse = false;
        std::string pad_id;     // pad --pad
        bool have_pad = false, all_devices = false;
        double mouse_counts = -1;
        bool ctl_reset = false, ctl_blink = false, ctl_forget = false;
        auto need = [&](int& i, const char* name, double lo, double hi) -> double {
            if (i + 1 >= argc) { std::fprintf(stderr, "ravengunctl: %s mist een waarde\n", name); std::exit(kExitUsage); }
            char* end = nullptr;
            double v = std::strtod(argv[++i], &end);
            if (!end || *end || !(v >= lo && v <= hi)) { std::fprintf(stderr, "ravengunctl: ongeldige waarde voor %s\n", name); std::exit(kExitUsage); }
            return v;
        };
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-h" || a == "--help") { usage(stdout); return kExitOk; }
            else if (a == "--json") json = true;
            else if (a == "--full-addresses") full = true;
            else if (a == "--rate") rate = need(i, "--rate", ipc::kWatchRateMin, ipc::kWatchRateMax);
            else if (a == "--duration") duration = need(i, "--duration", 0.1, 1e7);
            else if (a == "--min-cutoff") min_cutoff = need(i, "--min-cutoff", 0.001, 100);
            else if (a == "--beta") beta = need(i, "--beta", 0, 1000);
            else if (a == "--d-cutoff") d_cutoff = need(i, "--d-cutoff", 0.001, 100);
            else if (a == "--extrapolate-ms") extrap_ms = need(i, "--extrapolate-ms", 0, 100);
            else if (a == "--strength") r_strength = need(i, "--strength", 0, 255);
            else if (a == "--pulse-ms") r_pulse = need(i, "--pulse-ms", 10, 500);
            else if (a == "--repeat-hz") r_repeat = need(i, "--repeat-hz", 0, 30);
            else if (a == "--pump-strength") r_pump = need(i, "--pump-strength", 0, 255);
            else if (a == "--dwell") dwell = need(i, "--dwell", 50, 60000);
            else if (a == "--margin") margin = need(i, "--margin", 0, 0.499);
            else if (a == "--invert-x") inv_x = need(i, "--invert-x", 0, 1);
            else if (a == "--invert-y") inv_y = need(i, "--invert-y", 0, 1);
            else if (a == "--scale-x") sc_x = need(i, "--scale-x", 0.05, 20);
            else if (a == "--scale-y") sc_y = need(i, "--scale-y", 0.05, 20);
            else if (a == "--offset-x") off_x = need(i, "--offset-x", -1, 1);
            else if (a == "--offset-y") off_y = need(i, "--offset-y", -1, 1);
            else if (a == "--letterbox") { lb_screen = need(i, "--letterbox", 0.1, 10); lb_content = need(i, "--letterbox", 0.1, 10); }
            else if (a == "--on") filter_on = 1;
            else if (a == "--max-mb") max_mb = need(i, "--max-mb", 1, 1024);
            else if (a == "--close") { if (!action.empty()) { std::fprintf(stderr, "ravengunctl: kies één actie\n"); return kExitUsage; } action = "close"; }
            else if (a == "--start" || a == "--stop" || a == "--status" || a == "--abort" || a == "--check") {
                if (!action.empty()) { std::fprintf(stderr, "ravengunctl: kies één van --start/--stop/--status/--abort/--check\n"); return kExitUsage; }
                action = a.substr(2);
            }
            else if ((a == "--tag" || a == "--holder" || a == "--mark") && i + 1 < argc) {
                std::string v = argv[++i];
                if (a == "--tag") tag = v;
                else if (a == "--holder") holder = v;
                else { mark = v; action = "mark"; }
            }
            else if (a == "--off") filter_on = 0;
            else if (a == "--player") g_player = static_cast<int>(need(i, "--player", 1, 4));
            else if (a == "--route" && i + 1 < argc) io_route = argv[++i];
            else if (a == "--mouse" && i + 1 < argc) { mouse_id = argv[++i]; have_mouse = true; }
            else if (a == "--pad" && i + 1 < argc) { pad_id = argv[++i]; have_pad = true; }
            else if (a == "--all") all_devices = true;
            else if (a == "--counts") mouse_counts = need(i, "--counts", ipc::kMouseCountsMin, ipc::kMouseCountsMax);
            else if (a == "--name" && i + 1 < argc) ctl_name = argv[++i];
            else if (a == "--led" && i + 1 < argc) ctl_led = argv[++i];
            else if (a == "--reset") ctl_reset = true;
            else if (a == "--blink") ctl_blink = true;
            else if (a == "--forget") ctl_forget = true;
            else if (!a.empty() && a[0] == '-' && !(a.size() > 1 && (std::isdigit(static_cast<unsigned char>(a[1])) || a[1] == '.'))) { std::fprintf(stderr, "ravengunctl: onbekende optie '%s'\n\n", a.c_str()); usage(stderr); return kExitUsage; }
            else if (cmd.empty()) cmd = a;
            else if (cmd == "test-pattern" || cmd == "output-button" || cmd == "rumble" || cmd == "key" || cmd == "aagames" || cmd == "iobridge" ||
                     cmd == "game") pos.push_back(a);
            else if (arg2.empty() && (cmd == "calibrate" || cmd == "led" || cmd == "fusion" || cmd == "trim" || cmd == "predict" || cmd == "clock" ||
                                      cmd == "unpair" || cmd == "controller")) arg2 = a;
            else { std::fprintf(stderr, "ravengunctl: onverwacht argument '%s'\n\n", a.c_str()); usage(stderr); return kExitUsage; }
        }
        if (cmd.empty()) { usage(stderr); return kExitUsage; }

        if (cmd == "doctor" || cmd == "devices") {
            Snapshot snap = probe(ProbeOptions{});
            ReportOptions ro;
            ro.full_addresses = full;
            if (cmd == "doctor") {
                auto checks = evaluate(snap);
                int code = exit_code_for(checks);
                std::fputs((json ? doctor_json(checks, code) : doctor_human(checks)).c_str(), stdout);
                return code;
            }
            std::fputs((json ? devices_json(snap, ro) : devices_human(snap, ro)).c_str(), stdout);
            return kExitOk;
        }

        ipc::Client c;
        ipc::Request rq;
        rq.full_ids = full;
        if (cmd == "status") {
            rq.cmd = ipc::Cmd::Status;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"}); res && path(*res, {"sample"})) print_status(*res);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "trim") {
            if (!arg2.empty() && arg2 != "reset") { std::fprintf(stderr, "ravengunctl: trim kent alleen 'reset'\n"); return kExitUsage; }
            rq.cmd = arg2 == "reset" ? ipc::Cmd::ClearTrim : ipc::Cmd::Status;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) { std::printf("%s\n", r.line.c_str()); return kExitOk; }
            if (arg2 == "reset") { std::printf("Yaw-trim gewist (ook in het bewaarde bestand, zodra de bias bewaard wordt).\n"); return kExitOk; }
            const json::Value* yt = path(r.v, {"result", "yaw_trim"});
            if (!yt || !yt->is_object()) return kExitProtocol;
            std::printf("Yaw-trim: %+.3f graden/s (%s; %.0f drukken gezien, %.0f geleerd, %.0f overgeslagen; grens %.1f)\n", num(*yt, {"trim_deg_s"}, 0),
                        boolean(*yt, {"learning"}) ? "leert" : "leren uit", num(*yt, {"presses"}, 0), num(*yt, {"learned"}, 0), num(*yt, {"skipped"}, 0),
                        num(*yt, {"limit_deg_s"}, 0));
            return kExitOk;
        }
        if (cmd == "camera") {
            rq.cmd = ipc::Cmd::Camera;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) print_camera(*res);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "recenter") {
            rq.cmd = ipc::Cmd::Recenter;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else std::printf("Midden vastgelegd op de huidige richting van de Move.\n");
            return kExitOk;
        }
        if (cmd == "predict" || cmd == "clock") {
            const bool pr = cmd == "predict";
            double v = -1;
            if (!arg2.empty()) {
                char* end = nullptr;
                v = std::strtod(arg2.c_str(), &end);
                if (!end || *end || !std::isfinite(v) || v < 0 || v > (pr ? 200 : 1000) || (!pr && v > 0 && v < 20)) {
                    std::fprintf(stderr, pr ? "ravengunctl: predict verwacht een aantal ms tussen 0 (uit) en 200\n"
                                            : "ravengunctl: clock verwacht 0 (uit) of een frequentie tussen 20 en 1000 Hz\n");
                    return kExitUsage;
                }
            }
            if (v < 0 && extrap_ms < 0) {  // zonder waarde: tonen
                rq.cmd = ipc::Cmd::Status;
                Reply r = call(c, rq);
                if (r.code != kExitOk) return r.code;
                if (json) { std::printf("%s\n", r.line.c_str()); return kExitOk; }
                if (pr) std::printf("Voorspelling: %.0f ms (0 = uit)\n", num(r.v, {"result", "config", "aim", "predict_ms"}, 0));
                else std::printf("Uitvoerklok: %.0f Hz (0 = uit), extrapolatie max. %.0f ms\n", num(r.v, {"result", "output", "live", "clock", "hz"}, 0),
                                 num(r.v, {"result", "output", "live", "clock", "extrapolate_ms"}, 0));
                return kExitOk;
            }
            if (pr) { rq.cmd = ipc::Cmd::SetAim; rq.predict_ms = v; }
            else {
                rq.cmd = ipc::Cmd::SetClock;
                if (v >= 0) rq.clock_hz = v;
                if (extrap_ms >= 0) rq.extrapolate_ms = extrap_ms;
            }
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (pr) {
                const double ms = num(r.v, {"result", "predict_ms"}, 0);
                if (ms > 0) std::printf("Voorspelling: %.0f ms vooruit (live toegepast en bewaard in config.json).\n", ms);
                else std::printf("Voorspelling uit (live toegepast en bewaard in config.json).\n");
            } else {
                const double hz = num(r.v, {"result", "clock_hz"}, 0);
                if (hz > 0) std::printf("Uitvoerklok: %.0f Hz, extrapolatie max. %.0f ms (live toegepast en bewaard).\n", hz, num(r.v, {"result", "extrapolate_ms"}, 0));
                else std::printf("Uitvoerklok uit: de positie volgt alleen de rapporten (live toegepast en bewaard).\n");
            }
            return kExitOk;
        }
        if (cmd == "rumble") {
            const bool any = r_strength >= 0 || r_pulse >= 0 || r_repeat >= 0 || r_pump >= 0;
            const std::string sub = pos.empty() ? "" : pos[0];
            if (pos.size() > 2 || (!sub.empty() && sub != "on" && sub != "off" && sub != "test") || (sub == "test" && any) || (sub != "test" && pos.size() > 1)) {
                std::fprintf(stderr, "ravengunctl: rumble verwacht on, off, test [ms] of --strength/--pulse-ms/--repeat-hz/--pump-strength\n");
                return kExitUsage;
            }
            if (sub == "test") {
                if (pos.size() == 2) {
                    char* end = nullptr;
                    double ms = std::strtod(pos[1].c_str(), &end);
                    if (!end || *end || !(ms >= 10 && ms <= 1000)) { std::fprintf(stderr, "ravengunctl: rumble test verwacht 10 tot 1000 ms\n"); return kExitUsage; }
                    rq.rumble_ms = ms;
                }
                rq.cmd = ipc::Cmd::RumbleTest;
                Reply r = call(c, rq);
                if (r.code != kExitOk) return r.code;
                if (json) std::printf("%s\n", r.line.c_str());
                else std::printf("Testpuls van %.0f ms gestuurd (ook ongewapend).\n", num(r.v, {"result", "pulse_ms"}, 0));
                return kExitOk;
            }
            if (sub.empty() && !any && filter_on < 0) {  // tonen
                rq.cmd = ipc::Cmd::Status;
                Reply r = call(c, rq);
                if (r.code != kExitOk) return r.code;
                if (json) { std::printf("%s\n", r.line.c_str()); return kExitOk; }
                const json::Value* rm = path(r.v, {"result", "controller", "rumble"});
                std::printf("Trillen: %s\n", rm && rm->is_object() ? rumble_text(*rm).c_str() : "onbekend");
                return kExitOk;
            }
            rq.cmd = ipc::Cmd::SetRumble;
            if (sub == "on" || filter_on == 1) rq.rumble_enabled = true;
            if (sub == "off" || filter_on == 0) rq.rumble_enabled = false;
            if (r_strength >= 0) rq.rumble_strength = r_strength;
            if (r_pulse >= 0) rq.rumble_pulse_ms = r_pulse;
            if (r_repeat >= 0) rq.rumble_repeat_hz = r_repeat;
            if (r_pump >= 0) rq.rumble_pump_strength = r_pump;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) { std::printf("%s\n", r.line.c_str()); return kExitOk; }
            json::Value rm = r.v.find("result") ? *r.v.find("result") : json::Value();
            std::printf("Trillen: %s (live toegepast%s).\n", rumble_text(rm, false).c_str(), boolean(r.v, {"result", "saved"}) ? " en bewaard" : ", NIET bewaard");
            return kExitOk;
        }
        if (cmd == "filter") {
            if (filter_on < 0 && min_cutoff < 0 && beta < 0 && d_cutoff < 0) { std::fprintf(stderr, "ravengunctl: filter vereist --on, --off, --min-cutoff, --beta of --d-cutoff\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::SetFilter;
            if (filter_on >= 0) rq.filter_enabled = filter_on == 1;
            if (min_cutoff >= 0) rq.min_cutoff_hz = min_cutoff;
            if (beta >= 0) rq.beta = beta;
            if (d_cutoff >= 0) rq.d_cutoff_hz = d_cutoff;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            std::printf("%s\n", json ? r.line.c_str() : "Filterinstelling toegepast (alleen voor deze daemon-run).");
            return kExitOk;
        }
        if (cmd == "watch") {
            rq.cmd = ipc::Cmd::Watch;
            rq.rate_hz = rate > 0 ? rate : (json ? 30 : 10);
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            std::signal(SIGINT, on_signal);
            std::signal(SIGTERM, on_signal);
            double t0 = now_s();
            while (!g_stop.load()) {
                if (duration > 0 && now_s() - t0 >= duration) break;
                auto line = c.read_line(200);
                if (!line) {
                    if (c.error() == "time-out" || c.error() == "onderbroken") continue;
                    std::fprintf(stderr, "ravengunctl: %s\n", c.error().c_str());
                    return kExitDaemonUnavailable;
                }
                if (json) { std::printf("%s\n", line->c_str()); std::fflush(stdout); continue; }
                auto pr = json::parse(*line);
                if (!pr.ok) continue;
                const json::Value* d = path(pr.value, {"data"});
                if (!d) continue;
                const json::Value* s = path(*d, {"sample"});
                if (s) print_sample_line(*s, path(*d, {"edges_since_last_frame"}));
            }
            return kExitOk;
        }
        if (cmd == "arm" || cmd == "disarm" || cmd == "output") {
            rq.cmd = cmd == "arm" ? ipc::Cmd::Arm : cmd == "disarm" ? ipc::Cmd::Disarm : ipc::Cmd::Output;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) print_output(*res, cmd);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "key") {
            if (pos.empty() || pos.size() > 2) { std::fprintf(stderr, "ravengunctl: key vereist een toetsnaam en optioneel down, up of tap\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::OutputKey;
            rq.key_name = pos[0];
            rq.action = pos.size() > 1 ? pos[1] : "tap";
            if (g_player > 0) rq.player = g_player;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else std::printf("Toets %s: %s.\n", rq.key_name.c_str(), rq.action.c_str());
            return kExitOk;
        }
        if (cmd == "aagames") {
            rq.cmd = ipc::Cmd::Aagames;
            rq.action = pos.empty() ? "status" : pos[0];
            if (pos.size() > 1) rq.game = pos[1];
            if (pos.size() > 2 || (rq.action != "start" && rq.action != "stop" && rq.action != "status") || (rq.action == "start" && rq.game.empty())) {
                std::fprintf(stderr, "ravengunctl: aagames start GAME | stop | status\n");
                return kExitUsage;
            }
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) std::printf("%s\n", aagames_text(*res).c_str());
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "game") {
            const std::string act = pos.empty() ? "status" : pos[0];
            const bool with_profile = act == "start" || act == "ready";
            if (pos.size() > 2 || (act != "start" && act != "stop" && act != "status" && act != "ready") || with_profile != (pos.size() == 2)) {
                std::fprintf(stderr, "ravengunctl: game start PROFIEL | ready PROFIEL | stop | status\n");
                return kExitUsage;
            }
            rq.cmd = act == "start" ? ipc::Cmd::GameStart : act == "ready" ? ipc::Cmd::GameReady : act == "stop" ? ipc::Cmd::GameStop : ipc::Cmd::Games;
            if (with_profile) rq.profile = pos[1];
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) { std::printf("%s\n", r.line.c_str()); return kExitOk; }
            if (act == "ready") {
                // De poort vóór de start (docs/prelaunch.md): soort game, wie klaar is, op wie de start zou wachten.
                const json::Value* res = path(r.v, {"result"});
                if (!res || !res->is_object()) return kExitProtocol;
                auto ints = [&](const char* k) {
                    std::string t;
                    if (const json::Value* a = res->find(k); a && a->is_array())
                        for (const auto& x : a->arr) t += (t.empty() ? "" : ",") + std::to_string(static_cast<int>(x.num));
                    return t.empty() ? std::string("-") : t;
                };
                std::printf("%s (%s)\n", boolean(*res, {"lightgun"}) ? "lightgun-game" : "pad-game (geen Move nodig)", str(*res, {"source"}).c_str());
                std::printf("klaar: %s; wacht op speler: %s%s; doet nog niet mee: %s; kalibratie: %s\n", boolean(*res, {"ready"}) ? "ja" : "nee",
                            ints("waiting").c_str(), boolean(*res, {"waiting_any"}) ? " (een willekeurige lightgun)" : "",
                            ints("optional_off").c_str(), ints("calibrate").c_str());
                return kExitOk;
            }
            const json::Value* st = act == "status" ? path(r.v, {"result", "status"}) : path(r.v, {"result"});
            if (!st || !st->is_object()) return kExitProtocol;
            const json::Value* text = st->find("text");
            const json::Value* msg = st->find("message");
            std::printf("%s%s%s\n", text && text->is_string() ? text->str.c_str() : "?", msg && msg->is_string() ? " " : "",
                        msg && msg->is_string() ? msg->str.c_str() : "");
            return kExitOk;
        }
        if (cmd == "iobridge") {
            rq.cmd = ipc::Cmd::Iobridge;
            rq.action = pos.empty() ? "status" : pos[0];
            if (pos.size() > 1) rq.game = pos[1];
            rq.route = io_route;
            if (pos.size() > 2 || (rq.action != "start" && rq.action != "stop" && rq.action != "status" && rq.action != "route") ||
                ((rq.action == "start" || rq.action == "route") && rq.game.empty())) {
                std::fprintf(stderr, "ravengunctl: iobridge start GAME [--route R] | stop | status | route GAME\n");
                return kExitUsage;
            }
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) { std::printf("%s\n", r.line.c_str()); return kExitOk; }
            const json::Value* res = path(r.v, {"result"});
            if (!res) return kExitProtocol;
            if (rq.action == "route") std::printf("%s\n", str(*res, {"route"}).c_str());
            else std::printf("IO-brug: %s\n", iobridge_text(*res).c_str());
            return kExitOk;
        }
        if (cmd == "mice") {
            if (!pos.empty() || !arg2.empty()) { std::fprintf(stderr, "ravengunctl: mice heeft geen argumenten\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::Mice;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) { std::printf("%s\n", r.line.c_str()); return kExitOk; }
            const json::Value* res = path(r.v, {"result"});
            if (!res) return kExitProtocol;
            const json::Value* ms = path(*res, {"mice"});
            std::printf("Muizen:\n");
            if (!ms || !ms->is_array() || ms->arr.empty()) std::printf("  (geen gevonden)\n");
            else for (const auto& m : ms->arr)
                std::printf("  %s  \"%s\" (%s, %s)\n", str(m, {"id"}).c_str(), str(m, {"name"}).c_str(), boolean(m, {"absolute"}) ? "absoluut" : "relatief", str(m, {"event"}).c_str());
            std::printf("Toewijzingen:\n");
            if (const json::Value* as = path(*res, {"assignments"}); as && as->is_array())
                for (const auto& a : as->arr) std::printf("  %s\n", mouse_assignment_text(a).c_str());
            return kExitOk;
        }
        if (cmd == "pads") {
            if (!pos.empty() || !arg2.empty()) { std::fprintf(stderr, "ravengunctl: pads heeft geen argumenten (alleen --all)\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::Pads;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) { std::printf("%s\n", r.line.c_str()); return kExitOk; }
            const json::Value* res = path(r.v, {"result"});
            if (!res) return kExitProtocol;
            std::printf("Gamepads:\n");
            const json::Value* ps = path(*res, {"pads"});
            if (!ps || !ps->is_array() || ps->arr.empty()) std::printf("  (geen gevonden)\n");
            else for (const auto& p : ps->arr) {
                std::string live;
                if (const json::Value* l = p.find("live"); l && l->is_object()) {
                    if (const json::Value* ax = l->find("axes"); ax && ax->is_object())
                        for (const auto& kv : ax->obj) live += " " + kv.first + "=" + fmt(kv.second.num, "%.2f");
                    std::string b;
                    if (const json::Value* bs = l->find("buttons"); bs && bs->is_array())
                        for (const auto& x : bs->arr) b += (b.empty() ? "" : ",") + x.str;
                    live += " knoppen=" + (b.empty() ? std::string("-") : b);
                }
                std::printf("  %s  \"%s\" (%s, %s:%s)%s\n", str(p, {"id"}).c_str(), str(p, {"name"}).c_str(), str(p, {"event"}).c_str(),
                            str(p, {"vendor"}).c_str(), str(p, {"product"}).c_str(), live.c_str());
            }
            std::printf("Toewijzingen:\n");
            if (const json::Value* as = path(*res, {"assignments"}); as && as->is_array())
                for (const auto& a : as->arr) std::printf("  %s\n", pad_assignment_text(a).c_str());
            const json::Value* g = path(*res, {"game"});
            std::printf("Indeling (%s):", g && g->is_string() ? g->str.c_str() : "standaard, geen game gestart");
            if (const json::Value* ms = path(*res, {"mapping"}); ms && ms->is_array())
                for (const auto& m : ms->arr) std::printf(" %s=%s", str(m, {"control"}).c_str(), str(m, {"target"}).c_str());
            std::printf("\n");
            if (const json::Value* ws = path(*res, {"warnings"}); ws && ws->is_array())
                for (const auto& w : ws->arr) std::printf("  let op: %s\n", w.str.c_str());
            if (all_devices) {
                std::printf("Invoerapparaten:\n");
                if (const json::Value* ds = path(*res, {"devices"}); ds && ds->is_array())
                    for (const auto& d : ds->arr)
                        std::printf("  %-9s \"%s\" (%s:%s): %s\n", str(d, {"event"}).empty() ? "-" : str(d, {"event"}).c_str(), str(d, {"name"}).c_str(),
                                    str(d, {"vendor"}).c_str(), str(d, {"product"}).c_str(), str(d, {"why"}).c_str());
            }
            return kExitOk;
        }
        if (cmd == "pad") {
            const bool off = filter_on == 0;
            if (g_player < 1 || (have_pad == off) || !pos.empty() || !arg2.empty()) {
                std::fprintf(stderr, "ravengunctl: pad --player N --pad ID | pad --player N --off\n");
                return kExitUsage;
            }
            rq.cmd = ipc::Cmd::SetPad;
            rq.player = g_player;
            rq.pad = off ? std::string() : pad_id;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) std::printf("%s\n", pad_assignment_text(*res).c_str());
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "mouse-gun") {
            const bool off = filter_on == 0;
            if (g_player < 1 || (have_mouse == off) || !pos.empty() || !arg2.empty() || (off && mouse_counts > 0)) {
                std::fprintf(stderr, "ravengunctl: mouse-gun --player N --mouse ID [--counts C] | mouse-gun --player N --off\n");
                return kExitUsage;
            }
            rq.cmd = ipc::Cmd::SetMouseGun;
            rq.player = g_player;
            rq.mouse = off ? std::string() : mouse_id;
            if (mouse_counts > 0) rq.counts_per_screen = mouse_counts;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) std::printf("%s\n", mouse_assignment_text(*res).c_str());
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "test-pattern") {
            if (pos.empty()) { std::fprintf(stderr, "ravengunctl: test-pattern vereist center, corners, grid, sweep, point X Y of stop\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::TestPattern;
            rq.pattern = pos[0];
            if (pos[0] == "point") {
                char *e1 = nullptr, *e2 = nullptr;
                if (pos.size() != 3) { std::fprintf(stderr, "ravengunctl: test-pattern point vereist X en Y (0..1)\n"); return kExitUsage; }
                double px = std::strtod(pos[1].c_str(), &e1), py = std::strtod(pos[2].c_str(), &e2);
                if (*e1 || *e2 || !(px >= 0 && px <= 1 && py >= 0 && py <= 1)) { std::fprintf(stderr, "ravengunctl: X en Y moeten in [0,1] liggen\n"); return kExitUsage; }
                rq.x = px;
                rq.y = py;
            } else if (pos.size() != 1) { std::fprintf(stderr, "ravengunctl: onverwacht argument bij test-pattern\n"); return kExitUsage; }
            if (dwell > 0) rq.dwell_ms = dwell;
            if (duration > 0) rq.duration_s = duration;
            if (margin >= 0) rq.margin = margin;
            if (rate > 0) rq.rate_hz_pattern = rate;
            if (g_player > 0) rq.player = g_player;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) print_output(*res, cmd);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "output-button") {
            if (pos.empty() || pos.size() > 2) { std::fprintf(stderr, "ravengunctl: output-button vereist een knop en optioneel down, up of tap\n"); return kExitUsage; }
            rq.cmd = ipc::Cmd::OutputButton;
            rq.button = pos[0];
            rq.action = pos.size() > 1 ? pos[1] : "tap";
            if (g_player > 0) rq.player = g_player;
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else std::printf("Knop %s: %s.\n", rq.button.c_str(), rq.action.c_str());
            return kExitOk;
        }
        if (cmd == "set-output") {
            rq.cmd = ipc::Cmd::SetOutput;
            if (inv_x >= 0) rq.invert_x = inv_x > 0.5;
            if (inv_y >= 0) rq.invert_y = inv_y > 0.5;
            if (sc_x > 0) rq.scale_x = sc_x;
            if (sc_y > 0) rq.scale_y = sc_y;
            if (!std::isnan(off_x)) rq.offset_x = off_x;
            if (!std::isnan(off_y)) rq.offset_y = off_y;
            if (lb_screen > 0) { rq.screen_aspect = lb_screen; rq.content_aspect = lb_content; }
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else if (const json::Value* res = path(r.v, {"result"})) print_output(*res, cmd);
            else return kExitProtocol;
            return kExitOk;
        }
        if (cmd == "record") return cmd_record(json, action, tag, mark, duration, max_mb);
        if (cmd == "led") return cmd_led(json, arg2);
        if (cmd == "fusion") {
            core::FusionMode fm;
            if (!core::parse_fusion_mode(arg2.c_str(), fm)) {
                std::fprintf(stderr, "ravengunctl: fusion verwacht mahony, mahony_mag, vqf of vqf_mag\n");
                return kExitUsage;
            }
            rq.cmd = ipc::Cmd::SetFusion;
            rq.mode = core::to_string(fm);
            Reply r = call(c, rq);
            if (r.code != kExitOk) return r.code;
            if (json) std::printf("%s\n", r.line.c_str());
            else std::printf("Oriëntatieschatter: %s%s\n", str(r.v, {"result", "mode"}).c_str(),
                             boolean(r.v, {"result", "saved"}) ? " (bewaard in config.json)" : " (niet bewaard)");
            return kExitOk;
        }
        if (cmd == "controllers") return cmd_controllers(json, full);
        if (cmd == "pair") {
            if (!action.empty() && action != "status" && action != "abort") { std::fprintf(stderr, "ravengunctl: pair kent alleen --status en --abort\n"); return kExitUsage; }
            return cmd_pair(json, action);
        }
        if (cmd == "unpair" || cmd == "controller") {
            if (arg2.empty()) { std::fprintf(stderr, "ravengunctl: %s verwacht een ADRES (of de sleutel uit 'ravengunctl controllers')\n", cmd.c_str()); return kExitUsage; }
            if (cmd == "unpair") return cmd_unpair(json, arg2, ctl_forget);
            // --player is hier het nieuwe spelernummer van deze controller, geen spelerkeuze.
            const int np = g_player;
            g_player = 0;
            return cmd_controller(json, arg2, ctl_name, np, ctl_led, ctl_reset, ctl_blink);
        }
        if (cmd == "calibrate-ui") {
            if (!action.empty() && action != "close" && action != "status") { std::fprintf(stderr, "ravengunctl: calibrate-ui kent alleen --close en --status\n"); return kExitUsage; }
            return cmd_calibrate_ui(json, action.empty() ? "open" : action);
        }
        if (cmd == "calibrate") {
            if (arg2 != "mag") { std::fprintf(stderr, "ravengunctl: alleen 'calibrate mag' bestaat (nog)\n"); return kExitUsage; }
            return cmd_calibrate_mag(json, action, holder, duration);
        }
        std::fprintf(stderr, "ravengunctl: onbekend commando '%s'\n\n", cmd.c_str());
        usage(stderr);
        return kExitUsage;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ravengunctl: interne fout: %s\n", e.what());
        return kExitInternal;
    }
}
