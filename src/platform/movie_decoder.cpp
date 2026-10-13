// O decodificador de MPEG-2 dos filmes: a parte sem o sistema (a escolha do decodificador, o do port por trás da
// fachada, a conversão de cor do IPU e a fila de quadros). O do sistema é o do backend (movie_decoder_mf.cpp: o Media
// Foundation do Windows). Ver movie_decoder.h.

#include "platform/movie_decoder.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "platform/mpeg2_decoder.h"

// SSE2 (todo x86-64 e o x86 de 32 bits com SSE2, o padrão do MSVC): a conversão de cor e o croma intercalado em
// vetores, com as mesmas contas inteiras (o mesmo resultado bit a bit). Nas outras CPUs, o C++ puro.
// -DNF_MOVIE_SSE2=0 força o C++ puro também no x86.
#ifndef NF_MOVIE_SSE2
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define NF_MOVIE_SSE2 1
#else
#define NF_MOVIE_SSE2 0
#endif
#endif
#if NF_MOVIE_SSE2
#include <emmintrin.h>
#endif

namespace nf {

namespace {

// Os coeficientes do CSC do IPU, em 1/128 (Y 1.164, Cr->R 1.594, Cb->G -0.391, Cr->G -0.8125, Cb->B 2.016): BT.601 de
// faixa limitada.
constexpr int kY = 149, kRCr = 204, kGCb = -50, kGCr = -104, kBCb = 258;

uint8_t clamp8(int v) { return uint8_t(std::clamp(v, 0, 255)); }

}  // namespace

void yuv_to_bgr(int y, int cb, int cr, uint8_t* bgr) {
    const int yy = kY * (y - 16);
    const int u = cb - 128, v = cr - 128;
    bgr[0] = clamp8((yy + kBCb * u + 64) >> 7);
    bgr[1] = clamp8((yy + kGCb * u + kGCr * v + 64) >> 7);
    bgr[2] = clamp8((yy + kRCr * v + 64) >> 7);
}

#if NF_MOVIE_SSE2
namespace {

// 16 pixels de uma linha com as contas de nv12_to_bgra: os termos em 32 bits (pmaddwd: Y x 149 + 64 e o croma pelos
// pares (u, v)), o mesmo >> 7, e a saturação do packs/packus no lugar do clamp8 (os valores cabem em 16 bits).
inline void bgra16_sse2(const uint8_t* yr, const uint8_t* uvr, uint8_t* out, __m128i alpha) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i c128 = _mm_set1_epi16(128), c16 = _mm_set1_epi16(16), one = _mm_set1_epi16(1);
    const __m128i ky = _mm_set_epi16(64, kY, 64, kY, 64, kY, 64, kY);
    const __m128i kb = _mm_set_epi16(0, kBCb, 0, kBCb, 0, kBCb, 0, kBCb);
    const __m128i kg = _mm_set_epi16(kGCr, kGCb, kGCr, kGCb, kGCr, kGCb, kGCr, kGCb);
    const __m128i kr = _mm_set_epi16(kRCr, 0, kRCr, 0, kRCr, 0, kRCr, 0);
    const __m128i uv = _mm_loadu_si128(reinterpret_cast<const __m128i*>(uvr));
    const __m128i uv_lo = _mm_sub_epi16(_mm_unpacklo_epi8(uv, zero), c128);  // u0 v0 u1 v1 u2 v2 u3 v3
    const __m128i uv_hi = _mm_sub_epi16(_mm_unpackhi_epi8(uv, zero), c128);  // u4 v4 ... u7 v7
    const __m128i cb[2] = {_mm_madd_epi16(uv_lo, kb), _mm_madd_epi16(uv_hi, kb)};
    const __m128i cg[2] = {_mm_madd_epi16(uv_lo, kg), _mm_madd_epi16(uv_hi, kg)};
    const __m128i cr[2] = {_mm_madd_epi16(uv_lo, kr), _mm_madd_epi16(uv_hi, kr)};
    const __m128i yv = _mm_loadu_si128(reinterpret_cast<const __m128i*>(yr));
    const __m128i y16[2] = {_mm_sub_epi16(_mm_unpacklo_epi8(yv, zero), c16),
                            _mm_sub_epi16(_mm_unpackhi_epi8(yv, zero), c16)};
    __m128i b16[2], g16[2], r16[2];
    for (int h = 0; h < 2; ++h) {
        // Os 8 pixels desta metade: 4 amostras de croma, cada uma para 2 pixels.
        const __m128i yy0 = _mm_madd_epi16(_mm_unpacklo_epi16(y16[h], one), ky);  // pixels 0-3: Y x 149 + 64
        const __m128i yy1 = _mm_madd_epi16(_mm_unpackhi_epi16(y16[h], one), ky);  // pixels 4-7
        const __m128i b0 = _mm_shuffle_epi32(cb[h], _MM_SHUFFLE(1, 1, 0, 0)), b1 = _mm_shuffle_epi32(cb[h], _MM_SHUFFLE(3, 3, 2, 2));
        const __m128i g0 = _mm_shuffle_epi32(cg[h], _MM_SHUFFLE(1, 1, 0, 0)), g1 = _mm_shuffle_epi32(cg[h], _MM_SHUFFLE(3, 3, 2, 2));
        const __m128i r0 = _mm_shuffle_epi32(cr[h], _MM_SHUFFLE(1, 1, 0, 0)), r1 = _mm_shuffle_epi32(cr[h], _MM_SHUFFLE(3, 3, 2, 2));
        b16[h] = _mm_packs_epi32(_mm_srai_epi32(_mm_add_epi32(yy0, b0), 7), _mm_srai_epi32(_mm_add_epi32(yy1, b1), 7));
        g16[h] = _mm_packs_epi32(_mm_srai_epi32(_mm_add_epi32(yy0, g0), 7), _mm_srai_epi32(_mm_add_epi32(yy1, g1), 7));
        r16[h] = _mm_packs_epi32(_mm_srai_epi32(_mm_add_epi32(yy0, r0), 7), _mm_srai_epi32(_mm_add_epi32(yy1, r1), 7));
    }
    const __m128i b = _mm_packus_epi16(b16[0], b16[1]);
    const __m128i g = _mm_packus_epi16(g16[0], g16[1]);
    const __m128i r = _mm_packus_epi16(r16[0], r16[1]);
    const __m128i bg_lo = _mm_unpacklo_epi8(b, g), bg_hi = _mm_unpackhi_epi8(b, g);
    const __m128i ra_lo = _mm_unpacklo_epi8(r, alpha), ra_hi = _mm_unpackhi_epi8(r, alpha);
    __m128i* o = reinterpret_cast<__m128i*>(out);
    _mm_storeu_si128(o + 0, _mm_unpacklo_epi16(bg_lo, ra_lo));
    _mm_storeu_si128(o + 1, _mm_unpackhi_epi16(bg_lo, ra_lo));
    _mm_storeu_si128(o + 2, _mm_unpacklo_epi16(bg_hi, ra_hi));
    _mm_storeu_si128(o + 3, _mm_unpackhi_epi16(bg_hi, ra_hi));
}

}  // namespace
#endif

