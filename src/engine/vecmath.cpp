// Port das funções de matemática vetorial do ACTION.ELF.
//
// Fonte: C decompilado pelo Ghidra e desmontagem (para as máscaras das instruções da VU0, como
// vmul.xyz, que o decompilador omite). Cada função traz o marcador [decomp] com o endereço original.
//
// Componente w: várias funções escrevem o registrador inteiro da VU0 (sqc2) depois de operar só em
// xyz, e o w resultante é o que sobrou no registrador. Quando isso é determinístico, reproduzimos;
// quando depende do chamador ("lixo"), o w do destino fica como estava, e o comentário diz isso.

#include "engine/vecmath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kPi = 3.1415927f;        // 0x40490fdb
constexpr float kTwoPi = 6.2831855f;     // 0x40c90fdb
constexpr float kHalfPi = 1.5707964f;    // 0x3fc90fdb

// Coeficientes do seno em 0x002f3b20 (iguais em 0x002f3ad0 e 0x002f3ae0):
// sin(x) ~= x + c1*x^3 + c2*x^5 + c0*x^7 + c3*x^9, para x em [-pi/2, pi/2].
constexpr float kSinC0 = -0.00019807414f;
constexpr float kSinC1 = -0.16666657f;
constexpr float kSinC2 = 0.0083330255f;
constexpr float kSinC3 = 2.601887e-06f;

// Redução de ângulo de PS2Sinf: leva a [-pi, pi) somando/subtraindo 2*pi e depois espelha
// para [-pi/2, pi/2].
float reduce(float a) {
    if (kPi <= a) {
        do {
            a -= kTwoPi;
        } while (kPi <= a);
    } else {
        while (a < -kPi) a += kTwoPi;
    }
    if (a > kHalfPi) {
        a = kPi - a;
    } else if (a < -kHalfPi) {
        a = -kPi - a;
    }
    return a;
}

// Mesma ordem de avaliação do microcódigo (vmul/vmula/vmadda), em float.
float sin_poly(float x) {
    const float x2 = x * x;
    const float x4 = x2 * x2;
    const float x8 = x4 * x4;
    float acc = (x * kSinC1) * x2;
    acc += ((x * kSinC0) * x2) * x4;
    acc += (x * kSinC2) * x4;
    acc += x * 1.0f;
    return acc + (x * kSinC3) * x8;
}

}  // namespace

// [decomp] 0x00120700 PS2Sinf
float PS2Sinf(float a) { return sin_poly(reduce(a)); }

// [decomp] 0x0011f968 PS2Sinf2
void PS2Sinf2(float a, float b, float* sa, float* sb) {
    *sa = sin_poly(reduce(a));
    *sb = sin_poly(reduce(b));
}

// [decomp] 0x0011fc20 PS2Sinf3
void PS2Sinf3(float a, float b, float c, float* sa, float* sb, float* sc) {
    *sa = sin_poly(reduce(a));
    *sb = sin_poly(reduce(b));
    *sc = sin_poly(reduce(c));
}

// ------------------------------------------------------------------ vetores

// [decomp] 0x001e2b78 Vec_AngleDifference
float Vec_AngleDifference(float a, float b) {
    if ((b < 0.0f || kTwoPi <= b) && (b = std::fmod(b, kTwoPi), b < 0.0f)) b += kTwoPi;
    if ((a < 0.0f || kTwoPi <= a) && (a = std::fmod(a, kTwoPi), a < 0.0f)) a += kTwoPi;
    float d = b - a;
    if (kPi <= std::fabs(d)) d = (0.0f <= d) ? d - kTwoPi : d + kTwoPi;
    return d;
}

// [decomp] 0x001e0f10 Ang_RotateToBearing
// O alvo e o atual vão para 0..2pi (fmodf) só para achar a diferença; o passo (preso a +-max_step) soma ao atual
// como veio. Depois anda a fração rate do passo, sem passar do fim, e encosta no alvo (o original, não o
// normalizado) a menos de 0,0001.
float Ang_RotateToBearing(float cur, float target, float rate, float max_step) {
    float t = target;
    if ((target < 0.0f || kTwoPi <= target) && (t = std::fmod(target, kTwoPi), t < 0.0f)) t += kTwoPi;
    float c = cur;
    if ((cur < 0.0f || kTwoPi <= cur) && (c = std::fmod(cur, kTwoPi), c < 0.0f)) c += kTwoPi;
    float d = t - c;
    if (!(std::fabs(d) < kPi)) d = d < 0.0f ? d + kTwoPi : d - kTwoPi;
    if (d < -max_step) {
        d = -max_step;
    } else if (max_step < d) {
        d = max_step;
    }
    const float end = cur + d;
    const float step = end - cur;
    float v = cur + step * rate;
    if (0.0f <= step ? end < v : v < end) v = end;
    if (std::fabs(v - target) < 0.0001f) v = target;
    return v;
}

// [decomp] 0x001e2cf8 Vec_Copy2
void Vec_Copy2(const _VECTOR* src, _VECTOR* dst1, _VECTOR* dst2) {
    dst2->x = dst1->x = src->x;
    dst2->y = dst1->y = src->y;
    dst2->z = dst1->z = src->z;
}

// [decomp] 0x001e2d58 Vec_Zero
void Vec_Zero(_VECTOR* v) { v->x = v->y = v->z = 0.0f; }

// [decomp] 0x001e2d70 Vec_Swap
void Vec_Swap(_VECTOR* a, _VECTOR* b) { std::swap(*a, *b); }

// [decomp] 0x001e2d90 Vec_Avg3
void Vec_Avg3(const _VECTOR* a, const _VECTOR* b, const _VECTOR* c, _VECTOR* out) {
    out->x = (a->x + b->x + c->x) * 0.33333334f;
    out->y = (a->y + b->y + c->y) * 0.33333334f;
    out->z = (a->z + b->z + c->z) * 0.33333334f;
}

// [decomp] 0x001e2df8 Vec_Avg2
void Vec_Avg2(const _VECTOR* a, const _VECTOR* b, _VECTOR* out) {
    out->x = (a->x + b->x) * 0.5f;
    out->y = (a->y + b->y) * 0.5f;
    out->z = (a->z + b->z) * 0.5f;
}

// [decomp] 0x001e2e88 Vec_SqDist3D
float Vec_SqDist3D(const _VECTOR* a, const _VECTOR* b) {
    const float dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    return dx * dx + dy * dy + dz * dz;
}

// [decomp] 0x001e2e48 Vec_Dist3D
float Vec_Dist3D(const _VECTOR* a, const _VECTOR* b) { return std::sqrt(Vec_SqDist3D(a, b)); }

