// Referência na CPU das rotinas da VU1 e da névoa do GS. Ver vu1.h e docs/formats.md.

#include "platform/vu1.h"

#include <algorithm>
#include <cmath>

namespace vu1 {

namespace {
// FTOI0 da VU: trunca em direção a zero (os valores aqui nunca saturam).
float ftoi0(float v) { return std::trunc(v); }

// cvt.w.s do EE: trunca e satura (o FPU do EE não tem infinito; a divisão por zero dá +-FLT_MAX).
int32_t ee_to_int(float v) {
    if (std::isnan(v)) return 0;
    if (v >= 2147483648.0f) return INT32_MAX;
    if (v <= -2147483648.0f) return INT32_MIN;
    return int32_t(v);
}
}  // namespace

void tweak_vertex(const float colour[4], const float tambient[4], float out[4]) {
    for (int k = 0; k < 4; ++k) out[k] = ftoi0(tambient[k] * colour[k]);
}

void light_vertex(const float colour[4], const float normal_vu[3], const float p[3], const float tambient[4],
                  const Lights& lights, float out[4]) {
    // ITOF0 e MULi com I = 1/128: a normal da VU vira n / 128.
    const float n[3] = {normal_vu[0] * 0.0078125f, normal_vu[1] * 0.0078125f, normal_vu[2] * 0.0078125f};
    float s[2];
    for (int i = 0; i < 2; ++i) {
        const float d[3] = {lights.pos[i][0] - p[0], lights.pos[i][1] - p[1], lights.pos[i][2] - p[2]};
        const float d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        const float att = std::max(0.0f, 1.0f / d2 - lights.color[i][3]);
        const float ndl = n[0] * d[0] + n[1] * d[1] + n[2] * d[2];
        s[i] = std::max(0.0f, att * ndl);
    }
    for (int k = 0; k < 3; ++k) {
        const float c = tambient[k] * colour[k] + lights.color[1][k] * s[1] + lights.color[0][k] * s[0];
        out[k] = ftoi0(std::min(c, 255.0f));
    }
    out[3] = ftoi0(std::min(tambient[3] * colour[3], 255.0f));
}

void lights_to_local(const MATRIX& world, const float in[2][4], float out[2][4]) {
    const float* t = world.m[3];
    for (int i = 0; i < 2; ++i) {
        const float d[3] = {in[i][0] - t[0], in[i][1] - t[1], in[i][2] - t[2]};
        for (int c = 0; c < 3; ++c) out[i][c] = d[0] * world.m[c][0] + d[1] * world.m[c][1] + d[2] * world.m[c][2];
        out[i][3] = 1.0f;
    }
}

void env_st(const float normal_vu[3], const float row0[3], const float row1[3], float st[2]) {
    st[0] = (normal_vu[0] * row0[0] + normal_vu[1] * row0[1] + normal_vu[2] * row0[2]) * 0.00390625f + 0.5f;
    st[1] = (normal_vu[0] * row1[0] + normal_vu[1] * row1[1] + normal_vu[2] * row1[2]) * 0.00390625f + 0.5f;
}

void screen_z_coefficients(float far1, float& a, float& b) {
    const float range = kPs2Far2 - kPs2Near2;
    a = kPs2Far2 * kPs2Near2 * (far1 - kPs2Near1) / range;
    b = (kPs2Near1 * kPs2Far2 - far1 * kPs2Near2) / range;
}

float screen_z(float view_z, float far1) {
    float a, b;
    screen_z_coefficients(far1, a, b);
    return a / view_z + b;
}

uint32_t gs_z(float view_z, float far1) {
    const float z = screen_z(view_z, far1);
    if (!(z > 0.0f)) return 0;
    if (z >= 268435456.0f) return 0xffffff;  // FTOI4 satura bem antes disso; não acontece nos dados
    return uint32_t(z) & 0xffffff;
}

float fog_keep(uint32_t z) {
    if (z >= 0x7fff) return 1.0f;  // o sprite de névoa (Z 0x7fff, GREATER) não passa
    return float((z >> 8) & 0xff) * 0.0078125f;
}

uint8_t alpha_fog(float screen_z_of_centre, uint8_t olda) {
    int32_t f = ee_to_int(screen_z_of_centre) >> 7;
    if (0xff < f) f = 0xff;
    return uint8_t((f * int32_t(olda)) / 0xff);
}

}  // namespace vu1
