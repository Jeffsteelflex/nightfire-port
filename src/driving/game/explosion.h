#pragma once
// As explosões do jogo de carro (o Explosion de Noise.obj, o Simulation::SpawnExplosion/SpawnExplosionObject de
// Simulation.obj, os eventos ESpawnExplosion, ESpawnExplosionStatic e ERandomExplosion de DTuningFile.obj; docs/driving.md
// seção 22).
//
// Como o PS2 explode: quem explode (o míssil que bate, a morte de um carro pela chave do RAnimEngine, um gatilho da pista)
// cria um ESpawnExplosion (Simulation::SpawnExplosion, ou a lista 'el'); o destrutor do evento, no fim da passagem do
// escalonador, acha o lugar e a orientação (a instância do efeito, o RSceneObj, ou a posição e a direção do evento),
// escolhe e dispara o efeito da galeria (GFX::Trigger: "Ex_Small", "Ex_SmallD", "Un_ExplL"...), sacode o controle do
// jogador perto (IFeedback::ApplyJolt), cria o objeto Explosion (Simulation::SpawnExplosionObject), queima o chão
// (RScorchWorld::ScorchGround), acende a luz (RHighLevelLightManager::AddExplosion), avisa a IA
// (AIZoneController::RegisterZone) e toca os dois sons (um AOneShotSound "SFX_*Exp" na mistura "Explosions" e um
// ALimitedSound "SFX_DistExp" em "ExplosionBass", no máximo um par a cada 0,3 s por quatro vagas).
//
// O Explosion é um PhysicsObject de tipo 3 com um corpo simples de tipo 1 (raio 0, as bandeiras 0x131: testa os corpos
// rígidos e os simples dentro do raio, sem caixa). A cada passo o raio cresce 1 m (60 x 1/60); o dano do centro vai
// caindo com o quadrado de (raio + 1) / 2; cada corpo tocado pela primeira vez leva uma força para fora (8000, 9000, 8000
// x o dano, limitada a 25 x a massa), um giro sorteado e o dano (a entrada +0x1c da tabela virtual do dono, o
// PBondCar::ApplyDamage nos carros, com 0,8 e o tipo 3). Acaba quando o dano cai a 1 ou o raio passa de 149.
//
// Portado do MIPS do PS2 (a verdade); os nomes e o tipo dos corpos simples são do nightfire-cxbx (NightfireResearch, a
// versão de Xbox). Conferido na parte 18 do nf_driving_check (tests/driving_check_missile.cpp). O que o port não tem
// (a galeria dos efeitos, a queimadura do chão, a luz, a zona da IA, o ALimitedSound) é contado e anotado
// (effects_stats(), effects_trace()).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "driving/physics/rigid_body.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/world.h"

