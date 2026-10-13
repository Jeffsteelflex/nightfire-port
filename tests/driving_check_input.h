#pragma once
// nf_driving_check, parte 10: a entrada do jogo (tests/driving_check_input.cpp): o PS2PadDevice, o InputToAction com
// os .def do controle e o IOModule::SendGameMessages ORIGINAIS no interpretador MIPS lado a lado com o porte
// (driving/game/input.h).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `frames`: quadros de controle sorteados por tabela.
int run_input_checks(const nf::Elf& elf, const nf::DiscSource& disc, int frames, uint32_t seed);

}  // namespace drvcheck
