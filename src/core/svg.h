#pragma once
// Leitor de SVG do port (só C++ padrão, nada do sistema; docs/port.md, "macOS: o desenho vetorial"). O Direct2D lê SVG
// (ID2D1SvgDocument) e desenha os glifos dos menus Modern e as substitutas da camada HD no Windows; fora do Windows não
// há leitor de SVG no sistema, então o port lê os arquivos ele mesmo e o backend do desenho (render/ui_vector_cg.mm, no
// macOS) pinta o resultado: uma lista de formas, cada uma um caminho (só move, linha, cúbica e fecha) com a matriz que o
// leva ao espaço do documento, o preenchimento e o traço (cor ou degradê), a opacidade e os recortes.
//
// O subconjunto é o que os SVG do port usam (hd_ui/svg, gerados por tools/ui_hd_assets.py), com folga:
//   - elementos: svg, g, defs, path, rect (cantos arredondados), circle, ellipse, line, polyline, polygon,
//     linearGradient, radialGradient, stop, clipPath (title, desc e metadata são pulados);
//   - caminhos: M L H V C S Q T A Z, absolutos e relativos (as quadráticas e os arcos viram cúbicas);
//   - transform: matrix, translate, scale, rotate (com o centro), skewX, skewY;
//   - pintura: none, #rgb, #rrggbb, os nomes de cor básicos, url(#id); fill-opacity, stroke-opacity, opacity (multiplicada
//     na forma: os SVG do port não sobrepõem formas de um grupo com opacidade), fill-rule, clip-rule, stroke-width,
//     stroke-linecap, stroke-linejoin, stroke-miterlimit; os atributos e o atributo style ("nome: valor; ...");
//   - degradês: gradientUnits (userSpaceOnUse e objectBoundingBox), gradientTransform, os stops com offset (número ou %),
//     stop-color e stop-opacity; spreadMethod pad (o único);
//   - viewBox e preserveAspectRatio (none estica; os outros alinham com meet ou slice).
// O que não conhece (um elemento, um atributo, um valor) vai para Document::ignored: o teste do nf_tests exige que os SVG
// do repositório não tenham nada ali.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nf::svg {

// x' = a x + c y + e; y' = b x + d y + f (a ordem do SVG e do Direct2D).
struct Matrix {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    // Primeiro this, depois m (o ponto passa por this e o resultado por m).
    Matrix then(const Matrix& m) const;
    double apply_x(double x, double y) const { return a * x + c * y + e; }
    double apply_y(double x, double y) const { return b * x + d * y + f; }
    Matrix inverse() const;
    static Matrix translate(double x, double y) { return Matrix{1, 0, 0, 1, x, y}; }
    static Matrix scale(double sx, double sy) { return Matrix{sx, 0, 0, sy, 0, 0}; }
};

struct Point {
    double x = 0, y = 0;
};

// Um passo de um caminho: Move (p[0]), Line (p[0]), Cubic (p[0], p[1] os controles, p[2] o fim), Close.
struct PathOp {
    enum Kind : uint8_t { Move, Line, Cubic, Close };
    Kind kind = Move;
    Point p[3];
};
using Path = std::vector<PathOp>;

struct Box {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = true;
    void add(double x, double y);
    void add(const Box& b);
    Box intersect(const Box& b) const;
    double w() const { return empty ? 0.0 : x1 - x0; }
    double h() const { return empty ? 0.0 : y1 - y0; }
};

// Um arco de elipse (centro, raios, rotação do eixo em radianos, de t0 a t1 radianos; t1 < t0 vai no outro sentido) em
// cúbicas, até 90 graus cada (o erro fica abaixo de 0,03% do raio). Começa com uma linha até o primeiro ponto se o
// caminho já tiver um ponto, ou um Move se não tiver.
void append_arc(Path& path, double cx, double cy, double rx, double ry, double rotation, double t0, double t1);
// O arco do comando A do SVG (do ponto atual ao (x, y)), com as correções da especificação (raios pequenos crescem,
// raio 0 é uma linha).
void append_svg_arc(Path& path, double x0, double y0, double rx, double ry, double rotation_deg, bool large, bool sweep,
                    double x, double y);
