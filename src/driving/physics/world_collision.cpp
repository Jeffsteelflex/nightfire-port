// O chão da pista (world_collision.h), portado do MIPS do DRIVING.ELF (as desmontagens em out/disasm_drv_coll* do
// checkout principal). As contas em float seguem a FPU do EE (platform/ps2_float.h: truncamento, sem denormais), na
// ordem das instruções, para dar os mesmos bits que o original no nf_driving_check.

#include "driving/physics/world_collision.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "driving/physics/simulation.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;

uint64_t g_world_collision_guards = 0;

namespace {

uint32_t u32(const void* p) { return nf::p32_u(p); }
template <class T>
T* at(uint32_t a) {
    return nf::p32_ptr<T>(a);
}

// FUN_0025b890: o ponto (xyz) pela matriz com a translação, ((x m0 + y m1) + z m2) + m3; só o xyz é gravado.
void xform(const float src[3], const float m[16], float dst[3]) {
    const float s0 = src[0], s1 = src[1], s2 = src[2];
    float r[3];
    for (int c = 0; c < 3; ++c)
        r[c] = ee::add(ee::add(ee::add(ee::mul(s0, m[c]), ee::mul(s1, m[4 + c])), ee::mul(s2, m[8 + c])), m[12 + c]);
    dst[0] = r[0];
    dst[1] = r[1];
    dst[2] = r[2];
}

// FUN_0025a8e8: a x b.
void cross(const float a[3], const float b[3], float out[3]) {
    const float x = ee::sub(ee::mul(a[1], b[2]), ee::mul(a[2], b[1]));
    const float y = ee::sub(ee::mul(a[2], b[0]), ee::mul(a[0], b[2]));
    const float z = ee::sub(ee::mul(a[0], b[1]), ee::mul(a[1], b[0]));
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// O "para cima" de uma instância inclinada, frente x direito: pela FPU (FUN_0025a8e8) ou, com o caminho VU0 do gerente
// (+0x30, ligado pela Simulation durante a colisão com os prédios e os Simulate dos objetos), pelo par
// vopmula.xyz/vopmsub.xyz (a subtração com o acumulador da VU).
void instance_up(const WCollisionMgr& mgr, const WCollisionInstance& ci, float out[3]) {
    if (!mgr.vu0_path) {
        cross(ci.forward, ci.right, out);
        return;
    }
    namespace vu = ps2::vu;
    vu::Reg f, r, acc{}, d{};
    vu::load(f, ci.forward);  // lqc2: o w é half_z, não usado
    vu::load(r, ci.right);
    vu::opmula(acc, f, r);
    vu::opmsub(d, acc, r, f);
    std::memcpy(out, d, 12);
}

// O teste 2D (xz) de ponto no triângulo do WWorldPos e das faixas: os três produtos vetoriais com o mesmo sinal; `le`
// = os três <= 0 (sentido horário), senão >= 0. Na forma "qualquer sentido" o sinal vem do primeiro.
float c_edge(float ax, float az, float bx, float bz, float px, float pz) {
    // (a.x - b.x)(p.z - b.z) - (p.x - b.x)(a.z - b.z)
    return ee::sub(ee::mul(ee::sub(ax, bx), ee::sub(pz, bz)), ee::mul(ee::sub(px, bx), ee::sub(az, bz)));
}
float c_last(const StripVertex* t, float px, float pz) {
    // (c2.x - c0.x)(p.z - c0.z) - (p.x - c0.x)(c2.z - c0.z): a ordem das contas do original (f0 - f1)
    const float f1 = ee::mul(ee::sub(px, t[0].x), ee::sub(t[2].z, t[0].z));
    const float f0 = ee::mul(ee::sub(t[2].x, t[0].x), ee::sub(pz, t[0].z));
    return ee::sub(f0, f1);
}
bool inside_le(const StripVertex* t, float px, float pz) {
    if (!ee::le(c_edge(t[0].x, t[0].z, t[1].x, t[1].z, px, pz), 0.0f)) return false;
    if (!ee::le(c_edge(t[1].x, t[1].z, t[2].x, t[2].z, px, pz), 0.0f)) return false;
    return ee::le(c_last(t, px, pz), 0.0f);
}
bool inside_ge(const StripVertex* t, float px, float pz) {
    if (!ee::le(0.0f, c_edge(t[0].x, t[0].z, t[1].x, t[1].z, px, pz))) return false;
    if (!ee::le(0.0f, c_edge(t[1].x, t[1].z, t[2].x, t[2].z, px, pz))) return false;
    return ee::le(0.0f, c_last(t, px, pz));
}
bool inside_either(const StripVertex* t, float px, float pz) {
    return ee::le(c_edge(t[0].x, t[0].z, t[1].x, t[1].z, px, pz), 0.0f) ? inside_le(t, px, pz) : inside_ge(t, px, pz);
}
uint8_t subtype_of(const StripVertex* t) { return reinterpret_cast<const uint8_t*>(&t[2].w)[1]; }

// O 1e38 do começo das buscas (0x7e967699: DAT_00399014 e DAT_0039b730).
const float kNoFace = ps2::ffloat(0x7e967699u);

}  // namespace

// [driving] 0x002d19b0 OrthoInverse: a rotação transposta no lugar e a translação -t pelas linhas antigas.
void OrthoInverse(float m[16]) {
    const float o[16] = {m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]};
    const float nx = ee::neg(o[12]);
    m[4] = o[1];
    m[8] = o[2];
    m[1] = o[4];
    m[2] = o[8];
    m[6] = o[9];
    m[9] = o[6];
    m[12] = ee::sub(ee::sub(ee::mul(nx, o[0]), ee::mul(o[13], o[1])), ee::mul(o[14], o[2]));
    m[13] = ee::sub(ee::sub(ee::mul(nx, o[4]), ee::mul(o[13], o[5])), ee::mul(o[14], o[6]));
    m[14] = ee::sub(ee::sub(ee::mul(nx, o[8]), ee::mul(o[13], o[9])), ee::mul(o[14], o[10]));
}

