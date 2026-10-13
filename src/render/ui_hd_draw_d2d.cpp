// Camada 2D em alta definição no Windows: Direct2D e DirectWrite (o backend vetorial; as imagens PNG pelo
// platform/image_io.h). Ver ui_hd_draw_d2d.h.

#include "render/ui_hd_draw_d2d.h"

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "platform/image_io.h"

namespace nf {

namespace {

template <typename T>
using Com = Microsoft::WRL::ComPtr<T>;

// Cor na escala do GS (0x80 = 1.0; a textura das fontes é branca) para o Direct2D.
D2D1_COLOR_F gs_colour(const uint8_t rgba[4]) {
    return D2D1::ColorF(std::min(1.0f, rgba[0] / 128.0f), std::min(1.0f, rgba[1] / 128.0f),
                        std::min(1.0f, rgba[2] / 128.0f), std::min(1.0f, rgba[3] / 128.0f));
}

std::vector<uint8_t> read_file(const std::wstring& path) {
    std::vector<uint8_t> data;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) return data;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0 && size < (64 << 20)) {
        data.resize(size_t(size));
        if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
    }
    std::fclose(f);
    return data;
}

std::wstring widen_path(const std::string& s) {
    std::wstring w;
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    w.resize(size_t(n));
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    for (wchar_t& c : w) {
        if (c == L'/') c = L'\\';
    }
    return w;
}

// BGRA com alfa pré-multiplicado (Direct2D) ou não (WIC, PNG) para RGBA com a cor sem o alfa e o alfa na
// escala do GS (0x80 = opaco), como as texturas do jogo. Os texels transparentes ganham a cor do vizinho
// visível (o filtro bilinear não puxa preto para as bordas).
void to_gs_rgba(const uint8_t* src, uint32_t pitch, int w, int h, bool premultiplied, std::vector<uint8_t>& out) {
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

// Renderizador de glifos do DirectWrite: preenche com o pincel do trecho (SetDrawingEffect) ou com um
// pincel único (cópias de sombra), ou traça o contorno dos glifos (o efeito Stroke).
class GlyphRenderer final : public IDWriteTextRenderer {
public:
    ID2D1DeviceContext* ctx = nullptr;
    ID2D1Factory1* factory = nullptr;
    ID2D1Brush* fallback = nullptr;
    ID2D1Brush* override_brush = nullptr;
    ID2D1StrokeStyle* stroke_style = nullptr;
    float stroke = 0.0f;  // > 0: traça com essa largura

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) || riid == __uuidof(IDWriteTextRenderer)) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        *disabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) override {
        ctx->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F*>(m));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* ppd) override {
        *ppd = 1.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode, const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown* effect) override {
        Com<ID2D1Brush> own;
        ID2D1Brush* brush = override_brush;
        if (brush == nullptr && effect != nullptr && SUCCEEDED(effect->QueryInterface(IID_PPV_ARGS(&own)))) brush = own.Get();
        if (brush == nullptr) brush = fallback;
        if (stroke <= 0.0f) {
            ctx->DrawGlyphRun(D2D1::Point2F(x, y), run, brush, mode);
            return S_OK;
        }
        Com<ID2D1PathGeometry> geometry;
        Com<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(&geometry)) || FAILED(geometry->Open(&sink))) return S_OK;
        const HRESULT hr = run->fontFace->GetGlyphRunOutline(run->fontEmSize, run->glyphIndices, run->glyphAdvances,
                                                             run->glyphOffsets, run->glyphCount, run->isSideways,
                                                             (run->bidiLevel & 1) != 0, sink.Get());
        sink->Close();
        if (FAILED(hr)) return S_OK;
        D2D1_MATRIX_3X2_F old;
        ctx->GetTransform(&old);
        ctx->SetTransform(D2D1::Matrix3x2F::Translation(x, y) * old);
        ctx->DrawGeometry(geometry.Get(), brush, stroke, stroke_style);
        ctx->SetTransform(old);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override {
        return S_OK;
    }
};

}  // namespace

