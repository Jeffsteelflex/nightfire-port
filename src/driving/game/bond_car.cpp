// O carro (driving/game/bond_car.h): o passo de física do PBondCar, portado do MIPS do DRIVING.ELF (as desmontagens em
// out/disasm_drv_car/ do checkout principal). As contas seguem a FPU do EE e a VU0 em modo macro (platform/ps2_float.h,
// platform/vu0_macro.h) na ordem das instruções.
//
// Divergências (port), cada uma também no comentário da função:
//  - o que Simulate chama e não mexe no corpo (o som do motor, as luzes, o rastro dos pneus, os efeitos, a mira, a IA)
//    não foi portado; as chamadas aparecem nos contadores (bond_car_stats);
//  - a aterrissagem forte cria um evento ECollision (o som e a câmera tremendo) que aqui só conta;
//  - os índices de piso fora das tabelas de atrito (o PS2 leria o que vem depois no .data) contam como guarda e valem 1.

#include "driving/game/bond_car.h"

#include <cctype>

#include <cstdlib>
#include <cstring>
#include <string>

#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_spawn.h"
#include "driving/ai/sim_random.h"
#include "driving/bigf.h"
#include "driving/carp.h"
#include "driving/game/attributes.h"
#include "driving/game/rail.h"
#include "driving/game/missile.h"
#include "driving/game/target_picker.h"
#include "driving/physics/collide.h"
#include "driving/physics/collision.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "driving/real/vu0_math.h"
#include "driving/render/vehicle.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

BondCarTuning g_tuning;
BondCarScratch g_scratch{};
BondCarStats g_stats;
std::function<void(BondCar& car, float rpm)> g_audio_hook;  // bond_car_audio_hook

constexpr uint32_t kOne = 0x3f800000u;
const uint32_t kVf0[4] = {0, 0, 0, kOne};

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// vmul.xz vf2, v, v; vmulax.x ACC, v, v; vmaddz.x vf2, (1), vf2; vsqrt Q; vaddq.x d, vf0, Q: o comprimento no plano xz.
float vu_length_xz(const float v[3]) {
    const uint32_t x = bits(v[0]), z = bits(v[2]);
    const uint32_t acc = ps2::mul(x, x);
    const uint32_t s = ps2::madd(acc, kOne, ps2::mul(z, z));
    return flt(ps2::add(0, ps2::sqrt(s)));
}

// O comprimento ao quadrado pelo trio vmul.xyz/vmulax.x/vmadday.x/vmaddz.x (o 1 de vf3 no x).
uint32_t vu_length_sq(const uint32_t v[3]) {
    uint32_t acc = ps2::mul(v[0], v[0]);
    acc = ps2::madd(acc, kOne, ps2::mul(v[1], v[1]));
    return ps2::madd(acc, kOne, ps2::mul(v[2], v[2]));
}

float vu_length(const float v[3]) {
    const uint32_t b[3] = {bits(v[0]), bits(v[1]), bits(v[2])};
    return flt(ps2::add(0, ps2::sqrt(vu_length_sq(b))));
}

// vrsqrt Q, vf0w, s; vmulq.xyz: o vetor unitário (só o xyz de d muda).
void vu_unit(uint32_t* d, const uint32_t* v) {
    const uint32_t q = ps2::rsqrt(kOne, vu_length_sq(v));
    d[0] = ps2::mul(v[0], q);
    d[1] = ps2::mul(v[1], q);
    d[2] = ps2::mul(v[2], q);
}

// vmul.xyz; vaddy.x; vaddz.x: o produto escalar.
float vu_dot(const float a[3], const float b[3]) {
    const uint32_t x = ps2::mul(bits(a[0]), bits(b[0]));
    const uint32_t y = ps2::mul(bits(a[1]), bits(b[1]));
    const uint32_t z = ps2::mul(bits(a[2]), bits(b[2]));
    return flt(ps2::add(ps2::add(x, y), z));
}

// vopmula.xyz ACC, a, b; vopmsub.xyz d, b, a: d = a x b (o w de d fica).
void vu_cross(uint32_t* d, const uint32_t* a, const uint32_t* b) {
    Reg acc{};
    opmula(acc, a, b);
    opmsub(d, acc, b, a);
}

// Os vetores de 16 bytes da área de rascunho e do carro como registradores.
uint32_t* R(float* p) { return reinterpret_cast<uint32_t*>(p); }
const uint32_t* R(const float* p) { return reinterpret_cast<const uint32_t*>(p); }

// O atrito e a perda de lado pelo tipo do piso (as tabelas de 0x3319d8 e 0x331a18, uma depois da outra).
float surface_friction(uint8_t type) {
    if (type < 16) return g_tuning.friction[type];
    if (type < 32) return g_tuning.lateral_loss[type - 16];
    ++g_stats.guards;
    return 1.0f;
}
float surface_lateral_loss(uint8_t type) {
    if (type < 16) return g_tuning.lateral_loss[type];
    ++g_stats.guards;
    return 1.0f;
}

// A volta das rodas de 0..1: acima de 1, menos a parte inteira (cvt.w trunca); abaixo de 0, menos (a parte - 1).
float wrap_turn(float a) {
    if (ee::lt(1.0f, a)) return ee::sub(a, ee::cvt_s(ee::cvt_w(a)));
    if (ee::lt(a, 0.0f)) return ee::sub(a, ee::cvt_s(ee::cvt_w(a) - 1));
    return a;
}

}  // namespace

BondCarTuning& bond_car_tuning() { return g_tuning; }
void reset_bond_car_tuning() { g_tuning = BondCarTuning{}; }
BondCarScratch& bond_car_scratch() { return g_scratch; }
BondCarStats& bond_car_stats() { return g_stats; }

int load_bond_car_tuning(const char* physical, const char* friction) {
    struct FloatEntry {
        const char* name;
        float BondCarTuning::*field;
    };
    struct IntEntry {
        const char* name;
        int32_t BondCarTuning::*field;
    };
    static const FloatEntry kFloats[] = {
        {"PAD_DEAD_ZONE", &BondCarTuning::pad_dead_zone},
        {"SKID_AUDIO_SCALE", &BondCarTuning::skid_audio_scale},
        {"ROLLING_RESISTANCE", &BondCarTuning::rolling_resistance},
        {"TWO_WHEEL_ANGLE", &BondCarTuning::two_wheel_angle},
        {"TWO_WHEEL_SCALE", &BondCarTuning::two_wheel_scale},
        {"TWO_WHEEL_LIMIT", &BondCarTuning::two_wheel_limit},
        {"TWO_WHEEL_OPPOSITE_SCALE", &BondCarTuning::two_wheel_opposite_scale},
        {"TWO_WHEEL_TENSOR_SCALE", &BondCarTuning::two_wheel_tensor_scale},
        {"TYRE_DAMAGE_RADIUS", &BondCarTuning::tyre_damage_radius},
        {"ENABLE_ROLL_STOPS_THRESHOLD", &BondCarTuning::enable_roll_stops_threshold},
        {"BASE_FRICTION_MASS", &BondCarTuning::base_friction_mass},
        {"SHRED_DRAG", &BondCarTuning::shred_drag},
        {"MIN_BUTTON_VALUE", &BondCarTuning::min_button_value},
        {"WHEEL_SPIN_EXTRA_RPM", &BondCarTuning::wheel_spin_extra_rpm},
        {"DAMAGE_SCALE_COLLISION", &BondCarTuning::damage_scale_collision},
        {"MAX_WHEEL_SPIN_RATE", &BondCarTuning::max_wheel_spin_rate},
        {"MAX_WHEEL_SPIN_RATE_AI", &BondCarTuning::max_wheel_spin_rate_ai},
        {"POST_BRAKE_ACCEL_SCALE", &BondCarTuning::post_brake_accel_scale},
        {"POST_BRAKE_ACCEL_MIN", &BondCarTuning::post_brake_accel_min},
    };
    static const IntEntry kInts[] = {
        {"EMP_LIFETIME", &BondCarTuning::emp_lifetime},
        {"POST_BRAKE_ACCEL_COUNT", &BondCarTuning::post_brake_accel_count},
    };
    static const char* const kSurfaces[16] = {"kNODRIVE", "kPAVED", "kGRAVEL", "kGRASS", "kCOBBLE", "kDIRT",
                                              "kWATER", "kWOOD", "kICE", "kSNOW", "kPAVED_ROUGH", nullptr,
                                              "kRAILROAD", "kMETAL", nullptr, nullptr};
    int changed = 0;
    auto each_line = [](const char* text, auto&& fn) {
        const char* p = text;
        while (p && *p) {
            const char* end = std::strchr(p, '\n');
            std::string line = end ? std::string(p, end) : std::string(p);
            p = end ? end + 1 : nullptr;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string name = line.substr(0, eq);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
            fn(name, line.c_str() + eq + 1);
        }
    };
    if (physical)
        each_line(physical, [&](const std::string& name, const char* value) {
            for (const FloatEntry& e : kFloats)
                if (name == e.name) {
                    const float v = std::strtof(value, nullptr);
                    if (bits(g_tuning.*e.field) != bits(v)) ++changed;
                    g_tuning.*e.field = v;
                }
            for (const IntEntry& e : kInts)
                if (name == e.name) {
                    // dbattrib_s8: um inteiro de 8 bits com sinal (0..10000 no editor; 120 e 160 cabem? não: 160
                    // vira -96 num s8, mas o valor é guardado numa palavra; ver o comentário do .h).
                    const int32_t v = int32_t(std::strtol(value, nullptr, 10));
                    if (g_tuning.*e.field != v) ++changed;
                    g_tuning.*e.field = v;
                }
        });
    if (friction)
        each_line(friction, [&](const std::string& name, const char* value) {
            for (int i = 0; i < 16; ++i) {
                if (!kSurfaces[i]) continue;
                const float v = std::strtof(value, nullptr);
                if (name == std::string("friction[") + kSurfaces[i] + "]") {
                    if (bits(g_tuning.friction[i]) != bits(v)) ++changed;
                    g_tuning.friction[i] = v;
                } else if (name == std::string("lateralLoss[") + kSurfaces[i] + "]") {
                    if (bits(g_tuning.lateral_loss[i]) != bits(v)) ++changed;
                    g_tuning.lateral_loss[i] = v;
                }
            }
        });
    return changed;
}

// ---------------------------------------------------------------- BondCar

BondCar::BondCar() {
    d.type = 1;
    d.physics = nf::p32_u(&car_physics);
}

RigidBody* BondCar::body() const { return GetRigidBody(simulation(), d.slot); }

void BondCar::push_action(int32_t type, float value) {
    if (action_count < int(sizeof(actions) / sizeof(actions[0]))) actions[action_count++] = {type, value};
}

float BondCar::lookup_float(const char* key) {
    if (std::strcmp(key, "CAR_WHEEL_X_OFFSET") == 0) return wheel_x_offset;
    if (std::strcmp(key, "CAR_WHEEL_ZF_OFFSET") == 0) return wheel_zf_offset;
    if (std::strcmp(key, "CAR_WHEEL_ZR_OFFSET") == 0) return wheel_zr_offset;
    if (std::strcmp(key, "MASS") == 0) return lookup_mass;
    if (std::strcmp(key, "BOOST_EXTRA_ACC") == 0) return boost_extra_acc;
    if (std::strcmp(key, "TYRE_RADIUS") == 0) return tyre_radius;
    if (std::strcmp(key, "SPRING_REST_LENGTH") == 0) return spring_rest_length_attr;
    return 0.0f;
}

void BondCar::virtual_134(float out[4]) { std::memcpy(out, &d.control, 16); }

float BondCar::hit_points() {
    // PhysicsObject::GetHitPoints (0x195a30): o float de +0x54, ou 1 sem ele.
    const float* hp = nf::p32_ptr<const float>(d.hit_point_loc);
    return hp ? *hp : 1.0f;
}

// [driving] 0x00187a78 PBondCar::SetInShock: os passos de choque (o carro do jogador 15, a classe 2 7, a 3 60) vezes o
// choque (no máximo 1), mais um fora do jogador; só se não está em choque e não é da classe 1.
void BondCar::set_in_shock(float shock) {
    PBondCarData& c = d;
    if (c.car_class == 1 || c.in_shock != 0) return;
    if (ee::lt(1.0f, shock)) shock = 1.0f;
    const int32_t steps = c.car_class == 3 ? 60 : (c.car_class == 2 ? 7 : 15);
    const int32_t v = ee::cvt_w(ee::mul(ee::cvt_s(steps), shock));
    c.in_shock = int8_t(v);
    if (c.car_class != 0) c.in_shock = int8_t(v + 1);
}

// A entrada +0x1c da tabela virtual: o PBondCar::ApplyDamage (0x00189598, driving/game/bond_car_damage.cpp).
int32_t BondCar::apply_damage(const float* from, const float* to, float amount, float split, int32_t kind) {
    ++damage_calls;
    damage_total += amount;
    return PBondCar_ApplyDamage(*this, from, to, amount, split, kind);
}

// [driving] 0x0018ad28 PBondCar::GetSplinePath: o trilho do AIGroundVehicle (+0x2e4; AIVehicle::GetSplinePath, +0x50).
uint32_t BondCar::spline_path() {
    if (const AIGroundVehicle* v = nf::p32_ptr<const AIGroundVehicle>(d.ai_ground_vehicle)) return v->spline_path;
    return 0;
}

// [driving] 0x00187608 PBondCar::AddDamageByPlayer: o dano soma em +0x370 e o relógio (+0x374) vai à taxa de quadros;
// na classe 3 ou num carro de polícia, acima de 499 ou da vida, o evento 2 da missão.
void BondCar::add_damage_by_player(float damage) {
    PBondCarData& c = d;
    c.damage_by_player = ee::add(c.damage_by_player, damage);
    c.damage_by_player_timer = uint8_t(simulation().frame_rate);
    if (c.car_class != 3 && !is_police) return;
    if (ee::lt(499.0f, c.damage_by_player) || ee::lt(hit_points(), c.damage_by_player)) {
        ++mission_events;
        physics_pending("SMissionManager::ProgrammerDefinedEvent(2)");
    }
}

