// Os motoristas da IA (driving/ai/ground_vehicle.h), portados do MIPS do DRIVING.ELF (AIElementController.obj). As
// desmontagens ficam em out/agent_drv_traffic/ do checkout principal (fora do git).
//
// Divergências (port), cada uma também na função:
//  - o som do carro (o AVehicle do PBondCar +0x388): AVehicle::Reset no UnSpawn, o +0x244 (a sirene) no SetAttackMode e
//    o +0x154 (a buzina) no CheckTrafficCollision só contam em ground_vehicle_stats().audio_resets;
//  - os estados 1, 2, 3 e 5 do Update, os modos de ataque 13-15 e os de navegação 2, 4 e 5 do DoMoveMode (o jogador,
//    a direção automática, os trilhos, os agentes) contam em ground_vehicle_pending() e não fazem nada;
//  - IntersectionGreenLight: o port não tem o RPathEngine (os objetos que andam por trilhos de animação), então o
//    laço dos trilhos não acha nenhum;
//  - Spawn: RRenderWorldCulling::IsInFrustum2d (com a bandeira +0x350 do SMissionManager) não foi portado: conta como
//    fora da vista;
//  - guardas (contadas em ground_vehicle_stats().guards): o carro sem motorista (o PS2 leria o endereço 0x70 no
//    CheckTrafficCollision), o alvo nulo do UpdateNavPos com as bandeiras +0xb1/+0xb2 do navegador (o PS2 leria o
//    endereço 4).

#include "driving/ai/ground_vehicle.h"

#include <cstring>
#include <functional>
#include <map>
#include <string>

#include "driving/ai/road_nav.h"
#include "driving/ai/road_network.h"
#include "driving/ai/road_spawn.h"
#include "driving/ai/sim_random.h"
#include "driving/game/bond_car.h"
#include "driving/game/rail.h"
#include "driving/physics/simulation.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

const char* const kTrafficTypes[7] = {"coupe", "sedan2", "hatchback", "sedan", "station_wagon", "suv1", "taxi"};

namespace {

VehicleControllerGlobals g_ctl;
GroundVehicleStats g_stats;
std::map<std::string, int> g_pending;

alignas(16) AIGroundVehicle g_player_storage[kPlayerGroundVehicles];
alignas(16) AIGroundVehicle g_traffic_storage[kTrafficVehicles];

constexpr uint32_t kOne = 0x3f800000u;
uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// vmul.xyz; vmulax.x; vmadday.x; vmaddz.x
uint32_t len_sq(const float v[3]) {
    uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[1]), bits(v[1])));
    return ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2])));
}
// ... vsqrt; vaddq.x vf0
float vu_len(const float v[3]) { return flt(ps2::add(0, ps2::sqrt(len_sq(v)))); }
// vmul.xz (ou .xyz); vmulax.x; vmaddz.x: x² + z²
float vu_len_xz(const float v[3]) {
    const uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    return flt(ps2::add(0, ps2::sqrt(ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2]))))));
}
// vrsqrt Q, vf0w, |v|²; vmulq.xyz
void vu_unit(float d[3], const float v[3]) {
    const uint32_t q = ps2::rsqrt(kOne, len_sq(v));
    float r[3];
    for (int i = 0; i < 3; ++i) r[i] = flt(ps2::mul(bits(v[i]), q));
    std::memcpy(d, r, 12);
}
void vsub3(float d[3], const float a[3], const float b[3]) {
    for (int i = 0; i < 3; ++i) d[i] = ee::sub(a[i], b[i]);
}
// vmul.xyz; vaddy.x; vaddz.x
float vu_dot(const float a[3], const float b[3]) {
    const float x = ee::mul(a[0], b[0]), y = ee::mul(a[1], b[1]), z = ee::mul(a[2], b[2]);
    return ee::add(ee::add(x, y), z);
}
float vu_dist(const float a[3], const float b[3]) {
    float d[3];
    vsub3(d, a, b);
    return vu_len(d);
}
float vu_dist_xz(const float a[3], const float b[3]) {
    float d[3];
    vsub3(d, a, b);
    return vu_len_xz(d);
}
void copy3(float d[3], const float s[3]) { std::memcpy(d, s, 12); }

RigidBody* body_of(BondCar& car) { return GetRigidBody(simulation(), car.d.slot); }

// As entradas da tabela virtual do PBondCar que o motorista chama (os acessores de uma instrução do PS2).
void set_steering(BondCar& c, float v) { c.d.control.steering = v; }          // +0xd4 (0x193018)
void set_gas(BondCar& c, float v) { c.d.control.gas = v; }                    // +0xf4 (0x193038)
void set_brake(BondCar& c, float v) { c.d.control.brake = v; }                // +0xfc (0x193040)
void set_hand_brake(BondCar& c, uint8_t v) { c.d.control.hand_brake = v; }   // +0x104 (0x193048)
bool emp_active(const BondCar& c) { return c.d.emp_active != 0; }             // +0x144 (0x193108)
bool in_shock(const BondCar& c) { return c.d.in_shock > 0; }                  // +0x17c (0x193148)

// A guarda do carro nulo (o PS2 leria o PhysicsObject no endereço 0).
BondCar* car_or_guard(AIGroundVehicle& v) {
    if (!v.car) ++g_stats.guards;
    return v.car;
}

void set_spline_path_null(AIGroundVehicle& v) {
    // AIVehicle::SetSplinePath(0) (0x0013dfd8, driving/game/rail.h): o trilho em uso e o seu RPathHandle destruídos.
    AIVehicle_SetSplinePath(v, nullptr);
}

