// O que faltava em Paris (docs/driving.md seção 32): os pedestres (driving/ai/pedestrian.h), os efeitos das instâncias
// (driving/fx/scene_fx.h), os clarões dos canos (driving/fx/muzzle_flash.h), os eventos 252/253/279
// (driving/fx/fx_events.h), o WCollisionMgr::SetCollisionArticle e o RScorchWorld::ScorchWorld: propriedades
// independentes (a regra dos conjuntos pelos nomes das missões, a onda da luz dinâmica com números feitos à mão, as
// máscaras de um Handle montado à mão, a geometria do clarão, um artigo de colisão montado à mão, a marca numa parede ao
// lado de um chão plano). A comparação com o código original fica na parte 28 do nf_driving_check.

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "driving/ai/element_controller.h"
#include "driving/ai/pedestrian.h"
#include "driving/ai/road_network.h"
#include "driving/carp_runtime.h"
#include "driving/fx/bullet_streak.h"
#include "driving/fx/decals.h"
#include "driving/fx/fx_events.h"
#include "driving/fx/fx_render.h"
#include "driving/fx/muzzle_flash.h"
#include "driving/fx/scene_fx.h"
#include "driving/game/triggers.h"
#include "driving/light_manager.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

template <typename T>
void put(uint8_t* base, uint32_t off, T v) {
    std::memcpy(base + off, &v, sizeof v);
}
template <typename T>
T get(const uint8_t* base, uint32_t off) {
    T v;
    std::memcpy(&v, base + off, sizeof v);
    return v;
}

}  // namespace

TEST(driving_pedestrians_paris_has_none) {
    // Os conjuntos pelos nomes das missões do disco: Paris e a corrida no 0 (o conjunto sem tipos no paris.atr).
    CHECK(AICharacterPedestrian_GetPedBase("paris_mis01") == 0);
    CHECK(AICharacterPedestrian_GetPedBase("snow1a_mis3") == 1);
    CHECK(AICharacterPedestrian_GetPedBase("snow2a_mis4") == 2);
    CHECK(AICharacterPedestrian_GetPedBase("junglea_mis13a") == 3);
    CHECK(AICharacterPedestrian_GetPedBase("uw_mis11") == 5);
    CHECK(AICharacterPedestrian_GetPedBase("junglec_mis13c") == 6);
    const RoadNetworkGlobals saved = road_network();
    road_network().valid = 1;
    road_network().segments.clear();
    road_network().num_segments = 0;
    // NUM_PED_TYPES_PS2 = 0 achado: nenhum pedestre, o sistema ligado (o Manage roda e não faz nada).
    pedestrian_globals() = PedestrianGlobals();
    AICharacterPedestrian_Init("paris_mis01", 0, true);
    CHECK(pedestrian_globals().enabled == 1 && pedestrian_globals().num_peds == 0);
    CHECK(pedestrian_globals().num_special == 0 && pedestrian_globals().unported == 0);
    // Sem a rede de ruas: desligado.
    road_network().valid = 0;
    pedestrian_globals() = PedestrianGlobals();
    AICharacterPedestrian_Init("paris_mis01", 0, true);
    CHECK(pedestrian_globals().enabled == 0);
    // Uma rua especial à mão: o jogador entre 130 m e 250 m dela a força; mais perto ou mais longe, não.
    PedestrianGlobals& g = pedestrian_globals();
    g = PedestrianGlobals();
    g.enabled = 1;
    g.num_special = 1;
    g.special[0] = PedSpecialRoad{-1, 3, 100.0f, 0.0f, 0.0f, 0.0f};
    const float near_[3] = {100.0f, 0.0f, 100.0f}, mid[3] = {100.0f, 50.0f, 200.0f}, far_[3] = {100.0f, 0.0f, 400.0f};
    AICharacterPedestrian_Manage(near_);
    CHECK(g.force_special == -1);
    AICharacterPedestrian_Manage(far_);
    CHECK(g.force_special == -1);
    AICharacterPedestrian_Manage(mid);
    CHECK(g.force_special == 0);
    CHECK(AICharacterPedestrian_InvalidateFurthestPed(-1) == -1);
    road_network() = saved;
    pedestrian_globals() = PedestrianGlobals();
}

