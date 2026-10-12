// Testes de spline.cpp (caminhos) e door.cpp (portas), com caminhos e portas sintéticos.

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/gameflow.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/spline.h"
#include "engine/vecmath.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

constexpr float kPi = 3.14159265f;

framelist_tag frame(float x, float y, float z, float yaw) {
    framelist_tag f{};
    f.pos[0] = x;
    f.pos[1] = y;
    f.pos[2] = z;
    f.quat[1] = std::sin(yaw * 0.5f);  // rotação em torno de y
    f.quat[3] = std::cos(yaw * 0.5f);
    return f;
}

float det3(const MATRIX& m) {
    return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
           m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
           m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
}

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
    Door_Init();
    GS_SetRefreshRate(60, 60);
    for (obj_tag*& p : glb_players) p = nullptr;
}

// Porta com o caminho dado. kind: 0 de girar, 2 de correr.
struct TestDoor {
    celglist_tag glist{};
    level_tag level{};
    obj_tag* obj = nullptr;

    TestDoor(framelist_tag* path, int16_t frames, uint32_t kind) {
        glist.sphere = {0, 0, 0, 1};
        glist.box_min = {-1, -1, -1, 0};
        glist.box_max = {1, 1, 1, 0};
        level.path = path;
        level.path_count = frames;
        level.path_unknown = 30;  // 30 quadros para percorrer o caminho
        level.params[0] = kind;
    }
    obj_tag* create() {
        _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
        obj = Door_Create(&pos, &rot, &level, &glist);
        return obj;
    }
    DOORINFO* info() const { return static_cast<DOORINFO*>(obj->data); }
    // Um quadro de lógica das portas: Doors_Calc e a atualização da porta.
    void tick() {
        Doors_Calc();
        Door_Update(obj);
    }
};

}  // namespace

TEST(spline_eval_and_interp) {
    // Catmull-Rom passa pelos pontos do meio e é linear em pontos igualmente espaçados.
    const _VECTOR p0{0, 0, 0, 0}, p1{1, 2, 0, 0}, p2{2, 4, 0, 0}, p3{3, 6, 0, 0};
    _VECTOR out;
    Spline_Eval3D(&p0, &p1, &p2, &p3, 0.0f, &out);
    CHECK(out.x == 1.0f && out.y == 2.0f);
    Spline_Eval3D(&p0, &p1, &p2, &p3, 1.0f, &out);
    CHECK(out.x == 2.0f && out.y == 4.0f);
    Spline_Eval3D(&p0, &p1, &p2, &p3, 0.25f, &out);
    CHECK_NEAR(out.x, 1.25, 1e-6);
    CHECK_NEAR(out.y, 2.5, 1e-6);

    framelist_tag path[3] = {frame(0, 0, 0, 0), frame(2, 0, 0, kPi * 0.5f), frame(4, 0, 0, kPi * 0.5f)};
    MATRIX m{};
    quaternion_tag q{};
    // Meio do primeiro trecho: posição pela spline, rotação de 45 graus pelo slerp.
    Spline_Interp(path, 0, 2, 0.5f, &m, &q, 2);
    CHECK_NEAR(m.m[3][0], 1.0, 0.2);
    CHECK_NEAR(m.m[0][0], std::cos(kPi * 0.25f), 1e-5);
    CHECK_NEAR(det3(m), 1.0, 1e-5);
    CHECK_NEAR(q.y, std::sin(kPi * 0.125f), 1e-5);
    // Nos quadros: exatamente a posição do quadro.
    Spline_Interp(path, 0, 2, 2.0f, &m, nullptr, 2);
    CHECK(m.m[3][0] == 4.0f);
    // t fora do intervalo fica preso a ele.
    Spline_Interp(path, 0, 2, 7.0f, &m, nullptr, 2);
    CHECK(m.m[3][0] == 4.0f);
    // Ao contrário (first > last): t = 1,5 entre os quadros 2 e 1.
    MATRIX r{};
    Spline_Interp(path, 2, 0, 1.5f, &r, nullptr, 2);
    Spline_Interp(path, 0, 2, 1.5f, &m, nullptr, 2);
    CHECK_NEAR(r.m[3][0], m.m[3][0], 1e-5);
    // Modo 0: sem rotação. Modo fora de 0-3 com rotações diferentes: não escreve nada.
    Spline_Interp(path, 0, 2, 0.5f, &m, nullptr, 0);
    CHECK(m.m[0][0] == 1.0f && m.m[0][2] == 0.0f && m.m[2][2] == 1.0f);
    MATRIX untouched{};
    untouched.m[3][0] = 99.0f;
    Spline_Interp(path, 0, 2, 0.5f, &untouched, nullptr, 7);
    CHECK(untouched.m[3][0] == 99.0f);
    Spline_Interp(nullptr, 0, 2, 0.5f, &untouched, nullptr, 2);
    CHECK(untouched.m[3][0] == 99.0f);
}

