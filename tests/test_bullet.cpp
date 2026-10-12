// Testes dos projéteis (src/engine/bullet.cpp): voo, alcance, impacto no cenário e em objetos,
// quique, grudar, gravidade, dispersão, teleguiado, pavio, satchel e cápsulas, com o tratador de
// objetos do jogo (control_movement_object_handler) numa sala sintética.

#include <cmath>
#include <cstring>

#include "engine/sound.h"
#include "engine/anim.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

// Materiais sintéticos (EffectInfo vem do ACTION.ELF no jogo): o 3 deixa metade da velocidade num
// quique e uma marca de bala (modelo kMark); o 5 ricocheteia com a máscara 0x3f.
constexpr uint32_t kMark = 0x2000777;
celglist_tag g_mark{};
void materials() {
    std::memset(EffectInfo, 0, sizeof(EffectInfo));
    EffectInfo[3].bounce_keep = 0.5f;
    EffectInfo[3].decal = kMark;
    EffectInfo[5].bounce_keep = 0.5f;
    EffectInfo[5].ricochet = 0x3f;
}

void fresh(uint32_t logic_rate = 60) {
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
    g_sound_stats = SoundStats{};
    Door_Init();
    GS_SetRefreshRate(logic_rate, 60);
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(MPSettings, 0, sizeof(MPSettings));
    WldGravity = {0.0f, -9.8f, 0.0f, 0.0f};
    materials();
    effect_debug_power_on();
    Effect_InitLists();
    hashtable_additem(kMark, &g_mark);
}

void tick() {
    ++GameState.frame;
    control_movement_object_handler(0);
}

bool alive(const obj_tag* obj) {
    for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o == obj) return true;
    }
    return false;
}

int pending(const char* name) {
    const auto& m = pending_counts();
    const auto it = m.find(name);
    return it == m.end() ? 0 : it->second;
}

// Colisão de um quadrado de 20 x 20 no plano y = 0 do espaço local (normal +y), uma folha com dois
// triângulos e vértices quantizados com escala 0,01, como os blocos 0x2e/0x2f.
struct Square {
    alignas(16) uint8_t head[0x40]{};
    COLLBOX_tag box{};
    uint16_t tris[2][4]{};
    uint8_t surface[2];
    colldata_tag coll{};
    celglist_tag glist{};

    explicit Square(uint8_t material) : surface{material, material} {
        const int16_t v[4][3] = {{-1000, 0, -1000}, {-1000, 0, 1000}, {1000, 0, 1000}, {1000, 0, -1000}};
        std::memcpy(head, v, sizeof(v));
        const int16_t n[3] = {0, 16383, 0};
        std::memcpy(head + 24, n, sizeof(n));
        const float d = 0.0f;
        std::memcpy(head + 30, &d, 4);
        const uint16_t t[2][4] = {{0, 3, 6, 12}, {0, 6, 9, 12}};
        std::memcpy(tris, t, sizeof(t));
        box = {{-10, -1, -10}, int16_t(-0x7fff), 0, {10, 1, 10}, 2, 0, {0, 0, 0}, 0.01f};
        coll.boxes = reinterpret_cast<uint8_t*>(&box);
        coll.list = reinterpret_cast<uint8_t*>(tris);
        coll.rest = surface;
        coll.head = head;
        glist.coll = &coll;
        glist.sphere = {0, 0, 0, 15};
    }
};

// Sala (bit 0x40000, para build_FindCel) com a colisão já no espaço do mundo (0x8000000), caixa de
// -10 a 10 em x e z e de -1 a 20 em y; o piso, se houver, é o quadrado em y = 0.
cel_tag* make_room(celglist_tag* floor) {
    cel_tag* cel = build_alloc_cel(glb_world->first);
    glb_world->first = cel;
    cel->type = 0x4c023 | 0x8000000;
    cel->glist = floor;
    cel->box_min = {-10, -1, -10, 0};
    cel->box_max = {10, 20, 10, 0};
    return cel;
}

