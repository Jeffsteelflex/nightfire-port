// As estatísticas do jogador e a pontuação da missão. Ver player_stats.h.
//
// Para dar os mesmos pontos do PS2, PlrStat_GetScore calcula com o arredondamento da FPU do EE (para zero; a
// do PC arredonda para o mais próximo e o "teto" com tolerância de 1e-5 vê a diferença do último bit: 0,7 x 400
// daria 281). O modo volta ao sair.
//
// Divergências:
// - PlrStat_DoneBondMoment: um canal fora de 0x65-0x6e ou um jogador sem BLData (ou acima de 3: glb_blokes
//   tem 4) não escreve (o original escreveria fora de BondMoments ou por nulo); conta em g_player_stats_guards.
// - PlrStat_GetScore: sem as tabelas do ELF (os testes sem dados) a entrada não tem linhas e devolve nulo.

#include "engine/player_stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/gamestate.h"
#include "engine/menu.h"
#include "engine/mission.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "platform/compiler.h"
#include "platform/psi.h"

PLRMISSIONSTATS PlrMissionStats[kPlrStatPlayers];
uint16_t BondMoments[kBondMomentCount];
SCORETABLE ScoringTable[kScoringTableCount];
SCORETABLE_ROWS ScoringRows[kScoringTableCount];
float ScoringTableDifMul[3];
int32_t* NewScoresRef = nullptr;
PlayerStatsGuards g_player_stats_guards;

// O modo de arredondamento muda dentro do arquivo (nf::RoundTowardZero, platform/compiler.h: o _controlfp_s do MSVC):
// o compilador não pode dobrar contas de float supondo o modo padrão.
NF_FENV_ACCESS_ON

namespace {

uint32_t mp_on() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x180, 4);
    return v;
}

// fptoui (0x00224608, o __fixunssfsi da libgcc): trunca; de 2^31 para cima, subtrai 2^31 antes.
uint32_t fptoui(float a) {
    if (2147483648.0f <= a) return uint32_t(int32_t(a - 2147483648.0f)) + 0x80000000u;
    return uint32_t(int32_t(a));
}

// Os pontos de uma linha: trunca e soma 1 se sobrou 1e-5 ou mais (um "teto" com tolerância).
float ceil_points(float f) {
    int32_t i = int32_t(f);
    if (1e-05f <= std::fabs(f - float(i))) ++i;
    return float(i);
}

// A fração em centésimos: f já multiplicado por 100, arredondado para longe do zero pelo sinal da parte
// inteira (como o original: -0,7 iria para +1), vezes 0,01 e presa em [0, 1].
float percent(float f) {
    int32_t i = int32_t(f);
    if (0.5f <= std::fabs(f - float(i))) i += i < 0 ? -1 : 1;
    const float v = float(i) * 0.01f;
    if (v < 0.0f) return 0.0f;
    return 1.0f < v ? 1.0f : v;
}

// Zera o que a missão mudou nas linhas (valor, fração, pontos) e os totais de tempo até o recorde.
void clear_rows(SCORETABLE& e) {
    for (uint32_t r = 0; r < kScoreRows; ++r) {
        e.rows[r].actual = 0;
        e.rows[r].fraction = 0.0f;
        e.rows[r].earned = 0.0f;
    }
}

}  // namespace

// [decomp] 0x00198318 PlrStat_Init
// Na partida (bootup_bootup): as estatísticas da missão zeradas e, em cada missão da tabela, o que a
// pontuação calculou (valores, frações e pontos das linhas, o tempo, os totais e o recorde).
void PlrStat_Init() {
    PlrStat_ResetForMission();
    for (uint32_t i = 0; i < kScoringTableCount; ++i) {
        SCORETABLE& e = ScoringTable[i];
        if (e.rows != nullptr) clear_rows(e);
        e.time = 0;
        e.time_target = 0;
        e.difficulty_mul = 0;
        e.bonus = 0;
        e.score = 0;
        e.best = 0;
        e.total = 0;
    }
}

// [decomp] 0x001993a0 PlrStat_ResetForMission
// Estatísticas da missão e momentos Bond zerados no começo de uma missão (Mission_Init).
void PlrStat_ResetForMission() {
    std::memset(PlrMissionStats, 0, sizeof(PlrMissionStats));
    std::memset(BondMoments, 0, sizeof(BondMoments));
}

