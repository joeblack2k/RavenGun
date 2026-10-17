// SPDX-License-Identifier: GPL-3.0-or-later
// Proef voor rg-amhw.dll zonder spel: een klein Windows-programma dat de DLL laadt en daarna doet wat de AM Library van de Ring-games doet
// (CreateFileW \\.\columba, DeviceIoControl 0x9C406104, \\.\mxsram met versie, geometrie, lezen en schrijven), via zijn EIGEN importtabel
// (dat is wat de DLL omzet). Draait onder Wine in een wegwerp-prefix; start geen game. Gebruik: amhw_probe.exe <map met rg-amhw.dll>
// Uitvoer: een regel per controle ("OK ..."/"FOUT ..."), exitcode 0 als alles klopt.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "amhw_dmi.h"

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "OK  " : "FOUT", what);
    if (!ok) ++g_fail;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("gebruik: amhw_probe.exe <map>\n"); return 2; }
    const std::string dir = argv[1];
    SetEnvironmentVariableA("RG_AMHW_OEM", argc > 2 ? argv[2] : "AAM");
    SetEnvironmentVariableA("RG_AMHW_VENDOR", argc > 3 ? argv[3] : "Supermicro");
    SetEnvironmentVariableA("RG_AMHW_DIR", dir.c_str());
    DeleteFileA((dir + "\\mxsram.bin").c_str());
    HMODULE m = LoadLibraryA((dir + "\\rg-amhw.dll").c_str());
    check(m != nullptr, "rg-amhw.dll laadt");
    if (!m) return 1;

    // columba: zoals amOemstringGetOemstring (LGI 0x48F6E0)
    HANDLE c = CreateFileW(L"\\\\.\\columba", 0xC0000000, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    check(c != INVALID_HANDLE_VALUE, "CreateFileW(\\\\.\\columba) geeft een handvat");
    rg_amhw::Parsed r = rg_amhw::parse_like_game([&](uint32_t addr, uint8_t* out) {
        uint32_t in[4] = {addr, 0, 1, 0x10000};
        DWORD got = 0;
        const BOOL ok = DeviceIoControl(c, 0x9C406104, in, sizeof in, out, 0x10000, &got, nullptr);
        return ok && got == 0x10000;
    });
    check(r.ok, "DMI-tabel gelezen (twee opdrachten van 0x10000 bytes)");
    std::printf("     fabrikant '%s', OEM '%s', bordtype %d\n", r.vendor.c_str(), r.oem.c_str(), rg_amhw::board_type(r));
    check(r.vendor == (argc > 3 ? argv[3] : "Supermicro") && r.oem == (argc > 2 ? argv[2] : "AAM"), "fabrikant en platform-id kloppen");
    check(CloseHandle(c) != 0, "CloseHandle op het nagebootste handvat");
    check(CreateFileW(L"\\\\.\\mxsuperio", 0xC0000000, 0, nullptr, OPEN_EXISTING, 0, nullptr) == INVALID_HANDLE_VALUE, "\\\\.\\mxsuperio is (nog) niet nagebootst en faalt zoals voorheen");

    // mxsram: zoals amSramInit (LGI 0x4879F0) en amSramRead/Write
    HANDLE s = CreateFileW(L"\\\\.\\mxsram", 0xC0000000, 3, nullptr, OPEN_EXISTING, 0x80, nullptr);
    check(s != INVALID_HANDLE_VALUE, "CreateFileW(\\\\.\\mxsram) geeft een handvat");
    uint32_t ver = 0; DWORD got = 0;
    check(DeviceIoControl(s, 0x9C406000, nullptr, 0, &ver, 4, &got, nullptr) && got == 4 && (ver & 0xFFFF) == 1, "versie: 4 bytes, laag woord 1");
    uint32_t g[6] = {};
    check(DeviceIoControl(s, 0x70000, nullptr, 0, g, 0x18, &got, nullptr) && got == 0x18, "geometrie: 0x18 bytes");
    const unsigned long long total = static_cast<unsigned long long>(g[3]) * g[4];
    std::printf("     totaal %llu bytes, sectorgrootte 0x%X, deler %u\n", total, g[5], g[0]);
    check(total == 2048ull * 1024 && g[5] == 0x200 && g[0] == 1 && g[1] == 0, "2 MB in sectoren van 0x200");
    char w[0x200], rd[0x200] = {};
    for (int i = 0; i < 0x200; ++i) w[i] = static_cast<char>(i * 7 + 3);
    LONG hi = 0;
    check(SetFilePointer(s, 0x4000, &hi, FILE_BEGIN) == 0x4000, "SetFilePointer 0x4000");
    check(WriteFile(s, w, 0x200, &got, nullptr) && got == 0x200, "WriteFile 0x200");
    SetFilePointer(s, 0x4000, &hi, FILE_BEGIN);
    check(ReadFile(s, rd, 0x200, &got, nullptr) && got == 0x200 && std::memcmp(w, rd, 0x200) == 0, "ReadFile geeft terug wat er stond");
    SetFilePointer(s, 0, &hi, FILE_BEGIN);
    char z[0x200];
    check(ReadFile(s, z, 0x200, &got, nullptr) && got == 0x200 && z[0] == 0 && z[0x1FF] == 0, "een ander sector is nog leeg");
    check(SetFilePointer(s, 0x200000, &hi, FILE_BEGIN) == 0x200000 && ReadFile(s, z, 0x200, &got, nullptr) && got == 0, "voorbij het einde: 0 bytes");
    CloseHandle(s);
    // Persistentie: een tweede open leest het bestand weer in (de DLL leest alleen bij het eerste open; dus bestandscontrole).
    FILE* f = std::fopen((dir + "\\mxsram.bin").c_str(), "rb");
    check(f != nullptr, "mxsram.bin staat naast de DLL");
    if (f) {
        std::fseek(f, 0x4000, SEEK_SET);
        char fb[0x200] = {};
        const size_t n = std::fread(fb, 1, 0x200, f);
        std::fclose(f);
        check(n == 0x200 && std::memcmp(fb, w, 0x200) == 0, "het geschreven sector staat in mxsram.bin");
    }

    // Een gewoon bestand loopt ongemoeid door de haken.
    HANDLE n = CreateFileW((std::wstring(dir.begin(), dir.end()) + L"\\rg-amhw.dll").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(n != INVALID_HANDLE_VALUE, "een gewoon bestand openen werkt nog");
    char buf[2] = {};
    check(n != INVALID_HANDLE_VALUE && ReadFile(n, buf, 2, &got, nullptr) && got == 2 && buf[0] == 'M' && buf[1] == 'Z', "en lezen ook");
    if (n != INVALID_HANDLE_VALUE) CloseHandle(n);
    std::printf("%s\n", g_fail ? "MISLUKT" : "ALLES OK");
    return g_fail ? 1 : 0;
}
