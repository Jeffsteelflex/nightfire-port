// Testes dos corpos rígidos (engine/rigid_body.h) e do resto da colisão (Collide_CylinderIntersect,
// Collide_PlaceObject): a inércia de um cubo, RB_Build, as conversões mundo/corpo, o giro pelo quatérnio, a queda com
// arrasto (velocidade final), o arrasto angular, o quique no chão, o torque de uma força num ponto, os tiros, o pêndulo
// que volta à vertical, RB_Create, a cápsula contra um piso e o lugar de uma metralhadora.

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/rigid_body.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "platform/psi.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

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
    hashtable_debug_power_on();
    hashtable_initialise();
    sound_debug_power_on();
    std::memset(&GameState, 0, sizeof(GameState));
    GS_SetRefreshRate(60, 60);
    WldGravity = {0.0f, -9.8f, 0.0f, 0.0f};
    g_rigid_body_stats = RigidBodyStats{};
}

// Um corpo solto à mão (sem objeto): matriz identidade, massa 2, cubo de 1 m.
struct Body {
    alignas(16) MATRIX m{};
    RIGIDBODY rb{};
    explicit Body(uint16_t kind) {
        Mat_Identity(&m);
        RB_Init(&rb, kind);
        rb.sound = 0xffff;
        rb.matrix = &m;
        float inertia[3];
        RB_CuboidMomentsOfInertia(inertia, 2.0f, 1.0f, 1.0f, 1.0f);
        RB_Build(&rb, kind == 0 ? &WldGravity : &CONSTANT_ZERO_VECTOR, inertia, 2.0f);
        RB_SetState(&rb);
    }
};

// Sala com colisão como a de test_collide.cpp: um piso de 20 x 20 em y = 0, normal +y.
struct FloorRoom {
    alignas(16) uint8_t head[0x40]{};
    COLLBOX_tag box{};
    uint16_t tris[2][4]{};
    uint8_t surface[2]{3, 3};
    colldata_tag coll{};
    celglist_tag glist{};
    cel_tag* cel = nullptr;
    FloorRoom() {
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
        glist.box_min = {-10, -1, -10, 0};
        glist.box_max = {10, 1, 10, 0};
        cel = build_alloc_cel(glb_world->first);
        glb_world->first = cel;
        cel->glist = &glist;
        cel->type = 0xc023 | 0x40000 | 0x8000000;  // sala (0x40000: build_FindCel), sem matriz
        cel->glist_flags = 0;
        cel->box_min = {-10, -1, -10, 0};
        cel->box_max = {10, 10, 10, 0};
        cel->water_y = -500.0f;
    }
};

}  // namespace

TEST(rigid_body_cuboid_inertia_and_build) {
    float I[3];
    // Cubo de lado a e massa m: I = m a² / 6 nos três eixos; caixa 1 x 2 x 3: Ix = m (4 + 9) / 12...
    RB_CuboidMomentsOfInertia(I, 6.0f, 1.0f, 1.0f, 1.0f);
    for (float v : I) CHECK_NEAR(v, 1.0f, 1e-5f);
    RB_CuboidMomentsOfInertia(I, 12.0f, 1.0f, 2.0f, 3.0f);
    CHECK_NEAR(I[0], 13.0f, 1e-4f);
    CHECK_NEAR(I[1], 10.0f, 1e-4f);
    CHECK_NEAR(I[2], 5.0f, 1e-4f);
    // RB_Build: só momentos que formam um triângulo; os inversos guardados com I2 antes de I1.
    RIGIDBODY rb{};
    const float bad[3] = {1.0f, 1.0f, 3.0f};
    CHECK(RB_Build(&rb, &CONSTANT_ZERO_VECTOR, bad, 1.0f) == 0);
    const float neg[3] = {-1.0f, 1.0f, 1.0f};
    CHECK(RB_Build(&rb, &CONSTANT_ZERO_VECTOR, neg, 1.0f) == 0);
    CHECK(RB_Build(&rb, &CONSTANT_ZERO_VECTOR, I, 4.0f) == 1);
    CHECK_NEAR(rb.inv_mass, 0.25f, 1e-6f);
    CHECK_NEAR(rb.inv_inertia[0], 1.0f / 13.0f, 1e-6f);
    CHECK_NEAR(rb.inv_inertia[1], 1.0f / 5.0f, 1e-6f);
    CHECK_NEAR(rb.inv_inertia[2], 1.0f / 10.0f, 1e-6f);
}

