// O choque de um segmento com o mundo (WCollisionMgr::CheckHitWorld e o que ele usa; WGrid.obj, WCollisionMgr.obj,
// WWorldPos.obj e WWorldMath.obj do DRIVING.ELF): o voo do míssil entre a posição anterior e a de agora
// (driving/game/missile.h). Portado do MIPS; as contas em float seguem a FPU e a VU0 do EE (platform/ps2_float.h) na
// ordem das instruções; onde o original tem um caminho pela VU0 (o gerente +0x30) e outro pela FPU, as contas dão os
// mesmos bits e o port faz uma só. Conferido na parte 18 do nf_driving_check sobre o mundo de Paris.
//
// Divergência (a mesma das outras buscas do port, world_collision.cpp): as listas dinâmicas das células (o que anda; o
// port não as monta) não são lidas: uma célula com a lista não nula conta como guarda (nenhuma em Paris).

#include <cmath>
#include <cstring>
#include <vector>

#include "driving/game/missile.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

template <class T>
T* at(uint32_t a) {
    return nf::p32_ptr<T>(a);
}

// FUN_0025b890: ((x m0 + y m4) + z m8) + m12 por coluna; só o xyz é gravado.
void xform(const float src[3], const float m[16], float dst[3]) {
    const float s0 = src[0], s1 = src[1], s2 = src[2];
    float r[3];
    for (int c = 0; c < 3; ++c)
        r[c] = ee::add(ee::add(ee::add(ee::mul(s0, m[c]), ee::mul(s1, m[4 + c])), ee::mul(s2, m[8 + c])), m[12 + c]);
    dst[0] = r[0];
    dst[1] = r[1];
    dst[2] = r[2];
}

// O ponto pela matriz da VU0 (vmulax.xyz, vmadday.xyz, vmaddaz.xyz, vmaddw.xyz com o w de vf0 = 1): só o xyz muda.
void vu_xform(const float src[4], const float m[16], float dst[4]) {
    Reg v, r0, r1, r2, r3, acc, o;
    load(v, src);
    load(r0, m);
    load(r1, m + 4);
    load(r2, m + 8);
    load(r3, m + 12);
    load(o, dst);
    mulabc(acc, r0, v, BX, XYZ);
    maddabc(acc, r1, v, BY, XYZ);
    maddabc(acc, r2, v, BZ, XYZ);
    const uint32_t one[4] = {0, 0, 0, 0x3f800000};
    maddbc(o, acc, r3, one, BW, XYZ);
    store(dst, o);
}

float approx_distance(float dx, float dz) {
    const float a = ee::abs(dx), b = ee::abs(dz);
    return ee::lt(b, a) ? ee::add(a, ee::mul(b, 0.25f)) : ee::add(b, ee::mul(a, 0.25f));
}

// A distância ao quadrado da VU0 (vsub.xyz; vmul.xyz, vmulax.x, vmadday.x, vmaddz.x).
float vu_dist2(const float a[3], const float b[3]) {
    const uint32_t x = ps2::sub(ps2::fbits(a[0]), ps2::fbits(b[0]));
    const uint32_t y = ps2::sub(ps2::fbits(a[1]), ps2::fbits(b[1]));
    const uint32_t z = ps2::sub(ps2::fbits(a[2]), ps2::fbits(b[2]));
    uint32_t acc = ps2::mul(x, x);
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(y, y));
    return ps2::ffloat(ps2::madd(acc, 0x3f800000u, ps2::mul(z, z)));
}

// FUN_0025a8e8 (o produto vetorial da FPU): out = a x b.
void cross_fpu(const float a[3], const float b[3], float out[3]) {
    const float f7 = ee::mul(a[1], b[0]), f6 = ee::mul(a[0], b[2]), f3 = ee::mul(a[2], b[0]);
    const float f1 = ee::mul(a[0], b[1]), f2 = ee::mul(a[1], b[2]), f0 = ee::mul(a[2], b[1]);
    out[1] = ee::sub(f3, f6);
    out[2] = ee::sub(f1, f7);
    out[0] = ee::sub(f2, f0);
}

