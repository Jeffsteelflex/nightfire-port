#pragma once
// O sistema de menus do ACTION.ELF (MenuManager_*, Manager_*, Page_*, Menu_*, Component_*, os
// controles Label/Button/Checkbox/List/Radio/Scroll/Memo/Window, os scripts de menu Script_* de
// 0x002201c8 em diante e Process_*). Formato do arquivo em docs/formats.md, "Menus (item tipo 8)";
// arquitetura em docs/engine.md, "Menus".
//
// Um menu é criado por MenuManager_Create(menu, página, jogador, gerente, ação de fechar, pausa os
// sons, status): o gerente (M_MANAGER, 5 em `manager`) lê o arquivo de menu carregado (item tipo 8,
// MenuManager_Load guarda o endereço em file_start) e cria todas as páginas do `menu` pedido, com
// os seus controles, scripts e conjuntos de componentes; depois mostra a página pedida. Tudo é um
// M_CONTROL (cabeçalho de 0x94 bytes) com um tipo em +0x7b: 1 botão, 2 caixa de marcar, 3 combo
// (não usado no disco), 5 rótulo, 6 lista, 7 gerente, 8 página, 9 rádio, 10 barra de rolagem, 11
// spin (não usado), 12 caixa de texto (o teclado da tela, text.cpp), 13 janela, 200 memo, 0x72 o
// sub-controle de um componente (o destaque da lista, o cursor da barra).
//
// Mensagens: __Menu_SendMessage(controle, mensagem, a, b) despacha pelo tipo; cada tipo trata as
// suas e passa o resto ao dono (+0x0c). Os eventos de interface (0x4b escolheu, 0x4c página
// entrou, 0x4d saiu, 0x49 mudou, 0x4e/0x4f foco, 0x50 tick da página, 0x5d/0x5e outros botões,
// 0x5f voltar, 0x6b, 0x6d...) sobem até o gerente, que os entrega ao tratador do identificador do
// controle (Handler_HandleMessage: P_* para páginas, C_* para controles; os do jogo estão em
// menu_handlers.cpp, os do menu principal em menu_frontend.cpp e menu_codename.cpp; os ainda não portados são
// stubs que devolvem 0 = não tratado).
//
// O desenho é a camada 2D: cada controle tem sprites (Menu_CreateSprite, câmera 7) montados a partir
// dos componentes do arquivo (Component_SetupInstance: imagens de textura 03xxxxxx com bordas que
// esticam) e textos (o sprite de texto do rótulo, Label_Update). Coordenadas: o arquivo está em
// 640 x 480 e FixupResolution converte para a tela do PS2 (512 x 448).
//
// Páginas só do port: uma página criada em C++ pode usar as mesmas estruturas (Page_Init +
// Manager_SendMessage 0x43, controles com NEW_CONTROL e Manager_SendMessage 0x42, componentes de um
// conjunto já carregado) e um tratador registrado com Menu_PortRegisterHandler; ver docs/engine.md.

#include <cstddef>
#include <cstdint>

#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/view.h"
#include "platform/ptr32.h"

// ---- lista ligada genérica (LinkedList_*, 0x00219280) ----
// Cada item tem um nó {next, prev} em `offset` palavras do começo; head/tail apontam para o nó.
struct LINKEDLIST {
    P32<void> head;    // 0x00 nó do primeiro item
    P32<void> tail;    // 0x04 nó do último
    uint32_t count;    // 0x08
    uint32_t offset;   // 0x0c posição do nó no item, em palavras
};
static_assert(sizeof(LINKEDLIST) == 0x10);

void LinkedList_Init(LINKEDLIST* list, uint32_t offset);
void LinkedList_ReInit(LINKEDLIST* list);
uint32_t LinkedList_Add(LINKEDLIST* list, void* prev_field, void* node);
uint32_t LinkedList_Insert(LINKEDLIST* list, void* after, void* prev_field, void* node);
uint32_t LinkedList_Remove(LINKEDLIST* list, void* prev_field, void* node);
void* LinkedList_GetIndex(LINKEDLIST* list, uint32_t index);

// O primeiro item da lista (head - offset), ou nulo.
inline void* ll_first(const LINKEDLIST& list) {
    return list.head != nullptr ? static_cast<uint8_t*>(list.head) - list.offset * 4 : nullptr;
}

struct M_CONTROL;
struct M_MANAGER;
struct M_PAGE;
struct M_SCRIPT;
struct M_KEYFRAME;

// ---- scripts de menu (Script_*, 0x002201c8-0x002214f0) ----
// Uma mensagem de um quadro-chave (0x18 bytes).
struct M_MESSAGE {
    P32<M_MESSAGE> next;  // 0x00
    P32<M_MESSAGE> prev;  // 0x04
    uint32_t target;    // 0x08 identificador do controle; 0xfffffffd o gerente, 0xfffffffe o dono do script
    uint32_t message;   // 0x0c mensagem (0xe1 e acima: processo de cor, Process_Create)
    int32_t a;          // 0x10 (-4: o resultado da mensagem anterior)
    int32_t b;          // 0x14
};
static_assert(sizeof(M_MESSAGE) == 0x18);

