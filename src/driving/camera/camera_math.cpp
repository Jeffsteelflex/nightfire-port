// As contas da câmera (driving/camera/camera_math.h), com a aritmética do PS2 na ordem das instruções (platform/
// ps2_float.h para a FPU, vu0_macro.h para a VU0 em modo macro).

#include "driving/camera/camera_math.h"

#include <atomic>
#include <cstring>

#include "driving/anim/eagl_anim.h"
#include "driving/real/vu0_math.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

std::atomic<uint64_t> g_pending{0};

float fb(uint32_t b) { return ps2::ffloat(b); }
const uint32_t kOne = 0x3f800000u;
const uint32_t kVf0[4] = {0, 0, 0, 0x3f800000u};

// vmul.xyz t, v, v; vmulax.x ACC, v, v; vmadday.x ACC, one, t; vmaddz.x t, one, t: (x x + y y) + z z.
uint32_t vu_len2(const Reg v) {
    Reg t, acc{}, one = {kOne, 0, 0, 0};
    mul(t, v, v, XYZ);
    mulabc(acc, v, v, BX, X);
    maddabc(acc, one, t, BY, X);
    maddbc(t, acc, one, t, BZ, X);
    return t[0];
}
// vrsqrt Q, vf0w, t; vmulq.xyz d, v, Q.
void vu_normalize3(Reg d, const Reg v) {
    const uint32_t q = ps2::rsqrt(kOne, vu_len2(v));
    muls(d, v, q, XYZ);
}
// vopmula.xyz ACC, a, b; vopmsub.xyz d, b, a: d.xyz = a x b.
void vu_cross(Reg d, const Reg a, const Reg b) {
    Reg acc{};
    opmula(acc, a, b);
    opmsub(d, acc, b, a);
}

// As bases da curva (0x37e4a0 Bézier, 0x37e4e0 Catmull-Rom vezes 2).
const float kBezier[4][4] = {{-1, 3, -3, 1}, {3, -6, 3, 0}, {-3, 3, 0, 0}, {1, 0, 0, 0}};
const float kCatmull[4][4] = {{-1, 3, -3, 1}, {2, -5, 4, -1}, {-1, 0, 1, 0}, {0, 2, 0, 0}};

}  // namespace

uint64_t camera_math_pending() { return g_pending.load(); }

// ---------------------------------------------------------------- os ângulos em voltas

float cam_sin_turns(float t) {
    if (ee::lt(t, 0.0f)) t = ee::add(t, 1.0f);
    return VU0_Sin(t);
}

float cam_tan_turns(float t) {
    if (ee::lt(t, 0.0f)) t = ee::add(t, 1.0f);
    return nf::fm::tan(ee::mul(t, fb(0x40c90fdbu)));
}

float cam_atan2_turns(float y, float x) {
    float r = ee::mul(nf::fm::atan2(y, x), fb(0x3e22f983u));
    if (ee::lt(r, 0.0f)) r = ee::add(r, 1.0f);
    return r;
}

float cam_acos_turns(float c) {
    if (ee::lt(1.0f, c)) c = 1.0f;
    else if (ee::lt(c, -1.0f)) c = -1.0f;
    return ee::mul(nf::fm::acos(c), fb(0x3e22f984u));
}

float cam_asin_turns(float s) {
    if (ee::lt(1.0f, s)) s = 1.0f;
    else if (ee::lt(s, -1.0f)) s = -1.0f;
    return ee::mul(nf::fm::asin(s), fb(0x3e22f984u));
}

float cam_wrap_turns(float t) {
    if (ee::le(1.0f, t)) t = ee::sub(t, 1.0f);
    if (ee::le(t, -1.0f)) t = ee::add(t, 1.0f);
    if (ee::lt(t, 0.0f)) t = ee::add(t, 1.0f);
    return t;
}

float cam_math_sin(float t) {
    if (ee::lt(t, 0.0f)) t = ee::add(t, 1.0f);
    return VU0_Sin(t);
}

float cam_math_cos(float t) {
    if (ee::lt(t, 0.0f)) t = ee::add(t, 1.0f);
    return VU0_Cos(t);
}

// ---------------------------------------------------------------- RCameraMath

void RCameraMath_BuildRotationQuat(float q[4], const float axis[4], float turns) {
    const float half = ee::mul(turns, 0.5f);
    const float s = cam_math_sin(half);
    Reg v1, v3, v2 = {ps2::fbits(s), 0, 0, 0};
    load(v1, axis);
    load(v3, q);
    mulbc(v3, v1, v2, BX, XYZ);
    store(q, v3);
    q[3] = cam_math_cos(half);
    // vmul.xyzw; vmulax.x; vmadday.x, vmaddaz.x, vmaddw.x com 1; vrsqrt; vmulq.xyzw.
    Reg a, t, acc{}, one = {kOne, 0, 0, 0};
    load(a, q);
    mul(t, a, a, XYZW);
    mulabc(acc, a, a, BX, X);
    maddabc(acc, one, t, BY, X);
    maddabc(acc, one, t, BZ, X);
    maddbc(t, acc, one, t, BW, X);
    const uint32_t r = ps2::rsqrt(kOne, t[0]);
    muls(a, a, r, XYZW);
    store(q, a);
}

