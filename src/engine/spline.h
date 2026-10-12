#pragma once
// Interpolação ao longo de um caminho (framelist_tag, bloco 0x19): KeyFrame_Interp (linear) e
// Spline_Interp (Catmull-Rom na posição, slerp na rotação). Usados pelas portas, elevadores e
// objetos que seguem trilhos.

#include <cstdint>

#include "engine/map_types.h"

// Como Spline_Interp trata a rotação: 0 sem rotação (identidade), 1 a 3 slerp entre os quadros,
// outros valores não escrevem nada. As portas usam 2.
using SPLINE_INTERP = int32_t;

// Posição e rotação no instante t (em quadros, de first a last; last < first percorre o caminho ao
// contrário). Escreve a parte 3x3 e a translação de m (a coluna w fica como estava) e, no slerp, o
// quatérnio em q (se não for nulo).
void Spline_Interp(framelist_tag* path, uint16_t first, uint16_t last, float t, MATRIX* m, quaternion_tag* q,
                   SPLINE_INTERP mode);
void KeyFrame_Interp(framelist_tag* path, uint16_t first, uint16_t last, float t, MATRIX* m);
// Catmull-Rom entre p1 e p2 (p0 e p3 são os vizinhos), t de 0 a 1.
void Spline_Eval3D(const _VECTOR* p0, const _VECTOR* p1, const _VECTOR* p2, const _VECTOR* p3, float t,
                   _VECTOR* out);
