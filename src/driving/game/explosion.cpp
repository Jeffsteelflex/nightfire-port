// As explosões (driving/game/explosion.h), do MIPS do DRIVING.ELF (Noise.obj, Simulation.obj, DTuningFile.obj).

#include "driving/game/explosion.h"

#include <cstring>

#include "driving/ai/sim_random.h"
#include "driving/game/bond_car.h"
#include "driving/game/events.h"
#include "driving/game/feedback.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/smackable.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "driving/render/vehicle.h"
#include "driving/sound/audio_internal.h"
#include "driving/carp_runtime.h"
#include "driving/fx/fx_events.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {

EffectsStats g_stats;
std::vector<EffectsCall> g_trace;
EffectsEnv g_env;

// Os nomes dos efeitos de explosão (o .data 0x32bbd0: os três "Ex_SmallD" pelo passo; 0x32bbe0 os do jogador e 0x32bbf0
// os outros, pelo contador 0x32bbdc).
const char* const kExSmallD[3] = {"Ex_SmallD", "Ex_SmallD2", "Ex_SmallD3"};
const char* const kExPlayer[3] = {"Ex_SmB00", "Ex_SmB01", "Ex_SmB02"};
const char* const kExOther[3] = {"Ex_Small", "Ex_Small2", "Ex_Small3"};
EffectsGlobals g_globals;  // o contador 0x32bbdc, o anel 0x32bbfc e os prazos 0x32bc00
uint32_t& g_ex_counter = g_globals.ex_counter;
// Os sons (0x32bc10 pela vaga do anel; o anel 0x32bbfc e os prazos 0x32bc00 em ticks de 100 Hz).
const char* const kExpSounds[4] = {"SFX_HugeExp", "SFX_LrgExp", "SFX_MedExp", "SFX_SmallExp"};
uint32_t& g_ring = g_globals.ring;
int32_t (&g_ring_time)[4] = g_globals.ring_time;
bool g_trace_on = false;

const uint32_t kVf0[4] = {0, 0, 0, 0x3f800000};

uint32_t vu_len2(const float v[3]) {
    const uint32_t x = ps2::fbits(v[0]), y = ps2::fbits(v[1]), z = ps2::fbits(v[2]);
    uint32_t acc = ps2::mul(x, x);
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(y, y));
    return ps2::madd(acc, 0x3f800000u, ps2::mul(z, z));
}

// O dono de uma vaga de corpo rígido (-1: não é um dono rígido).
int rigid_slot_of(const Simulation& sim, const PhysicsObject* o) {
    for (int i = 0; i < kRigidBodySlots; ++i)
        if (sim.owners[i] == o) return i;
    return -1;
}

SimpleObject* as_simple(PhysicsObject* o) { return dynamic_cast<SimpleObject*>(o); }

// O produto vetorial normalizado do destrutor do ESpawnExplosion (vopmula/vopmsub, depois o comprimento com o w = 1
// somado: o vf3 começa em (0, 0, 0, 1), então a norma é a de (x, y, z, 1); a pista w fica 1).
void cross_norm(const float a[4], const float b[4], float out[4]) {
    Reg v1, v2, v3, v4, v5, acc;
    load(v3, kVf0);
    mul(v3, kVf0, kVf0, XYZW);
    load(v1, a);
    load(v2, b);
    opmula(acc, v1, v2);
    opmsub(v3, acc, v2, v1);
    const uint32_t one[4] = {0x3f800000, 0, 0, 0};
    std::memcpy(v5, one, 16);
    mul(v4, v3, v3, XYZW);
    mulabc(acc, v3, v3, BX, X);
    maddabc(acc, v5, v4, BY, X);
    maddabc(acc, v5, v4, BZ, X);
    maddbc(v4, acc, v5, v4, BW, X);
    const uint32_t q = ps2::rsqrt(0x3f800000u, v4[0]);
    muls(v3, v3, q, XYZ);
    store(out, v3);
}

// O produto de quatérnios inline do destrutor (0x0015a228): q = a ⊗ b com a convenção do EA (x, y, z, w).
void quat_mul(const float a[4], const float b[4], float out[4]) {
    Reg v1, v2, v3, v4, acc;
    load(v1, a);
    load(v2, b);
    mul(v3, v1, v2, XYZW);
    subbc(v3, v3, v3, BX, W);
    subbc(v3, v3, v3, BY, W);
    subbc(v3, v3, v3, BZ, W);
    opmula(acc, v1, v2);
    opmsub(v3, acc, v2, v1);
    mulbc(v4, v1, v2, BW, XYZ);
    add(v3, v3, v4, X);
    add(v3, v3, v4, Y);
    add(v3, v3, v4, Z);
    mulbc(v4, v2, v1, BW, XYZ);
    add(v3, v3, v4, X);
    add(v3, v3, v4, Y);
    add(v3, v3, v4, Z);
    store(out, v3);
}

