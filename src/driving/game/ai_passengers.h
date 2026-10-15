#pragma once
// Os passageiros dos carros da IA (docs/driving.md seção 26): o AICharacterEnemyWindow (o atirador que se debruça na
// janela), o AICharacterEnemySunroof (o que sobe pelo teto solar, com a metralhadora ou a bazuca) e o
// AICharacterEnemyDriver (o motorista com as mãos no volante), do MIPS do DRIVING.ELF (AICharacterEnemyGround.obj e
// AICharacterBond.obj). Em Paris, 17 dos 25 elementos do 'AIEl' levam um: cinco na janela e nove no teto (os
// "paris_henchbigbox", o capanga mascarado "2.h_pmask.h_pmask.t_paris.95" com a "ssmg" ou a "bazooka") e três
// motoristas (os "paradis_car", "3.c_domm.h_dommin.t_dommin.100"). Os carros da IA de Paris não têm armas próprias:
// quem atira no jogador são eles.
//
// Como o PS2 faz: o AIElementController::WakeUp (driving/ai/element_controller.h) cria o carro e, com o banco dos atores
// (o ActActorDatabase 0x326000), um personagem por passageiro vivo pela classe (3 a janela, 4 o teto, 0x11 o
// motorista; 0xe/0xf/0x10 são os da neve, que Paris não usa): o objeto, o ator (ActActorDatabase::GetNewActorHandle com
// o modelo da tabela 0x327ba8, a arma da 0x327b90 e a função da matriz GetRotPos da classe, a matriz com o lugar do
// banco: o vetor AIC_EWIND_POS / AIC_ESUNR_POS / AIC_BOND_POS dos atributos do carro), o SetActor, a primeira animação
// e o Human (o corpo simples que leva os tiros ao personagem, dono o carro). A cada passo o Human::Simulate roda o
// AICharacter::Execute: a matriz pela pose do ator e a do carro (UpdateRotPos), as interrupções (a morte com o carro, a
// vida) e o estado pela tabela virtual.
//
// O atirador da janela: escondido (o ator invisível) até o jogador entrar no alcance (TargetIsInRange: a distância ao
// quadrado até o +0x64 e o ângulo à direita do carro); então se debruça (o estado 2 com o 8: a animação 0xa), arma (8 ->
// 9) e, a cada passo armado, mira (DoAiming: o rumo +0x154 anda dois graus por passo para o lado do alvo, o pescoço e o
// ombro direito pelas sobreposições globais do ator) e atira quando o período (+0x8c, 60 passos) passou (o estado 0xb
// com uma das cinco animações de tiro: a chave "Fire" do ActPoser::DoEventPose chega ao ActActor::Fire, a arma toca o
// estímulo 10 e a chave dela cria o EFireWeapon, que mira no carro do jogador com a precisão do passageiro).
//
// O do teto: abre o teto (o estímulo 8 do sistema 0x13 do carro), registra-se como alvo da mira do jogador (o farol do
// carro apagado) e sobe; armado, gira o ator (0,005 volta por passo) até o alvo e atira, recarrega (a "ssmg" 25 tiros) e
// se agarra quando o carro acelera ou freia forte (VehicleIsAccelerating: os estados 0x10/0x15/0x14 de inclinação).
//
// O motorista: os pulsos presos ao volante (o IK e as sobreposições globais pelos efeitos "steerleft"/"steerright" do
// RSceneObj do carro) e três animações ao acaso.
//
// Os leiautes são os do PS2 (o Window 0x170, o Sunroof 0x180, o Driver 0x160) para a parte 22 do nf_driving_check
// (`--so passageiros`); o que eles chamam do ator, do carro e do resto vem de passenger_api() (os padrões são o port; a
// conferência troca pelos ganchos que o original também vê).
//
// Divergências (contadas em passenger_stats(), cada uma também na função):
//  - o ACharacter::Say ("HDeath", "HGrunt": o som do personagem, AICharacter::GetACharacter) não foi portado;
//  - o crossfade (ActActor::SetNewCrossFadeAnimation, CrossFadeAnimationController::StartNewCrossFade): o padrão do
//    port toca a animação nova sem a mistura de 0,25 s (o ActPoser do port não tem o modo 2) e devolve 0 (o +0xa0 fica
//    nulo, e o próximo ferimento também chama o SetNewCrossFadeAnimation);
//  - o IK do motorista (ActActor::CreateIKs, SetIKInfoArray: o ActIKSolverArray::Solve não foi portado) só guarda a
//    informação; as sobreposições dos pulsos são feitas;
//  - o RSceneObj::LookupFXHandle e o LocateFX do motorista vêm de passenger_api() (o nf_driving liga os efeitos do
//    modelo do carro; sem eles o motorista fica sem as mãos no volante, como no PS2 sem os efeitos).

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "driving/game/ai_character.h"

