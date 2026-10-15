#pragma once
// nf_driving_check, parte 18: as armas e os efeitos (tests/driving_check_weapons.cpp; docs/driving.md seção 22). O
// código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte: a tabela das
// armas do construtor estático, o SWeaponManager com estados e ações sorteados, os destrutores dos eventos das armas e
// dos power-ups, e o resto que o porte das armas e dos efeitos traz.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: os casos sorteados de cada tipo.
int run_weapons_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed);

}  // namespace drvcheck