// TransformPoint (0x002d1920): v' = v.x m0 + v.y m1 + v.z m2 + v.w m3 (xyzw).
void transform_point(const float m[4][4], const float v[4], float out[4]) {
    Reg vv, m0, m1, m2, m3, acc, r;
    load(vv, v);
    load(m0, m[0]);
    load(m1, m[1]);
    load(m2, m[2]);
    load(m3, m[3]);
    mulabc(acc, m0, vv, BX, XYZW);
    maddabc(acc, m1, vv, BY, XYZW);
    maddabc(acc, m2, vv, BZ, XYZW);
    maddbc(r, acc, m3, vv, BW, XYZW);
    store(out, r);
}

// ALimitedSound (0x003c4c58; 0x002f4f10 Play, 0x002f53e8 o destrutor): um AOneShotSound cujo volume é 1 / os vivos
// (fgCount 0x355ec0, que o criador soma).
class ALimitedSound : public AOneShotSound {
public:
    static int32_t& count;
    ALimitedSound(const char* mix_name, int32_t bank, int32_t patch, float vol)
        : AOneShotSound(mix_name, bank, patch, vol) {}
    ~ALimitedSound() override { --count; }
    // [driving] 0x002f4f10 ALimitedSound::Play
    void Play(APath& path) override {
        volume = ee::div(1.0f, ee::cvt_s(count));
        AOneShotSound::Play(path);
    }
};
int32_t& ALimitedSound::count = g_globals.limited_count;

BondCar* player_car(Simulation& sim) { return sim.cars.empty() ? nullptr : sim.cars[0]; }

// O atributo +0xbc do carro do jogador (a entrada +0x1d4 da tabela virtual, GetPhysics: o carro debaixo d'água).
bool player_underwater(Simulation& sim) {
    BondCar* p = player_car(sim);
    if (!p || !p->physics()) {
        ++g_stats.guards;
        return false;
    }
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t*>(p->physics()) + 0xbc, 4);
    return v != 0;
}

// strcasecmp == 0
bool same_nocase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return false;
        if (!ca) return true;
    }
}

}  // namespace

EffectsStats& effects_stats() { return g_stats; }
std::vector<EffectsCall>& effects_trace() { return g_trace; }
EffectsEnv& effects_env() { return g_env; }
EffectsGlobals& effects_globals() { return g_globals; }
bool& effects_trace_enabled() { return g_trace_on; }

void effects_note(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    if (g_trace_on) g_trace.push_back({fn, {a0, a1, a2, a3, a4, a5}});
}

void effects_reset() {
    g_stats = EffectsStats{};
    g_trace.clear();
    g_ex_counter = 0;
    g_ring = 0;
    for (int32_t& t : g_ring_time) t = 0;
}

SimpleRigidBody* SimpleObject::body() const {
    if (po.slot < 0 || po.slot >= kSimpleBodySlots) return nullptr;
    return GetSimpleRigidBody(simulation(), po.slot);
}

// ------------------------------------------------------------------------------- as funções do PhysicsObject do PS2

// [driving] 0x00195808 PhysicsObject::GetPosition
const float* PhysicsObject_GetPosition(PhysicsObject* o) {
    Simulation& sim = simulation();
    if (SimpleObject* s = as_simple(o)) return GetSimpleRigidBody(sim, s->po.slot)->pos;
    const int slot = rigid_slot_of(sim, o);
    if (slot < 0) {
        ++g_stats.guards;
        static const float zero[4] = {};
        return zero;
    }
    return GetRigidBody(sim, slot)->pos;
}

// [driving] 0x00195688 PhysicsObject::GetMass
float PhysicsObject_GetMass(PhysicsObject* o) {
    Simulation& sim = simulation();
    if (SimpleObject* s = as_simple(o)) return GetSimpleRigidBody(sim, s->po.slot)->mass;
    const int slot = rigid_slot_of(sim, o);
    if (slot < 0) {
        ++g_stats.guards;
        return 0.0f;
    }
    return GetRigidBody(sim, slot)->mass;
}

