// Os ponteiros de 32 bits das estruturas do jogo e a arena da versão de 64 bits (platform/ptr32.h, platform/arena.h).
// Em 32 bits (x86) P32<T> é T* e os testes conferem só as mesmas propriedades do ponteiro nativo.

#include <cstdint>
#include <cstring>
#include <vector>

#include "platform/arena.h"
#include "platform/ptr32.h"
#include "test_framework.h"

namespace {

struct Node {
    P32<Node> prev;
    P32<Node> next;
    int32_t value;
    uint32_t pad;
};
static_assert(sizeof(Node) == 16, "dois ponteiros de 4 bytes e dois inteiros");

struct Base {
    int32_t a;
};
struct Derived : Base {
    int32_t b;
};

int g_global_value = 7;
int add_one(int v) { return v + 1; }
int twice(int v) { return 2 * v; }

struct Table {
    F32<int(int)> fn;
    P32<int> data;
};
static_assert(sizeof(Table) == 8);

}  // namespace

TEST(ptr32_round_trip_and_null) {
    int locals[4] = {1, 2, 3, 4};
    P32<int> p = &locals[2];
    CHECK(p == &locals[2]);
    CHECK(*p == 3);
    int* raw = p;
    CHECK(raw == &locals[2]);
    P32<int> n = nullptr;
    CHECK(n == nullptr);
    CHECK(!n);
    CHECK(p != nullptr);
    CHECK(static_cast<int*>(n) == nullptr);
    // A palavra de 32 bits leva e traz o mesmo ponteiro.
    CHECK(nf::p32_ptr<int>(nf::p32_u(&locals[1])) == &locals[1]);
    CHECK(nf::p32_u(nullptr) == 0);
    CHECK(nf::p32_ptr<int>(0) == nullptr);
    // Globais e strings literais também cabem (estão na imagem).
    P32<int> g = &g_global_value;
    CHECK(*g == 7);
    P32<const char> s = "literal";
    CHECK(std::strcmp(s, "literal") == 0);
}

TEST(ptr32_arithmetic_and_comparisons) {
    int a[8] = {0, 10, 20, 30, 40, 50, 60, 70};
    P32<int> p = &a[1];
    P32<int> q = &a[5];
    CHECK(q - p == 4);
    CHECK(p < q && q > p && p <= p && q >= q);
    CHECK(*(p + 2) == 30);
    CHECK(p[3] == 40);
    ++p;
    CHECK(*p == 20);
    p += 3;
    CHECK(*p == 50);
    p -= 2;
    CHECK(*p == 30);
    P32<int> old = p++;
    CHECK(*old == 30 && *p == 40);
    old = p--;
    CHECK(*old == 40 && *p == 30);
    P32<uint8_t> bytes = reinterpret_cast<uint8_t*>(a);
    CHECK(reinterpret_cast<int*>(bytes + 8) == &a[2]);
    // void* e as conversões explícitas do código decompilado.
    P32<void> v = &a[6];
    CHECK(static_cast<int*>(v) == &a[6]);
    CHECK(*(int*)v == 60);
    // Derivada para base.
    Derived d{};
    d.a = 5;
    P32<Derived> pd = &d;
    P32<Base> pb = pd;
    CHECK(pb->a == 5);
}

TEST(ptr32_linked_structs_keep_the_ps2_layout) {
    Node nodes[3]{};
    for (int i = 0; i < 3; ++i) {
        nodes[i].value = i * 10;
        nodes[i].prev = i > 0 ? &nodes[i - 1] : nullptr;
        nodes[i].next = i < 2 ? &nodes[i + 1] : nullptr;
    }
    int sum = 0;
    for (Node* n = &nodes[0]; n != nullptr; n = n->next) sum += n->value;
    CHECK(sum == 30);
    CHECK(nodes[2].prev->prev == &nodes[0]);
    // Uma cópia dos 4 bytes (memcpy de estrutura) leva o mesmo endereço.
    Node copy;
    std::memcpy(&copy, &nodes[1], sizeof(Node));
    CHECK(copy.next == &nodes[2] && copy.prev == &nodes[0]);
    uint32_t word;
    std::memcpy(&word, &nodes[1].next, 4);
    CHECK(nf::p32_ptr<Node>(word) == &nodes[2]);
}

