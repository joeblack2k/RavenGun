// SPDX-License-Identifier: GPL-3.0-or-later
#include "ravengun/gamesettings/controls_settings.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace ravengun::gamesettings {

const ControlSignalDef kControlSignals[] = {{"trigger", false}, {"reload", true}, {"grenade", true}, {"aux1", true},
                                            {"aux2", true},     {"start", true},  {"coin", true},    {"offscreen", false}};
const size_t kControlSignalCount = sizeof kControlSignals / sizeof kControlSignals[0];

bool control_signal_known(const std::string& name) {
    for (const auto& s : kControlSignals) if (name == s.name) return true;
    return false;
}

bool control_signal_assignable(const std::string& name) {
    for (const auto& s : kControlSignals) if (name == s.name) return s.assignable;
    return false;
}

std::string control_action(const std::string& signal, int player) {
    if (signal == "reload" || signal == "grenade" || signal == "aux1" || signal == "aux2") return signal;
    if (signal == "start") return "key:" + std::to_string(player >= 1 && player <= 4 ? player : 1);   // config::player_start_key
    if (signal == "coin") return "key:5";
    return {};
}

const std::vector<std::string>& control_buttons() {
    static const std::vector<std::string> b{"triangle", "circle", "cross", "square", "select", "start", "move"};
    return b;
}

const std::vector<std::string>& sharp_shooter_buttons() {
    static const std::vector<std::string> b{"pump", "ss_rl"};
    return b;
}

std::pair<std::string, std::string> split_label(const std::string& value) {
    auto trim = [](const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    };
    const size_t bar = value.find('|');
    if (bar == std::string::npos) return {trim(value), {}};
    return {trim(value.substr(0, bar)), trim(value.substr(bar + 1))};
}

namespace {

// "P2.Cross" -> (2, "cross"); false als de sleutel geen keuze is.
bool parse_choice_key(const std::string& key, int* player, std::string* button) {
    if (key.size() < 4 || (key[0] != 'P' && key[0] != 'p') || key[1] < '1' || key[1] > '4' || key[2] != '.') return false;
    *player = key[1] - '0';
    std::string b = key.substr(3);
    for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    *button = b;
    return true;
}

}  // namespace

ControlsInfo controls_info(const Ini& merged) {
    ControlsInfo out;
    const Section* sec = merged.section(kControlsSection);
    if (!sec) return out;
    for (const auto& kv : sec->kv) {
        int p;
        std::string b;
        if (kv.first == "Confirmed") { out.confirmed = truthy(kv.second); continue; }
        if (parse_choice_key(kv.first, &p, &b)) continue;
        if (kv.second.empty()) continue;   // een lege waarde haalt het signaal weg
        if (!control_signal_known(kv.first)) {
            out.warnings.push_back("[Controls] onbekend signaal '" + kv.first + "'");
            continue;
        }
        ControlSignal s;
        s.signal = kv.first;
        std::tie(s.label, s.explanation) = split_label(kv.second);
        if (s.label.empty()) continue;
        s.assignable = control_signal_assignable(s.signal);
        out.entries.push_back(std::move(s));
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> controls_overrides(const Ini& merged, int player, std::vector<std::string>* warnings) {
    std::vector<std::pair<std::string, std::string>> out;
    const Section* sec = merged.section(kControlsSection);
    if (!sec) return out;
    auto warn = [&](const std::string& m) { if (warnings) warnings->push_back(m); };
    for (const auto& kv : sec->kv) {
        int p;
        std::string b;
        if (!parse_choice_key(kv.first, &p, &b) || p != player || kv.second.empty()) continue;
        if (std::find(control_buttons().begin(), control_buttons().end(), b) == control_buttons().end() &&
            std::find(sharp_shooter_buttons().begin(), sharp_shooter_buttons().end(), b) == sharp_shooter_buttons().end()) {
            warn("[Controls] " + kv.first + ": onbekende of vaste knop '" + b + "'");
            continue;
        }
        std::string action;
        const std::string& v = kv.second;
        if (v == "none" || v == "recenter" || v.compare(0, 4, "key:") == 0) action = v;
        else if (control_signal_assignable(v)) action = control_action(v, player);
        else { warn("[Controls] " + kv.first + ": onbekend signaal '" + v + "'"); continue; }
        out.erase(std::remove_if(out.begin(), out.end(), [&](const auto& o) { return o.first == b; }), out.end());   // de laatste telt
        out.emplace_back(b, action);
    }
    return out;
}

bool controls_need_setup(const Ini& merged) {
    const ControlsInfo ci = controls_info(merged);
    if (ci.confirmed) return false;
    return std::any_of(ci.entries.begin(), ci.entries.end(), [](const ControlSignal& s) { return s.assignable; });
}

namespace {

// Alle P<n>.<knop>-sleutels van de gebruikerslaag.
std::vector<std::string> user_choice_keys(const Dirs& dirs, const std::string& profile) {
    std::vector<std::string> keys;
    const Ini u = read_layer(dirs.user, profile);
    if (const Section* s = u.section(kControlsSection))
        for (const auto& kv : s->kv) {
            int p;
            std::string b;
            if (parse_choice_key(kv.first, &p, &b)) keys.push_back(kv.first);
        }
    return keys;
}

}  // namespace

bool reset_user_controls(const Dirs& dirs, const std::string& profile, std::string* err) {
    for (const auto& k : user_choice_keys(dirs, profile))
        if (!erase_user_value(dirs, profile, kControlsSection, k, err)) return false;
    if (read_layer(dirs.user, profile).has(kControlsSection, "Confirmed"))
        return erase_user_value(dirs, profile, kControlsSection, "Confirmed", err);
    return true;
}

bool save_user_controls(const Dirs& dirs, const std::string& profile, const std::vector<std::vector<std::pair<std::string, std::string>>>& choices,
                        std::string* err) {
    for (const auto& k : user_choice_keys(dirs, profile))
        if (!erase_user_value(dirs, profile, kControlsSection, k, err)) return false;
    for (size_t i = 0; i < choices.size() && i < 4; ++i)
        for (const auto& c : choices[i]) {
            std::string b = c.first;
            if (!b.empty()) b[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(b[0])));
            if (!set_user_value(dirs, profile, kControlsSection, "P" + std::to_string(i + 1) + "." + b, c.second, err)) return false;
        }
    return set_user_value(dirs, profile, kControlsSection, "Confirmed", "true", err);
}

}  // namespace ravengun::gamesettings
