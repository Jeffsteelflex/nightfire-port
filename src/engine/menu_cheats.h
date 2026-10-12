#pragma once
// As trapaças do jogo: a página de depuração 0x40000007 do menu de jogo (arquivo 08000001, menu 0x80000002, criada
// com toda pausa) e as caixas de marcar dela, cada uma com o seu tratador C_CHCH*. Ver docs/engine.md, "Trapaças e a
// página de depuração".
//
// Na versão americana a página não é alcançável: nenhum código nem script mostra a 0x40000007 (a lista de combinações
// ComboKeysList, que Input_Check4Combo leria, fica vazia; Input_Ready só a inicializa), e o disco tem o marcador "1" no
// lugar do nome de cada caixa. O que a versão de venda libera vem das medalhas (os bits de `bonus`, engine/
// menu_dossier.h) e dos códigos de "Secret Unlocks" (Menu_SpecialCodenameCheck, com Menu_UpgradeCheat). A página tem
// também a lista das fases (0x10000010, C_LBPMMAP_Handler), "Previews" (os filmes, página 0x4000000c, P_NIS_Handler),
// "Total Score:" (o rótulo do disco num botão que abre os ajustes 0x40000044, P_TWEAKS_Handler) e um botão sem nome
// (0x1000021e) que abre 0x4000004c (P_CHEATMEDAL_Handler: fracassar ou cumprir a missão).
//
// Cada caixa: ao ser criada (0x51) mostra o valor da global; escolhida (0x4b) grava o valor nela.
//   0x1000021d bDebugControls    (1 no boot)   nada no executável lê
//   0x10000019 debug_scanmode    voo livre: o subestado 5 do jogador (Player_SSScanMode) ou de volta ao 0
//   0x100000e7 CheatInfo +4, +8  todas as armas com 999 tiros (Player_EquipWeapon 1-0x47 e 0x4a-0x5e) e colete 50
//   0x100000e8 CheatInfo +0      invencível (Player_HandlePain)
//   0x1000001c hud_state         (1) liga ou desliga os 22 painéis do HUD de cada jogador e a opção da mira
//   0x1000001a switch_SHOW_COORDS as coordenadas no painel da vida
//   0x10000012 DrawInfo[1]       16:9 (switch_CONFIG_WIDESCREEN; Camera_CalcViewAngles das câmeras 0-8)
//   0x10000015 switch_NO_DRONES  marcada = os personagens existem (desmarcada: pausados e escondidos)
//   0x10000016 switch_BLIND_DRONES personagens cegos e surdos
//   0x10000017 Drone_DebugSpr    nada no executável lê
//   0x100000bd Drone_DummyPlayer os personagens miram num jogador falso
//   0x100000c7 switch_AllowFreeze o congelamento de FreezeGame (que nada liga na versão de venda)
//   0x1000001b SoundInfo[0]      o volume da música (100 ou 0; SFXMusicSetVolume)
//   0x10000101 switch_ForceDrawAll desenha as salas sem a visibilidade por portais (até as 64 de VisCelList)
//   0x10000018 Set.214           gravidade zero: o subestado 9 (Player_ZeroG) ou de volta ao 0
//   0x10000169 bDebugLockup      (1) nada no executável lê
//   0x100001f7 bTweakBrightness  nada no executável lê
//   0x10000216 menu_unlock_everything as missões, dificuldades e extras do multijogador todos liberados no menu
//
// Só do port (comentado nas funções):
// - os nomes das caixas e do botão sem nome (PcText Dbg*, inglês; português com [ui] language_pc = pt) no lugar do "1";
// - as seis linhas da lista de 0x4000004c (vazia no disco: escolher nela fracassava a missão);
// - --debug-menu: F9 numa fase abre a pausa nesta página (Menu_PortShowCheats);
// - --unlock-all: os perfis padrão como se todas as missões tivessem sido ganhas com platina (Menu_PortUnlockAll);
// - sem jogador (o menu aberto fora de uma fase), as caixas que mexem nele não fazem nada (g_menu_cheat_guards).

#include <cstddef>
#include <cstdint>

#include "engine/menu.h"

constexpr uint32_t kCheatsPage = 0x40000007;      // a página de depuração do menu de jogo
constexpr uint32_t kCheatMedalPage = 0x4000004c;  // fracassar ou cumprir a missão (P_CHEATMEDAL_Handler)

extern uint8_t hud_state;                    // 0x0030d2a4 (1 no boot)
extern uint32_t bDebugLockup;                // 0x0030d2b0 (1)
extern uint32_t bTweakBrightness;            // 0x0030d2b4
extern uint32_t bDebugControls;              // 0x0030d2b8 (1)
extern uint32_t switch_CONFIG_WIDESCREEN;    // 0x0030ce90

int32_t C_CHCHWS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHDRONES_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHBLIND_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHDEBUG_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHZEROG_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHFLY_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHCOORDS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHMUSIC_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHHUD_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHDUMMY_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHALLOWFREEZE_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHWEAP_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHHEALTH_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHDRAWALL_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHLOCKUP_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHBRIGHT_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHUNLOCK_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_CHCHCONTROLS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);

// ---- só do port ----
// Registra os nomes da página 0x40000007 e as linhas da 0x4000004c (Menu_PortRegisterHandler; o de 0x4000004c chama o
// tratador anterior, P_CHEATMEDAL_Handler, e depois põe as linhas). main.cpp chama depois de MenuPc_Install; os testes
// depois de menu_handlers_debug_power_on.
void MenuCheats_Install();
// --debug-menu, F9: numa fase, abre a pausa (como Player_Update) e mostra a página de depuração por cima dela (voltar
// volta à pausa); com a pausa aberta, só mostra a página. false fora de uma fase ou com outro menu aberto.
bool Menu_PortShowCheats(uint8_t player);
// --unlock-all: cada missão de sp_level cumprida com platina (Menu_SetLevelBonus: as missões liberadas, os bits de
// todas as medalhas e as melhorias) e o resultado copiado para os dois perfis padrão (def_codename +4 missões, +8
// medalhas, +0x19 o jogo terminado), que "Play without saving" e os codinomes novos usam. Um perfil do cartão continua
// com o que gravou.
void Menu_PortUnlockAll();
// "trapacas pagina P hud H invencivel G armas W voo F zero_g Z ..." (relatório das capturas).
int MenuCheats_Describe(char* out, size_t size);

struct MenuCheatGuards {
    uint32_t no_player = 0;  // uma caixa que mexe no jogador sem jogador (o original leria de nulo)
};
extern MenuCheatGuards g_menu_cheat_guards;

void menu_cheats_debug_power_on();
