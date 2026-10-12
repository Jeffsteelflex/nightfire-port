// Testes das estatísticas do jogador e da pontuação (src/engine/player_stats.cpp): os contadores (só com a
// missão em andamento ou no multijogador), o cronômetro, os momentos Bond e a pontuação de uma missão
// montada à mão (sem dados do jogo: alvos, pontos e limites de medalha inventados).

#include <cstring>

#include "engine/gamestate.h"
#include "engine/menu.h"
#include "engine/mission.h"
#include "engine/player.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

constexpr uint32_t kLevel = 0x7000077;  // uma missão inventada

void set_mp(uint32_t on) { std::memcpy(MPSettings + 0x180, &on, 4); }

// Mundo mínimo: estatísticas zeradas, sem multijogador, a missão kLevel em andamento.
struct StatsWorld {
    SCORETABLE saved[kScoringTableCount];
    SCORETABLE_ROW rows[kScoreRows]{};
    float mul[3];
    StatsWorld() {
        std::memcpy(saved, ScoringTable, sizeof(saved));
        std::memcpy(mul, ScoringTableDifMul, sizeof(mul));
        player_stats_debug_power_on();
        std::memset(MPSettings, 0, sizeof(MPSettings));
        std::memset(glb_blokes, 0, sizeof(glb_blokes));
        BaseMap = kLevel;
        Mission_SetStatus(kMissionRunning);
        DoneBetter = 0;
    }
    ~StatsWorld() {
        std::memcpy(ScoringTable, saved, sizeof(saved));
        std::memcpy(ScoringTableDifMul, mul, sizeof(mul));
        set_mp(0);
        player_stats_debug_power_on();
    }
    // A entrada 3 da tabela vira a missão kLevel: medalhas 1000/2000/3000/4000, 0,25 por detecção a mais,
    // e as linhas (alvo, pontos): momentos 2/100, abatidos 10/1000, desacordados sem alvo, rendidos 3/300,
    // detecções 2/400, precisão 100/1000, vida 100/500, tempo 60 s/600, fichas 3/700. Bônus x0, x1, x2.
    SCORETABLE& mission() {
        SCORETABLE& e = ScoringTable[3];
        std::memset(&e, 0, sizeof(e));
        e.level = kLevel;
        const uint32_t medals[4] = {1000, 2000, 3000, 4000};
        std::memcpy(e.medal, medals, sizeof(medals));
        e.detect_penalty = 0.25f;
        const uint32_t def[kScoreRows][2] = {{2, 100}, {10, 1000}, {0, 0},   {3, 300}, {2, 400},
                                             {100, 1000}, {100, 500}, {60, 600}, {3, 700}};
        for (uint32_t r = 0; r < kScoreRows; ++r) rows[r] = SCORETABLE_ROW{def[r][0], def[r][1], 0, 0.0f, 0.0f};
        e.rows = rows;
        ScoringTableDifMul[0] = 0.0f;
        ScoringTableDifMul[1] = 1.0f;
        ScoringTableDifMul[2] = 2.0f;
        return e;
    }
};

SCORETABLE* score(uint8_t peek) { return reinterpret_cast<SCORETABLE*>(PlrStat_GetScore(peek)); }

}  // namespace

