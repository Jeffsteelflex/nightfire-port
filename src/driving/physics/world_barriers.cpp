// As barreiras e os objetos de colisão do mundo (driving/physics/world_collision.h, a parte da colisão com os
// prédios), portados do MIPS do DRIVING.ELF (as desmontagens em out/disasm_drv_cww2 e out/disasm_drv_math2 do checkout
// principal). As contas em float seguem a FPU do EE (platform/ps2_float.h), na ordem das instruções; onde o original
// tem um caminho pela VU0 (o gerente +0x30) e outro pela FPU, as contas são as mesmas (a VU0 em macro e a FPU do EE
// truncam igual) e o port faz uma só.

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "driving/physics/obb.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

uint32_t u32(const void* p) { return nf::p32_u(p); }
template <class T>
T* at(uint32_t a) {
    return nf::p32_ptr<T>(a);
}

// FUN_0025b890: ((x m0 + y m4) + z m8) + m12 por coluna; só o xyz é gravado (src e dst podem ser o mesmo).
void xform(const float src[3], const float m[16], float dst[3]) {
    const float s0 = src[0], s1 = src[1], s2 = src[2];
    float r[3];
    for (int c = 0; c < 3; ++c)
        r[c] = ee::add(ee::add(ee::add(ee::mul(s0, m[c]), ee::mul(s1, m[4 + c])), ee::mul(s2, m[8 + c])), m[12 + c]);
    dst[0] = r[0];
    dst[1] = r[1];
    dst[2] = r[2];
}

// A distância 2D aproximada das buscas do mundo: o maior dos |dx|, |dz| mais um quarto do menor.
float approx_distance(float dx, float dz) {
    const float a = ee::abs(dx), b = ee::abs(dz);
    return ee::lt(b, a) ? ee::add(a, ee::mul(b, 0.25f)) : ee::add(b, ee::mul(a, 0.25f));
}

// A distância ao quadrado de a até b: ((dx dx + dy dy) + dz dz) (a VU0 pelo ACC e a FPU dão os mesmos bits).
float distance_sq(const float a[3], const float b[3]) {
    const float dx = ee::sub(a[0], b[0]), dy = ee::sub(a[1], b[1]), dz = ee::sub(a[2], b[2]);
    return ee::add(ee::add(ee::mul(dx, dx), ee::mul(dy, dy)), ee::mul(dz, dz));
}

const CollisionBarrier* article_barriers(const uint8_t* art) {
    const CollisionArticle* a = reinterpret_cast<const CollisionArticle*>(art);
    return reinterpret_cast<const CollisionBarrier*>(art + 0x20 + a->barrier_offset);
}

}  // namespace

void WorldCollisionInfo_Init(WorldCollisionInfo& info) {
    std::memset(&info, 0, sizeof(info));
    info.point[3] = 1.0f;  // 0x00355ba0: (0, 0, 0, 1)
}

// [driving] 0x00231548 WWorldMath::SegmentIntersect
bool WWorldMath_SegmentIntersect(const float* a, const float* b, float out[4]) {
    const float a0x = a[0], a0z = a[2], b0x = b[0], b0z = b[2];
    const float ex = ee::sub(a0x, b0x);    // f8
    const float dbz = ee::sub(b[6], b0z);  // f5
    const float dbx = ee::sub(b[4], b0x);  // f2
    const float dax = ee::sub(a[4], a0x);  // f1
    const float daz = ee::sub(a[6], a0z);  // f0
    const float ez = ee::sub(a0z, b0z);    // f3
    const float denom = ee::sub(ee::mul(dbz, dax), ee::mul(dbx, daz));
    const float nb = ee::sub(ee::mul(dax, ez), ee::mul(daz, ex));
    const float na = ee::sub(ee::mul(dbx, ez), ee::mul(dbz, ex));
    if (ee::eq(denom, 0.0f)) return false;
    const float eps = ps2::ffloat(0x38d1b717u), neps = ps2::ffloat(0xb8d1b717u);  // ±0,0001
    // Em [-0,0001, denom] ou em [denom, 0,0001] (o denominador negativo).
    auto in_range = [&](float v) {
        if (ee::le(neps, v) && ee::le(v, denom)) return true;
        return ee::le(v, eps) && ee::le(denom, v);
    };
    if (!in_range(na) || !in_range(nb)) return false;
    if (out) {
        const float t = ee::div(na, denom);
        const float d[3] = {ee::sub(a[4], a[0]), ee::sub(a[5], a[1]), ee::sub(a[6], a[2])};
        out[0] = ee::add(a[0], ee::mul(t, d[0]));
        out[1] = ee::add(a[1], ee::mul(t, d[1]));
        out[3] = 1.0f;
        out[2] = ee::add(a[2], ee::mul(t, d[2]));
    }
    return true;
}

