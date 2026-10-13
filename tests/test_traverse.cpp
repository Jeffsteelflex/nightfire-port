// Testes da travessia (engine/player_traverse.h): escada, tirolesa, corda, gancho, ícones de toque e a câmera de
// acompanhamento, com um jogador num mundo sem geometria (como test_player.cpp).


#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/anim.h"
#include "platform/keys.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_traverse.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "test_framework.h"

namespace {

constexpr float kPi = 3.1415927f;

void key(int vk, bool down) {
    g_psi_keyboard_mouse.active = true;
    g_psi_keyboard_mouse.down[vk] = down;
}

// Arquivo de sequência (como em test_weapon.cpp): cabeçalho de 0x40 bytes e um registro por canal com um só trecho
// de "frames" quadros, todos os coeficientes nulos.
std::vector<uint32_t> make_seq(int16_t frames, uint8_t skeleton, int channels) {
    std::vector<uint32_t> w(0x10, 0);
    for (int c = 0; c < channels; ++c) w.push_back(4u | (uint32_t(frames) << 6) << 16);
    auto* hdr = reinterpret_cast<uint8_t*>(w.data());
    std::memcpy(hdr + 0x38, &frames, 2);
    const int16_t n = int16_t(channels);
    std::memcpy(hdr + 0x3a, &n, 2);
    hdr[0x3c] = skeleton;
    return w;
}

// Os scripts do andar vazios; os da corda e da tirolesa com 30 quadros de uma sequência parada, que terminam (só
// scripts com sequência andam).
struct TraverseSkin {
    alignas(16) uint8_t skel[0x20] = {};
    uint8_t parents[1] = {0xff};
    ANIMSKIN_tag skin{};
    alignas(4) int16_t script[4] = {};
    alignas(4) uint16_t played[8] = {0, 0, 30, 1, 0, 1, 30, 0x0c00};
    std::vector<uint32_t> seq = make_seq(30, 0, 3);
    TraverseSkin() {
        auto* k = reinterpret_cast<ANIMSKEL_tag*>(skel);
        k->id = 0;
        k->bone_count = 1;
        pSkeletons[0] = k;
        skin.parents = parents;
        skin.skeleton = 0;
        skin.scale[0] = skin.scale[1] = skin.scale[2] = 1.0f;
        hashtable_additem(0x5000007, &skin);
        script[2] = 30;
        for (uint32_t h : {0x60001dcu, 0x60001ddu, 0x60009ffu, 0x6000a00u}) hashtable_additem(h, script);
        hashtable_additem(0x4000c00, seq.data());
        AnimProcessSeqData(0x4000c00);
        for (uint32_t h : {0x60005e5u, 0x60005eau, 0x60005ecu, 0x60005edu, 0x60005eeu, 0x60000b4u, 0x60000b5u,
                           0x6000094u, 0x6000095u, 0x6000096u}) {
            hashtable_additem(h, played);
        }
    }
};

// Um jogador num mundo vazio (nada embaixo: sem escada ele cai).
struct World {
    obj_tag* player = nullptr;
    BLData* b = nullptr;
    level_tag level{};
    World() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        object_debug_power_on();
        collide_debug_power_on();
        Coll_ResetHitHeap();
        view_debug_power_on();
        player_debug_power_on();
        traverse_debug_power_on();
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        std::memset(glb_players, 0, sizeof(glb_players));
        std::memset(switch_channels, 0, sizeof(switch_channels));
        hashtable_debug_power_on();
        hashtable_initialise();
        anim_debug_power_on();
        static TraverseSkin* skin = nullptr;
        delete skin;
        skin = new TraverseSkin;
        std::memset(&GameState, 0, sizeof(GameState));
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one = 1;
        std::memcpy(MPSettings + 0x1ac, &one, 4);
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        Input_Init();
        GS_SetRefreshRate(60, 60);
        Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        Mission_SetStatus(kMissionRunning);
        _VECTOR pos{0.0f, 2.5f, 0.0f, 1.0f};
        _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
        player = Player_Init(0, &pos, &rot, &level);
        b = player != nullptr ? bloke_of(player) : nullptr;
        if (player != nullptr && anim_of(player) != nullptr) {
            anim_of(player)->root_offset = 1.1f;
            anim_of(player)->root_height = 1.1f;
        }
        // O primeiro Input_Update só libera as ações.
        Input_Update();
    }
    void tick() {
        // A matriz pela posição e o giro, como o laço dos objetos faz antes de cada atualização.
        Control_GetObjMatrix(player, &player->matrix);
        Input_Update();
        Player_Update(player);
        Player_CollisionHandler(player);
        Camera_Update(0);
    }
    // Um objeto de travessia feito à mão: tipo, esfera no mundo, giro e dados.
    obj_tag* thing(uint8_t type, const _VECTOR& sphere, float yaw, float tilt, uint32_t data_size) {
        _VECTOR pos{sphere.x, sphere.y, sphere.z, 1.0f};
        _VECTOR rot{0.0f, yaw, tilt, 0.0f};
        obj_tag* o = control_create_object(data_size, &pos, &rot, nullptr);
        o->type = type;
        o->sphere = sphere;
        o->old_pos = o->pos;
        o->matrix.m[3][0] = pos.x;
        o->matrix.m[3][1] = pos.y;
        o->matrix.m[3][2] = pos.z;
        return o;
    }
};

