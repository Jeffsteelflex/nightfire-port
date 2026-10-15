// Os elementos da IA (driving/ai/element_controller.h), portados do MIPS do DRIVING.ELF (AIElementController.obj e os
// eventos de DTuningFile.obj). As desmontagens ficam em out/agent_drv_ai/ do checkout principal (fora do git).
//
// Divergências (port), cada uma também na função:
//  - os personagens e os helicópteros não são criados (WakeUp, contados); o GoToSleep e o EKillAIElement dos que o PS2
//    teria criado não acham nada;
//  - o carro vem da fábrica de quem monta os carros (ai_car_factory: o modelo e o render), e o Simulation::DeleteCarObject
//    o tira da lista e da vaga do corpo na hora (o PS2 o apaga no próximo ESimFrameUpdate) e o entrega à fábrica;
//  - o construtor: a troca dos "paris_hench" do snow2a_mis4, o RSceneObj::PreLoad dos veículos e o
//    ActCharacter::LoadCharacter dos personagens ficam de fora (contados);
//  - a memória que o operator new do PS2 entrega tem o que estava lá; aqui começa zerada (os campos que o construtor não
//    escreve: os acompanhamentos +0x1c..+0x2f, +0x30.., as listas);
//  - guardas (contadas): um registro fora do 'AIEl' em RedirectCarpElement/GetCarpElementTracking, o carro do jogador
//    ausente no Update, a câmera ausente no UpdatePosition.

#include "driving/ai/element_controller.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "driving/ai/ai_vu.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_nav.h"
#include "driving/ai/road_spawn.h"
#include "driving/anim/act_actor.h"
#include "driving/game/ai_passengers.h"
#include "driving/game/bond_car.h"
#include "driving/game/events.h"
#include "driving/game/mission.h"
#include "driving/game/smackable.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "platform/ptr32.h"

namespace drv {

using namespace aivu;

namespace {

// A memória do controlador (o PS2: __builtin_new(0x58) e os operator new das listas).
struct ControllerStore {
    AIElementControllerData ctl{};
    std::vector<AIElement> elements;
    std::vector<AIElementTracking> trackings;
    std::vector<int32_t> wake_list;
    std::vector<int32_t> pair_list;  // 2 palavras por par: o índice e o float do "à frente"
};
std::unique_ptr<ControllerStore> g_store;
AICarFactory g_factory;
AIElementStats g_stats;
std::map<std::string, int> g_pending;

constexpr float kFar = 1000000.0f;  // 0x49742400

Simulation& sim() { return simulation(); }

BondCar* car_from_p32(uint32_t p) {
    if (p == 0) return nullptr;
    for (BondCar* c : sim().cars)
        if (nf::p32_u(&c->d) == p) return c;
    return nullptr;
}

// PhysicsObject::GetHitPoints (0x195a30): o float do +0x54, ou 1.
float hit_points_of(uint32_t physics) {
    BondCar* c = car_from_p32(physics);
    if (!c) {
        ++g_stats.guards;
        return 1.0f;
    }
    return c->hit_points();
}

RigidBody* body_of_car(BondCar& c) { return GetRigidBody(sim(), c.d.slot); }

// O carro do jogador: o primeiro da lista da Simulation (+0xdcc).
BondCar* player_car() { return sim().cars.empty() ? nullptr : sim().cars.front(); }

// SMissionManager::ShowMissionText (0x2084a8; o nome do SYM está trocado): o fator da vida dos inimigos pela
// dificuldade, as configurações +0x2c4 (0 e as outras), +0x2c8 (1), +0x2cc (2).
float health_factor() {
    if (!mission_exists()) return 1.0f;
    SMissionManagerData& m = mission_manager();
    const MissionSettings* s = nf::p32_ptr<const MissionSettings>(m.settings);
    if (!s) {
        ++g_stats.guards;
        return 1.0f;
    }
    if (m.difficulty == 1) return s->scale_b[1];
    if (m.difficulty < 2) return s->scale_b[0];
    if (m.difficulty == 2) return s->scale_b[2];
    return s->scale_b[0];
}

AIElementTracking* tracking_of(const AIElement& e) {
    if (!g_store) return nullptr;
    const ptrdiff_t i = &e - g_store->elements.data();
    if (i < 0 || i >= ptrdiff_t(g_store->elements.size())) return nullptr;
    return &g_store->trackings[size_t(i)];
}

// AIVehicle::GetRotPos (0x13e120): a matriz do carro (VU0_quattom4 do quatérnio e a posição) no +0x10.
const float (*vehicle_rot_pos(AIGroundVehicle& v))[4] {
    if (v.physics != 0) {
        BondCar* c = car_from_p32(v.physics);
        if (!c) {
            ++g_stats.guards;
            return v.rot_pos;
        }
        if (c->d.flags & 1) {
            ai_element_pending("AIVehicle::GetRotPos (SimpleRigidBody)");
            return v.rot_pos;
        }
        RigidBody* b = body_of_car(*c);
        // Float_COORD3toCOORD4 (w = 1), VU0_quattom4 (a rotação; as linhas 3 da identidade), 0x25ae70 (a translação).
        float m[4][4];
        VU0_quattom4(b->quat, m);
        std::memcpy(v.rot_pos, m, sizeof m);
        v.rot_pos[3][0] = b->pos[0];
        v.rot_pos[3][1] = b->pos[1];
        v.rot_pos[3][2] = b->pos[2];
        v.rot_pos[3][3] = 1.0f;
    }
    return v.rot_pos;
}

// O PendingEvent de um evento criado pelo código (a imagem do objeto do PS2: a tabela virtual e os campos).
void push_event(int32_t type, std::initializer_list<uint32_t> words) {
    PendingEvent e{type, 0, 0.0f, 0, 0, 0};
    if (const EventInfo* info = event_info(type)) {
        e.size = info->size;
        e.obj[0] = info->vtable;
    }
    uint32_t k = 1;
    for (uint32_t w : words) e.obj[k++] = w;
    e.written = (1u << k) - 1u;
    ++event_stats().created[type];
    trigger_manager().pending.push_back(e);
}

}  // namespace

AIElementStats& ai_element_stats() { return g_stats; }
void ai_element_pending(const char* name) { ++g_pending[name]; }
int ai_element_pending_count(const char* name) {
    auto it = g_pending.find(name);
    return it == g_pending.end() ? 0 : it->second;
}
AICarFactory& ai_car_factory() { return g_factory; }

AIElementControllerData* ai_controller() { return g_store ? &g_store->ctl : nullptr; }
AIElement* ai_element(int i) {
    if (!g_store || i < 0 || i >= int(g_store->elements.size())) return nullptr;
    return &g_store->elements[size_t(i)];
}
AIElementTracking* ai_tracking(int i) {
    if (!g_store || i < 0 || i >= int(g_store->trackings.size())) return nullptr;
    return &g_store->trackings[size_t(i)];
}
int ai_element_index(uint32_t carp_element) {
    if (!g_store || carp_element < g_store->ctl.carp) return -1;
    const uint32_t off = carp_element - g_store->ctl.carp;
    if (off % 0x100 != 0 || off / 0x100 >= uint32_t(g_store->ctl.count)) return -1;
    return int(off / 0x100);
}

// ------------------------------------------------------------------------------------------------- o qsort do newlib

namespace {
void swap_bytes(uint8_t* a, uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) std::swap(a[i], b[i]);
}
uint8_t* med3(uint8_t* a, uint8_t* b, uint8_t* c, int (*cmp)(const void*, const void*)) {
    return cmp(a, b) < 0 ? (cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a))
                         : (cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c));
}
}  // namespace