// [decomp] 0x001993e0 PlrStat_OkToUpdate
// As estatísticas (e a vida) só mudam com a missão em andamento ou no multijogador.
bool PlrStat_OkToUpdate() {
    if (mp_on() != 0) return true;
    const int32_t status = Mission_Status();
    return status < 2 && 0 <= status;
}

// [decomp] 0x00198408 PlrStat_LogShotHitScenery
// Um tiro num alvo de cenário que conta como acerto (Destroy_Smash, os scripts 0x60007e2/e4-ea de
// SP_Update): desconta um disparo, para não pesar na precisão, e os acertos não passam dos disparos.
void PlrStat_LogShotHitScenery(uint32_t player) {
    if (player >= kPlrStatPlayers || !PlrStat_OkToUpdate()) return;
    PLRMISSIONSTATS& s = PlrMissionStats[player];
    if (0 < s.shots_fired) --s.shots_fired;
    s.shots_hit = std::min(s.shots_hit, s.shots_fired);
}

// [decomp] 0x001984b0 PlrStat_GetScore
// A pontuação da missão em BaseMap (a entrada dela na ScoringTable; nulo fora dela). Missão de direção
// (09xxxxxx): alvo, pontos e valor de cada linha vêm de NewScoresRef (o tempo vezes 100); sem ele, nulo. A pé:
// os valores vêm das estatísticas do jogador 0 (a precisão é acertos x alvo / disparos, o alvo sem
// disparos; a vida truncada, 0 se ele morreu). Cada linha com alvo:
// - 4 detecções: até o alvo, todos os pontos; acima, perde detect_penalty (0,03 se 0) por detecção;
// - 7 tempo (t = centésimos / 100): pontos x alvo / t (t no mínimo 1), sem limite: mais rápido, mais pontos;
// - 8 fichas 007: todos os pontos com o alvo alcançado, senão nenhum;
// - as outras: pontos x min(valor, alvo) / alvo.
// Os pontos vão para cima (tolerância 1e-5) e a fração é a razão em centésimos, presa em [0, 1]; a soma é
// acumulada em float. O bônus é a soma vezes ScoringTableDifMul da dificuldade (0, 1, 2). Com peek = 0
// (o resultado e as estatísticas; a pausa passa 1): o total (soma + bônus), o recorde e DoneBetter (o total,
// não nulo, alcançou o recorde). Fora do estado 6 (missão cumprida), os pontos, o bônus, a soma, o total e
// DoneBetter ficam zerados (o recorde já foi atualizado, como no original).
uint8_t* PlrStat_GetScore(uint8_t peek) {
    const nf::RoundTowardZero ee;  // como a FPU do EE
    const uint32_t level = Mission_BaseMapHCode();
    SCORETABLE* e = nullptr;
    for (uint32_t i = 0; i < kScoringTableCount; ++i) {
        if (ScoringTable[i].level == level) {
            e = &ScoringTable[i];
            break;
        }
    }
    if (e == nullptr) return nullptr;
    // Só no port: as tabelas não foram copiadas do ELF.
    if (e->rows == nullptr) return nullptr;
    SCORETABLE_ROW* r = e->rows;
    e->on_foot = (level & 0xff000000u) == 0x9000000u ? 0 : 1;
    if (e->on_foot == 0) {
        if (NewScoresRef == nullptr) return nullptr;
        const int32_t* s = NewScoresRef;
        for (uint32_t i = 0; i < kScoreRows; ++i, s += 3) {
            int32_t actual = s[0];
            if (i == kScoreTime) actual *= 100;
            r[i].target = uint32_t(s[1]);
            r[i].points = uint32_t(s[2]);
            r[i].actual = uint32_t(actual);
        }
    } else {
        const PLRMISSIONSTATS& st = PlrMissionStats[0];
        r[kScoreBondMoments].actual = st.bond_moments;
        r[kScoreDispatched].actual = st.dispatched;
        r[kScoreDisabled].actual = st.disabled;
        r[kScoreSurrendered].actual = st.surrendered;
        r[kScoreDetected].actual = st.detected;
        r[kScoreAccuracy].actual = r[kScoreAccuracy].target;
        const uint32_t fired = uint32_t(st.shots_fired);
        if (fired != 0) r[kScoreAccuracy].actual = uint32_t(st.shots_hit) * r[kScoreAccuracy].target / fired;
        r[kScoreHealth].actual = fptoui(st.health);
        r[kScoreTime].actual = uint32_t(st.elapsed);
        r[kScoreBonus].actual = st.bonus;
    }
    if (Mission_Status() == int32_t(kMissionDied)) r[kScoreHealth].actual = 0;
    e->time = r[kScoreTime].actual;
    e->score = 0;
    e->time_target = r[kScoreTime].target;
    for (uint32_t i = 0; i < kScoreRows; ++i) {
        SCORETABLE_ROW& row = r[i];
        row.fraction = 0.0f;
        row.earned = 0.0f;
        if (float(row.target) == 0.0f) continue;
        bool fraction_from_points = true;  // a fração pelos pontos ganhos / pontos (linhas 4 e 7)
        if (i == kScoreTime) {
            const uint32_t t = row.actual / 100;
            if (!(row.target < t)) {
                const float div = std::max(float(t), 1.0f);
                row.earned = ceil_points(float(row.points * row.target) / div);
            } else {
                const float ratio = float(t) / float(row.target);
                if (ratio != 0.0f) row.earned = ceil_points(float(row.points) / ratio);
            }
        } else if (i == kScoreDetected) {
            if (!(row.target < row.actual)) {
                row.earned = float(row.points);
                row.fraction = 1.0f;
                fraction_from_points = false;
            } else {
                float k = e->detect_penalty;
                if (k == 0.0f) k = 0.03f;
                const float left = std::max(1.0f - float(row.actual - row.target) * k, 0.0f);
                row.earned = ceil_points(left * float(row.points));
            }
        } else if (i == kScoreBonus) {
            if (!(row.actual < row.target)) {
                row.earned = float(row.points);
                row.fraction = 1.0f;
            }
            fraction_from_points = false;
        } else {
            const float v = float(std::min(row.actual, row.target));
            row.earned = ceil_points(float(row.points) * v / float(row.target));
            row.fraction = percent(v / float(row.target) * 100.0f);
            fraction_from_points = false;
        }
        if (fraction_from_points && float(row.points) != 0.0f) {
            row.fraction = percent(row.earned / float(row.points) * 100.0f);
        }
        e->score = fptoui(float(e->score) + row.earned);
    }
    float mul = ScoringTableDifMul[2];
    if (GameState.difficulty == 1) {
        mul = ScoringTableDifMul[0];
    } else if (GameState.difficulty == 2) {
        mul = ScoringTableDifMul[1];
    }
    e->difficulty_mul = fptoui(mul);
    e->bonus = e->score * e->difficulty_mul;
    if (peek == 0) {
        DoneBetter = 0;
        const uint32_t sum = e->score + e->bonus;
        if (sum != 0) DoneBetter = e->best <= sum ? 1 : 0;
        e->total = e->score + e->bonus;
        e->best = std::max(e->best, e->total);
    }
    if (Mission_Status() != int32_t(kMissionComplete)) {
        for (uint32_t i = 0; i < kScoreRows; ++i) r[i].earned = 0.0f;
        e->difficulty_mul = 0;
        e->bonus = 0;
        e->score = 0;
        e->total = 0;
        DoneBetter = 0;
    }
    return reinterpret_cast<uint8_t*>(e);
}

