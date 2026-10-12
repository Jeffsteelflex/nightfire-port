// Testes dos helicópteros (engine/copter.h): o cone de 45 graus (Intersect_ConeSphere e o construtor estático),
// Copter_Register, Copter_GetBody, a velocidade do caminho em Copter_Create e o dano das peças em Copter_Update (fuga
// ou explosão, o canal de "acabou"), com um helicóptero montado à mão num mundo sem geometria.

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/copter.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "platform/psi.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

struct CopterWorld {
    alignas(16) obj_tag part[9]{};
    COPTERPATH path{};
    obj_tag* obj = nullptr;
    COPTER* c = nullptr;
    CopterWorld() {
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
        copter_debug_power_on();
        Copter_Init();
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(&GameState, 0, sizeof(GameState));
        GS_SetRefreshRate(60, 60);
        std::memset(glb_players, 0, sizeof(glb_players));
        // Todas as peças sem máscara e com multiplicador 1, menos a 5 (metade).
        for (COPTERBIT& b : Bits) b = {0, 0, 1.0f};
        Bits[5].mul = 0.5f;
        _VECTOR pos{0.0f, 20.0f, 0.0f, 1.0f};
        _VECTOR rot{};
        obj = control_create_object(0x7c, &pos, &rot, nullptr);
        obj->type = 0x3b;
        obj->state = 1;
        c = static_cast<COPTER*>(obj->data);
        std::memset(c, 0, sizeof(*c));
        c->obj = obj;
        // Um caminho de um quadro só: Copter_Move não anda (sem pontos para interpolar).
        c->path = &path;
        c->channel = 100;  // só ronda: Copter_Update não procura o jogador
        c->done = 0x50;
        DiffMod = 1.0f;
        c->max_damage = 299.0f;
        for (uint16_t i = 0; i < 9; ++i) Copter_Register(&part[i], i, c);
    }
    // Um acerto de "damage" na peça i (o dano fica em HITDATA_tag +0x08, como Collide_GetDamageNObjects o deixa).
    void hit(int i, float damage) {
        HITDATA_tag* h = Coll_AddHitToList(&part[i].hits, 1.0f, nullptr, nullptr);
        std::memcpy(&h->unknown_08, &damage, 4);
    }
    void clear_hits() {
        for (obj_tag& p : part) p.hits = nullptr;
    }
    // Sem a peça 3: acima da metade do dano ela soltaria fumaça (Gas_Init numa sala que este mundo não tem).
    void no_smoke() { c->parts[3] = nullptr; }
};

}  // namespace

TEST(copter_cone_is_45_degrees_from_the_static_constructor) {
    // _GLOBAL_$I$Copter_Create: os dois valores são o seno de 45 graus pelo polinômio da VU0.
    CHECK(std::fabs(COPT_COS - 0.70710678f) < 1e-4f);
    CHECK(std::fabs(COPT_SIN - 0.70710678f) < 1e-4f);
}

TEST(cone_sphere_hits_ahead_inside_45_degrees_and_misses_behind_or_outside) {
    const _VECTOR apex{0.0f, 0.0f, 0.0f, 1.0f};
    const _VECTOR dir{0.0f, 0.0f, 1.0f, 0.0f};
    auto at = [](float deg, float dist, float r) {
        const float a = deg * 3.14159265f / 180.0f;
        return _VECTOR{nf::fm::sin(a) * dist, 0.0f, nf::fm::cos(a) * dist, r};
    };
    // No eixo, à frente; a 30 e 44 graus: dentro.
    _VECTOR s = at(0.0f, 50.0f, 1.0f);
    CHECK(Intersect_ConeSphere(&apex, &dir, COPT_COS, COPT_SIN, &s) == 1);
    s = at(30.0f, 50.0f, 0.1f);
    CHECK(Intersect_ConeSphere(&apex, &dir, COPT_COS, COPT_SIN, &s) == 1);
    s = at(44.0f, 50.0f, 0.1f);
    CHECK(Intersect_ConeSphere(&apex, &dir, COPT_COS, COPT_SIN, &s) == 1);
    // A 60 graus e atrás: fora.
    s = at(60.0f, 50.0f, 0.1f);
    CHECK(Intersect_ConeSphere(&apex, &dir, COPT_COS, COPT_SIN, &s) == 0);
    s = at(180.0f, 50.0f, 1.0f);
    CHECK(Intersect_ConeSphere(&apex, &dir, COPT_COS, COPT_SIN, &s) == 0);
    // A 50 graus, uma esfera grande o bastante para alcançar a borda do cone (a 50 m, 5 graus são ~4,4 m): dentro.
    s = at(50.0f, 50.0f, 5.0f);
    CHECK(Intersect_ConeSphere(&apex, &dir, COPT_COS, COPT_SIN, &s) == 1);
    s = at(50.0f, 50.0f, 3.0f);
    CHECK(Intersect_ConeSphere(&apex, &dir, COPT_COS, COPT_SIN, &s) == 0);
    // O vértice dentro da esfera: sempre.
    s = at(180.0f, 0.5f, 1.0f);
    CHECK(Intersect_ConeSphere(&apex, &dir, COPT_COS, COPT_SIN, &s) == 1);
}

