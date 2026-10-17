// SPDX-License-Identifier: GPL-3.0-or-later
// De dongle-tabel van de Sega Ring-games (<spel-id>_Table.dat, 1 190 000 bytes) en wat daar uit te halen is.
//
// Gelezen in de AM Library (amDongle, LGI_RingW_F_safe.exe 0x48D670 en 0x48D810; dezelfde indeling en byteverwisseling in LGI3D, LGS, GG, OG, SDR en TF):
//   0       .. 870000   10000 records van 87 bytes: een uitdaging van 7 bytes en 4 pagina's van 20 bytes (het verwachte antwoord per pagina)
//   870000  .. 1190000  10000 records van 32 bytes: een uitdaging van 16 bytes en het verwachte antwoord van 16 bytes
// De game kiest bij de start 100 willekeurige records, stuurt de uitdaging als `keychip.ds.compute=<hex>&page=<n>` of `keychip.ssd.proof=<hex>` naar de
// keychip (de AM-daemon) en vergelijkt het antwoord met het verwachte antwoord uit de tabel ("Verify MAC NG!!!" / "Verify SSD NG!!!", dan 'keychip niet
// gevonden', Error 0949). De verwachte antwoorden staan dus in het bestand: een AM-daemon die dezelfde tabel leest hoeft geen geheime sleutel te kennen.
// Het verwachte antwoord is NIET de ruwe bestandsinhoud: de game zet de bytes om met een vaste verwisseling (kPermDs / kPermSsd), de uitdaging blijft ruw.
// In 1 van de 10 gevallen stuurt de game een willekeurige uitdaging en vergelijkt hij niet (c2d678 in LGI): elk antwoord van de juiste lengte is dan goed.
// Daarnaast bestaat een vaste uitdaging 2CFECBC71CF1E4 (als bit 0 van keychip.appboot.systemflag gezet is) met vier vaste antwoorden in de exe (kFixedDs).
//
// Zonder dit bestand (LGS, GG, OG, SDR staan niet op de NAS) maakt rg-amhw.dll een vervangend bestand (fake_table) en leest de AM-daemon dezelfde bytes.
// Alleen standaardbibliotheek; gebruikt door tools/rg-amhw/rg_amhw.cpp (MinGW) en src/ravengun/launch/amdaemon.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace rg_amhw {

constexpr size_t kTableSize = 1190000;
constexpr size_t kDsRecord = 87, kDsCount = 10000, kSsdOffset = 870000, kSsdRecord = 32, kSsdCount = 10000;
constexpr size_t kDsPageSize = 20, kDsPages = 4, kSsdSize = 16, kDsChallenge = 7;

// uit[j] = bestand[kPerm*[j]] (LGI 0x48D71A.. en 0x48D8A7..; in alle zeven de exe's gelijk gevonden).
constexpr uint8_t kPermDs[20] = {15, 5, 13, 14, 10, 1, 2, 11, 16, 7, 4, 18, 12, 6, 3, 0, 17, 8, 19, 9};
constexpr uint8_t kPermSsd[16] = {6, 8, 4, 12, 7, 13, 1, 10, 2, 3, 11, 14, 15, 0, 5, 9};

// De vaste uitdaging en de vier antwoorden (LGI 0x9506F0..), in alle zeven de exe's gelijk.
inline const char* fixed_ds_challenge() { return "2CFECBC71CF1E4"; }
inline const char* fixed_ds_answer(int page) {
    static const char* const a[4] = {"CA6ED736401682DD4411A27D2440AC4B478BAD8B", "4AC606302CE5EF51ABB3DF2DC46C863B3C06AA2C",
                                     "2AAF35B2ABA4C6840BDB7BD40ECBCE2CCA934795", "2F6D713DABDE3C43DF818491AB9467BA8BA0FED4"};
    return page >= 0 && page < 4 ? a[page] : nullptr;
}

// Het vervangende bestand voor een spel zonder tabel: vaste pseudo-willekeurige bytes (zaad = FNV van de naam in hoofdletters). Moet byte voor byte gelijk
// zijn in de DLL en in de AM-daemon.
inline std::vector<uint8_t> fake_table(const std::string& file_name) {
    uint32_t x = 2166136261u;
    for (char ch : file_name) {
        const unsigned char c = static_cast<unsigned char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
        x = (x ^ c) * 16777619u;
    }
    if (!x) x = 1;
    std::vector<uint8_t> data(kTableSize);
    for (auto& b : data) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        b = static_cast<uint8_t>(x);
    }
    return data;
}

inline std::string to_hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

// Hexreeks (hoofd- of kleine letters) naar bytes; leeg bij een onbruikbare reeks.
inline std::string from_hex(const std::string& h) {
    if (h.empty() || h.size() % 2) return {};
    auto v = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string out;
    for (size_t i = 0; i < h.size(); i += 2) {
        const int a = v(h[i]), b = v(h[i + 1]);
        if (a < 0 || b < 0) return {};
        out += static_cast<char>(a * 16 + b);
    }
    return out;
}

class AuthTable {
public:
    explicit AuthTable(std::vector<uint8_t> data) : d_(std::move(data)) {
        if (d_.size() < kTableSize) { d_.clear(); return; }
        ds_.reserve(kDsCount * 2);
        ssd_.reserve(kSsdCount * 2);
        for (size_t i = 0; i < kDsCount; ++i) ds_.emplace(std::string(reinterpret_cast<const char*>(&d_[i * kDsRecord]), kDsChallenge), i);
        for (size_t i = 0; i < kSsdCount; ++i) ssd_.emplace(std::string(reinterpret_cast<const char*>(&d_[kSsdOffset + i * kSsdRecord]), kSsdSize), i);
    }
    bool valid() const { return !d_.empty(); }

    // Het antwoord (hoofdletter-hex) op een ds.compute-uitdaging; leeg als de uitdaging in de tabel niet voorkomt (dan was het een willekeurige en
    // vergelijkt de game niet).
    std::string ds(const std::string& challenge_hex, int page) const {
        if (!valid() || page < 0 || page >= static_cast<int>(kDsPages)) return {};
        std::string up;
        for (char c : challenge_hex) up += static_cast<char>(c >= 'a' && c <= 'f' ? c - 32 : c);
        if (up == fixed_ds_challenge()) return fixed_ds_answer(page);
        const std::string key = from_hex(challenge_hex);
        auto it = ds_.find(key);
        if (it == ds_.end()) return {};
        const uint8_t* src = &d_[it->second * kDsRecord + kDsChallenge + static_cast<size_t>(page) * kDsPageSize];
        uint8_t out[kDsPageSize];
        for (size_t j = 0; j < kDsPageSize; ++j) out[j] = src[kPermDs[j]];
        return to_hex(out, kDsPageSize);
    }
    std::string ssd(const std::string& challenge_hex) const {
        if (!valid()) return {};
        const std::string key = from_hex(challenge_hex);
        auto it = ssd_.find(key);
        if (it == ssd_.end()) return {};
        const uint8_t* src = &d_[kSsdOffset + it->second * kSsdRecord + kSsdSize];
        uint8_t out[kSsdSize];
        for (size_t j = 0; j < kSsdSize; ++j) out[j] = src[kPermSsd[j]];
        return to_hex(out, kSsdSize);
    }

private:
    std::vector<uint8_t> d_;
    std::unordered_map<std::string, size_t> ds_, ssd_;
};

}  // namespace rg_amhw