float dist_to_line(const _VECTOR& p, const _VECTOR& a, const _VECTOR& b) {
    _VECTOR c;
    vecutil_calculate_closest_point_on_line(&c, &a, &b, &p);
    return Vec_Dist3D(&c, &p);
}

}  // namespace

TEST(ladder_create_keeps_parameter_and_type) {
    World w;
    level_tag level{};
    level.params[1] = 0x12345;
    _VECTOR pos{1.0f, 2.0f, 3.0f, 1.0f};
    _VECTOR rot{0.0f, 0.25f, 0.0f, 0.0f};
    obj_tag* l = Ladder_Create(&pos, &rot, &level, nullptr);
    CHECK(l != nullptr && l->type == 0x2d);
    const auto* d = static_cast<const LADDER_tag*>(l->data);
    CHECK(d->kind == 0 && uint16_t(d->param1) == 0x2345);
    CHECK(l->flags == 0);  // sem as flags do modelo, como no original
}

TEST(ladder_attaches_half_metre_in_front_when_facing_it) {
    World w;
    CHECK(w.player != nullptr);
    // Escada olhando para +z (giro 0): o lado de subir é +z, o jogador olha para -z.
    obj_tag* l = w.thing(0x2d, {5.0f, 3.0f, 7.0f, 2.0f}, 0.0f, 0.0f, 4);
    w.player->rot.y = kPi;
    w.player->pos = {5.2f, 2.5f, 7.9f, 1.0f};
    Player_CollLadder(w.player, w.b, l);
    CHECK(w.player->unknown_f6 == kPlrClimb && w.b->ladder == l);
    // Topo = centro + raio + 0,85; base = centro - raio + 1.
    CHECK(std::fabs(w.b->ladder_top - 5.85f) < 1e-4f && std::fabs(w.b->ladder_bottom - 2.0f) < 1e-4f);
    // Meio metro à frente do centro, no lado para onde a escada aponta; a altura não muda.
    const float dx = w.player->pos.x - 5.0f, dz = w.player->pos.z - 7.0f;
    CHECK(std::fabs(std::sqrt(dx * dx + dz * dz) - 0.5f) < 0.01f && dz > 0.49f);
    CHECK(std::fabs(w.player->pos.y - 2.5f) < 1e-5f);
    CHECK(anim_of(w.player)->weapon == kWeaponNone && w.b->traverse_step == 1);
    CHECK(g_traverse_stats.ladders == 1);
}

