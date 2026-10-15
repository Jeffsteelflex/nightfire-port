#pragma once
// O míssil do jogo de carro (o Missile de Missile.obj, o Simulation::SpawnMissileObject de Simulation.obj, o
// RMissileStreak de RDebris.obj e o ASound de BondAudioPS2.lib; docs/driving.md seção 22).
//
// Como o PS2 faz voar: o EFireWeapon (a chave da animação do lançador, driving/game/weapons.h) chama
// Simulation::SpawnMissileObject com a direção e a posição da instância do míssil no carro; o Missile é um
// PhysicsObject de tipo 6 com um corpo simples de tipo 4 (raio 0,3, massa 4; as bandeiras 0x1c9: anda, testa os
// corpos rígidos e os simples pelas caixas), um RSceneObj que desenha a instância do carro (RSceneObj::UseInstance) e
// um ASound do voo ("SFX_MissleTrans"...). Cada tipo de arma dá a velocidade máxima, a aceleração, a curva, o tempo em
// linha reta, a vida e o som (o switch do construtor: 1 o foguete, 2 o míssil do Vanquish, 0xe o EMP, 0x15 e 0x16 os
// torpedos, 0x19, 0x1c, 0x1e, 0x1f; o resto, um foguete reto). A cada passo (Missile::Simulate, do Simulation::
// SimulateGame) o míssil vira para o alvo (SteerMissile: o slerp rápido para a direção do alvo, a aceleração para a
// velocidade máxima, o zigue-zague dos mísseis do jogador), e vê se bateu (CheckForCollision): a vida que acaba, os
// corpos da Simulation que o DetectCollisionsSRB marcou (as caixas), o chão e os prédios entre a posição anterior e a de
// agora (WCollisionMgr::CheckHitWorld). Ao bater cria o ESpawnExplosion (Simulation::SpawnExplosion,
// driving/game/explosion.h) e fica "morrendo" por dois segundos (esconde o modelo, para o som) antes de sair.
//
// Portado do MIPS do PS2 (a verdade); os nomes são do nightfire-cxbx (NightfireResearch, a versão de Xbox). Conferido na
// parte 18 do nf_driving_check (tests/driving_check_missile.cpp). Divergências e o que não foi portado (contados em
// missile_stats(), anotados em effects_trace()): a mira (o WTargetPicker: só o alvo do mundo, à frente do carro), o
// RSceneObj do modelo (o port guarda a instância para o desenho do nf_driving), o rastro e a luz do Missile::Render
// (RMissileStreak, desenhado pelo EAGL DynamicModel; RHighLevelLightManager::AddMissile), a luz (RHighLevelLightManager::AddMissile), o som do lock
// (AMenuSound), o PBondCar_PosInSoftZone dos torpedos 0x16, a quebra das janelas (WCollisionMgr::CheckHitWindow) e o
// dano das instâncias do mundo (Shell::ApplyWorldDamage: só os estímulos do RAnimEngine portados).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "driving/game/explosion.h"
#include "driving/game/target_picker.h"

