#include "driving/fx/fx_render.h"

#include <cstring>

#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {

inline uint32_t bf(float f) { return ps2::fbits(f); }
inline float fb(uint32_t w) { return ps2::ffloat(w); }

// p (linha, w 1) x M (linhas: p' = p x M), em float do PC.
void xform(const float p[4], const float m[16], float out[4]) {
    for (int c = 0; c < 4; ++c) out[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + p[3] * m[12 + c];
}

// O MULAx/MADDAy/MADDAz/MADDw da VU: p x as linhas (r0..r3).
void vu_xform(const float m[4][4], const uint32_t p[4], uint32_t out[4]) {
    uint32_t r0[4], r1[4], r2[4], r3[4], acc[4];
    load(r0, m[0], XYZW);
    load(r1, m[1], XYZW);
    load(r2, m[2], XYZW);
    load(r3, m[3], XYZW);
    mulabc(acc, r0, p, BX, XYZW);
    maddabc(acc, r1, p, BY, XYZW);
    maddabc(acc, r2, p, BZ, XYZW);
    maddbc(out, acc, r3, p, BW, XYZW);
}

}  // namespace

float fx_sprites_control_y(float fov, float guard_band) { return ee::div(66.0f, ee::mul(fov, guard_band)); }

void fx_particle_batches(const ParticleDrawLists& lists, const float view[16], const float proj[16], float control_y,
                         float aspect, std::vector<FxBatch>& out) {
    float vp[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            vp[r * 4 + c] = view[r * 4] * proj[c] + view[r * 4 + 1] * proj[4 + c] + view[r * 4 + 2] * proj[8 + c] +
                            view[r * 4 + 3] * proj[12 + c];
    const float ax = aspect > 0.0f ? (4.0f / 3.0f) / aspect : 1.0f;
    auto sprites = [&](const ParticleDrawLists::Sprites& l, uint64_t alpha, FxBatch& b) {
        const size_t n = l.colours.size();
        for (size_t i = 0; i < n; ++i) {
            const float* co = &l.coords[i * 4];
            const float* uv = &l.uvuv[i * 4];
            const float p[4] = {co[0], co[1], co[2], 1.0f};
            float c[4];
            xform(p, vp, c);
            const float r = control_y * co[3];  // o raio no recorte
            const float s = nf::fm::sin(uv[3]), k = nf::fm::cos(uv[3]);
            // v1 = (cos, sen) r, v2 = (-sen, cos) r em coordenadas normalizadas (y para cima), o x pela proporção 4:3.
            const float v1[2] = {k * r * ax, s * r}, v2[2] = {-s * r * ax, k * r};
            const float corner[4][2] = {{-v2[0], -v2[1]}, {v1[0], v1[1]}, {-v1[0], -v1[1]}, {v2[0], v2[1]}};
            const float st[4][2] = {{uv[0], uv[1]}, {uv[0] + uv[2], uv[1]}, {uv[0], uv[1] + uv[2]},
                                    {uv[0] + uv[2], uv[1] + uv[2]}};
            FxVertex v[4];
            for (int j = 0; j < 4; ++j) {
                v[j].pos[0] = c[0] + corner[j][0];
                v[j].pos[1] = c[1] + corner[j][1];
                v[j].pos[2] = c[2];
                v[j].pos[3] = c[3];
                v[j].uv[0] = st[j][0];
                v[j].uv[1] = st[j][1];
                v[j].colour = l.colours[i];
            }
            if (c[3] <= 0.0f) continue;  // atrás do olho (o PS2: o CLIPw e o ADC)
            for (int j : {0, 1, 2, 1, 2, 3}) b.verts.push_back(v[j]);
        }
        b.alpha = alpha;
    };
    for (int blk = 0; blk < 2; ++blk) {
        if (!lists.normal[blk].colours.empty()) {
            FxBatch b;
            b.texture = kFxTexMain;
            sprites(lists.normal[blk], 0x8000000044ull, b);
            if (!b.verts.empty()) out.push_back(std::move(b));
        }
        if (!lists.additive[blk].colours.empty()) {
            FxBatch b;
            b.texture = kFxTexMain;
            sprites(lists.additive[blk], 0x8000000048ull, b);
            if (!b.verts.empty()) out.push_back(std::move(b));
        }
    }
    for (int blk = 0; blk < 2; ++blk) {
        if (lists.tri_colours[blk].empty()) continue;
        FxBatch b;
        b.texture = kFxTexSpark;
        b.world = true;
        b.alpha = 0x8000000068ull;
        static const float kStq[3][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}};  // 0x40fdb0
        for (size_t i = 0; i < lists.tri_colours[blk].size(); ++i) {
            FxVertex v;
            std::memcpy(v.pos, &lists.tri_coords[blk][i * 4], 16);
            v.uv[0] = kStq[i % 3][0];
            v.uv[1] = kStq[i % 3][1];
            v.colour = lists.tri_colours[blk][i];
            b.verts.push_back(v);
        }
        out.push_back(std::move(b));
    }
}

