#pragma once
// Fontes do jogo (módulo Font do ACTION.ELF, 0x001ee298-0x001ef79c). Formato em docs/formats.md,
// "Fontes".
//
// As três fontes (NFont2, SerpLight, Medium) são dados do próprio executável (.data): a descrição
// (FONT_tag: kerning, faixa de caracteres, tabela de glifos, espaço entre linhas e largura do espaço), a
// tabela de glifos (CHARDATA_tag, 149 por fonte, ordenada pelo código) e os pares de kerning (KERN_tag).
// O port as copia do ACTION.ELF do usuário na partida (kTables em game/static_init.cpp, com os ponteiros
// traduzidos). A imagem de cada fonte é uma textura comum do item 010001d8, que toda fase carrega
// (0x03000005 NFont2 512 x 128, 0x03000003 SerpLight 512 x 64 e 0x030000d8 Medium 512 x 128, 4 bits);
// FontTable liga cada fonte ao hashcode da textura.
//
// Um texto tem um formato: bytes de controle que Font_ParseFormat lê (0xfa r g b x: cor; 0xfb n e 0xfc n:
// ignorados aqui; 0xfd: nada; 0xfe n: alinhamento, 2 à direita e 3 centrado; 0xff n: fonte n de
// FontTable, que também põe a textura e o tamanho dela em CharInfo). __Font_DrawText percorre o texto e,
// para cada glifo, preenche CharInfo (um SPRITE_DRAW) e o copia para a lista de sprites da tela
// (View_AddSprite); com a flag 1 só mede. "~X" desenha um símbolo de botão (specialchar, textura
// 0x03000075); 0xba troca a cor (dourado, depois bege); 0x99 (™) vira T e M em meio tamanho.
//
// Divergências (comentadas nas funções): a busca de um glifo para no fim da tabela (os caracteres 253 a
// 255 passam do último glifo, 252, e o original leria a entrada seguinte; contam em g_font_guards) e
// não segue um glifo nulo; Font_WordWrapString não copia mais que um buffer do StringHeap.

#include <cstdint>

#include "platform/psi_2d.h"

struct GPOINT {
    float x;
    float y;
};
static_assert(sizeof(GPOINT) == 8);

// Par de kerning (6 bytes): o glifo dono da lista, o caractere seguinte e o ajuste em pixels.
struct KERN_tag {
    uint16_t first;   // 0x00 o glifo da lista
    uint16_t second;  // 0x02 o caractere que vem depois
    int8_t adjust;    // 0x04
    uint8_t pad;
};
static_assert(sizeof(KERN_tag) == 6);

// Glifo (0x18 bytes).
struct CHARDATA_tag {
    uint16_t u;           // 0x00 canto na textura
    uint16_t v;           // 0x02
    uint16_t w;           // 0x04 tamanho em texels (e em pixels da tela, com escala 1)
    uint16_t h;           // 0x06
    int16_t y_offset;     // 0x08 deslocamento vertical (o do primeiro glifo dá a linha de base)
    int16_t pre;          // 0x0a espaço antes (quando há um caractere antes)
    int16_t post;         // 0x0c espaço depois
    uint16_t kern_count;  // 0x0e pares de kerning
    KERN_tag* kern;       // 0x10
    uint16_t code;        // 0x14 código do caractere (Latin-1)
    uint16_t pad;
};
static_assert(sizeof(CHARDATA_tag) == 0x18);

// Fonte (0x1c bytes).
struct FONT_tag {
    uint32_t kern_count;   // 0x00 pares de kerning da fonte
    KERN_tag* kern;        // 0x04
    uint16_t first;        // 0x08 menor código (0x21)
    uint16_t last;         // 0x0a maior código aceito (0xff)
    uint32_t count;        // 0x0c glifos
    CHARDATA_tag* chars;   // 0x10 ordenados pelo código
    float line_gap;        // 0x14 espaço entre linhas
    float space;           // 0x18 largura do espaço (vezes 1.25 e a escala)
};
static_assert(sizeof(FONT_tag) == 0x1c);

// Entrada de FontTable (8 bytes): o hashcode da textura e a fonte.
struct FONTTABLE_tag {
    uint32_t hash;
    FONT_tag* font;
};
static_assert(sizeof(FONTTABLE_tag) == 8);

