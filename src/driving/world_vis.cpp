// A visibilidade do mundo (driving/world_vis.h): o frustum 2D, as cortinas, a árvore de cenas, o corte das instâncias e
// a ordem do desenho do WRender, portados do MIPS do DRIVING.ELF com a aritmética do EE (platform/ps2_float.h) e a da
// VU0 nas instruções de macro (platform/vu0_macro.h), na ordem do código.

#include "driving/world_vis.h"

#include "driving/fx/gfx_gallery.h"

#include <algorithm>
#include <cstring>

#include "driving/carp_runtime.h"
#include "driving/physics/vu0_physics.h"
#include "driving/real/vu0_math.h"
#include "driving/vu0_culling.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

float g_cull_distance = 400.0f;  // 0x333ef8 (0x43c80000 no .data)
int32_t g_num_curtains = 0;      // não iniciado numa pista sem 'VisC' (o PS2 o deixa como estava)
WRender* g_render = nullptr;
uint32_t g_world_map = 0;
CachedDrawInfo g_scratchpad{};
CachedDrawInfo* g_current = &g_scratchpad;
WCullCursor g_cursor;
uint64_t g_guards = 0;

// DAT_003999d8: os passos das medidas empacotadas (0,25 e 16, pelo bit 30).
const float kDimensionStep[2] = {0.25f, 16.0f};

float fbits_f(uint32_t u) { return ps2::ffloat(u); }
uint32_t fb(float f) { return ps2::fbits(f); }

// ACC = a0 b0; ACC += a1 b1; ACC += a2 b2; d = ACC - 1 x a3 (mula.S/madda.S/msub.S do EE).
float dot3_minus(const float a[4], const float p[4]) {
    uint32_t acc = ps2::mul(fb(p[0]), fb(a[0]));
    acc = ps2::madd(acc, fb(p[1]), fb(a[1]));
    acc = ps2::madd(acc, fb(p[2]), fb(a[2]));
    return fbits_f(ps2::msub(acc, fb(1.0f), fb(a[3])));
}

}  // namespace

// [driving] 0x001d4fc0 (os construtores globais de RViewCamera::fCulling): RRenderWorldCulling(33, 400) na partida.
RRenderWorldCulling& fgWorldCulling() {
    static RRenderWorldCulling c = [] {
        RRenderWorldCulling x{};
        RRenderWorldCulling_Construct(x, 33.0f, 400.0f);
        return x;
    }();
    return c;
}
float& CullDistance() { return g_cull_distance; }
int32_t& numActiveCurtains() { return g_num_curtains; }
WRender*& fgRender() { return g_render; }
uint32_t& fgWorldMap() { return g_world_map; }
CachedDrawInfo& scratchpad_list() { return g_scratchpad; }
CachedDrawInfo*& CurrentList() { return g_current; }
WCullCursor& Cursor() { return g_cursor; }
uint64_t& world_vis_guards() { return g_guards; }

// ------------------------------------------------------------------------------------------------------ o frustum

// [driving] 0x001c6588 RRenderWorldCulling::RRenderWorldCulling(float, float)
void RRenderWorldCulling_Construct(RRenderWorldCulling& c, float fov, float range) { InitializePlaneInfo(c, fov, range); }

// [driving] 0x001c6b70 RRenderWorldCulling::InitializePlaneInfo
void InitializePlaneInfo(RRenderWorldCulling& c, float fov, float range) {
    c.fov = fov;                                              // +0x64
    const float turns = ee::mul(fov, fbits_f(0x3b360b61));    // fov x (1/360)
    c.range = range;                                          // +0x68
    c.cosine = VU0_Cos(turns);                                // +0x6c
    const float s = VU0_Sin(turns);
    c.sine = s;                                               // +0x8c
    const float r = c.range, co = c.cosine;
    const float ex = ee::mul(s, r);                           // sen x alcance
    const float ez = ee::sub(r, ee::mul(co, r));              // alcance - cos x alcance
    // O comprimento na VU0 em macro: vmul.xyz vf2 = vf1 x vf1; vmulax.x; vmadday.x; vmaddz.x; vsqrt; vaddq.x.
    uint32_t v1[4] = {fb(ex), fb(ez), 0, 0}, v2[4] = {0, 0, 0, 0}, v3[4] = {fb(1.0f), 0, 0, 0}, acc[4] = {0, 0, 0, 0};
    mul(v2, v1, v1, XYZ);
    mula(acc, v1, v1, X);
    maddabc(acc, v3, v2, BY, X);
    maddbc(v2, acc, v3, v2, BZ, X);
    const uint32_t q = ps2::sqrt(v2[0]);
    const float len = fbits_f(ps2::add(0, q));                // vaddq.x vf3, vf0, Q
    const float inv = ee::div(1.0f, len);
    c.edge_length = len;                                      // +0x60
    c.centre_local[0] = 0.0f;                                 // +0x80
    c.centre_local[1] = 0.0f;                                 // +0x84
    c.centre_local[2] = ee::mul(r, 0.5f);                     // +0x88
    c.edge_z = ee::mul(ez, inv);                              // +0x7c
    c.edge_x = ee::mul(ex, inv);                              // +0x90
}

