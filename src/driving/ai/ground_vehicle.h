#pragma once
// Os carros da IA (AIVehicle, AIGroundVehicle e AIVehicleController do DRIVING.ELF, AIElementController.obj;
// docs/driving.md seção 18): o "motorista" de cada carro que não é o do jogador. O AIVehicleController guarda os 20 do
// tráfego (um vetor estático de AIGroundVehicle, 0x3282d0) e, a cada quadro da IA (EAIUpdate), passa um em cada 8 deles
// (o do índice cujo resto por 8 é o do passo) por AIGroundVehicle::Update; o motorista olha a rua à frente pelo seu
// navegador (WRoadNav, road_nav.h), freia atrás de outros carros e nos sinais vermelhos dos cruzamentos e escreve os
// controles do carro (a direção, o acelerador, o freio, o freio de mão), que a física do carro (PBondCar, classe 3:
// ProcessSimplePhysics) segue a cada passo.
//
// Portado do MIPS do PS2 (a verdade); o nightfire-cxbx (NightfireResearch) não reconstruiu o AIGroundVehicle (só o
// cabeçalho AIVehicle.h), então os nomes dos campos vêm do DRIVING.SYM (os acessores Get/Set*) e do uso.
//
// O caminho do tráfego (o estado 4 de Update) está em ground_vehicle.cpp: DoTrafficStateUpdate, Spawn, UnSpawn,
// DoAttackMode (os modos 1 e 12), DoMoveMode (os modos de navegação 1 e 3), GetLookAhead, UpdateNavPos,
// CheckTrafficCollision, TestForCollision, GetCollisionStopPoint, DriveToPointTraffic, SetNavigateMode, SetAttackMode e
// o que eles chamam. O dos agentes (o estado 5: os veículos dos elementos da IA, driving/ai/element_controller.h) está
// em ground_vehicle_agent.cpp (docs/driving.md seção 21): UpdateStuck, DoAgentStateUpdate, os modos de ataque 13-15,
// os de navegação 2, 4 e o 1 e 3 dos agentes, DriveToPoint, os desvios (CheckAgentDirectCollision,
// CheckAgentRoadNetworkCollision e as faixas), DoWeaponUpdate. Os estados 1 (o jogador), 2 (a direção automática) e 3
// (o trilho), o modo de navegação 5 (o trilho 'AISp') e o submarino contam em ground_vehicle_pending().
//
// O leiaute é o do PS2 (0x2a0 bytes, static_assert); os ponteiros são palavras p32 (o carro em +0x04 aponta para o
// PBondCarData do BondCar; o port guarda também o BondCar nativo, fora dos 0x2a0 bytes).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace drv {