// [decomp] 0x001e2ef8 Vec_SqDist2D
float Vec_SqDist2D(const _VECTOR* a, const _VECTOR* b) {
    const float dx = a->x - b->x, dz = a->z - b->z;  // plano XZ (Y é a altura)
    return dx * dx + dz * dz;
}

// [decomp] 0x001e2ec8 Vec_Dist2D
float Vec_Dist2D(const _VECTOR* a, const _VECTOR* b) { return std::sqrt(Vec_SqDist2D(a, b)); }

// [decomp] 0x001e2f58 Vec_SqMagnitude
float Vec_SqMagnitude(const _VECTOR* v) { return v->x * v->x + v->y * v->y + v->z * v->z; }

// [decomp] 0x001e2f20 Vec_Magnitude
float Vec_Magnitude(const _VECTOR* v) { return std::sqrt(Vec_SqMagnitude(v)); }

// [decomp] 0x001e2f88 Vec_NormaliseLen
// w: no caminho normal o original escreve o w que sobrou no registrador (lixo); mantemos o w do destino.
float Vec_NormaliseLen(_VECTOR* out, const _VECTOR* in) {
    const _VECTOR v = *in;
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len == 0.0f) {
        out->x = out->y = out->z = 0.0f;
    } else {
        const float inv = 1.0f / len;
        out->x = v.x * inv;
        out->y = v.y * inv;
        out->z = v.z * inv;
    }
    return len;
}

// [decomp] 0x001e3000 Vec_Normalise
// w: igual a Vec_NormaliseLen.
void Vec_Normalise(_VECTOR* out, const _VECTOR* in) { Vec_NormaliseLen(out, in); }

// [decomp] 0x001e2d28 Vec_UnitVector
// vsub.xyzw seguido de Vec_Normalise(out, out): o w final é from->w se o comprimento não é zero
// (resto de vf1 dentro do Vec_Normalise) e to->w - from->w se é zero.
void Vec_UnitVector(_VECTOR* out, const _VECTOR* from, const _VECTOR* to) {
    const float from_w = from->w;
    *out = {to->x - from->x, to->y - from->y, to->z - from->z, to->w - from->w};
    if (Vec_NormaliseLen(out, out) != 0.0f) out->w = from_w;
}

// [decomp] 0x001e3078 Vec_CrossNormalise
// out = normalize(a x b). w: a->w se o produto vetorial é zero; senão b->w (restos de vf2 e vf1).
void Vec_CrossNormalise(const _VECTOR* a, const _VECTOR* b, _VECTOR* out) {
    const _VECTOR va = *a, vb = *b;
    _VECTOR c{va.y * vb.z - va.z * vb.y, va.z * vb.x - va.x * vb.z, va.x * vb.y - va.y * vb.x, va.w};
    const float len = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
    if (len == 0.0f) {
        *out = {0.0f, 0.0f, 0.0f, va.w};
    } else {
        const float inv = 1.0f / len;
        *out = {c.x * inv, c.y * inv, c.z * inv, vb.w};
    }
}

// [decomp] 0x001e3100 Vec_ScalarTripleProduct
// Atenção: o original calcula a . normalize(b x c), não a . (b x c).
float Vec_ScalarTripleProduct(const _VECTOR* a, const _VECTOR* b, const _VECTOR* c) {
    float nx = b->y * c->z - b->z * c->y;
    float ny = b->z * c->x - b->x * c->z;
    float nz = b->x * c->y - b->y * c->x;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len == 0.0f) {
        nx = ny = nz = 0.0f;
    } else {
        const float inv = 1.0f / len;
        nx *= inv;
        ny *= inv;
        nz *= inv;
    }
    return a->x * nx + a->y * ny + a->z * nz;
}

// [decomp] 0x001e31b0 Vec_NormToRot
void Vec_NormToRot(const _VECTOR* normal, _VECTOR* rot) {
    const float n = normal->x * normal->x + normal->z * normal->z;
    rot->x = -std::atan2(normal->y, std::sqrt(n));
    rot->y = std::atan2(normal->x, normal->z);
    rot->z = 0.0f;
}

// [decomp] 0x001e3218 Vec_DirToRot
void Vec_DirToRot(const _VECTOR* dir, _VECTOR* rot) {
    rot->y = std::atan2(dir->x, dir->z);
    rot->z = 0.0f;
    rot->x = -std::atan2(dir->y, std::sqrt(dir->x * dir->x + dir->z * dir->z));
}

// [decomp] 0x001e4b40 Vec_Mul
void Vec_Mul(_VECTOR* v, const _VECTOR* s) {
    v->x *= s->x;
    v->y *= s->y;
    v->z *= s->z;
}

// [decomp] 0x001e4b78 Vec_MakeOrthonormalBasis
// Dado n, monta u e v perpendiculares (n não precisa ser unitário para u; v herda o módulo de n).
void Vec_MakeOrthonormalBasis(_VECTOR* u, _VECTOR* v, _VECTOR* n) {
    if (std::fabs(n->x) <= std::fabs(n->y)) {
        const float inv = 1.0f / std::sqrt(n->z * n->z + n->y * n->y);
        u->x = 0.0f;
        u->y = -n->z * inv;
        u->z = n->y * inv;
        v->x = n->y * u->z - n->z * u->y;
        v->y = -n->x * u->z;
        v->z = n->x * u->y;
    } else {
        const float inv = 1.0f / std::sqrt(n->z * n->z + n->x * n->x);
        u->y = 0.0f;
        u->x = n->z * inv;
        u->z = -n->x * inv;
        v->x = n->y * u->z;
        v->y = n->z * u->x - n->x * u->z;
        v->z = -n->y * u->x;
    }
}

// Ghidra mostra min.S/max.S do EE como seleção por máscara; o efeito é o mínimo/máximo comum.
// [decomp] 0x001e4c90 Vec_Max
void Vec_Max(const _VECTOR* a, const _VECTOR* b, _VECTOR* out) {
    out->x = std::max(a->x, b->x);
    out->y = std::max(a->y, b->y);
    out->z = std::max(a->z, b->z);
}

// [decomp] 0x001e4cc8 Vec_Min
void Vec_Min(const _VECTOR* a, const _VECTOR* b, _VECTOR* out) {
    out->x = std::min(a->x, b->x);
    out->y = std::min(a->y, b->y);
    out->z = std::min(a->z, b->z);
}

// [decomp] 0x001e4d00 Vec_MinMax
void Vec_MinMax(const _VECTOR* a, const _VECTOR* b, _VECTOR* out_min, _VECTOR* out_max) {
    const _VECTOR va = *a, vb = *b;
    Vec_Min(&va, &vb, out_min);
    Vec_Max(&va, &vb, out_max);
}

