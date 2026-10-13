// O carro das missões de carro (driving/game/bond_car.h) e as rotinas da REAL que ele usa: propriedades independentes
// (o seno da VU0 nos pontos notáveis, as rotações, Util_Step, a escolha da marcha, um carro parado sobre um plano
// comprimindo as molas). A conferência bit a bit contra o código original fica no nf_driving_check (parte 6).

#include <cmath>
#include <cstring>

#include "driving/game/bond_car.h"
#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/real/vu0_math.h"
#include "test_framework.h"

using namespace drv;

namespace {
bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// Um carro de teste numa vaga da Simulation, com a caixa como geometria de colisão (as quatro rodas na frente).
BondCar* make_car(float y) {
    reset_rigid_globals();
    reset_bond_car_tuning();
    simulation_reset(60);
    BondCar* car = new BondCar();
    CarPhysics& p = car->car_physics;
    p.spring_rest_length = 0.24f;
    p.spring_compression_limit = 0.75f;
    p.spring_stiffness_front = p.spring_stiffness_rear = 90.0f;
    p.spring_damping_front = p.spring_damping_rear = 400.0f;
    p.tyre_grip_factor = 8.0f;
    p.friction_limit_front = p.friction_limit_rear = 19.0f;
    p.max_acc = 3.0f;
    p.max_brake = 3.0f;
    p.max_steering = 0.09f;
    p.mod_steer_speed = 10.0f;
    p.min_steer = 0.4f;
    p.fric_mod_speed = 10.0f;
    p.fric_mod_range = 75.0f;
    p.fric_mod_min = 0.45f;
    p.slope_scale = 0.5f;
    p.wheel_radius = 0.36f;
    p.accel_frw_ratio = p.coast_frw_ratio = 0.3f;
    p.wheel_force_app_scale = 0.5f;
    p.num_blend_steps = 20;
    const float limits[6] = {10, 18, 29, 50, 54, 69};
    for (int i = 0; i < 6; ++i) {
        p.gear_limit[i] = limits[i];
        p.gear_ratio[i] = 1.0f - 0.1f * float(i);
    }
    const float half[4] = {0.9f, 0.6f, 2.2f, 0};
    for (int i = 0; i < 8; ++i)
        car->collision_points.push_back(
            {(i & 1) ? half[0] : -half[0], (i & 2) ? half[1] : -half[1], (i & 4) ? half[2] : -half[2], 0.0f});
    RigidBodyParams rp;
    rp.mass = 1600.0f;
    rp.type = 1;
    rp.pos[1] = y;
    std::memcpy(rp.half_size, half, 16);
    Util_GenerateCarTensor(rp.inv_inertia, rp.mass, half[0], half[1], half[2]);
    rp.owner = car;
    RigidBody* rb = simulation_add_body(simulation(), rp, car);
    car->d.slot = rb->slot;
    return car;
}
}  // namespace

TEST(driving_vu0_sin_cos_in_turns) {
    CHECK(VU0_Sin(0.0f) == 0.0f);
    CHECK(near(VU0_Sin(0.25f), 1.0f, 1e-4f));
    CHECK(near(VU0_Sin(-0.25f), -1.0f, 1e-4f));
    CHECK(near(VU0_Cos(0.0f), 1.0f, 1e-4f));
    CHECK(near(VU0_Cos(0.5f), -1.0f, 1e-3f));
    CHECK(near(VU0_Sin(0.125f), std::sqrt(0.5f), 1e-4f));
    // A redução inteira: 1,25 voltas é 0,25 exatamente.
    CHECK(VU0_Sin(1.25f) == VU0_Sin(0.25f));
    // O polinômio de grau 9 do jogo erra perto de meia volta (pi): sen(pi) sai 0,0033, não 0.
    CHECK(near(VU0_Sin(0.5f), 0.00327f, 0.0002f));
}

TEST(driving_vu0_axis_rotations) {
    float m[4][4];
    VU0_MATRIX4setyrot(m, 0.25f);  // um quarto de volta em y: x vai para -z (vetor linha; o cosseno é sen(pi) do jogo)
    CHECK(near(m[0][0], 0.0f, 0.01f) && m[0][2] < -0.999f && m[1][1] == 1.0f && m[3][3] == 1.0f);
    VU0_MATRIX4setxrot(m, 0.25f);  // y vai para z
    CHECK(m[1][2] > 0.999f && m[0][0] == 1.0f && near(m[1][1], 0.0f, 0.01f));
    VU0_MATRIX4setzrot(m, 0.25f);  // x vai para y
    CHECK(m[0][1] > 0.999f && m[2][2] == 1.0f);
}

TEST(driving_util_step) {
    CHECK(Util_Step(0.0f, 1.0f, 0.25f) == 0.25f);
    CHECK(Util_Step(0.9f, 1.0f, 0.25f) == 1.0f);
    CHECK(Util_Step(0.0f, -1.0f, 0.25f) == -0.25f);
    CHECK(Util_Step(-0.9f, -1.0f, 0.25f) == -1.0f);
}

TEST(driving_car_rpm_picks_the_gear_by_speed) {
    BondCar* car = make_car(1.0f);
    RigidBody* rb = car->body();
    car->d.num_wheels_on_ground = 4;
    rb->vel[2] = 20.0f;  // entre GEAR_LIMIT1 (18) e GEAR_LIMIT2 (29)
    const float rpm = PBondCar_CalculateRPM(*car);
    CHECK(car->d.gear == 2);
    CHECK(near(rpm, 20.0f * 6000.0f / 29.0f, 1.0f));
    rb->vel[2] = 100.0f;  // além de todas: a quinta
    PBondCar_CalculateRPM(*car);
    CHECK(car->d.gear == 5);
    car->d.num_wheels_on_ground = 0;  // no ar: o acelerador
    car->d.control.gas = 0.5f;
    CHECK(PBondCar_CalculateRPM(*car) == 3500.0f);
    simulation_reset(60);
    delete car;
}

TEST(driving_car_springs_hold_it_on_a_plane) {
    // A caixa 0,6 abaixo do centro; o centro a 0,7 do plano: as molas (descanso 0,24) comprimidas 0,14.
    FlatGround ground(0.0f);
    set_world(&ground);
    BondCar* car = make_car(0.7f);
    RigidBody* rb = car->body();
    std::memset(rb->force, 0, 12);
    car->d.physics = nf::p32_u(&car->car_physics);
    PBondCar_ProcessPhysics(*car);
    CHECK(car->d.num_wheels_on_ground == 4);
    for (int w = 0; w < 4; ++w) CHECK(near(car->d.suspension[w], 0.14f, 1e-3f));
    CHECK(rb->force[1] > 0.0f);           // as molas empurram para cima
    CHECK(near(rb->force[0], 0.0f, 1.0f)); // e nada de lado num carro parado e reto
    // Sem chão por perto (muito alto): nenhuma roda.
    rb->pos[1] = 5.0f;
    for (int w = 0; w < 4; ++w) car->d.suspension[w] = 0.0f;
    car->d.num_wheels_on_ground = 0;
    PBondCar_ProcessPhysics(*car);
    CHECK(car->d.num_wheels_on_ground == 0);
    set_world(nullptr);
    simulation_reset(60);
    delete car;
}
