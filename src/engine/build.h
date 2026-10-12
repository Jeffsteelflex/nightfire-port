#pragma once
// Construção do mundo (módulo build do ACTION.ELF): células, portais e o próprio mundo.

#include "engine/map_types.h"

cel_tag* build_alloc_cel(cel_tag* next);
portal_tag* build_alloc_portal(const portal_tag* src, portal_tag* next, cel_tag* cel, uint8_t flag);
world_tag* build_alloc_world();
void build_MakeCelOverlapBox(world_tag* world);

bool Plane_PlaneEq(plane_equ_tag* plane, const _VECTOR* a, const _VECTOR* b, const _VECTOR* c);
// O ponto p levado ao plano pela normal: out = p - (n . p + d) n (n já normalizada, como a de Plane_PlaneEq).
void Plane_PutPntOnPlane(_VECTOR* out, const _VECTOR* p, const plane_equ_tag* plane);

// Leitura sequencial de dados binários (BIN_*): lê no ponteiro e o avança.
uint8_t BIN_GetByte(uint8_t** p);
uint16_t BIN_GetWord(uint8_t** p);
uint32_t BIN_GetDWord(uint8_t** p);
float BIN_GetFloat(uint8_t** p);