// [driving] 0x001931f8 PBondCar::IsWheelOnGround
bool PBondCar_IsWheelOnGround(const BondCar& car, int wheel) { return !ee::eq(car.d.suspension[wheel], 0.0f); }

// [driving] 0x002104a8 Util_Step
float Util_Step(float cur, float target, float step) {
    if (ee::lt(target, cur)) {
        const float f = ee::sub(cur, step);
        return ee::lt(f, target) ? target : f;
    }
    const float f = ee::add(cur, step);
    return ee::lt(target, f) ? target : f;
}

// [driving] 0x0018aa88 PBondCar::CalculateRPM
float PBondCar_CalculateRPM(BondCar& car) {
    PBondCarData& c = car.d;
    RigidBody* body = car.body();
    const float speed = vu_length_xz(body->vel);
    const CarPhysics* ph = car.physics();
    if (ph->sub_physics == 1) return ee::mul(speed, 100.0f);
    float rpm;
    if (c.num_wheels_on_ground == 0) {
        rpm = ee::mul(c.control.gas, 7000.0f);
    } else {
        float limit;
        float f0;
        if (c.control.gear != 0) {
            f0 = ee::mul(speed, 6000.0f);
            limit = 20.0f;
            c.gear = 0;
        } else {
            c.gear = 1;
            limit = ph->gear_limit[1];
            if (!ee::lt(speed, limit)) {
                for (;;) {
                    c.gear = int8_t(uint8_t(c.gear) + 1);
                    if (c.gear >= 5) break;
                    limit = car.physics()->gear_limit[c.gear];
                    if (ee::lt(speed, limit)) break;
                }
            }
            if (c.gear < c.previous_gear && c.gear_change_timer != 0) {
                c.gear = c.previous_gear;
                limit = car.physics()->gear_limit[c.previous_gear];
            }
            f0 = ee::mul(speed, 6000.0f);
        }
        rpm = ee::div(f0, limit);
    }
    return ee::add(rpm, ee::mul(g_tuning.wheel_spin_extra_rpm, c.wheel_spin_flag));
}

