// A missão (driving/game/mission.h), portada do MIPS do DRIVING.ELF (SMissionManager.obj). As desmontagens ficam em
// out/agent_drv_mission/ do checkout principal (fora do git).
//
// Divergências (port), cada uma também na função: as listas em vetores (MissionLists); os elementos da IA de
// mission_ai() (vivos e parados); as chamadas a sistemas não portados só anotadas (mission_trace) e contadas; o
// SRulePlayerDir e o SRuleAmmo devolvem 0; os índices fora dos vetores (cronômetros, etapas, contadores) viram guardas.

#include "driving/game/mission.h"

#include <cstring>
#include <memory>

#include "driving/ai/element_controller.h"
#include "driving/game/ai_character.h"
#include "driving/game/bond_car.h"
#include "driving/game/events.h"
#include "driving/game/feedback.h"
#include "driving/game/hud.h"
#include "driving/game/scheduler.h"
#include "driving/game/target_picker.h"
#include "driving/game/triggers.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
#include "platform/crt_compat.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

constexpr uint32_t kOne = 0x3f800000u;

// O SMissionManager vive no heap do PS2 (o `new` do Construct); aqui num bloco do port que não muda de lugar (os p32
// dos eventos e do carro apontam para ele).
std::unique_ptr<SMissionManagerData> g_manager;
bool g_exists = false;
MissionLists g_lists;
MissionAI g_ai;
MissionStats g_stats;
MissionEnv g_env;
std::vector<MissionCall> g_trace;
bool g_trace_enabled = false;
UberBondBlock g_uber{};
// As configurações padrão (0x4162b8, preenchidas pelo construtor quando a pista não tem 'MSet').
MissionSettings g_default_settings{};

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// |v| pela VU0 (vmul.xyz; vmulax.x, vmadday.x, vmaddz.x; vsqrt; vaddq.x).
float vu_length(const float v[3]) {
    uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[1]), bits(v[1])));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2])));
    return flt(ps2::sqrt(acc));
}
// v / |v| pela VU0 (... vrsqrt Q,vf0w; vmulq.xyz).
void vu_normalize(float v[3]) {
    uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[1]), bits(v[1])));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2])));
    const uint32_t q = ps2::rsqrt(kOne, acc);
    for (int i = 0; i < 3; ++i) v[i] = flt(ps2::mul(bits(v[i]), q));
}

void call(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    mission_note_call(fn, a0, a1, a2, a3);
}

RigidBody* player_body() {
    BondCar* car = mission_player_car();
    return car ? car->body() : nullptr;
}

// As funções externas que a conferência acompanha (os endereços do PS2).
constexpr uint32_t kFnWeaponReset = 0x20bbe0, kFnLoadWeapon = 0x20d378, kFnSetSecondary = 0x20d488;
constexpr uint32_t kFnActivateTargeting = 0x22b638, kFnAIDisableBlowOuts = 0x193240;
constexpr uint32_t kFnCalcMightAsWellBeDead = 0x12c678, kFnCalcEndScores = 0x207f20;
constexpr uint32_t kFnSetExitToDriving = 0x174b80, kFnStylin = 0x23c820, kFnMenuSound = 0x2f4b10;
constexpr uint32_t kFnSyncRuleInfo = 0x12daf0, kFnRestart = 0x156698;

}  // namespace

// ---------------------------------------------------------------- os globais

UberBondBlock& uber_bond() { return g_uber; }

void uber_bond_defaults(const char* track, int32_t difficulty) {
    std::memset(&g_uber, 0, sizeof g_uber);
    const char* name = track ? track : "";
    for (int i = 0; i < 15 && name[i]; ++i) g_uber.text(0x4f4)[i] = name[i];
    const char loc[] = "IDL_ENG.loc";
    std::memcpy(g_uber.text(0x540), loc, sizeof loc);
    g_uber.word(0x960) = 1;      // válido
    g_uber.word(0x964) = 100;    // os volumes
    g_uber.word(0x968) = 100;
    g_uber.word(0x984) = difficulty;
    g_uber.word(0x994) = 1;      // a vibração
}

MissionStats& mission_stats() { return g_stats; }
MissionEnv& mission_env() { return g_env; }
std::vector<MissionCall>& mission_trace() { return g_trace; }
bool& mission_trace_enabled() { return g_trace_enabled; }

void mission_note_call(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    ++g_stats.external_calls;
    if (g_trace_enabled) g_trace.push_back({fn, {a0, a1, a2, a3}});
}

MissionAIElement* MissionAI::find(uint32_t element) {
    for (MissionAIElement& e : elements)
        if (e.element == element) return &e;
    return nullptr;
}

MissionAIElement& MissionAI::get(uint32_t element) {
    if (MissionAIElement* e = find(element)) return *e;
    MissionAIElement e;
    e.element = element;
    // O ponto do elemento: as três primeiras palavras do registro do 'AIEl' (port: o ponto de partida; o PS2 lê o do
    // AIElement vivo).
    if (const float* p = nf::p32_ptr<const float>(element)) std::memcpy(e.pos, p, sizeof e.pos);
    elements.push_back(e);
    return elements.back();
}

MissionAI& mission_ai() { return g_ai; }

SMissionManagerData& mission_manager() {
    if (!g_manager) g_manager = std::make_unique<SMissionManagerData>();
    return *g_manager;
}
MissionLists& mission_lists() { return g_lists; }
bool mission_exists() { return g_exists; }

void mission_reset() {
    for (auto& l : g_lists.rules) {
        for (SMissionRuleData* r : l) delete r;
        l.clear();
    }
    for (SMissionObjective* o : g_lists.objectives) delete o;
    g_lists.objectives.clear();
    for (SMissionMessage* m : g_lists.messages) delete m;
    g_lists.messages.clear();
    g_ai = MissionAI{};
    g_stats = MissionStats{};
    g_trace.clear();
    g_exists = false;
    if (g_manager) std::memset(g_manager.get(), 0, sizeof(SMissionManagerData));
}

BondCar* mission_player_car() {
    Simulation& sim = simulation();
    return sim.cars.empty() ? nullptr : sim.cars[0];
}

// ---------------------------------------------------------------- o cronômetro

// [driving] 0x00209bb0 SMissionTimer::SMissionTimer
void SMissionTimer_Construct(SMissionTimer& t) {
    t.active = 0;
    t.ticking = 0;
    t.value = 0;
    t.last_second = 0;
}

// [driving] 0x00209bf0 SMissionTimer::Reset(int)
void SMissionTimer_Reset(SMissionTimer& t, int32_t value) {
    t.value = value;
    t.start_step = simulation().steps;
}

// [driving] 0x00209c08 SMissionTimer::Start(int)
void SMissionTimer_Start(SMissionTimer& t, int32_t rate) {
    if (t.active != 0) return;
    t.rate = rate;
    t.active = 1;
    t.start_step = simulation().steps;
}

// [driving] 0x00209c38 SMissionTimer::Stop
void SMissionTimer_Stop(SMissionTimer& t) {
    if (t.active == 0) return;
    t.value = SMissionTimer_GetValue(t);
    t.active = 0;
}

// [driving] 0x00209c78 SMissionTimer::Increment(int)
void SMissionTimer_Increment(SMissionTimer& t, int32_t n) { t.value += n; }

// [driving] 0x00209c88 SMissionTimer::GetValue: o valor parado, ou o valor + rate x (os passos desde o começo); com o
// bipe ligado, um AMenuSound::Trigger(0, "SFX_ReverseBeep", "Gadgets", 2) a cada segundo novo (anotado).
// Guarda: com a taxa de quadros 0 o PS2 para na divisão (trap 7); aqui nenhum bipe.
int32_t SMissionTimer_GetValue(SMissionTimer& t) {
    if (t.active == 0) return t.value;
    const Simulation& sim = simulation();
    const int32_t v = t.value + t.rate * (sim.steps - t.start_step);
    if (t.ticking != 0) {
        if (sim.frame_rate == 0) {
            ++g_stats.guards;
            return v;
        }
        const int32_t s = v / sim.frame_rate;
        if (s != t.last_second && s > 0) {
            call(kFnMenuSound, 0, 0x3979f8, 0x397a08, 2);
            t.last_second = s;
        }
    }
    return v;
}

// [driving] 0x00209d48 SMissionTimer::IsReverse
bool SMissionTimer_IsReverse(const SMissionTimer& t) { return t.rate < 0; }

SMissionTimer* mission_timer(SMissionManagerData& m, int32_t i) {
    if (i < 0 || i >= 10) {
        ++g_stats.guards;  // o PS2 escreveria fora dos dez
        return nullptr;
    }
    return &m.timers[i];
}

// ---------------------------------------------------------------- o gerente

