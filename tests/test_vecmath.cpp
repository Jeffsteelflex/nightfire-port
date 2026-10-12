// Testes do port de matemática vetorial (src/engine/vecmath.cpp).
// Verificam propriedades independentes da implementação e os comportamentos do original
// que o port preserva de propósito.

#include <cmath>
#include <random>

#include "engine/vecmath.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

std::mt19937 rng(1234);
float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

quaternion_tag random_quat() {
    quaternion_tag q{uniform(-1, 1), uniform(-1, 1), uniform(-1, 1), uniform(-1, 1)};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// v * M (vetor-linha, convenção do motor), só a parte 3x3.
_VECTOR row_mul(const _VECTOR& v, const MATRIX& m) {
    return {v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
            v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2], 0};
}

MATRIX zero_matrix() {
    MATRIX m{};
    return m;
}

}  // namespace

TEST(sin_poly_close_to_std_sin) {
    float worst = 0;
    for (int i = -2000; i <= 2000; ++i) {
        const float a = i * 0.01f;
        worst = std::max(worst, std::fabs(PS2Sinf(a) - nf::fm::sin(a)));
    }
    CHECK(worst < 3e-5f);
    CHECK_NEAR(PS2Sinf(0.0f), 0.0, 1e-7);
    CHECK_NEAR(PS2Sinf(1.5707964f), 1.0, 2e-5);
}

TEST(sin2_sin3_match_sin) {
    float a, b, c;
    PS2Sinf2(0.3f, -2.0f, &a, &b);
    CHECK(a == PS2Sinf(0.3f));
    CHECK(b == PS2Sinf(-2.0f));
    PS2Sinf3(5.0f, -7.5f, 1.0f, &a, &b, &c);
    CHECK(a == PS2Sinf(5.0f) && b == PS2Sinf(-7.5f) && c == PS2Sinf(1.0f));
}

TEST(rotmatrix_matches_independent_formula) {
    // Fórmula da RotMatrix escrita com std::sin/cos (a mesma usada em tools/geometry.py, que
    // posiciona corretamente todas as fases exportadas).
    for (int i = 0; i < 200; ++i) {
        const _VECTOR e{uniform(-3.2f, 3.2f), uniform(-3.2f, 3.2f), uniform(-3.2f, 3.2f), 0};
        MATRIX m = zero_matrix();
        RotMatrix(&e, &m);
        const double cx = nf::fm::cos(e.x), sx = nf::fm::sin(e.x), cy = nf::fm::cos(e.y), sy = nf::fm::sin(e.y);
        const double cz = nf::fm::cos(e.z), sz = nf::fm::sin(e.z);
        const double ref[3][3] = {{cy * cz, sx * sy * cz + cx * sz, -cx * sy * cz + sx * sz},
                                  {-cy * sz, cx * cz - sx * sy * sz, sx * cz + cx * sy * sz},
                                  {sy, -sx * cy, cx * cy}};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) CHECK_NEAR(m.m[r][c], ref[r][c], 1e-4);
        }
    }
}

TEST(single_axis_rotations_agree) {
    for (int i = 0; i < 50; ++i) {
        const float a = uniform(-3.0f, 3.0f);
        MATRIX rx = zero_matrix(), ry = zero_matrix(), rz = zero_matrix(), full = zero_matrix();
        RotMatrixX(a, &rx);
        RotMatrixY(a, &ry);
        RotMatrixZ(a, &rz);
        const _VECTOR ex{a, 0, 0, 0}, ey{0, a, 0, 0}, ez{0, 0, a, 0};
        RotMatrix(&ex, &full);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) CHECK_NEAR(rx.m[r][c], full.m[r][c], 1e-5);
        RotMatrix(&ey, &full);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) CHECK_NEAR(ry.m[r][c], full.m[r][c], 1e-5);
        RotMatrix(&ez, &full);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) CHECK_NEAR(rz.m[r][c], full.m[r][c], 1e-5);
    }
}

