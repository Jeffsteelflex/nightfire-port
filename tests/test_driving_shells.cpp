// As balas do jogo de carro (driving/game/shells.h; docs/driving.md seção 24): propriedades independentes das
// utilidades (o raio contra a esfera, a esfera esticada, a matriz que olha para uma direção, a perturbação, o rand e o
// log da biblioteca do PS2, o Box-Muller), dos globais (os rastros, os raios dos gatilhos) e do tiro inteiro do
// RayShell e do Shell numa Simulation montada à mão, sem mundo. A comparação com o código original fica na parte 20 do
// nf_driving_check (tests/driving_check_integ_shells.cpp).

#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "driving/ai/sim_random.h"
#include "driving/game/bond_car.h"
#include "driving/game/explosion.h"
#include "driving/game/mission.h"
#include "driving/game/shells.h"
#include "driving/game/weapons.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

float len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Um dono de corpo rígido que anota o dano recebido.
struct Hit {
    float from[3], to[3], amount, split;
    int32_t kind;
    bool player;
};
class Box : public BasicOwner {
public:
    int32_t type = 2;
    std::unique_ptr<WWorldPos> wp = std::make_unique<WWorldPos>();  // (fora do objeto: o alinhamento de 16)
    std::vector<Hit> hits;
    std::vector<float> by_player;
    std::map<uint32_t, uint32_t> words;
    Box() { set_world_pos(wp.get()); }
    int32_t kind() override { return type; }
    int32_t apply_damage(const float from[3], const float to[3], float amount, float split, int32_t kind) override {
        Hit h{};
        std::memcpy(h.from, from, 12);
        std::memcpy(h.to, to, 12);
        h.amount = amount;
        h.split = split;
        h.kind = kind;
        h.player = damage_source_is_player();
        hits.push_back(h);
        return 0;
    }
    void add_damage_by_player(float damage) override { by_player.push_back(damage); }
    uint32_t raw_word(uint32_t offset) override {
        auto it = words.find(offset);
        return it == words.end() ? 0u : it->second;
    }
    bool set_raw_word(uint32_t offset, uint32_t value) override {
        words[offset] = value;
        return true;
    }
};

// Uma Simulation nova, sem mundo, sem missão, com os globais das balas zerados.
struct Rig {
    Rig() {
        mission_reset();
        simulation_reset(60);
        shells_reset();
        effects_reset();
        shells_env() = ShellsEnv{};
    }
    ~Rig() {
        shells_env() = ShellsEnv{};
        shells_reset();
        simulation_reset(60);
    }
    // Um corpo rígido com o dono `o` em `pos` (meias-medidas `half`).
    RigidBody* add(PhysicsObject* o, const float pos[3], float half, int32_t type = 2) {
        RigidBodyParams p;
        p.type = type;
        p.mass = 100.0f;
        for (int k = 0; k < 3; ++k) p.pos[k] = pos[k];
        p.half_size[0] = p.half_size[1] = p.half_size[2] = half;
        p.moving = 0;
        return simulation_add_body(simulation(), p, o);
    }
};

}  // namespace

TEST(driving_shells_ray_meets_a_sphere_ahead_not_behind_nor_from_inside) {
    alignas(16) const float o[4] = {0, 0, 0, 1}, ahead[4] = {1, 0, 0, 0}, back[4] = {-1, 0, 0, 0};
    alignas(16) const float c[4] = {10, 0.5f, 0, 1}, far_c[4] = {10, 1.5f, 0, 1}, near_c[4] = {0.2f, 0, 0, 1};
    float out[3] = {};
    // O raio passa a 0,5 do centro: o ponto devolvido é o do raio mais perto do centro, dentro da esfera.
    CHECK(Util_CollideRayWithSphere(1.0f, o, ahead, c, out));
    CHECK_NEAR(out[0], 10.0, 1e-3);
    CHECK_NEAR(out[1], 0.0, 1e-6);
    CHECK(!Util_CollideRayWithSphere(1.0f, o, back, c, out));     // a esfera atrás
    CHECK(!Util_CollideRayWithSphere(1.0f, o, ahead, far_c, out)); // a 1,5 do raio
    CHECK(!Util_CollideRayWithSphere(1.0f, o, ahead, near_c, out)); // a origem dentro da esfera
    // A esfera esticada 3 vezes em y alcança o raio a 1,2 acima do centro (a de raio 0,5 não).
    alignas(16) const float o2[4] = {0, 1.2f, 0, 1}, c2[4] = {10, 0, 0, 1};
    CHECK(!Util_CollideRayWithSphere(0.5f, o2, ahead, c2, out));
    CHECK(Util_CollideRayWithStretchedSphere(0.5f, 3.0f, o2, ahead, c2, out));
    CHECK_NEAR(out[1], 1.2, 1e-5);
    CHECK_NEAR(out[0], 10.0, 1e-3);
}

