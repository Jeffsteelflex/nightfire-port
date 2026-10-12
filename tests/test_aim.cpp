// Testes do polimento das armas: a mira do tronco (AnimObjectAimAt, AnimGetBoneWorldTrans2), a mira automática
// (Check_AutoAim), o raio laser, o do taser (Draw_LaserBeam, Draw_TaserBeam, FractalThing), o relógio (Player_SetWatch),
// a câmera debaixo d'água (Camera_Shear) e a marola (Env_WaterRipple).

#include <windows.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "engine/anim.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_stats.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

void fresh_heap() {
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
    anim_debug_power_on();
    pending_reset();
    GS_SetRefreshRate(60, 60);
    ScriptCam = 0;
    std::memset(MPSettings, 0, sizeof(MPSettings));
}

// ---------------------------------------------------------------- a mira do tronco

// Esqueleto de quatro ossos: 0 raiz na origem, 1 bacia 1 m acima, 2 coluna 0,4 m acima da bacia, 3 a mão 0,2 m para
// +x e 0,5 m para a frente (+z) da coluna. A pele tem o anexo da arma (id cano + 3) na mão, girado 90 graus em y: o
// cano (-x do anexo) aponta para +z.
constexpr uint8_t kDatum = 0x20;
constexpr int8_t kAimWeapon = 0x30;

struct Aimer {
    alignas(16) uint8_t skel_data[0x10 + 4 * 12] = {};
    uint8_t parents[4] = {0xff, 0x80, 0x81, 0x82};
    P32<celglist_tag> meshes[1] = {nullptr};
    alignas(16) MATRIX inv[4]{};
    alignas(4) uint8_t extra[0x24] = {};
    ANIMSKIN_tag skin{};
    obj_tag* obj = nullptr;
    ANIMOBJ_tag* anim = nullptr;
    uint8_t saved_datum = 0;
    Aimer() {
        auto* k = reinterpret_cast<ANIMSKEL_tag*>(skel_data);
        k->id = 5;
        k->bone_count = 4;
        pSkeletons[5] = k;
        for (MATRIX& m : inv) Mat_Identity(&m);
        const uint32_t id = kDatum + 3, bone = 3;
        std::memcpy(extra, &id, 4);
        std::memcpy(extra + 4, &bone, 4);
        const float s = 0.70710678f;
        const float q[4] = {0.0f, s, 0.0f, s};  // 90 graus em y (x, y, z, w)
        std::memcpy(extra + 0x14, q, 16);
        skin.parents = parents;
        skin.skeleton = 5;
        skin.n_meshes = 1;
        skin.meshes = meshes;
        skin.inv_pose = inv;
        skin.extras = extra;
        skin.n_extra = 1;
        skin.scale[0] = skin.scale[1] = skin.scale[2] = 1.0f;
        hashtable_additem(0x5000322, &skin);
        _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f};
        obj = control_create_object(0, &pos, nullptr, nullptr);
        CHECK(AnimObjectNew(obj, 0x5000322) == 1);
        anim = anim_of(obj);
        Mat_Identity(&obj->matrix);
        auto* t = reinterpret_cast<_VECTOR*>(nf::raw(anim->frame));
        auto* r = reinterpret_cast<quaternion_tag*>(anim->frame + 4 * 16);
        t[0] = {0.0f, 0.0f, 0.0f, 0.0f};
        t[1] = {0.0f, 1.0f, 0.0f, 0.0f};
        t[2] = {0.0f, 0.4f, 0.0f, 0.0f};
        t[3] = {0.2f, 0.0f, 0.5f, 0.0f};
        for (int i = 0; i < 4; ++i) r[i] = {0.0f, 0.0f, 0.0f, 1.0f};
        anim->anim_flags |= 1;
        anim->weapon = kAimWeapon;
        saved_datum = weapon_data[kAimWeapon * 0x10c + 0x50];
        weapon_data[kAimWeapon * 0x10c + 0x50] = kDatum;
        obj->type = 2;
    }
    ~Aimer() { weapon_data[kAimWeapon * 0x10c + 0x50] = saved_datum; }
    void rebuild() {
        anim->anim_flags &= ~0xcu;
        psiBuildMatrixPalette(obj, anim_state(anim), 0);
    }
    // O cano depois da pose (com a mira): o ponto do anexo e a direção -x dele.
    void barrel(_VECTOR* at, _VECTOR* dir) {
        MATRIX m;
        AnimGetBoneWorldTrans(obj, kDatum, 3, at, &m);
        *dir = {-m.m[0][0], -m.m[0][1], -m.m[0][2], 0.0f};
    }
    // Distância do alvo à reta do cano.
    float miss(const _VECTOR& target) {
        _VECTOR at, dir;
        barrel(&at, &dir);
        const _VECTOR d{target.x - at.x, target.y - at.y, target.z - at.z, 0.0f};
        const float along = d.x * dir.x + d.y * dir.y + d.z * dir.z;
        const _VECTOR c{d.x - dir.x * along, d.y - dir.y * along, d.z - dir.z * along, 0.0f};
        return std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
    }
};

}  // namespace

