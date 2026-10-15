#pragma once
// A parte da Simulation do DRIVING.ELF (Simulation.obj; o objeto global em 0x339190) que passa os corpos rígidos a
// cada quadro: as 48 vagas de corpo rígido (o dono em +0x270, o corpo no vetor de +0x1c ou na cópia do scratchpad de
// +0x28, o RigidBodyInfo em +0x24), as 96 de corpo simples (+0x330), o estado (+0xdb4: 3 = parada), o passo (+0xdc0:
// 1 / a taxa de quadros do vídeo, 1/60 no NTSC) e Simulation::SimulateGame, que ESimFrameUpdate chama a cada quadro da
// simulação. Os campos ficam numa estrutura do port (não no leiaute do PS2: nada lê a Simulation por endereço aqui);
// o deslocamento do PS2 está no comentário de cada um.
//
// A colisão entre objetos (RigidBody::CollideWithObject) e com os prédios (CollideWithWorld) está em collide.h. O que o
// passo do PS2 faz e o porte não (contado em physics_pending): os corpos simples (SimpleRigidBody::UpdatePosition,
// DetectCollisionsSRB), os objetos rastreados e a visibilidade; e no ESimFrameUpdate, o resto do jogo (missão,
// gatilhos, atores, câmera, a grade dinâmica).

#include <cstdint>
#include <vector>

#include "driving/physics/rigid_body.h"
#include "driving/physics/simple_body.h"

namespace drv {

class BondCar;

constexpr int kRigidBodySlots = 48;
constexpr int kSimpleBodySlots = 96;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // o preenchimento dos membros alinhados a 16 bytes
#endif
struct Simulation {
    int32_t use_scratchpad = 0;               // +0x04 os corpos estão na cópia do scratchpad
    int32_t rigid_count = 0;                  // +0x08 as vagas de corpo rígido em uso (AssignRigidBodySlot)
    int32_t simple_count = 0;                 // +0x0c as de corpo simples
    // +0x14 o tamanho da cópia do scratchpad (e o começo da área livre). Port: 0x1800, só os rígidos; o construtor do
    // PS2 põe 0x3000 (os rígidos e os 96 corpos simples logo depois), que o port copia à parte.
    uint32_t scratchpad_bodies_size = 0x1800;
    RigidBody* bodies = nullptr;              // +0x1c
    SimpleRigidBody* simple_bodies = nullptr; // +0x20 (o vetor + 0x1800 no PS2)
    RigidBodyInfo* infos = nullptr;           // +0x24
    RigidBody* scratchpad_bodies = nullptr;   // +0x28
    SimpleRigidBody* scratchpad_simple = nullptr;  // +0x2c (0x70001800)
    uint32_t rigid_sig[kRigidBodySlots] = {};      // +0x30 as assinaturas (a vaga << 24 | o contador 0x339180)
    uint32_t simple_sig[kSimpleBodySlots] = {};    // +0xf0 (com o bit 31)
    PhysicsObject* owners[kRigidBodySlots] = {};   // +0x270 (não nulo: vaga em uso)
    PhysicsObject* simple_owners[kSimpleBodySlots] = {};  // +0x330
    SimCollisionMap srb_hits[kSimpleBodySlots] = {};      // +0x4b0 o DetectCollisionsSRB de cada vaga simples
    // Port: a ordem de chegada de cada dono de corpo simples, para os Simulate na ordem do mapa do PS2 (+0xe68: um
    // multimapa pelo tipo do objeto, os iguais na ordem em que entraram).
    uint32_t simple_seq[kSimpleBodySlots] = {};
    uint32_t next_seq = 1;
    int32_t state = 0;                        // +0xdb4 3 = simulação parada
    int32_t frame_rate = 60;                  // +0xdbc
    float dt = 0;                             // +0xdc0 1 / frame_rate
    int32_t steps = 0;                        // +0xdc4 AdvanceStep
    // +0xdcc..+0xdd0 os carros (SpawnPhysicsObject os põe aqui; o primeiro é o do jogador): o tráfego
    // (driving/ai/ground_vehicle.h) os percorre. Port: ponteiros nativos para os BondCar.
    std::vector<BondCar*> cars;
    // +0xe6c os objetos que o DeletePhysicsObject tirou do mapa (os Smackable que dormiram, driving/game/smackable.h):
    // o DeleteDeadObjects do próximo ESimFrameUpdate os apaga (o destrutor virtual).
    std::vector<PhysicsObject*> dead;
    // +0xdfc os mísseis e +0xe2c as explosões (driving/game/missile.h): os vetores do SpawnPhysicsObject.
    std::vector<PhysicsObject*> missiles;
    std::vector<PhysicsObject*> explosions;
    // +0xe20 as balas lentas (o Shell, driving/game/shells.h).
    std::vector<PhysicsObject*> shells;
    // O relógio da missão e a contagem de quadros (0x355e64 e 0x355e60), que ESimFrameUpdate avança.
    float clock = 0;
    int32_t frames = 0;

