#pragma once
// A missão (módulo Mission_* do ACTION.ELF, 0x00182270-0x001830e0): os objetivos de cada fase, o
// sucesso e o fracasso e o fim da fase.
//
// MissionData (24 entradas de 0x28 bytes, copiada do ELF na partida por game/hud_tables.cpp) liga
// cada pacote de fase à sua missão (BaseMap: o pacote da primeira fase), à ordem dentro dela e aos
// objetivos (OBJECTIVE_tag). Mission_Init (de ResetMap_GameInit) acha a missão da fase e reinicia os
// objetivos desta fase e das seguintes (os das anteriores ficam, para a missão de várias fases). A
// cada tick Mission_Update (de Game_Run):
// - canal 0x62 (o jogador morreu: Player_HandleDeath, quando a cortina de sangue cobre a tela ou ele
//   aperta Start) -> missão fracassada (estado 3), vida zerada, 1 s, menu de fim de missão;
// - Mission_MonitorObjectives anuncia cada objetivo (Text_AddMsg) quando a fase dele chega ou o
//   canal que o revela liga; um objetivo comum se cumpre quando o canal dele liga (estado 4), um
//   "não deixe acontecer" (flag 2) fracassa quando o canal liga (estado 5) e liga o canal 99;
//   os canais 0x60 e 0x61, e o 0x7b em 07000016 e o 0xc7 em 0700001b, também ligam o 99 com um
//   rótulo de falha; todos os objetivos da missão satisfeitos ligam o canal 100;
// - canal 99 -> missão fracassada (estado 2), mensagem, 5 s, menu de fim de missão;
// - canal 100 -> missão cumprida (estado 6), mensagem, 4 s, e a fase de LevelToEndTo (o menu,
//   07000048, ou o filme do fim da missão, 073xxxxx, que volta ao menu).
//
// O menu de fim de missão (P_ENDMISSION_Handler, engine/menu_handlers.cpp) e o resultado depois de
// uma missão cumprida (P_NFRESULTS_Handler, no menu principal 07000048) são os do jogo; o resultado
// continua no mapa das missões (P_NFMAP_Handler, engine/menu_frontend.h), com a missão seguinte liberada.

#include <cstddef>
#include <cstdint>

// Um objetivo (0x18 bytes).
struct OBJECTIVE_tag {
    uint32_t label;         // 0x00 texto do objetivo
    uint32_t fail_label;    // 0x04 texto da falha (0xffffffff nenhum)
    uint32_t text;          // 0x08 descrição (OBJ_STATE +0xc)
    uint8_t channel;        // 0x0c canal que cumpre (ou, com a flag 2, faz fracassar) o objetivo
    uint8_t channel_init;   // 0x0d valor do canal ao (re)começar a fase
    uint8_t unknown_0e;
    uint8_t trigger;        // 0x0f canal que revela o objetivo (0: revelado ao chegar à fase)
    uint8_t flags;          // 0x10 2: "não deixe acontecer"
    uint8_t pad_11[3];
    int32_t state;          // 0x14 0 escondido, 1 esperando o canal, 2/3 ativo (com/sem flag 2),
                            //      4 cumprido, 5 fracassado
};
static_assert(sizeof(OBJECTIVE_tag) == 0x18);

// Uma fase de missão (0x28 bytes).
struct MISSIONDATA_tag {
    uint32_t level;               // 0x00 pacote da fase
    uint32_t base_map;            // 0x04 pacote da primeira fase da missão
    uint32_t order;               // 0x08 ordem da fase na missão
    OBJECTIVE_tag* objectives;    // 0x0c
    uint32_t count;               // 0x10
    uint32_t unknown_14[5];
};
static_assert(sizeof(MISSIONDATA_tag) == 0x28);

// Um objetivo visível (Mission_ObjectiveState, para o menu de pausa).
struct OBJ_STATE {
    int32_t state;          // 0x00 OBJECTIVE_tag::state
    uint32_t status_label;  // 0x04 2 -> 0x2000001, 3 -> 0x2000000, 4 -> 0x16f, 5 -> 0x170
    uint32_t label;         // 0x08
    uint32_t text;          // 0x0c
};
static_assert(sizeof(OBJ_STATE) == 0x10);

// MISSION_STATE (Mission_Status): PlrStat_OkToUpdate só deixa a vida mudar abaixo de 2.
enum : uint32_t {
    kMissionRunning = 1,
    kMissionFailed = 2,     // objetivo fracassado (canal 99)
    kMissionDied = 3,       // o jogador morreu (canal 0x62)
    kMissionNone = 5,       // fase sem missão (multijogador, testes)
    kMissionComplete = 6,   // todos os objetivos (canal 100)
};

constexpr uint32_t kMissionCount = 24;
constexpr uint32_t kMissionObjectivePool = 64;
constexpr uint32_t kNoLabel = 0xffffffffu;  // &_heap_size no original

extern MISSIONDATA_tag MissionData[kMissionCount];
extern OBJECTIVE_tag MissionObjectives[kMissionObjectivePool];  // os objetivos (copiados do ELF)
extern uint32_t InternalState;  // passo de Mission_Update
extern uint32_t MissionState;
extern uint32_t BaseMap;
extern uint32_t ThisOrderNum;
extern uint32_t FailLabel;
extern uint32_t LevelToEndTo;
// Missões do menu (sp_level do módulo Menu, 12 x 0x18 bytes; o pacote em +0xc), copiadas do ELF
// para o substituto da escolha de missão.
extern uint8_t sp_level[12 * 0x18];

void Mission_Init(uint32_t level, uint16_t warm);
int16_t Mission_NumVisObjectives();
void Mission_ObjectiveState(OBJ_STATE* out, uint16_t index);
void Mission_MonitorObjectives();
void Mission_Update();
void Mission_SetFailLabel(uint32_t label);
uint32_t Mission_BaseMapHCode();
void Mission_SetMapHCode(uint32_t hash);
uint32_t Mission_FailLabel();
int32_t Mission_Status();
void Mission_SetStatus(uint32_t state);

// ---- só no port ----
struct MissionPort {
    uint32_t restarts = 0;          // a mesma fase pedida de novo (ResetMap_LevelToLoad: os recomeços dos menus)
};
extern MissionPort g_mission_port;
// Os objetivos da missão atual: cumpridos, fracassados, visíveis e o total.
struct MissionSummary {
    int32_t total = 0;
    int32_t done = 0;
    int32_t failed = 0;
    int32_t visible = 0;
};
MissionSummary Mission_Summary();
// "objetivos 1/3 fracassados 0 visiveis 2 missao 1 passo 1 base 07000001 ordem 0 falha ffffffff".
int Mission_Describe(char* out, size_t size);
// Estado inicial das globais do módulo (testes).
void mission_debug_power_on();
