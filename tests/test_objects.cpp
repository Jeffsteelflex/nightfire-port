// Testes do núcleo de objetos (src/engine/control.cpp, llist.cpp, objects.cpp).

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

struct Node {
    LLNODE_tag link;
    int value;
};

void fresh() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    pending_reset();
}

// Sala (tipo 0x40000) com caixa [lo, hi] e cores de luz nos dois canais.
cel_tag* add_room(float lo, float hi, uint8_t red) {
    cel_tag* c = build_alloc_cel(glb_world->first);
    glb_world->first = c;
    c->type = 0x40000;
    c->box_min = {lo, lo, lo, 0};
    c->box_max = {hi, hi, hi, 0};
    c->misc[0x9a - 0x98] = red;
    c->misc[0x9b - 0x98] = 1;  // canal 1
    c->misc[0x9c - 0x98] = 20;
    c->misc[0x9d - 0x98] = 0;
    c->misc[0x9e - 0x98] = 30;
    c->misc[0x9f - 0x98] = 0;
    c->misc[0xa2 - 0x98] = 0;  // canal de interruptor da sala (o heap deixa 0x98)
    return c;
}

float det3(const MATRIX& m) {
    return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
           m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
           m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
}

}  // namespace

TEST(llist_add_remove_insert_cut) {
    fresh();
    LLISTINFO_tag list;
    CHECK(LList_Init(&list, sizeof(Node), 3) == 1);
    CHECK(list.count == 3 && list.node_size == sizeof(Node));
    Node* a = reinterpret_cast<Node*>(nf::raw(list.first));
    Node* b = reinterpret_cast<Node*>(nf::raw(a->link.next));
    Node* c = reinterpret_cast<Node*>(nf::raw(b->link.next));
    CHECK(list.last == &c->link && c->link.next == nullptr && b->link.prev == &a->link);
    CHECK(LList_Remove(&list, &b->link) == &b->link);
    CHECK(list.count == 2 && a->link.next == &c->link && c->link.prev == &a->link);
    LList_Insert(&list, nullptr, &b->link);  // no começo
    CHECK(list.first == &b->link && b->link.next == &a->link && a->link.prev == &b->link);
    CHECK(LList_Cut(&list) == &c->link && list.last == &a->link && a->link.next == nullptr);
    LList_Insert(&list, &a->link, &c->link);  // depois do último
    CHECK(list.last == &c->link && list.count == 3);
}

TEST(control_create_and_delete_object) {
    fresh();
    _VECTOR pos{1, 2, 3, 0}, rot{0, 1.5707964f, 0, 0};
    obj_tag* a = control_create_object(0x10, &pos, &rot, nullptr);
    obj_tag* b = control_create_object(0, &pos, nullptr, nullptr);
    CHECK(DynamicObjCount == 2 && control_first_object() == b && b->next == a && a->prev == b);
    CHECK(a->data == reinterpret_cast<uint8_t*>(a) + 0x110);
    CHECK(a->scale == 1.0f && a->color[0] == 0xff && a->unknown_f8 == 0x1f && (a->life & 2) != 0);
    CHECK(a->old_pos.z == 3.0f && a->matrix.m[3][0] == 1.0f && a->matrix.m[3][2] == 3.0f);
    // Rotação de 90 graus em y: a linha 0 vira (0, 0, -1).
    CHECK_NEAR(a->matrix.m[0][0], 0.0, 1e-5);
    CHECK_NEAR(a->matrix.m[0][2], -1.0, 1e-5);
    control_delete_object(b);
    CHECK(DynamicObjCount == 1 && control_first_object() == a && a->prev == &DynamicObjList);
    CHECK(b->sphere_src == nullptr);  // AnimObjectDelete sem objeto de animação não faz nada
    CHECK(Control_ReturnNextObjectOfType(0, nullptr) == a);
    Control_DeleteAllObjectsOfType(0);
    CHECK((a->life & 1) != 0);  // só marca
}