// [driving] 0x001c6858 RRenderWorldCulling::Setup2dFrustrum. Divergência: o atan2f do newlib é o nf::fm::atan2.
void Setup2dFrustrum(RRenderWorldCulling& c, const float position[4], const float matrix[16], float fov, float range) {
    if (!(ee::eq(c.range, range) && ee::eq(c.fov, fov))) InitializePlaneInfo(c, fov, range);
    c.eye_y = position[1];                                    // +0x9c
    float row2[4];
    std::memcpy(row2, matrix + 8, 16);
    std::memcpy(c.eye, position, 16);                         // +0x50 (lq/sq: os 16 bytes)
    const float heading = nf::fm::atan2(row2[0], row2[2]);
    const float ez = c.edge_z, co = c.cosine, s = c.sine, ex = c.edge_x;
    const float turns = ee::mul(heading, fbits_f(0x3e22f983));  // 1 / (2 pi)
    float(*p)[4] = c.planes;
    p[0][1] = 0.0f;
    p[1][1] = 0.0f;
    p[2][1] = 0.0f;
    p[3][1] = 0.0f;
    p[4][1] = 0.0f;
    p[4][0] = 0.0f;
    p[0][0] = ee::neg(ez);
    p[1][0] = ez;
    p[1][2] = ex;
    p[2][0] = ee::neg(co);
    p[3][0] = co;
    p[3][2] = ee::neg(s);
    p[4][2] = -1.0f;
    p[0][2] = ex;
    p[2][2] = ee::neg(s);
    float rot[4][4];
    VU0_MATRIX4setyrot(rot, turns);
    uint32_t R[4][4];
    std::memcpy(R, rot, sizeof R);
    auto rotate = [&](float v[4]) {                           // vmulax/vmadday/vmaddz.xyz (o w fica)
        uint32_t a[4], acc[4] = {0, 0, 0, 0}, o[4];
        std::memcpy(a, v, 16);
        std::memcpy(o, v, 16);
        mulabc(acc, R[0], a, BX, XYZ);
        maddabc(acc, R[1], a, BY, XYZ);
        maddbc(o, acc, R[2], a, BZ, XYZ);
        std::memcpy(v, o, 16);
    };
    {
        float v[4] = {c.centre_local[0], c.centre_local[1], c.centre_local[2], c.sine};
        float out[4] = {c.centre[0], c.centre[1], c.centre[2], c.edge_z};
        uint32_t a[4], acc[4] = {0, 0, 0, 0}, o[4];
        std::memcpy(a, v, 16);
        std::memcpy(o, out, 16);
        mulabc(acc, R[0], a, BX, XYZ);
        maddabc(acc, R[1], a, BY, XYZ);
        maddbc(o, acc, R[2], a, BZ, XYZ);
        std::memcpy(c.centre, o, 12);                         // +0x70 (o w, +0x7c, fica)
    }
    c.centre[0] = ee::add(c.centre[0], position[0]);
    c.centre[2] = ee::add(c.centre[2], position[2]);
    for (int i = 0; i < 5; ++i) rotate(p[i]);
    // O olho recuado: |frente.y| x -alcance x 0,2 na direção girada (a linha 2 da rotação).
    const float back = ee::mul(ee::mul(ee::abs(matrix[9]), ee::neg(range)), fbits_f(0x3e4ccccd));
    {
        uint32_t b4[4] = {fb(back), 0, 0, 0}, e[4], t[4];
        std::memcpy(e, c.eye, 16);
        std::memcpy(t, c.eye, 16);
        mulbc(t, R[2], b4, BX, XYZ);
        add(t, e, t, XYZ);
        std::memcpy(c.eye, t, 16);
    }
    float far[4];
    {
        uint32_t r4[4] = {fb(range), 0, 0, 0}, e[4], t[4];
        std::memcpy(e, c.eye, 16);
        std::memcpy(t, R[2], 16);
        mulbc(t, R[2], r4, BX, XYZ);
        add(t, e, t, XYZ);
        std::memcpy(far, t, 16);
    }
    p[0][3] = ee::add(ee::mul(far[0], p[0][0]), ee::mul(far[2], p[0][2]));
    p[1][3] = ee::add(ee::mul(far[0], p[1][0]), ee::mul(far[2], p[1][2]));
    p[2][3] = ee::add(ee::mul(c.eye[0], p[2][0]), ee::mul(c.eye[2], p[2][2]));
    p[3][3] = ee::add(ee::mul(c.eye[0], p[3][0]), ee::mul(c.eye[2], p[3][2]));
    p[4][3] = ee::add(ee::mul(c.eye[0], p[4][0]), ee::mul(c.eye[2], p[4][2]));
    // A transposição dos quatro primeiros (pextlw/pextuw/pcpyld/pcpyud).
    float t[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) t[j][i] = p[i][j];
    std::memcpy(p, t, sizeof t);
}

// [driving] 0x001c65b0 RRenderWorldCulling::IsInFrustum2d
bool IsInFrustum2d(const RRenderWorldCulling& c, const float sphere[4], float radius, bool check_far, float far_scale,
                   float* distance) {
    float d = 0.0f;
    if (ee::lt(radius, dot3_minus(c.planes[4], sphere))) return false;  // o plano de perto
    const float x = sphere[0], z = sphere[2];
    if (check_far) {
        const float dx = ee::sub(c.eye[0], x), dz = ee::sub(c.eye[2], z);
        d = ee::sqrt(ee::add(ee::mul(dx, dx), ee::mul(dz, dz)));
        if (ee::lt(ee::mul(far_scale, c.range), ee::sub(d, radius))) return false;
    }
    // Os dois lados: as colunas 2 e 3 (x na linha 0, z na linha 2, a distância na linha 3).
    for (int k = 2; k < 4; ++k) {
        const float s = ee::sub(ee::add(ee::mul(c.planes[0][k], x), ee::mul(c.planes[2][k], z)), c.planes[3][k]);
        if (ee::lt(radius, s)) return false;
    }
    if (distance) *distance = d;
    return true;
}

// [driving] 0x001c66c8 RRenderWorldCulling::QuadtreeFrustrumCheck2d
int QuadtreeFrustrumCheck2d(const RRenderWorldCulling& c, const float box[4], float radius) {
    if (ee::lt(radius, dot3_minus(c.planes[4], box))) return 0;
    const float x = box[0], z = box[2];
    float d[4];
    for (int k = 0; k < 4; ++k) {
        uint32_t acc = ps2::mul(fb(c.planes[0][k]), fb(x));
        acc = ps2::madd(acc, fb(c.planes[2][k]), fb(z));
        d[k] = fbits_f(ps2::msub(acc, fb(1.0f), fb(c.planes[3][k])));
    }
    const float neg = ee::neg(radius);
    bool crossing = false, inside = true;
    for (int k = 0; k < 4; ++k) {
        const float v = d[k];
        if (ee::lt(0.0f, v)) {
            if (ee::lt(radius, v)) inside = false;
            else crossing = true;
        } else if (ee::lt(neg, v)) {
            crossing = true;
        }
    }
    if (!inside) return 0;
    return crossing ? 2 : 1;
}

