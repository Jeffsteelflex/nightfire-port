#pragma once
// O 2D do HUD das missões de carro (docs/driving.md seção 28): o módulo Draw do DRIVING.ELF (Draw::DrawSprite, DrawBox,
// DrawQuadC, os lotes DrawBatchedSprite e DrawBatchedTexturedQuad no scratchpad, o modo de mistura, o Z e a matriz do
// modelo) e os dois materiais simples que desenham com eles (USimpleMaterial::Draw, render method MatDebug;
// USimpleTexturedMaterial::Draw, render method MatDebugTexture: o EAGL::DynamicModel com a camada do material).
//
// No PS2 cada Draw do material manda ao EAGL a lista de vértices (as posições, o STQ e as cores) com o GeoPrimState do
// Draw (o 0x003335c0 sem textura, o 0x003335c4 com: o ALPHA_1 em +0x40, o ABE no bit 0x40 de +0x10, o teste de Z), o
// tipo da primitiva (4 a lista de triângulos, 5 a faixa, 10 os SPRITE de dois vértices) e a matriz do modelo (o
// 0x003335c8, nula sem ela); a VU1 transforma pela câmera do HUD (o GOrthoHudView). O port guarda cada Draw de material
// como um HudMatDraw na lista do quadro (hud_frame()), na ordem, com os pacotes das fontes (o FONT_drawtextfa) entre
// eles; o PC (driving/render/gs2d.h) os desenha na tela do PS2.
//
// Divergências (só do port): a câmera ortográfica do GOrthoHudView vira as coordenadas da tela (640 x 448) direto (o
// MatDebugTexture com a matriz da câmera fica no --verify); a textura (o EAGL::TAR) é o número da imagem do port.

#include <cstdint>
#include <vector>

#include "driving/render/ea_font.h"
#include "driving/render/gs2d.h"