// Um quadro-chave (0x28 bytes): posição e tamanho do controle num quadro do script.
struct M_KEYFRAME {
    P32<M_KEYFRAME> next;  // 0x00
    P32<M_KEYFRAME> prev;  // 0x04
    LINKEDLIST messages;  // 0x08 M_MESSAGE
    uint32_t frame;       // 0x18
    uint32_t flags;       // 0x1c 1 spline (no script), 2 começo do laço, 4 fim do laço, 0x20 fixo
    int16_t x;            // 0x20
    int16_t y;            // 0x22
    int16_t w;            // 0x24
    int16_t h;            // 0x26
};
static_assert(sizeof(M_KEYFRAME) == 0x28);

// Um script (0x20 bytes): quadros-chave em ordem de quadro.
struct M_SCRIPT {
    P32<M_SCRIPT> next;   // 0x00
    P32<M_SCRIPT> prev;   // 0x04
    LINKEDLIST keys;      // 0x08 M_KEYFRAME
    uint32_t id;          // 0x18
    uint32_t flags;       // 0x1c 1 interpola por spline, 8 laço ativo
};
static_assert(sizeof(M_SCRIPT) == 0x20);

// Um processo (0x24 bytes): uma mensagem 0xe1 (troca de cor gradual) rodando por `duration` ticks.
struct M_PROCESS {
    P32<M_PROCESS> next;  // 0x00
    P32<M_PROCESS> prev;  // 0x04
    uint32_t frame;       // 0x08
    uint32_t message;     // 0x0c
    uint32_t colour;      // 0x10 cor final (0xRRGGBBAA, byte baixo = R... na ordem da memória)
    int32_t b;            // 0x14 > 0: segunda cor
    uint32_t start;       // 0x18 cor inicial (mensagem 0x30 do controle)
    uint32_t unknown_1c;
    uint32_t duration;    // 0x20
};
static_assert(sizeof(M_PROCESS) == 0x24);

// ---- controles ----
// Cabeçalho comum (0x94 bytes).
struct M_CONTROL {
    P32<M_CONTROL> next;      // 0x00 nó na lista da página
    P32<M_CONTROL> prev;      // 0x04
    P32<P32<sprite>> sprites;  // 0x08 sprites do componente (Component_SetupInstance)
    P32<M_CONTROL> owner;     // 0x0c página, lista, botão...
    uint32_t unknown_10;
    uint32_t component;       // 0x14 conjunto << 16 | índice do componente no conjunto
    uint32_t id;              // 0x18 identificador (escolhe o tratador)
    uint32_t flags;           // 0x1c estado do componente: 0x10 normal, 0x20 foco, 0x40 apertado/marcado, 0x80
    uint32_t tag;             // 0x20 distingue controles de mesmo id
    uint32_t focus_ticks;     // 0x24 ticks com o foco (Page_Update)
    LINKEDLIST scripts;       // 0x28 M_SCRIPT
    P32<M_SCRIPT> script;     // 0x38 script atual
    P32<M_KEYFRAME> key;      // 0x3c quadro-chave atual (nulo: parado)
    P32<M_KEYFRAME> loop_key;  // 0x40 começo do laço
    int32_t loop_step;        // 0x44
    uint32_t unknown_48;
    int32_t step;             // 0x4c índice do quadro-chave
    int32_t result;           // 0x50 resultado da última mensagem do script
    int32_t frame;            // 0x54 quadro do script
    uint32_t script_flags;    // 0x58 8 laço, 0x10 voltou ao laço
    LINKEDLIST processes;     // 0x5c M_PROCESS
    int32_t sprite_count;     // 0x6c
    int16_t x;                // 0x70 relativo à página
    int16_t y;                // 0x72
    int16_t w;                // 0x74
    int16_t h;                // 0x76
    uint16_t depth;           // 0x78 prioridade dos sprites (menor fica na frente)
    uint8_t state;            // 0x7a 1 escondido, 2 desabilitado, 4 liberado
    uint8_t type;             // 0x7b
    P32<M_SCRIPT> defaults[6];  // 0x7c scripts por evento (0x21): entrar, sair, escolher, mudar, foco, voltar
};
static_assert(sizeof(M_CONTROL) == 0x94);
static_assert(offsetof(M_CONTROL, scripts) == 0x28 && offsetof(M_CONTROL, processes) == 0x5c);
static_assert(offsetof(M_CONTROL, x) == 0x70 && offsetof(M_CONTROL, defaults) == 0x7c);

enum : uint8_t {
    MCT_BUTTON = 1,
    MCT_CHECKBOX = 2,
    MCT_COMBO = 3,
    MCT_LABEL = 5,
    MCT_LIST = 6,
    MCT_MANAGER = 7,
    MCT_PAGE = 8,
    MCT_RADIO = 9,
    MCT_SCROLL = 10,
    MCT_SPIN = 11,
    MCT_TEXT = 12,
    MCT_WINDOW = 13,
    MCT_SUB = 0x72,
    MCT_MEMO = 200,
};

