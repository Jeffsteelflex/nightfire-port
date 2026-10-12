// O CARP como UGroup (carp.h).

#include "driving/carp.h"

#include <cstdio>
#include <cstring>

namespace drv {

namespace {

std::string name_of(uint32_t tag, bool short_tag) {
    if (short_tag) {
        char b[16];
        std::snprintf(b, sizeof b, "%c%c#%04x", char(tag >> 24), char(tag >> 16), tag & 0xFFFF);
        return b;
    }
    std::string s;
    for (int i = 3; i >= 0; --i) {
        const char c = char(tag >> (i * 8));
        s.push_back(c >= 32 && c < 127 ? c : '?');
    }
    return s;
}

uint32_t rd(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// [driving] 0x002d6940 UGroup::GetArray
size_t group_array(size_t pos, uint32_t flags, uint32_t offset) { return (flags & 2) ? pos + size_t(offset) * 16 : offset; }

// Um grupo e o que ele contém; `depth` evita laços num arquivo estragado.
bool read_group(const uint8_t* f, size_t size, size_t pos, int depth, CarpGroup& g, std::vector<CarpEntry>& all) {
    if (pos + 16 > size || depth > 16) return false;
    g.pos = pos;
    g.tag = rd(f + pos);
    g.flags = rd(f + pos + 4);
    g.count = rd(f + pos + 8);
    g.offset = rd(f + pos + 12);
    g.array = group_array(pos, g.flags, g.offset);
    const size_t ngroups = g.flags >> 5;
    if (g.array + (ngroups + g.count) * 16 > size) return false;
    for (uint32_t i = 0; i < g.count; ++i) {
        CarpEntry e;
        e.pos = g.array + (ngroups + i) * 16;
        e.tag = rd(f + e.pos);
        e.flags = rd(f + e.pos + 4);
        e.count = rd(f + e.pos + 8);
        e.offset = rd(f + e.pos + 12);
        e.data = (e.flags & 2) ? e.pos + e.offset : e.offset;
        g.data.push_back(e);
        all.push_back(e);
    }
    for (size_t i = 0; i < ngroups; ++i) {
        CarpGroup sub;
        if (!read_group(f, size, g.array + i * 16, depth + 1, sub, all)) return false;
        g.groups.push_back(std::move(sub));
    }
    return true;
}

}  // namespace

std::string CarpEntry::tag_name() const { return name_of(tag, short_tag()); }
std::string CarpGroup::tag_name() const { return name_of(tag, false); }

// [driving] 0x002d51f0 UGroup::GroupLocateTag
const CarpGroup* CarpGroup::group(uint32_t t) const {
    for (const CarpGroup& g : groups)
        if (g.tag == t) return &g;
    return nullptr;
}

// [driving] 0x002d5a10 UGroup::DataLocateTag
const CarpEntry* CarpGroup::find(uint32_t t) const {
    for (const CarpEntry& e : data)
        if (e.tag == t) return &e;
    return nullptr;
}

bool carp_parse(const uint8_t* data, size_t size, Carp& out) {
    out = Carp();
    if (size < 16 || std::memcmp(data, "PRAC", 4) != 0) return false;
    return read_group(data, size, 0, 0, out.root, out.entries);
}

std::vector<CarpInstance> carp_instances(const uint8_t* file, size_t file_size, const CarpGroup& g) {
    std::vector<CarpInstance> out;
    for (const CarpEntry& e : g.data) {
        if (!e.short_tag() || (e.tag >> 16) != 0x696E) continue;  // 'in'
        for (uint32_t i = 0; i < e.count && e.data + size_t(i + 1) * 64 <= file_size; ++i) {
            CarpInstance inst;
            uint32_t w[16];
            std::memcpy(w, file + e.data + size_t(i) * 64, 64);
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 3; ++col) std::memcpy(&inst.matrix[row * 4 + col], &w[row * 4 + col], 4);
            inst.flags = w[3];
            inst.piece = w[7];
            inst.extra[0] = w[11];
            inst.extra[1] = w[15];
            const CarpEntry* as = g.find(inst.piece);
            if (as && as->size() >= 40 && as->data + 40 <= file_size) {
                uint32_t sr_tag;
                std::memcpy(&sr_tag, file + as->data + 36, 4);
                if (const CarpEntry* sr = g.find(sr_tag)) {
                    inst.model = carp_string(file, file_size, *sr);
                    if (inst.model.rfind("EAGL::", 0) == 0) inst.model.erase(0, 6);
                }
            }
            out.push_back(inst);
        }
    }
    return out;
}

std::vector<std::vector<uint16_t>> carp_view_lists(const uint8_t* file, size_t file_size, const CarpGroup& g) {
    std::vector<std::vector<uint16_t>> out;
    for (const CarpEntry& e : g.data) {
        if (!e.short_tag() || (e.tag >> 16) != 0x7664) continue;  // 'vd'
        const unsigned v = e.tag & 0xFFFF;
        if (v > 64 || e.data + size_t(e.count) * 2 > file_size) continue;
        if (out.size() <= v) out.resize(v + 1);
        out[v].resize(e.count);
        std::memcpy(out[v].data(), file + e.data, size_t(e.count) * 2);
    }
    return out;
}

std::string carp_string(const uint8_t* file, size_t file_size, const CarpEntry& e) {
    if (e.data >= file_size) return std::string();
    size_t n = e.size();
    if (e.data + n > file_size) n = file_size - e.data;
    const char* s = reinterpret_cast<const char*>(file + e.data);
    return std::string(s, strnlen(s, n));
}

}  // namespace drv