void nv12_to_bgra(const uint8_t* nv12, int width, int height, uint8_t* bgra, uint8_t alpha) {
    const uint8_t* uv_plane = nv12 + size_t(width) * size_t(height);
#if NF_MOVIE_SSE2
    const __m128i va = _mm_set1_epi8(char(alpha));
#endif
    for (int y = 0; y < height; ++y) {
        const uint8_t* yr = nv12 + size_t(y) * size_t(width);
        const uint8_t* uvr = uv_plane + size_t(y / 2) * size_t(width);
        uint8_t* out = bgra + size_t(y) * size_t(width) * 4;
        int x = 0;
#if NF_MOVIE_SSE2
        for (; x + 16 <= width; x += 16, out += 64) bgra16_sse2(yr + x, uvr + x, out, va);
#endif
        for (; x < width; x += 2) {
            // O croma de cada bloco de 2 x 2 vale para os quatro pixels (como o CSC do IPU, sem interpolar).
            const int u = int(uvr[x]) - 128, v = int(uvr[x + 1]) - 128;
            const int b = kBCb * u + 64, g = kGCb * u + kGCr * v + 64, r = kRCr * v + 64;
            for (int k = 0; k < 2 && x + k < width; ++k) {
                const int yy = kY * (int(yr[x + k]) - 16);
                out[0] = clamp8((yy + b) >> 7);
                out[1] = clamp8((yy + g) >> 7);
                out[2] = clamp8((yy + r) >> 7);
                out[3] = alpha;
                out += 4;
            }
        }
    }
}

const char* const kOwnMpeg2DecoderName = "MPEG-2 do port";

bool parse_movie_decoder_choice(const std::string& text, MovieDecoderChoice* out) {
    std::string t;
    for (char c : text) {
        if (c != ' ' && c != '	') t += char(std::tolower(static_cast<unsigned char>(c)));
    }
    if (t == "auto") *out = MovieDecoderChoice::Auto;
    else if (t == "mf" || t == "system") *out = MovieDecoderChoice::System;
    else if (t == "own" || t == "port") *out = MovieDecoderChoice::Own;
    else return false;
    return true;
}