TEST(ladder_refuses_backwards_above_top_and_while_waiting) {
    World w;
    obj_tag* l = w.thing(0x2d, {0.0f, 3.0f, 0.0f, 2.0f}, 0.0f, 0.0f, 4);
    // De costas (mesmo giro da escada).
    w.player->rot.y = 0.0f;
    w.player->pos = {0.0f, 2.5f, 1.0f, 1.0f};
    Player_CollLadder(w.player, w.b, l);
    CHECK(w.player->unknown_f6 == kPlrWalk);
    // Acima do topo: não prende nem desce.
    w.player->rot.y = kPi;
    w.player->pos.y = 6.0f;
    Player_CollLadder(w.player, w.b, l);
    CHECK(w.player->unknown_f6 == kPlrWalk && w.player->pos.y == 6.0f);
    // Abaixo da base: sobe para 2 cm acima dela e prende (a altura é ajustada antes do teste).
    w.player->pos.y = 0.5f;
    Player_CollLadder(w.player, w.b, l);
    CHECK(w.player->unknown_f6 == kPlrClimb && std::fabs(w.player->pos.y - 2.02f) < 1e-4f);
    // Logo depois de soltar (traverse_wait) não prende.
    Player_ChangeSubState(w.player, kPlrWalk);
    w.b->traverse_wait = 10;
    Player_CollLadder(w.player, w.b, l);
    CHECK(w.player->unknown_f6 == kPlrWalk);
}

TEST(climb_rises_two_centimetres_per_tick_and_exits_at_top) {
    World w;
    obj_tag* l = w.thing(0x2d, {0.0f, 3.0f, 0.0f, 2.0f}, 0.0f, 0.0f, 4);
    w.player->rot.y = kPi;
    w.player->pos = {0.0f, 2.5f, 1.0f, 1.0f};
    Player_CollLadder(w.player, w.b, l);
    CHECK(w.player->unknown_f6 == kPlrClimb);
    const float y0 = w.player->pos.y;
    key('W', true);
    for (int t = 0; t < 30; ++t) w.tick();
    // O analógico para a frente inteiro sobe FRAME_RATE_MUL x 0,02 m por tick (60 Hz: 0,02).
    const float rise = w.player->pos.y - y0;
    CHECK(w.player->unknown_f6 == kPlrClimb);
    CHECK(std::fabs(rise - 30 * 0.02f) < 0.03f);
    // A escada não deixa o jogador cair (a queda é zerada a cada tick).
    CHECK(w.b->fall.y == 0.0f);
    // Até passar do topo (5,85): sai para a frente, um segundo sem prender de novo, a arma volta.
    for (int t = 0; t < 400 && w.player->unknown_f6 == kPlrClimb; ++t) w.tick();
    CHECK(w.player->unknown_f6 == kPlrWalk);
    CHECK(w.player->pos.y > w.b->ladder_top);
    CHECK(w.b->traverse_wait > 0);
    CHECK(g_traverse_stats.ladder_exits == 1);
    key('W', false);
}

TEST(climb_jump_without_forward_lets_go_backwards) {
    World w;
    obj_tag* l = w.thing(0x2d, {0.0f, 3.0f, 0.0f, 2.0f}, 0.0f, 0.0f, 4);
    w.player->rot.y = kPi;
    w.player->pos = {0.0f, 3.0f, 1.0f, 1.0f};
    Player_CollLadder(w.player, w.b, l);
    key(nf::key::Space, true);
    // Player_Climb sozinho: pular com o analógico parado solta, com a velocidade para trás (-0,2 / FRAME_RATE_DIV / 4).
    w.tick();
    CHECK(w.player->unknown_f6 == kPlrWalk);
    CHECK(w.b->traverse_wait == int8_t(FRAME_RATE));
    key(nf::key::Space, false);
}

