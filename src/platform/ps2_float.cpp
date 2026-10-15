// A aritmética de ponto flutuante do PS2, bit a bit (platform/ps2_float.h).

#include "platform/ps2_float.h"

#include <cmath>
#include "platform/fmath.h"

namespace ps2 {

namespace {

inline uint32_t sign_of(uint32_t b) { return b & kSignBit; }
inline int exponent_of(uint32_t b) { return int((b >> 23) & 0xff); }
inline bool is_zero(uint32_t b) { return exponent_of(b) == 0; }  // ±0 e os denormais

// Um resultado zero exato com o sinal pedido.
inline uint32_t zero_result(uint32_t sign, FloatFlags* flags) {
    if (flags) *flags = FloatFlags{true, sign != 0, false, false};
    return sign;
}

}  // namespace

double value(uint32_t bits) {
    const int e = exponent_of(bits);
    if (e == 0) return sign_of(bits) ? -0.0 : 0.0;
    if (e != 0xff) {  // um normal do IEEE: a conversão para double é exata (e bem mais rápida que o ldexp)
        float f;
        std::memcpy(&f, &bits, 4);
        return double(f);
    }
    const double m = 1.0 + double(bits & 0x7fffff) / 8388608.0;
    const double v = std::ldexp(m, e - 127);
    return sign_of(bits) ? -v : v;
}

uint32_t from_double(double x, FloatFlags* flags) {
    uint64_t d;
    std::memcpy(&d, &x, 8);
    const uint32_t sign = uint32_t(d >> 63) << 31;
    const int de = int((d >> 52) & 0x7ff);
    if (de == 0) return zero_result(sign, flags);  // zero (ou denormal do double: muito abaixo do float)
    const int fe = de - 1023 + 127;
    if (fe <= 0) {
        if (flags) *flags = FloatFlags{true, sign != 0, true, false};
        return sign;
    }
    if (fe > 255) {
        if (flags) *flags = FloatFlags{false, sign != 0, false, true};
        return sign | kMaxMagnitude;
    }
    const uint32_t mant = uint32_t((d >> 29) & 0x7fffff);  // trunca os 29 bits de baixo
    if (flags) *flags = FloatFlags{false, sign != 0, false, false};
    return sign | (uint32_t(fe) << 23) | mant;
}

uint32_t add(uint32_t a, uint32_t b, FloatFlags* flags) {
    const bool za = is_zero(a), zb = is_zero(b);
    if (za && zb) return zero_result(sign_of(a) & sign_of(b), flags);  // -0 só se os dois forem -0
    if (za) return from_double(value(b), flags);
    if (zb) return from_double(value(a), flags);
    const int ea = exponent_of(a), eb = exponent_of(b);
    // Até 29 de diferença a soma em double é exata. Além disso o menor operando fica abaixo do último bit do
    // maior: o truncamento dá o maior (mesmo sinal) ou o float logo abaixo dele em módulo (sinais opostos).
    if (ea - eb > 29 || eb - ea > 29) {
        const uint32_t big = ea > eb ? a : b;
        const uint32_t tiny = ea > eb ? b : a;
        uint32_t r = big;
        if (sign_of(big) != sign_of(tiny)) r = big - 1;  // a mantissa zerada pega emprestado do expoente
        if (is_zero(r)) {
            if (flags) *flags = FloatFlags{true, sign_of(r) != 0, true, false};
            return sign_of(r);
        }
        if (flags) *flags = FloatFlags{false, sign_of(r) != 0, false, false};
        return r;
    }
    const double s = value(a) + value(b);
    if (s == 0.0) return zero_result(0, flags);  // x + (-x) = +0 (arredondamento para zero)
    return from_double(s, flags);
}

uint32_t sub(uint32_t a, uint32_t b, FloatFlags* flags) { return add(a, neg(b), flags); }

uint32_t mul(uint32_t a, uint32_t b, FloatFlags* flags) {
    if (is_zero(a) || is_zero(b)) return zero_result(sign_of(a) ^ sign_of(b), flags);
    return from_double(value(a) * value(b), flags);  // 24 x 24 bits: exato no double
}

uint32_t madd(uint32_t acc, uint32_t a, uint32_t b, FloatFlags* flags) {
    FloatFlags pf;
    const uint32_t p = mul(a, b, &pf);
    FloatFlags sf;
    const uint32_t r = add(acc, p, &sf);
    (void)pf;
    if (flags) *flags = sf;  // os indicadores são os do resultado final, como no PCSX2
    return r;
}

uint32_t msub(uint32_t acc, uint32_t a, uint32_t b, FloatFlags* flags) {
    FloatFlags pf;
    const uint32_t p = mul(a, b, &pf);
    FloatFlags sf;
    const uint32_t r = sub(acc, p, &sf);
    (void)pf;
    if (flags) *flags = sf;
    return r;
}

uint32_t div(uint32_t a, uint32_t b, DivFlags* flags) {
    if (is_zero(b)) {
        if (flags) {
            flags->invalid = is_zero(a);
            flags->divzero = !is_zero(a);
        }
        return ((sign_of(a) ^ sign_of(b))) | kMaxMagnitude;
    }
    if (flags) *flags = DivFlags{};
    if (is_zero(a)) return sign_of(a) ^ sign_of(b);
    // O quociente em double não cruza a fronteira de um float sem ser exato (a distância de um quociente de
    // dois floats de 24 bits a um float é de pelo menos ~2^-47 relativo), então truncar o double dá o certo.
    return from_double(value(a) / value(b));
}

uint32_t sqrt(uint32_t b, DivFlags* flags) {
    if (flags) *flags = DivFlags{sign_of(b) != 0 && !is_zero(b), false};
    if (is_zero(b)) return 0;
    return from_double(std::sqrt(value(abs(b))));
}

uint32_t rsqrt(uint32_t a, uint32_t b, DivFlags* flags) {
    // a / sqrt(|b|) em duas etapas truncadas (a raiz, depois a divisão), como a unidade FDIV encadeia as duas.
    if (is_zero(b)) {
        if (flags) {
            flags->invalid = is_zero(a);
            flags->divzero = !is_zero(a);
        }
        return sign_of(a) | kMaxMagnitude;
    }
    DivFlags sf;
    const uint32_t s = sqrt(b, &sf);
    const uint32_t q = div(a, s);
    if (flags) *flags = DivFlags{sf.invalid, false};
    return q;
}

namespace {
inline int64_t order_key(uint32_t b) {
    const int64_t m = b & 0x7fffffff;
    return (b & kSignBit) ? -m - 1 : m;  // sinal e módulo; -0 fica logo abaixo de +0
}
}  // namespace

uint32_t max(uint32_t a, uint32_t b) { return order_key(a) >= order_key(b) ? a : b; }
uint32_t min(uint32_t a, uint32_t b) { return order_key(a) <= order_key(b) ? a : b; }

bool less(uint32_t a, uint32_t b) { return value(a) < value(b); }
bool less_equal(uint32_t a, uint32_t b) { return value(a) <= value(b); }
bool equal(uint32_t a, uint32_t b) { return value(a) == value(b); }

int32_t to_int(uint32_t a, int frac) {
    const double x = std::ldexp(value(a), frac);
    if (x >= 2147483648.0) return 0x7fffffff;
    if (x <= -2147483648.0) return int32_t(0x80000000u);
    return int32_t(x);  // trunca para zero
}

uint32_t from_int(int32_t i, int frac) { return from_double(std::ldexp(double(i), -frac)); }

uint32_t efu_atan(uint32_t a) { return from_double(nf::fm::atan(value(a))); }
uint32_t efu_sin(uint32_t a) { return from_double(nf::fm::sin(value(a))); }
uint32_t efu_exp(uint32_t a) { return from_double(nf::fm::exp(-value(a))); }

}  // namespace ps2
