// O dano do carro (driving/game/bond_car.h "o dano", docs/driving.md seção 21), portado do MIPS do DRIVING.ELF
// (PBondCar.obj, PhysicsData.obj e UDevCons.obj): PBondCar::ApplyDamage e as peças que ele chama (FindOBBIntersect,
// BytesToCoord, CARP::BaseDesc::CalcDamageZone, as zonas vizinhas, PhysicsObject::LoseHitPoints) e o
// PBondCar::AttackWithEmp. As desmontagens ficam em out/agent_drv_ai/ do checkout principal (fora do git).
//
// O ApplyDamage: o segmento de -> para (no referencial do carro) acha o ponto na caixa do modelo; a zona de dano (16 da
// tabela 0x3c1fe8) sai do ponto; um tiro (tipo 2) num carro com estouro de pneus gasta os pontos do pneu do lado
// atingido (e estoura); a vida cai (salvo a imunidade, o escudo +0x434 absorve antes); um carro da classe 2 que pontua
// e morre conta na missão (IncKills; os tiros acertados IncShotsHit); um tiro empurra o carro (ApplyForces com um torque
// sorteado pelo SimRandom); as zonas +0x2f0 (dois valores por zona: o amassado, 0..1, e o rasgado, 0..2, que o
// RVehicle::ComputeDamage leva ao desenho) sobem pela divisão do dano, e o render recebe estímulos e efeitos.
//
// Divergências (cada uma também na função):
//  - o Signature do PS2 (quem causou o dano) não existe no port: damage_source_is_player() diz se ele é do jogador;
//  - o que vai ao render e ao som não existe no carro do port (o RVehicle +0x48 é do nf_driving): os estímulos do
//    RAnimEngine (ProcessStimuli, ProcessStimuliZones), o RVehicle::TriggerFX, o GHud::TriggerDamageFlash, o som do
//    estouro do pneu (o AOneShotSound "SFX_TireBlowOut"), o do helicóptero morto e o SetVisualDamage dos submarinos são
//    gravados (car_damage_stats().calls_out, a parte 17 os compara com os do original) e contados;
//  - o RSceneObj::GetBoundingDimensions, o RVehicle::GetRenderOffset e a caixa das zonas do BaseDesc vêm do CarDesc do
//    carro (BondCar::desc_*), a mesma conta do render;
//  - a lista de alvos da Simulation (+0xdfc, os objetos com +0x6c igual ao alvo do carro) não existe no port: o laço de
//    IncShotsHit dela é contado;
//  - guardas (contadas): a missão nula (o PS2 leria o gerente no endereço 0), a física nula.

#include <cstring>

#include "driving/ai/ai_vu.h"
#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/sim_random.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/physics/collision.h"
#include "driving/physics/simulation.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

using namespace aivu;

