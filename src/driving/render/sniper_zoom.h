#pragma once
// A luneta do fuzil de Bond na seção do helicóptero de Paris (o RSniperZoom do DRIVING.ELF, RSniperZoom.obj; docs/driving.md
// seção 30): no pós-processamento do quadro (RRenderWorldCamera::DrawPostProcessingEffects) o GrabBackBuffer copia o
// quadro já desenhado, reduzido, para um buffer de 64 x 64 (ROffscreenBuffer::MakeTexCopy com a imagem subamostrada do
// RPostProcessing), e o Draw desenha dois anéis em faixa de triângulos (42 vértices cada) com essa cópia, em volta do
// cursor da mira (o RPlayerCamera +0x1c0): o de dentro, dos 65 aos 150 pixels, entra do transparente ao cinza 0x5c com o
// alfa da luneta; o de fora, dos 150 aos 530, vai do cinza ao preto. A imagem fica embaçada e escura em volta do círculo
// da luneta (que o GHud::DrawCrossHairs desenha por cima). Ligado e o alfa vêm do GHud (driving/game/hud.h,
// sniper_zoom()): o DrawCrossHairs da luneta os escreve e o AimOff desliga; o modo de cinema da missão desliga no
// GrabBackBuffer.
//
// Portado do MIPS do PS2 (o leiaute de 0xca0 bytes do PS2); a cópia do quadro e o desenho são do renderizador do port
// (driving_app/drv_renderer.h: o lote de pós-processamento com a textura da cena reduzida).

#include <cstdint>

namespace drv {

constexpr int kSniperZoomVerts = 42;

// RSniperZoom (0xca0 bytes; o fgThis_RSniperZoom 0x3353ec, criado pelo RRenderHigh::InitGameRender).
struct SniperZoomData {
    uint32_t vtable;                           // +0x000 0x3934a0
    int32_t on;                                // +0x004 o GHud::DrawCrossHairs liga, o AimOff desliga
    uint32_t alpha;                            // +0x008 0..255 (0xff no construtor)
    uint8_t offscreen[0x24];                   // +0x00c o ROffscreenBuffer (64 x 64, 24 bits; +0x14 a textura)
    float pos_inner[kSniperZoomVerts][4];      // +0x030 o anel de dentro (pixels da tela; z e w 1)
    float pos_outer[kSniperZoomVerts][4];      // +0x2d0 o de fora
    float uv_inner[kSniperZoomVerts][4];       // +0x570 (a posição sobre a largura e a altura da tela)
    float uv_outer[kSniperZoomVerts][4];       // +0x810
    uint32_t colour_inner[kSniperZoomVerts];   // +0xab0 RGBA na escala do GS (o R no byte de baixo)
    uint32_t unknown_b58[2];
    uint32_t colour_outer[kSniperZoomVerts];   // +0xb60
    uint32_t unknown_c08[2];
    uint8_t material[0x90];                    // +0xc10 o USimpleTexturedMaterial (a mistura 0x44, o teste de Z 1)
};
static_assert(sizeof(SniperZoomData) == 0xca0);

// O que os vértices leem de fora: a tela do RRenderer (fgRenderer +0x50, +0x54) e o cursor da mira do RPlayerCamera do
// mundo (+0x1c0, +0x1c4: pixels a partir do centro).
struct SniperZoomView {
    int32_t screen_w = 640, screen_h = 448;
    float cursor_x = 0.0f, cursor_y = 0.0f;
};

// [driving] 0x001ed480 RSniperZoom::RSniperZoom (sem o ROffscreenBuffer e o material, que são do renderizador)
void RSniperZoom_Construct(SniperZoomData& z);
// [driving] 0x001ed5e0 RSniperZoom::GrabBackBuffer: o modo de cinema da missão (SMissionManager +0x3cc) desliga; devolve
// se a cópia do quadro deve ser feita (ligado).
bool RSniperZoom_GrabBackBuffer(SniperZoomData& z, uint32_t mission_cinematic);
// [driving] 0x001ed7b0 (sem nome): um vértice do anel no raio `radius` (pixels) na fração `t` da volta.
void RSniperZoom_RingVertex(const SniperZoomView& v, float radius, float t, float pos[4], float uv[4]);
// [driving] 0x001ed8d8 RSniperZoom::SetupVerts
void RSniperZoom_SetupVerts(SniperZoomData& z, const SniperZoomView& v);
// [driving] 0x001ed650 RSniperZoom::Draw: desligado, nada (devolve falso); ligado, os vértices (o desenho dos dois anéis,
// primeiro o de fora, depois o de dentro, é do renderizador).
bool RSniperZoom_Draw(SniperZoomData& z, const SniperZoomView& v);

// O do port (o fgThis_RSniperZoom), construído na primeira chamada.
SniperZoomData& sniper_zoom_object();

}  // namespace drv
