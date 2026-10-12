// A missão. Ver mission.h.
//
// Divergências:
// - Mission_MonitorObjectives: Txt_GetStringFromHeap e Txt_BindLabel ainda devolvem nulo; sem a
//   string do heap o anúncio do objetivo (Text_AddMsg) não é feito (o original escreveria por nulo).
//   O estado dos objetivos muda do mesmo jeito.

#include "engine/mission.h"

#include <cstdio>
#include <cstring>

#include "engine/door.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "platform/pending.h"

MISSIONDATA_tag MissionData[kMissionCount];
OBJECTIVE_tag MissionObjectives[kMissionObjectivePool];
uint32_t InternalState = 5;
uint32_t MissionState = 5;
uint32_t BaseMap = 0;
uint32_t ThisOrderNum = 0;
uint32_t FailLabel = kNoLabel;
uint32_t LevelToEndTo = 0x7000048;
uint8_t sp_level[12 * 0x18];
MissionPort g_mission_port;

namespace {

constexpr uint32_t kFrontEnd = 0x7000048;  // pacote do menu principal

uint32_t FadeClr_147 = 0xff;   // cor do fade do fim da missão
float TimeOut_148 = 0.0f;      // espera antes do menu

uint32_t mp_on() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x180, 4);
    return v;
}

// A fase da missão anterior à atual (1), a atual (2) ou uma seguinte (0).
uint32_t relation(const MISSIONDATA_tag& m) {
    if (m.order < ThisOrderNum) return 1;
    return m.order == ThisOrderNum ? 2 : 0;
}

// Anúncio de um objetivo (Text_AddMsg de uma string do heap). Só no port: um texto nulo não é
// anunciado (Txt_GetStringFromHeap sempre devolve um buffer, mesmo com todos travados: o de reserva).
void announce(char* text, const char* fmt, const char* a, const char* b, uint8_t later, int16_t time) {
    if (text == nullptr) return;
    if (b == nullptr) {
        std::snprintf(text, 0x168, fmt, a != nullptr ? a : "");
    } else {
        std::snprintf(text, 0x168, fmt, a != nullptr ? a : "", b);
    }
    Text_AddMsg(0, later, 2, text, 0, time);
}

}  // namespace

// [decomp] 0x00182270 Mission_Init
// Na carga de cada fase (fora do menu): acha a fase na MissionData (a missão em BaseMap e a ordem
// em ThisOrderNum; sem missão, estado 5) e reinicia os objetivos desta fase e das seguintes da
// missão (estado 1 se um canal os revela, senão 0, e o canal do objetivo no valor inicial). Os das
// fases anteriores ficam como estavam. Num reinício frio a missão começa do zero (BaseMap = -1 se a
// fase não tem missão); começar pela primeira fase zera as estatísticas.
void Mission_Init(uint32_t level, uint16_t warm) {
    LevelToEndTo = kFrontEnd;
    if (level == kFrontEnd) return;
    if (warm == 0) BaseMap = 0xffffffffu;
    InternalState = 1;
    ThisOrderNum = 0xffffffffu;
    FailLabel = kNoLabel;
    Mission_SetStatus(kMissionRunning);
    GS_PauseGame(0);
    for (uint32_t i = 0; i < kMissionCount; ++i) {
        if (MissionData[i].level == level) {
            BaseMap = MissionData[i].base_map;
            ThisOrderNum = MissionData[i].order;
            break;
        }
    }
    if (BaseMap == 0xffffffffu) {
        InternalState = 5;
        Mission_SetStatus(kMissionNone);
        return;
    }
    for (uint32_t i = 0; i < kMissionCount; ++i) {
        if (MissionData[i].level != BaseMap) continue;
        if (level == MissionData[i].level) PlrStat_ResetForMission();
        break;
    }
    for (uint32_t i = 0; i < kMissionCount; ++i) {
        MISSIONDATA_tag& m = MissionData[i];
        if (m.base_map != BaseMap) continue;
        uint32_t rel = relation(m);
        if (rel == 1 && warm != 0) rel = 4;
        if (m.count == 0) continue;
        for (uint32_t k = 0; k < m.count; ++k) {
            OBJECTIVE_tag& o = m.objectives[k];
            if (rel != 0 && rel != 2) continue;
            o.state = o.trigger != 0 ? 1 : 0;
            if (o.channel == 0) continue;
            switch_channels[o.channel] = o.channel_init;
            if (o.channel_init != 0) switch_channels_time[o.channel] = GameState.frame;
        }
    }
}