// [decomp] 0x00199020 PlrStats_ResetScoring
// Um perfil novo (o nome do agente no menu principal): o que a pontuação calculou e os recordes zerados
// (o total fica).
void PlrStats_ResetScoring() {
    for (uint32_t i = 0; i < kScoringTableCount; ++i) {
        SCORETABLE& e = ScoringTable[i];
        if (e.rows != nullptr) clear_rows(e);
        e.time = 0;
        e.time_target = 0;
        e.difficulty_mul = 0;
        e.bonus = 0;
        e.score = 0;
        e.best = 0;
    }
}

// [decomp] 0x001990e0 PlrStats_SetupScoreTable
// Um perfil carregado (LS_LoadMission): as `count` primeiras entradas vêm de `table` (valor, fração e pontos
// de cada linha, o tempo, os totais e o recorde; o total fica).
void PlrStats_SetupScoreTable(SCORETABLE* table, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        SCORETABLE& e = ScoringTable[i];
        const SCORETABLE& from = table[i];
        for (uint32_t r = 0; r < kScoreRows; ++r) {
            e.rows[r].actual = from.rows[r].actual;
            e.rows[r].fraction = from.rows[r].fraction;
            e.rows[r].earned = from.rows[r].earned;
        }
        e.time = from.time;
        e.time_target = from.time_target;
        e.difficulty_mul = from.difficulty_mul;
        e.bonus = from.bonus;
        e.score = from.score;
        e.best = from.best;
    }
}

