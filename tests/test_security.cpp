// Testes do sistema de segurança e dos objetos de jogo que faltavam (engine/sensor.h, searchlight.h, hazard.h,
// gunimp.h, space_station.h e o inversor de trigger.h): as câmeras vendo o jogador pelo cone e levantando o alarme
// depois de 2 s, o monitor mostrando as câmeras do grupo na câmera do jogador, as contagens do SSys, o holofote seguindo
// o jogador preso ao arco e quebrando com um tiro, a mina explodindo com um toque, o atirador de um tiro só, o
// inversor, o tremor, o canhão montado mirando e superaquecendo e os mísseis da estação, num mundo sem geometria (os
// raios não batem em nada e a linha de visão é livre).

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/explode.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/gunimp.h"
#include "engine/hashtable.h"
#include "engine/hazard.h"
#include "engine/input.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/searchlight.h"
#include "engine/sensor.h"
#include "engine/sound.h"
#include "engine/space_station.h"
#include "engine/trigger.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

constexpr float kPi = 3.1415927f;

struct World {
    alignas(16) obj_tag player{};
    alignas(16) BLData bloke{};
    alignas(16) ANIMOBJ_tag anim{};
    alignas(16) obj_tag gun{};
    alignas(16) viewer_tag viewer{};
    viewer_tag* saved[kViewerCount];
    World() {
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
        explode_debug_power_on();
        lights_debug_power_on();
        Light_Init();
        security_debug_power_on();
        searchlight_debug_power_on();
        hazard_debug_power_on();
        gunimp_debug_power_on();
        space_station_debug_power_on();
        Sensor_Init();
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(switch_channels_time, 0, sizeof(switch_channels_time));
        std::memset(&GameState, 0, sizeof(GameState));
        std::memset(MPSettings, 0, sizeof(MPSettings));
        GS_SetRefreshRate(60, 60);
        Input_Init();
        std::memset(weapon_data, 0, sizeof(weapon_data));
        for (int i = 0; i < kWeaponCount; ++i) {
            weapon_def(i)->id = int16_t(i);
            weapon_def(i)->klass = int16_t(i);
            weapon_def(i)->speed = 1.0f;
            weapon_def(i)->range = 100.0f;
        }
        std::memcpy(saved, glb_viewer, sizeof(saved));
        std::memset(glb_viewer, 0, sizeof(glb_viewer));
        viewer.width = 512.0f;
        viewer.height = 448.0f;
        glb_viewer[0] = &viewer;
        // O jogador 5 m à frente (+z) da origem, vivo e ativo.
        player.type = 3;
        player.data = &bloke;
        player.sphere_src = reinterpret_cast<uint8_t*>(&anim);
        player.pos = {0.0f, 0.0f, 5.0f, 1.0f};
        player.sphere = {0.0f, 0.0f, 5.0f, 1.0f};
        bloke.health = 100.0f;
        bloke.active = 1;
        bloke.gun = &gun;
        std::memset(glb_players, 0, sizeof(glb_players));
        glb_players[0] = &player;
        glb_blokes[0] = &bloke;
    }
    ~World() {
        glb_players[0] = nullptr;
        glb_blokes[0] = nullptr;
        std::memcpy(glb_viewer, saved, sizeof(saved));
    }
    void put_player(float x, float y, float z) {
        player.pos = {x, y, z, 1.0f};
        player.sphere = {x, y, z, 1.0f};
    }
};

// Uma câmera na origem olhando para +z: cone de "angle" graus, alcance "range", alarme, canal de desligar e grupo.
obj_tag* camera(float angle, float range, uint16_t alarm, uint16_t off, uint8_t group, celglist_tag* glist = nullptr,
                _VECTOR at = {0.0f, 0.0f, 0.0f, 1.0f}) {
    level_tag level{};
    level.params[1] = uint32_t(angle);
    level.params[2] = uint32_t(range);
    level.params[3] = alarm;
    level.params[4] = off;
    level.params[6] = group;
    _VECTOR rot{};
    return Sensor_Create(&at, &rot, &level, glist);
}

void press(uint32_t action) { Input_SetAction(0, action, 4); }
void release(uint32_t action) {
    PlayerSetting[0].state[action] = 0;
    PlayerSetting[0].actions[action] = 0.0f;
}

// Um projétil (tipo 5) com dano "damage" na lista de acertos de "target".
void shoot(obj_tag* target, float damage) {
    obj_tag* b = control_create_object(0x100, nullptr, nullptr, nullptr);
    b->type = 5;
    HITDATA_tag* h = Coll_AddHitToList(&target->hits, 1.0f, nullptr, b);
    std::memcpy(&h->unknown_08, &damage, 4);
}

int count_type(uint8_t type) {
    int n = 0;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) n += o->type == type && (o->life & 1) == 0;
    return n;
}

