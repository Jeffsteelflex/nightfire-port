#pragma once
// Tipos básicos da EuroLand como o ACTION.ELF os usa. Os nomes são os originais (dos símbolos com
// mangling do GCC 2.95), para que o código decompilado compile com o mínimo de mudanças.
//
// Layout verificado no código: _VECTOR e quaternion_tag têm 16 bytes alinhados a 16 (são carregados
// com lqc2 na VU0); MATRIX tem 4 linhas de 16 bytes, em vetor-linha (v' = v * M), com a translação
// na linha 3. Ver docs/engine.md.

#include <cstdint>

// O port compila em 32 bits: ponteiros de 4 bytes, como no PS2, mantêm o layout das estruturas.
static_assert(sizeof(void*) == 4, "compile em 32 bits (x86)");

struct alignas(16) _VECTOR {
    float x, y, z, w;
};

struct alignas(16) _VECTOR4 {
    float x, y, z, w;
};

// Atenção: a maior parte do jogo usa (x, y, z, w). Quat_QuaternionMultiply e
// Quat_QuaternionToMatrix tratam o componente 0 como escalar (w, x, y, z). Ver vecmath.cpp.
struct alignas(16) quaternion_tag {
    float x, y, z, w;
};

struct alignas(16) MATRIX {
    float m[4][4];  // m[linha][coluna]; linha 3 = translação
};

static_assert(sizeof(_VECTOR) == 16);
static_assert(sizeof(quaternion_tag) == 16);
static_assert(sizeof(MATRIX) == 64);

// Marca uma função portada do ACTION.ELF. O argumento é o endereço original; o nome vem logo depois.
// tools/decomp_status.py lê estes marcadores para medir o progresso. Formato no código:
//   // [decomp] 0x001e3000 Vec_Normalise
