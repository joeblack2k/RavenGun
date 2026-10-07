#pragma once
// Minimale JSON-schrijver. De parser staat in ravengun/json_parse.hpp.
#include <string>
#include <string_view>

namespace ravengun::json {

// Escapet een string voor gebruik tussen aanhalingstekens (zonder die tekens).
std::string escape(std::string_view s);
// Geeft "\"...\"" terug.
std::string quote(std::string_view s);

// Eenvoudige opbouwer met komma-beheer; geen validatie van geneste volgorde.
class Writer {
public:
    Writer& begin_object();
    Writer& end_object();
    Writer& begin_array();
    Writer& end_array();
    Writer& key(std::string_view k);
    Writer& value(std::string_view v);
    Writer& value(const char* v) { return value(std::string_view(v)); }
    Writer& value(bool v);
    Writer& value(long long v);
    Writer& value(int v) { return value(static_cast<long long>(v)); }
    Writer& value(unsigned long long v);
    Writer& value(long v) { return value(static_cast<long long>(v)); }
    Writer& value(unsigned long v) { return value(static_cast<unsigned long long>(v)); }
    Writer& value(unsigned v) { return value(static_cast<unsigned long long>(v)); }
    // Niet-eindige getallen worden null (JSON kent geen NaN/Inf).
    Writer& value(double v);
    // Voegt een al geldige JSON-tekst ongewijzigd in (bijv. een eerder geschreven object).
    Writer& raw(std::string_view json_text);
    Writer& null();
    const std::string& str() const { return out_; }

private:
    void sep();
    std::string out_;
    bool need_comma_ = false;
    bool after_key_ = false;
};

}  // namespace ravengun::json