// [driving] 0x001d44c8 RViewCamera::RefreshLODMultiplier
float RefreshLODMultiplier(float fov, float x0, float x1) {
    const float a = ee::div(fov, 33.0f);
    const float w = ee::sub(x1, x0);
    return ee::mul(ee::div(a, 400.0f), w);
}

// [driving] 0x001beba0 RPlayerViewCamera::ConfigureView
void RPlayerViewCamera_ConfigureView(const float camera_matrix[16], float fov, bool widescreen) {
    float range = g_cull_distance;
    if (ee::lt(fov, 32.0f)) range = ee::mul(range, fbits_f(0x3f8ccccd));  // x 1,1
    if (widescreen) fov = ee::mul(fov, 1.25f);
    float pos[4];
    std::memcpy(pos, camera_matrix + 12, 16);
    Setup2dFrustrum(fgWorldCulling(), pos, camera_matrix, fov, range);
}

// ------------------------------------------------------------------------------------------------------ as cortinas

// [driving] 0x0022f428 WVisCurtain::CalcRadius: o w do começo = a distância no plano xz do começo ao meio (VU0 em macro).
void CalcRadius(WVisCurtain& c) {
    uint32_t s[4], e[4], mid[4] = {0, 0, 0, 0}, half[4] = {fb(0.5f), 0, 0, 0}, one[4] = {fb(1.0f), 0, 0, 0};
    std::memcpy(s, c.start, 16);
    std::memcpy(e, c.end, 16);
    add(mid, s, e, XYZ);
    mulbc(mid, mid, half, BX, XYZ);
    uint32_t d[4], sq[4] = {0, 0, 0, 0}, acc[4] = {0, 0, 0, 0};
    std::memcpy(d, s, 16);
    sub(d, d, mid, XYZ);
    mul(sq, d, d, XYZ);
    mula(acc, d, d, X);
    maddbc(sq, acc, one, sq, BZ, X);
    c.start[3] = fbits_f(ps2::add(0, ps2::sqrt(sq[0])));
}

// [driving] 0x0022f660 WVisCurtain::WVisCurtain(COORD4 &, COORD4 &)
void WVisCurtain_Construct(WVisCurtain& c, const float from[4], const float to[4]) {
    std::memcpy(c.start, from, 16);
    std::memcpy(c.end, to, 16);
    CalcRadius(c);
}

// [driving] 0x0022f408 InitActiveCurtainList
void InitActiveCurtainList() { g_num_curtains = 0; }

// [driving] 0x0022f418 GetCurtainCount
int GetCurtainCount() { return g_num_curtains; }

// [driving] 0x0022f5a8 IsVisibleAgainstCurtains: a esfera e a margem na VU0 (VU0_culling 0x92).
bool IsVisibleAgainstCurtains(const float sphere[4], float margin) {
    if (g_num_curtains == 0) return true;
    ps2::VuRegs& r = vu0().regs();
    r.vi[1] = uint16_t(g_num_curtains);                       // ctc2 vi1
    load(r.vf[1], sphere, XYZW);                              // lqc2 vf1
    const uint32_t m = fb(margin);                            // qmtc2 a1 (mfc1: o sinal estendido em 64 bits)
    r.vf[2][0] = m;
    r.vf[2][1] = (m & 0x80000000u) ? 0xffffffffu : 0u;
    vu0_culling_callms(0x92);
    return r.vi[2] != 0;                                      // cfc2 vi2
}

// [driving] 0x0022f5e8 CheckPositionAgainstActiveCurtains (VU0_culling 0xa8)
bool CheckPositionAgainstActiveCurtains(const float position[4]) {
    if (g_num_curtains == 0) return true;
    ps2::VuRegs& r = vu0().regs();
    r.vi[1] = uint16_t(g_num_curtains);
    load(r.vf[1], position, XYZW);
    vu0_culling_callms(0xa8);
    return r.vi[2] != 0;
}

// [driving] 0x0022f4a0 AddCurtain: até 63; a partir da segunda, só se o pé de uma das pontas (raio 0,1) não estiver
// escondido pelas cortinas já ativas; a cortina vira os quatro planos na VU0 (VU0_culling 0x000).
void AddCurtain(const WVisCurtain& curtain, const float eye[4]) {
    if (g_num_curtains >= 0x3f) return;
    if (g_num_curtains != 0) {
        const float tenth = fbits_f(0x3dcccccd);
        float a[4], b[4];
        std::memcpy(a, curtain.start, 16);
        std::memcpy(b, curtain.end, 16);
        a[3] = tenth;
        b[3] = tenth;
        if (!IsVisibleAgainstCurtains(a, tenth) && !IsVisibleAgainstCurtains(b, tenth)) return;
    }
    ps2::VuRegs& r = vu0().regs();
    load(r.vf[1], curtain.start, XYZW);
    load(r.vf[2], curtain.end, XYZW);
    load(r.vf[3], eye, XYZW);
    r.vi[1] = uint16_t(g_num_curtains);
    vu0_culling_callms(0x000);
    if (r.vi[2] != 0) ++g_num_curtains;
}

// ------------------------------------------------------------------------------------------------------ a árvore

// [driving] 0x0022c730 WTree::InitializeTree
void WTree_InitializeTree(MapNode* node, int depth, uint8_t x, uint8_t z) {
    node->depth = uint8_t(depth);
    ++depth;
    node->x = x;
    node->z = z;
    node->scene_objs = 0;
    if (node->children == 0) return;
    MapNode* kids = nf::p32_ptr<MapNode>(node->children);
    x = uint8_t(x * 2);
    z = uint8_t(z * 2);
    WTree_InitializeTree(kids + 0, depth, x, z);
    WTree_InitializeTree(kids + 1, depth, uint8_t(x + 1), z);
    WTree_InitializeTree(kids + 2, depth, x, uint8_t(z + 1));
    WTree_InitializeTree(kids + 3, depth, uint8_t(x + 1), uint8_t(z + 1));
}

