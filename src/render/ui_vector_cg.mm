// O desenho vetorial do macOS (render/ui_vector.h; docs/port.md, "macOS: o desenho vetorial"): os menus Modern e a camada
// 2D em alta definição com CoreGraphics e CoreText, rasterizados na CPU num CGBitmapContext BGRA com o alfa
// pré-multiplicado, que o renderizador (o Metal) envia como textura e compõe por cima do quadro. A especificação é o que o
// Direct2D e o DirectWrite fazem no Windows (render/ui_modern_draw_d2d.cpp, render/ui_hd_draw_d2d.cpp): a mesma geometria
// presa ao pixel, os mesmos degradês, as mesmas posições de texto. Só C++ sobre as APIs em C do CoreGraphics e do
// CoreText (nada de Objective-C; o arquivo é .mm como os outros backends do macOS).
//
// O mapa (Direct2D/DirectWrite -> CoreGraphics/CoreText):
//   - o alvo do Direct3D 11 -> um CGBitmapContext (kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little: B G R
//     A pré-multiplicado na memória), no espaço DeviceRGB sem gerenciamento de cor (as cores e os degradês nos mesmos
//     valores do Direct2D), a matriz virada (y para baixo, as coordenadas da janela);
//   - FillRectangle/FillRoundedRectangle/FillEllipse/DrawLine/DrawRectangle/DrawGeometry -> caminhos CGPath (os arcos em
//     cúbicas, svg::append_arc), CGContextFillPath/StrokePath, com as mesmas regras de pixel (render/ui_vector_util.h);
//   - os pincéis de degradê linear e radial (D2D1_GAMMA_2_2, a interpolação nos valores sRGB sem pré-multiplicar, como
//     a sonda das referências mostrou) -> CGGradient com CGContextDrawLinearGradient/DrawRadialGradient (o radial
//     elíptico pela escala da matriz), recortados ao retângulo ou à forma;
//   - PushAxisAlignedClip (aliased) -> CGContextClipToRect com as bordas nos pixels inteiros; PushLayer com opacidade ->
//     CGContextBeginTransparencyLayer com CGContextSetAlpha; D2D1_PRIMITIVE_BLEND_ADD -> uma camada à parte somada só na
//     cor (o alfa do quadro fica), que com ONE, ONE_MINUS_SRC_ALPHA dá a soma;
//   - IDWriteTextLayout -> CTLine/CTTypesetter com kCTKernAttributeName (o espaçamento entre letras, depois de cada
//     caractere, como o SetCharacterSpacing), as quebras de linha do CTTypesetter, a linha de base presa ao pixel (o
//     Direct2D prende o texto na vertical); os glifos por CTFontDrawGlyphs; o contorno de 1 pixel do PS2 pelo caminho dos
//     glifos (CTFontCreatePathForGlyph) e um traço redondo, como o GetGlyphRunOutline;
//   - a coleção privada da Rajdhani -> CTFontManagerCreateFontDescriptorsFromURL (nada instalado nem registrado), o peso
//     de cada arquivo pela tabela OS/2; as fontes do usuário (hd_ui/fonts) do mesmo jeito; as do sistema pela família
//     (CTFontDescriptorCreateMatchingFontDescriptors) com a escolha de peso, largura e estilo do DirectWrite (a regra do
//     CSS); o itálico que a família não tem é inclinado pela matriz da fonte, como a simulação do DirectWrite;
//   - ID2D1SvgDocument -> o leitor de SVG do port (core/svg.h) pintado aqui (os recortes, os degradês, a opacidade);
//   - WIC -> platform/image_io.h (o ImageIO no macOS).
// Os menus redesenham a cada quadro: a imagem fica guardada e só é rasterizada de novo quando a lista muda (o resumo de
// ui_vec_list_hash); o mesmo vale para os lotes da camada HD.

#include "render/ui_vector.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include "core/svg.h"
#include "platform/image_io.h"
#include "platform/os.h"
#include "render/ui_vector_util.h"

namespace nf {

namespace {

constexpr double kPi = 3.14159265358979323846;
// A inclinação do itálico simulado (o oblíquo do DirectWrite, medido nas referências: ~0,24 de deslocamento por unidade
// de altura).
constexpr double kObliqueShear = 0.24;

// ---------------------------------------------------------------- CoreFoundation

// Uma referência do CoreFoundation que se solta sozinha.
template <class T>
class Ref {
public:
    Ref() = default;
    explicit Ref(T r) : r_(r) {}
    ~Ref() { reset(); }
    Ref(const Ref& o) : r_(o.r_) {
        if (r_ != nullptr) CFRetain(r_);
    }
    Ref& operator=(const Ref& o) {
        if (this != &o) {
            reset();
            r_ = o.r_;
            if (r_ != nullptr) CFRetain(r_);
        }
        return *this;
    }
    Ref(Ref&& o) noexcept : r_(o.r_) { o.r_ = nullptr; }
    Ref& operator=(Ref&& o) noexcept {
        if (this != &o) {
            reset();
            r_ = o.r_;
            o.r_ = nullptr;
        }
        return *this;
    }
    void reset(T r = nullptr) {
        if (r_ != nullptr) CFRelease(r_);
        r_ = r;
    }
    T get() const { return r_; }
    explicit operator bool() const { return r_ != nullptr; }

private:
    T r_ = nullptr;
};

// Um texto do port (std::wstring: UTF-32 fora do Windows) como CFString.
Ref<CFStringRef> cf_string(const std::wstring& s) {
    static_assert(sizeof(wchar_t) == 4, "fora do Windows o wchar_t é UTF-32");
    return Ref<CFStringRef>(CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(s.data()),
                                                    CFIndex(s.size() * sizeof(wchar_t)), kCFStringEncodingUTF32LE, false));
}

std::string to_ascii(const std::wstring& s) {
    std::string out;
    for (const wchar_t c : s) out.push_back(c < 128 ? char(c) : '?');
    return out;
}

std::string cf_ascii(CFStringRef s) {
    if (s == nullptr) return {};
    char buf[256];
    if (!CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) return {};
    return buf;
}

std::vector<uint8_t> read_file(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string s = ss.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

// ---------------------------------------------------------------- as fontes

// Uma face: o descritor e o que a escolha do DirectWrite olha (o peso, a largura e o itálico da tabela OS/2).
struct FaceInfo {
    Ref<CTFontDescriptorRef> desc;
    std::string family;
    int weight = 400;
    int stretch = 5;
    bool italic = false;
};

FaceInfo face_info(CTFontDescriptorRef d) {
    FaceInfo f;
    CFRetain(d);
    f.desc.reset(d);
    Ref<CTFontRef> font(CTFontCreateWithFontDescriptor(d, 12.0, nullptr));
    if (!font) return f;
    Ref<CFStringRef> fam(CTFontCopyFamilyName(font.get()));
    f.family = cf_ascii(fam.get());
    Ref<CFDataRef> os2(CTFontCopyTable(font.get(), kCTFontTableOS2, kCTFontTableOptionNoOptions));
    if (os2 && CFDataGetLength(os2.get()) >= 64) {
        const UInt8* b = CFDataGetBytePtr(os2.get());
        f.weight = std::clamp(int(b[4]) << 8 | b[5], 1, 999);
        f.stretch = std::clamp(int(b[6]) << 8 | b[7], 1, 9);
        const int sel = int(b[62]) << 8 | b[63];
        f.italic = (sel & 0x201) != 0;  // italic ou oblique
    } else {
        f.italic = (CTFontGetSymbolicTraits(font.get()) & kCTFontTraitItalic) != 0;
        f.weight = (CTFontGetSymbolicTraits(font.get()) & kCTFontTraitBold) != 0 ? 700 : 400;
    }
    return f;
}

// As faces dos arquivos de uma pasta (.ttf, .otf, .ttc), sem instalar nem registrar nada.
std::vector<FaceInfo> faces_from_folder(const std::wstring& folder, bool ttf_only, int* files) {
    std::vector<FaceInfo> out;
    std::error_code ec;
    std::vector<std::filesystem::path> paths;
    for (const auto& e : std::filesystem::directory_iterator(std::filesystem::path(folder), ec)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(uint8_t(c)));
        if (ext == ".ttf" || (!ttf_only && (ext == ".otf" || ext == ".ttc"))) paths.push_back(e.path());
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& p : paths) {
        const std::string s = p.string();
        Ref<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(s.data()),
                                                                  CFIndex(s.size()), false));
        if (!url) continue;
        Ref<CFArrayRef> descs(CTFontManagerCreateFontDescriptorsFromURL(url.get()));
        if (!descs) continue;
        bool any = false;
        for (CFIndex i = 0; i < CFArrayGetCount(descs.get()); ++i) {
            const auto d = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(descs.get(), i));
            if (d == nullptr) continue;
            out.push_back(face_info(d));
            any = true;
        }
        if (any && files != nullptr) ++*files;
    }
    return out;
}

bool same_family(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) return false;
    }
    return true;
}

// As faces de uma família instalada no sistema (vazio se não existir).
std::vector<FaceInfo> system_family(const std::string& family) {
    std::vector<FaceInfo> out;
    Ref<CFStringRef> name(CFStringCreateWithCString(kCFAllocatorDefault, family.c_str(), kCFStringEncodingUTF8));
    if (!name) return out;
    const void* keys[1] = {kCTFontFamilyNameAttribute};
    const void* values[1] = {name.get()};
    Ref<CFDictionaryRef> attrs(
        CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    Ref<CTFontDescriptorRef> d(CTFontDescriptorCreateWithAttributes(attrs.get()));
    Ref<CFSetRef> mandatory(CFSetCreate(kCFAllocatorDefault, keys, 1, &kCFTypeSetCallBacks));
    if (!d) return out;
    Ref<CFArrayRef> matches(CTFontDescriptorCreateMatchingFontDescriptors(d.get(), mandatory.get()));
    if (!matches) return out;
    for (CFIndex i = 0; i < CFArrayGetCount(matches.get()); ++i) {
        const auto m = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(matches.get(), i));
        if (m == nullptr) continue;
        FaceInfo f = face_info(m);
        if (same_family(f.family, family)) out.push_back(std::move(f));
    }
    return out;
}

