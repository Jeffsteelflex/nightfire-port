#pragma once
// Página só do port: as opções do PC dentro do menu de pausa (docs/port.md, "Opções do PC no menu").
//
// A pausa do jogo (página 0x4000004b, arquivo 08000001) ganha uma quarta linha na lista principal, "OPTIONS",
// depois de CONTINUE/RESTART/QUIT: os tratadores P_PAUSE_Handler e C_GCPAUSE_Handler passam por um embrulho
// (MenuPc_Install) que acrescenta a linha ao entrar na página e, quando ela é escolhida, mostra a página de
// opções 0x40000060; o resto vai ao tratador anterior sem mudança.
//
// A página de opções é montada como a pausa e com as peças do jogo, na primeira vez que é mostrada em cada
// menu (MenuPc_ShowPage): o painel da pausa (0x03000070, no tamanho do arquivo), a faixa de abas com botões
// do conjunto 2 na fonte 2 como MISSION/OBJECTIVES/CONTROLS/SCORE (VÍDEO, GRÁFICOS, INTERFACE, ÁUDIO,
// CONTROLES, TECLAS) e a barra de ajuda com os símbolos de botão. Cada aba é uma lista de linhas como as
// páginas de opções do menu principal (08000002: "AV Options", "Advanced Options"): o nome à esquerda num
// rótulo desabilitado da fonte 3 com o mesmo id do controle (acende com o foco dele) e o controle à direita:
// - barra (MCT_SCROLL proporcional): a barra de volume do "AV Options" (as setas do conjunto 3 e o
//   preenchimento laranja de 0x03000042), num conjunto de componentes que a página cria com as peças do
//   conjunto 10 do 08000002, que o 08000001 não tem (MenuPc_EnsureComponents);
// - escolha e liga/desliga (MCT_RADIO do conjunto 3): o texto da opção centrado e as setas esquerda/direita
//   com o foco, como "Subtitles" e "Vibration" do jogo;
// - botão (MCT_BUTTON do conjunto 2, só o texto): "Aplicar mudanças";
// - informação (dois rótulos desabilitados), lista (a de teclas, MCT_LIST do conjunto 3: a moldura e a barra
//   com setas) e uma linha de estado.
// Cabem kPcRowsVisible linhas; com mais, elas rolam com o foco ou a roda, e uma barra do conjunto 5 (desabilitada)
// mostra onde se está.
//
// Navegação (a página não usa a do gerente: navegação 0): na faixa das abas, esquerda/direita troca de aba (o
// conteúdo troca na hora, como a pausa) e baixo desce para as linhas; nas linhas, cima/baixo anda (de cima da
// primeira, sobe para a aba) e esquerda/direita fica com o controle. Escolher uma aba (Espaço, ou o clique do
// mouse) a abre; passar o mouse por cima só dá o foco. Voltar volta à pausa e descarta o que não foi aplicado.
//
// VÍDEO, GRÁFICOS e INTERFACE editam uma cópia da configuração (nf::VideoSettings); "Aplicar mudanças" (em
// cada uma das três) a entrega a nf::video_controller() (platform/video_settings.h). Quando a resolução ou o
// modo de tela mudam, a caixa de mensagem do jogo (Menu_CreateOptionBox, página 0x4000002f) pergunta se
// mantém, com a contagem regressiva de kVideoConfirmSeconds; sem resposta, volta à anterior. A linha DLSS /
// DLAA está lá, apagada, "em breve" (CLAUDE.md, pendência 8); ÁUDIO mostra as barras de volume do jogo,
// apagadas, até o módulo de som existir. CONTROLES e TECLAS são de engine/menu_pc_controls.h (registradas com
// MenuPc_RegisterTab) e valem na hora. A linha das legendas de INTERFACE (kPcRowSubtitles) é a opção do jogo
// (DrawInfo[2], [ui] subtitles; nf::set_game_option, platform/game_options.h) e também vale na hora, sem o Apply.
//
// Identificadores: a página 0x40000060; controles 0x10000300-0x100003ff (0x10000400-0x100004ff são das abas
// de controles e teclas).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/pc_text.h"

struct M_MANAGER;
struct M_CONTROL;

