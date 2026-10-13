#pragma once
// Os choques de um corpo rígido com o mundo (os prédios: as barreiras das instâncias de colisão e os objetos de colisão,
// cilindros e caixas) e com os outros corpos, do DRIVING.ELF (RVehicleParticle.obj): RigidBody::CollideWithWorld,
// CollideWithObject, ResolveWorldOBBCollision e CalculateAndApplyWorldDamage. Portados do MIPS e conferidos contra o
// original no nf_driving_check (parte 7). docs/driving.md seção 14.
//
// CollideWithWorld em poucas palavras (lido na desmontagem; o nightfire-cxbx, NightfireResearch, explicou):
//  - o corpo tem um WCollider no dono (driving/physics/collider.h) com as barreiras perto dele (e, nos veículos com
//    SUB_PHYSICS, as instâncias), refeito quando ele sai da região;
//  - uma vez por passo no carro do jogador, nos outros a cada passo acima de 15 m/s, a cada dois abaixo e a cada quatro
//    abaixo de 2 m/s: as 8 quinas da caixa (nos veículos também um ponto à frente e dois dos lados) andam um passo pela
//    velocidade a partir do centro, e cada segmento é testado contra as barreiras e, no carro do jogador, contra os
//    cilindros; cada choque dá o impulso de GenerateImpulse (collision.h), o dano do mundo, os estímulos da animação da
//    instância batida e o evento ECollision; uma face do tipo 0x0e dispara o evento 0x15 da missão;
//  - no carro do jogador, a caixa contra a de cada objeto-caixa perto (ResolveWorldOBBCollision: escala 6000).
//
// O que fica fora do port (contado): o evento ECollision (só o registro de collide_events), o evento da missão e os
// estímulos da animação da instância batida (chamadas de collide_events), o dano dos donos (PhysicsObject::apply_damage),
// a velocidade dos objetos que andam com a sua instância (zero) e o chão das instâncias dos veículos com SUB_PHYSICS.

#include <cstdint>
#include <functional>

#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"

namespace drv {

// O que os choques mandam para fora da física (no PS2: eventos e chamadas a outros sistemas).
struct CollideEvents {
    // ECollision (Event::operator new(0x58) + ECollision::ECollision(COLLISION_INFO &)): o som, as faíscas, o HUD.
    std::function<void(const CollisionInfo&)> collision;
    // SMissionManager::ProgrammerDefinedEvent(gerente, 0x15, "") de uma face do tipo 0x0e.
    std::function<void(int32_t)> mission_event;
    // RAnimEngine::Handle::ProcessStimuli(estímulo, o passo da simulação, 0) da animação da instância de render batida
    // (WWorld::GetSceneObjFromInstance: só as que têm um objeto de cena).
    std::function<void(uint32_t render_index, uint32_t stimulus, int32_t step)> stimulus;
    uint64_t collisions = 0, mission_events = 0, stimuli = 0, kind_zero = 0;
};
CollideEvents& collide_events();

// [driving] 0x001fc2f0 RigidBody::CollideWithWorld(void)
void CollideWithWorld(RigidBody* rb);
// [driving] 0x001fbbd8 RigidBody::CollideWithObject(int, int): o corpo contra os corpos das vagas depois de `slot`.
void CollideWithObject(RigidBody* rb, int slot);
// [driving] 0x001fade8 RigidBody::ResolveWorldOBBCollision(COORD4 *, COORD4 *, float, COLLISION_INFO *, COORD3 *,
// float): a resposta à caixa de um objeto do mundo (que anda com object_velocity por passo).
bool ResolveWorldOBBCollision(RigidBody* rb, const float normal[4], const float point[4], float depth,
                              CollisionInfo* out, const float object_velocity[4], float force_scale);
// [driving] 0x001fc198 RigidBody::CalculateAndApplyWorldDamage(COORD3 &, float)
void CalculateAndApplyWorldDamage(RigidBody* rb, const float point[3], float strength);

}  // namespace drv
