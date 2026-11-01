#pragma once
// nf_driving_check, parte 28 (`--so efeitos_ped`): os pedestres, os efeitos das instâncias, os clarões dos canos, as marcas
// dos tiros e os eventos que faltavam em Paris (docs/driving.md seção 32), o código ORIGINAL do DRIVING.ELF no
// interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// `vu0`: o programa da VU0 do .vutext (o de número 1, como as outras partes). Devolve as diferenças.
int run_fx_ped_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0, uint32_t vu0_size, int cases,
                      uint32_t seed);

}  // namespace drvcheck
