// A família de pele dos personagens (mat_char.h): o leiaute, a referência na CPU e a conferência contra o microprograma.

#include "driving/mat_char.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "platform/crt_compat.h"
#include "platform/ps2_float.h"
#include "platform/vu_interp.h"

namespace drv {

namespace {

// Os leiautes, dos endereços "IADDIU viN, vi14, X" e "LQ/SQ/ILW X(vi14)" de cada microprograma e da ordem dos
// parâmetros (Parameters, ModelMatrix, MyMatrix, WorldMatrix, [ModelViewMatrix], State, [EnvMapState], LightSources,
// PositionalLightSources, CharacterOptions, Texture, [SpecularMap, ReflectionMap2], LightingProperties,
// [ReflectionInfo2, SpecularMat], SplitReg1-3, Coordinates, STQ, Normals; depois as áreas de trabalho).
const CharMethodLayout kLayouts[] = {
    // name, kind, passes, max, params, model, mvp, world, modelview, state, env_state, lights, point_lights, options,
    // tar, tar_spec, tar_refl, props, refl_info, spec_mat, split, pos, stq, normal, e_scratch, colour, gif_tag, out
    {"BondLitTextureAndShadow_Skin", kCharLit, kCharPassBase, 48, 0, 1, 5, 9, 0, 0x0d, 0, 0x15, 0x1c, 0x24, 0x25, 0,
     0, 0x2c, 0, 0, 0x2d, 0x30, 0x60, 0x90, 0, 0xc0, 0xf0, 0xf1},
    {"BondLitTextureAndShadowSpecular_Skin", kCharLitSpec, kCharPassBase | kCharPassSpec | kCharPassRefl, 40, 0, 1, 5,
     9, 0x0d, 0x11, 0x19, 0x21, 0x28, 0x30, 0x31, 0x38, 0x3f, 0x46, 0x47, 0x48, 0x4c, 0x4f, 0x77, 0x9f, 0xc7, 0xcb,
     0xf3, 0xf4},
};

using ps2::fbits;
constexpr uint32_t kOne = 0x3f800000u;
constexpr uint32_t kZero = 0;

// Os registradores de quatro pistas em bits.
struct V4 {
    uint32_t c[4] = {0, 0, 0, 0};
};

struct Mem {
    VuMemory& m;
    uint32_t* q(int qw) { return &m.w[(qw & 1023) << 2]; }
    V4 get(int qw) {
        V4 v;
        std::memcpy(v.c, q(qw), 16);
        return v;
    }
    void put(int qw, const V4& v, unsigned lanes = 15) {  // bit 3 = x ... bit 0 = w (como o campo de destino)
        uint32_t* d = q(qw);
        for (int l = 0; l < 4; ++l)
            if ((lanes >> (3 - l)) & 1) d[l] = v.c[l];
    }
    int lo16(int qw, int c) { return int(int16_t(uint16_t(q(qw)[c] & 0xFFFFu))); }  // ILW: os 16 bits de baixo
    void isw(int qw, int c, int v) { q(qw)[c] = uint32_t(uint16_t(v)); }            // ISW: 16 bits sem sinal
};

// out = a x b0 + c x b1 + d x b2 (+ e x b3): o MULA/MADDA/MADD com difusão, nas pistas pedidas (as outras ficam 0).
V4 mac3(const V4& r0, uint32_t s0, const V4& r1, uint32_t s1, const V4& r2, uint32_t s2, unsigned lanes = 15) {
    V4 o;
    for (int l = 0; l < 4; ++l) {
        if (!((lanes >> (3 - l)) & 1)) continue;
        uint32_t acc = ps2::mul(r0.c[l], s0);
        acc = ps2::madd(acc, r1.c[l], s1);
        o.c[l] = ps2::madd(acc, r2.c[l], s2);
    }
    return o;
}
V4 mac4(const V4& r0, uint32_t s0, const V4& r1, uint32_t s1, const V4& r2, uint32_t s2, const V4& r3, uint32_t s3,
        unsigned lanes = 15) {
    V4 o;
    for (int l = 0; l < 4; ++l) {
        if (!((lanes >> (3 - l)) & 1)) continue;
        uint32_t acc = ps2::mul(r0.c[l], s0);
        acc = ps2::madd(acc, r1.c[l], s1);
        acc = ps2::madd(acc, r2.c[l], s2);
        o.c[l] = ps2::madd(acc, r3.c[l], s3);
    }
    return o;
}
// ERLENG do xyz e MULx.xyzw: a linha vezes 1/|xyz|.
V4 normalize_row(const V4& r) {
    const uint32_t p = ps2::rsqrt(kOne, ps2::add(ps2::add(ps2::mul(r.c[0], r.c[0]), ps2::mul(r.c[1], r.c[1])),
                                                 ps2::mul(r.c[2], r.c[2])));
    V4 o;
    for (int l = 0; l < 4; ++l) o.c[l] = ps2::mul(r.c[l], p);
    return o;
}
V4 scale(const V4& r, uint32_t s) {
    V4 o;
    for (int l = 0; l < 4; ++l) o.c[l] = ps2::mul(r.c[l], s);
    return o;
}

// Os passos 4 (a transformação e a saída da passada base) e os SplitReg, na memória.
void transform_and_output(const CharMethodLayout& l, Mem& M, int T, int count) {
    // A etiqueta GIF: o NLOOP (ILW.x, IOR com a contagem, ISW.x: a palavra x fica só com os 16 bits).
    M.isw(T + l.gif_tag, 0, M.lo16(T + l.gif_tag, 0) | count);
    V4 mvp[4];
    for (int r = 0; r < 4; ++r) mvp[r] = M.get(T + l.mvp + r);
    const V4 vp_off = M.get(0), vp_scale = M.get(1);
    uint32_t flags = 0;  // os indicadores do CLIP, 6 bits por chamada (o mais novo em baixo)
    for (int i = 0; i < count; ++i) {
        const V4 p = M.get(T + l.pos + i);
        const V4 clip = mac4(mvp[0], p.c[0], mvp[1], p.c[1], mvp[2], p.c[2], mvp[3], p.c[3]);
        const uint32_t aw = ps2::abs(clip.c[3]);
        uint32_t f = 0;
        for (int c = 0; c < 3; ++c) {
            if (ps2::less(aw, clip.c[c])) f |= 1u << (c * 2);
            if (ps2::less(clip.c[c], ps2::neg(aw))) f |= 1u << (c * 2 + 1);
        }
        flags = ((flags << 6) | f) & 0xFFFFFFu;
        const uint32_t q = ps2::div(kOne, clip.c[3]);
        V4 xyzf;
        for (int c = 0; c < 3; ++c) {
            const uint32_t sc = ps2::mul(clip.c[c], vp_scale.c[c]);
            xyzf.c[c] = uint32_t(ps2::to_int(ps2::madd(vp_off.c[c], sc, q), 4));
        }
        // SUB.w vf10 (0), ou ADD.w vf10, vf00, vf31 com o vf31.w = vf00.w + 2047 (ADDi.w): 1 + 2048 = 2049, que o
        // FTOI4 leva a 0x8010: o bit 15 (o ADC) liga e o GS não desenha o triângulo que o vértice fecha.
        xyzf.c[3] = (i > 0 && (flags & 0x3FFFFu)) ? uint32_t(ps2::to_int(ps2::add(kOne, ps2::add(kOne, fbits(2047.0f))), 4))
                                                  : 0u;
        V4 stq = M.get(T + l.stq + i);
        for (int c = 0; c < 3; ++c) stq.c[c] = ps2::mul(stq.c[c], q);
        M.put(T + l.out + 3 * i, stq);
        M.put(T + l.out + 3 * i + 1, M.get(T + l.colour + i));
        M.put(T + l.out + 3 * i + 2, xyzf);
    }
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) {
            const int e = M.lo16(T + l.split + r, c);
            M.isw(T + l.out + 2 + e, 3, 0x8000);
            M.isw(T + l.out + 5 + e, 3, 0x8000);
        }
}

void read_pass(const CharMethodLayout& l, Mem& M, int T, int count, std::vector<CarVertexOut>& out) {
    out.assign(size_t(count), CarVertexOut{});
    for (int i = 0; i < count; ++i) {
        CarVertexOut& v = out[size_t(i)];
        const uint32_t* s = M.q(T + l.out + 3 * i);
        for (int c = 0; c < 3; ++c) std::memcpy(&v.stq[c], &s[c], 4);
        for (int c = 0; c < 4; ++c) v.rgba[c] = int32_t(M.q(T + l.out + 3 * i + 1)[c]);
        for (int c = 0; c < 4; ++c) v.xyzf[c] = int32_t(M.q(T + l.out + 3 * i + 2)[c]);
        v.adc = (v.xyzf[3] & 0x8000) != 0;
    }
}

// A cópia do STQ e da cor para a saída (o laço das passadas 2 e 3), sem tocar no XYZF.
void copy_stq_colour(const CharMethodLayout& l, Mem& M, int T, int count) {
    for (int i = 0; i < count; ++i) {
        M.put(T + l.out + 3 * i, M.get(T + l.stq + i));
        M.put(T + l.out + 3 * i + 1, M.get(T + l.colour + i));
    }
}

}  // namespace

