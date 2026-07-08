#pragma once
// Minimal JSON library — adapted from server.cpp

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value {
    Type type = Type::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    Array arr_val;
    Object obj_val;

    Value() = default;
    Value(std::nullptr_t) : type(Type::Null) {}
    Value(bool v) : type(Type::Bool), bool_val(v) {}
    Value(int v) : type(Type::Number), num_val(static_cast<double>(v)) {}
    Value(long v) : type(Type::Number), num_val(static_cast<double>(v)) {}
    Value(long long v) : type(Type::Number), num_val(static_cast<double>(v)) {}
    Value(unsigned v) : type(Type::Number), num_val(static_cast<double>(v)) {}
    Value(double v) : type(Type::Number), num_val(v) {}
    Value(const char* v) : type(Type::String), str_val(v) {}
    Value(const std::string& v) : type(Type::String), str_val(v) {}
    Value(const Array& v) : type(Type::Array), arr_val(v) {}
    Value(const Object& v) : type(Type::Object), obj_val(v) {}
    Value(std::initializer_list<Value> list) : type(Type::Array), arr_val(list) {}

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    int as_int() const { return static_cast<int>(num_val); }
    double as_double() const { return num_val; }
    bool as_bool() const { return bool_val; }
    const std::string& as_string() const { return str_val; }

    bool contains(const std::string& key) const {
        return is_object() && obj_val.count(key) > 0;
    }

    Value& operator[](const std::string& key) {
        if (!is_object()) { type = Type::Object; obj_val.clear(); }
        return obj_val[key];
    }
    const Value& operator[](const std::string& key) const {
        static Value null_val;
        if (is_object()) {
            auto it = obj_val.find(key);
            if (it != obj_val.end()) return it->second;
        }
        return null_val;
    }

    Value& operator[](size_t i) {
        if (!is_array()) { type = Type::Array; arr_val.clear(); }
        if (i >= arr_val.size()) arr_val.resize(i + 1);
        return arr_val[i];
    }
    const Value& operator[](size_t i) const {
        static Value null_val;
        if (is_array() && i < arr_val.size()) return arr_val[i];
        return null_val;
    }

    size_t size() const {
        if (is_array()) return arr_val.size();
        if (is_object()) return obj_val.size();
        return 0;
    }

    void push_back(const Value& v) {
        if (!is_array()) { type = Type::Array; arr_val.clear(); }
        arr_val.push_back(v);
    }
};

// Parser
struct Parser {
    const char* p;
    const char* end;
    Parser(const std::string& s) : p(s.c_str()), end(s.c_str() + s.size()) {}

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }

    Value parse_value() {
        skip_ws();
        if (p >= end) return Value();
        switch (*p) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return parse_string();
            case 't': case 'f': case 'n': return parse_literal();
            default: return parse_number();
        }
    }

    Value parse_object() {
        Object obj;
        p++;
        skip_ws();
        if (p < end && *p == '}') { p++; return Value(obj); }
        while (p < end) {
            skip_ws();
            Value key = parse_string();
            skip_ws();
            if (p < end && *p == ':') p++;
            Value val = parse_value();
            obj[key.as_string()] = val;
            skip_ws();
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == '}') { p++; break; }
        }
        return Value(obj);
    }

    Value parse_array() {
        Array arr;
        p++;
        skip_ws();
        if (p < end && *p == ']') { p++; return Value(arr); }
        while (p < end) {
            arr.push_back(parse_value());
            skip_ws();
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == ']') { p++; break; }
        }
        return Value(arr);
    }

    Value parse_string() {
        p++;
        std::string s;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                p++;
                if (p < end) {
                    switch (*p) {
                        case '"': s += '"'; break;
                        case '\\': s += '\\'; break;
                        case '/': s += '/'; break;
                        case 'b': s += '\b'; break;
                        case 'f': s += '\f'; break;
                        case 'n': s += '\n'; break;
                        case 'r': s += '\r'; break;
                        case 't': s += '\t'; break;
                        case 'u': p += 4; s += '?'; continue;
                        default: s += *p; break;
                    }
                }
            } else {
                s += *p;
            }
            p++;
        }
        if (p < end) p++;
        return Value(s);
    }

    Value parse_number() {
        const char* start = p;
        if (p < end && *p == '-') p++;
        while (p < end && *p >= '0' && *p <= '9') p++;
        if (p < end && *p == '.') {
            p++;
            while (p < end && *p >= '0' && *p <= '9') p++;
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            p++;
            if (p < end && (*p == '+' || *p == '-')) p++;
            while (p < end && *p >= '0' && *p <= '9') p++;
        }
        return Value(std::stod(std::string(start, p - start)));
    }

    Value parse_literal() {
        if (strncmp(p, "true", 4) == 0) { p += 4; return Value(true); }
        if (strncmp(p, "false", 5) == 0) { p += 5; return Value(false); }
        if (strncmp(p, "null", 4) == 0) { p += 4; return Value(); }
        return Value();
    }
};

inline Value parse(const std::string& s) {
    Parser parser(s);
    return parser.parse_value();
}

// Serializer
inline std::string serialize(const Value& v) {
    std::ostringstream oss;
    switch (v.type) {
        case Type::Null: oss << "null"; break;
        case Type::Bool: oss << (v.bool_val ? "true" : "false"); break;
        case Type::Number: {
            double d = v.num_val;
            if (d == static_cast<int64_t>(d)) oss << static_cast<int64_t>(d);
            else oss << d;
            break;
        }
        case Type::String: {
            oss << '"';
            for (char c : v.str_val) {
                switch (c) {
                    case '"': oss << "\\\""; break;
                    case '\\': oss << "\\\\"; break;
                    case '\n': oss << "\\n"; break;
                    case '\r': oss << "\\r"; break;
                    case '\t': oss << "\\t"; break;
                    default: oss << c;
                }
            }
            oss << '"';
            break;
        }
        case Type::Array: {
            oss << '[';
            for (size_t i = 0; i < v.arr_val.size(); i++) {
                if (i > 0) oss << ',';
                oss << serialize(v.arr_val[i]);
            }
            oss << ']';
            break;
        }
        case Type::Object: {
            oss << '{';
            bool first = true;
            for (auto& [key, val] : v.obj_val) {
                if (!first) oss << ',';
                oss << '"' << key << "\":" << serialize(val);
                first = false;
            }
            oss << '}';
            break;
        }
    }
    return oss.str();
}

} // namespace json
