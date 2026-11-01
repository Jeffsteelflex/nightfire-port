// A biblioteca de fontes da EA e o driver de fontes do EAGL (driving/render/ea_font.h; docs/driving.md seção 28).
#include "driving/render/ea_font.h"

#include <cstring>

#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

EaFontDriverState g_drv;
int32_t g_off_x = 0, g_off_y = 0;

template <typename T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
template <typename T>
void wr(uint8_t* p, T v) {
    std::memcpy(p, &v, sizeof v);
}
float rdf(const uint8_t* p) { return rd<float>(p); }

void put(uint64_t w) { g_drv.packet->words.push_back(w); }

// O tamanho de um registro de caractere: 16 com o bit 0x40000 das bandeiras, senão 12.
int glyph_size(const uint8_t* font) { return (rd<uint32_t>(font + 0xc) & 0x40000u) != 0 ? 16 : 12; }

// [driving] 0x00259498 (sem nome: a busca binária do caractere na tabela)
const uint8_t* find_binary(uint32_t code, const uint8_t* table, int32_t count, int32_t stride) {
    for (;;) {
        const int32_t n = count;
        if (n == 0) return nullptr;
        const uint8_t* g = table + (n >> 1) * stride;
        const uint16_t gc = rd<uint16_t>(g);
        if (code == gc) return g;
        count = n >> 1;
        if (int32_t(code - gc) > 0) {
            table = g + stride;
            count = (n - 1) >> 1;
        }
    }
}

// [driving] 0x00257fb0 (sem nome: a troca de caixa quando não há o caractere; 0 sem troca)
uint32_t swap_case(uint32_t c) {
    if (c - 0x41u < 0x1a) return c + 0x20;
    if (c - 0x61u < 0x1a) return c - 0x20;
    if (c - 0xc0u < 0x17 || c - 0xd8u < 7) return c + 0x20;
    if (c - 0xe0u < 0x17) return c - 0x20;
    if (c - 0xf8u < 7) return c - 0x20;
    return 0;
}

// O caractere pela posição direta (código - 0x20) ou pela busca binária.
const uint8_t* lookup(const uint8_t* font, uint32_t code) {
    const int stride = glyph_size(font);
    const uint8_t* table = font + rd<int32_t>(font + 0x14);
    const int32_t count = rd<uint16_t>(font + 0xa);
    const int32_t idx = int32_t(code) - 0x20;
    // Um caractere de controle (abaixo de 0x20: a quebra de linha também passa aqui antes do teste dela) dá um índice
    // negativo, e o
    // PS2 lê a memória antes da tabela; antes do começo da fonte isso é outra memória (guarda do port: conta como
    // diferente, o que a memória zerada antes da fonte na conferência também dá).
    if (idx < count && rd<int32_t>(font + 0x14) + idx * stride >= 0) {
        const uint8_t* g = table + idx * stride;
        if (rd<uint16_t>(g) == uint16_t(code)) return g;
    }
    return find_binary(code, table, count, stride);
}

// [driving] 0x002590f8 (sem nome: o kerning das fontes sem o bit 0x40000: a tabela +0x18 começa com a contagem)
const uint8_t* kern_find8(int32_t count, const uint8_t* list, uint32_t prev, uint32_t code) {
    // Os pares de 4 bytes {u16 o anterior, s8 o valor, u8 o caractere}.
    for (int32_t i = 0; i < count; ++i) {
        if (rd<uint16_t>(list) == prev && uint32_t(list[3]) == code) return list;
        list += 4;
    }
    return nullptr;
}
// [driving] 0x002590c0 (sem nome: o kerning de 16 bits: count pares {u16 anterior, u8, s8} a partir de list)
const uint8_t* kern_find16(int32_t count, const uint8_t* list, uint32_t prev) {
    for (int32_t i = 0; i < count; ++i) {
        if (rd<uint16_t>(list) == prev) return list;
        list += 4;
    }
    return nullptr;
}