void WCollisionInstance_MakeMatrix(const WCollisionMgr& mgr, const WCollisionInstance& ci, float m[16], bool translate) {
    m[0] = ci.right[0];
    m[1] = ci.right[1];
    m[2] = ci.right[2];
    m[3] = 0.0f;
    if (ci.flags & 3) {
        instance_up(mgr, ci, m + 4);
    } else {
        m[4] = 0.0f;
        m[5] = 1.0f;
        m[6] = 0.0f;
    }
    m[7] = 0.0f;
    m[8] = ci.forward[0];
    m[9] = ci.forward[1];
    m[10] = ci.forward[2];
    m[11] = 0.0f;
    if (translate) {
        m[12] = ci.position[0];
        m[13] = ci.position[1];
        m[14] = ci.position[2];
    } else {
        m[12] = m[13] = m[14] = 0.0f;
    }
    m[15] = 1.0f;
}

void WCollisionInstance_CalcPosition(const WCollisionMgr& mgr, const WCollisionInstance& ci, float out[3]) {
    const float* p = ci.position;
    out[0] = ee::sub(ee::sub(ee::mul(ee::neg(p[0]), ci.right[0]), ee::mul(p[1], ci.right[1])), ee::mul(p[2], ci.right[2]));
    out[2] = ee::sub(ee::sub(ee::mul(ee::neg(p[0]), ci.forward[0]), ee::mul(p[1], ci.forward[1])),
                     ee::mul(p[2], ci.forward[2]));
    if (ci.flags & 3) {
        float up[3];
        instance_up(mgr, ci, up);
        out[1] = ee::sub(ee::sub(ee::mul(ee::neg(p[0]), up[0]), ee::mul(p[1], up[1])), ee::mul(p[2], up[2]));
    } else {
        out[1] = ee::neg(p[1]);
    }
}

void WGrid_FindNodesBox(const WGrid& g, const float lo[4], const float hi[4], std::vector<uint32_t>& cells) {
    auto clamp = [](uint32_t v, uint32_t n) -> uint32_t {
        if (v < n) return v;
        return v > 0x7ffffffeu ? 0u : n - 1;  // negativo: 0; além do fim: o último
    };
    auto cell = [&](float v, float o) { return uint32_t(ee::cvt_w(ee::mul(ee::sub(v, o), g.inv_cell))); };
    uint32_t c0 = clamp(cell(lo[0], g.origin[0]), g.columns);
    uint32_t r0 = clamp(cell(lo[2], g.origin[2]), g.rows);
    uint32_t c1 = clamp(cell(hi[0], g.origin[0]), g.columns);
    uint32_t r1 = clamp(cell(hi[2], g.origin[2]), g.rows);
    if (r1 < r0) std::swap(r0, r1);
    if (c1 < c0) std::swap(c0, c1);
    if (r1 - r0 >= 21) r1 = r0;  // no máximo 21 linhas e 21 colunas: senão uma só
    if (c1 - c0 >= 21) c1 = c0;
    c0 = clamp(c0, g.columns);
    r0 = clamp(r0, g.rows);
    c1 = clamp(c1, g.columns);
    r1 = clamp(r1, g.rows);
    for (uint32_t c = c0; c <= c1; ++c)
        for (uint32_t r = r0; r <= r1; ++r) cells.push_back(r * g.columns + c);
}

void WGrid_FindNodes(const WGrid& g, const float centre[3], float radius, std::vector<uint32_t>& cells) {
    const float lo[4] = {ee::sub(centre[0], radius), 0.0f, ee::sub(centre[2], radius), 0.0f};
    const float hi[4] = {ee::add(centre[0], radius), 0.0f, ee::add(centre[2], radius), 0.0f};
    WGrid_FindNodesBox(g, lo, hi, cells);
}