// [decomp] 0x001e4d68 Vec_MinMax3
void Vec_MinMax3(const _VECTOR* a, const _VECTOR* b, const _VECTOR* c, _VECTOR* out_min, _VECTOR* out_max) {
    const _VECTOR va = *a, vb = *b, vc = *c;
    out_min->x = std::min(std::min(va.x, vb.x), vc.x);
    out_min->y = std::min(std::min(va.y, vb.y), vc.y);
    out_min->z = std::min(std::min(va.z, vb.z), vc.z);
    out_max->x = std::max(std::max(va.x, vb.x), vc.x);
    out_max->y = std::max(std::max(va.y, vb.y), vc.y);
    out_max->z = std::max(std::max(va.z, vb.z), vc.z);
}

// [decomp] 0x001e4e00 Vec_IsEqual
bool Vec_IsEqual(const _VECTOR* a, const _VECTOR* b, float tolerance) {
    return std::fabs(b->x - a->x) <= tolerance && std::fabs(b->y - a->y) <= tolerance &&
           std::fabs(b->z - a->z) <= tolerance;
}

// [decomp] 0x001e4e68 Vec_Negate
void Vec_Negate(const _VECTOR* in, _VECTOR* out) {
    out->x = -in->x;
    out->y = -in->y;
    out->z = -in->z;
}

// [decomp] 0x001e4e90 Vec_MulR32
void Vec_MulR32(_VECTOR* out, const _VECTOR* in, float s) {
    out->x = in->x * s;
    out->y = in->y * s;
    out->z = in->z * s;
}

// [decomp] 0x001e4eb8 Vec_MulR32
// Apesar do tipo _VECTOR4, o original também só escala x, y e z.
void Vec_MulR32(_VECTOR4* out, const _VECTOR4* in, float s) {
    out->x = in->x * s;
    out->y = in->y * s;
    out->z = in->z * s;
}

// [decomp] 0x001e4ee0 Vec_MulR32
void Vec_MulR32(_VECTOR* out, const _VECTOR* in, float sx, float sy, float sz) {
    out->x = in->x * sx;
    out->y = in->y * sy;
    out->z = in->z * sz;
}

// ------------------------------------------------------------------ matrizes

// [decomp] 0x001e3348 Mat_Copy
void Mat_Copy(const MATRIX* src, MATRIX* dst) { std::memmove(dst, src, sizeof(MATRIX)); }

// [decomp] 0x001e3370 Mat_CopyRot
// Copia as 3 primeiras linhas inteiras (0x30 bytes), inclusive a coluna w.
void Mat_CopyRot(const MATRIX* src, MATRIX* dst) { std::memmove(dst, src, 0x30); }

namespace {
void get_row(_VECTOR* out, const MATRIX* m, int row) {
    out->x = m->m[row][0];
    out->y = m->m[row][1];
    out->z = m->m[row][2];
}
void set_row(const _VECTOR* v, MATRIX* m, int row) {
    m->m[row][0] = v->x;
    m->m[row][1] = v->y;
    m->m[row][2] = v->z;
}
}  // namespace

// Linhas da matriz: 0 = "norm" (lateral), 1 = "up", 2 = "dir" (frente), 3 = translação.
// [decomp] 0x001e3398 Mat_GetDir
void Mat_GetDir(_VECTOR* out, MATRIX* m) { get_row(out, m, 2); }
// [decomp] 0x001e33b8 Mat_GetUp
void Mat_GetUp(_VECTOR* out, MATRIX* m) { get_row(out, m, 1); }
// [decomp] 0x001e33d8 Mat_GetNorm
void Mat_GetNorm(_VECTOR* out, MATRIX* m) { get_row(out, m, 0); }
// [decomp] 0x001e33f8 Mat_GetTrans
void Mat_GetTrans(_VECTOR* out, MATRIX* m) { get_row(out, m, 3); }
// [decomp] 0x001e3418 Mat_SetDir
void Mat_SetDir(_VECTOR* v, MATRIX* m) { set_row(v, m, 2); }
// [decomp] 0x001e3438 Mat_SetUp
void Mat_SetUp(_VECTOR* v, MATRIX* m) { set_row(v, m, 1); }
// [decomp] 0x001e3458 Mat_SetNorm
void Mat_SetNorm(_VECTOR* v, MATRIX* m) { set_row(v, m, 0); }
// [decomp] 0x001e3478 Mat_SetTrans
void Mat_SetTrans(_VECTOR* v, MATRIX* m) { set_row(v, m, 3); }

// [decomp] 0x001e3498 Mat_Identity
// Só a parte 3x3: a coluna w e a translação ficam como estavam.
void Mat_Identity(MATRIX* m) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) m->m[r][c] = (r == c) ? 1.0f : 0.0f;
    }
}

// [decomp] 0x001e34c8 Mat_IdentityT
// Zera a matriz inteira e põe 1 na diagonal 3x3. Atenção: m[3][3] fica 0, como no original.
void Mat_IdentityT(MATRIX* m) {
    std::memset(m, 0, sizeof(MATRIX));
    m->m[0][0] = m->m[1][1] = m->m[2][2] = 1.0f;
}

// [decomp] 0x001e3520 Mat_Inverse
// Inversa só para matrizes de rotação pura: transpõe o 3x3 e troca o sinal da translação SEM girá-la.
// Com rotação diferente de identidade o resultado não é a inversa (comportamento do original).
void Mat_Inverse(MATRIX* m) {
    std::swap(m->m[0][1], m->m[1][0]);
    std::swap(m->m[0][2], m->m[2][0]);
    std::swap(m->m[1][2], m->m[2][1]);
    m->m[3][0] = -m->m[3][0];
    m->m[3][1] = -m->m[3][1];
    m->m[3][2] = -m->m[3][2];
}

// [decomp] 0x001e3578 Mat_Inverse
// Mesma ressalva da versão no lugar. A coluna w de out não é escrita.
void Mat_Inverse(const MATRIX* in, MATRIX* out) {
    const MATRIX s = *in;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out->m[r][c] = s.m[c][r];
    }
    out->m[3][0] = -s.m[3][0];
    out->m[3][1] = -s.m[3][1];
    out->m[3][2] = -s.m[3][2];
}

// [decomp] 0x001e4620 MatrixScale
// Escala as linhas 0, 1 e 2 por s (a translação fica).
void MatrixScale(MATRIX* m, float s) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) m->m[r][c] *= s;
    }
}