// [driving] 0x00205188 (SMissionManager::Construct) e 0x00204de8 SMissionManager::SMissionManager
// Divergência: as listas (FastAlloc dos nós-cabeça) são as do port; o ActionQueue (+0x36c) não existe (0); o PS2 faz
// o new a cada WWorld::Open sem apagar o anterior (o port reaproveita o bloco).
void SMissionManager_Construct(bool accumulate_scores) {
    mission_reset();
    SMissionManagerData& m = mission_manager();
    std::memset(&m, 0, sizeof m);
    for (SMissionTimer& t : m.timers) SMissionTimer_Construct(t);
    for (MissionSectionProfile& p : m.profiles) {
        // [driving] 0x002092d8 MissionSectionProfile::Clear
        p.used = 0;
        p.min = 1000.0f;
        p.max = 0;
        p.sum = 0;
        p.count = 0;
    }
    m.rules = 0;
    m.state = 0;
    m.auto_drive = 0;
    m.missile_target = 0;
    m.unknown240 = -1;
    m.action_queue = 0;
    for (float& f : m.missile_steer) f = 0.0f;
    m.missile = 0;
    m.difficulty = g_uber.word(0x984);
    m.loads = 0;
    // As configurações padrão (MEM_fill de 0x2f8 bytes, "vanquish", a vida 3000 de 3000, o escudo 0 de 1000).
    std::memset(&g_default_settings, 0, sizeof g_default_settings);
    std::memcpy(g_default_settings.car_type, "vanquish", 9);
    g_default_settings.shield_max = 1000;
    g_default_settings.health = 3000;
    g_default_settings.health_max = 3000;
    g_default_settings.shield = 0;
    m.settings = nf::p32_u(&g_default_settings);
    SMissionManager_SetCarType(m);
    if (!accumulate_scores) SMissionManager_ClearScoreBuffer(g_uber);
    for (int i = 0; i < 12; ++i) m.uber_state[i] = g_uber.word(0x504 + 4 * uint32_t(i));
    SMissionManager_ClearState(m);
    SMissionManager_RestoreLives(m);
    g_exists = true;
}

// [driving] 0x00204d18 SMissionManager::SetCarType: o veículo das configurações; o "db5" e o "z8" trocados pelas
// trapaças (0x33a010..0x33a020, que o port não tem: ficam).
void SMissionManager_SetCarType(SMissionManagerData& m) { m.car_type = m.settings; }

// [driving] 0x002051f8 SMissionManager::GetCarType (o bit 0 do UberBond +0x9a1: "jungle_truck")
const char* SMissionManager_GetCarType(const SMissionManagerData& m) {
    if (g_uber.bytes[0x9a1] & 1) return "jungle_truck";
    return nf::p32_ptr<const char>(m.car_type);
}

// [driving] 0x00205220 SMissionManager::Load(UData *, UData *)
// Divergência: as regras vão para os vetores do port; uma regra de tipo desconhecido (o PS2 põe um nulo na lista, que
// o DoRule leria no endereço 0) e uma etapa fora das 64 não entram (guardas).
void SMissionManager_Load(SMissionManagerData& m, UData* rules, UData* mset) {
    const uint8_t* rec = UData_Data(rules);
    int32_t n = int32_t(rules->count);
    SMissionManager_Unload(m);
    for (; n > 0; --n, rec += 0x20) {
        SMissionRuleData* r = SMissionRule_Instantiate(rec);
        int32_t stage;
        std::memcpy(&stage, rec + 8, 4);
        if (!r) {
            ++g_stats.guards;
            continue;
        }
        if (stage < 0) {
            g_lists.rules[0].push_back(r);
        } else if (stage < 64) {
            g_lists.rules[1 + stage].push_back(r);
            if (!(stage < m.num_stages)) m.num_stages = stage + 1;
        } else {
            ++g_stats.guards;
            delete r;
        }
    }
    m.rules = nf::p32_u(rules);
    m.mset = nf::p32_u(mset);
    m.settings = mset ? nf::p32_u(UData_Data(mset)) : nf::p32_u(&g_default_settings);
    SMissionManager_SetCarType(m);
    SMissionManager_LoadWeaponSet(m);
    SMissionManager_ResetCharInfo(m);
    MissionSettings* s = nf::p32_ptr<MissionSettings>(m.settings);
    if (ee::eq(s->scale_a[0], 0.0f)) {
        s->scale_a[0] = 1.0f;
        s->scale_a[1] = 1.0f;
        s->scale_a[2] = 1.0f;
    }
    if (ee::eq(s->scale_b[0], 0.0f)) {
        s->scale_b[0] = 1.0f;
        s->scale_b[1] = 1.0f;
        s->scale_b[2] = 1.0f;
    }
    const int32_t mode = g_uber.word(0xa3c);
    if (mode == 3 || mode == 4) SMissionManager_ProgrammerDefinedEvent(m, 5);
    m.loads += 1;
}

// [driving] 0x00205788 SMissionManager::Unload (com 0x00206280 DestroyRuleList: o destrutor de cada regra)
void SMissionManager_Unload(SMissionManagerData& m) {
    for (auto& l : g_lists.rules) {
        for (SMissionRuleData* r : l) delete r;
        l.clear();
    }
    m.stage = 0;
    m.num_stages = 0;
    m.next_stage = 0;
}

// [driving] 0x00205498 SMissionManager::LoadWeaponSet: as armas da missão no SWeaponManager (driving/game/weapons.h; as
// chamadas também anotadas, para a parte 15 do nf_driving_check, que não tem o gerente das armas).
void SMissionManager_LoadWeaponSet(SMissionManagerData& m) {
    SWeaponManagerData* wm = weapon_manager();
    auto load = [&](int32_t w, int32_t ammo, int32_t a1, int32_t a2) {
        call(kFnLoadWeapon, uint32_t(w), uint32_t(ammo), uint32_t(a1), uint32_t(a2));
        if (wm) SWeaponManager_LoadWeapon(*wm, w, ammo, a1, a2);
    };
    auto set_secondary = [&](int32_t w) {
        call(kFnSetSecondary, uint32_t(w));
        if (wm) SWeaponManager_SetSecondary(*wm, w);
    };
    call(kFnWeaponReset);
    if (wm) SWeaponManager_Reset(*wm);
    int32_t secondary = -1;
    if (m.settings == nf::p32_u(&g_default_settings)) {
        if (std::strcmp(g_uber.text(0x4f4), "uw_map") == 0) {
            for (int32_t w : {0x15, 0x16, 3, 7}) load(w, 99, 0, 0);
            set_secondary(0x15);
        } else {
            for (int32_t w : {0, 1, 2, 0xe, 10, 0xb, 3, 0xf}) load(w, 99, 0, 0);
            set_secondary(1);
        }
        return;
    }
    const MissionSettings* s = nf::p32_ptr<const MissionSettings>(m.settings);
    for (int32_t i = 0; i < 0x20; ++i) {
        if (i == 0x17 || s->weapon_on[i] == 0) continue;
        if (s->weapon_ammo[i] >= 0x2711) continue;
        load(i, s->weapon_ammo[i], s->weapon_arg1[i], s->weapon_arg2[i]);
        bool pick = false;
        if (secondary == -1) {
            if (i == 0) continue;
            pick = s->weapon_ammo[i] >= 1;
        }
        if (pick) secondary = i;
    }
    if (secondary == -1) {
        load(0, 0, 0, 0);
        secondary = 0;
    } else if (secondary == 0x1b && s->weapon_on[0x1c] != 0 && s->weapon_ammo[0x1c] > 0) {
        if (nf::str_icmp(g_uber.text(0x4f4), "jungleb_mis13b") == 0) secondary = 0x1c;
    }
    {
        const int32_t sec = secondary;
        call(kFnSetSecondary, uint32_t(sec));
        if (wm) SWeaponManager_SetSecondary(*wm, sec);
    }
    call(kFnActivateTargeting, 0);
    WTargetPicker_ActivateTargeting(0);
}

// [driving] 0x002057f0 SMissionManager::ClearState
void SMissionManager_ClearState(SMissionManagerData& m) {
    m.num_stages = 0;
    m.stage = 0;
    m.next_stage = 0;
    m.shots_fired = 0;
    m.shots_hit = 0;
    m.kills = 0;
    m.player_damage = 0;
    m.cinematic_mode = 0;
    m.objective_check_step = 0;
    m.objective_seen_step = 0;
    m.unknown3d8[0] = 0;
    m.unknown3d8[1] = 0;
    m.last_damage_step = 0;
    m.stylin = 0;
    m.end_step = 0;
    m.skipping_cinematic = 0;
    m.unknown5ec = 0;
    m.last_shot_step = 0;
    if (feedback_state().paused != 0) m.unknown5ec = 1;  // 0x32e690
    for (int i = 0; i < 2; ++i) {
        m.objective_kind[i] = 0;
        m.objective_visible[i] = 0;
    }
    for (SMissionTimer& t : m.timers) {
        SMissionTimer_Reset(t, 0);
        SMissionTimer_Stop(t);
    }
    SMissionManager_ResetCharInfo(m);
    for (int32_t& e : m.programmer_events) e = 0;
    for (int32_t& s : m.scores) s = 0;
    for (MissionTimedAction& t : m.timed) t.active = 0;
    for (int i = 0; i < 64; ++i) {
        m.magic_counters[i] = 0;
        int32_t th = -1;
        switch (i) {
            case 6: case 7: case 10: th = 6; break;
            case 8: th = 3; break;
            case 9: th = 2; break;
            case 0x13: th = 4; break;
            default: break;
        }
        m.magic_thresholds[i] = th;
    }
    if (m.unknown5ec != 0) SMissionManager_ProgrammerDefinedEvent(m, 0x18);
    m.unknown1b3c = 0;
    m.current_profile = -1;
    for (MissionSectionProfile& p : m.profiles) {
        p.used = 0;
        p.min = 1000.0f;
        p.max = 0;
        p.sum = 0;
        p.count = 0;
    }
    SMissionManager_ClearObjectiveList(m);
    SMissionManager_ClearMessageList(m);
}

