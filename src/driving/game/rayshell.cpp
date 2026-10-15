// As balas instantâneas (o RayShell, driving/game/shells.h) e as utilidades que elas usam, do MIPS do DRIVING.ELF
// (PVehicle.obj, SWeaponManager.obj, PhysicsObject.obj, Simulation.obj, a newlib). As desmontagens ficam em
// out/agent_drv_integ/tiros/ do checkout principal (fora do git).

#include <cstdio>
#include <cstring>

#include "driving/ai/ai_vu.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/game/missile.h"
#include "driving/game/shells.h"
#include "driving/physics/obb.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;
using aivu::copy3;

namespace {

RayShellGlobals g_ray;
RayShellData g_last{};
ShellsStats g_stats;
GaussState g_gauss;
ShellsEnv g_env;

constexpr uint32_t kOne = 0x3f800000u;
constexpr float kUp[3] = {0.0f, 1.0f, 0.0f};  // 0x37bfa8 (o sentido da explosão da trapaça)

SimpleObject* as_simple(PhysicsObject* o) { return dynamic_cast<SimpleObject*>(o); }

int rigid_slot_of(const Simulation& sim, const PhysicsObject* o) {
    for (int i = 0; i < kRigidBodySlots; ++i)
        if (sim.owners[i] == o) return i;
    return -1;
}

// O tipo (+0x40) de um dono que pode ser nulo: o PS2 leria o endereço 0x40 (zero no interpretador): conta e dá 0.
int32_t kind_or_guard(PhysicsObject* o) {
    if (o) return o->kind();
    ++g_stats.guards;
    return 0;
}

// PhysicsObject::IsOwnedBy com o this nulo: o PS2 lê o +0x58 do endereço 0 (a memória do núcleo; zero no
// interpretador): falso, contado.
bool owned_by(PhysicsObject* self, PhysicsObject* other) {
    if (!self) {
        if (other) ++g_stats.guards;
        return false;
    }
    return PhysicsObject_IsOwnedBy(self, other);
}

// A trapaça das balas explosivas (o UberBond +0x9a0, o bit 0x1000) nos tiros do jogador.
bool cheat_explosive() { return (uber_bond().word(0x9a0) & 0x1000) != 0; }

// O fim dos acertos do DamagePhysicsObject e do CollideWithWorld: no máximo uma explosão da trapaça a cada 1/5 s (o
// global 0x333564).
void cheat_explosion(uint32_t sig, const float pos[3]) {
    Simulation& sim = simulation();
    const int32_t rate = sim.frame_rate / 5;  // div (com o break 7 do divisor zero)
    if (!(g_ray.last_explosion + rate < sim.steps) && sim.steps != g_ray.last_explosion) return;
    g_ray.last_explosion = sim.steps;
    PhysicsObject* owner = FindPhysicsObjectSignature(sim, sig);
    if (!owned_by(owner, GetPlayerObject(sim)) || !cheat_explosive()) return;
    ++g_stats.cheat_explosions;
    SpawnExplosion(sim, 50.0f, 2, -1, kUp, pos, 5, nullptr);
}

}  // namespace

RayShellGlobals& rayshell_globals() { return g_ray; }
const RayShellData& rayshell_last() { return g_last; }
ShellsStats& shells_stats() { return g_stats; }
GaussState& gauss_state() { return g_gauss; }
ShellsEnv& shells_env() { return g_env; }

void shells_reset() {
    g_ray = RayShellGlobals{};
    g_stats = ShellsStats{};
    g_gauss = GaussState{};
    window_map().clear();
    hit_window_events().clear();
    shell_effects_counters() = ShellEffectsCounters{};
    fire_sound_counters() = FireSoundCounters{};
}

// ------------------------------------------------------------------------------------------------- as palavras

uint32_t object_image(PhysicsObject* o) {
    if (!o) return 0;
    if (BondCar* c = dynamic_cast<BondCar*>(o)) return nf::p32_u(&c->d);
    if (SimpleObject* s = as_simple(o)) return nf::p32_u(&s->world_pos_);
    return nf::p32_u(o);
}

int32_t object_ordered_index(Simulation& sim, PhysicsObject* o) {
    if (!o) return -1;
    for (int i = 0; i < kRigidBodySlots; ++i)
        if (sim.owners[i] == o) return i;
    for (int i = 0; i < kSimpleBodySlots; ++i)
        if (sim.simple_owners[i] == o) return kRigidBodySlots + i;
    return -1;
}

PhysicsObject* object_of_image(Simulation& sim, uint32_t image) {
    if (image == 0) return nullptr;
    for (PhysicsObject* o : sim.owners)
        if (o && object_image(o) == image) return o;
    for (PhysicsObject* o : sim.simple_owners)
        if (o && object_image(o) == image) return o;
    return nullptr;
}

uint32_t object_word(PhysicsObject* o, uint32_t offset) {
    if (!o) {
        ++g_stats.guards;
        return 0;
    }
    uint32_t v = 0;
    if (BondCar* c = dynamic_cast<BondCar*>(o)) {
        if (offset + 4 <= sizeof(c->d)) std::memcpy(&v, reinterpret_cast<const uint8_t*>(&c->d) + offset, 4);
        return v;
    }
    if (SimpleObject* s = as_simple(o)) {
        if (offset < 0x40) {
            std::memcpy(&v, reinterpret_cast<const uint8_t*>(&s->world_pos_) + offset, 4);
            return v;
        }
        if (offset < 0x6c) {
            std::memcpy(&v, reinterpret_cast<const uint8_t*>(&s->po) + (offset - 0x40), 4);
            return v;
        }
    }
    if (offset == 0x40) return uint32_t(o->kind());
    return o->raw_word(offset);
}

