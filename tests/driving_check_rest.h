#pragma once
// nf_driving_check, parte 25 (`--so restos`): os efeitos e os sons que faltavam em Paris (docs/driving.md seção 29), o
// código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0) lado a lado com o porte.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// `vu0`: o programa da VU0 do .vutext (o de número 1, a física, como as outras partes). Devolve as diferenças.
int run_rest_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0, uint32_t vu0_size, int cases,
                    uint32_t seed);

}  // namespace drvcheck
