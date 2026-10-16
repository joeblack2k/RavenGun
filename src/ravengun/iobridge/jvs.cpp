#include "ravengun/iobridge/jvs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <tuple>

#include "ravengun/json_parse.hpp"

namespace ravengun::iobridge {

namespace json = ravengun::json;

// ---------------------------------------------------------------- framing

uint8_t jvs_checksum(const uint8_t* p, size_t n) {
    unsigned s = 0;
    for (size_t i = 0; i < n; ++i) s += p[i];
    return static_cast<uint8_t>(s & 0xFF);
}

std::vector<uint8_t> jvs_escape(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    out.reserve(raw.size() + 4);
    for (uint8_t b : raw) {
        if (b == kJvsSync) { out.push_back(kJvsEscape); out.push_back(0xDF); }
        else if (b == kJvsEscape) { out.push_back(kJvsEscape); out.push_back(0xCF); }
        else out.push_back(b);
    }
    return out;
}

std::vector<uint8_t> jvs_encode(uint8_t node, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> body;
    body.reserve(data.size() + 3);
    body.push_back(node);
    body.push_back(static_cast<uint8_t>(data.size() + 1));   // lengte = gegevens + checksum
    body.insert(body.end(), data.begin(), data.end());
    body.push_back(jvs_checksum(body));
    std::vector<uint8_t> wire;
    wire.push_back(kJvsSync);
    const std::vector<uint8_t> esc = jvs_escape(body);
    wire.insert(wire.end(), esc.begin(), esc.end());
    return wire;
}

void JvsFrameParser::reset() {
    st_ = St::Idle;
    esc_ = false;
    body_.clear();
}

void JvsFrameParser::feed(const uint8_t* p, size_t n, std::vector<JvsFrame>& out) {
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = p[i];
        if (b == kJvsSync) {   // een ongestuft sync begint altijd een nieuw packet
            if (st_ != St::Idle) ++resyncs_;
            st_ = St::Node;
            esc_ = false;
            body_.clear();
            continue;
        }
        if (st_ == St::Idle) { ++garbage_; continue; }
        if (esc_) { b = static_cast<uint8_t>(b + 1); esc_ = false; }   // 0xD0 0xCF -> 0xD0, 0xD0 0xDF -> 0xE0
        else if (b == kJvsEscape) { esc_ = true; continue; }
        switch (st_) {
            case St::Node: node_ = b; st_ = St::Len; break;
            case St::Len:
                len_ = b;
                body_.clear();
                if (len_ == 0) { st_ = St::Idle; ++garbage_; }   // een packet heeft minstens een checksum
                else st_ = St::Body;
                break;
            case St::Body:
                body_.push_back(b);
                if (body_.size() == len_) {
                    JvsFrame f;
                    f.node = node_;
                    f.checksum_wire = body_.back();
                    body_.pop_back();
                    f.data = body_;
                    unsigned s = node_ + len_;
                    for (uint8_t d : f.data) s += d;
                    f.checksum_calc = static_cast<uint8_t>(s & 0xFF);
                    f.checksum_ok = f.checksum_calc == f.checksum_wire;
                    out.push_back(std::move(f));
                    st_ = St::Idle;
                    body_.clear();
                }
                break;
            case St::Idle: break;
        }
    }
}

// ---------------------------------------------------------------- beschrijving

const JvsBoard* JvsBoards::find(const std::string& name) const {
    for (const auto& b : boards)
        if (b.name == name) return &b;
    return nullptr;
}

int jvs_switch_bytes(const JvsBoard& b) { return std::max(1, (b.buttons + 7) / 8); }

