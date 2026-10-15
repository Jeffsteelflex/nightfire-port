// Os gatilhos da pista (driving/game/triggers.h), portados do MIPS do DRIVING.ELF (WRender.obj e DTuningFile.obj). As
// desmontagens ficam em out/agent_drv_traffic/ do checkout principal (fora do git).
//
// Divergências (port), cada uma também na função:
//  - os eventos entram numa fila (driving/game/events.h: o número, os dados, a imagem do objeto do PS2) em vez do anel;
//    os destrutores portados (os do tráfego, dos objetos derrubáveis e da missão) rodam no fim da passagem, os outros
//    contam (trigger_stats().events, event_stats()); o RRandom do EFireRandomTrigger é o do render, que o PS2 também
//    gasta nos efeitos (o port não), então o sorteio difere do PS2 depois do primeiro efeito;
//  - Init sem 'Trgr' esvazia os dados da pista anterior (o PS2 os deixa; guarda);
//  - Restart não valida os fluxos de som dos EStreamEvent (só conta), nem os das regras da missão;
//  - Update só passa os corpos rígidos: os corpos simples, as instâncias dos trilhos e os raios não existem no port;
//  - Process: as listas dinâmicas das células da grade (o que se move de célula em célula) não existem no port; uma
//    célula com a lista conta como guarda e só a lista fixa dela é lida.

#include "driving/game/triggers.h"

#include <cstring>

#include "driving/ai/ground_vehicle.h"
#include "driving/ai/road_nav.h"
#include "driving/ai/sim_random.h"
#include "driving/game/events.h"
#include "driving/game/shells.h"
#include "driving/game/smackable.h"
#include "driving/camera/camera_math.h"
#include "driving/game/bond_car.h"
#include "driving/game/missile.h"
#include "driving/light_manager.h"
#include "driving/physics/obb.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

TriggerManagerGlobals g_mgr;
TriggerStats g_stats;
constexpr uint32_t kOne = 0x3f800000u;

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// vmul.xyz; vaddy.x; vaddz.x
float vu_dot(const float a[3], const float b[3]) {
    const uint32_t x = ps2::mul(bits(a[0]), bits(b[0]));
    const uint32_t y = ps2::mul(bits(a[1]), bits(b[1]));
    const uint32_t z = ps2::mul(bits(a[2]), bits(b[2]));
    return flt(ps2::add(ps2::add(x, y), z));
}

float body_radius(const RigidBody* body) { return flt(body->radius_flags); }  // lwc1 +0x7c (os dois bits de baixo vão junto)

}  // namespace

TriggerManagerGlobals& trigger_manager() { return g_mgr; }
TriggerStats& trigger_stats() { return g_stats; }

WTrigger* trigger_at(int index) {
    WTrigger* base = nf::p32_ptr<WTrigger>(g_mgr.triggers);
    return base ? base + index : nullptr;
}

// [driving] 0x0022cbe0 WTriggerManager::Init
// Divergência (guarda): sem 'Trgr' o PS2 deixa TriggerData, a cópia e o tamanho da pista anterior (o Restart copiaria
// de novo dentro de um CARP já liberado); aqui ficam vazios (contado quando havia algo).
void WTriggerManager_Init(UData* trgr) {
    if (trgr) {
        g_mgr.data_size = UData_Size(trgr);
        uint8_t* data = UData_Data(trgr);
        g_mgr.data = nf::p32_u(data);
        g_mgr.copy.assign(data, data + g_mgr.data_size);  // UMemory::Alloc + MEM_copy
        g_mgr.count = int32_t(trgr->count);
        g_mgr.triggers = g_mgr.data;
    } else {
        if (g_mgr.data) ++g_stats.guards;
        g_mgr.data = 0;
        g_mgr.data_size = 0;
        g_mgr.copy.clear();
        g_mgr.count = 0;
        g_mgr.triggers = 0;
    }
    g_mgr.manager = 1;  // __builtin_new(0xc) + WTriggerManager::WTriggerManager (o ativo = 1)
    g_mgr.active = 1;
    WTriggerManager_Restart();
    for (int i = 0; i < g_mgr.count; ++i) {
        WTrigger* t = trigger_at(i);
        if (t->flags & kTrigInstances) t->flags = uint16_t(t->flags & ~kTrigWhileActive);
    }
    g_mgr.query_stamp = 0;
}