// [driving] 0x002a8198 qsort (o do newlib, o do BSD 4.4: med3 e med3 de med3 acima de 40, a partição em três, a
// inserção abaixo de 7 elementos e sem trocas; recursão no menor lado).
void newlib_qsort(void* base, size_t n, size_t es, int (*cmp)(const void*, const void*)) {
    uint8_t* a = static_cast<uint8_t*>(base);
    for (;;) {
        bool swapped = false;
        if (n < 7) {
            for (uint8_t* pm = a + es; pm < a + n * es; pm += es)
                for (uint8_t* pl = pm; pl > a && cmp(pl - es, pl) > 0; pl -= es) swap_bytes(pl, pl - es, es);
            return;
        }
        uint8_t* pm = a + (n / 2) * es;
        if (n > 7) {
            uint8_t* pl = a;
            uint8_t* pn = a + (n - 1) * es;
            if (n > 40) {
                const size_t d = (n / 8) * es;
                pl = med3(pl, pl + d, pl + 2 * d, cmp);
                pm = med3(pm - d, pm, pm + d, cmp);
                pn = med3(pn - 2 * d, pn - d, pn, cmp);
            }
            pm = med3(pl, pm, pn, cmp);
        }
        swap_bytes(a, pm, es);
        uint8_t *pa = a + es, *pb = a + es;
        uint8_t *pc = a + (n - 1) * es, *pd = a + (n - 1) * es;
        for (;;) {
            int r;
            while (pb <= pc && (r = cmp(pb, a)) <= 0) {
                if (r == 0) {
                    swapped = true;
                    swap_bytes(pa, pb, es);
                    pa += es;
                }
                pb += es;
            }
            while (pb <= pc && (r = cmp(pc, a)) >= 0) {
                if (r == 0) {
                    swapped = true;
                    swap_bytes(pc, pd, es);
                    pd -= es;
                }
                pc -= es;
            }
            if (pb > pc) break;
            swap_bytes(pb, pc, es);
            swapped = true;
            pb += es;
            pc -= es;
        }
        if (!swapped) {
            for (uint8_t* p = a + es; p < a + n * es; p += es)
                for (uint8_t* pl = p; pl > a && cmp(pl - es, pl) > 0; pl -= es) swap_bytes(pl, pl - es, es);
            return;
        }
        uint8_t* pn = a + n * es;
        ptrdiff_t r = std::min(pa - a, pb - pa);
        if (r > 0) swap_bytes(a, pb - r, size_t(r));
        r = std::min(pd - pc, pn - pd - ptrdiff_t(es));
        if (r > 0) swap_bytes(pb, pn - r, size_t(r));
        if ((r = pb - pa) > ptrdiff_t(es)) newlib_qsort(a, size_t(r) / es, es, cmp);
        if ((r = pd - pc) > ptrdiff_t(es)) {
            a = pn - r;
            n = size_t(r) / es;
            continue;
        }
        return;
    }
}