float item_timer(int i) {
    float t;
    std::memcpy(&t, SSysItems + i * 8, 4);
    return t;
}

}  // namespace

// ---- câmeras de segurança ----

TEST(sensor_create_reads_cone_range_channels_and_joins_the_camera_list) {
    World w;
    celglist_tag broken{};
    celglist_tag model{};
    model.lod = &broken;
    model.flags = 0x80;
    obj_tag* c = camera(30.0f, 10.0f, 0x30, 0x31, 3, &model);
    CHECK(c != nullptr && c->type == 0x37 && c->glist == &model && c->flags == 0x80);
    const SENSOR* s = static_cast<const SENSOR*>(c->data);
    // Metade do cone: 15 graus (PS2Sinf, polinômio da VU0: perto do cosseno e do seno de verdade).
    CHECK_NEAR(s->cos_half, std::cos(15.0f * kPi / 180.0f), 1e-4f);
    CHECK_NEAR(s->sin_half, std::sin(15.0f * kPi / 180.0f), 1e-4f);
    CHECK(s->range == 10.0f && s->alarm == 0x30 && s->off == 0x31 && s->group == 3 && s->broken == &broken);
    CHECK(CameraList.first == &s->node && s->obj == c && c->state == 0);
    CHECK(g_security_stats.sensors == 1);
}

TEST(sensor_spots_player_in_cone_after_half_a_second_and_alarm_after_two) {
    World w;
    obj_tag* c = camera(30.0f, 10.0f, 0x30, 0, 0);
    // 31 ticks vendo: ainda varrendo; o 32º passa a seguir o jogador.
    for (int t = 0; t < 31; ++t) Sensor_Update(c);
    CHECK(c->state == 0 && g_security_stats.spotted == 0);
    Sensor_Update(c);
    CHECK(c->state == 1 && g_security_stats.spotted == 1);
    // O alarme só depois de 2 s seguindo (120 ticks a 60 Hz).
    for (int t = 0; t < 110; ++t) Sensor_Update(c);
    CHECK(c->state == 1 && switch_channels[0x30] == 0);
    for (int t = 0; t < 20; ++t) Sensor_Update(c);
    CHECK(switch_channels[0x30] == 1 && switch_channels_time[0x30] == GameState.frame);
}

TEST(sensor_does_not_see_outside_the_cone_or_beyond_its_range) {
    World w;
    obj_tag* c = camera(30.0f, 10.0f, 0x30, 0, 0);
    // Ao lado (90 graus da frente) e atrás.
    w.put_player(5.0f, 0.0f, 0.5f);
    for (int t = 0; t < 60; ++t) Sensor_Update(c);
    w.put_player(0.0f, 0.0f, -5.0f);
    for (int t = 0; t < 60; ++t) Sensor_Update(c);
    // Na frente, mas além do alcance + o raio dele.
    w.put_player(0.0f, 0.0f, 11.5f);
    for (int t = 0; t < 60; ++t) Sensor_Update(c);
    CHECK(c->state == 0 && g_security_stats.spotted == 0);
    // A câmera de script (4) ativa: a câmera não vê ninguém.
    w.put_player(0.0f, 0.0f, 5.0f);
    alignas(16) viewer_tag script{};
    script.active = 1;
    glb_viewer[4] = &script;
    for (int t = 0; t < 60; ++t) Sensor_Update(c);
    CHECK(c->state == 0);
    glb_viewer[4] = nullptr;
}

TEST(sensor_loses_the_player_after_a_second_and_clears_the_alarm) {
    World w;
    obj_tag* c = camera(30.0f, 10.0f, 0x30, 0, 0);
    for (int t = 0; t < 32 + 130; ++t) Sensor_Update(c);
    CHECK(c->state == 1 && switch_channels[0x30] == 1);
    // Longe: o alarme fica até a câmera desistir (mais de FRAME_RATE_INT ticks sem vê-lo).
    w.put_player(0.0f, 0.0f, 50.0f);
    for (int t = 0; t < 60; ++t) Sensor_Update(c);
    CHECK(c->state == 1);
    Sensor_Update(c);
    CHECK(c->state == 2 && g_security_stats.lost == 1);
    for (int t = 0; t < 120 && c->state == 2; ++t) Sensor_Update(c);
    CHECK(c->state == 0 && switch_channels[0x30] == 0);
}

TEST(sensor_turns_to_follow_the_player) {
    World w;
    obj_tag* c = camera(60.0f, 20.0f, 0, 0, 0);
    for (int t = 0; t < 32; ++t) Sensor_Update(c);
    CHECK(c->state == 1);
    // O jogador anda para o lado: a frente da câmera vai atrás dele (Vec_Track, 5% por tick).
    w.put_player(4.0f, 0.0f, 5.0f);
    for (int t = 0; t < 200; ++t) Sensor_Update(c);
    _VECTOR dir;
    Mat_GetDir(&dir, &c->matrix);
    const float want = std::atan2(4.0f, 5.0f);
    CHECK_NEAR(std::atan2(dir.x, dir.z), want, 0.02f);
    CHECK(c->state == 1);
}

