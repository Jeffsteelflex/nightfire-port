// Implementação para PC da interface de plataforma psi*.

#include "platform/psi.h"

#include "platform/arena.h"
#include "platform/os.h"

#include <cstring>

uint32_t VBlankCount = 0;

namespace {
uint32_t RealTimeCount = 0;
uint64_t g_start = 0, g_freq = 0;  // os::perf_counter e os::perf_frequency
double g_vblanks = 0.0;      // o relógio com a fração do VBlank em curso (a interpolação entre ticks)
bool g_fixed_clock = false;  // psi_set_clock_vblanks: o relógio simulado (--fixed-video-hz)
}  // namespace

void psi_update_clock() {
    if (g_fixed_clock) return;
    if (g_freq == 0) {
        g_freq = nf::os::perf_frequency();
        g_start = nf::os::perf_counter();
    }
    const uint64_t now = nf::os::perf_counter();
    // O jogo trata o VBlank como 60 Hz (psiGetTimeIn100ths multiplica por 10/6).
    const int64_t ticks = int64_t(now - g_start);
    VBlankCount = uint32_t(ticks * 60 / int64_t(g_freq));
    g_vblanks = double(ticks) * 60.0 / double(int64_t(g_freq));
}

double psi_clock_vblanks() { return g_vblanks; }

void psi_set_clock_vblanks(double vblanks) {
    g_fixed_clock = true;
    g_vblanks = vblanks;
    VBlankCount = uint32_t(vblanks);
}

// [decomp] 0x00109490 psiInitTimeIn100ths
void psiInitTimeIn100ths() { RealTimeCount = VBlankCount; }

// [decomp] 0x001094a0 psiGetTimeIn100ths
int psiGetTimeIn100ths() { return int(((VBlankCount - RealTimeCount) * 10) / 6); }

// [decomp] 0x0011f5c0 amemcpy
// O original copia em quadwords com lq/sq; no PC, memmove faz o mesmo.
void amemcpy(void* dst, const void* src, size_t size) { std::memmove(dst, src, size); }

// ---- heap ----

uint32_t psiMemHeapSize = 64u << 20;

namespace {
void* MallocBlock = nullptr;
uint32_t PS2HeapSize = 0;
}  // namespace

// [decomp] 0x0011f470 psiMem_Init
// Reimplementada: o original procura por busca binária o maior malloc que termina abaixo de
// 0x1fe0000 (o fim da RAM menos a pilha). Aqui o heap tem tamanho fixo.
void psiMem_Init(uint8_t** heap, uint32_t* size) {
#if NF_PTR64
    // 64 bits: o heap do jogo fica na arena, dentro da janela dos ponteiros de 32 bits (platform/arena.h); alinhado a
    // 64 KB e zerado como o VirtualAlloc da versão de 32 bits, então os blocos caem nos mesmos deslocamentos.
    if (MallocBlock != nullptr) nf::arena::free_pages(MallocBlock);
    MallocBlock = nf::arena::alloc_pages(psiMemHeapSize);
#else
    if (MallocBlock != nullptr) nf::os::vm_free(MallocBlock);
    MallocBlock = nullptr;
    MallocBlock = nf::os::vm_alloc(psiMemHeapSize);
#endif
    if (MallocBlock == nullptr) {
        nf::os::show_error(L"Nightfire", L"Não foi possível reservar o heap do jogo.");
        nf::os::exit_process(1);
    }
    PS2HeapSize = psiMemHeapSize & ~0xfu;
    *size = PS2HeapSize;
    *heap = static_cast<uint8_t*>(MallocBlock);
}

// [decomp] 0x00120668 psiMem_Info
void psiMem_Info(MEMINFO_tag*) {}