namespace {

CarDamageStats g_stats;
bool g_source_player = false;

// A tabela 0x3c1fe8: o código de cada zona (os bits do CalcDamageZone) e as zonas vizinhas.
constexpr uint16_t kZoneCode[16] = {0x0168, 0x0440, 0x0154, 0x0054, 0x0094, 0x0294, 0x0480, 0x02a8,
                                    0x00a8, 0x0068, 0x0049, 0x0045, 0x0085, 0x0089, 0x0042, 0x0082};
constexpr uint16_t kZoneNeighbours[16] = {0x4602, 0x4c05, 0x480a, 0xd816, 0xd868, 0x9050, 0xb0a0, 0xa140,
                                          0xe6c0, 0xe503, 0x2a03, 0x140e, 0x2870, 0x15c0, 0x820f, 0x41f0};
// 0x332068..0x332074 (os limiares dos estímulos do dano total), 0x332080..0x33208c (os níveis das zonas), 0x332090 e
// 0x332094 (0,75: o efeito 9 e as vizinhas), 0x379d20 (o peso das zonas vizinhas e o da atingida).
constexpr float kStim[4] = {0.75f, 0.5f, 0.25f, 0.001f};
constexpr float kLevel[4] = {0.15f, 0.35f, 0.65f, 0.85f};
constexpr float kWeight[2] = {0.2f, 1.0f};
constexpr uint32_t kOneOver255 = 0x3b808081u;  // BytesToCoord
constexpr uint32_t kTwoM16 = 0x37800000u;      // 2^-16 (SimRandom)

void note(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    if (g_stats.trace) g_stats.calls_out.push_back({fn, {a0, a1, a2, a3}});
}

// RAnimEngine::Handle::ProcessStimuli (0x1a27a0, com o sistema: o pneu estourado) e (0x1a2868) e
// ProcessStimuliZones (0x1a2b50): o estado de animação do render (+0x28) não existe no carro do port.
void stimuli3(int32_t system, int32_t stimulus, int32_t time) {
    ++g_stats.stimuli;
    note(0x1a27a0, uint32_t(system), uint32_t(stimulus), uint32_t(time), 2);
}
void stimuli(int32_t stimulus, int32_t time) {
    ++g_stats.stimuli;
    note(0x1a2868, uint32_t(stimulus), uint32_t(time), 2);
}
void stimuli_zones(int32_t stimulus, uint32_t zones, int32_t time) {
    ++g_stats.stimuli;
    note(0x1a2b50, uint32_t(stimulus), zones, uint32_t(time), 2);
}
// RVehicle::TriggerFX(-1, efeito, zonas, 0, kZeroCoord3, kZeroCoord3) (a entrada +0x84 do RVehicle).
void trigger_fx(BondCar& car, int32_t fx, uint32_t zones) {
    ++g_stats.render_fx;
    ++car.render_fx;
    note(0x1d3c28, uint32_t(fx), zones);
}

// __fixunssfsi (0x0024e340): o float sem sinal (o libgcc do EE: acima de 2^31, subtrai e liga o bit).
uint32_t fixunssfsi(float f) {
    if (!ee::lt(f, 2147483648.0f)) return uint32_t(ee::cvt_w(ee::sub(f, 2147483648.0f))) ^ 0x80000000u;
    return uint32_t(ee::cvt_w(f));
}

// PhysicsObject::ApplyForces (0x00195728): no corpo rígido (o bit 0 de +0x44 desligado) ResolveForce, ResolveTorque e
// o bit 0 de +0x7c; um corpo simples (SimpleRigidBody::Accelerate) não existe no carro.
void apply_forces(BondCar& car, const float force[4], const float torque[4]) {
    ++g_stats.forces;
    if (car.d.flags & 1) {
        ++g_stats.guards;
        return;
    }
    RigidBody* rb = car.body();
    ResolveForce(rb, force);
    ResolveTorque(rb, torque);
    rb->radius_flags |= 1u;
}

}  // namespace

CarDamageStats& car_damage_stats() { return g_stats; }
bool& damage_source_is_player() { return g_source_player; }

// [driving] 0x00195a58 PhysicsObject::LoseHitPoints
void PhysicsObject_LoseHitPoints(BondCar& car, float amount) {
    float* hp = nf::p32_ptr<float>(car.d.hit_point_loc);
    if (!hp) return;
    if (ee::lt(amount, 0.0f)) amount = 0.0f;
    *hp = ee::sub(*hp, amount);
    if (ee::lt(*hp, 0.0f)) *hp = 0.0f;
}

// [driving] 0x002d3950 FindOBBIntersect: de a para b, a primeira face da caixa (meias-medidas `half`, centrada) que a
// reta cruza, eixo a eixo pelo menor parâmetro; o ponto tem de ficar a até 0,1 da caixa nos três eixos (senão o eixo
// recebe 10^9 e tenta o próximo, três vezes).
bool FindOBBIntersect(const float half[4], const float a[4], const float b[4], float out[4]) {
    float d[3], u[3], corner[3], rel[3];
    sub3(d, b, a);
    unit(u, d);
    for (int i = 0; i < 3; ++i) corner[i] = ee::lt(a[i], 0.0f) ? ee::neg(half[i]) : half[i];
    sub3(rel, corner, a);
    float t[3] = {0.0f, 0.0f, 0.0f};
    const float eps = flt(0x38d1b717u);  // 1e-4
    for (int i = 0; i < 3; ++i)
        if (ee::lt(eps, ee::mul(u[i], u[i]))) t[i] = ee::div(rel[i], u[i]);
    float at[3] = {ee::abs(t[0]), ee::abs(t[1]), ee::abs(t[2])};
    const float far = flt(0x4e6e6b28u);  // 10^9
    const float m = flt(0x3dcccccdu);    // 0,1
    for (int n = 3; n > 0;) {
        float step[3], bump[3] = {0.0f, 0.0f, 0.0f};
        if (ee::lt(at[0], at[1]) && ee::lt(at[0], at[2])) {
            scale3(step, u, t[0]);
            bump[0] = far;
        } else if (ee::lt(at[1], at[2])) {
            scale3(step, u, t[1]);
            bump[1] = far;
        } else {
            scale3(step, u, t[2]);
            bump[2] = far;
        }
        add3(out, a, step);
        --n;
        if (ee::lt(ee::add(half[0], m), ee::abs(out[0])) || ee::lt(ee::add(half[1], m), ee::abs(out[1])) ||
            ee::lt(ee::add(half[2], m), ee::abs(out[2]))) {
            add3(at, at, bump);
            continue;
        }
        return true;
    }
    return false;
}