// Página (0xd0 bytes).
struct M_PAGE {
    M_CONTROL base;           // 0x00 tipo 8; base.owner = o gerente
    LINKEDLIST controls;      // 0x94
    P32<M_PAGE> next;         // 0xa4 nó na lista de páginas do gerente
    P32<M_PAGE> prev;         // 0xa8
    P32<M_CONTROL> focus;     // 0xac controle com o foco
    P32<M_CONTROL> player_focus[4];  // 0xb0 foco de cada jogador (multijogador, 0x5a)
    uint32_t navigation;      // 0xc0 1 quatro direções, 2 cima/baixo, 3 esquerda/direita (0x56)
    uint32_t movie;           // 0xc4 filme de fundo
    uint32_t ticks;           // 0xc8 ticks desde que apareceu
    uint32_t input_delay;     // 0xcc ticks sem entrada (0x6f)
};
static_assert(sizeof(M_PAGE) == 0xd0);

// Janela (0x9c bytes): o primeiro controle de cada página, com os scripts de entrada e saída.
struct M_WINDOW {
    M_CONTROL base;           // 0x00 tipo 13
    P32<M_PAGE> page;         // 0x94 a página atual do gerente na criação
    uint32_t unknown_98;
};
static_assert(sizeof(M_WINDOW) == 0x9c);

// Gerente (0x1d8 bytes).
struct M_MANAGER {
    M_CONTROL base;           // 0x00 tipo 7
    P32<sprite> cursor;       // 0x094 sprite do cursor (CursorInfo)
    LINKEDLIST pages;         // 0x098 M_PAGE (nó em +0xa4)
    STACKINFO stack;          // 0x0a8 páginas anteriores (voltar)
    int32_t stack_data[64];   // 0x0b0
    uint32_t movie;           // 0x1b0 filme de fundo tocando
    uint32_t status;          // 0x1b4 MenuManager_GetStatus: 3 pausa, 4 fim de missão, 5 menu principal
    uint32_t first_page;      // 0x1b8
    P32<M_PAGE> page;         // 0x1bc página atual
    P32<M_PAGE> previous;     // 0x1c0 página saindo (transição)
    P32<M_PAGE> overlay;      // 0x1c4 página por cima (0x45)
    uint32_t close_action;    // 0x1c8 ação que fecha o menu (0x28: nenhuma)
    int16_t player;           // 0x1cc jogador (-1 todos)
    uint8_t player_byte;      // 0x1ce
    uint8_t unknown_1cf;      // 0x1cf 7
    uint8_t index;            // 0x1d0
    uint8_t active;           // 0x1d1
    uint8_t input;            // 0x1d2 a página atualiza com entrada
    uint8_t transition;       // 0x1d3 trocando de página
    uint8_t frozen;           // 0x1d4 sem entrada (0x68)
    uint8_t pause_sounds;     // 0x1d5
    uint8_t pad_1d6[2];
};
static_assert(sizeof(M_MANAGER) == 0x1d8);
static_assert(offsetof(M_MANAGER, stack) == 0xa8 && offsetof(M_MANAGER, movie) == 0x1b0);
static_assert(offsetof(M_MANAGER, player) == 0x1cc && offsetof(M_MANAGER, index) == 0x1d0);

// Rótulo (0x114 bytes).
struct M_LABEL {
    M_CONTROL base;           // 0x000 tipo 5
    char format_buf[0x10];    // 0x094 formato próprio (0x23: o do arquivo + escala 100%)
    sprite text;              // 0x0a4 modelo do sprite (copiado para `sprite` a cada tick)
    P32<sprite> spr;          // 0x0e8
    uint32_t sprite_flags;    // 0x0ec (0x6c)
    uint32_t label;           // 0x0f0 rótulo do texto (0xffffffff: texto direto)
    P32<char> string;         // 0x0f4
    uint32_t image;           // 0x0f8 textura da imagem (0x24)
    uint32_t colour;          // 0x0fc cor com foco (0x1b)
    uint32_t colour_off;      // 0x100 cor sem foco (0x1c)
    uint32_t colour_throb;    // 0x104 cor pulsando (0x29)
    uint32_t mode;            // 0x108 1 cores próprias, 2 texto (sem: imagem)
    int16_t faded;            // 0x10c
    uint8_t outline;          // 0x10e (0x62)
    uint8_t throb;            // 0x10f (0x70)
    uint8_t keep_focus;       // 0x110 (0x76)
    uint8_t pad_111[3];
};
static_assert(sizeof(M_LABEL) == 0x114);
static_assert(offsetof(M_LABEL, text) == 0xa4 && offsetof(M_LABEL, spr) == 0xe8);

// Botão (0x1ac bytes).
struct M_BUTTON {
    M_CONTROL base;           // 0x000 tipo 1
    M_LABEL label;            // 0x094
    int16_t unknown_1a8;      // 0x1a8 NEW_CONTROL +4
    uint8_t unknown_1aa;
    uint8_t pad;
};
static_assert(sizeof(M_BUTTON) == 0x1ac);

// Caixa de marcar (0x1ac bytes).
struct M_CHECKBOX {
    M_CONTROL base;           // 0x000 tipo 2
    M_LABEL label;            // 0x094
    uint8_t checked;          // 0x1a8
    uint8_t pad[3];
};
static_assert(sizeof(M_CHECKBOX) == 0x1ac);

