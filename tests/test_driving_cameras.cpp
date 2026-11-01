// As câmeras e os gatilhos que faltavam a Paris (docs/driving.md seção 30): propriedades independentes, sem dados do
// jogo (a conferência contra o original é a parte 26 do nf_driving_check): a troca de câmera pelo triângulo (o próximo e o
// anterior selecionáveis, o anterior do modo 0 pelo divu do PS2), a entrada do carro, a velocidade da âncora no trilho, os
// gatilhos tocados por um ponto (o raio do gatilho não conta) e por um corpo simples, e os anéis da luneta.

#include <cmath>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "driving/ai/ground_vehicle.h"
#include "driving/camera/camera_ini.h"
#include "driving/camera/player_camera.h"
#include "driving/game/rail.h"
#include "driving/game/triggers.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "driving/render/path_engine.h"
#include "driving/render/sniper_zoom.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

// Uma tabela de modos montada à mão: `selectable[i]` diz quais aparecem no triângulo; todos do tipo Heli.
struct ModeTable {
    std::vector<CameraModeInfo> modes;
    std::unique_ptr<CameraTables> tables{new CameraTables()};
    explicit ModeTable(const std::vector<int>& selectable) {
        modes.resize(selectable.size() + 1);
        for (size_t i = 0; i < selectable.size(); ++i) {
            modes[i] = CameraModeInfo{};
            modes[i].type = kCamHeli;
            modes[i].selectable = uint8_t(selectable[i]);
            modes[i].look_back = 1;
        }
        tables->tables.modes = modes.data();
        tables->tables.mode_count = int32_t(selectable.size());
        tables->tables.cinematic_mode_index = -1;
        tables->tables.missile_index = -1;
    }
};

std::unique_ptr<PlayerCamera> camera_on(ModeTable& t, int32_t mode) {
    std::unique_ptr<PlayerCamera> c(new PlayerCamera());
    c->tables = t.tables.get();
    c->d.mode = mode;
    return c;
}

}  // namespace

TEST(driving_camera_next_mode_skips_the_ones_the_triangle_does_not_show) {
    ModeTable t({1, 0, 0, 1});
    auto c = camera_on(t, 0);
    c->NextCameraMode(0);
    CHECK(c->director_queue.size() == 1 && c->director_queue.back().mode == 3);
    auto c3 = camera_on(t, 3);
    c3->NextCameraMode(2);
    CHECK(c3->director_queue.size() == 1 && c3->director_queue.back().mode == 0);  // a volta
    CHECK(c3->director_queue.back().delay == 2);
    // Num modo que o triângulo não mostra (uma animação, o Tumble) o pedido não sai.
    auto c1 = camera_on(t, 1);
    c1->NextCameraMode(0);
    CHECK(c1->director_queue.empty());
}

TEST(driving_camera_previous_mode_of_zero_is_the_unsigned_remainder) {
    // O PS2 faz (modo - 1) % contagem com divu: do modo 0, (2^32 - 1) % contagem, não o último modo.
    // 4294967295 = 22 x 195225786 + 3 = 5 x 858993459 + 0 = 7 x 613566756 + 3.
    const std::pair<int, int> kCases[] = {{22, 3}, {5, 0}, {7, 3}};
    for (const auto& [n, want] : kCases) {
        ModeTable t(std::vector<int>(size_t(n), 1));
        auto c = camera_on(t, 0);
        c->PrevCameraMode(0);
        CHECK(c->director_queue.size() == 1 && c->director_queue.back().mode == want);
    }
    // Saindo do 1, o anterior comum.
    ModeTable t(std::vector<int>(22, 1));
    auto c = camera_on(t, 1);
    c->PrevCameraMode(0);
    CHECK(c->director_queue.back().mode == 0);
}

