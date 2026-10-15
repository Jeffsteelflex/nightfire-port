#pragma once
// O navegador da rede de estradas (WRoadNav do DRIVING.ELF, 0xc0 bytes, WRender.obj; docs/driving.md seção 18): um lugar
// num trecho (qual, em que sentido, quanto andado de 0 a 1) e o deslocamento de través (a faixa), com a curva que o lugar
// segue entre as pontas do trecho deslocadas pela faixa (reta, ou a Bézier do RCameraSpline do navegador nos trechos
// curvos). IncNavPosition o anda, escolhendo o próximo trecho em cada nó pelo modo: 1 mantendo a faixa
// (CalcNextSegmentLane: o tráfego), 2 pelas calçadas, 3 e 5 rumo a uma direção, 4 ao acaso.
//
// Portado do MIPS do PS2 (a verdade); o nightfire-cxbx (NightfireResearch, src/driving/world/RoadNav.cpp e RoadNav.h,
// a versão de Xbox, que usa double onde o PS2 usa float) deu os nomes dos campos e explicou o algoritmo.
//
// Os modos 3, 4 e 5 (CalcNextSegmentDirection e CalcNextSegmentRandom) são dos agentes da IA (seção 21: conferidos na
// parte 17 do nf_driving_check). O modo 2 (CalcNextSegmentSidewalk, os pedestres) fica sem porte e conta em
// road_nav_stats().pending_modes (o segmento não muda, como um modo desconhecido no PS2).
//
// Leiaute do PS2 (static_assert); o RCameraSpline (+0x90) é do port (CameraSpline, road_network.h), alocado com o
// navegador.

#include <cstdint>

#include "driving/ai/road_network.h"

