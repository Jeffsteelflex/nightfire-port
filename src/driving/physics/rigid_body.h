#pragma once
// O corpo rígido das missões de carro (RigidBody e RigidBodyInfo do DRIVING.ELF, objeto RVehicleParticle.obj) e a
// parte da Simulation que o passo da física usa. Leiautes do PS2 byte a byte (static_assert): o código portado lê os
// mesmos deslocamentos que o original, e o nf_driving_check copia estas estruturas para a RAM emulada e roda o código
// do PS2 em cima delas para comparar bit a bit (docs/driving.md, seção 11).
//
// O que é cada coisa (lido no código do PS2, entendido com a versão de Xbox, docs/driving.md seção 9):
//  - RigidBody (0x80 bytes, 48 vagas na Simulation): o estado integrado. Os quatro primeiros quadwords vão direto
//    para vf20..vf23 da VU0 e os quatro seguintes para vf24..vf27 (o passo, Timestep_LoadAndStartVU0); por isso os w
//    guardam outras coisas: a diagonal do tensor de inércia inverso (no referencial do corpo) nos w da posição, da
//    velocidade e da velocidade angular; a massa no w do momento; o ponteiro do RigidBodyInfo no w do momento angular;
//    quatro bytes de estado no w da força; o raio da esfera envolvente (com dois indicadores nos bits 0 e 1 da
//    mantissa) no w do torque;
//  - RigidBodyInfo (0x500 bytes): a orientação em matriz, o tensor inverso no mundo, as alavancas (os pontos que tocam
//    o chão: até 16, no referencial do corpo e no mundo), as quinas da caixa (buffer duplo), o chão sob cada alavanca;
//  - a área de rascunho (RigidBodyScratch): fica no scratchpad do EE logo depois da cópia dos corpos; as alavancas e
//    as normais que CollideWithGround passa à VU0, o bloco de parâmetros dela e o corpo em voo na VU0 (o PS2 dispara a
//    VU0 para um corpo e recolhe o anterior na chamada seguinte).
//
// O dono de um corpo (o PhysicsObject do jogo: PVehicle, PBondCar, Smackable...) não foi portado: o código da física
// só o consulta por chamadas virtuais (atributos, nome, se ignora o chão...) e por duas funções não virtuais
// (GetHitPoints, PlayAnimation). Essas chamadas passam pela interface PhysicsObject daqui, que diz no comentário de
// cada método a entrada da tabela virtual do PS2 (ou o endereço da função); sem dono (nullptr) os corpos de tipo >= 4
// não precisam de nenhuma, salvo a animação de ResolveCollision (que então não toca; conta em physics_pending).

#include <cstddef>
#include <cstdint>
#include <memory>

#include "driving/physics/obb.h"
#include "platform/ptr32.h"

