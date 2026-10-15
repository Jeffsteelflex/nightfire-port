#pragma once
// As contas da câmera do jogador que os modos novos usam (docs/driving.md seção 23), portadas do MIPS do DRIVING.ELF:
//  - os ângulos em voltas (1 = 360 graus) das funções sem nome de RPlayerCamera.obj (0x1b23e8..0x1b25c0) e de
//    RCameraMath.obj (0x1a7098, 0x1a70d8, 0x1a71b8): seno e cosseno pela VU0 (VU0_Sin/VU0_Cos), a tangente, o arco
//    tangente, o arco seno e o arco cosseno pela libm do jogo (tanf, atan2f, asinf, acosf: aqui nf::fm, como no resto do
//    port do carro; o nf_driving_check as põe no lugar das originais nos dois lados);
//  - RCameraMath: BuildRotationQuat, BuildRotationMat4, VU0_GenerateMatrix4 e VU0_GenerateMatrix4Unit,
//    FindAngleBetweenVectors;
//  - OrthoInverse (0x2d19b0, a inversa de uma matriz ortonormal, no lugar) e o produto 4x4 pela FPU (0x25b220);
//  - RCameraSpline (0x68 bytes no PS2): a curva de Bézier ou Catmull-Rom das transições das câmeras animadas, com a lista
//    de pontos (no PS2 uma std::list de COORD4; aqui um vetor com as mesmas regras de iteração).
// As matrizes são de vetores linha (v' = v * M), em linhas de 16 bytes, como no resto do jogo.

#include <array>
#include <cstdint>
#include <vector>

namespace drv {

// [driving] 0x1b2440 (sem nome): o seno de um ângulo em voltas (negativo: mais uma volta; VU0_Sin).
float cam_sin_turns(float turns);
// [driving] 0x1b24c0 (sem nome): a tangente (negativo: mais uma volta; tanf(x * 2 pi)).
float cam_tan_turns(float turns);
// [driving] 0x1b2510 (sem nome): atan2f(y, x) em voltas, 0..1.
float cam_atan2_turns(float y, float x);
// [driving] 0x1b2560 (sem nome) e 0x1a71b8 (o mesmo código, de RCameraMath): acosf do valor preso a -1..1, em voltas.
float cam_acos_turns(float c);
// [driving] 0x1b25c0 (sem nome): asinf do valor preso a -1..1, em voltas.
float cam_asin_turns(float s);
// [driving] 0x1b23e8 (sem nome): a volta reduzida: >= 1 menos 1, <= -1 mais 1, negativo mais 1.
float cam_wrap_turns(float turns);
// [driving] 0x1a7098 e 0x1a70d8 (RCameraMath, sem nome no SYM): VU0_Sin e VU0_Cos de um ângulo negativo mais uma volta.
float cam_math_sin(float turns);
float cam_math_cos(float turns);

// [driving] 0x1a72b8 RCameraMath::BuildRotationQuat(COORD4 &out, COORD4 &eixo, float voltas): o quatérnio (x, y, z, w)
// da rotação de `turns` voltas em torno do eixo, normalizado pela VU0.
void RCameraMath_BuildRotationQuat(float q[4], const float axis[4], float turns);
// [driving] 0x1a7360 RCameraMath::BuildRotationMat4(float voltas, MATRIX4 &out, COORD4 &eixo)
void RCameraMath_BuildRotationMat4(float turns, float m[4][4], const float axis[4]);
// [driving] 0x1a7558 RCameraMath::VU0_GenerateMatrix4(COORD4 &frente, MATRIX4 &out): a frente normalizada vai para a
// linha 2 (o w da linha fica), w = 0, e VU0_GenerateMatrix4Unit.
void RCameraMath_VU0_GenerateMatrix4(const float dir[4], float m[4][4]);
// [driving] 0x1a7608 RCameraMath::VU0_GenerateMatrix4Unit(COORD4 &frente, MATRIX4 &out): direita = (0, 1, 0) x frente
// normalizada, cima = linha 2 x direita, linha 3 = (0, 0, 0, 1), os w zerados.
void RCameraMath_VU0_GenerateMatrix4Unit(const float dir[4], float m[4][4]);
// [driving] 0x1a77b0 RCameraMath::FindAngleBetweenVectors(COORD4 &, COORD4 &): o ângulo em voltas (0..1: o sinal pelo y
// do produto vetorial).
float RCameraMath_FindAngleBetweenVectors(const float a[4], const float b[4]);
// [driving] 0x2d19b0 OrthoInverse(MATRIX4 &): a inversa de uma matriz de rotação e translação, no lugar.
void OrthoInverse(float m[4][4]);
// [driving] 0x25b220 (sem nome): out = a * b, 4x4 pela FPU (((a0 b0 + a1 b1) + a2 b2) + a3 b3), por uma cópia se out é
// a ou b.
void MatrixMultiplyFpu(const float a[4][4], const float b[4][4], float out[4][4]);

// RCameraSpline (+0x350 do RPlayerCamera; 0x68 bytes no PS2). O nome do port é PlayerCameraSpline: o CameraSpline da
// rede de estradas (driving/ai/road_network.h) é a mesma classe do PS2 vista só no que o navegador usa.
struct RCameraSplineData {
    float points[4][4];  // +0x00 os quatro pontos de controle (os w zerados por BuildSpline)
    float quat0[4];      // +0x40 a orientação no começo
    float quat1[4];      // +0x50 a do fim
    int32_t type;        // +0x60 2 no construtor (Bézier), 0 Bézier, 1 Catmull-Rom (o resultado vezes 0,5)
    uint32_t list;       // +0x64 (p32 std::list<COORD4>: no port, PlayerCameraSpline::points)
};
static_assert(sizeof(RCameraSplineData) == 0x68, "RCameraSpline");

class PlayerCameraSpline {
public:
    // [driving] 0x1a7ba0 RCameraSpline::RCameraSpline: as 14 primeiras palavras da identidade, os quatérnios (0, 0, 0,
    // 1), o tipo 2 e a lista vazia (o w do quarto ponto fica com o que a memória tinha: zero aqui).
    PlayerCameraSpline();
    RCameraSplineData d{};
    std::vector<std::array<float, 4>> points;