void road_count(int16_t road, int delta) {
    // WRoad +0x2e, com o teste de 0 <= rua < fNumRoads do PS2.
    if (road >= 0 && road < road_network().num_roads) {
        WRoad* r = road_road(road);
        r->count2e = uint8_t(r->count2e + delta);
    }
}

}  // namespace

VehicleControllerGlobals& vehicle_controller() { return g_ctl; }
GroundVehicleStats& ground_vehicle_stats() { return g_stats; }
void ground_vehicle_pending(const char* name) { g_pending[name]++; }
int ground_vehicle_pending_count(const char* name) {
    auto it = g_pending.find(name);
    return it == g_pending.end() ? 0 : it->second;
}

AIGroundVehicle* ai_of(const BondCar& car) { return nf::p32_ptr<AIGroundVehicle>(car.d.ai_ground_vehicle); }
BondCar* car_of(const AIGroundVehicle& v) { return v.car; }
WRoadNav* nav_of(const AIGroundVehicle& v) { return nf::p32_ptr<WRoadNav>(v.nav); }

namespace {
WRoadNav* new_nav() {
    // UMemory::FastAlloc(0xc0) e WRoadNav::WRoadNav.
    WRoadNav* n = new WRoadNav{};
    WRoadNav_Construct(*n);
    return n;
}
}  // namespace

// [driving] 0x0013de68 AIVehicle::AIVehicle e 0x0012dd80 AIGroundVehicle::AIGroundVehicle
void AIGroundVehicle_Construct(AIGroundVehicle& v) {
    // A memória estática do PS2 começa zerada; o port também (os campos que o Reset não escreve: +0x80, +0x84...).
    std::memset(&v, 0, kAIGroundVehicleSize);
    v.car = nullptr;
    v.vehicle_type = 0;
    v.vtable = 0x36b568;  // a tabela do AIGroundVehicle (nada a lê no port)
    v.physics = 0;
    v.active = 0;
    v.spline_path = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) v.rot_pos[i][j] = i == j ? 1.0f : 0.0f;  // 0x355bb0
    v.nav = nf::p32_u(new_nav());
    v.controller = 0;
    v.nav2 = nf::p32_u(new_nav());
    v.nav3 = nf::p32_u(new_nav());
    v.nav4 = nf::p32_u(new_nav());
    v.nav5 = nf::p32_u(new_nav());
    AIGroundVehicle_Reset(v);
}

// [driving] 0x0012ded8 AIGroundVehicle::Reset
void AIGroundVehicle_Reset(AIGroundVehicle& v) {
    WRoadNav_Reset(*nav_of(v));
    set_spline_path_null(v);
    v.physics = 0;  // SetPhysicsObject(0)
    v.car = nullptr;
    v.active = 0;
    v.road = -1;
    v.valid_command = 0;
    v.controller_idx = 0;
    v.controller = 0;
    v.unknown074 = 0;
    v.ai_command = 0;
    v.speed = 0.0f;
    v.update_state = 0;
    std::memset(v.unknown098, 0, sizeof v.unknown098);
    v.lane_weight_width = 3;
    v.collision_info = 0;
    v.collision_byte = 0;
    v.shocked = 0;
    std::memset(v.unknown0e8, 0, 8);
    WRoadNav_Reset(*nf::p32_ptr<WRoadNav>(v.nav2));
    std::memset(v.unknown0f8, 0, 8);
    WRoadNav_Reset(*nf::p32_ptr<WRoadNav>(v.nav3));
    std::memset(v.unknown108, 0, 8);
    WRoadNav_Reset(*nf::p32_ptr<WRoadNav>(v.nav4));
    WRoadNav_Reset(*nf::p32_ptr<WRoadNav>(v.nav5));
    v.is_sub = 0;
    v.rec_speed_offset = 1.0f;
    v.use_ceiling_floor = 0;
    v.ceiling = 0.0f;
    v.floor = 0.0f;
    v.unknown124 = 0.0f;
    v.unknown128 = 0.0f;
    v.has_turret = v.has_machine_guns = v.has_rockets = v.has_missiles = 0;
    v.accuracy = 0;
    v.damage = 0;
    v.speed_limit = 0.0f;
    v.speed_scaler = 0.0f;
    v.reversing = 0;
    v.coll_avoidance = 1;
    v.unknown148 = 0;
    v.ram_freq = 0.0f;
    v.unknown144 = simulation().frame_rate * 5;
    v.lead_scaler = 0.0f;
    v.recorded_glue = 0;
    v.unknown15c = 0;
    v.steering_disabled = 0;
    v.unknown164 = 0;
    v.been_in_smoke = 0;
    v.been_in_oil = 0;
    v.disabled = 0;
    v.unknown174 = 0;
    v.min_sim_update = 0;
    std::memset(v.min_sim_pos, 0, 12);
    std::memset(v.min_sim_dir, 0, 12);
    v.collision_ahead = 0;
    v.red_light = 0;
    v.stop_now = 0;
    v.force_full_update = 0;
    v.primary_target = 0;
    std::memset(v.unknown1a8, 0, 12);
    std::memset(v.target_pos, 0, 12);
    v.unknown1cc = 0;
    v.unknown1d0 = 0.0f;
    std::memset(v.direction, 0, 12);  // o w (+0x1ec) fica
    std::memset(v.drive_to, 0, 12);
    v.unknown1fc = -1;
    v.unknown208 = 1;
    v.default_target = 0;
    v.unknown204 = 0;
    v.unknown20c = 0;
    std::memset(v.unknown210, 0, 12);
    v.unknown21c = 1;
    v.weapons_active = 1;
    v.min_dist_to_target = 0.0f;
    v.unknown226 = 0;
    v.rate_of_fire = 0.0f;
    v.unknown22c = 0;
    v.unknown22e = 0;
    v.unknown230 = 0;
    v.unknown232 = 0;
    v.turret_angle = 0.0f;
    v.unknown238 = 0;
    std::memset(v.unknown240, 0, 12);
    std::memset(v.unknown250, 0, 12);
    v.dir_override_timer = 0;
    v.reverse_override = 0;
    v.stuck = 0;
    v.unknown268 = 0;
    std::memset(v.unknown270, 0, 12);
    v.unknown27c = 1;
    std::memset(v.spawn_pos, 0, 12);
    v.spawn_timer = 0;
    v.can_spawn = 0;
}