// A normal da face de um WWorldPos (o trecho comum do HeightAtPoint e do CheckHitWorld): (v1 - v0) x (v0 - v2)
// normalizada (o RSQRT da FPU), (0, 1, 0) se degenerada, virada para cima, o y no máximo 0,9999.
void face_normal(const WWorldPos& wp, float n[3]) {
    const float a[3] = {ee::sub(wp.v1[0], wp.v0[0]), ee::sub(wp.v1[1], wp.v0[1]), ee::sub(wp.v1[2], wp.v0[2])};
    const float b[3] = {ee::sub(wp.v0[0], wp.v2[0]), ee::sub(wp.v0[1], wp.v2[1]), ee::sub(wp.v0[2], wp.v2[2])};
    float c[3];
    cross_fpu(a, b, c);
    if (ee::eq(c[0], 0.0f) && ee::eq(c[1], 0.0f) && ee::eq(c[2], 0.0f)) {
        n[0] = 0.0f;
        n[1] = 1.0f;
        n[2] = 0.0f;
    } else {
        const float l = ee::add(ee::add(ee::mul(c[0], c[0]), ee::mul(c[1], c[1])), ee::mul(c[2], c[2]));
        const float r = ee::rsqrt(1.0f, l);
        n[0] = ee::mul(c[0], r);
        n[1] = ee::mul(c[1], r);
        n[2] = ee::mul(c[2], r);
    }
    if (ee::lt(n[1], 0.0f)) {
        n[1] = ee::neg(n[1]);
        n[0] = ee::neg(n[0]);
        n[2] = ee::neg(n[2]);
    }
    const float cap = ps2::ffloat(0x3f7ff972);  // 0,9999
    if (ee::le(cap, n[1])) n[1] = cap;
}

uint64_t g_guards = 0;

}  // namespace

// [driving] 0x002327a0 WWorldPos::HeightAtPoint(COORD3 &, bool): a altura do plano da face no ponto (0 sem face).
float WWorldPos_HeightAtPoint(const WWorldPos& wp, const float point[3]) {
    if (wp.valid == 0) return 0.0f;
    float n[3];
    face_normal(wp, n);
    return WWorldMath_GetPlaneY(n, wp.v0, point);
}