TEST(rigid_body_world_body_and_omega) {
    // RB_ToBody desfaz RB_ToWorld numa rotação.
    MATRIX m;
    const _VECTOR e{0.3f, -1.1f, 0.7f, 0.0f};
    RotMatrix(&e, &m);
    const _VECTOR v{1.0f, 2.0f, -3.0f, 0.0f};
    _VECTOR w{}, b{};
    RB_ToWorld(&w, &v, &m);
    RB_ToBody(&b, &w, &m);
    CHECK_NEAR(b.x, v.x, 1e-5f);
    CHECK_NEAR(b.y, v.y, 1e-5f);
    CHECK_NEAR(b.z, v.z, 1e-5f);
    // Girar a identidade (escalar primeiro) por omega = (0, pi, 0) durante 0,5 s: 90 graus em volta de y, com o
    // quatérnio unitário; omega zero não muda nada.
    quaternion_tag q{1.0f, 0.0f, 0.0f, 0.0f};
    const _VECTOR omega{0.0f, 3.1415927f, 0.0f, 0.0f};
    RB_apply_omega(&q, &omega, 0.5f);
    CHECK_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f, 1e-4f);
    CHECK_NEAR(q.x, 0.70710678f, 1e-3f);
    CHECK_NEAR(std::fabs(q.z), 0.70710678f, 1e-3f);
    MATRIX r;
    Mat_Identity(&r);
    Quat_QuaternionToMatrix(&r, &q);
    CHECK_NEAR(r.m[1][1], 1.0f, 1e-3f);
    CHECK_NEAR(r.m[0][0], 0.0f, 2e-3f);
    const quaternion_tag before = q;
    const _VECTOR zero{0.0f, 0.0f, 0.0f, 0.0f};
    RB_apply_omega(&q, &zero, 0.5f);
    CHECK(q.x == before.x && q.y == before.y && q.z == before.z && q.w == before.w);
}

TEST(rigid_body_falls_to_terminal_velocity) {
    fresh();
    Body body(0);
    // Tipo 0: gravidade com arrasto linear de 1 s; a velocidade tende a g x 1 s = 9,8 m/s e a posição desce.
    for (int i = 0; i < 600; ++i) RB_EulerEvolve(&body.rb, REC_FRAME_RATE, 0);
    CHECK_NEAR(body.rb.vel.y, -9.8f, 0.05f);
    CHECK(body.m.m[3][1] < -40.0f);
    // Um passo a partir do repouso: a = (e^-dt - 1) / dt x (0 - g) ~ g.
    Body rest(0);
    RB_EulerEvolve(&rest.rb, REC_FRAME_RATE, 0);
    CHECK_NEAR(rest.rb.accel.y, -9.8f * 60.0f * (1.0f - nf::fm::exp(-1.0f / 60.0f)), 1e-3f);
    CHECK_NEAR(rest.rb.vel.y, rest.rb.accel.y / 60.0f, 1e-5f);
    // Os pêndulos (tipo 1) não andam: sem gravidade, a posição fica.
    Body pend(1);
    for (int i = 0; i < 60; ++i) RB_EulerEvolve(&pend.rb, REC_FRAME_RATE, 0);
    CHECK(pend.m.m[3][1] == 0.0f && pend.rb.vel.y == 0.0f);
}