// [driving] 0x0012e350 AIGroundVehicle::Update
void AIGroundVehicle_Update(AIGroundVehicle& v) {
    if (v.active == 0 || v.valid_command == 0) return;
    if (v.force_full_update != 0) {
        v.update_state = 0;
        v.force_full_update = 0;
    }
    ++g_stats.updates;
    switch (v.controller) {
        case 1: AIGroundVehicle_DoPlayerLogic(v); break;  // driving/game/rail.h
        case 2:  // a direção automática (driving/ai/ground_vehicle_agent.cpp)
            AIGroundVehicle_UpdateStuck(v);
            AIGroundVehicle_DoAutoDriveUpdate(v);
            AIGroundVehicle_DoAttackMode(v);
            AIGroundVehicle_DoMoveMode(v);
            break;
        case 3: AIGroundVehicle_DoSplinePathUpdate(v); break;  // o trilho (driving/game/rail.h)
        case 4:
            AIGroundVehicle_DoTrafficStateUpdate(v);
            AIGroundVehicle_DoAttackMode(v);
            AIGroundVehicle_DoMoveMode(v);
            break;
        case 5:  // os agentes (driving/ai/ground_vehicle_agent.cpp)
            AIGroundVehicle_UpdateStuck(v);
            AIGroundVehicle_DoAgentStateUpdate(v);
            AIGroundVehicle_DoAttackMode(v);
            AIGroundVehicle_DoWeaponUpdate(v);
            AIGroundVehicle_DoMoveMode(v);
            break;
        default: break;
    }
    // GlareOn(1) (+0x174): os faróis.
    if (BondCar* car = car_or_guard(v)) PBondCar_GlareOn(*car, 1);
}

// [driving] 0x00132d80 AIGroundVehicle::DoTrafficStateUpdate
void AIGroundVehicle_DoTrafficStateUpdate(AIGroundVehicle& v) {
    if (v.update_state == 0) AIGroundVehicle_Spawn(v, g_ctl.max_active_traffic);
    if (v.attack_mode == 12) {
        const WRoadSegment* s = road_segment(nav_of(v)->segment);
        // A rua do trecho onde está: sai da contagem da anterior e entra na nova.
        if (v.road == -1 || (s->road != -1 && s->road != v.road)) {
            if (v.road != -1) road_count(v.road, -1);
            road_count(s->road, +1);
            v.road = s->road;
        }
    }
    BondCar* car = car_or_guard(v);
    if (!car) return;
    if (v.shocked == 0) {
        if (in_shock(*car)) {
            v.shocked = 1;
            return;
        }
        if (v.shocked == 0) return;
    }
    if (in_shock(*car)) return;
    // Depois do choque, quando quase parado (x² + z² da velocidade, vmul.xz) volta a dirigir.
    if (ee::le(vu_len_xz(body_of(*car)->vel), flt(0x3e4ccccdu))) v.shocked = 0;  // 0,2
}

// [driving] 0x00132808 AIGroundVehicle::Spawn
// Divergência: RRenderWorldCulling::IsInFrustum2d (com SMissionManager +0x350) conta como fora da vista.
void AIGroundVehicle_Spawn(AIGroundVehicle& v, int16_t max_active) {
    if (v.active == 0) return;
    BondCar* car = car_or_guard(v);
    if (!car) return;
    Simulation& sim = simulation();
    RigidBody* body = body_of(*car);
    float pos[3];
    if (v.min_sim_update != 0) copy3(pos, nav_of(v)->position);
    else copy3(pos, body->pos);
    if (AIRoadSpawn_TrafficRespawnAvailable(pos)) {
        if (sim.steps < v.spawn_timer) return;
        if (v.attack_mode == 12 && body->state == 2) AIGroundVehicle_UnSpawn(v);
        int16_t seg = 0;
        int8_t lane = 0;
        float t = 0.0f;
        int32_t fwd = 1;
        if (g_ctl.num_active_traffic < max_active) {
            if (AIRoadSpawn_GetRandomTrafficSpawn(&seg, &lane, &t, &fwd)) {
                WRoadNav* nav = nav_of(v);
                WRoadNav_InitAtSegment(*nav, seg, lane, t, fwd != 0);
                copy3(v.spawn_pos, nav->road_point);
                if (traffic_world().mission_camera_check == 0) {
                    v.can_spawn = 1;
                } else {
                    ground_vehicle_pending("RRenderWorldCulling::IsInFrustum2d");
                    v.can_spawn = 1;
                }
            }
        }
    }
    if (v.can_spawn == 0) return;
    if (AIRoadSpawn_TrafficRespawnPosCheck(v.spawn_pos, body->info->half_size[2])) {
        AIGroundVehicle_SetAttackMode(v, 12);
        AIGroundVehicle_SetNavigateMode(v, 3, 0);
        float dir[3];
        copy3(dir, nav_of(v)->direction);
        body->state = 2;
        PBondCar_ResetDamage(*car);       // +0x19c
        PBondCar_ClearEMPState(*car);     // +0x1bc
        PBondCar_ResetCar(*car, v.spawn_pos, dir);  // +0x164
        v.shocked = 0;
        int32_t n = int32_t(int16_t(g_ctl.num_active_traffic + 1));
        if (n < 0) n = 0;
        g_ctl.num_active_traffic = int16_t(n);
        v.spawn_timer = sim.steps + sim.frame_rate * 5;
        ++g_stats.spawns;
    }
    v.can_spawn = 0;
}