void object_set_word(PhysicsObject* o, uint32_t offset, uint32_t value) {
    if (BondCar* c = dynamic_cast<BondCar*>(o)) {
        if (offset + 4 <= sizeof(c->d)) {
            std::memcpy(reinterpret_cast<uint8_t*>(&c->d) + offset, &value, 4);
            return;
        }
    } else if (SimpleObject* s = as_simple(o)) {
        if (offset >= 0x40 && offset < 0x6c) {
            std::memcpy(reinterpret_cast<uint8_t*>(&s->po) + (offset - 0x40), &value, 4);
            return;
        }
    }
    if (o && o->set_raw_word(offset, value)) return;
    ++g_stats.guards;  // um dono sem a imagem do PS2 (o port não grava)
}

// ------------------------------------------------------------------------------------------------- as utilidades

// [driving] 0x001ff280 Simulation::FindPhysicsObjectSignature
// (com 0x001ff230 FindSignature: o bit 31 escolhe os corpos simples, a vaga é o byte de cima.)
PhysicsObject* FindPhysicsObjectSignature(Simulation& sim, uint32_t sig) {
    if (sig == 0) return nullptr;
    const int slot = int((sig >> 24) & 0x7f);
    if (sig & 0x80000000u) {
        if (slot < kSimpleBodySlots && sim.simple_sig[slot] == sig) return sim.simple_owners[slot];
        return nullptr;
    }
    if (slot < kRigidBodySlots && sim.rigid_sig[slot] == sig) return sim.owners[slot];
    return nullptr;
}

// [driving] 0x00201ae0 Simulation::GetPlayerObject
PhysicsObject* GetPlayerObject(Simulation& sim) {
    if (sim.cars.empty()) {
        ++g_stats.guards;  // o PS2 leria o primeiro de um vetor vazio
        return nullptr;
    }
    return sim.cars[0];
}

// O PhysicsObject::IsOwnedBy (0x00195c70; o is_owned_by do missile.cpp) para qualquer dono: o outro está na corrente
// das assinaturas +0x58 (só os de corpo simples a guardam no port; os carros e os outros rígidos dão 0).
bool PhysicsObject_IsOwnedBy(PhysicsObject* self, PhysicsObject* other) {
    if (!other) return false;
    Simulation& sim = simulation();
    const uint32_t want = PhysicsObject_GetSig(other);
    uint32_t sig = object_word(self, 0x58);
    if (sig == 0) return false;
    PhysicsObject* o = FindPhysicsObjectSignature(sim, sig);
    while (o) {
        if (PhysicsObject_GetSig(o) == want) return true;
        PhysicsObject* n = FindPhysicsObjectSignature(sim, object_word(o, 0x58));
        if (n == o) return false;
        o = n;
    }
    return false;
}

// [driving] 0x001956d8 PhysicsObject::GetRadius
float PhysicsObject_GetRadius(PhysicsObject* o) {
    Simulation& sim = simulation();
    if (!o) {
        ++g_stats.guards;
        return 0.0f;
    }
    if (SimpleObject* s = as_simple(o)) return GetSimpleRigidBody(sim, s->po.slot)->radius;
    const int slot = rigid_slot_of(sim, o);
    if (slot < 0) {
        ++g_stats.guards;
        return 0.0f;
    }
    return ps2::ffloat(GetRigidBody(sim, slot)->radius_flags);
}

// [driving] 0x00195858 PhysicsObject::GetLinearVelocity
const float* PhysicsObject_GetLinearVelocity(PhysicsObject* o) {
    static const float kZero[4] = {};
    Simulation& sim = simulation();
    if (!o) {
        ++g_stats.guards;
        return kZero;
    }
    if (SimpleObject* s = as_simple(o)) return GetSimpleRigidBody(sim, s->po.slot)->vel;
    const int slot = rigid_slot_of(sim, o);
    if (slot < 0) {
        ++g_stats.guards;
        return kZero;
    }
    return GetRigidBody(sim, slot)->vel;
}

// [driving] 0x00211098 Util_CollideRayWithSphere
bool Util_CollideRayWithSphere(float radius, const float origin[4], const float dir[4], const float centre[4],
                               float out[3]) {
    const float r2 = ee::mul(radius, radius);
    float d[3];
    aivu::sub3(d, centre, origin);
    if (ee::lt(aivu::flt(aivu::len_sq(d)), r2)) return false;  // a origem dentro da esfera
    // A direção unitária, o fim do raio a 1000 m e o segmento.
    float u[3], end[3], seg[3];
    aivu::unit(u, dir);
    aivu::scale3(end, u, 1000.0f);
    aivu::add3(end, origin, end);
    aivu::sub3(seg, end, origin);
    // O ponto do segmento mais perto do centro (a FPU).
    const float l2 = ee::add(ee::add(ee::mul(seg[0], seg[0]), ee::mul(seg[1], seg[1])), ee::mul(seg[2], seg[2]));
    const float inv = ee::eq(l2, 0.0f) ? 0.0f : ee::div(1.0f, l2);
    const float cx = ee::sub(centre[0], origin[0]), cy = ee::sub(centre[1], origin[1]),
                cz = ee::sub(centre[2], origin[2]);
    float t = ee::add(ee::add(ee::mul(cx, seg[0]), ee::mul(cy, seg[1])), ee::mul(cz, seg[2]));
    t = ee::mul(t, inv);
    t = ee::max(ee::min(t, 1.0f), 0.0f);
    float p[3];
    p[2] = ee::add(origin[2], ee::mul(t, seg[2]));
    p[0] = ee::add(origin[0], ee::mul(t, seg[0]));
    p[1] = ee::add(origin[1], ee::mul(t, seg[1]));
    float e[3];
    aivu::sub3(e, p, centre);
    if (!ee::le(aivu::flt(aivu::len_sq(e)), r2)) return false;
    copy3(out, p);
    return true;
}

