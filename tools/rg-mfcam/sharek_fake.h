// SPDX-License-Identifier: GPL-3.0-or-later
// De beslislogica van "een eigen ShareK-object" in rg-mfcam.dll (zie VIERDE TAAK in rg_mfcam.cpp), zonder Windows-aanroepen, zodat
// tests/test_sharek_fake.cpp haar op de host kan toetsen.
//
// WAT HET SPEL DOET (gemeten aan MK_AGP3_FINAL.exe v1.18.16, objdump; adressen zonder ASLR, imagebase 0x400000)
//   ShareKManager.cpp (0x47d9c6..0x482ced) is de client van "ShareK", het hulpprogramma dat bij de echte kast de content-distributie
//   (updates tussen kasten, NET_USE_SHAREK in Data/System/Config/NetConfig.csv) doet. ShareK.exe is een .NET-programma; de client-kant
//   is ipccl.dll (.NET, op de echte kast met RegAsm.exe geregistreerd: init_w10.ps1) en dat is een COM-klasse:
//       CoCreateInstance(CLSID {ccc44b2a-709a-33a8-a1b0-5dbe25b19c2c}, NULL, 0x17, IID {c2116f7f-cf46-3c68-b187-9d0fe9f70ed0}, &M->iface)
//   op 0x47e834 (StartIpcClient) en 0x48107f (herstart na een fout). Beide GUID's zijn naam-gebaseerd (versie 3), zoals .NET ze voor een
//   ComVisible-klasse zonder [Guid] afleidt; ze staan alleen in de exe, niet in ipccl.dll/ShareK.exe.
//   De registratie ontbreekt in onze prefix: CoCreateInstance geeft 0x80040154 (REGDB_E_CLASSNOTREG), het spel gooit een _com_error,
//   de catch-afhandelaar op 0x47e8f8 logt "*ERR* ComError Code = 80040154" en roept dan toch methode +0x20 aan op de nu lege interface
//   (0x47e95e: mov eax,[eax] = 0; mov ecx,[eax] -> toegangsfout). Dit is dus NIET de IAuth van AMAuthd (CLSID 4603BB03..., 0x4010c0):
//   de constructor van die wikkel vangt een mislukte CoCreateInstance zelf af (vlag-byte 0, 0x4010dd/0x4010d9).
//
// DE INTERFACE (IUnknown + IDispatch + .NET-methodes; de slotnummers zijn vtable-offset/4, gemeten aan elke aanroepplek)
//   Alle methodes zijn stdcall en geven een HRESULT; het spel gooit een _com_error bij HRESULT < 0. Elke aanroep heeft this + een uitvoer-
//   pointer of invoerwaarde (8 bytes argumenten), behalve de zes methodes met vier extra argumenten (20 bytes).
//   Wat het spel uit de antwoorden leest:
//     slot 7  (+0x1c)  StartIpcClient([out] VARIANT_BOOL*): WORD <> 0 = "Startup ShareK-Client." (0x47e892); 0 = "StartIpcClient() error"
//     slot 8  (+0x20)  stop/afsluiten, uitkomst ongelezen; aangeroepen vlak voor Release (0x47e77f) en in de catch (0x47e965)
//     slot 33 (+0x84)  ShareK-proces herstart? [out] WORD, 1 = ja: zet M+0x39b, daarna "ShareKプロセス再起動により再初期化" (0x47ed68)
//     slot 35 (+0x8c)  ShareK-status [out] WORD, bewaard in M+0x3a8 (vorige waarde in +0x3aa); de tick op 0x47ecd0 vertaalt hem:
//                        0 = onveranderd werk doen, 1 = "配信機能OFF" (eenmalig opruimen, daarna weer werk), 2 = "ネットワーク未初期化により
//                        自動復旧待機" (0x47eea5): met status 2 slaat de tick het werk over (0x47eee3: cmp [+0x3a8],2 -> klaar), elke
//                        tick; andere waarden = "未実装のShareKステータスコード"
//   De overige gelezen uitkomsten (slot 9, 11, 27, 31, 41, 43, 55) worden door de aanroeper eerst op 0 gezet (mov [ebp-x],ebx) en door een
//   leeg antwoord ongemoeid gelaten; 0 betekent daar "niets te doen".
//
// WAT HET EIGEN OBJECT DOET: een ShareK dat draait maar niets te verspreiden heeft. StartIpcClient slaagt (-1), de status is steeds 2
// (wacht op het netwerk: het spel doet dan niets), geen herstart gevraagd, en elke andere methode geeft S_OK zonder iets te schrijven.
#pragma once

#include <cstdint>
#include <cstring>