TEST(driving_shells_matrix_unit_looks_along_the_direction) {
    const float dirs[][3] = {{0, 0, 1}, {3, 0, 4}, {-1, 2, 0.5f}, {0.2f, -0.9f, 0.1f}};
    for (const auto& d : dirs) {
        float m[4][4];
        Util_GenerateMatrixUnit(m, d);
        const float l = len3(d);
        for (int k = 0; k < 3; ++k) CHECK_NEAR(m[2][k], d[k] / l, 1e-5);
        for (int i = 0; i < 3; ++i) CHECK_NEAR(len3(m[i]), 1.0, 1e-5);
        CHECK_NEAR(dot3(m[0], m[1]), 0.0, 1e-5);
        CHECK_NEAR(dot3(m[0], m[2]), 0.0, 1e-5);
        CHECK_NEAR(dot3(m[1], m[2]), 0.0, 1e-5);
        // A linha 0 é horizontal (o "para cima" (0, 1, 0) vezes a direção).
        CHECK_NEAR(m[0][1], 0.0, 1e-6);
        CHECK(m[3][0] == 0.0f && m[3][1] == 0.0f && m[3][2] == 0.0f && m[3][3] == 1.0f);
    }
}

TEST(driving_shells_perturb_keeps_the_length) {
    const float in[3] = {0.6f, -0.3f, 2.0f};
    float out[3];
    Util_PerturbVector(0.0f, 0.0f, out, in);
    for (int k = 0; k < 3; ++k) CHECK_NEAR(out[k], in[k], 1e-5);
    const float angles[][2] = {{0.1f, -0.05f}, {1.0f, 0.3f}, {-2.0f, 1.2f}};
    for (const auto& a : angles) {
        Util_PerturbVector(a[0], a[1], out, in);
        CHECK_NEAR(len3(out), len3(in), 1e-4);
    }
    // Só o giro em y (o pitch 0) não muda o y.
    Util_PerturbVector(0.7f, 0.0f, out, in);
    CHECK_NEAR(out[1], in[1], 1e-5);
}

TEST(driving_shells_rand_is_the_classic_lcg_and_log_is_ieee) {
    Rig rig;
    gauss_state().rand_next = 1;  // srand(1)
    CHECK(ee_rand() == 1103527590);
    CHECK(ee_rand() == 377401575);
    CHECK(ee_rand() == 662824084);
    CHECK(ee_log(1.0) == 0.0);
    CHECK(ee_log(2.0) == 0.6931471805599453);
    const double xs[] = {1e-6, 0.001, 0.25, 0.5, 0.999, 1.5, 3.0, 10.0, 12345.678};
    for (double x : xs) CHECK_NEAR(ee_log(x), std::log(x), std::fabs(std::log(x)) * 4e-16 + 1e-300);
}

TEST(driving_shells_gauss_random_has_unit_variance_and_keeps_the_pair) {
    Rig rig;
    gauss_state().rand_next = 12345;
    double sum = 0, sq = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        const double v = Util_GaussRandom();
        sum += v;
        sq += v * v;
    }
    const double mean = sum / n;
    CHECK_NEAR(mean, 0.0, 0.03);
    CHECK_NEAR(sq / n - mean * mean, 1.0, 0.05);
    // Um número novo sorteia (o rand anda) e guarda o par; o seguinte é o guardado (o rand não anda).
    gauss_state().have = 0;
    Util_GaussRandom();
    CHECK(gauss_state().have == 1);
    const uint32_t r = gauss_state().rand_next;
    const float saved = gauss_state().saved;
    CHECK(Util_GaussRandom() == saved);
    CHECK(gauss_state().rand_next == r && gauss_state().have == 0);
}