// [driving] 0x0021d298 WGrid::FindNodes(COORD4 *segmento, vector<unsigned int> &): as células que o segmento cruza no
// plano xz (o começo e o fim recortados à grade); acima de 100 (fgMapGroup) recomeça com as de raio 1 em volta do começo.
void WGrid_FindNodesSeg(const WGrid& g, const float* seg, std::vector<uint32_t>& cells) {
    float x0 = seg[0], z0 = seg[2], x1 = seg[4], z1 = seg[6];
    float dx = ee::sub(x1, x0), dz = ee::sub(z1, z0);
    auto cell = [&](float v, float origin) {
        return ee::cvt_w(ee::mul(ee::cvt_s(ee::cvt_w(ee::sub(v, origin))), g.inv_cell));
    };
    int32_t cx0 = cell(x0, g.origin[0]), cz0 = cell(z0, g.origin[2]);
    const int32_t cols = int32_t(g.columns), rows = int32_t(g.rows);
    const bool out0 = cx0 < 0 || cx0 >= cols || cz0 < 0 || cz0 >= rows;
    int32_t cx1 = cell(x1, g.origin[0]), cz1 = cell(z1, g.origin[2]);
    const bool out1 = cx1 < 0 || cx1 >= cols || cz1 < 0 || cz1 >= rows;
    const float small = ps2::ffloat(0x3d4ccccd);  // 0,05
    if (ee::lt(ee::abs(dx), small)) dx = small;
    if (ee::lt(ee::abs(dz), small)) dz = small;
    // A borda da grade no sentido de v: a última coluna (x n x tamanho + origem - 0,1) ou a primeira (origem + 0,1). O
    // n como unsigned na conversão (cvt.s.w com o bit de cima dividido: n nunca é negativo aqui).
    auto edge = [&](float v, uint32_t n, float origin) {
        if (ee::lt(0.0f, v)) return ee::sub(ee::add(ee::mul(ee::cvt_s(int32_t(n)), g.cell_size), origin), 0.1f);
        return ee::add(origin, 0.1f);
    };
    if (out0) {
        if (out1) return;
        const float ndx = ee::neg(dx), ndz = ee::neg(dz);
        const float ex = edge(ndx, g.columns, g.origin[0]);
        const float ez = edge(ndz, g.rows, g.origin[2]);
        const float tx = ee::div(ee::sub(ex, x1), ndx), tz = ee::div(ee::sub(ez, z1), ndz);
        if (ee::lt(tx, tz)) {
            x0 = ex;
            z0 = ee::add(z1, ee::mul(tx, ndz));
        } else {
            z0 = ez;
            x0 = ee::add(x1, ee::mul(tz, ndx));
        }
        cx0 = cell(x0, g.origin[0]);
        cz0 = cell(z0, g.origin[2]);
    } else if (out1) {
        const float ex = edge(dx, g.columns, g.origin[0]);
        const float ez = edge(dz, g.rows, g.origin[2]);
        const float tx = ee::div(ee::sub(ex, x0), dx), tz = ee::div(ee::sub(ez, z0), dz);
        if (ee::lt(tx, tz)) {
            x1 = ex;
            z1 = ee::add(z0, ee::mul(tx, dz));
        } else {
            z1 = ez;
            x1 = ee::add(x0, ee::mul(tz, dx));
        }
        cx1 = cell(x1, g.origin[0]);
        cz1 = cell(z1, g.origin[2]);
    }
    cells.push_back(uint32_t(cz0 * cols + cx0));
    int32_t count = 1;
    const float len = ee::sqrt(ee::add(ee::mul(dx, dx), ee::mul(dz, dz)));
    if (ee::le(len, g.cell_size)) {
        if (cx0 == cx1 && cz0 == cz1) return;
        if (cx0 == cx1 || cz0 == cz1) {
            cells.push_back(uint32_t(cz1 * cols + cx1));
            return;
        }
    }
    const int32_t kMaxCells = 100;  // fgMapGroup (0x33a5d0)
    auto restart = [&]() {
        cells.clear();
        WGrid_FindNodes(g, seg, 1.0f, cells);
    };
    const float uz = ee::div(dz, len), ux = ee::div(dx, len);  // f25, f24
    float px = x0, pz = z0;                                    // f22, f23
    int32_t ix = cx0, iz = cz0;                                // s1, s0
    const float eps = ps2::ffloat(0x38d1b717);
    const float kx = ee::le(eps, ee::abs(ux)) ? ee::div(1.0f, ux) : 10000.0f;  // f27
    const float kz = ee::le(eps, ee::abs(uz)) ? ee::div(1.0f, uz) : 10000.0f;  // f26
    const bool pos_x = ee::le(0.0f, ux), pos_z = ee::le(0.0f, uz);
    float nx = ee::mul(pos_x ? std::ceil(ee::mul(px, g.inv_cell)) : std::floor(ee::mul(px, g.inv_cell)), g.cell_size);
    float nz = ee::mul(pos_z ? std::ceil(ee::mul(pz, g.inv_cell)) : std::floor(ee::mul(pz, g.inv_cell)), g.cell_size);
    if (ix != cx1) {
        if (iz != cz1) {
            for (;;) {
                const float tx = ee::mul(ee::sub(nx, px), kx);
                const float tz = ee::mul(ee::sub(nz, pz), kz);
                if (ee::lt(tx, tz)) {
                    pz = ee::add(pz, ee::mul(tx, uz));
                    px = nx;
                    if (pos_x) {
                        ++ix;
                        nx = ee::add(px, g.cell_size);
                    } else {
                        --ix;
                        nx = ee::sub(px, g.cell_size);
                    }
                } else {
                    px = ee::add(px, ee::mul(tz, ux));
                    pz = nz;
                    if (pos_z) {
                        ++iz;
                        nz = ee::add(pz, g.cell_size);
                    } else {
                        --iz;
                        nz = ee::sub(pz, g.cell_size);
                    }
                }
                cells.push_back(uint32_t(iz * cols + ix));
                if (kMaxCells < ++count) {
                    restart();
                    return;
                }
                if (ix == cx1) break;
                if (iz == cz1) break;
            }
        }
        if (ix != cx1) {
            if (pos_x) {
                for (++ix; !(cx1 < ix); ++ix) {
                    cells.push_back(uint32_t(iz * cols + ix));
                    if (kMaxCells < ++count) {
                        restart();
                        return;
                    }
                }
            } else {
                for (--ix; !(ix < cx1); --ix) {
                    cells.push_back(uint32_t(iz * cols + ix));
                    if (kMaxCells < ++count) {
                        restart();
                        return;
                    }
                }
            }
            return;
        }
    }
    if (iz == cz1) return;
    if (pos_z) {
        for (++iz; !(cz1 < iz); ++iz) {
            cells.push_back(uint32_t(iz * cols + ix));
            if (kMaxCells < ++count) {
                restart();
                return;
            }
        }
    } else {
        for (--iz; !(iz < cz1); --iz) {
            cells.push_back(uint32_t(iz * cols + ix));
            if (kMaxCells < ++count) {
                restart();
                return;
            }
        }
    }
}

