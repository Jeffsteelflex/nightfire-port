// Os motoristas dos elementos da IA (o estado 5 do AIGroundVehicle, "agente"; driving/ai/ground_vehicle.h, docs/driving.md
// seção 21), portados do MIPS do DRIVING.ELF (AIElementController.obj). As desmontagens ficam em out/agent_drv_ai/ do
// checkout principal (fora do git).
//
// Um agente é o motorista de um veículo que o AIElementController acordou (classe 2): os comandos 'AICo' (EAICommand)
// lhe dão um alvo (o jogador, um ponto, outro elemento), um modo de navegação (1 parado, 2 direto ao alvo, 3 pela rede
// rumo ao alvo, 4 pela rede ao acaso, 5 por um trilho 'AISp') e um modo de ataque (13 perseguir, 14 fugir na frente, 15
// seguir a rede à velocidade da rua). A cada passo (Update: UpdateStuck, DoAgentStateUpdate, DoAttackMode,
// DoWeaponUpdate, DoMoveMode) ele decide para onde ir, a que velocidade, desvia dos outros carros e dirige
// (DriveToPoint).
//
// Divergências (cada uma também na função):
//  - DrivableVec e BarriersInPath testam o mundo com WCollisionMgr::CheckHitWorld e comparam o resultado (0 ou 1) com
//    "< 2", sempre verdadeiro no PS2: o teste não muda nada (um engano do original, mantido). O port não chama o
//    CheckHitWorld (não portado); o que ele mudaria são os carimbos das consultas do WCollisionMgr, que não mudam os
//    resultados de outras consultas;
//  - o AIZoneController (as zonas de fumaça e de óleo que as armas do jogador criam) está aqui inteiro, mas nada no port
//    registra zonas ainda (as armas são de outro módulo);
//  - guardas (contadas em ground_vehicle_stats().guards): o alvo nulo onde o PS2 leria o endereço 4 ou 0x46.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

#include "driving/ai/ai_vu.h"
#include "driving/ai/ai_weapons.h"
#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_nav.h"
#include "driving/ai/road_network.h"
#include "driving/ai/sim_random.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/game/rail.h"
#include "driving/physics/collider.h"
#include "driving/physics/collision.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/render/anim_engine.h"
#include "platform/fmath.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace aivu;

namespace {

Simulation& sim() { return simulation(); }
GroundVehicleStats& stats() { return ground_vehicle_stats(); }

RigidBody* body_of_car(BondCar& c) { return GetRigidBody(sim(), c.d.slot); }

// AIVehicle::GetPhysicsObject (0x13dfc8) e o corpo dele; nulo: guarda.
BondCar* car_g(AIGroundVehicle& v) {
    BondCar* c = car_of(v);
    if (!c) ++stats().guards;
    return c;
}
RigidBody* body_g(AIGroundVehicle* v) {
    if (!v) {
        ++stats().guards;
        return nullptr;
    }
    BondCar* c = car_g(*v);
    return c ? body_of_car(*c) : nullptr;
}
AIGroundVehicle* target_of(const AIGroundVehicle& v) { return nf::p32_ptr<AIGroundVehicle>(v.primary_target); }

int32_t i64_get(const uint8_t* p) {
    int32_t lo;
    std::memcpy(&lo, p, 4);
    return lo;
}
// O carimbo de 64 bits (sd) com o passo (o sinal estendido).
void i64_set(uint8_t* p, int32_t v) {
    const int64_t w = v;
    std::memcpy(p, &w, 8);
}

float glue_factor() {
    // [driving] 0x00208468 SMissionManager::GetEnemyGlueFactor: as configurações +0x2b8 (0 e as outras), +0x2bc (1),
    // +0x2c0 (2).
    if (!mission_exists()) return 1.0f;
    const SMissionManagerData& m = mission_manager();
    const MissionSettings* s = nf::p32_ptr<const MissionSettings>(m.settings);
    if (!s) {
        ++stats().guards;
        return 1.0f;
    }
    if (m.difficulty == 1) return s->scale_a[1];
    if (m.difficulty < 2) return s->scale_a[0];
    if (m.difficulty == 2) return s->scale_a[2];
    return s->scale_a[0];
}

}  // namespace

// ------------------------------------------------------------------------------------------------- o AIZoneController

namespace {
struct AIZone {
    int32_t active;   // +0x00
    float pos[3];     // +0x04
    float radius;     // +0x10
    int16_t timer;    // +0x14
    int16_t pad;
    int32_t type;     // +0x18 (lido como short no índice das tabelas)
};
static_assert(sizeof(AIZone) == 0x1c);
struct ZoneGlobals {
    uint32_t instance = 0;     // fgController
    int32_t active = 0;        // fActiveZones
    AIZone zones[30] = {};     // fZone (0x32b768)
    int16_t duration[8] = {};  // 0x4230d8
    int16_t limit[8] = {};     // 0x4230e8
    float radius[8] = {};      // 0x4230f8 (o quadrado do raio)
};
ZoneGlobals g_zones;
}  // namespace

uint8_t* AIZoneController_Zones() { return reinterpret_cast<uint8_t*>(g_zones.zones); }
int32_t& AIZoneController_Active() { return g_zones.active; }

// [driving] 0x00140110 AIZoneController::Init
void AIZoneController_Init() {
    if (g_zones.instance == 0) g_zones.instance = 1;
    g_zones.duration[4] = 0xaf;
    g_zones.limit[4] = 0x55;
    g_zones.radius[4] = 12.25f;
    g_zones.radius[2] = 625.0f;
    g_zones.limit[3] = 0xf;
    g_zones.radius[3] = 1225.0f;
    g_zones.duration[1] = 0;
    g_zones.limit[1] = 0;
    g_zones.radius[1] = 0.0f;
    g_zones.duration[2] = 0xf;
    g_zones.limit[2] = 0xf;
    g_zones.duration[3] = 0xf;
    for (AIZone& z : g_zones.zones) z.active = 0;
}

// [driving] 0x00140218 AIZoneController::InZone: o quadrado da distância menos `extra` (pelo menos 0) contra o raio.
bool AIZoneController_InZone(float extra, int zone, const float point[3]) {
    const AIZone& z = g_zones.zones[zone];
    if (z.active == 0) return false;
    float d[3];
    sub3(d, z.pos, point);
    const float d2 = flt(len_sq(d));
    const float f = ee::max(0.0f, ee::sub(d2, extra));
    return ee::le(f, g_zones.radius[uint16_t(z.type) & 7]);
}

// [driving] 0x001402f8 AIZoneController::RegisterZone(int, COORD3 &): uma zona do tipo no ponto (sem outra igual que
// já o cubra), se o tipo tem limite.
void AIZoneController_RegisterZone(int32_t type, const float point[3]) {
    if (type < 0 || type >= 8 || !(0 < g_zones.limit[type])) return;
    int32_t remaining = g_zones.active;
    AIZone* free_zone = nullptr;
    for (int i = 0; i < 30; ++i) {
        AIZone& z = g_zones.zones[i];
        if (!free_zone && z.active == 0) free_zone = &z;
        if (0 < remaining && z.active != 0) {
            --remaining;
            if (type == z.type && g_zones.limit[type] <= z.timer && AIZoneController_InZone(g_zones.radius[type], i, point))
                return;
        }
    }
    if (!free_zone) return;
    free_zone->type = type;
    free_zone->active = 1;
    std::memcpy(free_zone->pos, point, 12);
    free_zone->radius = g_zones.radius[type];
    free_zone->timer = g_zones.duration[type];
    g_zones.active += 1;
}

// ------------------------------------------------------------------------------------------------- o controlador

namespace {
std::vector<AIGroundVehicle*>& agents() { return vehicle_controller().agents; }
}  // namespace

// A parte dos agentes do AIVehicleController::Init (0x0013e688, a marca em ground_vehicle.cpp): AIElementController::fObj
// +4 motoristas.
void AIVehicleController_InitAgents() {
    for (AIGroundVehicle* v : agents()) delete v;
    agents().clear();
    const AIElementControllerData* c = ai_controller();
    const int n = c ? c->max_vehicles : 0;
    for (int i = 0; i < n; ++i) {
        AIGroundVehicle* v = new AIGroundVehicle();
        AIGroundVehicle_Construct(*v);
        v->controller_idx = i;
        agents().push_back(v);
    }
    vehicle_controller().agent_cull_index = 0;
}

// A parte dos agentes do AIVehicleController::Restart (0x0013e840, a marca em ground_vehicle.cpp).
void AIVehicleController_RestartAgents() {
    for (AIGroundVehicle* v : agents()) {
        AIGroundVehicle_Reset(*v);
        v->active = 0;
    }
}

// [driving] 0x0013ed38 AIVehicleController::UpdateAgentGroundVehicles: um em cada N faz a atualização inteira.
void AIVehicleController_UpdateAgentGroundVehicles() {
    const int n = int(agents().size());
    if (n == 0) {
        ++stats().guards;  // o PS2 divide por zero (break 7)
        return;
    }
    int32_t& cull = vehicle_controller().agent_cull_index;
    cull = (cull + 1) % n;
    for (int i = 0; i < n; ++i) {
        AIGroundVehicle* v = agents()[size_t(i)];
        v->update_state = cull == i ? 0 : 1;
        AIGroundVehicle_Update(*v);  // a entrada +0x1c da tabela virtual
    }
}

// [driving] 0x0013f390 AIVehicleController::ApproveAdditionalAgentGroundVehicleSpawn
AIGroundVehicle* AIVehicleController_ApproveAdditionalAgentGroundVehicleSpawn() {
    for (AIGroundVehicle* v : agents())
        if (v->active == 0) return v;
    return nullptr;
}

// [driving] 0x0013f748 AIVehicleController::RegisterAgentGroundVehicle
void AIVehicleController_RegisterAgentGroundVehicle(BondCar& car) {
    AIGroundVehicle* v = AIVehicleController_ApproveAdditionalAgentGroundVehicleSpawn();
    if (!v) {
        ++stats().guards;  // o PS2 escreveria a partir do endereço 0
        return;
    }
    AIVehicle_Attach(*v, car);  // SetPhysicsObject e o PBondCar::SetAIGroundVehicle (+0x54)
    v->valid_command = 0;
    v->active = 1;
    v->vehicle_type = 1;
    v->lane_weight_width = 3;
    v->controller = 5;
    v->has_turret = car.attr_bool("HAS_TURRET") ? 1 : 0;
    v->is_sub = uint8_t(car.physics()->is_sub);  // GetPhysics (+0x1d4) +0xbc
}

// [driving] 0x0013f7f0 AIVehicleController::DeRegisterAgentGroundVehicle
void AIVehicleController_DeRegisterAgentGroundVehicle(BondCar& car) {
    AIGroundVehicle* v = ai_of(car);
    if (!v) {
        ++stats().guards;
        return;
    }
    v->active = 0;
    v->valid_command = 0;
    AIGroundVehicle_Reset(*v);  // a entrada +0x14 da tabela virtual
}