// A face de uma família que o DirectWrite escolheria (GetFirstMatchingFont: a largura mais perto, o estilo, o peso pela
// regra do CSS). Nulo sem faces.
const FaceInfo* match_face(const std::vector<FaceInfo>& faces, int weight, int stretch, bool italic) {
    if (faces.empty()) return nullptr;
    int best_stretch = 99;
    for (const FaceInfo& f : faces) best_stretch = std::min(best_stretch, std::abs(f.stretch - stretch));
    bool has_style = false;
    for (const FaceInfo& f : faces) has_style |= std::abs(f.stretch - stretch) == best_stretch && f.italic == italic;
    std::vector<const FaceInfo*> pool;
    for (const FaceInfo& f : faces) {
        if (std::abs(f.stretch - stretch) != best_stretch) continue;
        if (has_style && f.italic != italic) continue;
        pool.push_back(&f);
    }
    // O peso (CSS): o exato; 400 tenta 500 antes dos mais leves; 500 tenta 400; abaixo de 400 os mais leves primeiro,
    // acima de 500 os mais pesados primeiro.
    const auto score = [&](int w) -> long {
        if (w == weight) return 0;
        if (weight == 400 && w == 500) return 1;
        if (weight == 500 && w == 400) return 1;
        const bool lighter_first = weight <= 500;
        if (lighter_first) return w < weight ? 10 + (weight - w) : 10000 + (w - weight);
        return w > weight ? 10 + (w - weight) : 10000 + (weight - w);
    };
    const FaceInfo* best = nullptr;
    long best_score = 0;
    for (const FaceInfo* f : pool) {
        const long s = score(f->weight);
        if (best == nullptr || s < best_score) {
            best = f;
            best_score = s;
        }
    }
    return best;
}

struct Font {
    Ref<CTFontRef> font;
    float cap = 0.7f;  // capHeight / em
};

Font make_font(const FaceInfo* face, float px, bool oblique) {
    Font f;
    if (face == nullptr) return f;
    const CGFloat size = std::max<CGFloat>(1.0, px);
    if (oblique) {
        const CGAffineTransform skew = CGAffineTransformMake(1, 0, kObliqueShear, 1, 0, 0);
        f.font.reset(CTFontCreateWithFontDescriptor(face->desc.get(), size, &skew));
    } else {
        f.font.reset(CTFontCreateWithFontDescriptor(face->desc.get(), size, nullptr));
    }
    if (f.font) {
        const CGFloat cap = CTFontGetCapHeight(f.font.get());
        if (cap > 0) f.cap = float(cap / size);
    }
    return f;
}

// ---------------------------------------------------------------- o texto

// Uma cadeia com a fonte, o espaçamento depois de cada caractere de [0, kern_count) e a cor pelo contexto.
Ref<CFAttributedStringRef> attributed(const std::wstring& text, CTFontRef font, float kern, size_t kern_count) {
    Ref<CFStringRef> str = cf_string(text);
    if (!str) return Ref<CFAttributedStringRef>();
    Ref<CFMutableAttributedStringRef> as(CFAttributedStringCreateMutable(kCFAllocatorDefault, 0));
    CFAttributedStringReplaceString(as.get(), CFRangeMake(0, 0), str.get());
    const CFIndex n = CFAttributedStringGetLength(as.get());
    CFAttributedStringSetAttribute(as.get(), CFRangeMake(0, n), kCTFontAttributeName, font);
    CFAttributedStringSetAttribute(as.get(), CFRangeMake(0, n), kCTForegroundColorFromContextAttributeName, kCFBooleanTrue);
    if (kern != 0.0f && kern_count > 0) {
        const CGFloat k = kern;
        Ref<CFNumberRef> num(CFNumberCreate(kCFAllocatorDefault, kCFNumberCGFloatType, &k));
        CFAttributedStringSetAttribute(as.get(), CFRangeMake(0, std::min<CFIndex>(n, CFIndex(kern_count))), kCTKernAttributeName,
                                       num.get());
    }
    CFRetain(as.get());
    return Ref<CFAttributedStringRef>(as.get());
}

// A largura de uma linha: com o branco do fim (widthIncludingTrailingWhitespace do DirectWrite) ou sem (width).
float line_width(CTLineRef line, bool with_trailing) {
    CGFloat ascent = 0, descent = 0, leading = 0;
    const double w = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    return float(with_trailing ? w : w - CTLineGetTrailingWhitespaceWidth(line));
}

// Desenha os glifos de uma linha com a origem (a caneta na linha de base) na origem do espaço de agora, com y para cima
// (quem chama vira a matriz). colour(i) dá a cor do caractere i (sem cor: o preenchimento do contexto); stroke > 0 traça
// o contorno dos glifos com essa largura em vez de preencher (o GetGlyphRunOutline do Direct2D).
template <class ColourOf>
void draw_glyphs(CGContextRef ctx, CTLineRef line, float stroke, const ColourOf& colour) {
    CFArrayRef runs = CTLineGetGlyphRuns(line);
    const CFIndex nruns = runs != nullptr ? CFArrayGetCount(runs) : 0;
    std::vector<CGGlyph> glyphs;
    std::vector<CGPoint> pos;
    std::vector<CFIndex> idx;
    for (CFIndex r = 0; r < nruns; ++r) {
        const auto run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs, r));
        const CFIndex n = CTRunGetGlyphCount(run);
        if (n <= 0) continue;
        glyphs.resize(size_t(n));
        pos.resize(size_t(n));
        idx.resize(size_t(n));
        CTRunGetGlyphs(run, CFRangeMake(0, 0), glyphs.data());
        CTRunGetPositions(run, CFRangeMake(0, 0), pos.data());
        CTRunGetStringIndices(run, CFRangeMake(0, 0), idx.data());
        CFDictionaryRef attrs = CTRunGetAttributes(run);
        const auto font = static_cast<CTFontRef>(CFDictionaryGetValue(attrs, kCTFontAttributeName));
        if (font == nullptr) continue;
        if (stroke > 0.0f) {
            CGMutablePathRef path = CGPathCreateMutable();
            for (CFIndex g = 0; g < n; ++g) {
                const CGAffineTransform t = CGAffineTransformMakeTranslation(pos[size_t(g)].x, pos[size_t(g)].y);
                CGPathRef gp = CTFontCreatePathForGlyph(font, glyphs[size_t(g)], &t);
                if (gp != nullptr) {
                    CGPathAddPath(path, nullptr, gp);
                    CGPathRelease(gp);
                }
            }
            CGContextAddPath(ctx, path);
            CGContextSetLineWidth(ctx, stroke);
            CGContextSetLineJoin(ctx, kCGLineJoinRound);
            CGContextSetLineCap(ctx, kCGLineCapRound);
            CGContextStrokePath(ctx);
            CGPathRelease(path);
            continue;
        }
        // Os glifos seguidos da mesma cor de uma vez.
        CFIndex start = 0;
        while (start < n) {
            const auto c0 = colour(idx[size_t(start)]);
            CFIndex end = start + 1;
            while (end < n && colour(idx[size_t(end)]) == c0) ++end;
            if (c0.set) CGContextSetRGBFillColor(ctx, c0.r, c0.g, c0.b, c0.a);
            CTFontDrawGlyphs(font, glyphs.data() + start, pos.data() + start, size_t(end - start), ctx);
            start = end;
        }
    }
}

struct Ink {
    bool set = false;
    float r = 1, g = 1, b = 1, a = 1;
    bool operator==(const Ink& o) const { return set == o.set && r == o.r && g == o.g && b == o.b && a == o.a; }
};

// ---------------------------------------------------------------- o contexto

// Um CGBitmapContext BGRA pré-multiplicado de w x h com a matriz virada (y para baixo, a origem em cima à esquerda) e a
// origem da janela em (ox, oy): o pixel (x, y) da janela é o (x - ox, y - oy) da imagem.
class Bitmap {
public:
    ~Bitmap() { release(); }
    // clear = false: o conteúdo fica (quem chama põe uma foto por cima).
    bool make(CGColorSpaceRef cs, int w, int h, int ox, int oy, bool clear = true) {
        if (w <= 0 || h <= 0) return false;
        if (ctx_ == nullptr || w != w_ || h != h_) {
            release();
            w_ = w;
            h_ = h;
            data_.assign(size_t(w) * size_t(h) * 4, 0);
            const uint32_t info = uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Little);
            ctx_ = CGBitmapContextCreate(data_.data(), size_t(w), size_t(h), 8, size_t(w) * 4, cs, info);
            if (ctx_ == nullptr) return false;
            CGContextSaveGState(ctx_);  // o estado de fábrica, para cada desenho começar dele
        } else {
            if (clear) std::fill(data_.begin(), data_.end(), uint8_t(0));
            CGContextRestoreGState(ctx_);
            CGContextSaveGState(ctx_);
        }
        // A matriz: y para baixo e a origem da janela (o pixel (x, y) da janela). O texto vira de volta para y para cima
        // onde é desenhado (CTFontDrawGlyphs põe a matriz da fonte como matriz de texto).
        CGContextTranslateCTM(ctx_, CGFloat(-ox), CGFloat(h + oy));
        CGContextScaleCTM(ctx_, 1, -1);
        CGContextSetTextMatrix(ctx_, CGAffineTransformIdentity);
        CGContextSetShouldAntialias(ctx_, true);
        CGContextSetAllowsAntialiasing(ctx_, true);
        CGContextSetAllowsFontSmoothing(ctx_, false);
        CGContextSetShouldSmoothFonts(ctx_, false);
        CGContextSetAllowsFontSubpixelPositioning(ctx_, true);
        CGContextSetShouldSubpixelPositionFonts(ctx_, true);
        CGContextSetAllowsFontSubpixelQuantization(ctx_, false);
        CGContextSetShouldSubpixelQuantizeFonts(ctx_, false);
        CGContextSetBlendMode(ctx_, kCGBlendModeNormal);
        ox_ = ox;
        oy_ = oy;
        return true;
    }
    CGContextRef ctx() const { return ctx_; }
    int width() const { return w_; }
    int height() const { return h_; }
    int ox() const { return ox_; }
    int oy() const { return oy_; }
    uint8_t* data() { return data_.data(); }
    const uint8_t* data() const { return data_.data(); }
    size_t bytes() const { return data_.size(); }
    // Termina o desenho (o CoreGraphics desenha na hora; a pilha de estados volta ao começo).
    void finish() { CGContextFlush(ctx_); }

private:
    CGContextRef ctx_ = nullptr;
    std::vector<uint8_t> data_;
    int w_ = 0, h_ = 0, ox_ = 0, oy_ = 0;
    void release() {
        if (ctx_ != nullptr) CGContextRelease(ctx_);
        ctx_ = nullptr;
    }
};

void add_path(CGMutablePathRef p, const svg::Path& path) {
    for (const svg::PathOp& op : path) {
        switch (op.kind) {
            case svg::PathOp::Move: CGPathMoveToPoint(p, nullptr, op.p[0].x, op.p[0].y); break;
            case svg::PathOp::Line:
                if (CGPathIsEmpty(p)) CGPathMoveToPoint(p, nullptr, op.p[0].x, op.p[0].y);
                else CGPathAddLineToPoint(p, nullptr, op.p[0].x, op.p[0].y);
                break;
            case svg::PathOp::Cubic:
                CGPathAddCurveToPoint(p, nullptr, op.p[0].x, op.p[0].y, op.p[1].x, op.p[1].y, op.p[2].x, op.p[2].y);
                break;
            case svg::PathOp::Close: CGPathCloseSubpath(p); break;
        }
    }
}

