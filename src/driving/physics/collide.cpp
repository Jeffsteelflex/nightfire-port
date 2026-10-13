// Os choques de um corpo rígido com o mundo e com os outros corpos (driving/physics/collide.h), da desmontagem do
// DRIVING.ELF (out/disasm_drv_cww e out/disasm_drv_cww2 do checkout principal). A VU0 em macro onde o original a usa
// (platform/vu0_macro.h), a FPU do EE no resto (platform/ps2_float.h). As variáveis que no PS2 ficam na pilha começam
// zeradas aqui; o w delas é lixo da pilha no PS2 e só aparece no w do ponto do evento (COLLISION_INFO +0x0c) e da
// velocidade (+0x2c), que o nf_driving_check não compara.

#include "driving/physics/collide.h"

#include <cstring>
#include <memory>
#include <vector>

#include "driving/physics/collider.h"
#include "driving/physics/obb.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {

CollideEvents g_events;

// vmul.xyz + vaddy.x + vaddz.x: o produto escalar no x.
float dot3(const uint32_t* a, const uint32_t* b) {
    Reg t{};
    mul(t, a, b, XYZ);
    addbc(t, t, t, BY, X);
    addbc(t, t, t, BZ, X);
    return f(t, 0);
}
float dot3f(const float* a, const float* b) {
    Reg ra, rb;
    load(ra, a);
    load(rb, b);
    return dot3(ra, rb);
}

// vmul.xyz vf2 = v * v; vmulax.x ACC = v.x v.x; vmadday.x ACC += 1 * vf2.y; vmaddz.x vf2 = ACC + 1 * vf2.z.
float length_sq(const uint32_t* v) {
    Reg sq{}, acc{};
    const uint32_t one[4] = {0x3f800000, 0, 0, 0};
    mul(sq, v, v, XYZ);
    mulabc(acc, v, v, BX, X);
    maddabc(acc, one, sq, BY, X);
    maddbc(sq, acc, one, sq, BZ, X);
    return f(sq, 0);
}
float length_sqf(const float* v) {
    Reg r;
    load(r, v);
    return length_sq(r);
}
// O mesmo seguido de vsqrt Q e vaddq.x vf3, vf0, Q.
float length(const float* v) { return ps2::ffloat(ps2::add(0, ps2::sqrt(ps2::fbits(length_sqf(v))))); }

// lqc2 d; vmulx.xyz d, s, k: o xyz de d vira s * k (o w de d fica).
void scale_into(float d[4], const float s[4], float k) {
    Reg rd, rs;
    load(rd, d);
    load(rs, s);
    const uint32_t kv[4] = {b(k), 0, 0, 0};
    mulbc(rd, rs, kv, BX, XYZ);
    store(d, rd);
}
// vadd.xyz d, a, b e vsub.xyz d, a, b (o w de d fica).
void add_into(float d[4], const float a[4], const float bb[4]) {
    Reg rd, ra, rb;
    load(rd, d);
    load(ra, a);
    load(rb, bb);
    add(rd, ra, rb, XYZ);
    store(d, rd);
}
void sub_into(float d[4], const float a[4], const float bb[4]) {
    Reg rd, ra, rb;
    load(rd, d);
    load(ra, a);
    load(rb, bb);
    sub(rd, ra, rb, XYZ);
    store(d, rd);
}
// vopmula.xyz ACC, a, b; vopmsub.xyz d, b, a: d.xyz = a x b (o w de d fica).
void cross_into(float d[4], const float a[4], const float bb[4]) {
    Reg rd, ra, rb, acc{};
    load(rd, d);
    load(ra, a);
    load(rb, bb);
    opmula(acc, ra, rb);
    opmsub(rd, acc, rb, ra);
    store(d, rd);
}

