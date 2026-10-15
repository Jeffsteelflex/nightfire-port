#pragma once
// A missão de uma pista: o SMissionManager do DRIVING.ELF (SMissionManager.obj; docs/driving.md seção 19), as regras
// (SMissionRule e as 15 classes SRule*), os cronômetros (SMissionTimer), os objetivos e as mensagens, e o bloco da troca
// (UberBond, 0x100000) que o BASE.ELF deixa para o DRIVING.ELF.
//
// O jogo cria o gerente no WWorld::Open (SMissionManager::Construct, 0x205188: um `new` de 0x1b50 bytes em
// glbMissionManager, 0x33a024), carrega as regras do 'Rule' do 'Map ' da pista e as configurações do 'MSet'
// (SMissionManager::Load), muda para a etapa 0 e começa (Start). A cada quadro da simulação (ESimFrameUpdate, depois da
// IA) o SMissionManager::Update roda as listas de regras: a lista de sempre (+0x20) e a da etapa corrente (+0x24 + 4 x
// etapa); cada regra (SMissionRule::DoRule) chama a sua CheckRule e, quando ela difere do "invertida", cria os eventos
// da sua lista 'el' (SMissionRule::RunEvents: os mesmos eventos dos gatilhos, driving/game/events.h). Os eventos mudam a
// etapa (EChangeStage na construção; ECallStage, que roda a lista de outra etapa já), os cronômetros, os gatilhos, os
// objetivos, o HUD, e terminam a missão (EPlayerWin, EPlayerLose).
//
// Portado do MIPS do PS2 (a verdade; o nightfire-cxbx não reconstruiu o gerente, só deu nomes de alguns campos). O
// leiaute do PS2 fica em SMissionManagerData (0x1b50 bytes, static_assert) e nas regras (SMissionRuleData, a imagem do
// objeto do PS2 com a tabela virtual em +0x10), para a parte 15 do nf_driving_check comparar com o original.
//
// Divergências (cada uma também na função):
//  - as listas do PS2 (os nós de 0xc bytes do FastAlloc em +0x00, +0x04, +0x20 e +0x24..) ficam em vetores do port
//    (MissionLists); as palavras das cabeças no leiaute ficam 0;
//  - o AIElementController não foi portado: o que as regras e os objetivos perguntam aos elementos da IA (a vida, a
//    posição, a velocidade, as propriedades, as estatísticas) vem de mission_ai(), que os mantém vivos (vida 1000),
//    parados no ponto do 'AIEl' e sem propriedade; as regras de morte ou dano de um inimigo esperam para sempre;
//  - as chamadas a sistemas não portados (o SWeaponManager do LoadWeaponSet, o GHud que não existe, o som, a câmera,
//    o AIElementController) são anotadas (mission_trace(), só na conferência) e contadas (mission_stats());
//  - o SRulePlayerDir (o acos do newlib) e o SRuleAmmo (a munição do SWeaponManager) não foram portados: CheckRule
//    devolve 0 e conta; nenhum dos dois aparece em Paris;
//  - os índices de cronômetro e de etapa que vêm dos dados não são conferidos no PS2 (escrevem fora dos vetores): aqui
//    um índice fora conta como guarda e não faz nada.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "driving/carp_runtime.h"