TEST(zipline_grabs_upper_half_and_slides_down_the_wire) {
    World w;
    // Tirolesa de 24 m (raio 12) ao longo de x, inclinada (a ponta de x negativo é a de baixo). Player_CollWire usa as
    // pontas 2 m para dentro; Player_Zipline, as de verdade.
    obj_tag* z = w.thing(0x2c, {0.0f, 10.0f, 0.0f, 12.0f}, 0.0f, 0.0f, 8);
    z->rot.z = 0.2f;
    auto* d = static_cast<WIRE_tag*>(z->data);
    d->kind = 2;
    d->channel = 0x40;
    _VECTOR off;
    Vec_Spherical_2_Cartesian(&off, 10.0f, z->rot.y + 1.5707964f, z->rot.z);
    const _VECTOR e1{z->sphere.x + off.x, z->sphere.y + off.y, z->sphere.z + off.z, 1.0f};
    const _VECTOR e2{z->sphere.x - off.x, z->sphere.y - off.y, z->sphere.z - off.z, 1.0f};
    const _VECTOR& hi = e1.y > e2.y ? e1 : e2;
    const _VECTOR& lo = e1.y > e2.y ? e2 : e1;
    _VECTOR full;
    Vec_Spherical_2_Cartesian(&full, 12.0f, z->rot.y + 1.5707964f, z->rot.z);
    const _VECTOR f1{z->sphere.x + full.x, z->sphere.y + full.y, z->sphere.z + full.z, 1.0f};
    const _VECTOR f2{z->sphere.x - full.x, z->sphere.y - full.y, z->sphere.z - full.z, 1.0f};
    // Na metade de baixo não pega.
    w.player->pos = {lo.x + (hi.x - lo.x) * 0.2f, lo.y - 1.0f, lo.z + (hi.z - lo.z) * 0.2f, 1.0f};
    Player_CollWire(w.player, w.b, z);
    CHECK(w.player->unknown_f6 == kPlrWalk);
    // Na de cima pega: 1,17363 m abaixo do fio, virado para a ponta de baixo.
    w.player->pos = {hi.x + (lo.x - hi.x) * 0.2f, hi.y - 1.0f, hi.z + (lo.z - hi.z) * 0.2f, 1.0f};
    Player_CollWire(w.player, w.b, z);
    CHECK(w.player->unknown_f6 == kPlrZipline && w.b->ladder == z);
    _VECTOR hand = w.player->pos;
    hand.y += 1.17363f;
    // O corpo sai 8,2 cm do fio (Player_CollWire recua 0,082 em x, girado pelo jogador, também na tirolesa).
    CHECK(dist_to_line(hand, e1, e2) < 0.09f);
    const float y0 = w.player->pos.y;
    bool on_line = true, channel = false;
    for (int t = 0; t < 600 && w.player->unknown_f6 == kPlrZipline; ++t) {
        w.tick();
        if (w.player->unknown_f6 != kPlrZipline) break;
        channel = channel || switch_channels[0x40] != 0;
        hand = w.player->pos;
        hand.y += 1.17363f;
        // Depois do tick o corpo andou 0,15 m para a frente desde o ponto do fio em que foi posto.
        on_line = on_line && dist_to_line(hand, f1, f2) < 0.5f;
    }
    CHECK(on_line && channel);
    CHECK(w.player->pos.y < y0 - 1.0f);
    // Chegou perto da ponta de baixo e soltou (a animação de soltar acabou); o canal desliga.
    CHECK(w.player->unknown_f6 == kPlrWalk);
    CHECK(Vec_Dist3D(&w.player->pos, &lo) < 3.0f);
    CHECK(switch_channels[0x40] == 0);
}

