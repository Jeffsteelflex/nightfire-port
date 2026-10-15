#pragma once
// O carro das missões de carro: o PBondCar do DRIVING.ELF (PBondCar.obj), o objeto que dirige cada carro do jogador e
// dos inimigos sobre um corpo rígido da Simulation (physics/rigid_body.h). Portado do MIPS e conferido contra o
// original no nf_driving_check (parte 6). docs/driving.md seção 13.
//
// O PS2 é a verdade; o nightfire-cxbx (NightfireResearch: src/driving/game/BondCar*.cpp, a reconstrução da versão do
// Xbox) deu os nomes dos campos e a estrutura das funções. Onde o PS2 difere do Xbox (a ordem das contas em float, que
// o Xbox faz em double; a área de rascunho; o leiaute depois de +0x2f0), vale o PS2: cada diferença notada está no
// comentário da função.
//
// O leiaute: PBondCarData é o objeto do PS2 byte a byte (0x460 bytes, FastAlloc(0x460) em Simulation::SpawnCarObject):
// o PhysicsObject (o WWorldPos no começo, a tabela virtual em +0x68 depois dos membros, como no GCC 2.95), o PVehicle
// (+0x6c) e o PBondCar (+0x74). Os ponteiros são palavras de 32 bits (p32) e os campos de nome desconhecido ficam como
// unknown<deslocamento>. BondCar (a classe do port) guarda os dados e responde às chamadas virtuais que a física faz ao
// dono (physics/rigid_body.h, PhysicsObject).
//
// A área de rascunho (BondCarScratch): no PS2 não é a pilha: Simulate a põe no scratchpad (a área livre da Simulation
// + 0x3af, alinhada a 16: logo depois do RigidBodyScratch) e copia para o começo dela, por DMA (o canal 9 toSPR), os
// 0x100 bytes do CarPhysics do carro, trocando o ponteiro +0x23c pela cópia enquanto ProcessPhysics roda. A área é a
// mesma para todos os carros e guarda o que sobrou do carro anterior; aqui é um objeto só (bond_car_scratch()).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <array>
#include <string>
#include <vector>

#include "driving/game/feedback.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/world.h"
#include "platform/ptr32.h"

