#include "driving/fx/decals.h"

#include <cstring>

#include "driving/game/missile.h"  // WWorldPos_HeightAtPoint
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

DecalState g_state;
DecalStats g_stats;

// FUN_0025a8e8 (o produto vetorial da FPU), como o do WWorldPos::HeightAtPoint (physics/world_hit.cpp).
void cross_fpu(const float a[3], const float b[3], float out[3]) {
    const float f7 = ee::mul(a[1], b[0]), f6 = ee::mul(a[0], b[2]), f3 = ee::mul(a[2], b[0]);
    const float f1 = ee::mul(a[0], b[1]), f2 = ee::mul(a[1], b[2]), f0 = ee::mul(a[2], b[1]);
    out[1] = ee::sub(f3, f6);
    out[2] = ee::sub(f1, f7);
    out[0] = ee::sub(f2, f0);
}

}  // namespace

DecalState& decal_state() { return g_state; }
DecalStats& decal_stats() { return g_stats; }

// [driving] 0x001da030 RDecalManager::RDecalManager
void RDecalManager_Construct(DecalState& s, const std::vector<uint8_t>& decal_dat) {
    s.m = RDecalManagerData();
    s.m.vtable = 0x0038b840;
    s.m.next = 0;
    // As cores (0x3f7f7f7f), os atrasos e os alfas; as posições e o STQ zerados (oito blocos de 0x48 vértices).
    for (uint32_t& c : s.b.colours) c = 0x3f7f7f7fu;
    std::memset(s.b.delay, 0, sizeof s.b.delay);
    std::memset(s.b.alpha, 0, sizeof s.b.alpha);
    std::memset(s.b.coords, 0, sizeof s.b.coords);
    std::memset(s.b.stq, 0, sizeof s.b.stq);
    // [driving] 0x001da3b0 RDecalLibrary::LoadData: o "data\render\decal.dat" (0x280 bytes) ou zeros.
    if (decal_dat.size() >= sizeof s.m.types) std::memcpy(s.m.types, decal_dat.data(), sizeof s.m.types);
    else std::memset(s.m.types, 0, sizeof s.m.types);
    s.ready = true;
}

// [driving] 0x001da6c8 RDecalManager::AddDecal(COORD4 &, COORD4 &, COORD4 &, DecalType, float, float)
void RDecalManager_AddDecal(DecalState& s, const float pos[4], const float normal[4], const float dir[4], int32_t type,
                            float w, float h) {
    if (type < 0 || type > 7) {
        ++g_stats.guards;  // o PS2 leria além dos oito tipos
        return;
    }
    RDecalManagerData& m = s.m;
    const DecalType& t = m.types[type];
    const int32_t i = m.next;
    s.b.delay[i] = t.delay != 0 ? uint8_t(t.delay) : uint8_t(1);
    s.b.alpha[i] = uint8_t(t.alpha);
    for (int k = 0; k < 6; ++k) s.b.colours[i * 6 + k] = 0x007f7f7fu;
    // Os lados: a = dir x normal, b = a x normal (o w 1 do vf0 x vf0), vezes w e h; o w deles zerado.
    Reg one{}, vdir, vnorm, acc, a, b, sw, sh;
    const uint32_t vf0[4] = {0, 0, 0, 0x3f800000u};
    mul(one, vf0, vf0, XYZW);
    load(vdir, dir);
    load(vnorm, normal);
    std::memcpy(a, one, 16);
    opmula(acc, vdir, vnorm);
    opmsub(a, acc, vnorm, vdir);
    std::memcpy(b, one, 16);
    opmula(acc, a, vnorm);
    opmsub(b, acc, vnorm, a);
    const uint32_t ws[4] = {ps2::fbits(w), 0, 0, 0}, hs[4] = {ps2::fbits(h), 0, 0, 0};
    mulbc(sw, a, ws, BX, XYZW);
    mulbc(sh, b, hs, BX, XYZW);
    sw[3] = 0;
    sh[3] = 0;
    // O canto: pos - a / 2 - b / 2, com w = 1.
    const uint32_t half[4] = {0xbf000000u, 0, 0, 0};
    Reg vpos, t3, c;
    load(vpos, pos);
    mulbc(t3, sw, half, BX, XYZW);
    add(c, vpos, t3, XYZW);
    mulbc(t3, sh, half, BX, XYZW);
    add(c, c, t3, XYZW);
    c[3] = 0x3f800000u;
    float(*v)[4] = &s.b.coords[i * 6];
    Reg r;
    store(v[0], c);
    add(r, c, sw, XYZW);
    store(v[1], r);
    add(r, c, sh, XYZW);
    store(v[2], r);
    std::memcpy(v[3], v[1], 16);
    std::memcpy(v[4], v[2], 16);
    Reg v1;
    load(v1, v[1]);
    add(r, v1, sh, XYZW);
    store(v[5], r);
    // O STQ: os cantos 0, 1, 2, 1, 2 e 3 do tipo.
    float(*q)[4] = &s.b.stq[i * 6];
    std::memcpy(q[0], t.stq[0], 16);
    std::memcpy(q[1], t.stq[1], 16);
    std::memcpy(q[2], t.stq[2], 16);
    std::memcpy(q[3], t.stq[1], 16);
    std::memcpy(q[4], t.stq[2], 16);
    std::memcpy(q[5], t.stq[3], 16);
    m.next = i + 1 < kDecalCount ? i + 1 : 0;
    ++g_stats.added;
}

