// Os destroços (driving/fx/debris.h).

#include "driving/fx/debris.h"

#include <cstring>

#include "driving/ai/sim_random.h"
#include "driving/eagl_state.h"
#include "driving/fx/gfx_gallery.h"
#include "driving/fx/glare.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace {

using namespace ps2::vu;
namespace ee = ps2::ee;
constexpr uint32_t kOne = 0x3f800000u;

DebrisStats g_stats;

float u32_to_float(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float h = ee::cvt_s(int32_t(v >> 1 | (v & 1)));
    return ee::add(h, h);
}
uint32_t len2(const Reg v) {
    Reg t{}, acc{};
    const Reg one = {kOne, 0, 0, 0};
    mul(t, v, v, XYZ);
    mulabc(acc, v, v, BX, X);
    maddabc(acc, one, t, BY, X);
    maddbc(t, acc, one, t, BZ, X);
    return t[0];
}
// A troca e a metade das cores do GS (o R e o B trocados, cada canal / 2).
uint32_t repack(uint32_t c) {
    return (c & 0xfeu) << 15 | (c & 0xfe00u) >> 1 | ((c >> 17) & 0x7fu) | (c & 0xfe000000u) >> 1;
}

}  // namespace

RDebris& debris() {
    static RDebris r;
    return r;
}
DebrisStats& debris_stats() { return g_stats; }

bool debris_load_geometry(const std::function<bool(uint32_t addr, void* out, uint32_t n)>& read,
                          std::vector<DebrisGeometry>& out) {
    out.assign(kDebrisTypes, DebrisGeometry());
    for (int i = 0; i < kDebrisTypes; ++i) {
        uint32_t e[5];
        if (!read(0x338e28u + uint32_t(i) * 0x14u, e, 20)) return false;
        DebrisGeometry& g = out[size_t(i)];
        g.count = int32_t(e[3]);
        g.tag = e[4];
        if (g.count <= 0 || g.count > 0x1e) return false;
        g.coords.resize(size_t(g.count) * 4);
        g.uv.resize(size_t(g.count) * 2);
        if (!read(e[0], g.coords.data(), uint32_t(g.count) * 16) || !read(e[1], g.uv.data(), uint32_t(g.count) * 8) ||
            !read(e[2], &g.split, 4))
            return false;
    }
    float base[26][4], scale = 0;
    if (!read(0x334a10, base, sizeof base) || !read(0x334bb0, &scale, 4)) return false;
    RDebris_PrepareGeometry(out, base, scale);
    return true;
}

// [driving] 0x001d93e8
void RDebris_PrepareGeometry(std::vector<DebrisGeometry>& g, const float shell_base[26][4], float shell_scale) {
    if (g.size() < size_t(kDebrisTypes)) return;
    std::vector<float>& t4 = g[4].coords;
    if (t4.size() >= 26 * 4)
        for (int i = 0; i < 26; ++i)
            for (int k = 0; k < 3; ++k) t4[size_t(i) * 4 + size_t(k)] = ee::mul(shell_base[i][k], shell_scale);
    // O tipo 22 a partir do 16 (a contagem é a do 22, lida da tabela: +0x1c4).
    std::vector<float>& t22 = g[22].coords;
    const std::vector<float>& t16 = g[16].coords;
    for (int i = 0; i < g[22].count && size_t(i) * 4 + 3 < t16.size(); ++i)
        for (int k = 0; k < 3; ++k) t22[size_t(i) * 4 + size_t(k)] = ee::mul(t16[size_t(i) * 4 + size_t(k)], 2.5f);
}

// [driving] 0x001d9628 RDebrisType::RDebrisType
void RDebrisType_Construct(RDebrisType& t) {
    t.d.count = 0;
    // EAGL::GeoPrimState::GeoPrimState (0x00283eb0 -> 0x00283568), o DynamicModel e a camada do GeoPrim (o port: os
    // campos do tipo)
    eagl::GeoPrimState_Init(t.state);
}

