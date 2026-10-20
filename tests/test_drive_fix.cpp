// The logic of the supplementary drive-letter redirection of rg-mfcam.dll (tools/rg-mfcam/drive_fix.h): the same rule as core patch 0004,
// only for the letters in RG_DRIVE_FIXUP.
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
    // The paths Mario Kart uses (measured in the view and in the exe: F:/playdata/, F:/system/..., G:/Contents).
    CHECK(red("F:/system/cnt_dis_1.bin.tmp", "F") == ".\\TeknoParrot\\F\\system/cnt_dis_1.bin.tmp");
    CHECK(red("F:\\system\\x.bin", "F") == ".\\TeknoParrot\\F\\system\\x.bin");
    CHECK(red("f:/TestMode/testmode_*.bin", "F") == ".\\TeknoParrot\\F\\TestMode/testmode_*.bin");   // search pattern stays intact
    CHECK(red("F:/playdata/1812/", "F") == ".\\TeknoParrot\\F\\playdata/1812/");
    CHECK(red("F:/", "F") == ".\\TeknoParrot\\F\\");

    // G: only if it is in the list; by default we leave it alone (ShareK then stays off).
    CHECK(red("G:/Contents", "F") == "-");
    CHECK(red("G:/Contents", "FG") == ".\\TeknoParrot\\G\\Contents");
    // C: and Z: never, even if they are in the list; no letter, relative paths and junk neither.
    CHECK(red("C:/x", "CZF") == "-");
    CHECK(red("Z:/home/x", "CZF") == "-");
    CHECK(red("Data/x.csv", "F") == "-");
    CHECK(red(".\\TeknoParrot\\F\\x", "F") == "-");   // already redirected (by the core): not again
    CHECK(red("F:x", "F") == "-");
    CHECK(red("F", "F") == "-");
    CHECK(red("", "F") == "-");
    CHECK(red(nullptr, "F") == "-");
    CHECK(red("F:/x", "") == "-");
    CHECK(red("F:/x", nullptr) == "-");

    // Output buffer too short: refuse, do not truncate.
    CHECK(red("F:/abcdefghij", "F", 20) == "-");
    CHECK(red("F:/a", "F", 20) != "-");

    // Wide (wchar_t), like _wrename/_waccess_s/GetFileAttributesW/FindFirstFileW.
    wchar_t wout[128];
    CHECK(redirect<wchar_t>(L"G:/Contents", "FG", wout, 128) && std::wstring(wout) == L".\\TeknoParrot\\G\\Contents");
    CHECK(!redirect<wchar_t>(L"G:/Contents", "F", wout, 128));
    CHECK(!redirect<wchar_t>(L"\x0446:/x", "F", wout, 128));

    CHECK(letter_enabled('f', "F") && letter_enabled('F', "f"));
    CHECK(!letter_enabled('C', "C") && !letter_enabled('Z', "Z") && !letter_enabled('1', "1"));

    // TeknoParrot style: strings of the game itself ("F:/" -> "./F", same length), with the real cases from MK_AGP3_FINAL.exe.
    {
        const char raw[] = "\0F:/system/\0\0\0F:/income/\0F:/\0" "1505\0F:\\workspace\\rom\\x.cpp\0"
                           "G:/\0H:/\0I:/\0J:/\0G:/Contents\0xF:/nope\0f:/lower/\0";
        std::string s(raw, sizeof raw);
        const size_t n = rg_drivefix::patch_strings(reinterpret_cast<uint8_t*>(&s[0]), s.size(), "F", 1);
        CHECK(n == 4);   // F:/system/, F:/income/, F:/ (bare, not in a drive row), f:/lower/
        CHECK(s.find("./Fsystem/") != std::string::npos);
        CHECK(s.find("./Fincome/") != std::string::npos);
        CHECK(s.find(std::string("\0./F\0" "1505", 9)) != std::string::npos);
        CHECK(s.find("./Flower/") != std::string::npos);
        CHECK(s.find("F:\\workspace\\rom\\x.cpp") != std::string::npos);   // source file path stays
        CHECK(s.find(std::string("G:/\0H:/\0I:/\0J:/", 15)) != std::string::npos);                       // drive list stays
        CHECK(s.find("G:/Contents") != std::string::npos);                // G is not in the list
        CHECK(s.find("xF:/nope") != std::string::npos);                    // in the middle of a string: no
        CHECK(s.size() == sizeof raw);                                      // in place: same length
        // with G added: G:/Contents yes, the drive list no
        std::string t(raw, sizeof raw);
        const size_t n2 = rg_drivefix::patch_strings(reinterpret_cast<uint8_t*>(&t[0]), t.size(), "FG", 1);
        CHECK(n2 == 5);
        CHECK(t.find("./GContents") != std::string::npos);
        CHECK(t.find(std::string("G:/\0H:/", 7)) != std::string::npos);   // even with G in the list the drive list stays
    }
    {
        // wide: "F:\income\Income_*.bin" and "F:/playdata/1812/" (after a narrow string "115\0"), plus G:/Contents which stays
        const char rawn[] = "115\0F\0:\0/\0p\0l\0\0\0\0\0F\0:\0\\\0i\0n\0\0\0G\0:\0/\0C\0\0\0";
        std::string w(rawn, sizeof rawn);
        const size_t n = rg_drivefix::patch_strings(reinterpret_cast<uint8_t*>(&w[0]), w.size(), "F", 2);
        CHECK(n == 2);
        CHECK(w.find(std::string(".\0/\0F\0p\0l\0", 10)) != std::string::npos);
        CHECK(w.find(std::string(".\0\\\0F\0i\0n\0", 10)) != std::string::npos);
        CHECK(w.find(std::string("G\0:\0/\0C\0", 8)) != std::string::npos);
    }

    std::printf("drive_fix: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
