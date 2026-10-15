// A família MatBond do mundo (mat_bond.h): o leiaute dos lotes, a referência na CPU do microprograma da VU1 e a
// conferência contra ele.
//
// A referência faz as contas com a aritmética da VU (platform/ps2_float.h: truncamento, sem denormais), na ordem das
// instruções (cada MULA/MADDA/MADD arredonda), então o que sai dela é o que o microprograma grava, bit a bit, menos o
// ADC do recorte (mat_bond.h).

#include "driving/mat_bond.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "driving/mat_car_verify.h"
#include "platform/ps2_float.h"
#include "platform/vu_interp.h"

namespace drv {

namespace {

// Os endereços dos "IADDIU viN, vi14, X", "LQ/ILW X(vi14)" e dos XGKICK de cada microprograma
// (out/driving/vu_<método>.txt).
const BondMethodLayout kLayouts[] = {
    // name, passes, packed, max, count, mvp, world, tar, tar_env, camera, split, pos, stq, colour, state, gloss_state,
    // env_stq, gif_tag, out, kind, lights, normal, sim_step, stq1, scroll, frame_anim
    {"MatBondTexturePacked", kBondBase, true, 72, 0, 1, 5, 9, 0, 0, 0x10, 0x13, 0x5b, 0xab, 0xa3, 0, 0, 0xf3, 0xf4},
    {"MatBondTexture", kBondBase, false, 72, 0, 1, 5, 9, 0, 0, 0x10, 0x13, 0x5b, 0xa3, 0xeb, 0, 0, 0xf3, 0xf4},
    {"MatBondReflectTexturePacked", kBondBase | kBondEnv, true, 60, 0, 1, 5, 9, 0x10, 0x17, 0x18, 0x1b, 0x57, 0x93,
     0xcf, 0xd7, 0xe0, 0x11c, 0x11d, kBondReflect},
    // O Dynamic: a cor de trabalho em 0xd6 (enchida com 127 e iluminada pelo passo 2b), sem fluxo de cor.
    {"MatDynamicTexturePacked", kBondBase, true, 60, 0, 1, 5, 9, 0, 0, 0x17, 0x1a, 0x56, 0xd6, 0xce, 0, 0, 0x112, 0x113,
     kBondDynamic, 0x10, 0x92},
    // Os Scroll2: a segunda passada com o State de novo (gloss_state = state) e o TAR do Texture1 (tar_env).
    {"MatBondScrollTexture2Packed", kBondBase | kBondEnv, true, 60, 0, 1, 5, 9, 0x10, 0, 0x18, 0x1b, 0x57, 0xcf, 0x10b,
     0x10b, 0, 0x11d, 0x11e, kBondScroll2, 0, 0, 0x17, 0x93, 0x11b},
    {"MatBondScrollTexture2", kBondBase | kBondEnv, false, 60, 0, 1, 5, 9, 0x10, 0, 0x18, 0x1b, 0x57, 0xe1, 0xcf, 0xcf,
     0, 0x11d, 0x11e, kBondScroll2, 0, 0, 0x17, 0x93, 0xdf},
    {"MatBondFrameAnimPacked", kBondBase, true, 72, 0, 1, 5, 9, 0, 0, 0x11, 0x14, 0x5c, 0xa4, 0xec, 0, 0, 0xf5, 0xf6,
     kBondFrameAnim, 0, 0, 0x10, 0, 0, 0xf4},
};

// A aritmética do PS2 (a VU e a FPU do EE arredondam igual), com nomes que não colidem com os da biblioteca.
float vadd(float a, float b) { return ps2::ee::add(a, b); }
float vsub(float a, float b) { return ps2::ee::sub(a, b); }
float vmul(float a, float b) { return ps2::ee::mul(a, b); }
float vdiv(float a, float b) { return ps2::ee::div(a, b); }
float vmax(float a, float b) { return ps2::ee::max(a, b); }
float vmin(float a, float b) { return ps2::ee::min(a, b); }
float vrsqrt(float a, float b) { return ps2::ee::rsqrt(a, b); }

float fl(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
float madd(float acc, float a, float b) { return ps2::ffloat(ps2::madd(ps2::fbits(acc), ps2::fbits(a), ps2::fbits(b))); }
float msub(float acc, float a, float b) { return ps2::ffloat(ps2::msub(ps2::fbits(acc), ps2::fbits(a), ps2::fbits(b))); }
float itof0(uint32_t i) { return ps2::ffloat(ps2::from_int(int32_t(i))); }
int32_t ftoi(float v, int frac) { return ps2::to_int(ps2::fbits(v), frac); }
// FTOI0 seguido de ITOF0 (a parte inteira truncada).
float trunc_vu(float v) { return ps2::ffloat(ps2::from_int(ps2::to_int(ps2::fbits(v), 0))); }

// p x M (ACC = M0 x, MADDA M1 y, MADDA M2 z, MADD M3 w): a ordem do microcódigo.
void transform(const float m[4][4], const float p[4], float w, float out[4]) {
    for (int c = 0; c < 4; ++c) {
        float acc = vmul(m[0][c], p[0]);
        acc = madd(acc, m[1][c], p[1]);
        acc = madd(acc, m[2][c], p[2]);
        out[c] = madd(acc, m[3][c], w);
    }
}

}  // namespace

const BondMethodLayout* bond_method_layout(const char* name) {
    if (!name) return nullptr;
    for (const BondMethodLayout& l : kLayouts)
        if (std::strcmp(l.name, name) == 0) return &l;
    return nullptr;
}

bool bond_decode_batch(const VuMemory& m, uint32_t top, const BondMethodLayout& l, BondBatch& b) {
    const int t = int(top);
    b.layout = &l;
    b.count = m.i(t + l.count, 0);
    if (b.count <= 0 || b.count > l.max_verts) return false;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            b.mvp[r][c] = m.f(t + l.mvp + r, c);
            b.world[r][c] = m.f(t + l.world + r, c);
        }
    for (int c = 0; c < 4; ++c) b.camera[c] = l.camera ? m.f(t + l.camera, c) : 0.0f;
    auto packet = [&](int at, int qw, eagl::GsRegs& regs) {
        uint32_t buf[16 * 4];
        for (int i = 0; i < qw * 4; ++i) buf[i] = m.u(t + at + i / 4, i % 4);
        eagl::gs_read_packet(reinterpret_cast<const uint8_t*>(buf), qw, regs);
    };
    b.pass[0] = eagl::GsRegs();
    b.pass[1] = eagl::GsRegs();
    packet(l.state, 8, b.pass[0]);
    packet(l.tar, 7, b.pass[0]);
    if (l.passes & kBondEnv) {
        packet(l.gloss_state, 8, b.pass[1]);
        packet(l.tar_env, 7, b.pass[1]);
    }
    b.strip = (m.u(t + l.state + 1, 0) & 7) != 3;
    for (int i = 0; i < 12; ++i) b.split[i] = int(m.u(t + l.split + i / 4, i % 4) & 0xFFFF);
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 4; ++c) b.lights[r][c] = l.lights ? m.f(t + l.lights + r, c) : 0.0f;
    for (int c = 0; c < 4; ++c) {
        b.sim_step[c] = l.sim_step ? m.f(t + l.sim_step, c) : 0.0f;
        b.frame_anim[c] = l.frame_anim ? m.f(t + l.frame_anim, c) : 0.0f;
    }
    for (int k = 0; k < 2; ++k)
        for (int c = 0; c < 2; ++c) b.scroll[k][c] = l.scroll ? m.f(t + l.scroll + k, c) : 0.0f;
    b.v.resize(size_t(b.count));
    for (int i = 0; i < b.count; ++i) {
        BondVertexIn& v = b.v[size_t(i)];
        for (int c = 0; c < 4; ++c) v.pos[c] = m.u(t + l.pos + i, c);
        for (int c = 0; c < 3; ++c) v.stq[c] = m.u(t + l.stq + i, c);
        // O Dynamic enche a cor de trabalho com ADDi.xyzw vf00 + 127 e FTOI0: (127, 127, 127, 128).
        for (int c = 0; c < 4; ++c) v.col[c] = l.kind == kBondDynamic ? (c == 3 ? 128u : 127u) : m.u(t + l.colour + i, c);
        for (int c = 0; c < 3; ++c) {
            v.normal[c] = l.normal ? m.u(t + l.normal + i, c) : 0u;
            v.stq1[c] = l.stq1 ? m.u(t + l.stq1 + i, c) : 0u;
        }
    }
    return true;
}