// [driving] 0x00205a38 SMissionManager::Reset (o ActionQueue::Flush do +0x36c: o port não o tem)
void SMissionManager_Reset(SMissionManagerData& m) {
    SMissionTimer_Reset(m.timers[0], 0);
    SMissionManager_LoadWeaponSet(m);
    m.state = 0;
    SMissionManager_ChangeStage(m, 0);
    SMissionManager_ClearState(m);
}

// [driving] 0x00205ac8 SMissionManager::Start: 1, salvo ganha ou perdida (3 e 4).
void SMissionManager_Start(SMissionManagerData& m) {
    if (uint32_t(m.state - 3) > 1u) m.state = 1;
}

// [driving] 0x00205ae8 SMissionManager::Stop
void SMissionManager_Stop(SMissionManagerData& m) {
    if (uint32_t(m.state - 3) > 1u) m.state = 2;
}

// [driving] 0x00205b08 SMissionManager::Win
// Divergência: o AIElementController (os inimigos que "podiam estar mortos", somados às mortes) e o
// CalcEndMissionScores (a pontuação do fim, 1220 bytes, que lê as estatísticas da IA) não foram portados: anotados;
// o "CHAIN_NEXT_MISSION" da coleção do mundo (Bond_SetExitToDriving) também.
void SMissionManager_Win(SMissionManagerData& m) {
    m.state = 3;
    call(kFnCalcMightAsWellBeDead);
    // + AIElementController::GetNumMightAsWellBeDead (sem o controlador, o port de antes: 0)
    if (ai_controller()) {
        AIElementController_CalcMightAsWellBeDead();
        m.kills += AIElementController_GetNumMightAsWellBeDead();
    }
    call(kFnCalcEndScores);
    if (m.end_step == 0) {
        const Simulation& sim = simulation();
        m.end_step = ee::cvt_w(ee::mul(ee::cvt_s(sim.steps), sim.dt));
    }
    if (SMissionManager_IsSkippingCinematic(m)) SMissionManager_SetCinematicMode(m, 0);
}

// [driving] 0x00205bf0 SMissionManager::Lose
void SMissionManager_Lose(SMissionManagerData& m) {
    m.state = 4;
    if (SMissionManager_IsSkippingCinematic(m)) SMissionManager_SetCinematicMode(m, 0);
}

// [driving] 0x00206108 SMissionManager::RestoreLives (o UberBond +0x53c: 3 vidas em 0 e 1, 2 em 2)
void SMissionManager_RestoreLives(SMissionManagerData& m) {
    const int32_t d = m.difficulty;
    if (d == 0 || d == 1) g_uber.word(0x53c) = 3;
    else if (d == 2) g_uber.word(0x53c) = d;
}

// [driving] 0x00206168 SMissionManager::ClearScoreBuffer(UberBond &)
void SMissionManager_ClearScoreBuffer(UberBondBlock& u) {
    for (int i = 0; i < 12; ++i) {
        u.word(0x5e4 + 4 * uint32_t(i) - 0xe0) = 0;
        u.word(0x5e4 + 4 * uint32_t(i)) = 0;
    }
    u.word(0x534) = 0;
    u.word(0x580) = 0;
    for (uint32_t o = 0x588; o <= 0x5cc; o += 4) u.word(o) = 0;
    u.word(0x61c) = 0;
    u.word(0x620) = 0;
    for (uint32_t r = 0; r < 9; ++r)
        for (uint32_t k = 0; k < 3; ++k) u.word(0x9a8 + r * 0xc + k * 4) = 0;
}

// [driving] 0x00206228 SMissionManager::ProcessRuleList: cada regra da lista (DoRule). A lista pode crescer? Não:
// nada insere regras durante o Update; o laço do PS2 relê o próximo nó a cada volta, o port o índice.
void SMissionManager_ProcessRuleList(SMissionManagerData&, int list) {
    if (list < 0 || list >= 65) {
        ++g_stats.guards;
        return;
    }
    std::vector<SMissionRuleData*>& l = g_lists.rules[list];
    for (size_t i = 0; i < l.size(); ++i) SMissionRule_DoRule(*l[i]);
}

// [driving] 0x00206340 SMissionManager::ProcessRules: com a missão rodando, a lista de sempre e a da etapa; depois a
// próxima etapa vira a corrente.
void SMissionManager_ProcessRules(SMissionManagerData& m) {
    if (m.state != 1) return;
    SMissionManager_ProcessRuleList(m, 0);
    if (m.stage < 0 || m.stage >= 64) ++g_stats.guards;
    else SMissionManager_ProcessRuleList(m, 1 + m.stage);
    m.stage = m.next_stage;
}

// [driving] 0x00206338 SMissionManager::ChangeStage
void SMissionManager_ChangeStage(SMissionManagerData& m, int32_t stage) { m.next_stage = stage; }

// [driving] 0x00206310 (sem nome: SMissionManager::CallStage, o ~ECallStage): a lista da etapa já.
void SMissionManager_CallStage(SMissionManagerData& m, int32_t stage) {
    if (stage < 0 || stage >= 64) {
        ++g_stats.guards;
        return;
    }
    SMissionManager_ProcessRuleList(m, 1 + stage);
}

// [driving] 0x00206b88 SMissionManager::Update
// Divergência: o "dia da marmota" (o UberBond +0x62c: reiniciar a missão num passo dado, depuração) cria um ERestart
// que o port não executa (conta).
void SMissionManager_Update(SMissionManagerData& m) {
    if ((g_uber.word(0x9a4) & 1) && g_env.scheduler) g_env.scheduler->time_scale = 2.0f;
    if (SMissionManager_IsSkippingCinematic(m) && (m.cinematic_mode == 0 || m.state != 1))
        SMissionManager_ClearCinematicMode(m, 0xffffffffu);
    if (SMissionManager_IsSkippingCinematic(m) && g_env.scheduler) g_env.scheduler->cinematic = 1;
    SMissionManager_UpdateAutoDriveMissile(m);
    SMissionManager_UpdateCharInfo(m);
    SMissionManager_UpdateObjectiveSpecial(m);
    SMissionManager_UpdateTimedActions(m);
    SMissionManager_UpdateSectionProfile(m);
    SMissionManager_ProcessRules(m);
    const int32_t at = g_uber.word(0x62c);
    if (at > 0 && at < simulation().steps) {
        g_uber.word(0x630) += 1;
        ++g_stats.restarts;
        call(kFnRestart);
        event_create_simple(kEventRestart);
    }
}

// [driving] 0x00206ca0 SMissionManager::UpdateAutoDriveMissile: o míssil guiado (+0x358) pela fila de ações; sem
// míssil (o port não tem o SWeaponManager), nada.
void SMissionManager_UpdateAutoDriveMissile(SMissionManagerData& m) {
    if (m.missile != 0) ++g_stats.guards;
}

// [driving] 0x00207228 SMissionManager::UpdateCharInfo: com o HUD fora da pausa, o "SUPER EASY" (o bit 0 do UberBond
// +0x9a0) devolve vida abaixo de 33% e o bit 2 a deixa no máximo (10000).
void SMissionManager_UpdateCharInfo(SMissionManagerData& m) {
    if (!hud_exists()) return;  // o PS2 também avisa uma vez (printf)
    if (hud().paused != 0) return;
    const Simulation& sim = simulation();
    if ((g_uber.word(0x9a0) & 1) && ee::lt(SMissionManager_GetPlayerHealth(m, true), flt(0x3ea8f5c3u)) &&
        sim.steps - m.last_damage_step >= 0) {
        const float max = m.player_health_max;
        float h = ee::add(m.player_health, ee::mul(ee::div(flt(0x3c23d70au), ee::cvt_s(sim.frame_rate)), max));
        if (ee::lt(max, h)) h = max;
        m.player_health = h;
    }
    if (g_uber.word(0x9a0) & 4) {
        m.player_health = 10000.0f;
        m.player_health_max = 10000.0f;
    }
}

