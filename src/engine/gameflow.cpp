// Port de GameFlow_GetState, GameFlow_PushState, GameFlow_PopState e GS_SetRefreshRate.

#include "engine/gameflow.h"

#include <cstring>

#include "engine/game.h"
#include "engine/memory.h"
#include "engine/menu_frontend.h"
#include "engine/modules.h"
#include "engine/resetmap.h"
#include "engine/view.h"
#include "platform/psi.h"

GameFlowHooks g_gameflow_hooks;
GAMESTATE_tag GameState;
uint16_t sloflag = 0;
uint8_t SkipCodeFrame = 0;

uint32_t VIDEO_FRAME_RATE = 60;
uint32_t FRAME_RATE_INT = 60;
float FRAME_RATE = 60.0f;
float FRAME_RATE_DIV = 1.0f;
float FRAME_RATE_MUL = 1.0f;
float REC_FRAME_RATE = 1.0f / 60.0f;

namespace {
uint32_t GameStateStack[64];
uint8_t StackIndex = 0;

// Mesmo switch repetido em PushState, PopState e GameFlow_Main.
void update_skip_draw() {
    switch (GameFlow_GetState()) {
        case GF_BOOT:
        case GF_LOAD_MAP:
        case GF_LOCKS_INIT:
        case 5:
        case GF_LAUNCH_DRIVING:
            GameState.skip_draw = 1;
            break;
        case GF_WAIT_START:
        case GF_MOVIE:
        case GF_FADE_REQUEST:
        case GF_FADE_WAIT:
        case GF_MENU:
        case GF_COUNTDOWN:
        case GF_CAMERA_REQUEST:
        case GF_CAMERA_ONLY:
            GameState.skip_draw = 0;
            break;
        default:
            break;  // outros estados não mexem no flag
    }
}
}  // namespace

// [decomp] 0x001c9e80 GameFlow_GetState
uint32_t GameFlow_GetState() { return StackIndex == 0 ? 0 : GameStateStack[StackIndex - 1]; }

// [decomp] 0x001c9f38 GameFlow_PushState
// A pilha tem 64 posições e volta para 0 ao estourar (o original não falha). Os pedidos usam a câmera 6 (a do
// esmaecimento entre fases): GF_COUNTDOWN_REQUEST grava a contagem em viewer_tag::fade_speed dela (GF_COUNTDOWN a
// desconta); GF_FADE_REQUEST e GF_CAMERA_REQUEST começam Camera_SetFade(6, countdown ticks, fade_param, 1), e o estado
// só é empilhado se o esmaecimento começou.
// Só no port: sem a câmera 6 (testes sem câmeras) a contagem não é gravada (o original escreveria no endereço 0x22c)
// e o esmaecimento não começa (Camera_SetFade devolve 0), então o pedido não é empilhado.
void GameFlow_PushState(uint32_t state, float countdown, uint32_t fade_param) {
    if (state == GF_COUNTDOWN_REQUEST) {
        state = GF_COUNTDOWN;
        if (glb_viewer[6] != nullptr) glb_viewer[6]->fade_speed = countdown;
        GameState.skip_draw = 0;
    } else if (state == GF_FADE_REQUEST || state == GF_CAMERA_REQUEST) {
        if (Camera_SetFade(6, countdown, fade_param, 1) == 0) return;
        state = (state == GF_FADE_REQUEST) ? GF_FADE_WAIT : GF_CAMERA_ONLY;
        GameState.skip_draw = 0;
    }
    GameStateStack[StackIndex] = state;
    ++StackIndex;
    if (StackIndex > 0x3F) StackIndex = 0;
    update_skip_draw();
}

// [decomp] 0x001c9eb0 GameFlow_PopState
// Devolve o estado que estava no topo (o que acabou de sair), ou 0 com a pilha vazia.
uint32_t GameFlow_PopState() {
    if (StackIndex == 0) return 0;
    --StackIndex;
    update_skip_draw();
    return GameStateStack[StackIndex];
}

uint32_t GameFlow_Depth() { return StackIndex; }

// [decomp] 0x0017d1e8 GS_IsPaused
// Com um jogador (player >= 0), o pausado dele em MPGame; com -1, GameState.paused fora do
// multijogador ou todos os jogadores pausados dentro dele.
bool GS_IsPaused(int16_t player) {
    if (player != -1) return MPGame[player * 0x30] != 0;
    uint32_t mp, players;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    if (mp == 0) return GameState.paused != 0;
    std::memcpy(&players, MPSettings + 0x1ac, 4);
    uint16_t all = 1;
    for (uint32_t i = 0; i < players && i < 8; ++i) {
        uint16_t p;
        std::memcpy(&p, MPGame + i * 0x30, 2);
        all &= p;
    }
    return int8_t(all) != 0;
}