// [driving] 0x00217c88 WCollisionMgr::GetInstanceList(WCollisionInstanceCacheList &, COORD4 *) e 0x00217768
// GetInstanceListGuts(vector<unsigned int> &, ..., COORD4 *): as instâncias cuja esfera (o raio +0x3c no xz) chega ao
// ponto do segmento mais perto da linha 3 da inversa da matriz delas (o quirk: a inversa, não a posição) e, nas não
// inclinadas, cuja altura cobre a do segmento.
void WCollisionMgr_GetInstanceListSeg(WCollisionMgr& mgr, const WGrid& g, std::vector<InstanceListEntry>& list,
                                      const float* seg) {
    std::vector<uint32_t> cells;
    cells.reserve(64);
    WGrid_FindNodesSeg(g, seg, cells);
    if (list.capacity() < 32) list.reserve(32);
    const uint32_t stamp = ++mgr.query_stamp;
    const float d[3] = {ee::sub(seg[4], seg[0]), ee::sub(seg[5], seg[1]), ee::sub(seg[6], seg[2])};
    const float inv = ee::div(1.0f, ee::add(ee::add(ee::mul(d[0], d[0]), ee::mul(d[1], d[1])), ee::mul(d[2], d[2])));
    const float ylo = ee::lt(seg[1], seg[5]) ? seg[1] : seg[5];
    const float yhi = ee::lt(seg[5], seg[1]) ? seg[1] : seg[5];
    const uint32_t* nodes = at<uint32_t>(g.nodes);
    WCollisionInstance* insts = at<WCollisionInstance>(mgr.instances);
    for (uint32_t c : cells) {
        const WGridNode* node = at<WGridNode>(nodes[c]);
        if (!node) continue;
        if (node->dynamic != 0) ++g_guards;
        const uint16_t* idx =
            reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(node) + 16 + node->static_offset[0]);
        for (int i = 0; i < node->static_count[0]; ++i) {
            WCollisionInstance& ci = insts[idx[i]];
            if (ci.query_stamp == stamp) continue;
            ci.query_stamp = stamp;
            if (ci.article == 0) continue;
            const float r2 = ee::mul(ci.radius, ci.radius);
            float m[16];
            WCollisionInstance_MakeMatrix(mgr, ci, m, true);
            OrthoInverse(m);
            const float* p = m + 12;
            float t = ee::mul(ee::add(ee::add(ee::mul(ee::sub(p[0], seg[0]), d[0]), ee::mul(ee::sub(p[1], seg[1]), d[1])),
                                      ee::mul(ee::sub(p[2], seg[2]), d[2])),
                              inv);
            t = ee::min(t, 1.0f);
            t = ee::max(t, 0.0f);
            const float q[3] = {ee::add(seg[0], ee::mul(t, d[0])), ee::add(seg[1], ee::mul(t, d[1])),
                                ee::add(seg[2], ee::mul(t, d[2]))};
            const float ex = ee::sub(p[0], q[0]), ez = ee::sub(p[2], q[2]);
            if (!ee::lt(ee::add(ee::mul(ex, ex), ee::mul(ez, ez)), r2)) continue;
            if ((ci.flags & 3) == 0) {
                if (!ee::lt(ylo, ee::add(p[1], ci.half_height))) continue;
                if (!ee::lt(ee::sub(p[1], ci.half_height), yhi)) continue;
            }
            list.push_back({&ci, nullptr});
        }
    }
}

