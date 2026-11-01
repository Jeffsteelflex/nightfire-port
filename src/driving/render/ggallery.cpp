// A galeria do HUD das missões de carro (driving/render/ggallery.h; docs/driving.md seção 28).
#include "driving/render/ggallery.h"

#include <cstring>

#include "driving/game/hud.h"
#include "driving/real/vu0_math.h"
#include "driving/render/hud_gfx.h"
#include "platform/crt_compat.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
namespace vu = ps2::vu;

namespace {

HudRendererInfo g_renderer;

constexpr uint32_t kTagName = 0x4e616d65u;  // 'Name'
constexpr float kInset = 0.001953125f;      // 0x0033aae0 (1/512): o recuo do UV

template <typename T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
float rdf(const uint8_t* p) { return rd<float>(p); }
uint8_t* ptr(uint32_t a) { return nf::p32_ptr<uint8_t>(a); }

const char* name_of(const UGroup* sub) {
    UData* n = UGroup_DataLocateTag(sub, kTagName);
    return reinterpret_cast<const char*>(UData_Data(n));
}

// As chaves 'gk#12288' (DataIndex(3, 0) | 'gk'<<16) de um grupo.
uint32_t keys_of(const UGroup* sub) {
    const uint32_t tag = GGallery_DataIndex(3, 0) | 0x676b0000u;
    UData* d = UGroup_DataLocateTag(sub, tag);
    return nf::p32_u(UData_Data(d));
}

}  // namespace

HudRendererInfo& hud_renderer() { return g_renderer; }

uint32_t gallery_colour(uint32_t c) {
    return ((c & 0xfe) << 15) | ((c & 0xfe00) >> 1) | ((c >> 17) & 0x7f) | ((c & 0xfe000000u) >> 1);
}

uint32_t GGallery_DataIndex(int32_t a, uint32_t b) { return (uint32_t(a) << 12) | b; }

void GGallery_STATE_Reset(GGalleryData& g) {
    g.colour = 0x7f7f7f7f;
    const float pos[4] = {0, 0, 0, 1}, quat[4] = {0, 0, 0, 1}, scale[4] = {1, 1, 1, 1};  // 0x0039d190
    std::memcpy(g.pos, pos, 16);
    std::memcpy(g.quat, quat, 16);
    std::memcpy(g.scale, scale, 16);
}

void GGallery_Construct(GGalleryData& g, uint8_t* file,
                        const std::function<uint32_t(const uint8_t* ssh, uint32_t size)>& image) {
    g.colour = 0x7f7f7f7f;
    g.texture_count = 0;
    GGallery_STATE_Reset(g);
    g.texture = 0;
    g.a0[0] = g.a0[1] = 0;  // 0x0039d178
    g.font = nullptr;
    g.font_file = 0;
    g.custom_state = 0;
    if (file == nullptr) return;
    UGroup* root = reinterpret_cast<UGroup*>(file);
    UGroup_Deserialize(root);  // UGroup::Deserialize(arquivo, 1)
    g.gallery = nf::p32_u(root);
    g.file = nf::p32_u(file);
    for (uint32_t i = 0; i < 0xf; ++i) {
        const uint32_t tag = i | 0x74780000u;  // 'tx#i'
        UData* d = UGroup_DataLocateTag(root, tag);
        if (d == UGroup_DataEnd(root)) return;
        const uint8_t* ssh = UData_Data(d);
        // MGMT_MainShapeIndex (0x00236ac8): o último 'MAIN' do diretório; o EAGL::TAR dele (o port: a imagem).
        g.textures[g.texture_count] = image ? image(ssh, UData_Size(d)) : 0u;
        ++g.texture_count;
    }
}

uint32_t GGallery_GET_Gallery(const GGalleryData& g) { return g.gallery; }

uint32_t GGallery_GET_Canvas(const GGalleryData& g, uint32_t group, const char* name) {
    (void)g;
    const UGroup* p = nf::p32_ptr<const UGroup>(group);
    if (p == nullptr) return 0;
    for (UData* s = UGroup_GetArray(p); s != UGroup_GroupEnd(p); ++s)
        if (nf::str_icmp(name_of(s), name) == 0) return nf::p32_u(s);
    return 0;  // o PS2 cai do laço sem valor de volta (o v0 da última strcasecmp: não zero)
}

