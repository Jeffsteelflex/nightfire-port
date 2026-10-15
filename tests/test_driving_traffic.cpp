// O mundo vivo das missões de carro (driving/game/triggers.h, smackable.h): propriedades independentes (o toque de um
// gatilho em esfera, em caixa e com sentido; o tensor de uma caixa e de uma barra; o nó da árvore do mapa que contém um
// círculo; o gatilho que um objeto derrubado leva consigo; o gerador do render). A conferência bit a bit contra o código
// original fica no nf_driving_check (parte 14).

#include <cmath>
#include <cstring>
#include <memory>

#include "driving/ai/sim_random.h"
#include "driving/game/smackable.h"
#include "driving/game/triggers.h"
#include "driving/physics/rigid_body.h"
#include "test_framework.h"

using namespace drv;

namespace {

// Um corpo de raio `r` no ponto, com a caixa alinhada de meias-medidas (h, h, h) e a velocidade dada (no heap: o +0x5c
// é um ponteiro de 4 bytes, que no x64 precisa da janela da arena).
struct TestBody {
    RigidBody body{};
    RigidBodyInfo info{};
    TestBody(float x, float y, float z, float r, float h, float vz = 0) {
        body.pos[0] = x;
        body.pos[1] = y;
        body.pos[2] = z;
        body.vel[2] = vz;
        std::memcpy(&body.radius_flags, &r, 4);
        for (int i = 0; i < 4; ++i) info.orient[i][i] = 1.0f;
        info.half_size[0] = info.half_size[1] = info.half_size[2] = h;
        body.info = &info;
    }
};

}  // namespace

// Uma esfera de raio 1 na origem: um corpo de raio 0,5 a 1,4 m toca (a soma dos raios é 1,5), a 1,6 m não; com o
// sentido ligado (frente +z) só quem anda para +z (ou parado) a toca.
TEST(driving_trigger_sphere_touch_and_direction) {
    WTrigger t{};
    t.shape = 2;
    t.radius = 1.0f;
    t.forward[2] = 1.0f;
    auto in = std::make_unique<TestBody>(1.4f, 0.0f, 0.0f, 0.5f, 0.5f);
    auto out = std::make_unique<TestBody>(1.6f, 0.0f, 0.0f, 0.5f, 0.5f);
    CHECK(WTriggerManager_CheckCollide(&in->body, t));
    CHECK(!WTriggerManager_CheckCollide(&out->body, t));
    t.flags = kTrigDirectional;
    auto forward = std::make_unique<TestBody>(1.0f, 0.0f, 0.0f, 0.5f, 0.5f, 3.0f);
    auto backward = std::make_unique<TestBody>(1.0f, 0.0f, 0.0f, 0.5f, 0.5f, -3.0f);
    CHECK(WTriggerManager_CheckCollide(&forward->body, t));
    CHECK(!WTriggerManager_CheckCollide(&backward->body, t));
}

// Um cilindro de 2 m de altura a partir de y = 0: um corpo acima do topo (mais o raio) não o toca. Uma caixa de 2 x 2 x 2
// (o raio de 3 só deixa chegar ao teste das caixas): um corpo de meia-medida 0,5 a 1,4 m do centro em x a toca, a 1,6 m
// não.
TEST(driving_trigger_cylinder_height_and_box) {
    WTrigger c{};
    c.shape = 3;
    c.radius = 1.0f;
    c.height = 2.0f;
    auto low = std::make_unique<TestBody>(0.2f, 1.0f, 0.0f, 0.5f, 0.5f);
    auto high = std::make_unique<TestBody>(0.2f, 2.6f, 0.0f, 0.5f, 0.5f);
    CHECK(WTriggerManager_CheckCollide(&low->body, c));
    CHECK(!WTriggerManager_CheckCollide(&high->body, c));
    WTrigger b{};
    b.shape = 1;
    b.radius = 3.0f;
    b.height = 2.0f;
    b.width = 2.0f;
    b.depth = 2.0f;
    b.right[0] = 1.0f;
    b.forward[2] = 1.0f;
    auto close_by = std::make_unique<TestBody>(1.4f, 1.0f, 0.0f, 0.5f, 0.5f);
    auto away = std::make_unique<TestBody>(1.6f, 1.0f, 0.0f, 0.5f, 0.5f);
    CHECK(WTriggerManager_CheckCollide(&close_by->body, b));
    CHECK(!WTriggerManager_CheckCollide(&away->body, b));
}

