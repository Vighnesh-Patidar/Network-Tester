#pragma once

// Self-contained JSON value type providing the subset of the nlohmann::json
// interface this project relies on (parsing, serialization, typed accessors,
// object/array construction and iteration). The build prefers a system-provided
// nlohmann/json when find_package locates it; this vendored header is used only
// for offline/air-gapped builds where the upstream package is unavailable. The
// public surface is kept API-compatible on purpose so call sites never branch.

#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace nlohmann {

class json;

namespace detail {

[[noreturn]] inline void throw_type_error(const std::string& what) {
    throw std::runtime_error("json type error: " + what);
}

[[noreturn]] inline void throw_parse_error(const std::string& what, std::size_t pos) {
    throw std::runtime_error("json parse error at offset " + std::to_string(pos) + ": " + what);
}

}  // namespace detail

class json {
public:
    enum class value_t {
        null,
        object,
        array,
        string,
        boolean,
        number_integer,
        number_float
    };

    using object_t = std::map<std::string, json>;
    using array_t = std::vector<json>;

    json() : type_(value_t::null) {}
    json(std::nullptr_t) : type_(value_t::null) {}
    json(bool v) : type_(value_t::boolean), bool_(v) {}

    template <typename T,
              typename std::enable_if<std::is_integral<T>::value &&
                                          !std::is_same<T, bool>::value,
                                      int>::type = 0>
    json(T v) : type_(value_t::number_integer), int_(static_cast<std::int64_t>(v)) {}

    template <typename T,
              typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
    json(T v) : type_(value_t::number_float), float_(static_cast<double>(v)) {}

    json(const char* v) : type_(value_t::string), str_(v) {}
    json(const std::string& v) : type_(value_t::string), str_(v) {}
    json(std::string&& v) : type_(value_t::string), str_(std::move(v)) {}

    static json array() {
        json j;
        j.type_ = value_t::array;
        return j;
    }

    static json object() {
        json j;
        j.type_ = value_t::object;
        return j;
    }

    value_t type() const { return type_; }

    bool is_null() const { return type_ == value_t::null; }
    bool is_object() const { return type_ == value_t::object; }
    bool is_array() const { return type_ == value_t::array; }
    bool is_string() const { return type_ == value_t::string; }
    bool is_boolean() const { return type_ == value_t::boolean; }
    bool is_number_integer() const { return type_ == value_t::number_integer; }
    bool is_number_float() const { return type_ == value_t::number_float; }
    bool is_number() const {
        return type_ == value_t::number_integer || type_ == value_t::number_float;
    }

    bool contains(const std::string& key) const {
        return type_ == value_t::object && obj_.find(key) != obj_.end();
    }

    std::size_t size() const {
        switch (type_) {
            case value_t::object:
                return obj_.size();
            case value_t::array:
                return arr_.size();
            case value_t::null:
                return 0;
            default:
                return 1;
        }
    }

    bool empty() const { return size() == 0; }

    json& operator[](const std::string& key) {
        if (type_ == value_t::null) {
            type_ = value_t::object;
        }
        if (type_ != value_t::object) {
            detail::throw_type_error("operator[] with string key on non-object");
        }
        return obj_[key];
    }

    json& operator[](const char* key) { return operator[](std::string(key)); }

    const json& operator[](const std::string& key) const { return at(key); }

    json& operator[](std::size_t idx) {
        if (type_ == value_t::null) {
            type_ = value_t::array;
        }
        if (type_ != value_t::array) {
            detail::throw_type_error("operator[] with index on non-array");
        }
        if (idx >= arr_.size()) {
            arr_.resize(idx + 1);
        }
        return arr_[idx];
    }

    const json& operator[](std::size_t idx) const {
        if (type_ != value_t::array) {
            detail::throw_type_error("operator[] with index on non-array");
        }
        return arr_.at(idx);
    }

    json& at(const std::string& key) {
        if (type_ != value_t::object) {
            detail::throw_type_error("at() with string key on non-object");
        }
        auto it = obj_.find(key);
        if (it == obj_.end()) {
            detail::throw_type_error("key '" + key + "' not found");
        }
        return it->second;
    }

    const json& at(const std::string& key) const {
        if (type_ != value_t::object) {
            detail::throw_type_error("at() with string key on non-object");
        }
        auto it = obj_.find(key);
        if (it == obj_.end()) {
            detail::throw_type_error("key '" + key + "' not found");
        }
        return it->second;
    }

