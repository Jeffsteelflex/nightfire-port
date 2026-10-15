#pragma once
// Os elementos da IA de uma missão (AIElementController e AIElementTracking do DRIVING.ELF, AIElementController.obj;
// docs/driving.md seção 21): os inimigos e os veículos que a missão acorda e põe para dormir.
//
// O 'AIEl' do 'Map ' da pista traz um registro de 0x100 bytes por elemento (CARP::AIElement): a matriz de partida, as
// bandeiras, a dificuldade em que existe, o tipo (0 um personagem a pé, 1 um veículo), a vida, o nome do veículo, os
// passageiros (até três, com o modelo, a arma e a vida) e o primeiro comando ('AICo'). No GameLoop_StartUp o
// AIElementController::Construct copia os registros (o controlador trabalha na cópia; RedirectCarpElement leva o
// endereço do CARP, que as regras e os eventos guardam, à cópia) e monta um AIElementTracking (0x50 bytes) por elemento:
// a distância ao carro do jogador, quanto ele está à frente da câmera, os raios de acordar (200 m) e de dormir (220 m),
// a posição, a velocidade e a vida.
//
// A cada passo da simulação (ESimFrameUpdate, antes do SMissionManager::Update) o Update atualiza os acompanhamentos (um
// por passo, os longe a cada 2-4), conta quem quer acordar (os elementos dentro do raio que nenhum evento desativou,
// bit 2 das bandeiras) e, se cabem nos limites (32 personagens, 6 veículos, 16 helicópteros, ou o que o mundo diz),
// acorda todos; senão ordena os candidatos pelo quanto estão à frente da câmera (o qsort do newlib) e acorda os que
// cabem, pondo os outros para dormir. Acordar um veículo (WakeUp) tira do caminho os carros do tráfego perto do ponto,
// cria o PBondCar (Simulation::SpawnCarObject: classe 2, um agente com motorista, com o bit 3 das bandeiras; classe 4,
// um carro parado, sem), põe-no no ponto, liga a vida dele ao registro (+0x4c) e cria o EAICommand do primeiro comando;
// dormir (GoToSleep) guarda a matriz do carro no registro e apaga o carro; morto fica morto (estado 2).
//
// Portado do MIPS do PS2 (a verdade). O nightfire-cxbx (NightfireResearch) deu os nomes dos métodos e o mapa dos campos
// do controlador (driving-symbol-matching/results/struct-evidence/AIElementController.md), não o código.
//
// O leiaute é o do PS2: o controlador (0x58 bytes), a cópia dos registros, os acompanhamentos e as duas listas de
// trabalho ficam em memória com ponteiros p32, para a parte 17 do nf_driving_check comparar com o original.
//
// Divergências (cada uma também na função):
//  - os personagens (os passageiros dos veículos, os inimigos a pé: AICharacterEnemy*, ActActorDatabase, Human) e os
//    helicópteros (AIHelicopter, Simulation::SpawnHelicopterObject) não foram portados: o WakeUp conta-os como
//    pendentes (ai_element_stats()) e não os cria; os registros e os contadores que eles mudariam ficam como estão;
//  - o PBondCar que o WakeUp cria e o GoToSleep apaga vem de quem monta os carros (o nf_driving: o modelo, o render),
//    pelos ganchos de ai_car_factory(); sem fábrica o carro não nasce (contado);
//  - o RSceneObj::PreLoad do construtor (o modelo do veículo carregado antes) e o ActCharacter::LoadCharacter dos
//    passageiros ficam com quem monta os carros (contados);
//  - guardas (contadas): um índice de elemento fora da cópia (o PS2 leria fora do vetor).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "driving/carp_runtime.h"

