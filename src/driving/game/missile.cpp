// O míssil (driving/game/missile.h), do MIPS do DRIVING.ELF (Missile.obj, Simulation.obj, BondAudioPS2.lib).

#include "driving/game/missile.h"

#include <cstring>
#include <set>

#include "driving/ai/sim_random.h"
#include "driving/anim/eagl_anim.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/game/shells.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/real/vu0_math.h"
#include "driving/sound/audio_internal.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

// ASound (0x120 bytes; 0x002ed3d0 o construtor, 0x002ed518 Play, 0x002ed5a0 o destrutor): um timbre em laço que segue a
// posição que o dono escreve (o voo do míssil).
class ASound : public ABaseSound {
public:
    AVoice voice;  // +0xb0
    // [driving] 0x002ed3d0 ASound::ASound(int banco, int timbre, char *mistura)
    ASound(const char* mix_name, int32_t bank, int32_t patch) : ABaseSound(mix_name, 4), voice(mix, bank, patch) {
        volume = 1.0f;
        pitch = 1.0f;
    }
    // [driving] 0x002ed518 ASound::Play(APath &)
    void Play(APath& path) override {
        const int32_t view = path.listener->view;
        const float mv = AMix_GetVolume(mix);
        AVoice_Play(ee::mul(ee::mul(volume, mv), path.volume), ee::mul(pitch, path.pitch), path.azimuth, path.delay,
                    0.0f, &voice, view);
    }
};

namespace {

MissileStats g_stats;
// Os globais do Missile.obj (.data 0x331918..0x331950).
const float kSteerScale = 0.004f;      // 0x331928
const float kSteerRate = 0.005f;       // 0x33192c
const float kMissile1fSpeed = 34.5f;   // 0x331930 (o tipo 0x1f)
const float kMissile1fAccel = 7.2f;    // 0x331934
const float kMissile1fExtra = 3.0f;    // 0x331938
const float kMissile1fTurn = 2.0f;     // 0x33193c
const int32_t kMissile1fLife = 6;      // 0x331940
const float kGroundOffset = 0.4f;      // 0x331944
const float kGroundMax = 5.0f;         // 0x331948
MissileGlobals g_globals;
TargetPickerData g_picker;
TargetGlobals g_picker_globals;
int32_t& g_wiggle_toggle = g_globals.wiggle_toggle;  // 0x33194c
float& g_pitch = g_globals.pitch;                    // 0x331950

const uint32_t kVf0[4] = {0, 0, 0, 0x3f800000};

uint32_t vu_len2(const float v[3]) {
    const uint32_t x = ps2::fbits(v[0]), y = ps2::fbits(v[1]), z = ps2::fbits(v[2]);
    uint32_t acc = ps2::mul(x, x);
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(y, y));
    return ps2::madd(acc, 0x3f800000u, ps2::mul(z, z));
}

// vmul.xyz/vmulax/vmadday/vmaddz, vrsqrt Q, vmulq.xyz: o vetor unitário (a pista w de `out` fica).
void vu_normalize(const float in[4], float out[4]) {
    Reg v;
    load(v, in);
    const uint32_t q = ps2::rsqrt(0x3f800000u, vu_len2(in));
    Reg o;
    load(o, out);
    muls(o, v, q, XYZ);
    store(out, o);
}

// O x·y + y·y + z·z da VU0 (vmul.xyz, vaddy.x, vaddz.x).
float vu_dot(const float a[4], const float b[4]) {
    Reg v1, v2, v3;
    load(v1, a);
    load(v2, b);
    mul(v3, v1, v2, XYZ);
    addbc(v3, v3, v3, BY, X);
    addbc(v3, v3, v3, BZ, X);
    return ps2::ffloat(v3[0]);
}

// O seno e o cosseno da RCameraMath (0x001a7098, 0x001a70d8): o ângulo negativo ganha uma volta.
float cam_sin(float t) { return VU0_Sin(ee::lt(t, 0.0f) ? ee::add(t, 1.0f) : t); }
float cam_cos(float t) { return VU0_Cos(ee::lt(t, 0.0f) ? ee::add(t, 1.0f) : t); }

// [driving] 0x001a72b8 RCameraMath::BuildRotationQuat(COORD4 &, COORD3 &eixo, float voltas) e 0x001a7360
// BuildRotationMat4(MATRIX4 &, COORD3 &, float).
void build_rotation_mat4(float m[4][4], const float axis[4], float turns) {
    const float h = ee::mul(turns, 0.5f);
    alignas(16) float q[4] = {0, 0, 0, 0};
    Reg v1, v2, v3;
    load(v1, axis);
    load(v3, q);
    v2[0] = ps2::fbits(cam_sin(h));
    mulbc(v3, v1, v2, BX, XYZ);
    store(q, v3);
    q[3] = cam_cos(h);
    Reg acc, sq, one = {0x3f800000, 0, 0, 0};
    load(v1, q);
    mul(sq, v1, v1, XYZW);
    mulabc(acc, v1, v1, BX, X);
    maddabc(acc, one, sq, BY, X);
    maddabc(acc, one, sq, BZ, X);
    maddbc(sq, acc, one, sq, BW, X);
    muls(v1, v1, ps2::rsqrt(0x3f800000u, sq[0]), XYZW);
    store(q, v1);
    VU0_quattom4(q, m);
}

// O PhysicsObject de uma assinatura (Simulation::FindPhysicsObjectSignature 0x001ff280): a vaga da assinatura (o byte
// de cima sem o bit 31) e o dono, se a assinatura dele ainda é essa.
PhysicsObject* find_signature(Simulation& sim, uint32_t sig) {
    if (sig == 0) return nullptr;
    const int slot = int((sig >> 24) & 0x7f);
    if (sig & 0x80000000u) {
        if (slot < kSimpleBodySlots && sim.simple_sig[slot] == sig) return sim.simple_owners[slot];
        return nullptr;
    }
    if (slot < kRigidBodySlots && sim.rigid_sig[slot] == sig) return sim.owners[slot];
    return nullptr;
}

// strcasecmp == 0 (o segundo em minúsculas)
bool same_nocase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return false;
        if (!ca) return true;
    }
}

PhysicsObject* player_object(Simulation& sim) {
    if (sim.cars.empty()) {
        ++g_stats.guards;  // o PS2 leria o primeiro de um vetor vazio
        return nullptr;
    }
    return sim.cars[0];
}