struct UiHdDevice::Impl {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> d3d;
    Com<ID2D1Factory1> factory;
    Com<ID2D1Device> d2d;
    Com<ID2D1DeviceContext> ctx;
    Com<ID2D1DeviceContext5> ctx5;  // SVG
    Com<IDWriteFactory> dwrite;
    Com<IDWriteFontCollection> collection;  // as do usuário (hd_ui/fonts) e as do sistema
    Com<ID2D1Bitmap1> target;
    Com<ID2D1StrokeStyle> stroke_style;
    Com<ID2D1SolidColorBrush> white;
    GlyphRenderer renderer;
    const UiHdConfig* config = nullptr;
    std::wstring dir;
    std::string error;
    int user_fonts = 0;
    bool drawing = false;

    struct Font {
        std::wstring family;  // a encontrada (pode ser a reserva)
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
        DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
        float cap_ratio = 0.7f;  // capHeight / designUnitsPerEm
        bool found = false;
    };
    Font fonts[4];
    std::map<uint64_t, Com<IDWriteTextFormat>> formats;    // (fonte, tamanho x 64)
    std::map<uint32_t, Com<ID2D1SolidColorBrush>> brushes;  // rgba

    struct Asset {
        int index = -1, w = 0, h = 0;
        Com<ID3D11ShaderResourceView> srv;
        uint64_t used = 0;
    };
    std::vector<Asset> assets;
    std::vector<Com<ID2D1SvgDocument>> svgs;
    std::vector<uint8_t> failed;
    uint64_t frame = 0;

