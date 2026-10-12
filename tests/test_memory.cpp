// Testes do port do gerenciador de memória (src/engine/memory.cpp).

#include <cstdint>

#include "engine/memory.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

struct Hdr {
    uint32_t prev;
    uint32_t size;
    uint16_t tag;
    uint8_t slack;
    uint8_t free;
};

uintptr_t A(const void* p) { return reinterpret_cast<uintptr_t>(p); }
uint8_t* P(void* p) { return static_cast<uint8_t*>(p); }
Hdr* hdr_at(uintptr_t a) { return reinterpret_cast<Hdr*>(a); }
Hdr* hdr_of(void* p) { return hdr_at(A(p) - 12); }
uintptr_t heap_base() { return A(PtrHeap); }
uintptr_t heap_limit() { return heap_base() + HeapByteSize - 12; }

void fresh_heap() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
}

struct Walk {
    int blocks = 0;
    int free_blocks = 0;
    bool ok = true;
};

// Percorre a cadeia: blocos contíguos, "anterior" certo e fim exatamente no limite.
Walk walk() {
    Walk w;
    uintptr_t prev = heap_base();
    uintptr_t b = heap_base();
    while (b < heap_limit()) {
        const Hdr* h = hdr_at(b);
        if (h->prev != prev || h->size < 12) {
            w.ok = false;
            return w;
        }
        ++w.blocks;
        if (h->free) ++w.free_blocks;
        prev = b;
        b += h->size;
    }
    w.ok = w.ok && b == heap_limit();
    return w;
}

}  // namespace

TEST(mem_init_single_free_block) {
    fresh_heap();
    CHECK(PtrHeap != nullptr);
    CHECK(PtrHeapEnd == heap_base() + HeapByteSize);

    CHECK(heap_base() % 16 == 0);
    const Hdr* h = hdr_at(heap_base());
    CHECK(h->free == 1 && h->tag == 4 && h->size == HeapByteSize - 12 && h->prev == heap_base());
    CHECK(h->slack == 0x98);  // não é escrita por Mem_Init
    CHECK(P(PtrHeap)[12] == 0x98 && P(PtrHeap)[HeapByteSize - 1] == 0x98);
    const Walk w = walk();
    CHECK(w.ok && w.blocks == 1 && w.free_blocks == 1);
}

TEST(mem_best_fit_allocates_from_top_aligned) {
    fresh_heap();
    void* p = Mem_Malloc(100, 0x0110, 0);
    CHECK(p != nullptr);
    CHECK(A(p) % 16 == 0);
    CHECK(A(p) + 100 <= heap_limit() && A(p) + 100 + 16 > heap_limit());  // encostado no topo
    const Hdr* h = hdr_of(p);
    CHECK(h->free == 0 && h->tag == 0x0110 && h->prev == heap_base());
    CHECK(h->size == heap_limit() - (A(p) - 12));
    CHECK(h->slack == uint8_t(h->size - 100 - 12));
    void* q = Mem_Malloc(64, 0x0280, 0);  // alinhamento 128 vindo da etiqueta
    CHECK(A(q) % 128 == 0 && A(q) < A(p));
    void* r = Mem_Malloc(10, 0x0110, 0x400);  // alinhamento explícito
    CHECK(A(r) % 0x400 == 0 && A(r) < A(q));
    const Walk w = walk();
    CHECK(w.ok && w.blocks == 4 && w.free_blocks == 1);
}

TEST(mem_free_coalesces_and_last_block_quirk) {
    fresh_heap();
    void* a = Mem_Malloc(1000, 0x0110, 0);
    void* b = Mem_Malloc(1000, 0x0110, 0);
    void* c = Mem_Malloc(1000, 0x0110, 0);
    CHECK(A(c) < A(b) && A(b) < A(a));
    Mem_Free(&b);
    CHECK(b == nullptr);
    Walk w = walk();
    CHECK(w.ok && w.blocks == 4 && w.free_blocks == 2);  // b não encosta em nenhum livre
    Mem_Free(&c);
    w = walk();
    CHECK(w.ok && w.blocks == 2 && w.free_blocks == 1);  // c junta com o de baixo e com b
    // O último bloco do heap não se junta ao anterior livre: ficam dois blocos livres.
    Mem_Free(&a);
    w = walk();
    CHECK(w.ok && w.blocks == 2 && w.free_blocks == 2);
}

TEST(mem_bottom_method_allocates_at_start) {
    fresh_heap();
    CHECK(Mem_SetMallocMethod(MALLOC_BOTTOM) == MALLOC_BEST);
    void* p = Mem_Malloc(100, 0x0110, 0);
    // O início dos dados (base + 12) não está alinhado: um bloco livre de 0x14 bytes vai na frente.
    CHECK(A(p) == heap_base() + 0x20);
    CHECK(hdr_at(heap_base())->free == 1 && hdr_at(heap_base())->size == 0x14);
    CHECK(hdr_of(p)->free == 0 && hdr_of(p)->tag == 0x0110 && hdr_of(p)->prev == heap_base());
    // O resto livre começa em base + 0x84, e os dados do próximo (base + 0x90) já saem alinhados.
    void* q = Mem_Malloc(100, 0x0110, 0);
    CHECK(A(q) == heap_base() + 0x90);
    const Walk w = walk();
    CHECK(w.ok && w.blocks == 4 && w.free_blocks == 2);
    Mem_SetMallocMethod(MALLOC_BEST);
}