// Barra de rolagem (0x148 bytes).
struct M_SCROLL {
    M_CONTROL base;           // 0x000 tipo 10
    M_CONTROL thumb;          // 0x094 o cursor (tipo 0x72)
    int32_t min;              // 0x128
    int32_t max;              // 0x12c
    int32_t pos;              // 0x130
    int32_t range;            // 0x134
    uint32_t held;            // 0x138 ticks com a direção segurada
    uint16_t mode;            // 0x13c 1 sem cursor, 2 vertical
    int16_t margin_a;         // 0x13e
    int16_t margin_b;         // 0x140
    uint16_t pressed;         // 0x142 0x40 menos, 0x80 mais
    uint8_t wrap;             // 0x144 dá a volta nas pontas
    uint8_t proportional;     // 0x145 cursor proporcional (0x69)
    uint8_t pad[2];
};
static_assert(sizeof(M_SCROLL) == 0x148);

// Uma célula de linha (0x8c bytes).
struct LIST_CELL {
    char text[0x80];          // 0x00
    uint32_t image;           // 0x80 textura (caminho morto no original: o texto nunca é nulo)
    uint16_t u, v, uw, vh;    // 0x84
};
static_assert(sizeof(LIST_CELL) == 0x8c);

// Linha de lista ou opção de rádio (0x2d0 bytes).
struct LIST_ROW {
    P32<LIST_ROW> next;       // 0x000
    P32<LIST_ROW> prev;       // 0x004
    LIST_CELL cells[5];       // 0x008
    int32_t data;             // 0x2c4 valor (0x35)
    uint8_t continuation;     // 0x2c8 linha quebrada do texto anterior
    uint8_t pad[3];
    P32<sprite> stripe;       // 0x2cc listras de fundo
};
static_assert(sizeof(LIST_ROW) == 0x2d0);

// Lista (0x56c bytes).
struct M_LISTBOX {
    M_CONTROL base;           // 0x000 tipo 6
    M_CONTROL highlight;      // 0x094 destaque da linha escolhida (tipo 0x72)
    M_SCROLL scroll;          // 0x128
    LINKEDLIST rows;          // 0x270 LIST_ROW
    P32<sprite> cells[32][5];  // 0x280 sprites das linhas visíveis
    int32_t visible;          // 0x500 linhas que cabem
    int32_t columns;          // 0x504
    int32_t widths[5];        // 0x508 largura de cada coluna, em % (0x28)
    int32_t align[5];         // 0x51c 1 esquerda, 2 direita, 3 centro (0x59)
    float row_height;         // 0x530
    int32_t top;              // 0x534 primeira linha visível
    uint32_t colour;          // 0x538 com foco (0x1b)
    uint32_t colour_off;      // 0x53c sem foco (0x1c)
    uint32_t colour_sel;      // 0x540 linha escolhida (0x29)
    uint32_t colour2;         // 0x544 (0x58)
    uint32_t stripe;          // 0x548 cor das listras (0x73)
    P32<char> format;         // 0x54c
    char format_sel[0x10];    // 0x550 formato da linha escolhida
    P32<LIST_ROW> selected;   // 0x560
    uint16_t highlight_mode;  // 0x564 1 destaque + cor, 2 só a cor (0x2a)
    uint8_t unknown_566;
    uint8_t outline;          // 0x567 (0x62)
    uint8_t throb;            // 0x568 (0x70)
    uint8_t wrap;             // 0x569 quebra textos longos
    uint8_t wrapped;          // 0x56a linhas extras das quebras
    uint8_t selected_index;   // 0x56b
};
static_assert(sizeof(M_LISTBOX) == 0x56c);
static_assert(offsetof(M_LISTBOX, rows) == 0x270 && offsetof(M_LISTBOX, visible) == 0x500);
static_assert(offsetof(M_LISTBOX, selected) == 0x560);

// Rádio (0x308 bytes): uma opção visível por vez, trocada pela barra.
struct M_RADIO {
    M_CONTROL base;           // 0x000 tipo 9
    M_LABEL label;            // 0x094
    M_SCROLL scroll;          // 0x1a8
    LINKEDLIST options;       // 0x2f0 LIST_ROW
    uint32_t unknown_300;
    P32<LIST_ROW> selected;   // 0x304
};
static_assert(sizeof(M_RADIO) == 0x308);

// Memo (0x284 bytes): texto com quebra de linha e rolagem.
struct M_MEMOBOX {
    M_CONTROL base;           // 0x000 tipo 200
    M_SCROLL scroll;          // 0x094
    P32<char> text;           // 0x1dc
    P32<char> format;         // 0x1e0
    P32<sprite> lines[32];    // 0x1e4
    int32_t visible;          // 0x264
    int32_t count;            // 0x268 linhas
    float line_height;        // 0x26c
    int32_t top;              // 0x270
    uint32_t colour;          // 0x274
    uint32_t colour_off;      // 0x278
    uint32_t colour2;         // 0x27c
    uint8_t centre;           // 0x280 centra cada linha (100)
    uint8_t used;             // 0x281 sprites em uso
    uint8_t vcentre;          // 0x282 centra na vertical (0x66)
    uint8_t pad;
};
static_assert(sizeof(M_MEMOBOX) == 0x284);