// [driving] 0x00219630 WCollisionMgr::GetBarrierNormal(WCollisionInstanceCacheList &, COORD4 *, WorldCollisionInfo &):
// a barreira das instâncias da lista que o segmento (levado ao referencial de cada uma) cruza mais perto do começo
// dentro da altura dela; o ponto e a normal (xz, virada para o começo) no mundo.
bool WCollisionMgr_GetBarrierNormalInst(const WCollisionMgr& mgr, const std::vector<InstanceListEntry>& list,
                                        const float* seg, WorldCollisionInfo& info) {
    float best = ps2::ffloat(0x7e967699u);  // 0x00399064: 1e38
    const CollisionBarrier* hit = nullptr;
    WCollisionInstance* hit_inst = nullptr;
    int32_t hit_index = 0;
    alignas(16) float hit_point[4] = {};
    for (const InstanceListEntry& e : list) {
        WCollisionInstance& ci = *e.instance;
        if (ci.article == 0) continue;
        const uint8_t* art = at<const uint8_t>(ci.article);
        const CollisionArticle& a = *reinterpret_cast<const CollisionArticle*>(art);
        if (a.barrier_count == 0) continue;
        float m[16];
        WCollisionInstance_MakeMatrix(mgr, ci, m, true);
        alignas(16) float local[8] = {};
        xform(seg, m, local);
        xform(seg + 4, m, local + 4);
        const CollisionBarrier* b = reinterpret_cast<const CollisionBarrier*>(art + 0x20 + a.barrier_offset);
        for (int k = 0; k < a.barrier_count; ++k, ++b) {
            if ((mgr.barrier_mask & b->subtype) != 0) continue;
            alignas(16) float p[4];
            if (!WWorldMath_SegmentIntersect(local, reinterpret_cast<const float*>(b), p)) continue;
            if (!ee::lt(b->y0, p[1])) continue;
            if (!ee::lt(p[1], b->y1)) continue;
            const float dd = approx_distance(ee::sub(p[0], local[0]), ee::sub(p[2], local[2]));
            if (!ee::lt(dd, best)) continue;
            best = dd;
            hit = b;
            hit_inst = &ci;
            hit_index = k;
            std::memcpy(hit_point, p, 16);
        }
    }
    info.hit = 0;
    if (!hit) return false;
    info.hit = 2;
    info.face_instance = nf::p32_u(hit_inst);
    info.flag52 = uint8_t((hit_inst->flags >> 1) & 1);
    float m[16];
    WCollisionInstance_MakeMatrix(mgr, *hit_inst, m, true);
    OrthoInverse(m);
    float p0[4], p1[4];
    xform(&hit->x0, m, p0);
    xform(&hit->x1, m, p1);
    std::memcpy(&p0[3], &hit->type, 4);
    p1[3] = hit->inv_length;
    std::memcpy(info.barrier.p0, p0, 16);
    std::memcpy(info.barrier.p1, p1, 16);
    info.barrier.instance = nf::p32_u(hit_inst);
    info.barrier.index = uint32_t(hit_index);
    xform(hit_point, m, info.point);
    info.point[3] = 1.0f;
    const float* s1 = &info.barrier.p0[0];
    info.normal[0] = ee::mul(ee::sub(s1[6], s1[2]), s1[7]);
    info.normal[1] = 0.0f;
    info.normal[2] = ee::mul(ee::neg(ee::sub(s1[4], info.barrier.p0[0])), s1[7]);
    info.normal[3] = 0.0f;
    uint8_t tag[4];
    std::memcpy(tag, &info.barrier.p0[3], 4);
    info.face_type = tag[0];
    info.face_subtype = tag[1];
    const float dx = ee::sub(seg[0], info.point[0]), dz = ee::sub(seg[2], info.point[2]);
    if (ee::lt(ee::add(ee::mul(info.normal[0], dx), ee::mul(info.normal[2], dz)), 0.0f)) {
        info.normal[0] = ee::neg(info.normal[0]);
        info.normal[2] = ee::neg(info.normal[2]);
    }
    return info.hit != 0;
}

// [driving] 0x00218150 WCollisionMgr::GetObjectLists(WCollisionObjectList &, WCollisionObjectList &, COORD4 *) e
// 0x00217e30 GetObjectListsGuts: os objetos das células do segmento cuja esfera chega ao meio dele (o raio, a metade do
// comprimento): os cilindros numa lista, as caixas na outra.
void WCollisionMgr_GetObjectListsSeg(const TrackWorld& world, std::vector<WCollisionObject*>& cylinders,
                                     std::vector<WCollisionObject*>& boxes, const float* seg) {
    std::vector<uint32_t> cells;
    cells.reserve(64);
    WGrid_FindNodesSeg(const_cast<TrackWorld&>(world).grid(), seg, cells);
    float mid[3] = {ee::add(seg[0], seg[4]), ee::add(seg[1], seg[5]), ee::add(seg[2], seg[6])};
    for (float& v : mid) v = ee::mul(v, 0.5f);
    const float ex = ee::sub(mid[0], seg[0]), ey = ee::sub(mid[1], seg[1]), ez = ee::sub(mid[2], seg[2]);
    const float radius = ee::sqrt(ee::add(ee::add(ee::mul(ex, ex), ee::mul(ey, ey)), ee::mul(ez, ez)));
    if (cylinders.capacity() < 16) cylinders.reserve(16);
    if (boxes.capacity() < 16) boxes.reserve(16);
    WCollisionObject* objs = world.objects();
    for (uint32_t c : cells) {
        const WGridNode* node = world.node(c);
        if (!node) continue;
        if (node->dynamic != 0) ++g_guards;
        const uint16_t* idx =
            reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(node) + 16 + node->static_offset[2]);
        for (int i = 0; i < node->static_count[2]; ++i) {
            WCollisionObject* o = &objs[idx[i]];
            const float d = approx_distance(ee::sub(o->position[0], mid[0]), ee::sub(o->position[2], mid[2]));
            if (!ee::lt(d, ee::add(radius, o->radius))) continue;
            if (o->is_cylinder) cylinders.push_back(o);
            else boxes.push_back(o);
        }
    }
}