// [driving] 0x00231720 WWorldMath::IntersectCircle
bool WWorldMath_IntersectCircle(float& t0, float& t1, float x0, float z0, float x1, float z1, float cx, float cz,
                                float r) {
    const float r2 = ee::mul(r, r);
    {
        const float dx = ee::sub(cx, x0), dz = ee::sub(cz, z0);
        if (ee::lt(ee::add(ee::mul(dx, dx), ee::mul(dz, dz)), r2)) {
            const float ex = ee::sub(cx, x1), ez = ee::sub(cz, z1);
            if (ee::lt(ee::add(ee::mul(ex, ex), ee::mul(ez, ez)), r2)) {
                t1 = 0.0f;
                t0 = 0.0f;
                return true;
            }
        }
    }
    const float px = ee::sub(x0, cx), pz = ee::sub(z0, cz), qx = ee::sub(x1, cx), qz = ee::sub(z1, cz);
    const float dx = ee::sub(qx, px), dz = ee::sub(qz, pz);
    const float half_b = ee::add(ee::mul(dx, px), ee::mul(dz, pz));
    const float a = ee::add(ee::mul(dx, dx), ee::mul(dz, dz));
    const float c = ee::sub(ee::add(ee::mul(px, px), ee::mul(pz, pz)), r2);
    const float bb = ee::add(half_b, half_b);
    const float disc = ee::sub(ee::mul(bb, bb), ee::mul(ee::mul(a, 4.0f), c));
    if (ee::lt(disc, 0.0f)) return false;
    const float sq = ee::sqrt(disc);
    const float inv = ee::div(1.0f, ee::add(a, a));
    const float nb = ee::neg(bb);
    t0 = ee::mul(ee::sub(nb, sq), inv);
    t1 = ee::mul(ee::add(nb, sq), inv);
    if (ee::le(0.0f, t0) && ee::le(t0, 1.0f)) return true;
    if (!ee::le(0.0f, t1)) return false;
    return ee::le(t1, 1.0f);
}