uint32_t GGallery_GET_Name(const GGalleryData& g, uint32_t group, uint32_t child) {
    (void)g;
    const UGroup* p = nf::p32_ptr<const UGroup>(group);
    for (UData* s = UGroup_GetArray(p); s != UGroup_GroupEnd(p); ++s)
        if (nf::p32_u(s) == child) return nf::p32_u(name_of(s));
    return 0;
}

uint32_t GGallery_GET_Puppet(const GGalleryData& g, uint32_t canvas, const char* name) {
    (void)g;
    const UGroup* p = nf::p32_ptr<const UGroup>(canvas);
    if (p == nullptr) return 0;
    UData* s = UGroup_GetArray(p);
    for (; s != UGroup_GroupEnd(p); ++s)
        if (std::strcmp(name_of(s), name) == 0) break;
    if (s == UGroup_GroupEnd(p)) return 0;
    return keys_of(s);
}

uint32_t GGallery_GET_PuppetAndGroup(const GGalleryData& g, uint32_t canvas, const char* name, uint32_t* group) {
    (void)g;
    const UGroup* p = nf::p32_ptr<const UGroup>(canvas);
    UData* s = UGroup_GetArray(p);
    for (; s != UGroup_GroupEnd(p); ++s)
        if (std::strcmp(name_of(s), name) == 0) break;
    if (s == UGroup_GroupEnd(p)) return 0;
    *group = nf::p32_u(s);
    return keys_of(s);
}

uint32_t GGallery_GET_Texture(const GGalleryData& g, int32_t index) {
    return rd<uint32_t>(reinterpret_cast<const uint8_t*>(&g) + 0x10 + index * 4);
}

uint32_t GGallery_GET_Key(const GGalleryData& g, uint32_t puppet, int32_t index) {
    (void)g;
    const uint32_t s = uint32_t(ptr(puppet)[0x21]) + 0x30;
    const uint32_t stride = 0x3f < s ? s : 0x40;
    return puppet + uint32_t(index) * stride;
}

uint32_t GGallery_GET_FirstIdleKey(const GGalleryData& g, uint32_t group) {
    (void)g;
    return keys_of(nf::p32_ptr<const UGroup>(group));
}

void GGallery_Get_InterpolatedKey(float t, uint8_t* out, const uint8_t* a, const uint8_t* b) {
    const int32_t fa = rd<int32_t>(a + 0xc), fb = rd<int32_t>(b + 0xc);
    float f4 = ee::mul(t, ee::cvt_s(fb - fa));
    const float x = ee::add(rdf(a), ee::mul(t, ee::sub(rdf(b), rdf(a))));
    const float y = ee::add(rdf(a + 4), ee::mul(t, ee::sub(rdf(b + 4), rdf(a + 4))));
    const float z = ee::add(rdf(a + 8), ee::mul(t, ee::sub(rdf(b + 8), rdf(a + 8))));
    const int32_t frame = ee::cvt_w(ee::add(ee::cvt_s(fa), f4));
    uint8_t tmp[0x34] = {};  // o PS2: a pilha (de +0x22 a +0x33 o que estiver lá; o port: zeros)
    std::memcpy(tmp, &x, 4);
    std::memcpy(tmp + 4, &y, 4);
    std::memcpy(tmp + 8, &z, 4);
    std::memcpy(tmp + 0xc, &frame, 4);
    std::memcpy(tmp + 0x10, a + 0x10, 16);
    tmp[0x20] = a[0x20];
    tmp[0x21] = a[0x21];
    std::memcpy(out, tmp, 0x34);
}

void GGallery_STATE_SetDefaultRenderState(GGalleryData& g) {
    (void)g;
    DrawGlobals& d = draw_globals();
    d.textured.misc &= ~1ull;
    d.textured.depth_test = 1;    // EAGL::GeoPrimState::SetDepthTestMethod(0x003335c4, 1)
    d.untextured.depth_test = 1;  // (0x003335c0, 1)
}

void GGallery_BITMAP_SetState(GGalleryData& g, const uint8_t* key) {
    g.pos[0] = rdf(key);
    g.pos[1] = rdf(key + 4);
    g.pos[2] = rdf(key + 8);
    std::memcpy(g.quat, key + 0x10, 16);
    g.scale[2] = 1.0f;
    g.scale[0] = ee::div(ee::cvt_s(rd<int32_t>(key + 0x54)), 1000.0f);
    g.scale[1] = ee::div(ee::cvt_s(rd<int32_t>(key + 0x58)), 1000.0f);
    g.colour = gallery_colour(rd<uint32_t>(key + 0x48));
    g.texture = GGallery_GET_Texture(g, rd<int32_t>(key + 0x4c));
}

