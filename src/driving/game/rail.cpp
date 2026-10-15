// O trilho (driving/game/rail.h), do MIPS do DRIVING.ELF. As desmontagens ficam em out/agent_drv_heli/ do checkout
// principal (fora do git).
//
// Divergências (port), também em cada função:
//  - o EControlToCPU (42) engata a direção automática (o estado 2 do AIGroundVehicle), cujo DoAutoDriveUpdate não foi
//    portado: o carro do jogador fica sem quem o dirija até o EControlToPlayer (43; em Paris, o 42 está no gatilho 677,
//    o fim do cronômetro, e numa regra de programador da etapa 27);
//  - os ramos dos eventos que pedem um elemento da IA (AIElementController::RedirectCarpElement) e os EPath* com uma
//    instância (o RPathHandle do objeto de cena, WWorld::GetSceneObjFromInstance: nenhum caminho de instância em Paris)
//    são contados (rail_stats) e não fazem nada.

#include "driving/game/rail.h"

#include <cstring>

#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_nav.h"
#include "driving/game/camera_events.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

RailStats g_stats;

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// O tempo que o AISplinePath dá ao RPathEngine: (float) o passo da simulação (o int de 0x339f54 convertido).
float sim_time() { return ee::cvt_s(simulation().steps); }

// O cvt.w.s e o "menos um abaixo de zero" das voltas das rodas (os ramos 0x001926b4..0x00192778).
float wrap_turn(float x) {
    if (ee::lt(1.0f, x)) return ee::sub(x, ee::cvt_s(ee::cvt_w(x)));
    if (ee::lt(x, 0.0f)) return ee::sub(x, ee::cvt_s(ee::cvt_w(x) - 1));
    return x;
}

}  // namespace

RailStats& rail_stats() { return g_stats; }

// [driving] 0x0013da80 AISplinePath::AISplinePath(CARP::AISpline *)
void AISplinePath_Construct(AISplinePath& p, const AISplineRec* spline) {
    std::memset(&p, 0, sizeof p);
    static const float kIdentity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    for (int r = 0; r < 4; ++r) std::memcpy(p.instance[r], kIdentity[r], 12);
    uint8_t* inst = reinterpret_cast<uint8_t*>(p.instance);
    inst[0xc] = 0x10;  // a bandeira de caminho
    inst[0xd] = 0xff;
    // +0xe, +0x1c, +0x2c, +0x3c: zero
    p.state.type = 2;
    p.state.unknown01 = 0xfa;
    p.state.unknown02 = 0;
    p.state.flags = 0x8000;  // andando
    p.state.scene_obj = 0;
    p.state.path = spline->path;
    p.state.master = 0;
    p.state.start_time = 0.0f;
    p.state.speed = 0.5f;
    p.state.identity_axes = 0;
    RPathHandle* h = RPathEngine_CreatePathHandle(nf::p32_u(&p.instance), nf::p32_u(&p.state), sim_time());
    p.handle = nf::p32_u(h);
    if (h) h->user = nf::p32_u(spline->name);
    else ++g_stats.guards;  // o PS2 escreveria em +0xd0 de nulo (um 'AISp' sem caminho)
    AISpline_GetApplyTransform(spline, p.apply);
}

// [driving] 0x0013dc78 AISplinePath::~AISplinePath
void AISplinePath_Destruct(AISplinePath& p) {
    RPathEngine_DestroyPathHandle(nf::p32_ptr<RPathHandle>(p.handle));
}

// [driving] 0x0013dcc0 AISplinePath::Reset
void AISplinePath_Reset(AISplinePath& p, const AISplineRec* spline) {
    p.state.path = spline->path;
    RPathHandle* h = nf::p32_ptr<RPathHandle>(p.handle);
    if (!h) {
        ++g_stats.guards;
        return;
    }
    RPathHandle_Init(*h, sim_time());
    h->user = nf::p32_u(spline->name);
    AISpline_GetApplyTransform(spline, p.apply);
}

