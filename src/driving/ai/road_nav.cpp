// O navegador da rede de estradas (driving/ai/road_nav.h), portado do MIPS do DRIVING.ELF (WRender.obj). As desmontagens
// ficam em out/agent_drv_traffic/ do checkout principal (fora do git).
//
// Divergências (port), cada uma também na função:
//  - IncNavPosition: a linha de depuração que o PS2 formata (sprintf) num trecho de comprimento zero e não usa não é
//    formatada; o modo 2 (CalcNextSegmentSidewalk, os pedestres) conta em road_nav_stats().pending_modes e fica no
//    trecho (como um modo desconhecido); a recursão de um trecho em outro para (guarda) depois de 256 níveis: num laço
//    de trechos de comprimento zero o PS2 recorreria até estourar a pilha (o passo vira 0 x FLT_MAX = 0 e nunca acaba);
//  - CalcNextSegmentLane: um nó de longe sem trecho com direção (o PS2 leria o endereço 0x50) conta como guarda e o
//    candidato é pulado; o mesmo no fim, para as faixas do trecho escolhido sem direção.
//  - InitAtPoint num ponto cuja célula da grade não tem trechos: o FindClosestSegmentInd devolve 0 sem escrever o t, e o
//    PS2 usa o lixo da pilha (sp+0xd0); o port usa 0 (contado em road_nav_stats().stack_garbage).

#include "driving/ai/road_nav.h"

#include <cstring>

#include "driving/ai/sim_random.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

RoadNavStats g_stats;
bool g_empty_cell = false;  // o último FindClosestSegmentInd saiu sem olhar trecho nenhum (o 0 da célula vazia)
constexpr uint32_t kOne = 0x3f800000u;

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

uint32_t len_sq(const float v[3]) {
    uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    acc = ps2::madd(acc, kOne, ps2::mul(bits(v[1]), bits(v[1])));
    return ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2])));
}
float vu_len(const float v[3]) { return flt(ps2::add(0, ps2::sqrt(len_sq(v)))); }
// vmul.xyz; vmulax.x; vmaddz.x (x² + z²); vsqrt; vaddq.x
float vu_len_xz(const float v[3]) {
    const uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    return flt(ps2::add(0, ps2::sqrt(ps2::madd(acc, kOne, ps2::mul(bits(v[2]), bits(v[2]))))));
}
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
float vu_dot(const float a[3], const float b[3]) {
    const float x = ee::mul(a[0], b[0]), y = ee::mul(a[1], b[1]), z = ee::mul(a[2], b[2]);
    return ee::add(ee::add(x, y), z);
}
void copy3(float d[3], const float s[3]) { std::memcpy(d, s, 12); }
void set_offset(WRoadNav& n, float o) {
    n.lane_change_from = o;
    n.lane_offset = o;
    n.lane_change_to = o;
}

const float* offsets_of(const WRoadSegment* s) {
    return lane_offset_row(road_network().lane_offsets[WRoadNetwork_GetSegmentLaneType(s)]);
}

// O ponto na curva em t (o fim de InitAtPoint, IncNavPosition e ChangeLanes): a Bézier, ou a reta entre as pontas.
void place_on_curve(WRoadNav& n, const WRoadSegment* s) {
    if (s->flags & kSegCurved) {
        float p[4];
        RCameraSpline_EvaluateSpline(*road_nav_spline(n), n.t, p);
        copy3(n.position, p);
        copy3(n.road_point, n.position);
    } else {
        float a[4] = {}, b[4] = {};
        copy3(a, n.bound_start);
        copy3(b, n.bound_end);
        WRoadNetwork_GetPointOnSegment(a, b, s, n.t, n.position);
        copy3(n.road_point, n.position);
    }
}

void build_spline(WRoadNav& n) {
    RCameraSpline_BuildSplineEx(*road_nav_spline(n), n.bound_start, n.start_control, n.bound_end, n.end_control);
}

// A calçada do lado (InitAtPoint e InitAtSegment no modo 2).
void sidewalk_lane(WRoadNav& n, const WRoadSegment* s, bool left) {
    if (left) {
        const int8_t lane = int8_t(~uint8_t(s->left_lanes));
        n.lane = n.lane2 = lane;
        const float o = ee::add(ee::neg(offsets_of(s)[s->left_lanes + 1]), 1.0f);
        set_offset(n, o);
        if (s->right_lanes == 0) set_offset(n, ee::add(o, 2.0f));
    } else {
        const int8_t lane = int8_t(uint8_t(s->right_lanes) + 1);
        n.lane = n.lane2 = lane;
        const float o = ee::sub(offsets_of(s)[s->right_lanes + 1], 1.0f);
        set_offset(n, o);
        if (s->left_lanes == 0) set_offset(n, ee::sub(o, 2.0f));
    }
}

}  // namespace

RoadNavStats& road_nav_stats() { return g_stats; }
CameraSpline* road_nav_spline(const WRoadNav& n) { return nf::p32_ptr<CameraSpline>(n.spline); }

