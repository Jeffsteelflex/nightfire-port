#pragma once
// Pilha de estados do jogo (GameFlow) e variáveis de tempo de quadro do ACTION.ELF.
// Ver docs/engine.md, seções "Boot e laço principal" e "GameFlow: pilha de estados".

#include <cstdint>

#include "engine/gamestate.h"

// Estados de GameFlow_Main (0x001c98c8). Números do original.
enum GameFlowState : uint32_t {
    GF_RUNNING = 0,         // jogo normal (pilha vazia)
    GF_BOOT = 1,            // Mem_Init, bootup_bootup, Boot_LoadPTPData; empilha GF_WAIT_START
    GF_WAIT_START = 2,      // espera o pedido de início; empilha GF_LOAD_MAP
    GF_LOAD_MAP = 3,        // ResetMap_Load
    GF_LOCKS_INIT = 4,      // Locks_Init e desempilha
    GF_MOVIE = 6,           // vídeo em tela cheia
    GF_FADE_REQUEST = 7,    // pedido: vira GF_FADE_WAIT depois de iniciar o fade da câmera
    GF_FADE_WAIT = 8,       // espera o fade terminar
    GF_LAUNCH_DRIVING = 9,  // psiLaunchDriving
    GF_MENU = 10,           // só entrada e desenho
    GF_COUNTDOWN_REQUEST = 11,  // pedido: vira GF_COUNTDOWN com o tempo em ticks (na câmera 6)
    GF_COUNTDOWN = 12,
    GF_CAMERA_REQUEST = 13,     // pedido: vira GF_CAMERA_ONLY depois do fade
    GF_CAMERA_ONLY = 14,
};

// Só no PC: pontos de extensão do laço.
struct GameFlowHooks {
    // Só no PC: quem desenha. Sem gancho, GameFlow_Main chama Game_Draw como no PS2; o nightfire.exe
    // instala um que só marca o pedido, e desenha a cada quadro de vídeo (a lógica fica em 60 Hz).
    void (*draw)() = nullptr;
};

extern GameFlowHooks g_gameflow_hooks;

// GameState.skip_draw (GameState[0x40], engine/gamestate.h): quando verdadeiro, GameFlow_Main não
// chama Game_Draw (estados de carga).

uint32_t GameFlow_GetState();
void GameFlow_PushState(uint32_t state, float countdown, uint32_t fade_param);
uint32_t GameFlow_PopState();
uint32_t GameFlow_Depth();  // só para o port: profundidade atual da pilha
// Um tick: o estado do topo da pilha decide o que roda (carga, filme, fade...); no resto, Game_Run
// e Game_Draw.
void GameFlow_Main();

extern uint16_t sloflag;        // câmera lenta: congela os contadores e não desenha
extern uint8_t SkipCodeFrame;   // GF_CAMERA_ONLY: só as câmeras andam

// GS_IsPaused(-1): o jogo (ou, no multijogador, todos os jogadores) está pausado.
bool GS_IsPaused(int16_t player);

// ---- tempo de quadro (GS_SetRefreshRate, 0x0017d2b8) ----
extern uint32_t VIDEO_FRAME_RATE;
extern uint32_t FRAME_RATE_INT;
extern float FRAME_RATE;
extern float FRAME_RATE_DIV;  // FRAME_RATE / 60
extern float FRAME_RATE_MUL;  // 60 / FRAME_RATE
extern float REC_FRAME_RATE;  // 1 / FRAME_RATE

void GS_SetRefreshRate(uint32_t game_rate, uint32_t video_rate);