// [driving] 0x00219a50 WCollisionMgr::GetBarrierList
void WCollisionMgr_GetBarrierList(const WCollisionMgr& mgr, std::vector<BarrierListEntry>& out,
                                  const std::vector<InstanceListEntry>& nearby, const float centre[3], float radius) {
    if (out.capacity() < 21) out.reserve(21);
    const float r2 = ee::mul(radius, radius);
    for (const InstanceListEntry& e : nearby) {
        const WCollisionInstance* inst = e.instance;
        const uint8_t* art = at<uint8_t>(inst->article);
        if (!art) continue;
        const CollisionArticle* ah = reinterpret_cast<const CollisionArticle*>(art);
        if (ah->barrier_count == 0) continue;
        float m[16];
        WCollisionInstance_MakeMatrix(mgr, *inst, m, true);
        bool inverse_built = false;
        float inv[16];
        float local[3];
        xform(centre, m, local);
        const CollisionBarrier* bars = article_barriers(art);
        for (uint32_t k = 0; k < ah->barrier_count; ++k) {
            const CollisionBarrier& b = bars[k];
            if (mgr.barrier_mask & b.subtype) continue;
            const float lx = local[0], lz = local[2];
            const float dx = ee::sub(b.x1, b.x0), dz = ee::sub(b.z1, b.z0);
            const float il2 = ee::mul(b.inv_length, b.inv_length);
            const float t = ee::mul(ee::add(ee::mul(ee::sub(lx, b.x0), dx), ee::mul(ee::sub(lz, b.z0), dz)), il2);
            float ex, ez;
            if (ee::lt(t, 0.0f)) {
                ex = ee::sub(b.x0, lx);
                ez = ee::sub(b.z0, lz);
            } else if (ee::lt(1.0f, t)) {
                ex = ee::sub(b.x1, lx);
                ez = ee::sub(b.z1, lz);
            } else {
                ez = ee::sub(ee::add(b.z0, ee::mul(t, dz)), lz);
                ex = ee::sub(ee::add(b.x0, ee::mul(t, dx)), lx);
            }
            const float d = ee::add(ee::mul(ex, ex), ee::mul(ez, ez));
            if (!ee::lt(d, r2)) continue;
            const float ly = local[1];
            if (!ee::lt(b.y0, ee::add(ly, radius))) continue;
            if (!ee::le(ee::sub(ly, radius), b.y1)) continue;
            if (!inverse_built) {
                std::memcpy(inv, m, sizeof(inv));
                inv[3] = inv[7] = inv[11] = 0.0f;
                inv[15] = 1.0f;
                OrthoInverse(inv);
                inverse_built = true;
            }
            BarrierListEntry entry{};
            std::memcpy(entry.p0, &b, 16);
            std::memcpy(entry.p1, reinterpret_cast<const uint8_t*>(&b) + 16, 16);
            xform(entry.p0, inv, entry.p0);
            xform(entry.p1, inv, entry.p1);
            if (ee::lt(entry.p1[1], entry.p0[1])) std::swap(entry.p0[1], entry.p1[1]);
            entry.instance = u32(inst);
            entry.index = k;
            out.push_back(entry);
        }
    }
}

// [driving] 0x00219328 WCollisionMgr::GetClosestIntersectingBarrier
bool WCollisionMgr_GetClosestIntersectingBarrier(const WCollisionMgr& mgr, const std::vector<BarrierListEntry>& list,
                                                 const float* segment, WorldCollisionInfo& info) {
    info.hit = 0;
    float best = ps2::ffloat(0x7e967699u);  // 0x00399060: 1e38
    const BarrierListEntry* found = nullptr;
    const WCollisionInstance* last = nullptr;
    float local[8] = {};  // o segmento no referencial da instância inclinada (os w não são escritos nem lidos)
    for (const BarrierListEntry& e : list) {
        const WCollisionInstance* inst = at<WCollisionInstance>(e.instance);
        float hit[4] = {};
        if (inst->flags & 3) {
            if (last != inst) {
                float m[16];
                WCollisionInstance_MakeMatrix(mgr, *inst, m, true);
                xform(segment, m, local);
                last = inst;
                xform(segment + 4, m, local + 4);
            }
            const uint8_t* art = at<uint8_t>(inst->article);
            const CollisionBarrier& b = article_barriers(art)[e.index];
            if (!WWorldMath_SegmentIntersect(local, reinterpret_cast<const float*>(&b), hit)) continue;
            if (!ee::lt(b.y0, hit[1]) || !ee::lt(hit[1], b.y1)) continue;
            const float d = approx_distance(ee::sub(hit[0], local[0]), ee::sub(hit[2], local[2]));
            if (!ee::lt(d, best)) continue;
            best = d;
            float m2[16];
            WCollisionInstance_MakeMatrix(mgr, *inst, m2, true);
            OrthoInverse(m2);
            found = &e;
            xform(hit, m2, info.point);
        } else {
            if (!WWorldMath_SegmentIntersect(segment, e.p0, hit)) continue;
            if (!ee::lt(e.p0[1], hit[1]) || !ee::lt(hit[1], e.p1[1])) continue;
            const float d = approx_distance(ee::sub(hit[0], segment[0]), ee::sub(hit[2], segment[2]));
            if (!ee::lt(d, best)) continue;
            std::memcpy(info.point, hit, 16);
            best = d;
            found = &e;
        }
    }
    if (found) {
        info.barrier = *found;
        info.hit = 2;
    }
    return info.hit != 0;
}

