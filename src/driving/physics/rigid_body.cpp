// O corpo rígido das missões de carro (driving/physics/rigid_body.h): o construtor, as alavancas, o chão, o sono e as
// forças do quadro, portados do DRIVING.ELF (RVehicleParticle.obj). As contas de ponto flutuante usam a aritmética do
// EE e da VU0 (platform/ps2_float.h: truncamento, sem denormais nem infinitos) na ordem da desmontagem, para dar os
// mesmos bits do PS2; as decompilações do Ghidra perdem essa ordem (e os campos das instruções VU0), por isso cada
// função foi lida na desmontagem (out/disasm_driving/, tools/ghidra_driving.sh disasm).
//
// Divergências (port):
//  - um dono nulo (o PS2 sempre tem um) não é chamado: os atributos valem zero, a geometria de colisão nenhuma, e a
//    chamada conta em physics_pending("dono nulo");
//  - ~RigidBody apaga os WWorldPos das alavancas e zera os ponteiros (o PS2 os deixa soltos; o próximo construtor os
//    sobrescreve);
//  - a linha 3 do tensor inverso no mundo (RigidBodyInfo +0x70) é lixo da pilha no PS2 (VU0_MATRIX3x4_mult não a
//    escreve); aqui é zero. Nada a lê.

#include "driving/physics/rigid_body.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "driving/physics/simulation.h"
#include "driving/physics/vu0_physics.h"
#include "driving/physics/world.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {

RigidTuning g_tuning;
float g_base_friction_mass = 8.0f;
TimestepConstants g_timestep;
bool g_underwater = false;
int32_t g_mission_uses_health = 0;
std::map<std::string, int> g_pending;
std::string g_pending_text;

// strcasecmp do newlib.
int ps2_strcasecmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || ca == 0) return ca - cb;
    }
}

}  // namespace

RigidTuning& rigid_tuning() { return g_tuning; }
float& base_friction_mass() { return g_base_friction_mass; }
TimestepConstants& timestep_constants() { return g_timestep; }
bool& underwater_track() { return g_underwater; }
int32_t& mission_uses_health() { return g_mission_uses_health; }

void physics_pending(const char* name) { g_pending[name]++; }
int physics_pending_count(const char* name) {
    auto it = g_pending.find(name);
    return it == g_pending.end() ? 0 : it->second;
}
void physics_pending_reset() { g_pending.clear(); }
const char* physics_pending_report() {
    g_pending_text.clear();
    for (const auto& [name, n] : g_pending) {
        if (!g_pending_text.empty()) g_pending_text += ", ";
        g_pending_text += name + "=" + std::to_string(n);
    }
    if (g_pending_text.empty()) g_pending_text = "nenhum";
    return g_pending_text.c_str();
}

// [driving] 0x1fd338 global constructors keyed to RVehicleParticle (as razões derivadas, na partida)
void rigid_static_init() {
    RigidTuning& t = g_tuning;
    t.ground_restitution_scale = ee::div(t.ground_restitution, t.micro_threshold);
    t.vehicle_restitution_scale = ee::div(t.vehicle_restitution, t.micro_threshold);
    t.building_restitution_scale = ee::div(t.building_coeff_restitution, t.building_micro_threshold);
    t.jump_gravity = t.bodge_player_gravity;
}

void reset_rigid_globals() {
    g_tuning = RigidTuning{};
    g_base_friction_mass = 8.0f;
    g_timestep = TimestepConstants{};
    g_underwater = false;
    g_mission_uses_health = 0;
    ground_impacts() = GroundImpactStats{};
    rigid_static_init();
}