TEST(player_stats_counters_only_while_running) {
    StatsWorld w;
    PLRMISSIONSTATS& s = PlrMissionStats[0];
    for (int i = 0; i < 4; ++i) PlrStat_LogShotFired(0);
    for (int i = 0; i < 5; ++i) PlrStat_LogShotHitEnemy(0);
    CHECK(s.shots_fired == 4 && s.shots_hit == 4);  // os acertos não passam dos disparos
    // Um alvo de cenário desconta um disparo e puxa os acertos junto.
    PlrStat_LogShotHitScenery(0);
    CHECK(s.shots_fired == 3 && s.shots_hit == 3);
    PlrStat_LogEnemyDispatched(0);
    PlrStat_LogEnemyDispatched(0);
    PlrStat_LogEnemyDisabled(0);
    PlrStat_LogEnemySurrender(0);
    PlrStat_LogEnemyDetectedPlayer(0);
    PlrStat_LogBondBonus();
    PlrStat_LogHealth(73.5f, 0);
    CHECK(s.dispatched == 2 && s.disabled == 1 && s.surrendered == 1 && s.detected == 1 && s.bonus == 1);
    CHECK(s.health == 73.5f);
    // Jogadores 0-7; o 8 não existe.
    PlrStat_LogShotFired(7);
    PlrStat_LogShotFired(8);
    CHECK(PlrMissionStats[7].shots_fired == 1);
    // Missão fracassada (ou sem missão): nada conta, a não ser os gerados.
    for (uint32_t st : {kMissionFailed, kMissionDied, kMissionNone, kMissionComplete}) {
        Mission_SetStatus(st);
        PlrStat_LogShotFired(0);
        PlrStat_LogEnemyDispatched(0);
        PlrStat_LogBondBonus();
        PlrStat_LogHealth(10.0f, 0);
        PlrStat_LogEnemySpawned();
    }
    CHECK(s.shots_fired == 3 && s.dispatched == 2 && s.bonus == 1 && s.health == 73.5f && s.spawned == 4);
    CHECK(PlrStat_OkToUpdate() == false);
    // No multijogador contam sempre.
    set_mp(1);
    CHECK(PlrStat_OkToUpdate() == true);
    PlrStat_LogShotFired(0);
    CHECK(s.shots_fired == 4);
    // O começo da missão zera tudo.
    PlrStat_ResetForMission();
    CHECK(s.shots_fired == 0 && s.spawned == 0 && PlrMissionStats[7].shots_fired == 0);
}

TEST(player_stats_mission_timer) {
    StatsWorld w;
    PLRMISSIONSTATS& s = PlrMissionStats[0];
    VBlankCount = 1000;
    psiInitTimeIn100ths();
    PlarStat_LogTimerUnpause(0);  // Player_Init
    VBlankCount += 120;           // 2 s
    PlarStat_LogUpdateElapsedTime(0);
    CHECK(s.elapsed == 200);
    // Parado (pausa, cena) não soma; ao voltar, conta a partir de então.
    PlarStat_LogTimerPause(0);
    VBlankCount += 600;
    PlarStat_LogUpdateElapsedTime(0);
    CHECK(s.elapsed == 200 && s.timer_paused == 1);
    PlarStat_LogTimerUnpause(0);
    VBlankCount += 30;
    PlarStat_LogUpdateElapsedTime(0);
    CHECK(s.elapsed == 250 && s.timer_paused == 0);
    // O cronômetro corre com a missão em qualquer estado.
    Mission_SetStatus(kMissionComplete);
    VBlankCount += 6;
    PlarStat_LogUpdateElapsedTime(0);
    CHECK(s.elapsed == 260);
    VBlankCount = 0;
    psiInitTimeIn100ths();
}

TEST(player_stats_bond_moments) {
    StatsWorld w;
    static BLData b;
    std::memset(&b, 0, sizeof(b));
    glb_blokes[0] = &b;
    PlrStat_DoneBondMoment(0, 0x66);
    CHECK(PlrMissionStats[0].bond_moments == 1 && BondMoments[1] == 1 && b.bond_moment == 200);
    // O mesmo canal de novo não conta (nem reacende o 007).
    b.bond_moment = 5;
    PlrStat_DoneBondMoment(0, 0x66);
    CHECK(PlrMissionStats[0].bond_moments == 1 && b.bond_moment == 5);
    PlrStat_DoneBondMoment(0, 0x6e);
    CHECK(PlrMissionStats[0].bond_moments == 2 && BondMoments[9] == 1);
    // Fora dos canais 0x65-0x6e ou sem BLData: guardas do port.
    PlrStat_DoneBondMoment(0, 0x6f);
    PlrStat_DoneBondMoment(1, 0x65);
    CHECK(PlrMissionStats[0].bond_moments == 2 && PlrMissionStats[1].bond_moments == 0);
    CHECK(g_player_stats_guards.bond_channel == 1 && g_player_stats_guards.bond_player == 1);
    // Fora da missão em andamento, nada.
    Mission_SetStatus(kMissionFailed);
    PlrStat_DoneBondMoment(0, 0x65);
    CHECK(PlrMissionStats[0].bond_moments == 2 && BondMoments[0] == 0);
    glb_blokes[0] = nullptr;
}