TEST(driving_dynamic_light_wave) {
    alignas(16) static uint8_t e[0x40];
    std::memset(e, 0, sizeof e);
    put<uint32_t>(e, 0x20, 100);     // o começo
    put<float>(e, 0x24, 0.1f);       // a frequência por quadro
    put<float>(e, 0x28, 0.5f);       // o ciclo ligado
    put<float>(e, 0x2c, 1.0f);       // a base
    put<float>(e, 0x30, 2.0f);       // a amplitude
    const uint32_t p = nf::p32_u(e);
    // 3 quadros depois: a fase 0,3, dentro do ciclo: 1 + 0,3 x 2.
    CHECK_NEAR(DynamicLight_Brightness(p, 103), 1.6f, 1e-5f);
    // 7 quadros: a fase 0,7, fora do ciclo: apagada.
    CHECK(DynamicLight_Brightness(p, 107) == 0.0f);
    // 13 quadros: a fase volta a 0,3.
    CHECK_NEAR(DynamicLight_Brightness(p, 113), 1.6f, 1e-5f);
    // A bandeira 0x800: um triângulo (o dobro da fase, refletido acima de 1).
    put<uint32_t>(e, 0x10, 0x800);
    put<float>(e, 0x28, 1.0f);
    CHECK_NEAR(DynamicLight_Brightness(p, 103), 1.0f + 0.6f * 2.0f, 1e-5f);
    CHECK_NEAR(DynamicLight_Brightness(p, 108), 1.0f + 0.4f * 2.0f, 1e-5f);
    // Com 0x4000 também: até 0,4 sobe 2,5 por unidade.
    put<uint32_t>(e, 0x10, 0x4800);
    CHECK_NEAR(DynamicLight_Brightness(p, 102), 1.0f + 0.5f * 2.0f, 1e-5f);
}

TEST(driving_scene_fx_trigger_masks) {
    // Um objeto de cena com um Handle de três efeitos (clarões, sem galeria): o 0 e o 2 sem zonas, o 1 na zona 0;
    // identificadores 5, 6, 7; todos acionáveis (+0x38), nenhum de dano.
    alignas(16) static uint8_t scene[0x40], handle[0x200];
    std::memset(scene, 0, sizeof scene);
    std::memset(handle, 0, sizeof handle);
    const uint32_t h = nf::p32_u(handle);
    put<uint32_t>(handle, 0x14, h + 0x40);
    put<uint32_t>(handle, 0x18, h + 0x100);
    handle[0x1f] = 3;
    for (int i = 0; i < 3; ++i) handle[0x40 + i * 0x40 + 0x14] = 2;
    put<uint16_t>(handle, 0x100 + 2, 1);
    handle[0x106] = 5;
    handle[0x107] = 6;
    handle[0x108] = 7;
    put<uint64_t>(handle, 0x38, 7);
    put<uint32_t>(scene, 0x28, h);
    const uint32_t s = nf::p32_u(scene);
    std::vector<uint32_t> stim;
    SceneFxEnv saved = scene_fx_env();
    scene_fx_env() = SceneFxEnv();
    static std::vector<uint32_t>* sink;
    sink = &stim;
    scene_fx_env().stimuli_zones = [](uint32_t, uint32_t st, uint32_t zones, uint32_t, uint32_t mode) {
        sink->push_back(st);
        sink->push_back(zones);
        sink->push_back(mode);
    };
    rtime_data().time = 1000;
    // Liga os sem zonas: o 0 e o 2; o prazo de 600 quadros.
    RSceneObj_TriggerFX(-1.0f, s, 0, 0);
    CHECK(get<uint64_t>(handle, 0x20) == 5);
    CHECK(get<uint32_t>(scene, 0x20) == 1600);
    // Pelo identificador 6: o 1.
    RSceneObj_TriggerFX(-1.0f, s, 1, 6);
    CHECK(get<uint64_t>(handle, 0x20) == 7);
    // Desliga os da zona 0 (o bit 16 da máscara): só o 1.
    RSceneObj_TriggerFX(-1.0f, s, 2, 0x10000);
    CHECK(get<uint64_t>(handle, 0x20) == 5);
    // Desliga tudo sem zonas: o prazo volta a zero.
    RSceneObj_TriggerFX(-1.0f, s, 2, 0);
    CHECK(get<uint64_t>(handle, 0x20) == 0 && get<uint32_t>(scene, 0x20) == 0);
    // O tipo 10: o estímulo 0x12 só das zonas novas, uma vez.
    RSceneObj_TriggerFX(-1.0f, s, 10, 3);
    RSceneObj_TriggerFX(-1.0f, s, 10, 3);
    CHECK(stim.size() == 3 && stim[0] == 0x12 && stim[1] == 3 && stim[2] == 2);
    CHECK(get<uint16_t>(scene, 0x1c) == 3);
    // Sem Handle: nada (contado).
    const uint64_t guards = scene_fx_stats().guards;
    put<uint32_t>(scene, 0x28, 0);
    RSceneObj_TriggerFX(-1.0f, s, 0, 0);
    CHECK(scene_fx_stats().guards == guards + 1);
    scene_fx_env() = saved;
}

