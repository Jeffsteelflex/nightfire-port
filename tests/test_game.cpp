// Testes do laço do jogo: GameFlow_Main, ResetMap_LevelToLoad, GS_IsPaused, o tratador de objetos
// (control_movement_object_handler), Collide_Update, Lights_CalcAmbientLight e Trigger_Calc.

#include <cmath>
#include <cstring>
#include <string>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
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
