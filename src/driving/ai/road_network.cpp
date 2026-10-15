// A rede de estradas (driving/ai/road_network.h), portada do MIPS do DRIVING.ELF (WRender.obj, RCameraSpline.obj). As
// desmontagens ficam em out/agent_drv_traffic/ do checkout principal (fora do git).
//
// Divergências (port): nenhuma de comportamento. As tabelas de ponteiros do heap do PS2 são std::vector de palavras
// p32; as linhas das tabelas de faixas guardam o endereço do .data do PS2 e são traduzidas por lane_offset_row /
// lane_weight_row (uma linha fora das tabelas copiadas, que o PS2 leria em outro .data, conta em road_network_guards e
// lê zeros).

#include "driving/ai/road_network.h"

#include <cstring>

#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

RoadNetworkGlobals g_road;
int g_guards = 0;
CameraSpline g_curve{};
bool g_curve_built = false;

constexpr uint32_t kOne = 0x3f800000u;

// As tabelas de faixas do .data (lidas do DRIVING.ELF): os deslocamentos de cada faixa a partir do meio, seis por tipo
// de faixa (só os tipos 0-3 são lidos), e os pesos das faixas, 32 por linha (só as linhas 0-9 são lidas). Uma para as
// pistas snow2a_mis4/snow2a_race, outra para as demais.
constexpr uint32_t kSnowOffsets = 0x33a618, kOffsets = 0x33a678, kSnowWeights = 0x33a6d8, kWeights = 0x33a818;
const float kLaneOffsets[2][4][6] = {
    {{0, 2.0f, 6.0f, 10.0f, 14.0f, 18.0f},
     {0, 3.0f, 7.0f, 11.0f, 15.0f, 19.0f},
     {0, 6.0f, 11.5f, 16.5f, 21.5f, 26.5f},
     {0, 8.0f, 13.0f, 18.0f, 23.0f, 28.0f}},
    {{0, 2.5f, 7.5f, 12.5f, 17.5f, 22.5f},
     {0, 4.0f, 9.0f, 14.0f, 19.0f, 24.0f},
     {0, 6.0f, 11.5f, 16.5f, 21.5f, 26.5f},
     {0, 8.0f, 13.0f, 18.0f, 23.0f, 28.0f}},
};
#define M1 -1, -1, -1, -1, -1, -1, -1, -1
const int8_t kLaneWeights[2][10][32] = {
    {{M1, M1, M1, -1, -1, 64, 64, 0, 0, 0, 0},
     {M1, M1, -1, -1, -1, -1, -1, -1, 64, 64, 0, 0, 0, 0, 0, 0, 0, 0},
     {M1, M1, M1, 64, 64, 32, 0, 0, 32, 64, 64},
     {M1, M1, -1, -1, -1, 64, 64, 32, 0, 0, 0, 0, 0, 0, 0, 32, 64, 64},
     {M1, M1, M1, -1, 64, 32, 0, 0, 0, 0, 0},
     {M1, M1, -1, -1, -1, -1, 64, 32, 0, 0, 0, 0, 0, 0, 0, 32, 64, 64},
     {M1, M1, -1, 64, 64, 64, 64, 64, 64, 32, 0, 0, 32, 64, 64, 64, 64, 64},
     {M1, -1, -1, -1, -1, -1, 64, 64, 64, 64, 64, 32, 0, 0, 0, 0, 0, 0, 0, 32, 64, 64, 64, 64, 64},
     {M1, M1, M1, -1, -1, -1, -1, -1, 64, 0, 0},
     {M1, M1, M1, -1, -1, -1, 64, 32, 0, 0, 0}},
    {{M1, M1, -1, -1, -1, -1, -1, -1, -1, 64, 64, 64, 64, 64, 32, 0, 0, 0},
     {M1, M1, -1, -1, 64, 64, 64, 64, 64, 32, 0, 0, 0, 0, 0, 0, 0, 0},
     {M1, M1, -1, -1, -1, -1, -1, 64, 64, 64, 64, 64, 32, 0, 0, 32, 64, 64},
     {M1, M1, 64, 64, 64, 64, 64, 32, 0, 0, 0, 0, 0, 0, 0, 32, 64, 64},
     {M1, M1, M1, -1, 64, 32, 0, 0, 0, 0, 0},
     {M1, M1, -1, -1, -1, -1, 64, 32, 0, 0, 0, 0, 0, 0, 0, 32, 64, 64},
     {M1, M1, -1, 64, 64, 64, 64, 64, 64, 32, 0, 0, 32, 64, 64, 64, 64, 64},
     {M1, -1, -1, -1, -1, -1, 64, 64, 64, 64, 64, 32, 0, 0, 0, 0, 0, 0, 0, 32, 64, 64, 64, 64, 64},
     {M1, M1, M1, -1, -1, -1, -1, -1, 64, 0, 0},
     {M1, M1, M1, -1, -1, -1, 64, 32, 0, 0, 0}},
};
#undef M1
const float kZeroRow[6] = {0, 0, 0, 0, 0, 0};
const int8_t kZeroWeights[32] = {};