TEST(driving_camera_drive_input_turns_the_dashboard_and_lifts_the_heli_arm) {
    simulation_reset(60);
    Simulation& sim = simulation();
    sim.steps = 10;
    ModeTable t({1, 1});
    auto c = camera_on(t, 0);
    CameraContext ctx;
    ctx.sim = &sim;
    c->DriveCamInputHandler(ctx, 1, 0.5f);  // o volante: o Dashboard gira para o outro lado
    float steer;
    std::memcpy(&steer, &c->state.unknown4c[0], 4);
    CHECK(steer == -0.5f);
    c->DriveCamInputHandler(ctx, 0x1b, 1.0f);  // o freio de mão: a câmera Heli sobe por 120 quadros
    CHECK(c->state.up_count == 0x78 && c->state.up_hold == 1);
    c->DriveCamInputHandler(ctx, 0x1c, 0.0f);
    CHECK(c->state.up_hold == 0 && c->state.up_count == 0x78);
    c->DriveCamInputHandler(ctx, 0x29, 1.0f);  // olhar para trás
    CHECK(c->state.look_back == 1 && (c->d.mode_change & 2) != 0 && c->state.unknown1c == 0);
    // Nos três primeiros passos (e na pausa) nada além do soltar responde.
    sim.steps = 2;
    auto p = camera_on(t, 0);
    p->DriveCamInputHandler(ctx, 1, 0.7f);
    p->DriveCamInputHandler(ctx, 0x26, 1.0f);
    std::memcpy(&steer, &p->state.unknown4c[0], 4);
    CHECK(p->state.paused == 1 && steer == 0.0f && p->director_queue.empty());
    simulation_reset(60);
}

TEST(driving_camera_anchor_velocity_follows_the_first_car_rail) {
    simulation_reset(60);
    Simulation& sim = simulation();
    RigidBody* rb = GetRigidBody(sim, 0);
    *rb = RigidBody{};
    rb->info = GetRigidBodyInfo(sim, 0);
    rb->vel[0] = 3.0f;
    rb->vel[2] = 4.0f;
    std::unique_ptr<AIGroundVehicle> v(new AIGroundVehicle());
    std::unique_ptr<AISplinePath> sp(new AISplinePath());
    std::unique_ptr<RPathHandle> h(new RPathHandle());
    std::memset(sp.get(), 0, sizeof(AISplinePath));
    std::memset(h.get(), 0, sizeof(RPathHandle));
    h->velocity[0] = -7.0f;
    h->velocity[1] = 1.0f;
    sp->handle = nf::p32_u(h.get());
    static AIGroundVehicle* s_vehicle = nullptr;
    s_vehicle = v.get();
    std::unique_ptr<CameraAnchor> a(new CameraAnchor());
    a->kind = 1;
    a->body = 0;
    ModeTable t({1});
    auto c = camera_on(t, 0);
    c->SetAnchor(a.get());
    CameraContext ctx;
    ctx.sim = &sim;
    ctx.player_vehicle = [] { return s_vehicle; };
    // Sem trilho: a velocidade do corpo da âncora.
    v->spline_path = 0;
    const float* got = c->GetAnchorLinearVelocity(ctx);
    CHECK(got[0] == 3.0f && got[2] == 4.0f);
    // No trilho: a do RPathHandle (a do corpo não conta), para qualquer âncora de corpo rígido.
    v->spline_path = nf::p32_u(sp.get());
    got = c->GetAnchorLinearVelocity(ctx);
    CHECK(got[0] == -7.0f && got[1] == 1.0f && c->rail_velocity_reads == 1);
    // Num corpo simples, a dele.
    SimpleRigidBody* b = GetSimpleRigidBody(sim, 5);
    std::memset(b, 0, sizeof(SimpleRigidBody));
    b->vel[1] = 9.0f;
    a->kind = 0;
    a->flags = 1;
    a->body = 5;
    c->SetAnchor(a.get());
    got = c->GetAnchorLinearVelocity(ctx);
    CHECK(got[1] == 9.0f);
    simulation_reset(60);
}

