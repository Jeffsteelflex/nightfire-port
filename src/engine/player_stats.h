#pragma once
// As estatísticas do jogador e a pontuação da missão (módulo PlrStat_* / PlrStats_* do ACTION.ELF,
// 0x00198318-0x00199b18). Ver docs/engine.md, "Estatísticas e pontuação".
//
// Os contadores: PlrMissionStats (8 jogadores x 0x38 bytes, PLRMISSIONSTATS) e BondMoments (um por canal
// 0x65-0x6e). PlrStat_ResetForMission os zera no começo de cada missão (Mission_Init, na primeira fase dela;
// PlrStat_Init na partida); as funções do jogo registram:
// - inimigos que viram o jogador (DroneVision_LogSeenOpponent, uma vez por personagem), gerados
//   (NDrone2_DefaultInit), abatidos pelo jogador (DroneFunc_OnInitDeath, NDrone2_LogDroneDeath), desacordados
//   (KnockedOut_Anim) e rendidos (Surrender_Anim);
// - tiros disparados (Player_WeaponInitBullet, armas com weapon_definition_tag::stats & 0xf8) e os que
//   acertaram um inimigo (NDrone2_HitDamage, ExplosiveImpact, DroneFunc_HandleImpact: nunca mais que os
//   disparados); um tiro num alvo de cenário (Destroy_Smash, SP_Update) desconta um disparo;
// - a vida (Player_SetHealth), os momentos Bond (Trigger_Calc: o primeiro de cada canal 0x65-0x6e acende
//   BLData::bond_moment, o 007 girando no HUD), as fichas "007 Bonus" pegas (Pickup_Handler);
// - o tempo da missão em centésimos (psiGetTimeIn100ths, tempo real como no PS2): Player_Init religa o
//   cronômetro, Player_Update soma, GS_PauseGame e os scripts de cena (eventos de pausa) o param.
// Só contam com a missão em andamento (Mission_Status 0 ou 1) ou no multijogador (PlrStat_OkToUpdate); o
// tempo e os gerados contam sempre.
//
// A pontuação: ScoringTable (12 missões, SCORETABLE, copiada do ELF na partida com as linhas de cada uma,
// HT_Level_*_ST, e ScoringTableDifMul) tem, por missão (o BaseMap), os quatro limites de medalha, a
// penalidade por detecção e 9 linhas (alvo, pontos, valor, fração, pontos ganhos): 0 momentos Bond,
// 1 inimigos abatidos, 2 desacordados, 3 rendidos, 4 detecções (alvo = quantas podem), 5 precisão (o alvo
// é 100: acertos x 100 / disparos, 100 sem disparos), 6 vida (0 se morreu), 7 tempo (alvo em segundos,
// valor em centésimos), 8 fichas 007. Missões 09xxxxxx são as de direção: as linhas vêm de NewScoresRef
// (o DRIVING.ELF, não portado). PlrStat_GetScore (ver o comentário da função) calcula os pontos de cada
// linha, a soma, o bônus da dificuldade (soma x 0/1/2 em Operative/Agent/00 Agent) e o total; a medalha é a
// do resultado (P_NFRESULTS_Handler: o maior limite que o total alcança). Com a missão sem estar cumprida
// (estado diferente de 6) os pontos ficam zerados.
//
// Tudo fica em memória estática (.data/.bss no PS2): sobrevive ao ResetMap_Load/Mem_Init da troca de fase e
// chega ao menu principal (07000048), onde o resultado (0x40000036) e as estatísticas (0x40000037) leem a
// pontuação da missão em BaseMap (Mission_Init não mexe nele nem no estado ao carregar o menu).
//
// Divergências (comentadas nas funções): PlrStat_DoneBondMoment não escreve num canal fora de 0x65-0x6e nem
// num jogador sem BLData (o original escreveria fora de BondMoments e por nulo); contam em
// g_player_stats_guards.

