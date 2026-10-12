// Testes do núcleo de colisão (src/engine/collide.cpp) e da ordenação (sort.cpp).

#include <cmath>
#include <cstring>
#include <vector>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/sort.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

void fresh() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    glb_world = build_alloc_world();
}

// Sala com colisão: um piso quadrado de 20 x 20 em y = 0 (dois triângulos, uma folha, vértices
// quantizados com escala 0,01), normal +y.
struct FloorRoom {
    alignas(16) uint8_t head[0x40]{};
    COLLBOX_tag box{};
    uint16_t tris[2][4]{};
    uint8_t surface[2]{3, 3};
    colldata_tag coll{};
    celglist_tag glist{};
    cel_tag* cel = nullptr;

    explicit FloorRoom(bool identity, float x) {
        const int16_t v[4][3] = {{-1000, 0, -1000}, {-1000, 0, 1000}, {1000, 0, 1000}, {1000, 0, -1000}};
        std::memcpy(head, v, sizeof(v));                        // vértices nos índices 0, 3, 6, 9
        const int16_t n[3] = {0, 16383, 0};
        std::memcpy(head + 24, n, sizeof(n));                   // normal no índice 12
        const float d = 0.0f;
        std::memcpy(head + 30, &d, 4);
        const uint16_t t[2][4] = {{0, 3, 6, 12}, {0, 6, 9, 12}};
        std::memcpy(tris, t, sizeof(t));
        // child_a = 0x8001 (-0x7fff): folha com 1 bloco de vértices.
        box = {{-10, -1, -10}, int16_t(-0x7fff), 0, {10, 1, 10}, 2, 0, {0, 0, 0}, 0.01f};
        coll.boxes = reinterpret_cast<uint8_t*>(&box);
        coll.list = reinterpret_cast<uint8_t*>(tris);
        coll.rest = surface;
        coll.head = head;
        glist.coll = &coll;
        glist.sphere = {0, 0, 0, 15};
        cel = build_alloc_cel(glb_world->first);
        glb_world->first = cel;
        cel->glist = &glist;
        cel->type = identity ? 0xc023 | 0x8000000 : 0xc023;
        cel->pos = {x, 0, 0, 0};
        cel->rot = {0, 0, 0, 0};
        cel->glist_flags = 0;
    }
};

}  // namespace

TEST(collide_closest_point_and_triangles) {
    const _VECTOR a{0, 0, 0, 0}, b{10, 0, 0, 0};
    _VECTOR out;
    const _VECTOR p1{4, 3, 0, 0}, p2{-5, 1, 0, 0}, p3{20, 0, 0, 0};
    CHECK_NEAR(vecutil_calculate_closest_point_on_line(&out, &a, &b, &p1), 0.4, 1e-6);
    CHECK(out.x == 4.0f && out.y == 0.0f);
    CHECK(vecutil_calculate_closest_point_on_line(&out, &a, &b, &p2) == 0.0f && out.x == 0.0f);
    CHECK(vecutil_calculate_closest_point_on_line(&out, &a, &b, &p3) == 1.0f && out.x == 10.0f);

    // Triângulo no plano y = 0 com normal +y e a orientação usada pela colisão do jogo.
    _VECTOR v0{0, 0, 0, 0}, v1{0, 0, 10, 0}, v2{10, 0, 0, 0};
    const plane_equ_tag up{0, 1, 0, 0};
    const _VECTOR inside{2, 5, 2, 0}, beyond_edge{-3, 4, 2, 0}, below{2, -1, 2, 0};
    uint16_t code = 0;
    _VECTOR q;
    CHECK_NEAR(vecutil_Dist2Tri(&inside, &v0, &v1, &v2, &up, &code, &q), 5.0, 1e-5);
    CHECK(code == 0 && q.y == 0.0f);
    CHECK_NEAR(vecutil_Dist2Tri(&beyond_edge, &v0, &v1, &v2, &up, &code, &q), 5.0, 1e-5);  // até a aresta x = 0
    CHECK(code != 0 && q.x == 0.0f);
    CHECK(vecutil_Dist2Tri(&below, &v0, &v1, &v2, &up, &code, nullptr) == -1.0f && code == 99);

    const _VECTOR on{2, 0, 2, 0}, off{9, 0, 9, 0};
    CHECK(vecutil_point_on_poly(&on, &v0, &v1, &v2, &up) == 1);
    CHECK(vecutil_point_on_poly(&off, &v0, &v1, &v2, &up) == 0);
    const _VECTOR start{2, 7, 2, 0}, down{0, -1, 0, 0};
    float t = 0;
    CHECK(Collide_RayTriangle(&start, &down, &v0, &v1, &v2, &t) == 1);
    CHECK_NEAR(t, 7.0, 1e-5);
}

