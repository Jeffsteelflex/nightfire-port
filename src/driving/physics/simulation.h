#pragma once
// A parte da Simulation do DRIVING.ELF (Simulation.obj; o objeto global em 0x339190) que passa os corpos rígidos a
// cada quadro: as 48 vagas de corpo rígido (o dono em +0x270, o corpo no vetor de +0x1c ou na cópia do scratchpad de
// +0x28, o RigidBodyInfo em +0x24), as 96 de corpo simples (+0x330), o estado (+0xdb4: 3 = parada), o passo (+0xdc0:
// 1 / a taxa de quadros do vídeo, 1/60 no NTSC) e Simulation::SimulateGame, que ESimFrameUpdate chama a cada quadro da
// simulação. Os campos ficam numa estrutura do port (não no leiaute do PS2: nada lê a Simulation por endereço aqui);
// o deslocamento do PS2 está no comentário de cada um.
//
// O que o passo do PS2 faz e o porte não (contado em physics_pending): a colisão entre objetos
// (RigidBody::CollideWithObject, que chama ResolveCollision), com os prédios (CollideWithWorld, que chama
// GenerateImpulse; as duas respostas já portadas, em collision.h), os
// corpos simples (SimpleRigidBody::UpdatePosition, DetectCollisionsSRB), os objetos rastreados e a visibilidade; e no
// ESimFrameUpdate, o resto do jogo (missão, gatilhos, atores, câmera, a grade dinâmica).

#include <cstdint>

#include "driving/physics/rigid_body.h"

namespace drv {

constexpr int kRigidBodySlots = 48;
constexpr int kSimpleBodySlots = 96;

#pragma warning(push)
#pragma warning(disable : 4324)  // o preenchimento dos membros alinhados a 16 bytes
struct Simulation {
    int32_t use_scratchpad = 0;               // +0x04 os corpos estão na cópia do scratchpad
    uint32_t scratchpad_bodies_size = 0x1800; // +0x14 o tamanho da cópia (e o começo da área livre)
    RigidBody* bodies = nullptr;              // +0x1c
    RigidBodyInfo* infos = nullptr;           // +0x24
    RigidBody* scratchpad_bodies = nullptr;   // +0x28
    PhysicsObject* owners[kRigidBodySlots] = {};  // +0x270 (não nulo: vaga em uso)
    void* simple_owners[kSimpleBodySlots] = {};   // +0x330
    int32_t state = 0;                        // +0xdb4 3 = simulação parada
    int32_t frame_rate = 60;                  // +0xdbc
    float dt = 0;                             // +0xdc0 1 / frame_rate
    int32_t steps = 0;                        // +0xdc4 AdvanceStep
    // O relógio da missão e a contagem de quadros (0x355e64 e 0x355e60), que ESimFrameUpdate avança.
    float clock = 0;
    int32_t frames = 0;

    // Port: o armazenamento.
    alignas(16) RigidBody body_storage[kRigidBodySlots] = {};
    alignas(16) RigidBody scratchpad_storage[kRigidBodySlots] = {};
    alignas(16) RigidBodyInfo info_storage[kRigidBodySlots] = {};
    alignas(16) RigidBodyScratch scratch = {};
    WWorldPos* world_pos_pool = nullptr;  // os WWorldPos que o construtor aloca (16 por corpo)
};
#pragma warning(pop)

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

// Port: ocupa uma vaga (o dono pode ser nulo para um corpo sem dono, de tipo >= 4) e constrói o corpo nela.
RigidBody* simulation_add_body(Simulation& sim, RigidBodyParams params, PhysicsObject* owner_tag = nullptr);

}  // namespace drv
