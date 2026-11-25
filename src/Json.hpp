// Minimalny parser/zapis JSON (UTF-8) bez zewnętrznych zależności.
// Wystarcza do plików konfiguracji i tłumaczeń WT Cleanera.
#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double d) : type_(Type::Number), num_(d) {}
    Value(int i) : type_(Type::Number), num_(i) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : type_(Type::Object), obj_(std::make_shared<Object>(std::move(o))) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return isBool() ? bool_ : def; }
    double asNumber(double def = 0) const { return isNumber() ? num_ : def; }
    const std::string& asString() const;
    std::string asString(const std::string& def) const { return isString() ? str_ : def; }
    const Array& asArray() const;
    const Object& asObject() const;
    Object& asObject();

    // Dostęp do pól obiektu; zwraca Null, jeśli brak klucza lub to nie obiekt.
    const Value& operator[](const std::string& key) const;
    bool has(const std::string& key) const;
    void set(const std::string& key, Value v);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::shared_ptr<Array> arr_;
    std::shared_ptr<Object> obj_;
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Value parse(const std::string& text);          // rzuca ParseError
std::string serialize(const Value& v, bool pretty = true);

} // namespace json