TEST(collide_ray_box) {
    HITTEST_tag test{};
    test.radius = 100.0f;
    const _VECTOR lo{0, 0, 0, 0}, hi{1, 1, 1, 0};
    float t = -1;
    test.start = {0.5f, 0.5f, 0.5f, 0};
    test.dir = {1, 0, 0, 0};
    CHECK(Intersect_RayBox(&test, &lo, &hi, &t) == 1 && t == 0.0f);  // começa dentro
    test.start = {-2, 0.5f, 0.5f, 0};
    CHECK(Intersect_RayBox(&test, &lo, &hi, &t) == 1 && t == 2.0f);
    test.start = {-2, 5, 0.5f, 0};
    CHECK(Intersect_RayBox(&test, &lo, &hi, &t) == 0);
    test.start = {-2, 0.5f, 0.5f, 0};
    test.dir = {-1, 0, 0, 0};  // afastando
    CHECK(Intersect_RayBox(&test, &lo, &hi, &t) == 0);
}

TEST(quicksort_matches_comparators) {
    struct Entry {
        uint32_t cel;
        uint16_t depth;
        uint16_t pad;
    };
    Entry e[7] = {{1, 3, 0}, {2, 0, 0}, {3, 2, 0}, {4, 0, 0}, {5, 7, 0}, {6, 1, 0}, {7, 2, 0}};
    QuickSort(e, 7, sizeof(Entry), COMP_DRAWCELS, nullptr);
    for (int i = 1; i < 7; ++i) CHECK(e[i - 1].depth <= e[i].depth);
    float keys[9] = {5, -1, 3, 3, 0, 8, -7, 2, 1};
    QuickSort(keys, 9, 4, COMP_ALPHAOBJ, nullptr);
    for (int i = 1; i < 9; ++i) CHECK(keys[i - 1] <= keys[i]);
}

TEST(collide_sphere_and_ray_against_floor) {
    fresh();
    FloorRoom room(true, 0.0f);
    // Esfera de raio 1 com o centro 0,5 acima do piso: acerto a 0,5 e empurrão de 0,5 para cima.
    _VECTOR pos{1, 0.5f, 1, 0};
    P32<HITDATA_tag> hits = nullptr;
    CHECK(Collide_SphereIntersect(&pos, 1.0f, room.cel, nullptr, nullptr, &hits, 1, 0, 0x90, 0));
    CHECK(hits != nullptr && hits->next == nullptr && hits->cel == room.cel);
    CHECK_NEAR(hits->dist, 0.5, 1e-3);
    CHECK_NEAR(hits->plane.b, 1.0, 1e-3);
    CHECK_NEAR(hits->pos.y, 0.0, 1e-3);
    CHECK_NEAR(pos.y, 1.0, 1e-3);
    CHECK(hits->surface == 3);
    Collide_FreeHitList(&hits);
    CHECK(hits == nullptr);
    // Longe do piso: nada.
    _VECTOR high{1, 5, 1, 0};
    CHECK(!Collide_SphereIntersect(&high, 1.0f, room.cel, nullptr, nullptr, &hits, 1, 0, 0x90, 0));

    // Raio de cima para baixo.
    const _VECTOR from{2, 5, 3, 0}, to{2, -5, 3, 0};
    CHECK(Collide_RayIntersect(&from, &to, room.cel, nullptr, nullptr, &hits, 1, 0x90, 0));
    CHECK_NEAR(hits->dist, 5.0, 1e-3);
    CHECK_NEAR(hits->pos.y, 0.0, 1e-3);
    CHECK_NEAR(hits->dir.y, -1.0, 1e-6);
    Collide_FreeHitList(&hits);
    // De baixo para cima o piso está de costas: não acerta.
    CHECK(!Collide_RayIntersect(&to, &from, room.cel, nullptr, nullptr, &hits, 1, 0x90, 0));
}