BondVertexWork bond_convert(const BondMethodLayout& l, const BondVertexIn& v) {
    BondVertexWork o{};
    if (l.packed) {
        // ITOF0, MULAi por 1/256 (o I de 0x16) e MADDw com -128 (vf19 = vf00 + I(-128)) x 1.
        const float k = 0.00390625f;
        for (int c = 0; c < 3; ++c) o.pos[c] = madd(vmul(itof0(v.pos[c]), k), -128.0f, 1.0f);
        o.pos[3] = 1.0f;  // ADDx.w vf17, vf00, vf00x
        for (int c = 0; c < 2; ++c) o.stq[c] = madd(vmul(itof0(v.stq[c]), k), -128.0f, 1.0f);
        o.stq[2] = 1.0f;  // ADDw.z vf18, vf00, vf00w
    } else {
        for (int c = 0; c < 4; ++c) o.pos[c] = fl(v.pos[c]);
        for (int c = 0; c < 3; ++c) o.stq[c] = fl(v.stq[c]);
    }
    for (int c = 0; c < 4; ++c) o.col[c] = float(int32_t(v.col[c]));
    // A segunda STQ dos Scroll2 fica crua (nem o Packed a converte); a normal do Dynamic já vem em float.
    for (int c = 0; c < 3; ++c) {
        o.stq1[c] = fl(v.stq1[c]);
        o.nrm[c] = fl(v.normal[c]);
    }
    return o;
}