TEST(euler_quat_matches_rotmatrix_on_one_axis_only) {
    // Verificado nos dados: Quat_EulerToQuat e RotMatrix concordam em um eixo e divergem em vários.
    const _VECTOR e{0.0f, 1.2f, 0.0f, 0};
    quaternion_tag q;
    MATRIX a = zero_matrix(), b = zero_matrix();
    Quat_EulerToQuat(&e, &q);
    Quat_QuatToMat(&q, &a);
    RotMatrix(&e, &b);
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) CHECK_NEAR(a.m[r][c], b.m[r][c], 1e-5);

    const _VECTOR e2{0.7f, 1.57f, 0.0f, 0};
    Quat_EulerToQuat(&e2, &q);
    Quat_QuatToMat(&q, &a);
    RotMatrix(&e2, &b);
    double diff = 0;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) diff = std::max(diff, double(std::fabs(a.m[r][c] - b.m[r][c])));
    CHECK(diff > 0.1);
}

TEST(quat_rot_vec_equals_quat_to_mat) {
    for (int i = 0; i < 200; ++i) {
        const quaternion_tag q = random_quat();
        const _VECTOR v{uniform(-5, 5), uniform(-5, 5), uniform(-5, 5), 0};
        _VECTOR out;
        MATRIX m = zero_matrix();
        Quat_Rot_Vec(&q, &v, &out);
        Quat_QuatToMat(&q, &m);
        const _VECTOR ref = row_mul(v, m);
        CHECK_NEAR(out.x, ref.x, 1e-4);
        CHECK_NEAR(out.y, ref.y, 1e-4);
        CHECK_NEAR(out.z, ref.z, 1e-4);
    }
}

TEST(mat_to_quat_inverts_quat_to_mat) {
    for (int i = 0; i < 500; ++i) {
        const quaternion_tag q = random_quat();
        MATRIX m = zero_matrix();
        quaternion_tag back;
        Quat_QuatToMat(&q, &m);
        Quat_MatToQuat(&back, &m);
        const float sign = (back.x * q.x + back.y * q.y + back.z * q.z + back.w * q.w) < 0 ? -1.0f : 1.0f;
        CHECK_NEAR(back.x * sign, q.x, 1e-4);
        CHECK_NEAR(back.y * sign, q.y, 1e-4);
        CHECK_NEAR(back.z * sign, q.z, 1e-4);
        CHECK_NEAR(back.w * sign, q.w, 1e-4);
    }
}

TEST(world_to_view_behaviour) {
    // Mat_World2ViewMat transpõe o 3x3 e calcula a translação como -(t * R), usando as colunas de R.
    // Só é a inversa exata quando R é identidade (ou simétrica): é o comportamento do original
    // (0x001e3280), preservado como em Mat_Inverse.
    for (int i = 0; i < 100; ++i) {
        const _VECTOR e{uniform(-3, 3), uniform(-3, 3), uniform(-3, 3), 0};
        const _VECTOR t{uniform(-50, 50), uniform(-50, 50), uniform(-50, 50), 0};
        MATRIX w = zero_matrix(), v = zero_matrix();
        RotTransMatrix(&e, &t, &w);
        Mat_World2ViewMat(&w, &v);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) CHECK(v.m[r][c] == w.m[c][r]);
        const _VECTOR tr = row_mul(t, w);
        CHECK_NEAR(v.m[3][0], -tr.x, 1e-3);
        CHECK_NEAR(v.m[3][1], -tr.y, 1e-3);
        CHECK_NEAR(v.m[3][2], -tr.z, 1e-3);
    }
    // Com rotação identidade é a inversa exata.
    const _VECTOR zero{0, 0, 0, 0}, t{3, -4, 5, 0};
    MATRIX w = zero_matrix(), v = zero_matrix();
    RotTransMatrix(&zero, &t, &w);
    Mat_World2ViewMat(&w, &v);
    CHECK_NEAR(v.m[3][0], -3.0, 1e-5);
    CHECK_NEAR(v.m[3][1], 4.0, 1e-5);
    CHECK_NEAR(v.m[3][2], -5.0, 1e-5);
}