// [driving] 0x00207ab0 SMissionManager::UpdateObjectiveSpecial: o objetivo à vista (a cada 10 passos) e o "caminho
// errado": fora do raio do radar e andando para longe do objetivo (o produto da direção com o rumo ao objetivo até
// -0,7) por mais de 5 s, o evento do programador 4 (que a regra de Paris mostra como texto).
// Divergência: os objetivos que são elementos da IA (o DisableTyreBlowOuts do carro do elemento) não existem no port.
void SMissionManager_UpdateObjectiveSpecial(SMissionManagerData& m) {
    Simulation& sim = simulation();
    for (int i = 0; i < 2; ++i)
        if (m.objective_kind[i] == 3) ++g_stats.ai_queries;
    if (!(sim.steps < m.objective_check_step)) {
        for (int i = 0; i < 2; ++i) m.objective_visible[i] = SMissionManager_CheckObjectiveVisibleToUser(m, i) ? 1 : 0;
        m.objective_check_step = sim.steps + 10;
    }
    const float* obj = SMissionManager_GetCurrObjective(m, 1);
    if (!obj) obj = SMissionManager_GetCurrObjective(m, 0);
    bool seen = true;
    RigidBody* body = player_body();
    if (obj && body) {
        float p[3], o[3];
        std::memcpy(p, body->pos, sizeof p);  // PhysicsObject::GetPosition (o corpo rígido +0x10)
        std::memcpy(o, obj, sizeof o);
        const float radius = hud_exists() ? 16.0f : 16.0f;  // GHud::RadarRadiusInMeters
        float d[3];
        for (int k = 0; k < 3; ++k) d[k] = flt(ps2::sub(bits(p[k]), bits(o[k])));
        if (!ee::lt(vu_length(d), radius)) {
            float h[3];
            if (ee::lt(flt(0x3dcccccdu), vu_length(body->vel))) std::memcpy(h, body->vel, sizeof h);
            else std::memcpy(h, body->info->orient[2], sizeof h);
            float t[3];
            for (int k = 0; k < 3; ++k) t[k] = flt(ps2::sub(bits(o[k]), bits(p[k])));
            vu_normalize(h);
            vu_normalize(t);
            const uint32_t x = ps2::mul(bits(h[0]), bits(t[0]));
            const uint32_t y = ps2::mul(bits(h[1]), bits(t[1]));
            const uint32_t z = ps2::mul(bits(h[2]), bits(t[2]));
            const float dot = flt(ps2::add(ps2::add(x, y), z));
            seen = ee::lt(flt(0xbf333333u), dot);
        }
    }
    if (seen) m.objective_seen_step = sim.steps;
    if (sim.frame_rate * 5 < sim.steps - m.objective_seen_step) {
        SMissionManager_ProgrammerDefinedEvent(m, 4);
        m.objective_seen_step = sim.steps;
    } else {
        SMissionManager_ClearProgrammerEvent(m, 4);
    }
}

// [driving] 0x00207500 SMissionManager::UpdateTimedActions: passado o prazo, completa.
void SMissionManager_UpdateTimedActions(SMissionManagerData& m) {
    const Simulation& sim = simulation();
    for (int i = 0; i < 8; ++i) {
        MissionTimedAction& t = m.timed[i];
        if (t.active == 0) continue;
        if (ee::lt(t.limit, ee::mul(ee::cvt_s(sim.steps - t.start_step), sim.dt))) SMissionManager_CompleteTimedAction(m, i);
    }
}

// [driving] 0x00207378 SMissionManager::StartTimedAction(int, float alvo, float prazo, int mínimo, int máximo)
// (o port junta os dois floats: target = f12, limit = f13)
void SMissionManager_StartTimedAction(SMissionManagerData& m, int32_t i, float seconds) {
    if (i < 0 || i >= 8) {
        ++g_stats.guards;
        return;
    }
    MissionTimedAction& t = m.timed[i];
    if (t.active != 0) return;
    t.active = 1;
    t.target = seconds;
    t.start_step = simulation().steps;
}

// [driving] 0x002073d0 SMissionManager::CompleteTimedAction: os pontos pelo tempo gasto (points_max até target, a reta
// até points_min no prazo) somados como AddScore(1).
void SMissionManager_CompleteTimedAction(SMissionManagerData& m, int32_t i) {
    if (i < 0 || i >= 8) {
        ++g_stats.guards;
        return;
    }
    MissionTimedAction& t = m.timed[i];
    if (t.active == 0) return;
    t.active = 0;
    const Simulation& sim = simulation();
    const float elapsed = ee::mul(ee::cvt_s(sim.steps - t.start_step), sim.dt);
    int32_t points = t.points_min;
    if (ee::lt(elapsed, t.target)) {
        points = t.points_max;
    } else if (ee::le(elapsed, t.limit)) {
        const float span = ee::sub(t.limit, t.target);
        if (ee::lt(0.0f, span)) {
            const float range = ee::cvt_s(t.points_max - t.points_min);
            if (ee::lt(0.0f, range)) {
                const float f = ee::sub(1.0f, ee::div(ee::sub(elapsed, t.target), span));
                points += ee::cvt_w(ee::mul(f, range));
            }
        }
    }
    if (ee::lt(0.0f, ee::cvt_s(points))) SMissionManager_AddScore(m, 1, points);
}

// [driving] 0x00208eb0 SMissionManager::UpdateSectionProfile: a taxa de quadros do render (0x333ed8 +0x48) na seção
// corrente. Port: a seção nunca começa (o EProfileMissionSection não está em Paris): nada.
void SMissionManager_UpdateSectionProfile(SMissionManagerData& m) {
    if (m.current_profile >= 0) ++g_stats.guards;
}

// [driving] 0x00206ec0 SMissionManager::ResetCharInfo: a vida e o escudo das configurações (10000/1000 sem elas), as
// trapaças (x 100: 0x33a008/0x33a00c; o port não as tem), os multiplicadores do UberBond (+0x61c, +0x620), metade da
// vida máxima nas recargas e a porcentagem por dificuldade.
void SMissionManager_ResetCharInfo(SMissionManagerData& m) {
    m.unknown374 = 100;
    const MissionSettings* s = nf::p32_ptr<const MissionSettings>(m.settings);
    m.unknown370 = 0;
    if (s->health != 0) {
        m.player_health = ee::cvt_s(s->health);
        m.player_health_max = ee::cvt_s(s->health_max);
        m.player_shield = ee::cvt_s(s->shield);
        m.player_shield_max = ee::cvt_s(s->shield_max);
    } else {
        m.player_health_max = 10000.0f;
        m.player_shield_max = 1000.0f;
        m.player_health = 10000.0f;
        m.player_shield = 0.0f;
    }
    const float scale_health = g_uber.real(0x61c);
    if (ee::lt(0.0f, scale_health)) m.player_health = ee::mul(m.player_health, scale_health);
    const float scale_shield = g_uber.real(0x620);
    if (ee::lt(0.0f, scale_shield)) {
        float v = ee::mul(m.player_shield_max, scale_shield);
        if (ee::lt(v, m.player_shield)) v = m.player_shield;
        m.player_shield = v;
    }
    if (m.loads > 0) {
        float v = ee::mul(m.player_health_max, 0.5f);
        if (ee::lt(v, m.player_health)) v = m.player_health;
        m.player_health = v;
    }
    if (s->health_pct[0] > 0) {
        int hi, si;
        if (m.difficulty == 1) hi = 1, si = 1;
        else if (m.difficulty == 2) hi = 2, si = 2;
        else hi = 0, si = 0;
        const float hp = ee::div(ee::cvt_s(s->health_pct[hi]), 100.0f);
        m.player_health = ee::mul(m.player_health, hp);
        m.player_health_max = ee::mul(m.player_health_max, ee::div(ee::cvt_s(s->health_pct[hi]), 100.0f));
        m.player_shield = ee::mul(m.player_shield, ee::div(ee::cvt_s(s->shield_pct[si]), 100.0f));
        m.player_shield_max = ee::mul(m.player_shield_max, ee::div(ee::cvt_s(s->shield_pct[si]), 100.0f));
    }
    m.unknown388 = 1;
    m.unknown3a0 = 2;
    m.unknown3a4[4] = 1000.0f;
    m.unknown38c[0] = 0;
    m.unknown38c[1] = 1000.0f;
    m.unknown38c[2] = 1000.0f;
    m.unknown38c[3] = 0;
    m.unknown38c[4] = 1000.0f;
    m.unknown3a4[0] = 0;
    m.unknown3a4[1] = 1000.0f;
    m.unknown3a4[2] = 1000.0f;
    m.unknown3a4[3] = 0;
    // [driving] 0x00206890 (sem nome): o escudo máximo das configurações x n (os bits 0x200, 0x400 e 0x800 do UberBond
    // +0x9a0: 2, 3, 4).
    auto shield_times = [&](int32_t n) {
        const float v = ee::cvt_s(s->shield_max * n);
        m.player_shield = v;
        m.player_shield_max = v;
    };
    const int32_t f = g_uber.word(0x9a0);
    if (f & 0x200) shield_times(2);
    if (f & 0x400) shield_times(3);
    if (f & 0x800) shield_times(4);
}

// ---------------------------------------------------------------- o modo de cinema

// [driving] 0x002065d0 SMissionManager::SetCinematicMode
void SMissionManager_SetCinematicMode(SMissionManagerData& m, uint32_t mode) {
    if (mode == 0) SMissionManager_ClearCinematicMode(m, 0xffffffffu);
    else m.cinematic_mode |= mode;
    g_env.sound_cinematic_mode = int32_t(m.cinematic_mode);
}

// [driving] 0x00206620 SMissionManager::ClearCinematicMode: sem nenhuma bandeira, o evento 3 apagado, o escalonador
// fora do modo de cinema e, escurecido ou escurecendo, o clarear em meio segundo.
void SMissionManager_ClearCinematicMode(SMissionManagerData& m, uint32_t mode) {
    m.cinematic_mode &= ~mode;
    if (m.cinematic_mode != 0) return;
    SMissionManager_ClearProgrammerEvent(m, 3);
    if (g_env.scheduler) g_env.scheduler->cinematic = 0;
    g_env.sound_cinematic_mode = int32_t(m.cinematic_mode);
    GHudData& h = hud();
    if (!ee::lt(0.0f, h.black_fade) && h.black_fading == 0) {
        m.skipping_cinematic = 0;
        return;
    }
    GHud_FadeInFromBlack(h, simulation().frame_rate >> 1);
    m.skipping_cinematic = 0;
}

