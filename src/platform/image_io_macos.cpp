// Imagens em arquivo no macOS: o backend de platform/image_io.h com o ImageIO (CGImageDestination, CGImageSource; docs/
// port.md, "macOS: os backends"). A mesma semântica do backend do WIC (image_io_wic.cpp): o PNG gravado tem os bytes
// dados (RGBA ou BGRA, o alfa sem pré-multiplicar, 8 bits por canal; as capturas, que a etapa 3e compara com as do
// Windows), e a leitura dá BGRA sem pré-multiplicar.
//
// Gravar: um CGImage sobre os próprios pixels (sem cópia), no espaço sRGB (só a etiqueta do arquivo: os valores vão como
// estão), com o alfa "não pré-multiplicado" que o CGImage aceita (o CGBitmapContext não aceitaria), para o codificador de
// PNG. Ler: os bytes decodificados do PNG direto do CGImage (CGDataProviderCopyData), sem gerenciamento de cor nem a
// pré-multiplicação de um desenho, quando o formato é RGB de 8 bits (o caso das substitutas PNG e das capturas); outros
// formatos (paleta, 16 bits, cinza) são desenhados num contexto RGBA pré-multiplicado e o alfa é desfeito (exato nos
// pixels opacos; nos semitransparentes, o arredondamento da pré-multiplicação).

#include "platform/image_io.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <stdexcept>
#include <string>

#include "platform/os.h"