namespace {
// [driving] 0x0012dbf0 (a comparação do Update): os pares pelo "à frente" decrescente (-1 salvo a < b).
int compare_facing(const void* a, const void* b) {
    float fa, fb;
    std::memcpy(&fa, static_cast<const uint8_t*>(a) + 4, 4);
    std::memcpy(&fb, static_cast<const uint8_t*>(b) + 4, 4);
    return ee::lt(fa, fb) ? 1 : -1;
}
}  // namespace

// ------------------------------------------------------------------------------------------------- o acompanhamento

// [driving] 0x0012af48 AIElementTracking::AIElementTracking
void AIElementTracking_Construct(AIElementTracking& t, uint32_t element, uint32_t controller) {
    t.next_update = sim().steps;
    t.period = 1;
    t.wake_range = 200.0f;
    t.sleep_range = 220.0f;
    t.element = element;
    t.controller = controller;
    t.distance = 0.0f;
    t.facing = 0.0f;
    t.in_range = 0;
}

// [driving] 0x0012b100 AIElementTracking::SetWakeRange
void AIElementTracking_SetWakeRange(AIElementTracking& t, float range) {
    if (!ee::lt(0.0f, range)) return;
    t.wake_range = range;
    t.sleep_range = ee::add(range, 20.0f);
}

// [driving] 0x0012b440 AIElementTracking::UpdatePosition
// Divergência: a posição de um personagem a pé acordado (o AICharacter +0x30) não existe no port (não são criados).
void AIElementTracking_UpdatePosition(AIElementTracking& t, const float player[4]) {
    AIElement& e = *nf::p32_ptr<AIElement>(t.element);
    if (e.state == 1) {
        if (e.type == 0) {
            ai_element_pending("AIElementTracking::UpdatePosition (personagem)");
        } else if (e.type == 1) {
            AIGroundVehicle* v = nf::p32_ptr<AIGroundVehicle>(e.ai);
            if (v) aivu::copy3(t.pos, AIVehicle_GetPosition(*v));
            else ++g_stats.guards;
        }
    } else {
        t.pos[0] = e.matrix[3][0];
        t.pos[1] = e.matrix[3][1];
        t.pos[2] = e.matrix[3][2];
    }
    const float d = dist(t.pos, player);
    t.distance = d;
    // O "à frente": a linha 2 da matriz do segundo objeto da lista de câmeras do mundo (0x333ed8 [1] +0x20).
    float fwd[3] = {0, 0, 0};
    if (traffic_world().camera) aivu::copy3(fwd, traffic_world().camera[2]);
    else ++g_stats.guards;
    float to[3], u_to[3], u_fwd[3];
    sub3(to, t.pos, player);
    unit(u_to, to);
    unit(u_fwd, fwd);
    const float half = ee::mul(ee::add(aivu::dot(u_fwd, u_to), 1.0f), 0.5f);
    if (ee::lt(0.0f, d)) {
        const float f = ee::mul(ee::div(1.0f, d), half);
        t.facing = ee::lt(f, 0.0f) ? 0.0f : f;
    } else {
        t.facing = 1.0f;
    }
    if (e.state != 1) {
        t.speed = 0.0f;
    } else if (e.type == 0) {
        ai_element_pending("AIElementTracking::UpdatePosition (velocidade do personagem)");
    } else if (e.type == 1) {
        BondCar* c = car_from_p32(e.physics);
        if (c) t.speed = len(body_of_car(*c)->vel);  // PhysicsObject::GetLinearVelocity (0x195858)
        else ++g_stats.guards;
    }
    const float prev = t.health;
    t.health = e.health;
    const float drop = ee::sub(prev, e.health);
    if (e.state != 1) return;
    if (ee::lt(0.0f, e.health)) {
        AIElementControllerData& c = *nf::p32_ptr<AIElementControllerData>(t.controller);
        if (ee::lt(c.stats[0], e.health)) c.stats[0] = e.health;
        if (ee::lt(e.health, c.stats[1])) c.stats[1] = e.health;
        if (ee::lt(c.stats[2], drop)) c.stats[2] = drop;
        if (ee::lt(c.stats[3], t.speed)) c.stats[3] = t.speed;
        if (ee::lt(t.speed, c.stats[4])) c.stats[4] = t.speed;
        if (ee::lt(c.stats[5], t.distance)) c.stats[5] = t.distance;
        if (ee::lt(t.distance, c.stats[6])) c.stats[6] = t.distance;
    }
    if (e.type != 1 || e.heli != 0) return;
    BondCar* car = car_from_p32(e.physics);
    if (!car) {
        ++g_stats.guards;
        return;
    }
    // Menos de 4 pneus (GetNumTires, +0x13c), ou o motorista no óleo (+0x16c) ou na fumaça (+0x168): pode estar morto.
    int tyres = 0;
    for (int i = 0; i < 4; ++i) tyres += car->d.tyre_damage_points[i] != 0;
    if (tyres < 4) e.flags = uint16_t(e.flags | kElemMightBeDead);
    AIGroundVehicle* v = ai_of(*car);
    if (!v) return;
    if (v->been_in_oil != 0) e.flags = uint16_t(e.flags | kElemMightBeDead);
    if (v->been_in_smoke != 0) e.flags = uint16_t(e.flags | kElemMightBeDead);
}

