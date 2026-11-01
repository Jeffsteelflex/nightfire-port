#pragma once
// A conferência dos render methods dos efeitos contra os microprogramas originais (docs/driving.md seção 25): o
// interpretador da VU1 (platform/vu_interp.h) roda o código do método sobre a memória da VU montada como o
// EAGL::DrawArray a deixa (os parâmetros nos endereços que o microprograma lê a partir do TOP), e a saída que ele grava
// para o XGKICK é comparada com a referência de fx_render.h, palavra a palavra (a mesma aritmética da VU dos dois lados:
// as diferenças têm de ser zero).
//
// RenderSpritesRot (TOP: +0 a contagem, +1..+4 a ModelViewMatrix, +5 o pacote da textura, +0xc as Coordinates, +0x9c os
// UVUVs, +0x12c as Colours (o V4-8: quatro inteiros), +0x1bc o State, +0x1c4 o Control, +0x1c5 a etiqueta GIF dos
// vértices e +0x1c6..+0x1d7 os seis vértices de um retângulo); MatDebugTexture (TOP: +0 a contagem, +1..+4 a matriz,
// +5 as Coordinates, +0x51 o STQ, +0x9d as Colours, +0xe9 a textura, +0xf0 o State (o PRIM em +0xf1: 3 a lista de
// triângulos), +0xf8 a etiqueta e +0xf9 os vértices, três qwords cada). Só o caminho sem recorte do MatDebugTexture é
// conferido (os triângulos dentro do volume de recorte); o recorte na VU1 é a divergência de sempre (o Direct3D recorta).

#include <cstdint>
#include <string>
#include <vector>

#include "driving/fx/fx_render.h"

namespace drv {

struct FxVerifyStats {
    int runs = 0, failed_runs = 0, sprites = 0, triangles = 0, vertices = 0;
    int st_diff = 0, rgba_diff = 0, xyz_diff = 0, adc_sprites = 0;
    std::string first_error;
};

// Um retângulo do RenderSpritesRot (code: as instruções, inferior | superior << 32).
void fx_verify_sprite(const std::vector<uint64_t>& code, const FxVuGlobals& g, const float mvp[4][4],
                      const float control[4], const float coord[4], const float uvuv[4], const uint32_t colour[4],
                      FxVerifyStats& s);
// Um triângulo do MatDebugTexture (dentro do volume de recorte).
void fx_verify_triangle(const std::vector<uint64_t>& code, const FxVuGlobals& g, const float mvp[4][4],
                        const float pos[3][4], const float stq[3][4], const uint32_t colour[3][4], FxVerifyStats& s);
// Os desenhos do HUD (docs/driving.md seção 28) nos dois métodos, com o tipo de primitiva do GS (3 a lista, 4 a faixa, 6
// os SPRITE) e n vértices dentro do volume de recorte: o MatDebug (TOP: +0 a contagem, +1..+4 a matriz, +5 as
// Coordinates, +0x79 as Colours, +0xed o State (o PRIM em +0xee), +0xf5 a etiqueta e +0xf6 os vértices, dois qwords
// cada: a cor e o XYZ; n <= 0x74) e o MatDebugTexture (como acima; n <= 0x4c); stq nulo no MatDebug.
void fx_verify_list(const std::vector<uint64_t>& code, bool textured, const FxVuGlobals& g, const float mvp[4][4],
                    int prim, int n, const float (*pos)[4], const float (*stq)[4], const uint32_t (*colour)[4],
                    FxVerifyStats& s);
// Um pedaço do BondDebris (seção 29): o microprograma com um pedaço só (ControlData.y = 1; TOP: +0 o número de vértices,
// +1..+4 a viewProj, +5 a textura, +0xc o ControlData, +0xd as posições, +0x2b o UV (u, v, 1, 1), +0x67 o State, +0x6f o
// SimTick, +0x71 a posição, +0xb1 o quatérnio, +0xf1 a cor, +0x131 a velocidade; a saída em +0x177, três qwords por
// vértice) contra bond_debris_mvp e o MatDebugTexture de cada vértice; só os pedaços inteiros dentro do recorte.
void fx_verify_debris(const std::vector<uint64_t>& code, const FxVuGlobals& g, const float viewproj[4][4],
                      const float simtick[4], const float offset[4], const float velocity[4], const float quat[4],
                      const uint32_t colour[4], const float* coords, const float* uv, int count, FxVerifyStats& s);
// O triângulo cabe no volume de recorte (|x|, |y|, |z| < w nos três vértices, com folga): o caminho sem recorte.
bool fx_triangle_inside(const float mvp[4][4], const float pos[3][4]);

}  // namespace drv
