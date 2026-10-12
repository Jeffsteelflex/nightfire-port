// Port do gerenciador de memória da EuroLand (módulo Mem do ACTION.ELF). Ver memory.h.

#include "engine/memory.h"

#include <cstdio>
#include <cstring>

#include "platform/psi.h"

void* PtrHeap = nullptr;
uintptr_t PtrHeapEnd = 0;
uint32_t HeapByteSize = 0;
uint32_t MallocSize = 0;
uint32_t MemStats[7];

namespace {

struct MemBlock {
    uint32_t prev;
    uint32_t size;
    uint16_t tag;
    uint8_t slack;
    uint8_t free;
};
static_assert(sizeof(MemBlock) == 12);

MEMINFO_tag MemInfo;
tMallocMethod MallocMethod = MALLOC_BEST;
uintptr_t QuickBlock = 0;

uintptr_t A(const void* p) { return reinterpret_cast<uintptr_t>(p); }
MemBlock* B(uintptr_t a) { return reinterpret_cast<MemBlock*>(a); }
uint32_t U32(uintptr_t a) { return static_cast<uint32_t>(a); }
uint8_t U8(uintptr_t a) { return static_cast<uint8_t>(a); }
// O endereço do bloco anterior no cabeçalho (+0, 32 bits): em x64 o deslocamento na janela (platform/ptr32.h).
uint32_t ADDR32(uintptr_t a) { return nf::p32_u(reinterpret_cast<void*>(a)); }
uintptr_t ADDR(uint32_t v) { return reinterpret_cast<uintptr_t>(nf::p32_ptr(v)); }

// Fim do último bloco: PtrHeap + HeapByteSize - 0xc.
uintptr_t heap_limit() { return A(PtrHeap) + HeapByteSize - 0xc; }

// Bloco com b < ptr < b + tamanho (busca usada por Mem_Shrink e Mem_ShrinkToEndOfBlock).
uintptr_t find_block(uintptr_t ptr) {
    for (uintptr_t b = A(PtrHeap); b < heap_limit(); b += B(b)->size) {
        if (ptr > b && ptr < b + B(b)->size) return b;
    }
    return 0;
}

}  // namespace

// [decomp] 0x001cf568 Mem_Free
void Mem_Free(void** ptr) {
    const uintptr_t p = A(*ptr);
    const uintptr_t limit = heap_limit();
    if (p >= A(PtrHeap) && p <= limit) {
        uintptr_t h = p - 0xc;
        B(h)->free = 1;
        B(h)->tag = 4;
        const uintptr_t prev = ADDR(B(h)->prev);
        bool merge_next = true;
        if (prev != h && B(prev)->free) {
            const uintptr_t next = h + B(h)->size;
            if (next < limit) {
                B(next)->prev = ADDR32(prev);
                B(prev)->size += B(h)->size;
                h = prev;
            } else {
                // Peculiaridade do original: o último bloco do heap não se junta ao anterior livre.
                merge_next = false;
            }
        }
        if (merge_next) {
            const uintptr_t next = h + B(h)->size;
            if (next < limit && B(next)->free) {
                B(h)->size += B(next)->size;
                const uintptr_t after = h + B(h)->size;
                if (after < limit) B(after)->prev = ADDR32(h);
            }
        }
        if (QuickBlock < h) QuickBlock = h;
        if (h < QuickBlock && QuickBlock < h + B(h)->size) QuickBlock = h;
    }
    *ptr = nullptr;
}

#if NF_PTR64
void Mem_Free(P32<void>* ptr) {
    void* p = *ptr;
    Mem_Free(&p);
    *ptr = nullptr;
}
#endif