// [driving] 0x0013f238 AIVehicleController::DeRegisterSimCar (os jogadores e o tráfego: 0x0013f6e0 e 0x0013f9b0, que
// fazem o mesmo nos seus vetores).
void AIVehicleController_DeRegisterSimCar(BondCar& car) {
    const int32_t c = car.d.car_class;
    if (c == 2 || c == 0 || c == 3) AIVehicleController_DeRegisterAgentGroundVehicle(car);
}

// [driving] 0x0013ebc0 AIVehicleController::GetPlayerGroundVehiclePtr
AIGroundVehicle* AIVehicleController_GetPlayerGroundVehicle(int i) {
    if (i < 0 || i >= kPlayerGroundVehicles) return nullptr;
    return vehicle_controller().player[i];
}

// ------------------------------------------------------------------------------------------------- os alvos

// [driving] 0x0012e670 AIGroundVehicle::SetTargetMode(char, COORD3 &): 0 o jogador, 1 um ponto.
void AIGroundVehicle_SetTargetMode(AIGroundVehicle& v, int32_t mode, const float point[3]) {
    if (mode == 0) {
        v.primary_target = nf::p32_u(AIVehicleController_GetPlayerGroundVehicle(0));
    } else if (mode == 1) {
        v.primary_target = 0;
        std::memcpy(v.unknown1a8, point, 12);
    }
    v.target_mode = mode;
    v.default_target = v.primary_target;
    v.unknown208 = 0;
}

// [driving] 0x0012e770 AIGroundVehicle::SetSecondaryTarget(CARP::AIElement *): o alvo é o veículo do elemento, se
// acordado e for um carro.
void AIGroundVehicle_SetSecondaryTarget(AIGroundVehicle& v, uint32_t element) {
    if (element == 0) return;
    v.unknown204 = int32_t(element);
    AIElement* e = AIElementController_RedirectCarpElement(element);
    if (!e) return;
    AIGroundVehicle* t = nf::p32_ptr<AIGroundVehicle>(e->ai);
    if (e->state == 1 && t && t->vehicle_type == 1) v.primary_target = e->ai;
}

// ------------------------------------------------------------------------------------------------- as consultas

// [driving] 0x001351d0 AIGroundVehicle::GetCollNav(COORD3 &, bool, char): o navegador da rede no ponto onde o carro
// estará em 0,25 s (o +0xf0, modo 3; com `alt` o +0x100, modo 4 com a faixa `lane`), refeito a cada 15 passos, virado
// para `dir`. A faixa guardada (+0x04, lbu) é comparada sem sinal com a pedida (com sinal): uma faixa negativa refaz sempre.
WRoadNav* AIGroundVehicle_GetCollNav(AIGroundVehicle& v, const float dir[3], bool alt, int8_t lane) {
    WRoadNav* n = nf::p32_ptr<WRoadNav>(alt ? v.nav3 : v.nav2);
    uint8_t* stamp = alt ? v.unknown0f8 : v.unknown0e8;
    const int32_t ts = i64_get(stamp);
    bool redo = ts == 0 || !(sim().steps < ts + 15);
    if (!redo && alt && int32_t(n->unknown04) != int32_t(lane)) redo = true;
    if (redo) {
        RigidBody* b = body_g(&v);
        if (!b) return n;
        float pos[3], step[3];
        scale3(step, b->vel, 0.25f);
        add3(pos, b->pos, step);
        i64_set(stamp, sim().steps);
        n->mode = alt ? 4 : 3;
        if (alt) n->unknown04 = uint8_t(lane);
        alignas(16) float p4[4] = {pos[0], pos[1], pos[2], 0}, f4[4];
        std::memcpy(f4, b->info->orient[2], 12);
        f4[3] = 0.0f;
        WRoadNav_InitAtPoint(*n, p4, f4, false);
    }
    if (ee::lt(dot(n->direction, dir), 0.0f)) WRoadNav_ReverseNavDirection(*n);
    return n;
}

// [driving] 0x00226bd8 WRoadNav::PathShareRoadSegment(short): o trecho é da mesma rua, ou liga os mesmos cruzamentos.
bool WRoadNav_PathShareRoadSegment(const WRoadNav& n, int16_t segment) {
    const WRoadSegment* cur = road_segment(n.segment);
    const WRoadSegment* tgt = road_segment(segment);
    if (cur->road == tgt->road) return true;
    if (cur->intersection[0] == tgt->intersection[0] && cur->intersection[1] == tgt->intersection[1]) return true;
    return cur->intersection[1] == tgt->intersection[0] && cur->intersection[0] == tgt->intersection[1];
}

// [driving] 0x00226c88 WRoadNav::PathForwardRoadSegment(short, bool *): o trecho está à frente do navegador (no
// mesmo trecho de rua, mais adiante; ou um cruzamento à frente, ou um vizinho dele, liga o trecho). `out` fica 1 salvo
// quando a busca pelos cruzamentos não acha nada.
// Quirk mantido: no laço dos vizinhos do vizinho, o pulo do cruzamento de trás compara o vizinho de índice `i` (o do
// laço de fora), não o `j`.
int32_t WRoadNav_PathForwardRoadSegment(const WRoadNav& n, int16_t segment, int32_t* out) {
    if (segment < 0) return 1;
    const WRoadSegment* cur = road_segment(n.segment);
    const WRoadSegment* tgt = road_segment(segment);
    if (out) *out = 1;
    if (cur->intersection[0] == cur->intersection[1]) return 1;
    const int fwd = n.forward;  // lb (com sinal)
    if (WRoadNav_PathShareRoadSegment(n, segment)) {
        if (cur->intersection[fwd] != tgt->intersection[fwd]) return 0;
        return tgt->road_position[fwd] < cur->road_position[fwd] ? 1 : 0;
    }
    const int back = uint8_t(n.forward) != 0 ? 0 : 1;  // lbu: o cruzamento de trás
    const int16_t t0 = tgt->intersection[0], t1 = tgt->intersection[1];
    if (t0 == t1 && t0 == cur->intersection[back]) return 0;
    const int16_t prev = cur->intersection[fwd != 0 ? 0 : 1];
    if (t0 == prev) return 0;
    if (t1 == prev) return 0;
    const int16_t next = cur->intersection[fwd];
    if (next < 0) {
        if (out) *out = 0;
        return 0;
    }
    if (t0 == t1 && t1 == next) return 1;
    if (t0 == next) return 1;
    if (t1 == next) return 1;
    const WRoadIntersection* in = road_intersection(next);
    const int count = in->neighbour_count;
    for (int i = 0; i < count; ++i) {
        const int16_t nb = int16_t(in->neighbours[i]);
        if (nb == prev) continue;
        if (t0 == nb) return 1;
        if (t1 == nb) return 1;
        const WRoadIntersection* in2 = road_intersection(nb);
        const int count2 = in2->neighbour_count;
        if (count2 == 0) continue;
        const int16_t fixed = int16_t(in2->neighbours[i]);
        for (int j = 0; j < count2; ++j) {
            if (fixed == prev) continue;
            const int16_t nb2 = int16_t(in2->neighbours[j]);
            if (t0 == nb2) return 1;
            if (t1 == nb2) return 1;
        }
    }
    if (out) *out = 0;
    return 0;
}

// [driving] 0x001330d0 AIGroundVehicle::DrivableVec(COORD3 &, COORD3 &): 1 se os pontos estão a até 200 m (ver o
// cabeçalho: o CheckHitWorld do PS2 não pode mudar a resposta).
int32_t AIGroundVehicle_DrivableVec(AIGroundVehicle& v, const float a[3], const float b[3]) {
    (void)v;
    return ee::lt(200.0f, dist(a, b)) ? 0 : 1;
}

// [driving] 0x00133238 AIGroundVehicle::BarriersInPath(bool): sempre 0 (ver o cabeçalho); com o choque de tipo 6
// guardado (+0xe0), o +0xdc é limpo.
int32_t AIGroundVehicle_BarriersInPath(AIGroundVehicle& v, bool reverse) {
    (void)reverse;
    if (v.collision_info == 0) return 0;
    if (v.collision_byte != 6) return 0;
    v.collision_info = 0;
    return 0;
}

// [driving] 0x0013e0b8 AIVehicle::GetPosition: a posição do corpo na linha 3 da matriz do motorista (+0x40).
const float* AIVehicle_GetPosition(AIGroundVehicle& v) {
    if (v.physics != 0) {
        if (RigidBody* b = body_g(&v)) {
            v.rot_pos[3][0] = b->pos[0];
            v.rot_pos[3][1] = b->pos[1];
            v.rot_pos[3][2] = b->pos[2];
        }
    }
    return v.rot_pos[3];
}

// [driving] 0x00133850 AIGroundVehicle::UpdateStuck: acelerando ou freando (ou em choque) sem sair de 5 m por 5 s:
// preso (+0x268), até andar 5 m.
void AIGroundVehicle_UpdateStuck(AIGroundVehicle& v) {
    BondCar* car = car_g(v);
    if (!car) return;
    const bool pushing = ee::le(0.5f, car->d.control.gas) || ee::le(0.5f, car->d.control.brake) || car->d.in_shock > 0;
    if (!pushing) {
        v.stuck = 0;
        return;
    }
    if (v.unknown268 == 0) {
        if (v.stuck == 0) {
            aivu::copy3(v.unknown270, AIVehicle_GetPosition(v));
            v.stuck = int16_t(v.stuck + 1);
            return;
        }
        v.stuck = int16_t(v.stuck + 1);
        if (v.stuck < sim().frame_rate * 5) return;
        const float d = dist(v.unknown270, AIVehicle_GetPosition(v));
        if (ee::lt(d, 5.0f)) v.unknown268 = 1;
        else v.stuck = 0;
        return;
    }
    const float d = dist(v.unknown270, AIVehicle_GetPosition(v));
    if (ee::lt(5.0f, d)) {
        v.stuck = 0;
        v.unknown268 = 0;
    }
}