const CharMethodLayout* char_method_layout(const char* name) {
    if (!name) return nullptr;
    for (const CharMethodLayout& l : kLayouts)
        if (std::strcmp(l.name, name) == 0) return &l;
    return nullptr;
}

// [driving] BondLitTextureAndShadow_Skin e BondLitTextureAndShadowSpecular_Skin (bondrm.o, os microprogramas da VU1):
// os passos de mat_char.h na ordem do código, na memória e na aritmética da VU.
void char_reference(const CharMethodLayout& l, const VuMemory& mem, uint32_t top, std::vector<CarVertexOut> out[kCharMaxPasses],
                    int* guards) {
    auto copy = std::make_unique<VuMemory>(mem);
    Mem M{*copy};
    const int T = int(top);
    const int B = T == l.top1 ? l.top1 : l.top0;  // a metade do código que roda (os endereços absolutos dela)
    int g = 0;
    for (int p = 0; p < kCharMaxPasses; ++p) out[p].clear();

    // 0x03-0x0a: a etiqueta GIF dos vértices (EOP, NREG 3: ST, RGBAQ, XYZF2).
    M.put(T + l.gif_tag, V4{{0x3f808000u, 0x30000000u, 0x3f800412u, 0u}});

    // Passo 1: a paleta misturada.
    if (M.lo16(l.weights, 3) == 0) {
        const int n_mat = M.lo16(T + l.params, 2);
        const int n_w = M.lo16(T + l.params, 3);
        if (n_mat <= 0 || n_w <= 0) {
            ++g;  // os do ... while com contagem zero rodariam 65.536 vezes
        } else {
            for (int k = 0; k < n_mat; ++k)
                for (int r = 0; r < 4; ++r) M.put(B + l.out + 4 * k + r, M.get(l.palette + 4 * k + r));
            const uint32_t i768 = fbits(768.0f);
            for (int i = 0; i < n_w; ++i) {
                const V4 e = M.get(l.weights + i);
                const int b0 = M.lo16(l.weights + i, 0) & 0xFF, b1 = M.lo16(l.weights + i, 1) & 0xFF,
                          b2 = M.lo16(l.weights + i, 2) & 0xFF;
                uint32_t w[3];
                for (int c = 0; c < 3; ++c) w[c] = ps2::sub(ps2::add(e.c[c], i768), i768);
                for (int r = 0; r < 4; ++r) {
                    const V4 m = mac3(M.get(B + l.out + b0 + r), w[0], M.get(B + l.out + b1 + r), w[1],
                                      M.get(B + l.out + b2 + r), w[2]);
                    M.put(l.palette + 7 * i + r, m);
                }
            }
            M.isw(l.weights, 3, n_w);
        }
    }

    // Passo 2: os vértices únicos pela matriz e espalhados nos lugares de saída; depois de volta às áreas de entrada.
    const int count = M.lo16(T + l.params, 0);
    const int unique = M.lo16(T + l.params, 1);
    if (unique <= 0 || count <= 0) {
        ++g;
    } else {
        V4 mat[4];  // os vf15-vf18 do vértice anterior (zero no começo do interpretador)
        for (int j = 0; j < unique; ++j) {
            const V4 p = M.get(B + 0x30 + (l.pos - 0x30) + j);
            V4 n = M.get(B + l.normal + j);
            n.c[3] = 0;  // o vf05.w zerado antes do laço (o LQ.xyz não o toca)
            const V4 s = M.get(B + l.stq + j);
            const int idx = int(int16_t(uint16_t(p.c[3] & 0xFFFFu)));
            const int slot0 = int(int16_t(uint16_t(s.c[2] & 0xFFFFu)));
            const int slot1 = int(int16_t(uint16_t(s.c[3] & 0xFFFFu)));
            if (idx >= 0)
                for (int r = 0; r < 4; ++r) mat[r] = M.get(l.palette + idx + r);
            V4 nn = mac3(mat[0], n.c[0], mat[1], n.c[1], mat[2], n.c[2], 0xE);
            nn.c[3] = 0;
            const V4 pp = mac4(mat[0], p.c[0], mat[1], p.c[1], mat[2], p.c[2], mat[3], kOne);
            V4 st;
            st.c[0] = ps2::from_int(int32_t(s.c[0]), 12);
            st.c[1] = ps2::from_int(int32_t(s.c[1]), 12);
            st.c[2] = kOne;
            st.c[3] = 0;
            M.put(B + l.out + 1 + slot0, nn);
            M.put(B + l.out + 1 + slot1, nn);
            M.put(B + l.out + 2 + slot0, pp);
            M.put(B + l.out + 2 + slot1, pp);
            M.put(B + l.out + slot0, st);
            M.put(B + l.out + slot1, st);
        }
        for (int k = 0; k < count; ++k) {
            const V4 st = M.get(B + l.out + 3 * k), n = M.get(B + l.out + 3 * k + 1), p = M.get(B + l.out + 3 * k + 2);
            M.put(B + l.pos + k, p);
            M.put(B + l.normal + k, n);
            M.put(B + l.stq + k, st);
        }
    }
    if (count <= 0) {
        if (guards) *guards += g;
        return;
    }

    // Passo 3: a cor.
    const V4 opt = M.get(T + l.options);
    const bool shadow = M.lo16(T + l.options, 2) != 0;
    {
        V4 c;
        const uint32_t base = shadow ? 0u : 0x7fu;
        c.c[0] = c.c[1] = c.c[2] = base;
        c.c[3] = opt.c[3];
        for (int k = 0; k < count; ++k) M.put(T + l.colour + k, c);
    }
    if (!shadow) {
        const V4 props = M.get(T + l.props);
        V4 L[7];
        for (int r = 0; r < 7; ++r) L[r] = M.get(T + l.lights + r);
        V4 rn[3];
        for (int i = 0; i < 3; ++i) rn[i] = normalize_row(M.get(T + l.world + i));
        V4 c0 = L[3], amb = L[6];
        for (int c = 0; c < 3; ++c) {
            c0.c[c] = ps2::mul(L[3].c[c], props.c[1]);
            amb.c[c] = ps2::mul(L[6].c[c], props.c[0]);
        }
        V4 A[3];
        for (int i = 0; i < 3; ++i) A[i] = mac3(L[0], rn[i].c[0], L[1], rn[i].c[1], L[2], rn[i].c[2], 0xE);
        const uint32_t k255 = fbits(255.0f);
        for (int k = 0; k < count; ++k) {
            const V4 n = M.get(T + l.normal + k);
            V4 d = mac3(A[0], n.c[0], A[1], n.c[1], A[2], n.c[2], 0xE);
            for (int c = 0; c < 3; ++c) d.c[c] = ps2::max(d.c[c], kZero);
            const V4 col = M.get(T + l.colour + k);
            uint32_t cf[4];
            for (int c = 0; c < 4; ++c) cf[c] = ps2::from_int(int32_t(col.c[c]));
            V4 o;
            for (int c = 0; c < 3; ++c) {
                uint32_t acc = ps2::mul(c0.c[c], d.c[0]);
                acc = ps2::madd(acc, L[4].c[c], d.c[1]);
                acc = ps2::madd(acc, L[5].c[c], d.c[2]);
                const uint32_t lt = ps2::madd(acc, amb.c[c], kOne);
                o.c[c] = uint32_t(ps2::to_int(ps2::min(ps2::mul(lt, cf[c]), k255)));
            }
            o.c[3] = uint32_t(ps2::to_int(cf[3]));
            M.put(T + l.colour + k, o);
        }
        // As luzes pontuais.
        V4 P[8];
        for (int r = 0; r < 8; ++r) P[r] = M.get(T + l.point_lights + r);
        const V4 nk = scale(P[3], fbits(-1.0f));
        V4 W[4];
        for (int r = 0; r < 4; ++r) W[r] = M.get(T + l.world + r);
        for (int k = 0; k < count; ++k) {
            const V4 p = M.get(T + l.pos + k);
            const V4 pw = mac4(W[0], p.c[0], W[1], p.c[1], W[2], p.c[2], W[3], kOne);
            uint32_t att[4];
            for (int L4 = 0; L4 < 4; ++L4) {
                const uint32_t dx = ps2::sub(P[0].c[L4], pw.c[0]), dy = ps2::sub(P[1].c[L4], pw.c[1]),
                               dz = ps2::sub(P[2].c[L4], pw.c[2]);
                uint32_t d2 = ps2::mul(dx, dx);
                d2 = ps2::madd(d2, dy, dy);
                d2 = ps2::madd(d2, dz, dz);
                att[L4] = ps2::max(ps2::madd(ps2::mul(d2, nk.c[L4]), k255, kOne), kZero);
            }
            V4 col = M.get(T + l.colour + k);
            for (int c = 0; c < 3; ++c) {
                uint32_t acc = ps2::mul(ps2::from_int(int32_t(col.c[c])), kOne);
                acc = ps2::madd(acc, P[4].c[c], att[0]);
                acc = ps2::madd(acc, P[5].c[c], att[1]);
                acc = ps2::madd(acc, P[6].c[c], att[2]);
                const uint32_t v = ps2::min(ps2::madd(acc, P[7].c[c], att[3]), k255);
                col.c[c] = uint32_t(ps2::to_int(v));
            }
            M.put(T + l.colour + k, col, 0xE);
        }
    }

    // Passo 4: a transformação e a passada base.
    transform_and_output(l, M, T, count);
    read_pass(l, M, T, count, out[0]);
    if (shadow || l.kind != kCharLitSpec) {
        if (guards) *guards += g;
        return;
    }

    // Passo 5: o especular e o reflexo.
    const V4 props = M.get(T + l.props), refl = M.get(T + l.refl_info);
    {
        V4 W[4], S[4];
        for (int r = 0; r < 4; ++r) W[r] = M.get(T + l.world + r);
        for (int r = 0; r < 4; ++r) S[r] = M.get(T + l.spec_mat + r);
        for (int r = 0; r < 4; ++r)
            M.put(T + l.e_scratch + r, mac4(S[0], W[r].c[0], S[1], W[r].c[1], S[2], W[r].c[2], S[3], W[r].c[3]));
        V4 E[3];
        for (int r = 0; r < 3; ++r) E[r] = scale(normalize_row(M.get(T + l.e_scratch + r)), refl.c[2]);
        V4 col;
        for (int c = 0; c < 4; ++c) col.c[c] = ps2::mul(fbits(128.0f), props.c[2]);
        for (int c = 0; c < 3; ++c) col.c[c] = uint32_t(ps2::to_int(col.c[c]));
        const uint32_t half = fbits(0.5f);
        for (int k = 0; k < count; ++k) {
            const V4 n = M.get(T + l.normal + k);
            const V4 a = mac3(E[0], n.c[0], E[1], n.c[1], E[2], n.c[2]);
            V4 st;
            st.c[0] = ps2::madd(a.c[0], half, kOne);
            st.c[1] = ps2::madd(a.c[1], half, kOne);
            st.c[2] = kOne;
            st.c[3] = kOne;
            M.put(T + l.stq + k, st);
            M.put(T + l.colour + k, col, 0xE);
        }
        copy_stq_colour(l, M, T, count);
        read_pass(l, M, T, count, out[1]);
    }
    {
        V4 Rw[3];
        for (int r = 0; r < 3; ++r) Rw[r] = scale(normalize_row(M.get(T + l.world + r)), refl.c[1]);
        uint32_t c128 = ps2::mul(fbits(128.0f), props.c[3]);
        const uint32_t half = fbits(0.5f), big = fbits(100000.0f);
        for (int k = 0; k < count; ++k) {
            const V4 n = M.get(T + l.normal + k);
            const V4 a = mac3(Rw[0], n.c[0], Rw[1], n.c[1], Rw[2], n.c[2]);
            // vf19 = (0,5, 0, 0,5, 0): vf04.yz = a.yz + vf19.yz, vf03.x = a.x + 0,5, vf03.y = vf04.z.
            const uint32_t gy = ps2::mul(ps2::max(ps2::madd(a.c[1], kZero, kOne), kZero), big);
            V4 st;
            st.c[0] = ps2::madd(a.c[0], half, kOne);
            st.c[1] = ps2::add(kZero, ps2::madd(a.c[2], half, kOne));
            st.c[2] = kOne;
            st.c[3] = kOne;
            M.put(T + l.stq + k, st);
            V4 col;
            for (int c = 0; c < 3; ++c) col.c[c] = uint32_t(ps2::to_int(ps2::min(c128, gy)));
            M.put(T + l.colour + k, col, 0xE);
        }
        copy_stq_colour(l, M, T, count);
        read_pass(l, M, T, count, out[2]);
    }
    if (guards) *guards += g;
}

