// O SWeaponManager e a fila de ações (driving/game/weapons.h). Portado do MIPS do PS2 (as desmontagens em
// out/agent_drv_weapons/s_wm do checkout principal); as divergências estão no cabeçalho e em cada função.

#include "driving/game/weapons.h"

#include <cstring>
#include <memory>

#include "driving/game/ai_character.h"
#include "driving/game/events.h"
#include "driving/game/hud.h"
#include "driving/game/mission.h"
#include "driving/game/target_picker.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "platform/ps2_float.h"
#include "platform/crt_compat.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

WeaponsEnv g_env;
WeaponsStats g_stats;
std::vector<WeaponsCall> g_trace;
bool g_trace_on = false;

// O armazenamento do gerente do jogo (o `new` do GameLoop_StartUp, o `new` da tabela e o FastAlloc da fila).
struct Storage {
    alignas(16) SWeaponManagerData manager{};
    alignas(16) WeaponEntry table[kWeaponCount]{};
    alignas(16) ActionQueueData queue{};
};
std::unique_ptr<Storage> g_storage;

// As funções dos sistemas não portados que o gerente chama (anotadas).
constexpr uint32_t kFnAMenuSoundTrigger = 0x2f4b10;
constexpr uint32_t kFnHandsGetWeapon = 0x124230;
constexpr uint32_t kFnUpdateCurrentArm = 0x1bb010;
constexpr uint32_t kFnTriggerFX = 0x1d3c28;           // RVehicle::TriggerFX (a entrada +0x68 da tabela virtual)
constexpr uint32_t kFnActivateTargeting = 0x22b638;
constexpr uint32_t kFnDeactivateTargeting = 0x22b698;
constexpr uint32_t kFnRegisterZone = 0x1402f8;        // AIZoneController::RegisterZone (com o Get)
constexpr uint32_t kFnMotionBlur = 0x1d9010;          // RColorize (0x3349e0): o desfoque de movimento
constexpr uint32_t kFnDetonateRemoteMines = 0x1ffe70;
constexpr uint32_t kFnEnableRocketBoost = 0x18b768;   // PBondCar (as entradas +0x230, +0x248, +0x1c0)
constexpr uint32_t kFnFireLaser = 0x18ac08;
constexpr uint32_t kFnEnableTwoWheelStunt = 0x18b820;
constexpr uint32_t kFnScorchGround = 0x1eb078;        // RScorchWorld::ScorchGround (o óleo: arma 10)
constexpr uint32_t kFnUpdateTurret = 0x20d9f0;
constexpr uint32_t kFnHandleNuclearSequence = 0x20ea60;

// As cadeias do .rodata.
constexpr const char* kStrDryFire = "SFX_DryFire";
constexpr const char* kStrPOVWeapons = "POV Weapons";
constexpr const char* kStrWeaponsFire = "Weapons Fire";
constexpr const char* kStrMenuSounds = "Menu Sounds";

int32_t steps() { return simulation().steps; }

SMissionManagerData* mission() { return mission_exists() ? &mission_manager() : nullptr; }
// Os campos do gerente da missão que as armas leem (sem gerente: 0, como um gerente parado).
int32_t m_state() { return mission() ? mission()->state : 0; }
int32_t m_auto_drive() { return mission() ? mission()->auto_drive : 0; }
uint32_t m_missile() { return mission() ? mission()->missile : 0; }
uint32_t m_cinematic() { return mission() ? mission()->cinematic_mode : 0; }
int32_t m_5ec() { return mission() ? mission()->unknown5ec : 0; }

WeaponEntry* table(SWeaponManagerData& w) { return nf::p32_ptr<WeaponEntry>(w.weapons); }

uint32_t anim_handle(const SWeaponManagerData& w) {
    return g_env.anim_handle ? g_env.anim_handle(w.player_car) : 0;
}
bool anim_playing(uint32_t h, int32_t sys) { return g_env.anim.is_playing ? g_env.anim.is_playing(h, sys) : false; }
int32_t anim_state(uint32_t h, int32_t sys) { return g_env.anim.state ? g_env.anim.state(h, sys) : 0; }
void anim_stimulus(uint32_t h, int32_t sys, int32_t stim, int32_t time, int32_t mode) {
    ++g_stats.stimuli;
    if (g_env.anim.stimulus) g_env.anim.stimulus(h, sys, stim, time, mode);
}

void sound(int32_t bank, const char* name, const char* group, int32_t mode) {
    weapons_note_call(kFnAMenuSoundTrigger, uint32_t(bank), 0, 0, uint32_t(mode), name, group);
}

// "SFX_" + o nome da arma + o sufixo (o buffer de 0x20 bytes da pilha: 5 copiados, 0x1b zerados, os strcat).
std::string weapon_sound_name(const WeaponEntry& e, const char* suffix) {
    std::string s = "SFX_";
    if (const char* n = nf::p32_ptr<const char>(e.sound_name)) s += n;
    s += suffix;
    return s;
}

// O RVehicle::TriggerFX dos clarões (a entrada +0x68 da tabela virtual do RSceneObj do carro: o tipo, o prazo, a
// duração). O porte do RVehicle conta os efeitos; aqui só anotado.
void trigger_fx(int32_t type) { weapons_note_call(kFnTriggerFX, uint32_t(type), uint32_t(steps() + 10), 0x1e); }

bool lookup_bool(const char* key) { return g_env.lookup_bool ? g_env.lookup_bool(key) : false; }
int32_t lookup_int(const char* key, bool* found) {
    if (g_env.lookup_int) return g_env.lookup_int(key, found);
    if (found) *found = false;
    return 0;
}

// O SMissionManager das estatísticas e dos contadores (porte em driving/game/mission.h; anotados também, para a
// conferência).
constexpr uint32_t kFnIncShotsFired = 0x2068e0, kFnIncMagicCounter = 0x206990, kFnSetMagicCounter = 0x206ad0;
constexpr uint32_t kFnProgrammerDefinedEvent = 0x208540, kFnInitPlayerCar = 0x205a90;
void inc_shots(int32_t n) {
    if (mission_exists()) SMissionManager_IncShotsFired(mission_manager(), n);
    weapons_note_call(kFnIncShotsFired, uint32_t(n));
}
void inc_magic(int32_t i) {
    if (mission_exists()) SMissionManager_IncMagicCounter(mission_manager(), i);
    weapons_note_call(kFnIncMagicCounter, uint32_t(i));
}
void set_magic(int32_t i, int32_t v) {
    if (mission_exists()) SMissionManager_SetMagicCounter(mission_manager(), i, v);
    weapons_note_call(kFnSetMagicCounter, uint32_t(i), uint32_t(v));
}
float vu_sub(float a, float b) { return ee::sub(a, b); }

bool valid_type(int32_t t) {
    if (t >= 0 && t < kWeaponCount) return true;
    ++g_stats.guards;
    return false;
}

}  // namespace

WeaponsEnv& weapons_env() { return g_env; }
WeaponsStats& weapons_stats() { return g_stats; }
std::vector<WeaponsCall>& weapons_trace() { return g_trace; }
bool& weapons_trace_enabled() { return g_trace_on; }

void weapons_note_call(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, const char* s1,
                       const char* s2) {
    ++g_stats.external_calls;
    if (!g_trace_on) return;
    WeaponsCall c{fn, {a0, a1, a2, a3}, s1 ? s1 : "", s2 ? s2 : ""};
    g_trace.push_back(std::move(c));
}

// ---------------------------------------------------------------- a fila