// [decomp] 0x00182540 Mission_NumVisObjectives
// Objetivos visíveis (estado 2 a 5) das fases da missão até a atual (para o menu de pausa).
int16_t Mission_NumVisObjectives() {
    int16_t n = 0;
    for (uint32_t i = 0; i < kMissionCount; ++i) {
        const MISSIONDATA_tag& m = MissionData[i];
        if (m.base_map != BaseMap) continue;
        if (ThisOrderNum < m.order) return n;
        for (uint32_t k = 0; k < m.count; k = uint16_t(k + 1)) {
            const int32_t st = m.objectives[k].state;
            if (1 < st || st < 0) ++n;
        }
    }
    return n;
}

// [decomp] 0x00182630 Mission_ObjectiveState
// O index-ésimo objetivo visível (na ordem de Mission_NumVisObjectives): estado, rótulo do estado,
// texto e descrição. Sem ele, out fica como estava.
void Mission_ObjectiveState(OBJ_STATE* out, uint16_t index) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < kMissionCount; ++i) {
        const MISSIONDATA_tag& m = MissionData[i];
        if (m.base_map != BaseMap) continue;
        if (ThisOrderNum < m.order) return;
        for (uint32_t k = 0; k < m.count; k = uint16_t(k + 1)) {
            const OBJECTIVE_tag& o = m.objectives[k];
            const int32_t st = o.state;
            if (!(1 < st || st < 0)) continue;
            if (seen++ != index) continue;
            if (st == 2) out->status_label = 0x2000001;
            else if (st == 3) out->status_label = 0x2000000;
            else if (st == 4) out->status_label = 0x16f;
            else out->status_label = 0x170;
            out->label = o.label;
            out->text = o.text;
            out->state = st;
            return;
        }
    }
}

// [decomp] 0x001827d8 Mission_MonitorObjectives
// Um tick dos objetivos da missão (ver mission.h): anuncia, cumpre, fracassa; os canais 0x60/0x61 e
// os de 07000016/0700001b fracassam a missão (canal 99) com um rótulo de falha; tudo satisfeito
// liga o canal 100.
void Mission_MonitorObjectives() {
    bool all = true;
    for (uint32_t i = 0; i < kMissionCount; i = uint16_t(i + 1)) {
        MISSIONDATA_tag& m = MissionData[i];
        if (m.base_map != BaseMap) continue;
        const uint8_t later = ThisOrderNum <= m.order ? 1 : 0;
        for (uint32_t k = 0; k < m.count; k = uint16_t(k + 1)) {
            OBJECTIVE_tag& o = m.objectives[k];
            char* text = Txt_GetStringFromHeap(0);
            const bool inverted = (o.flags & 2) != 0;
            const bool satisfied = inverted ? switch_channels[o.channel] == 0 : switch_channels[o.channel] != 0;
            all = all && satisfied;
            switch (o.state) {
                case 0:
                    if (m.level <= GameState.level_file) {
                        announce(text, "%s", Txt_BindLabel(o.label, 0), nullptr, later, 300);
                        o.state = inverted ? 2 : 3;
                    }
                    break;
                case 1:
                    if (o.trigger != 0 && o.trigger != 0xff && switch_channels[o.trigger] != 0) {
                        announce(text, "%s", Txt_BindLabel(o.label, 0), nullptr, later, 300);
                        o.state = inverted ? 2 : 3;
                    }
                    break;
                case 2:
                    if (satisfied) break;
                    switch_channels[99] = 1;
                    switch_channels_time[99] = GameState.frame;
                    announce(text, "%s: %s", Txt_BindLabel(0x170, 0), Txt_BindLabel(o.label, 0), later, 0xb4);
                    o.state = 5;
                    break;
                case 3:
                    if (satisfied) {
                        announce(text, "%s", Txt_BindLabel(0x2000053, 0), nullptr, later, 0xb4);
                        o.state = 4;
                    }
                    break;
                case 5:
                    if (o.fail_label != kNoLabel) FailLabel = o.fail_label;
                    break;
                default:
                    break;
            }
        }
    }
    if (switch_channels[0x61] != 0) {
        switch_channels_time[99] = GameState.frame;
        FailLabel = 0x4000032;
        switch_channels[99] = 1;
    }
    if (switch_channels[0x60] != 0) {
        FailLabel = 0x4000034;
        switch_channels_time[99] = GameState.frame;
        switch_channels[99] = 1;
    }
    bool fail = false;
    if (GameState.level_file == 0x7000016) {
        if (switch_channels[0x7b] != 0) {
            FailLabel = 0x2000059;
            fail = true;
        }
    } else if (GameState.level_file == 0x700001b && switch_channels[199] != 0) {
        FailLabel = 0x200005a;
        fail = true;
    }
    if (fail) {
        switch_channels[99] = 1;
        switch_channels_time[99] = GameState.frame;
    }
    if (all) {
        switch_channels[100] = 1;
        switch_channels_time[100] = GameState.frame;
        Music_Event(8, 1);
    }
}