// [decomp] 0x001cf688 Mem_Malloc
void* Mem_Malloc(uint32_t size, uint32_t tag, uint32_t align) {
    if (align == 0) align = tag & 0xff;
    const uint32_t mask = (align - 1) | 0xf;
    if (tag == 0x2980) size = (size + 0x7ff) & 0xfffff800u;
    const uint32_t needed = size + mask * 2 + 0x18;
    const uintptr_t limit = heap_limit();

    // Divergência: depois de uma falha no modo rápido o original deixa QuickBlock = 0 e a próxima
    // busca lê o "bloco" no endereço 0; aqui ela recomeça do início do heap.
    if (QuickBlock == 0) QuickBlock = A(PtrHeap);

    uintptr_t quick = QuickBlock;  // novo QuickBlock
    uintptr_t found = 0;
    if (MallocMethod == MALLOC_QUICK) {
        if (B(QuickBlock)->free && B(QuickBlock)->size >= needed) {
            found = QuickBlock;
        } else {
            // Para trás pelos anteriores, a partir do próprio QuickBlock.
            quick = 0;
            for (uintptr_t b = QuickBlock;;) {
                if (B(b)->free && B(b)->size >= needed) {
                    found = quick = b;
                    break;
                }
                const uintptr_t prev = ADDR(B(b)->prev);
                if (prev == b) break;
                b = prev;
            }
            // Depois para frente, avançando o próprio QuickBlock.
            if (found == 0 && QuickBlock < limit) {
                while (!(B(QuickBlock)->free && B(QuickBlock)->size >= needed)) {
                    QuickBlock += B(QuickBlock)->size;
                    if (QuickBlock >= limit) break;
                }
                if (QuickBlock < limit) found = quick = QuickBlock;
            }
        }
    } else if (MallocMethod == MALLOC_BOTTOM) {
        for (uintptr_t b = A(PtrHeap); b < limit; b += B(b)->size) {
            if (B(b)->free && B(b)->size >= needed) {
                found = b;
                break;
            }
        }
    } else if (MallocMethod == MALLOC_TOP) {
        for (uintptr_t b = A(PtrHeap); b < limit; b += B(b)->size) {
            if (B(b)->free && B(b)->size >= needed) found = b;
        }
    } else {
        // MALLOC_BEST e qualquer outro valor: o menor que serve; no empate fica o primeiro.
        uint32_t best = HeapByteSize;
        for (uintptr_t b = A(PtrHeap); b < limit; b += B(b)->size) {
            if (B(b)->free && B(b)->size >= needed && B(b)->size < best) {
                best = B(b)->size;
                found = b;
            }
        }
    }
    QuickBlock = quick;

    if (found == 0) {
        Mem_PrintAllInfo();
        return nullptr;
    }

    if (MallocMethod == MALLOC_BOTTOM) {
        // Aloca no começo do bloco; se o início dos dados não estiver alinhado, separa um bloco
        // livre na frente.
        const uintptr_t next = found + B(found)->size;
        if (((found + 0xc) & mask) != 0) {
            const uintptr_t aligned = (found + mask + 0x18) & ~uintptr_t(mask);
            const uintptr_t h = aligned - 0xc;
            B(h)->prev = ADDR32(found);
            B(found)->tag = 4;
            B(found)->size = U32(h - found);
            B(found)->free = 1;
            B(found)->slack = 0;
            found = h;
        }
        const uintptr_t tail = (found + size + 0xf) & ~uintptr_t(3);
        B(tail)->free = 1;
        B(tail)->slack = 0;
        B(tail)->size = U32(next - tail);
        B(tail)->tag = 4;
        B(tail)->prev = ADDR32(found);
        B(found)->free = 0;
        B(found)->slack = 0;
        B(found)->size = U32(tail - found);
        B(found)->tag = static_cast<uint16_t>(tag);
        if (next < limit) B(next)->prev = ADDR32(tail);
        return reinterpret_cast<void*>(found + 0xc);
    }

    // Os outros modos alocam no fim do bloco livre, que encolhe.
    const uintptr_t end = found + B(found)->size;
    const uintptr_t user = (end - size) & ~uintptr_t(mask);
    const uintptr_t h = user - 0xc;
    if (!(found + 0xc < h)) return nullptr;
    B(h)->free = 0;
    B(h)->size = U32(end - h);
    B(h)->tag = static_cast<uint16_t>(tag);
    B(h)->prev = ADDR32(found);
    B(h)->slack = static_cast<uint8_t>(U8(B(h)->size) - U8(size) - 0xc);
    B(found)->size = U32(h - found);
    if (end < limit) B(end)->prev = ADDR32(h);
    return reinterpret_cast<void*>(user);
}