void RCameraMath_BuildRotationMat4(float turns, float m[4][4], const float axis[4]) {
    float q[4] = {0, 0, 0, 0};  // a pilha do PS2 (o w é escrito antes de ser lido)
    RCameraMath_BuildRotationQuat(q, axis, turns);
    VU0_quattom4(q, m);
}

void RCameraMath_VU0_GenerateMatrix4(const float dir[4], float m[4][4]) {
    Reg v1, v2;
    load(v1, dir);
    load(v2, m[2]);
    const uint32_t q = ps2::rsqrt(kOne, vu_len2(v1));
    muls(v2, v1, q, XYZ);
    store(m[2], v2);
    m[2][3] = 0.0f;
    RCameraMath_VU0_GenerateMatrix4Unit(m[2], m);
}

void RCameraMath_VU0_GenerateMatrix4Unit(const float dir[4], float m[4][4]) {
    const float up[4] = {0, 1.0f, 0, 0};
    Reg v1, v2, v3;
    load(v1, up);
    load(v2, dir);
    load(v3, m[0]);
    vu_cross(v3, v1, v2);
    vu_normalize3(v3, v3);
    store(m[0], v3);
    load(v1, m[2]);
    load(v2, m[0]);
    load(v3, m[1]);
    vu_cross(v3, v1, v2);
    store(m[1], v3);
    store(m[3], kVf0);
    m[2][3] = 0.0f;
    m[1][3] = 0.0f;
    m[0][3] = 0.0f;
}

float RCameraMath_FindAngleBetweenVectors(const float a[4], const float b[4]) {
    Reg v1, v2, v3{};
    load(v1, a);
    load(v2, b);
    mul(v3, v1, v2, XYZ);
    addbc(v3, v3, v3, BY, X);
    addbc(v3, v3, v3, BZ, X);
    float angle = cam_acos_turns(ps2::ffloat(v3[0]));
    Reg c{};
    vu_cross(c, v1, v2);
    if (ee::lt(ps2::ffloat(c[1]), 0.0f)) angle = ee::sub(1.0f, angle);
    return angle;
}

void OrthoInverse(float m[4][4]) {
    float f12 = m[0][1];
    const float f11 = m[1][0];
    float f0 = m[3][0];
    m[1][0] = f12;
    f0 = ee::neg(f0);
    float f5 = m[3][1];
    f12 = m[0][2];
    const float f9 = m[2][0];
    const float f10 = m[2][1];
    float f7 = ee::mul(f0, f11);
    float f4 = m[1][0];
    float f6 = ee::mul(f0, f9);
    float f2 = m[0][0];
    const float f8 = ee::mul(f5, f10);
    float f1 = m[1][1];
    f4 = ee::mul(f5, f4);
    m[2][0] = f12;
    f0 = ee::mul(f0, f2);
    f5 = ee::mul(f5, f1);
    float f3 = m[3][2];
    f1 = m[2][0];
    f6 = ee::sub(f6, f8);
    f2 = m[2][2];
    f0 = ee::sub(f0, f4);
    f12 = m[1][2];
    f1 = ee::mul(f3, f1);
    f2 = ee::mul(f3, f2);
    m[0][1] = f11;
    f7 = ee::sub(f7, f5);
    m[0][2] = f9;
    f3 = ee::mul(f3, f12);
    m[1][2] = f10;
    f0 = ee::sub(f0, f1);
    m[2][1] = f12;
    f6 = ee::sub(f6, f2);
    f7 = ee::sub(f7, f3);
    m[3][0] = f0;
    m[3][2] = f6;
    m[3][1] = f7;
}

void MatrixMultiplyFpu(const float a[4][4], const float b[4][4], float out[4][4]) {
    float r[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = ee::add(ee::mul(a[i][0], b[0][j]), ee::mul(a[i][1], b[1][j]));
            s = ee::add(s, ee::mul(a[i][2], b[2][j]));
            r[i][j] = ee::add(s, ee::mul(a[i][3], b[3][j]));
        }
    std::memcpy(out, r, sizeof r);
}

// ---------------------------------------------------------------- RCameraSpline

PlayerCameraSpline::PlayerCameraSpline() {
    static const float kIdentity14[14] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0};
    std::memcpy(d.points, kIdentity14, sizeof kIdentity14);
    d.points[3][2] = 0;
    d.points[3][3] = 0;
    std::memcpy(d.quat0, kVf0, 16);
    std::memcpy(d.quat1, kVf0, 16);
    d.type = 2;
    d.list = 0;
}

