// O carro desenhado das missões de carro (driving/render/vehicle.h, render/shadow.h) e o que o cerca (game/feedback.h,
// game/input_config.h): propriedades independentes, contas feitas à mão (as rodas giradas e na altura da mola, os
// ponteiros do painel, o desvanecer dos botões, as zonas de dano, a sombra de um carro reto num plano, o pontilhado e os
// trancos da vibração, o Master.def e a parte FRONTEND de um .def). A conferência bit a bit contra o código original
// fica no nf_driving_check (parte 12).

#include <cmath>
#include <cstring>
#include <vector>

#include "driving/game/bond_car.h"
#include "driving/game/feedback.h"
#include "driving/game/input_config.h"
#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/render/shadow.h"
#include "driving/render/vehicle.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {
bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// Um carro com as medidas do Vanquish que o ComputeWheels lê (sem corpo: as rodas só leem o PBondCar e o CarPhysics).
void setup_wheels(BondCar& car, Vehicle& v) {
    car.d.type = 1;
    car.car_physics.wheel_radius = 0.36f;
    car.car_physics.spring_rest_length = 0.24f;
    car.car_physics.spring_compression_limit_draw = 0.3f;
    car.d.physics = nf::p32_u(&car.car_physics);
    RVehicle_Construct(v);
    RSkeletalObj_AllocateSkeleton(v, 8);
    for (int i = 0; i < 4; ++i) {
        v.d.hub_bone[i] = i + 1;
        v.d.wheel_bone[i] = i + 5;
        v.d.wheel_offset[i] = 0.1f;
    }
}
}  // namespace

TEST(driving_vehicle_wheels_steer_and_sit_on_the_springs) {
    BondCar car;
    Vehicle v;
    setup_wheels(car, v);
    car.d.car_steer = 0.2f;          // além do limite de 0,075 volta
    car.d.suspension[0] = 0.1f;      // abaixo do limite de desenho (0,3)
    car.d.suspension[1] = 0.5f;      // acima: o limite
    car.d.wheel_spin_angle[0] = 0.25f;
    // Um quadro anda 0,01 volta (fora da vista de dentro).
    const float s = RVehicle_ComputeWheels(v, &car, false);
    CHECK(near(s, 0.01f, 1e-6f) && near(v.d.steer, 0.01f, 1e-6f));
    // O cubo dianteiro esquerdo girou +0,01 volta em y, o direito -0,01 (a instância dele é espelhada); os de trás nada.
    const float sn = std::sin(2.0f * 3.14159265f * 0.01f);
    CHECK(near(v.bones[0].m[0][2], -sn, 2e-3f) && near(v.bones[1].m[0][2], sn, 2e-3f));
    CHECK(near(v.bones[2].m[0][2], 0.0f, 1e-6f) && near(v.bones[2].m[0][0], 1.0f, 1e-3f));
    // A altura: raio + compressão (limitada) - a altura da roda no modelo - o descanso da mola.
    CHECK(near(v.bones[0].m[3][1], 0.36f + 0.1f - 0.1f - 0.24f, 1e-6f));
    CHECK(near(v.bones[1].m[3][1], 0.36f + 0.3f - 0.1f - 0.24f, 1e-6f));
    // A roda é o giro em x vezes o cubo: a mesma posição, o eixo y virado para z (um quarto de volta).
    CHECK(near(v.bones[4].m[3][1], v.bones[0].m[3][1], 1e-6f));
    CHECK(v.bones[4].m[1][2] > 0.99f && std::fabs(v.bones[4].m[1][1]) < 0.02f);
    // Mais quadros chegam ao limite de 0,075 e param.
    for (int i = 0; i < 12; ++i) RVehicle_ComputeWheels(v, &car, false);
    CHECK(near(v.d.steer, 0.075f, 1e-6f));
    // Na vista de dentro, nos carros das classes 0 e 1, a direção anda só 0,002 por quadro.
    car.d.car_steer = 0.0f;
    RVehicle_ComputeWheels(v, &car, true);
    CHECK(near(v.d.steer, 0.073f, 1e-6f));
    car.d.car_class = 2;
    RVehicle_ComputeWheels(v, &car, true);
    CHECK(near(v.d.steer, 0.063f, 1e-6f));
    // Um carro que não é veículo não mexe em nada.
    car.d.type = 2;
    CHECK(RVehicle_ComputeWheels(v, &car, false) == 0.0f && near(v.d.steer, 0.063f, 1e-6f));
}

