// A IA dos elementos e o dano do carro (driving/ai/element_controller.h, ground_vehicle.h, driving/game/bond_car.h "o
// dano"; docs/driving.md seção 21): propriedades independentes sobre dados montados à mão (sem dados do jogo). A
// conferência contra o código original é a parte 17 do nf_driving_check.

#include <cstring>
#include <vector>

#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/carp_runtime.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

// A caixa das zonas de um carro: x de -1 a 1, y de 0 a 1,2, z de -2 a 2 (o ponto é negado em x e z).
const float kZoneMin[3] = {-1.0f, 0.0f, -2.0f};
const float kZoneMax[3] = {1.0f, 1.2f, 2.0f};

}  // namespace

// As vizinhas de cada zona (a tabela 0x3c1fe8): nenhuma zona é vizinha de si mesma e todas têm vizinhas.
TEST(driving_ai_damage_zone_neighbours) {
    for (int z = 0; z < 16; ++z) {
        const uint32_t n = DamageZoneNeighbours(z);
        CHECK(n != 0);
        CHECK((n & (1u << z)) == 0);
        CHECK(n < 0x10000u);
    }
}

// As zonas pelas faixas da caixa: o meio (sem cima nem embaixo) é a zona do código 0x480; embaixo, longe dos dois lados
// (q.x < min, q.z < min), 0x42; em cima, além do x máximo e antes do z mínimo, 0x49.
TEST(driving_ai_damage_zone_of_points) {
    const float centre[4] = {-0.5f, 0.6f, -1.0f, 0.0f};  // q = (0,5; 0,6; 1): x e z na metade de cima
    CHECK(CalcDamageZone(kZoneMin, kZoneMax, centre) == 6);
    const float below[4] = {2.0f, -1.0f, 3.0f, 0.0f};    // q = (-2; -1; -3)
    CHECK(CalcDamageZone(kZoneMin, kZoneMax, below) == 14);
    const float above[4] = {-2.0f, 2.0f, 3.0f, 0.0f};    // q = (2; 2; -3)
    CHECK(CalcDamageZone(kZoneMin, kZoneMax, above) == 10);
    // Toda zona devolvida está na tabela (0..15).
    for (int i = 0; i < 64; ++i) {
        const float p[4] = {float(i % 4) - 1.5f, float(i % 3) - 0.5f, float(i / 4) * 0.5f - 4.0f, 0.0f};
        const int32_t z = CalcDamageZone(kZoneMin, kZoneMax, p);
        CHECK(z >= 0 && z < 16);
    }
}

// BytesToCoord: os bytes 2, 1 e 0 / 255, mais a soma, vezes a escala (o torque sorteado do tiro).
TEST(driving_ai_bytes_to_coord) {
    float out[4] = {9, 9, 9, 9};
    BytesToCoord(0x00ff0080u, nullptr, nullptr, nullptr, out);
    CHECK_NEAR(out[0], 1.0f, 1e-6);
    CHECK_NEAR(out[1], 0.0f, 1e-6);
    CHECK_NEAR(out[2], 128.0f / 255.0f, 1e-6);
    const float lo[3] = {-0.5f, -0.5f, -0.5f}, hi[3] = {6000.0f, 7000.0f, 6000.0f};
    BytesToCoord(0x00ff0080u, lo, hi, nullptr, out);
    CHECK_NEAR(out[0], 3000.0f, 1e-2);
    CHECK_NEAR(out[1], -3500.0f, 1e-2);
    CHECK_NEAR(out[2], (128.0f / 255.0f - 0.5f) * 6000.0f, 1e-2);
}

