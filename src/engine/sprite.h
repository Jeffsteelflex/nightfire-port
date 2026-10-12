#pragma once
// Sprites da tela (módulo Sprite do ACTION.ELF, 0x001de3d8-0x001deaa4) e o desenho deles
// (View_DrawSprites, View_AddSprite e hashtable_set_sprite; View_3DPoint2Screen está em view.cpp). Ver
// docs/engine.md, "Camada 2D".
//
// Um sprite é uma imagem (textura 03xxxxxx, recorte u, v, uw, vh) ou um texto (texto + formato de
// fonte) na tela do PS2 (512 x 448), ligado a uma câmera (viewer). SpriteList tem 512 nós de 0x44
// bytes; Sprite_Create* tira um nó livre (nulo quando acabam) e Sprite_Delete o devolve (o nó guarda
// os campos antigos: Sprite_Create e Sprite_CreateLink só reiniciam texto, posição, recorte e escala).
//
// A cada quadro, Game_Draw pede a lista de cada câmera (Sprite_BuildList: os sprites da câmera com
// prioridade diferente de -1, alfa da cor diferente de 0 e texto ou textura, em ordem decrescente de
// prioridade) e View_DrawSprites desenha uma faixa dela: as imagens viram SPRITE_DRAW em SprBuffList
// (descarregada por psiDrawSprites a cada 63), os textos vão para Font_DrawText. Prioridade >= 50 é
// fundo: só as câmeras especiais (4-10) desenham o fundo antes da cena, e Sprite_BuildList deixa a
// contagem de fundo um a menos que o número de sprites de fundo (comportamento original preservado: o
// último sprite de fundo também sai na passada da frente).
//
// Só no PC: SprOriginList guarda, para cada entrada de SprBuffList, a origem do desenho (Psi2DOrigin:
// o sprite, o hashcode da textura, o glifo), que psiDrawSprites leva ao registro do quadro. O hashcode
// da textura de cada sprite vem de hashtable_set_sprite (g_sprite_texture_hash).

#include <cstddef>
#include <cstdint>

#include "engine/font.h"
#include "engine/object.h"
#include "platform/psi_2d.h"
#include "platform/ptr32.h"

struct viewer_tag;
struct _VECTOR;

// Flags de sprite (sprite::flags, SpriteInfo::flags, SPRITE_DRAW::flags).
enum : uint16_t {
    SPRF_SHADOW = 0x0020,     // imagem: cópia preta por baixo, deslocada; texto: sombra (Font_DrawText 0x1000)
    SPRF_FLIP_U = 0x0040,     // espelha na horizontal
    SPRF_FLIP_V = 0x0080,     // espelha na vertical
    SPRF_ADDITIVE = 0x0100,   // ALPHA 0x48
    SPRF_SUBTRACT = 0x0200,   // ALPHA 0x42
    SPRF_TILE = 0x0400,       // ladrilha a imagem ((uw + 1) / largura da textura vezes)
    SPRF_CENTRE = 0x0800,     // x, y é o centro
    SPRF_OUTLINE = 0x2000,    // texto: contorno de quatro cópias (Font_DrawText 0x4000)
    SPRF_HIGHLIGHT = 0x4000,  // texto: cópia em (-1, -1) com a cor branca (Font_DrawText 0x2000)
};

// Sprite (0x44 bytes, nó da SpriteList).
struct sprite {
    LLNODE_tag node;        // 0x00
    uint32_t created;       // 0x08 GameState.frame na criação (vai para SPRITE_DRAW +8)
    uint32_t texture;       // 0x0c pstex_tag* (hashtable_set_sprite)
    uint32_t colour;        // 0x10 0xRRGGBBAA; alfa 0 esconde
    uint32_t colour2;       // 0x14 texto: cor da sombra e do contorno
    P32<char> text;         // 0x18 texto (nulo: imagem)
    P32<char> format;       // 0x1c formato do texto (0xfcfcfcfc: nenhum ainda)
    uint16_t length;        // 0x20 caracteres do texto a desenhar
    uint16_t flags;         // 0x22 SPRF_*; os 4 bits baixos são zerados por Sprite_BuildList
    int16_t x;              // 0x24 posição na tela do PS2 (relativa à câmera)
    int16_t y;              // 0x26
    uint16_t tex_w;         // 0x28 tamanho da textura (texto de Sprite_Create2: a posição)
    uint16_t tex_h;         // 0x2a
    int16_t w;              // 0x2c tamanho na tela
    int16_t h;              // 0x2e
    int16_t u;              // 0x30 canto na textura
    int16_t v;              // 0x32
    int16_t uw;             // 0x34 extensão na textura; texto: largura medida (alinhamento)
    int16_t vh;             // 0x36 texto: altura medida
    int8_t priority;        // 0x38 ordem de desenho (maior primeiro); -1 escondido; >= 50 fundo
    uint8_t viewer;         // 0x39 câmera
    uint8_t unknown_3a[2];
    GPOINT scale;           // 0x3c escala do texto
};
static_assert(sizeof(sprite) == 0x44);
static_assert(offsetof(sprite, priority) == 0x38 && offsetof(sprite, scale) == 0x3c);

