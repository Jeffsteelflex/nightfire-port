// Onde o tráfego nasce (driving/ai/road_spawn.h), portado do MIPS do DRIVING.ELF (AIElementController.obj). As
// desmontagens ficam em out/agent_drv_traffic/ do checkout principal (fora do git).
//
// Divergências (port), cada uma também na função:
//  - GetTrafficSpawnPointOnSegment: um trecho sem faixas (esquerda + direita = 0) para o PS2 (break 7 na divisão); aqui
//    conta em road_spawn_guards() e fica na faixa 0;
//  - RefreshSpawnData: a lista de células da grade é um std::vector do port (o PS2 a aloca com FastAlloc e a solta no
//    fim); a ordem das células é a de WGrid::FindNodes.

#include "driving/ai/road_spawn.h"

#include <cstring>
#include <vector>

#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_nav.h"
#include "driving/ai/road_network.h"
#include "driving/ai/sim_random.h"
#include "driving/game/bond_car.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

RoadSpawnGlobals g_spawn;
TrafficWorld g_world;
uint64_t g_guards = 0;
WRoadNav* g_nav = nullptr;  // 0x3c8560 (construído no primeiro uso, como o static local do PS2)
alignas(16) uint8_t g_nav_storage[sizeof(WRoadNav)];
constexpr uint32_t kOne = 0x3f800000u;

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// vsub.xyz; vmul.xyz; vmulax.x; vmaddz.x (x² + z²); vsqrt; vaddq.x
float vu_dist_xz(const float a[3], const float b[3]) {
    const uint32_t dx = ps2::sub(bits(a[0]), bits(b[0]));
    const uint32_t dz = ps2::sub(bits(a[2]), bits(b[2]));
    const uint32_t acc = ps2::mul(dx, dx);
    return flt(ps2::add(0, ps2::sqrt(ps2::madd(acc, kOne, ps2::mul(dz, dz)))));
}

// O corpo do primeiro carro da Simulation (o do jogador).
const RigidBody* player_body() {
    Simulation& sim = simulation();
    if (sim.cars.empty() || !sim.cars[0]) return nullptr;
    return GetRigidBody(sim, sim.cars[0]->d.slot);
}

int iabs(int v) { return v < 0 ? -v : v; }

}  // namespace

RoadSpawnGlobals& road_spawn() { return g_spawn; }
TrafficWorld& traffic_world() { return g_world; }
uint64_t road_spawn_guards() { return g_guards; }

WRoadNav& road_spawn_nav() {
    if (!g_nav) {
        g_nav = new (g_nav_storage) WRoadNav{};
        WRoadNav_Construct(*g_nav);
    }
    return *g_nav;
}
bool road_spawn_nav_built() { return g_nav != nullptr; }

namespace {
void reset_lists() {
    g_spawn.refresh_step = simulation().steps;  // 0x3c8278 = Simulation +0xdc4
    g_spawn.flag60 = 1;
    for (int i = 0; i < 50; ++i) g_spawn.segments[i] = -1;
}
}  // namespace

// [driving] 0x0013c4d8 AIRoadSpawn::Init
void AIRoadSpawn_Init() {
    if (g_spawn.instance != 0) return;
    g_spawn.instance = 1;  // __builtin_new(1)
    reset_lists();
}

// [driving] 0x0013c558 AIRoadSpawn::Restart
void AIRoadSpawn_Restart() {
    if (g_spawn.instance == 0) return;
    reset_lists();
}

// [driving] 0x0013c5b0 AIRoadSpawn::Shutdown
void AIRoadSpawn_Shutdown() { g_spawn.instance = 0; }

// [driving] 0x0013c5e8 AIRoadSpawn::GetTrafficSpawnPointOnSegment
// Divergência: um trecho sem faixas para o PS2 (break 7 na divisão por zero); aqui conta como guarda e a faixa é 0.
void AIRoadSpawn_GetTrafficSpawnPointOnSegment(const int16_t* segment, int8_t* lane, float* t, int32_t* forwards) {
    const WRoadSegment* s = road_segment(*segment);
    // cvt.s.w do sorteio vezes 2^-16 (0x37800000)
    *t = ee::mul(ee::cvt_s(int32_t(SimRandom_Generate(sim_random()))), flt(0x37800000u));
    const int32_t lanes = int32_t(s->left_lanes) + int32_t(s->right_lanes);
    int32_t v = g_spawn.spawn_count;
    if (s->road != -1) v += int32_t(road_road(s->road)->count2e);
    int32_t pick = 0;
    if (lanes == 0) ++g_guards;
    else pick = v % lanes;  // div + mfhi: o resto com o sinal do dividendo, como em C++
    const uint8_t right = uint8_t(s->right_lanes);
    // A faixa da direita contada de fora para dentro, depois as da esquerda (sb: os 8 bits de baixo).
    if (pick < int32_t(s->right_lanes)) *lane = int8_t(uint8_t(right - pick));
    else *lane = int8_t(uint8_t(right - (pick + 1)));
    const uint32_t directions = s->unknown58;
    *forwards = 1;
    const uint32_t bit = (directions >> (uint32_t(pick) & 31)) & 1u;  // srav: o deslocamento pelos 5 bits de baixo
    const bool reverse = road_network().right_side_system != 0 ? bit == 0 : bit != 0;
    if (reverse) {
        *lane = int8_t(uint8_t(0u - uint8_t(*lane)));
        *forwards = 0;
    }
}

