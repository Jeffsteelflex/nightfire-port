// Testes do port da pilha de estados (src/engine/gameflow.cpp).

#include "engine/gameflow.h"
#include "engine/view.h"
#include "test_framework.h"

namespace {
void drain() {
    while (GameFlow_Depth() > 0) GameFlow_PopState();
}
}  // namespace

TEST(gameflow_push_pop_and_skip_draw) {
    drain();
    CHECK(GameFlow_GetState() == GF_RUNNING);
    GameFlow_PushState(GF_BOOT, 0, 0);
    CHECK(GameFlow_GetState() == GF_BOOT && GameState.skip_draw);
    GameFlow_PushState(GF_MENU, 0, 0);
    CHECK(GameFlow_GetState() == GF_MENU && !GameState.skip_draw);
    CHECK(GameFlow_PopState() == GF_MENU);
    CHECK(GameFlow_GetState() == GF_BOOT && GameState.skip_draw);
    CHECK(GameFlow_PopState() == GF_BOOT);
    CHECK(GameFlow_PopState() == 0);  // pilha vazia
}

TEST(gameflow_request_states_are_converted) {
    // Os pedidos usam a câmera 6 (o esmaecimento entre fases): a contagem vai para viewer_tag::fade_speed dela e o
    // esmaecimento começa por Camera_SetFade(6, ticks, cor, 1).
    drain();
    viewer_tag fade_view{};
    viewer_tag* saved = glb_viewer[6];
    glb_viewer[6] = &fade_view;
    GameFlow_PushState(GF_COUNTDOWN_REQUEST, 3.5f, 0);
    CHECK(GameFlow_GetState() == GF_COUNTDOWN && fade_view.fade_speed == 3.5f && !GameState.skip_draw);
    GameFlow_PushState(GF_FADE_REQUEST, 80.0f, 0xff);
    CHECK(GameFlow_GetState() == GF_FADE_WAIT && !GameState.skip_draw);
    // Para o preto (0xff = alfa de destino) em 80 ticks: começa em 0, trava (flag 1) e liga os sprites da câmera.
    CHECK(fade_view.fade_state == 1 && fade_view.unknown_234 == 1 && fade_view.sprites == 1);
    CHECK(fade_view.fade == 0.0f && fade_view.fade_rgba[3] == 0);
    CHECK_NEAR(fade_view.fade_speed, 1.0 / 80.0, 1e-7);
    // Travado: um segundo pedido não começa outro esmaecimento e não é empilhado.
    const uint32_t depth = GameFlow_Depth();
    GameFlow_PushState(GF_CAMERA_REQUEST, 30.0f, 0xff);
    CHECK(GameFlow_Depth() == depth);
    fade_view.fade_state = 0;
    GameFlow_PushState(GF_CAMERA_REQUEST, -30.0f, 0xff);
    CHECK(GameFlow_GetState() == GF_CAMERA_ONLY && fade_view.fade == 1.0f && fade_view.fade_rgba[3] == 0xff);
    // Sem a câmera 6 o esmaecimento não começa e o pedido não é empilhado.
    glb_viewer[6] = nullptr;
    const uint32_t depth2 = GameFlow_Depth();
    GameFlow_PushState(GF_FADE_REQUEST, 0, 1);
    CHECK(GameFlow_Depth() == depth2);
    glb_viewer[6] = saved;
    drain();
}

TEST(gameflow_stack_wraps_at_64) {
    drain();
    for (int i = 0; i < 63; ++i) GameFlow_PushState(GF_MENU, 0, 0);
    CHECK(GameFlow_Depth() == 63);
    GameFlow_PushState(GF_MOVIE, 0, 0);  // 64ª posição: o índice volta a 0, como no original
    CHECK(GameFlow_Depth() == 0);
    CHECK(GameFlow_GetState() == GF_RUNNING);
}

TEST(refresh_rate_globals) {
    GS_SetRefreshRate(30, 60);
    CHECK(FRAME_RATE_INT == 30 && VIDEO_FRAME_RATE == 60);
    CHECK_NEAR(FRAME_RATE_MUL, 2.0, 1e-6);
    CHECK_NEAR(FRAME_RATE_DIV, 0.5, 1e-6);
    CHECK_NEAR(REC_FRAME_RATE, 1.0 / 30, 1e-7);
    GS_SetRefreshRate(60, 60);
    CHECK_NEAR(FRAME_RATE_MUL, 1.0, 1e-6);
}