    bool init(ID3D11Device* dev, ID3D11DeviceContext* context) {
        device = dev;
        d3d = context;
        D2D1_FACTORY_OPTIONS fo{};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                     reinterpret_cast<void**>(factory.GetAddressOf())))) {
            error = "D2D1CreateFactory falhou";
            return false;
        }
        Com<IDXGIDevice> dxgi;
        if (FAILED(device.As(&dxgi)) || FAILED(factory->CreateDevice(dxgi.Get(), &d2d)) ||
            FAILED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx))) {
            error = "Direct2D no dispositivo D3D11 falhou (falta D3D11_CREATE_DEVICE_BGRA_SUPPORT?)";
            return false;
        }
        ctx.As(&ctx5);  // nulo antes do Windows 10 1703: sem SVG
        ctx->SetDpi(96.0f, 96.0f);
        ctx->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
        ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dwrite.GetAddressOf())))) {
            error = "DWriteCreateFactory falhou";
            return false;
        }
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // o COM da thread (as imagens do platform/image_io.h usam o WIC)
        const D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        factory->CreateStrokeStyle(sp, nullptr, 0, &stroke_style);
        ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &white);
        renderer.ctx = ctx.Get();
        renderer.factory = factory.Get();
        renderer.stroke_style = stroke_style.Get();
        renderer.fallback = white.Get();
        load_fonts();
        return true;
    }

    // As fontes da pasta hd_ui/fonts antes das do sistema; sem a pasta (ou sem IDWriteFactory5), só as do
    // sistema.
    void load_fonts() {
        collection.Reset();
        user_fonts = 0;
        Com<IDWriteFactory5> f5;
        if (!dir.empty() && SUCCEEDED(dwrite.As(&f5))) {
            Com<IDWriteFontSetBuilder1> builder;
            if (SUCCEEDED(f5->CreateFontSetBuilder(&builder))) {
                const std::wstring folder = dir + L"\\fonts\\";
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((folder + L"*").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        const std::wstring name = fd.cFileName;
                        const size_t dot = name.find_last_of(L'.');
                        if (dot == std::wstring::npos) continue;
                        std::wstring ext = name.substr(dot);
                        for (wchar_t& c : ext) c = wchar_t(towlower(c));
                        if (ext != L".ttf" && ext != L".otf" && ext != L".ttc") continue;
                        Com<IDWriteFontFile> file;
                        if (SUCCEEDED(f5->CreateFontFileReference((folder + name).c_str(), nullptr, &file)) &&
                            SUCCEEDED(builder->AddFontFile(file.Get()))) {
                            ++user_fonts;
                        }
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
                Com<IDWriteFontSet> system;
                Com<IDWriteFontSet> set;
                Com<IDWriteFontCollection1> c1;
                if (user_fonts > 0 && SUCCEEDED(f5->GetSystemFontSet(&system)) && SUCCEEDED(builder->AddFontSet(system.Get())) &&
                    SUCCEEDED(builder->CreateFontSet(&set)) && SUCCEEDED(f5->CreateFontCollectionFromFontSet(set.Get(), &c1))) {
                    collection = c1;
                }
            }
        }
        if (collection == nullptr) dwrite->GetSystemFontCollection(&collection);
        formats.clear();
        for (int k = 1; k <= 3; ++k) resolve_font(k);
    }

    void resolve_font(int k) {
        Font& f = fonts[k];
        f = Font{};
        const UiHdFont def;
        const UiHdFont& cfg = config != nullptr ? config->fonts[k] : def;
        f.weight = DWRITE_FONT_WEIGHT(cfg.weight);
        f.style = cfg.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
        f.stretch = DWRITE_FONT_STRETCH(cfg.stretch);
        const wchar_t* candidates[3] = {cfg.family.c_str(), L"Bahnschrift", L"Segoe UI"};
        for (const wchar_t* name : candidates) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (collection == nullptr || FAILED(collection->FindFamilyName(name, &index, &exists)) || !exists) continue;
            Com<IDWriteFontFamily> family;
            Com<IDWriteFont> font;
            if (FAILED(collection->GetFontFamily(index, &family)) ||
                FAILED(family->GetFirstMatchingFont(f.weight, f.stretch, f.style, &font))) {
                continue;
            }
            DWRITE_FONT_METRICS m{};
            font->GetMetrics(&m);
            if (m.designUnitsPerEm != 0 && m.capHeight != 0) f.cap_ratio = float(m.capHeight) / float(m.designUnitsPerEm);
            f.family = name;
            f.found = true;
            break;
        }
        if (!f.found) f.family = L"Segoe UI";
    }

    IDWriteTextFormat* format(int font, float em) {
        const uint64_t key = uint64_t(font) << 32 | uint32_t(std::lround(em * 64.0f));
        auto it = formats.find(key);
        if (it != formats.end()) return it->second.Get();
        const Font& f = fonts[font];
        Com<IDWriteTextFormat> fmt;
        if (FAILED(dwrite->CreateTextFormat(f.family.c_str(), collection.Get(), f.weight, f.style, f.stretch,
                                            std::max(1.0f, float(std::lround(em * 64.0f)) / 64.0f), L"en-us", &fmt))) {
            return nullptr;
        }
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (formats.size() > 256) formats.clear();
        formats[key] = fmt;
        return fmt.Get();
    }

    ID2D1SolidColorBrush* brush(const uint8_t rgba[4]) {
        uint32_t key;
        std::memcpy(&key, rgba, 4);
        auto it = brushes.find(key);
        if (it != brushes.end()) return it->second.Get();
        Com<ID2D1SolidColorBrush> b;
        if (FAILED(ctx->CreateSolidColorBrush(gs_colour(rgba), &b))) return white.Get();
        if (brushes.size() > 512) brushes.clear();
        brushes[key] = b;
        return b.Get();
    }

    ID3D11ShaderResourceView* make_texture(const std::vector<uint8_t>& rgba, int w, int h, bool mips) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(w);
        td.Height = UINT(h);
        td.MipLevels = mips ? 0 : 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (mips ? D3D11_BIND_RENDER_TARGET : 0);
        td.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        td.Usage = mips ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
        Com<ID3D11Texture2D> tex;
        D3D11_SUBRESOURCE_DATA init{rgba.data(), UINT(w * 4), 0};
        if (FAILED(device->CreateTexture2D(&td, mips ? nullptr : &init, &tex))) return nullptr;
        if (mips) d3d->UpdateSubresource(tex.Get(), 0, nullptr, rgba.data(), UINT(w * 4), 0);
        Com<ID3D11ShaderResourceView> srv;
        if (FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return nullptr;
        if (mips) d3d->GenerateMips(srv.Get());
        Asset a;
        a.srv = srv;
        assets.push_back(a);
        return srv.Get();
    }

    ID3D11ShaderResourceView* rasterize_svg(int index, int w, int h) {
        if (ctx5 == nullptr) return nullptr;
        if (svgs[size_t(index)] == nullptr) {
            const std::vector<uint8_t> data = read_file(dir + L"\\" + widen_path(config->images[size_t(index)].file));
            if (data.empty()) return nullptr;
            Com<IStream> stream;
            stream.Attach(SHCreateMemStream(data.data(), UINT(data.size())));
            if (stream == nullptr ||
                FAILED(ctx5->CreateSvgDocument(stream.Get(), D2D1::SizeF(float(w), float(h)), &svgs[size_t(index)]))) {
                return nullptr;
            }
        }
        ID2D1SvgDocument* doc = svgs[size_t(index)].Get();
        doc->SetViewportSize(D2D1::SizeF(float(w), float(h)));
        const D2D1_PIXEL_FORMAT pf = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        Com<ID2D1Bitmap1> bmp, cpu;
        if (FAILED(ctx->CreateBitmap(D2D1::SizeU(UINT(w), UINT(h)), nullptr, 0,
                                     D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, pf), &bmp)) ||
            FAILED(ctx->CreateBitmap(D2D1::SizeU(UINT(w), UINT(h)), nullptr, 0,
                                     D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, pf),
                                     &cpu))) {
            return nullptr;
        }
        Com<ID2D1Image> old;
        ctx->GetTarget(&old);
        ctx->SetTarget(bmp.Get());
        ctx->BeginDraw();
        ctx->SetTransform(D2D1::Matrix3x2F::Identity());
        ctx->Clear(D2D1::ColorF(0, 0, 0, 0));
        ctx5->DrawSvgDocument(doc);
        const HRESULT hr = ctx->EndDraw();
        ctx->SetTarget(old.Get());
        if (FAILED(hr) || FAILED(cpu->CopyFromBitmap(nullptr, bmp.Get(), nullptr))) return nullptr;
        D2D1_MAPPED_RECT m;
        if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &m))) return nullptr;
        std::vector<uint8_t> rgba;
        to_gs_rgba(m.bits, m.pitch, w, h, true, rgba);
        cpu->Unmap();
        dump(index, w, h, rgba);
        return make_texture(rgba, w, h, false);
    }

    // Depuração (variável de ambiente NF_UI_HD_DUMP=<pasta>): cada rasterização vira um PNG, com o alfa do
    // GS de volta a 0..255.
    void dump(int index, int w, int h, const std::vector<uint8_t>& rgba) {
        wchar_t folder[MAX_PATH];
        if (GetEnvironmentVariableW(L"NF_UI_HD_DUMP", folder, MAX_PATH) == 0) return;
        std::vector<uint8_t> px = rgba;
        for (size_t i = 3; i < px.size(); i += 4) px[i] = uint8_t(std::min(255, px[i] * 255 / 128));
        wchar_t name[64];
        swprintf(name, 64, L"/asset%02d_%dx%d.png", index, w, h);
        write_png_rgba(std::wstring(folder) + name, w, h, px.data());
    }

    ID3D11ShaderResourceView* load_png(int index) {
        const std::wstring path = dir + L"\\" + widen_path(config->images[size_t(index)].file);
        int w = 0, h = 0;
        std::vector<uint8_t> bgra;
        if (!read_image_bgra(path, 8192, &w, &h, &bgra)) return nullptr;
        std::vector<uint8_t> rgba;
        to_gs_rgba(bgra.data(), UINT(w) * 4, w, h, false, rgba);
        return make_texture(rgba, w, h, true);
    }
};

