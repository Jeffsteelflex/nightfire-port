// As contas neutras do desenho vetorial fora do Windows (render/ui_vector_util.h; docs/port.md, "macOS: o desenho
// vetorial"): o que o backend do macOS faz igual ao Direct2D sem depender do sistema. Os números esperados vêm das regras
// do ui_modern_draw_d2d.cpp e do ui_hd_draw_d2d.cpp (os pixels presos, os recortes, os degradês, o bloco de texto da
// camada HD, a tecla, a conversão para a escala do GS) e o resumo do cache muda quando o desenho muda.

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>

#include "render/ui_vector_util.h"
#include "test_framework.h"

using namespace nf;

TEST(ui_vector_snapping_like_direct2d) {
    // Um sublinhado de 1,6 pixel a 10,3: 2 pixels inteiros a partir de 10.
    UiVecRect f = ui_vec_snap_fill(UiVecRect{100.2f, 10.3f, 300.7f, 11.9f});
    CHECK(f.y0 == 10.0f && f.y1 == 12.0f && f.x0 == 100.2f && f.x1 == 300.7f);
    // Fino nos dois eixos; menos de meio pixel ainda vira 1.
    f = ui_vec_snap_fill(UiVecRect{5.6f, 7.0f, 5.9f, 7.2f});
    CHECK(f.x0 == 6.0f && f.x1 == 7.0f && f.y0 == 7.0f && f.y1 == 8.0f);
    // Largo: fica como está (as bordas com antisserrilhado).
    f = ui_vec_snap_fill(UiVecRect{1.5f, 2.5f, 20.5f, 30.5f});
    CHECK(f.x0 == 1.5f && f.y1 == 30.5f);
    CHECK(ui_vec_stroke_width(1.0f, 0.75f) == 1.0f && ui_vec_stroke_width(2.0f, 1.333f) == 3.0f &&
          ui_vec_stroke_width(0.2f, 1.0f) == 1.0f);
    // Linhas: horizontal ímpar no meio do pixel, par no canto; vertical igual; inclinada não muda.
    float x0 = 3.3f, y0 = 9.7f, x1 = 50.0f, y1 = 9.7f;
    ui_vec_snap_line(x0, y0, x1, y1, 1.0f);
    CHECK(y0 == 9.5f && y1 == 9.5f && x0 == 3.3f);
    x0 = 12.8f, y0 = 1.0f, x1 = 12.8f, y1 = 40.0f;
    ui_vec_snap_line(x0, y0, x1, y1, 2.0f);
    CHECK(x0 == 12.0f && x1 == 12.0f);
    x0 = 1.0f, y0 = 1.2f, x1 = 5.0f, y1 = 7.9f;
    ui_vec_snap_line(x0, y0, x1, y1, 1.0f);
    CHECK(y0 == 1.2f && y1 == 7.9f);
    const UiVecRect s = ui_vec_stroke_rect(UiVecRect{10.7f, 20.2f, 110.1f, 60.9f});
    CHECK(s.x0 == 10.5f && s.y0 == 20.5f && s.x1 == 109.5f && s.y1 == 59.5f);
    // O recorte "aliased": os pixels com o centro dentro.
    const UiVecRect c = ui_vec_aliased_clip(UiVecRect{10.4f, 10.6f, 20.5f, 20.49f});
    CHECK(c.x0 == 10.0f && c.y0 == 11.0f && c.x1 == 20.0f && c.y1 == 20.0f);
}