uint16_t jvs_analog_value(const JvsAnalogChannel& c, const IoState& s) {
    double t = c.idle;
    if (!c.signal.empty()) {
        // Alleen een levende bron telt (io_axis_live: aim of gamepad); zonder bron (Move weg, niet gewapend) blijft de as op idle.
        const bool valid = s.armed && io_axis_live(s, c.signal);
        double v = 0;
        if (valid && io_axis_signal(s, c.signal, v)) t = c.invert ? 1.0 - v : v;
    }
    t = std::max(0.0, std::min(1.0, t));
    double mn = c.min, mx = c.max;
    if (mn >= 0 && mn <= 255 && mx >= 0 && mx <= 255) { mn *= 257.0; mx *= 257.0; }   // 8 -> 16 bit (255 * 257 = 65535)
    const double v = mn + t * (mx - mn);
    return static_cast<uint16_t>(std::lround(std::max(0.0, std::min(65535.0, v))));
}

std::vector<uint8_t> jvs_switch_report(const JvsBoard& b, const IoState& s, int players, int bytes) {
    std::vector<uint8_t> out(static_cast<size_t>(1 + std::max(0, players) * std::max(0, bytes)), 0);
    for (const auto& sw : b.switches) {
        bool v = false;
        if (!io_button_signal(s, sw.signal, v) || !v) continue;
        size_t idx;
        if (sw.player == 0) {
            if (sw.byte != 0) continue;
            idx = 0;
        } else {
            if (sw.player > players || sw.byte >= bytes) continue;
            idx = 1 + static_cast<size_t>(sw.player - 1) * static_cast<size_t>(bytes) + static_cast<size_t>(sw.byte);
        }
        out[idx] = static_cast<uint8_t>(out[idx] | (1u << sw.bit));
    }
    return out;
}

namespace {

std::string str_or(const json::Value& o, const char* key, const std::string& def) {
    const json::Value* v = o.find(key);
    return v && v->is_string() ? v->str : def;
}

// Een getal of een tekst als "0x30".
bool int_field(const json::Value& o, const char* key, int& out) {
    const json::Value* v = o.find(key);
    if (!v) return true;
    if (v->is_number()) { out = static_cast<int>(std::lround(v->num)); return true; }
    if (v->is_string() && !v->str.empty()) {
        char* end = nullptr;
        long x = std::strtol(v->str.c_str(), &end, 0);
        if (end && *end == 0) { out = static_cast<int>(x); return true; }
    }
    return false;
}

}  // namespace

std::string validate_jvs_board(const JvsBoard& b) {
    const std::string pre = "jvs-boards.json: " + b.name + ": ";
    if (b.name.empty()) return "jvs-boards.json: een bord heeft geen naam";
    if (b.identify.empty() || b.identify.size() > 200) return pre + "'identify' is leeg of te lang";
    if (b.identify.find('\0') != std::string::npos) return pre + "'identify' bevat een nulbyte";
    for (int v : {b.comm_version, b.jvs_version, b.command_rev})
        if (v < 0 || v > 255) return pre + "een versie buiten 0..255";
    if (b.players < 0 || b.players > 4) return pre + "'players' buiten 0..4";
    if (b.buttons < 0 || b.buttons > 32) return pre + "'buttons' buiten 0..32";
    if (b.coin_slots < 0 || b.coin_slots > 8) return pre + "'coin_slots' buiten 0..8";
    if (b.analog_channels < 0 || b.analog_channels > kJvsMaxAnalogChannels) return pre + "'analog_channels' buiten 0..32";
    if (b.analog_bits < 0 || b.analog_bits > 16) return pre + "'analog_bits' buiten 0..16";
    if (b.gp_outputs < 0 || b.gp_outputs > 255) return pre + "'gp_outputs' buiten 0..255";
    if (static_cast<int>(b.analog.size()) > b.analog_channels) return pre + "meer 'analog'-kanalen dan 'analog_channels'";
    for (const auto& a : b.analog) {
        if (!a.signal.empty() && !valid_axis_signal(a.signal)) return pre + "onbekend as-signaal '" + a.signal + "'";
        if (a.min < 0 || a.max > 65535 || a.min == a.max) return pre + "ongeldig kalibratievenster " + std::to_string(a.min) + ".." + std::to_string(a.max);
        if (a.idle < 0 || a.idle > 1) return pre + "'idle' moet 0..1 zijn";
    }
    const int bytes = jvs_switch_bytes(b);
    std::set<std::tuple<int, int, int>> used;
    for (const auto& s : b.switches) {
        if (!valid_button_signal(s.signal)) return pre + "onbekend knop-signaal '" + s.signal + "'";
        if (s.player < 0 || s.player > b.players) return pre + "schakelbit '" + s.signal + "': speler buiten 0..players";
        if (s.bit < 0 || s.bit > 7) return pre + "schakelbit '" + s.signal + "': 'bit' buiten 0..7";
        if (s.player == 0 ? s.byte != 0 : (s.byte < 0 || s.byte >= bytes)) return pre + "schakelbit '" + s.signal + "': 'byte' buiten het bereik";
        if (!used.insert({s.player, s.byte, s.bit}).second) return pre + "schakelbit speler " + std::to_string(s.player) + " byte " + std::to_string(s.byte) + " bit " + std::to_string(s.bit) + " staat twee keer in de lijst";
    }
    if (static_cast<int>(b.coins.size()) > b.coin_slots) return pre + "meer 'coins' dan 'coin_slots'";
    for (const auto& c : b.coins)
        if (!valid_button_signal(c)) return pre + "onbekend munt-signaal '" + c + "'";
    return {};
}

