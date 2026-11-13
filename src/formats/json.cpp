#include "formats/json.hpp"

#include <cctype>
#include <cstdint>

namespace d2k::formats::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    bool parse(Value& out, std::string& err) {
        if (!value(out, 0)) {
            err = err_.empty() ? "bad JSON" : err_;
            return false;
        }
        ws();
        if (i_ != s_.size()) {
            err = "trailing data after the JSON value";
            return false;
        }
        return true;
    }

private:
    void ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    bool fail(const char* m) {
        if (err_.empty()) err_ = std::string(m) + " at offset " + std::to_string(i_);
        return false;
    }
    bool lit(const char* w) {
        const std::size_t n = std::char_traits<char>::length(w);
        if (s_.compare(i_, n, w) != 0) return fail("bad literal");
        i_ += n;
        return true;
    }
    static void utf8(std::string& o, std::uint32_t cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | cp >> 6); o += char(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { o += char(0xE0 | cp >> 12); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
        else { o += char(0xF0 | cp >> 18); o += char(0x80 | ((cp >> 12) & 63)); o += char(0x80 | ((cp >> 6) & 63)); o += char(0x80 | (cp & 63)); }
    }
    bool hex4(std::uint32_t& v) {
        if (i_ + 4 > s_.size()) return fail("short \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= std::uint32_t(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& o) {
        if (i_ >= s_.size() || s_[i_] != '"') return fail("expected a string");
        ++i_;
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in a string");
            if (c != '\\') { o += c; continue; }
            if (i_ >= s_.size()) break;
            const char e = s_[i_++];
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
                    std::uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        i_ += 2;
                        std::uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Value& v, int depth) {
        if (depth > 16) return fail("nested too deep");
        ws();
        if (i_ >= s_.size()) return fail("unexpected end");
        const char c = s_[i_];
        if (c == '{') {
            v.kind = Value::Object;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            while (true) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i_ >= s_.size() || s_[i_] != ':') return fail("expected ':'");
                ++i_;
                Value item;
                if (!value(item, depth + 1)) return false;
                v.obj[k] = std::move(item);
                ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.kind = Value::Array;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            while (true) {
                Value item;
                if (!value(item, depth + 1)) return false;
                v.arr.push_back(std::move(item));
                ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.kind = Value::String; return string(v.str); }
        if (c == 't') { v.kind = Value::Bool; v.b = true; return lit("true"); }
        if (c == 'f') { v.kind = Value::Bool; v.b = false; return lit("false"); }
        if (c == 'n') { v.kind = Value::Null; return lit("null"); }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = i_;
            if (s_[i_] == '-') ++i_;
            while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.' || s_[i_] == 'e' ||
                                      s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-'))
                ++i_;
            try {
                v.num = std::stod(s_.substr(start, i_ - start));
            } catch (...) {
                return fail("bad number");
            }
            v.kind = Value::Number;
            return true;
        }
        return fail("unexpected character");
    }
    const std::string& s_;
    std::size_t i_ = 0;
    std::string err_;
};

}  // namespace

bool parse(const std::string& text, Value& out, std::string& error) { return Parser(text).parse(out, error); }

const Value* field(const Value& o, const char* k) {
    if (o.kind != Value::Object) return nullptr;
    const auto it = o.obj.find(k);
    return it == o.obj.end() ? nullptr : &it->second;
}

std::string str(const Value& o, const char* k) {
    const Value* v = field(o, k);
    return v && v->kind == Value::String ? v->str : std::string();
}

}  // namespace d2k::formats::json