// [driving] 0x00132ba8 AIGroundVehicle::UnSpawn
// Divergência: AVehicle::Reset (o som do carro) só conta.
void AIGroundVehicle_UnSpawn(AIGroundVehicle& v) {
    g_ctl.unspawn_z = ee::min(5000.0f, ee::max(0.0f, ee::add(g_ctl.unspawn_z, 50.0f)));
    const float z = ee::add(g_ctl.unspawn_z, 40000.0f);
    AIGroundVehicle_SetAttackMode(v, 1);
    AIGroundVehicle_SetNavigateMode(v, 1, 0);
    float pos[4] = {0.0f, 0.0f, z, 0.0f};  // memset de 12 bytes e o z em +8
    const float dir[3] = {1.0f, 0.0f, 0.0f};  // 0x36b550
    float m[4][4];
    Util_GenerateMatrix(m, dir);
    BondCar* car = car_or_guard(v);
    if (car) {
        RigidBody* body = body_of(*car);
        RigidBody_ResetObject(body, m, pos);
        body->state = 3;
    }
    int32_t n = int32_t(int16_t(g_ctl.num_active_traffic - 1));
    if (n < 0) n = 0;
    g_ctl.num_active_traffic = int16_t(n);
    if (v.road != -1) {
        road_count(v.road, -1);
        v.road = -1;
    }
    ++g_stats.audio_resets;  // AVehicle::Reset(GetAudio())
    v.spawn_timer = 0;
    ++g_stats.unspawns;
}

// [driving] 0x0012e468 AIGroundVehicle::SetNavigateMode
void AIGroundVehicle_SetNavigateMode(AIGroundVehicle& v, int32_t mode, uint32_t path) {
    if (mode == 0) return;
    v.navigate_mode = mode;
    WRoadNav* nav = nav_of(v);
    if (v.controller == 4) {
        nav->mode = mode == 1 ? 0 : 1;
        return;
    }
    if (mode != 5) set_spline_path_null(v);
    if (mode == 1) {
        nav->mode = 0;
        return;
    }
    BondCar* car = car_or_guard(v);
    if (!car) return;
    const RigidBody* body = body_of(*car);
    float pos[3], fwd[3];
    copy3(pos, body->pos);
    copy3(fwd, body->info->orient[2]);
    if (mode == 4) {
        nav->mode = 4;
        WRoadNav_InitAtPoint(*nav, pos, fwd, v.coll_avoidance == 0);
        // AttributeSet::LookupBool(+0x5c, "IS_HELI")
        v.unknown128 = car->is_heli ? 2.5f : 10.0f;
        v.unknown124 = g_ctl.nav_spread;
        if (ee::lt(1.0f, g_ctl.nav_spread)) g_ctl.nav_spread = 0.0f;
        else g_ctl.nav_spread = ee::add(g_ctl.nav_spread, 0.25f);
        return;
    }
    if (mode == 3) {
        nav->mode = 3;
        WRoadNav_InitAtPoint(*nav, pos, fwd, false);
        return;
    }
    if (mode != 5) return;
    nav->mode = 0;
    AIVehicle_SetSplinePath(v, nf::p32_ptr<const AISplineRec>(path));  // o trilho 'AISp' (driving/game/rail.h)
}

// [driving] 0x0012e6f8 AIGroundVehicle::SetAttackMode
// Divergência: a sirene (o +0x244 do som do carro com HAS_SIREN, modo 13) só conta.
void AIGroundVehicle_SetAttackMode(AIGroundVehicle& v, int32_t mode) {
    if (mode == 0) return;
    v.attack_mode = mode;
    if (mode != 13) return;
    BondCar* car = car_or_guard(v);
    if (car && car->has_siren) ++g_stats.audio_resets;
}

// [driving] 0x0012e808 AIGroundVehicle::DoAttackMode (os modos do tráfego: 1, 12 e o padrão; os dos agentes, 13-15, em
// ground_vehicle_agent.cpp: AIGroundVehicle_DoAttackModeAgent)
void AIGroundVehicle_DoAttackMode(AIGroundVehicle& v) {
    BondCar* car = car_or_guard(v);
    if (!car) return;
    if (v.controller == 5 && ee::le(car->hit_points(), 0.0f)) {
        v.speed = 0.0f;
        return;
    }
    if (v.controller == 4 && ee::lt(car->hit_points(), 0.5f)) {
        v.speed = 0.0f;
        return;
    }
    if (emp_active(*car) || v.disabled != 0) {
        v.speed = 0.0f;
        return;
    }
    switch (v.attack_mode) {
        case 1: {
            const float d = vu_dist_xz(v.target_pos, body_of(*car)->pos);
            if (ee::le(d, 2.5f)) v.speed = 0.0f;
            else if (ee::le(d, ee::max(10.0f, v.min_dist_to_target))) v.speed = ee::mul(v.unknown1d0, 0.5f);
            else v.speed = v.speed_limit;
            return;
        }
        case 12: v.speed = 7.5f; return;
        case 13:
        case 14:
        case 15:
            AIGroundVehicle_DoAttackModeAgent(v);  // driving/ai/ground_vehicle_agent.cpp
            return;
        default: v.speed = 0.0f; return;
    }
}

