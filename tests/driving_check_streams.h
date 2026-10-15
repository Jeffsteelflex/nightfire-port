#pragma once
// nf_driving_check, parte 23: os fluxos das missões de carro (tests/driving_check_streams.cpp): o mixador do EE, os
// fluxos do SND e o tocador de pacotes, o AStream e o AFader e os eventos de som, o código ORIGINAL no interpretador
// MIPS (com a VU0) lado a lado com o porte (driving/sound/snd_mix.h, snd_stream.h, audio_stream.h).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam (0: tudo igual). `cases`: casos sorteados de cada tipo.
int run_stream_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed);

}  // namespace drvcheck