// O começo do COLLISION_INFO que os chamadores montam na pilha: o ponto, a normal e a velocidade em (0, 0, 0, 1) (o
// quadword 0x00355ba0), os campos +0x30, +0x34, +0x40, +0x44 e os bytes/meias-palavras zerados, a vaga em +0x49 e
// o "outro" em -1. +0x38 e +0x3c ficam como estavam (lixo da pilha no PS2; zero aqui).
void init_impact(CollisionInfo& c, const RigidBody* rb) {
    std::memset(&c, 0, sizeof(c));
    c.point[3] = c.normal[3] = c.velocity[3] = 1.0f;
    c.slot = rb->slot;
    c.other = -1;
}

void raise_event(const CollisionInfo& c) {
    ++g_events.collisions;
    if (g_events.collision) g_events.collision(c);
}

PhysicsObject* owner_or_count(const RigidBody* rb) {
    PhysicsObject* o = GetOwner(rb);
    if (!o) physics_pending("dono nulo");
    return o;
}

// O passo da simulação (Simulation +0xdc4: o que ProcessStimuli recebe e o que decide os passos da colisão).
int32_t sim_steps() { return simulation().steps; }

}  // namespace

CollideEvents& collide_events() { return g_events; }

// PhysicsObject::ApplyDamage (o padrão: o objeto sem vida não muda; as bandeiras 0x20, as do PhysicsObject do jogo).
int32_t PhysicsObject::apply_damage(const float* /*from*/, const float* /*to*/, float /*amount*/, float /*split*/,
                                    int32_t /*kind*/) {
    return 0x20;
}

// [driving] 0x001fc198 RigidBody::CalculateAndApplyWorldDamage
void CalculateAndApplyWorldDamage(RigidBody* rb, const float point[3], float strength) {
    const RigidTuning& t = rigid_tuning();
    const bool vehicle = rb->type < 4;
    const float threshold = vehicle ? ps2::ffloat(0x3e4ccccdu) : ps2::ffloat(0x3d4ccccdu);  // 0,2 : 0,05
    if (!ee::lt(threshold, strength)) return;
    float damage = ee::mul(ee::add(strength, strength), t.unknown_16c);
    if (vehicle) {
        damage = ee::mul(damage, t.unknown_170);
        PhysicsObject* o = owner_or_count(rb);
        if (o && o->damage_by_player_timer() != 0) o->add_damage_by_player(damage);
    }
    if (rb->type == 1) {
        PhysicsObject* o = owner_or_count(rb);
        if (o && o->spline_path() != 0) damage = 0.0f;
    }
    if (ee::lt(0.0f, damage)) {
        PhysicsObject* o = owner_or_count(rb);
        if (o) o->apply_damage(point, rb->pos, damage, t.unknown_15c, 1);
    }
}

