// Desenho 2D da plataforma para PC (psiDrawSprites e companhia). Ver psi_2d.h.
//
// Divergências: psiDrawSprites não monta pacotes de DMA (o registro do quadro guarda cada sprite com a
// textura, o ALPHA_1 e o filtro que o pacote levaria ao GS); a região da textura na VRAM (CLAMP_1
// REGION_REPEAT, TBP0/CBP, CacheTexture) não existe no PC, onde cada textura é um recurso próprio.

#include "platform/psi_2d.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/hashtable.h"
#include "engine/map_types.h"
#include "platform/psi_draw.h"
#include "platform/psi_shard.h"

uint32_t FontList[3] = {0, 0, 0};
const Psi2DOrigin* g_psi_2d_origins = nullptr;

namespace {
// Deslocamento da tela no espaço de 4096 x 4096 do GS (PS2OffX/Y, psiSetScreenPos), em unidades 12.4.
// Só a parte fracionária da conversão importa aqui: o GS trunca x * 16 (cvt.w.s) depois de somar.
constexpr float kPs2OffX = (2048.0f - 256.0f) * 16.0f;
constexpr float kPs2OffY = (2048.0f - 224.0f) * 16.0f;

// Coordenada da tela do PS2 como o XYZ2 a guarda (12.4, truncada) e de volta para pixels.
float gs_xy(float off, float pixels_times_16) { return float(int32_t(off + pixels_times_16) - int32_t(off)) / 16.0f; }
}  // namespace

void psi_2d_debug_power_on() {
    FontList[0] = FontList[1] = FontList[2] = 0;
    g_psi_2d_origins = nullptr;
}

Psi2DViewport psi_2d_viewport(int window_width, int window_height) {
    Psi2DViewport v;
    if (window_width <= 0 || window_height <= 0) return v;
    float w = float(window_height) * (4.0f / 3.0f);
    float h = float(window_height);
    if (w > float(window_width)) {
        w = float(window_width);
        h = float(window_width) * 0.75f;
    }
    v.left = (float(window_width) - w) * 0.5f;
    v.top = (float(window_height) - h) * 0.5f;
    v.width = w;
    v.height = h;
    v.scale_x = w / kPs2ScreenWidth;
    v.scale_y = h / kPs2ScreenHeight;
    return v;
}

bool psi_2d_map_rect(const Psi2DViewport& vp, int window_width, int window_height, const float in[4], float out[4]) {
    out[0] = vp.left + in[0] * vp.scale_x;
    out[1] = vp.top + in[1] * vp.scale_y;
    out[2] = vp.left + in[2] * vp.scale_x;
    out[3] = vp.top + in[3] * vp.scale_y;
    bool full = false;
    if (vp.left > 0.0f && in[0] <= 0.0f && in[2] >= kPs2ScreenWidth) {
        out[0] = 0.0f;
        out[2] = float(window_width);
        full = true;
    }
    if (vp.top > 0.0f && in[1] <= 0.0f && in[3] >= kPs2ScreenHeight) {
        out[1] = 0.0f;
        out[3] = float(window_height);
        full = true;
    }
    return full;
}

// As molduras (ver psi_2d.h). O painel dos menus: cantos de raio ~55 texels em cima à esquerda e embaixo,
// a faixa dourada das abas até a linha 37 e a barra de ajuda a partir da 222; os cortes ficam nos trechos
// retos (a borda de fora reta de y = 72 a 176, o vidro reto de x = 64 a 448).
namespace {
const Psi2DFrame kFrames[] = {
    {0x03000070u, 512, 256, {64.0f, 72.0f, 64.0f, 80.0f}, 1.0f, "painel dos menus"},
};
}  // namespace

const Psi2DFrame* psi_2d_find_frame(uint32_t hash) {
    if (hash == 0) return nullptr;
    for (const Psi2DFrame& f : kFrames) {
        if (f.hash == hash) return &f;
    }
    return nullptr;
}

