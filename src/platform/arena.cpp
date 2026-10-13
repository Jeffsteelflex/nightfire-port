// A arena da versão de 64 bits. Ver arena.h e ptr32.h.
//
// A região é dividida em pedaços de 64 KB. Um pedaço é livre, o início ou a continuação de um bloco grande (vários
// pedaços seguidos, achados pelo primeiro que serve num mapa de bits), ou uma lâmina de uma classe de tamanho do
// alocador pequeno (blocos de até 32 KB, listas livres por classe, sem cabeçalho: a classe sai do pedaço). Os pedaços
// só são comprometidos quando usados; um bloco grande liberado com 1 MB ou mais volta a ser só reservado.
//
// Daqui não sai nenhuma chamada ao sistema: a memória virtual, a trava e o fim do processo são de platform/os.h, e a
// troca de pilha do jogo é do backend (arena_win32.cpp: o TEB, o MASM e o SEH do Windows).

#include "platform/arena.h"

#if NF_PTR64

#include <cstdio>
#include <cstring>
#include <new>

#include "platform/compiler.h"
#include "platform/crash_report.h"
#include "platform/os.h"

namespace nf {

[[noreturn]] void p32_out_of_window(const void* p) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(kP32Base);
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "ponteiro %p fora da janela dos campos de 32 bits do jogo [%p, %p): guarde o objeto na arena "
                  "(platform/arena.h)",
                  p, reinterpret_cast<void*>(base), reinterpret_cast<void*>(base + 0x100000000ull));
    crash_report_here(msg);
    std::fflush(stderr);
    os::exit_process(3);
}