TEST(driving_muzzle_flash_geometry_and_slots) {
    MuzzleFlashState& st = muzzle_flash();
    const MuzzleFlashState saved = st;
    RMuzzleFlash_StaticInit(st, nullptr);
    RMuzzleFlash_Init(st);
    // O Draw: a faixa de 26 vértices com a matriz (direito = dir x cima, cima, dir, a posição) na escala tamanho / 4.
    const float ident[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const float pos[4] = {1, 2, 3, 1}, dir[4] = {0, 0, 1, 0};
    std::vector<VolatileDraw> out;
    RMuzzleFlash_Draw(st, 4.0f, ident, pos, dir, 2, &out);
    CHECK(out.size() == 1);
    CHECK(out[0].prim == 5 && out[0].count == 26 && out[0].has_matrix);
    CHECK(out[0].texture == muzzle_texture_slot(2));
    CHECK(out[0].alpha == 0x8000000048ull);
    CHECK_NEAR(out[0].matrix[0][0], -1.0f, 1e-6f);
    CHECK_NEAR(out[0].matrix[1][1], 1.0f, 1e-6f);
    CHECK_NEAR(out[0].matrix[2][2], 1.0f, 1e-6f);
    CHECK(out[0].matrix[3][0] == 1.0f && out[0].matrix[3][1] == 2.0f && out[0].matrix[3][2] == 3.0f);
    for (uint32_t c : out[0].colours) CHECK(c == 0x687f7f7fu);
    // O POV parado: o quadrado sem o giro do UV.
    out.clear();
    RMuzzleFlash_DrawPOV(st, 1.0f, ident, pos, 3, true, &out);
    CHECK(out.size() == 1 && out[0].count == 4 && out[0].uv[0] == 0.0f && out[0].uv[2] == 1.0f);
    // A arma de um ator: as vagas vencidas recebem o clarão (fim = passo + 4), a força entre 0,75 e 1,25.
    alignas(16) static uint8_t weapon[0x140];
    alignas(16) static float inst[4][4];
    std::memset(weapon, 0, sizeof weapon);
    std::memcpy(inst, ident, sizeof inst);
    put<uint32_t>(weapon, 0x48, nf::p32_u(inst));
    const MuzzleFlashEnv saved_env = muzzle_flash_env();
    muzzle_flash_env() = MuzzleFlashEnv();
    const float at[3] = {0, 0, 0};
    ActWeapon_StartMuzzleFlash(nf::p32_u(weapon), at, 10);
    ActWeapon_StartMuzzleFlash(nf::p32_u(weapon), at, 10);
    CHECK(get<int32_t>(weapon, 0x84 + 0xc) == 14 && get<int32_t>(weapon, 0x84 + 0x14 + 0xc) == 14);
    CHECK(get<int32_t>(weapon, 0x134) == 17);
    const float k = get<float>(weapon, 0x84 + 0x10);
    CHECK(k >= 0.75f && k < 1.0f);
    // Um passo depois: os dois desenhados, menores (0,4 x força x 0,75).
    out.clear();
    ActWeapon_DrawMuzzleFlashes(st, nf::p32_u(weapon), 11, false, &out);
    CHECK(out.size() == 2);
    CHECK_NEAR(out[0].matrix[0][0], -(0.4f * k * 0.75f) * 0.25f, 1e-5f);
    // Depois do fim, nada.
    out.clear();
    ActWeapon_DrawMuzzleFlashes(st, nf::p32_u(weapon), 14, false, &out);
    CHECK(out.empty());
    muzzle_flash_env() = saved_env;
    st = saved;
}

TEST(driving_set_collision_article_three_reads) {
    // Duas instâncias de colisão (as de render 0 e 1), o artigo da de render 1 com dois 'ca' (0 e 1).
    alignas(16) static WCollisionInstance ci[2];
    alignas(16) static uint32_t rinst[2][16];
    alignas(16) static uint32_t arti, ref;
    alignas(16) static uint8_t ug[0x80];
    std::memset(ci, 0, sizeof ci);
    std::memset(rinst, 0, sizeof rinst);
    std::memset(ug, 0, sizeof ug);
    ci[0].render_index = 0;
    ci[1].render_index = 1;
    ci[1].article = 0x1234;
    const uint32_t hdr[4] = {0x61727469u, 2u, 2u, 1u};
    const uint32_t d0[4] = {0x63610000u, 0x403u, 1u, 0x40u};
    const uint32_t d1[4] = {0x63610001u, 0x403u, 1u, 0x34u};
    std::memcpy(ug, hdr, 16);
    std::memcpy(ug + 0x10, d0, 16);
    std::memcpy(ug + 0x20, d1, 16);
    ref = nf::p32_u(ug);
    arti = nf::p32_u(&ref);
    rinst[1][7] = nf::p32_u(&arti);
    WCollisionMgr mgr{};
    mgr.instances = nf::p32_u(ci);
    mgr.instance_count = 2;
    std::map<uint32_t, uint32_t> saved;
    const uint32_t base = nf::p32_u(rinst[0]);
    CHECK(WCollisionMgr_SetCollisionArticle(mgr, base, nf::p32_u(rinst[1]), 1, saved) == 1);
    CHECK(ci[1].article == nf::p32_u(ug + 0x54));
    CHECK(saved.size() == 1 && saved.begin()->second == 0x1234);
    // Sem colisão; o mapa guarda só o primeiro.
    CHECK(WCollisionMgr_SetCollisionArticle(mgr, base, nf::p32_u(rinst[1]), -1, saved) == 1);
    CHECK(ci[1].article == 0 && saved.begin()->second == 0x1234);
    // Fora dos dois 'ca': recusado (o artigo fica).
    CHECK(WCollisionMgr_SetCollisionArticle(mgr, base, nf::p32_u(rinst[1]), 2, saved) == 0);
    CHECK(ci[1].article == 0);
    // Uma instância de render sem instância de colisão: nada.
    CHECK(WCollisionMgr_SetCollisionArticle(mgr, base, base + 7 * 0x40, 0, saved) == 0);
}

namespace {

// Um chão plano em y = 0 (o tipo 1), sem instância.
class FlatFloor : public WorldCollision {
public:
    bool find_closest_face(WWorldPos& wp, const float*, bool) override {
        const float v[3][3] = {{-50, 0, -50}, {50, 0, -50}, {-50, 0, 50}};
        for (int k = 0; k < 3; ++k) wp.v0[k] = v[0][k], wp.v1[k] = v[1][k], wp.v2[k] = v[2][k];
        wp.surface[0] = 1;
        wp.surface[1] = 0;
        wp.valid = 1;
        wp.instance = 0;
        return true;
    }
    void make_face_at_point(WWorldPos&, const float*) override {}
    float surface_bump_height(const float*, const uint8_t*) override { return 0.0f; }
};

bool always_hit(const float*, WorldCollisionInfo& info) {
    info.hit = 1;
    info.face_type = 1;
    info.face_subtype = 0;
    return true;
}
bool never_hit(const float*, WorldCollisionInfo& info) {
    info.hit = 0;
    return false;
}

}  // namespace

TEST(driving_scorch_world_wall_and_foot) {
    static DecalState s;
    RDecalManager_Construct(s, {});
    FlatFloor floor;
    WWorldPos wp{};
    const DecalStats before = decal_stats();
    // No chão: uma marca, sem descer.
    const float on_floor[4] = {0, 0, 0, 1}, up[4] = {0, 1, 0, 0};
    CHECK(RScorchWorld_ScorchWorld(s, &floor, wp, on_floor, up, 5, 1.0f, always_hit));
    CHECK(decal_stats().added == before.added + 1 && decal_stats().world_feet == before.world_feet);
    // Numa parede a meio metro do chão: a marca e a que desce ao pé dela.
    const float on_wall[4] = {0, 0.5f, 0, 1}, side[4] = {1, 0, 0, 0};
    CHECK(RScorchWorld_ScorchWorld(s, &floor, wp, on_wall, side, 5, 1.0f, always_hit));
    CHECK(decal_stats().added == before.added + 3 && decal_stats().world_feet == before.world_feet + 1);
    // A marca grande que não acha a superfície nas pontas: recusada.
    CHECK(!RScorchWorld_ScorchWorld(s, &floor, wp, on_wall, side, 5, 1.0f, never_hit));
    CHECK(decal_stats().added == before.added + 3);
    // A pequena (até 0,33) não procura as pontas.
    CHECK(RScorchWorld_ScorchWorld(s, &floor, wp, on_floor, up, 5, 0.3f, never_hit));
    CHECK(decal_stats().added == before.added + 4);
}

TEST(driving_switch_and_reflective_events) {
    alignas(16) static AIElement el;
    alignas(16) static uint8_t car[0x100];
    std::memset(&el, 0, sizeof el);
    std::memset(car, 0, sizeof car);
    el.type = 1;
    el.physics = nf::p32_u(car);
    put<uint32_t>(car, 0x6c, 0x1);
    put<uint32_t>(car, 0x70, 0x2);
    const FxEventsEnv saved = fx_events_env();
    fx_events_env() = FxEventsEnv();
    static AIElement* target;
    target = &el;
    fx_events_env().redirect_element = [](uint32_t) { return nf::p32_u(target); };
    PendingEvent on{253, 0, 0.0f, 0, 0, 0};
    on.obj[1] = 0x100;
    on.obj[2] = 3;
    CHECK(fx_event_execute(on));
    CHECK(get<uint32_t>(car, 0x6c) == 0x9 && get<uint32_t>(car, 0x70) == 0);
    PendingEvent off{252, 0, 0.0f, 0, 0, 0};
    off.obj[1] = 0x100;
    off.obj[2] = 4;
    CHECK(fx_event_execute(off));
    CHECK(get<uint32_t>(car, 0x6c) == 0 && get<uint32_t>(car, 0x70) == 0x10);
    // O 279: um veículo sem carro não reflete nada; sem elemento, o carro do jogador.
    fx_events_env().player_sig = []() { return 0x77u; };
    PendingEvent r{279, 0, 0.0f, 0, 0, 0};
    CHECK(fx_event_execute(r));
    CHECK(sim_reflective_object() == 0x77);
    el.physics = 0;
    r.obj[1] = nf::p32_u(&el);
    CHECK(fx_event_execute(r));
    CHECK(sim_reflective_object() == 0);
    fx_events_env() = saved;
}
