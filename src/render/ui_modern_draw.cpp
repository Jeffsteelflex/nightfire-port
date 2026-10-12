// O desenho do estilo de menu Modern com Direct2D e DirectWrite. Ver ui_modern_draw.h.

#include "render/ui_modern_draw.h"

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

namespace nf {

namespace {

template <typename T>
using Com = Microsoft::WRL::ComPtr<T>;

D2D1_COLOR_F d2d(const ui::Color& c) { return D2D1::ColorF(c.r, c.g, c.b, std::clamp(c.a, 0.0f, 1.0f)); }

std::vector<uint8_t> read_file(const std::wstring& path) {
    std::vector<uint8_t> data;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) return data;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0 && size < (16 << 20)) {
        data.resize(size_t(size));
        if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
    }
    std::fclose(f);
    return data;
}

uint32_t pack(const ui::Color& c) {
    const auto b = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return b(c.r) << 24 | b(c.g) << 16 | b(c.b) << 8 | b(c.a);
}

}  // namespace

struct UiModernDevice::Impl {
    Com<ID3D11Device> device;
    Com<ID2D1Factory1> factory;
    Com<ID2D1Device> d2d_device;
    Com<ID2D1DeviceContext> ctx;
    Com<ID2D1DeviceContext5> ctx5;
    Com<IDWriteFactory> dwrite;
    Com<IDWriteFontCollection> system;
    Com<IDWriteFontCollection> rajdhani;  // a coleção privada (nulo sem os arquivos)
    Com<ID2D1Bitmap1> target;
    Com<ID2D1SolidColorBrush> brush;
    Com<ID2D1StrokeStyle> round;
    std::wstring dir;
    std::wstring sans_family = L"Bahnschrift";
    IDWriteFontCollection* sans_collection = nullptr;
    std::string error;
    int font_files = 0;
    bool ready = false;

    struct FormatInfo {
        Com<IDWriteTextFormat> format;
        float cap = 0.7f;  // capHeight / em
    };
    std::map<uint64_t, FormatInfo> formats;
    std::unordered_map<std::wstring, float> widths;
    // Os layouts de texto dos últimos quadros (a tela monta os mesmos textos a cada quadro).
    std::unordered_map<std::wstring, std::pair<Com<IDWriteTextLayout>, FormatInfo*>> layouts;
    std::map<uint64_t, Com<ID2D1GradientStopCollection>> stops;
    std::map<std::string, Com<ID2D1SvgDocument>> glyphs;
    float scale = 1.0f;
    float off_x = 0.0f, off_y = 0.0f;