// [driving] 0x0013dd28 AISplinePath::SetNext
void AISplinePath_SetNext(AISplinePath& p, const AISplineRec* spline) {
    if (RPathHandle* h = nf::p32_ptr<RPathHandle>(p.handle)) RPathHandle_SetNextPath(*h, spline->path);
    else ++g_stats.guards;
}

// [driving] 0x0013dfd8 AIVehicle::SetSplinePath
void AIVehicle_SetSplinePath(AIGroundVehicle& v, const AISplineRec* spline) {
    AISplinePath* cur = nf::p32_ptr<AISplinePath>(v.spline_path);
    if (!spline) {
        if (cur) {
            AISplinePath_Destruct(*cur);
            delete cur;
        }
        v.spline_path = 0;
    } else if (!cur) {
        AISplinePath* p = new AISplinePath;  // __builtin_new(0xb0)
        AISplinePath_Construct(*p, spline);
        v.spline_path = nf::p32_u(p);
    } else {
        AISplinePath_Reset(*cur, spline);
    }
}

// [driving] 0x0013e058 AIVehicle::SetNextSplinePath
void AIVehicle_SetNextSplinePath(AIGroundVehicle& v, const AISplineRec* spline) {
    ++g_stats.next_paths;
    if (AISplinePath* cur = nf::p32_ptr<AISplinePath>(v.spline_path)) {
        AISplinePath_SetNext(*cur, spline);
    } else {
        AISplinePath* p = new AISplinePath;
        AISplinePath_Construct(*p, spline);
        v.spline_path = nf::p32_u(p);
    }
}

// [driving] 0x00131508 AIGroundVehicle::EngageSplinePath: a classe 1 (a CPU, SetCarClass +0x94 da tabela virtual) e os
// controles zerados (InitializeCarControls, +0xb4), o estado 3 e o trilho.
void AIGroundVehicle_EngageSplinePath(AIGroundVehicle& v, const AISplineRec* spline) {
    v.valid_command = 1;
    v.active = 1;
    if (BondCar* car = car_of(v)) {
        car->d.car_class = 1;
        PBondCar_InitializeCarControls(*car);
    } else {
        ++g_stats.guards;
    }
    v.controller = 3;
    AIVehicle_SetSplinePath(v, spline);
    ++g_stats.engaged;
}

// [driving] 0x00131590 AIGroundVehicle::DisengageSplinePath
void AIGroundVehicle_DisengageSplinePath(AIGroundVehicle& v) {
    if (BondCar* car = car_of(v)) {
        car->d.car_class = 0;
        PBondCar_InitializeCarControls(*car);
    } else {
        ++g_stats.guards;
    }
    v.controller = 1;
    AIGroundVehicle_SetAttackMode(v, 1);
    AIGroundVehicle_SetNavigateMode(v, 1, 0);
    v.primary_target = 0;
    AIVehicle_SetSplinePath(v, nullptr);
    ++g_stats.disengaged;
}

// [driving] 0x00130e88 AIGroundVehicle::EngageAutoDrive
void AIGroundVehicle_EngageAutoDrive(AIGroundVehicle& v) {
    BondCar* car = car_of(v);
    if (!car) {
        ++g_stats.guards;  // o PS2 leria o objeto físico nulo
        return;
    }
    car->d.car_class = 1;  // SetCarClass (+0x94 da tabela virtual)
    PBondCar_InitializeCarControls(*car);
    v.controller = 2;
    AIGroundVehicle_SetAttackMode(v, 15);
    AIGroundVehicle_SetNavigateMode(v, 4, 0);
    v.primary_target = 0;
    // O ponto e a frente do corpo (+0x10 e a linha 2 da orientação do RigidBodyInfo, +0x20) e o modo 4 do navegador.
    const RigidBody* body = GetRigidBody(simulation(), car->d.slot);
    float pos[3], dir[3];
    std::memcpy(pos, body->pos, 12);
    std::memcpy(dir, nf::raw(body->info)->orient[2], 12);
    WRoadNav* nav = nav_of(v);
    nav->mode = 4;
    WRoadNav_InitAtPoint(*nav, pos, dir, false);
    ++g_stats.autodrive_engaged;
}