// [driving] 0x00223338 WRoadNav::WRoadNav
void WRoadNav_Construct(WRoadNav& n) {
    // UMemory::FastAlloc(0x70) e RCameraSpline::RCameraSpline (0x001a7ba0).
    CameraSpline* s = new CameraSpline{};
    for (int i = 0; i < 4; ++i) s->points[i][i] = 1.0f;
    s->unknown40[3] = s->unknown50[3] = 1.0f;
    s->type = 2;
    n.spline = nf::p32_u(s);
    WRoadNav_Reset(n);
}

// [driving] 0x00223380 WRoadNav::~WRoadNav
void WRoadNav_Destruct(WRoadNav& n) {
    delete road_nav_spline(n);
    n.spline = 0;
}

// [driving] 0x002233d0 WRoadNav::Reset
void WRoadNav_Reset(WRoadNav& n) {
    const float zero[3] = {0, 0, 0};
    n.mode = 0;
    n.unknown04 = 0;
    copy3(n.position, zero);
    copy3(n.road_point, zero);
    copy3(n.direction, zero);
    n.forward = 0;
    n.segment = 0;
    n.t = 0;
    copy3(n.bound_start, zero);
    copy3(n.bound_end, zero);
    copy3(n.start_control, zero);
    copy3(n.end_control, zero);
    n.next_choice = -1;
    n.unknownb2 = 0;
    n.at_dead_end = 0;
    n.lane = 0;
    n.lane2 = 0;
    n.lane_offset = 0;
    n.lane_change_from = 0;
    n.lane_change_to = 0;
    n.lane_change_fraction = 0;
    n.lane_change_distance = 0;
    n.lane_change_length = 0;
    n.unknownb1 = 0;
}

// [driving] 0x00225490 WRoadNav::FindClosestSegmentInd
int WRoadNav_FindClosestSegmentInd(WRoadNav& n, const float point[4], float closest[3], float* along) {
    RoadNetworkGlobals& r = road_network();
    g_empty_cell = true;
    TrackWorld* tw = world() ? world()->track_world() : nullptr;
    if (!tw) return 0;
    const WGrid& g = tw->grid();
    // WGrid::RangeCheckROWCOL (dentro): a coluna pelo x, a linha pelo z; fora da grade, a borda (negativo: 0).
    uint32_t col = uint32_t(ee::cvt_w(ee::mul(ee::sub(point[0], g.origin[0]), g.inv_cell)));
    uint32_t row = uint32_t(ee::cvt_w(ee::mul(ee::sub(point[2], g.origin[2]), g.inv_cell)));
    if (col >= g.columns) col = col > 0x7ffffffeu ? 0 : g.columns - 1;
    if (row >= g.rows) row = row > 0x7ffffffeu ? 0 : g.rows - 1;
    const WGridNode* node = tw->node(row * g.columns + col);
    if (!node) return 0;
    if (node->static_count[3] == 0) return 0;
    g_empty_cell = false;
    uint64_t stamp = (uint64_t(r.segment_stamp[1]) << 32 | r.segment_stamp[0]) + 1;
    r.segment_stamp[0] = uint32_t(stamp);
    r.segment_stamp[1] = uint32_t(stamp >> 32);
    int best = -1;
    bool found = false;
    float nearest = ps2::ffloat(0x469c4000u);  // 20000
    const int16_t* list = reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(node) + 0x10 +
                                                           node->static_offset[3]);
    for (int i = 0; i < node->static_count[3]; ++i) {
        const int16_t index = list[i];
        if (!(index < r.num_segments)) continue;
        WRoadSegment* s = road_segment(index);
        if (s->stamp[0] == r.segment_stamp[0] && s->stamp[1] == r.segment_stamp[1]) continue;
        const uint16_t flags = s->flags;
        s->stamp[0] = r.segment_stamp[0];
        s->stamp[1] = r.segment_stamp[1];
        if (flags & kSegSpecial) continue;
        if (n.mode == 4) {
            if ((flags & kSegWidths) == 0) continue;
            if (n.unknown04 != s->unknown5a) continue;
        } else if (flags & kSegWidths) {
            continue;
        }
        if (n.mode == 5) {
            if ((s->flags & kSegWidths2) == 0) continue;
        } else if (s->flags & kSegWidths2) {
            continue;
        }
        float on_road[4] = {};
        const float t = WRoadNetwork_GetSegmentPointIntersect(s, point, on_road, true);
        if (s->flags & kSegCurved) WRoadNetwork_GetPointOnSegmentT(s, t, on_road);
        float d[3];
        vsub3(d, point, on_road);
        const float dist = vu_len(d);
        if (found && !ee::lt(dist, nearest)) continue;
        best = index;
        nearest = dist;
        found = true;
        copy3(closest, on_road);
        *along = t;
    }
    return best;
}