// [driving] 0x0022cd30 WTriggerManager::Restart
// Divergência: os EStreamEvent dos gatilhos e das regras ('Rule') não são validados (o som: só conta).
void WTriggerManager_Restart() {
    if (g_mgr.data && !g_mgr.copy.empty())
        std::memcpy(nf::p32_ptr<uint8_t>(g_mgr.data), g_mgr.copy.data(), g_mgr.copy.size());
    g_mgr.active = 1;
    for (int i = 0; i < g_mgr.count; ++i) {
        if (WTrigger_HasEvent(*trigger_at(i), 0x16)) WTrigger_FireEvents(*trigger_at(i), true, -1, 0);  // EStartMission
        if (WTrigger_HasEvent(*trigger_at(i), 0x14)) ++g_stats.streams;  // EStreamEvent
    }
}

// [driving] 0x0022ca18 WTrigger::HasEvent
bool WTrigger_HasEvent(const WTrigger& t, int32_t type) {
    const TriggerEvents* list = nf::p32_ptr<const TriggerEvents>(t.events);
    if (!list) return false;
    const TriggerEvent* e = reinterpret_cast<const TriggerEvent*>(list + 1);
    for (int i = 0; i < list->count; ++i)
        if (e[i].type == type) return true;
    return false;
}

// [driving] 0x0022ca68 WTrigger::TestDirection
bool WTrigger_TestDirection(const WTrigger& t, const float v[3]) { return !ee::lt(vu_dot(t.forward, v), 0.0f); }

// [driving] 0x0022c890 WTrigger::FireEvents
// Divergência: os eventos entram na fila do port (driving/game/events.h) em vez do anel do EventManager.
void WTrigger_FireEvents(WTrigger& t, uint32_t flag, int index, uint32_t instance) {
    TriggerEvents* list = nf::p32_ptr<TriggerEvents>(t.events);
    ++g_stats.fired;
    EventDynamicData& d = g_mgr.event_data;
    std::memset(&d, 0, sizeof d);  // MEM_fill
    d.instance = instance;
    d.instance_index = -1;
    d.flag = flag;
    d.index = index;
    std::memcpy(d.position, &t, 16);  // a posição e o raio
    d.position[3] = 1.0f;
    d.trigger = nf::p32_u(&t);
    // RayShell::GetTracer (0x00199278): o raio do Process(int) que tocou o gatilho (o começo, o fim, o dano, pelo menos
    // 50).
    if (const ActiveRayShell* ray = RayShell_GetTracer()) {
        d.has_ray = 1;
        std::memcpy(d.ray_start, ray->start, 12);
        std::memcpy(d.ray_end, ray->end, 12);
        d.ray_value = ray->damage;
        if (ee::lt(d.ray_value, 50.0f)) d.ray_value = 50.0f;
    } else {
        d.has_ray = 0;
    }
    // Divergência (guarda): sem lista o PS2 leria a contagem no endereço 0; aqui nenhum evento.
    if (!list) ++g_stats.guards;
    TriggerEvent* e = list ? reinterpret_cast<TriggerEvent*>(list + 1) : nullptr;
    for (int i = 0; list && i < list->count; ++i) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&e[i]) + e[i].data_offset;
        g_stats.events[e[i].type]++;
        // RegisterEvent::LookupEvent (0x1757b8, a tabela 0x36fda0) e a função criadora (driving/game/events.h).
        event_create(e[i].type, data);
    }
    if (t.flags & kTrigOnce) t.flags = uint16_t(t.flags & ~kTrigEnabled);
}

