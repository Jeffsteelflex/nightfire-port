#pragma once
// Mensagens e legendas da tela (Text_*, 0x001b09f0-0x001b1700), a caixa de texto dos menus
// (Text_Init/Text_SendMessage/TextBox_Update/Text_SetupChar, 0x00222b80-0x00224110) e o buffer circular
// das mensagens recebidas (CirBuf_*). Ver docs/engine.md, "Camada 2D".
//
// Text_AddMsg põe uma mensagem na fila (MsgQueue, nós de 0x20 bytes): cada linha (Font_WordWrapString
// na largura do tipo, TextMsgFormats) vira um TXT_MSG com o texto num buffer travado do StringHeap e um
// tempo em ticks (proporcional ao tamanho da linha; no mínimo 60 fora das legendas), em ordem de
// prioridade; as linhas da mesma mensagem ficam encadeadas (TXT_MSG::next). Tipos (TXTMSG_TYPE):
//   1 informação (portas, gatilhos, armas), 2 objetivo, 3 situação da missão, 4 legenda de uma linha,
//   5 legenda de duas linhas, 6 item pego (vira 1 no multijogador).
// Text_UpdateMsg desconta FRAME_RATE_MUL do tempo da mensagem atual de um jogador e tipo e passa para a
// próxima quando ele acaba; quem mostra é o HUD (tipos 1, 2, 3 e 6: HUD_UpdateStatusPane, engine/hud.cpp)
// e Text_Update/Text_Update2Line (legendas: os sprites SubTitleSpr*, ligados à câmera de maior
// número até a 5 que desenha sprites). Legendas só entram com a opção de legendas ligada
// (DrawInfo[2]; no PS2 americano o menu a deixa desligada por padrão: Menu_WillRunMiniMission).
//
// Legendas de fala: o som (Sound_UpdateSounds, ao começar um som marcado por Sound_ReqestPlaySfx) chama
// Sound_DoSubtitle, que procura o som em Snd2Lbl e põe a fala como legenda de prioridade 0xff (forçada)
// pelo tempo do som (SFXOutputData) x 0.92 (g_fSubtitleDurationModification). Quem decide se o som tem
// legenda é Sound_ReqestPlaySfx (engine/sound.cpp): som 2D sempre; 3D com cena de câmera, sem a flag de 3D
// ou com o ouvinte a menos de metade do alcance; nada além de 1.1 x o alcance.
//
// Divergências (comentadas nas funções): Text_AddMsg e Text_UpdateMsg não leem câmeras além de
// glb_viewer (jogador >= kViewerCount); Text_AddMsg sem nó livre (fila sem Text_Init, heap cheio)
// descarta a mensagem em vez de ficar em laço.

#include <cstdint>

#include "engine/object.h"
#include "engine/sprite.h"

// Uma linha de mensagem na fila (0x20 bytes).
struct TXT_MSG {
    LLNODE_tag node;     // 0x00
    char* text;          // 0x08 travado no StringHeap
    TXT_MSG* next;       // 0x0c a próxima linha da mesma mensagem
    uint32_t data;       // 0x10 o valor passado (o HUD o desenha como ícone, hashcode 03, no lugar do texto)
    uint16_t length;     // 0x14
    uint16_t pad;
    float time;          // 0x18 ticks restantes (<= 0: sai na próxima atualização)
    uint8_t type;        // 0x1c TXTMSG_TYPE; 0 só num nó nunca usado (o único que Text_AddMsg aproveita)
    int8_t priority;     // 0x1d ordem na fila (crescente; CompMsgTxt)
    int8_t player;       // 0x1e jogador (-1: legendas)
    uint8_t pad2;
};
static_assert(sizeof(TXT_MSG) == 0x20);

// Formato e largura de quebra de cada tipo de mensagem (8 bytes).
struct TXTMSGFORMAT_tag {
    char* format;
    uint16_t width;
    uint16_t unknown_06;
};
static_assert(sizeof(TXTMSGFORMAT_tag) == 8);

// Buffer circular de palavras (16 bytes).
struct CIRBUF_INFO {
    int32_t* buffer;      // 0x00
    uint16_t tail;        // 0x04 o mais antigo
    uint16_t head;        // 0x06 onde entra o próximo
    uint16_t count;       // 0x08
    uint16_t unknown_0a;
    uint16_t size;        // 0x0c
    uint16_t pad;
};
static_assert(sizeof(CIRBUF_INFO) == 16);

constexpr int kTextMsgTypes = 7;
extern TXTMSGFORMAT_tag TextMsgFormats[kTextMsgTypes];  // 0x002bf108
extern SpriteInfo SubtitlesInfo;                        // 0x00319940 (construtor estático)
extern DLISTINFO_tag MsgQueue;                          // 0x00319970
extern CIRBUF_INFO ReceivedMsgs;                        // 0x002bf140 mensagens recebidas (tipos 0, 2, 3)
extern int32_t ReceivedBuf[0x80];                       // 0x00319988
extern TXT_MSG* SubTitleMsg;                            // 0x0030cd98 legenda atual
extern sprite* SubTitleSpr;                             // 0x0030cd9c legenda de uma linha
extern sprite* SubTitleSpr1;                            // 0x0030cda0 primeira das duas linhas
extern sprite* SubTitleSpr2;                            // 0x0030cda4
extern char* SubTitleStr;                               // 0x0030cda8
extern char* SubTitleStr2;                              // 0x0030cdac