// Definição de arma com só o que os projéteis leem: id e classe 0x10 (nenhum caso especial), dano 10.
struct Weapon {
    weapon_definition_tag def{};
    Weapon(float speed, float range) {
        def.id = 0x10;
        def.klass = 0x10;
        def.damage = 10.0f;
        def.range = range;
        def.speed = speed;
    }
};

obj_tag* fire(Weapon& w, _VECTOR pos, _VECTOR dir) {
    return Bullet_init(-1, nullptr, nullptr, &w.def, &pos, &dir, 0xffff);
}

float dot3(const _VECTOR& a, const _VECTOR& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

}  // namespace

TEST(bullet_init_sets_up_the_projectile) {
    fresh();
    Square floor(3);
    cel_tag* room = make_room(&floor.glist);
    Weapon w(0.5f, 100.0f);
    obj_tag* o = fire(w, {1, 5, 1, 1}, {0, -2, 0, 0});
    CHECK(o != nullptr && o->type == 5 && o->cel == room && o->state == 0);
    if (o == nullptr) return;
    BU_tag* b = bullet_of(o);
    CHECK(o->hit_test == &b->hit_test && b->weapon == &w.def && b->from == nullptr);
    CHECK((o->mode & 2) != 0 && o->scale == 1.0f);
    // Sem dispersão a direção é a pedida, normalizada, e a linha 2 da matriz aponta para ela.
    CHECK(b->dir.x == 0.0f && b->dir.y == -1.0f && b->dir.z == 0.0f);
    CHECK_NEAR(o->matrix.m[2][1], -1.0, 1e-6);
    CHECK(b->speed == 0.5f && b->timer == 210.0f && b->dist == 0.0f && b->start.y == 5.0f);
    CHECK(Bullet_init(-1, nullptr, nullptr, &w.def, &b->start, nullptr, 0xffff) == nullptr);
    // Quem atira ganha o mode 0x400 (os projéteis esquecem dele quando é apagado).
    _VECTOR p{0, 1, 0, 1};
    obj_tag* shooter = control_create_object(0x40, &p, &p, nullptr);
    obj_tag* o2 = Bullet_init(0, shooter, shooter, &w.def, &p, &b->dir, 0xffff);
    CHECK(o2 != nullptr && (shooter->mode & 0x400) != 0 && bullet_of(o2)->from == shooter);
}

TEST(bullet_flies_at_its_speed_and_stops_at_the_floor) {
    fresh();
    Square floor(3);
    cel_tag* room = make_room(&floor.glist);
    Weapon w(0.5f, 100.0f);
    w.def.hit_flags = 2;  // efeitos de bala no impacto (Effect_Create com 0x61)
    obj_tag* o = fire(w, {1, 5, 1, 1}, {0, -1, 0, 0});
    BU_tag* b = bullet_of(o);
    const int count = DynamicObjCount;
    tick();
    // Primeiro passo: 0,5 a 1 x a velocidade.
    const float first = 5.0f - o->pos.y;
    CHECK(0.25f <= first && first <= 0.5f);
    CHECK_NEAR(b->dist, first, 1e-6);
    CHECK(o->pos.x == 1.0f && o->pos.z == 1.0f && o->state == 1);
    // O teste do tick é um raio de 1 m (o passo é menor) a partir da posição antes de andar, com o
    // dano da arma para quem for atingido.
    CHECK(b->hit_test.type == 0x201 && b->hit_test.cel == room && b->hit_test.flags == 0x20);
    CHECK_NEAR(b->hit_test.start.y, 5.0, 1e-6);
    float damage;
    std::memcpy(&damage, &b->hit_test.unknown_90, 4);
    CHECK(damage == 10.0f);
    // Depois, exatamente a velocidade por tick até o raio alcançar o piso.
    int ticks = 1;
    float prev = o->pos.y;
    while (o->state == 1 && ticks < 40) {
        tick();
        ++ticks;
        CHECK_NEAR(prev - o->pos.y, 0.5, 1e-5);
        prev = o->pos.y;
    }
    CHECK(o->state == 2 && (o->flags & 0x10) != 0);
    CHECK(-0.5f < o->pos.y && o->pos.y <= 0.5f);  // o raio de 1 m pegou o piso antes de atravessar
    CHECK(o->hits != nullptr && o->hits->cel == room);
    if (o->hits != nullptr) CHECK_NEAR(o->hits->pos.y, 0.0, 1e-3);
    // O impacto deixou a marca de bala do material no piso, 1 cm acima dele.
    CHECK(room->attach != nullptr && EffectList.used.count == 1);
    if (room->attach != nullptr) {
        CHECK(room->attach->glist == &g_mark && room->attach->cel == room);
        CHECK_NEAR(room->attach->matrix.m[3][1], 0.01, 2e-3);
    }
    CHECK(alive(o) && DynamicObjCount == count);
    // Estado 2: apagada no tick seguinte.
    tick();
    CHECK(!alive(o) && DynamicObjCount == count - 1);
}