// Descrição de um sprite para Sprite_Create2 (0x2c bytes).
struct SpriteInfo {
    uint32_t colour;      // 0x00
    uint32_t colour2;     // 0x04
    uint16_t priority;    // 0x08 o byte baixo vai para sprite::priority
    uint16_t flags;       // 0x0a
    int16_t x;            // 0x0c
    int16_t y;            // 0x0e
    int16_t w;            // 0x10 0: o da textura
    int16_t h;            // 0x12
    int16_t u;            // 0x14
    int16_t v;            // 0x16
    int16_t uw;           // 0x18 0: largura da textura - 1
    int16_t vh;           // 0x1a
    uint32_t label;       // 0x1c rótulo do texto (Txt_BindLabel); 0xffffffff: imagem
    P32<char> format;     // 0x20 formato do texto
    uint32_t hash;        // 0x24 textura da imagem (0: nenhuma)
    int8_t viewer;        // 0x28 câmera (< 0: não liga)
    uint8_t unknown_29[3];
};
static_assert(sizeof(SpriteInfo) == 0x2c);

constexpr int kSpriteCount = 0x200;   // nós da SpriteList
constexpr int kSprActive = 792;       // SprActiveList (0x00349b30 até SprBuffList)
constexpr int kSprBuff = 64;          // SprBuffList: até 63 em uso

extern DLISTINFO_tag SpriteList;               // 0x00349b18
extern P32<sprite> SprActiveList[kSprActive];      // 0x00349b30
extern SPRITE_DRAW SprBuffList[kSprBuff];      // 0x0034a790
extern Psi2DOrigin SprOriginList[kSprBuff];    // só no PC: a origem de cada entrada de SprBuffList
extern int32_t SprCnt;                         // 0x0030d0b0 entradas em uso de SprBuffList

struct SpriteGuards {
    uint32_t active_full = 0;   // mais sprites da câmera que SprActiveList comporta
    uint32_t zero_tile = 0;     // SPRF_TILE com textura de largura ou altura 0 (o original dividiria por 0)
    uint32_t no_format = 0;     // texto medido sem formato (0xfcfcfcfc; o original leria esse endereço)
};
extern SpriteGuards g_sprite_guards;

void Sprite_InitLists();
sprite* Sprite_Create();
sprite* Sprite_Create2(SpriteInfo* info);
sprite* Sprite_CreateLink(uint16_t viewer);
void Sprite_Link2Viewer(sprite* s, uint16_t viewer);
void Sprite_SetFmt(sprite* s, char* format, uint16_t unused);
void Sprite_SetText(sprite* s, char* text, uint16_t unused);
void Sprite_SetTextNFmt(sprite* s, char* text, char* format, uint16_t unused);
void Sprite_SetParams(sprite* s, int16_t priority, int16_t x, int16_t y, uint32_t hash, uint32_t colour);
void Sprite_Delete(sprite* s);
P32<sprite>* Sprite_BuildList(uint16_t camera, uint32_t* count_a, uint32_t* count_b, uint32_t* count_c);
// 1 se achou a textura (e a pôs no sprite), 0 se não.
uint32_t hashtable_set_sprite(sprite* s, uint32_t hash);

void View_DrawSprites(viewer_tag* viewer, P32<sprite>* list, uint32_t first, uint32_t end);
SPRITE_DRAW* View_AddSprite(uint16_t reserve);

// Só no PC: a origem da entrada de SprBuffList (View_AddSprite) e o hashcode da textura de um sprite (0
// se não veio de hashtable_set_sprite).
Psi2DOrigin* View_SpriteOrigin(const SPRITE_DRAW* entry);
uint32_t sprite_texture_hash(const sprite* s);

// Só para testes: estado inicial dos estáticos (não toca no heap).
void sprite_debug_power_on();