namespace drv {

class Vehicle;

// Os controles de um carro (0x20 bytes): o que o jogador ou a IA pede neste passo.
struct BondCarControl {
    float steering;            // +0x00 -1..1 (positivo: para a direita do carro)
    float steering_vertical;   // +0x04
    float strafe_vertical;     // +0x08
    float strafe_horizontal;   // +0x0c
    float gas;                 // +0x10 0..1
    float brake;               // +0x14 0..1
    uint8_t gear;              // +0x18 (não zero: CalculateRPM sem marchas)
    uint8_t hand_brake;        // +0x19
    uint8_t fire_primary;      // +0x1a
    uint8_t fire_secondary;    // +0x1b
    uint8_t unknown1c[4];
};
static_assert(sizeof(BondCarControl) == 0x20);

// O que as forças de uma roda recebem de ProcessPhysics (16 bytes).
struct BondCarWheelInput {
    float grip;            // +0x00 escala o escorregar de lado em força
    float drive;           // +0x04 o empurrão ao longo da roda (negativo: freio)
    float friction_limit;  // +0x08 a força de lado máxima do pneu
    float handbrake;       // +0x0c o arrasto contra o movimento da roda
};
static_assert(sizeof(BondCarWheelInput) == 0x10);

// A área de rascunho do carro (0x450 bytes no scratchpad do PS2; ver o cabeçalho).
struct alignas(16) BondCarScratch {
    CarPhysics physics;              // +0x000 a cópia por DMA do CarPhysics do carro
    float steered_orient[4][4];      // +0x100 a orientação do corpo girada pela direção
    float wheel_heading[2][4];       // +0x140 a frente das rodas dianteiras e a das traseiras
    float rolling_resistance4[4];    // +0x160 rolling_resistance com w = 0
    BondCarWheelInput wheel_input[4];  // +0x170
    float wheel_slip[4];             // +0x1b0 a força de lado de cada roda contra o limite, 0..1
    float velocity[3];               // +0x1c0 a do corpo, y zerado
    float forward_speed;             // +0x1cc ao longo da frente
    float rolling_resistance[3];     // +0x1d0 a velocidade vezes ROLLING_RESISTANCE
    float drive_force;               // +0x1dc
    float forward[3];                // +0x1e0 a frente do corpo (a linha 2 da orientação)
    float handbrake;                 // +0x1ec
    float steered_forward[3];        // +0x1f0 a frente das rodas dianteiras
    float front_grip;                // +0x1fc a parte da aderência do eixo da frente
    float position[3];               // +0x200 a do corpo
    float rear_grip;                 // +0x20c a do eixo de trás
    float steering_turn[4][4];       // +0x210 a rotação da direção em y
    // Os vetores de trabalho das forças das rodas (o w de cada um é o que estava ali).
    float lever[4];                  // +0x250 a roda a partir do centro do corpo
    float lateral[4];                // +0x260 o lado da roda, depois a força de lado
    float force[4];                  // +0x270 a força da roda
    float axle[4];                   // +0x280 ao longo do eixo, depois a força da mola
    float point_velocity[4];         // +0x290 a velocidade do corpo na roda
    float rolling[4];                // +0x2a0 a frente da roda no chão, depois a força de tração
    float torque[4];                 // +0x2b0
    float drag[4];                   // +0x2c0 o arrasto do freio de mão
    uint8_t unknown2d0[0xc];         // +0x2d0
    uint32_t force_info;             // +0x2dc (p32 RigidBodyInfo) o do corpo (AddWheelForces, ImproveLanding)
    uint8_t unknown2e0[0xc];         // +0x2e0
    float compression;               // +0x2ec a da mola da roda
    float heading[4];                // +0x2f0 a frente do corpo no plano (w: compression_change)
    float ground_normal[4];          // +0x300 a normal do chão no plano (w: spring_force)
    float spin[4];                   // +0x310 o giro do corpo na roda (w: lateral_excess)
    float lateral_force;             // +0x320
    float lift_above_ground;         // +0x324 a maior compressão de uma roda fora do chão
    uint8_t unknown328[8];           // +0x328
    int32_t wheels_on_ground;        // +0x330 as rodas cuja mola empurrou
    uint8_t unknown334[4];           // +0x334
    float slip_fraction;             // +0x338
    uint8_t unknown33c[4];           // +0x33c
    float landing[4][4];             // +0x340 a orientação de ImproveLanding
    float nose_rotation[4][4];       // +0x380 NOSE_JUMP_ANGLE em torno de x
    float up[4];                     // +0x3c0 (0, 1, 0)
    uint8_t unknown3d0[0x20];        // +0x3d0
    float landing_quat[4];           // +0x3f0
    uint8_t unknown400[0x10];        // +0x400
    uint32_t info;                   // +0x410 (p32 RigidBodyInfo) o do corpo (ProcessPhysics)
    float unknown414;                // +0x414
    float unknown418;                // +0x418
    float max_acc;                   // +0x41c MAXACC vezes a razão da marcha
    float max_rev_acc;               // +0x420 MAXREVACC, cortada acima da velocidade de ré
    float wheel_speed;               // +0x424 as voltas das rodas por passo, no máximo 1
    float boost_acc;                 // +0x428 BOOST_EXTRA_ACC enquanto dura o turbo
    float scale;                     // +0x42c a escala da direção pela velocidade, depois a da aderência
    float slope;                     // +0x430 o excesso de velocidade da ré, depois a inclinação (AddWheelForces)
    float blend;                     // +0x434 1 / NUM_BLEND_STEPS (ImproveLanding)
    float boost_grip;                // +0x438 a escala dos limites de atrito com o turbo
    float counter_steer;             // +0x43c a escala do limite de trás no contraesterço
    uint32_t handbrake_on;           // +0x440 a força do freio de mão aplicada (gravada como palavra)
    uint8_t unknown444;              // +0x444
    uint8_t wheel_spin;              // +0x445 rodas patinando
    uint8_t limited;                 // +0x446 a força de lado da roda foi cortada no limite
    uint8_t on_ground;               // +0x447 a da roda
    uint8_t unknown448[8];           // +0x448
};
static_assert(sizeof(BondCarScratch) == 0x450);
static_assert(offsetof(BondCarScratch, steered_orient) == 0x100 && offsetof(BondCarScratch, wheel_input) == 0x170 &&
              offsetof(BondCarScratch, velocity) == 0x1c0 && offsetof(BondCarScratch, lever) == 0x250 &&
              offsetof(BondCarScratch, force_info) == 0x2dc && offsetof(BondCarScratch, compression) == 0x2ec &&
              offsetof(BondCarScratch, lateral_force) == 0x320 && offsetof(BondCarScratch, wheels_on_ground) == 0x330 &&
              offsetof(BondCarScratch, landing) == 0x340 && offsetof(BondCarScratch, landing_quat) == 0x3f0 &&
              offsetof(BondCarScratch, info) == 0x410 && offsetof(BondCarScratch, counter_steer) == 0x43c &&
              offsetof(BondCarScratch, on_ground) == 0x447);

// O PBondCar do PS2 (0x460 bytes; ver o cabeçalho).
struct alignas(16) PBondCarData {
    // PhysicsObject (0x6c)
    WWorldPos world_pos;              // +0x000 o chão sob o centro
    int32_t type;                     // +0x040 1 nos carros
    uint16_t flags;                   // +0x044
    int16_t slot;                     // +0x046 a vaga do corpo rígido
    uint32_t render_object;           // +0x048 (p32 RSceneObj; o RVehicle)
    uint32_t audio_object;            // +0x04c
    uint32_t feedback_object;         // +0x050
    uint32_t hit_point_loc;           // +0x054 (p32 float) a vida
    uint32_t owner_sig;               // +0x058
    uint32_t attributes;              // +0x05c AttributeSet (a coleção)
    uint32_t collider;                // +0x060
    uint32_t unknown064;              // +0x064
    uint32_t vtable;                  // +0x068
    // PVehicle (+0x6c)
    uint32_t editor_on_mask;          // +0x06c
    uint32_t editor_off_mask;         // +0x070
    // PBondCar (+0x74)
    uint32_t tyre_tracks[4];          // +0x074
    uint8_t unknown084[0xc];          // +0x084
    WWorldPos wheels[4];              // +0x090 o chão sob cada roda
    float wheel_road_normal[4][4];    // +0x190 w: a penetração da roda no chão
    float wheel_pos[4][4];            // +0x1d0
    float suspension[4];              // +0x210 a compressão da mola (0: roda fora do chão)
    float wheel_slip[4];              // +0x220
    float wheel_spin_angle[2];        // +0x230 as rodas da frente, as de trás
    float wheel_spin_flag;            // +0x238 1 com as rodas patinando (o giro a mais do motor)
    uint32_t physics;                 // +0x23c (p32 CarPhysics)
    BondCarControl control;           // +0x240
    float steering_prev;              // +0x260 a direção do passo anterior (GetControllerInput)
    float steering_vertical_prev;     // +0x264
    float unknown268;                 // +0x268
    uint32_t action_queue;            // +0x26c "CarQ", só o do jogador
    float y_draw_modifier;            // +0x270
    float car_wheel_speed;            // +0x274
    float gas_input;                  // +0x278 o acelerador pedido (ação 22)
    float brake_input;                // +0x27c o freio pedido (ação 26)
    float axis_gas;                   // +0x280 o eixo acelerador/freio (ação 23) abaixo da zona morta, positivo
    float axis_brake;                 // +0x284 o mesmo eixo acima da zona morta
    float car_speed;                  // +0x288 no plano
    float car_steer;                  // +0x28c
    float unknown290;                 // +0x290
    int32_t rocket_boost;             // +0x294 passos de turbo
    uint8_t braking_at_speed;         // +0x298 o freio pedido acima de 2,5 m/s
    int8_t num_wheels_on_ground;      // +0x299
    int8_t reverse_timer;             // +0x29a
    uint8_t wheel_spin_enable;        // +0x29b
    uint8_t reversing;                // +0x29c
    int8_t in_shock;                  // +0x29d passos de choque
    uint8_t against_wall;             // +0x29e
    uint8_t handbrake_reverse;        // +0x29f o freio de mão puxado andando de ré
    uint32_t tyre_damage_points[4];   // +0x2a0
    uint8_t unknown2b0;               // +0x2b0
    int8_t drive_state;               // +0x2b1 0 solto, 1 acelerando, 2 freando, 3 ré, 4 freio de mão, 5 o de ré
    uint8_t two_wheel_mode;           // +0x2b2
    uint8_t tensor_dirty;             // +0x2b3
    float two_wheel_timer;            // +0x2b4 a escala do tensor em z (1 fora das duas rodas)
    uint8_t unknown2b8[4];            // +0x2b8
    float target_gas;                 // +0x2bc
    float target_brake;               // +0x2c0
    float steer_target;               // +0x2c4 a direção pedida (ação 1)
    int32_t car_class;                // +0x2c8 0 o do jogador
    uint32_t car_type;                // +0x2cc (p32 char) o nome da coleção de atributos
    uint32_t car_colour;              // +0x2d0
    uint8_t full_lock;                // +0x2d4 |direção pedida| > 0,9
    uint8_t unknown2d5;               // +0x2d5
    uint8_t sub_rolling;              // +0x2d6
    uint8_t gear_change_timer;        // +0x2d7
    int8_t gear;                      // +0x2d8
    int8_t previous_gear;             // +0x2d9
    uint8_t counter_steering;         // +0x2da
    uint8_t landing_flag;             // +0x2db passos no ar (até 255)
    float unknown2dc;                 // +0x2dc
    uint32_t target_beacon;           // +0x2e0
    uint32_t ai_ground_vehicle;       // +0x2e4
    float grip_scale;                 // +0x2e8 a aderência que cai acima de FRIC_MOD_SPEED
    float landing_impact;             // +0x2ec
    float damage_zones[16][2];        // +0x2f0
    float damage_by_player;           // +0x370
    uint8_t damage_by_player_timer;   // +0x374
    uint8_t unknown375[7];            // +0x375
    uint32_t tyre_blow_outs;          // +0x37c
    int32_t emp_active;               // +0x380
    int32_t emp_timer;                // +0x384
    uint32_t audio;                   // +0x388 o som do motor
    int32_t turn_signal_start;        // +0x38c o passo em que a seta começou a piscar
    uint32_t turn_signal_period;      // +0x390 60 (o construtor)
    float turn_signal_lit;            // +0x394 a parte acesa do período (0,4)
    float turn_signal_cancel;         // +0x398 a direção que desliga a seta
    int32_t turn_signal_mode;         // +0x39c 0 desligada, 1 a esquerda (brilho 3), 2 a direita (brilho 4)
    uint8_t laser_active;             // +0x3a0
    uint8_t unknown3a1[3];            // +0x3a1
    int32_t laser_timer;              // +0x3a4
    int32_t glare_on[16];             // +0x3a8 GlareOn/GlareOff: o brilho (a lâmpada) de cada SwitchData ligado
    int32_t reset_available;          // +0x3e8 passos de cabeça para baixo no chão (até 30)
    uint8_t unknown3ec[4];            // +0x3ec
    float reset_direction[3];         // +0x3f0 (lqc2: o w é locked_control.steering, que não muda)
    BondCarControl locked_control;    // +0x3fc
    int32_t control_lock_timer;       // +0x41c o passo até o qual os controles ficam os travados
    uint32_t unknown420;              // +0x420 zerado em InitializeCarVariables
    float bob_angle;                  // +0x424 SimRandom_Generate x 2^-16 em InitializeCarVariables
    uint32_t unknown428;              // +0x428
    float hit_points;                 // +0x42c 700 (o +0x54 aponta para cá)
    uint32_t immunity;                // +0x430
    uint32_t shield_point_loc;        // +0x434
    float steer_override;             // +0x438 não zero: a direção vai para ele em vez da pedida
    uint8_t oil_slick;                // +0x43c
    uint8_t landing_fx_timer;         // +0x43d
    uint8_t skidding;                 // +0x43e
    uint8_t unknown43f;               // +0x43f
    uint32_t scoreable;               // +0x440
    float steer_vertical_target;      // +0x444
    float steer_vertical_override;    // +0x448
    uint8_t unknown44c;               // +0x44c
    int8_t force_roll_direction;      // +0x44d
    uint8_t force_stop;               // +0x44e
    int8_t roll_sub;                  // +0x44f
    uint8_t unknown450[8];            // +0x450
    uint32_t was_in_air;              // +0x458
    uint8_t unknown45c[4];            // +0x45c
};
static_assert(sizeof(PBondCarData) == 0x460);
static_assert(offsetof(PBondCarData, slot) == 0x46 && offsetof(PBondCarData, vtable) == 0x68 &&
              offsetof(PBondCarData, wheels) == 0x90 && offsetof(PBondCarData, wheel_road_normal) == 0x190 &&
              offsetof(PBondCarData, suspension) == 0x210 && offsetof(PBondCarData, physics) == 0x23c &&
              offsetof(PBondCarData, control) == 0x240 && offsetof(PBondCarData, car_speed) == 0x288 &&
              offsetof(PBondCarData, tyre_damage_points) == 0x2a0 && offsetof(PBondCarData, car_class) == 0x2c8 &&
              offsetof(PBondCarData, damage_zones) == 0x2f0 && offsetof(PBondCarData, audio) == 0x388 &&
              offsetof(PBondCarData, reset_available) == 0x3e8 && offsetof(PBondCarData, reset_direction) == 0x3f0 &&
              offsetof(PBondCarData, control_lock_timer) == 0x41c && offsetof(PBondCarData, oil_slick) == 0x43c &&
              offsetof(PBondCarData, steer_vertical_target) == 0x444 && offsetof(PBondCarData, was_in_air) == 0x458 &&
              offsetof(PBondCarData, turn_signal_mode) == 0x39c && offsetof(PBondCarData, glare_on) == 0x3a8);

// As constantes de ajuste do carro: os bancos "Physics:Physical" e "Physics:Friction" (o .data 0x331970..0x331a57,
// registrados por PBondCar::InitializeBondCarGlobals 0x185668 com os valores iniciais do .data; o default.tun de cada
// banco os sobrescreve pelo nome exato, como o banco "Physics:Rigid").
struct BondCarTuning {
    float far_distance_sq = 2500.0f;            // 0x331960 (sem registro) um carro do tráfego mais longe da câmera anda
                                                // um passo em dois
    float enable_roll_stops_threshold = 0.2f;   // 0x331970 ENABLE_ROLL_STOPS_THRESHOLD
    float base_friction_mass = 8.0f;            // 0x331974 BASE_FRICTION_MASS
    float shred_drag = 2.0f;                    // 0x331980 SHRED_DRAG
    float pad_dead_zone = 0.2f;                 // 0x331988 PAD_DEAD_ZONE
    float two_wheel_angle = 0.1f;               // 0x33198c TWO_WHEEL_ANGLE
    float two_wheel_scale = 1875.0f;            // 0x331990 TWO_WHEEL_SCALE
    float two_wheel_limit = 1875.0f;            // 0x331994 TWO_WHEEL_LIMIT
    float two_wheel_opposite_scale = 0.05f;     // 0x331998 TWO_WHEEL_OPPOSITE_SCALE
    float two_wheel_tensor_scale = 1.0f;        // 0x33199c TWO_WHEEL_TENSOR_SCALE
    float skid_audio_scale = 0.15f;             // 0x3319a0 SKID_AUDIO_SCALE
    float rolling_resistance = 0.1f;            // 0x3319a4 ROLLING_RESISTANCE
    float min_button_value = 0.1f;              // 0x3319a8 MIN_BUTTON_VALUE
    float wheel_spin_extra_rpm = 4000.0f;       // 0x3319ac WHEEL_SPIN_EXTRA_RPM
    float damage_scale_collision = 1.0f;        // 0x3319b4 DAMAGE_SCALE_COLLISION
    int32_t emp_lifetime = 120;                 // 0x3319b8 EMP_LIFETIME
    float tyre_damage_radius = 1.25f;           // 0x3319bc TYRE_DAMAGE_RADIUS
    float max_wheel_spin_rate = 0.013f;         // 0x3319c0 MAX_WHEEL_SPIN_RATE
    float max_wheel_spin_rate_ai = 0.05f;       // 0x3319c4 MAX_WHEEL_SPIN_RATE_AI
    int32_t post_brake_accel_count = 160;       // 0x3319c8 POST_BRAKE_ACCEL_COUNT
    float post_brake_accel_scale = 0.2f;        // 0x3319cc POST_BRAKE_ACCEL_SCALE
    float post_brake_accel_min = 0.1f;          // 0x3319d0 POST_BRAKE_ACCEL_MIN
    // 0x3319d8 friction[16] e 0x331a18 lateralLoss[16], pelo tipo do piso (WSurface: 0 kNODRIVE, 1 kPAVED, 2 kGRAVEL,
    // 3 kGRASS, 4 kCOBBLE, 5 kDIRT, 6 kWATER, 7 kWOOD, 8 kICE, 9 kSNOW, 10 kPAVED_ROUGH, 11 (sem registro), 12
    // kRAILROAD, 13 kMETAL).
    float friction[16] = {1, 1, 1, 0.8f, 1, 0.9f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    float lateral_loss[16] = {1, 1, 1, 0.75f, 1, 0.9f, 1, 1, 0.6f, 1, 1, 1, 1, 1, 1, 1};
    int32_t landing_air_steps = 50;             // 0x331964 (sem registro) passos no ar antes de ImproveLanding
    int32_t landing_spin_steps = 10;            // 0x331968 (sem registro) passos no ar para o giro ser freado
};
BondCarTuning& bond_car_tuning();
void reset_bond_car_tuning();
// O default.tun dos dois bancos (data\tuning\Physics\Physical\ e \Friction\; nulo: fica o .data). Os nomes do banco
// "Physics:Friction" registrados são friction[kNODRIVE]...; o arquivo do disco usa frictionCoefficients[WSurface_k...],
// então nenhuma linha dele vale (o jogo fica com o .data: a grama 0,8 em vez de 0,5). Devolve os valores mudados.
int load_bond_car_tuning(const char* physical, const char* friction);

// A área de rascunho do carro (uma só, como o scratchpad do PS2).
BondCarScratch& bond_car_scratch();

// Os contadores do port (o que o carro chama e não foi portado: efeitos, som, eventos).
struct BondCarStats {
    uint64_t landing_impacts = 0;  // ECollision de uma aterrissagem forte (ProcessPhysics)
    uint64_t landing_fx = 0;       // a poeira da aterrissagem (TriggerFX)
    uint64_t resets = 0;           // ResetCar pedido depois de 30 passos de cabeça para baixo
    uint64_t improve_landing = 0;  // ImproveLanding que endireitou o carro no ar
    uint64_t two_wheel = 0;        // TwoWheelStunt que inclinou o carro
    uint64_t guards = 0;           // casos que o PS2 trataria com dados que o port não tem
};
BondCarStats& bond_car_stats();

class BondCar;
// As funções portadas, sobre o carro (os dados do PS2 em car.d).
// [driving] 0x0018cef8 PBondCar::ProcessPhysics
void PBondCar_ProcessPhysics(BondCar& car);
// [driving] 0x0018c1b0 PBondCar::AddWheelForces: devolve as rodas com a mola comprimida.
int8_t PBondCar_AddWheelForces(BondCar& car, BondCarWheelInput* input, float* slips, const float (*headings)[4],
                               float (*road_normals)[4], float (*wheel_pos)[4], const float* rolling_resistance,
                               bool wheel_spin);
// [driving] 0x0018bb90 PBondCar::ImproveLanding
void PBondCar_ImproveLanding(BondCar& car);
// [driving] 0x0018b830 PBondCar::TwoWheelStunt
void PBondCar_TwoWheelStunt(BondCar& car);
// [driving] 0x0018bb00 PBondCar::DisableTwoWheelStunt
void PBondCar_DisableTwoWheelStunt(BondCar& car);
// [driving] 0x0018aa88 PBondCar::CalculateRPM (e a marcha em +0x2d8)
float PBondCar_CalculateRPM(BondCar& car);
// [driving] 0x0018add8 PBondCar::GetControllerInput: as ações da fila "CarQ" nos controles.
void PBondCar_GetControllerInput(BondCar& car);
// [driving] 0x00188200 PBondCar::Simulate (a parte que move o carro do jogador; ver o .cpp).
void PBondCar_Simulate(BondCar& car);
// O fim do PBondCar::Simulate que vai ao objeto de som (GetAudio()): quem tem o som liga (o nf_driving, com
// driving/sound/audio.h PBondCar_AudioFeed); rpm é o PBondCar::CalculateRPM do passo.
std::function<void(BondCar& car, float rpm)>& bond_car_audio_hook();
// [driving] 0x001931f8 PBondCar::IsWheelOnGround
bool PBondCar_IsWheelOnGround(const BondCar& car, int wheel);
// [driving] 0x00210540 Util_GenerateMatrix(MATRIX4 &, COORD3 &): a orientação que olha para a direção (a linha 2), com
// cima = y (o w das linhas 0 e 1 é lixo da pilha no PS2; aqui zero: nada o lê).
void Util_GenerateMatrix(float out[4][4], const float dir[3]);
// [driving] 0x00187350 PBondCar::InitializeCarVariables(bool)
void PBondCar_InitializeCarVariables(BondCar& car, bool init_controls, uint32_t tyre_damage_points);
// [driving] 0x001872d8 PBondCar::InitializeCarControls
void PBondCar_InitializeCarControls(BondCar& car);
// [driving] 0x00187578 PBondCar::ResetDamage (sem o RSceneObj::Reset do render)
void PBondCar_ResetDamage(BondCar& car);
// [driving] 0x0018b730 PBondCar::ClearEMPState
void PBondCar_ClearEMPState(BondCar& car);

// ---- o dano (driving/game/bond_car_damage.cpp, docs/driving.md seção 21)
// [driving] 0x00189598 PBondCar::ApplyDamage(COORD3 &de, COORD3 &para, float dano, float divisão, DamageT tipo,
// Signature *): o ponto do segmento de -> para na caixa do carro (FindOBBIntersect), a zona de dano (CalcDamageZone), os
// pneus (o EMP do tipo 2), a vida (LoseHitPoints, o escudo +0x434, a imunidade), as mortes da missão, a força de um tiro
// (o tipo 2), as zonas +0x2f0 e o que vai ao render (estímulos, TriggerFX). Devolve as bandeiras do ECollision (0x10,
// 0x70, 0x80; 0x90 se o escudo absorveu tudo; 0 se o segmento não toca a caixa). Os tipos: 0 a morte (EKillAIElement),
// 1 um choque, 2 um tiro, 3 um evento.
int32_t PBondCar_ApplyDamage(BondCar& car, const float from[3], const float to[3], float amount, float split,
                             int32_t kind);
// O Signature do PS2 (o t0 do ApplyDamage: quem causou o dano) reduzido ao que a função usa dele: se o
// Simulation::FindPhysicsObjectSignature acha um objeto que o PhysicsObject::IsOwnedBy dá como do jogador (o projétil de
// uma arma do carro do jogador). O port não tem as assinaturas: quem atira (as armas) liga antes de chamar
// apply_damage e desliga depois; os choques e os eventos deixam desligado (o PS2 passa a assinatura do outro corpo, que
// só conta no tipo 2).
bool& damage_source_is_player();
// [driving] 0x001876c8 PBondCar::SetVisualDamage(float, float, float, float): as zonas +0x2f0 sorteadas (o RRandom do
// render) entre os limites do amassado e do rasgado, e os estímulos e efeitos dos níveis.
void PBondCar_SetVisualDamage(BondCar& car, float a_lo, float a_hi, float b_lo, float b_hi);
// [driving] 0x0018ac28 PBondCar::AttackWithEmp: o carro atingido pelo EMP (o Q-Pulse) fica parado (EMP_LIFETIME passos,
// SetInShock(1)); a van da bomba de Paris ("paris_bombvan") dá o ProgrammerDefinedEvent(0) da missão (a vitória).
void PBondCar_AttackWithEmp(BondCar& car);
// [driving] 0x0018ad50 PBondCar::EnableTargetBeacon(bool criar) e 0x0018adc0 DisableTargetBeacon: o alvo da mira do
// carro (a entrada +0x74 e a +0x7c da tabela virtual; o passageiro do teto troca o do carro pelo dele).
void PBondCar_EnableTargetBeacon(BondCar& car, bool create);
void PBondCar_DisableTargetBeacon(BondCar& car);
// O que o dano chama fora do port (o RAnimEngine do render, o som, o HUD, o TriggerFX): a sequência das chamadas (a parte
// 17 do nf_driving_check a compara com a do original) e os contadores.
struct CarDamageCall {
    uint32_t fn;          // o endereço do PS2 (0x1a27a0 ProcessStimuli(h, s, 3, t, 2), 0x1a2868, 0x1a2b50, 0x1d3c28...)
    uint32_t a[4];        // os argumentos inteiros
};
struct CarDamageStats {
    uint64_t calls = 0, hits = 0, misses = 0, shield = 0, tyres_hit = 0, blowouts = 0, deaths = 0, kills = 0;
    uint64_t stimuli = 0, render_fx = 0, sounds = 0, hud = 0, forces = 0, emp = 0, guards = 0;
    bool trace = false;                 // grava as chamadas em `calls_out`
    std::vector<CarDamageCall> calls_out;
};
CarDamageStats& car_damage_stats();
// As peças do ApplyDamage (conferidas uma a uma na parte 17):
// [driving] 0x002d3950 FindOBBIntersect(COORD3 &meias, COORD3 &a, COORD3 &b, COORD3 &saída)
bool FindOBBIntersect(const float half[4], const float a[4], const float b[4], float out[4]);
// [driving] 0x002d0b08 BytesToCoord(uint, COORD3 *soma, COORD3 *escala, COORD3 *soma2, COORD3 *saída)
void BytesToCoord(uint32_t bytes, const float* add, const float* scale, const float* add2, float out[4]);
// [driving] 0x002db798 CARP::BaseDesc::CalcDamageZone(COORD3 &): a zona (0..15) do ponto pela caixa +0x40/+0x50
int32_t CalcDamageZone(const float zone_min[3], const float zone_max[3], const float point[4]);
// [driving] 0x002db930 (sem nome) as zonas vizinhas de uma zona (a tabela 0x3c1fe8)
uint32_t DamageZoneNeighbours(int32_t zone);
// [driving] 0x00195a58 PhysicsObject::LoseHitPoints(float) (a vida no +0x54, não abaixo de 0)
void PhysicsObject_LoseHitPoints(BondCar& car, float amount);
// [driving] 0x00187eb8 PBondCar::ResetCar(COORD3 &, COORD3 &): o carro parado em `pos` (o y é reescrito com a altura das
// rodas assentadas no chão), de frente para `dir`.
void PBondCar_ResetCar(BondCar& car, float pos[3], const float dir[3]);
// [driving] 0x0018ecb8 PBondCar::ProcessSimplePhysics: o passo dos carros da IA (as classes 1-3).
void PBondCar_ProcessSimplePhysics(BondCar& car);
// [driving] 0x0018e548 PBondCar::AddSimpleWheelForces: devolve as rodas com a mola comprimida.
int8_t PBondCar_AddSimpleWheelForces(BondCar& car, BondCarWheelInput* input, const float (*headings)[4],
                                     float (*road_normals)[4], float (*wheel_pos)[4], float* slips);
// [driving] 0x0018b328 PBondCar::GlareOn(CarGlare): a lâmpada (o SwitchData e o brilho) `glare` acesa: os efeitos da
// zona 1 << (glare + 16) ligados e o TriggerFX 0xb do render (driving/render/vehicle.h), se ainda estava apagada.
void PBondCar_GlareOn(BondCar& car, int32_t glare);
// [driving] 0x0018b408 PBondCar::GlareOff(CarGlare): o inverso (o TriggerFX 2 e 0xc), se estava acesa.
void PBondCar_GlareOff(BondCar& car, int32_t glare);
// [driving] 0x0018b580 PBondCar::HandleTurnSignals: as setas (os brilhos 3 e 4) piscando pelo passo da simulação
// (o resto sem sinal de (passo - início) pelo período contra o período x (1 - a parte acesa)), desligadas quando a
// direção passa do limite (+0x398).
void PBondCar_HandleTurnSignals(BondCar& car);
// [driving] 0x00188510 PBondCar::Simulate, o trecho das luzes do carro do jogador (0x00188510..0x0018871c, depois do
// GetControllerInput): a velocidade contra a frente do corpo (vmul.xyz, vaddy.x, vaddz.x); a ré (brilho 5) acesa com o
// freio andando para trás, apagada andando para a frente; o freio (2) aceso com o freio andando para a frente ou com o
// acelerador a mais de 2,5 m/s para trás; nos submarinos as bolhas (o TriggerFX 0/4 da zona 0x800000: contado); as
// setas.
void PBondCar_SimulateLights(BondCar& car);

// O que o carro tira do seu modelo (o RVehicle): o BaseDesc ('Base') do artigo do carro no .crp.
struct CarDesc {
    float bbox_min[3] = {}, bbox_max[3] = {};
    float desc38 = 0;                                // +0x38 (a altura do chão ao centro, pelo uso em GetRenderOffset)
    float zone_min[3] = {}, zone_max[3] = {};        // +0x40, +0x50 a caixa das zonas de dano (CalcDamageZone)
    std::vector<std::array<float, 4>> collision;     // +0x60, BaseDesc +0x3c pontos
};
// Lê o BaseDesc do artigo do carro (o 'Arti' cujo 'Name' é o carro) do data\car\model\<car>.crp do arquivo.
class Bigf;
bool read_car_desc(const Bigf& arc, const std::string& car, CarDesc& out, std::string* error = nullptr);
// [driving] 0x001c9778 RSceneObj::GetBoundingDimensions: as meias-medidas da caixa.
void CarDesc_HalfExtents(const CarDesc& d, float half[4]);
// [driving] 0x001d38a8 RVehicle::GetRenderOffset: +0x38 / 2 + (+0x38 / 2 - a meia altura).
float CarDesc_RenderOffset(const CarDesc& d);

class AttributeCollection;
// [driving] 0x001862c0 PBondCar::PBondCar(CarClass, char *, unsigned int, COORD3 &, COORD3 &): a parte que monta o
// carro e o corpo (os atributos, a caixa e a geometria do modelo, as variáveis, o corpo rígido numa vaga da
// Simulation); o render, o som, o rastro dos pneus, o alvo da mira e a fila de ações do jogo ficam de fora. Devolve o
// corpo (nulo sem vaga).
RigidBody* PBondCar_Construct(BondCar& car, int car_class, const std::string& car_type, const CarDesc& desc,
                              const AttributeCollection& attrs, const float direction[3], const float position[3]);
// [driving] 0x002104a8 Util_Step(float cur, float target, float step)
float Util_Step(float cur, float target, float step);

// Uma ação da fila "CarQ" (ActionQueue): o tipo (a ação do .def, GAMEACTION_*) e o valor (o controle, 0..1 ou -1..1).
struct CarAction {
    int32_t type;
    float value;
};

// O carro do port: os dados do PS2, o CarPhysics da coleção de atributos e a fila de ações; responde às chamadas
// virtuais da física (PhysicsObject).
class BondCar : public PhysicsObject {
public:
    // PBondCar::~PBondCar (0x00186fb0; a parte da mira: o UnregisterTarget do +0x2e0; o resto é do port).
    ~BondCar() override;
    PBondCarData d{};
    CarPhysics car_physics{};       // o da coleção de atributos ("CarPhysics")
    int32_t boost_time = 0;         // BOOST_TIME (AttributeSet::LookupInt)
    float boost_extra_acc = 0;      // BOOST_EXTRA_ACC (AttributeSet::LookupFloat)
    float lookup_mass = 0;          // MASS
    float wheel_x_offset = 0, wheel_zf_offset = 0, wheel_zr_offset = 0;  // CAR_WHEEL_*_OFFSET
    std::string name_;              // o nome do carro (a coleção)
    CarAction actions[64];          // a fila "CarQ" (port: um vetor fixo)
    int action_count = 0;
    // A geometria de colisão do modelo (RSceneObj::GetCollisionGeometry: os pontos do BaseDesc +0x60) e a altura a
    // somar em cada um (o oposto de GetRenderOffset).
    std::vector<std::array<float, 4>> collision_points;
    float collision_y_offset = 0;
    // Os contadores do que o carro chamou e o port não tem (o som de +0x388, o render de +0x48).
    uint64_t audio_flags = 0;
    // O objeto de render (+0x48 no PS2: o RVehicle; o port não o constrói, quem monta o carro o põe antes de
    // PBondCar_Construct) e a vibração (+0x50: o IFeedback que o construtor dá ao carro do jogador).
    Vehicle* vehicle = nullptr;
    IFeedback feedback;
    uint64_t render_fx = 0;   // TriggerFX do render que o port não tem (os efeitos, as bolhas)

