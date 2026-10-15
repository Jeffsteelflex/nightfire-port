#pragma once
// As armas do carro do jogador: o SWeaponManager do DRIVING.ELF (SWeaponManager.obj; docs/driving.md seção 22) e a
// fila de ações dele (ActionQueue, actionqueuemgr.obj: "WeaponManQ").
//
// O jogo cria o gerente no GameLoop_StartUp (um `new` de 0x190 bytes em 0x33a530; o construtor aloca a tabela das 32
// armas, 0xa80 bytes, e a fila de 0x974 bytes do FastAlloc) e o liga ao carro do jogador pelo SetPlayerCar (o
// RSceneObj do carro: a animação dele, RAnimEngine::Handle em +0x28, é quem dispara as armas). O LoadWeaponSet da
// missão carrega as armas do 'MSet'; os eventos dos power-ups (EPowerUpAmmo, EPowerUpMultiDamage, ESetSpecialWeapon,
// EUnloadWeapon) mexem nelas. A cada quadro da simulação o ESimFrameUpdate chama o Update: as ações da fila (a
// entrada do jogador: o tiro primário, o secundário, o especial, trocar de arma, recarregar), o tiro (FireWeapon: o
// prazo de cada cano, a munição, o pente e o estímulo 10 na animação do sistema da arma: DoWeaponAnim), a recarga (o
// estímulo 11), os estímulos 8 e 9 que ligam e desligam os sistemas das armas carregadas, o desfoque do turbo, o
// "distance click" dos helicópteros e a mira (WTargetPicker).
//
// O tiro em si não está aqui: o estímulo faz a animação do carro (o lançador abre) e um evento da trilha dela
// (EFireWeapon, número 10) cria o míssil ou o projétil (driving/game/projectiles.h).
//
// Portado do MIPS do PS2. O leiaute do PS2 fica em SWeaponManagerData (0x190 bytes), WeaponEntry (0x54) e
// ActionQueueData (0x974), para a parte 18 do nf_driving_check comparar com o original.
//
// Divergências (cada uma também na função):
//  - as chamadas a sistemas não portados (o AMenuSound, o WTargetPicker, o AICharacterHands, o RPlayerCamera, o
//    AIZoneController, o RColorize, o GHud::LavaDamageEffectEnabled, o RVehicle::TriggerFX dos clarões, os métodos do
//    PBondCar das armas especiais) são anotadas (weapons_trace(), só na conferência) e contadas (weapons_stats());
//  - a animação do carro (RAnimEngine::Handle) vem de weapons_env().anim, que o nf_driving liga ao porte da animação
//    (driving/render/anim_engine.h) e a conferência a um sorteio igual nos dois lados;
//  - o UpdateTurret (só com HAS_TURRET, que nenhum carro de Paris tem) e o HandleNuclearSequence depois do disparo
//    (o estado +0x54 diferente de 0: o ENuclearBlast) ficam anotados e contados (weapons_stats().pending);
//  - o tipo de uma arma vem dos dados do 'MSet' e dos eventos: um índice fora de 0..31 conta como guarda e não faz
//    nada (o PS2 escreveria além da tabela).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace drv {

class BondCar;

// ---------------------------------------------------------------- a fila de ações

// ActionData: a ação (GAMEACTION_*), o aparelho e o valor.
struct ActionDataPS2 {
    int32_t action;
    int32_t device;
    float value;
};
// ActionQueue (0x974 bytes): o gerente das filas, a contagem, o fim (onde a próxima entra, -1 vazia), o começo, a
// capacidade (200) e as entradas.
struct ActionQueueData {
    uint32_t manager;       // +0x00 (p32 ActionQueueManager)
    int32_t count;          // +0x04
    int32_t tail;           // +0x08
    int32_t head;           // +0x0c
    int32_t capacity;       // +0x10
    ActionDataPS2 entries[200];  // +0x14
};
static_assert(sizeof(ActionQueueData) == 0x974);

// [driving] 0x00165288 ActionQueue::ActionQueue (com 0x00165488 Initialize; o RegisterQueue no ActionQueueManager
// fica com quem cria)
void ActionQueue_Construct(ActionQueueData& q, uint32_t manager);
// [driving] 0x001654d8 ActionQueue::PushAction (FeedQueue, sem nome no SYM): a fila cheia perde a mais antiga.
void ActionQueue_Push(ActionQueueData& q, const ActionDataPS2& a);
// [driving] 0x001654c8 ActionQueue::IsEmpty
inline bool ActionQueue_IsEmpty(const ActionQueueData& q) { return q.count == 0; }
// [driving] 0x001655c0 ActionQueue::GetAction: a mais antiga (nula com a fila vazia).
const ActionDataPS2* ActionQueue_GetAction(const ActionQueueData& q);
// [driving] 0x00165568 ActionQueue::PopAction
void ActionQueue_PopAction(ActionQueueData& q);
// [driving] 0x001655a8 ActionQueue::Flush
void ActionQueue_Flush(ActionQueueData& q);
// [driving] 0x00165850 ActionQueueManager::FeedQueue: o filtro (a ação 0x1f no passo 0 não entra).
bool ActionQueueManager_Accepts(int32_t action);

