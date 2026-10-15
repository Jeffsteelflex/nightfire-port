// O corpo simples (driving/physics/simple_body.h), do MIPS do DRIVING.ELF (RVehicleParticle.obj e Simulation.obj).

#include "driving/physics/simple_body.h"

#include <cstring>

#include "driving/physics/obb.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {

const uint32_t kVf0[4] = {0, 0, 0, 0x3f800000};
SimpleBodyStats g_stats;

// x² + y² + z² como a VU0 faz (vmul.xyz, vmulax.x, vmadday.x, vmaddz.x com 1,0 em vf3.x).
uint32_t vu_len2(const float v[3]) {
    const uint32_t x = ps2::fbits(v[0]), y = ps2::fbits(v[1]), z = ps2::fbits(v[2]);
    uint32_t acc = ps2::mul(x, x);
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(y, y));
    return ps2::madd(acc, 0x3f800000u, ps2::mul(z, z));
}

}  // namespace

SimpleBodyStats& simple_body_stats() { return g_stats; }

// [driving] 0x001fd448 SimpleRigidBody::SimpleRigidBody
SimpleRigidBody* SimpleRigidBody_Construct(SimpleRigidBody* b, float radius, float mass, int slot, uint8_t type,
                                           const float pos[3], const float vel[3], const float accel[3],
                                           const float orient[4][4]) {
    std::memcpy(b->pos, pos, 12);
    b->slot = int8_t(slot);
    b->type = type;
    b->flags = 0;
    std::memcpy(b->vel, vel, 12);
    b->radius = radius;
    std::memcpy(b->accel, accel, 12);
    b->mass = mass;
    VU0_m4toquat(orient, b->quat);
    return b;
}

// [driving] 0x001fd4d8 SimpleRigidBody::~SimpleRigidBody
void SimpleRigidBody_Destruct(SimpleRigidBody* b) { std::memset(b, 0, sizeof *b); }

// [driving] 0x001fd528 SimpleRigidBody::GetOwner
PhysicsObject* SimpleRigidBody_GetOwner(const SimpleRigidBody* b) { return simulation().simple_owners[b->slot]; }

// [driving] 0x001fd548 SimpleRigidBody::RecalcOrientMat
void SimpleRigidBody_RecalcOrientMat(const SimpleRigidBody* b, float m[4][4]) { VU0_quattom4(b->quat, m); }

// [driving] 0x002cc230 (o eixo x do quatérnio)
void srb_quat_x_axis(const float q[4], float out[4]) {
    Reg v1, v5, v6, v8;
    load(v1, q);
    load(v8, out);
    mulbc(v5, v1, v1, BY, XYZW);
    mulbc(v6, v1, v1, BZ, XYZW);
    add(v5, v5, v5, XYZW);
    add(v6, v6, v6, XYZW);
    addbc(v8, kVf0, kVf0, BW, X);
    addbc(v8, kVf0, v5, BX, Y);
    addbc(v8, kVf0, v6, BX, Z);
    subbc(v8, v8, v5, BY, X);
    addbc(v8, v8, v6, BW, Y);
    subbc(v8, v8, v5, BW, Z);
    subbc(v8, v8, v6, BZ, X);
    store(out, v8);
}

// [driving] 0x002cc2b0 (o eixo y do quatérnio)
void srb_quat_y_axis(const float q[4], float out[4]) {
    Reg v1, v4, v5, v6, v7, v9;
    load(v1, q);
    load(v9, out);
    mulbc(v4, v1, v1, BX, XYZW);
    mulbc(v5, v1, v1, BY, XYZW);
    mulbc(v6, v1, v1, BZ, XYZW);
    mulbc(v7, v1, v1, BW, XYZW);
    add(v4, v4, v4, XYZW);
    add(v5, v5, v5, XYZW);
    add(v6, v6, v6, XYZW);
    add(v7, v7, v7, XYZW);
    addbc(v9, kVf0, v5, BX, X);
    addbc(v9, kVf0, kVf0, BW, Y);
    addbc(v9, kVf0, v6, BY, Z);
    subbc(v9, v9, v6, BW, X);
    subbc(v9, v9, v4, BX, Y);
    addbc(v9, v9, v7, BX, Z);
    subbc(v9, v9, v6, BZ, Y);
    store(out, v9);
}

