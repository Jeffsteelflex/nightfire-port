#pragma once
// O menu principal (pacote 07000048, arquivo de menu 08000002): os tratadores do caminho de um jogador e as
// funções do jogo que eles usam. Ver docs/engine.md, "Menu principal", e engine/menu.h para o sistema de
// menus.
//
// O caminho (páginas do menu 0x80000002; bootup_bootup deixa GameState.menu_page = 0x40000034 e o ResetMap_Load
// do 07000048 cria o menu nela, status 5):
//   0x40000034 idioma (P_LANGUAGE_Handler; na versão americana passa direto, Txt_SetLanguage)
//   0x40000048 o cartão de memória na partida (P_PS2MEMCARDINIT_Handler: psiDoBootupCheck)
//   0x40000032 os filmes de abertura (P_INTRO_Handler: 07370048 e 07380048)
//   0x40000009 "Press START" (P_START_Handler; o rótulo de tela cheia tem o script de escolher que mostra a
//              0x4000004a; 45 s sem entrada mostram 0x40000035, o filme de demonstração, P_ATTRACT_Handler)
//   0x4000004a procura um perfil no cartão (P_PARISENUM_Handler) e mostra 0x40000043
//   0x40000043 o filme 073b0048 (P_ESTHERO_Handler) e o menu principal
//   0x40000002 NightFire / Multiplayer / Codenames (P_MAIN_Handler; C_GONIGHTFIRE/GOMULTIPLAYER/GOCODENAMES)
//   0x40000025 a roda de codinomes da campanha (P_NFSELECT_Handler, C_SBNFCN_Handler): os dois padrões
//              ("New Codename" e "Default") e os do cartão; escolher um do cartão o lê (LS_Load)
//   0x40000023 a dificuldade (P_NFDFCTY_Handler, C_SBNFDFCTY_Handler: GameState.difficulty)
//   0x4000001c o mapa das missões (P_NFMAP_Handler, C_SBNFMAP_Handler: sp_level; escolher uma missão liberada
//              chama ResetMap_LevelToLoad e fecha o menu)
//   0x4000001b os codinomes (P_CNSELECT_Handler, C_SBCNSELECT_Handler: editar, criar, apagar)
//   0x40000020 o teclado da tela para o nome (P_CNNAME_Handler, C_KEYBOARD_Handler; End grava o perfil novo)
//   0x4000001d o menu do codinome (P_CNMENU_Handler, C_SBCNOPTIONS_Handler: as opções e "Save Codename")
//   0x40000022 "Controller Setup" (P_CNCONTROLS_Handler, C_RBCONTROL_Handler; menu_options.cpp)
//   0x4000002d "Advanced Options" (P_CNOPTIONS_Handler), 0x4000002e "Multiplayer Options" do codinome
//              (P_CNMPOPTIONS_Handler), 0x40000031 "AV Options" (P_CNAVOPTIONS_Handler), 0x40000047 o ajuste de tela
//              (P_SCREENADJUST_Handler, C_SBSCREEN_Handler), 0x40000030 os créditos (P_CREDITS_Handler)
//   0x4000002a a caixa do cartão (PS2_DoCardFlow: "Loading data..."; C_LBERROPTIONS_Handler)
//   0x40000053 o fim do jogo (P_WINGAME_Handler: o filme 073f0048 e os créditos 0x40000030)
//
// O perfil: `ls` (MENU_LS) diz a operação do cartão em andamento (Menu_UpdateMessageBox: ler, gravar, listar,
// apagar), o codinome e a página seguinte; def_codename tem os dois perfis padrão (0x38 bytes: rótulo do nome,
// missões, medalhas, volumes, opções), Menu_MapDefaultCodename os aplica; os nomes da roda ficam em
// codename_buf. O que vai para o cartão: engine/loadsave.h; o cartão: platform/psi_card.h.
//
// Filmes: o vídeo (PSS/MPEG-2) não foi portado. psiStartBackgroundMovie não toca nada e psiMovieFinished é
// verdade, então cada página de filme (P_INTRO, P_ATTRACT, P_ESTHERO, P_TRAILER, P_FMVPLAYER, P_WINGAME) segue
// no tick seguinte como se o filme tivesse acabado; o fundo animado das páginas (o filme 07350048 do menu)
// fica preto.
//
// Só no port (comentado nas funções):
// - C_GOMULTIPLAYER_Handler: o multijogador não foi portado; escolher mostra a caixa de mensagem do jogo com
//   "Multiplayer is not available yet." em vez de ir à página 0x40000019.
// - Missões de carro (09xxxxxx, DRIVING.ELF, não portado): o jogo chama psiLaunchDriving; o port dá a missão
//   por pulada (Menu_SetLevelBonus sem medalha: libera a seguinte) e volta ao mapa das missões com uma caixa
//   de aviso (Menu_PortDrivingSkipped, engine/gameflow.cpp).

