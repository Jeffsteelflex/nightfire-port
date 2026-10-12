#pragma once
// GameState (0x002a3768, 88 bytes): estado global da partida. Só os campos já usados pelo código
// portado têm nome; o resto fica como unknown_<deslocamento> até aparecer no código.

#include <cstddef>
#include <cstdint>

struct GAMESTATE_tag {
    uint32_t unknown_00;
    uint32_t stamp;          // 0x04: marca de visita dos portais, incrementada a cada busca
    uint32_t load_file;      // 0x08: pacote a carregar (07xxxxxx); LoadableLoad troca a seção
    uint32_t level_file;     // 0x0c: pacote base da fase (load_file sem a seção de filme)
    uint32_t movie_file;     // 0x10: código de filme guardado por ResetMap_Load
    uint8_t unknown_14;
    uint8_t unknown_15[3];
    uint32_t start_request;  // 0x18: GF_WAIT_START empilha GF_LOAD_MAP enquanto não é zero
    uint32_t menu_page;      // 0x1c: 0x40000036 ao voltar de um filme para o menu
    uint32_t menu_id;        // 0x20: passado a MenuManager_Create
    uint32_t unknown_24;
    uint32_t difficulty;     // 0x28: dificuldade, 2 no boot (Player_InitWeapon muda a mira automática)
    uint8_t pulse;           // 0x2c: onda triangular de 0 a 62 derivada de ticks (GameFlow_Main)
    uint8_t unknown_2d[3];
    uint32_t draws;          // 0x30: quadros desenhados (Game_Draw)
    uint32_t frame;          // 0x34: contador de quadros de lógica
    uint32_t video_frames;   // 0x38: quadros de vídeo de jogo (soma VIDEO_FRAME_RATE / FRAME_RATE_INT)
    uint32_t ticks;          // 0x3c: chamadas a GameFlow_Main
    uint8_t skip_draw;       // 0x40: GameFlow_Main não chama Game_Draw (estados de carga)
    uint8_t unknown_41[3];
    uint32_t unknown_44[2];
    uint32_t start_time;     // 0x4c: psiGetTimeIn100ths do último tick em GF_WAIT_START
    uint8_t paused;          // 0x50: jogo pausado (GS_IsPaused fora do multijogador)
    uint8_t unknown_51;
    uint8_t unknown_52;      // Upgrade_Weapon: com ele a arma 10 vira a 12
    uint8_t unknown_53;
    uint32_t multiplayer;    // 0x54: MP_NeedFile só filtra arquivos quando não é zero
};
static_assert(sizeof(GAMESTATE_tag) == 88);
static_assert(offsetof(GAMESTATE_tag, pulse) == 0x2c && offsetof(GAMESTATE_tag, ticks) == 0x3c);
static_assert(offsetof(GAMESTATE_tag, start_time) == 0x4c);

extern GAMESTATE_tag GameState;
