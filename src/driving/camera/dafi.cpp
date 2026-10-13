// O DAFI e o IniFiles (driving/camera/dafi.h).

#include "driving/camera/dafi.h"

#include <atomic>
#include <climits>
#include <cstdlib>
#include <cstring>

namespace drv {

namespace {

std::atomic<uint64_t> g_dafi_guards{0};

bool line_end(char c) { return c == '\n' || c == '\r' || c == 0; }

}  // namespace

uint64_t dafi_guards() { return g_dafi_guards.load(); }

int dafi_strcasecmp(const char* a, const char* b) {
    for (;;) {
        int ca = static_cast<unsigned char>(*a++);
        int cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb || ca == 0) return ca - cb;
    }
}

float dafi_atof(const char* s) {
    const double d = std::strtod(s, nullptr);
    return static_cast<float>(d);
}

int32_t dafi_atoi(const char* s) {
    const long long v = std::strtoll(s, nullptr, 10);
    // O atoi da newlib é (int)strtol: o long de 32 bits do EE satura nos limites.
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return static_cast<int32_t>(v);
}

int32_t Dafi::next_line(int32_t p) const {
    if (p < end_ && !line_end(at(p))) {
        for (;;) {
            ++p;
            if (p >= end_) break;
            if (line_end(at(p))) break;
        }
    }
    for (; p < end_; ++p) {
        const char c = at(p);
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t' && c != 0) return p;
    }
    return -1;
}

int32_t Dafi::trim(int32_t p) {
    int32_t e = p + int32_t(std::strlen(&buffer_[size_t(p)])) - 1;  // o fim medido antes de pular o começo
    while (at(p) == ' ' || at(p) == '\t') ++p;
    while (p < e && (at(e) == ' ' || at(e) == '\t')) {
        buffer_[size_t(e)] = 0;
        --e;
    }
    return p;
}

void Dafi::open(const char* data, uint32_t size) {
    buffer_.assign(data, data + size);
    buffer_.push_back(0);
    end_ = int32_t(size);
    count_ = 0;
    current_ = -1;
    key_count_ = 0;
    for (int i = 0; i < kMaxEntries; ++i) names_[i] = lines_[i] = keys_[i] = values_[i] = -1;
    int32_t p = 0;
    while (p >= 0 && count_ < kMaxEntries) {
        if (at(p) != '[') {
            p = next_line(p);
            continue;
        }
        names_[count_] = ++p;
        // O nome até o ']' (nem quebra de linha nem o fim do buffer).
        while (at(p) != ']' && !line_end(at(p))) ++p;
        if (at(p) != ']') {
            names_[count_] = -1;  // um cabeçalho sem ']' não conta (p fica na quebra: a próxima volta pula a linha)
            continue;
        }
        buffer_[size_t(p)] = 0;
        p = next_line(p);
        lines_[count_] = p;
        names_[count_] = trim(names_[count_]);
        ++count_;
    }
    current_ = -1;
}

const char* Dafi::section_by_index(int32_t index) const {
    if (index == -1) index = current_;
    if (index < 0 || index >= kMaxEntries || names_[index] < 0) return nullptr;
    return &buffer_[size_t(names_[index])];
}

int32_t Dafi::section_index(const char* name) const {
    if (name == nullptr) return current_;
    for (int32_t i = 0; i < count_; ++i)
        if (names_[i] >= 0 && dafi_strcasecmp(&buffer_[size_t(names_[i])], name) == 0) return i;
    return -1;
}

int32_t Dafi::set_section(const char* name) {
    const int32_t index = section_index(name);
    if (index >= 0) set_section_by_index(index);
    return index;
}

void Dafi::set_section_by_index(int32_t index) {
    current_ = index;
    key_count_ = 0;
    int32_t p = lines_[index];
    if (p < 0 || at(p) == '[' || p >= end_) return;
    for (;;) {
        keys_[key_count_] = p;
        // A chave: até o '=', a quebra de linha ou um zero (o '=' que um recorte anterior apagou).
        while (at(p) != '=' && !line_end(at(p)) && p < end_) ++p;
        if (at(p) == '\n' || at(p) == '\r') {
            // Uma linha sem '=': o PS2 fica aqui para sempre (keys[n] = 0 e a mesma linha de novo).
            keys_[key_count_] = -1;
            g_dafi_guards.fetch_add(1);
            p = next_line(p);
        } else {
            buffer_[size_t(p)] = 0;
            ++p;
            keys_[key_count_] = trim(keys_[key_count_]);
            // Pula os zeros (os de um recorte anterior).
            if (at(p) == 0 && p < end_) {
                ++p;
                while (at(p) == 0 && p < end_) ++p;
            }
            values_[key_count_] = p;
            if (line_end(at(p)) && p < end_) {
                buffer_[size_t(p)] = ' ';  // um valor vazio engole a linha seguinte (peculiaridade do original)
                ++p;
            }
            while (!line_end(at(p)) && at(p) != ';' && p < end_) ++p;
            buffer_[size_t(p)] = 0;
            values_[key_count_] = trim(values_[key_count_]);
            p = next_line(p);
            ++key_count_;
        }
        if (p < 0 || at(p) == '[' || key_count_ >= kMaxEntries || p >= end_) return;
    }
}

int32_t Dafi::key_index(const char* key) const {
    for (int32_t i = 0; i < key_count_; ++i)
        if (keys_[i] >= 0 && dafi_strcasecmp(&buffer_[size_t(keys_[i])], key) == 0) return i;
    return -1;
}

const char* Dafi::value_by_index(int32_t index) const {
    return values_[index] < 0 ? nullptr : &buffer_[size_t(values_[index])];
}

const char* Dafi::value(const char* key) const {
    const int32_t index = key_index(key);
    return index < 0 ? nullptr : value_by_index(index);
}

void IniFile::open(const std::vector<uint8_t>& bytes) {
    dafi_.open(reinterpret_cast<const char*>(bytes.data()), uint32_t(bytes.size()));
}

float IniFile::read_float(const char* section, const char* key, float fallback) {
    if (dafi_.section_index(section) < 0) return fallback;
    dafi_.set_section(section);
    if (dafi_.key_index(key) < 0) return fallback;
    const char* v = get_value(key);
    if (v == nullptr) return fallback;
    return dafi_atof(v);
}

int32_t IniFile::read_integer(const char* section, const char* key, int32_t fallback) {
    if (dafi_.section_index(section) < 0) return fallback;
    dafi_.set_section(section);
    if (dafi_.key_index(key) < 0) return fallback;
    const char* v = get_value(key);
    if (v == nullptr) return fallback;
    return dafi_atoi(v);
}

const char* IniFile::read_string(const char* section, const char* key, const char* fallback) {
    if (dafi_.section_index(section) < 0) return fallback;
    dafi_.set_section(section);
    if (dafi_.key_index(key) < 0) return fallback;
    return get_value(key);
}

bool IniFile::section_exists(const char* section) const { return dafi_.section_index(section) >= 0; }

}  // namespace drv