    BondCar();
    CarPhysics* physics() { return nf::p32_ptr<CarPhysics>(d.physics); }
    RigidBody* body() const;
    void push_action(int32_t type, float value);

    // PhysicsObject (as entradas da tabela virtual que a física chama)
    PhysicsAttributes* attributes() override { return physics(); }
    const char* name() override { return name_.c_str(); }
    int32_t kind() override { return d.type; }
    const CollisionPoint* collision_geometry(uint32_t& count, float& y_offset) override {
        count = uint32_t(collision_points.size());
        y_offset = collision_y_offset;
        return collision_points.empty() ? nullptr : reinterpret_cast<const CollisionPoint*>(collision_points.data());
    }
    float lookup_float(const char* key) override;
    WWorldPos* world_pos() override { return &d.world_pos; }
    int32_t virtual_18c() override { return d.num_wheels_on_ground; }  // GetNumWheelsOnGround
    int32_t virtual_23c() override { return int32_t(d.was_in_air); }   // GetWasInAir
    void virtual_244(int32_t value) override { d.was_in_air = uint32_t(value); }  // SetWasInAir
    int32_t virtual_9c() override { return d.car_class; }               // GetCarClass
    int32_t virtual_1dc() override { return d.reversing; }              // IsReversing
    void virtual_134(float out[4]) override;                           // GetCarControl (o começo)
    float hit_points() override;
    void simulate() override { PBondCar_Simulate(*this); }
    // As entradas que os choques chamam (driving/physics/collide.h).
    void set_in_shock(float shock) override;                          // +0x14 SetInShock (0x00187a78)
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override;
    uint32_t spline_path() override;                                  // +0x64 GetSplinePath (0x0018ad28)
    void add_damage_by_player(float damage) override;                 // +0x1ac AddDamageByPlayer (0x00187608)
    int32_t damage_by_player_timer() override { return d.damage_by_player_timer; }  // +0x1b4 (0x00193168)
    void set_against_wall(bool against) override { d.against_wall = against ? 1 : 0; }  // +0x1e0 (0x001931b8)
    bool in_two_wheel_mode() override { return d.two_wheel_mode != 0; }  // +0x22c GetIsInTwoWheelMode (0x00193248)
    void attack_with_emp() override { PBondCar_AttackWithEmp(*this); }   // +0x254 AttackWithEmp (0x0018ac28)
    // AttributeSet::LookupBool(+0x5c, "IS_POLICE"), "IS_HELI" e "HAS_SIREN" (lidos na construção).
    bool is_police = false;
    bool is_heli = false;
    bool has_siren = false;
    // AttributeSet::LookupBool das chaves que a IA dos elementos pergunta (lidas na construção; ausente: falso).
    std::map<std::string, bool> ai_bools;
    // AttributeSet::LookupVector("TURRET_RANGE") (o TurnTurret dos motoristas: os dois intervalos de ângulo da torre,
    // em graus); lido na construção.
    // AttributeSet::LookupVector dos assentos (AIC_EWIND_POS, AIC_ESUNR_POS, AIC_ESNOW_POS, AIC_BOND_POS, AIC_PASS_POS),
    // lidos na construção (os quatro números do texto; ausente: o PS2 daria o vetor da coleção padrão).
    std::map<std::string, std::array<float, 4>> attr_vectors;
    bool turret_range_found = false;
    float turret_range[4] = {};
    bool attr_bool(const char* key) const {
        auto it = ai_bools.find(key);
        return it != ai_bools.end() && it->second;
    }
    // AttributeSet::LookupFloat/LookupUInt de TYRE_RADIUS, SPRING_REST_LENGTH e TYRE_DAMAGE_POINTS (ResetCar e
    // ProcessSimplePhysics).
    float tyre_radius = 0;
    float spring_rest_length_attr = 0;
    uint32_t tyre_damage_points_attr = 0;
    // Os ApplyDamage pedidos (PBondCar::ApplyDamage, 0x00189598, driving/game/bond_car_damage.cpp) e a soma do dano.
    uint64_t damage_calls = 0;
    float damage_total = 0;
    // O que o ApplyDamage lê do render (+0x48, o RVehicle; o port não o tem no carro): o RSceneObj::
    // GetBoundingDimensions, o RVehicle::GetRenderOffset (a entrada +0x54) e a caixa das zonas do BaseDesc (+0x40,
    // +0x50; CARP::BaseDesc::CalcDamageZone). Do CarDesc, no PBondCar_Construct.
    float desc_half[4] = {};
    float desc_render_offset = 0;
    float desc_zone_min[3] = {}, desc_zone_max[3] = {};
    // SMissionManager::ProgrammerDefinedEvent(2) de AddDamageByPlayer (a polícia batida pelo jogador).
    uint64_t mission_events = 0;
};

}  // namespace drv