// [driving] 0x0022f6d8 FasterSegmentIntersect(COORD4 *seg, COORD4 *aresta, COORD4 *saída): os dois segmentos se cruzam
// no xz (t no segmento em [0, 1], u na aresta em [-0, 1]); o ponto no segmento (xyz; o w da saída fica).
bool FasterSegmentIntersect(const float* s, const float* e, float* out) {
    const float f8 = ee::sub(e[6], e[2]);
    const float f10 = ee::sub(s[4], s[0]);
    const float f9 = ee::sub(s[6], s[2]);
    const float f2 = ee::sub(e[4], e[0]);
    const float denom = ee::sub(ee::mul(f8, f10), ee::mul(f2, f9));
    if (ee::eq(denom, 0.0f)) return false;
    const float f4 = ee::sub(s[2], e[2]);
    const float f5 = ee::sub(s[0], e[0]);
    const float inv = ee::div(1.0f, denom);
    const float t = ee::mul(ee::sub(ee::mul(f2, f4), ee::mul(f8, f5)), inv);
    if (!ee::le(0.0f, t) || !ee::le(t, 1.0f)) return false;
    const float u = ee::mul(ee::sub(ee::mul(f10, f4), ee::mul(f9, f5)), inv);
    if (!ee::le(ps2::ffloat(0x80000000u), u) || !ee::le(u, 1.0f)) return false;
    if (out) {
        Reg v1, v2, v3, v4, v5;
        load(v1, s);
        load(v2, s + 4);
        load(v5, out);
        v4[0] = ps2::fbits(t);
        sub(v3, v2, v1, XYZ);
        mulbc(v3, v3, v4, BX, XYZ);
        add(v5, v1, v3, XYZ);
        store(out, v5);
    }
    return true;
}