// [driving] 0x00211280 Util_CollideRayWithStretchedSphere
bool Util_CollideRayWithStretchedSphere(float radius, float stretch, const float origin[4], const float dir[4],
                                        const float centre[4], float out[3]) {
    const float k = ee::div(1.0f, stretch);
    alignas(16) float u[4] = {}, o[4] = {}, c[4] = {};
    aivu::unit(u, dir);
    u[1] = ee::mul(u[1], k);
    copy3(o, origin);
    o[1] = ee::mul(o[1], k);
    copy3(c, centre);
    c[1] = ee::mul(c[1], k);
    if (!Util_CollideRayWithSphere(radius, o, u, c, out)) return false;
    out[1] = ee::mul(ee::sub(out[1], c[1]), stretch);
    out[1] = ee::add(out[1], centre[1]);
    return true;
}

// [driving] 0x002106c0 Util_GenerateMatrixUnit
// Divergência: os w das linhas 0 e 1 são o lixo da pilha no PS2 (os vopmsub/vmulq.xyz mantêm o w do registro lido da
// pilha); aqui 0. Quem a usa (o OBB do raio no CollideWithObjects) só lê x, y e z.
void Util_GenerateMatrixUnit(float out[4][4], const float dir[3]) {
    alignas(16) float m[4][4] = {};
    alignas(16) float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    m[2][0] = dir[0];
    m[2][1] = dir[1];
    m[2][2] = dir[2];
    m[2][3] = 0.0f;
    aivu::unit(m[2], m[2]);
    Reg a, b, acc, r;
    load(a, up);
    load(b, m[2]);
    load(r, m[0]);
    opmula(acc, a, b);
    opmsub(r, acc, b, a);
    store(m[0], r);
    aivu::unit(m[0], m[0]);
    load(a, m[2]);
    load(b, m[0]);
    load(r, m[1]);
    opmula(acc, a, b);
    opmsub(r, acc, b, a);
    store(m[1], r);
    m[3][0] = m[3][1] = m[3][2] = 0.0f;
    m[3][3] = 1.0f;
    std::memcpy(out, m, 64);
}

namespace {
// v' = v.x linha 0 + v.y linha 1 + v.z linha 2 (vmulax.xyz, vmadday.xyz, vmaddz.xyz).
void rotate3(const float m[4][4], const float in[3], float out[3]) {
    Reg v, r0, r1, r2, acc, d = {};
    load(v, in, XYZ);
    load(r0, m[0]);
    load(r1, m[1]);
    load(r2, m[2]);
    mulabc(acc, r0, v, BX, XYZ);
    maddabc(acc, r1, v, BY, XYZ);
    maddbc(d, acc, r2, v, BZ, XYZ);
    for (int i = 0; i < 3; ++i) out[i] = ps2::ffloat(d[i]);
}
}  // namespace

// [driving] 0x00210b60 Util_PerturbVector(float, float, COORD3 &)
void Util_PerturbVector(float yaw, float pitch, float out[3], const float in[3]) {
    alignas(16) float m[4][4];
    float v[3];
    VU0_MATRIX4setyrot(m, yaw);
    rotate3(m, in, v);
    VU0_MATRIX4setzrot(m, pitch);
    rotate3(m, v, v);
    copy3(out, v);
}

// [driving] 0x00210c10 Util_PerturbVector(COORD3 &, float)
void Util_PerturbVectorGauss(float amount, float out[3], const float in[3]) {
    const float a = ee::mul(Util_GaussRandom(), amount);
    const float b = ee::mul(Util_GaussRandom(), amount);
    Util_PerturbVector(a, b, out, in);
}

// [driving] 0x002a8bd0 rand
int32_t ee_rand() {
    g_gauss.rand_next = g_gauss.rand_next * 1103515245u + 12345u;
    return int32_t(g_gauss.rand_next & 0x7fffffffu);
}

namespace {
// As palavras de uma dupla (o EXTRACT_WORDS da fdlibm).
uint32_t high_word(double x) {
    uint64_t b;
    std::memcpy(&b, &x, 8);
    return uint32_t(b >> 32);
}
uint32_t low_word(double x) {
    uint64_t b;
    std::memcpy(&b, &x, 8);
    return uint32_t(b);
}
double with_high(double x, uint32_t hi) {
    uint64_t b;
    std::memcpy(&b, &x, 8);
    b = (b & 0xffffffffull) | (uint64_t(hi) << 32);
    std::memcpy(&x, &b, 8);
    return x;
}
double dbits(uint64_t b) {
    double x;
    std::memcpy(&x, &b, 8);
    return x;
}
}  // namespace