// [driving] 0x0018cef8 PBondCar::ProcessPhysics
void PBondCar_ProcessPhysics(BondCar& car) {
    PBondCarData& c = car.d;
    RigidBody* body = car.body();
    BondCarScratch& s = g_scratch;
    s.front_grip = 0.5f;
    s.info = nf::p32_u(nf::raw(body->info));
    std::memcpy(s.velocity, body->vel, 12);
    RigidBodyInfo* info = body->info;
    std::memcpy(s.forward, info->orient[2], 12);
    std::memcpy(s.steered_forward, s.forward, 12);
    s.handbrake_on = 0;
    c.wheel_spin_flag = 0.0f;
    c.car_speed = vu_length_xz(body->vel);
    if (!ee::eq(c.two_wheel_timer, 1.0f) || c.tensor_dirty != 0) {
        // O tensor do carro de novo, com o z escalado pelo tempo das duas rodas.
        float t[3];
        Util_GenerateCarTensor(t, body->mass, info->half_size[0], info->half_size[1], info->half_size[2]);
        t[2] = ee::mul(t[2], c.two_wheel_timer);
        body->inv_inertia_x = t[0];
        body->inv_inertia_y = t[1];
        body->inv_inertia_z = t[2];
        c.tensor_dirty = 0;
    }
    s.velocity[1] = 0.0f;
    s.forward_speed = vu_dot(s.velocity, s.forward);
    {
        // vmulx.xyz: a velocidade (y zerado) vezes ROLLING_RESISTANCE.
        const uint32_t k = bits(g_tuning.rolling_resistance);
        for (int i = 0; i < 3; ++i) R(s.rolling_resistance)[i] = ps2::mul(R(s.velocity)[i], k);
    }
    const CarPhysics* ph = car.physics();
    c.braking_at_speed = 0;
    c.reversing = 0;
    if (ee::eq(c.control.brake, 0.0f)) {
        c.reverse_timer = 30;
    } else if (ee::lt(s.forward_speed, 2.5f)) {
        if (c.reverse_timer < 30) {
            c.reverse_timer = int8_t(uint8_t(c.reverse_timer) + 1);
            if (ee::lt(s.forward_speed, 0.5f)) c.control.brake = 0.0f;
        } else if (ee::le(s.forward_speed, 0.0f)) {
            s.front_grip = ph->reverse_frw_ratio;
            c.drive_state = 3;
            c.reversing = 1;
        }
    } else {
        c.reverse_timer = 0;
        c.braking_at_speed = 1;
    }
    if (c.reversing == 0) {
        const float min_button = g_tuning.min_button_value;
        if (ee::lt(min_button, c.control.brake)) {
            s.front_grip = ph->brake_frw_ratio;
            c.drive_state = 2;
        } else if (ee::lt(min_button, c.control.gas)) {
            s.front_grip = ph->accel_frw_ratio;
            c.drive_state = 1;
        } else {
            s.front_grip = ph->coast_frw_ratio;
            c.drive_state = 0;
        }
    }
    s.handbrake = 0.0f;
    s.rear_grip = ee::sub(1.0f, s.front_grip);
    if (c.control.hand_brake != 0) {
        if (ee::lt(c.car_speed, 1.0f)) {
            if (ee::lt(ps2::ffloat(0x3f34fdf4u), info->orient[1][1])) {  // 0,707
                body->momentum[0] = 0.0f;
                body->momentum[2] = 0.0f;
                s.handbrake = 0.0f;
            } else {
                s.handbrake = ee::add(ee::add(c.car_speed, c.car_speed), 1.0f);
            }
        } else {
            s.handbrake = ph->handbrake_force;
            s.handbrake_on = 1;
        }
        int8_t state;
        float rear;
        if (c.handbrake_reverse == 0) {
            state = 4;
            const float share = ph->handbrake_frw_ratio;
            s.front_grip = share;
            rear = ee::mul(ee::sub(1.0f, share), ph->handbrake_rw_fric_scale);
            s.rear_grip = rear;
            s.front_grip = ee::mul(share, ph->total_handbrake_friction_scale);
        } else {
            state = 5;
            const float share = ee::mul(ph->rev_handbrake_frw_ratio, 0.5f);
            rear = ee::mul(ee::sub(1.0f, share), 0.5f);
            s.front_grip = share;
            s.rear_grip = rear;
            s.front_grip = ee::mul(share, ph->total_handbrake_friction_scale);
        }
        s.rear_grip = ee::mul(rear, ph->total_handbrake_friction_scale);
        c.drive_state = state;
        if (ee::lt(c.car_speed, 5.0f)) {
            s.front_grip = 0.5f;
            s.rear_grip = 0.5f;
        }
    }
    if (c.drive_state == 2) {
        s.front_grip = ee::mul(s.front_grip, ph->braking_friction);
        s.rear_grip = ee::mul(s.rear_grip, ph->braking_friction);
    }
    s.wheel_spin = c.wheel_spin_enable != 0 ? uint8_t(c.control.hand_brake == 0) : 0;
    if (c.drive_state == 1 && ee::lt(c.car_wheel_speed, 0.0f)) {
        if (ph->is_rally != 0) {
            const float g = ee::min(1.0f, ee::add(ph->braking_friction, ph->braking_friction));
            s.front_grip = ee::mul(s.front_grip, g);
        }
        const float g = ee::min(1.0f, ee::add(ph->braking_friction, ph->braking_friction));
        s.rear_grip = ee::mul(s.rear_grip, g);
        if (ee::lt(0.75f, c.control.gas)) s.wheel_spin = uint8_t(c.drive_state);
    }
    // O contraesterço: o giro contra a direção alarga o limite de atrito de trás.
    c.counter_steering = 0;
    s.counter_steer = 1.0f;
    if (!ee::eq(ph->counter_steer_scale, 0.0f)) {
        const float wy = body->ang_vel[1];
        if (ee::lt(0.0f, wy) && ee::lt(c.control.steering, 0.0f)) c.counter_steering = 1;
        else if (ee::lt(wy, 0.0f) && ee::lt(0.0f, c.control.steering)) c.counter_steering = 1;
        if (c.counter_steering != 0) {
            float k = ee::mul(ee::mul(ee::abs(wy), ph->counter_steer_scale), ee::abs(c.control.steering));
            s.counter_steer = k;
            if (c.drive_state == 1) {
                k = ee::mul(k, ps2::ffloat(0x3f99999au));  // 1,2
                s.counter_steer = k;
            }
            s.counter_steer = ee::max(ee::min(s.counter_steer, ph->counter_steer_scale_limit), 1.0f);
        }
    }
    // O turbo.
    s.boost_acc = 0.0f;
    s.max_acc = ph->max_acc;
    if (c.rocket_boost != 0) {
        const int32_t boost_end = car.boost_time * 3;  // AttributeSet::LookupInt("BOOST_TIME")
        c.rocket_boost -= 1;
        if (boost_end < c.rocket_boost) s.boost_acc = car.boost_extra_acc;  // LookupFloat("BOOST_EXTRA_ACC")
        if (c.rocket_boost == boost_end) car.audio_flags |= 1;  // GetAudio()->+0x160 = 0 (o som do turbo acaba)
    }
    // A velocidade das rodas, a tração e os freios.
    const float vx = s.velocity[0], vz = s.velocity[2];
    s.max_rev_acc = ph->max_rev_acc;
    const float speed_xz = ee::sqrt(ee::add(ee::mul(vx, vx), ee::mul(vz, vz)));
    s.wheel_speed = speed_xz;
    {
        const float rate = ee::cvt_s(simulation().frame_rate);
        const float turn = ee::mul(ee::mul(ph->wheel_radius, ps2::ffloat(0x40ca3d71u)), rate);  // 6,32
        s.wheel_speed = ee::div(speed_xz, turn);
        if (ee::lt(s.wheel_speed, 0.01f) && c.control.hand_brake != 0) s.wheel_speed = 0.0f;
        s.wheel_speed = ee::min(s.wheel_speed, 1.0f);
    }
    if (c.reversing != 0) {
        const float over = ee::sub(c.car_speed, ph->speed_cutoff_reverse);
        s.slope = over;
        if (ee::lt(0.0f, over)) {
            const float acc = ee::sub(s.max_rev_acc, ee::mul(over, ph->speed_cutoff_rate_reverse));
            s.max_rev_acc = acc;
            if (ee::lt(acc, 0.0f)) s.max_rev_acc = 0.0f;
        }
    }
    if (c.gear != c.previous_gear) {
        c.previous_gear = c.gear;
        c.gear_change_timer = 15;
    }
    s.max_acc = ee::mul(s.max_acc, car.physics()->gear_ratio[c.gear]);
    if (c.reversing != 0 && c.rocket_boost == 0) {
        const float gas = ee::mul(c.control.gas, s.max_acc);
        const float brake = ee::mul(c.control.brake, s.max_rev_acc);
        s.drive_force = ee::sub(ee::add(s.boost_acc, gas), brake);
    } else {
        const float gas = ee::mul(c.control.gas, s.max_acc);
        const float brake = ee::mul(c.control.brake, ph->max_brake);
        s.drive_force = ee::sub(ee::add(s.boost_acc, gas), brake);
    }
    const RigidBodyInfo* sinfo = nf::p32_ptr<const RigidBodyInfo>(s.info);
    if (c.num_wheels_on_ground != 0 && ee::lt(0.1f, sinfo->orient[1][1])) {
        const float spin = ee::min(s.wheel_speed, g_tuning.max_wheel_spin_rate);
        c.car_wheel_speed = ee::lt(s.forward_speed, 0.0f) ? ee::neg(spin) : spin;
    } else if (c.braking_at_speed != 0) {
        c.car_wheel_speed = ee::mul(c.car_wheel_speed, 0.25f);
    } else {
        c.car_wheel_speed = ee::mul(c.car_wheel_speed, ps2::ffloat(0x3f733333u));  // 0,95
    }
    // A direção, menor acima de MODSTEERSPEED (até MINSTEER).
    s.scale = 1.0f;
    if (ee::lt(ph->mod_steer_speed, c.car_speed)) {
        const float over = ee::div(ee::sub(c.car_speed, ph->mod_steer_speed), 25.0f);
        s.scale = ee::sub(1.0f, ee::min(1.0f, over));
    }
    s.scale = ee::max(s.scale, ph->min_steer);
    const float steer = ee::mul(ee::mul(c.control.steering, ph->max_steering), s.scale);
    c.car_steer = steer;
    VU0_MATRIX4setyrot(s.steering_turn, steer);
    VU0_MATRIX4_mult(nf::p32_ptr<RigidBodyInfo>(s.info)->orient, s.steering_turn, s.steered_orient);
    std::memcpy(s.steered_forward, s.steered_orient[2], 12);
    if (c.against_wall != 0 && c.num_wheels_on_ground == 4) {
        float turn[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        turn[1] = ee::mul(ee::mul(ee::mul(c.control.steering, c.control.gas), body->mass), 0.5f);
        ModifyAngularMomentum(body, turn);
    }
    // As entradas das rodas.
    std::memcpy(s.position, body->pos, 12);
    std::memcpy(s.rolling_resistance4, s.rolling_resistance, 12);
    s.rolling_resistance4[3] = 0.0f;  // Float_COORD3toCOORD4 (0x17b8b0): w = 0
    s.wheel_slip[3] = s.wheel_slip[2] = s.wheel_slip[1] = s.wheel_slip[0] = 0.0f;
    c.landing_impact = 0.0f;
    std::memcpy(s.wheel_heading[0], s.steered_forward, 12);
    std::memcpy(s.wheel_heading[1], s.forward, 12);
    float rear_limit;
    if (c.rocket_boost != 0) {
        const int32_t boost_end = car.boost_time * 3;
        if (boost_end < c.rocket_boost) {
            s.boost_grip = 3.0f;
        } else {
            float f = ee::div(ee::cvt_s(c.rocket_boost), ee::cvt_s(boost_end));
            f = ee::add(ee::add(f, f), 1.0f);
            s.boost_grip = f;
        }
        const float front = ee::mul(car.physics()->friction_limit_front, s.boost_grip);
        s.wheel_input[0].friction_limit = front;
        s.wheel_input[1].friction_limit = front;
        rear_limit = ee::mul(ee::mul(car.physics()->friction_limit_rear, s.boost_grip), s.counter_steer);
    } else {
        const float front = car.physics()->friction_limit_front;
        s.wheel_input[0].friction_limit = front;
        s.wheel_input[1].friction_limit = front;
        rear_limit = ee::mul(car.physics()->friction_limit_rear, s.counter_steer);
    }
    s.wheel_input[2].friction_limit = rear_limit;
    s.wheel_input[3].friction_limit = rear_limit;
    for (int w = 0; w < 4; ++w) {
        BondCarWheelInput& in = s.wheel_input[w];
        std::memcpy(c.wheel_pos[w], body->info->lever_world[w], 16);
        for (int i = 0; i < 3; ++i) R(c.wheel_pos[w])[i] = ps2::add(R(c.wheel_pos[w])[i], R(s.position)[i]);
        TempGetHeightInformation(body, c.wheel_pos[w], c.wheel_road_normal[w], &c.wheels[w]);
        const CarPhysics* p = car.physics();
        if (w < 2) {
            in.grip = ee::mul(s.front_grip, p->tyre_grip_factor);
            if (!ee::le(0.0f, s.drive_force)) in.drive = s.drive_force;
            else in.drive = p->is_rally == 0 ? 0.0f : s.drive_force;
        } else {
            in.grip = ee::mul(ee::mul(s.rear_grip, p->tyre_grip_factor), s.counter_steer);
            if (s.wheel_spin != 0) in.drive = ee::mul(s.drive_force, p->ws_accel);
            else in.drive = ee::le(0.0f, s.drive_force) ? s.drive_force : 0.0f;
        }
        in.drive = ee::mul(in.drive, in.grip);
        in.handbrake = s.handbrake;
        const float fr = surface_friction(c.wheels[w].surface[0]);
        in.friction_limit = ee::mul(in.friction_limit, fr);
        in.drive = ee::mul(in.drive, fr);
        if (c.oil_slick != 0 && w >= 2) {
            in.friction_limit = ee::mul(in.friction_limit, 0.1f);
            in.drive = ee::mul(in.drive, 0.75f);
        }
    }
    // A aderência que cai acima de FRIC_MOD_SPEED, até FRIC_MOD_MIN.
    if (ee::lt(car.physics()->fric_mod_speed, c.car_speed)) {
        const CarPhysics* p = car.physics();
        const float over = ee::sub(c.car_speed, p->fric_mod_speed);
        s.scale = over;
        if (ee::lt(p->fric_mod_range, over)) s.scale = p->fric_mod_range;
        const float k = ee::sub(1.0f, ee::div(s.scale, p->fric_mod_range));
        s.scale = k;
        if (ee::lt(k, p->fric_mod_min)) s.scale = p->fric_mod_min;
    } else {
        s.scale = 1.0f;
    }
    c.grip_scale = s.scale;
    c.num_wheels_on_ground = PBondCar_AddWheelForces(car, s.wheel_input, s.wheel_slip, s.wheel_heading,
                                                     c.wheel_road_normal, c.wheel_pos, s.rolling_resistance4,
                                                     s.wheel_spin != 0);
    // Parado no freio de mão: parado de vez (salvo a queda).
    if (c.num_wheels_on_ground == 4 && c.control.hand_brake != 0 && ee::lt(c.control.gas, 0.1f) &&
        ee::lt(vu_length(body->vel), ps2::ffloat(0x3da3d70au))) {  // 0,08
        body->vel[0] = 0.0f;
        body->vel[2] = 0.0f;
        const uint32_t m = bits(body->mass);
        const float v[3] = {0.0f, body->vel[1], 0.0f};
        for (int i = 0; i < 3; ++i) R(body->momentum)[i] = ps2::mul(R(v)[i], m);
        body->force[0] = 0.0f;
        body->force[2] = 0.0f;
        body->torque[1] = 0.0f;
        static const float kZero[4] = {0, 0, 0, 0};  // kZeroCoord (0x355b80)
        SetAngularMomentum(body, kZero);
    }
    for (int w = 0; w < 4; ++w)
        c.wheel_pos[w][1] = ee::add(c.wheel_pos[w][1], ee::add(c.wheel_road_normal[w][3], 0.01f));
    for (int w = 0; w < 4; ++w) {
        const float slip = ee::mul(ee::sub(s.wheel_slip[w], 0.3f), ps2::ffloat(0x3fb6db6eu));  // / 0,7
        c.wheel_slip[w] = slip;
        if (ee::lt(slip, 0.0f)) c.wheel_slip[w] = 0.0f;
    }
    if (c.car_class == 0) {
        const RigidBodyInfo* bi = body->info;
        if (bi->flags[1] == 0 && uint32_t(g_tuning.landing_air_steps) < uint32_t(bi->age) &&
            c.num_wheels_on_ground == 0 && body->ground_levers == 0)
            PBondCar_ImproveLanding(car);
        if (c.two_wheel_mode != 0) PBondCar_TwoWheelStunt(car);
        if (ee::lt(0.01f, c.landing_impact)) {
            // Um ECollision (0x58 bytes) com a força da aterrissagem (até 1) na posição do corpo: o som e a vibração
            // (collide_events().collision, como os choques). A normal (0, 1, 0), a velocidade zero, as bandeiras 0xe
            // (o PlayLanding do som), o tipo 7, a etiqueta 1; os w e o +0x38 são o que estava na pilha do PS2 (0 aqui).
            c.landing_impact = ee::min(c.landing_impact, 1.0f);
            ++g_stats.landing_impacts;
            CollisionInfo ci{};
            std::memcpy(ci.point, s.position, 12);
            ci.point[3] = s.rear_grip;
            ci.normal[1] = 1.0f;
            ci.speed = c.landing_impact;
            ci.strength = c.landing_impact;
            ci.kind = uint8_t(body->type);
            ci.slot = body->slot;
            ci.flags = 0xe;
            ci.type = 7;
            ci.unknown_4d = 0;
            ci.tag = 1;
            ci.other = -1;
            const CollideEvents& ce = collide_events();
            if (ce.collision) ce.collision(ci);
        }
    }
    // O giro das rodas.
    if (c.braking_at_speed == 0 || ee::le(c.control.brake, 0.5f)) {
        if (s.wheel_spin != 0) {
            c.wheel_spin_angle[0] = ee::add(c.wheel_spin_angle[0], ee::mul(c.car_wheel_speed, 0.1f));
        } else {
            c.wheel_spin_angle[0] = ee::add(c.wheel_spin_angle[0], c.car_wheel_speed);
            if (c.control.hand_brake == 0) c.wheel_spin_angle[1] = ee::add(c.wheel_spin_angle[1], c.car_wheel_speed);
        }
    }
    if (s.wheel_spin != 0) c.wheel_spin_angle[1] = ee::add(c.wheel_spin_angle[1], g_tuning.max_wheel_spin_rate);
    c.wheel_spin_angle[0] = wrap_turn(c.wheel_spin_angle[0]);
    c.wheel_spin_angle[1] = wrap_turn(c.wheel_spin_angle[1]);
    if (s.wheel_spin != 0) {
        c.wheel_spin_flag = 1.0f;
        c.wheel_slip[3] = 1.0f;
        c.wheel_slip[2] = 1.0f;
    }
    // A aterrissagem: a poeira (TriggerFX do render) e as rodas derrapando.
    if (c.num_wheels_on_ground != 0 && c.landing_fx_timer == 0 && c.landing_flag >= 16) {
        c.landing_fx_timer = uint8_t(c.landing_flag >> 1);
        ++g_stats.landing_fx;  // o centro das rodas + 0,2 em y, a velocidade, o efeito 6/13 (render)
    }
    if (c.landing_fx_timer != 0) {
        c.landing_fx_timer = uint8_t(c.landing_fx_timer - 1);
        c.skidding = 1;
        const float k = ps2::ffloat(0x3f733333u);  // 0,95
        c.wheel_slip[0] = k;
        c.wheel_slip[3] = k;
        c.wheel_slip[2] = k;
        c.wheel_slip[1] = k;
    } else {
        c.skidding = 0;
    }
    if (s.handbrake_on != 0 && ee::lt(0.5f, c.car_speed)) {
        c.skidding = 1;
        c.wheel_slip[2] = 1.0f;
        c.wheel_slip[3] = 1.0f;
    }
    if (ee::lt(ps2::ffloat(0x3ea8f5c3u), c.control.brake) && c.reversing == 0) {  // 0,33
        const float half = ee::mul(c.control.brake, 0.5f);
        c.skidding = 1;
        for (int w = 0; w < 4; ++w) c.wheel_slip[w] = ee::max(half, c.wheel_slip[w]);
    }
    if (c.num_wheels_on_ground != 0) c.landing_flag = 0;
    else if (c.landing_flag < 255) c.landing_flag = uint8_t(c.landing_flag + 1);
    if (c.gear_change_timer != 0) c.gear_change_timer = uint8_t(c.gear_change_timer - 1);
}

// [driving] 0x0018c1b0 PBondCar::AddWheelForces
int8_t PBondCar_AddWheelForces(BondCar& car, BondCarWheelInput* input, float* slips, const float (*headings)[4],
                               float (*road_normals)[4], float (*wheel_pos)[4], const float* rolling_resistance,
                               bool wheel_spin) {
    PBondCarData& c = car.d;
    RigidBody* body = car.body();
    BondCarScratch& s = g_scratch;
    s.wheels_on_ground = 0;
    s.force_info = nf::p32_u(nf::raw(body->info));
    s.lift_above_ground = 0.0f;
    std::memcpy(s.heading, body->info->orient[2], 12);
    s.heading[1] = 0.0f;
    vu_unit(R(s.heading), R(s.heading));
    const float* heading = headings[0];
    for (int w = 0; w < 4; ++w) {
        BondCarWheelInput& in = input[w];
        const float* normal = road_normals[w];
        s.limited = 0;
        std::memcpy(s.lever, wheel_pos[w], 16);
        // A mola.
        s.compression = ee::add(normal[3], car.physics()->spring_rest_length);
        if (c.gear > 0 && c.gear_change_timer != 0 && w < 2 && c.two_wheel_mode == 0 && c.num_wheels_on_ground == 4 &&
            ee::lt(0.02f, s.compression))
            s.compression = ee::sub(s.compression, 0.02f);
        if (!PBondCar_IsWheelOnGround(car, w)) s.lift_above_ground = ee::max(s.lift_above_ground, s.compression);
        const float limited = ee::min(s.compression, car.physics()->spring_compression_limit);
        s.compression = limited;
        if (ee::lt(limited, 0.0f)) s.compression = 0.0f;
        bool on_ground = ee::lt(0.0f, s.compression) || ee::lt(0.0f, c.suspension[w]);
        s.on_ground = on_ground ? 1 : 0;
        if (on_ground) {
            s.wheels_on_ground += 1;
        } else if (c.two_wheel_mode == 0) {
            s.on_ground = c.num_wheels_on_ground > 0 ? 1 : 0;
        }
        const RigidBodyInfo* finfo = nf::p32_ptr<const RigidBodyInfo>(s.force_info);
        if (s.on_ground != 0 && ee::lt(g_tuning.enable_roll_stops_threshold, ee::mul(finfo->orient[1][1], normal[1]))) {
            std::memset(s.force, 0, 16);
            const CarPhysics* ph = car.physics();
            if (ee::lt(0.0f, s.compression)) {
                const float change = ee::sub(s.compression, c.suspension[w]);
                s.heading[3] = change;  // compression_change (+0x2fc)
                const float damping = w < 2 ? ph->spring_damping_front : ph->spring_damping_rear;
                const float stiffness = w < 2 ? ph->spring_stiffness_front : ph->spring_stiffness_rear;
                s.ground_normal[3] = ee::add(ee::mul(s.compression, stiffness), ee::mul(change, damping));
                if (!PBondCar_IsWheelOnGround(car, w))
                    c.landing_impact = ee::max(ee::mul(s.compression, 5.0f), c.landing_impact);  // 0x396330: 5
            } else {
                s.ground_normal[3] = 0.0f;  // spring_force (+0x30c)
            }
            // O referencial da roda no chão e a velocidade do corpo nela.
            Reg nv;
            load(nv, normal);
            vu_cross(R(s.axle), R(heading), nv);
            vu_unit(R(s.axle), R(s.axle));
            vu_cross(R(s.rolling), nv, R(s.axle));
            vu_unit(R(s.rolling), R(s.rolling));
            vu_cross(R(s.lateral), nv, R(s.rolling));
            s.lateral[1] = 0.0f;
            vu_unit(R(s.lateral), R(s.lateral));
            s.axle[2] = 0.0f;
            s.axle[1] = 0.0f;
            s.axle[0] = 0.0f;
            for (int i = 0; i < 3; ++i) R(s.lever)[i] = ps2::sub(R(s.lever)[i], R(body->pos)[i]);
            vu_cross(R(s.point_velocity), R(body->ang_vel), R(s.lever));
            for (int i = 0; i < 3; ++i)
                R(s.point_velocity)[i] = ps2::add(R(s.point_velocity)[i], R(body->vel)[i]);
            if (!ee::eq(in.handbrake, 0.0f) && w >= 2) {
                vu_unit(R(s.lateral), R(s.point_velocity));
                for (int i = 0; i < 3; ++i) R(s.lateral)[i] = ps2::sub(0, R(s.lateral)[i]);  // vsub.xyz vf0 - v
            }
            if (ph->is_rally != 0) {
                const float k = ee::max(1.0f, ee::sub(5.0f, c.car_speed));
                in.friction_limit = ee::mul(in.friction_limit, k);
                in.grip = ee::mul(in.grip, k);
            }
            // A aderência de lado, até o limite de atrito.
            s.lateral_force = ee::mul(vu_dot(s.point_velocity, s.lateral), in.grip);
            s.lateral_force = ee::mul(s.lateral_force, c.grip_scale);
            const uint8_t surface = c.wheels[w].surface[0];
            if (surface == 8) {  // kICE
                if (ee::lt(in.drive, 0.0f) || !ee::eq(in.handbrake, 0.0f)) {
                    in.drive = ee::mul(in.drive, 0.5f);
                    in.handbrake = ee::mul(in.handbrake, 0.5f);
                }
                in.friction_limit = ee::mul(in.friction_limit, ps2::ffloat(0x3f19999au));  // 0,6
            }
            if (ee::lt(in.friction_limit, ee::abs(s.lateral_force))) {
                s.spin[3] = ee::sub(ee::abs(s.lateral_force), in.friction_limit);  // lateral_excess (+0x31c)
                s.lateral_force = ee::lt(s.lateral_force, 0.0f) ? ee::neg(in.friction_limit) : in.friction_limit;
                s.limited = 1;
            }
            if (wheel_spin && w >= 2) {
                const float k = car.physics()->wheel_spin_scale;
                s.lateral_force = ee::mul(s.lateral_force, k);
                for (int i = 0; i < 3; ++i) R(s.rolling)[i] = ps2::mul(R(s.rolling)[i], bits(k));
            }
            // A tração, com a roda ou a outra do eixo no chão.
            if (PBondCar_IsWheelOnGround(car, w) || PBondCar_IsWheelOnGround(car, w ^ 1)) {
                for (int i = 0; i < 3; ++i) R(s.rolling)[i] = ps2::mul(R(s.rolling)[i], bits(in.drive));
                for (int i = 0; i < 3; ++i) R(s.force)[i] = ps2::add(R(s.force)[i], R(s.rolling)[i]);
            }
            const RigidBodyInfo* bi = body->info;
            if (bi->flags[1] != 0 || bi->flags[2] != 0) s.lever[1] = 0.0f;
            else s.lever[1] = ee::mul(s.lever[1], car.physics()->wheel_force_app_scale);
            if (s.limited != 0 && !wheel_spin) {
                s.slip_fraction = ee::min(ee::div(s.spin[3], in.friction_limit), 1.0f);
                const CarPhysics* p = car.physics();
                const float slipping = w < 2 ? p->front_slipping_scale : p->rear_slipping_scale;
                const float k = ee::sub(1.0f, ee::mul(ee::sub(1.0f, slipping), s.slip_fraction));
                s.lateral_force = ee::mul(s.lateral_force, k);
            }
            for (int i = 0; i < 3; ++i) R(s.lateral)[i] = ps2::mul(R(s.lateral)[i], bits(s.lateral_force));
            // A mola ao longo da normal do chão, menor nas rampas suaves.
            for (int i = 0; i < 3; ++i) R(s.axle)[i] = ps2::mul(R(normal)[i], bits(s.ground_normal[3]));
            s.ground_normal[0] = normal[0];
            s.ground_normal[1] = 0.0f;
            s.ground_normal[2] = normal[2];
            vu_unit(R(s.ground_normal), R(s.ground_normal));
            float slope = ee::abs(vu_dot(s.ground_normal, s.heading));
            s.slope = slope;
            if (!ee::lt(ps2::ffloat(0x3f34fdf4u), slope)) slope = ee::mul(slope, 0.2f);  // 0,707
            slope = ee::max(ps2::ffloat(0x3d4ccccdu), slope);                           // 0,05
            s.slope = slope;
            const CarPhysics* p = car.physics();
            if (!(p->is_rally != 0 && ee::lt(c.car_speed, 5.0f) && !ee::eq(in.handbrake, 0.0f))) {
                s.axle[0] = ee::mul(s.axle[0], ee::mul(p->slope_scale, slope));
                s.axle[2] = ee::mul(s.axle[2], ee::mul(car.physics()->slope_scale, slope));
            }
            if (!ee::eq(in.handbrake, 0.0f)) {
                vu_unit(R(s.drag), R(s.point_velocity));
                for (int i = 0; i < 3; ++i) R(s.drag)[i] = ps2::mul(R(s.drag)[i], bits(in.handbrake));
                for (int i = 0; i < 3; ++i) R(s.axle)[i] = ps2::sub(R(s.axle)[i], R(s.drag)[i]);
            }
            if (ee::eq(in.handbrake, 0.0f) || ee::lt(1.0f, c.car_speed))
                for (int i = 0; i < 3; ++i) R(s.axle)[i] = ps2::sub(R(s.axle)[i], R(s.lateral)[i]);
            for (int i = 0; i < 3; ++i) R(s.axle)[i] = ps2::sub(R(s.axle)[i], R(rolling_resistance)[i]);
            for (int i = 0; i < 3; ++i) R(s.force)[i] = ps2::add(R(s.force)[i], R(s.axle)[i]);
            slips[w] = ee::min(1.0f, ee::div(ee::abs(s.lateral_force), in.friction_limit));
            // Rápido, a perda de lado do piso deixa o carro escorregar: a força de lado do corpo cortada.
            if (ee::lt(10.0f, c.car_speed)) {
                float loss = surface_lateral_loss(surface);
                if (!ee::eq(loss, 1.0f)) {
                    if (w < 2) loss = ee::add(loss, ee::mul(ee::sub(1.0f, loss), 0.5f));
                    if (ee::lt(ee::mul(loss, in.friction_limit), ee::abs(s.lateral_force))) {
                        ConvertWorldToLocal(body, s.force);
                        const float held = ee::div(ee::mul(loss, in.friction_limit), ee::abs(s.lateral_force));
                        s.force[0] = ee::mul(s.force[0], held);
                        ConvertLocalToWorld(body, s.force);
                    }
                }
            }
            vu_cross(R(s.torque), R(s.lever), R(s.force));
            ResolveMassScaledForce4(body, s.force);
            ResolveMassScaledTorque4(body, s.torque);
        }
        if (ee::eq(s.compression, 0.0f)) {
            if (c.two_wheel_mode == 0 && c.num_wheels_on_ground != 0 &&
                ee::lt(g_tuning.enable_roll_stops_threshold, ee::mul(finfo->orient[1][1], normal[1])) &&
                c.in_shock == 0 && body->info->flags[2] == 0 && body->info->flags[1] == 0) {
                std::memcpy(s.lever, wheel_pos[w], 16);
                for (int i = 0; i < 3; ++i) R(s.lever)[i] = ps2::sub(R(s.lever)[i], R(body->pos)[i]);
                vu_cross(R(s.spin), R(body->ang_vel), R(s.lever));
                if (ee::lt(0.0f, s.spin[1])) {
                    s.force[2] = 0.0f;
                    s.force[1] = -50.0f;
                    s.force[0] = 0.0f;
                    vu_cross(R(s.torque), R(s.lever), R(s.force));
                    ResolveMassScaledTorque4(body, s.torque);
                }
            }
            slips[w] = 0.0f;
        }
        c.suspension[w] = s.compression;
        if (w == 1) heading = headings[1];
    }
    body->pos[1] = ee::add(body->pos[1], s.lift_above_ground);
    if (!ee::eq(car.physics()->yaw_stability_factor, 0.0f)) {
        float local[3];
        GetLocalAngularVelocity(body, local);
        float yaw = ee::mul(ee::neg(local[1]), car.physics()->yaw_stability_factor);
        if (c.counter_steering != 0) yaw = ee::mul(yaw, 8.0f);
        s.axle[1] = yaw;
        s.axle[0] = 0.0f;
        s.axle[2] = 0.0f;
        ConvertLocalToWorld(body, s.axle);
        ResolveMassScaledTorque(body, s.axle);
    }
    return int8_t(s.wheels_on_ground);
}

// [driving] 0x0018bb90 PBondCar::ImproveLanding
void PBondCar_ImproveLanding(BondCar& car) {
    RigidBody* body = car.body();
    BondCarScratch& s = g_scratch;
    const RigidBodyInfo* finfo = nf::p32_ptr<const RigidBodyInfo>(s.force_info);
    if (ee::lt(finfo->orient[1][1], ps2::ffloat(0x3ecccccdu))) return;  // 0,4
    s.up[0] = 0.0f;
    s.up[1] = 1.0f;
    s.up[2] = 0.0f;
    // A orientação nivelada: a linha 2 = a linha 0 do corpo x cima (unitária), a 0 = cima x a 2, a 1 = cima.
    vu_cross(R(s.landing[2]), R(finfo->orient[0]), R(s.up));
    vu_unit(R(s.landing[2]), R(s.landing[2]));
    vu_cross(R(s.landing[0]), R(s.up), R(s.landing[2]));
    std::memcpy(s.landing[1], s.up, 16);
    std::memcpy(s.landing[3], kVf0, 16);
    VU0_MATRIX4setxrot(s.nose_rotation, car.physics()->nose_jump_angle);
    VU0_MATRIX4_mult(s.nose_rotation, s.landing, s.landing);
    const float blend = ee::div(1.0f, ee::cvt_s(car.physics()->num_blend_steps));
    s.blend = blend;
    // (NUM_BLEND_STEPS - 1) partes da orientação atual e uma da nivelada, linha a linha (o xyz das três primeiras).
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 3; ++i) {
            const float keep = ee::cvt_s(car.physics()->num_blend_steps - 1);
            s.landing[r][i] = ee::mul(ee::add(ee::mul(keep, finfo->orient[r][i]), s.landing[r][i]), blend);
        }
    ++g_stats.improve_landing;
    VU0_m4toquat(s.landing, s.landing_quat);
    SetOrientation(body, s.landing_quat);
    if (g_tuning.landing_spin_steps < int32_t(car.d.landing_flag)) {
        float l[4] = {body->ang_momentum[0], body->ang_momentum[1], body->ang_momentum[2], 0.0f};
        for (int i = 0; i < 3; ++i) R(l)[i] = ps2::mul(R(l)[i], 0x3f4ccccdu);  // 0,8
        SetAngularMomentum(body, l);
    }
}