class BondCar;
struct WRoadNav;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // o preenchimento dos membros alinhados a 16 bytes
#endif
struct alignas(16) AIGroundVehicle {
    // AIVehicle (+0x00..+0x6f)
    int32_t vehicle_type;          // +0x000 1 no tráfego (RegisterTrafficVehicle)
    uint32_t physics;              // +0x004 (p32 PBondCarData) o carro
    uint8_t unknown008[8];
    float rot_pos[4][4];           // +0x010 GetRotPos (a identidade de 0x355bb0 no construtor)
    uint32_t spline_path;          // +0x050 (p32) o trilho (SetSplinePath)
    uint32_t nav;                  // +0x054 (p32 WRoadNav) o navegador (GetDriveToNav)
    int32_t active;                // +0x058
    int32_t valid_command;         // +0x05c
    int32_t controller_idx;        // +0x060
    uint8_t unknown064[8];
    uint32_t vtable;               // +0x06c
    // AIGroundVehicle
    int32_t controller;            // +0x070 1 jogador, 2 direção automática, 3 trilho, 4 tráfego, 5 agente
    int32_t unknown074;
    int32_t navigate_mode;         // +0x078 1 parado, 2 direto, 3 pela rede (tráfego), 4 pela rede (agente), 5 trilho
    uint32_t unknown07c;
    int32_t target_mode;           // +0x080
    int32_t attack_mode;           // +0x084 1 parar, 12 tráfego (7,5 m/s), 13-15 perseguir
    int32_t ai_command;            // +0x088
    float speed;                   // +0x08c a velocidade pedida (DoAttackMode)
    int16_t road;                  // +0x090 a rua onde conta (WRoad +0x2e), -1 nenhuma
    uint8_t unknown092[2];
    int32_t update_state;          // +0x094 0: Update ainda vai checar o nascimento neste quadro
    uint8_t unknown098[0x40];      // +0x098 (zerado no Reset)
    int8_t lane_weight_width;      // +0x0d8 3 (4 nos ônibus e caminhões)
    uint8_t unknown0d9[3];
    int32_t collision_info;        // +0x0dc
    uint8_t collision_byte;        // +0x0e0
    uint8_t unknown0e1[3];
    int32_t shocked;               // +0x0e4 o carro levou um choque (InShock) e ainda não parou
    uint8_t unknown0e8[8];
    uint32_t nav2;                 // +0x0f0 (p32 WRoadNav) os outros navegadores do construtor (os agentes)
    uint8_t unknown0f4[4];
    uint8_t unknown0f8[8];
    uint32_t nav3;                 // +0x100 (p32 WRoadNav)
    uint8_t unknown104[4];
    uint8_t unknown108[8];
    uint32_t nav4;                 // +0x110 (p32 WRoadNav)
    uint32_t nav5;                 // +0x114 (p32 WRoadNav)
    uint8_t is_sub;                // +0x118
    uint8_t use_ceiling_floor;     // +0x119
    uint8_t unknown11a[2];
    float ceiling;                 // +0x11c
    float floor;                   // +0x120
    float unknown124;              // +0x124
    float unknown128;              // +0x128
    uint8_t has_turret;            // +0x12c
    uint8_t has_machine_guns;      // +0x12d
    uint8_t has_rockets;           // +0x12e
    uint8_t has_missiles;          // +0x12f
    uint8_t accuracy;              // +0x130
    int8_t damage;                 // +0x131
    uint8_t unknown132[2];
    float rec_speed_offset;        // +0x134 1,0
    float speed_limit;             // +0x138
    float speed_scaler;            // +0x13c
    int32_t reversing;             // +0x140
    int32_t unknown144;            // +0x144 5 x a taxa de quadros
    int32_t unknown148;            // +0x148
    float ram_freq;                // +0x14c
    float lead_scaler;             // +0x150
    int32_t coll_avoidance;        // +0x154 1: olha os outros carros (CheckTrafficCollision)
    int32_t recorded_glue;         // +0x158
    int32_t unknown15c;
    int32_t steering_disabled;     // +0x160
    int16_t unknown164;            // +0x164
    uint8_t unknown166[2];
    int32_t been_in_smoke;         // +0x168
    int32_t been_in_oil;           // +0x16c
    uint8_t disabled;              // +0x170
    uint8_t unknown171[3];
    int32_t unknown174;            // +0x174
    int32_t min_sim_update;        // +0x178 não zero: a atualização mínima (longe da câmera)
    float min_sim_pos[3];          // +0x17c
    float min_sim_dir[3];          // +0x188
    int32_t collision_ahead;       // +0x194 um carro (ou o sinal) à frente
    int32_t red_light;             // +0x198 parado no sinal
    int32_t stop_now;              // +0x19c já quase parado atrás do obstáculo: freio de mão
    int32_t force_full_update;     // +0x1a0
    uint32_t primary_target;       // +0x1a4 (p32 AIGroundVehicle)
    float unknown1a8[3];           // +0x1a8
    uint8_t unknown1b4[0xc];
    float target_pos[3];           // +0x1c0
    int32_t unknown1cc;            // +0x1cc
    float unknown1d0;              // +0x1d0
    uint8_t unknown1d4[0xc];
    float direction[4];            // +0x1e0 a direção da busca do navegador
    float drive_to[3];             // +0x1f0 o ponto para onde dirigir (lido com lqc2: o w é o +0x1fc)
    int16_t unknown1fc;            // +0x1fc -1 (o alvo do IncNavPosition)
    uint8_t unknown1fe[2];
    uint32_t default_target;       // +0x200
    int32_t unknown204;            // +0x204
    int32_t unknown208;            // +0x208 1
    int32_t unknown20c;            // +0x20c
    float unknown210[3];           // +0x210
    int32_t unknown21c;            // +0x21c 1
    float min_dist_to_target;      // +0x220
    uint8_t weapons_active;        // +0x224 1
    uint8_t unknown225;
    int16_t unknown226;            // +0x226
    float rate_of_fire;            // +0x228
    int16_t unknown22c;            // +0x22c
    uint8_t unknown22e;            // +0x22e
    uint8_t unknown22f;
    int16_t unknown230;            // +0x230
    uint8_t unknown232;            // +0x232
    uint8_t unknown233;
    float turret_angle;            // +0x234
    uint8_t unknown238;            // +0x238
    uint8_t unknown239[7];
    float unknown240[3];           // +0x240
    uint32_t unknown24c;
    float unknown250[3];           // +0x250
    int16_t dir_override_timer;    // +0x25c
    uint8_t unknown25e[2];
    int32_t reverse_override;      // +0x260
    int16_t stuck;                 // +0x264
    uint8_t unknown266[2];
    int32_t unknown268;            // +0x268
    uint8_t unknown26c[4];
    float unknown270[3];           // +0x270
    int32_t unknown27c;            // +0x27c 1
    float spawn_pos[3];            // +0x280 GetSpawnPos
    int32_t can_spawn;             // +0x28c (SetCanSpawn) o lugar sorteado ainda espera
    int32_t spawn_timer;           // +0x290 o passo a partir do qual pode sumir de novo
    uint8_t unknown294[0xc];