namespace drv {

struct RigidBodyInfo;
struct WWorldPos;
struct WCollider;

// Os atributos de física de um veículo (a estrutura "CarPhysics" de 0x100 bytes que o sistema de atributos guarda em
// cada coleção "pvehicle" e que a entrada +0x1d4 da tabela virtual do dono devolve: PVehicle::GetPhysics). Cada campo
// tem o nome do atributo que o preenche (PVehicle::InitializeGlobals 0x1968f8 registra a chave e o deslocamento; os
// nomes vieram do nightfire-cxbx, NightfireResearch, e os deslocamentos foram conferidos no PS2).
struct CarPhysics {
    int32_t is_4x4;                          // +0x00 IS_4X4
    int32_t is_rally;                        // +0x04 IS_RALLY
    float speed_cutoff_reverse;              // +0x08 SPEED_CUTOFF_REVERSE
    float speed_cutoff_rate_reverse;         // +0x0c SPEED_CUTOFF_RATE_REVERSE
    float wheel_force_app_scale;             // +0x10 WHEEL_FORCE_APP_SCALE
    float max_acc;                           // +0x14 MAXACC
    float max_rev_acc;                       // +0x18 MAXREVACC
    float max_brake;                         // +0x1c MAXBRAKE
    float max_steering;                      // +0x20 MAXSTEERING
    float mod_steer_speed;                   // +0x24 MODSTEERSPEED
    float min_steer;                         // +0x28 MINSTEER
    float fric_mod_speed;                    // +0x2c FRIC_MOD_SPEED
    float fric_mod_range;                    // +0x30 FRIC_MOD_RANGE
    float fric_mod_min;                      // +0x34 FRIC_MOD_MIN
    float yaw_stability_factor;              // +0x38 YAW_STABILITY_FACTOR
    float nose_jump_angle;                   // +0x3c NOSE_JUMP_ANGLE
    float friction_limit_front;              // +0x40 FRICTIONLIMITFRONT
    float friction_limit_rear;               // +0x44 FRICTIONLIMITREAR
    float tyre_grip_factor;                  // +0x48 TYREGRIPFACTOR
    float slope_scale;                       // +0x4c SLOPE_SCALE
    float spring_stiffness_front;            // +0x50 SPRING_STIFFNESS_FRONT
    float spring_stiffness_rear;             // +0x54 SPRING_STIFFNESS_REAR
    float spring_damping_front;              // +0x58 SPRING_DAMPING_FRONT
    float spring_damping_rear;               // +0x5c SPRING_DAMPING_REAR
    float spring_rest_length;                // +0x60 SPRING_REST_LENGTH (InitLevers grava 1,5 num veículo sem
                                             //       geometria de colisão)
    float spring_compression_limit;          // +0x64 SPRING_COMPRESSION_LIMIT
    float spring_compression_limit_draw;     // +0x68 SPRING_COMPRESSION_LIMIT_DRAW
    float wheel_radius;                      // +0x6c WHEEL_RADIUS
    float braking_friction;                  // +0x70 BRAKING_FRICTION
    float accel_frw_ratio;                   // +0x74 ACCEL_FRW_RATIO
    float coast_frw_ratio;                   // +0x78 COAST_FRW_RATIO
    float brake_frw_ratio;                   // +0x7c BRAKE_FRW_RATIO
    float reverse_frw_ratio;                 // +0x80 REVERSE_FRW_RATIO
    float handbrake_frw_ratio;               // +0x84 HANDBRAKE_FRW_RATIO
    float rev_handbrake_frw_ratio;           // +0x88 REV_HANDBRAKE_FRW_RATIO
    float front_slipping_scale;              // +0x8c FRONT_SLIPPING_SCALE
    float rear_slipping_scale;               // +0x90 REAR_SLIPPING_SCALE
    float slipping_range;                    // +0x94 SLIPPING_RANGE
    float handbrake_force;                   // +0x98 HANDBRAKE_FORCE
    float total_handbrake_friction_scale;    // +0x9c TOTAL_HANDBRAKE_FRICTION_SCALE
    float handbrake_rw_fric_scale;           // +0xa0 HANDBRAKE_RW_FRIC_SCALE
    float wheel_spin_scale;                  // +0xa4 WHEELSPINSCALE
    float ws_speed;                          // +0xa8 WS_SPEED
    float ws_accel;                          // +0xac WS_ACCEL
    float counter_steer_scale;               // +0xb0 COUNTER_STEER_SCALE
    float counter_steer_scale_limit;         // +0xb4 COUNTER_STEER_SCALE_LIMIT
    int32_t num_blend_steps;                 // +0xb8 NUM_BLEND_STEPS
    int32_t is_sub;                          // +0xbc IS_SUB
    int32_t sub_physics;                     // +0xc0 SUB_PHYSICS: 1 = sem gravidade nem forças iniciais (o dono
                                             //       move o corpo)
    int32_t no_world_collisions;             // +0xc4 NO_WORLD_COLLISIONS: CollideWithGround não faz nada
    int32_t is_snowmobile;                   // +0xc8 IS_SNOWMOBILE: rodas traseiras no eixo (InitLevers) e a
                                             //       restituição do veículo
    int32_t is_boat;                         // +0xcc IS_BOAT: gravidade x 0,2 a mais e o chão 10 m mais fundo
    float gear_limit[6];                     // +0xd0 GEAR_LIMITR, GEAR_LIMIT1..5
    float gear_ratio[6];                     // +0xe8 GEAR_RATIOR, GEAR_RATIO1..5
};
static_assert(sizeof(CarPhysics) == 0x100);
static_assert(offsetof(CarPhysics, spring_rest_length) == 0x60 && offsetof(CarPhysics, num_blend_steps) == 0xb8 &&
              offsetof(CarPhysics, is_boat) == 0xcc && offsetof(CarPhysics, gear_ratio) == 0xe8);
using PhysicsAttributes = CarPhysics;

// Um ponto de colisão do modelo (x, y, z, w), como a tabela que GetCollisionGeometry devolve. Com o nome, a função
// virtual que devolve um ponteiro para eles se declara sem o declarador de ponteiro para vetor, que o GCC não aceita
// seguido de override.
using CollisionPoint = float[4];

// O dono de um corpo rígido (port: o PhysicsObject do DRIVING.ELF, só os métodos que a física chama).
class PhysicsObject {
public:
    virtual ~PhysicsObject() = default;
    // Tabela virtual +0x1d4: os atributos de física.
    virtual PhysicsAttributes* attributes() = 0;
    // Tabela virtual +0x64: verdadeiro para ignorar o chão neste quadro (CollideWithGround grava a normal (0, 1, 0)).
    virtual bool ignores_ground() { return false; }
    // Tabela virtual +0x84: o nome do objeto (ApplyInitialForcesAndTorques compara com "paradis_car"/"jungle_truck").
    virtual const char* name() { return ""; }
    // PhysicsObject::GetCollisionGeometry: os pontos de colisão do modelo (count = 0: nenhum, usa a caixa) e a altura a
    // somar no y de cada um.
    virtual const CollisionPoint* collision_geometry(uint32_t& count, float& y_offset) {
        count = 0;
        y_offset = 0;
        return nullptr;
    }
    // +0x40: 1 nos carros (InitLevers põe quatro alavancas de roda antes dos pontos do modelo).
    virtual int32_t kind() { return 0; }
    // AttributeSet::LookupFloat(owner + 0x5c, nome): os atributos do .atr (CAR_WHEEL_X_OFFSET...).
    virtual float lookup_float(const char* /*name*/) { return 0; }
    // O WWorldPos do dono (no PS2 o próprio objeto: o PhysicsObject começa com um WWorldPos), o que o corpo guarda em
    // RigidBodyInfo +0x4d8 para saber o chão sob o centro.
    virtual WWorldPos* world_pos() = 0;
    // As entradas +0x18c, +0x23c e +0x244 da tabela virtual, que ApplyInitialForcesAndTorques chama nos veículos do
    // jogador (tipo 1) e no "paradis_car" (a gravidade do pulo); a semântica não foi lida (PVehicle não foi portado).
    // A +0x18c (deslocamento em +0x188; a +0xc0 do Xbox) também em ResolveCollision, no choque entre dois veículos:
    // não zero conta como "apoiado" (como ter alavancas no chão).
    virtual int32_t virtual_18c() { return 0; }
    virtual int32_t virtual_23c() { return 0; }
    virtual void virtual_244(int32_t /*value*/) {}
    // As entradas +0x9c, +0x1dc e +0x134 (esta devolve um vetor por valor), que GenerateImpulse consulta no carro do
    // jogador (tipo 1) batendo num prédio; semântica não lida. A +0x9c (deslocamento em +0x98; a +0x48 do Xbox) também
    // em ResolveCollision: 1 ou 2 marcam um veículo cuja vida conta no choque entre dois veículos, e 1 no corpo que
    // chama (o this, de tipo 2 ou 3) faz o choque com um objeto pular a animação e o caso do corpo parado.
    virtual int32_t virtual_9c() { return 0; }
    virtual int32_t virtual_1dc() { return 0; }
    virtual void virtual_134(float out[4]) { out[0] = out[1] = out[2] = out[3] = 0; }
    // PhysicsObject::GetHitPoints(void) (0x195a30; não virtual no PS2): o float para onde o dono +0x54 aponta (a vida),
    // ou 1,0 sem ele. ResolveCollision o consulta nos veículos de tipo 2 e 3.
    virtual float hit_points() { return 1.0f; }
    // PhysicsObject::PlayAnimation(unsigned int) (0x195bd0; não virtual no PS2): toca a animação id no modelo do dono
    // (o dono +0x48, o +0x28 dele, pela 0x1a2868 com o global 0x339f54 e 2); sem modelo, nada. ResolveCollision toca a
    // 12 num choque com algum objeto, em cada corpo que não é o parado que leva o impulso todo (salvo com um veículo
    // vivo).
    virtual void play_animation(uint32_t /*id*/) {}
    // Tabela virtual +0x2c (Simulate): o passo do objeto, que Simulation::SimulateGame chama depois de mover os corpos
    // (o carro: os controles e as forças das rodas para o próximo passo).
    virtual void simulate() {}

