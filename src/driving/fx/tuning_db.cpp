// O banco de ajustes da EA (driving/fx/tuning_db.h).

#include "driving/fx/tuning_db.h"

#include <cstdio>
#include <cstring>

#include "platform/crt_compat.h"

namespace drv {

// [driving] 0x001790f8 OptionParser::OptionParser
bool OptionParser_FindValue(const char* text, const char* key, std::string& value) {
    if (!text || !key) return false;
    const size_t n = std::strlen(key);
    const char* hay = text;
    while (*hay != '\0') {
        const char* p = std::strstr(hay, key);
        if (p == nullptr) return false;
        if (p != text && p[-1] != '\n' && p[-1] != '\r') {  // no meio de uma linha: procura de novo um adiante
            hay = p + 1;
            continue;
        }
        // No começo de uma linha: só vale seguida de ' ' ou '=' (senão procura de novo um adiante).
        if (p[n] != ' ' && p[n] != '=') {
            hay = p + 1;
            continue;
        }
        const char* v = p + n;
        while (*v == ' ' || *v == '=' || *v == '#' || *v == '(' || *v == '[') ++v;
        const char* nl = std::strchr(v, '\n');
        size_t len = nl ? size_t(nl - v) : std::strlen(v);
        if (len != 0) {
            if (v[len - 1] == ']') --len;
            // O PS2 lê v[len - 1] mesmo com len 0 (o caractere antes do valor: um dos pulados, nunca ')').
            if (len != 0 && v[len - 1] == ')') --len;
        }
        // [driving] 0x00179508 OptionParser::GetFullString: strncpy do valor.
        value.assign(v, len);
        return true;
    }
    return false;
}

// [driving] 0x001440e0 (sem os espaços)
std::string tuning_strip_spaces(const char* s) {
    std::string o;
    for (; *s; ++s)
        if (*s != ' ') o.push_back(*s);
    return o;
}

// [driving] 0x00144548 DTuningFile::Reader::FindItem(char *)
bool DTuningFile_FindItem(const TuningReader& r, const char* name, std::string& value) {
    if (r.text.empty()) return false;
    return OptionParser_FindValue(r.text.c_str(), tuning_strip_spaces(name).c_str(), value);
}

// [driving] 0x00144648 DTuningFile::Reader::FindIndexedItem(char *, char *)
bool DTuningFile_FindIndexedItem(const TuningReader& r, const char* name, const char* index, std::string& value) {
    if (r.text.empty()) return false;
    const std::string key = std::string(name) + "{" + index + "}";
    return OptionParser_FindValue(r.text.c_str(), tuning_strip_spaces(key.c_str()).c_str(), value);
}

// [driving] 0x00144740 DTuningFile::ParseData_Colour
void DTuningFile_ParseData_Colour(const std::string* value, uint32_t& out) {
    if (!value) return;
    // Divergência: o PS2 usa o lixo da pilha nos números que o sscanf não leu; o port, zero (os .tun têm os quatro).
    int c[4] = {0, 0, 0, 0};
    NF_SSCANF(value->c_str(), "%d,%d,%d,%d", &c[0], &c[1], &c[2], &c[3]);
    out = uint32_t(c[0]) << 24 | uint32_t(c[3]) << 16 | uint32_t(c[2]) << 8 | uint32_t(c[1]);
}

namespace {

// O laço comum dos dbattrib_*: o valor de cada índice (ou o único, sem índice) passado a `parse`.
template <typename Parse>
void each_value(const TuningReader& r, const TuningIndex* ix, const char* name, uint8_t* first, uint32_t stride,
                Parse parse) {
    std::string v;
    if (!ix) {
        const bool found = DTuningFile_FindItem(r, name, v);
        parse(found ? &v : nullptr, first);
        return;
    }
    for (int32_t i = ix->lo; i <= ix->hi; ++i) {
        // A entrada +0x54 da tabela virtual do índice: o nome da tabela, ou o número.
        const std::string idx = ix->names ? std::string(ix->names[i]) : std::to_string(i);
        const bool found = DTuningFile_FindIndexedItem(r, name, idx.c_str(), v);
        parse(found ? &v : nullptr, first + size_t(i) * stride);
    }
}

}  // namespace

// [driving] 0x00143ad8 dbattrib_argb (ParseData_Colour)
void dbattrib_colour(const TuningReader& r, const TuningIndex* ix, const char* name, uint8_t* first, uint32_t stride) {
    each_value(r, ix, name, first, stride, [](const std::string* v, uint8_t* dst) {
        if (!v) return;
        uint32_t c;
        std::memcpy(&c, dst, 4);
        DTuningFile_ParseData_Colour(v, c);
        std::memcpy(dst, &c, 4);
    });
}

// [driving] 0x00142088 dbattrib_s8 (e 0x00142398 dbattrib_u8)
void dbattrib_int(const TuningReader& r, const TuningIndex* ix, const char* name, uint8_t* first, uint32_t stride) {
    each_value(r, ix, name, first, stride, [](const std::string* v, uint8_t* dst) {
        if (!v) return;
        int32_t x;
        std::memcpy(&x, dst, 4);
        NF_SSCANF(v->c_str(), "%d", &x);
        std::memcpy(dst, &x, 4);
    });
}

// [driving] 0x001426a8 dbattrib_float
void dbattrib_float(const TuningReader& r, const TuningIndex* ix, const char* name, uint8_t* first, uint32_t stride) {
    each_value(r, ix, name, first, stride, [](const std::string* v, uint8_t* dst) {
        if (!v) return;
        float x;
        std::memcpy(&x, dst, 4);
        NF_SSCANF(v->c_str(), "%f", &x);
        std::memcpy(dst, &x, 4);
    });
}

}  // namespace drv
