#pragma once
// Os objetos de cena que o carro derruba (o Smackable do DRIVING.ELF, Smackable.obj; o EWakeupSmackable de
// DTuningFile.obj; o que o RSceneObj faz com a física, RSceneObj.obj; docs/driving.md seção 18).
//
// Cada objeto de cena derrubável (os postes, as placas, as bancas de jornal, as cadeiras: o registro do 'ps  ' da
// instância com o bit 1 do byte +5) tem um gatilho cuja lista traz um EWakeupSmackable (o evento 35). Quando um carro
// toca o gatilho (driving/game/triggers.h), o evento acorda o objeto: Simulation::SpawnSmackableObject cria um
// Smackable, um PhysicsObject com um corpo rígido de tipo 4 (a caixa das medidas do 'pD' do objeto, as alavancas dos
// pontos de colisão dele, a massa e a vida da "PhysicsData" da coleção "smackable" do objeto), nascido parado e dormindo.
// O choque com o carro na SimulateGame (CollideWithObject) o acorda e o empurra; a cada passo o Smackable leva o RSceneObj
// (a matriz da instância) para a pose do corpo (RSceneObj::UpdatePosition, com o nó da árvore do mapa). Parado por alguns
// passos ele dorme (GoToSleep: o gatilho vai para onde o objeto ficou e volta a valer; o objeto sai do mapa da
// Simulation e é apagado no começo do próximo ESimFrameUpdate, quando a instância fica na última pose do corpo).
//
// Portado do MIPS do PS2 (a verdade); o nightfire-cxbx (NightfireResearch, a versão de Xbox) deu os nomes. Ficam de fora
// (contados em smackable_stats()): o som (o ASmackable), as animações de estímulo do RAnimEngine e o TriggerFX do render
// que o ApplyDamage pede (os pedaços que se soltam, as faíscas), o objeto jogado (o ramo Newton do EWakeupSmackable, o bit
// 6 do byte +5), os raios das armas (o ramo do RayShell e a força do ApplyDamage de tipo 2) e a lista dinâmica da grade
// (o WGrid::AddGridNodeDynamicElement do gatilho que o GoToSleep move: o port não o acha de novo).
//
// Leiaute do PS2 nos dados (SmackableData, 0x88 bytes, static_assert) para a parte 14 do nf_driving_check.

#include <cstdint>
#include <vector>

#include "driving/physics/rigid_body.h"
#include "driving/physics/world_collision.h"
#include "driving/world_vis.h"

namespace drv {

struct RSceneObj;
struct WTrigger;
struct Simulation;
struct SmackablePhysicsData;

// O Smackable do PS2 (0x88 bytes): o PhysicsObject (0x6c) e os campos do Smackable. Aqui a partir de +0x40 (o WWorldPos
// do começo, alinhado a 16, fica no objeto do port, Smackable::world_pos_).
struct SmackableData {
    int32_t type;             // +0x40 o tipo do PhysicsObject (2)
    uint16_t flags;           // +0x44 bit 0: corpo simples (0: rígido)
    int16_t slot;             // +0x46 a vaga do corpo
    uint32_t render;          // +0x48 (p32 RSceneObj)
    uint32_t actor;           // +0x4c
    uint32_t feedback;        // +0x50
    uint32_t hit_point_loc;   // +0x54 (p32 float) a vida
    uint32_t unknown58;
    uint32_t attributes;      // +0x5c o AttributeSet (port: a PhysicsData)
    uint32_t collider;        // +0x60
    uint32_t unknown64;
    uint32_t vtable;          // +0x68 (0x37cec0)
    uint32_t sleep_delay;     // +0x6c os passos parados antes de dormir (o EWakeupSmackable: 0,1 s de passos)
    uint32_t awake_timer;     // +0x70 10 s de passos, recarregado acima de 0,1 m/s
    float hit_points;         // +0x74
    uint32_t params;          // +0x78 (p32 SmackablePhysicsData)
    uint32_t trigger;         // +0x7c (p32 WTrigger)
    uint32_t proc_anim;       // +0x80 (p32) o registro do 'ps  ' da instância (o byte +4: o dano)
    uint32_t audio;           // +0x84 o ASmackable (port: 0)
};
static_assert(sizeof(SmackableData) == 0x48, "Smackable (+0x40..+0x88)");

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // o preenchimento do WWorldPos alinhado a 16 bytes
#endif
class Smackable : public PhysicsObject {
public:
    WWorldPos world_pos_{};   // +0x00 o do PhysicsObject
    SmackableData d{};
    ~Smackable() override;  // [driving] 0x0019e590 Smackable::~Smackable (e o ~PhysicsObject 0x00195568)
    RigidBody* body() const;
    RSceneObj* scene() const;

