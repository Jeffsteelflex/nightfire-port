#pragma once
// Carga de fase (ResetMap_*, 0x001d3560-0x001d6954 no ACTION.ELF).
//
// ResetMap_Load é chamada pelo GameFlow_Main a cada tick enquanto o estado é GF_LOAD_MAP. Passos
// (LoadState.270): 0 reinicia tudo e carrega o pacote comum 07000500 (tela de carga); se o pedido
// for um filme, empilha GF_MOVIE e vai para 1/2; 3 reinicia de novo, carrega a fase pedida em
// GameState.load_file, cria os objetos e troca GF_LOAD_MAP por GF_LOCKS_INIT.

#include <cstdint>

void ResetMap_Load();
void ResetMap_GameInit();
// A tela de carga (passo 3 do ResetMap_Load, antes do reinício da fase pedida): a imagem da fase, o rótulo 0x3c1, o
// nome da missão e uma dica, na lista de sprites 99, desenhados quatro vezes na câmera 10; então
// psiStartLoadingScreen (platform/psi_loading.h). Usa Rand_Random (ResetMap_LevelCode2Img) como no PS2.
void ResetMap_GenLoadScreen();
// A imagem da tela de carga da fase (03xxxxxx) e, se pedidos, o nome da missão e uma dica (texto do jogo, ou nulo).
// Sorteia com Rand_Random como o PS2 (uma, duas ou três vezes conforme a fase).
uint32_t ResetMap_LevelCode2Img(uint32_t level, char** mission, char** tip);
// Só do port (depuração, --no-load-screen): o passo 3 do ResetMap_Load pula o ResetMap_GenLoadScreen (nem a tela nem os
// sorteios dela), como antes do porte da tela; para comparar capturas com as de antes.
extern uint8_t g_port_no_load_screen;
uint32_t GetLevelToLoadFromMovieCode(uint32_t movie);
// Pede a carga de uma fase (só nos estados em que o jogo pode trocar de fase). Sem no_movie, as
// fases da campanha passam antes pelo filme de abertura (códigos 071xxxxx).
void ResetMap_LevelToLoad(uint32_t level, int8_t warm, int8_t no_movie);
// Fase inicial do boot: a do GameState ou, sem ela, o menu principal (07000048).
void Reset_MapLoadSettings();

// Globais de outros módulos que ResetMap_GameInit zera ou ajusta (os módulos ainda não foram
// portados; os tamanhos são os dos símbolos).
extern uint8_t MPSettings[476];
extern uint8_t mf_menu;
extern uint32_t switch_NO_DRONES;
extern uint8_t Drone_bDisableSystem;
struct obj_tag;
extern obj_tag* glb_players[4];  // objeto de cada jogador
extern uint8_t IsWarmReset;
struct cel_tag;
extern cel_tag** gbl_room_cels;  // salas do mundo, montada por build_FindCel
extern uint32_t DrawInfo[4];