// [driving] 0x0012afb8 AIElementTracking::Update
void AIElementTracking_Update(AIElementTracking& t, const float player[4]) {
    const int32_t steps = sim().steps;
    AIElement& e = *nf::p32_ptr<AIElement>(t.element);
    if (e.state == 2) return;
    if (steps < t.next_update) return;
    bool sleep = false;
    AIElementTracking_UpdatePosition(t, player);
    if (ee::lt(t.distance, t.wake_range)) {
        t.period = 1;  // SetUpdateEveryFrame (0x12b0d0)
        t.in_range = 1;
    } else {
        if (t.period < 4) t.period += 1;  // SetUpdateLowerFrequency (0x12b0e0)
        t.in_range = 0;
        if (e.state == 1 && ee::lt(t.sleep_range, t.distance)) sleep = true;
    }
    if ((e.flags & kElemInactive) && e.state == 1) sleep = true;
    if (sleep && AIElementController_CompareInRangePrio(e) != 0) AIElementController_GoToSleep(e);
    t.next_update = steps + t.period;
}

// [driving] 0x0012b138 AIElementTracking::GetProperty
bool AIElementTracking_GetProperty(AIElementTracking& t, const char* name) {
    AIElement& e = *nf::p32_ptr<AIElement>(t.element);
    if (std::strcmp(name, "EMPTY") == 0) {
        if (e.type != 1) return false;
        float sum = 0.0f;
        for (int i = 0; i < e.passengers; ++i) sum = ee::add(sum, e.passenger[i].health);
        return ee::eq(sum, 0.0f);
    }
    if (std::strcmp(name, "BLOWOUT") == 0) {
        if (e.type != 1 || e.state != 1 || e.heli != 0) return false;
        BondCar* car = car_from_p32(e.physics);
        if (!car) {
            ++g_stats.guards;
            return false;
        }
        int tyres = 0;
        for (int i = 0; i < 4; ++i) tyres += car->d.tyre_damage_points[i] != 0;
        if (3 < tyres) return false;
        if (e.passengers <= 0) return false;
        for (int i = 0; i < e.passengers; ++i)
            if (!ee::lt(0.0f, e.passenger[i].health)) return false;
        return true;
    }
    const bool smoked = std::strcmp(name, "SMOKED") == 0;
    if (smoked || std::strcmp(name, "OILED") == 0) {
        if (e.type != 1 || e.heli != 0 || e.state != 1) return false;
        BondCar* car = car_from_p32(e.physics);
        if (!car) return false;
        AIGroundVehicle* v = ai_of(*car);
        if (!v) return false;
        return (smoked ? v->been_in_smoke : v->been_in_oil) != 0;
    }
    if (std::strcmp(name, "AWAKE") == 0) {
        if (e.state != 1) return false;
        if (e.type == 0) {
            ai_element_pending("AIElementTracking::GetProperty (AWAKE, personagem)");
            return false;
        }
        if (e.type != 1) return true;
        return ee::lt(0.0f, hit_points_of(e.physics));
    }
    return false;
}

// ------------------------------------------------------------------------------------------------- o controlador