TEST(driving_vehicle_needles_and_buttons) {
    // O velocímetro (0..330 de 120 graus a 120 - 225) e o conta-giros (0..7000 de 121 a 121 - 210), limitados.
    CHECK(RVehicle_GetNeedlePos(0.0f, 1) == 120.0f);
    CHECK(near(RVehicle_GetNeedlePos(330.0f, 1), -105.0f, 1e-4f));
    CHECK(near(RVehicle_GetNeedlePos(900.0f, 1), -105.0f, 1e-4f));
    CHECK(RVehicle_GetNeedlePos(-5.0f, 1) == 120.0f);
    CHECK(near(RVehicle_GetNeedlePos(3500.0f, 2), 16.0f, 1e-3f));
    // Os botões 9 e 10 apagam em 30 quadros a partir do TriggerIlluminate.
    CHECK(near(RVehicle_ButtonFade(100, 85), 0.5f, 1e-5f));
    CHECK(RVehicle_ButtonFade(100, 0) == 0.0f);   // nunca acendeu
    CHECK(RVehicle_ButtonFade(100, 100) == 0.0f);  // no mesmo quadro: ainda não (só depois dele)
    CHECK(RVehicle_ButtonFade(100, 60) == 0.0f);   // passou dos 30
    Vehicle v;
    RVehicle_TriggerIlluminate(v, 9, 85);
    CHECK(v.d.switch_w[9] == 2.0f && v.d.switch_time[9] == 85);
    RVehicle_SetPerObjectBuffers(v, 100);
    CHECK(near(v.d.switch_w[9], 0.5f, 1e-5f) && near(vehicle_shared_buffers().switch_data[9][3], 0.5f, 1e-5f));
    // As lâmpadas acesas pelo construtor (painel, sempre acesas, as Misc) chegam ao SwitchData.
    CHECK(vehicle_shared_buffers().switch_data[7][3] == 1.0f && vehicle_shared_buffers().switch_data[0][3] == 0.0f);
    CHECK(RVehicle_TriggerFX(v, 0xb, 3) && v.d.switch_w[3] == 1.0f);
    CHECK(RVehicle_TriggerFX(v, 0xc, 3) && v.d.switch_w[3] == 0.0f);
    const uint64_t guards = vehicle_pending().guards;
    RVehicle_TriggerFX(v, 0xb, 20);  // fora da tabela: guarda, nada gravado
    CHECK(vehicle_pending().guards == guards + 1);
    CHECK(!RVehicle_TriggerFX(v, 6, 0));  // as faíscas: não portadas (contadas)
}

TEST(driving_vehicle_damage_zones) {
    BondCar car;
    Vehicle v;
    car.d.damage_zones[0][0] = 0.8f;
    car.d.damage_zones[0][1] = 0.2f;
    car.d.damage_zones[1][0] = 0.5f;
    car.d.damage_zones[1][1] = 0.1f;
    RVehicle_ComputeDamage(v, &car);
    uint32_t w0, w1;
    std::memcpy(&w0, &v.d.damage_zones[0][3], 4);
    std::memcpy(&w1, &v.d.damage_zones[1][3], 4);
    CHECK(v.d.damage_zones[0][0] == 0.8f && v.d.damage_zones[0][1] == 0.2f && near(v.d.damage_zones[0][2], 0.6f, 1e-6f));
    CHECK(w0 == 127 && w1 == 0);  // o inteiro 127 (o alfa do vidro quebrado) acima de 0,75
    RVehicle_ComputeDamage(v, nullptr);
    CHECK(v.d.damage_zones[0][0] == 0.0f);
}