// [driving] 0x0018add8 PBondCar::GetControllerInput
// As ações vêm da fila "CarQ" (ActionQueue::GetAction/PopAction), que o IOModule enche pelo .def do controle; aqui
// BondCar::actions (o port as põe; ver nf_driving). O HUD (GHud::TheApp() +0x2a0, que bloqueia os tiros) e o som (+0x388:
// as ações 12-17 marcam +0x194/+0x198 no AVehicle) não foram portados: o HUD vale 0 e o som só conta.
void PBondCar_GetControllerInput(BondCar& car) {
    PBondCarData& c = car.d;
    RigidBody* body = car.body();
    if (c.force_stop != 0) {
        c.control.steering = 0.0f;
        c.steer_target = 0.0f;
        c.control.steering_vertical = 0.0f;
        c.steer_vertical_target = 0.0f;
        c.control.gas = 0.0f;
        c.gas_input = 0.0f;
        c.control.hand_brake = 0;
        c.control.fire_primary = 0;
        c.control.fire_secondary = 0;
        if ((c.force_stop & 0xa) == 0) {
            const float along = vu_dot(body->info->orient[2], body->vel);
            c.control.brake = ee::lt(c.control.steering, along) ? 1.0f : c.control.steering;
        } else {
            ApplyHeavyFriction(body);
            std::memset(body->vel, 0, 12);
            std::memset(body->momentum, 0, 12);
            c.control.brake = 0.0f;
        }
        c.brake_input = c.control.brake;
        return;
    }
    const float along = vu_dot(body->vel, body->info->orient[2]);
    for (int i = 0; i < car.action_count; ++i) {
        const CarAction& a = car.actions[i];
        const float v = a.value;
        switch (a.type) {
            case 1: c.steer_target = v; break;
            case 2: c.steer_vertical_target = v; break;
            case 3: c.steer_vertical_target = ee::neg(v); break;
            case 12: case 13: case 14: case 15: car.audio_flags |= 2; break;   // GetAudio()->+0x194 = 1
            case 16: case 17: car.audio_flags |= 4; break;                     // GetAudio()->+0x198 = 1
            case 22: c.gas_input = v; break;
            case 23: {
                const float dz = g_tuning.pad_dead_zone;
                if (ee::lt(v, ee::neg(dz))) {
                    c.axis_brake = 0.0f;
                    c.axis_gas = ee::neg(v);
                } else if (ee::lt(dz, v)) {
                    c.axis_brake = v;
                    c.axis_gas = 0.0f;
                } else {
                    c.axis_gas = 0.0f;
                    c.axis_brake = 0.0f;
                }
                break;
            }
            case 26: c.brake_input = v; break;
            case 27:
                c.control.hand_brake = 1;
                c.handbrake_reverse = ee::le(0.0f, along) ? 0 : 1;
                break;
            case 28: c.control.hand_brake = 0; break;
            case 29: c.control.fire_primary = 1; break;
            case 30: c.control.fire_primary = 0; break;
            case 31: c.control.fire_secondary = 1; break;
            case 32: c.control.fire_secondary = 0; break;
            default: break;
        }
    }
    car.action_count = 0;
    c.gas_input = ee::min(1.0f, ee::add(c.gas_input, c.gas_input));
    c.target_gas = ee::max(c.axis_gas, c.gas_input);
    c.target_brake = ee::max(c.axis_brake, c.brake_input);
    c.full_lock = ee::lt(ps2::ffloat(0x3f666666u), ee::abs(c.steer_target)) ? 1 : 0;  // 0,9
    const CarPhysics* ph = car.physics();
    const float ws = c.wheel_spin_enable != 0 ? ee::mul(ph->ws_speed, 4.0f) : ph->ws_speed;
    if (c.num_wheels_on_ground < 3 || !ee::lt(ps2::ffloat(0x3f666666u), c.target_gas) || c.full_lock == 0 ||
        !ee::lt(c.car_speed, ws) || c.against_wall != 0)
        c.wheel_spin_enable = 0;
    else
        c.wheel_spin_enable = 1;
    c.control.gas = c.target_gas;
    c.control.brake = c.target_brake;
    if (ee::lt(c.target_brake, c.target_gas)) c.control.brake = 0.0f;
    else c.control.gas = 0.0f;
    if (c.control.hand_brake != 0 && ph->is_sub == 0) {
        c.control.brake = 0.0f;
        c.control.gas = 0.0f;
    }
    const float dz = g_tuning.pad_dead_zone;
    if (ee::lt(ee::neg(dz), c.steer_target) && ee::lt(c.steer_target, dz)) c.steer_target = 0.0f;
    float target = c.steer_override;
    c.steering_prev = c.control.steering;
    c.steering_vertical_prev = c.control.steering_vertical;
    if (ee::eq(target, 0.0f)) target = c.steer_target;
    c.control.steering = Util_Step(c.control.steering, target, 0.25f);
    if (ee::lt(ee::neg(dz), c.steer_vertical_target) && ee::lt(c.steer_vertical_target, dz))
        c.steer_vertical_target = 0.0f;
    target = c.steer_vertical_override;
    if (ee::eq(target, 0.0f)) target = c.steer_vertical_target;
    c.control.steering_vertical = Util_Step(c.control.steering_vertical, target, 0.25f);
}