UiHdDevice::UiHdDevice(ID3D11Device* device, ID3D11DeviceContext* context) : impl_(std::make_unique<Impl>()) {
    if (!impl_->init(device, context)) impl_->ctx.Reset();
}

UiHdDevice::~UiHdDevice() = default;

bool UiHdDevice::ok() const { return impl_->ctx != nullptr; }

void UiHdDevice::configure(const UiHdConfig* config, const std::wstring& dir) {
    Impl& m = *impl_;
    m.config = config;
    m.dir = dir;
    m.assets.clear();
    m.svgs.assign(config != nullptr ? config->images.size() : 0, nullptr);
    m.failed.assign(m.svgs.size(), 0);
    if (ok()) m.load_fonts();
}

void UiHdDevice::set_target(ID3D11Texture2D* target) {
    Impl& m = *impl_;
    m.target.Reset();
    if (!ok()) return;
    m.ctx->SetTarget(nullptr);
    if (target == nullptr) return;
    Com<IDXGISurface> surface;
    if (FAILED(target->QueryInterface(IID_PPV_ARGS(&surface)))) return;
    const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(m.ctx->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &m.target))) m.error = "alvo do Direct2D falhou";
}

ID3D11ShaderResourceView* UiHdDevice::asset(int index, int w, int h) {
    Impl& m = *impl_;
    if (!ok() || m.config == nullptr || index < 0 || size_t(index) >= m.config->images.size() || m.drawing) return nullptr;
    if (m.failed[size_t(index)] != 0) return nullptr;
    const bool png = m.config->images[size_t(index)].type == UiHdAssetType::Png;
    if (png) w = h = 0;
    w = std::clamp(w, png ? 0 : 1, 4096);
    h = std::clamp(h, png ? 0 : 1, 4096);
    for (Impl::Asset& a : m.assets) {
        if (a.index == index && a.w == w && a.h == h) {
            a.used = m.frame;
            return a.srv.Get();
        }
    }
    ID3D11ShaderResourceView* srv = png ? m.load_png(index) : m.rasterize_svg(index, w, h);
    if (srv == nullptr) {
        m.failed[size_t(index)] = 1;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "substituta %s não carregou", m.config->images[size_t(index)].file.c_str());
        m.error = buf;
        return nullptr;
    }
    Impl::Asset& a = m.assets.back();
    a.index = index;
    a.w = w;
    a.h = h;
    a.used = m.frame;
    return srv;
}