// [driving] 0x0021eae8 WRender::WRender
void WRender_Construct(WRender& r, UGroup* map, std::vector<WVisCurtain>& curtains) {
    std::memset(&r, 0, sizeof r);
    CurrentList() = &g_scratchpad;
    UData* in = UGroup_DataLocateFirst(map, 0x696e2020, 0, -1);  // 'in  '
    const uint32_t count = in->count;
    r.instances = nf::p32_u(UData_Data(in));
    for (int p = 0; p < 5; ++p) {
        r.pass_first[p] = 0;
        r.pass_count[p] = 0;
    }
    r.pass_first[4] = r.instances;
    // As passadas, do fim para o começo: uma instância sem artigo fica na passada da anterior (no laço, a seguinte).
    uint32_t pass = 0;
    for (uint32_t i = count; i-- > 0;) {
        const uint32_t addr = r.instances + i * 0x40;
        const CarpInstanceRec* inst = nf::p32_ptr<const CarpInstanceRec>(addr);
        if (inst->article) pass = (inst->flags & 4) ? 4u : nf::p32_ptr<const CarpArticle>(inst->article)->draw_pass;
        r.pass_first[pass] = addr;
        ++r.pass_count[pass];
    }
    for (int p = 3; p >= 0; --p) {
        if (r.pass_first[p] == 0) {
            r.pass_first[p] = r.pass_first[p + 1] + uint32_t(r.pass_count[p + 1]) * 0x40;
            r.pass_count[p] = 0;
        }
    }
    UData* ps = UGroup_DataLocateFirst(map, 0x70732020, 0, -1);  // 'ps  '
    r.proc_anims = ps == UGroup_DataEnd(map) ? 0 : nf::p32_u(UData_Data(ps));
    UData* vis = UGroup_DataLocateFirst(map, 0x56697343, -1, -1);  // 'VisC'
    curtains.clear();
    if (vis == UGroup_DataEnd(map)) {
        r.curtains = 0;  // o PS2 deixa o +0x30 como estava (o memset do port o zera)
    } else {
        const float* rec = reinterpret_cast<const float*>(UData_Data(vis));
        r.curtain_count = vis->count;
        curtains.assign(vis->count, WVisCurtain{});
        for (uint32_t i = 0; i < vis->count; ++i) WVisCurtain_Construct(curtains[i], rec + i * 8, rec + i * 8 + 4);
        r.curtains = curtains.empty() ? 0 : nf::p32_u(curtains.data());
    }
}

// [driving] 0x0021ef68 WRender::FindVisibleTreeNodesAndCurtains
void FindVisibleTreeNodesAndCurtains(WRender& r, const MapNode* node, const float box[4], bool clip, int depth) {
    bool crossing = false;
    if (clip) {
        const int where = QuadtreeFrustrumCheck2d(fgWorldCulling(), box, ee::mul(box[3], fbits_f(0x3fb4fdf4)));  // x 1,414
        if (where == 0) return;
        crossing = where == 2;
    }
    ++depth;
    CachedDrawInfo* list = g_current;
    if (list->node_count < kMaxVisibleNodes) {
        list->nodes[list->node_count] = nf::p32_u(node);
        ++list->node_count;
    } else {
        ++g_guards;  // divergência: o PS2 escreveria além da lista
    }
    const float* cam = nf::p32_ptr<const float>(r.camera);
    float eye[4];
    std::memcpy(eye, cam + 12, 16);
    const uint16_t* indices = MapNode_IndexList(node);
    const WVisCurtain* curtains = nf::p32_ptr<const WVisCurtain>(r.curtains);
    for (int i = 0; i < node->curtain_count; ++i) {
        const WVisCurtain& c = curtains[indices[node->instance_count + i]];
        uint32_t s[4], e[4], mid[4] = {0, 0, 0, 0}, half[4] = {fb(0.5f), 0, 0, 0};
        std::memcpy(s, c.start, 16);
        std::memcpy(e, c.end, 16);
        add(mid, s, e, XYZ);
        mulbc(mid, mid, half, BX, XYZ);
        float m[4];
        std::memcpy(m, mid, 16);
        if (IsInFrustum2d(fgWorldCulling(), m, c.start[3], true, 1.0f, nullptr)) AddCurtain(c, eye);
    }
    if (node->children == 0) return;
    const MapNode* kids = nf::p32_ptr<const MapNode>(node->children);
    float q[4];
    std::memcpy(q, box, 16);
    const float half = ee::mul(box[3], 0.5f);
    q[3] = half;
    q[0] = ee::sub(q[0], half);
    q[2] = ee::sub(q[2], half);
    FindVisibleTreeNodesAndCurtains(r, kids + 0, q, crossing, depth);
    q[0] = ee::add(q[0], box[3]);
    FindVisibleTreeNodesAndCurtains(r, kids + 1, q, crossing, depth);
    q[2] = ee::add(q[2], box[3]);
    FindVisibleTreeNodesAndCurtains(r, kids + 3, q, crossing, depth);
    q[0] = ee::sub(q[0], box[3]);
    FindVisibleTreeNodesAndCurtains(r, kids + 2, q, crossing, depth);
}

// [driving] 0x0021f1d8 WRender::GenerateCurtainsAndNodes
CachedDrawInfo* GenerateCurtainsAndNodes(WRender& r, const float* camera, float unknown) {
    g_scratchpad.node_count = 0;
    g_current = &g_scratchpad;
    r.unknown_d8 = unknown;
    r.camera = nf::p32_u(camera);
    const uint8_t* map = nf::p32_ptr<const uint8_t>(g_world_map);
    float corner[3], size;
    std::memcpy(corner, map + 0x1c, 12);
    std::memcpy(&size, map + 0x2c, 4);
    const float half = ee::mul(size, 0.5f);
    const float box[4] = {ee::add(corner[0], half), corner[1], ee::add(corner[2], half), half};
    InitActiveCurtainList();
    UploadCullingMicrocode(true);
    uint32_t root;
    std::memcpy(&root, map, 4);
    FindVisibleTreeNodesAndCurtains(r, nf::p32_ptr<const MapNode>(root), box, true, 0);
    return g_current;
}