CGAffineTransform cg(const svg::Matrix& m) { return CGAffineTransformMake(m.a, m.b, m.c, m.d, m.e, m.f); }

// Um degradê (os pontos sem pré-multiplicar) no DeviceRGB.
Ref<CGGradientRef> make_gradient(CGColorSpaceRef cs, const std::vector<UiVecStop>& stops) {
    std::vector<CGFloat> comps, locs;
    for (const UiVecStop& s : stops) {
        comps.insert(comps.end(), {CGFloat(s.r), CGFloat(s.g), CGFloat(s.b), CGFloat(std::clamp(s.a, 0.0f, 1.0f))});
        locs.push_back(std::clamp(s.offset, 0.0f, 1.0f));
    }
    if (locs.empty()) return Ref<CGGradientRef>();
    return Ref<CGGradientRef>(CGGradientCreateWithColorComponents(cs, comps.data(), locs.data(), locs.size()));
}

// A cor das pontas além delas (o D2D1_EXTEND_MODE_CLAMP).
const CGGradientDrawingOptions kExtend =
    CGGradientDrawingOptions(uint32_t(kCGGradientDrawsBeforeStartLocation) | uint32_t(kCGGradientDrawsAfterEndLocation));

// Um SVG lido pelo port, pintado com a matriz vp (do espaço do documento ao de agora), como o ID2D1SvgDocument.
void draw_svg(CGContextRef ctx, CGColorSpaceRef cs, const svg::Document& doc, const svg::Matrix& vp) {
    for (const svg::Shape& s : doc.shapes) {
        CGContextSaveGState(ctx);
        for (const svg::ClipRef& c : s.clips) {
            if (c.clip < 0 || size_t(c.clip) >= doc.clips.size()) continue;
            const svg::ClipPath& cp = doc.clips[size_t(c.clip)];
            CGMutablePathRef p = CGPathCreateMutable();
            for (const svg::Path& path : cp.paths) add_path(p, path);
            CGContextSaveGState(ctx);
            CGContextConcatCTM(ctx, cg(c.m.then(vp)));
            CGContextAddPath(ctx, p);
            CGContextRestoreGState(ctx);  // o caminho fica (já transformado), a matriz volta
            if (!cp.evenodd.empty() && cp.evenodd[0] != 0) CGContextEOClip(ctx);
            else CGContextClip(ctx);
            CGPathRelease(p);
        }
        CGContextConcatCTM(ctx, cg(s.m.then(vp)));
        CGMutablePathRef path = CGPathCreateMutable();
        add_path(path, s.path);
        const svg::Box box = svg::path_bounds(s.path, svg::Matrix{});
        const auto paint_gradient = [&](const svg::Paint& p, float opacity) {
            if (p.gradient < 0 || size_t(p.gradient) >= doc.gradients.size()) return;
            const svg::Gradient& g = doc.gradients[size_t(p.gradient)];
            std::vector<UiVecStop> stops;
            for (const svg::Stop& st : g.stops) stops.push_back(UiVecStop{float(st.offset), st.r, st.g, st.b, st.a});
            Ref<CGGradientRef> grad = make_gradient(cs, stops);
            if (!grad) return;
            CGContextSetAlpha(ctx, opacity);
            CGContextConcatCTM(ctx, cg(svg::gradient_matrix(g, box)));
            if (g.radial) {
                CGContextDrawRadialGradient(ctx, grad.get(), CGPointMake(g.fx, g.fy), 0, CGPointMake(g.cx, g.cy), g.r, kExtend);
            } else {
                CGContextDrawLinearGradient(ctx, grad.get(), CGPointMake(g.x1, g.y1), CGPointMake(g.x2, g.y2), kExtend);
            }
        };
        if (s.fill.kind == svg::Paint::Color) {
            CGContextSetRGBFillColor(ctx, s.fill.r, s.fill.g, s.fill.b, s.fill_opacity);
            CGContextAddPath(ctx, path);
            if (s.evenodd) CGContextEOFillPath(ctx);
            else CGContextFillPath(ctx);
        } else if (s.fill.kind == svg::Paint::Gradient) {
            CGContextSaveGState(ctx);
            CGContextAddPath(ctx, path);
            if (s.evenodd) CGContextEOClip(ctx);
            else CGContextClip(ctx);
            paint_gradient(s.fill, s.fill_opacity);
            CGContextRestoreGState(ctx);
        }
        if (s.stroke.kind != svg::Paint::None && s.stroke_width > 0.0) {
            CGContextSetLineWidth(ctx, s.stroke_width);
            CGContextSetLineCap(ctx, s.cap == svg::Cap::Round    ? kCGLineCapRound
                                     : s.cap == svg::Cap::Square ? kCGLineCapSquare
                                                                 : kCGLineCapButt);
            CGContextSetLineJoin(ctx, s.join == svg::Join::Round   ? kCGLineJoinRound
                                      : s.join == svg::Join::Bevel ? kCGLineJoinBevel
                                                                   : kCGLineJoinMiter);
            CGContextSetMiterLimit(ctx, s.miter);
            if (s.stroke.kind == svg::Paint::Color) {
                CGContextSetRGBStrokeColor(ctx, s.stroke.r, s.stroke.g, s.stroke.b, s.stroke_opacity);
                CGContextAddPath(ctx, path);
                CGContextStrokePath(ctx);
            } else {
                CGContextSaveGState(ctx);
                CGContextAddPath(ctx, path);
                CGContextReplacePathWithStrokedPath(ctx);
                CGContextClip(ctx);
                paint_gradient(s.stroke, s.stroke_opacity);
                CGContextRestoreGState(ctx);
            }
        }
        CGPathRelease(path);
        CGContextRestoreGState(ctx);
    }
}

// Uma caixa em pixels inteiros da janela (o que o desenho tocou).
struct Dirty {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = true;
    void add(float a0, float b0, float a1, float b1) {
        const int ix0 = int(std::floor(std::min(a0, a1))) - 1, iy0 = int(std::floor(std::min(b0, b1))) - 1;
        const int ix1 = int(std::ceil(std::max(a0, a1))) + 1, iy1 = int(std::ceil(std::max(b0, b1))) + 1;
        if (empty) {
            x0 = ix0;
            y0 = iy0;
            x1 = ix1;
            y1 = iy1;
            empty = false;
            return;
        }
        x0 = std::min(x0, ix0);
        y0 = std::min(y0, iy0);
        x1 = std::max(x1, ix1);
        y1 = std::max(y1, iy1);
    }
    void clamp(int w, int h) {
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(w, x1);
        y1 = std::min(h, y1);
        if (x1 <= x0 || y1 <= y0) empty = true;
    }
};

// Copia a parte [d] de um Bitmap (que cobre a janela a partir de ox, oy) para a imagem de saída.
void crop(const Bitmap& bmp, Dirty d, UiRaster& out) {
    d.x0 = std::max(d.x0, bmp.ox());
    d.y0 = std::max(d.y0, bmp.oy());
    d.x1 = std::min(d.x1, bmp.ox() + bmp.width());
    d.y1 = std::min(d.y1, bmp.oy() + bmp.height());
    if (d.empty || d.x1 <= d.x0 || d.y1 <= d.y0) {
        out.x = out.y = out.width = out.height = 0;
        out.bgra.clear();
        return;
    }
    out.x = d.x0;
    out.y = d.y0;
    out.width = d.x1 - d.x0;
    out.height = d.y1 - d.y0;
    out.bgra.resize(size_t(out.width) * size_t(out.height) * 4);
    for (int y = 0; y < out.height; ++y) {
        const uint8_t* src = bmp.data() + (size_t(d.y0 - bmp.oy() + y) * size_t(bmp.width()) + size_t(d.x0 - bmp.ox())) * 4;
        std::memcpy(out.bgra.data() + size_t(y) * size_t(out.width) * 4, src, size_t(out.width) * 4);
    }
}

}  // namespace

// ---------------------------------------------------------------- o desenho

class UiVectorCg final : public UiVectorCanvas {
public:
    UiVectorCg() {
        cs_ = CGColorSpaceCreateDeviceRGB();
        if (cs_ == nullptr) error_ = "sem o espaço de cor";
        mono_ = system_family("Menlo");
        if (mono_.empty()) mono_ = system_family("Courier");
        load_sans_fallback();
    }
    ~UiVectorCg() override {
        if (cs_ != nullptr) CGColorSpaceRelease(cs_);
    }

    bool ok() const override { return cs_ != nullptr; }

    // ---- os menus Modern ----

    void configure_modern(const std::wstring& dir) override {
        modern_dir_ = dir;
        rajdhani_.clear();
        font_files_ = 0;
        fonts_.clear();
        widths_.clear();
        glyphs_.clear();
        layers_.clear();
        lines_.clear();
        if (!dir.empty()) {
            std::vector<FaceInfo> faces = faces_from_folder(os::join(dir, L"fonts_ofl/rajdhani"), true, &font_files_);
            for (FaceInfo& f : faces) {
                if (same_family(f.family, "Rajdhani")) rajdhani_.push_back(std::move(f));
            }
        }
    }

    float measure(const std::wstring& text, const ui::TextStyle& st) override {
        const std::wstring s = st.caps ? ui::to_upper(text) : text;
        if (s.empty()) return 0.0f;
        wchar_t key[64];
        std::swprintf(key, 64, L"%d|%d|%.2f|%.3f|", int(st.face), st.weight, double(st.size), double(st.tracking));
        const std::wstring k = key + s;
        const auto it = widths_.find(k);
        if (it != widths_.end()) return it->second;
        float w = ui::estimate_width(s, st);
        const Font* f = modern_font(st.face, st.weight, st.size);
        if (f != nullptr && f->font) {
            Ref<CFAttributedStringRef> as = attributed(s, f->font.get(), st.tracking * st.size, s.size());
            if (as) {
                Ref<CTLineRef> line(CTLineCreateWithAttributedString(as.get()));
                if (line) w = std::max(0.0f, line_width(line.get(), true) - st.tracking * st.size);
            }
        }
        if (widths_.size() > 4096) widths_.clear();
        widths_[k] = w;
        return w;
    }

