#pragma once
// O 2D das missões de carro no PC (só do port; docs/driving.md seção 28): o que o GS do PS2 faria com os pacotes do GIF
// que o HUD, as fontes e as legendas mandam direto para ele (driving/render/ea_font.h, o GHud e a GGallery): as tags
// do GIF (PACKED com A+D, ou com registradores), o PRIM (pela tag com PRE, ou o registrador), o RGBAQ, o UV/ST, os
// XYZ2/XYZ3 (o chute) e o estado (TEX0_1, TEX1_1, ALPHA_1, TEST_1, ZBUF_1, PRMODE). Sai uma lista de lotes de
// triângulos na tela do PS2 (x e y em pixels, já sem o XYOFFSET), com a cor na escala do GS (0x80 = 1,0) e o UV em
// texels; o nf_driving os desenha por cima da cena (DrvFxBatch no recorte).
//
// Só o que o 2D usa: SPRITE, TRIANGLE, TRISTRIP e TRIFAN; o UV (FST=1) ou o ST com o Q; o TEX0 leva no TBP0 o número
// da imagem do port (ea_font_texture_regs e o lugar das texturas do EAGL no HUD). O resto dos registradores é ignorado.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace drv {

struct Gs2dVertex {
    float x = 0, y = 0;     // na tela do PS2 (pixels)
    uint32_t z = 0;
    float u = 0, v = 0;     // em texels (FST) ou normalizados (ST/Q)
    uint32_t rgba = 0x80808080u;
};
struct Gs2dBatch {
    uint32_t image = 0;        // o TBP0 do TEX0_1 (a imagem do port)
    bool textured = false;     // PRIM.TME
    bool st = false;           // o UV normalizado (o ST/Q: FST = 0)
    bool linear = false;       // TEX1_1.MMAG
    bool blend = false;        // PRIM.ABE
    uint64_t alpha = 0x8000000044ull;
    uint64_t test = 0x30000ull;
    bool zwrite = false;
    std::vector<Gs2dVertex> verts;  // três por triângulo
};

// O XYOFFSET a tirar das coordenadas (x 16), o do contexto do EAGL (0 no port).
struct Gs2dState {
    int32_t offset_x = 0, offset_y = 0;
    uint64_t prim = 0, rgbaq = 0x3f80000080808080ull, uv = 0, st = 0;
    uint64_t tex0 = 0, tex1 = 0, alpha = 0x8000000044ull, test = 0x30000ull, zbuf = 0, prmode = 0;
    bool prmodecont = true;
    float q = 1.0f;
    Gs2dVertex queue[3];
    int queued = 0;
};

// As imagens do 2D do port (o lugar das texturas do EAGL na VRAM: a fonte das legendas, as texturas das galerias):
// RGBA de 8 bits com o alfa na escala do GS (0x80 = opaco). O número (de 1 em diante) vai no TBP0 do TEX0.
struct UiImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
uint32_t ui_image_add(UiImage img);
const UiImage* ui_image_get(uint32_t id);
uint32_t ui_image_count();
void ui_images_clear();
// Troca o conteúdo de uma imagem (a moça do fundo da pausa, que muda a cada quadro: driving/game/hud_pause.h); o
// renderizador a sobe de novo (ui_image_take_dirty: as trocadas desde a última chamada).
bool ui_image_update(uint32_t id, UiImage img);
std::vector<uint32_t> ui_image_take_dirty();

// Interpreta as palavras de um pacote (do começo: a tag DMA e os códigos do VIF são pulados até a primeira tag do GIF
// depois do DIRECT); acrescenta os triângulos em `out` (um lote novo quando o estado muda).
void gs2d_run(const uint64_t* words, size_t count, Gs2dState& s, std::vector<Gs2dBatch>& out);
// Um par A+D só (para quem escreve os registradores sem tag).
void gs2d_write(uint8_t reg, uint64_t value, Gs2dState& s, std::vector<Gs2dBatch>& out);

}  // namespace drv
