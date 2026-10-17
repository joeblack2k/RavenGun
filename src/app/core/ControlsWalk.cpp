// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ControlsWalk.h"

#include <algorithm>

namespace rg {

namespace gs = ravengun::gamesettings;

ControlsWalk::ControlsWalk(const gs::ControlsInfo& info, std::map<std::string, std::string> base, int player)
    : steps_(info.entries), base_(std::move(base)), player_(player) {}

bool ControlsWalk::buttonTaken(const std::string& button) const {
    for (const auto& kv : picks_) if (kv.second == button) return true;
    return false;
}

ControlsWalk::Press ControlsWalk::press(const std::string& button) {
    if (done()) return Press::Ignored;
    const gs::ControlSignal& s = step();
    if (!s.assignable) {   // uitleg: elke knop gaat verder
        ++index_;
        return Press::Advanced;
    }
    if (button == "ps") {
        keepDefault();
        return Press::Kept;
    }
    const auto& ok = gs::control_buttons();
    if (std::find(ok.begin(), ok.end(), button) == ok.end()) return Press::Ignored;
    if (buttonTaken(button)) return Press::Duplicate;
    picks_[s.signal] = button;
    ++index_;
    return Press::Picked;
}

void ControlsWalk::keepDefault() {
    if (done()) return;
    picks_.erase(step().signal);
    ++index_;
}

void ControlsWalk::back() {
    if (index_ == 0) return;
    --index_;
    picks_.erase(step().signal);
}

void ControlsWalk::restart() {
    index_ = 0;
    picks_.clear();
}

std::vector<std::string> ControlsWalk::defaultButtons(const std::string& signal) const {
    std::vector<std::string> out;
    const std::string action = gs::control_action(signal, player_);
    if (action.empty()) return out;
    for (const auto& b : gs::control_buttons()) {
        auto it = base_.find(b);
        if (it != base_.end() && it->second == action) out.push_back(b);
    }
    return out;
}

std::string ControlsWalk::picked(const std::string& signal) const {
    auto it = picks_.find(signal);
    return it == picks_.end() ? std::string() : it->second;
}

std::vector<ControlsWalk::Line> ControlsWalk::summary() const {
    std::vector<Line> out;
    for (const auto& s : steps_) {
        Line l;
        l.signal = s.signal;
        l.label = s.label;
        l.assignable = s.assignable;
        if (s.assignable) {
            const std::string p = picked(s.signal);
            if (!p.empty()) {
                l.picked = true;
                l.buttons = {p};
            } else {
                for (const auto& b : defaultButtons(s.signal)) if (!buttonTaken(b)) l.buttons.push_back(b);
            }
        }
        out.push_back(std::move(l));
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> ControlsWalk::choices() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& s : steps_) {
        const std::string p = picked(s.signal);
        if (p.empty()) continue;
        out.emplace_back(p, s.signal);
        for (const auto& d : defaultButtons(s.signal)) {
            if (d == p || buttonTaken(d)) continue;
            out.emplace_back(d, "none");   // de oude knop van dit signaal is vrij
        }
    }
    // Een knop kan maar een keer voorkomen; een keuze gaat voor een "none".
    std::vector<std::pair<std::string, std::string>> uniq;
    for (const auto& c : out) {
        auto it = std::find_if(uniq.begin(), uniq.end(), [&](const auto& u) { return u.first == c.first; });
        if (it == uniq.end()) uniq.push_back(c);
        else if (it->second == "none") it->second = c.second;
    }
    return uniq;
}

std::vector<std::string> ControlsWalk::unbound() const {
    std::vector<std::string> out;
    for (const auto& l : summary()) if (l.assignable && l.buttons.empty()) out.push_back(l.label);
    return out;
}

std::string ControlsWalk::buttonName(const std::string& b) {
    static const std::map<std::string, std::string> n{{"triangle", "Triangle"}, {"circle", "Circle"}, {"cross", "Cross"}, {"square", "Square"},
                                                      {"select", "Select"},     {"start", "Start"},   {"move", "Move (big button)"}, {"ps", "PS"},
                                                      {"t", "Trigger"}};
    auto it = n.find(b);
    return it == n.end() ? b : it->second;
}

}  // namespace rg