// As bases do RCameraSpline (0x37e4a0 a Bézier; 0x37e4e0 a do tipo 1; 0x37e520 e 0x37e560 as derivadas).
const float kBezier[4][4] = {{-1, 3, -3, 1}, {3, -6, 3, 0}, {-3, 3, 0, 0}, {1, 0, 0, 0}};
const float kType1[4][4] = {{-1, 3, -3, 1}, {2, -5, 4, -1}, {-1, 0, 1, 0}, {0, 2, 0, 0}};
const float kBezierTangent[4][4] = {{0, 0, 0, 0}, {-3, 9, -9, 3}, {6, -12, 6, 0}, {-3, 3, 0, 0}};

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

// vmul.xyz; vmulax.x ACC; vmadday.x ACC, (1), v; vmaddz.x: o comprimento ao quadrado.
uint32_t len_sq(const float v[3]) {
    uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[1]), bits(v[1])));
    return ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2])));
}
// vsqrt Q; vaddq.x vf0, Q.
float vu_len(const float v[3]) { return flt(ps2::add(0, ps2::sqrt(len_sq(v)))); }
// vrsqrt Q, vf0w, s; vmulq.xyz.
void vu_unit(float d[3], const float v[3]) {
    const uint32_t q = ps2::rsqrt(kOne, len_sq(v));
    d[0] = flt(ps2::mul(bits(v[0]), q));
    d[1] = flt(ps2::mul(bits(v[1]), q));
    d[2] = flt(ps2::mul(bits(v[2]), q));
}
void vsub3(float d[3], const float a[3], const float b[3]) {
    for (int i = 0; i < 3; ++i) d[i] = ee::sub(a[i], b[i]);
}
void vadd3(float d[3], const float a[3], const float b[3]) {
    for (int i = 0; i < 3; ++i) d[i] = ee::add(a[i], b[i]);
}
void vscale3(float d[3], const float a[3], float k) {
    for (int i = 0; i < 3; ++i) d[i] = ee::mul(a[i], k);
}
// vmul.xyz; vaddy.x; vaddz.x
float vu_dot(const float a[3], const float b[3]) {
    const float x = ee::mul(a[0], b[0]), y = ee::mul(a[1], b[1]), z = ee::mul(a[2], b[2]);
    return ee::add(ee::add(x, y), z);
}

// O registro de dados de um UData (a posição relativa ou o endereço).
uint32_t record_of(const UData* d) { return nf::p32_u(UData_Data(d)); }

// Uma família de registros ('rn', 'rs'...) pelo índice: UGroup::DataLocateTag(tag | i) (i == -1: o tag com "  ").
uint32_t locate(const UGroup* g, uint32_t family, int32_t i) {
    const uint32_t tag = i != -1 ? (uint32_t(i) | family) : (family | 0x2020u);
    UData* d = UGroup_DataLocateTag(g, tag);
    if (d == UGroup_DataEnd(g)) return 0;
    return record_of(d);
}