    bool init(ID3D11Device* dev) {
        device = dev;
        D2D1_FACTORY_OPTIONS fo{};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                     reinterpret_cast<void**>(factory.GetAddressOf())))) {
            error = "D2D1CreateFactory falhou";
            return false;
        }
        Com<IDXGIDevice> dxgi;
        if (FAILED(device.As(&dxgi)) || FAILED(factory->CreateDevice(dxgi.Get(), &d2d_device)) ||
            FAILED(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx))) {
            error = "Direct2D no dispositivo D3D11 falhou";
            return false;
        }
        ctx.As(&ctx5);
        ctx->SetDpi(96.0f, 96.0f);
        ctx->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
        ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dwrite.GetAddressOf())))) {
            error = "DWriteCreateFactory falhou";
            return false;
        }
        dwrite->GetSystemFontCollection(&system);
        ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
        const D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        factory->CreateStrokeStyle(sp, nullptr, 0, &round);
        ready = brush != nullptr;
        load_fonts();
        return ready;
    }

    // A Rajdhani da pasta do repositório numa coleção só dela; sem ela, a Bahnschrift do sistema.
    void load_fonts() {
        rajdhani.Reset();
        font_files = 0;
        layouts.clear();
        formats.clear();
        widths.clear();
        sans_family = L"Bahnschrift";
        sans_collection = system.Get();
        Com<IDWriteFactory5> f5;
        if (!dir.empty() && SUCCEEDED(dwrite.As(&f5))) {
            Com<IDWriteFontSetBuilder1> builder;
            if (SUCCEEDED(f5->CreateFontSetBuilder(&builder))) {
                const std::wstring folder = dir + L"\\fonts_ofl\\rajdhani\\";
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((folder + L"*.ttf").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        Com<IDWriteFontFile> file;
                        if (SUCCEEDED(f5->CreateFontFileReference((folder + fd.cFileName).c_str(), nullptr, &file)) &&
                            SUCCEEDED(builder->AddFontFile(file.Get()))) {
                            ++font_files;
                        }
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
                Com<IDWriteFontSet> set;
                Com<IDWriteFontCollection1> c1;
                if (font_files > 0 && SUCCEEDED(builder->CreateFontSet(&set)) &&
                    SUCCEEDED(f5->CreateFontCollectionFromFontSet(set.Get(), &c1))) {
                    UINT32 index = 0;
                    BOOL exists = FALSE;
                    if (SUCCEEDED(c1->FindFamilyName(L"Rajdhani", &index, &exists)) && exists) {
                        rajdhani = c1;
                        sans_family = L"Rajdhani";
                        sans_collection = rajdhani.Get();
                    }
                }
            }
        }
    }

    FormatInfo* format(ui::Face face, int weight, float px) {
        const uint64_t key = uint64_t(face) << 48 | uint64_t(uint16_t(weight)) << 32 | uint32_t(std::lround(px * 16.0f));
        auto it = formats.find(key);
        if (it != formats.end()) return &it->second;
        const wchar_t* family = face == ui::Face::Mono ? L"Consolas" : sans_family.c_str();
        IDWriteFontCollection* coll = face == ui::Face::Mono ? system.Get() : sans_collection;
        FormatInfo fi;
        if (FAILED(dwrite->CreateTextFormat(family, coll, DWRITE_FONT_WEIGHT(weight), DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, std::max(1.0f, px), L"en-us", &fi.format))) {
            return nullptr;
        }
        UINT32 index = 0;
        BOOL exists = FALSE;
        Com<IDWriteFontFamily> fam;
        Com<IDWriteFont> font;
        if (coll != nullptr && SUCCEEDED(coll->FindFamilyName(family, &index, &exists)) && exists &&
            SUCCEEDED(coll->GetFontFamily(index, &fam)) &&
            SUCCEEDED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT(weight), DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                &font))) {
            DWRITE_FONT_METRICS m{};
            font->GetMetrics(&m);
            if (m.designUnitsPerEm != 0 && m.capHeight != 0) fi.cap = float(m.capHeight) / float(m.designUnitsPerEm);
        }
        if (formats.size() > 512) {
            formats.clear();
            layouts.clear();
        }
        return &(formats[key] = fi);
    }

    Com<IDWriteTextLayout> layout(const std::wstring& text, const ui::TextStyle& st, float px, float max_w, float max_h,
                                  ui::Align align, bool wrap, FormatInfo** info) {
        FormatInfo* fi = format(st.face, st.weight, px);
        if (info != nullptr) *info = fi;
        if (fi == nullptr) return nullptr;
        Com<IDWriteTextLayout> l;
        if (FAILED(dwrite->CreateTextLayout(text.c_str(), UINT32(text.size()), fi->format.Get(), std::max(1.0f, max_w),
                                            std::max(1.0f, max_h), &l))) {
            return nullptr;
        }
        l->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        l->SetTextAlignment(align == ui::Align::Left     ? DWRITE_TEXT_ALIGNMENT_LEADING
                            : align == ui::Align::Centre ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                         : DWRITE_TEXT_ALIGNMENT_TRAILING);
        l->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (wrap) l->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, px * st.line, px * st.line * 0.8f);
        if (st.tracking != 0.0f) {
            Com<IDWriteTextLayout1> l1;
            if (SUCCEEDED(l.As(&l1))) l1->SetCharacterSpacing(0.0f, st.tracking * px, 0.0f, DWRITE_TEXT_RANGE{0, UINT32(text.size())});
        }
        return l;
    }

    float measure(const std::wstring& text, const ui::TextStyle& st) {
        if (text.empty()) return 0.0f;
        wchar_t key[64];
        swprintf(key, 64, L"%d|%d|%.2f|%.3f|", int(st.face), st.weight, double(st.size), double(st.tracking));
        const std::wstring k = key + text;
        const auto it = widths.find(k);
        if (it != widths.end()) return it->second;
        float w = ui::estimate_width(text, st);
        Com<IDWriteTextLayout> l = layout(text, st, st.size, 100000.0f, st.size * 4.0f, ui::Align::Left, false, nullptr);
        DWRITE_TEXT_METRICS m{};
        if (l != nullptr && SUCCEEDED(l->GetMetrics(&m))) w = std::max(0.0f, m.widthIncludingTrailingWhitespace - st.tracking * st.size);
        if (widths.size() > 4096) widths.clear();
        widths[k] = w;
        return w;
    }

    ID2D1GradientStopCollection* gradient(const ui::Color& a, const ui::Color& b, bool glow) {
        const uint64_t key = uint64_t(pack(a)) << 32 | pack(b);
        const uint64_t k2 = key ^ (glow ? 0x8000000000000000ull : 0);
        auto it = stops.find(k2);
        if (it != stops.end()) return it->second.Get();
        Com<ID2D1GradientStopCollection> s;
        if (glow) {
            const ui::Color mid = ui::mix(a, b, 0.35f).with_alpha(1.0f);
            const D2D1_GRADIENT_STOP st[4] = {{0.0f, d2d(a)},
                                              {0.3f, d2d(ui::Color{mid.r, mid.g, mid.b, a.a * 0.62f})},
                                              {0.65f, d2d(ui::Color{b.r, b.g, b.b, a.a * 0.18f})},
                                              {1.0f, d2d(b)}};
            ctx->CreateGradientStopCollection(st, 4, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &s);
        } else {
            const D2D1_GRADIENT_STOP st[2] = {{0.0f, d2d(a)}, {1.0f, d2d(b)}};
            ctx->CreateGradientStopCollection(st, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &s);
        }
        if (stops.size() > 256) stops.clear();
        stops[k2] = s;
        return s.Get();
    }

    ID2D1SvgDocument* glyph(const std::string& name) {
        if (ctx5 == nullptr || dir.empty()) return nullptr;
        auto it = glyphs.find(name);
        if (it != glyphs.end()) return it->second.Get();
        Com<ID2D1SvgDocument> doc;
        const std::wstring path = dir + L"\\svg\\" + std::wstring(name.begin(), name.end()) + L".svg";
        const std::vector<uint8_t> data = read_file(path);
        if (!data.empty()) {
            Com<IStream> stream;
            stream.Attach(SHCreateMemStream(data.data(), UINT(data.size())));
            if (stream == nullptr || FAILED(ctx5->CreateSvgDocument(stream.Get(), D2D1::SizeF(32, 32), &doc))) doc.Reset();
        }
        glyphs[name] = doc;
        return doc.Get();
    }

    // Da tela virtual para a janela.
    float X(float x) const { return off_x + x * scale; }
    float Y(float y) const { return off_y + y * scale; }
    D2D1_RECT_F R(const ui::Rect& r) const { return D2D1::RectF(X(r.x0), Y(r.y0), X(r.x1), Y(r.y1)); }
    float W(float w) const { return std::max(1.0f, std::floor(w * scale + 0.5f)); }

    void fill(const ui::DrawCmd& c) {
        brush->SetColor(d2d(c.c));
        D2D1_RECT_F r = R(c.r);
        if (c.radius > 0.0f) {
            ctx->FillRoundedRectangle(D2D1::RoundedRect(r, c.radius * scale, c.radius * scale), brush.Get());
            return;
        }
        // Retângulos finos presos ao pixel (os sublinhados ficam nítidos).
        if (r.bottom - r.top < 6.0f) {
            const float h = std::max(1.0f, std::floor(r.bottom - r.top + 0.5f));
            r.top = std::floor(r.top + 0.5f);
            r.bottom = r.top + h;
        }
        if (r.right - r.left < 6.0f) {
            const float w = std::max(1.0f, std::floor(r.right - r.left + 0.5f));
            r.left = std::floor(r.left + 0.5f);
            r.right = r.left + w;
        }
        ctx->FillRectangle(r, brush.Get());
    }

    void line(const ui::DrawCmd& c) {
        brush->SetColor(d2d(c.c));
        const float w = W(c.width);
        float x0 = X(c.r.x0), y0 = Y(c.r.y0), x1 = X(c.r.x1), y1 = Y(c.r.y1);
        const float half = std::fmod(w, 2.0f) == 1.0f ? 0.5f : 0.0f;
        if (std::fabs(y0 - y1) < 0.01f) {
            y0 = y1 = std::floor(y0) + half;
        } else if (std::fabs(x0 - x1) < 0.01f) {
            x0 = x1 = std::floor(x0) + half;
        }
        ctx->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), brush.Get(), w);
    }

    void text(const ui::DrawCmd& c) {
        const ui::TextStyle& st = c.style;
        const std::wstring s = st.caps ? ui::to_upper(c.text) : c.text;
        const float px = st.size * scale;
        const D2D1_RECT_F r = R(c.r);
        FormatInfo* fi = nullptr;
        // O espaço depois da última letra: o alinhamento à direita e o centrado o compensam.
        const float trail = st.tracking * px;
        const float dx = c.align == ui::Align::Right ? trail : (c.align == ui::Align::Centre ? trail * 0.5f : 0.0f);
        const float max_w = r.right - r.left, max_h = c.wrap ? r.bottom - r.top : px * 4.0f;
        wchar_t key[96];
        swprintf(key, 96, L"%d|%d|%.2f|%.3f|%.1f|%.1f|%d|%d|%.2f|", int(st.face), st.weight, double(px), double(st.tracking),
                 double(max_w), double(max_h), int(c.align), c.wrap ? 1 : 0, double(st.line));
        const std::wstring k = key + s;
        Com<IDWriteTextLayout> l;
        const auto it = layouts.find(k);
        if (it != layouts.end()) {
            l = it->second.first;
            fi = it->second.second;
        } else {
            l = layout(s, st, px, max_w, max_h, c.align, c.wrap, &fi);
            if (l == nullptr) return;
            if (layouts.size() > 1024) layouts.clear();
            layouts[k] = {l, fi};
        }
        brush->SetColor(d2d(c.c));
        float oy = r.top;
        if (!c.wrap) {
            // Uma linha: as maiúsculas centradas na vertical no retângulo.
            DWRITE_LINE_METRICS lm{};
            UINT32 n = 0;
            if (SUCCEEDED(l->GetLineMetrics(&lm, 1, &n)) && n >= 1) {
                const float cap = (fi != nullptr ? fi->cap : 0.7f) * px;
                oy = (r.top + r.bottom) * 0.5f + cap * 0.5f - lm.baseline;
            }
        }
        ctx->DrawTextLayout(D2D1::Point2F(r.left + dx, oy), l.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
    }

    void radial_fill(const D2D1_RECT_F& area, const ui::Rect& ell, const ui::Color& a, const ui::Color& b, bool glow) {
        ID2D1GradientStopCollection* s = gradient(a, b, glow);
        if (s == nullptr) return;
        Com<ID2D1RadialGradientBrush> rb;
        const D2D1_RECT_F e = R(ell);
        if (FAILED(ctx->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(D2D1::Point2F((e.left + e.right) * 0.5f, (e.top + e.bottom) * 0.5f),
                                                    D2D1::Point2F(0, 0), (e.right - e.left) * 0.5f, (e.bottom - e.top) * 0.5f),
                s, &rb))) {
            return;
        }
        if (glow) {
            ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F((e.left + e.right) * 0.5f, (e.top + e.bottom) * 0.5f),
                                           (e.right - e.left) * 0.5f, (e.bottom - e.top) * 0.5f),
                             rb.Get());
        } else {
            ctx->FillRectangle(area, rb.Get());
        }
    }

    void path(const D2D1_POINT_2F* pts, int n, bool closed, bool filled, const ui::Color& c, float width) {
        Com<ID2D1PathGeometry> g;
        Com<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return;
        sink->BeginFigure(pts[0], filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 1; i < n; ++i) sink->AddLine(pts[i]);
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        sink->Close();
        brush->SetColor(d2d(c));
        if (filled) {
            ctx->FillGeometry(g.Get(), brush.Get());
        } else {
            ctx->DrawGeometry(g.Get(), brush.Get(), width, round.Get());
        }
    }

    // O setor de anel (a roda de armas): o arco de fora de a0 a a1, a linha para o de dentro e o arco de volta.
    void sector(const ui::DrawCmd& c) {
        const D2D1_RECT_F r = R(c.r);
        const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
        const float ro = (r.right - r.left) * 0.5f, ri = c.radius * scale;
        const auto at = [&](float rad, float a) { return D2D1::Point2F(cx + rad * std::sin(a), cy - rad * std::cos(a)); };
        const D2D1_ARC_SIZE size = c.a1 - c.a0 > 3.14159265f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
        Com<ID2D1PathGeometry> g;
        Com<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return;
        sink->BeginFigure(at(ro, c.a0), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddArc(D2D1::ArcSegment(at(ro, c.a1), D2D1::SizeF(ro, ro), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, size));
        if (ri > 0.5f) {
            sink->AddLine(at(ri, c.a1));
            sink->AddArc(D2D1::ArcSegment(at(ri, c.a0), D2D1::SizeF(ri, ri), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                          size));
        } else {
            sink->AddLine(D2D1::Point2F(cx, cy));
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        if (c.dir == 1 && ro > 0.5f) {
            // Em degradê do raio de dentro (c) para o de fora (c2): um pincel radial no centro da roda.
            const float k = std::clamp(ri / ro, 0.0f, 0.999f);
            const D2D1_GRADIENT_STOP st[3] = {{0.0f, d2d(c.c)}, {k, d2d(c.c)}, {1.0f, d2d(c.c2)}};
            Com<ID2D1GradientStopCollection> stops3;
            Com<ID2D1RadialGradientBrush> rb;
            if (SUCCEEDED(ctx->CreateGradientStopCollection(st, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stops3)) &&
                SUCCEEDED(ctx->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), ro, ro), stops3.Get(),
                    &rb))) {
                ctx->FillGeometry(g.Get(), rb.Get());
            }
            return;
        }
        brush->SetColor(d2d(c.c));
        ctx->FillGeometry(g.Get(), brush.Get());
    }

    void padlock(const ui::DrawCmd& c) {
        const D2D1_RECT_F r = R(c.r);
        const float w = r.right - r.left, h = r.bottom - r.top;
        brush->SetColor(d2d(c.c));
        // O corpo (a metade de baixo) e a alça (um arco).
        const D2D1_RECT_F body = D2D1::RectF(r.left, r.top + h * 0.45f, r.right, r.bottom);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(body, w * 0.12f, w * 0.12f), brush.Get());
        Com<ID2D1PathGeometry> g;
        Com<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return;
        const float sx0 = r.left + w * 0.22f, sx1 = r.right - w * 0.22f;
        const float sy = r.top + h * 0.48f, top = r.top + h * 0.06f;
        sink->BeginFigure(D2D1::Point2F(sx0, sy), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(D2D1::Point2F(sx0, top + (sx1 - sx0) * 0.5f));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(sx1, top + (sx1 - sx0) * 0.5f), D2D1::SizeF((sx1 - sx0) * 0.5f, (sx1 - sx0) * 0.5f),
                                      0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->AddLine(D2D1::Point2F(sx1, sy));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        ctx->DrawGeometry(g.Get(), brush.Get(), std::max(1.2f, w * 0.16f), round.Get());
    }

    void draw(const ui::DrawList& list, const ui::Canvas& cv) {
        if (!ready || target == nullptr) return;
        scale = cv.scale;
        off_x = cv.off_x;
        off_y = cv.off_y;
        const D2D1_RECT_F window = D2D1::RectF(0, 0, float(cv.win_w), float(cv.win_h));
        ctx->SetTarget(target.Get());
        ctx->BeginDraw();
        ctx->SetTransform(D2D1::Matrix3x2F::Identity());
        int clips = 0;
        for (const ui::DrawCmd& c : list.cmds) {
            switch (c.kind) {
                case ui::DrawCmd::Fill: fill(c); break;
                case ui::DrawCmd::Stroke: {
                    brush->SetColor(d2d(c.c));
                    const D2D1_RECT_F r = R(c.r);
                    const float w = W(c.width);
                    const D2D1_RECT_F rr = D2D1::RectF(std::floor(r.left) + 0.5f, std::floor(r.top) + 0.5f,
                                                       std::floor(r.right) - 0.5f, std::floor(r.bottom) - 0.5f);
                    if (c.radius > 0.0f) {
                        ctx->DrawRoundedRectangle(D2D1::RoundedRect(rr, c.radius * scale, c.radius * scale), brush.Get(), w);
                    } else {
                        ctx->DrawRectangle(rr, brush.Get(), w);
                    }
                    break;
                }
                case ui::DrawCmd::Line: line(c); break;
                case ui::DrawCmd::Text: text(c); break;
                case ui::DrawCmd::Ellipse: {
                    brush->SetColor(d2d(c.c));
                    const D2D1_RECT_F r = R(c.r);
                    const D2D1_ELLIPSE e = D2D1::Ellipse(D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f),
                                                         (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f);
                    if (c.width > 0.0f) {
                        ctx->DrawEllipse(e, brush.Get(), W(c.width));
                    } else {
                        ctx->FillEllipse(e, brush.Get());
                    }
                    break;
                }
                case ui::DrawCmd::Glow: {
                    const D2D1_RECT_F r = R(c.r);
                    ctx->PushAxisAlignedClip(D2D1::RectF(r.left, r.top, r.right, Y(c.radius)), D2D1_ANTIALIAS_MODE_ALIASED);
                    radial_fill(r, c.r, c.c, c.c2, true);
                    ctx->PopAxisAlignedClip();
                    break;
                }
                case ui::DrawCmd::Radial: radial_fill(window, c.r, c.c, c.c2, false); break;
                case ui::DrawCmd::Gradient: {
                    ID2D1GradientStopCollection* s = gradient(c.c, c.c2, false);
                    if (s == nullptr) break;
                    const D2D1_RECT_F r = R(c.r);
                    Com<ID2D1LinearGradientBrush> lb;
                    const D2D1_POINT_2F a = D2D1::Point2F(r.left, r.top);
                    const D2D1_POINT_2F b = c.dir == 1 ? D2D1::Point2F(r.right, r.top) : D2D1::Point2F(r.left, r.bottom);
                    if (SUCCEEDED(ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(a, b), s, &lb))) {
                        ctx->FillRectangle(r, lb.Get());
                    }
                    break;
                }
                case ui::DrawCmd::Chevron: {
                    const D2D1_RECT_F r = R(c.r);
                    const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
                    D2D1_POINT_2F p[3];
                    switch (c.dir) {
                        case 1: p[0] = {r.left, r.bottom}; p[1] = {cx, r.top}; p[2] = {r.right, r.bottom}; break;
                        case 2: p[0] = {r.right, r.top}; p[1] = {r.left, cy}; p[2] = {r.right, r.bottom}; break;
                        case 3: p[0] = {r.left, r.top}; p[1] = {r.right, cy}; p[2] = {r.left, r.bottom}; break;
                        default: p[0] = {r.left, r.top}; p[1] = {cx, r.bottom}; p[2] = {r.right, r.top}; break;
                    }
                    path(p, 3, false, false, c.c, std::max(1.0f, c.width * scale));
                    break;
                }
                case ui::DrawCmd::Padlock: padlock(c); break;
                case ui::DrawCmd::Diamond: {
                    const D2D1_RECT_F r = R(c.r);
                    const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
                    const D2D1_POINT_2F p[4] = {{cx, r.top}, {r.right, cy}, {cx, r.bottom}, {r.left, cy}};
                    path(p, 4, true, true, c.c, 1.0f);
                    break;
                }
                case ui::DrawCmd::Glyph: {
                    ID2D1SvgDocument* doc = glyph(c.glyph);
                    const D2D1_RECT_F r = R(c.r);
                    if (doc == nullptr) {
                        // Sem o SVG: uma tecla com o nome.
                        brush->SetColor(D2D1::ColorF(0.85f, 0.82f, 0.78f, c.c.a));
                        ctx->FillRoundedRectangle(D2D1::RoundedRect(r, 3 * scale, 3 * scale), brush.Get());
                        break;
                    }
                    doc->SetViewportSize(D2D1::SizeF(std::max(1.0f, r.right - r.left), std::max(1.0f, r.bottom - r.top)));
                    const bool layer = c.c.a < 0.999f;
                    if (layer) {
                        ctx->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                              D2D1::IdentityMatrix(), c.c.a),
                                       nullptr);
                    }
                    ctx->SetTransform(D2D1::Matrix3x2F::Translation(r.left, r.top));
                    ctx5->DrawSvgDocument(doc);
                    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
                    if (layer) ctx->PopLayer();
                    break;
                }
                case ui::DrawCmd::Sector: sector(c); break;
                case ui::DrawCmd::PushClip:
                    ctx->PushAxisAlignedClip(R(c.r), D2D1_ANTIALIAS_MODE_ALIASED);
                    ++clips;
                    break;
                case ui::DrawCmd::PopClip:
                    if (clips > 0) {
                        ctx->PopAxisAlignedClip();
                        --clips;
                    }
                    break;
            }
        }
        while (clips-- > 0) ctx->PopAxisAlignedClip();
        if (FAILED(ctx->EndDraw())) error = "EndDraw do Direct2D falhou";
        ctx->SetTarget(nullptr);
    }
};