void UiHdDevice::begin_text() {
    Impl& m = *impl_;
    if (!ok() || m.target == nullptr || m.drawing) return;
    m.ctx->SetTarget(m.target.Get());
    m.ctx->BeginDraw();
    m.ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    m.drawing = true;
}

void UiHdDevice::draw_text(const UiHdText& t) {
    Impl& m = *impl_;
    if (!m.drawing || t.lines.empty()) return;
    const int font = t.font >= 1 && t.font <= 3 ? t.font : 2;
    const UiHdFont def;
    const UiHdFont& cfg = m.config != nullptr ? m.config->fonts[font] : def;
    const Impl::Font& f = m.fonts[font];
    const float em = std::max(1.0f, t.cap_px / std::max(0.2f, f.cap_ratio) * cfg.size);
    IDWriteTextFormat* fmt = m.format(font, em);
    if (fmt == nullptr) return;

    struct Laid {
        Com<IDWriteTextLayout> layout;
        float width = 0, baseline = 0;
        float x = 0, sx = 1;
    };
    std::vector<Laid> laid(t.lines.size());
    float wo = 0.0f, wh = 0.0f, left = 1e9f, right = -1e9f;
    for (size_t i = 0; i < t.lines.size(); ++i) {
        const UiHdLine& line = t.lines[i];
        Laid& l = laid[i];
        if (FAILED(m.dwrite->CreateTextLayout(line.text.c_str(), UINT32(line.text.size()), fmt, 100000.0f, 100000.0f,
                                              &l.layout))) {
            continue;
        }
        for (const UiHdSpan& s : line.spans) l.layout->SetDrawingEffect(m.brush(s.rgba), {s.start, s.length});
        if (cfg.spacing != 0.0f) {
            Com<IDWriteTextLayout1> l1;
            if (SUCCEEDED(l.layout.As(&l1))) {
                // Depois de cada caractere menos o último: a borda esquerda não muda.
                if (line.text.size() > 1) {
                    l1->SetCharacterSpacing(0.0f, cfg.spacing * em, 0.0f, {0, UINT32(line.text.size() - 1)});
                }
            }
        }
        DWRITE_TEXT_METRICS tm{};
        l.layout->GetMetrics(&tm);
        l.width = tm.width;
        DWRITE_LINE_METRICS lm{};
        UINT32 n = 0;
        if (SUCCEEDED(l.layout->GetLineMetrics(&lm, 1, &n)) || n >= 1) l.baseline = lm.baseline;
        if (line.pinned) {
            const float wo_line = line.x1 - line.x0;
            l.sx = l.width > wo_line && l.width > 0.0f ? std::max(cfg.fit_min, wo_line / l.width) : 1.0f;
            l.x = line.x0;
        } else {
            wo = std::max(wo, line.x1 - line.x0);
            wh = std::max(wh, l.width);
            left = std::min(left, line.x0);
            right = std::max(right, line.x1);
        }
    }
    // O bloco (as linhas do jogo começam todas na mesma caneta; o alinhamento move o bloco inteiro): com a
    // largura da fonte vetorial, comprimida até a original se passar dela.
    const float sx = wh > wo && wh > 0.0f ? std::max(cfg.fit_min, wo / wh) : 1.0f;
    float block = left;
    if (t.align & 2) block = (left + right) * 0.5f - wh * sx * 0.5f;
    else if (t.align & 4) block = right - wh * sx;
    for (size_t i = 0; i < t.lines.size(); ++i) {
        if (t.lines[i].pinned) continue;
        laid[i].sx = sx;
        laid[i].x = block + (t.lines[i].x0 - left) * sx;
    }

    m.ctx->SetPrimitiveBlend(t.alpha == 0x48 ? D2D1_PRIMITIVE_BLEND_ADD : D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    m.ctx->PushAxisAlignedClip(D2D1::RectF(t.clip[0], t.clip[1], t.clip[2], t.clip[3]), D2D1_ANTIALIAS_MODE_ALIASED);
    const auto draw_all = [&](float dx, float dy) {
        for (size_t i = 0; i < t.lines.size(); ++i) {
            const Laid& l = laid[i];
            if (l.layout == nullptr) continue;
            const float top = t.lines[i].baseline - l.baseline;
            m.ctx->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(l.sx, 1.0f), D2D1::Point2F(l.x, 0.0f)) *
                                D2D1::Matrix3x2F::Translation(dx, dy));
            l.layout->Draw(nullptr, &m.renderer, l.x, top);
        }
    };
    for (const UiHdEffect& e : t.effects) {
        m.renderer.override_brush = m.brush(e.rgba);
        m.renderer.stroke = e.kind == UiHdEffect::Stroke ? std::max(0.5f, e.width) : 0.0f;
        draw_all(e.dx, e.dy);
    }
    m.renderer.override_brush = nullptr;
    m.renderer.stroke = 0.0f;
    draw_all(0.0f, 0.0f);
    m.ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    m.ctx->PopAxisAlignedClip();
    m.ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}

