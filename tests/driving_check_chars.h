#pragma once
// nf_driving_check, parte 16: os personagens (tests/driving_check_chars.cpp): o código ORIGINAL da animação do
// DRIVING.ELF (a libeaglanima e o ActPoser) no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte de
// driving/anim/eagl_anim.h e driving/anim/actor.h, com os bancos de animação e os esqueletos do disco. docs/driving.md
// seção 20.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: o tamanho das conferências (casos por função).
int run_chars_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                     int cases, uint32_t seed);

}  // namespace drvcheck