namespace drv {

class BondCar;

// ---------------------------------------------------------------- o bloco da troca (UberBond, 0x100000)

// Os 0xa50 bytes que o ACTION.ELF preenche (Boot_GetPTPData) e o BASE.ELF deixa em 0x100000 (docs/driving.md seção 4).
// O port não roda o ACTION antes: uber_bond_defaults() põe o que o BASE.ELF passaria numa ida pelo mapa das missões.
struct UberBondBlock {
    uint8_t bytes[0xa50];
    int32_t& word(uint32_t off) { return *reinterpret_cast<int32_t*>(bytes + off); }
    float& real(uint32_t off) { return *reinterpret_cast<float*>(bytes + off); }
    char* text(uint32_t off) { return reinterpret_cast<char*>(bytes + off); }
};
UberBondBlock& uber_bond();
// O bloco de uma ida à missão `track` (o nome em +0x4f4, IDL_ENG.loc em +0x540, válido, os volumes em 100, a
// dificuldade `difficulty` já mapeada pelo main: 0 fácil, 1 normal, 2 difícil).
void uber_bond_defaults(const char* track, int32_t difficulty);

// ---------------------------------------------------------------- o gerente

// SMissionTimer (0x18 bytes).
struct SMissionTimer {
    int32_t active;      // +0x00
    int32_t ticking;     // +0x04 o bipe a cada segundo (só o 1: o EStartTimer)
    int32_t start_step;  // +0x08 o passo da simulação em que começou (ou foi reiniciado)
    int32_t value;       // +0x0c o valor parado (em passos)
    int32_t rate;        // +0x10 passos por passo (-1: contagem regressiva)
    int32_t last_second; // +0x14 o último segundo que bipou
};
static_assert(sizeof(SMissionTimer) == 0x18);

// MissionSectionProfile (0x50 bytes): o perfil de quadros de uma seção (EProfileMissionSection).
struct MissionSectionProfile {
    uint8_t used;        // +0x00
    uint8_t name[0x3f];  // +0x01
    float min;           // +0x40 1000 (Clear)
    float max;           // +0x44
    float sum;           // +0x48
    int32_t count;       // +0x4c
};
static_assert(sizeof(MissionSectionProfile) == 0x50);

// A ação temporizada (0x18 bytes; StartTimedAction, CompleteTimedAction): o EStartTimedAction marca o começo; o fim
// (o EStopTimedAction, ou o prazo no UpdateTimedActions) soma pontos pelo tempo gasto.
struct MissionTimedAction {
    int32_t active;      // +0x00
    float limit;         // +0x04 segundos (passado o prazo, UpdateTimedActions a completa)
    float target;        // +0x08 segundos: até ele, points_max
    int32_t points_min;  // +0x0c no prazo (entre target e limit, a reta de points_max a points_min)
    int32_t points_max;  // +0x10
    int32_t start_step;  // +0x14
};
static_assert(sizeof(MissionTimedAction) == 0x18);

// As configurações da missão (o 'MSet' do 'Map ', 0x2f8 bytes; sem ele, o padrão do construtor em 0x4162b8).
struct MissionSettings {
    char car_type[0x2c];         // +0x000 o veículo do jogador ("vanquish" no padrão; "helicopter" em Paris)
    uint8_t weapon_on[0x20];     // +0x02c as armas do LoadWeaponSet
    int32_t weapon_ammo[0x20];   // +0x04c
    int32_t weapon_arg1[0x20];   // +0x0cc
    int32_t weapon_arg2[0x20];   // +0x14c
    uint8_t unknown1cc[0x27c - 0x1cc];
    int32_t health_max;          // +0x27c 3000 no padrão
    int32_t health;              // +0x280 3000
    int32_t shield_max;          // +0x284 1000
    int32_t shield;              // +0x288 0
    uint8_t unknown28c[0x294 - 0x28c];
    int32_t health_pct[3];       // +0x294 a vida e a vida máxima por dificuldade (%: 0, 1, 2)
    int32_t shield_pct[3];       // +0x2a0 o escudo
    uint8_t unknown2ac[0x2b8 - 0x2ac];
    float scale_a[3];            // +0x2b8 (Load: 1 se zero)
    float scale_b[3];            // +0x2c4
    uint8_t unknown2d0[0x2f8 - 0x2d0];
};
static_assert(sizeof(MissionSettings) == 0x2f8);
static_assert(offsetof(MissionSettings, health_max) == 0x27c && offsetof(MissionSettings, scale_a) == 0x2b8);

// O SMissionManager do PS2 (0x1b50 bytes). Os nomes são do uso; os campos sem nome ficam unknown<deslocamento>.
struct SMissionManagerData {
    uint32_t objectives_head;      // +0x000 (o PS2: a lista de SMissionObjective; port: 0, ver MissionLists)
    uint32_t messages_head;        // +0x004
    int32_t num_stages;            // +0x008 a maior etapa das regras + 1
    int32_t stage;                 // +0x00c a etapa corrente
    int32_t next_stage;            // +0x010 a próxima (ChangeStage; vira a corrente no fim do ProcessRules)
    uint32_t rules;                // +0x014 (p32 UData) o 'Rule'
    uint32_t mset;                 // +0x018 (p32 UData) o 'MSet'
    uint32_t settings;             // +0x01c (p32 MissionSettings)
    uint32_t always_head;          // +0x020
    uint32_t stage_heads[64];      // +0x024
    uint8_t unknown124[0x150 - 0x124];
    SMissionTimer timers[10];      // +0x150
    int32_t unknown240;            // +0x240 -1 (construtor)
    uint8_t unknown244[0x24c - 0x244];
    int32_t programmer_events[64]; // +0x24c ProgrammerDefinedEvent
    int32_t state;                 // +0x34c 0 parada, 1 rodando, 2 pausa, 3 ganhou, 4 perdeu
    int32_t auto_drive;            // +0x350 a direção automática (SetAutoDrive: o carro do jogador na IA, Bond atira)
    int32_t missile_target;        // +0x354
    uint32_t missile;              // +0x358 (p32 Missile) o míssil guiado
    float missile_steer[4];        // +0x35c
    uint32_t action_queue;         // +0x36c (p32 ActionQueue)
    int32_t unknown370;            // +0x370
    int32_t unknown374;            // +0x374 100
    float player_health;           // +0x378 (o +0x54 do carro do jogador aponta para cá)
    float player_health_max;       // +0x37c
    float player_shield;           // +0x380
    float player_shield_max;       // +0x384
    int32_t unknown388;            // +0x388 1
    float unknown38c[5];           // +0x38c
    int32_t unknown3a0;            // +0x3a0 2
    float unknown3a4[5];           // +0x3a4
    int32_t difficulty;            // +0x3b8 o UberBond +0x984
    int32_t shots_fired;           // +0x3bc
    int32_t shots_hit;             // +0x3c0
    int32_t kills;                 // +0x3c4
    float player_damage;           // +0x3c8
    uint32_t cinematic_mode;       // +0x3cc as bandeiras do modo de cinema (1, 2, 4)
    int32_t skipping_cinematic;    // +0x3d0
    int32_t objective_seen_step;   // +0x3d4 o último passo com o objetivo à vista
    int32_t unknown3d8[2];         // +0x3d8
    int32_t last_damage_step;      // +0x3e0
    int32_t magic_counters[64];    // +0x3e4
    int32_t magic_thresholds[64];  // +0x4e4 (ClearState: -1, salvo 6, 7, 10 = 6; 8 = 3; 9 = 2; 0x13 = 4)
    int32_t stylin;                // +0x5e4 os AddScore de tipo 1
    int32_t end_step;              // +0x5e8 o passo do fim (Win)
    int32_t unknown5ec;            // +0x5ec (ClearState: 1 com 0x32e690)
    int32_t last_shot_step;        // +0x5f0
    int32_t loads;                 // +0x5f4 as cargas (Load)
    uint32_t objective_target[2];  // +0x5f8 (p32) a posição, a instância ou o elemento da IA
    int32_t objective_kind[2];     // +0x600 0 nenhum, 1 posição, 2 instância, 3 elemento da IA
    uint32_t objective_comment[2]; // +0x608
    int32_t objective_visible[2];  // +0x610
    int32_t uber_state[12];        // +0x618 do UberBond +0x504 (o que atravessa as execuções)
    int32_t scores[12];            // +0x648 AddScore (por tipo)
    int32_t objective_check_step;  // +0x678
    MissionTimedAction timed[8];   // +0x67c
    MissionSectionProfile profiles[64];  // +0x73c
    int32_t unknown1b3c;           // +0x1b3c
    int32_t current_profile;       // +0x1b40 -1
    uint32_t car_type;             // +0x1b44 (p32 char)
    uint8_t unknown1b48[8];
};
static_assert(sizeof(SMissionManagerData) == 0x1b50);
static_assert(offsetof(SMissionManagerData, timers) == 0x150 && offsetof(SMissionManagerData, state) == 0x34c &&
              offsetof(SMissionManagerData, player_health) == 0x378 &&
              offsetof(SMissionManagerData, difficulty) == 0x3b8 &&
              offsetof(SMissionManagerData, magic_counters) == 0x3e4 &&
              offsetof(SMissionManagerData, stylin) == 0x5e4 &&
              offsetof(SMissionManagerData, objective_target) == 0x5f8 &&
              offsetof(SMissionManagerData, uber_state) == 0x618 &&
              offsetof(SMissionManagerData, timed) == 0x67c && offsetof(SMissionManagerData, profiles) == 0x73c &&
              offsetof(SMissionManagerData, car_type) == 0x1b44);

// Uma regra (a imagem do objeto do PS2: no máximo 0x28 bytes, SRuleRange/SRuleSpeed/SRuleHealth). A tabela virtual
// (+0x10) diz a classe (kRuleVtable); os campos +0x14.. são os da classe.
struct SMissionRuleData {
    uint32_t events;   // +0x00 (p32 TriggerEvents) a lista 'el'
    int32_t active;    // +0x04 o bit 0 das bandeiras do 'Rule'
    int32_t once;      // +0x08 o bit 1: desliga depois de disparar
    int32_t invert;    // +0x0c o bit 2: dispara quando CheckRule é falso
    uint32_t vtable;   // +0x10 a do PS2 (0x397be8 SRuleAlways...)
    uint32_t w[5];     // +0x14 os campos da classe (o SMissionRuleTarget de 8 bytes começa aqui nas que têm alvo)
    int32_t type;      // port: o tipo do 'Rule' (não faz parte do objeto do PS2)
    uint32_t size;     // port: o tamanho do objeto do PS2
};

// O SMissionObjective (0x14 bytes) e a mensagem (0xc).
struct SMissionObjective {
    int32_t id;        // +0x00 o texto (um rótulo do .loc)
    int32_t text;      // +0x04
    int32_t score;     // +0x08 (PassObjective: AddScore(0, score))
    int32_t arg;       // +0x0c
    int32_t state;     // +0x10 0 incompleto, 1 corrente, 2 cumprido, 3 fracassado
};
struct SMissionMessage {
    int32_t id, a, b;
};

// As listas do port (o que o PS2 guarda em listas encadeadas): [0] a de sempre, [1 + etapa] as das etapas.
struct MissionLists {
    std::vector<SMissionRuleData*> rules[65];
    std::vector<SMissionObjective*> objectives;
    std::vector<SMissionMessage*> messages;
};

// O que o port conta (o relatório e a conferência).
struct MissionStats {
    uint64_t rule_checks = 0, rules_fired = 0, events_created = 0;
    uint64_t ai_queries = 0;         // as perguntas a elementos da IA (mission_ai())
    uint64_t unported_rules = 0;     // SRulePlayerDir, SRuleAmmo
    uint64_t external_calls = 0;     // as chamadas a sistemas não portados (mission_trace())
    uint64_t guards = 0;
    uint64_t restarts = 0;           // os ERestart do Update e do EPlayerLose (o "dia da marmota")
};
MissionStats& mission_stats();

// Uma chamada que o gerente faz a um sistema não portado (a função do PS2 e os argumentos). Só anotada com
// mission_trace_enabled() (a parte 15 do nf_driving_check compara com as chamadas do original).
struct MissionCall {
    uint32_t fn;
    uint32_t args[4];
};
std::vector<MissionCall>& mission_trace();
bool& mission_trace_enabled();
void mission_note_call(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0);

// ---------------------------------------------------------------- os elementos da IA (não portados)

// O que o AIElementController responderia de um elemento (o registro do 'AIEl'): o AIElementTracking do PS2 (+0x20 o
// objeto com a vida em +0x4c, +0x30 a posição, +0x3c a velocidade). Port: vivos, parados, sem propriedade.
struct MissionAIElement {
    uint32_t element = 0;   // (p32) o registro do 'AIEl'
    float health = 1000.0f;
    float pos[3] = {0, 0, 0};
    float speed = 0.0f;
    bool active = false;    // EActivateAIElement / EDeactivateAIElement
    bool killed = false;    // EKillAIElement (port: a vida vai a 0)
    int32_t commands = 0;   // os EAICommand recebidos
};
struct MissionAI {
    std::vector<MissionAIElement> elements;
    float stats[8] = {};    // AIElementController::GetStats (as médias de todos; port: zeros)
    MissionAIElement* find(uint32_t element);   // nulo: não é um registro do 'AIEl' conhecido
    MissionAIElement& get(uint32_t element);    // cria se não houver (vida 1000)
};
MissionAI& mission_ai();

// ---------------------------------------------------------------- o que a missão lê de fora

class Scheduler;

// O resto do jogo que o gerente consulta e o port tem em outros módulos (o nf_driving liga; a conferência também).
struct MissionEnv {
    Scheduler* scheduler = nullptr;   // Scheduler::fgScheduler (+0x20 o modo de cinema, +0x24 o timeScale)
    float camera_pos[3] = {0, 0, 0};  // a câmera do jogador (o RPlayerCamera de 0x333ed8 +4, +0x30)
    // WCollisionMgr::StepCheckHitWorld(de, até, passo) (0x216500, não portado): verdadeiro se o mundo corta o segmento.
    // Sem ele (nulo), nada corta.
    bool (*step_check_hit_world)(const float from[4], const float to[4], float step) = nullptr;
    int32_t sound_cinematic_mode = 0; // ASoundManager::fCinematicMode (0x355e5c)
    int32_t io_updating = 0;          // IOModule +4 (o EStartMission liga)
    // O SWeaponManager (não portado): o que o GetProperty do jogador lê (+0x10 → +0x14c o turbo, +0x4e8 o EMP).
    int32_t weapon_boost = 0, weapon_emp = 0;
    // Os pedidos de ApplyDamage ao carro do jogador (o EDamagePlayer; PBondCar::ApplyDamage não foi portado: conta).
    uint64_t player_damage_requests = 0;
    float player_damage_requested = 0;
    // O SetAutoDrive com a entrada (seção 23): IOModule::ReleaseAllButtons e InputConfigManager::SetConfig(tipo, -1) (1:
    // a configuração do POV, a da mira do AutoDrive; 0: a do carro). Quem tem o IOModule liga (o nf_driving).
    std::function<void(int32_t config_type)> auto_drive_input;
};
MissionEnv& mission_env();

// ---------------------------------------------------------------- as funções

SMissionManagerData& mission_manager();
MissionLists& mission_lists();
// O gerente existe (WWorld::Open o construiu)?
bool mission_exists();
// Port: esquece o gerente, as listas e a IA (o fim de uma pista).
void mission_reset();

// [driving] 0x00205188 (sem nome: SMissionManager::Construct) e 0x00204de8 SMissionManager::SMissionManager: o gerente
// novo. `accumulate_scores`: o ACCUMULATE_SCORES da coleção do mundo (0: ClearScoreBuffer no bloco da troca).
void SMissionManager_Construct(bool accumulate_scores);
// [driving] 0x00204d18 SMissionManager::SetCarType
void SMissionManager_SetCarType(SMissionManagerData& m);
// [driving] 0x002051f8 SMissionManager::GetCarType
const char* SMissionManager_GetCarType(const SMissionManagerData& m);
// [driving] 0x00205220 SMissionManager::Load(UData *, UData *)
void SMissionManager_Load(SMissionManagerData& m, UData* rules, UData* mset);
// [driving] 0x00205788 SMissionManager::Unload
void SMissionManager_Unload(SMissionManagerData& m);
// [driving] 0x00205498 SMissionManager::LoadWeaponSet (as chamadas ao SWeaponManager só anotadas)
void SMissionManager_LoadWeaponSet(SMissionManagerData& m);
// [driving] 0x002057f0 SMissionManager::ClearState
void SMissionManager_ClearState(SMissionManagerData& m);
// [driving] 0x00205a38 SMissionManager::Reset
void SMissionManager_Reset(SMissionManagerData& m);
// [driving] 0x00205ac8 SMissionManager::Start / 0x00205ae8 Stop
void SMissionManager_Start(SMissionManagerData& m);
void SMissionManager_Stop(SMissionManagerData& m);
// [driving] 0x00205b08 SMissionManager::Win / 0x00205bf0 Lose
void SMissionManager_Win(SMissionManagerData& m);
void SMissionManager_Lose(SMissionManagerData& m);
// [driving] 0x00206108 SMissionManager::RestoreLives
void SMissionManager_RestoreLives(SMissionManagerData& m);
// [driving] 0x00206168 SMissionManager::ClearScoreBuffer(UberBond &)
void SMissionManager_ClearScoreBuffer(UberBondBlock& uber);
// [driving] 0x00206228 SMissionManager::ProcessRuleList / 0x00206340 ProcessRules
void SMissionManager_ProcessRuleList(SMissionManagerData& m, int list);
void SMissionManager_ProcessRules(SMissionManagerData& m);
// [driving] 0x00206338 SMissionManager::ChangeStage / 0x00206310 (sem nome: CallStage)
void SMissionManager_ChangeStage(SMissionManagerData& m, int32_t stage);
void SMissionManager_CallStage(SMissionManagerData& m, int32_t stage);
// [driving] 0x00206b88 SMissionManager::Update
void SMissionManager_Update(SMissionManagerData& m);
// [driving] 0x00207228 UpdateCharInfo / 0x00207ab0 UpdateObjectiveSpecial / 0x00207500 UpdateTimedActions /
// 0x00208eb0 UpdateSectionProfile / 0x00206ca0 UpdateAutoDriveMissile
void SMissionManager_UpdateCharInfo(SMissionManagerData& m);
void SMissionManager_UpdateObjectiveSpecial(SMissionManagerData& m);
void SMissionManager_UpdateTimedActions(SMissionManagerData& m);
void SMissionManager_UpdateSectionProfile(SMissionManagerData& m);
void SMissionManager_UpdateAutoDriveMissile(SMissionManagerData& m);
// [driving] 0x00206ec0 SMissionManager::ResetCharInfo
void SMissionManager_ResetCharInfo(SMissionManagerData& m);
// O modo de cinema: [driving] 0x002065d0 SetCinematicMode, 0x00206620 ClearCinematicMode, 0x002066d0 SkipCinematic,
// 0x00206788 GoingToSkipCinematic, 0x002067a8 IsSkippingCinematic
void SMissionManager_SetCinematicMode(SMissionManagerData& m, uint32_t mode);
void SMissionManager_ClearCinematicMode(SMissionManagerData& m, uint32_t mode);
void SMissionManager_SkipCinematic(SMissionManagerData& m);
bool SMissionManager_GoingToSkipCinematic(const SMissionManagerData& m);
bool SMissionManager_IsSkippingCinematic(const SMissionManagerData& m);
// [driving] 0x00206398 SMissionManager::SetAutoDrive(bool, bool) (a entrada, a mira e as mãos só anotadas: a direção
// automática do carro do jogador pela IA não foi portada) / 0x002064d8 SetDriveMissile(Missile *) (a câmera do
// míssil e o RColorize anotados)
void SMissionManager_SetAutoDrive(SMissionManagerData& m, int32_t on, bool reset_input);
void SMissionManager_SetDriveMissile(SMissionManagerData& m, uint32_t missile);
// A vida do jogador: [driving] 0x00206b20 GetPlayerHealth, 0x00206b40 GetPlayerShield, 0x00206810 BoostPlayerHealth,
// 0x00206850 BoostPlayerShield, 0x002068b8 IncPlayerDamage
float SMissionManager_GetPlayerHealth(const SMissionManagerData& m, bool fraction);
float SMissionManager_GetPlayerShield(const SMissionManagerData& m, bool fraction);
void SMissionManager_BoostPlayerHealth(SMissionManagerData& m, float amount);
void SMissionManager_BoostPlayerShield(SMissionManagerData& m, float amount);
void SMissionManager_IncPlayerDamage(SMissionManagerData& m, float damage);
// As estatísticas: [driving] 0x002068e0 IncShotsFired, 0x00206900 IncShotsHit, 0x00206960 IncKills, 0x00207ea8 AddScore,
// 0x002083e8 GetScore, 0x002083f8 GetScoreTotal
void SMissionManager_IncShotsFired(SMissionManagerData& m, int32_t n);
void SMissionManager_IncShotsHit(SMissionManagerData& m, bool check_rate);
void SMissionManager_IncKills(SMissionManagerData& m, int32_t n);
void SMissionManager_AddScore(SMissionManagerData& m, int32_t kind, int32_t points);
int32_t SMissionManager_GetScore(const SMissionManagerData& m, int32_t kind);
int32_t SMissionManager_GetScoreTotal(const SMissionManagerData& m);
// Os contadores mágicos e os eventos do programador: [driving] 0x00206980 GetMagicCounter, 0x00206990 IncMagicCounter,
// 0x00206ad0 SetMagicCounter, 0x00206b00 SetMagicCounterThreshold, 0x00208540 ProgrammerDefinedEvent, 0x00208558
// CheckProgrammerEvent, 0x00208568 ClearProgrammerEvent
int32_t SMissionManager_GetMagicCounter(const SMissionManagerData& m, int32_t i);
void SMissionManager_IncMagicCounter(SMissionManagerData& m, int32_t i);
void SMissionManager_SetMagicCounter(SMissionManagerData& m, int32_t i, int32_t value);
void SMissionManager_SetMagicCounterThreshold(SMissionManagerData& m, int32_t i, int32_t value);
void SMissionManager_ProgrammerDefinedEvent(SMissionManagerData& m, int32_t i);
int32_t SMissionManager_CheckProgrammerEvent(const SMissionManagerData& m, int32_t i);
void SMissionManager_ClearProgrammerEvent(SMissionManagerData& m, int32_t i);
// Os objetivos: [driving] 0x00208578 AddObjective, 0x002086e0 InsertObjective, 0x002088b8 PassObjective, 0x00208918
// FailObjective, 0x00208968 SetCurrentObjective, 0x002089b8 SetIncompleteObjective, 0x00208a00 GetObjectiveByID,
// 0x00208a48 GetObjectiveByIndex, 0x00208b40 ClearObjectiveList; o alvo do objetivo: 0x002075a8 SetObjective (posição),
// 0x00207600 (instância), 0x00207658 (elemento da IA), 0x00207730 ClearObjective, 0x00207720 SetObjectiveComment,
// 0x00207748 GetCurrObjective, 0x00207968 CheckObjectiveVisibleToUser
void SMissionManager_AddObjective(SMissionManagerData& m, int32_t id, int32_t text, int32_t score, int32_t arg);
void SMissionManager_InsertObjective(SMissionManagerData& m, int32_t before, int32_t id, int32_t text, int32_t score,
                                     int32_t arg);
void SMissionManager_PassObjective(SMissionManagerData& m, int32_t id);
void SMissionManager_FailObjective(SMissionManagerData& m, int32_t id);
void SMissionManager_SetCurrentObjective(SMissionManagerData& m, int32_t id);
void SMissionManager_SetIncompleteObjective(SMissionManagerData& m, int32_t id);
SMissionObjective* SMissionManager_GetObjectiveByID(const SMissionManagerData& m, int32_t id);
SMissionObjective* SMissionManager_GetObjectiveByIndex(const SMissionManagerData& m, int32_t index);
void SMissionManager_ClearObjectiveList(SMissionManagerData& m);
void SMissionManager_SetObjectivePos(SMissionManagerData& m, int32_t slot, uint32_t pos);
void SMissionManager_SetObjectiveInstance(SMissionManagerData& m, int32_t slot, uint32_t instance);
void SMissionManager_SetObjectiveElement(SMissionManagerData& m, int32_t slot, uint32_t element);
void SMissionManager_ClearObjective(SMissionManagerData& m, int32_t slot);
void SMissionManager_SetObjectiveComment(SMissionManagerData& m, int32_t slot, uint32_t comment);
// O ponto (3 floats) do objetivo `slot` (nulo: nenhum). Os elementos da IA: o ponto de mission_ai().
const float* SMissionManager_GetCurrObjective(SMissionManagerData& m, int32_t slot);
bool SMissionManager_CheckObjectiveVisibleToUser(SMissionManagerData& m, int32_t slot);
// As mensagens: [driving] 0x00208bb8 AddMessage, 0x00208d20 ClearMessageList
void SMissionManager_AddMessage(SMissionManagerData& m, int32_t id, int32_t a, int32_t b);
void SMissionManager_ClearMessageList(SMissionManagerData& m);
// As ações temporizadas: [driving] 0x00207378 StartTimedAction, 0x002073d0 CompleteTimedAction
void SMissionManager_StartTimedAction(SMissionManagerData& m, int32_t i, float seconds);
void SMissionManager_CompleteTimedAction(SMissionManagerData& m, int32_t i);

// O cronômetro: [driving] 0x00209bb0 SMissionTimer::SMissionTimer, 0x00209bf0 Reset, 0x00209c08 Start, 0x00209c38 Stop,
// 0x00209c78 Increment, 0x00209c88 GetValue, 0x00209d40 IsActive, 0x00209d48 IsReverse, 0x00209d58 SetTicking
void SMissionTimer_Construct(SMissionTimer& t);
void SMissionTimer_Reset(SMissionTimer& t, int32_t value);
void SMissionTimer_Start(SMissionTimer& t, int32_t rate);
void SMissionTimer_Stop(SMissionTimer& t);
void SMissionTimer_Increment(SMissionTimer& t, int32_t n);
int32_t SMissionTimer_GetValue(SMissionTimer& t);
bool SMissionTimer_IsReverse(const SMissionTimer& t);
// O cronômetro `i` do gerente (nulo fora dos 10: guarda).
SMissionTimer* mission_timer(SMissionManagerData& m, int32_t i);

// As regras: [driving] 0x00209390 SMissionRule::Instantiate (e os construtores das 15 classes), 0x002095f8 RunEvents,
// 0x002096d0 DoRule; a CheckRule de cada classe.
SMissionRuleData* SMissionRule_Instantiate(const uint8_t* rule);
void SMissionRule_RunEvents(SMissionRuleData& r);
void SMissionRule_DoRule(SMissionRuleData& r);
bool SMissionRule_CheckRule(SMissionRuleData& r);
// As tabelas virtuais das classes (as do PS2), pelo tipo do 'Rule' (0..14).
extern const uint32_t kRuleVtable[15];
extern const uint32_t kRuleSize[15];
extern const char* const kRuleName[15];

// O alvo de uma regra (SMissionRuleTarget, 8 bytes: o tipo e o elemento): [driving] 0x00209758 GetSpeed, 0x00209820
// GetHealth, 0x002098a0 GetPos, 0x00209aa0 GetProperty.
int32_t SMissionRuleTarget_GetSpeed(const uint32_t target[2]);
int32_t SMissionRuleTarget_GetHealth(const uint32_t target[2]);
void SMissionRuleTarget_GetPos(const uint32_t target[2], float out[3]);
bool SMissionRuleTarget_GetProperty(const uint32_t target[2], const char* name);

// O carro do jogador (o primeiro da Simulation; nulo sem ele).
BondCar* mission_player_car();

}  // namespace drv
