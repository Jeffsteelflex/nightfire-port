// Os arquivos BIGF e o RefPack (bigf.h; leitor só do port, ver o cabeçalho).

#include "driving/bigf.h"

#include <algorithm>
#include <cctype>

namespace drv {

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

std::string lower_slash(std::string s) {
    for (char& c : s) {
        if (c == '/') c = '\\';
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

}  // namespace

bool refpack_is(const uint8_t* data, size_t size) { return size >= 5 && data[1] == 0xFB && (data[0] & 0x3E) == 0x10; }

bool refpack_decode(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    out.clear();
    if (!refpack_is(src, n)) return false;
    const uint8_t flags = src[0];
    const size_t nsize = (flags & 0x80) ? 4 : 3;
    size_t p = 2;
    if (p + nsize > n) return false;
    uint32_t size = 0;
    for (size_t i = 0; i < nsize; ++i) size = size << 8 | src[p + i];
    p += nsize;
    if (flags & 0x01) p += nsize;  // o tamanho comprimido, que o descompressor não usa
    out.reserve(size);
    auto literal = [&](size_t count) {
        if (p + count > n) return false;
        out.insert(out.end(), src + p, src + p + count);
        p += count;
        return true;
    };
    while (p < n) {
        const uint8_t b0 = src[p];
        size_t lit = 0, cnt = 0, off = 0;
        if (b0 < 0x80) {
            if (p + 2 > n) return false;
            const uint8_t b1 = src[p + 1];
            lit = b0 & 3;
            cnt = ((b0 >> 2) & 7) + 3;
            off = ((b0 & 0x60) << 3) + b1 + 1;
            p += 2;
        } else if (b0 < 0xC0) {
            if (p + 3 > n) return false;
            const uint8_t b1 = src[p + 1], b2 = src[p + 2];
            lit = (b1 >> 6) & 3;
            cnt = (b0 & 0x3F) + 4;
            off = ((b1 & 0x3F) << 8) + b2 + 1;
            p += 3;
        } else if (b0 < 0xE0) {
            if (p + 4 > n) return false;
            const uint8_t b1 = src[p + 1], b2 = src[p + 2], b3 = src[p + 3];
            lit = b0 & 3;
            cnt = ((b0 & 0x0C) << 6) + b3 + 5;
            off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
            p += 4;
        } else if (b0 < 0xFC) {
            lit = ((b0 & 0x1F) << 2) + 4;
            p += 1;
        } else {  // fim, com até 3 literais
            p += 1;
            if (!literal(b0 & 3)) return false;
            break;
        }
        if (!literal(lit)) return false;
        if (cnt) {
            if (off > out.size()) return false;
            const size_t start = out.size() - off;
            for (size_t i = 0; i < cnt; ++i) out.push_back(out[start + i]);  // cópia sobreposta, byte a byte
        }
    }
    return out.size() == size;
}

Bigf::~Bigf() = default;

bool Bigf::parse_directory(const uint8_t* head, size_t size, std::vector<BigfEntry>& out) {
    out.clear();
    if (size < 16) return false;
    if (std::string(reinterpret_cast<const char*>(head), 4) != "BIGF" &&
        std::string(reinterpret_cast<const char*>(head), 4) != "BIG4")
        return false;
    const uint32_t count = be32(head + 8);
    size_t p = 16;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 9 > size) return false;
        BigfEntry e;
        e.offset = be32(head + p);
        e.size = be32(head + p + 4);
        p += 8;
        while (p < size && head[p]) e.name.push_back(char(head[p++]));
        if (p >= size) return false;
        ++p;
        out.push_back(std::move(e));
    }
    return true;
}

bool Bigf::open(const std::string& path) {
    // O caminho do PC em UTF-8 (como o fopen_s de antes, que o lia na página de código do sistema: os nomes são ASCII).
    std::wstring w(path.begin(), path.end());
    return open_file(nf::DiscFile::open_host(w), path);
}

bool Bigf::open(const nf::DiscSource& disc, const std::string& path) { return open_file(disc.open(path), path); }

bool Bigf::open_file(nf::DiscFile file, const std::string& path) {
    f_ = std::move(file);
    entries_.clear();
    path_ = path;
    if (!f_.is_open()) return false;
    uint8_t h[16];
    if (f_.read_at(0, h, 16) != 16) return false;
    const uint32_t header_size = be32(h + 12);
    if (header_size < 16 || header_size > (64u << 20)) return false;
    std::vector<uint8_t> head(header_size);
    if (f_.read_at(0, head.data(), head.size()) != head.size()) return false;
    return parse_directory(head.data(), head.size(), entries_);
}

int Bigf::find(const std::string& key) const {
    const std::string k = lower_slash(key);
    for (size_t i = 0; i < entries_.size(); ++i)
        if (lower_slash(entries_[i].name).find(k) != std::string::npos) return int(i);
    return -1;
}

bool Bigf::read_raw(int index, std::vector<uint8_t>& out) const {
    out.clear();
    if (!f_.is_open() || index < 0 || index >= int(entries_.size())) return false;
    const BigfEntry& e = entries_[index];
    out.resize(e.size);
    return f_.read_at(e.offset, out.data(), out.size()) == out.size();
}

bool Bigf::read(int index, std::vector<uint8_t>& out) const {
    std::vector<uint8_t> raw;
    if (!read_raw(index, raw)) return false;
    if (refpack_is(raw.data(), raw.size())) return refpack_decode(raw.data(), raw.size(), out);
    out = std::move(raw);
    return true;
}

std::string bigf_base_name(const std::string& name) {
    size_t s = name.find_last_of("\\/");
    std::string b = s == std::string::npos ? name : name.substr(s + 1);
    const size_t d = b.find_last_of('.');
    if (d != std::string::npos) b.resize(d);
    return b;
}

}  // namespace drv