namespace rg_sharek {

struct Guid {
    uint32_t d1;
    uint16_t d2, d3;
    uint8_t d4[8];
};
inline bool same(const Guid& a, const Guid& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

constexpr Guid kClsid = {0xccc44b2au, 0x709a, 0x33a8, {0xa1, 0xb0, 0x5d, 0xbe, 0x25, 0xb1, 0x9c, 0x2c}};
constexpr Guid kIid = {0xc2116f7fu, 0xcf46, 0x3c68, {0xb1, 0x87, 0x9d, 0x0f, 0xe9, 0xf7, 0x0e, 0xd0}};
constexpr Guid kIidUnknown = {0x00000000u, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr Guid kIidDispatch = {0x00020400u, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// Voor welke CLSID's we een eigen object leveren. Alles anders (ook de IAuth van AMAuthd) gaat naar de echte CoCreateInstance.
inline bool wants_clsid(const Guid& clsid) { return same(clsid, kClsid); }
// Welke interfaces ons object kent (QueryInterface).
inline bool knows_iid(const Guid& iid) { return same(iid, kIid) || same(iid, kIidUnknown) || same(iid, kIidDispatch); }

enum class Kind : uint8_t {
    Nop,      // S_OK, schrijft niets
    Start,    // [out] WORD = -1 (VARIANT_TRUE)
    Status,   // [out] WORD = kStatusIdle
    Restart,  // [out] WORD = 0
};
constexpr int16_t kStartOk = -1;     // VARIANT_TRUE
constexpr int16_t kStatusIdle = 2;   // "wacht op het netwerk": de tick op 0x47ecd0 doet dan niets
constexpr int16_t kRestartNo = 0;

struct Slot {
    int index;       // vtable-offset / 4
    int arg_bytes;   // stdcall: this + argumenten, door de aangeroepene opgeruimd
    Kind kind;
    const char* use; // waar het spel hem aanroept (adres van de aanroep)
};

// Elke vtable-aanroep in ShareKManager.cpp (0x47c000..0x483400), gevonden met een scanner op "mov r,[r+off] ... call r".
constexpr Slot kSlots[] = {
    {7, 8, Kind::Start, "StartIpcClient 0x47e86b, 0x4810b6"},
    {8, 8, Kind::Nop, "afsluiten 0x47e77f, 0x47e965, 0x4811bb, 0x48131c"},
    {9, 8, Kind::Nop, "0x4802eb: uitkomst op 0 gezet; 3 = actie"},
    {10, 8, Kind::Nop, "0x4802ff (invoer 1)"},
    {11, 8, Kind::Nop, "0x480298"},
    {14, 20, Kind::Nop, "0x480828"},
    {15, 20, Kind::Nop, "0x480998"},
    {17, 20, Kind::Nop, "0x480b05"},
    {18, 20, Kind::Nop, "0x480c68"},
    {19, 20, Kind::Nop, "0x480dd9"},
    {20, 20, Kind::Nop, "0x480f48"},
    {25, 8, Kind::Nop, "0x482c33"},
    {26, 8, Kind::Nop, "0x482c09"},
    {27, 8, Kind::Nop, "0x481b77"},
    {28, 8, Kind::Nop, "0x481c9f"},
    {29, 8, Kind::Nop, "0x481db8"},
    {30, 8, Kind::Nop, "0x481eef"},
    {31, 8, Kind::Nop, "0x482007"},
    {32, 8, Kind::Nop, "0x482130"},
    {33, 8, Kind::Restart, "herstartvraag 0x48128e"},
    {34, 8, Kind::Nop, "0x481398"},
    {35, 8, Kind::Status, "status 0x4814b7"},
    {38, 8, Kind::Nop, "0x482247"},
    {40, 8, Kind::Nop, "0x482357"},
    {41, 8, Kind::Nop, "0x4815e7"},
    {43, 8, Kind::Nop, "0x481717"},
    {44, 8, Kind::Nop, "0x481831"},
    {46, 8, Kind::Nop, "0x482467"},
    {47, 8, Kind::Nop, "0x482579"},
    {52, 8, Kind::Nop, "0x482697"},
    {53, 8, Kind::Nop, "0x4827a9"},
    {54, 8, Kind::Nop, "0x4828c1"},
    {55, 8, Kind::Nop, "0x481947"},
    {56, 8, Kind::Nop, "0x481a61"},
    {57, 8, Kind::Nop, "0x4829de"},
    {58, 8, Kind::Nop, "0x482af7"},
};
constexpr int kSlotCount = static_cast<int>(sizeof kSlots / sizeof kSlots[0]);
constexpr int kVtableSize = 64;   // ruim boven het hoogste gebruikte slot (58)

constexpr const Slot* find_slot(int index) {
    for (int i = 0; i < kSlotCount; ++i)
        if (kSlots[i].index == index) return &kSlots[i];
    return nullptr;
}

// Het antwoord van een slot als waarde voor zijn [out] WORD; geeft false als het slot niets schrijft.
inline bool out_word(Kind k, int16_t& v) {
    switch (k) {
        case Kind::Start: v = kStartOk; return true;
        case Kind::Status: v = kStatusIdle; return true;
        case Kind::Restart: v = kRestartNo; return true;
        case Kind::Nop: break;
    }
    return false;
}

}  // namespace rg_sharek