// [driving] 0x0022eb28 WTriggerManager::CheckCollide(RigidBody *, WTrigger *)
bool WTriggerManager_CheckCollide(const RigidBody* body, const WTrigger& t) {
    const float reach = ee::add(body_radius(body), t.radius);
    const float reach2 = ee::mul(reach, reach);
    uint32_t d[3];
    for (int i = 0; i < 3; ++i) d[i] = ps2::sub(bits(t.position[i]), bits(body->pos[i]));
    if (t.shape == 2) {
        // A esfera: |d|² (vmadday, sem raiz).
        uint32_t acc = ps2::mul(d[0], d[0]);
        acc = ps2::madd(acc, kOne, ps2::mul(d[1], d[1]));
        acc = ps2::madd(acc, kOne, ps2::mul(d[2], d[2]));
        if (!ee::lt(flt(acc), reach2)) return false;
        if (t.flags & kTrigDirectional) return WTrigger_TestDirection(t, body->vel);
        return true;
    }
    // O cilindro e a caixa: primeiro no plano (x² + z²).
    const uint32_t acc = ps2::madd(ps2::mul(d[0], d[0]), kOne, ps2::mul(d[2], d[2]));
    if (!ee::lt(flt(acc), reach2)) return false;
    if ((t.flags & kTrigDirectional) && !WTrigger_TestDirection(t, body->vel)) return false;
    if (t.shape == 3) {
        const float r = body_radius(body), y = body->pos[1];
        if (!ee::le(t.position[1], ee::add(y, r))) return false;
        return ee::lt(ee::sub(y, r), ee::add(t.position[1], t.height));
    }
    if (t.shape != 1) {
        ++g_stats.guards;  // printf("WTrigger - Unsupported trigger Shape: %d")
        return false;
    }
    // A caixa: a do corpo (a orientação dele, o centro com w 1, as meias-medidas) contra a do gatilho.
    OBB body_box, trig_box;
    const float centre[4] = {body->pos[0], body->pos[1], body->pos[2], 1.0f};
    OBB_Reset(body_box, body->info->orient, centre, body->info->half_size);
    float m[4][4] = {};
    std::memcpy(m[0], t.right, 12);
    if (t.flags & kTrigRotated) {
        // 0x25a8e8: forward x right pela FPU.
        const float* f = t.forward;
        const float* r = t.right;
        m[1][1] = ee::sub(ee::mul(f[2], r[0]), ee::mul(f[0], r[2]));
        m[1][2] = ee::sub(ee::mul(f[0], r[1]), ee::mul(f[1], r[0]));
        m[1][0] = ee::sub(ee::mul(f[1], r[2]), ee::mul(f[2], r[1]));
    } else {
        m[1][1] = 1.0f;
    }
    std::memcpy(m[2], t.forward, 12);
    m[3][3] = 1.0f;
    const float half_h = ee::mul(t.height, 0.5f);
    float tc[4];
    tc[0] = ee::add(t.position[0], ee::mul(half_h, m[1][0]));
    tc[1] = ee::add(t.position[1], ee::mul(half_h, m[1][1]));
    tc[2] = ee::add(t.position[2], ee::mul(half_h, m[1][2]));
    tc[3] = 1.0f;
    const float ext[4] = {ee::mul(t.width, 0.5f), half_h, ee::mul(t.depth, 0.5f), 1.0f};
    OBB_Reset(trig_box, m, tc, ext);
    return OBB_CheckOBBOverlap(trig_box, body_box);
}