// [driving] 0x00218d10 WCollisionMgr::GetOBBObjectIntersection(COORD4 *seg, MATRIX4 &, COORD4 &meias, COORD4 &saída):
// o segmento no referencial da caixa contra o topo e o fundo (y = ±meia) e as quatro arestas verticais (no xz).
bool WCollisionMgr_GetOBBObjectIntersection(const float* seg, const float m[16], const float half[4], float* out) {
    const float hx = half[0], hy = half[1], hz = half[2];
    const float nhx = ee::neg(hx), nhz = ee::neg(hz), nhy = ee::neg(hy);
    alignas(16) float c[5][4] = {{nhx, hy, hz, 1.0f}, {hx, hy, hz, 1.0f}, {hx, nhy, nhz, 1.0f},
                                 {nhx, nhy, nhz, 1.0f}, {nhx, hy, hz, 1.0f}};
    alignas(16) float inv[16];
    std::memcpy(inv, m, 64);
    OrthoInverse(inv);
    alignas(16) float ls[8] = {};
    vu_xform(seg, inv, ls);
    vu_xform(seg + 4, inv, ls + 4);
    const float y0 = ls[1], y1 = ls[5];
    bool planes = false;
    if (ee::lt(hy, y0) && ee::lt(y1, hy)) planes = true;
    else if (ee::lt(hy, y1) && ee::lt(y0, hy)) planes = true;
    else if (ee::lt(y0, nhy) && ee::lt(nhy, y1)) planes = true;
    else if (ee::lt(y1, nhy) && ee::lt(nhy, y0)) planes = true;
    if (planes) {
        const float up[4] = {0.0f, 1.0f, 0.0f, 1.0f};  // 0x00399050
        alignas(16) float top[4] = {}, bot[4] = {};
        float t = 0;
        const bool a = WWorldMath_IntersectSegPlane(ls, ls + 4, c[0], up, top, t);
        const bool b = WWorldMath_IntersectSegPlane(ls, ls + 4, c[2], up, bot, t);
        const float* p = nullptr;
        if (!a) {
            p = b ? bot : nullptr;
        } else if (!b) {
            p = top;
        } else {
            const float dt = ee::abs(ee::sub(top[1], y0)), db = ee::abs(ee::sub(bot[1], y0));
            p = ee::lt(dt, db) ? top : bot;
        }
        if (p && ee::lt(ee::abs(p[0]), hx) && ee::lt(ee::abs(p[2]), hz)) {
            vu_xform(p, m, out);
            return true;
        }
    }
    alignas(16) float hits[4][4] = {};
    uint32_t mask = 0;
    for (int k = 0; k < 4; ++k)
        if (FasterSegmentIntersect(ls, c[k], hits[k])) mask |= 1u << k;
    if (mask == 0) return false;
    // A tabela 0x33a548: os dois cruzamentos de cada máscara (-1 nenhum).
    static const int8_t kPairs[16][2] = {{-1, 0}, {0, -1}, {1, -1}, {0, 1}, {2, -1}, {0, 2}, {1, 2}, {-1, 0},
                                         {3, -1}, {0, 3}, {1, 3}, {-1, 0}, {2, 3}, {-1, 0}, {-1, 0}, {-1, 0}};
    const int first = kPairs[mask][0], second = kPairs[mask][1];
    alignas(16) float pick[4];
    if (second == -1) {
        if (first < 0) {
            ++g_guards;  // o PS2 leria a entrada -1 (fora da pilha das arestas)
            return false;
        }
        std::memcpy(pick, hits[first], 16);
    } else {
        alignas(16) float w0[4] = {}, w1[4] = {};
        if (first < 0) {
            ++g_guards;
            return false;
        }
        vu_xform(hits[first], m, w0);
        vu_xform(hits[second], m, w1);
        const float d0 = vu_dist2(seg, w0), d1 = vu_dist2(seg, w1);
        std::memcpy(pick, ee::lt(d0, d1) ? hits[first] : hits[second], 16);
    }
    if (!ee::le(0.0f, pick[1])) return false;
    if (!ee::lt(pick[1], ee::add(hy, hy))) return false;
    vu_xform(pick, m, out);
    out[3] = 1.0f;
    return true;
}

// [driving] 0x00218960 WCollisionMgr::GetOBBObjectNormal(COORD4 *, WCollisionObjectList &, WorldCollisionInfo &) e
// 0x00219280 GetClosestIntersectingOBBObject: a primeira caixa da lista que o segmento cruza; a normal do centro dela
// ao ponto no xz (virada para o começo), info.hit = 3.
bool WCollisionMgr_GetOBBObjectNormal(const TrackWorld& world, const float* seg,
                                      const std::vector<WCollisionObject*>& list, WorldCollisionInfo& info) {
    info.hit = 0;
    WCollisionObject* found = nullptr;
    for (WCollisionObject* o : list) {
        float m[4][4];
        WCollisionObject_MakeMatrix(world, *o, m, true);
        if (WCollisionMgr_GetOBBObjectIntersection(seg, &m[0][0], o->half, info.point)) {
            found = o;
            break;
        }
    }
    info.object = found ? nf::p32_u(found) : 0;
    if (!found) return false;
    if (found->flags & 1) info.flag52 = 1;
    if (found->is_cylinder == 0) {
        info.normal[1] = 0.0f;
        const float nx = ee::sub(info.point[0], found->position[0]);
        const float ny = info.normal[1];
        info.normal[0] = nx;
        info.normal[3] = 1.0f;
        const float nz = ee::sub(info.point[2], found->position[2]);
        info.normal[2] = nz;
        const float l = ee::add(ee::add(ee::mul(nx, nx), ny), ee::mul(nz, nz));
        const float r = ee::rsqrt(1.0f, l);
        info.normal[2] = ee::mul(nz, r);
        info.normal[0] = ee::mul(nx, r);
        info.normal[1] = ee::mul(r, ny);
        const float dx = ee::sub(seg[0], info.point[0]), dz = ee::sub(seg[2], info.point[2]);
        if (ee::lt(ee::add(ee::mul(info.normal[0], dx), ee::mul(info.normal[2], dz)), 0.0f)) {
            info.normal[0] = ee::neg(info.normal[0]);
            info.normal[2] = ee::neg(info.normal[2]);
        }
    }
    info.face_type = found->face_type;
    info.face_subtype = found->face_subtype;
    info.hit = 3;
    return true;
}

