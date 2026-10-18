// Het nagebootste STR-bord van rg-mfcam.dll (tools/rg-mfcam/str_board.h): eerst status uit zichzelf ("C01", "E00", dan "C01"), daarna een
// "C01" per opdracht van 10 bytes en niets meer uit zichzelf. Plus de antwoorden zoals de ontleder van het spel (0x5F5D00) ze leest.
#include <cstdio>
#include <cstring>
#include <string>

#include "../tools/rg-mfcam/str_board.h"

using namespace rg_strpcb;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

// Wat het spel met drie ontvangen bytes doet (0x5F5DBF..0x5F5F30): de vlaggen +0x49 (verbonden) en +0x4A (geen fout), en de foutcode (0 = geen).
struct GameView {
    bool connected = false, no_error = false;
    int error = 0;
    void feed(const char* r) {
        if (std::strstr(r, "C01")) connected = true;
        if (std::strstr(r, "C06")) connected = false;
        if (r[0] == 'E' && r[1] == '0' && r[2] == '0') no_error = true;
        else if (r[0] == 'E') error = 1;   // elke andere E-code is een fout voor het spel
    }
};

int main() {
    Board b;
    GameView g;
    // Luisterfase: C01, E00, C01, C01 ...
    CHECK(std::string(b.idle_status()) == "C01");
    CHECK(std::string(b.idle_status()) == "E00");
    for (int i = 0; i < 5; ++i) CHECK(std::string(b.idle_status()) == "C01");
    CHECK(b.status_sent == 7);
    Board fresh;
    for (int i = 0; i < 3; ++i) g.feed(fresh.idle_status());
    CHECK(g.connected && g.no_error && g.error == 0);   // de opstartcontrole (0x602DA6) is na drie meldingen tevreden
    // Lengte van elke melding: precies 3 bytes, zoals het spel leest.
    CHECK(std::strlen(Board::reply()) == kReplyLen);
    CHECK(std::strlen("E00") == kReplyLen);
    // Opdrachtfase: per 10 bytes een antwoord, ook als de bytes in stukken komen; daarna niets meer uit zichzelf.
    CHECK(b.on_bytes(0) == 0);
    CHECK(!b.command_mode);
    CHECK(b.on_bytes(10) == 1);
    CHECK(b.command_mode);
    CHECK(b.idle_status() == nullptr);
    CHECK(b.on_bytes(4) == 0);
    CHECK(b.on_bytes(6) == 1);
    CHECK(b.on_bytes(25) == 2);   // 45 bytes in totaal: 4 opdrachten
    CHECK(b.on_bytes(5) == 1);    // 50: 5
    CHECK(b.replies == 5);
    CHECK(b.rx_bytes == 50);
    GameView g2;
    g2.feed(Board::reply());
    CHECK(g2.connected && g2.error == 0);
    std::printf("%d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
