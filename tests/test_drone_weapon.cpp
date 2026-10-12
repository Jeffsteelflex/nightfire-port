// Testes das armas da IA (engine/drone_weapon.cpp) sem dados do jogo: um personagem sintético com a
// boca da arma num anexo da pele, numa sala sem paredes, e definições de arma montadas em weapon_data.
// Conferem propriedades do comportamento (cadência das balas e das rajadas, tamanho das rajadas,
// limites do erro de pontaria e da pausa, condições para atirar, granadas, golpes, troca de arma), não
// uma cópia das funções.

#include <cmath>
#include <cstring>
#include <vector>

#include "engine/anim.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

constexpr int kGun = 0x20;      // arma sintética (nenhum caso especial nos projéteis nem no tiro)
constexpr int kMuzzleBone = 3;  // weapon_data +0x50: o anexo da boca é o de id 3 + 1
constexpr int kSkeletonSlot = 120;

alignas(16) Drone_tag g_d[2];
alignas(16) obj_tag g_o[2];
alignas(16) ANIMOBJ_tag g_anim[2];
alignas(16) MATRIX g_bones[2][32];
alignas(16) ANIMSKIN_tag g_skin;
alignas(16) uint8_t g_extra[0x24];
ANIMSKEL_tag g_skel;
uint8_t g_saved_weapons[kWeaponDataSize];

// weapon_data é global (o jogo copia do ELF): cada teste guarda e devolve.
struct WeaponDataGuard {
    WeaponDataGuard() { std::memcpy(g_saved_weapons, weapon_data, kWeaponDataSize); }
    ~WeaponDataGuard() { std::memcpy(weapon_data, g_saved_weapons, kWeaponDataSize); }
};

template <class T>
void wr(void* base, uint32_t offset, T v) {
    static_assert(!nf::is_native_ptr_v<T>, "ponteiro de 4 bytes: use P32<...>");
    std::memcpy(static_cast<uint8_t*>(base) + offset, &v, sizeof(T));
}

void identity(MATRIX& m) {
    std::memset(&m, 0, sizeof(m));
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
}

// Arma: 1 projétil por disparo, rajada (terceiro modo) "burst", "delay" ticks entre balas, sem som
// (0x5ef), sem clarão, boca no osso kMuzzleBone.
void make_gun(int id, int16_t burst, int16_t delay) {
    weapon_definition_tag* w = weapon_def(id);
    std::memset(w, 0, sizeof(*w));
    w->id = int16_t(id);
    w->klass = int16_t(id);
    w->damage = 1.0f;
    w->bullets = 1;
    w->range = 50.0f;
    w->speed = 1.0f;
    w->burst[2] = burst;
    w->fire_delay = uint32_t(uint16_t(delay));
    w->clip = 30;
    wr<uint8_t>(w, 0x50, kMuzzleBone);
    wr<uint16_t>(w, 0x60, 0x5ef);
}

void fresh() {
    psiMemHeapSize = 2u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    view_debug_power_on();
    bullet_debug_power_on();
    hashtable_debug_power_on();
    hashtable_initialise();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    std::memset(glb_viewer, 0, sizeof(glb_viewer));
    std::memset(glb_players, 0, sizeof(glb_players));
    pending_reset();
    GS_SetRefreshRate(60, 60);
    std::memset(&GameState, 0, sizeof(GameState));
    GameState.difficulty = 2;
    GameState.frame = 100;
    std::memset(MPSettings, 0, sizeof(MPSettings));
    effect_debug_power_on();
    Effect_InitLists();
    Light_Init();
    // As constantes do ACTION.ELF (DroneFiring_*).
    DroneFiring = {15.0f, 45.0f, 60.0f, 4.0f, 30.0f, 0.5f, 1.0f, 2.0f, 2.0f, 2.0f,
                   0.25f, 0.75f, 2.0f, 1.0f, 4.0f, 2.0f, 2.0f, 2.0f};
}

// Sala sem colisão (as balas voam até o alcance), de -50 a 50.
cel_tag* make_room() {
    cel_tag* cel = build_alloc_cel(glb_world->first);
    glb_world->first = cel;
    cel->type = 0x4c023 | 0x8000000;
    cel->box_min = {-50, -10, -50, 0};
    cel->box_max = {50, 50, 50, 0};
    return cel;
}