void bond_env_stq(const float world[4][4], const float camera[4], const float pos[4], float out[3]) {
    // vf07 = CameraPos; SUBi.y vf07 -= 5; SUB.xyz vf13 (a linha 3) -= vf07; o ponto pelas linhas (MADD com vf00w = 1).
    const float cy = vsub(camera[1], 5.0f);
    const float cam[3] = {camera[0], cy, camera[2]};
    float d[3];
    for (int c = 0; c < 3; ++c) {
        float acc = vmul(world[0][c], pos[0]);
        acc = madd(acc, world[1][c], pos[1]);
        acc = madd(acc, world[2][c], pos[2]);
        d[c] = madd(acc, vsub(world[3][c], cam[c]), 1.0f);
    }
    // ERLENG: 1 / sqrt(x^2 + y^2 + z^2) (RSQRT 1 pela soma, como o interpretador da VU).
    const float len2 = vadd(vadd(vmul(d[0], d[0]), vmul(d[1], d[1])), vmul(d[2], d[2]));
    const float p = vrsqrt(1.0f, len2);
    for (int c = 0; c < 3; ++c) d[c] = vmul(vmul(d[c], p), 0.4375f);
    out[0] = vadd(0.5f, d[0]);
    out[1] = vadd(0.5f, d[2]);
    out[2] = 1.0f;
}

// O passo 3b dos Scroll2 (MULx.xy vf12, vf13, vf12x; FTOI0.xy; ITOF0.xy; SUB.xy).
void bond_scroll_offset(const float uv[2], float t, float out[2]) {
    for (int c = 0; c < 2; ++c) {
        const float v = vmul(uv[c], t);
        out[c] = vsub(v, trunc_vu(v));
    }
}

// O passo 3b do FrameAnim (SUBw.x, MUL.x, FTOI0/ITOF0, SUB.x, MULz.x, FTOI0/ITOF0, MULx.y).
float bond_frame_offset(const float fa[4], const float ss[4]) {
    float x = vsub(ss[0], ss[3]);
    x = vmul(fa[0], x);
    x = vsub(x, trunc_vu(x));
    x = vmul(x, fa[2]);
    return vmul(fa[1], trunc_vu(x));
}