TEST(aim_bone_world_trans2_matches_the_built_pose) {
    fresh_heap();
    Aimer a;
    a.rebuild();
    // A cadeia do anexo montada pelas matrizes (AnimGetBoneWorldTrans2) e pelos quatérnios de psiBuildMatrixPalette
    // (AnimGetBoneWorldTrans) dão o mesmo lugar e a mesma orientação quando não há mira.
    _VECTOR p1{}, p2{};
    MATRIX m1, m2;
    AnimGetBoneWorldTrans2(a.obj, kDatum, 3, &p1, &m1);
    AnimGetBoneWorldTrans(a.obj, kDatum, 3, &p2, &m2);
    CHECK_NEAR(p1.x, 0.2f, 1e-5f);
    CHECK_NEAR(p1.y, 1.4f, 1e-5f);
    CHECK_NEAR(p1.z, 0.5f, 1e-5f);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) CHECK_NEAR(m1.m[r][c], m2.m[r][c], 1e-5f);
    }
    // O cano aponta para a frente (+z).
    CHECK_NEAR(-m1.m[0][2], 1.0f, 1e-5f);
    // Por índice de osso (bit 31): a coluna.
    AnimGetBoneWorldTrans2(a.obj, 0x80000002u, 0, &p1, nullptr);
    CHECK_NEAR(p1.y, 1.4f, 1e-5f);
    // Sem o anexo: zero e identidade.
    AnimGetBoneWorldTrans2(a.obj, 0x55, 0, &p1, &m1);
    CHECK(p1.x == 0.0f && p1.y == 0.0f && p1.z == 0.0f && m1.m[0][0] == 1.0f);
}

TEST(aim_turns_the_spine_so_the_barrel_meets_the_target) {
    fresh_heap();
    Aimer a;
    alignas(16) _VECTOR target{3.0f, 1.4f, 5.0f, 0.0f};
    a.rebuild();
    const float before = a.miss(target);
    CHECK(before > 1.0f);
    AnimObjectSetAiming(a.obj, &target, 100.0f, 1.0f, 0, 0);
    a.rebuild();
    // O giro leva o cano ao alvo (só giro: o eixo passa pela bacia e pela coluna); o alvo no alcance liga 0x1000.
    CHECK(a.miss(target) < 0.01f);
    CHECK((a.anim->anim_flags & 0x1100) == 0x1100);
    CHECK(0.4f < a.anim->aim_yaw && a.anim->aim_yaw < 0.6f);
    CHECK_NEAR(a.anim->aim_pitch, 0.0f, 2e-3f);
    // Mais alto: a inclinação aproxima o cano (o pivô do ângulo é a bacia, o giro é na coluna: sobra um pouco).
    target = {0.2f, 3.0f, 5.5f, 0.0f};
    const float high_before = a.miss(target);
    a.anim->aim_yaw = 0.0f;
    a.rebuild();
    CHECK(a.miss(target) < high_before * 0.25f);
    CHECK(0.0f < std::fabs(a.anim->aim_pitch));
    // Devagar (aim_speed 0,2): um quinto do caminho por atualização.
    target = {3.0f, 1.4f, 5.0f, 0.0f};
    a.anim->aim_yaw = 0.0f;
    a.anim->aim_pitch = 0.0f;
    a.anim->aim_speed = 0.2f;
    a.rebuild();
    const float step = a.anim->aim_yaw;
    a.rebuild();
    CHECK(0.0f < step && step < a.anim->aim_yaw);
}