// [driving] 0x0012be48 AIElementController::Construct e 0x0012b8b0 AIElementController::AIElementController
void AIElementController_Construct(UData* aiel, const std::function<int32_t(const char*, bool*)>& limits) {
    g_store = std::make_unique<ControllerStore>();
    ControllerStore& s = *g_store;
    AIElementControllerData& c = s.ctl;
    const int32_t n = aiel ? int32_t(aiel->count) : 0;
    c.count = n;
    s.elements.assign(size_t(n), AIElement{});
    s.wake_list.assign(size_t(n), 0);
    s.pair_list.assign(size_t(n) * 2, 0);
    s.trackings.assign(size_t(n), AIElementTracking{});
    c.elements = nf::p32_u(s.elements.data());
    c.wake_list = nf::p32_u(s.wake_list.data());
    c.pair_list = nf::p32_u(s.pair_list.data());
    c.trackings = nf::p32_u(s.trackings.data());
    c.trackings_alloc = c.trackings;
    c.carp = aiel ? nf::p32_u(UData_Data(aiel)) : 0;
    if (n) std::memcpy(s.elements.data(), UData_Data(aiel), size_t(n) * 0x100);
    // O snow2a_mis4 na dificuldade 2 troca os veículos "paris_hench..." por outro (sem passageiros): não portado.
    if (std::strcmp(uber_bond().text(0x4f4), "snow2a_mis4") == 0) ai_element_pending("AIElementController (snow2a_mis4)");
    for (int i = 0; i < n; ++i) {
        AIElement& e = s.elements[size_t(i)];
        AIElementTracking_Construct(s.trackings[size_t(i)], nf::p32_u(&e), nf::p32_u(&c));
        if (e.type == 1) ai_element_pending("RSceneObj::PreLoad (o veículo do elemento)");
        // ActCharacter::LoadCharacter (com o ActActorDatabase 0x326000): os passageiros (os modelos e as armas da tabela
        // 0x327ba8/0x327b90; docs/driving.md seção 26); os personagens a pé não foram portados (contados).
        if (anim::act_globals().database != 0) {
            if (e.type == 0) ai_element_pending("ActCharacter::LoadCharacter");
            else if (e.type == 1)
                for (int k = 0; k < e.passengers; ++k)
                    AIElementController_LoadPassenger(nf::p32_u(&e), k);
        }
        if (e.type == 1)
            for (int k = 0; k < e.passengers; ++k)
                e.passenger[k].health = ee::mul(e.passenger[k].health, health_factor());
        if (e.type == 0 || e.type == 1) e.health = ee::mul(e.health, health_factor());
    }
    auto lookup = [&](const char* key, int32_t def) {
        bool found = false;
        const int32_t v = limits ? limits(key, &found) : 0;
        return found ? v : def;
    };
    c.max_characters = lookup("HENCH_SLOTS_AVAIL", 0x20);
    c.max_vehicles = lookup("VEHICLE_SLOTS_AVAIL", 6);
    c.max_helis = lookup("HELI_SLOTS_AVAIL", 0x10);
    c.characters_awake = c.vehicles_awake = c.helis_awake = c.vehicles_wanted = c.might_be_dead = 0;
}

BondCar* ai_car_from_physics(uint32_t physics) { return car_from_p32(physics); }

// [driving] 0x0012be90 AIElementController::Destruct e 0x0012bd70 o destrutor
void AIElementController_Destruct() { g_store.reset(); }

// [driving] 0x0012dc18 AIElementController::CompareInRangePrio
int32_t AIElementController_CompareInRangePrio(const AIElement& e) { return ((e.flags >> 2) ^ 1) & 1; }

// [driving] 0x0012c608 AIElementController::RedirectCarpElement
AIElement* AIElementController_RedirectCarpElement(uint32_t carp_element) {
    if (!g_store) {
        ++g_stats.guards;
        return nullptr;
    }
    int32_t i = int32_t(carp_element - g_store->ctl.carp) >> 8;
    if (i < 0) i = -i;
    if (i >= g_store->ctl.count) {
        ++g_stats.guards;  // o PS2 leria fora da cópia
        return nullptr;
    }
    return &g_store->elements[size_t(i)];
}

// [driving] 0x0012c640 AIElementController::GetCarpElementTracking
AIElementTracking* AIElementController_GetCarpElementTracking(uint32_t carp_element) {
    if (!g_store) {
        ++g_stats.guards;
        return nullptr;
    }
    int32_t i = int32_t(carp_element - g_store->ctl.carp) >> 8;
    if (i < 0) i = -i;
    if (i >= g_store->ctl.count) {
        ++g_stats.guards;
        return nullptr;
    }
    return &g_store->trackings[size_t(i)];
}

// [driving] 0x0012c5d0 AIElementController::OkToSpawnHench
bool AIElementController_OkToSpawnHench() { return g_store && g_store->ctl.vehicles_wanted < g_store->ctl.max_vehicles; }