TEST(view_rot_trans_matrix_is_a_rotation) {
    const _VECTOR pos{5, 6, 7, 0};
    for (float x : {0.0f, 0.4f, -1.1f}) {
        for (float y : {0.0f, 2.0f}) {
            for (float z : {0.0f, 0.7f}) {
                const _VECTOR rot{x, y, z, 0};
                MATRIX m{};
                View_RotTransMatrix(&rot, &pos, &m);
                CHECK_NEAR(det3(m), 1.0, 1e-4);
                for (int r = 0; r < 3; ++r) {
                    CHECK_NEAR(m.m[r][0] * m.m[r][0] + m.m[r][1] * m.m[r][1] + m.m[r][2] * m.m[r][2], 1.0, 1e-4);
                }
                CHECK(m.m[3][1] == 6.0f && m.m[0][3] == 0.0f);  // a coluna w não é escrita
                // Só com rotação em y é igual à RotMatrix dos posicionamentos estáticos.
                if (x == 0.0f && z == 0.0f) {
                    MATRIX r{};
                    RotMatrix(&rot, &r);
                    for (int i = 0; i < 3; ++i) {
                        for (int j = 0; j < 3; ++j) CHECK_NEAR(m.m[i][j], r.m[i][j], 1e-5);
                    }
                }
            }
        }
    }
    const _VECTOR rot{0.3f, 0.2f, 0.1f, 0}, scale{2, 3, 4, 0};
    MATRIX a{}, b{};
    View_RotTransMatrix(&rot, &pos, &a);
    View_RotTransScaleMatrix(&rot, &pos, &scale, &b);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) CHECK_NEAR(b.m[r][c], a.m[r][c] * (r == 0 ? 2 : r == 1 ? 3 : 4), 1e-5);
    }
}

TEST(find_cel_and_link_to_room) {
    fresh();
    cel_tag* small = add_room(0, 10, 100);
    cel_tag* big = add_room(-50, 50, 200);
    build_alloc_cel(nullptr);  // célula fora do mundo: ignorada
    const _VECTOR inside_both{5, 5, 5, 0}, only_big{40, 0, 0, 0}, outside{90, 0, 0, 0};
    // Salas sem geometria: os raios não acertam nada e fica a primeira da lista de salas (big foi a
    // última criada, então vem primeiro).
    CHECK(build_FindCel(&inside_both, glb_world) == big);
    CHECK(build_FindCel(&only_big, glb_world) == big);
    CHECK(build_FindCel(&outside, glb_world) == nullptr);
    CHECK(gbl_room_cels != nullptr && gbl_room_cels[0] == big && gbl_room_cels[1] == small && gbl_room_cels[2] == nullptr);

    _VECTOR pos{40, 0, 0, 0};
    obj_tag* obj = control_create_object(0, &pos, nullptr, nullptr);
    switch_channels[0] = 1;  // canal 1: segundo byte de cada par de cor
    CHECK(build_LinkToRoom(obj, 0, glb_world) == 1);
    CHECK(obj->cel == big && big->objects == obj && obj->cel_prev == big);
    CHECK(obj->color[0] == 1 && obj->color[1] == 0 && obj->color[2] == 0);
    CHECK(obj->sphere.x == 40.0f && obj->sphere.w == 0.1f);  // sem modelo: raio 0,1
    control_unlink_object(obj);
    CHECK(big->objects == nullptr && obj->cel == nullptr);
    obj->flags |= 0x20000000;  // sem sala: vai para a ForcedList
    CHECK(build_LinkToRoom(obj, 0, glb_world) == 0 && ForcedList.count == 1 && (obj->flags & 0x10008000) == 0x10008000);
    control_delete_object(obj);
    CHECK(ForcedList.count == 0 && DynamicObjCount == 0);
}

