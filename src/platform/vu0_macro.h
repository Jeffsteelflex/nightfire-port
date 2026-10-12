#pragma once
// As operações vetoriais da VU (as instruções de cima, com campo de destino e difusão) para o código portado
// que no PS2 usa a VU0 em modo macro (vmul.xyz, vmulax, vopmula, vdiv...) ou que reescreve à mão um
// microprograma. Mesma aritmética e mesmas regras do interpretador (platform/vu_interp.h, ps2_float.h): o
// resultado de uma operação aqui tem os mesmos bits que a instrução daria na VU, e só as pistas do destino mudam.
// Genérico (não sabe de jogo); os registradores são vetores de quatro pistas em bits (x, y, z, w).
//
// Os nomes seguem as instruções: add(d, s, t, XYZ) é ADD.xyz d, s, t; addbc(d, s, t, Y, XYZ) é ADDy.xyz;
// madd(d, acc, s, t, m) é MADD (d = acc + s * t); opmula/opmsub montam o produto vetorial como na VU.

#include <cstdint>
#include <cstring>

#include "platform/ps2_float.h"

namespace ps2::vu {

// O campo de destino (como na codificação: x = 8, y = 4, z = 2, w = 1).
enum Dest : uint32_t {
    W = 1, Z = 2, ZW = 3, Y = 4, YW = 5, YZ = 6, YZW = 7,
    X = 8, XW = 9, XZ = 10, XZW = 11, XY = 12, XYW = 13, XYZ = 14, XYZW = 15,
};
// O campo de difusão (bc).
enum Bc : int { BX = 0, BY = 1, BZ = 2, BW = 3 };

using Reg = uint32_t[4];

inline bool on(uint32_t dest, int lane) { return (dest >> (3 - lane)) & 1; }

inline void set(uint32_t* d, const uint32_t v[4], uint32_t dest) {
    for (int l = 0; l < 4; ++l)
        if (on(dest, l)) d[l] = v[l];
}

// Carrega e guarda (lqc2/sqc2 e LQ/SQ com campo).
inline void load(uint32_t* d, const void* src, uint32_t dest = XYZW) {
    uint32_t v[4];
    std::memcpy(v, src, 16);
    set(d, v, dest);
}
inline void store(void* dst, const uint32_t* s, uint32_t dest = XYZW) {
    uint32_t v[4];
    std::memcpy(v, dst, 16);
    set(v, s, dest);
    std::memcpy(dst, v, 16);
}
inline void move(uint32_t* d, const uint32_t* s, uint32_t dest = XYZW) {
    uint32_t v[4] = {s[0], s[1], s[2], s[3]};
    set(d, v, dest);
}
// MR32: x <- y, y <- z, z <- w, w <- x.
inline void mr32(uint32_t* d, const uint32_t* s, uint32_t dest = XYZW) {
    uint32_t v[4] = {s[1], s[2], s[3], s[0]};
    set(d, v, dest);
}

#define NF_VU_BINARY(name, fn)                                                                      \
    inline void name(uint32_t* d, const uint32_t* s, const uint32_t* t, uint32_t dest) {          \
        uint32_t v[4];                                                                              \
        for (int l = 0; l < 4; ++l) v[l] = on(dest, l) ? fn(s[l], t[l]) : 0;                        \
        set(d, v, dest);                                                                            \
    }                                                                                               \
    inline void name##bc(uint32_t* d, const uint32_t* s, const uint32_t* t, int bc, uint32_t dest) { \
        uint32_t v[4];                                                                              \
        const uint32_t b = t[bc];                                                                   \
        for (int l = 0; l < 4; ++l) v[l] = on(dest, l) ? fn(s[l], b) : 0;                           \
        set(d, v, dest);                                                                            \
    }                                                                                               \
    inline void name##s(uint32_t* d, const uint32_t* s, uint32_t q, uint32_t dest) {              \
        uint32_t v[4];                                                                              \
        for (int l = 0; l < 4; ++l) v[l] = on(dest, l) ? fn(s[l], q) : 0;                           \
        set(d, v, dest);                                                                            \
    }