// [driving] 0x002d0b08 BytesToCoord: os bytes 2, 1 e 0 de `bytes` / 255, + add, x scale, + add2 (os nulos pulados).
void BytesToCoord(uint32_t bytes, const float* add, const float* scale, const float* add2, float out[4]) {
    out[0] = ee::cvt_s(int32_t((bytes >> 16) & 0xff));
    out[1] = ee::cvt_s(int32_t((bytes >> 8) & 0xff));
    out[2] = ee::cvt_s(int32_t(bytes & 0xff));
    scale3(out, out, flt(kOneOver255));
    if (add) add3(out, out, add);
    if (scale)
        for (int i = 0; i < 3; ++i) out[i] = ee::mul(out[i], scale[i]);
    if (add2) add3(out, out, add2);
}

// [driving] 0x002db798 CARP::BaseDesc::CalcDamageZone: o ponto (x e z trocados de sinal, a escala 0x3c1f40) contra a
// caixa das zonas: três faixas em x (bits 0x14/0x04/0x08/0x28), três em z (0x140/0x40/0x80/0x280), em cima (1) ou
// embaixo (2) pelo y; o código acha a zona na tabela (nenhuma: 0).
int32_t CalcDamageZone(const float zone_min[3], const float zone_max[3], const float point[4]) {
    const float q[3] = {ee::mul(point[0], -1.0f), ee::mul(point[1], 1.0f), ee::mul(point[2], -1.0f)};
    uint32_t code;
    if (ee::lt(q[0], zone_min[0])) code = 0x14;
    else if (ee::lt(q[0], ee::mul(ee::add(zone_min[0], zone_max[0]), 0.5f))) code = 4;
    else if (ee::lt(zone_max[0], q[0])) code = 0x28;
    else code = 8;
    if (ee::lt(q[2], zone_min[2])) code |= 0x140;
    else if (ee::lt(q[2], ee::mul(ee::add(zone_min[2], zone_max[2]), 0.5f))) code |= 0x40;
    else if (ee::lt(zone_max[2], q[2])) code |= 0x280;
    else code |= 0x80;
    if (ee::lt(q[1], zone_min[1])) code = (code & 0xfffff8c3u) | 2;
    else if (ee::lt(zone_max[1], q[1])) code = (code & 0xfffffccfu) | 1;
    if ((code & 3) == 0 && (code & 0xc) != 0 && (code & 0x30) == 0) code = (code & 0xfffffcf3u) | 0x400;
    for (int i = 0; i < 16; ++i)
        if (kZoneCode[i] == code) return i;
    return 0;
}

// [driving] 0x002db930 as zonas vizinhas (a metade alta da entrada da tabela).
uint32_t DamageZoneNeighbours(int32_t zone) { return kZoneNeighbours[zone & 15]; }