TEST(ui_vector_gradients_premultiplied_and_glow) {
    // Vermelho opaco a azul transparente: no meio, com o alfa pré-multiplicado, vermelho a 50% (sem a mancha roxa da conta
    // sem pré-multiplicar).
    const std::vector<UiVecStop> two = {{0.0f, 1, 0, 0, 1}, {1.0f, 0, 0, 1, 0}};
    const std::vector<UiVecStop> pre = ui_vec_gradient(two, true, 5);
    CHECK(pre.size() == 5);
    CHECK_NEAR(pre[2].offset, 0.5, 1e-6);
    CHECK_NEAR(pre[2].a, 0.5, 1e-6);
    CHECK_NEAR(pre[2].r, 1.0, 1e-6);
    CHECK_NEAR(pre[2].b, 0.0, 1e-6);
    // O ponto transparente do fim ganha a cor do lado com alfa.
    CHECK_NEAR(pre[4].r, 1.0, 1e-6);
    // Sem pré-multiplicar: os pontos como vieram.
    const std::vector<UiVecStop> straight = ui_vec_gradient(two, false, 5);
    CHECK(straight.size() == 2 && straight[1].b == 1.0f);
    // Os pontos de dentro entram (um degradê de três pontos amostrado em 3 mantém o do meio).
    const std::vector<UiVecStop> three = {{0.0f, 0, 0, 0, 1}, {0.3f, 1, 1, 1, 1}, {1.0f, 0, 0, 0, 1}};
    const std::vector<UiVecStop> t3 = ui_vec_gradient(three, true, 3);
    bool has_mid = false;
    for (const UiVecStop& st : t3) has_mid |= std::fabs(st.offset - 0.3f) < 1e-6f && st.r == 1.0f;
    CHECK(has_mid && t3.size() == 4);
    // O brilho dos menus: quatro pontos com 62% e 18% do alfa do miolo.
    const std::vector<UiVecStop> g = ui_vec_modern_gradient(ui::Color{1, 0.5f, 0.2f, 0.4f}, ui::Color{0.6f, 0.2f, 0.1f, 0.0f}, true);
    CHECK(g.size() == 4);
    CHECK_NEAR(g[1].offset, 0.3, 1e-6);
    CHECK_NEAR(g[1].a, 0.4 * 0.62, 1e-6);
    CHECK_NEAR(g[1].r, 1.0 + (0.6 - 1.0) * 0.35, 1e-6);
    CHECK_NEAR(g[2].a, 0.4 * 0.18, 1e-6);
    CHECK_NEAR(g[2].r, 0.6, 1e-6);
    CHECK(g[3].a == 0.0f);
    const std::vector<UiVecStop> sec = ui_vec_sector_gradient(ui::Color{1, 1, 1, 0}, ui::Color{1, 0.8f, 0.4f, 0.5f}, 0.75f);
    CHECK(sec.size() == 3 && sec[1].offset == 0.75f && sec[1].a == 0.0f && sec[2].a == 0.5f);
}

TEST(ui_vector_hd_text_block_and_key) {
    UiHdText t;
    t.align = 2;  // centrado
    UiHdLine a, b, p;
    a.x0 = 100;
    a.x1 = 300;
    b.x0 = 120;
    b.x1 = 280;
    p.x0 = 400;
    p.x1 = 450;
    p.pinned = true;
    t.lines = {a, b, p};
    // A fonte vetorial mais larga: o bloco (250 contra 200) comprime até 0,8 e centra no meio do original (200).
    std::vector<UiHdLinePlace> pl = ui_hd_place_lines(t, {250, 150, 100}, 0.7f);
    CHECK_NEAR(pl[0].sx, 0.8, 1e-6);
    CHECK_NEAR(pl[0].x, 200 - 250 * 0.8 * 0.5, 1e-4);
    CHECK_NEAR(pl[1].x, pl[0].x + 20 * 0.8, 1e-4);
    // A presa: no x0 dela, comprimida até caber (50 de 100), mas não abaixo do fit_min.
    CHECK(pl[2].x == 400.0f);
    CHECK_NEAR(pl[2].sx, 0.7, 1e-6);
    // Mais estreita: sem compressão; à direita encosta no x1.
    t.align = 4;
    pl = ui_hd_place_lines(t, {150, 100, 30}, 0.7f);
    CHECK(pl[0].sx == 1.0f && pl[2].sx == 1.0f);
    CHECK_NEAR(pl[0].x, 300 - 150, 1e-4);
    // O em: altura das maiúsculas / razão, em 1/64 de pixel.
    CHECK_NEAR(ui_hd_em(14.0f, 0.7f, 1.0f), 20.0, 1e-6);
    CHECK_NEAR(ui_hd_em(10.0f, 0.71f, 1.0f), std::lround(10.0 / 0.71 * 64.0) / 64.0, 1e-6);
    CHECK(ui_hd_em(0.1f, 0.7f, 1.0f) == 1.0f);
    // A tecla: a borda é 6% da altura (pelo menos 1), o raio 22%, o alfa do símbolo no degradê e nas cores.
    UiHdKey k;
    k.x0 = 10;
    k.y0 = 20;
    k.x1 = 60;
    k.y1 = 50;
    k.rgba[3] = 0x40;
    const UiHdKeyShape s = ui_hd_key_shape(k);
    CHECK_NEAR(s.line, 1.8, 1e-6);
    CHECK_NEAR(s.radius, 6.6, 1e-5);
    CHECK_NEAR(s.body.x0, 10.9, 1e-5);
    CHECK_NEAR(s.body.y1, 49.1, 1e-5);
    CHECK(s.stops[0].a == 0.5f && s.stops[1].offset == 0.55f);
    CHECK(s.edge[3] == 58 && s.ink[3] == 64 && s.ink[0] == 0x7e);
    float c[4];
    const uint8_t gs[4] = {0x80, 0x40, 0xff, 0x20};
    ui_hd_gs_colour(gs, c);
    CHECK(c[0] == 1.0f && c[1] == 0.5f && c[2] == 1.0f && c[3] == 0.25f);
}