void GGallery_BITMAP_Draw2D(GGalleryData& g, const uint8_t* key) {
    const uint8_t* sub = key + 0x30;
    const float hy = ee::mul(ee::mul(rdf(sub + 0x14), g.scale[1]), 0.5f);
    const float hx = ee::mul(ee::mul(rdf(sub + 0x10), g.scale[0]), 0.5f);
    float c[4][4] = {{ee::neg(hx), ee::neg(hy), 1, 1}, {hx, ee::neg(hy), 1, 1}, {ee::neg(hx), hy, 1, 1}, {hx, hy, 1, 1}};
    const float u0 = ee::add(rdf(sub), kInset), v0 = ee::add(rdf(sub + 8), kInset);
    const float u1 = ee::sub(rdf(sub + 4), kInset), v1 = ee::sub(rdf(sub + 0xc), kInset);
    const float trans[4] = {g.pos[0], g.pos[1], g.pos[2], 1.0f};
    const float scale[4] = {1, 1, 1, 1};  // 0x0039d168
    const float uv[4][4] = {{u0, v0, 1, 1}, {u1, v0, 1, 1}, {u0, v1, 1, 1}, {u1, v1, 1, 1}};
    for (auto& p : c) GSystem_MATH_TransformPoints(p, trans, g.quat, scale);
    if (g.custom_state == 0) {
        Draw_SetNormalBlendMode(draw_globals().textured);
        GGallery_STATE_SetDefaultRenderState(g);
    }
    Draw_DrawBatchedTexturedQuad(c[0], c[1], c[2], c[3], g.colour, uv,
                                 GGallery_GET_Texture(g, rd<int32_t>(sub + 0x1c)));
}

void GGallery_FONT_SetScale(GGalleryData& g, float sx, float sy) {
    uint8_t* f = nf::raw(g.font)->ptr();
    std::memcpy(f + 0x38, &sx, 4);
    std::memcpy(f + 0x3c, &sy, 4);
}

void GGallery_FONT_SetState(GGalleryData& g, const uint8_t* key) {
    g.pos[0] = rdf(key);
    g.pos[2] = rdf(key + 8);
    g.pos[1] = rdf(key + 4);
    std::memcpy(g.quat, key + 0x10, 16);
    g.colour = gallery_colour(rd<uint32_t>(key + 0x30));
    g.texture = GGallery_GET_Texture(g, rd<int32_t>(key + 0x50));
    GGallery_FONT_SetScale(g, ee::div(ee::cvt_s(rd<int32_t>(key + 0x38)), 1000.0f),
                           ee::div(ee::cvt_s(rd<int32_t>(key + 0x3c)), 1000.0f));
}

void GGallery_FONT_Load(GGalleryData& g, std::vector<uint8_t> file, uint32_t image) {
    // FUN_00257f50 (FONT_destroy) e o UFileLoader::FileFree da anterior; o FileLoad e o FONT_create.
    if (g.font == nullptr) g.font = new EaFont();
    FONT_create(*nf::raw(g.font), std::move(file), image);
    g.font_file = 1;
}

namespace {
// O comum dos três FONT_Draw: o y pela altura da tela / 480, a escala x 0,75 e o x puxado ao centro na tela larga.
void font_draw(GGalleryData& g, float x, float y, const char* text, uint32_t colour) {
    if (g.custom_state == 0) GGallery_STATE_SetDefaultRenderState(g);
    uint8_t* f = nf::raw(g.font)->ptr();
    float sx = rdf(f + 0x38);
    const float k = ee::div(ee::cvt_s(g_renderer.height), 480.0f);
    float sy = ee::mul(rdf(f + 0x3c), k);
    if (g_renderer.wide != 0) {
        sx = ee::mul(sx, 0.75f);
        if (g_renderer.mode == 2) {
            if (ee::lt(x, 320.0f))
                x = ee::add(x, ee::mul(ee::div(ee::sub(320.0f, x), 320.0f), 80.0f));
            else
                x = ee::sub(x, ee::mul(ee::div(ee::sub(x, 320.0f), 320.0f), 80.0f));
        } else {
            sy = ee::mul(sy, 0.75f);
        }
    }
    std::memcpy(f + 0x38, &sx, 4);
    std::memcpy(f + 0x3c, &sy, 4);
    std::memcpy(f + 0x20, &colour, 4);
    HudFrameItem it;
    it.is_packet = true;
    FONT_drawtextfa(x, ee::mul(y, k), *nf::raw(g.font), text, it.packet);  // o texto é o formato
    hud_frame().push_back(std::move(it));
}
}  // namespace