// [driving] 0x0021a248 WCollisionMgr::GetBarrierNormal
bool WCollisionMgr_GetBarrierNormal(const WCollisionMgr& mgr, const std::vector<BarrierListEntry>& list,
                                    const float* segment, WorldCollisionInfo& info) {
    info.hit = 0;
    if (!WCollisionMgr_GetClosestIntersectingBarrier(mgr, list, segment, info)) return info.hit != 0;
    const BarrierListEntry& e = info.barrier;
    const float il = e.p1[3];
    info.normal[0] = ee::mul(ee::sub(e.p1[2], e.p0[2]), il);
    info.normal[1] = 0.0f;
    info.normal[2] = ee::mul(ee::neg(ee::sub(e.p1[0], e.p0[0])), il);
    info.normal[3] = 0.0f;
    const uint8_t* tag = reinterpret_cast<const uint8_t*>(&e.p0[3]);
    info.face_type = tag[0];
    info.face_subtype = tag[1];
    info.face_instance = e.instance;
    info.flag52 = uint8_t((at<WCollisionInstance>(e.instance)->flags >> 1) & 1);
    const float dx = ee::sub(segment[0], info.point[0]), dz = ee::sub(segment[2], info.point[2]);
    if (ee::lt(ee::add(ee::mul(info.normal[0], dx), ee::mul(info.normal[2], dz)), 0.0f)) {
        info.normal[0] = ee::neg(info.normal[0]);
        info.normal[2] = ee::neg(info.normal[2]);
    }
    info.hit = 2;
    return true;
}

// [driving] 0x00216d30 WCollisionMgr::ClosestCollisionInfo
void WCollisionMgr_ClosestCollisionInfo(const WCollisionMgr& mgr, const float* segment, const WorldCollisionInfo& a,
                                        const WorldCollisionInfo& b, WorldCollisionInfo& out) {
    (void)mgr;
    if (!a.hit && !b.hit) return;
    const float huge = ps2::ffloat(0x749c31c8u);
    const float da = a.hit ? distance_sq(segment, a.point) : huge;
    const float db = b.hit ? distance_sq(segment, b.point) : huge;
    out = ee::lt(da, db) ? a : b;
}

// [driving] 0x002166f8 WCollisionMgr::GetWorldNormal
bool WCollisionMgr_GetWorldNormal(const WCollisionMgr& mgr, const std::vector<InstanceListEntry>& instances,
                                  const std::vector<BarrierListEntry>& barriers, const float* segment,
                                  WorldCollisionInfo& info, bool faces) {
    WorldCollisionInfo ground, barrier;
    WorldCollisionInfo_Init(ground);
    WorldCollisionInfo_Init(barrier);
    WCollisionMgr_GetBarrierNormal(mgr, barriers, segment, barrier);
    if (faces) {
        // O chão das instâncias: a face que o segmento atravessa primeiro (pelas listas de faixas), o plano dela, o ponto
        // onde o segmento o corta; a normal virada para o começo do segmento.
        WWorldPos wp{};
        WWorldPos_FindClosestFaceSeg(wp, mgr, instances, segment, segment + 4, simulation().steps);
        if (wp.valid != 0) {
            float normal[3];
            const float corners[12] = {wp.v0[0], wp.v0[1], wp.v0[2], 0, wp.v1[0], wp.v1[1], wp.v1[2], 0,
                                       wp.v2[0], wp.v2[1], wp.v2[2], 0};
            WCollisionMgr_TriangleNormal(normal, corners);
            ground.normal[0] = normal[0];
            ground.normal[1] = normal[1];
            ground.normal[2] = normal[2];
            ground.normal[3] = 1.0f;
            float t = 0;
            if (WWorldMath_IntersectSegPlane(segment, segment + 4, wp.v0, ground.normal, ground.point, t)) {
                ground.hit = 1;
                ground.face_type = wp.surface[0];
                ground.face_subtype = wp.surface[1];
                ground.face_instance = wp.instance;
                const float d[3] = {ee::sub(segment[0], ground.point[0]), ee::sub(segment[1], ground.point[1]),
                                    ee::sub(segment[2], ground.point[2])};
                const float dot = ee::add(ee::add(ee::mul(ground.normal[0], d[0]), ee::mul(ground.normal[1], d[1])),
                                          ee::mul(ground.normal[2], d[2]));
                if (ee::lt(dot, 0.0f)) {
                    // O caminho VU0 subtrai de zero (vsub.xyz vf0 - n), a FPU troca o sinal: só o zero difere.
                    for (int i = 0; i < 3; ++i)
                        ground.normal[i] = mgr.vu0_path ? ee::sub(0.0f, ground.normal[i]) : ee::neg(ground.normal[i]);
                }
            }
        }
    }
    info.hit = 0;
    WCollisionMgr_ClosestCollisionInfo(mgr, segment, ground, barrier, info);
    return info.hit != 0;
}