// [driving] 0x00195b58 PhysicsObject::GetCollisionBounds: falso para um corpo simples com o bit 0 (sem colisão);
// senão as meias-medidas do RSceneObj do dono (+0x48), falso sem ele. Port: o RVehicle do carro (o BaseDesc dele) e o
// RSceneObj do Smackable; os objetos de corpo simples respondem por si.
bool PhysicsObject_GetCollisionBounds(PhysicsObject* o, float half[4]) {
    if (SimpleObject* s = as_simple(o)) {
        SimpleRigidBody* b = GetSimpleRigidBody(simulation(), s->po.slot);
        if (b->flags & kSrbNoCollide) return false;
        return s->collision_bounds(half);
    }
    if (BondCar* c = dynamic_cast<BondCar*>(o)) {
        if (!c->vehicle || !c->vehicle->d.base_desc) return false;
        const CarpBaseDesc& d = *nf::p32_ptr<const CarpBaseDesc>(c->vehicle->d.base_desc);
        for (int i = 0; i < 3; ++i) half[i] = ee::mul(ee::sub(d.bbox_max[i], d.bbox_min[i]), 0.5f);
        return true;
    }
    if (Smackable* m = dynamic_cast<Smackable*>(o)) {
        RSceneObj* so = m->scene();
        if (!so) return false;
        RSceneObj_GetBoundingDimensions(*so, half);
        return true;
    }
    return false;
}

// [driving] 0x00195728 PhysicsObject::ApplyForces
void PhysicsObject_ApplyForces(PhysicsObject* o, const float force[4], const float torque[4]) {
    Simulation& sim = simulation();
    ++g_stats.forces;
    if (SimpleObject* s = as_simple(o)) {
        SimpleRigidBody* b = GetSimpleRigidBody(sim, s->po.slot);
        if (!(b->flags & kSrbFriction)) return;
        const float k = ee::div(ps2::ffloat(0x3b5a740e), b->mass);  // 0,0033333334 / a massa
        Reg v1, v2, v3;
        load(v1, force);
        v2[0] = ps2::fbits(k);
        v3[0] = v3[1] = v3[2] = v3[3] = 0;
        mulbc(v3, v1, v2, BX, XYZ);
        float a[4];
        store(a, v3);
        SimpleRigidBody_Accelerate(b, a);
        return;
    }
    const int slot = rigid_slot_of(sim, o);
    if (slot < 0) {
        ++g_stats.guards;
        return;
    }
    RigidBody* rb = GetRigidBody(sim, slot);
    ResolveForce(rb, force);
    ResolveTorque(rb, torque);
    rb->radius_flags |= 1;
}

// [driving] 0x001958a8 PhysicsObject::GetSig
uint32_t PhysicsObject_GetSig(PhysicsObject* o) {
    Simulation& sim = simulation();
    if (SimpleObject* s = as_simple(o)) return sim.simple_sig[GetSimpleRigidBody(sim, s->po.slot)->slot];
    const int slot = rigid_slot_of(sim, o);
    if (slot < 0) {
        ++g_stats.guards;
        return 0;
    }
    return sim.rigid_sig[GetRigidBody(sim, slot)->slot];
}

// [driving] 0x00195aa8 PhysicsObject::Simulate: o Simulate do render (o RSceneObj +0x48, a entrada +0x7c); o port não
// tem render nos objetos de corpo simples (contado). Anotado sempre (com o tipo) para a parte 18.
void PhysicsObject_Simulate(PhysicsObject* o) {
    SimpleObject* s = as_simple(o);
    effects_note(0x00195aa8, uint32_t(o->kind()));
    if (s && s->po.render) ++g_stats.render_simulate;
}

// ------------------------------------------------------------------------------------------------- o Explosion

// [driving] 0x0017dd68 Explosion::~Explosion (e o ~PhysicsObject 0x00195568: o render, o som e a vibração são nulos; a
// vaga do corpo simples volta)
Explosion::~Explosion() {
    Simulation& sim = simulation();
    if (po.slot >= 0 && po.slot < kSimpleBodySlots && sim.simple_owners[po.slot] == this)
        ReleaseSimpleBodySlot(sim, po.slot);
}

void Explosion::simulate() { Explosion_Simulate(*this); }

// O Explosion não tem RSceneObj: GetCollisionBounds devolve falso.
bool Explosion::collision_bounds(float /*half*/[4]) { return false; }

// [driving] 0x0017dc58 Explosion::Explosion
void Explosion_Construct(Explosion& x, int32_t kind, const float pos[3], float damage, PhysicsObject* source,
                         uint32_t sig) {
    Simulation& sim = simulation();
    // [driving] 0x00195470 PhysicsObject::PhysicsObject (o corpo simples; o AttributeSet "explosion" pelo nome da tabela
    // 0x3318f0: o port não o tem)
    x.po = PhysicsObjectWords{};
    x.po.type = 3;
    x.po.flags = 1;
    x.po.slot = int16_t(AssignSimpleBodySlot(sim, &x));
    x.po.owner_sig = sig;
    x.po.vtable = 0x00377a40;
    x.d = ExplosionData{};
    x.d.kind = kind;
    x.d.damage = damage;
    x.d.source = source ? nf::p32_u(source) : 0;
    for (uint32_t& w : x.d.seen) w = 0;  // [driving] 0x0017e478 SimCollisionMap::Clear
    if (x.po.slot < 0) {
        ++g_stats.guards;  // o PS2 escreveria o corpo 0x40 bytes antes do vetor
        return;
    }
    static const float zero[3] = {0, 0, 0};
    static const float identity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    SimpleRigidBody* b = GetSimpleRigidBody(sim, x.po.slot);
    SimpleRigidBody_Construct(b, 0.0f, 0.0f, x.po.slot, kSimpleExplosion, pos, zero, zero, identity);
    b->flags = uint16_t(b->flags | 0x131);
}