// [decomp] 0x00182c60 Mission_Update
// Um tick da missão (fora do menu e do multijogador; ver mission.h). Passos: 1 em andamento
// (morte, objetivos, canais 99 e 100); 2 fracassada por objetivo; 3 fracassada por morte;
// 6 cumprida (LevelToEndTo pela fase); 7 conta o tempo da mensagem; 4 depois dele: cumprida, carrega
// LevelToEndTo com um fade; fracassada, o menu de fim de missão; 5 nada.
void Mission_Update() {
    if (GameState.level_file == kFrontEnd) return;
    if (mp_on() != 0) return;
    switch (InternalState) {
        case 1:
            if (switch_channels[0x62] != 0) {
                Music_Event(6, 1);
                InternalState = 3;
                FadeClr_147 = 0xff0000ffu;
                if (glb_players[0] != nullptr) Player_SetHealth(bloke_of(glb_players[0]), 0.0f);
                return;
            }
            Mission_MonitorObjectives();
            if (switch_channels[99] != 0) {
                Music_Event(7, 1);
                InternalState = 2;
            } else if (switch_channels[100] != 0) {
                Music_Event(8, 1);
                InternalState = 6;
                FadeClr_147 = 0xff;
            }
            return;
        case 2: {
            GameState.unknown_24 = 1;  // fim da missão: sem menu de pausa (Player_Update)
            MissionState = kMissionFailed;
            LevelToEndTo = kFrontEnd;
            InternalState = 7;
            Text_AddMsg(0, 0, 3, Txt_BindLabel(0x2000007, 0), 0, int16_t(VIDEO_FRAME_RATE << 2));
            TimeOut_148 = float(uint32_t((VIDEO_FRAME_RATE & 0xffff) * 4 + VIDEO_FRAME_RATE));
            return;
        }
        case 3:
            TimeOut_148 = 1.0f;
            GameState.unknown_24 = 1;
            MissionState = kMissionDied;
            InternalState = 7;
            LevelToEndTo = kFrontEnd;
            return;
        case 4:
            if (MissionState == kMissionComplete) {
                // GameState +0x48: a missão mais adiantada já cumprida (ordem do menu).
                const uint32_t done = Menu_GetLevelIndex(BaseMap);
                const uint32_t best = Menu_GetLevelIndex(GameState.unknown_44[1]);
                if (int32_t(best) < int32_t(done)) {
                    GameState.unknown_44[1] = BaseMap;
                    if (mp_on() != 0) GameState.unknown_44[1] = GameState.level_file;
                }
                GameState.menu_page = 0x40000036;
                ResetMap_LevelToLoad(LevelToEndTo, 0, 0);
                GameFlow_PushState(GF_FADE_REQUEST, 60.0f, FadeClr_147);
                InternalState = 5;
            } else {
                // O menu de fim de missão (P_ENDMISSION_Handler): tentar de novo, recomeçar, sair.
                MenuManager_Create(0x80000004, 0x40000042, 0, 0, 0x28, 0, 4);
                GS_PauseGame(1);
                InternalState = 5;
            }
            return;
        case 5:
            return;
        case 6: {
            MissionState = kMissionComplete;
            InternalState = 7;
            GameState.unknown_24 = 1;
            switch (GameState.level_file) {
                case 0x7000004: LevelToEndTo = 0x7300004; break;
                case 0x7000008: LevelToEndTo = 0x7300008; break;
                case 0x700000b: LevelToEndTo = 0x730000b; break;
                case 0x700000d: LevelToEndTo = 0x730000d; break;
                case 0x700000f: LevelToEndTo = 0x730000f; break;
                case 0x7000013: LevelToEndTo = 0x7300013; break;
                case 0x7000016: LevelToEndTo = 0x7300017; break;
                case 0x700001b: LevelToEndTo = 0x730001b; break;
                default: LevelToEndTo = kFrontEnd; break;
            }
            Text_AddMsg(0, 0, 3, Txt_BindLabel(0x2000006, 0), 0, int16_t(VIDEO_FRAME_RATE << 2));
            TimeOut_148 = float(int32_t((VIDEO_FRAME_RATE & 0xffff) << 2));
            return;
        }
        case 7:
            GS_PausePlayer(1, 0);
            TimeOut_148 = TimeOut_148 - FRAME_RATE_MUL;
            if (TimeOut_148 <= 0.0f) {
                GS_PauseGame(1);
                InternalState = 4;
            }
            return;
        default:
            InternalState = 1;
            return;
    }
}

