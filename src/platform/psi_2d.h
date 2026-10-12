#pragma once
// Desenho 2D da plataforma (sprites, HUD e texto): psiDrawSprites e o que ela manda ao GS, para PC.
//
// No PS2 (0x00120cf8-0x00121400): View_DrawSprites e __Font_DrawText enchem SprBuffList (até 63
// SPRITE_DRAW de 0x30 bytes) e psiDrawSprites transforma cada um num pacote de DMA com um SPRITE do GS:
// textura (psiFont_SetTexture), ALPHA_1 pela flag do sprite (0x100 aditiva 0x48, 0x200 subtrativa 0x42,
// senão 0x44), TEX1 com filtro bilinear (ou de ponto, nas três texturas de fonte de FontList), RGBAQ =
// (r, g, b, a >> 1) com Q = 1, ST normalizado (meio texel a mais fora das fontes) e XYZ2 em pixels
// 12.4 com o deslocamento da tela (PS2OffX/Y) e Z = 0x10. psiStartSprites tira o teste de Z (TEST_1 =
// alfa > 0) e a escrita de Z; psiEndSprites os devolve. MODULATE com TCC: Cf = Ct * Cv / 128,
// Af = At * Av / 128.
//
// No PC: cada psiDrawSprites vira um desenho do registro do quadro (psi_draw_2d, PsiDrawCall::sprites2d)
// com um Psi2DQuad por SPRITE_DRAW: o retângulo na tela do PS2 (512 x 448, a de Camera_CreateCameras),
// o ST, a cor na escala do GS, a textura (índice de Tex), o ALPHA_1, o filtro e a ORIGEM do elemento
// (Psi2DOrigin: que sprite do jogo, que textura 03xxxxxx, que glifo de que fonte, em que passada). O
// renderizador (RuntimeGfx::collect_frame) leva o retângulo para a janela por psi_2d_viewport: por
// padrão, uma área 4:3 centrada e escalada pela altura (a tela do PS2 de 512 x 448 aparece na TV em
// 4:3), com recorte nessa área como o SCISSOR do GS.
//
// Camada substituível: tudo o que a lógica do jogo desenha em 2D passa por aqui com a origem. Uma
// camada HD futura pode trocar o desenho por elemento (o glifo por uma fonte vetorial, a textura de um
// ícone por uma versão em alta) em collect_frame, sem mexer na lógica (Sprite_*, Text_*, Font_*, HUD).

#include <cstdint>

// Um sprite para o GS (0x30 bytes; o PS2 o chama de SPRITE_DRAW).
struct SPRITE_DRAW {
    float inv_w;          // 0x00 1 / largura da textura: S = u * inv_w
    float inv_h;          // 0x04 1 / altura
    uint32_t unknown_08;  // 0x08 sprite::created (View_DrawSprites copia; psiDrawSprites não lê)
    uint32_t texture;     // 0x0c pstex_tag* (0: o GS fica com a textura anterior)
    float x;              // 0x10 canto de cima à esquerda, em pixels da tela do PS2
    float y;              // 0x14
    float w;              // 0x18 tamanho na tela
    float h;              // 0x1c
    int16_t u;            // 0x20 texel do canto
    int16_t v;            // 0x22
    int16_t uw;           // 0x24 extensão em texels (negativa: espelhado)
    int16_t vh;           // 0x26
    uint8_t r, g, b;      // 0x28 cor na escala do GS (0x80 = 1.0)
    uint8_t a;            // 0x2b alfa 0..0xff (o GS recebe a >> 1)
    uint16_t flags;       // 0x2c sprite::flags: 0x100 aditiva, 0x200 subtrativa
    uint16_t unknown_2e;
};
static_assert(sizeof(SPRITE_DRAW) == 0x30);

