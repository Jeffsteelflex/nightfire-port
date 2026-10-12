// Testes de effect.cpp (marcas, fumaça, rastros, detritos), das listas DList_* e do tweak de cor por
// objeto (View_SetupRenderModes_Tweak), com materiais e modelos sintéticos.

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "test_framework.h"

namespace {

constexpr uint32_t kMark = 0x2000301, kPuff = 0x2000302, kTrail = 0x2000303, kChip = 0x2000304;
celglist_tag g_models[4]{};

// Uma sala de 10 m de altura e um material (2) com marca, uma fumaça e detritos.
struct FxWorld {
    alignas(16) cel_tag room{};
    FxWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        object_debug_power_on();
        collide_debug_power_on();
        view_debug_power_on();
        effect_debug_power_on();
        hashtable_debug_power_on();
        hashtable_initialise();
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        std::memset(&GameState, 0, sizeof(GameState));
        std::memset(MPSettings, 0, sizeof(MPSettings));
        GS_SetRefreshRate(60, 60);
        WldGravity = {0.0f, -9.8f, 0.0f, 0.0f};
        const uint32_t hashes[4] = {kMark, kPuff, kTrail, kChip};
        for (int i = 0; i < 4; ++i) hashtable_additem(hashes[i], &g_models[i]);
        Effect_InitLists();
        std::memset(EffectInfo, 0, sizeof(EffectInfo));
        HEINFO_tag& m = EffectInfo[2];
        m.decal = kMark;
        m.gas[0] = {kPuff, 1, 0xff000080u, 0x00ff0000u, 1.0f, 3.0f, 10, 0, 0};
        m.debris = kChip;
        m.debris_speed = 2.0f;
        m.debris_spin = 0.5f;
        m.debris_life = 20;
        m.debris_on = 1;
        m.debris_max = 3;
        room.box_max = {10.0f, 10.0f, 10.0f, 0.0f};
        room.box_min = {-10.0f, 0.0f, -10.0f, 0.0f};
    }
    // Um acerto no piso (normal +y) da sala no material 2.
    HITDATA_tag floor_hit(float x) {
        HITDATA_tag h{};
        h.plane = {0.0f, 1.0f, 0.0f, 0.0f};
        h.pos = {x, 0.0f, 0.0f, 1.0f};
        h.surface = 2;
        h.cel = &room;
        return h;
    }
    int count_attach() const {
        int n = 0;
        for (const ATTACH_tag* a = room.attach; a != nullptr; a = a->next) ++n;
        return n;
    }
};

// O "atirador" de Effect_Bullet: um objeto com 0x34 bytes de dados (o jogador em +0x30).
obj_tag* bullet_from_player(obj_tag* player) {
    obj_tag* b = control_create_object(0x40, nullptr, nullptr, nullptr);
    nf::p32_store(static_cast<uint8_t*>(b->data) + 0x30, player);
    return b;
}

}  // namespace

TEST(dlist_moves_nodes_between_free_and_used) {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    DLISTINFO_tag l{};
    CHECK(DList_Init(&l, 0x20, 3) == 1 && l.free.count == 3 && l.used.count == 0);
    void* a = DList_MoveFromFree2InUse(&l);
    void* b = DList_MoveFromFree2InUse(&l);
    void* c = DList_MoveFromFree2InUse(&l);
    CHECK(a != nullptr && b != nullptr && c != nullptr && DList_MoveFromFree2InUse(&l) == nullptr);
    CHECK(l.used.count == 3 && l.used.first == a && l.used.last == c);
    // Tirar o do meio: os vizinhos se ligam e ele volta para o começo dos livres.
    CHECK(DList_RemoveFromInUse2Free(&l, static_cast<LLNODE_tag*>(b)) == 1);
    CHECK(l.used.count == 2 && static_cast<LLNODE_tag*>(a)->next == c && l.free.first == b);
    // Os livres saem pelo fim; com um só livre, sai ele.
    CHECK(DList_MoveFromFree2InUse(&l) == b);
    DList_AllocnNodes(&l, 2);
    CHECK(l.free.count == 2);
}