// ---------------------------------------------------------------- o gerente

constexpr int kWeaponCount = 32;

// Uma arma (0x54 bytes; a tabela inicial vem do construtor estático 0x20f2b8, kWeaponDefaultTable). Os tipos: 0 Gun,
// 1 Rocket, 2 Missile, 3 Q-Booster, 4-7 as minas, 8 Detonator, 9 Spikes, 10 Q-Slick, 11 Q-Smoke, 12 Q-Worm, 13 Laser,
// 14 Q-Pulse (o EMP), 15 Radar, 16 Cannon, 17 Q-Vision, 18 2 Wheels, 19 Grenade, 20 Q-Charge, 21-22 os torpedos; de 23
// em diante as de Bond com a direção automática (P2K, Sniper Rifle...). Paris: 2 (30) e 26 (24, a seção do
// helicóptero).
struct WeaponEntry {
    int32_t type;               // +0x00 o próprio índice
    uint32_t name;              // +0x04 (p32 char) "Missile"
    int32_t name_label;         // +0x08 o rótulo do nome no .loc
    uint32_t msg_ammo;          // +0x0c o rótulo da mensagem do EPowerUpAmmo (arma sem pente ou pente vazio)
    uint32_t msg_clip;          // +0x10 o do pente
    uint32_t flags;             // +0x14 1 carregada, 2 o sistema tocando, 4 recarregando, 0x20 ligar o sistema, 0x40 ligado
    int32_t ammo;               // +0x18 a munição
    int32_t clip;               // +0x1c os tiros no pente
    int32_t clip_size;          // +0x20 o pente (4 no Vanquish: o par de mísseis do lançador)
    int32_t last_fire[4];       // +0x24 o passo do último tiro de cada cano
    int32_t damage;             // +0x34 o dano (o terceiro valor do LoadWeapon)
    int32_t counts_as_shot;     // +0x38 conta no SMissionManager::IncShotsFired
    int32_t fire_delay;         // +0x3c o intervalo em passos (o Reset: a tabela 0x33a030 x a taxa de quadros)
    int32_t sound_bank;         // +0x40 o banco do AMenuSound ("SFX_" + o nome + "Reload"/"Init"), -1 sem som
    uint32_t sound_name;        // +0x44 (p32 char)
    int32_t barrel;             // +0x48 o próximo cano a disparar
    int32_t reload_barrel;      // +0x4c o próximo cano a recarregar
    int32_t shots;              // +0x50 os tiros disparados
};
static_assert(sizeof(WeaponEntry) == 0x54);

struct SWeaponManagerData {
    int32_t primary;            // +0x00 (-1)
    int32_t secondary;          // +0x04
    int32_t special;            // +0x08 (-1)
    int32_t hands_secondary;    // +0x0c a secundária guardada enquanto Bond atira (direção automática)
    uint32_t weapons;           // +0x10 (p32 WeaponEntry[32])
    uint32_t player_car;        // +0x14 (p32 RSceneObj: a animação em +0x28)
    int32_t last_fired;         // +0x18 a última arma disparada (-1)
    uint32_t last_target;       // +0x1c (p32 WTargetable) o alvo do último tiro
    uint32_t action_queue;      // +0x20 (p32 ActionQueueData) "WeaponManQ"
    int32_t primary_held;       // +0x24
    int32_t secondary_held;     // +0x28
    int32_t special_held;       // +0x2c
    int32_t hands_fired;        // +0x30
    int32_t reload_pending;     // +0x34
    int32_t unknown38;          // +0x38
    int32_t unknown3c;
    int32_t unknown40;          // +0x40
    int32_t boost_blur;         // +0x44 o desfoque do turbo (120 ao disparar; x 0,99 por passo)
    int32_t unknown48;          // +0x48
    int32_t multi_damage;       // +0x4c o EPowerUpMultiDamage (o lançador dispara o pente inteiro)
    int32_t next_select;        // +0x50 o passo a partir do qual a troca de arma vale
    int32_t nuclear_state;      // +0x54 o StartNuclearSequence (1), o HandleNuclearSequence (2..)
    uint32_t unknown58[2];
    float nuclear_pos[3];       // +0x60
    int32_t nuclear_time;       // +0x6c
    float nuclear_radius;       // +0x70
    int32_t click_count;        // +0x74 o "distance click" dos helicópteros
    float turret_angle;         // +0x78
    uint8_t turret_on_target;   // +0x7c
    uint8_t unknown7d[3];
    uint32_t special_trigger;   // +0x80 (p32 CARP::Trigger) o lugar do especial
    float special_range;        // +0x84
    int32_t priority[32];       // +0x88 CalcWeaponPriorities: a posição na fWeaponSortOrder (-1 fora)
    int32_t sorted[32];         // +0x108 SortWeaponList: as secundárias disponíveis em ordem
    int32_t sorted_count;       // +0x188
    uint32_t unknown18c;
};
static_assert(sizeof(SWeaponManagerData) == 0x190);
static_assert(offsetof(SWeaponManagerData, boost_blur) == 0x44 && offsetof(SWeaponManagerData, nuclear_pos) == 0x60 &&
              offsetof(SWeaponManagerData, special_trigger) == 0x80 && offsetof(SWeaponManagerData, priority) == 0x88);