TEST(mem_best_top_and_quick_choose_different_holes) {
    // Dois buracos: um grande no topo (H1) e um menor no meio (H2).
    fresh_heap();
    void* x1 = Mem_Malloc(2000, 0x0110, 0);
    void* s1 = Mem_Malloc(16, 0x0110, 0);
    void* x2 = Mem_Malloc(500, 0x0110, 0);
    void* s2 = Mem_Malloc(16, 0x0110, 0);
    Mem_Free(&x1);
    Mem_Free(&x2);
    void* best = Mem_Malloc(400, 0x0110, 0);
    CHECK(A(best) > A(s2) && A(best) < A(s1));  // o menor que serve: H2
    Mem_SetMallocMethod(MALLOC_TOP);
    void* top = Mem_Malloc(400, 0x0110, 0);
    CHECK(A(top) > A(s1));  // o de endereço mais alto: H1
    Mem_SetMallocMethod(MALLOC_BEST);
    CHECK(walk().ok);

    // Rápido: Mem_Free deixa QuickBlock no bloco liberado mais alto, e a busca começa por ele.
    fresh_heap();
    Mem_SetMallocMethod(MALLOC_QUICK);
    void* a = Mem_Malloc(4000, 0x0110, 0);
    void* s = Mem_Malloc(16, 0x0110, 0);
    void* b = Mem_Malloc(1000, 0x0110, 0);
    Mem_Malloc(16, 0x0110, 0);
    Mem_Free(&b);
    Mem_Free(&a);
    void* c = Mem_Malloc(500, 0x0110, 0);
    CHECK(A(c) > A(s));  // no buraco grande do topo, não no menor
    Mem_SetMallocMethod(MALLOC_BEST);
    CHECK(walk().ok);
}

TEST(mem_failure_and_sector_rounding) {
    fresh_heap();
    CHECK(Mem_Malloc(HeapByteSize, 0x0110, 0) == nullptr);
    CHECK(walk().ok && walk().blocks == 1);
    // Etiqueta 0x2980: tamanho arredondado para setores de 0x800, alinhamento 0x80.
    void* p = Mem_Malloc(100, 0x2980, 0);
    CHECK(A(p) % 0x80 == 0 && heap_limit() - A(p) >= 0x800 && heap_limit() - A(p) < 0x880);
}

TEST(mem_free_ignores_null_and_foreign_pointers) {
    fresh_heap();
    void* n = nullptr;
    Mem_Free(&n);
    int local = 0;
    void* f = &local;
    Mem_Free(&f);
    CHECK(f == nullptr);
    CHECK(walk().ok && walk().blocks == 1);
}

TEST(mem_shrink_releases_tail_like_check_size_and_shrink) {
    fresh_heap();
    void* p = Mem_Malloc(4096, 0x0110, 0);
    void* e = P(p) + 1000;
    Mem_Shrink(&e, 4096 - 1000);
    CHECK(e == nullptr);
    CHECK(hdr_of(p)->size == 1012 && hdr_of(p)->slack == 0);
    Walk w = walk();
    CHECK(w.ok && w.blocks == 3 && w.free_blocks == 2);
    // Se o tamanho passar do fim dos dados, não faz nada.
    void* q = Mem_Malloc(256, 0x0110, 0);
    void* qe = P(q) + 100;
    const uint32_t before = hdr_of(q)->size;
    Mem_Shrink(&qe, 1000);
    CHECK(hdr_of(q)->size == before);
    Mem_Free(&q);
    Mem_Free(&p);
    // q era o último bloco: não se junta ao anterior, então sobram dois blocos livres.
    w = walk();
    CHECK(w.ok && w.blocks == 2 && w.free_blocks == 2);
}

TEST(mem_shrink_releases_front_and_middle) {
    fresh_heap();
    void* p = Mem_Malloc(4096, 0x0130, 0);
    void* q = p;
    Mem_Shrink(&q, 1024);
    CHECK(q == nullptr);
    // O resto continua em uso, com a mesma etiqueta, e os dados começam em p + 1024.
    const Hdr* rest = hdr_at(A(p) + 1012);
    CHECK(rest->free == 0 && rest->tag == 0x0130 && rest->size == heap_limit() - (A(p) + 1012));
    Walk w = walk();
    CHECK(w.ok && w.blocks == 2 && w.free_blocks == 1);

    fresh_heap();
    p = Mem_Malloc(4096, 0x0130, 0);
    void* h = P(p) + 1000;
    Mem_Shrink(&h, 2000);
    CHECK(hdr_of(p)->size == 1012);
    CHECK(hdr_at(A(p) + 1000)->free == 1 && hdr_at(A(p) + 1000)->size == 1988);
    CHECK(hdr_at(A(p) + 2988)->free == 0 && hdr_at(A(p) + 2988)->tag == 0x0130);
    w = walk();
    CHECK(w.ok && w.blocks == 4 && w.free_blocks == 2);
}

TEST(mem_shrink_to_end_of_block) {
    fresh_heap();
    void* p = Mem_Malloc(4096, 0x0110, 0);
    void* e = P(p) + 100;
    Mem_ShrinkToEndOfBlock(&e);
    CHECK(e == P(p) + 100);  // não zera o ponteiro
    CHECK(hdr_of(p)->size == 112);
    const Walk w = walk();
    CHECK(w.ok && w.blocks == 3 && w.free_blocks == 2);
}

TEST(mem_info_by_category) {
    fresh_heap();
    void* a = Mem_Malloc(100, 0x0110, 0);
    void* b = Mem_Malloc(200, 0x0310, 0);
    const MEMINFO_tag* info = Mem_Info();
    CHECK(info->used_by_type[1] == hdr_of(a)->size);
    CHECK(info->used_by_type[3] == hdr_of(b)->size);
    CHECK(info->used_by_type[0] == 0);
    CHECK(info->free_bytes == HeapByteSize && info->malloc_size == 0);
}
