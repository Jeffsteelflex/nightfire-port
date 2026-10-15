#pragma once
// Os gatilhos da pista e os eventos que eles disparam (WTrigger e WTriggerManager do DRIVING.ELF, WRender.obj; os
// eventos de DTuningFile.obj; docs/driving.md seção 18). Cada gatilho (o 'Trgr' do grupo 'Map ' do CARP da pista, 0x40
// bytes) é uma esfera, um cilindro ou uma caixa com uma lista de eventos ('el'); a cada quadro da simulação
// (ESimFrameUpdate) WTriggerManager::Update passa cada corpo rígido acordado pelos gatilhos das células da grade em
// volta dele, e um corpo que toca um gatilho dispara os eventos dele (WTrigger::FireEvents: cada evento é criado pela
// função do seu número na tabela de RegisterEvent e roda depois, quando o EventManager o apaga).
//
// Portado do MIPS do PS2 (a verdade); o nightfire-cxbx (NightfireResearch, src/driving/world/Trigger.cpp e
// TriggerManager.cpp, a versão de Xbox) deu os nomes e a estrutura. Só o que o carro e o tráfego usam: os corpos
// rígidos (Process e CheckCollide do RigidBody, as três formas), FireEvents e o Restart que dispara os EStartMission.
// Os corpos simples, as instâncias dos trilhos (RPathEngine) e os raios (RayShell) não existem no port; a validação
// dos fluxos de som do Restart também não (contados em trigger_stats()).
//
// Os eventos: o port executa o ESpawnTraffic (o número 28: um carro do tráfego parado ganha a rua no gatilho, com a
// velocidade do evento) e o EFireRandomTrigger (203: dispara um de até quatro gatilhos, sorteado pelo RRandom; em Paris
// o gatilho 703 tem 18 deles, que põem o tráfego nos gatilhos dos ESpawnTraffic) e o EFireTriggerSpeedCondition (171
// e 172: dispara um gatilho pela velocidade de quem tocou) e o EWakeupSmackable (35: o objeto de cena do gatilho vira um
// corpo rígido que o carro derruba, driving/game/smackable.h); os outros números contam em
// trigger_stats().events por número (os EStartMission, os sons, os efeitos, a missão...) e são descartados. Os criados ficam numa fila que trigger_run_events() executa (o EventManager
// do PS2 os executa na ordem em que foram criados, depois do quadro que os criou).
//
// Leiaute do PS2 (static_assert); os globais com os endereços do PS2 nos comentários (a parte 14 do nf_driving_check).

#include <cstdint>
#include <map>
#include <vector>

#include "driving/carp_runtime.h"