// ----------------------------------------------------------------------------------------------- as referências

// RenderSpritesRot (bondrm.o; a cópia do DRIVING.ELF em 0x34cc20): um retângulo, os passos na ordem do microprograma.
void render_sprites_rot_reference(const FxVuGlobals& g, const float mvp[4][4], const float control[4],
                                  const float coord[4], const float uvuv[4], const uint32_t colour[4],
                                  GsVertexOut out[6]) {
    uint32_t v05[4], v06[4], v25[4] = {0, 0, 0, 0}, v22[4], v07[4], v08[4], v09[4], v10[4], v11[4], v30[4] = {};
    load(v05, g.vp_scale, XYZW);
    load(v06, g.vp_offset, XYZW);
    load(v25, control, YZ);
    mulbc(v22, v05, v25, BY, XYZ);
    load(v07, coord, XYZW);
    // vf11 = (0, 0, 0, 1) + w: o raio (MOVE vf11, vf00; ADDw.xy vf11 = vf11 + vf07w).
    v11[0] = v11[1] = v11[2] = 0;
    v11[3] = bf(1.0f);
    addbc(v11, v11, v07, BW, X | Y);
    // clip = (x, y, z) x MVP + linha 3 x w(vf00 = 1)
    uint32_t p1[4] = {v07[0], v07[1], v07[2], bf(1.0f)};
    {
        uint32_t r0[4], r1[4], r2[4], r3[4], acc[4];
        load(r0, mvp[0], XYZW);
        load(r1, mvp[1], XYZW);
        load(r2, mvp[2], XYZW);
        load(r3, mvp[3], XYZW);
        mulabc(acc, r0, v07, BX, XYZW);
        maddabc(acc, r1, v07, BY, XYZW);
        maddabc(acc, r2, v07, BZ, XYZW);
        maddbc(v08, acc, r3, p1, BW, XYZW);
    }
    mulbc(v30, v25, v07, BW, Z);  // vf30.z = Control.z x tamanho
    // CLIPw.xyz: fora se |x|, |y| ou |z| > |w|.
    const float w = fb(v08[3]);
    const float aw = w < 0 ? -w : w;
    bool clipped = false;
    for (int c = 0; c < 3; ++c) {
        const float x = fb(v08[c]);
        if (x > aw || x < -aw) clipped = true;
    }
    mul(v09, v08, v05, XYZ);
    const uint32_t q = ps2::div(bf(1.0f), v08[3]);
    v10[3] = 0;  // SUB.w vf10 = vf00 - vf00
    add(v30, v09, v30, Z);  // ADDz.z vf30 = vf09.z + vf30.z
    const bool neg_z = (v30[2] & 0x80000000u) != 0 && (v30[2] & 0x7fffffffu) != 0;
    if (clipped || !neg_z) v10[3] = ps2::sub(bf(2049.0f), bf(1.0f));  // vf31.w - vf00.w = 2048: o ADC
    // vf10.xyz = vf06 + vf09 x Q (ADDA + MADDq)
    for (int c = 0; c < 3; ++c) v10[c] = ps2::add(v06[c], ps2::mul(v09[c], q));
    // vf17 = (u, v, 1, 0); vf18 = u + lado; vf19 = v + lado; vf20 = u, v + lado.
    uint32_t v17[4] = {bf(uvuv[0]), bf(uvuv[1]), bf(1.0f), 0}, v21[4];
    load(v21, uvuv, XYZW);
    uint32_t v18[4], v19[4], v20[4];
    std::memcpy(v18, v17, 16);
    std::memcpy(v19, v17, 16);
    std::memcpy(v20, v17, 16);
    v18[0] = ps2::add(v17[0], v21[2]);
    v19[1] = ps2::add(v17[1], v21[2]);
    v20[0] = ps2::add(v17[0], v21[2]);
    v20[1] = ps2::add(v17[1], v21[2]);
    v11[0] = ps2::mul(v11[0], q);
    v11[1] = ps2::mul(v11[1], q);
    v11[0] = ps2::mul(v11[0], v22[0]);
    v11[1] = ps2::mul(v11[1], v22[1]);
    // ESIN do giro e do giro + 1,5708: vf26 = (cos, sen), vf27 = (-sen, cos), vezes vf11 lane a lane.
    const uint32_t s = ps2::efu_sin(v21[3]);
    const uint32_t cs = ps2::efu_sin(ps2::add(v21[3], 0x3fc90ff9u));
    uint32_t v26[4] = {cs, s, 0, 0}, v27[4] = {ps2::mul(s, bf(-1.0f)), cs, 0, 0};
    for (int c = 0; c < 4; ++c) {
        v27[c] = ps2::mul(v27[c], v11[c]);
        v26[c] = ps2::mul(v26[c], v11[c]);
    }
    uint32_t v13[4], v14[4], v15[4], v16[4];
    std::memcpy(v13, v10, 16);
    std::memcpy(v14, v10, 16);
    std::memcpy(v15, v10, 16);
    std::memcpy(v16, v10, 16);
    for (int c = 0; c < 2; ++c) {
        v13[c] = ps2::sub(v10[c], v27[c]);
        v16[c] = ps2::add(v10[c], v27[c]);
        v14[c] = ps2::add(v10[c], v26[c]);
        v15[c] = ps2::sub(v10[c], v26[c]);
    }
    auto ftoi4 = [](const uint32_t in[4], uint32_t o[4]) {
        for (int c = 0; c < 4; ++c) o[c] = uint32_t(ps2::to_int(in[c], 4));
    };
    const uint32_t* sts[6] = {v17, v18, v19, v18, v19, v20};
    const uint32_t* xyzs[6] = {v13, v14, v15, v14, v15, v16};
    for (int k = 0; k < 6; ++k) {
        std::memcpy(out[k].st, sts[k], 16);
        std::memcpy(out[k].rgba, colour, 16);
        ftoi4(xyzs[k], out[k].xyz);
    }
}

// MatDebugTexture, o caminho sem recorte (eaglrm.o; a cópia do DRIVING.ELF em 0x34a280).
void mat_debug_texture_reference(const FxVuGlobals& g, const float mvp[4][4], const float pos[4], const float stq[4],
                                 const uint32_t colour[4], GsVertexOut& out) {
    uint32_t v05[4], v06[4], p[4], v08[4], v09[4], v10[4] = {0, 0, 0, 0}, v11[4];
    load(v05, g.vp_scale, XYZW);
    load(v06, g.vp_offset, XYZW);
    load(p, pos, XYZW);
    vu_xform(mvp, p, v08);
    mul(v09, v08, v05, XYZ);
    const uint32_t q = ps2::div(bf(1.0f), v08[3]);
    for (int c = 0; c < 3; ++c) v10[c] = ps2::add(v06[c], ps2::mul(v09[c], q));
    load(v11, stq, XYZW);
    for (int c = 0; c < 3; ++c) v11[c] = ps2::mul(v11[c], q);
    std::memcpy(out.st, v11, 16);
    std::memcpy(out.rgba, colour, 16);
    for (int c = 0; c < 4; ++c) out.xyz[c] = uint32_t(ps2::to_int(v10[c], 4));
}

}  // namespace drv
