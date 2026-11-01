// O 2D do HUD das missões de carro (driving/render/hud_gfx.h; docs/driving.md seção 28).
#include "driving/render/hud_gfx.h"

#include <cstring>

#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {
DrawGlobals g_draw;
std::vector<HudFrameItem> g_frame;

void put4(float* d, float a, float b, float c, float e) {
    d[0] = a;
    d[1] = b;
    d[2] = c;
    d[3] = e;
}
}  // namespace

std::vector<HudFrameItem>& hud_frame() { return g_frame; }
DrawGlobals& draw_globals() { return g_draw; }
void draw_power_on() {
    g_draw = DrawGlobals();
    g_frame.clear();
}

void Draw_SetModelMatrix(const float* m) { g_draw.model = m; }
void Draw_SetZDepth(float z) { g_draw.z = z; }
void Draw_SetZDepthFar() { g_draw.z = ps2::ffloat(0x411fd70au); }
void Draw_SetZDepthNear() { g_draw.z = 1.0f; }

void Draw_SetNormalBlendMode(HudPrimState& s) {
    s.alpha = 0x8000000044ull;
    s.flags = (s.flags & ~0x40ull) | 0x40;
}

void Draw_SetAdditiveBlendMode(HudPrimState& s, uint32_t fix) {
    s.alpha = (uint64_t(fix) << 32) | 0x68;
    s.flags = (s.flags & ~0x40ull) | 0x40;
}

void USimpleMaterial_Draw(const HudPrimState& s, int32_t prim, int32_t count, const float* model, const float* pos,
                          const uint32_t* colour) {
    HudFrameItem it;
    HudMatDraw& d = it.draw;
    d.textured = false;
    d.prim = prim;
    d.state = s;
    if (model) {
        d.has_matrix = true;
        std::memcpy(d.matrix, model, sizeof d.matrix);
    }
    d.pos.assign(pos, pos + size_t(count) * 4);
    d.colour.assign(colour, colour + count);
    g_frame.push_back(std::move(it));
}

void USimpleTexturedMaterial_Draw(const HudPrimState& s, int32_t prim, int32_t count, const float* model,
                                  const float* pos, const float* stq, const uint32_t* colour, uint32_t tex) {
    HudFrameItem it;
    HudMatDraw& d = it.draw;
    d.textured = true;
    d.prim = prim;
    d.image = tex;
    d.state = s;
    if (model) {
        d.has_matrix = true;
        std::memcpy(d.matrix, model, sizeof d.matrix);
    }
    d.pos.assign(pos, pos + size_t(count) * 4);
    d.stq.assign(stq, stq + size_t(count) * 4);
    d.colour.assign(colour, colour + count);
    g_frame.push_back(std::move(it));
}

void Draw_DrawSprite(float x, float y, float w, float h, uint32_t colour, const float uv[2][4], uint32_t tex) {
    if (g_draw.model == nullptr) {
        float pos[2][4];
        put4(pos[0], x, y, g_draw.z, 1.0f);
        put4(pos[1], ee::add(x, w), ee::add(y, h), g_draw.z, 1.0f);
        const uint32_t col[2] = {colour, colour};
        USimpleTexturedMaterial_Draw(g_draw.textured, 10, 2, nullptr, &pos[0][0], &uv[0][0], col, tex);
        return;
    }
    const float x1 = ee::add(x, w), y1 = ee::add(y, h);
    float pos[4][4], stq[4][4];
    put4(pos[0], x, y, g_draw.z, 1.0f);
    put4(pos[1], x1, y, g_draw.z, 1.0f);
    put4(pos[2], x, y1, g_draw.z, 1.0f);
    put4(pos[3], x1, y1, g_draw.z, 1.0f);
    put4(stq[0], uv[0][0], uv[0][1], 1.0f, 1.0f);
    put4(stq[1], uv[1][0], uv[0][1], 1.0f, 1.0f);
    put4(stq[2], uv[0][0], uv[1][1], 1.0f, 1.0f);
    put4(stq[3], uv[1][0], uv[1][1], 1.0f, 1.0f);
    const uint32_t col[4] = {colour, colour, colour, colour};
    USimpleTexturedMaterial_Draw(g_draw.textured, 5, 4, g_draw.model, &pos[0][0], &stq[0][0], col, tex);
}

void Draw_DrawBox(float x, float y, float w, float h, uint32_t colour) {
    float pos[2][4];
    put4(pos[0], x, y, g_draw.z, 1.0f);
    put4(pos[1], ee::add(x, w), ee::add(y, h), g_draw.z, 1.0f);
    const uint32_t col[2] = {colour, colour};
    USimpleMaterial_Draw(g_draw.untextured, 10, 2, g_draw.model, &pos[0][0], col);
}

void Draw_DrawQuadC(const float corners[4][4], const uint32_t colours[4]) {
    // 0x0019f850: as posições e as cores na camada, a faixa de quatro.
    USimpleMaterial_Draw(g_draw.untextured, 5, 4, g_draw.model, &corners[0][0], colours);
}

void Draw_DrawQuad(const float corners[4][4], uint32_t colour) {
    const uint32_t col[4] = {colour, colour, colour, colour};
    Draw_DrawQuadC(corners, col);
}

