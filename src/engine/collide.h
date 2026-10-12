#pragma once
// Colisão contra a geometria e os objetos (módulo Collide e Intersect do ACTION.ELF).
//
// Um teste (HITTEST_tag) é uma esfera, um raio ou uma cápsula. Collide_Pick faz a fase larga: a
// partir da sala do teste, segue os portais que o volume atravessa (Collide_StraddleCels) e junta
// candidatos (as salas, as células dentro delas e os objetos, filtrados por Collide_PickObj) numa
// lista de HITDATA_tag. Collide_Intersect faz a fase fina: leva o teste para o espaço local da
// geometria do candidato (a célula ou a matriz do objeto) e testa a árvore de caixas e os triângulos
// de colisão (Intersect_PointGeom para esferas, Intersect_RayGeom para raios, Intersect_CylGeom para
// cápsulas), empurrando o volume para fora quando ele entra na geometria. Objetos articulados sem
// colisão própria (personagens) passam por Collide_Jointy: caixas das juntas ou cápsula contra
// cápsula.
//
// Formato da colisão (blocos 0x2e/0x2f, colldata_tag):
// - árvore de caixas (COLLBOX_tag, 0x30 bytes): nó interno com dois filhos ou folha com um
//   intervalo de triângulos e um bloco de vértices quantizados (s16 * scale + offset);
// - triângulos (8 bytes): três índices de vértice e o índice da normal, todos em s16 dentro do
//   bloco de vértices da folha;
// - normal: três s16 (x / 16383 na esfera e no 0x2000, x / 16384 no raio e na cápsula, como no
//   original) e o d do plano em float logo depois;
// - um byte de material por triângulo (bits 0-5 tipo de superfície, 0x40 e 0x80 flags).

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "platform/ptr32.h"

struct COLLBOX_tag {
    float min[3];        // 0x00
    int16_t child_a;     // 0x0c: < 0 é folha (0x8000 | blocos de vértices de 0x40 bytes)
    int16_t child_b;     // 0x0e: folha: primeiro triângulo
    float max[3];        // 0x10
    int16_t tri_end;     // 0x1c: folha: fim (exclusivo) dos triângulos
    uint16_t verts;      // 0x1e: folha: bloco de vértices em colldata_tag::head (x 0x40 bytes)
    float offset[3];     // 0x20
    float scale;         // 0x2c
};
static_assert(sizeof(COLLBOX_tag) == 0x30);

struct alignas(16) HITTEST_tag {
    plane_equ_tag plane;   // 0x00 plano do último triângulo visto (Intersect_RayGeom)
    _VECTOR push;          // 0x10 deslocamento acumulado para tirar o volume da geometria
    _VECTOR start;         // 0x20 raio: início
    _VECTOR end;           // 0x30 raio: fim
    _VECTOR dir;           // 0x40 end - start
    _VECTOR pos;           // 0x50 centro da esfera
    _VECTOR sphere;        // 0x60 centro e raio (montado por Collide_Pick)
    P32<obj_tag> tester;   // 0x70 objeto temporário: acertos em hits, o teste em hit_test
    P32<obj_tag> ignore_a;  // 0x74
    P32<obj_tag> ignore_b;  // 0x78
    P32<MATRIX> matrix;    // 0x7c na cópia local de Collide_Intersect: geometria local -> mundo
    P32<cel_tag> cel;      // 0x80 sala de partida
    uint32_t unknown_84;
    P32<celglist_tag> glist;  // 0x88 geometria em teste
    float radius;          // 0x8c (raio: max(1, |dir|) depois de Collide_Pick)
    uint32_t unknown_90;   // copiado para HITDATA_tag::unknown_08
    uint16_t type;         // 0x94: 0x100/0x101 esfera, 0x200/0x201 raio, 0x400, 0x800 cápsula,
                           //       0x2000 cápsula que só junta triângulos (TriHeap)
    uint16_t unknown_96;
    uint16_t mask;         // 0x98: o que pular (0x10 objetos, 0x40 salas, 0x80 células das salas...)
    uint16_t flags;        // 0x9a: 0x8 esfera dos dois lados, 0x10 filtra por material; 0x2000:
                           //       0x1 aceita triângulos paralelos ao segmento
    uint16_t found;        // 0x9c cápsula: bit 0 = apoiada (Intersect_CylGeom)
    uint16_t unknown_9e;
};
static_assert(sizeof(HITTEST_tag) == 0xa0);