TEST(driving_shells_tracers_store_skip_and_clear) {
    Rig rig;
    Simulation& sim = simulation();
    RayShellGlobals& g = rayshell_globals();
    const float a[3] = {0, 0, 0}, b[3] = {0, 0, 10}, near_b[3] = {0, 0, 1.5f};
    sim.steps = 100;
    RayShell_StaticStoreTracer(a, b, -1, 0, 0);  // sem rastro
    CHECK(g.num_tracers == 0);
    RayShell_StaticStoreTracer(a, b, 0, 7, 9);
    CHECK(g.num_tracers == 1);
    // O rastro comum começa 2 m adiante (a bala andou mais de 2 m); o lado guarda o RSceneObj e o efeito.
    CHECK_NEAR(g.tracers[0].from[2], 2.0, 1e-5);
    CHECK_NEAR(g.tracers[0].to[2], 10.0, 1e-6);
    CHECK(g.tracers[0].step == 100 && g.tracers[0].type == 0);
    CHECK(g.tracer_side[0][0] == 7 && g.tracer_side[0][1] == 9);
    RayShell_StaticStoreTracer(a, near_b, 0, 0, 0);  // menos de 2 m: fica no cano
    CHECK_NEAR(g.tracers[1].from[2], 0.0, 1e-6);
    RayShell_StaticStoreTracer(a, b, 2, 0, 0);  // outro tipo: fica no cano
    CHECK_NEAR(g.tracers[2].from[2], 0.0, 1e-6);
    // O DeleteTracer põe o último no lugar.
    RayShell_DeleteTracer(0);
    CHECK(g.num_tracers == 2 && g.tracers[0].type == 2);
    // ClearTracers apaga os de antes do passo; o Reset, todos os do passo atual também.
    sim.steps = 105;
    RayShell_StaticStoreTracer(a, b, 1, 0, 0);
    RayShell_ClearTracers(101);
    CHECK(g.num_tracers == 1 && g.tracers[0].step == 105);
    RayShell_Reset();
    CHECK(g.num_tracers == 0 && g.num_active == 0 && g.num_flashes == 0);
    // No máximo 64.
    for (int i = 0; i < 70; ++i) RayShell_StaticStoreTracer(a, b, 1, 0, 0);
    CHECK(g.num_tracers == 64);
}

TEST(driving_shells_trigger_rays_keep_at_most_63) {
    Rig rig;
    RayShellData r{};
    r.pos[0] = 1;
    r.pos[1] = 2;
    r.pos[2] = 3;
    r.dir[2] = 1;
    r.damage = 40;
    r.sig = 0x01000005;
    RayShell_AddNewRayShellForTriggerCheck(r, 8.0f);
    const ActiveRayShell* a = RayShell_GetActiveRayShell(0);
    CHECK(RayShell_GetNumActiveRayShells() == 1);
    CHECK(a->sig == 0x01000005 && a->damage == 40.0f);
    CHECK_NEAR(a->end[2], 11.0, 1e-6);
    CHECK_NEAR(a->half, 4.0, 1e-6);
    for (int i = 0; i < 70; ++i) RayShell_AddNewRayShellForTriggerCheck(r, 1.0f);
    CHECK(RayShell_GetNumActiveRayShells() == 63);
    CHECK(shells_stats().too_many > 0);
    // O raio do gatilho: nenhum com -1, o do índice depois.
    RayShell_GetNumTracers(-1);
    CHECK(RayShell_GetTracer() == nullptr);
    RayShell_GetNumTracers(5);
    CHECK(RayShell_GetTracer() == RayShell_GetActiveRayShell(5));
    RayShell_ClearActiveRayShells();
    CHECK(RayShell_GetNumActiveRayShells() == 0);
}

TEST(driving_shells_enemy_accuracy_boost_follows_the_difficulty) {
    Rig rig;
    SMissionManager_Construct(false);
    static uint8_t settings[0x2f8] = {};
    const int32_t v[3] = {7, -3, 25};
    std::memcpy(settings + 0x2ac, v, 12);
    SMissionManagerData& m = mission_manager();
    m.settings = nf::p32_u(settings);
    for (int d = 0; d < 3; ++d) {
        m.difficulty = d;
        CHECK(SMissionManager_GetEnemyAccuracyBoost() == v[d]);
    }
    mission_reset();
}

TEST(driving_shells_rayshell_damages_the_box_in_front_once) {
    Rig rig;
    Simulation& sim = simulation();
    Box shooter, target, aside;
    const float p_shooter[3] = {0, 1, -2}, p_target[3] = {0, 1, 12}, p_aside[3] = {6, 1, 6};
    rig.add(&shooter, p_shooter, 0.5f);
    rig.add(&target, p_target, 1.0f);
    rig.add(&aside, p_aside, 1.0f);
    const uint32_t sig = PhysicsObject_GetSig(&shooter);
    const float pos[3] = {0, 1, 0}, dir[3] = {0, 0, 3}, muzzle[3] = {0, 1, 0.5f};
    RayShellData r{};
    RayShell_Construct(r, 220.0f, 30.0f, 0.0f, 0, sig, pos, dir, muzzle, 0, 0, 1, 1);
    // Um tiro, no alvo: do começo do raio ao ponto, 30 de dano, 1/4 de divisão, o tipo 2, não do jogador.
    CHECK(target.hits.size() == 1);
    CHECK(aside.hits.empty() && shooter.hits.empty());
    if (!target.hits.empty()) {
        const Hit& h = target.hits[0];
        CHECK(h.amount == 30.0f && h.split == 0.25f && h.kind == 2 && !h.player);
        CHECK_NEAR(h.from[2], 0.0, 1e-6);
        CHECK_NEAR(h.to[2], 12.0, 1e-3);
    }
    // A direção unitária; o raio para os gatilhos até o alvo; o rastro do cano ao ponto.
    CHECK_NEAR(r.dir[2], 1.0, 1e-6);
    CHECK(r.state == 1 && r.sig1 == PhysicsObject_GetSig(&target));
    CHECK(rayshell_globals().num_active == 1);
    CHECK_NEAR(rayshell_globals().active[0].end[2], r.dist1, 1e-4);
    CHECK(rayshell_globals().num_tracers == 1);
    CHECK_NEAR(rayshell_globals().tracers[0].to[2], 12.0, 1e-3);
    CHECK(sim.rigid_count == 3);
}