// [driving] 0x001fade8 RigidBody::ResolveWorldOBBCollision
bool ResolveWorldOBBCollision(RigidBody* rb, const float normal[4], const float point[4], float depth,
                              CollisionInfo* out, const float object_velocity[4], float force_scale) {
    const RigidTuning& t = rigid_tuning();
    const bool still = ee::eq(length_sqf(object_velocity), 0.0f);
    float lever[4] = {};
    sub_into(lever, point, rb->pos);
    if (rb->type < 4) {
        // O braço de um veículo: um décimo da altura do ponto, para baixo, no máximo 0,5.
        float y = ee::mul(lever[1], ps2::ffloat(0x3dcccccdu));
        lever[1] = y;
        if (ee::lt(0.0f, y)) {
            if (ee::lt(0.5f, y)) lever[1] = 0.5f;
            lever[1] = ee::neg(lever[1]);
        } else if (ee::lt(y, -0.5f)) {
            lever[1] = -0.5f;
        }
    }
    float pv[4] = {}, rel[4] = {};
    cross_into(pv, rb->ang_vel, lever);
    add_into(pv, pv, rb->vel);
    sub_into(rel, object_velocity, pv);
    const float closing = dot3f(normal, rel);
    out->kind = 1;
    out->flags = 0x10;
    out->type = 6;
    out->slot = rb->slot;
    out->tag = 0x10;
    std::memcpy(out->normal, normal, 16);
    std::memcpy(out->point, point, 16);
    out->speed = closing;
    out->strength = 0.0f;
    if (still && ee::lt(5.0f, closing)) return false;
    if (ee::lt(25.0f, closing)) return false;
    RigidBodyInfo* info = rb->info;
    if (info->flags[0] != 0) return true;
    info->flags[1] = 1;
    sub_into(rel, object_velocity, rb->vel);
    const float approach = ee::abs(dot3f(normal, rel));
    const float push = ee::mul(t.unknown_128, approach);
    const float dpush = ee::mul(depth, t.unknown_12c);
    float s = ee::lt(dpush, push) ? push : dpush;
    if (still) s = ee::mul(s, 0.25f);
    if (ee::lt(s, 1.0f)) s = 1.0f;
    const float speed_sq = length_sqf(rb->vel);
    const int32_t rate = simulation().frame_rate;
    const float object_sq = ee::mul(length_sqf(object_velocity), ee::cvt_s(rate * rate));
    const float fastest = ee::max(speed_sq, object_sq);
    float cap = ee::div(ee::mul(t.unknown_130, fastest), 2500.0f);
    cap = ee::max(cap, t.unknown_124);
    s = ee::mul(s, s);
    if (ee::lt(cap, s)) s = cap;
    float imp[4] = {};
    scale_into(imp, normal, s);
    scale_into(imp, imp, force_scale);
    const float sum = ee::add(speed_sq, object_sq);
    if (ee::lt(0.0f, sum)) {
        // Ninguém sai mais rápido que 1,25 vezes as velocidades de antes (a razão dos quadrados, no mínimo 0,15).
        float after[4] = {};
        sub_into(after, rb->momentum, imp);
        scale_into(after, after, ee::div(1.0f, rb->mass));
        const float after_sq = ee::add(length_sqf(after), object_sq);
        if (ee::lt(10.0f, after_sq)) {
            const float lim = ee::mul(sum, 1.25f);
            if (ee::lt(lim, after_sq)) {
                float ratio = ee::div(lim, after_sq);
                if (ee::lt(ratio, ps2::ffloat(0x3e19999au))) ratio = ps2::ffloat(0x3e19999au);
                scale_into(imp, imp, ratio);
            }
        }
    }
    float turn[4] = {};
    cross_into(turn, lever, imp);
    turn[1] = ee::mul(turn[1], ps2::ffloat(0x3d4ccccdu));  // 0,05
    sub_into(rb->ang_momentum, rb->ang_momentum, turn);
    sub_into(rb->momentum, rb->momentum, imp);
    scale_into(rb->vel, rb->momentum, ee::div(1.0f, rb->mass));
    {
        Reg row0, row1, row2, l, w, acc{};
        load(row0, info->inv_world_tensor[0]);
        load(row1, info->inv_world_tensor[1]);
        load(row2, info->inv_world_tensor[2]);
        load(l, rb->ang_momentum);
        load(w, rb->ang_vel);
        mulabc(acc, row0, l, BX, XYZ);
        maddabc(acc, row1, l, BY, XYZ);
        maddbc(w, acc, row2, l, BZ, XYZ);
        store(rb->ang_vel, w);
    }
    if (ee::lt(s, 0.0f)) s = ee::neg(s);
    out->strength = ee::min(ee::div(s, t.total_force_limit), 1.0f);
    return true;
}