// [driving] 0x00195c70 PhysicsObject::IsOwnedBy(PhysicsObject *): o outro está na corrente dos donos (a assinatura +0x58).
bool is_owned_by(Simulation& sim, SimpleObject& self, PhysicsObject* other) {
    if (!other) return false;
    const uint32_t want = PhysicsObject_GetSig(other);
    uint32_t sig = self.po.owner_sig;
    if (sig == 0) return false;
    PhysicsObject* o = find_signature(sim, sig);
    while (o) {
        if (PhysicsObject_GetSig(o) == want) return true;
        SimpleObject* s = dynamic_cast<SimpleObject*>(o);
        const uint32_t next = s ? s->po.owner_sig : 0;  // os donos rígidos do port não guardam o +0x58: zero
        PhysicsObject* n = find_signature(sim, next);
        if (n == o) return false;
        o = n;
    }
    return false;
}

// [driving] 0x002299b0 WTargetable::GetVelocity(COORD3 &): verdadeiro com a velocidade; o veículo da IA (o tipo 3 com
// o AIVehicle +0x58) não foi portado (falso, contado).
bool target_velocity(const WTargetableData& t, float out[3]) {
    if (t.kind == 1) {
        if (!t.object) return false;
        PhysicsObject* o = nf::p32_ptr<PhysicsObject>(t.object);
        Simulation& sim = simulation();
        if (SimpleObject* s = dynamic_cast<SimpleObject*>(o)) {
            std::memcpy(out, GetSimpleRigidBody(sim, s->po.slot)->vel, 12);
        } else {
            for (int i = 0; i < kRigidBodySlots; ++i)
                if (sim.owners[i] == o) {
                    std::memcpy(out, GetRigidBody(sim, i)->vel, 12);
                    return true;
                }
            ++g_stats.guards;
            return false;
        }
        return true;
    }
    if (t.kind == 0) {
        out[0] = out[1] = out[2] = 0.0f;
        return true;
    }
    if (t.kind != 3) return false;
    if (!t.object || nf::p32_ptr<const uint32_t>(t.object)[0x16] == 0) {
        out[0] = out[1] = out[2] = 0.0f;
        return true;
    }
    ++g_stats.guards;  // AIVehicle::GetPhysicsObject (o veículo da IA)
    return false;
}

// Os WTargetable que o port criou (WTargetPicker::GetWorldTarget): só esses ele apaga (os outros são de quem os criou).
std::set<uint32_t>& owned_targets() {
    static std::set<uint32_t> s;
    return s;
}

// [driving] 0x00229ba0 WTargetable::AddReference e 0x00229bb0 RemoveReference: com zero referências o alvo é apagado
// (0x002296a0 o destrutor: os vivos 0x33a960 menos um e o FastFree).
void target_add(uint32_t t) { ++nf::p32_ptr<WTargetableData>(t)->refs; }
void target_remove(uint32_t t) {
    WTargetableData& w = *nf::p32_ptr<WTargetableData>(t);
    --w.refs;
    if (w.refs == 0) {
        --g_picker_globals.live;
        if (owned_targets().erase(t)) delete &w;
    }
}

}  // namespace

MissileGlobals& missile_globals() { return g_globals; }
TargetPickerData& target_picker() { return g_picker; }
TargetGlobals& target_globals() { return g_picker_globals; }

void WTargetable_AddReference(uint32_t t) { target_add(t); }
void WTargetable_RemoveReference(uint32_t t) { target_remove(t); }
void WTargetable_Adopt(uint32_t t) { owned_targets().insert(t); }

// O WTargetPicker::UpdateSelection inteiro (0x0022aab0) está em game/target_picker.cpp; este é o caminho antigo da
// seção 22 (o alvo do mundo), que a parte 18 do nf_driving_check confere.
void WTargetPicker_UpdateWorldTarget() {
    Simulation& sim = simulation();
    TargetPickerData& p = g_picker;
    ++p.updates;
    if (p.mode == 1) {
        // A direção automática: a câmera + GetForwardAimVec4 x 1000 (o RPlayerCamera não tem a mira: contado).
        ++p.pending;
        return;
    }
    if (sim.cars.empty()) {
        ++p.guards;  // o PS2 leria o primeiro de um vetor vazio
        return;
    }
    BondCar* car = sim.cars[0];
    const RigidBody* rb = GetRigidBody(sim, car->d.slot);
    // O lugar do corpo (w 1) mais a frente (a linha 2 da orientação) x 175 (vmulx.xyz e vadd.xyz: o w fica o do +0x10).
    alignas(16) float seg[2][4] = {{rb->pos[0], rb->pos[1], rb->pos[2], 1.0f}, {}};
    Reg vpos, vfwd, vk, vt{}, vold;
    load(vpos, seg[0]);
    load(vfwd, rb->info->orient[2]);
    load(vold, p.world_target);
    vk[0] = 0x432f0000u;  // 175
    mulbc(vt, vfwd, vk, BX, XYZ);
    add(vold, vpos, vt, XYZ);
    store(p.world_target, vold);
    // Num submarino (SUB_PHYSICS, a física +0xc0) o alvo é o choque do segmento com o mundo, se não for de uma instância
    // com o bit 1 das bandeiras.
    const CarPhysics* ph = car->physics();
    if (ph && ph->sub_physics != 0) {
        seg[1][0] = p.world_target[0];
        seg[1][1] = p.world_target[1];
        seg[1][2] = p.world_target[2];
        seg[1][3] = 1.0f;
        WorldCollisionInfo info;
        WorldCollisionInfo_Init(info);
        if (WCollisionMgr_CheckHitWorld(&seg[0][0], info)) {
            uint32_t flags = 0;
            if (info.face_instance) flags = nf::p32_ptr<const WCollisionInstance>(info.face_instance)->flags;
            else ++p.guards;  // o PS2 leria o +0x18 do endereço 0 (a instância nula de um objeto)
            if ((flags & 2) == 0) std::memcpy(p.world_target, info.point, 12);
        }
    }
}

// O WTargetable num ponto (0x00229490, com o CommonInit e o WSimpleZone::SetPosition) é o de game/target_picker.cpp
// (conferido na parte 20).
uint32_t WTargetable_NewAtPoint(const float pos[3]) { return WTargetable_NewPoint(pos[0], pos[1], pos[2]); }