TEST(bullet_stops_exactly_at_its_range) {
    for (uint32_t rate : {60u, 30u}) {
        fresh(rate);
        Square floor(3);
        make_room(&floor.glist);
        Weapon w(0.7f, 5.3f);
        obj_tag* o = fire(w, {-9, 1, 0, 1}, {1, 0, 0, 0});
        BU_tag* b = bullet_of(o);
        int ticks = 0;
        float last_x = o->pos.x;
        while (alive(o) && ticks < 40) {
            last_x = o->pos.x;
            tick();
            ++ticks;
            // A 30 Hz cada tick anda o dobro (FRAME_RATE_MUL = 2).
            if (alive(o) && 1 < ticks && b->dist <= 5.3f) CHECK_NEAR(o->pos.x - last_x, 0.7f * FRAME_RATE_MUL, 1e-5);
        }
        CHECK(!alive(o));
        // O último passo é cortado no alcance: a bala some exatamente 5,3 m adiante.
        CHECK_NEAR(last_x, -9.0 + 5.3, 1e-5);
    }
}

TEST(bullet_hit_is_copied_to_the_object_with_the_damage) {
    fresh();
    Square geometry(3);
    cel_tag* room = make_room(nullptr);
    // O quadrado girado vira uma parede em x = 5, de frente para -x (matriz fixa: mode 4).
    _VECTOR pos{5, 0, 0, 0}, rot{0, 0, 0, 0};
    obj_tag* wall = control_create_object(0x40, &pos, &rot, nullptr);
    control_link_object_to_cel(wall, room);
    wall->glist = &geometry.glist;
    wall->mode |= 4;
    std::memset(&wall->matrix, 0, sizeof(wall->matrix));
    wall->matrix.m[0][1] = 1;
    wall->matrix.m[1][0] = -1;
    wall->matrix.m[2][2] = 1;
    wall->matrix.m[3][0] = 5;
    wall->matrix.m[3][3] = 1;
    wall->sphere = {5, 0, 0, 15};
    Weapon w(0.8f, 100.0f);
    w.def.damage = 25.0f;
    obj_tag* o = fire(w, {0, 1, 2, 1}, {1, 0, 0, 0});
    float before = o->pos.x;
    for (int i = 0; i < 20 && o->state < 2; ++i) {
        before = o->pos.x;
        tick();
    }
    CHECK(o->state == 2 && o->hits != nullptr);
    // O raio do tick (1 m a partir da posição antes de andar) alcançou a parede.
    CHECK(4.0f < before && before <= 5.0f);
    if (o->hits != nullptr) {
        CHECK(o->hits->obj == wall);
        CHECK_NEAR(o->hits->pos.x, 5.0, 1e-3);
        CHECK_NEAR(o->hits->plane.a, -1.0, 1e-3);
    }
    // A parede recebe o acerto (flag 1) com a bala e o dano da arma.
    CHECK(wall->hits != nullptr);
    if (wall->hits != nullptr) {
        CHECK(wall->hits->obj == o && (wall->hits->flags & 1) != 0);
        float damage;
        std::memcpy(&damage, &wall->hits->unknown_08, 4);
        CHECK(damage == 25.0f);
    }
}