// ---- componentes (Component_*, 0x001f0950) ----
// Uma peça de um componente (0x38 bytes): um SpriteInfo e como ela se posiciona no controle.
struct MenuSpriteInfo {
    SpriteInfo info;          // 0x00 (+0x24 a textura do componente)
    float stretch_w;          // 0x2c fração da largura livre (0: tamanho próprio)
    float stretch_h;          // 0x30
    uint32_t flags;           // 0x34 estados em que aparece (máscara de M_CONTROL::flags) e o encaixe:
                              //      1 embaixo, 2 à direita, 4 centro x, 8 centro y, 0x100/0x200 copia
                              //      a altura/largura, 0x400/0x800 não ocupa, 0x1000/0x2000 aditivo/subtrativo
};
static_assert(sizeof(MenuSpriteInfo) == 0x38);

// Um componente (0x14 bytes).
struct M_COMPONENT {
    uint32_t hash;            // 0x00 textura
    uint16_t count;           // 0x04 peças
    int16_t left;             // 0x06 margens do conteúdo
    int16_t top;              // 0x08
    int16_t right;            // 0x0a
    int16_t bottom;           // 0x0c
    uint16_t pad;
    P32<MenuSpriteInfo> pieces;  // 0x10
};
static_assert(sizeof(M_COMPONENT) == 0x14);
using M_COMPONENTHANDLE = M_COMPONENT;  // o nome antigo (text.cpp)

// Um conjunto de componentes (0x128 bytes).
struct M_COMPONENTSET {
    P32<M_COMPONENTSET> next;  // 0x000
    P32<M_COMPONENTSET> prev;  // 0x004
    M_COMPONENT items[14];    // 0x008
    uint32_t id;              // 0x120
    uint8_t manager;          // 0x124
    uint8_t pad[3];
};
static_assert(sizeof(M_COMPONENTSET) == 0x128);

// ---- globais ----
constexpr int kMenuManagers = 5;
extern M_MANAGER menu_manager[kMenuManagers];  // manager (0x002e3cd8)
extern uint8_t* file_start;               // 0x0030d600 o arquivo de menu (MenuManager_Load)
extern LINKEDLIST component_list;         // 0x002dda70
extern uint8_t user_restarted_or_quit;    // 0x0030d60c
extern uint8_t mf_menu;                   // 0x0030d611 menu de fim de missão (controle desconectado; resetmap.cpp)
extern float throb;                       // 0x0030d188
extern uint32_t frame_counter;            // 0x0030d164
extern uint32_t menu_delay_frame;         // 0x0030d190
extern M_CONTROL* help_control;           // h (0x0030d638): o memo de ajuda da página (0x10000225)
// help_table (0x002e4610, 130 x {id, valor, rótulo}): o texto de ajuda de cada controle. Copiada do ELF.
constexpr int kHelpEntries = 130;
extern uint32_t help_table[kHelpEntries * 3];
extern SpriteInfo CursorInfo;             // 0x002e3c98 o cursor (copiado do ELF)

// ---- gerente (menu_manager.cpp) ----
void MenuManager_Init();
uint32_t MenuManager_Create(uint32_t menu, uint32_t page, int16_t player, uint8_t index, uint32_t close_action,
                            int8_t pause_sounds, uint32_t status);