void WTargetable_AddRef(uint32_t t) { target_add(t); }

// [driving] 0x0022af58 WTargetPicker::GetWorldTarget: um WTargetable novo no alvo do mundo (+0x38 = 1).
uint32_t WTargetPicker_GetWorldTarget() {
    const uint32_t p = WTargetable_NewAtPoint(g_picker.world_target);
    nf::p32_ptr<WTargetableData>(p)->fixed = 1;
    return p;
}

void Missile_DriveMissile(uint32_t missile) {
    effects_note(0x002064d8, missile ? 1u : 0u);
    if (mission_exists()) SMissionManager_SetDriveMissile(mission_manager(), missile);
}
MissileStats& missile_stats() { return g_stats; }

// ------------------------------------------------------------------------------------------------- o Missile

// [driving] 0x001818c8 Missile::~Missile (e o ~PhysicsObject 0x00195568)
Missile::~Missile() {
    if (sound) sound->destroy();  // a entrada +0xc da tabela do som com 3
    sound = nullptr;
    if (d.target) target_remove(d.target);
    Simulation& sim = simulation();
    if (po.slot >= 0 && po.slot < kSimpleBodySlots && sim.simple_owners[po.slot] == this)
        ReleaseSimpleBodySlot(sim, po.slot);
    ++g_stats.deleted;
}

void Missile::simulate() { Missile_Simulate(*this); }

// O RSceneObj do míssil (a instância do carro): GetBoundingDimensions dele. O port não guarda as medidas da instância;
// o CheckCollisions só pergunta pelos outros corpos simples, e os mísseis não se testam entre si (sem o bit 0x20/0x80).
bool Missile::collision_bounds(float /*half*/[4]) {
    ++g_stats.guards;
    return false;
}

// O +0x13c do Missile do PS2 (o rastro: +0xa0 + 0x9c, a palavra y do nono ponto), que o CheckCollisions lê pelo quirk.
uint32_t Missile::raw_word(uint32_t offset) {
    if (offset < 0x6c || offset + 4 > 0x730) return 0;
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t*>(&d) + (offset - 0x6c), 4);
    return v;
}