TEST(aim_limits_and_the_upper_yaw_quirk) {
    fresh_heap();
    Aimer a;
    alignas(16) _VECTOR target{0.2f, 60.0f, 5.0f, 0.0f};
    AnimObjectSetAiming(a.obj, &target, 100.0f, 1.0f, 0, 0);
    a.rebuild();
    // Muito alto: a inclinação para em 0,9 rad e liga 0x4000.
    CHECK_NEAR(std::fabs(a.anim->aim_pitch), 0.9f, 1e-5f);
    CHECK((a.anim->anim_flags & 0x4000) != 0);
    // Para um lado o giro para em -0,9 (0x2000); para o outro não para (o original compara a inclinação, ver
    // AnimObjectAimAt).
    target = {-50.0f, 1.4f, 1.5f, 0.0f};
    a.anim->aim_yaw = a.anim->aim_pitch = 0.0f;
    a.rebuild();
    const float left = a.anim->aim_yaw;
    const uint32_t left_flags = a.anim->anim_flags;
    target = {50.0f, 1.4f, 1.5f, 0.0f};
    a.anim->aim_yaw = a.anim->aim_pitch = 0.0f;
    a.rebuild();
    const float right = a.anim->aim_yaw;
    CHECK_NEAR(left, -0.9f, 1e-5f);
    CHECK((left_flags & 0x2000) != 0);
    CHECK(1.2f < right && (a.anim->anim_flags & 0x2000) == 0);
    // Atrás (o cosseno negativo): fora do jogador não mede, o giro fica 0.
    target = {3.0f, 1.4f, -20.0f, 0.0f};
    a.anim->aim_yaw = 0.0f;
    a.rebuild();
    CHECK_NEAR(a.anim->aim_yaw, 0.0f, 1e-5f);
    // Fora do alcance (aim_dist_sq): sem 0x1000 e sem giro.
    target = {300.0f, 1.4f, 400.0f, 0.0f};
    a.rebuild();
    CHECK((a.anim->anim_flags & 0x1000) == 0);
}

TEST(aim_returns_to_center_and_releases) {
    fresh_heap();
    Aimer a;
    alignas(16) _VECTOR target{3.0f, 1.4f, 5.0f, 0.0f};
    AnimObjectSetAiming(a.obj, &target, 100.0f, 0.5f, 0, 0);
    for (int i = 0; i < 20; ++i) a.rebuild();
    CHECK(0.4f < a.anim->aim_yaw);
    // Soltar (0x80 apagado, 0x100 fica): o tronco volta ao meio na mesma rapidez e, quase lá, apaga 0x180.
    AnimObjectSetAiming(a.obj, nullptr, 100.0f, 0.5f, 1, 5);
    CHECK((a.anim->anim_flags & 0x180) == 0x100);
    a.rebuild();
    CHECK(0.0f < a.anim->aim_yaw && a.anim->aim_yaw < 0.4f);
    for (int i = 0; i < 20; ++i) a.rebuild();
    CHECK((a.anim->anim_flags & 0x180) == 0);
    CHECK(std::fabs(a.anim->aim_yaw) < 0.01f);
    // Sem arma (o cano 0xff), nada muda.
    a.anim->weapon = 0x7f;
    a.anim->aim_yaw = 0.3f;
    AnimObjectSetAiming(a.obj, &target, 100.0f, 1.0f, 0, 0);
    a.rebuild();
    CHECK_NEAR(a.anim->aim_yaw, 0.3f, 1e-6f);
}

// ---------------------------------------------------------------- o jogador: mira automática, lasers, relógio