void WCollisionMgr_GetInstanceList(WCollisionMgr& mgr, const WGrid& g, std::vector<InstanceListEntry>& list,
                                   const float point[3], float radius, bool strip_lists, bool flag_b) {
    std::vector<uint32_t> cells;
    cells.reserve(64);
    WGrid_FindNodes(g, point, radius, cells);
    if (list.capacity() < 32) list.reserve(32);
    // WCollisionMgr::GetInstanceListGuts (0x00217228)
    const uint32_t stamp = ++mgr.query_stamp;
    const uint32_t* nodes = at<uint32_t>(g.nodes);
    WCollisionInstance* insts = at<WCollisionInstance>(mgr.instances);
    for (uint32_t c : cells) {
        const WGridNode* node = at<WGridNode>(nodes[c]);
        if (!node) continue;
        // As instâncias estáticas da célula (o tipo 0) e depois a lista dinâmica. A dinâmica só ganha elementos pelos
        // 'de' do 'CDat' (WGrid::Init os põe numa lista que o jogo distribui pelas células), que o port ainda não
        // monta: uma célula com a lista não nula conta como guarda e só as estáticas são lidas.
        if (node->dynamic != 0) ++g_world_collision_guards;
        const uint16_t* idx = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(node) + 16 +
                                                                node->static_offset[0]);
        for (int i = 0; i < node->static_count[0]; ++i) {
            WCollisionInstance& ci = insts[idx[i]];
            if (ci.query_stamp == stamp) continue;
            ci.query_stamp = stamp;
            if (ci.article == 0) continue;
            float pos[3];
            WCollisionInstance_CalcPosition(mgr, ci, pos);
            const float a = ee::abs(ee::sub(pos[0], point[0])), b = ee::abs(ee::sub(pos[2], point[2]));
            const float d = ee::lt(b, a) ? ee::add(a, ee::mul(b, 0.25f)) : ee::add(b, ee::mul(a, 0.25f));
            if (!ee::lt(d, ee::add(radius, ci.radius))) continue;
            if (!strip_lists) {
                list.push_back({&ci, nullptr});
                continue;
            }
            // Com as listas de faixas (o WCollider da câmera): só as instâncias com alguma faixa perto.
            std::shared_ptr<const StripList> strips = WCollisionMgr_GetInstanceStripList(mgr, ci, point, radius, flag_b);
            if (strips) list.push_back({&ci, std::move(strips)});
        }
    }
}

const StripVertex* WCollisionMgr_FindFaceInTriStrip(const WCollisionMgr& mgr, const float local[3],
                                                    const StripVertex* v) {
    const uint32_t flags = v[1].w;
    const int n = int32_t(v[0].w) - 2;
    const float px = local[0], pz = local[2];
    if (flags & 2) {
        for (int t = 0; t < n; ++t) {
            const StripVertex* tri = v + t;
            const uint8_t sub = subtype_of(tri);
            if (sub & 4) continue;  // ignorada pelas buscas sob um ponto
            if (inside_either(tri, px, pz) && (mgr.barrier_mask & sub) == 0) return tri;
        }
        return nullptr;
    }
    bool clockwise = (flags & 1) != 0;
    for (int t = 0; t < n; ++t, clockwise = !clockwise) {
        const StripVertex* tri = v + t;
        const uint8_t sub = subtype_of(tri);
        if (sub & 4) continue;
        const bool in = clockwise ? inside_le(tri, px, pz) : inside_ge(tri, px, pz);
        if (in && (mgr.barrier_mask & sub) == 0) return tri;
    }
    return nullptr;
}

bool WCollisionMgr_FindFaceInCInst(const WCollisionMgr& mgr, const float point[3], const WCollisionInstance& ci,
                                   WWorldPos& face, float& height) {
    float m[16];
    WCollisionInstance_MakeMatrix(mgr, ci, m, true);
    float local[3];
    xform(point, m, local);
    const uint8_t* art = at<uint8_t>(ci.article);
    if (!art) return false;
    if (ee::lt(ci.half_x, local[0]) || ee::lt(local[0], ee::neg(ci.half_x))) return false;
    if (ee::lt(ci.half_z, local[2]) || ee::lt(local[2], ee::neg(ci.half_z))) return false;
    const CollisionArticle* a = reinterpret_cast<const CollisionArticle*>(art);
    float best = kNoFace;
    const StripVertex* found = nullptr;
    const CollisionStrip* strips = reinterpret_cast<const CollisionStrip*>(art + 0x20);
    for (int s = 0; s < a->strip_count; ++s) {
        const CollisionStrip& st = strips[s];
        // O centro menos o ponto: FUN_0025a750 pela FPU, ou vsub.xyz com o caminho VU0 do gerente (os mesmos bits; o
        // lqc2 do caminho VU0 ignora os 4 bits de baixo do endereço, e uma faixa desalinhada conta como guarda).
        if (mgr.vu0_path && (u32(&st) & 15) != 0) ++g_world_collision_guards;
        const float dx = ee::sub(st.centre[0], local[0]), dz = ee::sub(st.centre[2], local[2]);
        const float r = ee::mul(ee::cvt_s(int32_t(st.radius)), 0.0625f);
        if (!ee::lt(ee::add(ee::mul(dx, dx), ee::mul(dz, dz)), ee::mul(r, r))) continue;
        const StripVertex* tri =
            WCollisionMgr_FindFaceInTriStrip(mgr, local, reinterpret_cast<const StripVertex*>(art + 0x20 + st.vertex_offset));
        if (!tri) continue;
        const float miny = ee::min(ee::min(tri[0].y, tri[1].y), tri[2].y);
        const float h = (ci.flags & 1) ? ee::sub(miny, ee::sub(local[1], 0.5f)) : ee::sub(ee::add(local[1], 0.5f), miny);
        if (!ee::lt(0.0f, h) || !ee::lt(h, best)) continue;
        best = h;
        found = tri;
        height = h;
    }
    if (!found) return false;
    OrthoInverse(m);
    face.surface[0] = reinterpret_cast<const uint8_t*>(&found[2].w)[0];
    face.surface[1] = reinterpret_cast<const uint8_t*>(&found[2].w)[1];
    std::memcpy(&face.surface[2], reinterpret_cast<const uint8_t*>(&found[2].w) + 2, 2);
    std::memcpy(&face.v1[3], &found[1].w, 4);
    const float c0[3] = {found[0].x, found[0].y, found[0].z}, c1[3] = {found[1].x, found[1].y, found[1].z},
                c2[3] = {found[2].x, found[2].y, found[2].z};
    xform(c0, m, face.v0);
    xform(c1, m, face.v1);
    xform(c2, m, face.v2);
    return true;
}