// A Bézier: T = (t³, t², t, 1) vezes base x pontos (VU0_MATRIX4_mult e o vmulax/vmadday/vmaddaz/vmaddw).
void spline_eval(const float basis[4][4], const CameraSpline& s, float t, float out[4]) {
    const float t2 = ee::mul(t, t);
    const float t3 = ee::mul(t2, t);
    const float tv[4] = {t3, t2, t, 1.0f};
    float m[4][4];
    VU0_MATRIX4_mult(basis, s.points, m);
    for (int i = 0; i < 4; ++i) {
        uint32_t acc = ps2::mul(bits(m[0][i]), bits(tv[0]));
        acc = ps2::madd(acc, bits(m[1][i]), bits(tv[1]));
        acc = ps2::madd(acc, bits(m[2][i]), bits(tv[2]));
        out[i] = flt(ps2::madd(acc, bits(m[3][i]), bits(tv[3])));
    }
}

}  // namespace

RoadNetworkGlobals& road_network() { return g_road; }
int road_network_guards() { return g_guards; }
CameraSpline& curve_spline() { return g_curve; }
bool& curve_spline_built() { return g_curve_built; }

WRoadSegment* road_segment(int index) { return nf::p32_ptr<WRoadSegment>(g_road.segments[size_t(index)]); }
WRoadNode* road_node(int index) { return nf::p32_ptr<WRoadNode>(g_road.nodes[size_t(index)]); }
WRoad* road_road(int index) { return nf::p32_ptr<WRoad>(g_road.roads[size_t(index)]); }
WRoadIntersection* road_intersection(int index) {
    return nf::p32_ptr<WRoadIntersection>(g_road.intersections[size_t(index)]);
}

const float* lane_offset_row(uint32_t a) {
    for (int snow = 0; snow < 2; ++snow) {
        const uint32_t base = snow ? kSnowOffsets : kOffsets;
        if (a >= base && a < base + 4 * 0x18 && (a - base) % 0x18 == 0) return kLaneOffsets[snow][(a - base) / 0x18];
    }
    ++g_guards;
    return kZeroRow;
}
const int8_t* lane_weight_row(uint32_t a) {
    for (int snow = 0; snow < 2; ++snow) {
        const uint32_t base = snow ? kSnowWeights : kWeights;
        if (a >= base && a < base + 10 * 0x20 && (a - base) % 0x20 == 0) return kLaneWeights[snow][(a - base) / 0x20];
    }
    ++g_guards;
    return kZeroWeights;
}

