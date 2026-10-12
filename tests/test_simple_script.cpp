// Testes dos objetos de roteiro simples (engine/simple_script.h): SS_Create (velocidade, duração padrão, parada presa
// aos passos), o laço, a espera por um disparo (canal, toque, usar), a parada do meio, o fim do caminho e a água da
// sala, com um caminho reto de 4 quadros num mundo sem geometria.

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
#include "engine/resetmap.h"
#include "engine/simple_script.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

struct SsWorld {
    // Quatro quadros em x = 0, 10, 20, 30 (três intervalos).
    framelist_tag path[4]{};
    level_tag level{};
    obj_tag toucher{};
    SsWorld() {
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
        sound_debug_power_on();
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(&GameState, 0, sizeof(GameState));
        GS_SetRefreshRate(60, 60);
        g_simple_script_stats = SimpleScriptStats{};
        for (int i = 0; i < 4; ++i) {
            path[i].pos[0] = 10.0f * float(i);
            path[i].quat[3] = 1.0f;
        }
        level.path = path;
        level.path_count = 4;
        level.path_unknown = 30;  // 3 quadros em 30 ticks: 0,1 por tick
    }
    obj_tag* create(uint32_t mode, uint32_t flags = 0, uint32_t channel = 0) {
        level.params[0] = mode;
        level.params[2] = flags;
        level.params[3] = channel;
        _VECTOR pos{}, rot{};
        return SS_Create(&pos, &rot, &level, nullptr);
    }
    // Um toque do objeto da classe "type" na lista de acertos.
    void touch(obj_tag* o, uint8_t type) {
        toucher.type = type;
        Coll_AddHitToList(&o->hits, 1.0f, nullptr, &toucher);
    }
};

SIMPLESCRIPT_tag* ss(obj_tag* o) { return static_cast<SIMPLESCRIPT_tag*>(o->data); }

}  // namespace

TEST(simple_script_create_needs_a_path_and_crosses_it_in_its_duration) {
    SsWorld w;
    w.level.path_count = 0;
    CHECK(w.create(8) == nullptr);
    w.level.path_count = 4;
    obj_tag* o = w.create(8);
    CHECK(o != nullptr && o->type == 0x25 && ss(o)->frames == 3 && ss(o)->sound == 0xffffu);
    CHECK(std::fabs(ss(o)->speed * 30.0f - 3.0f) < 1e-5f);
    // Sem duração no caminho: 0x4b, escrita no próprio posicionamento.
    w.level.path_unknown = 0;
    obj_tag* o2 = w.create(8);
    CHECK(w.level.path_unknown == 0x4b && std::fabs(ss(o2)->speed * 75.0f - 3.0f) < 1e-5f);
}

TEST(simple_script_stop_frame_snaps_down_to_a_whole_number_of_steps) {
    SsWorld w;
    w.level.path_unknown = 7;  // 3/7 de quadro por tick
    w.level.params[5] = 2;
    obj_tag* o = w.create(0x40);
    const float speed = ss(o)->speed;
    const float steps = ss(o)->stop / speed;
    CHECK(ss(o)->stop <= 2.0f && 2.0f - ss(o)->stop < speed);
    CHECK(std::fabs(steps - std::round(steps)) < 1e-4f);
}

TEST(simple_script_loop_follows_the_path_and_wraps) {
    SsWorld w;
    obj_tag* o = w.create(8);
    // 15 ticks a 0,1 quadro: o objeto no quadro 1,4 (x = 14) e t = 1,5.
    for (int t = 0; t < 15; ++t) SS_Update(o);
    CHECK(std::fabs(o->matrix.m[3][0] - 14.0f) < 1e-3f);
    CHECK((o->mode & 0x20) != 0);
    // Passando do quadro 3, volta ao começo e continua em laço.
    for (int t = 0; t < 16; ++t) SS_Update(o);
    CHECK(ss(o)->t < 0.2f && ss(o)->mode == 8);
}