// [driving] 0x0017ddc0 Explosion::Simulate
void Explosion_Simulate(Explosion& x) {
    Simulation& sim = simulation();
    ++g_stats.explosions_simulated;
    SimpleRigidBody* b = GetSimpleRigidBody(sim, x.po.slot);
    const float centre[3] = {b->pos[0], b->pos[1], b->pos[2]};
    const float r1 = ee::add(b->radius, 1.0f);
    float fall = ee::mul(r1, 0.5f);
    if (!ee::lt(1.0f, fall)) fall = 1.0f;
    const float raw = ee::mul(x.d.damage, ee::div(1.0f, ee::mul(fall, fall)));
    const float clamped = ee::min(raw, 50.0f);
    SimpleRigidBody_SetCanHitTrigger(b, ee::le(40.0f, raw));
    if (ee::le(clamped, 1.0f) || ee::lt(150.0f, r1)) {
        DeletePhysicsObject(sim, &x);
    } else {
        const SimCollisionMap hits = sim.srb_hits[b->slot];
        SimCollisionMap fresh, seen;
        std::memcpy(seen.bits, x.d.seen, sizeof seen.bits);
        for (int i = 0; i < 3; ++i) fresh.bits[i] = ~seen.bits[i] & hits.bits[i];
        for (int i = 0; i < 3; ++i) seen.bits[i] |= hits.bits[i];
        std::memcpy(x.d.seen, seen.bits, sizeof seen.bits);
        static const float kForce[4] = {8000.0f, 9000.0f, 8000.0f, 0};     // 0x377a08
        static const float kTorqueAdd[4] = {-0.5f, -0.5f, -0.5f, 0};       // 0x377a18
        static const float kTorqueMul[4] = {16000.0f, 18000.0f, 16000.0f, 0};  // 0x377a28
        for (int i = 0; i < 0x90; ++i) {
            if (!((fresh.bits[i >> 6] >> (i & 63)) & 1)) continue;
            PhysicsObject* o = GetOrderedBody(sim, i);
            if (!o) {
                ++g_stats.guards;  // o PS2 leria o objeto nulo
                continue;
            }
            float aspect = 1.0f;
            float half[4] = {};
            if (PhysicsObject_GetCollisionBounds(o, half)) {
                const float lo = ee::min(ee::min(half[0], half[1]), half[2]);
                const float hi = ee::max(ee::max(half[0], half[1]), half[2]);
                if (ee::lt(ee::mul(lo, 20.0f), hi)) aspect = ee::div(ee::mul(lo, 10.0f), hi);
            }
            const float* opos = PhysicsObject_GetPosition(o);
            alignas(16) float dir[4] = {ee::sub(opos[0], centre[0]), ee::sub(opos[1], centre[1]),
                                        ee::sub(opos[2], centre[2]), 0.0f};
            const float scale = ee::mul(clamped, aspect);
            const float s[3] = {ee::mul(kForce[0], scale), ee::mul(kForce[1], scale), ee::mul(kForce[2], scale)};
            for (int k = 0; k < 3; ++k) dir[k] = ee::mul(dir[k], s[k]);
            const float limit = ee::mul(PhysicsObject_GetMass(o), 25.0f);
            const float len2 = ps2::ffloat(vu_len2(dir));
            if (ee::lt(ee::mul(limit, limit), len2)) {
                const float k = ee::mul(limit, ee::rsqrt(1.0f, len2));
                for (int j = 0; j < 3; ++j) dir[j] = ee::mul(dir[j], k);
            }
            const uint32_t r0 = SimRandom_Generate(sim_random());
            const uint32_t r2 = SimRandom_Generate(sim_random());
            alignas(16) float torque[4] = {};
            BytesToCoord((~r0 << 16) | r2, kTorqueAdd, kTorqueMul, nullptr, torque);
            for (int k = 0; k < 3; ++k) torque[k] = ee::mul(torque[k], scale);
            bool push = true;
            if (sim.cars.size() && o == sim.cars[0] && mission_exists() && mission_manager().auto_drive != 0)
                push = false;
            else if (o->kind() == 1 && same_nocase(o->name(), "tunnel"))
                push = false;
            if (push) PhysicsObject_ApplyForces(o, dir, torque);
            if (!ee::lt(0.0f, raw) || x.d.kind == 6) continue;
            const float amount = (x.d.source && nf::p32_u(o) == x.d.source) ? x.d.damage : raw;
            ++g_stats.damage_calls;
            effects_note(0x00189598, ps2::fbits(amount), uint32_t(i), 3);
            const bool at_player = sim.cars.size() && o == sim.cars[0];
            const float hp_before = at_player ? o->hit_points() : 0.0f;  // (só a conta do port)
            o->apply_damage(centre, opos, amount, 0.8f, 3);
            if (at_player && o->hit_points() < hp_before) {
                ++g_stats.player_hits;
                g_stats.player_damage += double(hp_before) - double(o->hit_points());
            }
        }
        const float rate = ee::cvt_s(sim.frame_rate);  // GetFoundationVideoModeRate (60,0)
        b->radius = ee::add(b->radius, ee::mul(rate, sim.dt));
    }
    PhysicsObject_Simulate(&x);
}