// [driving] 0x00130508 AIGroundVehicle::DoMoveMode (os modos 1 e 3 do tráfego; os dos agentes em
// ground_vehicle_agent.cpp: AIGroundVehicle_DoMoveModeAgent)
void AIGroundVehicle_DoMoveMode(AIGroundVehicle& v) {
    BondCar* car = car_or_guard(v);
    if (!car) return;
    bool stop = false;
    if (v.controller == 5 && ee::le(car->hit_points(), 0.0f)) stop = true;
    else if (v.controller == 4 && ee::lt(car->hit_points(), 0.5f)) stop = true;
    else if (emp_active(*car) || v.disabled != 0) stop = true;
    RigidBody* body = body_of(*car);
    if (stop) {
        // Parado: sem controles, e o freio (ou o acelerador andando para trás) até 0,25.
        if (car->physics()->sub_physics != 0) return;
        set_gas(*car, 0.0f);
        set_brake(*car, 0.0f);
        set_hand_brake(*car, 0);
        set_steering(*car, 0.0f);
        const float speed = vu_len(body->vel);
        if (!ee::le(0.1f, speed)) return;
        if (in_shock(*car)) return;
        float fwd[3], vel[3];
        copy3(fwd, body->info->orient[2]);
        vu_unit(vel, body->vel);
        const float speed2 = vu_len(body->vel);
        if (ee::lt(vu_dot(vel, fwd), 0.0f)) set_gas(*car, ee::min(0.25f, speed2));
        else set_brake(*car, ee::min(0.25f, speed2));
        return;
    }
    switch (v.navigate_mode) {
        case 1:
            copy3(v.drive_to, body->pos);
            if (v.controller == 5) AIGroundVehicle_DoMoveModeAgent(v);  // DriveToPoint
            return;
        case 3: {
            const float la = AIGroundVehicle_GetLookAhead(v, v.speed);
            if (v.controller != 4) {
                AIGroundVehicle_DoMoveModeAgent(v);  // driving/ai/ground_vehicle_agent.cpp
                return;
            }
            if (v.red_light == 0 || in_shock(*car)) AIGroundVehicle_UpdateNavPos(v, la, v.drive_to);
            if (v.shocked != 0) {
                // Depois de um choque: sem acelerar, freando até parar.
                float fwd[3], vel[3];
                copy3(fwd, body->info->orient[2]);
                vu_unit(vel, body->vel);
                const float speed = vu_len(body->vel);
                set_gas(*car, 0.0f);
                set_brake(*car, 0.0f);
                if (ee::lt(vu_dot(vel, fwd), 0.0f)) set_gas(*car, ee::min(0.25f, speed));
                else set_brake(*car, ee::min(0.25f, speed));
                set_hand_brake(*car, 0);
                return;
            }
            if (v.min_sim_update != 0) {
                // vmulx.xyz por 7,5; vadd.xyz (o w de +0x1fc fica)
                const float* f = body->info->orient[2];
                for (int i = 0; i < 3; ++i) v.drive_to[i] = ee::add(body->pos[i], ee::mul(f[i], 7.5f));
            }
            if (v.coll_avoidance != 0) AIGroundVehicle_CheckTrafficCollision(v, v.drive_to);
            if (nav_of(v)->at_dead_end != 0) copy3(v.drive_to, body->pos);
            AIGroundVehicle_DriveToPointTraffic(v, v.drive_to, v.speed);
            return;
        }
        case 2:
        case 4:
        case 5:
            AIGroundVehicle_DoMoveModeAgent(v);  // driving/ai/ground_vehicle_agent.cpp
            return;
        default: return;
    }
}

// [driving] 0x00132f80 AIGroundVehicle::GetLookAhead
// (O argumento, a velocidade pedida, não é lido; a direção do carro ao ponto da rua é calculada e não usada.)
float AIGroundVehicle_GetLookAhead(AIGroundVehicle& v, float speed) {
    (void)speed;
    BondCar* car = car_or_guard(v);
    if (!car) return 0.0f;
    const RigidBody* body = body_of(*car);
    const float speed_xz = vu_len_xz(body->vel);
    return ee::max(ee::add(body->info->half_size[2], 7.5f), ee::mul(speed_xz, flt(0x3eb33333u)));  // 0,35
}

// [driving] 0x00130140 AIGroundVehicle::UpdateNavPos
void AIGroundVehicle_UpdateNavPos(AIGroundVehicle& v, float look_ahead, float out[3]) {
    WRoadNav* nav = nav_of(v);
    if (v.min_sim_update != 0) {
        if (v.collision_ahead == 0) {
            WRoadNav_IncNavPosition(*nav, 0.125f, v.direction, v.unknown1fc);
            copy3(v.min_sim_pos, nav->road_point);
            const WRoadSegment* s = road_segment(nav->segment);
            if (s->flags & kSegCurved) {
                float tan[4];
                RCameraSpline_EvaluateTangent(*road_nav_spline(*nav), nav->t, tan);
                copy3(v.min_sim_dir, tan);
            } else {
                copy3(v.min_sim_dir, nav->direction);
            }
        }
        copy3(out, nav->road_point);
        return;
    }
    BondCar* car = car_or_guard(v);
    if (!car) return;
    const RigidBody* body = body_of(*car);
    float np[3], pos[3];
    copy3(np, nav->road_point);
    copy3(pos, body->pos);
    const float d = (v.is_sub != 0 && v.use_ceiling_floor != 0) ? vu_dist_xz(pos, np) : vu_dist(pos, np);
    float dir[4] = {0, 0, 0, 0};
    AIGroundVehicle* target = nf::p32_ptr<AIGroundVehicle>(v.primary_target);
    if (target && v.attack_mode == 13) {
        float t[3];
        vsub3(t, body_of(*target->car)->pos, pos);
        vu_unit(dir, t);
    } else if (nav->unknownb1 != 0 || nav->unknownb2 != 0) {
        if (!target || !target->car) {
            ++g_stats.guards;  // o PS2 leria o PhysicsObject do alvo nulo (o endereço 4)
        } else {
            float t[3];
            if (nav->unknownb1 != 0) vsub3(t, body_of(*target->car)->pos, pos);
            else vsub3(t, pos, body_of(*target->car)->pos);
            vu_unit(dir, t);
        }
    } else {
        copy3(dir, v.direction);
    }
    if (ee::lt(d, look_ahead)) WRoadNav_IncNavPosition(*nav, ee::abs(ee::sub(look_ahead, d)), dir, v.unknown1fc);
    copy3(out, nav->road_point);
}