namespace drv {

class BondCar;
struct AIGroundVehicle;
struct PendingEvent;

// Um passageiro do veículo (0x20 bytes, CARP::AIElement +0x80 + 0x20 x i).
struct AIPassenger {
    int32_t type;          // +0x00 a classe do personagem (3, 4, 0x10, 0x11...)
    int32_t model;         // +0x04 o índice do modelo (a tabela 0x327ba8)
    int32_t weapon;        // +0x08 o índice da arma (a tabela 0x327b90)
    int32_t unknown0c;     // +0x0c
    int32_t unknown10;     // +0x10
    float health;          // +0x14
    int32_t unknown18;     // +0x18
    int32_t accuracy;      // +0x1c 2/4: x4, 3/5: x9 (o +0x64 do personagem)
};
static_assert(sizeof(AIPassenger) == 0x20);

// CARP::AIElement (0x100 bytes; o registro do 'AIEl' e a cópia do controlador).
struct AIElement {
    float matrix[4][4];          // +0x00 a matriz de partida (a linha 2 a frente, a linha 3 a posição)
    uint16_t flags;              // +0x40 kElem*
    uint16_t difficulty;         // +0x42 em que dificuldade existe (0 sempre; 1-6: ver WakeUp)
    int32_t type;                // +0x44 0 personagem, 1 veículo
    int32_t state;               // +0x48 0 dormindo, 1 acordado, 2 morto
    float health;                // +0x4c (o +0x54 do PBondCar aponta para cá)
    uint32_t ai;                 // +0x50 (p32) o AIVehicle (veículo) ou o AICharacter (personagem)
    // +0x54: o veículo tem o nome (até +0x74); o personagem, o Human (+0x54), a classe (+0x58), o modelo (+0x5c), a
    // arma (+0x60), +0x70, +0x74.
    char name[0x20];             // +0x54
    uint8_t colour;              // +0x74
    uint8_t unknown75[2];
    uint8_t heli;                // +0x77 o veículo é um helicóptero
    int32_t unknown78;           // +0x78
    int32_t passengers;          // +0x7c
    AIPassenger passenger[3];    // +0x80
    uint32_t characters[3];      // +0xe0 (p32 AICharacter) os passageiros vivos
    uint32_t humans[3];          // +0xec (p32 Human)
    uint32_t command;            // +0xf8 (p32 'AICo') o primeiro comando
    uint32_t physics;            // +0xfc (p32 PBondCarData) o carro
};
static_assert(sizeof(AIElement) == 0x100);
static_assert(offsetof(AIElement, flags) == 0x40 && offsetof(AIElement, health) == 0x4c &&
              offsetof(AIElement, name) == 0x54 && offsetof(AIElement, colour) == 0x74 &&
              offsetof(AIElement, heli) == 0x77 && offsetof(AIElement, passengers) == 0x7c &&
              offsetof(AIElement, passenger) == 0x80 && offsetof(AIElement, characters) == 0xe0 &&
              offsetof(AIElement, humans) == 0xec && offsetof(AIElement, command) == 0xf8 &&
              offsetof(AIElement, physics) == 0xfc);
// As bandeiras (+0x40).
enum : uint16_t {
    kElemFixedPose = 0x0001,  // o registro fica com a matriz de partida (sem ele, GoToSleep e SyncRuleInfo
                              // copiam a matriz do carro)
    kElemInactive = 0x0002,   // desativado (EDeactivateAIElement; EActivateAIElement o tira)
    kElemPriority = 0x0004,   // acorda mesmo fora do raio e sem lugar (CompareInRangePrio)
    kElemAgent = 0x0008,      // o veículo é um agente (classe 2); sem: classe 4
    kElemCounted = 0x0010,    // já contou nos inimigos da missão (o +0x3dc do gerente)
    kElemMightBeDead = 0x0020,// o carro perdeu pneus ou o motorista: "pode estar morto"
    kElemNoScore = 0x0040,    // não pontua (SetScoreable 0)
};

// AIElementTracking (0x50 bytes).
struct AIElementTracking {
    int32_t next_update;     // +0x00 o passo da próxima atualização
    int32_t period;          // +0x04 1 a 4 passos
    float distance;          // +0x08 ao carro do jogador
    float facing;            // +0x0c quanto está à frente da câmera (0..1)
    float wake_range;        // +0x10 200
    float sleep_range;       // +0x14 220
    int32_t in_range;        // +0x18 dentro do raio de acordar
    int32_t wake;            // +0x1c o Update decidiu acordar
    uint32_t element;        // +0x20 (p32 AIElement) a cópia
    uint32_t controller;     // +0x24 (p32 AIElementControllerData)
    uint8_t unknown28[8];
    float pos[3];            // +0x30
    float speed;             // +0x3c (o w da posição)
    float health;            // +0x40 a do passo anterior
    uint8_t unknown44[0xc];
};
static_assert(sizeof(AIElementTracking) == 0x50);
static_assert(offsetof(AIElementTracking, pos) == 0x30 && offsetof(AIElementTracking, speed) == 0x3c &&
              offsetof(AIElementTracking, health) == 0x40);

// O AIElementController (0x58 bytes, AIElementController::fObj 0x327c30).
struct AIElementControllerData {
    int32_t max_characters;   // +0x00 MAX_ACTIVE_CHARACTERS (32)
    int32_t max_vehicles;     // +0x04 MAX_ACTIVE_GROUND_VEHICLES (6)
    int32_t max_helis;        // +0x08 MAX_ACTIVE_HELICOPTERS (16)
    uint32_t elements;        // +0x0c (p32 AIElement) a cópia
    uint32_t carp;            // +0x10 (p32 AIElement) os registros do 'AIEl'
    uint32_t trackings;       // +0x14 (p32 AIElementTracking)
    uint32_t trackings_alloc; // +0x18
    float stats[7];           // +0x1c GetStats: a vida maior, a menor, a maior queda, a velocidade maior, a menor, a
                              //        distância maior, a menor (zerados ou 1e6 a cada Update)
    uint32_t wake_list;       // +0x38 (p32 int32) os índices que querem acordar
    uint32_t pair_list;       // +0x3c (p32) pares {índice, o "à frente" do acompanhamento}
    int32_t count;            // +0x40
    int32_t characters_awake; // +0x44
    int32_t vehicles_awake;   // +0x48
    int32_t helis_awake;      // +0x4c
    int32_t vehicles_wanted;  // +0x50 (OkToSpawnHench)
    int32_t might_be_dead;    // +0x54 CalcMightAsWellBeDead
};
static_assert(sizeof(AIElementControllerData) == 0x58);

// O controlador do port (nulo sem missão).
AIElementControllerData* ai_controller();
AIElement* ai_element(int index);              // a cópia
AIElementTracking* ai_tracking(int index);
int ai_element_index(uint32_t carp_element);   // o índice de um registro do 'AIEl' (-1: não é um)

// Quem monta os carros dos elementos (o nf_driving): o PBondCar de classe `car_class` com o nome, a cor, a frente e o
// ponto (o Simulation::SpawnCarObject: nulo sem lugar), e o fim dele (Simulation::DeleteCarObject).
struct AICarFactory {
    std::function<BondCar*(int32_t car_class, const char* name, uint8_t colour, const float dir[3],
                           const float pos[3])> spawn;
    std::function<void(BondCar* car)> remove;
};
AICarFactory& ai_car_factory();
// O carro de um +0xfc (o p32 do PBondCarData do carro; nulo se não for um carro da simulação).
BondCar* ai_car_from_physics(uint32_t physics);

// [driving] 0x0012be48 AIElementController::Construct(UData *) e 0x0012b8b0 o construtor: a cópia dos registros do
// 'AIEl' `aiel` e os acompanhamentos; `limits` dá MAX_ACTIVE_* da coleção "world" (nulo ou ausente: 32, 6, 16).
void AIElementController_Construct(UData* aiel, const std::function<int32_t(const char*, bool*)>& limits = {});
// [driving] 0x0012be90 AIElementController::Destruct
void AIElementController_Destruct();
// [driving] 0x0012bec8 AIElementController::Update
void AIElementController_Update();
// [driving] 0x0012cb08 AIElementController::WakeUp(CARP::AIElement &)
void AIElementController_WakeUp(AIElement& e);
// [driving] 0x0012c740 AIElementController::GoToSleep(CARP::AIElement &)
void AIElementController_GoToSleep(AIElement& e);
// [driving] 0x0012da78 AIElementController::ForceVehicleToSleep(AIVehicle *)
void AIElementController_ForceVehicleToSleep(uint32_t vehicle);
// [driving] 0x0012daf0 AIElementController::SyncRuleInfo(CARP::AIElement &): a matriz do carro no registro.
void AIElementController_SyncRuleInfo(AIElement& e);
// [driving] 0x0012c678 AIElementController::CalcMightAsWellBeDead e 0x0012c738 GetNumMightAsWellBeDead
void AIElementController_CalcMightAsWellBeDead();
int32_t AIElementController_GetNumMightAsWellBeDead();
// [driving] 0x0012c4c0 AIElementController::EveryoneChill: os carros param (o fim da missão).
void AIElementController_EveryoneChill();
// [driving] 0x0012c448 AIElementController::EveryoneToSleep
void AIElementController_EveryoneToSleep();
// [driving] 0x0012c5d0 AIElementController::OkToSpawnHench
bool AIElementController_OkToSpawnHench();
// [driving] 0x0012c608 AIElementController::RedirectCarpElement / 0x0012c640 GetCarpElementTracking: o registro do
// CARP para a cópia e o acompanhamento (o índice é |(e - base) >> 8|, sem conferir o fim: guarda no port).
AIElement* AIElementController_RedirectCarpElement(uint32_t carp_element);
AIElementTracking* AIElementController_GetCarpElementTracking(uint32_t carp_element);
// [driving] 0x0012dc18 AIElementController::CompareInRangePrio: 1 sem o bit de prioridade.
int32_t AIElementController_CompareInRangePrio(const AIElement& e);

// [driving] 0x0012af48 AIElementTracking::AIElementTracking(CARP::AIElement &, AIElementController *)
void AIElementTracking_Construct(AIElementTracking& t, uint32_t element, uint32_t controller);
// [driving] 0x0012afb8 AIElementTracking::Update(COORD3 &): `player` é a posição do carro do jogador.
void AIElementTracking_Update(AIElementTracking& t, const float player[4]);
// [driving] 0x0012b440 AIElementTracking::UpdatePosition(COORD3 &)
void AIElementTracking_UpdatePosition(AIElementTracking& t, const float player[4]);
// [driving] 0x0012b100 AIElementTracking::SetWakeRange(float)
void AIElementTracking_SetWakeRange(AIElementTracking& t, float range);
// [driving] 0x0012b138 AIElementTracking::GetProperty(char *): as propriedades que as regras perguntam.
bool AIElementTracking_GetProperty(AIElementTracking& t, const char* name);

// Os eventos da IA (os destrutores do PS2; driving/game/events.h): devolve falso para um número que não é daqui.
bool ai_event_execute(const PendingEvent& e);
// [driving] 0x00145fc8 EAICommand::EAICommand: o evento que o WakeUp cria (no fim da fila, como no anel do PS2).
void EAICommand_Create(uint32_t command, uint32_t trigger_element, uint32_t element, uint32_t vehicle,
                       uint32_t trigger, uint32_t simple, uint32_t slot);

// [driving] newlib qsort (0x002a8198): o algoritmo do BSD (med3, partição em três, a inserção abaixo de 7), para a
// ordem dos empates ser a do PS2.
void newlib_qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*));

// O que não foi portado e as guardas.
struct AIElementStats {
    uint64_t updates = 0;           // Update
    uint64_t wakes = 0;             // WakeUp que acordou
    uint64_t sleeps = 0;            // GoToSleep
    uint64_t cars_spawned = 0;      // os PBondCar criados
    uint64_t no_factory = 0;        // sem fábrica ou sem lugar (SpawnCarObject nulo)
    uint64_t characters = 0;        // personagens (passageiros, a pé) que o PS2 criaria
    uint64_t helis = 0;             // helicópteros que o PS2 criaria
    uint64_t commands = 0;          // EAICommand executados
    uint64_t guards = 0;
};
AIElementStats& ai_element_stats();
void ai_element_pending(const char* name);
int ai_element_pending_count(const char* name);

}  // namespace drv