namespace {

uint32_t ulps(float a, float b) {
    int32_t ia, ib;
    std::memcpy(&ia, &a, 4);
    std::memcpy(&ib, &b, 4);
    if (ia < 0) ia = int32_t(0x80000000u) - ia;
    if (ib < 0) ib = int32_t(0x80000000u) - ib;
    const int64_t d = int64_t(ia) - int64_t(ib);
    return uint32_t(d < 0 ? -d : d);
}

bool verify_debug() {
    static const bool on = nf::get_env_narrow("NF_VERIFY_DEBUG", nullptr);
    return on;
}

void verify_once(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const CharMethodLayout& l,
                 CarVerifyStats& s) {
    ++s.batches;
    ps2::VuMachine vu(ps2::VuUnit::VU1);
    vu.load_micro(0, code.data(), uint32_t(code.size() * 8));
    std::memcpy(vu.data(), mem.w, sizeof mem.w);
    vu.set_top(uint16_t(top), uint16_t(top));
    try {
        vu.run(2, 4'000'000);
    } catch (const std::exception& e) {
        ++s.failed_runs;
        if (s.first_error.empty()) s.first_error = e.what();
        return;
    }
    std::vector<CarVertexOut> ref[kCharMaxPasses];
    char_reference(l, mem, top, ref);
    int pass = 0;
    for (const ps2::GifKick& k : vu.kicks()) {
        if (k.address != top + uint32_t(l.gif_tag)) continue;
        while (pass < kCharMaxPasses && ref[pass].empty()) ++pass;
        if (pass >= kCharMaxPasses) break;
        const std::vector<CarVertexOut>& r = ref[pass];
        if (k.data.size() < 16 + r.size() * 48) {
            ++s.missing_kicks;
            ++pass;
            continue;
        }
        for (size_t v = 0; v < r.size(); ++v) {
            uint32_t w[12];
            std::memcpy(w, k.data.data() + 16 + v * 48, 48);
            for (int c = 0; c < 3; ++c) {
                float f;
                std::memcpy(&f, &w[c], 4);
                const uint32_t u = ulps(f, r[v].stq[c]);
                const float ad = std::fabs(f - r[v].stq[c]);
                if (u > 4 && ad > 1.0f / 65536.0f) {
                    ++s.stq_diff;
                    if (verify_debug())
                        std::printf("DBG stq %s pass %d v %zu c %d ps2 %08x ref %08x\n", l.name, pass, v, c, w[c],
                                    fbits(r[v].stq[c]));
                }
                if (u > s.stq_max_ulps && u < 0x7f000000u) s.stq_max_ulps = u;
                if (ad > s.stq_max_abs) s.stq_max_abs = ad;
            }
            for (int c = 0; c < 4; ++c) {
                const int d = std::abs(int32_t(w[4 + c]) - r[v].rgba[c]);
                if (d) ++s.rgba_diff;
                if (d > s.rgba_max) s.rgba_max = d;
                if (d && verify_debug())
                    std::printf("DBG cor %s pass %d v %zu c %d ps2 %08x ref %08x\n", l.name, pass, v, c, w[4 + c],
                                uint32_t(r[v].rgba[c]));
            }
            for (int c = 0; c < 4; ++c) {
                const int64_t d = std::llabs(int64_t(int32_t(w[8 + c])) - int64_t(r[v].xyzf[c]));
                if (c < 3) {
                    const int64_t tol = c < 2 ? 1 : std::max<int64_t>(1, std::llabs(int64_t(r[v].xyzf[c])) >> 21);
                    if (d > tol) ++s.xyz_diff;
                    if (d > tol && verify_debug())
                        std::printf("DBG xyz %s pass %d v %zu c %d ps2 %08x ref %08x\n", l.name, pass, v, c, w[8 + c],
                                    uint32_t(r[v].xyzf[c]));
                    if (c < 2 && d > s.xyz_max) s.xyz_max = int(d);
                    if (c == 2 && d > s.z_max) s.z_max = int(std::min<int64_t>(d, 0x7fffffff));
                } else if (d) {
                    // O w: o ADC dos SplitReg e o 2047 do recorte, iguais nos dois lados (aqui o recorte é reproduzido).
                    ++s.fog_diff;
                    if (verify_debug())
                        std::printf("DBG w %s pass %d v %zu ps2 %08x ref %08x\n", l.name, pass, v, w[11],
                                    uint32_t(r[v].xyzf[3]));
                }
            }
            const bool adc_ps2 = (w[11] & 0x8000) != 0, adc_ref = (r[v].xyzf[3] & 0x8000) != 0;
            if (adc_ps2 && !adc_ref) ++s.adc_clip;
            if (adc_ref && !adc_ps2) ++s.adc_extra;
            ++s.vertices;
        }
        ++pass;
    }
    while (pass < kCharMaxPasses && ref[pass].empty()) ++pass;
    if (pass < kCharMaxPasses) ++s.missing_kicks;
}

}  // namespace

