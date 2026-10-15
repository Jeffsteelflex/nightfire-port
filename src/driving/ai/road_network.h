#pragma once
// A rede de estradas de uma pista (WRoadNetwork do DRIVING.ELF, WRender.obj; docs/driving.md seção 18): os nós, os
// trechos entre eles, os cruzamentos e as ruas que os trechos formam, como o grupo 'RNgp' do CARP da pista os guarda
// ('RNhd' com as contagens; um registro por índice: 'rn' os nós, 'rs' os trechos, 'ri' os cruzamentos, 'rj', 'rr' as
// ruas). Init monta tabelas de ponteiros para os registros, que ficam onde o CARP os tem; o tráfego (AIRoadSpawn,
// AIGroundVehicle), o navegador (WRoadNav, road_nav.h) e a câmera leem os registros direto.
//
// Portado do MIPS do PS2 (a verdade); o nightfire-cxbx (NightfireResearch, src/driving/world/RoadNetwork.cpp e
// RoadNetwork.h, a versão de Xbox) explicou os nomes dos campos. O PS2 tem mais sobrecargas que o Xbox (os "por índice"
// que só chamam o "por registro", GetPointAndVecOnSegment, GetPointOnSegmentDist...) e difere em detalhes (os pesos das
// faixas de um trecho com larguras, o carimbo de 8 bytes); onde difere vale o PS2.
//
// Os globais do EE são globais aqui também, com os endereços do PS2 nos comentários, para a conferência byte a byte (a
// parte 14 do nf_driving_check): as tabelas guardam palavras p32 (os registros moram no CARP da pista, na arena) e as
// linhas das tabelas de faixas guardam o endereço do .data do PS2 (lane_offsets/lane_weights abaixo traduzem).
//
// As contas seguem a FPU do EE e a VU0 em modo macro (platform/ps2_float.h); só o xyz dos vetores muda (o w das saídas
// fica como estava, como as instruções com campo .xyz).

#include <cstdint>
#include <string>
#include <vector>

#include "driving/carp_runtime.h"