// [driving] 0x00220c28 WRoadNetwork::Init
void WRoadNetwork_Init(UGroup* root, const std::string& track) {
    RoadNetworkGlobals& r = g_road;
    if (r.instance != 0) return;
    r.instance = 1;  // __builtin_new(1)
    const bool snow = track == "snow2a_mis4" || track == "snow2a_race";
    const uint32_t off = snow ? kSnowOffsets : kOffsets, wts = snow ? kSnowWeights : kWeights;
    for (int i = 0; i < 6; ++i) r.lane_offsets[i] = off + 0x18u * uint32_t(i);
    for (int i = 0; i < 32; ++i) r.lane_weights[i] = wts + 0x20u * uint32_t(i);
    r.right_side_system = 1;
    r.valid = 0;
    r.valid_traffic_roads = 0;
    r.num_junctions = r.num_roads = r.num_intersections = r.num_segments = r.num_nodes = 0;
    if (!root) return;
    UGroup* g = UGroup_GroupLocateTag(root, 0x524e6770);  // 'RNgp'
    if (g == UGroup_GroupEnd(root)) return;
    UData* hd = UGroup_DataLocateTag(g, 0x524e6864);  // 'RNhd'
    const uint16_t* h = reinterpret_cast<const uint16_t*>(UData_Data(hd));
    r.num_segments = h[1];
    r.num_nodes = h[0];
    r.num_intersections = h[2];
    r.num_junctions = h[4];
    r.num_roads = h[3];
    if (h[0] == 0) {
        r.valid = 0;
        return;
    }
    r.tables = true;
    r.nodes.assign(size_t(r.num_nodes), 0);
    r.segments.assign(size_t(r.num_segments), 0);
    r.intersections.assign(size_t(r.num_intersections), 0);
    r.junctions.assign(size_t(r.num_junctions), 0);
    r.roads.assign(size_t(r.num_roads), 0);
    for (int i = 0; i < r.num_nodes; ++i)
        if (uint32_t a = locate(g, 0x726e0000u, i)) r.nodes[size_t(i)] = a;
    for (int i = 0; i < r.num_segments; ++i) {
        const uint32_t a = locate(g, 0x72730000u, i);
        if (!a) continue;
        r.segments[size_t(i)] = a;
        WRoadSegment* s = nf::p32_ptr<WRoadSegment>(a);
        s->stamp[0] = r.segment_stamp[0];
        s->stamp[1] = r.segment_stamp[1];
        if ((s->flags & 0x8108) == 0) r.valid_traffic_roads = 1;  // ld +0x50 & 0x8108
    }
    for (int i = 0; i < r.num_intersections; ++i)
        if (uint32_t a = locate(g, 0x72690000u, i)) r.intersections[size_t(i)] = a;
    for (int i = 0; i < r.num_junctions; ++i)
        if (uint32_t a = locate(g, 0x726a0000u, i)) r.junctions[size_t(i)] = a;
    for (int i = 0; i < r.num_roads; ++i) {
        const uint32_t a = locate(g, 0x72720000u, i);
        if (!a) continue;
        r.roads[size_t(i)] = a;
        WRoad* road = nf::p32_ptr<WRoad>(a);
        road->count2e = 0;
        road->count2f = 0;
    }
    r.valid = 1;
}

// [driving] 0x002213d8 WRoadNetwork::Restart
void WRoadNetwork_Restart() {
    RoadNetworkGlobals& r = g_road;
    r.segment_stamp[0] = r.segment_stamp[1] = 0;
    for (int i = 0; i < r.num_segments; ++i) {
        WRoadSegment* s = road_segment(i);
        s->stamp[0] = s->stamp[1] = 0;
    }
    for (int i = 0; i < r.num_roads; ++i) {
        WRoad* road = road_road(i);
        road->count2e = 0;
        road->count2f = 0;
    }
}

// [driving] 0x002212f8 WRoadNetwork::Shutdown
void WRoadNetwork_Shutdown() {
    RoadNetworkGlobals& r = g_road;
    if (r.instance == 0) return;
    r.nodes.clear();
    r.segments.clear();
    r.intersections.clear();
    r.junctions.clear();
    r.roads.clear();
    r.tables = false;
    r.instance = 0;
}

// [driving] 0x00221528 WRoadNetwork::GetSpecialRoads
int WRoadNetwork_GetSpecialRoads(int32_t* out, int max) {
    int found = 0;
    for (int i = 0; i < g_road.num_segments; ++i) {
        const WRoadSegment* s = road_segment(i);
        if ((s->flags & kSegSpecial) == 0 || s->unknown59 == 0) continue;
        *out++ = i;
        if (++found == max) break;
    }
    return found;
}

// [driving] 0x002215f8 WRoadNetwork::GetSegmentNodes
void WRoadNetwork_GetSegmentNodes(const WRoadSegment* s, WRoadNode* out[2]) {
    out[0] = road_node(s->node[0]);
    out[1] = road_node(s->node[1]);
}

// [driving] 0x002216b0 WRoadNetwork::GetSegmentForwardVector
void WRoadNetwork_GetSegmentForwardVector(const WRoadSegment* s, float out[3]) { std::memcpy(out, s->forward, 12); }