// [driving] 0x00133f60 AIGroundVehicle::DriveToPoint(COORD3 &, float): os controles do carro para chegar ao ponto à
// velocidade: a direção pelo lado do ponto (ou toda para um lado na ré), o acelerador e o freio pela diferença de
// velocidade; a ré quando o ponto fica atrás, a troca forçada de sentido (+0x25c/+0x260) quando BarriersInPath acha
// uma parede (nunca: ver o cabeçalho), o freio de mão perto do alvo que vem de frente (modo 13).
// Divergência: o submarino (+0x118, DriveToPointSub) não foi portado: conta e não dirige.
void AIGroundVehicle_DriveToPoint(AIGroundVehicle& v, float point[4], float speed) {
    if (v.is_sub != 0) {
        ground_vehicle_pending("AIGroundVehicle::DriveToPointSub");
        return;
    }
    BondCar* car = car_g(v);
    if (!car) return;
    RigidBody* b = body_of_car(*car);
    float fwd[3], right[3], to[3], velu[3];
    aivu::copy3(fwd, b->info->orient[2]);
    aivu::copy3(right, b->info->orient[0]);
    sub3(to, point, b->pos);
    unit(to, to);
    const float side = dot(to, right);        // f26
    unit(velu, b->vel);
    const float along = dot(fwd, velu);       // f23: o carro anda para a frente (> 0) ou para trás
    const float facing = dot(fwd, to);        // f20
    float cur = len_xz(b->vel);               // f22
    if (ee::lt(along, 0.0f)) cur = ee::neg(cur);
    const float diff = ee::min(15.0f, ee::abs(ee::sub(speed, cur)));  // f27
    const float amount = ee::min(1.0f, ee::max(0.05f, ee::div(diff, 15.0f)));  // f25
    if (0 < v.dir_override_timer && !(sim().frame_rate / 2 < v.dir_override_timer))
        if (ee::lt(0.7f, facing)) v.dir_override_timer = 0;
    int32_t reversing;            // s3
    bool facing_target = false;   // s4: o alvo vem de frente a menos de 10 m
    if (0 < v.dir_override_timer) {
        reversing = v.reverse_override;
        const int32_t t = v.dir_override_timer - 1;
        v.dir_override_timer = int16_t(t > -1 ? t : 0);
    } else {
        if (v.reversing == 1) {
            if (ee::lt(0.7f, facing) && ee::lt(along, 0.0f)) v.reversing = 0;
            else if (ee::le(-0.15f, facing) && ee::lt(0.0f, along)) v.reversing = 0;
        }
        if (v.reversing == 0 && ee::lt(facing, -0.15f)) v.reversing = 1;
        reversing = v.reversing;
        AIGroundVehicle* t = target_of(v);
        if (t && v.attack_mode == 13) {
            BondCar* tc = car_g(*t);
            if (tc && tc->d.car_class == 1) {
                RigidBody* tb = body_of_car(*tc);
                float tdir[3];
                if (ee::lt(1.0f, len_xz(tb->vel))) unit(tdir, tb->vel);
                else aivu::copy3(tdir, tb->info->orient[2]);
                if (ee::le(dist(b->pos, tb->pos), 10.0f) && ee::le(dot(tdir, v.direction), -0.7f)) facing_target = true;
            }
        }
        if (!facing_target && v.update_state == 0) {
            if (AIGroundVehicle_BarriersInPath(v, v.reversing != 0) != 0) {
                v.dir_override_timer = int16_t(sim().frame_rate);
                reversing = v.reversing ^ 1;
                v.reverse_override = reversing;
            }
        }
    }
    bool steer_set = false;
    if (v.unknown164 != 0 && v.navigate_mode != 4) {
        // Na fumaça: a direção atual (GetCarControl, +0x134) dobrada (0,1 se zero).
        float s = car->d.control.steering;
        if (ee::eq(s, 0.0f)) s = 0.1f;
        car->d.control.steering = Util_Bound(ee::add(s, s), 1.0f, 1.0f);
        steer_set = true;
    }
    if (!steer_set && v.steering_disabled == 0 && v.unknown15c == 0) {
        float s = 0.0f;
        car->d.control.steering = 0.0f;           // SetCarControlSteering (+0xd4)
        car->d.control.steering_vertical = 0.0f;  // SetCarControlSteeringVertical (+0xdc)
        if (reversing == 0) {
            if (ee::le(1.0f, ee::abs(cur)) && ee::le(0.0f, along)) s = side;
        } else {
            s = ee::lt(side, 0.0f) ? 1.0f : -1.0f;
        }
        car->d.control.steering = Util_Bound(s, 1.0f, 1.0f);
    }
    car->d.control.gas = 0.0f;       // +0xf4
    car->d.control.brake = 0.0f;     // +0xfc
    car->d.control.hand_brake = 0;   // +0x104
    if (ee::lt(speed, 0.05f)) {
        const float s = ee::abs(cur);
        if (ee::le(1.0f, s)) {
            if (ee::lt(along, 0.0f)) car->d.control.gas = 1.0f;
            else car->d.control.brake = 1.0f;
        } else {
            car->d.control.steering = 0.0f;
            car->d.control.hand_brake = ee::le(0.1f, s) ? 1 : 0;
        }
    } else if (reversing == 0) {
        if (ee::lt(cur, speed)) {
            car->d.control.gas = amount;
        } else if (ee::lt(2.5f, diff) || ee::lt(speed, 5.0f)) {
            if (ee::lt(0.0f, along)) car->d.control.brake = amount;
            else car->d.control.gas = amount;
        }
    } else {
        if (ee::lt(0.0f, along)) car->d.control.brake = 1.0f;
        else if (ee::le(ee::abs(cur), 7.5f)) car->d.control.brake = 0.5f;
    }
    if (facing_target) car->d.control.hand_brake = 0;
}

