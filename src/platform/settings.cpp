// Configuração do port no PC: o INI nightfire.ini. Ver settings.h.

#include "platform/settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "platform/os.h"

namespace nf {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(uint8_t(s[a]))) ++a;
    while (b > a && std::isspace(uint8_t(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

bool Settings::load(const std::wstring& path) {
    path_ = path;
    lines_.clear();
    dirty_ = false;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    parse(ss.str());
    dirty_ = false;
    return true;
}

void Settings::parse(const std::string& text) {
    lines_.clear();
    std::string section;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string raw = text.substr(pos, end - pos);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        Line line;
        line.raw = raw;
        const std::string t = trim(raw);
        if (!t.empty() && t.front() == '[' && t.back() == ']') {
            section = lower(trim(t.substr(1, t.size() - 2)));
        } else if (!t.empty() && t.front() != ';' && t.front() != '#') {
            const size_t eq = t.find('=');
            if (eq != std::string::npos) {
                line.name = trim(t.substr(0, eq));
                line.key = lower(line.name);
                line.value = trim(t.substr(eq + 1));
            }
        }
        line.section = section;
        lines_.push_back(line);
        if (end == text.size()) break;
        pos = end + 1;
    }
    // Sem linha final vazia guardada: o texto gravado sempre termina em \n.
    while (!lines_.empty() && lines_.back().raw.empty() && lines_.back().key.empty()) lines_.pop_back();
    dirty_ = true;
}

std::string Settings::text() const {
    std::string out;
    for (const Line& line : lines_) {
        out += line.key.empty() ? line.raw : line.name + " = " + line.value;
        out += "\r\n";
    }
    return out;
}

bool Settings::save() const {
    if (path_.empty()) return false;
    std::ofstream out(std::filesystem::path(path_), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const std::string t = text();
    out.write(t.data(), std::streamsize(t.size()));
    if (!out) return false;
    const_cast<Settings*>(this)->dirty_ = false;
    return true;
}

int32_t Settings::find(const std::string& section, const std::string& key) const {
    const std::string s = lower(section);
    const std::string k = lower(key);
    for (size_t i = lines_.size(); i-- > 0;) {
        if (lines_[i].section == s && lines_[i].key == k) return int32_t(i);
    }
    return -1;
}

bool Settings::has(const std::string& section, const std::string& key) const { return find(section, key) >= 0; }

std::string Settings::get(const std::string& section, const std::string& key, const std::string& fallback) const {
    const int32_t i = find(section, key);
    return i < 0 ? fallback : lines_[size_t(i)].value;
}

int32_t Settings::get_int(const std::string& section, const std::string& key, int32_t fallback) const {
    const int32_t i = find(section, key);
    if (i < 0) return fallback;
    const std::string& v = lines_[size_t(i)].value;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 0);
    return end != v.c_str() ? int32_t(n) : fallback;
}

float Settings::get_float(const std::string& section, const std::string& key, float fallback) const {
    const int32_t i = find(section, key);
    if (i < 0) return fallback;
    const std::string& v = lines_[size_t(i)].value;
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    return end != v.c_str() ? f : fallback;
}

bool Settings::get_bool(const std::string& section, const std::string& key, bool fallback) const {
    const int32_t i = find(section, key);
    if (i < 0) return fallback;
    const std::string v = lower(lines_[size_t(i)].value);
    if (v == "1" || v == "true" || v == "yes" || v == "on" || v == "sim") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off" || v == "nao" || v == "não") return false;
    return fallback;
}

void Settings::set(const std::string& section, const std::string& key, const std::string& value) {
    const int32_t i = find(section, key);
    if (i >= 0) {
        if (lines_[size_t(i)].value != value) {
            lines_[size_t(i)].value = value;
            dirty_ = true;
        }
        return;
    }
    const std::string s = lower(section);
    Line line;
    line.section = s;
    line.key = lower(key);
    line.name = key;
    line.value = value;
    // Depois da última linha da seção que não seja vazia; sem a seção, uma seção nova no fim.
    int32_t last = -1;
    for (size_t j = 0; j < lines_.size(); ++j) {
        // Uma chave criada por set() não tem raw: conta também (senão as chaves novas saem na ordem inversa).
        if (lines_[j].section == s && (!lines_[j].key.empty() || !trim(lines_[j].raw).empty())) last = int32_t(j);
    }
    if (last < 0) {
        if (!lines_.empty()) {
            Line blank;
            blank.section = lines_.back().section;
            lines_.push_back(blank);
        }
        Line header;
        header.raw = "[" + section + "]";
        header.section = s;
        lines_.push_back(header);
        lines_.push_back(line);
    } else {
        lines_.insert(lines_.begin() + last + 1, line);
    }
    dirty_ = true;
}

void Settings::set_int(const std::string& section, const std::string& key, int32_t value) {
    set(section, key, std::to_string(value));
}

void Settings::set_float(const std::string& section, const std::string& key, float value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", double(value));
    set(section, key, buf);
}

void Settings::set_bool(const std::string& section, const std::string& key, bool value) {
    set(section, key, value ? "1" : "0");
}

void Settings::remove(const std::string& section, const std::string& key) {
    const int32_t i = find(section, key);
    if (i < 0) return;
    lines_.erase(lines_.begin() + i);
    dirty_ = true;
}

Settings& settings() {
    static Settings s;
    return s;
}

std::wstring default_settings_path() {
    std::wstring dir = os::exe_path();
    const size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
    return dir + L"nightfire.ini";
}

}  // namespace nf