// [driving] 0x00130f98 AIGroundVehicle::DisengageAutoDrive
void AIGroundVehicle_DisengageAutoDrive(AIGroundVehicle& v) {
    if (BondCar* car = car_of(v)) {
        car->d.car_class = 0;
        PBondCar_InitializeCarControls(*car);
    } else {
        ++g_stats.guards;
    }
    v.controller = 1;
    AIGroundVehicle_SetAttackMode(v, 1);
    AIGroundVehicle_SetNavigateMode(v, 1, 0);
    v.primary_target = 0;
    ++g_stats.autodrive_released;
}

// [driving] 0x00131018 AIGroundVehicle::DoPlayerLogic
void AIGroundVehicle_DoPlayerLogic(AIGroundVehicle& v) {
    int16_t c = int16_t(v.unknown164 - 1);
    if (!(-1 < c)) c = 0;
    v.unknown164 = c;
}

// [driving] 0x00131620 AIGroundVehicle::DoSplinePathUpdate: o corpo na pose do caminho (a orientação com a posição na
// linha 3, pela matriz de aplicação), com a velocidade do RPathHandle e o momento (x a massa).
void AIGroundVehicle_DoSplinePathUpdate(AIGroundVehicle& v) {
    AISplinePath* sp = nf::p32_ptr<AISplinePath>(v.spline_path);
    BondCar* car = car_of(v);
    RPathHandle* h = sp ? nf::p32_ptr<RPathHandle>(sp->handle) : nullptr;
    if (!h || !car) {
        ++g_stats.guards;
        return;
    }
    float m[4][4];
    RPathHandle_GetOrientMat(*h, m);
    std::memcpy(m[3], RPathHandle_GetPosition(*h), 12);
    VU0_MATRIX4_mult(m, sp->apply, m);
    RigidBody* body = GetRigidBody(simulation(), car->d.slot);
    RigidBody_ResetObject(body, m, m[3]);
    std::memcpy(body->vel, h->velocity, 12);
    Reg vel = {}, mass = {bits(body->mass), 0, 0, 0};
    load(vel, h->velocity, XYZ);
    mulbc(vel, vel, mass, BX, XYZ);
    store(body->momentum, vel, XYZ);
    ++g_stats.spline_updates;
}

