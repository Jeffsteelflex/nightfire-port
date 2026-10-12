// Testes da física das missões de carro (src/driving/physics), sem dados do jogo. Conferem propriedades independentes
// do porte: as contas da integração (queda livre com o passo de Euler simplético do microcódigo), o amortecimento, o
// sono, a identidade do impulso de uma alavanca (a velocidade normal do ponto depois do impulso é -e vn, e o momento
// angular muda de r x J), o quatérnio normalizado, a rotação livre, os limites. Os valores esperados vêm de contas à
// mão em double; a comparação bit a bit com o PS2 está no nf_driving_check.

#include <cmath>
#include <cstring>

#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/vu0_physics.h"
#include "driving/physics/world.h"
#include "platform/ps2_float.h"
#include "test_framework.h"

namespace {

void fresh() {
    drv::reset_rigid_globals();
    drv::physics_pending_reset();
    drv::set_vu0_mode(drv::Vu0Mode::Native);
    drv::simulation_reset(60);
    drv::vu0_reset();
    drv::set_world(nullptr);
}

drv::RigidBody* add_box(float mass, float y, float hx, float hy, float hz, int moving = 1) {
    drv::RigidBodyParams p;
    p.mass = mass;
    p.type = 4;
    p.pos[1] = y;
    p.half_size[0] = hx;
    p.half_size[1] = hy;
    p.half_size[2] = hz;
    const float hx2 = hx * hx, hy2 = hy * hy, hz2 = hz * hz;
    p.inv_inertia[0] = 3.0f / (mass * (hy2 + hz2));
    p.inv_inertia[1] = 3.0f / (mass * (hx2 + hz2));
    p.inv_inertia[2] = 3.0f / (mass * (hx2 + hy2));
    p.moving = moving;
    return drv::simulation_add_body(drv::simulation(), p);
}

double qlen(const float q[4]) {
    return std::sqrt(double(q[0]) * q[0] + double(q[1]) * q[1] + double(q[2]) * q[2] + double(q[3]) * q[3]);
}

}  // namespace

TEST(driving_physics_timestep_is_one_sixtieth_truncated) {
    fresh();
    // 1 / 60 com arredondamento para zero (o div.S do EE), não o float mais próximo.
    CHECK(ps2::fbits(drv::simulation().dt) == 0x3c888888u);
}

TEST(driving_physics_free_fall_follows_symplectic_euler) {
    fresh();
    drv::RigidBody* rb = add_box(100.0f, 50.0f, 0.5f, 0.5f, 0.5f);
    const double g = -34.0, dt = 1.0 / 60.0;
    // O primeiro quadro não tem força (o construtor zera; a gravidade entra no fim de cada quadro, para o seguinte).
    // No quadro k (1, 2, ...): v_k = g dt (k - 1), y_k = y0 + dt (v_1 + ... + v_k) = y0 + g dt² (k - 1) k / 2.
    const int frames = 40;
    for (int k = 1; k <= frames; ++k) drv::SimulateGame(drv::simulation());
    const double v_expected = g * dt * (frames - 1);
    const double y_expected = 50.0 + g * dt * dt * (frames - 1) * frames / 2.0;
    CHECK_NEAR(rb->vel[1], v_expected, std::fabs(v_expected) * 1e-4);
    CHECK_NEAR(rb->pos[1], y_expected, 1e-3);
    CHECK(rb->vel[0] == 0 && rb->vel[2] == 0);
    CHECK_NEAR(rb->momentum[1], v_expected * 100.0, std::fabs(v_expected * 100.0) * 1e-4);
    // Sem rotação.
    CHECK(rb->ang_vel[0] == 0 && rb->ang_vel[1] == 0 && rb->ang_vel[2] == 0);
}