int load_rigid_tuning(const char* text) {
    struct Entry {
        const char* name;
        float RigidTuning::*field;
    };
    static const Entry kEntries[] = {
        {"GRAVITY", &RigidTuning::gravity},
        {"BODGE_PLAYER_GRAVITY", &RigidTuning::bodge_player_gravity},
        {"SLEEP_VEL", &RigidTuning::sleep_vel},
        {"GROUND_FRICTION_COEFF", &RigidTuning::ground_friction_coeff},
        {"MICRO_THRESHOLD", &RigidTuning::micro_threshold},
        {"GROUND_RESTITUTION", &RigidTuning::ground_restitution},
        {"NATURAL_ANGULAR_DAMPING", &RigidTuning::natural_angular_damping},
        {"M_LIMIT", &RigidTuning::m_limit},
        {"AM_LIMIT", &RigidTuning::am_limit},
        {"CAR_COLLIDE_COG_SCALE", &RigidTuning::car_collide_cog_scale},
        {"DEPTH_FORCE_SCALE", &RigidTuning::depth_force_scale},
        {"SPEED_SCALE", &RigidTuning::speed_scale},
        {"SPEED_FORCE_LIMIT", &RigidTuning::speed_force_limit},
        {"TOTAL_FORCE_LIMIT", &RigidTuning::total_force_limit},
        {"BUILDING_MICRO_THRESHOLD", &RigidTuning::building_micro_threshold},
        {"BUILDING_FRICTION_FACTOR", &RigidTuning::building_friction_factor},
        {"BUILDING_COEFF_RESTITUTION", &RigidTuning::building_coeff_restitution},
        {"BUILDING_FRICTION_LIMIT", &RigidTuning::building_friction_limit},
        {"BUILDING_FORCE_LIMIT", &RigidTuning::building_force_limit},
        {"PF_SCALE", &RigidTuning::pf_scale},
        {"BUILDING_COLLIDE_COG_SCALE", &RigidTuning::building_collide_cog_scale},
    };
    int changed = 0;
    const char* p = text;
    while (p && *p) {
        const char* end = std::strchr(p, '\n');
        std::string line = end ? std::string(p, end) : std::string(p);
        p = end ? end + 1 : nullptr;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        const float value = std::strtof(line.c_str() + eq + 1, nullptr);
        for (const Entry& e : kEntries) {
            if (name != e.name) continue;
            if (ps2::fbits(g_tuning.*e.field) != ps2::fbits(value)) ++changed;
            g_tuning.*e.field = value;
        }
    }
    return changed;
}

RigidBodyScratch& rigid_scratch() { return *GetScratchPadFreeZone(simulation()); }

// [driving] 0x1f7860 RigidBody::GetOwner
PhysicsObject* GetOwner(const RigidBody* rb) { return simulation().owners[rb->slot]; }

PhysicsAttributes* owner_attributes(PhysicsObject* owner) {
    if (!owner) {
        physics_pending("dono nulo");
        return nullptr;
    }
    return owner->attributes();
}

// [driving] 0x1f6600 RigidBody::InitRigidBodySystem
void InitRigidBodySystem(const char* track, const char* tun) {
    // DAT_00339178 = GetScratchPadFreeZone (a Simulation a guarda; rigid_scratch() a consulta).
    // O banco de ajuste "Physics:Rigid" da pista (PROFILE_saveProfile ... DTuningDBMgr::CloseCurrent).
    if (tun) load_rigid_tuning(tun);
    else physics_pending("DTuningDBMgr (ajuste da pista)");
    g_underwater = track && std::strstr(track, "uw_") != nullptr;
}

// [driving] 0x1f6ac8 RigidBody::ResetRigidBodySP
void ResetRigidBodySP() {
    RigidBodyScratch& s = rigid_scratch();
    s.vu0_busy = 0;
    s.vu0_body = nullptr;
}

// [driving] 0x1f7ac0 RigidBody::SetOrientMat
void SetOrientMat(RigidBody* rb, const float m[4][4]) { VU0_m4toquat(m, rb->quat); }

// [driving] 0x1f7a80 RigidBody::GetBodyTensorMatrix4
void GetBodyTensorMatrix4(const RigidBody* rb, float m[4][4]) {
    std::memset(m, 0, 64);
    m[0][0] = rb->inv_inertia_x;
    m[1][1] = rb->inv_inertia_y;
    m[2][2] = rb->inv_inertia_z;
    m[3][3] = 1.0f;  // sqc2 vf0
}

// [driving] 0x1f7ae8 RigidBody::StoreBodyMatrix
void StoreBodyMatrix(RigidBody* rb, const float m[4][4]) { std::memcpy(rb->info->orient, m, 64); }

// [driving] 0x1f7b70 RigidBody::StoreInvWorldTensor
void StoreInvWorldTensor(RigidBody* rb, const float m[4][4]) { std::memcpy(rb->info->inv_world_tensor, m, 64); }