// [driving] 0x0022d240 WTriggerManager::Process(int, RigidBody *)
// Divergência: as listas dinâmicas das células (contadas como guarda) não existem no port.
void WTriggerManager_Process(int index, RigidBody* body) {
    const uint32_t stamp = ++g_mgr.query_stamp;
    uint16_t touch = kTrigOthers;
    if (body->type == 1) touch = kTrigRigidType1;
    else if (body->type == 2) touch = kTrigRigidType2;
    TrackWorld* tw = world() ? world()->track_world() : nullptr;
    if (!tw) {
        ++g_stats.guards;
        return;
    }
    std::vector<uint32_t> cells;
    cells.reserve(64);
    WGrid_FindNodes(tw->grid(), body->pos, body_radius(body), cells);
    for (uint32_t cell : cells) {
        const WGridNode* node = tw->node(cell);
        if (!node) continue;
        if (node->dynamic != 0) ++g_stats.guards;
        const uint16_t* list =
            reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(node) + 0x10 + node->static_offset[1]);
        for (int i = 0; i < node->static_count[1]; ++i) {
            WTrigger* t = trigger_at(list[i]);
            if (!t) {
                ++g_stats.guards;
                continue;
            }
            if (t->query_stamp == stamp) continue;
            t->query_stamp = stamp;
            const uint16_t f = t->flags;
            if (!(f & kTrigEnabled) || (f & kTrigIgnored)) continue;
            if ((f & kTrigWhileActive) && g_mgr.active == 0) continue;
            if ((f & touch) == 0) continue;
            if (WTriggerManager_CheckCollide(body, *t)) WTrigger_FireEvents(*t, false, index, 0);
        }
    }
}

// [driving] 0x00231cc8 WWorldMath::NearestPointLine3D (vsub.xyz, os produtos por vmul.xyz/vaddy.x/vaddz.x, a divisão
// pela FPU, o vadd.xyz: o w de `out` fica)
void WWorldMath_NearestPointLine3D(const float p[4], const float seg[2][4], float out[4]) {
    float d[3], v[3];
    for (int i = 0; i < 3; ++i) d[i] = flt(ps2::sub(bits(seg[1][i]), bits(seg[0][i])));
    for (int i = 0; i < 3; ++i) v[i] = flt(ps2::sub(bits(p[i]), bits(seg[0][i])));
    const float t = ee::div(vu_dot(v, d), vu_dot(d, d));
    for (int i = 0; i < 3; ++i) out[i] = flt(ps2::add(ps2::mul(bits(d[i]), bits(t)), bits(seg[0][i])));
}

// [driving] 0x002318b0 WWorldMath::NearestPointLine2D (a FPU; o y 0, o w de `out` fica)
void WWorldMath_NearestPointLine2D(const float p[4], const float seg[2][4], float out[4]) {
    const float sx = seg[0][0], sz = seg[0][2];
    const float vz = ee::sub(p[2], sz), dz = ee::sub(seg[1][2], sz);
    const float vx = ee::sub(p[0], sx), dx = ee::sub(seg[1][0], sx);
    const float dz2 = ee::mul(dz, dz);
    const float num = ee::add(ee::mul(vx, dx), ee::mul(vz, dz));
    const float den = ee::add(ee::mul(dx, dx), dz2);
    const float t = ee::div(num, den);
    out[1] = 0.0f;
    out[2] = ee::add(sz, ee::mul(t, dz));
    out[0] = ee::add(sx, ee::mul(t, dx));
}

// [driving] 0x0022caa8 WTrigger::TestDirection(COORD4 *): o 0x0022ca68 com o fim menos o começo (vsub.xyz).
bool WTrigger_TestDirectionSeg(const WTrigger& t, const float seg[2][4]) {
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = flt(ps2::sub(bits(seg[1][i]), bits(seg[0][i])));
    return WTrigger_TestDirection(t, d);
}