// [driving] 0x00189598 PBondCar::ApplyDamage
int32_t PBondCar_ApplyDamage(BondCar& car, const float from[3], const float to[3], float amount, float split,
                             int32_t kind) {
    PBondCarData& c = car.d;
    ++g_stats.calls;
    Simulation& sim = simulation();
    SMissionManagerData* m = mission_exists() ? &mission_manager() : nullptr;
    const bool emp_tyres = c.tyre_blow_outs != 0 && kind == 2;
    uint32_t immunity = c.immunity;
    if (uint32_t(c.car_class) < 2) {
        if (m) {
            note(0x2068b8, ps2::fbits(amount));
            SMissionManager_IncPlayerDamage(*m, amount);
            if (m->cinematic_mode != 0) immunity = 1;
        } else {
            ++g_stats.guards;
        }
    }
    if (uint32_t(kind - 2) < 2 && ee::lt(1.0f, amount) && uint32_t(c.car_class) < 2 && m && m->auto_drive != 0 &&
        immunity == 0) {
        ++g_stats.hud;  // GHud::TriggerDamageFlash(GHud::TheApp(), dano)
        note(0x23e270, ps2::fbits(amount));
    }
    if (float* shield = nf::p32_ptr<float>(c.shield_point_loc)) {
        if (ee::lt(amount, *shield)) {
            *shield = ee::sub(*shield, amount);
            ++g_stats.shield;
            return 0x90;
        }
        amount = ee::sub(amount, *shield);
        *shield = 0.0f;
    }
    const int32_t steps = sim.steps;
    RigidBody* body = car.body();
    const float* half = car.desc_half;
    const float render_offset = car.desc_render_offset;
    alignas(16) float a[4] = {0, 0, 0, 0}, b[4] = {0, 0, 0, 0}, hit[4] = {0, 0, 0, 0};
    sub3(a, from, body->pos);
    sub3(b, to, body->pos);
    ConvertWorldToLocal(body, a);
    ConvertWorldToLocal(body, b);
    if (!FindOBBIntersect(half, a, b, hit)) {
        ++g_stats.misses;
        return 0;
    }
    ++g_stats.hits;
    // Simulation::FindPhysicsObjectSignature e PhysicsObject::IsOwnedBy(GetPlayerObject): ver o cabeçalho.
    uint32_t scoring = c.scoreable;
    if (!g_source_player && kind == 2) scoring = 0;
    if (kind == 1) scoring = 0;
    if (scoring != 0 && c.car_class == 2 && ee::le(5.0f, amount) && ee::lt(0.0f, car.hit_points())) {
        note(0x206900, kind == 2 ? 1u : 0u);
        if (m) SMissionManager_IncShotsHit(*m, kind == 2);
        else ++g_stats.guards;
    }
    if (ee::lt(c.unknown2dc, 0.0f)) c.unknown2dc = ee::div(32.0f, car.hit_points());
    const float zp[4] = {hit[0], ee::add(ee::mul(hit[1], 0.5f), render_offset), hit[2], hit[3]};
    const int32_t zone = CalcDamageZone(car.desc_zone_min, car.desc_zone_max, zp);
    uint32_t mask = 1u << zone;
    CarPhysics* phys = car.physics();
    if (!phys) {
        ++g_stats.guards;
        return 0x10;
    }
    if (emp_tyres) {
        // O tiro nos pneus: o lado atingido pela relação das distâncias às faces (x ou z), o pneu (as alavancas 0-3) do
        // lado e perto do ponto (o raio da roda x TYRE_DAMAGE_RADIUS) perde o dano em pontos; a zero estoura. O dano do
        // tiro vai todo ao pneu.
        const bool x_side = ee::lt(ee::abs(ee::sub(ee::abs(hit[0]), ee::abs(half[0]))),
                                   ee::abs(ee::sub(ee::abs(hit[2]), ee::abs(half[2]))));
        float r = ee::mul(phys->wheel_radius, bond_car_tuning().tyre_damage_radius);
        const uint32_t dmg = fixunssfsi(amount);
        r = ee::mul(r, r);
        const float(*lever)[4] = body->info->lever_local;
        for (int t = 0; t < 4; ++t) {
            if (c.tyre_damage_points[t] == 0) continue;
            bool side, keep_x;
            if (x_side) {
                keep_x = false;
                side = !ee::lt(0.0f, lever[t][0]) != ee::lt(0.0f, hit[0]);
            } else {
                keep_x = ee::lt(ee::abs(ee::div(half[2], half[0])), ee::abs(ee::div(hit[2], hit[0])));
                side = keep_x && (!ee::lt(0.0f, lever[t][2]) != ee::lt(0.0f, hit[2]));
            }
            if (!side) continue;
            float p[3] = {lever[t][0], lever[t][1], lever[t][2]};
            if (!keep_x) p[0] = hit[0];
            if (!x_side) p[2] = hit[2];
            float dv[3];
            sub3(dv, p, hit);
            if (!ee::lt(flt(len_sq(dv)), r)) continue;
            ++g_stats.tyres_hit;
            if (dmg < c.tyre_damage_points[t]) {
                c.tyre_damage_points[t] -= dmg;
            } else {
                c.tyre_damage_points[t] = 0;
                ++g_stats.blowouts;
                stimuli3(t + 1, 3, steps);  // o pneu (a tabela 0x379cc8: 1..4)
                ++g_stats.sounds;           // o AOneShotSound "SFX_TireBlowOut" no mix "Blowout"
                note(0x2fca38);
            }
            amount = 0.0f;
            break;
        }
    }
    const bool was_dead = ee::le(car.hit_points(), 0.0f);
    uint32_t dead = 0;
    if (immunity == 0) PhysicsObject_LoseHitPoints(car, amount);
    if (phys->sub_physics == 0) {
        dead = ee::le(car.hit_points(), 0.0f) ? 1 : 0;
    } else if (was_dead) {
        dead = kind == 1 ? 1 : 0;
    }
    const float part_a = ee::mul(amount, split);
    if (dead != 0 && !was_dead && c.car_class == 2 && c.scoreable != 0) {
        note(0x206960, 1);
        if (m) SMissionManager_IncKills(*m, 1);
        else ++g_stats.guards;
        c.scoreable = 0;  // SetScoreable(0) (a entrada +0x16c)
        ++g_stats.kills;
        // (com um alvo +0x2e0, a lista +0xdfc da Simulation: vazia no port, ver o cabeçalho)
    }
    if (!was_dead && ee::le(car.hit_points(), 0.0f) && phys->sub_physics != 0) {
        // O submarino (e o que mais tiver SUB_PHYSICS) morto: as zonas inteiras, os controles sorteados (gira e afunda);
        // o helicóptero ("IS_HELI") também aciona o som da queda.
        if (c.scoreable != 0) {
            note(0x206960, 1);
            if (m) SMissionManager_IncKills(*m, 1);
            else ++g_stats.guards;
            c.scoreable = 0;
        }
        PBondCar_SetVisualDamage(car, 1.0f, 1.0f, 1.0f, 1.0f);  // a entrada +0x1a4
        const float k = flt(kTwoM16), s3 = flt(0x3e99999au);
        float v = ee::add(ee::mul(ee::mul(ee::cvt_s(int32_t(SimRandom_Generate(sim_random()))), k), s3),
                          flt(0x3f333333u));
        c.control.steering = v;
        if (ee::lt(0.5f, ee::mul(ee::cvt_s(int32_t(SimRandom_Generate(sim_random()))), k)))
            c.control.steering = ee::neg(c.control.steering);
        const float r3 = ee::cvt_s(int32_t(SimRandom_Generate(sim_random())));
        c.control.gas = 0.5f;
        c.control.brake = 0.0f;
        c.control.steering_vertical = ee::mul(ee::add(ee::mul(ee::mul(r3, k), s3), flt(0x3dcccccdu)), s3);
        if (car.is_heli) {
            c.control.gas = 1.0f;
            ++g_stats.sounds;  // o som do carro (+0x388): a queda
            note(0x388);
        }
    }
    if (kind == 2) {
        // O tiro empurra: a força ao longo de de -> para (dano x 1000, o y pelo menos um décimo disso, em dobro) e um
        // torque sorteado (BytesToCoord entre -0,5 e +0,5 de (6000, 7000, 6000)) vezes o dano; não no carro do jogador
        // em direção automática.
        const float k = ee::mul(amount, 1000.0f);
        float dv[3], u[3];
        sub3(dv, to, from);
        unit(u, dv);
        alignas(16) float force[4] = {0, 0, 0, 0};
        scale3(force, u, k);
        const float y = ee::max(force[1], ee::mul(k, flt(0x3dcccccdu)));
        force[1] = ee::add(y, y);
        const float lo[3] = {-0.5f, -0.5f, -0.5f};
        const float hi[3] = {6000.0f, 7000.0f, 6000.0f};
        const uint32_t r1 = SimRandom_Generate(sim_random());
        const uint32_t r2 = SimRandom_Generate(sim_random());
        alignas(16) float torque[4] = {0, 0, 0, 0};
        BytesToCoord((~r1 << 16) | r2, lo, hi, nullptr, torque);
        scale3(torque, torque, amount);
        BondCar* player = sim.cars.empty() ? nullptr : sim.cars.front();
        if (&car != player || !m || m->auto_drive == 0) apply_forces(car, force, torque);
    }
    const float dmg_b = ee::mul(part_a, c.unknown2dc);
    const float dmg_a = ee::mul(ee::mul(amount, ee::sub(1.0f, split)), c.unknown2dc);
    if (dead != 0) {
        // Destruído: os estímulos de destruição, o efeito 10 em todas as zonas e as zonas no máximo (o tipo 0) ou a
        // atingida e as vizinhas.
        ++g_stats.deaths;
        stimuli(2, steps);
        stimuli(kind == 3 ? 7 : 6, steps);
        stimuli_zones(0x16, 0xffff, steps);
        stimuli_zones(0x15, 0xffff, steps);
        stimuli_zones(0x14, 0xffff, steps);
        stimuli_zones(0x13, 0xffff, steps);
        trigger_fx(car, 10, 0xffff);
        if (kind != 0) mask |= DamageZoneNeighbours(zone);
        for (int z = 0; z < 16; ++z) {
            if (kind != 0 && !(mask & (1u << z))) continue;
            const float na = ee::add(c.damage_zones[z][0], dmg_a);
            c.damage_zones[z][0] = ee::lt(na, 1.0f) ? na : 1.0f;
            const float nb = ee::add(c.damage_zones[z][1], dmg_b);
            c.damage_zones[z][1] = ee::lt(nb, 2.0f) ? nb : 2.0f;
        }
        if (phys->sub_physics != 0) {
            if (car.attr_bool("IS_ALPHASUB")) {
                c.control.steering = 0.0f;
                c.control.steering_vertical = 0.0f;
                c.control.gas = 0.0f;
            } else if (AIGroundVehicle* v = nf::p32_ptr<AIGroundVehicle>(c.ai_ground_vehicle)) {
                note(0x12da78);
                AIElementController_ForceVehicleToSleep(nf::p32_u(v));
                std::memset(body->vel, 0, sizeof body->vel);
            }
        }
        return 0x10;
    }
    // O dano parcial: o estímulo pelo total, as zonas atingida (peso 1) e vizinhas (0,2; elas só com o tipo 3 ou a
    // atingida já acima de 0,75), cada uma até o limite (1, ou 0,5 com imunidade; o rasgado em dobro no tipo 3), os
    // níveis das zonas para o render (os estímulos 0x13-0x16) e os efeitos 10 (acima de 0,35) e 9 (acima de 0,75).
    const float total = ee::add(dmg_a, dmg_b);
    int32_t stim = 0;
    bool has_stim = true;
    if (ee::lt(kStim[0], total)) stim = 6;
    else if (ee::lt(kStim[1], total)) stim = 5;
    else if (ee::lt(kStim[2], total)) stim = 4;
    else if (ee::lt(kStim[3], total)) stim = 0x19;
    else has_stim = false;
    if (kind == 3 || ee::lt(0.75f, ee::add(c.damage_zones[zone][0], c.damage_zones[zone][1])))
        mask |= DamageZoneNeighbours(zone);
    const float cap_a = immunity != 0 ? 0.5f : 1.0f;
    float cap_b = immunity != 0 ? 0.5f : 1.0f;
    if (kind == 3) cap_b = ee::add(cap_b, cap_b);
    uint32_t levels[4] = {0, 0, 0, 0}, fx10 = 0, fx9 = 0, touched = 0;
    for (int z = 0; z < 16; ++z) {
        const uint32_t bit = 1u << z;
        float* zv = c.damage_zones[z];
        float mx;
        if (mask & bit) {
            const float ua = ee::lt(cap_a, zv[0]) ? zv[0] : cap_a;
            const float ub = ee::lt(cap_b, zv[1]) ? zv[1] : cap_b;
            const float w = kWeight[z == zone ? 1 : 0];
            const float na = ee::add(zv[0], ee::mul(dmg_a, w));
            zv[0] = ee::lt(na, ua) ? na : ua;
            const float nb = ee::add(zv[1], ee::mul(dmg_b, w));
            zv[1] = ee::lt(nb, ub) ? nb : ub;
            touched |= bit;
            mx = ee::lt(zv[0], zv[1]) ? zv[1] : zv[0];
            for (int i = 3; i >= 0; --i)
                if (ee::le(kLevel[i], mx)) {
                    levels[i] |= bit;
                    break;
                }
        } else {
            mx = ee::lt(zv[0], zv[1]) ? zv[1] : zv[0];
        }
        if (ee::lt(kLevel[1], mx)) fx10 |= bit;
        if (ee::lt(0.75f, mx)) fx9 |= bit;
    }
    for (int i = 0; i < 4; ++i)
        if (levels[i] != 0 && (phys->sub_physics == 0 || i + 0x13 != 0x16)) stimuli_zones(i + 0x13, levels[i], steps);
    if (has_stim && touched != 0) stimuli_zones(stim, touched, steps);
    if (fx10 != 0) trigger_fx(car, 10, fx10);
    if (fx9 != 0) trigger_fx(car, 9, fx9);
    // As bandeiras do ECollision: o som do choque (0x80 de lado num carro da classe 1, 0x70 de frente ou de trás, metade
    // das vezes), senão 0x10.
    const float r = ee::mul(ee::cvt_s(int32_t(SimRandom_Generate(sim_random()))), flt(kTwoM16));
    const uint32_t zb = 1u << zone;
    bool side = false;
    if ((zb & 0x3f18) != 0 && c.car_class == 1 && ee::lt(r, 0.5f)) side = true;
    bool front = false;
    if (!side && (zb & 0x3ca5) != 0 && ee::lt(r, 0.5f)) front = true;
    if (side) return 0x80;
    return front ? 0x70 : 0x10;
}