namespace drv {

// O WTargetable do PS2 (0x50 bytes; WTargetable.obj): o que o míssil lê do alvo.
struct WTargetableData {
    float pos[4];           // +0x00
    uint32_t unknown10[9];  // +0x10
    int32_t kind;           // +0x34 0 um ponto, 1 um PhysicsObject (+0x48), 3 um veículo da IA (+0x48 AIVehicle)
    int32_t fixed;          // +0x38 não conta referências (o alvo de um tiro da IA)
    int32_t refs;           // +0x3c
    int32_t unknown40;      // +0x40
    int32_t unknown44;
    uint32_t object;        // +0x48 (p32)
    uint32_t unknown4c;
};
static_assert(sizeof(WTargetableData) == 0x50);

// O rastro do míssil (RMissileStreak, 0x650 bytes): os pontos de uma faixa de triângulos.
struct RMissileStreakData {
    int32_t count;           // +0x00 os pontos (até 0x60, de dois em dois)
    int32_t tick;            // +0x04 os Update (o par novo a cada dois)
    int32_t kind;            // +0x08 0 o rastro comum, 1 o do 0x19, 2 o dos torpedos (a largura 0x334e30, o
                             //   deslocamento 0x334e20 e o GeoPrim 0x3de1a0 pelo tipo)
    int32_t unknown0c;
    float verts[100][4];     // +0x10 (o Update grava até o 0x60, mais um par além)
};
static_assert(sizeof(RMissileStreakData) == 0x650);

// O resto do Missile do PS2 (0x730 bytes): +0x6c..+0x730.
struct MissileData {
    uint32_t target;          // +0x6c (p32 WTargetableData)
    float max_speed;          // +0x70
    float accel;              // +0x74
    float turn;               // +0x78 o fator do slerp para o alvo
    float unknown7c;          // +0x7c
    float steer_scale;        // +0x80 (0x331928: 0,004; o SetUserSteer)
    int32_t straight_time;    // +0x84 os passos em linha reta antes de virar
    int32_t arm_time;         // +0x88 o passo a partir do qual bate nos corpos
    int32_t decay_time;       // +0x8c os passos morrendo (2 s; o EMP 1 s)
    int32_t life;             // +0x90 os passos de vida
    int32_t start_step;       // +0x94
    uint32_t unknown98[2];
    RMissileStreakData streak;  // +0xa0
    uint32_t sound;           // +0x6f0 (ASound: o port guarda o ponteiro nativo à parte)
    int32_t weapon;           // +0x6f4 o tipo da arma
    float steer[2];           // +0x6f8 a direção do jogador (o míssil guiado)
    float steer_target[2];    // +0x700 SetUserSteerX/Y
    int32_t locked;           // +0x708 já mira (sem a curva lenta)
    int32_t detonate;         // +0x70c
    int32_t decaying;         // +0x710
    float volume;             // +0x714 o som entrando (0 a 1 em 15 passos)
    float damage;             // +0x718
    int32_t beep_time;        // +0x71c o próximo bipe do alvo travado
    float wiggle[2];          // +0x720 o zigue-zague (x, y)
    float wiggle_phase;       // +0x728
    uint32_t unknown72c;
};
static_assert(sizeof(MissileData) == 0x6c4);

class ASound;

class Missile : public SimpleObject {
public:
    MissileData d{};
    ASound* sound = nullptr;   // o +0x6f0 do PS2
    // O modelo que o RSceneObj desenha (RSceneObj::UseInstance: a instância do carro que disparou), para o nf_driving.
    uint32_t source_scene = 0;  // (p32 RSceneObj) o carro
    int32_t source_instance = -1;
    ~Missile() override;
    void simulate() override;
    bool collision_bounds(float half[4]) override;
    uint32_t raw_word(uint32_t offset) override;
};

// [driving] 0x00180bc8 Missile::Missile(WeaponType, float dano, COORD3 &dir, COORD3 &pos, ObjSig, WTargetable *, int
// sistema) (com a velocidade inicial em f12)
void Missile_Construct(Missile& m, float speed, float damage, int32_t weapon, const float dir[3], const float pos[3],
                       uint32_t sig, uint32_t target, int32_t system);
// [driving] 0x00181c80 Missile::Simulate
void Missile_Simulate(Missile& m);
// [driving] 0x00181e70 Missile::SteerMissile
void Missile_SteerMissile(Missile& m);
// [driving] 0x00182978 Missile::CheckForCollision
void Missile_CheckForCollision(Missile& m);
// [driving] 0x00181df0 Missile::SetUserSteerX(float) e 0x00181e30 SetUserSteerY(float)
void Missile_SetUserSteerX(Missile& m, float x);
void Missile_SetUserSteerY(Missile& m, float y);
// O WTargetPicker do PS2 (0x33a970): a estrutura inteira e as funções estão em game/target_picker.h (seção 24); aqui o
// alvo do mundo (+0x10) do UpdateSelection e os contadores do port.
struct TargetPickerData : WTargetPickerData {
    uint64_t updates = 0, pending = 0, guards = 0;
};
TargetPickerData& target_picker();
// Os globais do WTargetable.obj: 0x33a964 fTargetsInWorld (o +0x28 de cada alvo novo, & 3) e 0x33a960 os vivos.
struct TargetGlobals {
    uint32_t targets_in_world = 0;
    int32_t live = 0;
};
TargetGlobals& target_globals();
// [driving] 0x0022aab0 WTargetPicker::UpdateSelection: só a parte do alvo do mundo (o lugar do carro do jogador mais a
// frente x 175; num submarino o choque do segmento com o mundo). O PS2 a chama a cada quadro desenhado
// (RRenderWorldCamera::DrawStaticWorldGeometry -> UpdateTargets); o nf_driving, a cada ERenderFrame.
void WTargetPicker_UpdateWorldTarget();
// [driving] 0x0022af58 WTargetPicker::GetWorldTarget: um WTargetable novo (p32, fixo) no alvo do mundo.
uint32_t WTargetPicker_GetWorldTarget();
// WTargetable::AddReference (0x00229ba0) e RemoveReference (0x00229bb0; com zero referências o alvo é apagado se é do
// port: WTargetable_Adopt).
void WTargetable_AddReference(uint32_t target);
void WTargetable_RemoveReference(uint32_t target);
void WTargetable_Adopt(uint32_t target);
// Um alvo novo (p32) num ponto, não fixo (o chamador põe o +0x38): o WTargetable_NewPoint (0x00229490).
uint32_t WTargetable_NewAtPoint(const float pos[3]);
// WTargetable::AddReference (0x00229ba0) de um alvo (p32).
void WTargetable_AddRef(uint32_t t);
// [driving] 0x002064d8 SMissionManager::SetDriveMissile(Missile *) pelos chamadores do míssil (anotado; o gerente da
// missão, se houver).
void Missile_DriveMissile(uint32_t missile);
// [driving] 0x00183888 Missile::SetTarget(WTargetable *)
void Missile_SetTarget(Missile& m, uint32_t target);
// [driving] 0x001ffc40 Simulation::SpawnMissileObject(float vel, float dano, int tipo, COORD3 &dir, COORD3 &pos, ObjSig,
// WTargetable *, int sistema): nulo sem vaga.
Missile* SpawnMissileObject(Simulation& sim, float speed, float damage, int32_t weapon, const float dir[3],
                            const float pos[3], uint32_t sig, uint32_t target, int32_t system);

// [driving] 0x0014ca60 EFireWeapon::~EFireWeapon (o evento 10; driving/game/fire_weapon.cpp): o tiro de uma instância
// de arma de um RSceneObj (a imagem do objeto: e.obj, +4 o efeito, +8 o RSceneObj, +0xc a cópia da instância, +0x10 o
// índice, +0x14 o veículo da IA).
void EFireWeapon_Execute(const PendingEvent& e);
// O que o EFireWeapon do port conta (os caminhos não portados: ver fire_weapon.cpp).
struct FireStats {
    uint64_t events = 0, missiles = 0, sounds = 0, menu_sounds = 0, jolts = 0;
    uint64_t locate_fx = 0, ai_vehicle = 0, ai_aim = 0, camera_aim = 0, camera_rocket = 0, helicopter = 0;
    uint64_t other_projectiles = 0, guards = 0;
};
FireStats& fire_stats();

// O que o port conta.
struct MissileStats {
    uint64_t spawned = 0, refused = 0, simulated = 0, exploded = 0, deleted = 0;
    uint64_t world_hits = 0, body_hits = 0, expired = 0;
    uint64_t soft_zone = 0, car_emp = 0, windows = 0, world_damage = 0, programmer_events = 0;
    uint64_t lock_beeps = 0, lights = 0, guards = 0;
};
MissileStats& missile_stats();
// Os globais do .data do Missile.obj que o construtor muda (a parte 18 os copia para o original).
struct MissileGlobals {
    int32_t wiggle_toggle = 1;  // 0x33194c o lado do zigue-zague
    float pitch = 1.0f;         // 0x331950 o tom do som do voo
};
MissileGlobals& missile_globals();

// O choque de um segmento com o mundo (driving/physics/world_hit.cpp).
struct WorldCollisionInfo;
struct WGrid;
struct WCollisionMgr;
struct WCollisionObject;
struct InstanceListEntry;
class TrackWorld;
// [driving] 0x00215cc8 WCollisionMgr::CheckHitWorld(COORD4 *, WorldCollisionInfo &): o choque do segmento seg[0]..seg[1]
// (dois quadwords) com as barreiras, o chão e os objetos (o mais perto do começo).
bool WCollisionMgr_CheckHitWorld(const float* segment, WorldCollisionInfo& info);
// [driving] 0x002327a0 WWorldPos::HeightAtPoint(COORD3 &, bool)
float WWorldPos_HeightAtPoint(const WWorldPos& wp, const float point[3]);
// [driving] 0x0021d298 WGrid::FindNodes(COORD4 *, vector<unsigned int> &)
void WGrid_FindNodesSeg(const WGrid& g, const float* seg, std::vector<uint32_t>& cells);
// [driving] 0x00217c88 WCollisionMgr::GetInstanceList(WCollisionInstanceCacheList &, COORD4 *) (com 0x00217768 Guts)
void WCollisionMgr_GetInstanceListSeg(WCollisionMgr& mgr, const WGrid& g, std::vector<InstanceListEntry>& list,
                                      const float* seg);
// [driving] 0x00219630 WCollisionMgr::GetBarrierNormal(WCollisionInstanceCacheList &, COORD4 *, WorldCollisionInfo &)
bool WCollisionMgr_GetBarrierNormalInst(const WCollisionMgr& mgr, const std::vector<InstanceListEntry>& list,
                                        const float* seg, WorldCollisionInfo& info);
// [driving] 0x00218150 WCollisionMgr::GetObjectLists(..., COORD4 *) (com 0x00217e30 Guts)
void WCollisionMgr_GetObjectListsSeg(const TrackWorld& world, std::vector<WCollisionObject*>& cylinders,
                                     std::vector<WCollisionObject*>& boxes, const float* seg);
// [driving] 0x00218d10 WCollisionMgr::GetOBBObjectIntersection
bool WCollisionMgr_GetOBBObjectIntersection(const float* seg, const float m[16], const float half[4], float* out);
// [driving] 0x00218960 WCollisionMgr::GetOBBObjectNormal (com 0x00219280 GetClosestIntersectingOBBObject)
bool WCollisionMgr_GetOBBObjectNormal(const TrackWorld& world, const float* seg,
                                      const std::vector<WCollisionObject*>& list, WorldCollisionInfo& info);
// [driving] 0x0022f6d8 FasterSegmentIntersect
bool FasterSegmentIntersect(const float* s, const float* e, float* out);
uint64_t world_hit_guards();

}  // namespace drv
