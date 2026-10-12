// Testes do módulo de visão (src/engine/view.cpp) e do registro de desenho (platform/psi_draw.cpp).

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "test_framework.h"

namespace {

void fresh() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    view_debug_power_on();
    psi_draw_debug_power_on();
    glb_world = build_alloc_world();
}

_VECTOR apply(const MATRIX& m, const _VECTOR& p) {
    _VECTOR o;
    o.x = p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0];
    o.y = p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1];
    o.z = p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2];
    o.w = 0.0f;
    return o;
}

// Duas salas em fila no eixo z: A (z de -10 a 10) e B (z de 10 a 30), ligadas por um portal
// quadrado de 4 x 4 em z = 10. A câmera 0 fica na origem, dentro de A.
struct TwoRooms {
    uint8_t gfx_block[16]{};
    entgfx_tag gfx{};
    celglist_tag glist_a{}, glist_b{}, glist_obj{};
    cel_tag* a = nullptr;
    cel_tag* b = nullptr;
    portal_tag* ab = nullptr;
    portal_tag* ba = nullptr;
    viewer_tag* v = nullptr;

    TwoRooms() {
        gfx.ps2_gfx = gfx_block;  // psiDrawObjectMatrix só desenha entidades com bloco 0x2d
        for (celglist_tag* g : {&glist_a, &glist_b, &glist_obj}) g->gfx = &gfx;
        b = build_alloc_cel(glb_world->first);
        a = build_alloc_cel(b);
        glb_world->first = a;
        a->type = 0xc023;
        b->type = 0x1000c023;  // bit 28: sala fechada, não mostra o céu
        a->glist = &glist_a;
        b->glist = &glist_b;
        a->box_min = {-10, -10, -10, 0};
        a->box_max = {10, 10, 10, 0};
        b->box_min = {-10, -10, 10, 0};
        b->box_max = {10, 10, 30, 0};
        b->pos = {0, 0, 20, 0};
        portal_tag tmp{};
        tmp.verts[0] = {-2, -2, 10, 0};
        tmp.verts[1] = {2, -2, 10, 0};
        tmp.verts[2] = {2, 2, 10, 0};
        tmp.verts[3] = {-2, 2, 10, 0};
        tmp.plane = {0, 0, 1, -10};  // normal para dentro de B: a câmera em A fica do lado negativo
        ab = build_alloc_portal(&tmp, nullptr, b, 1);
        tmp.plane = {0, 0, -1, 10};
        ba = build_alloc_portal(&tmp, nullptr, a, 1);
        ab->opposite = ba;
        ba->opposite = ab;
        a->portals = ab;
        b->portals = ba;

        Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
        Camera_Enable(0, 1, 1, nullptr);  // como o Player_Init
        v = glb_viewer[0];
        look(1.0f);
        v->cel = a;
    }
    // Câmera na origem olhando para +z (dir = 1) ou -z (dir = -1), com determinante +1.
    void look(float dir) {
        v->camera = {{{dir, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, dir, 0}, {0, 0, 0, 1}}};
        Camera_Update(0);
        v->prev_pos = v->pos;
    }
};

}  // namespace