TEST(keyframe_interp_is_linear) {
    framelist_tag path[3] = {frame(0, 0, 0, 0), frame(2, 0, 0, kPi * 0.5f), frame(10, 0, 0, kPi * 0.5f)};
    MATRIX m{};
    KeyFrame_Interp(path, 0, 2, 1.5f, &m);
    CHECK_NEAR(m.m[3][0], 6.0, 1e-5);  // linear entre 2 e 10
    CHECK_NEAR(m.m[0][0], 0.0, 1e-5);  // os dois quadros giram 90 graus
    KeyFrame_Interp(path, 0, 2, 0.5f, &m);
    CHECK_NEAR(m.m[3][0], 1.0, 1e-5);
    CHECK_NEAR(m.m[0][0], std::cos(kPi * 0.25f), 1e-5);
    KeyFrame_Interp(path, 2, 0, 0.5f, &m);  // ao contrário: entre os quadros 1 e 0
    CHECK_NEAR(m.m[3][0], 1.0, 1e-5);
}

TEST(swing_door_opens_away_and_closes) {
    fresh();
    // Caminho de girar: -90, 0 e +90 graus; fechada no meio.
    framelist_tag path[3] = {frame(0, 0, 0, -kPi * 0.5f), frame(0, 0, 0, 0), frame(0, 0, 0, kPi * 0.5f)};
    TestDoor t(path, 3, 0);
    obj_tag* door = t.create();
    DOORINFO* d = t.info();
    CHECK(door->type == 0x1c && door->state == kDoorClosed && Door_IsClosed(door));
    CHECK(d->pos == 0.5f && TheDoors.first == &d->node && d->obj == door);
    CHECK_NEAR(door->matrix.m[0][0], 1.0, 1e-6);  // fechada: a rotação do meio
    CHECK(d->side == 1.0f);
    CHECK_NEAR(d->speed, 0.5 / 7.0, 1e-7);  // meio caminho em 7 quadros inteiros

    // Quem empurra pelo lado +z abre para o lado do quadro 2 (pos sobe).
    alignas(16) static obj_tag player;
    std::memset(&player, 0, sizeof(player));
    player.sphere = {0, 0, 2, 0.5f};
    CHECK(Door_Activate(door, &player, 0) == 1);
    CHECK(door->state == kDoorOpening && d->dir == 1.0f && d->cooldown == 2);
    CHECK(Door_Activate(door, &player, 0) == 0);  // já mexida neste quadro
    for (int i = 0; i < 20 && door->state != kDoorOpen; ++i) t.tick();
    CHECK(door->state == kDoorOpen && d->pos == 1.0f && !Door_IsClosed(door));
    CHECK_NEAR(door->matrix.m[0][0], 0.0, 1e-5);  // 90 graus

    // Com alguém no caminho da folha, não fecha.
    alignas(16) static obj_tag blocker;
    std::memset(&blocker, 0, sizeof(blocker));
    blocker.sphere = {0, 0, 0, 0.5f};
    Doors_Calc();
    CHECK(Door_Activate(door, &blocker, 0) == 0 && door->state == kDoorOpen);
    // Livre, fecha e para exatamente no meio.
    Doors_Calc();
    CHECK(Door_Activate(door, &player, 0) == 1 && door->state == kDoorClosing && d->dir == -1.0f);
    for (int i = 0; i < 20 && door->state != kDoorClosed; ++i) t.tick();
    CHECK(door->state == kDoorClosed && d->pos == 0.5f && Door_IsClosed(door));
}