TEST(bullet_bounces_and_loses_speed_on_the_floor) {
    fresh();
    Square floor(3);  // material 3: sobra metade da velocidade num quique
    make_room(&floor.glist);
    Weapon w(0.5f, 100.0f);
    w.def.hit_flags = 0x80;
    obj_tag* o = fire(w, {1, 6, 1, 1}, {0, -1, 0, 0});
    BU_tag* b = bullet_of(o);
    int ticks = 0;
    while (b->bounces == 0 && ticks < 40) {
        tick();
        ++ticks;
    }
    CHECK(b->bounces == 1 && o->state == 1 && alive(o));
    CHECK_NEAR(b->dir.y, 1.0, 1e-5);  // refletida pela normal do piso
    CHECK_NEAR(b->speed, 0.25, 1e-6);
    CHECK(g_sound_stats.calls == 1);  // o som do primeiro quique
}

TEST(bullet_sticks_to_the_floor) {
    fresh();
    Square floor(3);
    make_room(&floor.glist);
    Weapon w(0.5f, 100.0f);
    w.def.hit_flags = 0x100;
    obj_tag* o = fire(w, {2, 6, 3, 1}, {0, -1, 0, 0});
    BU_tag* b = bullet_of(o);
    for (int i = 0; i < 40 && o->state != 3; ++i) tick();
    CHECK(o->state == 3);
    // Recuada 0,04 da superfície pela normal e apontando para dentro dela.
    CHECK_NEAR(o->pos.y, 0.04, 1e-5);
    CHECK_NEAR(o->pos.x, 2.0, 1e-5);
    CHECK_NEAR(o->matrix.m[2][1], -1.0, 1e-5);
    const _VECTOR stuck = o->pos;
    for (int i = 0; i < 10; ++i) tick();
    CHECK(alive(o) && o->state == 3 && b->hit_test.type == 0 && b->speed == 0.0f);
    CHECK(o->pos.x == stuck.x && o->pos.y == stuck.y && o->pos.z == stuck.z);
}

TEST(bullet_gravity_keeps_horizontal_speed) {
    fresh();
    make_room(nullptr);
    Weapon w(0.5f, 100.0f);
    w.def.flags2 = 8;
    obj_tag* o = fire(w, {-9, 10, 0, 1}, {1, 0, 0, 0});
    BU_tag* b = bullet_of(o);
    tick();  // primeiro passo, sem gravidade
    CHECK(o->state == 1);
    const float g = 9.8f / 3600.0f;
    for (int k = 1; k <= 20; ++k) {
        const _VECTOR before = o->pos;
        const float speed_before = b->speed;
        tick();
        // Velocidade por tick: x constante, y cai g dt^2 por tick.
        CHECK_NEAR(b->dir.x * b->speed, 0.5, 1e-5);
        CHECK_NEAR(b->dir.y * b->speed, -g * k, 1e-5);
        // Peculiaridade: o passo usa a velocidade de antes da gravidade e a direção de depois.
        CHECK_NEAR(o->pos.x - before.x, speed_before * b->dir.x, 1e-5);
        CHECK_NEAR(o->pos.y - before.y, speed_before * b->dir.y, 1e-5);
    }
}