// [driving] 0x002066d0 SMissionManager::SkipCinematic (o pedido do jogador: o escurecer de meio segundo)
void SMissionManager_SkipCinematic(SMissionManagerData& m) {
    if (m.cinematic_mode == 0 || m.skipping_cinematic != 0 || m.state != 1) return;
    if (hud().letterbox_mode != 0) return;
    const Simulation& sim = simulation();
    if (!(sim.frame_rate * 2 < sim.steps)) return;
    SMissionManager_ProgrammerDefinedEvent(m, 3);
    m.skipping_cinematic = m.state;
    GHud_FadeOutToBlack(hud(), sim.frame_rate >> 1);
}

// [driving] 0x00206788 SMissionManager::GoingToSkipCinematic
bool SMissionManager_GoingToSkipCinematic(const SMissionManagerData& m) {
    return m.skipping_cinematic != 0 && m.cinematic_mode != 0;
}

// [driving] 0x002067a8 SMissionManager::IsSkippingCinematic: pulando e já preto.
bool SMissionManager_IsSkippingCinematic(const SMissionManagerData& m) {
    if (m.skipping_cinematic == 0 || m.cinematic_mode == 0) return false;
    return ee::eq(hud().black_fade, 1.0f);
}

// [driving] 0x00206398 SMissionManager::SetAutoDrive(bool ligar, bool entrada)
// O estado muda e as chamadas ficam anotadas; a troca da entrada (IOModule::ReleaseAllButtons e a configuração do POV,
// a da mira do AutoDrive: seção 23) vai pelo gancho mission_env().auto_drive_input de quem tem o IOModule. As mãos
// (AICharacterHands, driving/game/ai_character.h) aparecem ou somem; a mira (WTargetPicker) e o GHud::SetAutoDrive só
// anotados.
void SMissionManager_SetAutoDrive(SMissionManagerData& m, int32_t on, bool reset_input) {
    if (on != m.auto_drive) {
        if (reset_input) {
            call(0x168428);  // IOModule::ReleaseAllButtons
            call(0x166cb0, on != 0 ? 1u : 0u, 0xffffffffu);  // InputConfigManager: a configuração da direção automática
            if (g_env.auto_drive_input) g_env.auto_drive_input(on != 0 ? 1 : 0);
            g_env.io_updating = 1;
        }
        // As mãos de Bond (0x327670) aparecem ou somem com a direção automática.
        if (const uint32_t hands = hands_globals().hands) {
            if (on != 0) AICharacterHands_ShowHands(hands);
            else AICharacterHands_HideHands(hands);
        }
        call(on != 0 ? 0x22b638u : 0x22b698u, on != 0 ? 1u : 0u);  // WTargetPicker::Activate/DeactivateTargeting
        if (on != 0) WTargetPicker_ActivateTargeting(1);
        else WTargetPicker_DeactivateTargeting();
    }
    if (reset_input) call(0x241040, uint32_t(on));  // GHud::SetAutoDrive
    m.auto_drive = on;
}

// [driving] 0x002064d8 SMissionManager::SetDriveMissile(Missile *): o míssil guiado (o port não tem mísseis: só o 0
// acontece) e o ForceStop 8 do carro do jogador (ligado com míssil, desligado sem).
void SMissionManager_SetDriveMissile(SMissionManagerData& m, uint32_t missile) {
    m.missile_steer[0] = 0.0f;
    m.missile = missile;
    m.missile_steer[3] = 0.0f;
    m.missile_steer[2] = 0.0f;
    m.missile_steer[1] = 0.0f;
    BondCar* car = mission_player_car();
    if (missile != 0) {
        call(0x1bb8a8);  // RPlayerCamera::SetMissileCamera
        if (m.missile_target == 1) call(0x183888);  // Missile::SetTarget
        if (car) car->d.force_stop = uint8_t(car->d.force_stop | 8);  // PBondCar::ForceStopOn(8)
        else ++g_stats.guards;
        return;
    }
    if (m.auto_drive != 0) call(0x1d8470, 0);  // RColorize::SetEnabled(0)
    else call(0x1bb970);                        // RPlayerCamera::EndMissileCamera
    if (car) car->d.force_stop = uint8_t(car->d.force_stop & ~8u);  // PBondCar::ForceStopOff(8)
    else ++g_stats.guards;
}

// ---------------------------------------------------------------- a vida e as estatísticas

// [driving] 0x00206b20 SMissionManager::GetPlayerHealth(bool)
float SMissionManager_GetPlayerHealth(const SMissionManagerData& m, bool fraction) {
    return fraction ? ee::div(m.player_health, m.player_health_max) : m.player_health;
}

// [driving] 0x00206b40 SMissionManager::GetPlayerShield(bool)
float SMissionManager_GetPlayerShield(const SMissionManagerData& m, bool fraction) {
    if (!ee::lt(0.0f, m.player_shield_max)) return 0.0f;
    return fraction ? ee::mul(m.player_shield, ee::div(1.0f, m.player_shield_max)) : m.player_shield;
}

// [driving] 0x00206810 SMissionManager::BoostPlayerHealth(float)
void SMissionManager_BoostPlayerHealth(SMissionManagerData& m, float amount) {
    const float max = m.player_health_max;
    float v = ee::add(m.player_health, ee::cvt_s(ee::cvt_w(ee::mul(amount, max))));
    if (ee::lt(max, v)) v = max;
    m.player_health = v;
}

// [driving] 0x00206850 SMissionManager::BoostPlayerShield(float)
void SMissionManager_BoostPlayerShield(SMissionManagerData& m, float amount) {
    const float max = m.player_shield_max;
    float v = ee::add(m.player_shield, ee::cvt_s(ee::cvt_w(ee::mul(amount, max))));
    if (ee::lt(max, v)) v = max;
    m.player_shield = v;
}

// [driving] 0x002068b8 SMissionManager::IncPlayerDamage(float)
void SMissionManager_IncPlayerDamage(SMissionManagerData& m, float damage) {
    if (m.cinematic_mode != 0) return;
    m.player_damage = ee::add(m.player_damage, damage);
    m.last_damage_step = simulation().steps;
}

// [driving] 0x002068e0 SMissionManager::IncShotsFired(int)
void SMissionManager_IncShotsFired(SMissionManagerData& m, int32_t n) {
    m.shots_fired += n;
    m.last_shot_step = simulation().steps;
}

// [driving] 0x00206900 SMissionManager::IncShotsHit(bool): um acerto, salvo mais acertos que tiros; com o bool, só se o
// último tiro foi há menos de um décimo de segundo.
// Guarda: a divisão pelo 10 do PS2 nunca para (o divisor é a constante).
void SMissionManager_IncShotsHit(SMissionManagerData& m, bool check_rate) {
    if (!(m.shots_hit < m.shots_fired)) return;
    if (check_rate) {
        const Simulation& sim = simulation();
        if (m.last_shot_step + sim.steps / 10 < sim.steps) return;
    }
    m.shots_hit += 1;
}

// [driving] 0x00206960 SMissionManager::IncKills(int) (nunca abaixo de 0)
void SMissionManager_IncKills(SMissionManagerData& m, int32_t n) {
    m.kills += n;
    if (m.kills < 0) m.kills = 0;
}

// [driving] 0x00207ea8 SMissionManager::AddScore(int, int): o tipo 1 com pontos chama o GHud::Stylin (anotado) e conta.
// Guarda: um tipo fora dos 12 escreveria fora do vetor.
void SMissionManager_AddScore(SMissionManagerData& m, int32_t kind, int32_t points) {
    if (kind < 0 || kind >= 12) {
        ++g_stats.guards;
        return;
    }
    m.scores[kind] += points;
    if (points <= 0 || kind != 1) return;
    if (hud_exists()) call(kFnStylin);
    m.stylin += 1;
}

// [driving] 0x002083e8 SMissionManager::GetScore / 0x002083f8 GetScoreTotal
int32_t SMissionManager_GetScore(const SMissionManagerData& m, int32_t kind) {
    if (kind < 0 || kind >= 12) return 0;
    return m.scores[kind];
}
int32_t SMissionManager_GetScoreTotal(const SMissionManagerData& m) {
    int32_t s = 0;
    for (int32_t v : m.scores) s += v;
    return s;
}

// ---------------------------------------------------------------- os contadores e os eventos do programador

// [driving] 0x00206980 SMissionManager::GetMagicCounter (guarda fora dos 64: 0)
int32_t SMissionManager_GetMagicCounter(const SMissionManagerData& m, int32_t i) {
    if (i < 0 || i >= 64) {
        ++g_stats.guards;
        return 0;
    }
    return m.magic_counters[i];
}

