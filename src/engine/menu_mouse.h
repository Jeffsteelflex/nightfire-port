#pragma once
// Só no port: o mouse nos menus do jogo (a pausa, o fim de missão, o resultado, o menu principal) e nos do
// port (as opções do PC). Ver docs/port.md, "Mouse nos menus".
//
// O ACTION.ELF não tem código de mouse: o "cursor" do gerente (M_MANAGER::cursor, um sprite de CursorInfo, um
// texel de 0x03000042 que nasce escondido) só marca o foco. O foco dos menus segue esse cursor
// (Page_Update -> Menu_GetControl: o menor controle habilitado sob ele) e o direcional o leva de controle em
// controle (Menu_FindControl). O mouse entra pelo mesmo caminho: Menu_MouseUpdate, chamado por
// MenuManager_Update no começo do tick do gerente que recebe a entrada, põe o cursor do gerente sob o mouse e
// deixa Page_Update mudar o foco (com os eventos 0x4f/0x4e e o som do jogo). Escolher e voltar viram as
// ações do jogo (0x1f escolher, 0x22 voltar) por um tick, lidas pelo código dos controles como as do
// teclado; as barras, os rádios e as listas são mexidos com as mensagens e o caminho de Scroll_Update (passo
// com o som e o evento 0x49 ao dono).
//
// Regras:
// - Passar por cima move o foco para o controle sob o mouse. Controles com o mesmo id do foco são um grupo só
//   (o jogo compara o foco por id: as abas e as listas da pausa são todas 0x10000028): passar por cima de um
//   deles não tira o foco de onde está; clicar numa aba da pausa leva o foco a ela, sem escolher (a troca de aba
//   é o evento de foco). Botões de mesmo id fora da pausa (as teclas do teclado da tela do menu principal,
//   0x10000074) são botões comuns: passar por cima leva o foco e clicar escolhe.
// - Um rótulo desabilitado com o mesmo id de um controle habilitado (o nome da linha nas páginas de opções do
//   jogo e do port) conta como esse controle para passar por cima e clicar (não para a roda).
// - Clique esquerdo: botões, caixas de marcar e rótulos escolhem; numa lista, a linha sob o mouse é escolhida e
//   depois escolhida de vez (0x1f); na barra da lista, as setas andam uma linha e o meio pula; num rádio, a seta
//   da esquerda volta uma opção e o resto avança; numa barra, as setas andam um passo e o trilho põe o valor
//   sob o mouse (e arrastar continua mexendo).
// - Passar por cima de uma lista escolhe a linha sob o mouse (como cima/baixo: o evento 0x49).
// - Roda: listas e memos andam uma linha por dente (para cima, a anterior); rádios e barras horizontais, um
//   passo (para cima, mais). A roda que nenhum controle usou fica para a página (Menu_MouseTakeWheel).
// - As rodas do menu principal (codinomes, dificuldade, missões, opções do codinome; a barra minúscula com o foco e
//   os cinco rótulos dos itens): clicar no rótulo do meio escolhe, nos outros gira a roda até ele; a roda do mouse
//   gira a roda em qualquer lugar da página.
// - Botão direito ou Mouse4: voltar.
// - O mouse só mexe no foco enquanto é o aparelho em uso (PsiMenuMouse::active: mexeu ou clicou depois da
//   última tecla ou botão do controle). Durante a captura de uma tecla (página de teclas), ele não faz nada.

#include <cstddef>
#include <cstdint>

struct M_MANAGER;
struct M_CONTROL;
struct M_SCROLL;
struct M_LISTBOX;

// Um tick do mouse no gerente (MenuManager_Update, antes da tecla de voltar e da navegação).
void Menu_MouseUpdate(M_MANAGER* m);

// O controle sob o ponto (x, y na tela do PS2), pela regra de Menu_GetControl (estritamente dentro,
// habilitado, não janela; o menor, e no empate o de menor profundidade); sem nenhum, um rótulo desabilitado sob
// o ponto com o mesmo id de um controle habilitado devolve esse controle (via_label = true). Nulo: nada.
M_CONTROL* Menu_MouseHitTest(M_MANAGER* m, float x, float y, bool* via_label = nullptr);

// Uma barra (M_SCROLL) sob o ponto: -1 a seta de menos, +1 a de mais, 0 o trilho entre elas.
int32_t Menu_MouseScrollPart(const M_SCROLL* s, float x, float y);
// O valor da barra para o ponto no trilho (entre as setas; preso entre o mínimo e o máximo).
int32_t Menu_MouseScrollValue(const M_SCROLL* s, float x, float y);
// A linha da lista sob a altura y (o índice "real", sem as continuações), ou -1. Fora da barra da lista.
int32_t Menu_MouseListRow(M_LISTBOX* l, float y);

// A roda deste tick que nenhum controle usou (dentes, positivo para cima); zera ao ler. As páginas do port
// a usam para rolar as linhas.
int32_t Menu_MouseTakeWheel();

// Uma linha para o relatório do --screenshot: "mouse dentro 1 x 200.0 y 150.0 ativo 1 alvo 10000320/0 tipo 9".
int Menu_MouseDescribe(char* out, size_t size);

struct MenuMouseStats {
    uint32_t hovers = 0;    // focos mudados pelo mouse
    uint32_t clicks = 0;    // cliques num controle
    uint32_t selects = 0;   // escolhas (0x1f) postas pelo clique
    uint32_t backs = 0;     // voltar (0x22)
    uint32_t steps = 0;     // passos de barra, rádio ou lista (setas, roda)
    uint32_t sets = 0;      // valores postos pelo trilho (clique ou arrasto)
    uint32_t rows = 0;      // linhas de lista escolhidas pelo mouse
};
extern MenuMouseStats g_menu_mouse_stats;

// Estado inicial (testes).
void menu_mouse_debug_power_on();