    PhysicsAttributes* attributes() override { return nullptr; }  // o tipo 4 não consulta
    WWorldPos* world_pos() override { return &world_pos_; }
    // [driving] 0x00195ae0 PhysicsObject::GetCollisionGeometry: os pontos do 'pD' do objeto de cena (+0x3c, +0x60) e a
    // altura menos o GetRenderOffset do RSceneObj (0).
    const CollisionPoint* collision_geometry(uint32_t& count, float& y_offset) override;
    float hit_points() override;  // [driving] 0x00195a30 PhysicsObject::GetHitPoints
    void simulate() override;     // [driving] 0x0019e718 Smackable::Simulate
    // [driving] 0x0019e8c0 Smackable::ApplyDamage (a vida; os estímulos e o TriggerFX contados; devolve 0x10)
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// [driving] 0x0019e100 Smackable::Smackable(RSceneObj *, WTrigger *, COORD3 &velocidade, unsigned int): o PhysicsObject
// (a vaga, o AttributeSet), os campos, a vida (menos o dano do 'ps  '), o render e o corpo rígido de tipo 4. Devolve o
// corpo (nulo sem vaga: o PS2 escreveria fora do vetor).
RigidBody* Smackable_Construct(Smackable& s, RSceneObj& scene, WTrigger* trigger, const float velocity[3],
                               uint32_t sleep_delay);
// [driving] 0x0019e610 Smackable::GoToSleep
void Smackable_GoToSleep(Smackable& s);

// [driving] 0x001ffa90 Simulation::SpawnSmackableObject(RSceneObj *, WTrigger *, COORD3 &, unsigned int)
Smackable* SpawnSmackableObject(Simulation& sim, RSceneObj& scene, WTrigger* trigger, const float velocity[3],
                                uint32_t sleep_delay);
// [driving] 0x002012d0 Simulation::CanSpawnRigidBody(COORD3 &, bool): com mais de 36 corpos (48 com `more`), o
// Smackable dormindo mais longe do ponto vai dormir de vez; verdadeiro se ainda há vaga.
bool CanSpawnRigidBody(Simulation& sim, const float pos[3], bool more);
// Simulation::DeleteSmackableObject (0x002019a0, com o DeletePhysicsObject 0x00200f18 de explosion.cpp; a parte dos Smackable): sai do mapa e do vetor; o objeto é
// apagado no DeleteDeadObjects do próximo ESimFrameUpdate (driving/physics/simulation.h).
void DeleteSmackableObject(Simulation& sim, Smackable& s);
// A contagem de corpos rígidos em uso (Simulation +0x8, port: as vagas com dono).
int32_t RigidBodiesInUse(const Simulation& sim);

// O EWakeupSmackable (0x3c bytes no PS2; a criadora 0x15e4b0): os três registros dos dados (o RSceneObj, a instância e
// o 'ps  ', TagReference do 'el') e o que ela lê do EventDynamicData.
struct WakeupSmackable {
    uint32_t scene;       // +0x04 (p32 RSceneObj) os dados +4; nulo: o do 'ps  ' (+8)
    uint32_t instance;    // +0x08 (p32) os dados +8
    uint32_t proc_anim;   // +0x0c (p32) os dados +0xc: o registro do 'ps  '
    uint32_t has_ray;     // +0x10 o EventDynamicData +0x44
    uint32_t trigger;     // +0x30 (p32 WTrigger) o +0x30
    uint32_t flag;        // +0x34 o +0x38
    int32_t index;        // +0x38 o +0x3c
};
WakeupSmackable WakeupSmackable_FromData(const uint8_t* data, uint32_t trigger, uint32_t has_ray, uint32_t flag,
                                         int32_t index);
// [driving] 0x0015de48 EWakeupSmackable::~EWakeupSmackable: o objeto do bit 1 vira um Smackable (o do bit 6, o Newton, e
// o raio das armas contados); sem objeto, o gatilho volta a valer.
void EWakeupSmackable_Execute(const WakeupSmackable& e);

// O RSceneObj com a física (RSceneObj.obj). `objdesc` é o RSceneObj +0xc do PS2 (o 'pD' do primeiro artigo do grupo do
// modelo, que o UseInstanceList grava; o port o acha quando precisa, scene_object_desc).
// O 'pD' de um objeto de cena: o que o RSceneObj::UseArticle (0x001c7658) / UseInstanceList (0x001c7940) gravaria em +0xc.
uint32_t scene_object_desc(const RSceneObj& o);
// [driving] 0x001c9190 RSceneObj::GetTransform: a matriz da instância (os w zerados, o da linha 3 = 1).
void RSceneObj_GetTransform(const RSceneObj& o, float m[4][4]);
// [driving] 0x001c9778 RSceneObj::GetBoundingDimensions: a metade da caixa do 'pD'.
void RSceneObj_GetBoundingDimensions(const RSceneObj& o, float out[3]);
// [driving] 0x001c9c78 RSceneObj::GetVelocity: a do corpo (nulo sem física; o trilho do RPathEngine não foi portado).
const float* RSceneObj_GetVelocity(const RSceneObj& o);
// [driving] 0x001c9b28 RSceneObj::SetPhysics: ao trocar, a instância fica na pose do corpo antigo (VU0_SQTquattom4).
void RSceneObj_SetPhysics(RSceneObj& o, Smackable* physics);
// [driving] 0x001c8d70 RSceneObj::UpdatePosition(bool): a instância na pose do corpo e, se andou, o nó da árvore.
void RSceneObj_UpdatePosition(RSceneObj& o, bool changed);
// [driving] 0x001c8b78 RSceneObj::Hide e 0x001c8b90 Show: o bit 24 das bandeiras.
void RSceneObj_Hide(RSceneObj& o);
void RSceneObj_Show(RSceneObj& o);
// [driving] 0x0022c530 WTree::CheckContainment(CARP::MapNode *, COORD4 &, COORD4 &) e 0x0022c6b8 WTree::FindNode(raiz,
// nó, ...): o nó atual se ainda contém a esfera, senão a busca da raiz.
bool WTree_CheckContainment(const MapNode& node, const float box[4], const float sphere[4]);
// [driving] 0x00230c48 WWorld::GetProcAnimStateFromInstance: o registro do 'ps  ' de uma instância do mundo (bit 0x10).
uint32_t WWorld_GetProcAnimStateFromInstance(uint32_t instance);
// [driving] 0x00210840 Util_GenerateTensor(float massa, float x, float y, float z): a diagonal do tensor inverso (uma
// barra fina quando 5 (x + z) < y, senão uma caixa).
void Util_GenerateTensor(float out[3], float mass, float x, float y, float z);
// [driving] 0x0022cad8 WTrigger::UpdateRotPos(COORD3 &, COORD3 &): o gatilho na posição (o centro menos meia altura;
// uma caixa vira cilindro, de raio 0,55 x altura se o raio for menor), e a lista dinâmica da grade (contada).
bool WTrigger_UpdateRotPos(WTrigger& t, const float pos[3]);

// O que o port precisa do mundo para os objetos de cena (nf_driving os põe na carga da pista): o WRender (as instâncias
// e o 'ps  ', para o GetProcAnimStateFromInstance) e as instâncias que a física moveu (para refazer a matriz em cache do
// desenho: o nf_driving as esvazia).
struct SmackableWorld {
    uint32_t instances = 0;      // (p32) WRender +0x00
    int32_t instance_count = 0;  // WRender +0x28 (a soma das passadas)
    uint32_t proc_anims = 0;     // (p32) WRender +0x2c
    std::vector<uint32_t> moved; // (p32) as instâncias que o UpdatePosition ou o SetPhysics reescreveram
};
SmackableWorld& smackable_world();

// Os contadores do port.
struct SmackableStats {
    uint64_t woken = 0;          // EWakeupSmackable que criaram um Smackable
    uint64_t no_room = 0;        // sem vaga (CanSpawnRigidBody)
    uint64_t newton = 0;         // o ramo Newton (não portado)
    uint64_t rays = 0;           // o ramo das armas (não portado)
    uint64_t asleep = 0;         // GoToSleep
    uint64_t hidden = 0;         // GoToSleep sem gatilho de volta (Hide)
    uint64_t deleted = 0;        // apagados no DeleteDeadObjects
    uint64_t damage = 0;         // ApplyDamage
    uint64_t stimuli = 0;        // os estímulos do RAnimEngine e o TriggerFX que o ApplyDamage pediria
    uint64_t dynamic_grid = 0;   // WGrid::AddGridNodeDynamicElement (o gatilho movido)
    uint64_t guards = 0;
};
SmackableStats& smackable_stats();
// Os Smackable vivos (o vetor da Simulation +0xde4; port: aqui).
std::vector<Smackable*>& smackables();

}  // namespace drv