// [driving] 0x0022e3a0 WTriggerManager::CheckCollide(float, COORD4 *, WTrigger *)
bool WTriggerManager_CheckCollideSeg(float half, const float seg[2][4], const WTrigger& t) {
    // O meio do segmento no plano contra o raio do gatilho mais a meia-largura do raio.
    const float mx = ee::mul(ee::add(seg[0][0], seg[1][0]), 0.5f);
    const float mz = ee::mul(ee::add(seg[0][2], seg[1][2]), 0.5f);
    const float dx = ee::sub(t.position[0], mx), dz = ee::sub(t.position[2], mz);
    const float reach = ee::add(half, t.radius);
    if (!ee::lt(ee::add(ee::mul(dx, dx), ee::mul(dz, dz)), ee::mul(reach, reach))) return false;
    if ((t.flags & kTrigDirectional) && !WTrigger_TestDirectionSeg(t, seg)) return false;
    if (uint8_t(t.shape - 2) < 2) {
        alignas(16) const float p[4] = {t.position[0], t.position[1], t.position[2], 0.0f};
        alignas(16) float out[4] = {t.position[0], t.position[1], t.position[2], 0.0f};
        if (t.shape == 2) {
            // A esfera: a reta inteira (vsub.xyz; vmul.xyz; vmulax.x; vmadday.x; vmaddz.x com 1).
            WWorldMath_NearestPointLine3D(p, seg, out);
            uint32_t d[3];
            for (int i = 0; i < 3; ++i) d[i] = ps2::sub(bits(out[i]), bits(p[i]));
            uint32_t acc = ps2::mul(d[0], d[0]);
            acc = ps2::madd(acc, kOne, ps2::mul(d[1], d[1]));
            acc = ps2::madd(acc, kOne, ps2::mul(d[2], d[2]));
            return ee::lt(flt(acc), ee::mul(t.radius, t.radius));
        }
        // O cilindro: no plano (vmulax.x; vmaddz.x com 1), depois a altura da reta no ponto mais perto.
        WWorldMath_NearestPointLine2D(p, seg, out);
        uint32_t d[3];
        for (int i = 0; i < 3; ++i) d[i] = ps2::sub(bits(out[i]), bits(p[i]));
        const uint32_t acc = ps2::madd(ps2::mul(d[0], d[0]), kOne, ps2::mul(d[2], d[2]));
        if (!ee::lt(flt(acc), ee::mul(t.radius, t.radius))) return false;
        const float sy = seg[0][1];
        const float dy = ee::sub(seg[1][1], sy);
        if (ee::lt(ee::abs(dy), ps2::ffloat(0x3c23d70au))) {  // 0,01
            if (!ee::le(t.position[1], sy)) return false;
            return ee::lt(sy, ee::add(t.position[1], t.height));
        }
        float f;
        const float ddx = ee::sub(seg[1][0], seg[0][0]);
        if (ee::lt(ps2::ffloat(0x3a83126fu), ee::abs(ddx))) {  // 0,001
            f = ee::div(ee::sub(seg[0][0], out[0]), ddx);
        } else {
            const float s0 = seg[0][2];
            f = ee::div(ee::sub(s0, out[2]), ee::sub(seg[1][2], s0));
        }
        const float y = ee::add(sy, ee::mul(ee::neg(f), dy));
        if (!ee::le(t.position[1], y)) return false;
        return ee::le(y, ee::add(t.position[1], t.height));
    }
    if (t.shape != 1) {
        ++g_stats.guards;  // printf("WTrigger - Unsupported trigger Shape: %d")
        return false;
    }
    // A caixa: as pontas no espaço do gatilho (a inversa da matriz right, cima, forward, o lugar).
    if (ee::lt(half, ps2::ffloat(0x3c23d70au))) return false;
    alignas(16) float m[4][4] = {};
    std::memcpy(m[0], t.right, 12);
    if (t.flags & kTrigRotated) {
        const float* f = t.forward;
        const float* r = t.right;
        m[1][1] = ee::sub(ee::mul(f[2], r[0]), ee::mul(f[0], r[2]));
        m[1][2] = ee::sub(ee::mul(f[0], r[1]), ee::mul(f[1], r[0]));
        m[1][0] = ee::sub(ee::mul(f[1], r[2]), ee::mul(f[2], r[1]));
    } else {
        m[1][1] = 1.0f;
    }
    std::memcpy(m[2], t.forward, 12);
    std::memcpy(m[3], t.position, 12);
    m[3][3] = 1.0f;
    OrthoInverse(m);
    alignas(16) float a[4], b[4];
    TransformPoint(&m[0][0], seg[0], a);  // 0x002d1920
    TransformPoint(&m[0][0], seg[1], b);
    float d[4];
    for (int i = 0; i < 4; ++i) d[i] = flt(ps2::sub(bits(b[i]), bits(a[i])));
    const float hx = ee::mul(t.width, 0.5f), hz = ee::mul(t.depth, 0.5f), h = t.height;
    auto inside = [&](const float* q) {
        return ee::lt(ee::neg(hx), q[0]) && ee::lt(q[0], hx) && ee::lt(0.0f, q[1]) && ee::lt(q[1], h) &&
               ee::lt(ee::neg(hz), q[2]) && ee::lt(q[2], hz);
    };
    if (inside(a) || inside(b)) return true;
    const float eps = ps2::ffloat(0x38d1b717u);  // 0,0001
    if (ee::lt(eps, ee::abs(d[0]))) {
        const float inv = ee::div(1.0f, d[0]);
        float s = ee::neg(ee::mul(ee::sub(a[0], hx), inv));
        if (ee::le(0.0f, s) && ee::le(s, 1.0f)) {
            const float y = ee::add(a[1], ee::mul(s, d[1]));
            if (ee::le(0.0f, y) && ee::lt(y, h) && ee::lt(ee::abs(ee::add(a[2], ee::mul(s, d[2]))), hz)) return true;
        }
        s = ee::neg(ee::mul(ee::add(a[0], hx), inv));
        if (ee::le(0.0f, s) && ee::le(s, 1.0f)) {
            const float y = ee::add(a[1], ee::mul(s, d[1]));
            if (ee::le(0.0f, y) && ee::lt(y, h) && ee::lt(ee::abs(ee::add(a[2], ee::mul(s, d[2]))), hz)) return true;
        }
    }
    if (ee::lt(eps, ee::abs(d[2]))) {
        const float inv = ee::div(1.0f, d[2]);
        float s = ee::neg(ee::mul(ee::sub(a[2], hz), inv));
        if (ee::le(0.0f, s) && ee::le(s, 1.0f)) {
            const float y = ee::add(a[1], ee::mul(s, d[1]));
            if (ee::lt(ee::abs(ee::add(a[0], ee::mul(s, d[0]))), hx) && ee::le(0.0f, y) && ee::lt(y, h)) return true;
        }
        s = ee::neg(ee::mul(ee::add(a[2], hz), inv));
        if (ee::lt(s, 0.0f) || ee::lt(1.0f, s)) return false;
        const float y = ee::add(a[1], ee::mul(s, d[1]));
        if (ee::le(hx, ee::abs(ee::add(a[0], ee::mul(s, d[0]))))) return false;
        if (ee::lt(y, 0.0f)) return false;
        return ee::lt(y, h);
    }
    return false;
}