TEST(bullet_spread_stays_in_its_cone) {
    fresh();
    make_room(nullptr);
    const _VECTOR aim{0.6f, 0.0f, 0.8f, 0.0f};
    Weapon exact(1.0f, 100.0f);
    Weapon loose(1.0f, 100.0f);
    loose.def.spread = 100.0f;  // perturbação de até 100 x 0,0014 = 0,14
    const float cos_max = std::cos(std::asin(0.14f));
    int deviated = 0;
    for (int i = 0; i < 50; ++i) {
        obj_tag* a = fire(exact, {0, 1, 0, 1}, aim);
        CHECK_NEAR(dot3(bullet_of(a)->dir, aim), 1.0, 1e-6);
        obj_tag* o = fire(loose, {0, 1, 0, 1}, aim);
        const _VECTOR& d = bullet_of(o)->dir;
        CHECK_NEAR(dot3(d, d), 1.0, 1e-5);
        CHECK(cos_max - 1e-5f <= dot3(d, aim));
        if (dot3(d, aim) < 0.99999f) ++deviated;
        CHECK_NEAR(o->matrix.m[2][0], d.x, 1e-5);
        CHECK_NEAR(o->matrix.m[2][2], d.z, 1e-5);
    }
    CHECK(deviated > 10);
}

TEST(bullet_homing_turns_toward_the_target) {
    fresh();
    make_room(nullptr);
    _VECTOR car_pos{6, 1, 4, 1}, rot{0, 0, 0, 0};
    obj_tag* car = control_create_object(0x40, &car_pos, &rot, nullptr);
    car->type = 0x28;
    car->unknown_fc = 255;
    car->flags |= 0x20000000;  // sem sala: não procura uma
    Weapon w(0.2f, 100.0f);
    w.def.flags2 = 0x20000;
    obj_tag* o = fire(w, {-6, 1, 0, 1}, {1, 0, 0, 0});
    BU_tag* b = bullet_of(o);
    tick();
    CHECK(b->target == car);
    float prev_angle = 10.0f;
    _VECTOR prev_dir = b->dir;
    for (int i = 0; i < 15; ++i) {
        tick();
        _VECTOR to{car->pos.x - o->pos.x, car->pos.y - o->pos.y, car->pos.z - o->pos.z, 0};
        Vec_Normalise(&to, &to);
        const float angle = std::acos(std::fmin(1.0f, dot3(b->dir, to)));
        CHECK(angle < prev_angle);
        prev_angle = angle;
        // No máximo 0,01 por componente por tick (mais o efeito de renormalizar).
        CHECK(std::fabs(b->dir.x - prev_dir.x) < 0.0105f && std::fabs(b->dir.z - prev_dir.z) < 0.0105f);
        prev_dir = b->dir;
    }
    // Alvos: carros vistos recentemente, jogadores com vida.
    car->unknown_fc = 0;
    CHECK(!Bullet_heatseekable(car));
    _VECTOR p{0, 1, 0, 1};
    obj_tag* player = control_create_object(0x970, &p, &rot, nullptr);
    player->type = 3;
    bloke_of(player)->health = 10.0f;
    CHECK(Bullet_heatseekable(player));
    bloke_of(player)->health = 0.0f;
    CHECK(!Bullet_heatseekable(player));
}

TEST(bullet_fuse_explodes_after_210_ticks) {
    fresh();
    make_room(nullptr);
    Weapon w(0.001f, 1000.0f);
    w.def.hit_flags = 0x200;
    obj_tag* o = fire(w, {0, 1, 0, 1}, {1, 0, 0, 0});
    const uint32_t booms = g_explode_stats.created;
    for (int i = 0; i < 210; ++i) tick();
    CHECK(alive(o) && bullet_of(o)->timer == 0.0f && g_explode_stats.created == booms);
    tick();
    CHECK(!alive(o) && g_explode_stats.created == booms + 1);
}

