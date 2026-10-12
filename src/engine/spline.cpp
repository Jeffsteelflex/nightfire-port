// Interpolação ao longo de caminhos. Ver spline.h.
//
// acosf é a da biblioteca do PC; a do PS2 pode diferir no último bit.

#include "engine/spline.h"

#include <cmath>

#include "engine/vecmath.h"

namespace {

// Índices e frações de KeyFrame_Interp (estáticos no original).
uint16_t K1_476 = 0;
uint16_t K2_477 = 0;
float Blend_478 = 0.0f;

_VECTOR frame_pos(const framelist_tag* path, uint32_t i) {
    const float* p = path[i].pos;
    return {p[0], p[1], p[2], 0.0f};
}

// Quatérnio (x, y, z, w) para a parte 3x3, na ordem de operações das duas funções.
void quat_matrix(float q0, float q1, float q2, float q3, MATRIX* m) {
    const float y2 = q1 + q1;
    const float z2 = q2 + q2;
    const float xx = (q0 + q0) * q0;
    const float xw = (q0 + q0) * q3;
    m->m[2][0] = z2 * q0 + y2 * q3;
    m->m[1][0] = y2 * q0 - z2 * q3;
    m->m[2][1] = z2 * q1 - xw;
    m->m[2][2] = 1.0f - (xx + y2 * q1);
    m->m[0][0] = 1.0f - (y2 * q1 + z2 * q2);
    m->m[1][1] = 1.0f - (xx + z2 * q2);
    m->m[0][1] = y2 * q0 + z2 * q3;
    m->m[0][2] = z2 * q0 - y2 * q3;
    m->m[1][2] = z2 * q1 + xw;
}

// Pesos do slerp de a para b na fração f (com o sinal de a já corrigido para o lado mais curto).
void slerp_weights(const float* a, const float* b, float f, float* wa, float* wb) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    const bool flip = d < 0.0f;
    if (flip) d = -d;
    if (0.01f < 1.0f - d) {
        const float angle = std::acos(d);
        const float af = angle * f;
        const float s = PS2Sinf(angle);
        *wb = PS2Sinf(af) * (1.0f / s);
        *wa = PS2Sinf(angle - af) * (1.0f / s);
    } else {
        *wa = 1.0f - f;
        *wb = f;
    }
    if (flip) *wa = -*wa;
}

bool same_quat(const float* a, const float* b) {
    return std::fabs(b[0] - a[0]) <= 0.0f && std::fabs(b[1] - a[1]) <= 0.0f && std::fabs(b[2] - a[2]) <= 0.0f &&
           std::fabs(b[3] - a[3]) <= 0.0f;
}

uint32_t clamp_index(int32_t i, uint32_t lo, uint32_t hi) {
    if (i < int32_t(lo)) return lo;
    return int32_t(hi) < i ? hi : uint32_t(i);
}

}  // namespace

// [decomp] 0x001e4330 Spline_Eval3D
void Spline_Eval3D(const _VECTOR* p0, const _VECTOR* p1, const _VECTOR* p2, const _VECTOR* p3, float t,
                   _VECTOR* out) {
    const float t1 = t - 1.0f;
    const float t1sq = t1 * t1;
    const auto axis = [&](float a, float b, float c, float d) {
        return (b + b + t * ((c - t1sq * a) + t * (((b * -5.0f + t * b * 3.0f + c * 4.0f) - t * c * 3.0f) + t1 * d))) *
               0.5f;
    };
    out->x = axis(p0->x, p1->x, p2->x, p3->x);
    out->y = axis(p0->y, p1->y, p2->y, p3->y);
    out->z = axis(p0->z, p1->z, p2->z, p3->z);
}