void GGallery_FONT_Draw_Text(GGalleryData& g, const uint8_t* key, const char* text) {
    if (text == nullptr) text = LOCALE_Symbol(rd<int32_t>(key + 4));
    font_draw(g, g.pos[0], g.pos[1], text, g.colour);
}

void GGallery_FONT_Draw_Colour(GGalleryData& g, float x, float y, const char* text, uint32_t colour) {
    font_draw(g, x, y, text, colour);
}

void GGallery_FONT_Draw(GGalleryData& g, float x, float y, const char* text) { font_draw(g, x, y, text, g.colour); }

float GGallery_FONT_Width(const GGalleryData& g, const char* text) {
    float w = 0.0f;
    FONT_getrecta(nf::raw(g.font)->ptr(), text, nullptr, nullptr, &w, nullptr);
    return w;
}

float GGallery_FONT_Height(const GGalleryData& g, const char* text) {
    float h = 0.0f;
    FONT_getrecta(nf::raw(g.font)->ptr(), text, nullptr, nullptr, nullptr, &h);
    return h;
}

void GGallery_QUAD_SetState(GGalleryData& g, const uint8_t* key) {
    g.pos[0] = rdf(key);
    g.pos[1] = rdf(key + 4);
    g.pos[2] = rdf(key + 8);
    std::memcpy(g.quat, key + 0x10, 16);
    g.colour = 0;
}

void GGallery_QUAD_Draw(GGalleryData& g, float alpha, const uint8_t* quad) {
    uint32_t col[4];
    if (g.colour != 0) {
        col[0] = col[1] = col[2] = col[3] = g.colour;
    } else {
        for (int i = 0; i < 4; ++i) col[i] = gallery_colour(rd<uint32_t>(quad + i * 4));
        for (uint32_t& c : col) GSystem_COLOR_ReduceAlpha(c, alpha);
    }
    float v[4][4] = {{rdf(quad + 0x10), rdf(quad + 0x14), 1, 1},
                     {rdf(quad + 0x18), rdf(quad + 0x1c), 1, 1},
                     {rdf(quad + 0x20), rdf(quad + 0x24), 1, 1},
                     {rdf(quad + 0x28), rdf(quad + 0x2c), 1, 1}};
    const float scale[3] = {1, 1, 1};  // 0x0039d180
    GGallery_TRANSFORM_Vertices(v, 4, g.pos, g.quat, scale);
    if (g.custom_state == 0) GGallery_STATE_SetDefaultRenderState(g);
    Draw_DrawQuadC(v, col);
}

void GGallery_QUAD_Draw2D(GGalleryData& g, float x, float y, float w, float h, uint32_t colour) {
    const float x1 = ee::add(x, w), y1 = ee::add(y, h);
    const float c[4][4] = {{x, y, 1, 1}, {x1, y, 1, 1}, {x, y1, 1, 1}, {x1, y1, 1, 1}};
    if (g.custom_state == 0) GGallery_STATE_SetDefaultRenderState(g);
    Draw_DrawQuad(c, colour);
}

void GGallery_QUAD_Interpolate(float t, const uint8_t* a, const uint8_t* b, uint8_t* out) {
    std::memcpy(out, a, 0x30);
    for (int i = 0; i < 4; ++i) {
        const uint32_t c = GSystem_COLOR_Interpolate(rd<uint32_t>(a + i * 4), rd<uint32_t>(b + i * 4), t);
        std::memcpy(out + i * 4, &c, 4);
    }
    for (int i = 0; i < 8; ++i) {
        const float va = rdf(a + 0x10 + i * 4), vb = rdf(b + 0x10 + i * 4);
        const float r = ee::add(va, ee::mul(ee::sub(vb, va), t));
        std::memcpy(out + 0x10 + i * 4, &r, 4);
    }
}