// [driving] 0x00206990 SMissionManager::IncMagicCounter: chegou ao limiar, o evento do programador de mesmo número; os
// contadores 0xc/0xd (o +0x414 = contador 12) e 0xe..0x10 (o +0x41c = contador 14) dão os eventos das contagens.
void SMissionManager_IncMagicCounter(SMissionManagerData& m, int32_t i) {
    if (i > 0x3f) return;
    if (i < 0) {
        ++g_stats.guards;
        return;
    }
    const int32_t v = m.magic_counters[i] + 1;
    m.magic_counters[i] = v;
    const int32_t th = m.magic_thresholds[i];
    if (th >= 0 && !(v < th)) SMissionManager_ProgrammerDefinedEvent(m, i);
    const int32_t a = m.magic_counters[12];
    if (a == 1) SMissionManager_ProgrammerDefinedEvent(m, 0xc);
    else if (a == 2) SMissionManager_ProgrammerDefinedEvent(m, 0xd);
    const int32_t b = m.magic_counters[14];
    if (b == 2) SMissionManager_ProgrammerDefinedEvent(m, 0xf);
    else if (b < 3) {
        if (b == 1) SMissionManager_ProgrammerDefinedEvent(m, 0xe);
    } else if (b == 3) {
        SMissionManager_ProgrammerDefinedEvent(m, 0x10);
    }
}

// [driving] 0x00206ad0 SMissionManager::SetMagicCounter: o valor - 1 e um IncMagicCounter.
void SMissionManager_SetMagicCounter(SMissionManagerData& m, int32_t i, int32_t value) {
    if (!(i < 0x40)) return;
    if (i < 0) {
        ++g_stats.guards;
        return;
    }
    m.magic_counters[i] = value - 1;
    SMissionManager_IncMagicCounter(m, i);
}

// [driving] 0x00206b00 SMissionManager::SetMagicCounterThreshold
void SMissionManager_SetMagicCounterThreshold(SMissionManagerData& m, int32_t i, int32_t value) {
    if (!(i < 0x40)) return;
    if (i < 0) {
        ++g_stats.guards;
        return;
    }
    m.magic_thresholds[i] = value;
}

// [driving] 0x00208540 ProgrammerDefinedEvent / 0x00208558 CheckProgrammerEvent / 0x00208568 ClearProgrammerEvent
// Guarda: um número fora dos 64 escreveria além do vetor (no PS2, em +0x34c..: o estado da missão).
void SMissionManager_ProgrammerDefinedEvent(SMissionManagerData& m, int32_t i) {
    if (i < 0 || i >= 64) {
        ++g_stats.guards;
        return;
    }
    m.programmer_events[i] = 1;
}
int32_t SMissionManager_CheckProgrammerEvent(const SMissionManagerData& m, int32_t i) {
    if (i < 0 || i >= 64) {
        ++g_stats.guards;
        return 0;
    }
    return m.programmer_events[i];
}
void SMissionManager_ClearProgrammerEvent(SMissionManagerData& m, int32_t i) {
    if (i < 0 || i >= 64) {
        ++g_stats.guards;
        return;
    }
    m.programmer_events[i] = 0;
}

// ---------------------------------------------------------------- os objetivos e as mensagens

// [driving] 0x00208578 SMissionManager::AddObjective: até 7; o primeiro vira o corrente (estado 1).
void SMissionManager_AddObjective(SMissionManagerData&, int32_t id, int32_t text, int32_t score, int32_t arg) {
    if (!(g_lists.objectives.size() < 7)) return;  // printf "Maximum objectives reached"
    // [driving] 0x00208f50 (sem nome: SMissionObjective::SMissionObjective)
    g_lists.objectives.push_back(new SMissionObjective{id, text, score, arg, 0});
    if (g_lists.objectives.size() == 1) g_lists.objectives.front()->state = 1;
}

// [driving] 0x002086e0 SMissionManager::InsertObjective: antes do objetivo `before` (ou no fim).
void SMissionManager_InsertObjective(SMissionManagerData& m, int32_t before, int32_t id, int32_t text, int32_t score,
                                     int32_t arg) {
    if (!(g_lists.objectives.size() < 7)) return;
    for (size_t i = 0; i < g_lists.objectives.size(); ++i) {
        if (g_lists.objectives[i]->id != before) continue;
        g_lists.objectives.insert(g_lists.objectives.begin() + std::ptrdiff_t(i),
                                  new SMissionObjective{id, text, score, arg, 0});
        if (g_lists.objectives.size() == 1) g_lists.objectives.front()->state = 1;
        return;
    }
    SMissionManager_AddObjective(m, id, text, score, arg);
}

// [driving] 0x00208a00 SMissionManager::GetObjectiveByID / 0x00208a48 GetObjectiveByIndex
SMissionObjective* SMissionManager_GetObjectiveByID(const SMissionManagerData&, int32_t id) {
    for (SMissionObjective* o : g_lists.objectives)
        if (o->id == id) return o;
    return nullptr;
}
SMissionObjective* SMissionManager_GetObjectiveByIndex(const SMissionManagerData&, int32_t index) {
    if (index < 0 || size_t(index) >= g_lists.objectives.size()) return nullptr;
    return g_lists.objectives[size_t(index)];
}

// [driving] 0x002088b8 SMissionManager::PassObjective: estado 2 e os pontos (AddScore(0, score)).
void SMissionManager_PassObjective(SMissionManagerData& m, int32_t id) {
    SMissionObjective* o = SMissionManager_GetObjectiveByID(m, id);
    if (!o) return;  // printf "Could not find objective"
    o->state = 2;
    SMissionManager_AddScore(m, 0, o->score);
}

// [driving] 0x00208918 FailObjective / 0x00208968 SetCurrentObjective / 0x002089b8 SetIncompleteObjective
void SMissionManager_FailObjective(SMissionManagerData& m, int32_t id) {
    if (SMissionObjective* o = SMissionManager_GetObjectiveByID(m, id)) o->state = 3;
}
void SMissionManager_SetCurrentObjective(SMissionManagerData& m, int32_t id) {
    if (SMissionObjective* o = SMissionManager_GetObjectiveByID(m, id)) o->state = 1;
}
void SMissionManager_SetIncompleteObjective(SMissionManagerData& m, int32_t id) {
    if (SMissionObjective* o = SMissionManager_GetObjectiveByID(m, id)) o->state = 0;
}

// [driving] 0x00208b40 SMissionManager::ClearObjectiveList
void SMissionManager_ClearObjectiveList(SMissionManagerData&) {
    for (SMissionObjective* o : g_lists.objectives) delete o;
    g_lists.objectives.clear();
}

// [driving] 0x002075a8 SMissionManager::SetObjective(int, COORD3 *): o ponto (o centro de um gatilho, por exemplo).
void SMissionManager_SetObjectivePos(SMissionManagerData& m, int32_t slot, uint32_t pos) {
    if (pos == 0) {
        SMissionManager_ClearObjective(m, slot);
        return;
    }
    m.objective_kind[slot] = 1;
    m.objective_target[slot] = pos;
    m.objective_visible[slot] = 0;
    m.objective_check_step = simulation().steps;
}

// [driving] 0x00207600 SMissionManager::SetObjective(int, CARP::Instance *)
void SMissionManager_SetObjectiveInstance(SMissionManagerData& m, int32_t slot, uint32_t instance) {
    if (instance == 0) {
        SMissionManager_ClearObjective(m, slot);
        return;
    }
    m.objective_kind[slot] = 2;
    m.objective_target[slot] = instance;
    m.objective_visible[slot] = 0;
    m.objective_check_step = simulation().steps;
}

// [driving] 0x00207658 SMissionManager::SetObjective(int, CARP::AIElement *): o elemento (o AIElementController o
// redireciona; ativo e com o carro, o DisableTyreBlowOuts dele). Divergência: o elemento é o do port (mission_ai()).
void SMissionManager_SetObjectiveElement(SMissionManagerData& m, int32_t slot, uint32_t element) {
    if (element == 0) {
        SMissionManager_ClearObjective(m, slot);
        return;
    }
    m.objective_kind[slot] = 3;
    m.objective_target[slot] = element;
    m.objective_visible[slot] = 0;
    m.objective_check_step = simulation().steps;
    ++g_stats.ai_queries;
    if (ai_controller()) {
        // RedirectCarpElement: um veículo acordado (com o carro) perde os estouros de pneu (a +0x224 do PBondCar).
        AIElement* e = AIElementController_RedirectCarpElement(element);
        if (e && e->type == 1 && e->state == 1 && e->heli == 0)
            if (BondCar* car = ai_car_from_physics(e->physics)) car->d.tyre_blow_outs = 0;  // 0x00193240
        return;
    }
    mission_ai().get(element);
}

// [driving] 0x00207730 SMissionManager::ClearObjective / 0x00207720 SetObjectiveComment
void SMissionManager_ClearObjective(SMissionManagerData& m, int32_t slot) {
    m.objective_visible[slot] = 0;
    m.objective_kind[slot] = 0;
}
void SMissionManager_SetObjectiveComment(SMissionManagerData& m, int32_t slot, uint32_t comment) {
    m.objective_comment[slot] = comment;
}