// De onde veio um elemento 2D (só no PC; o PS2 não guarda): o que uma camada HD precisa para trocar o
// desenho sem a lógica do jogo.
enum class Psi2DKind : uint8_t {
    Image = 0,   // imagem de um sprite (View_DrawSprites)
    Glyph = 1,   // caractere de um texto (__Font_DrawText)
    Symbol = 2,  // símbolo de botão de um texto ("~A", specialchar)
};
enum class Psi2DPass : uint8_t {
    Main = 0,     // o desenho em si
    Shadow = 1,   // sombra (texto: flag 0x1000; imagem: cópia preta, flag 0x20)
    Outline = 2,  // contorno do texto (flags 0x2000/0x4000: as cópias deslocadas de 1 pixel)
};
struct Psi2DOrigin {
    Psi2DKind kind = Psi2DKind::Image;
    Psi2DPass pass = Psi2DPass::Main;
    uint8_t font = 0;              // glifos: o código de fonte do formato (0xff n: 1 NFont2, 2 SerpLight, 3 Medium); 0 nenhum
    uint8_t glyph = 0;             // código do caractere (Latin-1) ou do símbolo depois do '~'
    const void* sprite = nullptr;  // sprite* do jogo (Sprite_*) que gerou o desenho; nulo se avulso
    uint32_t texture_hash = 0;     // hashcode 03xxxxxx da textura, quando conhecido (0: não)
    const char* text = nullptr;    // glifos: o texto do sprite
    uint16_t char_index = 0;       // glifos: posição do caractere no texto (contando a partir de 1)
    // Glifos e símbolos, para a camada HD (render/ui_hd.h) juntar e refazer o texto: a chamada de
    // __Font_DrawText que os desenhou (um contador por chamada; cada cópia de sombra e contorno é uma
    // chamada), o alinhamento do formato (bits de Font_ParseFormat: 2 centrado, 4 à direita), a escala y
    // e a linha de base da caneta (Pos.y, na tela do PS2: o pé dos glifos sem descendente).
    uint16_t run = 0;
    uint8_t align = 0;
    float scale = 1.0f;
    float baseline = 0.0f;
};

// Um elemento do registro 2D do quadro.
struct Psi2DQuad {
    float x0, y0, x1, y1;  // retângulo na tela do PS2 (pixels de 512 x 448, como XYZ2 sem o deslocamento)
    float u0, v0, u1, v1;  // ST (0..1), como o GS recebe
    uint8_t rgba[4];       // r, g, b, a na escala do GS (a já é o alfa do sprite >> 1)
    int32_t texture;       // índice de Tex (-1: nenhuma)
    uint64_t alpha;        // ALPHA_1: 0x44 transparente, 0x48 aditiva, 0x42 subtrativa
    bool linear;           // TEX1: filtro bilinear (false: de ponto, as fontes)
    Psi2DOrigin origin;
};

// Uma chamada de psiDrawSprites no registro (PsiFrame::lists2d).
struct Psi2DList {
    uint32_t first = 0;  // em PsiFrame::quads2d
    uint32_t count = 0;
};

// Tela do PS2 em que as coordenadas 2D estão (as câmeras de Camera_CreateCameras: 512 x 448).
constexpr float kPs2ScreenWidth = 512.0f;
constexpr float kPs2ScreenHeight = 448.0f;

// Onde a tela do PS2 fica na janela do PC, em pixels: x_janela = left + x * scale_x, y_janela =
// top + y * scale_y. Por padrão a área 4:3 centrada, pela altura (janelas mais estreitas que 4:3: pela
// largura). width e height: o tamanho da área (o recorte).
struct Psi2DViewport {
    float left = 0.0f, top = 0.0f;
    float width = 0.0f, height = 0.0f;
    float scale_x = 1.0f, scale_y = 1.0f;
};
Psi2DViewport psi_2d_viewport(int window_width, int window_height);