// [decomp] 0x001e35f0 Mat_Scale3f
// Escala as linhas 0, 1 e 2 (eixos lateral, cima e frente) por sx, sy e sz; copia a translação.
void Mat_Scale3f(MATRIX* out, MATRIX* in, float sx, float sy, float sz) {
    const MATRIX s = *in;
    const float k[3] = {sx, sy, sz};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out->m[r][c] = s.m[r][c] * k[r];
    }
    out->m[3][0] = s.m[3][0];
    out->m[3][1] = s.m[3][1];
    out->m[3][2] = s.m[3][2];
}

// [decomp] 0x001e3280 Mat_World2ViewMat
// Inversa correta de uma matriz rígida (rotação + translação): view = transposta, -t * R^T.
void Mat_World2ViewMat(MATRIX* world, MATRIX* view) {
    const MATRIX w = *world;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) view->m[r][c] = w.m[c][r];
    }
    for (int c = 0; c < 3; ++c) {
        view->m[3][c] = -(w.m[0][c] * w.m[3][0] + w.m[1][c] * w.m[3][1] + w.m[2][c] * w.m[3][2]);
    }
}

namespace {
// Senos e cossenos de RotMatrix/RotTransMatrix/Mat_Rotate, com as mesmas chamadas ao PS2Sinf3.
struct EulerTrig {
    float cx, sx, cy, sy, cz, sz;
};
EulerTrig euler_trig(const _VECTOR* e) {
    EulerTrig t;
    PS2Sinf3(e->x + kHalfPi, e->x, e->y + kHalfPi, &t.cx, &t.sx, &t.cy);
    PS2Sinf3(e->z + kHalfPi, e->z, e->y, &t.cz, &t.sz, &t.sy);
    return t;
}
void rot_xyz(const EulerTrig& t, MATRIX* m) {
    m->m[2][0] = t.sy;
    m->m[2][2] = t.cx * t.cy;
    m->m[1][0] = t.cy * -t.sz;
    m->m[2][1] = -t.sx * t.cy;
    m->m[0][1] = t.sx * t.sy * t.cz + t.cx * t.sz;
    m->m[1][1] = t.cx * t.cz - t.sx * t.sy * t.sz;
    m->m[0][2] = t.cx * -t.sy * t.cz + t.sx * t.sz;
    m->m[1][2] = t.sx * t.cz - t.cx * -t.sy * t.sz;
    m->m[0][0] = t.cy * t.cz;
}
}  // namespace

// [decomp] 0x001e2368 RotMatrix
// Só a parte 3x3. É a rotação usada no posicionamento das fases (bloco 0x1A).
void RotMatrix(const _VECTOR* euler, MATRIX* m) { rot_xyz(euler_trig(euler), m); }

// [decomp] 0x001def70 Mat_Rotate
// Idêntica a RotMatrix no binário.
void Mat_Rotate(_VECTOR* euler, MATRIX* m) { rot_xyz(euler_trig(euler), m); }

// [decomp] 0x001e2478 RotTransMatrix
void RotTransMatrix(const _VECTOR* euler, const _VECTOR* trans, MATRIX* m) {
    rot_xyz(euler_trig(euler), m);
    m->m[3][0] = trans->x;
    m->m[3][1] = trans->y;
    m->m[3][2] = trans->z;
}

// [decomp] 0x001e25a8 RotMatrixZYX
void RotMatrixZYX(const _VECTOR* euler, MATRIX* m) {
    const EulerTrig t = euler_trig(euler);
    m->m[2][0] = t.cz * t.sy * t.cx + t.sz * t.sx;
    m->m[1][0] = t.cz * t.sy * t.sx - t.sz * t.cx;
    m->m[1][1] = t.sz * t.sy * t.sx + t.cz * t.cx;
    m->m[2][1] = t.sz * t.sy * t.cx - t.cz * t.sx;
    m->m[2][2] = t.cy * t.cx;
    m->m[0][1] = t.sz * t.cy;
    m->m[0][2] = -t.sy;
    m->m[1][2] = t.cy * t.sx;
    m->m[0][0] = t.cz * t.cy;
}

// [decomp] 0x001e2820 MulMatrix0
void MulMatrix0(const MATRIX* a, const MATRIX* b, MATRIX* out) {
    for (int r = 0; r < 3; ++r) {
        const float x = b->m[r][0], y = b->m[r][1], z = b->m[r][2];
        for (int c = 0; c < 3; ++c) out->m[r][c] = a->m[0][c] * x + a->m[1][c] * y + a->m[2][c] * z;
    }
}

// [decomp] 0x001e26b0 MulMatrix2
// Como MulMatrix0 com o resultado em b: cada linha de b (lida antes de ser escrita) vezes a.
void MulMatrix2(const MATRIX* a, MATRIX* b) {
    for (int r = 0; r < 3; ++r) {
        const float x = b->m[r][0], y = b->m[r][1], z = b->m[r][2];
        for (int c = 0; c < 3; ++c) b->m[r][c] = a->m[0][c] * x + a->m[1][c] * y + a->m[2][c] * z;
    }
}

// [decomp] 0x001e46a0 RotMatrixX
void RotMatrixX(float a, MATRIX* m) {
    float c, s;
    PS2Sinf2(a + kHalfPi, a, &c, &s);
    m->m[0][0] = 1.0f;
    m->m[0][1] = m->m[0][2] = m->m[1][0] = m->m[2][0] = 0.0f;
    m->m[1][1] = c;
    m->m[1][2] = s;
    m->m[2][1] = -s;
    m->m[2][2] = c;
}

// [decomp] 0x001e4718 RotMatrixY
void RotMatrixY(float a, MATRIX* m) {
    float c, s;
    PS2Sinf2(a + kHalfPi, a, &c, &s);
    m->m[0][0] = c;
    m->m[0][1] = 0.0f;
    m->m[0][2] = -s;
    m->m[1][0] = 0.0f;
    m->m[1][1] = 1.0f;
    m->m[1][2] = 0.0f;
    m->m[2][0] = s;
    m->m[2][1] = 0.0f;
    m->m[2][2] = c;
}

// [decomp] 0x001e4798 RotMatrixZ
void RotMatrixZ(float a, MATRIX* m) {
    float c, s;
    PS2Sinf2(a + kHalfPi, a, &c, &s);
    m->m[0][0] = c;
    m->m[0][1] = s;
    m->m[0][2] = 0.0f;
    m->m[1][0] = -s;
    m->m[1][1] = c;
    m->m[1][2] = 0.0f;
    m->m[2][0] = m->m[2][1] = 0.0f;
    m->m[2][2] = 1.0f;
}

