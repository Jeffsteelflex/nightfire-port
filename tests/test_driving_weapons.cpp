// As armas do carro e o motor de animação das instâncias (driving/game/weapons.h, driving/render/anim_engine.h;
// docs/driving.md seção 22): propriedades independentes (a fila, a ordem das secundárias, o tiro e a recarga do
// lançador, a conversão do tempo do motor, um Handle montado à mão com um sistema, as suas animações e os eventos das
// chaves, as zonas de dano). A comparação com o código original fica na parte 18 do nf_driving_check.

#include <cstring>
#include <vector>

#include "driving/game/events.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
#include "driving/render/anim_engine.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

// Um gerente novo, sem missão, com a animação respondendo pelo `state` e anotando os estímulos.
struct Rig {
    std::vector<std::vector<int32_t>> stimuli;
    int32_t state = 2;
    Rig() {
        mission_reset();
        simulation_reset(60);
        weapons_env() = WeaponsEnv{};
        weapons_env().anim.state = [this](uint32_t, int32_t) { return state; };
        weapons_env().anim.is_playing = [](uint32_t, int32_t) { return false; };
        weapons_env().anim.stimulus = [this](uint32_t, int32_t s, int32_t st, int32_t, int32_t) {
            stimuli.push_back({s, st});
        };
        weapons_create();
    }
    ~Rig() {
        weapons_destroy();
        weapons_env() = WeaponsEnv{};
    }
    SWeaponManagerData& w() { return *weapon_manager(); }
};

}  // namespace

TEST(driving_weapons_action_queue_drops_the_oldest_when_full) {
    ActionQueueData q;
    ActionQueue_Construct(q, 0);
    CHECK(ActionQueue_IsEmpty(q));
    for (int i = 0; i < 205; ++i) ActionQueue_Push(q, {i, 0, 0.0f});
    CHECK(q.count == 200);
    // As cinco primeiras saíram: a mais antiga é a 5.
    CHECK(ActionQueue_GetAction(q)->action == 5);
    ActionQueue_PopAction(q);
    CHECK(ActionQueue_GetAction(q)->action == 6);
    ActionQueue_Flush(q);
    CHECK(ActionQueue_IsEmpty(q) && ActionQueue_GetAction(q) == nullptr);
}

TEST(driving_weapons_default_table_and_secondary_order) {
    Rig r;
    SWeaponManagerData& w = r.w();
    WeaponEntry* t = weapon_entry(w, 0);
    CHECK(std::strcmp(nf::p32_ptr<const char>(t[2].name), "Missile") == 0);
    CHECK(t[2].clip_size == 4 && t[2].damage == 250);
    // O intervalo do míssil: 0,666666687 s x 60 = 40,0000012, truncado em 40 passos.
    CHECK(t[2].fire_delay == 40);
    CHECK((t[0].flags & 1) != 0);  // a metralhadora sempre carregada
    // As secundárias carregadas e com munição, na ordem da fWeaponSortOrder (o Q-Pulse, 14, antes das minas).
    SWeaponManager_LoadWeapon(w, 4, 3, 0, 0);
    SWeaponManager_LoadWeapon(w, 14, 2, 0, 0);
    SWeaponManager_LoadWeapon(w, 2, 30, 0, 0);
    w.secondary = 2;
    SWeaponManager_SortWeaponList(w);
    CHECK(w.sorted_count == 3);
    CHECK(w.sorted[0] == 2 && w.sorted[1] == 14 && w.sorted[2] == 4);
    CHECK(SWeaponManager_GetNextSecondaryType(w) == 14);
    CHECK(SWeaponManager_GetPreviousSecondaryType(w) == 4);
}

TEST(driving_weapons_missile_fires_two_barrels_then_reloads) {
    Rig r;
    SWeaponManagerData& w = r.w();
    w.player_car = 1;
    SWeaponManager_LoadWeapon(w, 2, 30, 0, 0);
    w.secondary = 2;
    simulation().steps = 1000;
    SWeaponManager_FireWeapon(w, 2);
    WeaponEntry& m = *weapon_entry(w, 2);
    // Dois canos (os sistemas 7 e 8) com o estímulo 10, a munição -2, o pente -2 e a recarga pedida.
    CHECK(r.stimuli.size() == 2);
    CHECK(r.stimuli[0][0] == 7 && r.stimuli[0][1] == 10 && r.stimuli[1][0] == 8);
    CHECK(m.ammo == 28 && m.clip == 2 && m.barrel == 2 && (m.flags & 4) != 0);
    // O par seguinte (0x34 e 0x35) no mesmo passo: os canos 2 e 3 ainda não dispararam; depois o lançador volta ao
    // cano 0, que espera o intervalo.
    SWeaponManager_FireWeapon(w, 2);
    CHECK(r.stimuli.size() == 4 && r.stimuli[2][0] == 0x34 && r.stimuli[3][0] == 0x35);
    CHECK(m.ammo == 26 && m.clip == 0 && m.barrel == 0);
    SWeaponManager_FireWeapon(w, 2);
    CHECK(r.stimuli.size() == 4);
    // A recarga: com o prazo vencido o Update recarrega cano a cano (o estímulo 11) até voltar ao cano do tiro.
    r.stimuli.clear();
    simulation().steps = 1000 + m.fire_delay;
    SWeaponManager_Update(w);
    int reloads = 0;
    for (const auto& s : r.stimuli) reloads += s[1] == 11;
    CHECK(reloads == 4);
    CHECK(m.clip == 4 && (m.flags & 4) == 0);
}

