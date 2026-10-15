// A seção do helicóptero das fases de carro (driving/render/path_engine.h, driving/game/rail.h): propriedades
// independentes sobre caminhos montados à mão (sem dados do jogo): as chaves de um canal linear, o slerp de um canal de
// rotação, e o trilho de um carro pelos eventos (o engate, o acelerador, o caminho andando e a soltura; o 43).

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "driving/ai/ground_vehicle.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/game/rail.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/render/path_engine.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

// Um caminho de dois canais com duas chaves (os tempos 0 e `duration`): o 0 de rotação (quatérnios, slerp) e o 1 de
// posição (linear), no formato do CARP (os deslocamentos a partir do começo do PathInfo).
struct PathBuilder {
    std::vector<uint8_t> b;
    PathBuilder(float duration, const float q0[4], const float q1[4], const float p0[4], const float p1[4]) {
        b.assign(0x40 + 2 * 0x14, 0);
        put_f(0x00, duration);
        put_u(0x04, 2);
        put_u(0x0c, 0);  // o canal de rotação
        put_u(0x10, 1);  // o de posição
        const float times[2] = {0.0f, duration};
        for (uint32_t c = 0; c < 2; ++c) {
            const uint32_t base = 0x40 + 0x14 * c;
            const uint32_t name = append(c == 0 ? "xform" : "xlate", c == 0 ? 6 : 6);
            const uint32_t t = append(times, 8);
            const uint32_t k0 = append(c == 0 ? q0 : p0, 16);
            append(c == 0 ? q1 : p1, 16);
            put_u(base + 0x00, name);
            put_u(base + 0x04, t);
            put_u(base + 0x08, k0);
            put_u(base + 0x0c, 2);
            put_u(base + 0x10, c == 0 ? kPathRotation : 0u);
        }
    }
    uint32_t append(const void* src, size_t n) {
        while (b.size() & 15) b.push_back(0);
        const uint32_t at = uint32_t(b.size());
        b.resize(b.size() + n);
        std::memcpy(b.data() + at, src, n);
        return at;
    }
    void put_u(size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
    void put_f(size_t at, float v) { std::memcpy(b.data() + at, &v, 4); }
    const PathInfo* info() const { return reinterpret_cast<const PathInfo*>(b.data()); }
};

const float kIdentityQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
const float kQuarterTurnY[4] = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
const float kOrigin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
const float kTenAlongX[4] = {10.0f, 0.0f, 0.0f, 1.0f};

}  // namespace

TEST(driving_path_linear_channel_hits_its_keys_and_midpoints) {
    const PathBuilder pb(10.0f, kIdentityQuat, kIdentityQuat, kOrigin, kTenAlongX);
    const PathInfo* p = pb.info();
    CHECK(PathInfo_FindChannelIndex(p, "XLATE") == 1);  // (sem distinguir maiúsculas)
    CHECK(PathInfo_FindChannelIndex(p, "xform") == 0);
    float out[4];
    PathInfo_Evaluate(p, 1, 0.0f, 0, out);
    CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f);
    PathInfo_Evaluate(p, 1, 10.0f, 0, out);
    CHECK(out[0] == 10.0f && out[1] == 0.0f && out[2] == 0.0f);
    PathInfo_Evaluate(p, 1, 2.5f, 0, out);
    CHECK(std::fabs(out[0] - 2.5f) < 1e-5f && out[1] == 0.0f && out[2] == 0.0f);
    CHECK(PathInfo_GetKnotIndex(p, 5.0f, 0, p->channel(1)) == 0);
}

TEST(driving_path_rotation_channel_slerps_on_the_unit_sphere) {
    const PathBuilder pb(10.0f, kIdentityQuat, kQuarterTurnY, kOrigin, kTenAlongX);
    const PathInfo* p = pb.info();
    float q[4];
    PathInfo_Evaluate(p, 0, 5.0f, 0, q);
    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    CHECK(std::fabs(len - 1.0f) < 1e-3f);
    // Meio caminho entre 0 e 90 graus em torno de y: 45 graus (o seno e o cosseno de 22,5).
    CHECK(std::fabs(q[0]) < 1e-4f && std::fabs(q[2]) < 1e-4f);
    CHECK(std::fabs(q[1] - 0.38268343f) < 2e-3f && std::fabs(q[3] - 0.92387953f) < 2e-3f);
    // A matriz: a posição do canal 1 na linha 3.
    float m[4][4];
    uint32_t pk = 0, rk = 0;
    float weight = 0.0f;
    PathInfo_EvaluateMatrix(p, 5.0f, pk, rk, m, weight);
    CHECK(std::fabs(m[3][0] - 5.0f) < 1e-4f && std::fabs(m[3][1]) < 1e-4f && std::fabs(m[3][2]) < 1e-4f);
}

