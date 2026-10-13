// Testes do laço do jogo: GameFlow_Main, ResetMap_LevelToLoad, GS_IsPaused, o tratador de objetos
// (control_movement_object_handler), Collide_Update, Lights_CalcAmbientLight, Trigger_Calc e o ajuste das fases
// (ReadTuningVars).

#include <cmath>
#include <cstring>
#include <string>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/trigger.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

int draws = 0;

void fresh() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    view_debug_power_on();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    pending_reset();
    Door_Init();
    GS_SetRefreshRate(60, 60);
    while (GameFlow_Depth() != 0) GameFlow_PopState();
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(MPSettings, 0, sizeof(MPSettings));
    std::memset(MPGame, 0, sizeof(MPGame));
    draws = 0;
    g_gameflow_hooks.draw = [] { ++draws; };
}

void unhook() { g_gameflow_hooks.draw = nullptr; }

obj_tag* new_object(float x, float y, float z, uint8_t type) {
    _VECTOR pos{x, y, z, 0}, rot{0, 0, 0, 0};
    obj_tag* o = control_create_object(0x40, &pos, &rot, nullptr);
    o->type = type;
    o->sphere = {x, y, z, 0.5f};
    o->old_sphere = o->sphere;
    return o;
}

}  // namespace

TEST(gameflow_boot_and_level_requests) {
    fresh();
    GameFlow_PushState(GF_BOOT, 0.0f, 0);
    GameFlow_Main();
    // O boot zera o jogo, empilha GF_WAIT_START e pede o menu principal (sem filme).
    CHECK(GameFlow_GetState() == GF_LOAD_MAP && GameFlow_Depth() == 3);
    CHECK(GameState.load_file == 0x7000048 && GameState.start_request == 1);
    CHECK(GameState.skip_draw == 1 && draws == 0);
    CHECK(GameState.menu_id == 0x80000002 && GameState.menu_page == 0x40000034);
    CHECK(std::string(reinterpret_cast<const char*>(MPSettings + 7 * 0x30)) == "Bot 4");
    uint32_t players;
    std::memcpy(&players, MPSettings + 0x1ac, 4);
    CHECK(players == 1);
    // O boot reiniciou o heap: o mundo e as listas de objetos voltam como o ResetMap_GameInit faria.
    glb_world = build_alloc_world();
    control_init_object_lists();

    // De volta a GF_WAIT_START: com start_request, empilha a carga; sem, o jogo roda e desenha.
    GameFlow_PopState();
    GameFlow_Main();
    CHECK(GameFlow_GetState() == GF_LOAD_MAP);
    GameFlow_PopState();
    GameState.start_request = 0;
    const uint32_t frame = GameState.frame;
    GameFlow_Main();
    CHECK(GameFlow_GetState() == GF_WAIT_START && draws == 1 && GameState.frame == frame + 1);
    // Pausado, o contador de quadros não anda (mas os ticks sim).
    GameState.paused = 1;
    const uint32_t ticks = GameState.ticks;
    GameFlow_Main();
    CHECK(GameState.frame == frame + 1 && GameState.ticks == ticks + 1);
    GameState.paused = 0;

    // Pedidos de fase: filme de abertura, multijogador, DRIVING.ELF, e fora de hora.
    ResetMap_LevelToLoad(0x7000001, 0, 0);
    CHECK(GameState.load_file == 0x7100001 && GameFlow_GetState() == GF_LOAD_MAP);
    GameFlow_PopState();
    ResetMap_LevelToLoad(0x700001b, 0, 0);
    CHECK(GameState.load_file == 0x7100018);
    GameFlow_PopState();
    ResetMap_LevelToLoad(0x7000001, 0, 1);
    CHECK(GameState.load_file == 0x7000001 && GameState.multiplayer == 0);
    GameFlow_PopState();
    ResetMap_LevelToLoad(0x7000025, 0, 1);
    CHECK(GameState.multiplayer == 1);
    GameFlow_PopState();
    ResetMap_LevelToLoad(0x9000002, 0, 1);
    CHECK(GameFlow_GetState() == GF_LAUNCH_DRIVING);
    ResetMap_LevelToLoad(0x7000003, 0, 1);  // em GF_LAUNCH_DRIVING: ignorado
    CHECK(GameFlow_GetState() == GF_LAUNCH_DRIVING && GameState.load_file == 0x9000002);
    unhook();
}