// [driving] 0x002a1e50 (sem nome: o caractere do driver do EAGL: um SPRITE do GS)
void eagl_font_char(float x, float y, const uint8_t* font, const uint8_t* g) {
    const uint32_t gu = rd<uint16_t>(g + 4), gv = rd<uint16_t>(g + 6);
    const uint32_t gw = g[2], gh = g[3];
    const int32_t ox = int8_t(g[9]), oy = int8_t(g[10]);
    const float sx = rdf(font + 0x38), sy = rdf(font + 0x3c);
    const uint64_t z = uint64_t(uint32_t(g_drv.z)) << 32;
    auto uv = [](uint32_t u, uint32_t v) {
        const int64_t uu = int64_t(int32_t(u * 16u + uint32_t(g_drv.uv_x)));
        const int64_t vv = int64_t(int32_t(v * 16u + uint32_t(g_drv.uv_y)));
        return uint64_t(uu) | (uint64_t(vv) << 16);
    };
    auto xy = [&](int32_t dx, int32_t dy) {
        const int32_t ix = ee::cvt_w(ee::add(x, ee::mul(ee::cvt_s(dx), sx)));
        const int32_t iy = ee::cvt_w(ee::add(y, ee::mul(ee::cvt_s(dy), sy)));
        const int64_t xx = int64_t(int32_t(uint32_t(g_drv.offset_x) + uint32_t(ix) * 16u));
        const int64_t yy = int64_t(int32_t(uint32_t(g_drv.offset_y) + uint32_t(iy) * 16u));
        return uint64_t(xx) | (uint64_t(yy) << 16) | z;
    };
    put(uv(gu, gv));
    put(3);
    put(xy(ox, oy));
    put(0xd);
    put(uv(gu + gw, gv + gh));
    put(3);
    put(xy(ox + int32_t(gw), oy + int32_t(gh)));
    put(5);
}

// [driving] 0x002a20d0 (sem nome: o começo do driver do EAGL: as configurações do GS)
void eagl_font_begin(uint8_t* font, GsPacket& out) {
    g_drv.packet = &out;
    out.words.assign(2, 0);  // a tag DMA (o fim a escreve)
    put(0x1300000000000000ull);  // NOP, FLUSHA
    g_drv.tag_at = int32_t(out.words.size());
    put(0);                      // NOP, DIRECT (o fim põe o tamanho)
    g_drv.tag2_at = int32_t(out.words.size());
    put(0);                      // a tag do GIF (o fim a escreve)
    put(0xe);                    // A+D
    // FUN_0027e388 + EAGL::TARExtension::OutputGifTags: a textura (a filtragem pelo +0x48)
    uint64_t tex[12];
    ea_font_texture_regs(rd<uint32_t>(font + 0x7c) | (rd<int32_t>(font + 0x48) != 0 ? 0x80000000u : 0u), tex);
    for (uint64_t w : tex) put(w);
    g_drv.offset_y = g_off_y;  // o RenderContext +0x760
    g_drv.offset_x = g_off_x;  // +0x75c
    const bool ztest = rd<int32_t>(font + 0x4c) == 1;
    put((ztest ? 0x50000ull : 0x30000ull) | 0x105a);
    put(0x47);   // TEST_1
    put(0x8000000044ull);
    put(0x42);   // ALPHA_1
    put(0);
    put(0x1a);   // PRMODECONT
    put(0x158);
    put(0x1b);   // PRMODE
    put(0);      // o FRAME_1 do contexto (0x00348398; o port: 0)
    put(0x4c);
    const uint64_t zbuf = 0;  // 0x003483a0 (o port: 0)
    g_drv.zbuf = zbuf;
    put(ztest ? (zbuf & 0xffffffffull) : (zbuf | 0xffffffff00000000ull));
    put(0x4e);   // ZBUF_1
    put(0);      // 0x003483a8 FBA_1 (o port: 0)
    put(0x4a);
    const uint32_t c = rd<uint32_t>(font + 0x20);
    g_drv.uv_x = ee::cvt_w(ee::mul(rdf(font + 0x30), 16.0f));
    g_drv.uv_y = ee::cvt_w(ee::mul(rdf(font + 0x34), 16.0f));
    g_drv.z = ee::cvt_w(rdf(font + 0x28));
    const uint32_t rgba = ((c >> 25) << 24) | (((c >> 17) & 0x7f) << 16) | ((c >> 1) & 0x7f00u) | ((c & 0xff) >> 1);
    put(uint64_t(rgba) | 0x3f80000000000000ull);
    put(1);      // RGBAQ
}

// [driving] 0x002a25c8 (sem nome: o fim do driver do EAGL: o ZBUF de volta, as tags)
void eagl_font_end() {
    std::vector<uint64_t>& w = g_drv.packet->words;
    put(g_drv.zbuf);
    put(0x4e);
    put(1);
    put(0x1a);
    g_drv.end_at = int32_t(w.size()) - 1;  // o 0x00348b9c: o índice da última palavra (o 0x1a), não o fim
    w[size_t(g_drv.tag_at)] = (uint64_t((g_drv.end_at - g_drv.tag_at) >> 1) | 0x50000000ull) << 32;
    w[size_t(g_drv.tag2_at)] = uint64_t((g_drv.end_at - g_drv.tag2_at) >> 1) | 0x1003400000008000ull;
    // A tag DMA (CNT) no começo do acumulador; o PS2 anda o ponteiro do DmaMgr.
    w[0] = uint64_t(uint32_t((int32_t(w.size()) >> 1) - 1)) | (uint64_t(1) << 28);
    w[1] = 0;
}

}  // namespace

