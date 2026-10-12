#pragma once
// Gerenciador de memória da EuroLand (módulo Mem do ACTION.ELF, 0x001cf568-0x001d009c).
//
// Um único heap contíguo (PtrHeap, HeapByteSize) dividido em blocos encadeados. Cada bloco começa
// com um cabeçalho de 12 bytes:
//   +0  u32 endereço do bloco anterior (o primeiro aponta para si mesmo)
//   +4  u32 tamanho do bloco, incluindo o cabeçalho
//   +8  u16 etiqueta: byte alto = categoria (Mem_Info soma por categoria), byte baixo = alinhamento
//       padrão; blocos livres recebem 4
//   +a  u8  folga entre o fim dos dados e o fim do bloco (só o byte baixo)
//   +b  u8  1 = livre
// Os últimos 12 bytes do heap ficam fora de qualquer bloco e servem de limite.
//
// O endereço do anterior é de 32 bits, como no PS2 (o port compila em 32 bits).

#include <cstdint>

#include "platform/ptr32.h"

// Estratégia de busca de bloco livre (nomes do port; o jogo só usa os números).
enum tMallocMethod : uint32_t {
    MALLOC_QUICK = 0,   // a partir do último bloco usado (QuickBlock), para trás e depois para frente
    MALLOC_BEST = 1,    // menor bloco que serve (padrão depois de Mem_Init)
    MALLOC_BOTTOM = 2,  // primeiro que serve a partir do início; aloca no começo do bloco
    MALLOC_TOP = 3,     // último que serve (endereço mais alto)
};

// Resultado de Mem_Info (676 bytes, MemInfo em 0x002c7538).
struct MEMINFO_tag {
    uint32_t unknown[80];       // não preenchido pelo Mem_Info
    uint32_t used_by_type[80];  // bytes em uso por categoria (etiqueta >> 8)
    uint32_t free_bytes;        // HeapByteSize - MallocSize
    uint32_t malloc_size;       // MallocSize
    uint32_t platform[7];       // zerados; psiMem_Info poderia preenchê-los (no PS2 não faz nada)
};
static_assert(sizeof(MEMINFO_tag) == 676);

extern void* PtrHeap;
extern uintptr_t PtrHeapEnd;
extern uint32_t HeapByteSize;
extern uint32_t MallocSize;  // zerado em Mem_Init e nunca atualizado no módulo Mem
// 0x1c bytes, zerados em Mem_Init. [0] soma os bytes de colisão lidos (parsemap_block_Coll_Data_New).
extern uint32_t MemStats[7];

void Mem_Init();
tMallocMethod Mem_SetMallocMethod(tMallocMethod method);
void* Mem_Malloc(uint32_t size, uint32_t tag, uint32_t align);
void Mem_Free(void** ptr);
// Libera e zera um campo de ponteiro: Mem_Free(&obj->field). Em x86 é o Mem_Free(void**) de sempre; em x64 o campo
// de uma estrutura do jogo é um P32 (4 bytes) e uma variável do port um ponteiro nativo.
template <class T>
inline void Mem_Free(T** ptr) {
    Mem_Free(reinterpret_cast<void**>(ptr));
}
#if NF_PTR64
void Mem_Free(P32<void>* ptr);
template <class T>
inline void Mem_Free(P32<T>* ptr) {
    Mem_Free(reinterpret_cast<P32<void>*>(ptr));
}
#endif
// Libera os "size" bytes que começam em *ptr, dentro de um bloco em uso: o começo, o fim ou um
// trecho do meio (CheckSizeAndShrink devolve a sobra do fim de um buffer). Zera *ptr.
void Mem_Shrink(void** ptr, uint32_t size);
// Libera tudo do bloco a partir de *ptr (*ptr não é zerado).
void Mem_ShrinkToEndOfBlock(void** ptr);
MEMINFO_tag* Mem_Info();
void Mem_PrintAllInfo();