TEST(view_math_helpers) {
    // Concat(a, b) aplica b e depois a (vetor-linha) e preserva a coluna w de b.
    MATRIX a, b;
    const _VECTOR ea{0.3f, -1.1f, 0.7f, 0}, ta{1, 2, 3, 0}, eb{-0.4f, 0.2f, 2.0f, 0}, tb{-5, 4, 0.5f, 0};
    RotTransMatrix(&ea, &ta, &a);
    RotTransMatrix(&eb, &tb, &b);
    b.m[0][3] = 7.0f;
    MATRIX c = b;
    Concat(&a, &c);
    const _VECTOR p{0.5f, -2, 3, 0};
    const _VECTOR q1 = apply(a, apply(b, p)), q2 = apply(c, p);
    CHECK_NEAR(q1.x, q2.x, 1e-4);
    CHECK_NEAR(q1.y, q2.y, 1e-4);
    CHECK_NEAR(q1.z, q2.z, 1e-4);
    CHECK(c.m[0][3] == 7.0f);

    // ApplyMatrixLVI: só a rotação.
    _VECTOR r = p;
    ApplyMatrixLVI(&a, &r);
    const _VECTOR q3 = apply(a, p);
    CHECK_NEAR(r.x, q3.x - ta.x, 1e-4);
    CHECK_NEAR(r.z, q3.z - ta.z, 1e-4);

    CHECK_NEAR(LimitRadAngle(7.0f), 7.0 - 2 * 3.14159265, 1e-5);
    CHECK_NEAR(LimitRadAngle(-4.0f), -4.0 + 2 * 3.14159265, 1e-5);
    CHECK(LimitRadAngle(1.0f) == 1.0f);

    // Os três geradores avançam as mesmas sementes; FRand(1) é Random() / 2^32.
    view_debug_power_on();
    const uint32_t first = Rand_Random();
    view_debug_power_on();
    CHECK_NEAR(Rand_FRand(1.0f), double(first) / 4294967296.0, 1e-6);
    bool in_range = true;
    for (int i = 0; i < 1000; ++i) in_range = in_range && Rand_Rand(10) < 10;
    CHECK(in_range);
    CHECK(Rand_Rand(0) == 0);
}

TEST(view_clip_portal) {
    _PORTAL in{}, out{};
    in.count = 4;
    in.v[0] = {-1, -1, 0, 0};
    in.v[1] = {1, -1, 0, 0};
    in.v[2] = {1, 1, 0, 0};
    in.v[3] = {-1, 1, 0, 0};
    const plane_equ_tag keep_right{1, 0, 0, 0};  // x >= 0
    CHECK(ClipPortalWithPlane(&keep_right, &in, &out) == 1);
    CHECK(out.count == 4);
    int on_cut = 0;
    for (int i = 0; i < out.count; ++i) {
        CHECK(out.v[i].x >= -1e-6f);
        if (std::fabs(out.v[i].x) < 1e-6f) ++on_cut;
    }
    CHECK(on_cut == 2);
    const plane_equ_tag keep_all{1, 0, 0, 5};  // x >= -5: nada sai
    CHECK(ClipPortalWithPlane(&keep_all, &in, &out) == 0 && out.count == 0);
    const plane_equ_tag keep_none{1, 0, 0, -5};  // x >= 5: tudo sai
    CHECK(ClipPortalWithPlane(&keep_none, &in, &out) == 1 && out.count == 0);
}

TEST(view_frustum_and_in_view) {
    fresh();
    TwoRooms w;
    Vision_Calculate_View_Fustrum(w.v);
    const _VECTOR ahead{0, 0, 10, 0}, behind{0, 0, -10, 0}, right{100, 0, 10, 0}, near_apex{0, 0, -0.5f, 0};
    CHECK(Vision_InView(&ahead, 0.0f, w.v) == 1);
    CHECK(Vision_InView(&behind, 0.0f, w.v) == 0);
    CHECK(Vision_InView(&right, 0.0f, w.v) == 0);
    CHECK(Vision_InView(&right, 200.0f, w.v) == 1);
    CHECK(Vision_InView(&near_apex, 1.0f, w.v) == 1);
    // Billboard: a câmera com a linha 0 negada; pos: a linha 3.
    CHECK(w.v->billboard.m[0][0] == -1.0f && w.v->billboard.m[2][2] == 1.0f);
    CHECK(w.v->pos.x == 0.0f && w.v->pos.z == 0.0f);
}