void GGallery_SPRITE_SetState(GGalleryData& g, const uint8_t* key) {
    const uint8_t* sub = key + 0x30;
    g.pos[0] = rdf(key);
    g.pos[1] = rdf(key + 4);
    g.pos[2] = rdf(key + 8);
    std::memcpy(g.quat, key + 0x10, 16);
    g.scale[2] = 1.0f;
    const float k = ps2::ffloat(0x3a83126fu);  // 0,001
    g.scale[0] = ee::mul(ee::cvt_s(rd<int32_t>(sub + 0x24)), k);
    g.scale[1] = ee::mul(ee::cvt_s(rd<int32_t>(sub + 0x28)), k);
    g.colour = gallery_colour(rd<uint32_t>(sub + 0x18));
    g.texture = GGallery_GET_Texture(g, rd<int32_t>(sub + 0x1c));
}

void GGallery_SPRITE_Draw2D_At(GGalleryData& g, int32_t x, int32_t y, const uint8_t* key, const uint8_t* sprite) {
    const uint8_t* s = sprite != nullptr ? sprite : key + 0x30;
    if (sprite != nullptr) g.colour = gallery_colour(rd<uint32_t>(s + 0x18));
    const float w = ee::mul(rdf(s + 0x10), g.scale[0]);
    const float h = ee::mul(rdf(s + 0x14), g.scale[1]);
    // As repetições: os bytes +0x4b..+0x4e (a quantas vezes à esquerda, à direita, acima, abaixo).
    const int32_t r0 = int8_t(s[0x4b]), r1 = int8_t(s[0x4c]), r2 = int8_t(s[0x4d]), r3 = int8_t(s[0x4e]);
    int32_t step_x = 0, step_y = 0;
    if (r0 > 0) step_x = ee::cvt_w(ee::neg(w));
    if (r1 > 0) step_x = ee::cvt_w(w);
    if (r2 > 0) step_y = ee::cvt_w(ee::neg(h));
    if (r3 > 0) step_y = ee::cvt_w(h);
    int32_t n = r0 + r1 + r2 + r3;
    if (n == 0) n = 1;
    Draw_SetNormalBlendMode(draw_globals().textured);
    if (g.custom_state == 0) GGallery_STATE_SetDefaultRenderState(g);
    const uint32_t flags = rd<uint32_t>(s + 0x2c);
    int32_t px = x, py = y;
    auto uvs = [&](float a0, float a1, float b0, float b1, float out[2][4]) {
        out[0][0] = a0;
        out[0][1] = a1;
        out[0][2] = 1.0f;
        out[0][3] = 0.0f;
        out[1][0] = b0;
        out[1][1] = b1;
        out[1][2] = 1.0f;
        out[1][3] = 0.0f;
    };
    for (int32_t i = 0; i < n; ++i) {
        const float u0 = ee::add(rdf(s), kInset), u1 = ee::sub(rdf(s + 4), kInset);
        const float v0 = ee::add(rdf(s + 8), kInset), v1 = ee::sub(rdf(s + 0xc), kInset);
        const uint32_t tex = GGallery_GET_Texture(g, rd<int32_t>(s + 0x1c));
        float uv[2][4];
        if ((flags & 3) == 3) {
            // Os dois espelhamentos: os quatro quadrantes.
            const float fx = ee::cvt_s(px), fy = ee::cvt_s(py);
            const float x0 = ee::sub(fx, w), y0 = ee::sub(fy, h);
            uvs(u0, v0, u1, v1, uv);
            Draw_DrawSprite(fx, fy, w, h, g.colour, uv, tex);
            uvs(u1, v1, u0, v0, uv);
            Draw_DrawSprite(x0, y0, w, h, g.colour, uv, tex);
            // (o PS2 refaz o UV do registro a cada desenho)
            const float u0b = ee::add(rdf(s), kInset), u1b = ee::sub(rdf(s + 4), kInset);
            const float v0b = ee::add(rdf(s + 8), kInset), v1b = ee::sub(rdf(s + 0xc), kInset);
            uvs(u1b, v0b, u0b, v1b, uv);
            Draw_DrawSprite(ee::sub(ee::cvt_s(px), w), ee::cvt_s(py), w, h, g.colour, uv, tex);
            uvs(u0b, v1b, u1b, v0b, uv);
            Draw_DrawSprite(ee::cvt_s(px), ee::sub(ee::cvt_s(py), h), w, h, g.colour, uv, tex);
        } else if ((flags & 1) != 0) {
            uvs(u1, v0, u0, v1, uv);
            Draw_DrawSprite(ee::sub(ee::cvt_s(px), w), ee::cvt_s(py), w, h, g.colour, uv, tex);
        } else if ((flags & 2) != 0) {
            uvs(u0, v1, u1, v0, uv);
            Draw_DrawSprite(ee::cvt_s(px), ee::sub(ee::cvt_s(py), h), w, h, g.colour, uv, tex);
        } else {
            uvs(u0, v0, u1, v1, uv);
            Draw_DrawSprite(ee::cvt_s(px), ee::cvt_s(py), w, h, g.colour, uv, tex);
        }
        py += step_y;
        px += step_x;
    }
}