#include <cstddef>
#include <cstdint>
#include "platform/ptr32.h"

// Estatísticas da missão de um jogador (0x38 bytes; PlrMissionStats em 0x002bcc20).
struct PLRMISSIONSTATS {
    uint32_t detected;      // 0x00 inimigos que viram o jogador (PlrStat_LogEnemyDetectedPlayer)
    int32_t shots_fired;    // 0x04 tiros disparados (PlrStat_LogShotFired; PlrStat_LogShotHitScenery desconta)
    int32_t shots_hit;      // 0x08 tiros que acertaram um inimigo (nunca acima de shots_fired)
    uint32_t dispatched;    // 0x0c inimigos abatidos pelo jogador
    uint32_t disabled;      // 0x10 inimigos desacordados
    uint32_t surrendered;   // 0x14 inimigos rendidos
    uint32_t spawned;       // 0x18 inimigos criados (só o jogador 0: PlrStat_LogEnemySpawned)
    float health;           // 0x1c a última vida (PlrStat_LogHealth)
    uint32_t bond_moments;  // 0x20 momentos Bond
    uint32_t bonus;         // 0x24 fichas "007 Bonus" pegas (só o jogador 0)
    uint32_t unknown_28;    // 0x28 (nada do módulo escreve)
    int32_t timer_last;     // 0x2c psiGetTimeIn100ths da última soma
    int32_t elapsed;        // 0x30 tempo da missão em centésimos de segundo
    uint8_t timer_paused;   // 0x34 1: o cronômetro está parado
    uint8_t pad_35[3];
};
static_assert(sizeof(PLRMISSIONSTATS) == 0x38);

// Uma linha da pontuação (0x14 bytes).
struct SCORETABLE_ROW {
    uint32_t target;   // 0x00 alvo (0: a linha não conta)
    uint32_t points;   // 0x04 pontos do alvo alcançado
    uint32_t actual;   // 0x08 o valor da missão
    float fraction;    // 0x0c quanto do alvo (0..1, em centésimos)
    float earned;      // 0x10 pontos ganhos (inteiros)
};
static_assert(sizeof(SCORETABLE_ROW) == 0x14);

// As linhas de uma missão no ELF (HT_Level_*_ST: 9 linhas, 0xb4 bytes, alinhadas a 8: uma a cada 0xb8).
struct SCORETABLE_ROWS {
    SCORETABLE_ROW row[9];
    uint32_t pad_b4;
};
static_assert(sizeof(SCORETABLE_ROWS) == 0xb8);

// A pontuação de uma missão (0x3c bytes; ScoringTable em 0x002bc640, 12 entradas).
struct SCORETABLE {
    uint32_t level;          // 0x00 a missão (o pacote da primeira fase, Mission_BaseMapHCode)
    uint32_t medal[4];       // 0x04 os totais da medalha de bronze, prata, ouro e platina
    float detect_penalty;    // 0x14 fração perdida por detecção acima do alvo (0: 0,03)
    P32<SCORETABLE_ROW> rows;  // 0x18 as 9 linhas (HT_Level_*_ST)
    uint32_t time;           // 0x1c o tempo da missão (centésimos)
    uint32_t time_target;    // 0x20 o alvo de tempo (segundos)
    uint32_t difficulty_mul; // 0x24 multiplicador do bônus da dificuldade
    uint32_t bonus;          // 0x28 bônus da dificuldade (soma x multiplicador)
    uint32_t score;          // 0x2c soma dos pontos das linhas
    uint32_t best;           // 0x30 o recorde (o maior total)
    uint32_t total;          // 0x34 soma + bônus
    uint8_t on_foot;         // 0x38 1: missão a pé (as linhas vêm de PlrMissionStats); 0: direção (09xxxxxx)
    uint8_t pad_39[3];
};
static_assert(sizeof(SCORETABLE) == 0x3c);

