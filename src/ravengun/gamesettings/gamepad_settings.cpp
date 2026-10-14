// SPDX-License-Identifier: GPL-3.0-or-later
#include "ravengun/gamesettings/gamepad_settings.hpp"

namespace ravengun::gamesettings {

namespace {

// De sleutel in een sectie die deze bediening noemt (hoofdletters maken niet uit); nullptr als geen.
const std::pair<std::string, std::string>* find_control(const Section* s, output::PadControl c) {
    if (!s) return nullptr;
    const std::pair<std::string, std::string>* hit = nullptr;
    for (const auto& kv : s->kv) {
        output::PadControl k;
        if (output::pad_control_from_name(kv.first, k) && k == c) hit = &kv;   // de laatste telt, net als bij het samenvoegen
    }
    return hit;
}

}  // namespace

GamepadMapping gamepad_mapping(const Ini& merged, const Ini& user_layer) {
    GamepadMapping out;
    const Section* sec = merged.section(kGamepadSection);
    const Section* user = user_layer.section(kGamepadSection);
    out.has_section = sec != nullptr && !sec->kv.empty();
    out.map = output::gamepad_map_from(sec ? sec->kv : std::vector<std::pair<std::string, std::string>>{}, &out.warnings);
    const output::GamepadMap def = output::default_gamepad_map();
    for (int i = 0; i < output::kPadControls; ++i) {
        const auto c = static_cast<output::PadControl>(i);
        const output::PadTarget& t = out.map.at(c);
        // Zonder doel alleen tonen als een laag een bediening van de standaardindeling uitzet ("LT = none").
        if (t.kind == output::PadTargetKind::None && (find_control(sec, c) == nullptr || def.at(c).kind == output::PadTargetKind::None)) continue;
        GamepadRow r;
        r.control = output::pad_control_name(c);
        r.target = output::pad_target_name(t);
        // Herkomst: een geldige sleutel in de gebruikerslaag, anders een geldige sleutel in een andere laag, anders de standaard.
        auto valid = [&](const std::pair<std::string, std::string>* kv) {
            output::PadTarget pt;
            std::string err;
            return kv && !kv->second.empty() && output::parse_pad_target(kv->second, pt, err) && output::pad_target_allowed(c, pt);
        };
        if (valid(find_control(user, c))) r.source = "user";
        else if (valid(find_control(sec, c))) r.source = "shipped";
        else r.source = "default";
        out.rows.push_back(std::move(r));
    }
    return out;
}

GamepadMapping gamepad_mapping_for(const Dirs& dirs, const std::string& profile) {
    if (profile.empty()) return gamepad_mapping(Ini{}, Ini{});
    const Game g = load_game(dirs, profile);
    return gamepad_mapping(g.settings, read_layer(dirs.user, profile));
}

}  // namespace ravengun::gamesettings
