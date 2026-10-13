// Pacotes de texturas do PCSX2. Ver texture_pack.h.

#include "data/texture_pack.h"

#include <filesystem>

#include "core/xxh3.h"
#include "platform/os.h"

namespace nf {

namespace {

// Nas linhas 2-3 e 4-5 de cada grupo de 8 (alternando por coluna do bloco), as metades de 4
// texels trocam de lugar.
uint32_t half_swap(uint32_t x, uint32_t y) { return x ^ (4u * (((y >> 1) & 1u) ^ ((y >> 2) & 1u))); }

bool hex64(const std::wstring& s, uint64_t* out) {
    if (s.empty() || s.size() > 16) return false;
    uint64_t v = 0;
    for (wchar_t c : s) {
        uint32_t d;
        if (c >= L'0' && c <= L'9') {
            d = uint32_t(c - L'0');
        } else if (c >= L'a' && c <= L'f') {
            d = uint32_t(c - L'a' + 10);
        } else {
            return false;
        }
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

}  // namespace

uint32_t gs_block_offset8(uint32_t x, uint32_t y) {
    const uint32_t X = half_swap(x, y);
    return 64u * (y >> 2) + 8u * (y & 1u) + ((y >> 1) & 1u) + 2u * ((x >> 3) & 1u) + 4u * (X & 1u) +
           16u * ((X >> 1) & 3u);
}

uint32_t gs_block_offset4(uint32_t x, uint32_t y) {
    const uint32_t X = half_swap(x, y);
    return 128u * (y >> 2) + 16u * (y & 1u) + ((y >> 1) & 1u) + 2u * ((x >> 3) & 3u) + 8u * (X & 1u) +
           32u * ((X >> 1) & 3u);
}

uint64_t gs_texture_hash_linear(const uint8_t* indices, int width, int height) {
    return xxh3_64(indices, size_t(width) * size_t(height));
}

uint64_t gs_texture_hash_blocks(const uint8_t* indices, int width, int height, bool bpp8) {
    const int bw = bpp8 ? 16 : 32, bh = 16;
    if (width < bw || height < bh || width % bw != 0 || height % bh != 0) return 0;
    std::vector<uint8_t> blocks;
    blocks.reserve(size_t(width) * size_t(height) / (bpp8 ? 1 : 2));
    for (int by = 0; by < height / bh; ++by) {
        for (int bx = 0; bx < width / bw; ++bx) {
            uint8_t block[256] = {};
            for (int y = 0; y < bh; ++y) {
                const uint8_t* row = indices + size_t(by * bh + y) * size_t(width) + size_t(bx * bw);
                for (int x = 0; x < bw; ++x) {
                    if (bpp8) {
                        block[gs_block_offset8(uint32_t(x), uint32_t(y))] = row[x];
                    } else {
                        const uint32_t n = gs_block_offset4(uint32_t(x), uint32_t(y));
                        block[n >> 1] = uint8_t(block[n >> 1] | ((row[x] & 0xF) << (4 * (n & 1))));
                    }
                }
            }
            blocks.insert(blocks.end(), block, block + 256);
        }
    }
    return xxh3_64(blocks.data(), blocks.size());
}

uint64_t gs_clut_hash(const uint8_t* palette, int colors) { return xxh3_64(palette, size_t(colors) * 4); }

void unswizzle_csm1(const uint8_t* in, uint8_t* out) {
    for (int i = 0; i < 256; ++i) {
        int j = i;
        const int k = i & 31;
        if (k >= 8 && k < 16) {
            j = i + 8;
        } else if (k >= 16 && k < 24) {
            j = i - 8;
        }
        for (int c = 0; c < 4; ++c) out[i * 4 + c] = in[j * 4 + c];
    }
}

bool TexturePack::parse_name(const std::wstring& name, uint64_t* texture, uint64_t* clut) {
    const size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    // <textura>-[<paleta>-][r<região>-]<bits de 8 dígitos>
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (size_t i = 0; i <= dot; ++i) {
        if (i == dot || name[i] == L'-') {
            parts.push_back(name.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 2 || parts.back().size() != 8) return false;
    if (!hex64(parts[0], texture)) return false;
    *clut = 0;
    for (size_t i = 1; i + 1 < parts.size(); ++i) {
        if (!parts[i].empty() && parts[i][0] == L'r') continue;  // região
        if (!hex64(parts[i], clut)) return false;
    }
    return true;
}

bool TexturePack::open(const std::wstring& dir) {
    namespace fs = std::filesystem;
    by_texture_.clear();
    files_ = 0;
    dir_ = dir;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::wstring name = e.path().filename().wstring();
        if (e.path().extension() != L".dds") continue;  // os PNG (raros) ainda não são lidos
        uint64_t texture, clut;
        if (!parse_name(name, &texture, &clut)) continue;
        by_texture_[texture].push_back({clut, name});
        ++files_;
    }
    return true;
}

std::wstring TexturePack::find(int width, int height, bool bpp8, const uint8_t* pixels, const uint8_t* palette) const {
    if (files_ == 0 || width <= 0 || height <= 0) return {};
    const int colors = bpp8 ? 256 : 16;
    uint8_t logical[256 * 4];
    if (bpp8) {
        unswizzle_csm1(palette, logical);
    } else {
        std::copy(palette, palette + 16 * 4, logical);
    }
    const uint64_t clut = gs_clut_hash(logical, colors);
    std::vector<uint8_t> indices(size_t(width) * size_t(height));
    if (bpp8) {
        std::copy(pixels, pixels + indices.size(), indices.begin());
    } else {
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = uint8_t((pixels[i >> 1] >> (4 * (i & 1))) & 0xF);
    }
    const uint64_t candidates[2] = {gs_texture_hash_linear(indices.data(), width, height),
                                    gs_texture_hash_blocks(indices.data(), width, height, bpp8)};
    for (uint64_t h : candidates) {
        if (h == 0) continue;
        const auto it = by_texture_.find(h);
        if (it == by_texture_.end()) continue;
        for (const Entry& e : it->second) {
            if (e.clut == clut) return os::join(dir_, e.file);
        }
    }
    return {};
}

}  // namespace nf