struct alignas(16) HITDATA_tag {
    P32<HITDATA_tag> prev;  // 0x00
    P32<HITDATA_tag> next;  // 0x04
    uint32_t unknown_08;
    float dist;            // 0x0c: 1e8 = descartado
    plane_equ_tag plane;   // 0x10 normal e d do triângulo atingido
    uint32_t unknown_20[4];
    _VECTOR dir;           // 0x30 direção do raio, normalizada
    _VECTOR pos;           // 0x40 ponto de contato
    uint8_t surface;       // 0x50 material
    uint8_t flags;         // 0x51 (2 = não empurrar)
    uint16_t unknown_52;
    P32<cel_tag> cel;      // 0x54
    P32<obj_tag> obj;      // 0x58
    uint32_t unknown_5c;
};
static_assert(sizeof(HITDATA_tag) == 0x60);

// Caixa orientada de uma junta (AnimGetCollData, 0x60 bytes): eixos nas linhas 0-2 e centro na
// linha 3 da matriz, meias medidas em half.
struct alignas(16) COLLJOINT_tag {
    MATRIX m;              // 0x00
    _VECTOR half;          // 0x40
    uint16_t joint;        // 0x50 copiado para HITDATA_tag::unknown_52
    uint8_t pad[14];
};
static_assert(sizeof(COLLJOINT_tag) == 0x60);

constexpr float kNoHit = 1e8f;

extern LLISTINFO_tag HitHeap;
extern int32_t HitAllocCnt;
extern _VECTOR DeltaP;
extern uint8_t Ident;
// Triângulos (três _VECTOR no mundo cada) juntados pelos testes 0x2000 (Intersect_CylTriGeom); quem
// zera TriOnCnt é Collide_PlaceObject.
extern void* TriHeap;
extern int32_t TriAllocCnt;
extern uint32_t TriOnCnt;

void Coll_ResetHitHeap();
HITDATA_tag* Coll_AddHitToList(P32<HITDATA_tag>* list, float dist, cel_tag* cel, obj_tag* obj);
void Collide_FreeHitList(P32<HITDATA_tag>* list);

cel_tag** Collide_StraddleCels(HITTEST_tag* test, uint16_t* count);
uint32_t Intersect_Portal(HITTEST_tag* test, portal_tag* portal);
void Collide_Pick(HITTEST_tag* test);
void Collide_Update(obj_tag* first);
uint32_t Collide_Intersect(HITTEST_tag* test, HITDATA_tag* hit, uint16_t flags);
void Collide_PickObj(HITTEST_tag* test, obj_tag* obj, obj_tag* tester);
uint32_t Collide_Jointy(HITTEST_tag* test, HITDATA_tag* hit);
uint32_t Intersect_PointOOBox(MATRIX* m, const _VECTOR* half, const _VECTOR* p, float radius);
void Intersect_CylGeom(HITTEST_tag* test, HITDATA_tag* hit);
void Intersect_CylTriGeom(HITTEST_tag* test, HITDATA_tag* hit);
void ASH_RecurseBoxesBox(colldata_tag* coll, COLLBOX_tag* box, const _VECTOR* min, const _VECTOR* max);
void RecurseBoxesBox(colldata_tag* coll, COLLBOX_tag* box, const _VECTOR* min, const _VECTOR* max);
void Intersect_PointGeom(HITTEST_tag* test, HITDATA_tag* hit);
void RecurseBoxesSph(colldata_tag* coll, COLLBOX_tag* box, const _VECTOR* center, float radius);
void Intersect_RayGeom(HITTEST_tag* test, HITDATA_tag* hit);
uint32_t ASH_Intersect_RayBox(const HITTEST_tag* test, const float min[3], const float max[3], float* t);
void ASH_RecurseBoxesRay(colldata_tag* coll, COLLBOX_tag* box, const HITTEST_tag* test);
uint32_t Intersect_RayBox(const HITTEST_tag* test, const _VECTOR* min, const _VECTOR* max, float* t);
bool Collide_Filter(uint16_t surface, uint16_t hit_flags, uint32_t mask, float* dist);
// Tira da lista os acertos de projéteis cuja arma tem classe (weapon_definition_tag::stats) na máscara
// e reordena o resto pela distância (quebráveis e destrutíveis ignoram essas armas).
void Collide_FilterBullets(P32<HITDATA_tag>* list, uint16_t mask);
// Soma o dano (unknown_08) de todos os acertos da lista e junta em objs, até max, os objetos atingidos.
float Collide_GetDamageNObjects(HITDATA_tag* list, obj_tag** objs, uint16_t* count, uint16_t max);