TEST(ui_vector_direct_fills_like_direct2d) {
    // Um retângulo de 1,5 a 3,5 numa linha de 5 pixels: meio, inteiro, meio (a cobertura das bordas fracionárias), por
    // cima do transparente (BGRA pré-multiplicado).
    std::vector<uint8_t> px(5 * 4, 0);
    UiVecImage img{px.data(), 5, 1, 0, 0};
    const UiVecRect all{0, 0, 5, 1};
    ui_vec_fill_rect(img, UiVecRect{1.5f, 0, 3.5f, 1}, all, ui::Color{1, 1, 1, 1});
    CHECK(px[0 * 4 + 3] == 0 && px[1 * 4 + 3] == 128 && px[2 * 4 + 3] == 255 && px[3 * 4 + 3] == 128 && px[4 * 4 + 3] == 0);
    CHECK(px[1 * 4 + 0] == 128 && px[2 * 4 + 2] == 255);
    // Por cima do que há: vermelho a meio alfa sobre o branco opaco dá (255, 128, 128) opaco.
    ui_vec_fill_rect(img, UiVecRect{2, 0, 3, 1}, all, ui::Color{1, 0, 0, 0.5f});
    CHECK(px[2 * 4 + 2] == 255 && std::abs(px[2 * 4 + 1] - 128) <= 1 && std::abs(px[2 * 4 + 0] - 128) <= 1 && px[2 * 4 + 3] == 255);
    // O recorte (pixels inteiros) corta.
    std::fill(px.begin(), px.end(), uint8_t(0));
    ui_vec_fill_rect(img, UiVecRect{0, 0, 5, 1}, UiVecRect{1, 0, 2, 1}, ui::Color{0, 0, 1, 1});
    CHECK(px[0 * 4 + 3] == 0 && px[1 * 4 + 0] == 255 && px[2 * 4 + 3] == 0);
    // Degradê horizontal do preto ao branco em 4 pixels: a cor no centro de cada um (1/8, 3/8, 5/8, 7/8).
    std::vector<uint8_t> g(4 * 4, 0);
    UiVecImage gi{g.data(), 4, 1, 0, 0};
    ui_vec_fill_linear(gi, UiVecRect{0, 0, 4, 1}, UiVecRect{0, 0, 4, 1}, {{0.0f, 0, 0, 0, 1}, {1.0f, 1, 1, 1, 1}}, true);
    CHECK(g[0] == 32 && g[4] == 96 && g[8] == 159 && g[12] == 223 && g[3] == 255);
    // Vertical numa imagem com origem deslocada (a janela começa em y = 10): a mesma conta na janela.
    std::vector<uint8_t> v(1 * 2 * 4, 0);
    UiVecImage vi{v.data(), 1, 2, 0, 10};
    ui_vec_fill_linear(vi, UiVecRect{0, 10, 1, 12}, UiVecRect{0, 0, 100, 100}, {{0.0f, 0, 0, 0, 1}, {1.0f, 1, 1, 1, 1}}, false);
    CHECK(v[0] == 64 && v[4] == 191);
    // Radial: o centro com a cor do meio, fora da elipse a da borda (a área inteira pintada).
    std::vector<uint8_t> r(9 * 9 * 4, 0);
    UiVecImage ri{r.data(), 9, 9, 0, 0};
    ui_vec_fill_radial(ri, UiVecRect{0, 0, 9, 9}, UiVecRect{0, 0, 9, 9}, UiVecRect{0, 0, 9, 9},
                       {{0.0f, 1, 0, 0, 1}, {1.0f, 0, 0, 1, 1}});
    const uint8_t* mid = &r[(4 * 9 + 4) * 4];
    const uint8_t* corner = &r[0];
    CHECK(mid[2] == 255 && mid[0] == 0 && mid[3] == 255);
    CHECK(corner[0] == 255 && corner[2] == 0);
}

