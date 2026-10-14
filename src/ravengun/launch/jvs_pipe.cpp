// SPDX-License-Identifier: GPL-3.0-or-later
#include "ravengun/launch/jvs_pipe.hpp"

#include <cctype>
#include <cstring>

namespace ravengun::launch::jvspipe {

std::vector<Bytes> frames(const Bytes& in, Bytes& rest) {
    std::vector<Bytes> out;
    Bytes buf = in;
    for (;;) {
        size_t i = 0;
        while (i < buf.size() && buf[i] != 0xE0) ++i;
        if (i >= buf.size()) { rest.clear(); return out; }
        buf.erase(buf.begin(), buf.begin() + static_cast<long>(i));
        Bytes raw;
        size_t j = 1;
        bool esc = false;
        enum { Meer, Kapot, Af } state = Meer;
        while (j < buf.size()) {
            const uint8_t b = buf[j];
            if (b == 0xE0) { state = Kapot; break; }   // nieuwe sync voordat het pakket af was: het halve pakket vervalt
            ++j;
            if (esc) { raw.push_back(static_cast<uint8_t>(b + 1)); esc = false; }
            else if (b == 0xD0) esc = true;
            else raw.push_back(b);
            if (raw.size() >= 2 && raw.size() == static_cast<size_t>(raw[1]) + 2) { state = Af; break; }
        }
        if (state == Af) {
            out.push_back(raw);
            buf.erase(buf.begin(), buf.begin() + static_cast<long>(j));
        } else if (state == Kapot) {
            buf.erase(buf.begin(), buf.begin() + static_cast<long>(j));
        } else {
            rest = buf;
            return out;
        }
    }
}

Bytes pack(const Bytes& body) {
    Bytes raw{0x00, static_cast<uint8_t>(body.size() + 1)};
    raw.insert(raw.end(), body.begin(), body.end());
    unsigned sum = 0;
    for (uint8_t b : raw) sum += b;
    raw.push_back(static_cast<uint8_t>(sum & 0xFF));
    Bytes out{0xE0};
    for (uint8_t b : raw) {
        if (b == 0xE0 || b == 0xD0) { out.push_back(0xD0); out.push_back(static_cast<uint8_t>(b - 1)); }
        else out.push_back(b);
    }
    return out;
}

Board default_board() {
    Board b;
    const char* id = "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551 ;Ver1.00;98/10";
    b.ident.assign(id, id + std::strlen(id));
    b.ident.push_back(0);
    b.features = {0x01, 4, 16, 0, 0x02, 2, 0, 0, 0x03, 8, 16, 0, 0x06, 10, 10, 4, 0x12, 18, 0, 0, 0x00};
    return b;
}

bool from_hex(const std::string& s, Bytes& out) {
    out.clear();
    size_t i = 0;
    while (i < s.size()) {
        if (std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }
        if (i + 1 >= s.size() || !std::isxdigit(static_cast<unsigned char>(s[i])) || !std::isxdigit(static_cast<unsigned char>(s[i + 1]))) return false;
        out.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
        i += 2;
    }
    return true;
}

std::string hex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (uint8_t x : b) { out += d[x >> 4]; out += d[x & 15]; }
    return out;
}

bool configure(Board& b, const std::string& ident, const std::string& features_hex) {
    if (!ident.empty()) {
        b.ident.assign(ident.begin(), ident.end());
        b.ident.push_back(0);
    }
    if (!features_hex.empty()) {
        Bytes f;
        if (!from_hex(features_hex, f)) return false;
        b.features = f;
    }
    return true;
}

std::optional<Bytes> answer(const Board& bd, const Bytes& data) {
    Bytes out{0x01};
    size_t i = 0;
    const size_t n = data.size();
    auto add = [&](std::initializer_list<uint8_t> x) { out.insert(out.end(), x); };
    while (i < n) {
        const uint8_t c = data[i];
        if (c == 0xF0) return std::nullopt;
        if (c == 0x10) { add({1}); out.insert(out.end(), bd.ident.begin(), bd.ident.end()); i += 1; }
        else if (c == 0x11) { add({1, 0x13}); i += 1; }
        else if (c == 0x12) { add({1, 0x30}); i += 1; }
        else if (c == 0x13) { add({1, 0x10}); i += 1; }
        else if (c == 0x14) { add({1}); out.insert(out.end(), bd.features.begin(), bd.features.end()); i += 1; }
        else if (c == 0x15) { add({1}); i += 2; }
        else if (c == 0x20 && i + 2 < n) {
            const unsigned players = data[i + 1], nbytes = data[i + 2];
            add({1, 0});
            for (unsigned pl = 0; pl < players; ++pl)
                for (unsigned k = 0; k < nbytes; ++k) out.push_back(0);
            i += 3;
        } else if (c == 0x21 && i + 1 < n) { add({1}); for (unsigned k = 0; k < data[i + 1]; ++k) add({0, 0}); i += 2; }
        else if (c == 0x22 && i + 1 < n) { add({1}); for (unsigned k = 0; k < data[i + 1]; ++k) add({0x80, 0x00}); i += 2; }
        else if (c == 0x23 && i + 1 < n) { add({1}); for (unsigned k = 0; k < data[i + 1]; ++k) add({0, 0}); i += 2; }
        else if (c == 0x24) { add({1, 0}); i += 1; }
        else if (c == 0x25 && i + 1 < n) { add({1, 0, 0, 0, 0}); i += 2; }
        else if (c == 0x26) { add({1, 0, 0}); i += 1; }
        else if ((c == 0x30 || c == 0x31) && i + 3 < n + 1) { add({1}); i += 4; }
        else if (c == 0x32 && i + 1 < n) { add({1}); i += 2 + data[i + 1]; }
        else if (c == 0x35 && i + 1 < n) { add({1}); i += 2 + data[i + 1]; }
        else { add({2}); break; }   // rapport 2: onbekende opdracht
    }
    return out;
}

}  // namespace ravengun::launch::jvspipe
