#pragma once
// As sequências de VU0 em modo macro que o código da IA do DRIVING.ELF repete (port; docs/driving.md seção 21). Cada uma
// diz as instruções do PS2 que ela faz, na ordem e com as máscaras: o comprimento (vmul.xyz; vmulax.x; vmadday.x;
// vmaddz.x; vsqrt; vaddq.x vf0), a unidade (vrsqrt Q, vf0w; vmulq.xyz), o produto escalar (vmul.xyz; vaddy.x;
// vaddz.x). As mesmas de driving/ai/ground_vehicle.cpp, num cabeçalho para os arquivos novos da IA.

#include <cstdint>
#include <cstring>

#include "platform/ps2_float.h"

namespace drv::aivu {

namespace ee = ps2::ee;

constexpr uint32_t kOne = 0x3f800000u;
inline uint32_t bits(float f) { return ps2::fbits(f); }
inline float flt(uint32_t b) { return ps2::ffloat(b); }

// vmul.xyz; vmulax.x; vmadday.x; vmaddz.x
inline uint32_t len_sq(const float v[3]) {
    uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[1]), bits(v[1])));
    return ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2])));
}
// ... vsqrt Q; vaddq.x vf0
inline float len(const float v[3]) { return flt(ps2::add(0, ps2::sqrt(len_sq(v)))); }
// vmul.xz (ou .xyz); vmulax.x; vmaddz.x: a raiz de x² + z²
inline float len_xz(const float v[3]) {
    const uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    return flt(ps2::add(0, ps2::sqrt(ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2]))))));
}
// vrsqrt Q, vf0w, |v|²; vmulq.xyz (o w de d fica)
inline void unit(float d[3], const float v[3]) {
    const uint32_t q = ps2::rsqrt(kOne, len_sq(v));
    float r[3];
    for (int i = 0; i < 3; ++i) r[i] = flt(ps2::mul(bits(v[i]), q));
    std::memcpy(d, r, 12);
}
// vsub.xyz
inline void sub3(float d[3], const float a[3], const float b[3]) {
    float r[3];
    for (int i = 0; i < 3; ++i) r[i] = ee::sub(a[i], b[i]);
    std::memcpy(d, r, 12);
}
// vadd.xyz
inline void add3(float d[3], const float a[3], const float b[3]) {
    float r[3];
    for (int i = 0; i < 3; ++i) r[i] = ee::add(a[i], b[i]);
    std::memcpy(d, r, 12);
}
// vmulx.xyz (o vetor vezes um escalar)
inline void scale3(float d[3], const float a[3], float s) {
    float r[3];
    for (int i = 0; i < 3; ++i) r[i] = ee::mul(a[i], s);
    std::memcpy(d, r, 12);
}
// vmul.xyz; vaddy.x; vaddz.x
inline float dot(const float a[3], const float b[3]) {
    const float x = ee::mul(a[0], b[0]), y = ee::mul(a[1], b[1]), z = ee::mul(a[2], b[2]);
    return ee::add(ee::add(x, y), z);
}
inline float dist(const float a[3], const float b[3]) {
    float d[3];
    sub3(d, a, b);
    return len(d);
}
inline void copy3(float d[3], const float s[3]) { std::memmove(d, s, 12); }

}  // namespace drv::aivu