// [driving] 0x0013c798 AIRoadSpawn::TrafficRespawnPosCheck
bool AIRoadSpawn_TrafficRespawnPosCheck(const float pos[3], float radius) {
    if (g_spawn.forced != 0) return true;
    const RigidBody* p = player_body();
    if (!p) {
        ++g_guards;
        return true;
    }
    const float d = vu_dist_xz(p->pos, pos);
    return !ee::le(ee::sub(d, radius), g_spawn.max_dist);
}

// [driving] 0x0013c860 AIRoadSpawn::TrafficRespawnAvailable
// (O raio que os chamadores passam em f12 não é lido.)
bool AIRoadSpawn_TrafficRespawnAvailable(const float pos[3]) {
    if (g_spawn.forced != 0) return true;
    if (g_world.mission_no_traffic != 0) return false;
    const RigidBody* p = player_body();
    if (!p) {
        ++g_guards;
        return false;
    }
    const float d = vu_dist_xz(p->pos, pos);
    if (ee::le(d, g_spawn.min_dist)) return false;
    if (g_world.mission_camera_check != 0 && ee::lt(g_spawn.min_dist, d)) {
        // Atrás da câmera: o produto da direção (unitária, xyz) da câmera ao ponto com a frente dela abaixo de 0,65.
        if (!g_world.camera) {
            ++g_guards;
            return false;
        }
        const float* cam_pos = g_world.camera[3];
        const float* cam_fwd = g_world.camera[2];
        uint32_t dv[3];
        for (int i = 0; i < 3; ++i) dv[i] = ps2::sub(bits(pos[i]), bits(cam_pos[i]));
        uint32_t acc = ps2::mul(dv[0], dv[0]);
        acc = ps2::madd(acc, kOne, ps2::mul(dv[1], dv[1]));
        acc = ps2::madd(acc, kOne, ps2::mul(dv[2], dv[2]));
        const uint32_t q = ps2::rsqrt(kOne, acc);
        float u[3];
        for (int i = 0; i < 3; ++i) u[i] = flt(ps2::mul(dv[i], q));
        const float x = ee::mul(u[0], cam_fwd[0]), y = ee::mul(u[1], cam_fwd[1]), z = ee::mul(u[2], cam_fwd[2]);
        const float dot = flt(ps2::add(ps2::add(bits(x), bits(y)), bits(z)));  // vaddy.x; vaddz.x
        return ee::lt(dot, flt(0x3f266666u));  // 0,65
    }
    return ee::lt(g_spawn.max_dist, d);
}

// [driving] 0x0013ca60 AIRoadSpawn::GetRandomTrafficSpawn
bool AIRoadSpawn_GetRandomTrafficSpawn(int16_t* segment, int8_t* lane, float* t, int32_t* forwards) {
    AIRoadSpawn_RefreshSpawnData(false, 75.0f, 150.0f);
    if (g_world.mission_no_traffic != 0) return false;
    const int32_t n = g_spawn.num_segments;
    if (n <= 0) return false;
    // mult (os 32 bits de baixo) e sra 16
    const int32_t idx = int32_t(uint32_t(n) * SimRandom_Generate(sim_random())) >> 16;
    const uint16_t seg = uint16_t(g_spawn.segments[idx]);  // lhu: a metade de baixo da palavra
    *segment = int16_t(seg);
    if (int16_t(seg) < 0) return false;
    const WRoadSegment* s = road_segment(*segment);
    if (s->road == -1) return false;
    const WRoad* r = road_road(s->road);
    if (!(r->count2e < r->unknown2c)) return false;
    AIRoadSpawn_GetTrafficSpawnPointOnSegment(segment, lane, t, forwards);
    WRoadNav& nav = road_spawn_nav();
    nav.mode = 1;
    WRoadNav_InitAtSegment(nav, *segment, *lane, *t, *forwards != 0);
    float pos[3];
    std::memcpy(pos, nav.road_point, 12);
    // A zona de 5 m do ponto: arredondado para o inteiro mais perto (± 0,5 e cvt.w.s, que trunca).
    const float k = flt(0x3e4ccccdu);  // 0,2
    float fx = ee::mul(pos[0], k);
    const int32_t zx = ee::cvt_w(ee::add(ee::lt(fx, 0.0f) ? -0.5f : 0.5f, fx));
    float fz = ee::mul(pos[2], k);
    const int32_t zz = ee::cvt_w(ee::add(ee::lt(fz, 0.0f) ? -0.5f : 0.5f, fz));
    Simulation& sim = simulation();
    for (BondCar* car : sim.cars) {
        AIGroundVehicle* v = ai_of(*car);
        const RigidBody* b = GetRigidBody(sim, car->d.slot);
        const int32_t cz = b->info->zone[2], cx = b->info->zone[0];
        if (!v) {
            ++g_guards;  // o PS2 leria o endereço 0x58
            continue;
        }
        if (v->active == 0) continue;
        int limit = 9;
        if (v->controller == 4) {
            // Com vida (>= 1) e na mesma faixa e sentido o PS2 vai a outro trecho do código, que faz a mesma conta.
            limit = 5;
        }
        if (iabs(zx - cx) < limit && iabs(zz - cz) < limit) return false;
    }
    const RigidBody* p = player_body();
    if (!p) {
        ++g_guards;
        return false;
    }
    const float d = vu_dist_xz(p->pos, pos);
    if (!ee::le(g_spawn.min_dist, d)) return false;
    g_spawn.spawn_count += 1;
    return true;
}