TEST(player_stats_score_rows) {
    StatsWorld w;
    SCORETABLE& e = w.mission();
    PLRMISSIONSTATS& s = PlrMissionStats[0];
    s.bond_moments = 1;       // metade do alvo
    s.dispatched = 15;        // acima do alvo: não passa dos pontos
    s.disabled = 4;           // linha sem alvo: não conta
    s.surrendered = 1;        // um terço
    s.detected = 4;           // 2 acima do alvo: perde 2 x 0,25
    s.shots_fired = 4;
    s.shots_hit = 3;          // 75%
    s.health = 80.7f;         // trunca para 80
    s.elapsed = 12000;        // 120 s, o dobro do alvo: metade dos pontos
    s.bonus = 3;              // todas as fichas
    GameState.difficulty = 3; // 00 Agent: bônus x2
    Mission_SetStatus(kMissionComplete);
    SCORETABLE* t = score(0);
    CHECK(t == &e && e.on_foot == 1);
    const SCORETABLE_ROW* r = e.rows;
    CHECK(r[kScoreBondMoments].actual == 1 && r[kScoreBondMoments].earned == 50.0f);
    CHECK_NEAR(r[kScoreBondMoments].fraction, 0.5, 1e-6);
    CHECK(r[kScoreDispatched].actual == 15 && r[kScoreDispatched].earned == 1000.0f);
    CHECK_NEAR(r[kScoreDispatched].fraction, 1.0, 1e-6);
    CHECK(r[kScoreDisabled].actual == 4 && r[kScoreDisabled].earned == 0.0f && r[kScoreDisabled].fraction == 0.0f);
    CHECK(r[kScoreSurrendered].earned == 100.0f);
    CHECK_NEAR(r[kScoreSurrendered].fraction, 0.33, 1e-6);
    CHECK(r[kScoreDetected].actual == 4 && r[kScoreDetected].earned == 200.0f);
    CHECK_NEAR(r[kScoreDetected].fraction, 0.5, 1e-6);
    CHECK(r[kScoreAccuracy].actual == 75 && r[kScoreAccuracy].earned == 750.0f);
    CHECK_NEAR(r[kScoreAccuracy].fraction, 0.75, 1e-6);
    CHECK(r[kScoreHealth].actual == 80 && r[kScoreHealth].earned == 400.0f);
    CHECK(r[kScoreTime].actual == 12000 && r[kScoreTime].earned == 300.0f);
    CHECK_NEAR(r[kScoreTime].fraction, 0.5, 1e-6);
    CHECK(r[kScoreBonus].earned == 700.0f && r[kScoreBonus].fraction == 1.0f);
    CHECK(e.time == 12000 && e.time_target == 60);
    const uint32_t sum = 50 + 1000 + 100 + 200 + 750 + 400 + 300 + 700;
    CHECK(e.score == sum && e.difficulty_mul == 2 && e.bonus == 2 * sum && e.total == 3 * sum);
    CHECK(e.best == 3 * sum && DoneBetter == 1);
    CHECK(PlrStat_MedalFor(&e, e.total) == 4);  // 10500: platina
}

