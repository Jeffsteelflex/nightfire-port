#pragma once
// O corpo simples do DRIVING.ELF (SimpleRigidBody, 0x40 bytes; RVehicleParticle.obj) e a parte da Simulation que os
// guarda (Simulation.obj): um corpo de ponto, leve, das explosões, dos mísseis, das granadas, das pessoas, do
// helicóptero... A orientação só é posta, nunca integrada; a posição anda pela velocidade a cada passo
// (UpdatePosition, com o atrito dos que têm o bit 4); o raio e a caixa servem aos testes contra os outros corpos
// (CheckCollisions: as 48 vagas de corpo rígido e as 96 de corpo simples), cujo resultado (um bit por corpo, 144 bits
// em três palavras de 64) fica na Simulation +0x4b0 (o SimCollisionMap de cada vaga simples).
//
// Na Simulation do PS2: o vetor dos corpos simples em +0x20 (a cópia do scratchpad em +0x2c, 0x70001800, que o
// CopyRigidBodiesToScratchPad enche junto com os rígidos: 0x3000 bytes), os donos em +0x330, as assinaturas em +0xf0
// (a vaga << 24 | o contador global 0x339180, com o bit 31), a contagem em +0xc, os mapas das colisões em +0x4b0.
// Aqui na Simulation do port (driving/physics/simulation.h), nos campos do mesmo nome.
//
// Portado do MIPS do PS2 (a verdade); os nomes e o leiaute são do nightfire-cxbx (NightfireResearch, a versão de Xbox,
// physics/SimpleRigidBody.h). Conferido na parte 18 do nf_driving_check (tests/driving_check_missile.cpp).

#include <cstddef>
#include <cstdint>

