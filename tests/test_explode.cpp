// Testes de explode.cpp: o objeto da explosão, o dano que se espalha com a distância e o que ele faz
// em quebráveis, destrutíveis e personagens; control_ClosestPlayerPnt.

#include <cmath>
#include <cstring>

#include "engine/break.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/explode.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "test_framework.h"

namespace {

void fresh_world() {
    psiMemHeapSize = 2u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    view_debug_power_on();
    effect_debug_power_on();
    break_debug_power_on();
    explode_debug_power_on();
    psi_draw_debug_power_on();
    hashtable_debug_power_on();
    hashtable_initialise();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(MPSettings, 0, sizeof(MPSettings));
    std::memset(switch_channels, 0, sizeof(switch_channels));
    GS_SetRefreshRate(60, 60);
    for (obj_tag*& p : glb_players) p = nullptr;
    std::memset(EffectInfo, 0, sizeof(EffectInfo));
    InitShards();
}

// Sala sem geometria: as explosões só pegam os objetos dela.
cel_tag* empty_room() {
    cel_tag* cel = build_alloc_cel(glb_world->first);
    glb_world->first = cel;
    cel->type = 0xc023 | 0x8000000;
    return cel;
}

// Um objeto do tipo 0x28 (o dano da explosão soma em data + 0xf8) com esfera de raio 0,5 em x.
obj_tag* gauge(cel_tag* room, float x) {
    _VECTOR pos{x, 0, 0, 0};
    obj_tag* o = control_create_object(0x100, &pos, nullptr, nullptr);
    o->type = 0x28;
    o->sphere = {x, 0, 0, 0.5f};
    control_link_object_to_cel(o, room);
    return o;
}

float gauge_damage(const obj_tag* o) {
    float v;
    std::memcpy(&v, static_cast<const uint8_t*>(o->data) + 0xf8, 4);
    return v;
}

}  // namespace

TEST(explosion_damage_falls_with_distance) {
    fresh_world();
    cel_tag* room = empty_room();
    obj_tag* inside = gauge(room, 0.3f);
    obj_tag* near_ = gauge(room, 2.0f);
    obj_tag* mid = gauge(room, 4.0f);
    obj_tag* outside = gauge(room, 6.0f);
    obj_tag* gone = gauge(room, 2.0f);
    gone->life |= 1;
    _VECTOR centre{0, 0, 0, 0};
    // dano x (1 - max(0, distância até a esfera) / raio): 100, 70, 30; fora do raio, nada.
    Explode_Propagate(&centre, 5.0f, room, 100.0f, nullptr);
    CHECK_NEAR(gauge_damage(inside), 100.0, 1e-3);
    CHECK_NEAR(gauge_damage(near_), 70.0, 1e-3);
    CHECK_NEAR(gauge_damage(mid), 30.0, 1e-3);
    CHECK(gauge_damage(outside) == 0.0f && gauge_damage(gone) == 0.0f);
    CHECK(g_explode_stats.propagated == 1 && g_explode_stats.hit_objects == 3);
    // Sem sala ou com raio desprezível, nada.
    Explode_Propagate(&centre, 5.0f, nullptr, 100.0f, nullptr);
    Explode_Propagate(&centre, 0.0001f, room, 100.0f, nullptr);
    CHECK_NEAR(gauge_damage(inside), 100.0, 1e-3);
}

TEST(explosion_object_spreads_damage_once) {
    fresh_world();
    cel_tag* room = empty_room();
    obj_tag* target = gauge(room, 1.5f);
    _VECTOR pos{0, 0, 0, 0};
    obj_tag* grenade = control_create_object(0x40, &pos, nullptr, nullptr);
    grenade->cel = room;
    obj_tag* thrower = control_create_object(0x40, &pos, nullptr, nullptr);
    obj_tag* e = Explode_Create(grenade, &pos, &CONSTANT_UP1, 3.0f, 3.0f, 0x6000052, 40.0f, 0xff, 0xff, 100, thrower, 6);
    CHECK(e != nullptr && e->type == 0xf && e->flags == 0x30 && g_explode_stats.created == 1);
    if (e == nullptr) return;
    const EXPLODE_tag* x = static_cast<const EXPLODE_tag*>(e->data);
    CHECK(x->cel == room && x->from == thrower && x->damage == 40.0f && x->radius == 3.0f);
    CHECK(x->duration == 15 && x->script == nullptr && x->surface == 6 && x->propagated == 0);
    CHECK_NEAR(e->scale, 3.0 / 15.0, 1e-6);
    Explode_CollisionHandler(e);
    Explode_CollisionHandler(e);
    CHECK_NEAR(gauge_damage(target), 40.0 * (1.0 - 1.0 / 3.0), 1e-3);
    // Sem o script do efeito carregado (nulo) a explosão acaba no primeiro tick.
    Explode_Update(e);
    CHECK((e->life & 1) != 0);
    // Raio do dano abaixo de 0,5: não fere.
    obj_tag* weak = Explode_Create(grenade, &pos, &CONSTANT_UP1, 0.4f, 0.4f, 0x6000052, 40.0f, 0, 0, 0, nullptr, 0);
    CHECK(static_cast<const EXPLODE_tag*>(weak->data)->damage == 0.0f);
}

