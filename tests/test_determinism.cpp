// O determinismo entre as plataformas (docs/port.md, "macOS: compilação e CI"): um resumo (XXH3-64) de cada família de
// contas que o port precisa repetir bit a bit, comparado com o da referência (o Windows; o MSVC x86 e o x64 dão o mesmo).
// A CI roda estes testes no Linux x86_64 (GCC e clang) e no macOS ARM64 (o clang da Apple).
//
// Conferidos (o teste falha se o resumo mudar):
//  - a aritmética do PS2 em software (platform/ps2_float.h: soma, subtração, produto, multiplica-e-soma, divisão, raiz,
//    conversões), que o interpretador das VUs e a física das missões de carro usam;
//  - a física das missões de carro (src/driving/physics: seis caixas largadas num chão plano, 600 quadros);
//  - as contas de float do IEEE sem FMA (a * b + c, a * b - c * d, a / b, a raiz): o -ffp-contract=off do clang e do
//    GCC (o ARM64 juntaria em FMA);
//  - as mesmas contas com o arredondamento para zero do EE (nf::RoundTowardZero, a pontuação): o fesetround do POSIX
//    contra o _controlfp_s do MSVC.
// Só informados (a libm de cada sistema pode diferir no último bit da double: o resumo diz se deu o mesmo da
// referência, sem falhar): as funções de float de platform/fmath.h, as funções da EFU (que usam a libm em double) e as
// contas do vecmath do jogo (PS2Sinf, ATAN2_APPROX, RotMatrix, Quat_Slerp, Mat_Inverse).
//
// Os valores de referência saíram do nf_tests do Windows (x86 e x64, iguais); para trocá-los, rode
// "nf_tests determinism" e copie os resumos impressos.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/xxh3.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/vu0_physics.h"
#include "driving/physics/world.h"
#include "engine/vecmath.h"
#include "platform/compiler.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "test_framework.h"

// As contas sob nf::RoundTowardZero mudam o modo de arredondamento (como a pontuação, player_stats.cpp).
NF_FENV_ACCESS_ON