// Uma tecla do teclado (símbolo de botão com prompts = keyboard): tecla arredondada escura com borda clara
// e o texto da tecla (a fonte 2 do map.json, com a reserva do DirectWrite para as setas) centrado.
void UiHdDevice::draw_key(const UiHdKey& k) {
    Impl& m = *impl_;
    if (!m.drawing || k.label.empty()) return;
    const float w = k.x1 - k.x0, h = k.y1 - k.y0;
    if (!(w > 1.0f) || !(h > 1.0f)) return;
    const float a = std::min(1.0f, k.rgba[3] / 128.0f);
    m.ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    m.ctx->PushAxisAlignedClip(D2D1::RectF(k.clip[0], k.clip[1], k.clip[2], k.clip[3]), D2D1_ANTIALIAS_MODE_ALIASED);
    const float line = std::max(1.0f, h * 0.06f);
    const float r = h * 0.22f;
    const D2D1_ROUNDED_RECT body = D2D1::RoundedRect(D2D1::RectF(k.x0 + line * 0.5f, k.y0 + line * 0.5f, k.x1 - line * 0.5f,
                                                                 k.y1 - line * 0.5f), r, r);
    const D2D1_GRADIENT_STOP stops[3] = {{0.0f, D2D1::ColorF(0.36f, 0.36f, 0.38f, a)},
                                         {0.55f, D2D1::ColorF(0.16f, 0.16f, 0.17f, a)},
                                         {1.0f, D2D1::ColorF(0.08f, 0.08f, 0.09f, a)}};
    Com<ID2D1GradientStopCollection> coll;
    Com<ID2D1LinearGradientBrush> fill;
    if (SUCCEEDED(m.ctx->CreateGradientStopCollection(stops, 3, &coll)) &&
        SUCCEEDED(m.ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, k.y0),
                                                                                       D2D1::Point2F(0.0f, k.y1)),
                                                   coll.Get(), &fill))) {
        m.ctx->FillRoundedRectangle(body, fill.Get());
    }
    const uint8_t edge[4] = {0x6e, 0x6e, 0x6c, uint8_t(std::lround(a * 0.9f * 128.0f))};
    m.ctx->DrawRoundedRectangle(body, m.brush(edge), line);
    // O texto: a altura das maiúsculas pedida, centrado na tecla.
    const UiHdFont def;
    const UiHdFont& cfg = m.config != nullptr ? m.config->fonts[2] : def;
    const Impl::Font& f = m.fonts[2];
    const float em = std::max(1.0f, k.cap_px / std::max(0.2f, f.cap_ratio) * cfg.size);
    IDWriteTextFormat* fmt = m.format(2, em);
    Com<IDWriteTextLayout> layout;
    if (fmt != nullptr && SUCCEEDED(m.dwrite->CreateTextLayout(k.label.c_str(), UINT32(k.label.size()), fmt, 100000.0f,
                                                               100000.0f, &layout))) {
        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);
        DWRITE_LINE_METRICS lm{};
        UINT32 n = 0;
        layout->GetLineMetrics(&lm, 1, &n);
        const float sx = tm.width > w - 2.0f * line && tm.width > 0.0f ? (w - 2.0f * line) / tm.width : 1.0f;
        const float cx = (k.x0 + k.x1) * 0.5f;
        const float baseline = (k.y0 + k.y1) * 0.5f + k.cap_px * 0.5f;
        const float x = cx - tm.width * sx * 0.5f;
        m.ctx->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(sx, 1.0f), D2D1::Point2F(x, 0.0f)));
        const uint8_t ink[4] = {0x7e, 0x7e, 0x7c, uint8_t(std::lround(a * 128.0f))};
        m.renderer.override_brush = m.brush(ink);
        m.renderer.stroke = 0.0f;
        layout->Draw(nullptr, &m.renderer, x, baseline - lm.baseline);
        m.renderer.override_brush = nullptr;
        m.ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    m.ctx->PopAxisAlignedClip();
}

