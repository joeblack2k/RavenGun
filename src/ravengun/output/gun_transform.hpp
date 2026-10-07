#pragma once
// Coördinaattransformatie van de aim (genormaliseerd scherm) naar de absolute assen van het
// virtuele lightgun-apparaat. Dit is de ENIGE plek waar inversie, aspect/letterboxing, schaal en
// clamp plaatsvinden (agentprompt sectie 14: ieder precies één keer). Alles stroomopwaarts
// (aim, filter) blijft ongeclampt en in genormaliseerde schermcoördinaten; alles stroomafwaarts
// (uinput, TeknoParrot, game) krijgt alleen het resultaat van deze functie.
//
// Ruimtes:
//   aim-ruimte   (nx, ny) in [0,1], linksboven = (0,0), y omlaag. Dit is het volledige fysieke scherm.
//   viewport     het deel van de aim-ruimte waarin de game daadwerkelijk tekent (letterbox/pillarbox).
//   apparaat     gehele ABS_X/ABS_Y in [0, abs_max]; dit bereik wordt door TeknoParrot lineair op
//                0..1 van het gamevenster gelegd (EvdevMouseListener.DeviceLoop).
#include <optional>

namespace ravengun::output {

// Absolute bereik van het virtuele apparaat: 0..kAbsMax op beide assen (gedocumenteerd, vast).
inline constexpr int kAbsMin = 0;
inline constexpr int kAbsMax = 32767;

// Rechthoek in aim-ruimte waar de game tekent. Standaard het hele scherm.
struct Viewport {
    double x0 = 0, y0 = 0, x1 = 1, y1 = 1;
    bool valid() const;  // eindig, x1 > x0, y1 > y0, binnen [0,1]
};

// Centreert content met aspect `content_aspect` (breedte/hoogte) in een scherm met aspect
// `screen_aspect`: pillarbox als content smaller is, letterbox als breder. Geeft het volledige
// scherm terug bij gelijke aspecten of ongeldige invoer (<= 0 of niet eindig).
Viewport letterbox_viewport(double screen_aspect, double content_aspect);

struct GunTransform {
    Viewport viewport;                   // letterbox: aim-ruimte -> viewport-ruimte
    bool invert_x = false;               // spiegel de viewport-ruimte (u -> 1-u)
    bool invert_y = false;
    double scale_x = 1, scale_y = 1;     // versterking rond het midden (kalibratie), > 0
    double offset_x = 0, offset_y = 0;   // verschuiving in viewport-eenheden, na schaal
    int abs_max = kAbsMax;
};

struct AbsPoint {
    int x = 0, y = 0;
    bool clamped_x = false, clamped_y = false;  // invoer lag buiten de viewport (of werd erbuiten geschaald)
};

// Volgorde: viewport-normalisatie, inversie, schaal rond 0,5 plus offset, clamp naar [0,1],
// afronden op abs_max. nullopt bij niet-eindige invoer of een ongeldige transformatie
// (er wordt dan niets uitgestuurd).
std::optional<AbsPoint> to_abs(const GunTransform& t, double nx, double ny);

// Afzonderlijke stap, voor tests en diagnose: waarde (nog ongeclampt) in viewport-ruimte.
double to_viewport_unit(double n, double v0, double v1);

}  // namespace ravengun::output
