// SPDX-License-Identifier: GPL-3.0-or-later
// Proef voor rg-amhw.dll zonder spel: een klein Windows-programma dat de DLL laadt en daarna doet wat de AM Library van de Ring-games doet
// (CreateFileW \\.\columba, DeviceIoControl 0x9C406104, \\.\mxsram met versie, geometrie, lezen en schrijven), via zijn EIGEN importtabel
// (dat is wat de DLL omzet). Draait onder Wine in een wegwerp-prefix; start geen game. Gebruik: amhw_probe.exe <map met rg-amhw.dll>
// Uitvoer: een regel per controle ("OK ..."/"FOUT ..."), exitcode 0 als alles klopt.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "amhw_dmi.h"
#include "amhw_jvs.h"

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "OK  " : "FOUT", what);
    if (!ok) ++g_fail;
}

// Een nep-JVS-bord achter een named pipe, zoals rg-jvsbridge het aanbiedt: leest ruwe bytes, antwoordt met een kader (adrestoekenning F1 en alles
// anders), en zwijgt op een globale reset (FF F0).
static HANDLE g_srv = INVALID_HANDLE_VALUE;
static volatile LONG g_srv_frames = 0;
static DWORD WINAPI jvs_server(LPVOID) {
    ConnectNamedPipe(g_srv, nullptr);
    uint8_t buf[512];
    DWORD n = 0;
    while (ReadFile(g_srv, buf, sizeof buf, &n, nullptr) && n) {
        InterlockedIncrement(&g_srv_frames);
        if (!rg_amhw::expects_reply(buf, n)) continue;
        const uint8_t rep[] = {0xE0, 0x00, 0x03, 0x01, 0x01, 0x05};   // status 1, rapport 1, som
        // In twee stukken sturen: het kader moet over meer dan een leesopdracht heen gelezen worden.
        DWORD w = 0;
        WriteFile(g_srv, rep, 4, &w, nullptr);
        Sleep(15);
        WriteFile(g_srv, rep + 4, 2, &w, nullptr);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("gebruik: amhw_probe.exe <map>\n"); return 2; }
    const std::string dir = argv[1];
    SetEnvironmentVariableA("RG_AMHW_OEM", argc > 2 ? argv[2] : "AAM");
    SetEnvironmentVariableA("RG_AMHW_VENDOR", argc > 3 ? argv[3] : "Supermicro");
    SetEnvironmentVariableA("RG_AMHW_DIR", dir.c_str());
    SetEnvironmentVariableA("RG_AMHW_JVS_PIPE", "\\\\.\\pipe\\rg_amhw_probe_jvs");
    g_srv = CreateNamedPipeA("\\\\.\\pipe\\rg_amhw_probe_jvs", PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
    check(g_srv != INVALID_HANDLE_VALUE, "nep-JVS-pipe gemaakt");
    CreateThread(nullptr, 0, jvs_server, nullptr, 0, nullptr);
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
    // De nieuwere AM Library (LGS, LGI3D) leest de BIOS-ruimte in stukken van 0x8010 en eist BytesReturned == de buffergrootte.
    c = CreateFileW(L"\\\\.\\columba", 0xC0000000, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    {
        std::vector<uint8_t> buf(0x8010);
        uint32_t in[4] = {0xF0000, 0, 1, 0x8010};
        DWORD got2 = 0;
        check(DeviceIoControl(c, 0x9C406104, in, sizeof in, buf.data(), 0x8010, &got2, nullptr) && got2 == 0x8010 && std::memcmp(&buf[0x10], "_DMI_", 5) == 0,
              "columba: buffer van 0x8010 -> 0x8010 bytes terug, _DMI_ op +0x10");
        uint32_t in2[4] = {0xF0000 + 0x7FF0, 0, 1, 0x8010};
        check(DeviceIoControl(c, 0x9C406104, in2, sizeof in2, buf.data(), 0x8010, &got2, nullptr) && got2 == 0x8010, "columba: tweede stuk (0xF7FF0) idem");
    }
    CloseHandle(c);
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

    {   // 0x9C406004: sectorgrootte van de nieuwere AM Library
        HANDLE s2 = CreateFileW(L"\\\\.\\mxsram", 0xC0000000, 3, nullptr, OPEN_EXISTING, 0x80, nullptr);
        uint32_t sec = 0;
        DWORD g2 = 0;
        check(DeviceIoControl(s2, 0x9C406004, nullptr, 0, &sec, 4, &g2, nullptr) && g2 == 4 && sec == 0x200, "mxsram: ioctl 0x9C406004 geeft sectorgrootte 0x200");
        CloseHandle(s2);
    }
    // De tabel die de dongle-init leest (<spel-id>_Table.dat): ontbreekt hij, dan opent de game een vervangend bestand naast de DLL.
    {
        const std::string missing = dir + "\\nergens\\SBZZ_Table.dat";
        HANDLE t = CreateFileA(missing.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0x80, nullptr);
        check(t != INVALID_HANDLE_VALUE, "een ontbrekende SBZZ_Table.dat opent via de vervanging");
        DWORD sz = t != INVALID_HANDLE_VALUE ? GetFileSize(t, nullptr) : 0;
        check(sz == 1190000, "de vervanging is 1190000 bytes");
        if (t != INVALID_HANDLE_VALUE) CloseHandle(t);
        const std::wstring wmissing(missing.begin(), missing.end());
        HANDLE tw = CreateFileW(wmissing.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0x80, nullptr);
        check(tw != INVALID_HANDLE_VALUE, "idem via CreateFileW");
        if (tw != INVALID_HANDLE_VALUE) CloseHandle(tw);
        HANDLE other = CreateFileA((dir + "\\nergens\\iets.dat").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0x80, nullptr);
        check(other == INVALID_HANDLE_VALUE, "een ander ontbrekend bestand blijft ontbreken");
        check(GetFileAttributesA((dir + "\\rg-amhw-tables\\SBZZ_Table.dat").c_str()) != INVALID_FILE_ATTRIBUTES, "de vervanging staat in rg-amhw-tables naast de DLL, niet bij het opgegeven pad");
    }
    {   // .rdata van een gedumpte exe: schrijfbaar in de kop -> de DLL zet het terug (alleen zichtbaar als het proefprogramma zo gebouwd is)
        auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (std::memcmp(sec[i].Name, ".rdata", 6) == 0)
                check(!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE), ".rdata staat niet meer op schrijfbaar in de kop");
    }
    // mxjvs: zoals amJvstDriverSetup (LGI 0x490B30) en amJvstDriverSend/Receive (0x490780 / 0x4908A0)
    HANDLE j = CreateFileA("\\\\.\\mxjvs", 0xC0000000, 0, nullptr, OPEN_EXISTING, 0x40000000, nullptr);
    check(j != INVALID_HANDLE_VALUE, "CreateFileA(\\\\.\\mxjvs) geeft een handvat (de pipe van de brug)");
    DCB dcb = {};
    COMMTIMEOUTS to = {};
    check(GetCommState(j, &dcb) && SetCommState(j, &dcb) && SetCommTimeouts(j, &to) && SetupComm(j, 0x400, 0x400) && PurgeComm(j, 0xF), "comm-aanroepen op mxjvs slagen");
    DWORD ms = 0xFFFFFFFF;
    check(GetCommModemStatus(j, &ms) && (ms == 0x10 || ms == 0x30), "modemstatus: sense-lijn (0x10 zonder adres)");
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    const uint8_t assign[] = {0xE0, 0xFF, 0x03, 0xF1, 0x01, 0xF4};   // adres 1 toekennen
    uint8_t outb[0x100] = {};
    BOOL ok = DeviceIoControl(j, 0x9C402000, const_cast<uint8_t*>(assign), sizeof assign, outb, 0x100, &got, &ov);
    check(ok || GetLastError() == ERROR_IO_PENDING, "DeviceIoControl 0x9C402000 aanvaard (TRUE of ERROR_IO_PENDING)");
    check(WaitForSingleObject(ov.hEvent, 2000) == WAIT_OBJECT_0, "de gebeurtenis van de OVERLAPPED wordt gezet");
    check(ov.InternalHigh == 6 && outb[0] == 0xE0 && outb[5] == 0x05, "het antwoordkader (6 bytes, uit twee stukken) staat in de uitvoer, InternalHigh = 6");
    const uint8_t reset[] = {0xE0, 0xFF, 0x03, 0xF0, 0xD9, 0xCB};
    ResetEvent(ov.hEvent);
    const DWORD t0 = GetTickCount();
    ok = DeviceIoControl(j, 0x9C402000, const_cast<uint8_t*>(reset), sizeof reset, outb, 0x100, &got, &ov);
    check(ok && ov.InternalHigh == 0 && GetTickCount() - t0 < 100 && WaitForSingleObject(ov.hEvent, 0) == WAIT_OBJECT_0, "globale reset: geen antwoord verwacht, meteen klaar met 0 bytes");
    check(g_srv_frames == 2, "het nep-bord kreeg beide kaders");
    check(!DeviceIoControl(j, 0x9C402004, nullptr, 0, nullptr, 0, &got, nullptr) && GetLastError() == ERROR_INVALID_FUNCTION, "een onbekende ioctl op mxjvs faalt (en wordt gelogd)");
    CloseHandle(ov.hEvent);
    CloseHandle(j);

    // Een gewoon bestand loopt ongemoeid door de haken.
    HANDLE n = CreateFileW((std::wstring(dir.begin(), dir.end()) + L"\\rg-amhw.dll").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(n != INVALID_HANDLE_VALUE, "een gewoon bestand openen werkt nog");
    char buf[2] = {};
    check(n != INVALID_HANDLE_VALUE && ReadFile(n, buf, 2, &got, nullptr) && got == 2 && buf[0] == 'M' && buf[1] == 'Z', "en lezen ook");
    if (n != INVALID_HANDLE_VALUE) CloseHandle(n);
    std::printf("%s\n", g_fail ? "MISLUKT" : "ALLES OK");
    return g_fail ? 1 : 0;
}
