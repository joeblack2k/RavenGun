#pragma once
// Kleine, strikte JSON-parser (RFC 8259) voor de beheer-IPC. Geen externe afhankelijkheid.
// Grenzen: nestdiepte max 32; dubbele sleutels in een object worden geweigerd.
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ravengun::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }
    // nullptr als de sleutel ontbreekt of dit geen object is.
    const Value* find(std::string_view key) const;
};

struct ParseResult {
    bool ok = false;
    Value value;
    std::string error;  // leeg bij ok
    size_t offset = 0;  // positie van de fout
};

ParseResult parse(std::string_view text);

}  // namespace ravengun::json