TEST(collide_uses_the_cel_transform) {
    fresh();
    FloorRoom room(false, 100.0f);  // sem o bit de identidade: geometria deslocada para x = 100
    const _VECTOR from{102, 5, 3, 0}, to{102, -5, 3, 0};
    P32<HITDATA_tag> hits = nullptr;
    CHECK(Collide_RayIntersect(&from, &to, room.cel, nullptr, nullptr, &hits, 1, 0x90, 0));
    CHECK_NEAR(hits->pos.x, 102.0, 1e-3);
    CHECK_NEAR(hits->pos.y, 0.0, 1e-3);
    Collide_FreeHitList(&hits);
    const _VECTOR from2{2, 5, 3, 0}, to2{2, -5, 3, 0};
    CHECK(!Collide_RayIntersect(&from2, &to2, room.cel, nullptr, nullptr, &hits, 1, 0x90, 0));
}

TEST(link_objects_to_rooms_uses_geometry) {
    fresh();
    FloorRoom room(true, 0.0f);
    room.cel->sphere = {0, 0, 0, 15};
    room.cel->box_min = {-10, -1, -10, 0};
    room.cel->box_max = {10, 5, 10, 0};
    cel_tag* prop = build_alloc_cel(glb_world->first);
    glb_world->first = prop;
    prop->type = 0x8019;
    prop->sphere = {3, 2, 3, 1};
    cel_tag* outside = build_alloc_cel(glb_world->first);
    glb_world->first = outside;
    outside->type = 0x8018;
    outside->sphere = {50, 2, 3, 1};
    build_link_objects_to_rooms(glb_world->first);
    CHECK(prop->parent == room.cel && room.cel->children == prop);
    CHECK(outside->parent == nullptr);
}

TEST(collide_update_runs_object_tests) {
    fresh();
    control_init_object_lists();
    FloorRoom room(true, 0.0f);
    _VECTOR pos{1, 0.5f, 1, 0}, rot{0, 0, 0, 0};
    obj_tag* o = control_create_object(0x40, &pos, &rot, nullptr);
    alignas(16) static HITTEST_tag test;
    std::memset(&test, 0, sizeof(test));
    test.type = 0x101;
    test.pos = pos;
    test.radius = 1.0f;
    test.cel = room.cel;
    test.mask = 0x90;
    o->hit_test = &test;
    // Uma esfera de raio 1 a 0,5 do piso: um acerto a 0,5 e um empurrão de 0,5 para cima.
    Collide_Update(DynamicObjList.next);
    CHECK(test.tester == o);
    CHECK(o->hits != nullptr && o->hits->cel == room.cel);
    if (o->hits != nullptr) CHECK_NEAR(o->hits->dist, 0.5, 1e-3);
    CHECK_NEAR(test.push.y, 0.5, 1e-3);
    CHECK_NEAR(test.pos.y, 1.0, 1e-3);  // a esfera do teste sai empurrada (como no original)
    // No tick seguinte os acertos voltam ao heap e são refeitos e o empurrão recomeça do zero. No
    // jogo a atualização do objeto recoloca o teste; aqui ele volta à posição inicial.
    test.pos = pos;
    Collide_Update(DynamicObjList.next);
    CHECK(o->hits != nullptr && o->hits->next == nullptr);
    CHECK_NEAR(test.push.y, 0.5, 1e-3);
    // Sem tipo, o teste fica parado.
    test.type = 0;
    Collide_Update(DynamicObjList.next);
    CHECK(o->hits == nullptr);
}

namespace {

// Objeto com um teste de cápsula (0x800) vertical de "from" a "to", raio r, partindo de "room".
struct CapsuleTester {
    obj_tag* obj = nullptr;
    alignas(16) HITTEST_tag test{};