// [driving] 0x0013d618 AIRoadSpawn::RefreshSpawnData
// Divergência: a lista das células é um vetor do port (no PS2 alocada com FastAlloc e solta com FastFree).
void AIRoadSpawn_RefreshSpawnData(bool force, float min_dist, float max_dist) {
    Simulation& sim = simulation();
    if (!force && sim.steps < g_spawn.refresh_step) return;
    const RigidBody* p = player_body();
    float pos[3] = {0, 0, 0};
    if (p) std::memcpy(pos, p->pos, 12);
    else ++g_guards;
    if (force) {
        if (g_world.camera) std::memcpy(pos, g_world.camera[3], 12);
        else ++g_guards;
        g_spawn.refresh_step = sim.steps + 1;
        g_spawn.forced = 1;
    } else {
        g_spawn.forced = 0;
        g_spawn.refresh_step = sim.steps + 0x59;
    }
    RoadNetworkGlobals& rn = road_network();
    uint64_t stamp = uint64_t(rn.segment_stamp[0]) | (uint64_t(rn.segment_stamp[1]) << 32);
    stamp += 1;  // daddiu: 64 bits
    rn.segment_stamp[0] = uint32_t(stamp);
    rn.segment_stamp[1] = uint32_t(stamp >> 32);
    if (rn.valid_traffic_roads == 0) return;
    g_spawn.max_dist = max_dist;
    g_spawn.min_dist = min_dist;
    TrackWorld* tw = world() ? world()->track_world() : nullptr;
    if (!tw) {
        ++g_guards;
        return;
    }
    std::vector<uint32_t> cells;
    WGrid_FindNodes(tw->grid(), pos, max_dist, cells);
    int32_t n = 0;
    for (uint32_t cell : cells) {
        const WGridNode* node = tw->node(cell);
        if (!node) continue;
        const uint32_t count = node->static_count[3];
        if (count == 0) continue;
        const int16_t* list =
            reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(node) + 0x10 + node->static_offset[3]);
        for (uint32_t i = 0; i < count; ++i) {
            const int16_t idx = list[i];
            if (!(uint32_t(int32_t(idx)) < uint32_t(rn.num_segments))) continue;  // sltu: sem sinal
            if (n >= 50) break;  // o PS2 passa à próxima célula (e cada uma acaba aqui de novo)
            WRoadSegment* s = road_segment(idx);
            const uint64_t seen = uint64_t(s->stamp[0]) | (uint64_t(s->stamp[1]) << 32);
            if (seen == stamp) continue;
            s->stamp[0] = uint32_t(stamp);
            s->stamp[1] = uint32_t(stamp >> 32);
            const uint16_t f = s->flags;
            if (f & 0x0002) continue;
            if (f & 0x0001) continue;
            if (f & 0x0008) continue;
            if (f & 0x8000) continue;
            if (f & 0x0080) continue;
            if ((f & 0x1020) == 0x1020) continue;
            if ((f & 0x0100) != 0 && s->unknown59 != 0) continue;
            g_spawn.segments[n++] = int32_t(idx);  // sw a3: o índice com o sinal estendido do lh
        }
    }
    for (int32_t i = n; i < 50; ++i) g_spawn.segments[i] = -1;
    g_spawn.num_segments = n;
}

}  // namespace drv