// [driving] 0x00135530 TestForCollision
bool TestForCollision(BondCar& car, const float point[4], const float other[3], float radius) {
    const RigidBody* body = body_of(car);
    float a[3], b[3], t[3];
    vsub3(t, point, body->pos);
    vu_unit(a, t);
    vsub3(t, other, body->pos);
    vu_unit(b, t);
    if (ee::lt(vu_dot(a, b), flt(0x3f666666u))) return false;  // 0,9
    if (ee::lt(vu_dist(other, point), radius)) return true;
    const float to_other = vu_dist(body->pos, other);
    const float to_point = vu_dist(body->pos, point);
    return ee::lt(to_other, to_point);
}

// [driving] 0x00135978 GetCollisionStopPoint
void GetCollisionStopPoint(BondCar& car, float point[4], const float other[3], float radius) {
    const RigidBody* body = body_of(car);
    float p[3], d[3], t[3];
    copy3(p, body->pos);
    vsub3(t, point, p);
    vu_unit(d, t);
    vsub3(t, other, p);
    float k = ee::sub(vu_dot(d, t), radius);
    k = ee::sub(k, ee::mul(body->info->half_size[2], 1.5f));
    k = ee::max(k, 0.0f);
    for (int i = 0; i < 3; ++i) point[i] = ee::add(p[i], ee::mul(d[i], k));
}

// [driving] 0x001377c8 AIGroundVehicle::CheckTrafficCollision
void AIGroundVehicle_CheckTrafficCollision(AIGroundVehicle& v, float point[4]) {
    v.collision_ahead = 0;
    v.red_light = 0;
    BondCar* car = car_or_guard(v);
    if (!car) return;
    WRoadNav* nav = nav_of(v);
    const WRoadSegment* seg = road_segment(nav->segment);
    if (seg->flags & 0x80) {
        const WRoadNode* node = road_node(seg->node[nav->forward]);
        if (node->segment_count >= 3) {
            const WRoadNode* opp = WRoadNetwork_GetSegmentOppNode(seg, node);
            const WRoadIntersection* inter = road_intersection(seg->intersection[nav->forward]);
            int32_t count = inter->segment_count;
            int32_t index = 0, directional = 0;
            for (int i = 0; i < int(inter->segment_count); ++i) {
                if (inter->segments[i] == nav->segment) index = directional;
                if ((road_segment(inter->segments[i])->flags & 2) == 0) ++directional;
                else --count;
            }
            if ((road_segment(nav->segment)->flags & 0x20) == 0 && count >= 3) {
                if (!AIVehicleController_IntersectionGreenLight(inter->position, index, count)) {
                    v.collision_ahead = 1;
                    v.red_light = 1;
                    GetCollisionStopPoint(*car, point, opp->position, 0.0f);
                    if (ee::le(vu_len_xz(body_of(*car)->vel), 1.0f)) v.stop_now = 1;
                    ++g_stats.red_lights;
                }
            }
        }
    }
    if (v.collision_ahead != 0) return;
    Simulation& sim = simulation();
    const RigidBody* me = body_of(*car);
    const int32_t my_slot = me->slot;
    const int32_t zx = me->info->zone[0], zy = me->info->zone[1], zz = me->info->zone[2];
    alignas(16) float stop[4];
    copy3(stop, point);
    stop[3] = 0.0f;
    bool found = false;
    float best = 0.0f;
    for (BondCar* other : sim.cars) {
        if (my_slot == other->d.slot) continue;
        const RigidBody* ob = GetRigidBody(sim, other->d.slot);
        if (ob->type >= 4 || ob->state == 3) continue;
        AIGroundVehicle* oai = ai_of(*other);  // RigidBody::GetOwner e GetAIGroundVehiclePtr
        const RigidBodyInfo* oi = ob->info;
        auto near = [](int32_t a, int32_t b, int32_t lim) {
            const int32_t d = a - b;
            return d < 0 ? -d < lim : d < lim;
        };
        if (!near(zx, oi->zone[0], 4) || !near(zy, oi->zone[1], 3) || !near(zz, oi->zone[2], 4)) continue;
        const float radius = ee::max(oi->half_size[0], oi->half_size[2]);
        if (!TestForCollision(*car, point, ob->pos, radius)) continue;
        if (!oai) ++g_stats.guards;
        else if (oai->controller != 4) ++g_stats.audio_resets;  // a buzina: o +0x154 do som do carro
        v.collision_ahead = 1;
        ++g_stats.blocked;
        GetCollisionStopPoint(*car, stop, ob->pos, radius);  // a cópia, sem voltar ao ponto (como o PS2)
        if (ee::le(vu_len_xz(me->vel), 1.0f)) v.stop_now = 1;
        const float d = vu_dist(me->pos, stop);
        if (!found || ee::lt(d, best)) {
            copy3(point, stop);
            best = d;
            found = true;
        }
    }
    if (v.collision_ahead == 0) v.stop_now = 0;
}