constexpr uint32_t kPcOptionsPage = 0x40000060;
// A moldura.
constexpr uint32_t kPcWindow = 0x10000300;
constexpr uint32_t kPcPanel = 0x10000301;
constexpr uint32_t kPcHelp = 0x10000302;
constexpr uint32_t kPcRowsBar = 0x10000303;   // a barra que mostra a rolagem das linhas
constexpr uint32_t kPcTabFirst = 0x10000310;  // + PcTab: os botões da faixa de abas
// As linhas das abas deste módulo.
constexpr uint32_t kPcRowResolution = 0x10000320;
constexpr uint32_t kPcRowWindowMode = 0x10000321;
constexpr uint32_t kPcRowVSync = 0x10000322;
constexpr uint32_t kPcRowFov = 0x10000323;
constexpr uint32_t kPcRowFpsCap = 0x10000324;        // o limite de quadros ([video] fps_cap)
constexpr uint32_t kPcRowInterpolation = 0x10000325;  // a interpolação entre ticks ([video] interpolation)
constexpr uint32_t kPcRowInstantMouse = 0x10000326;  // o mouse sem atraso ([video] instant_mouse_look)
constexpr uint32_t kPcRowAdapter = 0x10000327;       // o adaptador de vídeo ([video] adapter; vale ao reiniciar)
constexpr uint32_t kPcRowApplyVideo = 0x1000032f;
constexpr uint32_t kPcRowRenderScale = 0x10000330;
constexpr uint32_t kPcRowAntiAliasing = 0x10000331;
constexpr uint32_t kPcRowFilter = 0x10000332;
constexpr uint32_t kPcRowHdTextures = 0x10000333;
constexpr uint32_t kPcRowDlss = 0x10000334;
constexpr uint32_t kPcRowApplyGraphics = 0x1000033f;
constexpr uint32_t kPcRowHdUi = 0x10000340;
constexpr uint32_t kPcRowHudAnchor = 0x10000341;
constexpr uint32_t kPcRowLanguage = 0x10000342;
constexpr uint32_t kPcRowMenuStyle = 0x10000343;  // o estilo dos menus ([ui] menu_style)
constexpr uint32_t kPcRowSubtitles = 0x10000344;  // as legendas (a opção do jogo, DrawInfo[2]; vale na hora)
constexpr uint32_t kPcRowApplyInterface = 0x1000034f;
constexpr uint32_t kPcRowMusic = 0x10000350;
constexpr uint32_t kPcRowEffects = 0x10000351;
constexpr uint32_t kPcRowAudioNote = 0x10000352;
// O id da caixa de confirmação (Menu_CreateOptionBox / Menu_UpdateOptionBox).
constexpr uint32_t kPcVideoConfirm = 0x10000360;
// A linha da pausa: índice na lista principal (tag 6 de 0x10000028).
constexpr int32_t kPcPauseRow = 3;
// O conjunto de componentes que a página cria (a barra de volume do 08000002, conjunto 10).
constexpr uint16_t kPcComponentSet = 0x50;
constexpr int32_t kPcRowsVisible = 7;

enum class PcTab : uint8_t { Video, Graphics, Interface, Audio, Controls, Keys, Count };
constexpr int kPcTabCount = int(PcTab::Count);
// O nome da aba no --menu-tab (video, graphics, interface, audio, controls, keys).
const char* MenuPc_TabName(PcTab tab);
bool MenuPc_ParseTab(const std::string& name, PcTab* out);

// Uma aba registrada (as de controles e teclas vêm de menu_pc_controls.cpp).
struct MenuPcTab {
    void (*build)(uint8_t index, M_MANAGER* m) = nullptr;  // cria as linhas (MenuPc_AddRow) na montagem
    void (*enter)(uint8_t index) = nullptr;                // a aba aparece: textos e valores
    void (*tick)(uint8_t index) = nullptr;                 // cada tick (0x50) com a aba aberta e a página atual
    void (*leave)(uint8_t index) = nullptr;                // a aba some (outra aba, ou a página sai)
    PcText help = PcText::HelpOptions;                     // a barra de ajuda nas linhas
};
void MenuPc_RegisterTab(PcTab tab, const MenuPcTab& desc);