TEST(effect_bullet_marks_and_recycles_the_oldest) {
    FxWorld w;
    obj_tag* player = control_create_object(0, nullptr, nullptr, nullptr);
    player->type = 3;
    obj_tag* bullet = bullet_from_player(player);
    for (int i = 0; i < 200; ++i) {
        HITDATA_tag h = w.floor_hit(float(i) * 0.01f);
        Effect_Create(&h, bullet, 1, 1.0f);
    }
    CHECK(EffectList.used.count == 200 && EffectList.free.count == 0 && w.count_attach() == 200);
    const ATTACH_tag* oldest = reinterpret_cast<const ATTACH_tag*>(nf::raw(EffectList.used.first));
    CHECK(oldest->glist == &g_models[0] && oldest->cel == &w.room);
    // 1 cm acima do piso, com o eixo da frente na normal.
    CHECK_NEAR(oldest->matrix.m[3][1], 0.01, 1e-6);
    CHECK_NEAR(oldest->matrix.m[2][1], 1.0, 1e-5);
    // A 201ª reaproveita a mais antiga (a da posição 0).
    HITDATA_tag h = w.floor_hit(5.0f);
    Effect_Create(&h, bullet, 1, 1.0f);
    CHECK(EffectList.used.count == 200 && w.count_attach() == 200);
    CHECK(w.room.attach->matrix.m[3][0] == 5.0f);
    bool still_there = false;
    for (const ATTACH_tag* a = w.room.attach; a != nullptr; a = a->next) still_there |= a->matrix.m[3][0] == 0.0f;
    CHECK(!still_there);
}

TEST(effect_create_makes_smoke_and_debris_by_material) {
    FxWorld w;
    obj_tag* player = control_create_object(0, nullptr, nullptr, nullptr);
    player->type = 3;
    obj_tag* bullet = bullet_from_player(player);
    const int before = DynamicObjCount;
    HITDATA_tag h = w.floor_hit(0.0f);
    // Bala do jogador com a flag 0x20: fumaça (sempre) e de 1 a 3 detritos.
    Effect_Create(&h, bullet, 0x21, 1.0f);
    int puffs = 0, chips = 0;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        puffs += o->type == 0xe;
        chips += o->type == 0x10;
    }
    CHECK(puffs == 1 && 1 <= chips && chips <= 3 && DynamicObjCount == before + puffs + chips);
    // A tangente guardada no acerto é perpendicular à normal.
    const float* t = reinterpret_cast<const float*>(h.unknown_20);
    CHECK_NEAR(t[1], 0.0, 1e-5);
    CHECK(0.5f < std::sqrt(t[0] * t[0] + t[2] * t[2]));
    // Material sem efeitos (0): nada.
    HITDATA_tag none = w.floor_hit(1.0f);
    none.surface = 0;
    Effect_Create(&none, bullet, 0x21, 1.0f);
    CHECK(EffectList.used.count == 1);
}

TEST(gas_grows_fades_and_dies) {
    FxWorld w;
    _VECTOR pos{0.0f, 1.0f, 0.0f, 1.0f};
    _VECTOR rot{};
    obj_tag* g = Gas_Init(&w.room, &pos, &rot, nullptr, 1.0f, 3.0f, kPuff, 1, 0xff000080u, 0x00ff0000u, 10);
    CHECK(g != nullptr && g->type == 0xe && g->glist == &g_models[1] && g->cel == &w.room);
    CHECK(g->unknown_103[0] == 0xff && g->unknown_103[3] == 0x80);
    for (int i = 0; i < 5; ++i) gases_smoke(g);
    // Na metade da vida: metade da escala e das cores.
    CHECK_NEAR(g->scale, 2.0, 1e-5);
    // (as cores andam em float por tick e viram byte truncando)
    CHECK(std::abs(int(g->unknown_103[0]) - 127) <= 1 && std::abs(int(g->unknown_103[1]) - 127) <= 1);
    CHECK(std::abs(int(g->unknown_103[3]) - 64) <= 1 && g->unknown_103[2] == 0);
    CHECK((g->life & 1) == 0);
    for (int i = 0; i < 5; ++i) gases_smoke(g);
    CHECK((g->life & 1) != 0);
    // Sem sala ou com vida 0, nada.
    CHECK(Gas_Init(nullptr, &pos, &rot, nullptr, 1.0f, 1.0f, kPuff, 1, 0, 0, 10) == nullptr);
    CHECK(Gas_Init(&w.room, &pos, &rot, nullptr, 1.0f, 1.0f, kPuff, 1, 0, 0, 0) == nullptr);
}

