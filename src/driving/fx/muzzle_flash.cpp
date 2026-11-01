#include "driving/fx/muzzle_flash.h"

#include <cmath>
#include <cstring>

#include "driving/ai/sim_random.h"
#include "driving/fx/fx_render.h"
#include "driving/light_manager.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

MuzzleFlashState g_state;
MuzzleFlashEnv g_env;
MuzzleFlashStats g_stats;

template <typename T>
T rd(uint32_t p, uint32_t off) {
    T v;
    std::memcpy(&v, nf::p32_ptr<const uint8_t>(p) + off, sizeof v);
    return v;
}
template <typename T>
void wr(uint32_t p, uint32_t off, T v) {
    std::memcpy(nf::p32_ptr<uint8_t>(p) + off, &v, sizeof v);
}

void identity(float m[4][4]) {
    std::memset(m, 0, 64);
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}
// 0x002d1158 BuildScale(MATRIX4 &, float, float, float) e 0x002d1128 BuildScale(MATRIX4 &, float): a diagonal, a linha 3
// o vf0.
void build_scale(float m[4][4], float x, float y, float z) {
    std::memset(m, 0, 64);
    m[0][0] = x;
    m[1][1] = y;
    m[2][2] = z;
    m[3][3] = 1.0f;
}
// 0x002d1188 BuildTranslate(MATRIX4 &, float, float, float)
void build_translate(float m[4][4], float x, float y, float z) {
    identity(m);
    m[3][0] = x;
    m[3][1] = y;
    m[3][2] = z;
}

// O inteiro sem sinal em float (CVT.S.W; acima de 2^31 pela metade, com o bit de baixo, somada a si mesma).
float u2f(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float h = ee::cvt_s(int32_t((v & 1) | (v >> 1)));
    return ee::add(h, h);
}
// pow(0,75, n) do newlib em double, levado a float pelo dptofp do libgcc (ao mais perto): o n é sempre um inteiro
// pequeno (os clarões vivem 4 ou 5 quadros), cuja potência o double tem exata.
float fade(float n) { return float(std::pow(0.75, double(n))); }

// O RRandom inline (a semente e o multiplicador de 0x333e60/0x333e64): os bits 8-23 do produto.
uint32_t rrandom() { return RRandom_Generate(render_random()); }

}  // namespace

MuzzleFlashState& muzzle_flash() { return g_state; }
MuzzleFlashEnv& muzzle_flash_env() { return g_env; }
MuzzleFlashStats& muzzle_flash_stats() { return g_stats; }

int muzzle_texture_slot(uint32_t texture) { return kFxTexMuzzle0 + int(texture); }
int muzzle_pov_texture_slot(uint32_t texture) { return kFxTexMuzzlePov0 + int(texture); }

void RMuzzleFlash_StaticInit(MuzzleFlashState& s, const std::function<bool(uint32_t, void*, uint32_t)>& read) {
    s = MuzzleFlashState();
    for (int i = 0; i < kMuzzleTypes; ++i) UVolatileMaterial_Construct(s.material[i]);
    UVolatileMaterial_Construct(s.pov_material);
    if (!read) return;
    // O modelo e os UV (0x334e40, 0x334fe0), os nomes das cinco texturas (0x3350d8: palavras de 4 letras) e os dos POV
    // (0x3350f0: três por tipo, 0x18 bytes por tipo).
    if (!read(0x334e40, s.positions, sizeof s.positions)) std::memset(s.positions, 0, sizeof s.positions);
    if (!read(0x334fe0, s.uv, sizeof s.uv)) std::memset(s.uv, 0, sizeof s.uv);
    if (!read(0x3350d8, s.texture_names, sizeof s.texture_names)) std::memset(s.texture_names, 0, 20);
    for (int t = 0; t < kMuzzlePovTypes; ++t)
        if (!read(0x3350f0 + uint32_t(t) * 0x18, s.pov[t].names, 12)) std::memset(s.pov[t].names, 0, 12);
}