TEST(driving_trigger_point_check_uses_only_the_instance_radius) {
    WTrigger t{};
    t.position[0] = 10.0f;
    t.position[1] = 2.0f;
    t.position[2] = 0.0f;
    t.radius = 50.0f;  // não entra
    t.height = 3.0f;
    const float near[3] = {12.0f, 2.5f, 0.0f}, far[3] = {16.0f, 2.5f, 0.0f};
    CHECK(WTriggerManager_CheckCollidePoint(3.0f, near, t));
    CHECK(!WTriggerManager_CheckCollidePoint(3.0f, far, t));  // 6 m fora do raio 3, dentro dos 50 do gatilho
    // Em altura: a base do gatilho até o ponto + raio, o topo acima do ponto - raio.
    const float below[3] = {10.0f, -1.5f, 0.0f}, above[3] = {10.0f, 8.5f, 0.0f};
    CHECK(!WTriggerManager_CheckCollidePoint(3.0f, below, t));
    CHECK(!WTriggerManager_CheckCollidePoint(3.0f, above, t));
    // Com os alcances de cima e de baixo à parte.
    CHECK(WTriggerManager_CheckCollidePoint3(3.0f, 4.0f, 0.5f, below, t));
    CHECK(!WTriggerManager_CheckCollidePoint3(3.0f, 3.0f, 0.5f, below, t));
}

TEST(driving_trigger_simple_body_check_adds_both_radii) {
    WTrigger t{};
    t.radius = 2.0f;
    t.height = 4.0f;
    SimpleRigidBody b{};
    b.radius = 1.0f;
    b.pos[0] = 2.9f;
    b.pos[1] = 1.0f;
    CHECK(WTriggerManager_CheckCollideSimple(b, t));
    b.pos[0] = 3.1f;
    CHECK(!WTriggerManager_CheckCollideSimple(b, t));
    b.pos[0] = 0.0f;
    b.pos[1] = -1.5f;  // o topo do corpo abaixo da base
    CHECK(!WTriggerManager_CheckCollideSimple(b, t));
    b.pos[1] = 4.9f;  // a base do corpo abaixo do topo
    CHECK(WTriggerManager_CheckCollideSimple(b, t));
}

TEST(driving_sniper_zoom_rings_have_their_radii_and_close) {
    std::unique_ptr<SniperZoomData> z(new SniperZoomData());
    RSniperZoom_Construct(*z);
    CHECK(z->alpha == 0xff && z->on == 0);
    SniperZoomView v;
    v.cursor_x = 10.0f;
    v.cursor_y = -6.0f;
    RSniperZoom_SetupVerts(*z, v);
    const float cx = 320.0f + 10.0f, cy = 224.0f - 6.0f;
    auto radius = [&](const float p[4]) { return std::sqrt((p[0] - cx) * (p[0] - cx) + (p[1] - cy) * (p[1] - cy)); };
    for (int i = 0; i < kSniperZoomVerts; ++i) {
        const bool odd = (i & 1) != 0;
        CHECK_NEAR(radius(z->pos_inner[i]), odd ? 150.0f : 65.0f, 0.01f);
        CHECK_NEAR(radius(z->pos_outer[i]), odd ? 530.0f : 150.0f, 0.02f);
        CHECK_NEAR(z->uv_inner[i][0], z->pos_inner[i][0] / 640.0f, 1e-6f);
        CHECK_NEAR(z->uv_outer[i][1], z->pos_outer[i][1] / 448.0f, 1e-6f);
        CHECK(z->pos_inner[i][2] == 1.0f && z->pos_inner[i][3] == 1.0f);
    }
    // 21 passos de 1/20 de volta: o último par repete o primeiro (a faixa fecha o anel).
    CHECK_NEAR(z->pos_inner[40][0], z->pos_inner[0][0], 0.01f);
    CHECK_NEAR(z->pos_outer[41][1], z->pos_outer[1][1], 0.01f);
    // O de dentro: o centro transparente (cinza 0x7f, alfa 0), a borda do meio com metade do alfa; o de fora escurece.
    CHECK(z->colour_inner[0] == 0x007f7f7fu);
    CHECK((z->colour_inner[1] >> 24) == 0x7f && (z->colour_inner[1] & 0xff) == 0xb9 / 2);
    CHECK(z->colour_outer[0] == z->colour_inner[1]);
    CHECK((z->colour_outer[1] & 0xffffff) == 0);
}