void ActionQueue_Construct(ActionQueueData& q, uint32_t manager) {
    for (ActionDataPS2& a : q.entries) a = {0, 0, 0.0f};  // ActionData::ActionData(0, 0, 0.0)
    q.count = 0;
    q.head = 0;
    q.capacity = 200;
    q.tail = -1;
    // [driving] 0x00165488 ActionQueue::Initialize (o nome da fila não fica nela; o RegisterQueue no gerente das filas
    // fica com quem cria)
    q.manager = manager;
    q.count = 0;
    q.tail = -1;
    q.head = 0;
}

void ActionQueue_Push(ActionQueueData& q, const ActionDataPS2& a) {
    q.tail = (q.capacity - 1 < q.tail + 1) ? 0 : q.tail + 1;
    ++q.count;
    if (q.capacity < q.count) {
        q.head = (q.capacity - 1 < q.head + 1) ? 0 : q.head + 1;
        q.count = q.capacity;
    }
    q.entries[q.tail] = a;
}

const ActionDataPS2* ActionQueue_GetAction(const ActionQueueData& q) {
    if (q.count <= 0) return nullptr;  // ActionRef(0)
    return &q.entries[q.head];
}

void ActionQueue_PopAction(ActionQueueData& q) {
    if (q.count <= 0) return;
    q.head = (q.capacity - 1 < q.head + 1) ? 0 : q.head + 1;
    --q.count;
}

void ActionQueue_Flush(ActionQueueData& q) {
    q.count = 0;
    q.tail = -1;
    q.head = 0;
}

bool ActionQueueManager_Accepts(int32_t action) {
    if (action > 0 && action < 0x41 && steps() == 0 && action == 0x1f) return false;
    return true;
}

void weapons_feed(int32_t action, int32_t device, float value) {
    SWeaponManagerData* w = weapon_manager();
    if (!w || !ActionQueueManager_Accepts(action)) return;
    ++g_stats.actions;
    ActionQueue_Push(*weapon_queue(*w), {action, device, value});
}

// ---------------------------------------------------------------- o gerente do jogo

SWeaponManagerData* weapon_manager() { return g_storage ? &g_storage->manager : nullptr; }
bool weapon_manager_exists() { return g_storage != nullptr; }

SWeaponManagerData& weapons_create() {
    g_storage = std::make_unique<Storage>();
    SWeaponManager_Construct(g_storage->manager, g_storage->table, &g_storage->queue);
    return g_storage->manager;
}

void weapons_destroy() { g_storage.reset(); }

WeaponEntry* weapon_entry(SWeaponManagerData& w, int32_t type) {
    if (type < 0 || type >= kWeaponCount) return nullptr;
    return table(w) + type;
}

ActionQueueData* weapon_queue(SWeaponManagerData& w) { return nf::p32_ptr<ActionQueueData>(w.action_queue); }

// ---------------------------------------------------------------- SWeaponManager

void SWeaponManager_Construct(SWeaponManagerData& w, WeaponEntry* t, ActionQueueData* queue) {
    std::memset(&w, 0, sizeof w);
    w.player_car = 0;
    w.weapons = nf::p32_u(t);
    ActionQueue_Construct(*queue, 0);  // (o ActionQueueManager, que o port não tem: as ações vêm do weapons_feed)
    w.action_queue = nf::p32_u(queue);
    SWeaponManager_Reset(w);
}

void SWeaponManager_Reset(SWeaponManagerData& w) {
    // O intervalo de cada arma (na tabela estática, antes da cópia): segundos x a taxa de quadros, truncado.
    const float rate = ee::cvt_s(simulation().frame_rate);
    WeaponEntry* t = table(w);
    for (int i = 0; i < kWeaponCount; ++i) {
        uint32_t words[21];
        for (int k = 0; k < 21; ++k) {
            const WeaponWord& d = kWeaponDefaultTable[i][k];
            words[k] = d.s ? nf::p32_u(d.s) : uint32_t(d.v);
        }
        words[0x3c / 4] = uint32_t(ee::cvt_w(ee::mul(kWeaponFireDelay[i], rate)));
        std::memcpy(&t[i], words, sizeof(WeaponEntry));
    }
    t[0].flags |= 1;
    if (ActionQueueData* q = weapon_queue(w)) ActionQueue_Flush(*q);
    w.secondary = 0;
    w.last_fired = -1;
    w.primary = -1;
    w.special = -1;
    w.hands_secondary = 0;
    w.last_target = 0;
    w.primary_held = 0;
    w.secondary_held = 0;
    w.special_held = 0;
    w.hands_fired = 0;
    w.reload_pending = 0;
    w.unknown38 = 0;
    w.unknown40 = 0;
    w.multi_damage = 0;
    w.boost_blur = 0;
    w.unknown48 = 0;
    w.next_select = 0;
    w.nuclear_state = 0;
    w.nuclear_time = 0;
    w.nuclear_radius = 0.0f;
    w.click_count = 0;
    w.turret_angle = 0.0f;
    w.special_trigger = 0;
    w.special_range = 0.0f;
    if (uber_bond().word(0x9a0) & 8) w.multi_damage = 1;
    SWeaponManager_CalcWeaponPriorities(w);
}

void SWeaponManager_CalcWeaponPriorities(SWeaponManagerData& w) {
    for (int32_t t = 0; t < kWeaponCount; ++t) {
        int32_t p = -1;
        if (kWeaponSortOrder[0] == t) {
            p = 0;
        } else {
            for (int32_t i = 1; i < kWeaponCount; ++i) {
                if (kWeaponSortOrder[i] == t) {
                    p = i;
                    break;
                }
            }
        }
        w.priority[t] = p;
    }
}

void SWeaponManager_SortWeaponList(SWeaponManagerData& w) {
    WeaponEntry* t = table(w);
    int32_t n = 0;
    const bool hands = m_auto_drive() != 0;
    for (int32_t i = 0; i < kWeaponCount; ++i) {
        // Com a direção automática só as armas de Bond (0x17 em diante); sem ela, as do carro (até 0x16).
        if (hands ? i < 0x17 : i >= 0x17) continue;
        const WeaponEntry& e = t[i];
        if (i != w.secondary) {
            if ((e.flags & 1) == 0) continue;
            if (e.ammo <= 0) continue;
        }
        if (i == w.special) continue;
        w.sorted[n++] = i;
    }
    w.sorted_count = n;
    // A bolha da ordem pela prioridade.
    const int32_t last = n - 1;
    for (int32_t i = 0; i < last; ++i) {
        for (int32_t k = 0; k < last - i; ++k) {
            int32_t& a = w.sorted[i + k];
            int32_t& b = w.sorted[i + k + 1];
            if (w.priority[b] < w.priority[a]) {
                const int32_t x = a;
                a = b;
                b = x;
            }
        }
    }
}

int32_t SWeaponManager_FindSortedSecondary(const SWeaponManagerData& w) {
    for (int32_t i = 0; i < w.sorted_count; ++i)
        if (w.sorted[i] == w.secondary) return i;
    return 0;  // (o -1 vira 0 no movn do fim)
}

void SWeaponManager_SetPlayerCar(SWeaponManagerData& w, uint32_t scene_obj) {
    w.player_car = scene_obj;
    weapons_note_call(kFnInitPlayerCar);  // SMissionManager::InitPlayerCar (fica com quem cria o carro)
    bool found = false;
    const int32_t n = lookup_int("MISSILES_FIRED_SIMULTANEOUSLY", &found);
    if (found) {
        table(w)[2].clip_size = n << 1;
        table(w)[2].clip = n << 1;
    }
}