// A tabela inicial (o construtor estático 0x20f2b8: 32 entradas de 21 palavras, as cadeias como ponteiros para o
// .rodata) e o intervalo de cada arma em segundos (0x33a030).
struct WeaponWord {
    int32_t v;
    const char* s;
    constexpr WeaponWord() : v(0), s(nullptr) {}
    constexpr WeaponWord(int32_t x) : v(x), s(nullptr) {}
    constexpr WeaponWord(const char* p) : v(0), s(p) {}
};
extern const WeaponWord kWeaponDefaultTable[kWeaponCount][21];
extern const float kWeaponFireDelay[kWeaponCount];
// SWeaponManager::fWeaponSortOrder (0x33a4b0) e os sistemas de animação de cada arma (0x33a0b0: 8 canos, 0 no fim).
extern const int32_t kWeaponSortOrder[kWeaponCount];
extern const int32_t kWeaponSystems[kWeaponCount][8];

// O que o gerente lê dos sistemas à volta (o porte de cada um, ou a conferência).
struct WeaponAnimApi {
    std::function<bool(uint32_t handle, int32_t system)> is_playing;          // 0x1a2d10 IsSystemPlaying
    std::function<int32_t(uint32_t handle, int32_t system)> state;            // 0x1a2d90 GetSystemState
    std::function<void(uint32_t handle, int32_t system, int32_t stimulus, int32_t time, int32_t mode)> stimulus;
                                                                              // 0x1a27a0 ProcessStimuli
};
struct WeaponsEnv {
    WeaponAnimApi anim;
    // O RSceneObj do carro do jogador: a palavra +0x28 (o RAnimEngine::Handle). 0 sem carro.
    std::function<uint32_t(uint32_t scene_obj)> anim_handle;
    // AttributeSet::LookupInt/LookupBool do objeto do jogador (+0x5c).
    std::function<int32_t(const char* key, bool* found)> lookup_int;
    std::function<bool(const char* key)> lookup_bool;
    // PhysicsObject::GetPosition do objeto do jogador.
    std::function<void(float out[3])> player_position;
    // PBondCar::GetCarType (a entrada +0x84 da tabela virtual): o nome do carro do jogador.
    std::function<const char*()> car_type;
    // PBondCar::GetPhysics()->IS_SUB (+0xbc).
    std::function<int32_t()> car_is_sub;
    std::function<bool()> camera_exists;  // a lista de câmeras (0x333ed8) existe
    // WWorld::GetSceneObjFromInstance: o objeto de cena (p32) de uma instância, 0 sem.
    std::function<uint32_t(uint32_t instance)> scene_obj_of;
    // RPlayerCamera::CameraAiming (o RPlayerCamera de 0x333ed8 +4).
    std::function<bool()> camera_aiming;
    // WTargetPicker::GetSelectedTarget (0x33a970): o alvo (p32), 0 sem.
    std::function<uint32_t()> selected_target;
    // O alvo é de tipo 1 (+0x34) com um objeto (+0x48) cujo atributo ONLY_TARGET_WITH_EMP é verdadeiro.
    std::function<bool(uint32_t target)> target_only_emp;
    // O RPlayerCamera (0x333ed8 +4): o +0x354 -> +0x2c (a vista de dentro tem arma) que o FireWeapon lê com o +0x5ec
    // do gerente da missão.
    std::function<int32_t()> camera_arm_2c;
    // O corpo do carro do jogador (Simulation::GetRigidBody da vaga dele): a posição (+0x10), a frente do
    // RigidBodyInfo (+0x5c -> +0x20) e o +0x4c8 dele (o Q-Smoke registra a zona atrás do carro).
    std::function<void(float pos[4], float fwd[4], float& back)> player_body;
    // PBondCar::EnableRocketBoost (a entrada +0x230 da tabela virtual): verdadeiro se o turbo começou.
    std::function<bool()> enable_rocket_boost;
    std::function<bool()> hud_exists;           // GHud::TheApp() != 0
    std::function<int32_t()> hud_2a0;           // GHud +0x2a0 (o menu de pausa aberto)
    std::function<bool()> hud_lava_damage;      // GHud::LavaDamageEffectEnabled
    int32_t infinite_ammo = 0;   // 0x33a008 (a munição sem fim)
    int32_t helicopters = 0;     // AIVehicleController +8 (sem o porte da IA: nenhum)
    // AICharacterBond::fgRightButton e fgLeftButton (0x3268c4, 0x3268c0): o Bond da direção automática lê.
    int32_t fg_right_button = 0, fg_left_button = 0;
};
WeaponsEnv& weapons_env();