namespace {

constexpr uint32_t kGunSkin = 0x5000100;
constexpr uint32_t kIdle = 0x6000b01;

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

// Como test_weapon.cpp: esqueleto de um osso, peles sem modelos (Bond, o soco, a arma e a do relógio) e scripts vazios.
struct Assets {
    alignas(16) uint8_t skel[0x20] = {};
    uint8_t parents[1] = {0xff};
    ANIMSKIN_tag skin{};
    ANIMSKIN_tag watch{};
    alignas(4) int16_t body[4] = {};
    alignas(4) uint16_t gun[8] = {0, 0, 30, 1, 0, 1, 30, 0x0b00};
    std::vector<uint32_t> seq = make_seq(30, 0, 3);
    Assets() {
        auto* k = reinterpret_cast<ANIMSKEL_tag*>(skel);
        k->id = 0;
        k->bone_count = 1;
        pSkeletons[0] = k;
        skin.parents = parents;
        skin.skeleton = 0;
        skin.scale[0] = skin.scale[1] = skin.scale[2] = 1.0f;
        watch = skin;
        for (uint32_t h : {0x5000007u, 0x50000abu, kGunSkin}) hashtable_additem(h, &skin);
        hashtable_additem(0x500004b, &watch);
        body[2] = 30;
        for (uint32_t h : {0x60001dcu, 0x60001ddu, 0x60009ffu, 0x6000a00u}) hashtable_additem(h, body);
        hashtable_additem(0x4000b00, seq.data());
        AnimProcessSeqData(0x4000b00);
        hashtable_additem(kIdle, gun);
    }
};

void define_weapons() {
    std::memset(weapon_data, 0, sizeof(weapon_data));
    std::memset(ammo_data, 0, sizeof(ammo_data));
    for (int w : {2, 0x4c}) {
        weapon_definition_tag* d = weapon_def(w);
        d->id = int16_t(w);
        d->klass = int16_t(w);
        d->selectable = 1;
        d->skin = kGunSkin;
        d->clip = 7;
        d->bullets = 1;
        d->fire_delay = 10;
        d->burst[0] = 1;
        d->modes = 1;
        d->anim_idle = kIdle;
        d->autoaim = 100.0f;
        d->range = 50.0f;
    }
    weapon_def(1)->id = 1;
    weapon_def(1)->klass = 1;
}

struct PlayerWorld {
    obj_tag* player = nullptr;
    BLData* b = nullptr;
    level_tag level{};
    PlayerWorld() {
        fresh_heap();
        view_debug_power_on();
        lights_debug_power_on();
        player_debug_power_on();
        player_weapon_debug_power_on();
        std::memset(glb_players, 0, sizeof(glb_players));
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(CheatInfo, 0, sizeof(CheatInfo));
        static Assets* assets = nullptr;
        delete assets;
        assets = new Assets;
        define_weapons();
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.difficulty = 2;
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        Input_Init();
        Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        Mission_SetStatus(kMissionRunning);
        _VECTOR pos{1.0f, 10.0f, 2.0f, 1.0f};
        _VECTOR rot{0.0f, 0.0f, 0.0f, 0.0f};
        player = Player_Init(0, &pos, &rot, &level);
        b = player != nullptr ? bloke_of(player) : nullptr;
        for (int i = 0; i < 3; ++i) tick();
    }
    void tick() {
        ++GameState.frame;
        Input_Update();
        Player_Update(player);
        Player_CollisionHandler(player);
        Camera_Update(0);
    }
    _VECTOR head() {
        _VECTOR h{};
        Player_GetHeadPos(&h, player, nullptr);
        return h;
    }
    // Um alvo genérico (tipo 0x51: a mira automática usa a posição dele) a "dist" metros dos olhos, "deg" graus para
    // o lado (giro do jogador + deg).
    obj_tag* target_at(float dist, float deg, float up = 0.0f) {
        const _VECTOR h = head();
        const float a = player->rot.y + deg * 0.017453292f;
        _VECTOR pos{h.x + nf::fm::sin(a) * dist, h.y + up, h.z + nf::fm::cos(a) * dist, 1.0f};
        obj_tag* o = control_create_object(0, &pos, nullptr, nullptr);
        o->type = 0x51;
        o->unknown_fc = 1;
        return o;
    }
};

}  // namespace