TEST(driving_physics_momentum_is_limited_to_m_limit_times_mass) {
    fresh();
    drv::RigidBody* rb = add_box(10.0f, 50.0f, 0.5f, 0.5f, 0.5f);
    rb->momentum[0] = 5000.0f;   // 500 m/s: acima do limite de 100 x massa
    rb->momentum[2] = -5000.0f;
    drv::UpdatePositionAndOrientation(rb, 1.0f);
    drv::Timestep_CollectVU0data();
    // O momento é limitado antes de somar a força (zero aqui): exatamente ±1000.
    CHECK(rb->momentum[0] == 1000.0f);
    CHECK(rb->momentum[2] == -1000.0f);
    CHECK_NEAR(rb->vel[0], 100.0, 1e-3);
}

TEST(driving_physics_angular_damping_only_in_the_air) {
    fresh();
    drv::RigidBody* rb = add_box(10.0f, 5.0f, 0.5f, 0.5f, 0.5f);
    rb->ang_momentum[0] = 3.0f;
    rb->ang_momentum[1] = -2.0f;
    rb->ang_momentum[2] = 1.0f;
    drv::ApplyAngularDamping(rb);
    // NATURAL_ANGULAR_DAMPING = 0,995 (o produto truncado fica a no máximo 1 ulp abaixo do exato).
    CHECK_NEAR(rb->ang_momentum[0], 3.0 * 0.995, 1e-6);
    CHECK_NEAR(rb->ang_momentum[1], -2.0 * 0.995, 1e-6);
    // Com alavancas no chão, nada.
    rb->ground_levers = 2;
    const float before = rb->ang_momentum[0];
    drv::ApplyAngularDamping(rb);
    CHECK(rb->ang_momentum[0] == before);
    // Veículos (tipo < 4) também não.
    rb->ground_levers = 0;
    rb->type = 2;
    drv::ApplyAngularDamping(rb);
    CHECK(rb->ang_momentum[0] == before);
}

TEST(driving_physics_control_sleep_rules) {
    fresh();
    drv::RigidBody* rb = add_box(10.0f, 5.0f, 0.5f, 0.5f, 0.5f);
    // Devagar (|v|1 = 4,5 < SLEEP_VEL 5) e com três alavancas no chão: dorme, e tudo zera.
    rb->vel[0] = 1.5f;
    rb->vel[1] = -1.5f;
    rb->vel[2] = 1.5f;
    rb->ang_vel[1] = 0.5f;
    rb->momentum[0] = 15.0f;
    rb->ground_levers = 3;
    drv::ControlSleep(rb);
    CHECK(rb->state == 1);
    CHECK(rb->vel[0] == 0 && rb->momentum[0] == 0 && rb->ang_vel[1] == 0);
    // Com duas alavancas, não.
    rb->state = 2;
    rb->vel[0] = 1.0f;
    rb->ground_levers = 2;
    drv::ControlSleep(rb);
    CHECK(rb->state == 2);
    // Rápido demais (|v|1 = 5,5), não.
    rb->ground_levers = 4;
    rb->vel[0] = 5.5f;
    drv::ControlSleep(rb);
    CHECK(rb->state == 2);
    // Um veículo de pé (o y do "para cima" acima de 0,7) nunca dorme.
    rb->type = 2;
    rb->vel[0] = 0.0f;
    rb->info->orient[1][1] = 1.0f;
    drv::ControlSleep(rb);
    CHECK(rb->state == 2);
}