JvsBoards parse_jvs_boards(const std::string& text, std::string& err) {
    JvsBoards out;
    err.clear();
    auto pr = json::parse(text);
    if (!pr.ok || !pr.value.is_object()) {
        err = "jvs-boards.json is geen geldig JSON-object" + (pr.error.empty() ? std::string() : ": " + pr.error);
        return {};
    }
    const json::Value* boards = pr.value.find("boards");
    if (!boards || !boards->is_object()) { err = "jvs-boards.json: 'boards' ontbreekt"; return {}; }
    for (const auto& kv : boards->obj) {
        const json::Value& o = kv.second;
        if (!o.is_object()) { err = "jvs-boards.json: " + kv.first + " is geen object"; return {}; }
        JvsBoard b;
        b.name = kv.first;
        b.description = str_or(o, "description", "");
        b.identify = str_or(o, "identify", b.identify);
        if (const json::Value* c = o.find("complete"); c && c->is_bool()) b.complete = c->b;
        if (const json::Value* c = o.find("namco"); c && c->is_bool()) b.namco = c->b;
        if (const json::Value* c = o.find("taito"); c && c->is_bool()) b.taito = c->b;
        bool ok = int_field(o, "comm_version", b.comm_version) && int_field(o, "jvs_version", b.jvs_version) &&
                  int_field(o, "command_rev", b.command_rev) && int_field(o, "players", b.players) && int_field(o, "buttons", b.buttons) &&
                  int_field(o, "coin_slots", b.coin_slots) && int_field(o, "analog_channels", b.analog_channels) &&
                  int_field(o, "analog_bits", b.analog_bits) && int_field(o, "gp_outputs", b.gp_outputs);
        if (const json::Value* an = o.find("analog")) {
            if (!an->is_array()) ok = false;
            else for (const auto& a : an->arr) {
                if (!a.is_object()) { ok = false; break; }
                JvsAnalogChannel c;
                c.signal = str_or(a, "signal", "");
                if (const json::Value* i = a.find("invert"); i && i->is_bool()) c.invert = i->b;
                ok = ok && int_field(a, "min", c.min) && int_field(a, "max", c.max);
                if (const json::Value* i = a.find("idle"); i && i->is_number()) c.idle = i->num;
                b.analog.push_back(std::move(c));
            }
        }
        if (const json::Value* sw = o.find("switches")) {
            if (!sw->is_array()) ok = false;
            else for (const auto& a : sw->arr) {
                if (!a.is_object()) { ok = false; break; }
                JvsSwitchBit s;
                s.signal = str_or(a, "signal", "");
                ok = ok && int_field(a, "player", s.player) && int_field(a, "byte", s.byte) && int_field(a, "bit", s.bit);
                b.switches.push_back(std::move(s));
            }
        }
        if (const json::Value* cs = o.find("coins")) {
            if (!cs->is_array()) ok = false;
            else for (const auto& c : cs->arr) {
                if (!c.is_string()) { ok = false; break; }
                b.coins.push_back(c.str);
            }
        } else {
            for (int i = 1; i <= std::min(b.coin_slots, 4); ++i) b.coins.push_back("coin" + std::to_string(i));
        }
        if (!ok) { err = "jvs-boards.json: " + kv.first + ": een veld heeft het verkeerde type"; return {}; }
        std::string v = validate_jvs_board(b);
        if (!v.empty()) { err = v; return {}; }
        out.boards.push_back(std::move(b));
    }
    return out;
}

