// Testes de input.cpp (ações e estados), platform/psi_input.cpp (teclado no lugar do controle) e
// player.cpp (andar, olhar, gravidade, cápsula), com um jogador num mundo vazio.


#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "platform/keys.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/hud.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_input.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

void fresh_input() {
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(MPSettings, 0, sizeof(MPSettings));
    const uint32_t one_player = 1;
    std::memcpy(MPSettings + 0x1ac, &one_player, 4);
    std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
    Input_Init();
    GS_SetRefreshRate(60, 60);
}

void press(int vk, bool down) {
    g_psi_keyboard_mouse.active = true;
    g_psi_keyboard_mouse.down[vk] = down;
}

// Esqueleto de um osso e a pele do Bond (05000007) sem modelos, para o AnimObjectNew do
// Player_Init funcionar sem os arquivos do jogo.
struct TestSkin {
    alignas(16) uint8_t skel[0x20] = {};
    uint8_t parents[1] = {0xff};
    ANIMSKIN_tag skin{};
    TestSkin() {
        auto* k = reinterpret_cast<ANIMSKEL_tag*>(skel);
        k->id = 0;
        k->bone_count = 1;
        pSkeletons[0] = k;  // sem scripts carregados as sequências ficam com o esqueleto 0
        skin.parents = parents;
        skin.skeleton = 0;
        skin.scale[0] = skin.scale[1] = skin.scale[2] = 1.0f;
        hashtable_additem(0x5000007, &skin);
        // Scripts de animação vazios (30 quadros, sem comandos) para os que o jogador usa.
        script[2] = 30;
        for (uint32_t h : {0x60001dc, 0x60001dd, 0x60009ff, 0x6000a00}) hashtable_additem(h, script);
    }
    alignas(4) int16_t script[4] = {};
};

// Um jogador num mundo sem geometria: nada embaixo, então ele cai.
struct World {
    TestSkin* skin = nullptr;
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
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        std::memset(glb_players, 0, sizeof(glb_players));
        std::memset(switch_channels, 0, sizeof(switch_channels));
        hashtable_debug_power_on();
        hashtable_initialise();
        anim_debug_power_on();
        static TestSkin* test_skin = nullptr;
        delete test_skin;
        test_skin = new TestSkin;
        skin = test_skin;
        fresh_input();
        Camera_Create(0, glb_world, 0, 0, 0, 0x200, 0x1c0);
        _VECTOR pos{1.0f, 10.0f, 2.0f, 1.0f};
        _VECTOR rot{0.0f, 0.5f, 0.0f, 0.0f};
        // A camada 2D como no ResetMap_GameInit (o HUD do Player_Init cria sprites; as mensagens vão
        // para a fila de textos): sem isso as listas seriam as de um heap anterior.
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        // A fase do teste tem missão em andamento (Mission_Init): a vida pode mudar (PlrStat_OkToUpdate).
        Mission_SetStatus(kMissionRunning);
        player = Player_Init(0, &pos, &rot, &level);
        b = player != nullptr ? bloke_of(player) : nullptr;
        // No jogo a altura do quadril vem da raiz da animação mais root_offset (AnimFrameResolve);
        // aqui as sequências estão vazias, então tudo fica no root_offset.
        if (player != nullptr && anim_of(player) != nullptr) {
            anim_of(player)->root_offset = 1.1f;
            anim_of(player)->root_height = 1.1f;
        }
    }
    void tick() {
        Input_Update();
        Player_Update(player);
        Player_CollisionHandler(player);
        Camera_Update(0);
    }
};

}  // namespace

TEST(input_pressed_held_repeat) {
    fresh_input();
    // Depois do Input_Init o primeiro Input_Update só libera as ações (bit 0x10 de lock), sem
    // registrar o estado; o jogo o chama desde o boot.
    press('E', true);
    Input_Update();
    CHECK(Input_Action(0, kActUse, kActionDown) == 0 && (PlayerSetting[0].lock[kActUse] & 0x10) != 0);
    press('E', false);
    Input_Update();
    press('E', true);
    Input_Update();
    CHECK(Input_Action(0, kActUse, kActionPressed) == 100);
    CHECK(Input_Action(0, kActUse, kActionDown) == 100);
    Input_Update();
    CHECK(Input_Action(0, kActUse, kActionPressed) == 0);  // só no primeiro tick
    CHECK(Input_Action(0, kActUse, kActionDown) == 100);
    // Segurando: repetição a cada 8 ticks a partir de 46.
    while (PlayerSetting[0].hold[kActUse] < 47) Input_Update();
    CHECK(Input_Action(0, kActUse, kActionRepeat) == 0);
    Input_Update();  // 48
    CHECK(Input_Action(0, kActUse, kActionRepeat) == 100);
    Input_Update();
    CHECK(Input_Action(0, kActUse, kActionRepeat) == 0);
    press('E', false);
    Input_Update();
    CHECK(Input_Action(0, kActUse, kActionDown) == 0 && PlayerSetting[0].hold[kActUse] == 0);
    CHECK(PlayerSetting[0].any == 0);
}