// Personagem i em pos, olhando para +z, com a arma "gun" na mão. A pose já está pronta (anim_flags
// 1 e 4): o osso 0 fica em pos + (0, 1, 0), e o anexo da boca da arma, 0,5 m à frente dele.
Drone_tag* make_drone(int i, cel_tag* cel, _VECTOR pos, int gun) {
    std::memset(&g_d[i], 0, sizeof(Drone_tag));
    std::memset(&g_o[i], 0, sizeof(obj_tag));
    std::memset(&g_anim[i], 0, sizeof(ANIMOBJ_tag));
    obj_tag* o = &g_o[i];
    Drone_tag* d = &g_d[i];
    o->type = 2;
    o->data = d;
    o->pos = pos;
    o->old_pos = pos;
    o->cel = cel;
    identity(o->matrix);
    o->matrix.m[3][0] = pos.x;
    o->matrix.m[3][1] = pos.y;
    o->matrix.m[3][2] = pos.z;
    o->sphere_src = reinterpret_cast<uint8_t*>(&g_anim[i]);
    ANIMOBJ_tag* a = &g_anim[i];
    a->weapon = int8_t(gun);
    a->anim_flags = 1 | 4;
    a->skin = &g_skin;
    a->bones = g_bones[i];
    d->obj = o;
    d->side = 1;
    d->flags = 0x100;
    d->health = 1.0f;
    d->max_health = 1.0f;
    d->behaviour = d->behaviour_a;
    d->stats[1] = 4;  // agressividade 1 e a pausa entre rajadas mais curta
    d->clip = d->clip_size = 30;
    d->anim_kind = 2;  // animação de mirar
    // A pele: um esqueleto de 1 osso e o anexo da boca (id kMuzzleBone + 1) preso ao osso 0.
    g_skel = {};
    g_skel.bone_count = 1;
    pSkeletons[kSkeletonSlot] = &g_skel;
    std::memset(&g_skin, 0, sizeof(g_skin));
    g_skin.skeleton = kSkeletonSlot;
    g_skin.n_extra = 1;
    g_skin.extras = g_extra;
    std::memset(g_extra, 0, sizeof(g_extra));
    wr<uint32_t>(g_extra, 0, kMuzzleBone + 1);
    wr<uint32_t>(g_extra, 4, 0);
    wr<float>(g_extra, 0x10, 0.5f);  // translação (0, 0, 0,5)
    wr<float>(g_extra, 0x20, 1.0f);  // quatérnio identidade (x, y, z, w)
    for (MATRIX& m : g_bones[i]) {
        identity(m);
        m.m[3][0] = pos.x;
        m.m[3][1] = pos.y + 1.0f;
        m.m[3][2] = pos.z;
    }
    return d;
}

// O oponente: outro personagem vivo em pos (Ready2Fire confere só a vida).
obj_tag* make_target(cel_tag* cel, _VECTOR pos) {
    Drone_tag* t = make_drone(1, cel, pos, 1);
    return t->obj;
}

void aim_at(Drone_tag* d, obj_tag* target) {
    d->opponent = target;
    d->opp_pos = target->pos;
    d->opp_delta = {target->pos.x - d->obj->pos.x, target->pos.y - d->obj->pos.y, target->pos.z - d->obj->pos.z,
                    0.0f};
    d->opp_dist = Vec_Magnitude(&d->opp_delta);
    d->opp_yaw = nf::fm::atan2(d->opp_delta.x, d->opp_delta.z);
    d->target_pos = {target->pos.x, target->pos.y + 1.0f, target->pos.z, target->pos.w};
}

DCVars_tag dcv_of(Drone_tag* d) { return {d->obj, d, d->obj->cel, &d->sm}; }

// Projéteis vivos atirados por obj.
std::vector<obj_tag*> bullets_from(obj_tag* obj) {
    std::vector<obj_tag*> out;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type == 5 && bullet_of(o)->from == obj) out.push_back(o);
    }
    return out;
}

int pending(const char* name) {
    const auto& m = pending_counts();
    const auto it = m.find(name);
    return it == m.end() ? 0 : it->second;
}

}  // namespace

TEST(drone_weapon_one_burst_fires_at_the_weapon_cadence) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    make_gun(kGun, 3, 4);
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    obj_tag* target = make_target(room, {0, 0, 2.5f, 1});
    aim_at(d, target);
    DCVars_tag dcv = dcv_of(d);
    DroneWeap_Fire(1, &dcv);
    d->one_burst = 1;
    CHECK(d->firing == 1 && d->burst == 3);
    const _VECTOR aim = d->target_pos;
    std::vector<uint32_t> shots;
    std::vector<obj_tag*> seen;
    for (int t = 0; t < 40; ++t) {
        ++GameState.frame;
        DroneWeap_HandleFiring(&dcv);
        for (obj_tag* b : bullets_from(d->obj)) {
            bool known = false;
            for (obj_tag* s : seen) known = known || s == b;
            if (!known) {
                seen.push_back(b);
                shots.push_back(GameState.frame);
            }
        }
    }
    // Uma rajada de 3 balas, uma a cada 4 ticks, e o pedido se desliga.
    CHECK(shots.size() == 3);
    if (shots.size() == 3) CHECK(shots[1] - shots[0] == 4 && shots[2] - shots[1] == 4);
    CHECK(d->firing == 0 && d->burst_over == 1 && d->fired == 1);
    CHECK(d->clip == 27 && d->last_shot == shots.back());
    // A menos de 3 m o tiro não erra: as balas saem da boca da arma direto para target_pos.
    const _VECTOR muzzle{0.0f, 1.0f, 0.5f, 0.0f};
    _VECTOR want{aim.x - muzzle.x, aim.y - muzzle.y, aim.z - muzzle.z, 0.0f};
    Vec_Normalise(&want, &want);
    for (obj_tag* b : seen) {
        const BU_tag* bu = bullet_of(b);
        CHECK(bu->weapon == weapon_def(kGun) && bu->owner == d->obj);
        CHECK_NEAR(bu->start.y, 1.0, 1e-5);
        CHECK_NEAR(bu->start.z, 0.5, 1e-5);
        CHECK_NEAR(bu->dir.x, want.x, 1e-5);
        CHECK_NEAR(bu->dir.y, want.y, 1e-5);
        CHECK_NEAR(bu->dir.z, want.z, 1e-5);
    }
    // O clarão: um detrito (tipo 0x10) de 2 ticks por disparo, na boca da arma.
    int flashes = 0;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) flashes += o->type == 0x10;
    CHECK(flashes == 3);
}