// [driving] 0x00225c48 WRoadNav::InitAtPoint
void WRoadNav_InitAtPoint(WRoadNav& n, const float point[3], const float heading[3], bool no_lane) {
    float at[4] = {point[0], point[1], point[2], 0};
    const float facing[3] = {heading[0], heading[1], heading[2]};
    float road_t = 0;  // sp+0xd0: no PS2, lixo da pilha se a célula não tem trechos (o 0 de FindClosestSegmentInd)
    const int index = WRoadNav_FindClosestSegmentInd(n, at, n.road_point, &road_t);
    if (g_empty_cell) ++g_stats.stack_garbage;
    if (index == -1) {
        n.segment = int16_t(index);
        return;
    }
    n.at_dead_end = 0;
    n.segment = int16_t(index);
    const WRoadSegment* s = road_segment(index);
    float fwd[3];
    WRoadNetwork_GetSegmentForwardVector(s, fwd);
    float dot = vu_dot(facing, fwd);
    if (ee::eq(facing[0], 0.0f) && ee::eq(facing[1], 0.0f) && ee::eq(facing[2], 0.0f)) dot = 1.0f;
    const bool random_free = n.mode == 4 && n.unknownb1 == 0 && n.unknownb2 == 0;
    if (!random_free && ee::lt(dot, 0.0f)) {
        const float neg[3] = {ee::neg(fwd[0]), ee::neg(fwd[1]), ee::neg(fwd[2])};
        n.forward = 0;
        copy3(n.direction, neg);
        n.t = ee::abs(ee::sub(1.0f, road_t));
        copy3(n.bound_start, road_node(s->node[1])->position);
        copy3(n.bound_end, road_node(s->node[0])->position);
    } else {
        n.forward = 1;
        copy3(n.direction, fwd);
        n.t = road_t;
        copy3(n.bound_start, road_node(s->node[0])->position);
        copy3(n.bound_end, road_node(s->node[1])->position);
    }
    n.lane2 = 0;
    n.lane = 0;
    n.lane_offset = 0;
    n.lane_change_to = 0;
    n.lane_change_from = 0;
    if (!no_lane) {
        float nearest[3];
        copy3(nearest, n.road_point);
        float from_start[3];
        vsub3(from_start, at, n.bound_start);
        float offset;
        if (ee::lt(vu_dot(from_start, n.direction), n.lane_offset)) {
            const float across[3] = {n.direction[2], n.direction[1], ee::neg(n.direction[0])};
            const float side = vu_dot(from_start, across);
            float foot[3];
            vscale3(foot, across, side);
            vadd3(foot, nearest, foot);
            float d[3];
            vsub3(d, foot, nearest);
            offset = vu_len_xz(d);
        } else {
            float d[3];
            vsub3(d, at, nearest);
            offset = vu_len_xz(d);
        }
        set_offset(n, offset);
        float side[3];
        if (ee::le(n.t, ps2::ffloat(0x3e4ccccdu))) {  // 0,2
            side[0] = n.direction[2];
            side[1] = n.direction[1];
            side[2] = ee::neg(n.direction[0]);
        } else {
            float to_road[3];
            vsub3(to_road, nearest, n.bound_start);
            vu_unit(to_road, to_road);
            side[0] = to_road[2];
            side[1] = to_road[1];
            side[2] = ee::neg(to_road[0]);
        }
        float to_point[3];
        vsub3(to_point, at, nearest);
        vu_unit(to_point, to_point);
        if (ee::lt(vu_dot(side, to_point), 0.0f)) set_offset(n, ee::neg(n.lane_offset));
        if (n.mode == 4) {
            float o;
            if (ee::lt(n.lane_offset, 0.0f))
                o = ee::max(ee::neg(WRoadNetwork_GetSegmentLeftDrivableDist(road_segment(n.segment))), n.lane_offset);
            else
                o = ee::min(WRoadNetwork_GetSegmentRightDrivableDist(road_segment(n.segment)), n.lane_offset);
            set_offset(n, o);
        }
        if (n.mode == 2) {
            sidewalk_lane(n, s, ee::lt(n.lane_offset, 0.0f));
        } else if (n.mode == 1) {
            float o = ee::max(2.5f, n.lane_offset);
            const int8_t lane = int8_t(WRoadNetwork_GetSegmentLaneIndex(s, &o));
            n.lane = lane;
            n.lane_change_from = o;
            n.lane2 = lane;
            n.lane_offset = o;
            n.lane_change_to = o;
        }
    }
    WRoadNav_SetStartEndPos(n, s, n.lane_offset, n.lane_offset);
    WRoadNav_SetStartEndControls(n, s);
    float a[4] = {}, b[4] = {}, on[4] = {};
    copy3(a, n.bound_start);
    copy3(b, n.bound_end);
    n.t = WRoadNetwork_GetLinePointIntersect(a, b, at, on, true);
    if (s->flags & kSegCurved) {
        build_spline(n);
        float p[4];
        RCameraSpline_EvaluateSpline(*road_nav_spline(n), n.t, p);
        copy3(n.road_point, p);
        copy3(n.position, n.road_point);
    } else {
        WRoadNetwork_GetPointOnSegment(a, b, s, n.t, n.position);
        copy3(n.road_point, n.position);
    }
}