// [driving] 0x00221730 WRoadNetwork::GetSegmentEndVecs
void WRoadNetwork_GetSegmentEndVecs(const WRoadSegment* s, float start[3], float end[3]) {
    if (s->flags & kSegCurved) {
        std::memcpy(start, s->start_across, 12);
        std::memcpy(end, s->end_across, 12);
        return;
    }
    float f[3];
    WRoadNetwork_GetSegmentForwardVector(s, f);
    const float across[3] = {f[2], f[1], ee::neg(f[0])};
    std::memcpy(start, across, 12);
    std::memcpy(end, start, 12);
}

// [driving] 0x00221888 WRoadNetwork::GetSegmentEndPoints
void WRoadNetwork_GetSegmentEndPoints(const WRoadSegment* s, float start[3], float end[3]) {
    WRoadNode* n[2];
    WRoadNetwork_GetSegmentNodes(s, n);
    std::memcpy(start, n[0]->position, 12);
    std::memcpy(end, n[1]->position, 12);
}

// [driving] 0x00221910 WRoadNetwork::GetSegmentEndPoints(WRoadSegment &, float, COORD3 &, COORD3 &)
void WRoadNetwork_GetSegmentEndPointsOffset(const WRoadSegment* s, float offset, float start[4], float end[4]) {
    float va[3], vb[3], pa[3], pb[3];
    WRoadNetwork_GetSegmentEndVecs(s, va, vb);
    WRoadNetwork_GetSegmentEndPoints(s, pa, pb);
    vscale3(va, va, offset);  // vmulx.xyz
    vadd3(start, pa, va);
    vscale3(vb, vb, offset);
    vadd3(end, pb, vb);
}

// [driving] 0x00221a10 WRoadNetwork::GetSegmentLength
float WRoadNetwork_GetSegmentLength(const WRoadSegment* s) {
    float a[3], b[3], d[3];
    WRoadNetwork_GetSegmentEndPoints(s, a, b);
    vsub3(d, a, b);
    return vu_len(d);
}

namespace {

// A largura de um lado (0x00221ac8 a esquerda, 0x00221cb0 a direita: mudam a palavra de +0x00/+0x04, a margem
// 0x200/0x400 e as faixas +0x52/+0x53).
float drivable_dist(const WRoadSegment* s, bool right) {
    float w = 0.0f;
    if (s->flags & (kSegWidths | kSegWidths2)) {  // ld +0x50 & 0x8008
        w = right ? flt(reinterpret_cast<const uint32_t*>(s)[1]) : s->start_control[0];
    } else if (s->flags & kSegNonDirectional) {
        float widest = ps2::ffloat(0xc61c4000u);  // -10000
        WRoadNode* n[2];
        WRoadNetwork_GetSegmentNodes(s, n);
        for (int i = 0; i < 2; ++i) {
            const WRoadSegment* a = GetAttachedDirectionalSegment(n[i], s->index);
            if (a) widest = ee::max(widest, drivable_dist(a, right));
        }
        if (ee::lt(ps2::ffloat(0xc61c4000u), widest)) w = widest;
    } else {
        const uint16_t f = s->flags;
        if (f & kSegLaneType3) w = 5.0f;
        if (f & kSegLanes) w = ee::add(w, 2.5f);
        if (f & (right ? kSegRightMargin : kSegLeftMargin)) w = ee::add(w, 5.0f);
        float lanes;
        if (f & kSegHalfLanes) {
            lanes = ee::mul(ee::mul(ee::cvt_s(int32_t(s->left_lanes) + int32_t(s->right_lanes)), 5.0f), 0.5f);
        } else {
            lanes = ee::mul(ee::cvt_s(right ? s->right_lanes : s->left_lanes), 5.0f);
        }
        w = ee::add(w, lanes);
    }
    if (ee::eq(w, 0.0f)) w = 5.0f;
    return w;
}

}  // namespace