TEST(player_stats_score_edges) {
    StatsWorld w;
    SCORETABLE& e = w.mission();
    SCORETABLE_ROW* r = e.rows;
    PLRMISSIONSTATS& s = PlrMissionStats[0];
    GameState.difficulty = 1;  // Operative: sem bônus
    Mission_SetStatus(kMissionComplete);
    // Sem disparos a precisão é o alvo (100%); detecções até o alvo valem tudo; fichas abaixo do alvo, nada;
    // o tempo abaixo do alvo dá mais que os pontos (30 s de 60: o dobro) e a fração fica em 1.
    s.health = 100.0f;
    s.detected = 2;
    s.bonus = 2;
    s.elapsed = 3000;
    score(0);
    CHECK(r[kScoreAccuracy].actual == 100 && r[kScoreAccuracy].earned == 1000.0f);
    CHECK(r[kScoreDetected].earned == 400.0f && r[kScoreDetected].fraction == 1.0f);
    CHECK(r[kScoreBonus].earned == 0.0f && r[kScoreBonus].fraction == 0.0f);
    CHECK(r[kScoreTime].earned == 1200.0f && r[kScoreTime].fraction == 1.0f);
    CHECK(e.difficulty_mul == 0 && e.bonus == 0 && e.score == 1000 + 400 + 500 + 1200 && e.total == e.score);
    // Menos de 1 s conta como 1 s.
    s.elapsed = 50;
    score(1);
    CHECK(r[kScoreTime].earned == 600.0f * 60.0f);
    // Detecções demais zeram a linha (sem ficar negativa); penalidade 0 vale 0,03.
    s.detected = 10;
    score(1);
    CHECK(r[kScoreDetected].earned == 0.0f && r[kScoreDetected].fraction == 0.0f);
    e.detect_penalty = 0.0f;
    s.detected = 12;  // 10 a mais: 1 - 0,3 = 70%
    score(1);
    CHECK(r[kScoreDetected].earned == 280.0f);
    CHECK_NEAR(r[kScoreDetected].fraction, 0.7, 1e-6);
    // Pontos fracionários vão para cima: 1 de 3 rendidos com 1000 pontos = 333,33 -> 334.
    r[kScoreSurrendered].points = 1000;
    s.surrendered = 1;
    score(1);
    CHECK(r[kScoreSurrendered].earned == 334.0f);
}

TEST(player_stats_score_record_and_status) {
    StatsWorld w;
    SCORETABLE& e = w.mission();
    PLRMISSIONSTATS& s = PlrMissionStats[0];
    GameState.difficulty = 2;  // Agent: bônus x1
    s.health = 100.0f;
    s.elapsed = 6000;  // no alvo
    Mission_SetStatus(kMissionComplete);
    // peek = 1 (a pausa): calcula as linhas, mas não mexe no total, no recorde nem em DoneBetter.
    score(1);
    const uint32_t sum = 1000 + 500 + 600 + 400;  // precisão, vida, tempo, detecções
    CHECK(e.score == sum && e.difficulty_mul == 1 && e.bonus == sum && e.total == 0 && e.best == 0 && DoneBetter == 0);
    GameState.difficulty = 1;  // Operative: sem bônus
    score(0);
    CHECK(e.total == sum && e.best == sum && DoneBetter == 1);
    CHECK(PlrStat_MedalFor(&e, e.total) == 2);  // 2500: prata
    // De novo, igual ao recorde: DoneBetter continua (o recorde foi alcançado).
    score(0);
    CHECK(e.total == sum && DoneBetter == 1);
    // Pior que o recorde: o recorde fica e DoneBetter apaga.
    s.health = 0.0f;
    score(0);
    CHECK(e.total == sum - 500 && e.best == sum && DoneBetter == 0);
    // O jogador morreu (estado 3): a vida conta 0 e, fora do estado 6, nenhum ponto.
    s.health = 100.0f;
    Mission_SetStatus(kMissionDied);
    score(0);
    CHECK(e.rows[kScoreHealth].actual == 0 && e.rows[kScoreAccuracy].earned == 0.0f);
    CHECK(e.score == 0 && e.bonus == 0 && e.total == 0 && e.difficulty_mul == 0 && DoneBetter == 0);
    CHECK(e.best == sum);
    // Fora da tabela: nulo. Missão de direção (09xxxxxx) sem os dados do DRIVING.ELF: nulo.
    BaseMap = 0x7000078;
    CHECK(PlrStat_GetScore(0) == nullptr && PlrStats_GetLevelTotals(0x7000078) == nullptr);
    e.level = 0x9000077;
    BaseMap = 0x9000077;
    CHECK(PlrStat_GetScore(0) == nullptr && e.on_foot == 0);
    // Com eles: valor, alvo e pontos de cada linha (o tempo em segundos vira centésimos).
    int32_t driving[kScoreRows * 3] = {};
    driving[kScoreTime * 3 + 0] = 30;   // 30 s
    driving[kScoreTime * 3 + 1] = 60;
    driving[kScoreTime * 3 + 2] = 100;
    NewScoresRef = driving;
    Mission_SetStatus(kMissionComplete);
    score(1);
    CHECK(e.rows[kScoreTime].actual == 3000 && e.rows[kScoreTime].earned == 200.0f && e.score == 200);
    CHECK(e.rows[kScoreAccuracy].target == 0 && e.rows[kScoreAccuracy].earned == 0.0f);
}

