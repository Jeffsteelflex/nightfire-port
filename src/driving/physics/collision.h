#pragma once
// O impulso de uma colisão de um corpo rígido contra o mundo (os prédios: as constantes BUILDING_*) e a resposta de um
// choque entre dois corpos rígidos (ResolveCollision, com ModifyLevers e ScaleObjObjForces), do DRIVING.ELF
// (RVehicleParticle.obj), e as pequenas rotinas que eles usam. Quem os chama (RigidBody::CollideWithWorld, com o
// WCollisionMgr e a grade do mundo; RigidBody::CollideWithObject, as caixas orientadas) não foi portado: as funções
// ficam prontas e conferidas contra o original (nf_driving_check) para quando o mundo e os choques vierem.
//
// ResolveCollision em poucas palavras (lido na desmontagem; o Xbox só explicou):
//  - a velocidade relativa do ponto (vB + ωB x rB - vA - ωA x rA) ao longo da normal acima de 10 m/s: os corpos já se
//    afastam, nada acontece (devolve 0); um corpo com RigidBodyInfo +0x4fc ligado também não reage (devolve 1);
//  - o tamanho do impulso não é o da física de corpos rígidos: k = max(SPEED_SCALE |Δv|, profundidade
//    DEPTH_FORCE_SCALE, 1), depois min(k², TOTAL_FORCE_LIMIT max(|vA|², |vB|²) / 1600 limitado a [5, TOTAL_FORCE_LIMIT]),
//    na direção da normal (de A para B: A leva -J, B leva +J), dividido entre os dois por regras de massa e de tipo;
//  - entre veículos (tipo < 4): o J de cada um é proporcional à massa do outro (massa do outro / soma vezes
//    min(soma, 3000)), então o momento não se conserva: |ΔP_A| / |ΔP_B| = m_B / m_A e o mais leve muda de velocidade
//    na razão do quadrado das massas; sem y no momento; com as exceções de quem está apoiado, da vida
//    (SMissionManager +0x350) e de ScaleObjObjForces (que corta o impulso de quem passaria de 1,05 vezes a maior
//    velocidade ao quadrado de antes e reforça quem bateu num prédio);
//  - com algum objeto comum (tipo >= 4): a parte de cada um é min(0,66, massa do outro / soma) vezes uma "massa de
//    choque" (a soma acima de 1500 kg cada, senão 0,75 da menor; metade entre dois objetos); no impulso de um objeto o y
//    vira |y| (A, que perde o impulso, só é empurrado para baixo; B só para cima) e cada componente fica em ±10 vezes
//    a massa; acima de 15 m/s o objeto não leva impulso nenhum; o veículo leva 0,1 da parte, sem y e sem giro; um
//    corpo parado com o bit 1 de +0x7c leva tudo de um corpo mais pesado e lento;
//  - o momento angular muda de r x J (um quarto dele nos objetos; nos veículos o y de r x J vai a 0,16 no carro do
//    jogador e 0,5 nos outros), v e ω são refeitos.

#include <cstdint>

#include "driving/physics/rigid_body.h"

