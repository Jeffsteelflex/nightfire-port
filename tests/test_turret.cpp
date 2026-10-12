// Testes das torretas (engine/turret.h): a escolha do alvo (GT_Track), os estados de GT_Update (abrir quando os scripts
// param, atordoar, destruir, recolher a automática sem alvo), GT_Register, GT_Activate e GT_Disable, com uma torreta
// montada à mão (os scripts parados, sem streams) num mundo sem geometria.

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/sound.h"
#include "engine/turret.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

struct TurretWorld {
    alignas(16) obj_tag player{};
    alignas(16) BLData bloke{};
    alignas(16) SCRIPTINFO body{};
    alignas(16) SCRIPTINFO base{};
    obj_tag* gun = nullptr;
    GUNTURRET* gt = nullptr;
    TurretWorld(int16_t kind) {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        object_debug_power_on();
        collide_debug_power_on();
        Coll_ResetHitHeap();
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        hashtable_debug_power_on();
        hashtable_initialise();
        sound_debug_power_on();
        turret_debug_power_on();
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(&GameState, 0, sizeof(GameState));
        std::memset(MPSettings, 0, sizeof(MPSettings));
        GS_SetRefreshRate(60, 60);
        std::memset(weapon_data, 0, sizeof(weapon_data));
        for (int i = 0; i < kWeaponCount; ++i) weapon_def(i)->id = weapon_def(i)->klass = int16_t(i);
        // O jogador a 10 m na frente (+z), vivo e ativo.
        player.type = 3;
        player.data = &bloke;
        player.pos = {0.0f, 0.0f, 10.0f, 1.0f};
        player.sphere = {0.0f, 0.0f, 10.0f, 1.0f};
        bloke.health = 100.0f;
        bloke.active = 1;
        std::memset(glb_players, 0, sizeof(glb_players));
        glb_players[0] = &player;
        glb_blokes[0] = &bloke;
        // A torreta na origem, olhando para +z; o corpo e a base parados.
        _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f};
        _VECTOR rot{};
        gun = control_create_object(0x100, &pos, &rot, nullptr);
        gun->type = 0x36;
        gt = static_cast<GUNTURRET*>(gun->data);
        std::memset(gt, 0, sizeof(*gt));
        gt->kind = kind;
        gt->health = 100.0f;
        gt->ammo = kind == 0 ? 200 : -1;
        gt->owner = gun;
        gt->facing = {0.0f, 0.0f, 1.0f, 0.0f};
        Mat_IdentityT(&body.base);
        gt->body = &body;
        gt->base = &base;
        Mat_IdentityT(&gt->aim);
        Mat_IdentityT(&gt->rest);
    }
    ~TurretWorld() {
        glb_players[0] = nullptr;
        glb_blokes[0] = nullptr;
    }
};

}  // namespace

TEST(turret_tracks_the_living_player_in_front_within_50_m) {
    TurretWorld w(1);
    CHECK(GT_Track(w.gt, w.gun) == &w.player);
    // Atrás da torreta (a frente é +z): nada.
    w.player.pos.z = w.player.sphere.z = -10.0f;
    CHECK(GT_Track(w.gt, w.gun) == nullptr);
    // Longe demais (mais de 50 m: a distância ao quadrado contra 2500).
    w.player.pos.z = w.player.sphere.z = 51.0f;
    CHECK(GT_Track(w.gt, w.gun) == nullptr);
    w.player.pos.z = w.player.sphere.z = 49.0f;
    CHECK(GT_Track(w.gt, w.gun) == &w.player);
    // Morto ou parado (BLData::active 0): nada.
    w.bloke.health = 0.0f;
    CHECK(GT_Track(w.gt, w.gun) == nullptr);
    w.bloke.health = 100.0f;
    w.bloke.active = 0;
    CHECK(GT_Track(w.gt, w.gun) == nullptr);
    // A torreta que o próprio jogador montou não mira nele.
    w.bloke.active = 1;
    w.gt->owner = &w.player;
    CHECK(GT_Track(w.gt, w.gun) == nullptr);
}

TEST(automatic_turret_opens_only_with_its_channels_and_a_target) {
    TurretWorld w(1);
    w.gt->enable = 0x40;
    w.gt->disable = 0x41;
    GT_Update(w.gun);
    CHECK(w.gun->state == 0);
    switch_channels[0x40] = 1;
    switch_channels[0x41] = 1;
    GT_Update(w.gun);
    CHECK(w.gun->state == 0);
    switch_channels[0x41] = 0;
    w.body.wait = 5;
    GT_Update(w.gun);
    // Abrindo: as pausas dos scripts soltas.
    CHECK(w.gun->state == 1 && w.body.wait == 0 && w.base.wait == 0);
    GT_Update(w.gun);
    CHECK(w.gun->state == 1);
    // Os dois scripts pararam na pausa do fim: aberta, com a orientação do corpo guardada.
    w.body.wait = -1;
    w.base.wait = -1;
    w.body.base.m[3][1] = 2.0f;
    GT_Update(w.gun);
    CHECK(w.gun->state == 4 && w.gt->aim.m[3][1] == 2.0f && w.gt->rest.m[3][1] == 2.0f);
    CHECK(g_turret_stats.opened == 1);
}