// [driving] 0x00192498 PBondCar::ProcessSplinePhysics: as rodas de um carro levado pelo trilho (a velocidade e a
// rotação das rodas pela velocidade do caminho, a derrapagem pelo ângulo entre ela e a frente, o esterço, as molas pela
// altura do chão sob cada roda); com SUB_PHYSICS (o CarPhysics +0xc0) nada.
void PBondCar_ProcessSplinePhysics(BondCar& car) {
    PBondCarData& c = car.d;
    const CarPhysics* phys = nf::p32_ptr<const CarPhysics>(c.physics);
    if (phys->sub_physics != 0) return;
    ++g_stats.spline_physics;
    RigidBody* body = GetRigidBody(simulation(), c.slot);
    AIGroundVehicle* v = ai_of(car);
    AISplinePath* sp = v ? nf::p32_ptr<AISplinePath>(v->spline_path) : nullptr;
    RPathHandle* h = sp ? nf::p32_ptr<RPathHandle>(sp->handle) : nullptr;
    if (!h) {
        ++g_stats.guards;
        return;
    }
    float m[4][4];
    RPathHandle_GetOrientMat(*h, m);
    std::memcpy(m[3], RPathHandle_GetPosition(*h), 12);
    VU0_MATRIX4_mult(m, sp->apply, m);
    // A velocidade do caminho: o módulo (vsqrt) em +0x288 e o do plano (sqrt.S) em +0x274.
    Reg vel = {};
    load(vel, h->velocity, XYZ);
    {
        Reg sq = {}, one = {0x3f800000u, 0, 0, 0}, acc = {};
        mul(sq, vel, vel, XYZ);
        mulabc(acc, vel, vel, BX, X);
        maddabc(acc, one, sq, BY, X);
        maddbc(sq, acc, one, sq, BZ, X);
        c.car_speed = flt(ps2::sqrt(sq[0]));
    }
    const float ground = ee::sqrt(ee::add(ee::mul(h->velocity[0], h->velocity[0]),
                                          ee::mul(h->velocity[2], h->velocity[2])));
    c.car_wheel_speed = ground;
    if (ee::lt(0.0f, phys->wheel_radius)) {
        // 6,32 x o raio x a taxa de quadros (Simulation +0xdbc): as voltas por passo.
        const float k = ee::mul(ee::mul(phys->wheel_radius, flt(0x40ca3d71u)), ee::cvt_s(simulation().frame_rate));
        c.car_wheel_speed = ee::div(ground, k);
    }
    if (ee::lt(c.car_wheel_speed, flt(0x3c23d70au))) c.car_wheel_speed = 0.0f;  // 0,01
    const float turn = ee::min(c.car_wheel_speed, 1.0f);
    c.car_wheel_speed = turn;
    c.wheel_spin_angle[0] = ee::add(c.wheel_spin_angle[0], turn);
    c.wheel_spin_angle[1] = ee::add(c.wheel_spin_angle[1], turn);
    c.wheel_spin_angle[0] = wrap_turn(c.wheel_spin_angle[0]);
    c.wheel_spin_angle[1] = wrap_turn(c.wheel_spin_angle[1]);
    // A direção do movimento (vrsqrt) contra a frente (linha 2) e o lado (linha 0) do caminho.
    {
        Reg sq = {}, one = {0x3f800000u, 0, 0, 0}, acc = {};
        mul(sq, vel, vel, XYZ);
        mulabc(acc, vel, vel, BX, X);
        maddabc(acc, one, sq, BY, X);
        maddbc(sq, acc, one, sq, BZ, X);
        const uint32_t q = ps2::rsqrt(0x3f800000u, sq[0]);
        muls(vel, vel, q, XYZ);
    }
    auto dot = [&](const float row[4]) {
        Reg r = {}, t = {};
        load(r, row, XYZW);
        mul(t, r, vel, XYZ);
        addbc(t, t, t, BY, X);
        addbc(t, t, t, BZ, X);
        return t[0];
    };
    const uint32_t fwd = dot(m[2]);
    if (ps2::less(fwd, 0x3f34fdf4u)) {  // 0,707
        c.skidding = 1;
        c.wheel_slip[3] = 1.0f;
        c.wheel_slip[2] = 1.0f;
        if (ps2::less(fwd, 0x3e800000u)) {  // 0,25
            c.wheel_slip[0] = 1.0f;
            c.wheel_slip[1] = 1.0f;
        }
    } else {
        c.skidding = 0;
    }
    const uint32_t side = dot(m[0]);
    c.car_steer = ee::min(1.0f, ee::max(-1.0f, flt(side)));
    // GetPhysics (+0x1d4 da tabela virtual: o +0x23c, a cópia do passo): IS_SNOWMOBILE ou SUB_PHYSICS zeram as molas.
    if (phys->is_snowmobile != 0 || phys->sub_physics != 0) {
        for (int w = 0; w < 4; ++w) {
            c.wheel_slip[w] = 0.0f;
            c.suspension[w] = 0.0f;
        }
        return;
    }
    RigidBodyInfo* info = nf::raw(body->info);
    for (int w = 0; w < 4; ++w) {
        // A alavanca da roda girada pela orientação do caminho (o w da alavanca do mundo fica) e posta na posição.
        Reg lv = {}, r0 = {}, r1 = {}, r2 = {}, acc = {}, out = {};
        load(lv, info->lever_local[w]);
        load(r0, m[0]);
        load(r1, m[1]);
        load(r2, m[2]);
        load(out, info->lever_world[w]);
        mulabc(acc, r0, lv, BX, XYZ);
        maddabc(acc, r1, lv, BY, XYZ);
        maddbc(out, acc, r2, lv, BZ, XYZ);
        store(info->lever_world[w], out);
        std::memcpy(c.wheel_pos[w], info->lever_world[w], 16);
        Reg wp = {}, pp = {};
        load(wp, c.wheel_pos[w]);
        load(pp, m[3]);
        add(wp, wp, pp, XYZ);
        store(c.wheel_pos[w], wp);
        TempGetHeightInformation(body, c.wheel_pos[w], c.wheel_road_normal[w], &c.wheels[w]);
        float s = ee::add(c.wheel_road_normal[w][3], phys->spring_rest_length);
        s = ee::min(s, phys->spring_compression_limit);
        s = ee::max(s, 0.0f);
        c.suspension[w] = s;
        if (ee::le(s, 0.0f)) c.wheel_slip[w] = 0.0f;
    }
}