// [driving] 0x002257c0 WRoadNav::InitAtSegment
void WRoadNav_InitAtSegment(WRoadNav& n, int16_t segment, int8_t lane, float t, bool forwards) {
    n.at_dead_end = 0;
    n.segment = segment;
    const WRoadSegment* s = road_segment(segment);
    float fwd[3];
    WRoadNetwork_GetSegmentForwardVector(s, fwd);
    if (n.mode != 4 && !forwards) {
        const float neg[3] = {ee::neg(fwd[0]), ee::neg(fwd[1]), ee::neg(fwd[2])};
        n.forward = 0;
        copy3(n.direction, neg);
        n.t = ee::abs(ee::sub(1.0f, t));
        copy3(n.bound_start, road_node(s->node[1])->position);
        copy3(n.bound_end, road_node(s->node[0])->position);
    } else {
        copy3(n.direction, fwd);
        n.forward = 1;
        n.t = t;
        copy3(n.bound_start, road_node(s->node[0])->position);
        copy3(n.bound_end, road_node(s->node[1])->position);
    }
    n.lane2 = 0;
    n.lane = 0;
    n.lane_offset = 0;
    n.lane_change_to = 0;
    n.lane_change_from = 0;
    if (n.mode == 2) {
        sidewalk_lane(n, s, lane < 0);
    } else {
        n.lane2 = lane;
        n.lane = lane;
        const float o = offsets_of(s)[lane < 0 ? -lane : lane];
        set_offset(n, lane < 0 ? ee::neg(o) : o);
    }
    WRoadNav_SetStartEndPos(n, s, n.lane_offset, n.lane_offset);
    WRoadNav_SetStartEndControls(n, s);
    if (s->flags & kSegCurved) {
        build_spline(n);
        float p[4];
        RCameraSpline_EvaluateSpline(*road_nav_spline(n), n.t, p);
        copy3(n.road_point, p);  // só o road_point
    } else {
        float a[4] = {}, b[4] = {};
        copy3(a, n.bound_start);
        copy3(b, n.bound_end);
        WRoadNetwork_GetPointOnSegment(a, b, s, n.t, n.position);
        copy3(n.road_point, n.position);
    }
}

// [driving] 0x002267d8 WRoadNav::SetBoundPos
void WRoadNav_SetBoundPos(WRoadNav& n, const WRoadSegment* s, float offset, float pos[3], bool at_start) {
    const int end = n.forward == 0 ? (at_start ? 1 : 0) : (at_start ? 0 : 1);
    float node[3];
    copy3(node, road_node(s->node[end])->position);
    float across[3];
    if (at_start) {
        if (n.forward != 0) copy3(across, s->start_across);
        else for (int i = 0; i < 3; ++i) across[i] = ee::neg(s->end_across[i]);
    } else {
        if (n.forward != 0) copy3(across, s->end_across);
        else for (int i = 0; i < 3; ++i) across[i] = ee::neg(s->start_across[i]);
    }
    vscale3(across, across, offset);
    vadd3(pos, node, across);
}

// [driving] 0x00226538 WRoadNav::SetControlPos
void WRoadNav_SetControlPos(WRoadNav& n, const WRoadSegment* s, float pos[3], bool at_start) {
    if ((s->flags & kSegCurved) == 0) return;
    // O nó e o ponto de controle da ponta: andando do nó 0 ao 1, o começo é o nó 0 e o controle +0x00.
    const int end = n.forward == 0 ? (at_start ? 1 : 0) : (at_start ? 0 : 1);
    const float* ctrl = end == 0 ? s->start_control : s->end_control;
    float node[3];
    copy3(node, road_node(s->node[end])->position);
    copy3(pos, ctrl);
    float d[3];
    vsub3(d, n.bound_start, n.bound_end);
    const float min_len = ps2::ffloat(0x3c23d70au);  // 0,01
    const float curve = ee::max(min_len, vu_len(d));
    const float scale = ee::max(min_len, ee::div(s->length, curve));
    vsub3(d, node, pos);
    const float reach = ee::div(vu_len(d), scale);
    float toward[3];
    vsub3(toward, pos, node);
    vu_unit(toward, toward);
    float bound[3];
    copy3(bound, at_start ? n.bound_start : n.bound_end);
    vscale3(toward, toward, reach);
    vadd3(pos, bound, toward);
}

// [driving] 0x00226a28 WRoadNav::SetStartEndPos
void WRoadNav_SetStartEndPos(WRoadNav& n, const WRoadSegment* s, float start_offset, float end_offset) {
    WRoadNav_SetBoundPos(n, s, start_offset, n.bound_start, true);
    WRoadNav_SetBoundPos(n, s, end_offset, n.bound_end, false);
}

// [driving] 0x00226788 WRoadNav::SetStartEndControls
void WRoadNav_SetStartEndControls(WRoadNav& n, const WRoadSegment* s) {
    WRoadNav_SetControlPos(n, s, n.start_control, true);
    WRoadNav_SetControlPos(n, s, n.end_control, false);
}