TEST(satchel_shows_its_countdown_digits) {
    fresh();
    Square floor(3);
    make_room(&floor.glist);
    celglist_tag digits[10]{};
    for (uint32_t i = 0; i < 10; ++i) hashtable_additem(0x2000791 + i, &digits[i]);
    // Satchel 0x3c (classe 0x3b): ((0x3c - 0x3b) x 5 + 5) s = 600 ticks de pavio, gruda no piso.
    Weapon w(0.5f, 100.0f);
    w.def.id = 0x3c;
    w.def.klass = 0x3b;
    w.def.hit_flags = 0x300;
    obj_tag* o = fire(w, {1, 1, 1, 1}, {0, -1, 0, 0});
    BU_tag* b = bullet_of(o);
    CHECK(b->timer == 600.0f && b->charge == 100);
    for (int i = 0; i < 30; ++i) {
        tick();
        // A cada tick dois dígitos novos (tipo 0x10); os do tick anterior já foram apagados.
        int count = 0;
        const celglist_tag* tens = nullptr;
        const celglist_tag* units = nullptr;
        for (obj_tag* d = DynamicObjList.next; d != nullptr; d = d->next) {
            if (d->type != 0x10) continue;
            ++count;
            if (count == 1) units = d->glist;  // o segundo criado fica na frente da lista
            if (count == 2) tens = d->glist;
        }
        CHECK(count == 2);
        CHECK(tens == &digits[0] && units == &digits[9]);  // 600 - k ticks: 9 s
    }
    CHECK(alive(o) && o->state == 3 && b->timer == 570.0f);
}

TEST(casings_are_reused_in_a_ring) {
    fresh();
    make_room(nullptr);
    Bullet_InitCasingLists();
    CHECK(NextCasing == 0);
    for (obj_tag* c : ShellCasings) CHECK(c != nullptr && c->state == 2 && c->type == 0);
    // Atirador com a arma 1: munição 5, cápsula 0x2000447, 7 ticks de atraso (tabelas sintéticas,
    // restauradas no fim).
    static uint8_t saved_weapons[kWeaponDataSize], saved_ammo[kAmmoDataSize];
    std::memcpy(saved_weapons, weapon_data, sizeof(weapon_data));
    std::memcpy(saved_ammo, ammo_data, sizeof(ammo_data));
    std::memset(ammo_data, 0, sizeof(ammo_data));
    alignas(16) static uint8_t anim[0x120];
    std::memset(anim, 0, sizeof(anim));
    reinterpret_cast<ANIMOBJ_tag*>(anim)->weapon = 1;
    _VECTOR p{0, 1, 0, 1};
    obj_tag* shooter = control_create_object(0x40, &p, &p, nullptr);
    shooter->sphere_src = anim;
    weapon_def(1)->ammo_type = 5;
    weapon_def(1)->casing_delay = 7;
    const uint32_t entity = 0x2000447;
    std::memcpy(ammo_data + 5 * 0xc + 4, &entity, 4);
    Bullet_init_casing(shooter);
    obj_tag* c = ShellCasings[0];
    const CASING_tag* data = static_cast<const CASING_tag*>(c->data);
    CHECK(NextCasing == 1 && c->type == 0xd && c->state == 3);
    CHECK(data->shooter == shooter && data->entity == entity && data->timer == 7.0f);
    for (int i = 0; i < kShellCasings; ++i) Bullet_init_casing(shooter);
    CHECK(NextCasing == 1);  // deu a volta nas 32
    // Munição sem cápsula: nada muda.
    weapon_def(1)->ammo_type = 6;
    Bullet_init_casing(shooter);
    CHECK(NextCasing == 1);
    std::memcpy(weapon_data, saved_weapons, sizeof(weapon_data));
    std::memcpy(ammo_data, saved_ammo, sizeof(ammo_data));
    // Esperando para sair: conta o atraso; sem atirador, volta a ser guardada.
    CASING_tag* w = static_cast<CASING_tag*>(c->data);
    w->shooter = nullptr;
    w->timer = 2.0f;
    Bullet_casing_update(c);
    CHECK(c->state == 3 && w->timer == 1.0f);
    Bullet_casing_update(c);
    CHECK(c->state == 2);
}