namespace drv {

struct WRoadNav {
    int32_t mode;                 // +0x00 1 faixa, 2 calçada, 3/5 direção, 4 ao acaso
    uint8_t unknown04;            // +0x04 no modo 4, o +0x5a dos trechos que ele aceita
    uint8_t unknown05[0xb];
    float position[4];            // +0x10 onde está: na curva, no deslocamento da faixa
    float road_point[4];          // +0x20
    float direction[3];           // +0x30 a frente do trecho, negada andando do nó 1 ao 0
    int8_t forward;               // +0x3c 1: do nó 0 ao 1
    uint8_t unknown3d;
    int16_t segment;              // +0x3e -1: InitAtPoint não achou trecho
    float t;                      // +0x40 quanto andado (0 a 1 no sentido dele)
    uint8_t unknown44[0xc];
    float bound_start[3];         // +0x50 as pontas da curva
    uint32_t unknown5c;
    float bound_end[3];           // +0x60
    uint32_t unknown6c;
    float start_control[3];       // +0x70 e os pontos de controle (só nos trechos curvos)
    uint32_t unknown7c;
    float end_control[3];         // +0x80
    uint32_t unknown8c;
    uint32_t spline;              // +0x90 (p32 CameraSpline)
    uint8_t at_dead_end;          // +0x94 a última troca de trecho ficou no mesmo
    int8_t lane;                  // +0x95 negativa à esquerda
    int8_t lane2;                 // +0x96 gravada com a faixa
    uint8_t unknown97;
    float lane_offset;            // +0x98 de través, a partir do meio do trecho
    float lane_change_from;       // +0x9c
    float lane_change_to;         // +0xa0
    float lane_change_fraction;   // +0xa4
    float lane_change_distance;   // +0xa8
    float lane_change_length;     // +0xac 0: a troca é imediata
    int8_t next_choice;           // +0xb0
    uint8_t unknownb1;            // +0xb1
    uint8_t unknownb2;            // +0xb2
    uint8_t unknownb3[0xd];
};
static_assert(sizeof(WRoadNav) == 0xc0);
static_assert(offsetof(WRoadNav, segment) == 0x3e && offsetof(WRoadNav, bound_start) == 0x50 &&
              offsetof(WRoadNav, spline) == 0x90 && offsetof(WRoadNav, lane_offset) == 0x98 &&
              offsetof(WRoadNav, next_choice) == 0xb0);

CameraSpline* road_nav_spline(const WRoadNav& n);

// [driving] 0x00223338 WRoadNav::WRoadNav: o RCameraSpline (port: um CameraSpline alocado na arena) e Reset.
void WRoadNav_Construct(WRoadNav& n);
// [driving] 0x00223380 WRoadNav::~WRoadNav
void WRoadNav_Destruct(WRoadNav& n);
// [driving] 0x002233d0 WRoadNav::Reset
void WRoadNav_Reset(WRoadNav& n);
// [driving] 0x00225490 WRoadNav::FindClosestSegmentInd(COORD4 &, COORD3 &, float *): o trecho mais perto do ponto entre
// os da célula da grade dele (fgGrid), o ponto mais perto e o t; -1 se nenhum serve, 0 com a célula sem trechos.
int WRoadNav_FindClosestSegmentInd(WRoadNav& n, const float point[4], float closest[3], float* along);
// [driving] 0x00225c48 WRoadNav::InitAtPoint(COORD3 &, COORD3 &, bool)
void WRoadNav_InitAtPoint(WRoadNav& n, const float point[3], const float heading[3], bool no_lane);
// [driving] 0x002257c0 WRoadNav::InitAtSegment(short, char, float, bool)
void WRoadNav_InitAtSegment(WRoadNav& n, int16_t segment, int8_t lane, float t, bool forwards);
// [driving] 0x002267d8 WRoadNav::SetBoundPos(WRoadSegment &, float, COORD3 &, bool)
void WRoadNav_SetBoundPos(WRoadNav& n, const WRoadSegment* s, float offset, float pos[3], bool at_start);
// [driving] 0x00226538 WRoadNav::SetControlPos(WRoadSegment &, COORD3 &, bool)
void WRoadNav_SetControlPos(WRoadNav& n, const WRoadSegment* s, float pos[3], bool at_start);
// [driving] 0x00226a28 WRoadNav::SetStartEndPos(WRoadSegment &, float, float)
void WRoadNav_SetStartEndPos(WRoadNav& n, const WRoadSegment* s, float start_offset, float end_offset);
// [driving] 0x00226788 WRoadNav::SetStartEndControls(WRoadSegment &)
void WRoadNav_SetStartEndControls(WRoadNav& n, const WRoadSegment* s);
// [driving] 0x00224e00 WRoadNav::IncNavPosition(float, COORD3 &, short)
void WRoadNav_IncNavPosition(WRoadNav& n, float step, float heading[3], int16_t target);
// [driving] 0x00223f98 WRoadNav::CalcNextSegmentLane(float *, char *, int *)
int16_t WRoadNav_CalcNextSegmentLane(WRoadNav& n, float* offset, int8_t* ahead, int32_t* changed);
// [driving] 0x002235c8 WRoadNav::CalcNextSegmentRandom(COORD3 &, char &, bool &) (o modo 4: os agentes da IA ao acaso)
int16_t WRoadNav_CalcNextSegmentRandom(WRoadNav& n, float heading[3], int8_t* ahead);
// [driving] 0x00223a30 WRoadNav::CalcNextSegmentDirection(COORD3 &, short &, char &, bool &) (os modos 3 e 5: rumo ao
// trecho alvo)
int16_t WRoadNav_CalcNextSegmentDirection(WRoadNav& n, float heading[3], const int16_t* target, int8_t* ahead);
// [driving] 0x00227058 WRoadNav::UpdateLaneChange(float)
bool WRoadNav_UpdateLaneChange(WRoadNav& n, float step);
// [driving] 0x00226f18 WRoadNav::ChangeLanes(float, float)
void WRoadNav_ChangeLanes(WRoadNav& n, float offset, float distance);
// [driving] 0x00225358 WRoadNav::ReverseNavDirection
void WRoadNav_ReverseNavDirection(WRoadNav& n);

// O que o navegador pediu e o port não tem (o modo 2 do IncNavPosition, os pedestres), e as guardas (memória inválida no PS2: um
// trecho sem trecho com direção ligado ao nó de longe no CalcNextSegmentLane).
struct RoadNavStats {
    uint64_t pending_modes = 0;
    uint64_t guards = 0;
    // InitAtPoint num ponto cuja célula da grade não tem trechos: o PS2 usa como t o lixo da pilha (sp+0xd0, que o
    // FindClosestSegmentInd não escreve); o port usa 0 (a parte 17 do nf_driving_check deixa esses casos de fora).
    uint64_t stack_garbage = 0;
};
RoadNavStats& road_nav_stats();

}  // namespace drv