TEST(gameflow_pulse_menu_and_locks) {
    fresh();
    GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
    GameState.ticks = 9;  // o próximo tick é o 10
    GameFlow_Main();
    CHECK(GameState.pulse == 20);
    GameState.ticks = 39;
    GameFlow_Main();
    CHECK(GameState.pulse == 46);  // 63 - 40, vezes 2
    // GF_MENU: desenha e não desempilha até CheatInfo + 0x10.
    GameFlow_PushState(GF_MENU, 0.0f, 0);
    const int before = draws;
    GameFlow_Main();
    CHECK(GameFlow_GetState() == GF_MENU && draws == before + 1);
    CheatInfo[0x10] = 1;
    GameFlow_Main();
    CHECK(GameFlow_GetState() == GF_WAIT_START);
    CheatInfo[0x10] = 0;
    // GF_LOCKS_INIT: Locks_Init, desempilha e o jogo já roda no mesmo tick.
    GameFlow_PushState(GF_LOCKS_INIT, 0.0f, 0);
    const int before_locks = draws;
    GameFlow_Main();
    CHECK(GameFlow_GetState() == GF_WAIT_START && draws == before_locks + 1);
    unhook();
}

TEST(gs_is_paused_in_multiplayer) {
    fresh();
    CHECK(!GS_IsPaused(-1));
    GameState.paused = 1;
    CHECK(GS_IsPaused(-1));
    const uint32_t mp = 1, players = 2;
    std::memcpy(MPSettings + 0x180, &mp, 4);
    std::memcpy(MPSettings + 0x1ac, &players, 4);
    MPGame[0] = 1;
    CHECK(!GS_IsPaused(-1));  // no multijogador só conta se todos pausaram
    MPGame[0x30] = 1;
    CHECK(GS_IsPaused(-1) && GS_IsPaused(1));
    MPGame[0x30] = 0;
    CHECK(!GS_IsPaused(1));
    unhook();
}

TEST(clrstep_and_ambient_light) {
    CHECK(ClrStep(0.0f, 100.0f, 0.1f) == 10.0f);
    CHECK(ClrStep(50.0f, 0.0f, 0.1f) == 45.0f);
    CHECK(ClrStep(99.5f, 100.0f, 0.1f) == 100.5f);  // passo mínimo de 1, sem parar no alvo
    CHECK(ClrStep(7.0f, 7.0f, 0.1f) == 7.0f);
    CHECK(ClrStep(0.0f, 100.0f, 3.0f) == 100.0f);  // fração presa em 1

    fresh();
    cel_tag* room = build_alloc_cel(nullptr);
    room->misc[0x9a - 0x98] = 200;  // vermelho (canal desligado)
    room->misc[0x9b - 0x98] = 20;   // vermelho (canal ligado)
    room->misc[0x9c - 0x98] = 100;
    room->misc[0x9e - 0x98] = 0;
    room->misc[0xa2 - 0x98] = 5;  // canal 5
    obj_tag* o = new_object(0, 0, 0, 0);
    control_link_object_to_cel(o, room);
    o->color[0] = o->color[1] = o->color[2] = 0;
    Lights_CalcAmbientLight(o);
    CHECK(o->color[0] == 0);  // sem a flag 0x800 não muda
    o->flags |= 0x800;
    Lights_CalcAmbientLight(o);
    CHECK(o->color[0] == 20 && o->color[1] == 10 && o->color[2] == 0);
    switch_channels[5] = 1;
    o->color[0] = 30;
    Lights_CalcAmbientLight(o);
    CHECK(o->color[0] == 29);  // vai para 20, um passo de no mínimo 1
    unhook();
}

