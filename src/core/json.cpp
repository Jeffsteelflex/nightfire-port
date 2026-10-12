// JSON mínimo. Ver json.h.

#include "core/json.h"

#include <cstdlib>
#include <cstring>

namespace nf {

namespace {

class JParser {
public:
    explicit JParser(const std::string& text) : t_(text) {}
    bool parse(JVal& out, std::string* error) {
        if (!value(out, 0)) {
            if (error != nullptr) *error = err_;
            return false;
        }
        ws();
        if (p_ != t_.size()) {
            fail("texto depois do fim");
            if (error != nullptr) *error = err_;
            return false;
        }
        return true;
    }

private:
    bool fail(const char* what) {
        if (err_.empty()) {
            int line = 1;
            for (size_t i = 0; i < p_ && i < t_.size(); ++i) line += t_[i] == '\n';
            err_ = std::string(what) + " (linha " + std::to_string(line) + ")";
        }
        return false;
    }
    void ws() {
        while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\r' || t_[p_] == '\n')) ++p_;
    }
    bool lit(const char* word) {
        const size_t n = std::strlen(word);
        if (t_.compare(p_, n, word) != 0) return false;
        p_ += n;
        return true;
    }
    static void put_utf8(std::string& s, uint32_t c) {
        if (c < 0x80) {
            s.push_back(char(c));
        } else if (c < 0x800) {
            s.push_back(char(0xc0 | (c >> 6)));
            s.push_back(char(0x80 | (c & 0x3f)));
        } else if (c < 0x10000) {
            s.push_back(char(0xe0 | (c >> 12)));
            s.push_back(char(0x80 | ((c >> 6) & 0x3f)));
            s.push_back(char(0x80 | (c & 0x3f)));
        } else {
            s.push_back(char(0xf0 | (c >> 18)));
            s.push_back(char(0x80 | ((c >> 12) & 0x3f)));
            s.push_back(char(0x80 | ((c >> 6) & 0x3f)));
            s.push_back(char(0x80 | (c & 0x3f)));
        }
    }
    bool hex4(uint32_t& v) {
        if (p_ + 4 > t_.size()) return fail("\\u incompleto");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return fail("\\u inválido");
        }
        return true;
    }
    bool string(std::string& out) {
        if (p_ >= t_.size() || t_[p_] != '"') return fail("esperava uma string");
        ++p_;
        while (p_ < t_.size()) {
            const char c = t_[p_++];
            if (c == '"') return true;
            if (uint8_t(c) < 0x20) return fail("caractere de controle numa string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (p_ >= t_.size()) break;
            const char e = t_[p_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t v;
                    if (!hex4(v)) return false;
                    if (v >= 0xd800 && v < 0xdc00 && t_.compare(p_, 2, "\\u") == 0) {
                        p_ += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return false;
                        v = 0x10000 + ((v - 0xd800) << 10) + (lo - 0xdc00);
                    }
                    put_utf8(out, v);
                    break;
                }
                default: return fail("escape inválido");
            }
        }
        return fail("string sem fim");
    }
    bool value(JVal& v, int depth) {
        if (depth > 64) return fail("aninhamento demais");
        ws();
        if (p_ >= t_.size()) return fail("fim inesperado");
        const char c = t_[p_];
        if (c == '{') {
            v.type = JVal::Obj;
            ++p_;
            ws();
            if (p_ < t_.size() && t_[p_] == '}') {
                ++p_;
                return true;
            }
            for (;;) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (p_ >= t_.size() || t_[p_] != ':') return fail("esperava ':'");
                ++p_;
                JVal item;
                if (!value(item, depth + 1)) return false;
                v.o.emplace_back(std::move(key), std::move(item));
                ws();
                if (p_ < t_.size() && t_[p_] == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < t_.size() && t_[p_] == '}') {
                    ++p_;
                    return true;
                }
                return fail("esperava ',' ou '}'");
            }
        }
        if (c == '[') {
            v.type = JVal::Arr;
            ++p_;
            ws();
            if (p_ < t_.size() && t_[p_] == ']') {
                ++p_;
                return true;
            }
            for (;;) {
                JVal item;
                if (!value(item, depth + 1)) return false;
                v.a.push_back(std::move(item));
                ws();
                if (p_ < t_.size() && t_[p_] == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < t_.size() && t_[p_] == ']') {
                    ++p_;
                    return true;
                }
                return fail("esperava ',' ou ']'");
            }
        }
        if (c == '"') {
            v.type = JVal::Str;
            return string(v.s);
        }
        if (lit("true")) {
            v.type = JVal::Bool;
            v.b = true;
            return true;
        }
        if (lit("false")) {
            v.type = JVal::Bool;
            return true;
        }
        if (lit("null")) return true;
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* begin = t_.c_str() + p_;
            char* end = nullptr;
            v.type = JVal::Num;
            v.n = std::strtod(begin, &end);
            if (end == begin) return fail("número inválido");
            p_ += size_t(end - begin);
            return true;
        }
        return fail("valor inválido");
    }

    const std::string& t_;
    size_t p_ = 0;
    std::string err_;
};

}  // namespace

bool json_parse(const std::string& text, JVal& out, std::string* error) {
    JParser parser(text);
    return parser.parse(out, error);
}

std::wstring widen_utf8(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        uint32_t v = c;
        int extra = 0;
        if (c >= 0xf0) {
            v = c & 0x07;
            extra = 3;
        } else if (c >= 0xe0) {
            v = c & 0x0f;
            extra = 2;
        } else if (c >= 0xc0) {
            v = c & 0x1f;
            extra = 1;
        }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i) v = (v << 6) | (uint8_t(s[i]) & 0x3f);
        if (v >= 0x10000) {
            v -= 0x10000;
            out.push_back(wchar_t(0xd800 + (v >> 10)));
            out.push_back(wchar_t(0xdc00 + (v & 0x3ff)));
        } else {
            out.push_back(wchar_t(v));
        }
    }
    return out;
}

}  // namespace nf