// [decomp] 0x001cfa50 Mem_Shrink
void Mem_Shrink(void** ptr, uint32_t size) {
    if (size <= 0xc) return;
    const uintptr_t p = A(*ptr);
    const uintptr_t limit = heap_limit();
    const uintptr_t blk = find_block(p);
    // Divergência: sem bloco o original lê o cabeçalho no endereço 0; aqui só zera o ponteiro.
    if (blk == 0 || B(blk)->free) {
        *ptr = nullptr;
        return;
    }
    MemBlock* b = B(blk);
    const uintptr_t user = blk + 0xc;
    const uintptr_t next = blk + b->size;

    if (p <= user) {
        // Libera o começo: o resto vira um bloco em uso com a mesma etiqueta.
        if (size + b->slack + 0xc < b->size) {
            const uintptr_t t = (p + (size - 0xc)) & ~uintptr_t(3);
            B(t)->free = 0;
            B(t)->size = U32(next - t);
            B(t)->prev = ADDR32(blk);
            B(t)->slack = static_cast<uint8_t>(b->slack + 0xc + (U8(p) - U8(t)));
            B(t)->tag = b->tag;
            b->slack = 0;
            b->size = U32(t - blk);
            if (next < limit) B(next)->prev = ADDR32(t);
            void* front = reinterpret_cast<void*>(user);
            Mem_Free(&front);
            *ptr = nullptr;
            return;
        }
    } else if (next <= p + b->slack + size) {
        // Libera até o fim dos dados (exatamente; se passar, não faz nada).
        if (next == p + b->slack + size) {
            const uintptr_t h = (p + 3) & ~uintptr_t(3);
            B(h)->free = 0;
            B(h)->slack = 0;
            B(h)->size = U32(next - h);
            B(h)->prev = ADDR32(blk);
            b->slack = static_cast<uint8_t>(h - p);
            b->size = U32(h - blk);
            if (next < limit) B(next)->prev = ADDR32(h);
            void* tail = reinterpret_cast<void*>(h + 0xc);
            Mem_Free(&tail);
        }
    } else {
        // Libera um trecho do meio: o bloco vira três (em uso, livre, em uso).
        const uintptr_t h1 = (p + 3) & ~uintptr_t(3);
        const uintptr_t h2 = (p + size - 0xc) & ~uintptr_t(3);
        if (U32(h2 - h1) >= 0xc) {
            B(h1)->free = 0;
            B(h1)->slack = 0;
            B(h1)->size = U32(h2 - h1);
            B(h1)->prev = ADDR32(blk);
            B(h2)->free = 0;
            B(h2)->size = U32(next - h2);
            B(h2)->prev = ADDR32(h1);
            B(h2)->slack = static_cast<uint8_t>(b->slack + U8(p + size) - 0xc - U8(h2));
            B(h2)->tag = b->tag;
            b->slack = static_cast<uint8_t>(h1 - p);
            b->size = U32(h1 - blk);
            if (next < limit) B(next)->prev = ADDR32(h2);
            void* hole = reinterpret_cast<void*>(h1 + 0xc);
            Mem_Free(&hole);
        }
    }
    *ptr = nullptr;
}