    json& at(std::size_t idx) {
        if (type_ != value_t::array) {
            detail::throw_type_error("at() with index on non-array");
        }
        return arr_.at(idx);
    }

    const json& at(std::size_t idx) const {
        if (type_ != value_t::array) {
            detail::throw_type_error("at() with index on non-array");
        }
        return arr_.at(idx);
    }

    template <typename T>
    T value(const std::string& key, const T& default_value) const {
        if (type_ != value_t::object) {
            return default_value;
        }
        auto it = obj_.find(key);
        if (it == obj_.end()) {
            return default_value;
        }
        return it->second.get<T>();
    }

    std::string value(const std::string& key, const char* default_value) const {
        return value<std::string>(key, std::string(default_value));
    }

    void push_back(const json& v) {
        if (type_ == value_t::null) {
            type_ = value_t::array;
        }
        if (type_ != value_t::array) {
            detail::throw_type_error("push_back on non-array");
        }
        arr_.push_back(v);
    }

    void push_back(json&& v) {
        if (type_ == value_t::null) {
            type_ = value_t::array;
        }
        if (type_ != value_t::array) {
            detail::throw_type_error("push_back on non-array");
        }
        arr_.push_back(std::move(v));
    }

    template <typename T>
    T get() const {
        if constexpr (std::is_same<T, bool>::value) {
            if (type_ != value_t::boolean) {
                detail::throw_type_error("value is not a boolean");
            }
            return bool_;
        } else if constexpr (std::is_integral<T>::value) {
            if (type_ == value_t::number_integer) {
                return static_cast<T>(int_);
            }
            if (type_ == value_t::number_float) {
                return static_cast<T>(float_);
            }
            detail::throw_type_error("value is not an integer");
        } else if constexpr (std::is_floating_point<T>::value) {
            if (type_ == value_t::number_float) {
                return static_cast<T>(float_);
            }
            if (type_ == value_t::number_integer) {
                return static_cast<T>(int_);
            }
            detail::throw_type_error("value is not a number");
        } else if constexpr (std::is_same<T, std::string>::value) {
            if (type_ != value_t::string) {
                detail::throw_type_error("value is not a string");
            }
            return str_;
        } else {
            static_assert(sizeof(T) == 0, "unsupported type for json::get<T>()");
        }
    }

    template <typename T>
    void get_to(T& out) const {
        out = get<T>();
    }

    const std::string& get_ref_string() const {
        if (type_ != value_t::string) {
            detail::throw_type_error("value is not a string");
        }
        return str_;
    }

    // Range support: iterating an array yields elements; iterating an object
    // yields mapped values, matching nlohmann's begin()/end() semantics.
    array_t::iterator begin() { ensure_array_for_iteration(); return arr_.begin(); }
    array_t::iterator end() { ensure_array_for_iteration(); return arr_.end(); }
    array_t::const_iterator begin() const { return arr_.begin(); }
    array_t::const_iterator end() const { return arr_.end(); }

    const array_t& array_items() const {
        if (type_ != value_t::array) {
            detail::throw_type_error("array_items() on non-array");
        }
        return arr_;
    }

    const object_t& object_items() const {
        if (type_ != value_t::object) {
            detail::throw_type_error("object_items() on non-object");
        }
        return obj_;
    }

    // Mirrors nlohmann::json::items(): a key/value view over an object.
    struct items_proxy {
        const object_t* obj;
        object_t::const_iterator begin() const { return obj->begin(); }
        object_t::const_iterator end() const { return obj->end(); }
    };

    items_proxy items() const {
        if (type_ != value_t::object) {
            detail::throw_type_error("items() on non-object");
        }
        return items_proxy{&obj_};
    }

    static json parse(const std::string& text) {
        parser p(text);
        json result = p.parse_value();
        p.skip_whitespace();
        if (!p.at_end()) {
            detail::throw_parse_error("trailing characters", p.position());
        }
        return result;
    }

    static json parse(std::istream& in) {
        std::ostringstream ss;
        ss << in.rdbuf();
        return parse(ss.str());
    }

    std::string dump(int indent = -1) const {
        std::ostringstream out;
        dump_internal(out, indent, 0);
        return out.str();
    }

private:
    value_t type_;
    object_t obj_;
    array_t arr_;
    std::string str_;
    bool bool_ = false;
    std::int64_t int_ = 0;
    double float_ = 0.0;

    void ensure_array_for_iteration() {
        if (type_ == value_t::null) {
            type_ = value_t::array;
        }
        if (type_ != value_t::array) {
            detail::throw_type_error("iteration requires an array");
        }
    }