bool WWorldPos_FindClosestFace(WWorldPos& wp, const WCollisionMgr& mgr, const std::vector<InstanceListEntry>& list,
                               const float point[3]) {
    wp.valid = 0;
    float nearest = kNoFace;
    bool found = false;
    WWorldPos cand{};  // a face candidata (no PS2 a pilha: o w de v0 e o que não é escrito ficam como estavam)
    float h = 0;
    for (const InstanceListEntry& e : list) {
        cand.surface[0] = 0;
        cand.surface[1] = 0;
        // [driving] 0x00214338 WCollisionMgr::FindFaceInCInst(COORD3 &, WCollisionInstanceCache &, ...): sem lista de
        // faixas, a de todas as faixas (0x00213ff8).
        if (!WCollisionMgr_FindFaceInCInst(mgr, point, *e.instance, cand, h)) continue;
        found = true;
        if (!ee::lt(h, nearest)) continue;
        wp.instance = u32(e.instance);
        wp.valid = 1;
        wp.article = e.instance->article;
        std::memcpy(&wp, &cand, 0x30);
        nearest = h;
    }
    return found;
}

bool WWorldPos_FindClosestFace(WWorldPos& wp, WCollisionMgr& mgr, const WGrid& g, const float point[3], bool keep,
                               int32_t sim_step) {
    wp.last_query = sim_step;
    // Guardar a face só vale se ela ainda é da mesma instância com o mesmo artigo (sem face, o original vai direto à
    // busca).
    if (keep && wp.valid != 0 && (wp.instance == 0 || wp.article != at<WCollisionInstance>(wp.instance)->article))
        keep = false;
    bool inside = false;
    if (wp.valid != 0) {
        StripVertex tri[3];
        std::memcpy(&tri[0], wp.v0, 12);
        std::memcpy(&tri[1], wp.v1, 12);
        std::memcpy(&tri[2], wp.v2, 12);
        inside = inside_either(tri, point[0], point[2]);
    }
    if (inside && keep) return false;
    std::vector<InstanceListEntry> list;
    WCollisionMgr_GetInstanceList(mgr, g, list, point, 0.0f, false, true);
    WWorldPos_FindClosestFace(wp, mgr, list, point);
    return true;
}

void WWorldPos_MakeFaceAtPoint(WWorldPos& wp, const WCollisionMgr& mgr, const float point[3]) {
    const float d = ps2::ffloat(0x3e4ccccdu);  // 0,2
    wp.v0[0] = point[0];
    wp.v1[0] = ee::add(point[0], d);
    wp.v2[0] = point[0];
    wp.v0[1] = wp.v1[1] = wp.v2[1] = point[1];
    wp.v0[2] = point[2];
    wp.v1[2] = point[2];
    wp.v2[2] = ee::add(point[2], d);
    wp.surface[0] = 1;
    wp.surface[1] = 0;
    wp.surface[2] = wp.surface[3] = 0;
    wp.valid = 1;
    wp.instance = mgr.instances;  // a primeira instância
    wp.article = 0;
}

float WCollisionMgr_SurfaceBumpHeight(const float point[3], const uint8_t* surface) {
    // A tabela de saltos de 0x00398f60 pelo tipo - 2: 2, 3, 5 e 10 (frequência 2, 1 cm), 4 e 7 (2, 1,5 cm), 6 (o
    // buraco: frequência 0, -10), 8 e 9 nada.
    float freq = 0.0f, amp = 0.0f;
    switch (surface[0]) {
        case 2: case 3: case 5: case 10:
            freq = 2.0f;
            amp = ps2::ffloat(0x3c23d70au);
            break;
        case 4: case 7:
            freq = 2.0f;
            amp = ps2::ffloat(0x3c75c28fu);
            break;
        case 6:
            freq = 0.0f;
            amp = -10.0f;
            break;
        default:
            break;
    }
    if (ee::eq(amp, 0.0f)) return 0.0f;
    const float two_pi = ps2::ffloat(0x40c90fdbu);
    const float s = nf::fm::sin(ee::mul(ee::mul(point[2], freq), two_pi));
    const float c = nf::fm::cos(ee::mul(ee::mul(point[0], freq), two_pi));
    return ee::mul(ee::add(s, c), amp);
}

float WWorldMath_GetPlaneY(const float n[3], const float p0[3], const float p[3]) {
    if (ee::eq(n[1], 0.0f)) return p0[1];
    const float t = ee::add(ee::mul(n[0], ee::sub(p[0], p0[0])), ee::mul(n[2], ee::sub(p[2], p0[2])));
    return ee::sub(p0[1], ee::div(t, n[1]));
}

// ---------------------------------------------------------------- as faces ao longo de um segmento

