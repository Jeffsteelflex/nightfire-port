// O desenho vetorial do Windows (o Direct2D e o DirectWrite de hoje: render/ui_modern_draw_d2d.h e
// render/ui_hd_draw_d2d.h) por trás da interface do macOS (render/ui_vector.h), só para a ferramenta nf_vector_check: as
// imagens de referência da comparação (tests/data/vector) saem daqui. Cada desenho vai a uma textura D3D11 fora da tela,
// limpa para transparente, e volta como a imagem BGRA pré-multiplicada que o backend do macOS dá (o Direct2D mistura em
// cima do que há, então desenhar sobre o transparente e compor depois dá o mesmo que desenhar direto no quadro).
// O dispositivo é o de hardware com BGRA (como o do jogo); sem ele, o WARP.

#include <d2d1_1.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

#include "core/svg.h"
#include "render/ui_hd_draw_d2d.h"
#include "render/ui_modern_draw_d2d.h"
#include "render/ui_vector_util.h"
#include "vector_cases.h"

namespace nf::vcase {

namespace {

template <typename T>
using Com = Microsoft::WRL::ComPtr<T>;

class D2DCanvas final : public UiVectorCanvas {
public:
    D2DCanvas() {
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &dev_,
                                     nullptr, &ctx_)) &&
            FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &dev_,
                                     nullptr, &ctx_))) {
            return;
        }
        modern_ = std::make_unique<UiModernDevice>(dev_.Get(), ctx_.Get());
        hd_ = std::make_unique<UiHdDevice>(dev_.Get(), ctx_.Get());
    }

    bool ok() const override { return modern_ != nullptr && modern_->ok() && hd_ != nullptr && hd_->ok(); }

    void configure_modern(const std::wstring& dir) override {
        if (modern_ != nullptr) modern_->configure(dir);
    }
    float measure(const std::wstring& text, const ui::TextStyle& style) override {
        return modern_ != nullptr ? modern_->measure(text, style) : 0.0f;
    }
    void draw_modern(const ui::DrawList& list, const ui::Canvas& canvas, int w, int h, UiRaster& out) override {
        if (!ok() || !begin(w, h)) return;
        modern_->set_target(target_.Get());
        modern_->draw(list, canvas);
        modern_->set_target(nullptr);
        read(out);
    }
    std::string modern_describe() const override { return modern_ != nullptr ? modern_->describe() : "sem Direct3D"; }

    void configure_hd(const UiHdConfig* config, const std::wstring& dir) override {
        if (hd_ != nullptr) hd_->configure(config, dir);
    }
    bool asset(int index, int w, int h, std::vector<uint8_t>& rgba, int& out_w, int& out_h) override {
        if (!ok()) return false;
        ID3D11ShaderResourceView* srv = hd_->asset(index, w, h);
        if (srv == nullptr) return false;
        Com<ID3D11Resource> res;
        srv->GetResource(&res);
        Com<ID3D11Texture2D> tex;
        if (FAILED(res.As(&tex))) return false;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        D3D11_TEXTURE2D_DESC sd = d;
        sd.MipLevels = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        Com<ID3D11Texture2D> staging;
        if (FAILED(dev_->CreateTexture2D(&sd, nullptr, &staging))) return false;
        ctx_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex.Get(), 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
        out_w = int(d.Width);
        out_h = int(d.Height);
        rgba.resize(size_t(out_w) * size_t(out_h) * 4);
        for (int y = 0; y < out_h; ++y) {
            std::memcpy(rgba.data() + size_t(y) * size_t(out_w) * 4, static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch,
                        size_t(out_w) * 4);
        }
        ctx_->Unmap(staging.Get(), 0);
        return true;
    }
    void draw_hd(const std::vector<const UiHdText*>& texts, const std::vector<const UiHdKey*>& keys,
                 const std::vector<UiHdOp>& order, int w, int h, UiRaster& out) override {
        if (!ok() || !begin(w, h)) return;
        hd_->set_target(target_.Get());
        hd_->begin_text();
        for (const UiHdOp& op : order) {
            if (op.kind == UiHdOp::Text && op.index < texts.size()) hd_->draw_text(*texts[op.index]);
            if (op.kind == UiHdOp::Key && op.index < keys.size()) hd_->draw_key(*keys[op.index]);
        }
        hd_->end_text();
        hd_->set_target(nullptr);
        read(out);
    }
    float hd_measure(const std::wstring& label, float cap_px) override {
        return hd_ != nullptr ? hd_->measure(label, cap_px) : 0.0f;
    }
    void end_frame() override {
        if (hd_ != nullptr) hd_->end_frame();
    }
    std::string hd_describe() const override { return hd_ != nullptr ? hd_->describe() : "sem Direct3D"; }

    // O SVG lido pelo leitor do port e pintado com as primitivas do Direct2D (a conferência do leitor).
    bool parsed_svg(const std::string& xml, int w, int h, std::vector<uint8_t>& rgba) {
        svg::Document doc;
        std::string err;
        if (!ok() || !svg::parse(xml, doc, &err) || w <= 0 || h <= 0) return false;
        if (d2d_ == nullptr) {
            D2D1_FACTORY_OPTIONS fo{};
            Com<IDXGIDevice> dxgi;
            Com<ID2D1Device> device;
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                         reinterpret_cast<void**>(factory_.GetAddressOf()))) ||
                FAILED(dev_.As(&dxgi)) || FAILED(factory_->CreateDevice(dxgi.Get(), &device)) ||
                FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_))) {
                return false;
            }
            d2d_->SetDpi(96.0f, 96.0f);
            d2d_->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
        }
        const D2D1_PIXEL_FORMAT pf = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        Com<ID2D1Bitmap1> bmp, cpu;
        if (FAILED(d2d_->CreateBitmap(D2D1::SizeU(UINT(w), UINT(h)), nullptr, 0,
                                      D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, pf), &bmp)) ||
            FAILED(d2d_->CreateBitmap(D2D1::SizeU(UINT(w), UINT(h)), nullptr, 0,
                                      D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, pf),
                                      &cpu))) {
            return false;
        }
        d2d_->SetTarget(bmp.Get());
        d2d_->BeginDraw();
        d2d_->Clear(D2D1::ColorF(0, 0, 0, 0));
        const svg::Matrix vp = svg::viewport(doc, w, h);
        for (const svg::Shape& s : doc.shapes) {
            int layers = 0;
            for (const svg::ClipRef& c : s.clips) {
                if (c.clip < 0 || size_t(c.clip) >= doc.clips.size()) continue;
                const svg::ClipPath& cp = doc.clips[size_t(c.clip)];
                std::vector<const svg::Path*> paths;
                for (const svg::Path& p : cp.paths) paths.push_back(&p);
                Com<ID2D1PathGeometry> g = geometry(paths, !cp.evenodd.empty() && cp.evenodd[0] != 0);
                if (g == nullptr) continue;
                d2d_->SetTransform(D2D1::Matrix3x2F::Identity());
                d2d_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), g.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                       matrix(c.m.then(vp))),
                                nullptr);
                ++layers;
            }
            d2d_->SetTransform(matrix(s.m.then(vp)));
            Com<ID2D1PathGeometry> g = geometry({&s.path}, s.evenodd);
            const svg::Box box = svg::path_bounds(s.path, svg::Matrix{});
            if (g != nullptr && s.fill.kind != svg::Paint::None) {
                Com<ID2D1Brush> b = brush(doc, s.fill, s.fill_opacity, box);
                if (b != nullptr) d2d_->FillGeometry(g.Get(), b.Get());
            }
            if (g != nullptr && s.stroke.kind != svg::Paint::None) {
                Com<ID2D1Brush> b = brush(doc, s.stroke, s.stroke_opacity, box);
                const D2D1_CAP_STYLE cap = s.cap == svg::Cap::Round    ? D2D1_CAP_STYLE_ROUND
                                           : s.cap == svg::Cap::Square ? D2D1_CAP_STYLE_SQUARE
                                                                       : D2D1_CAP_STYLE_FLAT;
                const D2D1_LINE_JOIN join = s.join == svg::Join::Round   ? D2D1_LINE_JOIN_ROUND
                                            : s.join == svg::Join::Bevel ? D2D1_LINE_JOIN_BEVEL
                                                                         : D2D1_LINE_JOIN_MITER_OR_BEVEL;
                Com<ID2D1StrokeStyle> style;
                factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(cap, cap, cap, join, float(s.miter)), nullptr, 0, &style);
                if (b != nullptr) d2d_->DrawGeometry(g.Get(), b.Get(), float(s.stroke_width), style.Get());
            }
            d2d_->SetTransform(D2D1::Matrix3x2F::Identity());
            while (layers-- > 0) d2d_->PopLayer();
        }
        const HRESULT hr = d2d_->EndDraw();
        d2d_->SetTarget(nullptr);
        if (FAILED(hr) || FAILED(cpu->CopyFromBitmap(nullptr, bmp.Get(), nullptr))) return false;
        D2D1_MAPPED_RECT m;
        if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &m))) return false;
        ui_vec_to_gs_rgba(m.bits, m.pitch, w, h, true, rgba);
        cpu->Unmap();
        return true;
    }