TEST(drone_weapon_bursts_are_separated_by_the_burst_pause) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    make_gun(kGun, 3, 4);
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    aim_at(d, make_target(room, {0, 0, 2.5f, 1}));
    DCVars_tag dcv = dcv_of(d);
    DroneWeap_Fire(1, &dcv);
    std::vector<uint32_t> shots;
    uint32_t last = d->last_shot;
    for (int t = 0; t < 80; ++t) {
        ++GameState.frame;
        DroneWeap_HandleFiring(&dcv);
        if (d->last_shot != last && d->fired != 0 && d->last_shot == GameState.frame) shots.push_back(GameState.frame);
        last = d->last_shot;
    }
    // Sem one_burst o personagem continua: 3 balas a cada 4 ticks, e entre as rajadas a pausa (a
    // menos de 4 m, a mínima: 15) mais o intervalo da arma.
    CHECK(shots.size() >= 6);
    if (shots.size() >= 6) {
        CHECK(shots[1] - shots[0] == 4 && shots[2] - shots[1] == 4);
        CHECK(shots[3] - shots[2] == 15 + 4);
        CHECK(shots[4] - shots[3] == 4 && shots[5] - shots[4] == 4);
    }
    CHECK(d->firing == 1);
}

TEST(drone_weapon_stops_when_the_opponent_is_lost_or_not_ready) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    make_gun(kGun, 3, 4);
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    obj_tag* target = make_target(room, {0, 0, 2.5f, 1});
    aim_at(d, target);
    DCVars_tag dcv = dcv_of(d);
    // Sem ver o oponente há 16 ticks: a rajada acaba sem atirar.
    d->lost_ticks = 16;
    DroneWeap_Fire(1, &dcv);
    d->one_burst = 1;
    ++GameState.frame;
    DroneWeap_HandleFiring(&dcv);
    CHECK(d->burst_over == 1 && bullets_from(d->obj).empty());
    ++GameState.frame;
    DroneWeap_HandleFiring(&dcv);
    CHECK(d->firing == 0);
    // Com 15 ticks ainda atira.
    d->lost_ticks = 15;
    DroneWeap_Fire(1, &dcv);
    ++GameState.frame;
    DroneWeap_HandleFiring(&dcv);
    CHECK(bullets_from(d->obj).size() == 1);
    // Ready2Fire: o oponente morto, a animação errada ou um script saindo impedem.
    CHECK(DroneWeap_Ready2Fire(&dcv) == 1);
    d->anim_kind = 4;
    d->blend_script = reinterpret_cast<ANIMSCRIPT_tag*>(&g_extra);
    CHECK(DroneWeap_Ready2Fire(&dcv) == 1);  // kind 4 sempre
    d->anim_kind = 2;
    CHECK(DroneWeap_Ready2Fire(&dcv) == 0);
    d->blend_script = nullptr;
    d->anim_kind = 0;
    CHECK(DroneWeap_Ready2Fire(&dcv) == 0);
    d->anim_kind = 2;
    static_cast<Drone_tag*>(target->data)->health = 0.0f;
    CHECK(DroneWeap_Ready2Fire(&dcv) == 0);
    d->opponent = nullptr;
    CHECK(DroneWeap_Ready2Fire(&dcv) == 0);
}