// [driving] 0x001d9720 RDebrisType::Init
void RDebrisType_Init(RDebrisType& t, int type) {
    t.index = type;
    // RTextureContext::FindOrCreateTexture(contexto 0, a etiqueta do tipo): o port guarda o tipo (DebrisGeometry::tag)
    t.texture_flags &= 0xf9ffffffu;
    eagl::GeoPrimState_SetPrimType(t.state, 5);  // a faixa de triângulos
    uint64_t prim;
    std::memcpy(&prim, t.state + 0x10, 8);
    prim = (prim & 0xffffffffffffffa7ull) | 0x58;
    const uint64_t alpha = 0x8000000044ull;
    std::memcpy(t.state + 0x40, &alpha, 8);
    prim = (prim & 0xffffffffffffffbfull) | 0x40;
    std::memcpy(t.state + 0x10, &prim, 8);
    // (a camada: Texture, ControlData = +0xd00, Coordinates e UV do modelo, State = +0xd20, SimTick = o fTimeData,
    // SplitReg1, Offsets = +0, Quats = +0x800, InstCols = +0xc00, Velocity = +0x400)
}

// [driving] 0x001d9bb8 RDebrisType::Add
void RDebrisType_Add(RDebrisType& t, float sim_w, const float pos[4], const float vel[4], uint32_t colour, uint32_t time) {
    RDebrisTypeData& d = t.d;
    if (d.count >= uint32_t(kDebrisMax)) {
        ++g_stats.full;
        return;
    }
    const float spawn = u32_to_float(time);
    const uint32_t n = d.count;
    std::memcpy(d.offsets[n], pos, 16);
    d.offsets[n][3] = 1.0f;
    d.life = 0x46;
    d.sim_w = sim_w;
    std::memcpy(d.velocity[n], vel, 16);
    d.velocity[n][3] = spawn;
    // O eixo e a velocidade do giro (o RRandom do render, 0x333e60/0x333e64): (2r / 65536 - 1) nos três, r / 65536 x
    // 0,0897598 no w.
    RRandom& rr = render_random();
    const float k = ps2::ffloat(0x37800000u);  // 2^-16
    float* q = d.quats[n];
    for (int c = 0; c < 3; ++c) {
        const float r = ee::cvt_s(int32_t(RRandom_Generate(rr)));
        q[c] = ee::sub(ee::mul(ee::add(r, r), k), 1.0f);
    }
    q[3] = ee::mul(ee::mul(ee::cvt_s(int32_t(RRandom_Generate(rr))), k), ps2::ffloat(0x3db7d3fbu));
    Reg v;
    load(v, q);
    muls(v, v, ps2::rsqrt(kOne, len2(v)), XYZ);
    store(q, v);
    d.colours[n] = repack(colour);
    d.count = n + 1;
    ++g_stats.added;
}

// [driving] 0x001d9920 RDebrisType::Draw
void RDebrisType_Draw(RDebrisType& t, const float time_data[4], std::vector<DebrisDraw>* out) {
    RDebrisTypeData& d = t.d;
    if (d.count == 0) return;
    const float limit = ee::sub(time_data[0], u32_to_float(d.life));
    for (uint32_t i = 0; i < d.count; ++i) {
        if (ee::lt(d.velocity[i][3], limit)) {
            const uint32_t last = --d.count;
            std::memcpy(d.offsets[i], d.offsets[last], 16);
            std::memcpy(d.velocity[i], d.velocity[last], 16);
            std::memcpy(d.quats[i], d.quats[last], 16);
            d.colours[i] = d.colours[last];
            --i;
            ++g_stats.expired;
            continue;
        }
        // O alfa vezes (agora - 70) / o nascimento (a conta do original), arredondado para o mais perto (0,5 com o sinal).
        const float a = ee::cvt_s(int32_t(d.colours[i] >> 24));
        const float f = ee::mul(a, ee::div(limit, d.velocity[i][3]));
        const float half = ee::lt(f, 0.0f) ? -0.5f : 0.5f;
        d.colours[i] = (d.colours[i] & 0xffffffu) | uint32_t(ee::cvt_w(ee::add(half, f))) << 24;
    }
    if (d.count == 0) return;
    // A camada (+0x5c, +0x64, +0x54, +0x4c: as contagens), o Parameters (x = os vértices do modelo), o ControlData
    // (+0xd04 = os pedaços), o w do fTimeData = +0xd10, e o EAGL::DynamicModel::Draw.
    d.control[1] = d.count;
    ++g_stats.drawn;
    g_stats.pieces += d.count;
    if (!out) return;
    DebrisDraw dr;
    dr.type = t.index;
    dr.count = int(d.count);
    std::memcpy(dr.simtick, time_data, 16);
    dr.simtick[3] = d.sim_w;
    dr.offsets.assign(&d.offsets[0][0], &d.offsets[0][0] + d.count * 4);
    dr.velocity.assign(&d.velocity[0][0], &d.velocity[0][0] + d.count * 4);
    dr.quats.assign(&d.quats[0][0], &d.quats[0][0] + d.count * 4);
    dr.colours.assign(d.colours, d.colours + d.count);
    std::memcpy(&dr.alpha, t.state + 0x40, 8);
    std::memcpy(&dr.test, t.state + 0x30, 8);
    out->push_back(std::move(dr));
}