TEST(sliding_door_channels_locks_and_auto_close) {
    fresh();
    framelist_tag path[2] = {frame(0, 0, 0, 0), frame(2, 0, 0, 0)};
    TestDoor t(path, 2, 2);
    t.level.params[2] = 7;  // canal da porta
    t.level.params[3] = 9;  // canal que destranca
    obj_tag* door = t.create();
    DOORINFO* d = t.info();
    CHECK(door->state == kDoorClosed && d->pos == 0.0f && door->matrix.m[3][0] == 0.0f);

    // Trancada: só o som, continua fechada.
    CHECK(Door_IsLocked(door));
    CHECK(Door_Activate(door, nullptr, 0) == 0 && door->state == kDoorClosed);
    // O canal de tranca liga: a próxima atualização destranca e abre.
    switch_channels[9] = 1;
    t.tick();
    CHECK(!Door_IsLocked(door) && d->lock_channel == 0 && door->state == kDoorOpening);
    for (int i = 0; i < 60 && door->state != kDoorOpen; ++i) t.tick();
    CHECK(door->state == kDoorOpen && d->pos == 1.0f);
    CHECK_NEAR(door->matrix.m[3][0], 2.0, 1e-5);
    CHECK(switch_channels[7] == 1);  // aberta: o canal da porta fica ligado

    // Fecha sozinha depois de 6 segundos (360 quadros) sem ninguém perto.
    int frames = 0;
    while (door->state == kDoorOpen && frames < 400) {
        t.tick();
        ++frames;
    }
    CHECK(frames == 361 && door->state == kDoorClosing);
    for (int i = 0; i < 60 && door->state != kDoorClosed; ++i) t.tick();
    CHECK(door->state == kDoorClosed && d->pos == 0.0f && switch_channels[7] == 0);

    // O canal da porta ligado de fora abre a porta.
    switch_channels[7] = 1;
    t.tick();
    CHECK(door->state == kDoorOpening);
}

TEST(door_groups_move_together) {
    fresh();
    framelist_tag path[2] = {frame(0, 0, 0, 0), frame(2, 0, 0, 0)};
    TestDoor a(path, 2, 2), b(path, 2, 2);
    a.level.params[1] = b.level.params[1] = 4;
    a.create();
    b.create();
    CHECK(Door_Activate(a.obj, nullptr, 0) == 1);
    for (int i = 0; i < 60 && a.obj->state != kDoorOpen; ++i) {
        Doors_Calc();
        Door_Update(a.obj);
        Door_Update(b.obj);
    }
    CHECK(a.obj->state == kDoorOpen && b.obj->state == kDoorOpen);
    CHECK(b.info()->pos == a.info()->pos);
    CHECK(DoorGroupsStates[4] == kDoorOpen);
}

TEST(doors_link_to_the_portal_they_cover) {
    fresh();
    // Duas salas ligadas por um portal em z = 10 e uma porta de girar no meio dele.
    cel_tag* b = build_alloc_cel(glb_world->first);
    cel_tag* a = build_alloc_cel(b);
    glb_world->first = a;
    a->box_min = {-10, -10, -10, 0};
    a->box_max = {10, 10, 10, 0};
    b->box_min = {-10, -10, 10, 0};
    b->box_max = {10, 10, 30, 0};
    portal_tag tmp{};
    tmp.verts[0] = {-2, -2, 10, 0};
    tmp.verts[1] = {2, -2, 10, 0};
    tmp.verts[2] = {2, 2, 10, 0};
    tmp.verts[3] = {-2, 2, 10, 0};
    tmp.plane = {0, 0, 1, -10};
    portal_tag* ab = build_alloc_portal(&tmp, nullptr, b, 1);
    tmp.plane = {0, 0, -1, 10};
    portal_tag* ba = build_alloc_portal(&tmp, nullptr, a, 1);
    ab->opposite = ba;
    ba->opposite = ab;
    a->portals = ab;
    b->portals = ba;

    framelist_tag path[3] = {frame(0, 0, 10, -kPi * 0.5f), frame(0, 0, 10, 0), frame(0, 0, 10, kPi * 0.5f)};
    TestDoor t(path, 3, 0);
    t.level.params[4] = 1;  // liga ao portal
    obj_tag* door = t.create();
    CHECK(Door_Link2Portal(door) == 1);
    control_link_object_to_cel(door, a);
    CHECK_NEAR(door->sphere.z, 10.0, 1e-5);
    build_LinkDoors2Portals(glb_world->first, control_first_object());
    CHECK((door->flags & 0x8000) != 0);
    CHECK(ab->unknown_0c == uint32_t(reinterpret_cast<uintptr_t>(door)));
    CHECK(ba->unknown_0c == ab->unknown_0c);
    CHECK(Door_IsClosed(reinterpret_cast<obj_tag*>(uintptr_t(ab->unknown_0c))));

    // Sem a flag de portal, nada muda.
    fresh();
    TestDoor u(path, 3, 0);
    obj_tag* plain = u.create();
    CHECK(Door_Link2Portal(plain) == 0);
}