    // Port: o carro nativo (o BondCar cujo PBondCarData +0x004 aponta).
    BondCar* car = nullptr;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
static_assert(offsetof(AIGroundVehicle, car) == 0x2a0, "os 0x2a0 bytes do PS2 vêm antes dos campos do port");
static_assert(offsetof(AIGroundVehicle, controller) == 0x70 && offsetof(AIGroundVehicle, speed) == 0x8c &&
              offsetof(AIGroundVehicle, lane_weight_width) == 0xd8 && offsetof(AIGroundVehicle, nav2) == 0xf0 &&
              offsetof(AIGroundVehicle, rec_speed_offset) == 0x134 && offsetof(AIGroundVehicle, disabled) == 0x170 &&
              offsetof(AIGroundVehicle, collision_ahead) == 0x194 && offsetof(AIGroundVehicle, target_pos) == 0x1c0 &&
              offsetof(AIGroundVehicle, direction) == 0x1e0 && offsetof(AIGroundVehicle, drive_to) == 0x1f0 &&
              offsetof(AIGroundVehicle, min_dist_to_target) == 0x220 && offsetof(AIGroundVehicle, stuck) == 0x264 &&
              offsetof(AIGroundVehicle, spawn_pos) == 0x280 && offsetof(AIGroundVehicle, spawn_timer) == 0x290,
              "o leiaute do AIGroundVehicle do PS2");
constexpr uint32_t kAIGroundVehicleSize = 0x2a0;

constexpr int kTrafficVehicles = 20;
constexpr int kPlayerGroundVehicles = 2;

// Os globais do AIVehicleController.
struct VehicleControllerGlobals {
    uint32_t instance = 0;                       // 0x327d80 (o objeto de 1 byte do Init)
    AIGroundVehicle* player[kPlayerGroundVehicles] = {};  // 0x327d90 (2 x 0x2a0)
    AIGroundVehicle* traffic[kTrafficVehicles] = {};      // 0x3282d0 (20 x 0x2a0)
    int16_t max_active_traffic = 0;              // 0x32b750 fMaxActiveTraffic (GameLoop_StartUp, EChangeMaxTraffic)
    int16_t num_active_traffic = 0;              // 0x32b752 fNumActiveTraffic
    int16_t types_chosen = 0;                    // 0x32b756 os tipos do tráfego já sorteados
    int16_t traffic_types[20] = {};              // 0x4230c8 os tipos sorteados (índices de kTrafficTypes)
    int32_t light_cycle = 0;                     // 0x3c828c o ciclo dos sinais (um a cada 300 passos)
    int32_t light_next = 0;                      // 0x3c8290 o passo da próxima troca
    float nav_spread = 0.0f;                     // 0x327c38 (SetNavigateMode, modo 4: +0x124 de cada um, 0..1,25)
    float unspawn_z = 0.0f;                      // 0x327c3c (UnSpawn: cada carro guardado 50 m adiante do anterior)
    std::vector<AIGroundVehicle*> agents;        // fAgentGroundVehicleList (AIElementController::fObj +4 motoristas)
    int32_t agent_cull_index = 0;                // fTrafficCarCullIndex (UpdateAgentGroundVehicles)
};
VehicleControllerGlobals& vehicle_controller();
// Os sete tipos de carro do tráfego (0x36c070): coupe, sedan2, hatchback, sedan, station_wagon, suv1, taxi.
extern const char* const kTrafficTypes[7];

// [driving] 0x0013de68 AIVehicle::AIVehicle e 0x0012dd80 AIGroundVehicle::AIGroundVehicle (com Reset).
void AIGroundVehicle_Construct(AIGroundVehicle& v);
// [driving] 0x0012ded8 AIGroundVehicle::Reset
void AIGroundVehicle_Reset(AIGroundVehicle& v);
// [driving] 0x0012e350 AIGroundVehicle::Update
void AIGroundVehicle_Update(AIGroundVehicle& v);
// [driving] 0x00132d80 AIGroundVehicle::DoTrafficStateUpdate
void AIGroundVehicle_DoTrafficStateUpdate(AIGroundVehicle& v);
// [driving] 0x00132808 AIGroundVehicle::Spawn(short)
void AIGroundVehicle_Spawn(AIGroundVehicle& v, int16_t max_active);
// [driving] 0x00132ba8 AIGroundVehicle::UnSpawn
void AIGroundVehicle_UnSpawn(AIGroundVehicle& v);
// [driving] 0x0012e468 AIGroundVehicle::SetNavigateMode(AICommandType::ENavigate, CARP::...)
void AIGroundVehicle_SetNavigateMode(AIGroundVehicle& v, int32_t mode, uint32_t path);
// [driving] 0x0012e6f8 AIGroundVehicle::SetAttackMode(AICommandType::EAttackMode)
void AIGroundVehicle_SetAttackMode(AIGroundVehicle& v, int32_t mode);
// [driving] 0x0012e808 AIGroundVehicle::DoAttackMode
void AIGroundVehicle_DoAttackMode(AIGroundVehicle& v);
// [driving] 0x00130508 AIGroundVehicle::DoMoveMode
void AIGroundVehicle_DoMoveMode(AIGroundVehicle& v);
// [driving] 0x00132f80 AIGroundVehicle::GetLookAhead(float)
float AIGroundVehicle_GetLookAhead(AIGroundVehicle& v, float speed);
// [driving] 0x00130140 AIGroundVehicle::UpdateNavPos(COORD3 &, float)
void AIGroundVehicle_UpdateNavPos(AIGroundVehicle& v, float look_ahead, float out[3]);
// [driving] 0x001377c8 AIGroundVehicle::CheckTrafficCollision(COORD3 *)
void AIGroundVehicle_CheckTrafficCollision(AIGroundVehicle& v, float point[4]);
// [driving] 0x00133ab0 AIGroundVehicle::DriveToPointTraffic(COORD3 *, float)
void AIGroundVehicle_DriveToPointTraffic(AIGroundVehicle& v, const float point[4], float speed);
// [driving] 0x00135530 (sem classe no SYM) TestForCollision(PhysicsObject *, COORD3 *, COORD3 *, float)
bool TestForCollision(BondCar& car, const float point[4], const float other[3], float radius);
// [driving] 0x00135978 (sem classe no SYM) GetCollisionStopPoint(PhysicsObject *, COORD3 *, COORD3 *, float)
void GetCollisionStopPoint(BondCar& car, float point[4], const float other[3], float radius);

// [driving] 0x0013e688 AIVehicleController::Init (a parte do tráfego: os 20 motoristas e os sinais).
void AIVehicleController_Init();
// [driving] 0x0013e840 AIVehicleController::Restart
void AIVehicleController_Restart();
// [driving] 0x0013ef40 AIVehicleController::UpdateVehicles (a parte do tráfego) e 0x0013ee30 UpdateTrafficVehicles.
void AIVehicleController_UpdateVehicles();
void AIVehicleController_UpdateTrafficVehicles();
// [driving] 0x0013efc8 AIVehicleController::IntersectionGreenLight(COORD3 &, int, int)
bool AIVehicleController_IntersectionGreenLight(const float pos[3], int index, int count);
// [driving] 0x0013f420 AIVehicleController::ApproveAdditionalTrafficVehicleSpawn
AIGroundVehicle* AIVehicleController_ApproveAdditionalTrafficVehicleSpawn();
// [driving] 0x0013f868 AIVehicleController::RegisterTrafficVehicle(PhysicsObject *)
void AIVehicleController_RegisterTrafficVehicle(BondCar& car);
// [driving] 0x0013f118 AIVehicleController::RegisterSimCar(PhysicsObject *) e 0x0013f1c8 RegisterSimCarList (só os
// carros de classe 3, o tráfego; as outras classes contam como pendentes).
void AIVehicleController_RegisterSimCar(BondCar& car);
void AIVehicleController_RegisterSimCarList();
// [driving] 0x0013fae0 AIVehicleController::CreateSimTrafficCars(bool): o sorteio dos tipos (uma vez) e de uma cor por
// carro; `spawn` monta cada carro na hora, como o Simulation::SpawnCarObject do PS2 (o construtor do PBondCar sorteia
// também, então a ordem importa). `num_types` é NUM_TRAFFIC_TYPES_PS2 (negativo: sem o atributo), `max_traffic`
// MAX_TRAFFIC (20 sem o atributo); `colours` dá o NUM_COLOURS de um tipo. Devolve os carros pedidos.
int AIVehicleController_CreateSimTrafficCars(int num_types, int max_traffic,
                                            const std::function<uint32_t(const char*)>& colours,
                                            const std::function<void(const char*, uint32_t)>& spawn);

// O que o motorista pediu e o port não tem, e as guardas (memória inválida no PS2).
struct GroundVehicleStats {
    uint64_t updates = 0;          // Update com o carro ativo
    uint64_t spawns = 0;           // Spawn que pôs o carro na rua
    uint64_t unspawns = 0;         // UnSpawn
    uint64_t red_lights = 0;       // CheckTrafficCollision parado num sinal
    uint64_t blocked = 0;          // CheckTrafficCollision atrás de outro carro
    uint64_t audio_resets = 0;     // AVehicle::Reset do som (UnSpawn) e o +0x154 dele (CheckTrafficCollision)
    uint64_t guards = 0;
};
GroundVehicleStats& ground_vehicle_stats();
void ground_vehicle_pending(const char* name);
int ground_vehicle_pending_count(const char* name);

// ---------------------------------------------------------------- os agentes (ground_vehicle_agent.cpp)

// AIVehicle::SetPhysicsObject (0x0013dfd0) e PBondCar::SetAIGroundVehicle (+0x54): o motorista e o carro ligados.
void AIVehicle_Attach(AIGroundVehicle& v, BondCar& car);
// [driving] 0x0013e0b8 AIVehicle::GetPosition
const float* AIVehicle_GetPosition(AIGroundVehicle& v);
// [driving] 0x0013e688 / 0x0013e840 / 0x0013ed38 AIVehicleController::Init, Restart e UpdateAgentGroundVehicles, a parte
// dos agentes; 0x0013f390 ApproveAdditionalAgentGroundVehicleSpawn; 0x0013f748 RegisterAgentGroundVehicle; 0x0013f7f0
// DeRegisterAgentGroundVehicle; 0x0013f238 DeRegisterSimCar; 0x0013ebc0 GetPlayerGroundVehiclePtr.
void AIVehicleController_InitAgents();
void AIVehicleController_RestartAgents();
void AIVehicleController_UpdateAgentGroundVehicles();
AIGroundVehicle* AIVehicleController_ApproveAdditionalAgentGroundVehicleSpawn();
void AIVehicleController_RegisterAgentGroundVehicle(BondCar& car);
void AIVehicleController_DeRegisterAgentGroundVehicle(BondCar& car);
void AIVehicleController_DeRegisterSimCar(BondCar& car);
AIGroundVehicle* AIVehicleController_GetPlayerGroundVehicle(int i);
// [driving] 0x0012e670 AIGroundVehicle::SetTargetMode / 0x0012e770 SetSecondaryTarget
void AIGroundVehicle_SetTargetMode(AIGroundVehicle& v, int32_t mode, const float point[3]);
void AIGroundVehicle_SetSecondaryTarget(AIGroundVehicle& v, uint32_t element);
// [driving] 0x00133850 UpdateStuck / 0x00131770 DoAgentStateUpdate / 0x00130050 DoWeaponUpdate
void AIGroundVehicle_UpdateStuck(AIGroundVehicle& v);
void AIGroundVehicle_DoAgentStateUpdate(AIGroundVehicle& v);
void AIGroundVehicle_DoWeaponUpdate(AIGroundVehicle& v);
// [driving] 0x00131080 AIGroundVehicle::DoAutoDriveUpdate (ground_vehicle_agent.cpp): o estado 2, a direção
// automática pela rede depois do EControlToCPU (o carro do jogador levado ao fim do cronômetro).
void AIGroundVehicle_DoAutoDriveUpdate(AIGroundVehicle& v);
// Os modos 13-15 do DoAttackMode e os de navegação dos agentes do DoMoveMode (0x0012e808, 0x00130508).
void AIGroundVehicle_DoAttackModeAgent(AIGroundVehicle& v);
void AIGroundVehicle_DoMoveModeAgent(AIGroundVehicle& v);
// [driving] 0x00133f60 DriveToPoint / 0x001351d0 GetCollNav / 0x001330d0 DrivableVec / 0x00133238 BarriersInPath
void AIGroundVehicle_DriveToPoint(AIGroundVehicle& v, float point[4], float speed);
WRoadNav* AIGroundVehicle_GetCollNav(AIGroundVehicle& v, const float dir[3], bool alt, int8_t lane);
int32_t AIGroundVehicle_DrivableVec(AIGroundVehicle& v, const float a[3], const float b[3]);
int32_t AIGroundVehicle_BarriersInPath(AIGroundVehicle& v, bool reverse);
// [driving] 0x00135b10 InCollisionRange / 0x00135cf0 GetBestWeightedLane / 0x00137070 WeightDirectLanesPosRad /
// 0x001369a0 GetDirectLaneWeights / 0x00137310 CheckAgentDirectCollision / 0x00135d88 GetRoadNetworkLaneWeights /
// 0x00135f70 CheckAgentRoadNetworkCollision
bool InCollisionRange(const float a[3], const float b[3], const float dir[3], float speed, float other, bool any_height);
int32_t AIGroundVehicle_GetBestWeightedLane(AIGroundVehicle& v);
void AIGroundVehicle_WeightDirectLanesPosRad(AIGroundVehicle& v, const float a[3], const float dir[3],
                                             const float perp[3], float point[3], float speed, float radius);
void AIGroundVehicle_GetDirectLaneWeights(AIGroundVehicle& v, const float point[3]);
void AIGroundVehicle_CheckAgentDirectCollision(AIGroundVehicle& v, float point[4]);
void AIGroundVehicle_GetRoadNetworkLaneWeights(AIGroundVehicle& v);
void AIGroundVehicle_CheckAgentRoadNetworkCollision(AIGroundVehicle& v, float point[4]);
// [driving] 0x00226bd8 WRoadNav::PathShareRoadSegment / 0x00226c88 PathForwardRoadSegment
bool WRoadNav_PathShareRoadSegment(const WRoadNav& n, int16_t segment);
int32_t WRoadNav_PathForwardRoadSegment(const WRoadNav& n, int16_t segment, int32_t* out);
// O AIZoneController (as zonas de fumaça e de óleo): [driving] 0x00140110 Init, 0x00140218 InZone, 0x001402f8
// RegisterZone.
void AIZoneController_Init();
bool AIZoneController_InZone(float extra, int zone, const float point[3]);
void AIZoneController_RegisterZone(int32_t type, const float point[3]);
// A tabela das zonas como no PS2 (fZone 0x32b768: 30 x 0x1c bytes) e o fActiveZones, para a conferência (parte 17).
uint8_t* AIZoneController_Zones();
int32_t& AIZoneController_Active();

// Port: o motorista do carro (o p32 de PBondCarData +0x2e4), ou nulo.
AIGroundVehicle* ai_of(const BondCar& car);
BondCar* car_of(const AIGroundVehicle& v);
WRoadNav* nav_of(const AIGroundVehicle& v);

}  // namespace drv