namespace arena {
namespace {

constexpr size_t kChunk = size_t(64) << 10;
constexpr uint32_t kMaxChunks = 0x10000;  // 4 GB / 64 KB
constexpr uint32_t kNone = 0xffffffffu;
constexpr uint8_t kFree = 0;
constexpr uint8_t kLargeHead = 0xfe;
constexpr uint8_t kLargeBody = 0xfd;
constexpr uint32_t kDecommitChunks = 16;  // blocos grandes de 1 MB ou mais voltam a ser só reservados

// Classes do alocador pequeno (múltiplos de 16; o índice + 1 é o "kind" do pedaço).
constexpr uint32_t kClassSizes[] = {16,    32,    48,    64,    80,    96,    112,   128,   160,   192,   224,
                                    256,   320,   384,   448,   512,   640,   768,   896,   1024,  1280,  1536,
                                    1792,  2048,  2560,  3072,  3584,  4096,  5120,  6144,  7168,  8192,  10240,
                                    12288, 14336, 16384, 20480, 24576, 28672, 32768};
constexpr uint32_t kClassCount = sizeof(kClassSizes) / sizeof(kClassSizes[0]);
constexpr uint32_t kSmallMax = 32768;

os::Mutex g_lock;
bool g_ready = false;
uintptr_t g_begin = 0, g_end = 0;
uint32_t g_chunks = 0;
uint8_t g_kind[kMaxChunks];
uint32_t g_run[kMaxChunks];               // no início de um bloco grande: o número de pedaços
uint64_t g_free_bits[kMaxChunks / 64];    // 1 = livre
uint64_t g_commit_bits[kMaxChunks / 64];  // 1 = comprometido
uint32_t g_first_free = 0;                // nenhum pedaço livre abaixo deste
uint8_t g_class_of[kSmallMax / 16 + 1];   // (tamanho + 15) / 16 -> classe

struct ClassState {
    void* free_list = nullptr;
    uintptr_t bump = 0, bump_end = 0;
};
ClassState g_class[kClassCount];
Stats g_stats;

struct Lock {
    Lock() { g_lock.lock(); }
    ~Lock() { g_lock.unlock(); }
};

bool bit(const uint64_t* bits, uint32_t i) { return (bits[i >> 6] >> (i & 63)) & 1; }
void set_bit(uint64_t* bits, uint32_t i) { bits[i >> 6] |= uint64_t(1) << (i & 63); }
void clear_bit(uint64_t* bits, uint32_t i) { bits[i >> 6] &= ~(uint64_t(1) << (i & 63)); }

[[noreturn]] void fatal(const char* what) {
    std::fprintf(stderr, "arena: %s\n", what);
    std::fflush(stderr);
    os::show_error_narrow("Nightfire (64 bits)", what);
    os::exit_process(3);
}

void init_locked() {
    if (g_ready) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(kP32Base);
    const uintptr_t window_end = base + 0x100000000ull;
    const uintptr_t at = (os::image_end() + kChunk - 1) & ~(kChunk - 1);
    // A maior região livre da janela depois da imagem.
    uintptr_t best = 0, best_size = 0;
    os::vm_largest_free(at, window_end, kChunk, &best, &best_size);
    if (best_size < (size_t(256) << 20)) fatal("não há 256 MB livres na janela de 4 GB depois da imagem do programa");
    if (!os::vm_reserve_at(best, best_size)) fatal("não consegui reservar a arena");
    g_begin = best;
    g_end = best + best_size;
    g_chunks = uint32_t(best_size / kChunk);
    std::memset(g_free_bits, 0, sizeof(g_free_bits));
    for (uint32_t i = 0; i < g_chunks; ++i) set_bit(g_free_bits, i);
    for (uint32_t s = 0, c = 0; s <= kSmallMax / 16; ++s) {
        while (kClassSizes[c] < s * 16) ++c;
        g_class_of[s] = uint8_t(c);
    }
    g_stats.window_base = base;
    g_stats.begin = g_begin;
    g_stats.end = g_end;
    g_stats.chunks_total = g_chunks;
    g_ready = true;
}

uint32_t chunk_of(uintptr_t p) { return uint32_t((p - g_begin) / kChunk); }
uintptr_t chunk_addr(uint32_t c) { return g_begin + uintptr_t(c) * kChunk; }

// O próximo pedaço livre a partir de i (kNone: nenhum).
uint32_t next_free(uint32_t i) {
    while (i < g_chunks) {
        const uint64_t w = g_free_bits[i >> 6] >> (i & 63);
        if (w != 0) {
            i += ctz64(w);
            return i < g_chunks ? i : kNone;
        }
        i = (i | 63) + 1;
    }
    return kNone;
}

void commit(uint32_t first, uint32_t count) {
    uint32_t i = first;
    const uint32_t end = first + count;
    while (i < end) {
        if (bit(g_commit_bits, i)) {
            ++i;
            continue;
        }
        uint32_t j = i;
        while (j < end && !bit(g_commit_bits, j)) ++j;
        if (!os::vm_commit(chunk_addr(i), size_t(j - i) * kChunk)) throw std::bad_alloc();
        for (uint32_t k = i; k < j; ++k) set_bit(g_commit_bits, k);
        g_stats.committed += uint64_t(j - i) * kChunk;
        i = j;
    }
}

// n pedaços seguidos (o primeiro que serve), comprometidos. Devolve o índice do primeiro.
uint32_t take_run(uint32_t n) {
    uint32_t i = next_free(g_first_free);
    while (i != kNone && i + n <= g_chunks) {
        uint32_t j = i;
        while (j < i + n && bit(g_free_bits, j)) ++j;
        if (j == i + n) {
            commit(i, n);
            for (uint32_t k = i; k < i + n; ++k) {
                clear_bit(g_free_bits, k);
                g_kind[k] = kLargeBody;
            }
            g_kind[i] = kLargeHead;
            g_run[i] = n;
            if (i <= g_first_free) {
                const uint32_t f = next_free(i + n);
                g_first_free = f == kNone ? g_chunks : f;
            }
            g_stats.chunks_used += n;
            return i;
        }
        i = next_free(j);
    }
    throw std::bad_alloc();
}

void give_run(uint32_t first) {
    const uint32_t n = g_run[first];
    for (uint32_t k = first; k < first + n; ++k) {
        set_bit(g_free_bits, k);
        g_kind[k] = kFree;
    }
    g_run[first] = 0;
    if (n >= kDecommitChunks) {
        os::vm_decommit(chunk_addr(first), size_t(n) * kChunk);
        for (uint32_t k = first; k < first + n; ++k) clear_bit(g_commit_bits, k);
        g_stats.committed -= uint64_t(n) * kChunk;
    }
    if (first < g_first_free) g_first_free = first;
    g_stats.chunks_used -= n;
}

void* alloc_locked(size_t size, size_t align) {
    init_locked();
    ++g_stats.allocations;
    if (size == 0) size = 1;
    if (size <= kSmallMax && align <= 16) {
        const uint32_t c = g_class_of[(size + 15) / 16];
        ClassState& cs = g_class[c];
        void* p = cs.free_list;
        if (p != nullptr) {
            cs.free_list = *static_cast<void**>(p);
        } else {
            if (cs.bump + kClassSizes[c] > cs.bump_end) {
                const uint32_t k = take_run(1);
                g_kind[k] = uint8_t(c + 1);
                g_run[k] = 0;
                cs.bump = chunk_addr(k);
                cs.bump_end = cs.bump + kChunk;
            }
            p = reinterpret_cast<void*>(cs.bump);
            cs.bump += kClassSizes[c];
        }
        g_stats.in_use += kClassSizes[c];
        if (g_stats.in_use > g_stats.peak) g_stats.peak = g_stats.in_use;
        return p;
    }
    if (align > kChunk) throw std::bad_alloc();
    const uint32_t n = uint32_t((size + kChunk - 1) / kChunk);
    const uint32_t k = take_run(n);
    g_stats.in_use += uint64_t(n) * kChunk;
    if (g_stats.in_use > g_stats.peak) g_stats.peak = g_stats.in_use;
    return reinterpret_cast<void*>(chunk_addr(k));
}

void free_locked(void* p) {
    if (p == nullptr) return;
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (!g_ready || a < g_begin || a >= g_end) fatal("free de um endereço fora da arena");
    const uint32_t k = chunk_of(a);
    const uint8_t kind = g_kind[k];
    if (kind >= 1 && kind <= kClassCount) {
        ClassState& cs = g_class[kind - 1];
        *static_cast<void**>(p) = cs.free_list;
        cs.free_list = p;
        g_stats.in_use -= kClassSizes[kind - 1];
        return;
    }
    if (kind != kLargeHead || a != chunk_addr(k)) fatal("free de um endereço que não é início de bloco");
    g_stats.in_use -= uint64_t(g_run[k]) * kChunk;
    give_run(k);
}

}  // namespace

void init() {
    Lock lock;
    init_locked();
}

bool in_window(const void* p) {
    const uintptr_t d = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(kP32Base);
    return p != nullptr && d < 0x100000000ull;
}

void* alloc(size_t size, size_t align) {
    Lock lock;
    return alloc_locked(size, align);
}

void free(void* p) {
    Lock lock;
    free_locked(p);
}

size_t usable_size(const void* p) {
    Lock lock;
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (!g_ready || a < g_begin || a >= g_end) return 0;
    const uint32_t k = chunk_of(a);
    const uint8_t kind = g_kind[k];
    if (kind >= 1 && kind <= kClassCount) return kClassSizes[kind - 1];
    return kind == kLargeHead ? size_t(g_run[k]) * kChunk : 0;
}

void* alloc_pages(size_t bytes) {
    Lock lock;
    init_locked();
    ++g_stats.allocations;
    const uint32_t n = uint32_t((bytes + kChunk - 1) / kChunk);
    const uint32_t k = take_run(n == 0 ? 1 : n);
    g_stats.in_use += uint64_t(g_run[k]) * kChunk;
    if (g_stats.in_use > g_stats.peak) g_stats.peak = g_stats.in_use;
    void* p = reinterpret_cast<void*>(chunk_addr(k));
    std::memset(p, 0, size_t(g_run[k]) * kChunk);  // como o VirtualAlloc da versão de 32 bits
    return p;
}

void free_pages(void* p) { free(p); }

Stats stats() {
    Lock lock;
    init_locked();
    return g_stats;
}

int run_on_game_stack(int (*fn)(void*), void* arg, size_t stack_bytes) {
    // A pilha: os 64 KB de baixo ficam sem acesso (um estouro vira acesso inválido em vez de corromper a arena).
    stack_bytes = (stack_bytes + kChunk - 1) & ~(kChunk - 1);
    auto* bottom = static_cast<uint8_t*>(alloc_pages(stack_bytes + kChunk));
    os::vm_protect(bottom, kChunk, false);
    uint8_t* top = bottom + kChunk + stack_bytes;
    const int result = call_on_stack(fn, arg, bottom + kChunk, top);
    os::vm_protect(bottom, kChunk, true);
    free_pages(bottom);
    return result;
}

}  // namespace arena
}  // namespace nf

#endif