// [driving] 0x00145d00 EPathUtil_GetFromInstanceOrAI
RPathHandle* EPathUtil_GetFromInstanceOrAI(uint32_t instance, uint32_t element) {
    if (instance != 0) {
        // WWorld::GetSceneObjFromInstance e o RSceneObj +0x2c: nenhum objeto de cena com caminho em Paris.
        ++g_stats.instances;
        return nullptr;
    }
    AIGroundVehicle* v = nullptr;
    if (element == 0) {
        v = rail_player_vehicle();
    } else {
        // AIElementController::RedirectCarpElement (driving/ai/element_controller.h): o veículo acordado do elemento.
        ++g_stats.ai_elements;
        const AIElement* el = AIElementController_RedirectCarpElement(element);
        if (!el || el->state != 1 || el->type != 1) return nullptr;
        v = rail_element_vehicle(*el);
    }
    if (!v) return nullptr;
    AISplinePath* sp = nf::p32_ptr<AISplinePath>(v->spline_path);
    return sp ? nf::p32_ptr<RPathHandle>(sp->handle) : nullptr;
}

AIGroundVehicle* rail_element_vehicle(const AIElement& el) {
    // O +0x50 do elemento: o AIGroundVehicle (o AIHelicopter dos helicópteros não existe no port: nulo, contado).
    if (el.heli != 0) {
        ++g_stats.guards;
        return nullptr;
    }
    AIGroundVehicle* v = nf::p32_ptr<AIGroundVehicle>(el.ai);
    if (!v) ++g_stats.guards;
    return v;
}

BondCar* rail_player_car() {
    const Simulation& s = simulation();
    return s.cars.empty() ? nullptr : s.cars[0];
}

AIGroundVehicle* rail_player_vehicle() {
    BondCar* car = rail_player_car();
    return car ? ai_of(*car) : nullptr;
}

namespace {

RPathHandle* path_of(const PendingEvent& e) {
    RPathHandle* h = EPathUtil_GetFromInstanceOrAI(e.obj[1], e.obj[2]);
    if (h) ++g_stats.path_events;
    return h;
}

}  // namespace