    // As entradas que os choques (driving/physics/collide.h) chamam. O deslocamento na tabela do PS2 é 8 x (a entrada
    // do Xbox + 1) + 4; os nomes são os do PBondCar (o nightfire-cxbx, NightfireResearch). Os padrões são os do
    // PhysicsObject de um objeto sem dono do jogo por trás (nada acontece; o dano conta como pendente).
    // +0x14 SetInShock(float): o susto de um choque com outro corpo (CollideWithObject).
    virtual void set_in_shock(float /*shock*/) {}
    // +0x1c ApplyDamage(COORD3 &de, COORD3 &para, float dano, float divisão, int tipo, sig): o dano de um choque (tipo
    // 1), do ponto de contato `from` ao centro `to`; devolve as bandeiras (vão ao evento ECollision).
    virtual int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind);
    // +0x64 GetSplinePath(): um carro guiado por um trilho da IA não leva dano do mundo.
    virtual uint32_t spline_path() { return 0; }
    // +0x1ac AddDamageByPlayer(float) e +0x1b4 GetDamageByPlayerTimer().
    virtual void add_damage_by_player(float /*damage*/) {}
    virtual int32_t damage_by_player_timer() { return 0; }
    // +0x1e0 SetAgainstWallFlag(bool): o carro do jogador encostado numa parede (CollideWithWorld).
    virtual void set_against_wall(bool /*against*/) {}
    // +0x22c GetIsInTwoWheelMode(): o carro em duas rodas tem a caixa dos choques estreita.
    virtual bool in_two_wheel_mode() { return false; }
    // +0x6c GetResetAvailable(): o carro pode ser reposto (capotado); a câmera do jogador troca para a de capotamento.
    virtual int32_t reset_available() { return 0; }