// [driving] 0x1f8908 RigidBody::UpdateZonalInfo
void UpdateZonalInfo(RigidBody* rb) {
    for (int i = 0; i < 3; ++i) {
        const float f = ee::mul(rb->pos[i], 0.2f);
        const float half = ee::lt(f, 0.0f) ? -0.5f : 0.5f;
        rb->info->zone[i] = ee::cvt_w(ee::add(half, f));
    }
}

// [driving] 0x1f8088 RigidBody::ApplyAngularDamping
void ApplyAngularDamping(RigidBody* rb) {
    if (rb->ground_levers != 0 || rb->type < 4) return;
    const float d = g_tuning.natural_angular_damping;
    rb->ang_momentum[0] = ee::mul(rb->ang_momentum[0], d);
    rb->ang_momentum[1] = ee::mul(rb->ang_momentum[1], d);
    rb->ang_momentum[2] = ee::mul(rb->ang_momentum[2], d);
}

// [driving] 0x1fd170 RigidBody::ForceToSleep
void ForceToSleep(RigidBody* rb) {
    rb->state = 1;
    for (int i = 0; i < 3; ++i) {
        rb->vel[i] = 0;
        rb->ang_vel[i] = 0;
        rb->momentum[i] = 0;
        rb->ang_momentum[i] = 0;
    }
}

// [driving] 0x1fcfc0 RigidBody::ControlSleep
void ControlSleep(RigidBody* rb) {
    // |v.x| + |v.y| + |v.z| e o mesmo da velocidade angular, somados nessa ordem.
    auto manhattan = [](const float v[3]) {
        float s = v[0];
        if (ee::lt(s, 0.0f)) s = ee::neg(s);
        s = ee::lt(v[1], 0.0f) ? ee::sub(s, v[1]) : ee::add(s, v[1]);
        s = ee::lt(v[2], 0.0f) ? ee::sub(s, v[2]) : ee::add(s, v[2]);
        return s;
    };
    const float lin = manhattan(rb->vel);
    const float ang = manhattan(rb->ang_vel);
    float limit;
    if (rb->type < 4) {
        // Um veículo de pé (o y do seu "para cima" acima de 0,7) nunca dorme.
        if (ee::lt(0.7f, rb->info->orient[1][1])) return;
        limit = ee::mul(g_tuning.sleep_vel, 0.25f);
    } else {
        limit = g_tuning.sleep_vel;
    }
    if (!ee::lt(lin, limit)) return;
    if (!ee::lt(ang, limit)) return;
    if (rb->ground_levers < 3) return;
    ForceToSleep(rb);
}

// [driving] 0x1f87d8 RigidBody::ResolveMassScaledForce
void ResolveMassScaledForce(RigidBody* rb, const float f[4]) {
    if (rb->state == 3) return;
    rb->radius_flags &= ~2u;
    rb->state = 2;
    Reg v1, force, fm;
    load(v1, f);
    load(force, &rb->force[0]);
    load(fm, &rb->force[0]);
    const uint32_t m[4] = {b(rb->mass), 0, 0, 0};
    mulbc(fm, v1, m, BX, XYZ);
    add(fm, force, fm, XYZ);
    store(&rb->force[0], fm);
}

// [driving] 0x1f8778 RigidBody::ResolveTorque
void ResolveTorque(RigidBody* rb, const float t[4]) {
    if (rb->state == 3) return;
    rb->state = 2;
    Reg v1, v2, v3;
    load(v1, t);
    load(v2, &rb->torque[0]);
    load(v3, &rb->torque[0]);
    add(v3, v1, v2, XYZ);
    store(&rb->torque[0], v3);
}

// [driving] 0x1f8588 RigidBody::SetAngularMomentum
void SetAngularMomentum(RigidBody* rb, const float l[4]) {
    const RigidBodyInfo* info = rb->info;
    Reg v1, r0, r1, r2, r3, out, acc;
    load(v1, l);
    load(r0, info->orient[0]);
    load(r1, info->orient[1]);
    load(r2, info->orient[2]);
    load(r3, info->orient[3]);
    load(out, &rb->ang_momentum[0]);
    static const uint32_t vf0[4] = {0, 0, 0, 0x3f800000};
    mulabc(acc, r0, v1, BX, XYZ);
    maddabc(acc, r1, v1, BY, XYZ);
    maddabc(acc, r2, v1, BZ, XYZ);
    maddbc(out, acc, r3, vf0, BW, XYZ);
    store(&rb->ang_momentum[0], out);
}

