#include "ravengun/json_parse.hpp"

#include <cmath>
#include <cstdlib>

namespace ravengun::json {

const Value* Value::find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
}

const char* type_name(Value::Type t) {
    switch (t) {
        case Value::Type::Null: return "null";
        case Value::Type::Bool: return "boolean";
        case Value::Type::Number: return "number";
        case Value::Type::String: return "string";
        case Value::Type::Array: return "array";
        case Value::Type::Object: return "object";
    }
    return "?";
}

namespace {

constexpr int kMaxDepth = 32;

struct Parser {
    std::string_view s;
    size_t i = 0;
    std::string err;

    bool fail(const char* m) { if (err.empty()) err = m; return false; }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool lit(std::string_view w) {
        if (s.substr(i, w.size()) != w) return fail("ongeldig literal");
        i += w.size();
        return true;
    }

    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& v) {
        if (i + 4 > s.size()) return fail("onvolledige \\u-escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("ongeldige \\u-escape");
        }
        return true;
    }

    bool string(std::string& o) {
        if (i >= s.size() || s[i] != '"') return fail("string verwacht");
        ++i;
        while (true) {
            if (i >= s.size()) return fail("onafgesloten string");
            unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"') return true;
            if (c < 0x20) return fail("stuurteken in string");
            if (c != '\\') { o += static_cast<char>(c); continue; }
            if (i >= s.size()) return fail("onafgesloten escape");
            char e = s[i++];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        unsigned lo;
                        if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return fail("losse surrogate");
                        i += 2;
                        if (!hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) return fail("ongeldige surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("losse surrogate");
                    }
                    utf8(o, cp);
                    break;
                }
                default: return fail("ongeldige escape");
            }
        }
    }

    bool number(double& v) {
        size_t st = i;
        if (i < s.size() && s[i] == '-') ++i;
        if (i >= s.size()) return fail("getal verwacht");
        if (s[i] == '0') ++i;
        else if (s[i] >= '1' && s[i] <= '9') { while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i; }
        else return fail("getal verwacht");
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') return fail("cijfer na punt verwacht");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i >= s.size() || s[i] < '0' || s[i] > '9') return fail("exponent verwacht");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        std::string tmp(s.substr(st, i - st));
        v = std::strtod(tmp.c_str(), nullptr);
        if (!std::isfinite(v)) return fail("getal buiten bereik");
        return true;
    }

    bool value(Value& v, int depth) {
        if (depth > kMaxDepth) return fail("te diep genest");
        ws();
        if (i >= s.size()) return fail("waarde verwacht");
        char c = s[i];
        if (c == '{') {
            ++i;
            v.type = Value::Type::Object;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (true) {
                ws();
                std::string k;
                if (!string(k)) return false;
                if (v.find(k)) return fail("dubbele sleutel");
                ws();
                if (i >= s.size() || s[i] != ':') return fail("':' verwacht");
                ++i;
                Value child;
                if (!value(child, depth + 1)) return false;
                v.obj.emplace_back(std::move(k), std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("',' of '}' verwacht");
            }
        }
        if (c == '[') {
            ++i;
            v.type = Value::Type::Array;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (true) {
                Value child;
                if (!value(child, depth + 1)) return false;
                v.arr.push_back(std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("',' of ']' verwacht");
            }
        }
        if (c == '"') { v.type = Value::Type::String; return string(v.str); }
        if (c == 't') { v.type = Value::Type::Bool; v.b = true; return lit("true"); }
        if (c == 'f') { v.type = Value::Type::Bool; v.b = false; return lit("false"); }
        if (c == 'n') { v.type = Value::Type::Null; return lit("null"); }
        v.type = Value::Type::Number;
        return number(v.num);
    }
};

}  // namespace

ParseResult parse(std::string_view text) {
    Parser p;
    p.s = text;
    ParseResult r;
    if (!p.value(r.value, 0)) {
        r.error = p.err;
        r.offset = p.i;
        r.value = Value{};
        return r;
    }
    p.ws();
    if (p.i != text.size()) {
        r.error = "extra tekens na de waarde";
        r.offset = p.i;
        r.value = Value{};
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace ravengun::json
