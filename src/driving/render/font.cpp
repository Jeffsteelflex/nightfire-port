// As fontes FNTS (font.h; leitor só do port, ver o cabeçalho).

#include "driving/render/font.h"

#include <cstring>

#include "driving/shps.h"

namespace drv {

namespace {

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

}  // namespace

const FontGlyph* FontFile::find(uint16_t code) const {
    for (const FontGlyph& g : glyphs)
        if (g.code == code) return &g;
    return nullptr;
}

int FontFile::width(const std::string& text) const {
    const FontGlyph* space = find(' ');
    int w = 0;
    for (unsigned char c : text) {
        const FontGlyph* g = find(c);
        if (!g) g = space;
        if (g) w += g->advance;
    }
    return w;
}

bool font_parse(const uint8_t* data, size_t size, FontFile& out) {
    out = FontFile();
    if (size < 0x48 || std::memcmp(data, "FNTS", 4) != 0) return false;
    const uint32_t count = rd16(data + 0xa);
    const uint32_t table = rd32(data + 0x14), image = rd32(data + 0x1c);
    out.line_height = int(rd32(data + 0x24));
    if (table + size_t(count) * 12 > size || image + 16 > size) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = data + table + i * 12;
        FontGlyph g;
        g.code = rd16(e);
        g.width = e[2];
        g.height = e[3];
        g.x = rd16(e + 4);
        g.y = rd16(e + 6);
        g.advance = int8_t(e[8]);
        g.offset_x = int8_t(e[9]);
        g.offset_y = int8_t(e[10]);
        out.glyphs.push_back(g);
    }
    // A imagem: os blocos do SHPS a partir de `image`, num SHPS de uma imagem montado em volta deles.
    std::vector<uint8_t> shps(0x18 + (size - image));
    std::memcpy(shps.data(), "SHPS", 4);
    wr32(shps.data() + 4, uint32_t(shps.size()));
    wr32(shps.data() + 8, 1);
    std::memcpy(shps.data() + 12, "G354", 4);
    std::memcpy(shps.data() + 16, "font", 4);
    wr32(shps.data() + 20, 0x18);
    std::memcpy(shps.data() + 0x18, data + image, size - image);
    ShpsFile f;
    if (!shps_parse(shps.data(), shps.size(), f) || f.images.empty() || f.images[0].rgba.empty()) return false;
    out.image_width = f.images[0].width;
    out.image_height = f.images[0].height;
    out.rgba = std::move(f.images[0].rgba);
    return true;
}

std::vector<FontQuad> font_layout(const FontFile& f, const std::string& text, float x, float y, float scale) {
    std::vector<FontQuad> q;
    if (f.image_width <= 0 || f.image_height <= 0) return q;
    const FontGlyph* space = f.find(' ');
    float pen = x;
    for (unsigned char c : text) {
        const FontGlyph* g = f.find(c);
        if (!g) g = space;
        if (!g) continue;
        if (c != ' ' && g->width > 0 && g->height > 0) {
            FontQuad r;
            r.x0 = pen + float(g->offset_x) * scale;
            r.y0 = y + float(g->offset_y) * scale;
            r.x1 = r.x0 + float(g->width) * scale;
            r.y1 = r.y0 + float(g->height) * scale;
            r.u0 = float(g->x) / float(f.image_width);
            r.v0 = float(g->y) / float(f.image_height);
            r.u1 = float(g->x + g->width) / float(f.image_width);
            r.v1 = float(g->y + g->height) / float(f.image_height);
            q.push_back(r);
        }
        pen += float(g->advance) * scale;
    }
    return q;
}

}  // namespace drv