TEST(casing_falls_and_rings_once) {
    fresh();
    make_room(nullptr);
    Bullet_InitCasingLists();
    obj_tag* c = ShellCasings[3];
    CASING_tag* d = static_cast<CASING_tag*>(c->data);
    c->type = 0xd;
    c->state = 0;
    c->pos = {0, 1, 0, 1};
    d->vel = {0.01f, 0.02f, 0, 0};
    d->spin = {0.1f, 0, 0, 0};
    d->floor_y = 0.9f;
    d->surface = 3;
    d->timer = 0.0f;
    const float g = 9.8f / 3600.0f;
    double y = 1.0;
    for (int k = 0; k < 30; ++k) {
        Bullet_casing_update(c);
        y += 0.02 - g * k;  // anda com a velocidade de antes e depois cai
        CHECK_NEAR(c->pos.y, y, 1e-4);
    }
    CHECK_NEAR(c->pos.x, 0.3, 1e-5);
    CHECK_NEAR(c->rot.x, 3.0, 1e-4);
    CHECK(g_sound_stats.calls == 1 && d->floor_y == -1000000.0f);
    for (int k = 30; k < 241; ++k) Bullet_casing_update(c);
    CHECK(c->state == 2);
}

TEST(debris_dies_with_its_life) {
    fresh();
    _VECTOR p{0, 0, 0, 1};
    obj_tag* o = control_create_object(0x40, &p, &p, nullptr);
    uint8_t* d = static_cast<uint8_t*>(o->data);
    const int16_t life = 2;
    std::memcpy(d + 0x30, &life, 2);
    d[0x32] = 0x10;
    const _VECTOR vel{0, 1, 0, 0};
    std::memcpy(d + 0x10, &vel, sizeof(vel));
    Debris_Update(o);
    CHECK((o->life & 1) == 0 && o->unknown_103[3] == 0xef && o->pos.y == 1.0f);
    Debris_Update(o);
    CHECK((o->life & 1) == 0);
    Debris_Update(o);  // vida esgotada
    CHECK((o->life & 1) != 0);
}

TEST(ricochet_probability_and_random_helpers) {
    view_debug_power_on();
    materials();
    CHECK(Effect_RicochetProb(5, 0) == 0);
    CHECK(Effect_RicochetProb(0, 8) == 0);
    bool zero = false, eight = false;
    for (int i = 0; i < 200; ++i) {
        const int8_t r = Effect_RicochetProb(5, 8);
        CHECK(r == 0 || r == 8);
        zero = zero || r == 0;
        eight = eight || r == 8;
    }
    CHECK(zero && eight);
    // Rand_FRand_MVar2 é o sorteio de Rand_FRand menos o segundo parâmetro.
    view_debug_power_on();
    const float a = Rand_FRand(2.0f);
    view_debug_power_on();
    CHECK(Rand_FRand_MVar2(2.0f, 0.5f) == a - 0.5f);
    for (int i = 0; i < 1000; ++i) {
        const float r = Rand_FRand_MVar2(3.0f, 1.0f);
        CHECK(-1.0f <= r && r < 2.0f);
    }
    // Ângulos de uma direção: elevação e azimute, z = 0, w intocado; volta pelo esférico do jogo.
    _VECTOR s{0, 0, 0, 7.0f};
    vecutil_cartesian_to_spherical_acc(&s, 1.0f, 1.0f, 0.0f);
    CHECK_NEAR(s.x, 0.785398, 1e-6);
    CHECK_NEAR(s.y, 1.570796, 1e-6);
    CHECK(s.z == 0.0f && s.w == 7.0f);
    vecutil_cartesian_to_spherical_acc(&s, 0.3f, -0.4f, -0.5f);
    _VECTOR back;
    Vec_Spherical_2_Cartesian(&back, std::sqrt(0.5f), s.y, s.x);
    CHECK_NEAR(back.x, 0.3, 1e-4);
    CHECK_NEAR(back.y, -0.4, 1e-4);
    CHECK_NEAR(back.z, -0.5, 1e-4);
}