// [driving] 0x00215cc8 WCollisionMgr::CheckHitWorld(COORD4 *segmento, WorldCollisionInfo &): o choque mais perto do
// começo entre as barreiras e o chão das instâncias perto do segmento, depois os cilindros e as caixas até onde ele
// chegou. Sem mundo, nada.
bool WCollisionMgr_CheckHitWorld(const float* segment, WorldCollisionInfo& info) {
    WorldCollision* wc = world();
    TrackWorld* tw = wc ? wc->track_world() : nullptr;
    if (!tw) {
        ++g_guards;
        return false;
    }
    WCollisionMgr& mgr = tw->mgr();
    alignas(16) float seg[3][4];
    std::memcpy(seg[0], segment, 16);
    std::memcpy(seg[1], segment + 4, 16);
    const float len = ps2::ffloat(ps2::sqrt(ps2::fbits(vu_dist2(seg[0], seg[1]))));
    if (ee::eq(len, 0.0f)) return false;
    if (ee::lt(len, 0.1f)) {
        const float k = ee::div(0.25f, len);
        for (int i = 0; i < 3; ++i) seg[1][i] = ee::add(seg[0][i], ee::mul(ee::sub(seg[1][i], seg[0][i]), k));
    }
    for (int i = 0; i < 3; ++i) seg[2][i] = ee::mul(ee::add(seg[0][i], seg[1][i]), 0.5f);
    seg[2][3] = ee::mul(len, 0.5f);
    std::vector<InstanceListEntry> list;
    WCollisionMgr_GetInstanceListSeg(mgr, tw->grid(), list, &seg[0][0]);
    int found = 0;
    if (WCollisionMgr_GetBarrierNormalInst(mgr, list, &seg[0][0], info)) found = 2;
    if (found) std::memcpy(seg[1], info.point, 16);
    WWorldPos wp{};
    WWorldPos_FindClosestFaceSeg(wp, mgr, list, seg[0], seg[1], simulation().steps);
    if (wp.valid) {
        WorldCollisionInfo face;
        WorldCollisionInfo_Init(face);
        face.hit = 1;
        float n[3];
        face_normal(wp, n);
        face.normal[0] = n[0];
        face.normal[1] = n[1];
        face.normal[2] = n[2];
        face.normal[3] = 1.0f;
        float t = 0;
        if (WWorldMath_IntersectSegPlane(seg[0], seg[1], wp.v0, face.normal, face.point, t)) {
            face.face_type = wp.surface[0];
            face.face_subtype = wp.surface[1];
            face.face_instance = wp.instance;
            face.flag52 = uint8_t((at<const uint32_t>(wp.instance)[6] >> 1) & 1);
            const float d[3] = {ee::sub(seg[0][0], face.point[0]), ee::sub(seg[0][1], face.point[1]),
                                ee::sub(seg[0][2], face.point[2])};
            Reg vn, vd, vt;
            load(vn, face.normal);
            const float d4[4] = {d[0], d[1], d[2], 0.0f};
            load(vd, d4);
            mul(vt, vn, vd, XYZ);
            addbc(vt, vt, vt, BY, X);
            addbc(vt, vt, vt, BZ, X);
            if (ee::lt(ps2::ffloat(vt[0]), 0.0f)) {
                // vsub.xyz vf1, vf0, vf1: 0 - n na VU (o zero fica +0, não -0).
                for (int i = 0; i < 3; ++i) face.normal[i] = ps2::ffloat(ps2::sub(0u, ps2::fbits(face.normal[i])));
            }
            if (found == 0 || ee::lt(vu_dist2(seg[0], face.point), vu_dist2(seg[0], info.point))) {
                info = face;
                found = 1;
            }
        }
    }
    if (found) std::memcpy(seg[1], info.point, 16);
    std::vector<WCollisionObject*> cyl, box;
    WCollisionMgr_GetObjectListsSeg(*tw, cyl, box, &seg[0][0]);
    WorldCollisionInfo obj;
    WorldCollisionInfo_Init(obj);
    bool hit = false;
    if (!cyl.empty()) hit = WCollisionMgr_GetCylObjectNormal(&seg[0][0], cyl, obj);
    if (!hit && !box.empty()) hit = WCollisionMgr_GetOBBObjectNormal(*tw, &seg[0][0], box, obj);
    if (hit) {
        if (found == 0 || ee::lt(vu_dist2(seg[0], obj.point), vu_dist2(seg[0], info.point))) info = obj;
    }
    return info.hit != 0;
}

uint64_t world_hit_guards() { return g_guards; }

}  // namespace drv