private:
    Com<ID2D1Factory1> factory_;
    Com<ID2D1DeviceContext> d2d_;

    static D2D1_MATRIX_3X2_F matrix(const svg::Matrix& m) {
        return D2D1::Matrix3x2F(float(m.a), float(m.b), float(m.c), float(m.d), float(m.e), float(m.f));
    }
    static D2D1_POINT_2F pt(const svg::Point& p) { return D2D1::Point2F(float(p.x), float(p.y)); }

    Com<ID2D1PathGeometry> geometry(const std::vector<const svg::Path*>& paths, bool evenodd) {
        Com<ID2D1PathGeometry> g;
        Com<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return nullptr;
        sink->SetFillMode(evenodd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
        for (const svg::Path* path : paths) {
            bool open = false;
            svg::Point start{}, cur{};
            for (const svg::PathOp& op : *path) {
                if (op.kind == svg::PathOp::Move) {
                    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->BeginFigure(pt(op.p[0]), D2D1_FIGURE_BEGIN_FILLED);
                    open = true;
                    start = cur = op.p[0];
                    continue;
                }
                if (op.kind == svg::PathOp::Close) {
                    if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    open = false;
                    cur = start;
                    continue;
                }
                if (!open) {
                    sink->BeginFigure(pt(cur), D2D1_FIGURE_BEGIN_FILLED);
                    open = true;
                    start = cur;
                }
                if (op.kind == svg::PathOp::Line) {
                    sink->AddLine(pt(op.p[0]));
                    cur = op.p[0];
                } else {
                    sink->AddBezier(D2D1::BezierSegment(pt(op.p[0]), pt(op.p[1]), pt(op.p[2])));
                    cur = op.p[2];
                }
            }
            if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
        }
        sink->Close();
        return g;
    }

    Com<ID2D1Brush> brush(const svg::Document& doc, const svg::Paint& p, float opacity, const svg::Box& box) {
        if (p.kind == svg::Paint::Color) {
            Com<ID2D1SolidColorBrush> b;
            d2d_->CreateSolidColorBrush(D2D1::ColorF(p.r, p.g, p.b, opacity), &b);
            return b;
        }
        if (p.gradient < 0 || size_t(p.gradient) >= doc.gradients.size()) return nullptr;
        const svg::Gradient& g = doc.gradients[size_t(p.gradient)];
        std::vector<D2D1_GRADIENT_STOP> stops;
        for (const svg::Stop& s : g.stops) stops.push_back({float(s.offset), D2D1::ColorF(s.r, s.g, s.b, s.a)});
        if (stops.empty()) return nullptr;
        Com<ID2D1GradientStopCollection> coll;
        if (FAILED(d2d_->CreateGradientStopCollection(stops.data(), UINT32(stops.size()), D2D1_GAMMA_2_2,
                                                      D2D1_EXTEND_MODE_CLAMP, &coll))) {
            return nullptr;
        }
        const D2D1_BRUSH_PROPERTIES bp = D2D1::BrushProperties(opacity, matrix(svg::gradient_matrix(g, box)));
        if (!g.radial) {
            Com<ID2D1LinearGradientBrush> b;
            d2d_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(float(g.x1), float(g.y1)), D2D1::Point2F(float(g.x2), float(g.y2))),
                bp, coll.Get(), &b);
            return b;
        }
        Com<ID2D1RadialGradientBrush> b;
        d2d_->CreateRadialGradientBrush(
            D2D1::RadialGradientBrushProperties(D2D1::Point2F(float(g.cx), float(g.cy)),
                                                D2D1::Point2F(float(g.fx - g.cx), float(g.fy - g.cy)), float(g.r), float(g.r)),
            bp, coll.Get(), &b);
        return b;
    }

    Com<ID3D11Device> dev_;
    Com<ID3D11DeviceContext> ctx_;
    std::unique_ptr<UiModernDevice> modern_;
    std::unique_ptr<UiHdDevice> hd_;
    Com<ID3D11Texture2D> target_, staging_;
    Com<ID3D11RenderTargetView> rtv_;
    int tw_ = 0, th_ = 0;

    // A textura do tamanho pedido, limpa para transparente.
    bool begin(int w, int h) {
        if (w <= 0 || h <= 0) return false;
        if (w != tw_ || h != th_ || target_ == nullptr) {
            target_.Reset();
            staging_.Reset();
            rtv_.Reset();
            D3D11_TEXTURE2D_DESC d{};
            d.Width = UINT(w);
            d.Height = UINT(h);
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(dev_->CreateTexture2D(&d, nullptr, &target_))) return false;
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(dev_->CreateTexture2D(&d, nullptr, &staging_))) return false;
            if (FAILED(dev_->CreateRenderTargetView(target_.Get(), nullptr, &rtv_))) return false;
            tw_ = w;
            th_ = h;
        }
        const float clear[4] = {0, 0, 0, 0};
        ctx_->ClearRenderTargetView(rtv_.Get(), clear);
        return true;
    }

    void read(UiRaster& out) {
        ctx_->CopyResource(staging_.Get(), target_.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
        out.x = 0;
        out.y = 0;
        out.width = tw_;
        out.height = th_;
        out.bgra.resize(size_t(tw_) * size_t(th_) * 4);
        for (int y = 0; y < th_; ++y) {
            std::memcpy(out.bgra.data() + size_t(y) * size_t(tw_) * 4, static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch,
                        size_t(tw_) * 4);
        }
        ctx_->Unmap(staging_.Get(), 0);
    }
};

}  // namespace

std::unique_ptr<UiVectorCanvas> create_tool_canvas() { return std::make_unique<D2DCanvas>(); }

bool tool_parsed_svg(UiVectorCanvas& canvas, const std::string& xml, int w, int h, std::vector<uint8_t>& rgba) {
    D2DCanvas* d = dynamic_cast<D2DCanvas*>(&canvas);
    return d != nullptr && d->parsed_svg(xml, w, h, rgba);
}

}  // namespace nf::vcase