// [driving] 0x002c1330 __ieee754_log
// A dupla do PS2 é a da libgcc em software (dpadd, dpmul, dpdiv: o arredondamento ao par mais perto da IEEE), então o
// algoritmo em dupla do hospedeiro dá os mesmos bits. As constantes são as do .rodata (as da fdlibm).
double ee_log(double x) {
    const double ln2_hi = dbits(0x3fe62e42fee00000ull), ln2_lo = dbits(0x3dea39ef35793c76ull);
    const double two54 = dbits(0x4350000000000000ull);
    const double Lg1 = dbits(0x3fe5555555555593ull), Lg2 = dbits(0x3fd999999997fa04ull),
                 Lg3 = dbits(0x3fd2492494229359ull), Lg4 = dbits(0x3fcc71c51d8e78afull),
                 Lg5 = dbits(0x3fc7466496cb03deull), Lg6 = dbits(0x3fc39a09d078c69full),
                 Lg7 = dbits(0x3fc2f112df3e5244ull);
    int32_t hx = int32_t(high_word(x));
    const uint32_t lx = low_word(x);
    int32_t k = 0;
    if (hx < 0x00100000) {
        volatile double zero = 0.0;  // (a divisão em tempo de execução, como a fdlibm)
        if (((hx & 0x7fffffff) | int32_t(lx)) == 0) return -two54 / zero;  // log(+-0) = -inf
        if (hx < 0) return (x - x) / zero;                                  // log(-x) = NaN
        k -= 54;
        x *= two54;
        hx = int32_t(high_word(x));
    }
    if (hx >= 0x7ff00000) return x + x;
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    const int32_t i0 = (hx + 0x95f64) & 0x100000;
    x = with_high(x, uint32_t(hx | (i0 ^ 0x3ff00000)));
    k += i0 >> 20;
    const double f = x - 1.0;
    if ((0x000fffff & (2 + hx)) < 3) {
        if (f == 0.0) {
            if (k == 0) return 0.0;
            const double dk = double(k);
            return dk * ln2_hi + dk * ln2_lo;
        }
        const double R = f * f * (0.5 - 0.33333333333333333 * f);
        if (k == 0) return f - R;
        const double dk = double(k);
        return dk * ln2_hi - ((R - dk * ln2_lo) - f);
    }
    const double s = f / (2.0 + f);
    const double dk = double(k);
    const double z = s * s;
    int32_t i = hx - 0x6147a;
    const double w = z * z;
    const int32_t j = 0x6b851 - hx;
    const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    const double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i |= j;
    const double R = t2 + t1;
    if (i > 0) {
        const double hfsq = 0.5 * f * f;
        if (k == 0) return f - (hfsq - s * (hfsq + R));
        return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
    }
    if (k == 0) return f - s * (f - R);
    return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
}

// [driving] 0x00210a30 Util_GaussRandom
float Util_GaussRandom() {
    if (g_gauss.have != 0) {
        g_gauss.have = 0;
        return g_gauss.saved;
    }
    const float k = ps2::ffloat(0x30000000u);  // 2^-31
    float a, b, s;
    do {
        float r = ee::mul(ee::cvt_s(ee_rand()), k);
        a = ee::sub(ee::add(r, r), 1.0f);
        r = ee::mul(ee::cvt_s(ee_rand()), k);
        b = ee::sub(ee::add(r, r), 1.0f);
        s = ee::add(ee::mul(a, a), ee::mul(b, b));
    } while (ee::le(1.0f, s));
    // log(s) em dupla (fptodp exato, dptofp ao par mais perto), depois a FPU: -2 log(s) / s e a raiz.
    const float l = float(ee_log(double(s)));
    const float q = ee::sqrt(ee::div(ee::mul(l, -2.0f), s));
    g_gauss.saved = ee::mul(b, q);
    g_gauss.have = 1;
    return ee::mul(a, q);
}

// [driving] 0x00208428 SMissionManager::GetEnemyAccuracyBoost
int32_t SMissionManager_GetEnemyAccuracyBoost() {
    if (!mission_exists()) {
        ++g_stats.guards;  // o PS2 leria o gerente nulo
        return 0;
    }
    const SMissionManagerData& m = mission_manager();
    const uint8_t* s = nf::p32_ptr<const uint8_t>(m.settings);
    if (!s) {
        ++g_stats.guards;
        return 0;
    }
    uint32_t off = 0x2ac;
    if (m.difficulty == 1) off = 0x2b0;
    else if (m.difficulty == 2) off = 0x2b4;
    int32_t v;
    std::memcpy(&v, s + off, 4);
    return v;
}

// ------------------------------------------------------------------------------------------------- os globais

// [driving] 0x0019ae98 global_constructors_keyed_to_gEnableRayShells
// (com 0x0019ada0, que zera a assinatura de cada clarão e de cada raio dos gatilhos)
void RayShell_StaticInit() {
    for (RayFlash& f : g_ray.flashes) f.sig = 0;
    for (ActiveRayShell& a : g_ray.active) a.sig = 0;
}

// [driving] 0x00199230 RayShell::GetNumActiveRayShells
int32_t RayShell_GetNumActiveRayShells() { return g_ray.num_active; }

