// As entradas por quadro dos métodos do mundo das missões de carro (driving/mat_bond.h, driving/light_manager.h):
// propriedades conferidas à mão, sem o original (a conferência bit a bit contra o microprograma e o DRIVING.ELF fica no
// `nf_driving --check --verify` e na parte 11 do nf_driving_check): o deslocamento da rolagem dos Scroll2 (a fração, com
// o sinal do produto), o quadro do FrameAnim, a cor do Dynamic (a luz que bate de frente, a de costas, o teto de 255, o
// alfa 128), a rotação do BuildRotate, as direções do bloco de luzes e o RTimeData sem sinal.

#include <cmath>
#include <cstring>

#include "driving/light_manager.h"
#include "driving/mat_bond.h"
#include "test_framework.h"

using namespace drv;

TEST(driving_bond_scroll_and_frame_offsets) {
    // A fração de ScrollUV x t, cortada em direção ao zero: o sinal acompanha o produto.
    float o[2];
    const float uv[2] = {0.25f, -0.002f};
    bond_scroll_offset(uv, 10.0f, o);
    CHECK(o[0] == 0.5f);
    CHECK(std::fabs(o[1] + 0.02f) < 1e-6f);
    const float uv2[2] = {0.5f, 0.0f};
    bond_scroll_offset(uv2, 1001.0f, o);
    CHECK(o[0] == 0.5f && o[1] == 0.0f);
    // O FrameAnim: ((SimStep.x - SimStep.w) x velocidade) -> fração x quadros -> inteiro x tamanho do quadro.
    const float fa[4] = {0.5f, 0.25f, 4.0f, 0.0f};
    const float ss1[4] = {3.0f, 0.0f, 0.0f, 0.0f};  // 1,5: fração 0,5 x 4 = quadro 2 -> 0,5
    CHECK(bond_frame_offset(fa, ss1) == 0.5f);
    const float ss2[4] = {7.0f, 0.0f, 0.0f, 1.0f};  // 3,0: fração 0 -> quadro 0
    CHECK(bond_frame_offset(fa, ss2) == 0.0f);
    const float ss3[4] = {3.5f, 0.0f, 0.0f, 0.0f};  // 1,75: 0,75 x 4 = quadro 3 -> 0,75
    CHECK(bond_frame_offset(fa, ss3) == 0.75f);
}

TEST(driving_bond_dynamic_colour) {
    // As direções nas colunas das linhas 0-2, as cores das três luzes nas linhas 3-5, o ambiente na 6.
    float L[7][4] = {};
    L[1][0] = 1.0f;  // a luz 0 aponta para +y
    L[3][0] = 1.0f;
    L[3][1] = 0.5f;
    L[6][0] = L[6][1] = L[6][2] = 0.2f;
    const float W[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    int32_t c[4];
    const float up[3] = {0, 1, 0}, down[3] = {0, -1, 0};
    bond_dynamic_colour(L, W, up, c);
    CHECK(c[0] == 152 && c[1] == 88 && c[2] == 25 && c[3] == 128);  // (1,2 0,7 0,2) x 127, cortados
    bond_dynamic_colour(L, W, down, c);
    CHECK(c[0] == 25 && c[1] == 25 && c[2] == 25 && c[3] == 128);  // de costas: só o ambiente
    // A matriz do mundo gira a normal: com y e z trocados, a normal +z recebe a luz de +y.
    const float Wyz[4][4] = {{1, 0, 0, 0}, {0, 0, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}};
    const float fwd[3] = {0, 0, 1};
    bond_dynamic_colour(L, Wyz, fwd, c);
    CHECK(c[0] == 152 && c[1] == 88);
    L[3][0] = 3.0f;
    bond_dynamic_colour(L, W, up, c);
    CHECK(c[0] == 255);  // o teto
}

TEST(driving_light_rotation_and_directions) {
    float m[16];
    BuildRotate(m, 0.0f, 0.0f, 1.0f, 0.0f);
    for (int k = 0; k < 16; ++k) CHECK(m[k] == ((k % 5) == 0 ? 1.0f : 0.0f));
    // 90 graus em volta de x levam (0, 0, 1) para o eixo y; o comprimento fica.
    BuildRotate(m, 90.0f, 2.0f, 0.0f, 0.0f);  // o eixo não precisa ser unitário
    const float z[4] = {0, 0, 1, 1};
    float p[4];
    TransformPoint(m, z, p);
    CHECK(std::fabs(p[0]) < 1e-5f && std::fabs(std::fabs(p[1]) - 1.0f) < 1e-5f && std::fabs(p[2]) < 1e-5f);
    // O bloco de luzes: ângulos zero dão (0, 0, 1) nas três colunas; X = 90 na luz 1 a põe no eixo y.
    LightBlock b{};
    const float zero[6] = {};
    RLightManager_SetLightDirections(b, zero);
    for (int k = 0; k < 3; ++k) CHECK(b.dir[0][k] == 0.0f && b.dir[1][k] == 0.0f && b.dir[2][k] == 1.0f);
    const float a[6] = {0.0f, 90.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    RLightManager_SetLightDirections(b, a);
    CHECK(std::fabs(std::fabs(b.dir[1][1]) - 1.0f) < 1e-5f && std::fabs(b.dir[2][1]) < 1e-5f);
    CHECK(b.dir[2][0] == 1.0f && b.dir[2][2] == 1.0f);
}

TEST(driving_rtime_data_unsigned) {
    const RTimeData saved = rtime_data();
    RTimeData_Update(10);
    CHECK(rtime_data().time_data[0] == 10.0f && rtime_data().time == 10u);
    CHECK(std::fabs(rtime_data().caustic_time - 0.14f) < 1e-6f);
    // O passo é lido sem sinal: -1 é 2^32 - 1.
    RTimeData_Update(-1);
    CHECK(rtime_data().time == 0xffffffffu && rtime_data().time_data[0] > 4.0e9f);
    // Os outros componentes ficam os do .data.
    CHECK(rtime_data().time_data[1] == -0.002f);
    rtime_data() = saved;
}