// [decomp] 0x00183098 Mission_SetFailLabel
void Mission_SetFailLabel(uint32_t label) { FailLabel = label; }

// [decomp] 0x001830a0 Mission_BaseMapHCode
uint32_t Mission_BaseMapHCode() { return mp_on() == 0 ? BaseMap : GameState.level_file; }

// [decomp] 0x001830c0 Mission_SetMapHCode
void Mission_SetMapHCode(uint32_t hash) { BaseMap = hash; }

// [decomp] 0x001830c8 Mission_FailLabel
uint32_t Mission_FailLabel() { return FailLabel; }

// [decomp] 0x001830d0 Mission_Status
int32_t Mission_Status() { return int32_t(MissionState); }

// [decomp] 0x001830d8 Mission_SetStatus
void Mission_SetStatus(uint32_t state) { MissionState = state; }

// ---- só no port ----

MissionSummary Mission_Summary() {
    MissionSummary s;
    if (BaseMap == 0xffffffffu) return s;
    for (const MISSIONDATA_tag& m : MissionData) {
        if (m.base_map != BaseMap || m.objectives == nullptr) continue;
        for (uint32_t k = 0; k < m.count; ++k) {
            const int32_t st = m.objectives[k].state;
            ++s.total;
            if (st == 4) ++s.done;
            if (st == 5) ++s.failed;
            if (1 < st || st < 0) ++s.visible;
        }
    }
    return s;
}

int Mission_Describe(char* out, size_t size) {
    const MissionSummary s = Mission_Summary();
    return std::snprintf(out, size, "objetivos %d/%d fracassados %d visiveis %d missao %u passo %u base %08x ordem %d falha %08x",
                         s.done, s.total, s.failed, s.visible, MissionState, InternalState, BaseMap, int32_t(ThisOrderNum),
                         FailLabel);
}

void mission_debug_power_on() {
    std::memset(MissionData, 0, sizeof(MissionData));
    std::memset(MissionObjectives, 0, sizeof(MissionObjectives));
    InternalState = 5;
    MissionState = 5;
    BaseMap = 0;
    ThisOrderNum = 0;
    FailLabel = kNoLabel;
    LevelToEndTo = kFrontEnd;
    FadeClr_147 = 0xff;
    TimeOut_148 = 0.0f;
    std::memset(sp_level, 0, sizeof(sp_level));
    g_mission_port = MissionPort{};
}