namespace drv {

// As bandeiras de um trecho (+0x50; os nomes do nightfire-cxbx).
enum : uint16_t {
    kSegNonDirectional = 0x0001,  // sem direção: as larguras são as dos trechos com direção nas pontas
    kSegWidths = 0x0008,          // as larguras no lugar do primeiro ponto de controle (+0x00 esquerda, +0x04 direita)
    kSegLaneType3 = 0x0010,       // faixas do tipo 3, e 5 m a mais antes delas
    kSegLaneType2 = 0x0020,       // com kSegLanes: tipo 2
    kSegSpecial = 0x0100,
    kSegLeftMargin = 0x0200,      // 5 m a mais à esquerda
    kSegRightMargin = 0x0400,     // 5 m a mais à direita
    kSegLanes = 0x0800,           // faixas do tipo 1 (2 com kSegLaneType2) e 2,5 m a mais
    kSegHalfLanes = 0x1000,       // a largura conta metade das faixas dos dois lados
    kSegCurved = 0x4000,          // uma curva de Bézier pelos pontos de controle
    kSegWidths2 = 0x8000,         // como kSegWidths
};

// Um trecho ('rs', 0x64 bytes no registro; Init grava 8 bytes em +0x60, o carimbo, com sd).
struct WRoadSegment {
    float start_control[3];   // +0x00 a curva: o ponto de controle ao lado do nó do começo (ou as larguras)
    uint16_t node[2];         // +0x0c os nós do começo e do fim
    float end_control[3];     // +0x10
    int16_t intersection[2];  // +0x1c
    float start_across[3];    // +0x20 unitário, de través no começo
    float length;             // +0x2c
    float end_across[3];      // +0x30
    int16_t road;             // +0x3c a rua (-1 nenhuma)
    int16_t index;            // +0x3e
    float forward[3];         // +0x40 unitário, do começo ao fim
    float unknown4c;
    uint16_t flags;           // +0x50
    int8_t left_lanes;        // +0x52
    int8_t right_lanes;       // +0x53
    int16_t road_position[2]; // +0x54
    uint8_t unknown58, unknown59, unknown5a, unknown5b;
    uint32_t unknown5c;
    uint32_t stamp[2];        // +0x60 o carimbo da última busca (o PS2 grava 8 bytes)
};
static_assert(sizeof(WRoadSegment) == 0x68);

// Um nó ('rn', 0x20 bytes).
struct WRoadNode {
    float position[3];        // +0x00
    int16_t index;            // +0x0c
    uint8_t segment_count;    // +0x0e
    uint8_t unknown0f;
    uint16_t segments[8];     // +0x10
};
static_assert(sizeof(WRoadNode) == 0x20);

// Um cruzamento ('ri', 0x30 bytes).
struct WRoadIntersection {
    float position[3];
    int16_t index;
    uint8_t neighbour_count;
    uint8_t unknown0f;
    uint16_t neighbours[4];   // +0x10
    uint8_t segment_count;    // +0x18
    uint8_t unknown19;
    int16_t segments[11];     // +0x1a
};
static_assert(sizeof(WRoadIntersection) == 0x30);

// Uma rua ('rr', 0x30 bytes).
struct WRoad {
    int32_t index;
    int16_t intersection[2];
    float length;
    char name[0x20];
    uint8_t unknown2c, unknown2d;
    uint8_t count2e;          // +0x2e o tráfego nela (AIGroundVehicle::DoTrafficStateUpdate)
    uint8_t count2f;          // +0x2f
};
static_assert(sizeof(WRoad) == 0x30);

// Os globais do WRoadNetwork (endereços do PS2).
struct RoadNetworkGlobals {
    uint32_t instance = 0;            // 0x33a5f8 fgRoadNetwork (o objeto de 1 byte do Init)
    std::vector<uint32_t> nodes;      // 0x33a5fc fNodes (p32 de cada registro; a tabela no heap)
    std::vector<uint32_t> segments;   // 0x33a600 fSegments
    std::vector<uint32_t> intersections;  // 0x33a604
    std::vector<uint32_t> junctions;  // 0x33a608
    std::vector<uint32_t> roads;      // 0x33a60c
    bool tables = false;              // as tabelas foram alocadas (fNodes != 0)
    uint32_t segment_stamp[2] = {0, 0};  // 0x33a610 fSegmentStamp (8 bytes)
    int32_t valid = 0;                // 0x3c8254 fValid
    int32_t num_roads = 0;            // 0x3c8258
    int32_t valid_traffic_roads = 0;  // 0x3c825c
    int32_t right_side_system = 0;    // 0x3c827c
    int32_t num_segments = 0;         // 0x3c8280
    int32_t num_nodes = 0;            // 0x3c8360
    int32_t num_junctions = 0;        // 0x3c8364
    int32_t num_intersections = 0;    // 0x3c8368
    uint32_t lane_offsets[6] = {};    // 0x427000 as linhas (endereços do .data do PS2)
    uint32_t lane_weights[32] = {};   // 0x427018
};
RoadNetworkGlobals& road_network();
// Os registros pelos índices (p32 -> ponteiro).
WRoadSegment* road_segment(int index);
WRoadNode* road_node(int index);
WRoad* road_road(int index);
WRoadIntersection* road_intersection(int index);
// Uma linha das tabelas de faixas pelo endereço do .data do PS2 (nulo e conta em road_network_guards() fora delas).
const float* lane_offset_row(uint32_t ps2_address);
const int8_t* lane_weight_row(uint32_t ps2_address);
int road_network_guards();

// [driving] 0x00220c28 WRoadNetwork::Init: as tabelas do 'RNgp' de `root` (o grupo do WWorld + 0x14); `track` é o nome
// da pista (WWorld + 0xc: "snow2a_mis4" e "snow2a_race" usam as tabelas de faixas da neve).
void WRoadNetwork_Init(UGroup* root, const std::string& track);
// [driving] 0x002213d8 WRoadNetwork::Restart
void WRoadNetwork_Restart();
// [driving] 0x002212f8 WRoadNetwork::Shutdown
void WRoadNetwork_Shutdown();
// [driving] 0x00221528 WRoadNetwork::GetSpecialRoads(int *, int)
int WRoadNetwork_GetSpecialRoads(int32_t* out, int max);
// [driving] 0x002215f8 WRoadNetwork::GetSegmentNodes(WRoadSegment &, WRoadNode **)
void WRoadNetwork_GetSegmentNodes(const WRoadSegment* s, WRoadNode* out[2]);
// [driving] 0x002216b0 WRoadNetwork::GetSegmentForwardVector(WRoadSegment &, COORD3 &)
void WRoadNetwork_GetSegmentForwardVector(const WRoadSegment* s, float out[3]);
// [driving] 0x00221730 WRoadNetwork::GetSegmentEndVecs(WRoadSegment &, COORD3 &, COORD3 &): os vetores de través; num
// trecho reto, (frente.z, frente.y, -frente.x) nos dois.
void WRoadNetwork_GetSegmentEndVecs(const WRoadSegment* s, float start[3], float end[3]);
// [driving] 0x00221888 WRoadNetwork::GetSegmentEndPoints(WRoadSegment &, COORD3 &, COORD3 &): os dois nós.
void WRoadNetwork_GetSegmentEndPoints(const WRoadSegment* s, float start[3], float end[3]);
// [driving] 0x00221910 WRoadNetwork::GetSegmentEndPoints(WRoadSegment &, float, COORD3 &, COORD3 &): as pontas
// deslocadas de través por `offset` (só o xyz das saídas).
void WRoadNetwork_GetSegmentEndPointsOffset(const WRoadSegment* s, float offset, float start[4], float end[4]);
// [driving] 0x00221a10 WRoadNetwork::GetSegmentLength(WRoadSegment &): a distância entre os nós (VU0).
float WRoadNetwork_GetSegmentLength(const WRoadSegment* s);
// [driving] 0x00221ac8 / 0x00221cb0 WRoadNetwork::GetSegmentLeft/RightDrivableDist(WRoadSegment &)
float WRoadNetwork_GetSegmentLeftDrivableDist(const WRoadSegment* s);
float WRoadNetwork_GetSegmentRightDrivableDist(const WRoadSegment* s);
// [driving] 0x00221e98 WRoadNetwork::GetSegmentLaneType(WRoadSegment &)
int WRoadNetwork_GetSegmentLaneType(const WRoadSegment* s);
// [driving] 0x00221f20 WRoadNetwork::GetSegmentLaneIndex(WRoadSegment &, float *)
int WRoadNetwork_GetSegmentLaneIndex(const WRoadSegment* s, float* offset);
// [driving] 0x00222060 WRoadNetwork::GetSegmentLaneWeights(int, char, unsigned char *, int, int)
void WRoadNetwork_GetSegmentLaneWeights(int segment, int8_t end, uint8_t* weights, int size, int count);
// [driving] 0x00222300 WRoadNetwork::GetSegmentOppNode(WRoadSegment &, WRoadNode *)
WRoadNode* WRoadNetwork_GetSegmentOppNode(const WRoadSegment* s, const WRoadNode* node);
// [driving] 0x002223b0 WRoadNetwork::GetPointOnSegment(WRoadSegment &, float, COORD3 &): t limitado a 0..1.
void WRoadNetwork_GetPointOnSegmentT(const WRoadSegment* s, float t, float out[4]);
// [driving] 0x00222468 WRoadNetwork::GetPointOnSegment(COORD3 &, COORD3 &, WRoadSegment &, float, COORD3 &)
void WRoadNetwork_GetPointOnSegment(const float start[4], const float end[4], const WRoadSegment* s, float t,
                                    float out[4]);
// [driving] 0x002225d8 WRoadNetwork::GetPointAndVecOnSegment(COORD3 &, COORD3 &, WRoadSegment &, float, COORD3 &,
// COORD3 &): o ponto e a frente do trecho.
void WRoadNetwork_GetPointAndVecOnSegment(const float start[4], const float end[4], const WRoadSegment* s, float t,
                                          float point[4], float vec[3]);
// [driving] 0x00222700 WRoadNetwork::GetPointOnSegmentDist(WRoadSegment &, float, COORD3 &)
void WRoadNetwork_GetPointOnSegmentDist(const WRoadSegment* s, float dist, float out[4]);
// [driving] 0x00222810 WRoadNetwork::GetPointAndVecOnSegmentLane(WRoadSegment &, float, int, COORD3 &, COORD3 &)
void WRoadNetwork_GetPointAndVecOnSegmentLane(const WRoadSegment* s, float t, int lane, float point[4], float vec[3]);
// [driving] 0x00222968 WRoadNetwork::GetSegmentPointIntersect(WRoadSegment &, COORD3 &, COORD3 &, bool)
float WRoadNetwork_GetSegmentPointIntersect(const WRoadSegment* s, const float point[4], float out[4], bool clamp);
// [driving] 0x00222a30 WRoadNetwork::GetLinePointIntersect(COORD3 &, COORD3 &, COORD3 &, COORD3 &, bool): o ponto da
// reta mais perto de `point` (limitado às pontas com `clamp`) e o seu t.
float WRoadNetwork_GetLinePointIntersect(const float start[4], const float end[4], const float point[4], float out[4],
                                         bool clamp);
// [driving] 0x00222d40 WRoadNetwork::GetSegmentCurveStep(COORD3 &, COORD3 &, WRoadSegment &, float, COORD3 &): o ponto da
// Bézier (o RCameraSpline estático de 0x4191e0) em t.
void WRoadNetwork_GetSegmentCurveStep(const float start[4], const float end[4], const WRoadSegment* s, float t,
                                      float out[3]);
// [driving] 0x002270e8 GetAttachedDirectionalSegment(WRoadNode *, short): o primeiro trecho do nó, salvo `excluded`,
// sem kSegNonDirectional; nulo se nenhum.
WRoadSegment* GetAttachedDirectionalSegment(const WRoadNode* node, int16_t excluded);

// A Bézier do RCameraSpline (camera/RCameraSpline.obj) no que a rede de estradas e o navegador usam: os quatro pontos
// (o começo, os dois de controle, o fim; o w zerado), o tipo (+0x60: 0 Bézier, 1 a outra base com a metade) e a lista
// de pontos (só o tamanho importa aqui: BuildSplineEx só monta com menos de 2).
struct CameraSpline {
    float points[4][4];       // +0x00
    float unknown40[4];       // +0x40 (vf0 no construtor)
    float unknown50[4];       // +0x50
    int32_t type;             // +0x60 (2 no construtor)
    uint32_t list;            // +0x64 a lista (port: não usada)
};
// [driving] 0x001a8380 RCameraSpline::BuildSplineEx(COORD3 &, COORD3 &, COORD3 &, COORD3 &)
void RCameraSpline_BuildSplineEx(CameraSpline& s, const float p0[3], const float p1[3], const float p3[3],
                                 const float p2[3]);
// [driving] 0x001a85d0 RCameraSpline::EvaluateSpline(float, COORD4 &)
void RCameraSpline_EvaluateSpline(const CameraSpline& s, float t, float out[4]);
// [driving] 0x001a86a8 RCameraSpline::EvaluateTangent(float, COORD4 &): a derivada (a base de 0x001a7e00), unitária.
void RCameraSpline_EvaluateTangent(const CameraSpline& s, float t, float out[4]);
// O RCameraSpline estático do GetSegmentCurveStep (0x4191e0; a guarda em 0x33a958).
CameraSpline& curve_spline();
bool& curve_spline_built();

}  // namespace drv