TEST(copter_register_lets_bullets_through_every_part_but_the_body) {
    CopterWorld w;
    for (int i = 0; i < 9; ++i) {
        CHECK(w.c->parts[i] == &w.part[i]);
        CHECK(((w.part[i].flags & 0x20) != 0) == (i != 2));
    }
    // Fora da faixa: ignorado.
    obj_tag extra{};
    Copter_Register(&extra, 9, w.c);
    CHECK(extra.flags == 0);
}

TEST(copter_body_is_a_target_only_once_its_channel_is_on) {
    CopterWorld w;
    CHECK(Copter_GetBody(w.c) == &w.part[2]);
    w.c->enable = 0x41;
    CHECK(Copter_GetBody(w.c) == nullptr);
    switch_channels[0x41] = 1;
    CHECK(Copter_GetBody(w.c) == &w.part[2]);
}

TEST(copter_path_speed_crosses_the_path_in_its_duration) {
    CopterWorld w;
    level_tag level{};
    level.path_count = 101;  // 100 intervalos
    level.params[0] = 1;     // um caminho de ronda: não cria helicóptero
    level.params[1] = 7;
    level.params[3] = 500;   // além do fim: fica no último quadro
    level.params[10] = 200;  // ticks
    _VECTOR pos{}, rot{};
    const uint32_t created = g_copter_stats.created;
    CHECK(Copter_Create(&pos, &rot, &level) == nullptr);
    CHECK(g_copter_stats.created == created);
    const auto* p = reinterpret_cast<const COPTERPATH*>(nf::raw(CopterPaths.first));
    CHECK(p != nullptr && p->group == 7 && p->kind == 1 && p->frames == 100 && p->start == 100);
    // 100 quadros em 200 ticks: meio quadro por tick.
    CHECK(std::fabs(p->speed * 200.0f - 100.0f) < 1e-3f);
    // Sem duração no posicionamento, a do caminho.
    level.params[10] = 0;
    level.path_unknown = 400;
    Copter_Create(&pos, &rot, &level);
    const auto* q = reinterpret_cast<const COPTERPATH*>(nf::raw(CopterPaths.last));
    CHECK(std::fabs(q->speed * 400.0f - 100.0f) < 1e-3f);
}

TEST(copter_damage_is_scaled_per_part_and_drives_it_off_with_its_channel) {
    CopterWorld w;
    w.no_smoke();
    w.hit(2, 60.0f);
    w.hit(5, 100.0f);  // metade
    Copter_Update(w.obj);
    CHECK(std::fabs(w.c->damage - 110.0f) < 1e-3f);
    CHECK(w.obj->state == 1 && switch_channels[0x50] == 0);
    // Passando do máximo (299): foge (estado 4) e liga o canal de "acabou".
    w.clear_hits();
    w.hit(2, 190.0f);
    const uint32_t fled = g_copter_stats.fled;
    Copter_Update(w.obj);
    CHECK(w.obj->state == 4 && switch_channels[0x50] == 1 && g_copter_stats.fled == fled + 1);
    // Fugindo, novos acertos não mudam o estado.
    w.clear_hits();
    w.hit(2, 500.0f);
    Copter_Update(w.obj);
    CHECK(w.obj->state == 4 && g_copter_stats.fled == fled + 1);
}

TEST(copter_explodes_instead_of_fleeing_in_the_two_levels_that_ask_for_it) {
    CopterWorld w;
    w.no_smoke();
    GameState.level_file = 0x700000b;
    w.hit(2, 300.0f);
    const uint32_t exploded = g_copter_stats.exploded;
    Copter_Update(w.obj);
    CHECK(w.obj->state == 6 && switch_channels[0x50] == 1 && g_copter_stats.exploded == exploded + 1);
}

TEST(copter_stays_hidden_and_still_until_its_channel_turns_on) {
    CopterWorld w;
    w.c->enable = 0x42;
    w.c->delay = 3;
    w.c->channel = 0x43;
    for (int t = 0; t < 10; ++t) Copter_Update(w.obj);
    // Desligado: nem o atraso do canal 0x43 corre.
    CHECK(w.c->delay == 3 && switch_channels[0x43] == 0);
    switch_channels[0x42] = 1;
    for (int t = 0; t < 3; ++t) Copter_Update(w.obj);
    CHECK(w.c->enable == 0 && w.c->delay == 0 && switch_channels[0x43] == 1);
}