TEST(mat_inverse_only_negates_translation) {
    // Comportamento do original preservado: a translação é negada sem ser girada.
    const _VECTOR e{0, 1.0f, 0, 0}, t{10, 0, 0, 0};
    MATRIX m = zero_matrix(), inv = zero_matrix();
    RotTransMatrix(&e, &t, &m);
    Mat_Inverse(&m, &inv);
    CHECK(inv.m[3][0] == -10.0f && inv.m[3][1] == 0.0f && inv.m[3][2] == 0.0f);
    CHECK(inv.m[0][2] == m.m[2][0] && inv.m[2][0] == m.m[0][2]);
    MATRIX in_place = m;
    Mat_Inverse(&in_place);
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c) CHECK(in_place.m[r][c] == inv.m[r][c]);
}

TEST(identity_variants) {
    MATRIX m;
    for (auto& row : m.m) for (float& f : row) f = 7.0f;
    Mat_Identity(&m);
    CHECK(m.m[0][0] == 1 && m.m[1][1] == 1 && m.m[2][2] == 1 && m.m[0][1] == 0);
    CHECK(m.m[3][0] == 7.0f && m.m[0][3] == 7.0f);  // só o 3x3 é escrito
    Mat_IdentityT(&m);
    CHECK(m.m[3][0] == 0 && m.m[0][3] == 0 && m.m[3][3] == 0);  // m[3][3] = 0, como no original
}

TEST(slerp_variants) {
    const quaternion_tag a{0, 0, 0, 1}, b{0, 0.70710677f, 0, 0.70710677f};
    quaternion_tag out;
    Quat_Slerp(0.3f, &a, &b, &out);
    CHECK(out.y == a.y && out.w == a.w);  // degrau, não interpolação
    Quat_Slerp(0.6f, &a, &b, &out);
    CHECK(out.y == b.y && out.w == b.w);
    Quat_Slerp_Acc(0.5f, &a, &b, &out);
    CHECK_NEAR(out.y, nf::fm::sin(3.14159265 / 8), 1e-4);
    CHECK_NEAR(out.w, nf::fm::cos(3.14159265 / 8), 1e-4);
}

TEST(vector_helpers) {
    CHECK_NEAR(Vec_AngleDifference(0.1f, 6.2f), -0.1831853, 1e-5);
    CHECK_NEAR(Vec_AngleDifference(-1.0f, 1.0f), 2.0, 1e-5);
    const _VECTOR a{1, 2, 3, 9}, b{4, -2, 3, 5};
    CHECK_NEAR(Vec_Dist3D(&a, &b), 5.0, 1e-6);
    CHECK_NEAR(Vec_SqDist2D(&a, &b), 9.0, 1e-6);  // ignora y
    _VECTOR u;
    Vec_UnitVector(&u, &a, &b);
    CHECK_NEAR(u.x, 0.6, 1e-6);
    CHECK_NEAR(u.y, -0.8, 1e-6);
    CHECK(u.w == a.w);  // resto de registrador do original, determinístico neste caso
    _VECTOR n;
    Vec_CrossNormalise(&a, &b, &n);
    CHECK_NEAR(n.x * a.x + n.y * a.y + n.z * a.z, 0.0, 1e-5);
    CHECK_NEAR(n.x * n.x + n.y * n.y + n.z * n.z, 1.0, 1e-5);
    _VECTOR lo, hi;
    Vec_MinMax(&a, &b, &lo, &hi);
    CHECK(lo.x == 1 && lo.y == -2 && hi.x == 4 && hi.y == 2);
}