TEST(driving_vehicle_proc_anim_instances) {
    // 0xff: a instância com a coluna w zerada e o 1 em [3][3] (o w de cada linha guarda as bandeiras).
    float in[16] = {1, 0, 0, 7, 0, 1, 0, 9, 0, 0, 1, 11, 5, 6, 7, 13};
    uint32_t inst[16];
    std::memcpy(inst, in, 64);
    reinterpret_cast<uint8_t*>(inst)[0xd] = 0xff;
    Vehicle v;
    ProcAnimContext ctx = RSkeletalObj_SetProcAnimState(v);
    float out[4][4];
    CHECK(GetInstanceMatrix(inst, ctx, out));
    CHECK(out[0][3] == 0.0f && out[3][3] == 1.0f && out[3][0] == 5.0f);
    // 0xfe sem esqueleto: a matriz zero de 0x383430 (a peça some).
    reinterpret_cast<uint8_t*>(inst)[0xd] = 0xfe;
    CHECK(GetInstanceMatrix(inst, ctx, out));
    CHECK(out[0][0] == 0.0f && out[3][0] == 0.0f && out[3][3] == 0.0f);
    // 0xef: o ponteiro: a rotação em z pelo parâmetro em graus (90: x vai para y).
    reinterpret_cast<uint8_t*>(inst)[0xd] = 0xef;
    reinterpret_cast<uint8_t*>(inst)[0xc] = 0;
    const uint16_t param = 1;
    std::memcpy(reinterpret_cast<uint8_t*>(inst) + 0xe, &param, 2);
    v.d.params[1] = 90.0f;
    ctx = RSkeletalObj_SetProcAnimState(v);
    CHECK(GetInstanceMatrix(inst, ctx, out));
    CHECK(near(out[0][1], 1.0f, 1e-5f) && near(out[0][0], 0.0f, 1e-5f) && out[3][0] == 5.0f);
    float m[4][4];
    BuildRotate(m, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(m[0][0] == 1.0f && m[1][1] == 1.0f && m[0][1] == 0.0f && m[3][3] == 1.0f);
}

TEST(driving_vehicle_simple_shadow_under_a_flat_car) {
    reset_rigid_globals();
    simulation_reset(60);
    BondCar* car = new BondCar();
    car->d.type = 1;
    RigidBodyParams rp;
    rp.mass = 1600.0f;
    rp.type = 1;
    rp.pos[1] = 0.7f;
    const float half[4] = {0.9f, 0.6f, 2.2f, 0};
    std::memcpy(rp.half_size, half, 16);
    Util_GenerateCarTensor(rp.inv_inertia, rp.mass, half[0], half[1], half[2]);
    rp.owner = car;
    RigidBody* rb = simulation_add_body(simulation(), rp, car);
    car->d.slot = rb->slot;
    RigidBodyInfo* info = rb->info;
    const float wheels[4][4] = {{-0.8f, -0.5f, 1.4f, 0}, {0.8f, -0.5f, 1.4f, 0}, {-0.8f, -0.5f, -1.4f, 0}, {0.8f, -0.5f, -1.4f, 0}};
    std::memcpy(info->lever_world, wheels, sizeof wheels);
    info->ground[1] = 1.0f;
    ShadowQuad q;
    RShadowMap_DrawSimpleShadow(*car, 0.8f, 0.8f, 24, q);
    CHECK(q.drawn);
    // O nível: (24 x 255) >> 8 = 23, x (2 - 0) x 0,5; a cor nos quatro bytes.
    CHECK(q.level == 23 && q.colour[0] == 0x17171717u);
    // O centro a 0,1 acima da média das rodas (y 0,2); os eixos: (as diferenças das rodas somadas aos pares) x 0,4.
    CHECK(near(q.corner[0][0], -1.28f, 1e-5f) && near(q.corner[0][1], 0.3f, 1e-5f) && near(q.corner[0][2], 2.24f, 1e-5f));
    CHECK(near(q.corner[3][0], 1.28f, 1e-5f) && near(q.corner[3][2], -2.24f, 1e-5f));
    // Uma roda 2 m acima do chão (a altura negativa): o nível vai a zero e a sombra some.
    car->d.wheel_road_normal[2][3] = -2.5f;
    RShadowMap_DrawSimpleShadow(*car, 0.8f, 0.8f, 24, q);
    CHECK(!q.drawn && q.reason == 3);
    car->d.wheel_road_normal[2][3] = 0.0f;
    info->ground[1] = 0.05f;  // o chão sob o centro quase de pé
    RShadowMap_DrawSimpleShadow(*car, 0.8f, 0.8f, 24, q);
    CHECK(!q.drawn && q.reason == 2);
    simulation_reset(60);
    delete car;
}

TEST(driving_feedback_jolts_and_motors) {
    feedback_reset();
    CHECK(feedback_dither(0) == 0 && feedback_dither(1) == 128 && feedback_dither(2) == 64 && feedback_dither(255) == 254);
    std::vector<int> calls;
    feedback_state().actuator = [&calls](int32_t port, int32_t motor, uint8_t value) {
        calls.push_back(port * 4096 + motor * 256 + value);
    };
    IFeedback f;
    IFeedback_Construct(f, 0);
    CHECK(feedback_state().ports[0].refs == 1 && feedback_state().threads == 1);
    IFeedback_ApplyJolt(f, 0.5f);  // forte: 255 por round(16) quadros
    const IFeedbackPortData& p = feedback_state().ports[0];
    CHECK(p.jolt_level == 255 && p.jolt_count == 16 && p.jolt_half == 8);
    IFeedback_ApplyJolt(f, 0.1f);  // mais fraco e mais curto: não substitui
    CHECK(p.jolt_level == 255 && p.jolt_count == 16);
    IFeedback_SetRumble(f, 0.5f, 1.0f);
    CHECK(p.rumble == 128 && p.rumble_time == 60);
    feedback_state().ports[0].small_base = 0.5f;
    IFeedback_Update();
    // Porta 0: o motor grande com o tranco e o SetRumble (255 + 128, limitado a 255), o pequeno ligado.
    CHECK(calls.size() == 2 && calls[0] == 256 + 255 && calls[1] == 1);
    CHECK(p.small_level == 50 && p.jolt_count == 15 && p.rumble_time == 59);
    // Desligada: os dois motores das quatro portas parados.
    calls.clear();
    feedback_state().enabled = 0;
    IFeedback_Update();
    CHECK(calls.size() == 8 && p.jolt_count == 0 && p.rumble_time == 0);
    for (int c : calls) CHECK((c & 255) == 0);
    feedback_state().enabled = 1;
    // Um choque contra um objeto do tipo 0x0e vale 0,2 da força.
    CollisionInfo ci{};
    ci.strength = -0.9f;
    ci.flags = 0xe;
    IFeedback_AddCollisionInfo(f, ci);
    CHECK(p.jolt_level == 184 && p.jolt_count == 8);  // round(0,18 x 1020)
    IFeedback_Destruct(f);
    CHECK(p.refs == 0 && feedback_state().threads == 0);
    feedback_state().actuator = nullptr;
    feedback_reset();
}

TEST(driving_input_config_master_and_frontend) {
    InputConfigManager m;
    CHECK(m.d.type == 2);
    InputConfigManager_ParseMasterConfigFile(m, "POV 2\r\npovcfg0.def\r\npovcfg1.def\r\nDriving 1\r\ndrivecfg.def\r\nEnd\r\n");
    CHECK(m.d.count[0] == 1 && m.d.count[1] == 2 && m.guards == 0);
    CHECK(std::strcmp(m.configs[0][0].name, "drivecfg.def") == 0 && std::strcmp(m.configs[1][1].name, "povcfg1.def") == 0);
    InputConfigInfo info;
    InputConfigManager_ParseDefFileForFrontEnd(
        m, info,
        "; comment\r\nX A_FIRE\r\nFRONTEND\t; for frontend\r\nDESCRIPTION\t\t2728\r\nButtonCross\t\t2706\r\nTITLE 2729\r\n"
        "END\r\n");
    CHECK(info.locale[0] == 2706 && info.locale[12] == 2728 && info.locale[13] == 2729 && info.locale[1] == 0);
    CHECK(input_locale_index("ButtonR2") == 11 && input_locale_index("Nope") == -1);
    // InitAndPreload: a configuração do carro fora da lista vira 0 com o aviso; a tabela é o FrontEnd.def mais ela.
    InputConfigManager m2;
    InputUberBond uber;
    uber.driving_config = 3;
    uber.inverted = 5;
    InputMap map;
    std::vector<std::string> asked;
    InputConfigManager_InitAndPreload(
        m2,
        [&asked](const std::string& path) {
            asked.push_back(path);
            if (path == "data/Control/Master.def") return std::string("Driving 1\r\ndrivecfg.def\r\nEnd\r\n");
            return std::string("; a primeira linha é pulada\r\nFRONTEND\r\nTITLE 7\r\nEND\r\n");
        },
        uber, map);
    CHECK(m2.d.initialized == 1 && m2.d.type == 0 && m2.d.current[0] == 0 && uber.driving_config == 0);
    CHECK(m2.warnings.size() == 2 && m2.d.inverted == 1);  // o POV sem lista também vira 0
    CHECK(asked.size() == 3 && asked[1] == "data/Control/FrontEnd.def" && asked[2] == "data/Control/drivecfg.def");
    CHECK(m2.configs[0][0].locale[13] == 7);
    CHECK(InputConfigManager_GetNumConfigs(m2, 0) == 1 && InputConfigManager_GetCurrentConfig(m2, 2) == 0);
}