// [driving] 0x0021f288 WRender::PrepareForCull (o DRIVING.SYM a chama GetNextPoint): zera os desenhos da lista que a
// árvore encheu por último (CurrentList), não da do argumento.
void PrepareForCull(CachedDrawInfo* list) {
    g_current->draw_count = 0;
    g_cursor.item = -1;
    g_cursor.list = list;
    g_cursor.node = 0;
}

// [driving] 0x0021f2b8 WRender::GetNextPoint
int32_t GetNextPoint(float sphere[4], float* height, int32_t* check_far, float* far_scale) {
    CachedDrawInfo* list = g_cursor.list;
    const int32_t count = list->node_count;
    if (count == 0) return -1;
    ++g_cursor.item;
    if (g_cursor.item >= nf::p32_ptr<const MapNode>(list->nodes[g_cursor.node])->instance_count) {
        int32_t n = g_cursor.node;
        for (;;) {
            ++n;
            if (n >= count) break;
            if (nf::p32_ptr<const MapNode>(list->nodes[n])->instance_count != 0) break;
        }
        g_cursor.node = n;
        if (g_cursor.node >= list->node_count) return -1;
        g_cursor.item = 0;
    }
    const MapNode* node = nf::p32_ptr<const MapNode>(list->nodes[g_cursor.node]);
    const uint16_t index = MapNode_IndexList(node)[g_cursor.item];
    const uint32_t addr = g_render->instances + uint32_t(index) * 0x40;
    const uint8_t* inst = nf::p32_ptr<const uint8_t>(addr);
    uint32_t packed;
    std::memcpy(&packed, inst + 0x3c, 4);
    const float step = kDimensionStep[(packed >> 30) & 1];
    if (packed & 0x80000000u) {
        *height = ee::mul(ee::cvt_s(int32_t((packed >> 10) & 0x3ff)), step);  // o y, dado à parte
    } else {
        const float x = ee::mul(ee::cvt_s(int32_t(packed & 0x3ff)), step);
        *height = ee::add(x, x);                                              // o dobro do x
    }
    std::memcpy(sphere, inst + 0x30, 16);
    sphere[3] = ee::mul(ee::cvt_s(int32_t(packed & 0x3ff)), step);
    const uint8_t flags = inst[0xc];
    const int32_t far = ((flags >> 3) ^ 1) & 1;
    *check_far = far;
    if (flags & 0x40) {
        *far_scale = far ? fbits_f(0x3e99999a) : 2.0f;  // 0,3 ou 2
        *check_far = 1;
    } else {
        *far_scale = 1.0f;
    }
    return int32_t(addr);
}

// [driving] 0x0021f570 WRender::SetCull
void SetCull(int32_t item, int32_t culled, float distance) {
    if (culled) return;
    CachedDrawInfo* list = g_cursor.list;
    if (list->draw_count + 1 >= kMaxDraws) return;
    list->draws[list->draw_count].instance = uint32_t(item);
    list->draws[list->draw_count].distance = distance;
    ++list->draw_count;
}

// [driving] 0x0021f5b8 WRender::CopyDrawPasses
void CopyDrawPasses(const WRender& r, const CachedDrawInfo& from, CachedDrawInfo& to, int first_pass, int last_pass) {
    to.draw_count = 0;
    for (int pass = last_pass;; --pass) {
        const uint32_t first = r.pass_first[pass];
        const uint32_t end = first + uint32_t(r.pass_count[pass]) * 0x40;
        for (int i = 0; i < from.draw_count; ++i) {
            const uint32_t a = from.draws[i].instance;
            if (!(a < first) && a < end) to.draws[to.draw_count++] = from.draws[i];
        }
        if (pass == first_pass) break;
    }
}

// [driving] 0x001c54a0 RRenderWorldCamera::CullModule: com o corte no longe, a esfera vai às cortinas sem raio (a
// altura como a margem do quarto plano).
void RRenderWorldCamera_CullModule(CullNextFn next, CullSetFn set) {
    float sphere[4], height = 0.0f, far_scale = 0.0f;
    int32_t check_far = 0;
    for (int32_t item = next(sphere, &height, &check_far, &far_scale); item != -1;
         item = next(sphere, &height, &check_far, &far_scale)) {
        float distance = 0.0f;
        if (!IsInFrustum2d(fgWorldCulling(), sphere, sphere[3], check_far != 0, far_scale, &distance)) {
            set(item, 1, distance);
            continue;
        }
        if (check_far == 1) sphere[3] = 0.0f;
        set(item, IsVisibleAgainstCurtains(sphere, height) ? 0 : 1, distance);
    }
}

// [driving] 0x001c5780 RRenderWorldCamera::PerformCulling (o mundo)
CachedDrawInfo* RRenderWorldCamera_PerformCulling(WRender& r, const float* camera) {
    CachedDrawInfo* list = GenerateCurtainsAndNodes(r, camera, 120.0f);
    PrepareForCull(list);
    RRenderWorldCamera_CullModule(GetNextPoint, SetCull);
    // (Os atores: ActActorDatabase::PrepareActorsForCulling e o CullModule deles só com o 0x00326000 não nulo.)
    PrepareSceneObjsForCulling(list);
    RRenderWorldCamera_CullModule(GetNextSceneObjCullInfo, SetSceneObjectCull);
    // A galeria (driving/fx/gfx_gallery.h): GFXGallery::CULL_Start e o CullModule das telas (GFX::CULL_GetNextCanvas
    // 0x00232d58 e GFX::CULL_SetCanvas 0x00232d98).
    if (GfxGallery* g = gfx_gallery()) {
        GFXGallery_CULL_Start(*g);
        RRenderWorldCamera_CullModule(
            [](float sphere[4], float* height, int32_t* check_far, float* far_scale) {
                return GFXGallery_CULL_GetNextCanvas(*gfx_gallery(), sphere, height, check_far, far_scale);
            },
            [](int32_t item, int32_t culled, float) { GFXGallery_CULL_SetCanvas(*gfx_gallery(), item, culled); });
    }
    return list;
}