TEST(object_handler_updates_deletes_and_settles) {
    fresh();
    // Porta de correr acionada: o tratador chama Doors_Calc e a atualização do tipo 0x1c.
    framelist_tag path[2]{};
    path[0].quat[3] = path[1].quat[3] = 1.0f;
    path[1].pos[0] = 2.0f;
    celglist_tag glist{};
    glist.sphere = {0, 0, 0, 1};
    level_tag level{};
    level.path = path;
    level.path_count = 2;
    level.path_unknown = 10;
    level.params[0] = 2;
    _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
    obj_tag* door = Door_Create(&pos, &rot, &level, &glist);
    CHECK(Door_Activate(door, nullptr, 0) == 1);
    for (int i = 0; i < 20; ++i) control_movement_object_handler(0);
    CHECK(door->state == kDoorOpen);

    // Remoção: o objeto marcado sai da lista e, com mode 0x400, os acertos que apontavam para ele
    // ficam sem objeto.
    obj_tag* victim = new_object(5, 0, 0, 0);
    obj_tag* witness = new_object(9, 0, 0, 0);
    HITDATA_tag* hit = Coll_AddHitToList(&witness->hits, 1.0f, nullptr, victim);
    victim->mode |= 0x400;
    victim->life |= 1;
    witness->flags |= 0x20000000;  // sem sala: não procura uma
    // (O nó do acerto volta ao HitHeap no Collide_Update do mesmo tick, mas o campo obj já foi zerado.)
    control_movement_object_handler(0);
    bool found = false;
    for (obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) found = found || o == victim;
    CHECK(!found);
    CHECK(hit->obj == nullptr);

    // Objeto que se mexeu (mode 0x20): ângulos de volta para (-pi, pi) e posição antiga atualizada.
    obj_tag* mover = new_object(0, 0, 0, 0);
    mover->flags |= 0x20000000;
    mover->mode |= 0x20;
    mover->rot = {7.0f, -4.0f, 1.0f, 0};
    mover->pos = {3, 0, 0, 0};
    control_movement_object_handler(0);
    CHECK_NEAR(mover->rot.x, 7.0 - 2 * 3.14159265, 1e-5);
    CHECK_NEAR(mover->rot.y, -4.0 + 2 * 3.14159265, 1e-5);
    CHECK(mover->rot.z == 1.0f && mover->old_pos.x == 3.0f && (mover->mode & 0x20) == 0);

    // Canais: hold é o tick anterior, prev o de dois ticks atrás.
    switch_channels[9] = 1;
    control_movement_object_handler(0);
    CHECK(switch_channels_hold[9] == 1 && switch_channels_prev[9] == 0);
    control_movement_object_handler(0);
    CHECK(switch_channels_prev[9] == 1);
    switch_channels[9] = 0;
    unhook();
}

TEST(trigger_calc_level_links) {
    fresh();
    GameState.level_file = 0x7000005;
    switch_channels[0xa4] = 1;
    switch_channels[0x6f] = 0;
    Trigger_Calc();
    CHECK(switch_channels[0x6f] == 1 && ProcessTouch == 0);
    GameState.level_file = 0x7000001;
    switch_channels[0x6f] = 0;
    Trigger_Calc();
    CHECK(switch_channels[0x6f] == 0);
    switch_channels[0xa4] = 0;
    unhook();
}