// [driving] 0x0022def0 WTriggerManager::Process(int)
// Divergência: as listas dinâmicas das células (contadas como guarda) não existem no port.
void WTriggerManager_ProcessRay(int index) {
    const ActiveRayShell* ray = RayShell_GetActiveRayShell(index);
    if (!ray) {
        ++g_stats.guards;
        return;
    }
    ++g_stats.rays;
    Simulation& sim = simulation();
    PhysicsObject* shooter = FindPhysicsObjectSignature(sim, ray->sig);
    PhysicsObject* player = GetPlayerObject(sim);
    const uint32_t stamp = ++g_mgr.query_stamp;
    const uint16_t touch = shooter == player ? uint16_t(0xc0) : uint16_t(0x40);
    alignas(16) const float seg[2][4] = {{ray->start[0], ray->start[1], ray->start[2], 1.0f},
                                         {ray->end[0], ray->end[1], ray->end[2], 1.0f}};
    TrackWorld* tw = world() ? world()->track_world() : nullptr;
    if (!tw) {
        ++g_stats.guards;
        return;
    }
    std::vector<uint32_t> cells;
    cells.reserve(64);
    WGrid_FindNodesSeg(tw->grid(), &seg[0][0], cells);
    for (uint32_t cell : cells) {
        const WGridNode* node = tw->node(cell);
        if (!node) continue;
        if (node->dynamic != 0) ++g_stats.guards;
        const uint16_t* list =
            reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(node) + 0x10 + node->static_offset[1]);
        for (int i = 0; i < node->static_count[1]; ++i) {
            WTrigger* t = trigger_at(list[i]);
            if (!t) {
                ++g_stats.guards;
                continue;
            }
            if (t->query_stamp == stamp) continue;
            t->query_stamp = stamp;
            const uint16_t f = t->flags;
            if (!(f & kTrigEnabled) || (f & kTrigIgnored)) continue;
            if ((f & kTrigWhileActive) && g_mgr.active == 0) continue;
            if ((f & touch) == 0) continue;
            if (shooter != player && (f & 0x80)) continue;
            if (!WTriggerManager_CheckCollideSeg(ray->half, seg, *t)) continue;
            ++g_stats.ray_hits;
            ++g_stats.ray_triggers[list[i]];
            RayShell_GetNumTracers(index);  // o raio do FireEvents
            WTrigger_FireEvents(*t, 1, -1, 0);
            RayShell_GetNumTracers(-1);
        }
    }
}