// [driving] 0x00199240 RayShell::GetActiveRayShell
ActiveRayShell* RayShell_GetActiveRayShell(int32_t i) {
    if (i < 0 || i >= 64) {
        ++g_stats.guards;  // fora do vetor (o PS2 leria a memória vizinha)
        return nullptr;
    }
    return &g_ray.active[i];
}

// [driving] 0x00199258 RayShell::ClearActiveRayShells
void RayShell_ClearActiveRayShells() { g_ray.num_active = 0; }

// [driving] 0x00199268 RayShell::GetNumTracers
void RayShell_GetNumTracers(int32_t index) { g_ray.trigger_index = index; }

// [driving] 0x00199278 RayShell::GetTracer
ActiveRayShell* RayShell_GetTracer() {
    if (g_ray.trigger_index == -1) return nullptr;
    return RayShell_GetActiveRayShell(g_ray.trigger_index);
}

// [driving] 0x001992d8 RayShell::DeleteTracer
void RayShell_DeleteTracer(int32_t i) {
    const int32_t n = g_ray.num_tracers;
    if (i < n - 1) {
        if (i < 0 || n - 1 >= 64) {
            ++g_stats.guards;
        } else {
            g_ray.tracers[i] = g_ray.tracers[n - 1];
            g_ray.tracer_side[i][0] = g_ray.tracer_side[n - 1][0];
            g_ray.tracer_side[i][1] = g_ray.tracer_side[n - 1][1];
        }
    }
    g_ray.num_tracers = n - 1;
}

// [driving] 0x00199358 RayShell::ClearTracers
void RayShell_ClearTracers(uint32_t step) {
    for (int32_t i = 0; i < g_ray.num_tracers; ++i) {
        if (i >= 64) {
            ++g_stats.guards;
            break;
        }
        if (uint32_t(g_ray.tracers[i].step) < step) {
            RayShell_DeleteTracer(i);
            --i;
        }
    }
}

// [driving] 0x00199668 RayShell::ClearFlashes
void RayShell_ClearFlashes() { g_ray.num_flashes = 0; }

// [driving] 0x00199780 RayShell::Reset
void RayShell_Reset() {
    RayShell_ClearTracers(uint32_t(simulation().steps + 1));
    RayShell_ClearActiveRayShells();
    RayShell_ClearFlashes();
    for (int i = 63; i >= 0; --i) g_ray.tracer_side[i][0] = 0;
    g_ray.last_explosion = 0;
}

// [driving] 0x0019a9b8 RayShell::StaticStoreTracer
void RayShell_StaticStoreTracer(const float from[3], const float to[3], int32_t type, uint32_t scene, uint32_t effect) {
    if (type < 0 || g_ray.num_tracers >= 64) return;
    const int32_t n = g_ray.num_tracers;
    RayTracer& t = g_ray.tracers[n];
    copy3(t.from, from);
    copy3(t.to, to);
    g_ray.tracer_side[n][0] = scene;
    g_ray.tracer_side[n][1] = effect;
    t.step = simulation().steps;
    t.type = type;
    if (type == 0) {
        // O rastro comum começa 2 m adiante do cano (se a bala andou mais de 2 m): a direção pela FPU e a VU.
        float d[3];
        aivu::sub3(d, from, to);
        if (ee::lt(4.0f, aivu::flt(aivu::len_sq(d)))) {
            float v[3] = {ee::sub(to[0], from[0]), ee::sub(to[1], from[1]), ee::sub(to[2], from[2])};
            aivu::unit(v, v);
            aivu::scale3(v, v, 2.0f);
            aivu::add3(t.from, t.from, v);
        }
    }
    g_ray.num_tracers = n + 1;
    ++g_stats.tracers;
}

// [driving] 0x0019a988 RayShell::StoreTracer
void RayShell_StoreTracer(const RayShellData& r, const float from[3], const float to[3]) {
    RayShell_StaticStoreTracer(from, to, r.streak, r.scene, r.effect);
}

// [driving] 0x0019abb8 RayShell::AddFlash
void RayShell_AddFlash(uint32_t sig, const float pos[3]) {
    if (g_ray.num_flashes >= 8) return;
    Simulation& sim = simulation();
    PhysicsObject* o = FindPhysicsObjectSignature(sim, sig);
    if (!o || o == GetPlayerObject(sim)) return;
    const int32_t k = o->kind();
    if (k != 4 && k != 0xb) return;
    for (int32_t i = 0; i < g_ray.num_flashes; ++i)
        if (g_ray.flashes[i].sig == sig) return;
    RayFlash& f = g_ray.flashes[g_ray.num_flashes];
    f.sig = sig;
    copy3(f.pos, pos);
    ++g_ray.num_flashes;
    ++g_stats.flashes;
}

// [driving] 0x001997e0 RayShell::AddNewRayShellForTriggerCheck
void RayShell_AddNewRayShellForTriggerCheck(const RayShellData& r, float dist) {
    const int32_t n = g_ray.num_active;
    if (!(n + 1 < 0x40)) {
        ++g_stats.too_many;
        effects_note(0x002a8148);  // printf("TOO MANY RAY SHELLS")
        return;
    }
    ActiveRayShell& a = g_ray.active[n];
    a.sig = r.sig;
    copy3(a.start, r.pos);
    float v[3];
    aivu::scale3(v, r.dir, dist);
    aivu::add3(a.end, r.pos, v);
    a.half = ee::mul(dist, 0.5f);
    g_ray.num_active = n + 1;
    a.damage = r.damage;
    ++g_stats.active;
}

