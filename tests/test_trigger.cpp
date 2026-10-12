// Testes dos gatilhos (engine/trigger.cpp) sem dados do jogo: gatilhos montados à mão e uma lista de
// acertos falsa.

#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "engine/text.h"
#include "engine/trigger.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

alignas(16) obj_tag g_trig;
alignas(16) TRIGGER_Tag g_data;
alignas(16) obj_tag g_toucher;
alignas(16) HITDATA_tag g_hit;

TRIGGER_Tag* fresh(uint16_t mode, uint32_t channel) {
    std::memset(switch_channels, 0, sizeof(switch_channels));
    std::memset(switch_channels_time, 0, sizeof(switch_channels_time));
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(&g_trig, 0, sizeof(g_trig));
    std::memset(&g_data, 0, sizeof(g_data));
    g_trig.type = 0x1d;
    g_trig.data = &g_data;
    g_data.mode = mode;
    g_data.channel = channel;
    g_data.touch = 8;
    return &g_data;
}

void touched_by(uint8_t type) {
    std::memset(&g_toucher, 0, sizeof(g_toucher));
    std::memset(&g_hit, 0, sizeof(g_hit));
    g_toucher.type = type;
    g_hit.obj = &g_toucher;
    g_trig.hits = &g_hit;
}

}  // namespace

TEST(trigger_and_or_and_copy) {
    TRIGGER_Tag* t = fresh(3, 0x40);
    t->inputs[0] = 0x10;
    t->inputs[1] = 0x11;
    switch_channels[0x10] = 1;
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x40] == 0);  // E: falta uma entrada
    switch_channels[0x11] = 1;
    GameState.frame = 77;
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x40] == 1 && switch_channels_time[0x40] == 77);
    switch_channels[0x10] = 0;
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x40] == 0);  // E desliga junto
    // OU
    t->mode = 0xc;
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x40] == 1);
    // Cópia: o canal de saída vai para as entradas.
    t->mode = 5;
    switch_channels[0x40] = 1;
    switch_channels[0x10] = 0;
    switch_channels[0x11] = 0;
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x10] == 1 && switch_channels[0x11] == 1);
}

TEST(trigger_sequence_needs_order) {
    TRIGGER_Tag* t = fresh(4, 0x40);
    t->inputs[0] = 0x10;
    t->inputs[1] = 0x11;
    switch_channels[0x10] = switch_channels[0x11] = 1;
    switch_channels_time[0x10] = 100;
    switch_channels_time[0x11] = 50;  // a segunda ligou antes (com folga de 4 ticks: não vale)
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x40] == 0);
    switch_channels_time[0x11] = 97;  // 3 ticks antes: dentro da folga
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x40] == 1);
}

TEST(trigger_touch_once_by_player) {
    TRIGGER_Tag* t = fresh(1, 0x20);
    g_trig.flags = 0x10;
    touched_by(2);  // um personagem: este gatilho só aceita o jogador (8)
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x20] == 0);
    touched_by(3);
    GameState.frame = 9;
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x20] == 1 && switch_channels_time[0x20] == 9);
    CHECK((g_trig.life & 1) == 0);
    Trigger_Update(&g_trig);  // já ligado: um gatilho invisível (0x10) de uma vez some
    CHECK((g_trig.life & 1) != 0);
    // Travado (canal 10 ligado): não faz nada.
    t = fresh(1, 0x21);
    t->lock = 0x30;
    switch_channels[0x30] = 1;
    touched_by(3);
    Trigger_Update(&g_trig);
    CHECK(switch_channels[0x21] == 0 && Trigger_Locked(&g_trig) == 1);
    g_trig.hits = nullptr;
}

TEST(trigger_touch_needs_process_touch) {
    TRIGGER_Tag* t = fresh(2, 0x22);
    t->touch = 0x18;
    touched_by(2);
    Trigger_Update(&g_trig);  // fora de Trigger_Calc
    CHECK(switch_channels[0x22] == 0);
    ProcessTouch = 1;
    Trigger_Update(&g_trig);
    ProcessTouch = 0;
    CHECK(switch_channels[0x22] == 1 && Trigger_Type(&g_trig) == 2);
    g_trig.hits = nullptr;
}