TEST(rope_hangs_moves_sideways_and_tires_out) {
    World w;
    obj_tag* r = w.thing(0x2c, {0.0f, 5.0f, 0.0f, 6.0f}, 0.0f, 0.0f, 8);
    static_cast<WIRE_tag*>(r->data)->kind = 0;
    w.player->rot.y = kPi * 0.5f;
    w.player->pos = {0.0f, 4.0f, 0.0f, 1.0f};
    Player_CollWire(w.player, w.b, r);
    CHECK(w.player->unknown_f6 == kPlrWire && w.b->traverse_step == 0xc);
    // Pendurado: sem cair. Depois de subir na corda (passo 0xc -> 6) o cansaço sobe 100 / 30 s.
    for (int t = 0; t < 60; ++t) w.tick();
    CHECK(w.player->unknown_f6 == kPlrWire && w.b->traverse_step != 0xc);
    const float tired = w.b->unknown_8cc;
    for (int t = 0; t < 60; ++t) w.tick();
    CHECK(std::fabs((w.b->unknown_8cc - tired) - 60 * 100.0f / (60.0f * 30.0f)) < 0.01f);
    // De lado (D): o passo vira 7 ou 8 (o quanto anda vem da raiz das animações, vazias aqui) e o corpo continua
    // pendurado 1,0207 m abaixo do fio, 5,42 cm para um lado ou outro do ponto mais perto.
    key('D', true);
    bool sideways = false;
    for (int t = 0; t < 120; ++t) {
        w.tick();
        sideways = sideways || w.b->traverse_step == 7 || w.b->traverse_step == 8;
    }
    key('D', false);
    CHECK(sideways);
    _VECTOR hand = w.player->pos;
    hand.y += 1.0207f;
    _VECTOR off;
    Vec_Spherical_2_Cartesian(&off, r->sphere.w - 0.5f, r->rot.y + 1.5707964f, 0.0f);
    const _VECTOR e1{r->sphere.x + off.x, r->sphere.y, r->sphere.z + off.z, 1.0f};
    const _VECTOR e2{r->sphere.x - off.x, r->sphere.y, r->sphere.z - off.z, 1.0f};
    CHECK(dist_to_line(hand, e1, e2) < 0.1f);
    // Pular solta.
    key(nf::key::Space, true);
    w.tick();
    key(nf::key::Space, false);
    CHECK(w.player->unknown_f6 == kPlrWalk && w.b->traverse_wait == 0x1e);
}

TEST(third_icon_gives_touch_type_to_the_player_that_touches_it) {
    World w;
    level_tag level{};
    level.params[0] = 2;     // gancho
    level.params[1] = 0x30;  // liga com o canal 0x30
    level.params[2] = 0x31;  // some com o 0x31
    _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f};
    _VECTOR rot{};
    obj_tag* icon = ThirdIcon_Create(&pos, &rot, &level, nullptr);
    CHECK(icon != nullptr && icon->type == 0x42 && icon->state == 0 && (icon->flags & 0x1000) != 0);
    Coll_AddHitToList(&icon->hits, 1.0f, nullptr, w.player);
    w.b->touch_type = 0xff;
    ThirdIcon_Update(icon);
    CHECK(w.b->touch_type == 0xff);  // desligado
    switch_channels[0x30] = 1;
    ThirdIcon_Update(icon);
    CHECK(w.b->touch_type == 2 && icon->state == 1);
    switch_channels[0x31] = 1;
    ThirdIcon_Update(icon);
    CHECK((icon->life & 1) != 0);
}