#include <cstddef>
#include <cstdint>

#include "engine/menu.h"

// Um item das rodas do menu principal (0x18 bytes): as missões (sp_level), as dificuldades, as opções do
// codinome (cn_options).
struct M_ITEM {
    uint32_t image;     // 0x00 a imagem (textura 03xxxxxx) ao lado da roda
    uint32_t name;      // 0x04 rótulo do nome
    uint32_t text;      // 0x08 rótulo da descrição
    uint32_t value;     // 0x0c o valor (a fase, a dificuldade, a página)
    uint8_t enabled;    // 0x10 liberado
    uint8_t pad[3];
    uint32_t locked;    // 0x14 rótulo da descrição quando bloqueado
};
static_assert(sizeof(M_ITEM) == 0x18);

// O perfil em uso e a operação do cartão (ls, 0x002e1ad0; os bytes crus em engine/menu.h).
struct MENU_LS {
    uint32_t active;      // 0x00 uma operação do cartão em andamento (Menu_UpdateMessageBox)
    uint32_t count;       // 0x04 nomes em codename_buf (os dois padrões e os do cartão)
    uint32_t next_page;   // 0x08 a página a mostrar no fim (0: nenhuma)
    uint32_t next_flags;  // 0x0c as flags da mensagem 0x44
    uint32_t slot;        // 0x10 o codinome: 0/1 os padrões, 2+ do cartão, 999 nenhum (gravar um novo)
    M_CONTROL* control;   // 0x14 o controle que recebe os nomes da listagem
    char name[0x20];      // 0x18 o codinome
    uint8_t unknown_38;
    uint8_t op;           // 0x39 0 ler, 1 gravar, 2 listar, 3 apagar, 4 listar só o primeiro
    uint8_t done;         // 0x3a a operação terminou
    uint8_t stale;        // 0x3b a lista de nomes precisa ser refeita
    uint8_t unknown_3c;
    uint8_t can_save;     // 0x3d há onde gravar (o menu principal liga)
    uint8_t unknown_3e[2];
    uint8_t mini;         // 0x40 a minimissão de demonstração
    uint8_t legal_once;   // 0x41 o texto legal da página "Press START" já esperou
    uint8_t keep;         // 0x42 a página do nome fica depois de uma falha
    uint8_t pad;
};
static_assert(sizeof(MENU_LS) == 0x44);
inline MENU_LS& menu_ls() { return *reinterpret_cast<MENU_LS*>(ls); }

// O estado do "íris" (a imagem que gira ao lado das rodas, menu_iris 0x002e1c30).
struct MENU_IRIS {
    uint32_t unknown_00;
    int32_t frame;       // 0x04 ticks (de dois em dois no PS2 a 60 Hz)
    int32_t step;        // 0x08 a imagem atual (1-9 abrindo e fechando; 10 parado)
    uint32_t unknown_0c;
    uint8_t hold;        // 0x10 não avança
    uint8_t busy;        // 0x11 girando
    uint8_t pad[2];
};
static_assert(sizeof(MENU_IRIS) == 0x14);