namespace {

// FUN_0025b220: o produto 4x4 (linha de a vezes colunas de b), cada termo (((a0 b0 + a1 b1) + a2 b2) + a3 b3). O
// VU0_MATRIX4_mult (0x002cbb88, o caminho VU0 do gerente) faz as mesmas contas na mesma ordem (mula/madda/madd).
void mat_mul(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = ee::add(ee::add(ee::add(ee::mul(a[i * 4], b[j]), ee::mul(a[i * 4 + 1], b[4 + j])),
                                          ee::mul(a[i * 4 + 2], b[8 + j])),
                                   ee::mul(a[i * 4 + 3], b[12 + j]));
    std::memcpy(out, r, sizeof(r));
}

float dot3(const float a[3], const float b[3]) {
    return ee::add(ee::add(ee::mul(a[0], b[0]), ee::mul(a[1], b[1])), ee::mul(a[2], b[2]));
}

float len2(const float d[3]) { return ee::add(ee::add(ee::mul(d[0], d[0]), ee::mul(d[1], d[1])), ee::mul(d[2], d[2])); }

// A normal de (e1 x e2) unitária, para cima e com y no máximo 0,99999 (o fim de 0x00213bd8 e de GetWorldNormal).
void up_normal(const float e1[3], const float e2[3], float n[3]) {
    float c[3];
    cross(e1, e2, c);
    if (ee::eq(c[0], 0.0f) && ee::eq(c[1], 0.0f) && ee::eq(c[2], 0.0f)) {
        n[0] = 0.0f;
        n[1] = 1.0f;
        n[2] = 0.0f;
    } else {
        const float k = ee::rsqrt(1.0f, len2(c));
        n[0] = ee::mul(c[0], k);
        n[1] = ee::mul(c[1], k);
        n[2] = ee::mul(c[2], k);
    }
    if (ee::lt(n[1], 0.0f)) {
        n[0] = ee::neg(n[0]);
        n[1] = ee::neg(n[1]);
        n[2] = ee::neg(n[2]);
    }
    const float top = ps2::ffloat(0x3f7ff972u);  // 0,99999
    if (ee::le(top, n[1])) n[1] = top;
}

}  // namespace

std::shared_ptr<const StripList> WCollisionMgr_GetInstanceStripList(const WCollisionMgr& mgr, const WCollisionInstance& ci,
                                                                    const float point[3], float radius, bool flat) {
    const uint8_t* art = at<uint8_t>(ci.article);
    if (!art) return nullptr;
    float m[16];
    WCollisionInstance_MakeMatrix(mgr, ci, m, true);
    float local[3];
    xform(point, m, local);
    const CollisionArticle* a = reinterpret_cast<const CollisionArticle*>(art);
    if (a->strip_count == 0) return nullptr;
    const float sixteenth = 0.0625f, third = ps2::ffloat(0x3eaaaaabu);
    std::shared_ptr<StripList> list;
    const CollisionStrip* strips = reinterpret_cast<const CollisionStrip*>(art + 0x20);
    for (int s = 0; s < a->strip_count; ++s) {
        const CollisionStrip& st = strips[s];
        const float d[3] = {ee::sub(st.centre[0], local[0]), ee::sub(st.centre[1], local[1]), ee::sub(st.centre[2], local[2])};
        const float dist = flat ? ee::add(ee::mul(d[0], d[0]), ee::mul(d[2], d[2])) : len2(d);
        const float reach = ee::add(ee::mul(ee::cvt_s(int32_t(st.radius)), sixteenth), radius);
        if (!ee::lt(dist, ee::mul(reach, reach))) continue;
        const StripVertex* v = reinterpret_cast<const StripVertex*>(art + 0x20 + st.vertex_offset);
        const int n = int32_t(v[0].w) - 2;
        for (int t = 0; t < n; ++t) {
            const StripVertex* tri = v + t;
            const float c[3] = {ee::mul(ee::add(ee::add(tri[0].x, tri[1].x), tri[2].x), third),
                                ee::mul(ee::add(ee::add(tri[0].y, tri[1].y), tri[2].y), third),
                                ee::mul(ee::add(ee::add(tri[0].z, tri[1].z), tri[2].z), third)};
            uint16_t tr;
            std::memcpy(&tr, reinterpret_cast<const uint8_t*>(&tri[2].w) + 2, 2);
            const float r = ee::add(radius, ee::mul(ee::cvt_s(int32_t(tr)), sixteenth));
            bool near;
            if (flat) {
                const float dx = ee::sub(c[0], local[0]), dz = ee::sub(c[2], local[2]);
                near = ee::lt(ee::add(ee::mul(dx, dx), ee::mul(dz, dz)), ee::mul(r, r));
            } else {
                // A FPU ou o caminho VU0 (vsub/vmul/vmulax/vmadday/vmaddz com 1,0): os mesmos bits.
                const float e[3] = {ee::sub(c[0], local[0]), ee::sub(c[1], local[1]), ee::sub(c[2], local[2])};
                near = ee::lt(len2(e), ee::mul(r, r));
            }
            if (!near) continue;
            if (!list) list = std::make_shared<StripList>();
            list->push_back(&st);
            break;
        }
    }
    return list;
}