    // Port: o armazenamento.
    alignas(16) RigidBody body_storage[kRigidBodySlots] = {};
    alignas(16) RigidBody scratchpad_storage[kRigidBodySlots] = {};
    alignas(16) RigidBodyInfo info_storage[kRigidBodySlots] = {};
    alignas(16) SimpleRigidBody simple_storage[kSimpleBodySlots] = {};
    alignas(16) SimpleRigidBody simple_scratch_storage[kSimpleBodySlots] = {};
    alignas(16) RigidBodyScratch scratch = {};
    WWorldPos* world_pos_pool = nullptr;  // os WWorldPos que o construtor aloca (16 por corpo)
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

Simulation& simulation();
// A Simulation do começo da missão (o FUN_001feda0, a parte que zera o estado e calcula o passo pela taxa do vídeo).
void simulation_reset(int frame_rate = 60);

// [driving] 0x1ff3c8 Simulation::GetScratchPadFreeZone(void)
RigidBodyScratch* GetScratchPadFreeZone(Simulation& sim);
// [driving] 0x1ff0d8 Simulation::GetRigidBodyInfo(int)
RigidBodyInfo* GetRigidBodyInfo(Simulation& sim, int slot);
// [driving] 0x1ff088 (sem nome no SYM): o corpo da vaga, no vetor principal ou na cópia do scratchpad.
RigidBody* GetRigidBody(Simulation& sim, int slot);
// [driving] 0x1ff2e8 Simulation::CopyRigidBodiesToScratchPad(void) (nome de confiança 1)
void CopyRigidBodiesToScratchPad(Simulation& sim);
// [driving] 0x1ff358 Simulation::CopyRigidBodiesFromScratchPad(void) (nome de confiança 1)
void CopyRigidBodiesFromScratchPad(Simulation& sim);
// [driving] 0x1fef20 Simulation::AdvanceStep(void)
void AdvanceStep(Simulation& sim);
// [driving] 0x1ff3d8 Simulation::SimulateGame(void)
void SimulateGame(Simulation& sim);
// [driving] 0x159d10 ESimFrameUpdate::~ESimFrameUpdate(void): um quadro da simulação (a parte da física; o resto do
// jogo conta como pendente).
void ESimFrameUpdate(Simulation& sim);

// A assinatura de uma vaga nova (Simulation::AssignRigidBodySlot): a vaga << 24 | o contador global 0x339180, que
// anda a cada vaga ocupada (rígida ou simples).
uint32_t simulation_next_signature(int slot);
uint32_t& simulation_signature_counter();

// Port: ocupa uma vaga (o dono pode ser nulo para um corpo sem dono, de tipo >= 4) e constrói o corpo nela.
RigidBody* simulation_add_body(Simulation& sim, RigidBodyParams params, PhysicsObject* owner_tag = nullptr);

}  // namespace drv