TEST(dynamic_and_break_create) {
    fresh();
    add_room(-100, 100, 50);
    celglist_tag glist{};
    glist.flags = 0x1200;
    glist.sphere = {0, 1, 0, 2};
    level_tag level;
    std::memset(&level, 0, sizeof(level));
    level.params[0] = 7;
    level.params[1] = 3;
    level.params[2] = 0x00f0;
    level.params[3] = 9;
    _VECTOR pos{1, 0, 0, 0}, rot{0, 0, 0, 0};
    obj_tag* d = DynamicObject_Create(&pos, &rot, &level, &glist);
    CHECK(d->type == 0x52 && d->glist == &glist && d->cel != nullptr);
    CHECK(d->flags == (0x1200 | 0x30));  // params[0] != 0 liga 0x30
    const int16_t* dd = static_cast<const int16_t*>(d->data);
    CHECK(dd[0] == 7 && dd[1] == 3 && dd[4] == 0xf0 && reinterpret_cast<const uint8_t*>(dd)[10] == 9);
    // Esfera do modelo levada para o mundo (o seno da VU0 dá 0,99999994 em pi/2).
    CHECK_NEAR(d->sphere.x, 1.0, 1e-6);
    CHECK_NEAR(d->sphere.y, 1.0, 1e-6);
    CHECK(d->sphere.w == 2.0f);

    obj_tag* b = Break_Create(&pos, &rot, &glist, &level);
    CHECK(b->type == 0x20 && b->glist == &glist && b->flags == 0x1200);
    const uint32_t* bd = static_cast<const uint32_t*>(b->data);
    float strength;
    std::memcpy(&strength, &bd[1], 4);
    uint16_t mask;
    std::memcpy(&mask, &bd[2], 2);
    CHECK(bd[0] == 7 && strength == 3.0f && mask == uint16_t(~0x00f0));
    CHECK(DynamicObjCount == 2);
}

TEST(sky_slots_and_env) {
    fresh();
    celglist_tag sky{};
    const _VECTOR pos{1, 2, 3, 0}, rot{0, 0.5f, 0, 0};
    View_AddSkyObj(4, &sky, &pos, &rot, nullptr, 0, 0, 0, 7);
    View_AddSkyObj(5, &sky, nullptr, nullptr, nullptr, 1, 10, 9, 0);
    View_AddSkyObj(21, &sky, &pos, &rot, nullptr, 0, 0, 0, 0);  // fora das 21 entradas: ignorado
    CHECK(GfxList.sky[4].glist == &sky && GfxList.sky[4].pos.z == 3.0f && GfxList.sky[4].layer == 7);
    CHECK(GfxList.sky[5].glist_flash == &sky && GfxList.sky[5].glist == nullptr && GfxList.sky[5].flash_frames == 9);
    CHECK(GfxList.sky[5].flash_period == int16_t(10 * FRAME_RATE) && GfxList.sky[5].rot.y == 0.0f);

    cel_tag* room = add_room(-10, 10, 0);
    celglist_tag room_glist{};
    room_glist.sphere.w = 4.0f;
    room->glist = &room_glist;
    level_tag level;
    std::memset(&level, 0, sizeof(level));
    level.params[0] = 2;
    level.params[3] = 25;
    level.params[9] = 1;
    level.params[10] = 90;
    _VECTOR at{0, 0, 0, 0}, dir{0, 0.3f, 0, 0};
    Env_Create(&at, &dir, nullptr, &level);
    ENV_tag* env = static_cast<ENV_tag*>(room->env);
    CHECK(env != nullptr && GlobalEnv == env && env->kind == 2 && env->unknown_4c == 0);
    CHECK_NEAR(env->unknown_30, 2.5, 1e-6);
    CHECK(env->inv_radius == 0.25f);
    // side = dir x (0, 1, 0): perpendicular aos dois.
    CHECK_NEAR(env->side.x * env->dir.x + env->side.y * env->dir.y + env->side.z * env->dir.z, 0.0, 1e-6);
    CHECK(env->side.y == 0.0f);
    CHECK_NEAR(std::sqrt(SunVector.x * SunVector.x + SunVector.y * SunVector.y + SunVector.z * SunVector.z), 1000.0, 0.1);
    Env_Create(&at, &dir, nullptr, &level);  // uma vez por sala
    CHECK(room->env == env);
}