// [driving] 0x1f7c00 RigidBody::GetOrientToGround
float GetOrientToGround(const RigidBody* rb) {
    static const float kUp[3] = {0.0f, 1.0f, 0.0f};  // 0x396568
    const float* r = rb->info->orient[1];
    const float x = ee::mul(r[0], kUp[0]);
    const float y = ee::mul(r[1], kUp[1]);
    const float z = ee::mul(r[2], kUp[2]);
    return ee::add(ee::add(x, y), z);
}

// [driving] 0x1f72d8 RigidBody::InitLevers
void InitLevers(RigidBody* rb, PhysicsObject* owner, const float half[4]) {
    RigidBodyInfo* info = rb->info;
    uint32_t count = 0;
    float y_offset = 0;
    const float(*geometry)[4] = nullptr;
    if (owner) geometry = owner->collision_geometry(count, y_offset);
    else physics_pending("dono nulo");
    if (count != 0) {
        int first = 0;
        if (owner->kind() == 1) {
            owner->attributes();  // a chamada à entrada +0x1d4 (o resultado não é usado)
            const float x_off = owner->lookup_float("CAR_WHEEL_X_OFFSET");
            const float zf_off = owner->lookup_float("CAR_WHEEL_ZF_OFFSET");
            const float zr_off = owner->lookup_float("CAR_WHEEL_ZR_OFFSET");
            float(*l)[4] = info->lever_local;
            l[0][0] = ee::add(ee::neg(half[0]), x_off);
            l[0][1] = ee::neg(half[1]);
            l[0][2] = ee::add(half[2], zf_off);
            l[0][3] = 0;
            l[1][0] = ee::sub(half[0], x_off);
            l[1][1] = ee::neg(half[1]);
            l[1][2] = ee::add(half[2], zf_off);
            l[1][3] = 0;
            const PhysicsAttributes* attr = owner_attributes(GetOwner(rb));
            if (!attr || attr->alt_layout == 0) {
                l[2][0] = ee::add(ee::neg(half[0]), x_off);
                l[2][1] = ee::neg(half[1]);
                l[2][2] = ee::add(ee::neg(half[2]), zr_off);
                l[2][3] = 0;
                l[3][0] = ee::sub(half[0], x_off);
            } else {
                l[2][0] = 0;
                l[2][1] = ee::neg(half[1]);
                l[2][2] = ee::add(ee::neg(half[2]), zr_off);
                l[2][3] = 0;
                l[3][0] = 0;
            }
            l[3][1] = ee::neg(half[1]);
            l[3][2] = ee::add(ee::neg(half[2]), zr_off);
            l[3][3] = 0;
            first = 4;
        }
        info->lever_count = int8_t(uint8_t(count) + uint8_t(first));
        if (info->lever_count > 16) {
            count = count + 16 - uint32_t(int32_t(info->lever_count));
            info->lever_count = 16;
        }
        for (uint32_t i = 0; i < count; ++i) {
            std::memcpy(info->lever_local[first + i], geometry[i], 16);
            info->lever_local[first + i][1] = ee::add(info->lever_local[first + i][1], y_offset);
        }
    } else {
        // Sem geometria: as oito quinas da caixa.
        const float hx = half[0], hy = half[1], hz = half[2];
        const float nx = ee::neg(hx), ny = ee::neg(hy), nz = ee::neg(hz);
        const float box[8][4] = {{nx, ny, hz, 0}, {hx, ny, hz, 0}, {nx, ny, nz, 0}, {hx, ny, nz, 0},
                                 {hx, hy, hz, 0}, {nx, hy, hz, 0}, {hx, hy, nz, 0}, {nx, hy, nz, 0}};
        info->lever_count = 8;
        std::memcpy(info->lever_local, box, sizeof(box));
        if (rb->type < 4) {
            PhysicsAttributes* attr = owner_attributes(owner);
            if (attr) attr->lever_scale = 1.5f;
        }
    }
    info->lever_friction = ee::mul(ee::div(rb->mass, g_base_friction_mass), g_tuning.ground_friction_coeff);
}