// [driving] 0x00224e00 WRoadNav::IncNavPosition
void WRoadNav_IncNavPosition(WRoadNav& n, float step, float heading[3], int16_t target) {
    const WRoadSegment* s = road_segment(n.segment);
    float d[3];
    vsub3(d, n.bound_start, n.bound_end);
    const float length = vu_len(d);
    const float fraction = ee::div(step, length);
    // Comprimento <= 0: o PS2 formata uma linha de depuração que ninguém usa (port: não formatada).
    const float reached = ee::add(n.t, fraction);
    if (ee::lt(reached, 1.0f)) {
        n.t = reached;
        n.t = ee::min(ee::max(reached, 0.0f), 1.0f);
        if (WRoadNav_UpdateLaneChange(n, step)) {
            WRoadNav_SetStartEndPos(n, s, n.lane_offset, n.lane_offset);
            WRoadNav_SetStartEndControls(n, s);
            if (s->flags & kSegCurved) build_spline(n);
        }
        place_on_curve(n, s);
        return;
    }
    const int16_t current = n.segment;
    WRoadNav_UpdateLaneChange(n, ee::mul(ee::sub(1.0f, n.t), length));
    float new_offset = n.lane_offset;
    int32_t changed = 0;
    int16_t next = current;
    switch (n.mode) {
    case 1: next = WRoadNav_CalcNextSegmentLane(n, &new_offset, &n.forward, &changed); break;
    case 4: next = WRoadNav_CalcNextSegmentRandom(n, heading, &n.forward); break;  // os agentes da IA
    case 3: case 5: next = WRoadNav_CalcNextSegmentDirection(n, heading, &target, &n.forward); break;
    case 2:
        // CalcNextSegmentSidewalk: sem porte (os pedestres não existem no port).
        ++g_stats.pending_modes;
        break;
    default: break;
    }
    if (n.segment == next) n.at_dead_end = 1;
    n.segment = next;
    const WRoadSegment* ns = road_segment(next);
    WRoadNetwork_GetSegmentForwardVector(ns, n.direction);
    if (n.forward == 0) {
        const float neg[3] = {ee::neg(n.direction[0]), ee::neg(n.direction[1]), ee::neg(n.direction[2])};
        copy3(n.direction, neg);
    }
    if (changed != 0) {
        copy3(n.bound_start, n.bound_end);
        WRoadNav_SetBoundPos(n, ns, new_offset, n.bound_end, false);
        set_offset(n, new_offset);
    } else {
        WRoadNav_SetStartEndPos(n, ns, n.lane_offset, n.lane_offset);
    }
    WRoadNav_SetStartEndControls(n, ns);
    if (ns->flags & kSegCurved) build_spline(n);
    copy3(n.road_point, n.bound_start);
    copy3(n.position, n.road_point);
    const float t = n.t;
    n.t = 0;
    const float rest = ee::mul(ee::sub(fraction, ee::sub(1.0f, t)), length);
    // Port: a recursão limitada (ver o cabeçalho; o PS2 estouraria a pilha num laço de trechos de comprimento zero).
    static int depth = 0;
    if (depth >= 256) {
        ++g_stats.guards;
        return;
    }
    ++depth;
    WRoadNav_IncNavPosition(n, rest, heading, target);
    --depth;
}