constexpr int kCodenames = 258;  // os dois padrões e até 256 do cartão
extern char codename_buf[kCodenames][0x20];  // 0x002e1c58
extern M_ITEM cn_options[7];                 // 0x002dfc28 as opções do codinome (copiadas do ELF)
extern M_ITEM difficulty[3];                 // 0x002df4c0 Operative, Agent, 00 Agent (copiadas do ELF)
extern MENU_IRIS menu_iris;
extern uint8_t movie_skip;                   // 0x002e1c49 o filme pode ser pulado (Menu_StopMovie)
extern uint8_t movie_sound;                  // 0x002e1c4a o filme troca a música do menu
extern uint32_t movie_counter;               // 0x002e1c4c ticks antes de poder pular
extern uint8_t menu_unlock_everything;       // 0x0030d2a7 trapaça: tudo liberado
extern uint8_t bDisplayLangMenu;             // 0x0030d2a8 mostra a escolha de idioma (0 na versão americana)
extern uint8_t bSkipAttract;                 // 0x0030d2a6
extern uint8_t RUN_MINI_MISSION;             // 0x0030d28c a minimissão de demonstração (0)
extern uint8_t cheat_mode;                   // 0x0030d528 o teclado digita um código (Secret Unlocks)
extern uint8_t cn_modified_flag;             // 0x0030d527 o codinome mudou (sair pergunta)
extern uint8_t del_when_keypressed;          // 0x0030d529 a primeira tecla apaga o nome sugerido
extern char codename_entry[0x10];            // buffer (0x0034e028): o nome digitado (até 8 letras)
extern uint32_t FlagsToDisplay;              // 0x0030d338 os idiomas da escolha (0: DefaultFlags)
extern uint32_t DefaultFlags;                // 0x0030d33c
extern uint32_t medal_from_cheat;            // 0x0030cc74
extern uint32_t movie_hashcode;              // 0x0030d374 o filme de P_FMVPLAYER_Handler
extern uint32_t enum_state;                  // 0x0030d22c Menu_EnumSaves
constexpr int kCodenameMax = 8;              // C_KEYBOARD_Handler: até 8 letras
extern uint8_t was_inverted;                 // 0x0030d535 a inversão mostrada em "Controller Setup"
extern int32_t PS2OldX;                      // 0x0030d578 P_SCREENADJUST_Handler: a posição da tela ao entrar
extern int32_t PS2OldY;

// Uma linha dos créditos (Menu_SetupCredits, 12 bytes).
struct CREDIT_tag {
    const char* name;    // 0x00 o nome (ou a linha toda)
    const char* title;   // 0x04 o cargo (a coluna da direita)
    uint8_t name_font;   // 0x08 1 fonte 1, 2 fonte 3, outro fonte 2 (P_CREDITS_Handler)
    uint8_t title_font;  // 0x09
    uint8_t single;      // 0x0a uma linha só, larga (o cargo escondido)
    uint8_t pad;
};
static_assert(sizeof(CREDIT_tag) == 12);
constexpr int kCreditsMax = 0x242;  // as linhas do disco
// Só no port: as linhas como Menu_SetupCredits do ELF as monta, tiradas na partida por game/menu_tables.cpp (um
// rótulo do *Txt.dat, lido na hora, ou uma string do ELF, copiada; 0/nulo: nenhum texto).
struct CREDIT_SOURCE {
    uint32_t name_label;
    const char* name_text;
    uint32_t title_label;
    const char* title_text;
    uint8_t name_font;
    uint8_t title_font;
    uint8_t single;
};
extern CREDIT_SOURCE CreditsSource[kCreditsMax];
extern uint32_t CreditsSourceCount;
extern CREDIT_tag* ptr_credits;              // 0x0030d268 as linhas no heap do menu (Menu_InitCredits zera)

// ---- tratadores (menu_frontend.cpp) ----
int32_t P_LANGUAGE_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_LANGUAGE_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_INTRO_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_ATTRACT_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_START_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_PARISENUM_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_ESTHERO_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_MAIN_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_GONIGHTFIRE_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_GOMULTIPLAYER_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_GOCODENAMES_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_FMV_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_TRAILER_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_FMVPLAYER_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_NFSELECT_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBNFCN_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_NFDFCTY_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBNFDFCTY_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_NFMAP_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBNFMAP_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_WINGAME_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_CHEATMEDAL_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
// ---- os codinomes e o cartão (menu_codename.cpp) ----
int32_t P_PS2MEMCARDINIT_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_LBERROPTIONS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_CNSELECT_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBCNSELECT_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_CNMENU_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBCNOPTIONS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_CNNAME_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_KEYBOARD_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_KEYPAD_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
// ---- as páginas de opções do codinome e os créditos (menu_options.cpp) ----
int32_t P_CNCONTROLS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_RBCONTROL_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_CNOPTIONS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_CNMPOPTIONS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_CNAVOPTIONS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_SCREENADJUST_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBSCREEN_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_CREDITS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);