// [driving] 0x1f6e98 RigidBody::RigidBody
void RigidBody_Construct(RigidBody* rb, const RigidBodyParams& p) {
    std::memcpy(rb->pos, p.pos, 12);
    std::memcpy(rb->vel, p.vel, 12);
    std::memcpy(rb->ang_vel, p.ang_vel, 12);
    rb->type = int8_t(p.type);
    rb->slot = int8_t(p.slot);
    rb->mass = p.mass;
    rb->ground_levers = 0;
    std::memset(rb->torque, 0, 12);  // a cópia do vetor zero de 0x355b80
    if (p.moving == 0) {
        std::memset(rb->vel, 0, 12);
        std::memset(rb->ang_vel, 0, 12);
    } else {
        std::memcpy(rb->vel, p.vel, 12);
        std::memcpy(rb->ang_vel, p.ang_vel, 12);
    }
    SetOrientMat(rb, p.orient);
    rb->inv_inertia_x = p.inv_inertia[0];
    rb->inv_inertia_y = p.inv_inertia[1];
    const float lx = ee::div(rb->ang_vel[0], p.inv_inertia[0]);
    const float ly = ee::div(rb->ang_vel[1], p.inv_inertia[1]);
    const float px = ee::mul(rb->vel[0], p.mass);
    const float lz = ee::div(rb->ang_vel[2], p.inv_inertia[2]);
    const float py = ee::mul(rb->vel[1], p.mass);
    const float pz = ee::mul(rb->vel[2], p.mass);
    std::memset(rb->force, 0, 12);
    std::memset(rb->torque, 0, 12);
    rb->momentum[0] = px;
    rb->momentum[2] = pz;
    rb->momentum[1] = py;
    rb->ang_momentum[0] = lx;
    rb->ang_momentum[1] = ly;
    rb->ang_momentum[2] = lz;
    rb->inv_inertia_z = p.inv_inertia[2];
    RigidBodyInfo* info = GetRigidBodyInfo(simulation(), rb->slot);
    rb->info = info;
    info->flags[3] = 0;
    std::memset(info, 0, sizeof(RigidBodyInfo));  // Clear64ByteData(info, 0x500)
    for (int i = 0; i < 3; ++i) info->half_size[i] = ee::max(0.001f, p.half_size[i]);
    info->half_size[3] = 0;
    for (int i = 0; i < 16; ++i) info->lever_pos[i] = new WWorldPos{};  // FastAlloc(0x40) + WWorldPos::WWorldPos
    info->owner_pos = p.owner ? p.owner->world_pos() : nullptr;
    // O raio da esfera envolvente: sqrt(hx² + hy² + hz²) pela VU0 (as meias-medidas antes do mínimo de 0,001).
    {
        Reg h, h2, acc, one;
        load(h, p.half_size);
        mul(h2, h, h, XYZ);
        const uint32_t onev[4] = {0x3f800000, 0, 0, 0};
        load(one, onev);
        mulabc(acc, h, h, BX, X);
        maddabc(acc, one, h2, BY, X);
        maddbc(h2, acc, one, h2, BZ, X);
        const uint32_t q = ps2::sqrt(h2[0]);
        const uint32_t r = ps2::add(0, q);  // vaddq.x vf3, vf0, Q
        rb->radius_flags = r;
    }
    InitLevers(rb, p.owner, p.half_size);
    rb->state = p.moving ? 2 : 1;
    info->corner_buffer = 0;
    // A orientação em matriz e o tensor inverso no mundo: R (VU0_quattom4), Rᵀ, D = diag(I⁻¹) e R (D Rᵀ).
    float rot[4][4], rt[4][4], d[4][4], drt[4][4] = {}, inv[4][4] = {};
    VU0_quattom4(rb->quat, rot);
    transpose4(rot, rt);
    GetBodyTensorMatrix4(rb, d);
    VU0_MATRIX3x4_mult(d, rt, drt);
    VU0_MATRIX3x4_mult(rot, drt, inv);
    StoreBodyMatrix(rb, rot);
    StoreInvWorldTensor(rb, inv);
    // As alavancas no mundo (l * R, com o w de l pela linha 3): um do-while sobre a contagem.
    {
        Reg r0, r1, r2, r3, l, acc, out;
        load(r0, rot[0]);
        load(r1, rot[1]);
        load(r2, rot[2]);
        load(r3, rot[3]);
        int n = info->lever_count;
        int i = 0;
        do {
            load(l, info->lever_local[i]);
            mulabc(acc, r0, l, BX, XYZW);
            maddabc(acc, r1, l, BY, XYZW);
            maddabc(acc, r2, l, BZ, XYZW);
            maddbc(out, acc, r3, l, BW, XYZW);
            store(info->lever_world[i], out);
            ++i;
        } while (--n != 0 && i < 16);
    }
    UpdateZonalInfo(rb);
    if (uint8_t(rb->type - 1) < 2) rb->radius_flags |= 1u;
    else rb->radius_flags &= ~1u;
    if (p.flag2) rb->radius_flags |= 2u;
    else rb->radius_flags &= ~2u;
    info->age = 0xffff;
}