// [driving] 0x00180bc8 Missile::Missile
void Missile_Construct(Missile& m, float speed, float damage, int32_t weapon, const float dir[3], const float pos[3],
                       uint32_t sig, uint32_t target, int32_t system) {
    Simulation& sim = simulation();
    // [driving] 0x00195470 PhysicsObject::PhysicsObject (tipo 6, corpo simples; o AttributeSet "Missile" de "weapon")
    m.po = PhysicsObjectWords{};
    m.po.type = 6;
    m.po.flags = 1;
    m.po.slot = int16_t(AssignSimpleBodySlot(sim, &m));
    m.po.owner_sig = sig;
    m.po.vtable = 0x00378748;
    MissileData& d = m.d;
    d = MissileData{};
    d.target = target;
    d.unknown7c = 0.0f;
    bool not_player = true;   // sp+0xb8
    PhysicsObject* car = nullptr;  // s4: o dono, se é um veículo (tipo 1)
    float radius = 0.3f;      // f22
    d.decay_time = sim.frame_rate << 1;
    d.start_step = sim.steps;
    // [driving] 0x001e4200 RMissileStreak::RMissileStreak: as três primeiras palavras zeradas.
    d.streak.count = d.streak.tick = d.streak.kind = 0;
    d.damage = damage;
    d.weapon = weapon;
    d.detonate = d.decaying = 0;
    d.volume = 0.0f;
    d.steer_scale = kSteerScale;
    d.steer[0] = 0.0f;
    d.beep_time = sim.steps + 5;
    d.steer[1] = d.steer_target[0] = d.steer_target[1] = 0.0f;
    d.arm_time = sim.steps;
    alignas(16) float orient[4][4];
    Util_GenerateMatrix(orient, dir);
    if (PhysicsObject* owner = find_signature(sim, sig)) {
        not_player = owner != player_object(sim);
        if (owner->kind() == 1) car = owner;
    }
    d.streak.kind = weapon == 0x19 ? 1 : 0;
    const char* sound_name = "SFX_RocketTrans";
    const float rate = ee::cvt_s(sim.frame_rate);
    float v = 0.0f;  // f20: a velocidade de saída
    auto world_target = [&]() {
        // Sem alvo, o alvo do mundo do WTargetPicker (o ponto 175 m à frente do carro do jogador).
        if (d.target == 0) d.target = WTargetPicker_GetWorldTarget();
    };
    int32_t life = 0;
    bool set_life = true;
    switch (weapon) {
    case 0xe:  // o EMP
        world_target();
        d.max_speed = 175.0f;
        d.accel = 17.5f;
        d.turn = 1.0f;
        sound_name = "SFX_EMPtrans";
        d.locked = 0;
        v = ee::add(speed, 4.0f);
        d.straight_time = ee::cvt_w(ee::mul(rate, ps2::ffloat(0x3ea8f5c3)));  // 0,33
        d.life = sim.frame_rate * 5;
        d.decay_time = sim.frame_rate;
        set_life = false;
        break;
    case 0x16:
        d.accel = 4.0f;
        d.max_speed = 40.0f;
        d.turn = 1.0f;
        d.target = 0;
        sound_name = "SFX_vgtorpTrans";
        v = 8.0f;
        d.locked = 0;
        d.straight_time = ee::cvt_w(ee::mul(rate, ps2::ffloat(0x3ea8f5c3)));
        d.streak.kind = 2;
        life = sim.frame_rate * 10;
        break;
    case 0x15: {  // os torpedos
        world_target();
        float a = 50.0f, b = 25.0f, c = 0.1f, e = -1.0f;
        int32_t l = sim.frame_rate * 5;
        if (!not_player || !car) {
        } else if (!same_nocase(car->name(), "alpha_sub")) {
            a = 20.0f, b = 10.0f, c = ps2::ffloat(0x3cf5c28f), e = 0.8f;
        } else {
            bool near = false;
            if (d.target) {
                const WTargetableData& t = *nf::p32_ptr<const WTargetableData>(d.target);
                const float dd[3] = {ee::sub(pos[0], t.pos[0]), ee::sub(pos[1], t.pos[1]), ee::sub(pos[2], t.pos[2])};
                near = ee::lt(ps2::ffloat(vu_len2(dd)), 10000.0f);
            }
            if (near) {
                d.damage = 2000.0f;
                a = 60.0f, b = 30.0f, c = 1.0f, e = -1.0f;
            } else {
                d.damage = 750.0f;
                a = 40.0f, b = 12.0f, c = ps2::ffloat(0x3c23d70a), e = 0.8f;
            }
            l = sim.frame_rate << 3;
        }
        d.max_speed = a;
        d.accel = b;
        d.turn = c;
        d.unknown7c = e;
        d.life = l;
        set_life = false;
        d.straight_time = 0;
        d.locked = 1;
        v = ee::add(speed, 1.0f);
        sound_name = not_player ? "SFX_htorpedotrans" : "SFX_vtorpedoTrans";
        d.streak.kind = 2;
        break;
    }
    case 2: {  // o míssil (o Vanquish)
        world_target();
        d.max_speed = 250.0f;
        d.accel = 50.0f;
        d.turn = 1.0f;
        sound_name = "SFX_MissleTrans";
        d.locked = 0;
        v = ee::add(speed, 7.0f);
        d.straight_time = ee::cvt_w(ee::mul(rate, ps2::ffloat(0x3e4ccccd)));  // 0,2
        d.life = sim.frame_rate << 2;
        set_life = false;
        if (sig & 0x80000000u) {
            PhysicsObject* o = sim.simple_owners[(sig >> 24) & 0x7f];
            const int32_t k = o ? o->kind() : 0;
            if (!o) ++g_stats.guards;
            if (k == 10) {
                d.accel = 100.0f;
                d.locked = 1;
                d.arm_time = ee::cvt_w(ee::add(ee::cvt_s(d.arm_time), ee::mul(rate, 0.5f)));
            } else if (k == 11) {
                const uint32_t r0 = SimRandom_Generate(sim_random());
                const uint32_t r1 = SimRandom_Generate(sim_random());
                alignas(16) float rnd[4] = {};
                // [driving] 0x002d0c48 BytesToCoord (xyzw): os quatro bytes / 255.
                const uint32_t bytes = (~r0 << 16) | r1;
                const float raw[4] = {ee::cvt_s(int32_t(bytes >> 24)), ee::cvt_s(int32_t((bytes >> 16) & 0xff)),
                                      ee::cvt_s(int32_t((bytes >> 8) & 0xff)), ee::cvt_s(int32_t(bytes & 0xff))};
                for (int i = 0; i < 4; ++i) rnd[i] = ee::mul(raw[i], ps2::ffloat(0x3b808081));
                const float w2 = ee::add(rnd[3], rnd[3]);
                d.straight_time = ee::cvt_w(ee::mul(ee::cvt_s(d.straight_time), w2));
                d.accel = ee::mul(d.accel, ee::add(rnd[0], 1.25f));
                d.turn = ee::mul(d.turn, ee::add(rnd[1], 0.25f));
                const float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};  // 0x378580
                const float dv[4] = {dir[0], dir[1], dir[2], 0.0f};
                if (ee::lt(ps2::ffloat(0x3f733333), vu_dot(up, dv))) {
                    alignas(16) float r[4][4];
                    VU0_MATRIX4setyrot(r, rnd[2]);
                    VU0_MATRIX4_mult(orient, r, orient);
                }
            }
        }
        break;
    }
    case 0x19:
        d.turn = 1.0f;
        sound_name = "";
        d.max_speed = 250.0f;
        d.accel = 125.0f;
        d.locked = 1;
        d.target = 0;
        d.straight_time = 0;
        v = ee::add(speed, 5.0f);
        life = sim.frame_rate * 5;
        break;
    case 0x1c:
        world_target();
        d.max_speed = 800.0f;
        d.accel = 80.0f;
        d.turn = 1.0f;
        v = 10.0f;
        d.locked = 1;
        d.straight_time = ee::cvt_w(ee::mul(rate, 0.5f));
        sound_name = "SFX_UMissileTrans";
        life = sim.frame_rate * 5;
        break;
    case 0x1f:
        d.max_speed = kMissile1fSpeed;
        d.accel = kMissile1fAccel;
        d.turn = ee::mul(kMissile1fTurn, ps2::ffloat(0x3d4ccccd));  // 0,05
        d.target = 0;
        sound_name = "SFX_MissleTrans";
        v = ee::add(speed, kMissile1fExtra);
        d.locked = 1;
        d.straight_time = ee::cvt_w(ee::mul(rate, ps2::ffloat(0x3dcccccd)));
        d.arm_time += 6;
        life = sim.frame_rate * kMissile1fLife;
        break;
    case 1: {  // o foguete
        d.accel = 125.0f;
        d.turn = 1.0f;
        d.max_speed = 250.0f;
        d.target = 0;
        d.locked = 1;
        d.straight_time = ee::cvt_w(ee::mul(rate, ps2::ffloat(0x3e4ccccd)));
        v = ee::add(speed, 5.0f);
        d.life = sim.frame_rate * 5;
        set_life = false;
        const char* level = effects_env().level ? effects_env().level : "";
        sound_name = (same_nocase(level, "snow1a_mis3") || same_nocase(level, "snow1a")) ? "" : "SFX_RocketTrans";
        d.arm_time += 10;
        break;
    }
    case 0x1e:
        d.max_speed = 300.0f;
        v = 300.0f;
        d.target = 0;
        sound_name = "SFX_RocketTrans";
        d.turn = 1.0f;
        d.accel = 0.0f;
        radius = ee::add(radius, radius);
        d.locked = 1;
        d.straight_time = ee::cvt_w(ee::mul(rate, ps2::ffloat(0x3dcccccd)));
        life = sim.frame_rate * 5;
        break;
    default:
        d.max_speed = 200.0f;
        v = 200.0f;
        d.target = 0;
        sound_name = "SFX_RocketTrans";
        d.turn = 1.0f;
        d.accel = 0.0f;
        d.locked = 1;
        d.straight_time = ee::cvt_w(ee::mul(rate, ps2::ffloat(0x3dcccccd)));
        life = sim.frame_rate * 5;
        break;
    }
    if (set_life) d.life = life;
    // O zigue-zague dos mísseis do jogador (o tipo 2): forte nos canos 7 e 8, fraco nos outros, o lado alternado.
    if (weapon == 2 && !not_player) {
        d.wiggle[1] = 0.5f;
        d.wiggle[0] = uint32_t(system - 7) < 2 ? 1.0f : 0.2f;
        g_wiggle_toggle ^= 1;
        if (g_wiggle_toggle != 0) {
            d.wiggle[1] = ee::neg(d.wiggle[1]);
            d.wiggle[0] = ee::neg(d.wiggle[0]);
        }
    } else {
        d.wiggle[1] = 0.0f;
        d.wiggle[0] = 0.0f;
    }
    d.wiggle_phase = 0.0f;
    alignas(16) float vel[4] = {dir[0], dir[1], dir[2], 0.0f};
    vu_normalize(vel, vel);
    // [driving] 0x0025a7a0 v3scale
    for (int i = 0; i < 3; ++i) vel[i] = ee::mul(vel[i], v);
    if (m.po.slot < 0) {
        ++g_stats.guards;  // o PS2 escreveria o corpo antes do vetor
    } else {
        static const float zero[3] = {0, 0, 0};
        SimpleRigidBody* b = GetSimpleRigidBody(sim, m.po.slot);
        SimpleRigidBody_Construct(b, radius, 4.0f, m.po.slot, kSimpleMissile, pos, vel, zero, orient);
        b->flags = uint16_t(b->flags | 0x1c9);
    }
    // O RSceneObj do modelo (FastAlloc(0x80): a cópia da instância gRigidBodyDraw 0x386b40 e o RSceneObj; o
    // SetRenderObject): o port guarda a instância de origem quando o EFireWeapon chama o UseInstance.
    effects_note(0x001c7098);
    // O ASound do voo (o banco "Weapons", ABank::fgBanks[4]; "POV Weapons" no EMP, "Weapons Transit" nos outros).
    ABank* bank = ABank_Category(4);
    const int32_t patch = bank ? bank->index.Lookup(sound_name) : -1;
    effects_note(0x002f5880, uint32_t(patch));
    if (bank) m.sound = new ASound(weapon == 0xe ? "POV Weapons" : "Weapons Transit", bank->snd_bank, patch);
    d.sound = m.sound ? 1u : 0u;
    // O tom: +0,01 e de volta a 1 quando passa de 0,04 (sempre: começa em 1).
    g_pitch = ee::add(g_pitch, ps2::ffloat(0x3c23d70a));
    if (ee::le(ps2::ffloat(0x3d23d70a), g_pitch)) g_pitch = 1.0f;
    if (m.sound) {
        m.sound->pitch = g_pitch;
        m.sound->min_distance = 15.0f;
        if (d.weapon == 0x15) {
            m.sound->max_distance = 300.0f;
            m.sound->max_distance2 = ps2::ffloat(0x47afc800);
        }
    }
    if (d.target && nf::p32_ptr<WTargetableData>(d.target)->fixed == 0) target_add(d.target);
}