    CapsuleTester(cel_tag* room, const _VECTOR& from, const _VECTOR& to, float r, uint16_t mask) {
        _VECTOR pos = to, rot{0, 0, 0, 0};
        obj = control_create_object(0x40, &pos, &rot, nullptr);
        std::memset(&test, 0, sizeof(test));
        test.type = 0x800;
        test.start = from;
        test.end = to;
        test.radius = r;
        test.cel = room;
        test.mask = mask;
        obj->hit_test = &test;
    }
};

// Sala vazia (sem geometria) para os testes contra objetos.
cel_tag* empty_room() {
    cel_tag* cel = build_alloc_cel(glb_world->first);
    glb_world->first = cel;
    cel->type = 0xc023 | 0x8000000;
    return cel;
}

}  // namespace

TEST(capsule_pushed_out_of_floor) {
    fresh();
    control_init_object_lists();
    FloorRoom room(true, 0.0f);
    // Cápsula de raio 0,55 com a ponta de baixo 0,3 acima do piso: sai 0,25 para cima, o acerto fica
    // a 0,3 com a normal do piso, e o contato conta como apoio (found bit 0).
    CapsuleTester c(room.cel, {1, 0.9f, 2, 0}, {1, 0.3f, 2, 0}, 0.55f, 0x90);
    Collide_Update(DynamicObjList.next);
    CHECK(c.obj->hits != nullptr && c.obj->hits->cel == room.cel && c.obj->hits->next == nullptr);
    if (c.obj->hits != nullptr) {
        const HITDATA_tag* h = c.obj->hits;
        CHECK_NEAR(h->dist, 0.3, 1e-3);
        CHECK_NEAR(h->plane.b, 1.0, 1e-3);
        CHECK_NEAR(h->dir.y, -1.0, 1e-3);
        CHECK_NEAR(h->pos.y, 0.0, 1e-3);
        CHECK(h->surface == 3);
    }
    CHECK_NEAR(c.test.push.x, 0.0, 1e-6);
    CHECK_NEAR(c.test.push.y, 0.25, 1e-3);
    CHECK_NEAR(c.test.end.y, 0.55, 1e-3);
    CHECK_NEAR(c.test.start.y, 1.15, 1e-3);
    CHECK((c.test.found & 1) != 0);

    // Longe do piso: nenhum acerto e nenhum empurrão.
    c.test.start = {1, 3, 2, 0};
    c.test.end = {1, 2, 2, 0};
    c.test.found = 0;
    Collide_Update(DynamicObjList.next);
    CHECK(c.obj->hits == nullptr);
    CHECK(c.test.push.y == 0.0f && c.test.end.y == 2.0f && c.test.found == 0);

    // Com a flag 4 (não empurrar), o acerto vem sem mexer na cápsula.
    c.test.start = {1, 0.9f, 2, 0};
    c.test.end = {1, 0.3f, 2, 0};
    c.test.flags = 4;
    Collide_Update(DynamicObjList.next);
    CHECK(c.obj->hits != nullptr);
    CHECK(c.test.push.y == 0.0f && c.test.end.y == 0.3f);
}

TEST(capsule_uses_the_cel_transform) {
    fresh();
    control_init_object_lists();
    FloorRoom room(false, 100.0f);  // piso deslocado para x = 100 pela matriz da célula
    CapsuleTester c(room.cel, {101, 0.9f, 2, 0}, {101, 0.3f, 2, 0}, 0.55f, 0x90);
    Collide_Update(DynamicObjList.next);
    CHECK(c.obj->hits != nullptr);
    if (c.obj->hits != nullptr) CHECK_NEAR(c.obj->hits->pos.x, 101.0, 1e-3);
    CHECK_NEAR(c.test.push.y, 0.25, 1e-3);
    // Em x = 1 não há piso (ele está em x = 90..110).
    CapsuleTester far(room.cel, {1, 0.9f, 2, 0}, {1, 0.3f, 2, 0}, 0.55f, 0x90);
    Collide_Update(DynamicObjList.next);
    CHECK(far.obj->hits == nullptr && far.test.push.y == 0.0f);
}