// As linhas da pontuação.
enum : uint32_t {
    kScoreBondMoments = 0,
    kScoreDispatched = 1,
    kScoreDisabled = 2,
    kScoreSurrendered = 3,
    kScoreDetected = 4,
    kScoreAccuracy = 5,
    kScoreHealth = 6,
    kScoreTime = 7,
    kScoreBonus = 8,
    kScoreRows = 9,
};

constexpr uint32_t kScoringTableCount = 12;
constexpr uint32_t kPlrStatPlayers = 8;
constexpr uint32_t kBondMomentFirst = 0x65;  // canais dos momentos Bond: 0x65-0x6e
constexpr uint32_t kBondMomentCount = 10;

extern PLRMISSIONSTATS PlrMissionStats[kPlrStatPlayers];
extern uint16_t BondMoments[kBondMomentCount];
extern SCORETABLE ScoringTable[kScoringTableCount];
extern SCORETABLE_ROWS ScoringRows[kScoringTableCount];  // HT_Level_Driving_Paris_ST..SpaceStationD_ST
extern float ScoringTableDifMul[3];      // Operative, Agent, 00 Agent
extern int32_t* NewScoresRef;            // 0x0030cc6c: 9 x (valor, alvo, pontos) da direção; nulo no port
extern uint8_t DoneBetter;               // 0x0030cc70 (engine/menu_handlers.cpp): o total bateu o recorde

// [decomp] o módulo.
void PlrStat_Init();
void PlrStat_ResetForMission();
bool PlrStat_OkToUpdate();
void PlrStat_LogShotHitScenery(uint32_t player);
uint8_t* PlrStat_GetScore(uint8_t peek);  // um SCORETABLE*; nulo fora da tabela (ou direção sem NewScoresRef)
void PlrStats_ResetScoring();
void PlrStats_SetupScoreTable(SCORETABLE* table, uint32_t count);
void PlrStat_LogEnemySpawned();
void PlrStat_DoneBondMoment(uint32_t player, uint16_t channel);
void PlarStat_LogTimerPause(uint32_t player);
void PlarStat_LogTimerUnpause(uint32_t player);
void PlarStat_LogUpdateElapsedTime(uint32_t player);
void PlrStat_LogHealth(float health, uint32_t player);
void PlrStat_LogEnemyDispatched(uint32_t player);
void PlrStat_LogEnemyDisabled(uint32_t player);
void PlrStat_LogEnemySurrender(uint32_t player);
void PlrStat_LogShotHitEnemy(uint32_t player);
void PlrStat_LogShotFired(uint32_t player);
void PlrStat_LogEnemyDetectedPlayer(uint32_t player);
void PlrStat_LogBondBonus();
bool PlrStats_HasGoldMedal();
SCORETABLE* PlrStats_GetLevelTotals(uint32_t level);
SCORETABLE* PlrStats_GetScoreTable(uint32_t* count);
void PlrStats_GetDrivingData(uint32_t* best, uint32_t* first);
void PlrStats_SetDrivingData(uint32_t* best, uint32_t* first);

// Só do port.
// A medalha de um total como no resultado (P_NFRESULTS_Handler): 4 platina, 3 ouro, 2 prata, 1 bronze, 0
// nenhuma (também com total 0).
uint32_t PlrStat_MedalFor(const SCORETABLE* table, uint32_t total);
// A entrada da missão em BaseMap, sem calcular nada (nulo fora da tabela).
const SCORETABLE* PlrStat_Current();
// A linha do relatório das capturas e do nf_loader_check: os contadores do jogador 0 e a pontuação guardada
// da missão (soma, bônus, total, recorde, medalha do total).
int PlrStat_Describe(char* out, size_t size);

struct PlayerStatsGuards {
    uint32_t bond_channel = 0;   // canal de momento Bond fora de 0x65-0x6e
    uint32_t bond_player = 0;    // jogador sem BLData (ou acima de 3) num momento Bond
};
extern PlayerStatsGuards g_player_stats_guards;

void player_stats_debug_power_on();
