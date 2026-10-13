#pragma once
// As rotinas de matriz e quatérnio da biblioteca REAL da EA (librealmema.a, memvalid.o) que a física usa. No PS2 são
// VU0 em modo macro (vmulax/vmaddw...) ou FPU; aqui com as mesmas operações na mesma ordem (platform/vu0_macro.h,
// platform/ps2_float.h), então os bits são os do PS2. Matrizes de vetores linha (v' = v * M), 4x4 em linhas de 16
// bytes, como no EAGL.

#include <cstdint>

namespace drv {

// [driving] 0x2cc0c8 VU0_quattom4(COORD4 &, MATRIX4 &): o quatérnio (x, y, z, w) em matriz de rotação, linha 3 =
// (0, 0, 0, 1).
void VU0_quattom4(const float q[4], float m[4][4]);
// [driving] 0x2cc690 VU0_m4toquat(MATRIX4 &, COORD4 &): a matriz em quatérnio (o método do traço, com FPU).
void VU0_m4toquat(const float m[4][4], float q[4]);
// [driving] 0x2cbb28 VU0_MATRIX3x4_mult(MATRIX4 &a, MATRIX4 &b, MATRIX4 &out): out = a * b nas três primeiras linhas
// de a, com a coluna w de b zerada (as linhas de out levam o w de a só pela coluna 3... ver o .cpp).
void VU0_MATRIX3x4_mult(const float a[4][4], const float b[4][4], float out[4][4]);
// [driving] 0x2cbb88 VU0_MATRIX4_mult(MATRIX4 &a, MATRIX4 &b, MATRIX4 &out): out = a * b (4x4).
void VU0_MATRIX4_mult(const float a[4][4], const float b[4][4], float out[4][4]);
// A transposta 4x4 com pextlw/pextuw/pcpyld/pcpyud (cópia exata dos bits).
void transpose4(const float in[4][4], float out[4][4]);

// [driving] 0x2cbd50 VU0_Sin(float): o seno de um ângulo em voltas (1 = 360 graus): reduzido a -0,5..0,5 volta pela
// FPU, vezes 2 pi, e o polinômio ímpar de grau 9 na VU0 (x - x³/6 + ...; os coeficientes do código).
float VU0_Sin(float turns);
// [driving] 0x2cbeb0 VU0_Cos(float): o mesmo com um quarto de volta somado depois da redução inteira.
float VU0_Cos(float turns);
// [driving] 0x2cc510 VU0_MATRIX4setxrot, 0x2cc590 VU0_MATRIX4setyrot, 0x2cc610 VU0_MATRIX4setzrot (MATRIX4 &, float):
// a rotação em torno de um eixo (o ângulo em voltas), linha 3 = (0, 0, 0, 1).
void VU0_MATRIX4setxrot(float m[4][4], float turns);
void VU0_MATRIX4setyrot(float m[4][4], float turns);
void VU0_MATRIX4setzrot(float m[4][4], float turns);

}  // namespace drv
