#pragma once
// Corpos rígidos (módulo RB do ACTION.ELF, 0x001d6bf0-0x001d8080). Ver docs/engine.md, "Corpos rígidos".
//
// Um posicionamento 0xd8 (objeto 0x1a; RB_Create) é um modelo com física: massa (parâmetro 0; 3 se zero), tipo
// (parâmetro 2), luz que acompanha (parâmetro 3) e som de batida (parâmetro 4). O tipo 0 é um corpo solto: cai com a
// gravidade (com arrasto: velocidade final de 9,8 m/s), bate na geometria pela esfera do teste de colisão dele
// (obj_tag::hit_test, 0x101: Collide_Update empurra e RB_RestingContact tira metade da velocidade contra a normal: com
// o coeficiente -0,5 na fórmula do original o corpo não quica, assenta), boia na água da
// sala e é carregado por objetos que se movem. Os tipos 1-4 ficam presos pelo centro e só giram (pêndulos: lustres,
// placas, lanternas): uma força que os leva de volta à vertical, o vento da sala, os tiros (o impulso do dano no ponto
// do acerto, com os eixos que o tipo deixa: 1 sem y, 2 só x, 3 só y, 4 só z) e, fora do tipo 3, a cápsula do corpo
// contra a geometria (Collide_CylinderIntersect). A orientação é um quatérnio escalar-primeiro (Quat_QuaternionToMatrix)
// que a velocidade angular gira (RB_apply_omega); o arrasto angular é exponencial (ang_damp).
//
// Peculiaridades mantidas:
// - RB_Create põe a orientação na identidade: o giro do posicionamento se perde (RB_SetState).
// - RB_ApplyImpulses divide o impulso angular do corpo por 1/I0, 1/I2, 1/I1 nos eixos x, y, z (inv_inertia em 0x8c,
//   0x90, 0x94 tem I2 antes de I1: RB_Build), e prende a mudança de giro a +-4 por eixo.
// - Um tiro encerra a lista de acertos de RB_RestingContact (os acertos seguintes não contam no tick).
// Divergências: as somas da VU0 em .xyzw levavam lixo da pilha para o w de vel, omega e da posição; aqui o w não muda.
// A orientação (em 0x64, sem alinhamento de 16) é lida e escrita por cópia.

#include <cstdint>

#include "engine/collide.h"
#include "engine/map_types.h"
#include "engine/object.h"
#include "engine/ps2types.h"
#include "platform/ptr32.h"

struct light_tag;

// Dados do objeto (0x160 bytes em obj->data, tipo 0x1a).
struct alignas(16) RIGIDBODY {
    _VECTOR vel;              // 0x00 velocidade linear (m/s, mundo; só o tipo 0 anda)
    _VECTOR omega;            // 0x10 velocidade angular (rad/s, mundo)
    _VECTOR accel;            // 0x20 aceleração linear (gravidade com arrasto, RB_VecSpecial)
    _VECTOR ang_accel;        // 0x30 aceleração angular (nada a escreve)
    _VECTOR impulse;          // 0x40 impulso linear do tick (RB_AddImpulsiveForce)
    _VECTOR ang_impulse;      // 0x50 impulso angular do tick (torques)
    P32<const _VECTOR> gravity;  // 0x60 WldGravity (tipo 0) ou zero
    float quat[4];            // 0x64 orientação (w, x, y, z): escalar primeiro
    P32<MATRIX> matrix;       // 0x74 obj->matrix
    float mass;               // 0x78
    float inv_mass;           // 0x7c
    float inertia[3];         // 0x80 momentos de inércia (RB_CuboidMomentsOfInertia)
    float inv_inertia[3];     // 0x8c 1/I0, 1/I2, 1/I1 (nessa ordem: RB_Build)
    float restitution;        // 0x98 -0,5 (tipo 0) ou -0,75: v_normal' = -restitution x v_normal (RB_RestingContact)
    float half_height;        // 0x9c |box_min.y| do modelo
    float lin_damp;           // 0xa0 constante de tempo do arrasto linear (1 s no tipo 0)
    float ang_damp;           // 0xa4 do arrasto angular (31; 25 nos pêndulos, 250 no tipo 3)
    uint16_t kind;            // 0xa8 parâmetro 2
    uint16_t test_type;       // 0xaa tipo do teste de colisão: 0x101 (tipo 0) ou 0 (os pêndulos)
    uint32_t unknown_ac;
    HITTEST_tag test;         // 0xb0 o teste de colisão (obj_tag::hit_test); radius = o raio do corpo
    P32<light_tag> light;     // 0x150 a luz da fase presa ao corpo (Light_FindMapLight)
    uint8_t carries_light;    // 0x154 parâmetro 3
    uint8_t pad_155[3];
    uint32_t sound;           // 0x158 som da batida (parâmetro 4; 0xffff nenhum)
    uint32_t unknown_15c;
};
static_assert(sizeof(RIGIDBODY) == 0x160);
static_assert(offsetof(RIGIDBODY, quat) == 0x64 && offsetof(RIGIDBODY, matrix) == 0x74);
static_assert(offsetof(RIGIDBODY, kind) == 0xa8 && offsetof(RIGIDBODY, test) == 0xb0);
static_assert(offsetof(RIGIDBODY, light) == 0x150 && offsetof(RIGIDBODY, sound) == 0x158);

constexpr uint8_t kRigidBodyType = 0x1a;

obj_tag* RB_Create(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist, level_tag* level);
void RB_Update(obj_tag* obj);
void RB_CollisionHandler(obj_tag* obj);
void RB_ApplyImpulses(RIGIDBODY* rb);
void RB_RestingContact(obj_tag* obj, RIGIDBODY* rb, HITDATA_tag* hit);
void RB_RestingContactRot(obj_tag* obj, RIGIDBODY* rb, HITDATA_tag* hit);
void RB_CalcTorque(RIGIDBODY* rb, HITDATA_tag* hit);
void RB_EulerEvolve(RIGIDBODY* rb, float dt, int32_t skip_impulses);
void RB_Init(RIGIDBODY* rb, uint16_t kind);
uint32_t RB_Build(RIGIDBODY* rb, const _VECTOR* gravity, const float* inertia, float mass);
void RB_ApplyWind(RIGIDBODY* rb, obj_tag* obj);
void RB_AddImpulsiveForce(RIGIDBODY* rb, const _VECTOR* force);
void RB_AddImpulsiveForceAtPoint(RIGIDBODY* rb, const _VECTOR* force, const _VECTOR* point);
void RB_SetState(RIGIDBODY* rb);
void RB_CuboidMomentsOfInertia(float* out, float mass, float sx, float sy, float sz);
void RB_ToWorld(_VECTOR* out, const _VECTOR* v, const MATRIX* m);
void RB_ToBody(_VECTOR* out, const _VECTOR* v, const MATRIX* m);
void RB_apply_omega(quaternion_tag* q, const _VECTOR* omega, float dt);
void RB_VecSpecial(_VECTOR* out, float k, const _VECTOR* a, const _VECTOR* b, float s);

// Só no port: o que os corpos rígidos fizeram desde a partida (nf_loader_check, relatório da captura).
struct RigidBodyStats {
    uint32_t created = 0;
    uint32_t bullet_hits = 0;   // tiros que empurraram um corpo
    uint32_t bounces = 0;       // empurrões da geometria (RB_RestingContact com push)
    uint32_t carried = 0;       // vezes que outro objeto carregou um corpo
    uint32_t cylinder_hits = 0; // cápsulas dos pêndulos que tocaram a geometria
};
extern RigidBodyStats g_rigid_body_stats;
