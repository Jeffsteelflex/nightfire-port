// O gerador da simulação (driving/ai/sim_random.h), do MIPS do DRIVING.ELF (Noise.obj).

#include "driving/ai/sim_random.h"

namespace drv {

namespace {
SimRandom g_random{0xf874af01u, 0, 0x75bcd15u, 0};
}  // namespace

SimRandom& sim_random() { return g_random; }

namespace {
RRandom g_rrandom{0xf874af01u, 1u};  // os valores do .data antes do StartUp
}  // namespace

RRandom& render_random() { return g_rrandom; }

// [driving] 0x001c08c0 RRandom::StartUp
void RRandom_StartUp(RRandom& r, uint32_t n) {
    r.multiplier = 0x75bcd15u;
    r.seed = 0x3ade68b1u;
    uint32_t left = n % 500u;  // divu
    if (left == 0) return;
    uint32_t v = 0x3ade68b1u;
    for (; left != 0; --left) v = (v * 0x75bcd15u) & 0xffffu;
    r.seed = v;
}

uint32_t RRandom_Generate(RRandom& r) {
    const uint32_t p = r.seed * r.multiplier;  // mult (os 32 bits de baixo)
    r.seed = p & 0xffffu;
    return (p >> 8) & 0xffffu;
}

// [driving] 0x0017db68 SimRandom::SimRandom
void SimRandom_Construct(SimRandom& r) { r.multiplier = 0x75bcd15u; }

// [driving] 0x0017dbb0 SimRandom::Reset
void SimRandom_Reset(SimRandom& r) {
    r.last = 0;
    r.seed = 0xf874af01u;
    r.count = 0;
    for (uint32_t n = r.multiplier % 50u; n != 0; --n) SimRandom_Generate(r);  // divu: sem sinal
}

// [driving] 0x0017dc28 SimRandom::SimRandom_Generate
uint32_t SimRandom_Generate(SimRandom& r) {
    const uint32_t p = r.seed * r.multiplier;  // mult (os 32 bits de baixo)
    r.count += 1;
    r.seed = p & 0xffffu;
    r.last = p;
    return (p >> 8) & 0xffffu;
}

}  // namespace drv