EaFontDriverState& ea_font_driver() { return g_drv; }

void ea_font_set_offset(int32_t x16, int32_t y16) {
    g_off_x = x16;
    g_off_y = y16;
}

void ea_font_texture_regs(uint32_t image, uint64_t out[12]) {
    // Só do port: TEX0_1 com a imagem no TBP0 (14 bits) e no CBP (o bit 31 do pedido: filtragem); TEX1_1 com o MMAG e o
    // MMIN lineares quando a fonte pede filtragem; CLAMP_1 CLAMP; os MIPTBP e o TEXFLUSH a 0.
    const bool linear = (image & 0x80000000u) != 0;
    const uint64_t img = image & 0x3fffu;
    out[0] = img | (uint64_t(1) << 34) /* TCC */;
    out[1] = 0x06;
    out[2] = linear ? (uint64_t(1) << 5) | (uint64_t(1) << 6) : 0;
    out[3] = 0x14;
    out[4] = 0x5;  // WMS = WMT = CLAMP
    out[5] = 0x08;
    out[6] = 0;
    out[7] = 0x34;
    out[8] = 0;
    out[9] = 0x36;
    out[10] = 0;
    out[11] = 0x3f;
}

bool FONT_create(EaFont& f, std::vector<uint8_t> file, uint32_t image) {
    if (file.size() < 0x80 || std::memcmp(file.data(), "FNTS", 4) != 0) return false;
    f.file = std::move(file);
    f.image = image;
    // O driver do EAGL (0x002a31d0): o EAGL::Texture da imagem em +0x1c, guardado em +0x7c (o port: o número).
    wr<uint32_t>(f.ptr() + 0x7c, image);
    return true;
}

int32_t FONT_getkern(const uint8_t* font, const uint8_t* glyph, uint32_t prev) {
    if (glyph[0xb] == 0) return 0;
    const uint8_t* k;
    if ((rd<uint32_t>(font + 0xc) & 0x40000u) == 0) {
        const uint8_t* t = font + rd<int32_t>(font + 0x18);
        k = kern_find8(rd<int32_t>(t), t + 4, prev, rd<uint16_t>(glyph));
    } else {
        k = kern_find16(int8_t(glyph[0xb]), font + rd<int32_t>(font + 0x18) + uint32_t(rd<uint16_t>(glyph + 0xc)) * 4 + 4,
                        prev);
    }
    return k ? int8_t(k[2]) : 0;
}

const uint8_t* ea_font_find_glyph(const uint8_t* font, uint8_t c) {
    const uint8_t* g = lookup(font, c);
    if (g) return g;
    if (c == 10) return nullptr;
    const uint32_t s = swap_case(c);
    if (s != 0) g = lookup(font, s);
    if (g) return g;
    // O 0x7f: pela posição 0x5f se a tabela tem 0x60 entradas, senão pela busca.
    const int stride = glyph_size(font);
    const uint8_t* table = font + rd<int32_t>(font + 0x14);
    const int32_t count = rd<uint16_t>(font + 0xa);
    if (count >= 0x60 && rd<int16_t>(table + stride * 0x5f) == 0x7f) return table + stride * 0x5f;
    return find_binary(0x7f, table, count, stride);
}

void FONT_drawtexta(float x, float y, EaFont& f, const char* text, GsPacket& out) {
    uint8_t* font = f.ptr();
    eagl_font_begin(font, out);  // o PTR_PTR_003c7f54 +4 (o começo)
    const auto* p = reinterpret_cast<const uint8_t*>(text);
    const float sx = rdf(font + 0x38), sy = rdf(font + 0x3c);
    float cx = x;
    uint32_t prev = 0;
    for (; *p != 0; ++p) {
        const uint8_t c = *p;
        const uint8_t* g = lookup(font, c);
        if (g == nullptr) {
            if (c == 10) {
                prev = 0;
                y = ee::add(y, ee::cvt_s(ee::cvt_w(ee::mul(ee::cvt_s(int32_t(font[0x13]) + int32_t(font[0x12])), sy))));
                cx = x;
                continue;
            }
            g = ea_font_find_glyph(font, c);
            if (g == nullptr) continue;
        }
        cx = ee::add(cx, ee::mul(ee::cvt_s(FONT_getkern(font, g, prev)), sx));
        eagl_font_char(cx, y, font, g);  // o PTR_PTR_003c7f54 +0 (o caractere: a extensão 0x003421e4 é nula)
        const int32_t adv = (rd<uint32_t>(font + 0xc) & 0x40000u) == 0 ? int32_t(int8_t(g[8])) : int32_t(rd<int16_t>(g + 0xe));
        cx = ee::add(cx, ee::mul(ee::cvt_s(adv), sx));
        prev = c;
    }
    eagl_font_end();  // +8 (o fim)
}