// ------------------------------------------------------------------------------------------------- a Simulation

// [driving] 0x00200200 Simulation::SpawnExplosionObject
Explosion* SpawnExplosionObject(Simulation& sim, float damage, int32_t kind, const float /*dir*/[3], const float pos[3],
                                PhysicsObject* source, uint32_t sig) {
    if (!CanSpawnSimpleRigidBody(sim, pos, ee::lt(500.0f, damage))) {
        ++g_stats.spawn_refused;
        return nullptr;
    }
    Explosion* x = new Explosion();
    Explosion_Construct(*x, kind, pos, damage, source, sig);
    SpawnPhysicsObject(sim, x);
    ++g_stats.explosions_spawned;
    return x;
}

// [driving] 0x00200cb8 Simulation::SpawnPhysicsObject (o mapa +0xe68 é a ordem dos donos de corpo simples na
// Simulation do port; os vetores dos tipos 3 e 6)
void SpawnPhysicsObject(Simulation& sim, PhysicsObject* o) {
    if (o->kind() == 3) sim.explosions.push_back(o);
    else if (o->kind() == 6) sim.missiles.push_back(o);
    else if (o->kind() == 7) sim.shells.push_back(o);  // +0xe20 (driving/game/shells.h)
}

// [driving] 0x00200f18 Simulation::DeletePhysicsObject
void DeletePhysicsObject(Simulation& sim, PhysicsObject* o) {
    for (PhysicsObject* d : sim.dead)
        if (d == o) return;  // já na lista (o conjunto +0xe6c)
    std::vector<PhysicsObject*>* v = o->kind() == 3   ? &sim.explosions
                                     : o->kind() == 6 ? &sim.missiles
                                     : o->kind() == 7 ? &sim.shells
                                                      : nullptr;
    if (v) {
        for (size_t i = 0; i < v->size(); ++i)
            if ((*v)[i] == o) {
                v->erase(v->begin() + std::ptrdiff_t(i));
                break;
            }
    }
    if (o->kind() == 3) ++g_stats.explosions_deleted;
    sim.dead.push_back(o);
}

// [driving] 0x00200088 Simulation::SpawnExplosion
void SpawnExplosion(Simulation& sim, float damage, int32_t kind, int32_t weapon, const float dir[3], const float pos[3],
                    int32_t face, PhysicsObject* object) {
    if (!CanSpawnSimpleRigidBody(sim, pos, ee::lt(500.0f, damage))) {
        ++g_stats.spawn_refused;
        return;
    }
    // [driving] 0x00159fb8 ESpawnExplosion::ESpawnExplosion: a imagem do objeto (0x50 bytes).
    PendingEvent e{33, 0, 0.0f, 0, 0, 0};
    e.size = 0x50;
    e.written = 0xfffff;
    e.obj[0] = 0x00370498;
    const float p4[4] = {pos[0], pos[1], pos[2], 1.0f};
    const float d4[4] = {dir[0], dir[1], dir[2], 0.0f};
    std::memcpy(&e.obj[1], p4, 16);
    std::memcpy(&e.obj[5], d4, 16);
    e.obj[9] = 0;                                                       // +0x24 o RSceneObj
    SimpleObject* so = object ? as_simple(object) : nullptr;
    e.obj[10] = so ? 1u : 0u;                                           // +0x28 corpo simples
    int32_t slot = -1;
    if (object) slot = so ? so->po.slot : rigid_slot_of(sim, object);
    e.obj[11] = uint32_t(slot);                                         // +0x2c
    e.obj[12] = object ? nf::p32_u(object) : 0;                         // +0x30
    e.obj[13] = uint32_t(kind);                                         // +0x34
    e.obj[14] = uint32_t(weapon);                                       // +0x38
    e.obj[15] = ps2::fbits(damage);                                     // +0x3c
    e.obj[16] = uint32_t(face);                                         // +0x40
    e.obj[17] = 0;                                                      // +0x44 o efeito
    e.obj[18] = 0;                                                      // +0x48
    e.obj[19] = 0;                                                      // +0x4c o nome
    ++event_stats().created[33];
    trigger_manager().pending.push_back(e);
}