void SWeaponManager_SetSpecialWeapon(SWeaponManagerData& w, int32_t type, uint32_t trigger, float range) {
    w.special_trigger = trigger;
    w.special_range = range;
    if (w.special != -1) {
        if (valid_type(w.special)) table(w)[w.special].ammo = 0;
    }
    if (type == 2) {
        SWeaponManager_LoadWeapon(w, 2, 200, 0, 0);
        w.special = -1;
        w.secondary = 2;
    } else {
        w.special = type;
    }
}

void SWeaponManager_LoadWeapon(SWeaponManagerData& w, int32_t type, int32_t ammo, int32_t clip_size, int32_t arg2) {
    if (!valid_type(type)) return;
    WeaponEntry& e = table(w)[type];
    e.flags |= 1;
    const bool hands = m_auto_drive() != 0;
    if (clip_size > 0 && hands) e.clip_size = clip_size;
    if (arg2 > 0) e.damage = arg2;
    if (hands) e.clip = ammo < e.clip_size ? ammo : e.clip_size;
    e.ammo += ammo;
}

void SWeaponManager_UnloadWeapon(SWeaponManagerData& w, int32_t type) {
    if (!valid_type(type)) return;
    WeaponEntry& e = table(w)[type];
    e.flags &= ~1u;
    e.ammo = 0;
    if (type == w.secondary) w.secondary = -1;
    if (type == w.special) w.special = -1;
}

void SWeaponManager_SetSecondary(SWeaponManagerData& w, int32_t type) {
    w.secondary = type;
    // RPlayerCamera::UpdateCurrentArm (só com a lista de câmeras de 0x333ed8)
    if (!g_env.camera_exists || g_env.camera_exists()) weapons_note_call(kFnUpdateCurrentArm);
}

void SWeaponManager_SelectSecondary(SWeaponManagerData& w, bool next) {
    if (w.reload_pending != 0 && m_auto_drive() != 0 && valid_type(w.secondary)) {
        WeaponEntry& e = table(w)[w.secondary];
        if (e.ammo > 0 && (e.flags & 4) == 0) {
            e.clip = e.ammo < e.clip_size ? e.ammo : e.clip_size;
            w.reload_pending = 0;
        }
    }
    const int32_t t = next ? SWeaponManager_GetNextSecondaryType(w) : SWeaponManager_GetPreviousSecondaryType(w);
    if (t != w.secondary) {
        w.secondary = t;
        w.next_select = simulation().steps + simulation().frame_rate / 3;
    }
}

int32_t SWeaponManager_GetPreviousSecondaryType(SWeaponManagerData& w) {
    if (w.sorted_count == 0) {
        SWeaponManager_SortWeaponList(w);
        if (w.sorted_count == 0) return 0;
    }
    const int32_t i = SWeaponManager_FindSortedSecondary(w);
    const uint32_t n = uint32_t(w.sorted_count);
    return w.sorted[(uint32_t(i + w.sorted_count) - 1u) % n];
}

int32_t SWeaponManager_GetNextSecondaryType(SWeaponManagerData& w) {
    if (w.sorted_count == 0) {
        SWeaponManager_SortWeaponList(w);
        if (w.sorted_count == 0) return 0;
    }
    const int32_t i = SWeaponManager_FindSortedSecondary(w);
    return w.sorted[uint32_t(i + 1) % uint32_t(w.sorted_count)];
}

void SWeaponManager_FirePrimary(SWeaponManagerData& w) {
    if (m_auto_drive() == 0) SWeaponManager_FireWeapon(w, w.primary);
}

void SWeaponManager_FireSecondary(SWeaponManagerData& w) {
    if (w.secondary != 0x16) {
        if (mission()) SMissionManager_ProgrammerDefinedEvent(*mission(), 0x16);
        weapons_note_call(kFnProgrammerDefinedEvent, 0x16);
    }
    SWeaponManager_FireWeapon(w, w.secondary);
}

void SWeaponManager_FireSpecial(SWeaponManagerData& w) {
    SWeaponManager_LoadWeapon(w, w.special, 1, 0, 0);
    SWeaponManager_FireWeapon(w, w.special);
}

void SWeaponManager_DoWeaponAnim(SWeaponManagerData& w, int32_t type, int32_t stimulus, int32_t barrel) {
    anim_stimulus(anim_handle(w), SWeaponManager_GetSystemFromWeapon(type, barrel), stimulus, steps(), 2);
}

int32_t SWeaponManager_GetWeaponFromSystem(int32_t system) {
    for (int32_t t = 0; t < kWeaponCount; ++t) {
        for (int32_t k = 0; k < 8 && kWeaponSystems[t][k] != 0; ++k)
            if (kWeaponSystems[t][k] == system) return t;
    }
    return -1;
}

int32_t SWeaponManager_GetSystemFromWeapon(int32_t type, int32_t barrel) {
    // O PS2 lê a tabela sem conferir: um cano além de 7 lê os sistemas da arma seguinte (o mesmo aqui, pela tabela
    // inteira); fora dela, guarda.
    const int32_t i = type * 8 + barrel;
    if (i < 0 || i >= kWeaponCount * 8) {
        ++g_stats.guards;
        return 0;
    }
    return kWeaponSystems[0][i];
}

void SWeaponManager_UserReloading(SWeaponManagerData& w) {
    // Só com as mãos de Bond (AICharacterHands, 0x327670): a secundária com o pente não cheio e munição além dele.
    const uint32_t hands = hands_globals().hands;
    if (hands == 0) return;
    WeaponEntry* t = table(w);
    if (!t || w.secondary < 0 || w.secondary >= kWeaponCount) {
        ++g_stats.guards;  // o PS2 leria a entrada fora da tabela
        return;
    }
    const WeaponEntry& e = t[w.secondary];
    if (!AICharacterHands_AbleToFire(hands)) return;
    if (!(e.clip < e.clip_size) || e.ammo <= 0 || !(e.clip < e.ammo)) return;
    w.reload_pending = 1;
    if (e.sound_bank >= 0) {
        const std::string name = weapon_sound_name(e, "Reload");
        sound(e.sound_bank, name.c_str(), kStrPOVWeapons, 0);
    }
    if (hands_globals().hands != 0) AICharacterHands_Reload(hands_globals().hands);
}

void SWeaponManager_HandleClipUpdate(SWeaponManagerData& w) {
    // Com a direção automática e as mãos de Bond: o pente da secundária enche (até a munição) quando o braço pode
    // atirar de novo depois de uma recarga pedida.
    if (m_auto_drive() == 0) return;
    WeaponEntry* t = table(w);
    if (!t || w.secondary < 0 || w.secondary >= kWeaponCount) {
        ++g_stats.guards;
        return;
    }
    WeaponEntry& e = t[w.secondary];
    if (e.ammo <= 0 || w.reload_pending == 0) return;
    const uint32_t hands = hands_globals().hands;
    if (hands == 0 || !AICharacterHands_AbleToFire(hands)) return;
    if (e.flags & 4) return;
    e.clip = e.ammo < e.clip_size ? e.ammo : e.clip_size;
    w.reload_pending = 0;
}

void SWeaponManager_StartNuclearSequence(SWeaponManagerData& w, const float pos[3], float radius) {
    w.nuclear_state = 1;
    w.nuclear_time = simulation().steps + simulation().frame_rate;
    w.nuclear_pos[0] = pos[0];
    w.nuclear_pos[1] = pos[1];
    w.nuclear_pos[2] = pos[2];
    w.nuclear_radius = radius;
}

