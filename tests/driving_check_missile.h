#pragma once
// nf_driving_check, parte 18 (continuação; tests/driving_check_missile.cpp): o corpo simples, as explosões, o míssil, o
// choque de um segmento com o mundo e o tiro (EFireWeapon) do DRIVING.ELF, o código ORIGINAL no interpretador MIPS
// (com a VU0 em modo macro) lado a lado com o porte. docs/driving.md seção 22.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: os casos sorteados de cada tipo.
int run_missile_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed);

}  // namespace drvcheck