// O que o porte conta.
struct WeaponsStats {
    uint64_t updates = 0, actions = 0, fired = 0, stimuli = 0, reloads = 0;
    uint64_t external_calls = 0;  // as chamadas a sistemas não portados (weapons_trace())
    uint64_t pending = 0;         // o UpdateTurret e o HandleNuclearSequence depois do disparo
    uint64_t guards = 0;
};
WeaponsStats& weapons_stats();

// Uma chamada a um sistema não portado (a função do PS2, os argumentos e as cadeias que ela recebe). Só anotada com
// weapons_trace_enabled() (a parte 18 do nf_driving_check compara com o original).
struct WeaponsCall {
    uint32_t fn;
    uint32_t args[4];
    std::string s1, s2;
};
std::vector<WeaponsCall>& weapons_trace();
bool& weapons_trace_enabled();
void weapons_note_call(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0,
                       const char* s1 = nullptr, const char* s2 = nullptr);

// O gerente do jogo (0x33a530): criado pelo weapons_create (o GameLoop_StartUp), nulo antes.
SWeaponManagerData* weapon_manager();
bool weapon_manager_exists();
// GameLoop_StartUp: o `new` e o construtor; GameLoop_CleanUp: o destrutor.
SWeaponManagerData& weapons_create();
void weapons_destroy();
WeaponEntry* weapon_entry(SWeaponManagerData& w, int32_t type);
ActionQueueData* weapon_queue(SWeaponManagerData& w);