namespace {

// O x² + y² + z² da VU0 (vmul.xyz, vmulax.x, vmadday.x, vmaddz.x): x·x no ACC, mais 1·y², mais 1·z².
uint32_t vu_len2(const float d[3]) {
    const uint32_t x = ps2::fbits(d[0]), y = ps2::fbits(d[1]), z = ps2::fbits(d[2]);
    uint32_t acc = ps2::mul(x, x);
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(y, y));
    return ps2::madd(acc, 0x3f800000u, ps2::mul(z, z));
}

bool is_active(const SWeaponManagerData& w, int32_t t) { return t == w.primary || t == w.secondary || t == w.special; }

// Uma entrada lida pelo índice (o PS2 lê a tabela sem conferir: a secundária ou o especial -1 leem os 0x54 bytes antes
// dela, outra memória do heap; aqui um índice fora conta como guarda e lê uma arma vazia: tudo 0).
const WeaponEntry& entry_or_zero(SWeaponManagerData& w, int32_t index) {
    static const WeaponEntry kEmpty{};
    if (index < 0 || index >= kWeaponCount) {
        ++g_stats.guards;
        return kEmpty;
    }
    return table(w)[index];
}
int32_t entry_type(SWeaponManagerData& w, int32_t index) { return entry_or_zero(w, index).type; }

// O prazo do cano `barrel` (+0x24 + 4 x cano). O PS2 não confere o cano: o lançador do míssil (4 canos) dispara dois de
// cada vez, e começando do cano 3 o segundo cai em +0x34 (o dano); aqui o mesmo dentro da entrada (as palavras
// +0x24..+0x50), e fora dela uma guarda.
int32_t& fire_time(WeaponEntry& e, int32_t barrel) {
    static int32_t dummy = 0;
    if (barrel < 0 || barrel > 11) {
        ++g_stats.guards;
        dummy = 0;
        return dummy;
    }
    return reinterpret_cast<int32_t*>(&e)[9 + barrel];
}

}  // namespace