// [driving] 0x00131770 AIGroundVehicle::DoAgentStateUpdate: o alvo (o secundário, a fumaça, a hora de abalroar), o
// ponto-alvo (+0x1c0: o alvo adiantado pela velocidade, o ponto do comando, o caminho da rede), a direção para ele
// (+0x1e0) e a da rede (+0x210), e as trocas do modo de navegação (3 <-> 2) e do navegador quando o alvo sai da rua.
void AIGroundVehicle_DoAgentStateUpdate(AIGroundVehicle& v) {
    BondCar* car = car_g(v);
    if (!car) return;
    RigidBody* self = body_of_car(*car);
    alignas(16) float my_pos[4] = {self->pos[0], self->pos[1], self->pos[2], 0};  // sp+0x00
    alignas(16) float tpos[4] = {0, 0, 0, 0};                                      // sp+0x10
    if (v.unknown204 != 0) {
        if (AIElement* e = AIElementController_RedirectCarpElement(uint32_t(v.unknown204))) {
            if (e->state != 1) {
                v.primary_target = 0;
            } else if (AIGroundVehicle* t = nf::p32_ptr<AIGroundVehicle>(e->ai)) {
                if (t->vehicle_type == 1) v.primary_target = e->ai;
            } else {
                ++stats().guards;
            }
        }
    }
    // A fumaça (as zonas de tipo 4 a 5 m): o carro fica "na fumaça" por um segundo.
    if (v.is_sub == 0 && v.update_state == 0 && v.unknown164 == 0) {
        for (int i = 0; i < 30; ++i) {
            if (g_zones.zones[i].type != 4) continue;
            if (AIZoneController_InZone(5.0f, i, my_pos) && ee::lt(0.0f, car->hit_points())) {
                v.been_in_smoke = 1;
                v.unknown164 = int16_t(sim().frame_rate);
                if (mission_exists()) SMissionManager_ProgrammerDefinedEvent(mission_manager(), 0x11);
            }
        }
    }
    {
        const int32_t a = v.unknown164 - 1, b = v.unknown148 - 1, c = v.unknown144 - 1;
        v.unknown164 = int16_t(a > -1 ? a : 0);
        v.unknown148 = b > -1 ? b : 0;
        v.unknown144 = c > -1 ? c : 0;
    }
    if (v.is_sub == 0) {
        if (v.unknown144 == 0 && v.unknown208 == 0) {
            // A hora de abalroar: a cada 5 s, com a chance +0x14c, por 3 s (1 s contra um carro de classe 1).
            v.unknown144 = sim().frame_rate * 5;
            const float r = ee::mul(ee::cvt_s(int32_t(SimRandom_Generate(sim_random()))), 1.5258789e-05f);
            if (ee::lt(r, v.ram_freq)) {
                v.unknown148 = sim().frame_rate * 3;
                if (AIGroundVehicle* t = target_of(v)) {
                    BondCar* tc = car_g(*t);
                    if (tc && tc->d.car_class == 1) v.unknown148 = sim().frame_rate;
                }
            }
        }
        if (v.is_sub == 0 && v.unknown268 != 0 && v.dir_override_timer < 1) {
            v.reverse_override = 1;
            v.dir_override_timer = int16_t(sim().frame_rate);
        }
    }
    if (car->d.num_wheels_on_ground != 0) v.unknown15c = 0;  // GetNumWheelsOnGround (+0x18c)
    else v.unknown15c += 1;
    WRoadNav* nav = nav_of(v);
    if (v.update_state == 0 &&
        !(sim().steps < v.unknown174 + int16_t(ee::cvt_w(ee::mul(ee::cvt_s(sim().frame_rate), 0.25f))))) {
        v.unknown174 = sim().steps;
        if (AIGroundVehicle* t = target_of(v)) {
            if (RigidBody* tb = body_g(t)) {
                aivu::copy3(tpos, tb->pos);
                const int32_t r = AIGroundVehicle_DrivableVec(v, my_pos, tpos);
                v.unknown1cc = r;
                if (r != 0) v.unknown20c = 1;
            }
        } else if (v.target_mode == 1) {
            v.unknown1cc = 1;
        }
        aivu::copy3(tpos, nav->road_point);
        v.unknown21c = AIGroundVehicle_DrivableVec(v, my_pos, tpos);
    }
    int32_t follow = 0;  // s1: seguir o alvo pela rede
    v.unknown208 = v.unknown1cc == 0 ? 1 : 0;
    AIGroundVehicle* t = target_of(v);
    if (v.attack_mode == 15 && t) {
        if (RigidBody* tb = body_g(t)) aivu::copy3(v.target_pos, tb->pos);
    } else if (v.unknown208 == 0) {
        if (t && v.target_mode != 1) {
            RigidBody* tb = body_g(t);
            if (!tb) return;
            alignas(16) float p[4] = {tb->pos[0], tb->pos[1], tb->pos[2], 0};  // sp+0x20
            if (ee::le(5.0f, len_xz(tb->vel))) {
                float lead = 0.2f;                  // f20
                const float tspeed = len_xz(tb->vel);  // f21
                if (v.navigate_mode == 3) {
                    lead = ee::min(ee::max(lead, v.lead_scaler), tspeed);
                    float dir[3], step[3];
                    unit(dir, tb->vel);
                    scale3(step, dir, lead);
                    add3(p, p, step);
                    if (ee::lt(v.min_dist_to_target, ee::mul(tspeed, lead))) follow = 0;
                    else follow = ee::le(v.unknown1d0, ee::max(15.0f, v.min_dist_to_target)) ? 0 : 1;
                } else {
                    float step[3];
                    scale3(step, tb->vel, lead);
                    add3(p, p, step);
                }
            }
            aivu::copy3(v.target_pos, p);
            aivu::copy3(tpos, v.target_pos);
        } else if (v.target_mode != 1) {
            v.unknown208 = 1;
        } else {
            aivu::copy3(v.target_pos, v.unknown1a8);
            aivu::copy3(tpos, v.target_pos);
            follow = 1;
        }
    }
    v.unknown1d0 = dist(my_pos, v.target_pos);
    {
        float d[3];
        sub3(d, v.target_pos, my_pos);
        unit(v.direction, d);
        sub3(d, nav->position, my_pos);
        unit(v.unknown210, d);
    }
    if (v.unknown208 == 1) {
        if (v.navigate_mode == 2) {
            if (t) {
                aivu::copy3(v.target_pos, AIVehicle_GetPosition(*t));
            } else {
                add3(v.target_pos, my_pos, self->info->orient[2]);
            }
        } else {
            aivu::copy3(v.target_pos, nav->road_point);
        }
        follow = t ? 1 : 0;
    }
    if (uint32_t(v.attack_mode - 14) < 2) follow = 0;
    else if (v.navigate_mode != 3) follow = 0;
    if (v.update_state == 0) {
        if (follow != 0) {
            alignas(16) float fwd[4] = {self->info->orient[2][0], self->info->orient[2][1], self->info->orient[2][2], 0};
            if (t) {
                // O navegador de colisão do ALVO (0x0013206c: a0 = +0x1a4), virado para a frente do próprio carro.
                WRoadNav* cn = AIGroundVehicle_GetCollNav(*t, fwd, false, 0);
                v.unknown1fc = cn->segment;
                if (cn->segment != nav->segment) {
                    int32_t found = 0;  // sp+0x80 (o PS2 lê a palavra que o PathForwardRoadSegment escreve)
                    if (WRoadNav_PathForwardRoadSegment(*nav, cn->segment, &found) == 0 && found != 0) {
                        alignas(16) float p[4] = {self->pos[0], self->pos[1], self->pos[2], 0};
                        WRoadNav_InitAtPoint(*nav, p, nav->direction, false);
                        WRoadNav_ReverseNavDirection(*nav);
                        WRoadNav_IncNavPosition(*nav, 7.5f, nav->direction, -1);
                        v.unknown27c = 0;
                    }
                }
            } else {
                WRoadNav* n5 = nf::p32_ptr<WRoadNav>(v.nav5);
                n5->mode = nav->mode == 4 ? 4 : 3;
                WRoadNav_InitAtPoint(*n5, tpos, fwd, false);
                v.unknown1fc = n5->segment;
            }
        } else {
            v.unknown1fc = -1;
        }
    }
    if (v.unknown074 != 0) {
        if (sim().steps < v.unknown074) return;
        v.unknown074 = 0;
    }
    if (v.update_state != 0) return;
    if (road_network().valid_traffic_roads == 0) return;
    if (v.attack_mode == 13) {
        if (v.navigate_mode == 3) {
            if (ee::lt(dot(nav->direction, v.direction), 0.5f) && v.unknown208 != 1) {
                v.unknown07c = 3;
                v.navigate_mode = 2;
                v.unknown074 = sim().steps + 60;
            }
            if (v.unknown208 == 0) {
                RigidBody* tb = body_g(t);
                if (tb && ee::lt(len_xz(tb->vel), 5.0f) && ee::lt(v.unknown1d0, v.min_dist_to_target)) {
                    v.unknown07c = 3;
                    v.navigate_mode = 2;
                    v.unknown074 = sim().steps + 60;
                }
            }
        } else if (v.navigate_mode == 2) {
            alignas(16) float fwd[4] = {self->info->orient[2][0], self->info->orient[2][1], self->info->orient[2][2], 0};
            alignas(16) float p[4] = {self->pos[0], self->pos[1], self->pos[2], 0};  // sp+0x60
            const float lane = ee::abs(AIGroundVehicle_GetCollNav(v, fwd, false, 0)->lane_offset);  // f22
            WRoadNav* cn = AIGroundVehicle_GetCollNav(v, fwd, false, 0);
            const WRoadSegment* s = road_segment(cn->segment);
            const float width = cn->forward != 0 ? WRoadNetwork_GetSegmentLeftDrivableDist(s)
                                                 : WRoadNetwork_GetSegmentRightDrivableDist(s);  // f20
            const float along = dot(AIGroundVehicle_GetCollNav(v, fwd, false, 0)->direction, fwd);  // f21
            const bool on_road = ee::le(10.0f, len_xz(self->vel)) && ee::lt(lane, width) && ee::lt(0.7f, along);
            if (on_road || v.unknown208 == 1) {
                nav->mode = 3;
                WRoadNav_InitAtPoint(*nav, p, fwd, false);
                WRoadNav_IncNavPosition(*nav, 7.5f, fwd, -1);
                float d[3];
                sub3(d, nav->position, p);
                unit(v.unknown210, d);
                if (ee::le(0.7f, dot(fwd, v.unknown210))) {
                    v.navigate_mode = 3;
                    v.unknown07c = 2;
                    v.unknown27c = 0;
                }
                v.unknown074 = sim().steps + 60;
            }
        }
    }
    if (uint32_t(v.navigate_mode - 3) < 2) {
        alignas(16) float fwd[4] = {self->info->orient[2][0], self->info->orient[2][1], self->info->orient[2][2], 0};
        if (v.unknown27c != 0) {
            if (ee::lt(dot(fwd, v.unknown210), -0.15f)) {
                WRoadNav_InitAtPoint(*nav, my_pos, fwd, (v.coll_avoidance ^ 1) != 0);
                WRoadNav_IncNavPosition(*nav, 7.5f, fwd, -1);
                v.unknown27c = 0;
            }
        } else if (ee::le(0.7f, dot(fwd, v.unknown210))) {
            v.unknown27c = 1;
        }
    }
    if (v.navigate_mode != 1 && !(v.unknown15c < sim().frame_rate)) {
        // Muito tempo com as rodas fora do chão: o navegador recomeça onde o carro está.
        alignas(16) float fwd[4] = {self->info->orient[2][0], self->info->orient[2][1], self->info->orient[2][2], 0};
        alignas(16) float p[4] = {self->pos[0], self->pos[1], self->pos[2], 0};
        WRoadNav_InitAtPoint(*nav, p, fwd, (v.coll_avoidance ^ 1) != 0);
    }
    if (v.unknown21c != 0) return;
    if (v.attack_mode != 15) return;
    alignas(16) float fwd[4] = {self->info->orient[2][0], self->info->orient[2][1], self->info->orient[2][2], 0};
    alignas(16) float p[4] = {self->pos[0], self->pos[1], self->pos[2], 0};
    WRoadNav_InitAtPoint(*nav, p, fwd, true);
    v.unknown21c = 1;
}

// ------------------------------------------------------------------------------------------------- as faixas

namespace {
// cvt.w.S de x ± 0,5 (o sinal de x): o arredondamento das contas das faixas.
int32_t round_away(float x) { return ee::cvt_w(ee::add(ee::lt(x, 0.0f) ? -0.5f : 0.5f, x)); }
uint8_t* lanes_of(AIGroundVehicle& v) { return v.unknown098; }  // +0x98: 64 pesos (0 livre, 255 bloqueada)
}  // namespace

// [driving] 0x00135b10 (sem classe no SYM) InCollisionRange(COORD3 &, COORD3 &, COORD3 &, float, float, bool): `b` está
// à frente de `a` (no sentido de `dir`) e perto: a menos de 2 x a velocidade (entre 20 e 35 m; 15 m quando `other` passa
// da velocidade + 1) e, sem `any_height`, a menos de 5 m de altura.
bool InCollisionRange(const float a[3], const float b[3], const float dir[3], float speed, float other,
                      bool any_height) {
    if (!any_height && ee::lt(5.0f, ee::abs(ee::sub(a[1], b[1])))) return false;
    const float d = dist(a, b);
    float range = ee::min(35.0f, ee::max(20.0f, ee::add(speed, speed)));
    if (ee::lt(0.0f, other) && ee::le(ee::add(speed, 1.0f), other)) range = 15.0f;
    if (ee::lt(range, d)) return false;
    float du[3], rel[3];
    unit(du, dir);
    sub3(rel, b, a);
    unit(rel, rel);
    return ee::le(0.0f, dot(du, rel));
}

// [driving] 0x00135cf0 AIGroundVehicle::GetBestWeightedLane: a janela de +0xd8 faixas de menor soma (a primeira; o
// meio dela), ou 32 se nenhuma soma menos que 255 x a largura.
int32_t AIGroundVehicle_GetBestWeightedLane(AIGroundVehicle& v) {
    const int32_t w = v.lane_weight_width;  // lbu
    int32_t best = 0x20, best_sum = w * 255;
    for (int32_t start = 0; start < 0x40 - w; ++start) {
        int32_t sum = 0;
        for (int32_t i = start; i < start + w; ++i) sum += lanes_of(v)[i];
        if (sum < best_sum) {
            best = start + int32_t(uint32_t(w) >> 1);
            best_sum = sum;
        }
    }
    return best;
}

// [driving] 0x00137070 AIGroundVehicle::WeightDirectLanesPosRad(COORD3 &, COORD3 &, COORD3 &, COORD3 &, float, float):
// um obstáculo em `point` (raio `radius`) à frente: as faixas que ele cobre (pelo lado de través `perp`) ficam
// bloqueadas. `point` é projetado na reta de través (o PS2 o muda no lugar).
void AIGroundVehicle_WeightDirectLanesPosRad(AIGroundVehicle& v, const float a[3], const float dir[3],
                                             const float perp[3], float point[3], float speed, float radius) {
    if (!InCollisionRange(a, point, dir, speed, 0.0f, false)) return;
    float rel[3];
    sub3(rel, point, a);
    const float k = dot(perp, rel);
    float step[3];
    scale3(step, perp, k);
    add3(point, a, step);
    const float centre = ee::lt(k, 0.0f) ? ee::sub(32.0f, dist(a, point)) : ee::add(32.0f, dist(a, point));
    int32_t width = 3;
    if (!(round_away(radius) * 2 < 3)) width = round_away(radius) * 2;
    int32_t start = round_away(centre) - width / 2;
    if (!(-1 < start)) start = 0;
    int32_t end = start + width;
    if (end < 0) end = -end;
    if (!(end < 0x41)) end = 0x40;
    for (int32_t i = start; i < end; ++i) {
        int32_t x = lanes_of(v)[i] + 0xff;
        if (!(x < 0x100)) x = 0xff;
        if (!(-1 < x)) x = 0;
        lanes_of(v)[i] = uint8_t(x);
    }
}