// [driving] 0x00221ac8 WRoadNetwork::GetSegmentLeftDrivableDist
float WRoadNetwork_GetSegmentLeftDrivableDist(const WRoadSegment* s) { return drivable_dist(s, false); }
// [driving] 0x00221cb0 WRoadNetwork::GetSegmentRightDrivableDist
float WRoadNetwork_GetSegmentRightDrivableDist(const WRoadSegment* s) { return drivable_dist(s, true); }

// [driving] 0x00221e98 WRoadNetwork::GetSegmentLaneType
int WRoadNetwork_GetSegmentLaneType(const WRoadSegment* s) {
    if (s->flags & kSegLaneType3) return 3;
    if ((s->flags & 0x820) == 0x820) return 2;
    return (s->flags & kSegLanes) ? 1 : 0;
}

// [driving] 0x00221f20 WRoadNetwork::GetSegmentLaneIndex
int WRoadNetwork_GetSegmentLaneIndex(const WRoadSegment* s, float* offset) {
    const int type = WRoadNetwork_GetSegmentLaneType(s);
    const float* row = lane_offset_row(g_road.lane_offsets[type]);
    const float f = *offset;
    const float d = ee::abs(f);
    int lane = 0;
    float lane_offset = 0.0f;
    for (int i = 1; i < 6; ++i) {
        if (ee::lt(d, ee::add(row[i], 2.5f))) {
            lane = i;
            lane_offset = row[i];
            break;
        }
    }
    if (ee::lt(f, 0.0f) && s->left_lanes < lane) {
        lane = s->left_lanes;
        lane_offset = lane_offset_row(g_road.lane_offsets[type])[lane];
    } else if (ee::lt(0.0f, f) && s->right_lanes < lane) {
        lane = s->right_lanes;
        lane_offset = lane_offset_row(g_road.lane_offsets[type])[lane];
    }
    if (ee::lt(f, 0.0f)) {
        *offset = ee::neg(lane_offset);
        return -lane;
    }
    *offset = lane_offset;
    return lane;
}

// [driving] 0x00222060 WRoadNetwork::GetSegmentLaneWeights
void WRoadNetwork_GetSegmentLaneWeights(int segment, int8_t end, uint8_t* weights, int size, int count) {
    const WRoadSegment* s = road_segment(segment);
    if (s->flags & (kSegWidths | kSegWidths2)) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(s);
        const int first = ee::cvt_w(flt(end == 0 ? w[1] : w[0]));
        const int limit = count - first;
        for (int i = 0; i < count; ++i) weights[i] = i > limit ? 0 : 0xff;
        const int last = ee::cvt_w(flt(end == 0 ? w[0] : w[1]));
        if (count > 0) {
            uint8_t* p = weights + (size - count);
            const int lim = count + last;
            for (int k = count; k > 0; --k) *p++ = (size - k) < lim ? 0 : 0xff;
        }
        return;
    }
    if (s->flags & kSegNonDirectional) {
        const WRoadSegment* a = GetAttachedDirectionalSegment(road_node(s->node[int(end)]), -1);
        if (a) s = a;
    }
    const uint16_t f = s->flags;
    int row;
    if (f & kSegLaneType3) row = 6;
    else if ((f & 0x820) == 0x820) row = 4;
    else row = (f & kSegLanes) ? 2 : 0;
    const int lanes = int(s->left_lanes) + int(s->right_lanes);
    if (lanes >= 3) ++row;
    else if (lanes == 1) row = 8;
    if ((f & kSegLeftMargin) == 0 && row == 0) row = 9;
    const int8_t* r = lane_weight_row(g_road.lane_weights[row]);
    for (int i = 0; i < count; ++i) weights[i] = uint8_t(r[i]);
    uint8_t* p = weights + (size - count);
    for (int k = count; k > 0; --k) *p++ = uint8_t(r[k - 1]);
}