// [decomp] 0x001e1858 Spline_Interp
// Os quatro quadros em volta de t (presos ao intervalo) dão a posição por Catmull-Rom, ou a posição
// do quadro se os quatro coincidem; a rotação é o slerp entre os dois quadros do meio.
void Spline_Interp(framelist_tag* path, uint16_t first, uint16_t last, float t, MATRIX* m, quaternion_tag* q,
                   SPLINE_INTERP mode) {
    if (path == nullptr) return;
    uint32_t i0, i1, i2, i3;
    float frac;
    if (first < last) {
        const float f = t < float(first) ? float(first) : (t < float(last) ? t : float(last));
        i1 = clamp_index(int32_t(int(f) & 0xffff), first, last);
        i2 = clamp_index(int32_t(i1) + 1, first, last);
        i0 = clamp_index(int32_t(i1) - 1, first, last);
        i3 = clamp_index(int32_t(i1) + 2, first, last);
        frac = f - float(i1);
    } else {
        const float f = t < float(last) ? float(last) : (t < float(first) ? t : float(first));
        i1 = clamp_index(int32_t(int(f + 0.999999f) & 0xffff), last, first);
        i2 = clamp_index(int32_t(i1) - 1, last, first);
        i0 = clamp_index(int32_t(i1) + 1, last, first);
        i3 = clamp_index(int32_t(i1) - 2, last, first);
        frac = float(i1) - f;
    }
    const _VECTOR p0 = frame_pos(path, i0), p1 = frame_pos(path, i1), p2 = frame_pos(path, i2),
                  p3 = frame_pos(path, i3);
    _VECTOR pos;
    if (Vec_IsEqual(&p0, &p1, 0.0f) && Vec_IsEqual(&p1, &p2, 0.0f) && Vec_IsEqual(&p2, &p3, 0.0f)) {
        pos = p0;
    } else {
        Spline_Eval3D(&p0, &p1, &p2, &p3, frac, &pos);
    }
    const float* qa = path[i1].quat;
    const float* qb = path[i2].quat;
    const auto set_pos = [&] {
        m->m[3][0] = pos.x;
        m->m[3][2] = pos.z;
        m->m[3][1] = pos.y;
    };
    if (mode != 0) {
        if (same_quat(qa, qb)) {
            set_pos();
            quat_matrix(qa[0], qa[1], qa[2], qa[3], m);
            return;
        }
        if (mode < 0 || 3 < mode) return;
        float wa, wb;
        slerp_weights(qa, qb, frac, &wa, &wb);
        const float x = wa * qa[0] + wb * qb[0];
        const float y = wa * qa[1] + wb * qb[1];
        const float z = wa * qa[2] + wb * qb[2];
        const float w = wa * qa[3] + wb * qb[3];
        set_pos();
        quat_matrix(x, y, z, w, m);
        if (q != nullptr) {
            q->w = w;
            q->x = x;
            q->y = y;
            q->z = z;
        }
        return;
    }
    m->m[3][2] = pos.z;
    m->m[2][2] = 1.0f;
    m->m[3][0] = pos.x;
    m->m[3][1] = pos.y;
    m->m[0][0] = 1.0f;
    m->m[1][0] = 0.0f;
    m->m[2][0] = 0.0f;
    m->m[0][1] = 0.0f;
    m->m[1][1] = 1.0f;
    m->m[2][1] = 0.0f;
    m->m[0][2] = 0.0f;
    m->m[1][2] = 0.0f;
}

// [decomp] 0x001e1140 KeyFrame_Interp
// Interpolação linear entre os dois quadros em volta de t (sem passar do segundo) e slerp na
// rotação.
void KeyFrame_Interp(framelist_tag* path, uint16_t first, uint16_t last, float t, MATRIX* m) {
    if (path == nullptr) return;
    K1_476 = last;
    if (first < last) {
        const float f = t < float(first) ? float(first) : (t < float(last) ? t : float(last));
        const uint32_t i = uint32_t(int(f)) & 0xffff;
        if (i < first) {
            K1_476 = first;
        } else {
            K1_476 = last < i ? last : uint16_t(int(f));
        }
        K2_477 = uint16_t(K1_476 + 1u <= last ? K1_476 + 1u : last);
        Blend_478 = f - float(K1_476);
    } else {
        const float f = t < float(last) ? float(last) : (t < float(first) ? t : float(first));
        const uint32_t i = uint32_t(int(f + 0.999999f)) & 0xffff;
        if (last <= i) K1_476 = first < i ? first : uint16_t(int(f + 0.999999f));
        const int32_t prev = int32_t(K1_476) - 1;
        K2_477 = last;
        if (int32_t(last) <= prev) K2_477 = uint16_t(prev <= int32_t(first) ? prev : first);
        Blend_478 = float(K1_476) - f;
    }
    const _VECTOR ta = frame_pos(path, K1_476), tb = frame_pos(path, K2_477);
    _VECTOR pos = ta;
    if (!Vec_IsEqual(&ta, &tb, 0.0f)) {
        const auto lerp = [](float a, float b, float blend) {
            const float v = a + (b - a) * blend;
            const bool past = 0.0f <= b - a ? b < v : v < b;
            return past ? b : v;
        };
        pos.x = lerp(ta.x, tb.x, Blend_478);
        pos.y = lerp(ta.y, tb.y, Blend_478);
        pos.z = lerp(ta.z, tb.z, Blend_478);
    }
    const float* ra = path[K1_476].quat;
    const float* rb = path[K2_477].quat;
    if (same_quat(ra, rb)) {
        quat_matrix(ra[0], ra[1], ra[2], ra[3], m);
    } else {
        float wa, wb;
        slerp_weights(ra, rb, Blend_478, &wa, &wb);
        quat_matrix(wa * ra[0] + wb * rb[0], wa * ra[1] + wb * rb[1], wa * ra[2] + wb * rb[2],
                    wa * ra[3] + wb * rb[3], m);
    }
    m->m[3][0] = pos.x;
    m->m[3][1] = pos.y;
    m->m[3][2] = pos.z;
}
