#pragma once
// O trilho (docs/driving.md seção 23): um carro levado por um caminho do CARP em vez da sua física. No PS2 a direção
// automática do carro do jogador (e a dos carros da IA que andam por trilhos) passa pelo AIGroundVehicle do carro: o
// EControlToSpline (o evento 202) chama o AIGroundVehicle::EngageSplinePath, que põe o carro na classe 1 (a CPU), o
// controlador no estado 3 (o trilho) e cria o AISplinePath (0xb0 bytes: uma instância própria e o seu estado de
// animação de caminho, com o RPathHandle do RPathEngine, driving/render/path_engine.h, e a matriz de aplicação do 'AISp').
// A cada passo o EAnimUpdate move o RPathHandle, o EAIUpdate (AIGroundVehicle::Update no estado 3: DoSplinePathUpdate)
// põe o corpo rígido do carro na pose do caminho com a velocidade dele, e o PBondCar::Simulate troca a física do carro
// pelo ProcessSplinePhysics (as rodas e as molas, para o desenho e o som; o corpo continua na simulação entre as poses).
//
// Em Paris o carro do jogador da seção do helicóptero é um PBondCar com o modelo "helicopter" (IS_HELICOPTER, o
// SECONDARY_TYPE vanquish: o ECustomTransition troca o tipo, game/heli_section.h) no trilho 'AISp' 0 ("path_Helicopter02",
// o caminho 'pt' 1: 1264 de tempo paramétrico, a 0,5 por 1/60 s no acelerador 0,71 do EPathSetDesiredThrottleStatic do
// gatilho 685: ~59 s), depois o 1 ("path_Heli_Hover", o ESetNextAISpline do gatilho 742).
//
// Este arquivo é do porte da seção do helicóptero; o AIGroundVehicle e o AIElementController são do porte da IA
// (driving/ai/): as funções do trilho do AIGroundVehicle e o AISplinePath, que a IA também usa nos seus carros, ficam
// aqui (os mesmos nomes do PS2); os ramos dos eventos que pedem um elemento da IA (o AIElementController::
// RedirectCarpElement) são contados e esperam o porte da IA.
//
// Portado do MIPS do PS2, conferido na parte 19 do nf_driving_check.

#include <cstdint>
#include <functional>

#include "driving/render/path_engine.h"