// [driving] 0x0012bec8 AIElementController::Update
void AIElementController_Update() {
    if (!g_store) return;
    ControllerStore& s = *g_store;
    AIElementControllerData& c = s.ctl;
    ++g_stats.updates;
    c.stats[0] = 0.0f;
    c.stats[6] = kFar;
    c.stats[1] = kFar;
    c.stats[2] = 0.0f;
    c.stats[3] = 0.0f;
    c.stats[4] = kFar;
    c.stats[5] = 0.0f;
    // A posição do carro do jogador (o w é o que estava na pilha: ninguém o lê).
    alignas(16) float player[4] = {0, 0, 0, 0};
    if (BondCar* p = player_car()) aivu::copy3(player, body_of_car(*p)->pos);
    else ++g_stats.guards;
    int32_t want_chars = 0, want_vehicles = 0, want_helis = 0, n_wake = 0, n_pairs = 0;
    for (int32_t i = 0; i < c.count; ++i) {
        AIElementTracking& t = s.trackings[size_t(i)];
        AIElementTracking_Update(t, player);
        t.wake = 0;
        AIElement& e = s.elements[size_t(i)];
        if (t.in_range == 0 && AIElementController_CompareInRangePrio(e) != 0) continue;
        if (e.state == 0) {
            if (e.flags & kElemInactive) continue;
            if (e.type == 0) {
                ++want_chars;
            } else if (e.type == 1) {
                if (e.heli == 0) ++want_vehicles;
                else ++want_helis;
                want_chars += e.passengers;
            }
            s.wake_list[size_t(n_wake++)] = i;
        }
        if (e.flags & kElemInactive) continue;
        s.pair_list[size_t(n_pairs) * 2] = i;
        std::memcpy(&s.pair_list[size_t(n_pairs) * 2 + 1], &t.facing, 4);
        ++n_pairs;
    }
    c.vehicles_wanted = want_vehicles + c.vehicles_awake;
    if (0 < want_chars + want_vehicles + want_helis) {
        int32_t free_chars = c.max_characters - c.characters_awake;
        int32_t free_vehicles = c.max_vehicles - c.vehicles_awake;
        int32_t free_helis = c.max_helis - c.helis_awake;
        if (!(free_chars < want_chars) && !(free_vehicles < want_vehicles) && !(free_helis < want_helis)) {
            // Todos cabem: acorda os da lista.
            for (int32_t k = 0; k < n_wake; ++k) AIElementController_WakeUp(s.elements[size_t(s.wake_list[size_t(k)])]);
        } else {
            newlib_qsort(s.pair_list.data(), size_t(n_pairs), 8, compare_facing);
            free_chars = c.max_characters;
            free_vehicles = c.max_vehicles;
            free_helis = c.max_helis;
            // Primeiro os de prioridade, que acordam sempre.
            for (int32_t k = 0; k < n_pairs; ++k) {
                const int32_t i = s.pair_list[size_t(k) * 2];
                AIElement& e = s.elements[size_t(i)];
                if (AIElementController_CompareInRangePrio(e) != 0) continue;
                if (e.type == 0) {
                    --free_chars;
                } else if (e.type == 1) {
                    if (e.heli == 0) {
                        --free_vehicles;
                        free_chars -= e.passengers;
                    } else {
                        --free_helis;
                    }
                } else {
                    continue;
                }
                s.trackings[size_t(i)].wake = 1;
            }
            // Depois os outros, na ordem, enquanto cabem.
            for (int32_t k = 0; k < n_pairs; ++k) {
                const int32_t i = s.pair_list[size_t(k) * 2];
                AIElement& e = s.elements[size_t(i)];
                if (AIElementController_CompareInRangePrio(e) == 0) continue;
                if (e.type == 0) {
                    if (0 < free_chars) {
                        --free_chars;
                        s.trackings[size_t(i)].wake = 1;
                    }
                } else if (e.type == 1) {
                    if (e.heli == 0) {
                        if (0 < free_vehicles && e.passengers <= free_chars) {
                            free_chars -= e.passengers;
                            --free_vehicles;
                            s.trackings[size_t(i)].wake = e.type;
                        }
                    } else if (0 < free_helis) {
                        --free_helis;
                        s.trackings[size_t(i)].wake = e.type;
                    }
                }
            }
            for (int32_t i = 0; i < c.count; ++i)
                if (s.elements[size_t(i)].state == 1 && s.trackings[size_t(i)].wake == 0)
                    AIElementController_GoToSleep(s.elements[size_t(i)]);
            for (int32_t i = 0; i < c.count; ++i)
                if (s.elements[size_t(i)].state == 0 && s.trackings[size_t(i)].wake != 0)
                    AIElementController_WakeUp(s.elements[size_t(i)]);
        }
    }
    if (mission_exists()) {
        if (ee::lt(c.stats[6], 120.0f)) SMissionManager_ClearProgrammerEvent(mission_manager(), 1);
        else SMissionManager_ProgrammerDefinedEvent(mission_manager(), 1);  // (com a cadeia 0x36a620, que só registra)
    }
}

// Simulation::SpawnCarObject (0x1ff850): com lugar (CanSpawnRigidBody(pos, 1)), o PBondCar da fábrica (que o põe na
// lista da Simulation, o SpawnPhysicsObject).
BondCar* Simulation_SpawnCarObject(int32_t car_class, const char* name, uint8_t colour, const float dir[3],
                                   const float pos[3]) {
    if (!CanSpawnRigidBody(sim(), pos, true)) return nullptr;
    if (!g_factory.spawn) {
        ++g_stats.no_factory;
        return nullptr;
    }
    return g_factory.spawn(car_class, name, colour, dir, pos);
}