TEST(sensor_off_channel_switches_it_off_and_back_on) {
    World w;
    celglist_tag broken{};
    celglist_tag model{};
    model.lod = &broken;
    obj_tag* c = camera(30.0f, 10.0f, 0x30, 0x31, 0, &model);
    switch_channels[0x31] = 1;
    Sensor_Update(c);
    CHECK(c->state == 3 && c->glist == &broken && g_security_stats.disabled == 1);
    for (int t = 0; t < 60; ++t) Sensor_Update(c);
    CHECK(c->state == 3 && switch_channels[0x30] == 0);
    switch_channels[0x31] = 0;
    Sensor_Update(c);
    CHECK(c->state == 0 && c->glist == &model);
}

// ---- monitores ----

TEST(monitor_shows_its_group_cameras_and_use_steps_through_them) {
    World w;
    obj_tag* a = camera(30.0f, 10.0f, 0, 0, 3, nullptr, {1.0f, 2.0f, 3.0f, 1.0f});
    obj_tag* other = camera(30.0f, 10.0f, 0, 0, 4, nullptr, {4.0f, 2.0f, 3.0f, 1.0f});
    obj_tag* b = camera(30.0f, 10.0f, 0, 0, 3, nullptr, {7.0f, 2.0f, 3.0f, 1.0f});
    static_cast<SENSOR*>(b->data)->view_channel = 0x40;
    level_tag level{};
    level.params[0] = 3;
    level.params[1] = 1;  // indestrutível
    _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f}, rot{};
    obj_tag* m = Monitor_Create(&pos, &rot, &level, nullptr, nullptr);
    CHECK(m != nullptr && m->type == 0x38 && m->unknown_f6 == 3);
    // A primeira câmera do grupo; o jogador parado, sem atualização de câmera.
    w.viewer.updater = Player_PositionCamera;
    w.viewer.follow = &w.player;
    Monitor_Activate(m, &w.player);
    auto* md = static_cast<MONITOR_tag*>(m->data);
    CHECK(m->state == 2 && md->camera == a && md->player == &w.player && w.bloke.active == 0);
    CHECK(w.viewer.updater == nullptr && a->unknown_f8 == 0);
    release(0xe);
    Monitor_Update(m);
    CHECK(std::memcmp(&w.viewer.camera, &a->matrix, sizeof(MATRIX)) == 0);
    CHECK(static_cast<SENSOR*>(a->data)->viewed == 1);
    // Usar: a próxima do grupo (pula a do grupo 4) e o canal de "sendo vista" dela.
    press(0xe);
    Monitor_Update(m);
    CHECK(md->camera == b && a->unknown_f8 == 1 && switch_channels[0x40] == 1);
    CHECK(std::memcmp(&w.viewer.camera, &b->matrix, sizeof(MATRIX)) == 0);
    // Depois da última: o jogador volta e o monitor espera 6 ticks para poder ser usado de novo.
    Monitor_Update(m);
    CHECK(md->camera == nullptr && m->state == 9 && w.bloke.active == 2);
    CHECK(w.viewer.updater == Player_PositionCamera);
    release(0xe);
    for (int t = 0; t < 5; ++t) Monitor_Update(m);
    CHECK(m->state == 4);
    Monitor_Update(m);
    CHECK(m->state == 0);
    (void)other;
}

TEST(monitor_skips_disabled_cameras_and_breaks_with_one_shot) {
    World w;
    obj_tag* a = camera(30.0f, 10.0f, 0, 0x31, 2);
    obj_tag* b = camera(30.0f, 10.0f, 0, 0, 2);
    level_tag level{};
    level.params[0] = 2;
    _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f}, rot{};
    obj_tag* m = Monitor_Create(&pos, &rot, &level, nullptr, nullptr);
    auto* md = static_cast<MONITOR_tag*>(m->data);
    CHECK(md->health == 1.0f);
    Monitor_Activate(m, &w.player);
    CHECK(md->camera == a);
    // A câmera mostrada desliga: o monitor passa à próxima do grupo.
    switch_channels[0x31] = 1;
    Sensor_Update(a);
    release(0xe);
    Monitor_Update(m);
    CHECK(md->camera == b);
    // Um tiro: o monitor quebra, explode e devolve o jogador.
    shoot(m, 2.0f);
    const uint32_t explosions = g_explode_stats.created;
    Monitor_Update(m);
    CHECK(m->state == 3 && g_security_stats.broken == 1 && g_explode_stats.created == explosions + 1);
    CHECK(w.bloke.active == 2 && w.viewer.updater == Player_PositionCamera);
    // Quebrado não volta a ser usado.
    Monitor_Activate(m, &w.player);
    CHECK(m->state == 3);
}