const char* movie_decoder_choice_name(MovieDecoderChoice c) {
    switch (c) {
        case MovieDecoderChoice::System: return "mf";
        case MovieDecoderChoice::Own: return "own";
        default: return "auto";
    }
}

namespace {

// O decodificador do port (platform/mpeg2_decoder.h) por trás da fachada: as imagens dele viram NV12 (o croma
// intercalado) na fila.
class OwnBackend final : public Mpeg2Decoder::Backend {
public:
    OwnBackend() {
        dec_.set_output([this](const Mpeg2Picture& p) { picture(p); });
    }
    bool feed(Mpeg2Decoder& d, const uint8_t* data, size_t size) override {
        sink_ = &d;
        dec_.decode(data, size);
        sink_ = nullptr;
        return true;
    }
    void drain(Mpeg2Decoder& d) override {
        sink_ = &d;
        dec_.finish();
        sink_ = nullptr;
    }
    void flush() override { dec_.reset(); }

private:
    void picture(const Mpeg2Picture& p) {
        if (sink_ == nullptr) return;
        MovieFrameNV12 f = reuse_frame(*sink_);
        // NV12 com o croma em pares: largura e altura pares (os filmes do jogo são 512 x 448).
        const int w = p.width & ~1, h = p.height & ~1;
        f.width = w;
        f.height = h;
        f.data.resize(size_t(w) * size_t(h) * 3 / 2);
        for (int y = 0; y < h; ++y) std::memcpy(&f.data[size_t(y) * size_t(w)], p.y + size_t(y) * size_t(p.y_stride), size_t(w));
        uint8_t* uv = f.data.data() + size_t(w) * size_t(h);
        for (int y = 0; y < h / 2; ++y) {
            const uint8_t* cb = p.cb + size_t(y) * size_t(p.c_stride);
            const uint8_t* cr = p.cr + size_t(y) * size_t(p.c_stride);
            uint8_t* o = uv + size_t(y) * size_t(w);
            int x = 0;
#if NF_MOVIE_SSE2
            for (; x + 16 <= w / 2; x += 16) {
                const __m128i u = _mm_loadu_si128(reinterpret_cast<const __m128i*>(cb + x));
                const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(cr + x));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(o + 2 * x), _mm_unpacklo_epi8(u, v));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(o + 2 * x + 16), _mm_unpackhi_epi8(u, v));
            }
#endif
            for (; x < w / 2; ++x) {
                o[2 * x] = cb[x];
                o[2 * x + 1] = cr[x];
            }
        }
        push_frame(*sink_, std::move(f));
    }

    Mpeg2VideoDecoder dec_;
    Mpeg2Decoder* sink_ = nullptr;
};

}  // namespace

Mpeg2Decoder::Mpeg2Decoder() = default;

Mpeg2Decoder::~Mpeg2Decoder() { close(); }

void Mpeg2Decoder::close() {
    backend_.reset();
    own_ = false;
    frames_.clear();
    spare_.clear();
}

bool Mpeg2Decoder::open(int width, int height, double fps, MovieDecoderChoice choice) {
    close();
    name_.clear();
    failure_.clear();
    if (choice != MovieDecoderChoice::Own) {
        backend_ = open_system_mpeg2_decoder(width, height, fps, &name_, &failure_);
        if (backend_ != nullptr) return true;
        if (choice == MovieDecoderChoice::System) {
            name_.clear();
            return false;
        }
    }
    backend_ = std::make_unique<OwnBackend>();
    own_ = true;
    name_ = kOwnMpeg2DecoderName;
    return true;
}

bool Mpeg2Decoder::feed(const uint8_t* data, size_t size) {
    if (backend_ == nullptr || size == 0) return false;
    return backend_->feed(*this, data, size);
}

void Mpeg2Decoder::drain() {
    if (backend_ != nullptr) backend_->drain(*this);
}

void Mpeg2Decoder::flush() {
    if (backend_ == nullptr) return;
    backend_->flush();
    for (MovieFrameNV12& f : frames_) spare_.push_back(std::move(f));
    frames_.clear();
}

bool Mpeg2Decoder::take(MovieFrameNV12& out) {
    if (frames_.empty()) return false;
    if (!out.data.empty()) spare_.push_back(std::move(out));
    out = std::move(frames_.front());
    frames_.pop_front();
    return true;
}

MovieFrameNV12 Mpeg2Decoder::reuse_frame() {
    MovieFrameNV12 f;
    if (!spare_.empty()) {
        f = std::move(spare_.back());
        spare_.pop_back();
    }
    return f;
}

void Mpeg2Decoder::push_frame(MovieFrameNV12&& f) {
    frames_.push_back(std::move(f));
    ++frames_out_;
}

}  // namespace nf