// [driving] 0x00181df0 Missile::SetUserSteerX
void Missile_SetUserSteerX(Missile& m, float x) {
    m.d.steer_target[0] = ee::le(ee::abs(x), 0.2f) ? 0.0f : ee::mul(x, m.d.steer_scale);
}

// [driving] 0x00181e30 Missile::SetUserSteerY
void Missile_SetUserSteerY(Missile& m, float y) {
    m.d.steer_target[1] = ee::le(ee::abs(y), 0.2f) ? 0.0f : ee::mul(y, m.d.steer_scale);
}

// [driving] 0x00183888 Missile::SetTarget
void Missile_SetTarget(Missile& m, uint32_t target) {
    m.d.target = target;
    if (target && nf::p32_ptr<WTargetableData>(target)->fixed == 0) target_add(target);
}

// [driving] 0x00181c80 Missile::Simulate
void Missile_Simulate(Missile& m) {
    Simulation& sim = simulation();
    ++g_stats.simulated;
    MissileData& d = m.d;
    if (d.decaying == 0) {
        Missile_SteerMissile(m);
        Missile_CheckForCollision(m);
        PhysicsObject_Simulate(&m);
        if (ee::lt(d.volume, 1.0f)) {
            const float v = ee::add(d.volume, ps2::ffloat(0x3d888889));
            d.volume = v;
            if (ee::lt(1.0f, v)) d.volume = 1.0f;
        }
        if (m.sound) {
            m.sound->volume = d.volume;
            const SimpleRigidBody* b = GetSimpleRigidBody(sim, m.po.slot);
            std::memcpy(m.sound->pos, b->pos, 12);
            std::memcpy(m.sound->vel, b->vel, 12);
        }
        return;
    }
    d.decay_time -= 1;
    if (d.decay_time == -1) {
        DeletePhysicsObject(sim, &m);  // Simulation::DeleteMissileObject
        return;
    }
    SimpleRigidBody* b = GetSimpleRigidBody(sim, m.po.slot);
    b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
    // [driving] 0x001c8b78 RSceneObj::Hide e 0x001cacb8 RSceneObj::StopFX (o render do port: o nf_driving não desenha
    // um míssil morrendo)
    effects_note(0x001c8b78);
    if (m.sound) m.sound->volume = 0.0f;
}