UiModernDevice::UiModernDevice(ID3D11Device* device, ID3D11DeviceContext*) : impl_(std::make_unique<Impl>()) {
    impl_->init(device);
}

UiModernDevice::~UiModernDevice() = default;

bool UiModernDevice::ok() const { return impl_->ready; }

void UiModernDevice::configure(const std::wstring& dir) {
    impl_->dir = dir;
    impl_->glyphs.clear();
    if (impl_->ready) impl_->load_fonts();
}

void UiModernDevice::set_target(ID3D11Texture2D* target) {
    Impl& m = *impl_;
    m.target.Reset();
    if (!m.ready) return;
    m.ctx->SetTarget(nullptr);
    if (target == nullptr) return;
    Com<IDXGISurface> surface;
    if (FAILED(target->QueryInterface(IID_PPV_ARGS(&surface)))) return;
    const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(m.ctx->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &m.target))) m.error = "alvo do Direct2D falhou";
}

float UiModernDevice::measure(const std::wstring& text, const ui::TextStyle& style) {
    if (!impl_->ready) return ui::estimate_width(text, style);
    return impl_->measure(style.caps ? ui::to_upper(text) : text, style);
}

void UiModernDevice::draw(const ui::DrawList& list, const ui::Canvas& canvas) { impl_->draw(list, canvas); }

std::string UiModernDevice::describe() const {
    const Impl& m = *impl_;
    std::string s = m.ready ? "ok" : "sem Direct2D";
    s += " fonte ";
    s += m.rajdhani != nullptr ? "Rajdhani" : "Bahnschrift";
    s += " (" + std::to_string(m.font_files) + " arquivos)";
    s += m.ctx5 != nullptr ? " svg" : " sem svg";
    if (!m.error.empty()) s += " erro: " + m.error;
    return s;
}

}  // namespace nf
