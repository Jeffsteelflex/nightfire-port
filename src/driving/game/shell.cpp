// O projétil lento (o Shell, driving/game/shells.h), os efeitos e o dano no mundo das balas, do MIPS do DRIVING.ELF
// (PVehicle.obj, Simulation.obj).

#include <cstring>

#include "driving/ai/ai_vu.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/sim_random.h"
#include "driving/game/bond_car.h"
#include "driving/game/missile.h"
#include "driving/game/shells.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/real/vu0_math.h"
#include "driving/render/anim_engine.h"
#include "driving/sound/audio_internal.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;
using aivu::copy3;

namespace {

constexpr uint32_t kOne = 0x3f800000u;
// Os nomes dos sons (0x333590 os ricochetes, 0x3335a0 o metal) e os contadores do .data (0x333584 o concreto, 0x333588 o
// ricochete) e o sistema das faíscas (0x333580 já procurado, 0x3d0d4c o sistema).
const char* const kRicochet[3] = {"SFX_Ricochet1", "SFX_Ricochet2", "SFX_Ricochet3"};
const char* const kMetal[5] = {"SFX_BulletMetal1", "SFX_BulletMetal2", "SFX_BulletMetal3", "SFX_BulletMetal4",
                               "SFX_BulletMetal5"};
ShellEffectsCounters g_counters;

ShellsStats& stats() { return shells_stats(); }

// A linha de um produto vetorial (vopmula/vopmsub com o w do registro de destino = 1, o vf0 x vf0) dividida pela raiz
// de |v|² + w² (vmul.xyzw e vmaddw.x: o w = 1 entra na soma; o vmulq.xyz deixa o w).
void cross_unit_w1(float out[4], const float a[4], const float b[4]) {
    Reg va, vb, acc, r = {0, 0, 0, kOne};
    load(va, a);
    load(vb, b);
    opmula(acc, va, vb);
    opmsub(r, acc, vb, va);
    uint32_t s = ps2::mul(r[0], r[0]);
    s = ps2::madd(s, kOne, ps2::mul(r[1], r[1]));
    s = ps2::madd(s, kOne, ps2::mul(r[2], r[2]));
    s = ps2::madd(s, kOne, ps2::mul(r[3], r[3]));
    const uint32_t q = ps2::rsqrt(kOne, s);
    muls(r, r, q, XYZ);
    store(out, r);
}

bool is_snow(uint8_t b) { return b == 9 || b == 8 || b == 0x30 || b == 0x40; }

// O IS_SUB do carro do jogador (a entrada +0x1d4 da tabela virtual, GetPhysics, e o +0xc0).
bool player_is_sub(Simulation& sim) {
    BondCar* car = sim.cars.empty() ? nullptr : sim.cars[0];
    const CarPhysics* ph = car ? car->physics() : nullptr;
    if (!ph) {
        ++stats().guards;
        return false;
    }
    return ph->sub_physics != 0;
}

}  // namespace

ShellEffectsCounters& shell_effects_counters() { return g_counters; }