// [driving] 0x001369a0 AIGroundVehicle::GetDirectLaneWeights(COORD3 &): os pesos das 64 faixas de través (1 m cada, a
// 32 o ponto) em volta do ponto: as além das barreiras de cada lado (WCollider::GetWorldNormal a 32 m) bloqueadas, e o
// custo de sair do meio (5 por faixa); as três do meio ficam livres.
void AIGroundVehicle_GetDirectLaneWeights(AIGroundVehicle& v, const float point[3]) {
    std::memset(lanes_of(v), 0, 0x40);
    RigidBody* self = body_g(&v);
    if (!self) return;
    alignas(16) float p[4] = {point[0], point[1], point[2], 0};
    float dir[3], perp[3];
    sub3(dir, p, self->pos);
    unit(dir, dir);
    perp[0] = dir[2];
    perp[1] = dir[1];
    perp[2] = ee::neg(dir[0]);
    TrackWorld* tw = world() ? world()->track_world() : nullptr;
    if (!tw) {
        ++stats().guards;
        return;
    }
    WCollider col;
    WCollider_Construct(col, *tw, p, 64.0f, false, 0xc);
    alignas(16) float seg[8] = {p[0], p[1], p[2], 1.0f, 0, 0, 0, 1.0f};
    WorldCollisionInfo info;  // montado uma vez (o PS2 o reusa no segundo lado)
    WorldCollisionInfo_Init(info);
    for (int side = 0; side < 2; ++side) {
        if (side == 1) {
            perp[0] = ee::neg(perp[0]);
            perp[2] = ee::neg(perp[2]);
        }
        float end[3], step[3];
        scale3(step, perp, 32.0f);
        add3(end, p, step);
        seg[4] = end[0];
        seg[5] = end[1];
        seg[6] = end[2];
        seg[7] = 1.0f;
        if (!WCollider_GetWorldNormal(col, *tw, seg, info)) continue;
        const float d = dist(p, info.point);
        if (side == 0) {
            // À direita: as faixas acima de 32 + d.
            for (int32_t i = 0x3f;; --i) {
                const int32_t edge = round_away(d) + 0x20;
                if (edge < 0) {
                    if (!(0 < i)) break;
                } else if (!(edge < i)) {
                    break;
                }
                lanes_of(v)[i] = 0xff;
            }
        } else {
            // À esquerda: as abaixo de 32 - d.
            for (int32_t i = 0;; ++i) {
                const int32_t edge = 0x20 - round_away(d);
                if (!(edge < 0x41)) {
                    if (!(i < 0x40)) break;
                } else if (!(i < edge)) {
                    break;
                }
                lanes_of(v)[i] = 0xff;
            }
        }
    }
    for (int32_t i = 0; i < 0x40; ++i) {
        const int32_t r = round_away(ee::mul(ee::abs(ee::sub(32.0f, ee::cvt_s(i))), 5.0f));
        int32_t x = lanes_of(v)[i] + r;
        if (x < 0) x = 0;
        if (!(x < 0x100)) x = 0xff;
        lanes_of(v)[i] = uint8_t(x);
    }
    lanes_of(v)[0x21] = 0;
    lanes_of(v)[0x1f] = 0;
    lanes_of(v)[0x20] = 0;
}

// [driving] 0x00137310 AIGroundVehicle::CheckAgentDirectCollision(COORD3 &): o ponto (indo direto ao alvo) desviado de
// lado para a faixa livre: os pesos (GetDirectLaneWeights), mais 0x80 nas faixas de cada veículo à frente (salvo o
// alvo), a melhor janela (GetBestWeightedLane).
// Guarda: um carro da lista sem motorista (o +0xd8 dele: o PS2 leria o endereço 0xd8; o port usa 0). Com o +0x94
// (a atualização curta) só os pesos ficam de fora: o ponto ainda é desviado pela janela dos pesos de antes.
void AIGroundVehicle_CheckAgentDirectCollision(AIGroundVehicle& v, float point[4]) {
    // Com o +0x94 ligado o PS2 pula só os pesos (o bne para 0x001376c0): a melhor janela sai dos pesos que ficaram.
    do {
        if (v.update_state != 0) break;
        BondCar* car = car_g(v);
        if (!car) break;
        RigidBody* self = body_of_car(*car);
        float my_pos[3], dir[3], perp[3];
        aivu::copy3(my_pos, self->pos);
        aivu::copy3(dir, v.direction);
        perp[0] = dir[2];
        perp[1] = dir[1];
        perp[2] = ee::neg(dir[0]);
        const float speed = len_xz(self->vel);
        AIGroundVehicle_GetDirectLaneWeights(v, point);
        for (BondCar* other : sim().cars) {
            if (other->d.slot == car->d.slot) continue;
            RigidBody* b = body_of_car(*other);
            if (!(b->type < 4) || b->state == 3) continue;
            AIGroundVehicle* oa = ai_of(*other);  // RigidBody::GetOwner e o +0x5c
            if (oa == target_of(v)) continue;
            float opos[3];
            aivu::copy3(opos, b->pos);
            if (!InCollisionRange(my_pos, opos, dir, speed, 0.0f, false)) continue;
            float rel[3];
            sub3(rel, opos, my_pos);
            const float lateral = dot(perp, rel);
            int32_t width = 0;
            if (oa) width = oa->lane_weight_width;
            else ++stats().guards;
            const float centre = ee::add(32.0f, lateral);
            const float across = ee::mul(b->info->half_size[2], ee::abs(dot(b->info->orient[0], dir)));
            if (!(round_away(across) < width)) width = round_away(across);
            int32_t start = round_away(centre) - width / 2;
            if (!(-1 < start)) start = 0;
            int32_t end = start + width;
            if (end < 0) end = -end;
            if (!(end < 0x41)) end = 0x40;
            for (int32_t i = start; i < end; ++i) {
                int32_t x = lanes_of(v)[i] + 0x80;
                if (!(x < 0x100)) x = 0xff;
                if (!(-1 < x)) x = 0;
                lanes_of(v)[i] = uint8_t(x);
            }
        }
    } while (false);
    float off = ee::cvt_s(AIGroundVehicle_GetBestWeightedLane(v) - 0x20);
    if (((v.lane_weight_width ^ 1) & 1) != 0) off = ee::sub(off, 0.5f);
    float side[3], step[3];
    side[0] = v.direction[2];
    side[1] = v.direction[1];
    side[2] = ee::neg(v.direction[0]);
    unit(side, side);
    scale3(step, side, off);
    add3(point, point, step);
}

// [driving] 0x00135d88 AIGroundVehicle::GetRoadNetworkLaneWeights: os pesos das faixas da rua
// (WRoadNetwork::GetSegmentLaneWeights) mais o custo de sair da faixa em que o navegador está (5 por faixa), salvo as
// bloqueadas.
void AIGroundVehicle_GetRoadNetworkLaneWeights(AIGroundVehicle& v) {
    WRoadNav* nav = nav_of(v);
    WRoadNetwork_GetSegmentLaneWeights(nav->segment, nav->forward, lanes_of(v), 0x40, 0x20);
    const int32_t centre = round_away(nav->lane_offset) + 0x20;
    for (int32_t i = 0; i < 0x40; ++i) {
        if (!(lanes_of(v)[i] < 0xff)) continue;
        const int32_t r = round_away(ee::mul(ee::abs(ee::sub(ee::cvt_s(centre), ee::cvt_s(i))), 5.0f));
        int32_t x = lanes_of(v)[i] + r;
        if (x < 0) x = 0;
        if (!(x < 0x100)) x = 0xff;
        lanes_of(v)[i] = uint8_t(x);
    }
}

namespace {
// O arredondamento do CheckAgentRoadNetworkCollision (floorf/ceilf e o floor do newlib em double): metade para longe do
// zero.
float round_half_away(float x) {
    if (ee::lt(0.0f, x)) {
        const float f = std::floor(x);
        return ee::le(ee::add(f, 0.5f), x) ? std::ceil(x) : f;
    }
    const float c = std::ceil(x);
    return ee::le(x, ee::sub(c, 0.5f)) ? std::floor(x) : c;
}
}  // namespace

// [driving] 0x00135f70 AIGroundVehicle::CheckAgentRoadNetworkCollision(COORD3 &): na rede, a faixa livre: os pesos da rua,
// mais 0x80 (menos perto do alvo; 0 de quem foge na frente e está inteiro) nas faixas de cada veículo à frente, e a troca
// de faixa (ChangeLanes) para a melhor; sem ninguém, no modo 4, a volta para o meio.
// Guarda: um carro da lista sem motorista (o PS2 leria a partir do endereço 0).
void AIGroundVehicle_CheckAgentRoadNetworkCollision(AIGroundVehicle& v, float point[4]) {
    (void)point;
    if (v.coll_avoidance == 0) return;
    WRoadNav* nav = nav_of(v);
    if (!ee::eq(nav->lane_change_to, nav->lane_offset)) return;
    BondCar* car = car_g(v);
    if (!car) return;
    RigidBody* self = body_of_car(*car);
    bool any = false;
    if (v.update_state == 0) {
        float my_pos[3], my_fwd[3];
        aivu::copy3(my_pos, self->pos);
        aivu::copy3(my_fwd, self->info->orient[2]);
        const float my_speed = len_xz(self->vel);
        AIGroundVehicle_GetRoadNetworkLaneWeights(v);
        for (BondCar* other : sim().cars) {
            if (other->d.slot == car->d.slot) continue;
            RigidBody* b = body_of_car(*other);
            if (!(b->type < 4) || b->state == 3) continue;
            AIGroundVehicle* oa = ai_of(*other);
            float opos[3], ofwd[3], ovel[3];
            aivu::copy3(opos, b->pos);
            float ospeed = len_xz(b->vel);
            aivu::copy3(ofwd, b->info->orient[2]);
            unit(ovel, b->vel);
            if (ee::lt(ospeed, 5.0f) || ee::le(dot(ofwd, ovel), 0.0f) || ee::le(dot(my_fwd, ofwd), 0.707f)) ospeed = 0.0f;
            bool any_height = false;
            if (v.is_sub != 0) {
                if (oa) any_height = oa->is_sub != 0;
                else ++stats().guards;
            }
            if (!InCollisionRange(my_pos, opos, my_fwd, my_speed, ospeed, any_height)) continue;
            if (!oa) {
                ++stats().guards;  // o GetCollNav do outro: o PS2 leria o motorista do endereço 0
                continue;
            }
            float fwd2[3];
            aivu::copy3(fwd2, self->info->orient[2]);
            WRoadNav* cn = AIGroundVehicle_GetCollNav(*oa, fwd2, nav->mode == 4, int8_t(nav->unknown04));
            const int32_t lane = ee::cvt_w(round_half_away(cn->lane_offset)) + 0x20;
            int32_t width = oa->lane_weight_width;
            int32_t weight = 0x80;
            if (oa->primary_target != 0 && oa->primary_target == nf::p32_u(&v) && oa->attack_mode == 14) {
                bool shredded = false;
                for (int i = 0; i < 4; ++i) shredded = shredded || other->d.tyre_damage_points[i] == 0;
                if (!shredded && ee::lt(0.0f, other->hit_points())) weight = 0;
            }
            float rel[3];
            sub3(rel, opos, my_pos);
            unit(rel, rel);
            if (v.primary_target == nf::p32_u(oa) && v.unknown208 == 0) {
                if (nav->mode != 4) {
                    weight -= 0x40;
                    if (!(-1 < weight)) weight = 0;
                }
                BondCar* tc = car_g(*oa);
                if (tc && tc->d.car_class != 1) {
                    const float d = dot(self->info->orient[0], rel);
                    if (v.unknown148 != 0 && ee::lt(0.9f, ee::abs(d))) weight = -0x40;
                }
            } else if (v.unknown204 != 0 && oa->controller == 1) {
                if (ee::lt(ee::abs(dot(nav->direction, rel)), 0.2f)) weight = -0x40;
            }
            if (weight == 0) continue;
            float operp[3];
            operp[0] = ofwd[2];
            operp[1] = ofwd[1];
            operp[2] = ee::neg(ofwd[0]);
            const float across = ee::mul(ee::mul(b->info->half_size[2], 2.25f), ee::abs(dot(operp, fwd2)));
            if (!(round_away(across) < width)) width = round_away(across);
            int32_t start = lane - width / 2;
            if (!(-1 < start)) start = 0;
            int32_t end = start + width;
            if (end < 0) end = -end;
            if (!(end < 0x41)) end = 0x40;
            for (int32_t i = start; i < end; ++i) {
                int32_t x = lanes_of(v)[i] + weight;
                if (!(x < 0x100)) x = 0xff;
                if (!(-1 < x)) x = 0;
                lanes_of(v)[i] = uint8_t(x);
            }
            any = true;
        }
        WRoadNav* n5 = nf::p32_ptr<WRoadNav>(v.nav5);
        n5->mode = nav->mode == 4 ? 4 : 3;
        if (!any && !ee::eq(nav->lane_offset, 0.0f) && nav->mode == 4) {
            // Ninguém à frente no modo 4: de volta para o meio da rua, metade de cada vez.
            lanes_of(v)[0x1f] = 0;
            lanes_of(v)[0x20] = 0;
            lanes_of(v)[0x21] = 0;
            const float off = nav->lane_offset;
            float target = 0.0f;
            if (!ee::le(ee::abs(off), 2.0f)) target = ee::mul(off, 0.5f);
            const float span = ee::abs(ee::sub(nav->lane_offset, target));
            const float speed = len_xz(self->vel);
            WRoadNav_ChangeLanes(*nav, target, ee::mul(span, ee::mul(speed, 0.1f)));
            return;
        }
    }
    float target = ee::cvt_s(int16_t(AIGroundVehicle_GetBestWeightedLane(v)) - 0x20);
    if (((v.lane_weight_width ^ 1) & 1) != 0) target = ee::sub(target, 0.5f);
    const float cur = nav->lane_offset;
    if (ee::eq(target, cur)) return;
    const float speed = len_xz(self->vel);
    WRoadNav_ChangeLanes(*nav, target, ee::mul(ee::abs(ee::sub(cur, target)), ee::mul(speed, 0.1f)));
}