// [driving] 0x0022f0c0 WTriggerManager::Update
// Divergência: os corpos simples e os trilhos (RPathEngine) não são conferidos no port.
void WTriggerManager_Update() {
    if (g_mgr.manager == 0) return;
    RayShell_GetNumTracers(-1);
    Simulation& sim = simulation();
    for (int i = 0; i < kRigidBodySlots; ++i) {
        if (!sim.owners[i]) continue;
        RigidBody* body = GetRigidBody(sim, i);
        if (body->state == 2) WTriggerManager_Process(i, body);
    }
    const int32_t n = RayShell_GetNumActiveRayShells();
    for (int32_t i = 0; i < n; ++i) WTriggerManager_ProcessRay(i);
    RayShell_ClearActiveRayShells();
}

// [driving] 0x0015c080 ESpawnTraffic::~ESpawnTraffic
void ESpawnTraffic_Execute(const WTrigger& t, float speed) {
    VehicleControllerGlobals& k = vehicle_controller();
    Simulation& sim = simulation();
    for (AIGroundVehicle* v : k.traffic) {
        if (!v || !v->car) {
            ++g_stats.guards;  // o PS2 leria o carro de um tráfego que não foi criado
            continue;
        }
        BondCar& car = *v->car;
        RigidBody* body = car.body();
        if (body->state != 3) continue;
        float pos[3], dir[3];
        std::memcpy(pos, t.position, 12);  // Float_COORD4toCOORD3
        std::memcpy(dir, t.forward, 12);
        AIGroundVehicle_SetAttackMode(*v, 12);
        AIGroundVehicle_SetNavigateMode(*v, 3, 0);
        PBondCar_ResetDamage(car);
        PBondCar_ClearEMPState(car);
        PBondCar_ResetCar(car, pos, dir);
        v->spawn_timer = sim.frame_rate * 5 + sim.steps;
        WRoadNav_InitAtPoint(*nav_of(*v), pos, dir, false);
        int32_t n = int32_t(int16_t(k.num_active_traffic + 1));
        if (n < 0) n = 0;
        k.num_active_traffic = int16_t(n);
        if (ee::lt(0.0f, speed)) {
            // A frente do corpo (unitária, vmadday + vrsqrt) vezes speed x massa: o momento.
            const float* f = body->info->orient[2];
            const float k_mass = ee::mul(speed, body->mass);
            uint32_t acc = ps2::mul(bits(f[0]), bits(f[0]));
            acc = ps2::madd(acc, kOne, ps2::mul(bits(f[1]), bits(f[1])));
            acc = ps2::madd(acc, kOne, ps2::mul(bits(f[2]), bits(f[2])));
            const uint32_t q = ps2::rsqrt(kOne, acc);
            for (int i = 0; i < 3; ++i) body->momentum[i] = flt(ps2::mul(ps2::mul(bits(f[i]), q), bits(k_mass)));
        }
        ++g_stats.traffic_spawned;
        return;
    }
}