TEST(drone_weapon_ready_to_fire_with_the_aiming_animation_needs_the_barrel_on_target) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    make_gun(kGun, 3, 4);
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    aim_at(d, make_target(room, {0, 0, 10.0f, 1}));
    DCVars_tag dcv = dcv_of(d);
    // Com a mira da animação (0x1000) o cano (o -x do anexo da boca) tem de apontar para o alvo.
    g_anim[0].anim_flags |= 0x1000;
    // O cano aponta para -x: o alvo em -x está na mira; em +z (90 graus), não.
    d->target_pos = {-10.0f, 1.0f, 0.5f, 0.0f};
    CHECK(DroneWeap_Ready2Fire(&dcv) == 1);
    d->target_pos = {-10.0f, 1.0f, 0.5f + 10.0f * nf::fm::tan(0.3f), 0.0f};  // 17 graus
    CHECK(DroneWeap_Ready2Fire(&dcv) == 1);
    d->target_pos = {-10.0f, 1.0f, 0.5f + 10.0f * nf::fm::tan(0.4f), 0.0f};  // 23 graus
    CHECK(DroneWeap_Ready2Fire(&dcv) == 0);
    d->type = 0xd;  // 30 graus
    CHECK(DroneWeap_Ready2Fire(&dcv) == 1);
    d->type = 0;
    d->target_pos = {0.0f, 1.0f, 10.0f, 0.0f};
    CHECK(DroneWeap_Ready2Fire(&dcv) == 0);
    d->health = 0.0f;  // morrendo atira de qualquer jeito
    CHECK(DroneWeap_Ready2Fire(&dcv) == 1);
}

TEST(drone_weapon_burst_sizes_and_pause_bounds) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    DCVars_tag dcv = dcv_of(d);
    // Armas 6 e 10 sem rajada própria: 2 a 4 balas; 0xe a 0x10: 1 ou 2; com no mínimo 60 ticks entre
    // as balas mesmo com fire_delay menor.
    for (int gun : {6, 10, 0xe, 0xf, 0x10}) {
        make_gun(gun, 1, 2);
        g_anim[0].weapon = int8_t(gun);
        int lo = 99, hi = -1;
        for (int i = 0; i < 200; ++i) {
            DroneWeap_NextBulletTime(&dcv, 1, 0);
            lo = std::min<int>(lo, d->burst);
            hi = std::max<int>(hi, d->burst);
            d->last_shot = GameState.frame;
            DroneWeap_NextBulletTime(&dcv, 0, 0);
            CHECK(d->next_shot == GameState.frame + 60);
        }
        if (gun == 6 || gun == 10) {
            CHECK(lo == 2 && hi == 4);
        } else {
            CHECK(lo == 1 && hi == 2);
        }
    }
    // Outras armas: a rajada do terceiro modo, encolhida pela agressividade (stats[1]) se passa de 3,
    // mas nunca abaixo de 3; rajada 0 vira 1.
    make_gun(kGun, 10, 2);
    g_anim[0].weapon = kGun;
    const struct {
        uint8_t level;
        int16_t want;
    } cases[] = {{4, 10}, {3, 8}, {1, 5}, {0, 3}};  // x 1, 0,85, 0,5 e 0,3 (truncado, no mínimo 3)
    for (const auto& c : cases) {
        d->stats[1] = c.level;
        DroneWeap_NextBulletTime(&dcv, 1, 0);
        CHECK(d->burst == c.want);
    }
    d->stats[1] = 2;  // x 0,7
    DroneWeap_NextBulletTime(&dcv, 1, 0);
    CHECK(6 <= d->burst && d->burst <= 7);
    make_gun(kGun, 0, 2);
    DroneWeap_NextBulletTime(&dcv, 1, 0);
    CHECK(d->burst == 1);
    // A pausa entre rajadas fica entre o mínimo e o máximo; perto, a mínima; longe, a máxima.
    for (uint8_t level = 0; level < 6; ++level) {
        d->stats[1] = level;
        for (int i = 0; i < 100; ++i) {
            d->opp_dist = 10.0f;
            const uint32_t p = DroneWeap_BurstDelay(d);
            CHECK(15 <= p && p <= 60);
            if (level == 4) CHECK(p == 15);  // 45 x 0,33 x (0,5 a 1) < 15
            d->opp_dist = 3.0f;
            CHECK(DroneWeap_BurstDelay(d) == 15);
            d->opp_dist = 31.0f;
            CHECK(DroneWeap_BurstDelay(d) == 60);
        }
    }
    // A 30 Hz (FRAME_RATE_DIV 0,5) a pausa é a metade.
    GS_SetRefreshRate(30, 60);
    d->opp_dist = 31.0f;
    CHECK(DroneWeap_BurstDelay(d) == 30);
    GS_SetRefreshRate(60, 60);
}