TEST(active_turret_fires_every_tenth_of_a_second_at_its_target) {
    TurretWorld w(1);
    w.gun->state = 4;
    const uint32_t before = g_turret_stats.shots;
    for (int t = 0; t < 60; ++t) GT_Update(w.gun);
    CHECK(w.gt->target == &w.player);
    // Um tiro quando o tempo passa de 0,1 s: 60 ticks a 60 Hz dão 9 (o sétimo tick de cada 7 atira).
    const uint32_t shots = g_turret_stats.shots - before;
    CHECK(shots >= 8 && shots <= 9);
    // A automática não gasta munição.
    CHECK(w.gt->ammo == -1);
}

TEST(automatic_turret_without_target_retracts_after_half_a_second) {
    TurretWorld w(1);
    w.gun->state = 4;
    w.player.pos.z = w.player.sphere.z = -10.0f;  // atrás: sem alvo
    for (int t = 0; t < 29; ++t) GT_Update(w.gun);
    CHECK(w.gun->state == 4);
    // O tempo soma 1/60 por tick e passa de 0,5 no 30º ou 31º (a soma em float).
    for (int t = 0; t < 2 && w.gun->state == 4; ++t) GT_Update(w.gun);
    CHECK(w.gun->state == 2);
    // Recolhendo 1,2 s, depois espera os scripts (estado 3) e volta a 0.
    for (int t = 0; t < 73; ++t) GT_Update(w.gun);
    CHECK(w.gun->state == 3);
    w.body.wait = -1;
    w.base.wait = -1;
    GT_Update(w.gun);
    CHECK(w.gun->state == 0);
}

TEST(turret_is_destroyed_when_health_runs_out_and_smokes_for_ten_seconds) {
    TurretWorld w(1);
    w.gun->state = 4;
    GT_ApplyDamage(w.gun, 101.0f);
    GT_Update(w.gun);
    CHECK(w.gun->state == 6 && g_turret_stats.destroyed == 1);
    for (int t = 0; t < 700; ++t) GT_Update(w.gun);
    CHECK(w.gun->state == 6 && w.gt->timer >= 10.0f && (w.gun->life & 1) == 0);
}

TEST(mounted_gun_runs_out_of_ammo_and_breaks) {
    TurretWorld w(0);
    w.gun->state = 4;
    w.gt->ammo = 2;
    for (int t = 0; t < 30 && w.gun->state == 4; ++t) GT_Update(w.gun);
    CHECK(w.gt->ammo == 0 && w.gun->state == 6);
}

TEST(taser_or_blast_stuns_an_active_turret_for_three_seconds) {
    TurretWorld w(1);
    w.gun->state = 0;
    GT_Disable(w.gun);
    CHECK(w.gun->state == 0);
    w.gun->state = 4;
    GT_Disable(w.gun);
    CHECK(w.gun->state == 5 && w.gun->unknown_f6 == 4);
    for (int t = 0; t < 180; ++t) GT_Update(w.gun);
    CHECK(w.gun->state == 5);
    GT_Update(w.gun);
    CHECK(w.gun->state == 4);
}

TEST(using_a_mounted_gun_packs_it_up_but_not_an_automatic_one) {
    TurretWorld w(1);
    w.gun->state = 4;
    GT_Activate(w.gun, &w.player);
    CHECK(w.gun->state == 4);
    w.gt->kind = 0;
    w.gun->state = 1;
    GT_Activate(w.gun, &w.player);
    CHECK(w.gun->state == 1);
    w.gun->state = 4;
    GT_Activate(w.gun, &w.player);
    CHECK(w.gun->state == 2);
}

TEST(turret_scripts_register_flash_barrel_and_part) {
    TurretWorld w(1);
    obj_tag a{}, b{}, c{}, d{};
    GT_Register(&a, 0, w.gt);
    GT_Register(&b, 1, w.gt);
    GT_Register(&c, 2, w.gt);
    GT_Register(&d, 3, w.gt);
    GT_Register(&d, 0, nullptr);
    CHECK(w.gt->flash == &a && w.gt->barrel == &b && w.gt->part == &c);
}