// [driving] 0x00188200 PBondCar::Simulate
// Portada a parte que move os carros, na ordem do PS2: o pulo de um passo em dois de um carro do tráfego longe da câmera
// (a mais de 50 m: só nos passos da paridade da vaga), os contadores de choque e de dano, a cópia do CarPhysics para a
// área de rascunho (o DMA para o scratchpad), o laser, o "pode ser recolocado" de cabeça para baixo (o do jogador até 30
// passos e ResetCar; os da IA até 120, levando 100 de dano a cada passo, que o port só conta: ApplyDamage não foi
// portado), GetControllerInput, o EMP, os controles zerados no choque e travados por tempo, a física (com +0x23c
// apontando para a cópia: ProcessPhysics no carro do jogador, ProcessSimplePhysics nos outros) e CalculateRPM. Fica de
// fora (só conta, bond_car_stats e physics_pending): a lista de desenho, as luzes de freio e de ré dos da IA, as setas,
// as bolhas e a neve (TriggerFX), o resto do som do motor, o rastro dos pneus (ControlTyreTracks),
// PhysicsObject::Simulate (a posição do render), as armas; as outras físicas (de submarino, de moto de neve, de trilho).
void PBondCar_Simulate(BondCar& car) {
    PBondCarData& c = car.d;
    RigidBody* body = car.body();
    if (body->state == 3) return;
    if (c.in_shock != 0) c.in_shock = int8_t(uint8_t(c.in_shock) - 1);
    if (c.damage_by_player_timer != 0) c.damage_by_player_timer = uint8_t(c.damage_by_player_timer - 1);
    if (c.car_class == 3 && (body->radius_flags & 1u) == 0) {
        // A câmera (a lista de câmeras do mundo, 0x333ed8): longe dela (|d|² > 2500, vmadday sem raiz), o carro anda um
        // passo em dois.
        const float (*cam)[4] = traffic_world().camera;
        if (cam) {
            uint32_t d[3];
            for (int i = 0; i < 3; ++i) d[i] = ps2::sub(bits(body->pos[i]), bits(cam[3][i]));
            const float d2 = flt(vu_length_sq(d));
            if (ee::lt(g_tuning.far_distance_sq, d2) && (body->slot & 1) != (simulation().steps & 1)) return;
        }
    }
    BondCarScratch& s = g_scratch;
    std::memcpy(&s.physics, car.physics(), sizeof(CarPhysics));  // FUN_002a6528: DMA toSPR, 16 quadwords
    if (c.target_beacon != 0 && ee::le(car.hit_points(), 0.0f)) PBondCar_DisableTargetBeacon(car);
    c.laser_active = c.laser_timer > 0 ? 1 : 0;
    if (c.laser_timer > 0) c.laser_timer -= 1;
    if (c.car_class == 0) {
        if (ee::lt(GetOrientToGround(body), ps2::ffloat(0x3e99999au)) && body->ground_levers != 0) {  // 0,3
            if (c.reset_available < 30) c.reset_available += 1;
        } else {
            c.reset_available = 0;
            // vsub.xyz; vmulx.xyz por 0,1; vadd.xyz: a direção de recolocar segue a frente do corpo.
            const uint32_t k = bits(0.1f);
            for (int i = 0; i < 3; ++i) {
                const uint32_t d = ps2::mul(ps2::sub(R(body->info->orient[2])[i], R(c.reset_direction)[i]), k);
                R(c.reset_direction)[i] = ps2::add(R(c.reset_direction)[i], d);
            }
        }
        PBondCar_GetControllerInput(car);
        PBondCar_SimulateLights(car);
        if (c.emp_active != 0 && c.emp_timer < simulation().steps) c.emp_active = 0;  // + RVehicle::SetEMPVictimEffect
    } else if (c.car_class == 1) {
        if (ee::lt(GetOrientToGround(body), ps2::ffloat(0x3e99999au)) && body->ground_levers != 0) {
            if (c.reset_available < 30) c.reset_available += 1;
        } else {
            c.reset_available = 0;
        }
    } else {
        // Os da IA de cabeça para baixo no chão: o contador sobe até 120 e o carro leva dano enquanto vivo.
        if (ee::lt(GetOrientToGround(body), ps2::ffloat(0x3e99999au)) && body->ground_levers != 0)
            c.reset_available += 1;
        else
            c.reset_available = 0;
        if (!(c.reset_available < 121)) c.reset_available = 120;
        if (c.reset_available != 0 && ee::lt(0.0f, car.hit_points())) {
            const float zero[3] = {0.0f, 0.0f, 0.0f};
            car.apply_damage(zero, body->pos, 100.0f, 0.5f, 3);  // a entrada +0x1c (não portada: conta)
        }
    }
    if (c.in_shock != 0) {
        c.control.gas = 0.0f;
        c.control.brake = 0.0f;
        c.control.steering = 0.0f;
    }
    if (simulation().steps < c.control_lock_timer) c.control = c.locked_control;
    if (c.car_class < 2 && c.reset_available == 30) {
        ++g_stats.resets;
        physics_pending("PBondCar::ResetCar");
    }
    const uint32_t saved = c.physics;
    c.physics = nf::p32_u(&s.physics);
    const AIGroundVehicle* ai = nf::p32_ptr<const AIGroundVehicle>(c.ai_ground_vehicle);
    if (ai && ai->spline_path != 0 && ee::lt(0.0f, car.hit_points())) PBondCar_ProcessSplinePhysics(car);  // rail.h
    else if (s.physics.sub_physics != 0) physics_pending("PBondCar::ProcessSubmarinePhysics");
    else if (s.physics.is_snowmobile != 0) physics_pending("PBondCar::ProcessSnowmobilePhysics");
    else if (c.car_class == 0) PBondCar_ProcessPhysics(car);
    else PBondCar_ProcessSimplePhysics(car);
    c.physics = saved;
    // O som (0x00188c64..0x00189390): CalculateRPM e o objeto de som (bond_car_audio_hook).
    if (nf::p32_ptr<CarPhysics>(saved)->sub_physics != 1 && c.car_class != 1) {
        const float rpm = PBondCar_CalculateRPM(car);
        if (g_audio_hook) g_audio_hook(car, rpm);
    }
}

std::function<void(BondCar& car, float rpm)>& bond_car_audio_hook() { return g_audio_hook; }

// [driving] 0x00210540 Util_GenerateMatrix
void Util_GenerateMatrix(float out[4][4], const float dir[3]) {
    float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    float m[4][4] = {};
    m[2][0] = dir[0];
    m[2][1] = dir[1];
    m[2][2] = dir[2];
    m[2][3] = 0.0f;
    vu_unit(R(m[2]), R(m[2]));
    vu_cross(R(m[0]), R(up), R(m[2]));
    vu_unit(R(m[0]), R(m[0]));
    vu_cross(R(m[1]), R(m[2]), R(m[0]));
    std::memcpy(m[3], kVf0, 16);
    std::memcpy(out, m, 64);
}

// [driving] 0x001872d8 PBondCar::InitializeCarControls
void PBondCar_InitializeCarControls(BondCar& car) {
    PBondCarData& c = car.d;
    c.control.gear = 0;
    c.control.gas = 0.0f;
    c.steer_vertical_override = 0.0f;
    c.steer_override = 0.0f;
    c.steer_vertical_target = 0.0f;
    c.steer_target = 0.0f;
    c.target_brake = 0.0f;
    c.target_gas = 0.0f;
    c.axis_brake = 0.0f;
    c.axis_gas = 0.0f;
    c.brake_input = 0.0f;
    c.gas_input = 0.0f;
    c.control.steering = 0.0f;
    c.control.strafe_vertical = 0.0f;
    c.control.strafe_horizontal = 0.0f;
    c.control.steering_vertical = 0.0f;
    c.control.brake = 0.0f;
    c.control.fire_secondary = 0;
    c.control.fire_primary = 0;
    c.control.hand_brake = 0;
}

// [driving] 0x00187350 PBondCar::InitializeCarVariables
// (O global 0x332094, zerado aqui no PS2, não é lido pelo que foi portado.)
void PBondCar_InitializeCarVariables(BondCar& car, bool init_controls, uint32_t tyre_damage_points) {
    PBondCarData& c = car.d;
    c.previous_gear = -1;
    c.gear = 1;
    c.laser_timer = 0;
    c.gear_change_timer = 0;
    c.landing_flag = 0;
    if (init_controls) PBondCar_InitializeCarControls(car);  // a entrada +0xb4 da tabela virtual
    c.control_lock_timer = -1;
    c.control.steering = 0.0f;
    c.control.strafe_vertical = 0.0f;
    c.control.strafe_horizontal = 0.0f;
    c.control.steering_vertical = 0.0f;
    c.unknown268 = 0.0f;
    c.control.brake = 0.0f;
    c.control.gas = 0.0f;
    c.control.gear = 0;
    c.reset_available = 0;
    c.control.fire_secondary = 0;
    c.control.hand_brake = 0;
    c.landing_fx_timer = 0;
    c.grip_scale = 1.0f;
    c.wheel_spin_angle[0] = 0.0f;
    c.two_wheel_timer = 1.0f;
    c.damage_by_player = 0.0f;
    c.car_wheel_speed = 0.0f;
    c.wheel_spin_angle[1] = 0.0f;
    c.damage_by_player_timer = 0;
    c.tensor_dirty = 0;
    c.skidding = 0;
    c.rocket_boost = 0;
    c.two_wheel_mode = 0;
    c.in_shock = 0;
    c.wheel_spin_enable = 0;
    c.reverse_timer = 0;
    c.full_lock = 0;
    c.oil_slick = 0;
    c.emp_active = 0;
    c.emp_timer = 0;
    for (int w = 3; w >= 0; --w) c.suspension[w] = 0.0f;
    c.drive_state = 0;
    for (int w = 0; w < 4; ++w) c.tyre_damage_points[w] = tyre_damage_points;  // LookupUInt("TYRE_DAMAGE_POINTS")
    c.control_lock_timer = -1;
    c.unknown420 = 0;
    // O sorteio do SimRandom da Simulation (cvt.s.w) vezes 2^-16 (0x37800000): a fase da onda de um barco.
    c.bob_angle = ee::mul(ee::cvt_s(int32_t(SimRandom_Generate(sim_random()))), ps2::ffloat(0x37800000u));
    c.unknown428 = 0;
    c.was_in_air = 1;
    for (int w = 0; w < 4; ++w) c.wheels[w] = WWorldPos{};
}

// [driving] 0x00187578 PBondCar::ResetDamage
void PBondCar_ResetDamage(BondCar& car) {
    PBondCarData& c = car.d;
    for (int z = 0; z < 16; ++z) {
        c.damage_zones[z][1] = 0.0f;
        c.damage_zones[z][0] = 0.0f;
    }
    // RSceneObj::Reset (0x1c7f10) do render: não portado.
    std::memset(c.glare_on, 0, sizeof c.glare_on);  // +0x3a8..+0x3e7: os 16 brilhos
    c.damage_by_player_timer = 0;
    c.hit_points = 700.0f;
    c.damage_by_player = 0.0f;
}

bool read_car_desc(const Bigf& arc, const std::string& car, CarDesc& out, std::string* error) {
    const std::string key = "data\\car\\model\\" + car + ".crp";
    const int idx = arc.find(key);
    std::vector<uint8_t> bytes;
    if (idx < 0 || !arc.read(idx, bytes)) {
        if (error) *error = "sem " + key;
        return false;
    }
    Carp c;
    if (!carp_parse(bytes.data(), bytes.size(), c)) {
        if (error) *error = key + " não é CARP";
        return false;
    }
    for (const CarpGroup& g : c.root.groups) {
        if (g.tag != carp_tag("Arti")) continue;
        const CarpEntry* name = g.find(carp_tag("Name"));
        if (!name) continue;
        std::string n = carp_string(bytes.data(), bytes.size(), *name);
        std::string lower;
        for (char ch : n) lower.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
        std::string want;
        for (char ch : car) want.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
        if (lower != want) continue;
        const CarpEntry* base = g.find(carp_tag("Base"));
        if (!base || base->data + 0x60 > bytes.size()) break;
        const uint8_t* b = bytes.data() + base->data;
        std::memcpy(out.bbox_min, b + 0x10, 12);
        std::memcpy(out.bbox_max, b + 0x20, 12);
        std::memcpy(&out.desc38, b + 0x38, 4);
        std::memcpy(out.zone_min, b + 0x40, 12);
        std::memcpy(out.zone_max, b + 0x50, 12);
        uint32_t count = 0;
        std::memcpy(&count, b + 0x3c, 4);
        if (base->data + 0x60 + 16ull * count > bytes.size()) break;
        out.collision.resize(count);
        for (uint32_t i = 0; i < count; ++i) std::memcpy(out.collision[i].data(), b + 0x60 + 16 * i, 16);
        return true;
    }
    if (error) *error = key + " não tem o artigo " + car;
    return false;
}

