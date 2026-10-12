#pragma once
// As funções matemáticas de float com o mesmo resultado em 32 e 64 bits (docs/port.md, "64 bits").
//
// No C runtime de 32 bits (x86) da Microsoft, sinf, cosf, tanf, asinf, acosf, atanf, atan2f, expf, logf, log10f e
// powf são inline: calculam na versão double e arredondam para float. No de 64 bits elas são funções próprias, que
// diferem do x86 em 1 ulp numa parte dos casos (medido em 2 milhões de entradas por função: asinf 6%, acosf 2%,
// sinf/cosf/log10f 0,1%, tanf, expf, powf, atan2f menos; atanf, sqrtf e fmodf iguais). As versões double dão o mesmo
// resultado nos dois (0 diferenças nas mesmas entradas). Então nf::fm::sin(float) = (float)sin((double)x) é, em x86,
// exatamente o que std::sin(float) já fazia, e em x64 dá o resultado do x86. Com argumentos que não são float, as
// funções chamam std:: como antes (as mesmas regras de promoção).
//
// log2f, exp2f e hypotf não entram: no x86 elas também são funções float próprias. hypotf deu o resultado da double
// nos dois (0 diferenças em 2 milhões); log2f e exp2f diferem da double, mas o port só as usa na barra da sensibilidade
// do mouse, cujas 61 posições dão os mesmos valores nos dois.

#include <cmath>
#include <type_traits>

namespace nf::fm {

#define NF_FM_UNARY(name)                                       \
    template <class A>                                          \
    inline auto name(A a) {                                     \
        if constexpr (std::is_same_v<A, float>) {               \
            return static_cast<float>(std::name(double(a)));    \
        } else {                                                \
            return std::name(a);                                \
        }                                                       \
    }
NF_FM_UNARY(sin)
NF_FM_UNARY(cos)
NF_FM_UNARY(tan)
NF_FM_UNARY(asin)
NF_FM_UNARY(acos)
NF_FM_UNARY(atan)
NF_FM_UNARY(exp)
NF_FM_UNARY(log)
NF_FM_UNARY(log10)
#undef NF_FM_UNARY

template <class A, class B>
inline auto atan2(A a, B b) {
    if constexpr (std::is_same_v<A, float> && std::is_same_v<B, float>) {
        return static_cast<float>(std::atan2(double(a), double(b)));
    } else {
        return std::atan2(a, b);
    }
}

template <class A, class B>
inline auto pow(A a, B b) {
    if constexpr (std::is_same_v<A, float> && std::is_same_v<B, float>) {
        return static_cast<float>(std::pow(double(a), double(b)));
    } else {
        return std::pow(a, b);
    }
}

}  // namespace nf::fm