// [driving] 0x001e4b48 RMuzzleFlash::InitPOVLayer
void RMuzzleFlash_InitPOVLayer(MuzzleFlashState& s) {
    static const float kPos[4][4] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}, {1, 1, 0, 1}};
    static const float kUv[4][4][2] = {
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
        {{1, 0}, {1, 1}, {0, 0}, {0, 1}},
        {{1, 1}, {0, 1}, {1, 0}, {0, 0}},
        {{0, 1}, {0, 0}, {1, 1}, {1, 0}},
    };
    std::memcpy(s.pov_positions, kPos, sizeof kPos);
    std::memcpy(s.pov_uv, kUv, sizeof kUv);
    const uint64_t alpha = 0x8000000048ull;  // o ALPHA_1: Cs + Cd
    std::memcpy(s.pov_material + 0x40, &alpha, 8);
    uint64_t prim;
    std::memcpy(&prim, s.pov_material + 0x10, 8);
    prim = (prim & 0xffffffffffffffa7ull) | 0x58;
    std::memcpy(s.pov_material + 0x10, &prim, 8);
    // RTextureContext::FindOrCreateTexture(contexto 0, nome): o port guarda tipo x 3 + k.
    for (int t = 0; t < kMuzzlePovTypes; ++t)
        for (int k = 0; k < 3; ++k) s.pov[t].textures[k] = uint32_t(t * 3 + k);
}

// [driving] 0x001e5488 RMuzzleFlash::Init
void RMuzzleFlash_Init(MuzzleFlashState& s) {
    RMuzzleFlash_InitPOVLayer(s);
    for (int i = 0; i < kMuzzleTypes; ++i) {
        s.texture[i] = uint32_t(i);
        s.texture_flags[i] &= 0xfffffff0u;
        const uint64_t alpha = 0x8000000048ull;
        std::memcpy(s.material[i] + 0x40, &alpha, 8);
        uint64_t prim;
        std::memcpy(&prim, s.material[i] + 0x10, 8);
        prim = (prim & 0xffffffffffffffa7ull) | 0x58;
        std::memcpy(s.material[i] + 0x10, &prim, 8);
    }
    // O modelo girado um quarto de volta em y ((x, z) vira (z, -x)) e a cor.
    for (int v = 0; v < kMuzzleVerts; ++v) {
        const float x = s.positions[v][0], z = s.positions[v][2];
        s.colours[v] = 0x687f7f7fu;
        s.positions[v][0] = z;
        s.positions[v][2] = ee::neg(x);
    }
    s.ready = true;
}

// [driving] 0x001e55f0 RMuzzleFlash::Draw
void RMuzzleFlash_Draw(MuzzleFlashState& s, float size, const float transform[4][4], const float pos[4],
                       const float dir[4], int32_t type, std::vector<VolatileDraw>* out) {
    using namespace ps2::vu;
    alignas(16) float m[4][4];
    identity(m);
    const float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    std::memcpy(m[1], up, 16);
    std::memcpy(m[2], dir, 16);
    m[2][3] = 0.0f;
    // vopmula.xyz ACC, dir, up; vopmsub.xyz vf3, up, dir (o direito = dir x up); vrsqrt Q, vf0w, |vf3|²; vmulq.xyz
    uint32_t d[4], u[4], acc[4] = {}, r[4] = {ps2::fbits(m[0][0]), ps2::fbits(m[0][1]), ps2::fbits(m[0][2]),
                                                ps2::fbits(m[0][3])};
    std::memcpy(d, dir, 16);
    std::memcpy(u, up, 16);
    opmula(acc, d, u);
    opmsub(r, acc, u, d);
    uint32_t l2 = ps2::mul(r[0], r[0]);
    l2 = ps2::madd(l2, 0x3f800000u, ps2::mul(r[1], r[1]));
    l2 = ps2::madd(l2, 0x3f800000u, ps2::mul(r[2], r[2]));
    const uint32_t q = ps2::rsqrt(0x3f800000u, l2);
    for (int i = 0; i < 3; ++i) m[0][i] = ps2::ffloat(ps2::mul(r[i], q));
    m[0][3] = 0.0f;
    std::memcpy(m[3], pos, 16);
    VU0_MATRIX4_mult(m, transform, m);
    size = ee::mul(size, 0.25f);
    alignas(16) float sc[4][4];
    build_scale(sc, size, size, size);
    VU0_MATRIX4_mult(sc, m, m);
    if (type < 0 || type >= kMuzzleTypes) {
        ++g_stats.guards;  // o PS2 leria o material e a textura fora das cinco
        return;
    }
    ++g_stats.draws;
    // (TIMER_removetask(fgRenderer + 0x78, 0 / 1): o perfil do PS2)
    UVolatileMaterial_Draw(s.material[type], 5, kMuzzleVerts, s.positions, s.uv, s.colours, 0, out, m,
                           muzzle_texture_slot(s.texture[type]));
}

