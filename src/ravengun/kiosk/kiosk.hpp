#pragma once
// Kiosk-browser op de tv voor de fullscreen kalibratie-UI (docs/calibration-ui.md).
// Start Brave (flatpak com.brave.Browser) fullscreen met een eigen, apart profiel onder
// ~/.local/state/ravengun/kiosk. Raakt nooit de profielen van de gebruiker: sluiten gebeurt alleen op
// processen waarvan de opdrachtregel --user-data-dir=<dit profiel> bevat.
#include <string>
#include <vector>

namespace ravengun::kiosk {

inline constexpr const char* kBrowserAppId = "com.brave.Browser";

// $XDG_STATE_HOME of ~/.local/state, plus /ravengun/kiosk. Leeg als HOME ontbreekt.
std::string profile_dir();

// Argumenten van `flatpak run` (zonder het programma zelf): rechten voor het profiel, app-id, vlaggen, URL.
std::vector<std::string> flatpak_args(const std::string& url, const std::string& profile);

// Waarde van --xwayland-xauthority uit een NUL-gescheiden opdrachtregel (kwin_wayland), of leeg.
std::string xauthority_from_cmdline(const std::string& nul_separated);

// Behoort deze (NUL-gescheiden) opdrachtregel tot de kiosk met dit profiel?
bool cmdline_is_kiosk(const std::string& nul_separated, const std::string& profile);

struct Result {
    bool ok = false;
    bool running = false;  // er draait minstens één kioskproces (na de actie)
    int processes = 0;
    std::string message;   // Nederlands
};

Result status();
// Opent http://127.0.0.1:<port>/calibrate fullscreen op de tv. Al open: ok, "al geopend".
Result open(int port);
// SIGTERM naar de kioskprocessen, na 4 s SIGKILL voor wat overblijft.
Result close();

}  // namespace ravengun::kiosk