void GGallery_SPRITE_Draw2D(GGalleryData& g, const uint8_t* key) {
    GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(rdf(key)), ee::cvt_w(rdf(key + 4)), key, nullptr);
}

void GGallery_SPRITE_Flush(GGalleryData& g) {
    Draw_SetNormalBlendMode(draw_globals().textured);
    if (g.custom_state == 0) GGallery_STATE_SetDefaultRenderState(g);
    const float uv[4][4] = {};
    Draw_DrawBatchedSprite(0, 0, 0, 0, 0, uv, 0);
    Draw_DrawBatchedTexturedQuad(uv[0], uv[1], uv[2], uv[3], 0, uv, 0);
}

void GGallery_TRANSFORM_Vertices(float (*v)[4], int32_t n, const float pos[3], const float quat[4],
                                 const float scale[3]) {
    const float cq[4] = {ee::neg(quat[0]), ee::neg(quat[1]), ee::neg(quat[2]), quat[3]};
    float m[4][4] = {};
    VU0_quattom4(cq, m);
    m[3][0] = pos[0];
    m[3][1] = pos[1];
    m[3][2] = pos[2];
    for (int r = 0; r < 3; ++r) {
        vu::Reg row, k, out;
        std::memcpy(row, m[r], 16);
        const float sc = scale[r];
        std::memcpy(&k[0], &sc, 4);
        vu::mulbc(out, row, k, vu::BX, vu::XYZW);
        std::memcpy(m[r], out, 16);
    }
    for (int32_t i = 0; i < n; ++i) {
        vu::Reg p, acc, r0, r1, r2, r3, out;
        std::memcpy(p, v[i], 16);
        std::memcpy(r0, m[0], 16);
        std::memcpy(r1, m[1], 16);
        std::memcpy(r2, m[2], 16);
        std::memcpy(r3, m[3], 16);
        vu::mulabc(acc, r0, p, vu::BX, vu::XYZW);
        vu::maddabc(acc, r1, p, vu::BY, vu::XYZW);
        vu::maddabc(acc, r2, p, vu::BZ, vu::XYZW);
        vu::maddbc(out, acc, r3, p, vu::BW, vu::XYZW);
        std::memcpy(v[i], out, 16);
    }
}

