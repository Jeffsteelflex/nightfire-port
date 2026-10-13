// O WCollider (driving/physics/collider.h), da desmontagem do DRIVING.ELF (out/disasm_drv_cww2 do checkout principal).

#include "driving/physics/collider.h"

#include <cstring>

#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

// [driving] 0x002124b8 WCollider::Validate
bool WCollider_Validate(const WCollider& c) {
    if (!c.region_valid) return false;
    // As listas de faixas guardam o artigo da instância (+0x0c delas); o port não as monta (ver PrepareRegion).
    for (const WCollider::Stamp& s : c.stamps)
        if (s.article != *s.source) return false;
    return true;
}

// [driving] 0x002126d0 WCollider::InRegion(COORD3 &, float, unsigned int)
bool WCollider_InRegion(const WCollider& c, const float position[3], float radius, uint32_t mask) {
    if (!WCollider_Validate(c)) return false;
    if ((mask & c.mask) != mask) return false;
    const float slack = ee::sub(c.region_radius, radius);
    if (ee::lt(slack, 0.0f)) return false;
    // A distância ao quadrado: vsub.xyz e o quadrado pelo ACC com o caminho VU0 do gerente, a FPU sem ele (os mesmos
    // bits).
    const float dx = ee::sub(position[0], c.region_centre[0]), dy = ee::sub(position[1], c.region_centre[1]),
                dz = ee::sub(position[2], c.region_centre[2]);
    const float d2 = ee::add(ee::add(ee::mul(dx, dx), ee::mul(dy, dy)), ee::mul(dz, dz));
    return ee::lt(d2, ee::mul(slack, slack));
}

// [driving] 0x00212368 WCollider::Clear
void WCollider_Clear(WCollider& c) {
    if (c.region_valid) {
        // As listas de faixas das instâncias seriam apagadas aqui (o port não as monta); os vetores ficam com a
        // capacidade (o PS2 só move o fim para o começo).
        c.instances.clear();
        c.barriers.clear();
        c.stamps.clear();
    }
    c.region_valid = 0;
}

// [driving] 0x00211ac8 (a região de uma esfera que anda)
void WCollider_FitRegion(bool had_region, const float position[3], const float prev_position[3], float centre[3],
                         float& region_radius, float radius) {
    const float k = ps2::ffloat(0x3f8ccccdu);  // 1,1
    if (!had_region) {
        std::memcpy(centre, position, 12);
        region_radius = ee::mul(radius, k);
        return;
    }
    float d[3] = {ee::sub(position[0], prev_position[0]), ee::sub(position[1], prev_position[1]),
                  ee::sub(position[2], prev_position[2])};
    const float len = ee::sqrt(ee::add(ee::add(ee::mul(d[0], d[0]), ee::mul(d[1], d[1])), ee::mul(d[2], d[2])));
    const float step = ee::lt(1.0f, len) ? 1.0f : len;
    region_radius = ee::add(ee::mul(step, k), ee::mul(radius, k));
    for (float& v : d) v = ee::mul(v, k);
    for (int i = 0; i < 3; ++i) centre[i] = ee::add(position[i], d[i]);
}

// [driving] 0x00211ed0 WCollider::PrepareRegion
void WCollider_PrepareRegion(WCollider& c, TrackWorld& world) {
    if (c.mask & WCollider::kInstances) {
        if (c.instances.capacity() < 100) c.instances.reserve(100);
        // As instâncias com as listas de faixas perto da região (a câmera do jogador e os veículos com SUB_PHYSICS).
        WCollisionMgr_GetInstanceList(world.mgr(), world.grid(), c.instances, c.region_centre, c.region_radius, true,
                                      c.flat != 0);
    }
    if (c.mask & WCollider::kBarriers) {
        std::vector<InstanceListEntry> nearby;
        nearby.reserve(100);
        WCollisionMgr_GetInstanceList(world.mgr(), world.grid(), nearby, c.region_centre, c.region_radius, false,
                                      c.flat != 0);
        if (c.barriers.capacity() < 21) c.barriers.reserve(21);
        WCollisionMgr_GetBarrierList(world.mgr(), c.barriers, nearby, c.region_centre, c.region_radius);
        if (c.stamps.capacity() < c.barriers.size()) c.stamps.reserve(c.barriers.size());
        for (const BarrierListEntry& b : c.barriers) {
            const WCollisionInstance* inst = nf::p32_ptr<WCollisionInstance>(b.instance);
            c.stamps.push_back({inst->article, &inst->article});
        }
    }
    c.region_valid = 1;
}

// [driving] 0x00211e18 WCollider::Refresh(COORD3 &, float)
void WCollider_Refresh(WCollider& c, TrackWorld& world, const float position[3], float radius) {
    if (!WCollider_InRegion(c, position, radius, c.mask)) {
        const bool had = c.region_valid != 0;
        WCollider_Clear(c);
        std::memcpy(c.position, position, 12);
        c.radius = radius;
        WCollider_FitRegion(had, c.position, c.prev_position, c.region_centre, c.region_radius, radius);
        WCollider_PrepareRegion(c, world);
    }
    std::memcpy(c.prev_position, position, 12);
    c.prev_radius = radius;
}

// [driving] 0x002117e8 WCollider::WCollider(COORD3 &, float, bool, unsigned int)
void WCollider_Construct(WCollider& c, TrackWorld& world, const float position[3], float radius, bool flat,
                         uint32_t mask) {
    c = WCollider{};
    c.flat = flat ? 1u : 0u;
    c.mask = mask;
    WCollider_Refresh(c, world, position, radius);
}

// [driving] 0x002127f0 WCollider::GetWorldNormal
bool WCollider_GetWorldNormal(const WCollider& c, const TrackWorld& world, const float* segment, WorldCollisionInfo& info) {
    if (c.instances.empty() && c.barriers.empty()) return false;
    return WCollisionMgr_GetWorldNormal(const_cast<TrackWorld&>(world).mgr(), c.instances, c.barriers, segment, info,
                                        ((c.mask >> 3) & 1) != 0);
}

}  // namespace drv