namespace drv {

// O GeoPrimState dos dois materiais (os campos que o HUD escreve).
struct HudPrimState {
    uint64_t flags = 0x40;            // +0x10 (o bit 0x40: o ABE do PRIM)
    uint64_t misc = 1;                // +0x30 (o bit 0: o STATE_SetDefaultRenderState o limpa)
    uint64_t alpha = 0x8000000044ull; // +0x40 o ALPHA_1
    int32_t depth_test = 1;           // o SetDepthTestMethod
};

// Um Draw de material.
struct HudMatDraw {
    bool textured = false;
    int32_t prim = 0;                 // 4 lista de triângulos, 5 faixa, 10 SPRITE
    uint32_t image = 0;               // a textura (o número da imagem do port)
    HudPrimState state;
    bool has_matrix = false;
    float matrix[4][4] = {};
    std::vector<float> pos;           // 4 floats por vértice
    std::vector<float> stq;           // 4 floats por vértice (só com textura)
    std::vector<uint32_t> colour;     // RGBA na escala do GS
};
// Um item do quadro: um Draw de material ou um pacote de fonte.
struct HudFrameItem {
    bool is_packet = false;
    HudMatDraw draw;
    GsPacket packet;
};
std::vector<HudFrameItem>& hud_frame();

// Os globais do Draw (0x003335c0..0x003335f0) e as listas dos lotes no scratchpad.
struct DrawGlobals {
    HudPrimState untextured;          // 0x003335c0
    HudPrimState textured;            // 0x003335c4
    const float* model = nullptr;     // 0x003335c8 (Draw::SetZDepth(MATRIX4 *) 0x0019f840: a matriz do modelo)
    float z = 1.0f;                   // 0x003335cc (o Z dos vértices)
    int32_t quad_count = 0;           // 0x003335d4 o lote dos quadrados com textura
    uint32_t quad_tex = 0;            // 0x003335e0
    int32_t sprite_count = 0;         // 0x003335ec o lote dos SPRITE
    uint32_t sprite_tex = 0;          // 0x003335f0
    // O scratchpad (0x70000000: os STQ dos SPRITE, 0x700004c0 as posições, 0x70000980 as cores; 0x70002000,
    // 0x700024c0, 0x70002980 os dos quadrados).
    float sprite_stq[0x4c][4] = {}, sprite_pos[0x4c][4] = {};
    uint32_t sprite_col[0x4c] = {};
    float quad_stq[0x4c][4] = {}, quad_pos[0x4c][4] = {};
    uint32_t quad_col[0x4c] = {};
};
DrawGlobals& draw_globals();
void draw_power_on();

// [driving] 0x0019f840 Draw::SetZDepth(MATRIX4 *) (a matriz do modelo dos desenhos seguintes; nula: nenhuma)
void Draw_SetModelMatrix(const float* m);
// [driving] 0x0019f8b0 Draw::SetZDepth(float), 0x0019f8c0 SetZDepthFar (10,24), 0x0019f8d8 SetZDepthNear (1)
void Draw_SetZDepth(float z);
void Draw_SetZDepthFar();
void Draw_SetZDepthNear();
// [driving] 0x001a0f80 Draw::SetNormalBlendMode / 0x001a0fb8 SetAdditiveBlendMode(FIX)
void Draw_SetNormalBlendMode(HudPrimState& s);
void Draw_SetAdditiveBlendMode(HudPrimState& s, uint32_t fix);
// [driving] 0x0019f9c8 Draw::DrawSprite(x, y, w, h, cor, uv[2] (STQ), textura)
void Draw_DrawSprite(float x, float y, float w, float h, uint32_t colour, const float uv[2][4], uint32_t tex);
// [driving] 0x0019fe08 Draw::DrawBox(x, y, w, h, cor)
void Draw_DrawBox(float x, float y, float w, float h, uint32_t colour);
// [driving] 0x001a0480 Draw::DrawQuadC(cantos[4], cores[4]) (pelo 0x0019f850: a faixa de quatro)
void Draw_DrawQuadC(const float corners[4][4], const uint32_t colours[4]);
// [driving] 0x001a0450 Draw::DrawQuad(cantos[4], cor)
void Draw_DrawQuad(const float corners[4][4], uint32_t colour);
// [driving] 0x001a1628 Draw::DrawBatchedSprite(x, y, w, h, cor, uv[2], textura): textura 0 descarrega.
void Draw_DrawBatchedSprite(float x, float y, float w, float h, uint32_t colour, const float uv[2][4], uint32_t tex);
// [driving] 0x001a0ff0 Draw::DrawBatchedTexturedQuad(p0, p1, p2, p3, cor, uv[4], textura): textura 0 descarrega.
void Draw_DrawBatchedTexturedQuad(const float p0[4], const float p1[4], const float p2[4], const float p3[4],
                                  uint32_t colour, const float uv[4][4], uint32_t tex);
// [driving] 0x002cf5b8 USimpleMaterial::Draw(GeoPrimState *, tipo, contagem, MATRIX4 *) (as posições e as cores da
// camada) / 0x002cf9a0 USimpleTexturedMaterial::Draw (e o STQ e a textura): o Draw do material no quadro.
void USimpleMaterial_Draw(const HudPrimState& s, int32_t prim, int32_t count, const float* model, const float* pos,
                          const uint32_t* colour);
void USimpleTexturedMaterial_Draw(const HudPrimState& s, int32_t prim, int32_t count, const float* model,
                                  const float* pos, const float* stq, const uint32_t* colour, uint32_t tex);

// O quadro no PC: os itens em lotes do GS na tela do PS2 (640 x 448). view: a escala e o deslocamento da câmera do HUD
// (x * view[0] + view[2], y * view[1] + view[3]: o GOrthoHudView::DoRender, o GHud +0x468..+0x474), nos materiais (os
// pacotes das fontes já estão na tela); nulo: nenhum.
void hud_frame_to_batches(const std::vector<HudFrameItem>& items, std::vector<Gs2dBatch>& out,
                          const float* view = nullptr);

}  // namespace drv