FireRandomTrigger FireRandomTrigger_FromData(const uint8_t* data, uint32_t flag, int32_t index) {
    FireRandomTrigger e{};
    for (int k = 0; k < 4; ++k) {
        std::memcpy(&e.trigger[k], data + 4 + 8 * k, 4);
        std::memcpy(&e.chance[k], data + 8 + 8 * k, 4);
    }
    e.flag = flag;
    e.index = index;
    return e;
}

// [driving] 0x0014c688 EFireRandomTrigger::~EFireRandomTrigger
void EFireRandomTrigger_Execute(const FireRandomTrigger& e) {
    const uint32_t n = RRandom_Generate(render_random());
    const float r = ee::mul(ee::mul(ee::cvt_s(int32_t(n)), 100.0f), flt(0x37800000u));  // x 100 / 65536
    const float c1 = e.chance[0];
    const float c2 = ee::add(e.chance[1], c1);
    const float c3 = ee::add(e.chance[2], c2);
    const float c4 = ee::add(e.chance[3], c3);
    uint32_t pick = 0;
    if (ee::le(r, c1)) pick = e.trigger[0];
    else if (ee::le(r, c2)) pick = e.trigger[1];
    else if (ee::le(r, c3)) pick = e.trigger[2];
    else if (ee::le(r, c4)) pick = e.trigger[3];
    if (WTrigger* t = nf::p32_ptr<WTrigger>(pick)) WTrigger_FireEvents(*t, e.flag, e.index, 0);
}

FireTriggerSpeed FireTriggerSpeed_FromData(const uint8_t* data, uint32_t flag, int32_t index) {
    FireTriggerSpeed e{};
    std::memcpy(&e.trigger, data + 4, 4);
    std::memcpy(&e.speed, data + 8, 4);
    std::memcpy(&e.less, data + 0xc, 4);
    e.flag = flag;
    e.index = index;
    return e;
}

// [driving] 0x0014c8c8 EFireTriggerSpeedCondition::~EFireTriggerSpeedCondition
// Guarda: um índice fora das 48 vagas (o Restart dispara com -1) faria o PS2 ler um corpo fora do vetor; aqui nada.
void EFireTriggerSpeedCondition_Execute(const FireTriggerSpeed& e) {
    WTrigger* t = nf::p32_ptr<WTrigger>(e.trigger);
    if (!t) return;
    if (e.index < 0 || e.index >= kRigidBodySlots) {
        ++g_stats.guards;
        return;
    }
    const RigidBody* b = GetRigidBody(simulation(), e.index);
    const float v2 = ee::add(ee::add(ee::mul(b->vel[0], b->vel[0]), ee::mul(b->vel[1], b->vel[1])),
                             ee::mul(b->vel[2], b->vel[2]));
    const float v = ee::sqrt(v2);
    const bool fire = e.less != 0 ? ee::lt(v, e.speed) : ee::lt(e.speed, v);
    if (fire) WTrigger_FireEvents(*t, e.flag, e.index, 0);
}

void WTriggerManager_Reset() { g_mgr = TriggerManagerGlobals{}; }

void trigger_run_events() {
    events_run();
    // Os EHitWindow do passe (os vidros: seção 26), depois dos numerados.
    hit_windows_run();
}

}  // namespace drv