TEST(autoaim_picks_targets_inside_the_cone) {
    PlayerWorld w;
    CHECK(w.player != nullptr && anim_of(w.player)->weapon == 2);
    // Cone a 6 m: 10 graus x 100% x (1 - 6/35) = 8,3 graus. A 4 graus entra; a mira vai para ele.
    obj_tag* close_t = w.target_at(6.0f, 4.0f);
    float best = Autoaim_Range;
    w.b->aim_target = nullptr;
    Check_AutoAim(w.player, close_t, &best);
    CHECK_NEAR(best, 6.0f, 1e-3f);
    CHECK(w.b->aim_target == close_t);
    const float x_right = w.b->aim_cur_x;
    CHECK(std::fabs(x_right) > 0.01f);
    // Do outro lado, a mira vai para o outro lado da tela; no meio, fica no meio.
    obj_tag* other = w.target_at(6.0f, -4.0f);
    best = Autoaim_Range;
    Check_AutoAim(w.player, other, &best);
    CHECK(w.b->aim_target == other && w.b->aim_cur_x * x_right < 0.0f);
    obj_tag* center = w.target_at(6.0f, 0.0f);
    best = Autoaim_Range;
    Check_AutoAim(w.player, center, &best);
    CHECK(std::fabs(w.b->aim_cur_x) < 0.01f && std::fabs(w.b->aim_cur_y) < 0.01f);
    // A 20 graus não entra (best e o alvo ficam).
    obj_tag* wide = w.target_at(6.0f, 20.0f);
    best = Autoaim_Range;
    Check_AutoAim(w.player, wide, &best);
    CHECK(best == Autoaim_Range && w.b->aim_target == center);
    // Mais longe que o melhor até agora: não.
    best = 5.0f;
    Check_AutoAim(w.player, close_t, &best);
    CHECK(best == 5.0f);
    // Alto demais (inclinação fora do cone vertical).
    obj_tag* high = w.target_at(6.0f, 0.0f, 3.0f);
    best = Autoaim_Range;
    w.b->aim_target = nullptr;
    Check_AutoAim(w.player, high, &best);
    CHECK(best == Autoaim_Range && w.b->aim_target == nullptr);
}

TEST(autoaim_lock_on_difficulty_and_filters) {
    PlayerWorld w;
    // A 12 graus fica fora do cone (8,3); travado nele (aim_target), o cone dobra e ele fica.
    obj_tag* t = w.target_at(6.0f, 12.0f);
    float best = Autoaim_Range;
    w.b->aim_target = nullptr;
    Check_AutoAim(w.player, t, &best);
    CHECK(best == Autoaim_Range);
    w.b->aim_target = t;
    Check_AutoAim(w.player, t, &best);
    CHECK_NEAR(best, 6.0f, 1e-3f);
    // 00 Agent (dificuldade 3): Autoaim_HardMul 0, sem mira automática.
    obj_tag* c = w.target_at(6.0f, 0.0f);
    w.b->aim_target = nullptr;
    GameState.difficulty = 3;
    best = Autoaim_Range;
    Check_AutoAim(w.player, c, &best);
    CHECK(best == Autoaim_Range);
    // Operative (1): cone x 1,25 (10,4 graus a 6 m): 9 graus entra.
    GameState.difficulty = 1;
    obj_tag* t9 = w.target_at(6.0f, 9.0f);
    best = Autoaim_Range;
    Check_AutoAim(w.player, t9, &best);
    CHECK_NEAR(best, 6.0f, 1e-3f);
    GameState.difficulty = 2;
    // Objeto desligado (+0xfc) e drone que não é inimigo (Drone_tag::side 2) ficam de fora.
    c->unknown_fc = 0;
    best = Autoaim_Range;
    Check_AutoAim(w.player, c, &best);
    CHECK(best == Autoaim_Range);
    std::vector<uint8_t> drone(sizeof(Drone_tag), 0);
    auto* d = reinterpret_cast<Drone_tag*>(drone.data());
    d->side = 2;
    d->health = 100.0f;
    obj_tag* ally = w.target_at(6.0f, 0.0f);
    ally->type = 2;
    ally->data = d;
    best = Autoaim_Range;
    Check_AutoAim(w.player, ally, &best);
    CHECK(best == Autoaim_Range);
    ally->type = 0x51;
    // Arma sem mira automática (weapon_definition_tag::autoaim 0): cone zero.
    weapon_def(2)->autoaim = 0.0f;
    c->unknown_fc = 1;
    best = Autoaim_Range;
    Check_AutoAim(w.player, c, &best);
    CHECK(best == Autoaim_Range);
}