// ------------------------------------------------------------------------------------------------- os eventos

// [driving] 0x0015a098 ESpawnExplosion::~ESpawnExplosion
void ESpawnExplosion_Execute(const PendingEvent& e) {
    Simulation& sim = simulation();
    const uint32_t* o = e.obj;
    auto f = [&](int i) { return ps2::ffloat(o[i]); };
    const uint32_t render = o[9];
    const uint32_t effect = o[17];
    alignas(16) float wpos[4] = {0, 0, 0, 1};    // sp+0x00
    alignas(16) float quat[4] = {0, 0, 0, 1};    // sp+0x10
    alignas(16) float lpos[4] = {0, 0, 0, 1};    // sp+0x20
    alignas(16) float lquat[4] = {0, 0, 0, 1};   // sp+0x30
    alignas(16) float m[4][4] = {};              // sp+0x40
    alignas(16) float q80[4] = {};               // sp+0x80
    uint32_t gfx = 0;                            // s2
    int32_t instance = -1;                       // s6
    bool trigger_ok = true;                      // s5
    if (effect) {
        const uint8_t* ef = nf::p32_ptr<const uint8_t>(effect);
        std::memcpy(&gfx, ef + 0x18, 4);
        instance = ef[0x15];
        std::memcpy(lpos, ef, 16);
        lpos[3] = 1.0f;
        std::memcpy(lquat, ef + 0x20, 16);
        if (render) {
            uint32_t flags;
            std::memcpy(&flags, ef + 0x10, 4);
            trigger_ok = (flags & 0x109e) == 0;
            if (!g_env.instance_position || !g_env.instance_position(render, instance, m)) ++g_stats.guards;
            effects_note(0x001c9368, render, uint32_t(instance));
            transform_point(m, lpos, wpos);
            m[3][0] = m[3][1] = m[3][2] = 0.0f;
            VU0_m4toquat(m, q80);
            quat_mul(q80, lquat, quat);
        } else {
            std::memcpy(wpos, lpos, 16);
            std::memcpy(quat, lquat, 16);
        }
    } else if (render) {
        if (!g_env.instance_position || !g_env.instance_position(render, -1, m)) ++g_stats.guards;
        effects_note(0x001c9368, render, 0xffffffffu);
        std::memcpy(lpos, m[3], 16);
        std::memcpy(wpos, lpos, 16);
        m[3][0] = m[3][1] = m[3][2] = 0.0f;
        VU0_m4toquat(m, lquat);
        std::memcpy(quat, lquat, 16);
    } else {
        lpos[0] = f(1);
        lpos[1] = f(2);
        lpos[2] = f(3);
        lpos[3] = f(4);
        std::memcpy(wpos, lpos, 16);
        alignas(16) float up[4] = {0, 1.0f, 0, 1.0f};  // sp+0x50: kZeroCoord4 com y = 1
        alignas(16) float dir[4] = {f(5), f(6), f(7), f(8)};
        Reg v1, v2, v3;
        load(v1, dir);
        load(v2, up);
        mul(v3, v1, v2, XYZW);
        addbc(v3, v3, v3, BY, X);
        addbc(v3, v3, v3, BZ, X);
        addbc(v3, v3, v3, BW, X);
        if (ee::lt(0.9f, ps2::ffloat(v3[0]))) {
            const float side[4] = {1.0f, 0, 0, 1.0f};
            cross_norm(dir, side, up);
            cross_norm(up, dir, m[0]);
        } else {
            cross_norm(up, dir, m[0]);
            cross_norm(dir, m[0], up);
        }
        std::memcpy(m[1], up, 16);
        std::memcpy(m[2], dir, 16);
        m[3][0] = m[3][1] = m[3][2] = 0.0f;
        m[3][3] = 1.0f;
        VU0_m4toquat(m, lquat);
        std::memcpy(quat, lquat, 16);
    }
    // A direção da explosão: (0, 1, 0, 1) girado pelo quatérnio (0x36fb18 vezes a matriz).
    alignas(16) float up_dir[4] = {0, 1.0f, 0, 1.0f};
    VU0_quattom4(quat, m);
    transform_point(m, up_dir, q80);
    if (gfx == 0) {
        const char* name = o[19] ? nf::p32_ptr<const char>(o[19]) : nullptr;
        bool counted = false;
        if (!name) {
            const int32_t weapon = int32_t(o[14]), face = int32_t(o[16]);
            const float damage = f(15);
            if (weapon == 0x1e) name = "FODHitEffect";
            else if (face == 9) name = "snowspecificexplosion";
            else if (face == 6) name = "rocketsplash";
            else if (player_underwater(sim)) {
                name = ee::lt(600.0f, damage) ? "Un_ExplL" : ee::lt(200.0f, damage) ? "Un_ExplM" : "Un_ExplS";
            } else if (uint32_t(face - 2) < 4 || face == 10 || face == 0x50 || face == 0x60) {
                name = kExSmallD[uint32_t(sim.steps) % 3];  // div (com sinal) do passo
            } else {
                const BondCar* p = player_car(sim);
                const bool mine = p && o[12] == nf::p32_u(p);
                name = (mine ? kExPlayer : kExOther)[g_ex_counter];
                gfx = g_env.gfx_lookup ? g_env.gfx_lookup(name) : 0;
                effects_note(0x00232f48, gfx);
                g_ex_counter = (g_ex_counter + 1) % 3;
                counted = true;
            }
        }
        if (!counted) {
            gfx = g_env.gfx_lookup ? g_env.gfx_lookup(name) : 0;
            effects_note(0x00232f48, gfx);
        }
        if (!gfx) ++g_stats.gfx_lookups_failed;
        trigger_ok = gfx != 0;
    }
    if (trigger_ok) {
        // [driving] 0x002330b0 GFX::Trigger (a galeria não foi portada)
        ++g_stats.gfx_triggers;
        static const float kZero3[3] = {0.0f, 0.0f, 0.0f}, kIdentity[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // 0x355b80/90
        if (render) {
            effects_note(0x002330b0, gfx, ps2::fbits(lpos[0]), ps2::fbits(lpos[1]), ps2::fbits(lpos[2]), render,
                         uint32_t(instance));
            if (g_env.gfx_trigger) g_env.gfx_trigger(gfx, kZero3, kIdentity, lpos, lquat, render, instance, 0);
        } else {
            effects_note(0x002330b0, gfx, ps2::fbits(lpos[0]), ps2::fbits(lpos[1]), ps2::fbits(lpos[2]), 0, 0);
            if (g_env.gfx_trigger) g_env.gfx_trigger(gfx, lpos, lquat, kZero3, kIdentity, 0, 0, 0);
        }
    } else if (gfx) {
        // A entrada +0x84 do RSceneObj (o TriggerFX da instância do efeito) com -1 e o byte +0x17 do efeito.
        ++g_stats.render_fx;
        effects_note(0x001c0084, render, nf::p32_ptr<const uint8_t>(effect)[0x17]);
    }
    PhysicsObject* source = nullptr;
    const int32_t slot = int32_t(o[11]);
    if (slot != -1) {
        if (slot < 0 || slot >= (o[10] ? kSimpleBodySlots : kRigidBodySlots)) ++g_stats.guards;
        else source = o[10] ? sim.simple_owners[slot] : sim.owners[slot];
    }
    BondCar* player = player_car(sim);
    if (player) {
        const RigidBody* rb = player->body();
        const float d[3] = {ee::sub(rb->pos[0], wpos[0]), ee::sub(rb->pos[1], wpos[1]), ee::sub(rb->pos[2], wpos[2])};
        float dist = ps2::ffloat(ps2::sqrt(vu_len2(d)));
        dist = ee::max(ps2::ffloat(0x38d1b717), dist);  // 0,0001
        if (ee::le(dist, 20.0f) && player->feedback.port >= 0) {
            const float jolt = ee::min(1.0f, ee::div(10.0f, dist));
            ++g_stats.jolts;
            effects_note(0x00165fb0, ps2::fbits(jolt));
            IFeedback_ApplyJolt(player->feedback, jolt);
        }
    } else {
        ++g_stats.guards;  // o PS2 leria o carro do jogador de um vetor vazio
    }
    SpawnExplosionObject(sim, f(15), int32_t(o[13]), q80, wpos, source, 0);
    // [driving] 0x001eb078 RScorchWorld::ScorchGround(8, WWorldPos &, pos, 5) e 0x001afa88 RHighLevelLightManager::
    // AddExplosion(dano, pos) (driving/fx, pelo ambiente); 0x001402f8 AIZoneController::RegisterZone(3, pos): não portado.
    ++g_stats.scorches;
    ++g_stats.lights;
    ++g_stats.ai_zones;
    effects_note(0x001eb078, ps2::fbits(wpos[0]), ps2::fbits(wpos[1]), ps2::fbits(wpos[2]));
    if (g_env.scorch_ground) g_env.scorch_ground(wpos, 5, 8.0f);
    effects_note(0x001afa88, o[15], ps2::fbits(wpos[0]), ps2::fbits(wpos[1]), ps2::fbits(wpos[2]));
    if (g_env.explosion_light) g_env.explosion_light(wpos, f(15));
    effects_note(0x001402f8, 3, ps2::fbits(wpos[0]), ps2::fbits(wpos[1]), ps2::fbits(wpos[2]));
    // Os sons: a primeira vaga do anel de quatro cujo prazo passou (no máximo quatro tentativas).
    const uint32_t start = g_ring & 3;
    for (;;) {
        if (g_ring_time[g_ring & 3] < TIMER_gettick()) break;
        ++g_ring;
        if ((g_ring & 3) == start) return;
    }
    g_ring_time[g_ring & 3] = TIMER_gettick() + 0x1e;
    float scale = 1.0f;
    if (same_nocase(g_env.level ? g_env.level : "", "jungleb_mis13b")) scale = 5.0f;
    ABank* bank = ABank_Category(4);  // ABank::fgBanks[4] "Weapons"
    const int32_t patch = bank ? bank->index.Lookup(kExpSounds[g_ring & 3]) : -1;
    effects_note(0x002f5880, uint32_t(patch));
    if (bank) {
        AOneShotSound* s = new AOneShotSound("Explosions", bank->snd_bank, patch, 1.0f);
        s->fx = 0.0f;
        s->lifetime = 0.0f;
        std::memcpy(s->pos, wpos, 12);
        s->fx = 1.0f;
        s->max_distance = ee::mul(scale, 400.0f);
        s->min_distance = ee::mul(scale, 100.0f);
        s->max_distance2 = ee::mul(s->max_distance, s->max_distance);
        ++g_stats.sounds;
        const int32_t patch2 = bank->index.Lookup("SFX_DistExp");
        effects_note(0x002f5880, uint32_t(patch2));
        ALimitedSound* l = new ALimitedSound("ExplosionBass", bank->snd_bank, patch2, 1.0f);
        l->lifetime = 0.0f;
        std::memcpy(l->pos, wpos, 12);
        l->fx = 0.0f;
        l->max_distance = ee::mul(scale, 5000.0f);
        l->min_distance = ee::mul(scale, 1000.0f);
        l->max_distance2 = ee::mul(l->max_distance, l->max_distance);
        ++ALimitedSound::count;
        ++g_stats.limited_sounds;
    } else {
        ++g_stats.sounds_skipped;
    }
    ++g_ring;
}

// [driving] 0x0015b010 ESpawnExplosionStatic::~ESpawnExplosionStatic: um ESpawnExplosion na posição do evento (+4: o
// COORD3, ou a do gatilho +8, o +0x30 dele), apontando para cima (0, 0, 1, 1), sem RSceneObj nem objeto.
void ESpawnExplosionStatic_Execute(const PendingEvent& e) {
    const uint32_t* o = e.obj;
    alignas(16) float pos[4] = {0, 0, 1.0f, 1.0f};
    alignas(16) float dir[4] = {0, 0, 1.0f, 1.0f};
    if (o[1]) {
        std::memcpy(pos, nf::p32_ptr<const uint8_t>(o[1]), 12);
    } else if (o[2]) {
        std::memcpy(pos, nf::p32_ptr<const uint8_t>(o[2]) + 0x30, 12);
    }
    PendingEvent x{33, 0, 0.0f, 0, 0, 0};
    x.size = 0x50;
    x.written = 0xfffff;
    x.obj[0] = 0x00370498;
    const float p4[4] = {pos[0], pos[1], pos[2], 1.0f};
    std::memcpy(&x.obj[1], p4, 16);
    std::memcpy(&x.obj[5], dir, 16);
    x.obj[9] = 0;
    x.obj[10] = 0;
    x.obj[11] = 0xffffffffu;
    x.obj[12] = 0;
    x.obj[13] = o[3];   // +0xc o tipo
    x.obj[14] = o[4];   // +0x10 a arma
    x.obj[15] = o[5];   // +0x14 o dano
    x.obj[16] = o[6];   // +0x18 a face
    x.obj[17] = 0;
    x.obj[18] = 0;
    x.obj[19] = o[7];   // +0x1c o nome
    ++event_stats().created[33];
    trigger_manager().pending.push_back(x);
}

bool effects_event_execute(const PendingEvent& e) {
    switch (e.type) {
    case 10:
        EFireWeapon_Execute(e);
        return true;
    case 33:
        ESpawnExplosion_Execute(e);
        return true;
    case 154:
        ESpawnExplosionStatic_Execute(e);
        return true;
    case 9:
    case 36:
    case 130:
    case 173:
    case 196:
    case 250:
    case 252:
    case 253:
    case 257:
    case 279:
        // Os eventos de efeito (driving/fx/fx_events.h): 9, 196, 250 e 257 executados; os outros só contados.
        g_stats.other_events.push_back(e.type);
        return fx_event_execute(e);
    default:
        return false;
    }
}

}  // namespace drv