TEST(gas_rises_and_stops_at_the_ceiling) {
    FxWorld w;
    _VECTOR pos{0.0f, 9.9f, 0.0f, 1.0f};
    _VECTOR rot{};
    // Tipo 0 sem velocidade: sobe 1 m/s, freado 5% por tick, e a gravidade puxa 2% para cima.
    obj_tag* g = Gas_Init(&w.room, &pos, &rot, nullptr, 1.0f, 1.0f, kPuff, 0, 0xffffffffu, 0xffffffffu, 600);
    const float y0 = g->pos.y;
    gases_smoke(g);
    const float expect = (1.0f / 60.0f) * 0.95f + 9.8f * (1.0f / 60.0f) * 0.02f;
    CHECK_NEAR(g->pos.y - y0, expect, 1e-5);
    for (int i = 0; i < 600 && (g->life & 1) == 0; ++i) gases_smoke(g);
    CHECK(g->pos.y == 10.0f);  // o teto da sala
}

TEST(swoosh_follows_its_line_and_fades) {
    FxWorld w;
    Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
    viewer_tag* v = glb_viewer[0];
    v->pos = {0.0f, 0.0f, 10.0f, 1.0f};
    _VECTOR from{0.0f, 1.0f, 0.0f, 1.0f};
    _VECTOR to{4.0f, 1.0f, 0.0f, 1.0f};
    obj_tag* s = Swoosh_Create(&from, &to, 60, kTrail, 0);
    CHECK(s != nullptr && s->type == 0x41 && s->glist == &g_models[2]);
    Swoosh_Update(s);
    CHECK(s->unknown_103[3] == uint8_t(59 * 128 / 60) && s->scale == 4.0f);
    // O eixo da frente acompanha a linha (comprimento 4) e a largura cai com o tempo.
    CHECK_NEAR(s->matrix.m[2][0], 4.0, 1e-4);
    for (int i = 0; i < 29; ++i) Swoosh_Update(s);
    CHECK(s->unknown_103[3] == 64);
    for (int i = 0; i < 30 && (s->life & 1) == 0; ++i) Swoosh_Update(s);
    CHECK((s->life & 1) != 0);
}

TEST(effect_marks_on_objects_go_back_with_the_object) {
    FxWorld w;
    _VECTOR at{3.0f, 0.0f, 0.0f, 1.0f};
    obj_tag* crate = control_create_object(0, &at, nullptr, nullptr);
    obj_tag* bullet = bullet_from_player(nullptr);
    HITDATA_tag h = w.floor_hit(3.5f);
    h.cel = nullptr;
    h.obj = crate;
    crate->cel = &w.room;
    // Sem Rand_Rand(2) dando fumaça, só a marca importa aqui.
    Effect_Create(&h, bullet, 1, 1.0f);
    CHECK(crate->attach != nullptr && w.room.attach == nullptr && crate->attach->parent == crate);
    // A marca fica no espaço do objeto: 0,5 à direita do centro dele.
    CHECK_NEAR(crate->attach->matrix.m[3][0], 0.5, 1e-5);
    Effect_RemoveSObjs(crate);
    CHECK(crate->attach == nullptr && EffectList.used.count == 0 && EffectList.free.count == 200);
}

TEST(tweak_is_the_object_colour_times_the_room_light) {
    FxWorld w;
    psi_draw_debug_power_on();
    Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
    viewer_tag* v = glb_viewer[0];
    obj_tag* o = control_create_object(0, nullptr, nullptr, nullptr);
    o->unknown_103[0] = 0x80;
    o->unknown_103[1] = 0x40;
    o->unknown_103[2] = 0x20;
    o->unknown_103[3] = 0x60;
    View_SetupRenderModes_Tweak(v, o);
    CHECK(olda == 0x60 && oldr == 0x80 && oldg == 0x40);
    // Com a flag 0x800, multiplicada pela luz da sala (/256).
    o->flags |= 0x800;
    o->color[0] = 0x80;
    o->color[1] = 0xff;
    o->color[2] = 0x00;
    View_SetupRenderModes_Tweak(v, o);
    CHECK(oldr == 0x40 && oldg == 0x3f && oldb == 0);
    // Na visão do modo 1, objetos 0x20000 com 1/4 do alfa.
    o->flags = 0x20000;
    v->mode = 1;
    View_SetupRenderModes_Tweak(v, o);
    CHECK(olda == 0x18);
}
