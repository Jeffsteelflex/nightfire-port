// Testes das funções de alinhamento, Euler, rastreamento e conversões (segunda parte de vecmath).

#include <cmath>
#include <random>

#include "engine/vecmath.h"
#include "test_framework.h"

namespace {

std::mt19937 rng2(4321);
float uni(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng2); }

float dot_row(const MATRIX& m, int a, int b) {
    return m.m[a][0] * m.m[b][0] + m.m[a][1] * m.m[b][1] + m.m[a][2] * m.m[b][2];
}

void check_orthonormal(const MATRIX& m, double tol) {
    for (int a = 0; a < 3; ++a) {
        CHECK_NEAR(dot_row(m, a, a), 1.0, tol);
        for (int b = a + 1; b < 3; ++b) CHECK_NEAR(dot_row(m, a, b), 0.0, tol);
    }
}

MATRIX transpose3(const MATRIX& m) {
    MATRIX t{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) t.m[r][c] = m.m[c][r];
    return t;
}

}  // namespace

TEST(lookat_points_row2_at_target) {
    for (int i = 0; i < 100; ++i) {
        const _VECTOR eye{uni(-20, 20), uni(-20, 20), uni(-20, 20), 3.0f};
        const _VECTOR target{uni(-20, 20), uni(-20, 20), uni(-20, 20), 0};
        MATRIX m{};
        Mat_LookAt(&m, &eye, &target, &CONSTANT_UP1, &CONSTANT_UP2);
        check_orthonormal(m, 1e-4);
        const float dx = target.x - eye.x, dy = target.y - eye.y, dz = target.z - eye.z;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        CHECK_NEAR(m.m[2][0], dx / len, 1e-4);
        CHECK_NEAR(m.m[2][1], dy / len, 1e-4);
        CHECK_NEAR(m.m[2][2], dz / len, 1e-4);
        CHECK(m.m[3][0] == eye.x && m.m[3][1] == eye.y && m.m[3][2] == eye.z && m.m[3][3] == eye.w);
        CHECK(m.m[1][1] >= -1e-4f);  // o eixo de cima nunca aponta para baixo com up = +Y
    }
}

TEST(lookat_straight_up_uses_alternate_axis) {
    const _VECTOR eye{0, 0, 0, 0}, target{0, 10, 0, 0};
    MATRIX m{};
    Mat_LookAt(&m, &eye, &target, &CONSTANT_UP1, &CONSTANT_UP2);
    check_orthonormal(m, 1e-5);
    // dir = +Y é paralelo a UP1; a lateral sai de UP2 x dir = (1,0,0) x (0,1,0) = (0,0,1).
    CHECK_NEAR(m.m[0][2], 1.0, 1e-6);
}

TEST(align2dir_and_align2up) {
    const _VECTOR dir{3, 0, 4, 0};
    MATRIX m{};
    m.m[3][0] = 7.0f;
    Mat_Align2Dir(&m, &dir, &CONSTANT_UP1, &CONSTANT_UP2);
    check_orthonormal(m, 1e-5);
    CHECK_NEAR(m.m[2][0], 0.6, 1e-6);
    CHECK_NEAR(m.m[2][2], 0.8, 1e-6);
    CHECK(m.m[3][0] == 7.0f);  // translação intocada
    const _VECTOR up{0, 2, 0, 0};
    MATRIX n{};
    Mat_Align2Up(&n, &up, &dir);
    CHECK(n.m[1][1] == 2.0f);  // a linha de cima recebe up sem normalizar
    CHECK_NEAR(dot_row(n, 0, 2), 0.0, 1e-6);
}

TEST(normalize_repairs_and_keeps_rotations) {
    for (int i = 0; i < 100; ++i) {
        const _VECTOR e{uni(-3, 3), uni(-3, 3), uni(-3, 3), 0};
        MATRIX m{};
        RotMatrix(&e, &m);
        MATRIX n = m;
        Mat_Normalize(&n);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) CHECK_NEAR(n.m[r][c], m.m[r][c], 1e-4);
        // Bagunça a matriz e confere que volta a ser ortonormal.
        n.m[1][0] += 0.2f;
        n.m[2][2] *= 1.3f;
        Mat_Normalize(&n);
        check_orthonormal(n, 1e-4);
    }
}