// ---------------------------------------------------------------- node

void JvsNode::set_input(const IoState& s) {
    in_ = s;
    for (size_t i = 0; i < board_.coins.size() && i < coin_count_.size(); ++i) {
        bool v = false;
        io_button_signal(s, board_.coins[i], v);
        if (v && !coin_prev_[i]) {
            coin_count_[i] = static_cast<uint16_t>((coin_count_[i] + 1) & 0x3FFF);   // 14 bits
            ++stats_.coin_edges;
        }
        coin_prev_[i] = v;
    }
}

namespace {

// Aantal argumentbytes van een commando, of -1 als het commando onbekend is. `p` wijst naar het eerste argument (kan op het einde
// van de gegevens staan: dan geeft de functie -2 voor "te weinig bytes").
int arg_length(const std::vector<uint8_t>& d, size_t pos, uint8_t cmd) {
    auto remaining = d.size() - pos;
    switch (cmd) {
        case kCmdReset: return 1;
        case kCmdSetAddress: return 1;
        case kCmdIdentify: case kCmdCommandRev: case kCmdJvsVersion: case kCmdCommVersion: case kCmdFeatures: return 0;
        case kCmdMainId: {   // tekst met afsluitende nul
            size_t n = 0;
            while (n < remaining && d[pos + n] != 0) ++n;
            return n < remaining ? static_cast<int>(n + 1) : -2;
        }
        case kCmdSwitches: return 2;
        case kCmdCoins: case kCmdAnalog: case kCmdRotary: case kCmdScreenPos: case kCmdMiscSwitches: return 1;
        case kCmdKeycode: return 0;
        case kCmdHopperStatus: return 1;
        case kCmdRetransmit: return 0;
        case kCmdCoinSubtract: case kCmdPayout: case kCmdCoinAdd: case kCmdPayoutSubtract: return 3;
        case kCmdGpOut1: case kCmdCharOut: return remaining >= 1 ? 1 + d[pos] : -2;
        case kCmdAnalogOut: return remaining >= 1 ? 1 + 2 * d[pos] : -2;
        case kCmdGpOut2: return 2;
        default: return -1;
    }
}

}  // namespace

size_t jvs_namco_command_length(const std::vector<uint8_t>& d, size_t pos) {
    if (pos >= d.size()) return 0;
    const size_t rest = d.size() - pos;
    auto clamp = [&](size_t n) { return std::min(n, rest); };
    const uint8_t cmd = d[pos];
    if (cmd >= 0x78 && cmd <= 0x80) return clamp(15);
    if (cmd != 0x70) return 0;
    if (rest < 2) return rest;
    switch (d[pos + 1]) {
        case 0x03: case 0x15: case 0x16: return clamp(4);
        case 0x05: {   // de derde byte is de lengte van het hele commando (minstens commando, sub en die lengtebyte zelf)
            if (rest < 3) return rest;
            return clamp(std::max<size_t>(3, d[pos + 2]));
        }
        default: return rest;   // 0x18 en onbekende subcommando's: de rest van het packet
    }
}