// [driving] 0x00181e70 Missile::SteerMissile
void Missile_SteerMissile(Missile& m) {
    Simulation& sim = simulation();
    MissileData& d = m.d;
    SimpleRigidBody* b = GetSimpleRigidBody(sim, m.po.slot);
    float limit = d.max_speed;  // f22
    const float speed = SimpleRigidBody_GetScalarVelocity(b);  // f23
    if (d.target == 0) d.locked = 1;
    else if (d.locked == 0) limit = ee::mul(limit, 0.3f);
    alignas(16) float orient[4][4];
    SimpleRigidBody_RecalcOrientMat(b, orient);
    alignas(16) float fwd[4] = {};
    SimpleRigidBody_GetForwardVector(b, fwd);
    if (ee::lt(d.max_speed, d.accel)) d.accel = d.max_speed;
    // O míssil guiado pelo jogador (SMissionManager +0x358: os tipos 0x16 e 0x1f).
    if (mission_exists() && mission_manager().missile == nf::p32_u(&m)) {
        auto approach = [&](float& cur, float want) {
            const float up = ee::add(cur, kSteerRate);
            if (ee::lt(up, want)) {
                cur = up;
                return;
            }
            const float dn = ee::sub(cur, kSteerRate);
            cur = ee::lt(want, dn) ? dn : want;
        };
        approach(d.steer[0], d.steer_target[0]);
        // O y: o mesmo com a ordem dos testes do código (up se want > up; down se want < down).
        {
            const float up = ee::add(d.steer[1], kSteerRate);
            if (ee::lt(up, d.steer_target[1])) d.steer[1] = up;
            else {
                const float dn = ee::sub(d.steer[1], kSteerRate);
                d.steer[1] = ee::lt(d.steer_target[1], dn) ? dn : d.steer_target[1];
            }
        }
        alignas(16) float q[4], m70[4][4], r[4][4], qn[4];
        std::memcpy(q, b->quat, 16);
        VU0_quattom4(q, m70);
        if (!ee::eq(d.steer[0], 0.0f)) {
            VU0_MATRIX4setyrot(r, d.steer[0]);
            VU0_MATRIX3x4_mult(m70, r, m70);
        }
        const float sy = d.steer[1];
        bool rot = false;
        float ang = sy;
        if (ee::lt(0.0f, sy)) {
            if (ee::lt(m70[2][1], ps2::ffloat(0x3f7d70a4))) {
                rot = true;
                ang = ee::neg(sy);
            }
        }
        if (!rot && ee::lt(sy, 0.0f) && ee::lt(ps2::ffloat(0xbf7d70a4), m70[2][1])) {
            rot = true;
            ang = ee::neg(sy);
        }
        if (rot) {
            build_rotation_mat4(r, m70[0], ang);
            VU0_MATRIX3x4_mult(m70, r, m70);
        }
        VU0_m4toquat(m70, qn);
        anim::fast_qslerp(q, qn, q, ps2::ffloat(0x3f266666));  // 0,65
        SimpleRigidBody_SetOrientation(b, q);
        alignas(16) float f2[4] = {};
        SimpleRigidBody_GetForwardVector(b, f2);
        alignas(16) float g[4][4];
        Util_GenerateMatrix(g, f2);
        std::memcpy(m70, g, 64);
        build_rotation_mat4(r, m70[2], 0.0f);
        VU0_MATRIX3x4_mult(m70, r, m70);
        VU0_m4toquat(m70, qn);
        anim::fast_qslerp(q, qn, q, ps2::ffloat(0x3ca3d70a));  // 0,02
        SimpleRigidBody_SetOrientation(b, q);
        alignas(16) float z[4];
        std::memcpy(z, g[2], 16);
        srb_quat_z_axis(q, z);
        const float s = SimpleRigidBody_GetScalarVelocity(b);
        for (int i = 0; i < 3; ++i) b->vel[i] = ee::mul(z[i], s);
    }
    const int32_t age = sim.steps - d.start_step;
    if (d.target == 0 || !(d.straight_time < age)) {
        if (!(d.straight_time < age)) {
            // Ainda em linha reta: a orientação vai 10% para a da velocidade.
            alignas(16) float dirv[4] = {0, 0, 0, 0};
            std::memcpy(dirv, b->vel, 12);
            vu_normalize(dirv, dirv);
            alignas(16) float g[4][4], qn[4], q2[4];
            Util_GenerateMatrix(g, dirv);
            VU0_m4toquat(g, qn);
            anim::fast_qslerp(b->quat, qn, q2, ps2::ffloat(0x3dcccccd));
            SimpleRigidBody_SetOrientation(b, q2);
        }
    } else {
        const WTargetableData& t = *nf::p32_ptr<const WTargetableData>(d.target);
        alignas(16) float aim[4] = {t.pos[0], t.pos[1], t.pos[2], 0.0f};
        alignas(16) float tv[4] = {};
        if (target_velocity(t, tv)) {
            const float k = ee::mul(sim.dt, 3.0f);
            for (int i = 0; i < 3; ++i) aim[i] = ee::add(aim[i], ee::mul(tv[i], k));
        }
        alignas(16) float to[4] = {ee::sub(aim[0], b->pos[0]), ee::sub(aim[1], b->pos[1]), ee::sub(aim[2], b->pos[2]),
                                   0.0f};
        const float dist2 = ps2::ffloat(vu_len2(to));  // f21
        vu_normalize(to, to);
        BondCar* player = sim.cars.empty() ? nullptr : sim.cars[0];
        // A entrada +0x29c da tabela virtual do carro do jogador (o alvo travado do WTargetPicker): o port não tem.
        const uint32_t locked_target = 0;
        (void)player;
        if (d.target == locked_target && d.beep_time < sim.steps) {
            ++g_stats.lock_beeps;
            effects_note(0x002f4b10);  // AMenuSound::Trigger("SFX_Htargetlock", "Tracking System", 2)
            const float add = ee::mul(ee::mul(dist2, ee::cvt_s(sim.frame_rate)), ps2::ffloat(0x38d1b717));
            d.beep_time += ee::cvt_w(add);
        }
        if (ee::lt(4.0f, dist2)) {
            alignas(16) float g[4][4], qn[4], q2[4];
            Util_GenerateMatrix(g, to);
            VU0_m4toquat(g, qn);
            float t2 = d.turn;
            if (d.locked == 0) t2 = ee::mul(t2, 0.5f);
            const float ramp = ee::mul(ee::mul(ee::cvt_s(age - d.straight_time), sim.dt), 0.5f);
            if (!ee::lt(1.0f, ramp)) t2 = ee::mul(t2, ramp);
            anim::fast_qslerp(b->quat, qn, q2, t2);
            SimpleRigidBody_SetOrientation(b, q2);
            alignas(16) float z[4] = {};
            std::memcpy(z, g[0], 16);
            srb_quat_z_axis(q2, z);
            const float s = SimpleRigidBody_GetScalarVelocity(b);
            for (int i = 0; i < 3; ++i) b->vel[i] = ee::mul(z[i], s);
        }
        alignas(16) float vd[4] = {};
        std::memcpy(vd, b->vel, 12);
        vu_normalize(vd, vd);
        const float dot = vu_dot(to, vd);  // f20
        limit = ee::lt(0.1f, dot) ? ee::mul(limit, dot) : ee::mul(limit, 0.1f);
        const WTargetableData& t3 = *nf::p32_ptr<const WTargetableData>(d.target);
        d.detonate = (t3.kind != 0 && ee::le(dist2, 0.25f)) ? 1 : 0;
        if (d.weapon == 0x15) {
            if (ee::lt(dot, 0.5f)) {
                target_remove(d.target);
                d.target = 0;
                d.life = sim.frame_rate >> 3;
            }
        } else if (t3.kind == 0 && ee::le(dist2, 2.0f)) {
            target_remove(d.target);
            d.target = 0;
        }
        if (ee::lt(ps2::ffloat(0x3f7d70a4), dot)) d.locked = 1;
    }
    alignas(16) float acc[4] = {};
    const float a = ee::lt(speed, limit) ? d.accel : ee::neg(d.accel);
    for (int i = 0; i < 3; ++i) acc[i] = ee::mul(fwd[i], a);
    SimpleRigidBody_Accelerate(b, acc);
    // O zigue-zague.
    const float w0 = d.wiggle[0];
    if (ee::lt(0.01f, w0) || ee::lt(w0, -0.01f)) {
        float ramp = ee::mul(ee::cvt_s(sim.steps - d.start_step), sim.dt);
        if (ee::lt(1.0f, ramp)) ramp = 1.0f;
        d.wiggle[0] = ee::mul(w0, ee::sub(1.0f, ee::add(sim.dt, sim.dt)));
        d.wiggle[1] = ee::mul(d.wiggle[1], ee::sub(1.0f, ee::add(sim.dt, sim.dt)));
        float ph = ee::add(d.wiggle_phase, sim.dt);
        d.wiggle_phase = ph;
        if (ee::lt(1.0f, ph)) {
            do ph = ee::sub(ph, 1.0f);
            while (ee::lt(1.0f, ph));
            d.wiggle_phase = ph;
        }
        alignas(16) float off[4] = {};
        off[0] = ee::mul(ee::mul(ee::neg(b->vel[2]), VU0_Cos(d.wiggle_phase)), d.wiggle[0]);
        off[2] = ee::mul(ee::mul(b->vel[0], VU0_Cos(d.wiggle_phase)), d.wiggle[0]);
        off[1] = ee::mul(VU0_Sin(d.wiggle_phase), d.wiggle[1]);
        const float k = ee::mul(sim.dt, ramp);
        for (int i = 0; i < 3; ++i) b->pos[i] = ee::add(b->pos[i], ee::mul(off[i], k));
    }
}