// [driving] 0x00222300 WRoadNetwork::GetSegmentOppNode
WRoadNode* WRoadNetwork_GetSegmentOppNode(const WRoadSegment* s, const WRoadNode* node) {
    WRoadNode* n[2];
    WRoadNetwork_GetSegmentNodes(s, n);
    return node == n[0] ? n[1] : n[0];
}

// [driving] 0x00222468 WRoadNetwork::GetPointOnSegment(COORD3 &, COORD3 &, WRoadSegment &, float, COORD3 &)
void WRoadNetwork_GetPointOnSegment(const float start[4], const float end[4], const WRoadSegment* s, float t,
                                    float out[4]) {
    if (s->flags & kSegCurved) {
        WRoadNetwork_GetSegmentCurveStep(start, end, s, t, out);
        return;
    }
    float d[3];
    vsub3(d, end, start);
    vscale3(d, d, t);
    vadd3(out, start, d);
}

// [driving] 0x002223b0 WRoadNetwork::GetPointOnSegment(WRoadSegment &, float, COORD3 &)
void WRoadNetwork_GetPointOnSegmentT(const WRoadSegment* s, float t, float out[4]) {
    if (ee::lt(1.0f, t)) t = 1.0f;
    if (ee::lt(t, 0.0f)) t = 0.0f;
    float a[4] = {}, b[4] = {};
    WRoadNetwork_GetSegmentEndPoints(s, a, b);
    WRoadNetwork_GetPointOnSegment(a, b, s, t, out);
}

// [driving] 0x002225d8 WRoadNetwork::GetPointAndVecOnSegment(COORD3 &, COORD3 &, WRoadSegment &, float, ...)
void WRoadNetwork_GetPointAndVecOnSegment(const float start[4], const float end[4], const WRoadSegment* s, float t,
                                          float point[4], float vec[3]) {
    WRoadNetwork_GetPointOnSegment(start, end, s, t, point);
    WRoadNetwork_GetSegmentForwardVector(s, vec);  // os dois ramos do PS2 fazem o mesmo
}

// [driving] 0x00222700 WRoadNetwork::GetPointOnSegmentDist
void WRoadNetwork_GetPointOnSegmentDist(const WRoadSegment* s, float dist, float out[4]) {
    const float len = WRoadNetwork_GetSegmentLength(s);
    if (ee::lt(len, dist)) dist = len;
    if (ee::lt(dist, 0.0f)) dist = 0.0f;
    WRoadNetwork_GetPointOnSegmentT(s, ee::div(dist, len), out);
}

// [driving] 0x00222810 WRoadNetwork::GetPointAndVecOnSegmentLane
void WRoadNetwork_GetPointAndVecOnSegmentLane(const WRoadSegment* s, float t, int lane, float point[4], float vec[3]) {
    const int type = WRoadNetwork_GetSegmentLaneType(s);
    float offset = lane_offset_row(g_road.lane_offsets[type])[lane < 0 ? -lane : lane];
    if (lane < 0) offset = ee::neg(offset);
    float a[4] = {}, b[4] = {};
    WRoadNetwork_GetSegmentEndPointsOffset(s, offset, a, b);
    WRoadNetwork_GetPointAndVecOnSegment(a, b, s, t, point, vec);
}

// [driving] 0x00222968 WRoadNetwork::GetSegmentPointIntersect(WRoadSegment &, ...)
float WRoadNetwork_GetSegmentPointIntersect(const WRoadSegment* s, const float point[4], float out[4], bool clamp) {
    float a[4] = {}, b[4] = {};
    std::memcpy(a, road_node(s->node[0])->position, 12);
    std::memcpy(b, road_node(s->node[1])->position, 12);
    return WRoadNetwork_GetLinePointIntersect(a, b, point, out, clamp);
}