// FindOBBIntersect: de fora pelo eixo x até o centro, o ponto fica na face x = meia-medida; de dentro, dentro da caixa.
TEST(driving_ai_obb_intersect) {
    const float half[4] = {1.0f, 1.0f, 2.0f, 0.0f};
    const float a[4] = {5.0f, 0.2f, 0.1f, 0.0f}, b[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float out[4] = {0, 0, 0, 0};
    CHECK(FindOBBIntersect(half, a, b, out));
    CHECK_NEAR(out[0], 1.0f, 1e-4);
    CHECK(std::fabs(out[1]) <= 1.1f && std::fabs(out[2]) <= 2.1f);
    const float c[4] = {-3.0f, 0.0f, 9.0f, 0.0f};
    CHECK(FindOBBIntersect(half, c, b, out));
    CHECK(std::fabs(out[0]) <= 1.1f && std::fabs(out[1]) <= 1.1f && std::fabs(out[2]) <= 2.1f);
}

// PhysicsObject::LoseHitPoints: a vida no float do +0x54, nunca abaixo de 0, e um dano negativo não cura.
TEST(driving_ai_lose_hit_points) {
    BondCar car;
    car.d.hit_points = 100.0f;
    car.d.hit_point_loc = nf::p32_u(&car.d.hit_points);
    PhysicsObject_LoseHitPoints(car, 30.0f);
    CHECK_NEAR(car.d.hit_points, 70.0f, 1e-6);
    PhysicsObject_LoseHitPoints(car, -50.0f);
    CHECK_NEAR(car.d.hit_points, 70.0f, 1e-6);
    PhysicsObject_LoseHitPoints(car, 500.0f);
    CHECK(car.d.hit_points == 0.0f);
    car.d.hit_point_loc = 0;
    PhysicsObject_LoseHitPoints(car, 10.0f);  // sem a vida: nada
    CHECK(car.d.hit_points == 0.0f);
}

// O AIZoneController: a fumaça (tipo 4) registrada no ponto o contém e não o de longe; outra igual no mesmo lugar não
// abre zona nova; o tipo 1 (sem limite) não registra.
TEST(driving_ai_zone_controller) {
    AIZoneController_Init();
    std::memset(AIZoneController_Zones(), 0, 30 * 0x1c);
    AIZoneController_Active() = 0;
    const float p[3] = {10.0f, 0.0f, 20.0f}, far[3] = {100.0f, 0.0f, 20.0f}, near[3] = {11.0f, 0.0f, 21.0f};
    AIZoneController_RegisterZone(4, p);
    CHECK(AIZoneController_Active() == 1);
    CHECK(AIZoneController_InZone(0.0f, 0, p));
    CHECK(AIZoneController_InZone(0.0f, 0, near));
    CHECK(!AIZoneController_InZone(0.0f, 0, far));
    AIZoneController_RegisterZone(4, near);
    CHECK(AIZoneController_Active() == 1);
    AIZoneController_RegisterZone(1, far);
    CHECK(AIZoneController_Active() == 1);
    AIZoneController_RegisterZone(3, far);
    CHECK(AIZoneController_Active() == 2);
}

// GetBestWeightedLane: a janela livre de largura +0xd8 de menor soma dá o meio dela; tudo bloqueado, 32.
TEST(driving_ai_best_weighted_lane) {
    AIGroundVehicle v;
    AIGroundVehicle_Construct(v);
    v.lane_weight_width = 3;
    std::memset(v.unknown098, 0xff, sizeof v.unknown098);
    CHECK(AIGroundVehicle_GetBestWeightedLane(v) == 0x20);
    v.unknown098[10] = v.unknown098[11] = v.unknown098[12] = 0;
    CHECK(AIGroundVehicle_GetBestWeightedLane(v) == 11);
    v.unknown098[40] = v.unknown098[41] = v.unknown098[42] = 0;  // empate: fica a primeira
    CHECK(AIGroundVehicle_GetBestWeightedLane(v) == 11);
}

// O qsort do newlib: ordena (a comparação do Update: o "à frente" decrescente).
TEST(driving_ai_newlib_qsort_sorts) {
    struct Pair {
        int32_t index;
        float facing;
    };
    std::vector<Pair> v;
    uint32_t s = 12345;
    for (int i = 0; i < 57; ++i) {
        s = s * 1103515245u + 12345u;
        v.push_back({i, float((s >> 8) % 1000) / 100.0f});
    }
    newlib_qsort(v.data(), v.size(), sizeof(Pair), [](const void* a, const void* b) {
        const float fa = static_cast<const Pair*>(a)->facing, fb = static_cast<const Pair*>(b)->facing;
        return fa < fb ? 1 : (fa > fb ? -1 : 0);
    });
    for (size_t i = 1; i < v.size(); ++i) CHECK(v[i - 1].facing >= v[i].facing);
}

// O AIElementController sobre um 'AIEl' montado à mão: a cópia, os acompanhamentos (200 e 220 m), o RedirectCarpElement,
// a prioridade, o OkToSpawnHench e o CalcMightAsWellBeDead.
TEST(driving_ai_element_controller) {
    mission_reset();
    std::vector<AIElement> recs(3);
    std::memset(recs.data(), 0, recs.size() * sizeof(AIElement));
    for (int i = 0; i < 3; ++i) {
        recs[size_t(i)].type = 1;
        recs[size_t(i)].health = 100.0f;
        recs[size_t(i)].matrix[3][0] = float(i) * 50.0f;
    }
    recs[1].flags = kElemPriority;
    recs[2].flags = kElemMightBeDead;
    recs[2].passengers = 1;
    recs[2].passenger[0].health = 10.0f;
    UData aiel{};
    aiel.tag = 0x4149456cu;
    aiel.count = 3;
    aiel.offset = nf::p32_u(recs.data());
    AIElementController_Construct(&aiel);
    AIElementControllerData* c = ai_controller();
    CHECK(c != nullptr);
    if (!c) return;
    CHECK(c->count == 3 && c->max_characters == 32 && c->max_vehicles == 6 && c->max_helis == 16);
    CHECK(ai_element(1)->flags == kElemPriority);
    CHECK(ai_tracking(0)->wake_range == 200.0f && ai_tracking(0)->sleep_range == 220.0f);
    CHECK(AIElementController_RedirectCarpElement(nf::p32_u(&recs[2])) == ai_element(2));
    CHECK(ai_element_index(nf::p32_u(&recs[1])) == 1);
    CHECK(AIElementController_CompareInRangePrio(*ai_element(0)) == 1);
    CHECK(AIElementController_CompareInRangePrio(*ai_element(1)) == 0);
    CHECK(AIElementController_OkToSpawnHench());
    c->vehicles_wanted = 6;
    CHECK(!AIElementController_OkToSpawnHench());
    AIElementController_CalcMightAsWellBeDead();
    CHECK(AIElementController_GetNumMightAsWellBeDead() == 2);  // o carro e o passageiro vivo
    ai_element(2)->flags = uint16_t(ai_element(2)->flags | kElemNoScore);
    AIElementController_CalcMightAsWellBeDead();
    CHECK(AIElementController_GetNumMightAsWellBeDead() == 0);
    AIElementTracking_SetWakeRange(*ai_tracking(0), 50.0f);
    CHECK(ai_tracking(0)->wake_range == 50.0f && ai_tracking(0)->sleep_range == 70.0f);
    AIElementTracking_SetWakeRange(*ai_tracking(0), -1.0f);  // não positivo: nada
    CHECK(ai_tracking(0)->wake_range == 50.0f);
    AIElementController_Destruct();
    CHECK(ai_controller() == nullptr);
}