// [driving] 0x001876c8 PBondCar::SetVisualDamage
void PBondCar_SetVisualDamage(BondCar& car, float a_lo, float a_hi, float b_lo, float b_hi) {
    PBondCarData& c = car.d;
    const float a_span = ee::sub(a_hi, a_lo), b_span = ee::sub(b_hi, b_lo);
    if (!ee::le(0.0f, a_span) || !ee::le(0.0f, b_span)) return;
    if (!ee::le(ee::add(a_span, a_lo), 1.0f) || !ee::le(ee::add(b_span, b_lo), 1.0f)) return;
    uint32_t levels[4] = {0, 0, 0, 0}, fx10 = 0, fx9 = 0;
    RRandom& rr = render_random();
    const float k = flt(kTwoM16);
    for (int z = 0; z < 16; ++z) {
        const uint32_t bit = 1u << z;
        const float x1 = ee::cvt_s(int32_t(RRandom_Generate(rr)));
        const float x2 = ee::cvt_s(int32_t(RRandom_Generate(rr)));
        c.damage_zones[z][0] = ee::add(a_lo, ee::mul(ee::mul(x1, a_span), k));
        c.damage_zones[z][1] = ee::add(b_lo, ee::mul(ee::mul(x2, b_span), k));
        const float mx = ee::lt(c.damage_zones[z][0], c.damage_zones[z][1]) ? c.damage_zones[z][1] : c.damage_zones[z][0];
        for (int i = 3; i >= 0; --i)
            if (ee::le(kLevel[i], mx)) levels[i] |= bit;  // (sem parar: todos os níveis abaixo)
        if (ee::lt(kLevel[1], mx)) fx10 |= bit;
        if (ee::lt(0.75f, mx)) fx9 |= bit;
    }
    const int32_t steps = simulation().steps;
    for (int i = 3; i >= 0; --i)
        if (levels[i] != 0) stimuli_zones(i + 0x13, levels[i], steps);
    if (fx10 != 0) trigger_fx(car, 10, fx10);
    if (fx9 != 0) trigger_fx(car, 9, fx9);
}

// [driving] 0x0018ac28 PBondCar::AttackWithEmp
void PBondCar_AttackWithEmp(BondCar& car) {
    PBondCarData& c = car.d;
    if (c.emp_active != 0) return;
    ++g_stats.emp;
    ++g_stats.sounds;
    note(0x2f4b10, 4);  // AMenuSound::Trigger(4, ...)
    note(0x1d2bd8, 1);  // RVehicle::SetEMPVictimEffect(1)
    note(0x1d2c18);     // RVehicle::TriggerEmpBolts
    ++car.render_fx;
    c.emp_active = 1;
    c.emp_timer = simulation().steps + bond_car_tuning().emp_lifetime;
    car.set_in_shock(1.0f);  // a entrada +0x14
    if (std::strncmp("paris_bombvan", car.name(), 13) == 0) {
        note(0x208540, 0);
        note(0x206900, 0);
        if (mission_exists()) {
            SMissionManager_ProgrammerDefinedEvent(mission_manager(), 0);
            SMissionManager_IncShotsHit(mission_manager(), false);
        } else {
            ++g_stats.guards;
        }
    }
}

}  // namespace drv