// ---- as contagens do SSys ----

TEST(ssys_countdown_speaks_at_its_minutes_and_turns_the_channel_off) {
    World w;
    for (int i = 0; i < 4; ++i) SSysMinutes[i] = float(i == 0 ? 180 : i == 1 ? 120 : i == 2 ? 60 : 6);
    SSys_Init();
    SSysItems[3 * 8 + 4] = 0x50;
    SSys_Monitor();
    CHECK(item_timer(3) == -1.0f && g_security_stats.messages == 0);
    switch_channels[0x50] = 1;
    SSys_Monitor();
    CHECK(item_timer(3) == 240.0f && SSysItems[3 * 8 + 5] == 6 && g_security_stats.messages == 1);
    // As falas dos minutos (5, 4, 3, 2) e o fim (1): 240 s a 60 Hz.
    int ticks = 0;
    while (switch_channels[0x50] != 0 && ticks < 20000) {
        SSys_Monitor();
        ++ticks;
    }
    CHECK(switch_channels[0x50] == 0 && g_security_stats.messages == 6);
    CHECK(ticks > 239 * 60 && ticks < 241 * 60);
    CHECK(SSysItems[3 * 8 + 5] == 0 && item_timer(3) == -1.0f);
}

TEST(ssys_countdown_stops_when_the_channel_goes_off_and_waits_for_scenes) {
    World w;
    SSys_Init();
    SSysItems[4 * 8 + 4] = 0x51;
    switch_channels[0x51] = 1;
    SSys_Monitor();
    CHECK(SSysItems[4 * 8 + 5] == 6);
    // Numa cena (ScriptCam) nada anda.
    ScriptCam = 1;
    SSys_Monitor();
    CHECK(item_timer(4) == 240.0f);
    ScriptCam = 0;
    switch_channels[0x51] = 0;
    SSys_Monitor();
    CHECK(SSysItems[4 * 8 + 5] == 0 && item_timer(4) == -1.0f && g_security_stats.messages == 2);
}

TEST(ssys_laser_two_first_ends_laser_one_in_0700000b) {
    World w;
    SSys_Init();
    GameState.level_file = 0x700000b;
    switch_channels[0x76] = 1;
    SSysItems[1 * 8 + 4] = 0x52;
    SSysItems[2 * 8 + 4] = 0x53;
    switch_channels[0x52] = 1;
    SSys_Monitor();
    CHECK(SSysItems[1 * 8 + 5] == 6);
    switch_channels[0x53] = 1;
    SSys_Monitor();
    // O laser 1 sai da conta (sem canal) e o laser 2 espera 8 s.
    CHECK(SSysItems[1 * 8 + 4] == 0xff && SSysItems[1 * 8 + 5] == 0 && SSysDelayTimer == 8.0f);
    CHECK(item_timer(2) == -1.0f);
    for (int t = 0; t < 8 * 60 + 2; ++t) SSys_Monitor();
    CHECK(SSysItems[2 * 8 + 5] == 6 && item_timer(2) > 230.0f);
}

TEST(ssys_ramload_brings_items_back_but_keeps_laser_two_in_0700000a) {
    World w;
    uint8_t copy[40];
    std::memset(copy, 0x11, sizeof(copy));
    SSys_Init();
    GameState.level_file = 0x700000a;
    SSys_RamLoad(copy);
    CHECK(SSysItems[1 * 8 + 4] == 0x11 && SSysItems[2 * 8 + 4] == 0xff && SSysItems[4 * 8 + 4] == 0x11);
    // A carga quente não zera.
    IsWarmReset = 1;
    SSys_Init();
    CHECK(SSysItems[1 * 8 + 4] == 0x11);
    IsWarmReset = 0;
}

// ---- holofotes ----

namespace {

struct Searchlight {
    obj_tag* lamp = nullptr;
    obj_tag* base = nullptr;
    obj_tag* spot = nullptr;
    obj_tag* beam = nullptr;
    // Lâmpada no alto (y 10) apontando para +z, arco de 20 a 60 graus (centro 40, metade 20), numa sala vazia (as
    // peças se juntam pela sala). Como no disco, a lâmpada vem primeiro e as peças se juntam a ela.
    Searchlight(uint8_t start, uint8_t off, uint8_t alarm) {
        cel_tag* room = build_alloc_cel(glb_world->first);
        glb_world->first = room;
        room->type = 0x4c023 | 0x8000000;
        room->box_min = {-50, -50, -50, 0};
        room->box_max = {50, 50, 50, 0};
        level_tag level{};
        level.params[1] = start;
        level.params[2] = off;
        level.params[3] = alarm;
        level.params[4] = 60;
        level.params[5] = 20;
        _VECTOR pos{0.0f, 10.1f, 0.0f, 1.0f}, rot{0.0f, 0.0f, 0.0f, 0.0f};
        const auto part = [&](uint32_t kind, float y) {
            level.params[0] = kind;
            pos.y = y;
            return Searchlight_Create(&pos, &rot, &level, nullptr);
        };
        lamp = part(1, 10.1f);
        base = part(0, 10.0f);
        spot = part(2, 10.2f);
        beam = part(3, 10.15f);
    }
    SEARCHLIGHT_tag* d() const { return static_cast<SEARCHLIGHT_tag*>(lamp->data); }
};

}  // namespace