int psi_2d_nine_slice(const Psi2DQuad& q, const Psi2DFrame& f, float pixel_aspect, Psi2DQuad out[9], float corner_scale[2]) {
    out[0] = q;
    if (corner_scale != nullptr) corner_scale[0] = corner_scale[1] = 0.0f;
    if (f.tex_w <= 1 || f.tex_h <= 1 || !(pixel_aspect > 0.0f) || !(f.aspect > 0.0f)) return 1;
    // Os texels que o quadrado mostra (centros do primeiro e do último; nas fontes, as bordas): só a textura
    // inteira é cortada.
    const float span_u = std::fabs(q.u1 - q.u0) * float(f.tex_w);
    const float span_v = std::fabs(q.v1 - q.v0) * float(f.tex_h);
    if (span_u < float(f.tex_w) - 1.5f || span_v < float(f.tex_h) - 1.5f) return 1;
    const float w = std::fabs(q.x1 - q.x0), h = std::fabs(q.y1 - q.y0);
    if (!(w > 0.0f) || !(h > 0.0f)) return 1;
    // Escala na tela (em pixels de altura da tela do PS2 por texel) em cada eixo.
    const float sx = w * pixel_aspect / span_u;
    const float sy = h / span_v;
    if (std::fabs((sx / sy) / f.aspect - 1.0f) <= kPsi2DFrameTolerance) return 1;
    // Cantos: o eixo menos esticado manda, e o outro segue a proporção de referência.
    const float cy = std::min(sy, sx / f.aspect);
    const float cx = cy * f.aspect;
    // Espelhado: a borda esquerda da tela mostra a direita da textura.
    float inset[4] = {f.inset[0], f.inset[1], f.inset[2], f.inset[3]};
    if (q.u0 > q.u1) std::swap(inset[0], inset[2]);
    if (q.v0 > q.v1) std::swap(inset[1], inset[3]);
    float lw = inset[0] * cx / pixel_aspect, rw = inset[2] * cx / pixel_aspect;
    float th = inset[1] * cy, bh = inset[3] * cy;
    if (lw + rw > w) {
        const float k = w / (lw + rw);
        lw *= k;
        rw *= k;
    }
    if (th + bh > h) {
        const float k = h / (th + bh);
        th *= k;
        bh *= k;
    }
    // As linhas de corte na tela e no ST (o ST é linear nos texels mostrados).
    const float dx = q.x1 >= q.x0 ? 1.0f : -1.0f, dy = q.y1 >= q.y0 ? 1.0f : -1.0f;
    const float xs[4] = {q.x0, q.x0 + dx * lw, q.x1 - dx * rw, q.x1};
    const float ys[4] = {q.y0, q.y0 + dy * th, q.y1 - dy * bh, q.y1};
    const float fl = std::min(1.0f, inset[0] / span_u), fr = std::min(1.0f, inset[2] / span_u);
    const float ft = std::min(1.0f, inset[1] / span_v), fb = std::min(1.0f, inset[3] / span_v);
    const float us[4] = {q.u0, q.u0 + (q.u1 - q.u0) * fl, q.u1 - (q.u1 - q.u0) * fr, q.u1};
    const float vs[4] = {q.v0, q.v0 + (q.v1 - q.v0) * ft, q.v1 - (q.v1 - q.v0) * fb, q.v1};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            Psi2DQuad& o = out[r * 3 + c];
            o = q;
            o.x0 = xs[c];
            o.x1 = xs[c + 1];
            o.y0 = ys[r];
            o.y1 = ys[r + 1];
            o.u0 = us[c];
            o.u1 = us[c + 1];
            o.v0 = vs[r];
            o.v1 = vs[r + 1];
        }
    }
    if (corner_scale != nullptr) {
        corner_scale[0] = cx / pixel_aspect;
        corner_scale[1] = cy;
    }
    return 9;
}

// Trecho final de CreateSpaceAndPackTextures (0x0011dcf8): as texturas de fonte, pelos hashcodes.
void psi_2d_find_font_textures() {
    const uint32_t hashes[3] = {0x3000005, 0x3000003, 0x30000d8};
    for (int k = 0; k < 3; ++k) {
        const auto* t = static_cast<const maptex_tag*>(hashtable_getitem(hashes[k]));
        if (t != nullptr) FontList[k] = nf::p32_u(t->rec);
    }
}