// ------------------------------------------------------------------------------------------------ os objetos de cena

namespace {
SceneObjCullState g_scene_cull;

// O raio das medidas empacotadas (+0x3c) de uma instância: o x vezes o passo do bit 30.
float packed_x(uint32_t packed) {
    return ee::mul(ee::cvt_s(int32_t(packed & 0x3ff)), kDimensionStep[(packed >> 30) & 1]);
}
}  // namespace

SceneObjCullState& scene_obj_cull() { return g_scene_cull; }

SceneObjBodyPos& scene_obj_body_pos() {
    static SceneObjBodyPos fn = nullptr;
    return fn;
}

// [driving] 0x0022c1a8 WTree::FindNode: desce para o filho que contém o círculo (o produto das duas folgas >= 0 em x e
// em z), na ordem dos filhos do FindVisibleTreeNodesAndCurtains (0: x-, z-; 1: x+, z-; 2: x-, z+; 3: x+, z+).
MapNode* WTree_FindNode(MapNode* node, const float box[4], const float sphere[4]) {
    if (node->children == 0) return node;
    MapNode* kids = nf::p32_ptr<MapNode>(node->children);
    const float r = sphere[3], h = box[3];
    const float lo = ee::sub(sphere[0], r), hi = ee::add(sphere[0], r);
    auto fits = [](float a, float b) { return ee::le(0.0f, ee::mul(a, b)); };
    int kid;
    if (fits(ee::sub(lo, ee::sub(box[0], h)), ee::sub(box[0], hi))) {
        kid = 0;  // x na metade de baixo
    } else if (fits(ee::sub(lo, box[0]), ee::sub(ee::add(box[0], h), hi))) {
        kid = 1;  // x na de cima
    } else {
        return node;
    }
    const float zlo = ee::sub(sphere[2], r), zhi = ee::add(sphere[2], r);
    if (fits(ee::sub(zlo, ee::sub(box[2], h)), ee::sub(box[2], zhi))) {
        // z na metade de baixo
    } else if (fits(ee::sub(zlo, box[2]), ee::sub(ee::add(box[2], h), zhi))) {
        kid += 2;
    } else {
        return node;
    }
    const float q = ee::mul(box[3], 0.5f);
    float sub[4] = {kid & 1 ? ee::add(box[0], q) : ee::sub(box[0], q), 0.0f,
                    kid & 2 ? ee::add(box[2], q) : ee::sub(box[2], q), q};
    return WTree_FindNode(kids + kid, sub, sphere);
}

// [driving] 0x001c8cf8 RSceneObj::SetMapNode: sai da lista do nó antigo e entra no começo da do novo.
void RSceneObj_SetMapNode(RSceneObj& o, MapNode* node) {
    const uint32_t n = nf::p32_u(node);
    const uint32_t self = nf::p32_u(&o);
    if (n == o.map_node) return;
    if (o.map_node != 0) {
        MapNode* old = nf::p32_ptr<MapNode>(o.map_node);
        if (old->scene_objs == self) old->scene_objs = o.next;
        if (o.next != 0) nf::p32_ptr<RSceneObj>(o.next)->prev = o.prev;
        if (o.prev != 0) nf::p32_ptr<RSceneObj>(o.prev)->next = o.next;
    }
    o.map_node = n;
    o.next = node->scene_objs;
    o.prev = 0;
    if (o.next != 0) nf::p32_ptr<RSceneObj>(o.next)->prev = self;
    node->scene_objs = self;
}

// [driving] 0x001c7098 RSceneObj::RSceneObj
void RSceneObj_Construct(RSceneObj& o, uint32_t instance) {
    std::memset(&o, 0, sizeof o);
    o.flags = (o.flags & 0xe1ffffffu) | 0x01640000u;  // o byte +0x1a = 100, o bit 24
    o.instance = instance;
    // A esfera: a posição da matriz da instância (RSceneObj::GetTransform 0x001c9190) e o raio.
    const uint8_t* inst = nf::p32_ptr<const uint8_t>(instance);
    float sphere[4];
    std::memcpy(sphere, inst + 0x30, 12);
    uint32_t packed;
    std::memcpy(&packed, inst + 0x3c, 4);
    sphere[3] = packed_x(packed);
    if (ee::eq(sphere[3], 0.0f)) sphere[3] = 10.0f;
    // O quadrado da raiz: o canto + a metade do lado.
    const uint8_t* wmap = nf::p32_ptr<const uint8_t>(g_world_map);
    float corner[3], size;
    std::memcpy(corner, wmap + 0x1c, 12);
    std::memcpy(&size, wmap + 0x2c, 4);
    const float half = ee::mul(size, 0.5f);
    const float box[4] = {ee::add(corner[0], half), corner[1], ee::add(corner[2], half), half};
    uint32_t root;
    std::memcpy(&root, wmap, 4);
    RSceneObj_SetMapNode(o, WTree_FindNode(nf::p32_ptr<MapNode>(root), box, sphere));
    o.path = 0;  // RPathEngine::GetPathHandle (não portado)
}