// [driving] 0x001d94a8 RDebris::RDebris
void RDebris_Construct(RDebris& r) {
    r.unknown5c = 0;
    for (int i = 0; i < kDebrisTypes; ++i) {
        r.types[i] = RDebrisType();
        RDebrisType_Construct(r.types[i]);
        RDebrisType_Init(r.types[i], i);
    }
    // (o 0x001d93e8: RDebris_PrepareGeometry, nos modelos lidos do ELF)
}

// [driving] 0x001d9560 RDebris::Draw
void RDebris_Draw(RDebris& r, const float time_data[4], std::vector<DebrisDraw>* out) {
    for (int i = 0; i < kDebrisTypes; ++i)
        if (i != 4) RDebrisType_Draw(r.types[i], time_data, out);
}

// [driving] 0x001d95c0 RDebris::Reset
void RDebris_Reset(RDebris& r) {
    r.unknown5c = 0;
    for (int i = 0; i < kDebrisTypes; ++i) r.types[i].d.count = 0;
}

// [driving] 0x001d95e8 RDebris::Add
void RDebris_Add(RDebris& r, float sim_w, int32_t type, const float pos[4], const float vel[4], uint32_t colour,
                 uint32_t enable, uint32_t time) {
    if (enable == 0) return;
    if (type < 0 || type >= kDebrisTypes) {
        ++g_stats.skipped;  // o PS2 leria um ponteiro além da tabela
        return;
    }
    RDebrisType_Add(r.types[type], sim_w, pos, vel, colour, time);
}

// [driving] 0x00235808 GFXGallery::DEBRIS_SetState
void GFXGallery_DEBRIS_SetState() {}

// [driving] 0x002cbc50 (a cópia do VU0_v3quatrotate)
void VU0_quatrotate_copy(const float q[4], const float v[4], float out[4]) { VU0_v3quatrotate(q, v, out); }