// [driving] 0x00133ab0 AIGroundVehicle::DriveToPointTraffic
void AIGroundVehicle_DriveToPointTraffic(AIGroundVehicle& v, const float point[4], float speed) {
    if (v.min_sim_update != 0) return;
    BondCar* car = car_or_guard(v);
    if (!car) return;
    const RigidBody* body = body_of(*car);
    float fwd[3], right[3], d[3], t[3], vel[3];
    copy3(fwd, body->info->orient[2]);
    copy3(right, body->info->orient[0]);
    vsub3(t, point, body->pos);
    vu_unit(d, t);
    const float side = vu_dot(d, right);
    vu_unit(vel, body->vel);
    const float moving = vu_dot(fwd, vel);
    const float ahead = vu_dot(fwd, d);
    float want = ee::lt(ahead, 0.0f) ? ee::mul(speed, ee::neg(ahead)) : ee::mul(speed, ahead);
    want = ee::max(0.5f, want);
    const float speed_xz = vu_len_xz(body->vel);
    const float near = ee::max(5.0f, ee::mul(speed_xz, 0.5f));
    if (ee::le(vu_dist_xz(point, body->pos), near)) want = 0.0f;
    float steer = side;
    if (ee::le(ahead, flt(0xbe4ccccdu))) steer = ee::lt(side, 0.0f) ? -1.0f : 1.0f;  // -0,2: o ponto atrás
    // Util_Bound(steer, 1, 1)
    if (ee::lt(1.0f, steer)) steer = 1.0f;
    else if (ee::lt(steer, ee::neg(1.0f))) steer = ee::neg(1.0f);
    set_steering(*car, steer);
    set_gas(*car, 0.0f);
    set_brake(*car, 0.0f);
    set_hand_brake(*car, 0);
    if (v.stop_now != 0) {
        set_gas(*car, 0.0f);
        set_brake(*car, 0.0f);
        set_hand_brake(*car, 1);
        return;
    }
    const float diff = ee::min(7.5f, ee::abs(ee::sub(want, speed_xz)));
    const float amount = ee::max(flt(0x3d4ccccdu), ee::div(diff, 7.5f));  // 0,05
    if (ee::lt(speed_xz, want)) {
        set_gas(*car, amount);
        return;
    }
    if (!ee::lt(0.0f, moving)) {
        set_gas(*car, amount);
        return;
    }
    if (ee::lt(1.0f, diff) || ee::lt(want, 2.5f)) set_brake(*car, amount);
}

// [driving] 0x0013e688 AIVehicleController::Init (o tráfego e os do jogador; os agentes em ground_vehicle_agent.cpp,
// AIVehicleController_InitAgents; os helicópteros, alocados pelas contagens da missão, ficam de fora).
void AIVehicleController_Init() {
    if (g_ctl.instance == 0) g_ctl.instance = 1;  // __builtin_new(1)
    g_ctl.light_cycle = 0;
    g_ctl.light_next = simulation().steps + 300;
    for (int i = 0; i < kPlayerGroundVehicles; ++i) {
        if (!g_ctl.player[i]) {
            g_ctl.player[i] = &g_player_storage[i];
            AIGroundVehicle_Construct(*g_ctl.player[i]);  // o construtor estático do PS2
        }
        g_ctl.player[i]->controller_idx = i;
    }
    for (int i = 0; i < kTrafficVehicles; ++i) {
        if (!g_ctl.traffic[i]) {
            g_ctl.traffic[i] = &g_traffic_storage[i];
            AIGroundVehicle_Construct(*g_ctl.traffic[i]);
        }
    }
    AIVehicleController_InitAgents();  // os agentes: AIElementController::fObj +4 (ground_vehicle_agent.cpp)
}

// [driving] 0x0013e840 AIVehicleController::Restart (os agentes em ground_vehicle_agent.cpp)
void AIVehicleController_Restart() {
    g_ctl.light_cycle = 0;
    g_ctl.light_next = simulation().steps + 300;
    for (AIGroundVehicle* v : g_ctl.player) {
        if (!v) continue;
        AIGroundVehicle_Reset(*v);  // a entrada +0x14 da tabela virtual
        v->active = 0;
    }
    for (AIGroundVehicle* v : g_ctl.traffic) {
        if (!v) continue;
        AIGroundVehicle_Reset(*v);
        v->active = 0;
    }
    AIVehicleController_RestartAgents();
}

// [driving] 0x0013ee30 AIVehicleController::UpdateTrafficVehicles
void AIVehicleController_UpdateTrafficVehicles() {
    const int32_t steps = simulation().steps;
    for (int i = 0; i < kTrafficVehicles; ++i) {
        if ((steps & 7) != (i & 7)) continue;
        AIGroundVehicle* v = g_ctl.traffic[i];
        if (!v) continue;
        v->update_state = 0;
        AIGroundVehicle_Update(*v);  // a entrada +0x1c da tabela virtual
    }
}

// [driving] 0x0013ef40 AIVehicleController::UpdateVehicles
void AIVehicleController_UpdateVehicles() {
    const int32_t steps = simulation().steps;
    if (g_ctl.light_next < steps) {
        g_ctl.light_next = steps + 300;
        g_ctl.light_cycle += 1;
    }
    // [driving] 0x0013ece0 AIVehicleController::UpdatePlayerGroundVehicles: Update de cada um (o estado 1, DoPlayerLogic,
    // e o 3, o trilho, estão em driving/game/rail.h).
    for (AIGroundVehicle* v : g_ctl.player)
        if (v) AIGroundVehicle_Update(*v);
    if (!g_ctl.agents.empty()) AIVehicleController_UpdateAgentGroundVehicles();
    AIVehicleController_UpdateTrafficVehicles();
}

