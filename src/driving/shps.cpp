// As texturas SHPS (shps.h; leitor só do port, ver o cabeçalho).

#include "driving/shps.h"

#include <cstring>

namespace drv {

namespace {

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

struct Block {
    int type = 0;
    size_t pos = 0;  // começo do cabeçalho de 16 bytes
    size_t end = 0;  // fim dos dados (o próximo bloco ou o fim do arquivo)
};

}  // namespace

const ShpsImage* ShpsFile::find(const std::string& name) const {
    for (const ShpsImage& im : images)
        if (im.name == name) return &im;
    return nullptr;
}

int shps_psmt8_swizzled_index(int x, int y, int width) {
    // O PSMT8 visto como PSMCT32: blocos de 16x16 pixels, e dentro de cada um as colunas de 4 linhas com os pares de
    // linhas intercalados e as metades trocadas a cada 4 linhas.
    const int block = (y & ~0xF) * width + (x & ~0xF) * 2;
    const int swap = (((y + 2) >> 2) & 1) * 4;
    const int posy = (((y & ~3) >> 1) + (y & 1)) & 7;
    const int column = posy * width * 2 + ((x + swap) & 7) * 4;
    const int byte = ((y >> 1) & 1) + ((x >> 2) & 2);
    return block + column + byte;
}

bool shps_parse(const uint8_t* data, size_t size, ShpsFile& out) {
    out = ShpsFile();
    if (size < 16 || std::memcmp(data, "SHPS", 4) != 0) return false;
    const uint32_t count = rd32(data + 8);
    out.version.assign(reinterpret_cast<const char*>(data + 12), 4);
    if (16 + size_t(count) * 8 > size) return false;
    for (uint32_t i = 0; i < count; ++i) {
        ShpsImage im;
        im.name.assign(reinterpret_cast<const char*>(data + 16 + i * 8), 4);
        size_t p = rd32(data + 20 + i * 8);
        std::vector<Block> blocks;
        for (int guard = 0; guard < 32 && p + 16 <= size; ++guard) {
            Block b;
            b.type = data[p];
            b.pos = p;
            const uint32_t next = data[p + 1] | data[p + 2] << 8 | data[p + 3] << 16;
            b.end = next ? p + next : size;
            if (b.end > size) b.end = size;
            blocks.push_back(b);
            if (!next) break;
            p += next;
        }
        const Block* img = nullptr;
        const Block* pal = nullptr;
        for (const Block& b : blocks) {
            if (!img && (b.type == 0x01 || b.type == 0x02 || b.type == 0x04 || b.type == 0x05)) img = &b;
            else if (!pal && (b.type == 0x20 || b.type == 0x21 || b.type == 0x22 || b.type == 0x23)) pal = &b;
            else if (b.type == 0x70 && b.pos + 4 < b.end) {
                const char* s = reinterpret_cast<const char*>(data + b.pos + 4);
                im.long_name.assign(s, strnlen(s, b.end - b.pos - 4));
            }
        }
        if (!img) {
            out.warnings.push_back(im.name + ": sem bloco de imagem");
            out.images.push_back(std::move(im));
            continue;
        }
        im.type = img->type;
        im.width = rd16(data + img->pos + 4);
        im.height = rd16(data + img->pos + 6);
        im.swizzled = (rd16(data + img->pos + 12) & 0x2000) != 0;
        im.mipmaps = rd16(data + img->pos + 14) >> 12;
        const uint8_t* px = data + img->pos + 16;
        const size_t avail = img->end - img->pos - 16;
        const int w = im.width, h = im.height;
        im.rgba.assign(size_t(w) * h * 4, 0);

        // A paleta como RGBA (até 256 entradas).
        uint8_t palette[256][4] = {};
        int pal_count = 0;
        if (pal) {
            im.palette_type = pal->type;
            pal_count = rd16(data + pal->pos + 4) * rd16(data + pal->pos + 6);
            if (pal_count > 256) pal_count = 256;
            im.csm1 = (rd16(data + pal->pos + 12) & 0x2000) != 0;
            const uint8_t* pp = data + pal->pos + 16;
            const size_t pav = pal->end - pal->pos - 16;
            for (int k = 0; k < pal_count; ++k) {
                if (pal->type == 0x21 || pal->type == 0x20) {
                    if (size_t(k) * 4 + 4 > pav) break;
                    std::memcpy(palette[k], pp + k * 4, 4);
                } else if (pal->type == 0x22) {
                    if (size_t(k) * 3 + 3 > pav) break;
                    palette[k][0] = pp[k * 3];
                    palette[k][1] = pp[k * 3 + 1];
                    palette[k][2] = pp[k * 3 + 2];
                    palette[k][3] = 0x80;
                } else {  // 0x23: 16 bits, 5 5 5 1
                    if (size_t(k) * 2 + 2 > pav) break;
                    const uint16_t c = rd16(pp + k * 2);
                    palette[k][0] = uint8_t((c & 31) << 3);
                    palette[k][1] = uint8_t(((c >> 5) & 31) << 3);
                    palette[k][2] = uint8_t(((c >> 10) & 31) << 3);
                    palette[k][3] = (c & 0x8000) ? 0x80 : 0;
                }
            }
        }
        auto put = [&](int x, int y, const uint8_t c[4]) { std::memcpy(&im.rgba[(size_t(y) * w + x) * 4], c, 4); };
        if (img->type == 0x02) {
            im.gs_psm = 0x13;
            if (!pal) out.warnings.push_back(im.name + ": 8 bits sem paleta");
            if (avail < size_t(w) * h) {
                out.warnings.push_back(im.name + ": dados curtos");
            } else {
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        int idx = px[im.swizzled ? shps_psmt8_swizzled_index(x, y, w) : y * w + x];
                        if (im.csm1 && pal_count > 8) idx = shps_csm1_index(idx);
                        put(x, y, palette[idx]);
                    }
            }
        } else if (img->type == 0x01) {
            im.gs_psm = 0x14;
            if (im.swizzled) out.warnings.push_back(im.name + ": PSMT4 no swizzle do GS não é decodificado");
            if (avail < (size_t(w) * h + 1) / 2) {
                out.warnings.push_back(im.name + ": dados curtos");
            } else {
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const size_t n = size_t(y) * w + x;
                        const int idx = (n & 1) ? px[n >> 1] >> 4 : px[n >> 1] & 15;  // o pixel par no nibble baixo
                        put(x, y, palette[idx]);
                    }
            }
        } else if (img->type == 0x05) {
            im.gs_psm = 0x00;
            if (avail < size_t(w) * h * 4) out.warnings.push_back(im.name + ": dados curtos");
            else std::memcpy(im.rgba.data(), px, size_t(w) * h * 4);
        } else {  // 0x04
            im.gs_psm = 0x01;
            if (avail < size_t(w) * h * 3) {
                out.warnings.push_back(im.name + ": dados curtos");
            } else {
                for (int k = 0; k < w * h; ++k) {
                    im.rgba[size_t(k) * 4] = px[k * 3];
                    im.rgba[size_t(k) * 4 + 1] = px[k * 3 + 1];
                    im.rgba[size_t(k) * 4 + 2] = px[k * 3 + 2];
                    im.rgba[size_t(k) * 4 + 3] = 0x80;
                }
            }
        }
        out.images.push_back(std::move(im));
    }
    return true;
}

}  // namespace drv