TEST(driving_shells_rayshell_ignores_what_the_shooter_owns_and_reaches_the_range) {
    Rig rig;
    Box shooter, own;
    const float p_shooter[3] = {0, 1, -2}, p_own[3] = {0, 1, 8};
    rig.add(&shooter, p_shooter, 0.5f);
    rig.add(&own, p_own, 1.0f);
    const uint32_t sig = PhysicsObject_GetSig(&shooter);
    own.words[0x58] = sig;  // o +0x58: quem o criou
    const float pos[3] = {0, 1, 0}, dir[3] = {0, 0, 1};
    RayShellData r{};
    RayShell_Construct(r, 500.0f, 30.0f, 0.0f, 0, sig, pos, dir, pos, 0, 0, 1, 2);
    CHECK(own.hits.empty());
    CHECK(r.state == 0);
    // Ninguém: o alcance fica em 220, o rastro vai até ele e o raio dos gatilhos também.
    CHECK(r.range == 220.0f);
    CHECK(rayshell_globals().num_tracers == 1 && rayshell_globals().tracers[0].type == 2);
    CHECK_NEAR(rayshell_globals().tracers[0].to[2], 220.0, 1e-3);
    CHECK_NEAR(rayshell_globals().active[0].half, 110.0, 1e-3);
}

TEST(driving_shells_shell_flies_for_its_life_and_is_deleted) {
    Rig rig;
    Simulation& sim = simulation();
    RRandom_StartUp(render_random(), 1234);
    const float dir[3] = {0, 0, 1}, pos[3] = {0, 5, 0};
    Shell* s = SpawnShellObject(sim, 20.0f, 0.0f, 0, dir, pos, 0, -1);
    CHECK(s != nullptr);
    if (!s) return;
    std::unique_ptr<Shell> own(s);
    CHECK(s->kind() == 7 && s->po.owner_sig == 0 && sim.shells.size() == 1);
    // A vida de 1,3 s a 60 Hz (o cvt.w trunca 77,99...) e a velocidade de 200 a 230 m/s.
    CHECK(s->d.life == 77 || s->d.life == 78);
    const SimpleRigidBody* b = GetSimpleRigidBody(sim, s->po.slot);
    CHECK((b->flags & 0x169) == 0x169);
    CHECK(len3(b->vel) >= 200.0f && len3(b->vel) < 230.0f);
    const int life = s->d.life;
    int steps = 0;
    while (!sim.shells.empty() && steps < 200) {
        Shell_Simulate(*s);
        ++steps;
    }
    CHECK(steps == life);
    CHECK(sim.shells.empty() && !sim.dead.empty() && sim.dead.back() == s);
    // O canhão (0x10): 1 s e de 170 a 200 m/s.
    Shell* c = SpawnShellObject(sim, 20.0f, 5.0f, 0x10, dir, pos, 0, -1);
    CHECK(c != nullptr);
    if (!c) return;
    std::unique_ptr<Shell> own_c(c);
    CHECK(c->d.life == 60);
    const float v = len3(GetSimpleRigidBody(sim, c->po.slot)->vel);
    CHECK(v >= 170.0f && v < 200.0f);
}

TEST(driving_shells_window_needs_a_barrier_hit_and_owner_words_need_an_image) {
    Rig rig;
    WorldCollisionInfo info;
    WorldCollisionInfo_Init(info);
    info.hit = 1;  // o chão: nunca uma janela
    CHECK(!WCollisionMgr_CheckHitWindow(info, true, 0));
    // Os donos sem a imagem do PS2 só gravam pelo set_raw_word; o índice na ordem do GetOrderedBody.
    Box box;
    BasicOwner plain;
    const float at[3] = {0, 0, 0};
    rig.add(&box, at, 1.0f);
    object_set_word(&box, 0x64, 3);
    CHECK(object_word(&box, 0x64) == 3);
    const uint64_t g0 = shells_stats().guards;
    object_set_word(&plain, 0x64, 3);
    CHECK(shells_stats().guards == g0 + 1);
    CHECK(object_ordered_index(simulation(), &box) == 0);
    CHECK(object_ordered_index(simulation(), &plain) == -1);
}