TEST(capsule_pushed_out_of_object_geometry) {
    fresh();
    control_init_object_lists();
    FloorRoom geometry(true, 0.0f);  // só a colisão é usada: o objeto é o piso girado para virar parede
    cel_tag* room = empty_room();
    _VECTOR pos{50, 0, 0, 0}, rot{0, 0, 0, 0};
    obj_tag* wall = control_create_object(0x40, &pos, &rot, nullptr);
    control_link_object_to_cel(wall, room);
    wall->glist = &geometry.glist;
    wall->sphere = {50, 0, 0, 15};
    // Matriz do objeto (vetores linha): x local -> +y, y local (a normal) -> -x, translação x = 50.
    // A parede fica no plano x = 50, de frente para -x.
    std::memset(&wall->matrix, 0, sizeof(wall->matrix));
    wall->matrix.m[0][1] = 1;
    wall->matrix.m[1][0] = -1;
    wall->matrix.m[2][2] = 1;
    wall->matrix.m[3][0] = 50;
    wall->matrix.m[3][3] = 1;
    // Cápsula deitada ao longo de y a 0,3 da parede: sai 0,25 para -x e não conta como apoio.
    CapsuleTester c(room, {49.7f, 1.5f, 2, 0}, {49.7f, 0.9f, 2, 0}, 0.55f, 0xc0);
    Collide_Update(DynamicObjList.next);
    CHECK(c.obj->hits != nullptr && c.obj->hits->obj == wall && c.obj->hits->cel == nullptr);
    if (c.obj->hits != nullptr) {
        CHECK_NEAR(c.obj->hits->dist, 0.3, 1e-3);
        CHECK_NEAR(c.obj->hits->plane.a, -1.0, 1e-3);
        CHECK_NEAR(c.obj->hits->pos.x, 50.0, 1e-3);
    }
    CHECK_NEAR(c.test.push.x, -0.25, 1e-3);
    CHECK_NEAR(c.test.push.y, 0.0, 1e-5);
    CHECK_NEAR(c.test.start.x, 49.45, 1e-3);
    CHECK(c.test.found == 0);
    // O acerto também vai para a lista da parede, marcado como recebido (flag 1).
    CHECK(wall->hits != nullptr && wall->hits->obj == c.obj && (wall->hits->flags & 1) != 0);

    // A máscara 0x10 pula os objetos; e objetos com a flag 0x20 nunca entram.
    c.test.start = {49.7f, 1.5f, 2, 0};
    c.test.end = {49.7f, 0.9f, 2, 0};
    c.test.mask = 0xd0;
    Collide_Update(DynamicObjList.next);
    CHECK(c.obj->hits == nullptr && c.test.push.x == 0.0f);
    c.test.mask = 0xc0;
    wall->flags |= 0x20;
    Collide_Update(DynamicObjList.next);
    CHECK(c.obj->hits == nullptr && c.test.push.x == 0.0f);
}