// [driving] 0x001e5808 RMuzzleFlash::DrawPOV
void RMuzzleFlash_DrawPOV(MuzzleFlashState& s, float size, const float transform[4][4], const float pos[3],
                          int32_t type, bool paused, std::vector<VolatileDraw>* out) {
    alignas(16) float m[4][4], sc[4][4];
    build_translate(m, pos[0], pos[1], pos[2]);
    VU0_MATRIX4_mult(m, transform, m);
    build_scale(sc, size, size, size);
    VU0_MATRIX4_mult(sc, m, m);
    if (type < 0 || type >= kMuzzlePovTypes) {
        ++g_stats.guards;
        return;
    }
    int turn = 0, tex = 0;
    if (!paused) {
        // Dois números do RRandom: o giro do UV (o resto por 4) e a textura (o resto por 3).
        turn = int32_t(rrandom()) % 4;
        tex = int32_t(rrandom()) % 3;
    }
    ++g_stats.draws;
    UVolatileMaterial_Draw(s.pov_material, 5, 4, s.pov_positions, s.pov_uv[turn], s.colours, 0, out, m,
                           muzzle_pov_texture_slot(s.pov[type].textures[tex]));
}

// [driving] 0x00116720 ActWeapon::StartMuzzleFlash
void ActWeapon_StartMuzzleFlash(uint32_t weapon, const float pos[3], int32_t steps) {
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t slot = 0x84 + i * 0x14;
        if (steps < rd<int32_t>(weapon, slot + 0xc)) continue;
        // A distância ao carro do jogador / 400, até 0,5 (vsub.xyz, o comprimento: vsqrt, vaddq.x vf0).
        float player[3] = {0.0f, 0.0f, 0.0f};
        if (g_env.player_position) g_env.player_position(player);
        uint32_t dv[3];
        for (int k = 0; k < 3; ++k) dv[k] = ps2::sub(ps2::fbits(player[k]), ps2::fbits(pos[k]));
        uint32_t l2 = ps2::mul(dv[0], dv[0]);
        l2 = ps2::madd(l2, 0x3f800000u, ps2::mul(dv[1], dv[1]));
        l2 = ps2::madd(l2, 0x3f800000u, ps2::mul(dv[2], dv[2]));
        float near = ee::div(ps2::ffloat(ps2::add(0, ps2::sqrt(l2))), 400.0f);
        if (ee::lt(0.5f, near)) near = 0.5f;
        std::memcpy(nf::p32_ptr<uint8_t>(weapon) + slot, pos, 12);
        wr<int32_t>(weapon, slot + 0xc, steps + 4);
        const float r = ee::cvt_s(int32_t(rrandom()));
        const float strength = ee::add(ee::add(ee::mul(ee::mul(r, 0.25f), ps2::ffloat(0x37800000u)), 0.75f), near);
        wr<float>(weapon, slot + 0x10, strength);
        // A linha 2 da matriz da arma (RSceneObj::GetTransform do +0x40) e o passo + 7.
        alignas(16) float t[4][4];
        if (g_env.transform) {
            g_env.transform(weapon + 0x40, t);
        } else {
            std::memcpy(t, nf::p32_ptr<const uint8_t>(rd<uint32_t>(weapon, 0x48)), 64);
            t[0][3] = t[1][3] = t[2][3] = 0.0f;
            t[3][3] = 1.0f;
        }
        std::memcpy(nf::p32_ptr<uint8_t>(weapon) + 0x124, t[2], 16);
        wr<int32_t>(weapon, 0x134, steps + 7);
        ++g_stats.actor_started;
        return;
    }
    ++g_stats.actor_full;
}