// [driving] 0x00223f98 WRoadNav::CalcNextSegmentLane
int16_t WRoadNav_CalcNextSegmentLane(WRoadNav& n, float* offset, int8_t* ahead, int32_t* changed) {
    RoadNetworkGlobals& r = road_network();
    const int32_t current = n.segment;
    const WRoadSegment* road = road_segment(current);
    int8_t new_lane = n.lane;
    int32_t next = current;
    int32_t one_blocked = 0;
    const WRoadNode* node = road_node(road->node[int(n.forward)]);
    const WRoadSegment* attached = GetAttachedDirectionalSegment(node, int16_t(next));
    if (node->segment_count == 3) {
        int blocked = 0;
        for (int i = 0; i < node->segment_count; ++i) {
            const WRoadSegment* c = road_segment(node->segments[i]);
            if (c->index == n.segment) continue;
            const WRoadSegment* b = GetAttachedDirectionalSegment(WRoadNetwork_GetSegmentOppNode(c, node), -1);
            if (b && (b->flags & 2)) ++blocked;
        }
        one_blocked = blocked == int(node->segment_count) - 2 ? 1 : 0;
    }
    auto starts_at = [&](int32_t seg) { return node == road_node(road_segment(seg)->node[0]) ? 1 : 0; };
    // Um sorteio do SimRandom da Simulation: o número / 65536 (exato em float) comparado em double com 0,3.
    auto keep = [&]() {
        const float f = ee::mul(ee::cvt_s(int32_t(SimRandom_Generate(sim_random()))), ps2::ffloat(0x37800000u));
        return double(f) < 0.3;
    };
    if (node->segment_count < 2) {
        next = road->index;
        *ahead = int8_t(*ahead == 0 ? 1 : 0);
    } else if (attached) {
        next = attached->index;
        *ahead = int8_t(node == road_node(attached->node[0]) ? 1 : 0);
    } else {
        const uint32_t rss = uint32_t(r.right_side_system);
        for (int i = 0; i < node->segment_count; ++i) {
            const uint16_t cand_index = node->segments[i];
            if (int32_t(cand_index) == n.segment) continue;
            const WRoadSegment* cand = road_segment(cand_index);
            if (cand->flags & 2) continue;
            WRoadNode* ends[2];
            WRoadNetwork_GetSegmentNodes(cand, ends);
            const WRoadNode* far_end = node != ends[0] ? ends[0] : ends[1];
            const WRoadSegment* beyond = GetAttachedDirectionalSegment(far_end, -1);
            if (!beyond) {
                ++g_stats.guards;  // o PS2 leria o +0x50 do endereço 0
                continue;
            }
            if (beyond->flags & 2) continue;
            int32_t side = rss != 0 ? 1 : 0;
            if (far_end != road_node(beyond->node[0])) side = side == 0 ? 1 : 0;
            if (rss != 0 && (beyond->flags & kSegHalfLanes)) side = side == 0 ? 1 : 0;
            bool no_lane = true;
            const int lanes = int(beyond->left_lanes) + int(beyond->right_lanes);
            for (int k = 0; k < lanes; ++k)
                if ((uint32_t(side) ^ (uint32_t(beyond->unknown58) & (1u << k))) == 0) no_lane = false;
            if (no_lane) continue;
            const int count = node->segment_count;
            bool take = false;  // ir para o TAKE (0x002245d0) com a faixa em new_lane
            if ((count == 4 && i == 2) || count == 2) {
                next = int16_t(cand_index);
                new_lane = n.lane;
                *ahead = int8_t(starts_at(next));
                *changed = 1;
                if (keep()) break;
            }
            const uint64_t road_flags = road->flags;  // ld +0x50: os 16 bits de baixo
            const uint32_t f1020 = uint32_t(road_flags & 0x1020);
            const bool there1000 = (beyond->flags & kSegHalfLanes) != 0;
            const bool last = (count == 3 && i == 2) || (count == 4 && i == 3);
            const int8_t lane = n.lane;
            if (rss == 0) {
                if (i == 1) {
                    if (one_blocked) {
                        new_lane = lane;
                    } else if (f1020 == 0x20 && !there1000) {
                        new_lane = lane;
                    } else if (f1020 == 0x1020 && !there1000) {
                        new_lane = -1;
                        if (lane != 0 && lane == -road->left_lanes) new_lane = int8_t(-beyond->left_lanes);
                    } else if (f1020 == 0x20 && there1000) {
                        new_lane = 1;
                        if (lane != 0 && lane == -road->left_lanes) new_lane = int8_t(-beyond->left_lanes);
                    } else {
                        if (lane != 0 && lane != -road->left_lanes) continue;
                        new_lane = int8_t(-beyond->left_lanes);
                    }
                    take = true;
                } else if (last) {
                    if (one_blocked) {
                        new_lane = lane;
                    } else if (f1020 == 0x20) {
                        if (there1000) continue;
                        new_lane = lane;
                    } else {
                        if (lane != -1) continue;
                        new_lane = -1;
                    }
                    take = true;
                } else {
                    continue;
                }
            } else {
                if (last) {
                    if (one_blocked) {
                        new_lane = lane;
                    } else if (f1020 == 0x20 && !there1000) {
                        new_lane = lane;
                    } else if (f1020 == 0x1020 && !there1000) {
                        new_lane = 1;
                        if (lane != 0 && lane == road->right_lanes) new_lane = beyond->right_lanes;
                    } else if (f1020 == 0x20 && there1000) {
                        new_lane = -1;
                        if (lane != 0 && lane == road->right_lanes) new_lane = beyond->right_lanes;
                    } else {
                        if (lane != 0 && lane != road->right_lanes) continue;
                        new_lane = beyond->right_lanes;
                    }
                    take = true;
                } else if (i == 1) {
                    if (one_blocked) {
                        new_lane = lane;
                    } else if (f1020 == 0x20) {
                        if (there1000) continue;
                        new_lane = lane;
                    } else {
                        if (lane != 1) continue;
                        new_lane = 1;
                    }
                    take = true;
                } else {
                    continue;
                }
            }
            if (take) {
                next = int16_t(cand_index);
                *ahead = int8_t(starts_at(next));
                *changed = 1;
                if (keep()) break;
            }
        }
    }
    n.lane2 = new_lane;
    n.lane = new_lane;
    const WRoadSegment* chosen = road_segment(next);
    const WRoadSegment* lanes_of = chosen;
    if (chosen->flags & kSegNonDirectional) {
        lanes_of = GetAttachedDirectionalSegment(WRoadNetwork_GetSegmentOppNode(chosen, node), -1);
        if (!lanes_of) {
            ++g_stats.guards;
            lanes_of = chosen;
        }
    }
    const float o = offsets_of(lanes_of)[new_lane < 0 ? -new_lane : new_lane];
    *offset = o;
    if (new_lane < 0) *offset = ee::neg(*offset);
    if (!ee::eq(*offset, n.lane_offset) && next != current) *changed = 1;
    return int16_t(next);
}