// [driving] 0x002cc350 VU0_ExtractZAxis3FromQuat
void srb_quat_z_axis(const float q[4], float out[4]) {
    Reg v1, v4, v5, v6, v7, v10;
    load(v1, q);
    load(v10, out);
    mulbc(v4, v1, v1, BX, XYZW);
    mulbc(v5, v1, v1, BY, XYZW);
    mulbc(v6, v1, v1, BZ, XYZW);
    mulbc(v7, v1, v1, BW, XYZW);
    add(v4, v4, v4, XYZW);
    add(v5, v5, v5, XYZW);
    add(v6, v6, v6, XYZW);
    add(v7, v7, v7, XYZW);
    addbc(v10, kVf0, v6, BX, X);
    addbc(v10, kVf0, v5, BZ, Y);
    addbc(v10, kVf0, kVf0, BW, Z);
    addbc(v10, v10, v5, BW, X);
    subbc(v10, v10, v7, BX, Y);
    subbc(v10, v10, v4, BX, Z);
    subbc(v10, v10, v5, BY, Z);
    store(out, v10);
}

// [driving] 0x0017b8b0 Float_COORD3toCOORD4
void Float_COORD3toCOORD4(float out[4], const float in[3]) {
    const float t[4] = {in[0], in[1], in[2], 0.0f};
    std::memcpy(out, t, 16);
}

// [driving] 0x001fd598 SimpleRigidBody::GetForwardVector
void SimpleRigidBody_GetForwardVector(const SimpleRigidBody* b, float out[4]) { srb_quat_z_axis(b->quat, out); }
// [driving] 0x001fd5b8 SimpleRigidBody::GetRightVector
void SimpleRigidBody_GetRightVector(const SimpleRigidBody* b, float out[4]) { srb_quat_x_axis(b->quat, out); }
// [driving] 0x001fd5d8 SimpleRigidBody::GetUpVector
void SimpleRigidBody_GetUpVector(const SimpleRigidBody* b, float out[4]) { srb_quat_y_axis(b->quat, out); }

// [driving] 0x001fd5f8 SimpleRigidBody::SetOrientMat
void SimpleRigidBody_SetOrientMat(SimpleRigidBody* b, const float m[4][4]) { VU0_m4toquat(m, b->quat); }

// [driving] 0x001fd620 SimpleRigidBody::SetOrientation
void SimpleRigidBody_SetOrientation(SimpleRigidBody* b, const float q[4]) { std::memcpy(b->quat, q, 16); }

// [driving] 0x001fd648 SimpleRigidBody::GetScalarVelocity
float SimpleRigidBody_GetScalarVelocity(const SimpleRigidBody* b) { return ps2::ffloat(ps2::sqrt(vu_len2(b->vel))); }

// [driving] 0x001fd680 SimpleRigidBody::GetLinearVelocityCH
float SimpleRigidBody_GetLinearVelocityCH(const SimpleRigidBody* b, int axis) {
    alignas(16) float m[4][4];
    SimpleRigidBody_RecalcOrientMat(b, m);
    Reg v1, v2, v3;
    load(v1, b->vel);
    load(v2, m[axis]);
    mul(v3, v1, v2, XYZ);
    addbc(v3, v3, v3, BY, X);
    addbc(v3, v3, v3, BZ, X);
    return ps2::ffloat(v3[0]);
}

// [driving] 0x001fd6d8 SimpleRigidBody::Accelerate
void SimpleRigidBody_Accelerate(SimpleRigidBody* b, const float a[3]) {
    if (!(b->flags & kSrbMoves)) return;
    const float dt = simulation().dt;
    b->vel[0] = ee::add(b->vel[0], ee::mul(a[0], dt));
    b->vel[1] = ee::add(b->vel[1], ee::mul(a[1], dt));
    b->vel[2] = ee::add(b->vel[2], ee::mul(a[2], dt));
}

// [driving] 0x001fd730 SimpleRigidBody::ApplyFriction
void SimpleRigidBody_ApplyFriction(SimpleRigidBody* b) {
    if (!(b->flags & kSrbMoves)) return;
    Reg v1, v2, v3;
    load(v1, b->vel);
    load(v3, b->vel);
    v2[0] = 0x3f7d70a4;  // 0,99
    mulbc(v3, v1, v2, BX, XYZ);
    store(b->vel, v3);
    load(v1, b->accel);
    load(v3, b->accel);
    v2[0] = 0x3f7ae148;  // 0,98
    mulbc(v3, v1, v2, BX, XYZ);
    store(b->accel, v3);
}