namespace drv {

class BondCar;
struct AIGroundVehicle;
struct PendingEvent;

// AISplinePath (0xb0 bytes; o construtor 0x0013da80).
struct AISplinePath {
    float instance[4][4];   // +0x00 a instância que o RPathHandle move (a identidade; o byte +0xc = 0x10, +0xd = 0xff)
    ProcAnimState state;    // +0x40 o estado de caminho (tipo 2, andando, velocidade 0,5, o caminho do 'AISp')
    uint32_t handle;        // +0x60 (p32 RPathHandle)
    uint8_t unknown64[0xc];
    float apply[4][4];      // +0x70 a matriz de aplicação do 'AISp' (GetApplyTransform)
};
static_assert(sizeof(AISplinePath) == 0xb0);

// [driving] 0x0013da80 AISplinePath::AISplinePath(CARP::AISpline *) (com o tempo da simulação para o RPathHandle)
void AISplinePath_Construct(AISplinePath& p, const AISplineRec* spline);
// [driving] 0x0013dc78 AISplinePath::~AISplinePath
void AISplinePath_Destruct(AISplinePath& p);
// [driving] 0x0013dcc0 AISplinePath::Reset(CARP::AISpline *)
void AISplinePath_Reset(AISplinePath& p, const AISplineRec* spline);
// [driving] 0x0013dd28 AISplinePath::SetNext(CARP::AISpline *)
void AISplinePath_SetNext(AISplinePath& p, const AISplineRec* spline);

// [driving] 0x0013dfd8 AIVehicle::SetSplinePath(CARP::AISpline *) (nulo: destrói o trilho)
void AIVehicle_SetSplinePath(AIGroundVehicle& v, const AISplineRec* spline);
// [driving] 0x0013e058 AIVehicle::SetNextSplinePath(CARP::AISpline *)
void AIVehicle_SetNextSplinePath(AIGroundVehicle& v, const AISplineRec* spline);
// [driving] 0x00131508 AIGroundVehicle::EngageSplinePath(CARP::AISpline *)
void AIGroundVehicle_EngageSplinePath(AIGroundVehicle& v, const AISplineRec* spline);
// [driving] 0x00131590 AIGroundVehicle::DisengageSplinePath
void AIGroundVehicle_DisengageSplinePath(AIGroundVehicle& v);
// [driving] 0x00130e88 AIGroundVehicle::EngageAutoDrive: a direção automática (a classe 1, os controles zerados, o
// estado 2, o ataque 15, a navegação 4 com o WRoadNav posto no corpo). O estado 2 (DoAutoDriveUpdate, a direção pela
// rede de estradas) não foi portado: o carro fica sem quem o dirija até o DisengageAutoDrive.
void AIGroundVehicle_EngageAutoDrive(AIGroundVehicle& v);
// [driving] 0x00130f98 AIGroundVehicle::DisengageAutoDrive: o jogador de volta (a classe 0, o estado 1, o ataque 1, a
// navegação 1, sem alvo).
void AIGroundVehicle_DisengageAutoDrive(AIGroundVehicle& v);
// [driving] 0x00131018 AIGroundVehicle::DoPlayerLogic: o contador +0x164 desce até 0 (a posição do corpo lida e não
// usada).
void AIGroundVehicle_DoPlayerLogic(AIGroundVehicle& v);
// [driving] 0x00131620 AIGroundVehicle::DoSplinePathUpdate
void AIGroundVehicle_DoSplinePathUpdate(AIGroundVehicle& v);
// [driving] 0x00192498 PBondCar::ProcessSplinePhysics
void PBondCar_ProcessSplinePhysics(BondCar& car);
// [driving] 0x00145d00 EPathUtil_GetFromInstanceOrAI(CARP::Instance *, CARP::AIElement *): o RPathHandle de uma
// instância (o do seu objeto de cena) ou do trilho de um carro (0: o do jogador); nulo sem.
RPathHandle* EPathUtil_GetFromInstanceOrAI(uint32_t instance, uint32_t element);

// O carro do jogador (o Simulation +0xdcc: o primeiro carro) e o seu AIGroundVehicle (o +0x5c da tabela virtual do
// PBondCar, GetAIGroundVehiclePtr): nulos sem.
BondCar* rail_player_car();
// O veículo de um elemento da IA (o +0x50 do AIElement, driving/ai/element_controller.h; nulo nos helicópteros).
struct AIElement;
AIGroundVehicle* rail_element_vehicle(const AIElement& el);
AIGroundVehicle* rail_player_vehicle();

// Os destrutores dos eventos do trilho e da direção automática (os números 202, 239, 209, 108, os EPath* estáticos e
// dinâmicos de Paris: 116, 153, 215; 42 EControlToCPU e 43 EControlToPlayer): devolve se o número é um destes
// (executado ou contado).
bool rail_event_execute(const PendingEvent& e);
// (O RPlayerCamera::SetControlToCPU 0x001bd198 do EControlToSpline e do EEndControlToSpline vai pela câmera dos eventos:
// driving/game/camera_events.h.)

struct RailStats {
    uint64_t engaged = 0, disengaged = 0, spline_updates = 0, spline_physics = 0, next_paths = 0;
    uint64_t autodrive_engaged = 0, autodrive_released = 0;  // os 42 e 43 (a direção automática)
    uint64_t path_events = 0;      // os EPath* que acharam o seu RPathHandle
    uint64_t ai_elements = 0;      // os eventos com um elemento da IA (o porte da IA)
    uint64_t instances = 0;        // os EPath* com uma instância (o objeto de cena: nenhum caminho em Paris)
    uint64_t guards = 0;
};
RailStats& rail_stats();

}  // namespace drv
