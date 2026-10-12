#pragma once
// Ordenação genérica do jogo (QuickSort, 0x001d30a8) e os comparadores escolhidos pelo número.
// A ordem final de elementos iguais depende do algoritmo exato, então ele é o do original
// (quicksort sem recursão, pivô no meio, pilha explícita).

#include <cstdint>

enum COMP_FUNC : uint32_t {
    COMP_SPR = 0,          // byte com sinal em (*a)+0x38, decrescente
    COMP_LIGHT = 1,        // float em (*a)+0x38, crescente (também o padrão para 10 e acima)
    COMP_HITDATA = 2,      // float em (*a)+0xc (distância do acerto), crescente
    COMP_DRAWCELS = 3,     // u16 em a+4 (profundidade do portal), crescente
    COMP_ALPHAOBJ = 4,     // float em a+0 (chave da lista de desenho), crescente
    COMP_MSGTXT = 5,       // byte com sinal em (*a)+0x1d, crescente
    COMP_TARGETOBJ = 6,    // float em a+4, crescente
    COMP_MENUCTRL = 7,     // s16 em a+4, crescente
    COMP_STRINGS = 8,      // strcmp
    COMP_BOXLIST = 9,      // float em (*a)+4, crescente (nunca devolve 0)
};

void QuickSort(void* base, uint32_t count, uint32_t size, COMP_FUNC comp, void* unused);
void Swap(char* a, char* b, uint32_t size);