// Contadores para as ferramentas (nf_loader_check, relatório da captura); não existem no PS2.
struct TextStats {
    uint32_t added[8] = {};   // Text_AddMsg aceitas, por tipo
    uint32_t lines = 0;       // TXT_MSG criados
    uint32_t refused = 0;     // Text_AddMsg recusadas (legendas desligadas, jogador >= 4)
    uint32_t no_node = 0;     // só no port: fila sem Text_Init ou sem memória (o original ficaria em laço)
};
extern TextStats g_text_stats;

// Som -> rótulo da fala (8 bytes): o bit 31 do rótulo força a legenda mesmo com elas desligadas.
struct SND2LBL_tag {
    uint32_t label;
    uint32_t sound;
};
static_assert(sizeof(SND2LBL_tag) == 8);
constexpr int kSnd2Lbl = 484;
constexpr int kSfxOutputCount = 0x60d;
extern SND2LBL_tag Snd2Lbl[kSnd2Lbl];                 // 0x002d25d8
// Tabela do módulo de som (0x002c94a0, 0x18 bytes por som): +0x08 alcance, +0x10 duração em segundos,
// +0x14 o som não precisa ser ouvido de perto, +0x15 legenda só perto. Copiada do ACTION.ELF.
extern uint8_t SFXOutputData[kSfxOutputCount * 0x18];
extern float g_fSubtitleDurationModification;         // 0x0030d084 (0.92)
void Sound_DoSubtitle(uint32_t sound);
// Só no PC (ver acima): pos nulo = som 2D.

uint32_t CirBuf_Init(CIRBUF_INFO* info, uint16_t size, int32_t* buffer);
int32_t CirBuf_Add(CIRBUF_INFO* info, int32_t value);

void Text_Init(int8_t warm);
void Text_AddMsg(int8_t player, uint8_t priority, int32_t type, char* text, uint32_t data, int16_t time);
uint32_t Text_UpdateMsg(int8_t player, int32_t type, TXT_MSG** msg);
void Text_Update();
void Text_Update2Line();
void Text_DelMsg(TXT_MSG* msg);
void Text_FlushAllSubtitles();

// ---- caixa de texto dos menus (o teclado da tela para digitar o nome) ----
// Os menus (MenuManager_*, Component_*, Label_*) ainda não foram portados: estas funções só são
// chamadas por eles e chamam os stubs da seção "Chamadas da camada 2D ainda não portadas".
struct M_CONTROL;
struct M_MANAGER;
struct M_PAGE;
struct M_LABEL;

// Descrição de um controle novo (0x24 bytes; só os campos usados aqui).
struct NEW_CONTROL {
    uint32_t id;           // 0x00
    char* format;          // 0x04
    uint32_t unknown_08;
    uint32_t unknown_0c;   // 0x0c vai para o controle +0x20
    uint32_t unknown_10;
    uint16_t owner;        // 0x14 vai para os 16 bits altos do identificador
    uint16_t depth;        // 0x16
    int16_t x;             // 0x18
    int16_t y;             // 0x1a
    int16_t w;             // 0x1c
    int16_t h;             // 0x1e
    uint32_t unknown_20;
};
static_assert(sizeof(NEW_CONTROL) == 0x24);

// Uma tecla do teclado da tela (0x14 bytes).
struct TEXTBOX_KEY {
    int32_t x, y, w, h;   // em pixels, relativos ao teclado
    uint8_t key;          // 0x10 o caractere ('-' apaga, '+' fecha)
    uint8_t blocked;      // 0x11 lados sem vizinho: 1 cima, 2 baixo, 4 esquerda, 8 direita
    uint8_t pad[2];
};
static_assert(sizeof(TEXTBOX_KEY) == 0x14);

// Caixa de texto (0x9b0 bytes).
struct M_TEXTBOX {
    uint8_t control[0x70];   // 0x000 M_CONTROL: +0x0c dono, +0x14 identificador, +0x18 id, +0x1c estado
    int16_t x;               // 0x070
    int16_t y;               // 0x072
    int16_t w;               // 0x074
    int16_t h;               // 0x076
    uint16_t depth;          // 0x078
    uint8_t state;           // 0x07a bit 0: fechar; bit 2: liberada
    uint8_t type;            // 0x07b 0xc
    uint32_t scripts[6];     // 0x07c Script_Select (Text_SendMessage aceita 7: o sétimo cai em label)
    uint8_t label[0x114];    // 0x094 M_LABEL (o nome digitado); +0xe8 (0x17c) aponta os dados dele
    uint8_t cursor[0x94];    // 0x1a8 componente
    uint8_t highlight[0x94]; // 0x23c componente sobre a tecla escolhida
    uint8_t keyboard[0x1a8]; // 0x2d0 componente do teclado
    TEXTBOX_KEY keys[65];    // 0x478
    char name[0x13];         // 0x98c o texto digitado
    uint8_t pad;
    int32_t cursor_x;        // 0x9a0
    int32_t cursor_y;        // 0x9a4
    float line_height;       // 0x9a8
    uint16_t key;            // 0x9ac tecla escolhida (em Text_SetupChar, a última criada)
    uint16_t pad2;
};
static_assert(sizeof(M_TEXTBOX) == 0x9b0);

void Text_Init(M_TEXTBOX* box, NEW_CONTROL* desc, M_CONTROL* owner);
int32_t Text_SendMessage(M_TEXTBOX* box, uint32_t message, int32_t a, int32_t b);
void TextBox_Update(M_MANAGER* manager, M_PAGE* page, M_TEXTBOX* box);
void Text_SetupChar(M_TEXTBOX* box, uint8_t key, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t blocked);

// Só para testes: estado inicial dos estáticos (não toca no heap).
void text_debug_power_on();