// [decomp] 0x001cfcf0 Mem_Info
MEMINFO_tag* Mem_Info() {
    const uintptr_t limit = heap_limit();
    for (uint32_t type = 0; type < 0x50; ++type) {
        MemInfo.used_by_type[type] = 0;
        for (uintptr_t b = A(PtrHeap); b < limit; b += B(b)->size) {
            if (!B(b)->free && uint32_t(B(b)->tag >> 8) == type) MemInfo.used_by_type[type] += B(b)->size;
        }
    }
    MemInfo.malloc_size = MallocSize;
    MemInfo.free_bytes = HeapByteSize - MallocSize;
    for (uint32_t& w : MemInfo.platform) w = 0;
    psiMem_Info(&MemInfo);
    return &MemInfo;
}

// [decomp] 0x001cfde8 Mem_PrintAllInfo
// Na versão de produção os printf foram removidos e só sobraram os laços: uso por categoria e os
// tamanhos distintos dos blocos livres, do maior para o menor (até 500). O port imprime em stderr.
void Mem_PrintAllInfo() {
    const MEMINFO_tag* info = Mem_Info();
    std::fprintf(stderr, "Mem: heap de %u bytes, uso por categoria:", HeapByteSize);
    for (uint32_t type = 1; type < 0x50; ++type) {
        if (info->used_by_type[type]) std::fprintf(stderr, " %02x=%u", type, info->used_by_type[type]);
    }
    const uintptr_t limit = heap_limit();
    uint32_t largest = 0;
    for (uintptr_t b = A(PtrHeap); b < limit; b += B(b)->size) {
        if (B(b)->free && B(b)->size > largest) largest = B(b)->size;
    }
    std::fprintf(stderr, "\nMem: blocos livres:");
    uint32_t below = largest + 1;
    for (int i = 0; i < 500; ++i) {
        uint32_t size = 0;
        for (uintptr_t b = A(PtrHeap); b < limit; b += B(b)->size) {
            if (B(b)->free && B(b)->size < below && B(b)->size > size) size = B(b)->size;
        }
        below = size;
        if (size == 0) break;
        std::fprintf(stderr, " %u", size);
    }
    std::fprintf(stderr, "\n");
}

// [decomp] 0x001cff00 Mem_Init
void Mem_Init() {
    std::memset(MemStats, 0, sizeof(MemStats));
    Mem_SetMallocMethod(MALLOC_BEST);
    if (PtrHeap == nullptr) psiMem_Init(reinterpret_cast<uint8_t**>(&PtrHeap), &HeapByteSize);
    PtrHeapEnd = A(PtrHeap) + HeapByteSize;
    std::memset(PtrHeap, 0x98, HeapByteSize);
    // A folga do primeiro bloco não é escrita e fica com 0x98, como no original.
    MemBlock* first = B(A(PtrHeap));
    first->free = 1;
    first->tag = 4;
    QuickBlock = A(PtrHeap);
    first->size = HeapByteSize - 0xc;
    first->prev = ADDR32(A(PtrHeap));
    MallocSize = 0;
    psiAllocateLoadingBlobs();
}

// [decomp] 0x001cffa8 Mem_SetMallocMethod
tMallocMethod Mem_SetMallocMethod(tMallocMethod method) {
    const tMallocMethod old = MallocMethod;
    MallocMethod = method;
    return old;
}

// [decomp] 0x001cffb8 Mem_ShrinkToEndOfBlock
void Mem_ShrinkToEndOfBlock(void** ptr) {
    const uintptr_t blk = find_block(A(*ptr));
    // Divergência: sem bloco o original lê o cabeçalho no endereço 0; aqui não faz nada.
    if (blk == 0 || B(blk)->free) return;
    const uintptr_t p = A(*ptr);
    const uintptr_t h = (p + 3) & ~uintptr_t(3);
    const uintptr_t next = blk + B(blk)->size;
    B(h)->free = 0;
    B(h)->slack = 0;
    B(h)->size = U32(next - h);
    B(h)->prev = ADDR32(blk);
    B(blk)->slack = static_cast<uint8_t>(h - p);
    B(blk)->size = U32(h - blk);
    if (next < heap_limit()) B(next)->prev = ADDR32(h);
    void* tail = reinterpret_cast<void*>(h + 0xc);
    Mem_Free(&tail);
}