TEST(player_stats_tables_and_reset) {
    StatsWorld w;
    SCORETABLE& e = w.mission();
    uint32_t n = 0;
    CHECK(PlrStats_GetScoreTable(&n) == ScoringTable && n == kScoringTableCount);
    CHECK(PlrStats_GetLevelTotals(kLevel) == &e);
    // A medalha: o maior limite alcançado; total 0 é nenhuma.
    CHECK(PlrStat_MedalFor(&e, 0) == 0 && PlrStat_MedalFor(&e, 999) == 0 && PlrStat_MedalFor(&e, 1000) == 1);
    CHECK(PlrStat_MedalFor(&e, 2999) == 2 && PlrStat_MedalFor(&e, 3000) == 3 && PlrStat_MedalFor(&e, 99999) == 4);
    // Um perfil salvo volta (valores, frações, pontos, tempo, totais e recorde; o total não).
    SCORETABLE saved[kScoringTableCount];
    SCORETABLE_ROW saved_rows[kScoringTableCount][kScoreRows]{};
    std::memcpy(saved, ScoringTable, sizeof(saved));
    for (uint32_t i = 0; i < kScoringTableCount; ++i) saved[i].rows = saved_rows[i];
    saved_rows[3][kScoreTime] = SCORETABLE_ROW{99, 99, 4500, 0.5f, 300.0f};
    saved[3].best = 7777;
    saved[3].score = 123;
    saved[3].total = 999;
    // As outras entradas da tabela real não têm linhas aqui: só a 3 e as anteriores (com linhas falsas).
    SCORETABLE_ROW scratch[3][kScoreRows]{};
    SCORETABLE keep[3];
    std::memcpy(keep, ScoringTable, sizeof(keep));
    for (uint32_t i = 0; i < 3; ++i) ScoringTable[i].rows = scratch[i];
    PlrStats_SetupScoreTable(saved, 4);
    CHECK(e.rows[kScoreTime].actual == 4500 && e.rows[kScoreTime].earned == 300.0f && e.rows[kScoreTime].target == 60);
    CHECK(e.best == 7777 && e.score == 123 && e.total == 0);
    // Um perfil novo zera o que a pontuação calculou e o recorde.
    e.total = 55;
    PlrStats_ResetScoring();
    CHECK(e.rows[kScoreTime].actual == 0 && e.rows[kScoreTime].earned == 0.0f && e.best == 0 && e.score == 0);
    CHECK(e.total == 55 && e.rows[kScoreTime].points == 600);
    // A partida zera também o total.
    PlrStat_Init();
    CHECK(e.total == 0);
    std::memcpy(ScoringTable, keep, sizeof(keep));
}