void char_verify_batch(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const CharMethodLayout& l,
                       CarVerifyStats& s) {
    verify_once(code, mem, top, l, s);
    // Na carga as matrizes são identidades e as luzes pontuais apagadas: o lote roda de novo com uma câmera em
    // perspectiva que corta parte do modelo, uma WorldMatrix girada (as normais e a luz), a SpecularMat girada, as
    // quatro luzes pontuais acesas perto do personagem, e outra vez com a sombra (CharacterOptions.z = 1).
    auto m2 = std::make_unique<VuMemory>(mem);
    auto put = [&](int qw, int c, float v) { std::memcpy(&m2->w[((qw & 1023) << 2) | c], &v, 4); };
    const int T = int(top);
    static const float kMvp[16] = {1.4f, 0.0f, 0.1f, 0.0f, 0.0f, 1.8f, 0.3f, 0.0f, 0.2f, -0.1f, 1.0f, 1.0f,
                                   -0.3f, -0.6f, 2.2f, 2.5f};
    static const float kWorld[16] = {0.8f, 0.0f, -0.6f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.6f, 0.0f, 0.8f, 0.0f,
                                     1.5f, 0.2f, -3.0f, 1.0f};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            put(T + l.mvp + r, c, kMvp[r * 4 + c] * 300.0f);
            put(T + l.world + r, c, kWorld[r * 4 + c]);
            if (l.spec_mat) put(T + l.spec_mat + r, c, kWorld[(3 - r) * 4 + c] * 0.7f + (r == c ? 0.2f : 0.0f));
        }
    for (int L = 0; L < 4; ++L) {
        put(T + l.point_lights + 0, L, 1.0f - 0.5f * float(L));
        put(T + l.point_lights + 1, L, 0.3f * float(L));
        put(T + l.point_lights + 2, L, -3.0f + 0.4f * float(L));
        put(T + l.point_lights + 3, L, 15.0f + 20.0f * float(L));
        for (int c = 0; c < 3; ++c) put(T + l.point_lights + 4 + L, c, 0.1f * float(1 + ((L + c) % 3)));
    }
    ++s.variant_batches;
    verify_once(code, *m2, top, l, s);
    m2->w[((T + l.options) & 1023) << 2 | 2] = 1;  // a sombra
    m2->w[((T + l.options) & 1023) << 2 | 3] = 19;
    ++s.variant_batches;
    verify_once(code, *m2, top, l, s);
}