// [driving] 0x001da978 RDecalManager::Draw: o atraso de cada marca; no fim dele, o alfa do tipo nos seis vértices (o
// RGB do primeiro vértice). Os oito DrawArray (0x002851f8 entre o 0x00286018 e o 0x00285388) ficam com o desenho.
void RDecalManager_Draw(DecalState& s) {
    for (int i = 0; i < kDecalCount; ++i) {
        if (s.b.delay[i] == 0) continue;
        if (--s.b.delay[i] != 0) continue;
        const uint32_t c = (uint32_t(s.b.alpha[i]) << 24) + s.b.colours[i * 6];
        for (int k = 5; k >= 0; --k) s.b.colours[i * 6 + k] = c;
    }
    ++g_stats.draws;
}

// [driving] 0x001eb078 RScorchWorld::ScorchGround(WWorldPos &, COORD4 &, float, DecalType)
bool RScorchWorld_ScorchGround(DecalState& s, WorldCollision* wc, WWorldPos& wp, const float pos[4], int32_t type,
                               float size) {
    ++g_stats.scorches;
    if (!wc) {
        ++g_stats.guards;  // sem mundo, sem face
        return false;
    }
    wc->find_closest_face(wp, pos, true);  // 0x002322c8 WWorldPos::FindClosestFace(COORD3 &, bool)
    if ((wp.surface[1] & 2) != 0 || wp.surface[0] == 6) {
        ++g_stats.refused_face;
        return false;
    }
    if (wp.instance != 0) {
        const WCollisionInstance* ci = nf::p32_ptr<const WCollisionInstance>(wp.instance);
        if (ci->article == 0 || nf::p32_ptr<const uint8_t>(ci->article)[0xc] != 0) {
            if (ci->article == 0) ++g_stats.guards;  // o PS2 leria o byte +0xc do endereço 0
            ++g_stats.refused_face;
            return false;
        }
    }
    float half = ee::mul(size, 0.5f);
    if (ee::lt(6.0f, half)) half = 6.0f;
    const float height = ee::sub(pos[1], WWorldPos_HeightAtPoint(wp, pos));  // f25
    // A normal da face (a do HeightAtPoint), (0, 1, 0) sem face.
    alignas(16) float n[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    if (wp.valid != 0) {
        const float e1[3] = {ee::sub(wp.v1[0], wp.v0[0]), ee::sub(wp.v1[1], wp.v0[1]), ee::sub(wp.v1[2], wp.v0[2])};
        const float e2[3] = {ee::sub(wp.v0[0], wp.v2[0]), ee::sub(wp.v0[1], wp.v2[1]), ee::sub(wp.v0[2], wp.v2[2])};
        float c[3];
        cross_fpu(e1, e2, c);
        if (!(ee::eq(c[0], 0.0f) && ee::eq(c[1], 0.0f) && ee::eq(c[2], 0.0f))) {
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
    n[3] = 0.0f;
    if (ee::lt(n[1], ps2::ffloat(0x3f733333))) {  // 0,95
        ++g_stats.refused_slope;
        return false;
    }
    bool ok = false;
    if (wp.valid == 1) {
        if (type == 4) {
            ok = true;
        } else {
            // Os quatro pontos a meio lado: face achada e altura a menos de 0,15 da do centro.
            float p[3] = {pos[0], pos[1], pos[2]};
            const float tol = ps2::ffloat(0x3e19999a);  // 0,15
            const float h0 = WWorldPos_HeightAtPoint(wp, p);
            const float full = ee::add(half, half);
            auto probe = [&](int axis, float delta, bool back) {
                p[axis] = back ? ee::sub(p[axis], delta) : ee::add(p[axis], delta);
                wc->find_closest_face(wp, p, true);
                if (wp.valid != 1) return false;
                return ee::lt(ee::abs(ee::sub(h0, WWorldPos_HeightAtPoint(wp, p))), tol);
            };
            ok = probe(0, half, false) && probe(0, full, true) && probe(2, half, false) && probe(2, full, true) &&
                 wp.surface[0] != 9;
        }
    }
    if (!ok) {
        ++g_stats.refused_edges;
        return false;
    }
    const float side = ee::sub(half, ee::mul(height, 0.5f));
    if (!ee::lt(0.0f, side)) {
        ++g_stats.refused_size;
        return false;
    }
    alignas(16) float at[4];
    std::memcpy(at, pos, 16);
    at[1] = ee::add(ee::sub(at[1], height), ps2::ffloat(0x3ccccccd));  // 0,025 acima da face
    alignas(16) const float dir[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    const float w = ee::add(side, side);
    RDecalManager_AddDecal(s, at, n, dir, type, w, w);
    return true;
}

}  // namespace drv
