// ravengunctl: tekstopmaak van status, watch en output.
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

using namespace ravengun;

namespace ravengunctl {

// Eén regel over het trillen bij schieten, uit controller.rumble van de status.
std::string rumble_text(const json::Value& rm, bool counters) {
    if (!boolean(rm, {"enabled"})) return "uit";
    std::string rep = num(rm, {"repeat_hz"}, 0) > 0 ? fmt(num(rm, {"repeat_hz"}), "%.0f") + " Hz bij vasthouden" : "één puls per schot";
    return "aan, sterkte " + fmt(num(rm, {"strength"}), "%.0f") + ", puls " + fmt(num(rm, {"pulse_ms"}), "%.0f") + " ms, " + rep + ", pomp " +
           (num(rm, {"pump_strength"}, 0) > 0 ? fmt(num(rm, {"pump_strength"}), "%.0f") : std::string("uit")) +
           (counters ? "; " + fmt(num(rm, {"pulses"}, 0), "%.0f") + " pulsen, " + fmt(num(rm, {"reports"}, 0), "%.0f") + " rapporten" : std::string());
}

// Optische meting als korte tekst (2D-beeldpositie, geen diepte).
static std::string optical_text(const json::Value& s) {
    const json::Value* p = path(s, {"position"});
    if (!p) return "-";
    if (boolean(*p, {"valid"}))
        return "bol op (" + fmt(num(*p, {"x_px"}), "%.1f") + ", " + fmt(num(*p, {"y_px"}), "%.1f") + ") px, genormaliseerd (" +
               fmt(num(*p, {"x_norm"}), "%.3f") + ", " + fmt(num(*p, {"y_norm"}), "%.3f") + "), straal " +
               fmt(num(*p, {"radius_px"}), "%.1f") + " px (2D-beeldpositie, diepte onbekend)";
    return "ongeldig: " + str(*p, {"reason"}) + " (camera " + str(*p, {"camera"}) + ")";
}

// Eén regel over de IO-brug (status.iobridge): route, game, dialect en de laatst geschreven waarden.
std::string iobridge_text(const json::Value& a) {
    if (!boolean(a, {"active"})) return "geen (route evdev: alleen de virtuele apparaten)";
    std::string t = "route " + str(a, {"route"}) + " voor " + str(a, {"game"}) + ", dialect " + str(a, {"dialect"});
    if (const json::Value* d = path(a, {"detail"}); d && d->is_object() && d->find("nodes")) {
        // jvs: de daemon is het JVS-bord; poll- en identificatieteller laten zien of de game praat.
        t += ", lijn " + str(*d, {"path"}) + " (modemstatus " + fmt(num(*d, {"modem_status"}, 0), "%.0f") + "), " +
             fmt(num(*d, {"frames"}, 0), "%.0f") + " packets, " + fmt(num(*d, {"identifies"}, 0), "%.0f") + "x identificatie, " +
             fmt(num(*d, {"switch_polls"}, 0), "%.0f") + " schakelaarpolls, " + fmt(num(*d, {"analog_polls"}, 0), "%.0f") + " analoogpolls";
        if (const json::Value* ns = path(*d, {"nodes"}); ns && ns->is_array())
            for (const auto& n : ns->arr) t += "; node " + str(n, {"board"}) + " adres " + fmt(num(n, {"address"}, 0), "%.0f") + " schakelaars " + str(n, {"switches"}, "");
        if (num(*d, {"since_last_frame_ms"}, -1) >= 0) t += ", laatste packet " + fmt(num(*d, {"since_last_frame_ms"}), "%.0f") + " ms geleden";
        if (!str(*d, {"error"}, "").empty()) t += " (" + str(*d, {"error"}, "") + ")";
    } else if (const json::Value* d2 = path(a, {"detail"}); d2 && d2->is_object() && d2->find("board")) {
        t += ", bord " + str(*d2, {"board"}) + " (" + str(*d2, {"transport"}) + (boolean(*d2, {"created"}) ? ", apparaat aanwezig" : ", GEEN apparaat") + "), " +
             fmt(num(*d2, {"frames_sent"}, 0), "%.0f") + " beelden";
        if (const json::Value* ax = path(*d2, {"axes"}); ax && ax->is_array()) {
            t += ", assen";
            for (const auto& v : ax->arr) t += " " + fmt(v.num, "%.0f");
        }
        t += ", knoppenmasker " + fmt(num(*d2, {"buttons"}, 0), "%.0f");
        if (!str(*d2, {"error"}, "").empty()) t += " (" + str(*d2, {"error"}, "") + ")";
        if (num(a, {"resumes"}, 0) > 0) t += ", " + fmt(num(a, {"resumes"}, 0), "%.0f") + "x hervat na een herstart";
    } else if (const json::Value* d3 = path(a, {"detail"}); d3 && d3->is_object() && d3->find("layout")) {
        t += ", indeling " + str(*d3, {"layout"}) + ", " + fmt(num(*d3, {"writes"}, 0), "%.0f") + " schrijfacties";
        const std::string bytes = str(*d3, {"bytes"}, "");
        if (bytes.size() >= 56) t += ", bytes 8..27: " + bytes.substr(16, 40);
        if (const json::Value* ps = path(*d3, {"players"}); ps && ps->is_array() && !ps->arr.empty()) {
            const json::Value& p = ps->arr[0];
            if (boolean(p, {"valid"})) t += ", P1 aim " + fmt(num(p, {"x"}, 0), "%.3f") + "," + fmt(num(p, {"y"}, 0), "%.3f");
            const std::string b = str(p, {"buttons"}, "");
            if (!b.empty()) t += " knoppen " + b;
        }
        const std::string sysb = str(*d3, {"system"}, "");
        if (!sysb.empty()) t += ", systeem " + sysb;
        if (const json::Value* go = path(a, {"game_out"}); go && go->is_object()) {
            // [Input] Rumble = game: what the daemon reads from the game's own outputs (motor level and solenoid pulses per player).
            t += ", game rumble " + std::string(boolean(*go, {"present"}) ? (boolean(*go, {"stale"}) ? "STALE" : "live") : "no file");
            if (const json::Value* ps = path(*go, {"players"}); ps && ps->is_array())
                for (size_t i = 0; i < ps->arr.size() && i < 2; ++i) {
                    const json::Value& p = ps->arr[i];
                    t += " P" + std::to_string(i + 1) + " motor " + fmt(num(p, {"motor"}, 0), "%.0f") + "/" + fmt(num(p, {"motor_max"}, 0), "%.0f") +
                         " hold " + fmt(num(p, {"hold"}, 0), "%.0f") + " pulses " + fmt(num(p, {"pulses"}, 0), "%.0f") + " changes " +
                         fmt(num(p, {"changes"}, 0), "%.0f");
                }
        }
        if (boolean(a, {"resumes"}) || num(a, {"resumes"}, 0) > 0) t += ", " + fmt(num(a, {"resumes"}, 0), "%.0f") + "x hervat na een herstart";
    }
    return t;
}

// Eén toewijzing uit `mice`/`set_mouse_gun`: "speler 2: <id> verbonden, 1920 counts per schermbreedte".
std::string mouse_assignment_text(const json::Value& a) {
    const json::Value* m = a.find("mouse");
    std::string t = "speler " + fmt(num(a, {"player"}, 0), "%.0f") + ": ";
    if (!m || !m->is_string()) return t + "geen muis";
    t += m->str + (boolean(a, {"connected"}) ? " verbonden" : " NIET verbonden") + ", " + fmt(num(a, {"counts_per_screen"}, 0), "%.0f") + " counts per schermbreedte";
    if (!str(a, {"last_error"}, "").empty()) t += " (" + str(a, {"last_error"}, "") + ")";
    return t;
}

// Eén toewijzing uit `pads`/`set_pad`: "speler 1: <id> verbonden (automatisch)".
std::string pad_assignment_text(const json::Value& a) {
    const json::Value* m = a.find("pad");
    std::string t = "speler " + fmt(num(a, {"player"}, 0), "%.0f") + ": ";
    if (!m || !m->is_string()) return t + "geen gamepad";
    t += m->str + (boolean(a, {"connected"}) ? " verbonden" : " NIET verbonden");
    if (boolean(a, {"auto"})) t += " (automatisch: de enige aangesloten pad)";
    if (!str(a, {"last_error"}, "").empty()) t += " (" + str(a, {"last_error"}, "") + ")";
    return t;
}

// Eén regel over de Unity-route (status van de klant van de BepInEx-plugin).
std::string aagames_text(const json::Value& a) {
    if (!boolean(a, {"active"})) return "uit";
    std::string t = str(a, {"name"}, str(a, {"game"}).c_str()) + ": " + (boolean(a, {"connected"}) ? "VERBONDEN met de game" : "wacht op de game (127.0.0.1:33610)");
    if (boolean(a, {"connected"})) {
        t += ", " + fmt(num(a, {"packets_per_s"}, 0), "%.0f") + " pakketten/s, " + fmt(num(a, {"output_frames"}, 0), "%.0f") + " uitvoerberichten";
        if (const json::Value* ps = path(a, {"players"}); ps && ps->is_array())
            for (const auto& p : ps->arr) {
                const json::Value* pl = p.find("playing");
                if (!pl || !pl->is_bool() || !pl->b) continue;
                t += "; P" + fmt(num(p, {"player"}, 0), "%.0f") + " ammo " + fmt(num(p, {"ammo"}, 0), "%.0f") + " leven " + fmt(num(p, {"life"}, 0), "%.2f") +
                     " schoten " + fmt(num(p, {"recoils"}, 0), "%.0f");
            }
    }
    if (!str(a, {"last_error"}, "").empty()) t += " (" + str(a, {"last_error"}, "") + ")";
    return t;
}

void print_camera(const json::Value& c) {
    std::printf("Toestand:     %s%s%s\n", str(c, {"state"}).c_str(), path(c, {"error"}) && path(c, {"error"})->is_string() ? ": " : "",
                path(c, {"error"}) && path(c, {"error"})->is_string() ? str(c, {"error"}).c_str() : "");
    std::printf("Apparaat:     %s\n              by-path %s -> %s (driver %s, %s)\n", str(c, {"device", "by_id"}).c_str(),
                str(c, {"device", "by_path"}).c_str(), str(c, {"device", "node"}).c_str(), str(c, {"device", "driver"}).c_str(),
                str(c, {"device", "bus_info"}).c_str());
    std::printf("Modus:        %s %.0fx%.0f, gevraagd %.0f fps, driver %s fps; timestamps %s/%s\n", str(c, {"mode", "format"}).c_str(),
                num(c, {"mode", "width"}, 0), num(c, {"mode", "height"}, 0), num(c, {"mode", "requested_fps"}, 0),
                fmt(num(c, {"mode", "driver_fps"}), "%.2f").c_str(), str(c, {"timestamps", "type"}, "-").c_str(),
                str(c, {"timestamps", "source"}, "-").c_str());
    std::printf("Frames:       %.0f ontvangen, gemeten %s fps (totaal) / %s fps (5 s), gemist (sequence) %.0f, fout %.0f, kort %.0f\n",
                num(c, {"frames", "received"}, 0), fmt(num(c, {"frames", "fps_total"}), "%.2f").c_str(),
                fmt(num(c, {"frames", "fps_5s"}), "%.2f").c_str(), num(c, {"frames", "missed_by_sequence"}, 0),
                num(c, {"frames", "error_frames"}, 0), num(c, {"frames", "short_frames"}, 0));
    auto ring = [&](const char* label, const char* k, const char* unit) {
        std::printf("%-14sp50 %s, p95 %s, p99 %s, max %s %s\n", label, fmt(num(c, {k, "p50"}), "%.3f").c_str(),
                    fmt(num(c, {k, "p95"}), "%.3f").c_str(), fmt(num(c, {k, "p99"}), "%.3f").c_str(), fmt(num(c, {k, "max"}), "%.3f").c_str(), unit);
    };
    ring("Interval:", "interval_ms", "ms");
    ring("Leeftijd:", "age_at_processing_ms", "ms (start verwerking - frametimestamp)");
    ring("Verwerking:", "processing_us", "us per frame");
    std::printf("Wachtrij:     capaciteit %.0f, weggegooid vol %.0f / verouderd %.0f\n", num(c, {"queue", "capacity"}, 0),
                num(c, {"queue", "dropped_full"}, 0), num(c, {"queue", "dropped_stale"}, 0));
    std::printf("Controls:    ");
    if (const json::Value* a = path(c, {"controls"}); a && a->is_array())
        for (const auto& e : a->arr)
            std::printf(" %s=%s%s;", str(e, {"name"}).c_str(), fmt(num(e, {"value"}), "%.0f").c_str(),
                        path(e, {"requested"}) && path(e, {"requested"})->is_string() ? "*" : "");
    std::printf("\n              (* = door ravengund gezet)\n");
    std::printf("Tracker:      %.0f verwerkt, %.0f geldig; ROI %.0f, volledig beeld %.0f; straal p50 %s px\n", num(c, {"tracker", "processed"}, 0),
                num(c, {"tracker", "valid"}, 0), num(c, {"tracker", "roi_hits"}, 0), num(c, {"tracker", "full_frame_searches"}, 0),
                fmt(num(c, {"tracker", "radius_px", "p50"}), "%.1f").c_str());
    std::printf("Redenen:     ");
    if (const json::Value* o = path(c, {"tracker", "reasons"}); o && o->is_object())
        for (const auto& kv : o->obj) std::printf(" %s %.0f;", kv.first.c_str(), kv.second.num);
    std::printf("\n");
    std::printf("Laatste:      %s (%s), kandidaten %.0f, componenten %.0f\n", boolean(c, {"tracker", "last", "valid"}) ? "geldig" : "ongeldig",
                str(c, {"tracker", "last", "reason"}).c_str(), num(c, {"tracker", "last", "candidates"}, 0),
                num(c, {"tracker", "last", "components"}, 0));
    if (boolean(c, {"scene", "available"})) {
        auto at = [&](std::initializer_list<const char*> k, size_t i) {
            const json::Value* a = path(c, k);
            return a && a->is_array() && a->arr.size() > i && a->arr[i].is_number() ? a->arr[i].num : NAN;
        };
        std::printf("Scène:        Y gem %s p50 %s p99 %s, verzadigd %s %%, RGB gem %s/%s/%s, kleurmatch %s %%\n",
                    fmt(at({"scene", "mean_yuv"}, 0), "%.0f").c_str(), fmt(num(c, {"scene", "y_p50"}), "%.0f").c_str(),
                    fmt(num(c, {"scene", "y_p99"}), "%.0f").c_str(), fmt(num(c, {"scene", "saturated_frac"}) * 100, "%.2f").c_str(),
                    fmt(at({"scene", "mean_rgb"}, 0), "%.0f").c_str(), fmt(at({"scene", "mean_rgb"}, 1), "%.0f").c_str(),
                    fmt(at({"scene", "mean_rgb"}, 2), "%.0f").c_str(), fmt(num(c, {"scene", "color_match_frac"}) * 100, "%.3f").c_str());
    }
}

void print_sample_line(const json::Value& s, const json::Value* edges) {
    bool av = boolean(s, {"aim", "valid"});
    std::string aim = av ? "x " + fmt(num(s, {"aim", "x"}), "%6.3f") + " y " + fmt(num(s, {"aim", "y"}), "%6.3f") +
                               (boolean(s, {"aim", "off_screen"}) ? " BUITEN" : "       ")
                         : std::string("aim ongeldig            ");
    std::string e;
    if (edges) {
        std::string p = edge_names(path(*edges, {"pressed"})), r = edge_names(path(*edges, {"released"}));
        if (!p.empty()) e += " +" + p;
        if (!r.empty()) e += " -" + r;
    }
    const json::Value* p = path(s, {"position"});
    std::string opt = p && boolean(*p, {"valid"})
                          ? "bol " + fmt(num(*p, {"x_px"}), "%6.1f") + "," + fmt(num(*p, {"y_px"}), "%6.1f") + " r" + fmt(num(*p, {"radius_px"}), "%4.1f")
                          : "bol " + (p ? str(*p, {"reason"}) : std::string("-"));
    std::string xt = ext_text(s);
    std::printf("%-8s %s  yaw %7s pitch %7s  trig %3d  %-24s ext %s  knoppen %s%s\n", str(s, {"link"}).c_str(), aim.c_str(),
                fmt(num(s, {"aim", "yaw_rel_deg"}), "%+.2f").c_str(), fmt(num(s, {"aim", "pitch_rel_deg"}), "%+.2f").c_str(),
                static_cast<int>(num(s, {"trigger"}, 0)), opt.c_str(), xt.c_str(), names(path(s, {"buttons", "down"})).c_str(), e.c_str());
    std::fflush(stdout);
}


void print_status(const json::Value& r) {
    const json::Value& s = *path(r, {"sample"});
    bool con = boolean(r, {"controller", "connected"});
    std::printf("Daemon:       pid %.0f, actief %.0f s%s\n", num(r, {"daemon", "pid"}), num(r, {"daemon", "uptime_s"}),
                boolean(r, {"daemon", "hid_enabled"}) ? "" : " (HID uit: --no-hid)");
    if (con) {
        std::printf("Move:         verbonden (%s), %s, id %s, kalibratie %s\n", str(r, {"controller", "connection"}).c_str(),
                    str(r, {"controller", "model"}).c_str(), str(r, {"controller", "id"}).c_str(),
                    boolean(r, {"controller", "has_calibration"}) ? "ja" : "NEE");
    } else {
        std::printf("Move:         niet verbonden (druk op de PS-knop)\n");
    }
    std::printf("Batterij:     %s\n", str(r, {"controller", "battery", "text"}).c_str());
    std::printf("Verbinding:   %s\n", str(s, {"link"}).c_str());
    std::printf("Rapporten:    %.0f totaal, %s/s (5 s), %s/s sinds verbinden; 2 IMU-subsamples per rapport\n",
                num(r, {"reports", "total"}), fmt(num(r, {"reports", "rate_hz_5s"}), "%.1f").c_str(),
                fmt(num(r, {"reports", "rate_hz_since_connect"}), "%.1f").c_str());
    std::printf("Intervallen:  p50 %s ms, p95 %s ms, p99 %s ms, max %s ms; gemist volgens seq %.0f\n",
                fmt(num(r, {"reports", "interval_ms", "p50"}), "%.1f").c_str(),
                fmt(num(r, {"reports", "interval_ms", "p95"}), "%.1f").c_str(),
                fmt(num(r, {"reports", "interval_ms", "p99"}), "%.1f").c_str(),
                fmt(num(r, {"reports", "interval_ms", "max"}), "%.1f").c_str(), num(r, {"reports", "dropped_by_seq"}));
    std::printf("Gaten:       ");
    if (const json::Value* h = path(r, {"reports", "interval_histogram"}); h && h->is_array()) {
        for (const auto& b : h->arr) {
            double c = num(b, {"count"}, 0);
            if (c <= 0) continue;
            double to = num(b, {"to_ms"});
            if (std::isnan(to)) std::printf(" >=%.0f ms: %.0f;", num(b, {"from_ms"}), c);
            else std::printf(" %.0f-%.0f ms: %.0f;", num(b, {"from_ms"}), to, c);
        }
    }
    std::printf("\n");
    std::printf("Verwerking:   p95 %s us per rapport\n", fmt(num(r, {"processing_us", "p95"}), "%.0f").c_str());
    std::printf("Oriëntatie:   %s, bias %s%s, yaw-referentie %s, wereld-yaw %s graden, pitch %s graden\n",
                boolean(s, {"orientation", "valid"}) ? "geldig" : "ONGELDIG", str(s, {"orientation", "bias_state"}).c_str(),
                boolean(s, {"orientation", "still"}) ? " (stil)" : "", str(s, {"orientation", "yaw_reference"}).c_str(),
                fmt(num(s, {"orientation", "world_yaw_deg"}), "%.2f").c_str(),
                fmt(num(s, {"orientation", "world_pitch_deg"}), "%.2f").c_str());
    {
        const json::Value* gb = path(s, {"orientation", "gyro_bias_deg_s"});
        std::string vals = "-";
        if (gb && gb->is_array() && gb->arr.size() == 3)
            vals = fmt(gb->arr[0].num, "%+.2f") + " " + fmt(gb->arr[1].num, "%+.2f") + " " + fmt(gb->arr[2].num, "%+.2f");
        std::printf("Gyro-bias:    %s (bron %s), %s graden/s (x y z); stilstand %s s, quasi-stil %s s", str(s, {"orientation", "bias_state"}).c_str(),
                    str(s, {"orientation", "bias_source"}, "-").c_str(), vals.c_str(), fmt(num(r, {"gyro_bias", "still_s"}), "%.0f").c_str(),
                    fmt(num(r, {"gyro_bias", "quasi_s"}), "%.0f").c_str());
        if (const json::Value* sl = path(r, {"gyro_bias", "temp_slope_deg_s_per_count"}); sl && sl->is_array() && sl->arr.size() == 3 &&
            (sl->arr[0].num != 0 || sl->arr[1].num != 0 || sl->arr[2].num != 0))
            std::printf("; temperatuurhelling %s %s %s graden/s per telling", fmt(sl->arr[0].num, "%+.4f").c_str(), fmt(sl->arr[1].num, "%+.4f").c_str(),
                        fmt(sl->arr[2].num, "%+.4f").c_str());
        if (num(r, {"gyro_bias", "optical_s"}, 0) > 0 || num(r, {"gyro_bias", "optical_still_windows"}, 0) > 0)
            std::printf("; optisch-stil %.0f s (%.0f vensters gebruikt, %.0f afgewezen)", num(r, {"gyro_bias", "optical_s"}, 0),
                        num(r, {"gyro_bias", "optical_windows_used"}, 0), num(r, {"gyro_bias", "optical_windows_rejected"}, 0));
        std::printf("; modus %s\n", str(r, {"gyro_bias", "mode"}, "-").c_str());
        if (const json::Value* yt = path(r, {"yaw_trim"}); yt && yt->is_object())
            std::printf("Yaw-trim:     %+.3f graden/s (%s; %.0f drukken gezien, %.0f geleerd, %.0f overgeslagen)\n", num(*yt, {"trim_deg_s"}, 0),
                        boolean(*yt, {"learning"}) ? "leert" : "leren uit", num(*yt, {"presses"}, 0), num(*yt, {"learned"}, 0), num(*yt, {"skipped"}, 0));
        if (const json::Value* st = path(r, {"gyro_bias", "store"}); st && boolean(*st, {"enabled"})) {
            std::string ld = boolean(*st, {"loaded"}) ? "hersteld bij start (tc " + fmt(num(*st, {"loaded_tc"}), "%.0f") + ")" : "niets hersteld";
            std::printf("              bewaard: %s; %.0f keer bewaard in deze run%s\n", ld.c_str(), num(*st, {"saves"}, 0),
                        str(*st, {"error"}, "").empty() ? "" : (", FOUT: " + str(*st, {"error"}, "")).c_str());
        } else if (st) {
            std::printf("              bewaren uit (--no-bias-store)\n");
        }
    }
    std::printf("Positie:      %s\n", optical_text(s).c_str());
    if (const json::Value* cam = path(r, {"camera"}); cam && cam->is_object())
        std::printf("Camera:       %s, %s/s (5 s), gemist %.0f, bol geldig %.0f van %.0f frames\n", str(*cam, {"state"}).c_str(),
                    fmt(num(*cam, {"frames", "fps_5s"}), "%.2f").c_str(), num(*cam, {"frames", "missed_by_sequence"}, 0),
                    num(*cam, {"tracker", "valid"}, 0), num(*cam, {"tracker", "processed"}, 0));
    if (const json::Value* ib = path(r, {"iobridge"}); ib && ib->is_object())
        std::printf("IO-brug:      %s\n", iobridge_text(*ib).c_str());
    if (const json::Value* aa = path(r, {"aagames"}); aa && aa->is_object() && boolean(*aa, {"active"}))
        std::printf("Unity-game:   %s\n", aagames_text(*aa).c_str());
    if (boolean(s, {"aim", "valid"})) {
        std::printf("Aim:          x %.3f y %.3f (ongefilterd %.3f %.3f)%s, yaw %+.2f pitch %+.2f graden, midden: %s\n",
                    num(s, {"aim", "x"}), num(s, {"aim", "y"}), num(s, {"aim", "x_raw"}), num(s, {"aim", "y_raw"}),
                    boolean(s, {"aim", "off_screen"}) ? " BUITEN SCHERM" : "", num(s, {"aim", "yaw_rel_deg"}),
                    num(s, {"aim", "pitch_rel_deg"}), str(s, {"aim", "center_source"}).c_str());
    } else {
        std::printf("Aim:          ONGELDIG (midden: %s)\n", str(s, {"aim", "center_source"}).c_str());
    }
    std::printf("Aim-mapping:  oriëntatiemodus (voorlopig), rand bij %.1f graden horizontaal / %.2f verticaal; filter %s\n",
                num(r, {"config", "aim", "h_edge_deg"}), num(r, {"config", "aim", "v_edge_deg"}),
                boolean(r, {"config", "filter", "enabled"})
                    ? ("1€ (min_cutoff " + fmt(num(r, {"config", "filter", "min_cutoff_hz"}), "%.2f") + " Hz, beta " +
                       fmt(num(r, {"config", "filter", "beta"}), "%.2f") + ")").c_str()
                    : "uit");
    std::printf("Voorspelling: %s; huidige draaisnelheid %.0f graden/s, gewicht %.2f, voorspelde hoek %.2f graden, rapportleeftijd %.1f ms\n",
                num(r, {"config", "aim", "predict_ms"}, 0) > 0 ? (fmt(num(r, {"config", "aim", "predict_ms"}), "%.0f") + " ms vooruit").c_str() : "uit",
                num(r, {"config", "predict", "rate_dps"}, 0), num(r, {"config", "predict", "gain"}, 0), num(r, {"config", "predict", "angle_deg"}, 0),
                num(r, {"config", "predict", "report_age_ms"}, 0));
    if (const json::Value* rm = path(r, {"controller", "rumble"}); rm && rm->is_object())
        std::printf("Trillen:      %s\n", rumble_text(*rm).c_str());
    if (const json::Value* ck = path(r, {"output", "live", "clock"}))
        std::printf("Uitvoerklok:  %s; extrapolatie max. %.0f ms; %.0f tikken, %.0f geëxtrapoleerd, %.0f vastgehouden, %.0f positie-events\n",
                    boolean(*ck, {"enabled"}) ? (fmt(num(*ck, {"hz"}), "%.0f") + " Hz").c_str() : "uit (positie per rapport)", num(*ck, {"extrapolate_ms"}, 0),
                    num(*ck, {"ticks"}, 0), num(*ck, {"extrapolated"}, 0), num(*ck, {"held"}, 0), num(*ck, {"events"}, 0));
    std::printf("Knoppen:      %s, trigger %.0f\n", names(path(s, {"buttons", "down"})).c_str(), num(s, {"trigger"}, 0));
    std::printf("Uitbreiding:  ext verbonden: %s\n", ext_text(s).c_str());
    if (const json::Value* tb = path(r, {"timebase"})) {
        std::printf("Tijdbasis:    %s; %.0f stempels, %s, skew %s ppm, restvertraging p50 %s / p95 %s / max %s ms; temperatuur %s (ruw %.0f)\n",
                    str(*tb, {"integration_dt"}).c_str(), num(*tb, {"stamps"}, 0), boolean(*tb, {"coupled"}) ? "gekoppeld" : "nog niet gekoppeld",
                    fmt(num(*tb, {"skew_ppm"}), "%.1f").c_str(), fmt(num(*tb, {"residual_ms", "p50"}), "%.2f").c_str(),
                    fmt(num(*tb, {"residual_ms", "p95"}), "%.2f").c_str(), fmt(num(*tb, {"residual_ms", "max"}), "%.2f").c_str(),
                    fmt(num(*tb, {"temperature_c"}), "%.1f C").c_str(), num(*tb, {"temperature_raw"}, -1));
    }
    if (const json::Value* h = path(r, {"hidraw"}))
        std::printf("hidraw-tap:   %s %s, gekoppeld %.0f, niet gekoppeld %.0f, weggegooid %.0f\n", boolean(*h, {"open"}) ? "open" : "dicht",
                    str(*h, {"node"}, "").c_str(), num(*h, {"matched"}, 0), num(*h, {"unmatched"}, 0), num(*h, {"discarded"}, 0));
    if (const json::Value* m = path(r, {"magnetometer"})) {
        if (boolean(*m, {"calibrated"}))
            std::printf("Magnetometer: gekalibreerd (%s, houder %s), |m| %.3f, dip %s graden (ref %.2f), toestand %s%s%s; %s\n",
                        str(*m, {"calibration", "kind"}).c_str(), str(*m, {"calibration", "holder"}).c_str(), num(*m, {"live", "norm"}, 0),
                        fmt(num(*m, {"live", "dip_deg"}), "%.2f").c_str(), num(*m, {"calibration", "dip_ref_deg"}, 0),
                        str(*m, {"live", "state"}).c_str(), str(*m, {"live", "reasons"}, "").empty() ? "" : ", afgewezen: ",
                        str(*m, {"live", "reasons"}, "").c_str(), boolean(*m, {"used_in_fusion"}) ? "in de fusie" : "niet in de fusie");
        else
            std::printf("Magnetometer: niet gekalibreerd (%s)\n", str(*m, {"calibration_error"}).c_str());
        if (const json::Value* y = path(*m, {"yaw_correction"}))
            std::printf("Yaw-correctie: %s, %s; openstaand %+.2f graden (σ %.2f), drift %+.3f graden/s, verticale bias %+.3f graden/s, "
                        "correctie nu %+.3f graden/s (grens %.3f), totaal %+.2f graden\n",
                        str(*y, {"mode"}).c_str(), boolean(*y, {"applied"}) ? (boolean(*y, {"locked"}) ? "actief" : "wacht op vergrendeling") : "schaduw (niet toegepast)",
                        num(*y, {"pending_deg"}, 0), num(*y, {"sigma_deg"}, 0), num(*y, {"drift_deg_s"}, 0), num(*y, {"vertical_bias_deg_s"}, 0),
                        num(*y, {"rate_deg_s"}, 0), num(*y, {"rate_limit_deg_s"}, 0), num(*y, {"total_deg"}, 0));
        if (const json::Value* c = path(*m, {"session", "mag_calibration"}); c && boolean(*c, {"active"}))
            std::printf("              kalibratie loopt: dekking %.0f %%, %.0f samples\n", num(*c, {"coverage_pct"}, 0), num(*c, {"samples"}, 0));
    }
    if (const json::Value* rc = path(r, {"recording"}); rc && rc->is_object()) print_record_status(*rc);
}

void print_output(const json::Value& o, const std::string& what) {
    if (what == "arm") std::printf("Uitvoer GEWAPEND.\n");
    else if (what == "disarm") std::printf("Uitvoer ontwapend; alle knoppen vrijgegeven.\n");
    std::printf("Uitvoer:      %s; apparaat %s%s (%s:%s), event %s, abs %.0f..%.0f\n", boolean(o, {"armed"}) ? "GEWAPEND" : "ontwapend",
                boolean(o, {"device", "created"}) ? "aangemaakt" : "niet aangemaakt", "", str(o, {"device", "vendor"}).c_str(),
                str(o, {"device", "product"}).c_str(), str(o, {"device", "event_node"}, "-").c_str(), num(o, {"device", "abs_min"}, 0),
                num(o, {"device", "abs_max"}, 0));
    std::printf("Positie:      aim %s,%s -> abs %s,%s; knoppen: %s\n", fmt(num(o, {"position", "nx"}), "%.3f").c_str(),
                fmt(num(o, {"position", "ny"}), "%.3f").c_str(), fmt(num(o, {"position", "abs_x"}), "%.0f").c_str(),
                fmt(num(o, {"position", "abs_y"}), "%.0f").c_str(), names(path(o, {"buttons_down"})).c_str());
    std::printf("Toetsenbord:  %s, toetsen: %s\n", boolean(o, {"keyboard", "created"}) ? "aangemaakt" : "niet aangemaakt",
                names(path(o, {"keys_down"})).c_str());
    if (const json::Value* m = path(o, {"map"}); m && m->is_object() && !m->obj.empty()) {
        std::string line;
        for (const auto& kv : m->obj) line += (line.empty() ? "" : ", ") + kv.first + "=" + (kv.second.is_string() ? kv.second.str : "?");
        std::printf("Indeling:     %s\n", line.c_str());
    }
    std::printf("Transformatie: invert %d/%d, schaal %.3f/%.3f, offset %.3f/%.3f, viewport (%.3f,%.3f)-(%.3f,%.3f)\n",
                boolean(o, {"transform", "invert_x"}), boolean(o, {"transform", "invert_y"}), num(o, {"transform", "scale_x"}),
                num(o, {"transform", "scale_y"}), num(o, {"transform", "offset_x"}), num(o, {"transform", "offset_y"}),
                num(o, {"transform", "viewport", "x0"}), num(o, {"transform", "viewport", "y0"}), num(o, {"transform", "viewport", "x1"}),
                num(o, {"transform", "viewport", "y1"}));
    std::printf("Patroon:      %s", str(o, {"pattern", "state"}).c_str());
    if (str(o, {"pattern", "state"}) != "none")
        std::printf(" (%s, stap %.0f/%.0f, %.1f s)", str(o, {"pattern", "kind"}).c_str(), num(o, {"pattern", "step"}, 0),
                    num(o, {"pattern", "steps"}, 0), num(o, {"pattern", "length_s"}, 0));
    std::printf("\nTellers:      %.0f positie-events, %.0f knop-events, %.0f geweigerd, %.0f schrijffouten, %.0f vrijgaves\n",
                num(o, {"stats", "position_events"}, 0), num(o, {"stats", "button_events"}, 0), num(o, {"stats", "rejected"}, 0),
                num(o, {"stats", "write_errors"}, 0), num(o, {"stats", "releases"}, 0));
}

}  // namespace ravengunctl