// [driving] 0x00227058 WRoadNav::UpdateLaneChange
bool WRoadNav_UpdateLaneChange(WRoadNav& n, float step) {
    const float to = n.lane_change_to;
    if (ee::eq(n.lane_offset, to)) return false;
    const float length = n.lane_change_length;
    if (!ee::lt(0.0f, length)) {
        n.lane_offset = to;
        return true;
    }
    const float dist = ee::add(n.lane_change_distance, step);
    const float frac = ee::div(dist, length);
    n.lane_change_distance = dist;
    n.lane_change_fraction = frac;
    if (ee::le(1.0f, frac)) {
        n.lane_change_from = to;
        n.lane_offset = to;
        return true;
    }
    const float from = n.lane_change_from;
    n.lane_offset = ee::add(from, ee::mul(ee::sub(to, from), frac));
    return true;
}

// [driving] 0x00226f18 WRoadNav::ChangeLanes
void WRoadNav_ChangeLanes(WRoadNav& n, float offset, float distance) {
    if (ee::lt(0.0f, distance)) {
        const float from = n.lane_change_to;
        n.lane_change_distance = 0;
        n.lane_change_from = from;
        n.lane_change_to = offset;
        n.lane_change_length = distance;
        n.lane_offset = from;
        n.lane_change_fraction = 0;
        return;
    }
    if (ee::eq(n.lane_offset, offset)) return;
    set_offset(n, offset);
    const WRoadSegment* s = road_segment(n.segment);
    WRoadNav_SetStartEndPos(n, s, n.lane_offset, n.lane_offset);
    WRoadNav_SetStartEndControls(n, s);
    if (s->flags & kSegCurved) build_spline(n);
    place_on_curve(n, s);
}

// [driving] 0x00225358 WRoadNav::ReverseNavDirection
void WRoadNav_ReverseNavDirection(WRoadNav& n) {
    const float neg[3] = {ee::neg(n.direction[0]), ee::neg(n.direction[1]), ee::neg(n.direction[2])};
    n.forward = int8_t(uint32_t(int32_t(n.forward)) < 1u ? 1 : 0);  // sltiu sobre o lb
    copy3(n.direction, neg);
    n.t = ee::abs(ee::sub(1.0f, n.t));
    float tmp[3];
    copy3(tmp, n.bound_start);
    copy3(n.bound_start, n.bound_end);
    copy3(n.bound_end, tmp);
    copy3(tmp, n.start_control);
    copy3(n.start_control, n.end_control);
    copy3(n.end_control, tmp);
    set_offset(n, ee::neg(n.lane_offset));
    const int8_t lane = int8_t(-int32_t(uint8_t(n.lane)));
    n.lane2 = lane;
    n.lane = lane;
}

// O caminho pela frente de um trecho candidato no CalcNextSegmentRandom/Direction: a frente do trecho (a de trás quando
// ele termina no nó), a direção pedida feita unitária no lugar (vmadday + vrsqrt + vmulq.xyz; o PS2 grava de volta no
// COORD3 do chamador) e o produto escalar.
namespace {
float heading_dot(float heading[3], const WRoadSegment* s, bool reverse) {
    float fwd[3];
    WRoadNetwork_GetSegmentForwardVector(s, fwd);
    if (reverse) {
        const float r[3] = {ee::neg(fwd[0]), ee::neg(fwd[1]), ee::neg(fwd[2])};
        copy3(fwd, r);
    }
    vu_unit(heading, heading);
    vu_unit(fwd, fwd);
    return vu_dot(fwd, heading);
}
}  // namespace

// [driving] 0x002235c8 WRoadNav::CalcNextSegmentRandom(COORD3 &, char &, bool &): o próximo trecho no nó da ponta: o
// escolhido pelo comando (+0xb0, só num cruzamento de 3 ou mais), o mesmo trecho de volta num beco, o mais alinhado com
// a direção pedida (com +0xb1/+0xb2, o seguir e o fugir do alvo) ou, sem eles, um ao acaso que não volte ao nó de onde
// veio (o primeiro num nó de 2; num de 3 ou mais, o SimRandom entre os que servem).
int16_t WRoadNav_CalcNextSegmentRandom(WRoadNav& n, float heading[3], int8_t* ahead) {
    const WRoadSegment* road = road_segment(n.segment);
    const WRoadNode* node = road_node(road->node[int(n.forward)]);
    int32_t result = n.segment;
    const int count = node->segment_count;
    if (n.next_choice >= 0 && count >= 3) {
        result = n.next_choice < count ? node->segments[n.next_choice] : node->segments[0];
        n.next_choice = -1;
        return int16_t(result);
    }
    if (count < 2) {
        *ahead = int8_t(*ahead == 0 ? 1 : 0);
        return road->index;
    }
    if (n.unknownb1 != 0 || n.unknownb2 != 0) {
        float best = -2.0f;
        for (int i = 0; i < count; ++i) {
            const uint16_t cand = node->segments[i];
            if (int32_t(cand) == n.segment) continue;
            const WRoadSegment* cs = road_segment(cand);
            const bool ends_here = uint32_t(cs->node[1]) == uint32_t(int32_t(node->index));
            const float d = heading_dot(heading, cs, ends_here);
            if (ee::lt(best, d)) {
                result = cand;
                best = d;
                *ahead = int8_t(node == road_node(road_segment(result)->node[0]) ? 1 : 0);
            }
        }
        return int16_t(result);
    }
    const int side = *ahead;
    if (count < 3) {
        for (int i = 0; i < count; ++i) {
            const uint16_t cand = node->segments[i];
            if (int32_t(cand) == n.segment) continue;
            if (road->node[side] != road_segment(cand)->node[side]) return int16_t(cand);
        }
        return int16_t(result);
    }
    int valid = count;
    for (int i = 0; i < count; ++i) {
        const uint16_t cand = node->segments[i];
        if (int32_t(cand) == n.segment) --valid;
        else if (road->node[side] == road_segment(cand)->node[side]) --valid;
    }
    const int32_t pick = int32_t(uint32_t(valid) * SimRandom_Generate(sim_random())) >> 16;
    int k = 0;
    for (int i = 0; i < count; ++i) {
        const uint16_t cand = node->segments[i];
        if (int32_t(cand) == n.segment) continue;
        if (road->node[side] == road_segment(cand)->node[side]) continue;
        result = cand;  // (o delay slot do beq: fica o último que serviu)
        if (k == pick) return int16_t(result);
        ++k;
    }
    return int16_t(result);
}