// ---- funções do jogo ----
void PS2_DoCardFlow(M_MANAGER* m, uint32_t state, MENU_LS* l);
void Menu_UpdateMessageBox(uint8_t index, uint16_t mask, uint8_t no);
uint32_t Menu_EnumSaves(uint8_t index, int8_t one);
void Menu_FlushEnum();
void Menu_PutCodenamesIntoControl(M_CONTROL* c);
void Menu_MapDefaultCodename(uint8_t player, uint8_t slot, uint32_t what);
uint32_t Menu_IsValidCodename(char* name);
uint32_t Menu_SpecialCodenameCheck(char* code);           // menu_secret.cpp
uint64_t Menu_UpgradeCheat(uint64_t bits, uint32_t bit, uint8_t count);
void Menu_SelectCodenameInControl(uint8_t index, char* name, uint32_t id);
void Menu_UpdateWheel(uint8_t index, M_CONTROL* c, M_ITEM* items, uint32_t labels, uint32_t image, uint32_t text,
                      uint32_t iris, int8_t animate);
void Menu_UpdateCodenameWheel(uint8_t index, M_CONTROL* c, uint32_t labels, uint32_t image, uint32_t text, uint32_t iris,
                              int8_t animate);
void Menu_StartIris(uint32_t op, uint8_t index, uint32_t id);
void Menu_PlayIris(int8_t advance, uint8_t index, uint32_t id);
void Menu_ChangePageCloseIris(uint32_t page, uint8_t index, uint32_t id);
void Menu_AddItemsToControl(M_CONTROL* c, M_ITEM* items, uint16_t count, uint16_t first, uint8_t all);
uint32_t Menu_SelectItemInControl(M_CONTROL* c, M_ITEM* items, uint16_t count, uint32_t value);
M_ITEM* Menu_GetItemFromHash(M_ITEM* items, uint32_t value, uint32_t count);
void Menu_SetNightfireStatus(uint32_t bits);
uint32_t Menu_WillRunMiniMission();
uint32_t Menu_RunMiniMission(uint8_t index);
uint32_t Menu_IsDrivingLevel(uint32_t level);  // portada (modules_stubs.cpp)
void Menu_PlayMovie(uint32_t movie, int8_t skip, int8_t loop, int8_t sound, int8_t freeze, uint32_t counter);
void Menu_StopMovie(uint8_t force);
void Menu_UpdateMovieCounter();
CREDIT_tag* Menu_SetupCredits(uint32_t* count);
void Menu_InitCredits();
void Menu_KillCredits();

uint32_t Menu_IsBotGood(uint32_t bot);

// ---- só no port ----
// Missão de carro escolhida no mapa (psiLaunchDriving no PC, engine/gameflow.cpp): marca a missão como feita
// (Menu_SetLevelBonus sem medalha, libera a seguinte) e pede o menu principal no mapa das missões, que mostra o
// aviso. 0 se `level` não é de carro.
uint32_t Menu_PortDrivingSkipped(uint32_t level);
// "frontend pagina X perfil N codinome \"...\" op O fim D cartao ..." (relatório das capturas).
int Menu_FrontEndDescribe(char* out, size_t size);
// As tabelas tiradas do ELF na partida (game/menu_tables.cpp: os créditos e as listas da página de depuração): uma
// cópia de `s` que dura até Menu_PortClearTables, que também esvazia as tabelas.
const char* Menu_PortTableString(const char* s);
void Menu_PortClearTables();
// Os códigos de "Secret Unlocks" na ordem do original (nulo depois do último; menu_secret.cpp).
const char* Menu_PortSecretCode(uint32_t i);
// O estado inicial dos módulos (testes): menu_frontend.cpp, menu_codename.cpp e menu_options.cpp (as tabelas do ELF
// ficam).
void menu_frontend_debug_power_on();
void menu_codename_debug_power_on();
void menu_options_debug_power_on();
