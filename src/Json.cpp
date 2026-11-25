#include "Json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json {

namespace {
const Value kNull;
const std::string kEmpty;
const Value::Array kEmptyArray;
const Value::Object kEmptyObject;
} // namespace

const std::string& Value::asString() const { return isString() ? str_ : kEmpty; }
const Value::Array& Value::asArray() const { return isArray() ? *arr_ : kEmptyArray; }
const Value::Object& Value::asObject() const { return isObject() ? *obj_ : kEmptyObject; }

Value::Object& Value::asObject() {
    if (!isObject()) {
        *this = Value(Object{});
    } else if (obj_.use_count() > 1) {
        obj_ = std::make_shared<Object>(*obj_); // copy-on-write
    }
    return *obj_;
}

const Value& Value::operator[](const std::string& key) const {
    if (!isObject()) return kNull;
    auto it = obj_->find(key);
    return it == obj_->end() ? kNull : it->second;
}

bool Value::has(const std::string& key) const {
    return isObject() && obj_->count(key) > 0;
}

void Value::set(const std::string& key, Value v) { asObject()[key] = std::move(v); }

// ---------------------------------------------------------------- parser

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    Value parseDocument() {
        // Pomiń BOM UTF-8 (Notatnik potrafi go dodać).
        if (s_.size() >= 3 && (unsigned char)s_[0] == 0xEF && (unsigned char)s_[1] == 0xBB &&
            (unsigned char)s_[2] == 0xBF)
            pos_ = 3;
        Value v = parseValue();
        skipWs();
        if (pos_ != s_.size()) fail("unexpected trailing characters");
        return v;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;
    int depth_ = 0;

    [[noreturn]] void fail(const std::string& msg) {
        throw ParseError("JSON: " + msg + " at offset " + std::to_string(pos_));
    }

    void skipWs() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    char peek() {
        skipWs();
        if (pos_ >= s_.size()) fail("unexpected end of input");
        return s_[pos_];
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    bool consumeLiteral(const char* lit) {
        size_t n = std::char_traits<char>::length(lit);
        if (s_.compare(pos_, n, lit) == 0) {
            pos_ += n;
            return true;
        }
        return false;
    }

    Value parseValue() {
        if (++depth_ > 200) fail("nesting too deep");
        Value result;
        char c = peek();
        if (c == '{') result = parseObject();
        else if (c == '[') result = parseArray();
        else if (c == '"') result = Value(parseString());
        else if (c == 't' && consumeLiteral("true")) result = Value(true);
        else if (c == 'f' && consumeLiteral("false")) result = Value(false);
        else if (c == 'n' && consumeLiteral("null")) result = Value();
        else if (c == '-' || (c >= '0' && c <= '9')) result = parseNumber();
        else fail("unexpected character");
        --depth_;
        return result;
    }

    Value parseObject() {
        expect('{');
        Value::Object obj;
        if (peek() == '}') {
            ++pos_;
            return Value(std::move(obj));
        }
        for (;;) {
            if (peek() != '"') fail("expected string key");
            std::string key = parseString();
            expect(':');
            obj[key] = parseValue();
            char c = peek();
            ++pos_;
            if (c == '}') break;
            if (c != ',') fail("expected ',' or '}'");
        }
        return Value(std::move(obj));
    }

    Value parseArray() {
        expect('[');
        Value::Array arr;
        if (peek() == ']') {
            ++pos_;
            return Value(std::move(arr));
        }
        for (;;) {
            arr.push_back(parseValue());
            char c = peek();
            ++pos_;
            if (c == ']') break;
            if (c != ',') fail("expected ',' or ']'");
        }
        return Value(std::move(arr));
    }

    Value parseNumber() {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() &&
               (isdigit((unsigned char)s_[pos_]) || s_[pos_] == '.' || s_[pos_] == 'e' ||
                s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-'))
            ++pos_;
        std::string num = s_.substr(start, pos_ - start);
        char* end = nullptr;
        double d = std::strtod(num.c_str(), &end);
        if (end == num.c_str() || *end != '\0') fail("invalid number");
        return Value(d);
    }

    unsigned parseHex4() {
        if (pos_ + 4 > s_.size()) fail("truncated \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else fail("invalid \\u escape");
        }
        return v;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    std::string parseString() {
        expect('"');
        std::string out;
        for (;;) {
            if (pos_ >= s_.size()) fail("unterminated string");
            char c = s_[pos_++];
            if (c == '"') break;
            if ((unsigned char)c < 0x20) fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) fail("unterminated escape");
            char e = s_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = parseHex4();
                if (cp >= 0xD800 && cp <= 0xDBFF) { // para surogatów
                    if (pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned lo = parseHex4();
                        if (lo < 0xDC00 || lo > 0xDFFF) fail("invalid surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else {
                        fail("lone high surrogate");
                    }
                }
                appendUtf8(out, cp);
                break;
            }
            default: fail("invalid escape");
            }
        }
        return out;
    }
};

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c; // UTF-8 przepuszczamy bez zmian
            }
        }
    }
    out += '"';
}

void writeValue(std::string& out, const Value& v, bool pretty, int indent) {
    auto newline = [&](int lvl) {
        if (!pretty) return;
        out += '\n';
        out.append(lvl * 2, ' ');
    };
    switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
    case Value::Type::Number: {
        double d = v.asNumber();
        char buf[32];
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15)
            std::snprintf(buf, sizeof buf, "%.0f", d);
        else
            std::snprintf(buf, sizeof buf, "%.17g", d);
        out += buf;
        break;
    }
    case Value::Type::String: writeString(out, v.asString()); break;
    case Value::Type::Array: {
        const auto& arr = v.asArray();
        out += '[';
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i) out += ',';
            newline(indent + 1);
            writeValue(out, arr[i], pretty, indent + 1);
        }
        if (!arr.empty()) newline(indent);
        out += ']';
        break;
    }
    case Value::Type::Object: {
        const auto& obj = v.asObject();
        out += '{';
        bool first = true;
        for (const auto& [k, val] : obj) {
            if (!first) out += ',';
            first = false;
            newline(indent + 1);
            writeString(out, k);
            out += pretty ? ": " : ":";
            writeValue(out, val, pretty, indent + 1);
        }
        if (!obj.empty()) newline(indent);
        out += '}';
        break;
    }
    }
}

} // namespace

Value parse(const std::string& text) { return Parser(text).parseDocument(); }

std::string serialize(const Value& v, bool pretty) {
    std::string out;
    writeValue(out, v, pretty, 0);
    if (pretty) out += '\n';
    return out;
}

} // namespace json