    // O PhysicsObject +0x60 do PS2: o WCollider do objeto (criado pela primeira colisão com os prédios).
    std::shared_ptr<WCollider> collider;
};

// Um dono mínimo (port) para os corpos sem objeto do jogo por trás: atributos zerados, sem geometria, o seu próprio
// WWorldPos. É o que simulation_add_body usa quando não recebe dono.
class BasicOwner : public PhysicsObject {
public:
    PhysicsAttributes* attributes() override { return &attr; }
    WWorldPos* world_pos() override { return pos_; }
    PhysicsAttributes attr{};
    void set_world_pos(WWorldPos* wp) { pos_ = wp; }

private:
    WWorldPos* pos_ = nullptr;
};

// RigidBody (construtor 0x1f6e98).
struct alignas(16) RigidBody {
    float quat[4];             // +0x00 orientação (x, y, z, w)
    float pos[3];              // +0x10
    float inv_inertia_x;       // +0x1c diagonal do tensor de inércia inverso no referencial do corpo
    float vel[3];              // +0x20
    float inv_inertia_y;       // +0x2c
    float ang_vel[3];          // +0x30
    float inv_inertia_z;       // +0x3c
    float momentum[3];         // +0x40
    float mass;                // +0x4c
    float ang_momentum[3];     // +0x50
    P32<RigidBodyInfo> info;   // +0x5c
    float force[3];            // +0x60 (zerada a cada quadro em ApplyInitialForcesAndTorques)
    int8_t ground_levers;      // +0x6c alavancas que tocaram o chão no último CollideWithGround
    int8_t type;               // +0x6d < 4: veículos (o dono tem atributos de carro)
    int8_t state;              // +0x6e 0 livre, 1 dormindo (ForceToSleep), 2 acordado, 3 congelado
    int8_t slot;               // +0x6f vaga na Simulation
    float torque[3];           // +0x70
    uint32_t radius_flags;     // +0x7c raio da esfera envolvente (float) e os bits 0 (tipo 1-2) e 1 (do construtor)
};
static_assert(sizeof(RigidBody) == 0x80);
static_assert(offsetof(RigidBody, info) == 0x5c);
static_assert(offsetof(RigidBody, ground_levers) == 0x6c);

