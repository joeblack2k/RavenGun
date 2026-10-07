// ravengunctl: alleen-lezen diagnose van de RavenGun-hardware.
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "ravengun/doctor.hpp"
#include "ravengun/probe.hpp"
#include "ravengun/report.hpp"

namespace {

void usage(FILE* f) {
    std::fputs(
        "gebruik: ravengunctl <commando> [opties]\n"
        "\n"
        "commando's:\n"
        "  doctor    controleert camera, Move, Bluetooth, uinput en PSMoveAPI en geeft per punt een herstelactie\n"
        "  devices   toont de gevonden camera's, Sony HID-apparaten, Bluetooth-adapters en uinput\n"
        "\n"
        "opties:\n"
        "  --json             machineleesbare uitvoer (JSON op stdout)\n"
        "  --full-addresses   toon volledige Bluetooth-adressen (standaard geredigeerd)\n"
        "  -h, --help         deze tekst\n"
        "\n"
        "exitcodes:\n"
        "  0   doctor: alles in orde; devices: gelukt\n"
        "  1   doctor: minstens een waarschuwing, geen fouten\n"
        "  2   doctor: minstens een fout\n"
        "  64  verkeerd gebruik (onbekend commando of optie)\n"
        "  70  interne fout\n"
        "\n"
        "Het programma wijzigt niets op het systeem.\n", f);
}

}  // namespace

int main(int argc, char** argv) {
    using namespace ravengun;
    try {
        std::string cmd;
        bool json = false, full = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-h" || a == "--help") { usage(stdout); return kExitOk; }
            else if (a == "--json") json = true;
            else if (a == "--full-addresses") full = true;
            else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "ravengunctl: onbekende optie '%s'\n\n", a.c_str()); usage(stderr); return kExitUsage; }
            else if (cmd.empty()) cmd = a;
            else { std::fprintf(stderr, "ravengunctl: onverwacht argument '%s'\n\n", a.c_str()); usage(stderr); return kExitUsage; }
        }
        if (cmd.empty()) { usage(stderr); return kExitUsage; }
        if (cmd != "doctor" && cmd != "devices") {
            std::fprintf(stderr, "ravengunctl: onbekend commando '%s'\n\n", cmd.c_str());
            usage(stderr);
            return kExitUsage;
        }

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
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ravengunctl: interne fout: %s\n", e.what());
        return kExitInternal;
    }
}
