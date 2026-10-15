#pragma once
// nf_driving_check, parte 13: o som das missões de carro (tests/driving_check_sound.cpp): o SND da EA, o decodificador
// de EA-XA e o jogo por cima, o código ORIGINAL no interpretador MIPS lado a lado com o porte (driving/sound/).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam (0: tudo igual). `cases`: casos sorteados de cada tipo.
int run_sound_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed);

}  // namespace drvcheck