// ------------------------------------------------------------------------------------------- a velocidade dos agentes

namespace {
// A velocidade do corpo no plano (vmul.xz; vmulax.x; vmaddz.x; vsqrt; vaddq.x vf0).
float speed_xz(const RigidBody& b) { return len_xz(b.vel); }
}  // namespace

// A parte dos agentes do AIGroundVehicle::DoAttackMode (0x0012e808, a marca em ground_vehicle.cpp): os modos de ataque dos agentes (13 perseguir o alvo, 14 manter-se
// à frente dele, 15 seguir a rua com o alvo colado); as saídas do começo (sem vida, EMP, desligado) e os modos 1 e 12
// ficam em ground_vehicle.cpp. A velocidade pedida (+0x8c) a partir da do alvo (+0x1a4), da distância (+0x1d0) e da
// direção da busca (+0x1e0). Guardas (contadas): o alvo nulo onde o PS2 o leria (o modo 14 e o 13 com +0x208 = 1 o
// testam; o 14 sem +0x208 não), o navegador ou o trecho nulos do modo 15.
void AIGroundVehicle_DoAttackModeAgent(AIGroundVehicle& v) {
    if (v.attack_mode == 13) {
        v.speed = 30.0f;
        if (v.unknown208 == 0) {
            AIGroundVehicle* t = target_of(v);
            if (t) {
                float f20 = v.min_dist_to_target;
                BondCar* tc = car_g(*t);
                if (!tc) return;
                if (tc->d.car_class == 1) {
                    // GetCarClass do alvo: a soma das meias-medidas (z) dos dois carros x 1,25 como distância mínima.
                    const RigidBody* tb = body_of_car(*tc);
                    const RigidBody* ob = body_g(&v);
                    if (!tb || !ob) return;
                    float f1 = ee::mul(nf::raw(tb->info)->half_size[2], 1.25f);
                    const float f0 = ee::mul(nf::raw(ob->info)->half_size[2], 1.25f);
                    f1 = ee::add(f1, f0);
                    f20 = ee::max(f1, v.min_dist_to_target);
                    if (v.unknown148 != 0 && ee::le(v.unknown1d0, f1)) v.unknown148 = 0;
                }
                const RigidBody* tb = body_g(t);
                if (!tb) return;
                const float f21 = speed_xz(*tb);
                bool go = true;
                if (v.navigate_mode == 4) {
                    // Na rede: andando para longe da direção da busca (cos < 0,707), 60% da velocidade do alvo.
                    const RigidBody* ob = body_g(&v);
                    if (!ob) return;
                    float u[3];
                    unit(u, ob->vel);
                    if (ee::lt(dot(u, v.direction), flt(0x3f34fdf4u))) {
                        go = false;
                        v.unknown148 = 0;
                        v.speed = ee::mul(f21, flt(0x3f19999au));
                    }
                }
                if (go) {
                    const float f3 = v.unknown1d0;
                    if (ee::lt(f3, f20)) {
                        // Perto: o PS2 copia a linha +0x20 da orientação do próprio carro e não a usa.
                        float u[3];
                        unit(u, tb->vel);
                        const float d = dot(u, v.direction);
                        if (v.unknown148 != 0) v.speed = ee::max(30.0f, ee::mul(f21, v.speed_scaler));
                        else if (!ee::lt(0.0f, d)) v.speed = 0.0f;
                        else v.speed = ee::mul(f21, flt(0x3f4ccccdu));
                    } else {
                        float f0 = ee::max(f20, 1.0f);
                        f0 = ee::div(f3, f0);
                        f0 = ee::mul(f0, 0.75f);
                        const float f4 = v.speed_scaler;
                        f0 = ee::min(f4, f0);
                        f0 = ee::mul(f21, f0);
                        f0 = ee::max(30.0f, f0);
                        const float f2 = ee::add(f20, f20);
                        v.speed = f0;
                        if (ee::lt(f2, f3) || v.unknown148 != 0) v.speed = ee::mul(f0, f4);
                    }
                }
            }
        } else if (v.unknown208 == 1) {
            AIGroundVehicle* t = target_of(v);
            if (!t) {
                v.speed = 25.0f;
            } else {
                const RigidBody* tb = body_g(t);
                if (!tb) return;
                v.speed = ee::max(25.0f, ee::mul(speed_xz(*tb), v.speed_scaler));
            }
        }
        float f20 = v.speed_limit;
        if (v.unknown204 != 0) f20 = ee::mul(f20, glue_factor());
        v.speed = ee::min(f20, v.speed);
        return;
    }
    if (v.attack_mode == 14) {
        v.speed = 10.0f;
        float f0;
        if (v.unknown208 != 0) {
            f0 = 25.0f;
        } else {
            // A linha 2 da orientação do próprio carro (a frente) contra a direção da busca.
            const RigidBody* ob = body_g(&v);
            if (!ob) return;
            const float d = dot(nf::raw(ob->info)->orient[2], v.direction);
            const RigidBody* tb = body_g(target_of(v));  // o PS2 não testa o alvo
            if (!tb) return;
            const float sp = speed_xz(*tb);
            if (ee::lt(v.unknown1d0, v.min_dist_to_target)) f0 = ee::mul(sp, v.speed_scaler);
            else if (ee::le(0.0f, d)) f0 = ee::mul(sp, v.speed_scaler);
            else f0 = ee::mul(sp, flt(0x3f4ccccdu));
        }
        v.speed = f0;
        v.speed = ee::max(10.0f, v.speed);
        v.speed = ee::min(v.speed_limit, v.speed);
        return;
    }
    if (v.attack_mode != 15) {
        v.speed = 0.0f;
        return;
    }
    // O modo 15: a velocidade da rua (o +0x4c do trecho do navegador) vezes o +0x134; com o alvo colado (+0x158) ela
    // sobe para 1,1 x a do alvo, ou cai (0,75; 0,85) quando ele ficou para trás.
    const WRoadNav* n = nf::p32_ptr<const WRoadNav>(v.nav);
    const WRoadSegment* s = n ? road_segment(n->segment) : nullptr;
    if (!s) {
        ++stats().guards;
        return;
    }
    float f20 = s->unknown4c;
    if (v.recorded_glue != 0) {
        if (AIGroundVehicle* t = target_of(v)) {
            const RigidBody* tb = body_g(t);
            if (!tb) return;
            const float f21 = speed_xz(*tb);
            const float f2 = ee::mul(v.min_dist_to_target, glue_factor());
            const float f3 = v.unknown1d0;
            bool fast = ee::lt(f3, f2);
            if (!fast) {
                if (ee::le(0.0f, dot(v.unknown210, v.direction))) {
                    fast = true;
                } else if (ee::le(ee::add(f2, f2), f3)) {
                    f20 = ee::mul(f20, 0.75f);
                } else if (ee::le(f2, f3)) {
                    const float f1 = ee::mul(f21, flt(0x3f59999au));
                    const float f0 = ee::mul(f20, flt(0x3f59999au));
                    f20 = ee::max(f0, f1);
                }
            }
            if (fast) f20 = ee::max(f20, ee::mul(f21, flt(0x3f8ccccdu)));
            f20 = ee::min(ee::mul(v.speed_limit, glue_factor()), f20);
            if (v.unknown164 != 0) f20 = 0.0f;
        }
    }
    v.speed = ee::mul(f20, v.rec_speed_offset);
}