// ------------------------------------------------------------------------------------------------- o RayShell

// [driving] 0x0019a548 RayShell::ConsiderVictim
void RayShell_ConsiderVictim(RayShellData& r, float dist, uint32_t sig, const float hit[3], const float victim[3]) {
    if (r.state == 0 || ee::lt(dist, r.dist1)) {
        // O mais perto (o que já estava não passa para o segundo: o PS2 o sobrescreve).
        copy3(r.hit1, hit);
        copy3(r.victim1, victim);
        r.sig1 = sig;
        r.dist1 = dist;
        if (r.state == 0) r.state = 1;
        return;
    }
    Simulation& sim = simulation();
    PhysicsObject* o = FindPhysicsObjectSignature(sim, sig);
    PhysicsObject* first = FindPhysicsObjectSignature(sim, r.sig1);
    uint32_t take = 0;
    if (kind_or_guard(o) == 4) {
        take = object_word(o, 0x8c);  // o personagem atravessável (o +0x8c do Human)
    } else if (ee::lt(dist, r.dist1)) {
        if (ee::lt(ee::sub(r.dist1, PhysicsObject_GetRadius(first)), dist)) take = 1;
    }
    if (take == 0) return;
    if (r.state != 1 && !ee::lt(dist, r.dist2)) return;
    copy3(r.hit2, hit);
    copy3(r.victim2, victim);
    r.sig2 = sig;
    r.dist2 = dist;
    if (r.state == 1) r.state = 2;
}

// [driving] 0x001998c0 RayShell::CollideWithObjects
bool RayShell_CollideWithObjects(RayShellData& r) {
    Simulation& sim = simulation();
    for (int i = 0; i < 0x90; ++i) {
        PhysicsObject* o = GetOrderedBody(sim, i);
        if (!o) continue;
        if (owned_by(o, FindPhysicsObjectSignature(sim, r.sig))) continue;
        SimpleObject* so = as_simple(o);
        if (so && (GetSimpleRigidBody(sim, so->po.slot)->flags & kSrbNoCollide)) continue;
        float radius = PhysicsObject_GetRadius(o);
        float y_off = 0.0f, stretch = 1.0f;
        bool stretched = false;
        alignas(16) float centre[4] = {};  // sp+0x10 (o w fica: lixo da pilha)
        copy3(centre, PhysicsObject_GetPosition(o));
        if (o->kind() == 4) {
            // Um personagem: só os tiros do jogador; o centro pela matriz do ator (+0x70: as linhas e o ponto +0x40)
            // e a esfera esticada (+0xa4: o raio +0xc, o deslocamento em y +0x10, o esticamento +0x14).
            if (FindPhysicsObjectSignature(sim, r.sig) != GetPlayerObject(sim)) continue;
            const uint32_t actor = object_word(o, 0x70);
            if (actor != 0) {
                const float* a = nf::p32_ptr<const float>(actor);
                Reg v, r0, r1, r2, r3, acc, d;
                load(v, a + 16);
                load(r0, a);
                load(r1, a + 4);
                load(r2, a + 8);
                load(r3, a + 12);
                load(d, centre);
                const Reg vf0 = {0, 0, 0, kOne};
                mulabc(acc, r0, v, BX, XYZ);
                maddabc(acc, r1, v, BY, XYZ);
                maddabc(acc, r2, v, BZ, XYZ);
                maddbc(d, acc, r3, vf0, BW, XYZ);
                store(centre, d);
                stretched = true;
                uint32_t shape;
                std::memcpy(&shape, reinterpret_cast<const uint8_t*>(a) + 0xa4, 4);
                if (shape == 0) {
                    radius = ps2::ffloat(0x3eb33333u);  // 0,35
                    stretch = 2.5f;
                } else {
                    const float* sh = nf::p32_ptr<const float>(shape);
                    stretch = sh[5];   // +0x14
                    radius = sh[3];    // +0xc
                    y_off = sh[4];     // +0x10
                }
            }
        }
        alignas(16) float hit[4] = {};  // sp+0x30
        bool got;
        if (stretched) {
            centre[1] = ee::add(centre[1], y_off);
            got = Util_CollideRayWithStretchedSphere(radius, stretch, r.pos, r.dir, centre, hit);
        } else {
            got = Util_CollideRayWithSphere(radius, r.pos, r.dir, centre, hit);
        }
        if (!got) continue;
        alignas(16) float victim[4] = {hit[0], hit[1], hit[2], 0.0f};  // sp+0x40
        bool take = false;
        if (so) {
            // Um corpo simples: o lugar fica no raio do corpo a partir do ponto, para trás.
            float back[3];
            aivu::sub3(back, r.pos, hit);
            aivu::unit(back, back);
            aivu::scale3(back, back, radius);
            aivu::add3(victim, hit, back);
            take = true;
        } else {
            // Um corpo rígido: a caixa do raio (0,01 x 0,01 x 250 no ponto) contra a do corpo; o lugar é onde o segmento
            // do começo do raio ao ponto entra na caixa do corpo (do pé do corpo: o centro menos a linha 1 x a meia
            // altura).
            const int slot = rigid_slot_of(sim, o);
            const RigidBody* rb = GetRigidBody(sim, slot < 0 ? 0 : slot);
            if (slot < 0) ++g_stats.guards;
            const RigidBodyInfo* info = rb->info;
            alignas(16) float m[4][4];
            std::memcpy(m, info->orient, 64);
            alignas(16) float ray_m[4][4];
            Util_GenerateMatrixUnit(ray_m, r.dir);
            alignas(16) float ray_c[4] = {hit[0], hit[1], hit[2], 1.0f};
            alignas(16) const float ray_h[4] = {0.01f, 0.01f, 250.0f, 1.0f};  // 0x37bf98
            alignas(16) float body_c[4] = {rb->pos[0], rb->pos[1], rb->pos[2], 1.0f};
            alignas(16) float half[4];
            std::memcpy(half, info->half_size, 16);
            OBB body_box{}, ray_box{};
            OBB_Reset(body_box, m, body_c, half);
            OBB_Reset(ray_box, ray_m, ray_c, ray_h);
            if (OBB_CheckOBBOverlap(ray_box, body_box)) {
                take = true;
                alignas(16) float foot[4] = {body_c[0], body_c[1], body_c[2], 0.0f};
                float d[3];
                aivu::scale3(d, info->orient[1], ee::neg(half[1]));
                aivu::add3(foot, foot, d);
                m[3][0] = foot[0];
                m[3][1] = foot[1];
                m[3][2] = foot[2];
                m[3][3] = 1.0f;
                alignas(16) float seg[2][4] = {{r.pos[0], r.pos[1], r.pos[2], 1.0f}, {hit[0], hit[1], hit[2], 1.0f}};
                alignas(16) float out[4] = {};
                if (WCollisionMgr_GetOBBObjectIntersection(&seg[0][0], &m[0][0], half, out)) copy3(victim, out);
            }
        }
        if (!take) continue;
        float d[3];
        aivu::sub3(d, hit, r.pos);
        const float dist = aivu::len(d);
        RayShell_ConsiderVictim(r, dist, PhysicsObject_GetSig(o), hit, victim);
    }
    return 0 < r.state;
}

