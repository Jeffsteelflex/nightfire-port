#pragma once
// Camada de plataforma dos quebráveis e dos estilhaços (psi*, no PS2 em 0x00117418-0x00122d50).
//
// Na carga, psiCreateBreakableData lê a cadeia de DMA da primeira caixa do modelo de um quebrável de
// vidro e guarda os vértices (SHATTER_VERT: posição, UV e cor de cada vértice dos lotes) e os
// triângulos das tiras (BREAKTRI_tag: textura e três índices) num registro de 0x18 bytes pendurado no
// cabeçalho do bloco 0x2d (o terceiro campo, que nos modelos com skinning é o SkinInfo).
// psiGetTriList devolve esses dados para Break_Kill estilhaçar o modelo.
//
// Cada estilhaço é um triângulo com cadeia de DMA própria (psiCreateShard: GIF tag da tira, posição,
// cor e UV dos três vértices para a VU1), desenhado por psiDrawShard com a matriz do estilhaço e a
// textura do triângulo (o PS2 a põe no GS por psiFont_SetTexture), no estado de
// psiSetShardRenderStates: Z testado sem escrita, alfa > 0, mistura transparente (ALPHA 0x44). A
// rotina da VU1 é a 0 (ROTATE_FAST, cor do vértice sem tweak): o alfa que DrawAllShards põe no tweak
// (psiSetTweakA) não muda o estilhaço.
//
// No PC: a cadeia é montada igual, no heap, e psiDrawShard lê dela os três vértices, leva-os ao mundo
// pela matriz e registra um triângulo no quadro (psi_draw_triangles, platform/psi_draw.h) com a
// textura e o estado em vigor; o renderizador desenha os triângulos com um vertex buffer dinâmico.
//
// Divergências (comentadas nas funções): psiDeleteShard libera a cadeia na hora, em vez de pô-la na
// lista de liberação do quadro com o endereço errado (o original passa o endereço do campo, e
// PS2StartCalcPacket liberaria o meio de um bloco já livre); psiCreateBreakableData para e conta
// (g_shard_guards) nos códigos de DMA/VIF que o original não sabe pular (laço infinito) e nos
// triângulos e vértices além do que a caixa reserva (escreveria fora do bloco).

#include <cstdint>

#include "engine/map_types.h"

struct obj_tag;
struct _SHATTER_VERT_LIST;
struct SHATTER_VERT;

// Triângulo de um quebrável (0xc bytes).
struct BREAKTRI_tag {
    uint32_t texture;    // 0x00 endereço do pacote de registradores da textura (Tex[i].dma)
    uint16_t vert[3];    // 0x04 índices no lote em que o triângulo está (ver psiCreateBreakableData)
    uint16_t pad;
};
static_assert(sizeof(BREAKTRI_tag) == 0xc);

// Dados de estilhaçar (0x18 bytes, no terceiro campo do cabeçalho do bloco 0x2d).
struct BREAKGFX_tag {
    uint32_t unknown_00[4];  // não escritos
    BREAKTRI_tag* tris;      // 0x10 caixa + 0x30 entradas
    SHATTER_VERT* verts;     // 0x14 caixa + 0x2c entradas
};
static_assert(sizeof(BREAKGFX_tag) == 0x18);

// Resultado de psiGetTriList (0x10 bytes, alocado a cada chamada e nunca liberado, como no PS2).
struct TRILIST_tag {
    int32_t tri_count;   // 0x00 caixa + 0x30 (0 sem dados)
    int32_t vert_count;  // 0x04 caixa + 0x2c
    BREAKTRI_tag* tris;  // 0x08
    SHATTER_VERT* verts; // 0x0c
};
static_assert(sizeof(TRILIST_tag) == 0x10);

extern uint32_t ShardTexture;  // textura do último estilhaço desenhado (0xffffffff: nenhuma)

struct ShardGuards {
    uint32_t unknown_code = 0;    // código de DMA/VIF que o original não pula (laço infinito)
    uint32_t tri_overflow = 0;    // triângulos além de caixa + 0x30
    uint32_t vert_overflow = 0;   // vértices além de caixa + 0x2c
};
extern ShardGuards g_shard_guards;

void psiCreateBreakableData(celglist_tag* glist);
TRILIST_tag* psiGetTriList(obj_tag* obj);
int32_t* psiCreateShard(_SHATTER_VERT_LIST* list);
void psiDeleteShard(int32_t* chain_field);
void psiFinishedObjectShatter();
void psiDrawShard(int32_t* chain, MATRIX* m, uint32_t texture);
// pstex_tag* da entrada de Tex cujo pacote de registradores é dma (0 se nenhuma).
uint32_t GetTextureFromDMA(int32_t* dma);
// No PS2 põe a textura (pstex_tag*) no cache da VRAM para os próximos desenhos; no PC guarda o índice
// de Tex dela (g_psi_font_texture). 0 não muda nada.
void psiFont_SetTexture(uint32_t texture);
extern int32_t g_psi_font_texture;  // só no PC: índice de Tex da textura em vigor (-1 nenhuma)

// Só para testes: estado inicial das globais.
void psi_shard_debug_power_on();
