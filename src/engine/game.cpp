// Game_Run e bootup_bootup. Ver game.h.

#include "engine/game.h"

#include <cstdio>
#include <cstring>

#include "engine/door.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/lights.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/resetmap.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_woman.h"

uint8_t FreezeGame = 0;
uint8_t switch_AllowFreeze = 0;
uint8_t DrawSnow = 1;  // 0x0030ca40: 1 no .sdata e ninguém escreve (o --no-env do port o zera)
uint32_t ScriptCam = 0;
uint8_t CheatInfo[0x30];
uint16_t SoundInfo[2];
uint8_t MPGame[0x1d0];

namespace {
void put32(uint8_t* base, uint32_t offset, uint32_t value) { std::memcpy(base + offset, &value, 4); }
}  // namespace

// [decomp] 0x001c9790 Game_Run
// Um tick de lógica: entrada; sem congelamento, menus, missão, textos, fragmentos, neve, e fora de
// pausa os objetos (control_movement_object_handler); por fim as câmeras.
void Game_Run() {
    psiPreGame_Run();
    Input_Update();
    if (FreezeGame == 0 || switch_AllowFreeze == 0) {
        Sound_UpdateListeners();
        if (sloflag == 0) {
            MenuManager_Update();
            MenuManager_Monitor();
            Mission_Update();
            MP_Update();
            if (ScriptCam == 0 && psiMovieFinished()) {
                if (!GS_IsPaused(-1)) Text_Update2Line();
            } else {
                Text_Update();
            }
            UpdateAllShards();
            if (DrawSnow != 0) Env_Update();
            if (!GS_IsPaused(-1)) {
                SSys_Monitor();
                Light_Update();
                control_movement_object_handler(0);
            } else {
                if (ScriptCam != 0) Light_Update();
                psiDecompressWoman();
            }
        }
        Camera_UpdateAll();
    }
    psiPostGame_Run();
}

// [decomp] 0x001c7e78 bootup_bootup
// Zera e preenche o estado do jogo no boot: GameState, MPSettings (opções do multijogador e os
// nomes dos 8 jogadores, "Player n" e "Bot n"), MPGame, CheatInfo, e inicializa entrada,
// estatísticas e IA.
// SoundInfo (os volumes, 100 e 100: só a trapaça C_CHCHMUSIC os lê no port) fica pronto para o módulo de som.
// Parcial: as globais de módulos que o port ainda não tem (GlobalVars, PTPDATA,
// WldGravity, a gravidade em 0x002d8890, as tabelas em 0x002a3a1e...) ficam para quando eles
// chegarem; as constantes (CONSTANT_*) já nascem com os valores certos em vecmath.cpp. Sem o
// módulo de textos, o rótulo "Player" sai vazio.
void bootup_bootup() {
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(CheatInfo, 0, sizeof(CheatInfo));
    std::memset(SoundInfo, 0, sizeof(SoundInfo));
    std::memset(MPSettings, 0, 0x1dc);
    std::memset(MPGame, 0, sizeof(MPGame));
    psiInitTimeIn100ths();
    GameState.menu_id = 0x80000002;
    GameState.menu_page = 0x40000034;
    GameState.difficulty = 2;
    SoundInfo[1] = 100;
    DrawInfo[3] = 1;
    put32(MPSettings, 0x1c0, 2);
    put32(MPSettings, 0x19c, 10);
    put32(MPSettings, 0x1a4, 1);
    GameState.unknown_44[1] = 0;
    put32(MPSettings, 0x1ac, 1);
    SoundInfo[0] = 100;
    DrawInfo[2] = 0;
    put32(MPSettings, 0x1b8, 0);
    put32(MPSettings, 0x1b4, 0);
    put32(MPSettings, 0x1a0, 10);
    put32(MPSettings, 0x198, 0);
    put32(MPSettings, 0x1cc, 0);
    put32(MPSettings, 0x1d0, 0);
    put32(MPSettings, 0x1d4, 0);
    put32(MPSettings, 0x1c8, 1);
    put32(MPSettings, 0x1bc, 0);
    put32(MPSettings, 0x1c4, 1);
    for (uint32_t i = 0; i < 8; ++i) {
        uint8_t* p = MPSettings + i * 0x30;
        put32(p, 0x28, 1);
        put32(p, 0x20, i & 1);
        put32(p, 0x2c, 0);
        put32(p, 0x24, 0);
        const char* label = "Bot";
        uint32_t n = i - 3;
        if (i < 4) {
            label = Txt_BindLabel(0x1c3, 0);
            n = i + 1;
        }
        std::snprintf(reinterpret_cast<char*>(p), 0x20, "%s %d", label != nullptr ? label : "", int(n));
    }
    GameState.start_request = 1;
    Input_Init();
    PlrStat_Init();
    Drone_InitOnce();
    Input_Ready();
    SFXSetMode(1);
}