// [driving] 0x001fc2f0 RigidBody::CollideWithWorld
void CollideWithWorld(RigidBody* rb) {
    RigidBodyInfo* info = rb->info;
    info->flags[1] = 0;
    if (!(rb->radius_flags & 1)) return;
    WorldCollision* wc = world();
    TrackWorld* tw = wc ? wc->track_world() : nullptr;
    if (!tw) {
        // Um mundo de teste (FlatGround) não tem prédios: nada bate (o port não monta o WCollider sem a grade).
        physics_pending("RigidBody::CollideWithWorld sem mundo de pista");
        return;
    }
    Simulation& sim = simulation();
    const float speed_sq = length_sqf(rb->vel);
    const int32_t step_mask = ee::lt(speed_sq, 4.0f) ? 3 : (ee::lt(speed_sq, 225.0f) ? 1 : 0);
    float radius;
    std::memcpy(&radius, &rb->radius_flags, 4);  // o +0x7c como float (os bits de baixo são as bandeiras)
    std::vector<WCollisionObject*> cylinders, boxes;
    if (rb->type == 1) WCollisionMgr_GetObjectLists(*tw, cylinders, boxes, rb->pos, radius);
    PhysicsObject* owner = owner_or_count(rb);
    if (!owner) return;
    const PhysicsAttributes* attr = rb->type < 4 ? owner_attributes(owner) : nullptr;
    const bool sub_physics = attr && attr->sub_physics == 1;
    WCollisionMgr& mgr = tw->mgr();
    mgr.barrier_mask = 0x20;
    if (!owner->collider) {
        owner->collider = std::make_shared<WCollider>();
        WCollider_Construct(*owner->collider, *tw, rb->pos, radius, false,
                            sub_physics ? (WCollider::kBarriers | WCollider::kInstances) : WCollider::kBarriers);
    }
    WCollider& collider = *owner->collider;
    WCollider_Refresh(collider, *tw, rb->pos, radius);
    mgr.barrier_mask = 0x10;
    bool wall_checked = false;
    float point[4] = {};  // sp+0x20: o último ponto também vai ao dano das caixas
    CollisionInfo impact;
    if (rb->type == 1 || (sim.steps & step_mask) == 0) {
        const float(*corner)[4] = info->corners[info->corner_buffer];
        if (rb->type < 4) owner->set_against_wall(false);
        const int count = rb->type > 3 ? 8 : 11;
        float origin[4];
        std::memcpy(origin, rb->pos, 16);  // a posição e o w (inv_inertia_x)
        for (int i = 0; i < count; ++i) {
            if (i < 8) {
                add_into(point, corner[i], origin);
            } else if (i == 8) {
                const float k = ee::add(info->half_size[2], ps2::ffloat(0x3d4ccccdu));  // + 0,05
                float tmp[4];
                std::memcpy(tmp, point, 16);
                scale_into(tmp, info->orient[2], k);
                add_into(point, rb->pos, tmp);
            } else {
                const float k = i == 9 ? info->half_size[0] : ee::neg(info->half_size[0]);
                float tmp[4];
                std::memcpy(tmp, point, 16);
                scale_into(tmp, info->orient[0], k);
                add_into(point, rb->pos, tmp);
            }
            {
                float tmp[4];
                std::memcpy(tmp, point, 16);
                scale_into(tmp, rb->vel, sim.dt);
                add_into(point, point, tmp);
            }
            float segment[8];
            std::memcpy(segment, origin, 16);
            std::memcpy(segment + 4, point, 16);
            WorldCollisionInfo hit;
            WorldCollisionInfo_Init(hit);
            mgr.barrier_mask = 0x20;
            bool collided = WCollider_GetWorldNormal(collider, *tw, segment, hit);
            if (!collided) collided = WCollisionMgr_GetCylObjectNormal(segment, cylinders, hit);
            mgr.barrier_mask = 0x10;
            if (!collided) continue;
            if (hit.face_type == 0x0e) {
                ++g_events.mission_events;
                if (g_events.mission_event) g_events.mission_event(0x15);
                continue;
            }
            // O carro do jogador encostado numa parede: uma quina da frente ou o ponto à frente bate numa face de que
            // ele não se afasta, salvo acima de 7,5 m/s.
            if (!wall_checked && rb->type == 1 && (unsigned(i - 2) < 4u || i == 8)) {
                if (ee::lt(7.5f, length(rb->vel))) wall_checked = true;
                else if (ee::le(dot3f(info->orient[2], hit.normal), 0.0f)) owner->set_against_wall(true);
            }
            hit.normal[3] = 1.0f;
            float pen[4] = {};
            sub_into(pen, segment + 4, hit.point);
            if (!(rb->type < 4 && sub_physics)) pen[1] = 0.0f;
            float push = length(pen);
            float scale = 1.0f;
            if (i >= 9) {
                scale = 0.25f;
                push = ee::mul(push, 0.25f);
            }
            if (rb->type < 4) {
                // A altura do ponto acima do centro sai, e um levante entra, no ponto onde o impulso age.
                float arm[4] = {};
                sub_into(arm, point, origin);
                const float height = dot3f(arm, info->orient[1]);
                const RigidTuning& t = rigid_tuning();
                float lift;
                if (ee::lt(length(rb->vel), 5.0f)) {
                    lift = t.unknown_154;
                } else {
                    lift = ee::max(0.5f, ee::div(ee::sub(length(rb->vel), 5.0f), 10.0f));
                    const float facing = ee::abs(dot3f(hit.normal, info->orient[2]));
                    lift = ee::lt(ps2::ffloat(0x3f51b08au), facing) ? ee::mul(lift, t.unknown_154) : 0.0f;  // 0,8191
                }
                if (attr && attr->sub_physics == 0) point[1] = ee::add(ee::sub(point[1], height), lift);
            }
            init_impact(impact, rb);
            GenerateImpulse(rb, &impact, hit.normal, point, push, true, hit.face_type, scale);
            impact.kind = uint8_t(rb->type);
            if (i < 9) CalculateAndApplyWorldDamage(rb, point, impact.strength);
            if (rb->type < 4 && i < 8) impact.other = i;
            if (hit.face_instance != 0 && rb->type < 4) {
                const WCollisionInstance* fi = nf::p32_ptr<WCollisionInstance>(hit.face_instance);
                if (WWorld_HasSceneObj(*tw, fi->render_index)) {
                    auto stim = [&](uint32_t s) {
                        ++g_events.stimuli;
                        if (g_events.stimulus) g_events.stimulus(fi->render_index, s, sim_steps());
                    };
                    stim(0x04);
                    if (ee::le(5.0f, impact.strength)) stim(0x16);
                    if (ee::le(2.5f, impact.strength)) stim(0x15);
                    if (ee::le(0.5f, impact.strength)) stim(0x14);
                    if (ee::le(ps2::ffloat(0x3dcccccdu), impact.strength)) stim(0x13);
                }
            }
            if (impact.kind == 0) ++g_events.kind_zero;  // o FUN_0010c6c8 do original (uma mensagem de depuração)
            raise_event(impact);
        }
    }
    RigidBodyScratch& scratch = rigid_scratch();
    scratch.body_box_valid = 0;
    while (!boxes.empty()) {
        float velocity[4] = {};
        uint8_t tag[2] = {0, 0};
        OBB box;
        WCollisionMgr_GetObjectOBB(*tw, boxes, velocity, tag, box);
        if (!scratch.body_box_valid) {
            OBB_Reset(scratch.body_box, info->orient, rb->pos, info->half_size);
            scratch.body_box_valid = 1;
        }
        if (OBB_CheckOBBOverlapAndFindIntersection(scratch.body_box, box)) {
            float normal[4];
            std::memcpy(normal, scratch.body_box.normal, 16);
            init_impact(impact, rb);
            ResolveWorldOBBCollision(rb, normal, scratch.body_box.contact, ee::neg(scratch.body_box.penetration),
                                     &impact, velocity, 6000.0f);
            CalculateAndApplyWorldDamage(rb, point, impact.strength);
            raise_event(impact);
        }
    }
}