TEST(view_portals_decide_visible_rooms) {
    fresh();
    TwoRooms w;
    View_CaptureScene(w.v);
    CHECK(w.v->counts[0] == 2);
    CHECK(w.v->lists[0][0].cel == w.a && w.v->lists[0][0].key == -90000.0f);
    CHECK(w.v->lists[0][1].cel == w.b && w.v->lists[0][1].key == -89990.0f);
    CHECK(w.v->sky_visible == 1);  // só a sala A deixa ver o céu
    CHECK(w.ab->unknown_26 == 1 && w.ba->unknown_26 == 1);
    CHECK(w.a->vis_next == w.b && w.b->vis_next == nullptr);

    // De costas para o portal: só a sala da câmera.
    w.look(-1.0f);
    View_CaptureScene(w.v);
    CHECK(w.v->counts[0] == 1 && w.v->lists[0][0].cel == w.a);

    // Porta fechada no portal.
    w.look(1.0f);
    alignas(16) static uint8_t door_mem[0x110 + 0x70];
    std::memset(door_mem, 0, sizeof(door_mem));
    auto* door = reinterpret_cast<obj_tag*>(door_mem);
    door->data = door_mem + 0x110;
    door->state = 2;
    w.ab->unknown_0c = uint32_t(reinterpret_cast<uintptr_t>(door));
    View_CaptureScene(w.v);
    CHECK(w.v->counts[0] == 1);
    door->state = 0;
    View_CaptureScene(w.v);
    CHECK(w.v->counts[0] == 2);

    // Grupo de portas e porta trancada.
    const uint16_t group = 3;
    std::memcpy(door_mem + 0x110 + 0x6a, &group, 2);
    CHECK(!Door_IsClosed(door));
    DoorGroupsStates[3] = 2;
    CHECK(Door_IsClosed(door));
    DoorGroupsStates[3] = 0;
    door->unknown_f6 = 1;
    CHECK(Door_IsClosed(door));
    CHECK(!Door_IsClosed(nullptr));
}

TEST(view_camera_follows_portals) {
    fresh();
    TwoRooms w;
    // Andando de A para B através do portal (longe da diagonal que divide o quadrilátero em dois
    // triângulos): Camera_CheckLocation troca a sala.
    w.v->camera.m[3][0] = 1.0f;
    w.v->camera.m[3][1] = -0.5f;
    w.v->camera.m[3][2] = 15.0f;
    Camera_UpdateAll();
    CHECK(w.v->cel == w.b);
    CHECK(w.v->prev_pos.z == 15.0f);
    // Dentro da caixa de B, sem cruzar portal: continua em B.
    w.v->camera.m[3][2] = 20.0f;
    Camera_UpdateAll();
    CHECK(w.v->cel == w.b && w.v->delta.z == 5.0f);
}

TEST(view_objects_and_game_draw) {
    fresh();
    TwoRooms w;
    // Um objeto em A à frente da câmera e outro atrás.
    alignas(16) static obj_tag front, back;
    std::memset(&front, 0, sizeof(front));
    std::memset(&back, 0, sizeof(back));
    for (obj_tag* o : {&front, &back}) {
        o->glist = &w.glist_obj;
        o->unknown_f8 = 1;  // visível na câmera 0
        o->scale = 1.0f;
        Mat_IdentityT(&o->matrix);
    }
    front.sphere = {0, 0, 5, 1};
    back.sphere = {0, 0, -5, 1};
    front.cel_next = &back;
    w.a->objects = &front;
    View_CaptureScene(w.v);
    CHECK(w.v->counts[0] == 3);
    CHECK(w.v->lists[0][1].obj == &front && w.v->lists[0][1].key == 25.0f);
    CHECK(front.unknown_fc == 2 && back.unknown_fc == 0);

    // Game_Draw registra salas e objeto na ordem da chave, todos na câmera 0, com a coluna w
    // corrigida.
    const uint32_t frames = GameState.draws;
    Game_Draw();
    CHECK(GameState.draws == frames + 1);
    CHECK(g_psi_frame.finished);
    CHECK(g_psi_frame.draws.size() == 3);
    if (g_psi_frame.draws.size() == 3) {
        CHECK(g_psi_frame.draws[0].glist == &w.glist_a);
        CHECK(g_psi_frame.draws[1].glist == &w.glist_b);
        CHECK(g_psi_frame.draws[2].glist == &w.glist_obj);
        CHECK(g_psi_frame.draws[1].world.m[3][2] == 20.0f && g_psi_frame.draws[1].world.m[3][3] == 1.0f);
        for (const PsiDrawCall& d : g_psi_frame.draws) CHECK(d.camera == w.v && d.blend == 2 && d.alpha_pass == 0);
    }

    // Objeto transparente (flag 1): lista 1, entre psiStartAlpha e psiEndAlpha, depois do opaco.
    front.flags = 1;
    Game_Draw();
    CHECK(w.v->counts[1] == 1 && w.v->lists[1][0].key == -25.0f);
    CHECK(g_psi_frame.draws.size() == 3 && g_psi_frame.draws.back().glist == &w.glist_obj);
    if (!g_psi_frame.draws.empty()) {
        CHECK(g_psi_frame.draws.back().alpha_pass == 1 && g_psi_frame.draws.back().blend == 1);
        CHECK(g_psi_frame.draws.back().after_solid == 1);
    }
}