// [driving] 0x00207748 SMissionManager::GetCurrObjective: o ponto do objetivo de tipo 1, ou o do elemento de tipo 3
// (AIElementController::RedirectCarpElement e SyncRuleInfo: anotado; o ponto do port); o tipo 2 não tem ponto.
const float* SMissionManager_GetCurrObjective(SMissionManagerData& m, int32_t slot) {
    const int32_t kind = m.objective_kind[slot];
    if (kind == 1) return nf::p32_ptr<const float>(m.objective_target[slot]);
    if (kind == 3) {
        call(kFnSyncRuleInfo, m.objective_target[slot]);
        ++g_stats.ai_queries;
        if (ai_controller()) {
            AIElement* e = AIElementController_RedirectCarpElement(m.objective_target[slot]);
            if (!e) return nullptr;  // (guarda contada no controlador)
            AIElementController_SyncRuleInfo(*e);
            return e->matrix[3];
        }
        return mission_ai().get(m.objective_target[slot]).pos;
    }
    return nullptr;
}

// [driving] 0x00207968 SMissionManager::CheckObjectiveVisibleToUser: só o primeiro; a menos de 200 m da câmera e sem o
// mundo no meio (WCollisionMgr::StepCheckHitWorld com passo de 16 m: mission_env()).
bool SMissionManager_CheckObjectiveVisibleToUser(SMissionManagerData& m, int32_t slot) {
    if (slot != 0) return false;
    const float* o = SMissionManager_GetCurrObjective(m, 0);
    if (!o) return false;
    const float from[4] = {g_env.camera_pos[0], g_env.camera_pos[1], g_env.camera_pos[2], 1.0f};
    const float to[4] = {o[0], o[1], o[2], 1.0f};
    float d[3];
    for (int k = 0; k < 3; ++k) d[k] = flt(ps2::sub(bits(from[k]), bits(to[k])));
    if (!ee::le(vu_length(d), 200.0f)) return false;
    const bool hit = g_env.step_check_hit_world ? g_env.step_check_hit_world(from, to, 16.0f) : false;
    return !hit;
}

// [driving] 0x00208bb8 SMissionManager::AddMessage / 0x00208d20 ClearMessageList
void SMissionManager_AddMessage(SMissionManagerData&, int32_t id, int32_t a, int32_t b) {
    g_lists.messages.push_back(new SMissionMessage{id, a, b});
}
void SMissionManager_ClearMessageList(SMissionManagerData&) {
    for (SMissionMessage* mm : g_lists.messages) delete mm;
    g_lists.messages.clear();
}

// ---------------------------------------------------------------- as regras

const uint32_t kRuleVtable[15] = {0x397be8, 0x397bc8, 0x397ba8, 0x397b88, 0x397b68, 0x397b48, 0x397b28, 0x397ae8,
                                  0x397ac8, 0x397aa8, 0x397b08, 0x397a48, 0x397a68, 0x397a88, 0x397a28};
const uint32_t kRuleSize[15] = {0x14, 0x24, 0x24, 0x1c, 0x28, 0x20, 0x18, 0x28, 0x28, 0x24, 0x24, 0x1c, 0x24, 0x18, 0x20};
const char* const kRuleName[15] = {"Always", "Ammo",   "Damage",   "Death",     "Health",     "PIP",
                                   "Prog",   "Range",  "Speed",    "Timer",     "Property",   "Collision",
                                   "PlayerDir", "Difficulty", "ProgCounter"};

namespace {

uint32_t rd(const uint8_t* p, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}

// [driving] 0x00209738 SMissionRuleTarget::SMissionRuleTarget(int tipo, int elemento): o elemento só no tipo 4.
void target_construct(uint32_t* t, uint32_t kind, uint32_t element) {
    t[0] = kind;
    t[1] = kind == 4 ? element : 0;
}

// O "último" das regras de limiar: o limiar - 1 (abaixo de) ou + 1 (acima de).
int32_t threshold_last(uint32_t dir, int32_t th) { return dir == 0 ? th - 1 : th + 1; }

}  // namespace

// [driving] 0x00209390 SMissionRule::Instantiate (os construtores: 0x00209318 SMissionRule::SMissionRule e os das
// classes, 0x00209d60 SRuleAlways .. 0x0020aec8 SRuleProgCounter). Nulo num tipo fora dos 15.
SMissionRuleData* SMissionRule_Instantiate(const uint8_t* rule) {
    const uint32_t type = rd(rule, 0);
    if (type >= 15) return nullptr;
    SMissionRuleData* r = new SMissionRuleData{};
    r->type = int32_t(type);
    r->size = kRuleSize[type];
    r->events = rd(rule, 0xc);
    const uint32_t flags = rd(rule, 4);
    r->active = int32_t(flags & 1);
    r->once = int32_t((flags >> 1) & 1);
    r->invert = int32_t((flags >> 2) & 1);
    r->vtable = kRuleVtable[type];
    const uint32_t f10 = rd(rule, 0x10), f14 = rd(rule, 0x14), f18 = rd(rule, 0x18), f1c = rd(rule, 0x1c);
    uint32_t* w = r->w;  // +0x14..
    switch (type) {
        case 0:  // Always
            break;
        case 1:  // Ammo: +0x14 a arma, +0x18 o sentido, +0x1c o limiar, +0x20 o último
            w[1] = f14;
            w[2] = f18;
            w[3] = uint32_t(threshold_last(f14, int32_t(f18)));
            w[0] = f10;
            break;
        case 2:  // Damage: o alvo, +0x1c o dano, +0x20 a vida anterior (0)
            target_construct(w, f10, f14);
            w[2] = f18;
            w[3] = 0;
            break;
        case 3:  // Death
        case 11: // Collision
            target_construct(w, f10, f14);
            break;
        case 4:  // Health
        case 7:  // Range
        case 8:  // Speed: o alvo, +0x1c o sentido, +0x20 o limiar, +0x24 o último
            target_construct(w, f10, f14);
            w[2] = f18;
            w[3] = f1c;
            w[4] = uint32_t(threshold_last(f18, int32_t(f1c)));
            break;
        case 5:  // PIP
            w[0] = f10;
            w[1] = f14;
            w[2] = f18;
            break;
        case 6:   // Prog: o evento
        case 13:  // Difficulty: a máscara
            w[0] = f10;
            break;
        case 9: {  // Timer: +0x14 o cronômetro, +0x18 o sentido, +0x1c o limiar (s), +0x20 o último
            w[0] = f10;
            w[1] = f14;
            const float th = flt(f18);
            w[2] = f18;
            w[3] = bits(f14 == 0 ? ee::sub(th, 1.0f) : ee::add(th, 1.0f));
            break;
        }
        case 10:  // Property: o alvo, +0x1c o nome (strncpy de 8)
            target_construct(w, f10, f14);
        {
            // O strncpy: até 8 caracteres, o resto com zeros depois do nulo.
            char* dst = reinterpret_cast<char*>(&w[2]);
            const char* src = reinterpret_cast<const char*>(rule + 0x18);
            bool end = false;
            for (int i = 0; i < 8; ++i) {
                if (!end && src[i] == 0) end = true;
                dst[i] = end ? 0 : src[i];
            }
            break;
        }
        case 12:  // PlayerDir: o alvo, +0x1c, +0x20
            target_construct(w, f10, f14);
            w[2] = f18;
            w[3] = f1c;
            break;
        case 14:  // ProgCounter: +0x14 o contador, +0x18 a operação, +0x1c o valor
            w[0] = f10;
            w[1] = f14;
            w[2] = f18;
            break;
        default:
            break;
    }
    return r;
}

// [driving] 0x002095f8 SMissionRule::RunEvents: o EventDynamicData zerado (o índice da instância -1, +0x38 o bit 0 das
// bandeiras do objeto do jogador, +0x3c a vaga dele) e cada evento da lista criado pela sua função.
void SMissionRule_RunEvents(SMissionRuleData& r) {
    if (r.events == 0) return;
    EventDynamicData& d = trigger_manager().event_data;
    std::memset(&d, 0, sizeof d);
    d.instance_index = -1;
    d.index = -1;
    BondCar* car = mission_player_car();
    // Guarda: sem o carro do jogador o PS2 leria o objeto no endereço 0 (o vetor de carros da Simulation vazio).
    if (car) {
        d.flag = car->d.flags & 1u;
        d.index = car->d.slot;
    } else {
        ++g_stats.guards;
    }
    const TriggerEvents* list = nf::p32_ptr<const TriggerEvents>(r.events);
    const TriggerEvent* e = reinterpret_cast<const TriggerEvent*>(list + 1);
    for (int32_t i = 0; i < list->count; ++i) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&e[i]) + e[i].data_offset;
        ++g_stats.events_created;
        event_create(e[i].type, data);
    }
}

// [driving] 0x002096d0 SMissionRule::DoRule: ativa, CheckRule diferente de "invertida" dispara; "uma vez" desliga.
void SMissionRule_DoRule(SMissionRuleData& r) {
    if (r.active == 0) return;
    ++g_stats.rule_checks;
    const uint32_t v = uint32_t(SMissionRule_CheckRule(r));
    if (uint32_t(r.invert != 0) == v) return;
    ++g_stats.rules_fired;
    SMissionRule_RunEvents(r);
    if (r.once != 0) r.active = 0;
}