// [driving] 0x0019a710 RayShell::DamagePhysicsObject
void RayShell_DamagePhysicsObject(RayShellData& r, uint32_t sig, const float hit[3], const float victim[3]) {
    Simulation& sim = simulation();
    PhysicsObject* o = FindPhysicsObjectSignature(sim, sig);
    if (!o) return;
    // O ApplyDamage do dono (a entrada +0x1c: 1/4 de divisão, o tipo 2, a assinatura de quem atirou).
    ++g_stats.damage_calls;
    effects_note(0x00189598, ps2::fbits(r.damage), uint32_t(object_ordered_index(sim, o)), 2);
    const bool saved = damage_source_is_player();
    damage_source_is_player() = owned_by(FindPhysicsObjectSignature(sim, r.sig), GetPlayerObject(sim));
    const bool at_player = o == GetPlayerObject(sim) && !damage_source_is_player();
    const float hp_before = at_player ? o->hit_points() : 0.0f;  // (só a conta do port: os tiros no jogador)
    const int32_t flags = o->apply_damage(r.pos, hit, r.damage, 0.25f, 2);
    if (at_player) {
        ++g_stats.player_hits;
        g_stats.player_damage += double(hp_before) - double(o->hit_points());
    }
    damage_source_is_player() = saved;
    alignas(16) const float normal[4] = {r.dir[0], r.dir[1], r.dir[2], 1.0f};
    const uint8_t surface[2] = {0, 0};
    alignas(16) const float at[4] = {victim[0], victim[1], victim[2], 0.0f};
    Shell_ApplyWorldEffects(at, normal, 1, r.weapon, surface, flags, o);
    RayShell_StoreTracer(r, r.muzzle, hit);
    RayShell_AddFlash(r.sig, r.pos);
    cheat_explosion(r.sig, hit);
    if (FindPhysicsObjectSignature(sim, r.sig) == GetPlayerObject(sim) && o->kind() == 1) {
        // AddDamageByPlayer (+0x1ac)
        o->add_damage_by_player(r.damage);
    }
}

// [driving] 0x00199f88 RayShell::CollideWithWorld
bool RayShell_CollideWithWorld(RayShellData& r, float& dist) {
    WWorldPos wp{};  // 0x00232180 WWorldPos::WWorldPos (o ScorchWorld a recebe)
    alignas(16) float dir4[4] = {r.dir[0], r.dir[1], r.dir[2], 1.0f};
    float total = r.range;
    if (0 < r.state && ee::lt(r.dist1, r.range)) total = r.dist1;
    float step = ee::mul(total, 0.25f);
    if (ee::lt(55.0f, step)) step = 55.0f;
    float t = 0.0f;
    bool hit = false;
    alignas(16) float seg[2][4] = {{r.pos[0], r.pos[1], r.pos[2], 1.0f}, {}};
    alignas(16) float delta[4];
    for (int i = 0; i < 4; ++i) delta[i] = ee::mul(dir4[i], step);  // vmulx.xyzw
    delta[3] = 1.0f;
    if (!ee::lt(t, total)) return false;
    for (;;) {
        for (int i = 0; i < 4; ++i) seg[1][i] = ee::add(seg[0][i], delta[i]);  // vadd.xyzw
        WorldCollisionInfo info;
        WorldCollisionInfo_Init(info);
        ++g_stats.rays;
        if (WCollisionMgr_CheckHitWorld(&seg[0][0], info)) {
            WCollisionMgr_CheckHitWindow(info, true, 0);
            float d[3];
            aivu::sub3(d, info.point, r.pos);
            const float dd = aivu::len(d);
            bool take = true;
            if (r.state == 0) dist = dd;
            else if (ee::lt(dd, dist)) dist = dd;
            else take = false;
            if (take) {
                const float size = r.weapon == 0x1d ? ps2::ffloat(0x3ea00000u) : ps2::ffloat(0x3d800000u);
                Shell_ApplyWorldEffects(info.point, info.normal, 1, r.weapon, &info.face_type, 0, nullptr);
                hit = true;
                ++g_stats.world_hits;
                Shell_ApplyWorldDamage(r.damage, size, wp, 2, info);
                RayShell_StoreTracer(r, r.muzzle, info.point);
                RayShell_AddFlash(r.sig, r.pos);
                cheat_explosion(r.sig, info.point);
            }
        }
        std::memcpy(seg[0], seg[1], 16);
        if (hit) break;
        t = ee::add(t, step);
        if (!ee::lt(t, total)) break;
    }
    return hit;
}