// Simulation::DeleteCarObject (0x201980) e DeletePhysicsObject (0x200f18): o carro sai da lista e da vaga do corpo;
// a fábrica o recebe (o render), e o objeto é apagado no próximo ESimFrameUpdate (DeleteDeadObjects).
void Simulation_DeleteCarObject(BondCar* car) {
    if (!car) return;
    Simulation& s = sim();
    auto it = std::find(s.cars.begin(), s.cars.end(), car);
    if (it != s.cars.end()) s.cars.erase(it);
    if (car->d.slot >= 0 && car->d.slot < kRigidBodySlots && s.owners[car->d.slot] == car) {
        RigidBody* rb = GetRigidBody(s, car->d.slot);
        if (rb->info) RigidBody_Destruct(rb);
        s.owners[car->d.slot] = nullptr;
    }
    if (g_factory.remove) g_factory.remove(car);
    s.dead.push_back(car);
}

// [driving] 0x0012cb08 AIElementController::WakeUp
// Divergências: os personagens (a pé e os passageiros) e os helicópteros não são criados (contados); o carro vem da
// fábrica (ai_car_factory).
void AIElementController_WakeUp(AIElement& e) {
    if (e.state != 0 || !g_store) return;
    AIElementControllerData& c = g_store->ctl;
    const uint32_t difficulty = mission_exists() ? uint32_t(mission_manager().difficulty) : 0u;
    switch (e.difficulty) {
        case 1: if (1 < difficulty - 1) return; break;
        case 2: if (difficulty != 2) return; break;
        case 3: if (difficulty != 0) return; break;
        case 4: if (difficulty != 1) return; break;
        case 5: if (1 < difficulty) return; break;
        case 6: if (difficulty != 0 && difficulty != 2) return; break;
        default: break;
    }
    if (e.type == 0) {
        // O personagem a pé (AICharacterEnemyGround e o Human) e o seu EnemyCount: não portados.
        ++g_stats.characters;
        ai_element_pending("AIElementController::WakeUp (personagem a pé)");
        return;
    }
    if (e.type != 1) return;
    alignas(16) float dir[4] = {e.matrix[2][0], e.matrix[2][1], e.matrix[2][2], 0};
    alignas(16) float pos[4] = {e.matrix[3][0], e.matrix[3][1], e.matrix[3][2], 0};
    if (e.heli == 0) {
        // Os carros do tráfego perto do ponto somem (UnSpawn).
        for (BondCar* other : std::vector<BondCar*>(sim().cars)) {
            if (other->d.car_class != 3) continue;
            RigidBody* b = body_of_car(*other);
            const float d = dist(b->pos, pos);
            if (ee::le(d, ee::add(b->info->half_size[2], 6.0f))) {
                if (AIGroundVehicle* v = ai_of(*other)) AIGroundVehicle_UnSpawn(*v);
                else ++g_stats.guards;  // o PS2 chamaria o UnSpawn do endereço 0
            }
        }
        const int32_t car_class = (e.flags & kElemAgent) ? 2 : 4;
        BondCar* car = Simulation_SpawnCarObject(car_class, e.name, e.colour, dir, pos);
        if (!car) return;
        ++g_stats.cars_spawned;
        PBondCar_ResetCar(*car, pos, dir);  // a entrada +0x164
        AIVehicleController_RegisterSimCar(*car);
        car->d.hit_point_loc = nf::p32_u(&e.health);  // PhysicsObject::SetHitPointLoc (o ValidateHitPoints é vazio)
        if (e.flags & kElemNoScore) car->d.scoreable = 0;  // SetScoreable(0) (+0x16c)
        e.physics = nf::p32_u(&car->d);
        e.ai = car->d.ai_ground_vehicle;  // GetAIGroundVehiclePtr (+0x5c)
        e.state = 1;
        if (e.command != 0) EAICommand_Create(e.command, 0, 0, e.ai, 0, 0, uint32_t(car->d.slot));
        ++c.vehicles_awake;
    } else {
        ++g_stats.helis;
        ai_element_pending("AIElementController::WakeUp (helicóptero)");
        return;
    }
    ++g_stats.wakes;
    // O número de inimigos da missão (o +0x3dc do gerente): uma vez por elemento que pontua, mais os passageiros.
    if (!(e.flags & kElemCounted) && !(e.flags & kElemNoScore) && mission_exists()) {
        SMissionManagerData& m = mission_manager();
        m.unknown3d8[1] += 1;
        for (int k = 0; k < e.passengers; ++k) m.unknown3d8[1] += 1;
    }
    e.flags = uint16_t(e.flags | kElemCounted);
    // Os passageiros (driving/game/ai_passengers.h: o AICharacterEnemyWindow, o Sunroof e o Driver, com o Human de cada
    // um), com o banco dos atores (0x326000).
    if (anim::act_globals().database == 0) return;
    BondCar* car = car_from_p32(e.physics);
    if (!car) return;  // (o helicóptero: não portado)
    g_stats.characters += uint64_t(AIElementController_WakeUpPassengers(nf::p32_u(&e), c.characters_awake, *car));
}