TEST(searchlight_create_reads_the_arc_and_gives_the_spot_a_flashlight) {
    World w;
    Searchlight s(0, 0, 0);
    const SEARCHLIGHT_tag* d = s.d();
    CHECK(d->kind == 1 && s.lamp->type == 0x22 && s.lamp->state == 0);
    CHECK_NEAR(d->pitch, 40.0f * kPi / 180.0f, 1e-5f);
    CHECK_NEAR(d->half, 20.0f * kPi / 180.0f, 1e-5f);
    CHECK(0.0f <= d->timer && d->timer < 480.0f);
    // A mancha: a luz do tipo 2 (lanterna), branca, raio 500, presa a ela; o objeto 10 km longe.
    light_tag* l = Searchlight_Light(s.spot);
    CHECK(l != nullptr && l->kind == 2 && l->obj == s.spot && l->radius == 500.0f && l->on == 1);
    CHECK(l->r == 0xff && l->g == 0xff && l->b == 0xff && l->pos.y == 10.2f);
    CHECK(s.spot->pos.y < -9000.0f && g_searchlight_stats.lights == 1);
    CHECK(d->parts[2] == s.spot && d->parts[3] == s.beam && d->parts[0] == s.base);
}

TEST(searchlight_sweeps_inside_its_arc) {
    World w;
    Searchlight s(0, 0, 0);
    const SEARCHLIGHT_tag* d = s.d();
    float lo = 10.0f, hi = -10.0f;
    for (int t = 0; t < 600; ++t) {
        Searchlight_Update(s.lamp);
        const float dy = Vec_AngleDifference(d->rot.y, s.lamp->rot.y);
        const float dx = s.lamp->rot.x - d->pitch;
        CHECK(std::fabs(dy) <= d->half + 1e-4f && std::fabs(dx) <= d->half + 1e-4f);
        lo = std::fmin(lo, dy);
        hi = std::fmax(hi, dy);
    }
    // Varre o arco todo (480 ticks por volta) e o suporte gira junto.
    CHECK(hi - lo > 1.8f * d->half && s.base->rot.y == s.lamp->rot.y);
    CHECK(s.lamp->state == 0);
}

TEST(searchlight_tracking_raises_alarm_and_stays_in_its_arc) {
    World w;
    Searchlight s(0, 0, 0x37);
    // O jogador bem à direita (fora do arco): a lâmpada vira até a borda e fica.
    w.put_player(30.0f, 0.0f, 1.0f);
    s.lamp->state = 1;
    for (int t = 0; t < 120; ++t) Searchlight_Update(s.lamp);
    const SEARCHLIGHT_tag* d = s.d();
    CHECK(switch_channels[0x37] == 1 && s.lamp->state == 1);
    CHECK_NEAR(std::fabs(Vec_AngleDifference(s.lamp->rot.y, d->rot.y)), d->half, 1e-3f);
    // A mancha longe dele (o raio não bate em nada) por 15 s: volta a varrer; o alarme fica.
    for (int t = 0; t < 15 * 60 + 2; ++t) Searchlight_Update(s.lamp);
    CHECK(s.lamp->state == 0 && switch_channels[0x37] == 1 && g_searchlight_stats.lost == 1);
}

TEST(searchlight_waits_for_its_channel_and_breaks_with_a_shot) {
    World w;
    Searchlight s(0x20, 0, 0);
    CHECK(s.lamp->state == 3);
    for (int t = 0; t < 10; ++t) Searchlight_Update(s.lamp);
    CHECK(s.lamp->state == 3);
    switch_channels[0x20] = 1;
    Searchlight_Update(s.lamp);
    CHECK(s.lamp->state == 0 && g_searchlight_stats.switched == 1);
    // Um tiro: a luz volta à lista de livres, as peças pedem a remoção e a lâmpada cai até 0,8 rad.
    light_tag* l = Searchlight_Light(s.spot);
    shoot(s.lamp, 5.0f);
    Searchlight_Update(s.lamp);
    CHECK(s.lamp->state == 2 && (s.spot->life & 1) != 0 && (s.beam->life & 1) != 0);
    bool used = false;
    for (LLNODE_tag* n = LightList.used.first; n != nullptr; n = n->next) used = used || n == &l->node;
    CHECK(!used && s.d()->parts[2] == nullptr && g_searchlight_stats.shot == 1);
    s.lamp->hits = nullptr;
    for (int t = 0; t < 40; ++t) Searchlight_Update(s.lamp);
    CHECK(s.lamp->rot.x >= 0.8f && s.lamp->rot.x < 0.86f);
}