// A parte dos agentes do AIGroundVehicle::DoMoveMode (0x00130508, a marca em ground_vehicle.cpp): os modos de navegação dos agentes (o resto em ground_vehicle.cpp):
// 1 parado (DriveToPoint no lugar), 2 direto (o ponto à frente pela direção do alvo, desviando dos outros carros), 3 e 4
// pela rede (o ponto do navegador e a faixa livre), 5 pelo trilho (o AISplinePath de driving/game/rail.h).
void AIGroundVehicle_DoMoveModeAgent(AIGroundVehicle& v) {
    BondCar* car = car_g(v);
    if (!car) return;
    RigidBody* self = body_of_car(*car);
    switch (v.navigate_mode) {
        case 1:
            aivu::copy3(v.drive_to, self->pos);
            AIGroundVehicle_DriveToPoint(v, v.drive_to, 0.0f);
            return;
        case 2: {
            float pos[3], step[3];
            aivu::copy3(pos, self->pos);
            const float la = AIGroundVehicle_GetLookAhead(v, v.speed);
            scale3(step, v.direction, la);
            add3(v.drive_to, pos, step);
            if (v.coll_avoidance != 0) {
                if (v.controller == 5) AIGroundVehicle_CheckAgentDirectCollision(v, v.drive_to);
                else AIGroundVehicle_CheckTrafficCollision(v, v.drive_to);
            }
            AIGroundVehicle_DriveToPoint(v, v.drive_to, v.speed);
            return;
        }
        case 3: {
            const float la = AIGroundVehicle_GetLookAhead(v, v.speed);
            AIGroundVehicle_UpdateNavPos(v, la, v.drive_to);
            AIGroundVehicle_CheckAgentRoadNetworkCollision(v, v.drive_to);
            AIGroundVehicle_DriveToPoint(v, v.drive_to, v.speed);
            return;
        }
        case 4: {
            const float la = AIGroundVehicle_GetLookAhead(v, v.speed);
            AIGroundVehicle_UpdateNavPos(v, la, v.drive_to);
            if (v.update_state == 0) AIGroundVehicle_CheckAgentRoadNetworkCollision(v, v.drive_to);
            AIGroundVehicle_DriveToPoint(v, v.drive_to, v.speed);
            return;
        }
        case 5:
            // O trilho: o mesmo corpo do AIGroundVehicle::DoSplinePathUpdate (0x00131620; a pose e a velocidade do
            // RPathHandle do AISplinePath no corpo, driving/game/rail.h).
            AIGroundVehicle_DoSplinePathUpdate(v);
            return;
        default:
            return;
    }
}

// [driving] 0x00131080 AIGroundVehicle::DoAutoDriveUpdate: o estado 2 (o EngageAutoDrive do EControlToCPU). A cada quarto
// de segundo o DrivableVec da posição ao ponto da rua do navegador (+0x21c); sem caminho livre o navegador recomeça no
// carro. A direção para o ponto do navegador (+0x210, unitária); com o +0x27c o carro de costas para ela (frente . dir
// < -0,15) refaz o navegador 7,5 m à frente e o apaga; sem ele, de frente (>= 0,7) o religa. Um segundo sem rodas no chão
// recomeça o navegador no carro; o +0x268 (o UpdateStuck) põe o carro no navegador 5 m à frente (ResetCar).
void AIGroundVehicle_DoAutoDriveUpdate(AIGroundVehicle& v) {
    BondCar* car = car_g(v);
    WRoadNav* nav = nf::p32_ptr<WRoadNav>(v.nav);
    if (!car || !nav) {
        ++stats().guards;  // o PS2 leria o carro ou o navegador nulos
        return;
    }
    Simulation& s = sim();
    if (v.update_state == 0) {
        const int16_t quarter = int16_t(ee::cvt_w(ee::mul(ee::cvt_s(s.frame_rate), 0.25f)));
        if (!(s.steps < v.unknown174 + int32_t(quarter))) {
            v.unknown174 = s.steps;
            const RigidBody* b = body_of_car(*car);
            float a[3], c[3];
            copy3(a, b->pos);
            copy3(c, nav->road_point);
            v.unknown21c = AIGroundVehicle_DrivableVec(v, a, c);
        }
    }
    if (v.unknown21c == 0) {
        const RigidBody* b = body_of_car(*car);
        float fwd[3], pos[3];
        copy3(fwd, b->info->orient[2]);
        copy3(pos, b->pos);
        WRoadNav_InitAtPoint(*nav, pos, fwd, true);
        v.unknown21c = 1;
    }
    const RigidBody* b = body_of_car(*car);
    float fwd[3], pos[3];
    copy3(fwd, b->info->orient[2]);
    copy3(pos, b->pos);
    sub3(v.unknown210, nav->position, pos);
    unit(v.unknown210, v.unknown210);
    if (v.unknown27c == 0) {
        if (ee::le(ps2::ffloat(0x3f333333u), dot(fwd, v.unknown210))) v.unknown27c = 1;  // 0,7
    } else if (ee::lt(dot(fwd, v.unknown210), ps2::ffloat(0xbe19999au))) {  // -0,15
        WRoadNav_InitAtPoint(*nav, pos, fwd, false);
        WRoadNav_IncNavPosition(*nav, 7.5f, fwd, -1);
        v.unknown27c = 0;
    }
    if (car->virtual_18c() == 0) {  // GetNumWheelsOnGround (+0x18c)
        ++v.unknown15c;
    } else if (v.unknown15c < s.frame_rate) {
        v.unknown15c = 0;
    } else {
        const RigidBody* b2 = body_of_car(*car);
        float f2[3], p2[3];
        copy3(f2, b2->info->orient[2]);
        copy3(p2, b2->pos);
        WRoadNav_InitAtPoint(*nav, p2, f2, (v.coll_avoidance ^ 1) != 0);
        v.unknown15c = 0;
    }
    if (v.unknown268 != 0) {
        RigidBody* b3 = body_of_car(*car);
        b3->info->flags[0] = 1;  // +0x4fc
        float dir[3];
        copy3(dir, nav->direction);
        WRoadNav_ChangeLanes(*nav, 0.0f, 0.0f);
        WRoadNav_IncNavPosition(*nav, 5.0f, dir, -1);
        float p3[3];
        copy3(dir, nav->direction);
        copy3(p3, nav->position);
        PBondCar_ResetCar(*car, p3, dir);  // a entrada +0x164 da tabela virtual
        v.stuck = 0;
        v.unknown268 = 0;
    }
}

// ------------------------------------------------------------------------------------------------- as armas

namespace {
AIWeaponHooks g_weapon_hooks;
AIWeaponStats g_weapon_stats;
AIWeaponAnim g_weapon_anim{
    [](uint32_t h, uint32_t s, uint32_t st, uint32_t t, uint32_t m) { AnimHandle_ProcessStimuli(h, s, st, t, m); },
    [](uint32_t h, uint32_t s) { return AnimHandle_IsSystemPlaying(h, s); },
    [](uint32_t h, uint32_t s, uint32_t st, uint32_t f) { AnimHandle_SetFrame(h, s, st, f); }};

// O RAnimEngine::Handle do carro (o +0x28 do RSceneObj, o PhysicsObject +0x48); 0 sem render ou sem handle.
uint32_t weapon_handle(const BondCar& car) {
    const uint32_t render = car.d.render_object;
    if (render == 0) return 0;
    uint32_t h;
    std::memcpy(&h, nf::p32_ptr<const uint8_t>(render) + 0x28, 4);
    return h;
}
void stim(uint32_t h, uint32_t system, uint32_t stimulus) {
    g_weapon_anim.stimulus(h, system, stimulus, uint32_t(sim().steps), 2);
    if (stimulus == 10) ++g_weapon_stats.shots;
}

// O som da metralhadora (o AVehicle do carro, +0x4c do PhysicsObject: +0xb0 e +0xb4).
void gun_sound(BondCar& car, uint32_t on) {
    uint8_t* audio = nf::p32_ptr<uint8_t>(car.d.audio_object);
    if (!audio) {
        ++g_weapon_stats.guards;  // o PS2 escreveria no AVehicle do endereço 0 (os carros da IA do port não têm som)
        return;
    }
    std::memcpy(audio + 0xb4, &on, 4);
    std::memcpy(audio + 0xb0, &on, 4);
}

// O prazo do próximo tiro: arredondado (o +0,5 ou -0,5 e o cvt.w.s).
int16_t round_ticks(float f) { return int16_t(ee::cvt_w(ee::add(ee::lt(f, 0.0f) ? -0.5f : 0.5f, f))); }

// A frente do carro (a linha 2 da orientação; o w do PS2 é o lixo da pilha, que só as faixas x, y e z leem).
void car_forward(const BondCar& car, float fwd[4]) {
    const RigidBody* b = GetRigidBody(simulation(), car.d.slot);
    fwd[0] = b->info->orient[2][0];
    fwd[1] = b->info->orient[2][1];
    fwd[2] = b->info->orient[2][2];
    fwd[3] = 0;
}
}  // namespace
AIWeaponHooks& ai_weapon_hooks() { return g_weapon_hooks; }
AIWeaponStats& ai_weapon_stats() { return g_weapon_stats; }
AIWeaponAnim& ai_weapon_anim() { return g_weapon_anim; }

// [driving] 0x0012f530 ClearShot
// Divergência: o RigidBody::GetOwner e a entrada +0x5c da tabela virtual (GetAIGroundVehiclePtr) do port: o dono da vaga
// (Simulation +0x270) e o motorista do BondCar.
bool ClearShot(float distance, int16_t slot, const float forward[4], const float position[4]) {
    Simulation& s = sim();
    for (BondCar* c : s.cars) {
        if (!c || c->d.slot == slot) continue;
        const RigidBody* rb = GetRigidBody(s, c->d.slot);
        if (!(rb->type < 4) || rb->state == 3) continue;
        const BondCar* owner = dynamic_cast<const BondCar*>(s.owners[uint8_t(rb->slot)]);
        const AIGroundVehicle* av = owner ? ai_of(*owner) : nullptr;
        if (!av) {
            ++g_weapon_stats.guards;  // o PS2 leria o +0x70 de um motorista nulo
            continue;
        }
        if (av->controller != 5) continue;
        float d[3];
        sub3(d, rb->pos, position);
        unit(d, d);
        if (!ee::lt(ps2::ffloat(0x3f7d70a4u), dot(d, forward))) continue;  // 0,99
        float e[3];
        sub3(e, position, rb->pos);
        if (ee::lt(len(e), distance)) return false;
    }
    return true;
}

