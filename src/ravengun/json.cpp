#include "ravengun/json.hpp"

#include <cmath>
#include <cstdio>

namespace ravengun::json {

std::string escape(std::string_view s) {
    std::string r;
    r.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            case '\b': r += "\\b"; break;
            case '\f': r += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    r += buf;
                } else {
                    r += static_cast<char>(c);
                }
        }
    }
    return r;
}

std::string quote(std::string_view s) { return "\"" + escape(s) + "\""; }

void Writer::sep() {
    if (after_key_) { after_key_ = false; return; }
    if (need_comma_) out_ += ',';
}

Writer& Writer::begin_object() { sep(); out_ += '{'; need_comma_ = false; return *this; }
Writer& Writer::end_object() { out_ += '}'; need_comma_ = true; return *this; }
Writer& Writer::begin_array() { sep(); out_ += '['; need_comma_ = false; return *this; }
Writer& Writer::end_array() { out_ += ']'; need_comma_ = true; return *this; }
Writer& Writer::key(std::string_view k) { sep(); out_ += quote(k); out_ += ':'; after_key_ = true; return *this; }
Writer& Writer::value(std::string_view v) { sep(); out_ += quote(v); need_comma_ = true; return *this; }
Writer& Writer::value(bool v) { sep(); out_ += v ? "true" : "false"; need_comma_ = true; return *this; }
Writer& Writer::value(long long v) { sep(); out_ += std::to_string(v); need_comma_ = true; return *this; }
Writer& Writer::value(unsigned long long v) { sep(); out_ += std::to_string(v); need_comma_ = true; return *this; }
Writer& Writer::value(double v) {
    sep();
    if (!std::isfinite(v)) { out_ += "null"; }
    else { char buf[32]; std::snprintf(buf, sizeof buf, "%.9g", v); out_ += buf; }
    need_comma_ = true;
    return *this;
}
Writer& Writer::raw(std::string_view t) { sep(); out_ += t; need_comma_ = true; return *this; }
Writer& Writer::null() { sep(); out_ += "null"; need_comma_ = true; return *this; }

}  // namespace ravengun::json