// [driving] 0x001169c8 ActWeapon::ManualRender (os clarões)
void ActWeapon_DrawMuzzleFlashes(MuzzleFlashState& s, uint32_t weapon, int32_t steps, bool paused,
                                 std::vector<VolatileDraw>* out) {
    for (uint32_t i = 0; i < 8; ++i) {
        const uint32_t slot = 0x84 + i * 0x14;
        const int32_t end = rd<int32_t>(weapon, slot + 0xc);
        if (!(steps < end)) continue;
        alignas(16) float pos[4];
        std::memcpy(pos, nf::p32_ptr<const uint8_t>(weapon) + slot, 12);
        pos[3] = 1.0f;
        const float w = fade(u2f(uint32_t(steps + 4 - end)));
        alignas(16) float t[4][4];
        if (g_env.transform) {
            g_env.transform(weapon + 0x40, t);
        } else {
            std::memcpy(t, nf::p32_ptr<const uint8_t>(rd<uint32_t>(weapon, 0x48)), 64);
            t[0][3] = t[1][3] = t[2][3] = 0.0f;
            t[3][3] = 1.0f;
        }
        const float strength = rd<float>(weapon, slot + 0x10);
        if ((rd<int32_t>(weapon, 0x138) >> 1) & 1) {
            // A arma do jogador: o tamanho e a textura pela última arma disparada.
            const int32_t last = g_env.last_fired ? g_env.last_fired() : 0;
            float k = ps2::ffloat(0x3d9fbe77u);  // 0,078
            int32_t type = 0;
            switch (last) {
                case 0x1a: k = ps2::ffloat(0x3e3851ecu); type = 2; break;  // 0,18
                case 0x1b: k = ps2::ffloat(0x3d75c28fu); type = 6; break;  // 0,06
                case 0x1c: k = ps2::ffloat(0x3da3d70au); type = 3; break;  // 0,08
                case 0x1d: k = ps2::ffloat(0x3dcccccdu); type = 7; break;  // 0,1
                case 0x1e: k = ps2::ffloat(0x3e4ccccdu); type = 7; break;  // 0,2
                default: break;
            }
            ++g_stats.pov_drawn;
            RMuzzleFlash_DrawPOV(s, ee::mul(ee::mul(strength, k), w), t, pos, type, paused, out);
        } else {
            const float dir[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            ++g_stats.actor_drawn;
            RMuzzleFlash_Draw(s, ee::mul(ee::mul(0.4f, strength), w), t, pos, dir, 0, out);
        }
    }
}

// [driving] 0x001d38d8 RVehicle::StartMuzzleFlash
void RVehicle_StartMuzzleFlash(uint32_t vehicle, const float pos[3], const float dir[3], int32_t slot, int32_t weapon) {
    const uint32_t s4 = uint32_t(slot) * 4, s16 = uint32_t(slot) * 0x10;
    const uint32_t time = rtime_data().time;
    if (time < rd<uint32_t>(vehicle, 0x220 + s4)) {
        ++g_stats.vehicle_busy;
        return;
    }
    std::memcpy(nf::p32_ptr<uint8_t>(vehicle) + 0x1e0 + s16, pos, 12);
    std::memcpy(nf::p32_ptr<uint8_t>(vehicle) + 0x200 + s16, dir, 12);
    wr<uint32_t>(vehicle, 0x220 + s4, time + 5);
    const uint32_t owner = rd<uint32_t>(vehicle, 0x24);
    bool turret = false;
    if (owner != 0 && rd<int32_t>(owner, 0x40) == 1) turret = g_env.has_turret ? g_env.has_turret(owner) : false;
    float strength;
    int32_t type;
    if (weapon == 0x2d) {
        strength = ee::add(ee::mul(ee::cvt_s(int32_t(rrandom())), ps2::ffloat(0x37800000u)), 3.0f);
        type = 4;
    } else if (turret) {
        strength = ee::add(ee::mul(ee::mul(ee::cvt_s(int32_t(rrandom())), 0.2f), ps2::ffloat(0x37800000u)), 0.6f);
        wr<float>(vehicle, 0x230 + s4, strength);
        wr<int32_t>(vehicle, 0x228 + s4, (g_env.is_snowmobile && g_env.is_snowmobile(owner) != 0) ? 4 : 3);
        ++g_stats.vehicle_started;
        return;
    } else {
        strength = ee::add(ee::mul(ee::mul(ee::cvt_s(int32_t(rrandom())), 0.2f), ps2::ffloat(0x37800000u)), 0.6f);
        type = 2;
    }
    wr<float>(vehicle, 0x230 + s4, strength);
    wr<int32_t>(vehicle, 0x228 + s4, type);
    ++g_stats.vehicle_started;
}

// [driving] 0x001d3278 RVehicle::RenderFX (os clarões)
void RVehicle_DrawMuzzleFlashes(MuzzleFlashState& s, uint32_t vehicle, const float transform[4][4], uint32_t time,
                                bool paused, std::vector<VolatileDraw>* out) {
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t end = rd<uint32_t>(vehicle, 0x220 + i * 4);
        if (!(time < end)) continue;
        const float w = fade(u2f(time + 5 - end));
        const float size = ee::mul(rd<float>(vehicle, 0x230 + i * 4), w);
        alignas(16) float pos[4], dir[4];
        std::memcpy(pos, nf::p32_ptr<const uint8_t>(vehicle) + 0x1e0 + i * 0x10, 16);
        std::memcpy(dir, nf::p32_ptr<const uint8_t>(vehicle) + 0x200 + i * 0x10, 16);
        const int32_t type = rd<int32_t>(vehicle, 0x228 + i * 4);
        ++g_stats.vehicle_drawn;
        if (type == 2) RMuzzleFlash_DrawPOV(s, size, transform, pos, 0, paused, out);
        else RMuzzleFlash_Draw(s, size, transform, pos, dir, type, out);
    }
}

}  // namespace drv