// [driving] 0x0012c740 AIElementController::GoToSleep
void AIElementController_GoToSleep(AIElement& e) {
    if (e.state != 1 || !g_store) return;
    AIElementControllerData& c = g_store->ctl;
    ++g_stats.sleeps;
    if (e.type == 0) {
        ai_element_pending("AIElementController::GoToSleep (personagem)");
        return;
    }
    if (e.type != 1) return;
    AIGroundVehicle* v = nf::p32_ptr<AIGroundVehicle>(e.ai);
    if (!(e.flags & kElemFixedPose)) {
        if (v) std::memcpy(e.matrix, vehicle_rot_pos(*v), sizeof e.matrix);
        else ++g_stats.guards;
    }
    bool can_reset = true, tyre_gone = false;
    float health = 0.0f;
    for (int k = 0; k < e.passengers; ++k) {
        if (e.characters[k] != 0) {
            e.characters[k] = 0;
            // Simulation::DeleteHumanObject (o Human apaga o personagem, que mata o ator, no DeleteDeadObjects).
            for (Human* hm : simulation_humans())
                if (Human_Image(*hm) == e.humans[k]) {
                    DeleteHumanObject(simulation(), hm);
                    break;
                }
            e.humans[k] = 0;
            --c.characters_awake;
        }
    }
    if (e.heli == 0) {
        --c.vehicles_awake;
        BondCar* car = car_from_p32(e.physics);
        if (car) {
            health = car->hit_points();
            can_reset = car->d.reset_available == 0;  // GetResetAvailable (+0x6c)
            for (int i = 0; i < 4 && !tyre_gone; ++i) tyre_gone = car->d.tyre_damage_points[i] == 0;  // IsTyreShredded
            AIVehicleController_DeRegisterSimCar(*car);
            Simulation_DeleteCarObject(car);
        } else {
            ++g_stats.guards;
        }
        e.physics = 0;
    } else {
        --c.helis_awake;
        ai_element_pending("AIElementController::GoToSleep (helicóptero)");
        e.physics = 0;
    }
    if (ee::lt(0.0f, health)) e.state = (can_reset && !tyre_gone) ? 0 : 2;
    else e.state = 2;
    e.ai = 0;
}

// [driving] 0x0012da78 AIElementController::ForceVehicleToSleep
void AIElementController_ForceVehicleToSleep(uint32_t vehicle) {
    if (!g_store) return;
    for (AIElement& e : g_store->elements)
        if (e.state == 1 && e.type == 1 && e.ai == vehicle) {
            AIElementController_GoToSleep(e);
            return;
        }
}

// [driving] 0x0012daf0 AIElementController::SyncRuleInfo
void AIElementController_SyncRuleInfo(AIElement& e) {
    if (e.state != 1 || (e.flags & kElemFixedPose)) return;
    if (e.type == 0) {
        ai_element_pending("AIElementController::SyncRuleInfo (personagem)");
    } else if (e.type == 1) {
        if (AIGroundVehicle* v = nf::p32_ptr<AIGroundVehicle>(e.ai)) std::memcpy(e.matrix, vehicle_rot_pos(*v), 64);
        else ++g_stats.guards;
    }
}

// [driving] 0x0012c678 AIElementController::CalcMightAsWellBeDead / 0x0012c738 GetNumMightAsWellBeDead
void AIElementController_CalcMightAsWellBeDead() {
    if (!g_store) return;
    int32_t n = 0;
    for (const AIElement& e : g_store->elements) {
        if (e.type != 1 || !(e.flags & kElemMightBeDead) || (e.flags & kElemNoScore) || !ee::lt(0.0f, e.health))
            continue;
        ++n;
        for (int k = 0; k < e.passengers; ++k)
            if (ee::lt(0.0f, e.passenger[k].health)) ++n;
    }
    g_store->ctl.might_be_dead = n;
}
int32_t AIElementController_GetNumMightAsWellBeDead() { return g_store ? g_store->ctl.might_be_dead : 0; }

// [driving] 0x0012c4c0 AIElementController::EveryoneChill
void AIElementController_EveryoneChill() {
    if (!g_store) return;
    for (AIElement& e : g_store->elements) {
        if (e.type != 1 || e.heli != 0) continue;
        BondCar* car = car_from_p32(e.physics);
        if (!car) continue;
        car->d.control.gas = 0.0f;        // SetCarControlGas (+0xf4)
        car->d.control.brake = 0.0f;      // SetCarControlBrake (+0xfc)
        car->d.control.hand_brake = 1;    // SetCarControlHandBrake (+0x104)
        if (AIGroundVehicle* v = ai_of(*car)) {
            AIGroundVehicle_SetAttackMode(*v, 1);
            AIGroundVehicle_SetNavigateMode(*v, 1, 0);
        }
    }
}

// [driving] 0x0012c448 AIElementController::EveryoneToSleep
void AIElementController_EveryoneToSleep() {
    if (!g_store) return;
    for (AIElement& e : g_store->elements)
        if (e.state == 1) AIElementController_GoToSleep(e);
}

}  // namespace drv