TEST(driving_physics_lever_impulse_reverses_normal_velocity_by_restitution) {
    // Uma alavanca, sem atrito: o impulso -(1 + e) vn / (1/m + n . ((I⁻¹ (r x n)) x r)) deixa a velocidade normal do
    // ponto em -e vn, com e = min(|vn|, MICRO_THRESHOLD) x GROUND_RESTITUTION / MICRO_THRESHOLD. O momento angular
    // muda de r x J.
    fresh();
    drv::RigidBody* rb = add_box(50.0f, 5.0f, 1.0f, 0.4f, 2.0f);
    rb->vel[0] = 0.7f;
    rb->vel[1] = -3.0f;
    rb->vel[2] = 0.2f;
    rb->momentum[0] = 35.0f;
    rb->momentum[1] = -150.0f;
    rb->momentum[2] = 10.0f;
    // ω coerente com L (ω = I⁻¹ L no mundo; a orientação é a identidade).
    rb->ang_momentum[0] = 4.0f;
    rb->ang_momentum[2] = -2.0f;
    rb->ang_vel[0] = 4.0f * rb->info->inv_world_tensor[0][0];
    rb->ang_vel[2] = -2.0f * rb->info->inv_world_tensor[2][2];
    const float r[4] = {0.8f, -0.4f, 1.5f, 0};
    const float n[4] = {0.0f, 1.0f, 0.0f, 0};
    drv::LeverParams params{0, 0.0f, 0.0f, 1};
    const double L0[3] = {rb->ang_momentum[0], rb->ang_momentum[1], rb->ang_momentum[2]};
    const double P0[3] = {rb->momentum[0], rb->momentum[1], rb->momentum[2]};
    // A velocidade normal do ponto antes, em double: v + ω x r.
    auto point_vel_y = [&]() {
        const double wx = rb->ang_vel[0], wz = rb->ang_vel[2];
        return double(rb->vel[1]) + (wz * r[0] - wx * r[2]);
    };
    const double vn = point_vel_y();
    drv::ResolveLeverForces(rb, &params, &r, &n, rb->info->inv_world_tensor);
    drv::Resolve_CollectVU0data();
    const double e = std::fmin(std::fabs(vn), 10.0) * (0.5 / 10.0);
    CHECK_NEAR(point_vel_y(), -e * vn, 1e-3);
    // ΔP = J (só em y, sem atrito) e ΔL = r x J.
    const double J = rb->momentum[1] - P0[1];
    CHECK(J > 0);
    CHECK_NEAR(rb->momentum[0], P0[0], 1e-4);
    CHECK_NEAR(rb->momentum[2], P0[2], 1e-4);
    // r x (0, J, 0) = (-r.z J, 0, r.x J)
    CHECK_NEAR(rb->ang_momentum[0] - L0[0], -r[2] * J, std::fabs(J) * 1e-5 + 1e-4);
    CHECK_NEAR(rb->ang_momentum[1] - L0[1], 0.0, 1e-4);
    CHECK_NEAR(rb->ang_momentum[2] - L0[2], r[0] * J, std::fabs(J) * 1e-5 + 1e-4);
    // v = P / m.
    CHECK_NEAR(rb->vel[1], rb->momentum[1] / 50.0, 1e-4);
}

TEST(driving_physics_lever_impulse_never_pulls_down) {
    // Uma normal para baixo daria um impulso com y negativo: o microcódigo o descarta (o FMAND do sinal de y).
    fresh();
    drv::RigidBody* rb = add_box(50.0f, 5.0f, 1.0f, 0.4f, 2.0f);
    rb->vel[1] = 3.0f;
    rb->momentum[1] = 150.0f;
    const float r[4] = {0.0f, 0.4f, 0.0f, 0};
    const float n[4] = {0.0f, -1.0f, 0.0f, 0};
    drv::LeverParams params{0, 0.0f, 0.0f, 1};
    drv::ResolveLeverForces(rb, &params, &r, &n, rb->info->inv_world_tensor);
    drv::Resolve_CollectVU0data();
    CHECK(rb->momentum[1] == 150.0f);
    CHECK(rb->vel[1] == 3.0f);
}

TEST(driving_physics_free_rotation_keeps_unit_quaternion_and_angle) {
    // Um cubo (inércia isotrópica) girando em torno de y sem torque: ω constante, o ângulo avança ω dt por quadro e
    // o quatérnio continua unitário (o microcódigo normaliza com RSQRT).
    fresh();
    drv::RigidBody* rb = add_box(10.0f, 50.0f, 0.5f, 0.5f, 0.5f);
    const float I = 10.0f / 3.0f * (0.25f + 0.25f);  // m / 3 (b² + c²)
    rb->ang_momentum[1] = 2.0f * I;                    // ω = 2 rad/s
    rb->ang_vel[1] = 2.0f;
    const int frames = 30;
    for (int k = 0; k < frames; ++k) {
        drv::UpdatePositionAndOrientation(rb, 1.0f);
        drv::Timestep_CollectVU0data();
        CHECK_NEAR(qlen(rb->quat), 1.0, 2e-6);
    }
    CHECK_NEAR(rb->ang_vel[1], 2.0, 1e-4);
    // O ângulo do quatérnio (x, y, z, w): 2 atan2(|xyz|, w).
    const double s = std::sqrt(double(rb->quat[0]) * rb->quat[0] + double(rb->quat[1]) * rb->quat[1] +
                               double(rb->quat[2]) * rb->quat[2]);
    const double angle = 2.0 * std::atan2(s, double(rb->quat[3]));
    CHECK_NEAR(angle, 2.0 * frames / 60.0, 2e-3);
    // Sem translação.
    CHECK(rb->pos[0] == 0.0f && rb->pos[2] == 0.0f);
}