// RigidBodyInfo (0x500 bytes por vaga, Simulation +0x24).
struct alignas(16) RigidBodyInfo {
    float orient[4][4];            // +0x000 a orientação em linhas (vetor linha: v' = v * M); a linha 3 é (0, 0, 0, 1)
    float inv_world_tensor[4][4];  // +0x040 R^T diag(I^-1) R
    float lever_local[16][4];      // +0x080 alavancas no referencial do corpo
    float lever_world[16][4];      // +0x180 as mesmas giradas para o mundo (sem a posição)
    float corners[2][8][4];        // +0x280 as 8 quinas da caixa no mundo (sem a posição), buffer duplo
    P32<WWorldPos> lever_pos[16];  // +0x380 o rastreador do chão de cada alavanca
    float lever_ground[16][4];     // +0x3c0 a normal do chão sob cada alavanca e (w) a penetração
    float half_size[4];            // +0x4c0 meias-medidas da caixa (pelo menos 0,001); w = 0
    float lever_restitution;       // +0x4d0 o parâmetro +8 das alavancas (zero salvo em ModifyLevers)
    float lever_friction;          // +0x4d4 massa / BASE_FRICTION_MASS x GROUND_FRICTION_COEFF (InitLevers)
    P32<WWorldPos> owner_pos;      // +0x4d8 o WWorldPos do dono (o chão sob o centro)
    int8_t lever_count;            // +0x4dc
    int8_t corner_buffer;          // +0x4dd qual dos dois buffers de quinas é o atual
    uint16_t age;                  // +0x4de contador de quadros (0xffff ao criar: não conta)
    float ground[4];               // +0x4e0 a normal sob o centro; w: a penetração, depois + pos.y (a altura do chão)
    int32_t zone[3];               // +0x4f0 a posição / 5 m arredondada (UpdateZonalInfo)
    uint8_t flags[4];              // +0x4fc..0x4ff (0x4fd: bateu em prédio; 0x4fe/0x4ff: este quadro e o anterior)
};
static_assert(sizeof(RigidBodyInfo) == 0x500);
static_assert(offsetof(RigidBodyInfo, lever_pos) == 0x380);
static_assert(offsetof(RigidBodyInfo, half_size) == 0x4c0);
static_assert(offsetof(RigidBodyInfo, owner_pos) == 0x4d8);
static_assert(offsetof(RigidBodyInfo, ground) == 0x4e0);
static_assert(offsetof(RigidBodyInfo, flags) == 0x4fc);

// Os parâmetros que CollideWithGround passa a ResolveLeverForces (na pilha do PS2).
struct LeverParams {
    uint32_t unused0;   // +0x0
    float friction;     // +0x4 (RigidBodyInfo +0x4d4)
    float restitution;  // +0x8 (RigidBodyInfo +0x4d0)
    int32_t count;      // +0xc alavancas em contato (até 12)
};

// A área de rascunho do scratchpad (Simulation::GetScratchPadFreeZone: o scratchpad + Simulation +0x14).
struct alignas(16) RigidBodyScratch {
    float levers[16][4];        // +0x000 as alavancas em contato (no mundo, relativas ao centro)
    float normals[16][4];       // +0x100 a normal e a penetração de cada uma
    OBB body_box;               // +0x200 a caixa do corpo dos choques (CollideWithObject, CollideWithWorld)
    OBB other_box;              // +0x2b0 a do outro corpo (CollideWithObject)
    float lever_block[3][4];    // +0x360 o que vai para a memória da VU0 27..29 (ResolveLeverForces)
    P32<RigidBody> vu0_body;    // +0x390 o corpo cujo resultado ainda está na VU0
    P32<RigidBodyInfo> vu0_info;  // +0x394
    uint32_t body_box_valid;    // +0x398 body_box já montada nesta chamada
    uint8_t vu0_busy;           // +0x39c
    uint8_t pad[3];
};
static_assert(sizeof(RigidBodyScratch) == 0x3a0);
static_assert(offsetof(RigidBodyScratch, body_box) == 0x200 && offsetof(RigidBodyScratch, other_box) == 0x2b0);
static_assert(offsetof(RigidBodyScratch, lever_block) == 0x360);
static_assert(offsetof(RigidBodyScratch, body_box_valid) == 0x398);
static_assert(offsetof(RigidBodyScratch, vu0_busy) == 0x39c);