// O passo 2b do Dynamic (as instruções 0x01f-0x066: a cor de trabalho (127, 127, 127, 128), o ADDi.xyzw vf00 + 127 com o
// w de vf00 = 1; M = as linhas da WorldMatrix x L; d = n x M; a soma das cores pelos d positivos e o ambiente; x 127; o
// mínimo de 255; FTOI0; o alfa 128 movido em float e convertido).
void bond_dynamic_colour(const float L[7][4], const float W[4][4], const float n[3], int32_t out[4]) {
    float r[3][3];
    for (int k = 0; k < 3; ++k)
        for (int c = 0; c < 3; ++c) {
            float acc = vmul(L[0][c], W[k][0]);
            acc = madd(acc, L[1][c], W[k][1]);
            r[k][c] = madd(acc, L[2][c], W[k][2]);
        }
    float dp[3];
    for (int c = 0; c < 3; ++c) {
        float acc = vmul(r[0][c], n[0]);
        acc = madd(acc, r[1][c], n[1]);
        dp[c] = vmax(madd(acc, r[2][c], n[2]), 0.0f);
    }
    const float base = itof0(127u), base_w = itof0(128u);
    for (int c = 0; c < 3; ++c) {
        float acc = vmul(L[3][c], dp[0]);
        acc = madd(acc, L[4][c], dp[1]);
        acc = madd(acc, L[5][c], dp[2]);
        const float s = madd(acc, L[6][c], 1.0f);
        out[c] = ftoi(vmin(vmul(s, base), 255.0f), 0);
    }
    out[3] = ftoi(base_w, 0);
}

void bond_reference(const BondBatch& b, const CarConstants& k, std::vector<CarVertexOut> out[2]) {
    const int n = b.count;
    const BondMethodLayout& l = *b.layout;
    const int passes = l.passes;
    out[0].assign(size_t(n), CarVertexOut{});
    out[1].assign(size_t((passes & kBondEnv) ? n : 0), CarVertexOut{});
    std::vector<bool> adc(size_t(n) + 2, false);
    for (int s : b.split) {
        const int v = s / 3;
        if (v < n) adc[size_t(v)] = true;
        if (v + 1 < n) adc[size_t(v) + 1] = true;
    }
    float negk[4];
    for (int L = 0; L < 4; ++L) negk[L] = vmul(k.point_pos[3][L], -1.0f);  // MULi.xyzw vf11, vf11, I(-1)
    // O passo 3b, o mesmo para todos os vértices do lote.
    float off0[2] = {0, 0}, off1[2] = {0, 0}, off_t = 0;
    if (l.kind == kBondScroll2) {
        bond_scroll_offset(b.scroll[0], b.sim_step[0], off0);
        bond_scroll_offset(b.scroll[1], b.sim_step[0], off1);
    } else if (l.kind == kBondFrameAnim) {
        off_t = bond_frame_offset(b.frame_anim, b.sim_step);
    }
    for (int i = 0; i < n; ++i) {
        const BondVertexWork v = bond_convert(l, b.v[size_t(i)]);
        // 2b. (Dynamic) a cor de partida pelas três luzes do lote.
        int32_t base_col[4];
        if (l.kind == kBondDynamic) {
            bond_dynamic_colour(b.lights, b.world, v.nrm, base_col);
        } else {
            for (int c = 0; c < 4; ++c) base_col[c] = int32_t(b.v[size_t(i)].col[c]);
        }
        // 3. a cor: a de partida + as luzes pontuais (o ponto no mundo com vf00w = 1).
        float pw[4];
        transform(b.world, v.pos, 1.0f, pw);
        float att[4];
        for (int L = 0; L < 4; ++L) {
            const float dx = vsub(k.point_pos[0][L], pw[0]), dy = vsub(k.point_pos[1][L], pw[1]),
                        dz = vsub(k.point_pos[2][L], pw[2]);
            float acc = vmul(dx, dx);
            acc = madd(acc, dy, dy);
            const float d2 = madd(acc, dz, dz);
            att[L] = vmax(madd(vmul(d2, negk[L]), 255.0f, 1.0f), 0.0f);
        }
        int32_t col[4];
        for (int c = 0; c < 3; ++c) {
            float acc = vmul(itof0(uint32_t(base_col[c])), 1.0f);  // MULAw ACC, vf20, vf00w
            acc = madd(acc, k.point_col[0][c], att[0]);
            acc = madd(acc, k.point_col[1][c], att[1]);
            acc = madd(acc, k.point_col[2][c], att[2]);
            const float lc = madd(acc, k.point_col[3][c], att[3]);
            col[c] = ftoi(vmin(lc, 255.0f), 0);
        }
        col[3] = base_col[3];
        // 3b. a STQ deslocada.
        float stq[3] = {v.stq[0], v.stq[1], v.stq[2]};
        if (l.kind == kBondScroll2) {
            stq[0] = vadd(stq[0], off0[0]);
            stq[1] = vadd(stq[1], off0[1]);
        } else if (l.kind == kBondFrameAnim) {
            stq[1] = vadd(stq[1], off_t);
        }
        // 4. a transformação e a névoa.
        float clip[4];
        transform(b.mvp, v.pos, v.pos[3], clip);
        const float q = vdiv(1.0f, clip[3]);
        const float ww = vmin(vmax(clip[3], k.fog[0]), k.fog[2]);
        const float dw = vsub(ww, k.fog[0]);
        const float f = msub(vmul(1.0f, 255.0f), k.fog[3], dw);
        int32_t xyzf[4];
        for (int c = 0; c < 3; ++c) xyzf[c] = ftoi(madd(vadd(k.vp_offset[c], 0.0f), vmul(clip[c], k.vp_scale[c]), q), 4);
        xyzf[3] = ftoi(f, 4);
        if (adc[size_t(i)]) xyzf[3] = ((xyzf[3] & 0xFFFF) & 0xBFFE) | 0x8000;
        CarVertexOut& o = out[0][size_t(i)];
        for (int c = 0; c < 3; ++c) o.stq[c] = vmul(stq[c], q);
        for (int c = 0; c < 4; ++c) o.rgba[c] = col[c];
        for (int c = 0; c < 4; ++c) o.xyzf[c] = xyzf[c];
        o.adc = adc[size_t(i)];
        if (passes & kBondEnv) {
            CarVertexOut& e = out[1][size_t(i)];
            e = o;
            if (l.kind == kBondScroll2) {
                const float s1[3] = {vadd(v.stq1[0], off1[0]), vadd(v.stq1[1], off1[1]), v.stq1[2]};
                for (int c = 0; c < 3; ++c) e.stq[c] = vmul(s1[c], q);
            } else {
                float st[3];
                bond_env_stq(b.world, b.camera, v.pos, st);
                for (int c = 0; c < 3; ++c) e.stq[c] = vmul(st[c], q);
            }
        }
    }
}

