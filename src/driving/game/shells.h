#pragma once
// As balas do jogo de carro (o RayShell e o Shell de PVehicle.obj, o Simulation::SpawnShellObject de Simulation.obj, as
// utilidades de SWeaponManager.obj e o WCollisionMgr::CheckHitWindow; docs/driving.md seção 24).
//
// Como o PS2 atira uma bala: o EFireWeapon (driving/game/fire_weapon.cpp) das metralhadoras (as armas 0, 0x17, 0x18,
// 0x1a, 0x1b e 0x1d) cria um RayShell (0xd0 bytes, UMemory::FastAlloc) cujo construtor faz o tiro inteiro e se apaga no
// fim: o raio do cano até o alcance (220 m; 400 na 0x1a) contra os corpos da Simulation (CollideWithObjects: as esferas
// dos corpos, os personagens como esferas esticadas, a caixa dos corpos rígidos, o mais perto e o segundo mais perto
// guardados por ConsiderVictim) e depois contra o mundo em passos de um quarto do alcance, até 55 m cada
// (CollideWithWorld: WCollisionMgr::CheckHitWorld, as janelas, os efeitos e o dano do mundo); sem o mundo no caminho, o
// dano no corpo atingido (Fire, DamagePhysicsObject: o ApplyDamage do dono, a entrada +0x1c da tabela virtual, com 1/4
// de divisão e o tipo 2). Cada tiro guarda o rastro (StoreTracer: 64 vagas com o passo, as pontas, o tipo do rastro do
// RBulletStreak, o RSceneObj e o efeito), o clarão de quem atirou (AddFlash: 8 vagas, só dos personagens e das
// sentinelas) e o raio para os gatilhos (AddNewRayShellForTriggerCheck: 63 vagas que o WTriggerManager lê). O desenho dos
// rastros e dos clarões (DrawTracers, DrawFlashes, do RRenderWorldCamera::DrawBulletStreaks) não é deste módulo: as
// vagas ficam em rayshell_globals() para quem desenha.
//
// O Shell (0x90 bytes) é o projétil lento do canhão 0x10 (e da arma 0 se gEnableRayShells fosse 0: nunca, nada escreve
// o global): um PhysicsObject de tipo 7 com um corpo simples de tipo 5 (raio 0,3, massa 0,1, as bandeiras 0x169), que voa
// pela velocidade sorteada pelo RRandom do render (200..230 m/s; 170..200 no canhão) ou pela do tipo de rastro, vive
// 1,3 s (1 s no canhão) e, a cada passo (Shell::Simulate), vê os corpos que o DetectCollisionsSRB marcou (o dano, a
// explosão do canhão, ApplyWorldEffects) e o mundo do passo seguinte (CheckHitWorld: as janelas, a explosão, os efeitos e
// ApplyWorldDamage).
//
// Shell::ApplyWorldEffects: o efeito da galeria pelo material (GFX::TriggerIncidental: "bullet", "Henbult",
// "bulletcar"...), as faíscas (RParticleSystemManager::CreateParticles com o sistema "SPKB"), a zona da IA (3) e o som
// do impacto (um AOneShotSound "SFX_Bullet*" ou "SFX_Ricochet*" em "Bullet Impact"/"Bullet Ricochet").
// Shell::ApplyWorldDamage: a queimadura (RScorchWorld::ScorchWorld) e os estímulos 0x13..0x16 do RAnimEngine do objeto de
// cena da instância atingida (pelo dano: 10, 50, 250, 500).
//
// Portado do MIPS do PS2 (a verdade); os nomes são do DRIVING.SYM e do nightfire-cxbx (NightfireResearch, a versão de
// Xbox). Conferido na parte 20 do nf_driving_check (tests/driving_check_integ_shells*.cpp). O que vai para sistemas não
// portados (as partículas, a queimadura, o RHighLevelLightManager) é anotado (effects_note) e contado (shells_stats());
// o efeito da galeria (GFX::TriggerIncidental) também vai à galeria pelo effects_env().gfx_trigger_incidental quando o
// nf_driving o liga (seção 26).
//
// Divergências (cada uma também na função):
//  - o PS2 lê o +0x58 de um PhysicsObject nulo no PhysicsObject::IsOwnedBy quando a assinatura de quem atirou não acha
//    objeto (o endereço 0x58, a memória do núcleo): o port devolve falso e conta (guards);
//  - o Fire com dois atingidos lê o tipo (+0x40) do primeiro sem conferir que a assinatura ainda acha o objeto: guardado;
//  - o EHitWindow (o evento das janelas que quebram, sem número na tabela dos eventos) fica em hit_window_events() e o
//    destrutor (EHitWindow_Destroy, game/hit_window.cpp, seção 26) roda no fim do passe dos eventos, depois dos
//    numerados (no PS2, no mesmo anel).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "driving/game/explosion.h"