TEST(searchlight_off_channel_puts_it_out_for_good) {
    World w;
    Searchlight s(0, 0x21, 0x37);
    switch_channels[0x21] = 1;
    Searchlight_Update(s.lamp);
    CHECK(s.lamp->state == 3 && s.d()->start == 0 && s.d()->alarm == 0 && (s.spot->life & 1) != 0);
    switch_channels[0x21] = 0;
    for (int t = 0; t < 60; ++t) Searchlight_Update(s.lamp);
    CHECK(s.lamp->state == 3 && g_searchlight_stats.off == 1);
}

// ---- minas, atiradores, inversores e o tremor ----

TEST(mine_bobs_spins_and_explodes_when_touched) {
    World w;
    level_tag level{};
    level.params[0] = 80;    // dano
    level.params[1] = 10;    // raio
    level.params[2] = 500;   // giro: 0,1 rad por tick
    level.params[3] = 1000;  // balanço: 0,2 rad por quadro
    level.params[4] = 5;     // amplitude 0,5
    _VECTOR pos{1.0f, 2.0f, 3.0f, 1.0f}, rot{};
    obj_tag* m = Mine_Create(&pos, &rot, &level, nullptr);
    CHECK(m != nullptr && m->type == 0x4b);
    const auto* md = static_cast<const MINE_tag*>(m->data);
    CHECK(md->damage == 80.0f && md->radius == 10.0f && md->home.y == 2.0f);
    for (uint32_t f = 1; f <= 30; ++f) {
        GameState.frame = f;
        Mine_Update(m);
        CHECK_NEAR(m->pos.y, 2.0f + PS2Sinf(float(f) * 0.2f) * 0.5f, 1e-5f);
    }
    CHECK_NEAR(m->rot.y, 3.0f, 1e-4f);
    CHECK(g_explode_stats.created == 0 && (m->life & 1) == 0);
    // Um acerto de qualquer objeto: a explosão no lugar dela, com a mina como causadora.
    obj_tag* toucher = control_create_object(0, nullptr, nullptr, nullptr);
    Coll_AddHitToList(&m->hits, 1.0f, nullptr, toucher);
    Mine_Update(m);
    CHECK(g_explode_stats.created == 1 && (m->life & 1) != 0 && g_hazard_stats.mines_blown == 1);
    const obj_tag* e = nullptr;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type == 0xf) e = o;
    }
    CHECK(e != nullptr);
    if (e != nullptr) {
        const auto* x = static_cast<const EXPLODE_tag*>(e->data);
        CHECK(x->damage == 80.0f && x->radius == 10.0f && x->from == m && e->pos.x == m->pos.x);
    }
}

TEST(mine_ignores_hits_without_an_object) {
    World w;
    level_tag level{};
    level.params[0] = 80;
    level.params[1] = 10;
    _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f}, rot{};
    obj_tag* m = Mine_Create(&pos, &rot, &level, nullptr);
    Coll_AddHitToList(&m->hits, 1.0f, nullptr, nullptr);
    Mine_Update(m);
    CHECK(g_explode_stats.created == 0 && (m->life & 1) == 0);
}

TEST(shooter_fires_once_when_its_channel_turns_on) {
    World w;
    level_tag level{};
    level.params[0] = 0x44;
    level.params[1] = 0x36;
    _VECTOR pos{0.0f, 5.0f, 0.0f, 1.0f}, rot{};
    obj_tag* s = Shooter_Create(&pos, &rot, &level, nullptr);
    CHECK(s != nullptr && s->type == 0x4e && s->state == 0x44 && s->unknown_f6 == 0x36);
    for (int t = 0; t < 5; ++t) Shooter_Update(s);
    CHECK(count_type(5) == 0 && (s->life & 1) == 0);
    switch_channels[0x44] = 1;
    Shooter_Update(s);
    CHECK(count_type(5) == 1 && (s->life & 1) != 0 && g_hazard_stats.shots == 1);
}

TEST(inverter_turns_its_output_off_while_the_input_is_on) {
    World w;
    level_tag level{};
    level.params[0] = 0xb1;
    level.params[1] = 0x37;
    obj_tag* i = InverterTrigger_Create(&level);
    CHECK(i != nullptr && i->type == 0x53);
    switch_channels[0x37] = 1;
    InverterTrigger_Update(i);
    CHECK(switch_channels[0x37] == 1);
    switch_channels[0xb1] = 1;
    InverterTrigger_Update(i);
    CHECK(switch_channels[0x37] == 0);
}