TEST(input_lock_waits_for_release) {
    fresh_input();
    Input_Update();  // libera tudo (bit 0x10)
    press(nf::key::Space, true);
    Input_Update();
    CHECK(Input_Action(0, kActJump, kActionDown) == 100);
    // Travada enquanto ligada (bit 1 sem 0x10): o estado congela até soltar.
    PlayerSetting[0].lock[kActJump] = 1;
    Input_ClearAction(0, kActJump);
    Input_Update();
    CHECK(Input_Action(0, kActJump, kActionDown) == 0);
    press(nf::key::Space, false);
    Input_Update();
    CHECK((PlayerSetting[0].lock[kActJump] & 0x10) != 0);
    press(nf::key::Space, true);
    Input_Update();
    CHECK(Input_Action(0, kActJump, kActionPressed) == 100);
}

TEST(input_any_player_and_set_action) {
    fresh_input();
    Input_SetAction(2, kActFire, kActionDown);
    CHECK(Input_Action(-1, kActFire, kActionDown) == 100);
    CHECK(Input_Action(0, kActFire, kActionDown) == 0);
    CHECK(Input_Actionf(-1, kActFire, kActionDown) == 1.0f);
    Input_ClearAction(-1, kActFire);
    CHECK(Input_Action(-1, kActFire, kActionDown) == 0);
    // Valor negativo: Input_Action devolve o u16 de -100.
    PlayerSetting[1].actions[kActTurn] = -1.0f;
    PlayerSetting[1].state[kActTurn] = kActionDown;
    CHECK(Input_Action(1, kActTurn, kActionDown) == uint16_t(-100));
    CHECK(Input_Actionf(-1, kActTurn, kActionDown) == 0.0f);  // o maior, a partir de 0
}

TEST(input_stick_curve) {
    fresh_input();
    CHECK(MapAnalogStick(0.0f) == 0.0f);
    CHECK(MapAnalogStick(0.25f) == 0.0f);  // zona morta (até 36/127)
    CHECK(MapAnalogStick(1.0f) == 1.0f);
    CHECK(MapAnalogStick(-1.0f) == -1.0f);
    float last = 0.0f;
    bool monotonic = true;
    for (int i = 0; i <= 128; ++i) {
        const float v = MapAnalogStick(float(i) / 127.0f);
        monotonic = monotonic && last <= v;
        last = v;
        CHECK(MapAnalogStick(-float(i) / 127.0f) == -v);
    }
    CHECK(monotonic);
    // StickCompensation2 mantém a direção e passa o comprimento pela curva.
    float x = 0.6f, y = 0.8f;
    StickCompensation2(&x, &y);
    CHECK_NEAR(x / y, 0.75f, 1e-5f);
    CHECK_NEAR(std::sqrt(x * x + y * y), 1.0f, 1e-5f);
    // StickCompensation3: um eixo perto do centro e o outro além de 0,707 vai ao máximo.
    x = 0.5f;
    y = 0.8f;
    StickCompensation3(&x, &y);
    CHECK(x == 0.5f && y == 1.0f);
    x = 0.8f;
    y = 0.8f;
    StickCompensation3(&x, &y);
    CHECK(x == 0.8f && y == 0.8f);
}

TEST(input_keyboard_mapping) {
    fresh_input();
    Input_Update();
    press('W', true);
    press('A', true);
    Input_Update();
    // Diagonal como o analógico: 1/sqrt(2) em cada eixo.
    CHECK_NEAR(Input_Actionf(0, kActForward, kActionDown), 0.70710677f, 1e-6f);
    CHECK_NEAR(Input_Actionf(0, kActStrafe, kActionDown), 0.70710677f, 1e-6f);
    press('W', false);
    press('A', false);
    press(nf::key::Left, true);
    press(nf::key::Up, true);
    Input_Update();
    // Virar e olhar passam pela curva juntos (StickCompensation2): a diagonal vira 1/sqrt(2).
    CHECK_NEAR(Input_Actionf(0, kActTurn, kActionDown), -0.70710677f, 1e-5f);
    CHECK_NEAR(Input_Actionf(0, kActLookY, kActionDown), 0.70710677f, 1e-5f);
    PlayerSetting[0].invert_look = 1;
    Input_Update();
    CHECK(Input_Actionf(0, kActLookY, kActionDown) < 0.0f && Input_Actionf(0, kActAimY, kActionDown) < 0.0f);
    // Sem o jogo ativo (câmera livre, mouse solto), nada passa.
    g_psi_keyboard_mouse.active = false;
    Input_Update();
    CHECK(Input_Actionf(0, kActTurn, kActionDown) == 0.0f && PlayerSetting[0].any == 0);
}