// [driving] 0x001fbbd8 RigidBody::CollideWithObject
void CollideWithObject(RigidBody* rb, int slot) {
    Simulation& sim = simulation();
    RigidBodyScratch& scratch = rigid_scratch();
    const RigidTuning& t = rigid_tuning();
    scratch.body_box_valid = 0;
    for (int s = slot + 1; s < kRigidBodySlots; ++s) {
        if (!sim.owners[s]) continue;
        RigidBody* o = GetRigidBody(sim, s);
        if (o->state == 3 || o->state == 0) continue;
        float ra, rb_r;
        std::memcpy(&ra, &rb->radius_flags, 4);
        std::memcpy(&rb_r, &o->radius_flags, 4);
        float reach = ee::add(ra, rb_r);
        reach = ee::max(1.0f, ee::mul(reach, reach));
        float d[4] = {};
        sub_into(d, rb->pos, o->pos);
        if (!ee::lt(length_sqf(d), reach)) continue;
        if (!scratch.body_box_valid) {
            PhysicsObject* owner = rb->type == 1 ? owner_or_count(rb) : nullptr;
            if (owner && owner->in_two_wheel_mode()) {
                float half[4];
                std::memcpy(half, rb->info->half_size, 16);
                half[0] = ee::mul(half[0], ps2::ffloat(0x3e4ccccdu));  // 0,2
                half[1] = ee::mul(half[1], 0.25f);
                OBB_Reset(scratch.body_box, rb->info->orient, rb->pos, half);
            } else {
                OBB_Reset(scratch.body_box, rb->info->orient, rb->pos, rb->info->half_size);
            }
            scratch.body_box_valid = 1;
        }
        OBB_Reset(scratch.other_box, o->info->orient, o->pos, o->info->half_size);
        if (!OBB_CheckOBBOverlapAndFindIntersection(scratch.body_box, scratch.other_box)) continue;
        CollisionInfo impact;
        init_impact(impact, rb);
        if (!ResolveCollision(rb, rb, o, scratch.body_box.normal, scratch.body_box.contact, &impact,
                              ee::neg(scratch.body_box.penetration)))
            continue;
        if (rb->info->flags[0] != 0 || o->info->flags[0] != 0) {
            if (rb->info->flags[0] != 0) rb->state = 2;
            if (o->info->flags[0] != 0) o->state = 2;
            continue;
        }
        const float strength = impact.strength;
        float da = strength, db = strength;
        PhysicsObject* oa = owner_or_count(rb);
        if (oa && ee::lt(0.0f, strength)) oa->set_in_shock(strength);
        impact.kind = uint8_t(rb->type);
        impact.slot = rb->slot;
        PhysicsObject* ob = owner_or_count(o);
        if (ob && ee::lt(0.0f, strength)) ob->set_in_shock(strength);
        impact.type = uint8_t(o->type);
        impact.unknown_4d = uint8_t(o->slot);
        if (rb->type < 4) da = ee::add(strength, strength);
        if (o->type < 4) db = ee::add(strength, strength);
        if (!ee::lt(ps2::ffloat(0x3d4ccccdu), impact.strength)) continue;  // 0,05
        const float contact[3] = {scratch.body_box.contact[0], scratch.body_box.contact[1], scratch.body_box.contact[2]};
        float dmg = ee::mul(ee::mul(da, impact.unknown_30[0]), t.unknown_16c);
        if (rb->type < 4) {
            dmg = ee::mul(dmg, 3.0f);
            if (o->type == 1 && oa) oa->add_damage_by_player(dmg);
        }
        if (oa && ee::lt(0.0f, dmg)) impact.flags = uint16_t(oa->apply_damage(contact, rb->pos, dmg, t.unknown_168, 1));
        dmg = ee::mul(ee::mul(db, impact.unknown_30[1]), t.unknown_16c);
        if (o->type < 4) {
            dmg = ee::mul(dmg, 3.0f);
            if (rb->type == 1 && ob) ob->add_damage_by_player(dmg);
        }
        if (ob && ee::lt(0.0f, dmg)) impact.tag = uint16_t(ob->apply_damage(contact, o->pos, dmg, t.unknown_168, 1));
        raise_event(impact);
    }
}

}  // namespace drv