TEST(apocalypse_shakes_and_flashes_for_its_seconds_once_triggered) {
    World w;
    level_tag level{};
    level.params[0] = 0x1e;
    level.params[1] = 2;  // segundos
    level.params[2] = 0;
    level.params[3] = 1;
    level.params[4] = 1;
    _VECTOR pos{}, rot{};
    obj_tag* a = Apocalypse_Create(&pos, &rot, &level, nullptr);
    CHECK(a != nullptr && a->type == 0x54 && a->cel == nullptr);
    for (int t = 0; t < 10; ++t) Apocalypse_Update(a);
    CHECK(w.viewer.shake == 0.0f && w.bloke.fade_time == 0.0f);
    switch_channels[0x1e] = 1;
    Apocalypse_Update(a);
    // Um tick dos 120: sacudir 5 x 119/120 e o clarão 382 x 119/120 (no máximo 255).
    CHECK_NEAR(w.viewer.shake, 5.0f * 119.0f / 120.0f, 1e-4f);
    CHECK(w.bloke.blur == 255 && w.bloke.fade_time == 119.0f && w.bloke.fade_left == 119.0f);
    // O canal pode desligar: o tremor continua até o fim.
    switch_channels[0x1e] = 0;
    for (int t = 0; t < 100; ++t) Apocalypse_Update(a);
    CHECK(w.bloke.blur == uint8_t(int32_t(19.0f * 382.0f / 120.0f)) && (a->life & 1) == 0);
    for (int t = 0; t < 19; ++t) Apocalypse_Update(a);
    CHECK((a->life & 1) != 0);
}

// ---- o canhão montado ----

TEST(gunimp_aims_with_the_sticks_and_overheats_firing_the_beam) {
    World w;
    for (TURRETGUN& g : TurretGuns) g = {};
    TurretGuns[11] = {{0.0f, 0.0f, 1.71f}, 0, 0x67, 1.0f, 0x51b};
    celglist_tag base{};
    base.box_min = {0.0f, 0.0f, -2.0f, 0.0f};
    _VECTOR pos{0.0f, 1.0f, 0.0f, 1.0f};
    quaternion_tag q{0.0f, 0.0f, 0.0f, 1.0f};
    obj_tag* g = GunImp_Create(&pos, &q, &base, nullptr);
    CHECK(g != nullptr && g->type == 0x47 && (g->flags & 0x1000) != 0);
    GUNIMP_tag* d = static_cast<GUNIMP_tag*>(g->data);
    CHECK(d->kind == 11 && d->gun != nullptr && d->seat.z == -1.5f);
    // Montado à mão (GunImp_Activate precisa do jogador inteiro): o jogador fica atrás da base.
    g->state = 1;
    g->unknown_f6 = 0;
    d->player = &w.player;
    PlayerSetting[0].actions[3] = -1.0f;  // gira para a esquerda
    PlayerSetting[0].state[3] = 1;
    PlayerSetting[0].actions[4] = -1.0f;  // olha para cima
    PlayerSetting[0].state[4] = 1;
    for (int t = 0; t < 200; ++t) GunImp_Update(g);
    // A inclinação para em 45 graus; o giro segue o pedido a 15% por tick.
    CHECK_NEAR(d->pitch_target, 0.7853982f, 1e-6f);
    CHECK_NEAR(d->pitch, 0.7853982f, 1e-4f);
    CHECK_NEAR(d->yaw_target, 200 * 0.015f, 1e-3f);
    CHECK(d->yaw_target - d->yaw > 0.0f && d->yaw_target - d->yaw < 0.1f);
    CHECK_NEAR(w.player.pos.y, 1.0f, 1e-4f);
    CHECK_NEAR(std::sqrt(w.player.pos.x * w.player.pos.x + w.player.pos.z * w.player.pos.z), 1.5f, 1e-4f);
    CHECK_NEAR(w.bloke.pitch, -d->pitch * 0.5f, 1e-6f);
    // Atirando: um tiro por tick (atraso 1), +2 de calor por tiro e -1 por tick; acima de 200 superaquece.
    PlayerSetting[0].actions[9] = 1.0f;
    PlayerSetting[0].state[9] = 1;
    int t = 0;
    while (d->overheated == 0 && t < 400) {
        GunImp_Update(g);
        ++t;
    }
    CHECK(d->overheated == 1 && t > 180 && t < 220 && g_gunimp_stats.overheats == 1);
    const uint32_t shots = g_gunimp_stats.shots;
    for (int k = 0; k < 50; ++k) GunImp_Update(g);
    CHECK(g_gunimp_stats.shots == shots && d->loop == 0);
    // Esfria (1 por tick até 0): no tick em que o calor zera, o superaquecimento acaba e o tiro já sai.
    int cool = 0;
    while (d->overheated != 0 && cool < 400) {
        GunImp_Update(g);
        ++cool;
    }
    CHECK(d->overheated == 0 && cool > 150 && g_gunimp_stats.shots == shots + 1);
}