bool WWorldMath_MakeSegSpaceMatrix(const float a[3], const float b[3], float m[16]) {
    const float eps = ps2::ffloat(0x38d1b717u);  // 1e-4
    if (ee::lt(ee::abs(ee::sub(a[0], b[0])), eps) && ee::lt(ee::abs(ee::sub(a[1], b[1])), eps) &&
        ee::lt(ee::abs(ee::sub(a[2], b[2])), eps))
        return false;
    float* r0 = m;
    float* r1 = m + 4;
    float* r2 = m + 8;
    r1[0] = ee::sub(a[0], b[0]);
    r1[1] = ee::sub(a[1], b[1]);
    r1[2] = ee::sub(a[2], b[2]);
    r1[3] = 0.0f;
    const float k = ee::rsqrt(1.0f, len2(r1));
    r1[0] = ee::mul(r1[0], k);
    r1[1] = ee::mul(r1[1], k);
    r1[2] = ee::mul(r1[2], k);
    if (ee::lt(0.5f, r1[1])) {
        r0[0] = 1.0f;
        r0[1] = 0.0f;
    } else {
        r0[1] = 1.0f;
        r0[0] = 0.0f;
    }
    r0[2] = 0.0f;
    r0[3] = 0.0f;
    namespace vu = ps2::vu;
    vu::Reg v0, v1, acc{}, d{};
    vu::load(v0, r0);
    vu::load(v1, r1);
    vu::opmula(acc, v0, v1);
    vu::opmsub(d, acc, v1, v0);
    std::memcpy(r2, d, 12);
    r2[3] = 0.0f;
    const float l = len2(r2);
    const float s = ee::eq(l, r2[3]) ? r2[3] : ee::rsqrt(1.0f, l);
    r2[0] = ee::mul(r2[0], s);
    r2[1] = ee::mul(r2[1], s);
    r2[2] = ee::mul(r2[2], s);
    vu::Reg w1, w2, acc2{}, e{};
    vu::load(w1, r1);
    vu::load(w2, r2);
    vu::opmula(acc2, w1, w2);
    vu::opmsub(e, acc2, w2, w1);
    std::memcpy(r0, e, 12);
    m[12] = a[0];
    m[13] = a[1];
    m[14] = a[2];
    m[15] = 1.0f;
    return true;
}

bool WWorldMath_IntersectSegPlane(const float a[3], const float b[3], const float on_plane[3], const float normal[3],
                                  float out[3], float& t) {
    const float d1[3] = {ee::sub(on_plane[0], a[0]), ee::sub(on_plane[1], a[1]), ee::sub(on_plane[2], a[2])};
    const float d2[3] = {ee::sub(b[0], a[0]), ee::sub(b[1], a[1]), ee::sub(b[2], a[2])};
    const float num = dot3(normal, d1);
    const float den = dot3(normal, d2);
    if (ee::eq(den, 0.0f)) return false;
    t = ee::div(num, den);
    out[0] = ee::add(a[0], ee::mul(t, ee::sub(b[0], a[0])));
    out[1] = ee::add(a[1], ee::mul(t, ee::sub(b[1], a[1])));
    out[2] = ee::add(a[2], ee::mul(t, ee::sub(b[2], a[2])));
    return ee::le(0.0f, t) && ee::le(t, 1.0f);
}

void WCollisionMgr_TriangleNormal(float out[3], const float* tri) {
    const float e1[3] = {ee::sub(tri[4], tri[0]), ee::sub(tri[5], tri[1]), ee::sub(tri[6], tri[2])};
    const float e2[3] = {ee::sub(tri[0], tri[8]), ee::sub(tri[1], tri[9]), ee::sub(tri[2], tri[10])};
    up_normal(e1, e2, out);
}

const StripVertex* WCollisionMgr_FindFaceInTriStripSeg(const WCollisionMgr& mgr, const float m[16], const float point[3],
                                                       const StripVertex* v, float& height) {
    const int n = int32_t(v[0].w) - 2;
    // Os vértices levados ao referencial (no PS2 num buffer do scratchpad, ou em 0x417860 com 180 triângulos ou mais).
    std::vector<StripVertex> buf(size_t(n > 0 ? n + 2 : 2));
    auto put = [&](int i) {
        const float src[3] = {v[i].x, v[i].y, v[i].z};
        float dst[3];
        xform(src, m, dst);
        buf[size_t(i)].x = dst[0];
        buf[size_t(i)].y = dst[1];
        buf[size_t(i)].z = dst[2];
    };
    put(0);
    put(1);
    float best = kNoFace;
    const StripVertex* found = nullptr;
    for (int t = 0; t < n; ++t) {
        put(t + 2);
        const StripVertex* tri = &buf[size_t(t)];
        if (!inside_either(tri, point[0], point[2])) continue;
        float normal[3];
        const float corners[12] = {tri[0].x, tri[0].y, tri[0].z, 0, tri[1].x, tri[1].y, tri[1].z, 0,
                                   tri[2].x, tri[2].y, tri[2].z, 0};
        WCollisionMgr_TriangleNormal(normal, corners);
        const float on_plane[3] = {tri[2].x, tri[2].y, tri[2].z};
        const float d = ee::sub(point[1], WWorldMath_GetPlaneY(normal, on_plane, point));
        height = d;
        if (!ee::lt(d, best) || !ee::lt(-1.0f, d)) continue;
        if ((mgr.barrier_mask & subtype_of(v + t)) != 0) continue;
        best = d;
        found = v + t;
    }
    height = best;
    return found;
}