namespace nf {

namespace {

// Libera um objeto do Core Foundation no fim do escopo.
template <class T>
struct CfRef {
    T ref = nullptr;
    CfRef() = default;
    explicit CfRef(T r) : ref(r) {}
    ~CfRef() {
        if (ref != nullptr) CFRelease(ref);
    }
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
    explicit operator bool() const { return ref != nullptr; }
};

CFURLRef file_url(const std::wstring& path) {
    const std::string utf8 = os::to_utf8(path);
    CfRef<CFStringRef> str(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(utf8.data()),
                                                   CFIndex(utf8.size()), kCFStringEncodingUTF8, false));
    if (!str) return nullptr;
    return CFURLCreateWithFileSystemPath(nullptr, str.ref, kCFURLPOSIXPathStyle, false);
}

// O PNG de pixels de 32 bits (bgra: B, G, R, A na memória; senão R, G, B, A). Vazio se gravou; senão o passo que falhou.
std::string write_png(const std::wstring& path, int width, int height, const uint8_t* pixels, bool bgra) {
    if (width <= 0 || height <= 0 || pixels == nullptr) return "tamanho";
    CfRef<CFURLRef> url(file_url(path));
    if (!url) return "caminho";
    CfRef<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    if (!space) return "espaço de cor";
    const size_t row = size_t(width) * 4;
    CfRef<CGDataProviderRef> provider(CGDataProviderCreateWithData(nullptr, pixels, row * size_t(height), nullptr));
    if (!provider) return "CGDataProvider";
    // BGRA: a palavra de 32 bits ARGB em little-endian (os bytes B, G, R, A); RGBA: os bytes na ordem dos componentes.
    const CGBitmapInfo info = bgra ? CGBitmapInfo(kCGBitmapByteOrder32Little) | CGBitmapInfo(kCGImageAlphaFirst)
                                   : CGBitmapInfo(kCGBitmapByteOrderDefault) | CGBitmapInfo(kCGImageAlphaLast);
    CfRef<CGImageRef> image(CGImageCreate(size_t(width), size_t(height), 8, 32, row, space.ref, info, provider.ref, nullptr,
                                          false, kCGRenderingIntentDefault));
    if (!image) return "CGImageCreate";
    CfRef<CGImageDestinationRef> dest(CGImageDestinationCreateWithURL(url.ref, CFSTR("public.png"), 1, nullptr));
    if (!dest) return "arquivo PNG";
    CGImageDestinationAddImage(dest.ref, image.ref, nullptr);
    if (!CGImageDestinationFinalize(dest.ref)) return "CGImageDestinationFinalize";
    return {};
}

// Os bytes decodificados de um CGImage RGB de 8 bits em BGRA; false se o formato não é um destes (o caminho do desenho).
bool raw_bgra(CGImageRef image, size_t w, size_t h, std::vector<uint8_t>* out) {
    if (CGImageGetBitsPerComponent(image) != 8) return false;
    CGColorSpaceRef space = CGImageGetColorSpace(image);
    if (space == nullptr || CGColorSpaceGetModel(space) != kCGColorSpaceModelRGB) return false;
    const size_t bpp = CGImageGetBitsPerPixel(image);
    const CGBitmapInfo info = CGImageGetBitmapInfo(image);
    if ((info & kCGBitmapFloatComponents) != 0) return false;
    const CGImageAlphaInfo alpha = CGImageAlphaInfo(info & kCGBitmapAlphaInfoMask);
    const CGBitmapInfo order = info & kCGBitmapByteOrderMask;
    // A posição de R, G, B e A (-1: sem alfa, opaco) em cada pixel.
    int r = 0, g = 1, b = 2, a = -1;
    size_t stride = 0;
    if (bpp == 24 && alpha == kCGImageAlphaNone && order == kCGBitmapByteOrderDefault) {
        stride = 3;
    } else if (bpp == 32) {
        stride = 4;
        const bool last = alpha == kCGImageAlphaLast || alpha == kCGImageAlphaNoneSkipLast;
        const bool first = alpha == kCGImageAlphaFirst || alpha == kCGImageAlphaNoneSkipFirst;
        if (!last && !first) return false;  // pré-multiplicado ou sem alfa em 32 bits: o caminho do desenho
        const bool has_alpha = alpha == kCGImageAlphaLast || alpha == kCGImageAlphaFirst;
        if (order == kCGBitmapByteOrderDefault || order == kCGBitmapByteOrder32Big) {
            if (last) {
                r = 0, g = 1, b = 2, a = 3;
            } else {
                a = 0, r = 1, g = 2, b = 3;
            }
        } else if (order == kCGBitmapByteOrder32Little) {
            if (last) {
                a = 0, b = 1, g = 2, r = 3;
            } else {
                b = 0, g = 1, r = 2, a = 3;
            }
        } else {
            return false;
        }
        if (!has_alpha) a = -1;
    } else {
        return false;
    }
    CGDataProviderRef provider = CGImageGetDataProvider(image);
    if (provider == nullptr) return false;
    CfRef<CFDataRef> data(CGDataProviderCopyData(provider));
    if (!data) return false;
    const size_t row = CGImageGetBytesPerRow(image);
    const uint8_t* src = CFDataGetBytePtr(data.ref);
    if (src == nullptr || size_t(CFDataGetLength(data.ref)) < row * (h - 1) + w * stride) return false;
    out->assign(w * h * 4, 0);
    for (size_t y = 0; y < h; ++y) {
        const uint8_t* p = src + y * row;
        uint8_t* q = out->data() + y * w * 4;
        for (size_t x = 0; x < w; ++x, p += stride, q += 4) {
            q[0] = p[b];
            q[1] = p[g];
            q[2] = p[r];
            q[3] = a >= 0 ? p[a] : 255;
        }
    }
    return true;
}

// Qualquer outro formato: desenhado num RGBA pré-multiplicado (o único com alfa que o CGBitmapContext aceita), o alfa
// desfeito.
bool drawn_bgra(CGImageRef image, size_t w, size_t h, std::vector<uint8_t>* out) {
    CfRef<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    if (!space) return false;
    std::vector<uint8_t> rgba(w * h * 4, 0);
    CfRef<CGContextRef> ctx(CGBitmapContextCreate(rgba.data(), w, h, 8, w * 4, space.ref,
                                                  CGBitmapInfo(kCGBitmapByteOrderDefault) |
                                                      CGBitmapInfo(kCGImageAlphaPremultipliedLast)));
    if (!ctx) return false;
    CGContextSetBlendMode(ctx.ref, kCGBlendModeCopy);
    CGContextDrawImage(ctx.ref, CGRectMake(0, 0, CGFloat(w), CGFloat(h)), image);
    out->assign(w * h * 4, 0);
    for (size_t i = 0; i < w * h; ++i) {
        const uint8_t* p = rgba.data() + i * 4;
        uint8_t* q = out->data() + i * 4;
        const unsigned al = p[3];
        const auto un = [al](unsigned c) -> uint8_t {
            if (al == 0) return 0;
            if (al == 255) return uint8_t(c);
            const unsigned v = (c * 255u + al / 2) / al;
            return uint8_t(v > 255u ? 255u : v);
        };
        q[0] = un(p[2]);
        q[1] = un(p[1]);
        q[2] = un(p[0]);
        q[3] = uint8_t(al);
    }
    return true;
}

}  // namespace

bool write_png_rgba(const std::wstring& path, int width, int height, const uint8_t* rgba) {
    return write_png(path, width, height, rgba, false).empty();
}

void write_png_bgra(const std::wstring& path, int width, int height, const uint8_t* bgra) {
    const std::string failed = write_png(path, width, height, bgra, true);
    if (!failed.empty()) throw std::runtime_error("ImageIO: " + failed + " falhou");
}

bool read_image_bgra(const std::wstring& path, int max_side, int* width, int* height, std::vector<uint8_t>* bgra) {
    CfRef<CFURLRef> url(file_url(path));
    if (!url) return false;
    CfRef<CGImageSourceRef> source(CGImageSourceCreateWithURL(url.ref, nullptr));
    if (!source || CGImageSourceGetCount(source.ref) < 1) return false;
    CfRef<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.ref, 0, nullptr));
    if (!image) return false;
    const size_t w = CGImageGetWidth(image.ref), h = CGImageGetHeight(image.ref);
    if (w == 0 || h == 0 || w > size_t(max_side) || h > size_t(max_side)) return false;
    if (!raw_bgra(image.ref, w, h, bgra) && !drawn_bgra(image.ref, w, h, bgra)) return false;
    *width = int(w);
    *height = int(h);
    return true;
}

}  // namespace nf