namespace detail {
inline uint32_t add2(uint32_t a, uint32_t b) { return ps2::add(a, b); }
inline uint32_t sub2(uint32_t a, uint32_t b) { return ps2::sub(a, b); }
inline uint32_t mul2(uint32_t a, uint32_t b) { return ps2::mul(a, b); }
inline uint32_t max2(uint32_t a, uint32_t b) { return ps2::max(a, b); }
inline uint32_t min2(uint32_t a, uint32_t b) { return ps2::min(a, b); }
}  // namespace detail

// add/addbc/adds (s = um escalar: Q ou I), sub, mul, max, mini.
NF_VU_BINARY(add, detail::add2)
NF_VU_BINARY(sub, detail::sub2)
NF_VU_BINARY(mul, detail::mul2)
NF_VU_BINARY(max, detail::max2)
NF_VU_BINARY(mini, detail::min2)
#undef NF_VU_BINARY

// Os acumulados: d = acc + s * t (madd) e d = acc - s * t (msub); mula/madda/msuba escrevem em ACC.
inline void madd(uint32_t* d, const uint32_t* acc, const uint32_t* s, const uint32_t* t, uint32_t dest) {
    uint32_t v[4];
    for (int l = 0; l < 4; ++l) v[l] = on(dest, l) ? ps2::madd(acc[l], s[l], t[l]) : 0;
    set(d, v, dest);
}
inline void maddbc(uint32_t* d, const uint32_t* acc, const uint32_t* s, const uint32_t* t, int bc, uint32_t dest) {
    uint32_t v[4];
    for (int l = 0; l < 4; ++l) v[l] = on(dest, l) ? ps2::madd(acc[l], s[l], t[bc]) : 0;
    set(d, v, dest);
}
inline void msub(uint32_t* d, const uint32_t* acc, const uint32_t* s, const uint32_t* t, uint32_t dest) {
    uint32_t v[4];
    for (int l = 0; l < 4; ++l) v[l] = on(dest, l) ? ps2::msub(acc[l], s[l], t[l]) : 0;
    set(d, v, dest);
}
inline void msubbc(uint32_t* d, const uint32_t* acc, const uint32_t* s, const uint32_t* t, int bc, uint32_t dest) {
    uint32_t v[4];
    for (int l = 0; l < 4; ++l) v[l] = on(dest, l) ? ps2::msub(acc[l], s[l], t[bc]) : 0;
    set(d, v, dest);
}
inline void mula(uint32_t* acc, const uint32_t* s, const uint32_t* t, uint32_t dest) { mul(acc, s, t, dest); }
inline void mulabc(uint32_t* acc, const uint32_t* s, const uint32_t* t, int bc, uint32_t dest) {
    mulbc(acc, s, t, bc, dest);
}
inline void madda(uint32_t* acc, const uint32_t* s, const uint32_t* t, uint32_t dest) { madd(acc, acc, s, t, dest); }
inline void maddabc(uint32_t* acc, const uint32_t* s, const uint32_t* t, int bc, uint32_t dest) {
    maddbc(acc, acc, s, t, bc, dest);
}

// OPMULA.xyz ACC, s, t: ACC = (s.y t.z, s.z t.x, s.x t.y); OPMSUB.xyz d, s, t: d = ACC - (s.y t.z, s.z t.x, s.x t.y).
// opmula(acc, a, b) seguido de opmsub(d, acc, b, a) dá d = a x b.
inline void opmula(uint32_t* acc, const uint32_t* s, const uint32_t* t) {
    const uint32_t v[4] = {ps2::mul(s[1], t[2]), ps2::mul(s[2], t[0]), ps2::mul(s[0], t[1]), 0};
    set(acc, v, XYZ);
}
inline void opmsub(uint32_t* d, const uint32_t* acc, const uint32_t* s, const uint32_t* t) {
    const uint32_t v[4] = {ps2::msub(acc[0], s[1], t[2]), ps2::msub(acc[1], s[2], t[0]), ps2::msub(acc[2], s[0], t[1]),
                           0};
    set(d, v, XYZ);
}

inline void abs(uint32_t* d, const uint32_t* s, uint32_t dest) {
    const uint32_t v[4] = {ps2::abs(s[0]), ps2::abs(s[1]), ps2::abs(s[2]), ps2::abs(s[3])};
    set(d, v, dest);
}

// Lê uma pista como float (para o código C++ em volta).
inline float f(const uint32_t* s, int lane) { return ffloat(s[lane]); }
inline uint32_t b(float x) { return fbits(x); }

}  // namespace ps2::vu