TEST(simple_script_waits_for_its_channel_then_runs_to_the_end_and_waits_again) {
    SsWorld w;
    obj_tag* o = w.create(0x40, 1, 0x30);
    for (int t = 0; t < 10; ++t) SS_Update(o);
    CHECK(ss(o)->mode == 0x40 && ss(o)->t == 0.0f);
    switch_channels[0x30] = 1;
    SS_Update(o);
    CHECK(ss(o)->mode == 1 && g_simple_script_stats.triggered == 1);
    switch_channels[0x30] = 0;
    for (int t = 0; t < 60 && ss(o)->mode == 1; ++t) SS_Update(o);
    // No fim, de volta ao começo esperando (parâmetro 1 zero), o objeto no fim do caminho.
    CHECK(ss(o)->mode == 0x40 && ss(o)->t == 0.0f && g_simple_script_stats.finished == 1);
    CHECK(std::fabs(o->matrix.m[3][0] - 30.0f) < 1e-3f);
}

TEST(simple_script_once_stops_for_good_at_the_end) {
    SsWorld w;
    w.level.params[1] = 1;
    obj_tag* o = w.create(1);
    for (int t = 0; t < 60; ++t) SS_Update(o);
    CHECK(ss(o)->mode == 0);
    // Parado: nem o canal nem usar o fazem andar de novo.
    SS_Activate(o, nullptr);
    CHECK(ss(o)->mode == 0);
}

TEST(simple_script_touch_fires_by_class_and_sets_its_channel) {
    SsWorld w;
    // Só o jogador (8): um personagem (tipo 2) não dispara.
    obj_tag* o = w.create(0x40, 8, 0x31);
    w.touch(o, 2);
    SS_Update(o);
    CHECK(ss(o)->mode == 0x40 && switch_channels[0x31] == 0);
    o->hits = nullptr;
    w.touch(o, 3);
    SS_Update(o);
    // Sem o bit 1, o disparo liga o canal.
    CHECK(ss(o)->mode == 1 && switch_channels[0x31] == 1);
    // Com o bit 1, o canal só dispara: um tiro não o liga.
    obj_tag* o2 = w.create(0x40, 1 | 2, 0x32);
    w.touch(o2, 5);
    SS_Update(o2);
    CHECK(ss(o2)->mode == 1 && switch_channels[0x32] == 0);
}

TEST(simple_script_pauses_at_the_stop_frame_until_fired_again) {
    SsWorld w;
    w.level.params[5] = 1;  // para no quadro 1 (10 passos de 0,1)
    obj_tag* o = w.create(0x40, 4, 0x33);
    SS_Activate(o, nullptr);
    CHECK(ss(o)->mode == 1 && switch_channels[0x33] == 1);
    switch_channels[0x33] = 0;
    // O canal fica ligado pelo próprio disparo enquanto ninguém o desliga: desliga a cada tick para ver a parada.
    int ticks = 0;
    for (; ticks < 40 && ss(o)->mode == 1; ++ticks) {
        SS_Update(o);
        switch_channels[0x33] = 0;
    }
    CHECK(ss(o)->mode == 0x40);
    CHECK(ss(o)->t > 1.0f && ss(o)->t < 1.2f);
    // Usar de novo: anda do quadro 1 até o fim.
    SS_Activate(o, nullptr);
    for (int t = 0; t < 40 && ss(o)->mode == 1; ++t) {
        SS_Update(o);
        switch_channels[0x33] = 0;
    }
    CHECK(g_simple_script_stats.finished == 1);
}

TEST(simple_script_use_needs_flag_4) {
    SsWorld w;
    obj_tag* o = w.create(0x40, 0, 0x34);
    SS_Activate(o, nullptr);
    CHECK(ss(o)->mode == 0x40 && switch_channels[0x34] == 0);
}

TEST(simple_script_water_level_follows_the_object) {
    SsWorld w;
    w.level.params[7] = 1;
    obj_tag* o = w.create(8);
    cel_tag room{};
    room.water_y = -500.0f;
    cel_tag* old = o->cel;
    o->cel = &room;
    o->sphere.y = 3.5f;
    SS_Update(o);
    CHECK(room.water_y == 3.5f);
    o->cel = old;
}