// A conta do RSceneObj::GetBoundingDimensions (0x001c9778, a marca em smackable.cpp) sobre o CarDesc do carro.
void CarDesc_HalfExtents(const CarDesc& d, float half[4]) {
    for (int i = 0; i < 3; ++i) half[i] = ee::mul(ee::sub(d.bbox_max[i], d.bbox_min[i]), 0.5f);
    half[3] = 0.0f;
}

// [driving] 0x001d38a8 RVehicle::GetRenderOffset
float CarDesc_RenderOffset(const CarDesc& d) {
    const float h = ee::mul(ee::sub(d.bbox_max[1], d.bbox_min[1]), 0.5f);
    const float f1 = ee::mul(d.desc38, 0.5f);
    return ee::add(f1, ee::sub(f1, h));
}

// [driving] 0x00186fb0 PBondCar::~PBondCar, a parte da mira (o resto, a fila de ações e as rodas, é do port): o alvo
// sai do WTargetPicker, vira do tipo 0 e perde a referência do carro.
BondCar::~BondCar() {
    if (d.target_beacon != 0) {
        WTargetPicker_UnregisterTarget(d.target_beacon);
        nf::p32_ptr<WTargetableData>(d.target_beacon)->kind = 0;
        WTargetable_RemoveReference(d.target_beacon);
        d.target_beacon = 0;
    }
}

// [driving] 0x001862c0 PBondCar::PBondCar
RigidBody* PBondCar_Construct(BondCar& car, int car_class, const std::string& car_type, const CarDesc& desc,
                              const AttributeCollection& attrs, const float direction[3], const float position[3]) {
    PBondCarData& c = car.d;
    for (WWorldPos& w : c.wheels) w = WWorldPos{};
    c.unknown44c = 0;
    c.sub_rolling = 1;
    c.force_roll_direction = 0;
    c.force_stop = 0;
    c.roll_sub = 0;
    c.scoreable = 1;
    c.car_class = car_class;
    car.name_ = car_type;
    c.shield_point_loc = 0;
    c.unknown2dc = -1.0f;
    // Os atributos: o CarPhysics da coleção (PVehicle::CarPhysicsAttrib) e as chaves que a física lê.
    car.car_physics = attrs.car_physics();
    c.physics = nf::p32_u(&car.car_physics);
    car.boost_time = attrs.lookup_int("BOOST_TIME");
    car.boost_extra_acc = attrs.lookup_float("BOOST_EXTRA_ACC");
    car.lookup_mass = attrs.lookup_float("MASS");
    car.wheel_x_offset = attrs.lookup_float("CAR_WHEEL_X_OFFSET");
    car.wheel_zf_offset = attrs.lookup_float("CAR_WHEEL_ZF_OFFSET");
    car.wheel_zr_offset = attrs.lookup_float("CAR_WHEEL_ZR_OFFSET");
    car.is_police = attrs.lookup_bool("IS_POLICE");
    car.is_heli = attrs.lookup_bool("IS_HELI");
    car.has_siren = attrs.lookup_bool("HAS_SIREN");
    // As que a IA dos elementos pergunta (AttributeSet::LookupBool no EAICommand, no RegisterAgentGroundVehicle e nas
    // armas do motorista: driving/ai/element_controller.h).
    for (const char* k : {"HAS_TURRET", "HAS_MACHINEGUNS", "HAS_CANNONS", "HAS_ROCKETS", "HAS_MISSLES", "HAS_TORPEDOS",
                          "IS_ALPHASUB", "IS_HELI"})
        car.ai_bools[k] = attrs.lookup_bool(k);
    {
        // AttributeSet::LookupVector (0x00169e48, não portado: os números do texto da chave, até quatro).
        std::string text;
        car.turret_range_found = attrs.find("TURRET_RANGE", text);
        const char* p = text.c_str();
        for (int i = 0; i < 4 && car.turret_range_found; ++i) {
            while (*p && !(std::isdigit(uint8_t(*p)) || *p == '-' || *p == '.' || *p == '+')) ++p;
            char* end = nullptr;
            car.turret_range[i] = std::strtof(p, &end);
            if (end == p) break;
            p = end;
        }
    }
    // Os assentos dos passageiros (AttributeSet::LookupVector do WakeUp, 0x0012cb08: docs/driving.md seção 26).
    for (const char* k : {"AIC_EWIND_POS", "AIC_ESUNR_POS", "AIC_ESNOW_POS", "AIC_BOND_POS", "AIC_PASS_POS"}) {
        std::string text;
        if (!attrs.find(k, text)) continue;
        std::array<float, 4> v = {0, 0, 0, 0};
        const char* p = text.c_str();
        for (int i = 0; i < 4; ++i) {
            while (*p && !(std::isdigit(uint8_t(*p)) || *p == '-' || *p == '.' || *p == '+')) ++p;
            char* end = nullptr;
            v[size_t(i)] = std::strtof(p, &end);
            if (end == p) break;
            p = end;
        }
        car.attr_vectors[k] = v;
    }
    car.tyre_radius = attrs.lookup_float("TYRE_RADIUS");
    car.spring_rest_length_attr = attrs.lookup_float("SPRING_REST_LENGTH");
    car.tyre_damage_points_attr = uint32_t(attrs.lookup_int("TYRE_DAMAGE_POINTS"));
    c.laser_active = 0;
    // O modelo (RVehicle::Load não portado aqui: o render é do nf_driving): a caixa e a geometria de colisão.
    float dims[4];
    CarDesc_HalfExtents(desc, dims);
    car.collision_points = desc.collision;
    car.collision_y_offset = ee::neg(CarDesc_RenderOffset(desc));  // PhysicsObject::GetCollisionGeometry
    std::memcpy(car.desc_half, dims, sizeof car.desc_half);         // o que o ApplyDamage lê do render
    car.desc_render_offset = CarDesc_RenderOffset(desc);
    std::memcpy(car.desc_zone_min, desc.zone_min, sizeof car.desc_zone_min);
    std::memcpy(car.desc_zone_max, desc.zone_max, sizeof car.desc_zone_max);
    std::memset(c.glare_on, 0, sizeof c.glare_on);
    uint8_t* raw = reinterpret_cast<uint8_t*>(&c);
    auto put32 = [raw](uint32_t off, uint32_t v) { std::memcpy(raw + off, &v, 4); };
    put32(0x378, 0);
    if (car_class == 4) {
        c.car_class = 2;
        put32(0x378, 1);
    }
    c.turn_signal_period = 60;  // o período das setas
    put32(0x394, 0x3ecccccdu);  // 0,4
    c.turn_signal_mode = 0;
    c.turn_signal_start = 0;
    const CarPhysics& ph = car.car_physics;
    c.tyre_blow_outs = car_class != 0 && ph.is_sub == 0 && ph.is_snowmobile == 0 && !attrs.lookup_bool("IS_TANK") &&
                               !attrs.lookup_bool("IS_HELICOPTER") && !attrs.lookup_bool("TYRES_INVUNERABLE")
                           ? 1
                           : 0;
    c.emp_active = 0;
    c.emp_timer = 0;
    std::memcpy(c.reset_direction, direction, 12);
    c.hit_points = 700.0f;
    c.hit_point_loc = nf::p32_u(&c.hit_points);
    c.immunity = 0;
    c.control.fire_primary = 0;
    PBondCar_InitializeCarVariables(car, true, car.tyre_damage_points_attr);
    PBondCar_ResetDamage(car);
    RigidBodyParams p;
    Util_GenerateMatrix(p.orient, direction);
    p.type = car_class < 0 ? 2 : car_class <= 1 ? 1 : car_class == 3 ? 3 : 2;
    p.mass = car.lookup_mass;
    std::memcpy(p.pos, position, 12);
    std::memcpy(p.half_size, dims, 16);
    Util_GenerateCarTensor(p.inv_inertia, p.mass, dims[0], dims[1], dims[2]);
    p.owner = &car;
    p.moving = 1;
    p.flag2 = 0;
    RigidBody* rb = simulation_add_body(simulation(), p, &car);
    if (rb) c.slot = rb->slot;
    // O alvo da mira (+0x2e0, docs/driving.md seção 24): o carro do jogador (a classe 0) tem um WTargetable fora da
    // lista, os agentes (a classe 2) se registram no WTargetPicker; os outros nenhum (a classe 4 vira 2 sem alvo). O PS2
    // o cria depois do GetBoundingDimensions, com o corpo já na vaga do PhysicsObject; o port, logo depois do corpo.
    c.target_beacon = 0;
    if (car_class == 0 || car_class == 2) c.target_beacon = WTargetable_NewObject(&car);
    if (car_class == 2) WTargetPicker_RegisterTarget(c.target_beacon);
    // O carro do jogador: a vibração (o IFeedback da porta do controle, o UberBond +0x990: 0 no port) e os faróis.
    if (car_class == 0) {
        IFeedback_Construct(car.feedback, 0);
        c.feedback_object = car.feedback.port >= 0 ? 1u : 0u;  // no port só "tem" (o ponteiro do PS2 não existe)
        c.ai_ground_vehicle = 0;
        PBondCar_GlareOn(car, 1);
    } else {
        c.ai_ground_vehicle = 0;
    }
    return rb;
}

namespace {

// O TriggerFX do render (a +0x84 da tabela virtual do RSceneObj de +0x48, com a intensidade -1 e as posições zero):
// as lâmpadas pelo RVehicle; os efeitos contados.
void render_fx(BondCar& car, int32_t type, uint32_t which) {
    if (!car.vehicle) {
        ++g_stats.guards;  // o PS2 sempre tem o render (o construtor o cria)
        return;
    }
    if (!RVehicle_TriggerFX(*car.vehicle, type, int32_t(which))) ++car.render_fx;
}

}  // namespace

// [driving] 0x0018b328 PBondCar::GlareOn
void PBondCar_GlareOn(BondCar& car, int32_t glare) {
    int32_t& on = car.d.glare_on[glare & 15];
    if (on != 0) return;
    render_fx(car, 0, 1u << ((glare + 16) & 31));
    render_fx(car, 0xb, uint32_t(glare));
    on = 1;
}

// [driving] 0x0018b408 PBondCar::GlareOff
void PBondCar_GlareOff(BondCar& car, int32_t glare) {
    int32_t& on = car.d.glare_on[glare & 15];
    if (on != 1) return;
    render_fx(car, 2, 1u << ((glare + 16) & 31));
    render_fx(car, 0xc, uint32_t(glare));
    on = 0;
}

// [driving] 0x0018b580 PBondCar::HandleTurnSignals
void PBondCar_HandleTurnSignals(BondCar& car) {
    PBondCarData& c = car.d;
    PBondCar_GlareOff(car, 3);
    PBondCar_GlareOff(car, 4);
    bool cancel;
    if (c.turn_signal_mode == 1) cancel = ee::lt(c.turn_signal_cancel, c.control.steering);
    else if (c.turn_signal_mode == 2) cancel = ee::lt(c.control.steering, c.turn_signal_cancel);
    else return;
    if (cancel) c.turn_signal_mode = 0;
    // divu: (passo - início) e o período sem sinal; o resto e o período em float pelo truque do sem sinal (srl 1, o bit
    // de baixo, cvt.s.w, x 2).
    const uint32_t period = c.turn_signal_period;
    if (period == 0) {
        ++g_stats.guards;  // break 7 no PS2
        return;
    }
    const uint32_t rem = (uint32_t(simulation().steps) - uint32_t(c.turn_signal_start)) % period;
    auto u2f = [](uint32_t u) {
        if (int32_t(u) >= 0) return ee::cvt_s(int32_t(u));
        const float h = ee::cvt_s(int32_t((u & 1u) | (u >> 1)));
        return ee::add(h, h);
    };
    const float lit = c.turn_signal_lit;
    const bool flash = ee::lt(ee::mul(u2f(period), ee::sub(1.0f, lit)), u2f(rem));
    if (c.turn_signal_mode == 1) {
        if (flash) PBondCar_GlareOn(car, 3);  // a +0x174 da tabela virtual
    } else if (c.turn_signal_mode == 2) {
        if (flash) PBondCar_GlareOn(car, 4);
    }
}

// [driving] 0x00188510 PBondCar::Simulate (as luzes)
void PBondCar_SimulateLights(BondCar& car) {
    PBondCarData& c = car.d;
    const RigidBody* body = car.body();
    // vmul.xyz da velocidade do corpo pela frente (a linha 2 da orientação), vaddy.x, vaddz.x.
    uint32_t p[3];
    for (int i = 0; i < 3; ++i) p[i] = ps2::mul(R(body->vel)[i], R(body->info->orient[2])[i]);
    const float v = flt(ps2::add(ps2::add(p[0], p[1]), p[2]));
    if (ee::lt(0.0f, c.control.brake) && ee::lt(v, 0.0f)) PBondCar_GlareOn(car, 5);  // a +0x174 da tabela virtual
    else if (ee::le(0.0f, v)) PBondCar_GlareOff(car, 5);
    if (ee::le(v, 0.0f)) {
        if (ee::lt(0.0f, c.control.gas) && ee::lt(v, -2.5f)) PBondCar_GlareOn(car, 2);
        else PBondCar_GlareOff(car, 2);
    } else {
        if (ee::lt(0.0f, c.control.brake)) PBondCar_GlareOn(car, 2);
        else PBondCar_GlareOff(car, 2);
    }
    if (car.physics()->is_sub == 1) {
        // As bolhas do submarino: TriggerFX 0 (com a intensidade min(1, (v - 5) x 0,1)) acima de 5 m/s, senão 4.
        render_fx(car, ee::lt(5.0f, v) ? 0 : 4, 0x800000u);
    }
    PBondCar_HandleTurnSignals(car);
}

// [driving] 0x0018bb00 PBondCar::DisableTwoWheelStunt: o giro em z do corpo zerado, o tensor refeito no próximo
// ProcessPhysics. O som (AMenuSound::Trigger) não foi portado.
void PBondCar_DisableTwoWheelStunt(BondCar& car) {
    RigidBody* body = car.body();
    float l[4] = {body->ang_momentum[0], body->ang_momentum[1], 0.0f, 0.0f};
    SetAngularMomentum(body, l);
    car.d.two_wheel_mode = 0;
    car.d.tensor_dirty = 1;
    car.d.two_wheel_timer = 1.0f;
}