TEST(explosion_breaks_glass_and_smashes_destroyables) {
    fresh_world();
    cel_tag* room = empty_room();
    _VECTOR pos{1, 0, 0, 0}, rot{0, 0, 0, 0};
    // Quebrável sem colisão no modelo (Break_Kill só o marca) com resistência 10.
    celglist_tag plain{};
    level_tag lb{};
    lb.params[0] = 1;
    lb.params[1] = 10;
    obj_tag* br = Break_Create(&pos, &rot, &plain, &lb);
    br->sphere = {1, 0, 0, 0.5f};
    control_link_object_to_cel(br, room);
    // Dois destrutíveis: um comum e um que ignora explosões (máscara 0x20).
    level_tag ld{};
    ld.params[3] = 1000;
    obj_tag* soft = Destroy_Create(&pos, &rot, &ld, &plain, nullptr);
    soft->sphere = {1, 0, 0, 0.5f};
    control_link_object_to_cel(soft, room);
    ld.params[4] = 0xffdf;  // ~0x20
    obj_tag* hard = Destroy_Create(&pos, &rot, &ld, &plain, nullptr);
    hard->sphere = {1, 0, 0, 0.5f};
    control_link_object_to_cel(hard, room);
    _VECTOR centre{0, 0, 0, 0};
    Explode_Propagate(&centre, 4.0f, room, 20.0f, nullptr);
    CHECK((br->life & 1) != 0);  // 20 x (1 - 0.5 / 4) = 17.5 > 10
    CHECK(static_cast<DESTROY_tag*>(soft->data)->smashed_flag == 1);
    CHECK(static_cast<DESTROY_tag*>(hard->data)->smashed_flag == 0);
}

TEST(explosive_hit_marks_the_attacker) {
    fresh_world();
    alignas(16) static Drone_tag fake;
    std::memset(&fake, 0, sizeof(fake));
    _VECTOR pos{0, 0, 0, 0};
    obj_tag* o = control_create_object(0x40, &pos, nullptr, nullptr);
    o->type = 0x28;  // não é personagem: só o atacante muda
    o->data = &fake;
    obj_tag* thrower = control_create_object(0x40, &pos, nullptr, nullptr);
    DroneImpact_tag impact{};
    impact.damage = 10.0f;
    impact.source = thrower;
    Drone_ExplosiveHit(o, &impact);
    CHECK(fake.attacker == thrower && g_explode_guards.not_a_drone == 1);
    // A classe 0x10 ignora a própria explosão (antes de qualquer outra coisa).
    fake.klass = 0x10;
    impact.source = o;
    Drone_ExplosiveHit(o, &impact);
    CHECK(fake.attacker == o && g_explode_guards.not_a_drone == 1);
    static_assert(offsetof(DroneImpact_tag, radius) == 4 && offsetof(DroneImpact_tag, pos) == 0x10);
}

TEST(closest_player_point) {
    fresh_world();
    _VECTOR a{10, 0, 0, 0}, b{0, 3, 0, 0};
    obj_tag* p0 = control_create_object(0x40, &a, nullptr, nullptr);
    obj_tag* p2 = control_create_object(0x40, &b, nullptr, nullptr);
    p0->pos = a;
    p2->pos = b;
    _VECTOR out{7, 7, 7, 7};
    _VECTOR at{0, 0, 0, 0};
    CHECK(control_ClosestPlayerPnt(&at, &out) == 1e6f && out.x == 7.0f);  // sem jogadores
    glb_players[0] = p0;
    glb_players[2] = p2;
    CHECK(control_ClosestPlayerPnt(&at, &out) == 9.0f && out.y == 3.0f && out.x == 0.0f);
    glb_players[0] = glb_players[2] = nullptr;
}