void GSystem_MATH_TransformPoints(float point[4], const float trans[4], const float quat[4], const float scale[4]) {
    vu::Reg vf0 = {0, 0, 0, 0x3f800000u}, vf1, vf2, vf3, vf4, vf5, vf6, vf7, vf8, vf9, vf10, vf11, vf12;
    vu::load(vf1, quat);
    vu::load(vf11, scale);
    vu::load(vf12, trans);
    vu::sub(vf1, vf0, vf1, vu::XYZ);
    vu::mulbc(vf4, vf1, vf1, vu::BX, vu::XYZW);
    vu::mulbc(vf5, vf1, vf1, vu::BY, vu::XYZW);
    vu::mulbc(vf6, vf1, vf1, vu::BZ, vu::XYZW);
    vu::mulbc(vf7, vf1, vf1, vu::BW, vu::XYZW);
    vu::add(vf4, vf4, vf4, vu::XYZW);
    vu::add(vf5, vf5, vf5, vu::XYZW);
    vu::add(vf6, vf6, vf6, vu::XYZW);
    vu::add(vf7, vf7, vf7, vu::XYZW);
    vu::sub(vf8, vf0, vf0, vu::XYZW);
    vu::sub(vf9, vf0, vf0, vu::XYZW);
    vu::sub(vf10, vf0, vf0, vu::XYZW);
    vu::addbc(vf8, vf0, vf0, vu::BW, vu::X);
    vu::addbc(vf9, vf0, vf5, vu::BX, vu::X);
    vu::addbc(vf10, vf0, vf6, vu::BX, vu::X);
    vu::subbc(vf8, vf8, vf5, vu::BY, vu::X);
    vu::subbc(vf9, vf9, vf6, vu::BW, vu::X);
    vu::addbc(vf10, vf10, vf5, vu::BW, vu::X);
    vu::subbc(vf8, vf8, vf6, vu::BZ, vu::X);
    vu::addbc(vf9, vf0, vf0, vu::BW, vu::Y);
    vu::addbc(vf10, vf0, vf5, vu::BZ, vu::Y);
    vu::addbc(vf8, vf0, vf5, vu::BX, vu::Y);
    vu::subbc(vf9, vf9, vf4, vu::BX, vu::Y);
    vu::subbc(vf10, vf10, vf7, vu::BX, vu::Y);
    vu::addbc(vf8, vf8, vf6, vu::BW, vu::Y);
    vu::subbc(vf9, vf9, vf6, vu::BZ, vu::Y);
    vu::addbc(vf10, vf0, vf0, vu::BW, vu::Z);
    vu::addbc(vf8, vf0, vf6, vu::BX, vu::Z);
    vu::addbc(vf9, vf0, vf6, vu::BY, vu::Z);
    vu::subbc(vf10, vf10, vf4, vu::BX, vu::Z);
    vu::subbc(vf8, vf8, vf5, vu::BW, vu::Z);
    vu::addbc(vf9, vf9, vf7, vu::BX, vu::Z);
    vu::subbc(vf10, vf10, vf5, vu::BY, vu::Z);
    vu::sub(vf8, vf8, vf8, vu::W);
    vu::sub(vf9, vf9, vf9, vu::W);
    vu::sub(vf10, vf10, vf10, vu::W);
    vu::load(vf1, point);
    vu::mulbc(vf8, vf8, vf11, vu::BX, vu::XYZ);
    vu::mulbc(vf9, vf9, vf11, vu::BY, vu::XYZ);
    vu::mulbc(vf10, vf10, vf11, vu::BZ, vu::XYZ);
    vu::mul(vf2, vf1, vf8, vu::XYZ);
    vu::mul(vf3, vf1, vf9, vu::XYZ);
    vu::mul(vf4, vf1, vf10, vu::XYZ);
    vu::mulbc(vf1, vf0, vf0, vu::BW, vu::W);
    vu::addbc(vf1, vf2, vf2, vu::BY, vu::X);
    vu::addbc(vf1, vf3, vf3, vu::BX, vu::Y);
    vu::addbc(vf1, vf4, vf4, vu::BX, vu::Z);
    vu::addbc(vf1, vf1, vf2, vu::BZ, vu::X);
    vu::addbc(vf1, vf1, vf3, vu::BZ, vu::Y);
    vu::addbc(vf1, vf1, vf4, vu::BY, vu::Z);
    vu::add(vf1, vf1, vf12, vu::XYZ);
    vu::store(point, vf1);
}

uint32_t GSystem_COLOR_Interpolate(uint32_t a, uint32_t b, float t) {
    uint32_t r = a;
    for (int i = 0; i < 4; ++i) {
        const int sh = i * 8;
        const int32_t ca = int32_t((r >> sh) & 0xff), cb = int32_t((b >> sh) & 0xff);
        const float v = ee::add(ee::cvt_s(ca), ee::mul(ee::cvt_s(cb - ca), t));
        const uint32_t iv = uint32_t(ee::cvt_w(v));
        const uint32_t mask = i == 3 ? 0x00ffffffu : ~(0xffu << sh);
        r = (r & mask) | (i == 3 ? (iv << 24) : ((iv & 0xff) << sh));
    }
    return r;
}

void GSystem_COLOR_ReduceAlpha(uint32_t& c, float f) {
    const uint32_t a = c >> 24;
    const int32_t v = ee::cvt_w(ee::mul(ee::cvt_s(int32_t(a)), f));
    c = (c & 0x00ffffffu) | (uint32_t(v) << 24);
}

}  // namespace drv