TEST(autoaim_option_drives_player_autoaim) {
    PlayerWorld w;
    obj_tag* t = w.target_at(6.0f, 4.0f);
    // Com a opção desligada (PlayerSetting::autoaim), Player_AutoAim não procura: o alvo de antes fica.
    PlayerSetting[0].autoaim = 0;
    w.b->aim_target = t;
    Player_AutoAim(w.player);
    CHECK(w.b->aim_target == t);
    // Ligada, procura nos drones e nos jogadores (o alvo genérico não é nenhum dos dois): sem candidato, o alvo some.
    PlayerSetting[0].autoaim = 1;
    Player_AutoAim(w.player);
    CHECK(w.b->aim_target == nullptr);
    // Mirando (anim flag 1), também não procura.
    w.b->aim_target = t;
    anim_of(w.player)->flags |= 1;
    Player_AutoAim(w.player);
    CHECK(w.b->aim_target == t);
    anim_of(w.player)->flags &= ~1u;
    // As armas 0x50/0x51 (o gancho) ligam a mira automática mesmo com a opção desligada e procuram os objetos 0x51,
    // com o cone inteiro (10 graus) até o alcance da arma.
    PlayerSetting[0].autoaim = 0;
    w.b->aim_target = nullptr;
    weapon_def(0x50)->range = 30.0f;
    anim_of(w.player)->weapon = 0x50;
    Player_AutoAim(w.player);
    CHECK(w.b->aim_target == t);
    anim_of(w.player)->weapon = 2;
    PlayerSetting[0].autoaim = 1;
}

TEST(laser_beam_spans_from_the_barrel_to_the_hit) {
    PlayerWorld w;
    // A arma do teste não tem anexos: o cano (osso 0) fica na origem.
    alignas(16) _VECTOR to{3.0f, 4.0f, 12.0f, 1.0f};
    Draw_LaserBeam(w.b, &to);
    obj_tag* beam = w.b->fx[0];
    CHECK_NEAR(beam->scale, 13.0f, 1e-4f);
    CHECK(beam->unknown_103[3] == 0xff && beam->unknown_f8 != 0 && (beam->mode & 0x21) == 0x21);
    // O modelo do raio (1 m em -z, escalado só em z: mode 1) cobre do cano até "to".
    MATRIX m;
    View_RotTransMatrix(&beam->rot, &beam->pos, &m);
    const _VECTOR end{m.m[3][0] - m.m[2][0] * beam->scale, m.m[3][1] - m.m[2][1] * beam->scale,
                      m.m[3][2] - m.m[2][2] * beam->scale, 0.0f};
    CHECK_NEAR(end.x, to.x, 1e-3f);
    CHECK_NEAR(end.y, to.y, 1e-3f);
    CHECK_NEAR(end.z, to.z, 1e-3f);
}

TEST(taser_beam_zigzags_in_connected_segments) {
    PlayerWorld w;
    SEED_X = 1234;
    SEED_Y = 5678;
    // 10 m: 8 segmentos (0,8 por metro) de fx[0] a fx[7]; cada um termina onde o próximo começa e o último em "to".
    alignas(16) _VECTOR to{0.0f, 0.0f, 10.0f, 1.0f};
    weapon_def(0x4c)->range = 50.0f;
    Draw_TaserBeam(w.b, &to);
    P32<obj_tag>* fx = w.b->fx;
    float total = 0.0f;
    for (int i = 0; i < 8; ++i) {
        CHECK(fx[i]->unknown_f8 != 0);
        const _VECTOR& next = i < 7 ? fx[i + 1]->pos : to;
        CHECK_NEAR(fx[i]->scale, Vec_Dist3D(&fx[i]->pos, &next), 1e-4f);
        total += fx[i]->scale;
        // O zigue-zague: os pontos do meio desviam até raiz(10) / 6 / 2 da reta.
        CHECK(std::fabs(fx[i]->pos.x) <= 1.0f && std::fabs(fx[i]->pos.y) <= 1.0f);
        CHECK(0x7f <= fx[i]->unknown_103[3]);
    }
    CHECK(total >= 10.0f - 1e-4f);
    CHECK(fx[8]->unknown_f8 == 0 || fx[8]->glist == nullptr);
    // Mais longe que o alcance do taser (arma 0x4c): "to" é puxado para o alcance.
    weapon_def(0x4c)->range = 5.0f;
    to = {0.0f, 0.0f, 10.0f, 1.0f};
    Draw_TaserBeam(w.b, &to);
    CHECK_NEAR(to.z, 5.0f, 1e-4f);
}