// [driving] 0x1f77e0 RigidBody::~RigidBody
void RigidBody_Destruct(RigidBody* rb) {
    rb->type = 0;
    for (int i = 0; i < 16; ++i) {
        delete rb->info->lever_pos[i];  // WWorldPos::~WWorldPos(.., 3)
        rb->info->lever_pos[i] = nullptr;
    }
    rb->info = nullptr;
}

// [driving] 0x1f7c60 RigidBody::ApplyInitialForcesAndTorques
void ApplyInitialForcesAndTorques(RigidBody* rb) {
    float gravity[4] = {0, g_tuning.gravity, 0, 0};          // sp+0x00
    const float jump[4] = {0, g_tuning.jump_gravity, 0, 0};  // sp+0x10
    float drag[4] = {0, 0, 0, 0};                            // sp+0x20
    std::memset(rb->force, 0, 12);
    std::memset(rb->torque, 0, 12);
    RigidBodyInfo* info = rb->info;
    if (info->age < 0xffff) info->age = uint16_t(info->age + 1);
    info->flags[3] = info->flags[2];
    info->flags[2] = 0;
    auto velocity_drag = [&](float k) {  // drag = v * k (xyz), y = 0
        Reg v, t;
        std::memcpy(drag, rb->vel, 12);
        load(v, drag);
        load(t, drag);
        const uint32_t kv[4] = {b(k), 0, 0, 0};
        mulbc(t, v, kv, BX, XYZ);
        store(drag, t);
        drag[1] = 0;
    };
    if (g_underwater && rb->type != 1) {
        gravity[1] = ee::mul(gravity[1], 0.1f);
        velocity_drag(-0.1f);
        ResolveMassScaledForce(rb, drag);
    }
    bool skip_gravity = false;
    if (rb->type < 4) {
        PhysicsAttributes* attr = owner_attributes(GetOwner(rb));
        if (attr && attr->heavy_ground != 0) {
            gravity[1] = ee::mul(gravity[1], 0.2f);
            velocity_drag(-1.0f);
            ResolveMassScaledForce(rb, drag);
        }
        bool player_like = rb->type == 1;
        if (!player_like) {
            PhysicsObject* owner = GetOwner(rb);
            player_like = owner && ps2_strcasecmp(owner->name(), "paradis_car") == 0;
        }
        if (player_like) {
            PhysicsObject* owner = GetOwner(rb);
            if (owner && owner->virtual_18c() == 0) {
                owner->virtual_244(1);
                std::memcpy(gravity, jump, 12);
            } else if (owner && owner->virtual_23c() != 0) {
                owner->virtual_244(0);
                g_tuning.jump_gravity = ps2_strcasecmp(owner->name(), "jungle_truck") == 0
                                            ? -34.0f
                                            : g_tuning.bodge_player_gravity;
            }
        }
        attr = owner_attributes(GetOwner(rb));
        if (attr && attr->special_mode == 1) skip_gravity = true;
    }
    if (!skip_gravity) ResolveMassScaledForce(rb, gravity);
    // Um corpo comum parado no chão (três alavancas ou mais) perde 4 x GROUND_FRICTION_COEFF da velocidade.
    if (rb->ground_levers < 3 || rb->type < 4 || info->flags[3] != 0 || info->flags[1] != 0) return;
    const float k = ee::mul(g_tuning.ground_friction_coeff, 4.0f);
    Reg v, t, scaled;
    load(v, &rb->vel[0]);
    load(t, gravity);  // sp+0x00: o w é o que a pilha tinha
    const uint32_t kv[4] = {b(k), 0, 0, 0};
    mulbc(t, v, kv, BX, XYZ);
    load(scaled, &rb->vel[0]);
    sub(scaled, v, t, XYZ);
    store(&rb->vel[0], scaled);
    Reg vv, p;
    load(vv, &rb->vel[0]);
    load(p, &rb->momentum[0]);
    const uint32_t m[4] = {b(rb->mass), 0, 0, 0};
    mulbc(p, vv, m, BX, XYZ);
    store(&rb->momentum[0], p);
}