namespace {

constexpr int kCount = 1 << 17;

struct Rng {
    uint32_t s = 0x2545f491u;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    // Um float normal, sem NaN, infinito ou denormal, de sinal e expoente variados (2^-27 a 2^23).
    float normal() {
        const uint32_t r = next();
        const uint32_t exp = 100u + (r >> 8) % 51u;
        uint32_t bits = (r & 0x80000000u) | (exp << 23) | (next() & 0x7fffffu);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
};

uint32_t bits_of(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

uint64_t digest(const std::vector<uint32_t>& v) { return nf::xxh3_64(v.data(), v.size() * 4); }

// Imprime e devolve se bate com a referência.
bool report(const char* what, uint64_t got, uint64_t ref) {
    const bool same = got == ref;
    std::printf("  determinismo %-14s %016llx %s\n", what, static_cast<unsigned long long>(got),
                same ? "= referência" : "DIFERENTE da referência");
    return same;
}

}  // namespace

TEST(determinism_ps2_float_matches_the_reference) {
    Rng rng;
    std::vector<uint32_t> out;
    out.reserve(size_t(kCount) * 9);
    for (int i = 0; i < kCount; ++i) {
        const uint32_t a = rng.next(), b = rng.next(), c = rng.next();
        out.push_back(ps2::add(a, b));
        out.push_back(ps2::sub(a, b));
        out.push_back(ps2::mul(a, b));
        out.push_back(ps2::madd(c, a, b));
        out.push_back(ps2::msub(c, a, b));
        out.push_back(ps2::div(a, b));
        out.push_back(ps2::sqrt(b));
        out.push_back(ps2::rsqrt(a, b));
        out.push_back(uint32_t(ps2::to_int(a, int(c & 3) * 4)) ^ ps2::from_int(int32_t(b), int(c >> 30) * 4));
    }
    CHECK(report("ps2_float", digest(out), 0x6fcf9ff7c126fb1cull));
}

namespace {

void physics_fresh() {
    drv::reset_rigid_globals();
    drv::physics_pending_reset();
    drv::set_vu0_mode(drv::Vu0Mode::Native);
    drv::simulation_reset(60);
    drv::vu0_reset();
    drv::set_world(nullptr);
}

}  // namespace

TEST(determinism_driving_physics_matches_the_reference) {
    // Seis caixas de massas, tamanhos, alturas, giros e velocidades diferentes, largadas num chão plano: 600 quadros do
    // passo inteiro (ESimFrameUpdate), com o estado de cada corpo em cada quadro no resumo.
    physics_fresh();
    drv::FlatGround ground(0.0f);
    drv::set_world(&ground);
    const float c = 0.6f, s = 0.8f;  // um giro com cosseno e seno exatos em qualquer libm
    for (int k = 0; k < 6; ++k) {
        drv::RigidBodyParams p;
        p.mass = 50.0f + 75.0f * float(k);
        p.type = 4;
        p.pos[0] = 4.0f * float(k) - 10.0f;
        p.pos[1] = 1.5f + 0.75f * float(k);
        p.pos[2] = 2.0f - 0.5f * float(k);
        p.vel[0] = 0.5f * float(k % 3) - 0.5f;
        p.vel[2] = 1.25f - 0.25f * float(k);
        p.ang_vel[0] = 0.3f * float(k);
        p.ang_vel[1] = 1.0f - 0.2f * float(k);
        p.ang_vel[2] = -0.4f + 0.15f * float(k);
        if (k % 2 == 1) {  // girada em torno de y (k = 1, 5) ou de x (k = 3)
            const bool about_x = k == 3;
            float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
            if (about_x) {
                m[1][1] = c, m[1][2] = s, m[2][1] = -s, m[2][2] = c;
            } else {
                m[0][0] = c, m[0][2] = -s, m[2][0] = s, m[2][2] = c;
            }
            std::memcpy(p.orient, m, sizeof(m));
        }
        const float hx = 0.4f + 0.1f * float(k), hy = 0.3f + 0.05f * float(k), hz = 0.6f;
        p.half_size[0] = hx;
        p.half_size[1] = hy;
        p.half_size[2] = hz;
        p.inv_inertia[0] = 3.0f / (p.mass * (hy * hy + hz * hz));
        p.inv_inertia[1] = 3.0f / (p.mass * (hx * hx + hz * hz));
        p.inv_inertia[2] = 3.0f / (p.mass * (hx * hx + hy * hy));
        drv::simulation_add_body(drv::simulation(), p);
    }
    std::vector<uint32_t> out;
    for (int frame = 0; frame < 600; ++frame) {
        drv::ESimFrameUpdate(drv::simulation());
        for (int k = 0; k < 6; ++k) {
            const drv::RigidBody* rb = drv::GetRigidBody(drv::simulation(), k);
            for (float f : rb->quat) out.push_back(bits_of(f));
            for (float f : rb->pos) out.push_back(bits_of(f));
            for (float f : rb->vel) out.push_back(bits_of(f));
            for (float f : rb->ang_vel) out.push_back(bits_of(f));
            for (float f : rb->momentum) out.push_back(bits_of(f));
            for (float f : rb->ang_momentum) out.push_back(bits_of(f));
            out.push_back(uint32_t(rb->state));
        }
    }
    drv::set_world(nullptr);
    CHECK(report("fisica_carros", digest(out), 0x1522d892ad13f50eull));
}

TEST(determinism_ieee_float_without_fma_matches_the_reference) {
    Rng rng;
    std::vector<uint32_t> out;
    out.reserve(size_t(kCount) * 4);
    for (int i = 0; i < kCount; ++i) {
        const float a = rng.normal(), b = rng.normal(), c = rng.normal(), d = rng.normal();
        out.push_back(bits_of(a * b + c));
        out.push_back(bits_of(a * b - c * d));
        out.push_back(bits_of(a / b));
        out.push_back(bits_of(std::sqrt(std::fabs(a))));
    }
    CHECK(report("float_ieee", digest(out), 0x46d2dae1212f1c87ull));
}

TEST(determinism_round_toward_zero_matches_the_reference) {
    Rng rng;
    std::vector<uint32_t> out;
    out.reserve(size_t(kCount) * 4);
    {
        const nf::RoundTowardZero ee;
        for (int i = 0; i < kCount; ++i) {
            const float a = rng.normal(), b = rng.normal(), c = rng.normal(), d = rng.normal();
            out.push_back(bits_of(a * b + c));
            out.push_back(bits_of(a * b - c * d));
            out.push_back(bits_of(a / b));
            out.push_back(bits_of(std::sqrt(std::fabs(a))));
        }
    }
    CHECK(report("arredonda_zero", digest(out), 0x20b9b32fe50f24c3ull));
}

TEST(determinism_libm_and_engine_math_are_reported) {
    // Só informa (a libm de cada sistema): o resumo e se bate com o do Windows.
    Rng rng;
    std::vector<uint32_t> fm, efu, eng;
    for (int i = 0; i < kCount; ++i) {
        const float x = rng.normal(), y = rng.normal();
        const float u = float(rng.next() >> 8) * (2.0f / 16777216.0f) - 1.0f;  // [-1, 1)
        fm.push_back(bits_of(nf::fm::sin(x)));
        fm.push_back(bits_of(nf::fm::cos(x)));
        fm.push_back(bits_of(nf::fm::tan(u)));
        fm.push_back(bits_of(nf::fm::asin(u)));
        fm.push_back(bits_of(nf::fm::acos(u)));
        fm.push_back(bits_of(nf::fm::atan(x)));
        fm.push_back(bits_of(nf::fm::atan2(x, y)));
        fm.push_back(bits_of(nf::fm::exp(u * 20.0f)));
        fm.push_back(bits_of(nf::fm::log(std::fabs(x))));
        fm.push_back(bits_of(nf::fm::log10(std::fabs(y))));
        fm.push_back(bits_of(nf::fm::pow(std::fabs(u) + 0.5f, u * 8.0f)));
        efu.push_back(ps2::efu_sin(bits_of(u * 1.5f)));
        efu.push_back(ps2::efu_atan(bits_of(u)));
        efu.push_back(ps2::efu_exp(bits_of(std::fabs(u) * 4.0f)));
        eng.push_back(bits_of(PS2Sinf(u * 3.0f)));
        eng.push_back(bits_of(ATAN2_APPROX(x, y)));
    }
    for (int i = 0; i < 4096; ++i) {
        _VECTOR e{rng.normal(), rng.normal(), rng.normal(), 0.0f};
        MATRIX m{};
        RotMatrix(&e, &m);
        MATRIX inv{};
        Mat_Inverse(&m, &inv);
        const quaternion_tag qa{0.5f, 0.5f, 0.5f, 0.5f};
        const quaternion_tag qb{float(i % 7) * 0.1f, 0.2f, -0.3f, 0.9f};
        quaternion_tag qs{};
        Quat_Slerp(float(i % 100) * 0.01f, &qa, &qb, &qs);
        for (const float* f = &m.m[0][0]; f < &m.m[0][0] + 16; ++f) eng.push_back(bits_of(*f));
        for (const float* f = &inv.m[0][0]; f < &inv.m[0][0] + 16; ++f) eng.push_back(bits_of(*f));
        eng.push_back(bits_of(qs.x));
        eng.push_back(bits_of(qs.y));
        eng.push_back(bits_of(qs.z));
        eng.push_back(bits_of(qs.w));
    }
    report("fmath", digest(fm), 0x90b6f870987728a0ull);
    report("efu", digest(efu), 0x71aa6515feb3b4d8ull);
    report("vecmath", digest(eng), 0x3052af0dc5d3dbe7ull);
}