TEST(ptr32_function_fields) {
    Table t{};
    CHECK(t.fn == nullptr);
    t.fn = add_one;
    CHECK(t.fn(4) == 5);
    t.fn = &twice;
    CHECK(t.fn(4) == 8);
    int (*raw)(int) = t.fn;
    CHECK(raw == &twice);
    CHECK(nf::p32_fn<int(int)>(nf::p32_fn_u(&add_one)) == &add_one);
    // Dois campos iguais guardam a mesma palavra.
    Table u{};
    u.fn = twice;
    uint32_t a, b;
    std::memcpy(&a, &t.fn, 4);
    std::memcpy(&b, &u.fn, 4);
    CHECK(a == b && a != 0);
}

TEST(ptr32_heap_and_vectors_fit) {
    // O que o port aloca com new (std::vector...) também é endereçável pelos campos do jogo.
    std::vector<int> v(1000, 3);
    P32<int> p = v.data();
    CHECK(p[999] == 3);
    auto* big = new uint8_t[size_t(3) << 20];
    P32<uint8_t> pb = big + 100;
    CHECK(pb == big + 100);
    delete[] big;
}

#if NF_PTR64

TEST(arena_window_contains_image_heap_and_stack) {
    const nf::arena::Stats st = nf::arena::stats();
    CHECK(st.window_base == reinterpret_cast<uintptr_t>(nf::kP32Base));
    CHECK(st.begin > st.window_base && st.end > st.begin);
    CHECK(st.end - st.window_base <= 0x100000000ull);
    int local = 0;
    CHECK(nf::arena::in_window(&local));  // os testes rodam na pilha da arena
    CHECK(nf::arena::in_window(&g_global_value));
    CHECK(nf::arena::in_window(reinterpret_cast<const void*>(&add_one)));
    std::vector<int> v(10);
    CHECK(nf::arena::in_window(v.data()));
    CHECK(!nf::arena::in_window(nullptr));
    CHECK(!nf::arena::in_window(reinterpret_cast<void*>(st.window_base + 0x100000000ull)));
    // Offsets: o ponteiro nulo é 0, e nada na janela dá 0.
    CHECK(nf::p32_u(&g_global_value) != 0);
    CHECK(nf::p32_u(&local) == uint32_t(reinterpret_cast<uintptr_t>(&local) - st.window_base));
}

TEST(arena_allocator_classes_and_large_blocks) {
    // Blocos pequenos: alinhados a 16, distintos, reaproveitados depois de liberados.
    std::vector<void*> small;
    for (int i = 1; i <= 200; ++i) {
        void* p = nf::arena::alloc(size_t(i) * 7);
        CHECK((reinterpret_cast<uintptr_t>(p) & 15) == 0);
        CHECK(nf::arena::usable_size(p) >= size_t(i) * 7);
        std::memset(p, i & 0xff, size_t(i) * 7);
        small.push_back(p);
    }
    for (int i = 1; i <= 200; ++i) {
        const auto* b = static_cast<const uint8_t*>(small[size_t(i - 1)]);
        CHECK(b[0] == (i & 0xff) && b[size_t(i) * 7 - 1] == (i & 0xff));
    }
    void* first = small[0];
    for (void* p : small) nf::arena::free(p);
    void* again = nf::arena::alloc(7);
    CHECK(again == first || nf::arena::usable_size(again) == 16);
    nf::arena::free(again);
    // Blocos grandes: alinhados a 64 KB; páginas zeradas.
    const nf::arena::Stats before = nf::arena::stats();
    void* big = nf::arena::alloc(size_t(5) << 20);
    CHECK((reinterpret_cast<uintptr_t>(big) & 0xffff) == 0);
    CHECK(nf::arena::usable_size(big) >= (size_t(5) << 20));
    auto* pages = static_cast<uint8_t*>(nf::arena::alloc_pages(size_t(1) << 20));
    bool zero = true;
    for (size_t i = 0; i < (size_t(1) << 20); i += 4096) zero = zero && pages[i] == 0;
    CHECK(zero);
    CHECK(nf::arena::stats().in_use >= before.in_use + (size_t(6) << 20));
    nf::arena::free(big);
    nf::arena::free_pages(pages);
    CHECK(nf::arena::stats().in_use == before.in_use);
    // Alinhamento maior que 16.
    void* aligned = nf::arena::alloc(100, 256);
    CHECK((reinterpret_cast<uintptr_t>(aligned) & 255) == 0);
    nf::arena::free(aligned);
}

namespace {
int on_stack(void* arg) {
    int local = *static_cast<int*>(arg);
    return nf::arena::in_window(&local) ? local + 1 : -1;
}
}  // namespace

TEST(arena_game_stack_runs_inside_the_window) {
    int value = 41;
    CHECK(nf::arena::run_on_game_stack(on_stack, &value, size_t(1) << 20) == 42);
}

#endif
