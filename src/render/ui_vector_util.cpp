// As contas neutras do desenho vetorial fora do Windows. Ver ui_vector_util.h.

#include "render/ui_vector_util.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/xxh3.h"

namespace nf {

// ---------------------------------------------------------------- geometria

UiVecRect ui_vec_snap_fill(UiVecRect r) {
    if (r.y1 - r.y0 < 6.0f) {
        const float h = std::max(1.0f, std::floor(r.y1 - r.y0 + 0.5f));
        r.y0 = std::floor(r.y0 + 0.5f);
        r.y1 = r.y0 + h;
    }
    if (r.x1 - r.x0 < 6.0f) {
        const float w = std::max(1.0f, std::floor(r.x1 - r.x0 + 0.5f));
        r.x0 = std::floor(r.x0 + 0.5f);
        r.x1 = r.x0 + w;
    }
    return r;
}

float ui_vec_stroke_width(float canvas_width, float scale) { return std::max(1.0f, std::floor(canvas_width * scale + 0.5f)); }

void ui_vec_snap_line(float& x0, float& y0, float& x1, float& y1, float width) {
    const float half = std::fmod(width, 2.0f) == 1.0f ? 0.5f : 0.0f;
    if (std::fabs(y0 - y1) < 0.01f) {
        y0 = y1 = std::floor(y0) + half;
    } else if (std::fabs(x0 - x1) < 0.01f) {
        x0 = x1 = std::floor(x0) + half;
    }
}

UiVecRect ui_vec_stroke_rect(UiVecRect r) {
    return UiVecRect{std::floor(r.x0) + 0.5f, std::floor(r.y0) + 0.5f, std::floor(r.x1) - 0.5f, std::floor(r.y1) - 0.5f};
}

UiVecRect ui_vec_aliased_clip(UiVecRect r) {
    return UiVecRect{std::ceil(r.x0 - 0.5f), std::ceil(r.y0 - 0.5f), std::ceil(r.x1 - 0.5f), std::ceil(r.y1 - 0.5f)};
}

// ---------------------------------------------------------------- degradês

std::vector<UiVecStop> ui_vec_gradient(const std::vector<UiVecStop>& stops, bool premultiplied, int samples) {
    if (stops.empty()) return {};
    if (!premultiplied || stops.size() == 1) return stops;
    // Os pontos pedidos mais os originais (as quinas do degradê ficam exatas).
    samples = std::max(samples, 2);
    std::vector<float> at;
    for (int i = 0; i < samples; ++i) at.push_back(float(i) / float(samples - 1));
    for (const UiVecStop& s : stops) at.push_back(std::clamp(s.offset, 0.0f, 1.0f));
    std::sort(at.begin(), at.end());
    at.erase(std::unique(at.begin(), at.end(), [](float a, float b) { return std::fabs(a - b) < 1e-6f; }), at.end());
    std::vector<UiVecStop> out;
    out.reserve(at.size());
    for (const float t : at) {
        // O par de pontos em volta de t.
        size_t k = 0;
        while (k + 1 < stops.size() && stops[k + 1].offset < t) ++k;
        const UiVecStop& a = stops[k];
        const UiVecStop& b = stops[std::min(k + 1, stops.size() - 1)];
        float u = 0.0f;
        if (t <= a.offset) u = 0.0f;
        else if (b.offset > a.offset) u = std::clamp((t - a.offset) / (b.offset - a.offset), 0.0f, 1.0f);
        else u = 1.0f;
        const float aa = std::clamp(a.a, 0.0f, 1.0f), ba = std::clamp(b.a, 0.0f, 1.0f);
        const float alpha = aa + (ba - aa) * u;
        UiVecStop s;
        s.offset = t;
        s.a = alpha;
        const float pr = a.r * aa + (b.r * ba - a.r * aa) * u;
        const float pg = a.g * aa + (b.g * ba - a.g * aa) * u;
        const float pb = a.b * aa + (b.b * ba - a.b * aa) * u;
        if (alpha > 1e-6f) {
            s.r = std::clamp(pr / alpha, 0.0f, 1.0f);
            s.g = std::clamp(pg / alpha, 0.0f, 1.0f);
            s.b = std::clamp(pb / alpha, 0.0f, 1.0f);
        } else {
            // Transparente: a cor não aparece; a do lado que tem alfa evita puxar outra cor para a interpolação.
            const UiVecStop& c = ba > aa ? b : a;
            s.r = c.r;
            s.g = c.g;
            s.b = c.b;
        }
        out.push_back(s);
    }
    return out;
}

namespace {
UiVecStop stop(float offset, const ui::Color& c) { return UiVecStop{offset, c.r, c.g, c.b, std::clamp(c.a, 0.0f, 1.0f)}; }
}  // namespace

std::vector<UiVecStop> ui_vec_modern_gradient(const ui::Color& a, const ui::Color& b, bool glow) {
    if (!glow) return {stop(0.0f, a), stop(1.0f, b)};
    const ui::Color mid = ui::mix(a, b, 0.35f).with_alpha(1.0f);
    return {stop(0.0f, a), stop(0.3f, ui::Color{mid.r, mid.g, mid.b, a.a * 0.62f}),
            stop(0.65f, ui::Color{b.r, b.g, b.b, a.a * 0.18f}), stop(1.0f, b)};
}

std::vector<UiVecStop> ui_vec_sector_gradient(const ui::Color& c, const ui::Color& c2, float k) {
    k = std::clamp(k, 0.0f, 0.999f);
    return {stop(0.0f, c), stop(k, c), stop(1.0f, c2)};
}

// ---------------------------------------------------------------- pintar direto na imagem

namespace {

// A parte de [a, b) dentro do pixel [i, i + 1).
inline float coverage(int i, float a, float b) {
    return std::clamp(std::min(b, float(i + 1)) - std::max(a, float(i)), 0.0f, 1.0f);
}

// Os pixels da imagem que o retângulo r (cortado pelo recorte) toca: [x0, x1) x [y0, y1), na imagem.
struct Span {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
bool span_of(const UiVecImage& img, const UiVecRect& r, const UiVecRect& clip, Span& s) {
    const float x0 = std::max(r.x0, clip.x0), y0 = std::max(r.y0, clip.y0);
    const float x1 = std::min(r.x1, clip.x1), y1 = std::min(r.y1, clip.y1);
    if (!(x1 > x0) || !(y1 > y0)) return false;
    s.x0 = std::max(0, int(std::floor(x0)) - img.ox);
    s.y0 = std::max(0, int(std::floor(y0)) - img.oy);
    s.x1 = std::min(img.w, int(std::ceil(x1)) - img.ox);
    s.y1 = std::min(img.h, int(std::ceil(y1)) - img.oy);
    return s.x1 > s.x0 && s.y1 > s.y0;
}

// Uma cor já pré-multiplicada em 8 bits (B, G, R, A), com a cobertura aplicada.
struct Px {
    uint32_t b = 0, g = 0, r = 0, a = 0;
};
Px premul(float r, float g, float b, float a) {
    a = std::clamp(a, 0.0f, 1.0f);
    const auto q = [](float v) { return uint32_t(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); };
    return Px{q(b * a), q(g * a), q(r * a), q(a)};
}

// v / 255 arredondado (v até 255 * 255).
inline uint32_t div255(uint32_t v) {
    v += 128;
    return (v + (v >> 8)) >> 8;
}

// Por cima de um pixel BGRA pré-multiplicado (ONE, ONE_MINUS_SRC_ALPHA), em inteiros.
inline void over(uint8_t* d, const Px& s) {
    if (s.a == 0 && (s.r | s.g | s.b) == 0) return;
    const uint32_t inv = 255u - s.a;
    d[0] = uint8_t(std::min(255u, s.b + div255(d[0] * inv)));
    d[1] = uint8_t(std::min(255u, s.g + div255(d[1] * inv)));
    d[2] = uint8_t(std::min(255u, s.r + div255(d[2] * inv)));
    d[3] = uint8_t(std::min(255u, s.a + div255(d[3] * inv)));
}

// A cor de um degradê em t (sem pré-multiplicar, interpolada nos valores, as pontas estendidas).
UiVecStop gradient_at(const std::vector<UiVecStop>& stops, float t) {
    if (stops.empty()) return UiVecStop{};
    if (t <= stops.front().offset) return stops.front();
    if (t >= stops.back().offset) return stops.back();
    size_t k = 0;
    while (k + 1 < stops.size() && stops[k + 1].offset < t) ++k;
    const UiVecStop& a = stops[k];
    const UiVecStop& b = stops[std::min(k + 1, stops.size() - 1)];
    const float u = b.offset > a.offset ? (t - a.offset) / (b.offset - a.offset) : 1.0f;
    return UiVecStop{t, a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, a.a + (b.a - a.a) * u};
}

UiVecRect image_rect(const UiVecImage& img) {
    return UiVecRect{float(img.ox), float(img.oy), float(img.ox + img.w), float(img.oy + img.h)};
}

UiVecRect intersect(const UiVecRect& a, const UiVecRect& b) {
    return UiVecRect{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

}  // namespace

void ui_vec_fill_rect(UiVecImage& img, UiVecRect r, UiVecRect clip, const ui::Color& c) {
    Span s;
    clip = intersect(clip, image_rect(img));
    if (img.bgra == nullptr || !span_of(img, r, clip, s)) return;
    const Px full = premul(c.r, c.g, c.b, c.a);
    std::vector<float> cx(size_t(s.x1 - s.x0));
    for (int x = s.x0; x < s.x1; ++x) cx[size_t(x - s.x0)] = coverage(x + img.ox, std::max(r.x0, clip.x0), std::min(r.x1, clip.x1));
    for (int y = s.y0; y < s.y1; ++y) {
        const float cy = coverage(y + img.oy, std::max(r.y0, clip.y0), std::min(r.y1, clip.y1));
        uint8_t* row = img.bgra + (size_t(y) * size_t(img.w) + size_t(s.x0)) * 4;
        for (int x = s.x0; x < s.x1; ++x, row += 4) {
            const float k = cy * cx[size_t(x - s.x0)];
            // O meio inteiro com a cor pronta; as bordas fracionárias com a cobertura.
            over(row, k >= 1.0f ? full : premul(c.r, c.g, c.b, c.a * k));
        }
    }
}

void ui_vec_fill_linear(UiVecImage& img, UiVecRect r, UiVecRect clip, const std::vector<UiVecStop>& stops, bool horizontal) {
    Span s;
    clip = intersect(clip, image_rect(img));
    if (img.bgra == nullptr || stops.empty() || !span_of(img, r, clip, s)) return;
    const float len = horizontal ? r.x1 - r.x0 : r.y1 - r.y0;
    const float start = horizontal ? r.x0 : r.y0;
    // A cor no centro de cada coluna (horizontal) ou linha (vertical).
    const int n = horizontal ? s.x1 - s.x0 : s.y1 - s.y0;
    std::vector<UiVecStop> col(static_cast<size_t>(n));
    std::vector<Px> col_px(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float p = float((horizontal ? s.x0 + img.ox : s.y0 + img.oy) + i) + 0.5f;
        col[size_t(i)] = gradient_at(stops, len > 0.0f ? (p - start) / len : 0.0f);
        const UiVecStop& c = col[size_t(i)];
        col_px[size_t(i)] = premul(c.r, c.g, c.b, c.a);
    }
    std::vector<float> cx(size_t(s.x1 - s.x0));
    for (int x = s.x0; x < s.x1; ++x) cx[size_t(x - s.x0)] = coverage(x + img.ox, std::max(r.x0, clip.x0), std::min(r.x1, clip.x1));
    for (int y = s.y0; y < s.y1; ++y) {
        const float cy = coverage(y + img.oy, std::max(r.y0, clip.y0), std::min(r.y1, clip.y1));
        uint8_t* row = img.bgra + (size_t(y) * size_t(img.w) + size_t(s.x0)) * 4;
        for (int x = s.x0; x < s.x1; ++x, row += 4) {
            const size_t i = size_t(horizontal ? x - s.x0 : y - s.y0);
            const float k = cy * cx[size_t(x - s.x0)];
            if (k >= 1.0f) {
                over(row, col_px[i]);
            } else {
                const UiVecStop& c = col[i];
                over(row, premul(c.r, c.g, c.b, c.a * k));
            }
        }
    }
}

void ui_vec_fill_radial(UiVecImage& img, UiVecRect e, UiVecRect area, UiVecRect clip, const std::vector<UiVecStop>& stops) {
    Span s;
    clip = intersect(clip, image_rect(img));
    const float rx = (e.x1 - e.x0) * 0.5f, ry = (e.y1 - e.y0) * 0.5f;
    if (img.bgra == nullptr || stops.empty() || !(rx > 0.0f) || !(ry > 0.0f) || !span_of(img, area, clip, s)) return;
    const float cx = (e.x0 + e.x1) * 0.5f, cy = (e.y0 + e.y1) * 0.5f;
    // A cor pronta pelo quadrado da distância normalizada (0 no centro, 1 na borda; além, a da borda): sem raiz por pixel.
    // A tabela é fina o bastante para o degradê mudar menos de um tom entre duas entradas.
    constexpr int kLut = 16384;
    std::vector<Px> lut(kLut + 1);
    std::vector<UiVecStop> lut_c(kLut + 1);
    for (int i = 0; i <= kLut; ++i) {
        lut_c[size_t(i)] = gradient_at(stops, std::sqrt(float(i) / float(kLut)));
        const UiVecStop& c = lut_c[size_t(i)];
        lut[size_t(i)] = premul(c.r, c.g, c.b, c.a);
    }
    std::vector<float> dx2(size_t(s.x1 - s.x0)), cov_x(size_t(s.x1 - s.x0));
    for (int x = s.x0; x < s.x1; ++x) {
        const float d = (float(x + img.ox) + 0.5f - cx) / rx;
        dx2[size_t(x - s.x0)] = d * d * float(kLut);
        cov_x[size_t(x - s.x0)] = coverage(x + img.ox, std::max(area.x0, clip.x0), std::min(area.x1, clip.x1));
    }
    for (int y = s.y0; y < s.y1; ++y) {
        const float d = (float(y + img.oy) + 0.5f - cy) / ry;
        const float dy2 = d * d * float(kLut);
        const float cov_y = coverage(y + img.oy, std::max(area.y0, clip.y0), std::min(area.y1, clip.y1));
        uint8_t* row = img.bgra + (size_t(y) * size_t(img.w) + size_t(s.x0)) * 4;
        for (int x = s.x0; x < s.x1; ++x, row += 4) {
            const float t2 = dx2[size_t(x - s.x0)] + dy2;
            const size_t i = t2 >= float(kLut) ? size_t(kLut) : size_t(t2 + 0.5f);
            const float k = cov_y * cov_x[size_t(x - s.x0)];
            if (k >= 1.0f) {
                over(row, lut[i]);
            } else {
                const UiVecStop& c = lut_c[i];
                over(row, premul(c.r, c.g, c.b, c.a * k));
            }
        }
    }
}

// ---------------------------------------------------------------- a camada HD

std::vector<UiHdLinePlace> ui_hd_place_lines(const UiHdText& t, const std::vector<float>& widths, float fit_min) {
    std::vector<UiHdLinePlace> out(t.lines.size());
    float wo = 0.0f, wh = 0.0f, left = 1e9f, right = -1e9f;
    for (size_t i = 0; i < t.lines.size(); ++i) {
        const UiHdLine& line = t.lines[i];
        const float width = i < widths.size() ? widths[i] : 0.0f;
        if (line.pinned) {
            const float wo_line = line.x1 - line.x0;
            out[i].sx = width > wo_line && width > 0.0f ? std::max(fit_min, wo_line / width) : 1.0f;
            out[i].x = line.x0;
        } else {
            wo = std::max(wo, line.x1 - line.x0);
            wh = std::max(wh, width);
            left = std::min(left, line.x0);
            right = std::max(right, line.x1);
        }
    }
    const float sx = wh > wo && wh > 0.0f ? std::max(fit_min, wo / wh) : 1.0f;
    float block = left;
    if (t.align & 2) block = (left + right) * 0.5f - wh * sx * 0.5f;
    else if (t.align & 4) block = right - wh * sx;
    for (size_t i = 0; i < t.lines.size(); ++i) {
        if (t.lines[i].pinned) continue;
        out[i].sx = sx;
        out[i].x = block + (t.lines[i].x0 - left) * sx;
    }
    return out;
}

float ui_hd_em(float cap_px, float cap_ratio, float size) {
    const float em = std::max(1.0f, cap_px / std::max(0.2f, cap_ratio) * size);
    return std::max(1.0f, float(std::lround(em * 64.0f)) / 64.0f);
}

UiHdKeyShape ui_hd_key_shape(const UiHdKey& k) {
    UiHdKeyShape s;
    const float h = k.y1 - k.y0;
    const float a = std::min(1.0f, k.rgba[3] / 128.0f);
    s.line = std::max(1.0f, h * 0.06f);
    s.radius = h * 0.22f;
    s.body = UiVecRect{k.x0 + s.line * 0.5f, k.y0 + s.line * 0.5f, k.x1 - s.line * 0.5f, k.y1 - s.line * 0.5f};
    s.stops[0] = UiVecStop{0.0f, 0.36f, 0.36f, 0.38f, a};
    s.stops[1] = UiVecStop{0.55f, 0.16f, 0.16f, 0.17f, a};
    s.stops[2] = UiVecStop{1.0f, 0.08f, 0.08f, 0.09f, a};
    const uint8_t edge[4] = {0x6e, 0x6e, 0x6c, uint8_t(std::lround(a * 0.9f * 128.0f))};
    const uint8_t ink[4] = {0x7e, 0x7e, 0x7c, uint8_t(std::lround(a * 128.0f))};
    std::memcpy(s.edge, edge, 4);
    std::memcpy(s.ink, ink, 4);
    return s;
}

void ui_hd_gs_colour(const uint8_t rgba[4], float out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = std::min(1.0f, rgba[i] / 128.0f);
}

void ui_vec_to_gs_rgba(const uint8_t* src, uint32_t pitch, int w, int h, bool premultiplied, std::vector<uint8_t>& out) {
    out.assign(size_t(w) * size_t(h) * 4, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + size_t(y) * pitch;
        uint8_t* d = out.data() + size_t(y) * size_t(w) * 4;
        for (int x = 0; x < w; ++x, s += 4, d += 4) {
            const uint32_t a = s[3];
            uint32_t b = s[0], g = s[1], r = s[2];
            if (premultiplied && a > 0 && a < 255) {
                r = std::min(255u, (r * 255 + a / 2) / a);
                g = std::min(255u, (g * 255 + a / 2) / a);
                b = std::min(255u, (b * 255 + a / 2) / a);
            }
            d[0] = uint8_t(r);
            d[1] = uint8_t(g);
            d[2] = uint8_t(b);
            d[3] = uint8_t((a * 128 + 127) / 255);
        }
    }
    for (int pass = 0; pass < 4; ++pass) {
        bool changed = false;
        std::vector<uint8_t> next = out;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint8_t* d = next.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
                if (out[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] != 0) continue;
                if (d[0] | d[1] | d[2]) continue;
                static const int kDir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& k : kDir) {
                    const int nx = x + k[0], ny = y + k[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const uint8_t* n = out.data() + (size_t(ny) * size_t(w) + size_t(nx)) * 4;
                    if (n[3] == 0 && (n[0] | n[1] | n[2]) == 0) continue;
                    d[0] = n[0];
                    d[1] = n[1];
                    d[2] = n[2];
                    changed = true;
                    break;
                }
            }
        }
        out.swap(next);
        if (!changed) break;
    }
}

// ---------------------------------------------------------------- o cache

namespace {

struct Hasher {
    std::vector<uint8_t> bytes;
    void raw(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        bytes.insert(bytes.end(), b, b + n);
    }
    void f(float v) { raw(&v, sizeof(v)); }
    void i(int32_t v) { raw(&v, sizeof(v)); }
    void color(const ui::Color& c) {
        f(c.r);
        f(c.g);
        f(c.b);
        f(c.a);
    }
};

void hash_canvas(Hasher& h, const ui::Canvas& cv, int window_w, int window_h) {
    h.i(window_w);
    h.i(window_h);
    h.i(cv.win_w);
    h.i(cv.win_h);
    h.f(cv.scale);
    h.f(cv.off_x);
    h.f(cv.off_y);
}

void hash_cmd(Hasher& h, const ui::DrawCmd& c) {
    {
        h.i(int32_t(c.kind));
        h.f(c.r.x0);
        h.f(c.r.y0);
        h.f(c.r.x1);
        h.f(c.r.y1);
        h.color(c.c);
        h.color(c.c2);
        h.f(c.width);
        h.f(c.radius);
        h.i(c.dir);
        h.f(c.a0);
        h.f(c.a1);
        h.i(int32_t(c.align));
        h.i(c.wrap ? 1 : 0);
        if (c.kind == ui::DrawCmd::Text) {
            h.i(int32_t(c.style.face));
            h.f(c.style.size);
            h.i(c.style.weight);
            h.f(c.style.tracking);
            h.i(c.style.caps ? 1 : 0);
            h.f(c.style.line);
            h.i(int32_t(c.text.size()));
            for (const wchar_t ch : c.text) h.i(int32_t(ch));
        }
        if (c.kind == ui::DrawCmd::Glyph) {
            h.i(int32_t(c.glyph.size()));
            h.raw(c.glyph.data(), c.glyph.size());
        }
    }
}

}  // namespace

uint64_t ui_vec_list_hash(const ui::DrawList& list, const ui::Canvas& cv, int window_w, int window_h) {
    Hasher h;
    h.bytes.reserve(list.cmds.size() * 96 + 64);
    hash_canvas(h, cv, window_w, window_h);
    for (const ui::DrawCmd& c : list.cmds) hash_cmd(h, c);
    return xxh3_64(h.bytes.data(), h.bytes.size());
}

std::vector<uint64_t> ui_vec_cmd_hashes(const ui::DrawList& list) {
    std::vector<uint64_t> out;
    out.reserve(list.cmds.size());
    Hasher h;
    for (const ui::DrawCmd& c : list.cmds) {
        h.bytes.clear();
        hash_cmd(h, c);
        out.push_back(xxh3_64(h.bytes.data(), h.bytes.size()));
    }
    return out;
}

uint64_t ui_vec_canvas_hash(const ui::Canvas& cv, int window_w, int window_h) {
    Hasher h;
    hash_canvas(h, cv, window_w, window_h);
    return xxh3_64(h.bytes.data(), h.bytes.size());
}

}  // namespace nf