// [decomp] 0x001e4818 RotMatrixXY
void RotMatrixXY(float ax, float ay, MATRIX* m) {
    float cy, sy, cx, sx;
    PS2Sinf2(ay + kHalfPi, ay, &cy, &sy);
    PS2Sinf2(ax + kHalfPi, ax, &cx, &sx);
    m->m[0][0] = cy;
    m->m[0][1] = 0.0f;
    m->m[0][2] = -sy;
    m->m[1][0] = sy * sx;
    m->m[1][1] = cx;
    m->m[1][2] = cy * sx;
    m->m[2][0] = sy * cx;
    m->m[2][1] = -sx;
    m->m[2][2] = cy * cx;
}

// [decomp] 0x001e29d8 RotTransMat
// b = b * a nas linhas de rotação (vetor-linha) e soma as translações (sem girar a de b).
void RotTransMat(const MATRIX* a, MATRIX* b) {
    const MATRIX s = *b;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            b->m[r][c] = a->m[0][c] * s.m[r][0] + a->m[1][c] * s.m[r][1] + a->m[2][c] * s.m[r][2];
        }
    }
    b->m[3][0] = s.m[3][0] + a->m[3][0];
    b->m[3][1] = s.m[3][1] + a->m[3][1];
    b->m[3][2] = s.m[3][2] + a->m[3][2];
}

// [decomp] 0x001e49e8 RotPreTransVec
// out = (in + t) * R, com R a parte 3x3 de m e t a translação de m.
void RotPreTransVec(const MATRIX* m, const _VECTOR* in, _VECTOR* out) {
    const float x = in->x + m->m[3][0], y = in->y + m->m[3][1], z = in->z + m->m[3][2];
    out->x = m->m[0][0] * x + m->m[1][0] * y + m->m[2][0] * z;
    out->y = m->m[0][1] * x + m->m[1][1] * y + m->m[2][1] * z;
    out->z = m->m[0][2] * x + m->m[1][2] * y + m->m[2][2] * z;
}

// ------------------------------------------------------------------ quatérnios

// [decomp] 0x001e3ae8 Quat_IsEqual
bool Quat_IsEqual(const quaternion_tag* a, const quaternion_tag* b, float tolerance) {
    return std::fabs(b->x - a->x) <= tolerance && std::fabs(b->y - a->y) <= tolerance &&
           std::fabs(b->z - a->z) <= tolerance && std::fabs(b->w - a->w) <= tolerance;
}

// [decomp] 0x001e3b80 Quat_Copy
void Quat_Copy(quaternion_tag* dst, const quaternion_tag* src) { *dst = *src; }

// [decomp] 0x001e3ba8 Quat_QuaternionMultiply
// Convenção (w, x, y, z): o componente 0 é o escalar. Os campos x/y/z/w da struct são só posições.
void Quat_QuaternionMultiply(const quaternion_tag* a, const quaternion_tag* b, quaternion_tag* out) {
    const float a0 = a->x, a1 = a->y, a2 = a->z, a3 = a->w;
    const float b0 = b->x, b1 = b->y, b2 = b->z, b3 = b->w;
    out->x = ((a0 * b0 - a1 * b1) - a2 * b2) - a3 * b3;
    out->y = (a2 * b3 - a3 * b2) + a0 * b1 + a1 * b0;
    out->z = (a3 * b1 - a1 * b3) + a0 * b2 + a2 * b0;
    out->w = (a1 * b2 - a2 * b1) + a0 * b3 + a3 * b0;
}

// [decomp] 0x001e3cb0 Quat_Rot_Vec
// Convenção (x, y, z, w). out = q * in * q^-1 pela forma t = 2 (q.xyz x v); v + w t + q.xyz x t.
void Quat_Rot_Vec(const quaternion_tag* q, const _VECTOR* in, _VECTOR* out) {
    const float vx = in->x, vy2 = in->y + in->y, vz2 = in->z + in->z, vx2 = vx + vx;
    const float qx = q->x, qy = q->y, qz = q->z, qw = q->w;
    const float t3 = qx * vy2 - qy * vx2;
    const float t1 = qy * vz2 - qz * vy2;
    const float t2 = qz * vx2 - qx * vz2;
    const float iy = in->y, iz = in->z;
    out->x = t1 * qw + vx + (qy * t3 - qz * t2);
    out->y = t2 * qw + iy + (qz * t1 - qx * t3);
    out->z = t3 * qw + iz + (qx * t2 - qy * t1);
}

// [decomp] 0x001e3d78 Quat_EulerToQuat
// Atenção: não corresponde a RotMatrix para rotações em mais de um eixo (ordem diferente).
void Quat_EulerToQuat(const _VECTOR* euler, quaternion_tag* q) {
    const float hx = euler->x * 0.5f, hy = euler->y * 0.5f, hz = euler->z * 0.5f;
    float cx, sx, cy, cz, sz, sy;
    PS2Sinf3(hx + kHalfPi, hx, hy + kHalfPi, &cx, &sx, &cy);
    PS2Sinf3(hz + kHalfPi, hz, hy, &cz, &sz, &sy);
    q->x = cy * sx * cz - sy * cx * sz;
    q->z = cy * cx * sz - sy * sx * cz;
    q->y = cy * sx * sz + sy * cx * cz;
    q->w = cy * cx * cz + sy * sx * sz;
}

namespace {
void quat_rot(const quaternion_tag* q, MATRIX* m) {
    const float x = q->x, y = q->y, z = q->z, w = q->w;
    const float y2 = y + y, z2 = z + z, xx2 = (x + x) * x, xw2 = (x + x) * w;
    m->m[2][0] = z2 * x + y2 * w;
    m->m[1][0] = y2 * x - z2 * w;
    m->m[2][1] = z2 * y - xw2;
    m->m[2][2] = 1.0f - (xx2 + y2 * y);
    m->m[0][0] = 1.0f - (y2 * y + z2 * z);
    m->m[0][1] = y2 * x + z2 * w;
    m->m[1][2] = z2 * y + xw2;
    m->m[1][1] = 1.0f - (xx2 + z2 * z);
    m->m[0][2] = z2 * x - y2 * w;
}
}  // namespace

// [decomp] 0x001e3e80 Quat_QuatToMat
// Só a parte 3x3. Em vetor-linha, equivale ao glTF em vetor-coluna com o mesmo quatérnio.
void Quat_QuatToMat(const quaternion_tag* q, MATRIX* m) { quat_rot(q, m); }