bool char_decode_batch(const VuMemory& mem, uint32_t top, const CharMethodLayout& l, const std::vector<int32_t>& bones,
                       CharBatch& b) {
    const int T = int(top);
    const int B = T == l.top1 ? l.top1 : l.top0;
    b = CharBatch();
    b.layout = &l;
    const auto lo16 = [&](int qw, int c) { return int(int16_t(uint16_t(mem.u(qw, c) & 0xFFFFu))); };
    b.count = lo16(T + l.params, 0);
    const int unique = lo16(T + l.params, 1);
    const int n_w = lo16(T + l.params, 3);
    if (b.count <= 0 || b.count > l.max_verts || unique <= 0 || unique > l.max_verts) return false;
    for (int c = 0; c < 4; ++c) {
        b.props[c] = mem.f(T + l.props, c);
        b.refl_info[c] = l.refl_info ? mem.f(T + l.refl_info, c) : 0.0f;
        b.options[c] = mem.u(T + l.options, c);
    }
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 4; ++c) b.lights[r][c] = mem.f(T + l.lights + r, c);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 4; ++c) b.point_lights[r][c] = mem.f(T + l.point_lights + r, c);
    auto packet = [&](int at, int qw, eagl::GsRegs& regs) {
        uint32_t buf[16 * 4];
        for (int i = 0; i < qw * 4; ++i) buf[i] = mem.u(T + at + i / 4, i % 4);
        eagl::gs_read_packet(reinterpret_cast<const uint8_t*>(buf), qw, regs);
    };
    packet(l.state, 8, b.pass[0]);
    packet(l.tar, 7, b.pass[0]);
    if (l.passes & kCharPassSpec) {
        packet(l.env_state, 8, b.pass[1]);
        packet(l.tar_spec, 7, b.pass[1]);
        packet(l.env_state, 8, b.pass[2]);
        packet(l.tar_refl, 7, b.pass[2]);
    }
    b.strip = (mem.u(T + l.state + 1, 0) & 7) != 3;
    for (int i = 0; i < 12; ++i) b.split[i] = int(mem.u(T + l.split + i / 4, i % 4) & 0xFFFF);
    // A tabela dos pesos (os ossos e os pesos limpos como no passo 1; o w do qword 4 é zero no envio).
    struct Entry {
        int32_t bone[3];
        float w[3];
    };
    std::vector<Entry> entries(size_t(std::max(n_w, 0)));
    for (int i = 0; i < n_w; ++i) {
        Entry& e = entries[size_t(i)];
        for (int c = 0; c < 3; ++c) {
            const int byte = int(mem.u(l.weights + i, c) & 0xFF);
            const int slot = byte / 4;
            e.bone[c] = (byte % 4 == 0 && slot < int(bones.size())) ? bones[size_t(slot)] : -1;
            if (e.bone[c] < 0) ++b.guards;
            e.w[c] = ps2::ffloat(ps2::sub(ps2::add(mem.u(l.weights + i, c), fbits(768.0f)), fbits(768.0f)));
        }
    }
    b.v.assign(size_t(b.count), CharVertex{});
    std::vector<bool> filled(size_t(b.count), false);
    int cur = -1;
    for (int j = 0; j < unique; ++j) {
        const int idx = int(int16_t(uint16_t(mem.u(B + l.pos + j, 3) & 0xFFFFu)));
        if (idx >= 0) cur = (idx % 7 == 0 && idx / 7 < n_w) ? idx / 7 : -2;
        CharVertex v{};
        for (int c = 0; c < 3; ++c) v.pos[c] = mem.f(B + l.pos + j, c);
        v.pos[3] = 1.0f;
        for (int c = 0; c < 3; ++c) v.normal[c] = mem.f(B + l.normal + j, c);
        for (int c = 0; c < 2; ++c) v.st[c] = ps2::ffloat(ps2::from_int(mem.i(B + l.stq + j, c), 12));
        if (cur >= 0) {
            for (int c = 0; c < 3; ++c) {
                v.bone[c] = entries[size_t(cur)].bone[c];
                v.weight[c] = entries[size_t(cur)].w[c];
            }
        } else {
            ++b.guards;
            for (int c = 0; c < 3; ++c) v.bone[c] = -1;
        }
        for (int s = 2; s < 4; ++s) {
            const int slot = int(int16_t(uint16_t(mem.u(B + l.stq + j, s) & 0xFFFFu)));
            if (slot >= 0 && slot % 3 == 0 && slot / 3 < b.count) {
                b.v[size_t(slot / 3)] = v;
                filled[size_t(slot / 3)] = true;
            } else {
                ++b.guards;
            }
        }
    }
    for (bool f : filled)
        if (!f) ++b.guards;
    if (nf::get_env_narrow("NF_DRV_CHAR_DUMP", nullptr)) {
        std::printf("DUMP lote %s top %x count %d unique %d mats %d pesos %d ossos %zu guardas %d\n", l.name, top, b.count,
                    unique, lo16(T + l.params, 2), n_w, bones.size(), b.guards);
        for (int i = 0; i < n_w; ++i)
            std::printf("  peso %d: %08x %08x %08x %08x\n", i, mem.u(l.weights + i, 0), mem.u(l.weights + i, 1),
                        mem.u(l.weights + i, 2), mem.u(l.weights + i, 3));
        for (int j = 0; j < unique; ++j)
            std::printf("  v %d: w %08x stq %08x %08x %08x %08x\n", j, mem.u(B + l.pos + j, 3), mem.u(B + l.stq + j, 0),
                        mem.u(B + l.stq + j, 1), mem.u(B + l.stq + j, 2), mem.u(B + l.stq + j, 3));
    }
    return true;
}

void char_triangles(const CharBatch& b, std::vector<uint16_t>& tris) {
    const int n = b.count;
    std::vector<bool> adc(size_t(n) + 2, false);
    for (int s : b.split) {
        const int v = s / 3;
        if (v < n) adc[size_t(v)] = true;
        if (v + 1 < n) adc[size_t(v) + 1] = true;
    }
    if (b.strip) {
        for (int v = 2; v < n; ++v)
            if (!adc[size_t(v)]) {
                tris.push_back(uint16_t(v - 2));
                tris.push_back(uint16_t(v - 1));
                tris.push_back(uint16_t(v));
            }
    } else {
        for (int v = 2; v < n; v += 3)
            if (!adc[size_t(v)]) {
                tris.push_back(uint16_t(v - 2));
                tris.push_back(uint16_t(v - 1));
                tris.push_back(uint16_t(v));
            }
    }
}

}  // namespace drv