// [driving] 0x0020bb00 SWeaponManager::SWeaponManager (a tabela e a fila no armazenamento do port)
void SWeaponManager_Construct(SWeaponManagerData& w, WeaponEntry* table, ActionQueueData* queue);
// [driving] 0x0020bbe0 SWeaponManager::Reset
void SWeaponManager_Reset(SWeaponManagerData& w);
// [driving] 0x0020bd80 SWeaponManager::CalcWeaponPriorities
void SWeaponManager_CalcWeaponPriorities(SWeaponManagerData& w);
// [driving] 0x0020bdf8 SWeaponManager::SortWeaponList
void SWeaponManager_SortWeaponList(SWeaponManagerData& w);
// [driving] 0x0020bf20 SWeaponManager::FindSortedSecondary
int32_t SWeaponManager_FindSortedSecondary(const SWeaponManagerData& w);
// [driving] 0x0020bf80 SWeaponManager::SetPlayerCar (o SMissionManager::InitPlayerCar fica com quem chama)
void SWeaponManager_SetPlayerCar(SWeaponManagerData& w, uint32_t scene_obj);
// [driving] 0x0020bff0 SWeaponManager::SetSpecialWeapon(int, CARP::Trigger *, float): (alcance, gerente, tipo, gatilho)
void SWeaponManager_SetSpecialWeapon(SWeaponManagerData& w, int32_t type, uint32_t trigger, float range);
// [driving] 0x0020c078 SWeaponManager::Update
void SWeaponManager_Update(SWeaponManagerData& w);
// [driving] 0x0020d378 SWeaponManager::LoadWeapon(WeaponType, int, int, int)
void SWeaponManager_LoadWeapon(SWeaponManagerData& w, int32_t type, int32_t ammo, int32_t clip_size, int32_t arg2);
// [driving] 0x0020d420 SWeaponManager::UnloadWeapon(WeaponType)
void SWeaponManager_UnloadWeapon(SWeaponManagerData& w, int32_t type);
// [driving] 0x0020d480 SWeaponManager::SetPrimary
inline void SWeaponManager_SetPrimary(SWeaponManagerData& w, int32_t type) { w.primary = type; }
// [driving] 0x0020d488 SWeaponManager::SetSecondary (o RPlayerCamera::UpdateCurrentArm anotado)
void SWeaponManager_SetSecondary(SWeaponManagerData& w, int32_t type);
// [driving] 0x0020d4c0 SWeaponManager::SelectSecondary(bool próxima)
void SWeaponManager_SelectSecondary(SWeaponManagerData& w, bool next);
// [driving] 0x0020d598 SWeaponManager::GetPreviousSecondaryType
int32_t SWeaponManager_GetPreviousSecondaryType(SWeaponManagerData& w);
// [driving] 0x0020d608 SWeaponManager::GetNextSecondaryType
int32_t SWeaponManager_GetNextSecondaryType(SWeaponManagerData& w);
// [driving] 0x0020d678 SWeaponManager::FirePrimary
void SWeaponManager_FirePrimary(SWeaponManagerData& w);
// [driving] 0x0020d6b0 SWeaponManager::FireSecondary
void SWeaponManager_FireSecondary(SWeaponManagerData& w);
// [driving] 0x0020d700 SWeaponManager::FireSpecial
void SWeaponManager_FireSpecial(SWeaponManagerData& w);
// [driving] 0x0020d740 SWeaponManager::DoWeaponAnim(WeaponType, int estímulo, unsigned int cano)
void SWeaponManager_DoWeaponAnim(SWeaponManagerData& w, int32_t type, int32_t stimulus, int32_t barrel);
// [driving] 0x0020de20 SWeaponManager::FireWeapon(WeaponType)
void SWeaponManager_FireWeapon(SWeaponManagerData& w, int32_t type);
// [driving] 0x0020e678 SWeaponManager::GetWeaponFromSystem(int): a arma de um sistema de animação, -1 nenhuma.
int32_t SWeaponManager_GetWeaponFromSystem(int32_t system);
// [driving] 0x0020e6f0 SWeaponManager::GetSystemFromWeapon(WeaponType, int cano)
int32_t SWeaponManager_GetSystemFromWeapon(int32_t type, int32_t barrel);
// [driving] 0x0020e868 SWeaponManager::UserReloading
void SWeaponManager_UserReloading(SWeaponManagerData& w);
// [driving] 0x0020e978 SWeaponManager::HandleClipUpdate
void SWeaponManager_HandleClipUpdate(SWeaponManagerData& w);
// [driving] 0x0020d9a8 SWeaponManager::UpdateRadarDish
void SWeaponManager_UpdateRadarDish(SWeaponManagerData& w);
// [driving] 0x0020d9f0 SWeaponManager::UpdateTurret (só o começo: sem HAS_TURRET nada)
void SWeaponManager_UpdateTurret(SWeaponManagerData& w);
// [driving] 0x0020ea60 SWeaponManager::HandleNuclearSequence (só o começo: com o estado 0 nada)
void SWeaponManager_HandleNuclearSequence(SWeaponManagerData& w);
// [driving] 0x0020ea20 SWeaponManager::StartNuclearSequence(COORD3 &, float)
void SWeaponManager_StartNuclearSequence(SWeaponManagerData& w, const float pos[3], float radius);

// Os destrutores dos eventos das armas e dos power-ups (o trabalho do evento; driving/game/events.h): 25 EPowerUp,
// 56 EPowerUpAmmo, 60 EPowerUpMultiDamage, 149 EDisablePowerUp, 150 EEnablePowerUp, 192 ESetSpecialWeapon,
// 249 EGadgetOn, 269 EUnloadWeapon. Devolve falso para os outros números. As chamadas a sistemas não portados (o
// GFX::Trigger do efeito, o RSceneObj::Hide/Show, o Simulation::TrackInstance/UntrackInstance, o
// RAnimEngine::Handle::ProcessStimuli da instância, o AMenuSound, o GHud::GadgetOn) ficam anotadas.
struct PendingEvent;
bool weapons_event_execute(const PendingEvent& e);

// Port: as ações do jogador para a fila do gerente (o ActionQueueManager::FeedQueue a todas as filas).
void weapons_feed(int32_t action, int32_t device, float value);

}  // namespace drv