// [driving] 0x0019d5a8 Shell::ApplyWorldEffects
void Shell_ApplyWorldEffects(const float pos[4], const float normal[4], int32_t /*mode*/, int32_t weapon,
                             const uint8_t surface[2], int32_t damage_flags, PhysicsObject* object) {
    Simulation& sim = simulation();
    ++stats().world_effects;
    // A orientação do efeito: a normal como linha 2 e duas linhas perpendiculares (pelo (1, 0, 0) se a normal aponta
    // para cima, senão pelo (0, 1, 0)); o "para cima" é (0, 1, 0, 1) e o produto escalar inclui o w da normal.
    alignas(16) float m[4][4];
    alignas(16) float up[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    alignas(16) float n[4];
    std::memcpy(n, normal, 16);
    std::memcpy(m[2], normal, 16);
    {
        Reg a, b, r;
        load(a, n);
        load(b, up);
        mul(r, a, b, XYZW);
        addbc(r, r, r, BY, X);
        addbc(r, r, r, BZ, X);
        addbc(r, r, r, BW, X);
        if (ee::lt(ps2::ffloat(0x3f666666u), ps2::ffloat(r[0]))) {
            alignas(16) const float side[4] = {1.0f, 0.0f, 0.0f, 1.0f};
            cross_unit_w1(m[1], n, side);
            cross_unit_w1(m[0], m[1], n);
        } else {
            cross_unit_w1(m[0], up, n);
            cross_unit_w1(m[1], n, m[0]);
        }
    }
    m[3][0] = m[3][1] = m[3][2] = 0.0f;
    m[3][3] = 1.0f;
    alignas(16) float quat[4];
    VU0_m4toquat(m, quat);
    // O efeito da galeria pelo material e por quem foi atingido.
    const char* name = "bullet";
    const PhysicsObject* player = sim.cars.empty() ? nullptr : sim.cars[0];
    if (damage_flags == 0x80 || surface[0] == 0x80) {
        name = "Henbult";
    } else if (object && object == player) {
        name = player_is_sub(sim) ? nullptr : "bullethe";
    } else if (object && (object->kind() == 1 || object->kind() == 10)) {
        name = "bulletcar";
    } else {
        const uint8_t b = surface[0];
        if (is_snow(b)) {
            name = "snowbullethit";
        } else if (b == 6) {
            name = "bulletsplash";
            quat[0] = 0.0f;
            quat[1] = 1.0f;
            quat[2] = 0.0f;
            quat[3] = 0.0f;
        } else if (b == 0xc) {
            name = "bulletHut";
        } else if (b == 0xd) {
            name = "bulletcar";
        } else if (!object) {
            if (weapon == 0x1b) name = "bulletrockH";
            else if (weapon == 0x1d) name = "bulletrock";
            else if (weapon == 0x1a) name = "Bullgat";
        }
    }
    // As faíscas (o sistema "SPKB", procurado uma vez), menos na neve e no carro do jogador submarino.
    if (g_counters.sparks_found == 0) {
        g_counters.sparks = shells_env().spark_system ? shells_env().spark_system() : 0u;
        g_counters.sparks_found = 1;
        effects_note(0x001e8008, 0x53504b42u, g_counters.sparks);
    }
    if (g_counters.sparks != 0 && !is_snow(surface[0]) && (object != player || !player_is_sub(sim))) {
        // RParticleSystemManager::CreateParticles (0x001e6330; sistema, lugar, normal, 2): não portado.
        ++stats().particles;
        effects_note(0x001e6330, g_counters.sparks, ps2::fbits(pos[0]), ps2::fbits(pos[1]), ps2::fbits(pos[2]), 2);
    }
    if (name) {
        // GFX::LookupReference e GFX::TriggerIncidental(efeito, lugar, quatérnio, 0, (0, 0, 0, 1)): a galeria.
        const uint32_t ref = effects_env().gfx_lookup ? effects_env().gfx_lookup(name) : 0u;
        effects_note(0x00232f48, ref);
        ++stats().gfx;
        effects_note(0x00233108, ref, ps2::fbits(pos[0]), ps2::fbits(pos[1]), ps2::fbits(pos[2]), ps2::fbits(quat[0]),
                     ps2::fbits(quat[3]));
        // O efeito posto na galeria (seção 26; nulo nas conferências: só anotado). O 0x00355b80 e o 0x00355ba0.
        static const float kZero3[3] = {0.0f, 0.0f, 0.0f}, kQuat2[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (effects_env().gfx_trigger_incidental)
            effects_env().gfx_trigger_incidental(ref, pos, quat, kZero3, kQuat2, 0, 0, 0);
    }
    effects_note(0x001402f8, 3, ps2::fbits(pos[0]), ps2::fbits(pos[1]), ps2::fbits(pos[2]));
    AIZoneController_RegisterZone(3, pos);
    // O som do impacto: um ricochete em 25% (pelo SimRandom), senão pelo material.
    const uint8_t hi = surface[0] & 0xf0, lo = surface[0] & 0xf;
    const uint32_t r = SimRandom_Generate(sim_random());
    const char* sound;
    const char* mix;
    if (ee::lt(ee::mul(ee::cvt_s(int32_t(r)), ps2::ffloat(0x37800000u)), 0.25f)) {
        sound = kRicochet[g_counters.ricochet];
        ++g_counters.ricochet;
        if (!(g_counters.ricochet < 3)) g_counters.ricochet = 0;
        mix = "Bullet Ricochet";
    } else {
        if (hi == 0x90 || hi == 0x10 || lo == 0xd) {
            const uint32_t r2 = SimRandom_Generate(sim_random());
            sound = kMetal[int32_t(r2 * 5) >> 16];
        } else if (lo == 7 || hi == 0x20) {
            sound = "SFX_BulletWood";
        } else if (lo == 5 || hi == 0x50) {
            sound = "SFX_BulletDirt";
        } else if (hi == 0x70) {
            sound = "SFX_BulletGlass";
        } else if (hi == 0x80) {
            sound = "SFX_BulletFlesh";
        } else {
            sound = (g_counters.concrete & 1) ? "SFX_BulletConcrete2" : "SFX_BulletConcrete1";
            ++g_counters.concrete;
        }
        mix = "Bullet Impact";
    }
    ABank* bank = ABank_Category(4);
    if (!bank) {
        ++stats().guards;  // o PS2 leria o banco nulo
        return;
    }
    const int32_t patch = bank->index.Lookup(sound);
    effects_note(0x002f5880, uint32_t(patch));
    AOneShotSound* s = new AOneShotSound(mix, bank->snd_bank, patch, 1.0f);
    std::memcpy(s->pos, pos, 12);
    s->max_distance2 = 2500.0f;
    s->min_distance = 10.0f;
    s->max_distance = 50.0f;
    ++stats().sounds;
}

// [driving] 0x0019ddb8 Shell::ApplyWorldDamage
void Shell_ApplyWorldDamage(float damage, float size, const WWorldPos& /*wp*/, int32_t decal,
                            const WorldCollisionInfo& info) {
    if (info.flag52 == 0 && info.face_type != 0x70 && (info.face_subtype & 2) == 0) {
        // RScorchWorld::ScorchWorld (0x001ea8e8; tamanho, WWorldPos &, ponto, normal, decalque): não portado.
        ++stats().scorches;
        effects_note(0x001ea8e8, ps2::fbits(size), uint32_t(decal), ps2::fbits(info.point[0]),
                     ps2::fbits(info.point[1]), ps2::fbits(info.point[2]));
    }
    if (info.face_instance == 0) return;
    // WCollisionInstance::GetRenderInstance (o fgWorld +0x20 + o índice +0x1a x 0x40) e WWorld::GetSceneObjFromInstance.
    WorldCollision* w = world();
    TrackWorld* tw = w ? w->track_world() : nullptr;
    if (!tw) {
        ++stats().guards;
        return;
    }
    const WCollisionInstance& ci = *nf::p32_ptr<const WCollisionInstance>(info.face_instance);
    const uint32_t render = nf::p32_u(tw->render_instances()) + uint32_t(ci.render_index) * 0x40u;
    if (render == 0) return;
    if (ci.render_index >= tw->render_instance_count() || !WWorld_HasSceneObj(*tw, ci.render_index)) return;
    const uint32_t scene = shells_env().scene_obj_of ? shells_env().scene_obj_of(render) : 0u;
    if (scene == 0) return;
    uint32_t handle;
    std::memcpy(&handle, nf::p32_ptr<const uint8_t>(scene) + 0x28, 4);
    const uint32_t steps = uint32_t(simulation().steps);
    auto stim = [&](uint32_t s) {
        ++stats().stimuli;
        effects_note(0x001a2868, handle, s, steps, 2);
        if (shells_env().stimuli) shells_env().stimuli(handle, s, steps, 2);
        else if (handle) AnimHandle_ProcessStimuliAll(handle, s, steps, 2);
        else ++stats().guards;
    };
    if (ee::le(500.0f, damage)) stim(0x16);
    if (ee::le(250.0f, damage)) stim(0x15);
    if (ee::le(50.0f, damage)) stim(0x14);
    if (ee::le(10.0f, damage)) stim(0x13);
}

// ------------------------------------------------------------------------------------------------- o Shell

// [driving] 0x0019d028 Shell::~Shell (e o ~PhysicsObject 0x00195568: a vaga do corpo simples volta)
Shell::~Shell() {
    Simulation& sim = simulation();
    if (po.slot >= 0 && po.slot < kSimpleBodySlots && sim.simple_owners[po.slot] == this)
        ReleaseSimpleBodySlot(sim, po.slot);
}

void Shell::simulate() { Shell_Simulate(*this); }

uint32_t Shell::raw_word(uint32_t offset) {
    if (offset < 0x6c || offset + 4 > 0x90) return 0;
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t*>(&d) + (offset - 0x6c), 4);
    return v;
}

// [driving] 0x0019cd90 Shell::Shell
void Shell_Construct(Shell& s, float damage, float explosion, int32_t weapon, const float dir[3], const float pos[3],
                     uint32_t sig, int32_t streak) {
    Simulation& sim = simulation();
    // A parte do PhysicsObject::PhysicsObject (0x00195470; tipo 7, corpo simples; o AttributeSet "Shell" de "weapon")
    s.po = PhysicsObjectWords{};
    s.po.type = 7;
    s.po.flags = 1;
    s.po.slot = int16_t(AssignSimpleBodySlot(sim, &s));
    s.po.owner_sig = sig;
    s.po.vtable = 0x0037ccf8;
    ShellData& d = s.d;
    d = ShellData{};
    d.damage = damage;
    d.explosion = explosion;
    d.weapon = weapon;
    d.streak = streak;
    alignas(16) float vel[4] = {dir[0], dir[1], dir[2], 0.0f};  // sp+0x10 (o w: lixo da pilha)
    // A vida e a velocidade sorteadas pelo RRandom do render.
    RRandom& rr = render_random();
    const float rate = ee::cvt_s(sim.frame_rate);
    const uint32_t p = rr.seed * rr.multiplier;
    rr.seed = p & 0xffffu;
    const float r = ee::cvt_s(int32_t((p >> 8) & 0xffffu));
    float speed;
    if (weapon == 0x10) {
        d.life = ee::cvt_w(rate);
        speed = ee::add(ee::mul(ee::mul(r, 30.0f), ps2::ffloat(0x37800000u)), 170.0f);
    } else {
        d.life = ee::cvt_w(ee::mul(rate, ps2::ffloat(0x3fa66666u)));  // 1,3 s
        speed = ee::add(ee::mul(ee::mul(r, 30.0f), ps2::ffloat(0x37800000u)), 200.0f);
    }
    if (streak >= 0) {
        // A velocidade do tipo de rastro (a tabela do RBulletStreak::Init, 0x423f38 +0x10).
        float v = 0.0f;
        if (shells_env().streak_speed && shells_env().streak_speed(streak, v)) speed = v;
        else ++stats().guards;  // a tabela do render não veio: fica a velocidade sorteada
    }
    aivu::scale3(vel, vel, speed);
    alignas(16) float orient[4][4];
    Util_GenerateMatrix(orient, dir);
    copy3(d.start, pos);
    if (SimpleRigidBody* b = GetSimpleRigidBody(sim, s.po.slot)) {
        static const float kZero[3] = {0.0f, 0.0f, 0.0f};  // 0x355b80
        SimpleRigidBody_Construct(b, ps2::ffloat(0x3e99999au), ps2::ffloat(0x3dcccccdu), s.po.slot, kSimpleShell, pos,
                                  vel, kZero, orient);
    }
    GetSimpleRigidBody(sim, s.po.slot)->flags |= 0x169;
}

// [driving] 0x001fffa8 Simulation::SpawnShellObject
Shell* SpawnShellObject(Simulation& sim, float damage, float explosion, int32_t weapon, const float dir[3],
                        const float pos[3], uint32_t sig, int32_t streak) {
    if (!CanSpawnSimpleRigidBody(sim, pos, false)) {
        ++stats().shells_refused;
        return nullptr;
    }
    Shell* s = new Shell();
    Shell_Construct(*s, damage, explosion, weapon, dir, pos, sig, streak);
    SpawnPhysicsObject(sim, s);
    ++stats().shells_spawned;
    return s;
}

// [driving] 0x00201a40 Simulation::DeleteShellObject
void DeleteShellObject(Simulation& sim, Shell* s) {
    ++stats().shells_deleted;
    DeletePhysicsObject(sim, s);
}

// [driving] 0x0019d080 Shell::Simulate
void Shell_Simulate(Shell& s) {
    Simulation& sim = simulation();
    SimpleRigidBody* b = GetSimpleRigidBody(sim, s.po.slot);
    ShellData& d = s.d;
    ++stats().shells_simulated;
    d.life -= 1;
    if (d.life < 1) {
        DeleteShellObject(sim, &s);
        PhysicsObject_Simulate(&s);
        return;
    }
    const SimCollisionMap& hits = sim.srb_hits[b->slot];
    for (int i = 0; i < 0x90; ++i) {
        PhysicsObject* o = GetOrderedBody(sim, i);
        if (!((hits.bits[i >> 6] >> (i & 63)) & 1)) continue;
        if (PhysicsObject_IsOwnedBy(&s, o)) continue;
        if (!o) {
            ++stats().guards;  // o PS2 chamaria a tabela virtual do dono nulo
            continue;
        }
        ++stats().shell_hits;
        const float* opos = PhysicsObject_GetPosition(o);
        // O ApplyDamage do atingido (1/4 de divisão, o tipo 2, a assinatura nula 0x339188: não é do jogador).
        ++stats().damage_calls;
        effects_note(0x00189598, ps2::fbits(d.damage), uint32_t(object_ordered_index(sim, o)), 2);
        const bool saved = damage_source_is_player();
        damage_source_is_player() = false;
        const bool at_player = o == GetPlayerObject(sim) && FindPhysicsObjectSignature(sim, s.po.owner_sig) != o;
        const float hp_before = at_player ? o->hit_points() : 0.0f;  // (só a conta do port)
        const int32_t flags = o->apply_damage(d.start, b->pos, d.damage, 0.25f, 2);
        if (at_player) {
            ++stats().player_hits;
            stats().player_damage += double(hp_before) - double(o->hit_points());
        }
        damage_source_is_player() = saved;
        alignas(16) const float at[4] = {opos[0], opos[1], opos[2], 1.0f};
        const float* ov = PhysicsObject_GetLinearVelocity(o);
        alignas(16) const float vel[4] = {ov[0], ov[1], ov[2], 1.0f};
        if (ee::lt(1.0f, d.explosion)) {
            alignas(16) float fwd[4] = {};
            srb_quat_z_axis(b->quat, fwd);
            SpawnExplosion(sim, d.explosion, 2, d.weapon, fwd, b->pos, 0, o);
            const uint8_t surface[2] = {0, 0};
            Shell_ApplyWorldEffects(at, vel, 2, d.weapon, surface, 0, nullptr);
        } else {
            const uint8_t surface[2] = {0, 0};
            Shell_ApplyWorldEffects(at, vel, 1, d.weapon, surface, flags, nullptr);
        }
        if (PhysicsObject_IsOwnedBy(&s, GetPlayerObject(sim)) && o->kind() == 1) o->add_damage_by_player(d.damage);
        PhysicsObject_Simulate(&s);
        DeleteShellObject(sim, &s);
        return;
    }
    // O mundo entre o lugar e o do próximo passo.
    alignas(16) float seg[2][4] = {{b->pos[0], b->pos[1], b->pos[2], 1.0f}, {}};
    alignas(16) float step[4] = {b->vel[0], b->vel[1], b->vel[2], 1.0f};
    for (int i = 0; i < 4; ++i) step[i] = ee::mul(step[i], sim.dt);  // vmulx.xyzw
    for (int i = 0; i < 4; ++i) seg[1][i] = ee::add(seg[0][i], step[i]);
    WorldCollisionInfo info;
    WorldCollisionInfo_Init(info);
    WCollisionMgr_CheckHitWorld(&seg[0][0], info);
    if (info.hit != 0) {
        WCollisionMgr_CheckHitWindow(info, true, 0);
        if (ee::lt(1.0f, d.explosion)) {
            alignas(16) float fwd[4] = {};
            srb_quat_z_axis(b->quat, fwd);
            SpawnExplosion(sim, d.explosion, 2, d.weapon, fwd, info.point, 0, nullptr);
            Shell_ApplyWorldEffects(info.point, info.normal, 2, d.weapon, &info.face_type, 0, nullptr);
        } else {
            Shell_ApplyWorldEffects(info.point, info.normal, 1, d.weapon, &info.face_type, 0, nullptr);
        }
        Shell_ApplyWorldDamage(d.damage, ee::mul(d.damage, ps2::ffloat(0x3ba3d70au)), s.world_pos_, 5, info);
        DeleteShellObject(sim, &s);
    }
    PhysicsObject_Simulate(&s);
}

}  // namespace drv