// [driving] 0x00218440 WCollisionMgr::GetObjectLists (e 0x00217e30 GetObjectListsGuts)
void WCollisionMgr_GetObjectLists(const TrackWorld& world, std::vector<WCollisionObject*>& cylinders,
                                  std::vector<WCollisionObject*>& boxes, const float position[3], float radius) {
    std::vector<uint32_t> cells;
    cells.reserve(64);
    WGrid_FindNodes(const_cast<TrackWorld&>(world).grid(), position, radius, cells);
    if (cylinders.capacity() < 16) cylinders.reserve(16);
    if (boxes.capacity() < 16) boxes.reserve(16);
    WCollisionObject* objs = world.objects();
    for (uint32_t c : cells) {
        const WGridNode* node = world.node(c);
        if (!node) continue;
        // A lista dinâmica (os 'de') não é montada pelo port: conta como guarda (nenhuma em Paris).
        if (node->dynamic != 0) ++g_world_collision_guards;
        const uint16_t* idx = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(node) + 16 +
                                                                node->static_offset[2]);
        for (int i = 0; i < node->static_count[2]; ++i) {
            WCollisionObject* o = &objs[idx[i]];
            const float d = approx_distance(ee::sub(o->position[0], position[0]), ee::sub(o->position[2], position[2]));
            if (!ee::lt(d, ee::add(radius, o->radius))) continue;
            if (o->is_cylinder) cylinders.push_back(o);
            else boxes.push_back(o);
        }
    }
}

// [driving] 0x00218af0 WCollisionMgr::GetClosestIntersectingCylObject
WCollisionObject* WCollisionMgr_GetClosestIntersectingCylObject(const float* segment, WorldCollisionInfo& info,
                                                                 const std::vector<WCollisionObject*>& list) {
    WCollisionObject* result = nullptr;
    if (list.empty()) return nullptr;
    float best = ps2::ffloat(0x7e967699u);  // 0x00399048: 1e38
    for (WCollisionObject* o : list) {
        float t0 = 0, t1 = 0;
        if (!WWorldMath_IntersectCircle(t0, t1, segment[0], segment[2], segment[4], segment[6], o->position[0],
                                        o->position[2], o->radius))
            continue;
        // O parâmetro: t1 se ele está em [0, 1] e t0 não; o menor dos dois se os dois estão; senão t0.
        float t;
        if (ee::le(0.0f, t1) && ee::le(t1, 1.0f)) {
            if (ee::le(0.0f, t0) && ee::le(t0, 1.0f)) t = ee::lt(t1, t0) ? t1 : t0;
            else t = t1;
        } else {
            t = t0;
        }
        const float u = ee::sub(1.0f, t);
        const float px = ee::add(ee::mul(u, segment[0]), ee::mul(t, segment[4]));
        info.point[0] = px;
        const float py = ee::add(ee::mul(u, segment[1]), ee::mul(t, segment[5]));
        info.point[1] = py;
        info.point[3] = 1.0f;
        const float pz = ee::add(ee::mul(u, segment[2]), ee::mul(t, segment[6]));
        info.point[2] = pz;
        if (!ee::lt(o->position[1], py)) continue;
        if (!ee::lt(py, ee::add(o->position[1], ee::add(o->half[1], o->half[1])))) continue;
        const float d = approx_distance(ee::sub(px, segment[0]), ee::sub(pz, segment[2]));
        if (!ee::lt(d, best)) continue;
        best = d;
        result = o;
    }
    return result;
}

