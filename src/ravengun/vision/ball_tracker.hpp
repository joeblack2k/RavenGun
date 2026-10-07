#pragma once
// Boltracker voor de gekleurde LED-bol van de PlayStation Move. Eigen, lichte beeldverwerking
// (geen OpenCV). Werkt op YUYV-frames (zoals de PS3 Eye/ov534 die levert).
//
// Stappen per frame:
//  1. Masker: een pixel telt mee als zijn chroma (U,V) in de tint-sector van de LED-kleur valt,
//     met voldoende chroma-sterkte en helderheid ("kleurpixel"), of als hij (bijna) verzadigd is
//     ("verzadigde pixel": de overbelichte kern van de bol is wit, zonder kleur).
//  2. Connected components (8-buren) binnen een ROI rond de vorige positie, met terugval naar het
//     volledige beeld als daar niets bruikbaars staat.
//  3. Per component: oppervlak, aandeel kleurpixels, zwaartepunt en tweede momenten. Daaruit
//     volgen straal (gelijk oppervlak), langgerektheid (wortel van de eigenwaardenverhouding) en
//     vulling (oppervlak t.o.v. de ellips met dezelfde momenten). Criteria verwerpen te kleine,
//     te grote, niet-ronde en te weinig gekleurde componenten. Een witte lamp of raam (verzadigd
//     zonder kleur) valt zo af.
//  4. Keuze: tijdens volgen de geschikte kandidaat het dichtst bij de voorspelde positie binnen
//     een poort. Zonder voorgeschiedenis wordt NIET de grootste gekozen: is er meer dan één
//     geschikte kandidaat, dan is de meting ongeldig ("meerdere kandidaten").
//  5. Uitbijters: een sprong buiten de poort of een plotselinge straalverandering wordt pas
//     geaccepteerd als het volgende frame hem bevestigt. Een nieuwe acquisitie (na verlies) vraagt
//     ook bevestiging in een tweede frame.
//
// Uitvoer: 2D-beeldpositie in px en genormaliseerd, straal in px, geldigheid en een reden bij
// ongeldigheid. Geen confidence-percentage en geen diepte (geen gekalibreerd cameramodel).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ravengun::vision {

enum class PixelFormat { YUYV };

struct ImageView {
    const uint8_t* data = nullptr;
    int width = 0, height = 0;
    int stride = 0;  // bytes per regel
    PixelFormat format = PixelFormat::YUYV;
};

// Kleur van de LED (wat naar de Move gaat, 0..255 per kanaal).
struct Rgb { uint8_t r = 0, g = 0, b = 0; };

// Tint (graden, atan2(V-128, U-128)) van een RGB-kleur in BT.601-YUV. Dat is de nominale tint;
// de werkelijke tint in het camerabeeld wijkt af door sensor en witbalans en wordt gemeten.
double nominal_hue_deg(Rgb c);

// Benoemde LED-kleuren (Nederlandse en Engelse namen). false bij een onbekende naam.
bool led_color_by_name(const std::string& name, Rgb& out);

// Gemeten tint van een benoemde LED-kleur in het beeld van de PS3 Eye van deze opstelling
// (YUYV, AWB uit, belichting 30-50, 7 okt 2026). NaN als er geen meting is; dan geldt
// nominal_hue_deg(). Magenta wijkt sterk af van nominaal (86 i.p.v. 61 graden).
double measured_hue_deg(const std::string& name);

struct TrackerConfig {
    double hue_deg = -75;          // midden van de tint-sector (camera-UV); -75 = cyaan, gemeten
    double hue_tol_deg = 30;       // halve breedte van de sector
    int min_chroma = 28;           // minimale |(U-128, V-128)| van een kleurpixel
    int min_luma = 40;             // minimale Y van een kleurpixel
    int sat_luma = 245;            // Y vanaf hier: verzadigd (telt als bol-kern, zonder kleur)
    int min_area_px = 12;          // kleiner: "te klein" (bij een kandidaat) of ruis (< noise_area_px)
    int noise_area_px = 4;         // componenten kleiner dan dit worden genegeerd
    double max_radius_frac = 0.25; // straal groter dan dit deel van de beeldhoogte: "te groot"
    double min_color_frac = 0.25;  // minimaal aandeel kleurpixels in een component
    double max_elongation = 1.8;   // wortel(lambda1/lambda2); half afgedekte bol ~1,9
    double min_fill = 0.70;        // oppervlak / (pi * 4 * wortel(lambda1*lambda2)); schijf = 1
    double max_fill = 1.35;
    double gate_px_min = 24;       // minimale poortstraal (px, bij 640 breed; schaalt met breedte)
    double gate_radius_factor = 4; // poortstraal = max(gate_px_min, factor * straal) + |snelheid|
    double max_radius_ratio = 1.8; // straalverandering per frame boven deze factor: bevestiging nodig
    int roi_margin_factor = 5;     // ROI = vorige positie +- max(roi_min, factor*straal + |v|)
    int roi_min_px = 48;           // bij 640 breed; schaalt met breedte
    int lost_after_frames = 8;     // zoveel frames op rij niets geaccepteerd: verloren, ROI weg
    int confirm_frames = 2;        // frames nodig voor een (her)acquisitie of bevestigde sprong
};