void Draw_DrawBatchedSprite(float x, float y, float w, float h, uint32_t colour, const float uv[2][4], uint32_t tex) {
    DrawGlobals& g = g_draw;
    if (g.sprite_tex != tex || 0x44 < g.sprite_count) {
        if (g.sprite_tex != 0)
            USimpleTexturedMaterial_Draw(g.textured, 10, g.sprite_count, nullptr, &g.sprite_pos[0][0],
                                         &g.sprite_stq[0][0], g.sprite_col, g.sprite_tex);
        g.sprite_count = 0;
        g.sprite_tex = tex;
    }
    if (tex == 0) return;
    int n = g.sprite_count;
    put4(g.sprite_pos[n], x, y, g.z, 1.0f);
    std::memcpy(g.sprite_stq[n], uv[0], 16);
    g.sprite_col[n] = colour;
    ++n;
    put4(g.sprite_pos[n], ee::add(x, w), ee::add(y, h), g.z, 1.0f);
    std::memcpy(g.sprite_stq[n], uv[1], 16);
    g.sprite_col[n] = colour;
    g.sprite_count = n + 1;
}

void Draw_DrawBatchedTexturedQuad(const float p0[4], const float p1[4], const float p2[4], const float p3[4],
                                  uint32_t colour, const float uv[4][4], uint32_t tex) {
    DrawGlobals& g = g_draw;
    if (g.quad_tex != tex || 0x44 < g.quad_count) {
        if (g.quad_tex != 0)
            USimpleTexturedMaterial_Draw(g.textured, 4, g.quad_count, nullptr, &g.quad_pos[0][0], &g.quad_stq[0][0],
                                         g.quad_col, g.quad_tex);
        g.quad_count = 0;
        g.quad_tex = tex;
    }
    if (tex == 0) return;
    const float* ps[6] = {p0, p1, p2, p1, p2, p3};
    const int us[6] = {0, 1, 2, 1, 2, 3};
    for (int i = 0; i < 6; ++i) {
        std::memcpy(g.quad_pos[g.quad_count], ps[i], 16);
        std::memcpy(g.quad_stq[g.quad_count], uv[us[i]], 16);
        g.quad_col[g.quad_count] = colour;
        ++g.quad_count;
    }
}

void hud_frame_to_batches(const std::vector<HudFrameItem>& items, std::vector<Gs2dBatch>& out, const float* view) {
    for (const HudFrameItem& it : items) {
        if (it.is_packet) {
            Gs2dState s;
            gs2d_run(it.packet.words.data(), it.packet.words.size(), s, out);
            continue;
        }
        const HudMatDraw& d = it.draw;
        const size_t n = d.colour.size();
        if (n == 0) continue;
        std::vector<Gs2dVertex> v(n);
        for (size_t i = 0; i < n; ++i) {
            float p[4] = {d.pos[i * 4], d.pos[i * 4 + 1], d.pos[i * 4 + 2], d.pos[i * 4 + 3]};
            if (d.has_matrix) {
                float r[4];
                for (int c = 0; c < 4; ++c)
                    r[c] = p[0] * d.matrix[0][c] + p[1] * d.matrix[1][c] + p[2] * d.matrix[2][c] + p[3] * d.matrix[3][c];
                std::memcpy(p, r, sizeof p);
            }
            if (view) {
                p[0] = p[0] * view[0] + view[2];
                p[1] = p[1] * view[1] + view[3];
            }
            v[i].x = p[0];
            v[i].y = p[1];
            v[i].rgba = d.colour[i];
            if (d.textured) {
                const float q = d.stq[i * 4 + 2] != 0.0f ? d.stq[i * 4 + 2] : 1.0f;
                v[i].u = d.stq[i * 4] / q;
                v[i].v = d.stq[i * 4 + 1] / q;
            }
        }
        Gs2dBatch b;
        b.textured = d.textured;
        b.st = true;
        b.image = d.image;
        b.linear = true;
        b.blend = (d.state.flags & 0x40) != 0;
        b.alpha = d.state.alpha;
        b.test = 0x30000ull;
        b.zwrite = false;
        auto tri = [&](size_t a, size_t c, size_t e) {
            b.verts.push_back(v[a]);
            b.verts.push_back(v[c]);
            b.verts.push_back(v[e]);
        };
        if (d.prim == 10) {  // SPRITE: dois vértices (o canto e o oposto)
            for (size_t i = 0; i + 1 < n; i += 2) {
                const Gs2dVertex& p = v[i];
                const Gs2dVertex& q = v[i + 1];
                Gs2dVertex a = p, c = p, e = p, f = q;
                c.x = q.x;
                c.u = q.u;
                e.y = q.y;
                e.v = q.v;
                a.rgba = c.rgba = e.rgba = f.rgba = q.rgba;
                b.verts.insert(b.verts.end(), {a, c, e, e, c, f});
            }
        } else if (d.prim == 4) {
            for (size_t i = 0; i + 2 < n; i += 3) tri(i, i + 1, i + 2);
        } else if (d.prim == 5) {
            for (size_t i = 0; i + 2 < n; ++i) tri(i, i + 1, i + 2);
        }
        if (!b.verts.empty()) out.push_back(std::move(b));
    }
}

}  // namespace drv