namespace drv {

struct PendingEvent;
struct Simulation;

// As palavras do PhysicsObject do PS2 depois do WWorldPos (+0x40..+0x6c).
struct PhysicsObjectWords {
    int32_t type;            // +0x40 1 carro, 2 Smackable, 3 explosão, 6 míssil...
    uint16_t flags;          // +0x44 bit 0 corpo simples, 2 e 4 do construtor
    int16_t slot;            // +0x46 a vaga do corpo
    uint32_t render;         // +0x48 (p32 RSceneObj)
    uint32_t audio;          // +0x4c
    uint32_t feedback;       // +0x50
    uint32_t hit_point_loc;  // +0x54
    uint32_t owner_sig;      // +0x58 a assinatura de quem criou
    uint32_t attributes;     // +0x5c o AttributeSet (port: 0)
    uint32_t collider;       // +0x60
    uint32_t unknown64;      // +0x64
    uint32_t vtable;         // +0x68
};
static_assert(sizeof(PhysicsObjectWords) == 0x2c);

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // o preenchimento do WWorldPos alinhado a 16 bytes
#endif
// Um PhysicsObject de corpo simples do port (o explosão e o míssil): o WWorldPos do começo e as palavras do PS2.
class SimpleObject : public PhysicsObject {
public:
    WWorldPos world_pos_{};   // +0x00
    PhysicsObjectWords po{};  // +0x40
    WWorldPos* world_pos() override { return &world_pos_; }
    PhysicsAttributes* attributes() override { return nullptr; }
    int32_t kind() override { return po.type; }
    SimpleRigidBody* body() const;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// O resto do Explosion do PS2 (0x98 bytes): +0x6c..+0x98.
struct ExplosionData {
    int32_t kind;            // +0x6c o tipo (o nome da tabela 0x3318f0; 6: sem dano)
    float damage;            // +0x70 o dano no centro
    uint32_t source;         // +0x74 (p32 PhysicsObject) o objeto atingido que causou (leva o dano inteiro)
    uint32_t seen[6];        // +0x78 o SimCollisionMap dos corpos que já levaram a onda (três palavras de 64 bits)
    uint32_t unknown90[2];   // +0x90
};
static_assert(sizeof(ExplosionData) == 0x2c);

class Explosion : public SimpleObject {
public:
    ExplosionData d{};
    ~Explosion() override;
    void simulate() override;
    bool collision_bounds(float half[4]) override;
};

// [driving] 0x0017dc58 Explosion::Explosion(ExplosionType, COORD3 &dir, COORD3 &pos, float dano, PhysicsObject *quem,
// ObjSig) (a direção não é usada)
void Explosion_Construct(Explosion& x, int32_t kind, const float pos[3], float damage, PhysicsObject* source,
                         uint32_t sig);
// [driving] 0x0017ddc0 Explosion::Simulate
void Explosion_Simulate(Explosion& x);
// [driving] 0x00200200 Simulation::SpawnExplosionObject: nulo sem vaga de corpo simples (CanSpawnSimpleRigidBody, 96
// vagas acima de 500 de dano).
Explosion* SpawnExplosionObject(Simulation& sim, float damage, int32_t kind, const float dir[3], const float pos[3],
                                PhysicsObject* source, uint32_t sig);
// [driving] 0x00200088 Simulation::SpawnExplosion(int tipo, int arma, COORD3 &dir, COORD3 &pos, float dano, int face,
// PhysicsObject *quem): cria o ESpawnExplosion (o número 33, na fila dos eventos), se houver vaga.
void SpawnExplosion(Simulation& sim, float damage, int32_t kind, int32_t weapon, const float dir[3], const float pos[3],
                    int32_t face, PhysicsObject* object);
// [driving] 0x00200cb8 Simulation::SpawnPhysicsObject (as explosões e os mísseis): o vetor do tipo.
void SpawnPhysicsObject(Simulation& sim, PhysicsObject* o);
// [driving] 0x00200f18 Simulation::DeletePhysicsObject (0x00201a60 DeleteExplosionObject, 0x002019e0 DeleteMissileObject):
// sai do vetor do tipo e vai para a lista dos apagados (o DeleteDeadObjects do próximo quadro).
void DeletePhysicsObject(Simulation& sim, PhysicsObject* o);

// Os destrutores dos eventos das explosões, dos tiros e dos efeitos (o events.cpp chama): devolve falso nos números que
// não são daqui. 10 EFireWeapon (driving/game/missile.h), 33 ESpawnExplosion, 154 ESpawnExplosionStatic, e os números de
// efeito que o port conta (9, 36, 130, 173, 196, 250, 252, 253, 257, 279).
bool effects_event_execute(const PendingEvent& e);
// [driving] 0x0015a098 ESpawnExplosion::~ESpawnExplosion (a imagem do objeto: e.obj).
void ESpawnExplosion_Execute(const PendingEvent& e);
// [driving] 0x0015b010 ESpawnExplosionStatic::~ESpawnExplosionStatic
void ESpawnExplosionStatic_Execute(const PendingEvent& e);

// As funções do PhysicsObject do PS2 (não virtuais) sobre os donos do port: o corpo simples ou o rígido da vaga.
// [driving] 0x00195808 PhysicsObject::GetPosition
const float* PhysicsObject_GetPosition(PhysicsObject* o);
// [driving] 0x00195688 PhysicsObject::GetMass
float PhysicsObject_GetMass(PhysicsObject* o);
// [driving] 0x00195b58 PhysicsObject::GetCollisionBounds
bool PhysicsObject_GetCollisionBounds(PhysicsObject* o, float half[4]);
// [driving] 0x00195728 PhysicsObject::ApplyForces(COORD3 &força, COORD3 &giro)
void PhysicsObject_ApplyForces(PhysicsObject* o, const float force[4], const float torque[4]);
// [driving] 0x001958a8 PhysicsObject::GetSig
uint32_t PhysicsObject_GetSig(PhysicsObject* o);
// [driving] 0x00195aa8 PhysicsObject::Simulate: o Simulate do render (o RSceneObj +0x48: a entrada +0x7c); o port não tem
// render nos objetos de corpo simples (contado).
void PhysicsObject_Simulate(PhysicsObject* o);
// BytesToCoord (0x002d0b08) está em game/bond_car.h (bond_car_damage.cpp, conferido na parte 17).

// O que o port anota e conta.
struct EffectsStats {
    uint64_t explosions_spawned = 0, explosions_simulated = 0, explosions_deleted = 0;
    uint64_t spawn_refused = 0;          // sem vaga de corpo simples
    uint64_t gfx_triggers = 0;           // GFX::Trigger (a galeria não foi portada)
    uint64_t gfx_lookups_failed = 0;
    uint64_t render_fx = 0;              // o RSceneObj +0x84 (o TriggerFX do efeito da instância)
    uint64_t jolts = 0;                  // IFeedback::ApplyJolt
    uint64_t scorches = 0, lights = 0, ai_zones = 0;
    uint64_t sounds = 0, sounds_skipped = 0, limited_sounds = 0;
    uint64_t forces = 0, damage_calls = 0;
    uint64_t player_hits = 0;            // as explosões que tiraram vida do carro do jogador (seção 26)
    double player_damage = 0;
    uint64_t render_simulate = 0;        // PhysicsObject::Simulate com render
    uint64_t guards = 0;
    std::vector<int32_t> other_events;   // os números de efeito executados sem porte (9, 36, 130...)
};
EffectsStats& effects_stats();

// A anotação das chamadas a sistemas não portados (a parte 18 compara com o original): o endereço da função do PS2 e
// até seis palavras.
struct EffectsCall {
    uint32_t fn;
    uint32_t a[6];
};
std::vector<EffectsCall>& effects_trace();
// A anotação só com isto ligado (a parte 18; o jogo não guarda nada).
bool& effects_trace_enabled();
void effects_note(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t a4 = 0,
                  uint32_t a5 = 0);

// O que o destrutor do ESpawnExplosion lê de fora (o port do nf_driving liga; nos testes, nulos).
struct EffectsEnv {
    // RSceneObj::GetInstancePosition(int instância, MATRIX4 &, bool): a matriz da instância (ou do objeto com -1).
    bool (*instance_position)(uint32_t scene, int32_t instance, float m[4][4]) = nullptr;
    // GFX::LookupReference(char *): o efeito da galeria (0: não há).
    uint32_t (*gfx_lookup)(const char* name) = nullptr;
    // GFX::Trigger (driving/fx/gfx_gallery.h): o efeito posto numa tela da galeria (nulo: só anotado).
    int32_t (*gfx_trigger)(uint32_t ref, const float pos[3], const float quat[4], const float pos2[3],
                           const float quat2[4], uint32_t scene, int32_t instance, uint32_t extra) = nullptr;
    // GFX::TriggerIncidental (o mesmo, a reserva da tela pela bandeira 1): os impactos das balas e o EHitWindow (nulo: só
    // anotado; docs/driving.md seção 26).
    int32_t (*gfx_trigger_incidental)(uint32_t ref, const float pos[3], const float quat[4], const float pos2[3],
                                      const float quat2[4], uint32_t scene, int32_t instance, uint32_t extra) = nullptr;
    // O nome do nível (0x1004f4: "jungleb_mis13b" dá cinco vezes o volume dos sons).
    const char* level = "";
    // RScorchWorld::ScorchGround(WWorldPos novo, pos, tipo 5, tamanho 8) e RHighLevelLightManager::AddExplosion(pos,
    // dano) do destrutor do ESpawnExplosion (driving/fx/decals.h, fx_lights.h; nulos: só anotados).
    void (*scorch_ground)(const float pos[4], int32_t type, float size) = nullptr;
    void (*explosion_light)(const float pos[4], float radius) = nullptr;
    // O EFireWeapon (driving/game/missile.h): RSceneObj::GetInstanceSystemID(int instância).
    uint32_t (*instance_system)(uint32_t scene, uint32_t instance) = nullptr;
    // AttributeSet::LookupInt(chave, 0) do PhysicsObject de uma imagem do PS2 (o +0x5c).
    int32_t (*lookup_int)(uint32_t object, const char* key) = nullptr;
    // O BulletStreakCount de um tipo de rastro (a tabela 0x423f38 +0x14 do RBulletStreak::Init).
    int32_t (*streak_count)(int32_t type) = nullptr;
    // O RPlayerCamera +0x1f4 (RPlayerCamera::WeaponFired grava o passo nele).
    int32_t* camera_1f4 = nullptr;
};
EffectsEnv& effects_env();

// Os globais do .data que o destrutor do ESpawnExplosion muda (a parte 18 os copia para o original).
struct EffectsGlobals {
    uint32_t ex_counter = 0;    // 0x32bbdc o próximo nome "Ex_Small"/"Ex_SmB0"
    uint32_t ring = 0;          // 0x32bbfc a vaga do anel dos sons
    int32_t ring_time[4] = {};  // 0x32bc00 o prazo de cada vaga (ticks de 100 Hz)
    int32_t limited_count = 0;  // 0x355ec0 ALimitedSound::fgCount (os vivos)
};
EffectsGlobals& effects_globals();

// Port: zera as contagens, a anotação e o estado dos sons e dos nomes (o começo de uma missão; a parte 18).
void effects_reset();

}  // namespace drv