int WWorld_CreateSceneObjects(WRender& r, std::vector<RSceneObj>& objs) {
    objs.clear();
    if (!r.instances) return 0;
    const int count = r.pass_count[0] + r.pass_count[1] + r.pass_count[2] + r.pass_count[3] + r.pass_count[4];
    uint8_t* first = nf::p32_ptr<uint8_t>(r.instances);
    // A primeira volta conta (o reserve do vector de ProcAnimInstanceAndPointer; aqui o dos objetos).
    size_t want = 0;
    for (int i = 0; i < count; ++i) {
        const uint8_t* inst = first + 0x40 * i;
        uint32_t article;
        std::memcpy(&article, inst + 0x1c, 4);
        if ((inst[0xc] & 0x10) && article) ++want;
    }
    objs.reserve(want);
    for (int i = 0; i < count; ++i) {
        uint8_t* inst = first + 0x40 * i;
        if (!(inst[0xc] & 0x10)) continue;  // os efeitos (GFX::Trigger) não foram portados
        if (!r.proc_anims) {
            ++g_guards;  // o PS2 leria o registro a partir do endereço 0
            continue;
        }
        uint16_t index;
        std::memcpy(&index, inst + 0xe, 2);
        uint8_t* rec = nf::p32_ptr<uint8_t>(r.proc_anims) + 0x20 * index;
        if (rec[7] == 0xff) continue;
        inst[0xc] |= 1;
        uint32_t article;
        std::memcpy(&article, inst + 0x1c, 4);
        if (!article) continue;
        objs.emplace_back();
        RSceneObj_Construct(objs.back(), nf::p32_u(inst));
        const uint32_t o = nf::p32_u(&objs.back());
        std::memcpy(rec + 8, &o, 4);
    }
    return int(objs.size());
}

// [driving] 0x001c80f0 RSceneObj::PrepareSceneObjsForCulling
void PrepareSceneObjsForCulling(CachedDrawInfo* list) {
    g_scene_cull.node = -1;
    g_scene_cull.list = list;
    g_scene_cull.max_visible = 0x200;
    g_scene_cull.num_visible = 0;
    g_scene_cull.num_draw_last = 0;
    g_scene_cull.current = nullptr;
}

// [driving] 0x001c8130 RSceneObj::GetNextSceneObjCullInfo
int32_t GetNextSceneObjCullInfo(float sphere[4], float* height, int32_t* check_far, float* far_scale) {
    SceneObjCullState& s = g_scene_cull;
    const CachedDrawInfo* list = s.list;
    if (list->node_count == 0) return -1;
    if (s.current) s.current = s.current->next ? nf::p32_ptr<RSceneObj>(s.current->next) : nullptr;
    if (!s.current) {
        // O próximo nó visível com objetos.
        const MapNode* node = nullptr;
        for (;;) {
            ++s.node;
            if (s.node >= list->node_count) break;
            node = nf::p32_ptr<const MapNode>(list->nodes[s.node]);
            if (node->scene_objs != 0) break;
        }
        if (s.node >= list->node_count) return -1;
        s.current = nf::p32_ptr<RSceneObj>(node->scene_objs);
    }
    const RSceneObj& o = *s.current;
    const uint8_t* inst = nf::p32_ptr<const uint8_t>(o.instance);
    if (o.physics != 0) {
        // A posição do corpo rígido (Simulation::GetRigidBody(+0x46) +0x10; o w fica para o raio abaixo).
        float pos[3];
        if (scene_obj_body_pos() && scene_obj_body_pos()(o.physics, pos)) std::memcpy(sphere, pos, 12);
        else ++g_guards;
    } else {
        std::memcpy(sphere, inst + 0x30, 16);
    }
    uint32_t packed;
    std::memcpy(&packed, inst + 0x3c, 4);
    sphere[3] = packed_x(packed);
    if (packed & 0x80000000u) {
        *height = ee::mul(ee::cvt_s(int32_t((packed >> 10) & 0x3ff)), kDimensionStep[(packed >> 30) & 1]);
    } else {
        *height = sphere[3];
    }
    *check_far = int32_t(((o.flags >> 26) ^ 1) & 1);
    *far_scale = (o.flags & 0x2000000u) ? fbits_f(0x3e99999a) : 1.0f;  // 0,3
    return int32_t(nf::p32_u(s.current));
}

// [driving] 0x001c8490 RSceneObj::SetSceneObjectCull
void SetSceneObjectCull(int32_t item, int32_t culled, float /*distance*/) {
    if (culled == 0) RSceneObj_SetNowVisible(*nf::p32_ptr<RSceneObj>(uint32_t(item)));
}

// [driving] 0x001c8c70 RSceneObj::SetNowVisible
void RSceneObj_SetNowVisible(RSceneObj& o) {
    SceneObjCullState& s = g_scene_cull;
    if (!(o.flags & 0x1000000u) || s.num_visible >= s.max_visible) return;
    if (o.flags & 0x8000000u) {
        if (s.num_draw_last >= 0x200) {
            ++g_guards;  // o PS2 escreveria além da lista
            return;
        }
        s.draw_last[s.num_draw_last++] = nf::p32_u(&o);
        return;
    }
    s.visible[s.num_visible++] = nf::p32_u(&o);
}

// ------------------------------------------------------------------------------------------------------ o desenho