// Símbolo de botão ("~A" num texto; 0x18 bytes).
struct SPECIALCHAR_tag {
    int8_t code;        // 0x00 o caractere depois do '~'
    uint8_t pad[3];
    uint32_t hash;      // 0x04 textura (0x03000075)
    uint16_t tex_w;     // 0x08
    uint16_t tex_h;     // 0x0a
    uint16_t u;         // 0x0c
    uint16_t v;         // 0x0e
    uint16_t uw;        // 0x10
    uint16_t vh;        // 0x12
    int16_t width;      // 0x14 tamanho na tela e avanço
    int16_t height;     // 0x16
};
static_assert(sizeof(SPECIALCHAR_tag) == 0x18);

// Informação de impressão (PInfo, 0x40 bytes) que Font_WordWrapString reinicia; nenhuma função portada a
// lê.
struct PRINTINFO_tag {
    uint16_t unknown_00;
    uint16_t unknown_02;
    uint16_t unknown_04;
    uint16_t unknown_06;   // 0xffff
    float unknown_08[2];   // 1.0
    float unknown_10[6];
    float unknown_28;
    uint8_t unknown_2c[4];
    uint32_t unknown_30;
    uint32_t unknown_34;
    const char* format;    // 0x38
    uint16_t unknown_3c;
    uint16_t pad;
};
static_assert(sizeof(PRINTINFO_tag) == 0x40);

// Tamanhos das tabelas do ACTION.ELF (conferidos na cópia).
constexpr int kFontChars = 149;
constexpr int kKernNFont2 = 42;
constexpr int kKernMedium = 442;
constexpr int kKernSerpLight = 321;
constexpr int kFontTableCount = 5;
constexpr int kSpecialChars = 17;

extern KERN_tag KernNFont2[kKernNFont2];          // 0x002d9be0
extern KERN_tag KernMedium[kKernMedium];          // 0x002daad8
extern KERN_tag KernSerpLight[kKernSerpLight];    // 0x002dc330
extern CHARDATA_tag FontDataNFont2[kFontChars];   // 0x002d9ce0
extern CHARDATA_tag FontDataMedium[kFontChars];   // 0x002db538
extern CHARDATA_tag FontDataSerpLight[kFontChars];  // 0x002dcab8
extern FONT_tag FONTNFont2;                       // 0x00305a58
extern FONT_tag FONTMedium;                       // 0x00305a78
extern FONT_tag FONTSerpLight;                    // 0x00305a98
extern FONTTABLE_tag FontTable[kFontTableCount];  // 0x002dd8b0: 1 NFont2, 2 SerpLight, 3 Medium
extern SPECIALCHAR_tag specialchar[kSpecialChars];  // 0x002dd8d8
extern FONT_tag* Font;                            // 0x0030d8c0 fonte em uso
extern SPRITE_DRAW CharInfo;                      // 0x0034c7a0 molde de cada glifo
extern PRINTINFO_tag PInfo;                       // 0x0034c760

struct FontGuards {
    uint32_t past_table = 0;  // busca de glifo além da última entrada (caracteres 253-255)
    uint32_t no_glyph = 0;    // 'T' ou 'M' do ™ fora da fonte (o original seguiria um glifo nulo)
    uint32_t long_line = 0;   // linha maior que um buffer do StringHeap (Font_WordWrapString)
};
extern FontGuards g_font_guards;

// Só no PC: origem dos glifos que __Font_DrawText põe na lista de sprites (o sprite do jogo e a
// passada); View_DrawSprites e Font_DrawText a ajustam antes de desenhar.
extern const void* g_font_origin_sprite;
extern Psi2DPass g_font_origin_pass;

float Font_GetKernAdjust(char c, char prev);
void Font_ParseFormat(char* format, uint32_t& flags, GPOINT& scale);
GPOINT* __Font_DrawText(float x, float y, uint32_t colour, uint32_t colour2, uint32_t colour3, char* text,
                        char* format, GPOINT* scale, uint32_t flags, int32_t count, float width, uint32_t* fit);
GPOINT* Font_DrawText(float x, float y, uint32_t colour, uint32_t shadow, uint32_t outline, char* text, char* format,
                      GPOINT* scale, uint32_t flags, int32_t count, float width);
char* Font_WordWrapString(char** text, uint16_t length, char* format, uint32_t width, GPOINT* scale);
uint32_t Font_GetTextExtent(char* text, char* format, GPOINT* extent, GPOINT* scale, int32_t count);
uint32_t Font_GetTextCountExtent(char* text, char* format, GPOINT* scale, float width);
uint8_t Font_GetAlignment(char* format);

// Só para testes: estado inicial dos estáticos (não toca nas tabelas copiadas do ELF).
void font_debug_power_on();