// [driving] 0x00235810 GFXGallery::DEBRIS_Draw
void GFXGallery_DEBRIS_Draw(const uint8_t* key, const float pos[4], const float quat[4], const float* vel, uint32_t time) {
    ++g_stats.gallery;
    const uint8_t* dd = key + 0x30;
    auto rf = [&](uint32_t o) {
        float f;
        std::memcpy(&f, dd + o, 4);
        return f;
    };
    auto ru = [&](uint32_t o) {
        uint32_t u;
        std::memcpy(&u, dd + o, 4);
        return u;
    };
    const float size = rf(0x00);
    int32_t count = ee::cvt_w(ee::add(size, size));
    if (ee::le(size, 15.0f) && count > 7) count = 7;
    const float sz = ee::mul(rf(0x0c), size);    // f22
    const float sx = ee::mul(rf(0x08), size);    // f25
    const float lift = ee::mul(rf(0x04), size);  // f27
    const float sim_w = rf(0x28);                // f28
    const uint32_t enable = ru(0x2c);
    float v0[4] = {0, 0, 0, 0};  // sp (memset)
    if (vel) std::memcpy(v0, vel, 12);
    const uint32_t mode = ru(0x14);
    const float k = ps2::ffloat(0x37800000u);  // 2^-16
    if (mode == 0) {
        // Um cartucho (o tipo 4): a velocidade + 0,018 em y; o quatérnio da tela vezes o da chave gira (0, f22, 0, 1).
        ++g_stats.gallery_single;
        v0[1] = ee::add(v0[1], ps2::ffloat(0x3c9374bcu));
        Reg q1, q2, q3{}, q4{}, acc{};
        load(q1, quat);
        load(q2, key + 0x10);
        mul(q3, q1, q2, XYZW);
        subbc(q3, q3, q3, BX, W);
        subbc(q3, q3, q3, BY, W);
        subbc(q3, q3, q3, BZ, W);
        opmula(acc, q1, q2);
        opmsub(q3, acc, q2, q1);
        mulbc(q4, q1, q2, BW, XYZ);
        add(q3, q3, q4, X);
        add(q3, q3, q4, Y);
        add(q3, q3, q4, Z);
        mulbc(q4, q2, q1, BW, XYZ);
        add(q3, q3, q4, X);
        add(q3, q3, q4, Y);
        add(q3, q3, q4, Z);
        float qq[4], v[4] = {0.0f, sz, 0.0f, 1.0f}, o[4];
        store(qq, q3);
        std::memcpy(o, v, 16);
        VU0_quatrotate_copy(qq, v, o);
        Reg a, b, c{};
        load(a, o);
        load(b, v0);
        add(c, a, b, XYZW);
        float out_vel[4];
        store(out_vel, c);
        RDebris_Add(debris(), ps2::ffloat(0x3ba0902eu), 4, pos, out_vel, repack(ru(0x10)), 100, time);
        return;
    }
    GlareJitter& j = glare_jitter();
    static const float kZero4[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // 0x355ba0 (o kZeroCoord4)
    for (int32_t i = 0; i < count; ++i) {
        auto three = [&](uint32_t r[3]) {
            const uint32_t a = j.mul;
            const uint32_t p1 = j.seed * a;
            const uint32_t p2 = (p1 & 0xffffu) * a;
            const uint32_t p3 = (p2 & 0xffffu) * a;
            j.seed = p3 & 0xffffu;
            r[0] = (p1 & 0xffff00u) >> 8;
            r[1] = (p2 & 0xffff00u) >> 8;
            r[2] = (p3 & 0xffff00u) >> 8;
        };
        uint32_t r[3];
        three(r);
        // A velocidade espalhada: x e z por f22 em volta de 0 (menos a metade), y por f25 mais o f27.
        float vv[4];
        vv[0] = ee::sub(ee::add(v0[0], ee::mul(ee::mul(ee::cvt_s(int32_t(r[0])), sz), k)), ee::mul(sz, 0.5f));
        vv[1] = ee::sub(ee::add(ee::add(v0[1], lift), ee::mul(ee::mul(ee::cvt_s(int32_t(r[1])), sx), k)),
                        ee::mul(sx, 0.5f));
        vv[2] = ee::sub(ee::add(v0[2], ee::mul(ee::mul(ee::cvt_s(int32_t(r[2])), sz), k)), ee::mul(sz, 0.5f));
        vv[3] = 0.0f;  // (o PS2: o w que estava na pilha; o Add o sobrescreve)
        float off[4];
        std::memcpy(off, kZero4, 16);
        if (!ee::eq(rf(0x1c), 0.0f) || !ee::eq(rf(0x20), 0.0f) || !ee::eq(rf(0x24), 0.0f)) {
            uint32_t s[3];
            three(s);
            const float a = rf(0x1c), b = rf(0x24), c = rf(0x20);
            off[0] = ee::add(ee::mul(ee::neg(a), 0.5f), ee::mul(ee::mul(ee::cvt_s(int32_t(s[0])), a), k));
            off[1] = ee::add(ee::mul(ee::neg(b), 0.5f), ee::mul(ee::mul(ee::cvt_s(int32_t(s[1])), b), k));
            off[2] = ee::add(ee::mul(ee::neg(c), 0.5f), ee::mul(ee::mul(ee::cvt_s(int32_t(s[2])), c), k));
            float rot[4];
            std::memcpy(rot, off, 16);
            VU0_quatrotate_copy(quat, off, rot);
            std::memcpy(off, rot, 16);
        }
        Reg a, b, c{};
        load(a, off);
        load(b, pos);
        add(c, a, b, XYZW);
        float p[4];
        store(p, c);
        if (mode == 0 || mode >= 0x14) continue;
        int32_t type;
        if (mode == 2) type = i % 3;
        else if (mode == 3) type = (i % 2) + 5;
        else {
            // A tabela 0x39c980 (o tipo de destroço por modo).
            static const int32_t kModeTypes[0x14] = {0, 3, 0, 0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22};
            type = kModeTypes[mode];
        }
        RDebris_Add(debris(), sim_w, type, p, vv, ru(0x10), enable, time);
    }
}

// O que o BondDebris faz por pedaço (o primeiro laço e o começo do segundo): o ângulo, o seno e o cosseno pelo ESIN,
// t = SimTick.x - o nascimento, a posição + v t + (0, SimTick.y t², 0), as linhas da matriz do quatérnio.
void bond_debris_instance_matrix(const float simtick[4], const float offset[4], const float velocity[4],
                                 const float quat[4], float m[4][4]) {
    Reg g, v, q, o;
    load(g, simtick);
    load(v, velocity);
    load(q, quat);
    load(o, offset);
    // o primeiro laço
    const uint32_t t = ps2::sub(g[0], v[3]);
    q[3] = ps2::mul(q[3], t);
    q[3] = ps2::sub(q[3], 0x407ccccdu);  // 3,95
    const uint32_t s = ps2::efu_sin(q[3]);
    q[3] = ps2::add(q[3], 0x3fc90ff9u);  // 1,5708
    v[3] = ps2::mul(kOne, t);
    for (int c = 0; c < 3; ++c) q[c] = ps2::mul(q[c], s);
    q[3] = ps2::efu_sin(q[3]);
    // o segundo laço
    for (int c = 0; c < 3; ++c) v[c] = ps2::mul(v[c], v[3]);
    v[3] = ps2::mul(v[3], v[3]);
    Reg x2, y2, z2, w2;
    for (int c = 0; c < 4; ++c) {
        x2[c] = ps2::mul(q[c], q[0]);
        y2[c] = ps2::mul(q[c], q[1]);
        z2[c] = ps2::mul(q[c], q[2]);
        w2[c] = ps2::mul(q[c], q[3]);
    }
    g[1] = ps2::mul(g[1], v[3]);
    for (int c = 0; c < 3; ++c) o[c] = ps2::add(o[c], v[c]);
    for (int c = 0; c < 4; ++c) {
        x2[c] = ps2::add(x2[c], x2[c]);
        y2[c] = ps2::add(y2[c], y2[c]);
        z2[c] = ps2::add(z2[c], z2[c]);
        w2[c] = ps2::add(w2[c], w2[c]);
    }
    o[1] = ps2::add(o[1], g[1]);
    Reg r0 = {kOne, ps2::add(0, y2[0]), ps2::add(0, z2[0]), 0};
    Reg r1 = {ps2::add(0, y2[0]), kOne, ps2::add(0, z2[1]), 0};
    Reg r2 = {ps2::add(0, z2[0]), ps2::add(0, y2[2]), kOne, 0};
    r0[0] = ps2::sub(r0[0], y2[1]);
    r0[1] = ps2::add(r0[1], z2[3]);
    r0[2] = ps2::sub(r0[2], y2[3]);
    r1[0] = ps2::sub(r1[0], z2[3]);
    r1[1] = ps2::sub(r1[1], x2[0]);
    r1[2] = ps2::add(r1[2], w2[0]);
    r2[2] = ps2::sub(r2[2], x2[0]);
    r2[0] = ps2::add(r2[0], y2[3]);
    r2[1] = ps2::sub(r2[1], w2[0]);
    r0[0] = ps2::sub(r0[0], z2[2]);
    r1[1] = ps2::sub(r1[1], z2[2]);
    r2[2] = ps2::sub(r2[2], y2[1]);
    store(m[0], r0);
    store(m[1], r1);
    store(m[2], r2);
    const Reg r3 = {o[0], o[1], o[2], kOne};
    store(m[3], r3);
}

}  // namespace drv