// [driving] 0x001fd798 SimpleRigidBody::UpdatePosition
void SimpleRigidBody_UpdatePosition(SimpleRigidBody* b) {
    if (b->flags & kSrbFriction) SimpleRigidBody_ApplyFriction(b);
    if (!(b->flags & kSrbMoves)) return;
    const float dt = simulation().dt;
    const float dx = ee::mul(b->vel[0], dt), dy = ee::mul(b->vel[1], dt), dz = ee::mul(b->vel[2], dt);
    b->pos[0] = ee::add(b->pos[0], dx);
    b->pos[1] = ee::add(b->pos[1], dy);
    b->pos[2] = ee::add(b->pos[2], dz);
}

// [driving] 0x001fd820 SimpleRigidBody::NeedsCollisionCheck
bool SimpleRigidBody_NeedsCollisionCheck(const SimpleRigidBody* b) { return (b->flags & 0xf0) != 0; }

// [driving] 0x001fd830 SimpleRigidBody::CheckCollisions
// Quirk guardado: o teste contra um corpo simples de helicóptero (tipo 8) lê o +0x13c do dono DESTE corpo (o
// GetOwner do this, não o do outro); no port o dono responde pelo seu PhysicsObject::raw_word. Divergência: numa
// explosão (0x98 bytes) o +0x13c já está fora do objeto, no lixo do heap do PS2; o port lê 0 (o helicóptero conta 3 m
// mais alto).
void SimpleRigidBody_CheckCollisions(const SimpleRigidBody* b, SimCollisionMap& hits) {
    Simulation& sim = simulation();
    float r = b->radius;
    if (ee::lt(r, 0.25f)) r = 0.25f;
    const float speed = SimpleRigidBody_GetScalarVelocity(b);
    const uint32_t flags = b->flags;
    if (flags & (kSrbNearRigid | kSrbBoxRigid)) {
        for (int i = 0; i < kRigidBodySlots; ++i) {
            if (!sim.owners[i]) continue;
            const RigidBody* rb = GetRigidBody(sim, i);
            if (rb->state == 3) continue;
            const float d[3] = {ee::sub(b->pos[0], rb->pos[0]), ee::sub(b->pos[1], rb->pos[1]),
                                ee::sub(b->pos[2], rb->pos[2])};
            const float d2 = ps2::ffloat(vu_len2(d));
            float rr;
            std::memcpy(&rr, &rb->radius_flags, 4);
            const float sum = ee::add(r, rr);
            if (!ee::lt(d2, ee::mul(sum, sum))) continue;
            if (flags & kSrbNearRigid) {
                hits.bits[i >> 6] |= uint64_t(1) << (i & 63);
                continue;
            }
            const RigidBodyInfo* info = rb->info;
            alignas(16) float mrb[4][4], msrb[4][4], crb[4], csrb[4], hrb[4];
            std::memcpy(mrb, info->orient, 64);
            VU0_quattom4(b->quat, msrb);
            Float_COORD3toCOORD4(crb, rb->pos);
            Float_COORD3toCOORD4(csrb, b->pos);
            const float two_r = ee::add(r, r);
            const float hsrb[4] = {two_r, two_r, ee::mul(two_r, speed), 1.0f};
            std::memcpy(hrb, info->half_size, 16);
            OBB o_rb{}, o_srb{};
            OBB_Reset(o_rb, mrb, crb, hrb);
            OBB_Reset(o_srb, msrb, csrb, hsrb);
            if (OBB_CheckOBBOverlap(o_srb, o_rb)) hits.bits[i >> 6] |= uint64_t(1) << (i & 63);
        }
    }
    if (flags & (kSrbNearSimple | kSrbBoxSimple)) {
        for (int j = 0; j < kSimpleBodySlots; ++j) {
            const int bit = j + kRigidBodySlots;
            if (j == b->slot || !sim.simple_owners[j]) continue;
            const SimpleRigidBody* o = GetSimpleRigidBody(sim, j);
            if (o->flags & kSrbNoCollide) continue;
            float p[3] = {o->pos[0], o->pos[1], o->pos[2]};
            const float orad = o->radius;
            if (o->type == kSimpleHuman) p[1] = ee::add(p[1], 0.5f);
            if (o->type == kSimpleHelicopter) {
                PhysicsObject* own = SimpleRigidBody_GetOwner(b);
                ++g_stats.helicopter_owner;
                if ((own ? own->raw_word(0x13c) : 0) == 0) p[1] = ee::add(p[1], 3.0f);
            }
            const float d[3] = {ee::sub(b->pos[0], p[0]), ee::sub(b->pos[1], p[1]), ee::sub(b->pos[2], p[2])};
            const float d2 = ps2::ffloat(vu_len2(d));
            const float sum = ee::add(r, orad);
            if (!ee::lt(d2, ee::mul(sum, sum))) continue;
            if (flags & kSrbNearSimple) {
                hits.bits[bit >> 6] |= uint64_t(1) << (bit & 63);
                continue;
            }
            alignas(16) float ma[4][4], mb[4][4], ca[4], cb[4], hb[4];
            VU0_quattom4(b->quat, ma);
            VU0_quattom4(o->quat, mb);
            Float_COORD3toCOORD4(ca, b->pos);
            Float_COORD3toCOORD4(cb, p);
            const float ha[4] = {r, r, ee::mul(r, speed), 1.0f};
            PhysicsObject* other = sim.simple_owners[j];
            if (!other->collision_bounds(hb)) hb[0] = hb[1] = hb[2] = orad;
            hb[3] = 1.0f;
            OBB oa{}, ob{};
            OBB_Reset(oa, ma, ca, ha);
            OBB_Reset(ob, mb, cb, hb);
            if (OBB_CheckOBBOverlap(oa, ob)) hits.bits[bit >> 6] |= uint64_t(1) << (bit & 63);
        }
    }
}