// [driving] 0x00182978 Missile::CheckForCollision
void Missile_CheckForCollision(Missile& m) {
    Simulation& sim = simulation();
    MissileData& d = m.d;
    SimpleRigidBody* b = GetSimpleRigidBody(sim, m.po.slot);
    d.life -= 1;
    d.detonate = (d.life < 1 || d.detonate != 0) ? 1 : 0;
    if (d.weapon == 0xe && mission_exists() && mission_manager().cinematic_mode != 0) d.detonate = 1;
    if (d.weapon == 0x16) {
        // PBondCar_PosInSoftZone(pos - 10 y) (0x00190eb8, não portado: falso, contado)
        ++g_stats.soft_zone;
        effects_note(0x00190eb8);
    }
    // O míssil guiado do gerente (+0x358), relido a cada vez como no PS2: depois do primeiro SetDriveMissile(0) do laço
    // dos atingidos, os seguintes não chamam de novo.
    auto mission_missile = [&] { return mission_exists() && mission_manager().missile == nf::p32_u(&m); };
    auto emp_sound = [&](float min_d, float max_d) {
        ABank* bank = ABank_Category(4);
        const int32_t patch = bank ? bank->index.Lookup("SFX_EMPdetonate") : -1;
        effects_note(0x002f5880, uint32_t(patch));
        if (!bank) return;
        AOneShotSound* s = new AOneShotSound("POV Weapons", bank->snd_bank, patch, 1.0f);
        s->fx = 0.0f;
        s->lifetime = 0.0f;
        std::memcpy(s->pos, b->pos, 12);
        std::memcpy(s->vel, b->vel, 12);
        s->fx = 1.0f;
        s->min_distance = min_d;
        s->max_distance = max_d;
        s->max_distance2 = ee::mul(max_d, max_d);
    };
    if (d.detonate) {
        ++g_stats.expired;
        if (d.weapon == 0xe) {
            emp_sound(50.0f, 500.0f);
        } else {
            alignas(16) float fwd[4] = {};
            SimpleRigidBody_GetForwardVector(b, fwd);
            ++g_stats.exploded;
            SpawnExplosion(sim, d.damage, 2, d.weapon, fwd, b->pos, 0, nullptr);
        }
        if (mission_missile()) Missile_DriveMissile(0);
        d.decaying = 1;
        return;
    }
    if (d.arm_time < sim.steps) {
        const SimCollisionMap& hits = sim.srb_hits[b->slot];
        for (int i = 0; i < 0x90; ++i) {
            PhysicsObject* o = GetOrderedBody(sim, i);
            if (!((hits.bits[i >> 6] >> (i & 63)) & 1)) continue;
            if (is_owned_by(sim, m, o)) continue;
            if (!o) {
                ++g_stats.guards;
                continue;
            }
            bool hit = true;
            if (d.weapon == 0xe) {
                if (o->kind() == 1) {
                    // A entrada +0x254 da tabela virtual do carro: o PBondCar::AttackWithEmp (bond_car_damage.cpp).
                    ++g_stats.car_emp;
                    effects_note(0x0018ac28, uint32_t(i));
                    o->attack_with_emp();
                } else {
                    emp_sound(50.0f, 500.0f);
                }
            } else {
                SimpleObject* so = dynamic_cast<SimpleObject*>(o);
                if (!so) {
                    // Um corpo rígido: as caixas (a do corpo e a do míssil: 0,1 x 0,1 x 0,5, o raio do corpo no w).
                    const float* opos = PhysicsObject_GetPosition(o);
                    alignas(16) float c_rb[4] = {opos[0], opos[1], opos[2], 1.0f};
                    alignas(16) float c_m[4] = {b->pos[0], b->pos[1], b->pos[2], 1.0f};
                    alignas(16) float h_m[4] = {0.1f, 0.1f, 0.5f, b->radius};
                    alignas(16) float mm[4][4];
                    VU0_quattom4(b->quat, mm);
                    int slot = -1;
                    for (int k = 0; k < kRigidBodySlots; ++k)
                        if (sim.owners[k] == o) slot = k;
                    const RigidBody* rb = GetRigidBody(sim, slot < 0 ? 0 : slot);
                    if (slot < 0) ++g_stats.guards;
                    OBB a{}, ob{};
                    OBB_Reset(a, rb->info->orient, c_rb, rb->info->half_size);
                    OBB_Reset(ob, mm, c_m, h_m);
                    if (!OBB_CheckOBBOverlap(ob, a)) hit = false;
                }
                if (hit) {
                    alignas(16) float fwd[4] = {};
                    const float hitpos[3] = {b->pos[0], b->pos[1], b->pos[2]};
                    srb_quat_z_axis(b->quat, fwd);
                    if (is_owned_by(sim, m, player_object(sim)) && o->kind() == 1) {
                        // AddDamageByPlayer (+0x1ac) e o ApplyDamage (+0x1c) do choque: 32, de 10 m antes a 10 m depois.
                        o->add_damage_by_player(d.damage);
                        alignas(16) float vd[4] = {};
                        std::memcpy(vd, b->vel, 12);
                        vu_normalize(vd, vd);
                        float from[3], to[3];
                        for (int k = 0; k < 3; ++k) {
                            from[k] = ee::add(hitpos[k], ee::mul(vd[k], -10.0f));
                            to[k] = ee::add(hitpos[k], ee::mul(vd[k], 10.0f));
                        }
                        effects_stats().damage_calls++;
                        effects_note(0x00189598, ps2::fbits(32.0f), uint32_t(i), 2);
                        o->apply_damage(from, to, 32.0f, 0.0f, 2);
                    }
                    ++g_stats.body_hits;
                    ++g_stats.exploded;
                    SpawnExplosion(sim, d.damage, 2, d.weapon, fwd, hitpos, 0, o);
                }
            }
            if (mission_missile()) Missile_DriveMissile(0);
            if (hit) {
                d.decaying = 1;
                return;
            }
        }
    }
    // O mundo entre a posição anterior e a de agora.
    alignas(16) float seg[2][4];
    seg[1][0] = b->pos[0];
    seg[1][1] = b->pos[1];
    seg[1][2] = b->pos[2];
    seg[1][3] = 1.0f;
    alignas(16) float vel[4] = {b->vel[0], b->vel[1], b->vel[2], 1.0f};
    const float avy = ee::abs(vel[1]);
    const float spd = ps2::ffloat(ps2::sqrt(vu_len2(vel)));
    const bool fast_level = ee::lt(ee::mul(avy, 4.0f), spd);
    for (int i = 0; i < 3; ++i) vel[i] = ee::mul(vel[i], sim.dt);
    seg[0][3] = 0.0f;
    for (int i = 0; i < 3; ++i) seg[0][i] = ee::sub(seg[1][i], vel[i]);
    bool hug = false;
    if (fast_level) {
        const int32_t w = d.weapon;
        if (w != 0x19 && w != 0x1c && w != 0x1e && w != 0x15 && w != 0x16) hug = is_owned_by(sim, m, player_object(sim));
    }
    if (hug) {
        // O míssil do jogador rente ao chão: a face sob ele e a altura; entre -0,8 e 5 m abaixo, fica 0,4 acima.
        WorldCollision* wc = world();
        if (wc) wc->find_closest_face(m.world_pos_, seg[1], true);
        else ++g_stats.guards;
        const float h = wc ? WWorldPos_HeightAtPoint(m.world_pos_, seg[1]) : 0.0f;
        if (m.world_pos_.valid) {
            const float dy = ee::sub(h, seg[1][1]);
            if (ee::lt(ee::mul(kGroundOffset, -2.0f), dy) && ee::lt(dy, kGroundMax)) {
                seg[1][1] = ee::add(h, kGroundOffset);
                std::memcpy(b->pos, seg[1], 12);
            }
        }
    }
    WorldCollisionInfo info;
    WorldCollisionInfo_Init(info);
    if (d.weapon == 0x16) {
        alignas(16) float f[4] = {};
        SimpleRigidBody_GetForwardVector(b, f);
        for (int i = 0; i < 3; ++i) seg[1][i] = ee::add(seg[1][i], ee::mul(f[i], 1.5f));
    }
    WCollisionMgr_CheckHitWorld(&seg[0][0], info);
    if (info.hit == 0) return;
    ++g_stats.world_hits;
    // WCollisionMgr::CheckHitWindow (as janelas que quebram, driving/game/shells.h)
    ++g_stats.windows;
    WCollisionMgr_CheckHitWindow(info, true, 0);
    if (d.weapon != 0xe) {
        // Shell::ApplyWorldDamage (a queimadura e os estímulos da instância atingida, driving/game/shells.h)
        ++g_stats.world_damage;
        Shell_ApplyWorldDamage(d.damage, ee::mul(d.damage, ps2::ffloat(0x3c03126f)), m.world_pos_, 5, info);
        alignas(16) float fwd[4] = {};
        SimpleRigidBody_GetForwardVector(b, fwd);
        ++g_stats.exploded;
        SpawnExplosion(sim, d.damage, 2, d.weapon, fwd, info.point, info.face_type, nullptr);
    }
    if (uint32_t(d.weapon - 0x15) < 2 && info.face_type == 0xe) {
        ++g_stats.programmer_events;
        effects_note(0x00208540, 0x15);
        if (mission_exists()) SMissionManager_ProgrammerDefinedEvent(mission_manager(), 0x15);
    }
    if (mission_missile()) Missile_DriveMissile(0);
    if (d.weapon == 0xe) emp_sound(50.0f, 500.0f);
    d.decaying = 1;
}

// [driving] 0x001ffc40 Simulation::SpawnMissileObject
Missile* SpawnMissileObject(Simulation& sim, float speed, float damage, int32_t weapon, const float dir[3],
                            const float pos[3], uint32_t sig, uint32_t target, int32_t system) {
    PhysicsObject* owner = find_signature(sim, sig);
    const bool player = owner == (sim.cars.empty() ? nullptr : sim.cars[0]);
    if (!CanSpawnSimpleRigidBody(sim, pos, player)) {
        ++g_stats.refused;
        return nullptr;
    }
    Missile* m = new Missile();
    Missile_Construct(*m, speed, damage, weapon, dir, pos, sig, target, system);
    SpawnPhysicsObject(sim, m);
    ++g_stats.spawned;
    return m;
}

}  // namespace drv