void SWeaponManager_Update(SWeaponManagerData& w) {
    ++g_stats.updates;
    WeaponEntry* t = table(w);
    const int32_t st = m_state();
    const bool over = st == 3 || st == 4;
    if (over || m_cinematic() != 0) {
        w.primary_held = 0;
        w.secondary_held = 0;
        w.special_held = 0;
        w.hands_fired = 0;
    }
    bool blocked = over || simulation().state == 3 || m_cinematic() != 0;
    if (g_env.hud_exists && g_env.hud_exists()) {
        if (!blocked) blocked = (g_env.hud_2a0 ? g_env.hud_2a0() : 0) != 0;
    }
    int32_t keep[0x4a] = {}, stop[0x4a] = {};  // sp+0 e sp+0x130: os sistemas a ligar e a desligar

    // A direção automática: as armas de Bond (0x17 em diante) trocam de lugar com a secundária do carro.
    if (m_auto_drive() != 0 && w.secondary < 0x17) {
        const int32_t old = w.hands_secondary;
        w.hands_secondary = w.secondary;
        w.secondary = old;
        bool search = true;
        if (old >= 0x17 && old < kWeaponCount) {
            const WeaponEntry& e = t[old];
            if (e.ammo > 0 && (e.flags & 1)) search = false;
        } else if (old >= kWeaponCount) {
            ++g_stats.guards;
        }
        if (search) {
            for (int32_t i = 0x17; i < kWeaponCount; ++i) {
                if (t[i].ammo > 0 && (t[i].flags & 1)) {
                    w.secondary = i;
                    break;
                }
            }
        }
    }
    SWeaponManager_HandleClipUpdate(w);
    if (m_auto_drive() == 0 && w.secondary >= 0x17) {
        const int32_t old = w.hands_secondary;
        w.hands_secondary = w.secondary;
        w.secondary = old;
        bool search = true;
        if (old < 0x17) {
            if (old >= 0) {
                const WeaponEntry& e = t[old];
                if (e.ammo > 0 && (e.flags & 1)) search = false;
            } else {
                ++g_stats.guards;
            }
        }
        if (search) {
            for (int32_t i = 0; i < 0x17; ++i) {
                if (t[i].ammo > 0 && (t[i].flags & 1)) {
                    w.secondary = i;
                    break;
                }
            }
        }
    }
    // Com a direção automática e as mãos de Bond: o braço troca para a secundária (0x18.. = as armas 1.. das mãos), e o
    // sistema da arma nova liga (0x20) se não estava ligado (0x40).
    if (m_auto_drive() != 0 && hands_globals().hands != 0) {
        const uint32_t hands = hands_globals().hands;
        if (AICharacterHands_GetWeapon(hands) + 0x17 != w.secondary && w.next_select < steps()) {
            AICharacterHands_ChangeWeapon(hands, w.secondary - 0x17);
            if (w.secondary < 0 || w.secondary >= kWeaponCount) {
                ++g_stats.guards;
            } else {
                WeaponEntry& e = t[w.secondary];
                if ((e.flags & 0x40) == 0) e.flags |= 0x20;
            }
        }
    }
    SWeaponManager_SortWeaponList(w);
    if (m_auto_drive() == 0 && w.secondary != 0) {
        w.secondary_held = 0;
        w.hands_fired = 0;
    }

    // As ações da fila.
    ActionQueueData* q = weapon_queue(w);
    while (q && !ActionQueue_IsEmpty(*q)) {
        const ActionDataPS2* a = ActionQueue_GetAction(*q);
        const int32_t action = a ? a->action : 0;
        switch (action) {
            case 0x1d:  // o tiro primário apertado
                if (!blocked) {
                    w.primary_held = 1;
                    g_env.fg_right_button = 1;
                    trigger_fx(10);
                }
                break;
            case 0x1e:
                w.primary_held = 0;
                g_env.fg_right_button = 0;
                break;
            case 0x1f: {  // o tiro secundário apertado
                if (blocked) break;
                const int32_t was = w.secondary_held;
                if (w.secondary != 0 && was == 0 && entry_or_zero(w, w.secondary).ammo <= 0)
                    sound(4, kStrDryFire, kStrPOVWeapons, 0);
                w.secondary_held = 1;
                g_env.fg_right_button = 1;
                trigger_fx(10);
                if (m_missile() == 0) {
                    SWeaponManager_FireSecondary(w);
                } else if (was == 0) {
                    // O míssil guiado (+0x358): o +0x70c dele detona.
                    if (uint8_t* mis = nf::p32_ptr<uint8_t>(m_missile())) {
                        const int32_t one = 1;
                        std::memcpy(mis + 0x70c, &one, 4);
                    }
                }
                break;
            }
            case 0x20:
                w.secondary_held = 0;
                w.hands_fired = 0;
                g_env.fg_right_button = 0;
                break;
            case 0x21:
            case 0x22:  // a próxima secundária
            case 0x23:  // a anterior
                if (blocked || w.secondary_held != 0) break;
                if (g_env.camera_aiming && g_env.camera_aiming()) break;
                if (m_missile() != 0) break;
                SWeaponManager_SelectSecondary(w, action != 0x23);
                g_env.fg_left_button = 1;
                trigger_fx(9);
                break;
            case 0x24: {  // o especial
                if (blocked || w.special == -1 || m_missile() != 0) break;
                if (w.special_held == 0) {
                    if (w.special < 0 || w.special >= kWeaponCount) {
                        ++g_stats.guards;
                    } else if (t[w.special].ammo <= 0) {
                        if ((g_env.car_is_sub ? g_env.car_is_sub() : 0) == 0) sound(4, kStrDryFire, kStrPOVWeapons, 0);
                    }
                }
                w.special_held = 1;
                g_env.fg_left_button = 1;
                trigger_fx(9);
                break;
            }
            case 0x25:
                w.special_held = 0;
                break;
            case 0x3f:  // recarregar
                if (!blocked && w.primary_held == 0 && w.secondary_held == 0) SWeaponManager_UserReloading(w);
                break;
            default:
                break;
        }
        ActionQueue_PopAction(*q);
    }

    // A troca automática quando a secundária acaba (uber +0xa48).
    if (uber_bond().word(0xa48) != 0) {
        bool sel = false;
        const WeaponEntry& e = entry_or_zero(w, w.secondary);
        if (e.ammo <= 0) sel = e.clip < 1;
        if (m_auto_drive() != 0) {
            if (hands_globals().hands == 0) sel = false;  // sem as mãos de Bond (0x327670 nulo)
            if (g_env.camera_aiming && g_env.camera_aiming()) sel = false;
        }
        if (sel) SWeaponManager_SelectSecondary(w, true);
    }

    // O tiro contínuo.
    int32_t held;
    if (m_auto_drive() == 0) {
        if (w.primary_held != 0) SWeaponManager_FirePrimary(w);
        held = w.secondary_held;
    } else {
        held = w.secondary_held;
        if (held != 0 && (w.secondary == 0x18 || w.secondary == 0x1b || w.secondary == 0x1d)) {
            SWeaponManager_FireSecondary(w);
            held = w.secondary_held;
        }
    }
    if (held != 0 && w.secondary == 0) SWeaponManager_FireSecondary(w);
    if (w.special_held != 0 && w.special != -1) {
        bool fire = true;
        if (const float* trig = nf::p32_ptr<const float>(w.special_trigger)) {
            float player[3] = {0, 0, 0};
            if (g_env.player_position) g_env.player_position(player);
            const float d[3] = {vu_sub(trig[0], player[0]), vu_sub(trig[1], player[1]),
                                vu_sub(trig[2], player[2])};
            const float r2 = ee::mul(w.special_range, w.special_range);
            if (ee::lt(r2, ps2::ffloat(vu_len2(d)))) fire = false;
        }
        if (fire) SWeaponManager_FireSpecial(w);
    }

    // As armas em uso: a recarga cano a cano (o estímulo 11) e os sistemas a ligar ou desligar.
    for (int32_t i = 0; i < kWeaponCount; ++i) {
        WeaponEntry& e = t[i];
        if (is_active(w, i)) {
            bool reload_sound = false;
            if (e.flags & 4) {
                for (;;) {
                    bool more = false;
                    if (fire_time(e, e.reload_barrel) + e.fire_delay - 5 < steps()) {
                        // Com as mãos de Bond na direção automática, o braço recarrega o pente vazio.
                        const uint32_t hands = m_auto_drive() != 0 ? hands_globals().hands : 0u;
                        if (hands != 0) more = AICharacterHands_AbleToFire(hands) && e.ammo > 0 && e.clip < 1;
                        else more = e.ammo - e.clip > 0;
                        if (more) {
                            if (hands != 0) {
                                AICharacterHands_Reload(hands);
                            } else {
                                SWeaponManager_DoWeaponAnim(w, i, 0xb, e.reload_barrel);
                                ++g_stats.reloads;
                                if (i != 2 || (e.reload_barrel & 1) || e.clip_size == 4) ++e.clip;
                                if (i != 0) reload_sound = true;
                            }
                            w.reload_pending = 1;
                            ++e.reload_barrel;
                            if (SWeaponManager_GetSystemFromWeapon(i, e.reload_barrel) == 0) e.reload_barrel = 0;
                            if (e.reload_barrel == e.barrel) e.flags &= ~4u;
                        }
                    }
                    if ((e.flags & 4) == 0 || !more) break;
                }
            }
            if (reload_sound && e.sound_bank >= 0) {
                const std::string name = weapon_sound_name(e, "Reload");
                sound(e.sound_bank, name.c_str(), m_auto_drive() != 0 ? kStrPOVWeapons : "Weapons Reload", 0);
            }
        }
        for (int k = 0; k < 8 && kWeaponSystems[i][k] != 0; ++k) {
            const int32_t sys = kWeaponSystems[i][k];
            if (i == w.special || (e.ammo > 0 && (i == w.primary || i == w.secondary))) {
                keep[sys] = 1;
                stop[sys] = 0;
            } else if (keep[sys] == 0) {
                stop[sys] = 1;
            }
        }
    }
    if (w.next_select < steps()) {
        const uint32_t h = anim_handle(w);
        for (int32_t s = 0; s < 0x4a; ++s) {
            if (anim_playing(h, s)) continue;
            const int32_t state = anim_state(h, s);
            if (keep[s] != 0 && state == 1) {
                anim_stimulus(h, s, 8, steps(), 2);
                const int32_t wt = SWeaponManager_GetWeaponFromSystem(s);
                if (wt < 0) {
                    ++g_stats.guards;
                } else if ((t[wt].flags & 0x40) == 0) {
                    t[wt].flags |= 0x20;
                }
            } else if (stop[s] != 0 && state == 2) {
                anim_stimulus(h, s, 9, steps(), 2);
            }
        }
    }
    for (int32_t i = 0; i < kWeaponCount; ++i) {
        WeaponEntry& e = t[i];
        if (e.flags & 0x20) {
            if (e.sound_bank >= 0) {
                const std::string name = weapon_sound_name(e, "Init");
                sound(e.sound_bank, name.c_str(), m_auto_drive() != 0 ? kStrPOVWeapons : "Weapons Init", 0);
            }
            e.flags = (e.flags & ~0x20u) | 0x40u;
        }
        if (!is_active(w, i)) e.flags &= ~0x40u;
    }
    {
        const uint32_t h = anim_handle(w);
        for (int32_t i = kWeaponCount - 1, n = 0; i >= 0; --i, ++n) {
            WeaponEntry& e = t[n];
            const int32_t sys = SWeaponManager_GetSystemFromWeapon(e.type, e.barrel);
            if (anim_playing(h, sys)) e.flags |= 2;
            else e.flags &= ~2u;
        }
    }
    // O Q-Smoke recém-disparado: a zona da fumaça atrás do carro para a IA (AIZoneController::RegisterZone).
    {
        WeaponEntry& e = t[11];
        if ((e.flags & 2) && w.last_fired == 0xb) {
            if (steps() < fire_time(e, e.barrel) + e.fire_delay - 5) {
                float pos[4] = {0, 0, 0, 0}, fwd[4] = {0, 0, 0, 0}, back = 0;
                if (g_env.player_body) g_env.player_body(pos, fwd, back);
                const float k = ee::neg(back);
                float p[3];
                for (int c = 0; c < 3; ++c) p[c] = ee::add(pos[c], ee::mul(fwd[c], k));
                weapons_note_call(kFnRegisterZone, 4, ps2::fbits(p[0]), ps2::fbits(p[1]), ps2::fbits(p[2]));
            }
        }
    }
    {
        const char* type = g_env.car_type ? g_env.car_type() : "";
        if (nf::str_icmp(type ? type : "", "rental") == 0) {
            weapons_note_call(kFnDeactivateTargeting);
            WTargetPicker_DeactivateTargeting();
        }
    }
    SWeaponManager_UpdateRadarDish(w);
    SWeaponManager_UpdateTurret(w);
    // O desfoque do turbo.
    if (w.boost_blur > 0) w.boost_blur = ee::cvt_w(ee::mul(ee::cvt_s(w.boost_blur), ps2::ffloat(0x3f7d70a4u)));
    int32_t blur = ee::cvt_w(ee::mul(ee::cvt_s(w.boost_blur), 1.5f));
    if (blur >= 0x61) blur = 0x60;
    if ((uber_bond().word(0x9a4) & 2) && !(0x4f < blur)) blur = 0x50;
    if (over || simulation().state == 3 || m_cinematic() != 0) blur = 0;
    if (g_env.hud_exists && g_env.hud_exists()) {
        if (!(g_env.hud_lava_damage && g_env.hud_lava_damage())) {
            const float f = ee::mul(ee::cvt_s(blur), 0.0078125f);
            weapons_note_call(kFnMotionBlur, ps2::fbits(f));
        }
    }
    SWeaponManager_HandleNuclearSequence(w);
    // O "distance click" dos helicópteros inimigos (o AIVehicleController): sem helicópteros, o contador zera.
    if (g_env.helicopters > 0) ++g_stats.pending;
    w.click_count = 0;
    // A mira.
    // (as anotações ficam para a parte 18; o WTargetPicker de verdade em game/target_picker.cpp)
    if (m_auto_drive() != 0) {
        weapons_note_call(kFnActivateTargeting, 1);
        WTargetPicker_ActivateTargeting(1);
    } else if (entry_type(w, w.special) == 0xe) {
        weapons_note_call(kFnActivateTargeting, 0);
        WTargetPicker_ActivateTargeting(0);
    } else {
        const int32_t sec = entry_type(w, w.secondary);
        if (sec == 2 || sec == 0x15 || lookup_bool("HAS_TURRET")) {
            weapons_note_call(kFnActivateTargeting, 0);
            WTargetPicker_ActivateTargeting(0);
        } else {
            weapons_note_call(kFnDeactivateTargeting);
            WTargetPicker_DeactivateTargeting();
        }
    }
}