TEST(accel_func0_ramp) {
    float speed = 0.0f;
    // Na zona morta a velocidade é a base.
    CHECK(AccelFunc0(0.0f, &speed, 0.04f, 2.0f, 120.0f, 0.05f, 0.95f) == 0.04f);
    speed = 0.04f;
    // No fim do analógico ela sobe (0,08 - 0,04) / 120 por chamada até 0,08.
    for (int i = 0; i < 60; ++i) speed = AccelFunc0(1.0f, &speed, 0.04f, 2.0f, 120.0f, 0.05f, 0.95f);
    CHECK_NEAR(speed, 0.06f, 1e-5f);
    for (int i = 0; i < 200; ++i) speed = AccelFunc0(1.0f, &speed, 0.04f, 2.0f, 120.0f, 0.05f, 0.95f);
    CHECK(speed == 0.08f);
    // Peculiaridade do original: acima do alvo (analógico entre 0,05 e 0,95 depois de acelerar) o
    // passo continua positivo, e a velocidade sobe até o limite base + base * mul.
    const float up = AccelFunc0(0.5f, &speed, 0.04f, 2.0f, 120.0f, 0.05f, 0.95f);
    CHECK_NEAR(up, 0.08f + 0.04f / 120.0f, 1e-6f);
    for (int i = 0; i < 400; ++i) speed = AccelFunc0(0.5f, &speed, 0.04f, 2.0f, 120.0f, 0.05f, 0.95f);
    CHECK_NEAR(speed, 0.12f, 1e-6f);
}

TEST(player_init_and_fall) {
    World w;
    CHECK(w.player != nullptr && glb_players[0] == w.player && glb_blokes[0] == w.b);
    CHECK(w.player->type == 3 && w.player->hit_test == &w.b->hit_test);
    CHECK(glb_viewer[0]->follow == w.player && glb_viewer[0]->updater == Player_PositionCamera);
    CHECK(anim_of(w.player) != nullptr && anim_of(w.player)->skin == &w.skin->skin);
    // Sem chão: posição 0,1 + 1,1 acima do ponto de partida.
    CHECK_NEAR(w.player->pos.y, 11.2f, 1e-5f);
    CHECK(WldGravity.y == -9.8f && w.b->eye_height == 0.7f);
    // Cai com a gravidade: v = -9,8 t, queda = 9,8 n (n + 1) / 2 / 3600 depois de n ticks.
    const float y0 = w.player->pos.y;
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK_NEAR(w.b->fall.y, -9.8f, 1e-3f);
    CHECK_NEAR(y0 - w.player->pos.y, 9.8f * 60.0f * 61.0f / 7200.0f, 1e-3f);
    CHECK(w.b->ground_bits == 0 && w.b->fall_time == 60.0f);
    // Limite de 45 m/s.
    for (int i = 0; i < 400; ++i) w.tick();
    CHECK(w.b->fall.y == -45.0f);
}

