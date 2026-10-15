#pragma once
// nf_driving_check, parte 21 (tests/driving_check_fx.cpp): os efeitos do DRIVING.ELF (o sistema de partículas, a galeria
// dos efeitos, as luzes das explosões e dos mísseis, as marcas de queimado, o rastro dos mísseis e os eventos de efeito),
// o código ORIGINAL no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte. docs/driving.md seção 25.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: os casos sorteados de cada tipo.
int run_fx_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                  int cases, uint32_t seed);

}  // namespace drvcheck
