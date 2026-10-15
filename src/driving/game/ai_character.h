#pragma once
// Os personagens da IA das missões de carro (o AICharacter do DRIVING.ELF, ActTextureDatabase.obj e
// AICharacterHands.obj, e o Human de Noise.obj; docs/driving.md seção 24): a base AICharacter (a máquina de estados que o
// Human::Simulate roda a cada passo da Simulation, a zona do dano, o movimento), o Human (o PhysicsObject de tipo 4 com
// um corpo simples de tipo 2, sem colisão até o Activate, que leva o dano dos tiros ao personagem) e as mãos de Bond
// (AICharacterHands: o braço com a arma na câmera da direção automática; na seção do helicóptero de Paris, o fuzil de
// precisão do jogador).
//
// Como as mãos atiram no PS2: o SWeaponManager::FireWeapon (driving/game/weapons.h) com a direção automática pergunta
// AICharacterHands::AbleToFire e chama AICharacterHands::Fire, que põe o estado 11 e a animação de tiro do braço
// (ActActor::SetNewAnimation, o banco e a animação das tabelas 0x327530); o evento "Fire" da animação (o ActPoser::
// DoEventPose do ActActor::Update, driving/anim/act_actor.h) chama ActActor::Fire, e a arma do braço (o ActWeapon) toca
// o estímulo 10: a chave da animação dela cria o EFireWeapon.
//
// As classes e as tabelas virtuais: os objetos guardam o endereço da tabela virtual do PS2 (+0x140), e o port despacha
// cada entrada pelo registro ai_vtable() (os personagens que outros módulos portarem registram as deles). As funções da
// matriz do ActActor (o +0x10: 0x123878 as mãos) entram em act_register_callback.
//
// Portado do MIPS do PS2. Os leiautes são os do PS2 (AICharacterData 0x150, AICharacterHandsData 0x170, o Human 0x90)
// para a parte 20 do nf_driving_check (`--so integracao.atores`).
//
// Divergências (contadas em ai_character_stats(), cada uma também na função):
//  - o som do personagem (AICharacter::GetACharacter, o ACharacter de BondAudioPS2: +0x94) não foi portado (o Execute
//    lê o +0x94, sempre nulo no port);
//  - a câmera: as mãos chamam o RPlayerCamera::UpdateCamera, o AimZoom e o AimRelease pelos ganchos de
//    ai_character_env() (o nf_driving liga à câmera do port; sem eles, contados), e o fgZoomButton é o
//    CameraStatics::zoom_button da câmera do port (o mesmo global 0x3276e0 no PS2);
//  - o AMenuSound do ferrolho do fuzil (DoFiring) vai pelo gancho menu_sound (sem ele, contado);
//  - a assinatura do Human::ApplyDamage (Simulation::FindPhysicsObjectSignature e PhysicsObject::IsOwnedBy do carro do
//    jogador) vem do `source` (0 nenhum objeto, 1 um objeto do jogador, 2 um objeto de outro); a entrada virtual do port
//    (apply_damage) a tira do damage_source_is_player() (driving/game/bond_car.h): 1 ligado, 2 desligado;
//  - os pedestres (AICharacterPedestrian), o Bond no carro (AICharacterBond: dois personagens das tabelas 0x3268a8 e
//    o ator com IK e sobreposições, num carro que não é o "helicopter" com os dois FX do RSceneObj do jogador) e a
//    passageira (AICharacterPassenger) não foram portados: o AICharacter::Init só os conta;
//  - o port guarda os Human que criou (o PS2 os apaga no fim da Simulation) e os apaga em ai_character_reset().

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "driving/game/explosion.h"
#include "platform/ptr32.h"