void FONT_getrecta(const uint8_t* font, const char* text, float* ox, float* oy, float* ow, float* oh) {
    const float sx = rdf(font + 0x38), sy = rdf(font + 0x3c);
    float minx = 1e7f, maxx = -1e7f, miny = 1e7f, maxy = -1e7f;  // 0x003c7e00, 0x003c7e04
    float cx = 0.0f, cy = 0.0f;
    uint32_t prev = 0;
    for (const auto* p = reinterpret_cast<const uint8_t*>(text); *p != 0; ++p) {
        const uint8_t c = *p;
        const uint8_t* g = lookup(font, c);
        if (g == nullptr) {
            if (c == 10) {
                prev = 0;
                cx = 0.0f;
                cy = ee::add(cy, ee::cvt_s(ee::cvt_w(ee::mul(ee::cvt_s(int32_t(font[0x13]) + int32_t(font[0x12])), sy))));
                continue;
            }
            g = ea_font_find_glyph(font, c);
            if (g == nullptr) continue;
        }
        cx = ee::add(cx, ee::mul(ee::cvt_s(FONT_getkern(font, g, prev)), sx));
        float gx = ee::add(cx, ee::mul(ee::cvt_s(int8_t(g[9])), sx));
        float gy = ee::add(cy, ee::mul(ee::cvt_s(int8_t(g[10])), sy));
        if (!ee::le(minx, gx)) minx = gx;
        if (ee::lt(gy, miny)) miny = gy;
        gx = ee::add(gx, ee::mul(ee::cvt_s(int32_t(g[2])), sx));
        gy = ee::add(gy, ee::mul(ee::cvt_s(int32_t(g[3])), sy));
        if (!ee::le(gx, maxx)) maxx = gx;
        if (ee::lt(maxy, gy)) maxy = gy;
        const int32_t adv = (rd<uint32_t>(font + 0xc) & 0x40000u) == 0 ? int32_t(int8_t(g[8])) : int32_t(rd<int16_t>(g + 0xe));
        cx = ee::add(cx, ee::mul(ee::cvt_s(adv), sx));
        prev = c;
    }
    const bool hx = ee::lt(minx, maxx), hy = ee::lt(miny, maxy);
    if (ox) *ox = hx ? minx : 0.0f;
    if (oy) *oy = hy ? miny : 0.0f;
    if (ow) *ow = hx ? ee::sub(maxx, minx) : 0.0f;
    if (oh) *oh = hy ? ee::sub(maxy, miny) : 0.0f;
}

std::string FONT_format_noargs(const char* fmt, int32_t* guards) {
    std::string out;
    const char* p = fmt;
    auto guard = [&] {
        if (guards) ++*guards;
    };
    while (*p) {
        if (*p != '%') {
            out += *p++;
            continue;
        }
        ++p;
        bool left = false, zero = false;
        int width = 0;
        for (;;) {
            const char ch = *p++;
            if (ch == ' ' || ch == '#' || ch == '+' || ch == 'h' || ch == 'l' || ch == 'q' || ch == 'L') continue;
            if (ch == '-') {
                left = true;
                continue;
            }
            if (ch == '0') {
                zero = true;
                continue;
            }
            if (ch == '*') {  // a largura de um argumento
                guard();
                continue;
            }
            if (ch == '.') {
                if (*p == '*') {
                    ++p;
                    guard();
                } else {
                    while (*p >= '0' && *p <= '9') ++p;
                }
                continue;
            }
            if (ch >= '1' && ch <= '9') {
                int n = ch - '0';
                while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
                width = n;
                continue;
            }
            if (ch == '\0') return out;  // o '%' no fim: acabou
            if (std::strchr("cDdiOopsUuXxeEfgGn", ch) != nullptr) {
                guard();  // leria um argumento
                break;
            }
            // Outro caractere (o '%' de "%%" também): ele mesmo, na largura.
            if (!left)
                for (int i = 1; i < width; ++i) out += zero ? '0' : ' ';
            out += ch;
            if (left)
                for (int i = 1; i < width; ++i) out += ' ';
            break;
        }
    }
    return out;
}

void FONT_drawtextfa(float x, float y, EaFont& f, const char* fmt, GsPacket& out, int32_t* guards) {
    const std::string text = FONT_format_noargs(fmt, guards);
    FONT_drawtexta(x, y, f, text.c_str(), out);
}

}  // namespace drv