// As constantes de ajuste da física (o bloco de .data 0x3390cc..0x339177 do DRIVING.ELF, com os valores iniciais
// dele). InitRigidBodySystem as registra no banco de ajuste "Physics:Rigid" (o default.tun da pista as sobrescreve:
// load_tuning). Os nomes são os do registro; as sem registro têm o nome do uso.
struct alignas(4) RigidTuning {
    float unknown_0cc = 0.0f;                 // 0x3390cc
    float gravity = -34.0f;                   // 0x3390d0 GRAVITY
    float bodge_player_gravity = -34.0f;      // 0x3390d4 BODGE_PLAYER_GRAVITY
    float jump_gravity = 0.0f;                // 0x3390d8 (a gravidade do jogador: a cópia de BODGE na partida)
    float micro_threshold = 10.0f;            // 0x3390dc MICRO_THRESHOLD
    float ground_restitution = 0.5f;          // 0x3390e0 GROUND_RESTITUTION
    float vehicle_restitution = 0.025f;       // 0x3390e4 (sem registro)
    float ground_friction_coeff = 0.05f;      // 0x3390e8 GROUND_FRICTION_COEFF
    float unknown_0ec = 0.0f;                 // 0x3390ec
    float ground_restitution_scale = 0.0f;    // 0x3390f0 GROUND_RESTITUTION / MICRO_THRESHOLD (na partida)
    float unknown_0f4 = 0.0f;                 // 0x3390f4
    float vehicle_restitution_scale = 0.0f;   // 0x3390f8 0x3390e4 / MICRO_THRESHOLD (na partida)
    float natural_angular_damping = 0.995f;   // 0x3390fc NATURAL_ANGULAR_DAMPING
    float sleep_vel = 5.0f;                   // 0x339100 SLEEP_VEL
    float am_limit = 10.0f;                   // 0x339104 AM_LIMIT (limite da velocidade angular)
    float m_limit = 100.0f;                   // 0x339108 M_LIMIT (limite do momento / massa)
    float speed_force_limit = 10.0f;          // 0x33910c SPEED_FORCE_LIMIT
    float unknown_110 = 0.1f;                 // 0x339110
    float speed_scale = 0.25f;                // 0x339114 SPEED_SCALE
    float depth_force_scale = 4.5f;           // 0x339118 DEPTH_FORCE_SCALE
    float total_force_limit = 40.0f;          // 0x33911c TOTAL_FORCE_LIMIT
    float unknown_120 = 1600.0f;              // 0x339120
    float unknown_124 = 5.0f;                 // 0x339124
    float unknown_128 = 0.32f;                // 0x339128
    float unknown_12c = 3.75f;                // 0x33912c
    float unknown_130 = 40.0f;                // 0x339130
    float building_micro_threshold = 25.0f;   // 0x339134 BUILDING_MICRO_THRESHOLD
    float building_coeff_restitution = 0.01f; // 0x339138 BUILDING_COEFF_RESTITUTION
    float building_friction_factor = 0.0045f; // 0x33913c BUILDING_FRICTION_FACTOR
    float building_friction_limit = 0.125f;   // 0x339140 BUILDING_FRICTION_LIMIT
    float building_collide_cog_scale = 1.0f;  // 0x339144 BUILDING_COLLIDE_COG_SCALE
    float building_force_limit = 10.0f;       // 0x339148 BUILDING_FORCE_LIMIT
    float unknown_14c = 0.0f;                 // 0x33914c
    float building_restitution_scale = 0.0f;  // 0x339150 BUILDING_COEFF_RESTITUTION / BUILDING_MICRO_THRESHOLD
    float unknown_154 = -0.11f;               // 0x339154
    float pf_scale = 10.0f;                   // 0x339158 PF_SCALE
    float unknown_15c = 0.35f;                // 0x33915c
    float car_collide_cog_scale = 0.4f;       // 0x339160 CAR_COLLIDE_COG_SCALE
    float unknown_164 = 0.1f;                 // 0x339164
    float unknown_168 = 0.35f;                // 0x339168
    float unknown_16c = 5.0f;                 // 0x33916c
    float unknown_170 = 6.0f;                 // 0x339170
    uint32_t timestep_latched = 0;            // 0x339174 o passo da VU0 já montou as constantes (vf04)
};
static_assert(sizeof(RigidTuning) == 0x339178 - 0x3390cc);