bool rail_event_execute(const PendingEvent& e) {
    switch (e.type) {
        // [driving] 0x0014ae60 EControlToSpline::~EControlToSpline: o carro no trilho (o elemento 0: o do jogador, e
        // RPlayerCamera::SetControlToCPU(1), driving/game/heli_section.h) se o 'AISp' tem caminho.
        case 202: {
            const AISplineRec* s = nf::p32_ptr<const AISplineRec>(e.obj[2]);
            if (s && s->path != 0) {
                if (e.obj[1] == 0) {
                    if (AIGroundVehicle* v = rail_player_vehicle()) AIGroundVehicle_EngageSplinePath(*v, s);
                    else ++g_stats.guards;
                    camera_events_control_to_cpu(1);
                } else {
                    // O elemento acordado: o EngageSplinePath do seu veículo (a entrada +0x24 do AIVehicle).
                    ++g_stats.ai_elements;
                    const AIElement* el = AIElementController_RedirectCarpElement(e.obj[1]);
                    if (el && el->state == 1 && el->type == 1)
                        if (AIGroundVehicle* v = rail_element_vehicle(*el)) AIGroundVehicle_EngageSplinePath(*v, s);
                }
            }
            return true;
        }
        // [driving] 0x0014c110 EEndControlToSpline::~EEndControlToSpline (o elemento 0: o do jogador e a câmera)
        case 239:
            if (e.obj[1] == 0) {
                if (AIGroundVehicle* v = rail_player_vehicle()) AIGroundVehicle_DisengageSplinePath(*v);
                else ++g_stats.guards;
                camera_events_control_to_cpu(0);
            } else {
                // O DisengageSplinePath do veículo do elemento (o PS2 não testa se ele está acordado: guarda).
                ++g_stats.ai_elements;
                const AIElement* el = AIElementController_RedirectCarpElement(e.obj[1]);
                if (AIGroundVehicle* v = el ? rail_element_vehicle(*el) : nullptr) AIGroundVehicle_DisengageSplinePath(*v);
            }
            return true;
        // [driving] 0x00158370 ESetNextAISpline::~ESetNextAISpline
        case 209: {
            const AISplineRec* s = nf::p32_ptr<const AISplineRec>(e.obj[2]);
            if (s && s->path != 0) {
                if (e.obj[1] == 0) {
                    if (AIGroundVehicle* v = rail_player_vehicle()) AIVehicle_SetNextSplinePath(*v, s);
                    else ++g_stats.guards;
                } else {
                    ++g_stats.ai_elements;
                    const AIElement* el = AIElementController_RedirectCarpElement(e.obj[1]);
                    if (el && el->state == 1 && el->type == 1)
                        if (AIGroundVehicle* v = rail_element_vehicle(*el)) AIVehicle_SetNextSplinePath(*v, s);
                }
            }
            return true;
        }
        // [driving] 0x0014ac38 EControlToCPU::~EControlToCPU: a direção automática do carro do jogador e a câmera na
        // CPU (RPlayerCamera::SetControlToCPU(1)).
        case 42:
            if (AIGroundVehicle* v = rail_player_vehicle()) AIGroundVehicle_EngageAutoDrive(*v);
            else ++g_stats.guards;
            camera_events_control_to_cpu(1);
            return true;
        // [driving] 0x0014ad18 EControlToPlayer::~EControlToPlayer: o jogador de volta, o acelerador e o freio
        // pedidos a 0 (PBondCar::SetTargetGas 0x001930b0 e SetTargetBrake 0x001930b8, +0x124 e +0x12c da tabela
        // virtual), o IOModule mandando as ações de novo (+4 = 1) e a câmera.
        case 43:
            if (AIGroundVehicle* v = rail_player_vehicle()) AIGroundVehicle_DisengageAutoDrive(*v);
            else ++g_stats.guards;
            if (BondCar* car = rail_player_car()) {
                car->d.target_gas = 0.0f;
                car->d.target_brake = 0.0f;
            } else {
                ++g_stats.guards;
            }
            mission_env().io_updating = 1;
            camera_events_control_to_cpu(0);
            return true;
        // [driving] 0x001482f8 EAutoDriveSpeedChange::~EAutoDriveSpeedChange: a velocidade da direção automática
        // (AIGroundVehicle +0x134).
        case 108:
            if (AIGroundVehicle* v = rail_player_vehicle()) v->rec_speed_offset = flt(e.obj[1]);
            else ++g_stats.guards;
            return true;
        // [driving] 0x00152cb0 EPathSetDesiredThrottleStatic::~EPathSetDesiredThrottleStatic
        case 116:
            if (RPathHandle* h = path_of(e)) RPathHandle_SetDesiredThrottle(*h, flt(e.obj[3]));
            return true;
        // [driving] 0x00153340 EPathSetTimeStatic::~EPathSetTimeStatic: a fração da duração do caminho.
        case 153:
            if (RPathHandle* h = path_of(e))
                RPathHandle_SetParametricTime(*h, ee::mul(RPathHandle_GetParametricDuration(*h), flt(e.obj[3])));
            return true;
        // [driving] 0x00151dc0 EPathChangeThrottleStatic::~EPathChangeThrottleStatic
        case 215:
            if (RPathHandle* h = path_of(e)) {
                RPathHandle_SetAccelDelay(*h, int32_t(e.obj[3]));
                RPathHandle_SetAcceleration(*h, flt(e.obj[4]));
                RPathHandle_SetDesiredThrottle(*h, flt(e.obj[5]));
                RPathHandle_SetRunning(*h, 1);
            }
            return true;
        default:
            return false;
    }
}

}  // namespace drv
