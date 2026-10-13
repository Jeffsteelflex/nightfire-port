// O leitor de SVG do port (core/svg.h; docs/port.md, "macOS: o desenho vetorial"): os caminhos (os comandos, os arcos, as
// quadráticas), as transformações, as formas básicas, os degradês e os recortes, e todos os SVG do repositório
// (hd_ui/svg: os glifos dos botões e as substitutas da camada HD) lidos sem erro, sem nada ignorado e com a caixa do
// desenho batendo com o viewBox.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "core/svg.h"
#include "platform/crt_compat.h"
#include "test_framework.h"

using namespace nf::svg;

namespace {

std::string read_text(const std::filesystem::path& p) {
    FILE* f = nf::fopen_narrow(p.string().c_str(), "rb");
    if (f == nullptr) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// O ponto da curva cúbica de um passo em t.
Point cubic_at(const Point& p0, const PathOp& op, double t) {
    const double u = 1 - t;
    return Point{u * u * u * p0.x + 3 * u * u * t * op.p[0].x + 3 * u * t * t * op.p[1].x + t * t * t * op.p[2].x,
                 u * u * u * p0.y + 3 * u * u * t * op.p[0].y + 3 * u * t * t * op.p[1].y + t * t * t * op.p[2].y};
}

// O maior erro de raio de um caminho feito de arcos de um círculo (centro cx, cy, raio r), amostrando as cúbicas.
double circle_error(const Path& p, double cx, double cy, double r) {
    double worst = 0;
    Point cur{};
    for (const PathOp& op : p) {
        if (op.kind == PathOp::Move || op.kind == PathOp::Line) {
            cur = op.p[0];
            worst = std::max(worst, std::fabs(std::hypot(cur.x - cx, cur.y - cy) - r));
        } else if (op.kind == PathOp::Cubic) {
            for (int k = 1; k <= 16; ++k) {
                const Point q = cubic_at(cur, op, k / 16.0);
                worst = std::max(worst, std::fabs(std::hypot(q.x - cx, q.y - cy) - r));
            }
            cur = op.p[2];
        }
    }
    return worst;
}

}  // namespace

TEST(svg_path_commands_absolute_and_relative) {
    Path p;
    std::string err;
    CHECK(parse_path("M10,20 L30,20 H40 V50 l-10,0 h-5 v-5 Z m1 2 3 4", p, &err));
    CHECK(err.empty());
    // M L H V l h v Z m (e o par seguinte do m vira l).
    CHECK(p.size() == 10);
    CHECK(p[0].kind == PathOp::Move && p[0].p[0].x == 10 && p[0].p[0].y == 20);
    CHECK(p[2].kind == PathOp::Line && p[2].p[0].x == 40 && p[2].p[0].y == 20);
    CHECK(p[3].p[0].x == 40 && p[3].p[0].y == 50);
    CHECK(p[4].p[0].x == 30 && p[4].p[0].y == 50);
    CHECK(p[6].p[0].x == 25 && p[6].p[0].y == 45);
    CHECK(p[7].kind == PathOp::Close);
    // Depois do Z o ponto atual é o começo do subcaminho (10, 20): o m é relativo a ele.
    CHECK(p[8].kind == PathOp::Move && p[8].p[0].x == 11 && p[8].p[0].y == 22);
    CHECK(p[9].kind == PathOp::Line && p[9].p[0].x == 14 && p[9].p[0].y == 26);
    // Números colados ("-" e "." separam) e expoentes.
    Path q;
    CHECK(parse_path("M1-2.5.5.25L1e1,2E0", q, &err));
    CHECK(q.size() == 3 && q[0].p[0].x == 1 && q[0].p[0].y == -2.5 && q[1].p[0].x == 0.5 && q[1].p[0].y == 0.25);
    CHECK(q[2].p[0].x == 10 && q[2].p[0].y == 2);
    // Erros.
    Path bad;
    CHECK(!parse_path("10 20", bad, &err) && !err.empty());
    CHECK(!parse_path("M10", bad, &err));
    CHECK(!parse_path("M0 0 X1 1", bad, &err));
}

TEST(svg_path_curves_quadratic_smooth_and_arcs) {
    Path p;
    std::string err;
    // Q: a quadrática (0,0) (10,10) (20,0) vira a cúbica com controles a 2/3: (6.667, 6.667) e (13.333, 6.667).
    CHECK(parse_path("M0,0 Q10,10 20,0 T40,0", p, &err));
    CHECK(p.size() == 3 && p[1].kind == PathOp::Cubic);
    CHECK_NEAR(p[1].p[0].x, 20.0 / 3.0, 1e-9);
    CHECK_NEAR(p[1].p[0].y, 20.0 / 3.0, 1e-9);
    CHECK_NEAR(p[1].p[1].x, 40.0 / 3.0, 1e-9);
    // T reflete o controle (10,10) em (20,0): (30,-10); o meio da curva fica em y = -5.
    CHECK_NEAR(cubic_at(Point{20, 0}, p[2], 0.5).y, -5.0, 1e-9);
    // S reflete o segundo controle da C anterior.
    Path s;
    CHECK(parse_path("M0,0 C0,10 10,10 10,0 S20,-10 20,0", s, &err));
    CHECK(s.size() == 3 && s[2].p[0].x == 10 && s[2].p[0].y == -10);
    // A: meia volta de (0,0) a (20,0), raio 10; sweep 1 é o sentido dos ângulos crescentes (horário com y para baixo):
    // passa por cima (y = -10).
    Path a;
    CHECK(parse_path("M0,0 A10,10 0 0 1 20,0", a, &err));
    CHECK(a.back().kind == PathOp::Cubic && a.back().p[2].x == 20 && a.back().p[2].y == 0);
    const Box b = path_bounds(a, Matrix{});
    CHECK_NEAR(b.y0, -10.0, 1e-3);
    CHECK_NEAR(b.y1, 0.0, 1e-9);
    CHECK(circle_error(a, 10, 0, 10) < 0.01);
    // sweep 0: por baixo. Os sinalizadores podem vir colados ("0 01").
    Path a2;
    CHECK(parse_path("M0,0 A10,10 0 0020,0", a2, &err));
    CHECK_NEAR(path_bounds(a2, Matrix{}).y1, 10.0, 1e-3);
    // Raio pequeno demais cresce até caber (a especificação): o mesmo meio círculo.
    Path a3;
    CHECK(parse_path("M0,0 A1,1 0 0 1 20,0", a3, &err));
    CHECK_NEAR(path_bounds(a3, Matrix{}).y0, -10.0, 1e-3);
    // large-arc com o mesmo ponto final: a volta grande de um círculo de raio 10 por (0,0) e (10,10).
    Path a4;
    CHECK(parse_path("M0,0 A10,10 0 1 1 10,10", a4, &err));
    const Box b4 = path_bounds(a4, Matrix{});
    CHECK_NEAR(b4.x1 - b4.x0, 20.0, 1e-2);
    CHECK_NEAR(b4.y1 - b4.y0, 20.0, 1e-2);
}

TEST(svg_transforms_and_shapes) {
    Matrix m;
    CHECK(parse_transform("translate(25,0) skewX(-14)", m));
    // O skew age primeiro: (0, 10) -> (10 tan(-14°), 10) -> + (25, 0).
    CHECK_NEAR(m.apply_x(0, 10), 25.0 + 10.0 * std::tan(-14.0 * 3.14159265358979 / 180.0), 1e-9);
    CHECK_NEAR(m.apply_y(0, 10), 10.0, 1e-9);
    CHECK(parse_transform("rotate(90 10 10)", m));
    CHECK_NEAR(m.apply_x(10, 0), 20.0, 1e-9);
    CHECK_NEAR(m.apply_y(10, 0), 10.0, 1e-9);
    CHECK(parse_transform("matrix(2 0 0 3 5 7) scale(2)", m));
    CHECK_NEAR(m.apply_x(1, 1), 9.0, 1e-9);
    CHECK_NEAR(m.apply_y(1, 1), 13.0, 1e-9);
    const Matrix inv = m.inverse();
    CHECK_NEAR(inv.apply_x(9, 13), 1.0, 1e-9);
    CHECK(!parse_transform("rotate(1 2)", m));
    CHECK(!parse_transform("spin(3)", m));
    // As formas básicas: o círculo e o retângulo arredondado com o erro do arco pequeno.
    Path c;
    append_ellipse(c, 5, 6, 4, 4);
    CHECK(c.front().kind == PathOp::Move && c.back().kind == PathOp::Close);
    CHECK(circle_error(c, 5, 6, 4) < 4 * 3e-4);
    const Box cb = path_bounds(c, Matrix{});
    CHECK_NEAR(cb.x0, 1.0, 1e-6);
    CHECK_NEAR(cb.y1, 10.0, 1e-6);
    Path r;
    append_rect(r, 0, 0, 10, 4, 3, 3);  // os raios ficam na metade de cada lado
    const Box rb = path_bounds(r, Matrix{});
    CHECK(rb.x0 == 0 && rb.y0 == 0 && rb.x1 == 10 && rb.y1 == 4);
}

TEST(svg_document_paint_gradients_and_clips) {
    const char* xml = R"svg(<?xml version="1.0"?>
<!-- um comentário -->
<svg xmlns="http://www.w3.org/2000/svg" viewBox="10 20 40 30" width="100%" height="100%" preserveAspectRatio="none">
<defs>
<linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f00"/><stop offset="50%" style="stop-color: #00ff00; stop-opacity: 0.5"/></linearGradient>
<clipPath id="c"><rect x="10" y="20" width="20" height="30"/></clipPath>
</defs>
<g fill="url(#g)" opacity="0.5" clip-path="url(#c)" transform="translate(1,2)">
<rect x="10" y="20" width="40" height="30" stroke="blue" stroke-width="2" fill-opacity="0.5"/>
</g>
<circle cx="30" cy="35" r="5" fill="none" stroke="#123456" stroke-linecap="round" stroke-linejoin="bevel"/>
<line x1="10" y1="20" x2="50" y2="50" stroke="white"/>
</svg>)svg";
    Document d;
    std::string err;
    CHECK(parse(xml, d, &err));
    CHECK(err.empty() && d.ignored.empty());
    CHECK(d.vb_x == 10 && d.vb_y == 20 && d.vb_w == 40 && d.vb_h == 30 && d.preserve_none);
    CHECK(d.gradients.size() == 1 && !d.gradients[0].user_space && d.gradients[0].stops.size() == 2);
    CHECK_NEAR(d.gradients[0].stops[1].offset, 0.5, 1e-9);
    CHECK_NEAR(d.gradients[0].stops[1].g, 1.0, 1e-6);
    CHECK_NEAR(d.gradients[0].stops[1].a, 0.5, 1e-6);
    CHECK(d.shapes.size() == 3);
    const Shape& r = d.shapes[0];
    CHECK(r.fill.kind == Paint::Gradient && r.fill.gradient == 0 && r.stroke.kind == Paint::Color);
    CHECK_NEAR(r.fill_opacity, 0.25, 1e-6);   // fill-opacity 0.5 x a opacity 0.5 do grupo
    CHECK_NEAR(r.stroke_opacity, 0.5, 1e-6);
    CHECK(r.clips.size() == 1 && r.clips[0].clip == 0);
    CHECK_NEAR(r.m.e, 1.0, 1e-9);
    // A caixa: a do retângulo com o traço, cortada pelo recorte (que segue a translação do grupo).
    const Box rb = shape_bounds(d, r);
    CHECK_NEAR(rb.x0, 11.0, 1e-9);
    CHECK_NEAR(rb.x1, 31.0, 1e-9);
    CHECK(d.shapes[1].fill.kind == Paint::None && d.shapes[1].cap == Cap::Round && d.shapes[1].join == Join::Bevel);
    CHECK(d.shapes[2].fill.kind == Paint::None && d.shapes[2].stroke.kind == Paint::Color);
    // O degradê em objectBoundingBox vai do quadrado unitário à caixa da geometria.
    const Matrix gm = gradient_matrix(d.gradients[0], path_bounds(r.path, Matrix{}));
    CHECK_NEAR(gm.apply_x(1, 1), 50.0, 1e-9);
    CHECK_NEAR(gm.apply_y(1, 1), 50.0, 1e-9);
    // O viewport: preserveAspectRatio none estica cada eixo; o padrão (xMidYMid meet) centra.
    const Matrix vp = viewport(d, 80, 30);
    CHECK_NEAR(vp.apply_x(10, 20), 0.0, 1e-9);
    CHECK_NEAR(vp.apply_x(50, 50), 80.0, 1e-9);
    CHECK_NEAR(vp.apply_y(50, 50), 30.0, 1e-9);
    d.preserve_none = false;
    const Matrix vm = viewport(d, 80, 30);
    CHECK_NEAR(vm.apply_x(10, 20), 20.0, 1e-9);
    CHECK_NEAR(vm.apply_x(50, 50), 60.0, 1e-9);
    // O que o leitor não conhece vai para ignored; o XML quebrado é erro.
    Document u;
    CHECK(parse(R"svg(<svg viewBox="0 0 1 1"><filter id="f"/><rect width="1" height="1" mask="url(#m)" fill="url(#nada)"/></svg>)svg", u, &err));
    CHECK(u.ignored.size() == 3);
    CHECK(!parse("<svg viewBox=\"0 0 1 1\"><rect></svg>", u, &err) && !err.empty());
    CHECK(!parse("<g/>", u, &err));
    CHECK(!parse("<svg/>", u, &err));
}

// Todos os SVG do repositório: os glifos dos botões (DrawCmd::Glyph dos menus Modern) e as substitutas da camada HD
// (hd_ui/map.json). Nada ignorado, alguma forma, e a caixa do desenho batendo com o viewBox: o desenho cobre ao menos
// 40% do viewBox em cada eixo e fica dentro dele com até 15% de folga de cada lado (as lâminas da vida passam um pouco
// das bordas, como no desenho do jogo; a caixa conta o traço com folga), ou o viewBox é uma janela dentro do desenho (as
// pontas e o meio da faixa das mensagens são pedaços de uma faixa só, como na textura do jogo).
TEST(svg_repository_files_parse_completely) {
    const std::filesystem::path dir = std::filesystem::path(NF_SOURCE_DIR) / "hd_ui" / "svg";
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() == ".svg") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    CHECK(files.size() >= 60);
    int shapes = 0, gradients = 0, clips = 0;
    for (const auto& f : files) {
        const std::string xml = read_text(f);
        Document d;
        std::string err;
        const bool ok = parse(xml, d, &err);
        if (!ok || !d.ignored.empty() || d.shapes.empty()) {
            std::printf("  %s: %s %s\n", f.filename().string().c_str(), err.c_str(),
                        d.ignored.empty() ? "" : d.ignored.front().c_str());
        }
        CHECK(ok && d.ignored.empty() && !d.shapes.empty());
        const Box b = document_bounds(d);
        CHECK(!b.empty);
        const double mx = d.vb_w * 0.15, my = d.vb_h * 0.15;
        const bool inside = b.x0 >= d.vb_x - mx && b.y0 >= d.vb_y - my && b.x1 <= d.vb_x + d.vb_w + mx &&
                            b.y1 <= d.vb_y + d.vb_h + my;
        const bool window = b.x0 <= d.vb_x + 0.5 && b.x1 >= d.vb_x + d.vb_w - 0.5 && b.y0 <= d.vb_y + 0.5 &&
                            b.y1 >= d.vb_y + d.vb_h - 0.5;
        const double cover_x = std::min(b.x1, d.vb_x + d.vb_w) - std::max(b.x0, d.vb_x);
        const double cover_y = std::min(b.y1, d.vb_y + d.vb_h) - std::max(b.y0, d.vb_y);
        const bool covers = cover_x >= d.vb_w * 0.4 && cover_y >= d.vb_h * 0.4;
        if (!(inside || window) || !covers) {
            std::printf("  %s: caixa %.2f %.2f %.2f %.2f, viewBox %.2f %.2f %.2f %.2f\n", f.filename().string().c_str(),
                        b.x0, b.y0, b.x1, b.y1, d.vb_x, d.vb_y, d.vb_w, d.vb_h);
        }
        CHECK((inside || window) && covers);
        shapes += int(d.shapes.size());
        gradients += int(d.gradients.size());
        clips += int(d.clips.size());
    }
    // Os números do conjunto de hoje (um SVG novo muda; o que importa é que todos passam acima).
    CHECK(shapes > 300 && gradients > 70 && clips >= 4);
}