// [driving] 0x00222a30 WRoadNetwork::GetLinePointIntersect
float WRoadNetwork_GetLinePointIntersect(const float start[4], const float end[4], const float point[4], float out[4],
                                         bool clamp) {
    float along[3], to[3], tmp[3];
    vsub3(along, end, start);
    vu_unit(along, along);
    vsub3(to, point, start);
    vsub3(tmp, point, start);
    const float dist = vu_len(tmp);
    vu_unit(to, to);
    const float projected = ee::mul(dist, vu_dot(along, to));
    vsub3(tmp, start, end);
    const float length = vu_len(tmp);
    if (clamp) {
        if (ee::le(length, projected)) {
            std::memcpy(out, end, 12);
            return 1.0f;
        }
        if (ee::le(projected, 0.0f)) {
            std::memcpy(out, start, 12);
            return 0.0f;
        }
    }
    vu_unit(along, along);
    vscale3(along, along, projected);
    vadd3(out, start, along);
    return ee::div(projected, length);
}

// [driving] 0x00222d40 WRoadNetwork::GetSegmentCurveStep(COORD3 &, COORD3 &, WRoadSegment &, float, COORD3 &)
void WRoadNetwork_GetSegmentCurveStep(const float start[4], const float end[4], const WRoadSegment* s, float t,
                                      float out[3]) {
    if (!g_curve_built) {
        // RCameraSpline::RCameraSpline (0x001a7ba0): a matriz identidade em +0x00..+0x3f (0x3ad6a8), vf0 em +0x40 e
        // +0x50, o tipo 2 e a lista vazia.
        g_curve = CameraSpline{};
        for (int i = 0; i < 4; ++i) g_curve.points[i][i] = 1.0f;
        g_curve.unknown40[3] = g_curve.unknown50[3] = 1.0f;
        g_curve.type = 2;
        g_curve_built = true;
    }
    RCameraSpline_BuildSplineEx(g_curve, start, s->start_control, end, s->end_control);
    float p[4];
    RCameraSpline_EvaluateSpline(g_curve, t, p);
    std::memcpy(out, p, 12);
}

// [driving] 0x002270e8 GetAttachedDirectionalSegment
WRoadSegment* GetAttachedDirectionalSegment(const WRoadNode* node, int16_t excluded) {
    for (int i = 0; i < node->segment_count; ++i) {
        WRoadSegment* s = road_segment(node->segments[i]);
        if (s->index == excluded) continue;
        if ((s->flags & kSegNonDirectional) == 0) return s;
    }
    return nullptr;
}

// [driving] 0x001a8380 RCameraSpline::BuildSplineEx
void RCameraSpline_BuildSplineEx(CameraSpline& s, const float p0[3], const float p1[3], const float p3[3],
                                 const float p2[3]) {
    // A lista de pontos (+0x64) da spline estática é sempre vazia: o PS2 conta os nós e só monta com menos de 2.
    s.type = 0;
    std::memcpy(s.points[0], p0, 12);
    std::memcpy(s.points[1], p1, 12);
    std::memcpy(s.points[2], p2, 12);
    std::memcpy(s.points[3], p3, 12);
    s.points[0][3] = s.points[1][3] = s.points[2][3] = s.points[3][3] = 0.0f;
}

// [driving] 0x001a85d0 RCameraSpline::EvaluateSpline(float, COORD4 &)
void RCameraSpline_EvaluateSpline(const CameraSpline& s, float t, float out[4]) {
    spline_eval(s.type == 1 ? kType1 : kBezier, s, t, out);
    out[3] = 1.0f;
    if (s.type == 1) vscale3(out, out, 0.5f);
}

// [driving] 0x001a86a8 RCameraSpline::EvaluateTangent(float, COORD4 &)
void RCameraSpline_EvaluateTangent(const CameraSpline& s, float t, float out[4]) {
    // A base de 0x001a7e00: a derivada da Bézier (0x37e520) ou a do tipo 1 (0x37e560, não lida: a spline do navegador é
    // sempre do tipo 0).
    spline_eval(kBezierTangent, s, t, out);
    vu_unit(out, out);
    out[3] = 1.0f;
}

}  // namespace drv
