#pragma once
// nf_driving_check, parte 18 (continuação): o motor de animação das instâncias (RAnimEngine) do DRIVING.ELF no
// interpretador MIPS lado a lado com o porte (driving/render/anim_engine.h), sobre o CARP do Vanquish copiado para a RAM
// emulada (tests/driving_check_anim.cpp).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: as operações sorteadas.
int run_anim_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                    int cases, uint32_t seed);

}  // namespace drvcheck
