// De logica van de aanvullende schijfletter-herleiding van rg-mfcam.dll (tools/rg-mfcam/drive_fix.h): dezelfde regel als patch 0004 van de kern,
// alleen voor de letters in RG_DRIVE_FIXUP.
#include <cstdio>
#include <cstring>
#include <string>

#include "../tools/rg-mfcam/drive_fix.h"

using namespace rg_drivefix;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static std::string red(const char* in, const char* letters, size_t cap = 520) {
    char out[520];
    if (!redirect<char>(in, letters, out, cap < sizeof out ? cap : sizeof out)) return "-";
    return out;
}

int main() {
    // De paden die Mario Kart gebruikt (gemeten in de view en in de exe: F:/playdata/, F:/system/..., G:/Contents).
    CHECK(red("F:/system/cnt_dis_1.bin.tmp", "F") == ".\\TeknoParrot\\F\\system/cnt_dis_1.bin.tmp");
    CHECK(red("F:\\system\\x.bin", "F") == ".\\TeknoParrot\\F\\system\\x.bin");
    CHECK(red("f:/TestMode/testmode_*.bin", "F") == ".\\TeknoParrot\\F\\TestMode/testmode_*.bin");   // zoekpatroon blijft intact
    CHECK(red("F:/playdata/1812/", "F") == ".\\TeknoParrot\\F\\playdata/1812/");
    CHECK(red("F:/", "F") == ".\\TeknoParrot\\F\\");

    // G: alleen als hij in de lijst staat; standaard laten we hem met rust (ShareK blijft dan uit).
    CHECK(red("G:/Contents", "F") == "-");
    CHECK(red("G:/Contents", "FG") == ".\\TeknoParrot\\G\\Contents");
    // C: en Z: nooit, ook als ze in de lijst staan; geen letter, relatieve paden en rommel ook niet.
    CHECK(red("C:/x", "CZF") == "-");
    CHECK(red("Z:/home/x", "CZF") == "-");
    CHECK(red("Data/x.csv", "F") == "-");
    CHECK(red(".\\TeknoParrot\\F\\x", "F") == "-");   // al herleid (door de kern): niet nog eens
    CHECK(red("F:x", "F") == "-");
    CHECK(red("F", "F") == "-");
    CHECK(red("", "F") == "-");
    CHECK(red(nullptr, "F") == "-");
    CHECK(red("F:/x", "") == "-");
    CHECK(red("F:/x", nullptr) == "-");

    // Te kort uitvoerbuffer: weigeren, niet afkappen.
    CHECK(red("F:/abcdefghij", "F", 20) == "-");
    CHECK(red("F:/a", "F", 20) != "-");

    // Breed (wchar_t), zoals _wrename/_waccess_s/GetFileAttributesW/FindFirstFileW.
    wchar_t wout[128];
    CHECK(redirect<wchar_t>(L"G:/Contents", "FG", wout, 128) && std::wstring(wout) == L".\\TeknoParrot\\G\\Contents");
    CHECK(!redirect<wchar_t>(L"G:/Contents", "F", wout, 128));
    CHECK(!redirect<wchar_t>(L"\x0446:/x", "F", wout, 128));

    CHECK(letter_enabled('f', "F") && letter_enabled('F', "f"));
    CHECK(!letter_enabled('C', "C") && !letter_enabled('Z', "Z") && !letter_enabled('1', "1"));

    std::printf("drive_fix: %d controles, %d mislukt\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