    // [driving] 0x1a7cf8 AddToSplinePtList(COORD4 &), 0x1a7cd8 ClearSplinePtList, 0x1a7d90 GetPointListSize
    void AddToSplinePtList(const float p[4]);
    void ClearSplinePtList() { points.clear(); }
    int32_t GetPointListSize() const { return int32_t(points.size()); }
    // [driving] 0x1a7dd0 GetBasisMatrix (a do ponto e a da derivada estão no RCameraSpline da rede de estradas:
    // driving/ai/road_network.h, RCameraSpline_EvaluateSpline/EvaluateTangent)
    const float (*GetBasisMatrix() const)[4];
    // [driving] 0x1a7ea0 FindPointInList (o índice do primeiro ponto com x, y e z iguais; o tamanho se nenhum)
    size_t FindPointInList(const float p[4]) const;
    // [driving] 0x1a7f58 BuildSpline(float tensão, COORD4 &p0, COORD4 &dir0, COORD4 &p1, COORD4 &dir1, COORD4 *q0,
    // COORD4 *q1): normaliza dir0 e dir1 no lugar (se não nulos).
    void BuildSpline(float tension, const float p0[4], float dir0[4], const float p1[4], float dir1[4], const float* q0,
                     const float* q1);
    // [driving] 0x1a8440 EvaluateSpline(float t, MATRIX4 *out, bool): só o modo 1 (o quatérnio pelo slerp rápido da
    // VU0); o outro (a frente interpolada, VU0_ExtractZAxis4FromQuat) não é usado pelos modos portados: conta.
    void EvaluateSpline(float t, float out[4][4], int32_t mode) const;
};

uint64_t camera_math_pending();

}  // namespace drv