float UiHdDevice::measure(const std::wstring& label, float cap_px) {
    Impl& m = *impl_;
    if (!ok() || label.empty()) return 0.0f;
    const UiHdFont def;
    const UiHdFont& cfg = m.config != nullptr ? m.config->fonts[2] : def;
    const Impl::Font& f = m.fonts[2];
    const float em = std::max(1.0f, cap_px / std::max(0.2f, f.cap_ratio) * cfg.size);
    IDWriteTextFormat* fmt = m.format(2, em);
    Com<IDWriteTextLayout> layout;
    if (fmt == nullptr ||
        FAILED(m.dwrite->CreateTextLayout(label.c_str(), UINT32(label.size()), fmt, 100000.0f, 100000.0f, &layout))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    return tm.width;
}

void UiHdDevice::end_text() {
    Impl& m = *impl_;
    if (!m.drawing) return;
    m.drawing = false;
    if (FAILED(m.ctx->EndDraw())) m.error = "EndDraw do Direct2D falhou";
    m.ctx->SetTarget(nullptr);
}

void UiHdDevice::end_frame() {
    Impl& m = *impl_;
    ++m.frame;
    // Rasterizações sem uso há uns 5 s (a 60 quadros) saem do cache.
    m.assets.erase(std::remove_if(m.assets.begin(), m.assets.end(),
                                  [&](const Impl::Asset& a) { return a.used + 300 < m.frame; }),
                   m.assets.end());
}

std::string UiHdDevice::describe() const {
    const Impl& m = *impl_;
    std::string s = ok() ? "ok" : "sem Direct2D";
    s += m.ctx5 != nullptr ? ", svg" : ", sem svg";
    s += ", fontes do usuário " + std::to_string(m.user_fonts);
    for (int k = 1; k <= 3; ++k) {
        char buf[160];
        const Impl::Font& f = m.fonts[k];
        std::string fam;
        for (wchar_t c : f.family) fam.push_back(c < 128 ? char(c) : '?');
        std::snprintf(buf, sizeof(buf), ", fonte %d %s %d%s%s (maiúsculas %.3f em)", k, fam.c_str(), int(f.weight),
                      f.style != DWRITE_FONT_STYLE_NORMAL ? " itálico" : "", f.found ? "" : " (reserva)", double(f.cap_ratio));
        s += buf;
    }
    if (!m.error.empty()) s += ", erro: " + m.error;
    return s;
}

}  // namespace nf