// O ajuste em uso (a cópia do bloco de .data) e o BASE_FRICTION_MASS do "Physics:Physical" (0x331974, 8).
RigidTuning& rigid_tuning();
float& base_friction_mass();
// A constante de vf04 do passo (0x416280: dt, 0,5, 1000, 0) e o indicador "uw_" (0x416298: a pista é submarina).
struct TimestepConstants {
    float dt_scaled = 0;
    float half = 0;
    float quat_scale = 0;
    float zero = 0;
};
TimestepConstants& timestep_constants();
bool& underwater_track();
// O SMissionManager +0x350 (uma palavra; o gerente é o ponteiro em 0x33a024), que ResolveCollision consulta no choque
// entre dois veículos: não zero numa missão em que a vida dos veículos conta (EPlayerLose, com ele, trava a câmera no
// jogador e mostra a cortina de sangue quando a vida acaba). O nome é do uso; zero por padrão (reset_rigid_globals).
int32_t& mission_uses_health();

// O construtor estático do RVehicleParticle.obj (0x1fd338): as razões derivadas das constantes, na partida.
void rigid_static_init();
// Os valores do PS2 de novo (para testes e para cada execução do nf_driving_check).
void reset_rigid_globals();
// Aplica um .tun ("NOME=valor" por linha) às constantes registradas em "Physics:Rigid". Devolve quantas mudaram.
int load_rigid_tuning(const char* text);

// ---- as funções do RigidBody -------------------------------------------------------------------------------------

// A área de rascunho no scratchpad (DAT_00339178: Simulation::GetScratchPadFreeZone, gravado por InitRigidBodySystem).
RigidBodyScratch& rigid_scratch();
// [driving] 0x1f7860 RigidBody::GetOwner(void): o dono na vaga do corpo (Simulation +0x270).
PhysicsObject* GetOwner(const RigidBody* rb);
// Os atributos de um dono (a entrada +0x1d4); nulo sem dono (o porte conta: o PS2 chamaria um ponteiro nulo).
PhysicsAttributes* owner_attributes(PhysicsObject* owner);

// [driving] 0x1f6600 RigidBody::InitRigidBodySystem(void). track: o nome da pista (o bloco de troca +0x4f4); tun: o
// texto do data\tuning\Physics\Rigid\<pista>.tun ou default.tun (nulo: os valores do .data).
void InitRigidBodySystem(const char* track, const char* tun);
// [driving] 0x1f6ac8 RigidBody::ResetRigidBodySP(void)
void ResetRigidBodySP();

