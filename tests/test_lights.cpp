// Testes de lights.cpp (luzes pontuais, escolha das mais próximas, pisca-pisca) e da parte da
// plataforma (psiLight_SetLights, psiPreGameRun).

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "test_framework.h"

namespace {
void fresh() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    lights_debug_power_on();
    view_debug_power_on();
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(switch_channels, 0, sizeof(switch_channels));
    Light_Init();
}
light_tag* point(float x, float radius, uint8_t kind = 1, int16_t ticks = -1) {
    _VECTOR p{x, 0.0f, 0.0f, 1.0f};
    return Light_Create(&p, 200, 100, 50, radius, 0, ticks, kind, 2.0f, 0, 0, 0, 0);
}
int used_count() {
    int n = 0;
    for (LLNODE_tag* l = LightList.used.first; l != nullptr; l = l->next) ++n;
    return n;
}
}  // namespace

TEST(lights_create_live_and_die) {
    fresh();
    CHECK(LightList.free.count == 32 && LightList.used.count == 0);
    light_tag* forever = point(0.0f, 5.0f);
    light_tag* flash = point(1.0f, 5.0f, 1, 0);  // 0 ticks: some no próximo Light_Update
    CHECK(forever->life == 0 && flash->life == 1 && forever->on == 1 && forever->sound == 0xffff);
    CHECK(forever->r == 200 && forever->radius == 5.0f && forever->intensity == 2.0f);
    Light_Update();
    CHECK(used_count() == 1 && LightList.used.first == &forever->node);
    for (int i = 0; i < 100; ++i) Light_Update();
    CHECK(used_count() == 1);
    CHECK(Light_Delete(forever) == 1 && used_count() == 0 && Light_Delete(forever) == 0);
    // Mais de 32 luzes: a lista cresce.
    for (int i = 0; i < 40; ++i) point(float(i), 1.0f);
    CHECK(used_count() == 40);
}

TEST(lights_closest_by_relative_distance_and_kind) {
    fresh();
    light_tag* near_small = point(2.0f, 4.0f);   // d 2, relativa 2 / (4 / 2) = 1
    light_tag* far_big = point(6.0f, 20.0f);     // d 6, relativa 0,6
    point(30.0f, 5.0f);                          // longe demais (30 > 1 + 5)
    light_tag* objects_only = point(1.0f, 4.0f, 0);  // tipo 0: só com a flag 0x80
    _VECTOR s{0.0f, 0.0f, 0.0f, 1.0f};
    uint16_t n = 0;
    P32<light_tag>* l = Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&s), 0, &n);
    CHECK(n == 2 && l != nullptr);
    if (n == 2) CHECK(l[0] == far_big && l[1] == near_small);  // crescente pela distância relativa
    CHECK_NEAR(far_big->dist, 0.6, 1e-6);
    l = Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&s), 0x80, &n);
    CHECK(n == 3 && l[0] == objects_only);
    // Tipo 1 não ilumina objetos com a flag 0x100.
    l = Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&s), 0x180, &n);
    CHECK(n == 1 && l[0] == objects_only);
    // Apagada não conta; sem nenhuma, nulo.
    near_small->on = far_big->on = objects_only->on = 0;
    CHECK(Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&s), 0x80, &n) == nullptr && n == 0);
}

TEST(lights_follow_channel_and_flicker) {
    fresh();
    _VECTOR p{0.0f, 0.0f, 0.0f, 1.0f};
    light_tag* lamp = Light_Create(&p, 255, 255, 255, 5.0f, 7, -1, 1, 1.0f, 0, 0, 0, 0);
    Light_Update();
    CHECK(lamp->on == 0);
    switch_channels[7] = 1;
    Light_Update();
    CHECK(lamp->on == 1);
    // Pisca: acesa (máscara 1: só nos quadros ímpares) por até 4 ticks, apagada por 2 a 3.
    light_tag* blink = Light_Create(&p, 255, 255, 255, 5.0f, 0, -1, 1, 1.0f, 4, 4, 1, 0x55);
    CHECK(blink->flicker_rand == 2 && 4 <= blink->flicker && blink->flicker <= 7);
    int lit = 0, dark = 0;
    for (int t = 0; t < 200; ++t) {
        ++GameState.frame;
        Light_Update();
        (blink->on != 0 ? lit : dark) += 1;
        if ((blink->flicker & 0xf000) != 0 && blink->on != 0) CHECK((GameState.frame & 1) == 1);
    }
    CHECK(lit > 0 && dark > 0);
}