void bond_triangles(const BondBatch& b, std::vector<uint16_t>& tris) {
    CarBatch c;
    c.count = b.count;
    c.strip = b.strip;
    std::memcpy(c.split, b.split, sizeof c.split);
    car_triangles(c, tris);
}

namespace {

void verify_once(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const BondBatch& b,
                 CarVerifyStats& s) {
    ++s.batches;
    const BondMethodLayout& l = *b.layout;
    ps2::VuMachine vu(ps2::VuUnit::VU1);
    vu.load_micro(0, code.data(), uint32_t(code.size() * 8));
    std::memcpy(vu.data(), mem.w, sizeof mem.w);
    vu.set_top(uint16_t(top), uint16_t(top));
    try {
        vu.run(2, 2'000'000);
    } catch (const std::exception& e) {
        ++s.failed_runs;
        if (s.first_error.empty()) s.first_error = e.what();
        return;
    }
    std::vector<CarVertexOut> ref[2];
    bond_reference(b, car_constants_from(mem), ref);
    // Os pacotes de vértices na ordem dos XGKICK: no Reflect o do ambiente e depois o da textura; nos Scroll2 o da
    // textura e depois o do Texture1.
    std::vector<const std::vector<CarVertexOut>*> order;
    if ((l.passes & kBondEnv) && l.kind == kBondReflect) order.push_back(&ref[1]);
    order.push_back(&ref[0]);
    if ((l.passes & kBondEnv) && l.kind == kBondScroll2) order.push_back(&ref[1]);
    size_t next = 0;
    for (const ps2::GifKick& k : vu.kicks()) {
        if (k.address != top + uint32_t(l.gif_tag)) continue;
        if (next >= order.size()) break;
        const std::vector<CarVertexOut>& r = *order[next++];
        if (k.data.size() < 16 + r.size() * 48) {
            ++s.missing_kicks;
            continue;
        }
        for (size_t v = 0; v < r.size(); ++v) {
            uint32_t w[12];
            std::memcpy(w, k.data.data() + 16 + v * 48, 48);
            for (int c = 0; c < 3; ++c) {
                const uint32_t ref_bits = ps2::fbits(r[v].stq[c]);
                const uint32_t d = w[c] > ref_bits ? w[c] - ref_bits : ref_bits - w[c];
                if (d) ++s.stq_diff;
                if (d > s.stq_max_ulps && d < 0x7f000000u) s.stq_max_ulps = d;
                const float ad = std::fabs(fl(w[c]) - r[v].stq[c]);
                if (ad > s.stq_max_abs) s.stq_max_abs = ad;
            }
            for (int c = 0; c < 4; ++c) {
                const int d = std::abs(int32_t(w[4 + c]) - r[v].rgba[c]);
                if (d) ++s.rgba_diff;
                if (d > s.rgba_max) s.rgba_max = d;
            }
            for (int c = 0; c < 3; ++c) {
                const int64_t d = std::llabs(int64_t(int32_t(w[8 + c])) - int64_t(r[v].xyzf[c]));
                if (d) ++s.xyz_diff;
                if (c < 2 && d > s.xyz_max) s.xyz_max = int(std::min<int64_t>(d, 0x7fffffff));
                if (c == 2 && d > s.z_max) s.z_max = int(std::min<int64_t>(d, 0x7fffffff));
            }
            const bool adc_ps2 = (w[11] & 0x8000) != 0, adc_ref = (r[v].xyzf[3] & 0x8000) != 0;
            if (adc_ps2 && !adc_ref) ++s.adc_clip;
            if (adc_ref && !adc_ps2) ++s.adc_extra;
            if (!adc_ps2 && !adc_ref && int32_t(w[11]) != r[v].xyzf[3]) ++s.fog_diff;
            ++s.vertices;
        }
    }
    if (next < order.size()) ++s.missing_kicks;
}

}  // namespace