TEST(view_draw_owner_identity) {
    // Só do port: cada desenho registrado leva o dono (a sala, o objeto, o anexo), a lista do View_DrawObjList e a
    // ordem dentro do dono; dois Game_Draw seguidos dão os mesmos donos (a identidade da interpolação entre ticks).
    fresh();
    TwoRooms w;
    alignas(16) static obj_tag front;
    std::memset(&front, 0, sizeof(front));
    front.glist = &w.glist_obj;
    front.unknown_f8 = 1;
    front.scale = 1.0f;
    front.created = 33;
    Mat_IdentityT(&front.matrix);
    front.sphere = {0, 0, 5, 1};
    w.a->objects = &front;
    // Uma marca de tiro presa ao objeto: o dono do desenho dela é o anexo, e o objeto volta a ser o dono depois.
    celglist_tag mark{};
    mark.gfx = &w.gfx;
    alignas(16) static ATTACH_tag hole;
    std::memset(&hole, 0, sizeof(hole));
    hole.glist = &mark;
    Mat_IdentityT(&hole.matrix);
    front.attach = &hole;
    Game_Draw();
    CHECK(g_psi_frame.draws.size() == 4);
    if (g_psi_frame.draws.size() != 4) return;
    const PsiDrawCall* d = g_psi_frame.draws.data();
    CHECK(d[0].owner == w.a && d[0].owner_kind == PsiOwnerKind::Cel && d[0].list == 0 && d[0].owner_seq == 0);
    CHECK(d[1].owner == w.b && d[1].owner_kind == PsiOwnerKind::Cel && d[1].owner_seq == 0);
    CHECK(d[2].owner == &front && d[2].owner_kind == PsiOwnerKind::Object && d[2].created == 33);
    CHECK(d[3].owner == &hole && d[3].owner_kind == PsiOwnerKind::Attach && d[3].list == 0 && d[3].glist == &mark);
    CHECK(psi_draw_owner() == nullptr && psi_draw_list() == -1);  // fora do View_DrawObjList, ninguém
    std::vector<const void*> first;
    for (const PsiDrawCall& c : g_psi_frame.draws) first.push_back(c.owner);
    Game_Draw();
    CHECK(g_psi_frame.draws.size() == first.size());
    for (size_t i = 0; i < first.size() && i < g_psi_frame.draws.size(); ++i) CHECK(g_psi_frame.draws[i].owner == first[i]);
    // A ordem dentro do dono: um dono novo recomeça em 0, o mesmo dono conta.
    psi_draw_set_owner(&front, PsiOwnerKind::Object, 1);
    MATRIX m;
    Mat_IdentityT(&m);
    psiDrawObjectMatrix(&w.glist_obj, &m, 0);
    psiDrawObjectMatrix(&w.glist_obj, &m, 0);
    psi_draw_set_owner(w.a, PsiOwnerKind::Cel, 0);
    psiDrawObjectMatrix(&w.glist_a, &m, 0);
    const size_t n = g_psi_frame.draws.size();
    CHECK(g_psi_frame.draws[n - 3].owner_seq == 0 && g_psi_frame.draws[n - 2].owner_seq == 1);
    CHECK(g_psi_frame.draws[n - 2].list == 1 && g_psi_frame.draws[n - 1].owner_seq == 0);
    psi_draw_set_owner(nullptr, PsiOwnerKind::Object, 0);
    CHECK(psi_draw_owner_kind() == PsiOwnerKind::None);
    front.attach = nullptr;
}