namespace drv {

class BondCar;

// AICharacterEnemyWindow (0x170 bytes).
struct AICharacterEnemyWindowData {
    AICharacterData base;
    uint32_t car;            // +0x150 (p32 PBondCarData) o carro
    float aim;               // +0x154 o rumo do ombro (graus: -60..80)
    uint32_t others[2];      // +0x158 os outros passageiros do mesmo carro (DoInitial)
    int32_t side;            // +0x160 o lado da janela (0)
    int32_t countdown;       // +0x164 passos armado antes de poder desarmar (a taxa x 300)
    uint32_t unknown168[2];
};
static_assert(sizeof(AICharacterEnemyWindowData) == 0x170);
static_assert(offsetof(AICharacterEnemyWindowData, car) == 0x150 &&
              offsetof(AICharacterEnemyWindowData, countdown) == 0x164);

// AICharacterEnemySunroof (0x180 bytes).
struct AICharacterEnemySunroofData {
    AICharacterData base;
    uint32_t car;            // +0x150 (p32 PBondCarData)
    uint32_t unknown154[3];  // +0x154 (o construtor não escreve)
    float vel[3];            // +0x160 a velocidade do carro no passo anterior (VehicleIsAccelerating)
    int32_t accel;           // +0x16c a direção da aceleração (1 frente, 2 trás, 3 direita, 4 esquerda; 0 nenhuma)
    uint32_t others[2];      // +0x170
    uint32_t target;         // +0x178 (p32 WTargetable) o alvo da mira do jogador
    uint32_t unknown17c;
};
static_assert(sizeof(AICharacterEnemySunroofData) == 0x180);
static_assert(offsetof(AICharacterEnemySunroofData, vel) == 0x160 &&
              offsetof(AICharacterEnemySunroofData, target) == 0x178);

// AICharacterEnemyDriver (0x160 bytes).
struct AICharacterEnemyDriverData {
    AICharacterData base;
    uint32_t car;            // +0x150 (p32 PBondCarData)
    uint32_t fx_left;        // +0x154 o efeito "steerleft" do RSceneObj do carro
    uint32_t fx_right;       // +0x158 "steerright"
    int32_t idle;            // +0x15c a animação de espera (0..2)
};
static_assert(sizeof(AICharacterEnemyDriverData) == 0x160);

constexpr uint32_t kVtAICharacterEnemyWindow = 0x00369200, kVtAICharacterEnemySunroof = 0x00368f08,
                   kVtAICharacterEnemyDriver = 0x003681f0;
// As funções da matriz dos atores (o +0x10 do ActActor): os GetRotPos(int, MATRIX4 &, COORD3 &) das classes.
constexpr uint32_t kFnWindowGetRotPos = 0x00122358, kFnSunroofGetRotPos = 0x00120a18,
                   kFnDriverGetRotPos = 0x0011ab50;

// 0x001224b0 AICharacterEnemyWindow::AICharacterEnemyWindow(MATRIX4 &, PVehicle *)
void AICharacterEnemyWindow_Construct(AICharacterEnemyWindowData& c, const float m[16], uint32_t car);
// 0x00120b70 AICharacterEnemySunroof::AICharacterEnemySunroof(MATRIX4 &, int arma, int, PVehicle *)
void AICharacterEnemySunroof_Construct(AICharacterEnemySunroofData& c, const float m[16], int32_t weapon,
                                       int32_t unknown60, uint32_t car);
// 0x0011aca8 AICharacterEnemyDriver::AICharacterEnemyDriver(MATRIX4 &, PVehicle *)
void AICharacterEnemyDriver_Construct(AICharacterEnemyDriverData& c, const float m[16], uint32_t car);
// Os destrutores (0x001225a8, 0x00120d18, 0x0011ad88): a tabela, o alvo do teto, o ~AICharacter.
void AICharacterEnemyWindow_Destruct(AICharacterEnemyWindowData& c);
void AICharacterEnemySunroof_Destruct(AICharacterEnemySunroofData& c);
void AICharacterEnemyDriver_Destruct(AICharacterEnemyDriverData& c);
// Os GetRotPos(int, MATRIX4 &) (0x00122238, 0x001208f8, 0x0011aa30) e (int, MATRIX4 &, COORD3 &): a orientação do
// corpo do carro com a posição dele; a velocidade no terceiro.
void AICharacterEnemy_CarRotPos(uint32_t self, float m[16], float vel[3]);
// 0x00121c10 AICharacterEnemySunroof::VehicleIsAccelerating
bool AICharacterEnemySunroof_VehicleIsAccelerating(uint32_t self);

// O que os passageiros chamam do ator, do carro e do resto (os padrões: o port; a parte 22 troca pelos ganchos).
struct PassengerApi {
    void (*set_time_scale)(uint32_t actor, float scale) = nullptr;                            // 0x0010ace0
    int32_t (*get_bone_index)(uint32_t actor, const char* name) = nullptr;                    // 0x00108438
    void (*create_overrides)(uint32_t actor, int32_t n, const int32_t* bones, int32_t local) = nullptr;  // 0x001090c0
    void (*set_overrides)(uint32_t actor, int32_t n, const float* m, const float* w) = nullptr;         // 0x00109120
    void (*rotate_actor)(uint32_t actor, float turns) = nullptr;                              // 0x0010afc0
    uint32_t (*set_new_crossfade)(uint32_t actor, float t, int32_t bank, int32_t index) = nullptr;     // 0x00107d08
    void (*start_new_crossfade)(uint32_t controller, float t, int32_t bank, int32_t index) = nullptr;  // 0x001075e0
    void (*create_iks)(uint32_t actor, int32_t n, const int32_t* bones, const float* a, const float* b) = nullptr;  // 0x00108f60
    void (*set_ik_info_array)(uint32_t actor, int32_t n, const float* info) = nullptr;       // 0x00109050
    uint32_t (*lookup_fx)(uint32_t scene, const char* name) = nullptr;                         // 0x001cad58
    void (*locate_fx)(uint32_t scene, uint32_t fx, float m[16]) = nullptr;                     // 0x001cae00
    void (*process_stimuli)(uint32_t handle, uint32_t system, uint32_t stimulus, uint32_t steps, uint32_t mode) = nullptr;  // 0x001a27a0
    void (*say)(uint32_t self, const char* what) = nullptr;                                    // ACharacter::Say (0x0013..)
    void (*inc_kills)(int32_t n) = nullptr;                                                    // SMissionManager::IncKills
    void (*register_target)(uint32_t target) = nullptr;                                       // 0x0022afa8
    void (*unregister_target)(uint32_t target) = nullptr;                                     // 0x0022b098
    uint32_t (*new_character_target)(uint32_t character) = nullptr;                           // WTargetable(AICharacter *)
    void (*remove_reference)(uint32_t target) = nullptr;                                      // 0x00229bb0
    void (*human_activate)(uint32_t human) = nullptr;                                         // 0x0017f540
    void (*human_deactivate)(uint32_t human) = nullptr;                                       // 0x0017f570
    // PBondCar::EnableTargetBeacon(bool) e DisableTargetBeacon (as entradas +0x74 e +0x7c da tabela do carro).
    void (*enable_beacon)(uint32_t car, int32_t create) = nullptr;
    void (*disable_beacon)(uint32_t car) = nullptr;
    // O WakeUp: ActActorDatabase::GetNewActorHandle (0x0010b298: sem a segunda arma), Simulation::SpawnHumanObject
    // (0x001ffb90 com o tipo 0 e a assinatura 0), Human::SetAICharacter (0x0017f5a0), PhysicsObject::SetOwnerObject
    // (0x00195c30).
    uint32_t (*new_actor_handle)(uint32_t user, const char* model, const char* weapon, uint32_t callback,
                                 const float* m) = nullptr;
    Human* (*spawn_human)(const float dir[3], const float pos[3]) = nullptr;
    void (*human_set_character)(Human& h, uint32_t character) = nullptr;
    void (*human_set_owner)(Human& h, BondCar& car) = nullptr;
};
PassengerApi& passenger_api();
// O IK do motorista (0x326950: 24 palavras, o alvo dos dois pulsos reescrito pelo HandleInterrupts).
uint32_t* passenger_driver_ik();
PassengerApi passenger_default_api();

struct PassengerStats {
    uint64_t windows = 0, sunroofs = 0, drivers = 0;  // construídos
    uint64_t fires = 0;          // os DoArmed que passaram ao estado 0xb (um tiro pedido à animação)
    uint64_t reloads = 0, aims = 0, deaths = 0, leans = 0;
    uint64_t says = 0;           // ACharacter::Say (não portado)
    uint64_t crossfades = 0;     // o crossfade tocado sem mistura (o padrão do port)
    uint64_t iks = 0;            // o IK guardado sem o Solve
    uint64_t guards = 0;
    std::map<int, uint64_t> guard_lines;  // as guardas por linha de ai_passengers.cpp (o relatório)
};
PassengerStats& passenger_stats();
void passenger_reset();
// A linha "passageiros ..." do relatório do nf_driving: as contas e cada passageiro vivo (a classe, o estado, o
// subestado, visível, a vida, o pente, o lugar).
std::string passenger_report();

// O que o WakeUp faz para cada passageiro vivo (0x0012cb08, a parte dos personagens; docs/driving.md seção 26):
// devolve o AICharacter criado (p32; 0 numa classe que o port não tem). `element` é o CARP::AIElement (p32), `k` o
// passageiro, `car` o carro criado (o assento: o AttributeSet::LookupVector dele, AIC_EWIND_POS, AIC_ESUNR_POS,
// AIC_BOND_POS).
uint32_t AIElementController_WakeUpPassenger(uint32_t element, int32_t k, BondCar& car);
// O laço dos passageiros do WakeUp (0x0012cb08): cada passageiro vivo com o personagem, o alcance pela precisão, o
// Human (a vida no registro, o dono o carro) e o passageiro na arma do ator; `characters_awake` o +0x44 do controlador.
// Devolve quantos personagens criou.
int32_t AIElementController_WakeUpPassengers(uint32_t element, int32_t& characters_awake, BondCar& car);
// O que o construtor do AIElementController carrega para cada passageiro (0x0012b8b0: o ActCharacter::LoadCharacter do
// modelo e da arma).
void AIElementController_LoadPassenger(uint32_t element, int32_t k);

}  // namespace drv