    // Os menus mandam uma ou mais listas por quadro (o menu principal: o fundo, o véu e a frente), e a da frente muda a
    // cada quadro (o brilho do foco tremula). Cada lista fica numa camada: igual à última da camada, a imagem guardada
    // volta sem desenhar; com o começo igual, a foto do bitmap depois desse começo (sem recorte aberto) volta e só o
    // resto é desenhado. A camada é a de começo mais parecido (ou a menos usada).
    void draw_modern(const ui::DrawList& list, const ui::Canvas& cv, int window_w, int window_h, UiRaster& out) override {
        if (!ok() || window_w <= 0 || window_h <= 0) {
            out = UiRaster{};
            return;
        }
        ++draws_;
        const uint64_t canvas_key = ui_vec_canvas_hash(cv, window_w, window_h);
        std::vector<uint64_t> h = ui_vec_cmd_hashes(list);
        ++use_clock_;
        for (Layer& l : layers_) {
            if (l.canvas == canvas_key && l.raster.version != 0 && l.cmds == h) {
                l.used = use_clock_;
                ++cache_hits_;
                if (out.version != l.raster.version) out = l.raster;
                return;
            }
        }
        // A camada: a do começo comum mais longo; sem nenhuma, uma nova ou a menos usada.
        Layer* layer = nullptr;
        size_t common = 0;
        for (Layer& l : layers_) {
            if (l.canvas != canvas_key) continue;
            size_t n = 0;
            while (n < l.cmds.size() && n < h.size() && l.cmds[n] == h[n]) ++n;
            if (n > common) {
                common = n;
                layer = &l;
            }
        }
        if (layer == nullptr) {
            if (layers_.size() < kLayers) {
                layers_.emplace_back();
                layer = &layers_.back();
            } else {
                layer = &*std::min_element(layers_.begin(), layers_.end(),
                                           [](const Layer& a, const Layer& b) { return a.used < b.used; });
            }
            const uint64_t keep = layer->raster.version;
            *layer = Layer{};
            layer->raster.version = keep;
            layer->canvas = canvas_key;
            common = 0;
        }
        Layer& L = *layer;
        L.used = use_clock_;
        // A foto vale até o fim do começo comum, num ponto sem recorte aberto.
        std::vector<int> depth(h.size() + 1, 0);
        for (size_t i = 0; i < list.cmds.size(); ++i) {
            const ui::DrawCmd::Kind k = list.cmds[i].kind;
            depth[i + 1] = std::max(0, depth[i] + (k == ui::DrawCmd::PushClip ? 1 : k == ui::DrawCmd::PopClip ? -1 : 0));
        }
        size_t snap_at = common;
        while (snap_at > 0 && depth[snap_at] != 0) --snap_at;
        const size_t full = size_t(window_w) * size_t(window_h) * 4;
        const size_t start = L.snap_n > 0 && L.snap_n <= common && L.snap.size() == full ? L.snap_n : 0;
        const auto t0 = std::chrono::steady_clock::now();
        if (!modern_bmp_.make(cs_, window_w, window_h, 0, 0, start == 0)) {
            error_ = "CGBitmapContextCreate falhou";
            out = UiRaster{};
            return;
        }
        if (start > 0) {
            std::memcpy(modern_bmp_.data(), L.snap.data(), full);
            dirty_ = L.snap_dirty;
            ++restored_;
        } else {
            dirty_ = Dirty{};
        }
        t_clear_ += ms_since(t0);
        scale_ = cv.scale;
        off_x_ = cv.off_x;
        off_y_ = cv.off_y;
        win_w_ = float(cv.win_w > 0 ? cv.win_w : window_w);
        win_h_ = float(cv.win_h > 0 ? cv.win_h : window_h);
        clips_.clear();
        CGContextRef ctx = modern_bmp_.ctx();
        for (size_t i = start; i < list.cmds.size(); ++i) {
            if (i == snap_at && snap_at > start) {
                // A foto do começo que não mudou (o próximo quadro começa dela).
                const auto ts = std::chrono::steady_clock::now();
                L.snap.assign(modern_bmp_.data(), modern_bmp_.data() + full);
                L.snap_n = snap_at;
                L.snap_dirty = dirty_;
                t_snap_ += ms_since(ts);
            }
            timed_command(ctx, list.cmds[i]);
        }
        if (snap_at == list.cmds.size() && snap_at > start) {
            L.snap.assign(modern_bmp_.data(), modern_bmp_.data() + full);
            L.snap_n = snap_at;
            L.snap_dirty = dirty_;
        }
        if (snap_at < L.snap_n) L.snap_n = 0;  // o começo mudou antes da foto: ela não vale mais
        cmds_drawn_ += list.cmds.size() - start;
        cmds_total_ += list.cmds.size();
        while (!clips_.empty()) {
            CGContextRestoreGState(ctx);
            clips_.pop_back();
        }
        modern_bmp_.finish();
        const auto tc = std::chrono::steady_clock::now();
        Dirty area = dirty_;
        area.clamp(window_w, window_h);
        crop(modern_bmp_, area, L.raster);
        L.raster.version = ++version_;
        L.cmds = std::move(h);
        out = L.raster;
        t_crop_ += ms_since(tc);
        ++rasters_;
    }

    std::string modern_describe() const override {
        std::string s = ok() ? "ok (CoreGraphics)" : "sem CoreGraphics";
        s += " fonte ";
        s += !rajdhani_.empty() ? "Rajdhani" : (sans_name_.empty() ? "nenhuma" : sans_name_);
        s += " (" + std::to_string(font_files_) + " arquivos) svg";
        s += " | listas " + std::to_string(draws_) + " iguais " + std::to_string(cache_hits_) + " desenhadas " +
             std::to_string(rasters_) + " do_comeco " + std::to_string(restored_) + " comandos " + std::to_string(cmds_drawn_) +
             "/" + std::to_string(cmds_total_) + " camadas " + std::to_string(layers_.size());
        if (rasters_ > 0) {
            // O tempo médio de cada desenho, por parte (ms).
            char buf[96];
            const double n = double(rasters_);
            std::snprintf(buf, sizeof(buf), " | ms: limpar %.2f foto %.2f recortar %.2f", t_clear_ / n, t_snap_ / n, t_crop_ / n);
            s += buf;
            static const char* kNames[] = {"Fill",    "Stroke",  "Line",  "Text",  "Ellipse", "Glow",  "Radial", "Gradient",
                                           "Chevron", "Padlock", "Diamond", "Glyph", "Sector",  "PushClip", "PopClip"};
            for (int k = 0; k < kKinds; ++k) {
                if (n_kind_[k] == 0) continue;
                std::snprintf(buf, sizeof(buf), " %s %.2f(%llu)", k < 15 ? kNames[k] : "?", t_kind_[k] / n,
                              static_cast<unsigned long long>(n_kind_[k]));
                s += buf;
            }
        }
        if (!error_.empty()) s += " erro: " + error_;
        return s;
    }

    // ---- a camada HD ----

    void configure_hd(const UiHdConfig* config, const std::wstring& dir) override {
        config_ = config;
        hd_dir_ = dir;
        assets_.clear();
        svgs_.clear();
        failed_.assign(config != nullptr ? config->images.size() : 0, 0);
        svgs_.resize(failed_.size());
        hd_cache_.clear();
        user_fonts_.clear();
        user_font_files_ = 0;
        if (!dir.empty()) user_fonts_ = faces_from_folder(os::join(dir, L"fonts"), false, &user_font_files_);
        hd_fonts_.clear();
        for (int k = 1; k <= 3; ++k) resolve_hd_font(k);
    }

    bool asset(int index, int w, int h, std::vector<uint8_t>& rgba, int& out_w, int& out_h) override {
        if (!ok() || config_ == nullptr || index < 0 || size_t(index) >= config_->images.size()) return false;
        if (failed_[size_t(index)] != 0) return false;
        const UiHdImageEntry& e = config_->images[size_t(index)];
        const bool png = e.type == UiHdAssetType::Png;
        if (png) w = h = 0;
        w = std::clamp(w, png ? 0 : 1, 4096);
        h = std::clamp(h, png ? 0 : 1, 4096);
        for (Asset& a : assets_) {
            if (a.index == index && a.w == w && a.h == h) {
                a.used = frame_;
                rgba = a.rgba;
                out_w = a.out_w;
                out_h = a.out_h;
                return true;
            }
        }
        Asset a;
        a.index = index;
        a.w = w;
        a.h = h;
        a.used = frame_;
        const std::wstring path = os::join(hd_dir_, std::filesystem::path(e.file).wstring());
        bool done = false;
        if (png) {
            std::vector<uint8_t> bgra;
            int pw = 0, ph = 0;
            if (read_image_bgra(path, 8192, &pw, &ph, &bgra)) {
                ui_vec_to_gs_rgba(bgra.data(), uint32_t(pw) * 4, pw, ph, false, a.rgba);
                a.out_w = pw;
                a.out_h = ph;
                done = true;
            }
        } else {
            std::unique_ptr<svg::Document>& doc = svgs_[size_t(index)];
            if (doc == nullptr) {
                const std::vector<uint8_t> data = read_file(path);
                auto d = std::make_unique<svg::Document>();
                std::string err;
                if (!data.empty() && svg::parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), *d, &err)) {
                    doc = std::move(d);
                }
            }
            Bitmap bmp;
            if (doc != nullptr && bmp.make(cs_, w, h, 0, 0)) {
                draw_svg(bmp.ctx(), cs_, *doc, svg::viewport(*doc, w, h));
                bmp.finish();
                ui_vec_to_gs_rgba(bmp.data(), uint32_t(w) * 4, w, h, true, a.rgba);
                a.out_w = w;
                a.out_h = h;
                done = true;
            }
        }
        if (!done) {
            failed_[size_t(index)] = 1;
            error_ = "substituta " + e.file + " não carregou";
            return false;
        }
        rgba = a.rgba;
        out_w = a.out_w;
        out_h = a.out_h;
        assets_.push_back(std::move(a));
        return true;
    }

    void draw_hd(const std::vector<const UiHdText*>& texts, const std::vector<const UiHdKey*>& keys,
                 const std::vector<UiHdOp>& order, int window_w, int window_h, UiRaster& out) override {
        if (!ok() || window_w <= 0 || window_h <= 0) {
            out = UiRaster{};
            return;
        }
        // O que cada passo desenha: o índice do passo aponta em texts ou keys (os do lote inteiro).
        struct Step {
            const UiHdText* text = nullptr;
            const UiHdKey* key = nullptr;
        };
        std::vector<Step> steps;
        for (const UiHdOp& op : order) {
            Step s;
            if (op.kind == UiHdOp::Text && size_t(op.index) < texts.size()) s.text = texts[op.index];
            if (op.kind == UiHdOp::Key && size_t(op.index) < keys.size()) s.key = keys[op.index];
            if (s.text != nullptr || s.key != nullptr) steps.push_back(s);
        }
        // O resumo do lote (o cache) e a área tocada (os recortes de cada um, na janela).
        const uint64_t hash = hd_hash(steps.size(), [&](size_t i, auto& h) { hash_step(h, steps[i].text, steps[i].key); },
                                      window_w, window_h);
        ++hd_clock_;
        for (HdEntry& e : hd_cache_) {
            if (e.hash == hash && e.raster.version != 0) {
                e.used = hd_clock_;
                if (out.version != e.raster.version) out = e.raster;
                return;
            }
        }
        HdEntry* entry = nullptr;
        if (hd_cache_.size() < kHdEntries) {
            hd_cache_.emplace_back();
            entry = &hd_cache_.back();
        } else {
            entry = &*std::min_element(hd_cache_.begin(), hd_cache_.end(),
                                       [](const HdEntry& a, const HdEntry& b) { return a.used < b.used; });
        }
        entry->hash = hash;
        entry->used = hd_clock_;
        UiRaster& hd_out_ = entry->raster;
        Dirty area;
        for (const Step& s : steps) {
            const float* c = s.text != nullptr ? s.text->clip : s.key->clip;
            float x0 = c[0], y0 = c[1], x1 = c[2], y1 = c[3];
            if (s.key != nullptr) {
                x0 = std::max(x0, s.key->x0);
                y0 = std::max(y0, s.key->y0);
                x1 = std::min(x1, s.key->x1);
                y1 = std::min(y1, s.key->y1);
            }
            if (x1 > x0 && y1 > y0) area.add(x0, y0, x1, y1);
        }
        area.clamp(window_w, window_h);
        if (area.empty) {
            hd_out_ = UiRaster{};
            hd_out_.version = ++version_;
            out = hd_out_;
            return;
        }
        const int aw = area.x1 - area.x0, ah = area.y1 - area.y0;
        if (!hd_bmp_.make(cs_, aw, ah, area.x0, area.y0)) {
            error_ = "CGBitmapContextCreate falhou";
            out = UiRaster{};
            return;
        }
        for (const Step& s : steps) {
            if (s.text != nullptr) {
                if (s.text->alpha == 0x48) {
                    // Aditivo: numa camada à parte, somada só na cor.
                    if (!add_bmp_.make(cs_, aw, ah, area.x0, area.y0)) continue;
                    draw_text(add_bmp_.ctx(), *s.text);
                    add_bmp_.finish();
                    uint8_t* d = hd_bmp_.data();
                    const uint8_t* a = add_bmp_.data();
                    for (size_t i = 0; i < size_t(aw) * size_t(ah) * 4; i += 4) {
                        for (int k = 0; k < 3; ++k) d[i + size_t(k)] = uint8_t(std::min(255, d[i + size_t(k)] + a[i + size_t(k)]));
                    }
                    continue;
                }
                draw_text(hd_bmp_.ctx(), *s.text);
            } else {
                draw_key(hd_bmp_.ctx(), *s.key);
            }
        }
        hd_bmp_.finish();
        crop(hd_bmp_, area, hd_out_);
        hd_out_.version = ++version_;
        out = hd_out_;
    }

    float hd_measure(const std::wstring& label, float cap_px) override {
        if (!ok() || label.empty()) return 0.0f;
        const HdFont& hf = hd_font(2);
        const UiHdFont def;
        const UiHdFont& cfg = config_ != nullptr ? config_->fonts[2] : def;
        const float em = ui_hd_em(cap_px, hf.cap, cfg.size);
        const Font* f = hd_sized(2, em);
        if (f == nullptr || !f->font) return 0.0f;
        Ref<CFAttributedStringRef> as = attributed(label, f->font.get(), 0.0f, 0);
        if (!as) return 0.0f;
        Ref<CTLineRef> line(CTLineCreateWithAttributedString(as.get()));
        return line ? line_width(line.get(), false) : 0.0f;
    }

    void end_frame() override {
        ++frame_;
        // Rasterizações sem uso há uns 5 s (a 60 quadros) saem do cache.
        assets_.erase(std::remove_if(assets_.begin(), assets_.end(), [&](const Asset& a) { return a.used + 300 < frame_; }),
                      assets_.end());
    }

    std::string hd_describe() const override {
        std::string s = ok() ? "ok (CoreGraphics)" : "sem CoreGraphics";
        s += ", svg (leitor do port), fontes do usuário " + std::to_string(user_font_files_);
        for (int k = 1; k <= 3; ++k) {
            const auto it = hd_fonts_.find(k);
            if (it == hd_fonts_.end()) continue;
            const HdFont& f = it->second;
            char buf[200];
            std::snprintf(buf, sizeof(buf), ", fonte %d %s %d%s%s (maiúsculas %.3f em)", k, f.name.c_str(), f.weight,
                          f.oblique ? " inclinada" : (f.italic ? " itálico" : ""), f.found ? "" : " (reserva)", double(f.cap));
            s += buf;
        }
        if (!error_.empty()) s += ", erro: " + error_;
        return s;
    }

