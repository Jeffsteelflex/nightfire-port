// Leitor de SVG do port (neutro). Ver svg.h.

#include "core/svg.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace nf::svg {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- XML

struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<Node> children;
    const std::string* attr(const char* key) const {
        for (const auto& [k, v] : attrs) {
            if (k == key) return &v;
        }
        return nullptr;
    }
};

class Xml {
public:
    explicit Xml(std::string_view s) : s_(s) {}
    bool parse(Node& root, std::string* error) {
        skip_misc();
        if (!element(root, 0)) {
            if (error != nullptr) *error = error_.empty() ? "XML inválido" : error_;
            return false;
        }
        return true;
    }

private:
    std::string_view s_;
    size_t i_ = 0;
    std::string error_;

    bool at(const char* t) const { return s_.compare(i_, std::strlen(t), t) == 0; }
    void ws() {
        while (i_ < s_.size() && std::isspace(uint8_t(s_[i_]))) ++i_;
    }
    bool skip_to(const char* end) {
        const size_t p = s_.find(end, i_);
        if (p == std::string_view::npos) return false;
        i_ = p + std::strlen(end);
        return true;
    }
    // O prólogo, os comentários, o DOCTYPE, as instruções e o texto entre as tags.
    void skip_misc() {
        for (;;) {
            while (i_ < s_.size() && s_[i_] != '<') ++i_;
            if (at("<?")) {
                if (!skip_to("?>")) i_ = s_.size();
            } else if (at("<!--")) {
                if (!skip_to("-->")) i_ = s_.size();
            } else if (at("<![CDATA[")) {
                if (!skip_to("]]>")) i_ = s_.size();
            } else if (at("<!")) {
                if (!skip_to(">")) i_ = s_.size();
            } else {
                return;
            }
        }
    }
    static bool name_char(char c) { return std::isalnum(uint8_t(c)) || c == '_' || c == '-' || c == ':' || c == '.'; }
    std::string name() {
        const size_t b = i_;
        while (i_ < s_.size() && name_char(s_[i_])) ++i_;
        return std::string(s_.substr(b, i_ - b));
    }
    static std::string unescape(std::string_view v) {
        std::string out;
        out.reserve(v.size());
        for (size_t k = 0; k < v.size(); ++k) {
            if (v[k] != '&') {
                out.push_back(v[k]);
                continue;
            }
            const size_t semi = v.find(';', k);
            if (semi == std::string_view::npos) {
                out.push_back('&');
                continue;
            }
            const std::string_view e = v.substr(k + 1, semi - k - 1);
            uint32_t cp = 0;
            if (e == "amp") cp = '&';
            else if (e == "lt") cp = '<';
            else if (e == "gt") cp = '>';
            else if (e == "quot") cp = '"';
            else if (e == "apos") cp = '\'';
            else if (e.size() > 1 && e[0] == '#') cp = uint32_t(std::strtoul(std::string(e.substr(e[1] == 'x' ? 2 : 1)).c_str(), nullptr, e[1] == 'x' ? 16 : 10));
            if (cp == 0) {
                out.push_back('&');
                continue;
            }
            // UTF-8.
            if (cp < 0x80) {
                out.push_back(char(cp));
            } else if (cp < 0x800) {
                out.push_back(char(0xc0 | (cp >> 6)));
                out.push_back(char(0x80 | (cp & 0x3f)));
            } else {
                out.push_back(char(0xe0 | (cp >> 12)));
                out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
                out.push_back(char(0x80 | (cp & 0x3f)));
            }
            k = semi;
        }
        return out;
    }
    bool element(Node& n, int depth) {
        if (depth > 64) {
            error_ = "XML fundo demais";
            return false;
        }
        if (i_ >= s_.size() || s_[i_] != '<') {
            error_ = "esperava um elemento";
            return false;
        }
        ++i_;
        n.name = name();
        if (n.name.empty()) {
            error_ = "elemento sem nome";
            return false;
        }
        for (;;) {
            ws();
            if (i_ >= s_.size()) {
                error_ = "fim do arquivo dentro de <" + n.name + ">";
                return false;
            }
            if (at("/>")) {
                i_ += 2;
                return true;
            }
            if (s_[i_] == '>') {
                ++i_;
                break;
            }
            std::string key = name();
            if (key.empty()) {
                error_ = "atributo inválido em <" + n.name + ">";
                return false;
            }
            ws();
            if (i_ >= s_.size() || s_[i_] != '=') {
                error_ = "atributo sem valor em <" + n.name + ">";
                return false;
            }
            ++i_;
            ws();
            if (i_ >= s_.size() || (s_[i_] != '"' && s_[i_] != '\'')) {
                error_ = "valor sem aspas em <" + n.name + ">";
                return false;
            }
            const char q = s_[i_++];
            const size_t end = s_.find(q, i_);
            if (end == std::string_view::npos) {
                error_ = "aspas sem fim em <" + n.name + ">";
                return false;
            }
            n.attrs.emplace_back(std::move(key), unescape(s_.substr(i_, end - i_)));
            i_ = end + 1;
        }
        for (;;) {
            skip_misc();
            if (i_ >= s_.size()) {
                error_ = "fim do arquivo dentro de <" + n.name + ">";
                return false;
            }
            if (at("</")) {
                i_ += 2;
                const std::string close = name();
                ws();
                if (close != n.name || i_ >= s_.size() || s_[i_] != '>') {
                    error_ = "</" + close + "> fecha <" + n.name + ">";
                    return false;
                }
                ++i_;
                return true;
            }
            n.children.emplace_back();
            if (!element(n.children.back(), depth + 1)) return false;
        }
    }
};

// ---------------------------------------------------------------- números e listas

void skip_sep(std::string_view s, size_t& i) {
    while (i < s.size() && (std::isspace(uint8_t(s[i])) || s[i] == ',')) ++i;
}

bool number(std::string_view s, size_t& i, double& out) {
    skip_sep(s, i);
    const size_t b = i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    bool digits = false;
    while (i < s.size() && std::isdigit(uint8_t(s[i]))) {
        ++i;
        digits = true;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && std::isdigit(uint8_t(s[i]))) {
            ++i;
            digits = true;
        }
    }
    if (!digits) {
        i = b;
        return false;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
        if (j < s.size() && std::isdigit(uint8_t(s[j]))) {
            while (j < s.size() && std::isdigit(uint8_t(s[j]))) ++j;
            i = j;
        }
    }
    out = std::strtod(std::string(s.substr(b, i - b)).c_str(), nullptr);
    return true;
}