TEST(drone_weapon_accuracy_error_is_bounded_and_close_shots_hit) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    obj_tag* target = make_target(room, {0, 0, 10.0f, 1});
    aim_at(d, target);
    DCVars_tag dcv = dcv_of(d);
    // Precisão 20 (chance 0): longe, sempre erra; o erro tem no máximo |(0,5, 1,5, 1,5)|.
    d->stats[0] = 20;
    d->seen_ticks = 1000;
    const float bound = std::sqrt(0.5f * 0.5f + 1.5f * 1.5f + 1.5f * 1.5f) + 1e-4f;
    int nonzero = 0;
    for (int i = 0; i < 200; ++i) {
        ++GameState.frame;
        DroneWeap_DoBulletAccuracy(&dcv);
        const float m = Vec_Magnitude(&d->aim_error);
        CHECK(m <= bound);
        nonzero += m > 0.0f;
    }
    CHECK(nonzero > 150);
    // O erro é o mesmo desvio girado para o lado do oponente: o comprimento não depende do ângulo.
    d->opp_yaw = 0.0f;
    DroneWeap_DoBulletAccuracy(&dcv);
    const float m0 = Vec_Magnitude(&d->aim_error);
    const float y0 = d->aim_error.y;
    d->opp_yaw = 1.2f;
    DroneWeap_DoBulletAccuracy(&dcv);
    CHECK_NEAR(Vec_Magnitude(&d->aim_error), m0, 1e-5);
    CHECK_NEAR(d->aim_error.y, y0, 1e-5);  // giro em volta de y
    // A menos de 3 m sempre acerta.
    d->opp_dist = 2.9f;
    for (int i = 0; i < 50; ++i) {
        DroneWeap_DoBulletAccuracy(&dcv);
        CHECK(Vec_Magnitude(&d->aim_error) == 0.0f);
    }
    // Precisão 0 (chance 100 ou mais): acerta só depois de ver o alvo por 2 s, e não logo depois de
    // perdê-lo.
    d->opp_dist = 10.0f;
    d->stats[0] = 0;
    GameState.difficulty = 3;  // x 2: a chance passa de 100
    d->seen_ticks = 119;
    DroneWeap_DoBulletAccuracy(&dcv);
    CHECK(Vec_Magnitude(&d->aim_error) > 0.0f);
    d->seen_ticks = 120;
    DroneWeap_DoBulletAccuracy(&dcv);
    CHECK(Vec_Magnitude(&d->aim_error) == 0.0f);
    d->lost_sight = 1;
    DroneWeap_DoBulletAccuracy(&dcv);
    CHECK(Vec_Magnitude(&d->aim_error) > 0.0f);
    // Sem oponente, nada muda.
    d->aim_error = {9.0f, 9.0f, 9.0f, 9.0f};
    d->opponent = nullptr;
    DroneWeap_DoBulletAccuracy(&dcv);
    CHECK(d->aim_error.x == 9.0f);
}

TEST(drone_weapon_target_movement_tracking) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    // O oponente é o jogador (velocidade em BLData::vel).
    alignas(16) static obj_tag player;
    alignas(16) static BLData bloke;
    std::memset(&player, 0, sizeof(player));
    std::memset(&bloke, 0, sizeof(bloke));
    player.type = 3;
    player.data = &bloke;
    d->opponent = &player;
    DCVars_tag dcv = dcv_of(d);
    // Parado: "acabou de parar" por 2 s (120 ticks), depois não.
    DroneWeap_DoOpponentTargetting(&dcv);
    CHECK(d->target_moving == 0 && d->target_stopped == 1 && d->target_change_time == GameState.frame);
    GameState.frame += 120;
    DroneWeap_DoOpponentTargetting(&dcv);
    CHECK(d->target_stopped == 1);
    ++GameState.frame;
    DroneWeap_DoOpponentTargetting(&dcv);
    CHECK(d->target_stopped == 0);
    // Só a velocidade no plano conta.
    bloke.vel = {0.0f, 0.3f, 0.0f, 0.0f};
    DroneWeap_DoOpponentTargetting(&dcv);
    CHECK(d->target_moving == 0);
    bloke.vel = {0.1f, 0.0f, 0.0f, 0.0f};
    DroneWeap_DoOpponentTargetting(&dcv);
    CHECK(d->target_moving == 1 && d->target_started == 1 && d->target_stopped == 0);
    const uint32_t started = d->target_change_time;
    CHECK(started == GameState.frame);
    GameState.frame += 121;
    DroneWeap_DoOpponentTargetting(&dcv);
    CHECK(d->target_started == 0 && d->target_change_time == started);  // não recomeça enquanto anda
    // Andando, a chance de acertar cai (x 0,75; logo depois de começar, x 0,25 a mais).
}

