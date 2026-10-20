// The emulated STR board of rg-mfcam.dll (tools/rg-mfcam/str_board.h): first unprompted status ("C01", "E00", then "C01"), then a
// "C01" per 10-byte command and nothing unprompted. Plus the replies as the game's parser (0x5F5D00) reads them.
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

// What the game does with three received bytes (0x5F5DBF..0x5F5F30): the flags +0x49 (connected) and +0x4A (no error), and the error code (0 = none).
struct GameView {
    bool connected = false, no_error = false;
    int error = 0;
    void feed(const char* r) {
        if (std::strstr(r, "C01")) connected = true;
        if (std::strstr(r, "C06")) connected = false;
        if (r[0] == 'E' && r[1] == '0' && r[2] == '0') no_error = true;
        else if (r[0] == 'E') error = 1;   // any other E code is an error for the game
    }
};

int main() {
    Board b;
    GameView g;
    // Listening phase: C01, E00, C01, C01 ...
    CHECK(std::string(b.idle_status()) == "C01");
    CHECK(std::string(b.idle_status()) == "E00");
    for (int i = 0; i < 5; ++i) CHECK(std::string(b.idle_status()) == "C01");
    CHECK(b.status_sent == 7);
    Board fresh;
    for (int i = 0; i < 3; ++i) g.feed(fresh.idle_status());
    CHECK(g.connected && g.no_error && g.error == 0);   // the startup check (0x602DA6) is satisfied after three messages
    // Length of every message: exactly 3 bytes, as the game reads it.
    CHECK(std::strlen(Board::reply()) == kReplyLen);
    CHECK(std::strlen("E00") == kReplyLen);
    // Command phase: one reply per 10 bytes, even if the bytes arrive in pieces; after that nothing unprompted.
    CHECK(b.on_bytes(0) == 0);
    CHECK(!b.command_mode);
    CHECK(b.on_bytes(10) == 1);
    CHECK(b.command_mode);
    CHECK(b.idle_status() == nullptr);
    CHECK(b.on_bytes(4) == 0);
    CHECK(b.on_bytes(6) == 1);
    CHECK(b.on_bytes(25) == 2);   // 45 bytes in total: 4 commands
    CHECK(b.on_bytes(5) == 1);    // 50: 5 commands
    CHECK(b.replies == 5);
    CHECK(b.rx_bytes == 50);
    GameView g2;
    g2.feed(Board::reply());
    CHECK(g2.connected && g2.error == 0);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
