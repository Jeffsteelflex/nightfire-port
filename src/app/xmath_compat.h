#pragma once
// O pedaço do DirectXMath que o laço do nightfire usa (src/app/app_main.cpp: a câmera livre e as matrizes de visão e
// projeção), fora do Windows (só do port; docs/port.md, "macOS: os backends"). No Windows o app_main.cpp continua com o
// <DirectXMath.h> do SDK, como sempre; o macOS e o Linux não têm o SDK do Windows, e o DirectXMath do GitHub pediria ainda
// o sal.h. As contas são as do caminho sem intrínsecos do DirectXMath (_XM_NO_INTRINSICS_; MIT,
// github.com/microsoft/DirectXMath), na mesma ordem: XMScalarSinCos com os polinômios dele (não o sinf), a normalização
// pelo inverso do comprimento, a ordem das somas do produto de matrizes. O caminho SSE do Windows pode diferir no último
// bit em alguma conta (a normalização divide em vez de multiplicar pelo inverso); as imagens do macOS são comparadas com
// as do Direct3D 11 com tolerância (etapa 3e). tests/test_macos.cpp compara as duas no Windows.
//
// Tudo fica em nf::xm; no fim, o namespace DirectX passa a ver nf::xm (o "using namespace DirectX" do app_main.cpp vale
// como no Windows). Com NF_XMATH_COMPAT_NO_ALIAS, sem isso (o teste, que inclui também o DirectXMath de verdade).

#include <cmath>
#include <cstdint>

namespace nf::xm {

constexpr float XM_PI = 3.141592654f;
constexpr float XM_2PI = 6.283185307f;
constexpr float XM_1DIV2PI = 0.159154943f;
constexpr float XM_PIDIV2 = 1.570796327f;

struct XMVECTOR {
    float v[4];
};

struct XMMATRIX {
    XMVECTOR r[4];
};

struct XMFLOAT3 {
    float x, y, z;
};

struct XMFLOAT4X4 {
    float m[4][4];
};

inline float XMConvertToRadians(float degrees) { return degrees * (XM_PI / 180.0f); }
inline float XMConvertToDegrees(float radians) { return radians * (180.0f / XM_PI); }

inline XMVECTOR XMVectorSet(float x, float y, float z, float w) { return XMVECTOR{{x, y, z, w}}; }
inline XMVECTOR XMVectorZero() { return XMVECTOR{{0.0f, 0.0f, 0.0f, 0.0f}}; }

inline XMVECTOR operator+(const XMVECTOR& a, const XMVECTOR& b) {
    return XMVECTOR{{a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3]}};
}
inline XMVECTOR operator-(const XMVECTOR& a, const XMVECTOR& b) {
    return XMVECTOR{{a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3]}};
}
inline XMVECTOR operator-(const XMVECTOR& a) { return XMVECTOR{{-a.v[0], -a.v[1], -a.v[2], -a.v[3]}}; }
inline XMVECTOR operator*(const XMVECTOR& a, float s) {
    return XMVECTOR{{a.v[0] * s, a.v[1] * s, a.v[2] * s, a.v[3] * s}};
}
inline XMVECTOR operator*(float s, const XMVECTOR& a) { return a * s; }
inline XMVECTOR& operator+=(XMVECTOR& a, const XMVECTOR& b) { return a = a + b; }
inline XMVECTOR& operator-=(XMVECTOR& a, const XMVECTOR& b) { return a = a - b; }

// O produto escalar dos três primeiros (x * x + y * y + z * z, nessa ordem), como o XMVector3Dot sem intrínsecos.
inline float xm_dot3(const XMVECTOR& a, const XMVECTOR& b) { return a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2]; }

inline XMVECTOR XMVector3Cross(const XMVECTOR& a, const XMVECTOR& b) {
    return XMVECTOR{{(a.v[1] * b.v[2]) - (a.v[2] * b.v[1]), (a.v[2] * b.v[0]) - (a.v[0] * b.v[2]),
                     (a.v[0] * b.v[1]) - (a.v[1] * b.v[0]), 0.0f}};
}

// O comprimento, o inverso (zero fica zero) e o vetor inteiro multiplicado (o w também, como no DirectXMath).
inline XMVECTOR XMVector3Normalize(const XMVECTOR& a) {
    float length = std::sqrt(xm_dot3(a, a));
    if (length > 0.0f) length = 1.0f / length;
    return a * length;
}

inline bool XMVector3Equal(const XMVECTOR& a, const XMVECTOR& b) {
    return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2];
}