// [driving] 0x00223a30 WRoadNav::CalcNextSegmentDirection(COORD3 &, short &, char &, bool &): o próximo trecho rumo ao
// trecho alvo: o escolhido pelo comando (+0xb0), o de volta num beco, o trecho com direção ligado ao nó; com um alvo, o
// cruzamento da ponta: um vizinho dele (salvo o de onde veio) que toca o alvo, por um trecho do cruzamento que chega a
// ele, dá o trecho do nó cuja ponta de longe o tem; senão o primeiro trecho com direção do nó, ou, entre os sem
// direção, o mais alinhado (pelo trecho com direção da ponta de longe) com a direção pedida.
int16_t WRoadNav_CalcNextSegmentDirection(WRoadNav& n, float heading[3], const int16_t* target, int8_t* ahead) {
    const WRoadSegment* road = road_segment(n.segment);
    const WRoadNode* node = road_node(road->node[int(n.forward)]);
    int32_t result = n.segment;
    const WRoadSegment* attached = GetAttachedDirectionalSegment(node, int16_t(result));
    const int count = node->segment_count;
    auto starts_here = [&](int32_t seg) { return int8_t(node == road_node(road_segment(seg)->node[0]) ? 1 : 0); };
    if (n.next_choice >= 0 && count >= 3) {
        // (o PS2 não confere a escolha com a contagem)
        result = int16_t(node->segments[n.next_choice]);
        n.next_choice = -1;
        return int16_t(result);
    }
    if (count < 2) {
        *ahead = int8_t(*ahead == 0 ? 1 : 0);
        return road->index;
    }
    if (attached) {
        *ahead = int8_t(node == road_node(attached->node[0]) ? 1 : 0);
        return attached->index;
    }
    if (*target >= 0) {
        const WRoadSegment* tseg = road_segment(*target);
        const WRoadSegment* cur = road_segment(n.segment);
        const int16_t at = cur->intersection[int(n.forward)];  // o cruzamento da ponta (+0x1c + forward x 2)
        if (at >= 0) {
            const WRoadIntersection* in = road_intersection(at);
            const int16_t back = cur->intersection[n.forward != 0 ? 0 : 1];  // o da outra ponta
            for (int j = 0; j < in->neighbour_count; ++j) {
                const int16_t nb = int16_t(in->neighbours[j]);
                if (nb == back) continue;
                if (tseg->intersection[0] != nb && tseg->intersection[1] != nb) continue;
                for (int k = 0; k < in->segment_count; ++k) {
                    const WRoadSegment* s2 = road_segment(in->segments[k]);
                    if (s2->intersection[0] != nb && s2->intersection[1] != nb) continue;
                    for (int i = 0; i < count; ++i) {
                        const WRoadSegment* cand = road_segment(node->segments[i]);
                        const WRoadNode* opp = WRoadNetwork_GetSegmentOppNode(cand, node);
                        for (int m = 0; m < opp->segment_count; ++m)
                            if (int16_t(opp->segments[m]) == s2->index) result = cand->index;
                    }
                    *ahead = starts_here(result);
                    return int16_t(result);
                }
            }
        }
    }
    float best = -2.0f;
    for (int i = 0; i < count; ++i) {
        const uint16_t cand = node->segments[i];
        if (int32_t(cand) == n.segment) continue;
        const WRoadSegment* cs = road_segment(cand);
        if ((cs->flags & 1) == 0) {
            result = int16_t(cand);
            *ahead = starts_here(result);
            return int16_t(result);
        }
        const WRoadNode* opp = WRoadNetwork_GetSegmentOppNode(cs, node);
        const WRoadSegment* att = GetAttachedDirectionalSegment(opp, -1);
        if (!att) {
            ++g_stats.guards;  // o PS2 leria o nó do trecho nulo (o endereço 0xc)
            continue;
        }
        const float d = heading_dot(heading, att, opp != road_node(att->node[0]));
        if (ee::lt(best, d)) {
            result = int16_t(cand);
            best = d;
            *ahead = starts_here(result);
        }
    }
    return int16_t(result);
}

}  // namespace drv