// [driving] 0x0020d9a8 SWeaponManager::UpdateRadarDish: o sistema 0x36 (a antena do Radar) parado é só consultado.
void SWeaponManager_UpdateRadarDish(SWeaponManagerData& w) {
    const uint32_t h = anim_handle(w);
    if (!anim_playing(h, 0x36)) (void)anim_state(h, 0x36);
}

// [driving] 0x0020d9f0 SWeaponManager::UpdateTurret: só os carros com HAS_TURRET (nenhum em Paris); a torre (o acos
// do newlib, as comparações em dupla precisão por software) não foi portada: anotada e contada.
void SWeaponManager_UpdateTurret(SWeaponManagerData& w) {
    (void)w;
    if (simulation().state == 3) return;
    if (!lookup_bool("HAS_TURRET")) return;
    ++g_stats.pending;
    weapons_note_call(kFnUpdateTurret);
}

// [driving] 0x0020ea60 SWeaponManager::HandleNuclearSequence: sem o ENuclearBlast (+0x54 = 0) nada; a seqüência
// (a explosão de tipo 7, o empurrão no carro, a câmera fixa) não foi portada: anotada e contada.
void SWeaponManager_HandleNuclearSequence(SWeaponManagerData& w) {
    if (w.nuclear_state == 0) return;
    ++g_stats.pending;
    weapons_note_call(kFnHandleNuclearSequence, uint32_t(w.nuclear_state));
}

void SWeaponManager_FireWeapon(SWeaponManagerData& w, int32_t type) {
    const int32_t now = steps();
    const uint32_t target = g_env.selected_target ? g_env.selected_target() : 0;
    // Um tipo fora da tabela (a secundária -1 depois do EUnloadWeapon) lê uma arma vazia: sem munição, volta.
    if (type < 0 || type >= kWeaponCount) {
        ++g_stats.guards;
        if (target != 0 && g_env.target_only_emp && g_env.target_only_emp(target)) {
            sound(4, kStrDryFire, kStrWeaponsFire, 0);
            return;
        }
        w.hands_fired = 0;
        return;
    }
    WeaponEntry& e = table(w)[type];
    if (target != 0 && g_env.target_only_emp && g_env.target_only_emp(target) && type != 0xe && type != 0) {
        sound(4, kStrDryFire, kStrWeaponsFire, 0);
        return;
    }
    if (e.ammo <= 0) {
        w.hands_fired = 0;
        return;
    }
    if (m_5ec() != 0 && (g_env.camera_arm_2c ? g_env.camera_arm_2c() : 1) == 0 && type == 0x1a) {
        sound(4, kStrDryFire, kStrPOVWeapons, 0);
        return;
    }
    if (!(w.next_select < now)) return;
    if (now < fire_time(e, e.barrel) + e.fire_delay) return;
    // Com a direção automática e as mãos de Bond (0x327670): o braço atira (AICharacterHands::Fire: a animação de tiro,
    // cujo evento faz o fuzil do braço criar o EFireWeapon); senão, o carro.
    const uint32_t hands = m_auto_drive() != 0 ? hands_globals().hands : 0u;
    if (hands != 0) {
        if (!AICharacterHands_AbleToFire(hands)) return;
        ++g_stats.fired;
        AICharacterHands_Fire(hands);
        w.hands_fired = 1;
        fire_time(e, e.barrel) = now;
    } else {
        if (e.flags & 2) return;
        const bool needs_ready = !(type == 0 || type == 1 || type == 2 || type == 0x10 || type == 0xa || type == 4 ||
                                   type == 5 || type == 6 || type == 7 || type == 0x14 || type == 0xc || type == 0x12);
        if (needs_ready && anim_state(anim_handle(w), SWeaponManager_GetSystemFromWeapon(type, e.barrel)) != 2) return;
        ++g_stats.fired;
        auto fire_barrel = [&](WeaponEntry& x, int32_t ty) {
            fire_time(x, x.barrel) = now;
            const int32_t b = x.barrel++;
            SWeaponManager_DoWeaponAnim(w, ty, 10, b);
        };
        WeaponEntry& missile = table(w)[2];
        if (type == 2) {
            if (w.multi_damage != 0 && !(e.ammo < missile.clip_size)) {
                // O pente inteiro: o cano 0 e o 1 (e o par de sempre abaixo).
                e.barrel = 0;
                e.last_fire[0] = now;
                fire_barrel(missile, 2);
                if (e.clip_size == 4) --e.clip;
                fire_barrel(missile, 2);
                --missile.clip;
            }
            fire_barrel(e, type);
            if (e.clip_size == 4) --e.clip;
        } else if (type == 0x15 && w.multi_damage != 0 && !(e.ammo < table(w)[0x15].clip_size)) {
            e.last_fire[0] = now;
            e.barrel = 1;
            SWeaponManager_DoWeaponAnim(w, 0x15, 10, 0);
            --e.clip;
        }
        fire_barrel(e, type);
        --e.clip;
        if (SWeaponManager_GetSystemFromWeapon(type, e.barrel) == 0) e.barrel = 0;
    }
    if (e.counts_as_shot != 0) inc_shots(1);
    ++e.shots;
    if (type == 0 || type == 0x18 || type == 0x1b || type == 0x1d) {
        if (!lookup_bool("HAS_TURRET") && e.counts_as_shot != 0) inc_shots(1);
    }
    switch (type) {
        case 3:  // Q-Booster
            sound(4, "SFX_boostFire", kStrWeaponsFire, 0);
            if (g_env.enable_rocket_boost ? g_env.enable_rocket_boost() : false) w.boost_blur = 0x78;
            weapons_note_call(kFnEnableRocketBoost);
            break;
        case 8:  // Detonator
            weapons_note_call(kFnDetonateRemoteMines);
            break;
        case 10:  // Q-Slick: a mancha no chão (RScorchWorld::ScorchGround, 4,2 m)
            ++g_stats.pending;
            weapons_note_call(kFnScorchGround);
            break;
        case 11:  // Q-Smoke
            sound(4, "SFX_smkscrnFire", kStrWeaponsFire, 0);
            break;
        case 12:  // Q-Worm
            sound(0, "SFX_uploadcode", kStrMenuSounds, 2);
            inc_magic(0x1f);
            break;
        case 13:  // Laser
            weapons_note_call(kFnFireLaser);
            break;
        case 15:  // Radar
            sound(0, "SFX_downloadcode", kStrMenuSounds, 2);
            // [driving] 0x0020d980 SWeaponManager::UseRadarDish
            inc_magic(0x20);
            break;
        case 18:  // 2 Wheels
            sound(4, "SFX_twowheelfire", kStrWeaponsFire, 0);
            weapons_note_call(kFnEnableTwoWheelStunt);
            break;
        case 20:  // Q-Charge
            set_magic(0x22, 1);
            break;
        default:
            break;
    }
    // A munição gasta.
    int32_t spent = 0;
    const bool consume = !(g_env.infinite_ammo != 0 || (uber_bond().word(0x9a0) & 2)) && type != 0;
    if (consume) {
        spent = 1;
        bool found = false;
        if (type == 0x1c) {
            spent = 2;
        } else if (type == 2) {
            spent = 2;
            const int32_t n = lookup_int("MISSILES_FIRED_SIMULTANEOUSLY", &found);
            if (found) spent = n;
            if (w.multi_damage != 0 && !(e.ammo < spent * 2)) spent *= 2;
        } else if (type == 1) {
            spent = 2;
            const int32_t n = lookup_int("ROCKETS_FIRED_SIMULTANEOUSLY", &found);
            if (found) spent = n;
        } else if (type == 0x15) {
            if (w.multi_damage != 0 && !(e.ammo < 2)) spent = 2;
        }
        e.ammo -= spent;
    }
    if (spent >= 2 && e.counts_as_shot != 0) inc_shots(spent - 1);
    if (g_env.infinite_ammo == 0) {
        if (m_auto_drive() != 0) {
            e.clip -= spent;
            if (e.clip < 0) e.clip = 0;
            if (e.ammo > 0 && e.clip <= 0) e.flags |= 4;
        } else {
            e.flags |= 4;
        }
    }
    w.last_target = target;
    w.last_fired = type;
}

