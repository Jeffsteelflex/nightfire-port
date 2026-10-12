#pragma once
// A aritmética de ponto flutuante do PS2, bit a bit: a FPU do EE (COP1) e as VUs (COP2 em macro e os
// microprogramas). Serve ao código portado que precisa dar os mesmos bits do PS2 (a física do DRIVING.ELF,
// o interpretador das VUs em platform/vu_interp.h) e não depende de jogo nenhum.
//
// O formato é o IEEE de 32 bits, mas as unidades do PS2 não são IEEE:
//  - não há denormais: uma entrada com expoente 0 vale ±0, e um resultado pequeno demais vira ±0 (underflow,
//    com o sinal do resultado);
//  - não há infinitos nem NaN: o expoente 255 é um número normal (até (2 - 2^-23) x 2^128), e um resultado
//    grande demais satura em ±0x7fffffff, o maior módulo do formato (overflow);
//  - o arredondamento é sempre para zero (truncamento).
// Aqui o resultado é o valor exato (soma ou produto em double, que é exato para dois floats de 24 bits, ou o
// quociente e a raiz em double, que nunca cruzam a fronteira de um float sem ser exatos) truncado para 24
// bits. Aproximações conhecidas, que não mudam nada na física (docs/driving.md, seção 11):
//  - o somador do PS2 alinha o menor operando sem bits de guarda: numa subtração em que o menor operando perde
//    bits no alinhamento, o PS2 pode dar 1 ulp acima do truncamento exato daqui; o multiplicador (Booth) também
//    difere do produto truncado em casos raros. O PCSX2 (rodando com "chop", o padrão) faz como aqui;
//  - o PCSX2 e o interpretador MIPS do port (data/mips_interp.cpp) saturam em ±FLT_MAX (0x7f7fffff) e leem o
//    expoente 255 como ±FLT_MAX; aqui é o valor do hardware (0x7fffffff, expoente 255 normal);
//  - as funções da EFU da VU1 (ESIN, EATAN, EEXP, ERSQRT...) usam polinômios próprios no PS2; aqui são as da
//    libm em double, truncadas.

#include <cstdint>
#include <cstring>

namespace ps2 {

inline uint32_t fbits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

inline float ffloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

constexpr uint32_t kSignBit = 0x80000000u;
constexpr uint32_t kMaxMagnitude = 0x7fffffffu;  // o maior módulo do PS2 (expoente 255, mantissa cheia)

// Indicadores de um resultado (os bits do MAC flag da VU para uma pista, ou os do FCR31 da FPU).
struct FloatFlags {
    bool zero = false;
    bool sign = false;
    bool underflow = false;
    bool overflow = false;
};

// O valor exato de um float do PS2 (expoente 0 vale ±0; expoente 255 é normal).
double value(uint32_t bits);
// O valor x truncado para um float do PS2, com underflow (±0) e overflow (±0x7fffffff). x precisa ser exato ou
// já truncado (nunca arredondado para cima além de uma fronteira de float).
uint32_t from_double(double x, FloatFlags* flags = nullptr);

// ---- operações (as mesmas na VU e na FPU do EE) --------------------------------------------------------------
uint32_t add(uint32_t a, uint32_t b, FloatFlags* flags = nullptr);
uint32_t sub(uint32_t a, uint32_t b, FloatFlags* flags = nullptr);
uint32_t mul(uint32_t a, uint32_t b, FloatFlags* flags = nullptr);
// acc + a * b e acc - a * b com duas truncagens (o produto, depois a soma), como MADD/MSUB da VU e MADD.S do EE.
uint32_t madd(uint32_t acc, uint32_t a, uint32_t b, FloatFlags* flags = nullptr);
uint32_t msub(uint32_t acc, uint32_t a, uint32_t b, FloatFlags* flags = nullptr);

// Divisão, raiz e raiz inversa (FDIV da VU, DIV.S/SQRT.S/RSQRT.S do EE). invalid: 0/0, raiz de negativo;
// divzero: divisor zero. Divisor zero dá ±0x7fffffff (sinal de a xor b); a raiz usa o módulo da entrada.
struct DivFlags {
    bool invalid = false;
    bool divzero = false;
};
uint32_t div(uint32_t a, uint32_t b, DivFlags* flags = nullptr);
uint32_t sqrt(uint32_t b, DivFlags* flags = nullptr);
uint32_t rsqrt(uint32_t a, uint32_t b, DivFlags* flags = nullptr);

// MAX/MINI da VU e MAX.S/MIN.S do EE: comparação dos padrões de bits como sinal e módulo (não há NaN).
uint32_t max(uint32_t a, uint32_t b);
uint32_t min(uint32_t a, uint32_t b);
inline uint32_t abs(uint32_t a) { return a & ~kSignBit; }
inline uint32_t neg(uint32_t a) { return a ^ kSignBit; }
// a < b, a <= b, a == b (C.LT.S, C.LE.S, C.EQ.S do EE; ±0 e os denormais são zero).
bool less(uint32_t a, uint32_t b);
bool less_equal(uint32_t a, uint32_t b);
bool equal(uint32_t a, uint32_t b);

// Conversões. FTOI0/4/12/15 da VU e CVT.W.S do EE: x * 2^frac truncado, saturando em 0x7fffffff/0x80000000.
// ITOF0/4/12/15 e CVT.S.W: o inteiro / 2^frac, truncado.
int32_t to_int(uint32_t a, int frac = 0);
uint32_t from_int(int32_t i, int frac = 0);

// ---- a EFU da VU1 (registrador P); aproximações em double --------------------------------------------------
uint32_t efu_atan(uint32_t a);  // EATAN: atan(x)
uint32_t efu_sin(uint32_t a);   // ESIN: sin(x)
uint32_t efu_exp(uint32_t a);   // EEXP: exp(-x)

// ---- a FPU do EE com floats (a mesma aritmética; para o código portado ficar legível) --------------------------
namespace ee {
inline float add(float a, float b) { return ffloat(ps2::add(fbits(a), fbits(b))); }
inline float sub(float a, float b) { return ffloat(ps2::sub(fbits(a), fbits(b))); }
inline float mul(float a, float b) { return ffloat(ps2::mul(fbits(a), fbits(b))); }
inline float div(float a, float b) { return ffloat(ps2::div(fbits(a), fbits(b))); }
inline float sqrt(float a) { return ffloat(ps2::sqrt(fbits(a))); }
inline float rsqrt(float a, float b) { return ffloat(ps2::rsqrt(fbits(a), fbits(b))); }  // RSQRT.S: a / sqrt(b)
inline float neg(float a) { return ffloat(ps2::neg(fbits(a))); }
inline float abs(float a) { return ffloat(ps2::abs(fbits(a))); }
inline float max(float a, float b) { return ffloat(ps2::max(fbits(a), fbits(b))); }
inline float min(float a, float b) { return ffloat(ps2::min(fbits(a), fbits(b))); }
inline bool lt(float a, float b) { return ps2::less(fbits(a), fbits(b)); }
inline bool le(float a, float b) { return ps2::less_equal(fbits(a), fbits(b)); }
inline bool eq(float a, float b) { return ps2::equal(fbits(a), fbits(b)); }
inline int32_t cvt_w(float a) { return ps2::to_int(fbits(a)); }
inline float cvt_s(int32_t i) { return ffloat(ps2::from_int(i)); }
}  // namespace ee

}  // namespace ps2