TEST(player_capsule_and_camera) {
    World w;
    w.tick();
    const _VECTOR& p = w.player->pos;
    const HITTEST_tag& t = w.b->hit_test;
    // Andando: segmento de 0,275 acima da posição até root_height - 0,55 abaixo, raio 0,55; o pé
    // da cápsula fica na altura da raiz abaixo da posição.
    CHECK(t.type == 0x800 && t.radius == 0.55f && t.cel == w.player->cel);
    CHECK_NEAR(t.start.y - p.y, 0.275f, 1e-5f);
    CHECK_NEAR(t.end.y - p.y, 0.55f - 1.1f, 1e-5f);
    CHECK_NEAR(t.end.y - t.radius, p.y - 1.1f, 1e-5f);
    CHECK(t.start.x == p.x && t.end.z == p.z);
    // A câmera: olhos 0,7 acima da posição, girada como o jogador.
    const MATRIX& cam = glb_viewer[0]->camera;
    CHECK_NEAR(cam.m[3][1], p.y + 0.7f, 1e-5f);
    CHECK_NEAR(cam.m[3][0], p.x, 1e-5f);
    CHECK_NEAR(cam.m[2][0], w.player->matrix.m[2][0], 1e-5f);
    // Olhar para cima inclina a câmera para cima; o limite é 90 graus.
    w.b->pitch = 0.5f;
    Player_PositionCamera(w.player, nullptr);
    CHECK_NEAR(cam.m[2][1], nf::fm::sin(0.5f * 1.5707964f), 2e-3f);
    w.b->pitch = 3.0f;
    Player_ViewClamping(w.player);
    CHECK(w.b->pitch == 1.0f);
    // Agachado os olhos descem até 0,45 (0,0225 por tick).
    w.player->unknown_f6 = kPlrCrouch;
    for (int i = 0; i < 30; ++i) Player_PositionCamera(w.player, nullptr);
    CHECK(w.b->crouch_drop == 0.45f);
    _VECTOR head;
    Player_GetHeadPos(&head, w.player, nullptr);
    CHECK_NEAR(head.y, p.y + 0.7f - 0.45f, 1e-5f);
}

TEST(player_walks_and_turns) {
    World w;
    // Parado no ar não importa para andar: o movimento local vem de Player_Move.
    press('W', true);
    for (int i = 0; i < 30; ++i) w.tick();
    // Para a frente: alvo 0,105 por tick, limitado a 0,1.
    CHECK_NEAR(w.b->vel.z, 0.1f, 1e-6f);
    CHECK(w.b->vel.x == 0.0f);
    const _VECTOR before = w.player->pos;
    w.tick();
    _VECTOR fwd;
    Mat_GetDir(&fwd, &w.player->matrix);
    const float dx = w.player->pos.x - before.x, dz = w.player->pos.z - before.z;
    CHECK_NEAR(dx * fwd.x + dz * fwd.z, 0.1f, 1e-4f);
    press('W', false);
    press('S', true);
    for (int i = 0; i < 80; ++i) w.tick();
    CHECK_NEAR(w.b->vel.z, -(0.075f + 0.005f), 1e-6f);  // para trás é mais lento
    press('S', false);
    press('D', true);
    for (int i = 0; i < 80; ++i) w.tick();
    CHECK_NEAR(w.b->vel.x, -0.075f, 1e-6f);  // direita é -x local
    press('D', false);
    // Virar: o primeiro tick parado põe a velocidade de giro na base (0,04); segurando, acelera.
    w.tick();
    CHECK(w.b->turn_speed == Plr_NoAimTurnSpeed_X);
    const float yaw = w.player->rot.y;
    press(nf::key::Left, true);
    w.tick();
    CHECK_NEAR(w.player->rot.y - yaw, 0.04f + 0.04f / 120.0f, 1e-5f);  // esquerda aumenta o giro
    // O mouse (só no PC) soma direto ao giro e à inclinação.
    press(nf::key::Left, false);
    w.tick();
    const float yaw2 = w.player->rot.y;
    psiMouseSensitivity = 0.001f;
    g_psi_keyboard_mouse.look_dx = -100.0f;  // para a esquerda
    g_psi_keyboard_mouse.look_dy = -50.0f;   // para cima
    w.tick();
    CHECK_NEAR(w.player->rot.y - yaw2, 0.1f, 1e-5f);
    CHECK_NEAR(w.b->pitch, 0.05f / 1.5707964f, 1e-5f);
    CHECK(w.b->look_state == 1);
}