TEST(lights_ambient_radiators_from_the_map_block) {
    fresh();
    alignas(4) uint8_t block[4 + 2 * 0x20] = {};
    const uint32_t n = 2;
    std::memcpy(block, &n, 4);
    const float a[8] = {1.0f, 2.0f, 3.0f, 16.0f, 0.0f, 1.0f, 0.5f, 0.0f};
    const float b[8] = {-1.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(block + 4, a, sizeof(a));
    std::memcpy(block + 4 + 0x20, b, sizeof(b));
    Light_SetAmbientRadiators(block);
    CHECK(used_count() == 2);
    auto* first = reinterpret_cast<light_tag*>(nf::raw(LightList.used.first));
    CHECK(first->pos.x == 1.0f && first->pos.z == 3.0f && first->radius == 4.0f);  // raiz do raio²
    CHECK(first->r == 255 && first->g == 127 && first->b == 0 && first->kind == 0 && first->life == 0);
}

TEST(psi_lights_load_the_two_first) {
    fresh();
    light_tag* a = point(1.0f, 2.0f);
    light_tag* b = point(3.0f, 4.0f);
    light_tag* c = point(5.0f, 4.0f);
    P32<light_tag> list[3] = {a, b, c};
    psiLight_SetLights(list, 3, 1.0f, 0.5f, 2.0f);
    CHECK(PS2NumLights == 2);
    CHECK(TLight[1][0] == 3.0f && TLight[1][3] == 1.0f);
    CHECK(TColor0[0] == 400.0f && TColor0[1] == 100.0f && TColor0[2] == 200.0f && TColor0[3] == 0.25f);
    CHECK(TColor1[3] == 1.0f / 16.0f);
    // Uma luz: a segunda cor fica zerada; nenhuma: as duas.
    psiLight_SetLights(list, 1, 1.0f, 1.0f, 1.0f);
    CHECK(PS2NumLights == 1 && TColor1[0] == 0.0f && TColor1[3] == 1.0f);
    psiLight_SetLights(nullptr, 0, 1.0f, 1.0f, 1.0f);
    CHECK(PS2NumLights == 0 && TColor0[0] == 0.0f && TColor0[3] == 1.0f);
}

TEST(psi_pregame_sets_fog_and_far_per_level) {
    std::memset(&GameState, 0, sizeof(GameState));
    GameState.level_file = 0x7000002;
    psiPreGameRun();
    CHECK(FogOn == 1 && PS2FogR == 0x24 && PS2FogG == 0x2e && PS2FogB == 0x4c && Ps2Far1 == 9987520.0f);
    CHECK_NEAR(PS2ParticleSize, 9987520.0 * 0.002386635, 1.0);
    GameState.level_file = 0x7000001;
    psiPreGameRun();
    CHECK(FogOn == 0 && Ps2Far1 == 4194240.0f);
}

TEST(light_objects_corona_and_flicker) {
    fresh();
    object_debug_power_on();
    control_init_object_lists();
    glb_world = build_alloc_world();
    static celglist_tag halo{};
    halo.flags = 0x2001;
    level_tag level{};
    level.params[0] = 1;  // faíscas
    level.params[1] = 9;  // canal
    _VECTOR pos{1.0f, 2.0f, 3.0f, 1.0f};
    _VECTOR rot{};
    obj_tag* c = Corona_Create(&pos, &rot, &level, &halo);
    CHECK(c != nullptr && c->type == 1 && c->glist == &halo && c->flags == 0x2001);
    obj_tag* f = Flicker_Create(&pos, &rot, &level, &halo, nullptr);
    CHECK(f != nullptr && f->type == 0x26);
    // Sem luz do mapa (Light_FindMapLight nunca acha): fica acesa, branca, com o canal desligado.
    f->unknown_103[0] = f->unknown_103[1] = f->unknown_103[2] = 0;
    switch_channels[9] = 1;
    for (int i = 0; i < 60; ++i) Flicker_Update(f);
    CHECK(f->unknown_103[0] == 0xff && f->unknown_103[1] == 0xff && switch_channels[9] == 0);
    // O azul anda 10% sem o passo mínimo de 1: para perto do branco.
    CHECK(0xf0 < f->unknown_103[2]);
}