// As linhas. Info: o nome e um valor (rótulos desabilitados). Slider: a barra de volume (lo..hi). Choice: o
// rádio (as opções com MenuPc_SetChoices). Button: um botão na coluna do nome. List: uma lista que ocupa a
// área das linhas (lo = colunas). Status: um texto centrado embaixo da área (sem foco).
enum class PcRowKind : uint8_t { Info, Slider, Choice, Button, List, Status };
// Uma linha nova da aba (na montagem, pelo `build` dela); devolve o controle (a lista, a barra, o rádio...).
M_CONTROL* MenuPc_AddRow(M_MANAGER* m, PcTab tab, uint32_t id, PcRowKind kind, int32_t lo = 0, int32_t hi = 0);
// O controle da linha na página atual do gerente (nulo sem a página).
M_CONTROL* MenuPc_Control(uint8_t index, uint32_t id);
// Os textos (UTF-8, no idioma atual; viram a fonte do jogo com PcText_ToGame). O nome fica guardado na
// página (o rótulo aponta para ele).
void MenuPc_SetRowName(uint8_t index, uint32_t id, const std::string& utf8);
void MenuPc_SetRowValue(uint8_t index, uint32_t id, const std::string& utf8);  // Info e Status
// As opções do rádio (UTF-8) e a escolhida (sem o evento 0x49).
void MenuPc_SetChoices(uint8_t index, uint32_t id, const std::vector<std::string>& options, int32_t selected);
int32_t MenuPc_Choice(uint8_t index, uint32_t id);  // a opção escolhida (-1 sem o rádio)
// A posição da barra (sem o evento) e a lida.
void MenuPc_SetSlider(uint8_t index, uint32_t id, int32_t pos);
int32_t MenuPc_Slider(uint8_t index, uint32_t id);
// Linha habilitada (com foco e entrada) ou apagada.
void MenuPc_SetRowEnabled(uint8_t index, uint32_t id, bool on);

// Põe os embrulhos da pausa e os tratadores da página (Menu_PortRegisterHandler) e registra a página e as abas
// deste módulo. Pode ser chamada de novo (não embrulha duas vezes). O main a chama depois do boot; os testes,
// depois de menu_handlers_debug_power_on (que apaga os registros).
void MenuPc_Install();

// Uma página só do port: `build` cria os controles na página atual do gerente (MenuPc_AddControl);
// `focus` é o id do controle que recebe o foco ao mostrar.
using MenuPcBuilder = void (*)(uint8_t index, M_MANAGER* m);
void MenuPc_RegisterPage(uint32_t page, MenuPcBuilder build, uint32_t focus);
// A página existe no gerente ou tem construtor.
bool MenuPc_PageAvailable(uint8_t index, uint32_t page);
// Mostra a página (montando-a na primeira vez), empilhando a atual: voltar volta para ela.
bool MenuPc_ShowPage(uint8_t index, uint32_t page);
// Mostra a página de opções (se preciso) na aba pedida, com o foco na faixa de abas.
bool MenuPc_ShowTab(uint8_t index, PcTab tab);
// O estilo Modern (src/ui, [ui] menu_style = modern): a linha OPTIONS da pausa chama o gancho antes de mostrar a
// página Original; true = as opções Modern abriram por cima (o main põe o gancho; sem ele, ou com false, a
// página Original).
using MenuPcOptionsHook = bool (*)(uint8_t index);
void MenuPc_SetOptionsHook(MenuPcOptionsHook hook);
// A aba aberta na página de opções.
PcTab MenuPc_CurrentTab();
// Um controle novo na página atual do gerente, em coordenadas do arquivo (640 x 480; FixupResolution
// converte, menos na janela), como um registro 0xfffffff9: tipo, profundidade, conjunto de componentes,
// tag e os parâmetros A e B do tipo.
M_CONTROL* MenuPc_AddControl(M_MANAGER* m, uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t w, int16_t h,
                             uint16_t depth, uint16_t set, uint32_t tag = 0, uintptr_t a = 0, uintptr_t b = 0);
// O conjunto de componentes kPcComponentSet do gerente (a barra de volume), criado se ainda não existe.
void MenuPc_EnsureComponents(uint8_t index);

// A cada quadro de vídeo (main.cpp): os textos com a camada HD (PcText_SetHdText) e, se o menu fechou no
// meio de uma confirmação (Start), a configuração anterior volta.
void MenuPc_Frame(bool hd_text);

// Uma linha para o relatório do --screenshot: a página, a aba, o foco, a rolagem e os valores pendentes.
int MenuPc_Describe(char* out, size_t size);

struct MenuPcStats {
    uint32_t built = 0;       // páginas montadas
    uint32_t shown = 0;       // páginas mostradas
    uint32_t pause_rows = 0;  // linhas "OPTIONS" acrescentadas à pausa
    uint32_t confirms = 0;    // caixas de confirmação abertas
    uint32_t tab_changes = 0; // trocas de aba
    uint32_t back_closes = 0; // pausas fechadas por voltar no topo (Esc, Backspace, triângulo)
};
extern MenuPcStats g_menu_pc_stats;

// Estado inicial (testes; não toca no heap).
void menu_pc_debug_power_on();