int32_t psi_2d_texture_slot(uint32_t hash, int* width, int* height) {
    const auto* t = static_cast<const maptex_tag*>(hashtable_getitem(hash));
    if (t == nullptr || t->rec == nullptr) return -1;
    if (width != nullptr) *width = t->rec->width;
    if (height != nullptr) *height = t->rec->height;
    return t->rec->tex_index;
}

// [decomp] 0x001213d8 psiStartSprites
// Sem teste de Z (TEST_1 só com alfa > 0) e sem escrita de Z.
void psiStartSprites() {
    psiSwitchZBufferRead(1);
    psiSwitchZBufferWrite(1);
}

// [decomp] 0x00121400 psiEndSprites
void psiEndSprites() {
    psiSwitchZBufferRead(0);
    psiSwitchZBufferWrite(0);
}

// [decomp] 0x00120e68 psiDrawSprites
// No PS2: um pacote de 0xe0 bytes por sprite (FillSpriteData, TEX0 do cache da VRAM, CLAMP pela região
// da textura). A textura só vai ao GS quando muda (psiFont_SetTexture); se ela é uma das fontes
// (FontList), o ST não leva o meio texel, o canto de baixo à direita recua 0.1 unidade 12.4 e o filtro
// é de ponto. O ALPHA_1 também só é escrito quando o modo muda (0x100 aditiva, 0x200 subtrativa, senão
// transparente). A cor vai como (r, g, b, a >> 1).
// No PC: um Psi2DQuad por sprite no registro do quadro, com a origem de g_psi_2d_origins.
void psiDrawSprites(SPRITE_DRAW* list, int16_t count) {
    const Psi2DOrigin* origins = g_psi_2d_origins;
    g_psi_2d_origins = nullptr;
    if (count <= 0 || list == nullptr) return;
    uint32_t texture = 0xffffffffu;
    uint32_t mode = 0xffffffffu;
    uint64_t alpha = 0x44;
    bool font = false;
    std::vector<Psi2DQuad> quads;
    quads.reserve(size_t(count));
    for (int16_t i = 0; i < count; ++i) {
        const SPRITE_DRAW& s = list[i];
        if (s.texture != texture) {
            psiFont_SetTexture(s.texture);
            texture = s.texture;
            font = texture == FontList[0] || texture == FontList[1] || texture == FontList[2];
        }
        uint32_t m = (s.flags & 0x100) != 0 ? 1 : 0;
        if ((s.flags & 0x200) != 0) m = 2;
        if (m != mode) {
            mode = m;
            alpha = m == 1 ? 0x48 : (m == 0 ? 0x44 : 0x42);
        }
        float u0 = float(s.u);
        float v0 = float(s.v);
        float u1 = float(int32_t(s.u) + int32_t(s.uw));
        float v1 = float(int32_t(s.v) + int32_t(s.vh));
        if (!font) {
            u0 += 0.5f;
            v0 += 0.5f;
            u1 += 0.5f;
            v1 += 0.5f;
        }
        float x1 = (s.x + s.w) * 16.0f;
        const float y0 = s.y * 16.0f;
        float y1 = (s.y + s.h) * 16.0f;
        if (font) {
            x1 -= 0.1f;
            y1 -= 0.1f;
        }
        Psi2DQuad q;
        q.x0 = gs_xy(kPs2OffX, s.x * 16.0f);
        q.y0 = gs_xy(kPs2OffY, y0);
        q.x1 = gs_xy(kPs2OffX, x1);
        q.y1 = gs_xy(kPs2OffY, y1);
        q.u0 = u0 * s.inv_w;
        q.v0 = v0 * s.inv_h;
        q.u1 = u1 * s.inv_w;
        q.v1 = v1 * s.inv_h;
        q.rgba[0] = s.r;
        q.rgba[1] = s.g;
        q.rgba[2] = s.b;
        q.rgba[3] = uint8_t(s.a >> 1);
        q.texture = g_psi_font_texture;
        q.alpha = alpha;
        q.linear = !font;
        q.origin = origins != nullptr ? origins[i] : Psi2DOrigin{};
        quads.push_back(q);
    }
    psi_draw_2d(quads.data(), uint32_t(quads.size()));
}