TEST(player_pain_difficulty_armour_and_flash) {
    World w;
    CHECK(w.b->health == 100.0f);
    Plr_DMod_Easy = 0.75f;
    Plr_DMod_Normal = 1.0f;
    Plr_DMod_Hard = 1.25f;
    // Normal: 10 de dano tiram 10 de vida; o vermelho soma dano x 21,3 (até 128 por vez).
    GameState.difficulty = 2;
    Player_HandlePain(w.player, w.b, 10.0f, 0, -1);
    CHECK_NEAR(w.b->health, 90.0f, 1e-4f);
    CHECK(w.b->pain_flash == 128 && w.b->unknown_8bc == 1.0f && w.b->hit_dir == 0xc);
    Player_HandlePain(w.player, w.b, 1.0f, 0, -1);
    CHECK(w.b->pain_flash == 149);  // 128 + 21
    // Difícil: x1,25; o colete absorve primeiro (só nos tiros, tipo 0).
    GameState.difficulty = 3;
    w.b->armour = 5.0f;
    Player_HandlePain(w.player, w.b, 8.0f, 0, -1);
    CHECK(w.b->armour == 0.0f);
    CHECK_NEAR(w.b->health, 89.0f - 5.0f, 1e-4f);  // 10 de dano, 5 no colete
    // Queda (tipo 6): o colete não ajuda.
    GameState.difficulty = 1;
    w.b->armour = 50.0f;
    Player_HandlePain(w.player, w.b, 4.0f, 6, -1);
    CHECK(w.b->armour == 50.0f && w.b->hit_dir == 2);
    CHECK_NEAR(w.b->health, 84.0f - 3.0f, 1e-4f);
    // Invencível: a vida não muda. Sem dano ou já morto, nada acontece.
    const uint32_t god = 1;
    std::memcpy(CheatInfo, &god, 4);
    Player_HandlePain(w.player, w.b, 30.0f, 0, -1);
    CHECK_NEAR(w.b->health, 81.0f, 1e-4f);
    std::memset(CheatInfo, 0, 4);
    w.b->pain_flash = 0;
    Player_HandlePain(w.player, w.b, 0.0f, 0, -1);
    CHECK(w.b->pain_flash == 0);
    // Menos de 1 de vida conta como morto (normal: 81 - 80,5 = 0,5).
    GameState.difficulty = 2;
    Player_HandlePain(w.player, w.b, 80.5f, 1, -1);
    CHECK(w.b->health == 0.0f);
    CHECK(MP_RegisterBulletHit(0, nullptr, 0.0f));  // fora do multijogador todo acerto vale
}

TEST(player_death_and_mission_failed) {
    // A cortina de sangue do HUD (pane 0xc): Player_HandleDeath a liga e espera por ela ou por Start.
    static SpriteInfo blood[2] = {};
    for (SpriteInfo& s : blood) s.label = 0xffffffffu;
    static HUDPANECREATE_tag blood_pane = {0, 0, 512, 448, HUD_CreateShrink, HUD_UpdateBloodPane, blood, 2, 0, 0x180, 0};
    hud_debug_power_on();
    PaneList[kHudBlood] = &blood_pane;
    World w;
    w.tick();
    CHECK(Player_CheckForDeath(w.player, 3) == 0 && w.player->type == 3);
    // A vida é presa em 500.
    w.b->health = 900.0f;
    CHECK(Player_CheckForDeath(w.player, 3) == 0 && w.b->health == 500.0f);
    // Sem vida: vira o tipo 0x12 no estado 2, sem arma, e a missão falha (canal 0x5e).
    w.b->health = 0.0f;
    GameState.frame = 1234;
    CHECK(Player_CheckForDeath(w.player, 3) == 1);
    CHECK(w.player->type == 0x12 && w.player->state == 2 && w.player->created == 1234);
    CHECK(switch_channels[0x5e] == 1 && switch_channels[0x62] == 0);
    CHECK(w.player->unknown_f6 == 0xd && (anim_of(w.player)->flags & 1) == 0);
    CHECK(Player_CheckForDeath(w.player, 3) == 1);  // já morto
    // Morto, o tick só anima e mostra a cortina de sangue; X (Espaço, ação 25) liga o canal 0x62 (missão
    // fracassada). Start (Enter) abriria o menu de pausa, como no PS2.
    for (int i = 0; i < 3; ++i) w.tick();
    CHECK(switch_channels[0x62] == 0);
    CHECK(static_cast<const HUDINFO_tag*>(w.b->hud)->panes[kHudBlood].enabled == 1);
    press(nf::key::Space, true);
    w.tick();
    press(nf::key::Space, false);
    CHECK(switch_channels[0x62] == 1);
    hud_debug_power_on();
}

TEST(camera_shake_falls_with_distance) {
    World w;
    viewer_tag* v = glb_viewer[0];
    v->camera.m[3][0] = 0.0f;
    v->camera.m[3][1] = 0.0f;
    v->camera.m[3][2] = 0.0f;
    v->shake = 0.0f;
    _VECTOR at{1.0f, 0.0f, 0.0f, 1.0f};
    Camera_Shake(&at, 2.0f);  // alcance 4: 4 x (1 - 1/16)
    CHECK_NEAR(v->shake, 3.75f, 1e-5f);
    v->shake = 0.0f;
    _VECTOR away{5.0f, 0.0f, 0.0f, 1.0f};
    Camera_Shake(&away, 2.0f);
    CHECK(v->shake == 0.0f);
    Camera_Shake(&at, 20.0f);  // até 10
    CHECK(v->shake == 10.0f);
}