TEST(gunimp_refuses_a_crouching_player_and_lets_go_when_he_dies) {
    World w;
    celglist_tag base{};
    _VECTOR pos{0.0f, 1.0f, 0.0f, 1.0f};
    quaternion_tag q{0.0f, 0.0f, 0.0f, 1.0f};
    obj_tag* g = GunImp_Create(&pos, &q, &base, nullptr);
    w.player.unknown_f6 = 4;
    GunImp_Activate(g, &w.player);
    CHECK(g->state == 0 && w.bloke.ride == nullptr);
    // Montado num subestado sem animação de troca (5), a morte do jogador desmonta e o devolve.
    w.player.unknown_f6 = 5;
    GunImp_Activate(g, &w.player);
    CHECK(g->state == 1 && w.bloke.ride == g && w.player.unknown_f6 == 0xc && w.bloke.frozen == 1);
    CHECK(w.bloke.cam_mode == 0xe && (w.player.mode & 0x44) == 0x44);
    GunImp_PlayerHasDied(&w.player);
    CHECK(g->state == 0 && w.bloke.ride == nullptr && w.player.unknown_f6 == 5 && (w.player.mode & 0x44) == 0);
}

// ---- a estação espacial ----

TEST(space_missile_arms_after_its_delay_and_reports_the_outcome) {
    World w;
    level_tag level{};
    level.params[3] = 2;  // 2 s
    level.params[4] = 3;  // míssil 3
    level.params[5] = 0x60;
    level.params[6] = 0x61;
    level.params[7] = 0x13;
    _VECTOR pos{}, rot{};
    obj_tag* m = Create_SpaceMissile(&pos, &rot, &level);
    CHECK(m != nullptr && m->type == 0x4f && MissileDeploy[2] == 0);
    auto* d = static_cast<SPACEMISSILE_tag*>(m->data);
    // Sem o script do silo (hash não registrado) nada anda; com um script na mão, a contagem só com o canal 0x13.
    alignas(16) static SCRIPTINFO silo{};
    std::memset(&silo, 0, sizeof(silo));
    silo.play = 1;
    d->script = &silo;
    for (int t = 0; t < 200; ++t) SpaceMissile_Update(m);
    CHECK(m->state == 0);
    switch_channels[0x13] = 1;
    for (int t = 0; t < 2 * 60 + 2; ++t) SpaceMissile_Update(m);
    CHECK(m->state == 1 && MissileDeploy[2] == 1 && g_space_stats.armed == 1);
    // O desfecho (estado 3) terminado: o lançamento liga o canal do parâmetro 5.
    m->state = 3;
    m->unknown_f6 = 1;
    silo.play = 2;
    SpaceMissile_Update(m);
    CHECK(m->state == 4 && switch_channels[0x60] == 1 && switch_channels[0x61] == 0 && d->script == nullptr);
}

TEST(space_laser_counts_down_and_fires_at_the_player) {
    World w;
    for (int i = 0; i < kWeaponCount; ++i) weapon_def(i)->klass = 0x10;
    level_tag level{};
    level.params[3] = 30;  // segundos até o fim
    level.params[4] = 1;
    level.params[5] = 0x63;
    level.params[6] = 0x0e;
    level.params[7] = 0x13;
    _VECTOR pos{}, rot{};
    obj_tag* l = Create_SpaceLaser(&pos, &rot, &level);
    CHECK(l != nullptr && l->type == 0x50);
    auto* d = static_cast<SPACELASER_tag*>(l->data);
    CHECK(d->wait == 10.0f && d->countdown == 30.0f);
    for (int t = 0; t < 100; ++t) SpaceLaser_Update(l);
    CHECK(l->state == 0 && d->countdown == 30.0f);
    switch_channels[0x13] = 1;
    // 10 s: uma carga (o script não existe: termina já) mirando onde o jogador estava, e o tiro.
    int t = 0;
    while (g_space_stats.shots == 0 && t < 11 * 60) {
        SpaceLaser_Update(l);
        ++t;
    }
    CHECK(g_space_stats.charges == 1 && g_space_stats.shots == 1 && t > 9 * 60);
    CHECK(d->target.z == 5.0f && count_type(5) == 1 && count_type(0x10) == 1 && l->state == 0);
    // O fim: o script em laço e, 1 s depois, o canal 99.
    d->countdown = -1.0f;
    for (int k = 0; k < 2 * 60; ++k) SpaceLaser_Update(l);
    CHECK(l->state == 3 && switch_channels[0x63] == 1);
    // O canal do parâmetro 6 apaga o laser.
    switch_channels[0x0e] = 1;
    SpaceLaser_Update(l);
    CHECK(l->state == 4 && (l->life & 1) != 0);
}