// [decomp] 0x00199430 PlrStat_LogEnemySpawned
// Um inimigo criado (NDrone2_DefaultInit): sempre, no jogador 0.
void PlrStat_LogEnemySpawned() { ++PlrMissionStats[0].spawned; }

// [decomp] 0x00199448 PlrStat_DoneBondMoment
// O canal de um momento Bond ligado (Trigger_Calc, canais 0x65-0x6e): a primeira vez na missão conta e
// acende o 007 girando no HUD (BLData::bond_moment = 200 ticks, HUD_UpdateHealthPane).
void PlrStat_DoneBondMoment(uint32_t player, uint16_t channel) {
    if (player >= kPlrStatPlayers || !PlrStat_OkToUpdate()) return;
    const uint32_t i = uint32_t(channel) - kBondMomentFirst;
    // Só no port: o original escreveria fora de BondMoments.
    if (kBondMomentCount <= i) {
        ++g_player_stats_guards.bond_channel;
        return;
    }
    if (BondMoments[i] != 0) return;
    // Só no port: glb_blokes tem 4 jogadores; sem BLData o original escreveria por nulo.
    BLData* b = player < 4 ? glb_blokes[player] : nullptr;
    if (b == nullptr) {
        ++g_player_stats_guards.bond_player;
        return;
    }
    BondMoments[i] = 1;
    ++PlrMissionStats[player].bond_moments;
    b->bond_moment = 200;
}

// [decomp] 0x00199520 PlarStat_LogTimerPause
void PlarStat_LogTimerPause(uint32_t player) {
    if (player < kPlrStatPlayers) PlrMissionStats[player].timer_paused = 1;
}

// [decomp] 0x00199550 PlarStat_LogTimerUnpause
// O cronômetro volta a correr a partir de agora.
void PlarStat_LogTimerUnpause(uint32_t player) {
    if (player >= kPlrStatPlayers) return;
    PlrMissionStats[player].timer_last = psiGetTimeIn100ths();
    PlrMissionStats[player].timer_paused = 0;
}

// [decomp] 0x001995a0 PlarStat_LogUpdateElapsedTime
// Cada tick do jogador vivo (Player_Update): soma o tempo desde a última vez, se o cronômetro corre.
void PlarStat_LogUpdateElapsedTime(uint32_t player) {
    if (player >= kPlrStatPlayers || PlrMissionStats[player].timer_paused != 0) return;
    PLRMISSIONSTATS& s = PlrMissionStats[player];
    const int32_t now = psiGetTimeIn100ths();
    s.elapsed += now - s.timer_last;
    s.timer_last = now;
}

// [decomp] 0x00199620 PlrStat_LogHealth
void PlrStat_LogHealth(float health, uint32_t player) {
    if (player < kPlrStatPlayers && PlrStat_OkToUpdate()) PlrMissionStats[player].health = health;
}

// [decomp] 0x001996a8 PlrStat_LogEnemyDispatched
void PlrStat_LogEnemyDispatched(uint32_t player) {
    if (player < kPlrStatPlayers && PlrStat_OkToUpdate()) ++PlrMissionStats[player].dispatched;
}

// [decomp] 0x00199730 PlrStat_LogEnemyDisabled
void PlrStat_LogEnemyDisabled(uint32_t player) {
    if (player < kPlrStatPlayers && PlrStat_OkToUpdate()) ++PlrMissionStats[player].disabled;
}

// [decomp] 0x001997b8 PlrStat_LogEnemySurrender
void PlrStat_LogEnemySurrender(uint32_t player) {
    if (player < kPlrStatPlayers && PlrStat_OkToUpdate()) ++PlrMissionStats[player].surrendered;
}

// [decomp] 0x00199840 PlrStat_LogShotHitEnemy
// Um acerto num inimigo; nunca passa dos disparos.
void PlrStat_LogShotHitEnemy(uint32_t player) {
    if (player >= kPlrStatPlayers || !PlrStat_OkToUpdate()) return;
    PLRMISSIONSTATS& s = PlrMissionStats[player];
    s.shots_hit = std::min(s.shots_hit + 1, s.shots_fired);
}

// [decomp] 0x001998d8 PlrStat_LogShotFired
void PlrStat_LogShotFired(uint32_t player) {
    if (player < kPlrStatPlayers && PlrStat_OkToUpdate()) ++PlrMissionStats[player].shots_fired;
}