TEST(rigid_body_angular_damping_and_impulses) {
    fresh();
    Body body(0);
    // O arrasto angular do tipo 0 (31): omega x (1 - (1 - e^(-dt/31)) / dt) por passo (a forma do original).
    body.rb.omega = {0.0f, 2.0f, 0.0f, 0.0f};
    RB_EulerEvolve(&body.rb, REC_FRAME_RATE, 0);
    const float k = 1.0f - (1.0f - nf::fm::exp(-(1.0f / 60.0f) / 31.0f)) * 60.0f;
    CHECK_NEAR(body.rb.omega.y, 2.0f * k, 1e-4f);
    // Uma força num ponto: o impulso soma a força e o torque r x F; aplicado, o giro vem pela inércia (presa a +-4).
    Body p(1);
    const _VECTOR f{0.0f, 0.0f, 1.0f, 0.0f};
    const _VECTOR at{1.0f, 0.0f, 0.0f, 0.0f};
    RB_AddImpulsiveForceAtPoint(&p.rb, &f, &at);
    CHECK_NEAR(p.rb.impulse.z, 1.0f, 1e-6f);
    CHECK_NEAR(p.rb.ang_impulse.y, -1.0f, 1e-6f);
    CHECK_NEAR(p.rb.ang_impulse.x, 0.0f, 1e-6f);
    RB_ApplyImpulses(&p.rb);
    // I = 2 / 6 nos três eixos: -1 / (1/3) = -3.
    CHECK_NEAR(p.rb.omega.y, -3.0f, 1e-4f);
    p.rb.omega = {0.0f, 0.0f, 0.0f, 0.0f};
    p.rb.ang_impulse = {0.0f, -10.0f, 0.0f, 0.0f};
    RB_ApplyImpulses(&p.rb);
    CHECK_NEAR(p.rb.omega.y, -4.0f, 1e-5f);
}

TEST(rigid_body_pendulum_swings_back_to_vertical) {
    fresh();
    Body p(1);
    // Inclinado de lado (giro em z): o impulso que leva o "cima" à vertical e o arrasto o trazem de volta.
    p.rb.omega = {0.0f, 0.0f, 3.0f, 0.0f};
    float max_tilt = 0.0f;
    for (int i = 0; i < 30; ++i) {
        RB_EulerEvolve(&p.rb, REC_FRAME_RATE, 0);
        max_tilt = std::max(max_tilt, nf::fm::acos(std::min(1.0f, p.m.m[1][1])));
    }
    CHECK(max_tilt > 0.2f);
    for (int i = 0; i < 1200; ++i) RB_EulerEvolve(&p.rb, REC_FRAME_RATE, 0);
    CHECK(nf::fm::acos(std::min(1.0f, p.m.m[1][1])) < 0.05f);
}