// [driving] 0x0012f170 AIGroundVehicle::TurnTurret
// Divergências: o acosf do newlib é o nf::fm::acos; o AttributeSet::LookupVector("TURRET_RANGE") vem da construção do
// carro (BondCar::turret_range); sem RSceneObj o PS2 leria o +0x28 do endereço 0 (guardado e contado).
void AIGroundVehicle_TurnTurret(AIGroundVehicle& v) {
    if (v.unknown208 != 0 && v.target_mode != 1) return;
    BondCar* car = car_g(v);
    if (!car) return;
    float fwd[4];
    car_forward(*car, fwd);
    float c[3] = {dot(v.direction, fwd), -1.0f, 1.0f};
    const float* p = ee::lt(c[0], -1.0f) ? &c[1] : &c[0];
    if (ee::lt(1.0f, *p)) p = &c[2];
    float ang = ee::mul(nf::fm::acos(*p), ps2::ffloat(0x3e22f983u));  // 1 / 2 pi
    // vopmula.xyz, vopmsub.xyz: a direção x a frente (o y diz de que lado).
    {
        using namespace ps2::vu;
        Reg dv, fv, acc, cr;
        load(dv, v.direction);
        load(fv, fwd);
        opmula(acc, dv, fv);
        opmsub(cr, acc, fv, dv);
        if (ee::lt(ps2::ffloat(cr[1]), 0.0f)) ang = ee::sub(1.0f, ang);
    }
    bool ok = true;
    if (car->turret_range_found) {
        const float* r = car->turret_range;
        const float deg = ee::mul(ang, 360.0f);
        ok = (ee::le(r[0], deg) && ee::le(deg, r[1])) || (ee::le(r[2], deg) && ee::le(deg, r[3]));
    }
    if (ok) {
        const float step = ps2::ffloat(0x3ba3d70au);  // 0,005
        const float cur = v.turret_angle;
        if (!ee::lt(step, ee::abs(ee::sub(ang, cur)))) {
            v.turret_angle = ang;
        } else {
            float next;
            if (ee::lt(cur, ang) && ee::le(0.75f, ang)) {
                next = ee::le(cur, 0.25f) ? ee::sub(cur, step) : ee::add(cur, step);
            } else if (ee::lt(ang, cur) && ee::le(ang, 0.25f)) {
                next = ee::le(0.75f, cur) ? ee::add(cur, step) : ee::sub(cur, step);
            } else {
                next = ee::lt(cur, ang) ? ee::add(cur, step) : ee::sub(cur, step);
            }
            v.turret_angle = next;
            if (ee::lt(1.0f, next)) v.turret_angle = ee::sub(next, 1.0f);
            else if (ee::lt(next, 0.0f)) v.turret_angle = ee::add(next, 1.0f);
        }
    }
    const float deg = ee::mul(v.turret_angle, 360.0f);
    const int32_t frame = ee::cvt_w(ee::add(ee::lt(deg, 0.0f) ? -0.5f : 0.5f, deg));
    if (car->d.render_object == 0) {
        ++g_weapon_stats.guards;
    } else if (const uint32_t h = weapon_handle(*car)) {
        g_weapon_anim.set_frame(h, 0x10, 8, uint32_t(frame));
    }
    v.unknown238 = ee::eq(v.turret_angle, ang) ? 1 : 0;
}

// [driving] 0x0012f730 AIGroundVehicle::FireBullets: com alvo (+0x1a4), sem o +0x208 e com o +0x1cc, o tiro se o caminho
// está livre e a frente aponta para o alvo (cosseno acima de 0,99), ou, com torre, se ela já aponta (+0x238): os
// estímulos 8 (abrir) dos sistemas 9 e 0x48, a direção do tiro (+0x240), e a cada prazo (+0x226, a taxa x +0x228) o 10
// (o tiro: a chave da animação cria o EFireWeapon); o som da metralhadora liga ou desliga.
void AIGroundVehicle_FireBullets(AIGroundVehicle& v) {
    BondCar* car = car_g(v);
    if (!car) return;
    if (v.primary_target == 0 || v.unknown208 != 0 || v.unknown1cc == 0) return;
    float fwd[4];
    car_forward(*car, fwd);
    bool fire = false;
    if (v.has_turret == 0) {
        float pos[4];
        copy3(pos, AIVehicle_GetPosition(v));
        pos[3] = 0;
        if (ClearShot(v.unknown1d0, car->d.slot, fwd, pos) && ee::lt(ps2::ffloat(0x3f7d70a4u), dot(v.direction, fwd)))
            fire = true;
    } else {
        fire = v.unknown238 == 1;
    }
    if (!fire) {
        ++g_weapon_stats.blocked;
        gun_sound(*car, 0);
        return;
    }
    if (const uint32_t h = weapon_handle(*car)) {
        stim(h, 9, 8);
        stim(h, 0x48, 8);
        unit(v.unknown240, v.has_turret == 0 ? fwd : v.direction);
        if (ee::le(0.0f, v.rate_of_fire)) {
            if (v.unknown226 < 1) {
                stim(h, 9, 10);
                stim(h, 0x48, 10);
                v.unknown226 = round_ticks(ee::mul(ee::cvt_s(sim().frame_rate), v.rate_of_fire));
            } else {
                v.unknown226 = int16_t(std::max(int32_t(v.unknown226) - 1, 0));
            }
        }
    } else {
        ++g_weapon_stats.no_handle;
    }
    gun_sound(*car, 1);
}

// [driving] 0x0012fa28 AIGroundVehicle::FireRockets: como o FireBullets (o cosseno acima de 0,9), os sistemas 5 e 6
// alternados (+0x22e), o prazo (+0x22c) de pelo menos meio segundo, a direção em +0x250; um lançador ainda tocando
// espera.
void AIGroundVehicle_FireRockets(AIGroundVehicle& v) {
    BondCar* car = car_g(v);
    if (!car) return;
    if (v.primary_target == 0 || v.unknown208 != 0 || v.unknown1cc == 0) return;
    float fwd[4];
    car_forward(*car, fwd);
    bool fire = false;
    if (v.has_turret == 0) {
        float pos[4];
        copy3(pos, AIVehicle_GetPosition(v));
        pos[3] = 0;
        if (ClearShot(v.unknown1d0, car->d.slot, fwd, pos) && ee::lt(ps2::ffloat(0x3f666666u), dot(v.direction, fwd)))
            fire = true;
    } else {
        fire = v.unknown238 == 1;
    }
    if (!fire) {
        ++g_weapon_stats.blocked;
        return;
    }
    const uint32_t h = weapon_handle(*car);
    if (!h) {
        ++g_weapon_stats.no_handle;
        return;
    }
    stim(h, 5, 8);
    stim(h, 6, 8);
    unit(v.unknown250, v.has_turret == 0 ? fwd : v.direction);
    if (!ee::le(0.0f, v.rate_of_fire)) return;
    if (v.unknown22c < 1) {
        uint32_t sys;
        if (v.unknown22e == 0) {
            if (g_weapon_anim.is_playing(h, 6)) {
                ++g_weapon_stats.busy;
                return;
            }
            stim(h, 6, 10);
            sys = 6;
        } else {
            if (g_weapon_anim.is_playing(h, 5)) {
                ++g_weapon_stats.busy;
                return;
            }
            stim(h, 5, 10);
            sys = 5;
        }
        stim(h, sys, 0xb);
        const float rate = ee::cvt_s(sim().frame_rate);
        v.unknown22c = round_ticks(ee::max(ee::mul(rate, 0.5f), ee::mul(rate, v.rate_of_fire)));
        v.unknown22e = uint8_t((v.unknown22e + 1) & 1);
    } else {
        v.unknown22c = int16_t(std::max(int32_t(v.unknown22c) - 1, 0));
    }
}

// [driving] 0x0012fd58 AIGroundVehicle::FireMissiles: sem torre; o caminho livre e o cosseno acima de 0,707; os sistemas
// 7/8 (os lançadores) e 0x3d/0x3e (as tampas) alternados (+0x232), o prazo em +0x230.
void AIGroundVehicle_FireMissiles(AIGroundVehicle& v) {
    BondCar* car = car_g(v);
    if (!car) return;
    if (v.primary_target == 0 || v.unknown208 != 0 || v.unknown1cc == 0) return;
    float fwd[4];
    car_forward(*car, fwd);
    float pos[4];
    copy3(pos, AIVehicle_GetPosition(v));
    pos[3] = 0;
    if (!ClearShot(v.unknown1d0, car->d.slot, fwd, pos) || !ee::lt(ps2::ffloat(0x3f34fdf4u), dot(v.direction, fwd))) {
        ++g_weapon_stats.blocked;
        return;
    }
    const uint32_t h = weapon_handle(*car);
    if (!h) {
        ++g_weapon_stats.no_handle;
        return;
    }
    stim(h, 7, 8);
    stim(h, 8, 8);
    stim(h, 0x3d, 8);
    stim(h, 0x3e, 8);
    if (!ee::le(0.0f, v.rate_of_fire)) return;
    if (v.unknown230 < 1) {
        uint32_t sys;
        if (v.unknown232 == 0) {
            if (g_weapon_anim.is_playing(h, 8)) {
                ++g_weapon_stats.busy;
                return;
            }
            stim(h, 8, 10);
            stim(h, 8, 0xb);
            stim(h, 0x3e, 10);
            sys = 0x3e;
        } else {
            if (g_weapon_anim.is_playing(h, 7)) {
                ++g_weapon_stats.busy;
                return;
            }
            stim(h, 7, 10);
            stim(h, 7, 0xb);
            stim(h, 0x3d, 10);
            sys = 0x3d;
        }
        stim(h, sys, 0xb);
        const float rate = ee::cvt_s(sim().frame_rate);
        v.unknown230 = round_ticks(ee::max(ee::mul(rate, 0.5f), ee::mul(rate, v.rate_of_fire)));
        v.unknown232 = uint8_t((v.unknown232 + 1) & 1);
    } else {
        v.unknown230 = int16_t(std::max(int32_t(v.unknown230) - 1, 0));
    }
}

// [driving] 0x00130050 AIGroundVehicle::DoWeaponUpdate: vivo, ligado (+0x224) e com alvo: a torre (+0x12c), e o
// disparo pelo modo de ataque (10 foguetes, 9 metralhadora) ou pelas armas do comando (+0x12d, +0x12e, +0x12f). Um gancho
// de ai_weapon_hooks() troca a função (a parte 17 do nf_driving_check só conta).
void AIGroundVehicle_DoWeaponUpdate(AIGroundVehicle& v) {
    BondCar* car = car_g(v);
    if (!car) return;
    if (!ee::lt(0.0f, car->hit_points())) return;
    if (v.disabled != 0 || v.weapons_active == 0 || v.primary_target == 0) return;
    auto call = [&v](const std::function<void(AIGroundVehicle&)>& hook, void (*real)(AIGroundVehicle&), uint64_t& n) {
        ++n;
        if (hook) hook(v);
        else real(v);
    };
    if (v.has_turret != 0) call(g_weapon_hooks.turn_turret, AIGroundVehicle_TurnTurret, g_weapon_stats.turret);
    if (v.attack_mode == 10) {
        call(g_weapon_hooks.fire_rockets, AIGroundVehicle_FireRockets, g_weapon_stats.rockets);
    } else if (v.attack_mode == 9) {
        call(g_weapon_hooks.fire_bullets, AIGroundVehicle_FireBullets, g_weapon_stats.bullets);
    } else {
        if (v.has_machine_guns != 0)
            call(g_weapon_hooks.fire_bullets, AIGroundVehicle_FireBullets, g_weapon_stats.bullets);
        if (v.has_rockets != 0) call(g_weapon_hooks.fire_rockets, AIGroundVehicle_FireRockets, g_weapon_stats.rockets);
        if (v.has_missiles != 0)
            call(g_weapon_hooks.fire_missiles, AIGroundVehicle_FireMissiles, g_weapon_stats.missiles);
    }
}

}  // namespace drv