namespace drv {

struct WorldCollisionInfo;
struct WCollisionMgr;
struct WCollisionInstance;
class TrackWorld;

// ------------------------------------------------------------------------------------------------- o RayShell

// O RayShell do PS2 (0xd0 bytes).
struct alignas(16) RayShellData {
    float pos[4];          // +0x00 o começo do raio (o w fica: a memória do FastAlloc)
    float origin[3];       // +0x10 o começo do primeiro raio
    float damage;          // +0x1c (dobrado com a trapaça 0x1000 do UberBond +0x9a0 nos tiros do jogador)
    float dir[4];          // +0x20 a direção unitária (o w fica)
    float base_dir[3];     // +0x30 a direção do primeiro raio (os outros a perturbam)
    float range;           // +0x3c até 220 (3 nos raios depois do primeiro)
    float muzzle[3];       // +0x40 o cano (a ponta de trás do rastro)
    int32_t weapon;        // +0x4c
    int32_t state;         // +0x50 0 nada, 1 um atingido, 2 dois
    uint32_t sig;          // +0x54 quem atirou
    uint32_t unknown58[2];
    float hit1[3];         // +0x60 o ponto do primeiro atingido (o mais perto)
    uint32_t unknown6c;
    float victim1[3];      // +0x70 o lugar do primeiro (o centro ou o ponto na caixa)
    uint32_t sig1;         // +0x7c
    float dist1;           // +0x80
    uint32_t unknown84[3];
    float hit2[3];         // +0x90 o segundo
    uint32_t unknown9c;
    float victim2[3];      // +0xa0
    uint32_t sig2;         // +0xac
    float dist2;           // +0xb0
    uint32_t unknownb4[3];
    int32_t streak;        // +0xc0 o tipo do rastro (o BULLET_STREAK_TYPE; -1: sem rastro)
    uint32_t scene;        // +0xc4 (p32 RSceneObj) do efeito de tipo 6 do evento
    uint32_t effect;       // +0xc8 (p32) o efeito
    uint32_t vtable;       // +0xcc 0x0037bfb8
};
static_assert(sizeof(RayShellData) == 0xd0);
static_assert(offsetof(RayShellData, state) == 0x50 && offsetof(RayShellData, sig1) == 0x7c &&
              offsetof(RayShellData, sig2) == 0xac && offsetof(RayShellData, streak) == 0xc0 &&
              offsetof(RayShellData, vtable) == 0xcc);

// Um rastro (0x20 bytes, fTracer 0x3320c0) e o par de palavras ao lado (0x424180: o RSceneObj e o efeito).
struct RayTracer {
    float from[3];   // +0x00
    int32_t step;    // +0x0c o passo da Simulation (o ClearTracers apaga os anteriores)
    float to[3];     // +0x10
    int32_t type;    // +0x1c
};
static_assert(sizeof(RayTracer) == 0x20);
// Um clarão (0x10 bytes, fFlash 0x3328d0).
struct RayFlash {
    float pos[3];
    uint32_t sig;
};
// Um raio para os gatilhos (0x30 bytes, fActiveRayShells 0x332960).
struct ActiveRayShell {
    uint32_t sig;        // +0x00
    uint32_t unknown04[3];
    float start[3];      // +0x10
    float damage;        // +0x1c
    float end[3];        // +0x20 o começo + a direção x a distância
    float half;          // +0x2c a distância / 2
};
static_assert(sizeof(ActiveRayShell) == 0x30);

// Os globais do PVehicle.obj das balas (o .data e o .bss do PS2).
struct RayShellGlobals {
    int32_t enable = 1;                 // 0x3320b0 gEnableRayShells (1 no .data; nada o muda)
    int32_t num_tracers = 0;            // 0x3320b4 fNumTracers
    RayTracer tracers[64] = {};         // 0x3320c0 fTracer
    uint32_t tracer_side[64][2] = {};   // 0x424180 (o RSceneObj, o efeito)
    int32_t num_flashes = 0;            // 0x3328c0 fNumFlashes
    RayFlash flashes[8] = {};           // 0x3328d0 fFlash
    int32_t num_active = 0;             // 0x332950 fNumActiveRayShells
    ActiveRayShell active[64] = {};     // 0x332960 fActiveRayShells
    int32_t trigger_index = -1;         // 0x333560 o raio que tocou o gatilho (RayShell::GetNumTracers o põe; -1 nenhum)
    int32_t last_explosion = 0;         // 0x333564 o passo da última explosão da trapaça 0x1000
    // O construtor estático (0x0019ae98 -> 0x0019ada0) põe também 0x3d0d3c = 2^-126 e 0x3d0d40 = 0x7e800000 (não lidos
    // aqui).
};
RayShellGlobals& rayshell_globals();

// [driving] 0x00198e80 RayShell::RayShell(float alcance, float dano, float perturbação, WeaponType, SimObjSig, COORD3 &cano,
// COORD3 &direção, COORD3 &cano2, RSceneObj *, efeito, int raios, int rastro): o tiro inteiro (o objeto se apaga no fim,
// o ~RayShell com 3); `r` é a memória do FastAlloc (o port a dá: zerada no começo).
void RayShell_Construct(RayShellData& r, float range, float damage, float perturb, int32_t weapon, uint32_t sig,
                        const float pos[3], const float dir[3], const float muzzle[3], uint32_t scene, uint32_t effect,
                        int32_t rays, int32_t streak);
// [driving] 0x00199200 RayShell::~RayShell: a tabela virtual (o FastFree com o bit 0).
void RayShell_Destruct(RayShellData& r);
// Port: a cópia do último RayShell no fim do construtor (o objeto do PS2 antes do FastFree; para a conferência).
const RayShellData& rayshell_last();
// [driving] 0x001998c0 RayShell::CollideWithObjects: verdadeiro com algum atingido.
bool RayShell_CollideWithObjects(RayShellData& r);
// [driving] 0x00199f88 RayShell::CollideWithWorld(float &distância)
bool RayShell_CollideWithWorld(RayShellData& r, float& dist);
// [driving] 0x0019a3e0 RayShell::Fire(float &distância)
void RayShell_Fire(RayShellData& r, float& dist);
// [driving] 0x0019a548 RayShell::ConsiderVictim(float distância, SimObjSig, COORD3 &ponto, COORD3 &lugar)
void RayShell_ConsiderVictim(RayShellData& r, float dist, uint32_t sig, const float hit[3], const float victim[3]);
// [driving] 0x0019a710 RayShell::DamagePhysicsObject(SimObjSig, COORD3 &ponto, COORD3 &lugar)
void RayShell_DamagePhysicsObject(RayShellData& r, uint32_t sig, const float hit[3], const float victim[3]);
// [driving] 0x001997e0 RayShell::AddNewRayShellForTriggerCheck(float distância)
void RayShell_AddNewRayShellForTriggerCheck(const RayShellData& r, float dist);
// [driving] 0x0019a988 RayShell::StoreTracer(COORD3 &, COORD3 &)
void RayShell_StoreTracer(const RayShellData& r, const float from[3], const float to[3]);
// [driving] 0x0019a9b8 RayShell::StaticStoreTracer(COORD3 &, COORD3 &, int tipo, RSceneObj *, efeito): o tipo 0 começa
// 2 m adiante do cano se a distância passa de 2 m.
void RayShell_StaticStoreTracer(const float from[3], const float to[3], int32_t type, uint32_t scene, uint32_t effect);
// [driving] 0x0019abb8 RayShell::AddFlash(SimObjSig, COORD3 &)
void RayShell_AddFlash(uint32_t sig, const float pos[3]);
// [driving] 0x001992d8 RayShell::DeleteTracer(int), 0x00199358 ClearTracers(int passo), 0x00199780 Reset,
// 0x00199258 ClearActiveRayShells, 0x00199668 ClearFlashes, 0x00199230 GetNumActiveRayShells,
// 0x00199240 GetActiveRayShell(int), 0x00199268 GetNumTracers(int) (que no PS2 grava o índice do raio do gatilho) e
// 0x00199278 GetTracer (o raio desse índice, nulo com -1).
void RayShell_DeleteTracer(int32_t i);
void RayShell_ClearTracers(uint32_t step);
void RayShell_Reset();
void RayShell_ClearActiveRayShells();
void RayShell_ClearFlashes();
int32_t RayShell_GetNumActiveRayShells();
ActiveRayShell* RayShell_GetActiveRayShell(int32_t i);
void RayShell_GetNumTracers(int32_t index);
ActiveRayShell* RayShell_GetTracer();
// [driving] 0x0019ae98 global_constructors_keyed_to_gEnableRayShells (0x0019ada0): as vagas dos clarões e dos raios
// zeradas (a assinatura de cada uma).
void RayShell_StaticInit();

// ------------------------------------------------------------------------------------------------- o Shell

// O resto do Shell do PS2 (0x90 bytes): +0x6c..+0x90.
struct ShellData {
    int32_t weapon;     // +0x6c
    float damage;       // +0x70
    float explosion;    // +0x74 o dano da explosão (só acima de 1: o canhão, 1/4 do dano)
    int32_t life;       // +0x78 os passos de vida
    uint32_t unknown7c;
    float start[3];     // +0x80 o cano
    int32_t streak;     // +0x8c o tipo do rastro
};
static_assert(sizeof(ShellData) == 0x24);

class Shell : public SimpleObject {
public:
    ShellData d{};
    ~Shell() override;
    void simulate() override;
    uint32_t raw_word(uint32_t offset) override;
};

// [driving] 0x0019cd90 Shell::Shell(float dano, float explosão, WeaponType, COORD3 &dir, COORD3 &pos, SimObjSig, int
// rastro)
void Shell_Construct(Shell& s, float damage, float explosion, int32_t weapon, const float dir[3], const float pos[3],
                     uint32_t sig, int32_t streak);
// [driving] 0x0019d080 Shell::Simulate
void Shell_Simulate(Shell& s);
// [driving] 0x0019d5a8 Shell::ApplyWorldEffects(COORD4 &lugar, COORD4 &normal, int, WeaponType, WSurface &, int bandeiras
// do dano, PhysicsObject *): o efeito, as faíscas, a zona da IA, o som.
void Shell_ApplyWorldEffects(const float pos[4], const float normal[4], int32_t mode, int32_t weapon,
                             const uint8_t surface[2], int32_t damage_flags, PhysicsObject* object);
// [driving] 0x0019ddb8 Shell::ApplyWorldDamage(float dano, float tamanho, WWorldPos &, DecalType, WorldCollisionInfo &):
// a queimadura e os estímulos da instância atingida.
void Shell_ApplyWorldDamage(float damage, float size, const WWorldPos& wp, int32_t decal,
                            const WorldCollisionInfo& info);
// [driving] 0x001fffa8 Simulation::SpawnShellObject(float dano, float explosão, WeaponType, COORD3 &dir, COORD3 &pos,
// SimObjSig, int rastro): nulo sem vaga (CanSpawnSimpleRigidBody, 72 corpos simples).
Shell* SpawnShellObject(Simulation& sim, float damage, float explosion, int32_t weapon, const float dir[3],
                        const float pos[3], uint32_t sig, int32_t streak);
// [driving] 0x00201a40 Simulation::DeleteShellObject
void DeleteShellObject(Simulation& sim, Shell* s);

// ------------------------------------------------------------------------------------------------- as janelas

// O estado de uma janela atingida (WCollisionWindowDynData, 0x2c bytes, no mapa do WCollisionMgr +0x14: a instância ->
// a janela -> isto).
struct WindowDynData {
    int32_t type;       // +0x00 0..3 sorteado pelo RRandom (+4 acima de 10 m)
    int32_t hits;       // +0x04 os tiros depois do primeiro
    int32_t unknown08;
    float a[4];         // +0x0c um canto (no mundo; w 1)
    float b[4];         // +0x1c o outro canto (o x e o z do fim, o y do pé)
};
static_assert(sizeof(WindowDynData) == 0x2c);
// O EHitWindow (0x54 bytes; o construtor 0x0014f330): o ponto, a normal, a cópia do estado da janela e a bandeira.
struct HitWindowEvent {
    uint32_t image[21];
};
std::vector<HitWindowEvent>& hit_window_events();
// [driving] 0x0014f480 EHitWindow::~EHitWindow (docs/driving.md seção 26; game/hit_window.cpp): o vidro que quebra. O
// primeiro tiro numa janela (os tiros +0x28 = 0): o som SFX_GlassShatter1/2 (alternados pelo contador 0x0032bbc0) do
// banco Smackables, perto do chão (y < 10) o SFX_StoreAlarm do Generic uma vez a cada oito (o contador 0x0032bbc4), e na
// galeria o "Wd_Glass" no ponto e, no modo 1 (+0x50), o "Wd_Wsmk" no meio dos cantos; do segundo ao quinto tiro, o
// SFX_Headlight do Collisions. Os sons são AOneShotSound("Explosions"). O port roda a fila no fim do passe dos eventos
// (hit_windows_run, pelo trigger_run_events), depois dos eventos numerados: no PS2 o EHitWindow entra no mesmo anel.
void EHitWindow_Destroy(const HitWindowEvent& ev);
void hit_windows_run();
// Os contadores do destrutor (o .data do PS2: nunca zerados; o shells_reset não os toca).
struct HitWindowCounters {
    uint32_t shatter = 0;  // 0x0032bbc0
    uint32_t alarm = 0;    // 0x0032bbc4
};
HitWindowCounters& hit_window_counters();
// O que o destrutor pede de fora: o som (nulo: o AOneShotSound do port no banco da categoria; a conferência o grava).
struct HitWindowEnv {
    void (*sound)(int32_t category, const char* name, const char* mix, const float pos[3], float min_distance,
                  float max_distance, float max_distance2, float fx) = nullptr;
};
HitWindowEnv& hit_window_env();
struct HitWindowStats {
    uint64_t events = 0, shatter = 0, alarm = 0, headlight = 0, glass = 0, smoke = 0, guards = 0;
};
HitWindowStats& hit_window_stats();
// [driving] 0x002153b0 WCollisionMgr::CheckHitWindow(WorldCollisionInfo &, bool efeito, int modo): o choque numa barreira
// cai numa janela do artigo? Com `effect`, guarda o estado da janela (o mapa), cria o EHitWindow, troca a face por 0x70
// (vidro) e devolve verdadeiro; sem `effect` devolve falso mesmo achando a janela (como o PS2).
bool WCollisionMgr_CheckHitWindow(WorldCollisionInfo& info, bool effect, int32_t mode);
// O mapa das janelas atingidas (o WCollisionMgr +0x14; o port o guarda à parte, zerado quando o mundo muda).
std::map<uint32_t, std::map<uint32_t, WindowDynData>>& window_map();
// [driving] 0x0025b890 (sem nome): o ponto (x, y, z) pela matriz com a translação (a FPU; no lugar se a == out).
void Mat_TransformPoint3(const float* a, const float m[16], float* out);
// [driving] 0x0025b9f8 (sem nome): o vetor de quatro (x, y, z, w) pela matriz (a FPU; no lugar se a == out).
void Mat_TransformVec4(const float* a, const float m[16], float* out);

// ------------------------------------------------------------------------------------------------- as utilidades

// [driving] 0x001ff280 Simulation::FindPhysicsObjectSignature(SimObjSig): o dono da vaga da assinatura, se ela ainda é
// a dele (nulo com 0).
PhysicsObject* FindPhysicsObjectSignature(Simulation& sim, uint32_t sig);
// [driving] 0x00201ae0 Simulation::GetPlayerObject: o primeiro dos carros (+0xdcc); nulo sem carros (o PS2 leria o
// vetor vazio: contado).
PhysicsObject* GetPlayerObject(Simulation& sim);
// PhysicsObject::IsOwnedBy (missile.cpp) sobre qualquer dono: a corrente das assinaturas +0x58.
bool PhysicsObject_IsOwnedBy(PhysicsObject* self, PhysicsObject* other);
// [driving] 0x001956d8 PhysicsObject::GetRadius: o raio do corpo simples (+0x2c) ou a palavra +0x7c do rígido.
float PhysicsObject_GetRadius(PhysicsObject* o);
// [driving] 0x00195858 PhysicsObject::GetLinearVelocity: a velocidade do corpo (+0x20).
const float* PhysicsObject_GetLinearVelocity(PhysicsObject* o);
// [driving] 0x00211098 Util_CollideRayWithSphere(float raio, COORD3 &origem, COORD3 &dir, COORD3 &centro, COORD3 &ponto):
// o raio de 1000 m toca a esfera (de fora)?
bool Util_CollideRayWithSphere(float radius, const float origin[4], const float dir[4], const float centre[4],
                               float out[3]);
// [driving] 0x00211280 Util_CollideRayWithStretchedSphere(float raio, float esticamento, ...): a esfera esticada em y.
bool Util_CollideRayWithStretchedSphere(float radius, float stretch, const float origin[4], const float dir[4],
                                        const float centre[4], float out[3]);
// [driving] 0x002106c0 Util_GenerateMatrixUnit(MATRIX4 &, COORD3 &): a orientação que olha para dir (os w das linhas 0 e
// 1 são o lixo da pilha no PS2; aqui 0).
void Util_GenerateMatrixUnit(float out[4][4], const float dir[3]);
// [driving] 0x00210b60 Util_PerturbVector(float yaw, float pitch, COORD3 &out, COORD3 &in): giro em y e depois em z.
void Util_PerturbVector(float yaw, float pitch, float out[3], const float in[3]);
// [driving] 0x00210c10 Util_PerturbVector(COORD3 &out, float, COORD3 &in): os dois giros sorteados pelo Util_GaussRandom.
void Util_PerturbVectorGauss(float amount, float out[3], const float in[3]);
// [driving] 0x00210a30 Util_GaussRandom: o par de Box-Muller (o rand da libc e o log em dupla), o segundo guardado.
float Util_GaussRandom();
// [driving] 0x002a8bd0 rand (a newlib do PS2: next = next x 1103515245 + 12345, & 0x7fffffff).
int32_t ee_rand();
// O estado do rand (o _rand_next da estrutura _impure_ptr) e o do Util_GaussRandom (0x33a53c, 0x417040).
struct GaussState {
    uint32_t rand_next = 1;
    int32_t have = 0;
    float saved = 0.0f;
};
GaussState& gauss_state();
// [driving] 0x002c1330 __ieee754_log (a dupla em software da libgcc; o algoritmo da fdlibm) e 0x002bfea0 log (o
// invólucro: x > 0 dá o mesmo).
double ee_log(double x);

// [driving] 0x00208428 SMissionManager::GetEnemyAccuracyBoost: o acréscimo de pontaria da dificuldade (as configurações
// da missão +0x2ac, +0x2b0, +0x2b4).
int32_t SMissionManager_GetEnemyAccuracyBoost();

// ------------------------------------------------------------------------------------------------- o ambiente e as contas

// O que as balas leem de fora (o nf_driving liga; nos testes, os da conferência).
struct ShellsEnv {
    // WWorld::GetSceneObjFromInstance (fgWorld 0x33aac8) da instância de render de uma instância de colisão: o objeto
    // de cena (p32; o +0x28 é o RAnimEngine::Handle). Nulo: nenhum (o port não monta os objetos de cena do mundo).
    std::function<uint32_t(uint32_t render_instance)> scene_obj_of;
    // RAnimEngine::Handle::ProcessStimuli(estímulo, tempo, modo) de um Handle (p32): sem ele, a função do port.
    std::function<void(uint32_t handle, uint32_t stimulus, uint32_t steps, uint32_t mode)> stimuli;
    // RParticleLibrary::FindSystemViaTag("SPKB") (o sistema das faíscas; 0 nenhum).
    std::function<uint32_t()> spark_system;
    // A entrada +0x29c da tabela virtual do carro do jogador (o WTargetable dele; p32, 0 sem).
    std::function<uint32_t()> player_targetable;
    // Sentry::GetTarget (um WTargetable novo, p32, no alvo da sentinela; 0 sem): a sentinela não foi portada.
    std::function<uint32_t(PhysicsObject* sentry)> sentry_target;
    // RSceneObj::LocateFX(efeito, MATRIX4 &, bool) e RSceneObj::TransformMatrixByInstancePosition(índice, MATRIX4 &, bool)
    // do evento do EFireWeapon (o render).
    std::function<bool(uint32_t scene, uint32_t effect, float m[4][4])> locate_fx;
    std::function<void(uint32_t scene, uint32_t index, float m[4][4])> transform_by_instance;
    // A entrada +0x3c da tabela virtual do RSceneObj (TransformToWorldSpace: vazia no RSceneObj e no RVehicle).
    std::function<void(uint32_t scene, float m[4][4])> to_world;
    // AttributeSet::LookupBool(chave, 0) do PhysicsObject de uma imagem (o HAS_TURRET do carro do jogador).
    std::function<bool(uint32_t image, const char* key)> lookup_bool;
    // O ator de um personagem (o +0x70 do Human): a entrada +0x24 do +0x140 dele e, se não for nula, a +0x13c do que ela
    // devolve (o EFireWeapon tira (4 - valor) x 30 da precisão). Falso: a primeira deu nulo.
    std::function<bool(uint32_t actor, int32_t& value)> actor_vehicle_value;
    // A velocidade de um tipo de rastro (a tabela 0x423f38 +0x10 do RBulletStreak::Init): falso sem a tabela.
    std::function<bool(int32_t type, float& speed)> streak_speed;
    // RPlayerCamera::WeaponFired(arma) do RPlayerCamera (0x333ed8 +4): sem ele, o port grava o effects_env().camera_1f4.
    std::function<void(int32_t weapon)> weapon_fired;
};
ShellsEnv& shells_env();

// Os contadores do .data dos efeitos das balas (Shell::ApplyWorldEffects: 0x333584 o concreto, 0x333588 o ricochete,
// 0x333580 o sistema das faíscas já procurado e 0x3d0d4c o sistema) e dos sons das metralhadoras do EFireWeapon
// (0x32bba0 o nome, 0x32bba4 o dos personagens, 0x32bba8 o dos carros da IA).
struct ShellEffectsCounters {
    uint32_t concrete = 0, ricochet = 0;
    int32_t sparks_found = 0;
    uint32_t sparks = 0;
};
ShellEffectsCounters& shell_effects_counters();
struct FireSoundCounters {
    uint32_t name = 0, actor = 0, car = 0;
};
FireSoundCounters& fire_sound_counters();

// O que o port conta.
struct ShellsStats {
    uint64_t rayshells = 0, rays = 0, world_hits = 0, fired = 0, damage_calls = 0;
    uint64_t tracers = 0, flashes = 0, active = 0, too_many = 0;
    uint64_t shells_spawned = 0, shells_refused = 0, shells_simulated = 0, shells_deleted = 0, shell_hits = 0;
    uint64_t world_effects = 0, sounds = 0, gfx = 0, particles = 0, scorches = 0, stimuli = 0;
    uint64_t windows_checked = 0, windows_hit = 0, window_events = 0, cheat_explosions = 0;
    uint64_t guards = 0;
    uint64_t player_hits = 0;  // os raios dos outros que acertaram o carro do jogador (seção 26)
    double player_damage = 0;  // e a vida que ele perdeu com eles
};
ShellsStats& shells_stats();
// Port: zera as contas, os globais das balas, o estado do rand e o mapa das janelas (o começo de uma missão).
void shells_reset();

// As palavras do PS2 de um PhysicsObject do port (a imagem: o PBondCarData do carro, o WWorldPos dos de corpo simples;
// raw_word para o resto): o que o EFireWeapon e as balas leem pelo deslocamento.
uint32_t object_image(PhysicsObject* o);
PhysicsObject* object_of_image(Simulation& sim, uint32_t image);
uint32_t object_word(PhysicsObject* o, uint32_t offset);
// Grava uma palavra (no carro, nos de corpo simples até +0x6c, ou pelo PhysicsObject::set_raw_word dos outros donos: o
// helicóptero, a sentinela; sem nenhum, conta como guarda).
void object_set_word(PhysicsObject* o, uint32_t offset, uint32_t value);
// Port: o índice do objeto na ordem do GetOrderedBody (a vaga rígida, ou 48 + a vaga simples; -1 nenhum): o que as
// anotações do ApplyDamage (0x00189598) levam, como as do míssil e da explosão.
int32_t object_ordered_index(Simulation& sim, PhysicsObject* o);

}  // namespace drv