TEST(ui_vector_gs_rgba_and_cache_hash) {
    // Um texel vermelho a meio alfa pré-multiplicado, um transparente (ganha a cor de um vizinho) e um opaco azul.
    const uint8_t bgra[12] = {0, 0, 128, 128, 0, 0, 0, 0, 255, 0, 0, 255};
    std::vector<uint8_t> out;
    ui_vec_to_gs_rgba(bgra, 12, 3, 1, true, out);
    CHECK(out.size() == 12);
    CHECK(out[0] == 255 && out[1] == 0 && out[3] == 64);
    CHECK(out[4] == 0 && out[6] == 255 && out[7] == 0);  // a cor do vizinho da direita (o primeiro olhado), alfa 0
    CHECK(out[8] == 0 && out[10] == 255 && out[11] == 128);
    // Sem pré-multiplicar a cor fica.
    ui_vec_to_gs_rgba(bgra, 12, 3, 1, false, out);
    CHECK(out[0] == 128);
    // O resumo: a mesma lista dá o mesmo; um texto, uma cor, o tamanho da janela ou a tela virtual mudam.
    ui::DrawList l;
    l.fill(ui::Rect{0, 0, 10, 10}, ui::Color{1, 0, 0, 1});
    l.text(ui::Rect{0, 0, 100, 20}, L"OPTIONS", ui::TextStyle{}, ui::Color{1, 1, 1, 1});
    const ui::Canvas cv = ui::make_canvas(1920, 1080);
    const uint64_t h0 = ui_vec_list_hash(l, cv, 1920, 1080);
    CHECK(h0 == ui_vec_list_hash(l, cv, 1920, 1080));
    ui::DrawList l2 = l;
    l2.cmds[1].text = L"OPTIONZ";
    CHECK(ui_vec_list_hash(l2, cv, 1920, 1080) != h0);
    l2 = l;
    l2.cmds[0].c.a = 0.5f;
    CHECK(ui_vec_list_hash(l2, cv, 1920, 1080) != h0);
    CHECK(ui_vec_list_hash(l, cv, 1920, 1081) != h0);
    CHECK(ui_vec_list_hash(l, ui::make_canvas(2560, 1440), 1920, 1080) != h0);
    // Por comando: só o que mudou muda (o começo comum das duas listas é o que o desenho reaproveita).
    l.gradient(ui::Rect{0, 0, 50, 50}, ui::Color{1, 0, 0, 1}, ui::Color{0, 0, 1, 0}, true);
    l2 = l;
    l2.cmds[1].text = L"OPTIONZ";
    const std::vector<uint64_t> a = ui_vec_cmd_hashes(l), b = ui_vec_cmd_hashes(l2);
    CHECK(a.size() == 3 && b.size() == 3);
    CHECK(a[0] == b[0] && a[1] != b[1] && a[2] == b[2]);
    CHECK(a == ui_vec_cmd_hashes(l));
    CHECK(ui_vec_canvas_hash(cv, 1920, 1080) == ui_vec_canvas_hash(ui::make_canvas(1920, 1080), 1920, 1080));
    CHECK(ui_vec_canvas_hash(cv, 1920, 1080) != ui_vec_canvas_hash(cv, 1920, 1200));
}