std::vector<uint8_t> JvsNode::handle(const std::vector<uint8_t>& d, bool broadcast, bool can_take_address) {
    std::vector<uint8_t> rep;   // rapporten (zonder status)
    bool answer = !broadcast;   // een broadcast krijgt alleen antwoord van de node die het adres neemt
    uint8_t status = kJvsStatusOk;
    ++stats_.requests;
    size_t pos = 0;
    while (pos < d.size()) {
        if (board_.namco) {
            // De eigen commando's van een Namco-hoofdbord (zie JvsBoard::namco): lezen, kort beantwoorden, verder.
            if (const size_t nl = jvs_namco_command_length(d, pos)) {
                const uint8_t c = d[pos];
                ++stats_.commands[c];
                rep.push_back(kJvsReportOk);
                if (c == 0x70 && nl >= 2) {
                    const uint8_t sub = d[pos + 1];
                    if (sub == 0x03) rep.push_back(0x00);
                    else if (sub == 0x05 || sub == 0x15 || sub == 0x16 || sub == 0x18) rep.push_back(0x01);
                }
                pos += nl;
                continue;
            }
        }
        const uint8_t cmd = d[pos++];
        if (board_.taito && cmd >= 0x01 && cmd <= 0x05 && cmd != 0x02) {
            // De eigen commando's van een Taito-bord (zie JvsBoard::taito).
            const size_t tl = cmd == 0x05 ? 2 : (cmd == 0x04 ? 0 : 1);
            if (pos + tl > d.size()) { rep.push_back(kJvsReportBadParamCount); break; }
            pos += tl;
            ++stats_.commands[cmd];
            rep.push_back(kJvsReportOk);
            if (cmd == 0x01) { rep.push_back(0x01); rep.push_back(0x01); }
            else if (cmd == 0x03) rep.push_back(0x01);
            continue;
        }
        if (board_.taito && cmd >= 0x78 && cmd <= 0x80) {
            // Zoals bij een Namco-bord: vaste lengte van 15 bytes (commando plus 14, of de rest van het packet), rapport 01.
            pos = std::min(d.size(), pos + 14);
            ++stats_.commands[cmd];
            rep.push_back(kJvsReportOk);
            continue;
        }
        const int al = arg_length(d, pos, cmd);
        if (al == -1 && board_.namco) { ++stats_.unknown; continue; }   // Namco: een onbekende byte overslaan, zonder antwoord
        if (al == -1) { status = kJvsStatusUnknownCmd; ++stats_.unknown; break; }
        if (al == -2 || pos + static_cast<size_t>(al) > d.size()) { rep.push_back(kJvsReportBadParamCount); break; }
        const uint8_t* a = d.data() + pos;
        pos += static_cast<size_t>(al);
        ++stats_.commands[cmd];
        switch (cmd) {
            case kCmdReset:
                if (a[0] == 0xD9) { addr_ = 0; answer = false; }
                else { rep.push_back(kJvsReportBadParamData); }
                break;
            case kCmdSetAddress:
                if (addr_ == 0 && can_take_address && a[0] >= 1 && a[0] <= 0x1F) { addr_ = a[0]; answer = true; rep.push_back(kJvsReportOk); }
                else if (!broadcast) rep.push_back(kJvsReportOk);
                break;
            case kCmdIdentify:
                rep.push_back(kJvsReportOk);
                rep.insert(rep.end(), board_.identify.begin(), board_.identify.end());
                rep.push_back(0);
                break;
            case kCmdCommandRev: rep.push_back(kJvsReportOk); rep.push_back(static_cast<uint8_t>(board_.command_rev)); break;
            case kCmdJvsVersion: rep.push_back(kJvsReportOk); rep.push_back(static_cast<uint8_t>(board_.jvs_version)); break;
            case kCmdCommVersion: rep.push_back(kJvsReportOk); rep.push_back(static_cast<uint8_t>(board_.comm_version)); break;
            case kCmdFeatures: {
                rep.push_back(kJvsReportOk);
                auto feat = [&](uint8_t code, int p1, int p2, int p3) {
                    rep.push_back(code);
                    rep.push_back(static_cast<uint8_t>(p1));
                    rep.push_back(static_cast<uint8_t>(p2));
                    rep.push_back(static_cast<uint8_t>(p3));
                };
                if (board_.players > 0) feat(0x01, board_.players, board_.buttons, 0);
                if (board_.coin_slots > 0) feat(0x02, board_.coin_slots, 0, 0);
                if (board_.analog_channels > 0) feat(0x03, board_.analog_channels, board_.analog_bits, 0);
                if (board_.gp_outputs > 0) feat(0x12, board_.gp_outputs, 0, 0);
                rep.push_back(0x00);
                break;
            }
            case kCmdMainId:
                stats_.main_id.assign(reinterpret_cast<const char*>(a), static_cast<size_t>(al - 1));
                rep.push_back(kJvsReportOk);
                break;
            case kCmdSwitches: {
                const int players = a[0], bytes = a[1];
                if (players > board_.players || bytes > 8 || players * bytes > 64) { rep.push_back(kJvsReportBadParamData); break; }
                rep.push_back(kJvsReportOk);
                const std::vector<uint8_t> r = jvs_switch_report(board_, in_, players, bytes);
                rep.insert(rep.end(), r.begin(), r.end());
                break;
            }
            case kCmdCoins: {
                const int slots = a[0];
                if (slots > board_.coin_slots) { rep.push_back(kJvsReportBadParamData); break; }
                rep.push_back(kJvsReportOk);
                for (int i = 0; i < slots; ++i) {
                    const uint16_t c = coin_count_[static_cast<size_t>(i)];
                    rep.push_back(static_cast<uint8_t>((c >> 8) & 0x3F));   // bovenste twee bits: toestand 00 = normaal
                    rep.push_back(static_cast<uint8_t>(c & 0xFF));
                }
                break;
            }
            case kCmdAnalog: {
                // Zoveel kanalen als de game vraagt, ook boven het eigen aantal (zoals TeknoParrot): Gaia Attack 4 vraagt er 10 aan
                // een bord met 8, en een foutrapport geeft "I/O ERROR". Kanalen zonder bron staan in rust.
                const int ch = a[0];
                if (ch > kJvsMaxAnalogChannels) { rep.push_back(kJvsReportBadParamData); break; }
                rep.push_back(kJvsReportOk);
                for (int i = 0; i < ch; ++i) {
                    JvsAnalogChannel c;
                    if (static_cast<size_t>(i) < board_.analog.size()) c = board_.analog[static_cast<size_t>(i)];
                    else c.min = 0, c.max = 255;
                    const uint16_t v = jvs_analog_value(c, in_);
                    rep.push_back(static_cast<uint8_t>(v >> 8));   // 16 bit, hoogste byte eerst
                    rep.push_back(static_cast<uint8_t>(v & 0xFF));
                }
                break;
            }
            case kCmdRotary: {   // geen roterende ingangen: nullen voor het gevraagde aantal
                rep.push_back(kJvsReportOk);
                for (int i = 0; i < a[0]; ++i) { rep.push_back(0); rep.push_back(0); }
                break;
            }
            case kCmdKeycode: rep.push_back(kJvsReportOk); rep.push_back(0); break;
            case kCmdScreenPos: rep.push_back(kJvsReportOk); for (int i = 0; i < 4; ++i) rep.push_back(0); break;
            case kCmdMiscSwitches: rep.push_back(kJvsReportOk); for (int i = 0; i < a[0]; ++i) rep.push_back(0); break;
            case kCmdHopperStatus: rep.push_back(kJvsReportOk); rep.push_back(0); rep.push_back(0); rep.push_back(0); rep.push_back(0); break;
            case kCmdCoinSubtract: {
                const size_t slot = a[0] == 0 ? 0 : static_cast<size_t>(a[0] - 1);
                const uint16_t n = static_cast<uint16_t>((a[1] << 8) | a[2]);
                if (a[0] >= 1 && slot < coin_count_.size()) {
                    coin_count_[slot] = static_cast<uint16_t>((coin_count_[slot] >= n ? coin_count_[slot] - n : 0) & 0x3FFF);
                    rep.push_back(kJvsReportOk);
                } else rep.push_back(kJvsReportBadParamData);
                break;
            }
            case kCmdCoinAdd: {
                const size_t slot = a[0] == 0 ? 0 : static_cast<size_t>(a[0] - 1);
                const uint16_t n = static_cast<uint16_t>((a[1] << 8) | a[2]);
                if (a[0] >= 1 && slot < coin_count_.size()) {
                    coin_count_[slot] = static_cast<uint16_t>((coin_count_[slot] + n) & 0x3FFF);
                    rep.push_back(kJvsReportOk);
                } else rep.push_back(kJvsReportBadParamData);
                break;
            }
            case kCmdPayout: case kCmdPayoutSubtract: rep.push_back(kJvsReportOk); break;   // geen hopper: stil aanvaarden
            case kCmdGpOut1:
                stats_.gp_out.assign(a + 1, a + al);
                rep.push_back(kJvsReportOk);
                break;
            case kCmdGpOut2:
                if (stats_.gp_out.size() <= a[0]) stats_.gp_out.resize(static_cast<size_t>(a[0]) + 1, 0);
                stats_.gp_out[a[0]] = a[1];
                rep.push_back(kJvsReportOk);
                break;
            case kCmdAnalogOut: case kCmdCharOut: rep.push_back(kJvsReportOk); break;
            default: break;
        }
    }
    if (!answer) return {};
    std::vector<uint8_t> out;
    out.push_back(status);
    out.insert(out.end(), rep.begin(), rep.end());
    return out;
}