TEST(view_list2_drawn_after_z_clear) {
    // A lista 2 (flag 0x2000: a arma em primeira pessoa, o clarão do tiro) vem depois dos opacos, da névoa e dos
    // transparentes, e o Game_Draw limpa o Z logo antes dela (psiClearZ com a câmera 5 em uso, como no PS2): o
    // registro marca a limpeza no índice do primeiro desenho da lista 2. Sem nada na lista 2, só a limpeza do
    // psiPreDraw (índice 0); com WipeZBuffer desligado, nenhuma.
    fresh();
    TwoRooms w;
    alignas(16) static obj_tag crate, glass, gun;
    for (obj_tag* o : {&crate, &glass, &gun}) {
        std::memset(o, 0, sizeof(*o));
        o->glist = &w.glist_obj;
        o->unknown_f8 = 1;
        o->scale = 1.0f;
        Mat_IdentityT(&o->matrix);
    }
    crate.sphere = {0, 0, 5, 1};
    glass.sphere = {0, 0, 6, 1};
    glass.flags = 1;
    gun.sphere = {0, 0, 0.5f, 0.4f};
    gun.flags = 0x2000;
    crate.cel_next = &glass;
    glass.cel_next = &gun;
    w.a->objects = &crate;
    FogOn = 1;
    Game_Draw();
    FogOn = 0;
    CHECK(w.v->counts[0] == 3 && w.v->counts[1] == 1 && w.v->counts[2] == 1);
    const std::vector<PsiDrawCall>& d = g_psi_frame.draws;
    CHECK(d.size() == 5);
    if (d.size() != 5) return;
    CHECK(d[2].owner == &crate && d[2].list == 0);
    CHECK(d[3].owner == &glass && d[3].list == 1);
    CHECK(d[4].owner == &gun && d[4].list == 2 && d[4].camera == w.v);
    CHECK(g_psi_frame.fog_at == 3);  // depois dos opacos
    CHECK(g_psi_frame.clear_z == 2);
    CHECK(g_psi_frame.clear_z_at.size() == 2);
    if (g_psi_frame.clear_z_at.size() == 2) {
        CHECK(g_psi_frame.clear_z_at[0] == 0);  // psiPreDraw
        CHECK(g_psi_frame.clear_z_at[1] == 4);  // antes da arma, depois do vidro
    }
    // Sem nada na lista 2: só a limpeza do começo do quadro.
    glass.cel_next = nullptr;
    Game_Draw();
    CHECK(g_psi_frame.draws.size() == 4 && g_psi_frame.clear_z_at.size() == 1 && g_psi_frame.clear_z_at[0] == 0);
    // WipeZBuffer desligado (no ELF nada o desliga): psiClearZ não manda nada.
    glass.cel_next = &gun;
    WipeZBuffer = 0;
    Game_Draw();
    WipeZBuffer = 1;
    CHECK(g_psi_frame.draws.size() == 5 && g_psi_frame.clear_z == 0 && g_psi_frame.clear_z_at.empty());
    w.a->objects = nullptr;
}

TEST(view_sky_follows_camera) {
    fresh();
    TwoRooms w;
    celglist_tag sky{};
    sky.gfx = &w.gfx;
    sky.flags = 2;  // acompanha a câmera
    const _VECTOR far_pos{500, 0, 500, 0};
    View_AddSkyObj(0, &sky, &far_pos, nullptr, nullptr, 0, 0, 0, 1);
    w.v->camera.m[3][0] = 3.0f;
    Camera_UpdateAll();
    Game_Draw();
    bool found = false;
    for (const PsiDrawCall& d : g_psi_frame.draws) {
        if (d.glist != &sky) continue;
        found = true;
        CHECK(d.world.m[3][0] == 3.0f && d.world.m[3][2] == 0.0f);
    }
    CHECK(found);
    // Sem nenhuma sala visível que mostre o céu, ele não é desenhado.
    w.a->type = 0x1000c023;
    Game_Draw();
    for (const PsiDrawCall& d : g_psi_frame.draws) CHECK(d.glist != &sky);
}