// [driving] 0x1f6ae0 RigidBody::TempGetHeightInformation
int TempGetHeightInformation(RigidBody* rb, const float pos[3], float out[4], WWorldPos* wp) {
    WorldCollision* w = world();
    int result = 1;
    // O WCollisionMgr global (+0x2c = 0x20 durante a consulta, 0x10 depois) não foi portado.
    bool found = false;
    if (!wp) {
        physics_pending("WWorldPos nulo");
        out[0] = 0;
        out[1] = 1.0f;
        out[2] = 0;
        out[3] = 0;
        return 0;
    }
    if (w) found = w->find_closest_face(*wp, pos, true);
    else physics_pending("WWorldPos::FindClosestFace (mundo)");
    if (wp->face == nullptr) {
        if (wp->surface[0] != 0) {
            found = true;
            float p[3] = {pos[0], pos[1], pos[2]};
            for (int tries = 1;; ++tries) {
                p[0] = ee::add(p[0], 0.1f);
                if (w) w->find_closest_face(*wp, p, true);
                if (wp->face != nullptr) break;
                if (tries >= 2) {
                    if (wp->surface[0] != 0) {
                        result = 0;
                        wp->face = nf::p32_ptr<const void>(1);
                    }
                    break;
                }
            }
        } else {
            if (w) w->make_face_at_point(*wp, pos);
            else physics_pending("WWorldPos::MakeFaceAtPoint (mundo)");
            found = true;
        }
    }
    if (found) {
        if (wp->face == nullptr) {
            out[2] = 0;
            out[1] = 1.0f;
            out[0] = 0;
            out[3] = 0;
        } else {
            const float e1[3] = {ee::sub(wp->v1[0], wp->v0[0]), ee::sub(wp->v1[1], wp->v0[1]),
                                 ee::sub(wp->v1[2], wp->v0[2])};
            const float e2[3] = {ee::sub(wp->v0[0], wp->v2[0]), ee::sub(wp->v0[1], wp->v2[1]),
                                 ee::sub(wp->v0[2], wp->v2[2])};
            // 0x25a8e8: o produto vetorial e1 x e2.
            const float n[3] = {ee::sub(ee::mul(e1[1], e2[2]), ee::mul(e1[2], e2[1])),
                                ee::sub(ee::mul(e1[2], e2[0]), ee::mul(e1[0], e2[2])),
                                ee::sub(ee::mul(e1[0], e2[1]), ee::mul(e1[1], e2[0]))};
            if (ee::eq(n[0], 0.0f) && ee::eq(n[1], 0.0f) && ee::eq(n[2], 0.0f)) {
                out[2] = 0;
                out[1] = 1.0f;
                out[0] = 0;
            } else {
                const float len2 = ee::add(ee::add(ee::mul(n[0], n[0]), ee::mul(n[1], n[1])), ee::mul(n[2], n[2]));
                const float r = ee::rsqrt(1.0f, len2);
                out[2] = ee::mul(n[2], r);
                out[0] = ee::mul(n[0], r);
                out[1] = ee::mul(n[1], r);
            }
            if (ee::lt(out[1], 0.0f)) {
                out[1] = ee::neg(out[1]);
                out[0] = ee::neg(out[0]);
                out[2] = ee::neg(out[2]);
            }
            // 0x3f7ff972 = 0,9999: uma normal quase vertical fica um pouco inclinada (o y limitado, x e z não mudam).
            const float kMaxUp = ps2::ffloat(0x3f7ff972u);
            if (ee::le(kMaxUp, out[1])) out[1] = kMaxUp;
            out[3] = 0;
        }
    }
    if (wp->face == nullptr) {
        out[3] = 0;
        return result;
    }
    // Com a face guardada de antes e FindClosestFace sem achar (found falso), o PS2 não refaz a normal: a penetração
    // sai com a normal velha que out já tinha (a do quadro anterior).
    const float bump = w ? w->surface_bump_height(pos, wp->surface) : 0.0f;
    if (!w) physics_pending("WCollisionMgr::SurfaceBumpHeight (mundo)");
    float d = ee::mul(ee::sub(wp->v0[0], pos[0]), out[0]);
    d = ee::add(d, ee::mul(ee::sub(wp->v0[1], pos[1]), out[1]));
    d = ee::add(d, ee::mul(ee::sub(wp->v0[2], pos[2]), out[2]));
    d = ee::add(d, bump);
    out[3] = d;
    if (rb->type < 4) {
        const PhysicsAttributes* attr = owner_attributes(GetOwner(rb));
        if (attr && attr->heavy_ground != 0) out[3] = ee::add(out[3], 10.0f);
    }
    return result;
}