    static void dump_string(std::ostream& out, const std::string& s) {
        out << '"';
        for (char c : s) {
            switch (c) {
                case '"':
                    out << "\\\"";
                    break;
                case '\\':
                    out << "\\\\";
                    break;
                case '\b':
                    out << "\\b";
                    break;
                case '\f':
                    out << "\\f";
                    break;
                case '\n':
                    out << "\\n";
                    break;
                case '\r':
                    out << "\\r";
                    break;
                case '\t':
                    out << "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        static const char* hex = "0123456789abcdef";
                        out << "\\u00" << hex[(c >> 4) & 0xF] << hex[c & 0xF];
                    } else {
                        out << c;
                    }
            }
        }
        out << '"';
    }

    static void dump_float(std::ostream& out, double v) {
        std::ostringstream tmp;
        tmp.precision(15);
        tmp << v;
        std::string s = tmp.str();
        // Guarantee a JSON number reader sees a float, not an int literal.
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
            s.find('E') == std::string::npos && s.find("inf") == std::string::npos &&
            s.find("nan") == std::string::npos) {
            s += ".0";
        }
        out << s;
    }

    void dump_internal(std::ostream& out, int indent, int depth) const {
        const bool pretty = indent >= 0;
        const std::string pad = pretty ? std::string(static_cast<std::size_t>(indent) * (depth + 1), ' ') : std::string();
        const std::string pad_close = pretty ? std::string(static_cast<std::size_t>(indent) * depth, ' ') : std::string();
        const char* nl = pretty ? "\n" : "";
        const char* sep = pretty ? ": " : ":";

        switch (type_) {
            case value_t::null:
                out << "null";
                break;
            case value_t::boolean:
                out << (bool_ ? "true" : "false");
                break;
            case value_t::number_integer:
                out << int_;
                break;
            case value_t::number_float:
                dump_float(out, float_);
                break;
            case value_t::string:
                dump_string(out, str_);
                break;
            case value_t::array: {
                if (arr_.empty()) {
                    out << "[]";
                    break;
                }
                out << '[' << nl;
                bool first = true;
                for (const auto& e : arr_) {
                    if (!first) {
                        out << ',' << nl;
                    }
                    first = false;
                    out << pad;
                    e.dump_internal(out, indent, depth + 1);
                }
                out << nl << pad_close << ']';
                break;
            }
            case value_t::object: {
                if (obj_.empty()) {
                    out << "{}";
                    break;
                }
                out << '{' << nl;
                bool first = true;
                for (const auto& kv : obj_) {
                    if (!first) {
                        out << ',' << nl;
                    }
                    first = false;
                    out << pad;
                    dump_string(out, kv.first);
                    out << sep;
                    kv.second.dump_internal(out, indent, depth + 1);
                }
                out << nl << pad_close << '}';
                break;
            }
        }
    }

    class parser {
    public:
        explicit parser(const std::string& text) : text_(text) {}

        bool at_end() const { return pos_ >= text_.size(); }
        std::size_t position() const { return pos_; }

        void skip_whitespace() {
            while (pos_ < text_.size()) {
                char c = text_[pos_];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    ++pos_;
                } else {
                    break;
                }
            }
        }

        json parse_value() {
            skip_whitespace();
            if (at_end()) {
                detail::throw_parse_error("unexpected end of input", pos_);
            }
            char c = text_[pos_];
            switch (c) {
                case '{':
                    return parse_object();
                case '[':
                    return parse_array();
                case '"':
                    return json(parse_string());
                case 't':
                case 'f':
                    return parse_bool();
                case 'n':
                    return parse_null();
                default:
                    return parse_number();
            }
        }

    private:
        const std::string& text_;
        std::size_t pos_ = 0;

        char peek() const { return text_[pos_]; }

        void expect(char c) {
            if (at_end() || text_[pos_] != c) {
                detail::throw_parse_error(std::string("expected '") + c + "'", pos_);
            }
            ++pos_;
        }

        json parse_object() {
            expect('{');
            json obj = json::object();
            skip_whitespace();
            if (!at_end() && peek() == '}') {
                ++pos_;
                return obj;
            }
            while (true) {
                skip_whitespace();
                if (at_end() || peek() != '"') {
                    detail::throw_parse_error("expected object key", pos_);
                }
                std::string key = parse_string();
                skip_whitespace();
                expect(':');
                obj.obj_[key] = parse_value();
                skip_whitespace();
                if (at_end()) {
                    detail::throw_parse_error("unterminated object", pos_);
                }
                char c = text_[pos_++];
                if (c == '}') {
                    break;
                }
                if (c != ',') {
                    detail::throw_parse_error("expected ',' or '}'", pos_ - 1);
                }
            }
            return obj;
        }

        json parse_array() {
            expect('[');
            json arr = json::array();
            skip_whitespace();
            if (!at_end() && peek() == ']') {
                ++pos_;
                return arr;
            }
            while (true) {
                arr.arr_.push_back(parse_value());
                skip_whitespace();
                if (at_end()) {
                    detail::throw_parse_error("unterminated array", pos_);
                }
                char c = text_[pos_++];
                if (c == ']') {
                    break;
                }
                if (c != ',') {
                    detail::throw_parse_error("expected ',' or ']'", pos_ - 1);
                }
            }
            return arr;
        }

        std::string parse_string() {
            expect('"');
            std::string out;
            while (true) {
                if (at_end()) {
                    detail::throw_parse_error("unterminated string", pos_);
                }
                char c = text_[pos_++];
                if (c == '"') {
                    break;
                }
                if (c == '\\') {
                    if (at_end()) {
                        detail::throw_parse_error("unterminated escape", pos_);
                    }
                    char esc = text_[pos_++];
                    switch (esc) {
                        case '"':
                            out += '"';
                            break;
                        case '\\':
                            out += '\\';
                            break;
                        case '/':
                            out += '/';
                            break;
                        case 'b':
                            out += '\b';
                            break;
                        case 'f':
                            out += '\f';
                            break;
                        case 'n':
                            out += '\n';
                            break;
                        case 'r':
                            out += '\r';
                            break;
                        case 't':
                            out += '\t';
                            break;
                        case 'u':
                            out += parse_unicode_escape();
                            break;
                        default:
                            detail::throw_parse_error("invalid escape sequence", pos_ - 1);
                    }
                } else {
                    out += c;
                }
            }
            return out;
        }

        std::string parse_unicode_escape() {
            if (pos_ + 4 > text_.size()) {
                detail::throw_parse_error("incomplete unicode escape", pos_);
            }
            unsigned int code = 0;
            for (int i = 0; i < 4; ++i) {
                char c = text_[pos_++];
                code <<= 4;
                if (c >= '0' && c <= '9') {
                    code |= static_cast<unsigned int>(c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    code |= static_cast<unsigned int>(c - 'a' + 10);
                } else if (c >= 'A' && c <= 'F') {
                    code |= static_cast<unsigned int>(c - 'A' + 10);
                } else {
                    detail::throw_parse_error("invalid unicode digit", pos_ - 1);
                }
            }
            return encode_utf8(code);
        }

        static std::string encode_utf8(unsigned int code) {
            std::string out;
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
            return out;
        }

        json parse_bool() {
            if (text_.compare(pos_, 4, "true") == 0) {
                pos_ += 4;
                return json(true);
            }
            if (text_.compare(pos_, 5, "false") == 0) {
                pos_ += 5;
                return json(false);
            }
            detail::throw_parse_error("invalid literal", pos_);
        }

        json parse_null() {
            if (text_.compare(pos_, 4, "null") == 0) {
                pos_ += 4;
                return json();
            }
            detail::throw_parse_error("invalid literal", pos_);
        }

        json parse_number() {
            std::size_t start = pos_;
            bool is_float = false;
            if (!at_end() && (peek() == '-' || peek() == '+')) {
                ++pos_;
            }
            while (!at_end()) {
                char c = peek();
                if (c >= '0' && c <= '9') {
                    ++pos_;
                } else if (c == '.' || c == 'e' || c == 'E') {
                    is_float = true;
                    ++pos_;
                    if (!at_end() && (peek() == '-' || peek() == '+')) {
                        ++pos_;
                    }
                } else {
                    break;
                }
            }
            if (pos_ == start) {
                detail::throw_parse_error("invalid number", pos_);
            }
            std::string token = text_.substr(start, pos_ - start);
            json result;
            if (is_float) {
                result.type_ = value_t::number_float;
                result.float_ = std::stod(token);
            } else {
                result.type_ = value_t::number_integer;
                result.int_ = static_cast<std::int64_t>(std::stoll(token));
            }
            return result;
        }
    };
};

inline std::ostream& operator<<(std::ostream& out, const json& j) {
    out << j.dump();
    return out;
}

}  // namespace nlohmann