TEST(read_tuning_vars_per_level_group) {
    // ReadTuningVars (0x001d3a80). A comparação valor a valor com o código do ELF, fase a fase, é do nf_loader_check
    // ("ajuste das fases"); aqui as propriedades. Os valores fora do switch são marcados para ver quem os grava.
    const float mark = 123.0f;
    auto poison = [&] {
        Plr_AimSpeed_X = Autoaim_Range = Autoaim_HardMul = ContinueHealthBoostHard = mark;
        Plr_DMod_Easy = Plr_DMod_Normal = Plr_DMod_Hard = Plr_DMod_Multi = Plr_DMod_Head = mark;
        DroneDamage_Head = DroneDamage_Easy = DroneArmour_Helmet = DroneCaptain_Mod_Health = mark;
        DroneFiring.accuracy_easy = DroneFiring.accuracy_normal = DroneFiring.accuracy_hard = mark;
        DroneFiring.too_close_distance = mark;
    };
    // Os globais que ReadTuningVars grava, guardados e devolvidos no fim (outros testes contam com os do .data).
    float* const vars[] = {
        &Plr_AimSpeed_X, &Plr_AimSpeed_Y, &Plr_AimTurnSpeed_X, &Plr_AimTurnSpeed_Y, &Plr_ScopeSpeed_X,
        &Plr_ScopeSpeed_X_Mul, &Plr_ScopeSpeed_X_Steps, &Plr_ScopeSpeed_Y, &Plr_ScopeSpeed_Y_Mul,
        &Plr_ScopeSpeed_Y_Steps, &Plr_NoAimTurnSpeed_X, &Plr_NoAimTurnSpeed_X_Mul, &Plr_NoAimTurnSpeed_X_Steps,
        &Plr_NoAimTurnSpeed_Y, &Plr_NoAimTurnSpeed_Y_Mul, &Plr_NoAimTurnSpeed_Y_Steps, &ContinueHealthBoostEasy,
        &ContinueHealthBoostMedium, &ContinueHealthBoostHard, &Autoaim_Angle_H, &Autoaim_Angle_V, &Autoaim_Range,
        &Autoaim_LockOnMul, &Autoaim_EasyMul, &Autoaim_NormalMul, &Autoaim_HardMul, &Plr_DMod_Easy, &Plr_DMod_Normal,
        &Plr_DMod_Hard, &Plr_DMod_Multi, &Plr_DMod_Head, &Plr_DMod_LowerLimb, &Plr_DMod_UpperLimb, &DroneDamage_Easy,
        &DroneDamage_Normal, &DroneDamage_Hard, &DroneDamage_Head, &DroneDamage_Legs, &DroneDamage_Arms,
        &DroneDamage_Torso, &DroneArmour_Helmet, &DroneArmour_Combat, &DroneArmour_Jacket, &DroneArmour_Vest,
        &DroneCaptain_Mod_BulletDamage, &DroneCaptain_Mod_BulletAccuracy, &DroneCaptain_Mod_Health};
    float saved_vars[sizeof(vars) / sizeof(vars[0])];
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); ++i) saved_vars[i] = *vars[i];
    const DroneFiringSettings saved_firing = DroneFiring;
    const uint32_t saved = GameState.level_file;
    // Sem condição (qualquer fase, até a de menu): a mira do jogador e a automática (alcance 25, nada no 00 Agent).
    poison();
    GameState.level_file = 0x7000048;
    ReadTuningVars();
    CHECK(Plr_AimSpeed_X == 0.1275f && Autoaim_Range == 25.0f && Autoaim_HardMul == 0.0f);
    CHECK(ContinueHealthBoostHard == 50.0f);
    // Fora do switch (o menu principal, as cenas 0700000e-10, as de teste): o resto fica como estava.
    for (uint32_t level : {0x7000048u, 0x700000eu, 0x7000010u, 0x7000017u, 0x7000041u, 0x7000500u, 0x9000001u}) {
        poison();
        GameState.level_file = level;
        ReadTuningVars();
        CHECK(Plr_DMod_Normal == mark && DroneDamage_Head == mark && DroneFiring.accuracy_normal == mark);
    }
    // Toda fase da campanha a pé: o tiro na cabeça x 10, o capacete 1, o dano recebido crescendo com a dificuldade (o
    // do 00 Agent pelo menos 1) e a precisão dos personagens também; nada do multijogador.
    const uint32_t campaign[] = {0x7000001, 0x7000002, 0x7000003, 0x7000004, 0x7000005, 0x7000006, 0x7000007,
                                 0x7000008, 0x7000009, 0x700000a, 0x700000b, 0x700000c, 0x700000d, 0x7000011,
                                 0x7000012, 0x7000013, 0x7000014, 0x7000015, 0x7000016, 0x700001b, 0x700004a};
    for (uint32_t level : campaign) {
        poison();
        GameState.level_file = level;
        ReadTuningVars();
        CHECK(DroneDamage_Head == 10.0f && DroneArmour_Helmet == 1.0f && DroneCaptain_Mod_Health == 2.0f);
        CHECK(Plr_DMod_Easy <= Plr_DMod_Normal && Plr_DMod_Normal <= Plr_DMod_Hard && 1.0f <= Plr_DMod_Hard);
        CHECK(DroneFiring.accuracy_easy <= DroneFiring.accuracy_normal &&
              DroneFiring.accuracy_normal <= DroneFiring.accuracy_hard);
        CHECK(DroneFiring.too_close_distance == 3.0f && Plr_DMod_Multi == mark && Plr_DMod_Head == mark);
    }
    // 07000012 (a segunda torre): o jogador recebe 0,5/0,6/1,0 do dano, os personagens acertam 0,7/0,8/1,0 (estudo do
    // nightfire-cxbx, docs/cxbx.md, conferido na desmontagem) e os quatro do grupo dão o mesmo.
    for (uint32_t level : {0x7000011u, 0x7000012u, 0x7000013u, 0x700004au}) {
        poison();
        GameState.level_file = level;
        ReadTuningVars();
        CHECK(Plr_DMod_Easy == 0.5f && Plr_DMod_Normal == 0.6f && Plr_DMod_Hard == 1.0f);
        CHECK(DroneFiring.accuracy_easy == 0.7f && DroneFiring.accuracy_normal == 0.8f &&
              DroneFiring.accuracy_hard == 1.0f);
    }
    // O multijogador: o dano por parte do corpo, não o por dificuldade; o capacete vale meio.
    for (uint32_t level : {0x7000021u, 0x7000029u, 0x700004bu, 0x700004cu}) {
        poison();
        GameState.level_file = level;
        ReadTuningVars();
        CHECK(Plr_DMod_Multi == 4.0f && Plr_DMod_Head == 4.0f && Plr_DMod_Normal == mark);
        CHECK(DroneArmour_Helmet == 0.5f && DroneFiring.too_close_distance == 4.0f);
    }
    GameState.level_file = saved;
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); ++i) *vars[i] = saved_vars[i];
    DroneFiring = saved_firing;
}