void PlayerCameraSpline::AddToSplinePtList(const float p[4]) { points.push_back({p[0], p[1], p[2], p[3]}); }

const float (*PlayerCameraSpline::GetBasisMatrix() const)[4] { return d.type == 1 ? kCatmull : kBezier; }

size_t PlayerCameraSpline::FindPointInList(const float p[4]) const {
    for (size_t i = 0; i < points.size(); ++i)
        if (ee::eq(p[0], points[i][0]) && ee::eq(p[1], points[i][1]) && ee::eq(points[i][2], p[2])) return i;
    return points.size();
}

void PlayerCameraSpline::BuildSpline(float tension, const float p0[4], float dir0[4], const float p1[4], float dir1[4],
                               const float* q0, const float* q1) {
    auto normalize_in_place = [](float v[4]) {
        if (ee::eq(v[0], 0.0f) && ee::eq(v[1], 0.0f) && ee::eq(v[2], 0.0f)) return;
        Reg r;
        load(r, v);
        Reg n;
        load(n, v);
        vu_normalize3(n, r);
        store(v, n);
    };
    normalize_in_place(dir0);
    normalize_in_place(dir1);
    Reg a, b;
    load(a, p0);
    load(b, p1);
    sub(a, a, b, XYZ);
    const float dist = ee::sqrt(ps2::ffloat(vu_len2(a)));
    const float f20 = ee::mul(dist, tension);
    auto offset = [](float out[4], const float base[4], const float dir[4], float k) {
        Reg vd, vb, vo, s = {ps2::fbits(k), 0, 0, 0};
        load(vd, dir);
        load(vb, base);
        load(vo, out);
        mulbc(vo, vd, s, BX, XYZ);
        add(vo, vb, vo, XYZ);
        store(out, vo);
    };
    if (points.size() < 2) {
        d.type = 0;
        std::memcpy(d.points[0], p0, 16);
        offset(d.points[1], p0, dir0, f20);
        offset(d.points[2], p1, dir1, ee::neg(f20));
        std::memcpy(d.points[3], p1, 16);
        points.clear();
        d.points[0][3] = 0.0f;
    } else {
        d.type = 1;
        const size_t it = FindPointInList(p1);
        if (it == points.size()) return;
        if (it == 0) {
            offset(d.points[0], p0, dir0, ee::neg(f20));
            std::memcpy(d.points[1], p0, 16);
            std::memcpy(d.points[2], p1, 16);
            std::memcpy(d.points[3], points[it + 1].data(), 16);
            d.points[0][3] = 0.0f;
        } else {
            std::memcpy(d.points[0], d.points[1], 16);
            std::memcpy(d.points[1], p0, 16);
            std::memcpy(d.points[2], p1, 16);
            if (it + 1 == points.size()) offset(d.points[3], p1, dir1, f20);
            else std::memcpy(d.points[3], points[it + 1].data(), 16);
            d.points[0][3] = 0.0f;
        }
    }
    d.points[1][3] = 0.0f;
    d.points[2][3] = 0.0f;
    d.points[3][3] = 0.0f;
    if (q0 && q1) {
        std::memcpy(d.quat0, q0, 16);
        std::memcpy(d.quat1, q1, 16);
    }
}

namespace {
// (t³, t², t, 1) vezes base * pontos pela VU (vmulax/vmadday/vmaddaz/vmaddw.xyzw).
void cubic(const float basis[4][4], const float points[4][4], float t, Reg out) {
    float m[4][4];
    VU0_MATRIX4_mult(basis, points, m);
    const float t2 = ee::mul(t, t);
    const float t3 = ee::mul(t2, t);
    const float tv[4] = {t3, t2, t, 1.0f};
    Reg v, r0, r1, r2, r3, acc{};
    load(v, tv);
    load(r0, m[0]);
    load(r1, m[1]);
    load(r2, m[2]);
    load(r3, m[3]);
    mulabc(acc, r0, v, BX, XYZW);
    maddabc(acc, r1, v, BY, XYZW);
    maddabc(acc, r2, v, BZ, XYZW);
    maddbc(out, acc, r3, v, BW, XYZW);
}
}  // namespace

void PlayerCameraSpline::EvaluateSpline(float t, float out[4][4], int32_t mode) const {
    Reg p;
    cubic(GetBasisMatrix(), d.points, t, p);
    p[3] = kOne;
    if (d.type == 1) {
        const Reg half = {0x3f000000u, 0, 0, 0};
        mulbc(p, p, half, BX, XYZ);
    }
    if (mode != 1) {
        // VU0_ExtractZAxis4FromQuat + VU0_GenerateMatrix4 (0x1a75b0): nenhum modo portado usa.
        g_pending.fetch_add(1);
        return;
    }
    float q[4];
    anim::fast_qslerp(d.quat0, d.quat1, q, t);
    VU0_quattom4(q, out);
    store(out[3], p);
}

}  // namespace drv