// [decomp] 0x00199960 PlrStat_LogEnemyDetectedPlayer
// Um inimigo viu o jogador (DroneVision_LogSeenOpponent, uma vez por personagem).
void PlrStat_LogEnemyDetectedPlayer(uint32_t player) {
    if (player < kPlrStatPlayers && PlrStat_OkToUpdate()) ++PlrMissionStats[player].detected;
}

// [decomp] 0x001999e0 PlrStat_LogBondBonus
// Uma ficha "007 Bonus" pega (Pickup_Handler), no jogador 0.
void PlrStat_LogBondBonus() {
    if (PlrStat_OkToUpdate()) ++PlrMissionStats[0].bonus;
}

// [decomp] 0x00199a40 PlrStats_HasGoldMedal
// A missão já foi cumprida com a medalha de ouro (o bit dela no perfil do jogador 0): só então as fichas
// "007 Bonus" da fase existem (Pickup_Create, tipo 6).
bool PlrStats_HasGoldMedal() { return Menu_HasMedal(Mission_BaseMapHCode(), 3, 0) != 0; }

// [decomp] 0x00199a70 PlrStats_GetLevelTotals
// A entrada da missão `level` (os recordes do menu principal, C_RBDSRECORDS_Handler); nulo fora da tabela.
SCORETABLE* PlrStats_GetLevelTotals(uint32_t level) {
    for (uint32_t i = 0; i < kScoringTableCount; ++i) {
        if (ScoringTable[i].level == level) return &ScoringTable[i];
    }
    return nullptr;
}

// [decomp] 0x00199aa8 PlrStats_GetScoreTable
// A tabela inteira para salvar o perfil (LS_MakeMission).
SCORETABLE* PlrStats_GetScoreTable(uint32_t* count) {
    *count = kScoringTableCount;
    return ScoringTable;
}

// [decomp] 0x00199ac8 PlrStats_GetDrivingData
// O que passa para o DRIVING.ELF (Boot_GetPTPData): o recorde e o valor da linha 0 de cada missão.
void PlrStats_GetDrivingData(uint32_t* best, uint32_t* first) {
    for (uint32_t i = 0; i < kScoringTableCount; ++i) {
        best[i] = ScoringTable[i].best;
        first[i] = ScoringTable[i].rows->actual;
    }
}

// [decomp] 0x00199b18 PlrStats_SetDrivingData
// O que volta do DRIVING.ELF (Boot_LoadPTPData).
void PlrStats_SetDrivingData(uint32_t* best, uint32_t* first) {
    for (uint32_t i = 0; i < kScoringTableCount; ++i) {
        ScoringTable[i].best = best[i];
        ScoringTable[i].rows->actual = first[i];
    }
}

// ---- só do port ----

uint32_t PlrStat_MedalFor(const SCORETABLE* table, uint32_t total) {
    if (table == nullptr || total == 0) return 0;
    for (uint32_t m = 4; m != 0; --m) {
        if (table->medal[m - 1] <= total) return m;
    }
    return 0;
}

const SCORETABLE* PlrStat_Current() {
    const uint32_t level = Mission_BaseMapHCode();
    for (const SCORETABLE& e : ScoringTable) {
        if (e.level == level && e.rows != nullptr) return &e;
    }
    return nullptr;
}

int PlrStat_Describe(char* out, size_t size) {
    const PLRMISSIONSTATS& s = PlrMissionStats[0];
    int n = std::snprintf(out, size,
                          "tiros %d acertos %d abatidos %u desacordados %u rendidos %u vistos %u gerados %u vida %.0f "
                          "momentos %u fichas %u tempo %.2f parado %u",
                          s.shots_fired, s.shots_hit, s.dispatched, s.disabled, s.surrendered, s.detected, s.spawned,
                          double(s.health), s.bond_moments, s.bonus, double(s.elapsed) / 100.0, s.timer_paused);
    if (n < 0 || size_t(n) >= size) return n;
    const SCORETABLE* e = PlrStat_Current();
    if (e == nullptr) {
        n += std::snprintf(out + n, size - size_t(n), " | sem_tabela %08x", Mission_BaseMapHCode());
    } else {
        n += std::snprintf(out + n, size - size_t(n), " | missao %08x pontos %u bonus %u total %u recorde %u medalha %u",
                           e->level, e->score, e->bonus, e->total, e->best, PlrStat_MedalFor(e, e->total));
    }
    return n;
}

void player_stats_debug_power_on() {
    PlrStat_ResetForMission();
    NewScoresRef = nullptr;
    g_player_stats_guards = PlayerStatsGuards{};
}