namespace drv {

struct RigidBody;

struct WTrigger {
    float position[3];     // +0x00 o centro de baixo
    float radius;          // +0x0c da esfera ou do cilindro (com a posição: o Coord4 que a grade guarda)
    uint8_t unknown10;
    uint8_t shape;         // +0x11 1 caixa, 2 esfera, 3 cilindro
    uint16_t flags;        // +0x12 kTrig*
    float height;          // +0x14
    uint32_t events;       // +0x18 (p32 TriggerEvents) nulo: nenhum
    uint32_t query_stamp;  // +0x1c o último Process que olhou para ele
    float right[3];        // +0x20
    float width;           // +0x2c da caixa, ao longo de right
    float forward[3];      // +0x30
    float depth;           // +0x3c da caixa, ao longo de forward
};
static_assert(sizeof(WTrigger) == 0x40);

// As bandeiras de um gatilho (os nomes dos bits de toque são do nightfire-cxbx).
enum : uint16_t {
    kTrigEnabled = 0x0001,
    kTrigOnce = 0x0002,          // FireEvents o desliga
    kTrigRigidType1 = 0x0004,    // tocado pelos corpos de tipo 1 (os carros do jogador)
    kTrigRigidType2 = 0x0008,    // pelos de tipo 2
    kTrigOthers = 0x0040,        // pelos outros (o tráfego é do tipo 3)
    kTrigInstances = 0x0200,     // pelas instâncias dos trilhos; Init lhe tira kTrigWhileActive
    kTrigWhileActive = 0x0400,   // só com o gerente ativo
    kTrigDirectional = 0x0800,   // só por quem anda no sentido de forward
    kTrigRotated = 0x1000,       // o "para cima" da caixa é forward x right
    kTrigIgnored = 0x2000,
};

// Um evento de uma lista ('el'): o número e onde estão os dados (a partir do próprio registro).
struct TriggerEvent {
    int32_t type;
    uint32_t unknown04;
    int32_t data_offset;
    uint32_t unknown0c;
};
static_assert(sizeof(TriggerEvent) == 0x10);
// A lista: a contagem e depois os eventos.
struct TriggerEvents {
    int32_t count;
    uint8_t unknown04[0xc];
};
static_assert(sizeof(TriggerEvents) == 0x10);

// O que o código de um evento lê de quem o disparou (0x64 bytes em 0x32e708; o nome é do nightfire-cxbx).
struct EventDynamicData {
    uint32_t instance;      // +0x00 (p32 CARP::Instance)
    uint32_t unknown04;
    uint32_t scene_obj;     // +0x08
    int32_t instance_index; // +0x0c FireEvents: -1
    float position[4];      // +0x10 o do gatilho, w 1
    uint8_t unknown20[0x10];
    uint32_t trigger;       // +0x30 (p32 WTrigger)
    uint32_t weapon;        // +0x34
    uint32_t flag;          // +0x38 o primeiro argumento do FireEvents (gravado como palavra)
    int32_t index;          // +0x3c o segundo
    uint32_t unknown40;
    uint32_t has_ray;       // +0x44 um raio tocando o gatilho
    float ray_start[3];     // +0x48
    float ray_end[3];       // +0x54
    float ray_value;        // +0x60
};
static_assert(sizeof(EventDynamicData) == 0x64);

// Um evento criado e ainda não executado (todos entram na fila; driving/game/events.h executa os portados).
struct PendingEvent {
    int32_t type;
    uint32_t trigger;  // (p32 WTrigger) o EventDynamicData +0x30 da criação
    float speed;       // o ESpawnTraffic: os dados do evento +4 (os outros: 0)
    uint32_t data;     // (p32) os dados do evento na lista 'el'
    uint32_t flag;     // o EventDynamicData +0x38 da criação
    int32_t index;     // o +0x3c
    // A imagem do objeto do PS2 (events.h): o tamanho do operator new, as palavras (obj[0] a tabela virtual da classe)
    // e quais a criadora escreveu (bit k: obj[k]; as outras ficam com o que o anel tinha).
    uint32_t size = 0;
    uint32_t written = 0;
    uint32_t obj[20] = {};  // (o maior: o ESpawnExplosion, 0x50 bytes)
};

// O EFireRandomTrigger (0x2c bytes no PS2; os campos +4..+0x28): quatro gatilhos com as suas chances (em %, somadas na
// ordem), o +0x38 e o +0x3c do EventDynamicData da criação.
struct FireRandomTrigger {
    uint32_t trigger[4];  // (p32 WTrigger) nulo: nenhum
    float chance[4];
    uint32_t flag;
    int32_t index;
};

// Os globais do WTriggerManager.
struct TriggerManagerGlobals {
    uint32_t manager = 0;          // 0x33aa88 fgTriggerManager (o objeto de 12 bytes do Init)
    int32_t count = 0;             //   +0x00
    uint32_t triggers = 0;         //   +0x04 (p32 WTrigger) os do CARP
    int32_t active = 0;            //   +0x08 Restart liga; EPlayerWin e EPlayerLose desligam
    uint32_t data_size = 0;        // 0x33aa8c
    uint32_t data = 0;             // 0x33aa90 TriggerData (p32: os gatilhos do CARP, que o Restart reescreve)
    std::vector<uint8_t> copy;     // 0x33aa94 os gatilhos como Init os achou (Restart os põe de volta)
    uint32_t query_stamp = 0;      // 0x33aa98
    EventDynamicData event_data{}; // 0x32e708
    std::vector<PendingEvent> pending;  // a fila dos eventos criados (port)
};
TriggerManagerGlobals& trigger_manager();

// Os contadores do port: os eventos criados por número, os gatilhos disparados, o que não foi portado.
struct TriggerStats {
    std::map<int32_t, uint64_t> events;  // por número (o 28 é o ESpawnTraffic)
    uint64_t fired = 0;                  // FireEvents
    uint64_t traffic_spawned = 0;        // ESpawnTraffic que achou um carro parado
    uint64_t streams = 0;                // os EStreamEvent que o Restart validaria (o som)
    uint64_t rays = 0;                   // os raios dos tiros (RayShell) conferidos pelo Process(int)
    uint64_t ray_hits = 0;               // os gatilhos que um raio tocou (o FireEvents com o raio)
    std::map<int32_t, uint64_t> ray_triggers;  // por gatilho (o índice): os toques dos raios
    uint64_t guards = 0;
};
TriggerStats& trigger_stats();

WTrigger* trigger_at(int index);

// [driving] 0x0022cbe0 WTriggerManager::Init(UData *): os gatilhos de `trgr` (o 'Trgr' do 'Map '; nulo: nenhum).
void WTriggerManager_Init(UData* trgr);
// [driving] 0x0022cd30 WTriggerManager::Restart (sem a validação dos fluxos de som).
void WTriggerManager_Restart();
// [driving] 0x0022f0c0 WTriggerManager::Update: os corpos rígidos acordados e os raios dos tiros (os corpos simples e
// os trilhos não são conferidos no port).
void WTriggerManager_Update();
// [driving] 0x0022def0 WTriggerManager::Process(int): o raio ativo `index` (RayShell::GetActiveRayShell) contra os
// gatilhos das células da grade que o segmento cruza (docs/driving.md seção 26).
void WTriggerManager_ProcessRay(int index);
// [driving] 0x0022e3a0 WTriggerManager::CheckCollide(float, COORD4 *, WTrigger *): o segmento `seg` (as duas pontas,
// w 1) com a meia-largura `half` contra o gatilho.
bool WTriggerManager_CheckCollideSeg(float half, const float seg[2][4], const WTrigger& t);
// [driving] 0x0022caa8 WTrigger::TestDirection(COORD4 *): o segmento não anda contra forward.
bool WTrigger_TestDirectionSeg(const WTrigger& t, const float seg[2][4]);
// [driving] 0x00231cc8 WWorldMath::NearestPointLine3D(COORD4 &, COORD4 *, COORD4 &) e 0x002318b0 NearestPointLine2D
// (o y 0): o ponto da reta do segmento mais perto de `p` (sem limitar ao segmento).
void WWorldMath_NearestPointLine3D(const float p[4], const float seg[2][4], float out[4]);
void WWorldMath_NearestPointLine2D(const float p[4], const float seg[2][4], float out[4]);
// [driving] 0x0022d240 WTriggerManager::Process(int, RigidBody *)
void WTriggerManager_Process(int index, RigidBody* body);
// [driving] 0x0022eb28 WTriggerManager::CheckCollide(RigidBody *, WTrigger *)
bool WTriggerManager_CheckCollide(const RigidBody* body, const WTrigger& t);
// [driving] 0x0022c890 WTrigger::FireEvents(bool, int, CARP::Instance *) (o bool gravado como palavra)
void WTrigger_FireEvents(WTrigger& t, uint32_t flag, int index, uint32_t instance);
// [driving] 0x0022ca18 WTrigger::HasEvent(int)
bool WTrigger_HasEvent(const WTrigger& t, int32_t type);
// [driving] 0x0022ca68 WTrigger::TestDirection(COORD3 &): o vetor não anda contra forward.
bool WTrigger_TestDirection(const WTrigger& t, const float v[3]);

// Port: esquece os gatilhos e a fila (o fim de uma pista, antes de o CARP dela ser liberado).
void WTriggerManager_Reset();

// Executa os eventos criados, na ordem (o EventManager do PS2, que os executa ao apagá-los).
void trigger_run_events();
// [driving] 0x0015c080 ESpawnTraffic::~ESpawnTraffic: o primeiro carro do tráfego parado (o corpo congelado) vai para o
// gatilho, de frente para o forward dele, com a velocidade `speed` (o momento = a frente x speed x massa).
void ESpawnTraffic_Execute(const WTrigger& t, float speed);
// [driving] 0x0014c688 EFireRandomTrigger::~EFireRandomTrigger: um número do RRandom (0..100) escolhe o primeiro
// gatilho cuja chance somada o alcança (nenhum além da quarta) e dispara os eventos dele com o flag e o índice do evento.
void EFireRandomTrigger_Execute(const FireRandomTrigger& e);
// Os campos do EFireRandomTrigger a partir dos dados do evento (a criadora 0x14c7b8).
FireRandomTrigger FireRandomTrigger_FromData(const uint8_t* data, uint32_t flag, int32_t index);

// O EFireTriggerSpeedCondition (171 e 172, a criadora 0x14c9c0; 0x18 bytes no PS2): o gatilho, a velocidade, "menor
// que" e o +0x38 e o +0x3c do EventDynamicData (o índice é a vaga do corpo que tocou o gatilho).
struct FireTriggerSpeed {
    uint32_t trigger;  // (p32 WTrigger)
    float speed;
    uint32_t less;
    uint32_t flag;
    int32_t index;
};
// [driving] 0x0014c8c8 EFireTriggerSpeedCondition::~EFireTriggerSpeedCondition: com a velocidade do corpo da vaga
// `index` (o módulo pela FPU) abaixo (less) ou acima da do evento, dispara os eventos do gatilho.
void EFireTriggerSpeedCondition_Execute(const FireTriggerSpeed& e);
FireTriggerSpeed FireTriggerSpeed_FromData(const uint8_t* data, uint32_t flag, int32_t index);

}  // namespace drv
