#pragma once
// Os atores das missões de carro (o "Act*" do DRIVING.ELF: ActActorDatabase.obj e ActTextureDatabase.obj; docs/driving.md
// seções 20 e 24): o ActManager (os bancos de dados dos esqueletos, dos modelos, das texturas, das armas, das animações
// e os eventos), o ActActorDatabase (a lista dos atores), o ActActor (o personagem posto no mundo: as matrizes, o poser,
// o controlador da animação, as armas nos ossos), o ActCharacter (o modelo, as texturas e as armas), o ActCharacterInfo
// (a cadeia "esqueleto.modelo.texturas.escala"), o ActWeapon (a arma: um RSceneObj com o RAnimEngine::Handle do CARP
// dela, por onde o tiro passa), os eventos das animações (o ActEventResolver/EventTarget, o ActEvents e os tratadores
// FireEvent, DropWeaponEvent, HeightCalcEvent) e os controladores da animação (StandardAnimationController).
//
// Como um personagem atira no PS2: a animação de tiro (o ActAnimGroup com a animação de eventos "E_*") chega à chave do
// evento "Character_WeaponEvents_Fire_fireValue" no ActPoser::DoEventPose (do ActActor::Update): o FireEvent::HandleEvent
// chama ActActor::Fire do ator corrente (0x326010), que pede o estímulo 10 à arma (ActCharacter::PlayEvent ->
// ActWeapon::PlayEvent -> RAnimEngine::Handle::ProcessStimuli do Handle da arma, +0x68); a chave da animação da arma cria
// o EFireWeapon (o evento 10, driving/game/fire_weapon.cpp) com o RSceneObj da arma e, pelo ActWeapon::
// SetEventDynamicData, o ActWeapon no EventDynamicData +0x34 (o +0x14 do evento).
//
// Os leiautes são os do PS2 (os ponteiros como palavras p32; as tabelas virtuais e as funções da matriz como os endereços
// do PS2, que o port despacha), para a parte 20 do nf_driving_check (`--so integracao.atores`) comparar com o original.
//
// Divergências e o que não foi portado (contados em act_stats(), cada um também na função):
//  - os bancos de dados dos modelos e das texturas (ActModelDatabase, ActTextureDatabase, os URefCounter): o port carrega
//    o modelo e as texturas do personagem pelo CarModel (driving/car_model.h) e guarda nos campos do ActCharacter só o
//    p32 do objeto do port (act_resources()); o ActWeaponDatabase guarda o CARP da arma e o modelo dela do mesmo jeito;
//  - o RSceneObj da arma (+0x40 do ActWeapon) não entra na árvore do mapa do mundo (o PS2 o põe no nó da origem e o
//    ActWeapon::Render não o desenha enquanto o bit 0 do +0x138 está ligado: o personagem o desenha, ActCharacter::
//    DrawWeapon); o UpdatePosition dele (do RSceneObj::SetTransform) também não;
//  - o desenho (ActActor::Draw, ActCharacter::Draw, ActWeapon::ManualRender, os clarões RMuzzleFlash, a sombra): o
//    nf_driving desenha os atores pela família de pele (mat_char.h) com as matrizes que o ActActor calcula
//    (act_draw_list());
//  - o IK e as sobreposições globais (driving/anim/actor.h), o crossfade e a mistura manual (idem), os controladores
//    Aimed/AimedShooting/CrossFade (contados), o DropWeapon/SpawnWeapon (o Simulation::SpawnNewtonObject não foi
//    portado: contado).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "driving/anim/actor.h"
#include "driving/physics/world.h"
#include "driving/world_vis.h"

namespace drv {
class CarModel;
}