// O tensor inverso: um cubo de lado 2 (meias-medidas 1) e massa 12 tem 3 / (12 x 2) = 0,125 nos três eixos; uma barra
// (5 (x + z) < y) tem os dois eixos de través iguais e o do comprimento 1 / (m/2 (x² + y²)).
TEST(driving_tensor_of_a_box_and_a_bar) {
    float out[3];
    Util_GenerateTensor(out, 12.0f, 1.0f, 1.0f, 1.0f);
    CHECK_NEAR(out[0], 0.125f, 1e-6f);
    CHECK_NEAR(out[1], 0.125f, 1e-6f);
    CHECK_NEAR(out[2], 0.125f, 1e-6f);
    Util_GenerateTensor(out, 100.0f, 0.1f, 5.0f, 0.1f);
    CHECK(out[0] == out[1]);
    CHECK_NEAR(out[2], 1.0 / (50.0 * (0.01 + 25.0)), 1e-7);
    CHECK(out[0] > 0.0f && out[0] < out[2] * 100.0f);
}

// Um nó de profundidade 1 (o filho x+, z- da raiz de lado 200 centrada na origem) é o quadrado de centro (50, -50) e
// meio lado 50: um círculo de raio 10 no centro cabe, um de raio 60 não, um encostado na borda de fora também não.
TEST(driving_tree_node_contains_a_circle) {
    MapNode n{};
    n.depth = 1;
    n.x = 1;
    n.z = 0;
    const float box[4] = {0, 0, 0, 100};
    const float small[4] = {50, 0, -50, 10}, big[4] = {50, 0, -50, 60}, edge[4] = {95, 0, -50, 10};
    CHECK(WTree_CheckContainment(n, box, small));
    CHECK(!WTree_CheckContainment(n, box, big));
    CHECK(!WTree_CheckContainment(n, box, edge));
}

// O gatilho que um objeto derrubado leva (o GoToSleep): vai para a posição com o pé meia altura abaixo; uma caixa vira
// cilindro, com o raio 0,55 x altura quando era menor que a altura.
TEST(driving_trigger_follows_a_fallen_object) {
    WTrigger t{};
    t.shape = 1;
    t.height = 4.0f;
    t.radius = 1.0f;
    const float pos[3] = {10.0f, 3.0f, -7.0f};
    CHECK(WTrigger_UpdateRotPos(t, pos));
    CHECK(t.position[0] == 10.0f && t.position[1] == 1.0f && t.position[2] == -7.0f);
    CHECK(t.shape == 3);
    CHECK_NEAR(t.radius, 2.2f, 1e-5f);
}

// O gerador do render: o StartUp com o mesmo número dá a mesma sequência; os números ficam em 0..65535 e não se repetem
// logo (a semente de 16 bits tem período curto, mas não de um passo).
TEST(driving_render_random_sequence) {
    RRandom a{}, b{};
    RRandom_StartUp(a, 0x2e81a11u);
    RRandom_StartUp(b, 0x2e81a11u);
    CHECK(a.multiplier == 123456789u && a.seed == b.seed && a.seed <= 0xffffu);
    uint32_t prev = 0x10000u;
    int repeats = 0;
    for (int i = 0; i < 64; ++i) {
        const uint32_t x = RRandom_Generate(a);
        CHECK(x <= 0xffffu && x == RRandom_Generate(b));
        repeats += x == prev;
        prev = x;
    }
    CHECK(repeats == 0);
}