namespace {
void draw() {
    if (g_gameflow_hooks.draw != nullptr) {
        g_gameflow_hooks.draw();
    } else {
        Game_Draw();
    }
}
}  // namespace

// [decomp] 0x001c98c8 GameFlow_Main
// O que o estado do topo faz neste tick:
//   GF_BOOT        zera o jogo (bootup_bootup), empilha GF_WAIT_START e pede a fase inicial;
//   GF_WAIT_START  com start_request, empilha GF_LOAD_MAP; senão o jogo roda (é o estado de jogo);
//   GF_LOAD_MAP    ResetMap_Load; GF_LOCKS_INIT: Locks_Init e desempilha;
//   GF_MOVIE       espera o filme acabar (ou o botão de pular); GF_MENU: só entrada e desenho;
//   GF_FADE_WAIT, GF_COUNTDOWN, GF_CAMERA_ONLY: esperam o fade ou a contagem da câmera 6.
// No resto dos casos roda Game_Run (a não ser em SkipCodeFrame) e desenha, se o estado deixar.
// No PC o desenho passa pelo gancho g_gameflow_hooks.draw.
void GameFlow_Main() {
    ++GameState.ticks;
    if (sloflag == 0 && !GS_IsPaused(-1)) {
        ++GameState.frame;
        GameState.video_frames += VIDEO_FRAME_RATE / FRAME_RATE_INT;
    }
    uint8_t pulse = uint8_t(GameState.ticks) & 0x3f;
    if (0x1f < pulse) pulse = uint8_t(0x3f - pulse);
    GameState.pulse = uint8_t(pulse << 1);
    viewer_tag* fade_view = glb_viewer[6];
    switch (GameFlow_GetState()) {
        case GF_BOOT:
            GameState.video_frames = 1;
            GameState.frame = 1;
            GameState.ticks = 1;
            GameState.draws = 0;
            GameState.skip_draw = 0;
            Mem_Init();
            bootup_bootup();
            GameFlow_PushState(GF_WAIT_START, 0.0f, 0);
            Boot_LoadPTPData();
            Reset_MapLoadSettings();
            return;
        case GF_WAIT_START:
            GameState.start_time = uint32_t(psiGetTimeIn100ths());
            if (GameState.start_request != 0) {
                GameFlow_PushState(GF_LOAD_MAP, 0.0f, 0);
                return;
            }
            break;
        case GF_LOAD_MAP:
            ResetMap_Load();
            return;
        case GF_LOCKS_INIT:
            Locks_Init();
            GameFlow_PopState();
            break;
        case GF_MOVIE:
            if (!psiMovieFinished() && Input_Action(-1, 0x19, 1) == 0) break;
            psiStopBackgroundMovie();
            GameFlow_PopState();
            break;
        case GF_FADE_WAIT:
            if (fade_view == nullptr) break;
            if (fade_view->fade_state != 1) GameFlow_PopState();
            break;
        case GF_LAUNCH_DRIVING: {
            uint8_t* data = nullptr;
            uint32_t size = 0;
            Boot_GetPTPData(&data, &size);
            psiLaunchDriving(data, size);
            GameFlow_PopState();
            // Só no port: no PS2 psiLaunchDriving não volta (o DRIVING.ELF assume). Sem ele, a missão de carro
            // fica por pulada e o menu principal volta ao mapa das missões (engine/menu_frontend.h).
            Menu_PortDrivingSkipped(GameState.load_file);
            break;
        }
        case GF_MENU: {
            Input_Update();
            uint32_t done;
            std::memcpy(&done, CheatInfo + 0x10, 4);
            if (done != 0) GameFlow_PopState();
            draw();
            return;
        }
        case GF_COUNTDOWN:
            if (fade_view == nullptr) break;
            if (fade_view->fade_speed <= 0.0f) {
                GameFlow_PopState();
            } else {
                fade_view->fade_speed -= 1.0f;
            }
            break;
        case GF_CAMERA_ONLY:
            if (fade_view == nullptr || fade_view->fade_state == 1) {
                Camera_UpdateAll();
                SkipCodeFrame = 1;
            } else {
                GameFlow_PopState();
                SkipCodeFrame = 0;
            }
            break;
        default:
            break;
    }
    if (SkipCodeFrame == 0) Game_Run();
    if (GameState.skip_draw == 0 && sloflag == 0) draw();
    GameState.skip_draw = 0;
}

// [decomp] 0x0017d2b8 GS_SetRefreshRate
void GS_SetRefreshRate(uint32_t game_rate, uint32_t video_rate) {
    VIDEO_FRAME_RATE = video_rate;
    FRAME_RATE_INT = game_rate;
    FRAME_RATE = float(game_rate);
    FRAME_RATE_DIV = FRAME_RATE * 0.016666668f;
    FRAME_RATE_MUL = 60.0f / FRAME_RATE;
    REC_FRAME_RATE = 1.0f / FRAME_RATE;
}