TEST(driving_physics_angular_velocity_is_limited_to_am_limit) {
    fresh();
    drv::RigidBody* rb = add_box(10.0f, 50.0f, 0.5f, 0.5f, 0.5f);
    rb->ang_momentum[2] = 1000.0f;  // ω = L / I = 600 rad/s, muito acima de AM_LIMIT (10)
    drv::UpdatePositionAndOrientation(rb, 1.0f);
    drv::Timestep_CollectVU0data();
    CHECK(rb->ang_vel[2] == 10.0f);
    // E L é refeito a partir do ω limitado: I ω = 10/3 x 0,5 x 10.
    CHECK_NEAR(rb->ang_momentum[2], 10.0 / 3.0 * 0.5 * 10.0, 1e-3);
}

TEST(driving_physics_box_comes_to_rest_on_flat_ground) {
    // Uma caixa largada de 2 m num chão plano em y = 0 para com o centro na meia-altura (com a normal limitada a
    // 0,9999 e a correção de penetração, a poucos milímetros) e dorme.
    fresh();
    drv::FlatGround ground(0.0f);
    drv::set_world(&ground);
    drv::RigidBody* rb = add_box(100.0f, 2.0f, 0.5f, 0.5f, 0.5f);
    for (int k = 0; k < 600; ++k) drv::ESimFrameUpdate(drv::simulation());
    rb = drv::GetRigidBody(drv::simulation(), 0);
    CHECK_NEAR(rb->pos[1], 0.5, 0.02);
    CHECK(rb->state == 1);
    CHECK(rb->ground_levers >= 3);
    CHECK(ground.queries > 0);
    drv::set_world(nullptr);
}