// As formas básicas como caminho (o sentido do SVG: do ponto mais à direita, no sentido horário com y para baixo).
void append_ellipse(Path& path, double cx, double cy, double rx, double ry);
void append_rect(Path& path, double x, double y, double w, double h, double rx, double ry);
// A caixa de um caminho depois da matriz (as cúbicas pelos extremos de verdade, não pelos controles).
Box path_bounds(const Path& path, const Matrix& m);
// O "d" de um caminho (false e a mensagem se não for válido; o caminho até o erro fica).
bool parse_path(std::string_view d, Path& out, std::string* error);
// Um transform (false se não for válido).
bool parse_transform(std::string_view s, Matrix& out);

struct Stop {
    double offset = 0;
    float r = 0, g = 0, b = 0, a = 1;  // 0 a 1, sem pré-multiplicar (a = stop-opacity)
};

struct Gradient {
    std::string id;
    bool radial = false;
    bool user_space = false;  // gradientUnits="userSpaceOnUse" (o padrão é objectBoundingBox)
    double x1 = 0, y1 = 0, x2 = 1, y2 = 0;        // linear
    double cx = 0.5, cy = 0.5, r = 0.5, fx = 0.5, fy = 0.5;  // radial (fx, fy: o foco)
    Matrix transform;  // gradientTransform
    std::vector<Stop> stops;
};

struct Paint {
    enum Kind : uint8_t { None, Color, Gradient };
    Kind kind = None;
    float r = 0, g = 0, b = 0;
    int gradient = -1;  // Document::gradients
};

enum class Cap : uint8_t { Butt, Round, Square };
enum class Join : uint8_t { Miter, Round, Bevel };

// Um clipPath: os caminhos dos filhos, já no espaço do clipPath (com a matriz de cada filho).
struct ClipPath {
    std::string id;
    std::vector<Path> paths;
    std::vector<uint8_t> evenodd;  // clip-rule de cada caminho
};

// Um recorte aplicado a uma forma: o clipPath e a matriz do elemento que o pediu (do espaço dele ao do documento).
struct ClipRef {
    int clip = -1;
    Matrix m;
};

struct Shape {
    std::string element;     // "path", "rect"...
    Path path;               // no espaço do elemento
    Matrix m;                // do espaço do elemento ao do documento (o do viewBox)
    Paint fill, stroke;
    float fill_opacity = 1, stroke_opacity = 1;  // já com a opacity do elemento e dos grupos
    double stroke_width = 1;
    Cap cap = Cap::Butt;
    Join join = Join::Miter;
    double miter = 4;
    bool evenodd = false;
    std::vector<ClipRef> clips;  // todos valem (a interseção), na ordem de fora para dentro
};

struct Document {
    double vb_x = 0, vb_y = 0, vb_w = 0, vb_h = 0;  // o viewBox (sem ele: 0, 0 e o width/height)
    double width = 0, height = 0;                   // os atributos em números (0: em porcentagem ou ausentes)
    bool preserve_none = false;                     // preserveAspectRatio="none"
    int align_x = 1, align_y = 1;                   // 0 min, 1 mid, 2 max
    bool slice = false;
    std::vector<Shape> shapes;
    std::vector<Gradient> gradients;
    std::vector<ClipPath> clips;
    std::vector<std::string> ignored;  // o que o leitor não conhece ("elemento foo", "atributo bar em rect"...)
    int find_gradient(std::string_view id) const;
};

// Lê um SVG (o texto do arquivo, UTF-8). false com a mensagem se o XML ou o svg de fora não forem válidos; o que só não
// for conhecido vai para Document::ignored.
bool parse(std::string_view xml, Document& out, std::string* error);

// Do espaço do documento (o viewBox) a uma área de w x h pixels com o canto em (0, 0), pelo preserveAspectRatio.
Matrix viewport(const Document& doc, double w, double h);
// A caixa de uma forma no espaço do documento: a geometria, mais meia largura de traço se tiver traço (vezes o tamanho da
// matriz), cortada pelos recortes. Vazia se a forma não pinta nada.
Box shape_bounds(const Document& doc, const Shape& s);
// A união das caixas das formas.
Box document_bounds(const Document& doc);
// A matriz do espaço do degradê ao do elemento: primeiro o gradientTransform, depois (em objectBoundingBox) o quadrado
// unitário vai à caixa da geometria (bbox, no espaço do elemento).
Matrix gradient_matrix(const Gradient& g, const Box& bbox);

}  // namespace nf::svg