inline void XMStoreFloat3(XMFLOAT3* out, const XMVECTOR& a) {
    out->x = a.v[0];
    out->y = a.v[1];
    out->z = a.v[2];
}
inline XMVECTOR XMLoadFloat3(const XMFLOAT3* p) { return XMVECTOR{{p->x, p->y, p->z, 0.0f}}; }

inline void XMStoreFloat4x4(XMFLOAT4X4* out, const XMMATRIX& m) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) out->m[i][j] = m.r[i].v[j];
    }
}

// XMMatrixMultiply: vetor-linha, cada linha de a vezes b, as somas na ordem do DirectXMath.
inline XMMATRIX operator*(const XMMATRIX& a, const XMMATRIX& b) {
    XMMATRIX r;
    for (int i = 0; i < 4; ++i) {
        const float x = a.r[i].v[0], y = a.r[i].v[1], z = a.r[i].v[2], w = a.r[i].v[3];
        for (int j = 0; j < 4; ++j) {
            r.r[i].v[j] = (b.r[0].v[j] * x) + (b.r[1].v[j] * y) + (b.r[2].v[j] * z) + (b.r[3].v[j] * w);
        }
    }
    return r;
}

// XMScalarSinCos: o ângulo levado a [-pi, pi], depois a [-pi/2, pi/2], e os polinômios minimax de grau 11 e 10.
inline void XMScalarSinCos(float* s, float* c, float value) {
    float quotient = XM_1DIV2PI * value;
    if (value >= 0.0f) {
        quotient = float(int(quotient + 0.5f));
    } else {
        quotient = float(int(quotient - 0.5f));
    }
    float y = value - XM_2PI * quotient;
    float sign;
    if (y > XM_PIDIV2) {
        y = XM_PI - y;
        sign = -1.0f;
    } else if (y < -XM_PIDIV2) {
        y = -XM_PI - y;
        sign = -1.0f;
    } else {
        sign = +1.0f;
    }
    const float y2 = y * y;
    *s = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 +
          1.0f) *
         y;
    const float p =
        ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f;
    *c = sign * p;
}

// XMMatrixLookToRH = XMMatrixLookToLH com a direção negada: as linhas R0 (lado), R1 (cima), R2 (frente) e as
// translações -olho . R, transpostas.
inline XMMATRIX XMMatrixLookToRH(const XMVECTOR& eye, const XMVECTOR& direction, const XMVECTOR& up) {
    const XMVECTOR r2 = XMVector3Normalize(-direction);
    const XMVECTOR r0 = XMVector3Normalize(XMVector3Cross(up, r2));
    const XMVECTOR r1 = XMVector3Cross(r2, r0);
    const XMVECTOR neg_eye = -eye;
    const float d0 = xm_dot3(r0, neg_eye);
    const float d1 = xm_dot3(r1, neg_eye);
    const float d2 = xm_dot3(r2, neg_eye);
    XMMATRIX m;
    m.r[0] = XMVECTOR{{r0.v[0], r1.v[0], r2.v[0], 0.0f}};
    m.r[1] = XMVECTOR{{r0.v[1], r1.v[1], r2.v[1], 0.0f}};
    m.r[2] = XMVECTOR{{r0.v[2], r1.v[2], r2.v[2], 0.0f}};
    m.r[3] = XMVECTOR{{d0, d1, d2, 1.0f}};
    return m;
}

inline XMMATRIX XMMatrixPerspectiveFovRH(float fov_y, float aspect, float near_z, float far_z) {
    float sin_fov = 0.0f, cos_fov = 0.0f;
    XMScalarSinCos(&sin_fov, &cos_fov, 0.5f * fov_y);
    const float height = cos_fov / sin_fov;
    const float width = height / aspect;
    const float range = far_z / (near_z - far_z);
    XMMATRIX m;
    m.r[0] = XMVECTOR{{width, 0.0f, 0.0f, 0.0f}};
    m.r[1] = XMVECTOR{{0.0f, height, 0.0f, 0.0f}};
    m.r[2] = XMVECTOR{{0.0f, 0.0f, range, -1.0f}};
    m.r[3] = XMVECTOR{{0.0f, 0.0f, range * near_z, 0.0f}};
    return m;
}

}  // namespace nf::xm

#if !defined(NF_XMATH_COMPAT_NO_ALIAS)
namespace DirectX {
using namespace nf::xm;
}  // namespace DirectX
#endif