TEST(mat_to_euler_inverts_rot_matrices) {
    // Relações verificadas: com RotMatrix a ordem 5 devolve -e e a transposta com ordem 0 devolve e;
    // com RotMatrixZYX é o contrário (ordem 0 devolve -e, transposta com ordem 5 devolve e).
    for (int i = 0; i < 200; ++i) {
        const _VECTOR e{uni(-1, 1), uni(-1, 1), uni(-1, 1), 0};
        MATRIX m{}, z{};
        _VECTOR out;
        RotMatrix(&e, &m);
        Vec_MatToEuler(&out, &m, 5);
        CHECK_NEAR(out.x, -e.x, 1e-4);
        CHECK_NEAR(out.y, -e.y, 1e-4);
        CHECK_NEAR(out.z, -e.z, 1e-4);
        const MATRIX mt = transpose3(m);
        Vec_MatToEuler(&out, &mt, 0);
        CHECK_NEAR(out.x, e.x, 1e-4);
        CHECK_NEAR(out.y, e.y, 1e-4);
        CHECK_NEAR(out.z, e.z, 1e-4);
        RotMatrixZYX(&e, &z);
        Vec_MatToEuler(&out, &z, 0);
        CHECK_NEAR(out.x, -e.x, 1e-4);
        CHECK_NEAR(out.z, -e.z, 1e-4);
    }
}

TEST(matrix_to_quaternion_old_convention) {
    // Código antigo, escalar na posição 0 e negativo no caminho do traço. Medido nos 4 ramos (traço e
    // cada eixo dominante): Quat_QuaternionToMatrix(Quat_MatrixToQuaternion(m)) devolve a própria m.
    for (int i = 0; i < 300; ++i) {
        const _VECTOR e{uni(-3, 3), uni(-3, 3), uni(-3, 3), 0};
        MATRIX m{}, back{};
        RotMatrix(&e, &m);
        quaternion_tag q;
        Quat_MatrixToQuaternion(&q, &m);
        Quat_QuaternionToMatrix(&back, &q);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) CHECK_NEAR(back.m[r][c], m.m[r][c], 2e-4);
    }
}

TEST(track_moves_toward_target) {
    MATRIX m{};
    Mat_IdentityT(&m);
    _VECTOR from{0, 0, 0, 0}, to{10, 0, 0, 0};
    quaternion_tag q;
    // t = 1: gira por completo para olhar na direção de "from - to" (o original usa a - b).
    Vec_Track(1.0f, &from, &to, &m, &q);
    check_orthonormal(m, 1e-4);
    CHECK_NEAR(m.m[2][0], -1.0, 1e-4);
    // t = 0: não muda.
    MATRIX same{};
    Mat_IdentityT(&same);
    Vec_Track(0.0f, &from, &to, &same, &q);
    CHECK_NEAR(same.m[2][2], 1.0, 1e-4);
}

TEST(line_sphere_intersection) {
    const _VECTOR a{-10, 0, 0, 5}, b{10, 0, 0, 0}, c{0, 0, 0, 0};
    _VECTOR out;
    CHECK(Vec_LineSphereIntersection(&a, &b, &c, 2.0f, &out));
    CHECK_NEAR(out.x, 2.0, 1e-5);  // ponto mais distante de a
    CHECK(out.w == a.w);
    const _VECTOR far{0, 5, 0, 0};
    CHECK(!Vec_LineSphereIntersection(&a, &b, &far, 2.0f, &out));
}

TEST(atan2_approx_and_spherical) {
    float worst = 0;
    for (int i = 0; i < 2000; ++i) {
        const float y = uni(-10, 10), x = uni(-10, 10);
        worst = std::max(worst, std::fabs(ATAN2_APPROX(y, x) - std::atan2(y, x)));
    }
    CHECK(worst < 0.006f);  // aproximação parabólica: erro máximo de cerca de 0,005 rad
    CHECK(ATAN2_APPROX(0, 0) == 0.0f);
    _VECTOR c, s;
    Vec_Spherical_2_Cartesian(&c, 5.0f, 0.7f, 0.3f);
    CHECK_NEAR(std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z), 5.0, 1e-4);
    Vec_Cartesian_2_Spherical(&s, c.x, c.y, c.z);
    CHECK_NEAR(s.z, 5.0, 1e-4);
    CHECK_NEAR(s.y, 0.7, 0.01);  // azimute
    CHECK_NEAR(s.x, 0.3, 0.01);  // elevação
}

TEST(segment_sphere_intersection_for_bots) {
    const _VECTOR c{0, 0, 0, 0};
    _VECTOR out;
    const _VECTOR a{-10, 0, 0, 1}, b{10, 0, 0, 0};
    CHECK(Vec_LineSphereIntersection_THAT_DOESNT_WORK_FOR_TIM(&a, &b, &c, 2.0f, &out));
    CHECK_NEAR(out.x, -2.0, 1e-5);  // ponto de entrada, o mais próximo de a
    const _VECTOR short_b{-5, 0, 0, 0};
    CHECK(!Vec_LineSphereIntersection_THAT_DOESNT_WORK_FOR_TIM(&a, &short_b, &c, 2.0f, &out));  // não alcança
    const _VECTOR inside{1, 0, 0, 0}, ahead{10, 0, 0, 0};
    CHECK(Vec_LineSphereIntersection_THAT_DOESNT_WORK_FOR_TIM(&inside, &ahead, &c, 2.0f, &out));
    CHECK_NEAR(out.x, -2.0, 1e-5);  // a dentro da esfera: entrada atrás de a (t < 0), como no original
}