// [driving] 0x0013efc8 AIVehicleController::IntersectionGreenLight
// Divergência: sem RPathEngine no port, o laço dos trilhos (um objeto num trilho a menos de 50 m e de 5 m de altura
// deixa o sinal vermelho) não acha nenhum.
bool AIVehicleController_IntersectionGreenLight(const float pos[3], int index, int count) {
    (void)pos;
    const int32_t steps = simulation().steps;
    if (g_ctl.light_next - 150 < steps) return false;  // a troca: todos vermelhos nos últimos 150 passos
    if (count == 0) {
        ++g_stats.guards;  // divu por zero (break 7 no PS2)
        return false;
    }
    return uint32_t(g_ctl.light_cycle) % uint32_t(count) == uint32_t(index);
}

// [driving] 0x0013f420 AIVehicleController::ApproveAdditionalTrafficVehicleSpawn
AIGroundVehicle* AIVehicleController_ApproveAdditionalTrafficVehicleSpawn() {
    for (AIGroundVehicle* v : g_ctl.traffic)
        if (v && v->active == 0) return v;
    return nullptr;
}

namespace {
// [driving] 0x0013f328 AIVehicleController::ApproveAdditionalPlayerGroundVehicleSpawn
AIGroundVehicle* approve_player() {
    for (AIGroundVehicle* v : g_ctl.player)
        if (v && v->active == 0) return v;
    return nullptr;
}

void attach(AIGroundVehicle& v, BondCar& car) { AIVehicle_Attach(v, car); }

// [driving] 0x0013f618 AIVehicleController::RegisterPlayerGroundVehicle
void register_player(BondCar& car) {
    AIGroundVehicle* v = approve_player();
    if (!v) {
        ++g_stats.guards;
        return;
    }
    attach(*v, car);
    v->valid_command = 1;
    v->vehicle_type = 1;
    v->controller = 1;
    v->active = 1;
    AIGroundVehicle_SetAttackMode(*v, 1);
    AIGroundVehicle_SetNavigateMode(*v, 1, 0);
    v->lane_weight_width = 3;
    v->primary_target = 0;
    nav_of(*v)->mode = 0;
    v->is_sub = uint8_t(car.physics()->is_sub);  // lb +0xbc do CarPhysics
}
}  // namespace

// AIVehicle::SetPhysicsObject (0x0013dfd0) e PBondCar::SetAIGroundVehicle (+0x54).
void AIVehicle_Attach(AIGroundVehicle& v, BondCar& car) {
    v.physics = nf::p32_u(&car.d);
    v.car = &car;
    car.d.ai_ground_vehicle = nf::p32_u(&v);
}

// [driving] 0x0013f868 AIVehicleController::RegisterTrafficVehicle
void AIVehicleController_RegisterTrafficVehicle(BondCar& car) {
    AIGroundVehicle* v = AIVehicleController_ApproveAdditionalTrafficVehicleSpawn();
    if (!v) {
        ++g_stats.guards;  // o PS2 escreveria no endereço 4
        return;
    }
    attach(*v, car);
    if (road_network().valid == 0) return;
    v->controller = 4;
    v->valid_command = 1;
    v->vehicle_type = 1;
    v->active = 1;
    v->primary_target = 0;
    // GetCarType (+0x84) contra "bus", "truck", "tnkr" e "flatb": os grandes ocupam a faixa com peso 4.
    const std::string& type = car.name_;
    const bool big = type == "bus" || type == "truck" || type == "tnkr" || type == "flatb";
    v->lane_weight_width = big ? 4 : 3;
    AIGroundVehicle_UnSpawn(*v);
}

// [driving] 0x0013f118 AIVehicleController::RegisterSimCar
void AIVehicleController_RegisterSimCar(BondCar& car) {
    switch (car.d.car_class) {  // GetCarClass (+0x9c)
        case 0: register_player(car); break;
        case 2: AIVehicleController_RegisterAgentGroundVehicle(car); break;
        case 3: AIVehicleController_RegisterTrafficVehicle(car); break;
        default: break;
    }
}

// [driving] 0x0013f1c8 AIVehicleController::RegisterSimCarList
void AIVehicleController_RegisterSimCarList() {
    for (BondCar* car : simulation().cars) AIVehicleController_RegisterSimCar(*car);
}

// [driving] 0x0013fae0 AIVehicleController::CreateSimTrafficCars
int AIVehicleController_CreateSimTrafficCars(int num_types, int max_traffic,
                                            const std::function<uint32_t(const char*)>& colours,
                                            const std::function<void(const char*, uint32_t)>& spawn) {
    int types = num_types;
    if (num_types < 0) types = max_traffic < 8 ? max_traffic : 7;  // sem NUM_TRAFFIC_TYPES_PS2
    if (g_ctl.types_chosen == 0) {
        for (int i = 0; i < types; ++i) {
            if (types < 7) g_ctl.traffic_types[i] = int16_t(int32_t(SimRandom_Generate(sim_random()) * 7u) >> 16);
            else g_ctl.traffic_types[i] = int16_t(i);
        }
        g_ctl.types_chosen = 1;
    }
    // (o PreLoad dos modelos, com o segundo argumento, fica com quem monta os carros)
    if (road_network().valid == 0) return 0;
    int n = 0;
    for (int i = 0; i < max_traffic; ++i) {
        if (types == 0) {
            ++g_stats.guards;  // div por zero (break 7)
            break;
        }
        const char* type = kTrafficTypes[g_ctl.traffic_types[i % types]];
        uint32_t count = colours ? colours(type) : 0;  // PVehicle::NumColoursAttrib
        if (count < 1) count = 1;
        const uint32_t colour = uint32_t(int32_t(count * SimRandom_Generate(sim_random())) >> 16);
        // Simulation::SpawnCarObject(3, tipo, cor, (0,0,0), (0,0,0)), na hora (o construtor do PBondCar também sorteia).
        if (spawn) spawn(type, colour);
        ++n;
    }
    return n;
}

}  // namespace drv