// ---------------------------------------------------------------- bus

bool JvsBus::any_addressed() const {
    for (const auto& n : nodes_)
        if (n.address() != 0) return true;
    return false;
}

bool JvsBus::all_addressed() const {
    for (const auto& n : nodes_)
        if (n.address() == 0) return false;
    return !nodes_.empty();
}

std::vector<uint8_t> JvsBus::process(const JvsFrame& f) {
    ++stats_.frames;
    if (!f.checksum_ok) {
        ++stats_.bad_sum;
        // Een ontvanger met een checksumfout antwoordt met status 3 als het packet voor hem (of voor iedereen) bedoeld was; bij een
        // broadcast zwijgt hij (anders antwoorden alle nodes tegelijk).
        if (f.node == kJvsBroadcast) return {};
        for (const auto& n : nodes_)
            if (n.address() == f.node) return jvs_encode(kJvsHostAddr, {kJvsStatusSumError});
        return {};
    }
    if (f.data.empty()) return {};
    stats_.last_cmd = f.data[0];
    const bool broadcast = f.node == kJvsBroadcast;
    // 0x2F: herhaal het laatste antwoord (alleen als het het enige commando is).
    if (f.data.size() == 1 && f.data[0] == kCmdRetransmit) {
        if (!broadcast) return last_wire_;
        return {};
    }
    if (f.data[0] == kCmdReset) ++stats_.resets;
    std::vector<uint8_t> payload;
    bool sense = true;   // de sense-lijn staat bij de eerste node zonder adres, als alle voorgaande al een adres hebben
    for (auto& n : nodes_) {
        // Een Taito-bord neemt ook node 0 (zie JvsBoard::taito), zodra het een adres heeft.
        const bool for_me = broadcast || (n.address() != 0 && (n.address() == f.node || (f.node == kJvsHostAddr && n.board().taito)));
        if (for_me) {
            std::vector<uint8_t> r = n.handle(f.data, broadcast, sense);
            if (!r.empty()) { payload = std::move(r); break; }
        }
        if (n.address() == 0) sense = false;   // dit bord heeft nog geen adres: de volgende ziet de sense-lijn nog niet
    }
    if (payload.empty()) { ++stats_.unanswered; return {}; }
    ++stats_.answers;
    last_wire_ = jvs_encode(kJvsHostAddr, payload);
    return last_wire_;
}

}  // namespace ravengun::iobridge
