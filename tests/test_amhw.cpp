// Tests voor de nagebootste DMI-tabel van rg-amhw (tools/rg-amhw/amhw_dmi.h): de bytes moeten precies zo gelezen worden als de AM Library
// van de Sega Ring-games ze leest (LGI_RingW_F_safe.exe 0x48F6E0 en 0x48F660), en de uitkomst moet het bordtype geven dat de game kent.
// Geen Windows, geen game: alleen de gegevens.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../tools/rg-amhw/amhw_dmi.h"
#include "../tools/rg-amhw/amhw_jvs.h"

using namespace rg_amhw;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static Parsed read_all(const Platform& p, int* reads = nullptr) {
    return parse_like_game([&](uint32_t addr, uint8_t* out) {
        phys_read(p, addr, out, kWindow);
        if (reads) ++*reads;
        return true;
    });
}

static uint8_t sum(const uint8_t* b, size_t n) { uint8_t s = 0; for (size_t i = 0; i < n; ++i) s = static_cast<uint8_t>(s + b[i]); return s; }

int main() {
    // De drie platformen die de games kennen, en wat de game eruit haalt.
    struct Case { const char* oem; const char* vendor; int type; } cases[] = {
        {"AAM", "Supermicro", 1},   // RingWide (LGI, LGS, GG, OG, SDR)
        {"AAM", "Advantech", 2},
        {"AAL", "NEC", 0},          // RingEdge
        {"AAL2", "NEC", 3},         // RingEdge 2 (alleen de nieuwere AM Library: LGI3D, Transformers)
    };
    for (const auto& c : cases) {
        Platform p;
        p.oem = c.oem; p.vendor = c.vendor;
        int reads = 0;
        const Parsed r = read_all(p, &reads);
        CHECK(r.ok && reads == 2);                       // twee leesopdrachten: de BIOS-ruimte en de tabel
        CHECK(r.vendor == c.vendor && r.oem == c.oem);
        CHECK(r.bios_version == "6.00" && r.bios_date == "01/01/2012");
        CHECK(board_type(r) == c.type);
    }
    // Negatieve controle: een combinatie die de game niet kent geeft een type dat zijn tabel niet heeft (daar crashte LGI op).
    Platform fout;
    fout.oem = "AAV"; fout.vendor = "Supermicro";
    CHECK(board_type(read_all(fout)) == -1);
    fout.oem = "AAL"; fout.vendor = "Supermicro";
    CHECK(board_type(read_all(fout)) == 3);   // oudere AM Library: 3 is onbekend (nulwijzer); daarom nooit zo instellen

    // De ingangsstructuur: "_DMI_" binnen de eerste 0xFFF8 bytes, geldige controlesommen, tabeladres met onderste 16 bits 0 (de game
    // vergelijkt tegen lengte + die 16 bits als een dword), tabellengte gelijk aan wat de tabel groot is.
    Platform p;
    std::vector<uint8_t> bios(kWindow);
    phys_read(p, kBiosBase, bios.data(), kWindow);
    CHECK(std::memcmp(&bios[0], "_SM_", 4) == 0 && std::memcmp(&bios[0x10], "_DMI_", 5) == 0);
    CHECK(sum(&bios[0], 0x1F) == 0);       // controlesom van de hele ingangsstructuur
    CHECK(sum(&bios[0x10], 0x0F) == 0);    // en van het _DMI_-deel
    uint16_t len; uint32_t addr; uint16_t cnt;
    std::memcpy(&len, &bios[0x16], 2); std::memcpy(&addr, &bios[0x18], 4); std::memcpy(&cnt, &bios[0x1C], 2);
    uint16_t tcount = 0;
    const auto tab = table(p, &tcount);
    CHECK(addr == kTableBase && (addr & 0xFFFF) == 0 && len == tab.size() && cnt == tcount && tcount == 4);
    for (size_t i = 0x20; i < kWindow; ++i) if (bios[i]) { CHECK(!"BIOS-ruimte na de ingangsstructuur is niet leeg"); break; }
    // De tabel op haar adres, met nullen erachter (de game leest altijd 0x10000 bytes en stopt bij een structuur met lengte 0).
    std::vector<uint8_t> t(kWindow);
    phys_read(p, kTableBase, t.data(), kWindow);
    CHECK(std::memcmp(t.data(), tab.data(), tab.size()) == 0);
    for (size_t i = tab.size(); i < kWindow; ++i) if (t[i]) { CHECK(!"na de tabel moeten nullen staan"); break; }
    CHECK(t[0] == 0 && t[1] == 0x18);       // eerst het BIOS-type 0
    // Een leesopdracht die een stuk van de tabel raakt geeft dat stuk op de goede plaats (deelleesopdracht).
    uint8_t mid[16];
    phys_read(p, kTableBase + 4, mid, sizeof mid);
    CHECK(std::memcmp(mid, tab.data() + 4, 16) == 0);
    uint8_t nul[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    phys_read(p, 0x10000, nul, sizeof nul);
    CHECK(sum(nul, 8) == 0);                // elders niets

    // De tekenreeksen passen in de buffers van de game: fabrikant 31 + nul (c30acf, 0x20), OEM 31 (c30aef), datum 10 + nul (c30ac4, 0xb).
    CHECK(p.vendor.size() < 0x20 && p.oem.size() < 0x20 && p.bios_date.size() <= 10 && p.bios_version.size() < 0x20);
    // JVS-kaders (amhw_jvs.h): lengte van het eerste complete kader, escapes, onvolledig, ruis ervoor, reset zonder antwoord.
    {
        const uint8_t ok[] = {0xE0, 0x00, 0x03, 0x01, 0x01, 0x05};
        CHECK(rg_amhw::frame_length(ok, 6) == 6);
        CHECK(rg_amhw::frame_length(ok, 5) == 0 && rg_amhw::frame_length(ok, 3) == 0 && rg_amhw::frame_length(ok, 1) == 0 && rg_amhw::frame_length(ok, 0) == 0);
        const uint8_t noise[] = {0x00, 0x7F, 0xE0, 0x00, 0x03, 0x01, 0x01, 0x05, 0xAA};
        size_t skip = 99;
        CHECK(rg_amhw::frame_length(noise, 9, &skip) == 6 && skip == 2);      // twee bytes ruis, dan een kader, met een staart die blijft liggen
        // D0 xx is een logische byte (xx + 1): aantal 4 = 4 logische bytes na het aantal, waarvan een geescapete E0 (D0 DF) en een D0 (D0 CF).
        const uint8_t esc[] = {0xE0, 0x00, 0x04, 0xD0, 0xDF, 0xD0, 0xCF, 0x01, 0x77};
        CHECK(rg_amhw::frame_length(esc, 9) == 9);
        CHECK(rg_amhw::frame_length(esc, 8) == 0);                              // een D0 vlak voor het einde of een tekort aan bytes: nog niet compleet
        const uint8_t cut[] = {0xE0, 0x00, 0x03, 0xD0};
        CHECK(rg_amhw::frame_length(cut, 4) == 0);                              // D0 zonder volgende byte
        const uint8_t resync[] = {0xE0, 0x00, 0xE0, 0x00, 0x03, 0x01, 0x01, 0x05};
        CHECK(rg_amhw::frame_length(resync, 8) == 0);                           // een nieuwe E0 midden in een kader: kapot
        const uint8_t reset[] = {0xE0, 0xFF, 0x03, 0xF0, 0xD9, 0xCB};
        const uint8_t assign[] = {0xE0, 0xFF, 0x03, 0xF1, 0x01, 0xF4};
        const uint8_t read1[] = {0xE0, 0x01, 0x02, 0x20, 0x23};
        CHECK(!rg_amhw::expects_reply(reset, 6) && rg_amhw::expects_reply(assign, 6) && rg_amhw::expects_reply(read1, 5));
        CHECK(rg_amhw::expects_reply(ok, 2));                                   // te kort om te beoordelen: wacht op een antwoord
    }
    std::printf("amhw: %d controles, %d fout\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
