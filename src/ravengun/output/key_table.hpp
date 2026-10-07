#pragma once
// Toetsnamen voor het virtuele toetsenbord (uinput_kbd) en de configuratie (`key:<naam>`).
//
// De tabel is dezelfde als in het losse hulpprogramma src/tools/vinput/vinput.c (dat is C en deelt deze
// header niet; tests/test_output.cpp controleert de namen en codes). Namen zijn kleine letters; een
// voorvoegsel "key_" en hoofdletters worden bij het opzoeken genegeerd.
#include <linux/input-event-codes.h>

#include <array>
#include <cctype>
#include <string>

namespace ravengun::output {

struct KeyName { const char* name; int code; };

inline constexpr std::array<KeyName, 60> kKeyNames{{
    {"esc", KEY_ESC}, {"enter", KEY_ENTER}, {"space", KEY_SPACE}, {"tab", KEY_TAB}, {"backspace", KEY_BACKSPACE},
    {"up", KEY_UP}, {"down", KEY_DOWN}, {"left", KEY_LEFT}, {"right", KEY_RIGHT},
    {"lshift", KEY_LEFTSHIFT}, {"lctrl", KEY_LEFTCTRL}, {"lalt", KEY_LEFTALT},
    {"f1", KEY_F1}, {"f2", KEY_F2}, {"f3", KEY_F3}, {"f4", KEY_F4}, {"f5", KEY_F5}, {"f6", KEY_F6},
    {"f7", KEY_F7}, {"f8", KEY_F8}, {"f9", KEY_F9}, {"f10", KEY_F10}, {"f11", KEY_F11}, {"f12", KEY_F12},
    {"0", KEY_0}, {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4}, {"5", KEY_5}, {"6", KEY_6},
    {"7", KEY_7}, {"8", KEY_8}, {"9", KEY_9},
    {"a", KEY_A}, {"b", KEY_B}, {"c", KEY_C}, {"d", KEY_D}, {"e", KEY_E}, {"f", KEY_F}, {"g", KEY_G},
    {"h", KEY_H}, {"i", KEY_I}, {"j", KEY_J}, {"k", KEY_K}, {"l", KEY_L}, {"m", KEY_M}, {"n", KEY_N},
    {"o", KEY_O}, {"p", KEY_P}, {"q", KEY_Q}, {"r", KEY_R}, {"s", KEY_S}, {"t", KEY_T}, {"u", KEY_U},
    {"v", KEY_V}, {"w", KEY_W}, {"x", KEY_X}, {"y", KEY_Y}, {"z", KEY_Z},
}};

// evdev-code bij een toetsnaam, of -1 als de naam onbekend is.
inline int key_code(const std::string& name) {
    std::string b;
    for (char c : name) b += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (b.compare(0, 4, "key_") == 0) b.erase(0, 4);
    for (const auto& k : kKeyNames)
        if (b == k.name) return k.code;
    return -1;
}

// Canonieke naam bij een evdev-code ("" als onbekend).
inline const char* key_name(int code) {
    for (const auto& k : kKeyNames)
        if (k.code == code) return k.name;
    return "";
}

}  // namespace ravengun::output