// [decomp] 0x001e3f20 Quat_AxisAngleToQuat
// Entrada: eixo em x, y, z e ângulo em w.
void Quat_AxisAngleToQuat(const quaternion_tag* axis_angle, quaternion_tag* q) {
    const float half = axis_angle->w * 0.5f;
    float s;
    PS2Sinf2(half + kHalfPi, half, &q->w, &s);
    q->x = s * axis_angle->x;
    q->y = s * axis_angle->y;
    q->z = s * axis_angle->z;
}

// [decomp] 0x001e3fa8 Quat_QuatTransToMat
void Quat_QuatTransToMat(const quaternion_tag* q, const _VECTOR* trans, MATRIX* m) {
    quat_rot(q, m);
    m->m[3][0] = trans->x;
    m->m[3][1] = trans->y;
    m->m[3][2] = trans->z;
}

// [decomp] 0x001e4060 Quat_Normalise
// Quatérnio nulo vira (1, 0, 0, 0) na posição 0 (convenção escalar-primeiro do código antigo).
void Quat_Normalise(quaternion_tag* q) {
    if (q->x == 0.0f && q->y == 0.0f && q->z == 0.0f && q->w == 0.0f) {
        q->x = 1.0f;
        return;
    }
    const float inv = 1.0f / std::sqrt(q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w);
    q->w *= inv;
    q->x *= inv;
    q->y *= inv;
    q->z *= inv;
}

// [decomp] 0x001e4120 Quat_Slerp
// Não interpola: devolve a se t < 0.5, senão b (comportamento do original). A interpolação de
// verdade é Quat_Slerp_Acc.
void Quat_Slerp(float t, const quaternion_tag* a, const quaternion_tag* b, quaternion_tag* out) {
    *out = (0.5f <= t) ? *b : *a;
}

// [decomp] 0x001e4188 Quat_Slerp_Acc
void Quat_Slerp_Acc(float t, const quaternion_tag* a, const quaternion_tag* b, quaternion_tag* out) {
    float cosom = a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w;
    const bool flip = cosom < 0.0f;
    if (flip) cosom = -cosom;
    float sa, sb;
    if (0.01f < 1.0f - cosom) {
        const float omega = std::acos(cosom);
        const float wt = omega * t;
        const float inv = 1.0f / PS2Sinf(omega);
        sb = PS2Sinf(wt) * inv;
        sa = PS2Sinf(omega - wt) * inv;
    } else {
        sa = 1.0f - t;
        sb = t;
    }
    if (flip) sa = -sa;
    const quaternion_tag qa = *a, qb = *b;
    out->x = sa * qa.x + sb * qb.x;
    out->y = sa * qa.y + sb * qb.y;
    out->z = sa * qa.z + sb * qb.z;
    out->w = sa * qa.w + sb * qb.w;
}

// [decomp] 0x001e02a0 Quat_QuaternionToMatrix
// Convenção escalar-primeiro (w, x, y, z) nas posições 0..3. Só a parte 3x3.
void Quat_QuaternionToMatrix(MATRIX* m, const quaternion_tag* q) {
    const float a = q->x, b = q->y, c = q->z, d = q->w;  // a = escalar
    float t;
    t = b * b + a * a;
    m->m[0][0] = (t + t) - 1.0f;
    t = b * c + d * a;
    m->m[0][1] = t + t;
    t = d * b - c * a;
    m->m[0][2] = t + t;
    t = b * c - d * a;
    m->m[1][0] = t + t;
    t = c * c + a * a;
    m->m[1][1] = (t + t) - 1.0f;
    t = c * d + b * a;
    m->m[1][2] = t + t;
    t = d * b + c * a;
    m->m[2][0] = t + t;
    t = c * d - b * a;
    m->m[2][1] = t + t;
    t = d * d + a * a;
    m->m[2][2] = (t + t) - 1.0f;
}

// [decomp] 0x001e03f0 Quat_MatToQuat
// Convenção (x, y, z, w). Inverso de Quat_QuatToMat.
void Quat_MatToQuat(quaternion_tag* q, const MATRIX* m) {
    const float tr = m->m[0][0] + m->m[1][1] + m->m[2][2];
    if (0.0f <= tr) {
        const float s = std::sqrt(tr + 1.0f);
        const float f = 0.5f / s;
        q->w = s * 0.5f;
        q->x = (m->m[1][2] - m->m[2][1]) * f;
        q->y = (m->m[2][0] - m->m[0][2]) * f;
        q->z = (m->m[0][1] - m->m[1][0]) * f;
        return;
    }
    static const int next[3] = {1, 2, 0};  // tabela em 0x00305a10
    int i = (m->m[0][0] < m->m[1][1]) ? 1 : 0;
    if (m->m[i][i] < m->m[2][2]) i = 2;
    const int j = next[i], k = next[j];
    float out[4];
    float s = std::sqrt((m->m[i][i] - (m->m[j][j] + m->m[k][k])) + 1.0f);
    out[i] = s * 0.5f;
    if (s != 0.0f) s = 0.5f / s;
    out[3] = (m->m[j][k] - m->m[k][j]) * s;
    out[j] = (m->m[i][j] + m->m[j][i]) * s;
    out[k] = (m->m[i][k] + m->m[k][i]) * s;
    q->x = out[0];
    q->w = out[3];
    q->y = out[1];
    q->z = out[2];
}

// ------------------------------------------------------------------ constantes globais

// Preenchidas por bootup_bootup (0x001c7e78) no PS2; aqui com o mesmo valor desde o início.
// O w fica 0 (BSS), como no original.
const _VECTOR CONSTANT_ZERO_VECTOR = {0.0f, 0.0f, 0.0f, 0.0f};  // 0x002d6200
const _VECTOR CONSTANT_UP1 = {0.0f, 1.0f, 0.0f, 0.0f};          // 0x002d6210: "para cima" padrão
const _VECTOR CONSTANT_UP2 = {1.0f, 0.0f, 0.0f, 0.0f};          // 0x002d6220: alternativo
const MATRIX CONSTANT_MAT_IDENT = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}}};  // Mat_IdentityT

// ------------------------------------------------------------------ eixos e alinhamento

namespace {

_VECTOR cross(const _VECTOR& a, const _VECTOR& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, 0.0f};
}

// Normalização dos alinhamentos: vetor nulo vira (0, 0, 0).
_VECTOR unit(const _VECTOR& v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len == 0.0f) return {0.0f, 0.0f, 0.0f, v.w};
    const float inv = 1.0f / len;
    return {v.x * inv, v.y * inv, v.z * inv, v.w};
}

