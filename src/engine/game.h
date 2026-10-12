#pragma once
// Um tick do jogo (Game_Run) e a inicialização do boot (bootup_bootup). GameFlow_Main
// (engine/gameflow.h) chama Game_Run nos estados em que o jogo roda. Ver docs/engine.md,
// "Um quadro".

#include <cstdint>

#include "engine/input.h"
#include "engine/player_stats.h"

extern uint8_t FreezeGame;          // congela o jogo (com switch_AllowFreeze)
extern uint8_t switch_AllowFreeze;
extern uint8_t DrawSnow;            // Env_Update (o vento e as gotas) a cada tick; 1 no jogo
extern uint32_t ScriptCam;          // câmera de script ativa (os textos usam outra atualização)
extern uint8_t CheatInfo[0x30];     // +0x10: GF_MENU desempilha quando não é zero
extern uint16_t SoundInfo[2];      // 0x0030cb50 volumes: [0] a música (C_CHCHMUSIC), [1] 100 (bootup_bootup)
extern uint8_t MPGame[0x1d0];       // estado de cada jogador no multijogador (0x30 bytes cada)

void Game_Run();
void bootup_bootup();

// Chamados por Game_Run, GameFlow_Main e bootup_bootup e ainda não portados (stubs em
// modules_stubs.cpp).
void Sound_UpdateListeners();
void MenuManager_Update();
void MenuManager_Monitor();
void Mission_Update();
void MP_Update();
void Text_Update();
void Text_Update2Line();
void UpdateAllShards();
void Env_Update();  // portada (weather.cpp)
void SSys_Monitor();
void Drone_InitOnce();
void SFXSetMode(int32_t mode);
void Boot_LoadPTPData();
void Boot_GetPTPData(uint8_t** data, uint32_t* size);