namespace drv {

// COLLISION_INFO (0x54 bytes): o que um choque informa ao evento ECollision (som, dano, faíscas).
struct CollisionInfo {
    float point[4];       // +0x00 o ponto de contato
    float normal[4];      // +0x10
    float velocity[4];    // +0x20 a velocidade do ponto (v + ω x r); o w é lixo da pilha no PS2
    float unknown_30[4];  // +0x30 [0] e [1]: ResolveCollision grava 1,0 ou 0,0 por corpo (0 num veículo de um choque
                          //       com objeto e no corpo parado que leva o impulso todo); [2] e [3] não são escritos
    float speed;          // +0x40 a velocidade normal (vezes a escala)
    float strength;       // +0x44 0..1
    uint8_t kind;         // +0x48
    int8_t slot;          // +0x49
    uint16_t flags;       // +0x4a
    uint8_t type;         // +0x4c (6: prédio; 7: chão)
    uint8_t unknown_4d;
    uint16_t tag;         // +0x4e
    int32_t other;        // +0x50
};
static_assert(sizeof(CollisionInfo) == 0x54);

// [driving] 0x210510 Util_Bound(float, float, float): v > a dá b; v < -a dá -b; senão v.
float Util_Bound(float v, float a, float b);
// [driving] 0x210938 Util_GenerateCarTensor(float, float, float, float): a diagonal do tensor de inércia inverso de
// um veículo de massa m e medidas (dx, dy, dz): uma caixa (m / 3) se dz > 4, senão a fórmula de carro.
void Util_GenerateCarTensor(float out[3], float mass, float dx, float dy, float dz);

// [driving] 0x1f8418 RigidBody::ConvertToLocalAngularMomentum(COORD3 &): v * Rᵀ (mais a linha 3).
void ConvertToLocalAngularMomentum(const RigidBody* rb, float v[4]);
// [driving] 0x1f8498 RigidBody::ConvertFromLocalAngularMomentum(COORD3 &): v * R.
void ConvertFromLocalAngularMomentum(const RigidBody* rb, float v[4]);
// [driving] 0x1f8508 RigidBody::ConvertWorldToLocal(COORD3 &)
void ConvertWorldToLocal(const RigidBody* rb, float v[4]);
// [driving] 0x1f84d0 RigidBody::ConvertLocalToWorld(COORD3 &)
void ConvertLocalToWorld(const RigidBody* rb, float v[4]);

// [driving] 0x1fb318 RigidBody::GenerateImpulse(COLLISION_INFO &, COORD4 &, COORD4 &, float, bool, short, float):
// o impulso de um choque no ponto point com a normal n: a velocidade normal do ponto (vezes scale), o impulso
// -(1 + e min(|vn|, BUILDING_MICRO_THRESHOLD)) (vn - depth PF_SCALE) / (n . ((I⁻¹(r x n)) x r) + o termo da
// distância ao eixo), limitado a ±BUILDING_FORCE_LIMIT, menos o atrito (com world) e vezes a massa; P e L mudam, v e
// ω são refeitos. world: o choque é com o mundo (sem ele, o impulso não tem atrito nem o limite de velocidade). Com
// world e o ponto se afastando (vn > 0) só preenche out.
void GenerateImpulse(RigidBody* rb, CollisionInfo* out, const float n[4], const float point[4], float depth,
                     bool world, uint16_t tag, float scale);

// [driving] 0x1f98e8 RigidBody::ModifyLevers(RigidBody &, RigidBody &, COORD3 &, COORD3 &, COORD4 &, COORD4 &, bool):
// os braços ra e rb (do centro de cada corpo ao ponto de contato, no mundo) de um choque. Entre dois veículos
// (vehicles): o y de cada braço no referencial do corpo vai a max(0, c + 0,12) com c = CAR_COLLIDE_COG_SCALE no carro
// do jogador (tipo 1) e 0,1 nos outros, e é virado para baixo, se os dois estão alinhados (|frente A . frente B| e
// |(centro A - centro B) . frente A| acima de 0,866: uma batida de frente ou de traseira); senão vai a
// max(0, c - 0,35) (zero: o choque fica na altura do centro). Sem os dois veículos: o braço de um veículo perde o y e,
// se há algum veículo, a normal vira a direção do centro de A ao ponto (normalizada com RSQRT); entre dois objetos,
// nada.
void ModifyLevers(RigidBody* a, RigidBody* b, float ra[4], float rb[4], const float point[4], float normal[4],
                  bool vehicles);
// [driving] 0x1f9b40 RigidBody::ScaleObjObjForces(RigidBody &, RigidBody &, COORD3 &, COORD3 &, float, float, float):
// os impulsos do choque entre dois veículos (imp_a sai de A, imp_b entra em B). Se o mais rápido dos dois passa de
// threshold (a velocidade ao quadrado; ResolveCollision passa 225), o impulso de quem teria depois dele uma velocidade
// ao quadrado s acima de threshold e de lim = factor (1,05) vezes a maior de antes é multiplicado por lim / s (quirk
// preservado: a razão dos quadrados, não a raiz dela, então o corte é maior que o necessário); com depth acima de
// 0,5, quem bateu num prédio neste quadro (RigidBodyInfo +0x4fd) empurra o outro (2 (depth - 0,5) 1,5 + 1,5 vezes) e
// leva metade.
void ScaleObjObjForces(const RigidBody* a, const RigidBody* b, float imp_a[4], float imp_b[4], float threshold,
                       float factor, float depth);
// [driving] 0x1f9d98 RigidBody::ResolveCollision(RigidBody &, RigidBody &, COORD4 *, COORD4 *, COLLISION_INFO *,
// float): a resposta do choque entre os corpos a e b no ponto point com a normal normal (de a para b; ModifyLevers
// pode reescrevê-la), depth a profundidade (CollideWithObject passa o negativo da sua). self é o this (o corpo de
// CollideWithObject, que se passa também como a): só o tipo e o dono dele são lidos. Preenche em out o ponto, a
// normal, a velocidade normal relativa (speed), a força (strength, 0..1) e +0x30/+0x34. Devolve 0 se os corpos já se
// afastam (nada muda), 1 nos outros casos. Os corpos que levaram impulso acordam (estado 2, RigidBodyInfo +0x4fe, o
// bit 0 de +0x7c).
int ResolveCollision(RigidBody* self, RigidBody* a, RigidBody* b, float normal[4], const float point[4],
                     CollisionInfo* out, float depth);

}  // namespace drv
