// As rotinas de matriz e quatérnio da REAL (drv/real/vu0_math.h), com as operações do PS2 na mesma ordem.

#include "driving/real/vu0_math.h"

#include <cstring>

#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {
const uint32_t kVf0[4] = {0, 0, 0, 0x3f800000};
}

// [driving] 0x2cc0c8 VU0_quattom4
void VU0_quattom4(const float q[4], float m[4][4]) {
    Reg v1, v4, v5, v6, v7, v8, v9, v10;
    load(v1, q);
    mulbc(v4, v1, v1, BX, XYZW);
    mulbc(v5, v1, v1, BY, XYZW);
    mulbc(v6, v1, v1, BZ, XYZW);
    mulbc(v7, v1, v1, BW, XYZW);
    add(v4, v4, v4, XYZW);
    add(v5, v5, v5, XYZW);
    add(v6, v6, v6, XYZW);
    add(v7, v7, v7, XYZW);
    sub(v8, kVf0, kVf0, XYZW);
    sub(v9, kVf0, kVf0, XYZW);
    sub(v10, kVf0, kVf0, XYZW);
    addbc(v8, kVf0, kVf0, BW, X);
    addbc(v9, kVf0, v5, BX, X);
    addbc(v10, kVf0, v6, BX, X);
    subbc(v8, v8, v5, BY, X);
    subbc(v9, v9, v6, BW, X);
    addbc(v10, v10, v5, BW, X);
    subbc(v8, v8, v6, BZ, X);
    addbc(v9, kVf0, kVf0, BW, Y);
    addbc(v10, kVf0, v5, BZ, Y);
    addbc(v8, kVf0, v5, BX, Y);
    subbc(v9, v9, v4, BX, Y);
    subbc(v10, v10, v7, BX, Y);
    addbc(v8, v8, v6, BW, Y);
    subbc(v9, v9, v6, BZ, Y);
    addbc(v10, kVf0, kVf0, BW, Z);
    addbc(v8, kVf0, v6, BX, Z);
    addbc(v9, kVf0, v6, BY, Z);
    subbc(v10, v10, v4, BX, Z);
    subbc(v8, v8, v5, BW, Z);
    addbc(v9, v9, v7, BX, Z);
    subbc(v10, v10, v5, BY, Z);
    sub(v8, v8, v8, W);
    sub(v9, v9, v9, W);
    sub(v10, v10, v10, W);
    store(m[3], kVf0);
    store(m[0], v8);
    store(m[1], v9);
    store(m[2], v10);
}

// [driving] 0x2cc690 VU0_m4toquat
void VU0_m4toquat(const float m[4][4], float q[4]) {
    const float m00 = m[0][0], m11 = m[1][1], m22 = m[2][2];
    const float s01 = ee::add(m00, m11);
    const float trace = ee::add(s01, m22);
    if (ee::lt(0.0f, trace)) {
        const float s = ee::sqrt(ee::add(trace, 1.0f));
        const float h = ee::mul(s, 0.5f);
        const float r = ee::div(0.5f, s);
        q[3] = h;
        q[0] = ee::mul(ee::sub(m[1][2], m[2][1]), r);
        q[1] = ee::mul(ee::sub(m[2][0], m[0][2]), r);
        q[2] = ee::mul(ee::sub(m[0][1], m[1][0]), r);
        return;
    }
    // O maior elemento da diagonal (0, 1 ou 2), comparando m00 com m11 e o maior com m22.
    const int i = ee::lt(m00, m11) ? 1 : 0;
    const float mii = i ? m11 : m00;
    if (ee::lt(mii, m22)) {
        float s = ee::sqrt(ee::add(ee::sub(m22, s01), 1.0f));
        q[2] = ee::mul(s, 0.5f);
        if (!ee::eq(s, 0.0f)) s = ee::div(0.5f, s);
        q[3] = ee::mul(ee::sub(m[0][1], m[1][0]), s);
        q[0] = ee::mul(ee::add(m[2][0], m[0][2]), s);
        q[1] = ee::mul(ee::add(m[2][1], m[1][2]), s);
        return;
    }
    if (i != 0) {
        float s = ee::sqrt(ee::add(ee::sub(m11, ee::add(m22, m00)), 1.0f));
        q[1] = ee::mul(s, 0.5f);
        if (!ee::eq(s, 0.0f)) s = ee::div(0.5f, s);
        q[3] = ee::mul(ee::sub(m[2][0], m[0][2]), s);
        q[2] = ee::mul(ee::add(m[1][2], m[2][1]), s);
        q[0] = ee::mul(ee::add(m[0][1], m[1][0]), s);
        return;
    }
    float s = ee::sqrt(ee::add(ee::sub(m00, ee::add(m11, m22)), 1.0f));
    q[0] = ee::mul(s, 0.5f);
    if (!ee::eq(s, 0.0f)) s = ee::div(0.5f, s);
    q[3] = ee::mul(ee::sub(m[1][2], m[2][1]), s);
    q[1] = ee::mul(ee::add(m[0][1], m[1][0]), s);
    q[2] = ee::mul(ee::add(m[0][2], m[2][0]), s);
}

// [driving] 0x2cbb28 VU0_MATRIX3x4_mult: as três primeiras linhas de a vezes b com a coluna w de b zerada (vsub.w);
// a linha 3 de out não é escrita.
void VU0_MATRIX3x4_mult(const float a[4][4], const float b[4][4], float out[4][4]) {
    Reg v1, v2, v3, v5, v6, v7, acc, v9, v10, v11;
    load(v1, a[0]);
    load(v2, a[1]);
    load(v3, a[2]);
    load(v5, b[0]);
    load(v6, b[1]);
    load(v7, b[2]);
    sub(v5, v5, v5, W);
    sub(v6, v6, v6, W);
    sub(v7, v7, v7, W);
    mulabc(acc, v5, v1, BX, XYZW);
    maddabc(acc, v6, v1, BY, XYZW);
    maddbc(v9, acc, v7, v1, BZ, XYZW);
    mulabc(acc, v5, v2, BX, XYZW);
    maddabc(acc, v6, v2, BY, XYZW);
    maddbc(v10, acc, v7, v2, BZ, XYZW);
    mulabc(acc, v5, v3, BX, XYZW);
    maddabc(acc, v6, v3, BY, XYZW);
    maddbc(v11, acc, v7, v3, BZ, XYZW);
    store(out[0], v9);
    store(out[1], v10);
    store(out[2], v11);
}

// [driving] 0x2cbb88 VU0_MATRIX4_mult
void VU0_MATRIX4_mult(const float a[4][4], const float b[4][4], float out[4][4]) {
    Reg row[4], bm[4], acc, r[4];
    for (int i = 0; i < 4; ++i) {
        load(row[i], a[i]);
        load(bm[i], b[i]);
    }
    for (int i = 0; i < 4; ++i) {
        mulabc(acc, bm[0], row[i], BX, XYZW);
        maddabc(acc, bm[1], row[i], BY, XYZW);
        maddabc(acc, bm[2], row[i], BZ, XYZW);
        maddbc(r[i], acc, bm[3], row[i], BW, XYZW);
    }
    for (int i = 0; i < 4; ++i) store(out[i], r[i]);
}

void transpose4(const float in[4][4], float out[4][4]) {
    float t[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) t[j][i] = in[i][j];
    std::memcpy(out, t, sizeof(t));
}

}  // namespace drv
