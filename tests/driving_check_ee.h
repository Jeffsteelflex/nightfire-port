#pragma once
// nf_driving_check, parte 4: as funções originais do EE (código MIPS do DRIVING.ELF) no interpretador MIPS do port,
// com a VU0 ligada (COP2 em macro e vcallms), lado a lado com o porte em C++ (tests/driving_check_ee.cpp).

#include <cstdint>
#include <vector>

namespace nf {
class Elf;
}

namespace drvcheck {

// Devolve o número de comparações que falharam (0: tudo bit a bit).
int run_ee_checks(const nf::Elf& elf, const std::vector<uint8_t>& physics_program, int cases, uint32_t seed);

}  // namespace drvcheck
