// Implementação para PC da interface de plataforma psi*.

#include "platform/psi.h"

#include <windows.h>

#include <cstring>

uint32_t VBlankCount = 0;

namespace {
uint32_t RealTimeCount = 0;
LARGE_INTEGER g_start{}, g_freq{};
double g_vblanks = 0.0;      // o relógio com a fração do VBlank em curso (a interpolação entre ticks)
bool g_fixed_clock = false;  // psi_set_clock_vblanks: o relógio simulado (--fixed-video-hz)
}  // namespace

void psi_update_clock() {
    if (g_fixed_clock) return;
    if (g_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_start);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // O jogo trata o VBlank como 60 Hz (psiGetTimeIn100ths multiplica por 10/6).
    const int64_t ticks = now.QuadPart - g_start.QuadPart;
    VBlankCount = uint32_t(ticks * 60 / g_freq.QuadPart);
    g_vblanks = double(ticks) * 60.0 / double(g_freq.QuadPart);
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
    if (MallocBlock != nullptr) VirtualFree(MallocBlock, 0, MEM_RELEASE);
    MallocBlock = nullptr;
    MallocBlock = VirtualAlloc(nullptr, psiMemHeapSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (MallocBlock == nullptr) {
        MessageBoxW(nullptr, L"Não foi possível reservar o heap do jogo.", L"Nightfire", MB_ICONERROR);
        ExitProcess(1);
    }
    PS2HeapSize = psiMemHeapSize & ~0xfu;
    *size = PS2HeapSize;
    *heap = static_cast<uint8_t*>(MallocBlock);
}

// [decomp] 0x00120668 psiMem_Info
void psiMem_Info(MEMINFO_tag*) {}