void MenuManager_Delete(uint8_t index);
void MenuManager_Update();
void MenuManager_Monitor();
void MenuManager_Load(uint32_t hash, uint8_t* data);
uint8_t MenuManager_Exists(uint8_t index);
uint32_t MenuManager_GetStatus();
void MenuManager_Enable(int8_t on, uint8_t index);
int32_t Manager_SendMessage(M_MANAGER* m, uint32_t message, int32_t a, int32_t b);
int32_t Manager_HandleMessage(uint8_t index, M_CONTROL* c, uint32_t message, int32_t a, int32_t b);
int32_t Handler_HandleMessage(uint8_t index, M_CONTROL* c, uint32_t message, int32_t a, int32_t b);
uint32_t Menu_PlayerHasQuitOrRestarted();
void FixupResolution(NEW_CONTROL* desc);
void FixupResolution(M_KEYFRAME* key);
// Páginas.
void Page_Init(M_PAGE* page, uint32_t id, M_CONTROL* owner);
int32_t Page_SendMessage(M_PAGE* page, uint32_t message, int32_t a, int32_t b);
void Page_Update(M_MANAGER* m, M_PAGE* page, uint32_t input);
void Page_FindHelpControl(M_PAGE* page);
void Page_SetHelpText(M_PAGE* page);
void Window_Init(M_WINDOW* w, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Window_SendMessage(M_WINDOW* w, uint32_t message, int32_t a, int32_t b);
void Window_Update(M_MANAGER* m, M_PAGE* page, M_WINDOW* w);
M_CONTROL* Window_Get(M_PAGE* page);
// Processos.
void Process_Init(M_CONTROL* c);
void Process_Create(M_CONTROL* c, M_MESSAGE* msg, uint32_t duration, uint32_t unused);
void Process_Destroy(M_CONTROL* c);
void Process_RunFrame(M_CONTROL* c);

// ---- scripts de menu (menu_script.cpp) ----
M_KEYFRAME* Script_AddKeyFrame(M_SCRIPT* script, M_KEYFRAME* key);
int32_t Script_PlayDefault(M_CONTROL* c, uint32_t event);
void Script_RunFrame(M_MANAGER* m, M_CONTROL* c);
void Script_InterpolateSpline(M_CONTROL* c, M_SCRIPT* script, float t);
void Script_InterpolateLine(M_CONTROL* c, M_SCRIPT* script, float t);
void Script_PrepareScripting(M_CONTROL* c);
void Script_DelAllScripts(M_CONTROL* c);
M_SCRIPT* Script_AddScript(M_CONTROL* c, uint32_t id);
void Script_DelScript(M_CONTROL* c, M_SCRIPT* script);
void Script_DelKeyFrame(M_SCRIPT* script, M_KEYFRAME* key);
M_MESSAGE* Script_AddMessage(M_KEYFRAME* key, M_MESSAGE* msg);
void Script_DelMessage(M_KEYFRAME* key, M_MESSAGE* msg);
M_SCRIPT* Script_Select(M_CONTROL* c, uint32_t id);
int32_t Script_Play(M_CONTROL* c, uint32_t id, int8_t loop);
int32_t Script_ExitLoop(M_CONTROL* c);
void Script_Stop(M_CONTROL* c);
float Script_CalculateSpline(int32_t p0, int32_t p1, int32_t p2, int32_t p3, float t);
float Script_CalculateLine(int32_t p0, int32_t p1, int32_t p2, int32_t p3, float t);

// ---- núcleo (menu.cpp) ----
int32_t __Menu_SendMessage(M_CONTROL* c, uint32_t message, int32_t a, int32_t b);
int32_t __Menu_Send(uint8_t index, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t __Menu_SendEx(uint8_t index, uint32_t id, uint32_t tag, uint32_t message, int32_t a, int32_t b);
int32_t __Menu_SendEx2(uint8_t index, uint32_t id, uint32_t bit, uint32_t message, int32_t a, int32_t b);
uint32_t __Menu_SendDelayed(uint32_t delay, uint8_t index, uint32_t id, uint32_t message, int32_t a, int32_t b);
uint32_t __Menu_SendDelayedMessage(uint32_t delay, M_CONTROL* c, uint32_t message, int32_t a, int32_t b);
void Menu_ProcessDelayedMessages();
void Menu_ClearDelayedMessages();
void Menu_Update(M_MANAGER* m, M_PAGE* page, M_CONTROL* c);
int32_t Menu_InputAction(int16_t player, int32_t action, uint16_t mask);
int32_t Menu_CursorOverMe(M_MANAGER* m, M_CONTROL* c);
M_CONTROL* Menu_GetControl(M_MANAGER* m, int8_t unused);
M_CONTROL* Menu_FindControl(M_MANAGER* m, uint8_t direction);
M_CONTROL* Menu_GetFirstControl(M_MANAGER* m);
M_PAGE* Menu_GetPage(M_CONTROL* c);
uint32_t Menu_Assert(M_CONTROL* c);
void Menu_ResizeControl(M_CONTROL* c, int16_t x, int16_t y, int16_t w, int16_t h);
void Menu_ResizeControl(M_CONTROL* c, M_CONTROL* from);
int32_t Menu_ClipString(sprite* s, int32_t width, char* format);
sprite* Menu_CreateSprite(SpriteInfo* info, uint32_t type);
void Menu_DeleteSprite(sprite* s, uint32_t type);
void Menu_InitializeSprite(sprite* s, int16_t priority, int16_t x, int16_t y, int16_t w, int16_t hh, char* text,
                           uint32_t hash, uint32_t colour, uint32_t colour2, uint16_t u, uint16_t v, uint16_t uw,
                           uint16_t vh, uint32_t flags);
void* Menu_Malloc(uint32_t size, uint32_t type);
void Menu_Free(void** p, int32_t type);
void Menu_SetAction(int16_t player, uint32_t action, uint8_t state);
uint32_t Menu_GetLastController();
uint32_t Menu_GetNoInputCount();
void Menu_ResetNoInputCount();
void Menu_AlphaUpdate();
void Menu_AlphaReset();
int16_t Menu_AlphaGet();
void Menu_EnableSounds(uint32_t page);
void Menu_PlaySound(uint32_t sound);
void Menu_UpdateTextFmt();
void Menu_GetNormalTextFmt(sprite* s);
void Menu_StringInit(int32_t size);
void Menu_StringKill();
// Componentes.
void Component_InitComponents();
M_COMPONENTSET* Component_AddSet(uint8_t index, uint16_t id);
void Component_DestroyAll(uint8_t index);
MenuSpriteInfo* Component_AddComponent(M_COMPONENT* c, int16_t left, int16_t top, int16_t right, int16_t bottom,
                                       uint32_t hash, int16_t count);
void Component_InitInstance(MenuSpriteInfo* p, int16_t x, int16_t y, int16_t w, int16_t hh, uint16_t u, uint16_t v,
                            uint16_t uw, uint16_t vh, uint32_t flags, uint32_t colour, float stretch_w,
                            float stretch_h);
M_COMPONENT* Component_GetHandle(M_CONTROL* c);
void Component_SetupInstance(M_CONTROL* c, uint32_t flags, int32_t x, int32_t y, int32_t w, int32_t hh);
void Component_Free(M_CONTROL* c);
void Component_Destroy(M_COMPONENTSET* set);

// ---- controles (menu_controls.cpp) ----
void Label_Init(M_LABEL* label, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Label_SendMessage(M_LABEL* label, uint32_t message, int32_t a, int32_t b);
void Label_Update(M_MANAGER* m, M_PAGE* page, M_LABEL* label, int8_t over);
void Button_Init(M_BUTTON* b, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Button_SendMessage(M_BUTTON* b, uint32_t message, int32_t a, int32_t bb);
void Button_Update(M_MANAGER* m, M_PAGE* page, M_BUTTON* b);
void Checkbox_Init(M_CHECKBOX* c, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Checkbox_SendMessage(M_CHECKBOX* c, uint32_t message, int32_t a, int32_t b);
void Checkbox_Update(M_MANAGER* m, M_PAGE* page, M_CHECKBOX* c);
void Scroll_Init(M_SCROLL* s, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Scroll_SendMessage(M_SCROLL* s, uint32_t message, int32_t a, int32_t b);
void Scroll_Update(M_MANAGER* m, M_PAGE* page, M_SCROLL* s);
void List_Init(M_LISTBOX* l, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t List_SendMessage(M_LISTBOX* l, uint32_t message, int32_t a, int32_t b);
void List_Update(M_MANAGER* m, M_PAGE* page, M_LISTBOX* l);
int32_t List_GetIndex(M_LISTBOX* l, LINKEDLIST* rows, LIST_ROW* row);
int32_t List_GetIndex_RealItem(M_LISTBOX* l, LINKEDLIST* rows, LIST_ROW* row);
LIST_ROW* List_SetIndex(M_LISTBOX* l, LINKEDLIST* rows, int32_t index);
LIST_ROW* List_SetIndex_RealItem(M_LISTBOX* l, LINKEDLIST* rows, int32_t index);
void Radio_Init(M_RADIO* r, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Radio_SendMessage(M_RADIO* r, uint32_t message, int32_t a, int32_t b);
void Radio_Update(M_MANAGER* m, M_PAGE* page, M_RADIO* r);
int32_t Radio_GetIndex(M_RADIO* r, LINKEDLIST* options, LIST_ROW* row);
LIST_ROW* Radio_SetIndex(M_RADIO* r, LINKEDLIST* options, int32_t index);
void Memo_Init(M_MEMOBOX* mm, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Memo_SendMessage(M_MEMOBOX* mm, uint32_t message, int32_t a, int32_t b);
void Memo_Update(M_MANAGER* m, M_PAGE* page, M_MEMOBOX* mm);
// Combo e spin não aparecem nos arquivos do disco: stubs (modules_stubs.cpp).
struct M_COMBO;
struct M_SPIN;
void Combo_Init(M_COMBO* c, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Combo_SendMessage(M_COMBO* c, uint32_t message, int32_t a, int32_t b);
void Combo_Update(M_MANAGER* m, M_PAGE* page, M_COMBO* c);
void Spin_Init(M_SPIN* s, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Spin_SendMessage(M_SPIN* s, uint32_t message, int32_t a, int32_t b);
void Spin_Update(M_MANAGER* m, M_PAGE* page, M_SPIN* s);

// ---- tratadores e funções do jogo (menu_handlers.cpp) ----
// Um tratador: (gerente, controle, id, mensagem, a, b); 1 = tratado.
using MenuHandler = int32_t (*)(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_PAUSE_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_GCPAUSE_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_ENDMISSION_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_NFRESULTS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_NFBONUS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_NFSTATS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_LBMSGOPTIONS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
// A caixa de mensagem (página 0x4000002f por cima): o texto, "sim/não" (ok_only 0) ou "ok" (1;
// with_continue: "continuar"); a resposta sai em Menu_UpdateOptionBox (1 sim, 2 não/voltar, 3 ok, 4).
void Menu_CreateOptionBox(uint8_t index, char* text, uint32_t id, uint8_t ok_only, uint8_t with_continue);
void Menu_CreateOptionBox(uint8_t index, uint32_t label, uint32_t id, uint8_t ok_only, uint8_t with_continue);
uint32_t Menu_UpdateOptionBox(uint32_t* id);
uint32_t Menu_GetLevelIndex(uint32_t level);
uint32_t Menu_GetNightfireStatus();
uint64_t Menu_GetBonus(uint8_t player);
void Menu_SetBonus(uint64_t bits, uint8_t player, int8_t merge);
uint32_t Menu_SetLevelBonus(uint32_t level, uint32_t medal, uint8_t player);
uint8_t* Menu_GetLevelBonuses(uint32_t level);
uint8_t Menu_HasMedal(uint32_t level, uint32_t medal, uint8_t player);
int32_t Menu_GetBestMedal(uint32_t level, uint8_t player);
void Menu_UpdateDefaultCodename(uint8_t player, uint8_t slot);
void Menu_ChangeControllerStyle(int16_t player, uint32_t style);
void Menu_AddRow(uint8_t index, uint32_t label, uint32_t a, uint32_t b, uint32_t score, int8_t indent, int8_t times);
void Menu_AddRowPercentage(uint8_t index, uint32_t label, uint32_t percent, int32_t target, uint32_t score, int8_t indent);
// As medalhas de cada fase (PlrStarts_*, 0x00199210) e as melhorias de arma que elas dão.
struct REWARDINFO_tag {
    uint32_t kind;        // 0x00 0-4 (4: conta as fichas do grupo); 5 nenhuma
    uint32_t bit;         // 0x04 bit em `bonus`
    uint32_t count;       // 0x08 bits do grupo ligados (tipo 4)
    uint8_t has;          // 0x0c o bit está ligado
    uint8_t pad[3];
};
static_assert(sizeof(REWARDINFO_tag) == 0x10);
void PlrStarts_ProcessRewardCounter(uint64_t* bits, uint32_t level, uint32_t medal, int8_t set, REWARDINFO_tag* out);
uint32_t PlrStarts_GetRewardsTableSize();
uint8_t PlrStats_DoneBetter();
void Set_Upgrade(uint8_t player, int16_t slot, int16_t value);
void ResetUpgrade(uint8_t player);
int32_t SeparateNumber(uint32_t n, char* out);
char* Timer_Seconds2String(int32_t hundredths, uint32_t format);
constexpr int kRewardsSize = 0x300;
extern uint8_t RewardsTable[kRewardsSize];  // 0x002bc920 (12 x 0x40), copiada do ELF
extern uint8_t Upgrades[36];               // 0x002beea8 (player_weapon.cpp)
extern uint8_t menu_bonus[0xa8];           // bonus (0x002e1b88): 4 x uint64 por jogador, o texto do bônus, +0xa4 novo
extern uint8_t ls[0x44];                   // 0x002e1ad0 o perfil (load/save)
extern uint8_t def_codename[112];          // 0x002e1b18
extern char VersionString[29];             // 0x002dee48 (copiada do ELF)
extern uint8_t menu_movie;                 // 0x002e1c48

// Uma entrada da tabela de tratadores: os do jogo (handler nulo: ainda não portado, um stub que
// devolve 0) e os registrados pelo port.
struct MenuHandlerEntry {
    uint32_t id;
    const char* name;
    MenuHandler handler;
};
const MenuHandlerEntry* Menu_FindHandler(uint32_t id);
// Só no port: registra (ou troca) o tratador de um identificador, para páginas e controles feitos em
// C++ (uma página de opções do PC, por exemplo). handler nulo apaga o registro.
void Menu_PortRegisterHandler(uint32_t id, const char* name, MenuHandler handler);

// ---- só no port ----
// Um gancho no começo do tick do gerente que recebe a entrada, logo depois do mouse (Menu_MouseUpdate): os menus
// no estilo Modern mandam por ele os pedidos deles (engine/menu_modern.h). Nulo (o padrão): nada.
using MenuPortTick = void (*)(M_MANAGER* m);
void Menu_SetPortTick(MenuPortTick tick);
// Estado inicial das globais do módulo (testes; não toca no heap; também apaga o gancho acima).
void menu_debug_power_on();
void menu_handlers_debug_power_on();
// Casos protegidos dos tratadores (o original escreveria fora das tabelas ou leria por nulo).
struct MenuHandlerGuards {
    uint32_t level_index = 0;  // Menu_SetLevelBonus com a fase fora de sp_level (ou a última)
    uint32_t option_box = 0;   // Menu_CreateOptionBox sem a página da caixa
};
extern MenuHandlerGuards g_menu_handler_guards;
// Os menus abertos e o que mostram (relatório do --screenshot e do nf_loader_check):
// "menus gerente 0 menu 80000002 pagina 4000004b foco 10000028/6 ...".
int Menu_Describe(char* out, size_t size);
// A página atual do gerente (0 se ele não existe).
uint32_t Menu_CurrentPage(uint8_t index);
// O controle com o foco na página atual do gerente: identificador e tag (0 se não houver).
bool Menu_Focus(uint8_t index, uint32_t* id, uint32_t* tag);
// Contagens de casos protegidos (o original leria endereços inválidos).
struct MenuGuards {
    uint32_t no_component = 0;   // Component_GetHandle sem conjunto: o original devolve nulo e quem chama lê
    uint32_t no_focus = 0;       // página sem controle com foco onde o original leria por nulo
    uint32_t no_page = 0;        // controle sem página
    uint32_t handler_stub = 0;   // mensagens para tratadores ainda não portados
    uint32_t scroll_zero_range = 0;  // barra sem faixa (o original para na divisão por zero)
};
extern MenuGuards g_menu_guards;
// Menus abertos e fechados desde o início (testes e relatórios).
struct MenuStats {
    uint32_t created = 0;
    uint32_t deleted = 0;
    uint32_t pages = 0;          // páginas criadas
    uint32_t controls = 0;       // controles criados
};
extern MenuStats g_menu_stats;
// O item tipo 8 carregado (08000001 os menus de jogo, 08000002 o menu principal); 0 nenhum.
extern uint32_t g_menu_file;