TEST(grapple_pulls_faster_each_tick_and_holds_while_firing) {
    World w;
    w.player->pos = {0.0f, 2.0f, 0.0f, 1.0f};
    _VECTOR point{0.0f, 12.0f, 0.0f, 1.0f};
    obj_tag* target = w.thing(0x51, {0.0f, 12.0f, 0.0f, 0.5f}, 0.0f, 0.0f, 0);
    Player_SetGrapplePoint(w.b, &point, target);
    CHECK(w.player->unknown_f6 == kPlrGrapple && w.b->unknown_15c == 1 && w.b->grapple_wait == 0xf);
    key(nf::key::LButton, true);
    float last = Vec_Dist3D(&w.player->pos, &point);
    float speed = 0.0f;
    bool faster = true;
    for (int t = 0; t < 120 && w.b->unknown_15c == 1; ++t) {
        w.tick();
        const float d = Vec_Dist3D(&w.player->pos, &point);
        // A velocidade cresce 0,05 por tick até 1,5 m por tick.
        if (w.b->unknown_15c == 1) {
            faster = faster && w.b->grapple_speed >= speed && w.b->grapple_speed <= 1.5f;
            speed = w.b->grapple_speed;
            CHECK(d < last);
        }
        last = d;
    }
    CHECK(faster);
    CHECK(w.b->unknown_15c == 2 && last < 2.0f);
    // Segurando o gatilho fica pendurado; soltando, volta a andar.
    for (int t = 0; t < 10; ++t) w.tick();
    CHECK(w.b->unknown_15c == 2);
    key(nf::key::LButton, false);
    for (int t = 0; t < 3; ++t) w.tick();
    CHECK(w.player->unknown_f6 == kPlrWalk && w.b->unknown_15c == 0);
    CHECK(g_traverse_stats.grapples == 1);
}

TEST(camera_tracking_keeps_distance_from_the_wire) {
    World w;
    obj_tag* r = w.thing(0x2c, {0.0f, 5.0f, 0.0f, 6.0f}, 0.0f, 0.0f, 8);
    static_cast<WIRE_tag*>(r->data)->kind = 0;
    w.player->rot.y = kPi * 0.5f;
    w.player->pos = {0.0f, 4.0f, 0.0f, 1.0f};
    Player_CollWire(w.player, w.b, r);
    CHECK(w.b->cam_mode == 0xb);
    viewer_tag* v = glb_viewer[0];
    camera_tracking(w.player, v);
    // As pontas da conta da câmera (1 m abaixo do fio) e o ponto mais perto do jogador.
    _VECTOR off;
    Vec_Spherical_2_Cartesian(&off, r->sphere.w, r->rot.y + 1.5707964f, r->rot.z);
    const _VECTOR e1{r->pos.x + off.x, r->pos.y + off.y - 1.0f, r->pos.z + off.z, 1.0f};
    const _VECTOR e2{r->pos.x - off.x, r->pos.y - off.y - 1.0f, r->pos.z - off.z, 1.0f};
    _VECTOR at;
    vecutil_calculate_closest_point_on_line(&at, &e1, &e2, &w.player->pos);
    CHECK(std::fabs(Vec_Dist3D(&v->pos, &at) - 4.0f) < 0.01f);
    // A câmera aponta para o ponto: a linha 2 da matriz (frente) vai da câmera a ele.
    _VECTOR to{at.x - v->pos.x, at.y - v->pos.y, at.z - v->pos.z, 0.0f};
    Vec_Normalise(&to, &to);
    const float dot = to.x * v->camera.m[2][0] + to.y * v->camera.m[2][1] + to.z * v->camera.m[2][2];
    CHECK(dot > 0.99f);
    // Sem fio: a câmera volta ao modo do jogador (unknown_24 = 0) e nada mais muda.
    w.b->ladder = nullptr;
    v->unknown_24 = 5;
    camera_tracking(w.player, v);
    CHECK(v->unknown_24 == 0);
}

TEST(third_camera_index_out_of_table_is_ignored) {
    World w;
    level_tag level{};
    level.params[0] = 20;
    level.path_count = 2;
    _VECTOR pos{}, rot{};
    ThirdCam_Create(&pos, &rot, &level, nullptr);
    CHECK(g_traverse_stats.guards == 1);
    level.params[0] = 3;
    level.params[1] = 1;
    ThirdCam_Create(&pos, &rot, &level, nullptr);
    CHECK(ThirdPersonCameras[3] != nullptr);
    const auto* d = static_cast<const THIRDCAM_tag*>(ThirdPersonCameras[3]->data);
    CHECK(d->index == 3 && d->frames == 1 && d->aim == 1);
}