void bond_verify_batch(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const BondBatch& b,
                       CarVerifyStats& s) {
    verify_once(code, mem, top, b, s);
    // As variantes: na carga o SimStep é o do quadro 0 (o deslocamento das texturas zero) e o LightSources o do
    // RLightManager da pista; os mesmos lotes rodam de novo com o SimStep de um jogo longo (e um SimStep.w no FrameAnim)
    // e com outras luzes no Dynamic.
    const BondMethodLayout& l = *b.layout;
    if (l.kind != kBondDynamic && l.kind != kBondScroll2 && l.kind != kBondFrameAnim) return;
    const int t = int(top);
    auto put = [&](VuMemory& m2, int qw, int c, float v) { std::memcpy(&m2.w[((qw & 1023) << 2) | c], &v, 4); };
    const int variants = l.kind == kBondDynamic ? 1 : 2;
    for (int var = 0; var < variants; ++var) {
        auto m2 = std::make_unique<VuMemory>(mem);
        if (l.kind == kBondDynamic) {
            // Três direções (transpostas: a coluna k é a luz k), as cores e o ambiente.
            const float L[7][4] = {{0.36f, -0.80f, 0.0f, 0}, {-0.72f, -0.48f, 1.0f, 0}, {0.6f, 0.36f, 0.0f, 0},
                                   {0.9f, 0.85f, 0.7f, 0}, {0.30f, 0.32f, 0.45f, 0}, {0.12f, 0.1f, 0.08f, 0},
                                   {0.22f, 0.2f, 0.31f, 0}};
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 4; ++c) put(*m2, t + l.lights + r, c, L[r][c]);
        } else {
            put(*m2, t + l.sim_step, 0, var == 0 ? 12345.0f : 1234567.0f);
            if (l.kind == kBondFrameAnim) put(*m2, t + l.sim_step, 3, var == 0 ? 13.0f : 0.5f);
        }
        BondBatch b2;
        if (!bond_decode_batch(*m2, top, l, b2)) continue;
        ++s.variant_batches;
        verify_once(code, *m2, top, b2, s);
    }
}

}  // namespace drv