// [driving] 0x002187f0 WCollisionMgr::GetCylObjectNormal
bool WCollisionMgr_GetCylObjectNormal(const float* segment, const std::vector<WCollisionObject*>& list,
                                      WorldCollisionInfo& info) {
    info.hit = 0;
    WCollisionObject* o = WCollisionMgr_GetClosestIntersectingCylObject(segment, info, list);
    info.object = u32(o);
    if (!o) return info.hit != 0;
    if (o->flags & 1) info.flag52 = 1;
    if (o->is_cylinder == 1) {
        const float nx = ee::sub(info.point[0], o->position[0]);
        info.normal[1] = 0.0f;
        const float ny = info.normal[1];
        info.normal[0] = nx;
        float len = ee::add(ee::mul(nx, nx), ny);
        info.normal[3] = 1.0f;
        const float nz = ee::sub(info.point[2], o->position[2]);
        len = ee::add(len, ee::mul(nz, nz));
        info.normal[2] = nz;
        const float r = ee::rsqrt(1.0f, len);
        info.normal[2] = ee::mul(nz, r);
        info.normal[0] = ee::mul(nx, r);
        info.normal[1] = ee::mul(r, ny);
        const float dx = ee::sub(segment[0], info.point[0]), dz = ee::sub(segment[2], info.point[2]);
        if (ee::lt(ee::add(ee::mul(info.normal[0], dx), ee::mul(info.normal[2], dz)), ny)) {
            info.normal[0] = ee::neg(info.normal[0]);
            info.normal[2] = ee::neg(info.normal[2]);
        }
    }
    info.face_type = o->face_type;
    info.face_subtype = o->face_subtype;
    info.hit = 3;
    return true;
}

// [driving] 0x00212c10 WCollisionObject::MakeMatrix
void WCollisionObject_MakeMatrix(const TrackWorld& world, const WCollisionObject& obj, float m[4][4], bool translate) {
    const float* ri = reinterpret_cast<const float*>(world.render_instances() + 64u * obj.render_index);
    for (int r = 0; r < 3; ++r) {
        m[r][0] = ri[r * 4 + 0];
        m[r][1] = ri[r * 4 + 1];
        m[r][2] = ri[r * 4 + 2];
        m[r][3] = 0.0f;
    }
    if (translate) {
        m[3][0] = obj.position[0];
        m[3][1] = obj.position[1];
        m[3][3] = 1.0f;
        m[3][2] = obj.position[2];
    } else {
        m[3][0] = 0.0f;
        m[3][3] = 1.0f;
        m[3][1] = 0.0f;
        m[3][2] = 0.0f;
    }
}

bool WWorld_HasSceneObj(const TrackWorld& world, uint32_t render_index) {
    if (render_index >= world.render_instance_count()) return false;
    return (world.render_instances()[64u * render_index + 0x0c] & 0x10) != 0;
}

// [driving] 0x00218668 WCollisionMgr::GetObjectOBB
bool WCollisionMgr_GetObjectOBB(const TrackWorld& world, std::vector<WCollisionObject*>& list, float velocity[3],
                                uint8_t tag[2], OBB& out) {
    if (list.empty()) return false;
    WCollisionObject* o = list.back();
    tag[0] = o->face_type;
    tag[1] = o->face_subtype;
    const float pos[4] = {o->position[0], o->position[1], o->position[2], 1.0f};
    float m[4][4];
    WCollisionObject_MakeMatrix(world, *o, m, false);
    list.pop_back();
    velocity[0] = velocity[2] = velocity[1] = 0.0f;
    // A velocidade da instância de render que anda (o RSceneObj da animação, entrada +0x74 da tabela dele), vezes o
    // passo (Simulation +0xdc0): o port não monta os objetos de cena e ela fica zero (guarda).
    if ((o->flags & 1) && WWorld_HasSceneObj(world, o->render_index)) ++g_world_collision_guards;
    OBB_Reset(out, m, pos, o->half);
    return true;
}

}  // namespace drv