// [driving] 0x0018b830 PBondCar::TwoWheelStunt
// O PS2 monta a orientação inclinada numa matriz da pilha cujos w das linhas 0 e 2 ficam com o que estava lá; aqui são
// zero. Só o xyz das linhas entra no produto escalar, mas o produto 4x4 com a rotação em z soma 0 x w (sinal de zero
// pode mudar o -0): sem efeito no resultado observável.
void PBondCar_TwoWheelStunt(BondCar& car) {
    PBondCarData& c = car.d;
    if (c.num_wheels_on_ground == 0 || c.drive_state == 2 || c.drive_state == 4 || ee::lt(c.car_speed, 20.0f)) {
        PBondCar_DisableTwoWheelStunt(car);  // a entrada +0x1cc da tabela virtual
        return;
    }
    RigidBody* body = car.body();
    const RigidBodyInfo* info = body->info;
    float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    float m[4][4] = {};
    vu_cross(R(m[2]), R(info->orient[0]), R(up));
    vu_unit(R(m[2]), R(m[2]));
    vu_cross(R(m[0]), R(up), R(m[2]));
    std::memcpy(m[1], up, 16);
    std::memcpy(m[3], kVf0, 16);
    float rot[4][4];
    VU0_MATRIX4setzrot(rot, g_tuning.two_wheel_angle);
    VU0_MATRIX4_mult(rot, m, m);
    float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float d = vu_dot(m[0], info->orient[0]);
    v[2] = d;
    float t = ee::mul(ee::abs(d), g_tuning.two_wheel_tensor_scale);
    c.two_wheel_timer = t;
    if (ee::lt(1.0f, t)) c.two_wheel_timer = 1.0f;
    float l[4] = {};
    GetLocalAngularMomentum(body, l);
    if (ee::lt(0.0f, d) && ee::lt(0.0f, l[2])) {
        l[2] = ee::mul(l[2], g_tuning.two_wheel_opposite_scale);
        SetAngularMomentum(body, l);
    } else if (ee::lt(d, 0.0f) && ee::lt(l[2], 0.0f)) {
        l[2] = ee::mul(l[2], g_tuning.two_wheel_opposite_scale);
        SetAngularMomentum(body, l);
    }
    if (ee::lt(ee::abs(d), 0.1f)) {
        l[2] = 0.0f;
        SetAngularMomentum(body, l);
        return;
    }
    ++g_stats.two_wheel;
    float z = ee::mul(ee::neg(d), g_tuning.two_wheel_scale);
    z = ee::max(z, ee::neg(g_tuning.two_wheel_limit));
    z = ee::min(z, g_tuning.two_wheel_limit);
    v[2] = z;
    ModifyAngularMomentum(body, v);
}

// ---------------------------------------------------------------- os carros da IA (classe 3: o tráfego)

// [driving] 0x0018b730 PBondCar::ClearEMPState (sem o RVehicle::SetEMPVictimEffect do render, contado)
void PBondCar_ClearEMPState(BondCar& car) {
    ++car.render_fx;
    car.d.emp_timer = 0;
    car.d.emp_active = 0;
}

// [driving] 0x00187eb8 PBondCar::ResetCar(COORD3 &, COORD3 &): o carro parado em `pos`, de frente para `dir`, com as
// rodas no chão: a altura do corpo vem da média das rodas assentadas (o y de `pos` é reescrito, como no PS2). Fica de
// fora o que é do render e da câmera (RSceneObj +0x7c, SetEMPVictimEffect, RPlayerCamera::ResetCamera no carro do
// jogador, os rastros dos pneus: FastFree e InitTyreTracks).
void PBondCar_ResetCar(BondCar& car, float pos[3], const float dir[3]) {
    PBondCarData& c = car.d;
    PBondCar_InitializeCarVariables(car, false, car.tyre_damage_points_attr);
    float m[4][4];
    Util_GenerateMatrix(m, dir);
    RigidBody* body = car.body();
    if (car.physics()->sub_physics == 0) {
        float h = 0.0f;
        TrackWorld* tw = world() ? world()->track_world() : nullptr;
        if (!tw || !tw->world_height_at_point(pos, h)) h = ee::add(pos[1], 1.0f);
        alignas(16) float base[4] = {pos[0], 0.0f, pos[2], 0.0f};
        base[1] = ee::add(ee::add(ee::add(h, car.lookup_float("TYRE_RADIUS")), car.lookup_float("SPRING_REST_LENGTH")),
                          body->info->half_size[1]);
        float sum = 0.0f;
        for (int w = 0; w < 4; ++w) {
            RigidBodyInfo* info = body->info;
            // vmulax.xyz/vmadday.xyz/vmaddz.xyz: a alavanca girada (o w fica).
            Reg r0, r1, r2, l, acc, out;
            load(r0, m[0]);
            load(r1, m[1]);
            load(r2, m[2]);
            load(l, info->lever_local[w]);
            load(out, info->lever_world[w]);
            mulabc(acc, r0, l, BX, XYZ);
            maddabc(acc, r1, l, BY, XYZ);
            maddbc(out, acc, r2, l, BZ, XYZ);
            store(info->lever_world[w], out);
            std::memcpy(c.wheel_pos[w], info->lever_world[w], 16);
            for (int i = 0; i < 3; ++i) c.wheel_pos[w][i] = ee::add(c.wheel_pos[w][i], base[i]);
            TempGetHeightInformation(body, c.wheel_pos[w], c.wheel_road_normal[w], &c.wheels[w]);
            c.suspension[w] = ee::mul(car.physics()->spring_compression_limit, ps2::ffloat(0x3e19999au));  // 0,15
            c.wheel_pos[w][1] = ee::sub(ee::add(c.wheel_pos[w][1], c.wheel_road_normal[w][3]), c.suspension[w]);
            sum = ee::add(sum, c.wheel_pos[w][1]);
        }
        sum = ee::mul(sum, 0.25f);
        pos[1] = ee::add(ee::add(ee::add(sum, car.lookup_float("TYRE_RADIUS")), car.lookup_float("SPRING_REST_LENGTH")),
                         body->info->half_size[1]);
    }
    RigidBody_ResetObject(body, m, pos);
    ++car.render_fx;  // o RSceneObj (+0x7c) e SetEMPVictimEffect(0) do render
}

// [driving] 0x0018e548 PBondCar::AddSimpleWheelForces: a mola, a tração ao longo da roda, a aderência de lado cortada no
// limite e o freio de mão; uma roda fora do chão com o corpo girando sobre ela leva um empurrão que para a rolagem.
// Devolve as rodas com a mola comprimida.
int8_t PBondCar_AddSimpleWheelForces(BondCar& car, BondCarWheelInput* input, const float (*headings)[4],
                                     float (*road_normals)[4], float (*wheel_pos)[4], float* slips) {
    PBondCarData& c = car.d;
    RigidBody* body = car.body();
    BondCarScratch& s = g_scratch;
    s.wheels_on_ground = 0;
    s.lift_above_ground = 0.0f;
    s.force_info = nf::p32_u(nf::raw(body->info));
    if (ee::le(car.hit_points(), 0.0f) && car.physics()->is_boat != 0) return 0;
    const float* heading = headings[0];
    for (int w = 0; w < 4; ++w) {
        BondCarWheelInput& in = input[w];
        const float* normal = road_normals[w];
        const CarPhysics* ph = car.physics();
        std::memcpy(s.lever, wheel_pos[w], 16);
        s.compression = ee::add(normal[3], ph->spring_rest_length);
        if (!PBondCar_IsWheelOnGround(car, w)) s.lift_above_ground = ee::max(s.lift_above_ground, s.compression);
        const float limited = ee::min(s.compression, ph->spring_compression_limit);
        s.compression = limited;
        if (ee::lt(limited, 0.0f)) s.compression = 0.0f;
        const float comp = s.compression;
        const RigidBodyInfo* finfo = nf::p32_ptr<const RigidBodyInfo>(s.force_info);
        if ((ee::lt(0.0f, comp) || ee::lt(0.0f, c.suspension[w])) &&
            ee::lt(g_tuning.enable_roll_stops_threshold, ee::mul(finfo->orient[1][1], normal[1]))) {
            if (ee::lt(0.0f, comp)) {
                s.wheels_on_ground += 1;
                const float change = ee::sub(comp, c.suspension[w]);
                s.heading[3] = change;  // compression_change (+0x2fc)
                const float damping = w < 2 ? ph->spring_damping_front : ph->spring_damping_rear;
                const float stiffness = w < 2 ? ph->spring_stiffness_front : ph->spring_stiffness_rear;
                s.ground_normal[3] = ee::add(ee::mul(comp, stiffness), ee::mul(change, damping));  // spring_force
                if (!PBondCar_IsWheelOnGround(car, w))
                    c.landing_impact = ee::max(ee::mul(s.compression, 5.0f), c.landing_impact);  // 0x396330: 5
            } else {
                s.ground_normal[3] = 0.0f;
            }
            Reg nv;
            load(nv, normal);
            vu_cross(R(s.axle), R(heading), nv);
            vu_unit(R(s.axle), R(s.axle));
            vu_cross(R(s.rolling), nv, R(s.axle));
            vu_unit(R(s.rolling), R(s.rolling));
            vu_cross(R(s.lateral), nv, R(s.rolling));
            for (int i = 0; i < 3; ++i) R(s.lever)[i] = ps2::sub(R(s.lever)[i], R(body->pos)[i]);
            vu_cross(R(s.point_velocity), R(body->ang_vel), R(s.lever));
            for (int i = 0; i < 3; ++i) R(s.point_velocity)[i] = ps2::add(R(s.point_velocity)[i], R(body->vel)[i]);
            float lf = ee::mul(vu_dot(s.point_velocity, s.lateral), in.grip);
            s.lateral_force = lf;
            lf = ee::max(lf, ee::neg(in.friction_limit));
            s.lateral_force = lf;
            lf = ee::min(lf, in.friction_limit);
            const float twice = ee::abs(ee::add(lf, lf));
            s.lateral_force = lf;
            slips[w] = ee::div(twice, in.friction_limit);
            slips[w] = ee::min(slips[w], 1.0f);
            for (int i = 0; i < 3; ++i) R(s.lateral)[i] = ps2::mul(R(s.lateral)[i], bits(s.lateral_force));
            for (int i = 0; i < 3; ++i) R(s.force)[i] = ps2::mul(R(s.rolling)[i], bits(in.drive));
            const RigidBodyInfo* bi = body->info;
            if (bi->flags[1] != 0 || bi->flags[2] != 0) s.lever[1] = 0.0f;
            else s.lever[1] = ee::mul(s.lever[1], ph->wheel_force_app_scale);
            for (int i = 0; i < 3; ++i) R(s.axle)[i] = ps2::mul(R(normal)[i], bits(s.ground_normal[3]));
            s.axle[0] = ee::mul(s.axle[0], car.physics()->slope_scale);
            s.axle[2] = ee::mul(s.axle[2], car.physics()->slope_scale);
            if (!ee::eq(in.handbrake, 0.0f)) {
                vu_unit(R(s.drag), R(s.point_velocity));
                for (int i = 0; i < 3; ++i) R(s.drag)[i] = ps2::mul(R(s.drag)[i], bits(in.handbrake));
                for (int i = 0; i < 3; ++i) R(s.axle)[i] = ps2::sub(R(s.axle)[i], R(s.drag)[i]);
            }
            if (ee::eq(in.handbrake, 0.0f) || ee::lt(1.0f, c.car_speed))
                for (int i = 0; i < 3; ++i) R(s.axle)[i] = ps2::sub(R(s.axle)[i], R(s.lateral)[i]);
            for (int i = 0; i < 3; ++i) R(s.force)[i] = ps2::add(R(s.force)[i], R(s.axle)[i]);
            vu_cross(R(s.torque), R(s.lever), R(s.force));
            ResolveMassScaledForce4(body, s.force);
            ResolveMassScaledTorque4(body, s.torque);
        }
        if (ee::eq(s.compression, 0.0f)) {
            bool push = false;
            if (c.tyre_damage_points[w] != 0 && c.two_wheel_mode == 0 && c.num_wheels_on_ground != 0 &&
                ee::lt(g_tuning.enable_roll_stops_threshold, ee::mul(finfo->orient[1][1], normal[1]))) {
                const bool dead2 = c.car_class == 2 && ee::le(car.hit_points(), 0.0f);
                if (c.car_class == 1) push = true;
                else
                    push = !dead2 && c.in_shock == 0 && body->info->flags[2] == 0 && (body->radius_flags & 1u) != 0 &&
                           body->info->flags[1] == 0;
            }
            if (push) {
                std::memcpy(s.lever, wheel_pos[w], 16);
                for (int i = 0; i < 3; ++i) R(s.lever)[i] = ps2::sub(R(s.lever)[i], R(body->pos)[i]);
                vu_cross(R(s.spin), R(body->ang_vel), R(s.lever));
                if (ee::lt(0.0f, s.spin[1])) {
                    s.force[2] = 0.0f;
                    s.force[1] = -25.0f;
                    s.force[0] = 0.0f;
                    vu_cross(R(s.torque), R(s.lever), R(s.force));
                    ResolveMassScaledTorque4(body, s.torque);
                }
            }
            slips[w] = 0.0f;
        }
        c.suspension[w] = s.compression;
        if (w == 1) heading = headings[1];
    }
    body->pos[1] = ee::add(body->pos[1], s.lift_above_ground);
    return int8_t(s.wheels_on_ground);
}