namespace {

inline bool sort_less(const InstanceAndDistance& a, const InstanceAndDistance& b, int sort) {
    if (sort == 1) return a.instance < b.instance;  // [driving] 0x00220bc0 lessThanByInstance (sltu)
    return ee::lt(b.distance, a.distance);          // [driving] 0x00220bd0 greaterThanByDistance (c.lt.S b, a)
}

// O SGI STL do GCC 2.95 (stl_algo.h, stl_heap.h), como o DRIVING.ELF o compilou para InstanceAndDistance.
void push_heap(InstanceAndDistance* first, int hole, int top, InstanceAndDistance value, int sort) {
    int parent = (hole - 1) / 2;
    while (hole > top && sort_less(first[parent], value, sort)) {
        first[hole] = first[parent];
        hole = parent;
        parent = (hole - 1) / 2;
    }
    first[hole] = value;
}

void adjust_heap(InstanceAndDistance* first, int hole, int len, InstanceAndDistance value, int sort) {
    const int top = hole;
    int second = 2 * hole + 2;
    while (second < len) {
        if (sort_less(first[second], first[second - 1], sort)) --second;
        first[hole] = first[second];
        hole = second;
        second = 2 * (second + 1);
    }
    if (second == len) {
        first[hole] = first[second - 1];
        hole = second - 1;
    }
    push_heap(first, hole, top, value, sort);
}

void make_heap(InstanceAndDistance* first, InstanceAndDistance* last, int sort) {
    const int len = int(last - first);
    if (len < 2) return;
    int parent = (len - 2) / 2;
    for (;;) {
        adjust_heap(first, parent, len, first[parent], sort);
        if (parent == 0) return;
        --parent;
    }
}

void partial_sort(InstanceAndDistance* first, InstanceAndDistance* middle, InstanceAndDistance* last, int sort) {
    make_heap(first, middle, sort);
    for (InstanceAndDistance* i = middle; i < last; ++i)
        if (sort_less(*i, *first, sort)) {
            const InstanceAndDistance v = *i;
            *i = *first;
            adjust_heap(first, 0, int(middle - first), v, sort);
        }
    while (middle - first > 1) {  // sort_heap
        --middle;
        const InstanceAndDistance v = *middle;
        *middle = *first;
        adjust_heap(first, 0, int(middle - first), v, sort);
    }
}

const InstanceAndDistance& median(const InstanceAndDistance& a, const InstanceAndDistance& b,
                                  const InstanceAndDistance& c, int sort) {
    if (sort_less(a, b, sort)) {
        if (sort_less(b, c, sort)) return b;
        if (sort_less(a, c, sort)) return c;
        return a;
    }
    if (sort_less(a, c, sort)) return a;
    if (sort_less(b, c, sort)) return c;
    return b;
}

InstanceAndDistance* unguarded_partition(InstanceAndDistance* first, InstanceAndDistance* last,
                                         InstanceAndDistance pivot, int sort) {
    for (;;) {
        while (sort_less(*first, pivot, sort)) ++first;
        --last;
        while (sort_less(pivot, *last, sort)) --last;
        if (!(first < last)) return first;
        std::swap(*first, *last);
        ++first;
    }
}

void unguarded_linear_insert(InstanceAndDistance* last, InstanceAndDistance value, int sort) {
    InstanceAndDistance* next = last - 1;
    while (sort_less(value, *next, sort)) {
        *last = *next;
        last = next;
        --next;
    }
    *last = value;
}

void insertion_sort(InstanceAndDistance* first, InstanceAndDistance* last, int sort) {
    if (first == last) return;
    for (InstanceAndDistance* i = first + 1; i != last; ++i) {
        const InstanceAndDistance value = *i;
        if (sort_less(value, *first, sort)) {
            std::copy_backward(first, i, i + 1);
            *first = value;
        } else {
            unguarded_linear_insert(i, value, sort);
        }
    }
}

}  // namespace

// [driving] 0x002207c0 __introsort_loop<InstanceAndDistance *, InstanceAndDistance, int, bool (*)(...)>
void introsort_loop(InstanceAndDistance* first, InstanceAndDistance* last, int depth_limit, int sort) {
    while (last - first > 16) {
        if (depth_limit == 0) {
            partial_sort(first, last, last, sort);
            return;
        }
        --depth_limit;
        const InstanceAndDistance pivot = median(*first, *(first + (last - first) / 2), *(last - 1), sort);
        InstanceAndDistance* cut = unguarded_partition(first, last, pivot, sort);
        introsort_loop(cut, last, depth_limit, sort);
        last = cut;
    }
}

// [driving] 0x00220b10 __final_insertion_sort<InstanceAndDistance *, bool (*)(...)>
void final_insertion_sort(InstanceAndDistance* first, InstanceAndDistance* last, int sort) {
    if (last - first > 16) {
        insertion_sort(first, first + 16, sort);
        for (InstanceAndDistance* i = first + 16; i != last; ++i) unguarded_linear_insert(i, *i, sort);
    } else {
        insertion_sort(first, last, sort);
    }
}

void DrawPass_sort(CachedDrawInfo& list, int sort) {
    if (sort != 1 && sort != 2) return;
    InstanceAndDistance* first = list.draws;
    InstanceAndDistance* last = list.draws + list.draw_count;
    if (first == last) return;
    int lg = 0;
    for (int n = int(last - first); n != 1; n >>= 1) ++lg;
    introsort_loop(first, last, lg * 2, sort);
    final_insertion_sort(first, last, sort);
}

// [driving] 0x0021f670 WRender::DrawPass (com o QuickDrawInstance 0x001ad618 trocado pela lista)
void DrawPass_order(const WRender& r, CachedDrawInfo& list, int first_pass, int last_pass, int sort,
                    std::vector<InstanceAndDistance>& out) {
    DrawPass_sort(list, sort);
    for (int pass = last_pass;; --pass) {
        const uint32_t first = r.pass_first[pass];
        const uint32_t end = first + uint32_t(r.pass_count[pass]) * 0x40;
        for (int i = 0; i < list.draw_count; ++i) {
            const uint32_t a = list.draws[i].instance;
            if (a < first || !(a < end)) continue;
            const CarpInstanceRec* inst = nf::p32_ptr<const CarpInstanceRec>(a);
            if ((inst->flags & 1) == 0 && inst->article != 0) out.push_back(list.draws[i]);
        }
        if (pass == first_pass) break;
    }
}

// [driving] 0x001ece18 RSky::Draw (só a ordem: o jogo escreve a posição da câmera em cada instância do céu e a desenha;
// o nf_driving desenha o céu levado ao olho)
void RSky_order(const WRender& r, std::vector<InstanceAndDistance>& out) {
    uint32_t a = r.instances;
    const CarpInstanceRec* inst = nf::p32_ptr<const CarpInstanceRec>(a);
    if (!inst || (inst->flags & 4) == 0) return;
    for (;;) {
        if ((inst->flags & 1) == 0 && inst->article != 0) out.push_back(InstanceAndDistance{a, 0.0f});
        a += 0x40;
        inst = nf::p32_ptr<const CarpInstanceRec>(a);
        if ((inst->flags & 4) == 0) break;
    }
}

}  // namespace drv