TEST(drone_weapon_grenade_arc_and_count) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    make_gun(0x34, 1, 1);
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    DCVars_tag dcv = dcv_of(d);
    d->look = 0x34;
    d->look_variant = 3;
    const auto throw_at = [&](_VECTOR target) -> obj_tag* {
        const std::vector<obj_tag*> before = bullets_from(d->obj);
        d->target_pos = target;
        DroneWeap_ThrowGrenade(&dcv);
        for (obj_tag* o : bullets_from(d->obj)) {
            bool old = false;
            for (obj_tag* b : before) old = old || b == o;
            if (!old) return o;
        }
        return nullptr;
    };
    // Na direção +z: a menos de 10 m 45 graus para baixo, além de 18 m 45 graus para cima, no meio reto;
    // saindo da mão (osso 0x15, em (0, 1, 0)).
    const float h = 0.70710678f;
    const struct {
        float dist;
        float y;
    } cases[] = {{5.0f, -h}, {14.0f, 0.0f}, {25.0f, h}};
    for (const auto& c : cases) {
        obj_tag* g = throw_at({0.0f, 0.0f, c.dist, 0.0f});
        CHECK(g != nullptr);
        if (g == nullptr) continue;
        const BU_tag* b = bullet_of(g);
        CHECK(b->weapon == weapon_def(0x34));
        CHECK_NEAR(b->start.y, 1.0, 1e-6);
        CHECK_NEAR(b->dir.x, 0.0, 1e-5);
        CHECK_NEAR(b->dir.y, c.y, 1e-5);
        CHECK_NEAR(b->dir.z, c.y == 0.0f ? 1.0f : h, 1e-5);
    }
    // Peculiaridade preservada: a inclinação é em volta do eixo x do mundo (MulMatrix2 depois de
    // RotMatrix), então na direção x a granada sai reta mesmo perto.
    obj_tag* side = throw_at({5.0f, 0.0f, 0.0f, 0.0f});
    CHECK(side != nullptr);
    if (side != nullptr) {
        CHECK_NEAR(bullet_of(side)->dir.x, 1.0, 1e-5);
        CHECK_NEAR(bullet_of(side)->dir.y, 0.0, 1e-5);
    }
    // Cada arremesso gasta uma; sem nenhuma a granada sai mesmo assim (a contagem fica em 0).
    CHECK(d->look_variant == 0);
    CHECK(throw_at({0.0f, 0.0f, 14.0f, 0.0f}) != nullptr && d->look_variant == 0);
    // Sem granada (look 0) nada sai, e não dá para jogar.
    d->look = 0;
    CHECK(throw_at({0.0f, 0.0f, 14.0f, 0.0f}) == nullptr);
    CHECK(DroneWeap_CanThrowGrenade(&dcv) == 0);
    // O DTYPE 0xd joga 0x35 ou 0x36 mesmo sem granada própria.
    make_gun(0x35, 1, 1);
    make_gun(0x36, 1, 1);
    d->type = 0xd;
    obj_tag* g = throw_at({0.0f, 0.0f, 14.0f, 0.0f});
    CHECK(g != nullptr && (bullet_of(g)->weapon == weapon_def(0x35) || bullet_of(g)->weapon == weapon_def(0x36)));
}

TEST(drone_weapon_close_combat_range) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    for (int w : {99, 100, 0x65, 0x66}) make_gun(w, 1, 1);
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    DCVars_tag dcv = dcv_of(d);
    const auto hit_weapon = [&](int kind) -> int {
        const std::vector<obj_tag*> before = bullets_from(d->obj);
        DroneWeap_CloseCombatImpact(&dcv, kind);
        for (obj_tag* o : bullets_from(d->obj)) {
            bool old = false;
            for (obj_tag* b : before) old = old || b == o;
            if (!old) return bullet_of(o)->weapon->id;
        }
        return -1;
    };
    d->subclass = 0xc;
    d->opp_delta = {0.0f, 0.0f, 1.5f, 0.0f};
    d->opp_dist = 1.5f;
    d->opp_bearing = 0.5f;
    CHECK(hit_weapon(1) == 0x65);
    CHECK(hit_weapon(2) == 0x66);
    CHECK(hit_weapon(5) == 100);
    d->opp_bearing = 1.1f;  // fora dos 60 graus da frente
    CHECK(hit_weapon(1) == -1);
    d->opp_bearing = 0.0f;
    d->opp_dist = 1.8f;
    CHECK(hit_weapon(1) == -1);
    // A subclasse 7 golpeia até 2 m, para qualquer lado, sempre com a arma 99.
    d->subclass = 7;
    d->opp_bearing = 3.0f;
    CHECK(hit_weapon(1) == 99);
    d->opp_dist = 2.1f;
    CHECK(hit_weapon(1) == -1);
}