// [driving] 0x0019a3e0 RayShell::Fire
void RayShell_Fire(RayShellData& r, float& dist) {
    Simulation& sim = simulation();
    if (r.state == 0) {
        // Ninguém: o rastro do cano até o alcance e o clarão.
        float end[3];
        aivu::scale3(end, r.dir, r.range);
        aivu::add3(end, r.muzzle, end);
        RayShell_StoreTracer(r, r.muzzle, end);
        RayShell_AddFlash(r.sig, r.pos);
        dist = r.range;
        return;
    }
    if (r.state == 2) {
        // Dois atingidos: atrás de um carro ou de um helicóptero o segundo leva o tiro (no lugar do primeiro).
        const int32_t k = kind_or_guard(FindPhysicsObjectSignature(sim, r.sig1));
        if (k == 1 || k == 10) {
            ++g_stats.fired;
            RayShell_DamagePhysicsObject(r, r.sig2, r.hit2, r.victim1);
            dist = r.dist2;
            return;
        }
    } else if (r.state != 1) {
        return;
    }
    ++g_stats.fired;
    RayShell_DamagePhysicsObject(r, r.sig1, r.hit1, r.victim1);
    dist = r.dist1;
}

// [driving] 0x00199200 RayShell::~RayShell
void RayShell_Destruct(RayShellData& r) { r.vtable = 0x0037bfb8; }

// [driving] 0x00198e80 RayShell::RayShell
void RayShell_Construct(RayShellData& r, float range, float damage, float perturb, int32_t weapon, uint32_t sig,
                        const float pos[3], const float dir[3], const float muzzle[3], uint32_t scene, uint32_t effect,
                        int32_t rays, int32_t streak) {
    Simulation& sim = simulation();
    ++g_stats.rayshells;
    r.vtable = 0x0037bfb8;
    copy3(r.pos, pos);
    r.damage = damage;
    r.range = ee::lt(range, 220.0f) ? range : 220.0f;
    copy3(r.muzzle, muzzle);
    r.weapon = weapon;
    r.sig1 = 0;
    r.sig2 = 0;
    r.streak = streak;
    r.scene = scene;
    r.effect = effect;
    r.sig = sig;
    if (owned_by(FindPhysicsObjectSignature(sim, sig), GetPlayerObject(sim)) && cheat_explosive())
        r.damage = ee::add(r.damage, r.damage);
    float dist = 0.0f;       // sp+0x20
    bool any = false;        // s3: o primeiro raio achou alguém
    bool near_world = false; // s4: o mundo a menos de 20 m no primeiro
    for (int32_t i = 0; i < rays;) {
        r.state = 0;
        if (i == 0) {
            alignas(16) float d[4];
            std::memcpy(d, r.dir, 16);
            aivu::unit(d, dir);
            std::memcpy(r.dir, d, 12);
            copy3(r.base_dir, r.dir);
            copy3(r.origin, r.pos);
            dist = 0.0f;
            RayShell_CollideWithObjects(r);
            if (r.state != 0) any = true;
        } else {
            // Os outros raios: a direção perturbada, o começo 4 m antes da distância do anterior, o alcance 3 m.
            float p[3];
            Util_PerturbVectorGauss(perturb, p, r.base_dir);
            copy3(r.dir, p);
            float back[3];
            aivu::scale3(back, r.dir, ee::sub(dist, 4.0f));
            aivu::add3(r.pos, r.origin, back);
            r.range = 3.0f;
            dist = 0.0f;
            if (any) RayShell_CollideWithObjects(r);
        }
        if (0 < r.state) {
            PhysicsObject* o = FindPhysicsObjectSignature(sim, r.sig1);
            dist = ee::sub(r.dist1, PhysicsObject_GetRadius(o));
        } else {
            dist = 150.0f;
        }
        bool world = false;
        if (i == 0) {
            world = RayShell_CollideWithWorld(r, dist);
            if (world && ee::lt(dist, 20.0f)) near_world = true;
        } else if (near_world) {
            world = RayShell_CollideWithWorld(r, dist);
        }
        if (!world) RayShell_Fire(r, dist);
        if (i == 0) RayShell_AddNewRayShellForTriggerCheck(r, dist);
        ++i;
        if (r.state == 0 && !world) break;
    }
    RayShell_Destruct(r);
    g_last = r;
}

}  // namespace drv