bool Collide_SphereIntersect(_VECTOR* pos, float radius, const cel_tag* cel, const obj_tag* a, const obj_tag* b,
                             P32<HITDATA_tag>* hits, int8_t only_cel, int8_t one_sided, uint32_t mask, uint16_t flags);
bool Collide_RayIntersect(const _VECTOR* from, const _VECTOR* to, const cel_tag* cel, const obj_tag* a,
                          const obj_tag* b, P32<HITDATA_tag>* hits, int8_t only_cel, uint32_t mask, uint16_t flags);
// A cápsula do teste dado (start, end, radius) contra a geometria (os pêndulos dos corpos rígidos); o empurrão fica
// em test->push.
bool Collide_CylinderIntersect(HITTEST_tag* test, P32<HITDATA_tag>* hits, int8_t only_cel, uint16_t flags);
// Cabe um objeto de raio "radius" e meia largura "extent" no chão em pos? Ajusta pos e a matriz ao chão
// (GT_DeployMiniGun).
bool Collide_PlaceObject(_VECTOR* pos, MATRIX* m, float radius, float extent, obj_tag* obj);
bool Collide_LineOfSight(const _VECTOR* from, const _VECTOR* to, const cel_tag* cel, const obj_tag* a,
                         const obj_tag* b, uint32_t mask);

// Geometria vetorial usada pela colisão (vecutil_*).
float vecutil_calculate_closest_point_on_line(_VECTOR* out, const _VECTOR* a, const _VECTOR* b, const _VECTOR* p);
float vecutil_Dist2Tri(const _VECTOR* p, _VECTOR* a, _VECTOR* b, _VECTOR* c, const plane_equ_tag* plane,
                       uint16_t* code, _VECTOR* closest);
float vecutil_Dist2Tri_PointGeom(const _VECTOR* p, _VECTOR* a, _VECTOR* b, _VECTOR* c, const plane_equ_tag* plane,
                                 float dist, _VECTOR* closest);
float ASH_vecutil_Dist2Tri(const _VECTOR* p, const _VECTOR* a, const _VECTOR* b, const _VECTOR* c,
                           const plane_equ_tag* plane, uint16_t* code, _VECTOR* closest);
uint32_t vecutil_point_on_poly(const _VECTOR* p, const _VECTOR* a, const _VECTOR* b, const _VECTOR* c,
                               const plane_equ_tag* plane);
uint32_t Collide_RayTriangle(const _VECTOR* start, const _VECTOR* dir, const _VECTOR* a, const _VECTOR* b,
                             const _VECTOR* c, float* t);
// 0 sem interseção, 1 colineares (out não é escrito), 2 com out.x/out.z no ponto (planta x, z).
uint32_t vecutil_intersect_line_line_2d(const _VECTOR* p1, const _VECTOR* p2, const _VECTOR* p3, const _VECTOR* p4,
                                        _VECTOR* out);
uint32_t Intersect_SphereBox(const _VECTOR* c, float r, const _VECTOR* min, const _VECTOR* max);

// Só para testes: estado inicial das globais do módulo.
void collide_debug_power_on();

// Situações em que o original escreveria fora de uma tabela; o port pula e conta.
struct CollideGuards {
    uint32_t box_list_full = 0;   // mais de 600 folhas numa busca
    uint32_t hit_heap_empty = 0;  // mais de 1000 acertos alocados
    uint32_t tri_heap_alloc = 0;  // Mem_Malloc falhou ao aumentar o TriHeap
};
extern CollideGuards g_collide_guards;