namespace drv::anim {

// ------------------------------------------------------------------------------------------------------- os eventos

// O EAGLAnim::EventTarget (0x18 bytes; o ActEventResolver aponta para ele): os nomes dos eventos em ordem (a busca
// binária) com os números na ordem em que apareceram.
struct EventTargetData {
    uint16_t unknown00;
    uint16_t count;        // +0x02
    uint32_t unknown04;
    uint32_t names;        // +0x08 (p32: {char *nome; int número}[capacidade], em ordem)
    uint32_t by_id;        // +0x0c (p32: char *[capacidade], pelo número)
    int32_t capacity;      // +0x10
    int32_t grow;          // +0x14 50
};
static_assert(sizeof(EventTargetData) == 0x18);
// 0x0010e3a8 ActEventResolver::ActEventResolver: o EventTarget novo (vazio, a capacidade 0, crescer 50).
uint32_t ActEventResolver_Construct();
// 0x00263e18 EAGLAnim::EventTarget::ResolveEventId(char *, int &): "event.<nome>": o número do nome (um novo
// no fim se não há; os nomes ficam em ordem). Falso sem o prefixo.
bool EventTarget_ResolveEventId(uint32_t target, const char* name, int32_t& id);
// 0x00264298 EAGLAnim::EventTarget::GetEventId(char *, int &): a busca binária (o número, ou -1; `pos` o lugar
// de inserção).
int32_t EventTarget_GetEventId(uint32_t target, const char* name, int32_t& pos);

// Um tratador (8 bytes: +0 zero, +4 a tabela virtual do PS2).
struct ActEventHandler {
    uint32_t unknown00;
    uint32_t vtable;       // 0x364cd0 DefaultEventHandler, 0x364cb8 FireEvent, 0x364ca0 DropWeaponEvent, 0x364c88
                           // HeightCalcEvent
};
constexpr uint32_t kVtDefaultEvent = 0x00364cd0, kVtFireEvent = 0x00364cb8, kVtDropWeaponEvent = 0x00364ca0,
                   kVtHeightCalcEvent = 0x00364c88;

// O ActEvents (0x94 bytes): a tabela dos tratadores pelo número do evento (32), os quatro tratadores e o ator corrente.
struct ActEventsData {
    uint32_t handlers[32];  // +0x00 (p32 ActEventHandler)
    uint32_t def;           // +0x80
    uint32_t drop;          // +0x84
    uint32_t height;        // +0x88
    uint32_t fire;          // +0x8c
    uint32_t current;       // +0x90 (p32 ActActorData)
};
static_assert(sizeof(ActEventsData) == 0x94);
// 0x0010e448 ActEvents::ActEvents(ActEventResolver &): todos os números no DefaultEventHandler, e os de
// "Character_WeaponEvents_Fire_fireValue" (FireEvent), "..._DropWeapon_dropWeaponValue" e "..._ThrowWeapon_
// throwWeaponValue" (DropWeaponEvent), "..._PhysicsOff_physicsOffValue" (HeightCalcEvent).
uint32_t ActEvents_Construct(uint32_t resolver_target);
// 0x0010e5b8 ActEvents::~ActEvents
void ActEvents_Destruct(uint32_t events);
// 0x0010e610 ActEvents::SetCurrentActor
void ActEvents_SetCurrentActor(uint32_t events, uint32_t actor);

// ----------------------------------------------------------------------------------------- os controladores

// O AnimationController (0x20 o Standard, 0x30 os de mistura, 0x40 o AimedShooting).
struct AnimControllerData {
    uint32_t group;      // +0x00 (p32 ActAnimGroup)
    uint32_t poser;      // +0x04 (p32 ActPoser)
    uint32_t unknown08[2];
    uint32_t matrix;     // +0x10 (p32) a matriz do ator (o ActActor +0xc +0x380)
    uint32_t vtable;     // +0x14 0x363ba8 AnimationController, 0x363b88 StandardAnimationController
    uint32_t unknown18[2];
};
static_assert(sizeof(AnimControllerData) == 0x20);
constexpr uint32_t kVtAnimController = 0x00363ba8, kVtStandardController = 0x00363b88;
// 0x00107418 StandardAnimationController::StandardAnimationController(ActAnimGroup *, ActPoser *, int banco,
// int índice, MATRIX4 &): o grupo muda de animação e o poser a toca do começo (o SetNormalAnimation com a matriz).
uint32_t StandardAnimationController_Construct(uint32_t group, uint32_t poser, int32_t bank, int32_t index,
                                               uint32_t matrix);
// A entrada +0xc da tabela virtual: o Update do controlador (0x00107490 StandardAnimationController::Update).
void AnimController_Update(uint32_t controller);
// A entrada +0x14: o destrutor (0x0010bba8 ~StandardAnimationController).
void AnimController_Destruct(uint32_t controller);
// 0x00107388 AnimationController::GetCurrentFrame
float AnimController_GetCurrentFrame(uint32_t controller);
// 0x00107398 AnimationController::GetTotalFrames
float AnimController_GetTotalFrames(uint32_t controller);
// 0x001073a8 AnimationController::GetCurrentTime (o quadro x 1/20)
float AnimController_GetCurrentTime(uint32_t controller);
// 0x001073c0 AnimationController::GetTotalTime
float AnimController_GetTotalTime(uint32_t controller);
// 0x001073d8 AnimationController::GetRemainingTime
float AnimController_GetRemainingTime(uint32_t controller);

// ------------------------------------------------------------------------------------------------------ o ator

// As matrizes do ator (0x394 bytes, um __builtin_new: o WWorldPos no começo).
struct ActActorMatrices {
    WWorldPos world_pos;        // +0x000
    float local[16];            // +0x040 a pose do ator (InitializeMatrices; a raiz do ActPoser a cada Update)
    float world[16];            // +0x080 a escala (+ a altura do chão), vezes a matriz da função
    float local_copy[16];       // +0x0c0
    float scaled[16];           // +0x100 a pose com a translação na escala do personagem (+ a altura)
    float world2[16];           // +0x140 o mesmo, vezes a matriz da função
    float weapon_bone[2][16];   // +0x180 as matrizes dos ossos das armas (ActPoser::GetWeaponBonePosOri)
    float weapon[2][16];        // +0x200 as armas na escala do personagem (vezes a matriz da função)
    float weapon_prev[2][16];   // +0x280 as do quadro anterior
    float scale[16];            // +0x300 BuildScale(a escala)
    float initial[16];          // +0x340 a matriz do CalculateMatrices(1)
    uint32_t pose_to_world;     // +0x380 (p32) o +0x340 com a função, o +0xc0 sem: a matriz dos controladores
    float floor_pos[3];         // +0x384 o ponto da última busca do chão (1000000 no começo)
    float floor_height;         // +0x390
};
static_assert(offsetof(ActActorMatrices, floor_height) == 0x390);

// O ActActor (0x64 bytes).
struct ActActorData {
    uint32_t character;      // +0x00 (p32 ActCharacterData)
    uint32_t poser;          // +0x04 (p32 ActPoser)
    uint32_t group;          // +0x08 (p32 ActAnimGroup)
    uint32_t matrices;       // +0x0c (p32 ActActorMatrices)
    uint32_t callback;       // +0x10 a função da matriz (o endereço do PS2: 0x123878 as mãos, 0x119cc0 Bond, 0x1252d8 a
                             //       passageira...): (usuário, MATRIX4 &, COORD3 &velocidade)
    int32_t has_callback;    // +0x14
    uint32_t user;           // +0x18 (p32) o primeiro argumento da função (o AICharacter)
    int32_t has_weapon[2];   // +0x1c as armas pedidas
    int32_t weapon_held[2];  // +0x24 as armas na mão (o DropWeapon solta)
    uint32_t controller;     // +0x2c (p32 AnimControllerData)
    int32_t visible;         // +0x30
    int32_t unknown34;
    int32_t floor_check;     // +0x38 1: a altura do chão a cada CalculateMatrices (o HeightCalcEvent desliga)
    float alpha_base;        // +0x3c
    float fade;              // +0x40 > 0: o alfa desce um por quadro (o fim: -1)
    int32_t not_mirrored;    // +0x44
    float scale;             // +0x48 a escala do personagem x 0,1 (o GetBaseScaleFactor)
    float inv_scale;         // +0x4c
    float height;            // +0x50 a altura do chão somada
    int32_t unknown54;       // +0x54 1
    uint32_t draw_flags;     // +0x58 3: bit 0 o corpo, bit 1 as armas
    float fov_scale;         // +0x5c 1 (as mãos: 0,5: o campo de visão do Draw)
    int32_t pov;             // +0x60 1: desenhado na câmera (as mãos)
};
static_assert(sizeof(ActActorData) == 0x64);

// As funções da matriz conhecidas (o +0x10 do ator, os endereços do PS2) e quem as atende no port.
using ActMatrixCallback = void (*)(uint32_t user, float m[16], float vel[3]);
// O port registra a função de cada endereço (driving/game/ai_character.cpp registra as dos AICharacter).
void act_register_callback(uint32_t ps2_address, ActMatrixCallback fn);

// 0x00108038 ActActor::ActActor(void *usuário, char *personagem, char *arma0, char *arma1, função, MATRIX4 &,
// bool espelhado): o ActCharacter, a escala, as matrizes, o ActPoser e o ActAnimGroup vazio.
uint32_t ActActor_Construct(uint32_t user, const char* character, const char* weapon0, const char* weapon1,
                            uint32_t callback, const float* matrix, int32_t not_mirrored);
// 0x00108358 ActActor::~ActActor
void ActActor_Destruct(uint32_t actor);
// 0x00107c68 ActActor::SetNewAnimation(int banco, int índice): o StandardAnimationController novo (o anterior
// sai); devolve o controlador.
uint32_t ActActor_SetNewAnimation(uint32_t actor, int32_t bank, int32_t index);
// 0x00108438 ActActor::GetBoneIndex
int32_t ActActor_GetBoneIndex(uint32_t actor, const char* name);
// 0x00108460 ActActor::InitializeMatrices(MATRIX4 &)
void ActActor_InitializeMatrices(uint32_t actor, const float* m);
// 0x00108610 ActActor::Fire(float, float, float, float): o estímulo 10 na arma 0.
void ActActor_Fire(uint32_t actor);
// 0x00108638 ActActor::CalculateMatrices(bool primeira)
void ActActor_CalculateMatrices(uint32_t actor, bool first);
// 0x00108d48 ActActor::GetActorLocalPosOri(MATRIX4 &)
void ActActor_GetActorLocalPosOri(uint32_t actor, float* out);
// 0x00108e98 ActActor::GetActorWorldPosition(COORD4 &)
void ActActor_GetActorWorldPosition(uint32_t actor, float* out);
// 0x00108ec8 ActActor::GetWeaponPosition(MATRIX4 &, bool, int)
void ActActor_GetWeaponPosition(uint32_t actor, float* out, int32_t weapon);
// 0x00109140 ActActor::Update
void ActActor_Update(uint32_t actor);
// 0x00109a08 ActActor::DropWeapon(float)
void ActActor_DropWeapon(uint32_t actor, float value);
// 0x0010abe0 ActActor::SetWeaponVelocity
void ActActor_SetWeaponVelocity(uint32_t actor);
// ActActor::IsAnimationDone (0x0010ac58, o ActPoser::IsAnimationDone de driving/anim/actor.h)
bool ActActor_IsAnimationDone(uint32_t actor);
// 0x0010acc0 ActActor::SetStartPosOri
void ActActor_SetStartPosOri(uint32_t actor);
// 0x0010ace0 ActActor::SetTimeScale(float)
void ActActor_SetTimeScale(uint32_t actor, float scale);
// 0x0010ad90 ActActor::GetAnimationOrigin(COORD3 &)
void ActActor_GetAnimationOrigin(uint32_t actor, float* out);
// 0x0010adf8 ActActor::SetAnimationOrigin(COORD3 &)
void ActActor_SetAnimationOrigin(uint32_t actor, float* origin);
// 0x0010ad00 ActActor::ChangeAnimationOrigin(COORD3 &)
void ActActor_ChangeAnimationOrigin(uint32_t actor, float* origin);
// 0x0010afc0 ActActor::RotateActor(float voltas)
void ActActor_RotateActor(uint32_t actor, float turns);
// 0x0010b098 ActActor::RotateActorX(float voltas)
void ActActor_RotateActorX(uint32_t actor, float turns);
// 0x0010b180 ActActor::SetSuppressAnimationTranslation(bool)
void ActActor_SetSuppressAnimationTranslation(uint32_t actor, int32_t on);
// As sobreposições globais do poser (docs/driving.md seção 26; driving/anim/actor.h):
// 0x001090c0 ActActor::CreateGlobalPoseOverrides(int, int *ossos, bool local)
void ActActor_CreateGlobalPoseOverrides(uint32_t actor, int32_t count, const int32_t* bones, int32_t local_space);
// 0x001090e0 ActActor::DeleteGlobalPoseOverrides
void ActActor_DeleteGlobalPoseOverrides(uint32_t actor);
// 0x00109100 ActActor::SetGlobalPoseOverride(int, MATRIX4 &, float)
void ActActor_SetGlobalPoseOverride(uint32_t actor, int32_t index, const float* m, float weight);
// 0x00109120 ActActor::SetGlobalPoseOverrides(int, MATRIX4 *, float *) (o número não é lido: o do vetor)
void ActActor_SetGlobalPoseOverrides(uint32_t actor, int32_t count, const float* m, const float* weights);

// ------------------------------------------------------------------------------------------------ o personagem

// O ActCharacterInfo (0xdc bytes): a cadeia "esqueleto.modelo.texturas.escala" partida.
struct ActCharacterInfoData {
    int32_t skeleton;        // +0x00
    int32_t type;            // +0x04 h 0, p 1, c 2, a 3 (outra letra: fica o que estava)
    char text[0x40];         // +0x08 a cópia da cadeia (o strtok a parte)
    uint32_t tok_skeleton;   // +0x48 (p32 dentro de text)
    uint32_t tok_model;      // +0x4c
    uint32_t tok_texture_a;  // +0x50 a textura dos tipos 1 e 3
    uint32_t tok_texture_b;  // +0x54 a primeira dos tipos 0 e 2
    uint32_t tok_texture_c;  // +0x58 a segunda
    uint32_t tok_scale;      // +0x5c
    float scale;             // +0x60 o número / 100
    float inv_scale;         // +0x64
    char model_path[0x64];   // +0x68 "data\actors\models\<modelo>y.xxx"
    uint32_t path_a;         // +0xcc (p32, 100 bytes) "data\actors\textures\<a>.ssh"
    uint32_t path_b;         // +0xd0
    uint32_t path_c;         // +0xd4
    uint32_t model_suffix;   // +0xd8 (p32) o 'y' de model_path
};
static_assert(sizeof(ActCharacterInfoData) == 0xdc);
// 0x0010de98 ActCharacterInfo::ActCharacterInfo(char *)
void ActCharacterInfo_Construct(ActCharacterInfoData& info, const char* text);
// 0x0010e2a0 ActCharacterInfo::~ActCharacterInfo (os caminhos das texturas)
void ActCharacterInfo_Destruct(ActCharacterInfoData& info);
// 0x0010e318 ActCharacterInfo::GetModelFileName(int lod): "...<modelo><lod+1>.dat"
const char* ActCharacterInfo_GetModelFileName(ActCharacterInfoData& info, int lod);

// O ActRenderOptions (0x60 bytes): +0 o alfa (0-255), +4/+8/+0xc a escuridão da sombra, +0x10 1, +0x30 o triângulo da
// sombra (ActCharacter::GetShadowTriangle).
struct ActRenderOptionsData {
    int32_t alpha;
    float shadow[3];
    float one;
    uint32_t unknown14[7];
    float shadow_triangle[9];  // +0x30
    uint32_t unknown54[3];
};
static_assert(sizeof(ActRenderOptionsData) == 0x60);
// 0x0010de10 ActRenderOptions::SetAlpha(float)
void ActRenderOptions_SetAlpha(ActRenderOptionsData& o, float alpha);
// 0x0010de58 ActRenderOptions::SetShadowDarkness(float)
void ActRenderOptions_SetShadowDarkness(ActRenderOptionsData& o, float d);

// O ActCharacter (0x84 bytes).
struct ActCharacterData {
    uint32_t models;        // +0x00 (p32 do banco de dados do port)
    uint32_t textures;      // +0x04
    uint32_t weapons;       // +0x08 (p32 ActWeaponDatabase do port)
    uint32_t model;         // +0x0c (p32 CarModel do port: o UseModel)
    uint32_t texture[3];    // +0x10 as três texturas (o port: 0; o CarModel as tem)
    uint32_t base_texture;  // +0x1c "hhhh" (o port: 0)
    uint32_t info;          // +0x20 (p32 ActCharacterInfoData)
    uint32_t weapon[2];     // +0x24 (p32 ActWeaponData)
    char weapon_name[2][0x1e];  // +0x2c e +0x4a ("NO WEAPON!" sem arma)
    uint32_t render_options;    // +0x68 (p32 ActRenderOptionsData)
    int32_t no_callback;        // +0x6c 1: o ator não tem a função da matriz (o triângulo da sombra)
    uint32_t unknown70[5];
};
static_assert(sizeof(ActCharacterData) == 0x84);
// 0x0010cc60 ActCharacter::ActCharacter
uint32_t ActCharacter_Construct(int32_t no_callback, const char* character, const char* weapon0, const char* weapon1);
// 0x0010d188 ActCharacter::~ActCharacter
void ActCharacter_Destruct(uint32_t ch);
// 0x0010cb88 ActCharacter::LoadCharacter(char *, char *, char *): o modelo, as texturas e as armas nos bancos
// de dados (o port: act_resources()).
void ActCharacter_LoadCharacter(const char* character, const char* weapon0, const char* weapon1);
// 0x0010d258 ActCharacter::PlayEvent(int arma, unsigned int estímulo)
void ActCharacter_PlayEvent(uint32_t ch, int32_t weapon, uint32_t stimulus);
// 0x0010d298 ActCharacter::GetScaleFactors(float &, float &)
void ActCharacter_GetScaleFactors(uint32_t ch, float& scale, float& inv);
// 0x0010d1f0 ActCharacter::SetAlpha(float)
void ActCharacter_SetAlpha(uint32_t ch, float alpha);
// 0x0010dc00 ActCharacter::SetWeaponBone(int, MATRIX4 &)
void ActCharacter_SetWeaponBone(uint32_t ch, int32_t weapon, const float* m);
// 0x0010d048 ActCharacter::SetWeaponVelocity(COORD3 &)
void ActCharacter_SetWeaponVelocity(uint32_t ch, const float* vel);
// 0x0010cfe8 ActCharacter::SetWeaponsTransformToWorldSpace(MATRIX4 &, MATRIX4 &)
void ActCharacter_SetWeaponsTransformToWorldSpace(uint32_t ch, const float* bone, const float* world);
// 0x0010cd78 ActCharacter::ShowWeapon(int)
void ActCharacter_ShowWeapon(uint32_t ch, int32_t weapon);
// 0x0010cdc8 ActCharacter::HideWeapon(int)
void ActCharacter_HideWeapon(uint32_t ch, int32_t weapon);

// ------------------------------------------------------------------------------------------------------ a arma

// O ActWeaponAux (4 bytes): o CARP da arma ("data\actors\weapons\<nome>.crp"). O port guarda o p32 do CarModel do port
// (act_resources()).
struct ActWeaponData {
    uint8_t instance[0x40];  // +0x000 a instância falsa do RSceneObj (a cópia do gRigidBodyDraw 0x386b40)
    RSceneObj scene;         // +0x040 (a tabela virtual +0x7c: 0x367458); +0x68 o RAnimEngine::Handle
    uint32_t aux;            // +0x080 (p32) o ActWeaponAux (o port: o ActWeaponResource)
    struct Flash {           // +0x084 os oito clarões (0x14 bytes): o lugar, o passo de início (o construtor zera), a
                             //        força (StartMuzzleFlash; o ManualRender os desenha)
        float pos[3];
        int32_t start;
        float strength;
    } flash[8];
    uint32_t unknown124[5];
    uint32_t flags;          // +0x138 bit 0: o personagem o desenha (Show); bit 1: do jogador (SetOwner)
    uint32_t bone_matrix;    // +0x13c (p32 MATRIX4, SetTransformToWorldSpace)
    uint32_t world_matrix;   // +0x140 (p32 MATRIX4)
    uint32_t owner;          // +0x144 (p32 PhysicsObject: a imagem do PS2 do dono) SetOwner
    uint32_t unknown148[2];
    float velocity[3];       // +0x150 (SetWeaponVelocity: a velocidade / 60)
    uint32_t accuracy;       // +0x15c (p32) o passageiro do CARP::AIElement (+0xc o dano, +0x10 a precisão; o WakeUp)
    uint32_t unknown160;     // +0x160 e +0x164: os rastros das balas (o EFireWeapon)
    uint32_t unknown164;
    uint32_t unknown168[2];
};
static_assert(sizeof(ActWeaponData) == 0x170);
constexpr uint32_t kVtActWeaponScene = 0x00367458;
// 0x001162a8 ActWeapon::ActWeapon(ActWeaponAux *, unsigned int bandeiras)
uint32_t ActWeapon_Construct(uint32_t aux, uint32_t flags);
// 0x00116410 ActWeapon::~ActWeapon
void ActWeapon_Destruct(uint32_t weapon);
// 0x00116498 ActWeapon::SetTransformToWorldSpace(MATRIX4 &, MATRIX4 &)
void ActWeapon_SetTransformToWorldSpace(uint32_t weapon, const float* bone, const float* world);
// 0x00116608 ActWeapon::SetOwner(PhysicsObject *): `owner` é a imagem do PS2 (p32) e `owned_by_player` o
// PhysicsObject::IsOwnedBy(Simulation::GetPlayerObject) dela.
void ActWeapon_SetOwner(uint32_t weapon, uint32_t owner, bool owned_by_player);
// 0x00116678 ActWeapon::SetEventDynamicData: o do RSceneObj e o ActWeapon no EventDynamicData +0x34.
void ActWeapon_SetEventDynamicData(uint32_t weapon);
// 0x001166a8 ActWeapon::PlayEvent(unsigned int): o estímulo em todos os sistemas do Handle da arma.
void ActWeapon_PlayEvent(uint32_t weapon, uint32_t stimulus);
// 0x00116cb0 ActWeapon::TransformToWorldSpace(MATRIX4 &)
void ActWeapon_TransformToWorldSpace(uint32_t weapon, float* m);
// 0x00116df0 ActWeapon::TransformMatrixToWorldSpace(MATRIX4 &)
void ActWeapon_TransformMatrixToWorldSpace(uint32_t weapon, float* m);
// O RSceneObj de uma arma: o ActWeapon dele (p32), ou 0 se o objeto de cena não é de uma arma (a tabela virtual +0x3c).
uint32_t ActWeapon_FromScene(uint32_t scene);
// 0x001c9368 RSceneObj::GetInstancePosition(unsigned int, MATRIX4 &, bool) para o RSceneObj de uma arma (com
// a TransformToWorldSpace dela, a entrada +0x4c): o lugar e a orientação da cópia da instância no mundo. O EFireWeapon do
// nf_driving o chama pelo effects_env().instance_position. Falso: não é de uma arma.
bool ActWeapon_GetInstancePosition(uint32_t scene, int32_t index, float m[4][4]);
// 0x001cae00 RSceneObj::LocateFX(CARP::Effect &, MATRIX4 &, bool) (com o TransformMatrixByInstancePosition 0x001c95f0 e
// o AdjustEffectPosition 0x001ca8e0) para o RSceneObj de uma arma (docs/driving.md seção 26): o lugar do efeito (o cano
// dos passageiros, de onde o EFireWeapon atira). Falso: não é de uma arma. O AdjustEffectPosition (a bandeira 0x100: a
// altura do mundo) é contado (as armas não a têm).
bool ActWeapon_LocateFX(uint32_t scene, uint32_t effect, float m[4][4]);
// 0x001c9328 RSceneObj::GetInstanceSystemID (o de uma arma)
uint32_t ActWeapon_GetInstanceSystemID(uint32_t scene, uint32_t index);

// ---------------------------------------------------------------------------------------------- os bancos de dados

// O BankInfo do ActAnimationDatabase (0x3c bytes) e o PrivateData (0x448).
struct ActBankInfo {
    uint32_t bank;          // +0x00 (p32) o AnimationBank
    uint32_t def_bank;      // +0x04 (p32) o FnDefaultAnimBank (8 bytes: a tabela virtual 0x364028 e o banco)
    uint32_t loader;        // +0x08
    uint32_t file;          // +0x0c
    int32_t num_anims;      // +0x10
    char name[0x28];        // +0x14
};
static_assert(sizeof(ActBankInfo) == 0x3c);
struct ActAnimationDatabaseData {
    ActBankInfo banks[18];  // +0x000 pela ordem da tabela 0x3264d0 ("driving", "essm", ..., "worker")
    int32_t loaded;         // +0x438
    int32_t total;          // +0x43c
    uint32_t lookup;        // +0x440 (p32) o ALookup.bin: o começo de cada banco em palavras, e pares (eventos, pose)
    uint32_t lookup_size;   // +0x444
};
static_assert(sizeof(ActAnimationDatabaseData) == 0x448);
// Os 18 nomes da tabela 0x3264d0.
const char* act_bank_name(int index);
// 0x0010c1d8 PrivateData::GetAnimation(int banco, int índice, AnimMemoryMap **pose, **eventos)
void ActAnimationDatabase_GetAnimation(int32_t bank, int32_t index, uint32_t& pose, uint32_t& events);

// O ActManager (0x80 bytes, o 0x3260d8).
struct ActManagerData {
    uint32_t textures;        // +0x00 (port)
    uint32_t models;          // +0x04 (port)
    uint32_t animations;      // +0x08 (p32 ActAnimationDatabaseData)
    uint32_t skeletons;       // +0x0c (p32: as nove ActSkeletonData*)
    uint32_t weapons;         // +0x10 (port)
    uint32_t events;          // +0x14 (p32 ActEventsData)
    float frame_time;         // +0x18 o GetFoundationVideoModeRate (segundos por quadro)
    uint32_t unknown1c;
    ActRenderOptionsData render_options;  // +0x20
};
static_assert(sizeof(ActManagerData) == 0x80);

// Os globais do PS2 que a camada usa.
struct ActGlobals {
    uint32_t manager = 0;        // 0x3260d8 (p32 ActManagerData)
    uint32_t resolver = 0;       // 0x3260dc (p32 EventTargetData; o ActEventResolver aponta para ele)
    uint32_t updating = 0;       // 0x3260f0 o ator do ActActor::Update
    uint32_t current = 0;        // 0x326010 o ator corrente dos eventos (ActEvents::SetCurrentActor)
    uint32_t database = 0;       // 0x326000 o ActActorDatabase (p32: +0 o nó sentinela da lista)
};
ActGlobals& act_globals();
// Um nó da lista dos atores (o std::list do GCC 2.95, 12 bytes): o "handle" de um ator é o nó (+8 o ActActor).
struct ActListNode {
    uint32_t next;   // (p32 ActListNode)
    uint32_t prev;
    uint32_t actor;  // (p32 ActActorData)
};
static_assert(sizeof(ActListNode) == 12);
// O ActActor de um handle (o +8 do nó), 0 sem.
inline uint32_t act_actor_of(uint32_t handle) {
    return handle ? nf::p32_ptr<const ActListNode>(handle)->actor : 0u;
}
// Os atores da lista, na ordem do UpdateAll (o mais novo primeiro).
std::vector<uint32_t> act_actor_list();

// Os recursos do port (os modelos, os esqueletos, os bancos, as armas): quem carrega (o nf_driving) preenche; os
// testes e a conferência montam à mão.
struct ActWeaponResource {
    std::string name;
    uint32_t instances = 0;  // (p32) as instâncias 'in#0000' do primeiro 'Arti' do CARP resolvido
    uint32_t count = 0;
    uint32_t damage = 0;
    std::shared_ptr<CarModel> model;  // o desenho (as peças das instâncias)
};
struct ActResources {
    std::shared_ptr<ActSkeleton> skeletons[9];
    std::vector<std::shared_ptr<AnimBank>> banks;  // 18: pela tabela 0x3264d0 (nulo: não carregado)
    std::vector<uint8_t> lookup;                   // o ALookup.bin
    std::vector<std::pair<std::string, std::shared_ptr<CarModel>>> models;  // pelo nome do modelo (o LOD 1)
    std::vector<std::shared_ptr<ActWeaponResource>> weapons;
    // Do port: carrega um modelo ou uma arma que o LoadCharacter pede (nulo: só registra o nome).
    std::shared_ptr<CarModel> (*load_model)(const ActCharacterInfoData& info) = nullptr;
    std::shared_ptr<ActWeaponResource> (*load_weapon)(const char* name) = nullptr;
};
ActResources& act_resources();

// 0x00110900 ActManager::StartUp(float tempo de quadro): o EventTarget, o ActManager e o ActManagerInit (os
// bancos de dados: os esqueletos e as animações vêm de act_resources(); o ActEvents com o EventTarget).
void ActManager_StartUp(float frame_time);
// 0x001109f8 ActManager::ShutDown
void ActManager_ShutDown();
// 0x00110f10 ActManager::SymbolResolver(char *, bool &): "event.<nome>" pelo EventTarget (o resolvedor das
// cargas dos bancos de animação do port).
uint32_t ActManager_SymbolResolver(const char* name, bool& found);
// Port: monta o ActAnimationDatabase de act_resources() (os bancos já carregados com o ActManager_SymbolResolver, o
// ALookup.bin): o ActAnimationDatabase::ActAnimationDatabase (0x0010c2b0) sem a leitura dos arquivos.
uint32_t ActAnimationDatabase_Build();

// 0x0010b540 ActActorDatabase::ActActorDatabase (a lista vazia: o nó sentinela)
void ActActorDatabase_Construct();
// 0x0010b3b8 ActActorDatabase::GetNewActorHandle(void *, char *, char *, char *, função, MATRIX4 &, bool):
// o ator novo num nó no começo da lista; devolve o handle (o nó).
uint32_t ActActorDatabase_GetNewActorHandle(uint32_t user, const char* character, const char* weapon0,
                                            const char* weapon1, uint32_t callback, const float* matrix,
                                            int32_t mirrored);
// 0x0010b4e0 ActActorDatabase::KillActorByHandle (o ator e o nó)
void ActActorDatabase_KillActorByHandle(uint32_t handle);
// 0x0010b800 ActActorDatabase::UpdateAll
void ActActorDatabase_UpdateAll();
// 0x0010b260 ActActorDatabase::ShutDown (o ~ActActorDatabase: todos os atores)
void ActActorDatabase_ShutDown();

// O que o nf_driving desenha (o ActActor::Draw e o DrawWeapons do port): cada ator visível com as matrizes que o
// ActActor calculou e o EAGLAnimationBuffer do ActPoser::Skin.
struct ActDrawItem {
    uint32_t actor = 0;
    const CarModel* model = nullptr;
    float world[16];          // o +0x80 do ator (a escala e a matriz da função)
    std::vector<float> skin;  // 16 floats por osso
    int pov = 0;              // o +0x60 (as mãos: na câmera)
    float fov_scale = 1.0f;   // o +0x5c
    int32_t alpha = 255;
    // As armas mostradas: o modelo e a matriz de cada instância (o RSceneObj::GetInstancePosition delas).
    struct Weapon {
        const CarModel* model = nullptr;
        std::vector<std::vector<float>> instances;  // 16 floats por instância
    };
    std::vector<Weapon> weapons;
};
void act_draw_list(std::vector<ActDrawItem>& out);

// O que a camada conta (o port).
struct ActStats {
    uint64_t actors = 0, updates = 0, fires = 0, plays = 0, drops = 0, spawn_weapon = 0;
    uint64_t events_fired = 0;     // os tratadores chamados (FireEvent, DropWeaponEvent, HeightCalcEvent)
    uint64_t default_events = 0;   // DefaultEventHandler
    uint64_t controllers = 0, other_controllers = 0;
    uint64_t scene_tree = 0;       // o RSceneObj da arma fora da árvore do mapa (o construtor e o UpdatePosition)
    uint64_t draws = 0;            // o Draw do PS2 (desenhado pelo nf_driving)
    uint64_t missing_resources = 0, guards = 0;
};
ActStats& act_stats();
// Port: zera tudo (o começo de uma missão; os testes).
void act_reset();

// A anotação das chamadas a sistemas fora da camada (a parte 20 compara com o original), como effects_trace().
struct ActCall {
    uint32_t fn;
    uint32_t a[4];
};
std::vector<ActCall>& act_trace();
bool& act_trace_enabled();
void act_note(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0);

}  // namespace drv::anim