TEST(drone_weapon_change_and_drop) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    DCVars_tag dcv = dcv_of(d);
    const struct {
        int weapon;
        uint16_t subclass;
        uint16_t clip;
    } table[] = {{1, 0xc, 0}, {0x36, 0xc, 0}, {6, 4, 0x10}, {0x12, 6, 0x20}, {0x18, 5, 0x20}, {0x19, 5, 0x20}};
    for (const auto& t : table) {
        DroneWeap_ChangeWeapon(&dcv, t.weapon);
        CHECK(g_anim[0].weapon == t.weapon && d->subclass == t.subclass && d->clip == t.clip &&
              d->clip_size == t.clip);
    }
    // Outras armas não mudam nada.
    DroneWeap_ChangeWeapon(&dcv, 2);
    DroneWeap_ChangeWeapon(&dcv, -1);
    DroneWeap_ChangeWeapon(&dcv, 0x37);
    CHECK(g_anim[0].weapon == 0x19 && d->subclass == 5);
    // Soltar: sem modelo de item (weapon_data +0x80) só esvazia a mão; com ele cria o item no chão
    // (Pickup_CreateSimple: a arma com 1/4 a 3/4 do pente de 30), e a arma 0x1a solta também a
    // munição 0x1b, escondida (flag 0x10).
    alignas(16) static celglist_tag item{};
    item.sphere = {0.0f, 0.0f, 0.0f, 0.2f};
    hashtable_additem(0x2000600, &item);
    const auto items = [] {
        std::vector<obj_tag*> out;
        for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
            if (o->type == kPickupObjectType) out.push_back(o);
        }
        return out;
    };
    make_gun(kGun, 1, 1);
    g_anim[0].weapon = kGun;
    DroneWeap_DropWeapon(&dcv);
    CHECK(g_anim[0].weapon == 0 && items().empty());
    wr<uint32_t>(weapon_def(kGun), 0x80, 0x2000600);
    g_anim[0].weapon = kGun;
    DroneWeap_DropWeapon(&dcv);
    CHECK(g_anim[0].weapon == 0 && items().size() == 1);
    if (items().size() == 1) {
        const PICKUPINFO* p = pickup_of(items()[0]);
        CHECK(p->kind == kPickupWeapon && p->item == kGun && 7 <= p->count && p->count <= 22);
        CHECK(p->state == kPickupFalling && items()[0]->glist == &item);
    }
    make_gun(0x1a, 1, 1);
    make_gun(0x1b, 1, 1);
    wr<uint32_t>(weapon_def(0x1a), 0x80, 0x2000600);
    wr<uint32_t>(weapon_def(0x1b), 0x80, 0x2000600);
    g_anim[0].weapon = 0x1a;
    DroneWeap_DropWeapon(&dcv);
    CHECK(g_anim[0].weapon == 0 && items().size() == 3);
    int hidden_ammo = 0;
    for (obj_tag* o : items()) {
        if (pickup_of(o)->kind == kPickupAmmo && pickup_of(o)->item == 0x1b && (o->flags & 0x10) != 0) ++hidden_ammo;
    }
    CHECK(hidden_ammo == 1);
    // Mão vazia: nada.
    DroneWeap_DropWeapon(&dcv);
    CHECK(items().size() == 3);
}

TEST(drone_weapon_damage_to_the_player) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    make_gun(kGun, 1, 1);
    make_gun(0x65, 1, 1);
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    alignas(16) static obj_tag player;
    std::memset(&player, 0, sizeof(player));
    player.type = 3;
    _VECTOR from{0, 1, 0, 1}, dir{0, 0, 1, 0};
    obj_tag* b = Bullet_init(-1, d->obj, d->obj, weapon_def(kGun), &from, &dir, 0xffff);
    CHECK(b != nullptr);
    if (b == nullptr) return;
    alignas(16) static HITDATA_tag hit;
    std::memset(&hit, 0, sizeof(hit));
    hit.obj = b;
    hit.dir = {0, 0, 1, 0};
    const auto damage_of = [&](float base) {
        std::memcpy(&hit.unknown_08, &base, 4);
        CHECK(Drone_ModPlayerHitDamage(&player, &hit));
        float out;
        std::memcpy(&out, &hit.unknown_08, 4);
        return out;
    };
    d->damage_mul = 1.0f;
    d->opp_dist = 10.0f;
    CHECK(damage_of(10.0f) == 10.0f);
    d->damage_mul = 2.0f;  // capitão
    CHECK(damage_of(10.0f) == 20.0f);
    d->opp_dist = 3.0f;  // perto: x too_close_damage
    CHECK(damage_of(10.0f) == 40.0f);
    // Pelas costas (o tiro na direção para onde o jogador olha): só a subclasse 0x12.
    d->damage_mul = 1.0f;
    d->opp_dist = 10.0f;
    player.rot.y = 0.0f;
    CHECK(damage_of(10.0f) == 10.0f);
    d->subclass = 0x12;
    CHECK(damage_of(10.0f) == 20.0f);
    player.rot.y = 3.1415927f;  // de frente
    CHECK(damage_of(10.0f) == 10.0f);
    // Golpes (0x63-0x66) passam como estão; desarmado (classe 0xc) não fere; bala de outro objeto
    // (não personagem) não muda.
    bullet_of(b)->weapon = weapon_def(0x65);
    d->opp_dist = 1.0f;
    CHECK(damage_of(10.0f) == 10.0f);
    bullet_of(b)->weapon = weapon_def(kGun);
    d->klass = 0xc;
    CHECK(damage_of(10.0f) == 0.0f);
    d->klass = 0;
    bullet_of(b)->from = &player;
    CHECK(damage_of(7.0f) == 7.0f);
}

