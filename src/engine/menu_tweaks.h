#pragma once
// As subpáginas da página de depuração 0x40000007 do menu de jogo (arquivo 08000001; as trapaças em
// engine/menu_cheats.h; só alcançável no port com --debug-menu, F9 numa fase):
//   0x40000044 os ajustes de dano (P_TWEAKS_Handler: o botão "Total Score:" da página): barras para os
//              multiplicadores do dano que os personagens recebem (DroneDamage_*, DroneArmour_*) e que o jogador
//              recebe (Plr_DMod_*), com o valor ao lado; X grava, Y ("More tweaks") vai a 0x40000046
//   0x40000046 os ajustes de tiro da IA (P_TWEAKS2_Handler: DroneFiring)
//   0x4000000c "Previews" (P_NIS_Handler, a lista C_NIS_Handler): os scripts de cena das fases, tocados na fase
//              aberta quando o script está carregado nela
//   a lista das fases da página (0x10000010, C_LBPMMAP_Handler): na versão de venda só "Main Menu" (07000048)
// Os textos dos nomes dos ajustes são os do .rodata do ELF ("Drone Easy"...: rótulos de desenvolvedor, em inglês);
// os itens das duas listas vêm do ELF na partida (game/menu_tables.cpp).

#include <cstdint>

#include "engine/menu.h"
#include "engine/script.h"

// Um item das listas da página de depuração: o texto e o valor (o hash do script ou da fase; 0 um título).
struct MENU_LIST_ITEM {
    const char* text;
    int32_t value;
};
constexpr int kNisListMax = 0x60;
constexpr int kLevelListMax = 8;
extern MENU_LIST_ITEM NisListSource[kNisListMax];   // os 70 itens que C_NIS_Handler acrescenta (0x51)
extern uint32_t NisListCount;
extern MENU_LIST_ITEM LevelListSource[kLevelListMax];  // os de C_LBPMMAP_Handler
extern uint32_t LevelListCount;

extern SCRIPTINFO* FMVScript;       // 0x0030d8f0 a cena tocada por "Previews"
extern uint8_t show_frame_count;    // 0x0030d2fe X na lista liga, quadrado desliga (nada no port o lê)

int32_t P_TWEAKS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_TWEAKS2_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_NIS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_NIS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_LBPMMAP_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);

// Só no port: o estado do módulo (testes).
void menu_tweaks_debug_power_on();