// [driving] 0x001fde20 SimpleRigidBody::SetCanHitTrigger
void SimpleRigidBody_SetCanHitTrigger(SimpleRigidBody* b, bool can_hit) {
    if (can_hit) b->flags = uint16_t(b->flags | kSrbTouchesTriggers);
    else b->flags = uint16_t(b->flags & 0xfeff);
}

// ------------------------------------------------------------------------------------------------- a Simulation

// [driving] 0x001ff0b0 Simulation::GetSimpleRigidBody
SimpleRigidBody* GetSimpleRigidBody(Simulation& sim, int slot) {
    return sim.use_scratchpad ? &sim.scratchpad_simple[slot] : &sim.simple_bodies[slot];
}

// [driving] 0x001fef48 Simulation::AssignRigidBodySlot (corpo simples)
int AssignSimpleBodySlot(Simulation& sim, PhysicsObject* owner) {
    for (int i = 0; i < kSimpleBodySlots; ++i) {
        if (sim.simple_owners[i]) continue;
        sim.simple_sig[i] = simulation_next_signature(i) | 0x80000000u;
        sim.simple_owners[i] = owner;
        sim.simple_seq[i] = sim.next_seq++;
        ++sim.simple_count;
        return i;
    }
    return -1;
}

// [driving] 0x001ff0f0 Simulation::ReleaseRigidBodySlot (corpo simples)
void ReleaseSimpleBodySlot(Simulation& sim, int slot) {
    SimpleRigidBody_Destruct(GetSimpleRigidBody(sim, slot));
    --sim.simple_count;
    sim.simple_owners[slot] = nullptr;
    sim.simple_sig[slot] = 0;
    sim.simple_seq[slot] = 0;
}

// [driving] 0x00201408 Simulation::CanSpawnSimpleRigidBody
// Acima de 72 corpos o PS2 apaga os Newton (os pedaços que voam das explosões, +0xe38) até ficar com 72; o port não os
// cria (o vetor está sempre vazio), então nada é apagado, como no PS2 sem Newton.
bool CanSpawnSimpleRigidBody(Simulation& sim, const float /*pos*/[3], bool more) {
    const int limit = more ? 0x60 : 0x48;
    return sim.simple_count < limit;
}

// [driving] 0x002014b8 Simulation::DetectCollisionsSRB
void DetectCollisionsSRB(Simulation& sim) {
    for (int i = 0; i < kSimpleBodySlots; ++i) {
        if (!sim.simple_owners[i]) continue;
        SimpleRigidBody* b = GetSimpleRigidBody(sim, i);
        if (!SimpleRigidBody_NeedsCollisionCheck(b)) continue;
        sim.srb_hits[i] = SimCollisionMap{};
        SimpleRigidBody_CheckCollisions(b, sim.srb_hits[i]);
    }
}

// [driving] 0x00201590 Simulation::GetOrderedBody
PhysicsObject* GetOrderedBody(Simulation& sim, int index) {
    if (index < kRigidBodySlots) return sim.owners[index];
    return sim.simple_owners[index - kRigidBodySlots];
}

}  // namespace drv
