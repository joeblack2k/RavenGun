// Tests voor de nagebootste DMI-tabel van rg-amhw (tools/rg-amhw/amhw_dmi.h): de bytes moeten precies zo gelezen worden als de AM Library
// van de Sega Ring-games ze leest (LGI_RingW_F_safe.exe 0x48F6E0 en 0x48F660), en de uitkomst moet het bordtype geven dat de game kent.
// Geen Windows, geen game: alleen de gegevens.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../tools/rg-amhw/amhw_dmi.h"
#include "../tools/rg-amhw/amhw_jvs.h"
#include "../tools/rg-amhw/amhw_smbus.h"
#include "../tools/rg-amhw/amhw_sysdata.h"

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
    // AM_SYSDATA_STATIC (amhw_sysdata.h): de CRC is zlib.crc32 over de 0x1C bytes na de eerste vier; een leeg SRAM krijgt een record met de regio.
    {
        CHECK(rg_amhw::crc32_ieee(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u);
        const std::vector<uint8_t> zeros508(508, 0);
        CHECK(rg_amhw::crc32_ieee(zeros508.data(), zeros508.size()) == 0x0FBB6D54u);   // wat de game zelf in het SRAM schreef (LGS mxsram.bin op 0)
        std::vector<uint8_t> sram(2 * 1024 * 1024, 0);
        CHECK(rg_amhw::seed_static_sysdata(sram, 2));
        CHECK(rg_amhw::static_record_valid(&sram[0]) && rg_amhw::static_record_valid(&sram[0x200]));
        CHECK(sram[0xC] == 2 && sram[0x200 + 0xC] == 2 && std::memcmp(&sram[0xF], "INVALID_SERIALNO", 16) == 0 && sram[0x1F] == 0);
        CHECK(!rg_amhw::seed_static_sysdata(sram, 2) && sram[0xC] == 2);                // een geldig record met dezelfde regio blijft staan
        CHECK(rg_amhw::seed_static_sysdata(sram, 4) && sram[0xC] == 4 && sram[0x200 + 0xC] == 4);   // een andere regio (de optie) wint
        CHECK(rg_amhw::static_record_valid(&sram[0]) && rg_amhw::static_record_valid(&sram[0x200]));
        sram[0xC] = 0; sram[0x200 + 0xC] = 0;                                           // regio 0 is nooit geldig, ook niet met een goede CRC
        const uint32_t c = rg_amhw::crc32_ieee(&sram[4], 0x1C);
        std::memcpy(&sram[0], &c, 4); std::memcpy(&sram[0x200], &c, 4);
        CHECK(rg_amhw::seed_static_sysdata(sram, 8) && sram[0xC] == 8);
        std::vector<uint8_t> small(16, 0);
        CHECK(!rg_amhw::seed_static_sysdata(small, 2) && !rg_amhw::seed_static_sysdata(sram, 0));
    }
    // SMBus en Super I/O (amhw_smbus.h).
    {
        rg_amhw::HwRegs hw;
        uint8_t p[0x25] = {0, 5, 0x20, 4, 0, 0, 0};                                    // lees dip-register 4 van de PCA9535 op 0x20
        CHECK(hw.smbus_request(p) && p[0] == 0 && p[5] == 0x70);
        CHECK(((p[5] >> 4) & 7) == 7);                                                  // resolutie-index 7 = 1920x1080 (OG-tabel 0x5F72A4)
        uint8_t in0[0x25] = {0, 5, 0x20, 0}, in1[0x25] = {0, 5, 0x20, 1};          // invoerpoorten met pull-up: 0xFF = geen schakelaar aan (de game keert ze om)
        CHECK(hw.smbus_request(in0) && in0[5] == 0xFF && hw.smbus_request(in1) && in1[5] == 0xFF);
        uint8_t wr0[0x25] = {0, 4, 0x20, 0, 0, 0x00, 0xFF};
        CHECK(hw.smbus_request(wr0));
        uint8_t in0b[0x25] = {0, 5, 0x20, 0};
        CHECK(hw.smbus_request(in0b) && in0b[5] == 0xFF);                           // schrijven naar een invoerpoort verandert niets
        uint8_t w[0x25] = {0, 4, 0x20, 3, 0, 0x5A, 0xFF};                               // schrijf register 3, lees terug
        CHECK(hw.smbus_request(w));
        uint8_t r[0x25] = {0, 5, 0x20, 3};
        CHECK(hw.smbus_request(r) && r[5] == 0x5A);
        uint8_t rdy[0x25] = {0x18, 3, 0x57};                                            // 'EEPROM klaar?' (amEeprom 0x58EDC0): status 0x18 wordt 0
        CHECK(hw.smbus_request(rdy) && rdy[0] == 0);
        uint8_t bad[0x25] = {0, 7, 0x20, 3};
        CHECK(!hw.smbus_request(bad));
        hw.set_dip(0x30, 0x01);
        uint8_t d2[0x25] = {0, 5, 0x20, 5};
        CHECK(hw.smbus_request(d2) && d2[5] == 1);
        // EEPROM: schrijf 5 bytes op offset 0x123, lees ze terug; een lege EEPROM is 0xFF.
        uint8_t e[0x27] = {0, 9, 0x57, 0, 0x23, 0x01, 5};
        size_t eo = 0, el = 0;
        CHECK(hw.eeprom_block(e, &eo, &el) && e[0] == 0 && e[7] == 0xFF && e[11] == 0xFF && el == 0);
        uint8_t ew[0x27] = {0, 8, 0x57, 0, 0x23, 0x01, 5, 1, 2, 3, 4, 5};
        CHECK(hw.eeprom_block(ew, &eo, &el) && eo == 0x123 && el == 5);
        uint8_t er[0x27] = {0, 9, 0x57, 0, 0x23, 0x01, 5};
        CHECK(hw.eeprom_block(er, nullptr, nullptr) && er[7] == 1 && er[11] == 5);
        uint8_t big[0x27] = {0, 9, 0x57, 0, 0, 0, 0x21};
        CHECK(!hw.eeprom_block(big, nullptr, nullptr));                                  // meer dan 0x20 per keer kan niet
        // Super I/O: chip-id 0xA230 (type 1) op chip 0; chip 1 bestaat niet.
        uint8_t id1[4] = {0, 0x0B, 0x20, 0}, id2[4] = {0, 0x0B, 0x21, 0};
        CHECK(hw.superio_read(id1) && hw.superio_read(id2));
        CHECK((((id1[3] << 8) | id2[3]) & 0xFFF0) == 0xA230);
        uint8_t n1[4] = {1, 0x0B, 0x20, 0}, n2[4] = {1, 0x0B, 0x21, 0};
        CHECK(hw.superio_read(n1) && hw.superio_read(n2) && n1[3] == 0 && n2[3] == 0);
        uint8_t i2c[3] = {0, 0x48, 0};
        CHECK(hw.lpc_read(i2c) && i2c[2] >= 0x10 && i2c[2] < 0x70);                     // I2C-adres van de monitor binnen 0x10..0x6F
        uint8_t t1[3] = {0, 0x27, 0}, t2[3] = {0, 0x50, 0};                                // temperaturen: 40 graden, bank 1: 42, bank 0: 0
        CHECK(hw.lpc_read(t1) && t1[2] == 40);
        const uint8_t bk1[3] = {0, 0x4E, 0x01};
        CHECK(hw.lpc_write(bk1) && hw.lpc_read(t2) && t2[2] == 42);
        const uint8_t bk0[3] = {0, 0x4E, 0x00};
        uint8_t t3[3] = {0, 0x50, 0};
        CHECK(hw.lpc_write(bk0) && hw.lpc_read(t3) && t3[2] == 0);
        const uint8_t lw[3] = {0, 0x4E, 0x01};
        uint8_t lr[3] = {0, 0x4E, 0};
        CHECK(hw.lpc_write(lw) && hw.lpc_read(lr) && lr[2] == 1);
    }
    std::printf("amhw: %d controles, %d fout\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
