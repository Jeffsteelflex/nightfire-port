#pragma once
// As contas neutras do desenho vetorial fora do Windows (render/ui_vector.h; docs/port.md, "macOS: o desenho vetorial"):
// o que o backend do macOS (render/ui_vector_cg.mm) faz igual ao Direct2D (render/ui_modern_draw_d2d.cpp,
// render/ui_hd_draw_d2d.cpp) sem depender do sistema, para ficar testado em toda plataforma: os pixels presos das linhas
// finas, os recortes alinhados ao pixel, os degradês do Direct2D como pontos para o CoreGraphics, o bloco de linhas do
// texto da camada HD, a tecla, a conversão das substitutas para a escala do GS e o resumo de uma lista de desenho (o
// cache). No Windows só os testes e a ferramenta de comparação (nf_vector_check) usam.

#include <cstdint>
#include <vector>

#include "render/ui_hd.h"
#include "ui/modern_ui.h"

namespace nf {

// ---------------------------------------------------------------- a geometria dos menus Modern (como o Direct2D)

struct UiVecRect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// Um Fill sem cantos: os retângulos com menos de 6 pixels de altura (ou largura) vão para pixels inteiros (os
// sublinhados nítidos), com pelo menos 1 pixel.
UiVecRect ui_vec_snap_fill(UiVecRect r);
// A largura de um traço em pixels (pelo menos 1, arredondada).
float ui_vec_stroke_width(float canvas_width, float scale);
// Um Line: horizontal ou vertical, preso ao meio do pixel quando a largura é ímpar (ao canto quando é par).
void ui_vec_snap_line(float& x0, float& y0, float& x1, float& y1, float width);
// O retângulo de um Stroke: as bordas no meio dos pixels de dentro.
UiVecRect ui_vec_stroke_rect(UiVecRect r);
// Um recorte "aliased" do Direct2D: os pixels cujo centro cai dentro (bordas inteiras).
UiVecRect ui_vec_aliased_clip(UiVecRect r);

// ---------------------------------------------------------------- degradês

// Um ponto de um degradê: a posição (0 a 1) e a cor sem o alfa pré-multiplicado (0 a 1).
struct UiVecStop {
    float offset = 0, r = 0, g = 0, b = 0, a = 0;
};
// O degradê de dois pontos do Direct2D (D2D1_GAMMA_2_2: a conta nos valores sRGB, sem linearizar) como `samples`
// pontos para quem interpola sem pré-multiplicar (o CGGradient): com `premultiplied`, a cor de cada ponto sai da conta
// feita com o alfa pré-multiplicado (a do Direct2D) e volta sem ele.
std::vector<UiVecStop> ui_vec_gradient(const std::vector<UiVecStop>& stops, bool premultiplied, int samples);
// Os pontos dos degradês dos menus Modern: o comum (dois pontos) e o do brilho (Glow: quatro pontos, o miolo, 35% para
// a borda com 62% do alfa a 0,3, 18% a 0,65, a borda); como o ui_modern_draw_d2d.cpp.
std::vector<UiVecStop> ui_vec_modern_gradient(const ui::Color& a, const ui::Color& b, bool glow);
// O setor em degradê da roda: c até o raio de dentro (k = r_dentro / r_fora), c2 no de fora.
std::vector<UiVecStop> ui_vec_sector_gradient(const ui::Color& c, const ui::Color& c2, float k);

// ---------------------------------------------------------------- pintar direto na imagem

// As peças grandes do fundo dos menus (o véu da janela inteira, a vinheta, as faixas em degradê) pintadas direto numa
// imagem BGRA pré-multiplicada, sem o CoreGraphics (que na CPU desenha a vinheta de uma tela cheia em ~10 ms). O mesmo
// resultado do Direct2D: a cobertura das bordas fracionárias do retângulo, a cor do degradê no centro de cada pixel
// (interpolada sem pré-multiplicar, as pontas estendidas), misturada por cima (ONE, ONE_MINUS_SRC_ALPHA). clip: os
// pixels inteiros onde se pode pintar (o recorte aberto; a imagem inteira sem recorte), em coordenadas da janela.
struct UiVecImage {
    uint8_t* bgra = nullptr;
    int w = 0, h = 0;
    int ox = 0, oy = 0;  // o pixel (0, 0) da imagem é o (ox, oy) da janela
};
void ui_vec_fill_rect(UiVecImage& img, UiVecRect r, UiVecRect clip, const ui::Color& c);
// Um degradê linear no retângulo: horizontal de x0 a x1 ou vertical de y0 a y1 (os pontos sem pré-multiplicar).
void ui_vec_fill_linear(UiVecImage& img, UiVecRect r, UiVecRect clip, const std::vector<UiVecStop>& stops, bool horizontal);
// Um degradê radial elíptico (o centro e os raios da elipse e), pintando a área inteira (fora da elipse, a cor da borda).
void ui_vec_fill_radial(UiVecImage& img, UiVecRect e, UiVecRect area, UiVecRect clip, const std::vector<UiVecStop>& stops);

// ---------------------------------------------------------------- a camada HD

// Onde fica cada linha de um texto da camada HD e a compressão horizontal dela (UiHdDevice::draw_text): as linhas presas
// (os trechos entre símbolos) ficam no x0 delas, comprimidas até caber em x1 - x0; as outras formam um bloco com a largura
// da fonte vetorial (`widths`: a largura de cada linha sem o espaço do fim), comprimido até a largura original e
// alinhado como o texto do jogo (align 0 à esquerda, 2 centrado, 4 à direita).
struct UiHdLinePlace {
    float x = 0, sx = 1;
};
std::vector<UiHdLinePlace> ui_hd_place_lines(const UiHdText& t, const std::vector<float>& widths, float fit_min);
// O tamanho do em de um texto (pixels): a altura das maiúsculas da fonte do jogo dividida pela razão maiúsculas/em da
// fonte vetorial, vezes o size do map.json, arredondado a 1/64 (o formato do DirectWrite).
float ui_hd_em(float cap_px, float cap_ratio, float size);

// A tecla da camada HD (UiHdDevice::draw_key): o corpo arredondado (meia linha para dentro), o raio, a largura da borda,
// os três pontos do degradê de cima para baixo, a cor da borda e a da letra (escala do GS, como o jogo).
struct UiHdKeyShape {
    UiVecRect body;
    float radius = 0, line = 1;
    UiVecStop stops[3];
    uint8_t edge[4]{}, ink[4]{};
};
UiHdKeyShape ui_hd_key_shape(const UiHdKey& k);
// A cor na escala do GS (0x80 = 1,0) para 0 a 1.
void ui_hd_gs_colour(const uint8_t rgba[4], float out[4]);

// BGRA (com o alfa pré-multiplicado ou não) para RGBA com a cor sem o alfa e o alfa na escala do GS (0x80 = opaco), como
// as substitutas do Direct2D: os texels transparentes ganham a cor do vizinho visível (até 4 passadas).
void ui_vec_to_gs_rgba(const uint8_t* bgra, uint32_t pitch, int w, int h, bool premultiplied, std::vector<uint8_t>& out);

// ---------------------------------------------------------------- o cache

// Um resumo (XXH3) de tudo o que muda o desenho de uma lista: os comandos (os campos, um a um), a tela virtual e o
// tamanho da janela.
uint64_t ui_vec_list_hash(const ui::DrawList& list, const ui::Canvas& canvas, int window_w, int window_h);
// O resumo de cada comando (os mesmos campos), para achar o começo comum de duas listas: o desenho guarda a imagem
// depois do começo que não mudou (o fundo, a barra de cima) e só redesenha o resto (o brilho do foco, que tremula a cada
// quadro, e o que vem depois).
std::vector<uint64_t> ui_vec_cmd_hashes(const ui::DrawList& list);
// O resumo da tela virtual e da janela (as listas só se comparam com a mesma).
uint64_t ui_vec_canvas_hash(const ui::Canvas& canvas, int window_w, int window_h);

}  // namespace nf