private:
    CGColorSpaceRef cs_ = nullptr;
    std::string error_;
    uint64_t version_ = 0;

    // ---- menus Modern ----
    std::wstring modern_dir_;
    std::vector<FaceInfo> rajdhani_, sans_, mono_;
    std::string sans_name_;
    int font_files_ = 0;
    std::map<uint64_t, Font> fonts_;
    std::unordered_map<std::wstring, float> widths_;
    std::map<std::string, std::unique_ptr<svg::Document>> glyphs_;
    std::map<uint64_t, Ref<CGGradientRef>> gradients_;
    Bitmap modern_bmp_;
    // As camadas (ver draw_modern).
    struct Layer {
        uint64_t canvas = 0;           // ui_vec_canvas_hash
        std::vector<uint64_t> cmds;    // os resumos dos comandos da última lista
        UiRaster raster;               // a imagem dela
        std::vector<uint8_t> snap;     // o bitmap (a janela inteira) depois de snap_n comandos
        size_t snap_n = 0;
        Dirty snap_dirty;
        uint64_t used = 0;
    };
    static constexpr size_t kLayers = 4;
    std::vector<Layer> layers_;
    uint64_t use_clock_ = 0;
    // As linhas do CoreText dos textos dos últimos quadros (a tela monta os mesmos textos a cada quadro).
    struct TextLines {
        std::vector<Ref<CTLineRef>> lines;
        std::vector<float> trimmed, full;  // as larguras sem e com o branco do fim
    };
    std::unordered_map<std::wstring, TextLines> lines_;
    // O tempo de cada parte (o --bench do nf_vector_check).
    static constexpr int kKinds = 16;
    double t_kind_[kKinds]{};
    uint64_t n_kind_[kKinds]{};
    double t_clear_ = 0, t_snap_ = 0, t_crop_ = 0;
    uint64_t draws_ = 0, cache_hits_ = 0, rasters_ = 0, restored_ = 0, cmds_drawn_ = 0, cmds_total_ = 0;

    static double ms_since(std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    }
    void timed_command(CGContextRef ctx, const ui::DrawCmd& c) {
        const auto t = std::chrono::steady_clock::now();
        command(ctx, c);
        const int k = std::clamp(int(c.kind), 0, kKinds - 1);
        t_kind_[k] += ms_since(t);
        ++n_kind_[k];
    }
    float scale_ = 1, off_x_ = 0, off_y_ = 0, win_w_ = 0, win_h_ = 0;
    Dirty dirty_;
    std::vector<UiVecRect> clips_;  // os recortes abertos (a janela), para a área tocada

    // ---- camada HD ----
    const UiHdConfig* config_ = nullptr;
    std::wstring hd_dir_;
    std::vector<FaceInfo> user_fonts_;
    int user_font_files_ = 0;
    struct HdFont {
        std::vector<FaceInfo> faces;  // a família escolhida
        const FaceInfo* face = nullptr;
        std::string name;
        int weight = 400;
        bool italic = false, oblique = false, found = false;
        float cap = 0.7f;
        std::map<uint32_t, Font> sized;  // pelo em x 64
    };
    std::map<int, HdFont> hd_fonts_;
    struct Asset {
        int index = -1, w = 0, h = 0, out_w = 0, out_h = 0;
        std::vector<uint8_t> rgba;
        uint64_t used = 0;
    };
    std::vector<Asset> assets_;
    std::vector<std::unique_ptr<svg::Document>> svgs_;
    std::vector<uint8_t> failed_;
    uint64_t frame_ = 0;
    Bitmap hd_bmp_, add_bmp_;
    // Os lotes da camada HD (várias chamadas por quadro, um por pedaço de psiDrawSprites).
    struct HdEntry {
        uint64_t hash = 0, used = 0;
        UiRaster raster;
    };
    static constexpr size_t kHdEntries = 16;
    std::vector<HdEntry> hd_cache_;
    uint64_t hd_clock_ = 0;

    // ---------------------------------------------------------------- fontes

    // A reserva da Rajdhani (o Windows usa a Bahnschrift): a primeira família do mesmo desenho que o macOS tiver.
    void load_sans_fallback() {
        for (const char* name : {"DIN Alternate", "DIN Condensed", "Helvetica Neue", "Helvetica"}) {
            sans_ = system_family(name);
            if (!sans_.empty()) {
                sans_name_ = name;
                return;
            }
        }
    }

    const Font* modern_font(ui::Face face, int weight, float px) {
        const uint64_t key = uint64_t(face) << 48 | uint64_t(uint16_t(weight)) << 32 | uint32_t(std::lround(px * 16.0f));
        const auto it = fonts_.find(key);
        if (it != fonts_.end()) return &it->second;
        const std::vector<FaceInfo>& faces = face == ui::Face::Mono ? mono_ : (!rajdhani_.empty() ? rajdhani_ : sans_);
        const FaceInfo* f = match_face(faces, weight, 5, false);
        if (f == nullptr) return nullptr;
        if (fonts_.size() > 512) {
            fonts_.clear();
        }
        return &(fonts_[key] = make_font(f, px, false));
    }

    void resolve_hd_font(int k) {
        HdFont hf;
        const UiHdFont def;
        const UiHdFont& cfg = config_ != nullptr ? config_->fonts[k] : def;
        hf.weight = cfg.weight;
        hf.italic = cfg.italic;
        const std::string family = to_ascii(cfg.family);
        // 1) a family do map.json nas fontes do usuário; 2) as do macOS (family_macos), do usuário ou do sistema; 3) a
        // family no sistema; 4) as reservas.
        std::vector<std::pair<std::string, bool>> candidates;  // (nome, só do usuário)
        candidates.emplace_back(family, true);
        for (const std::wstring& n : cfg.family_macos) candidates.emplace_back(to_ascii(n), false);
        candidates.emplace_back(family, false);
        const size_t configured = candidates.size();  // as do map.json; depois, as reservas
        for (const char* n : {"DIN Alternate", "Helvetica Neue", "Helvetica"}) candidates.emplace_back(n, false);
        for (size_t c = 0; c < candidates.size(); ++c) {
            const auto& [name, user_only] = candidates[c];
            std::vector<FaceInfo> faces;
            for (const FaceInfo& f : user_fonts_) {
                if (same_family(f.family, name)) faces.push_back(f);
            }
            if (faces.empty() && !user_only) faces = system_family(name);
            if (faces.empty()) continue;
            hf.faces = std::move(faces);
            hf.name = name;
            hf.found = c < configured;
            break;
        }
        hf.face = match_face(hf.faces, cfg.weight, cfg.stretch, cfg.italic);
        if (hf.face != nullptr) {
            hf.weight = hf.face->weight;
            hf.oblique = cfg.italic && !hf.face->italic;
            const Font probe = make_font(hf.face, 64.0f, false);
            hf.cap = probe.cap;
        }
        hd_fonts_[k] = std::move(hf);
    }

    const HdFont& hd_font(int k) {
        const auto it = hd_fonts_.find(k);
        if (it == hd_fonts_.end()) resolve_hd_font(k);
        return hd_fonts_[k];
    }

    const Font* hd_sized(int k, float em) {
        HdFont& hf = hd_fonts_[k];
        if (hf.face == nullptr) return nullptr;
        const uint32_t key = uint32_t(std::lround(em * 64.0f));
        const auto it = hf.sized.find(key);
        if (it != hf.sized.end()) return &it->second;
        if (hf.sized.size() > 256) hf.sized.clear();
        return &(hf.sized[key] = make_font(hf.face, em, hf.oblique));
    }

    // ---------------------------------------------------------------- menus Modern: os comandos

    float X(float x) const { return off_x_ + x * scale_; }
    float Y(float y) const { return off_y_ + y * scale_; }
    UiVecRect R(const ui::Rect& r) const { return UiVecRect{X(r.x0), Y(r.y0), X(r.x1), Y(r.y1)}; }
    float W(float w) const { return ui_vec_stroke_width(w, scale_); }

    // A área tocada, cortada pelos recortes abertos.
    void touch(float x0, float y0, float x1, float y1, float pad = 0.0f) {
        x0 -= pad;
        y0 -= pad;
        x1 += pad;
        y1 += pad;
        for (const UiVecRect& c : clips_) {
            x0 = std::max(x0, c.x0);
            y0 = std::max(y0, c.y0);
            x1 = std::min(x1, c.x1);
            y1 = std::min(y1, c.y1);
        }
        if (x1 > x0 && y1 > y0) dirty_.add(x0, y0, x1, y1);
    }

    // A imagem do bitmap dos menus (para pintar direto nela) e o recorte aberto (os pixels inteiros onde se pode pintar).
    UiVecImage image() { return UiVecImage{modern_bmp_.data(), modern_bmp_.width(), modern_bmp_.height(), 0, 0}; }
    UiVecRect clip_rect() const {
        UiVecRect c{0, 0, float(modern_bmp_.width()), float(modern_bmp_.height())};
        for (const UiVecRect& k : clips_) {
            c.x0 = std::max(c.x0, k.x0);
            c.y0 = std::max(c.y0, k.y0);
            c.x1 = std::min(c.x1, k.x1);
            c.y1 = std::min(c.y1, k.y1);
        }
        return c;
    }

    static void set_fill(CGContextRef ctx, const ui::Color& c) {
        CGContextSetRGBFillColor(ctx, c.r, c.g, c.b, std::clamp(c.a, 0.0f, 1.0f));
    }
    static void set_stroke(CGContextRef ctx, const ui::Color& c) {
        CGContextSetRGBStrokeColor(ctx, c.r, c.g, c.b, std::clamp(c.a, 0.0f, 1.0f));
    }

    CGGradientRef gradient(const ui::Color& a, const ui::Color& b, bool glow) {
        const auto pack = [](const ui::Color& c) {
            const auto q = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return q(c.r) << 24 | q(c.g) << 16 | q(c.b) << 8 | q(c.a);
        };
        const uint64_t key = (uint64_t(pack(a)) << 32 | pack(b)) ^ (glow ? 0x8000000000000000ull : 0);
        const auto it = gradients_.find(key);
        if (it != gradients_.end()) return it->second.get();
        if (gradients_.size() > 256) gradients_.clear();
        Ref<CGGradientRef>& g = gradients_[key];
        g = make_gradient(cs_, ui_vec_modern_gradient(a, b, glow));
        return g.get();
    }

    static CGMutablePathRef rounded_rect(const UiVecRect& r, float rx, float ry) {
        CGMutablePathRef p = CGPathCreateMutable();
        const CGFloat w = std::max(0.0f, r.x1 - r.x0), h = std::max(0.0f, r.y1 - r.y0);
        // O CGPathAddRoundedRect não aceita cantos maiores que a metade do lado.
        const CGFloat cw = std::clamp<CGFloat>(rx, 0, w * 0.5), ch = std::clamp<CGFloat>(ry, 0, h * 0.5);
        if (cw > 0 && ch > 0) CGPathAddRoundedRect(p, nullptr, CGRectMake(r.x0, r.y0, w, h), cw, ch);
        else CGPathAddRect(p, nullptr, CGRectMake(r.x0, r.y0, w, h));
        return p;
    }

    void fill_path(CGContextRef ctx, CGPathRef p, const ui::Color& c) {
        set_fill(ctx, c);
        CGContextAddPath(ctx, p);
        CGContextFillPath(ctx);
    }

    void command(CGContextRef ctx, const ui::DrawCmd& c) {
        switch (c.kind) {
            case ui::DrawCmd::Fill: {
                UiVecRect r = R(c.r);
                if (c.radius > 0.0f) {
                    CGMutablePathRef p = rounded_rect(r, c.radius * scale_, c.radius * scale_);
                    fill_path(ctx, p, c.c);
                    CGPathRelease(p);
                } else {
                    // Direto na imagem (o véu da janela inteira: o CoreGraphics na CPU é mais lento).
                    r = ui_vec_snap_fill(r);
                    UiVecImage img = image();
                    ui_vec_fill_rect(img, r, clip_rect(), c.c);
                }
                touch(r.x0, r.y0, r.x1, r.y1);
                break;
            }
            case ui::DrawCmd::Stroke: {
                const UiVecRect rr = ui_vec_stroke_rect(R(c.r));
                const float w = W(c.width);
                set_stroke(ctx, c.c);
                CGContextSetLineWidth(ctx, w);
                CGContextSetLineJoin(ctx, kCGLineJoinMiter);
                CGContextSetLineCap(ctx, kCGLineCapButt);
                CGMutablePathRef p = rounded_rect(rr, c.radius > 0.0f ? c.radius * scale_ : 0.0f, c.radius > 0.0f ? c.radius * scale_ : 0.0f);
                CGContextAddPath(ctx, p);
                CGContextStrokePath(ctx);
                CGPathRelease(p);
                touch(rr.x0, rr.y0, rr.x1, rr.y1, w);
                break;
            }
            case ui::DrawCmd::Line: {
                const float w = W(c.width);
                float x0 = X(c.r.x0), y0 = Y(c.r.y0), x1 = X(c.r.x1), y1 = Y(c.r.y1);
                ui_vec_snap_line(x0, y0, x1, y1, w);
                set_stroke(ctx, c.c);
                CGContextSetLineWidth(ctx, w);
                CGContextSetLineCap(ctx, kCGLineCapButt);
                CGContextMoveToPoint(ctx, x0, y0);
                CGContextAddLineToPoint(ctx, x1, y1);
                CGContextStrokePath(ctx);
                touch(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1), w);
                break;
            }
            case ui::DrawCmd::Text: text(ctx, c); break;
            case ui::DrawCmd::Ellipse: {
                const UiVecRect r = R(c.r);
                const CGRect e = CGRectMake(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0);
                if (c.width > 0.0f) {
                    const float w = W(c.width);
                    set_stroke(ctx, c.c);
                    CGContextSetLineWidth(ctx, w);
                    CGContextStrokeEllipseInRect(ctx, e);
                    touch(r.x0, r.y0, r.x1, r.y1, w);
                } else {
                    set_fill(ctx, c.c);
                    CGContextFillEllipseInRect(ctx, e);
                    touch(r.x0, r.y0, r.x1, r.y1);
                }
                break;
            }
            case ui::DrawCmd::Glow: {
                const UiVecRect r = R(c.r);
                const UiVecRect clip = ui_vec_aliased_clip(UiVecRect{r.x0, r.y0, r.x1, Y(c.radius)});
                CGContextSaveGState(ctx);
                CGContextClipToRect(ctx, CGRectMake(clip.x0, clip.y0, clip.x1 - clip.x0, clip.y1 - clip.y0));
                CGContextAddEllipseInRect(ctx, CGRectMake(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0));
                CGContextClip(ctx);
                radial(ctx, r, gradient(c.c, c.c2, true));
                CGContextRestoreGState(ctx);
                touch(clip.x0, clip.y0, clip.x1, std::min(clip.y1, r.y1));
                break;
            }
            case ui::DrawCmd::Radial: {
                // A vinheta: a janela inteira (fora da elipse, a cor da borda), como o FillRectangle da janela no
                // Direct2D; direto na imagem (no CoreGraphics da CPU uma tela cheia leva ~10 ms).
                UiVecImage img = image();
                ui_vec_fill_radial(img, R(c.r), UiVecRect{0, 0, win_w_, win_h_}, clip_rect(),
                                   ui_vec_modern_gradient(c.c, c.c2, false));
                touch(0, 0, win_w_, win_h_);
                break;
            }
            case ui::DrawCmd::Gradient: {
                // As faixas em degradê (o fundo, o sublinhado do foco): direto na imagem.
                const UiVecRect r = R(c.r);
                UiVecImage img = image();
                ui_vec_fill_linear(img, r, clip_rect(), ui_vec_modern_gradient(c.c, c.c2, false), c.dir == 1);
                touch(r.x0, r.y0, r.x1, r.y1);
                break;
            }
            case ui::DrawCmd::Chevron: {
                const UiVecRect r = R(c.r);
                const float cx = (r.x0 + r.x1) * 0.5f, cy = (r.y0 + r.y1) * 0.5f;
                CGPoint p[3];
                switch (c.dir) {
                    case 1: p[0] = CGPointMake(r.x0, r.y1); p[1] = CGPointMake(cx, r.y0); p[2] = CGPointMake(r.x1, r.y1); break;
                    case 2: p[0] = CGPointMake(r.x1, r.y0); p[1] = CGPointMake(r.x0, cy); p[2] = CGPointMake(r.x1, r.y1); break;
                    case 3: p[0] = CGPointMake(r.x0, r.y0); p[1] = CGPointMake(r.x1, cy); p[2] = CGPointMake(r.x0, r.y1); break;
                    default: p[0] = CGPointMake(r.x0, r.y0); p[1] = CGPointMake(cx, r.y1); p[2] = CGPointMake(r.x1, r.y0); break;
                }
                const float w = std::max(1.0f, c.width * scale_);
                set_stroke(ctx, c.c);
                CGContextSetLineWidth(ctx, w);
                CGContextSetLineCap(ctx, kCGLineCapRound);
                CGContextSetLineJoin(ctx, kCGLineJoinRound);
                CGContextAddLines(ctx, p, 3);
                CGContextStrokePath(ctx);
                touch(r.x0, r.y0, r.x1, r.y1, w);
                break;
            }
            case ui::DrawCmd::Padlock: padlock(ctx, c); break;
            case ui::DrawCmd::Diamond: {
                const UiVecRect r = R(c.r);
                const float cx = (r.x0 + r.x1) * 0.5f, cy = (r.y0 + r.y1) * 0.5f;
                const CGPoint p[4] = {CGPointMake(cx, r.y0), CGPointMake(r.x1, cy), CGPointMake(cx, r.y1), CGPointMake(r.x0, cy)};
                set_fill(ctx, c.c);
                CGContextAddLines(ctx, p, 4);
                CGContextClosePath(ctx);
                CGContextFillPath(ctx);
                touch(r.x0, r.y0, r.x1, r.y1);
                break;
            }
            case ui::DrawCmd::Glyph: glyph(ctx, c); break;
            case ui::DrawCmd::Sector: sector(ctx, c); break;
            case ui::DrawCmd::PushClip: {
                const UiVecRect r = ui_vec_aliased_clip(R(c.r));
                CGContextSaveGState(ctx);
                CGContextClipToRect(ctx, CGRectMake(r.x0, r.y0, std::max(0.0f, r.x1 - r.x0), std::max(0.0f, r.y1 - r.y0)));
                clips_.push_back(r);
                break;
            }
            case ui::DrawCmd::PopClip:
                if (!clips_.empty()) {
                    CGContextRestoreGState(ctx);
                    clips_.pop_back();
                }
                break;
        }
    }

    // O degradê radial (elíptico) do retângulo r, com a cor da borda além dela.
    void radial(CGContextRef ctx, const UiVecRect& r, CGGradientRef g) {
        if (g == nullptr) return;
        const float rx = (r.x1 - r.x0) * 0.5f, ry = (r.y1 - r.y0) * 0.5f;
        if (!(rx > 0.0f) || !(ry > 0.0f)) return;
        CGContextTranslateCTM(ctx, (r.x0 + r.x1) * 0.5f, (r.y0 + r.y1) * 0.5f);
        CGContextScaleCTM(ctx, 1, ry / rx);
        CGContextDrawRadialGradient(ctx, g, CGPointMake(0, 0), 0, CGPointMake(0, 0), rx, kExtend);
    }

    void padlock(CGContextRef ctx, const ui::DrawCmd& c) {
        const UiVecRect r = R(c.r);
        const float w = r.x1 - r.x0, h = r.y1 - r.y0;
        // O corpo (a metade de baixo) e a alça (um arco).
        CGMutablePathRef body = rounded_rect(UiVecRect{r.x0, r.y0 + h * 0.45f, r.x1, r.y1}, w * 0.12f, w * 0.12f);
        fill_path(ctx, body, c.c);
        CGPathRelease(body);
        const float sx0 = r.x0 + w * 0.22f, sx1 = r.x1 - w * 0.22f;
        const float sy = r.y0 + h * 0.48f, top = r.y0 + h * 0.06f;
        const float half = (sx1 - sx0) * 0.5f;
        svg::Path p;
        p.push_back(svg::PathOp{svg::PathOp::Move, {svg::Point{sx0, sy}}});
        p.push_back(svg::PathOp{svg::PathOp::Line, {svg::Point{sx0, top + half}}});
        svg::append_arc(p, (sx0 + sx1) * 0.5, top + half, half, half, 0.0, kPi, 2.0 * kPi);
        p.push_back(svg::PathOp{svg::PathOp::Line, {svg::Point{sx1, sy}}});
        CGMutablePathRef shackle = CGPathCreateMutable();
        add_path(shackle, p);
        const float lw = std::max(1.2f, w * 0.16f);
        set_stroke(ctx, c.c);
        CGContextSetLineWidth(ctx, lw);
        CGContextSetLineCap(ctx, kCGLineCapRound);
        CGContextSetLineJoin(ctx, kCGLineJoinRound);
        CGContextAddPath(ctx, shackle);
        CGContextStrokePath(ctx);
        CGPathRelease(shackle);
        touch(r.x0, r.y0, r.x1, r.y1, lw);
    }

    // O setor de anel (a roda de armas): o arco de fora de a0 a a1, a linha para o de dentro e o arco de volta.
    void sector(CGContextRef ctx, const ui::DrawCmd& c) {
        const UiVecRect r = R(c.r);
        const double cx = (r.x0 + r.x1) * 0.5, cy = (r.y0 + r.y1) * 0.5;
        const double ro = (r.x1 - r.x0) * 0.5, ri = c.radius * scale_;
        // 0 em cima, sentido horário: o ângulo da tela (de +x para +y, y para baixo) é a - 90 graus.
        const double t0 = c.a0 - kPi * 0.5, t1 = c.a1 - kPi * 0.5;
        svg::Path p;
        svg::append_arc(p, cx, cy, ro, ro, 0.0, t0, t1);
        if (ri > 0.5) {
            p.push_back(svg::PathOp{svg::PathOp::Line, {svg::Point{cx + ri * std::cos(t1), cy + ri * std::sin(t1)}}});
            svg::append_arc(p, cx, cy, ri, ri, 0.0, t1, t0);
        } else {
            p.push_back(svg::PathOp{svg::PathOp::Line, {svg::Point{cx, cy}}});
        }
        p.push_back(svg::PathOp{svg::PathOp::Close, {}});
        CGMutablePathRef path = CGPathCreateMutable();
        add_path(path, p);
        if (c.dir == 1 && ro > 0.5) {
            // Em degradê do raio de dentro (c) para o de fora (c2), centrado na roda.
            Ref<CGGradientRef> g = make_gradient(cs_, ui_vec_sector_gradient(c.c, c.c2, float(ri / ro)));
            CGContextSaveGState(ctx);
            CGContextAddPath(ctx, path);
            CGContextClip(ctx);
            if (g) CGContextDrawRadialGradient(ctx, g.get(), CGPointMake(cx, cy), 0, CGPointMake(cx, cy), ro, kExtend);
            CGContextRestoreGState(ctx);
        } else {
            fill_path(ctx, path, c.c);
        }
        const CGRect b = CGPathGetBoundingBox(path);
        touch(float(b.origin.x), float(b.origin.y), float(b.origin.x + b.size.width), float(b.origin.y + b.size.height));
        CGPathRelease(path);
    }

    void glyph(CGContextRef ctx, const ui::DrawCmd& c) {
        const UiVecRect r = R(c.r);
        const svg::Document* doc = glyph_doc(c.glyph);
        if (doc == nullptr) {
            // Sem o SVG: uma tecla clara.
            CGMutablePathRef p = rounded_rect(r, 3 * scale_, 3 * scale_);
            fill_path(ctx, p, ui::Color{0.85f, 0.82f, 0.78f, c.c.a});
            CGPathRelease(p);
            touch(r.x0, r.y0, r.x1, r.y1);
            return;
        }
        const float w = std::max(1.0f, r.x1 - r.x0), h = std::max(1.0f, r.y1 - r.y0);
        const bool layer = c.c.a < 0.999f;
        CGContextSaveGState(ctx);
        if (layer) {
            CGContextSetAlpha(ctx, std::clamp(c.c.a, 0.0f, 1.0f));
            CGContextBeginTransparencyLayer(ctx, nullptr);
        }
        draw_svg(ctx, cs_, *doc, svg::viewport(*doc, w, h).then(svg::Matrix::translate(r.x0, r.y0)));
        if (layer) CGContextEndTransparencyLayer(ctx);
        CGContextRestoreGState(ctx);
        touch(r.x0, r.y0, r.x1, r.y1, 2.0f);
    }

    const svg::Document* glyph_doc(const std::string& name) {
        if (modern_dir_.empty()) return nullptr;
        const auto it = glyphs_.find(name);
        if (it != glyphs_.end()) return it->second.get();
        std::unique_ptr<svg::Document> doc;
        const std::vector<uint8_t> data = read_file(os::join(modern_dir_, L"svg/" + std::wstring(name.begin(), name.end()) + L".svg"));
        if (!data.empty()) {
            doc = std::make_unique<svg::Document>();
            std::string err;
            if (!svg::parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), *doc, &err)) doc.reset();
        }
        const svg::Document* p = doc.get();
        glyphs_[name] = std::move(doc);
        return p;
    }

    // As linhas de um texto (uma, ou quebradas na largura wrap_w), guardadas pelo texto, a fonte e o espaçamento.
    const TextLines* text_lines(const std::wstring& s, const ui::TextStyle& st, const Font& f, float px, float wrap_w) {
        wchar_t key[96];
        std::swprintf(key, 96, L"%d|%d|%.3f|%.4f|%.2f|", int(st.face), st.weight, double(px), double(st.tracking), double(wrap_w));
        std::wstring k = key + s;
        const auto it = lines_.find(k);
        if (it != lines_.end()) return &it->second;
        Ref<CFAttributedStringRef> as = attributed(s, f.font.get(), st.tracking * px, s.size());
        if (!as) return nullptr;
        TextLines tl;
        if (wrap_w < 0.0f) {
            tl.lines.emplace_back(CTLineCreateWithAttributedString(as.get()));
        } else {
            Ref<CTTypesetterRef> ts(CTTypesetterCreateWithAttributedString(as.get()));
            if (!ts) return nullptr;
            const CFIndex n = CFAttributedStringGetLength(as.get());
            CFIndex start = 0;
            while (start < n && tl.lines.size() < 256) {
                CFIndex count = CTTypesetterSuggestLineBreak(ts.get(), start, std::max(1.0f, wrap_w));
                if (count <= 0) count = 1;
                tl.lines.emplace_back(CTTypesetterCreateLine(ts.get(), CFRangeMake(start, count)));
                start += count;
            }
        }
        for (const Ref<CTLineRef>& l : tl.lines) {
            tl.trimmed.push_back(l ? line_width(l.get(), false) : 0.0f);
            tl.full.push_back(l ? line_width(l.get(), true) : 0.0f);
        }
        if (lines_.size() > 1024) lines_.clear();
        return &(lines_[std::move(k)] = std::move(tl));
    }

    // O texto (como o IDWriteTextLayout do Direct2D): uma linha com as maiúsculas centradas no retângulo, ou quebrado na
    // largura de cima para baixo; o espaçamento depois de cada letra, compensado no alinhamento.
    void text(CGContextRef ctx, const ui::DrawCmd& c) {
        const ui::TextStyle& st = c.style;
        const std::wstring s = st.caps ? ui::to_upper(c.text) : c.text;
        const float px = st.size * scale_;
        const Font* f = modern_font(st.face, st.weight, px);
        if (f == nullptr || !f->font || s.empty()) return;
        const UiVecRect r = R(c.r);
        const float trail = st.tracking * px;
        const float dx = c.align == ui::Align::Right ? trail : (c.align == ui::Align::Centre ? trail * 0.5f : 0.0f);
        const float max_w = r.x1 - r.x0;
        const TextLines* laid = text_lines(s, st, *f, px, c.wrap ? max_w : -1.0f);
        if (laid == nullptr) return;
        set_fill(ctx, c.c);
        const float line_h = px * st.line;
        for (size_t i = 0; i < laid->lines.size(); ++i) {
            CTLineRef line = laid->lines[i].get();
            if (line == nullptr) continue;
            const float w = laid->trimmed[i];
            float x = r.x0;
            if (c.align == ui::Align::Centre) x = r.x0 + (max_w - w) * 0.5f;
            else if (c.align == ui::Align::Right) x = r.x1 - w;
            x += dx;
            // A linha de base: numa linha só, as maiúsculas centradas no retângulo; quebrado, a 80% da altura da linha de
            // cada uma (o espaçamento uniforme do DirectWrite). Presa ao pixel na vertical, como o Direct2D.
            const float baseline = c.wrap ? r.y0 + float(i) * line_h + line_h * 0.8f
                                          : (r.y0 + r.y1) * 0.5f + f->cap * px * 0.5f;
            const float by = std::round(baseline);
            CGContextSaveGState(ctx);
            CGContextTranslateCTM(ctx, x, by);
            CGContextScaleCTM(ctx, 1, -1);
            CGContextSetTextPosition(ctx, 0, 0);
            draw_glyphs(ctx, line, 0.0f, [](CFIndex) { return Ink{}; });
            CGContextRestoreGState(ctx);
            touch(x - px, by - px * 1.3f, x + laid->full[i] + px, by + px * 0.6f);
        }
    }

    // ---------------------------------------------------------------- camada HD

    struct Hasher {
        std::vector<uint8_t> b;
        void raw(const void* p, size_t n) {
            const uint8_t* q = static_cast<const uint8_t*>(p);
            b.insert(b.end(), q, q + n);
        }
        void f(float v) { raw(&v, sizeof(v)); }
        void i(int64_t v) { raw(&v, sizeof(v)); }
        void str(const std::wstring& s) {
            i(int64_t(s.size()));
            for (const wchar_t c : s) i(int64_t(c));
        }
    };

    template <class F>
    uint64_t hd_hash(size_t n, const F& each, int w, int h) {
        Hasher hs;
        hs.i(w);
        hs.i(h);
        for (size_t k = 0; k < n; ++k) each(k, hs);
        // FNV-1a de 64 bits sobre os bytes (um lote tem poucos textos).
        uint64_t x = 1469598103934665603ull;
        for (const uint8_t v : hs.b) x = (x ^ v) * 1099511628211ull;
        return x;
    }

    static void hash_step(Hasher& h, const UiHdText* t, const UiHdKey* k) {
        if (t != nullptr) {
            h.i(1);
            h.i(t->font);
            h.f(t->cap_px);
            h.i(t->align);
            h.i(int64_t(t->alpha));
            for (const float v : t->clip) h.f(v);
            for (const UiHdLine& l : t->lines) {
                h.str(l.text);
                h.f(l.x0);
                h.f(l.x1);
                h.f(l.baseline);
                h.i(l.pinned ? 1 : 0);
                for (const UiHdSpan& s : l.spans) {
                    h.i(s.start);
                    h.i(s.length);
                    h.raw(s.rgba, 4);
                }
            }
            for (const UiHdEffect& e : t->effects) {
                h.i(e.kind);
                h.f(e.dx);
                h.f(e.dy);
                h.f(e.width);
                h.raw(e.rgba, 4);
            }
        }
        if (k != nullptr) {
            h.i(2);
            h.f(k->x0);
            h.f(k->y0);
            h.f(k->x1);
            h.f(k->y1);
            h.str(k->label);
            h.f(k->cap_px);
            h.raw(k->rgba, 4);
            for (const float v : k->clip) h.f(v);
        }
    }

    // Um texto da camada HD (UiHdDevice::draw_text): as linhas na fonte do map.json, com a altura das maiúsculas da fonte
    // do jogo, o bloco alinhado e comprimido como o original, os efeitos antes (as cópias deslocadas, o traço) e a cor de
    // cada trecho.
    void draw_text(CGContextRef ctx, const UiHdText& t) {
        if (t.lines.empty()) return;
        const int font = t.font >= 1 && t.font <= 3 ? t.font : 2;
        const UiHdFont def;
        const UiHdFont& cfg = config_ != nullptr ? config_->fonts[font] : def;
        const HdFont& hf = hd_font(font);
        const float em = ui_hd_em(t.cap_px, hf.cap, cfg.size);
        const Font* f = hd_sized(font, em);
        if (f == nullptr || !f->font) return;
        std::vector<Ref<CTLineRef>> lines(t.lines.size());
        std::vector<float> widths(t.lines.size(), 0.0f);
        for (size_t i = 0; i < t.lines.size(); ++i) {
            const std::wstring& s = t.lines[i].text;
            if (s.empty()) continue;
            // O espaço depois de cada caractere menos o último (a borda esquerda não muda).
            Ref<CFAttributedStringRef> as = attributed(s, f->font.get(), cfg.spacing * em, s.size() > 1 ? s.size() - 1 : 0);
            if (!as) continue;
            lines[i].reset(CTLineCreateWithAttributedString(as.get()));
            if (lines[i]) widths[i] = line_width(lines[i].get(), false);
        }
        const std::vector<UiHdLinePlace> place = ui_hd_place_lines(t, widths, cfg.fit_min);
        const UiVecRect clip = ui_vec_aliased_clip(UiVecRect{t.clip[0], t.clip[1], t.clip[2], t.clip[3]});
        CGContextSaveGState(ctx);
        CGContextClipToRect(ctx, CGRectMake(clip.x0, clip.y0, std::max(0.0f, clip.x1 - clip.x0), std::max(0.0f, clip.y1 - clip.y0)));
        const auto draw_all = [&](float dx, float dy, const Ink* over, float stroke) {
            for (size_t i = 0; i < t.lines.size(); ++i) {
                if (!lines[i]) continue;
                const UiHdLine& line = t.lines[i];
                CGContextSaveGState(ctx);
                CGContextTranslateCTM(ctx, place[i].x + dx, std::round(line.baseline + dy));
                CGContextScaleCTM(ctx, place[i].sx, -1);
                CGContextSetTextPosition(ctx, 0, 0);
                if (over != nullptr) {
                    CGContextSetRGBFillColor(ctx, over->r, over->g, over->b, over->a);
                    CGContextSetRGBStrokeColor(ctx, over->r, over->g, over->b, over->a);
                    draw_glyphs(ctx, lines[i].get(), stroke, [](CFIndex) { return Ink{}; });
                } else {
                    // A cor de cada trecho; fora deles, branco (o pincel de reserva do Direct2D).
                    draw_glyphs(ctx, lines[i].get(), 0.0f, [&line](CFIndex idx) {
                        Ink k;
                        k.set = true;
                        for (const UiHdSpan& sp : line.spans) {
                            if (idx >= CFIndex(sp.start) && idx < CFIndex(sp.start + sp.length)) {
                                float c[4];
                                ui_hd_gs_colour(sp.rgba, c);
                                k.r = c[0];
                                k.g = c[1];
                                k.b = c[2];
                                k.a = c[3];
                                return k;
                            }
                        }
                        return k;
                    });
                }
                CGContextRestoreGState(ctx);
            }
        };
        for (const UiHdEffect& e : t.effects) {
            float c[4];
            ui_hd_gs_colour(e.rgba, c);
            const Ink ink{true, c[0], c[1], c[2], c[3]};
            draw_all(e.dx, e.dy, &ink, e.kind == UiHdEffect::Stroke ? std::max(0.5f, e.width) : 0.0f);
        }
        draw_all(0.0f, 0.0f, nullptr, 0.0f);
        CGContextRestoreGState(ctx);
    }

    // Uma tecla (UiHdDevice::draw_key): a tecla arredondada escura com a borda clara e o texto centrado.
    void draw_key(CGContextRef ctx, const UiHdKey& k) {
        if (k.label.empty()) return;
        const float w = k.x1 - k.x0, h = k.y1 - k.y0;
        if (!(w > 1.0f) || !(h > 1.0f)) return;
        const UiHdKeyShape shape = ui_hd_key_shape(k);
        const UiVecRect clip = ui_vec_aliased_clip(UiVecRect{k.clip[0], k.clip[1], k.clip[2], k.clip[3]});
        CGContextSaveGState(ctx);
        CGContextClipToRect(ctx, CGRectMake(clip.x0, clip.y0, std::max(0.0f, clip.x1 - clip.x0), std::max(0.0f, clip.y1 - clip.y0)));
        CGMutablePathRef body = rounded_rect(shape.body, shape.radius, shape.radius);
        Ref<CGGradientRef> g = make_gradient(cs_, {shape.stops[0], shape.stops[1], shape.stops[2]});
        CGContextSaveGState(ctx);
        CGContextAddPath(ctx, body);
        CGContextClip(ctx);
        if (g) CGContextDrawLinearGradient(ctx, g.get(), CGPointMake(0, k.y0), CGPointMake(0, k.y1), kExtend);
        CGContextRestoreGState(ctx);
        float edge[4];
        ui_hd_gs_colour(shape.edge, edge);
        CGContextSetRGBStrokeColor(ctx, edge[0], edge[1], edge[2], edge[3]);
        CGContextSetLineWidth(ctx, shape.line);
        CGContextAddPath(ctx, body);
        CGContextStrokePath(ctx);
        CGPathRelease(body);
        // O texto: a altura das maiúsculas pedida, centrado na tecla, comprimido se não couber.
        const HdFont& hf = hd_font(2);
        const UiHdFont def;
        const UiHdFont& cfg = config_ != nullptr ? config_->fonts[2] : def;
        const Font* f = hd_sized(2, ui_hd_em(k.cap_px, hf.cap, cfg.size));
        if (f != nullptr && f->font) {
            Ref<CFAttributedStringRef> as = attributed(k.label, f->font.get(), 0.0f, 0);
            Ref<CTLineRef> line(as ? CTLineCreateWithAttributedString(as.get()) : nullptr);
            if (line) {
                const float tw = line_width(line.get(), false);
                const float sx = tw > w - 2.0f * shape.line && tw > 0.0f ? (w - 2.0f * shape.line) / tw : 1.0f;
                const float x = (k.x0 + k.x1) * 0.5f - tw * sx * 0.5f;
                const float baseline = (k.y0 + k.y1) * 0.5f + k.cap_px * 0.5f;
                float ink[4];
                ui_hd_gs_colour(shape.ink, ink);
                CGContextSaveGState(ctx);
                CGContextTranslateCTM(ctx, x, std::round(baseline));
                CGContextScaleCTM(ctx, sx, -1);
                CGContextSetTextPosition(ctx, 0, 0);
                CGContextSetRGBFillColor(ctx, ink[0], ink[1], ink[2], ink[3]);
                draw_glyphs(ctx, line.get(), 0.0f, [](CFIndex) { return Ink{}; });
                CGContextRestoreGState(ctx);
            }
        }
        CGContextRestoreGState(ctx);
    }
};

std::unique_ptr<UiVectorCanvas> create_ui_vector_canvas() {
    auto c = std::make_unique<UiVectorCg>();
    if (!c->ok()) return nullptr;
    return c;
}

}  // namespace nf