bool WCollisionMgr_FindFaceInCInstSeg(const WCollisionMgr& mgr, const float seg[16], const float end[3],
                                      const InstanceListEntry& entry, WWorldPos& face, float& height) {
    const WCollisionInstance& ci = *entry.instance;
    float m[16];
    WCollisionInstance_MakeMatrix(mgr, ci, m, true);  // o mundo para a instância
    float to_world[16];
    std::memcpy(to_world, m, sizeof(m));
    OrthoInverse(to_world);
    float to_seg[16];
    std::memcpy(to_seg, seg, sizeof(to_seg));
    OrthoInverse(to_seg);
    float local_to_seg[16];
    mat_mul(to_world, to_seg, local_to_seg);
    const uint8_t* art = at<uint8_t>(ci.article);
    if (!art) return false;
    float start[3], stop[3];
    xform(seg + 12, m, start);
    xform(end, m, stop);
    const float d[3] = {ee::sub(stop[0], start[0]), ee::sub(stop[1], start[1]), ee::sub(stop[2], start[2])};
    const float inv = ee::div(1.0f, len2(d));
    const CollisionArticle* a = reinterpret_cast<const CollisionArticle*>(art);
    const CollisionStrip* all = reinterpret_cast<const CollisionStrip*>(art + 0x20);
    const size_t count = entry.strips ? entry.strips->size() : size_t(a->strip_count);
    static const float kOrigin[3] = {0.0f, 0.0f, 0.0f};  // 0x00355b80: o começo do segmento no referencial dele
    float best = kNoFace;
    const StripVertex* found = nullptr;
    for (size_t i = 0; i < count; ++i) {
        const CollisionStrip& st = entry.strips ? *(*entry.strips)[i] : all[i];
        float t = ee::mul(ee::add(ee::add(ee::mul(ee::sub(st.centre[0], start[0]), d[0]),
                                          ee::mul(ee::sub(st.centre[1], start[1]), d[1])),
                                  ee::mul(ee::sub(st.centre[2], start[2]), d[2])),
                          inv);
        const float r = ee::mul(ee::cvt_s(int32_t(st.radius)), 0.0625f);
        const float r2 = ee::mul(r, r);
        if (mgr.vu0_path) {
            t = ee::max(ee::min(t, 1.0f), 0.0f);
        } else {
            t = ee::lt(t, 1.0f) ? t : 1.0f;
            if (ee::lt(t, 0.0f)) t = 0.0f;
        }
        const float closest[3] = {ee::add(start[0], ee::mul(t, d[0])), ee::add(start[1], ee::mul(t, d[1])),
                                  ee::add(start[2], ee::mul(t, d[2]))};
        const float e[3] = {ee::sub(st.centre[0], closest[0]), ee::sub(st.centre[1], closest[1]),
                            ee::sub(st.centre[2], closest[2])};
        if (!ee::lt(len2(e), r2)) continue;
        float h = 0;
        const StripVertex* tri = WCollisionMgr_FindFaceInTriStripSeg(
            mgr, local_to_seg, kOrigin, reinterpret_cast<const StripVertex*>(art + 0x20 + st.vertex_offset), h);
        if (!tri) continue;
        h = ee::add(h, 0.5f);
        if (!ee::lt(-1.0f, h) || !ee::lt(h, best)) continue;
        best = h;
        found = tri;
        height = best;
    }
    if (!found) return false;
    face.surface[0] = reinterpret_cast<const uint8_t*>(&found[2].w)[0];
    face.surface[1] = reinterpret_cast<const uint8_t*>(&found[2].w)[1];
    std::memcpy(&face.surface[2], reinterpret_cast<const uint8_t*>(&found[2].w) + 2, 2);
    std::memcpy(&face.v1[3], &found[1].w, 4);
    const float c0[3] = {found[0].x, found[0].y, found[0].z}, c1[3] = {found[1].x, found[1].y, found[1].z},
                c2[3] = {found[2].x, found[2].y, found[2].z};
    xform(c0, to_world, face.v0);
    xform(c1, to_world, face.v1);
    xform(c2, to_world, face.v2);
    return true;
}

void WWorldPos_FindClosestFaceSeg(WWorldPos& wp, const WCollisionMgr& mgr, const std::vector<InstanceListEntry>& list,
                                  const float a[3], const float b[3], int32_t sim_step) {
    wp.valid = 0;
    wp.last_query = sim_step;
    float seg[16];
    if (!WWorldMath_MakeSegSpaceMatrix(a, b, seg)) return;
    wp.valid = 0;
    float best = kNoFace;
    WWorldPos face{};  // (no PS2 a pilha: o w de v0 fica com o que estava)
    for (const InstanceListEntry& e : list) {
        face.surface[0] = 0;
        face.surface[1] = 0;
        float h = 0;
        if (!WCollisionMgr_FindFaceInCInstSeg(mgr, seg, b, e, face, h)) continue;
        if (!ee::lt(h, best)) continue;
        best = h;
        wp.instance = u32(e.instance);
        wp.valid = 1;
        wp.article = e.instance->article;
        std::memcpy(&wp, &face, 0x30);
    }
}

// ---------------------------------------------------------------- TrackWorld