TEST(capsule_against_capsule_of_jointed_object) {
    fresh();
    control_init_object_lists();
    cel_tag* room = empty_room();
    celglist_tag no_coll{};  // modelo sem colisão: o objeto articulado passa por Collide_Jointy
    // b é criado antes, então a (no começo da DynamicObjList) é testado primeiro.
    CapsuleTester b(room, {0.8f, 0.9f, 0, 0}, {0.8f, 0.3f, 0, 0}, 0.55f, 0xc0);
    CapsuleTester a(room, {0, 0.9f, 0, 0}, {0, 0.3f, 0, 0}, 0.55f, 0xc0);
    for (CapsuleTester* t : {&a, &b}) {
        control_link_object_to_cel(t->obj, room);
        t->obj->glist = &no_coll;
        t->obj->mode = 8;
        t->obj->sphere = {t->test.start.x, 0.6f, 0, 0.6f};
    }
    uint32_t mp = 0;
    std::memcpy(MPSettings + 0x180, &mp, 4);
    // Eixos a 0,8 com raios de 0,55: no jogo de um jogador quem testa primeiro sai todo o caminho
    // (0,3) e o segundo já não encosta.
    Collide_Update(DynamicObjList.next);
    CHECK_NEAR(a.test.push.x, -0.3, 1e-4);
    CHECK_NEAR(a.test.start.x, -0.3, 1e-4);
    CHECK_NEAR(a.test.end.x, -0.3, 1e-4);
    CHECK_NEAR(b.test.push.x, 0.0, 1e-4);
    // A lista de a tem o acerto do próprio teste e, na frente, o que veio do teste de b (flag 1).
    const HITDATA_tag* own = a.obj->hits;
    while (own != nullptr && (own->flags & 1) != 0) own = own->next;
    CHECK(own != nullptr && own->obj == b.obj);
    if (own != nullptr) {
        CHECK_NEAR(own->dist, 0.3, 1e-4);
        CHECK_NEAR(own->plane.a, 1.0, 1e-5);  // do testador para o outro
    }
    // No multijogador cada um sai metade.
    mp = 1;
    std::memcpy(MPSettings + 0x180, &mp, 4);
    a.test.start = {0, 0.9f, 0, 0};
    a.test.end = {0, 0.3f, 0, 0};
    Collide_Update(DynamicObjList.next);
    CHECK_NEAR(a.test.push.x, -0.15, 1e-4);
    mp = 0;
    std::memcpy(MPSettings + 0x180, &mp, 4);
    // Separados: nada.
    a.test.start = {-2, 0.9f, 0, 0};
    a.test.end = {-2, 0.3f, 0, 0};
    b.test.start = {0.8f, 0.9f, 0, 0};
    b.test.end = {0.8f, 0.3f, 0, 0};
    Collide_Update(DynamicObjList.next);
    CHECK(a.test.push.x == 0.0f && b.test.push.x == 0.0f);
}

TEST(capsule_collects_triangles_in_world_space) {
    fresh();
    control_init_object_lists();
    FloorRoom room(false, 100.0f);
    // Teste 0x2000: junta os triângulos a menos do raio, no mundo, sem empurrar e sem acerto. A
    // cápsula fica perto da diagonal que divide o quadrado, então pega os dois triângulos.
    CapsuleTester c(room.cel, {101, 0.9f, 1.2f, 0}, {101, 0.3f, 1.2f, 0}, 0.55f, 0x90);
    c.test.type = 0x2000;
    Collide_Update(DynamicObjList.next);
    CHECK(c.obj->hits == nullptr);
    CHECK(c.test.push.y == 0.0f);
    CHECK(TriOnCnt == 6);
    const _VECTOR* tri = static_cast<const _VECTOR*>(TriHeap);
    CHECK_NEAR(tri[0].x, 90.0, 1e-4);
    CHECK_NEAR(tri[0].z, -10.0, 1e-4);
    for (uint32_t i = 0; i < TriOnCnt; ++i) CHECK_NEAR(tri[i].y, 0.0, 1e-6);
    // Longe da diagonal só o triângulo de baixo dela.
    c.test.start = {101, 0.9f, 3, 0};
    c.test.end = {101, 0.3f, 3, 0};
    Collide_Update(DynamicObjList.next);
    CHECK(TriOnCnt == 9);
}

TEST(point_in_oriented_box) {
    // Caixa com centro em (10, 0, 0), girada 90 graus em y: o eixo local x aponta para +z e o z
    // local para -x. Meias medidas 2 (x local), 1 (y), 0,5 (z local).
    MATRIX m{};
    m.m[0][2] = 1;
    m.m[1][1] = 1;
    m.m[2][0] = -1;
    m.m[3][0] = 10;
    m.m[3][3] = 1;
    const _VECTOR half{2, 1, 0.5f, 0};
    const _VECTOR along_z{10, 0, 1.9f, 0}, past_z{10, 0, 2.1f, 0}, along_x{10.6f, 0, 0, 0};
    CHECK(Intersect_PointOOBox(&m, &half, &along_z, 0.0f) == 1);
    CHECK(Intersect_PointOOBox(&m, &half, &past_z, 0.0f) == 0);
    CHECK(Intersect_PointOOBox(&m, &half, &past_z, 0.2f) == 1);
    CHECK(Intersect_PointOOBox(&m, &half, &along_x, 0.0f) == 0);
    CHECK(Intersect_PointOOBox(&m, &half, &along_x, 0.2f) == 1);
}