// Um retângulo {x0, y0, x1, y1} da tela do PS2 na janela, por "vp". Só no PC: um elemento que cobre a
// largura inteira da tela do PS2 (x0 <= 0 e x1 >= 512: as tarjas de cinema, a cortina de sangue, o clarão
// da dor, as telas de luneta e de visão noturna) se estica até as bordas de uma janela mais larga que 4:3
// (e um de altura inteira, numa janela mais estreita): no PS2 ele cobre a tela toda, e na área 4:3 deixaria
// a cena à mostra dos lados. Devolve true quando esticou (o recorte desse elemento é a janela inteira).
bool psi_2d_map_rect(const Psi2DViewport& vp, int window_width, int window_height, const float in[4], float out[4]);

// Molduras em nove pedaços (só no PC). Um rótulo de imagem do menu mostra a textura inteira esticada no
// retângulo do controle: o painel da pausa (0x03000070) numa página mais alta que a do jogo (a de vídeo do
// PC) deformaria os cantos arredondados. Regra: um quadrado que mostra a textura inteira de uma moldura da
// tabela, cujos texels na janela (largura / altura) fiquem a até kPsi2DFrameTolerance da proporção de
// referência da moldura, é desenhado como sempre (um quadrado: as páginas do jogo ficam idênticas); fora
// disso vira nove: os cantos (inset[] texels da borda) com a escala do eixo menos esticado nos dois eixos
// (texels na proporção de referência), as bordas esticadas só no comprimento e o meio nos dois eixos.
struct Psi2DFrame {
    uint32_t hash = 0;           // textura 03xxxxxx
    int16_t tex_w = 0, tex_h = 0;
    float inset[4] = {};         // cantos em texels: esquerda, cima, direita, baixo
    float aspect = 1.0f;         // proporção de referência dos texels na tela (1: quadrados)
    const char* name = "";
};
constexpr float kPsi2DFrameTolerance = 0.10f;
// A moldura de uma textura (nulo: não é moldura).
const Psi2DFrame* psi_2d_find_frame(uint32_t hash);
// Corta "q" (tela do PS2) em out[0..n): devolve 1 (out[0] = q, sem cortar: não mostra a textura inteira ou
// está na proporção) ou 9 (linhas de cima para baixo, colunas da esquerda para a direita na tela). pixel_aspect:
// a largura de um pixel da tela do PS2 na janela dividida pela altura (Psi2DViewport::scale_x / scale_y).
// corner_scale (opcional): pixels da tela do PS2 por texel dos cantos, em x e em y (0 sem cortar).
int psi_2d_nine_slice(const Psi2DQuad& q, const Psi2DFrame& f, float pixel_aspect, Psi2DQuad out[9],
                      float corner_scale[2] = nullptr);

// FontList (0x00264ba8): o pstex_tag* das três texturas de fonte (0x03000005 NFont2, 0x03000003
// SerpLight, 0x030000d8 Medium), que psiDrawSprites desenha sem o meio texel e com filtro de ponto.
extern uint32_t FontList[3];
// O fim de CreateSpaceAndPackTextures (chamada por psiPreGameRun): FontList pelos hashcodes das fontes.
// O resto dela (empacotar as texturas na VRAM) não existe no PC.
void psi_2d_find_font_textures();

// Só no PC: o índice de Tex de uma textura 03xxxxxx carregada (a do hashtable, como psi_2d_find_font_textures acha
// as fontes) e o tamanho dela em texels; -1 sem ela. Os menus Modern desenham assim imagens do jogo (a arte do
// "Press START", o logotipo, a imagem da missão) por baixo do Direct2D (render/ui_modern_app.h).
int32_t psi_2d_texture_slot(uint32_t hash, int* width = nullptr, int* height = nullptr);

// Só no PC: as origens dos SPRITE_DRAW que a próxima psiDrawSprites recebe (mesmo índice); nulo: todas
// as origens ficam no padrão (imagem sem sprite). A lógica (sprite.cpp) aponta para a lista paralela a
// SprBuffList antes de cada chamada; psiDrawSprites zera o ponteiro.
extern const Psi2DOrigin* g_psi_2d_origins;

void psiStartSprites();
void psiEndSprites();
void psiDrawSprites(SPRITE_DRAW* list, int16_t count);

// Só para testes: estado inicial.
void psi_2d_debug_power_on();