float dot3(const _VECTOR& a, const _VECTOR& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

void set_row3(MATRIX* m, int row, const _VECTOR& v) {
    m->m[row][0] = v.x;
    m->m[row][1] = v.y;
    m->m[row][2] = v.z;
}

// Base de Mat_LookAt e Mat_Align2Dir: lateral = up x dir (ou alt x dir se up e dir quase paralelos),
// cima = dir x lateral. Linhas: 0 lateral, 1 cima, 2 frente.
void basis_from_dir(MATRIX* m, const _VECTOR& dir, const _VECTOR* up, const _VECTOR* alt) {
    const _VECTOR& u = (0.999f < std::fabs(dot3(*up, dir))) ? *alt : *up;
    const _VECTOR side = unit(cross(u, dir));
    const _VECTOR up2 = unit(cross(dir, side));
    set_row3(m, 2, dir);
    set_row3(m, 1, up2);
    set_row3(m, 0, side);
}

}  // namespace

// [decomp] 0x001df088 Mat_LookAt
// Matriz de câmera/objeto em eye olhando para target. A linha 3 recebe eye inteiro, inclusive w.
void Mat_LookAt(MATRIX* m, const _VECTOR* eye, const _VECTOR* target, const _VECTOR* up, const _VECTOR* alt_up) {
    const _VECTOR e = *eye;
    const _VECTOR dir = unit({target->x - e.x, target->y - e.y, target->z - e.z, 0.0f});
    basis_from_dir(m, dir, up, alt_up);
    m->m[3][0] = e.x;
    m->m[3][1] = e.y;
    m->m[3][2] = e.z;
    m->m[3][3] = e.w;
}

// [decomp] 0x001df2a8 Mat_Align2Dir
// Só as 3 linhas de rotação; a translação não é tocada.
void Mat_Align2Dir(MATRIX* m, const _VECTOR* dir, const _VECTOR* up, const _VECTOR* alt_up) {
    basis_from_dir(m, unit(*dir), up, alt_up);
}

// [decomp] 0x001df4b0 Mat_Align2Up
// lateral = normalize(up x dir); frente = normalize(lateral x up); a linha de cima recebe up sem
// normalizar (como no original).
void Mat_Align2Up(MATRIX* m, const _VECTOR* up, const _VECTOR* dir) {
    const _VECTOR u = *up;
    const _VECTOR side = unit(cross(u, *dir));
    const _VECTOR fwd = unit(cross(side, u));
    set_row3(m, 2, fwd);
    set_row3(m, 1, u);
    set_row3(m, 0, side);
}

// [decomp] 0x001df5f8 Mat_Normalize
// Reortogonaliza a partir da frente e de cima: lateral = normalize(cima x frente),
// cima = frente x lateral (sem normalizar de novo, como no original).
void Mat_Normalize(MATRIX* m) {
    const _VECTOR dir = unit({m->m[2][0], m->m[2][1], m->m[2][2], 0.0f});
    const _VECTOR up = unit({m->m[1][0], m->m[1][1], m->m[1][2], 0.0f});
    const _VECTOR side = unit(cross(up, dir));
    const _VECTOR up2 = cross(dir, side);
    set_row3(m, 2, dir);
    set_row3(m, 1, up2);
    set_row3(m, 0, side);
}

// [decomp] 0x001ded10 Vec_MatToEuler
// Extrai ângulos de Euler numa das 6 ordens de eixo (RotOrder 0-5).
void Vec_MatToEuler(_VECTOR* out, const MATRIX* m, int order) {
    int i, j, k;
    switch (order) {
        case 0: i = 2; j = 1; k = 0; break;
        case 1: i = 1; j = 2; k = 0; break;
        case 3: i = 0; j = 2; k = 1; break;
        case 4: i = 1; j = 0; k = 2; break;
        case 5: i = 0; j = 1; k = 2; break;
        default: i = 2; j = 0; k = 1; break;
    }
    const float cy = std::sqrt(m->m[i][i] * m->m[i][i] + m->m[j][i] * m->m[j][i]);
    float ax, ay, az;
    if (1.9073486e-06f < cy) {
        ax = std::atan2(m->m[k][j], m->m[k][k]);
        ay = std::atan2(-m->m[k][i], cy);
        az = std::atan2(m->m[j][i], m->m[i][i]);
    } else {
        ax = std::atan2(-m->m[j][k], m->m[j][j]);
        ay = std::atan2(-m->m[k][i], cy);
        az = 0.0f;
    }
    switch (order) {
        case 0: out->x = -az; out->y = -ay; out->z = -ax; break;
        case 1: out->x = az; out->y = ax; out->z = ay; break;
        case 3: out->x = -ax; out->y = -az; out->z = -ay; break;
        case 4: out->x = -ay; out->y = -ax; out->z = -az; break;
        case 5: out->x = ax; out->y = ay; out->z = az; break;
        default: out->x = ay; out->y = az; out->z = ax; break;
    }
}

// [decomp] 0x001e20a0 Vec_Track
// Gira m em direção a "de b para a" interpolando por t (slerp com PS2Sinf, como Quat_Slerp_Acc).
// Devolve o quatérnio em q, escreve a rotação em m e reortogonaliza.
void Vec_Track(float t, _VECTOR* a, _VECTOR* b, MATRIX* m, quaternion_tag* q) {
    const _VECTOR dir = unit({a->x - b->x, a->y - b->y, a->z - b->z, 0.0f});
    MATRIX target{};
    Mat_Align2Dir(&target, &dir, &CONSTANT_UP1, &CONSTANT_UP2);
    quaternion_tag q0, q1;
    Quat_MatToQuat(&q0, m);
    Quat_MatToQuat(&q1, &target);
    Quat_Slerp_Acc(t, &q0, &q1, q);
    Quat_QuatToMat(q, m);
    Mat_Normalize(m);
}