bool TrackWorld::init(UGroup* root, std::string* error) {
    UGroup* cdat = UGroup_GroupLocateTag(root, 0x43446174);  // 'CDat'
    if (cdat == UGroup_GroupEnd(root)) {
        if (error) *error = "a pista não tem o grupo 'CDat'";
        return false;
    }
    UData* ci = UGroup_DataLocateTag(cdat, 0x63690000);  // 'ci' 0000
    UData* co = UGroup_DataLocateTag(cdat, 0x636f0000);  // 'co' 0000
    if (ci == UGroup_DataEnd(cdat) || co == UGroup_DataEnd(cdat)) {
        if (error) *error = "o 'CDat' não tem 'ci' ou 'co'";
        return false;
    }
    // [driving] 0x00213110 WCollisionMgr::WCollisionMgr: a máscara 0xf0, o caminho VU0 desligado.
    mgr_ = WCollisionMgr{};
    mgr_.barrier_mask = 0xf0;
    mgr_.instances = u32(UData_Data(ci));
    mgr_.instance_count = ci->count;
    mgr_.objects = u32(UData_Data(co));
    object_count_ = co->count;
    // As instâncias de render do mundo (WWorld +0x20: o 'in  ' do 'Map ', o mesmo que o WRender lê), para os objetos.
    render_instances_ = 0;
    render_count_ = 0;
    UGroup* map = UGroup_GroupLocateTag(root, 0x4d617020);  // 'Map '
    if (map != UGroup_GroupEnd(root)) {
        UData* in = UGroup_DataLocateFirst(map, 0x696e2020, 0, -1);  // 'in  '
        if (in != UGroup_DataEnd(map)) {
            render_instances_ = u32(UData_Data(in));
            render_count_ = in->count;
        }
    }
    WCollisionInstance* insts = instances();
    for (uint32_t i = 0; i < mgr_.instance_count; ++i) {
        // O artigo: o grupo (resolvido pela 'sr' do 'ci') -> os dados do 'ca' 0000. Um artigo nulo derrubaria o PS2
        // (o original imprime o erro e lê o grupo nulo); aqui fica nulo e a instância não acha faces.
        UGroup* g = at<UGroup>(insts[i].article);
        if (!g) {
            ++g_world_collision_guards;
            continue;
        }
        UData* ca = UGroup_DataLocateTag(g, 0x63610000);
        insts[i].article = ca == UGroup_DataEnd(g) ? 0 : u32(UData_Data(ca));
    }
    // WGrid::Init (0x0021c1f8) e [driving] 0x0021c0f0 WGrid::WGrid.
    UData* grd = UGroup_DataLocateTag(cdat, 0x43477264);  // 'CGrd'
    if (grd == UGroup_DataEnd(cdat)) {
        if (error) *error = "o 'CDat' não tem 'CGrd'";
        return false;
    }
    const uint8_t* gr = UData_Data(grd);
    std::memcpy(grid_.origin, gr, 16);
    std::memcpy(&grid_.cell_size, gr + 0x10, 4);
    std::memcpy(&grid_.rows, gr + 0x18, 4);
    std::memcpy(&grid_.columns, gr + 0x1c, 4);
    grid_.inv_cell = ee::div(1.0f, grid_.cell_size);
    node_table_.assign(size_t(grid_.rows) * grid_.columns, 0u);
    grid_.nodes = u32(node_table_.data());
    // Os nós: os 'cn' em sequência a partir do primeiro, a contagem de cada registro é o número da célula.
    uint32_t count = 0;
    UData* items = UGroup_GroupEnd(cdat);
    for (uint32_t i = 0; i < cdat->count; ++i)
        if (UData_MatchTag(items + i) == 0x636e2020) ++count;
    UData* rec = UGroup_DataLocateFirst(cdat, 0x636e2020, 0, -1);
    for (uint32_t k = 0; k < count && rec != UGroup_DataEnd(cdat); ++k, ++rec)
        if (rec->count < node_table_.size()) node_table_[rec->count] = u32(UData_Data(rec));
    return true;
}

WGridNode* TrackWorld::node(uint32_t cell) const {
    return cell < node_table_.size() ? at<WGridNode>(node_table_[cell]) : nullptr;
}

bool TrackWorld::find_closest_face(WWorldPos& wp, const float pos[3], bool keep) {
    ++queries;
    const bool r = WWorldPos_FindClosestFace(wp, mgr_, grid_, pos, keep, simulation().steps);
    if (wp.valid) ++faces;
    return r;
}

void TrackWorld::make_face_at_point(WWorldPos& wp, const float pos[3]) { WWorldPos_MakeFaceAtPoint(wp, mgr_, pos); }

float TrackWorld::surface_bump_height(const float pos[3], const uint8_t* surface) {
    return WCollisionMgr_SurfaceBumpHeight(pos, surface);
}

bool TrackWorld::world_height_at_point(const float pos[3], float& height) {
    WWorldPos wp{};  // 0x00232180 WWorldPos::WWorldPos: tudo zerado
    WWorldPos_FindClosestFace(wp, mgr_, grid_, pos, true, simulation().steps);
    if (!wp.valid) return false;
    const float e1[3] = {ee::sub(wp.v1[0], wp.v0[0]), ee::sub(wp.v1[1], wp.v0[1]), ee::sub(wp.v1[2], wp.v0[2])};
    const float e2[3] = {ee::sub(wp.v0[0], wp.v2[0]), ee::sub(wp.v0[1], wp.v2[1]), ee::sub(wp.v0[2], wp.v2[2])};
    float n[3];
    cross(e1, e2, n);
    if (ee::eq(n[0], 0.0f) && ee::eq(n[1], 0.0f) && ee::eq(n[2], 0.0f)) {
        n[0] = 0.0f;
        n[1] = 1.0f;
        n[2] = 0.0f;
    } else {
        const float r = ee::rsqrt(1.0f, ee::add(ee::add(ee::mul(n[0], n[0]), ee::mul(n[1], n[1])), ee::mul(n[2], n[2])));
        n[2] = ee::mul(n[2], r);
        n[0] = ee::mul(n[0], r);
        n[1] = ee::mul(n[1], r);
    }
    if (ee::lt(n[1], 0.0f)) {
        n[1] = ee::neg(n[1]);
        n[0] = ee::neg(n[0]);
        n[2] = ee::neg(n[2]);
    }
    const float kMaxUp = ps2::ffloat(0x3f7ff972u);
    if (ee::le(kMaxUp, n[1])) n[1] = kMaxUp;
    height = WWorldMath_GetPlaneY(n, wp.v0, pos);
    return true;
}

}  // namespace drv