namespace {

// A vida, a velocidade ou o alcance do AIElementController::GetStats (port: zeros, MissionAI::stats).
int32_t stat_int(int i) {
    ++g_stats.ai_queries;
    if (const AIElementControllerData* c = ai_controller()) return ee::cvt_w(c->stats[i]);
    return ee::cvt_w(g_ai.stats[i]);
}

// SRuleHealth/SRuleSpeed: o valor cruzou o limiar desde a última vez (sentido 0: para baixo; senão para cima).
bool crossed(uint32_t dir, int32_t value, int32_t th, int32_t& last) {
    bool r = false;
    if (dir == 0) {
        if (value < th) r = !(last < th);
    } else {
        if (th < value) r = !(th < last);
    }
    last = value;
    return r;
}

}  // namespace

// A CheckRule de cada classe (a entrada +0x14 da tabela virtual):
// [driving] 0x00209df0 SRuleAlways, 0x00209ec0 SRuleAmmo, 0x00209ff0 SRuleDamage, 0x0020a108 SRuleDeath, 0x0020a1f8
// SRuleHealth, 0x0020a378 SRulePIP, 0x0020a420 SRuleProg, 0x0020a500 SRuleProperty, 0x0020a5f0 SRuleRange, 0x0020a788
// SRuleSpeed, 0x0020a940 SRuleTimer, 0x0020aac0 SRuleDifficulty, 0x0020ac70 SRulePlayerDir, 0x0020aec0
// SRuleCollision, 0x0020af78 SRuleProgCounter.
// Divergência: SRuleAmmo (a munição do SWeaponManager) e SRulePlayerDir (o acos do newlib) devolvem 0 (contadas);
// SRuleRange com o alvo (FUN_0025a7e8, a distância da posição ao carro do jogador) também.
bool SMissionRule_CheckRule(SMissionRuleData& r) {
    SMissionManagerData& m = mission_manager();
    uint32_t* w = r.w;
    switch (r.type) {
        case 0:  // Always
        case 5:  // PIP
            return true;
        case 1:  // Ammo
            ++g_stats.unported_rules;
            return false;
        case 2: {  // Damage: a vida caiu mais que o dano desde a última vez
            int32_t drop;
            if (w[0] == 3) {
                drop = stat_int(2);
            } else {
                const int32_t h = SMissionRuleTarget_GetHealth(w);
                drop = int32_t(w[3]) - h;
                w[3] = uint32_t(h);
            }
            return int32_t(w[2]) < drop;
        }
        case 3:  // Death
            return SMissionRuleTarget_GetHealth(w) == 0;
        case 4: {  // Health
            const int32_t v = w[0] == 3 ? stat_int(w[2] == 0 ? 1 : 0) : SMissionRuleTarget_GetHealth(w);
            int32_t last = int32_t(w[4]);
            const bool r2 = crossed(w[2], v, int32_t(w[3]), last);
            w[4] = uint32_t(last);
            return r2;
        }
        case 6:  // Prog
            return SMissionManager_CheckProgrammerEvent(m, int32_t(w[0])) != 0;
        case 7: {  // Range
            int32_t v;
            if (w[0] == 3) {
                v = stat_int(w[2] == 0 ? 6 : 5);
            } else {
                ++g_stats.unported_rules;
                return false;
            }
            const bool r2 = w[2] == 0 ? v < int32_t(w[3]) : int32_t(w[3]) < v;
            w[4] = uint32_t(v);
            return r2;
        }
        case 8: {  // Speed
            const int32_t v = w[0] == 3 ? stat_int(w[2] == 0 ? 4 : 3) : SMissionRuleTarget_GetSpeed(w);
            int32_t last = int32_t(w[4]);
            const bool r2 = crossed(w[2], v, int32_t(w[3]), last);
            w[4] = uint32_t(last);
            return r2;
        }
        case 9: {  // Timer: o cronômetro cruzou o limiar (s) desde a última vez
            SMissionTimer* t = mission_timer(m, int32_t(w[0]));
            if (!t || t->active == 0) return false;
            const Simulation& sim = simulation();
            const float v = ee::div(ee::cvt_s(SMissionTimer_GetValue(*t)), ee::cvt_s(sim.frame_rate));
            const float th = flt(w[2]);
            bool r2;
            if (w[1] == 0) {
                if (!ee::lt(v, th)) {
                    w[3] = bits(v);
                    return false;
                }
                r2 = ee::le(th, flt(w[3]));
            } else {
                if (!ee::lt(th, v)) {
                    w[3] = bits(v);
                    return false;
                }
                r2 = ee::le(flt(w[3]), th);
            }
            w[3] = bits(v);
            return r2;
        }
        case 10: {  // Property
            char name[9] = {};
            std::memcpy(name, &w[2], 8);
            return SMissionRuleTarget_GetProperty(w, name);
        }
        case 11:  // Collision
            return false;
        case 12:  // PlayerDir
            ++g_stats.unported_rules;
            return false;
        case 13: {  // Difficulty: o bit 0 fácil, o 1 normal, o 2 difícil
            const int32_t d = m.difficulty;
            const uint32_t mask = w[0];
            if ((mask & 1) && d == 0) return true;
            return ((mask & 2) && d == 1) || ((mask & 4) && d == 2);
        }
        case 14: {  // ProgCounter
            const uint32_t v = uint32_t(SMissionManager_GetMagicCounter(m, int32_t(w[0])));
            switch (w[1]) {
                case 0: return v == w[2];
                case 1: return int32_t(w[2]) < int32_t(v);
                case 2: return int32_t(v) < int32_t(w[2]);
                case 3: return (v & w[2]) != 0;
                case 4: return (v & w[2]) == 0;
                default: return false;
            }
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------- o alvo de uma regra

// [driving] 0x00209758 SMissionRuleTarget::GetSpeed: o jogador (0) pela velocidade do corpo, o elemento (4) pelo do
// port; os outros 0.
int32_t SMissionRuleTarget_GetSpeed(const uint32_t target[2]) {
    const int32_t kind = int32_t(target[0]);
    if (kind == 0) {
        RigidBody* b = player_body();
        if (!b) {
            ++g_stats.guards;
            return 0;
        }
        return ee::cvt_w(vu_length(b->vel));
    }
    if (kind == 4) {
        ++g_stats.ai_queries;
        if (ai_controller()) {
            const AIElementTracking* t = AIElementController_GetCarpElementTracking(target[1]);
            return t ? ee::cvt_w(t->speed) : 0;
        }
        return ee::cvt_w(mission_ai().get(target[1]).speed);
    }
    return 0;
}

// [driving] 0x00209820 SMissionRuleTarget::GetHealth: a vida do jogador (o gerente +0x378) ou a do elemento.
int32_t SMissionRuleTarget_GetHealth(const uint32_t target[2]) {
    const int32_t kind = int32_t(target[0]);
    if (kind == 0) return ee::cvt_w(mission_manager().player_health);
    if (kind == 4) {
        ++g_stats.ai_queries;
        if (ai_controller()) {
            const AIElementTracking* t = AIElementController_GetCarpElementTracking(target[1]);
            const AIElement* e = t ? nf::p32_ptr<const AIElement>(t->element) : nullptr;
            return e ? ee::cvt_w(e->health) : 0;
        }
        return ee::cvt_w(mission_ai().get(target[1]).health);
    }
    return 0;
}

// [driving] 0x002098a0 SMissionRuleTarget::GetPos
void SMissionRuleTarget_GetPos(const uint32_t target[2], float out[3]) {
    const int32_t kind = int32_t(target[0]);
    if (kind == 0) {
        if (RigidBody* b = player_body()) std::memcpy(out, b->pos, 12);
    } else if (kind == 4) {
        ++g_stats.ai_queries;
        if (ai_controller()) {
            if (const AIElementTracking* t = AIElementController_GetCarpElementTracking(target[1]))
                std::memcpy(out, t->pos, 12);
            return;
        }
        std::memcpy(out, mission_ai().get(target[1]).pos, 12);
    }
}

// [driving] 0x00209aa0 SMissionRuleTarget::GetProperty: do jogador "NOBOOST" (o turbo do SWeaponManager acabou),
// "ONEEMP" e "2WHEELS" (PBondCar::GetIsInTwoWheelMode); do elemento, o AIElementTracking::GetProperty (port: falso).
bool SMissionRuleTarget_GetProperty(const uint32_t target[2], const char* name) {
    const int32_t kind = int32_t(target[0]);
    if (kind == 0) {
        if (std::strcmp(name, "NOBOOST") == 0) return g_env.weapon_boost == 0;
        if (std::strcmp(name, "ONEEMP") == 0) return g_env.weapon_emp == 1;
        if (std::strcmp(name, "2WHEELS") == 0) {
            BondCar* car = mission_player_car();
            if (!car) {
                ++g_stats.guards;
                return false;
            }
            return car->d.two_wheel_mode != 0;  // 0x00193248 PBondCar::GetIsInTwoWheelMode (o byte +0x2b2)
        }
        return false;
    }
    if (kind == 4) {
        ++g_stats.ai_queries;
        if (ai_controller()) {
            AIElementTracking* t = AIElementController_GetCarpElementTracking(target[1]);
            return t ? AIElementTracking_GetProperty(*t, name) : false;
        }
        mission_ai().get(target[1]);
        return false;
    }
    return false;
}

}  // namespace drv