// ---------------------------------------------------------------- os eventos

namespace {

constexpr uint32_t kFnGfxTrigger = 0x232f70, kFnSceneHide = 0x1c8b78, kFnSceneShow = 0x1c8b90;
constexpr uint32_t kFnTrackInstance = 0x2015d8, kFnUntrackInstance = 0x201690, kFnHandleStimuli = 0x1a2868;
constexpr uint32_t kFnGadgetOn = 0x244f48;

uint32_t rd32(uint32_t p32, uint32_t off) {
    uint32_t v = 0;
    if (const uint8_t* p = nf::p32_ptr<const uint8_t>(p32)) std::memcpy(&v, p + off, 4);
    return v;
}
float fl(uint32_t bits) { return ps2::ffloat(bits); }

// WWorld::GetSceneObjFromInstance (0x33aac8): o objeto de cena de uma instância (weapons_env().scene_obj_of).
uint32_t scene_obj_of(uint32_t inst) {
    return weapons_env().scene_obj_of ? weapons_env().scene_obj_of(inst) : 0;
}

}  // namespace

bool weapons_event_execute(const PendingEvent& e) {
    SWeaponManagerData* w = weapon_manager();
    switch (e.type) {
        // [driving] 0x001554c0 EPowerUp::~EPowerUp: o efeito "Pr_Dstry" um metro acima de onde o gatilho foi tocado
        // (GFX::Trigger), o objeto escondido (RSceneObj::Hide), o estímulo 10 na animação dele e o fim do
        // acompanhamento da instância (Simulation::UntrackInstance).
        case 25: {
            const float y1 = ee::add(fl(e.obj[4]), 1.0f);
            weapons_note_call(kFnGfxTrigger, e.obj[3], ps2::fbits(y1), e.obj[5], 0, "Pr_Dstry");
            const uint32_t holder = e.obj[2];
            if (holder != 0 && rd32(holder, 8) != 0) {
                const uint32_t scene = rd32(holder, 8);
                weapons_note_call(kFnSceneHide);
                if (const uint32_t handle = rd32(scene, 0x28)) {
                    (void)handle;
                    weapons_note_call(kFnHandleStimuli, 10, uint32_t(simulation().steps), 2);
                }
            }
            weapons_note_call(kFnUntrackInstance);
            return true;
        }
        // [driving] 0x001556b8 EPowerUpAmmo::~EPowerUpAmmo: a mensagem (o rótulo +0xc da arma sem nada, o +0x10 com
        // munição), a munição (LoadWeapon) e o som "SFX_PowerupWeap".
        case 56: {
            const int32_t type = int32_t(e.obj[1]), ammo = int32_t(e.obj[2]);
            if (ammo <= 0) return true;
            if (!w) {
                ++g_stats.guards;
                return true;
            }
            const WeaponEntry& x = entry_or_zero(*w, type);
            if (hud_exists()) {
                const uint32_t label = (x.flags & 1) && x.ammo >= 1 ? x.msg_clip : x.msg_ammo;
                GHud_TriggerMissionMsg(hud(), 2.0f, int32_t(label));
                weapons_note_call(0x245890, label);  // GHud::TriggerMissionMsg (o HUD do porte também a recebe)
            }
            SWeaponManager_LoadWeapon(*w, type, ammo, 0, 0);
            sound(0, "SFX_PowerupWeap", "Power Ups", 2);
            return true;
        }
        // [driving] 0x00155a70 EPowerUpMultiDamage::~EPowerUpMultiDamage
        case 60:
            if (w) w->multi_damage = 1;
            else ++g_stats.guards;
            return true;
        // [driving] 0x0014b630 EDisablePowerUp::~EDisablePowerUp / 0x0014bb58 EEnablePowerUp::~EEnablePowerUp: a
        // bandeira 1 do estado da animação procedural (+0x12), a da instância (+0xc), o objeto de cena escondido ou
        // mostrado e o acompanhamento da instância (o Enable não acompanha as de modelo 0x696).
        case 149:
        case 150: {
            const bool enable = e.type == 150;
            if (uint8_t* state = nf::p32_ptr<uint8_t>(e.obj[2])) {
                uint16_t f;
                std::memcpy(&f, state + 0x12, 2);
                f = enable ? uint16_t(f | 1) : uint16_t(f & 0xfffe);
                std::memcpy(state + 0x12, &f, 2);
            }
            if (uint8_t* inst = nf::p32_ptr<uint8_t>(e.obj[1])) {
                inst[0xc] = enable ? uint8_t(inst[0xc] & 0xfe) : uint8_t(inst[0xc] | 1);
                if (scene_obj_of(e.obj[1]) != 0) weapons_note_call(enable ? kFnSceneShow : kFnSceneHide);
                if (enable) {
                    const uint32_t model = rd32(rd32(rd32(rd32(e.obj[1], 0x1c), 0), 0xc), 0x28);
                    if (model != 0x696) weapons_note_call(kFnTrackInstance);
                } else {
                    weapons_note_call(kFnUntrackInstance);
                }
            }
            return true;
        }
        // [driving] 0x0014f168 EHideInstance::~EHideInstance / 0x00159a88 EShowInstance::~EShowInstance: a cópia da
        // instância no RAnimEngine::Handle (EventDynamicData +0) escondida ou mostrada (o bit 1 de +0xc, que o
        // DrawInstance pula): o míssil disparado some do lançador e volta na recarga. O EShowInstance não confere o
        // ponteiro (no PS2 escreveria no endereço 0xc): aqui guarda.
        case 11:
        case 12:
            if (uint8_t* inst = nf::p32_ptr<uint8_t>(e.obj[1])) {
                inst[0xc] = e.type == 11 ? uint8_t(inst[0xc] | 1) : uint8_t(inst[0xc] & 0xfe);
            } else if (e.type == 12) {
                ++g_stats.guards;
            }
            return true;
        // [driving] 0x00158df0 ESetSpecialWeapon::~ESetSpecialWeapon
        case 192:
            if (w) SWeaponManager_SetSpecialWeapon(*w, int32_t(e.obj[1]), e.obj[2], fl(e.obj[3]));
            else ++g_stats.guards;
            return true;
        // [driving] 0x0014f0a0 EGadgetOn::~EGadgetOn: GHud::GadgetOn(o tempo, o aparelho) (o HUD das galerias, anotado)
        case 249:
            weapons_note_call(kFnGadgetOn, e.obj[1], e.obj[2]);
            return true;
        // [driving] 0x0015dcf8 EUnloadWeapon::~EUnloadWeapon (o SWeaponManager::UnloadWeapon, 0x20d420)
        case 269:
            if (w) SWeaponManager_UnloadWeapon(*w, int32_t(e.obj[1]));
            else ++g_stats.guards;
            return true;
        default:
            return false;
    }
}