// Um sinalizador do arco: "0" ou "1", que pode vir colado ao seguinte ("011").
bool flag(std::string_view s, size_t& i, bool& out) {
    skip_sep(s, i);
    if (i < s.size() && (s[i] == '0' || s[i] == '1')) {
        out = s[i] == '1';
        ++i;
        return true;
    }
    return false;
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(uint8_t(s[b]))) ++b;
    while (e > b && std::isspace(uint8_t(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

// Um comprimento: número com "px" ou nada; com "%", a fração de `percent_of` (e `is_percent`).
bool length(std::string_view s, double percent_of, double& out, bool* is_percent = nullptr) {
    const std::string t = trim(s);
    size_t i = 0;
    double v = 0;
    if (!number(t, i, v)) return false;
    const std::string unit = t.substr(i);
    if (is_percent != nullptr) *is_percent = unit == "%";
    if (unit == "%") {
        out = v / 100.0 * percent_of;
        return true;
    }
    if (unit.empty() || unit == "px") {
        out = v;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- cores

bool hexval(char c, int& v) {
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return false;
    return true;
}

bool color(std::string_view sv, float& r, float& g, float& b) {
    const std::string s = trim(sv);
    if (!s.empty() && s[0] == '#') {
        int d[6];
        if (s.size() == 4) {
            for (int k = 0; k < 3; ++k) {
                if (!hexval(s[size_t(k) + 1], d[k])) return false;
            }
            r = float(d[0] * 17) / 255.0f;
            g = float(d[1] * 17) / 255.0f;
            b = float(d[2] * 17) / 255.0f;
            return true;
        }
        if (s.size() == 7) {
            for (int k = 0; k < 6; ++k) {
                if (!hexval(s[size_t(k) + 1], d[k])) return false;
            }
            r = float(d[0] * 16 + d[1]) / 255.0f;
            g = float(d[2] * 16 + d[3]) / 255.0f;
            b = float(d[4] * 16 + d[5]) / 255.0f;
            return true;
        }
        return false;
    }
    if (s.rfind("rgb(", 0) == 0 && s.back() == ')') {
        const std::string_view in(s.data() + 4, s.size() - 5);
        size_t i = 0;
        double v[3];
        for (double& x : v) {
            if (!number(in, i, x)) return false;
            if (i < in.size() && in[i] == '%') {
                x = x * 255.0 / 100.0;
                ++i;
            }
        }
        r = float(std::clamp(v[0], 0.0, 255.0) / 255.0);
        g = float(std::clamp(v[1], 0.0, 255.0) / 255.0);
        b = float(std::clamp(v[2], 0.0, 255.0) / 255.0);
        return true;
    }
    static const struct {
        const char* name;
        uint32_t rgb;
    } kNames[] = {{"black", 0x000000}, {"white", 0xffffff}, {"red", 0xff0000},   {"green", 0x008000},
                  {"blue", 0x0000ff},  {"yellow", 0xffff00}, {"gray", 0x808080}, {"grey", 0x808080},
                  {"silver", 0xc0c0c0}, {"orange", 0xffa500}, {"currentColor", 0x000000}};
    for (const auto& n : kNames) {
        if (s == n.name) {
            r = float((n.rgb >> 16) & 0xff) / 255.0f;
            g = float((n.rgb >> 8) & 0xff) / 255.0f;
            b = float(n.rgb & 0xff) / 255.0f;
            return true;
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------- matrizes e caixas

Matrix Matrix::then(const Matrix& m) const {
    return Matrix{m.a * a + m.c * b, m.b * a + m.d * b, m.a * c + m.c * d, m.b * c + m.d * d, m.a * e + m.c * f + m.e,
                  m.b * e + m.d * f + m.f};
}

Matrix Matrix::inverse() const {
    const double det = a * d - b * c;
    if (std::fabs(det) < 1e-300) return Matrix{};
    const double ia = d / det, ib = -b / det, ic = -c / det, id = a / det;
    return Matrix{ia, ib, ic, id, -(ia * e + ic * f), -(ib * e + id * f)};
}

void Box::add(double x, double y) {
    if (empty) {
        x0 = x1 = x;
        y0 = y1 = y;
        empty = false;
        return;
    }
    x0 = std::min(x0, x);
    y0 = std::min(y0, y);
    x1 = std::max(x1, x);
    y1 = std::max(y1, y);
}

void Box::add(const Box& b) {
    if (b.empty) return;
    add(b.x0, b.y0);
    add(b.x1, b.y1);
}

Box Box::intersect(const Box& b) const {
    Box r;
    if (empty || b.empty) return r;
    r.x0 = std::max(x0, b.x0);
    r.y0 = std::max(y0, b.y0);
    r.x1 = std::min(x1, b.x1);
    r.y1 = std::min(y1, b.y1);
    r.empty = !(r.x1 > r.x0 && r.y1 > r.y0);
    return r;
}

// ---------------------------------------------------------------- caminhos

namespace {

bool current_point(const Path& p, Point& out) {
    // O ponto atual: o fim do último passo; depois de um Close, o começo do subcaminho.
    for (size_t k = p.size(); k-- > 0;) {
        const PathOp& op = p[k];
        if (op.kind == PathOp::Close) {
            for (size_t j = k; j-- > 0;) {
                if (p[j].kind == PathOp::Move) {
                    out = p[j].p[0];
                    return true;
                }
            }
            return false;
        }
        out = op.kind == PathOp::Cubic ? op.p[2] : op.p[0];
        return true;
    }
    return false;
}

void push(Path& p, PathOp::Kind k, Point a, Point b = {}, Point c = {}) {
    PathOp op;
    op.kind = k;
    op.p[0] = a;
    op.p[1] = b;
    op.p[2] = c;
    p.push_back(op);
}

}  // namespace

void append_arc(Path& path, double cx, double cy, double rx, double ry, double rotation, double t0, double t1) {
    const double cr = std::cos(rotation), sr = std::sin(rotation);
    const auto at = [&](double t) {
        const double ex = rx * std::cos(t), ey = ry * std::sin(t);
        return Point{cx + cr * ex - sr * ey, cy + sr * ex + cr * ey};
    };
    const auto deriv = [&](double t) {
        const double dx = -rx * std::sin(t), dy = ry * std::cos(t);
        return Point{cr * dx - sr * dy, sr * dx + cr * dy};
    };
    const Point start = at(t0);
    Point cur;
    if (!current_point(path, cur)) {
        push(path, PathOp::Move, start);
    } else if (std::fabs(cur.x - start.x) + std::fabs(cur.y - start.y) > 1e-7 * (1.0 + std::fabs(rx) + std::fabs(ry))) {
        push(path, PathOp::Line, start);
    }
    const double sweep = t1 - t0;
    const int n = std::max(1, int(std::ceil(std::fabs(sweep) / (kPi * 0.5) - 1e-9)));
    const double step = sweep / n;
    const double k = 4.0 / 3.0 * std::tan(step / 4.0);
    for (int s = 0; s < n; ++s) {
        const double a = t0 + step * s, b = (s + 1 == n) ? t1 : t0 + step * (s + 1);
        const Point pa = at(a), pb = at(b), da = deriv(a), db = deriv(b);
        push(path, PathOp::Cubic, Point{pa.x + k * da.x, pa.y + k * da.y}, Point{pb.x - k * db.x, pb.y - k * db.y}, pb);
    }
}

void append_svg_arc(Path& path, double x0, double y0, double rx, double ry, double rotation_deg, bool large, bool sweep,
                    double x, double y) {
    if (x0 == x && y0 == y) return;
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx == 0.0 || ry == 0.0) {
        push(path, PathOp::Line, Point{x, y});
        return;
    }
    const double phi = rotation_deg * kPi / 180.0;
    const double cp = std::cos(phi), sp = std::sin(phi);
    const double dx2 = (x0 - x) * 0.5, dy2 = (y0 - y) * 0.5;
    const double x1p = cp * dx2 + sp * dy2, y1p = -sp * dx2 + cp * dy2;
    const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1.0) {
        const double s = std::sqrt(lambda);
        rx *= s;
        ry *= s;
    }
    const double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    const double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    double coef = den > 0.0 ? std::sqrt(std::max(0.0, num / den)) : 0.0;
    if (large == sweep) coef = -coef;
    const double cxp = coef * rx * y1p / ry, cyp = -coef * ry * x1p / rx;
    const double cx = cp * cxp - sp * cyp + (x0 + x) * 0.5, cy = sp * cxp + cp * cyp + (y0 + y) * 0.5;
    const auto angle = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const double ux = (x1p - cxp) / rx, uy = (y1p - cyp) / ry;
    const double vx = (-x1p - cxp) / rx, vy = (-y1p - cyp) / ry;
    const double t1 = angle(1.0, 0.0, ux, uy);
    double dt = angle(ux, uy, vx, vy);
    if (!sweep && dt > 0.0) dt -= 2.0 * kPi;
    if (sweep && dt < 0.0) dt += 2.0 * kPi;
    append_arc(path, cx, cy, rx, ry, phi, t1, t1 + dt);
    // O fim exato (o arco termina no ponto pedido, sem o erro do seno).
    if (!path.empty() && path.back().kind == PathOp::Cubic) path.back().p[2] = Point{x, y};
}

void append_ellipse(Path& path, double cx, double cy, double rx, double ry) {
    if (!(rx > 0.0) || !(ry > 0.0)) return;
    push(path, PathOp::Move, Point{cx + rx, cy});
    append_arc(path, cx, cy, rx, ry, 0.0, 0.0, 2.0 * kPi);
    path.back().p[2] = Point{cx + rx, cy};
    push(path, PathOp::Close, Point{});
}

void append_rect(Path& path, double x, double y, double w, double h, double rx, double ry) {
    if (!(w > 0.0) || !(h > 0.0)) return;
    rx = std::clamp(rx, 0.0, w * 0.5);
    ry = std::clamp(ry, 0.0, h * 0.5);
    if (rx <= 0.0 || ry <= 0.0) {
        push(path, PathOp::Move, Point{x, y});
        push(path, PathOp::Line, Point{x + w, y});
        push(path, PathOp::Line, Point{x + w, y + h});
        push(path, PathOp::Line, Point{x, y + h});
        push(path, PathOp::Close, Point{});
        return;
    }
    push(path, PathOp::Move, Point{x + rx, y});
    push(path, PathOp::Line, Point{x + w - rx, y});
    append_arc(path, x + w - rx, y + ry, rx, ry, 0.0, -kPi * 0.5, 0.0);
    push(path, PathOp::Line, Point{x + w, y + h - ry});
    append_arc(path, x + w - rx, y + h - ry, rx, ry, 0.0, 0.0, kPi * 0.5);
    push(path, PathOp::Line, Point{x + rx, y + h});
    append_arc(path, x + rx, y + h - ry, rx, ry, 0.0, kPi * 0.5, kPi);
    push(path, PathOp::Line, Point{x, y + ry});
    append_arc(path, x + rx, y + ry, rx, ry, 0.0, kPi, kPi * 1.5);
    push(path, PathOp::Close, Point{});
}

Box path_bounds(const Path& path, const Matrix& m) {
    Box b;
    Point cur{};
    const auto add = [&](const Point& p) { b.add(m.apply_x(p.x, p.y), m.apply_y(p.x, p.y)); };
    for (const PathOp& op : path) {
        switch (op.kind) {
            case PathOp::Move:
            case PathOp::Line:
                add(op.p[0]);
                cur = op.p[0];
                break;
            case PathOp::Cubic: {
                // Os extremos da cúbica já transformada (a matriz é afim: a curva transformada é a cúbica dos pontos
                // transformados), em x e em y.
                double px[4], py[4];
                const Point pts[4] = {cur, op.p[0], op.p[1], op.p[2]};
                for (int k = 0; k < 4; ++k) {
                    px[k] = m.apply_x(pts[k].x, pts[k].y);
                    py[k] = m.apply_y(pts[k].x, pts[k].y);
                }
                b.add(px[0], py[0]);
                b.add(px[3], py[3]);
                for (int axis = 0; axis < 2; ++axis) {
                    const double* v = axis == 0 ? px : py;
                    // Derivada: 3[(v1-v0)(1-t)^2 + 2(v2-v1)t(1-t) + (v3-v2)t^2] = 0 -> a t^2 + b t + c.
                    const double a = -v[0] + 3 * v[1] - 3 * v[2] + v[3];
                    const double bb = 2 * (v[0] - 2 * v[1] + v[2]);
                    const double c = v[1] - v[0];
                    double roots[2];
                    int n = 0;
                    if (std::fabs(a) < 1e-12) {
                        if (std::fabs(bb) > 1e-12) roots[n++] = -c / bb;
                    } else {
                        const double disc = bb * bb - 4 * a * c;
                        if (disc >= 0) {
                            const double sq = std::sqrt(disc);
                            roots[n++] = (-bb + sq) / (2 * a);
                            roots[n++] = (-bb - sq) / (2 * a);
                        }
                    }
                    for (int k = 0; k < n; ++k) {
                        const double t = roots[k];
                        if (!(t > 0.0 && t < 1.0)) continue;
                        const double u = 1 - t;
                        const double x = u * u * u * px[0] + 3 * u * u * t * px[1] + 3 * u * t * t * px[2] + t * t * t * px[3];
                        const double y = u * u * u * py[0] + 3 * u * u * t * py[1] + 3 * u * t * t * py[2] + t * t * t * py[3];
                        b.add(x, y);
                    }
                }
                cur = op.p[2];
                break;
            }
            case PathOp::Close: break;
        }
    }
    return b;
}

bool parse_path(std::string_view d, Path& out, std::string* error) {
    size_t i = 0;
    char cmd = 0;
    Point cur{}, start{}, last_ctrl{};
    char last_cmd = 0;
    const auto fail = [&](const char* msg) {
        if (error != nullptr) *error = std::string(msg) + " em \"" + std::string(d.substr(0, 48)) + "\"";
        return false;
    };
    for (;;) {
        skip_sep(d, i);
        if (i >= d.size()) break;
        if (std::isalpha(uint8_t(d[i]))) {
            cmd = d[i++];
        } else if (cmd == 0) {
            return fail("caminho sem comando");
        } else if (cmd == 'M') {
            cmd = 'L';  // números seguidos depois de um M são linhas
        } else if (cmd == 'm') {
            cmd = 'l';
        } else if (cmd == 'Z' || cmd == 'z') {
            return fail("número depois de Z");
        }
        const bool rel = std::islower(uint8_t(cmd)) != 0;
        const char up = char(std::toupper(uint8_t(cmd)));
        const double ox = rel ? cur.x : 0.0, oy = rel ? cur.y : 0.0;
        double v[7];
        const auto nums = [&](int n) {
            for (int k = 0; k < n; ++k) {
                if (!number(d, i, v[k])) return false;
            }
            return true;
        };
        switch (up) {
            case 'M':
                if (!nums(2)) return fail("M sem coordenadas");
                cur = start = Point{ox + v[0], oy + v[1]};
                push(out, PathOp::Move, cur);
                break;
            case 'L':
                if (!nums(2)) return fail("L sem coordenadas");
                cur = Point{ox + v[0], oy + v[1]};
                push(out, PathOp::Line, cur);
                break;
            case 'H':
                if (!nums(1)) return fail("H sem coordenada");
                cur.x = ox + v[0];
                push(out, PathOp::Line, cur);
                break;
            case 'V':
                if (!nums(1)) return fail("V sem coordenada");
                cur.y = oy + v[0];
                push(out, PathOp::Line, cur);
                break;
            case 'C': {
                if (!nums(6)) return fail("C sem coordenadas");
                const Point c1{ox + v[0], oy + v[1]}, c2{ox + v[2], oy + v[3]}, p{ox + v[4], oy + v[5]};
                push(out, PathOp::Cubic, c1, c2, p);
                last_ctrl = c2;
                cur = p;
                break;
            }
            case 'S': {
                if (!nums(4)) return fail("S sem coordenadas");
                const char lu = char(std::toupper(uint8_t(last_cmd)));
                const Point c1 = (lu == 'C' || lu == 'S') ? Point{2 * cur.x - last_ctrl.x, 2 * cur.y - last_ctrl.y} : cur;
                const Point c2{ox + v[0], oy + v[1]}, p{ox + v[2], oy + v[3]};
                push(out, PathOp::Cubic, c1, c2, p);
                last_ctrl = c2;
                cur = p;
                break;
            }
            case 'Q':
            case 'T': {
                Point q, p;
                if (up == 'Q') {
                    if (!nums(4)) return fail("Q sem coordenadas");
                    q = Point{ox + v[0], oy + v[1]};
                    p = Point{ox + v[2], oy + v[3]};
                } else {
                    if (!nums(2)) return fail("T sem coordenadas");
                    const char lu = char(std::toupper(uint8_t(last_cmd)));
                    q = (lu == 'Q' || lu == 'T') ? Point{2 * cur.x - last_ctrl.x, 2 * cur.y - last_ctrl.y} : cur;
                    p = Point{ox + v[0], oy + v[1]};
                }
                // A quadrática como cúbica (exata): os controles a 2/3 do caminho até o da quadrática.
                push(out, PathOp::Cubic, Point{cur.x + 2.0 / 3.0 * (q.x - cur.x), cur.y + 2.0 / 3.0 * (q.y - cur.y)},
                     Point{p.x + 2.0 / 3.0 * (q.x - p.x), p.y + 2.0 / 3.0 * (q.y - p.y)}, p);
                last_ctrl = q;
                cur = p;
                break;
            }
            case 'A': {
                bool large = false, sweep = false;
                if (!nums(3) || !flag(d, i, large) || !flag(d, i, sweep) || !number(d, i, v[3]) || !number(d, i, v[4])) {
                    return fail("A sem parâmetros");
                }
                const Point p{ox + v[3], oy + v[4]};
                append_svg_arc(out, cur.x, cur.y, v[0], v[1], v[2], large, sweep, p.x, p.y);
                cur = p;
                break;
            }
            case 'Z':
                push(out, PathOp::Close, Point{});
                cur = start;
                break;
            default: return fail("comando de caminho desconhecido");
        }
        last_cmd = cmd;
    }
    return true;
}

bool parse_transform(std::string_view s, Matrix& out) {
    Matrix m;
    size_t i = 0;
    for (;;) {
        skip_sep(s, i);
        if (i >= s.size()) break;
        const size_t b = i;
        while (i < s.size() && std::isalpha(uint8_t(s[i]))) ++i;
        const std::string fn(s.substr(b, i - b));
        skip_sep(s, i);
        if (fn.empty() || i >= s.size() || s[i] != '(') return false;
        ++i;
        std::vector<double> a;
        double v;
        while (number(s, i, v)) a.push_back(v);
        skip_sep(s, i);
        if (i >= s.size() || s[i] != ')') return false;
        ++i;
        Matrix f;
        if (fn == "matrix" && a.size() == 6) {
            f = Matrix{a[0], a[1], a[2], a[3], a[4], a[5]};
        } else if (fn == "translate" && (a.size() == 1 || a.size() == 2)) {
            f = Matrix::translate(a[0], a.size() == 2 ? a[1] : 0.0);
        } else if (fn == "scale" && (a.size() == 1 || a.size() == 2)) {
            f = Matrix::scale(a[0], a.size() == 2 ? a[1] : a[0]);
        } else if (fn == "rotate" && (a.size() == 1 || a.size() == 3)) {
            const double t = a[0] * kPi / 180.0, c = std::cos(t), sn = std::sin(t);
            f = Matrix{c, sn, -sn, c, 0, 0};
            if (a.size() == 3) f = Matrix::translate(-a[1], -a[2]).then(f).then(Matrix::translate(a[1], a[2]));
        } else if (fn == "skewX" && a.size() == 1) {
            f = Matrix{1, 0, std::tan(a[0] * kPi / 180.0), 1, 0, 0};
        } else if (fn == "skewY" && a.size() == 1) {
            f = Matrix{1, std::tan(a[0] * kPi / 180.0), 0, 1, 0, 0};
        } else {
            return false;
        }
        // Na lista, a função da direita age primeiro: o ponto passa pela nova e depois pelas anteriores.
        m = f.then(m);
    }
    out = m;
    return true;
}

// ---------------------------------------------------------------- o documento

namespace {

struct Style {
    Paint fill{Paint::Color, 0, 0, 0, -1};
    Paint stroke;
    float fill_opacity = 1, stroke_opacity = 1;
    float group_opacity = 1;  // o produto das opacity dos grupos de fora
    double stroke_width = 1;
    Cap cap = Cap::Butt;
    Join join = Join::Miter;
    double miter = 4;
    bool evenodd = false;
    bool clip_evenodd = false;
};

class Builder {
public:
    explicit Builder(Document& doc) : doc_(doc) {}

    bool run(const Node& root, std::string* error) {
        if (root.name != "svg") {
            if (error != nullptr) *error = "o elemento de fora não é <svg>";
            return false;
        }
        // O viewBox e o tamanho.
        if (const std::string* vb = root.attr("viewBox")) {
            size_t i = 0;
            double v[4];
            for (double& x : v) {
                if (!number(*vb, i, x)) {
                    if (error != nullptr) *error = "viewBox inválido";
                    return false;
                }
            }
            doc_.vb_x = v[0];
            doc_.vb_y = v[1];
            doc_.vb_w = v[2];
            doc_.vb_h = v[3];
        }
        bool pct = false;
        double v = 0;
        if (const std::string* w = root.attr("width"); w != nullptr && length(*w, 0.0, v, &pct) && !pct) doc_.width = v;
        if (const std::string* h = root.attr("height"); h != nullptr && length(*h, 0.0, v, &pct) && !pct) doc_.height = v;
        if (doc_.vb_w <= 0.0 || doc_.vb_h <= 0.0) {
            doc_.vb_x = doc_.vb_y = 0.0;
            doc_.vb_w = doc_.width;
            doc_.vb_h = doc_.height;
        }
        if (!(doc_.vb_w > 0.0) || !(doc_.vb_h > 0.0)) {
            if (error != nullptr) *error = "sem viewBox nem tamanho";
            return false;
        }
        if (const std::string* p = root.attr("preserveAspectRatio")) parse_aspect(*p);
        // Primeiro os degradês e os recortes (podem ser usados antes de definidos), depois as formas.
        collect_defs(root);
        Style st;
        walk_children(root, st, Matrix{}, {});
        return true;
    }

private:
    Document& doc_;

    void ignore(const std::string& what) {
        if (std::find(doc_.ignored.begin(), doc_.ignored.end(), what) == doc_.ignored.end()) doc_.ignored.push_back(what);
    }

    void parse_aspect(const std::string& s) {
        const std::string t = trim(s);
        if (t == "none") {
            doc_.preserve_none = true;
            return;
        }
        size_t sp = t.find(' ');
        const std::string align = t.substr(0, sp);
        const std::string mode = sp == std::string::npos ? "" : trim(std::string_view(t).substr(sp + 1));
        const auto part = [](const std::string& a, const char* lo, const char* mid, const char* hi, int& out) {
            if (a.find(lo) != std::string::npos) out = 0;
            else if (a.find(mid) != std::string::npos) out = 1;
            else if (a.find(hi) != std::string::npos) out = 2;
        };
        part(align, "xMin", "xMid", "xMax", doc_.align_x);
        part(align, "YMin", "YMid", "YMax", doc_.align_y);
        doc_.slice = mode == "slice";
    }

    // Os atributos de um elemento, com os do style="..." por cima.
    static std::vector<std::pair<std::string, std::string>> props(const Node& n) {
        std::vector<std::pair<std::string, std::string>> out = n.attrs;
        if (const std::string* style = n.attr("style")) {
            size_t i = 0;
            while (i < style->size()) {
                const size_t semi = std::min(style->find(';', i), style->size());
                const std::string decl = style->substr(i, semi - i);
                const size_t colon = decl.find(':');
                if (colon != std::string::npos) out.emplace_back(trim(decl.substr(0, colon)), trim(decl.substr(colon + 1)));
                i = semi + 1;
            }
        }
        return out;
    }

    // ---- degradês e recortes ----

    void collect_defs(const Node& n) {
        for (const Node& c : n.children) {
            if (c.name == "linearGradient" || c.name == "radialGradient") {
                gradient(c);
            } else if (c.name == "clipPath") {
                clip_path(c);
            } else {
                collect_defs(c);
            }
        }
    }

    double coord(const std::string& s, bool user_space, double extent, double fallback) {
        double v = 0;
        bool pct = false;
        if (!length(s, 1.0, v, &pct)) {
            ignore("valor \"" + s + "\" num degradê");
            return fallback;
        }
        if (pct && user_space) v *= extent;
        return v;
    }

    void gradient(const Node& n) {
        Gradient g;
        g.radial = n.name == "radialGradient";
        bool has_fx = false, has_fy = false;
        if (const std::string* u = n.attr("gradientUnits")) {
            if (*u == "userSpaceOnUse") g.user_space = true;
            else if (*u != "objectBoundingBox") ignore("gradientUnits=\"" + *u + "\"");
        }
        const double diag = std::sqrt((doc_.vb_w * doc_.vb_w + doc_.vb_h * doc_.vb_h) * 0.5);
        if (g.user_space) {
            // Os padrões em porcentagem, no espaço do usuário.
            g.x2 = doc_.vb_w;
            g.cx = g.fx = doc_.vb_w * 0.5;
            g.cy = g.fy = doc_.vb_h * 0.5;
            g.r = diag * 0.5;
            g.x1 = g.y1 = g.y2 = 0.0;
        }
        for (const auto& [k, v] : n.attrs) {
            if (k == "id") g.id = v;
            else if (k == "gradientUnits") continue;
            else if (k == "x1" && !g.radial) g.x1 = coord(v, g.user_space, doc_.vb_w, g.x1);
            else if (k == "y1" && !g.radial) g.y1 = coord(v, g.user_space, doc_.vb_h, g.y1);
            else if (k == "x2" && !g.radial) g.x2 = coord(v, g.user_space, doc_.vb_w, g.x2);
            else if (k == "y2" && !g.radial) g.y2 = coord(v, g.user_space, doc_.vb_h, g.y2);
            else if (k == "cx" && g.radial) g.cx = coord(v, g.user_space, doc_.vb_w, g.cx);
            else if (k == "cy" && g.radial) g.cy = coord(v, g.user_space, doc_.vb_h, g.cy);
            else if (k == "r" && g.radial) g.r = coord(v, g.user_space, diag, g.r);
            else if (k == "fx" && g.radial) {
                g.fx = coord(v, g.user_space, doc_.vb_w, g.fx);
                has_fx = true;
            } else if (k == "fy" && g.radial) {
                g.fy = coord(v, g.user_space, doc_.vb_h, g.fy);
                has_fy = true;
            } else if (k == "gradientTransform") {
                if (!parse_transform(v, g.transform)) ignore("gradientTransform=\"" + v + "\"");
            } else if (k == "spreadMethod") {
                if (v != "pad") ignore("spreadMethod=\"" + v + "\"");
            } else {
                ignore("atributo " + k + " em " + n.name);
            }
        }
        if (!has_fx) g.fx = g.cx;
        if (!has_fy) g.fy = g.cy;
        double last = 0.0;
        for (const Node& s : n.children) {
            if (s.name != "stop") {
                ignore("elemento " + s.name + " num degradê");
                continue;
            }
            Stop st;
            for (const auto& [k, v] : props(s)) {
                if (k == "offset") {
                    double o = 0;
                    bool pct = false;
                    if (length(v, 1.0, o, &pct)) st.offset = std::clamp(o, 0.0, 1.0);
                    else ignore("offset=\"" + v + "\"");
                } else if (k == "stop-color") {
                    if (!color(v, st.r, st.g, st.b)) ignore("stop-color=\"" + v + "\"");
                } else if (k == "stop-opacity") {
                    st.a = float(std::clamp(std::strtod(v.c_str(), nullptr), 0.0, 1.0));
                } else if (k != "style" && k != "id") {
                    ignore("atributo " + k + " em stop");
                }
            }
            st.offset = std::max(st.offset, last);
            last = st.offset;
            g.stops.push_back(st);
        }
        doc_.gradients.push_back(std::move(g));
    }

    void clip_path(const Node& n) {
        ClipPath cp;
        for (const auto& [k, v] : n.attrs) {
            if (k == "id") cp.id = v;
            else if (k == "clipPathUnits") {
                if (v != "userSpaceOnUse") ignore("clipPathUnits=\"" + v + "\"");
            } else {
                ignore("atributo " + k + " em clipPath");
            }
        }
        for (const Node& c : n.children) {
            Path p;
            Matrix m;
            bool evenodd = false;
            if (!geometry(c, p)) {
                ignore("elemento " + c.name + " num clipPath");
                continue;
            }
            for (const auto& [k, v] : props(c)) {
                if (k == "transform") {
                    if (!parse_transform(v, m)) ignore("transform=\"" + v + "\"");
                } else if (k == "clip-rule") {
                    evenodd = v == "evenodd";
                }
            }
            // O caminho já no espaço do clipPath.
            for (PathOp& op : p) {
                for (Point& q : op.p) q = Point{m.apply_x(q.x, q.y), m.apply_y(q.x, q.y)};
            }
            cp.paths.push_back(std::move(p));
            cp.evenodd.push_back(evenodd ? 1 : 0);
        }
        doc_.clips.push_back(std::move(cp));
    }

    // ---- as formas ----

    // A geometria de um elemento de forma (false se não for forma). Atributos de geometria que faltam valem 0.
    bool geometry(const Node& n, Path& out) {
        const auto num = [&](const char* key, double extent) {
            double v = 0;
            if (const std::string* s = n.attr(key)) {
                if (!length(*s, extent, v)) ignore(std::string(key) + "=\"" + *s + "\"");
            }
            return v;
        };
        const double diag = std::sqrt((doc_.vb_w * doc_.vb_w + doc_.vb_h * doc_.vb_h) * 0.5);
        if (n.name == "path") {
            const std::string* d = n.attr("d");
            std::string err;
            if (d != nullptr && !parse_path(*d, out, &err)) ignore("d inválido: " + err);
            return true;
        }
        if (n.name == "rect") {
            const double x = num("x", doc_.vb_w), y = num("y", doc_.vb_h), w = num("width", doc_.vb_w),
                         h = num("height", doc_.vb_h);
            const bool has_rx = n.attr("rx") != nullptr, has_ry = n.attr("ry") != nullptr;
            double rx = num("rx", doc_.vb_w), ry = num("ry", doc_.vb_h);
            if (has_rx && !has_ry) ry = rx;
            if (has_ry && !has_rx) rx = ry;
            append_rect(out, x, y, w, h, rx, ry);
            return true;
        }
        if (n.name == "circle") {
            const double r = num("r", diag);
            append_ellipse(out, num("cx", doc_.vb_w), num("cy", doc_.vb_h), r, r);
            return true;
        }
        if (n.name == "ellipse") {
            append_ellipse(out, num("cx", doc_.vb_w), num("cy", doc_.vb_h), num("rx", doc_.vb_w), num("ry", doc_.vb_h));
            return true;
        }
        if (n.name == "line") {
            push(out, PathOp::Move, Point{num("x1", doc_.vb_w), num("y1", doc_.vb_h)});
            push(out, PathOp::Line, Point{num("x2", doc_.vb_w), num("y2", doc_.vb_h)});
            return true;
        }
        if (n.name == "polyline" || n.name == "polygon") {
            if (const std::string* pts = n.attr("points")) {
                size_t i = 0;
                double x, y;
                bool first = true;
                while (number(*pts, i, x) && number(*pts, i, y)) {
                    push(out, first ? PathOp::Move : PathOp::Line, Point{x, y});
                    first = false;
                }
                if (n.name == "polygon" && !first) push(out, PathOp::Close, Point{});
            }
            return true;
        }
        return false;
    }

    Paint paint(const std::string& v, const char* what) {
        Paint p;
        const std::string t = trim(v);
        if (t == "none" || t == "transparent") return p;
        if (t.rfind("url(", 0) == 0) {
            const size_t hash = t.find('#'), close = t.find(')');
            if (hash != std::string::npos && close != std::string::npos && close > hash) {
                const int g = doc_.find_gradient(std::string_view(t).substr(hash + 1, close - hash - 1));
                if (g >= 0) {
                    p.kind = Paint::Gradient;
                    p.gradient = g;
                    return p;
                }
            }
            ignore(std::string(what) + "=\"" + v + "\" (sem o degradê)");
            return p;
        }
        if (color(t, p.r, p.g, p.b)) {
            p.kind = Paint::Color;
            return p;
        }
        ignore(std::string(what) + "=\"" + v + "\"");
        return p;
    }

    static bool is_presentation(const std::string& k) {
        static const char* kKeys[] = {"fill",           "stroke",          "fill-opacity",      "stroke-opacity",
                                      "opacity",        "stroke-width",    "stroke-linecap",    "stroke-linejoin",
                                      "stroke-miterlimit", "fill-rule",    "clip-rule",         "clip-path",
                                      "transform",      "style",           "id",                "display",
                                      "visibility"};
        for (const char* key : kKeys) {
            if (k == key) return true;
        }
        return false;
    }

    static bool is_geometry_attr(const std::string& element, const std::string& k) {
        if (element == "path") return k == "d";
        if (element == "rect") return k == "x" || k == "y" || k == "width" || k == "height" || k == "rx" || k == "ry";
        if (element == "circle") return k == "cx" || k == "cy" || k == "r";
        if (element == "ellipse") return k == "cx" || k == "cy" || k == "rx" || k == "ry";
        if (element == "line") return k == "x1" || k == "y1" || k == "x2" || k == "y2";
        if (element == "polyline" || element == "polygon") return k == "points";
        return false;
    }

    // Aplica os atributos de apresentação de um elemento ao estilo herdado; devolve a matriz local e os recortes que o
    // próprio elemento pede (em `own`, vazio na entrada).
    bool apply(const Node& n, Style& st, Matrix& local, std::vector<ClipRef>& own, const Matrix& ctm) {
        local = Matrix{};
        bool visible = true;
        for (const auto& [k, v] : props(n)) {
            if (k == "fill") st.fill = paint(v, "fill");
            else if (k == "stroke") st.stroke = paint(v, "stroke");
            else if (k == "fill-opacity") st.fill_opacity = float(std::clamp(std::strtod(v.c_str(), nullptr), 0.0, 1.0));
            else if (k == "stroke-opacity") st.stroke_opacity = float(std::clamp(std::strtod(v.c_str(), nullptr), 0.0, 1.0));
            else if (k == "opacity") st.group_opacity *= float(std::clamp(std::strtod(v.c_str(), nullptr), 0.0, 1.0));
            else if (k == "stroke-width") {
                double w = 1;
                if (length(v, std::sqrt((doc_.vb_w * doc_.vb_w + doc_.vb_h * doc_.vb_h) * 0.5), w)) st.stroke_width = std::max(0.0, w);
                else ignore("stroke-width=\"" + v + "\"");
            } else if (k == "stroke-linecap") {
                if (v == "butt") st.cap = Cap::Butt;
                else if (v == "round") st.cap = Cap::Round;
                else if (v == "square") st.cap = Cap::Square;
                else ignore("stroke-linecap=\"" + v + "\"");
            } else if (k == "stroke-linejoin") {
                if (v == "miter") st.join = Join::Miter;
                else if (v == "round") st.join = Join::Round;
                else if (v == "bevel") st.join = Join::Bevel;
                else ignore("stroke-linejoin=\"" + v + "\"");
            } else if (k == "stroke-miterlimit") {
                st.miter = std::max(1.0, std::strtod(v.c_str(), nullptr));
            } else if (k == "fill-rule") {
                st.evenodd = v == "evenodd";
            } else if (k == "clip-rule") {
                st.clip_evenodd = v == "evenodd";
            } else if (k == "transform") {
                if (!parse_transform(v, local)) ignore("transform=\"" + v + "\"");
            } else if (k == "clip-path") {
                const size_t hash = v.find('#'), close = v.find(')');
                int found = -1;
                if (hash != std::string::npos && close != std::string::npos && close > hash) {
                    const std::string id = v.substr(hash + 1, close - hash - 1);
                    for (size_t c = 0; c < doc_.clips.size(); ++c) {
                        if (doc_.clips[c].id == id) found = int(c);
                    }
                }
                if (found >= 0) own.push_back(ClipRef{found, Matrix{}});
                else if (v != "none") ignore("clip-path=\"" + v + "\"");
            } else if (k == "display") {
                if (v == "none") visible = false;
            } else if (k == "visibility") {
                if (v == "hidden" || v == "collapse") visible = false;
            } else if (k == "style" || k == "id") {
                continue;
            } else if (!is_geometry_attr(n.name, k) && !(n.name == "svg" && is_root_attr(k))) {
                ignore("atributo " + k + " em " + n.name);
            }
        }
        // O recorte usa a matriz do elemento (com o transform dele), que só é conhecida no fim.
        for (ClipRef& c : own) c.m = local.then(ctm);
        return visible;
    }

    static bool is_root_attr(const std::string& k) {
        return k == "viewBox" || k == "width" || k == "height" || k == "preserveAspectRatio" || k == "xmlns" ||
               k.rfind("xmlns:", 0) == 0 || k == "version" || k == "x" || k == "y" || k == "xml:space";
    }

    void walk_children(const Node& n, const Style& st, const Matrix& ctm, const std::vector<ClipRef>& clips) {
        // Os atributos do <svg> de fora (a raiz: o transform dele não existe no SVG 1.1; os de apresentação valem).
        Style base = st;
        std::vector<ClipRef> base_clips = clips;
        if (n.name == "svg") {
            Matrix local;
            std::vector<ClipRef> own;
            apply(n, base, local, own, ctm);
            base_clips.insert(base_clips.end(), own.begin(), own.end());
        }
        for (const Node& c : n.children) walk(c, base, ctm, base_clips);
    }

    void walk(const Node& n, const Style& parent, const Matrix& ctm, const std::vector<ClipRef>& parent_clips) {
        if (n.name == "defs" || n.name == "linearGradient" || n.name == "radialGradient" || n.name == "clipPath" ||
            n.name == "title" || n.name == "desc" || n.name == "metadata") {
            return;
        }
        Style st = parent;
        // A opacidade e os recortes não são herdados como propriedade: a do grupo passa às formas pela multiplicação.
        Matrix local;
        std::vector<ClipRef> own;
        if (!apply(n, st, local, own, ctm)) return;
        const Matrix m = local.then(ctm);
        std::vector<ClipRef> clips = parent_clips;
        clips.insert(clips.end(), own.begin(), own.end());
        if (n.name == "g") {
            for (const Node& c : n.children) walk(c, st, m, clips);
            return;
        }
        Shape s;
        if (!geometry(n, s.path)) {
            ignore("elemento " + n.name);
            return;
        }
        for (const Node& c : n.children) {
            if (c.name != "title" && c.name != "desc") ignore("elemento " + c.name + " dentro de " + n.name);
        }
        s.element = n.name;
        s.m = m;
        s.fill = st.fill;
        s.stroke = st.stroke;
        s.fill_opacity = st.fill_opacity * st.group_opacity;
        s.stroke_opacity = st.stroke_opacity * st.group_opacity;
        s.stroke_width = st.stroke_width;
        s.cap = st.cap;
        s.join = st.join;
        s.miter = st.miter;
        s.evenodd = st.evenodd;
        s.clips = std::move(clips);
        // Uma linha nunca é preenchida (não tem área).
        if (n.name == "line" || n.name == "polyline") s.fill.kind = Paint::None;
        if (s.stroke_width <= 0.0) s.stroke.kind = Paint::None;
        if (s.path.empty()) return;
        doc_.shapes.push_back(std::move(s));
    }
};

}  // namespace

int Document::find_gradient(std::string_view id) const {
    for (size_t i = 0; i < gradients.size(); ++i) {
        if (gradients[i].id == id) return int(i);
    }
    return -1;
}

bool parse(std::string_view xml, Document& out, std::string* error) {
    out = Document{};
    Node root;
    Xml x(xml);
    if (!x.parse(root, error)) return false;
    Builder b(out);
    return b.run(root, error);
}

Matrix viewport(const Document& doc, double w, double h) {
    double sx = w / doc.vb_w, sy = h / doc.vb_h;
    double tx = 0, ty = 0;
    if (!doc.preserve_none) {
        const double s = doc.slice ? std::max(sx, sy) : std::min(sx, sy);
        const double ex = w - doc.vb_w * s, ey = h - doc.vb_h * s;
        tx = doc.align_x == 0 ? 0.0 : doc.align_x == 1 ? ex * 0.5 : ex;
        ty = doc.align_y == 0 ? 0.0 : doc.align_y == 1 ? ey * 0.5 : ey;
        sx = sy = s;
    }
    return Matrix::translate(-doc.vb_x, -doc.vb_y).then(Matrix::scale(sx, sy)).then(Matrix::translate(tx, ty));
}

Box shape_bounds(const Document& doc, const Shape& s) {
    const bool fill = s.fill.kind != Paint::None && s.fill_opacity > 0.0f;
    const bool stroke = s.stroke.kind != Paint::None && s.stroke_opacity > 0.0f && s.stroke_width > 0.0;
    Box b;
    if (!fill && !stroke) return b;
    b = path_bounds(s.path, s.m);
    if (stroke && !b.empty) {
        // Meia largura do traço no espaço do documento (o maior estiramento da matriz).
        const double k = std::max(std::hypot(s.m.a, s.m.b), std::hypot(s.m.c, s.m.d));
        double half = s.stroke_width * 0.5 * k;
        if (s.cap == Cap::Square || s.join == Join::Miter) half *= s.join == Join::Miter ? std::min(s.miter, 4.0) : 1.5;
        b.x0 -= half;
        b.y0 -= half;
        b.x1 += half;
        b.y1 += half;
    }
    for (const ClipRef& c : s.clips) {
        if (c.clip < 0 || size_t(c.clip) >= doc.clips.size()) continue;
        Box cb;
        for (const Path& p : doc.clips[size_t(c.clip)].paths) cb.add(path_bounds(p, c.m));
        b = b.intersect(cb);
    }
    return b;
}

Box document_bounds(const Document& doc) {
    Box b;
    for (const Shape& s : doc.shapes) b.add(shape_bounds(doc, s));
    return b;
}

Matrix gradient_matrix(const Gradient& g, const Box& bbox) {
    if (g.user_space) return g.transform;
    const Matrix unit = Matrix{std::max(1e-9, bbox.w()), 0, 0, std::max(1e-9, bbox.h()), bbox.x0, bbox.y0};
    return g.transform.then(unit);
}

}  // namespace nf::svg