TEST(drone_weapon_opponent_checks_and_turning) {
    WeaponDataGuard guard;
    fresh();
    cel_tag* room = make_room();
    Drone_tag* d = make_drone(0, room, {0, 0, 0, 1}, kGun);
    obj_tag* target = make_target(room, {3.0f, 0, 3.0f, 1});
    DCVars_tag dcv = dcv_of(d);
    // Arma do oponente: só jogador ou personagem com arma de fogo (2 a 0x46 menos 0x47; 0x6a a 0x71).
    d->opponent = target;
    g_anim[1].weapon = 1;
    CHECK(DroneWeap_OpponentHasWeapon(&dcv) == 0);
    g_anim[1].weapon = 2;
    CHECK(DroneWeap_OpponentHasWeapon(&dcv) == 1);
    CHECK(DroneWeap_OpponentIsAimingAtMe(&dcv, 30.0f) == 0);
    d->in_sights = 1;
    CHECK(DroneWeap_OpponentIsAimingAtMe(&dcv, 30.0f) == 1);
    target->type = 0x10;
    CHECK(DroneWeap_OpponentHasWeapon(&dcv) == 0 && DroneWeap_OpponentIsAimingAtMe(&dcv, 30.0f) == 0);
    target->type = 2;
    // De frente: |opp_facing| em graus.
    d->opp_facing = 0.5f;
    CHECK(NDrone2_BondIsFacingMe(&dcv, 30.0f) == 1 && NDrone2_BondIsFacingMe(&dcv, 28.0f) == 0);
    // Girar para o oponente (a 45 graus): move_angles.y aponta para ele; travado sem force.
    NDrone2_SetAngleToObj(&dcv, target, 0.1f, 0);
    CHECK_NEAR(d->move_angles.y, 0.7853982 + 0.1, 1e-3);
    d->move_angles.y = 0.0f;
    d->control_flags = 0x8000000;
    NDrone2_SetAngleToObj(&dcv, target, 0.0f, 0);
    CHECK(d->move_angles.y == 0.0f);
    NDrone2_SetAngleToObj(&dcv, target, 0.0f, 1);
    CHECK_NEAR(d->move_angles.y, 0.7853982, 1e-3);
    CHECK(DroneWeap_AimTarget_IsOpponent(1, 0, &dcv) == 0);  // travado
    d->control_flags = 0;
    d->move_angles.y = 0.0f;
    // AimTarget_IsOpponent: já de frente (< 0,9 rad), 1 sem girar; de lado, 0 sem keep_turning.
    d->obj->rot.y = 0.7f;
    CHECK(DroneWeap_AimTarget_IsOpponent(1, 0, &dcv) == 1 && d->move_angles.y == 0.0f);
    d->obj->rot.y = -1.0f;
    CHECK(DroneWeap_AimTarget_IsOpponent(1, 0, &dcv) == 0 && d->move_angles.y == 0.0f);
    CHECK(DroneWeap_AimTarget_IsOpponent(1, 1, &dcv) == 1);
    CHECK_NEAR(d->move_angles.y, 0.7853982, 1e-3);
    d->opponent = nullptr;
    CHECK(DroneWeap_AimTarget_IsOpponent(0, 0, &dcv) == 0);
}

TEST(drone_weapon_aggression_multiplier) {
    fresh();
    alignas(16) static Drone_tag d;
    std::memset(&d, 0, sizeof(d));
    const float want[] = {0.3f, 0.5f, 0.7f, 0.85f, 1.0f, 1.0f};
    for (uint8_t i = 0; i < 6; ++i) {
        d.stats[1] = i;
        CHECK(BOT_getAggressionMul(&d) == want[i]);
    }
    // Perto do oponente só conta com bots (até 3 vezes).
    alignas(16) static obj_tag opp;
    d.opponent = &opp;
    d.opp_dist = 0.0f;
    d.stats[1] = 1;
    CHECK(BOT_getAggressionMul(&d) == 0.5f);
    const uint32_t on = 1;
    std::memcpy(MPSettings + 0x184, &on, 4);
    CHECK_NEAR(BOT_getAggressionMul(&d), 1.5, 1e-6);
    d.opp_dist = 2.0f;
    CHECK_NEAR(BOT_getAggressionMul(&d), 0.75, 1e-6);
    std::memset(MPSettings, 0, sizeof(MPSettings));
}

TEST(mul_matrix2_composes_rotations_in_place) {
    // b = b x a (3x3), a linha 3 de b fica: duas rotações em x somam os ângulos.
    MATRIX a{}, b{}, c{};
    RotMatrixX(0.3f, &a);
    RotMatrixX(0.5f, &b);
    b.m[3][0] = 7.0f;
    b.m[3][1] = 8.0f;
    RotMatrixX(0.8f, &c);
    MulMatrix2(&a, &b);
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) CHECK_NEAR(b.m[r][k], c.m[r][k], 1e-6);
    }
    CHECK(b.m[3][0] == 7.0f && b.m[3][1] == 8.0f);
    // Igual a MulMatrix0(a, b, out) com uma cópia de b.
    MATRIX x{}, y{}, out{};
    RotMatrixY(0.7f, &x);
    RotMatrixZ(-0.2f, &y);
    MulMatrix0(&x, &y, &out);
    MulMatrix2(&x, &y);
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) CHECK(y.m[r][k] == out.m[r][k]);
    }
}