const WeaponWord kWeaponDefaultTable[kWeaponCount][21] = {
    {0, "Gun", 1611, 1611, 1611, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 10, 1, 0, 4, "mntmach", 0, 0, 0},
    {1, "Rocket", 1602, 1664, 1664, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "Rocket", 0, 0, 0},
    {2, "Missile", 1615, 1665, 1665, 0, 0, 4, 4, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "Missile", 0, 0, 0},
    {3, "Q-Booster", 1614, 1674, 1674, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 0, 0, 0, 4, "Boost", 0, 0, 0},
    {4, "Time Mine", 1628, 1628, 1628, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 100, 1, 0, 4, "", 0, 0, 0},
    {5, "Prox Mine", 1627, 1627, 1627, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 100, 1, 0, 4, "Hmine", 0, 0, 0},
    {6, "Rem Mine", 1629, 1629, 1629, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 500, 1, 0, 4, "", 0, 0, 0},
    {7, "Floating Mine", 1629, 1629, 1629, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 1000, 1, 0, 4, "", 0, 0, 0},
    {8, "Detonator", 3000, 3000, 3000, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 0, 0, 0, 4, "", 0, 0, 0},
    {9, "Spikes", 3000, 3000, 3000, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 25, 0, 0, 4, "", 0, 0, 0},
    {10, "Q-Slick", 1616, 1663, 1663, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 0, 0, 0, 4, "", 0, 0, 0},
    {11, "Q-Smoke", 1621, 1666, 1666, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 0, 0, 0, 4, "smkscrn", 0, 0, 0},
    {12, "Q-Worm", 3000, 3000, 3000, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 25, 0, 0, 4, "", 0, 0, 0},
    {13, "Laser", 1610, 1610, 1610, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 25, 0, 0, 4, "", 0, 0, 0},
    {14, "Q-Pulse", 1606, 1675, 1675, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 50, 1, 0, 4, "EMP", 0, 0, 0},
    {15, "Radar", 1640, 3000, 3000, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 0, 0, 0, 4, "qlocate", 0, 0, 0},
    {16, "Cannon", 3000, 3000, 3000, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "cannon", 0, 0, 0},
    {17, "Q-Vision", 1649, 1669, 1669, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 0, 0, 0, 4, "", 0, 0, 0},
    {18, "2 Wheels", 3000, 3000, 3000, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 0, 0, 0, 4, "TwoWheel", 0, 0, 0},
    {19, "Grenade", 3000, 3000, 3000, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 500, 1, 0, 4, "grenade", 0, 0, 0},
    {20, "Q-Charge", 3000, 3000, 3000, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 5000, 0, 0, 4, "Hmine", 0, 0, 0},
    {21, "Torpedo", 2553, 2555, 2554, 0, 0, 2, 2, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "vtorpedo", 0, 0, 0},
    {22, "Remote Torpedo", 2556, 2558, 2557, 0, 0, 2, 2, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "vGtorp", 0, 0, 0},
    {23, "P2K", 3000, 3000, 3000, 0, 0, 10, 10, -1000, -1000, -1000, -1000, 10, 1, 0, 4, "HHandGun", 0, 0, 0},
    {24, "Snowmobile gun", 2700, 1670, 1676, 0, 0, 50, 50, -1000, -1000, -1000, -1000, 50, 1, 0, 4, "snomach", 0, 0, 0},
    {25, "Snowmobile Rocket", 2701, 1693, 1677, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "grenade", 0, 0, 0},
    {26, "Sniper Rifle", 2550, 2552, 2551, 0, 0, 30, 30, -1000, -1000, -1000, -1000, 10, 1, 0, 4, "sniper", 0, 0, 0},
    {27, "Ultralight gun", 2704, 3000, 3000, 0, 0, 10, 10, -1000, -1000, -1000, -1000, 30, 1, 0, 4, "Umntmach", 0, 0, 0},
    {28, "Ultralight rocket", 2705, 1667, 1667, 0, 0, 10, 10, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "Umissile", 0, 0, 0},
    {29, "FOD Gun", 8709, 1670, 1676, 0, 0, 50, 50, -1000, -1000, -1000, -1000, 50, 1, 0, 4, "fingergun", 0, 0, 0},
    {30, "FOD Rocket", 8710, 1671, 1677, 0, 0, 1, 1, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "finger", 0, 0, 0},
    {31, "CH-6 Remote Rocket", 1647, 1683, 1678, 0, 0, 10, 10, -1000, -1000, -1000, -1000, 250, 1, 0, 4, "Rocket", 0, 0, 0},
};
const float kWeaponFireDelay[kWeaponCount] = {
    0.166666672f, 0.25f, 0.666666687f, 4.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 1.0f, 1.0f,
    1.0f, 2.0f, 2.0f, 1.0f, 1.0f, 3.0f, 0.25f, 0.0833333358f,
    0.0833333358f, 0.0833333358f, 0.0833333358f, 0.0500000007f, 1.0f, 0.0833333358f, 5.0f, 0.0833333358f};
const int32_t kWeaponSortOrder[kWeaponCount] = {0,  1,  2,  3,  14, 4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 15,
                                                 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
const int32_t kWeaponSystems[kWeaponCount][8] = {
    {9},    {5, 6},       {7, 8, 0x34, 0x35}, {0xd},  {0x19}, {0x19}, {0x19}, {0x41}, {0},    {0x17}, {0xc},
    {0xe},  {0},          {0x16},             {0x24}, {0x36}, {0x48}, {0},    {0},    {0x26}, {0},    {0x3d, 0x3e},
    {0x3f, 0x40}, {0x21}, {0x22},             {0x23}, {0x25}, {0x2e}, {0x2d}, {0x46}, {0x47}, {0x2f}};

}  // namespace drv
