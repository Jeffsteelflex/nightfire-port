// Imagens 2D de um pacote de fase. Ver pack_images.h.

#include "data/pack_images.h"

#include <algorithm>

#include "data/level_pak.h"

namespace nf {

std::map<uint32_t, Texture> decode_pack_images(ByteView pak, const std::vector<uint32_t>& want) {
    std::map<uint32_t, Texture> out;
    if (want.empty()) return out;
    std::vector<PakItem> items;
    try {
        items = parse_pak(pak);
    } catch (const FormatError&) {
        return out;
    }
    for (const PakItem& it : items) {
        if (!it.is_map() || out.size() == want.size()) continue;
        try {
            const ByteView item = it.view(pak);
            const std::vector<Block> blocks = read_blocks(item);
            const Block* tex_header = nullptr;
            const Block* pal_header = nullptr;
            std::vector<const Block*> pixels, palettes;
            for (const Block& b : blocks) {
                if (b.id == block_id::kTextureHeader && tex_header == nullptr) tex_header = &b;
                if (b.id == block_id::kPaletteHeader && pal_header == nullptr) pal_header = &b;
                if (b.id == 0x15 || b.id == block_id::kTextureData) pixels.push_back(&b);
                if (b.id == 0x11 || b.id == block_id::kPaletteData) palettes.push_back(&b);
            }
            if (tex_header == nullptr || pal_header == nullptr) continue;
            const ByteView th = tex_header->payload(item);
            const ByteView ph = pal_header->payload(item);
            const size_t count = std::min({th.size() / 12, ph.size() / 8, pixels.size(), palettes.size()});
            for (size_t i = 0; i < count; ++i) {
                const uint32_t hash = u32(ph, i * 8 + 4);
                if (std::find(want.begin(), want.end(), hash) == want.end() || out.count(hash) != 0) continue;
                const uint8_t flags = u8(th, i * 12);
                const int w = u16(th, i * 12 + 2) + 1;
                const int h = u16(th, i * 12 + 4) + 1;
                const int frames = std::max(1, int(u16(th, i * 12 + 6) & 0xFF));
                Texture t;
                if (decode_texture(w, h, frames, (flags & 1) != 0, pixels[i]->payload(item), palettes[i]->payload(item), t)) {
                    out.emplace(hash, std::move(t));
                }
            }
        } catch (const FormatError&) {
            continue;
        }
    }
    return out;
}

}  // namespace nf