TEST(driving_weapons_power_up_events) {
    Rig r;
    SWeaponManagerData& w = r.w();
    PendingEvent e{};
    e.type = 56;  // EPowerUpAmmo: 10 mísseis
    e.obj[1] = 2;
    e.obj[2] = 10;
    CHECK(weapons_event_execute(e));
    CHECK(weapon_entry(w, 2)->ammo == 10 && (weapon_entry(w, 2)->flags & 1) != 0);
    e.type = 60;  // EPowerUpMultiDamage
    CHECK(weapons_event_execute(e) && w.multi_damage == 1);
    e.type = 192;  // ESetSpecialWeapon: o Q-Smoke
    e.obj[1] = 11;
    e.obj[2] = 0;
    e.obj[3] = 0;
    CHECK(weapons_event_execute(e) && w.special == 11);
    e.type = 269;  // EUnloadWeapon
    e.obj[1] = 2;
    CHECK(weapons_event_execute(e) && weapon_entry(w, 2)->ammo == 0 && (weapon_entry(w, 2)->flags & 1) == 0);
    e.type = 41;  // um número que não é das armas
    CHECK(!weapons_event_execute(e));
}

TEST(driving_anim_engine_ticks_truncate_like_the_ee) {
    anim_engine_reset(60.0f);
    // passo x (1/60) x 60 com o arredondamento para zero do EE: 3 dá 2,9999998 e trunca em 2.
    CHECK(anim_ticks(0) == 0);
    CHECK(anim_ticks(3) == 2);
    CHECK(anim_ticks(60) == 59 || anim_ticks(60) == 60);
}

TEST(driving_anim_engine_handle_plays_an_animation_and_fires_its_key_events) {
    anim_engine_reset(60.0f);
    trigger_manager().pending.clear();
    // Uma instância com um artigo: o sistema 5 com duas animações (1 -8-> 2 em 2 quadros, com o evento 11 na chave 1;
    // 2 -9-> 1 em 1 quadro).
    alignas(16) static uint8_t inst[0x40], art[0x40], anims[0x30], keys[0x40], el[0x40];
    std::memset(inst, 0, sizeof inst);
    std::memset(art, 0, sizeof art);
    std::memset(anims, 0, sizeof anims);
    std::memset(keys, 0, sizeof keys);
    std::memset(el, 0, sizeof el);
    auto w32 = [](uint8_t* p, uint32_t off, uint32_t v) { std::memcpy(p + off, &v, 4); };
    auto w16 = [](uint8_t* p, uint32_t off, uint16_t v) { std::memcpy(p + off, &v, 2); };
    inst[0xd] = 0xff;
    w32(inst, 0x1c, nf::p32_u(art));
    w32(art, 8, nf::p32_u(anims));
    CarpAnimInfo* a = reinterpret_cast<CarpAnimInfo*>(anims);
    a[0] = {nf::p32_u(keys), 5, 5, 2, 0, 8, 1, 2, 1};
    a[1] = {nf::p32_u(keys), 5, 5, 1, 0, 9, 2, 1, 1};
    w32(keys, 0x20 + 0x1c, nf::p32_u(el));  // a chave 1
    w32(el, 0, 1);                            // um evento
    w32(el, 0x10, 11);                        // EHideInstance
    w32(el, 0x18, 0x10);                      // os dados logo depois
    w16(el, 0x1e, 0);
    const uint32_t h = AnimHandle_Create(1, 0, nf::p32_u(inst), 0, 0);
    const AnimHandleData& hd = *nf::p32_ptr<AnimHandleData>(h);
    CHECK(hd.n_systems == 1 && hd.n_instances == 1);
    CHECK(AnimHandle_GetSystemExists(h, 5) && !AnimHandle_GetSystemExists(h, 6));
    CHECK(art[0x1a] == 5 && art[0x1b] == 5);  // o Create grava o sistema no artigo
    CHECK(AnimHandle_GetSystemState(h, 5) == 1);
    AnimHandle_ProcessStimuli(h, 5, 8, 100, 0);
    CHECK(AnimHandle_IsSystemPlaying(h, 5) && anim_engine().count == 1);
    // Os tiques andam: a chave 1 cria o evento 11; no fim o sistema chega ao estado 2 e sai da lista.
    for (uint32_t s = 101; s < 120; ++s) RAnimEngine_Update(s);
    CHECK(!AnimHandle_IsSystemPlaying(h, 5) && anim_engine().count == 0);
    CHECK(AnimHandle_GetSystemState(h, 5) == 2);
    CHECK(trigger_manager().pending.size() == 1 && trigger_manager().pending[0].type == 11);
    // O estímulo sem animação daquele estado não toca.
    AnimHandle_ProcessStimuli(h, 5, 8, 200, 0);
    CHECK(!AnimHandle_IsSystemPlaying(h, 5));
    AnimHandle_Destroy(h);
    trigger_manager().pending.clear();
    anim_engine_reset();
}

TEST(driving_anim_engine_damage_zone_codes) {
    // A caixa do dano de -1..1 (x), -0,5..0,8 (y), -2,5..2,4 (z): o ponto é espelhado em x e z antes da conta.
    alignas(16) static uint8_t base[0x60];
    std::memset(base, 0, sizeof base);
    const float box[8] = {-1.0f, -0.5f, -2.5f, 0.0f, 1.0f, 0.8f, 2.4f, 0.0f};
    std::memcpy(base + 0x40, box, sizeof box);
    const uint32_t b = nf::p32_u(base);
    const float centre[3] = {0, 0, 0}, high[3] = {0, 2, 0}, low[3] = {0, -2, 0};
    const uint32_t z0 = BaseDesc_CalcDamageZone(b, centre);
    CHECK(z0 < 16);
    CHECK(BaseDesc_CalcDamageZone(b, high) != BaseDesc_CalcDamageZone(b, low));
    CHECK(BaseDesc_ZoneNeighbours(z0) != 0 && BaseDesc_ZoneNeighbours(16) == 0);
}