// [decomp] 0x001e0600 Quat_MatrixToQuaternion
// Convenção escalar-primeiro do código antigo (posição 0 = escalar). No caminho do traço o escalar sai
// negativo (-0.5 * s). É o par de Quat_QuaternionToMatrix: a ida e volta devolve a mesma matriz nos
// quatro ramos (verificado em tests/test_vecmath2.cpp).
void Quat_MatrixToQuaternion(quaternion_tag* q, const MATRIX* m) {
    float out[4];
    const float m00 = m->m[0][0], m11 = m->m[1][1], m22 = m->m[2][2];
    const float tr = m00 + m11 + m22;
    if (0.0f <= tr) {
        const float s = std::sqrt(tr + 1.0f);
        const float f = 0.5f / s;
        q->x = s * -0.5f;
        q->y = (m->m[2][1] - m->m[1][2]) * f;
        q->z = (m->m[0][2] - m->m[2][0]) * f;
        q->w = (m->m[1][0] - m->m[0][1]) * f;
        return;
    }
    int i = (m00 < m11) ? 1 : 0;
    if (m->m[i][i] < m22) i = 2;
    float s, f, a, b;
    if (i == 1) {
        s = std::sqrt((m11 - (m22 + m00)) + 1.0f);
        f = 0.5f / s;
        out[2] = s * 0.5f;
        out[3] = (m->m[1][2] + m->m[2][1]) * f;
        out[1] = (m->m[0][1] + m->m[1][0]) * f;
        a = m->m[2][0];
        b = m->m[0][2];
    } else if (i == 0) {
        s = std::sqrt((m00 - (m11 + m22)) + 1.0f);
        f = 0.5f / s;
        out[1] = s * 0.5f;
        out[2] = (m->m[0][1] + m->m[1][0]) * f;
        out[3] = (m->m[2][0] + m->m[0][2]) * f;
        a = m->m[1][2];
        b = m->m[2][1];
    } else {
        s = std::sqrt((m22 - (m00 + m11)) + 1.0f);
        f = 0.5f / s;
        out[3] = s * 0.5f;
        out[1] = (m->m[2][0] + m->m[0][2]) * f;
        out[2] = (m->m[1][2] + m->m[2][1]) * f;
        a = m->m[0][1];
        b = m->m[1][0];
    }
    q->y = out[1];
    q->z = out[2];
    q->w = out[3];
    q->x = (a - b) * f;
}

// [decomp] 0x001e0820 Vec_LineSphereIntersection
// Reta por a e b contra a esfera (centro, raio). Devolve false se não toca. Se toca, out recebe o
// ponto mais distante de a ao longo da direção a -> b (raiz +). w: o original soma a.w com um resto
// de registrador (vmulx.xyz não escreve w de vf6/vf7), então é indefinido; usamos a.w.
bool Vec_LineSphereIntersection(const _VECTOR* a, const _VECTOR* b, const _VECTOR* center, float radius,
                                _VECTOR* out) {
    const _VECTOR dir = unit({b->x - a->x, b->y - a->y, b->z - a->z, 0.0f});
    const _VECTOR to_c{center->x - a->x, center->y - a->y, center->z - a->z, 0.0f};
    const float proj = dot3(dir, to_c);
    const float disc = std::sqrt((proj * proj - dot3(to_c, to_c)) + radius * radius);
    if (!(0.0f <= disc)) return false;  // raiz de negativo dá NaN: sem interseção
    const float t = (proj + disc < proj - disc) ? proj - disc : proj + disc;
    *out = {a->x + dir.x * t, a->y + dir.y * t, a->z + dir.z * t, a->w};
    return true;
}

// [decomp] 0x001e4528 ATAN2_APPROX
// Arco-tangente aproximado por uma parábola em [0, 1] (erro de alguns milésimos de radiano).
float ATAN2_APPROX(float y, float x) {
    float r;
    if (x == y) {
        r = (x == 0.0f) ? 0.0f : 0.7853982f;
    } else {
        const float ax = std::fabs(x), ay = std::fabs(y);
        if (ay < ax) {
            const float q = ay / ax;
            r = (1.0596788f - q * 0.27131295f) * q;
        } else {
            const float q = ax / ay;
            r = kHalfPi - (1.0596788f - q * 0.27131295f) * q;
        }
    }
    if (x < 0.0f) r = kPi - r;
    if (y < 0.0f) r = -r;
    return r;
}

// [decomp] 0x001e3948 Vec_Spherical_2_Cartesian
// (raio, azimute a, elevação b) -> cartesiano: x = r sin(a) cos(b), y = r sin(b), z = r cos(a) cos(b).
void Vec_Spherical_2_Cartesian(_VECTOR* out, float radius, float a, float b) {
    float ca, sa, cb, sb;
    PS2Sinf2(a + kHalfPi, a, &ca, &sa);
    PS2Sinf2(b + kHalfPi, b, &cb, &sb);
    out->y = radius * sb;
    out->z = radius * ca * cb;
    out->x = radius * sa * cb;
}

// [decomp] 0x001e39e8 Vec_Cartesian_2_Spherical
// out = (elevação, azimute, raio), com o arco-tangente aproximado.
void Vec_Cartesian_2_Spherical(_VECTOR* out, float x, float y, float z) {
    const float horiz2 = x * x + z * z;
    out->x = ATAN2_APPROX(y, std::sqrt(horiz2));
    out->y = ATAN2_APPROX(x, z);
    out->z = std::sqrt(horiz2 + y * y);
}

// [decomp] 0x001e3a70 vecutil_cartesian_to_spherical_acc
// Como Vec_Cartesian_2_Spherical, mas com atan2f exato e z = 0 no lugar do raio (w não é escrito).
void vecutil_cartesian_to_spherical_acc(_VECTOR* out, float x, float y, float z) {
    out->x = std::atan2(y, std::sqrt(x * x + z * z));
    out->y = std::atan2(x, z);
    out->z = 0.0f;
}

// [decomp] 0x001e0960 Vec_LineSphereIntersection_THAT_DOESNT_WORK_FOR_TIM
// Versão de segmento usada pelos bots (BOTSTATE_isPathWithinObjectRange). Com d = b - a e
// f = a - centro, resolve |a + t d - centro| = raio e devolve a menor raiz t, desde que t <= 1 e o
// segmento não esteja todo antes da esfera. Se a está dentro da esfera, devolve o ponto de entrada
// atrás de a (t negativo), como o original. w: indefinido no original (resto de vf6); usamos a.w.
bool Vec_LineSphereIntersection_THAT_DOESNT_WORK_FOR_TIM(const _VECTOR* a, const _VECTOR* b, const _VECTOR* center,
                                                       float radius, _VECTOR* out) {
    const _VECTOR d{b->x - a->x, b->y - a->y, b->z - a->z, 0.0f};
    const _VECTOR f{a->x - center->x, a->y - center->y, a->z - center->z, 0.0f};
    const float qa = dot3(d, d);
    const float qb = dot3(f, d);
    const float disc = qb * qb - qa * (dot3(f, f) - radius * radius);
    if (!(0.0f <= disc)) return false;
    float t;
    if (0.0f < disc) {
        const float root = std::sqrt(disc);
        t = (-qb - root) * (1.0f / qa);
        if (1.0f < t) return false;
        if (t < 0.0f && (-qb + root) * (1.0f / qa) < 0.0f) return false;
    } else {
        t = -qb / qa;
    }
    *out = {a->x + d.x * t, a->y + d.y * t, a->z + d.z * t, a->w};
    return true;
}
