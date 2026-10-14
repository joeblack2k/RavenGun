#pragma once
// De stapel van de hoofdthread van een Windows-game ([Launch] StackReserve in game.ini).
//
// Onder Wine krijgt de hoofdthread precies de stapel uit de PE-kop van de exe (SizeOfStackReserve). Sommige games hebben daar
// onder Wine te weinig aan; TeknoParrot zelf start Mario Kart Arcade GP DX onder Wine (Winlator) daarom vanaf een kopie van
// MK_AGP3_FINAL.exe met dezelfde naam en 16 MiB in dat ene veld (TeknoParrotUI, Tools/InputMethodAudit/AndroidManagedImportTest.cs:
// "patches an exact-name sixteen MiB stack copy"). Hier: het veld lezen en zetten in de bytes van een PE32- of PE32+-bestand, en de
// waarde uit game.ini lezen. De launcher (direct.cpp, apply_direct) schrijft de kopie in de view van de game, nooit op de NAS.
#include <cstdint>
#include <string>

namespace ravengun::launch {

// Grenzen voor [Launch] StackReserve: minstens 64 KiB, hoogstens 256 MiB (een 32-bits proces moet hem nog kunnen reserveren).
inline constexpr uint64_t kStackReserveMin = 64ull * 1024;
inline constexpr uint64_t kStackReserveMax = 256ull * 1024 * 1024;

// "16M", "16 MiB", "16MB", "512K", "512 KiB", "0x1000000" of "16777216" -> bytes. K/M zijn 1024-machten (zoals editbin /STACK).
// false bij een lege, onleesbare of te grote waarde; de grenzen hierboven controleert de aanroeper.
bool parse_stack_size(const std::string& text, uint64_t& out);

// "16 MiB (16777216)", "1 MiB (1048576)", "96 KiB (98304)", of alleen het getal als het geen hele KiB is.
std::string stack_size_text(uint64_t bytes);

// SizeOfStackReserve uit de bytes van een PE-bestand. false + err als het geen PE32/PE32+-bestand is (MZ, PE-handtekening,
// optionele kop groot genoeg).
bool pe_stack_reserve(const std::string& data, uint64_t& reserve, std::string& err);

// Zet SizeOfStackReserve in `data` op `reserve` (verder blijft elke byte gelijk; ook de controlesom, die Windows en Wine voor een exe
// niet nakijken). false + err als het geen PE is of als de waarde niet in een PE32-veld past.
bool pe_set_stack_reserve(std::string& data, uint64_t reserve, std::string& err);

// De OpenParrot-kern herkent een game aan de CRC32 van de eerste 0x400 bytes van de exe in het geheugen (OpenParrot
// GameDetect.cpp, GetCRC32(moduleBase, 0x400)); een ander veld in de kop zou die herkenning breken. Deze functie zet de vier bytes
// op kHeaderCrcPatchAt (de opvulling achter "$" in de DOS-stub, die Windows en Wine nooit lezen) zo dat die CRC32 weer gelijk is aan
// `crc`. Alleen de stub verandert; de rest van de kop blijft zoals hij is. false + err als de exe korter is dan 0x400 bytes of als de
// PE-kop in de stub begint.
inline constexpr size_t kHeaderCrcLength = 0x400;
inline constexpr size_t kHeaderCrcPatchAt = 0x78;
uint32_t crc32_bytes(const std::string& data, size_t length);
bool pe_keep_header_crc(std::string& data, uint32_t crc, std::string& err);

}  // namespace ravengun::launch