enum class Reason {
    None,               // geldig
    NoCandidate,        // geen component met de bolkleur
    TooSmall,           // wel een kandidaat, maar kleiner dan min_area_px
    TooLarge,
    NotRound,           // vorm (langgerektheid/vulling) klopt niet
    TooLittleColor,     // vooral verzadigd/wit, te weinig kleurpixels
    MultipleCandidates, // meer dan één geschikte kandidaat zonder voorgeschiedenis
    JumpUnconfirmed,    // sprong buiten de poort, wacht op bevestiging
    AcquirePending,     // nieuwe acquisitie, wacht op bevestiging
    BadFrame,           // frame onbruikbaar (fout, te kort)
};
const char* to_string(Reason r);
inline constexpr int kReasonCount = 10;

enum class TrackState { Searching, Tracking };
const char* to_string(TrackState s);

struct Blob {
    int area = 0, color_px = 0, sat_px = 0;
    double cx = 0, cy = 0;          // zwaartepunt (px, pixelcentra op +0,5)
    double radius = 0;              // wortel(oppervlak/pi)
    double elongation = 0, fill = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // omhullende (inclusief)
    double mean_luma = 0;
    double mean_hue_deg = 0, mean_chroma = 0;  // uit de gemiddelde (U,V) van de kleurpixels
    Reason verdict = Reason::None;  // None = geschikte kandidaat
};

struct TrackResult {
    bool valid = false;
    Reason reason = Reason::NoCandidate;
    TrackState state = TrackState::Searching;
    double x_px = 0, y_px = 0;       // zwaartepunt in px (0..breedte), alleen geldig bij valid
    double x_norm = 0, y_norm = 0;   // x/breedte, y/hoogte (0..1, links-boven = 0,0)
    double radius_px = 0;
    double elongation = 0, fill = 0;
    int area_px = 0, color_px = 0, sat_px = 0;
    double mean_luma = 0, mean_hue_deg = 0, mean_chroma = 0;
    int candidates = 0;              // geschikte kandidaten (na vormcriteria) in het doorzochte gebied
    int components = 0;              // alle componenten boven de ruisgrens
    bool used_roi = false;           // gevonden binnen de ROI (zonder volledig beeld)
    bool full_frame_searched = false;
    int width = 0, height = 0;
};

// Scènestatistiek (alleen getallen, subsampled), o.a. om een LED-kleur te kiezen en om te melden
// wat de camera ziet als de bol niet gevonden wordt.
struct SceneStats {
    int samples = 0;
    double mean_y = 0, mean_u = 0, mean_v = 0;
    double p50_y = 0, p99_y = 0;
    double sat_frac = 0;            // aandeel Y >= sat_luma
    double color_match_frac = 0;    // aandeel kleurpixels (zelfde criteria als de tracker)
    // Histogram van de tint van chromatische pixels (chroma >= min_chroma, Y >= min_luma),
    // 12 sectoren van 30 graden, sector i = [i*30-180, i*30-150).
    std::array<double, 12> hue_hist_frac{};
    double chromatic_frac = 0;      // aandeel pixels met chroma >= min_chroma en Y >= min_luma
    // Gemiddelde R, G, B (BT.601 terugrekening uit YUV).
    double mean_r = 0, mean_g = 0, mean_b = 0;
};

class BallTracker {
public:
    explicit BallTracker(TrackerConfig cfg = {});
    void set_config(const TrackerConfig& c);
    const TrackerConfig& config() const { return cfg_; }
    void reset();

    TrackResult process(const ImageView& img);
    SceneStats scene_stats(const ImageView& img, int step = 4) const;

    // Alle componenten uit de laatste process()-aanroep (voor tests en diagnose).
    const std::vector<Blob>& last_blobs() const { return blobs_; }

private:
    void build_lut();
    // Labelt het masker binnen [x0,x1)x[y0,y1) en vult blobs_.
    void find_blobs(const ImageView& img, int x0, int y0, int x1, int y1);
    void classify(Blob& b, int height) const;

    TrackerConfig cfg_;
    std::vector<uint8_t> lut_;      // 65536: 1 = chroma in de sector
    std::vector<int32_t> labels_;
    std::vector<int32_t> parent_;
    std::vector<Blob> blobs_;

    // Volgtoestand
    TrackState state_ = TrackState::Searching;
    bool have_last_ = false;
    double last_x_ = 0, last_y_ = 0, last_r_ = 0, vx_ = 0, vy_ = 0;
    int misses_ = 0;
    // Kandidaat die op bevestiging wacht (acquisitie of sprong).
    bool pending_ = false;
    double pend_x_ = 0, pend_y_ = 0, pend_r_ = 0;
    int pend_count_ = 0;
};

}  // namespace ravengun::vision
