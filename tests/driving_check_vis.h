#pragma once
// nf_driving_check, parte 11: a visibilidade do mundo (tests/driving_check_vis.cpp): o microprograma VU0_culling
// original no interpretador da VU contra o porte à mão (driving/vu0_culling.h), e o frustum 2D, as cortinas, a árvore
// de cenas, o corte das instâncias e a ordem do desenho do código ORIGINAL do EE no interpretador MIPS (com a VU0) lado
// a lado com o porte (driving/world_vis.h), com câmeras andando pelas pistas do disco.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam (0: tudo igual). `program`: o VU0_culling (o terceiro programa do
// .vutext); `cases`: os casos sorteados de cada tipo; `frames`: as câmeras por pista.
int run_vis_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* program, uint32_t program_size,
                   int cases, int frames, uint32_t seed);

}  // namespace drvcheck