TEST(rigid_body_bounces_and_takes_shots) {
    fresh();
    Body body(0);
    obj_tag holder{};
    holder.matrix = body.m;
    // Caindo a 4 m/s contra um piso que empurra 0,1 para cima: v += n x -(r d + d) com r = -0,5 deixa a velocidade
    // normal pela metade, sem inverter (o corpo não quica: assenta), a tangente fica, e a posição anda o empurrão.
    body.rb.matrix = &holder.matrix;
    body.rb.vel = {1.0f, -4.0f, 0.0f, 0.0f};
    body.rb.test.push = {0.0f, 0.1f, 0.0f, 0.0f};
    RB_RestingContact(&holder, &body.rb, nullptr);
    CHECK_NEAR(body.rb.vel.y, -2.0f, 1e-5f);
    CHECK_NEAR(body.rb.vel.x, 1.0f, 1e-6f);
    CHECK_NEAR(holder.matrix.m[3][1], 0.1f, 1e-6f);
    // Um tiro (acerto de um objeto 5 com dano 4 na direção +z, 0,5 acima do centro): impulso dano x 0,5 e torque.
    obj_tag bullet{};
    bullet.type = 5;
    P32<HITDATA_tag> hits = nullptr;
    HITDATA_tag* h = Coll_AddHitToList(&hits, 1.0f, nullptr, &bullet);
    const float dmg = 4.0f;
    std::memcpy(&h->unknown_08, &dmg, 4);
    h->dir = {0.0f, 0.0f, 1.0f, 0.0f};
    h->pos = {0.0f, 0.6f, 0.0f, 1.0f};
    h->plane = {0.0f, 0.0f, -1.0f, 0.0f};
    body.rb.test.push = {0.0f, 0.0f, 0.0f, 0.0f};
    body.rb.vel = {0.0f, 0.0f, 0.0f, 0.0f};
    RB_RestingContact(&holder, &body.rb, hits);
    CHECK_NEAR(body.rb.impulse.z, 2.0f, 1e-5f);
    CHECK_NEAR(body.rb.ang_impulse.x, 2.0f * 0.5f, 1e-5f);
    CHECK(g_rigid_body_stats.bullet_hits == 1);
    // Os pêndulos só ficam com os eixos do tipo: tipo 2 só x, tipo 3 só y.
    Body p(2);
    p.rb.matrix = &holder.matrix;
    Mat_Identity(&holder.matrix);
    h->pos = {0.5f, 0.6f, 0.0f, 1.0f};
    p.rb.test.radius = 0.3f;
    RB_RestingContactRot(&holder, &p.rb, hits);
    CHECK(p.rb.ang_impulse.x != 0.0f && p.rb.ang_impulse.y == 0.0f && p.rb.ang_impulse.z == 0.0f);
    Body q(3);
    q.rb.matrix = &holder.matrix;
    q.rb.test.radius = 0.3f;
    RB_RestingContactRot(&holder, &q.rb, hits);
    CHECK(q.rb.ang_impulse.x == 0.0f && q.rb.ang_impulse.y != 0.0f && q.rb.ang_impulse.z == 0.0f);
    Collide_FreeHitList(&hits);
}

TEST(rigid_body_create_from_placement) {
    fresh();
    FloorRoom room;
    celglist_tag model{};
    model.box_min = {-0.2f, -0.5f, -0.1f, 0.0f};
    model.box_max = {0.2f, 0.1f, 0.1f, 0.0f};
    model.sphere = {0.0f, 0.0f, 0.0f, 0.45f};
    level_tag level{};
    _VECTOR pos{1.0f, 3.0f, 2.0f, 1.0f};
    _VECTOR rot{0.0f, 1.0f, 0.0f, 0.0f};
    // Tipo 0, sem massa (3) nem som (0xffff): teste de esfera 0x101 com o raio do modelo, gravidade, orientação
    // identidade (o giro do posicionamento se perde).
    obj_tag* o = RB_Create(&pos, &rot, &model, &level);
    CHECK(o != nullptr && o->type == kRigidBodyType);
    auto* rb = static_cast<RIGIDBODY*>(o->data);
    CHECK(o->hit_test == &rb->test && rb->matrix == &o->matrix && (o->mode & 0x44) == 0x44);
    CHECK(rb->mass == 3.0f && rb->sound == 0xffffu && rb->test_type == 0x101 && rb->gravity == &WldGravity);
    CHECK_NEAR(rb->test.radius, 0.45f, 1e-6f);
    CHECK_NEAR(rb->half_height, 0.5f, 1e-6f);
    CHECK_NEAR(o->matrix.m[0][0], 1.0f, 1e-6f);
    CHECK(rb->test.ignore_a == o);
    // Tipo 2: o raio é o menor lado em x/z (0,2), sem teste no Collide_Update; tipo 1: metade da diagonal em xz.
    level.params[2] = 2;
    level.params[0] = 5;
    level.params[4] = 0x123;
    rb = static_cast<RIGIDBODY*>(RB_Create(&pos, &rot, &model, &level)->data);
    CHECK(rb->test_type == 0 && rb->mass == 5.0f && rb->sound == 0x123 && rb->gravity == &CONSTANT_ZERO_VECTOR);
    CHECK_NEAR(rb->test.radius, 0.2f, 1e-6f);
    level.params[2] = 1;
    rb = static_cast<RIGIDBODY*>(RB_Create(&pos, &rot, &model, &level)->data);
    CHECK_NEAR(rb->test.radius, std::sqrt(0.4f * 0.4f + 0.2f * 0.2f) * 0.5f, 1e-5f);
    CHECK(g_rigid_body_stats.created == 3);
}