namespace drv {

// ------------------------------------------------------------------------------------------------------- AICharacter

struct AICharacterData {
    float matrix[16];        // +0x000 a orientação e o lugar do personagem (a linha 3: +0x30)
    float offset[3];         // +0x040 (-0,08, 0, 0,12)
    int32_t state;           // +0x04c o estado (0..0x16: a entrada +0x44 + 8 x estado da tabela virtual)
    int32_t sub_state;       // +0x050 o próximo estado (8 armando, 0xd desarmando)
    int32_t anim;            // +0x054 a animação corrente (o índice no banco +0x78)
    int32_t index;           // +0x058 a arma das mãos (1..7), o personagem dos outros
    int32_t unknown5c;
    int32_t unknown60;
    float range_sq;          // +0x064 5625 (75 m ao quadrado: AICharacterEnemy::TargetIsInRange)
    uint32_t hit_points;     // +0x068 (p32 float) a vida (o Human::SetAICharacter a dá ao Human)
    float damage_taken;      // +0x06c
    float initial_hp;        // +0x070
    uint32_t world_pos;      // +0x074 (p32 WWorldPos) o chão do Execute
    int32_t bank;            // +0x078 o banco das animações
    int32_t clip;            // +0x07c 15 (NeedsToReload)
    int32_t clip_size;       // +0x080 15
    int32_t target_right;    // +0x084 TargetIsAcquired: o alvo à direita
    int32_t unknown88;
    int32_t unknown8c;       // +0x08c 60
    int32_t unknown90;
    uint32_t sound;          // +0x094 o ACharacter (o som: não portado, sempre 0)
    uint32_t actor;          // +0x098 o handle do ator (o nó da lista do ActActorDatabase, +8 o ActActor)
    uint32_t controller;     // +0x09c o controlador da última animação (SetNewAnimation)
    int32_t unknowna0;
    uint32_t zone_map;       // +0x0a4 (p32) as zonas do corpo (+0 largura, +4 altura, +8 o mapa, +0xc o raio, +0x10 o y,
                             //        +0x14 a escala do y)
    int32_t zone_last_hit;   // +0x0a8
    int32_t unknownac;       // +0x0ac 1
    int32_t counts_hits;     // +0x0b0 1 (Human::ApplyDamage: SMissionManager::IncShotsHit)
    uint32_t unknownb4[3];
    float avoid_base[3];     // +0x0c0
    int32_t unknowncc;
    float body[16];          // +0x0d0 a matriz do corpo solto (o Execute: a linha 3, +0x100, cai)
    float velocity[3];       // +0x110
    int32_t falling;         // +0x11c
    int32_t floor_check;     // +0x120
    float avoid_distance;    // +0x124
    uint32_t unknown128[2];
    float avoid_point[3];    // +0x130
    int32_t use_avoid;       // +0x13c
    uint32_t vtable;         // +0x140 (o endereço da tabela virtual do PS2)
    uint32_t unknown144[3];
};
static_assert(sizeof(AICharacterData) == 0x150);
static_assert(offsetof(AICharacterData, vtable) == 0x140 && offsetof(AICharacterData, body) == 0xd0 &&
              offsetof(AICharacterData, actor) == 0x98);

// As mãos (0x170 bytes).
struct AICharacterHandsData {
    AICharacterData base;
    int32_t reload_pending;  // +0x150 (Reload fora do estado 2: o DoIdling recarrega)
    int32_t hide_on_change;  // +0x154 1 (a troca de arma esconde o ator antigo)
    int32_t pending_weapon;  // +0x158 a troca pedida enquanto o braço anima
    float shake;             // +0x15c o tranco do tiro (UpdateRotPos)
    float aim_fraction;      // +0x160 o quanto a mira entrou (DoAiming; o HUD o lê)
    int32_t visible;         // +0x164 ShowHands
    uint32_t unknown168[2];
};
static_assert(sizeof(AICharacterHandsData) == 0x170);

constexpr uint32_t kVtAICharacter = 0x00367930, kVtAICharacterHands = 0x003696e0, kVtAICharacterBond = 0x00367e38,
                   kVtAICharacterPassenger = 0x00369a28;

// A tabela virtual de uma classe (as entradas que o código portado chama; nulo: a do AICharacter).
struct AIVtable {
    uint32_t address = 0;
    void (*destroy)(uint32_t self) = nullptr;                                   // +0x0c o destrutor com o delete (3)
    int32_t (*is_alive)(uint32_t self) = nullptr;                               // +0x14
    void (*set_actor)(uint32_t self, uint32_t handle) = nullptr;                // +0x1c
    int32_t (*slot24)(uint32_t self) = nullptr;                                 // +0x24
    void (*notify_zone_damage)(uint32_t self, float damage, const float* pos) = nullptr;  // +0x2c
    float (*zone_hit_point_scale)(uint32_t self, float damage, int32_t zone) = nullptr;   // +0x34
    void (*play_zone_injury_anim)(uint32_t self, float damage, int32_t zone, int32_t kind) = nullptr;  // +0x3c
    void (*states[23])(uint32_t self) = {};  // +0x44 + 8 x estado (0..0x16)
    void (*handle_interrupts)(uint32_t self) = nullptr;                         // +0xfc
    int32_t (*slot104)(uint32_t self) = nullptr;                                // +0x104
    void (*update_rot_pos)(uint32_t self) = nullptr;                            // +0x10c
    // As do AICharacterEnemy (os passageiros, driving/game/ai_passengers.h; nulas nas outras classes).
    int32_t (*target_in_range)(uint32_t self) = nullptr;                        // +0x114
    int32_t (*needs_to_reload)(uint32_t self) = nullptr;                        // +0x11c
    int32_t (*target_acquired)(uint32_t self) = nullptr;                        // +0x124
};
// O registro das tabelas (a das mãos e a base vêm daqui; outros módulos registram as suas).
void ai_register_vtable(const AIVtable& vt);
const AIVtable* ai_vtable(uint32_t address);

// 0x001186b0 AICharacter::Init: os pedestres, a passageira, Bond e as mãos (o GameLoop_StartUp e o ERestart).
void AICharacter_Init();
// 0x001186e8 AICharacter::Shutdown
void AICharacter_Shutdown();
// 0x00118720 AICharacter::AICharacter
void AICharacter_Construct(AICharacterData& c);
// 0x00118900 AICharacter::~AICharacter (o som e o ator)
void AICharacter_Destruct(AICharacterData& c);
// 0x00118af0 AICharacter::Execute: o UpdateRotPos e o HandleInterrupts, a queda do corpo solto, o estado.
void AICharacter_Execute(uint32_t self);
// 0x00118eb0 AICharacter::NotifyZoneDamage(COORD3 &): a zona do mapa do corpo no ponto.
void AICharacter_NotifyZoneDamage(uint32_t self, const float* pos);
// 0x00119140 AICharacter::MoveToPoint(COORD3 &, bool com_y)
void AICharacter_MoveToPoint(uint32_t self, const float* point, bool with_y);
// 0x00119858 AICharacter::UpdateRotPos: a matriz do personagem é a pose do ator.
void AICharacter_UpdateRotPos(uint32_t self);
// 0x00118988 AICharacter::GetACharacter (o som: não portado; contado, devolve 0)
uint32_t AICharacter_GetACharacter(uint32_t self);
// 0x001191e8 AICharacterEnemy::TargetIsInRange: o carro do jogador a menos de 75 m.
bool AICharacterEnemy_TargetIsInRange(uint32_t self);
// 0x00119270 AICharacterEnemy::NeedsToReload
bool AICharacterEnemy_NeedsToReload(uint32_t self);
// 0x00119290 AICharacterEnemy::TargetIsAcquired: o rumo da arma (ou do personagem) para o carro do jogador
// dentro de 2 graus (e o lado em +0x84).
bool AICharacterEnemy_TargetIsAcquired(uint32_t self);
// O mapa das zonas do corpo dos capangas (0x326860: largura 4, altura 15, o mapa 0x367750, o raio 0,35, o y 0,15, a
// escala 2): o +0xa4 dos passageiros (driving/game/ai_passengers.h). Devolve o endereço (p32) da cópia do port.
uint32_t ai_character_zone_map();
// 0x00210e08 Util_VecToPoleDepthAndLong(COORD3 &, float &profundidade, float &longitude)
void Util_VecToPoleDepthAndLong(const float* v, float& depth, float& lon);
// 0x00210ff0 Util_CharMapLookup(float, float, char *, int, int)
int32_t Util_CharMapLookup(float u, float v, uint32_t map, int32_t w, int32_t h);

// ------------------------------------------------------------------------------------------------------- o Human

struct HumanData {
    int32_t kind;            // +0x6c 1: não leva dano
    uint32_t character;      // +0x70 (p32 AICharacterData)
    int32_t dead;            // +0x74
    uint32_t unknown78[2];
    float hit_pos[4];        // +0x80 o ponto do último tiro
};
static_assert(sizeof(HumanData) == 0x24);

class Human : public SimpleObject {
public:
    HumanData h{};
    ~Human() override;
    void simulate() override;
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override;
    float hit_points() override;  // PhysicsObject::GetHitPoints (0x00195a30): o float do +0x54, ou 1
};
// A imagem do PS2 de um Human (o começo do SimpleObject: o WWorldPos).
inline uint32_t Human_Image(Human& h) { return nf::p32_u(&h.world_pos_); }

// 0x0017f3b0 Human::Human(int tipo, COORD3 &dir, COORD3 &pos, ObjSig)
void Human_Construct(Human& h, int32_t kind, const float dir[3], const float pos[3], uint32_t sig);
// 0x0017f540 Human::Activate: o corpo passa a colidir.
void Human_Activate(Human& h);
// 0x0017f570 Human::Deactivate
void Human_Deactivate(Human& h);
// 0x0017f5a0 Human::SetAICharacter(AICharacter *): a vida, o dono das armas do ator, o corpo no lugar.
void Human_SetAICharacter(Human& h, uint32_t character);
// 0x0017f6c0 Human::ApplyDamage(float, COORD3 &de, COORD3 &para, int tipo, ObjSig): `source` 0 sem objeto, 1
// do jogador, 2 de outro (ver o cabeçalho). Devolve 0x80 com dano, 0 sem.
int32_t Human_ApplyDamage(Human& h, float damage, const float from[3], const float to[3], int32_t kind,
                          int32_t source);
// 0x0017f980 Human::Simulate
void Human_Simulate(Human& h);
// 0x0017fbd0 Human::GetHuman (o tipo +0x6c)
int32_t Human_GetHuman(const Human& h);
// 0x001ffb90 Simulation::SpawnHumanObject(int tipo, COORD3 &dir, COORD3 &pos, ObjSig): nulo sem vaga.
Human* SpawnHumanObject(Simulation& sim, int32_t kind, const float dir[3], const float pos[3], uint32_t sig);
// 0x002019c0 Simulation::DeleteHumanObject: sai do vetor dos Human (+0xdf0) e vai para os mortos (DeletePhysicsObject).
void DeleteHumanObject(Simulation& sim, Human* human);
// O vetor dos Human da Simulation (+0xdf0, na ordem do SpawnHumanObject; os passageiros o percorrem).
std::vector<Human*>& simulation_humans();
// 0x00195c30 PhysicsObject::SetOwnerObject(PhysicsObject *): a assinatura do dono em +0x58 (0 sem).
void PhysicsObject_SetOwnerObject(SimpleObject& o, PhysicsObject* owner);

// ------------------------------------------------------------------------------------------------------- as mãos

// Os globais das mãos (0x327670 o AICharacterHands, 0x3276c0 os oito handles dos atores, 0x422420 os lados do tiro,
// 0x3276e4 fgIsZoomed, 0x327680 fgInitRotPos, 0x3276f0/0x327700 o atraso do braço, 0x3276e8/0x3276ec a mira).
struct HandsGlobals {
    uint32_t hands = 0;            // 0x327670 (p32 AICharacterHandsData)
    uint32_t human = 0;            // fgHandObj (p32 Human: o objeto do port)
    uint32_t actor[8] = {};        // 0x3276c0 os handles
    int32_t toggle[8] = {};        // 0x422420
    int32_t is_zoomed = 0;         // fgIsZoomed
    // 0x327680 fgInitRotPos: a identidade (0x3ad6a8) que o construtor global 0x00124e00 copia.
    float init_rot_pos[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float lag_pos[4] = {};         // 0x3276f0
    float lag_vel[4] = {};         // 0x327700
    float aim_yaw = 0.4f, aim_pitch = 0.08f;  // 0x3276e8, 0x3276ec (o GetAimMatrix4)
};
HandsGlobals& hands_globals();
// O fgZoomButton (0x3276e0): o da câmera do port (CameraStatics::zoom_button) quando o gancho o dá.
int32_t& hands_zoom_button();

// 0x00123950 AICharacterHands::Init: os personagens das armas carregadas (0x18..0x1e), o AICharacterHands, o
// Human e um ator por arma (escondidos, na câmera).
void AICharacterHands_Init();
// 0x00123d90 AICharacterHands::Shutdown
void AICharacterHands_Shutdown();
// 0x00123e28 AICharacterHands::AICharacterHands(int arma)
void AICharacterHands_Construct(AICharacterHandsData& h, int32_t weapon);
// 0x00123ea0 AICharacterHands::~AICharacterHands
void AICharacterHands_Destruct(AICharacterHandsData& h);
// 0x00123ef8 AICharacterHands::ShowHands
void AICharacterHands_ShowHands(uint32_t self);
// 0x00123f10 AICharacterHands::HideHands
void AICharacterHands_HideHands(uint32_t self);
// 0x00123f90 AICharacterHands::ChangeWeapon(int)
void AICharacterHands_ChangeWeapon(uint32_t self, int32_t weapon);
// 0x001240c8 AICharacterHands::Fire
void AICharacterHands_Fire(uint32_t self);
// 0x00124170 AICharacterHands::Reload
void AICharacterHands_Reload(uint32_t self);
// 0x001241e0 AICharacterHands::AbleToFire
bool AICharacterHands_AbleToFire(uint32_t self);
// 0x00124230 AICharacterHands::GetWeapon
inline int32_t AICharacterHands_GetWeapon(uint32_t self) {
    return nf::p32_ptr<const AICharacterData>(self)->index;
}
// 0x001237d0 AICharacterHands::GetAimFraction (o +0x160 das mãos, 0 sem elas)
float AICharacterHands_GetAimFraction();
// 0x00124848 AICharacterHands::UpdateRotPos: a câmera (UpdateCamera), a matriz da mira (GetAimMatrix4) no
// lugar do olho, o atraso do braço com a velocidade do carro e o tranco do tiro.
void AICharacterHands_UpdateRotPos(uint32_t self);
// 0x00123878 AICharacterHands::GetRotPos(int, MATRIX4 &, COORD3 &): a função da matriz dos atores das mãos
// (a matriz das mãos e a velocidade do carro do jogador).
void AICharacterHands_GetRotPos(uint32_t self, float m[16], float vel[3]);

// O que as mãos leem de fora (o nf_driving liga; a conferência dá o mesmo aos dois lados; nulos: contados).
struct AICharacterEnv {
    // O RPlayerCamera do jogador (0x333ed8 [1]): o leiaute do PS2 (PlayerCameraData) e a entrada +0x24 (UpdateCamera).
    std::function<const uint8_t*()> camera;
    std::function<void()> camera_update;
    // RPlayerCamState::AimZoom (0x001be570) e AimRelease (0x001be660).
    std::function<void()> aim_zoom, aim_release;
    // O fgZoomButton da câmera do port (CameraStatics::zoom_button).
    int32_t* zoom_button = nullptr;
    // AttributeSet::LookupVector(o carro do jogador +0x5c, nome, 0) (0x00169e48): falso sem a chave (o PS2 leria nulo).
    std::function<bool(const char* name, float out[4])> lookup_vector;
    // AMenuSound::Trigger(banco, nome, mistura, 0).
    std::function<void(int32_t bank, const char* name, const char* mix)> menu_sound;
};
AICharacterEnv& ai_character_env();

// O que a base, o Human e as mãos chamam do ator e do mundo (o padrão: driving/anim/act_actor.h, o chão do world() e as
// assinaturas da Simulation; a parte 20 do nf_driving_check troca pelos ganchos que o original também vê).
struct AICharacterActorApi {
    uint32_t (*set_new_animation)(uint32_t actor, int32_t bank, int32_t index) = nullptr;  // 0x00107c68
    bool (*is_animation_done)(uint32_t actor) = nullptr;                                   // 0x0010ac58
    float (*current_frame)(uint32_t controller) = nullptr;                                 // 0x00107388
    float (*current_time)(uint32_t controller) = nullptr;                                  // 0x001073a8
    float (*total_time)(uint32_t controller) = nullptr;                                    // 0x001073c0
    float (*remaining_time)(uint32_t controller) = nullptr;                                // 0x001073d8
    void (*world_position)(uint32_t actor, float* out) = nullptr;                          // 0x00108e98
    void (*get_origin)(uint32_t actor, float* out) = nullptr;                              // 0x0010ad90
    void (*set_origin)(uint32_t actor, float* origin) = nullptr;                           // 0x0010adf8
    void (*local_pos_ori)(uint32_t actor, float* out) = nullptr;                           // 0x00108d48
    void (*weapon_position)(uint32_t actor, float* out, int32_t weapon) = nullptr;         // 0x00108ec8
    // WWorldPos::FindClosestFace(pos, 1) e HeightAtPoint(pos, 0) sobre o WWorldPos (p32): falso sem o mundo.
    bool (*floor_height)(uint32_t world_pos, const float pos[3], float& height) = nullptr;
    // PhysicsObject::IsOwnedBy(Simulation::GetPlayerObject()) (0x00195c70).
    bool (*owned_by)(SimpleObject& self, PhysicsObject* player) = nullptr;
};
AICharacterActorApi& ai_character_actor_api();
// Os padrões do port.
AICharacterActorApi ai_character_default_actor_api();

// O que o port conta.
struct AICharacterStats {
    uint64_t constructed = 0, executes = 0, humans = 0, hands_fired = 0, reloads = 0, changes = 0;
    uint64_t sound = 0;           // GetACharacter (o ACharacter não foi portado)
    uint64_t pedestrians = 0;     // AICharacterPedestrian::Init/Destroy (não portado)
    uint64_t bond = 0, passenger = 0;  // AICharacterBond/Passenger: os atores no carro (não portados: IK)
    uint64_t camera = 0;          // as chamadas à câmera sem o gancho (UpdateCamera, AimZoom, AimRelease)
    uint64_t menu_sounds = 0;
    uint64_t guards = 0;
};
AICharacterStats& ai_character_stats();
// A anotação das chamadas para fora (a parte 20 compara com o original), como act_trace().
std::vector<std::array<uint32_t, 4>>& ai_character_trace();
bool& ai_character_trace_enabled();
// Port: tudo zerado (o começo de uma missão; os testes).
void ai_character_reset();

}  // namespace drv