// [driving] 0x0018ecb8 PBondCar::ProcessSimplePhysics: o passo dos carros da IA com o modelo simples (os eixos com a
// parte fixa da aderência, a tração só atrás, a direção pela matriz girada, as rodas de trás de um carro longe copiando o
// chão das da frente).
// Divergência: o ECollision de uma aterrissagem forte (como em ProcessPhysics) só conta.
void PBondCar_ProcessSimplePhysics(BondCar& car) {
    PBondCarData& c = car.d;
    RigidBody* body = car.body();
    BondCarScratch& s = g_scratch;
    s.info = nf::p32_u(nf::raw(body->info));
    std::memcpy(s.velocity, body->vel, 12);
    std::memcpy(s.forward, body->info->orient[2], 12);
    std::memcpy(s.steered_forward, s.forward, 12);
    if (ee::lt(c.control.brake, c.control.gas)) c.control.brake = 0.0f;
    else c.control.gas = 0.0f;
    // Um barco fora de trilho balança na onda (+0x420 a altura, +0x424 e +0x428 as fases).
    auto boat_off_spline = [&]() {
        if (car.physics()->is_boat == 0) return false;
        const AIGroundVehicle* ai = nf::p32_ptr<const AIGroundVehicle>(c.ai_ground_vehicle);
        return ai && ai->spline_path == 0;
    };
    const bool boat = boat_off_spline();
    if (boat) {
        float bob = ps2::ffloat(c.unknown420);
        body->pos[1] = ee::add(body->pos[1], bob);
        float wave = VU0_Sin(c.bob_angle);
        const float wave2 = VU0_Sin(ps2::ffloat(c.unknown428));
        wave = ee::add(ee::add(wave, 1.0f), wave2);
        const float a = ee::add(c.bob_angle, ps2::ffloat(0x3b102de0u));  // 0,0022
        c.bob_angle = a;
        c.unknown420 = bits(ee::add(ee::mul(wave, 0.1f), 1.0f));
        if (ee::lt(1.0f, a)) c.bob_angle = ee::sub(a, 1.0f);
        const float b = ee::add(ps2::ffloat(c.unknown428), ps2::ffloat(0x3b656042u));  // 0,0035
        c.unknown428 = bits(b);
        if (ee::lt(1.0f, b)) c.unknown428 = bits(ee::sub(b, 1.0f));
    }
    c.wheel_spin_flag = 0.0f;
    c.car_speed = vu_length_xz(body->vel);
    s.velocity[1] = 0.0f;
    s.forward_speed = vu_dot(s.velocity, s.forward);
    if (c.control.hand_brake != 0) {
        if (ee::lt(5.0f, c.car_speed)) {
            s.front_grip = ps2::ffloat(0x3f4ccccdu);  // 0,8
            s.rear_grip = ps2::ffloat(0x3e4ccccdu);   // 0,2
        } else {
            s.front_grip = 0.5f;
            s.rear_grip = 0.5f;
        }
        c.control.brake = 0.0f;
        c.control.gas = 0.0f;
    } else {
        s.front_grip = ps2::ffloat(0x3e99999au);  // 0,3
        s.rear_grip = ps2::ffloat(0x3f333333u);   // 0,7
    }
    if (c.in_shock == 0) {
        s.rear_grip = ee::mul(s.rear_grip, 5.0f);
        s.front_grip = ee::mul(s.front_grip, 5.0f);
        if (c.car_class == 1 || c.car_class == 2) {  // GetCarClass (+0x9c)
            s.front_grip = ee::mul(s.front_grip, 1.5f);
            s.rear_grip = ee::mul(s.rear_grip, 1.5f);
        }
    }
    c.braking_at_speed = ee::lt(ps2::ffloat(0x3d4ccccdu), c.control.brake) ? 1 : 0;  // 0,05
    const CarPhysics* ph = car.physics();
    if (c.control.hand_brake != 0) {
        if (ee::lt(c.car_speed, 1.0f)) s.handbrake = ee::add(ee::add(c.car_speed, c.car_speed), 1.0f);
        else
            s.handbrake = ee::mul(ph->handbrake_force,
                                  ee::add(ee::mul(c.car_speed, ps2::ffloat(0x3d99999au)), 1.75f));  // 0,075
    } else {
        s.handbrake = 0.0f;
    }
    // As voltas das rodas por passo (sqrt.S da FPU), a tração e a direção.
    const float speed = ee::sqrt(ee::add(ee::mul(s.velocity[0], s.velocity[0]), ee::mul(s.velocity[2], s.velocity[2])));
    s.wheel_speed = speed;
    const float per_turn =
        ee::mul(ee::mul(ph->wheel_radius, ps2::ffloat(0x40ca3d71u)), ee::cvt_s(simulation().frame_rate));  // 6,32
    s.wheel_speed = ee::div(speed, per_turn);
    if (ee::lt(s.wheel_speed, ps2::ffloat(0x3c23d70au)) && c.control.hand_brake != 0) s.wheel_speed = 0.0f;  // 0,01
    s.wheel_speed = ee::min(s.wheel_speed, 1.0f);
    const float max_acc = ee::max(12.5f, ph->max_acc);
    const float max_brake = ee::max(12.5f, ph->max_brake);
    s.drive_force = ee::sub(ee::mul(c.control.gas, max_acc), ee::mul(c.control.brake, max_brake));
    const RigidBodyInfo* sinfo = nf::p32_ptr<const RigidBodyInfo>(s.info);
    if (c.num_wheels_on_ground != 0 && ee::lt(0.1f, sinfo->orient[1][1])) {
        const float spin = ee::min(s.wheel_speed, g_tuning.max_wheel_spin_rate_ai);
        c.car_wheel_speed = spin;
        if (ee::lt(s.forward_speed, 0.0f)) c.car_wheel_speed = ee::neg(spin);
    } else {
        c.car_wheel_speed = ee::mul(c.car_wheel_speed, ps2::ffloat(0x3f733333u));  // 0,95
    }
    const float max_steer = ee::max(0.2f, ph->max_steering);
    c.car_steer = ee::mul(c.control.steering, max_steer);
    s.unknown414 = c.control.steering;
    if (c.tyre_damage_points[0] == 0) s.unknown414 = ee::sub(c.control.steering, 1.5f);
    if (c.tyre_damage_points[1] == 0) s.unknown414 = ee::add(s.unknown414, 1.5f);
    if (c.tyre_damage_points[2] == 0) s.unknown414 = ee::sub(s.unknown414, 0.5f);
    if (c.tyre_damage_points[3] == 0) s.unknown414 = ee::add(s.unknown414, 0.5f);
    s.unknown414 = ee::max(-1.0f, s.unknown414);
    const float steer = ee::min(1.0f, s.unknown414);
    s.unknown414 = steer;
    VU0_MATRIX4setyrot(s.steering_turn, ee::mul(steer, ee::max(0.2f, ph->max_steering)));
    VU0_MATRIX4_mult(nf::p32_ptr<RigidBodyInfo>(s.info)->orient, s.steering_turn, s.steered_orient);
    std::memcpy(s.steered_forward, s.steered_orient[2], 12);
    std::memcpy(s.position, body->pos, 12);
    // Float_COORD3toCOORD4: o rolling_resistance da área (o que ficou do carro anterior) com w = 0; ninguém o lê aqui.
    s.rolling_resistance4[0] = s.rolling_resistance[0];
    s.rolling_resistance4[1] = s.rolling_resistance[1];
    s.rolling_resistance4[2] = s.rolling_resistance[2];
    s.rolling_resistance4[3] = 0.0f;
    c.landing_impact = 0.0f;
    // As entradas das rodas: a frente só dirige, a tração atrás.
    std::memcpy(s.wheel_heading[0], s.steered_forward, 12);
    std::memcpy(s.wheel_heading[1], s.forward, 12);
    for (int w = 0; w < 4; ++w) {
        s.wheel_input[w].grip = w < 2 ? s.front_grip : s.rear_grip;
        s.wheel_input[w].drive = w < 2 ? 0.0f : s.drive_force;
        s.wheel_input[w].friction_limit = 20.0f;
    }
    if (c.oil_slick != 0) {
        const float d = ee::mul(s.drive_force, 0.75f);
        s.wheel_input[2].friction_limit = 2.0f;
        s.wheel_input[3].friction_limit = 2.0f;
        s.wheel_input[2].drive = d;
        s.wheel_input[3].drive = d;
    }
    s.unknown444 = 0;  // os pneus estourados
    for (int w = 3; w >= 0; --w)
        if (c.tyre_damage_points[w] == 0) s.unknown444 = uint8_t(s.unknown444 + 1);
    s.unknown418 = s.unknown444 == 1 ? 0.5f : 1.0f;
    float tyre_radius = -1.0f;
    for (int w = 0; w < 4; ++w) {
        BondCarWheelInput& in = s.wheel_input[w];
        in.handbrake = s.handbrake;
        std::memcpy(c.wheel_pos[w], body->info->lever_world[w], 16);
        if (c.car_class != 1 && s.unknown444 != 0) {
            // Um pneu estourado: a roda mais baixa (com o carro em pé) e o carro arrastando nas outras.
            if (c.tyre_damage_points[w] == 0 && ee::lt(ps2::ffloat(0x3f34fdf4u), sinfo->orient[1][1])) {  // 0,707
                if (ee::lt(tyre_radius, 0.0f)) tyre_radius = car.lookup_float("TYRE_RADIUS");
                c.wheel_pos[w][1] = ee::add(c.wheel_pos[w][1], tyre_radius);
            }
            in.drive = ee::mul(in.drive, ee::sub(1.0f, s.unknown418));
            in.handbrake = s.unknown418;
        }
        for (int i = 0; i < 3; ++i) c.wheel_pos[w][i] = ee::add(c.wheel_pos[w][i], s.position[i]);
        if (c.car_class == 3 && (body->radius_flags & 1u) == 0 && (w == 1 || w == 3)) {
            // Um carro do tráfego: as rodas da direita copiam o chão das da esquerda (uma consulta a menos por eixo).
            c.wheels[w] = c.wheels[w - 1];
            std::memcpy(c.wheel_road_normal[w], c.wheel_road_normal[w - 1], 16);
            c.wheel_road_normal[w][3] =
                ee::add(c.wheel_road_normal[w][3], ee::sub(c.wheel_pos[w - 1][1], c.wheel_pos[w][1]));
        } else {
            TempGetHeightInformation(body, c.wheel_pos[w], c.wheel_road_normal[w], &c.wheels[w]);
        }
    }
    for (int w = 0; w < 4; ++w) s.wheel_slip[w] = 0.0f;
    c.num_wheels_on_ground =
        PBondCar_AddSimpleWheelForces(car, s.wheel_input, s.wheel_heading, c.wheel_road_normal, c.wheel_pos,
                                      s.wheel_slip);
    for (int w = 0; w < 4; ++w) {
        c.wheel_pos[w][1] = ee::add(c.wheel_pos[w][1], ee::add(c.wheel_road_normal[w][3], ps2::ffloat(0x3c23d70au)));
        const float slip = ee::mul(ee::sub(s.wheel_slip[w], ps2::ffloat(0x3ecccccdu)), ps2::ffloat(0x3fd55555u));
        c.wheel_slip[w] = slip;
        if (ee::lt(slip, 0.0f)) c.wheel_slip[w] = 0.0f;
    }
    // A aterrissagem: a poeira e as rodas escorregando por alguns passos.
    if (c.num_wheels_on_ground != 0 && c.landing_fx_timer == 0 && !(c.landing_flag < 11))
        c.landing_fx_timer = c.landing_flag;
    if (c.landing_fx_timer != 0) {
        c.skidding = 1;
        c.landing_fx_timer = uint8_t(c.landing_fx_timer - 1);
        std::string type = car.name_;
        for (char& ch : type) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        const float slip = type == "750" ? 1.75f : ps2::ffloat(0x3f733333u);  // 0x2a9198 (sem caixa)
        for (int w = 0; w < 4; ++w) c.wheel_slip[w] = slip;
    } else {
        c.skidding = 0;
    }
    if (c.num_wheels_on_ground != 0) c.landing_flag = 0;
    else if (c.landing_flag < 255) c.landing_flag = uint8_t(c.landing_flag + 1);
    c.wheel_spin_angle[0] = ee::add(c.wheel_spin_angle[0], c.car_wheel_speed);
    c.wheel_spin_angle[1] = ee::add(c.wheel_spin_angle[1], c.car_wheel_speed);
    c.wheel_spin_angle[0] = wrap_turn(c.wheel_spin_angle[0]);
    c.wheel_spin_angle[1] = wrap_turn(c.wheel_spin_angle[1]);
    bool landing = c.car_class == 1;
    if (!landing && ee::lt(0.0f, car.hit_points()) && c.car_class == 2) landing = true;
    if (landing) {
        const RigidBodyInfo* info = body->info;
        if (info->flags[1] == 0 && uint32_t(g_tuning.landing_air_steps) < uint32_t(info->age) &&
            c.num_wheels_on_ground == 0 && body->ground_levers == 0)
            PBondCar_ImproveLanding(car);
    }
    if (ee::lt(ps2::ffloat(0x3c23d70au), c.landing_impact)) {
        c.landing_impact = ee::min(c.landing_impact, 1.0f);
        ++g_stats.landing_impacts;  // o ECollision (o som e a câmera)
    }
    if (boat_off_spline()) body->pos[1] = ee::add(body->pos[1], ee::neg(ps2::ffloat(c.unknown420)));
}

// [driving] 0x0018ad50 PBondCar::EnableTargetBeacon(bool criar): o alvo da mira do carro (criado e registrado com
// `create`) volta a valer (+0x40).
void PBondCar_EnableTargetBeacon(BondCar& car, bool create) {
    PBondCarData& c = car.d;
    if (create && c.target_beacon == 0) {
        c.target_beacon = WTargetable_NewObject(&car);
        WTargetPicker_RegisterTarget(c.target_beacon);
    }
    if (c.target_beacon != 0) nf::p32_ptr<WTargetableData>(c.target_beacon)->unknown40 = 1;
}

// [driving] 0x0018adc0 PBondCar::DisableTargetBeacon
void PBondCar_DisableTargetBeacon(BondCar& car) {
    if (car.d.target_beacon != 0) nf::p32_ptr<WTargetableData>(car.d.target_beacon)->unknown40 = 0;
}

}  // namespace drv