TEST(watch_hands_tell_the_mission_time) {
    PlayerWorld w;
    obj_tag* gun = w.b->gun;
    // Sem a pele do relógio: nada.
    Player_SetWatch(w.player);
    CHECK(Hours == nullptr && Minutes == nullptr && Seconds == nullptr);
    // Com ela (0x500004b) em 07000005 (19:30): os três ponteiros, presos ao jogador.
    AnimObjectNew(gun, 0x500004b);
    GameState.level_file = 0x7000005;
    PlrMissionStats[0].elapsed = 0;
    Player_SetWatch(w.player);
    CHECK(Hours != nullptr && Minutes != nullptr && Seconds != nullptr);
    const MATRIX h0 = Hours->matrix, m0 = Minutes->matrix;
    // Uma hora depois (360000 centésimos): o minuto dá a volta (igual), a hora anda 30 graus em volta de x.
    PlrMissionStats[0].elapsed = 360000;
    Player_SetWatch(w.player);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) CHECK_NEAR(Minutes->matrix.m[r][c], m0.m[r][c], 2e-3f);
    }
    const float dot = h0.m[1][1] * Hours->matrix.m[1][1] + h0.m[1][2] * Hours->matrix.m[1][2];
    CHECK_NEAR(dot, 0.8660254f, 2e-3f);
    // Trocando de arma, os ponteiros são apagados.
    obj_tag* hours = Hours;
    AnimObjectNew(gun, kGunSkin);
    Player_SetWatch(w.player);
    CHECK(Hours == nullptr && (hours->life & 1) != 0);
}

TEST(camera_shear_wobbles_under_water) {
    fresh_heap();
    view_debug_power_on();
    Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
    viewer_tag* v = glb_viewer[0];
    Mat_Identity(&v->camera);
    v->camera.m[3][0] = 5.0f;
    float phase = 0.0f;
    std::memcpy(&v->unknown_1e0[0], &phase, 4);
    GlobalBlur = 0;
    Camera_Shear(v);
    // Fase 0: xs = 0,025 (cos), ys = 0: escala 1,025 em x e z, a posição fica, o borrão liga e a fase anda 1/60.
    CHECK_NEAR(v->camera.m[0][0], 1.025f, 1e-4f);
    CHECK_NEAR(v->camera.m[2][2], 1.025f, 1e-4f);
    CHECK_NEAR(v->camera.m[1][1], 1.0f, 1e-4f);
    CHECK_NEAR(v->camera.m[3][0], 5.0f, 1e-6f);
    CHECK(GlobalBlur == 0x91);
    std::memcpy(&phase, &v->unknown_1e0[0], 4);
    CHECK_NEAR(phase, 1.0f / 60.0f, 1e-6f);
    // A fase dá a volta em (-pi, pi).
    phase = 3.14f;
    std::memcpy(&v->unknown_1e0[0], &phase, 4);
    Camera_Shear(v);
    std::memcpy(&phase, &v->unknown_1e0[0], 4);
    CHECK(phase < 0.0f && phase > -3.1415927f);
}

TEST(water_ripple_only_on_water) {
    fresh_heap();
    effect_debug_power_on();
    cel_tag* room = build_alloc_cel(glb_world->first);
    glb_world->first = room;
    room->type = 0xc023;
    HITDATA_tag hit{};
    hit.cel = room;
    hit.pos = {1.0f, 0.0f, 2.0f, 1.0f};
    const auto smokes = [] {
        int n = 0;
        for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) n += o->type == 0xe;
        return n;
    };
    hit.surface = 3;  // pedra
    Env_WaterRipple(&hit);
    CHECK(smokes() == 0);
    hit.surface = 13;  // água
    Env_WaterRipple(&hit);
    CHECK(smokes() == 1);
    hit.surface = 0x40 | 14;  // água com o bit 0x40 (só os 6 bits de baixo contam)
    Env_WaterRipple(&hit);
    CHECK(smokes() == 2);
}
