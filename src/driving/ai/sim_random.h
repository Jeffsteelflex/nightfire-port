#pragma once
// O gerador de números da simulação (SimRandom do DRIVING.ELF, Noise.obj; docs/driving.md seção 18): o tráfego (os tipos
// de carro, as cores, onde nascem), o navegador (a escolha do próximo trecho) e os carros o usam pela Simulation (a
// primeira palavra da Simulation, 0x339190, aponta para ele).
//
// Peculiaridade mantida: o estado guarda só os 16 bits de baixo do produto (seed = (seed x mult) & 0xffff), então a
// sequência é curta; o número devolvido são os bits 8-23 do produto.

#include <cstdint>

namespace drv {

struct SimRandom {
    uint32_t seed;        // +0x00
    uint32_t last;        // +0x04 o último produto
    uint32_t multiplier;  // +0x08 123456789 (o construtor)
    uint32_t count;       // +0x0c quantos já saíram
};
static_assert(sizeof(SimRandom) == 16);

// [driving] 0x0017db68 SimRandom::SimRandom
void SimRandom_Construct(SimRandom& r);
// [driving] 0x0017dbb0 SimRandom::Reset: a semente 0xf874af01 e (multiplicador % 50) números jogados fora.
void SimRandom_Reset(SimRandom& r);
// [driving] 0x0017dc28 SimRandom::SimRandom_Generate: 0..65535.
uint32_t SimRandom_Generate(SimRandom& r);

// O da Simulation (port: um global; Simulation::Reset o reinicia).
SimRandom& sim_random();

// O gerador do render (RRandom, 0x333e60 a semente e 0x333e64 o multiplicador): a mesma conta do SimRandom, sem contador.
// O PS2 o gasta nos efeitos (partículas, clarões, destroços, raios, o dano visual) e no EFireRandomTrigger; o port, só
// no EFireRandomTrigger (driving/game/triggers.h), então a sequência difere da do PS2 depois do primeiro efeito.
struct RRandom {
    uint32_t seed;        // 0x333e60
    uint32_t multiplier;  // 0x333e64
};
// [driving] 0x001c08c0 RRandom::StartUp(int): a semente 0x3ade68b1, o multiplicador 123456789 e (n % 500) números
// jogados fora (o Render_InitLibRender passa 0x2e81a11).
void RRandom_StartUp(RRandom& r, uint32_t n);
// A conta inline dos usuários (EFireRandomTrigger 0x14c688): 0..65535, os bits 8-23 do produto; a semente fica com
// os 16 de baixo.
uint32_t RRandom_Generate(RRandom& r);
RRandom& render_random();

}  // namespace drv