TEST(init_switch_channels_clears_all_and_stamps_frame) {
    fresh(0, 0);
    for (int i = 0; i < 256; ++i) {
        switch_channels[i] = 1;
        switch_channels_prev[i] = 1;
        switch_channels_hold[i] = 1;
        switch_channels_MusicVars[i] = 9;
        switch_channels_time[i] = 5;
    }
    GameState.frame = 1234;
    Init_SwitchChannels();
    bool ok = true;
    for (int i = 0; i < 256; ++i) {
        ok = ok && switch_channels[i] == 0 && switch_channels_prev[i] == 0 && switch_channels_hold[i] == 0 &&
             switch_channels_MusicVars[i] == 0 && switch_channels_time[i] == 1234;
    }
    CHECK(ok);
}

TEST(hint_plays_its_sound_and_text_once_for_the_player) {
    // Uma dica (Hint_Update) ligada e não apagada: um personagem tocando não conta; o jogador faz o som (Sound_Play),
    // a mensagem (Text_AddMsg; sem fila, contada como perdida) e a remoção, uma vez.
    std::memset(switch_channels, 0, sizeof(switch_channels));
    sound_debug_power_on();
    text_debug_power_on();
    alignas(16) static obj_tag hint;
    alignas(16) static HINT_tag data;
    alignas(16) static BLData bond;
    std::memset(&hint, 0, sizeof(hint));
    std::memset(&bond, 0, sizeof(bond));
    hint.type = 0x43;
    hint.data = &data;
    data = {0x123, 0x2000005, 0x30, 0x31};
    touched_by(3);
    g_toucher.data = &bond;
    g_trig.hits = nullptr;
    hint.hits = &g_hit;
    Hint_Update(&hint);  // o canal que a liga (0x30) está desligado
    CHECK(g_sound_stats.calls == 0 && (hint.life & 1) == 0);
    switch_channels[0x30] = 1;
    g_toucher.type = 2;
    Hint_Update(&hint);  // só um personagem
    CHECK(g_sound_stats.calls == 0 && (hint.life & 1) == 0);
    g_toucher.type = 3;
    Hint_Update(&hint);
    CHECK(g_sound_stats.calls == 1 && (hint.life & 1) != 0);
    CHECK(g_text_stats.no_node == 1);  // a mensagem foi pedida
    // Apagada (canal 0x31): some calada, mesmo com o jogador em cima.
    hint.life = 0;
    switch_channels[0x31] = 1;
    Hint_Update(&hint);
    CHECK(g_sound_stats.calls == 1 && (hint.life & 1) != 0);
    // Sem som (0): só a mensagem.
    hint.life = 0;
    switch_channels[0x31] = 0;
    data.sound = 0;
    Hint_Update(&hint);
    CHECK(g_sound_stats.calls == 1 && (hint.life & 1) != 0 && g_text_stats.no_node == 2);
    hint.hits = nullptr;
    sound_debug_power_on();
    text_debug_power_on();
}

TEST(hint_create_follows_the_hints_option_and_the_difficulty) {
    // Parâmetro 2: 0 todas as dificuldades, 1-4 até ela, 5/6/7 só a 1/2/3; DrawInfo[3] desligado: nenhuma. A dica
    // criada é do tipo 0x43 com {som = parâmetro 1, texto = 0, liga = 3, apaga = 4}.
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    struct Case {
        uint32_t param, difficulty;
        bool created;
    };
    const Case cases[] = {{0, 3, true}, {1, 1, true}, {1, 2, false}, {2, 2, true}, {3, 2, true},
                          {5, 1, true}, {5, 2, false}, {6, 2, true}, {6, 3, false}, {7, 3, true}};
    level_tag level{};
    level.params[0] = 0x2000005;
    level.params[1] = 0x1ab;
    level.params[3] = 0x40;
    level.params[4] = 0x41;
    _VECTOR pos{1.0f, 2.0f, 3.0f, 1.0f}, rot{};
    for (const Case& c : cases) {
        std::memset(&GameState, 0, sizeof(GameState));
        GameState.difficulty = c.difficulty;
        level.params[2] = c.param;
        DrawInfo[3] = 0;
        CHECK(Hint_Create(&pos, &rot, &level, nullptr) == nullptr);
        DrawInfo[3] = 1;
        obj_tag* o = Hint_Create(&pos, &rot, &level, nullptr);
        CHECK((o != nullptr) == c.created);
        if (o == nullptr) continue;
        const HINT_tag* h = static_cast<const HINT_tag*>(o->data);
        CHECK(o->type == 0x43 && o->pos.x == 1.0f && o->pos.z == 3.0f);
        CHECK(h->sound == 0x1ab && h->label == 0x2000005 && h->on == 0x40 && h->off == 0x41);
    }
    DrawInfo[3] = 0;
}
