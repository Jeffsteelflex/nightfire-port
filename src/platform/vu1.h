#pragma once
// O que o microprograma da VU1 (e as passadas de névoa do GS) faz com a cor, a UV e a profundidade de
// cada vértice, como referência na CPU. O renderizador faz as mesmas contas nos shaders
// (src/render/renderer_d3d11.cpp); estas funções servem aos testes e ao lado da CPU (psi_draw).
// Rotinas e fórmulas em docs/formats.md, "Microprograma da VU1", lidas com tools/vudis.py.
//
// Escalas: cor do vértice como a VU1 a recebe (UNPACK V4-8u: inteiros 0..255, 0x80 = 1.0 no GS);
// normal das malhas rígidas como inteiro com sinal (V4-8, cerca de 127 por unidade), e a das malhas
// com skinning como o SkinIt a deixa (normal * 128 girada pelo primeiro osso, truncada); TAmbient é
// o tweak / 255 (0..1); TColor0/1 têm r, g, b = cor da luz (0..255) x intensidade x multiplicador e
// w = 1 / raio².

#include <cstdint>

#include "engine/ps2types.h"

namespace vu1 {

// Constantes do PS2 (.sdata) da projeção (PS2WorldViewVU1Kick2): Ps2Near2 e Ps2Far2 são as
// distâncias dos planos de perto e de longe em metros; o Z do GS vai de Ps2Far1 (no plano de perto)
// a Ps2Near1 (no infinito). Ps2Far1 muda por fase (psiPreGameRun).
constexpr float kPs2Near1 = 8.0f;
constexpr float kPs2Near2 = 0.1f;
constexpr float kPs2Far2 = 16777216.0f;

// As luzes de um desenho como a VU1 as vê (qw 8, 9, 12 e 13 da cadeia de matrizes).
struct Lights {
    float pos[2][4];    // no espaço dos vértices (objeto rígido) ou no mundo (skinning)
    float color[2][4];  // TColor0, TColor1
};

// Rotinas 0x25e (ROTATE_FAST, sem nada), 0x2a3 (ROTATE_TWEEK) e 0x2e0 (ROTATE_ENV, a cor):
// cor = FTOI0(TAmbient * cor), nos quatro canais. Sem tweak TAmbient é 1 e a cor passa intacta.
void tweak_vertex(const float colour[4], const float tambient[4], float out[4]);

// Rotina 0x340 (ROTATE_LIGHT; também com tweak), por luz i:
//   D = luz - p, att = max(0, 1 / |D|² - TColor.w), s = max(0, att * (n . D)), com n = normal / 128
//   rgb = min(255, TAmbient * cor + TColor1 * s1 + TColor0 * s0), a = min(255, TAmbient.a * cor.a)
// e FTOI0 (trunca) nos quatro. normal_vu: a normal na escala da VU (ver acima); p: posição do vértice
// no mesmo espaço das luzes.
void light_vertex(const float colour[4], const float normal_vu[3], const float p[3], const float tambient[4],
                  const Lights& lights, float out[4]);

// FillMatrixChainRot: as luzes do mundo vão para o espaço do objeto pela inversa da matriz do objeto
// calculada como transposta (só certa para rotação + translação): L' = (L - t) * R^T, com R as três
// primeiras linhas e t a quarta (vetor-linha). w fica 1 (TLight.w).
void lights_to_local(const MATRIX& world, const float in[2][4], float out[2][4]);

// Rotina 0x2e0 (ROTATE_ENV): s = 0.5 + (n . linha 0 da câmera) / 256, t = 0.5 + (n . linha 1) / 256,
// com n a normal na escala da VU (inteiros) e as linhas da matriz da câmera (viewer_tag::camera) no
// mundo. (FillMatrixChainRot põe as linhas / 256 e 0.5 nos qw 16-19 quando a caixa 0 tem a flag 1.)
void env_st(const float normal_vu[3], const float row0[3], const float row1[3], float st[2]);

// Z do GS (24 bits) de um ponto a z metros na frente da câmera: a matriz de tela dá
// A / z + B, com A = Ps2Near2 * Ps2Far2 * (Ps2Far1 - Ps2Near1) / (Ps2Far2 - Ps2Near2) e
// B = (Ps2Near1 * Ps2Far2 - Ps2Far1 * Ps2Near2) / (Ps2Far2 - Ps2Near2); o FTOI4 do microprograma e o
// XYZF2 empacotado (bits 4-27) deixam a parte inteira, módulo 2^24.
float screen_z(float view_z, float far1);
uint32_t gs_z(float view_z, float far1);
void screen_z_coefficients(float far1, float& a, float& b);

// Névoa (psiDrawFogPass, depois da lista opaca): a passada RGB2A copia os bits 8-15 do Z de cada pixel
// para o alfa do quadro, e o sprite de névoa (Z 0x7fff, teste GREATER, ALPHA (Cd - Cs) * Ad + Cs, Cs = a
// cor da névoa) mistura onde Z < 0x7fff. Devolve quanto da cor do pixel fica (Ad / 128); o resto é a cor
// da névoa. O Z apagado é 1 (CreateZClearSprite): onde nada escreveu Z, só a névoa.
float fog_keep(uint32_t z);

// FillMatrixChainRot com AlphaFogObjects (os transparentes depois da passada de névoa): o alfa do
// tweak passa a (int(Z do centro da caixa 0) >> 7, no máximo 255) * olda / 255, com as conversões do
// EE (float para int satura; divisão inteira trunca; o resultado vira uchar).
uint8_t alpha_fog(float screen_z_of_centre, uint8_t olda);

}  // namespace vu1