// [driving] 0x1f9500 RigidBody::CollideWithGround
void CollideWithGround(RigidBody* rb) {
    if (g_underwater) return;
    if (rb->type < 4) {
        const PhysicsAttributes* attr = owner_attributes(GetOwner(rb));
        if (attr && attr->skip_ground != 0) return;
        PhysicsObject* owner = GetOwner(rb);
        if (owner && owner->ignores_ground()) {
            rb->info->ground[0] = 0;
            rb->info->ground[1] = 1.0f;
            rb->info->ground[2] = 0;
            return;
        }
    }
    RigidBodyScratch& s = rigid_scratch();
    int n = 0;
    rb->ground_levers = 0;
    float deepest = 0;
    int deepest_lever = 0;
    int first, end;
    if (rb->type < 4) {
        first = 8;
        if (!ee::lt(-32.0f, rb->vel[1]) && rb->type != 1 && (rb->radius_flags & 1)) {
            float sp = ee::add(ee::mul(rb->vel[0], rb->vel[0]), ee::mul(rb->vel[1], rb->vel[1]));
            sp = ee::add(sp, ee::mul(rb->vel[2], rb->vel[2]));
            first = ee::lt(25.0f, sp) ? 4 : 8;
        }
        if (ee::lt(0.8f, GetOrientToGround(rb))) {
            end = 12;
        } else {
            first = 4;
            end = rb->info->lever_count;
        }
    } else {
        first = 0;
        end = rb->info->lever_count;
    }
    TempGetHeightInformation(rb, rb->pos, rb->info->ground, rb->info->owner_pos);
    for (int i = first; i < end; ++i) {
        RigidBodyInfo* info = rb->info;
        const float* lever = info->lever_world[i];
        float* ground = info->lever_ground[i];
        if (ee::le(info->ground[3], 0.0f) && ee::lt(0.0f, lever[1])) continue;  // centro fora do chão: só as de baixo
        const float p[3] = {ee::add(lever[0], rb->pos[0]), ee::add(lever[1], rb->pos[1]),
                            ee::add(lever[2], rb->pos[2])};
        TempGetHeightInformation(rb, p, ground, info->lever_pos[i]);
        const float depth = ground[3];
        if (!ee::lt(0.0f, depth)) continue;
        if (ee::lt(deepest, depth)) {
            deepest = depth;
            deepest_lever = i;
        }
        std::memcpy(s.levers[n], lever, 16);
        std::memcpy(s.normals[n], ground, 16);
        ++n;
    }
    if (n != 0) {
        LeverParams params;
        params.unused0 = 0;
        params.count = n;
        rb->ground_levers = int8_t(n);
        params.friction = rb->info->lever_friction;
        params.restitution = rb->info->lever_restitution;
        ResolveLeverForces(rb, &params, s.levers, s.normals, rb->info->inv_world_tensor);
        if (ee::lt(0.0f, deepest)) {
            const float* g = rb->info->lever_ground[deepest_lever];
            rb->pos[0] = ee::add(rb->pos[0], ee::mul(g[0], deepest));
            rb->pos[1] = ee::add(rb->pos[1], ee::mul(g[1], deepest));
            rb->pos[2] = ee::add(rb->pos[2], ee::mul(g[2], deepest));
        }
    }
    rb->info->ground[3] = ee::add(rb->info->ground[3], rb->pos[1]);
}

}  // namespace drv