TEST(rigid_body_lands_on_a_floor) {
    fresh();
    FloorRoom room;
    celglist_tag model{};
    model.box_min = {-0.25f, -0.25f, -0.25f, 0.0f};
    model.box_max = {0.25f, 0.25f, 0.25f, 0.0f};
    model.sphere = {0.0f, 0.0f, 0.0f, 0.3f};
    level_tag level{};
    _VECTOR pos{1.0f, 2.0f, 2.0f, 1.0f};
    _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
    obj_tag* o = RB_Create(&pos, &rot, &model, &level);
    o->cel = room.cel;
    // O tick do jogo para este objeto: atualizar (cai), colisão (o piso empurra a esfera e o corpo quica e assenta).
    for (int i = 0; i < 240; ++i) {
        ++GameState.frame;
        RB_Update(o);
        Collide_Update(DynamicObjList.next);
    }
    // Parado em cima do piso: o centro a um raio (0,3) do chão, sem velocidade vertical.
    CHECK_NEAR(o->matrix.m[3][1], 0.3f, 0.05f);
    CHECK(std::fabs(static_cast<RIGIDBODY*>(o->data)->vel.y) < 0.5f);
    CHECK(g_rigid_body_stats.bounces > 0);
}

TEST(cylinder_intersect_pushes_off_the_floor) {
    fresh();
    FloorRoom room;
    obj_tag owner{};
    alignas(16) HITTEST_tag test{};
    test.start = {1.0f, 0.9f, 2.0f, 0.0f};
    test.end = {1.0f, 0.3f, 2.0f, 0.0f};
    test.radius = 0.55f;
    test.cel = room.cel;
    test.mask = 0x10;
    test.tester = &owner;
    P32<HITDATA_tag> hits = nullptr;
    // A cápsula com a ponta 0,3 acima do piso sai 0,25 para cima; o acerto é o piso; o testador volta ao dono.
    CHECK(Collide_CylinderIntersect(&test, &hits, 0, 0));
    CHECK(hits != nullptr && hits->cel == room.cel);
    CHECK_NEAR(test.push.y, 0.25f, 1e-3f);
    CHECK(test.type == 0x800 && test.tester == &owner);
    Collide_FreeHitList(&hits);
    // Longe do piso: nada.
    test.start = {1.0f, 4.0f, 2.0f, 0.0f};
    test.end = {1.0f, 3.0f, 2.0f, 0.0f};
    test.push = {0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(!Collide_CylinderIntersect(&test, &hits, 0, 0));
    CHECK(hits == nullptr && test.push.y == 0.0f);
}

TEST(place_object_needs_floor_room) {
    fresh();
    FloorRoom room;
    // No meio do piso cabe: o ponto vai para o chão + raio e a matriz fica em pé.
    alignas(16) _VECTOR pos{1.0f, 1.0f, 2.0f, 1.0f};
    MATRIX m;
    Mat_Identity(&m);
    CHECK(Collide_PlaceObject(&pos, &m, 0.5f, 0.6f, nullptr));
    CHECK_NEAR(pos.y, 0.5f, 1e-3f);
    CHECK_NEAR(m.m[1][1], 1.0f, 1e-4f);
    // No canto (a 0,2 das duas bordas) a caixa do chão não chega a 0,6 do ponto: não cabe.
    pos = {9.8f, 1.0f, 9.8f, 1.0f};
    Mat_Identity(&m);
    CHECK(!Collide_PlaceObject(&pos, &m, 0.5f, 0.6f, nullptr));
    // Sem chão em 3 m para baixo: não cabe.
    pos = {1.0f, 5.0f, 2.0f, 1.0f};
    Mat_Identity(&m);
    CHECK(!Collide_PlaceObject(&pos, &m, 0.5f, 0.6f, nullptr));
}