namespace drv {

class PhysicsObject;
struct Simulation;

// O tipo do corpo simples (o construtor de cada dono o dá).
enum SimpleBodyType : uint8_t {
    kSimpleExplosion = 1,
    kSimpleHuman = 2,       // CheckCollisions o testa 0,5 mais alto
    kSimpleMine = 3,
    kSimpleMissile = 4,
    kSimpleShell = 5,
    kSimpleGrenade = 6,
    kSimpleNewton = 7,
    kSimpleHelicopter = 8,  // o do PHelicopter: 3 mais alto se o dono +0x13c é zero
    kSimpleSentry = 9,
};

// As bandeiras (+0x1e).
enum SimpleBodyFlag : uint16_t {
    kSrbNoCollide = 0x0001,       // CheckCollisions não o testa; GetCollisionBounds não tem caixa
    kSrbFriction = 0x0004,        // ApplyForces o acelera; UpdatePosition aplica o atrito
    kSrbMoves = 0x0008,           // Accelerate e UpdatePosition o mudam
    kSrbNearRigid = 0x0010,       // um corpo rígido dentro dos dois raios é choque (sem a caixa)
    kSrbNearSimple = 0x0020,      // um corpo simples dentro dos dois raios é choque (sem a caixa)
    kSrbBoxRigid = 0x0040,        // um corpo rígido dentro dos raios é choque se as caixas se cruzam
    kSrbBoxSimple = 0x0080,       // o mesmo com um corpo simples
    kSrbTouchesTriggers = 0x0100, // testado contra os gatilhos (SetCanHitTrigger)
};

struct alignas(16) SimpleRigidBody {
    float quat[4];     // +0x00 a orientação (x, y, z, w)
    float pos[3];      // +0x10
    uint8_t type;      // +0x1c SimpleBodyType
    int8_t slot;       // +0x1d a vaga nas tabelas de corpo simples da Simulation
    uint16_t flags;    // +0x1e SimpleBodyFlag
    float vel[3];      // +0x20
    float radius;      // +0x2c
    float accel[3];    // +0x30
    float mass;        // +0x3c
};
static_assert(sizeof(SimpleRigidBody) == 0x40);
static_assert(offsetof(SimpleRigidBody, pos) == 0x10 && offsetof(SimpleRigidBody, slot) == 0x1d &&
              offsetof(SimpleRigidBody, flags) == 0x1e && offsetof(SimpleRigidBody, vel) == 0x20 &&
              offsetof(SimpleRigidBody, radius) == 0x2c && offsetof(SimpleRigidBody, mass) == 0x3c);

// O mapa das colisões de uma vaga (SimCollisionMap, 0x18 bytes): o bit de cada corpo tocado, os rígidos 0-47 e os
// simples 48-143 (a vaga + 48).
struct SimCollisionMap {
    uint64_t bits[3];
};
static_assert(sizeof(SimCollisionMap) == 0x18);

// [driving] 0x001fd448 SimpleRigidBody::SimpleRigidBody(float raio, float massa, int vaga, unsigned char tipo,
// COORD3 &pos, COORD3 &vel, COORD3 &acel, MATRIX4 &orientação)
SimpleRigidBody* SimpleRigidBody_Construct(SimpleRigidBody* b, float radius, float mass, int slot, uint8_t type,
                                           const float pos[3], const float vel[3], const float accel[3],
                                           const float orient[4][4]);
// [driving] 0x001fd4d8 SimpleRigidBody::~SimpleRigidBody: zera os 0x40 bytes (MEM_fill).
void SimpleRigidBody_Destruct(SimpleRigidBody* b);
// [driving] 0x001fd528 SimpleRigidBody::GetOwner: o dono da vaga na Simulation (+0x330).
PhysicsObject* SimpleRigidBody_GetOwner(const SimpleRigidBody* b);
// [driving] 0x001fd548 SimpleRigidBody::RecalcOrientMat(MATRIX4 &): VU0_quattom4 do quatérnio.
void SimpleRigidBody_RecalcOrientMat(const SimpleRigidBody* b, float m[4][4]);
// [driving] 0x001fd598 SimpleRigidBody::GetForwardVector, 0x001fd5b8 GetRightVector, 0x001fd5d8 GetUpVector
// (COORD3 &): o eixo z, x ou y da orientação (as rotinas da REAL 0x2cc350, 0x2cc230, 0x2cc2b0; o w do destino fica).
void SimpleRigidBody_GetForwardVector(const SimpleRigidBody* b, float out[4]);
void SimpleRigidBody_GetRightVector(const SimpleRigidBody* b, float out[4]);
void SimpleRigidBody_GetUpVector(const SimpleRigidBody* b, float out[4]);
// [driving] 0x001fd5f8 SimpleRigidBody::SetOrientMat(MATRIX4 &) e 0x001fd620 SetOrientation(COORD4 &).
void SimpleRigidBody_SetOrientMat(SimpleRigidBody* b, const float m[4][4]);
void SimpleRigidBody_SetOrientation(SimpleRigidBody* b, const float q[4]);
// [driving] 0x001fd648 SimpleRigidBody::GetScalarVelocity: o módulo da velocidade (a raiz da VU0).
float SimpleRigidBody_GetScalarVelocity(const SimpleRigidBody* b);
// [driving] 0x001fd680 SimpleRigidBody::GetLinearVelocityCH(int): a velocidade na linha `axis` da orientação.
float SimpleRigidBody_GetLinearVelocityCH(const SimpleRigidBody* b, int axis);
// [driving] 0x001fd6d8 SimpleRigidBody::Accelerate(COORD3 &): vel += a x dt (com o bit 8).
void SimpleRigidBody_Accelerate(SimpleRigidBody* b, const float a[3]);
// [driving] 0x001fd730 SimpleRigidBody::ApplyFriction: vel x 0,99, acel x 0,98 (com o bit 8).
void SimpleRigidBody_ApplyFriction(SimpleRigidBody* b);
// [driving] 0x001fd798 SimpleRigidBody::UpdatePosition: o atrito (bit 4) e pos += vel x dt (bit 8).
void SimpleRigidBody_UpdatePosition(SimpleRigidBody* b);
// [driving] 0x001fd820 SimpleRigidBody::NeedsCollisionCheck: algum dos bits 0xf0.
bool SimpleRigidBody_NeedsCollisionCheck(const SimpleRigidBody* b);
// [driving] 0x001fd830 SimpleRigidBody::CheckCollisions(SimCollisionMap &): os corpos tocados.
void SimpleRigidBody_CheckCollisions(const SimpleRigidBody* b, SimCollisionMap& hits);
// [driving] 0x001fde20 SimpleRigidBody::SetCanHitTrigger(bool): o bit 0x100.
void SimpleRigidBody_SetCanHitTrigger(SimpleRigidBody* b, bool can_hit);

// [driving] 0x002cc230 (sem nome; VU0_ExtractXAxis3FromQuat no Xbox), 0x002cc2b0 (Y) e 0x002cc350
// VU0_ExtractZAxis3FromQuat(COORD4 &, COORD3 &): a linha da matriz do quatérnio; só x, y e z do destino mudam.
void srb_quat_x_axis(const float q[4], float out[4]);
void srb_quat_y_axis(const float q[4], float out[4]);
void srb_quat_z_axis(const float q[4], float out[4]);
// [driving] 0x0017b8b0 Float_COORD3toCOORD4(COORD3 &): (x, y, z, 0).
void Float_COORD3toCOORD4(float out[4], const float in[3]);

// A parte da Simulation dos corpos simples.
// [driving] 0x001ff0b0 Simulation::GetSimpleRigidBody(int): o corpo da vaga (no vetor ou na cópia do scratchpad).
SimpleRigidBody* GetSimpleRigidBody(Simulation& sim, int slot);
// [driving] 0x001fef48 Simulation::AssignRigidBodySlot(PhysicsObject *, bool) com o bool verdadeiro: a primeira vaga
// simples livre, a assinatura (com o bit 31) e a contagem; -1 sem vaga.
int AssignSimpleBodySlot(Simulation& sim, PhysicsObject* owner);
// [driving] 0x001ff0f0 Simulation::ReleaseRigidBodySlot(int, bool) com o bool verdadeiro.
void ReleaseSimpleBodySlot(Simulation& sim, int slot);
// [driving] 0x00201408 Simulation::CanSpawnSimpleRigidBody(COORD3 &, bool): menos de 72 corpos simples (96 se `more`);
// acima de 72 apaga os Newton (o port não os tem).
bool CanSpawnSimpleRigidBody(Simulation& sim, const float pos[3], bool more);
// [driving] 0x002014b8 Simulation::DetectCollisionsSRB: o CheckCollisions de cada corpo simples que testa algo.
void DetectCollisionsSRB(Simulation& sim);
// [driving] 0x00201590 Simulation::GetOrderedBody(int): o dono do corpo de índice i (0-47 rígidos, 48-143 simples).
PhysicsObject* GetOrderedBody(Simulation& sim, int index);

// Port: as contagens do que os corpos simples pediram e o port não tem.
struct SimpleBodyStats {
    uint32_t helicopter_owner = 0; // CheckCollisions leu o +0x13c do dono por um corpo de helicóptero
};
SimpleBodyStats& simple_body_stats();

}  // namespace drv