TEST(driving_rail_events_engage_drive_and_release_the_player_car) {
    RTimeData_Init(60.0f);
    RPathEngine_Purge();
    simulation_reset(60);
    std::unique_ptr<BondCar> owned(new BondCar());
    BondCar& car = *owned;
    static AIGroundVehicle v;
    AIGroundVehicle_Construct(v);
    v.physics = nf::p32_u(&car.d);
    v.car = &car;
    v.controller = 1;
    car.d.ai_ground_vehicle = nf::p32_u(&v);
    car.d.control.gas = 0.7f;
    simulation().cars.push_back(&car);

    const PathBuilder pb(10.0f, kIdentityQuat, kIdentityQuat, kOrigin, kTenAlongX);
    static AISplineRec rail{};
    rail = AISplineRec{};
    for (int i = 0; i < 4; ++i) rail.matrix[i][i] = 1.0f;
    rail.path = nf::p32_u(pb.info());
    std::strcpy(rail.name, "rail");
    rail.disabled = 1;  // a colocação não vale: a matriz de aplicação é a identidade
    static AISplineRec no_path{};
    no_path = rail;
    no_path.path = 0;

    // Um 'AISp' sem caminho e um elemento da IA não engatam o carro do jogador.
    PendingEvent e;
    e.type = 202;
    e.obj[2] = nf::p32_u(&no_path);
    CHECK(rail_event_execute(e));
    CHECK(v.controller == 1 && v.spline_path == 0);
    e.obj[1] = 7;
    e.obj[2] = nf::p32_u(&rail);
    CHECK(rail_event_execute(e));
    CHECK(v.controller == 1 && v.spline_path == 0);
    // O engate: a CPU, os controles zerados, o estado 3, o trilho com o seu RPathHandle na lista.
    e.obj[1] = 0;
    CHECK(rail_event_execute(e));
    CHECK(v.controller == 3 && car.d.car_class == 1 && car.d.control.gas == 0.0f);
    CHECK(v.spline_path != 0 && path_engine_handles().size() == 1);
    const AISplinePath* sp = nf::p32_ptr<const AISplinePath>(v.spline_path);
    RPathHandle* h = nf::p32_ptr<RPathHandle>(sp->handle);
    CHECK(h != nullptr);
    if (!h) return;
    // O acelerador desejado pelo 116 (a palavra 3, um float) e o caminho andando ao longo de x (a linha 3 da instância:
    // a matriz de eixos do RPathHandle não mexe na posição), sem chegar ao fim (lá ele volta ao começo do caminho).
    PendingEvent t;
    t.type = 116;
    t.obj[3] = ps2::fbits(0.5f);
    CHECK(rail_event_execute(t));
    CHECK(h->desired == 0.5f);
    float last = RPathHandle_GetPosition(*h)[0];
    bool moved = false, monotonic = true;
    for (int s = 1; s <= 30; ++s) {
        simulation().steps = s;
        RPathEngine_Update(ps2::ee::cvt_s(s));
        const float* pos = RPathHandle_GetPosition(*h);
        if (pos[0] < last - 1e-5f) monotonic = false;
        if (pos[0] > last) moved = true;
        CHECK(std::fabs(pos[1]) < 1e-4f && std::fabs(pos[2]) < 1e-4f);
        CHECK(pos[0] >= -1e-4f && pos[0] <= 10.0f + 1e-4f);
        last = pos[0];
    }
    CHECK(moved && monotonic);
    // A soltura: o jogador de volta, o trilho e o RPathHandle destruídos.
    PendingEvent d;
    d.type = 239;
    CHECK(rail_event_execute(d));
    CHECK(v.controller == 1 && car.d.car_class == 0 && v.spline_path == 0);
    CHECK(path_engine_handles().empty());
    // O 43 (EControlToPlayer): o jogador de volta, o acelerador e o freio pedidos a 0, o IOModule mandando as ações.
    car.d.target_gas = 0.4f;
    car.d.target_brake = 0.2f;
    car.d.car_class = 1;
    v.controller = 2;
    mission_env().io_updating = 0;
    PendingEvent back;
    back.type = 43;
    CHECK(rail_event_execute(back));
    CHECK(v.controller == 1 && car.d.car_class == 0 && car.d.target_gas == 0.0f && car.d.target_brake == 0.0f);
    CHECK(mission_env().io_updating == 1);
    mission_env().io_updating = 0;
    simulation().cars.clear();
    RPathEngine_Purge();
}