// Os argumentos do construtor (RigidBody(int, int, COORD3 &, COORD3 &, COORD3 &, MATRIX4 &, COORD3 &, COORD4 &,
// PhysicsObject *, int, int), mais a massa em f12).
struct RigidBodyParams {
    float mass = 1;
    int slot = 0;
    int type = 4;
    float pos[3] = {};
    float vel[3] = {};
    float ang_vel[3] = {};
    float orient[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    float inv_inertia[3] = {1, 1, 1};  // a diagonal do tensor inverso no referencial do corpo
    float half_size[4] = {0.5f, 0.5f, 0.5f, 0};
    PhysicsObject* owner = nullptr;
    int moving = 1;  // 0: velocidades zeradas e o corpo nasce dormindo (estado 1)
    int flag2 = 0;   // o bit 1 de +0x7c
};
// [driving] 0x1f6e98 RigidBody::RigidBody(...): constrói rb (a vaga na Simulation) e o RigidBodyInfo da vaga.
void RigidBody_Construct(RigidBody* rb, const RigidBodyParams& p);
// [driving] 0x1f77e0 RigidBody::~RigidBody(void)
void RigidBody_Destruct(RigidBody* rb);

// [driving] 0x1f72d8 RigidBody::InitLevers(PhysicsObject *, COORD4 &)
void InitLevers(RigidBody* rb, PhysicsObject* owner, const float half[4]);
// [driving] 0x1f7ac0 RigidBody::SetOrientMat(MATRIX4 &)
void SetOrientMat(RigidBody* rb, const float m[4][4]);
// [driving] 0x1f7a80 RigidBody::GetBodyTensorMatrix4(MATRIX4 &): diag(I⁻¹ do corpo), linha 3 = (0, 0, 0, 1).
void GetBodyTensorMatrix4(const RigidBody* rb, float m[4][4]);
// [driving] 0x1f7ae8 RigidBody::StoreBodyMatrix(MATRIX4 &)
void StoreBodyMatrix(RigidBody* rb, const float m[4][4]);
// [driving] 0x1f7b70 RigidBody::StoreInvWorldTensor(MATRIX4 &)
void StoreInvWorldTensor(RigidBody* rb, const float m[4][4]);
// [driving] 0x1f8908 RigidBody::UpdateZonalInfo(void)
void UpdateZonalInfo(RigidBody* rb);
// [driving] 0x1f8088 RigidBody::ApplyAngularDamping(void)
void ApplyAngularDamping(RigidBody* rb);
// [driving] 0x1fcfc0 RigidBody::ControlSleep(void)
void ControlSleep(RigidBody* rb);
// [driving] 0x1fd170 RigidBody::ForceToSleep(void)
void ForceToSleep(RigidBody* rb);
// [driving] 0x1f87d8 RigidBody::ResolveMassScaledForce(COORD3 &): force += f * massa (acorda o corpo).
void ResolveMassScaledForce(RigidBody* rb, const float f[4]);
// [driving] 0x1f8778 RigidBody::ResolveTorque(COORD3 &)
void ResolveTorque(RigidBody* rb, const float t[4]);
// [driving] 0x1f8588 RigidBody::SetAngularMomentum(COORD3 &): L = l girado pela orientação.
void SetAngularMomentum(RigidBody* rb, const float l[4]);
// [driving] 0x1f8168 RigidBody::ModifyAngularMomentum(COORD3 &): l (no referencial do corpo) girado para o mundo,
// gravado de volta em l (só o xyz), e somado ao momento angular.
void ModifyAngularMomentum(RigidBody* rb, float l[4]);
// [driving] 0x1f8380 RigidBody::GetLocalAngularVelocity(void) e 0x1f8250 RigidBody::GetLocalAngularMomentum(void): a
// velocidade e o momento angulares no referencial do corpo (o PS2 devolve o COORD3 por valor: o xyz).
void GetLocalAngularVelocity(const RigidBody* rb, float out[3]);
void GetLocalAngularMomentum(const RigidBody* rb, float out[3]);
// [driving] 0x1f8830 RigidBody::ResolveMassScaledForce4(COORD4 &): o mesmo código de ResolveMassScaledForce.
void ResolveMassScaledForce4(RigidBody* rb, const float f[4]);
// [driving] 0x1f8888 RigidBody::ResolveMassScaledTorque(COORD3 &) e 0x1f88c8 ResolveMassScaledTorque4(COORD4 &):
// torque += t * massa (acorda o corpo, sem limpar o bit 1 de +0x7c).
void ResolveMassScaledTorque(RigidBody* rb, const float t[4]);
void ResolveMassScaledTorque4(RigidBody* rb, const float t[4]);
// [driving] 0x1f85c0 RigidBody::SetOrientation(COORD4 &): o quatérnio, a matriz e o tensor inverso no mundo.
void SetOrientation(RigidBody* rb, const float q[4]);
// [driving] 0x1f80d8 RigidBody::ApplyHeavyFriction(void): os momentos linear e angular vezes zero (v3scale pela FPU: o
// zero leva o sinal de cada componente).
void ApplyHeavyFriction(RigidBody* rb);
// [driving] 0x1f7c60 RigidBody::ApplyInitialForcesAndTorques(void): zera força e torque, aplica a gravidade (e o
// arrasto na água, os ajustes dos veículos) e o atrito de um corpo parado no chão.
void ApplyInitialForcesAndTorques(RigidBody* rb);
// [driving] 0x1f7c00 RigidBody::GetOrientToGround(void): o "para cima" do corpo . (0, 1, 0).
float GetOrientToGround(const RigidBody* rb);
// [driving] 0x1f9500 RigidBody::CollideWithGround(void)
void CollideWithGround(RigidBody* rb);
// [driving] 0x1f6ae0 RigidBody::TempGetHeightInformation(WCollider *, COORD3 &, COORD4 &, WWorldPos *) (nome de
// confiança 1): a normal do chão sob pos e a penetração (w > 0: abaixo do chão). Devolve 0 se não achou face.
int TempGetHeightInformation(RigidBody* rb, const float pos[3], float out[4], WWorldPos* wp);

// Contadores do que o porte não cobre (chamadas a código ainda não portado), por nome.
void physics_pending(const char* name);
int physics_pending_count(const char* name);
void physics_pending_reset();
// Lista "nome=contagem ..." para os relatórios.
const char* physics_pending_report();

}  // namespace drv
