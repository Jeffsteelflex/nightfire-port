#pragma once
// nf_driving_check, parte 15: a missão (tests/driving_check_mission.cpp; docs/driving.md seção 19). O código ORIGINAL do
// DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte: o SMissionManager construído e
// carregado com as regras e as configurações de Paris, o Update com estados sorteados (as regras, os cronômetros, os
// eventos que elas criam e os construtores que agem), a criação de cada evento das listas de Paris e os destrutores
// portados dos eventos da missão e do HUD.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: os casos sorteados de cada tipo.
int run_mission_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed);

}  // namespace drvcheck