TEST(driving_physics_generate_impulse_bounces_off_a_wall) {
    // GenerateImpulse contra o mundo: um corpo indo para -x bate numa parede de normal +x no seu centro; o impulso é
    // positivo em x e a velocidade x fica menor em módulo (o fator de restituição dos prédios é pequeno).
    fresh();
    drv::RigidBody* rb = add_box(100.0f, 5.0f, 0.5f, 0.5f, 0.5f);
    rb->vel[0] = -6.0f;
    rb->momentum[0] = -600.0f;
    const float n[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const float point[4] = {rb->pos[0] - 0.5f, rb->pos[1], rb->pos[2], 1.0f};
    drv::CollisionInfo ci{};
    drv::GenerateImpulse(rb, &ci, n, point, 0.0f, true, 7, 1.0f);
    CHECK(rb->momentum[0] > -600.0f);
    CHECK(rb->vel[0] > -6.0f);
    CHECK(ci.speed < 0.0f);
    CHECK(ci.type == 6 && ci.tag == 7 && ci.kind == 1);
    CHECK(ci.strength >= 0.0f && ci.strength <= 1.0f);
    CHECK(rb->info->flags[1] == 1);
    // Saindo da parede: só informa.
    const float before = rb->momentum[0];
    rb->vel[0] = 2.0f;
    drv::GenerateImpulse(rb, &ci, n, point, 0.0f, true, 7, 1.0f);
    CHECK(rb->momentum[0] == before);
    CHECK(ci.speed > 0.0f && ci.strength == 0.0f);
}

namespace {
// Dois corpos para os choques: A em (0, 5, 0) e B à frente em z, a mesma orientação (identidade).
struct Pair {
    drv::RigidBody* a;
    drv::RigidBody* b;
};
Pair add_pair(float ma, float mb, float bz, float va, float vb) {
    drv::RigidBody* a = add_box(ma, 5.0f, 0.8f, 0.5f, 2.0f);
    drv::RigidBody* b = add_box(mb, 5.0f, 0.8f, 0.5f, 2.0f);
    b->pos[2] = bz;
    a->vel[2] = va;
    a->momentum[2] = va * ma;
    b->vel[2] = vb;
    b->momentum[2] = vb * mb;
    return {a, b};
}
}  // namespace

TEST(driving_physics_resolve_collision_conserves_momentum_between_equal_objects) {
    // Dois objetos comuns de massas iguais indo um contra o outro, a normal de A para B passando pelos centros: os
    // impulsos são iguais e opostos (o momento total se conserva), A perde velocidade em z, B ganha, e o braço é
    // paralelo ao impulso (r x J = 0: nada gira).
    fresh();
    Pair p = add_pair(100.0f, 100.0f, 2.0f, 3.0f, -3.0f);
    const double total0 = double(p.a->momentum[2]) + p.b->momentum[2];
    float n[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    const float point[4] = {0.0f, 5.0f, 1.0f, 0.0f};
    drv::CollisionInfo ci{};
    CHECK(drv::ResolveCollision(p.a, p.a, p.b, n, point, &ci, 0.1f) == 1);
    const double da = double(p.a->momentum[2]) - 300.0, db = double(p.b->momentum[2]) + 300.0;
    CHECK(da < 0.0 && db > 0.0);
    CHECK(p.a->momentum[2] + p.b->momentum[2] == float(total0));
    CHECK_NEAR(da, -db, 1e-6);
    CHECK(p.a->momentum[0] == 0.0f && p.a->momentum[1] == 0.0f && p.b->momentum[0] == 0.0f);
    CHECK(p.a->vel[2] < 3.0f && p.b->vel[2] > -3.0f);
    CHECK_NEAR(p.a->vel[2], p.a->momentum[2] / 100.0, 1e-6);
    for (int i = 0; i < 3; ++i) {
        CHECK(p.a->ang_momentum[i] == 0.0f);
        CHECK(p.b->ang_momentum[i] == 0.0f);
    }
    // Os dois acordam; o choque é informado com a velocidade normal relativa (-6) e uma força entre 0 e 1.
    CHECK(p.a->state == 2 && p.b->state == 2);
    CHECK(p.a->info->flags[2] == 1 && (p.b->radius_flags & 1u) != 0);
    CHECK_NEAR(ci.speed, -6.0, 1e-5);
    CHECK(ci.strength > 0.0f && ci.strength <= 1.0f);
    CHECK(ci.normal[2] == 1.0f && ci.point[2] == 1.0f);
}

TEST(driving_physics_resolve_collision_ignores_bodies_moving_apart) {
    // A velocidade relativa do ponto ao longo da normal acima de 10 m/s: os corpos já se afastam; devolve 0 e nada
    // muda, nem a saída.
    fresh();
    Pair p = add_pair(100.0f, 80.0f, 2.0f, -6.0f, 6.0f);  // vn = 6 - (-6) = 12
    const drv::RigidBody a0 = *p.a, b0 = *p.b;
    float n[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    const float point[4] = {0.0f, 5.0f, 1.0f, 0.0f};
    drv::CollisionInfo ci{};
    ci.speed = 123.0f;
    CHECK(drv::ResolveCollision(p.a, p.a, p.b, n, point, &ci, 0.0f) == 0);
    CHECK(std::memcmp(&a0, p.a, sizeof(a0)) == 0);
    CHECK(std::memcmp(&b0, p.b, sizeof(b0)) == 0);
    CHECK(ci.speed == 123.0f);
}

TEST(driving_physics_resolve_collision_between_vehicles_shares_by_the_other_mass) {
    // Dois veículos (tipo 2) de 1000 e 1500 kg batendo de frente em z, devagar: o impulso de cada um é proporcional à
    // massa do outro, então m_A ΔP_A = -m_B ΔP_B (o momento não se conserva: o mais leve muda mais de velocidade), e o
    // y do momento não muda.
    fresh();
    Pair p = add_pair(1000.0f, 1500.0f, 4.0f, 5.0f, -3.0f);
    p.a->type = 2;
    p.b->type = 2;
    float n[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    const float point[4] = {0.0f, 5.0f, 2.0f, 0.0f};
    drv::CollisionInfo ci{};
    CHECK(drv::ResolveCollision(p.a, p.a, p.b, n, point, &ci, 0.0f) == 1);
    const double da = double(p.a->momentum[2]) - 5000.0, db = double(p.b->momentum[2]) + 4500.0;
    CHECK(da < 0.0 && db > 0.0);
    CHECK_NEAR(da * 1000.0, -db * 1500.0, std::fabs(db) * 1500.0 * 1e-5);
    CHECK(std::fabs(p.a->vel[2] - 5.0f) > std::fabs(p.b->vel[2] + 3.0f));
    CHECK(p.a->momentum[1] == 0.0f && p.b->momentum[1] == 0.0f);
    CHECK(p.a->momentum[0] == 0.0f && p.b->momentum[0] == 0.0f);
    // Entre veículos as idades zeram e +0x30/+0x34 da saída ficam em 1.
    CHECK(p.a->info->age == 0 && p.b->info->age == 0);
    CHECK(ci.unknown_30[0] == 1.0f && ci.unknown_30[1] == 1.0f);
}

TEST(driving_physics_modify_levers_for_vehicle_and_object) {
    // Um veículo e um objeto: o braço do veículo perde o y e a normal vira a direção (unitária) do centro de A ao
    // ponto; entre dois objetos, nada muda.
    fresh();
    Pair p = add_pair(100.0f, 100.0f, 2.0f, 0.0f, 0.0f);
    p.a->type = 3;
    float ra[4] = {0.5f, 0.7f, 1.0f, 9.0f}, rb[4] = {0.5f, 0.7f, -1.0f, 9.0f};
    float n[4] = {0.0f, 0.0f, 1.0f, 7.0f};
    const float point[4] = {3.0f, 9.0f, 0.0f, 0.0f};  // (3, 4, 0) do centro de A: a direção (0,6; 0,8; 0)
    drv::ModifyLevers(p.a, p.b, ra, rb, point, n, false);
    CHECK(ra[1] == 0.0f && ra[0] == 0.5f && ra[2] == 1.0f);
    CHECK(rb[1] == 0.7f);
    CHECK_NEAR(n[0], 0.6, 1e-6);
    CHECK_NEAR(n[1], 0.8, 1e-6);
    CHECK(n[2] == 0.0f && n[3] == 7.0f);
    p.a->type = 4;
    const float n0[4] = {n[0], n[1], n[2], n[3]};
    drv::ModifyLevers(p.a, p.b, ra, rb, point, n, false);
    CHECK(std::memcmp(n, n0, sizeof(n)) == 0 && rb[1] == 0.7f);
}

TEST(driving_physics_modify_levers_between_vehicles_lowers_aligned_hits) {
    // Dois veículos alinhados (a mesma frente, B 4 m à frente de A): o y de cada braço vai para baixo, a
    // (CAR_COLLIDE_COG_SCALE + 0,12) no carro do jogador (tipo 1) e a (0,1 + 0,12) nos outros. Fora do alinhamento
    // (B virado 90 graus), o y vai a max(0, c - 0,35): 0,05 no jogador e zero nos outros.
    fresh();
    Pair p = add_pair(1000.0f, 1000.0f, 4.0f, 0.0f, 0.0f);
    p.a->type = 1;
    p.b->type = 2;
    float ra[4] = {0.3f, 0.6f, 2.0f, 0}, rb[4] = {0.3f, -0.4f, -2.0f, 0}, n[4] = {0, 0, 1, 0};
    const float point[4] = {0.3f, 5.6f, 2.0f, 0};
    drv::ModifyLevers(p.a, p.b, ra, rb, point, n, true);
    CHECK_NEAR(ra[1], -0.6 * 0.52, 1e-6);
    CHECK_NEAR(rb[1], -0.4 * 0.22, 1e-6);
    CHECK_NEAR(ra[0], 0.3, 1e-6);
    CHECK_NEAR(rb[2], -2.0, 1e-6);
    // B virado 90 graus em y: a frente dele é x.
    const float rot[4][4] = {{0, 0, -1, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 1}};
    std::memcpy(p.b->info->orient, rot, sizeof(rot));
    float ra2[4] = {0.3f, 0.6f, 2.0f, 0}, rb2[4] = {0.3f, -0.4f, -2.0f, 0};
    drv::ModifyLevers(p.a, p.b, ra2, rb2, point, n, true);
    CHECK_NEAR(ra2[1], 0.6 * 0.05, 1e-6);
    CHECK_NEAR(rb2[1], 0.0, 1e-6);
}

TEST(driving_physics_scale_obj_obj_forces_cuts_by_the_squared_ratio) {
    // B a 20 m/s (400 > 225); A parado, com um impulso que o levaria a 40 m/s (1600): o impulso de A é multiplicado
    // por (1,05 x 400) / 1600 (a razão dos quadrados, quirk do PS2), e o de B (que o deixa mais lento) fica. Com a
    // profundidade 1 e A tendo batido num prédio: o impulso de B triplica (2 x 0,5 x 1,5 + 1,5) e o de A cai à metade.
    fresh();
    Pair p = add_pair(100.0f, 100.0f, 4.0f, 0.0f, -20.0f);
    float ia[4] = {0.0f, 0.0f, 4000.0f, 0.0f};   // P_A - ia = -4000: 40 m/s
    float ib[4] = {0.0f, 0.0f, 500.0f, 0.0f};    // P_B + ib = -1500: 15 m/s
    drv::ScaleObjObjForces(p.a, p.b, ia, ib, 225.0f, ps2::ffloat(0x3f866666u), 0.0f);
    CHECK_NEAR(ia[2], 4000.0 * (1.05 * 400.0) / 1600.0, 0.05);
    CHECK(ib[2] == 500.0f);
    p.a->info->flags[1] = 1;
    float ia2[4] = {0.0f, 0.0f, 100.0f, 0.0f}, ib2[4] = {0.0f, 0.0f, 100.0f, 0.0f};
    drv::ScaleObjObjForces(p.a, p.b, ia2, ib2, 225.0f, ps2::ffloat(0x3f866666u), 1.0f);
    CHECK(ia2[2] == 50.0f);
    CHECK(ib2[2] == 300.0f);
    // Ninguém acima do limiar: nada muda.
    p.b->vel[2] = -10.0f;
    float ia3[4] = {0.0f, 0.0f, 4000.0f, 0.0f};
    drv::ScaleObjObjForces(p.a, p.b, ia3, ib2, 225.0f, ps2::ffloat(0x3f866666u), 1.0f);
    CHECK(ia3[2] == 4000.0f);
}

TEST(driving_physics_util_bound_and_car_tensor) {
    CHECK(drv::Util_Bound(5.0f, 2.0f, 3.0f) == 3.0f);
    CHECK(drv::Util_Bound(-5.0f, 2.0f, 3.0f) == -3.0f);
    CHECK(drv::Util_Bound(1.0f, 2.0f, 3.0f) == 1.0f);
    // A caixa (dz > 4): I⁻¹ = 1 / (m x 0,3333 (b² + c²)).
    float t[3];
    drv::Util_GenerateCarTensor(t, 30.0f, 1.0f, 2.0f, 5.0f);
    CHECK_NEAR(t[0], 1.0 / (30.0 * 0.3333 * (4.0 + 25.0)), 1e-6);
    CHECK_NEAR(t[1], 1.0 / (30.0 * 0.3333 * (1.0 + 25.0)), 1e-6);
    CHECK_NEAR(t[2], 1.0 / (30.0 * 0.3333 * (1.0 + 4.0)), 1e-6);
}
