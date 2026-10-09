#include "ravengun/pairing/pairing.hpp"

#include <cctype>
#include <cstdio>

#include "ravengun/config/settings.hpp"

namespace ravengun::pairing {

bool parse_address(const std::string& a, uint8_t out[6]) {
    const std::string n = config::normalize_address(a);
    if (n.empty()) return false;
    for (int i = 0; i < 6; ++i) {
        unsigned v = 0;
        if (std::sscanf(n.c_str() + i * 3, "%2x", &v) != 1) return false;
        out[i] = static_cast<uint8_t>(v);
    }
    return true;
}

std::string format_address(const uint8_t b[6]) {
    char s[24];
    std::snprintf(s, sizeof s, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
    return s;
}

std::vector<uint8_t> encode_move_set_master(const std::string& host) {
    uint8_t a[6];
    if (!parse_address(host, a)) return {};
    std::vector<uint8_t> buf(kMoveSetSize, 0);
    buf[0] = kMoveSetMaster;
    for (int i = 0; i < 6; ++i) buf[static_cast<size_t>(1 + i)] = a[5 - i];  // laagste byte eerst (PSMoveAPI _psmove_btaddr_from_string)
    return buf;
}

bool decode_move_addresses(const uint8_t* buf, size_t n, std::string& own, std::string& master) {
    if (!buf || n < kMoveGetSize || buf[0] != kMoveGetAddr) return false;
    uint8_t o[6], m[6];
    for (int i = 0; i < 6; ++i) { o[i] = buf[6 - i]; m[i] = buf[15 - i]; }
    own = format_address(o);
    master = format_address(m);
    return true;
}

std::vector<uint8_t> encode_nav_set_master(const std::string& host) {
    uint8_t a[6];
    if (!parse_address(host, a)) return {};
    std::vector<uint8_t> buf(kNavMasterSize, 0);
    buf[0] = kNavMaster;
    buf[1] = 0x01;
    for (int i = 0; i < 6; ++i) buf[static_cast<size_t>(2 + i)] = a[i];  // leesvolgorde (BlueZ sixaxis: baswap van bdaddr_t)
    return buf;
}

bool decode_nav_master(const uint8_t* buf, size_t n, std::string& master) {
    if (!buf || n < kNavMasterSize || buf[0] != kNavMaster) return false;
    master = format_address(buf + 2);
    return true;
}

bool decode_nav_own(const uint8_t* buf, size_t n, std::string& own) {
    if (!buf || n < 10 || buf[0] != kNavOwnAddr) return false;
    own = format_address(buf + 4);
    return true;
}

std::string helper_type(const std::string& type, const std::string& model) {
    if (type == "navigation") return "navigation";
    if (type == "move") return model == "ZCM2" ? "move2" : "move";
    return {};
}

std::vector<std::string> helper_argv(const std::string& action, const std::string& address, const std::string& htype,
                                     const std::string& helper_path) {
    if (action != "register" && action != "unregister") return {};
    const std::string n = config::normalize_address(address);
    if (n.empty()) return {};
    std::string up = n;
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::vector<std::string> v{"/usr/bin/sudo", "-n", helper_path, action, up};
    if (action == "register") {
        if (htype != "move" && htype != "move2" && htype != "navigation") return {};
        v.push_back(htype);
    } else if (!htype.empty()) {
        return {};
    }
    return v;
}

const char* to_string(State s) {
    switch (s) {
        case State::Idle: return "idle";
        case State::WaitUsb: return "wait_usb";
        case State::AlreadyPaired: return "already_paired";
        case State::SetMaster: return "set_master";
        case State::NeedHelper: return "need_helper";
        case State::Register: return "register";
        case State::WaitUnplug: return "wait_unplug";
        case State::WaitBluetooth: return "wait_bluetooth";
        case State::Done: return "done";
        case State::Failed: return "failed";
        case State::Cancelled: return "cancelled";
    }
    return "?";
}

void Machine::go(State s, int64_t now, std::string msg) {
    state_ = s;
    since_ = now;
    pending_ = false;
    message_ = std::move(msg);
}

void Machine::start(int64_t now) {
    target_ = UsbCandidate{};
    host_.clear();
    confirmed_ = false;
    set_done_ = false;
    go(State::WaitUsb, now);
}

bool Machine::confirm(int64_t now) {
    if (pending_) return false;
    if (state_ == State::AlreadyPaired) { confirmed_ = true; go(State::SetMaster, now); return true; }
    if (state_ == State::NeedHelper) { go(State::Register, now); return true; }
    return false;
}

void Machine::cancel() {
    if (!active()) return;
    state_ = State::Cancelled;
    pending_ = false;
    message_.clear();
}

Action Machine::step(int64_t now, const Observation& obs) {
    Action a;
    if (pending_) return a;  // er loopt nog een actie
    const auto still_plugged = [&] {
        for (const auto& c : obs.usb) if (c.node == target_.node || (!target_.address.empty() && c.address == target_.address)) return true;
        return false;
    };
    switch (state_) {
        case State::WaitUsb: {
            if (now - since_ > to_.wait_usb_ns) { go(State::Failed, now, "Er is binnen 5 minuten geen controller via USB aangesloten."); break; }
            for (const auto& c : obs.usb) {
                if (c.address.empty()) continue;  // eigen adres nog niet gelezen
                if (obs.host.empty()) { go(State::Failed, now, "Geen Bluetooth-adapter gevonden op deze pc."); return a; }
                target_ = c;
                host_ = obs.host;
                const bool paired = obs.bluez_paired.count(c.address) && c.master == obs.host;
                go(paired ? State::AlreadyPaired : State::SetMaster, now);
                return step(now, obs);
            }
            break;
        }
        case State::AlreadyPaired:
            if (!still_plugged() && obs.bt_connected.count(target_.address)) go(State::Done, now, "Al gekoppeld; de controller is verbonden.");
            break;
        case State::SetMaster:
            if (!still_plugged()) { go(State::Failed, now, "De kabel is losgehaald voordat het koppelen klaar was. Sluit hem opnieuw aan."); break; }
            if (target_.master == host_ && !set_done_) {  // wijst al naar deze pc: niets te schrijven
                set_done_ = true;
                go(obs.helper_installed ? State::Register : State::NeedHelper, now);
                return step(now, obs);
            }
            a.kind = Action::Kind::SetMaster;
            a.target = target_;
            a.host = host_;
            pending_ = true;
            break;
        case State::NeedHelper:
            if (obs.helper_installed) { go(State::Register, now); return step(now, obs); }
            break;
        case State::Register:
            if (!obs.helper_installed) { go(State::NeedHelper, now); break; }
            a.kind = Action::Kind::Register;
            a.target = target_;
            a.host = host_;
            pending_ = true;
            break;
        case State::WaitUnplug:
            if (obs.bt_connected.count(target_.address)) { go(State::Done, now); break; }
            if (!still_plugged()) { go(State::WaitBluetooth, now); break; }
            if (now - since_ > to_.wait_unplug_ns) go(State::Failed, now, "De kabel is niet binnen 2 minuten losgehaald. Start het koppelen opnieuw.");
            break;
        case State::WaitBluetooth:
            if (obs.bt_connected.count(target_.address)) { go(State::Done, now); break; }
            if (now - since_ > to_.wait_bluetooth_ns)
                go(State::Failed, now, target_.type == "navigation"
                    ? "De Navigation Controller verbond niet binnen 2 minuten. Is de accu geladen? Probeer opnieuw met de PS-knop."
                    : "De Move verbond niet binnen 2 minuten. Druk kort op de PS-knop; knippert hij niet, laad hem dan eerst op.");
            break;
        default: break;
    }
    return a;
}

void Machine::done(int64_t now, bool ok, const std::string& msg) {
    if (!pending_) return;
    pending_ = false;
    if (state_ == State::SetMaster) {
        if (!ok) { go(State::Failed, now, "Het adres van deze pc kon niet in de controller gezet worden" + (msg.empty() ? std::string(".") : ": " + msg)); return; }
        set_done_ = true;
        target_.master = host_;
        go(State::Register, now);
        return;
    }
    if (state_ == State::Register) {
        if (!ok) { go(State::Failed, now, "Registreren bij Bluetooth mislukte" + (msg.empty() ? std::string(".") : ": " + msg)); return; }
        go(State::WaitUnplug, now);
    }
}

std::string Machine::instruction() const {
    const bool nav = target_.type == "navigation";
    const std::string what = nav ? "de Navigation Controller" : "de Move";
    switch (state_) {
        case State::Idle: return "Druk op Nieuwe controller koppelen om te beginnen.";
        case State::WaitUsb: return "Sluit de controller met een USB-kabel aan op deze pc. Gebruik een kabel die data doorgeeft (geen laadkabeltje).";
        case State::AlreadyPaired: return "Deze controller is al gekoppeld aan deze pc. Wil je hem opnieuw koppelen? Haal anders de kabel eruit en druk op de PS-knop.";
        case State::SetMaster: return "Bezig: " + what + " wordt aan deze pc gekoppeld. Laat de kabel zitten.";
        case State::NeedHelper: return "Voor de laatste stap is eenmalig een hulpprogramma met beheerrechten nodig. Laat iemand met het beheerwachtwoord dit uitvoeren in een terminal, en druk daarna op Opnieuw proberen:";
        case State::Register: return "Bezig: Bluetooth wordt ingesteld. Bluetooth start daarvoor even opnieuw; andere controllers verbinden vanzelf weer.";
        case State::WaitUnplug: return "Haal de USB-kabel eruit.";
        case State::WaitBluetooth: return "Druk nu kort op de PS-knop van " + what + ".";
        case State::Done: return "Klaar: " + what + " is gekoppeld en verbonden.";
        case State::Failed: return "Koppelen is niet gelukt.";
        case State::Cancelled: return "Koppelen afgebroken.";
    }
    return {};
}

}  // namespace ravengun::pairing
